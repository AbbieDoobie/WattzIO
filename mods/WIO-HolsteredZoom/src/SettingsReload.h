#pragma once

#include "InputState.h"
#include "Settings.h"
#include "ZoomEffect.h"

namespace ZoomOffhand::SettingsReload
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
				REX::WARN("Holstered Zoom: RE::UI unavailable - settings reload watcher not installed"sv);
				return;
			}

			static MenuWatcher singleton;
			static bool registered = false;
			if (registered) {
				return;
			}

			ui->GetEventSource<RE::MenuModeChangeEvent>()->RegisterSink(&singleton);
			registered = true;
			REX::INFO("Holstered Zoom: settings reload watcher installed"sv);
		}

	private:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuModeChangeEvent& a_event, RE::BSTEventSource<RE::MenuModeChangeEvent>*) override
		{
			// Clears the latched zoom button across menu mode. s_secondaryAttackValue is written only
			// from WeaponDrawBlock's hook and ZoomEffect's tick hangs off PlayerCamera, and neither is
			// fed while a menu is up, so a release during a menu reaches nothing and the value stays
			// latched at 1.0. This event fires from the UI regardless of input dispatch. Clearing both
			// ways is safe: a genuine hold repopulates on the next gameplay frame.
			InputState::s_secondaryAttackValue = 0.0F;

			if (a_event.enteringMenuMode) {
				// ApplyZoom's per-frame restore does not run while a menu is up, so a zoomed FOV
				// would otherwise stand for the menu to read.
				ZoomEffect::ForceRestore();
			}

			if (!a_event.enteringMenuMode && a_event.menuName == "PauseMenu"sv) {
				Settings::Load();
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}
