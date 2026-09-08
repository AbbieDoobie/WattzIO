#pragma once

#include <optional>

#include "ActivateReload.h"
#include "ControlRemap.h"
#include "PowerArmorExitRemap.h"
#include "Settings.h"

namespace ARC
{
	// Raw vtable hook on PlayerCamera's BSInputEventReceiver base, so it sees unresolved input: raw
	// device and idCode, before ControlMap tags it with a UserEvent name. What Activate means per
	// device is resolved through RE::ControlMap::GetMappedKey and compared as raw keycodes, so the
	// vanilla HUD prompt stays correct for any HUD replacer or rebind. Never blocks or consumes:
	// every event still reaches _original() unmodified.

	// === F4RD RELOCATIONS ========================================================
	//   Kind   Site                                          ID / RVA     +Off
	//   vtbl   RE::VTABLE::PlayerCamera[1]                   F4RD            -
	//   vfunc  BSInputEventReceiver::PerformInputProcessing  slot 0          -
	// =============================================================================
	class InputHook
	{
	public:
		static void Install()
		{
			// F4RD:vtbl - [1] = BSInputEventReceiver subobject
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::PlayerCamera[1] };
			// F4RD:vfunc - slot 0 = PerformInputProcessing
			_original = vtbl.write_vfunc(0, &InputHook::PerformInputProcessing);
			RefreshActivateKeycodes();
			REX::INFO("Activate/Reload Combo: BSInputEventReceiver hook installed on PlayerCamera"sv);
		}

		// Re-resolves what "Activate" is bound to per device, at Install() and on every pause-menu close,
		// the vanilla Controls menu being a tab inside PauseMenu. Gated by bEnableComboGamepad and
		// bEnableComboKeyboard: a disabled device's keycode stays nullopt, so IsActivateKeycode() never
		// matches for it.
		static void RefreshActivateKeycodes()
		{
			const auto controlMap = RE::ControlMap::GetSingleton();
			if (!controlMap) {
				return;
			}

			if (Settings::bEnableComboKeyboard) {
				s_activateKeyboardKeycode = ResolveDeviceKeycode(
					RE::GetMappedKey(controlMap, "Activate"sv, RE::INPUT_DEVICE::kKeyboard), 0);
				s_activateMouseKeycode = ResolveDeviceKeycode(
					RE::GetMappedKey(controlMap, "Activate"sv, RE::INPUT_DEVICE::kMouse), F4SE::InputMap::kMacro_MouseButtonOffset);
			} else {
				s_activateKeyboardKeycode = std::nullopt;
				s_activateMouseKeycode = std::nullopt;
			}

			if (Settings::bEnableComboGamepad) {
				// Gamepad's raw inputKey is a button mask, not a small sequential index, so it needs the same
				// GamepadMaskToKeycode conversion ToUnifiedKeycode() applies below.
				const auto gamepadRaw = RE::GetMappedKey(controlMap, "Activate"sv, RE::INPUT_DEVICE::kGamepad);
				s_activateGamepadKeycode = (gamepadRaw == RE::kInvalidMappedKey) ?
				                               std::nullopt :
				                               std::optional<std::uint32_t>{ F4SE::InputMap::GamepadMaskToKeycode(gamepadRaw) };
			} else {
				s_activateGamepadKeycode = std::nullopt;
			}
		}

	private:
		static void PerformInputProcessing(RE::BSInputEventReceiver* a_this, const RE::InputEvent* a_queueHead)
		{
			for (auto event = a_queueHead; event; event = event->next) {
				// const_cast is safe: real, engine-owned mutable input-queue memory, const only
				// because of PerformInputProcessing's signature.
				if (const auto button = const_cast<RE::InputEvent*>(event)->As<RE::ButtonEvent>(); button) {
					const auto keycode = ToUnifiedKeycode(*button);
					if (!keycode) {
						continue;
					}

					if (IsActivateKeycode(*keycode)) {
						ActivateReload::Trigger(*button);
					}

					// Secondary Action's key, for the power armor exit gesture (PowerArmorExitRemap.h). Not gated on
					// the Enable Combo toggles: it replaces a vanilla binding rather than layering onto the combo.
					if (const auto secondaryKey = ControlRemap::EffectiveSecondaryActionKey(button->device.get());
						secondaryKey && static_cast<std::uint32_t>(*secondaryKey) == *keycode) {
						PowerArmorExitRemap::TryForward(*button);
					}
				}
			}

			_original(a_this, a_queueHead);
		}

		[[nodiscard]] static bool IsActivateKeycode(std::uint32_t a_keycode)
		{
			return (s_activateKeyboardKeycode && *s_activateKeyboardKeycode == a_keycode) ||
			       (s_activateMouseKeycode && *s_activateMouseKeycode == a_keycode) ||
			       (s_activateGamepadKeycode && *s_activateGamepadKeycode == a_keycode);
		}

		[[nodiscard]] static std::optional<std::uint32_t> ResolveDeviceKeycode(std::uint32_t a_raw, std::uint32_t a_offset)
		{
			if (a_raw == RE::kInvalidMappedKey) {
				return std::nullopt;
			}
			return a_offset + a_raw;
		}

		static inline std::optional<std::uint32_t> s_activateKeyboardKeycode;
		static inline std::optional<std::uint32_t> s_activateMouseKeycode;
		static inline std::optional<std::uint32_t> s_activateGamepadKeycode;

		// Converts a ButtonEvent's device-relative idCode into F4SE::InputMap's unified 0-281
		// numbering.
		[[nodiscard]] static std::optional<std::uint32_t> ToUnifiedKeycode(const RE::ButtonEvent& a_event)
		{
			switch (a_event.device.get()) {
			case RE::INPUT_DEVICE::kKeyboard:
				return static_cast<std::uint32_t>(a_event.QIDCode());
			case RE::INPUT_DEVICE::kMouse:
				if (a_event.QIDCode() == static_cast<std::int32_t>(RE::kBSButtonCodeWheelUp)) {
					return static_cast<std::uint32_t>(F4SE::InputMap::kMacro_MouseWheelOffset);
				} else if (a_event.QIDCode() == static_cast<std::int32_t>(RE::kBSButtonCodeWheelDown)) {
					return static_cast<std::uint32_t>(F4SE::InputMap::kMacro_MouseWheelOffset) + 1;
				} else {
					return F4SE::InputMap::kMacro_MouseButtonOffset + static_cast<std::uint32_t>(a_event.QIDCode());
				}
			case RE::INPUT_DEVICE::kGamepad:
				return F4SE::InputMap::GamepadMaskToKeycode(static_cast<std::uint32_t>(a_event.QIDCode()));
			default:
				return std::nullopt;
			}
		}

		using OriginalFunc = void(RE::BSInputEventReceiver*, const RE::InputEvent*);
		static inline REL::Relocation<OriginalFunc*> _original;
	};
}
