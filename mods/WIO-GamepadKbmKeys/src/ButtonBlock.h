#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

// Suppresses a gamepad button's normal action by setting `disabled` on its ButtonEvent.
// IDEvent::QUserEvent() then returns "DISABLED", and every vanilla handler matches on that name,
// so the button stops meaning Jump, Sneak, Ready Weapon, and the rest. The idCode stays visible,
// so anything detecting the button by idCode still sees it.
//
// Drive it from a handler at index 0 of RE::PlayerControls::handlers, ahead of every vanilla
// PlayerControls handler. The flag stays on the event, so MenuControls and later receivers see
// the button disabled as well. PlayerControls only dispatches with no menu open, which scopes the
// block to gameplay on its own.
//
// Shared verbatim across the WattzIO family. The namespace is the only per-mod difference.
namespace GKK::ButtonBlock
{
	enum class Mode : std::int32_t
	{
		kUseGlobal = 0,
		kOff = 1,
		kMainOnly = 2,
		kMainAndModifier = 3
	};

	// Per-key dropdown: Use Global Setting, Off (Don't Block), Main Button Only, Main and Modifier.
	[[nodiscard]] inline Mode FromKeyIndex(std::int32_t a_index) noexcept
	{
		return (a_index >= 0 && a_index <= 3) ? static_cast<Mode>(a_index) : Mode::kUseGlobal;
	}

	// Global dropdown has no Use Global entry, so its indices sit one lower than the per-key ones.
	[[nodiscard]] inline Mode FromGlobalIndex(std::int32_t a_index) noexcept
	{
		switch (a_index) {
		case 0:
			return Mode::kOff;
		case 2:
			return Mode::kMainAndModifier;
		default:
			return Mode::kMainOnly;
		}
	}

	[[nodiscard]] inline Mode Resolve(Mode a_key, Mode a_global) noexcept
	{
		return a_key == Mode::kUseGlobal ? a_global : a_key;
	}

	namespace detail
	{
		[[nodiscard]] inline std::vector<std::uint32_t>& Claims()
		{
			static std::vector<std::uint32_t> claims;
			return claims;
		}
	}

	// Blocks the button from this event through its release. Call on the press edge: a claim made
	// mid-hold would swallow a release whose press the handlers already saw, leaving held actions
	// stuck.
	inline void Claim(std::uint32_t a_idCode)
	{
		auto& claims = detail::Claims();
		if (std::find(claims.begin(), claims.end(), a_idCode) == claims.end()) {
			claims.push_back(a_idCode);
		}
	}

	[[nodiscard]] inline bool IsClaimed(std::uint32_t a_idCode)
	{
		const auto& claims = detail::Claims();
		return std::find(claims.begin(), claims.end(), a_idCode) != claims.end();
	}

	// Call once per gamepad ButtonEvent, after anything that borrows the event has restored it.
	// The release frame is disabled before the claim ends, so the release is swallowed with the
	// press.
	inline void Apply(RE::ButtonEvent& a_event)
	{
		const auto id = static_cast<std::uint32_t>(a_event.idCode);
		if (!IsClaimed(id)) {
			return;
		}
		a_event.disabled = true;
		if (a_event.value == 0.0F) {
			auto& claims = detail::Claims();
			claims.erase(std::remove(claims.begin(), claims.end(), id), claims.end());
		}
	}

	// A release that lands while a menu has input focus never reaches PlayerControls, so a claim
	// left standing would swallow the next press after the menu closes.
	inline void Reset()
	{
		detail::Claims().clear();
	}
}
