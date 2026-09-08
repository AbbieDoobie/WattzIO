#pragma once

#include <array>
#include <format>
#include <string>
#include <string_view>

// Tables converting a stored key or button code into something human-readable, or into what
// the HUD widget's SetButtons() expects.
namespace ARC::InputLabels
{
	// --- Gamepad ---
	//
	// MCM's "hotkey" widget does not reliably capture gamepad presses, so gamepad binding is a
	// 17-option dropdown instead. It stores the selected index (0-16), not a keycode.

	// Table order matches config.json's own gamepad dropdown option order (None, DPad
	// Up/Down/Left/Right, Start, Select, LS, RS, LB, RB, A, B, X, Y, LT, RT), and
	// F4SE::InputMap's unified numbering (kMacro_GamepadOffset=266 = DPad Up, ascending).
	inline constexpr std::array<std::int32_t, 17> kGamepadDropdownKeycodes{
		0, 266, 267, 268, 269, 270, 271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281
	};

	// Same order, same index - the "Xenon_*" glyph name Shared.AS3.BSButtonHint's own
	// NameToTextMap expects when pushed into a BSButtonHintData's XenonButton field. D-pad
	// directions are the one irregularity - "Up"/"Down"/"Left"/"Right" with no "Xenon_" prefix.
	inline constexpr std::array<std::string_view, 17> kGamepadDropdownGlyphNames{
		""sv, "Up"sv, "Down"sv, "Left"sv, "Right"sv, "Xenon_Start"sv, "Xenon_Select"sv,
		"Xenon_L3"sv, "Xenon_R3"sv, "Xenon_L1"sv, "Xenon_R1"sv, "Xenon_A"sv, "Xenon_B"sv,
		"Xenon_X"sv, "Xenon_Y"sv, "Xenon_L2"sv, "Xenon_R2"sv
	};

	[[nodiscard]] inline std::int32_t GamepadDropdownIndexToKeycode(std::int32_t a_index)
	{
		if (a_index <= 0 || static_cast<std::size_t>(a_index) >= kGamepadDropdownKeycodes.size()) {
			return 0;  // 0 = "None" - never a valid keycode to match against
		}
		return kGamepadDropdownKeycodes[static_cast<std::size_t>(a_index)];
	}

	[[nodiscard]] inline std::string_view GamepadDropdownIndexToGlyphName(std::int32_t a_index)
	{
		if (a_index <= 0 || static_cast<std::size_t>(a_index) >= kGamepadDropdownGlyphNames.size()) {
			return ""sv;
		}
		return kGamepadDropdownGlyphNames[static_cast<std::size_t>(a_index)];
	}

	// Reverse of the above: a live gamepad keycode to its glyph name. Empty for anything that is
	// not one of the 16 known buttons, which callers should treat as "nothing to show".
	[[nodiscard]] inline std::string_view GamepadKeycodeToGlyphName(std::int32_t a_keycode)
	{
		for (std::size_t i = 1; i < kGamepadDropdownKeycodes.size(); ++i) {
			if (kGamepadDropdownKeycodes[i] == a_keycode) {
				return kGamepadDropdownGlyphNames[i];
			}
		}
		return ""sv;
	}

	// --- Keyboard / mouse ---
	//
	// MCM's "hotkey" widget writes F4SE::InputMap's unified 0-281 numbering into the shared
	// Keybinds.json. CustomControlMap.txt's keyboard column is Windows VK codes, not DirectInput
	// scancodes. This table covers the keys likely to be picked as a hotkey;
	// GetKeyboardMouseDisplayText() falls back to a generic label for the rest.

