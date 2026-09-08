#pragma once

#include <optional>

#include "InputLabels.h"
#include "Keybinds.h"
#include "Settings.h"

// Drives QCOpenTransferMenu and SecondaryActivate, the two controls behind Quick Loot's
// Transfer prompt and other Secondary Action prompts. Vanilla links both to ReadyWeapon
// ("!ReadyWeapon" in all three device columns of CustomControlMap.txt), so unbinding
// ReadyWeapon alone silently breaks them.
namespace ARC::ControlRemap
{
	// What Secondary Action resolves to on a device, in F4SE's unified 0-281 keycode space, or
	// nullopt when unset there or bound to the mouse wheel. Public so QuickContainerUIFix.h derives
	// its label from the same decision Apply() makes.
	[[nodiscard]] inline std::optional<std::int32_t> EffectiveSecondaryActionKey(RE::INPUT_DEVICE a_device)
	{
		if (a_device == RE::INPUT_DEVICE::kGamepad) {
			return Settings::iSecondaryActionGamepad > 0 ?
			           std::optional<std::int32_t>{ InputLabels::GamepadDropdownIndexToKeycode(Settings::iSecondaryActionGamepad) } :
			           std::nullopt;
		}

		const auto raw = Keybinds::SecondaryActionKeyboardKeycode();
		if (!raw) {
			return std::nullopt;
		}
		if (*raw == static_cast<std::int32_t>(F4SE::InputMap::kMacro_MouseWheelOffset) ||
			*raw == static_cast<std::int32_t>(F4SE::InputMap::kMacro_MouseWheelOffset) + 1) {
			return std::nullopt;
		}

		const bool isMouseValue = *raw >= static_cast<std::int32_t>(F4SE::InputMap::kMacro_MouseButtonOffset);
		if (a_device == RE::INPUT_DEVICE::kMouse) {
			return isMouseValue ? std::optional<std::int32_t>{ *raw - static_cast<std::int32_t>(F4SE::InputMap::kMacro_MouseButtonOffset) } : std::nullopt;
		}
		// kKeyboard
		return isMouseValue ? std::nullopt : raw;
	}

	namespace detail
	{
		constexpr std::int32_t kUnbound = static_cast<std::int32_t>(RE::kInvalidMappedKey);  // 0xFF

		// Writes inputKey/linked directly, bypassing RemapButton. QCOpenTransferMenu and
		// SecondaryActivate are remappable=false in CustomControlMap.txt, and RemapButton respects
		// that flag.
		inline bool WriteMapping(RE::ControlMap* a_controlMap, RE::UserEvents::INPUT_CONTEXT_ID a_context,
			RE::INPUT_DEVICE a_device, std::string_view a_eventID, std::int32_t a_inputKey, bool a_linked)
		{
			const auto context = a_controlMap->controlMaps[std::to_underlying(a_context)];
			if (!context) {
				return false;
			}

			auto&                    mappings = context->deviceMappings[std::to_underlying(a_device)];
			const RE::BSFixedString target(a_eventID);

			for (auto& mapping : mappings) {
				if (mapping.eventID == target) {
					mapping.inputKey = a_inputKey;
					mapping.linked = a_linked;
					return true;
				}
			}
			return false;
		}

		// Secondary Action and its two linked entries. Neither is exposed in the vanilla UI, so the only
		// prior state is what this plugin last wrote or the vanilla ReadyWeapon link. Must run after any
		// ReadyWeapon command.
		inline void ApplyLinkedEntries(RE::ControlMap* a_controlMap, RE::INPUT_DEVICE a_device)
		{
			const auto secondaryKey = EffectiveSecondaryActionKey(a_device);

			std::int32_t inputKey;
			bool         linked;
			if (secondaryKey) {
				// ControlMap's gamepad inputKey holds the raw XInput bitmask, not the unified keycode
				// EffectiveSecondaryActionKey returns. Keyboard and mouse already match, so only gamepad
				// converts.
				inputKey = a_device == RE::INPUT_DEVICE::kGamepad ?
				               static_cast<std::int32_t>(F4SE::InputMap::GamepadKeycodeToMask(static_cast<std::uint32_t>(*secondaryKey))) :
				               *secondaryKey;
				linked = false;
			} else {
				inputKey = static_cast<std::int32_t>(RE::GetMappedKey(a_controlMap, "ReadyWeapon"sv, a_device));
				linked = true;
			}

			WriteMapping(a_controlMap, RE::UserEvents::INPUT_CONTEXT_ID::kQuickContainerMenu, a_device, "QCOpenTransferMenu"sv, inputKey, linked);
			WriteMapping(a_controlMap, RE::UserEvents::INPUT_CONTEXT_ID::kTwoButtonRollover, a_device, "SecondaryActivate"sv, inputKey, linked);
		}
	}

	// Called at kGameLoaded and again on every pause-menu close (SettingsReload.h).
	inline void Apply()
	{
		const auto controlMap = RE::ControlMap::GetSingleton();
		if (!controlMap) {
			return;
		}

		// ReadyWeapon itself is handled in BindingCommand.h, through Control Unbinder. The linked
		// entries below read its live binding, so they must run after any command has been
		// applied, which is the order SettingsReload uses.
		detail::ApplyLinkedEntries(controlMap, RE::INPUT_DEVICE::kGamepad);
		detail::ApplyLinkedEntries(controlMap, RE::INPUT_DEVICE::kKeyboard);
		detail::ApplyLinkedEntries(controlMap, RE::INPUT_DEVICE::kMouse);

		// Gamepad buttons with no other vanilla binding in these contexts (LS, RS, D-pad, shoulders)
		// only go live for QCOpenTransferMenu/SecondaryActivate once SaveRemappings() runs.
		// Unconditional, so Secondary Action works without also unbinding ReadyWeapon.
		controlMap->SaveRemappings();
	}
}
