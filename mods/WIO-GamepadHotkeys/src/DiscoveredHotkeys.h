#pragma once

// === F4RD RELOCATIONS ========================================================
// This file resolves no addresses, but its Papyrus dispatch reaches a per-runtime
// ABI offset through the shared compat layer: WIO::Papyrus hands the game a
// BSTThreadScrapFunction whose impl pointer it reads at 0x18 on OG and 0x38 on
// NG/AE. That banner is in lib/commonlibf4rd/compat/WattzIO/Papyrus.h.
// =============================================================================

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <glaze/glaze.hpp>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Read-only convenience page, no part of the gamepad-to-proxy-key mechanism. Scans every mod's
// static Data\MCM\Config\<ModName>\keybinds.json for the hotkeys it declares, cross-references
// the shared Data\MCM\Settings\Keybinds.json for what is bound, and pushes one line per hotkey
// into MCM's live cache with MCM.SetModSettingString. config.json is never regenerated.
//
// The per-mod file is read with `error_on_unknown_keys = false`: real keybinds.json files carry
// an "action" object KeybindDecl does not declare, and the strict default would make every mod
// fail to parse. The shared Keybinds.json below stays strict, its schema being fully known.
namespace GMH::DiscoveredHotkeys
{
	struct KeybindDecl
	{
		std::string id;
		std::string desc;
	};

	struct ModConfigFile
	{
		std::string modName;
		std::vector<KeybindDecl> keybinds;
	};

	// displayName is what MCM shows in the mod list, so it is the name a player recognises;
	// modName is a file-system token.
	struct ModDisplayFile
	{
		std::string displayName;
	};

	struct BoundEntry
	{
		std::string id;
		int keycode = -1;
		std::string modName;
		int modifiers = 0;
	};

	struct SharedSettingsFile
	{
		std::vector<BoundEntry> keybinds;
		int version = 0;
	};

	// Fixed cap: config.json's page has exactly this many read-only rows and settings.ini must
	// declare exactly this many sDiscoveredN keys, the widget count being static while the scan is
	// not. A real load order can declare 78 hotkeys. Overflow gets no row rather than an error, and
	// Scan() orders bound hotkeys first, so what falls off claims no key and cannot collide.
	inline constexpr int kMaxRows = 50;

	// Per-row character budget, applied by SanitizeForIni. The constraint is Win32's INI section
	// limit, not the display: GetPrivateProfileSection's ANSI form cannot return more than 32,767
	// characters for a whole section however large the buffer, and MCM's read loop grows its buffer
	// until the call stops filling it, so an oversized section comes back short and the tail rows
	// vanish. 50 rows x ~215 chars is about 10,800, leaving room for multi-byte UTF-8.
	inline constexpr std::size_t kMaxRowChars = 200;

