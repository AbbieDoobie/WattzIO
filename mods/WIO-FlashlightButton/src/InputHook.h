#pragma once

#include <optional>
#include <string>

// NOGDI keeps <wingdi.h>'s bare `ERROR` macro from colliding with REX::ERROR.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOGDI
#include <Windows.h>

#include "Flashlight.h"
#include "InputLabels.h"
#include "Keybinds.h"
#include "MenuContext.h"
#include "PovWorkshop.h"
#include "Settings.h"

namespace FMB
{
	// Raw BSInputEventReceiver vtable slot 0 hook on two receivers, because the name stamp must land
	// before both PlayerControls (togglePOVHandler, the workshop long-hold) and PlayerCamera
	// (third-person zoom) read the user-event name, and the broadcast order between them is not
	// observable. The chain is shared, so whichever runs first stamps it and owns the session.
	// === F4RD RELOCATIONS ========================================================
	//   Kind   Site                                          ID / RVA     +Off
	//   vtbl   RE::VTABLE::PlayerCamera[1]                   F4RD            -
	//   live   PlayerControls singleton's own vtable ptr     none            -
	//   vtbl   RE::VTABLE::PlayerControls[0]  (assert only)  F4RD            -
	//   vfunc  BSInputEventReceiver::PerformInputProcessing  slot 0          -
	// =============================================================================
	class InputHook
	{
	public:
		// BSInputEventReceiver declares no virtual destructor, so PerformInputProcessing really is
		// vtable slot 0 for both hooks below.
		static void Install()
		{
			// PlayerCamera: VTABLE[1] is its BSInputEventReceiver subobject (offset 0x038).
			// F4RD:vtbl - [1] = BSInputEventReceiver subobject (offset 0x038)
			REL::Relocation<std::uintptr_t> cameraVtbl{ RE::VTABLE::PlayerCamera[1] };
			// F4RD:vfunc - slot 0 = PerformInputProcessing
			_originalCamera = cameraVtbl.write_vfunc(0, &InputHook::PerformInputProcessingCamera);

			// Read from the live singleton rather than VTABLE[0], which needs no id at all:
			// BSInputEventReceiver is PlayerControls' primary base at offset 0x000, so the singleton's
			// first 8 bytes are that subobject's vtable pointer on any build.
			if (const auto controls = RE::PlayerControls::GetSingleton(); controls) {
				const auto vtblAddr = *reinterpret_cast<const std::uintptr_t*>(controls);

				// One-off assertion that IDs_VTABLE's PlayerControls[0] still agrees with the
				// live object's real vtable. A mismatch means a game update moved it.
				// F4RD:vtbl - [0] = PlayerControls' primary base; read for the assert only
				const auto idVtblAddr = REL::Relocation<std::uintptr_t>{ RE::VTABLE::PlayerControls[0] }.address();
				REX::INFO("Flashlight Button: PlayerControls vtable - live=0x{:X} IDs_VTABLE[0]=0x{:X} ({})"sv,
					vtblAddr, idVtblAddr, vtblAddr == idVtblAddr ? "match"sv : "MISMATCH, IDs_VTABLE[0] is wrong"sv);

				// F4RD:live - vtable read from the live singleton, no id involved
				REL::Relocation<std::uintptr_t> controlsVtbl{ vtblAddr };
				// F4RD:vfunc - slot 0 = PerformInputProcessing
				_originalControls = controlsVtbl.write_vfunc(0, &InputHook::PerformInputProcessingControls);
				REX::INFO("Flashlight Button: input hooks installed on PlayerCamera and PlayerControls"sv);
			} else {
				// Not fatal, but the name stamp is no longer guaranteed to land first.
				REX::WARN("Flashlight Button: PlayerControls singleton unavailable - only the PlayerCamera hook is installed"sv);
			}

			RefreshGamepadKeycode();
		}

		// Re-resolves the gamepad dropdown to keycode conversion, at Install() and on every settings
		// reload. The keyboard hotkey needs no equivalent cache, Keybinds::Load() refreshing its own.
		static void RefreshGamepadKeycode()
		{
			s_gamepadKeycode = static_cast<std::uint32_t>(InputLabels::GamepadDropdownIndexToKeycode(Settings::iGamepadKey));
			s_gamepadModifierKeycode = static_cast<std::uint32_t>(InputLabels::GamepadDropdownIndexToKeycode(Settings::iGamepadModifier));
		}

		// The gamepad-modifier-held flag only updates on a raw ButtonEvent for that button, and there
		// is no live "is this button down" poll, so a release swallowed while a menu had input focus
		// would leave it stuck true. A handoff in flight is unwound rather than dropped: vanilla is
		// owed a release, and clearing s_handedOff without one strands PlayerControlsData::togglePOV
		// true and disarms the watchdog that would otherwise catch it.
		static void ResetTransientState()
		{
			if (s_handedOff) {
				RequestEngineForceRelease();
				REX::DEBUG("Flashlight Button: handoff unwound by a transient-state reset"sv);
			}
			s_gamepadModifierHeld = false;
			s_gestureActive = false;
			s_handedOff = false;
			s_missedFrames = 0;
		}

