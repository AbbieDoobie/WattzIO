#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "InputLabels.h"
#include "Keybinds.h"
#include "MenuContext.h"
#include "Settings.h"
#include "WeaponSwapLogic.h"

// Raw BSInputEventReceiver vtable hook on PlayerCamera. This mod has no vanilla control to
// piggyback on, so its own gamepad and keyboard bindings are matched directly against every
// incoming button event.
namespace WS
{
	// === F4RD RELOCATIONS ========================================================
	//   Kind   Site                                          ID / RVA     +Off
	//   vtbl   RE::VTABLE::PlayerCamera[1]                   F4RD            -
	//   vfunc  BSInputEventReceiver::PerformInputProcessing  slot 0          -
	// =============================================================================
	class InputHook
	{
	public:
		static void Install()
		{
			// F4RD:vtbl - [1] = BSInputEventReceiver subobject
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::PlayerCamera[1] };
			// F4RD:vfunc - slot 0 = PerformInputProcessing
			_original = vtbl.write_vfunc(0, &InputHook::PerformInputProcessing);
			RefreshGamepadKeycode();
			REX::INFO("Weapon Swap Button: BSInputEventReceiver hook installed on PlayerCamera"sv);
		}

		// Re-resolves the gamepad dropdown -> keycode conversion, at Install() and again on every
		// settings reload, since the dropdown can change mid-session. The keyboard hotkey needs no
		// equivalent cache - Keybinds::Load() refreshes its own optional, read fresh per event.
		static void RefreshGamepadKeycode()
		{
			s_gamepadKeycode = static_cast<std::uint32_t>(InputLabels::GamepadDropdownIndexToKeycode(Settings::iGamepadInput));
		}

	private:
		static void PerformInputProcessing(RE::BSInputEventReceiver* a_this, const RE::InputEvent* a_queueHead)
		{
			// Dialogue and the vanilla quickloot prompt do not set RE::UI::menuMode, which counts
			// only true pausing menus, and a full-screen menu never reaches this hook at all, so
			// non-pausing menus need their own check. Captured once per frame - see MenuContext.h.
			const auto state = MenuContext::Capture();

			// Off unless Settings::debugLog is set by hand. Change-detected, so even when on it is a
			// handful of lines per session rather than hundreds per second.
			if (Settings::debugLog) {
				if (auto line = MenuContext::BuildDiagnosticLine(state); line != s_lastDiagLine) {
					REX::DEBUG("Weapon Swap Button: [DIAG] {}", line);
					s_lastDiagLine = std::move(line);
				}
			}

			const bool blockHotkeys = MenuContext::ShouldBlock(state, Settings::blockMode);

			for (auto event = a_queueHead; event; event = event->next) {
				if (blockHotkeys) {
					continue;
				}
				// const_cast is safe here: real, engine-owned mutable input-queue memory, only
				// exposed as const by this vfunc's own signature.
				if (const auto button = const_cast<RE::InputEvent*>(event)->As<RE::ButtonEvent>(); button) {
					if (const auto keycode = ToUnifiedKeycode(*button); keycode && IsOurKeycode(*keycode)) {
						HandleEvent(*button);
					}
				}
			}
			_original(a_this, a_queueHead);
		}

		static void HandleEvent(RE::ButtonEvent& a_event)
		{
			if (!RE::QReleased(a_event)) {
				return;  // equipping occurs on release only, for every swap type
			}

			// Hold (Slot 3) is checked first and short-circuits. A hold is a different gesture from
			// a tap, so it also clears any tap chain in progress rather than counting toward one:
			// tap, tap, hold gives slot 3 and starts over, never slot 4.
			if (Settings::bHoldSlot3 && a_event.heldDownSecs >= Settings::HoldThresholdSeconds()) {
				s_tapCount = 0;
				WeaponSwapLogic::TriggerSlot(a_event, 2);
				return;
			}

			// Triple Tap (Slot 4). The window is measured from the first tap of the chain, so all three
			// must land inside it, and a late tap becomes tap 1 of a fresh chain. Taps 1 and 2 still
			// perform their normal swap, so slot 4 costs two ordinary swaps on the way.
			if (Settings::bTripleTapSlot4) {
				const auto now = std::chrono::steady_clock::now();
				const auto sinceFirst = std::chrono::duration<float>(now - s_firstTapTime).count();
				if (s_tapCount == 0 || sinceFirst > Settings::TripleTapWindowSeconds()) {
					s_tapCount = 1;
					s_firstTapTime = now;
				} else if (++s_tapCount >= 3) {
					s_tapCount = 0;
					WeaponSwapLogic::TriggerSlot(a_event, 3);
					return;
				}
			} else {
				s_tapCount = 0;
			}

			WeaponSwapLogic::Trigger(a_event);
		}

		// Tap-chain state for Triple Tap (Slot 4). steady_clock rather than an engine timer, because
		// this is a real-time input gesture and the hotkey is already gated out entirely while a menu
		// owns input, so no paused-game window can leave a stale chain accumulating.
		static inline std::int32_t s_tapCount = 0;
		static inline std::chrono::steady_clock::time_point s_firstTapTime{};

		[[nodiscard]] static bool IsOurKeycode(std::uint32_t a_keycode)
		{
			if (s_gamepadKeycode != 0 && s_gamepadKeycode == a_keycode) {
				return true;
			}
			const auto kb = Keybinds::WeaponSwapKeyboardKeycode();
			return kb && static_cast<std::uint32_t>(*kb) == a_keycode;
		}

		static inline std::uint32_t s_gamepadKeycode = 0;

		// Converts a ButtonEvent's device-relative idCode into F4SE::InputMap's unified 0-281
		// numbering.
		[[nodiscard]] static std::optional<std::uint32_t> ToUnifiedKeycode(const RE::ButtonEvent& a_event)
		{
			switch (a_event.device.get()) {
			case RE::INPUT_DEVICE::kKeyboard:
				return static_cast<std::uint32_t>(a_event.QIDCode());
			case RE::INPUT_DEVICE::kMouse:
				// MCM's "hotkey" widget can capture a mouse button or wheel too, even though this mod's
				// own settings text calls it a keyboard hotkey, so a mouse-bound hotkey still works.
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

		// Previous rendered diagnostic line, for the change-detected trace above.
		static inline std::string s_lastDiagLine;

		using OriginalFunc = void(RE::BSInputEventReceiver*, const RE::InputEvent*);
		static inline REL::Relocation<OriginalFunc*> _original;
	};
}
