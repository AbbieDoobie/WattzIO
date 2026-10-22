#pragma once

#include <optional>
#include <string>

// NOGDI keeps <wingdi.h>'s bare ERROR macro from colliding with REX::ERROR. See Settings.h.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOGDI
#include <Windows.h>

#include "Consume.h"
#include "Diagnostics.h"
#include "InputLabels.h"
#include "Keybinds.h"
#include "MenuContext.h"
#include "Settings.h"
#include "SmartHeal.h"

namespace SH
{
	// Raw BSInputEventReceiver vtable slot 0 hook, on PlayerCamera only. A mod that rewrites events
	// would have to hook PlayerControls too, so its edit lands before whichever receiver runs
	// first; this one only reads and never touches the queue.
	// === F4RD RELOCATIONS ========================================================
	//   Kind   Site                                          ID / RVA     +Off
	//   vtbl   RE::VTABLE::PlayerCamera[1]                   F4RD            -
	//   vfunc  BSInputEventReceiver::PerformInputProcessing  slot 0          -
	// =============================================================================
	class InputHook
	{
	public:
		// BSInputEventReceiver declares no virtual destructor, so PerformInputProcessing is vtable
		// slot 0 (RE/B/BSInputEventReceiver.h).
		static void Install()
		{
			if (_installed) {
				return;
			}
			// F4RD:vtbl - [1] = PlayerCamera's BSInputEventReceiver subobject (offset 0x038)
			REL::Relocation<std::uintptr_t> cameraVtbl{ RE::VTABLE::PlayerCamera[1] };
			// F4RD:vfunc - slot 0 = PerformInputProcessing
			_original = cameraVtbl.write_vfunc(0, &InputHook::PerformInputProcessing);
			_installed = true;
			RefreshGamepadKeycode();
			REX::INFO("Smart Heal Button: input hook installed on PlayerCamera"sv);
		}

		// Re-resolves the gamepad dropdown to keycode conversion, at Install() and on every settings
		// reload. The keyboard hotkey needs no equivalent cache, Keybinds::Load() refreshing its own.
		static void RefreshGamepadKeycode()
		{
			s_gamepadKeycode = static_cast<std::uint32_t>(InputLabels::GamepadDropdownIndexToKeycode(Settings::iGamepadKey));
			s_gamepadModifierKeycode = static_cast<std::uint32_t>(InputLabels::GamepadDropdownIndexToKeycode(Settings::iGamepadModifier));
		}

		// The gamepad-modifier-held flag only updates on a raw ButtonEvent for that button, and there
		// is no live "is this button down" poll. A release swallowed while a menu had input focus
		// would otherwise leave it stuck true.
		static void ResetTransientState()
		{
			s_gamepadModifierHeld = false;
		}

	private:
		static void PerformInputProcessing(RE::BSInputEventReceiver* a_this, const RE::InputEvent* a_queueHead)
		{
			Dispatch(a_queueHead);
			_original(a_this, a_queueHead);  // never skip calling through - always forward
		}

		static void Dispatch(const RE::InputEvent* a_queueHead)
		{
			// The only per-frame callback this mod owns, so the consume queue drains here. Ahead of the
			// blocking gate, because once items are queued they should finish and a crosshair prompt
			// appearing mid-swallow is not a reason to stop. A menu opening still aborts it, from
			// SettingsReload.
			ItemCatalog::TickCooldown();
			Consume::Tick();

			// Captured once per frame, not per event - see MenuContext.h.
			const auto state = MenuContext::Capture();

			// Off unless Settings::debugLog is set by hand (see Settings.h). Both traces below are
			// change-detected, so even when on this is a handful of lines per session.
			if (Settings::debugLog) {
				if (auto line = MenuContext::BuildDiagnosticLine(state); line != s_lastDiagLine) {
					REX::DEBUG("Smart Heal Button: [DIAG] {}", line);
					s_lastDiagLine = std::move(line);
				}
				// Per-frame health/radiation/active-effect watch, which catches a stimpak's
				// five-second effect and its perk-modified magnitude without the player needing
				// to press anything at the right moment. See Diagnostics.h.
				Diagnostics::Tick();
			}

			// A blocked frame can drop its events outright: this mod holds no multi-frame gesture
			// state that vanilla is waiting on.
			const bool blocked = MenuContext::ShouldBlock(state, Settings::blockMode);

			for (auto event = a_queueHead; event; event = event->next) {
				const auto button = const_cast<RE::InputEvent*>(event)->As<RE::ButtonEvent>();
				if (!button) {
					continue;
				}
				const auto keycode = ToUnifiedKeycode(*button);
				if (!keycode) {
					continue;
				}

				// The modifier's own press triggers nothing. It is tracked even while blocked, so
				// the flag reflects physical state when the block lifts.
				if (s_gamepadModifierKeycode != 0 && *keycode == s_gamepadModifierKeycode) {
					s_gamepadModifierHeld = RE::QPressed(*button);
					continue;
				}

				if (blocked) {
					continue;
				}

				// Fires on the press edge, not the release. This is a single instantaneous action
				// with no hold behaviour, so waiting for the release only adds latency.
				if (button->QJustPressed() && IsOurButton(*button, *keycode)) {
					// The full labelled dump goes out before anything is consumed, so a press is a
					// clean measurement of the state the solver was handed.
					Diagnostics::DumpFull("hotkey press");
					SmartHeal::Trigger();
				}
			}
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

		// Keyboard modifier support ("allowModifierKeys": true on the hotkey widget).
		//
		// Held state comes from GetAsyncKeyState rather than tracked ButtonEvents, which would first
		// need to establish whether a keyboard idCode is DIK or VK. Only consulted when one of this
		// mod's own key events arrives, so the game has focus.
		//
		// The bitmask below is undocumented; Keybinds.h logs the raw value on load. A wrong
		// assignment fails safe, because a modified binding then never matches.
		static constexpr std::int32_t kModShift = 1 << 0;
		static constexpr std::int32_t kModCtrl = 1 << 1;
		static constexpr std::int32_t kModAlt = 1 << 2;

		[[nodiscard]] static bool KeyboardModifiersSatisfied(std::int32_t a_modifiers)
		{
			if (a_modifiers == 0) {
				return true;  // no modifiers required - the overwhelmingly common case
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
				// MCM's "hotkey" widget can capture a mouse button or wheel too. The scroll wheel
				// arrives as a large BS_BUTTON_CODE sentinel rather than a small sequential index,
				// so it is special-cased ahead of the flat button-offset math below.
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

		static inline bool          _installed = false;
		static inline std::uint32_t s_gamepadKeycode = 0;
		static inline std::uint32_t s_gamepadModifierKeycode = 0;
		static inline bool          s_gamepadModifierHeld = false;

		// Previous rendered diagnostic line, for the change-detected trace above.
		static inline std::string s_lastDiagLine;

		using OriginalFunc = void(RE::BSInputEventReceiver*, const RE::InputEvent*);
		static inline REL::Relocation<OriginalFunc*> _original;
	};
}
