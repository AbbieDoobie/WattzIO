#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <random>

#include "Settings.h"

namespace ZoomOffhand
{
	// Optional integration with Immersive HUD - iHUD (Nexus 20830): while the zoom is engaged,
	// reveal the compass or the whole HUD through iHUD's own pipeline, then hand control back.
	//
	// iHUD drives its widget from one int bitmask, produced by iHUDQuestScript.processStatusFlags()
	// and delivered as HUDFramework.SendMessage(widget, 1000 /*Command_Set_Flags*/, flags, nonce,
	// 0,0,0,0). That word is the only channel by which anything becomes visible.
	//
	// processStatusFlags() is pure and iHUD sends only when the word differs from its own cache, so
	// reading costs nothing and iHUD never corrects a value pushed from here. It re-evaluates on a
	// 1s timer and on combat and weapon-draw events, so its word moves while a bit is held on: ask
	// for the word, OR the bit onto it, re-ask often enough to re-assert on top of a new one, and
	// on release push iHUD's own word back verbatim. Nothing in iHUD is written.
	//
	// The linger is implemented here rather than left to iHUD's "Fade Time (s)", because only the
	// two toggle bits zero the widget's fade counters when they change. Fade Time is how long an
	// element stays visible after its condition ends, not an alpha fade. Nothing is ever pushed
	// that was not derived from a word iHUD just returned.
	class IHudReveal
	{
	public:
		static void Install()
		{
			// Transient state only. Form pointers and handles stay valid for the process, but a cached
			// flag word from a previous session is meaningless.
			s_phase = Phase::kIdle;
			s_sentBase = kNoBase;
			s_sentBit = 0;
			s_activeBit = 0;
			s_queryAge = 0.0F;
			s_lingerRemaining = 0.0F;
			s_lingerExtensions = 0;
			s_failures = 0;
			s_haveFlags.store(false);
			s_queryPending.store(false);
			s_badResult.store(false);
		}