	// Keybinds.json's "keycode" uses F4SE's unified numbering: 0-255 keyboard VK codes, 256-263
	// mouse buttons, 264-265 wheel, 266-281 gamepad. Covered exhaustively because ordinary
	// punctuation keys (VK_OEM_2, '/', is 191) are what a mod author picks as a default. OEM_*
	// legends are layout-dependent and the VK code carries no layout, so the names assume US, as
	// MCM's own UI does; anything unknown falls back to a raw numeric label.
	[[nodiscard]] inline std::string KeyName(int a_keycode)
	{
		switch (a_keycode) {
		case 0x08: return "Backspace";
		case 0x09: return "Tab";
		case 0x0C: return "Clear";
		case 0x0D: return "Enter";
		case 0x13: return "Pause";
		case 0x14: return "Caps Lock";
		case 0x1B: return "Escape";
		case 0x20: return "Space";
		case 0x21: return "Page Up";
		case 0x22: return "Page Down";
		case 0x23: return "End";
		case 0x24: return "Home";
		case 0x25: return "Left Arrow";
		case 0x26: return "Up Arrow";
		case 0x27: return "Right Arrow";
		case 0x28: return "Down Arrow";
		case 0x2C: return "Print Screen";
		case 0x2D: return "Insert";
		case 0x2E: return "Delete";
		case 0x5B: return "Left Windows";
		case 0x5C: return "Right Windows";
		case 0x5D: return "Menu (Apps)";
		case 0x60: return "NumPad 0";
		case 0x61: return "NumPad 1";
		case 0x62: return "NumPad 2";
		case 0x63: return "NumPad 3";
		case 0x64: return "NumPad 4";
		case 0x65: return "NumPad 5";
		case 0x66: return "NumPad 6";
		case 0x67: return "NumPad 7";
		case 0x68: return "NumPad 8";
		case 0x69: return "NumPad 9";
		case 0x6A: return "NumPad *";
		case 0x6B: return "NumPad +";
		case 0x6C: return "NumPad Separator";
		case 0x6D: return "NumPad -";
		case 0x6E: return "NumPad .";
		case 0x6F: return "NumPad /";
		case 0x90: return "Num Lock";
		case 0x91: return "Scroll Lock";
		// Both the generic and the side-specific modifier VKs: a hotkey widget can capture
		// either, depending on how the capturing code read the keyboard state.
		case 0x10: return "Shift";
		case 0x11: return "Ctrl";
		case 0x12: return "Alt";
		case 0xA0: return "Left Shift";
		case 0xA1: return "Right Shift";
		case 0xA2: return "Left Ctrl";
		case 0xA3: return "Right Ctrl";
		case 0xA4: return "Left Alt";
		case 0xA5: return "Right Alt";
		// US-layout punctuation - see the caveat above.
		case 0xBA: return "; :";
		case 0xBB: return "= +";
		case 0xBC: return ", <";
		case 0xBD: return "- _";
		case 0xBE: return ". >";
		case 0xBF: return "/ ?";
		case 0xC0: return "` ~";
		case 0xDB: return "[ {";
		case 0xDC: return "Backslash |";
		case 0xDD: return "] }";
		case 0xDE: return "' \"";
		case 0xE2: return "Backslash | (102nd key)";
		default: break;
		}
		if (a_keycode >= 0x41 && a_keycode <= 0x5A) {
			return std::string(1, static_cast<char>(a_keycode));  // A-Z
		}
		if (a_keycode >= 0x30 && a_keycode <= 0x39) {
			return std::string(1, static_cast<char>(a_keycode));  // top-row 0-9
		}
		if (a_keycode >= 0x70 && a_keycode <= 0x87) {
			return "F" + std::to_string(a_keycode - 0x70 + 1);  // F1-F24
		}
		if (a_keycode >= 256 && a_keycode <= 263) {
			static constexpr std::array<const char*, 8> kMouseNames{
				"Mouse 1 (Left)", "Mouse 2 (Right)", "Mouse 3 (Middle)",
				"Mouse 4", "Mouse 5", "Mouse 6", "Mouse 7", "Mouse 8"
			};
			return kMouseNames[a_keycode - 256];
		}
		if (a_keycode == 264) {
			return "Mouse Wheel Up";
		}
		if (a_keycode == 265) {
			return "Mouse Wheel Down";
		}
		if (a_keycode >= 266 && a_keycode <= 281) {
			// kMacro_GamepadOffset + button index, in F4SE's enum order, which is also the order
			// this mod's gamepad dropdowns use.
			static constexpr std::array<const char*, 16> kGamepadNames{
				"Gamepad - DPad Up", "Gamepad - DPad Down", "Gamepad - DPad Left",
				"Gamepad - DPad Right", "Gamepad - Start", "Gamepad - Back",
				"Gamepad - Left Thumb (LS)", "Gamepad - Right Thumb (RS)",
				"Gamepad - Left Bumper (LB/L1)", "Gamepad - Right Bumper (RB/R1)",
				"Gamepad - A", "Gamepad - B", "Gamepad - X", "Gamepad - Y",
				"Gamepad - Left Trigger (LT/L2)", "Gamepad - Right Trigger (RT/R2)"
			};
			return kGamepadNames[a_keycode - 266];
		}
		return "Key " + std::to_string(a_keycode);
	}

