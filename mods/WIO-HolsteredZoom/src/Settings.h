#pragma once

namespace ZoomOffhand::Settings
{
	// Reads Data\MCM\Settings\WIO-HolsteredZoom.ini through REX::TIniSetting/FIniSettingStore.
	// The zoom multiplier and transition speed are conceptually floats but stored as whole-number
	// ints, MCM widgets binding only to Bool/Int/String; GetZoomMultiplier() and
	// GetZoomSpeedSeconds() convert back.

	// Master on/off switch.
	inline REX::TIniSetting<bool> bEnabled{ "Settings", "bEnabled", true };

	// Zoom strength as a whole-number percent of the current FOV at full zoom; lower means more
	// zoom. MCM slider range 30-95, step 5.
	inline REX::TIniSetting<std::int32_t> iZoomPercent{ "Settings", "iZoomPercent", 75 };

	// Milliseconds to blend fully in or fully out, one shared value for both directions. MCM
	// slider range 0-1000, step 25.
	inline REX::TIniSetting<std::int32_t> iZoomSpeedMs{ "Settings", "iZoomSpeedMs", 100 };

	inline REX::TIniSetting<bool> bApplyInFirstPerson{ "Settings", "bApplyInFirstPerson", true };
	inline REX::TIniSetting<bool> bApplyInThirdPerson{ "Settings", "bApplyInThirdPerson", true };

	// Gamepad only: the blend target tracks the SecondaryAttack event's raw analog value, so a
	// light pull partially zooms. Default off, because whether FO4's trigger events carry a
	// graduated value or a binary post-deadzone 0/1 is unconfirmed.
	inline REX::TIniSetting<bool> bAnalogTriggerScaling{ "Settings", "bAnalogTriggerScaling", false };

	// Optional Immersive HUD integration; see IHudReveal.h. 0 = Off (default), 1 = reveal the
	// compass while zoomed, 2 = reveal the whole HUD.
	inline REX::TIniSetting<std::int32_t> iHudRevealMode{ "Settings", "iHudRevealMode", 0 };

	// FOV multiplier at full zoom, e.g. 85 -> 0.85F.
	[[nodiscard]] inline float GetZoomMultiplier()
	{
		return static_cast<float>(iZoomPercent.GetValue()) / 100.0F;
	}

	// Seconds to blend fully in or out, e.g. 150 -> 0.15F. Floored to a small positive minimum,
	// so a 0ms MCM value cannot divide by zero in the per-frame blend step.
	[[nodiscard]] inline float GetZoomSpeedSeconds()
	{
		return std::max(static_cast<float>(iZoomSpeedMs.GetValue()) / 1000.0F, 0.001F);
	}

	// Both paths point at the same file: MCM produces one flat settings.ini, not a base plus
	// override pair.
	inline void Load()
	{
		constexpr auto path = "Data/MCM/Settings/WIO-HolsteredZoom.ini";
		REX::FIniSettingStore::GetSingleton()->Init(path, path);
		REX::FIniSettingStore::GetSingleton()->Load();
		REX::INFO("Holstered Zoom: settings loaded from {}"sv, path);
	}
}
