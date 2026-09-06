#pragma once

#include <array>
#include <fstream>
#include <string>

#include "MenuContext.h"

namespace TSO::Settings
{
	namespace detail
	{
		// SimpleIni's SaveFile prepends a UTF-8 BOM by default and FIniSettingStore::Save() calls it
		// with no explicit argument, so every save writes one. MCM's reader is
		// GetPrivateProfileSection, which corrupts the first section header when it meets a BOM, so
		// GetModSettingString/Int/Bool find nothing in that section. Fixed here rather than in the
		// vendored SimpleIni, this being the one shared file both read and write. Self-healing: the
		// next Save() strips it. Also run at Load(), though load order does not guarantee it beats MCM.
		inline void StripUtf8BomIfPresent(std::string_view a_path)
		{
			const std::string path{ a_path };
			std::ifstream     in(path, std::ios::binary);
			if (!in) {
				return;
			}
			std::string content{ std::istreambuf_iterator<char>{ in }, std::istreambuf_iterator<char>{} };
			in.close();

			constexpr std::array<unsigned char, 3> kBom{ 0xEF, 0xBB, 0xBF };
			if (content.size() < kBom.size() ||
				static_cast<unsigned char>(content[0]) != kBom[0] ||
				static_cast<unsigned char>(content[1]) != kBom[1] ||
				static_cast<unsigned char>(content[2]) != kBom[2]) {
				return;
			}

			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			if (out) {
				out.write(content.data() + kBom.size(), static_cast<std::streamsize>(content.size() - kBom.size()));
			}
		}
	}
	// Reads Data\MCM\Settings\WIO-ThrowSystem.ini, keeping the section and key names the
	// original Papyrus version used so existing settings carry over with no migration step.

	// [Settings]
	inline REX::TIniSetting<bool> bThrowCheckWeaponDrawn{ "Settings", "bThrowCheckWeaponDrawn", true };
	inline REX::TIniSetting<bool> bThrowCheckThrowableEquipped{ "Settings", "bThrowCheckThrowableEquipped", true };
	inline REX::TIniSetting<bool> bThrowBlockIfWeaponBlocked{ "Settings", "bThrowBlockIfWeaponBlocked", false };
	inline REX::TIniSetting<bool> bThrowBlockIfWeaponRelaxed{ "Settings", "bThrowBlockIfWeaponRelaxed", false };
	inline REX::TIniSetting<bool> bThrowBlockIfReloading{ "Settings", "bThrowBlockIfReloading", false };
	inline REX::TIniSetting<std::int32_t> iThrowGamepadKey{ "Settings", "iThrowGamepadKey", 0 };
	inline REX::TIniSetting<std::int32_t> iThrowGamepadModifier{ "Settings", "iThrowGamepadModifier", 0 };
	inline REX::TIniSetting<std::int32_t> iMeleeGamepadKey{ "Settings", "iMeleeGamepadKey", 0 };
	inline REX::TIniSetting<std::int32_t> iMeleeGamepadModifier{ "Settings", "iMeleeGamepadModifier", 0 };

	// Throw/Melee Type: 0 = Engine (default), 1 = Animation-only.
	inline REX::TIniSetting<std::int32_t> iThrowInputType{ "Settings", "iThrowInputType", 0 };
	inline REX::TIniSetting<std::int32_t> iMeleeInputType{ "Settings", "iMeleeInputType", 0 };

	// One control per device, because the engine cannot represent keyboard, mouse and gamepad as
	// one decision: keyboard and mouse share a binding slot and gamepad has its own.
	// Applied through Control Unbinder as momentary commands:
	// 0 = No Change, 1 = Unbind, 2 = Rebind, resetting to 0 once applied so nothing re-asserts a
	// state, a standing toggle being unworkable once several mods manage one action.
	inline REX::TIniSetting<std::int32_t> iUnbindMeleeKeyboard{ "Settings", "iUnbindMeleeKeyboard", 0 };
	inline REX::TIniSetting<std::int32_t> iUnbindMeleeGamepad{ "Settings", "iUnbindMeleeGamepad", 0 };

	// Which auto-equip method to use when a throw is attempted with nothing equipped, plus a
	// separate throw-immediately-after toggle. 0=No, 1=Yes (Quick Slots), 2=Yes (Search/Random),
	// 3=Yes (Both). Quick Slots is tried first when both apply, matching the original's
	// Cycle-before-Search priority.
	inline REX::TIniSetting<std::int32_t> iThrowAutoEquipMode{ "Settings", "iThrowAutoEquipMode", 1 };
	inline REX::TIniSetting<bool> bThrowAutoEquipImmediateThrow{ "Settings", "bThrowAutoEquipImmediateThrow", false };

	// [SearchEquip]
	inline REX::TIniSetting<bool> bSearchIncludeMines{ "SearchEquip", "bSearchIncludeMines", false };
	inline REX::TIniSetting<std::int32_t> iSearchEquipGamepadKey{ "SearchEquip", "iSearchEquipGamepadKey", 0 };
	inline REX::TIniSetting<std::int32_t> iSearchEquipGamepadModifier{ "SearchEquip", "iSearchEquipGamepadModifier", 0 };

