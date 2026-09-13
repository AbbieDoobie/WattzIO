#pragma once

#include <array>
#include <string_view>

#include "ControlRemap.h"

namespace UnbindAny::SettingsReload
{
	// Re-reads settings and re-applies unbinds on pause-menu close, so MCM changes take effect
	// without a save or reload.
	//
	// Also lends vanilla binds to some menus while they are open. Each lend happens only when its
	// workaround toggle is on and at least one of the binds it covers is unbound; otherwise the
	// menu's open and close are ignored entirely.
	//
	// - Dialogue menu, Quick Slots ("Workaround for Incorrect Menu Binds"). Some dialogue replacers
	//   navigate with the D-pad Quick Slot events rather than dedicated menu binds.
	// - Workshop menu, ZoomIn/ZoomOut ("Workaround for Workshop Menu Zoom Binds"). WorkshopMenu
	//   zooms on the kMainGameplay mouse wheel events; kWorkshop has no zoom entries of its own.
	// - Pip-Boy and Favorites menus, Quickkey1-12 ("Workaround for Pip-Boy and Quick Menu Favorite
	//   Binds"). Both assign an item to a favorite slot on the kMainGameplay Quickkey<n> events, and
	//   no other context carries those keys.
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

		static constexpr std::array kQuickSlotEvents{ "QuickkeyUp"sv, "QuickkeyDown"sv,
			"QuickkeyLeft"sv, "QuickkeyRight"sv };
		static constexpr std::array kZoomEvents{ "ZoomIn"sv, "ZoomOut"sv };
		static constexpr std::array kFavoriteEvents{ "Quickkey1"sv, "Quickkey2"sv, "Quickkey3"sv,
			"Quickkey4"sv, "Quickkey5"sv, "Quickkey6"sv, "Quickkey7"sv, "Quickkey8"sv, "Quickkey9"sv,
			"Quickkey10"sv, "Quickkey11"sv, "Quickkey12"sv };
		static constexpr auto kFavoritesMenuName = "FavoritesMenu"sv;

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuModeChangeEvent& a_event, RE::BSTEventSource<RE::MenuModeChangeEvent>*) override
		{
			if (a_event.menuName != "PauseMenu"sv) {
				return RE::BSEventNotifyControl::kContinue;
			}

			// Populate the status rows before the player can look at them. The push at kGameLoaded
			// can land before MCM has built its settings store - built once per launch, never
			// re-read - which leaves every row blank.
			if (a_event.enteringMenuMode) {
				ControlRemap::RefreshStatus();
				return RE::BSEventNotifyControl::kContinue;
			}

			// Skipped while the Dialogue menu holds the Quick Slot binds - it reapplies the user's
			// real settings itself when it closes.
			if (m_dialogueLending) {
				return RE::BSEventNotifyControl::kContinue;
			}

			// Re-evaluated before Apply(), so a bind or workaround change made in this pause menu
			// takes effect for a menu still open behind it.
			if (m_workshopMenuOpen) {
				UpdateWorkshopZoomLend();
			}
			if (m_pipboyMenuOpen || m_favoritesMenuOpen) {
				UpdateFavoritesLend();
			}
			ControlRemap::Apply("PauseMenu close"sv);
			return RE::BSEventNotifyControl::kContinue;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event.menuName == RE::kDialogueMenuName) {
				OnDialogueMenu(a_event.opening);
			} else if (a_event.menuName == RE::WorkshopMenu::MENU_NAME) {
				OnWorkshopMenu(a_event.opening);
			} else if (a_event.menuName == RE::PipboyMenu::MENU_NAME) {
				m_pipboyMenuOpen = a_event.opening;
				OnFavoritesMenu(a_event.opening);
			} else if (a_event.menuName == kFavoritesMenuName) {
				m_favoritesMenuOpen = a_event.opening;
				OnFavoritesMenu(a_event.opening);
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		void OnDialogueMenu(bool a_opening)
		{
			if (a_opening) {
				// Default ON, because MCM only writes a key into the runtime Settings ini once the
				// player has touched that control - see GetDesired in Settings.h.
				m_dialogueLending = Settings::GetDesired("Gamepad", "bWorkaroundIncorrectMenuBinds", "1") &&
				                    ControlRemap::AnyInMemoryUnbound(kQuickSlotEvents,
										ControlMapService::Slot::kGamepad);
				if (m_dialogueLending) {
					// Deferred by a few real frames through F4SE's task queue: a D-pad press landing
					// as the Dialogue menu opens can otherwise fire the real Quick Slot action
					// alongside dialogue navigation. blockPlayerInput never becomes true during a
					// dialogue, so it is no use as a signal to wait on.
					DeferredRestoreQuickSlots(kDeferFrames);
				}
				return;
			}

			if (m_dialogueLending) {
				m_dialogueLending = false;
				// Hand the D-pad back to whatever the user actually configured.
				ControlRemap::Apply("Dialogue close"sv);
			}
		}

		void OnWorkshopMenu(bool a_opening)
		{
			m_workshopMenuOpen = a_opening;

			if (a_opening) {
				if (UpdateWorkshopZoomLend()) {
					ControlRemap::Apply("Workshop open"sv);
				}
				return;
			}

			if (ControlRemap::WorkshopZoomLend()) {
				ControlRemap::SetWorkshopZoomLend(false);
				ControlRemap::Apply("Workshop close"sv);
			}
		}

		// Shared by the Pip-Boy and Favorites menus. Opening writes only when the lend was not
		// already in effect; closing hands the keys back only once neither menu is still open.
		void OnFavoritesMenu(bool a_opening)
		{
			const bool wasLending = ControlRemap::FavoritesLend();
			const bool lending = UpdateFavoritesLend();

			if (a_opening) {
				if (lending && !wasLending) {
					ControlRemap::Apply("Favorites lend open"sv);
				}
				return;
			}

			if (wasLending && !lending) {
				ControlRemap::Apply("Favorites lend close"sv);
			}
		}

		// Default ON, for the same reason as the Dialogue workaround.
		bool UpdateFavoritesLend()
		{
			const bool lend = (m_pipboyMenuOpen || m_favoritesMenuOpen) &&
			                  Settings::GetDesired("Keyboard", "bWorkaroundFavoriteMenuBinds", "1") &&
			                  ControlRemap::AnyInMemoryUnbound(kFavoriteEvents, ControlMapService::Slot::kKeyboard);
			ControlRemap::SetFavoritesLend(lend);
			return lend;
		}

		// Default ON, for the same reason as the Dialogue workaround.
		bool UpdateWorkshopZoomLend()
		{
			const bool lend = m_workshopMenuOpen &&
			                  Settings::GetDesired("Keyboard", "bWorkaroundWorkshopZoomBinds", "1") &&
			                  ControlRemap::AnyInMemoryUnbound(kZoomEvents, ControlMapService::Slot::kKeyboard);
			ControlRemap::SetWorkshopZoomLend(lend);
			return lend;
		}

		// Re-queues itself through F4SE's task interface until a_framesRemaining reaches 0, then
		// writes. Re-checks m_dialogueLending first: the menu may have closed meanwhile, in which
		// case the close branch has already restored the user's settings.
		void DeferredRestoreQuickSlots(int a_framesRemaining)
		{
			if (a_framesRemaining > 0) {
				F4SE::GetTaskInterface()->AddTask([this, a_framesRemaining]() {
					DeferredRestoreQuickSlots(a_framesRemaining - 1);
				});
				return;
			}

			if (!m_dialogueLending) {
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

		bool m_dialogueLending = false;
		bool m_workshopMenuOpen = false;
		bool m_pipboyMenuOpen = false;
		bool m_favoritesMenuOpen = false;
	};
}
