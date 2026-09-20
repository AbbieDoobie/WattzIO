#pragma once

#include "KeyRedirect.h"
#include "Settings.h"

namespace GKK::SettingsReload
{
	// Reloads Settings on every PauseMenu close, so an MCM change needs no restart, and clears the
	// handler's state whenever menu mode starts.
	//
	// The reset matters because a button released while a menu has input focus never reaches
	// PlayerControls: without it, a modifier held when the menu opened stays "held" forever, and a
	// block claim made before it swallows the next press after the menu closes.
	class MenuWatcher :
		public RE::BSTEventSink<RE::MenuModeChangeEvent>
	{
	public:
		static void Install()
		{
			const auto ui = RE::UI::GetSingleton();
			if (!ui) {
				REX::WARN("Gamepad KBM Keys: RE::UI unavailable - settings reload not installed"sv);
				return;
			}

			static MenuWatcher singleton;
			static bool        registered = false;
			if (registered) {
				return;
			}

			ui->GetEventSource<RE::MenuModeChangeEvent>()->RegisterSink(&singleton);
			registered = true;
		}

	private:
		RE::BSEventNotifyControl ProcessEvent(
			const RE::MenuModeChangeEvent&           a_event,
			RE::BSTEventSource<RE::MenuModeChangeEvent>*) override
		{
			if (a_event.enteringMenuMode) {
				KeyRedirect::Handler::Reset();
			} else if (a_event.menuName == "PauseMenu"sv) {
				Settings::Load();
				// Re-rotates the handler to index 0 (Handler::Install).
				KeyRedirect::Handler::Install();
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}