	// [QuickSwap]
	inline REX::TIniSetting<std::int32_t> iQuickSwapCycleSlots{ "QuickSwap", "iQuickSwapCycleSlots", 2 };
	inline REX::TIniSetting<std::int32_t> iQuickSwapSlotThrowMode{ "QuickSwap", "iQuickSwapSlotThrowMode", 0 };
	inline REX::TIniSetting<std::int32_t> iQuickSwapCycleGamepadKey{ "QuickSwap", "iQuickSwapCycleGamepadKey", 0 };
	inline REX::TIniSetting<std::int32_t> iQuickSwapGamepadModifier{ "QuickSwap", "iQuickSwapGamepadModifier", 0 };
	inline REX::TIniSetting<std::int32_t> iQuickSwap1GamepadKey{ "QuickSwap", "iQuickSwap1GamepadKey", 0 };
	inline REX::TIniSetting<std::int32_t> iQuickSwap2GamepadKey{ "QuickSwap", "iQuickSwap2GamepadKey", 0 };
	inline REX::TIniSetting<std::int32_t> iQuickSwap3GamepadKey{ "QuickSwap", "iQuickSwap3GamepadKey", 0 };
	inline REX::TIniSetting<std::int32_t> iQuickSwap4GamepadKey{ "QuickSwap", "iQuickSwap4GamepadKey", 0 };
	inline REX::TIniSetting<bool> bCycleSearchOnFail{ "QuickSwap", "bCycleSearchOnFail", false };
	// Hold a Quick Slot key for this long to clear its stored reference, so a slot can be reset
	// without overwriting it with something else first. 0=Off, 1=10 seconds, 2=5 seconds
	// (default), 3=2 seconds, matching the MCM dropdown's option order. See
	// QuickSlots::GetClearHoldThresholdSecs.
	inline REX::TIniSetting<std::int32_t> iQuickSwapClearHoldMode{ "QuickSwap", "iQuickSwapClearHoldMode", 2 };
	// Quick Slot references, stored as EditorID strings. See EditorIDPatch.h.
	inline REX::TIniSetting<std::string> sQuickSwap1Ref{ "QuickSwap", "sQuickSwap1Ref", "" };
	inline REX::TIniSetting<std::string> sQuickSwap2Ref{ "QuickSwap", "sQuickSwap2Ref", "" };
	inline REX::TIniSetting<std::string> sQuickSwap3Ref{ "QuickSwap", "sQuickSwap3Ref", "" };
	inline REX::TIniSetting<std::string> sQuickSwap4Ref{ "QuickSwap", "sQuickSwap4Ref", "" };

	// [Notifications]
	inline REX::TIniSetting<bool> bThrowNotifyOnNothingEquipped{ "Notifications", "bThrowNotifyOnNothingEquipped", true };
	inline REX::TIniSetting<bool> bSearchNotifyOnEquip{ "Notifications", "bSearchNotifyOnEquip", true };
	inline REX::TIniSetting<bool> bSearchNotifyOnFail{ "Notifications", "bSearchNotifyOnFail", true };
	inline REX::TIniSetting<bool> bQuickSwapNotifyOnEquip{ "Notifications", "bQuickSwapNotifyOnEquip", true };
	inline REX::TIniSetting<bool> bQuickSwapNotifyOnSave{ "Notifications", "bQuickSwapNotifyOnSave", true };
	inline REX::TIniSetting<bool> bQuickSwapNotifyOnCycleSuccess{ "Notifications", "bQuickSwapNotifyOnCycleSuccess", true };
	inline REX::TIniSetting<bool> bQuickSwapNotifyOnCycleFail{ "Notifications", "bQuickSwapNotifyOnCycleFail", true };
	inline REX::TIniSetting<bool> bCleanNotificationNames{ "Notifications", "bCleanNotificationNames", true };
	// Three modes, because AddObjectToContainer unconditionally fires the engine's own "item
	// added" HUD message when the container is the player. It has no silent argument, so a
	// simple on/off stacked this mod's own text on top of a message that was already showing.
	//
	//   0 = Off (neither)
	//   1 = Minimal (engine's own message only, the default, closest to vanilla)
	//   2 = Full Text (this mod's text only, engine's message suppressed)
	inline REX::TIniSetting<std::int32_t> iKillBonusNotify{ "Notifications", "iKillBonusNotify", 1 };
	// Independent of the message mode above: the engine's pickup audio is a separate suppression
	// channel on the same scope guard, so it stays available in all three modes.
	inline REX::TIniSetting<bool> bKillBonusSound{ "Notifications", "bKillBonusSound", true };

	// [Advanced]
	inline REX::TIniSetting<bool> bAdvancedCheckPlayable{ "Advanced", "bAdvancedCheckPlayable", true };
	// Whether this mod's hotkeys fire while a non-pausing gameplay menu is up. Those menus do not
	// set RE::UI::menuMode, which only true pausing menus increment and which never reaches the
	// input hook anyway, so they need their own check. See MenuContext.h.
	//
	// Values are MenuContext::BlockMode. Read through BlockMode() rather than cast directly, so an
	// out-of-range ini value cannot silently mean "never block".
	inline REX::TIniSetting<std::int32_t> iAdvancedBlockHotkeysInMenus{ "Advanced", "iAdvancedBlockHotkeysInMenus", 0 };

