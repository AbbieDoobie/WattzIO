#pragma once

#include <glaze/glaze.hpp>

namespace PipboyPipbindFix::Keybinds
{
	inline constexpr char kSharedFile[] = "Data/MCM/Settings/Keybinds.json";
	inline constexpr char kModName[]    = "WIO-PipboyBindings";

	struct Entry
	{
		std::string id;
		int         keycode   = -1;
		std::string modName;
		int         modifiers = 0;
	};

	struct File
	{
		std::vector<Entry> keybinds;
		int                version = 0;
	};

	inline int iZoomKeyboardKeycode      = -1;  // "ZoomKeyboardHotkey"
	inline int iCloseKeyboardKeycode     = -1;  // "CloseKeyboardHotkey"
	inline int iOrderExitKeyboardKeycode = -1;  // "OrderExitKeyboardHotkey"

	// MCM stores F4SE-unified keycodes: 1-255 keyboard (Windows VK), 256-263 mouse, 264-265 wheel.
	// Keyboard ButtonEvent idCodes are VK codes too, so they compare directly. The wheel is
	// rejected: it fires as a momentary event with no release, suiting neither a held zoom nor a
	// close press.
	[[nodiscard]] inline bool MatchesHotkey(const RE::ButtonEvent& a_event, int a_keycode)
	{
		if (a_keycode <= 0) {
			return false;  // unbound
		}

		switch (a_event.device.get()) {
		case RE::INPUT_DEVICE::kKeyboard:
			return a_keycode < F4SE::InputMap::kMacro_MouseButtonOffset &&
				   static_cast<int>(a_event.QIDCode()) == a_keycode;

		case RE::INPUT_DEVICE::kMouse:
			return a_keycode >= F4SE::InputMap::kMacro_MouseButtonOffset &&
				   a_keycode < F4SE::InputMap::kMacro_MouseWheelOffset &&
				   static_cast<int>(a_event.QIDCode()) ==
					   (a_keycode - F4SE::InputMap::kMacro_MouseButtonOffset);

		default:
			return false;
		}
	}

	// Reads this mod's entries out of Data\MCM\Settings\Keybinds.json.
	inline void Load()
	{
		iZoomKeyboardKeycode      = -1;
		iCloseKeyboardKeycode     = -1;
		iOrderExitKeyboardKeycode = -1;

		File file;
		const auto err = glz::read_file_json(file, kSharedFile, std::string{});
		if (err) {
			// The file does not exist on a fresh install with no hotkeys set yet.
			REX::DEBUG("Pip-Boy Bindings Fix: Keybinds.json not read"sv);
			return;
		}

		for (const auto& entry : file.keybinds) {
			if (entry.modName != kModName) {
				continue;
			}
			if (entry.id == "ZoomKeyboardHotkey") {
				iZoomKeyboardKeycode = (entry.keycode > 0) ? entry.keycode : -1;
			} else if (entry.id == "CloseKeyboardHotkey") {
				iCloseKeyboardKeycode = (entry.keycode > 0) ? entry.keycode : -1;
			} else if (entry.id == "OrderExitKeyboardHotkey") {
				iOrderExitKeyboardKeycode = (entry.keycode > 0) ? entry.keycode : -1;
			}
		}

		REX::DEBUG("Pip-Boy Bindings Fix: keybinds loaded - ZoomKB={} CloseKB={} OrderExitKB={}",
			iZoomKeyboardKeycode, iCloseKeyboardKeycode, iOrderExitKeyboardKeycode);
	}
}
