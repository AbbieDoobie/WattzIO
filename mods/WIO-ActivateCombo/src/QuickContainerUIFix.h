#pragma once

#include <array>

#include "ControlRemap.h"
#include "InputLabels.h"

// QuickContainerWidget.XButton is a hardcoded ActionScript literal
// (`new BSButtonHintData("$QuickContainerTransfer","R","PSN_X","Xenon_X",1,null)`) that never
// reads RE::ControlMap, so a native Scaleform push is the only way its on-screen label reflects
// a remapped key.
namespace ARC::QuickContainerUIFix
{
	namespace detail
	{
		constexpr std::int32_t kUnbound = static_cast<std::int32_t>(RE::kInvalidMappedKey);

		[[nodiscard]] inline bool StepInto(const RE::Scaleform::GFx::Value& a_parent, std::string_view a_name, RE::Scaleform::GFx::Value& a_out)
		{
			return a_parent.IsObject() && a_parent.GetMember(a_name, &a_out) && !a_out.IsUndefined();
		}

		// HUDMenu -> CenterGroup_mc -> QuickContainerWidget_mc -> XButton.
		[[nodiscard]] inline bool TryGetXButton(RE::Scaleform::GFx::Value& a_out)
		{
			const auto ui = RE::UI::GetSingleton();
			if (!ui) {
				return false;
			}
			const auto menu = ui->GetMenu("HUDMenu"sv);
			if (!menu || !menu->menuObj.IsObject()) {
				return false;
			}

			RE::Scaleform::GFx::Value centerGroup;
			RE::Scaleform::GFx::Value widget;
			return StepInto(menu->menuObj, "CenterGroup_mc"sv, centerGroup) &&
			       StepInto(centerGroup, "QuickContainerWidget_mc"sv, widget) &&
			       StepInto(widget, "XButton"sv, a_out);
		}

		// What QCOpenTransferMenu currently means for one keyboard/mouse device, as display text.
		// Mirrors ControlRemap::EffectiveSecondaryActionKey's fallback logic but returns a label rather
		// than a raw code, or an empty string when that device is fully unbound.
		[[nodiscard]] inline std::string ResolveKbmDisplayText(RE::ControlMap* a_controlMap, RE::INPUT_DEVICE a_device)
		{
			if (const auto secondary = ControlRemap::EffectiveSecondaryActionKey(a_device)) {
				return InputLabels::GetKeyboardMouseDisplayText(*secondary);
			}

			const auto readyWeaponKey = static_cast<std::int32_t>(RE::GetMappedKey(a_controlMap, "ReadyWeapon"sv, a_device));
			if (readyWeaponKey == kUnbound) {
				return {};
			}

			const auto unified = a_device == RE::INPUT_DEVICE::kMouse ?
			                          readyWeaponKey + static_cast<std::int32_t>(F4SE::InputMap::kMacro_MouseButtonOffset) :
			                          readyWeaponKey;
			return InputLabels::GetKeyboardMouseDisplayText(unified);
		}
	}

	// Call after ControlRemap::Apply() in the same pass, so this reads already-finalized state.
	inline void Apply()
	{
		const auto controlMap = RE::ControlMap::GetSingleton();
		if (!controlMap) {
			return;
		}

		RE::Scaleform::GFx::Value xButton;
		if (!detail::TryGetXButton(xButton)) {
			// Expected before HUDMenu exists; HUDMenuWatcher below re-triggers this once it does.
			return;
		}

		// Secondary Action if set, else whatever ReadyWeapon's gamepad binding resolves to. GetMappedKey
		// returns a raw XInput-style bitmask and GamepadKeycodeToGlyphName expects F4SE's unified 0-281
		// space, so it converts through GamepadMaskToKeycode first.
		const auto readyWeaponUnified = static_cast<std::int32_t>(F4SE::InputMap::GamepadMaskToKeycode(
			static_cast<std::uint32_t>(RE::GetMappedKey(controlMap, "ReadyWeapon"sv, RE::INPUT_DEVICE::kGamepad))));
		const auto gamepadKey = ControlRemap::EffectiveSecondaryActionKey(RE::INPUT_DEVICE::kGamepad).value_or(readyWeaponUnified);
		const auto xenonName = InputLabels::GamepadKeycodeToGlyphName(gamepadKey);

		const auto keyboardText = detail::ResolveKbmDisplayText(controlMap, RE::INPUT_DEVICE::kKeyboard);
		const auto mouseText = detail::ResolveKbmDisplayText(controlMap, RE::INPUT_DEVICE::kMouse);
		// Prefer keyboard's own label; fall back to mouse's if keyboard is unbound but mouse
		// isn't.
		const std::string& pcKeyText = !keyboardText.empty() ? keyboardText : mouseText;

		if (pcKeyText.empty() && xenonName.empty()) {
			// Nothing sensible to show on either axis - leave whatever's already displayed.
			return;
		}

		// Owned, null-terminated storage: GFx::Value has no string_view constructor and its const char*
		// one keeps the pointer rather than copying, so these must outlive the Invoke below.
		const std::string pcKeyArg = pcKeyText.empty() ? std::string{ "?" } : pcKeyText;
		const std::string xenonArg = xenonName.empty() ? std::string{ "Xenon_X" }
		                                               : std::string{ xenonName };

		// PSN (PlayStation) glyph intentionally left as the vanilla default - no PlayStation
		// controller testing path on PC.
		const std::array<RE::Scaleform::GFx::Value, 3> args{
			RE::Scaleform::GFx::Value{ pcKeyArg.c_str() },
			RE::Scaleform::GFx::Value{ "PSN_X" },
			RE::Scaleform::GFx::Value{ xenonArg.c_str() }
		};

		RE::Scaleform::GFx::Value result;
		xButton.Invoke("SetButtons", &result, args.data(), args.size());
	}

	// HUDMenu is loaded once per game session and stays alive, so one push when it opens covers the
	// session; SettingsReload.h's pause-menu-close call covers later settings changes.
	class HUDMenuWatcher :
		public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static void Install()
		{
			static HUDMenuWatcher singleton;
			const auto ui = RE::UI::GetSingleton();
			if (!ui) {
				REX::WARN("Activate/Reload Combo: UI singleton unavailable - watcher not installed"sv);
				return;
			}
			ui->GetEventSource<RE::MenuOpenCloseEvent>()->RegisterSink(&singleton);
			REX::INFO("Activate/Reload Combo: HUDMenu-open watcher installed"sv);
		}

	private:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event.opening && a_event.menuName == "HUDMenu"sv) {
				Apply();
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}
