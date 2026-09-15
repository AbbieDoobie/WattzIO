#pragma once

#include "DeviceTracker.h"
#include "Settings.h"

namespace FalloutInputSwapper::GlyphState
{
	// One signal drives both icon selection and cursor visibility. Cursor visibility on every
	// menu other than the Pipboy reads the same IsGamepadConnected/UsingGamepad pair redirected
	// in InputDevicePatches.h, so the two cannot be split without breaking the cursor everywhere
	// except PipboyCursorRetarget::Sync, which has its own path.
	[[nodiscard]] inline bool IsGamepadActive()
	{
		using DT = DeviceTracker;
		const auto tracker = DT::GetSingleton();
		const auto hysteresis = std::chrono::seconds(Settings::GetHysteresisSeconds());

		switch (Settings::GetInputPreference()) {
		case Settings::InputPreference::kGamepad:
			return tracker->GetPreferredModeDevice(DT::GlyphDevice::kGamepad, hysteresis) == DT::GlyphDevice::kGamepad;
		case Settings::InputPreference::kKBM:
			return tracker->GetPreferredModeDevice(DT::GlyphDevice::kKBM, hysteresis) == DT::GlyphDevice::kGamepad;
		case Settings::InputPreference::kAuto:
		default:
			return tracker->GetAutoGlyphDevice() == DT::GlyphDevice::kGamepad;
		}
	}
}
