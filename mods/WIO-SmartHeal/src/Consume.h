#pragma once

// === F4RD RELOCATIONS ========================================================
// This file resolves no addresses, but its Papyrus dispatch reaches a per-runtime
// ABI offset through the shared compat layer: WIO::Papyrus hands the game a
// BSTThreadScrapFunction whose impl pointer it reads at 0x18 on OG and 0x38 on
// NG/AE. That banner is in lib/commonlibf4rd/compat/WattzIO/Papyrus.h.
// =============================================================================

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "Inventory.h"
#include "Notify.h"
#include "PapyrusLog.h"
#include "PlayerState.h"
#include "Settings.h"
#include "Solver.h"

// Feeds the plan to the player one item at a time, re-checking between each.
//
// A queue rather than a loop. The vanilla Stimpak carries PlayAnimOnItemUse, so ten pushed
// through in one frame would have ten animations fighting over one animation graph. Re-checking
// between items also makes the plan a budget and an ordering rather than a fixed shopping list,
// which is what corrects for perks the solver cannot see in the item records. The re-check reads
// projected health, never current: almost every heal arrives over five to ten seconds, so
// current health one frame after a stimpak is unchanged.
//
// An item goes down one of two ways:
//
//   Immediate  ActorEquipManager::EquipObject or Actor::DrinkPotion. Synchronous and reliable,
//              but no use animation plays.
//   Animation  Papyrus Actor.EquipItem through the VM. The only call that reaches the game's
//              use-animation handler.
//
// Exactly one item per press takes the Animation path, the last one, because animations collide
// and the asynchronous path carries a verification cost (see g_verifyItem). Spending cheap items
// first means the precious one is what the re-check drops if the target is already met.
namespace SH::Consume
{
	namespace detail
	{
		struct Queued
		{
			RE::AlchemyItem* item = nullptr;
			std::string      name;
			std::uint32_t    count = 0;
			Solver::Reason   reason = Solver::Reason::kHealth;

			// Routing comes from the category, never PlayAnimOnItemUse: that keyword only says whether
			// vanilla animates the item, and an animation framework animates more than it covers.
			ItemCatalog::Category category = ItemCatalog::Category::kUnusable;

			// Log only: whether the item would animate with no animation mod installed.
			bool animates = false;

			// What the solver predicted one would heal, so the re-check can credit it the instant
			// it goes down. See g_creditHP.
			float hp = 0.0F;

			// The same for radiation, also used by the [RADS] measurement line.
			float rads = 0.0F;
		};

		// Use order, lowest first: radiation removers, then food and other consumables, then stimpaks,
		// so the most precious item is spent last and is the one the re-check drops. The preferred
		// category is forced past all of these, so it lands last and gets the animation.
		[[nodiscard]] inline int UseRank(ItemCatalog::Category a_category, Settings::PreferredLast a_preferred)
		{
			using C = ItemCatalog::Category;

			const bool preferred = [&]() {
				switch (a_preferred) {
				case Settings::PreferredLast::kStimpak:
					return a_category == C::kStimpak;
				case Settings::PreferredLast::kAid:
					return a_category == C::kAid;
				case Settings::PreferredLast::kAlcohol:
					return a_category == C::kAlcohol;
				case Settings::PreferredLast::kRadaway:
					return a_category == C::kRadaway;
				case Settings::PreferredLast::kFood:
					// All three food categories count as food here.
					return a_category == C::kFood || a_category == C::kSpecialFood ||
					       a_category == C::kIrradiatedFood;
				default:
					return false;
				}
			}();

			if (preferred) {
				return 3;
			}

			switch (a_category) {
			case C::kRadaway:
				return 0;
			case C::kStimpak:
				return 2;
			default:
				return 1;  // food of every kind, aid, alcohol
			}
		}

		inline std::vector<Queued> g_queue;
		inline std::size_t         g_next = 0;

		// The pause before the next item, as both a tick count and a deadline.
		inline std::int32_t                          g_cooldownTicks = 0;
		inline std::chrono::steady_clock::time_point g_resumeAt{};

		inline void Pause(std::int32_t a_ticks, std::int32_t a_milliseconds)
		{
			g_cooldownTicks = a_ticks;
			g_resumeAt = std::chrono::steady_clock::now() + std::chrono::milliseconds{ a_milliseconds };
		}

		[[nodiscard]] inline bool Paused()
		{
			if (g_cooldownTicks > 0) {
				--g_cooldownTicks;
				return true;
			}
			return std::chrono::steady_clock::now() < g_resumeAt;
		}

