#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

#include "PowerArmorExitRemap.h"
#include "Settings.h"

// Blocks Activate from starting an interaction with NPCs, or specifically with the player's
// companions, while in combat, and independently with companions whenever the weapon is drawn.
// Everything else - doors, terminals, containers, workbenches, corpse-looting - is untouched.
//
// Hooks TESNPC::Activate, the base form's vtable slot 0x40. TESObjectREFR::ActivateRef
// dispatches to the activated ref's base form at vtable +0x200, and a false return there aborts
// the activation: ActivateRef skips its success path and returns false. Doors, terminals and
// containers are different classes with different vtables, so a hook on TESNPC's slot cannot
// reach them.
namespace ARC::CombatActivateBlock
{
	namespace detail
	{
		// CurrentCompanionFaction: true for active companions, false for every other NPC and for
		// the player. Not PlayerTeammate, which also covers settlers and provisioners.
		inline constexpr std::uint32_t kCurrentCompanionFaction = 0x00023C01;

		// Live crosshair handle, published ~100/sec by Guard (below) and read by
		// layer 2 at the moment of a press. Lives here rather than on Guard so the
		// two can be defined in either order without a forward reference.
		inline std::atomic<std::uint32_t> s_crosshairHandle{ 0 };

		// Real-time stamp of the last moment the player was seen in combat, sampled by Guard's
		// PickRefUpdateEvent sink. Raw tick count so it fits in an atomic. Wall-clock rather than a
		// frame count, because the MCM option is expressed in seconds.
		inline std::atomic<std::int64_t> s_lastInCombatTicks{ 0 };

		// True while the player is in combat, and for iBlockExtendSeconds of real
		// time afterwards. Combat status drops for a moment between waves or when
		// the current target dies, so without this the block flickers off mid-fight
		// - exactly when a reflexive reload tap is most likely.
		[[nodiscard]] inline bool InCombatOrGrace(RE::PlayerCharacter* a_player)
		{
			if (a_player->IsInCombat()) {
				return true;
			}

			const auto extend = Settings::iBlockExtendSeconds;
			if (extend <= 0) {
				return false;
			}

			const auto last = s_lastInCombatTicks.load(std::memory_order_relaxed);
			if (last == 0) {
				return false;  // never seen in combat this session
			}

			const auto elapsed = std::chrono::steady_clock::now().time_since_epoch() -
			                     std::chrono::steady_clock::duration{ last };
			return elapsed < std::chrono::seconds{ extend };
		}

		[[nodiscard]] inline RE::TESFaction* CompanionFaction()
		{
			// Resolved fresh rather than cached. GetFormByID is a BSAutoReadLock-protected
			// hashmap lookup, and this runs only on real NPC activations, never per frame.
			return RE::TESForm::GetFormByID<RE::TESFaction>(kCurrentCompanionFaction);
		}
	}

	// === F4RD RELOCATIONS ========================================================
	//   Kind   Site                                          ID / RVA     +Off
	//   live   player base form's own vtable ptr (TESNPC)    none            -
	//   vfunc  TESObjectREFR::Activate                       slot 0x40       -
	//   live   PlayerControls::activateHandler's vtable ptr  none            -
	//   vfunc  BSInputEventUser::OnButtonEvent               slot 0x8        -
	//
	// Not RE::VTABLE::TESNPC[0]: that id resolves 0x788 bytes from the real vtable.
	//
	// Re-derive: count the virtuals in RE/T/TESObjectREFR.h up to Activate, and in
	// RE/B/BSInputEventUser.h up to OnButtonEvent.
	// =============================================================================

	// Mirrors Settings::iBlockNpcActivate.
	enum class Mode : std::int32_t
	{
		kOff = 0,
		kCompanions = 1,
		kAllNpcs = 2
	};

	class Hook
	{
	public:
		// Installed lazily from a live TESNPC, never from RE::VTABLE::TESNPC[0] - see
		// the relocations banner above. The player's own base form is a real TESNPC
		// (the Player NPC_ record), so this resolves as soon as a save is loaded.
		static bool TryInstall()
		{
			if (s_installed.load(std::memory_order_relaxed)) {
				return true;
			}

			const auto player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return false;
			}
			const auto base = player->GetObjectReference();
			if (!base || base->GetFormType() != RE::ENUM_FORM_ID::kNPC_) {
				return false;
			}

			// F4RD:live - vtable read from the live TESNPC, no id involved
			const auto liveVtable = *reinterpret_cast<std::uintptr_t*>(base);
			REL::Relocation<std::uintptr_t> vtbl{ liveVtable };
			// F4RD:vfunc - slot 0x40 = TESObjectREFR::Activate
			_original = vtbl.write_vfunc(0x40, &Hook::Activate);
			s_installed.store(true, std::memory_order_relaxed);

			return true;
		}

