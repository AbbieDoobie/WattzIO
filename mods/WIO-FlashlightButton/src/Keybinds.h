#pragma once

#include <filesystem>
#include <glaze/glaze.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Reads this mod's MCM "hotkey" widget capture out of Data/MCM/Settings/Keybinds.json, a flat
// file shared by every mod with an MCM hotkey and keyed by {id, keycode, modName, modifiers}.
// keycode uses F4SE::InputMap's unified 0-281 numbering, the same space the rest of this
// plugin expects. The `modifiers` field is honoured.
namespace FMB::Keybinds
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

		inline std::optional<Entry> g_multiButtonKeyboard;
	}

	inline void Load()
	{
		constexpr auto path = "Data/MCM/Settings/Keybinds.json";
		detail::g_multiButtonKeyboard.reset();

		if (!std::filesystem::exists(path)) {
			return;
		}

		detail::File file{};
		std::string  buffer{};
		if (const auto err = glz::read_file_json(file, path, buffer); err) {
			REX::ERROR("Flashlight Button: failed to parse Keybinds.json"sv);
			return;
		}

		for (auto& entry : file.keybinds) {
			if (entry.modName == "WIO-FlashlightButton"sv && entry.id == "MultiButtonKeyboardHotkey"sv) {
				detail::g_multiButtonKeyboard = entry;
				break;
			}
		}

		// MCM does not document the modifier bitmask, so the raw value is logged; InputHook.h
		// holds the bit assignments it is compared against.
		if (detail::g_multiButtonKeyboard) {
			REX::INFO("Flashlight Button: keyboard hotkey loaded - keycode={} modifiers={}"sv,
				detail::g_multiButtonKeyboard->keycode, detail::g_multiButtonKeyboard->modifiers);
		}
	}

	// std::nullopt when the hotkey has never been bound - MCM only writes an entry once the
	// player sets one.
	[[nodiscard]] inline std::optional<std::int32_t> KeyboardKeycode()
	{
		if (!detail::g_multiButtonKeyboard) {
			return std::nullopt;
		}
		return detail::g_multiButtonKeyboard->keycode;
	}

	// 0 = no modifiers required.
	[[nodiscard]] inline std::int32_t KeyboardModifiers()
	{
		if (!detail::g_multiButtonKeyboard) {
			return 0;
		}
		return detail::g_multiButtonKeyboard->modifiers;
	}
}