	private:
		static void PerformInputProcessingCamera(RE::BSInputEventReceiver* a_this, const RE::InputEvent* a_queueHead)
		{
			Dispatch(Owner::kCamera, a_queueHead);
			_originalCamera(a_this, a_queueHead);
		}

		static void PerformInputProcessingControls(RE::BSInputEventReceiver* a_this, const RE::InputEvent* a_queueHead)
		{
			Dispatch(Owner::kControls, a_queueHead);
			_originalControls(a_this, a_queueHead);
		}

		enum class Owner
		{
			kNone,
			kCamera,
			kControls
		};

		static void Dispatch(Owner a_caller, const RE::InputEvent* a_queueHead)
		{
			// The first receiver the engine calls claims the work. Registration order is fixed for the
			// process, so this latches once.
			if (s_owner == Owner::kNone) {
				s_owner = a_caller;
				REX::INFO("Flashlight Button: input processing owned by {}"sv,
					a_caller == Owner::kCamera ? "PlayerCamera"sv : "PlayerControls"sv);
			}
			if (s_owner != a_caller) {
				return;
			}

			// Captured once per frame, not per event - see MenuContext.h.
			const auto state = MenuContext::Capture();

			// Off unless Settings::debugLog is set by hand. Change-detected, so even when on it is
			// a handful of lines per session rather than hundreds per second.
			if (Settings::debugLog) {
				if (auto line = MenuContext::BuildDiagnosticLine(state); line != s_lastDiagLine) {
					REX::DEBUG("Flashlight Button: [DIAG] {}", line);
					s_lastDiagLine = std::move(line);
				}
			}

			const bool blocked = MenuContext::ShouldBlock(state, Settings::blockMode);

			// This gate belongs on starting a gesture, never on finishing one: once handed off, vanilla is
			// owed a release and nothing else produces it, and abandoning mid-hold strands
			// PlayerControlsData::togglePOV true, zeroing the vertical look axis and keeping the "Toggle
			// POV Input Layer" alive. Blocking contexts are non-pausing, so events keep arriving.
			if (blocked && !s_handedOff) {
				// Safe to abandon here: vanilla is holding no state on this mod's behalf yet.
				ResetTransientState();
				return;
			}

			bool sawOurButton = false;

			for (auto event = a_queueHead; event; event = event->next) {
				// const_cast is safe: real, engine-owned mutable input-queue memory, const only
				// because of this vfunc's signature.
				const auto button = const_cast<RE::InputEvent*>(event)->As<RE::ButtonEvent>();
				if (!button) {
					continue;
				}
				const auto keycode = ToUnifiedKeycode(*button);
				if (!keycode) {
					continue;
				}

				// Gamepad modifier is tracked, never acted on.
				if (s_gamepadModifierKeycode != 0 && *keycode == s_gamepadModifierKeycode) {
					s_gamepadModifierHeld = RE::QPressed(*button);
					continue;
				}

				if (IsOurButton(*button, *keycode)) {
					sawOurButton = true;
					HandleEvent(*button);
				}
			}

			// Watchdog for an orphaned handoff. A held button dispatches a fresh ButtonEvent every frame,
			// so several consecutive event-free frames with a handoff in flight mean the button is
			// physically up and its release was never seen, leaving togglePOV stuck true.
			if (s_handedOff) {
				if (sawOurButton) {
					s_missedFrames = 0;
				} else if (++s_missedFrames >= kMissedFrameLimit) {
					RecoverOrphanedHandoff();
				}
			}
		}

		// Unwinds a handoff vanilla is still holding, through the engine's own recovery path:
		// PerformInputProcessing checks data.checkHeldStates and, for every held-state handler with
		// triggerReleaseEvent set, builds a "ForceRelease" ButtonEvent, calls OnButtonEvent, then
		// SetHeldStateActive(false). TogglePOVHandler is a HeldStateHandler with its base subobject
		// at offset 0, so the cast is address-identical.
		static void RequestEngineForceRelease()
		{
			if (const auto pc = RE::PlayerControls::GetSingleton(); pc && pc->togglePOVHandler) {
				auto* const held = reinterpret_cast<RE::HeldStateHandler*>(pc->togglePOVHandler);
				held->triggerReleaseEvent = true;
				pc->data.checkHeldStates = true;
			}
		}

		// The watchdog's own route into the unwind above, for a handoff whose release was never
		// seen at all.
		static void RecoverOrphanedHandoff()
		{
			RequestEngineForceRelease();
			REX::WARN("Flashlight Button: handoff orphaned (release never seen) - requesting engine ForceRelease"sv);
			s_gestureActive = false;
			s_handedOff = false;
			s_missedFrames = 0;
		}

