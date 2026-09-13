#pragma once

#include <filesystem>
#include <glaze/glaze.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Reads this mod's MCM "hotkey" widget captures out of Data/MCM/Settings/Keybinds.json, a flat
// file shared by every mod with an MCM hotkey and keyed by {id, keycode, modName, modifiers}.
// keycode uses F4SE::InputMap's unified 0-281 numbering, the same space the rest of this plugin
// expects.
namespace WS::Keybinds
{
	struct Entry
	{
		std::string id;
		int         keycode = -1;
		std::string modName;
		int         modifiers = 0;  // Shift/Ctrl/Alt bitmask - see InputHook::ModifiersHeld
	};

	namespace detail
	{
		struct File
		{
			std::vector<Entry> keybinds;
			int                version = 0;
		};

		inline std::optional<Entry> g_weaponSwapKeyboard;
		inline std::optional<Entry> g_nextFavoriteKeyboard;
		inline std::optional<Entry> g_previousFavoriteKeyboard;
	}

	inline void Load()
	{
		constexpr auto path = "Data/MCM/Settings/Keybinds.json";
		detail::g_weaponSwapKeyboard.reset();
		detail::g_nextFavoriteKeyboard.reset();
		detail::g_previousFavoriteKeyboard.reset();

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
			if (entry.modName != "WIO-WeaponSwap"sv) {
				continue;
			}
			if (entry.id == "WeaponSwapKeyboardHotkey"sv) {
				detail::g_weaponSwapKeyboard = entry;
			} else if (entry.id == "NextFavoriteKeyboardHotkey"sv) {
				detail::g_nextFavoriteKeyboard = entry;
			} else if (entry.id == "PreviousFavoriteKeyboardHotkey"sv) {
				detail::g_previousFavoriteKeyboard = entry;
			}
		}
	}

	// std::nullopt when the hotkey has never been bound - MCM only writes an entry once the player
	// sets one.
	[[nodiscard]] inline const std::optional<Entry>& WeaponSwapKeyboard() { return detail::g_weaponSwapKeyboard; }
	[[nodiscard]] inline const std::optional<Entry>& NextFavoriteKeyboard() { return detail::g_nextFavoriteKeyboard; }
	[[nodiscard]] inline const std::optional<Entry>& PreviousFavoriteKeyboard() { return detail::g_previousFavoriteKeyboard; }
}
