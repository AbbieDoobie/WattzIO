#pragma once

#include <array>
#include <string>

#include "MenuContext.h"
#include "Settings.h"
#include "VirtualKey.h"

namespace GMH
{
	// All 10 slots. gamepad press -> SendInput -> the game's own BSInputEventReceiver -> an
	// unrelated mod's MCM-captured hotkey fires normally.
	// === F4RD RELOCATIONS ========================================================
	//   Kind   Site                                          ID / RVA     +Off
	//   vtbl   RE::VTABLE::PlayerCamera[1]                   F4RD            -
	//   vfunc  BSInputEventReceiver::PerformInputProcessing  slot 0          -
	// =============================================================================
	class InputHook
	{
	public:
		static void Install()
		{
			// F4RD:vtbl - [1] = BSInputEventReceiver subobject
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::PlayerCamera[1] };
			// F4RD:vfunc - slot 0 = PerformInputProcessing
			_original = vtbl.write_vfunc(0, &InputHook::PerformInputProcessing);
			REX::INFO("Gamepad MCM Hotkeys: input hook installed"sv);
		}

		// A modifier's release can be swallowed if a menu had input focus when the player let go, and
		// there is no live "is this button down" poll. Forgetting a held modifier costs a re-press;
		// a stuck flag costs more.
		static void ResetGamepadModifierState()
		{
			s_modHeld.fill(false);
		}

	private:
		// Menu and prompt classification lives in MenuContext.h. A single "is any non-gameplay
		// context up" boolean over-blocks, counting the engine's own crosshair button-prompt contexts
		// as menus. MenuContext::Capture() reports the pieces separately so each slot can decide.
		static void PerformInputProcessing(RE::BSInputEventReceiver* a_this, const RE::InputEvent* a_queueHead)
		{
			// Captured once per frame, before the event loop, not per event and not per slot.
			// Every slot consults the same snapshot.
			const auto state = MenuContext::Capture();

			// Off unless Settings::debugLog is set by hand; gating keeps the line-building cost off
			// the per-frame path. Logs only when the rendered line changes.
			if (Settings::debugLog) {
				if (auto line = MenuContext::BuildDiagnosticLine(state); line != s_lastDiagLine) {
					REX::DEBUG("Gamepad MCM Hotkeys: [DIAG] {}", line);
					s_lastDiagLine = std::move(line);
				}
			}

			// Pausing menus are handled here rather than per slot, the engine already having stopped
			// routing gameplay input to this receiver. The queue is forwarded unmodified either way.
			if (!state.pausingMenuOpen) {
				for (auto event = a_queueHead; event; event = event->next) {
					if (const auto button = const_cast<RE::InputEvent*>(event)->As<RE::ButtonEvent>(); button) {
						if (button->device.get() == RE::INPUT_DEVICE::kGamepad) {
							if (const auto keycode = F4SE::InputMap::GamepadMaskToKeycode(static_cast<std::uint32_t>(button->QIDCode()))) {
								HandleHotkey(keycode, *button, state);
							}
						}
					}
				}
			}

			// Runs every frame regardless of whether an event arrived or of menuMode: a slot can
			// become blocked, or the player can open the Pip-Boy, with no gamepad event at all,
			// and the release edge that would end an injected keypress would never be seen. See
			// ReleaseStrandedInjections.
			ReleaseStrandedInjections(state);

			_original(a_this, a_queueHead);
		}

		// A key-down goes out on the gamepad button's press edge and the matching key-up on its release
		// edge, mirroring the physical button so a target mod's hold gesture works. If blocking begins
		// between those edges the trigger pass is skipped, the key-up never fires, and the injected key
		// stays down at the OS level for the session, which is not game state that can be resynced. So
		// a slot with an injection outstanding sends its key-up the moment it stops being allowed to
		// fire: menus, pausing menus, and a settings reload mid-hold.
		static void ReleaseStrandedInjections(const MenuContext::State& a_state)
		{
			for (int i = 0; i < Settings::kSlotCount; ++i) {
				if (!s_injectedDown[i]) {
					continue;
				}
				if (!a_state.pausingMenuOpen && !MenuContext::ShouldBlock(a_state, Settings::EffectiveBlockMode(i))) {
					continue;  // still allowed to be down
				}
				if (const auto vkey = VirtualKey::FromDropdownIndex(Settings::slots[i].virtualKey); vkey) {
					VirtualKey::Send(*vkey, false);
				}
				s_injectedDown[i] = false;
			}
		}

