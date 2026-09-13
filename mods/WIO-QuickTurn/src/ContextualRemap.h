#pragma once

#include <optional>
#include <string_view>

#include "QuickTurn.h"
#include "Settings.h"

// Shared Action Input: intercepts the configured action's button inside PlayerControls'
// PerformInputProcessing hook, before the event reaches any handler. When the direction condition
// is met the event is removed from the queue and Quick Turn fires; otherwise it is left in place
// and the vanilla handler processes it through the live ControlMap binding.
//
// ControlMap is never modified, so the binding stays visible and rebindable in the Controls menu.
namespace QT::ContextualRemap
{
	namespace detail
	{
		// Unified keycodes for the selected action, derived from GetMappedKey at Apply() time.
		// 0 = no binding on that device. Compared against incoming ButtonEvent keycodes.
		inline std::uint32_t s_kbKeycode    = 0;
		inline std::uint32_t s_mouseKeycode = 0;
		inline std::uint32_t s_gpKeycode    = 0;

		// True from a swallowed press until the matching release, so held and release events for
		// that gesture are also removed. Otherwise SprintHandler would start a sprint on the held
		// events and ReadyWeaponHandler would holster on hold.
		inline bool s_swallowActive = false;

		[[nodiscard]] constexpr std::string_view EventIDForAction(int a_action)
		{
			switch (a_action) {
			case 1: return "Sneak"sv;
			case 2: return "Sprint"sv;
			case 3: return "Activate"sv;
			case 4: return "ReadyWeapon"sv;
			default: return ""sv;
			}
		}

		// Converts a raw GetMappedKey result to the unified keycode space InputHook uses.
		[[nodiscard]] inline std::uint32_t ToUnifiedForDevice(std::uint32_t a_rawKey, RE::INPUT_DEVICE a_device)
		{
			switch (a_device) {
			case RE::INPUT_DEVICE::kKeyboard: return a_rawKey;
			case RE::INPUT_DEVICE::kMouse:    return F4SE::InputMap::kMacro_MouseButtonOffset + a_rawKey;
			case RE::INPUT_DEVICE::kGamepad:  return F4SE::InputMap::GamepadMaskToKeycode(a_rawKey);
			default:                          return 0;
			}
		}
	}

	// Reads the live ControlMap bindings for the selected action and caches them as unified
	// keycodes for HandleButtonEvent. Called at kGameLoaded and on pause-menu close. Never
	// calls RemapButton - ControlMap is strictly read-only here.
	inline void Apply()
	{
		const auto eventID = detail::EventIDForAction(Settings::iContextualAction);
		if (eventID.empty()) {
			detail::s_kbKeycode    = 0;
			detail::s_mouseKeycode = 0;
			detail::s_gpKeycode    = 0;
			detail::s_swallowActive = false;
			return;
		}

		const auto controlMap = RE::ControlMap::GetSingleton();
		if (!controlMap) {
			return;
		}

		const auto rawKB    = RE::GetMappedKey(controlMap, eventID, RE::INPUT_DEVICE::kKeyboard);
		const auto rawMouse = RE::GetMappedKey(controlMap, eventID, RE::INPUT_DEVICE::kMouse);
		const auto rawGP    = RE::GetMappedKey(controlMap, eventID, RE::INPUT_DEVICE::kGamepad);

		constexpr auto kInvalid = RE::kInvalidMappedKey;
		detail::s_kbKeycode    = (rawKB    != kInvalid) ? detail::ToUnifiedForDevice(rawKB,    RE::INPUT_DEVICE::kKeyboard) : 0;
		detail::s_mouseKeycode = (rawMouse != kInvalid) ? detail::ToUnifiedForDevice(rawMouse, RE::INPUT_DEVICE::kMouse)    : 0;
		detail::s_gpKeycode    = (rawGP    != kInvalid) ? detail::ToUnifiedForDevice(rawGP,    RE::INPUT_DEVICE::kGamepad)  : 0;
	}

	[[nodiscard]] inline bool IsSwallowLatched()
	{
		return detail::s_swallowActive;
	}

	// Clears the swallow latch. Called on any menu open so a menu that swallows a release
	// event does not leave the latch stuck.
	inline void Reset()
	{
		detail::s_swallowActive = false;
	}

	// Called from PlayerControlsHook for every ButtonEvent. Returns true when the event should be
	// removed from the queue: a press where Quick Turn fires, and the held and release events after
	// it. An unmet direction condition returns false and vanilla handles it normally, as does
	// a_blockHotkeys, so forwarding is never affected.
	[[nodiscard]] inline bool HandleButtonEvent(std::uint32_t a_keycode, RE::ButtonEvent& a_event, bool a_blockHotkeys)
	{
		if (Settings::iContextualAction <= 0) {
			return false;
		}

		const bool isContextual =
			(detail::s_kbKeycode    > 0 && a_keycode == detail::s_kbKeycode)    ||
			(detail::s_mouseKeycode > 0 && a_keycode == detail::s_mouseKeycode) ||
			(detail::s_gpKeycode    > 0 && a_keycode == detail::s_gpKeycode);
		if (!isContextual) {
			return false;
		}

		if (a_event.QJustPressed()) {
			if (a_blockHotkeys) {
				return false;  // passthrough: menu is up, don't steal this press
			}
			// Trigger also refuses when the Perspective settings turn Quick Turn off in this view,
			// and then the press must reach vanilla like any other unmet condition.
			const auto deltaYaw = QuickTurn::ComputeTurnDeltaYaw(Settings::iContextualMoveMod + 1);
			if (deltaYaw && QuickTurn::Trigger(*deltaYaw)) {
				detail::s_swallowActive = true;
				return true;   // swallow: Quick Turn fires, vanilla does not
			}
			return false;      // passthrough: vanilla fires normally
		}

		if (detail::s_swallowActive) {
			if (RE::QReleased(a_event)) {
				detail::s_swallowActive = false;
			}
			return true;       // swallow held and release following a swallowed press
		}

		return false;
	}
}
