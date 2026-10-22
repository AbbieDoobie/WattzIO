#pragma once

#include "Consume.h"
#include "InputHook.h"
#include "ItemCatalog.h"
#include "Keybinds.h"
#include "MenuContext.h"
#include "Settings.h"

namespace SH::SettingsReload
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
				REX::WARN("Smart Heal Button: UI singleton unavailable - settings will not reload until restart"sv);
				return;
			}

			static MenuWatcher singleton;
			static bool registered = false;
			if (registered) {
				return;
			}

			ui->GetEventSource<RE::MenuModeChangeEvent>()->RegisterSink(&singleton);
			registered = true;
			REX::INFO("Smart Heal Button: settings reload watcher installed"sv);
		}

	private:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuModeChangeEvent& a_event, RE::BSTEventSource<RE::MenuModeChangeEvent>*) override
		{
			if (a_event.enteringMenuMode) {
				// Any blocking menu opening can swallow the release event for the gamepad
				// modifier, so clear that transient state on the way in, not just on the way out.
				InputHook::ResetTransientState();
				// The player has changed context, and the input hook stops ticking anyway.
				Consume::Abort();
				return RE::BSEventNotifyControl::kContinue;
			}

			if (a_event.menuName == "PauseMenu"sv) {
				Settings::Load();
				Keybinds::Load();
				InputHook::RefreshGamepadKeycode();
				// The catalogue is load-order state and does not change with settings, but
				// Build() is idempotent and retried here because TESDataHandler may not have
				// been usable at kGameLoaded.
				ItemCatalog::Build();
				// Retried in case PlayerCharacter's singleton was not up at kGameLoaded. A
				// no-op once the sinks are registered.
				MenuContext::CrosshairWatcher::Install();
				MenuContext::CrosshairModeWatcher::Install();
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}
