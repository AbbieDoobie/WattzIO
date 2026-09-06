#pragma once

#include <algorithm>
#include <cstdlib>
#include <cstring>

// NOGDI: <wingdi.h> defines a bare `ERROR` macro that collides with REX::ERROR wherever a
// translation unit pulls Windows.h in ahead of a header that logs an error.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOGDI
#include <Windows.h>

// Reads MCM's runtime settings file through the Win32 GetPrivateProfileString*A APIs, never
// REX::FIniSettingStore/TIniSetting, whose CSimpleIni backend writes a UTF-8 BOM that corrupts
// MCM's own GetPrivateProfileSection reader for the first section. Never writes that file.
#include "MenuContext.h"

namespace FMB::Settings
{
	namespace detail
	{
		constexpr auto kSettingsPath = "Data/MCM/Settings/WIO-FlashlightButton.ini";

		[[nodiscard]] inline bool GetBool(const char* a_section, const char* a_key, bool a_default)
		{
			return WIO::Ini::GetBool(a_section, a_key, a_default, kSettingsPath);
		}

		[[nodiscard]] inline std::int32_t GetInt(const char* a_section, const char* a_key, std::int32_t a_default)
		{
			return static_cast<std::int32_t>(::GetPrivateProfileIntA(a_section, a_key, a_default, kSettingsPath));
		}
	}

	// Re-read by Load() at kGameLoaded and on every pause-menu close.
	//   [Gamepad]  iGamepadKey=0  iGamepadModifier=0  iUnbindTogglePOVGamepad=0
	//   [Keyboard] iUnbindTogglePOVKeyboard=0
	//   [Advanced] bEnablePovWorkshop=0  iHoldTenths=4  iBlockHotkeysInMenus=0
	// The keyboard hotkey is MCM's "hotkey" widget, in Keybinds.json instead. The two
	// iUnbindTogglePOV* dropdowns are read by BindingCommand.h, not here.
	inline std::int32_t iGamepadKey = 0;
	inline std::int32_t iGamepadModifier = 0;

	// The shared POV/Workshop switch. Off makes this a pure flashlight button, stopping
	// handing the gesture back to vanilla entirely, and any press toggles the light on release
	// regardless of how long it was held (iHoldTenths goes unused).
	inline bool         bEnablePovWorkshop = false;

	// X, in tenths of a second. Capped at 10: X must stay below vanilla's fEnterWorkshopDelay
	// (default 1.5s), or the handoff would never happen in time for vanilla's workshop window.
	inline std::int32_t iHoldTenths = 4;

	// Menu-vs-prompt blocking - see MenuContext.h.
	//
	// Exception to this project's usual "new toggles default off" convention: a hotkey firing
	// mid-dialogue or mid-loot reads as a bug rather than a feature to opt into, so this defaults
	// to a blocking mode both here and in settings.ini's display default.
	inline MenuContext::BlockMode blockMode = MenuContext::BlockMode::kMenusOnly;

	// Hidden diagnostic switch, deliberately not in config.json, so it has no MCM control. Add
	// bDebugLog=1 under [Advanced] in Data/MCM/Settings/WIO-FlashlightButton.ini by hand and close
	// the pause menu - MCM leaves keys it does not own alone. Turns on MenuContext's
	// change-detected state trace.
	inline bool         debugLog = false;

	inline void Load()
	{
		iGamepadKey = detail::GetInt("Gamepad", "iGamepadKey", 0);
		iGamepadModifier = detail::GetInt("Gamepad", "iGamepadModifier", 0);
		bEnablePovWorkshop = detail::GetBool("Advanced", "bEnablePovWorkshop", false);
		// Clamped rather than trusted: a hand-edited ini could hold a value at or past vanilla's
		// fEnterWorkshopDelay, and the POV half of the mod would then silently never fire.
		iHoldTenths = std::clamp(detail::GetInt("Advanced", "iHoldTenths", 4), 1, 10);
		debugLog = detail::GetBool("Advanced", "bDebugLog", false);

		// Clamped rather than cast blind - a hand-edited ini can hold a value this enum does not
		// have, and an out-of-range cast would silently mean "never block".
		const auto rawBlockMode = detail::GetInt("Advanced", "iBlockHotkeysInMenus", 0);
		blockMode = (rawBlockMode >= 0 && rawBlockMode <= 2) ?
		                static_cast<MenuContext::BlockMode>(rawBlockMode) :
		                MenuContext::BlockMode::kMenusOnly;
	}

	[[nodiscard]] inline float HoldThresholdSeconds() { return static_cast<float>(iHoldTenths) / 10.0f; }
}
