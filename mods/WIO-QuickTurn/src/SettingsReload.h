#pragma once

#include "ContextualRemap.h"
#include "InputHook.h"
#include "Keybinds.h"
#include "MenuContext.h"
#include "QuickTurn.h"
#include "Settings.h"

namespace QT::SettingsReload
{
	// kGameLoaded fires once per process, so without this an MCM change would need a restart.
	// MCM lives inside the vanilla PauseMenu.
	//
	// This also covers the vanilla Controls screen, which is reached through PauseMenu, so
	// in-game movement key rebinds take effect without a restart.
	class MenuWatcher :
		public RE::BSTEventSink<RE::MenuModeChangeEvent>
	{
	public:
		static void Install()
		{
			const auto ui = RE::UI::GetSingleton();
			if (!ui) {
				REX::WARN("Quick Turn: UI singleton unavailable - settings will not reload until restart"sv);
				return;
			}

			static MenuWatcher singleton;
			static bool registered = false;
			if (registered) {
				return;
			}

			ui->GetEventSource<RE::MenuModeChangeEvent>()->RegisterSink(&singleton);
			registered = true;
			REX::INFO("Quick Turn: settings reload watcher installed"sv);
		}

	private:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuModeChangeEvent& a_event, RE::BSTEventSource<RE::MenuModeChangeEvent>*) override
		{
			if (a_event.enteringMenuMode) {
				// Reset movement and passthrough state so events swallowed by the menu
				// do not leave held-key state stuck after it closes.
				QuickTurn::ResetMovementState();
				ContextualRemap::Reset();
				PlayerControlsHook::ResetGamepadModifierState();
				return RE::BSEventNotifyControl::kContinue;
			}
			if (a_event.menuName == "PauseMenu"sv) {
				Settings::Load();
				Keybinds::Load();
				QuickTurn::RefreshMovementKeyBindings();
				ContextualRemap::Apply();
				// Retried in case PlayerCharacter's singleton was not up at kGameLoaded. A
				// no-op once the sinks are registered.
				MenuContext::CrosshairWatcher::Install();
				MenuContext::CrosshairModeWatcher::Install();
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}
