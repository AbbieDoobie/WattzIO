#pragma once

#include "BindingCommand.h"
#include "InputHook.h"
#include "Keybinds.h"
#include "MenuContext.h"
#include "Settings.h"

namespace FMB::SettingsReload
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
				REX::WARN("Flashlight Button: UI singleton unavailable - settings will not reload until restart"sv);
				return;
			}

			static MenuWatcher singleton;
			static bool registered = false;
			if (registered) {
				return;
			}

			ui->GetEventSource<RE::MenuModeChangeEvent>()->RegisterSink(&singleton);
			registered = true;
			REX::INFO("Flashlight Button: settings reload watcher installed"sv);
		}

	private:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuModeChangeEvent& a_event, RE::BSTEventSource<RE::MenuModeChangeEvent>*) override
		{
			if (a_event.enteringMenuMode) {
				// A blocking menu opening can swallow a release for the button or the gamepad
				// modifier, so transient state is cleared on the way in as well as out.
				InputHook::ResetTransientState();

				// Refresh the binding status rows on the way in, not only after a command. MCM
				// builds its settings store once per launch, and a push at kGameLoaded can land
				// before that store exists, leaving the rows blank.
				BindingCommand::RefreshStatus();
				return RE::BSEventNotifyControl::kContinue;
			}

			if (a_event.menuName == "PauseMenu"sv) {
				Settings::Load();
				Keybinds::Load();
				InputHook::RefreshGamepadKeycode();
				BindingCommand::Run();
				// Retried in case PlayerCharacter's singleton was not up at kGameLoaded. A
				// no-op once the sinks are registered.
				MenuContext::CrosshairWatcher::Install();
				MenuContext::CrosshairModeWatcher::Install();
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}
