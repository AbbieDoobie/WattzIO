#pragma once

#include <array>
#include <Windows.h>
#ifdef ERROR
// Windows.h defines ERROR as 0 via WinError.h; undef it so REX::ERROR compiles correctly.
#undef ERROR
#endif

#include "MenuContext.h"

// Reads MCM's runtime settings file directly via Win32 GetPrivateProfile* APIs.
// GetPrivateProfileIntA parses decimal integers, which is what MCM writes for all
// ModSettingInt controls. This plugin never writes to the ini - MCM owns it exclusively.
namespace QT::Settings
{
	namespace detail
	{
		constexpr auto kSettingsPath = "Data/MCM/Settings/WIO-QuickTurn.ini";

		[[nodiscard]] inline bool GetBool(const char* a_section, const char* a_key, bool a_default)
		{
			return WIO::Ini::GetBool(a_section, a_key, a_default, kSettingsPath);
		}

		[[nodiscard]] inline std::int32_t GetInt(const char* a_section, const char* a_key, std::int32_t a_default)
		{
			return static_cast<std::int32_t>(::GetPrivateProfileIntA(a_section, a_key, a_default, kSettingsPath));
		}
	}

	// Shared Action section 0=Off, 1=Sneak, 2=Sprint,
	// 3=Activate, 4=Ready/Reload
	inline std::int32_t iContextualAction = 0;
	// 0=Back Only, 1=Cardinals Only, 2=Omnidirectional
	inline std::int32_t iContextualMoveMod = 0;

	// Direct Input section MCM stores the selected option index (0-16), not a real
	// keycode. Convert via GamepadDropdownIndexToKeycode() before comparing against
	// ButtonEvents.
	inline std::int32_t iQuickTurnGamepadKey = 0;
	// Same dropdown index space. 0 (None) = no modifier required. Direct Input only - Shared
	// Action never reads it.
	inline std::int32_t iQuickTurnGamepadModifier = 0;
	// Milliseconds. 0 = instant snap.
	inline std::int32_t iQuickTurnDurationMs = 100;
	// 0=Off, 1=Back Only, 2=Cardinals Only, 3=Omnidirectional
	inline std::int32_t iQuickTurnMovementModifier = 0;

	// Perspective section. First person keeps the original heading-only turn. Third person has a
	// camera that can point away from the character (while the weapon is holstered), so it gets a
	// choice of what to turn. Index order matches config.json's options.
	enum class ThirdPersonMode : std::int32_t
	{
		kOff = 0,
		kTurnCamera = 1,
		kTurnPlayer = 2,
		kTurnBoth = 3,
	};
	inline bool bApplyInFirstPerson = true;
	inline ThirdPersonMode thirdPersonMode = ThirdPersonMode::kTurnCamera;

	// Advanced section. Suppresses Quick Turn's own steal/trigger decisions while a menu, and in
	// the stricter mode a crosshair button prompt, is up. Never touches forwarding: an event that
	// would already pass through to the vanilla handler still does so unchanged.
	//
	// Defaults to kOff. A Quick Turn is a pure camera action with no UI of its own, so a stray one
	// mid-menu is harmless where a swallowed Activate or Sneak press would not be.
	inline MenuContext::BlockMode blockMode = MenuContext::BlockMode::kOff;

	// Hidden diagnostic switch, deliberately not in config.json, so it has no MCM control. Add
	// bDebugLog=1 under [Settings] in Data/MCM/Settings/WIO-QuickTurn.ini by hand and close the
	// pause menu - MCM leaves keys it does not own alone. Turns on MenuContext's change-detected
	// state trace and CameraTrace's per-turn and per-second input traces.
	//
	// Load() hands it to WIO::SetVerbose, which is what actually lifts the log level: a release
	// build is pinned to Info and REX::DEBUG is spdlog::debug, so without that these lines are
	// formatted and dropped.
	inline bool debugLog = false;

	// Lookup table from MCM gamepad dropdown index to F4SE unified keycode.
	// Table order matches config.json options: None, DPad Up/Down/Left/Right, Start, Select,
	// LS, RS, LB, RB, A, B, X, Y, LT, RT. kMacro_GamepadOffset=266, ascending from there.
	inline constexpr std::array<std::int32_t, 17> kGamepadDropdownKeycodes{
		0, 266, 267, 268, 269, 270, 271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281
	};

	[[nodiscard]] inline std::int32_t GamepadDropdownIndexToKeycode(std::int32_t a_index)
	{
		if (a_index <= 0 || static_cast<std::size_t>(a_index) >= kGamepadDropdownKeycodes.size()) {
			return 0;
		}
		return kGamepadDropdownKeycodes[static_cast<std::size_t>(a_index)];
	}

	inline void Load()
	{
		iContextualAction          = detail::GetInt("Settings", "iContextualAction",          0);
		iContextualMoveMod         = detail::GetInt("Settings", "iContextualMoveMod",         0);
		iQuickTurnGamepadKey       = detail::GetInt("Settings", "iQuickTurnGamepadKey",       0);
		iQuickTurnGamepadModifier  = detail::GetInt("Settings", "iQuickTurnGamepadModifier",  0);
		iQuickTurnDurationMs       = detail::GetInt("Settings", "iQuickTurnDurationMs",       100);
		iQuickTurnMovementModifier = detail::GetInt("Settings", "iQuickTurnMovementModifier", 0);
		bApplyInFirstPerson        = detail::GetBool("Settings", "bApplyInFirstPerson",       true);
		debugLog                   = detail::GetBool("Settings", "bDebugLog",                 false);
		WIO::SetVerbose(debugLog);

		// A stale or hand-edited ini can hold a value this enum does not have, and an
		// out-of-range cast would land on no defined mode at all.
		const auto rawBlockMode    = detail::GetInt("Settings", "iBlockHotkeysInMenus",       2);
		blockMode = (rawBlockMode >= 0 && rawBlockMode <= 2) ?
		                static_cast<MenuContext::BlockMode>(rawBlockMode) :
		                MenuContext::BlockMode::kOff;

		const auto rawThirdPerson  = detail::GetInt("Settings", "iApplyInThirdPerson",        1);
		thirdPersonMode = (rawThirdPerson >= 0 && rawThirdPerson <= 3) ?
		                      static_cast<ThirdPersonMode>(rawThirdPerson) :
		                      ThirdPersonMode::kTurnCamera;
	}
}