		// Public so layer 2 reuses the same predicate. Read live on every call, so an MCM change
		// takes effect on the next activation with no pause-menu round trip.
		[[nodiscard]] static bool ShouldBlockRef(RE::TESObjectREFR* a_target, RE::TESObjectREFR* a_actionRef)
		{
			const auto mode = static_cast<Mode>(Settings::iBlockNpcActivate);
			if (mode == Mode::kOff && !Settings::bBlockCompanionDrawn) {
				return false;
			}

			const auto player = RE::PlayerCharacter::GetSingleton();
			if (!player || !a_target) {
				return false;
			}

			// Only the player's own activations. Blocking NPC-on-NPC or script-driven
			// activation would break quests, AI packages and workshop logic.
			if (a_actionRef != static_cast<RE::TESObjectREFR*>(player)) {
				return false;
			}

			// Two independent conditions, ORed. The combat one honours the full
			// Off/Companions/All NPCs mode. The drawn-weapon one is companions-only, so
			// turning it on can never widen what the combat setting blocks.
			const bool combatApplies = (mode != Mode::kOff) && detail::InCombatOrGrace(player);
			const bool drawnApplies = Settings::bBlockCompanionDrawn && player->GetWeaponMagicDrawn();
			if (!combatApplies && !drawnApplies) {
				return false;
			}

			const auto actor = a_target->As<RE::Actor>();
			if (!actor) {
				return false;
			}

			// Corpse-looting must keep working mid-combat. TESNPC::Activate has its own
			// IsDead branch running the loot-container path, so passing dead actors
			// straight through is all that is needed.
			if (a_target->IsDead(false)) {
				return false;
			}

			// All NPCs can only ever arrive via the combat condition, so it is
			// checked against that one specifically. A drawn-weapon block that
			// reached here falls through to the companion test below.
			if (combatApplies && mode == Mode::kAllNpcs) {
				return true;
			}

			const auto faction = detail::CompanionFaction();
			return faction && actor->IsInFaction(faction);
		}

	private:
		// RE::TESForm::Activate, vtable slot 0x40.
		static bool Activate(RE::TESForm* a_this, RE::TESObjectREFR* a_itemActivated,
			RE::TESObjectREFR* a_actionRef, RE::TESBoundObject* a_objectToGet, std::int32_t a_count)
		{
			if (ShouldBlockRef(a_itemActivated, a_actionRef)) {
				return false;
			}
			return _original(a_this, a_itemActivated, a_actionRef, a_objectToGet, a_count);
		}

