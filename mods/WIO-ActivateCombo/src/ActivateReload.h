#pragma once

#include "Settings.h"

namespace ARC::ActivateReload
{
	namespace detail
	{
		// Added on top of Settings::UnholsterHoldSeconds() when forging heldDownSecs, so the value
		// lands on the "hold" side of whatever threshold ReadyWeaponHandler itself uses.
		constexpr float kForceHoldMarginSecs = 20.0f;

		// VATSMenu stays open through both targeting and playback. RE::VATS::mode is not a substitute:
		// it has only kNone/kPlayback, so it reads kNone during targeting.
		[[nodiscard]] inline bool IsVatsMenuOpen()
		{
			static const RE::BSFixedString kVatsMenu{ "VATSMenu" };
			const auto ui = RE::UI::GetSingleton();
			return ui && ui->GetMenuOpen(kVatsMenu);
		}
	}

	// Drives readyWeaponHandler only, layering Reload/Ready onto the Activate key. The real Activate
	// key still reaches PlayerControls.activateHandler untouched (InputHook.h).
	//
	// Unholster is instant on press, so a synthetic press withheld until the hold threshold works.
	// Holster is a multi-frame animation needing the button held across real frames, so a synthetic
	// press plus immediate release never starts it. That is why Holster Hold Time is not
	// configurable and the drawn-weapon case forwards every real event unmodified.
	inline void TryReadyReload(RE::ButtonEvent& a_event)
	{
		const auto pcon = RE::PlayerControls::GetSingleton();
		const auto readyWeapon = pcon ? pcon->readyWeaponHandler : nullptr;
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!readyWeapon || !player) {
			return;
		}

		// Which of the two gestures below applies, decided once at the real press and held for
		// the rest of this press/hold/release sequence.
		static bool s_drawnAtPress = false;

		// Latched at the press like s_drawnAtPress, so ReadyWeaponHandler only ever sees whole
		// press/release pairs: a press sent before VATS opened still gets its release, and a press
		// swallowed inside VATS never gets a stray one.
		static bool s_suppressAtPress = false;

		if (a_event.QJustPressed()) {
			s_drawnAtPress = player->GetWeaponMagicDrawn();
			s_suppressAtPress = Settings::bDisableReloadInVATS && detail::IsVatsMenuOpen();
		}

		if (s_suppressAtPress) {
			return;
		}

		if (s_drawnAtPress) {
			// Drawn: forward every real press/held-repeat/release event unmodified.
			readyWeapon->HandleEvent(&a_event);
			return;
		}

		// Holstered from here on. Vanilla ReadyWeaponHandler unholsters instantly on press, and Activate
		// shares that press, so a plain tap would also draw the weapon. The real press is withheld and a
		// synthetic one sent once Settings::UnholsterHoldSeconds() has elapsed.
		if (a_event.QJustPressed()) {
			return;
		}
		if (!RE::QReleased(a_event)) {
			return;
		}

		const float threshold = Settings::UnholsterHoldSeconds();
		if (a_event.heldDownSecs < threshold) {
			return;
		}

		// Reuses the real, engine-owned ButtonEvent rather than constructing one: these are novtable
		// types, so a locally built instance has no valid vtable pointer. heldDownSecs is forged to
		// 0.0f for the synthetic press, then past the threshold for the release, so vanilla reads a
		// genuine hold.
		const auto realHeldDownSecs = a_event.heldDownSecs;
		a_event.heldDownSecs = 0.0f;
		readyWeapon->HandleEvent(&a_event);  // synthetic press
		a_event.heldDownSecs = threshold + detail::kForceHoldMarginSecs;
		readyWeapon->HandleEvent(&a_event);  // release, forced past threshold
		a_event.heldDownSecs = realHeldDownSecs;
	}

	inline void Trigger(RE::ButtonEvent& a_event)
	{
		TryReadyReload(a_event);
	}
}
