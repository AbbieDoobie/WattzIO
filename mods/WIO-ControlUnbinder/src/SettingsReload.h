#pragma once

#include "ControlRemap.h"

namespace UnbindAny::SettingsReload
{
	// Re-reads settings and re-applies unbinds on pause-menu close, so MCM changes take effect
	// without a save or reload.
	//
	// Also watches the Dialogue menu. Some dialogue replacers repurpose the D-pad Quick Slot binds
	// for menu navigation rather than using dedicated binds, so unbinding Quick Slots breaks
	// navigation there. The "Workaround for Incorrect Menu Binds" toggle lends the Dialogue menu
	// the vanilla D-pad binds while it is open and hands them back on close.
	class MenuWatcher :
		public RE::BSTEventSink<RE::MenuModeChangeEvent>,
		public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static void Install()
		{
			const auto ui = RE::UI::GetSingleton();
			if (!ui) {
				REX::WARN("Control Unbinder: RE::UI unavailable - settings reload watcher not installed"sv);
				return;
			}

			static MenuWatcher singleton;
			static bool registered = false;
			if (registered) {
				return;
			}

			ui->GetEventSource<RE::MenuModeChangeEvent>()->RegisterSink(&singleton);
			ui->GetEventSource<RE::MenuOpenCloseEvent>()->RegisterSink(&singleton);
			registered = true;
			REX::INFO("Control Unbinder: settings reload watcher installed"sv);
		}

	private:
		// Frames to defer the D-pad restore write by once the Dialogue menu opens - see
		// DeferredRestoreQuickSlots. A frame count rather than a wall-clock delay, so the real wait
		// varies with framerate: ~1s at 60 FPS, ~0.25s at 240. 30 frames was not enough.
		static constexpr int kDeferFrames = 60;

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuModeChangeEvent& a_event, RE::BSTEventSource<RE::MenuModeChangeEvent>*) override
		{
			// Populate the status rows before the player can look at them. The push at kGameLoaded
			// can land before MCM has built its settings store - built once per launch, never
			// re-read - which leaves every row blank.
			if (a_event.enteringMenuMode && a_event.menuName == "PauseMenu"sv) {
				ControlRemap::RefreshStatus();
				return RE::BSEventNotifyControl::kContinue;
			}

			// Skipped while the Dialogue menu's own workaround is active - it owns the Quick
			// Slot binds until it closes and reapplies the user's real settings itself.
			if (!a_event.enteringMenuMode && a_event.menuName == "PauseMenu"sv && !m_dialogueMenuOpen) {
				ControlRemap::Apply("PauseMenu close"sv);
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event.menuName != RE::kDialogueMenuName) {
				return RE::BSEventNotifyControl::kContinue;
			}

			m_dialogueMenuOpen = a_event.opening;

			// Default ON, because MCM only writes a key into the runtime Settings ini once the player
			// has touched that control - see GetDesired in Settings.h.
			if (!Settings::GetDesired("Gamepad", "bWorkaroundIncorrectMenuBinds", "1")) {
				return RE::BSEventNotifyControl::kContinue;
			}

			if (a_event.opening) {
				// Deferred by a few real frames through F4SE's task queue: a D-pad press landing as
				// the Dialogue menu opens can otherwise fire the real Quick Slot action alongside
				// dialogue navigation. blockPlayerInput never becomes true during a dialogue, so it
				// is no use as a signal to wait on.
				DeferredRestoreQuickSlots(kDeferFrames);
			} else {
				// Menu closed - hand the D-pad back to whatever the user actually configured.
				ControlRemap::Apply("Dialogue close"sv);
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		// Re-queues itself through F4SE's task interface until a_framesRemaining reaches 0, then
		// writes. Re-checks m_dialogueMenuOpen first: the menu may have closed meanwhile, in which
		// case the close branch has already restored the user's settings.
		void DeferredRestoreQuickSlots(int a_framesRemaining)
		{
			if (a_framesRemaining > 0) {
				F4SE::GetTaskInterface()->AddTask([this, a_framesRemaining]() {
					DeferredRestoreQuickSlots(a_framesRemaining - 1);
				});
				return;
			}

			if (!m_dialogueMenuOpen) {
				return;
			}

			// Force the vanilla D-pad binds back for as long as the Dialogue menu is open,
			// regardless of the player's Quick Slot unbind settings. Force() rather than Apply()
			// because this is a temporary override, not a settings-driven end state - it must be
			// written every pass even when the engine already reads the intended value.
			using Slot = ControlMapService::Slot;
			ControlMapService::Force("QuickkeyUp"sv, Slot::kGamepad, Settings::kGP_QuickSlotUp_Default);
			ControlMapService::Force("QuickkeyDown"sv, Slot::kGamepad, Settings::kGP_QuickSlotDown_Default);
			ControlMapService::Force("QuickkeyLeft"sv, Slot::kGamepad, Settings::kGP_QuickSlotLeft_Default);
			ControlMapService::Force("QuickkeyRight"sv, Slot::kGamepad, Settings::kGP_QuickSlotRight_Default);
			// Force() already performs the kick that repairs the sort order and calls
			// SaveRemappings itself - see ControlMapService.h rule 2.
		}

		bool m_dialogueMenuOpen = false;
	};
}
