#pragma once

#include <array>
#include <cstdio>

#include "MenuContext.h"

// Reads MCM's runtime settings file through the Win32 GetPrivateProfileString*A APIs, not
// REX::FIniSettingStore/TIniSetting, whose CSimpleIni backend writes a UTF-8 BOM on Save() that
// GetPrivateProfileSection cannot handle, corrupting the first section header. Never writes.
namespace GMH::Settings
{
	namespace detail
	{
		constexpr auto kSettingsPath = "Data/MCM/Settings/WIO-GamepadHotkeys.ini";

		[[nodiscard]] inline std::int32_t GetInt(const char* a_section, const char* a_key, std::int32_t a_default)
		{
			return static_cast<std::int32_t>(::GetPrivateProfileIntA(a_section, a_key, a_default, kSettingsPath));
		}

		[[nodiscard]] inline bool GetBool(const char* a_section, const char* a_key, bool a_default)
		{
			return WIO::Ini::GetBool(a_section, a_key, a_default, kSettingsPath);
		}
	}

	struct Slot
	{
		// Raw 0-based MCM dropdown option indices, not engine keycodes or scan codes. See InputHook.h's
		// GamepadDropdownToKeycode and VirtualKey.h's FromDropdownIndex for the conversions.
		std::int32_t gamepadKey = 0;      // 0 = None
		std::int32_t gamepadModifier = 0; // 0 = None (no modifier required)
		std::int32_t virtualKey = 0;      // 0 = None; real default set per-slot, see DefaultVirtualKeyIndex

		// Per-slot menu-blocking override, with its own dropdown numbering: 0 = Use Global Setting,
		// and 1/2/3 are the three MenuContext::BlockMode values shifted up by one. The raw value is
		// not a BlockMode; EffectiveBlockMode() translates.
		std::int32_t blockMode = 0;
	};

	inline constexpr int kSlotCount = 10;
	inline std::array<Slot, kSlotCount> slots{};

	// Advanced page: the global default every slot follows unless it overrides. See MenuContext.h.
	//
	// Defaults to kMenusOnly rather than kOff, against this mod's usual "new settings default off"
	// convention, because a hotkey firing mid-dialogue or mid-loot reads as a bug.
	inline MenuContext::BlockMode blockMode = MenuContext::BlockMode::kMenusOnly;

	// Hidden diagnostic switch, not in config.json, so it has no MCM control. Add bDebugLog=1 under
	// [Advanced] in Data/MCM/Settings/WIO-GamepadHotkeys.ini by hand and close the pause menu; MCM
	// leaves keys it does not own alone. Turns on the state and block-decision traces in InputHook.
	inline bool debugLog = false;

	// Resolves one slot's raw dropdown value against the global. Called per slot at dispatch
	// time rather than cached, because two slots can disagree.
	[[nodiscard]] inline MenuContext::BlockMode EffectiveBlockMode(int a_slotIndex)
	{
		const auto raw = slots[a_slotIndex].blockMode;
		if (raw <= 0 || raw > 3) {
			return blockMode;  // "Use Global Setting", or an out-of-range value
		}
		return static_cast<MenuContext::BlockMode>(raw - 1);
	}

	// Slot 1 -> NumPad 1 through Slot 9 -> NumPad 9, Slot 10 -> NumPad 0, so each slot's default
	// digit matches its number. VirtualKey.h dropdown index: 1 = NumPad 0 through 10 = NumPad 9.
	[[nodiscard]] constexpr std::int32_t DefaultVirtualKeyIndex(int a_slotNumber1Based)
	{
		const int numPadDigit = (a_slotNumber1Based == 10) ? 0 : a_slotNumber1Based;
		return numPadDigit + 1;
	}

	// Re-read at kGameLoaded and on every pause-menu close, so a rebind takes effect immediately.
	inline void Load()
	{
		char key[32];
		for (int i = 0; i < kSlotCount; ++i) {
			const int slotNum = i + 1;

			std::snprintf(key, sizeof(key), "iSlot%dGamepadKey", slotNum);
			slots[i].gamepadKey = detail::GetInt("Slots", key, 0);

			std::snprintf(key, sizeof(key), "iSlot%dGamepadModifier", slotNum);
			slots[i].gamepadModifier = detail::GetInt("Slots", key, 0);

			std::snprintf(key, sizeof(key), "iSlot%dVirtualKey", slotNum);
			slots[i].virtualKey = detail::GetInt("Slots", key, DefaultVirtualKeyIndex(slotNum));

			std::snprintf(key, sizeof(key), "iSlot%dBlockInMenus", slotNum);
			slots[i].blockMode = detail::GetInt("Slots", key, 0);  // 0 = Use Global Setting
		}

		debugLog = detail::GetBool("Advanced", "bDebugLog", false);

		const auto mode = detail::GetInt("Advanced", "iBlockHotkeysInMenus", 0);
		blockMode = (mode >= 0 && mode <= 2) ? static_cast<MenuContext::BlockMode>(mode) : MenuContext::BlockMode::kMenusOnly;
	}
}
