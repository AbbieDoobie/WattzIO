#pragma once

#include "DiscoveredHotkeys.h"
#include "InputHook.h"
#include "Settings.h"

// kGameLoaded fires once per process, so without this an MCM change would need a restart. MCM
// lives inside the vanilla PauseMenu, and MenuModeChangeEvent fires on gameplay-blocking mode
// transitions rather than per menu instance.
namespace GMH::SettingsReload
{
	class MenuWatcher :
		public RE::BSTEventSink<RE::MenuModeChangeEvent>
	{
	public:
		static void Install()
		{
			const auto ui = RE::UI::GetSingleton();
			if (!ui) {
				REX::WARN("Gamepad MCM Hotkeys: UI singleton unavailable - settings will not reload until restart"sv);
				return;
			}

			static MenuWatcher singleton;
			static bool registered = false;
			if (registered) {
				return;
			}

			ui->GetEventSource<RE::MenuModeChangeEvent>()->RegisterSink(&singleton);
			registered = true;
			REX::INFO("Gamepad MCM Hotkeys: settings reload watcher installed"sv);
		}

	private:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuModeChangeEvent& a_event, RE::BSTEventSource<RE::MenuModeChangeEvent>*) override
		{
			if (a_event.enteringMenuMode) {
				// Any menu opening, not just PauseMenu: Pip-Boy, dialogue and container menus can all
				// swallow a modifier release the same way.
				InputHook::ResetGamepadModifierState();

				if (a_event.menuName == "PauseMenu"sv) {
					// Pushed on the way into the pause menu, not at kGameLoaded: MCM's SettingStore
					// has no entry for these keys that early and SetModSettingString is a silent
					// no-op. MCM is reached from inside the pause menu, so this is late enough to
					// land and earlier than the player can navigate to the page.
					//
					// Gap: MCM is also reachable from the main menu, where this never fires and the
					// page shows whatever the last session left in the ini.
					DiscoveredHotkeys::Push();
				}
				return RE::BSEventNotifyControl::kContinue;
			}
			if (a_event.menuName == "PauseMenu"sv) {
				Settings::Load();
				// Retried in case PlayerCharacter's singleton was not up at kGameLoaded. A
				// no-op once the sink is registered.
				MenuContext::CrosshairWatcher::Install();
				MenuContext::CrosshairModeWatcher::Install();
				// Re-scan: the player may have rebound a hotkey in another mod's MCM page
				// during this same pause-menu visit.
				DiscoveredHotkeys::Push();
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}
