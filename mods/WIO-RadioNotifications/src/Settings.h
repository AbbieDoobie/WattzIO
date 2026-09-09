#pragma once

#include <cstring>

// NOGDI: <wingdi.h> defines a bare ERROR macro, which collides with REX::ERROR.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOGDI
#include <Windows.h>

// Reads MCM's runtime settings file directly. Never writes to it - MCM owns that file. Never uses
// REX::FIniSettingStore, whose CSimpleIni backend writes a UTF-8 BOM that breaks MCM's own reader.
namespace RNF::Settings
{
	namespace detail
	{
		constexpr auto kSettingsPath = "Data/MCM/Settings/WIO-RadioNotifications.ini";

		[[nodiscard]] inline bool GetBool(const char* a_section, const char* a_key, bool a_default)
		{
			return WIO::Ini::GetBool(a_section, a_key, a_default, kSettingsPath);
		}
	}

	// Must match Data\MCM\Config\WIO-RadioNotifications\settings.ini. MCM displays that file's
	// value for a key the player has not set; this plugin reads MCM's runtime file, where the key
	// is equally absent. If the two disagree, a fresh install displays one value and applies the
	// other until the player toggles the switch.
	inline constexpr bool kDefaultHideStationFound = false;
	inline constexpr bool kDefaultHideStationLost = true;
	inline constexpr bool kDefaultHideBeaconFound = false;
	inline constexpr bool kDefaultHideBeaconLost = true;

	inline bool bHideStationFound = kDefaultHideStationFound;
	inline bool bHideStationLost = kDefaultHideStationLost;
	inline bool bHideBeaconFound = kDefaultHideBeaconFound;
	inline bool bHideBeaconLost = kDefaultHideBeaconLost;

	// Hidden diagnostic switch, deliberately not in config.json, so it has no MCM control. Add
	// bDebugLog=1 under [Advanced] in Data/MCM/Settings/WIO-RadioNotifications.ini by hand and
	// close the pause menu - MCM leaves keys it does not own alone. Turns on Diagnostics' one line
	// per intercepted notification, which is how a station that should classify as a beacon but
	// does not gets identified.
	//
	// Load() hands it to WIO::SetVerbose, which is what actually lifts the log level: a release
	// build is pinned to Info and REX::DEBUG is spdlog::debug, so without that these lines are
	// formatted and dropped.
	inline bool bDebugLog = false;

	inline void Load()
	{
		bHideStationFound = detail::GetBool("Stations", "bHideStationFound", kDefaultHideStationFound);
		bHideStationLost = detail::GetBool("Stations", "bHideStationLost", kDefaultHideStationLost);
		bHideBeaconFound = detail::GetBool("Beacons", "bHideBeaconFound", kDefaultHideBeaconFound);
		bHideBeaconLost = detail::GetBool("Beacons", "bHideBeaconLost", kDefaultHideBeaconLost);

		bDebugLog = detail::GetBool("Advanced", "bDebugLog", false);
		WIO::SetVerbose(bDebugLog);
	}
}