		// Below the hold threshold nothing is touched and a release toggles the light. Crossing it
		// hands vanilla a fabricated press edge, after which every event passes through with its real
		// heldDownSecs and vanilla owns the rest of the gesture.
		static void HandleEvent(RE::ButtonEvent& a_event)
		{
			if (a_event.QJustPressed()) {
				s_gestureActive = true;
				s_handedOff = false;
				return;
			}

			if (RE::QReleased(a_event)) {
				if (s_handedOff) {
					PovWorkshop::PassThrough(a_event);
				} else if (s_gestureActive) {
					// Never crossed X (or POV/Workshop is switched off entirely): this was a tap.
					Flashlight::Toggle();
				}
				s_gestureActive = false;
				s_handedOff = false;
				return;
			}

			// Held.
			if (!s_gestureActive || !Settings::bEnablePovWorkshop) {
				return;
			}

			if (!s_handedOff) {
				if (a_event.heldDownSecs >= Settings::HoldThresholdSeconds()) {
					s_handedOff = true;
					PovWorkshop::HandOffPress(a_event);
				}
				return;
			}

			PovWorkshop::PassThrough(a_event);
		}

		[[nodiscard]] static bool IsOurButton(const RE::ButtonEvent& a_event, std::uint32_t a_keycode)
		{
			if (a_event.device.get() == RE::INPUT_DEVICE::kGamepad) {
				if (s_gamepadKeycode == 0 || a_keycode != s_gamepadKeycode) {
					return false;
				}
				// An unset modifier ("None") means no modifier is required.
				return s_gamepadModifierKeycode == 0 || s_gamepadModifierHeld;
			}

			const auto kb = Keybinds::KeyboardKeycode();
			if (!kb || static_cast<std::uint32_t>(*kb) != a_keycode) {
				return false;
			}
			return KeyboardModifiersSatisfied(Keybinds::KeyboardModifiers());
		}

		// Keyboard modifier support ("allowModifierKeys": true on the hotkey widget). Held state comes
		// from GetAsyncKeyState rather than tracked ButtonEvents, which would first need to establish
		// whether a keyboard idCode is a scan code or a virtual-key code, and is only consulted when
		// one of this mod's own key events arrives, so the game has focus. MCM does not document the
		// bitmask below; a wrong assignment fails safe, a modified binding never matching.
		static constexpr std::int32_t kModShift = 1 << 0;
		static constexpr std::int32_t kModCtrl = 1 << 1;
		static constexpr std::int32_t kModAlt = 1 << 2;

		[[nodiscard]] static bool KeyboardModifiersSatisfied(std::int32_t a_modifiers)
		{
			if (a_modifiers == 0) {
				return true;  // no modifiers required, the common case
			}

			const auto down = [](int a_vk) { return (::GetAsyncKeyState(a_vk) & 0x8000) != 0; };

			if (((a_modifiers & kModShift) != 0) != down(VK_SHIFT)) {
				return false;
			}
			if (((a_modifiers & kModCtrl) != 0) != down(VK_CONTROL)) {
				return false;
			}
			if (((a_modifiers & kModAlt) != 0) != down(VK_MENU)) {
				return false;
			}
			return true;
		}

		// Converts a ButtonEvent's device-relative idCode into F4SE::InputMap's unified 0-281
		// numbering.
		[[nodiscard]] static std::optional<std::uint32_t> ToUnifiedKeycode(const RE::ButtonEvent& a_event)
		{
			switch (a_event.device.get()) {
			case RE::INPUT_DEVICE::kKeyboard:
				return static_cast<std::uint32_t>(a_event.QIDCode());
			case RE::INPUT_DEVICE::kMouse:
				// The wheel arrives as a large BS_BUTTON_CODE sentinel, not a small sequential index.
				if (a_event.QIDCode() == static_cast<std::int32_t>(RE::kBSButtonCodeWheelUp)) {
					return static_cast<std::uint32_t>(F4SE::InputMap::kMacro_MouseWheelOffset);
				} else if (a_event.QIDCode() == static_cast<std::int32_t>(RE::kBSButtonCodeWheelDown)) {
					return static_cast<std::uint32_t>(F4SE::InputMap::kMacro_MouseWheelOffset) + 1;
				} else {
					return F4SE::InputMap::kMacro_MouseButtonOffset + static_cast<std::uint32_t>(a_event.QIDCode());
				}
			case RE::INPUT_DEVICE::kGamepad:
				return F4SE::InputMap::GamepadMaskToKeycode(static_cast<std::uint32_t>(a_event.QIDCode()));
			default:
				return std::nullopt;
			}
		}

		static inline Owner         s_owner = Owner::kNone;
		static inline std::uint32_t s_gamepadKeycode = 0;
		static inline std::uint32_t s_gamepadModifierKeycode = 0;
		static inline bool          s_gamepadModifierHeld = false;
		static inline bool          s_gestureActive = false;
		static inline bool          s_handedOff = false;

		// Not 1: a single event-free frame is timing noise, so the watchdog cannot cut a hold short.
		static constexpr int        kMissedFrameLimit = 3;
		static inline int           s_missedFrames = 0;

		// Previous rendered diagnostic line, for the change-detected trace above.
		static inline std::string   s_lastDiagLine;

		using OriginalFunc = void(RE::BSInputEventReceiver*, const RE::InputEvent*);
		static inline REL::Relocation<OriginalFunc*> _originalCamera;
		static inline REL::Relocation<OriginalFunc*> _originalControls;
	};
}
