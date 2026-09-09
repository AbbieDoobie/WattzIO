#pragma once

#include <algorithm>
#include <array>
#include <vector>

namespace RNF::Classify
{
	namespace detail
	{
		// Settlement recruitment beacons are one shared radio station, not one per settlement.
		// Every beacon notification reports the same station reference, WorkshopRadioRef. A built
		// beacon only switches it on and off.
		//
		// That reference is not workshop-built and carries no linked-ref data. It shares its base
		// form (RadioTransmitter, 0x01FA5A) with ordinary stations, so it is matched by reference.
		struct KnownStation
		{
			const char*   plugin;
			std::uint32_t rawFormID;
			const char*   editorID;
		};

		inline constexpr std::array kBeaconStations{
			// Also covers Nuka-World raider outposts and mods using the vanilla workshop framework.
			KnownStation{ "Fallout4.esm", 0x02A195, "WorkshopRadioRef" },
			KnownStation{ "DLCworkshop03.esm", 0x004FBD, "DLC06WorkshopRadioRef" },
		};

		// WorkshopItemKeyword. Secondary test only: it does not match any vanilla or DLC beacon,
		// but would match a mod implementing its own settlement radio as a workshop-built object.
		constexpr std::uint32_t kWorkshopItemKeywordRawID = 0x054BA6;
		constexpr auto kFallout4ESM = "Fallout4.esm"sv;

		inline RE::BGSKeyword* g_workshopItemKeyword = nullptr;

		[[nodiscard]] inline RE::BGSKeyword* WorkshopItemKeyword()
		{
			if (!g_workshopItemKeyword) {
				if (const auto handler = RE::TESDataHandler::GetSingleton()) {
					g_workshopItemKeyword = handler->LookupForm<RE::BGSKeyword>(kWorkshopItemKeywordRawID, kFallout4ESM);
				}
			}
			return g_workshopItemKeyword;
		}

		inline std::vector<std::uint32_t> g_beaconStationFormIDs;
		inline bool g_stationsResolved = false;

		// Resolved through TESDataHandler so load order is accounted for and an absent DLC
		// contributes nothing. Retried until TESDataHandler is available.
		inline void ResolveBeaconStations()
		{
			if (g_stationsResolved) {
				return;
			}

			const auto handler = RE::TESDataHandler::GetSingleton();
			if (!handler) {
				return;
			}

			g_beaconStationFormIDs.clear();
			for (const auto& station : kBeaconStations) {
				if (const auto formID = handler->LookupFormID(station.rawFormID, station.plugin); formID != 0) {
					g_beaconStationFormIDs.push_back(formID);
				}
			}

			g_stationsResolved = true;

			if (g_beaconStationFormIDs.empty()) {
				REX::WARN("Radio Notification Filter: no beacon stations resolved - the Settlement Beacon toggles will have no effect"sv);
			}
		}

		[[nodiscard]] inline bool IsKnownBeaconStation(RE::TESObjectREFR* a_station)
		{
			ResolveBeaconStations();
			return std::find(g_beaconStationFormIDs.begin(), g_beaconStationFormIDs.end(), a_station->GetFormID()) != g_beaconStationFormIDs.end();
		}
	}

	// A null station is not a beacon. The engine skips the name lookup when the station reference
	// is null and still emits, using sRadioStationDefaultName.
	[[nodiscard]] inline bool IsSettlementBeacon(RE::TESObjectREFR* a_station)
	{
		if (!a_station) {
			return false;
		}

		if (detail::IsKnownBeaconStation(a_station)) {
			return true;
		}

		const auto keyword = detail::WorkshopItemKeyword();
		return keyword && a_station->GetLinkedRef(keyword) != nullptr;
	}
}