		// a_zoomActive means the zoom is engaged right now: eligible, and the button actually held.
		static void Update(bool a_zoomActive)
		{
			const std::int32_t mode = Settings::iHudRevealMode.GetValue();
			const bool         desired = a_zoomActive && (mode == kModeCompass || mode == kModeAll);

			// Fast path and permanent off switch: option off, or not zooming with nothing outstanding.
			if (s_disabled || (!desired && s_phase == Phase::kIdle)) {
				return;
			}

			if (!Resolve() || !IsIHudActive()) {
				// iHUD absent, unresolvable, or switched off in its own MCM, in which case it has already
				// deactivated its widget. Nothing was sent that is not already accounted for.
				Standdown();
				return;
			}

			const auto  timer = RE::GetBSTimer();
			const float delta = timer ? timer->realTimeDelta : 0.0F;

			if (desired) {
				if (s_phase != Phase::kAsserting) {
					s_phase = Phase::kAsserting;
					s_pollAccum = kPollInterval;  // force a read on this very frame
				}
				s_activeBit = (mode == kModeAll) ? kStatusFlagToggleHUD : kStatusFlagToggleCompass;
				// Refreshed every held frame, as iHUD refreshes its own fade counter, so the linger starts
				// from a full Fade Time at release however long the button was held.
				s_lingerRemaining = FadeSeconds();
			} else if (s_phase == Phase::kAsserting || s_phase == Phase::kLingering) {
				// Released: keep the bit asserted for iHUD's own Fade Time, then drop it.
				if (s_phase == Phase::kAsserting) {
					s_phase = Phase::kLingering;
					s_lingerExtensions = 0;
				} else {
					s_lingerRemaining -= delta;
				}
				if (s_lingerRemaining <= 0.0F) {
					s_phase = Phase::kReleasing;
					s_activeBit = 0;
					s_releaseAccum = 0.0F;
					s_pollAccum = kPollInterval;  // one final authoritative read to restore from
				}
			}

			// A dispatch accepted but never called back would wedge polling and leave a stale word
			// being pushed, so it times out and counts as a failure.
			if (s_queryPending.load()) {
				s_queryAge += delta;
				if (s_queryAge >= kQueryTimeout) {
					s_queryPending.store(false);
					if (RegisterFailure()) {
						return;
					}
				}
			}

			// A result that is not the Int this function returns means iHUD's shape has changed.
			if (s_badResult.exchange(false) && RegisterFailure()) {
				return;
			}

			// iHUD's word as of this frame, snapshotted once for every decision below. The poll further
			// down is asynchronous, so its result lands on a later frame.
			const std::int32_t base = s_haveFlags.load() ? s_flags.load() : kNoBase;

			// Never drop the bit while one of iHUD's own lingers is still running: dropping a toggle bit
			// zeroes the widget's fade counters, cutting short a linger iHUD started when one of its own
			// conditions ended during the reveal. Losing a condition bit is the observable signal that
			// this happened, so the linger restarts and both expire together. Capped, so repeatedly
			// entering and leaving combat cannot extend the reveal forever.
			if (s_phase == Phase::kLingering &&
				base >= 0 && s_sentBase >= 0 &&
				((s_sentBase & ~base) & kConditionMask) != 0 &&
				s_lingerExtensions < kMaxLingerExtensions) {
				s_lingerRemaining = FadeSeconds();
				++s_lingerExtensions;
			}

			// A correct read clears the tally: it counts iHUD no longer looking like iHUD, not hiccups.
			if (s_goodResult.exchange(false)) {
				s_failures = 0;
			}

			// Keeps the snapshot fresh, which is what lets combat, sprint and the rest keep reacting: a
			// new word from iHUD is noticed within one poll and the bit re-asserted on top of it.
			s_pollAccum += delta;
			if (s_pollAccum >= kPollInterval && !s_queryPending.load()) {
				s_pollAccum = 0.0F;
				if (!QueryFlags() && RegisterFailure()) {
					return;
				}
			}

			// Latched rather than recomputed from `mode`, so changing the MCM dropdown mid-linger cannot
			// swap which bit is held.
			const std::int32_t bit = (s_phase == Phase::kAsserting || s_phase == Phase::kLingering) ?
			                             s_activeBit :
			                             0;

			// Only ever push a word derived from one iHUD returned, which is what keeps every failure
			// mode inert rather than sticky.
			if (base >= 0) {
				// Compared as a (base, bit) pair rather than on the OR'd result, which can
				// collide. Toggling the compass off while the compass bit is held leaves
				// base|bit unchanged even though iHUD pushed a different word.
				if (base != s_sentBase || bit != s_sentBit) {
					if (!SendFlags(base | bit)) {
						if (RegisterFailure()) {
							return;
						}
					} else {
						s_sentBase = base;
						s_sentBit = bit;
					}
				}
			}

			if (s_phase == Phase::kReleasing) {
				s_releaseAccum += delta;
				// Done once the final read has landed and been pushed. The grace window is a backstop against
				// a stalled VM, and only means the restore used a slightly older word.
				const bool restored = !s_queryPending.load() && s_sentBit == 0 && s_haveFlags.load();
				if (restored || s_releaseAccum >= kReleaseGrace) {
					s_phase = Phase::kIdle;
				}
			}
		}

	private:
		enum class Phase
		{
			kIdle,
			kAsserting,   // button held - this mod's bit is on iHUD's word
			kLingering,   // button released - bit deliberately still on, for iHUD's Fade Time
			kReleasing    // bit dropped - pushing iHUD's own word back and standing down
		};

		// iHUD's own STATUS_FLAG_* values, from iHUDQuestScript's compiled variable defaults and
		// cross-checked against the same names in iHUDController.swf.
		static constexpr std::int32_t kStatusFlagToggleHUD = 1;
		static constexpr std::int32_t kStatusFlagToggleCompass = 2;

		// ihudtouiapiscript's Command_Set_Flags. Same source.
		static constexpr std::int32_t kCommandSetFlags = 1000;

		// The condition bits feeding shouldShowCompass/Health/ActionPoints, whose ending starts a
		// linger of iHUD's own that this one must not cut short. 3rd-person and power-armor are
		// excluded, steering crosshair and PA handling rather than starting a fade countdown.
		static constexpr std::int32_t kConditionMask = 4 | 8 | 16;  // combat | weapon drawn | sprinting

