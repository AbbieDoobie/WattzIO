#pragma once

#include <array>
#include <optional>
#include <utility>

// NOGDI keeps <wingdi.h>'s bare `ERROR` macro from colliding with REX::ERROR.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOGDI
#include <Windows.h>

#include "Keybinds.h"
#include "MenuContext.h"
#include "QuickSlots.h"
#include "RestockOnKill.h"
#include "SearchEquip.h"
#include "Settings.h"
#include "ThrowMelee.h"

namespace TSO
{
	// Raw hook on PlayerCamera's BSInputEventReceiver base, covering keyboard, mouse and gamepad
	// uniformly through F4SE::InputMap's unified numbering, which is what MCM's Keybinds.json
	// stores.
	//
	// This mod ships no Papyrus or Quest, so MCM's own "action":"CallFunction" hotkey dispatch has
	// nothing to call into and this hook detects every hotkey press, in both modes.
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
			// F4RD:vtbl - [1] = BSInputEventReceiver subobject (offset 0x038)
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::PlayerCamera[1] };
			// F4RD:vfunc - slot 0 = PerformInputProcessing
			_original = vtbl.write_vfunc(0, &InputHook::PerformInputProcessing);
			REX::INFO("Throwing System Overhaul: BSInputEventReceiver hook installed on PlayerCamera"sv);
		}

		// The four gamepad-modifier-held flags below only update on a raw ButtonEvent for that
		// key, and this engine has no live "is this button down" poll API, so a release event
		// swallowed while a menu had input focus would leave a flag stuck true.
		//
		// Called from SettingsReload.h's MenuWatcher on any menu opening, not just PauseMenu.
		// Forgetting a still-held modifier costs one release and re-press; a stuck flag firing
		// an unwanted action later does not.
		static void ResetGamepadModifierState()
		{
			s_throwModHeld = false;
			s_meleeModHeld = false;
			s_quickSwapModHeld = false;
			s_searchEquipModHeld = false;
		}

	private:
		static void PerformInputProcessing(RE::BSInputEventReceiver* a_this, const RE::InputEvent* a_queueHead)
		{
			// The proximity-deferred restock-on-kill check rides on this per-frame call rather
			// than adding a second polling mechanism. Throttled inside Update().
			RestockOnKill::Update();

			// Dialogue and the vanilla quickloot prompt do not set RE::UI::menuMode, which counts only
			// true pausing menus, and a full-screen menu never reaches this hook at all. So non-pausing
			// menus need their own check, and MenuContext classifies them against crosshair button
			// prompts so a hotkey does not go inert from looking at an activatable object. Captured
			// once per frame.
			const auto state = MenuContext::Capture();

			// Off unless Settings::bAdvancedDebugLog is set by hand (see Settings.h).
			// Change-detected on the rendered line, so anything printed is also watched, and
			// even when on this is a handful of lines per session.
			if (Settings::bAdvancedDebugLog.GetValue()) {
				if (auto line = MenuContext::BuildDiagnosticLine(state); line != s_lastDiagLine) {
					REX::DEBUG("Throwing System Overhaul: [DIAG] {}", line);
					s_lastDiagLine = std::move(line);
				}
			}

			const bool blockHotkeys = MenuContext::ShouldBlock(state, Settings::BlockMode());

			// Deferred throw (see ThrowMelee.h). An equip is only queued, so a press that had to
			// equip something first cannot throw in the same frame - this fires it once the equip
			// lands. Both the auto-equip waterfall and Quick Slots' Throw After Equipping feed it.
			// Cancelled outright if a menu came up in between, so a throw can never fire into a menu.
			if (blockHotkeys) {
				ThrowMelee::CancelDeferredThrow();
			} else {
				ThrowMelee::Update();
			}

			if (!blockHotkeys) {
				for (auto event = a_queueHead; event; event = event->next) {
					// This is engine-owned mutable input-queue memory, exposed as
					// `const InputEvent*` only by PerformInputProcessing's signature. Engine mode
					// rewrites heldDownSecs on the real event before forwarding it (see
					// ThrowMelee.h).
					if (const auto button = const_cast<RE::InputEvent*>(event)->As<RE::ButtonEvent>(); button) {
						if (const auto keycode = ToUnifiedKeycode(*button); keycode) {
							HandleHotkey(*keycode, *button);
						}
					}
				}
			}

			_original(a_this, a_queueHead);
		}


		// Gamepad Throw and Melee hotkeys carry an optional modifier key. Keyboard and mouse
		// hotkeys come from the shared Keybinds.json instead, matched through
		// KeyboardHotkeyMatches so that file's `modifiers` field is honoured.
		//
		// Not gated to just-pressed: Engine mode needs both press and release for throw, and
		// release only for melee, so press/release filtering happens inside TryThrow and
		// TryMelee per mode.
		static void HandleHotkey(std::uint32_t a_keycode, RE::ButtonEvent& a_event)
		{
			const auto throwGamepadMod = GamepadDropdownToKeycode(Settings::iThrowGamepadModifier.GetValue());
			const auto meleeGamepadMod = GamepadDropdownToKeycode(Settings::iMeleeGamepadModifier.GetValue());
			const auto quickSwapGamepadMod = GamepadDropdownToKeycode(Settings::iQuickSwapGamepadModifier.GetValue());
			const auto searchEquipGamepadMod = GamepadDropdownToKeycode(Settings::iSearchEquipGamepadModifier.GetValue());

			if (throwGamepadMod > 0 && a_keycode == throwGamepadMod) {
				s_throwModHeld = RE::QPressed(a_event);
				return;
			}
			if (meleeGamepadMod > 0 && a_keycode == meleeGamepadMod) {
				s_meleeModHeld = RE::QPressed(a_event);
				return;
			}
			if (quickSwapGamepadMod > 0 && a_keycode == quickSwapGamepadMod) {
				s_quickSwapModHeld = RE::QPressed(a_event);
				return;
			}
			if (searchEquipGamepadMod > 0 && a_keycode == searchEquipGamepadMod) {
				s_searchEquipModHeld = RE::QPressed(a_event);
				return;
			}

			const auto throwGamepadKey = GamepadDropdownToKeycode(Settings::iThrowGamepadKey.GetValue());
			const auto meleeGamepadKey = GamepadDropdownToKeycode(Settings::iMeleeGamepadKey.GetValue());
			const auto throwKeybind = Keybinds::Get("ThrowKeyboardHotkey"sv);
			const auto meleeKeybind = Keybinds::Get("MeleeKeyboardHotkey"sv);

			const bool isThrow = (throwGamepadKey > 0 && a_keycode == throwGamepadKey && (throwGamepadMod == 0 || s_throwModHeld)) ||
			                     KeyboardHotkeyMatches(throwKeybind, a_keycode);
			const bool isMelee = (meleeGamepadKey > 0 && a_keycode == meleeGamepadKey && (meleeGamepadMod == 0 || s_meleeModHeld)) ||
			                     KeyboardHotkeyMatches(meleeKeybind, a_keycode);

			if (isThrow) {
				ThrowMelee::TryThrow(a_event);
				return;
			}
			if (isMelee) {
				ThrowMelee::TryMelee(a_event);
				return;
			}

			// Quick Slot 1-4 keys are checked before the press-only gate below, unlike Cycle and
			// Search and Equip, because the clear-on-hold feature needs the continuously-held
			// events between press and release.
			static constexpr std::array<std::pair<int, const char*>, 4> kQuickSwapSlots{ {
				{ 1, "QuickSwap1KeyboardHotkey" },
				{ 2, "QuickSwap2KeyboardHotkey" },
				{ 3, "QuickSwap3KeyboardHotkey" },
				{ 4, "QuickSwap4KeyboardHotkey" },
			} };

			for (const auto& [slot, keybindID] : kQuickSwapSlots) {
				const auto gamepadKey = GamepadDropdownToKeycode(GetQuickSwapGamepadKey(slot));
				const auto keybind = Keybinds::Get(keybindID);
				if ((gamepadKey > 0 && a_keycode == gamepadKey && (quickSwapGamepadMod == 0 || s_quickSwapModHeld)) ||
					KeyboardHotkeyMatches(keybind, a_keycode)) {
					HandleQuickSlotKey(slot, a_event);
					return;
				}
			}

			// Cycle and Search and Equip: instant, press only, no hold semantics.
			if (!a_event.QJustPressed()) {
				return;
			}

			const auto cycleGamepadKey = GamepadDropdownToKeycode(Settings::iQuickSwapCycleGamepadKey.GetValue());
			const auto cycleKeybind = Keybinds::Get("QuickSwapCycleKeyboardHotkey"sv);
			if ((cycleGamepadKey > 0 && a_keycode == cycleGamepadKey && (quickSwapGamepadMod == 0 || s_quickSwapModHeld)) ||
				KeyboardHotkeyMatches(cycleKeybind, a_keycode)) {
				QuickSlots::CycleToNextSlot();
				return;
			}

			const auto searchEquipGamepadKey = GamepadDropdownToKeycode(Settings::iSearchEquipGamepadKey.GetValue());
			const auto searchEquipKeybind = Keybinds::Get("SearchEquipKeyboardHotkey"sv);
			if ((searchEquipGamepadKey > 0 && a_keycode == searchEquipGamepadKey && (searchEquipGamepadMod == 0 || s_searchEquipModHeld)) ||
				KeyboardHotkeyMatches(searchEquipKeybind, a_keycode)) {
				SearchEquip::FindAndEquipFirstThrowable();
				return;
			}
		}

		// Press equips or saves (QuickSlots::UpdateSlot). Held, if Clear on Hold is enabled and
		// heldDownSecs crosses the threshold, clears the slot instead - once per gesture, guarded by
		// s_quickSlotClearFired and requiring RE::UI::menuMode == 0. Release resets the guard.
		static void HandleQuickSlotKey(int a_slot, RE::ButtonEvent& a_event)
		{
			if (a_event.QJustPressed()) {
				if (QuickSlots::UpdateSlot(a_slot)) {
					// Only a press that actually equipped can throw - see
					// ThrowMelee::TryQuickSlotThrow, which is a no-op unless the option is on.
					ThrowMelee::TryQuickSlotThrow();
				}
				s_quickSlotClearFired[a_slot - 1] = false;
				return;
			}
			if (RE::QReleased(a_event)) {
				s_quickSlotClearFired[a_slot - 1] = false;
				return;
			}

			if (s_quickSlotClearFired[a_slot - 1]) {
				return;
			}
			const auto thresholdSecs = QuickSlots::GetClearHoldThresholdSecs();
			if (thresholdSecs <= 0.0f || a_event.heldDownSecs < thresholdSecs) {
				return;
			}
			if (const auto ui = RE::UI::GetSingleton(); !ui || ui->menuMode != 0) {
				return;
			}

			s_quickSlotClearFired[a_slot - 1] = true;
			QuickSlots::ClearSlot(a_slot);
		}

		// Keyboard and mouse hotkeys are stored in the shared Keybinds.json with a `modifiers`
		// bitmask, and every hotkey widget in config.json sets "allowModifierKeys": true, so a
		// binding can carry Shift, Ctrl or Alt. Matching on the keycode alone would fire the
		// hotkey on the bare key.
		[[nodiscard]] static bool KeyboardHotkeyMatches(const Keybinds::Entry* a_entry, std::uint32_t a_keycode)
		{
			return a_entry && static_cast<std::uint32_t>(a_entry->keycode) == a_keycode &&
			       KeyboardModifiersSatisfied(a_entry->modifiers);
		}

		// Held state comes from GetAsyncKeyState rather than tracked ButtonEvents, which would
		// first need to establish whether a keyboard idCode is a scan code or a virtual-key code.
		// Only consulted once one of this mod's own key events has arrived, so the game has focus.
		//
		// MCM does not document the bitmask. A wrong assignment fails safe: a modified binding
		// never matches.
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

		[[nodiscard]] static std::int32_t GetQuickSwapGamepadKey(int a_slot)
		{
			switch (a_slot) {
			case 1:
				return Settings::iQuickSwap1GamepadKey.GetValue();
			case 2:
				return Settings::iQuickSwap2GamepadKey.GetValue();
			case 3:
				return Settings::iQuickSwap3GamepadKey.GetValue();
			case 4:
				return Settings::iQuickSwap4GamepadKey.GetValue();
			default:
				return 0;
			}
		}

		// Every *GamepadKey and *GamepadModifier setting must be converted before it can be
		// compared against a_keycode. The MCM gamepad dropdown stores the raw 0-based option
		// index (17 options: None plus 16 buttons, in the order below) as its ModSettingInt
		// value, while a real gamepad ButtonEvent's unified keycode runs 266 (DPAD_UP) to 281
		// (RT). Without this conversion no non-None gamepad binding can ever match a press.
		[[nodiscard]] static std::uint32_t GamepadDropdownToKeycode(std::int32_t a_dropdownValue)
		{
			if (a_dropdownValue <= 0 || a_dropdownValue > 16) {
				return 0;  // "None", or out of range
			}
			return static_cast<std::uint32_t>(F4SE::InputMap::kMacro_GamepadOffset) + static_cast<std::uint32_t>(a_dropdownValue) - 1;
		}

		static inline bool s_throwModHeld = false;
		static inline bool s_meleeModHeld = false;
		static inline bool s_quickSwapModHeld = false;
		static inline bool s_searchEquipModHeld = false;
		// Per-slot clear-on-hold guard (index 0 is slot 1). True once the hold threshold has
		// fired for the gesture in progress, reset on release.
		static inline std::array<bool, 4> s_quickSlotClearFired{};

		// Converts a ButtonEvent's device-relative idCode into the unified 0-281 numbering
		// F4SE::InputMap and MCM's Keybinds.json both use.
		[[nodiscard]] static std::optional<std::uint32_t> ToUnifiedKeycode(const RE::ButtonEvent& a_event)
		{
			switch (a_event.device.get()) {
			case RE::INPUT_DEVICE::kKeyboard:
				return static_cast<std::uint32_t>(a_event.QIDCode());
			case RE::INPUT_DEVICE::kMouse:
				// The scroll wheel arrives as a large BS_BUTTON_CODE sentinel (kWheelUp=0x800,
				// kWheelDown=0x900) rather than a small sequential button index, so it is
				// special-cased ahead of the flat button-offset math below.
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
