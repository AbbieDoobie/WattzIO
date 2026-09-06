#pragma once

#include "BindingCommand.h"
#include "InputHook.h"
#include "Keybinds.h"
#include "MenuContext.h"
#include "Settings.h"

namespace TSO::SettingsReload
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
				REX::WARN("Throwing System Overhaul: UI singleton unavailable - settings will not reload until restart"sv);
				return;
			}

			static MenuWatcher singleton;
			static bool registered = false;
			if (registered) {
				return;
			}

			ui->GetEventSource<RE::MenuModeChangeEvent>()->RegisterSink(&singleton);
			registered = true;
			REX::INFO("Throwing System Overhaul: settings reload watcher installed"sv);
		}

	private:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuModeChangeEvent& a_event, RE::BSTEventSource<RE::MenuModeChangeEvent>*) override
		{
			if (a_event.enteringMenuMode) {
				BindingCommand::RefreshStatus();
				// Any menu opening, not just PauseMenu: Pip-Boy, dialogue and container menus
				// can all swallow a release event the same way.
				InputHook::ResetGamepadModifierState();
				return RE::BSEventNotifyControl::kContinue;
			}
			if (a_event.menuName == "PauseMenu"sv) {
				Settings::Load();
				BindingCommand::Run();
				// Keybinds::Get() reads an in-memory map, so without this a hotkey rebound in
				// MCM would keep firing its old binding until a restart.
				Keybinds::Load();
				// Retried in case PlayerCharacter's singleton was not up at kGameLoaded. A
				// no-op once the sinks are registered.
				MenuContext::CrosshairWatcher::Install();
				MenuContext::CrosshairModeWatcher::Install();
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}
