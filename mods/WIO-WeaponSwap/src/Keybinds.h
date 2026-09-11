#pragma once

#include <filesystem>
#include <glaze/glaze.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Reads this mod's MCM "hotkey" widget capture out of Data/MCM/Settings/Keybinds.json, a flat
// file shared by every mod with an MCM hotkey and keyed by {id, keycode, modName, modifiers}.
// keycode uses F4SE::InputMap's unified 0-281 numbering, the same space the rest of this plugin
// expects.
namespace WS::Keybinds
{
	namespace detail
	{
		struct Entry
		{
			std::string id;
			int         keycode = -1;
			std::string modName;
			int         modifiers = 0;
		};

		struct File
		{
			std::vector<Entry> keybinds;
			int                version = 0;
		};

		inline std::optional<Entry> g_weaponSwapKeyboard;
	}

	inline void Load()
	{
		constexpr auto path = "Data/MCM/Settings/Keybinds.json";
		detail::g_weaponSwapKeyboard.reset();

		if (!std::filesystem::exists(path)) {
			return;
		}

		detail::File file{};
		std::string  buffer{};
		if (const auto err = glz::read_file_json(file, path, buffer); err) {
			REX::ERROR("Weapon Swap Button: failed to parse Keybinds.json"sv);
			return;
		}

		for (auto& entry : file.keybinds) {
			if (entry.modName == "WIO-WeaponSwap"sv && entry.id == "WeaponSwapKeyboardHotkey"sv) {
				detail::g_weaponSwapKeyboard = entry;
				break;
			}
		}
	}

	// std::nullopt when the hotkey has never been bound - MCM only writes an entry once the
	// player sets one.
	[[nodiscard]] inline std::optional<std::int32_t> WeaponSwapKeyboardKeycode()
	{
		if (!detail::g_weaponSwapKeyboard) {
			return std::nullopt;
		}
		return detail::g_weaponSwapKeyboard->keycode;
	}
}
