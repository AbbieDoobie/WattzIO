#pragma once

// Dropdown-index to XInput-bitmask tables for this mod's three gamepad button pickers.
namespace PipboyPipbindFix::ControlRemap
{
	// XInput bitmask for each gamepad button, indexed by the Close GP dropdown option index.
	// Close dropdown: 0 = SameAsOpen, 1 = OFF, 2-17 = buttons (DPad Up through RT).
	inline constexpr std::array<std::int32_t, 16> kCloseGPXInput{{
		0x0001,  // [2]  DPad Up
		0x0002,  // [3]  DPad Down
		0x0004,  // [4]  DPad Left
		0x0008,  // [5]  DPad Right
		0x0010,  // [6]  Start
		0x0020,  // [7]  Back
		0x0040,  // [8]  LS
		0x0080,  // [9]  RS
		0x0100,  // [10] LB
		0x0200,  // [11] RB
		0x1000,  // [12] A
		0x2000,  // [13] B
		0x4000,  // [14] X
		0x8000,  // [15] Y
		0x0009,  // [16] LT
		0x000A,  // [17] RT
	}};

	// XInput bitmask for each gamepad button, indexed by the Zoom GP dropdown option index.
	// Zoom dropdown: 0 = OFF, 1-16 = buttons (DPad Up through RT).
	inline constexpr std::array<std::int32_t, 16> kZoomGPXInput{{
		0x0001,  // [1]  DPad Up
		0x0002,  // [2]  DPad Down
		0x0004,  // [3]  DPad Left
		0x0008,  // [4]  DPad Right
		0x0010,  // [5]  Start
		0x0020,  // [6]  Back
		0x0040,  // [7]  LS
		0x0080,  // [8]  RS
		0x0100,  // [9]  LB
		0x0200,  // [10] RB
		0x1000,  // [11] A
		0x2000,  // [12] B
		0x4000,  // [13] X
		0x8000,  // [14] Y
		0x0009,  // [15] LT
		0x000A,  // [16] RT
	}};

	// The Companion Order Mode exit dropdown offers the same option list as Pipboy Close, so it
	// indexes the same table. An alias rather than a copy: these tables are indexed positionally,
	// so two copies that drifted apart would mis-map buttons rather than fail to build.
	inline constexpr const auto& kOrderExitGPXInput = kCloseGPXInput;
}