	// Hidden diagnostic switch, deliberately not in config.json, so it has no MCM control. Add
	// bAdvancedDebugLog=1 under [Advanced] in Data/MCM/Settings/WIO-ThrowSystem.ini by hand and
	// close the pause menu. Turns on MenuContext's change-detected state trace.
	inline REX::TIniSetting<bool> bAdvancedDebugLog{ "Advanced", "bAdvancedDebugLog", false };
	inline REX::TIniSetting<std::string> sGrenadeKeywordStrings{ "Advanced", "sGrenadeKeywordStrings", "WeaponTypeGrenade,WeaponTypeCryoGrenade,WeaponTypeMolotov,WeaponTypeNukaGrenade,WeaponTypePlasmaGrenade,WeaponTypePulseGrenade" };
	inline REX::TIniSetting<std::string> sMineKeywordStrings{ "Advanced", "sMineKeywordStrings", "WeaponTypeMine,WeaponTypeCryoMine,WeaponTypeBottlecapMine,WeaponTypeNukaMine,WeaponTypePlasmaMine,WeaponTypePulseMine" };

	// [KillBonus]
	inline REX::TIniSetting<bool> bKillBonusEnabled{ "KillBonus", "bKillBonusEnabled", false };
	inline REX::TIniSetting<std::int32_t> iKillBonusChance{ "KillBonus", "iKillBonusChance", 25 };
	inline REX::TIniSetting<std::int32_t> iKillBonusLegendaryChance{ "KillBonus", "iKillBonusLegendaryChance", 0 };
	inline REX::TIniSetting<std::int32_t> iKillBonusType{ "KillBonus", "iKillBonusType", 0 };
	inline REX::TIniSetting<bool> bKillBonusIncludeMines{ "KillBonus", "bKillBonusIncludeMines", false };
	inline REX::TIniSetting<bool> bKillBonusLimitEnabled{ "KillBonus", "bKillBonusLimitEnabled", true };
	inline REX::TIniSetting<std::int32_t> iKillBonusLimitCount{ "KillBonus", "iKillBonusLimitCount", 4 };
	// Defers the reward until the player walks near the corpse rather than granting it instantly
	// on kill. A separate toggle rather than another iKillBonusType value.
	inline REX::TIniSetting<bool> bKillBonusDeferToProximity{ "KillBonus", "bKillBonusDeferToProximity", true };
	// How long (real seconds) a deferred reward waits for the player to walk near the
	// corpse before it is dropped.
	inline REX::TIniSetting<std::int32_t> iKillBonusProximityTimeoutSecs{ "KillBonus", "iKillBonusProximityTimeoutSecs", 90 };

	// Points the ini store at the real settings file and loads every setting declared above.
	// Both the base and user paths are the same file: FIniSettingStore's two-pass base/user
	// model collapses to a single correct load when both point at the same path, and MCM only
	// ever produces one flat settings.ini rather than a base plus override pair.
	inline void Load()
	{
		constexpr auto path = "Data/MCM/Settings/WIO-ThrowSystem.ini";
		detail::StripUtf8BomIfPresent(path);
		REX::FIniSettingStore::GetSingleton()->Init(path, path);
		REX::FIniSettingStore::GetSingleton()->Load();

		// Load() runs again on every pause-menu close, so only the first is worth a release log
		// line. Announcing every reload buries the one-shot lines a bug report needs.
		static bool announced = false;
		if (!announced) {
			announced = true;
			REX::INFO("Throwing System Overhaul: settings loaded from {}"sv, path);
		} else {
			REX::DEBUG("Throwing System Overhaul: settings re-read from {}"sv, path);
		}
	}

	// Resolves iAdvancedBlockHotkeysInMenus to a MenuContext::BlockMode, clamping rather than
	// casting blind: a stale or hand-edited ini can hold a value this enum does not have, and an
	// out-of-range cast would silently mean "never block".
	[[nodiscard]] inline MenuContext::BlockMode BlockMode()
	{
		const auto raw = iAdvancedBlockHotkeysInMenus.GetValue();
		return (raw >= 0 && raw <= 2) ?
		           static_cast<MenuContext::BlockMode>(raw) :
		           MenuContext::BlockMode::kMenusOnly;
	}

	// Writes every declared setting's current in-memory value back to the ini. This merges rather
	// than overwriting: FIniSettingStore::Save() loads the file first, updates only the keys it
	// manages, then writes back, so untouched content such as comments or sections this mod no
	// longer reads is left alone. Quick Slots needs it, to persist a newly-saved sQuickSwap1-4Ref.
	inline void Save()
	{
		REX::FIniSettingStore::GetSingleton()->Save();
		detail::StripUtf8BomIfPresent("Data/MCM/Settings/WIO-ThrowSystem.ini");
	}
}