		// Two passes over all 10 slots, because one incoming keycode can be one slot's modifier and
		// another slot's trigger at the same time. Every slot's held-modifier state updates first,
		// then every slot's trigger is checked against the now-current state.
		static void HandleHotkey(std::uint32_t a_keycode, const RE::ButtonEvent& a_event, const MenuContext::State& a_state)
		{
			// Pass 1 is not gated on the menu state: skipping a blocked slot's modifier press or release
			// would leave s_modHeld stale, so the combo would misfire or refuse to fire once the menu
			// closed. Only acting on the state needs gating.
			for (int i = 0; i < Settings::kSlotCount; ++i) {
				const auto gamepadMod = GamepadDropdownToKeycode(Settings::slots[i].gamepadModifier);
				if (gamepadMod > 0 && a_keycode == gamepadMod) {
					s_modHeld[i] = RE::QPressed(a_event);
				}
			}

			for (int i = 0; i < Settings::kSlotCount; ++i) {
				const auto& slot = Settings::slots[i];
				const auto gamepadKey = GamepadDropdownToKeycode(slot.gamepadKey);
				if (gamepadKey == 0 || a_keycode != gamepadKey) {
					continue;
				}

				// Resolved inside the loop, not hoisted: two slots can disagree, one set to fire during
				// quickloot and another not. A blocked slot still gets its release processed below if it has
				// an injection outstanding, so a press that started while allowed always gets its key-up.
				const auto mode = Settings::EffectiveBlockMode(i);
				const bool blocked = MenuContext::ShouldBlock(a_state, mode);

				// Recorded at the press, which separates "the setting never reached the code" from "the
				// classification was wrong". Same hidden switch.
				if (Settings::debugLog && a_event.QJustPressed()) {
					REX::DEBUG("Gamepad MCM Hotkeys: [DIAG] slot {} pressed - rawSlotMode={} effectiveMode={} globalMode={} blocked={} (menuCtx={} promptActive={} commandMode={} nonGameplayCtx={} quickloot={} dialogue={})",
						i + 1, slot.blockMode, static_cast<std::int32_t>(mode), static_cast<std::int32_t>(Settings::blockMode),
						blocked, a_state.anyMenuContext, a_state.promptActive, a_state.commandMode,
						a_state.anyNonGameplayContext, a_state.quickloot, a_state.dialogueOpen);
				}

				if (blocked && !s_injectedDown[i]) {
					continue;
				}

				const auto gamepadMod = GamepadDropdownToKeycode(slot.gamepadModifier);
				if (gamepadMod > 0 && !s_modHeld[i] && !s_injectedDown[i]) {
					continue;
				}

				const auto vkey = VirtualKey::FromDropdownIndex(slot.virtualKey);
				if (!vkey) {
					continue;  // Simulated Key set to None - slot configured but nothing to inject
				}

				// Mirrors the gamepad button's own press and release timing rather than sending a single tap,
				// so a target mod's hold gesture works. SendInput needs only the two edges.
				if (a_event.QJustPressed() && !blocked) {
					VirtualKey::Send(*vkey, true);
					s_injectedDown[i] = true;
				} else if (RE::QReleased(a_event) && s_injectedDown[i]) {
					VirtualKey::Send(*vkey, false);
					s_injectedDown[i] = false;
				}
			}
		}

		// The dropdown's ModSettingInt value is the raw 0-based option index into config.json's
		// "options" array, not an engine keycode. That 17-entry list (None plus 16 buttons) is in
		// F4SE::InputMap's own button-offset enum order, so this is offset + index - 1.
		[[nodiscard]] static std::uint32_t GamepadDropdownToKeycode(std::int32_t a_dropdownValue)
		{
			if (a_dropdownValue <= 0 || a_dropdownValue > 16) {
				return 0;  // "None", or out of range
			}
			return static_cast<std::uint32_t>(F4SE::InputMap::kMacro_GamepadOffset) + static_cast<std::uint32_t>(a_dropdownValue) - 1;
		}

		static inline std::array<bool, Settings::kSlotCount> s_modHeld{};

		// Whether a key-down went out via SendInput and its matching key-up has not. Drives the
		// stranded-injection release above; the OS keyboard state cannot be polled back.
		static inline std::array<bool, Settings::kSlotCount> s_injectedDown{};

		// Empty initially, so the first frame logs a baseline and "no output" stays distinguishable
		// from "not running".
		static inline std::string s_lastDiagLine{};

		using OriginalFunc = void(RE::BSInputEventReceiver*, const RE::InputEvent*);
		static inline REL::Relocation<OriginalFunc*> _original;
	};
}
