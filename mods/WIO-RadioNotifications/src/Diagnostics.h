#pragma once

#include <format>
#include <string>

#include "Classify.h"
#include "Settings.h"

// Per-notification diagnostic logging, off unless the hidden bDebugLog key is set - see
// Settings.h. One line per intercepted notification: station FormID, base form, name, link data,
// classification and decision, which is what identifies a station that should classify as a beacon
// but does not.
namespace RNF::Diagnostics
{
	namespace detail
	{
		[[nodiscard]] inline std::string FormIDOrNull(RE::TESForm* a_form)
		{
			return a_form ? std::format("0x{:08X}", a_form->GetFormID()) : "<null>";
		}
	}

	[[nodiscard]] inline std::string DescribeStation(RE::TESObjectREFR* a_station)
	{
		if (!a_station) {
			return "station=<null>";
		}

		std::string out = std::format("station=0x{:08X}", a_station->GetFormID());

		if (const auto base = a_station->GetObjectReference()) {
			out += std::format(" base=0x{:08X} baseType={}", base->GetFormID(), RE::GetFormTypeString(base));
		}

		const char* name = RE::GetDisplayFullName(a_station);
		out += std::format(" name=\"{}\"", (name && name[0]) ? name : "");

		if (const auto keyword = Classify::detail::WorkshopItemKeyword()) {
			out += std::format(" workshopLink={}", detail::FormIDOrNull(a_station->GetLinkedRef(keyword)));
		}

		return out;
	}

	inline void ReportNotification(
		const char*        a_kind,
		const char*        a_message,
		RE::TESObjectREFR* a_station,
		bool               a_isBeacon,
		bool               a_emitted)
	{
		if (Settings::bDebugLog) {
			REX::DEBUG("[{}] msg=\"{}\" | {} | beacon={} | {}"sv,
				a_kind,
				a_message ? a_message : "<null>",
				DescribeStation(a_station),
				a_isBeacon,
				a_emitted ? "emitted" : "suppressed");
		}
	}
}
