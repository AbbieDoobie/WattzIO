#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ContextualRemap.h"
#include "Keybinds.h"
#include "MenuContext.h"
#include "QuickTurn.h"
#include "Settings.h"

namespace QT
{
	// === F4RD RELOCATIONS ========================================================
	//   Kind   Site                                          ID / RVA     +Off
	//   vtbl   RE::VTABLE::PlayerCamera[1]                   F4RD            -
	//   vfunc  BSInputEventReceiver::PerformInputProcessing  slot 0          -
	//   vtbl   RE::VTABLE::PlayerControls[0]                 F4RD            -
	//   vfunc  BSInputEventReceiver::PerformInputProcessing  slot 0          -
	// =============================================================================

	// Hooks PlayerCamera's BSInputEventReceiver (VTABLE[1] slot 0) to observe movement and
	// stick events and advance the in-progress Quick Turn each frame. Does not modify the
	// event queue - it is called before PlayerControls and only reads events.
	class PlayerCameraHook
	{
	public:
		static void Install()
		{
			// RE::VTABLE::PlayerCamera, not RE::PlayerCamera::VTABLE: the in-class array holds
			// one element, so [1] reads out of bounds and resolves a garbage ID.
			// F4RD:vtbl - [1] = BSInputEventReceiver (offset 0x038)
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::PlayerCamera[1] };
			// F4RD:vfunc - slot 0 = PerformInputProcessing
			_original = vtbl.write_vfunc(0, &PlayerCameraHook::PerformInputProcessing);
			REX::INFO("Quick Turn: camera hook installed"sv);
		}

	private:
		static void PerformInputProcessing(RE::BSInputEventReceiver* a_this, const RE::InputEvent* a_queueHead)
		{
			for (auto event = a_queueHead; event; event = event->next) {
				const auto mutableEvent = const_cast<RE::InputEvent*>(event);
				CameraTrace::CountCameraEvent(*mutableEvent);

				if (const auto button = mutableEvent->As<RE::ButtonEvent>(); button) {
					if (button->device.get() == RE::INPUT_DEVICE::kKeyboard) {
						if (const auto keycode = static_cast<std::uint32_t>(button->QIDCode()); keycode < 256) {
							QuickTurn::UpdateKeyboardMovementState(keycode, RE::QPressed(*button));
						}
					}
				} else if (const auto thumb = mutableEvent->As<RE::ThumbstickEvent>(); thumb) {
					if (thumb->device.get() == RE::INPUT_DEVICE::kGamepad &&
						thumb->QIDCode() == static_cast<std::uint32_t>(RE::ThumbstickEvent::THUMBSTICK_ID::kLeft)) {
						QuickTurn::UpdateGamepadStickState(thumb->xValue, thumb->yValue);
					}
				}
			}

			CameraTrace::PreUpdate();
			QuickTurn::Update();
			CameraTrace::PostUpdate(QuickTurn::IsTurning());
			CameraTrace::Heartbeat(ContextualRemap::IsSwallowLatched());

			_original(a_this, a_queueHead);
		}

