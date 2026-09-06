#pragma once

#include <cstdint>
#include <string>

#include "Bindings.h"

// Names for a live ControlMap value, so a status line can say which key an action is on even
// after the player has remapped it away from the vanilla default.
//
// ControlMap uses a different numbering per device, and none of them are the F4SE unified
// keycodes that Keybinds.json stores:
//
//   keyboard  Windows Virtual-Key codes (VK_*), same as CustomControlMap.txt
//   mouse     0-based button index, matching RE::INPUT_DEVICE::kMouse
//   gamepad   XInput button bitmask, plus 9 and 10 as Bethesda's pseudo-IDs for the triggers
//
// Names are not translated - they are what is printed on the hardware.
namespace UnbindAny::KeyNames
{
	// Empty when the code has no name here, which the caller reports as a bare "Bound" rather
	// than inventing one.
	[[nodiscard]] inline std::string Keyboard(std::int32_t a_vk)
	{
		switch (a_vk) {
		case 0x08: return "Backspace";
		case 0x09: return "Tab";
		case 0x0D: return "Enter";
		case 0x13: return "Pause";
		case 0x14: return "Caps Lock";
		case 0x1B: return "Esc";
		case 0x20: return "Space";
		case 0x21: return "Page Up";
		case 0x22: return "Page Down";
		case 0x23: return "End";
		case 0x24: return "Home";
		case 0x25: return "Left Arrow";
		case 0x26: return "Up Arrow";
		case 0x27: return "Right Arrow";
		case 0x28: return "Down Arrow";
		case 0x2C: return "Print Screen";
		case 0x2D: return "Insert";
		case 0x2E: return "Delete";
		case 0x5B: return "Left Windows";
		case 0x5C: return "Right Windows";
		case 0x5D: return "Menu (Apps)";
		case 0x6A: return "NumPad *";
		case 0x6B: return "NumPad +";
		case 0x6D: return "NumPad -";
		case 0x6E: return "NumPad .";
		case 0x6F: return "NumPad /";
		case 0x90: return "Num Lock";
		case 0x91: return "Scroll Lock";
		case 0xA0: return "Left Shift";
		case 0xA1: return "Right Shift";
		case 0xA2: return "Left Ctrl";
		case 0xA3: return "Right Ctrl";
		case 0xA4: return "Left Alt";
		case 0xA5: return "Right Alt";
		// Spelled out rather than returned as the literal character: consumer mods push this
		// text through an ini, and their SanitiseForIni blanks = ; [ ] and quotes.
		case 0xBA: return "Semicolon";
		case 0xBB: return "Equals";
		case 0xBC: return "Comma";
		case 0xBD: return "Minus";
		case 0xBE: return "Period";
		case 0xBF: return "Forward Slash";
		case 0xC0: return "Backtick";
		case 0xDB: return "Left Bracket";
		case 0xDC: return "Backslash";
		case 0xDD: return "Right Bracket";
		case 0xDE: return "Apostrophe";
		default: break;
		}

		// Contiguous ranges, named from the code rather than listed one by one.
		if ((a_vk >= 0x41 && a_vk <= 0x5A) || (a_vk >= 0x30 && a_vk <= 0x39)) {  // A-Z, 0-9
			return std::string(1, static_cast<char>(a_vk));
		}
		if (a_vk >= 0x60 && a_vk <= 0x69) {  // NumPad 0-9
			return "NumPad " + std::to_string(a_vk - 0x60);
		}
		if (a_vk >= 0x70 && a_vk <= 0x87) {  // F1-F24
			return "F" + std::to_string(a_vk - 0x70 + 1);
		}
		return {};
	}

	[[nodiscard]] inline std::string Mouse(std::int32_t a_index)
	{
		switch (a_index) {
		case 0: return "Mouse: Left Click";
		case 1: return "Mouse: Right Click";
		case 2: return "Mouse: Middle Click";
		default: break;
		}
		if (a_index >= 3 && a_index <= 7) {
			return "Mouse " + std::to_string(a_index + 1);
		}
		return {};
	}

	// These match Bindings.h's own displayGamepad strings, so a remapped button reads the same
	// way a default one does.
	[[nodiscard]] inline std::string Gamepad(std::int32_t a_value)
	{
		switch (a_value) {
		case 0x0001: return "D-Pad Up";
		case 0x0002: return "D-Pad Down";
		case 0x0004: return "D-Pad Left";
		case 0x0008: return "D-Pad Right";
		case 9: return "Left Trigger (LT)";    // Bethesda pseudo-ID, not an XInput mask
		case 10: return "Right Trigger (RT)";  // likewise
		case 0x0010: return "Start / Menu Button";
		case 0x0020: return "Back / View Button";
		case 0x0040: return "Left Stick Click (L3)";
		case 0x0080: return "Right Stick Click (R3)";
		case 0x0100: return "Left Bumper (LB)";
		case 0x0200: return "Right Bumper (RB)";
		case 0x1000: return "A Button";
		case 0x2000: return "B Button";
		case 0x4000: return "X Button";
		case 0x8000: return "Y Button";
		default: return {};
		}
	}

	// Keyboard and mouse are one slot to the engine, so both halves are named and joined the way
	// Bindings.h writes a combined default ("V / Mouse: Middle Click").
	[[nodiscard]] inline std::string KeyboardAndMouse(std::int32_t a_keyboard, std::int32_t a_mouse)
	{
		const auto kb = a_keyboard != Bindings::kUnbound ? Keyboard(a_keyboard) : std::string{};
		const auto ms = a_mouse != Bindings::kUnbound ? Mouse(a_mouse) : std::string{};

		if (!kb.empty() && !ms.empty()) {
			return kb + " / " + ms;
		}
		return kb.empty() ? ms : kb;
	}
}
