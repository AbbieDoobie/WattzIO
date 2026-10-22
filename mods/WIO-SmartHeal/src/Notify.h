#pragma once

#include <string>
#include <string_view>

#include "Settings.h"

// The corner HUD message that reports what a press consumed.
//
// Every user-facing string here is a translation key, but a HUD message is plain text, so
// ShowHUDMessage would print "$SH_Notify_Prefix" verbatim. WIO::Translations::Localize resolves
// it, falling back to the English beside the key.
namespace SH::Notify
{
	// Strips a leading "[...]" item-sorter tag and up to two leading spaces, so a HUD line reads
	// "x2 Stimpak" rather than "x2 [Aid] Stimpak" for anyone running a sorter. The two-space limit
	// and the "first [ to first ]" span are deliberate: an unbalanced bracket keeps its bracket
	// instead of losing the rest of the string.
	[[nodiscard]] inline std::string CleanName(std::string_view a_name)
	{
		if (!Settings::bCleanNames || a_name.empty()) {
			return std::string{ a_name };
		}

		std::string result{ a_name };
		const auto  left = result.find('[');
		const auto  right = result.find(']');
		if (left != std::string::npos && right != std::string::npos && right > left) {
			result.erase(left, right - left + 1);
		}

		std::size_t trimmed = 0;
		while (trimmed < 2 && !result.empty() && result.front() == ' ') {
			result.erase(result.begin());
			++trimmed;
		}
		return result;
	}

	inline void Show(const std::string& a_message)
	{
		if (!Settings::bNotify || a_message.empty()) {
			return;
		}
		// throttle = true so a burst of presses cannot flood the message queue; warning = false so
		// this does not use the alarming variant reserved for real warnings.
		RE::SendHUDMessage::ShowHUDMessage(a_message.c_str(), nullptr, true, false);
	}

	[[nodiscard]] inline std::string Prefix() { return WIO::Translations::Localize("$SH_Notify_Prefix"sv, "Smart Heal:"sv); }
	[[nodiscard]] inline std::string Nothing() { return WIO::Translations::Localize("$SH_Notify_Nothing"sv, "Smart Heal: nothing needed"sv); }
	[[nodiscard]] inline std::string NoItems() { return WIO::Translations::Localize("$SH_Notify_NoItems"sv, "Smart Heal: no usable items"sv); }
}