	// MCM stores held modifiers as a separate bitfield alongside the keycode (Shift = 1<<0,
	// Control = 1<<1, Alt = 1<<2), so "Ctrl + R" is keycode 82 with modifiers 2. Rendering the
	// keycode alone would show two different bindings as the same key.
	[[nodiscard]] inline std::string KeyNameWithModifiers(int a_keycode, int a_modifiers)
	{
		std::string out;
		if (a_modifiers & (1 << 0)) {
			out += "Shift + ";
		}
		if (a_modifiers & (1 << 1)) {
			out += "Ctrl + ";
		}
		if (a_modifiers & (1 << 2)) {
			out += "Alt + ";
		}
		out += KeyName(a_keycode);
		return out;
	}

	// A mod's keybinds.json "desc" is often a translation key rather than display text, and the
	// shared Scaleform translator cannot resolve it here: row values are pushed as ini-backed
	// settings and MCM only translates config.json labels. So it is read from the file the engine
	// would read, Interface\Translations\<Mod>_<lang>.txt, UTF-16LE with a BOM and one
	// "$KEY<TAB>Value" per line, through RE::BSResourceNiBinaryStream so a BA2-packed translation
	// resolves like a loose one.
	namespace detail
	{
		[[nodiscard]] inline std::string WideToUtf8(const std::wstring_view a_wide)
		{
			if (a_wide.empty()) {
				return {};
			}
			const int needed = ::WideCharToMultiByte(CP_UTF8, 0, a_wide.data(), static_cast<int>(a_wide.size()),
				nullptr, 0, nullptr, nullptr);
			if (needed <= 0) {
				return {};
			}
			std::string out(static_cast<std::size_t>(needed), '\0');
			::WideCharToMultiByte(CP_UTF8, 0, a_wide.data(), static_cast<int>(a_wide.size()),
				out.data(), needed, nullptr, nullptr);
			return out;
		}

		// Limits on what this reader accepts from a third-party file, so a corrupt or truncated input
		// costs a skipped mod rather than a hang or an allocation the size of the file.
		inline constexpr std::size_t kMaxTranslationFileBytes = 0x400000;  // 4 MB
		inline constexpr std::size_t kMaxTranslationEntries = 20000;
		inline constexpr std::size_t kMaxTranslationValueChars = 512;      // matches MCM's own per-line parse buffer

