#pragma once

// Hands the button gesture back to vanilla past the hold threshold by rewriting the key's real
// ButtonEvent into a genuine "TogglePOV" event: camera zoom while held, POV change on release,
// workshop entry at vanilla's own fEnterWorkshopDelay. Stamping the real event rather than
// calling a handler is what makes that work, the 1st/3rd person switch sitting further down the
// chain than the name-gated consumers and being reachable only by the event itself.
namespace FMB::PovWorkshop
{
	namespace detail
	{
		// Interned once, matching the "TogglePOV" BSFixedString every vanilla consumer compares against.
		[[nodiscard]] inline const RE::BSFixedString& TogglePOVName()
		{
			static const RE::BSFixedString name{ "TogglePOV" };
			return name;
		}

		// `disabled` must be cleared as well as the name set: QUserEvent() returns "DISABLED" whenever
		// that flag is set, regardless of strUserEvent, so a key with no vanilla binding, or one
		// switched off by a BSInputEnableManager layer, would ignore the stamp alone.
		inline void Stamp(RE::ButtonEvent& a_event)
		{
			a_event.strUserEvent = TogglePOVName();
			a_event.disabled = false;
		}
	}

	// The one fabricated edge: a press with heldDownSecs == 0 at the moment the player crosses X.
	// It arms every vanilla consumer that keys off a press: TogglePOVHandler's press-registered and
	// can-start-workshop flags, and the camera state's zoom baseline.
	inline void HandOffPress(RE::ButtonEvent& a_event)
	{
		a_event.value = 1.0f;
		a_event.heldDownSecs = 0.0f;
		detail::Stamp(a_event);
	}

	// Every later event passes through with its real value and heldDownSecs, so vanilla's own
	// 1.5s fEnterWorkshopDelay still fires 1.5s after the physical press rather than after the
	// handoff. No threshold crossing is faked; vanilla compares against its own INI setting.
	inline void PassThrough(RE::ButtonEvent& a_event)
	{
		detail::Stamp(a_event);
	}
}