		using OriginalFunc = bool(RE::TESForm*, RE::TESObjectREFR*, RE::TESObjectREFR*, RE::TESBoundObject*, std::int32_t);
		static inline REL::Relocation<OriginalFunc*> _original;
		static inline std::atomic<bool>              s_installed{ false };
	};

	// Layer 2: the companion / command-mode path.
	//
	// The engine routes commandable actors straight to command-mode entry and everything else
	// through ActivateRef -> TESNPC::Activate, so companions never reach layer 1. Layer 2
	// intercepts one step higher, at ActivateHandler::OnButtonEvent (vtable slot 8), and declines
	// the whole gesture. It engages only when the crosshair is already on a qualifying living
	// actor, from RE::PickRefUpdateEvent.
	class ActivateHandlerHook
	{
	public:
		static bool TryInstall()
		{
			if (s_installed.load(std::memory_order_relaxed)) {
				return true;
			}
			const auto pcon = RE::PlayerControls::GetSingleton();
			if (!pcon || !pcon->activateHandler) {
				return false;
			}
			// ActivateHandler has no commonlibf4 header, but is plain single
			// inheritance from PlayerInputHandler with its base at offset 0, so the
			// object's first 8 bytes are its real vtable pointer.
			// F4RD:live - vtable read from the live ActivateHandler, no id involved
			const auto liveVtable = *reinterpret_cast<std::uintptr_t*>(pcon->activateHandler);
			REL::Relocation<std::uintptr_t> vtbl{ liveVtable };
			// F4RD:vfunc - slot 0x8 = BSInputEventUser::OnButtonEvent
			_original = vtbl.write_vfunc(0x8, &ActivateHandlerHook::OnButtonEvent);
			s_installed.store(true, std::memory_order_relaxed);

			// This class owns the slot-8 hook, so it is also the only thing that
			// holds the original. PowerArmorExitRemap needs to call it directly
			// (bypassing this hook) to hand the vanilla handler a Secondary Action
			// event - see PowerArmorExitRemap.h.
			PowerArmorExitRemap::SetForwarder(&ActivateHandlerHook::ForwardToVanilla);
			return true;
		}

		// Calls the real, unhooked ActivateHandler::OnButtonEvent. Public only so
		// PowerArmorExitRemap can be handed a pointer to it at install time.
		static void ForwardToVanilla(const RE::ButtonEvent* a_event)
		{
			const auto pcon = RE::PlayerControls::GetSingleton();
			if (!s_installed.load(std::memory_order_relaxed) || !pcon || !pcon->activateHandler) {
				return;
			}
			_original(reinterpret_cast<RE::BSInputEventUser*>(pcon->activateHandler), a_event);
		}

	private:
		static void OnButtonEvent(RE::BSInputEventUser* a_this, const RE::ButtonEvent* a_event)
		{
			// Latched at the press and held for the whole gesture. ActivateHandler is a
			// HeldStateHandler that registers a press and acts on release, so suppressing only one
			// half of a pair leaves its press-registered flags set and desyncs it.
			if (a_event) {
				if (a_event->QJustPressed()) {
					s_suppressGesture = ShouldSuppressNow();
				}
				if (s_suppressGesture) {
					// Release ends the gesture, so clear the latch after handling
					// it - otherwise a suppressed release would leave the latch set
					// into the next, unrelated press.
					if (RE::QReleased(*a_event)) {
						s_suppressGesture = false;
					}
					return;  // never forwarded to the real handler
				}

				// Drops held-repeat events only, and only in power armor with the option on, so the
				// press and release halves still flow through. See PowerArmorExitRemap.h.
				if (PowerArmorExitRemap::ShouldDropActivateEvent(a_event)) {
					return;
				}
			}
			_original(a_this, a_event);
		}

		[[nodiscard]] static bool ShouldSuppressNow()
		{
			// "Is the feature on at all" gate only. Which of the two conditions
			// applies is decided once, inside ShouldBlockRef. Duplicating that here
			// is what would let the two layers drift apart.
			if (static_cast<Mode>(Settings::iBlockNpcActivate) == Mode::kOff && !Settings::bBlockCompanionDrawn) {
				return false;
			}
			const auto player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return false;
			}

			RE::ObjectRefHandle handle{};
			*reinterpret_cast<std::uint32_t*>(&handle) = detail::s_crosshairHandle.load(std::memory_order_relaxed);
			const auto ref = handle.get();
			if (!ref) {
				return false;
			}
			// Reuses layer 1's own predicate verbatim, so the two layers can never
			// disagree about what qualifies. The player is the activator here, because
			// this is the player's own button press.
			return Hook::ShouldBlockRef(ref.get(), static_cast<RE::TESObjectREFR*>(player));
		}

		using OriginalFunc = void(RE::BSInputEventUser*, const RE::ButtonEvent*);
		static inline REL::Relocation<OriginalFunc*> _original;
		static inline std::atomic<bool>              s_installed{ false };
		static inline bool                           s_suppressGesture{ false };
	};

	// Publishes the live crosshair target for layer 2, and doubles as the retry
	// point for both vtable installs - once this sink is live it fires constantly,
	// so neither hook can miss its window.
	class Guard :
		public RE::BSTEventSink<RE::PickRefUpdateEvent>
	{
	public:
		static void Install()
		{
			if (s_installed.load(std::memory_order_relaxed)) {
				return;
			}
			const auto pc = RE::PlayerCharacter::GetSingleton();
			if (!pc) {
				return;
			}
			static Guard singleton;
			static_cast<RE::BSTEventSource<RE::PickRefUpdateEvent>*>(pc)->RegisterSink(&singleton);
			s_installed.store(true, std::memory_order_relaxed);
			REX::INFO("Activate/Reload Combo: combat NPC-activate block installed"sv);
		}

	private:
		RE::BSEventNotifyControl ProcessEvent(const RE::PickRefUpdateEvent& a_event, RE::BSTEventSource<RE::PickRefUpdateEvent>*) override
		{
			// This event has no commonlibf4 header. Its payload's first 4 bytes are
			// a live ObjectRefHandle, read in place because ObjectRefHandle has no
			// public raw-value constructor.
			detail::s_crosshairHandle.store(*reinterpret_cast<const std::uint32_t*>(&a_event), std::memory_order_relaxed);

			// Combat state is sampled here rather than at activation time: the grace window needs
			// to know when combat last held, which needs a continuously-running signal.
			if (const auto pc = RE::PlayerCharacter::GetSingleton(); pc && pc->IsInCombat()) {
				detail::s_lastInCombatTicks.store(
					std::chrono::steady_clock::now().time_since_epoch().count(),
					std::memory_order_relaxed);
			}

			Hook::TryInstall();
			ActivateHandlerHook::TryInstall();
			return RE::BSEventNotifyControl::kContinue;
		}

		static inline std::atomic<bool> s_installed{ false };
	};

	inline void Install()
	{
		Guard::Install();
		Hook::TryInstall();
		ActivateHandlerHook::TryInstall();
	}

	// Needs a live PlayerCharacter / PlayerControls, neither of which exists at kGameLoaded.
	// Install() therefore runs from kGameLoaded, every pause-menu close, and this HUDMenu-open
	// watcher, which covers a player who loads a save and walks straight into a fight.
	class GameplayStartWatcher :
		public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static void Install()
		{
			static GameplayStartWatcher singleton;
			const auto ui = RE::UI::GetSingleton();
			if (!ui) {
				REX::WARN("Activate/Reload Combo: UI singleton unavailable - watcher not installed"sv);
				return;
			}
			ui->GetEventSource<RE::MenuOpenCloseEvent>()->RegisterSink(&singleton);
		}

	private:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event.opening && a_event.menuName == "HUDMenu"sv) {
				CombatActivateBlock::Install();
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}
