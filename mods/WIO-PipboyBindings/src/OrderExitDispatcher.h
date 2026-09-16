#pragma once

#include "ControlRemap.h"
#include "Keybinds.h"
#include "Settings.h"

namespace PipboyPipbindFix
{
	// Companion Order Mode (command mode) exit.
	//
	// Order mode is what holding Activate on a companion enters. It pushes no ControlMap context
	// and opens no IMenu, and gameplay keeps running underneath, so the exit button has to be
	// swallowed here.
	//
	// Vanilla's exit shares PipboyHandler::OnButtonEvent with "open the Pip-Boy". Dispatch() rewrites
	// a real ButtonEvent's strUserEvent to "Pipboy", hands it to that handler, and restores what it
	// touched. It arms on the press edge and acts on the release edge, as vanilla does; held frames
	// are skipped, or the exit would also toggle the Pip-Boy light.
	//
	// Driven from GameplayInputWatcher. Suppression runs from MenuControlsWatcher; see
	// SuppressVanillaExit.
	class OrderExitDispatcher
	{
	public:
		// A menu holding input focus owns the configured button, and swallowing it there kills
		// Back/Accept. Same menuMode gate PipboyHandler::ShouldHandleEvent applies.
		[[nodiscard]] static bool IsActive()
		{
			if (!InCommandMode()) {
				return false;
			}
			const auto ui = RE::UI::GetSingleton();
			return ui && ui->menuMode == 0;
		}

		// actorDoingPlayerCommand is the companion being ordered, the same field PipboyHandler branches
		// on. The raw-handle test runs first and the age-validating resolve only when it passes: a
		// stale-but-nonzero handle would dispatch into the open-the-Pip-Boy branch.
		[[nodiscard]] static bool InCommandMode()
		{
			const auto player = RE::PlayerCharacter::GetSingleton();
			if (!player || !static_cast<bool>(player->actorDoingPlayerCommand)) {
				return false;
			}
			return static_cast<bool>(player->actorDoingPlayerCommand.get());
		}

		static void OnGameplayButtonEvent(RE::ButtonEvent& a_event)
		{
			if (!Matches(a_event)) {
				return;
			}

			// The configured button is already the vanilla exit, so dispatching too would fire
			// PipboyHandler twice for one press. Suppression on kills the vanilla path, leaving this
			// as the only exit.
			if (a_event.QRawUserEvent() == PipboyEvent() && !Settings::bSuppressVanillaOrderExit) {
				return;
			}

			const bool isPress   = a_event.QJustPressed();
			const bool isRelease = a_event.value == 0.0F;

			if (isPress && !s_armed) {
				if (Dispatch(a_event)) {
					s_armed = true;
				}
			} else if (isRelease && s_armed) {
				Dispatch(a_event);
				s_armed = false;
				a_event.disabled = true;
				return;
			}

			// Swallowed on press, held and release alike; edges alone would let a held frame reach a
			// gameplay handler. Conditional on s_armed, so a button this plugin is not driving keeps
			// doing whatever it normally does.
			if (s_armed) {
				a_event.disabled = true;
			}
		}

		// Kills the vanilla order-mode exit so the Pip-Boy button stops backing out of it.
		// PipboyHandler::ShouldHandleEvent compares QUserEvent() against "Pipboy", and QUserEvent()
		// returns "DISABLED" whenever `disabled` is set, so the handler declines and never arms.
		static void SuppressVanillaExit(RE::ButtonEvent& a_event)
		{
			if (Settings::bSuppressVanillaOrderExit && a_event.QRawUserEvent() == PipboyEvent()) {
				a_event.disabled = true;
			}
		}

		// Called when order mode is not active. An armed press whose release lands after order mode
		// ended must not dispatch, because that branch opens the Pip-Boy instead.
		static void NotifyCommandModeEnded() noexcept { s_armed = false; }

	private:
		// Function-local, not namespace-scope: a BSFixedString interns into an engine string pool
		// that does not exist during static initialisation. First use is inside an input callback.
		[[nodiscard]] static const RE::BSFixedString& PipboyEvent()
		{
			static const RE::BSFixedString value{ "Pipboy"sv };
			return value;
		}

		static inline bool s_armed = false;

		// QIDCode() reports the raw XInput bitmask for gamepad, so it compares straight against
		// kOrderExitGPXInput.
		[[nodiscard]] static bool Matches(const RE::ButtonEvent& a_event)
		{
			if (a_event.device.get() == RE::INPUT_DEVICE::kGamepad) {
				const auto target = ResolveGamepadBitmask();
				return target && static_cast<std::int32_t>(a_event.QIDCode()) == *target;
			}

			return Keybinds::MatchesHotkey(a_event, Keybinds::iOrderExitKeyboardKeycode);
		}

		[[nodiscard]] static std::optional<std::int32_t> ResolveGamepadBitmask()
		{
			const int gp = Settings::iOrderExitGamepadButton;

			if (gp == 0) {
				const auto cm = RE::ControlMap::GetSingleton();
				if (!cm) {
					return std::nullopt;
				}
				const auto mapped = RE::GetMappedKey(cm, "Pipboy"sv, RE::INPUT_DEVICE::kGamepad);
				if (mapped == 0 || mapped == RE::kInvalidMappedKey) {
					return std::nullopt;
				}
				return static_cast<std::int32_t>(mapped);
			}

			if (gp >= 2 && gp <= 17) {
				return ControlRemap::kOrderExitGPXInput[static_cast<std::size_t>(gp) - 2];
			}

			// gp == 1 ("OFF"), or an unexpected value.
			return std::nullopt;
		}

		// PipboyHandler is only forward-declared, but pipboyHandler is a member of
		// MenuControls::handlers (BSTArray<BSInputEventUser*>), so its BSInputEventUser base is real.
		static bool Dispatch(RE::ButtonEvent& a_event)
		{
			const auto controls = RE::MenuControls::GetSingleton();
			if (!controls || !controls->pipboyHandler) {
				return false;
			}

			auto* const user = reinterpret_cast<RE::BSInputEventUser*>(controls->pipboyHandler);

			// strUserEvent, disabled, and handled are all restored below.
			const RE::BSFixedString savedUserEvent = a_event.strUserEvent;
			const bool              savedDisabled  = a_event.disabled;
			const auto              savedHandled   = a_event.handled.get();

			a_event.strUserEvent = PipboyEvent();
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