	[[nodiscard]] inline std::string_view VirtualKeyToDisplayText(std::int32_t a_vk)
	{
		switch (a_vk) {
		case 0x08: return "Backspace"sv;
		case 0x09: return "Tab"sv;
		case 0x0D: return "Enter"sv;
		case 0x13: return "Pause"sv;
		case 0x14: return "Caps Lock"sv;
		case 0x1B: return "Esc"sv;
		case 0x20: return "Space"sv;
		case 0x21: return "Page Up"sv;
		case 0x22: return "Page Down"sv;
		case 0x23: return "End"sv;
		case 0x24: return "Home"sv;
		case 0x25: return "Left Arrow"sv;
		case 0x26: return "Up Arrow"sv;
		case 0x27: return "Right Arrow"sv;
		case 0x28: return "Down Arrow"sv;
		case 0x2C: return "Print Screen"sv;
		case 0x2D: return "Insert"sv;
		case 0x2E: return "Delete"sv;
		case 0x30: return "0"sv;
		case 0x31: return "1"sv;
		case 0x32: return "2"sv;
		case 0x33: return "3"sv;
		case 0x34: return "4"sv;
		case 0x35: return "5"sv;
		case 0x36: return "6"sv;
		case 0x37: return "7"sv;
		case 0x38: return "8"sv;
		case 0x39: return "9"sv;
		case 0x41: return "A"sv;
		case 0x42: return "B"sv;
		case 0x43: return "C"sv;
		case 0x44: return "D"sv;
		case 0x45: return "E"sv;
		case 0x46: return "F"sv;
		case 0x47: return "G"sv;
		case 0x48: return "H"sv;
		case 0x49: return "I"sv;
		case 0x4A: return "J"sv;
		case 0x4B: return "K"sv;
		case 0x4C: return "L"sv;
		case 0x4D: return "M"sv;
		case 0x4E: return "N"sv;
		case 0x4F: return "O"sv;
		case 0x50: return "P"sv;
		case 0x51: return "Q"sv;
		case 0x52: return "R"sv;
		case 0x53: return "S"sv;
		case 0x54: return "T"sv;
		case 0x55: return "U"sv;
		case 0x56: return "V"sv;
		case 0x57: return "W"sv;
		case 0x58: return "X"sv;
		case 0x59: return "Y"sv;
		case 0x5A: return "Z"sv;
		case 0x60: return "Num 0"sv;
		case 0x61: return "Num 1"sv;
		case 0x62: return "Num 2"sv;
		case 0x63: return "Num 3"sv;
		case 0x64: return "Num 4"sv;
		case 0x65: return "Num 5"sv;
		case 0x66: return "Num 6"sv;
		case 0x67: return "Num 7"sv;
		case 0x68: return "Num 8"sv;
		case 0x69: return "Num 9"sv;
		case 0x6A: return "Num *"sv;
		case 0x6B: return "Num +"sv;
		case 0x6D: return "Num -"sv;
		case 0x6E: return "Num ."sv;
		case 0x6F: return "Num /"sv;
		case 0x70: return "F1"sv;
		case 0x71: return "F2"sv;
		case 0x72: return "F3"sv;
		case 0x73: return "F4"sv;
		case 0x74: return "F5"sv;
		case 0x75: return "F6"sv;
		case 0x76: return "F7"sv;
		case 0x77: return "F8"sv;
		case 0x78: return "F9"sv;
		case 0x79: return "F10"sv;
		case 0x7A: return "F11"sv;
		case 0x7B: return "F12"sv;
		case 0x90: return "Num Lock"sv;
		case 0x91: return "Scroll Lock"sv;
		case 0xA0: return "L Shift"sv;
		case 0xA1: return "R Shift"sv;
		case 0xA2: return "L Ctrl"sv;
		case 0xA3: return "R Ctrl"sv;
		case 0xA4: return "L Alt"sv;
		case 0xA5: return "R Alt"sv;
		case 0xBA: return ";"sv;
		case 0xBB: return "="sv;
		case 0xBC: return ","sv;
		case 0xBD: return "-"sv;
		case 0xBE: return "."sv;
		case 0xBF: return "/"sv;
		case 0xC0: return "`"sv;
		case 0xDB: return "["sv;
		case 0xDC: return "\\"sv;
		case 0xDD: return "]"sv;
		case 0xDE: return "'"sv;
		default: return {};  // unmapped - caller falls back to a generic label
		}
	}

	[[nodiscard]] inline std::string_view MouseButtonToDisplayText(std::int32_t a_buttonIndex)
	{
		switch (a_buttonIndex) {
		case 0: return "Mouse 1 (Left)"sv;
		case 1: return "Mouse 2 (Right)"sv;
		case 2: return "Mouse 3 (Middle)"sv;
		case 3: return "Mouse 4"sv;
		case 4: return "Mouse 5"sv;
		default: return {};
		}
	}

	// Converts a raw F4SE::InputMap unified keycode (as read from Keybinds.json - never a
	// gamepad value here, that's the dropdown's job above) into display text for the
	// on-screen prompt. Always returns something non-empty, even for an unmapped key, so an
	// unusual hotkey choice degrades to "still shows a label" rather than a blank one.
	[[nodiscard]] inline std::string GetKeyboardMouseDisplayText(std::int32_t a_unifiedKeycode)
	{
		if (a_unifiedKeycode == static_cast<std::int32_t>(F4SE::InputMap::kMacro_MouseWheelOffset)) {
			return "Wheel Up";
		}
		if (a_unifiedKeycode == static_cast<std::int32_t>(F4SE::InputMap::kMacro_MouseWheelOffset) + 1) {
			return "Wheel Down";
		}
		if (a_unifiedKeycode >= static_cast<std::int32_t>(F4SE::InputMap::kMacro_MouseButtonOffset)) {
			const auto rawButton = a_unifiedKeycode - static_cast<std::int32_t>(F4SE::InputMap::kMacro_MouseButtonOffset);
			const auto text = MouseButtonToDisplayText(rawButton);
			return text.empty() ? std::format("Mouse Button {}", rawButton + 1) : std::string{ text };
		}
		const auto text = VirtualKeyToDisplayText(a_unifiedKeycode);
		return text.empty() ? std::format("Key {:#04x}", a_unifiedKeycode) : std::string{ text };
	}
}
