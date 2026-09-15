#pragma once

#include <atomic>

#include "AutoMoveStickCancel.h"
#include "DiagnosticLog.h"
#include "MouseLookFix.h"
#include "Settings.h"

namespace FalloutInputSwapper
{
	// Layer B: tracks which device most recently produced real input, with hysteresis so ambient
	// noise does not flip the "Auto" glyph device every frame. An observer, ShouldHandleEvent
	// always returning false, except that Settings::LookInputSource suppresses a blocked device's
	// camera-look contribution.
	//
	//   _glyphDevice                            hysteresis-smoothed, what "Auto" reads
	//   _lastGamepadActivity, _lastKBMActivity  per-category stamps, used by the preferred modes
	//                                           to tell whether the away device has gone quiet
	class DeviceTracker :
		public RE::BSInputEventUser
	{
	public:
		enum class GlyphDevice
		{
			kGamepad,
			kKBM,
		};

		[[nodiscard]] static DeviceTracker* GetSingleton()
		{
			static DeviceTracker singleton;
			return std::addressof(singleton);
		}

		// MenuControls::handlers sees every input event regardless of which device the engine
		// considers active. Idempotent.
		static void Install()
		{
			static bool s_installed = false;
			if (s_installed) {
				return;
			}

			const auto controls = RE::MenuControls::GetSingleton();
			if (!controls) {
				REX::WARN("Input Swapper: MenuControls unavailable - device tracker not installed yet, will retry"sv);
				return;
			}

			s_installed = true;
			controls->handlers.insert(controls->handlers.begin(), GetSingleton());
			REX::INFO("Input Swapper: device tracker installed"sv);
		}

		// "Auto" reads this: hysteresis-smoothed, persists until a real switch.
		[[nodiscard]] GlyphDevice GetAutoGlyphDevice() const noexcept { return _glyphDevice; }

		// a_preferred unless the other device produced real input within a_hysteresis seconds. Input
		// matching a_preferred wins instantly; a_hysteresis <= 0 means no grace period.
		[[nodiscard]] GlyphDevice GetPreferredModeDevice(GlyphDevice a_preferred, std::chrono::seconds a_hysteresis) const
		{
			const bool preferredIsGamepad = a_preferred == GlyphDevice::kGamepad;
			const auto preferredLast = preferredIsGamepad ? _lastGamepadActivity : _lastKBMActivity;
			const auto awayLast = preferredIsGamepad ? _lastKBMActivity : _lastGamepadActivity;
			const GlyphDevice away = preferredIsGamepad ? GlyphDevice::kKBM : GlyphDevice::kGamepad;

			if (preferredLast >= awayLast) {
				return a_preferred;  // preferred device used more recently (or never used away) - instant, no wait
			}

			if (a_hysteresis > std::chrono::seconds::zero() && (Clock::now() - awayLast) < a_hysteresis) {
				return away;  // still within the grace period
			}

			return a_preferred;  // grace period expired (or disabled) - fall back
		}

		// Read by the camera-look patches. Latched, not hysteresis-smoothed: which device produced the
		// most recent look-relevant input.
		[[nodiscard]] bool IsGamepadActiveLooking() const noexcept
		{
			return _gamepadActiveLooking.load(std::memory_order_relaxed);
		}

		// The engine calls this once per queued input event, not once per frame, so everything below
		// runs per event.
		bool ShouldHandleEvent(const RE::InputEvent* a_event) override
		{
			// Required alongside DisableKBMIgnore, not instead of it: with the NOP patches alone and this
			// removed, KBM stays gamepad-gated.
			if (const auto controlMap = RE::ControlMap::GetSingleton(); controlMap) {
				controlMap->SetIgnoreKeyboardMouse(false);
			}

			// Installs or uninstalls the OnThumbstickEvent wrapper to match the setting.
			AutoMoveStickCancel::Sync();

			// Counts down once per call; armed in ObserveMouse.
			TickLookWatch();

			// See ClampLookInputVecAnomaly() for what this corrects.
			ClampLookInputVecAnomaly();

			// Whether a real look-relevant event from each device happened in this batch, meaning the
			// whole InputEvent chain for this tick. See ApplyLookInputSourceFilter.
			bool mouseLookThisBatch = false;
			bool gamepadLookThisBatch = false;

			for (auto event = a_event; event; event = event->next) {
				ObserveOne(*event, mouseLookThisBatch, gamepadLookThisBatch);
			}

			ApplyLookInputSourceFilter(mouseLookThisBatch, gamepadLookThisBatch);

			// See DiagnosticLog.h. Throttled internally.
			DiagnosticLog::MaybeLogPeriodicSnapshot(
				_glyphDevice == GlyphDevice::kGamepad, static_cast<std::int32_t>(Settings::GetInputPreference()));

			return false;  // never consume - always let the real handler chain run
		}

	private:
		using Clock = std::chrono::steady_clock;

		DeviceTracker() = default;
		DeviceTracker(const DeviceTracker&) = delete;
		DeviceTracker& operator=(const DeviceTracker&) = delete;

		static constexpr float kThumbstickDeadzone = 0.2f;
		static constexpr std::int32_t kMouseMoveThreshold = 2;  // raw device units, filters idle jitter
		static constexpr auto kSwitchCooldown = std::chrono::milliseconds(200);

		void ObserveOne(const RE::InputEvent& a_event, bool& a_mouseLookThisBatch, bool& a_gamepadLookThisBatch)
		{
			switch (*a_event.device) {
			case RE::INPUT_DEVICE::kGamepad:
				ObserveGamepad(a_event, a_gamepadLookThisBatch);
				break;
			case RE::INPUT_DEVICE::kKeyboard:
				ObserveKeyboard(a_event);
				break;
			case RE::INPUT_DEVICE::kMouse:
				ObserveMouse(a_event, a_mouseLookThisBatch);
				break;
			default:
				break;
			}
		}

		void ObserveGamepad(const RE::InputEvent& a_event, bool& a_gamepadLookThisBatch)
		{
			bool real = false;
			if (const auto button = a_event.As<RE::ButtonEvent>(); button) {
				real = button->QJustPressed();
				if (real) {
					DiagnosticLog::LogEvent("gamepad"sv, "button"sv, static_cast<float>(button->idCode), 0.0f);
				}
			} else if (const auto stick = a_event.As<RE::ThumbstickEvent>(); stick) {
				real = std::abs(stick->xValue) > kThumbstickDeadzone || std::abs(stick->yValue) > kThumbstickDeadzone;
				if (real) {
					DiagnosticLog::LogEvent("gamepad"sv, "thumbstick"sv, stick->xValue, stick->yValue);

					// idCode is logged alongside strUserEvent, ThumbstickEvent::THUMBSTICK_ID (kLeft=0xB,
					// kRight=0xC) being an alternate way to tell the sticks apart.
					REX::DEBUG("FIS-DIAG gamepad thumbstick strUserEvent='{}' idCode={}"sv,
						stick->strUserEvent.c_str(), stick->idCode);

					// "Look" is the right stick specifically. Movement must never be affected by
					// LookInputSource.
					if (stick->strUserEvent == "Look"sv) {
						// Zeroes the raw event fields only; the real per-device block is
						// LookHandlerPatches::ShouldBlockGamepadLook, at vanilla's own write site. Do not also
						// zero lookInputVec, a single shared field, or the other device's legitimate value for
						// the tick goes with it. a_gamepadLookThisBatch and _gamepadActiveLooking are skipped
						// when blocked, a zeroed stick not driving look, while _lastGamepadActivity and
						// TrySwitchGlyphDevice sit outside this branch so the stick still counts as activity.
						if (Settings::GetLookInputSource() == Settings::LookInputSource::kMouseOnly) {
							auto& mutableStick = const_cast<RE::ThumbstickEvent&>(*stick);
							mutableStick.xValue = 0.0f;
							mutableStick.yValue = 0.0f;
							REX::DEBUG("FIS-DIAG look-filter: blocked gamepad look-stick raw event zeroed at source"sv);
						} else {
							a_gamepadLookThisBatch = true;
							_gamepadActiveLooking.store(true, std::memory_order_relaxed);
						}
					}
				}
			}

			if (real) {
				_lastGamepadActivity = Clock::now();
				TrySwitchGlyphDevice(GlyphDevice::kGamepad, "gamepad input"sv);
			}
		}

		void ObserveKeyboard(const RE::InputEvent& a_event)
		{
			if (const auto button = a_event.As<RE::ButtonEvent>(); button && button->QJustPressed()) {
				DiagnosticLog::LogEvent("keyboard"sv, "button"sv, static_cast<float>(button->idCode), 0.0f);

				_lastKBMActivity = Clock::now();
				TrySwitchGlyphDevice(GlyphDevice::kKBM, "keyboard key"sv);
			}
		}

		void ObserveMouse(const RE::InputEvent& a_event, bool& a_mouseLookThisBatch)
		{
			if (const auto button = a_event.As<RE::ButtonEvent>(); button && button->QJustPressed()) {
				DiagnosticLog::LogEvent("mouse"sv, "button"sv, static_cast<float>(button->idCode), 0.0f);
				MarkRealMouseActivity();
				TrySwitchGlyphDevice(GlyphDevice::kKBM, "mouse button"sv);
				return;
			}

			if (const auto move = a_event.As<RE::MouseMoveEvent>(); move) {
				// Captured before any zeroing below, and used by the glyph-device tracking further down
				// rather than move->mouseInputX/Y, so moving the mouse still counts as mouse activity even
				// when kGamepadOnly suppresses its look contribution.
				const std::int32_t originalX = move->mouseInputX;
				const std::int32_t originalY = move->mouseInputY;

				// Read once and reused below, by both the block-at-source branch and the
				// pure-mouse bypass.
				const auto lookSource = Settings::GetLookInputSource();

				// Per-event, so it cannot stomp the other device; ShouldBlockMouseLook is the real per-device
				// block. lookInputVec is left alone, being a single shared field: suppressing the mouse that
				// way would destroy whatever the stick wrote that tick, which in kGamepadOnly makes stick look
				// stutter when the mouse is nudged, DeviceTracker and PlayerControls being independent
				// receivers with uncontrolled ordering.
				if (lookSource == Settings::LookInputSource::kGamepadOnly &&
				    (originalX != 0 || originalY != 0)) {
					auto& mutableMove = const_cast<RE::MouseMoveEvent&>(*move);
					mutableMove.mouseInputX = 0;
					mutableMove.mouseInputY = 0;
					REX::DEBUG("FIS-DIAG look-filter: blocked mouse raw delta ({}, {}) zeroed at source"sv,
						originalX, originalY);
				}

				// Everything below reads move->mouseInputX/Y, now zero if blocked above, rather
				// than originalX/Y, so it no-ops when blocked without a separate branch.
				if (move->mouseInputX != 0 || move->mouseInputY != 0) {
					// Unconditional on any nonzero mouse-look event and not gated behind the magnitude check
					// below, so the redirected call sites see "mouse is looking" immediately rather than
					// staying latched on gamepad and its different sensitivity curve. exchange() because the
					// transition guard below needs the previous value.
					const bool wasGamepadLooking = _gamepadActiveLooking.exchange(false, std::memory_order_relaxed);

					// Arms a short per-tick watch on the real lookInputVec at a gamepad-to-mouse handoff; see
					// TickLookWatch(). ProcessLookInput rescales lookInputVec with no clamp, unlike
					// lookInputVecNormalized.
					if (wasGamepadLooking) {
						_lookWatchTicksRemaining = 90;  // ~1.5s at 60fps
					}

					// Skips this mod's own mouse-look write for the one event after a gamepad-to-mouse handoff.
					// In kMouseOnly the gamepad's contribution is blocked outright, so _gamepadActiveLooking
					// never latches true and vanilla takes its mouse branch with the right scaling: no
					// mis-scaling to correct and no handoffs.
					if (lookSource == Settings::LookInputSource::kMouseOnly) {
						// No write at all: vanilla's own LookHandler value stands untouched.
					} else if (wasGamepadLooking) {
						REX::DEBUG(
							"FIS-DIAG mouse-look transition guard: skipped first post-gamepad delta ({}, {})"sv,
							move->mouseInputX, move->mouseInputY);
					} else {
						ApplyMouseLook(move->mouseInputX, move->mouseInputY);
					}

					a_mouseLookThisBatch = true;
				}

				const auto magnitude = std::abs(originalX) + std::abs(originalY);
				if (magnitude > kMouseMoveThreshold) {
					DiagnosticLog::LogEvent("mouse"sv, "move"sv, static_cast<float>(originalX), static_cast<float>(originalY));
					MarkRealMouseActivity();
					TrySwitchGlyphDevice(GlyphDevice::kKBM, "mouse movement"sv);
				}
			}
		}

		// Defence in depth behind the source-level zeroing in ObserveGamepad/ObserveMouse. Vanilla
		// recomputes lookInputVec from the raw event right after this runs, so a write here does not
		// survive on its own. Runs once per batch, after every event in the chain, so a same-batch
		// contribution from the allowed device is not cancelled by this reacting to the blocked one's
		// event. Limitation: if both devices produce a real look event in the same batch, the blocked
		// one is not guaranteed to be cancelled.
		void ApplyLookInputSourceFilter(bool a_mouseLookThisBatch, bool a_gamepadLookThisBatch)
		{
			const auto source = Settings::GetLookInputSource();
			if (source == Settings::LookInputSource::kBoth) {
				return;
			}

			const auto playerControls = RE::PlayerControls::GetSingleton();
			if (!playerControls) {
				return;
			}

			const bool cancelMouse = source == Settings::LookInputSource::kGamepadOnly &&
			                         a_mouseLookThisBatch && !a_gamepadLookThisBatch;
			const bool cancelGamepad = source == Settings::LookInputSource::kMouseOnly &&
			                           a_gamepadLookThisBatch && !a_mouseLookThisBatch;

			// Logs every decision, not only when it fires, so a filter that never reaches a
			// cancel decision is distinguishable from one whose write does nothing.
			if (a_mouseLookThisBatch || a_gamepadLookThisBatch) {
				REX::DEBUG(
					"FIS-DIAG look-filter source={} mouseThisBatch={} gamepadThisBatch={} cancelMouse={} cancelGamepad={} "
					"lookBefore=({:.3f},{:.3f})"sv,
					static_cast<std::int32_t>(source), a_mouseLookThisBatch, a_gamepadLookThisBatch, cancelMouse, cancelGamepad,
					playerControls->data.lookInputVec.x, playerControls->data.lookInputVec.y);
			}

			if (cancelMouse || cancelGamepad) {
				playerControls->data.lookInputVec = {};
				REX::DEBUG("FIS-DIAG look-filter zeroed lookInputVec"sv);
			}
		}

		// Shared by mouse button and mouse move, both counting as real mouse activity for
		// _lastKBMActivity, which combines keyboard and mouse.
		void MarkRealMouseActivity()
		{
			_lastKBMActivity = Clock::now();
		}

		// Logs the real PlayerControlsData fields per call for a short window after a gamepad-to-mouse
		// handoff, armed in ObserveMouse. It reads lookInputVec (LookHandler's write target) and
		// lookInputVecNormalized (ProcessLookInput's separate output, clamped to [-1,1]), so a spike
		// in the first far beyond the second is visible.
		void TickLookWatch()
		{
			if (_lookWatchTicksRemaining <= 0) {
				return;
			}
			--_lookWatchTicksRemaining;

			const auto playerControls = RE::PlayerControls::GetSingleton();
			if (!playerControls) {
				return;
			}

			REX::DEBUG(
				"FIS-DIAG look-watch lookInputVec=({:.3f},{:.3f}) lookInputVecNormalized=({:.3f},{:.3f}) "
				"gamepadActiveLooking={} ticksLeft={}"sv,
				playerControls->data.lookInputVec.x, playerControls->data.lookInputVec.y,
				playerControls->data.lookInputVecNormalized.x, playerControls->data.lookInputVecNormalized.y,
				_gamepadActiveLooking.load(std::memory_order_relaxed), _lookWatchTicksRemaining);
		}

		// Corrects the one-frame lookInputVec spike at a gamepad-to-mouse handoff, where it can read in
		// the thousands while lookInputVecNormalized reads its clamped -1..1, ProcessLookInput's mouse
		// branch rescaling with no clamp. Runs after PlayerControls has finished its per-tick
		// processing and corrects in place within the same tick, so there is no race with LookHandler's
		// write. kMaxSaneLookInputVec has headroom over the normal -5..+5 range while still catching a
		// three-orders-of-magnitude spike.
		static constexpr float kMaxSaneLookInputVec = 50.0f;

		void ClampLookInputVecAnomaly()
		{
			// Only meaningful in kBoth: with LookInputSource restricting look to one device there are no
			// handoffs, so this could only fire on a legitimate value.
			if (Settings::GetLookInputSource() != Settings::LookInputSource::kBoth) {
				return;
			}

			const auto playerControls = RE::PlayerControls::GetSingleton();
			if (!playerControls) {
				return;
			}

			auto& look = playerControls->data.lookInputVec;
			const bool anomalous = std::abs(look.x) > kMaxSaneLookInputVec || std::abs(look.y) > kMaxSaneLookInputVec;
			if (!anomalous) {
				return;
			}

			const float origX = look.x;
			const float origY = look.y;
			look.x = std::clamp(look.x, -kMaxSaneLookInputVec, kMaxSaneLookInputVec);
			look.y = std::clamp(look.y, -kMaxSaneLookInputVec, kMaxSaneLookInputVec);

			REX::DEBUG(
				"FIS-DIAG look-clamp: lookInputVec anomaly ({:.3f},{:.3f}) clamped to ({:.3f},{:.3f})"sv,
				origX, origY, look.x, look.y);
		}

		void TrySwitchGlyphDevice(GlyphDevice a_device, std::string_view a_cause)
		{
			if (_glyphDevice == a_device) {
				return;
			}

			const auto now = Clock::now();
			if (now - _lastSwitch < kSwitchCooldown) {
				REX::DEBUG("Input Swapper: glyph switch to {} blocked by cooldown (cause: {})"sv,
					a_device == GlyphDevice::kGamepad ? "gamepad"sv : "KBM"sv, a_cause);
				return;
			}

			_glyphDevice = a_device;
			_lastSwitch = now;
			REX::DEBUG("Input Swapper: glyph device -> {} (cause: {})"sv,
				a_device == GlyphDevice::kGamepad ? "gamepad"sv : "KBM"sv, a_cause);
		}

		GlyphDevice _glyphDevice{ GlyphDevice::kKBM };
		Clock::time_point _lastSwitch{};
		Clock::time_point _lastGamepadActivity{};
		Clock::time_point _lastKBMActivity{};

		// Atomic because the redirected call sites it feeds (InputDevicePatches::UsingGamepadLook)
		// run from inside engine camera and UI code, which is not provably the same thread as this
		// hook's input-event callback.
		std::atomic<bool> _gamepadActiveLooking{ false };

		// Countdown for TickLookWatch(). Plain int rather than atomic, because ShouldHandleEvent
		// both arms and ticks it and already runs single-threaded, like the rest of this class's
		// non-atomic state.
		int _lookWatchTicksRemaining{ 0 };
	};
}
