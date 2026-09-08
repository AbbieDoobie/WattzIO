#pragma once

#include <filesystem>
#include <glaze/glaze.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Reads the "Secondary Action (Keyboard)" hotkey MCM's native "hotkey" widget captures.
// Data/MCM/Settings/Keybinds.json is a single flat file shared by every mod with an MCM hotkey,
// keyed by {id, keycode, modName, modifiers}. The keycode value already uses F4SE::InputMap's
// unified 0-281 numbering, the same space the rest of this plugin expects.
namespace ARC::Keybinds
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

		inline std::optional<Entry> g_secondaryActionKeyboard;
	}

	inline void Load()
	{
		constexpr auto path = "Data/MCM/Settings/Keybinds.json";
		detail::g_secondaryActionKeyboard.reset();

		if (!std::filesystem::exists(path)) {
			return;
		}

		detail::File file{};
		std::string  buffer{};
		if (const auto err = glz::read_file_json(file, path, buffer); err) {
			REX::ERROR("Activate/Reload Combo: failed to parse Keybinds.json"sv);
			return;
		}

		for (auto& entry : file.keybinds) {
			if (entry.modName == "WIO-ActivateCombo"sv && entry.id == "SecondaryActionKeyboardHotkey"sv) {
				detail::g_secondaryActionKeyboard = entry;
				break;
			}
		}
	}

	// Returns std::nullopt if the hotkey has never been bound (MCM only writes an entry once
	// the user actually sets it).
	[[nodiscard]] inline std::optional<std::int32_t> SecondaryActionKeyboardKeycode()
	{
		if (!detail::g_secondaryActionKeyboard) {
			return std::nullopt;
		}
		return detail::g_secondaryActionKeyboard->keycode;
	}
}
