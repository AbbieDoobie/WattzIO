#pragma once

#include "Settings.h"

// Moves the vanilla "hold Activate to exit power armor" gesture onto the Secondary Action
// button, because this mod already puts Ready/Reload on a hold of Activate and vanilla cannot
// separate the two.
//
// ActivateHandler::OnButtonEvent can only do two things for an event with value != 0, both gated
// on heldDownSecs against an INI threshold: grab the object under the crosshair, and exit power
// armor. It then jumps to its epilogue, so activation-on-release is unreachable while the button
// is down. A press (heldDownSecs == 0) only registers the press and clears the two
// done-this-gesture latches; a release performs an ordinary activation.
//
// So: drop the held-repeat Activate events while in power armor and forward the real held-repeat
// Secondary Action events to the same handler instead. Press and release flow untouched, so
// tap-to-activate is unaffected and the engine's own thresholds, checks, messages and animation
// all run on a genuine event. Hold-to-grab moves with it, both hold actions living in that one
// branch separated only by their thresholds.
namespace ARC::PowerArmorExitRemap
{
	namespace detail
	{
		// Calls the real ActivateHandler::OnButtonEvent, bypassing this plugin's own hook on that
		// slot. CombatActivateBlock's ActivateHandlerHook owns the slot-8 hook and so holds the
		// only pointer to the original.
		using ForwardFunc = void(const RE::ButtonEvent*);
		inline ForwardFunc* s_forward = nullptr;

		// A held-repeat event: the button is still down and the engine has been accumulating time on
		// it. Presses (heldDownSecs == 0) and releases (value == 0) are excluded.
		[[nodiscard]] inline bool IsHeldRepeat(const RE::ButtonEvent& a_event)
		{
			return a_event.value != 0.0F && a_event.heldDownSecs > 0.0F;
		}

		// Read live on every call, so toggling the MCM switch takes effect on the next button event
		// with no pause-menu round trip.
		[[nodiscard]] inline bool Active()
		{
			if (!Settings::bPowerArmorExitOnSecondary) {
				return false;
			}
			const auto player = RE::PlayerCharacter::GetSingleton();
			// RE::PowerArmor::ActorInPowerArmor is a header-documented native, not a raw flag read of this
			// mod's own, so it needs no F4RD relocation entry.
			return player && RE::PowerArmor::ActorInPowerArmor(*player);
		}
	}

	inline void SetForwarder(detail::ForwardFunc* a_forward)
	{
		detail::s_forward = a_forward;
	}

	// True when this Activate event must not reach the vanilla handler. Called
	// from CombatActivateBlock::ActivateHandlerHook::OnButtonEvent.
	[[nodiscard]] inline bool ShouldDropActivateEvent(const RE::ButtonEvent* a_event)
	{
		return a_event && detail::IsHeldRepeat(*a_event) && detail::Active();
	}

	// Called from InputHook.h for every real Secondary Action button event, on
	// whichever device it is bound to.
	inline void TryForward(RE::ButtonEvent& a_event)
	{
		if (!detail::s_forward || !detail::IsHeldRepeat(a_event) || !detail::Active()) {
			return;
		}

		// The exit branch sets `handled` to kStop, and that field belongs to the real input queue, still
		// mid-flight here, so it is saved and restored: this forwarding is an extra consumer of the
		// event, not a redirection of it.
		const auto handled = a_event.handled;
		detail::s_forward(&a_event);
		a_event.handled = handled;
	}
}
