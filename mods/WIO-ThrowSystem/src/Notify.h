#pragma once

#include <string>
#include <string_view>

#include "Settings.h"

namespace TSO::Notify
{
	// HUD notifications, via RE::SendHUDMessage::ShowHUDMessage(message, sound, throttle,
	// warning). The (msg, nullptr, true, true) call pattern is for plain informational text
	// rather than an error dialog.

	inline void Show(std::string_view a_message)
	{
		const std::string msg{ a_message };
		RE::SendHUDMessage::ShowHUDMessage(msg.c_str(), nullptr, true, true);
	}

	// Strips a leading "[...]"-bracketed item-sorter tag and up to two leading spaces, gated by
	// bCleanNotificationNames. A no-op passthrough when that setting is off.
	[[nodiscard]] inline std::string CleanName(std::string_view a_name)
	{
		if (!Settings::bCleanNotificationNames.GetValue() || a_name.empty()) {
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

	// TESForm has no GetName() of its own: display names live on the TESFullName mixin, which
	// this resolves for any form without knowing the concrete derived type.
	[[nodiscard]] inline std::string_view NameOf(const RE::TESForm* a_form)
	{
		return a_form ? RE::TESFullName::GetFullName(*a_form) : std::string_view{};
	}

	// Substitutes every occurrence of each {TOKEN} placeholder with its replacement value.
	[[nodiscard]] inline std::string Format(std::string_view a_template, std::initializer_list<std::pair<std::string_view, std::string_view>> a_replacements)
	{
		std::string result{ a_template };
		for (const auto& [token, value] : a_replacements) {
			for (std::size_t pos = 0; (pos = result.find(token, pos)) != std::string::npos;) {
				result.replace(pos, token.size(), value);
				pos += value.size();
			}
		}
		return result;
	}
}