		// Loads one mod's translation file into a "$KEY" -> text map, the player's language first then
		// English. Every failure mode is non-fatal and ends the same way: that mod contributes no
		// translations and its rows show raw $KEY text.
		[[nodiscard]] inline std::unordered_map<std::string, std::string> LoadModTranslations(const std::string& a_modName)
		{
			std::unordered_map<std::string, std::string> table;

			if (a_modName.empty()) {
				return table;
			}

			try {
				const auto setting = RE::GetINISetting("sLanguage:General");
				std::string language{ setting ? setting->GetString() : ""sv };
				if (language.empty()) {
					language = "en";
				}

				std::vector<std::string> candidates;
				candidates.push_back(std::format(R"(Interface\Translations\{}_{}.txt)", a_modName, language));
				if (::_stricmp(language.c_str(), "en") != 0) {
					candidates.push_back(std::format(R"(Interface\Translations\{}_en.txt)", a_modName));
				}

				for (const auto& path : candidates) {
					// The common case, not an error: many mods write plain-text descriptions and ship none.
					RE::BSResourceNiBinaryStream stream(path.c_str());
					if (!stream) {
						continue;
					}

					RE::NiBinaryStream::BufferInfo info{};
					stream.GetBufferInfo(info);
					if (info.fileSize < sizeof(wchar_t) * 2 || info.fileSize > kMaxTranslationFileBytes) {
						REX::WARN("Gamepad MCM Hotkeys: translation file {} has an implausible size ({} bytes) - skipped"sv,
							path, info.fileSize);
						continue;
					}

					// An odd trailing byte from a mid-character truncation is floored away by the division.
					std::wstring buffer(info.fileSize / sizeof(wchar_t), L'\0');
					const auto readBytes = stream.binary_read(buffer.data(), buffer.size() * sizeof(wchar_t));
					buffer.resize(readBytes / sizeof(wchar_t));
					if (buffer.size() < 2 || buffer.front() != 0xFEFF) {
						// Not UCS-2 LE with a BOM. MCM's own ParseTranslation rejects these too.
						REX::WARN("Gamepad MCM Hotkeys: translation file {} is not UCS-2 LE with a BOM - skipped"sv, path);
						continue;
					}

					std::wstring_view rest{ buffer };
					rest.remove_prefix(1);  // BOM

					while (!rest.empty() && table.size() < kMaxTranslationEntries) {
						const auto eol = rest.find(L'\n');
						std::wstring_view line = rest.substr(0, eol);
						rest = (eol == std::wstring_view::npos) ? std::wstring_view{} : rest.substr(eol + 1);

						if (!line.empty() && line.back() == L'\r') {
							line.remove_suffix(1);
						}

						// A well-formed entry is "$KEY<TAB>text"; anything else is skipped silently, as the
						// engine's own parser does.
						const auto tab = line.find(L'\t');
						if (tab == std::wstring_view::npos || tab == 0 || line.front() != L'$') {
							continue;
						}

						auto value = line.substr(tab + 1);
						while (!value.empty() && (value.front() == L' ' || value.front() == L'\t')) {
							value.remove_prefix(1);
						}
						if (value.empty()) {
							continue;  // declared but empty - leave the row showing its raw $KEY
						}
						if (value.size() > kMaxTranslationValueChars) {
							value = value.substr(0, kMaxTranslationValueChars);
						}

						auto key = WideToUtf8(line.substr(0, tab));
						auto text = WideToUtf8(value);
						if (key.empty() || text.empty()) {
							continue;  // conversion failed (unpaired surrogates, etc.)
						}
						table.insert_or_assign(std::move(key), std::move(text));
					}

					if (table.size() >= kMaxTranslationEntries) {
						REX::WARN("Gamepad MCM Hotkeys: translation file {} hit the {}-entry cap - reading no further"sv,
							path, kMaxTranslationEntries);
					}
					if (!table.empty()) {
						break;  // player's own language won; don't overlay English on top of it
					}
				}
			} catch (const std::exception& e) {
				REX::WARN("Gamepad MCM Hotkeys: failed to read translations for {} - descriptions will show raw keys ({})"sv,
					a_modName, e.what());
				table.clear();
			} catch (...) {
				REX::WARN("Gamepad MCM Hotkeys: failed to read translations for {} - descriptions will show raw keys"sv, a_modName);
				table.clear();
			}

			return table;
		}
	}

	// Resolves a "$KEY" description against that mod's translation file, caching each table for the
	// scan. Anything unresolvable is returned unchanged, so a visible "$SomeKey" row means the
	// mod's own file did not cover it.
	[[nodiscard]] inline std::string ResolveDescription(
		const std::string& a_modName,
		const std::string& a_desc,
		std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& a_cache)
	{
		if (a_desc.empty() || a_desc.front() != '$') {
			return a_desc;
		}

		auto itr = a_cache.find(a_modName);
		if (itr == a_cache.end()) {
			itr = a_cache.emplace(a_modName, detail::LoadModTranslations(a_modName)).first;
		}

		const auto& table = itr->second;
		if (const auto match = table.find(a_desc); match != table.end() && !match->second.empty()) {
			return match->second;
		}
		return a_desc;
	}