		static constexpr std::int32_t kModeCompass = 1;
		static constexpr std::int32_t kModeAll = 2;

		// Nothing pushed yet this engagement. A real flag word is a small non-negative bitmask.
		static constexpr std::int32_t kNoBase = -1;

		// How often iHUD's word is re-read while a bit is held, and so the worst-case window in which
		// iHUD can push a word without that bit. A toggle bit changing zeroes the widget's fade
		// counters, making that window a visible blink. Only runs while the player holds the zoom.
		static constexpr float kPollInterval = 0.15F;
		static constexpr float kReleaseGrace = 1.00F;
		static constexpr float kQueryTimeout = 2.00F;

		// iHUD's own Fade Time slider is 0-6. Clamped, so a junk global cannot strand a bit asserted.
		static constexpr float kMaxFadeSeconds = 10.00F;

		// How many times an in-flight iHUD linger may restart this one, bounding the worst case at
		// release + 3x Fade Time.
		static constexpr std::int32_t kMaxLingerExtensions = 2;

		// Quests bind their scripts as they start up, so an early attempt can fail and then succeed.
		// Retried a handful of times, then given up on permanently.
		static constexpr float       kResolveRetryInterval = 5.00F;
		static constexpr std::int32_t kMaxResolveAttempts = 5;

		// Consecutive failed reads or sends before the integration switches itself off for the
		// session. Small, so it stops the moment iHUD stops looking like iHUD.
		static constexpr std::int32_t kMaxFailures = 3;

		static constexpr auto kWidgetID = "iHUDController.swf"sv;
		static constexpr auto kIHudScript = "iHUDQuestScript"sv;
		static constexpr auto kHudFrameworkScript = "HUDFramework"sv;

		// Receives processStatusFlags()'s return value and stores it, nothing more. It runs on a VM
		// thread, so every decision and send is left to Update() on the main thread, and these
		// atomics are the only cross-thread state.
		class FlagsCallback : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			void CallQueued() override {}

			// A call the VM accepted and then threw away is what a renamed or removed function looks
			// like from here. Transient cancellations are covered by the tally resetting on the next
			// good read, three consecutive failures being needed.
			void CallCanceled() override
			{
				s_badResult.store(true);
				s_queryPending.store(false);
			}
			void StartMultiDispatch() override {}
			void EndMultiDispatch() override {}

			void operator()(RE::BSScript::Variable a_result) override
			{
				// If a future iHUD changes what this returns, nothing is read and the mismatch is flagged.
				if (a_result.is<std::int32_t>()) {
					s_flags.store(RE::BSScript::get<std::int32_t>(a_result));
					s_haveFlags.store(true);
					s_goodResult.store(true);
				} else {
					s_badResult.store(true);
				}
				s_queryPending.store(false);
			}

			bool CanSave() override { return false; }
		};

		// True once the integration has switched itself off, so callers bail out.
		static bool RegisterFailure()
		{
			if (++s_failures < kMaxFailures) {
				return false;
			}
			Disable("iHUD integration stopped: iHUD did not respond as expected"sv);
			return true;
		}

		// Stops for the session, restoring anything still asserted first. A bit pushed and never
		// taken back would otherwise stay on the widget until iHUD's own state changed.
		static void Disable(std::string_view a_reason)
		{
			if (s_sentBit != 0 && s_haveFlags.load()) {
				static_cast<void>(SendFlags(s_flags.load()));  // failure ignored; stopping either way
			}
			s_disabled = true;
			Standdown();
			REX::INFO("Holstered Zoom: {}"sv, a_reason);
		}

		static void Standdown()
		{
			s_phase = Phase::kIdle;
			s_sentBase = kNoBase;
			s_sentBit = 0;
			s_activeBit = 0;
			s_pollAccum = 0.0F;
			s_releaseAccum = 0.0F;
			s_queryAge = 0.0F;
			s_lingerRemaining = 0.0F;
			s_lingerExtensions = 0;
		}

