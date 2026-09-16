#pragma once

#include "Keybinds.h"
#include "Settings.h"

namespace PipboyPipbindFix
{
	// Reloads Settings and Keybinds on every PauseMenu close, so an MCM change needs no restart.
	class SettingsReload
	{
	public:
		class MenuWatcher :
			public RE::BSTEventSink<RE::MenuModeChangeEvent>
		{
		public:
			static void Install()
			{
				const auto ui = RE::UI::GetSingleton();
				if (!ui) {
					REX::WARN("Pip-Boy Bindings Fix: RE::UI unavailable - SettingsReload not installed"sv);
					return;
				}

				static MenuWatcher singleton;
				static bool registered = false;
				if (registered) {
					return;
				}

				ui->GetEventSource<RE::MenuModeChangeEvent>()->RegisterSink(&singleton);
				registered = true;
				REX::INFO("Pip-Boy Bindings Fix: settings reload watcher installed"sv);
			}

		private:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::MenuModeChangeEvent& a_event,
				RE::BSTEventSource<RE::MenuModeChangeEvent>*) override
			{
				if (!a_event.enteringMenuMode && a_event.menuName == "PauseMenu"sv) {
					Settings::Load();
					Keybinds::Load();
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	};
}
