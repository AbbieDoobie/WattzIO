#pragma once

#include <filesystem>
#include <glaze/glaze.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace TSO::Keybinds
{
	// Data\MCM\Settings\Keybinds.json is a single flat file shared by every mod with an MCM
	// hotkey: a flat "keybinds" array of {id, keycode, modName, modifiers}, disambiguated by
	// "modName". The "keycode" values already use the same F4SE::InputMap unified 0-281
	// numbering InputHook.h produces (keycode 259 is mouse4, 260 is mouse5).
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

		// Load() runs again on every pause-menu close so a rebind takes effect without a
		// restart. Only the first pass is worth a release log line; the rest bury it.
		static bool announced = false;
		const bool  first = !announced;
		announced = true;

		detail::g_entries.clear();

		if (!std::filesystem::exists(path)) {
			if (first) {
				REX::INFO("Throwing System Overhaul: no Keybinds.json found at {} (no hotkeys bound yet?)"sv, path);
			} else {
				REX::DEBUG("Throwing System Overhaul: no Keybinds.json found at {}"sv, path);
			}
			return;
		}

		detail::File file{};
		std::string  buffer{};
		if (const auto err = glz::read_file_json(file, path, buffer); err) {
			REX::ERROR("Throwing System Overhaul: failed to parse Keybinds.json"sv);
			return;
		}

		for (auto& entry : file.keybinds) {
			if (entry.modName == "WIO-ThrowSystem"sv) {
				detail::g_entries[entry.id] = entry;
			}
		}

		if (first) {
			REX::INFO("Throwing System Overhaul: loaded {} keybind(s) from Keybinds.json"sv, detail::g_entries.size());
		} else {
			REX::DEBUG("Throwing System Overhaul: re-read {} keybind(s) from Keybinds.json"sv, detail::g_entries.size());
		}
	}

	// Returns nullptr if the hotkey id has never been bound. MCM only writes an entry once the
	// player sets a hotkey.
	[[nodiscard]] inline const Entry* Get(std::string_view a_id)
	{
		const auto it = detail::g_entries.find(std::string{ a_id });
		return it != detail::g_entries.end() ? &it->second : nullptr;
	}
}
