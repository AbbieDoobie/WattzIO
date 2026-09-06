#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>


namespace TSO::SettingsMigration
{
	namespace detail
	{
		// The path this mod read before the WattzIO rename, which is also what the original v1.1
		// Papyrus mod used, so upgraders kept their settings with no migration step. Renaming the file
		// broke that; this module restores it.
		constexpr auto kLegacyPath = "Data/MCM/Settings/ThrowingSystemOverhaul.ini";
		constexpr auto kCurrentPath = "Data/MCM/Settings/WIO-ThrowSystem.ini";

		// Written into the legacy file once its contents have been carried over.
		constexpr auto kStampKey = "bMigratedToWattzIO";
		constexpr auto kStamp = "\r\n[Migration]\r\n" \
		                        "; Settings were carried over to WIO-ThrowSystem.ini by\r\n" \
		                        "; Throwing System Overhaul v2. This file is no longer read.\r\n" \
		                        "; Delete this line to allow the carry-over to run again.\r\n" \
		                        "bMigratedToWattzIO=1\r\n";

		// Set when Run() actually carries a file over, so LoadGameSink can tell the player once they
		// are in the world.
		inline bool s_migratedThisSession = false;
		// Guards the notice to once per session (see LoadGameSink::ProcessEvent).
		inline bool s_warningShown = false;
	}

	// One-time carry-over of a pre-rename settings file. The whole file is copied verbatim rather
	// than key by key: keys the native version no longer reads are never looked up, keys the old
	// file never had fall back to their declared defaults, and Settings::Save() merges rather than
	// overwrites, so carried-over dead sections survive.
	//
	// Runs at most once, on two conditions: the destination does not exist yet, and the source
	// exists and is not already stamped. The stamp goes on the source, which is the half that
	// guarantees it, destination absence alone being enough to resurrect old values for a player who
	// deleted the new file or reset to defaults. Deleting the stamped line re-arms it by hand.
	//
	// Nothing is deleted. Runs from F4SEPlugin_Load, before all Papyrus, so it beats MCM to creating
	// the destination. It cannot cover a first launch where this plugin fails to load: MCM writes a
	// defaults file, the first condition fails forever, and the carry-over is skipped.
	inline void Run()
	{
		std::error_code ec;

		if (std::filesystem::exists(detail::kCurrentPath, ec)) {
			return;  // already migrated, or the player has settings under the new name
		}

		if (!std::filesystem::exists(detail::kLegacyPath, ec)) {
			return;  // clean install - nothing to carry over
		}

		std::ifstream in(detail::kLegacyPath, std::ios::binary);
		if (!in) {
			REX::WARN("Throwing System Overhaul: found {} but could not open it - settings not carried over"sv,
				detail::kLegacyPath);
			return;
		}
		const std::string content{ std::istreambuf_iterator<char>{ in }, std::istreambuf_iterator<char>{} };
		in.close();

		if (content.find(detail::kStampKey) != std::string::npos) {
			return;  // already carried over in an earlier session
		}

		std::filesystem::create_directories(
			std::filesystem::path{ detail::kCurrentPath }.parent_path(), ec);

		std::ofstream out(detail::kCurrentPath, std::ios::binary | std::ios::trunc);
		if (!out) {
			REX::WARN("Throwing System Overhaul: could not write {} - settings not carried over"sv, detail::kCurrentPath);
			return;
		}
		out.write(content.data(), static_cast<std::streamsize>(content.size()));
		out.close();

		// Stamped only after the destination is on disk, so a failure halfway leaves the carry-over
		// armed rather than spent.
		std::ofstream stamp(detail::kLegacyPath, std::ios::binary | std::ios::app);
		if (!stamp) {
			// The copy succeeded either way. Warned because without the stamp, condition 2 stays satisfied
			// and the carry-over could run again if the destination is removed.
			REX::WARN("Throwing System Overhaul: carried settings over to {} but could not stamp {} - "
			          "delete the old file by hand to be sure it is never re-read"sv,
				detail::kCurrentPath, detail::kLegacyPath);
			return;
		}
		stamp << detail::kStamp;

		detail::s_migratedThisSession = true;

		REX::INFO("Throwing System Overhaul: carried settings over from {} to {} (one time only; the old file is "
		          "kept, stamped, and no longer read)"sv,
			detail::kLegacyPath, detail::kCurrentPath);
	}

	// Tells the player to restart once, on the session where the carry-over ran.
	//
	// MCM is itself an F4SE plugin and reads every Data/MCM/Settings/*.ini into an in-memory store
	// at its own plugin-load time, which happens before this one's. On the carry-over session that
	// read precedes Run() creating the destination file, so MCM has no entry for this mod and shows
	// template defaults however correct the file on disk is. The next launch is fine; only MCM's
	// display is stale.
	//
	// It cannot be fixed from here: MCM exposes no re-read-from-disk native, and nothing this plugin
	// can hook runs before another F4SE plugin's load.
	//
	// It warns rather than staying quiet because a setting changed in MCM during that session
	// writes MCM's store - defaults for this mod - back over the carried-over file, and the legacy
	// file is stamped by then. English is hardcoded, and it is shown on save load rather than at
	// kGameLoaded, which fires while the main menu is up and has no HUD.
	class LoadGameSink :
		public RE::BSTEventSink<RE::TESLoadGameEvent>
	{
	public:
		// Only registers on the session the carry-over ran, so an ordinary launch installs no
		// sink at all.
		static void InstallIfMigrated()
		{
			if (!detail::s_migratedThisSession) {
				return;
			}

			const auto source = RE::TESLoadGameEvent::GetEventSource();
			if (!source) {
				REX::WARN("Throwing System Overhaul: load-game event source unavailable - the carry-over notice cannot be shown"sv);
				return;
			}
			static LoadGameSink singleton;
			source->RegisterSink(&singleton);

			REX::WARN("Throwing System Overhaul: settings were carried over this session, so MCM's own in-memory store "
			          "(populated at mcm.dll's plugin load, before ours ran) still holds defaults "
			          "for this mod. It will be correct on the next launch. Changing a setting in "
			          "MCM before then writes those defaults back over the carried-over file."sv);
		}

	private:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESLoadGameEvent&, RE::BSTEventSource<RE::TESLoadGameEvent>*) override
		{
			// Once per session: this is advice, not a status readout.
			if (std::exchange(detail::s_warningShown, true)) {
				return RE::BSEventNotifyControl::kContinue;
			}

			const auto msg = WIO::Translations::Localize(
				"$TSO_Note_SettingsCarriedOver"sv,
				"TSO: settings imported from your previous version. Restart the game before "
				"changing them in MCM - until then MCM still shows defaults."sv);
			RE::SendHUDMessage::ShowHUDMessage(msg.c_str(), nullptr, true, true);

			REX::INFO("Throwing System Overhaul: shown the carry-over restart notice on save load"sv);
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}