	// Prefers the sibling mod's config.json displayName, resolved through its own translation file
	// the way descriptions are, and falls back to the raw modName when anything is missing.
	[[nodiscard]] inline std::string ResolveModName(
		const std::filesystem::path& a_modDir,
		const std::string& a_folderName,
		const std::string& a_modName,
		std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& a_cache)
	{
		const auto configPath = a_modDir / "config.json";
		if (!std::filesystem::exists(configPath)) {
			return a_modName;
		}

		ModDisplayFile cfg{};
		std::string buffer;
		constexpr auto kLenientOpts = glz::opts{ .error_on_unknown_keys = false };
		if (const auto err = glz::read_file_json<kLenientOpts>(cfg, configPath.string(), buffer); err) {
			return a_modName;
		}
		if (cfg.displayName.empty()) {
			return a_modName;
		}

		// ResolveDescription returns its input unchanged when unresolvable, so an unresolved
		// displayName lands back here as a literal "$SOME_KEY".
		auto resolved = ResolveDescription(a_folderName, cfg.displayName, a_cache);
		if (resolved.empty() || resolved.front() == '$') {
			return a_modName;
		}
		return resolved;
	}

	// The text comes from arbitrary third-party mod names, so characters the ini format treats as
	// structure are neutralised here: '=' splits key from value, ';' starts a comment, '[' and ']'
	// open a section header, Win32 strips a matched leading/trailing '"' pair, and control
	// characters have no representation. Substituted with similar ASCII so the text still reads.
	[[nodiscard]] inline std::string SanitizeForIni(std::string a_line)
	{
		std::string out;
		out.reserve(a_line.size());

		for (const char c : a_line) {
			switch (c) {
			case '=': out += '-'; break;
			case ';': out += ','; break;
			case '[': out += '('; break;
			case ']': out += ')'; break;
			case '"': out += '\''; break;
			default:
				// Control characters, CR, LF and tab included, collapse to a space. Anything at or above
				// 0x20 passes through, so non-ASCII UTF-8 in a translated mod name survives.
				if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
					out += ' ';
				} else {
					out += c;
				}
				break;
			}
		}

		constexpr auto isSpace = [](char c) { return c == ' '; };
		const auto first = std::find_if_not(out.begin(), out.end(), isSpace);
		const auto last = std::find_if_not(out.rbegin(), out.rend(), isSpace).base();
		std::string trimmed = first < last ? std::string(first, last) : std::string{};

		// The cut backs off any UTF-8 continuation bytes (0b10xxxxxx), so a multi-byte character is
		// dropped whole rather than left half-written into the ini.
		if (trimmed.size() > kMaxRowChars) {
			std::size_t cut = kMaxRowChars;
			while (cut > 0 && (static_cast<unsigned char>(trimmed[cut]) & 0xC0) == 0x80) {
				--cut;
			}
			trimmed.resize(cut);
			while (!trimmed.empty() && trimmed.back() == ' ') {
				trimmed.pop_back();
			}
			trimmed += "...";
		}