		using OriginalFunc = void(RE::BSInputEventReceiver*, const RE::InputEvent*);
		static inline REL::Relocation<OriginalFunc*> _original;
	};

	// Hooks PlayerControls' BSInputEventReceiver (VTABLE[0] slot 0) to filter the event
	// queue before handlers see it. Events claimed by Shared Action Input are left out of the
	// chain PlayerControls is handed, so no handler (SprintHandler, SneakHandler, etc.) fires
	// for them; the engine's own links are restored as soon as PlayerControls returns.
	// ControlMap is never modified - the binding stays live in the Controls menu.
	class PlayerControlsHook
	{
	public:
		static void Install()
		{
			// [0] = BSInputEventReceiver, PlayerControls' primary base at offset 0x000.
			// IDs_VTABLE.h reusing this id for VTABLE::IMovementPlayerControls is a generator
			// quirk, not a sign the entry is stale.
			// F4RD:vtbl - [0] = BSInputEventReceiver, PlayerControls' primary base
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::PlayerControls[0] };
			// F4RD:vfunc - slot 0 = PerformInputProcessing
			_original = vtbl.write_vfunc(0, &PlayerControlsHook::PerformInputProcessing);
			REX::INFO("Quick Turn: controls hook installed"sv);
		}

		// The gamepad-modifier-held flag only updates on a raw ButtonEvent for that button, and there
		// is no live "is this button down" poll, so a release swallowed while a menu had input focus
		// would leave it stuck true. Called from SettingsReload.h on any menu opening. Forgetting a
		// still-held modifier costs one release and re-press; a stuck flag firing a turn does not.
		static void ResetGamepadModifierState()
		{
			s_gamepadModifierHeld = false;
		}

	private:
		static void PerformInputProcessing(RE::BSInputEventReceiver* a_this, const RE::InputEvent* a_queueHead)
		{
			// Captured once per frame, not per event - a non-pausing menu does not change mid-frame.
			// Only suppresses Quick Turn's own steal/trigger decisions below; every event still
			// reaches _original() unmodified regardless. See MenuContext.h.
			const auto state = MenuContext::Capture();

			// Off unless Settings::debugLog is set by hand. Change-detected, so even when on it is a
			// handful of lines per session rather than hundreds per second.
			if (Settings::debugLog) {
				if (auto line = MenuContext::BuildDiagnosticLine(state); line != s_lastDiagLine) {
					REX::DEBUG("Quick Turn: [DIAG] {}", line);
					s_lastDiagLine = std::move(line);
				}
			}

			const bool blockHotkeys = MenuContext::ShouldBlock(state, Settings::blockMode);

			// Pass 1: decide per event, touching no links. Each entry keeps the event's original
			// `next` so pass 3 can put it back.
			auto& seen = s_seen;
			seen.clear();
			bool anySwallowed = false;
			for (auto cur = const_cast<RE::InputEvent*>(a_queueHead); cur; cur = cur->next) {
				bool swallow = false;
				if (const auto button = cur->As<RE::ButtonEvent>(); button) {
					if (const auto keycode = ToUnifiedKeycode(*button); keycode) {
						TrackGamepadModifier(*keycode, *button);
						swallow = ContextualRemap::HandleButtonEvent(*keycode, *button, blockHotkeys);
						if (!swallow) {
							HandleDirectInput(*keycode, *button, blockHotkeys);
						}
					}
				}
				CameraTrace::CountControlsEvent(swallow);
				anySwallowed |= swallow;
				seen.push_back({ cur, cur->next, swallow });
			}

			if (!anySwallowed) {
				_original(a_this, a_queueHead);
				return;
			}

			// Pass 2: link a filtered view that skips the swallowed events.
			//
			// These are the engine's own event objects, and its queue (0x143394B20 on 1.11.221) is a
			// PERSISTENT list: events are appended at a tail pointer and receivers read by sequence
			// watermark (dispatcher FUN_1416737c0, append FUN_1416742f0). Leaving the view linked
			// orphans the queue's tail whenever a swallowed event was last - every later event, from
			// every device, is then appended after an unreachable node and all input dies for good.
			// The dispatcher holds the queue lock across this call, so nothing can append between
			// the relink and the restore below.
			const RE::InputEvent* filteredHead = nullptr;
			RE::InputEvent*       prevKept     = nullptr;
			for (const auto& entry : seen) {
				if (entry.swallowed) {
					continue;
				}
				if (prevKept) {
					prevKept->next = entry.event;
				} else {
					filteredHead = entry.event;
				}
				prevKept = entry.event;
			}
			if (prevKept) {
				prevKept->next = nullptr;
			}

			_original(a_this, filteredHead);

			// Pass 3: restore every link exactly as the engine left it.
			for (const auto& entry : seen) {
				entry.event->next = entry.originalNext;
			}
		}

		struct SeenEvent
		{
			RE::InputEvent* event;
			RE::InputEvent* originalNext;
			bool            swallowed;
		};
		// Reused every frame to avoid a per-frame allocation. Only ever touched on the main thread,
		// from inside the dispatcher's queue lock.
		static inline std::vector<SeenEvent> s_seen;

		// Handles Direct Input Quick Turn hotkeys (keyboard MCM keybind and gamepad dropdown).
		// a_blockHotkeys suppresses triggering while a menu context is active - see
		// PerformInputProcessing. This path has no vanilla handler behind it either way, so
		// there is no forwarding behavior to preserve here.
		static void HandleDirectInput(std::uint32_t a_keycode, RE::ButtonEvent& a_event, bool a_blockHotkeys)
		{
			if (!a_event.QJustPressed() || a_blockHotkeys) {
				return;
			}

			// MCM dropdown stores a selection index (0-16), not a raw keycode.
			const auto gamepadKey = static_cast<std::uint32_t>(Settings::GamepadDropdownIndexToKeycode(Settings::iQuickTurnGamepadKey));
			const auto gamepadMod = static_cast<std::uint32_t>(Settings::GamepadDropdownIndexToKeycode(Settings::iQuickTurnGamepadModifier));
			const auto keybind    = Keybinds::Get("QuickTurnKeyboardHotkey"sv);

			// An unset modifier ("None") means no modifier is required.
			const bool isGamepad  = gamepadKey > 0 && a_keycode == gamepadKey && (gamepadMod == 0 || s_gamepadModifierHeld);
			const bool isKeyboard = keybind && a_keycode == static_cast<std::uint32_t>(keybind->keycode) &&
			                        KeyboardModifiersSatisfied(keybind->modifiers);

			if (isGamepad || isKeyboard) {
				QuickTurn::Trigger();
			}
		}

		// Tracked on every event, ahead of Shared Action and the menu block, so the held state stays
		// right even when the modifier is itself a Shared Action button or is pressed inside a menu
		// context. Only read here - never acted on, never swallowed.
		static void TrackGamepadModifier(std::uint32_t a_keycode, const RE::ButtonEvent& a_event)
		{
			const auto gamepadMod = static_cast<std::uint32_t>(Settings::GamepadDropdownIndexToKeycode(Settings::iQuickTurnGamepadModifier));
			if (gamepadMod != 0 && a_keycode == gamepadMod) {
				s_gamepadModifierHeld = RE::QPressed(a_event);
			}
		}

		// Keyboard modifier support ("allowModifierKeys": true on the hotkey widget). Held state comes
		// from GetAsyncKeyState rather than tracked ButtonEvents, which would first need to establish
		// whether a keyboard idCode is a scan code or a virtual-key code, and is only consulted when
		// the hotkey's own key event arrives, so the game has focus. Exact match: a Shift+Q binding
		// does not fire on bare Q, and a bare Q binding does not fire on Shift+Q. MCM does not
		// document the bitmask below; a wrong assignment fails safe, a modified binding never matching.
		static constexpr std::int32_t kModShift = 1 << 0;
		static constexpr std::int32_t kModCtrl = 1 << 1;
		static constexpr std::int32_t kModAlt = 1 << 2;

		[[nodiscard]] static bool KeyboardModifiersSatisfied(std::int32_t a_modifiers)
		{
			const auto down = [](int a_vk) { return (::GetAsyncKeyState(a_vk) & 0x8000) != 0; };

			return ((a_modifiers & kModShift) != 0) == down(VK_SHIFT) &&
			       ((a_modifiers & kModCtrl) != 0) == down(VK_CONTROL) &&
			       ((a_modifiers & kModAlt) != 0) == down(VK_MENU);
		}

		// Converts a ButtonEvent's device-relative idCode to unified 0-281 numbering.
		[[nodiscard]] static std::optional<std::uint32_t> ToUnifiedKeycode(const RE::ButtonEvent& a_event)
		{
			switch (a_event.device.get()) {
			case RE::INPUT_DEVICE::kKeyboard:
				return static_cast<std::uint32_t>(a_event.QIDCode());
			case RE::INPUT_DEVICE::kMouse:
				// Scroll wheel uses sentinel values rather than sequential button indices.
				if (a_event.QIDCode() == static_cast<std::int32_t>(RE::kBSButtonCodeWheelUp)) {
					return static_cast<std::uint32_t>(F4SE::InputMap::kMacro_MouseWheelOffset);
				} else if (a_event.QIDCode() == static_cast<std::int32_t>(RE::kBSButtonCodeWheelDown)) {
					return static_cast<std::uint32_t>(F4SE::InputMap::kMacro_MouseWheelOffset) + 1;
				} else {
					return F4SE::InputMap::kMacro_MouseButtonOffset + static_cast<std::uint32_t>(a_event.QIDCode());
				}
			case RE::INPUT_DEVICE::kGamepad:
				return F4SE::InputMap::GamepadMaskToKeycode(static_cast<std::uint32_t>(a_event.QIDCode()));
			default:
				return std::nullopt;
			}
		}

		static inline bool s_gamepadModifierHeld = false;

		// Previous rendered diagnostic line, for the change-detected trace above.
		static inline std::string s_lastDiagLine;

		using OriginalFunc = void(RE::BSInputEventReceiver*, const RE::InputEvent*);
		static inline REL::Relocation<OriginalFunc*> _original;
	};
}
