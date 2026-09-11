#pragma once

#include <cstdlib>
#include <cstring>

#include <Windows.h>

#include "MenuContext.h"

// Reads MCM's own runtime settings file directly through the Win32 GetPrivateProfileString*A
// APIs, never REX::FIniSettingStore/TIniSetting - its CSimpleIni backend writes a UTF-8 BOM,
// which corrupts MCM's own GetPrivateProfileSection-based reader for the first section in the
// file. This plugin never writes to Data/MCM/Settings/WIO-WeaponSwap.ini; MCM owns it.
namespace WS::Settings
{
	// Weapon Swap Type (config.json dropdown index order).
	enum class SwapType : std::int32_t
	{
		kTwoSlots = 0,
		kAll = 1
	};

	// If Weapon is Currently Holstered or None Equipped (config.json dropdown index order).
	enum class HolsteredBehavior : std::int32_t
	{
		kEquipNextAndUnholster = 0,
		kEquipNext = 1,
		kDoNothing = 2,
		kUnholsterDontSwap = 3
	};

	namespace detail
	{
		constexpr auto kSettingsPath = "Data/MCM/Settings/WIO-WeaponSwap.ini";

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
	//   [Input]     iGamepadInput=0
	//   [Equipment] iSwapType=0  bHoldSlot3=0  bTripleTapSlot4=0  iHolsteredBehavior=0
	//               bOnlyEquipWeapons=1
	//   [Advanced]  iHoldTenths=4  iTripleTapTenths=7  iBlockHotkeysInMenus=0
	// The keyboard hotkey is MCM's "hotkey" widget, in Keybinds.json instead.
	inline std::int32_t       iGamepadInput = 0;
	inline SwapType           swapType = SwapType::kTwoSlots;
	// Hold (Slot 3) and Triple Tap (Slot 4) are independent of swapType - either can be on in
	// either mode. Neither removes its slot from [All Slots]'s cycle; they're purely additive
	// shortcuts on top of whatever the normal press does.
	inline bool               bHoldSlot3 = false;
	inline bool               bTripleTapSlot4 = false;
	inline HolsteredBehavior  holsteredBehavior = HolsteredBehavior::kEquipNextAndUnholster;
	inline bool               bOnlyEquipWeapons = true;
	inline std::int32_t       iHoldTenths = 4;
	inline std::int32_t       iTripleTapTenths = 7;
	// Menu-vs-prompt hotkey blocking - see MenuContext.h.
	//
	// Exception to this project's usual "new toggles default off" convention: a hotkey firing
	// mid-dialogue or mid-loot reads as a bug rather than a feature to opt into, so this defaults
	// to a blocking mode both here and in settings.ini's display default.
	inline MenuContext::BlockMode blockMode = MenuContext::BlockMode::kMenusOnly;

	// Hidden diagnostic switch, deliberately not in config.json, so it has no MCM control. Add
	// bDebugLog=1 under [Advanced] in Data/MCM/Settings/WIO-WeaponSwap.ini by hand and close the
	// pause menu - MCM leaves keys it does not own alone. Turns on MenuContext's change-detected
	// state trace.
	//
	// Load() hands it to WIO::SetVerbose, which is what actually lifts the log level: a release
	// build is pinned to Info and REX::DEBUG is spdlog::debug, so without that these lines are
	// formatted and dropped.
	inline bool               debugLog = false;

	inline void Load()
	{
		iGamepadInput = detail::GetInt("Input", "iGamepadInput", 0);

		// A stale or hand-edited ini can hold an index this enum does not have, and an
		// out-of-range value would match no case at all in ResolveTarget.
		const auto rawSwapType = detail::GetInt("Equipment", "iSwapType", 0);
		swapType = (rawSwapType == static_cast<std::int32_t>(SwapType::kAll)) ? SwapType::kAll : SwapType::kTwoSlots;

		bHoldSlot3 = detail::GetBool("Equipment", "bHoldSlot3", false);
		bTripleTapSlot4 = detail::GetBool("Equipment", "bTripleTapSlot4", false);
		holsteredBehavior = static_cast<HolsteredBehavior>(detail::GetInt("Equipment", "iHolsteredBehavior", 0));
		bOnlyEquipWeapons = detail::GetBool("Equipment", "bOnlyEquipWeapons", true);

		iHoldTenths = detail::GetInt("Advanced", "iHoldTenths", 4);
		iTripleTapTenths = detail::GetInt("Advanced", "iTripleTapTenths", 7);
		// A stale or hand-edited ini can hold a value this enum does not have, and an
		// out-of-range cast would silently mean "never block".
		const auto rawBlockMode = detail::GetInt("Advanced", "iBlockHotkeysInMenus", 0);
		blockMode = (rawBlockMode >= 0 && rawBlockMode <= 2) ?
		                static_cast<MenuContext::BlockMode>(rawBlockMode) :
		                MenuContext::BlockMode::kMenusOnly;

		debugLog = detail::GetBool("Advanced", "bDebugLog", false);
		WIO::SetVerbose(debugLog);
	}

	[[nodiscard]] inline float HoldThresholdSeconds() { return static_cast<float>(iHoldTenths) / 10.0f; }

	// Measured from the first tap of a chain rather than the gap between consecutive taps, so all
	// three taps must land inside it. See InputHook.h's tap-chain state.
	[[nodiscard]] inline float TripleTapWindowSeconds() { return static_cast<float>(iTripleTapTenths) / 10.0f; }
}
