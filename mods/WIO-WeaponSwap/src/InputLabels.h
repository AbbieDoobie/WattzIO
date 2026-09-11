#pragma once

#include <array>
#include <cstdint>

// Gamepad dropdown to real keycode conversion. MCM's dropdown widget stores the selected option's
// raw 0-based index, not an engine keycode, so this table converts that index into
// F4SE::InputMap's unified keycode space before it is compared against a live ButtonEvent.
namespace WS::InputLabels
{
	// Table order matches config.json's gamepad dropdown option order exactly (None, DPad
	// Up/Down/Left/Right, Start, Select, LS, RS, LB, RB, A, B, X, Y, LT, RT), and
	// F4SE::InputMap's unified numbering (kMacro_GamepadOffset = 266 = DPad Up, ascending).
	inline constexpr std::array<std::int32_t, 17> kGamepadDropdownKeycodes{
		0, 266, 267, 268, 269, 270, 271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281
	};

	[[nodiscard]] inline std::int32_t GamepadDropdownIndexToKeycode(std::int32_t a_index)
	{
		if (a_index <= 0 || static_cast<std::size_t>(a_index) >= kGamepadDropdownKeycodes.size()) {
			return 0;  // 0 = "None" - never a valid keycode to match against
		}
		return kGamepadDropdownKeycodes[static_cast<std::size_t>(a_index)];
	}
}
