#pragma once

#include <cstdlib>
#include <cstring>

#include <Windows.h>

// wingdi.h defines a bare `ERROR` macro that collides with REX::ERROR. Nothing here uses GDI.
#undef ERROR

// Reads MCM's runtime settings file through the Win32 GetPrivateProfileString*A APIs, never
// REX::FIniSettingStore/TIniSetting, whose CSimpleIni backend writes a UTF-8 BOM on Save() that
// corrupts the first section header for MCM's GetPrivateProfileSection reader.
namespace ARC::Settings
{
	namespace detail
	{
		constexpr auto kSettingsPath = "Data/MCM/Settings/WIO-ActivateCombo.ini";

		[[nodiscard]] inline bool GetBool(const char* a_section, const char* a_key, bool a_default)
		{
			return WIO::Ini::GetBool(a_section, a_key, a_default, kSettingsPath);
		}

		[[nodiscard]] inline std::int32_t GetInt(const char* a_section, const char* a_key, std::int32_t a_default)
		{
			return static_cast<std::int32_t>(::GetPrivateProfileIntA(a_section, a_key, a_default, kSettingsPath));
		}
	}

	// Re-read by Load() at kGameLoaded and on every pause-menu close. Secondary Action (Keyboard)
	// is MCM's "hotkey" widget and lives in Keybinds.json; the two iUnbindReadyWeapon* dropdowns
	// are read by BindingCommand.h, not here.
	inline bool          bEnableComboGamepad = true;
	inline bool          bEnableComboKeyboard = false;
	inline std::int32_t  iSecondaryActionGamepad = 0;
	inline bool          bSilenceActivationSound = true;
	inline std::int32_t  iUnholsterHoldTenths = 4;

	// Skips the combo's Ready/Reload half for any Activate press made while VATSMenu is open, where
	// Activate selects targets. Defaults On: the real Activate key never reloads in VATS.
	inline bool          bDisableReloadInVATS = true;

	// Companions only, gated on the weapon being drawn rather than on combat. Independent of
	// iBlockNpcActivate below: either one on its own is enough to block. Defaults Off.
	inline bool          bBlockCompanionDrawn = false;

	// 0 = Off (default), 1 = Companions, 2 = All NPCs. See CombatActivateBlock.h.
	inline std::int32_t  iBlockNpcActivate = 0;

	// How long the combat block keeps applying after combat ends, in whole seconds
	// of real time (0-10, default 1). Combat drops for a moment between waves and
	// targets, so a small grace window stops the block flickering off mid-fight.
	inline std::int32_t  iBlockExtendSeconds = 1;

	// Moves vanilla's hold-Activate-to-exit-power-armor gesture onto the Secondary Action button,
	// so holding Activate in power armor only does this mod's unholster. See PowerArmorExitRemap.h.
	// Defaults Off, changing a vanilla control.
	inline bool          bPowerArmorExitOnSecondary = false;

	inline void Load()
	{
		bEnableComboGamepad = detail::GetBool("Gamepad", "bEnableCombo", true);
		iSecondaryActionGamepad = detail::GetInt("Gamepad", "iSecondaryAction", 0);

		bEnableComboKeyboard = detail::GetBool("Keyboard", "bEnableCombo", false);

		bSilenceActivationSound = detail::GetBool("Advanced", "bSilenceActivationSound", true);
		iUnholsterHoldTenths = detail::GetInt("Advanced", "iUnholsterHoldTenths", 4);

		bDisableReloadInVATS = detail::GetBool("Combat", "bDisableReloadInVATS", true);
		bBlockCompanionDrawn = detail::GetBool("Combat", "bBlockCompanionDrawn", false);
		iBlockNpcActivate = detail::GetInt("Combat", "iBlockNpcActivate", 0);
		iBlockExtendSeconds = detail::GetInt("Combat", "iBlockExtendSeconds", 1);

		bPowerArmorExitOnSecondary = detail::GetBool("PowerArmor", "bExitOnSecondaryAction", false);
	}

	[[nodiscard]] inline float UnholsterHoldSeconds() { return static_cast<float>(iUnholsterHoldTenths) / 10.0f; }
}
