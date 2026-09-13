#pragma once

#include <filesystem>
#include <glaze/glaze.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace QT::Keybinds
{
	// Data/MCM/Settings/Keybinds.json is a flat file shared by every mod with an MCM hotkey: a
	// "keybinds" array of {id, keycode, modName, modifiers}, disambiguated by modName. keycode
	// uses F4SE::InputMap's unified 0-281 numbering, the same space ToUnifiedKeycode() produces.
	struct Entry
	{
		std::string id;
		int keycode = -1;
		std::string modName;
		int modifiers = 0;
	};

	namespace detail
	{
		struct File
		{
			std::vector<Entry> keybinds;
			int version = 0;
		};

		inline std::unordered_map<std::string, Entry> g_entries;
	}

	inline void Load()
	{
		constexpr auto path = "Data/MCM/Settings/Keybinds.json";
		detail::g_entries.clear();

		if (!std::filesystem::exists(path)) {
			REX::INFO("Quick Turn: no Keybinds.json found at {} (no hotkey bound yet?)"sv, path);
			return;
		}

		detail::File file{};
		std::string  buffer{};
		if (const auto err = glz::read_file_json(file, path, buffer); err) {
			REX::ERROR("Quick Turn: failed to parse Keybinds.json"sv);
			return;
		}

		for (auto& entry : file.keybinds) {
			if (entry.modName == "WIO-QuickTurn"sv) {
				detail::g_entries[entry.id] = entry;
			}
		}

		REX::INFO("Quick Turn: loaded {} keybind(s) from Keybinds.json"sv, detail::g_entries.size());
	}

	// Returns nullptr if the given hotkey id has never been bound (MCM only writes an entry
	// once the user actually sets a hotkey).
	[[nodiscard]] inline const Entry* Get(std::string_view a_id)
	{
		const auto it = detail::g_entries.find(std::string{ a_id });
		return it != detail::g_entries.end() ? &it->second : nullptr;
	}
}
