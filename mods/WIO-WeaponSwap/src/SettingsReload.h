#pragma once

#include "InputHook.h"
#include "Keybinds.h"
#include "MenuContext.h"
#include "Settings.h"

namespace WS::SettingsReload
{
	// kGameLoaded fires once per process, so without this an MCM change would need a restart.
	// MCM lives inside the vanilla PauseMenu.
	class MenuWatcher :
		public RE::BSTEventSink<RE::MenuModeChangeEvent>
	{
	public:
		static void Install()
		{
			const auto ui = RE::UI::GetSingleton();
			if (!ui) {
				REX::WARN("Weapon Swap Button: UI singleton unavailable - settings will not reload until restart"sv);
				return;
			}

			static MenuWatcher singleton;
			static bool registered = false;
			if (registered) {
				return;
			}

			ui->GetEventSource<RE::MenuModeChangeEvent>()->RegisterSink(&singleton);
			registered = true;
			REX::INFO("Weapon Swap Button: settings reload watcher installed"sv);
		}

	private:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuModeChangeEvent& a_event, RE::BSTEventSource<RE::MenuModeChangeEvent>*) override
		{
			if (a_event.enteringMenuMode) {
				return RE::BSEventNotifyControl::kContinue;
			}
			if (a_event.menuName == "PauseMenu"sv) {
				Settings::Load();
				Keybinds::Load();
				InputHook::RefreshGamepadKeycode();
				// Retried here in case PlayerCharacter's singleton wasn't up at kGameLoaded -
				// a no-op once the sinks are actually registered.
				MenuContext::CrosshairWatcher::Install();
				MenuContext::CrosshairModeWatcher::Install();
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}
