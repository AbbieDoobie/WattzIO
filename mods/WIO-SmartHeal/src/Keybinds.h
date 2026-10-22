#pragma once

#include <filesystem>
#include <glaze/glaze.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Reads the "Smart Heal Hotkey (Keyboard)" MCM native "hotkey" widget capture.
// Data/MCM/Settings/Keybinds.json is a single flat file shared by every mod with an MCM hotkey,
// keyed by {id, keycode, modName, modifiers}. The keycode value already uses F4SE::InputMap's
// unified 0-281 numbering, the same space the rest of this plugin expects.
namespace SH::Keybinds
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

		inline std::optional<Entry> g_smartHealKeyboard;
	}

	inline void Load()
	{
		constexpr auto path = "Data/MCM/Settings/Keybinds.json";
		detail::g_smartHealKeyboard.reset();

		if (!std::filesystem::exists(path)) {
			return;
		}

		detail::File file{};
		std::string  buffer{};
		if (const auto err = glz::read_file_json(file, path, buffer); err) {
			REX::ERROR("Smart Heal Button: failed to parse Keybinds.json"sv);
			return;
		}

		for (auto& entry : file.keybinds) {
			if (entry.modName == "WIO-SmartHeal"sv && entry.id == "SmartHealKeyboardHotkey"sv) {
				detail::g_smartHealKeyboard = entry;
				break;
			}
		}

		// The modifier bitmask encoding is undocumented; see InputHook.h's
		// kModShift/kModCtrl/kModAlt. Logging the raw value lets a wrong assumption be
		// corrected from a real binding.
		if (detail::g_smartHealKeyboard) {
			REX::INFO("Smart Heal Button: keyboard hotkey loaded - keycode={} modifiers={}"sv,
				detail::g_smartHealKeyboard->keycode, detail::g_smartHealKeyboard->modifiers);
		}
	}

	// Returns std::nullopt if the hotkey has never been bound (MCM only writes an entry once the
	// user actually sets it).
	[[nodiscard]] inline std::optional<std::int32_t> KeyboardKeycode()
	{
		if (!detail::g_smartHealKeyboard) {
			return std::nullopt;
		}
		return detail::g_smartHealKeyboard->keycode;
	}

	// 0 = no modifiers required.
	[[nodiscard]] inline std::int32_t KeyboardModifiers()
	{
		if (!detail::g_smartHealKeyboard) {
			return 0;
		}
		return detail::g_smartHealKeyboard->modifiers;
	}
}
