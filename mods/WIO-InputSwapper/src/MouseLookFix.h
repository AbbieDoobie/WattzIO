#pragma once

namespace FalloutInputSwapper
{
	// FO4's mouse-look math applies gamepad scaling, tuned for a normalised -1..1 stick value, to
	// whichever device's raw delta arrived that frame, so with both devices live the mouse is
	// wildly over-scaled. The look vector is computed here instead, from the INI mouse
	// sensitivity, for every real mouse delta and regardless of what the engine reports as active.
	inline void ApplyMouseLook(std::int32_t a_mouseInputX, std::int32_t a_mouseInputY)
	{
		if (a_mouseInputX == 0 && a_mouseInputY == 0) {
			return;
		}

		const auto playerControls = RE::PlayerControls::GetSingleton();
		if (!playerControls) {
			return;
		}

		const auto sensitivity = RE::GetINISetting("fMouseHeadingSensitivity:Controls"sv);
		const auto xScale = RE::GetINISetting("fMouseHeadingXScale:Controls"sv);
		const auto yScale = RE::GetINISetting("fMouseHeadingYScale:Controls"sv);
		if (!sensitivity || !xScale || !yScale) {
			return;  // not resolved yet (too early in load) - let this one frame fall through to vanilla
		}

		// Self-measured frame delta, clamped so a hitch cannot produce one huge spike. Not
		// bit-identical to the engine's own frame time, which has no verified address here.
		static auto s_lastCall = std::chrono::steady_clock::now();
		const auto now = std::chrono::steady_clock::now();
		const float deltaTime = std::clamp(std::chrono::duration<float>(now - s_lastCall).count(), 1.0f / 240.0f, 1.0f / 15.0f);
		s_lastCall = now;

		auto& lookVec = playerControls->data.lookInputVec;
		lookVec.x = ((sensitivity->GetFloat() * xScale->GetFloat()) / deltaTime) * static_cast<float>(a_mouseInputX);
		lookVec.y = ((sensitivity->GetFloat() * yScale->GetFloat()) / deltaTime) * -static_cast<float>(a_mouseInputY);
	}

}