		[[nodiscard]] inline std::int64_t MillisSince(std::chrono::steady_clock::time_point a_when)
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - a_when)
			    .count();
		}

		// The health re-check reads active effects, which is perk-exact once they exist, but an item
		// swallowed two frames ago need not be listed yet, which would let a second stimpak follow the
		// first. So the queue keeps its own floor: where health stood when it started, plus what the
		// solver predicted of everything swallowed since. It takes whichever is higher, never the sum,
		// which would double-count once the engine catches up, and being a lower bound the error can
		// only be stopping one item early. Radiation needs no equivalent, 84 of the 88 vanilla
		// radiation effects being instantaneous and RadAway registering as pending immediately.
		inline float g_baselineHP = 0.0F;
		inline float g_creditHP = 0.0F;

		// What actually went down, for the notification once the queue drains. Separate from the plan,
		// which the re-check can undershoot.
		inline std::vector<std::pair<std::string, std::uint32_t>> g_consumed;

		// The queue drains before the Animation item's verification resolves, so announcing and
		// resetting are separate moments and the summary must not repeat while it is pending.
		inline bool g_announced = false;

		// Every wait here is a tick count and a wall-clock duration, and both must pass.
		//
		// Frame counts alone are wrong at high framerates: at 300+ fps a 20-frame gap collapses to
		// 60 ms, inside the VM's own latency, so a timeout could fire before the call ran and cause a
		// double consume. Time alone is not enough either, since some waits need at least one engine
		// update, which at 20 fps is 50 ms. Both gates together hold from 20 to 1000 fps.
		inline constexpr std::int32_t kMinTicksBetweenItems = 2;
		inline constexpr std::int32_t kMinGapMS = 33;

		// How long the immediate paths get to show a dropped stack before it is called a failure. They
		// act on the calling thread, so this only covers the engine publishing the inventory change.
		inline constexpr std::int32_t kMinVerifyTicks = 2;
		inline constexpr std::int32_t kImmediateVerifyMS = 250;

		// How long the Papyrus error log stays piped into this mod's log after a VM dispatch. VM
		// turnaround is 49-82 ms; a longer window than this scoops up unrelated Papyrus output.
		inline constexpr std::int32_t kPapyrusLogWindowMS = 500;

		// Deferred by at least one tick. A use request being accepted is not the same as an item being
		// used, so the stack is read before and after; no path steps the count down within the frame
		// that asked for it. Nothing counts as consumed until the stack drops, so the notification and
		// the healing credit both happen here.
		inline RE::AlchemyItem* g_verifyItem = nullptr;
		inline std::string      g_verifyName;
		inline std::uint32_t    g_verifyBefore = 0;
		inline float            g_verifyHP = 0.0F;
		inline float            g_verifyRads = 0.0F;

		// Ticks the pending use has been waiting, and when it was dispatched. The Animation path sits
		// on the VM's queue, so it needs a far longer grace period. See Settings::iAnimatedUseGraceMS.
		inline std::int32_t                                g_verifyWaited = 0;
		inline std::chrono::steady_clock::time_point       g_verifyDispatched{};

		// Already counted toward the notification. The Animation item is announced at dispatch
		// rather than at drop, so Verify must not count it again when the stack drops.
		inline bool g_verifyRecorded = false;

		// The player's radiation when the item went down, for the [RADS] measurement. The one axis
		// where prediction can be checked against outcome immediately, 84 of the 88 vanilla radiation
		// effects being instantaneous. Health cannot be checked this way.
		inline float g_radBefore = 0.0F;

		// Which function swallows the item. Only the last item of a press is eligible for anything
		// but kEquip. See Settings::iAnimatedUseMode.
		enum class Method
		{
			kEquip,        // ActorEquipManager::EquipObject
			kDrinkPotion,  // Actor::DrinkPotion, virtual 0x119
			kPapyrus       // Papyrus Actor.EquipItem, through the VM - the one that animates
		};

		// iAnimatedUseMode is a hidden debugging key selecting which call the Animation route
		// makes; bAnimateLastItem is the player-facing switch for whether there is an Animation
		// route at all. Only the one item flagged a_animated reaches past kEquip.
		[[nodiscard]] inline Method MethodFor(bool a_animated)
		{
			if (!a_animated || !Settings::bAnimateLastItem || Settings::iAnimatedUseMode == 0) {
				return Method::kEquip;
			}
			return Settings::iAnimatedUseMode == 2 ? Method::kPapyrus : Method::kDrinkPotion;
		}

		[[nodiscard]] inline std::string_view MethodName(Method a_method)
		{
			switch (a_method) {
			case Method::kDrinkPotion:
				return "DrinkPotion"sv;
			case Method::kPapyrus:
				return "Papyrus EquipItem"sv;
			default:
				return "EquipObject"sv;
			}
		}

		// Which route actually consumed the item, not which was chosen: when the animated route gives
		// up and the item goes down through DrinkPotion, the verification line must name DrinkPotion.
		// Atomic because GiveUpAndUseImmediately corrects it from the main thread while UseOne sets it
		// from the input thread.
		inline std::atomic<Method> g_verifyMethod{ Method::kEquip };

		// Called from Verify, once an item is known to have left the inventory.
		inline void RecordConsumed(const std::string& a_name)
		{
			for (auto& entry : g_consumed) {
				if (entry.first == a_name) {
					++entry.second;
					return;
				}
			}
			g_consumed.emplace_back(a_name, 1u);
		}

		// ActorEquipManager::EquipObject, the immediate path. Not RemoveItem + ModActorValue: the
		// engine's own equip path is what makes addiction rolls, Survival hooks, perk effects and
		// mod-added item scripts fire as they would by hand.
		//
		// iEquipMode selects the flag set: 0 applies immediately on this thread, 1 hands it to the
		// engine's equip queue. Neither animates.
		[[nodiscard]] inline bool UseViaEquip(RE::PlayerCharacter* a_player, RE::AlchemyItem* a_item)
		{
			const auto manager = RE::ActorEquipManager::GetSingleton();
			if (!manager) {
				return false;
			}

			const RE::BGSObjectInstance instance{ a_item, nullptr };
			const bool                  deferred = Settings::iEquipMode == 1;

			return manager->EquipObject(
				a_player,
				instance,
				0,          // stackID - the default stack
				1,          // number  - one at a time; see the queue rationale above
				nullptr,    // slot    - aid items have no equip slot
				deferred,   // queueEquip
				true,       // forceEquip
				true,       // playSounds - the player should hear what they just drank
				!deferred,  // applyNow
				false);     // locked
		}

		// Actor::DrinkPotion, virtual 0x119, declared by CommonLibF4RD on Actor itself, so an ordinary
		// virtual call with no relocation id. The full use path: a stimpak taken this way drops the
		// stack, puts healing in flight and clears `crippled`, so limb-restore effects fire too. It
		// does not animate. stackID 0 is the default stack, as UseViaEquip passes EquipObject.
		[[nodiscard]] inline bool UseViaDrinkPotion(RE::PlayerCharacter* a_player, RE::AlchemyItem* a_item)
		{
			return a_player->DrinkPotion(a_item, 0);
		}

		inline void GiveUpAndUseImmediately(RE::PlayerCharacter* a_player, RE::AlchemyItem* a_item,
			std::string_view a_why)
		{
			REX::WARN("Smart Heal Button: [CONSUME] cannot dispatch {} to Papyrus ({}) - using it the "
			          "immediate way instead, so it will not animate"sv,
				a_item->GetFullName(), a_why);
			if (UseViaDrinkPotion(a_player, a_item)) {
				// Correct the record before Verify reports it, or the verification line will name
				// the route that failed instead of the one that worked.
				g_verifyMethod.store(Method::kDrinkPotion);
			} else {
				REX::WARN("Smart Heal Button: [CONSUME] {} could not be used at all"sv, a_item->GetFullName());
			}
		}

		// How long to wait for the VM to materialise a Form object before giving up and using the item
		// without an animation.
		inline constexpr std::int32_t kBindWaitMS = 500;

		inline void DispatchEquipItem(RE::PlayerCharacter* a_player, RE::AlchemyItem* a_item,
			std::chrono::steady_clock::time_point a_deadline, bool a_askedForBinding);

		// An attempt that wants another go parks itself here rather than re-queueing: F4SE drains its
		// task queue in a loop, so a task added during the drain is picked up by the same drain and
		// would spin on the main thread for the whole budget, starving the VM of the chance to produce
		// the binding. Tick drives the retry instead. Atomic: written by the main thread, read by input.
		inline std::atomic<RE::AlchemyItem*> g_awaitItem{ nullptr };
		inline std::atomic<std::int64_t>     g_awaitDeadline{ 0 };

		inline void AwaitBinding(RE::AlchemyItem* a_item, std::chrono::steady_clock::time_point a_deadline)
		{
			g_awaitDeadline.store(a_deadline.time_since_epoch().count());
			g_awaitItem.store(a_item);
		}

		// The script shipped as Data\Scripts\WIO_SmartHeal.pex. Global-only and attached to nothing, so
		// it needs no plugin: the VM loads it by name.
		inline constexpr std::string_view kBridgeScript = "WIO_SmartHeal"sv;

		// The primary route. WIO_SmartHeal.UseItem(int) looks the item up and calls EquipItem from
		// inside Papyrus, where passing a Potion to EquipItem's Form parameter is an ordinary widening.
		// From C++ it is not: the VM refuses a Potion object outright, and a form that vanilla scripts
		// already hold as a Potion property (the Stimpak, Dirty Water) never gets a Form-typed object
		// for the route below to find. Passing a FormID sidesteps the whole problem.
		//
		// Returns false only when nothing was handed to the VM, so falling back cannot use the item
		// twice.
		[[nodiscard]] inline bool DispatchViaBridge(RE::BSScript::IVirtualMachine* a_vm, RE::AlchemyItem* a_item)
		{
			// The loading lookup, not the NoLoad one, so the first call pulls the script in from disk.
			// Failing here means the .pex is missing or did not load.
			RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> bridge;
			if (!a_vm->GetScriptObjectType(RE::BSFixedString{ kBridgeScript }, bridge) || !bridge) {
				REX::WARN("Smart Heal Button: [PAPYRUS] script {} not found - is Data\\Scripts\\{}.pex "
				          "installed? Trying the direct route for {}"sv,
					kBridgeScript, kBridgeScript, a_item->GetFullName());
				return false;
			}

			// Papyrus int is signed, so a FormID above 0x7FFFFFFF arrives negative. Game.GetForm takes
			// it back as unsigned.
			const auto formID = a_item->GetFormID();
			const bool queued = WIO::Papyrus::DispatchStaticCallRaw(a_vm,
				RE::BSFixedString{ kBridgeScript },
				"UseItem"sv,
				[formID](RE::BSScrapArray<RE::BSScript::Variable>& a_args) {
					a_args.resize(1);
					a_args[0] = static_cast<std::int32_t>(formID);
					return true;
				},
				RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>{});
			if (!queued) {
				REX::WARN("Smart Heal Button: [PAPYRUS] {}.UseItem was refused for {} - trying the direct "
				          "route"sv,
					kBridgeScript, a_item->GetFullName());
				return false;
			}

			if (Settings::debugLog) {
				REX::DEBUG("Smart Heal Button: [CONSUME] dispatching {}.UseItem({:08X}) for {}"sv,
					kBridgeScript, formID, a_item->GetFullName());
			}
			PapyrusLog::Arm(kPapyrusLogWindowMS);
			return true;
		}

		// Papyrus' own Actor.EquipItem, dispatched through the VM - the route that animates. It tries
		// DispatchViaBridge first and runs the direct route below only if that queued nothing. EquipItem
		// is not a thin wrapper over ActorEquipManager::EquipObject. A raw handle is not enough, since
		// the VM has to resolve `self` to a bound script object, so this looks the type up, finds an
		// already-bound object, and dispatches on the object overload.
		//
		// (akItem, abPreventRemoval=false, abSilent=true): removal must happen because the item is
		// being used, and silent suppresses the engine's own equip message in favour of this mod's
		// summary when the queue drains.
		//
		// Asynchronous, so the item leaves the inventory whenever the VM runs it. A dispatch that never
		// lands is a log line in Verify(), never a second use. Falling back to the immediate path is
		// only safe while setting the call up, which is why every early-out below happens before
		// anything is handed to the VM.
		//
		// Must run on the game's main thread.
		//
		// Never calls IVirtualMachine::CreateObject: a missing Form object is requested through
		// Game.GetForm instead. That rules out BSScriptUtil::PackVariable, whose find-or-create path
		// reaches CreateObject, so the lookup half is inlined by hand below.
		inline void DispatchEquipItem(RE::PlayerCharacter* a_player, RE::AlchemyItem* a_item,
			std::chrono::steady_clock::time_point a_deadline, bool a_askedForBinding)
		{
			const auto gameVM = RE::GameVM::GetSingleton();
			const auto vm = gameVM ? gameVM->GetVM() : nullptr;
			if (!vm) {
				GiveUpAndUseImmediately(a_player, a_item, "no Papyrus VM"sv);
				return;
			}

			// The bridge first. A retry (a_askedForBinding) is already on the direct route below, which
			// the bridge only falls through to when it queued nothing.
			if (!a_askedForBinding && DispatchViaBridge(vm.get(), a_item)) {
				return;
			}

			// Fetched by type id rather than the name "Actor", so a load order that extended the type
			// still resolves to what the VM registered.
			RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> typeInfo;
			if (!vm->GetScriptObjectType(RE::BSScript::GetVMTypeID<RE::Actor>(), typeInfo) ||
				!typeInfo) {
				GiveUpAndUseImmediately(a_player, a_item, "no Papyrus type info for Actor"sv);
				return;
			}

			auto&      policy = vm->GetObjectHandlePolicy();
			const auto handle = policy.GetHandleForObject(
				RE::BSScript::GetVMTypeID<RE::Actor>(), a_player);
			if (handle == policy.EmptyHandle()) {
				GiveUpAndUseImmediately(a_player, a_item, "no VM handle for the player"sv);
				return;
			}

			// The player has bound scripts in any normal game, so this effectively always succeeds.
			RE::BSTSmartPointer<RE::BSScript::Object> object;
			if (!vm->FindBoundObject(handle, typeInfo->name.c_str(), false, object, false) ||
				!object) {
				GiveUpAndUseImmediately(a_player, a_item, "the player has no bound Actor object"sv);
				return;
			}

			// Actor.psc declares `Function EquipItem(Form akItem, bool abPreventRemoval, bool abSilent)`,
			// so the argument's type has to be `Form` or the VM answers `Type mismatch for argument 1`.
			// CastObject cannot fix it: Variable::operator=(BSTSmartPointer<Object>) reads varType straight
			// off the object, so the type is decided at pack time below.
			RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> formType;
			if (!vm->GetScriptObjectType("Form"sv, formType) || !formType) {
				GiveUpAndUseImmediately(a_player, a_item, "no Papyrus type info for Form"sv);
				return;
			}

			// Packed as TESForm*, not AlchemyItem*. The object's type comes from GetVMTypeID<T>(), which is
			// T::FORM_ID: kALCH for an AlchemyItem gives the script type `Potion`, while the same pointer as
			// TESForm* selects kNONE, which the VM registers as `Form`.
			const auto itemHandle = policy.GetHandleForObject(
				RE::BSScript::GetVMTypeID<RE::TESForm>(), a_item);
			if (itemHandle == policy.EmptyHandle()) {
				GiveUpAndUseImmediately(a_player, a_item, "no VM handle for the item"sv);
				return;
			}

			// exactMatch = true: an already-bound `Potion` is no use, the VM comparing the object's own
			// type against the declared `Form` and refusing to widen.
			RE::BSTSmartPointer<RE::BSScript::Object> formArg;
			if (!vm->FindBoundObject(itemHandle, formType->name.c_str(), false, formArg, true) ||
				!formArg) {
				// No object yet. The VM materialises script objects lazily, so a form no script has touched
				// has none. Game.GetForm(int) returns a Form, so running it binds one through the engine's own
				// path; the return value is discarded and the binding is the point. If it does not work,
				// FindBoundObject keeps failing, the deadline expires, and the item is used without animation.
				if (!a_askedForBinding) {
					const auto formID = a_item->GetFormID();
					// Papyrus int is signed, so a FormID above 0x7FFFFFFF arrives negative. Game.GetForm
					// handles that.
					(void)WIO::Papyrus::DispatchStaticCallRaw(vm,
						"Game"sv,
						"GetForm"sv,
						[formID](RE::BSScrapArray<RE::BSScript::Variable>& a_args) {
							a_args.resize(1);
							a_args[0] = static_cast<std::int32_t>(formID);
							return true;
						},
						RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>{});

					if (Settings::debugLog) {
						REX::DEBUG("Smart Heal Button: [PAPYRUS] no bound Form object for {} - asked "
						          "Game.GetForm({:08X}) to create one"sv,
							a_item->GetFullName(), formID);
					}
				}

				// Polled rather than waited on: CommonLibF4RD declares ~IStackCallbackFunctor() and never
				// defines it, so there is no completion callback to hook. Tick retries once per frame
				// until the deadline; see AwaitBinding.
				if (std::chrono::steady_clock::now() < a_deadline) {
					AwaitBinding(a_item, a_deadline);
					return;
				}

				// exactMatch is false here, so any bound object is reported whatever its type.
				if (Settings::debugLog) {
					RE::BSTSmartPointer<RE::BSScript::Object> anyBound;
					if (vm->FindBoundObject(itemHandle, formType->name.c_str(), false, anyBound, false) &&
						anyBound) {
						const auto boundType = anyBound->GetTypeInfo();
						REX::DEBUG("Smart Heal Button: [PAPYRUS] {} does have a bound object, of type '{}' "
						          "- but not one typed exactly 'Form'"sv,
							a_item->GetFullName(), boundType ? boundType->name.c_str() : "?");
					} else {
						REX::DEBUG("Smart Heal Button: [PAPYRUS] {} has no bound object of any type"sv,
							a_item->GetFullName());
					}
				}

				GiveUpAndUseImmediately(a_player, a_item,
					"the VM never produced a bound Form object for it"sv);
				return;
			}

			// The type the argument actually has, not the one that was asked for. If this says
			// Potion, the binding did not take.
			const auto argTypeInfo = formArg->GetTypeInfo();
			const auto argType = argTypeInfo ? argTypeInfo->name.c_str() : "?";

			// The VM's own type tables, once, before the first dispatch.
			PapyrusLog::DumpTypes();

			// DispatchMethodCall's return value only says the call was queued, so everything that can go
			// wrong from here is reported by the VM alone. PapyrusLog pipes those errors into this log.
			if (Settings::debugLog) {
				// The elapsed figure includes one task-queue hop of roughly a frame even when nothing was
				// requested, so printing it unconditionally would label scheduling time as binding time.
				if (a_askedForBinding) {
					const auto waitedMS = kBindWaitMS -
					                      std::chrono::duration_cast<std::chrono::milliseconds>(
											  a_deadline - std::chrono::steady_clock::now())
					                          .count();
					REX::DEBUG("Smart Heal Button: [CONSUME] dispatching Actor.EquipItem on type '{}' "
					          "for {}, arg1 bound as '{}' (self {:016X}, Game.GetForm took "
					          "~{} ms)"sv,
						typeInfo->name.c_str(), a_item->GetFullName(), argType,
						static_cast<std::uint64_t>(handle), waitedMS);
				} else {
					REX::DEBUG("Smart Heal Button: [CONSUME] dispatching Actor.EquipItem on type '{}' "
					          "for {}, arg1 bound as '{}' (self {:016X}, already bound)"sv,
						typeInfo->name.c_str(), a_item->GetFullName(), argType,
						static_cast<std::uint64_t>(handle));
				}
			}
			PapyrusLog::Arm(kPapyrusLogWindowMS);

			(void)WIO::Papyrus::DispatchMethodCallRaw(vm,
				object,
				"EquipItem"sv,
				[formArg](RE::BSScrapArray<RE::BSScript::Variable>& a_args) {
					a_args.resize(3);
					a_args[0] = formArg;  // akItem, bound as Form above
					a_args[1] = false;    // abPreventRemoval - the item is being USED
					a_args[2] = true;     // abSilent - this mod prints its own summary
					return true;
				},
				RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>{});
		}

		// Measurement only; changes no behaviour.
		//
		// Lead Belly, Rad Resistant and Aquaboy work through the RadResistIngestion and
		// RadResistExposure actor values rather than perk entry points, applied when the effect lands
		// rather than when it is described, so an irradiated food's real rad gain is not in the item
		// record and the solver plans as though they do not exist. Radiation is the one axis that can
		// be checked immediately, so this prints prediction and outcome on one line.
		inline void MeasureRads(RE::PlayerCharacter& a_player)
		{
			// Only items that add radiation. RadAway removes its rads over five seconds, so measuring it
			// when the stack drops would print `observed` near zero against a `predicted` of -300.
			if (!Settings::debugLog || g_verifyRads <= 0.0F) {
				return;
			}

			const auto radsAV = Effects::RadsAV();
			const auto av = RE::ActorValue::GetSingleton();
			if (!radsAV || !av) {
				return;
			}

			const auto observed = a_player.GetActorValue(*radsAV) - g_radBefore;
			const auto resistIngestion = av->radIngestionResistance ?
			                                 a_player.GetActorValue(*av->radIngestionResistance) :
			                                 0.0F;
			const auto resistExposure = av->radExposureResistance ?
			                                a_player.GetActorValue(*av->radExposureResistance) :
			                                0.0F;

			REX::DEBUG("Smart Heal Button: [RADS] {} predicted={:+.1f} observed={:+.1f} "
			          "(ingestionResist={:.1f} exposureResist={:.1f} radsBefore={:.1f})"sv,
				g_verifyName, g_verifyRads, observed,
				resistIngestion, resistExposure, g_radBefore);
		}

		// The VM must not be touched from the input thread. Everything here runs from a vtable hook on
		// PlayerCamera's input receiver, which is fine for reading actor values and for the immediate
		// equip paths, but not for VM dispatch, which the VM's own thread is running alongside.
		//
		// F4SE's task interface runs the callable on the main thread a frame later, which costs nothing
		// because this path is already asynchronous. A true return only means the task was queued.
		[[nodiscard]] inline bool UseViaPapyrus(RE::PlayerCharacter* a_player, RE::AlchemyItem* a_item)
		{
			const auto task = F4SE::GetTaskInterface();
			if (!task) {
				REX::WARN("Smart Heal Button: [CONSUME] no F4SE task interface - cannot dispatch {}"sv,
					a_item->GetFullName());
				return false;
			}

			// Both pointers outlive the task: the player is a singleton and an AlchemyItem is a permanent
			// form. The deadline is computed once, so re-queued attempts share it.
			const auto deadline = std::chrono::steady_clock::now() +
			                      std::chrono::milliseconds{ kBindWaitMS };
			task->AddTask([a_player, a_item, deadline]() {
				DispatchEquipItem(a_player, a_item, deadline, false);
			});
			return true;
		}

		// Reads the stack again, at least one tick after the use was requested, and only then counts
		// the item as consumed.
		inline void Verify()
		{
			if (!g_verifyItem) {
				return;
			}

			const auto player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->inventoryList) {
				g_verifyItem = nullptr;
				return;
			}

			const auto item = g_verifyItem;
			const auto after = RE::GetItemCount(player->inventoryList, item);
			++g_verifyWaited;

			if (after < g_verifyBefore) {
				const auto elapsed = MillisSince(g_verifyDispatched);

				g_verifyItem = nullptr;

				// The Animation item was announced at dispatch, so counting it here would show it
				// twice.
				if (!g_verifyRecorded) {
					RecordConsumed(g_verifyName);
					// Credited whatever the reason: a reserved crippled-limb stimpak heals as much as a
					// health one, and leaving it uncounted is what lets a second stimpak follow it.
					g_creditHP += g_verifyHP;
				}
				g_verifyRecorded = false;

				if (Settings::debugLog) {
					// The elapsed time is the number that sizes iAnimatedUseGraceMS.
					REX::DEBUG("Smart Heal Button: [CONSUME] used {} via {} ({} -> {}) after {} tick(s), {} ms"sv,
						g_verifyName, MethodName(g_verifyMethod.load()), g_verifyBefore, after,
						g_verifyWaited, elapsed);
				}

				MeasureRads(*player);
				return;
			}

			// Still holding. Both gates must pass before calling it a failure; see
			// kMinTicksBetweenItems.
			const auto patienceMS = (g_verifyMethod.load() == Method::kPapyrus) ?
			                            Settings::iAnimatedUseGraceMS :
			                            kImmediateVerifyMS;
			if (g_verifyWaited < kMinVerifyTicks || MillisSince(g_verifyDispatched) < patienceMS) {
				return;
			}

			// Reports without repairing. The Papyrus call is asynchronous, so "it has not happened
			// yet" and "it is never going to happen" look identical from here. A retry landing
			// alongside the original spends a second item; a lost dispatch costs only one item's
			// healing and a press.
			REX::WARN("Smart Heal Button: [CONSUME] {} was dispatched via {} but the inventory count did "
			          "not drop ({} -> {}) after {} tick(s) / {} ms - not re-issued; the "
			          "notification may have named an item that was not used"sv,
				g_verifyName, MethodName(g_verifyMethod.load()), g_verifyBefore, after, g_verifyWaited,
				MillisSince(g_verifyDispatched));
			g_verifyItem = nullptr;
			g_verifyRecorded = false;
		}

		// a_animated selects the route: true is the Animation path (Papyrus), false is Immediate.
		// Exactly one item per press is called with true; see Tick.
		//
		// a_hp and a_rads are what the solver predicted one of these would do. They are parked on
		// the pending verification rather than applied here, because nothing counts as consumed
		// until the stack is seen to drop.
		[[nodiscard]] inline bool UseOne(RE::AlchemyItem* a_item, bool a_animated, float a_hp, float a_rads)
		{
			// Dry run: the solve, the queue and the per-item re-check all ran for real, and only
			// the swallowing is skipped. Returning true keeps the queue draining.
			auto method = MethodFor(a_animated);

			if (Settings::bDryRun) {
				REX::DEBUG("Smart Heal Button: [DRYRUN] would use {} via {}"sv,
					a_item ? a_item->GetFullName() : "<null>", MethodName(method));
				// Recorded here rather than through Verify, because no stack is going to drop.
				RecordConsumed(a_item && a_item->GetFullName() ? a_item->GetFullName() : "");
				g_creditHP += a_hp;
				return true;
			}

			const auto player = RE::PlayerCharacter::GetSingleton();
			if (!player || !a_item) {
				return false;
			}

			// Never send an item the player no longer has. The stack can change between the solve
			// and the item's turn in the queue.
			if (player->inventoryList && RE::GetItemCount(player->inventoryList, a_item) == 0) {
				return false;
			}

			const auto before = player->inventoryList ?
			                        RE::GetItemCount(player->inventoryList, a_item) :
			                        0u;

			bool accepted = false;
			switch (method) {
			case Method::kDrinkPotion:
				accepted = UseViaDrinkPotion(player, a_item);
				break;
			case Method::kPapyrus:
				// No task interface means the dispatch cannot be queued at all, so the item goes
				// down through DrinkPotion now, and the record names the route that consumed it.
				accepted = UseViaPapyrus(player, a_item);
				if (!accepted) {
					method = Method::kDrinkPotion;
					accepted = UseViaDrinkPotion(player, a_item);
				}
				break;
			default:
				accepted = UseViaEquip(player, a_item);
				break;
			}

			// Hand the check to the next tick rather than doing it here; see g_verifyItem.
			if (accepted) {
				g_verifyItem = a_item;
				g_verifyName = a_item->GetFullName() ? a_item->GetFullName() : "";
				g_verifyBefore = before;
				g_verifyMethod.store(method);
				g_verifyRecorded = false;
				g_verifyHP = a_hp;
				g_verifyRads = a_rads;
				g_verifyWaited = 0;
				g_verifyDispatched = std::chrono::steady_clock::now();

				if (const auto radsAV = Effects::RadsAV(); radsAV) {
					g_radBefore = player->GetActorValue(*radsAV);
				}
			} else if (Settings::debugLog) {
				REX::DEBUG("Smart Heal Button: [CONSUME] REFUSED {} via {} (count {})"sv,
					a_item->GetFullName(), MethodName(method), before);
			}

			return accepted;
		}

		// True once the axis this pick serves no longer needs anything. Reserved picks are never
		// dropped: a crippled limb needs its stimpak whatever the health bar says.
		[[nodiscard]] inline bool AlreadySatisfied(Solver::Reason a_reason)
		{
			if (a_reason == Solver::Reason::kReserved) {
				return false;
			}

			const auto state = PlayerState::Read();
			if (!state.valid) {
				return false;
			}

			if (a_reason == Solver::Reason::kRadiation) {
				return Solver::RadBand(state).Acceptable(state.ProjectedRads());
			}

			const auto ceiling = state.ProjectedReachableHP();
			// Whichever is further along: what the engine has published, or what this queue has
			// already fed the player. A max, never a sum. See g_creditHP.
			const auto floor = std::min(g_baselineHP + g_creditHP, ceiling);
			const auto effective = std::max(state.ProjectedHP(), floor);
			return Solver::HPBand(ceiling).Acceptable(effective);
		}

		// The HUD summary, shown once the queue has drained, which is before the Animation item's
		// verification resolves. The Animation path can take the better part of a second to remove
		// the item, so waiting for it would put the message after the moment had passed.
		inline void Announce()
		{
			if (g_announced) {
				return;
			}
			g_announced = true;

			if (g_consumed.empty()) {
				return;
			}

			std::string message = Notify::Prefix();
			for (const auto& [name, count] : g_consumed) {
				message += std::format(" x{} {},", count, Notify::CleanName(name));
			}
			if (message.back() == ',') {
				message.pop_back();
			}
			Notify::Show(message);
			// message already opens with Notify::Prefix(), so no second prefix here. Per-heal, so
			// DEBUG: the player-facing notification is the real output.
			REX::DEBUG("{}"sv, message);
		}

		// Everything back to rest. Separate from Announce, because the queue and the pending
		// verification do not end at the same moment.
		inline void Reset()
		{
			g_queue.clear();
			g_consumed.clear();
			g_next = 0;
			g_cooldownTicks = 0;
			g_resumeAt = {};
			g_baselineHP = 0.0F;
			g_creditHP = 0.0F;
			g_verifyItem = nullptr;
			g_verifyRecorded = false;
			g_announced = false;
			g_awaitItem.store(nullptr);
		}

		inline void Finish()
		{
			Announce();
			Reset();
		}
	}

	[[nodiscard]] inline bool Busy() { return detail::g_next < detail::g_queue.size(); }

	// Drops whatever is left. Called when a menu opens.
	inline void Abort()
	{
		if (Busy() && Settings::debugLog) {
			REX::DEBUG("Smart Heal Button: [CONSUME] aborted with {} entr(ies) left"sv,
				detail::g_queue.size() - detail::g_next);
		}
		detail::Finish();
	}

	// Queues a plan. Replaces anything still in flight: the newer press was made with better
	// information than the older one.
	inline void Begin(const Solver::Plan& a_plan, const PlayerState::State& a_state)
	{
		// A verification left over from the previous press belongs to that press: it was already
		// announced and credited there. g_awaitItem is left alone, because an item waiting on its
		// Form binding has been announced and still has to be used.
		detail::g_queue.clear();
		detail::g_consumed.clear();
		detail::g_next = 0;
		detail::g_cooldownTicks = 0;
		detail::g_resumeAt = {};
		detail::g_verifyItem = nullptr;
		detail::g_verifyRecorded = false;
		detail::g_announced = false;

		// The same reading the solver planned from, so the floor and the plan agree about where
		// the player started. Re-reading here would drift by a frame for no benefit.
		detail::g_baselineHP = a_state.ProjectedHP();
		detail::g_creditHP = 0.0F;

		for (const auto& pick : a_plan.picks) {
			if (!pick.item || !pick.item->entry || !pick.item->entry->item) {
				continue;
			}
			detail::g_queue.push_back(detail::Queued{
				pick.item->entry->item,
				pick.item->Name() ? pick.item->Name() : "",
				pick.count,
				pick.reason,
				pick.item->Category(),
				pick.item->entry->animatesOnUse,
				pick.item->hp,
				pick.item->rads });

			// Start the Alcohol Cooldown the moment alcohol is committed to this press, rather
			// than after the consume completes: the consume is asynchronous and may be verified
			// frames later.
			if (pick.item->Category() == ItemCatalog::Category::kAlcohol) {
				ItemCatalog::MarkAlcoholUsed();
			}
		}

		// Resolve Random once, so the whole press agrees about what it is aiming for. Chosen from
		// the categories actually in the plan rather than from all seven, because Random picking a
		// category the player is not spending would mean no preference at all.
		auto preferred = Settings::preferredLast;
		if (preferred == Settings::PreferredLast::kRandom && !detail::g_queue.empty()) {
			static std::mt19937 rng{ std::random_device{}() };
			const auto&         pickFrom = detail::g_queue[
                std::uniform_int_distribution<std::size_t>{ 0, detail::g_queue.size() - 1 }(rng)];
			switch (pickFrom.category) {
			case ItemCatalog::Category::kStimpak: preferred = Settings::PreferredLast::kStimpak; break;
			case ItemCatalog::Category::kAid:     preferred = Settings::PreferredLast::kAid; break;
			case ItemCatalog::Category::kAlcohol: preferred = Settings::PreferredLast::kAlcohol; break;
			case ItemCatalog::Category::kRadaway: preferred = Settings::PreferredLast::kRadaway; break;
			default:                              preferred = Settings::PreferredLast::kFood; break;
			}
		}

		// Sort into use order; see UseRank. Stable, so the solver's own ordering survives inside
		// each rank and reserved picks keep their place within their group.
		std::stable_sort(detail::g_queue.begin(), detail::g_queue.end(),
			[preferred](const detail::Queued& a_lhs, const detail::Queued& a_rhs) {
				return detail::UseRank(a_lhs.category, preferred) <
				       detail::UseRank(a_rhs.category, preferred);
			});

		if (Settings::debugLog) {
			std::string order;
			for (const auto& entry : detail::g_queue) {
				if (!order.empty()) {
					order += ", ";
				}
				order += std::format("x{} {}{}", entry.count, entry.name,
					entry.animates ? " (anim)" : "");
			}
			REX::DEBUG("Smart Heal Button: [CONSUME] queued {} entr(ies), preferredLast={} | order: {}"sv,
				detail::g_queue.size(), std::to_underlying(preferred), order);
		}
	}

	// One step. Called every frame from the input hook, which is the only per-frame callback this
	// mod already owns.
	inline void Tick()
	{
		using namespace detail;

		// One probe per frame for an item still waiting on its Form binding. This runs before the
		// pause gate and before the Busy() check, because the wait outlives the queue: the
		// animated item is the last of the press, so by the time it is waiting there is nothing
		// left to drain. See AwaitBinding for why this cannot live inside the task itself.
		if (auto* awaiting = g_awaitItem.exchange(nullptr); awaiting) {
			const auto deadline = std::chrono::steady_clock::time_point{
				std::chrono::steady_clock::duration{ g_awaitDeadline.load() }
			};
			const auto task = F4SE::GetTaskInterface();
			const auto player = RE::PlayerCharacter::GetSingleton();
			if (task && player) {
				task->AddTask([player, awaiting, deadline]() {
					DispatchEquipItem(player, awaiting, deadline, true);
				});
			}
		}

		if (!Busy() && !g_verifyItem) {
			return;
		}
		if (Paused()) {
			return;
		}

		// At least one tick has passed since the last item was requested, so the stack has had a
		// chance to move. See g_verifyItem for why this cannot happen inside UseOne.
		Verify();

		// The queue has drained. Announce immediately, so the player reads the summary while the
		// animation plays, but stay alive until the last item's verification resolves.
		if (!Busy()) {
			Announce();
			if (!g_verifyItem) {
				Reset();
			}
			return;
		}

		// The previous item has not been seen to leave the inventory yet. Using the next one now
		// would overwrite its verification, so it would never be counted or credited.
		if (g_verifyItem) {
			return;
		}

		auto& entry = g_queue[g_next];

		// Re-check before every single item, not just at the start of each entry. An entry of
		// three stimpaks can and should stop after the first if that one was enough.
		if (AlreadySatisfied(entry.reason)) {
			if (Settings::debugLog) {
				REX::DEBUG("Smart Heal Button: [CONSUME] {} already satisfied - skipping x{} {}"sv,
					entry.reason == Solver::Reason::kRadiation ? "radiation"sv : "health"sv,
					entry.count, entry.name);
			}
			// Everything else serving the same axis is equally unnecessary. Only the contiguous
			// run is skipped here, because Begin sorts by category and same-axis picks need not
			// sit together. The next Tick re-checks the new head and skips again.
			const auto reason = entry.reason;
			while (g_next < g_queue.size() && g_queue[g_next].reason == reason) {
				++g_next;
			}
			if (!Busy()) {
				Finish();
			}
			return;
		}

		// Exactly one item per press takes the Animation route: the final unit of the final entry.
		// Computed here rather than tagged in Begin, because the re-check above can drop entries
		// mid-plan, and because an entry of three stimpaks must spend two through the immediate
		// path and only the third through the animation.
		const bool lastItem = (g_next + 1 == g_queue.size()) && entry.count == 1;

		// Neither the notification nor the healing credit happens here for immediate items. Both
		// are done by Verify() once the stack has been seen to drop, still in time for the next
		// AlreadySatisfied call, since Verify runs above it in Tick.
		if (!UseOne(entry.item, lastItem, entry.hp, entry.rads)) {
			// Gone from the inventory, or the engine refused it. Drop the whole entry rather than
			// spinning on it.
			REX::WARN("Smart Heal Button: could not use {} - skipping the rest of that stack"sv, entry.name);
			entry.count = 1;
		} else if (lastItem && !Settings::bDryRun && g_verifyMethod == Method::kPapyrus) {
			// The Animation item is announced at dispatch rather than at drop, so the player can
			// read the message while the animation plays. A scoped exception to the rule that
			// nothing is reported until the stack moves: it covers one item, whose dispatch has
			// already been accepted.
			RecordConsumed(entry.name);
			g_creditHP += entry.hp;
			g_verifyRecorded = true;
		}

		if (--entry.count == 0) {
			++g_next;
		}

		// An animation fired two frames (~33ms) behind an equip is stomped by whatever that equip
		// did to the animation graph, so the last item gets a wider gap. Read off what comes next
		// rather than what just happened, because the gap that matters is the one in front of the
		// animation.
		const bool nextAnimates = Settings::bAnimateLastItem && Settings::iAnimatedUseMode != 0 &&
		                          Busy() && g_next + 1 == g_queue.size() &&
		                          g_queue[g_next].count == 1;
		Pause(kMinTicksBetweenItems,
			kMinGapMS + (nextAnimates ? Settings::iAnimatedUseDelayMS : 0));

		// Finish() is not called when the queue just emptied: the item that went
		// down this tick has a verification pending, which needs a tick to pass. The guard at the
		// top of Tick keeps this alive until that resolves.
	}
}