		return trimmed;
	}

	[[nodiscard]] inline std::vector<std::string> Scan()
	{
		namespace fs = std::filesystem;
		std::vector<std::string> lines;

		const fs::path configRoot = "Data/MCM/Config";
		if (!fs::exists(configRoot)) {
			return lines;
		}

		SharedSettingsFile bound{};
		{
			constexpr auto sharedPath = "Data/MCM/Settings/Keybinds.json";
			if (fs::exists(sharedPath)) {
				std::string buffer;
				if (glz::read_file_json(bound, sharedPath, buffer)) {
					REX::WARN("Gamepad MCM Hotkeys: failed to parse shared Keybinds.json for Discovered Hotkeys"sv);
				}
			}
		}

		// One table per mod, loaded on first use and reused for that mod's remaining hotkeys.
		std::unordered_map<std::string, std::unordered_map<std::string, std::string>> translationCache;

		// Collected separately from unbound ones so they can be ordered ahead of them; see kMaxRows.
		std::vector<std::string> unbound;

		std::error_code ec;
		for (const auto& modDir : fs::directory_iterator(configRoot, ec)) {
			if (!modDir.is_directory()) {
				continue;
			}
			const auto keybindsPath = modDir.path() / "keybinds.json";
			if (!fs::exists(keybindsPath)) {
				continue;
			}

			ModConfigFile cfg{};
			std::string buffer;
			constexpr auto kLenientOpts = glz::opts{ .error_on_unknown_keys = false };
			if (const auto err = glz::read_file_json<kLenientOpts>(cfg, keybindsPath.string(), buffer); err) {
				REX::WARN("Gamepad MCM Hotkeys: failed to parse {} for Discovered Hotkeys - skipped ({})"sv,
					keybindsPath.string(), glz::format_error(err, buffer));
				continue;
			}

			for (const auto& decl : cfg.keybinds) {
				const BoundEntry* match = nullptr;
				for (const auto& b : bound.keybinds) {
					if (b.modName == cfg.modName && b.id == decl.id) {
						match = &b;
						break;
					}
				}
				// The engine's translation file is named after the MCM config folder, not cfg.modName
				// (Interface\Translations\<folder>_en.txt). They are usually identical, but only the
				// folder name is guaranteed by MCM's layout convention.
				const auto folderName = modDir.path().filename().string();
				const auto desc = ResolveDescription(folderName, decl.desc, translationCache);

				std::string line = ResolveModName(modDir.path(), folderName, cfg.modName, translationCache) +
				                   " - " + desc + ": ";
				// Key and modifier names are what is printed on the keys, so only "(unbound)" is prose. Rows
				// reach MCM through an ini read back with ANSI GetPrivateProfileSection, so a non-codepage
				// translation shows as mojibake.
				line += match ? KeyNameWithModifiers(match->keycode, match->modifiers) :
								WIO::Translations::Localize("$GMH_Row_Unbound"sv, "(unbound)"sv);
				(match ? lines : unbound).push_back(SanitizeForIni(std::move(line)));
			}
		}

		// Alphabetical within each group, bound hotkeys first, so a load order with more hotkeys than
		// rows loses the unbound ones, which claim no key and cannot collide with anything sent.
		std::sort(lines.begin(), lines.end());
		std::sort(unbound.begin(), unbound.end());
		lines.insert(lines.end(), std::make_move_iterator(unbound.begin()), std::make_move_iterator(unbound.end()));
		return lines;
	}

	// Every key pushed here also has to exist in this mod's shipped
	// Data/MCM/Config/WIO-GamepadHotkeys/settings.ini under a real [Discovered] section, or this
	// whole function is a silent no-op. MCM's setting store is populated only from INI files, so an
	// id in config.json but not settings.ini is never registered.
	//
	// Rows beyond the discovered count are pushed as an empty string, not left stale.
	inline void Push()
	{
		const auto lines = Scan();

		const auto game = RE::GameVM::GetSingleton();
		const auto vm = game ? game->GetVM() : nullptr;
		if (!vm) {
			REX::WARN("Gamepad MCM Hotkeys: Papyrus VM unavailable - Discovered Hotkeys not pushed to MCM"sv);
			return;
		}

		const RE::BSFixedString modName{ "WIO-GamepadHotkeys" };
		for (int i = 0; i < kMaxRows; ++i) {
			const RE::BSFixedString settingKey{ "sDiscovered" + std::to_string(i + 1) + ":Discovered" };
			const RE::BSFixedString value{ i < static_cast<int>(lines.size()) ? lines[i] : std::string{} };
			WIO::Papyrus::DispatchStaticCall(vm, "MCM", "SetModSettingString", nullptr, modName, settingKey, value);
		}

		REX::INFO("Gamepad MCM Hotkeys: Discovered Hotkeys - found {} hotkey(s) across installed mods, pushed to MCM"sv, lines.size());
	}
}