		[[nodiscard]] static RE::BSScript::IVirtualMachine* GetVM()
		{
			const auto game = RE::GameVM::GetSingleton();
			if (!game) {
				return nullptr;
			}
			const auto vm = game->GetVM();
			return vm.get();
		}

		// Locates a quest that actually has `a_scriptName` bound to it, trying the known FormID first
		// and falling back to a scan keyed on the script name, so a release that renumbers its forms
		// still resolves rather than binding to the wrong thing.
		[[nodiscard]] static bool FindQuestByScript(
			RE::BSScript::IVirtualMachine* a_vm,
			RE::TESQuest*                  a_hint,
			std::string_view               a_scriptName,
			std::size_t&                   a_outHandle)
		{
			const auto&    policy = a_vm->GetObjectHandlePolicy();
			constexpr auto questType = static_cast<std::uint32_t>(RE::ENUM_FORM_ID::kQUST);

			const auto tryQuest = [&](RE::TESQuest* a_quest) {
				if (!a_quest) {
					return false;
				}
				const std::size_t handle = policy.GetHandleForObject(questType, a_quest);
				if (!handle) {
					return false;
				}
				RE::BSTSmartPointer<RE::BSScript::Object> object;
				if (!a_vm->FindBoundObject(handle, a_scriptName.data(), false, object, false) || !object) {
					return false;
				}
				a_outHandle = handle;
				return true;
			};

			if (tryQuest(a_hint)) {
				return true;
			}

			const auto dataHandler = RE::TESDataHandler::GetSingleton();
			if (!dataHandler) {
				return false;
			}
			for (const auto quest : dataHandler->GetFormArray<RE::TESQuest>()) {
				if (quest != a_hint && tryQuest(quest)) {
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] static bool Resolve()
		{
			if (s_resolved) {
				return true;
			}

			const auto  timer = RE::GetBSTimer();
			const float delta = timer ? timer->realTimeDelta : 0.0F;
			s_resolveAccum += delta;
			if (s_resolveAttempts > 0 && s_resolveAccum < kResolveRetryInterval) {
				return false;
			}
			s_resolveAccum = 0.0F;
			++s_resolveAttempts;

			const auto vm = GetVM();
			const auto dataHandler = RE::TESDataHandler::GetSingleton();
			if (vm && dataHandler) {
				// 0x000F99 in "Immersive HUD.esp", the quest carrying iHUDQuestScript. A hint only:
				// FindQuestByScript verifies the script is bound there and scans if it is not.
				auto* const ihudHint = dataHandler->LookupForm<RE::TESQuest>(0x000F99, "Immersive HUD.esp"sv);

				// HUDFramework ships as an .esm, but its own GetInstance_Internal() checks for an .esp too.
				// 0x000F99 comes from that same function (Game.GetFormFromFile(3993,...)).
				auto* hfHint = dataHandler->LookupForm<RE::TESQuest>(0x000F99, "HUDFramework.esm"sv);
				if (!hfHint) {
					hfHint = dataHandler->LookupForm<RE::TESQuest>(0x000F99, "HUDFramework.esp"sv);
				}

				if (FindQuestByScript(vm, ihudHint, kIHudScript, s_ihudHandle) &&
					FindQuestByScript(vm, hfHint, kHudFrameworkScript, s_hudFrameworkHandle)) {
					// iHUD's master on/off switch and its "Fade Time (s)" slider, from the same two globals
					// iHUD's own MCM config binds those widgets to. Both optional; an absent one means that
					// feature is not applied.
					s_ihudActiveGlobal = dataHandler->LookupForm<RE::TESGlobal>(0x002678, "Immersive HUD.esp"sv);
					s_ihudFadeTimeGlobal = dataHandler->LookupForm<RE::TESGlobal>(0x012153, "Immersive HUD.esp"sv);
					s_resolved = true;
					REX::INFO("Holstered Zoom: iHUD detected - reveal integration active"sv);
					return true;
				}
			}

			if (s_resolveAttempts >= kMaxResolveAttempts) {
				// Almost always means the player does not have iHUD. Logged so the file explains why the
				// setting appears to do nothing.
				s_disabled = true;
				REX::INFO("Holstered Zoom: iHUD not found - HUD reveal option inactive"sv);
			}
			return false;
		}

		[[nodiscard]] static bool IsIHudActive()
		{
			return !s_ihudActiveGlobal || s_ihudActiveGlobal->value != 0.0F;
		}

		// Read live from iHUD's global, so changing it in iHUD's MCM applies immediately. An absent
		// global means no linger, degrading to an instant drop.
		[[nodiscard]] static float FadeSeconds()
		{
			if (!s_ihudFadeTimeGlobal) {
				return 0.0F;
			}
			return std::clamp(s_ihudFadeTimeGlobal->value, 0.0F, kMaxFadeSeconds);
		}

		// Read-only by construction, processStatusFlags's `statusFlags` being a function-local.
		// Returns false only if the call could not be dispatched at all.
		[[nodiscard]] static bool QueryFlags()
		{
			const auto vm = GetVM();
			if (!vm || !s_ihudHandle) {
				return false;
			}

			const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{ new FlagsCallback() };
			s_goodResult.store(false);
			s_queryPending.store(true);
			s_queryAge = 0.0F;
			if (!WIO::Papyrus::DispatchMethodCall(vm, s_ihudHandle, kIHudScript.data(), "processStatusFlags"sv, callback)) {
				s_queryPending.store(false);
				return false;
			}
			// A dispatch accepted but then failed inside the VM comes back through CallCanceled rather
			// than the result path. Update()'s pending-timeout and bad-result checks catch both.
			return true;
		}

		// The same function, widget and argument shape iHUD itself uses, so the widget cannot tell
		// this apart from iHUD's own update.
		[[nodiscard]] static bool SendFlags(std::int32_t a_flags)
		{
			const auto vm = GetVM();
			if (!vm || !s_hudFrameworkHandle) {
				return false;
			}

			// arg2 is a nonce in iHUD's own calls, mirrored exactly.
			const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> noCallback;
			return WIO::Papyrus::DispatchMethodCall(vm,
				s_hudFrameworkHandle,
				kHudFrameworkScript.data(),
				"SendMessage"sv,
				noCallback,
				RE::BSFixedString(kWidgetID),
				kCommandSetFlags,
				static_cast<float>(a_flags),
				Nonce(),
				0.0F,
				0.0F,
				0.0F,
				0.0F);
		}

		[[nodiscard]] static float Nonce()
		{
			static std::mt19937                          engine{ std::random_device{}() };
			static std::uniform_real_distribution<float> dist{ 0.0F, 1.0F };
			return dist(engine);
		}

		// Resolution results - stable for the process once found.
		static inline bool           s_resolved = false;
		static inline bool           s_disabled = false;
		static inline std::int32_t   s_resolveAttempts = 0;
		static inline float          s_resolveAccum = 0.0F;
		static inline RE::TESGlobal* s_ihudActiveGlobal = nullptr;
		static inline RE::TESGlobal* s_ihudFadeTimeGlobal = nullptr;
		static inline std::size_t    s_ihudHandle = 0;
		static inline std::size_t    s_hudFrameworkHandle = 0;

		// Main-thread only.
		static inline Phase        s_phase = Phase::kIdle;
		static inline std::int32_t s_sentBase = kNoBase;
		static inline std::int32_t s_sentBit = 0;
		static inline std::int32_t s_failures = 0;
		static inline std::int32_t s_activeBit = 0;
		static inline std::int32_t s_lingerExtensions = 0;
		static inline float        s_pollAccum = 0.0F;
		static inline float        s_releaseAccum = 0.0F;
		static inline float        s_queryAge = 0.0F;
		static inline float        s_lingerRemaining = 0.0F;

		// Written from the VM thread by FlagsCallback, read from the main thread by Update.
		static inline std::atomic<std::int32_t> s_flags{ 0 };
		static inline std::atomic<bool>         s_haveFlags{ false };
		static inline std::atomic<bool>         s_queryPending{ false };
		static inline std::atomic<bool>         s_badResult{ false };
		static inline std::atomic<bool>         s_goodResult{ false };
	};
}
