#pragma once

#include "ButtonBlock.h"
#include "Controls.h"

#include <array>
#include <cstdlib>
#include <string>

namespace GKK::Settings
{
	inline constexpr char kINI[] = "Data/MCM/Settings/WIO-GamepadKbmKeys.ini";

	// One entry per Controls::kAll entry, same order. Button and modifier hold the MCM dropdown
	// index (0 = None, 1-16 = buttons in F4SE::InputMap's own order), not a keycode, so both go
	// through KeyRedirect's DropdownToKeycode before any comparison against an event's idCode.
	inline std::array<int, Controls::kAll.size()> gamepadButton{};
	inline std::array<int, Controls::kAll.size()> gamepadModifier{};
	inline std::array<ButtonBlock::Mode, Controls::kAll.size()> blockMode{};

	// [Advanced] What a control set to Use Global Setting does. Shipped default is Main Button Only.
	inline ButtonBlock::Mode globalBlockMode = ButtonBlock::Mode::kMainOnly;

	// Hidden diagnostic switch, not in config.json, so it has no MCM control. Add
	// bDebugLog=1 under [Advanced] in Data/MCM/Settings/WIO-GamepadKbmKeys.ini by hand and close the
	// pause menu - MCM leaves keys it does not own alone.
	//
	// Load() hands it to WIO::SetVerbose, which is what actually lifts the log level: a release build
	// is pinned to Info and REX::DEBUG is spdlog::debug, so without that every DEBUG line is
	// formatted and dropped.
	inline bool bDebugLog = false;

	// Returns a_default if the key is absent or not a valid integer.
	[[nodiscard]] inline int ReadInt(std::string_view a_section, std::string_view a_key, int a_default)
	{
		const std::string section{ a_section };
		const std::string key{ a_key };
		char              buf[16] = {};
		REX::W32::GetPrivateProfileStringA(section.c_str(), key.c_str(), "", buf, sizeof(buf), kINI);
		if (buf[0] == '\0') {
			return a_default;
		}
		char*      end = nullptr;
		const long value = std::strtol(buf, &end, 10);
		return (end != buf) ? static_cast<int>(value) : a_default;
	}

	// Read at kGameLoaded and on every PauseMenu close, so an MCM change needs no restart.
	inline void Load()
	{
		for (std::size_t i = 0; i < Controls::kAll.size(); ++i) {
			const auto& control = Controls::kAll[i];
			gamepadButton[i] = ReadInt(control.iniSection, control.buttonKey, 0);
			gamepadModifier[i] = ReadInt(control.iniSection, control.modifierKey, 0);
			blockMode[i] = ButtonBlock::FromKeyIndex(ReadInt(control.iniSection, control.blockKey, 0));
		}

		globalBlockMode = ButtonBlock::FromGlobalIndex(ReadInt("Advanced", "iGlobalBlockMode", 1));

		bDebugLog = WIO::Ini::GetBool("Advanced", "bDebugLog", false, kINI);
		WIO::SetVerbose(bDebugLog);

		std::size_t bound = 0;
		for (const auto button : gamepadButton) {
			if (button > 0) {
				++bound;
			}
		}
		REX::DEBUG("Gamepad KBM Keys: settings loaded - {} of {} controls bound, global block mode {}"sv,
			bound, Controls::kAll.size(), static_cast<int>(globalBlockMode));
	}

	// The blocking a control actually gets, with Use Global Setting resolved.
	[[nodiscard]] inline ButtonBlock::Mode EffectiveBlockMode(std::size_t a_index)
	{
		return ButtonBlock::Resolve(blockMode[a_index], globalBlockMode);
	}
}
