#pragma once

#include "CombatActivateBlock.h"
#include "BindingCommand.h"
#include "ControlRemap.h"
#include "InputHook.h"
#include "Keybinds.h"
#include "QuickContainerUIFix.h"
#include "Settings.h"

namespace ARC::SettingsReload
{
	// kGameLoaded fires once per process, so without this an MCM change would need a restart.
	// Also re-resolves the live "Activate" binding, since the vanilla Controls menu is a tab
	// inside the same PauseMenu.
	class MenuWatcher :
		public RE::BSTEventSink<RE::MenuModeChangeEvent>
	{
	public:
		static void Install()
		{
			const auto ui = RE::UI::GetSingleton();
			if (!ui) {
				REX::WARN("Activate/Reload Combo: UI singleton unavailable - settings will not reload until restart"sv);
				return;
			}

			static MenuWatcher singleton;
			static bool registered = false;
			if (registered) {
				return;
			}

			ui->GetEventSource<RE::MenuModeChangeEvent>()->RegisterSink(&singleton);
			registered = true;
			REX::INFO("Activate/Reload Combo: settings reload watcher installed"sv);
		}

	private:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuModeChangeEvent& a_event, RE::BSTEventSource<RE::MenuModeChangeEvent>*) override
		{
			if (a_event.enteringMenuMode) {
				BindingCommand::RefreshStatus();
				return RE::BSEventNotifyControl::kContinue;
			}
			if (a_event.menuName == "PauseMenu"sv) {
				Settings::Load();
				Keybinds::Load();
				BindingCommand::Run();
				ControlRemap::Apply();
				InputHook::RefreshActivateKeycodes();
				QuickContainerUIFix::Apply();

				CombatActivateBlock::Install();
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}
