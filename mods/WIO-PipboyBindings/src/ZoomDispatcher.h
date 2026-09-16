#pragma once

#include "ControlRemap.h"
#include "Keybinds.h"
#include "Settings.h"

namespace PipboyPipbindFix
{
	// Pipboy Zoom.
	//
	// Zoom here is Pipboy examine mode, where the Pipboy moves toward the camera and the arm
	// becomes steerable. The state lives on RE::PipboyManager (+0x1C4).
	//
	// RE::PipboyMenu::OnButtonEvent drives it off the user-event name "Select", starting examine
	// mode on the press edge and stopping it on the release. "Select" is a non-remappable
	// kBasicMenuNav event bound to keyboard V, right mouse, and gamepad Back, so it never appears in
	// the Controls menu. DispatchAsSelect() rewrites a real ButtonEvent's strUserEvent to "Select",
	// hands it to that OnButtonEvent, and restores every field it touched.
	class ZoomDispatcher
	{
	public:
		static void OnButtonEvent(RE::ButtonEvent& a_event)
		{
			if (!Matches(a_event)) {
				SuppressVanillaIfConfigured(a_event);
				return;
			}

			const bool isPress   = a_event.QJustPressed();
			const bool isRelease = a_event.value == 0.0F;

			// Held frames fire a fresh event every frame (~100/sec). Vanilla no-ops on them.
			if (!isPress && !isRelease) {
				return;
			}

			// A button that already resolves to "Select" (gamepad Back, keyboard V, right mouse) has
			// been handled by vanilla already, and dispatching again would toggle examine mode twice.
			if (a_event.QRawUserEvent() == SelectEvent()) {
				return;
			}

			if (isPress && s_zoomHeld) {
				return;  // defensive: never start a second session over an open one
			}
			if (isRelease && !s_zoomHeld) {
				return;  // release with no matching press of ours - nothing to end
			}

			if (DispatchAsSelect(a_event)) {
				s_zoomHeld = isPress;
			}
		}

		// Ends an in-progress zoom before the Pipboy is hidden, forcing a release through whatever
		// real ButtonEvent is arriving this frame. Called immediately before UIMessageQueue::kHide, so
		// "hold zoom, press close" cannot leave examine mode latched with an input layer pushed.
		static void ReleaseIfHeld(RE::ButtonEvent& a_event)
		{
			if (!s_zoomHeld) {
				return;
			}

			const float savedValue = a_event.value;
			const float savedHeld  = a_event.heldDownSecs;

			a_event.value        = 0.0F;
			a_event.heldDownSecs = 0.25F;  // any non-zero: vanilla's release branch only tests value

			DispatchAsSelect(a_event);

			a_event.value        = savedValue;
			a_event.heldDownSecs = savedHeld;
			s_zoomHeld           = false;
		}

		// Called when PipboyMenu closes. Nothing is left to dispatch to, so the flag is cleared and the
		// engine's own shutdown resets examine mode. Vanilla has the same exposure via hold Back, press B.
		static void NotifyPipboyClosed() noexcept { s_zoomHeld = false; }

	private:
		// Function-local, not namespace-scope: a BSFixedString interns into an engine string pool
		// that does not exist during static initialisation. First use is inside an input callback.
		[[nodiscard]] static const RE::BSFixedString& SelectEvent()
		{
			static const RE::BSFixedString value{ "Select"sv };
			return value;
		}

		[[nodiscard]] static const RE::BSFixedString& PipboyMenuName()
		{
			static const RE::BSFixedString value{ RE::PipboyMenu::MENU_NAME };
			return value;
		}

		static inline bool s_zoomHeld = false;

		// Kills the vanilla zoom inputs (gamepad Back, keyboard V, right mouse) while the Pipboy is
		// open, freeing those buttons. Unlike handled = kStop, setting `disabled` does not halt the
		// chain, so anything matching on raw idCode can still use the button. Only reached when
		// Matches() was false, so a player's own configured button is never suppressed.
		static void SuppressVanillaIfConfigured(RE::ButtonEvent& a_event)
		{
			if (Settings::bSuppressVanillaZoom && a_event.QRawUserEvent() == SelectEvent()) {
				a_event.disabled = true;
			}
		}

		// QIDCode() reports the raw XInput bitmask for gamepad devices, so it compares straight
		// against kZoomGPXInput. Keyboard and mouse go through Keybinds::MatchesHotkey.
		[[nodiscard]] static bool Matches(const RE::ButtonEvent& a_event)
		{
			if (a_event.device.get() == RE::INPUT_DEVICE::kGamepad) {
				const int gp = Settings::iZoomGamepadButton;
				if (gp < 1 || gp > 16) {
					return false;  // 0 == OFF, or an unexpected value
				}
				return static_cast<std::int32_t>(a_event.QIDCode()) ==
					   ControlRemap::kZoomGPXInput[static_cast<std::size_t>(gp) - 1];
			}

			return Keybinds::MatchesHotkey(a_event, Keybinds::iZoomKeyboardKeycode);
		}

		// Dispatched through RE::IMenu's BSInputEventUser base: an ordinary virtual call, no id.
		static bool DispatchAsSelect(RE::ButtonEvent& a_event)
		{
			const auto ui = RE::UI::GetSingleton();
			if (!ui) {
				return false;
			}

			const auto menu = ui->GetMenu(PipboyMenuName());
			if (!menu) {
				return false;
			}

			auto* const user = static_cast<RE::BSInputEventUser*>(menu.get());

			// strUserEvent, disabled and handled are all restored below. Not clearing `disabled` would make
			// the rewrite silently do nothing, since QUserEvent() returns "DISABLED" whenever it is set.
			const RE::BSFixedString savedUserEvent = a_event.strUserEvent;
			const bool              savedDisabled  = a_event.disabled;
			const auto              savedHandled   = a_event.handled.get();

			a_event.strUserEvent = SelectEvent();
			a_event.disabled     = false;

			const bool accepted = user->ShouldHandleEvent(&a_event);
			if (accepted) {
				user->HandleEvent(&a_event);
			}

			a_event.strUserEvent = savedUserEvent;
			a_event.disabled     = savedDisabled;
			a_event.handled      = savedHandled;

			return accepted;
		}
	};
}
