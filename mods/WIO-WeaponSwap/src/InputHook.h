#pragma once

#include <array>
#include <bit>
#include <chrono>
#include <optional>
#include <string>

#include "InputLabels.h"
#include "Keybinds.h"
#include "MenuContext.h"
#include "Settings.h"
#include "WeaponSwapLogic.h"

// Raw BSInputEventReceiver vtable hook on PlayerCamera. This mod has no vanilla control to
// piggyback on, so its own gamepad and keyboard bindings are matched directly against every
// incoming button event.
namespace WS
{
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
			RefreshGamepadKeycode();
			REX::INFO("Weapon Swap Button: BSInputEventReceiver hook installed on PlayerCamera"sv);
		}

		// Re-resolves the gamepad dropdown -> keycode conversions, at Install() and again on every
		// settings reload, since the dropdowns can change mid-session. The keyboard hotkeys need no
		// equivalent cache - Keybinds::Load() refreshes its own optionals, read fresh per event.
		static void RefreshGamepadKeycode()
		{
			s_gamepadKeycode = static_cast<std::uint32_t>(InputLabels::GamepadDropdownIndexToKeycode(Settings::iGamepadInput));
			s_gamepadNextKeycode = static_cast<std::uint32_t>(InputLabels::GamepadDropdownIndexToKeycode(Settings::iGamepadNextFavorite));
			s_gamepadPreviousKeycode = static_cast<std::uint32_t>(InputLabels::GamepadDropdownIndexToKeycode(Settings::iGamepadPreviousFavorite));
			s_gamepadModifierKeycode = static_cast<std::uint32_t>(InputLabels::GamepadDropdownIndexToKeycode(Settings::iGamepadModifier));
		}

		// The gamepad-modifier-held flag only updates on a raw ButtonEvent for that button, and there
		// is no live "is this button down" poll, so a release swallowed while a menu had input focus
		// would leave it stuck true. Called from SettingsReload.h on any menu opening. Forgetting a
		// still-held modifier costs one release and re-press; a stuck flag firing a swap does not.
		static void ResetGamepadModifierState()
		{
			s_gamepadModifierHeld = false;
		}

	private:
		static void PerformInputProcessing(RE::BSInputEventReceiver* a_this, const RE::InputEvent* a_queueHead)
		{
			// Dialogue and the vanilla quickloot prompt do not set RE::UI::menuMode, which counts
			// only true pausing menus, and a full-screen menu never reaches this hook at all, so
			// non-pausing menus need their own check. Captured once per frame - see MenuContext.h.
			const auto state = MenuContext::Capture();

			// Off unless Settings::debugLog is set by hand. Change-detected, so even when on it is a
			// handful of lines per session rather than hundreds per second.
			if (Settings::debugLog) {
				if (auto line = MenuContext::BuildDiagnosticLine(state); line != s_lastDiagLine) {
					REX::DEBUG("Weapon Swap Button: [DIAG] {}", line);
					s_lastDiagLine = std::move(line);
				}
			}

			const bool blockHotkeys = MenuContext::ShouldBlock(state, Settings::blockMode);

			for (auto event = a_queueHead; event; event = event->next) {
				// const_cast is safe here: real, engine-owned mutable input-queue memory, only
				// exposed as const by this vfunc's own signature.
				if (const auto button = const_cast<RE::InputEvent*>(event)->As<RE::ButtonEvent>(); button) {
					if (const auto keycode = ToUnifiedKeycode(*button); keycode) {
						// Ahead of the menu block, so the held state stays right when the modifier is
						// pressed or released inside a menu context.
						TrackGamepadModifier(*keycode, *button);
						if (blockHotkeys) {
							continue;
						}
						switch (ResolveBinding(*keycode, *button)) {
						case Binding::kWeaponSwap:
							HandleEvent(*button);
							break;
						case Binding::kNextFavorite:
							HandleCycleEvent(*button, true);
							break;
						case Binding::kPreviousFavorite:
							HandleCycleEvent(*button, false);
							break;
						case Binding::kNone:
							HandleWheelEvent(*keycode, *button);
							break;
						}
					}
				}
			}
			_original(a_this, a_queueHead);
		}

		static void HandleEvent(RE::ButtonEvent& a_event)
		{
			if (!RE::QReleased(a_event)) {
				return;  // equipping occurs on release only, for every swap type
			}

			// Hold (Slot 3) is checked first and short-circuits. A hold is a different gesture from
			// a tap, so it also clears any tap chain in progress rather than counting toward one:
			// tap, tap, hold gives slot 3 and starts over, never slot 4.
			if (Settings::bHoldSlot3 && a_event.heldDownSecs >= Settings::HoldThresholdSeconds()) {
				s_tapCount = 0;
				WeaponSwapLogic::TriggerSlot(a_event, 2);
				return;
			}

			// Triple Tap (Slot 4). The window is measured from the first tap of the chain, so all three
			// must land inside it, and a late tap becomes tap 1 of a fresh chain. Taps 1 and 2 still
			// perform their normal swap, so slot 4 costs two ordinary swaps on the way.
			if (Settings::bTripleTapSlot4) {
				const auto now = std::chrono::steady_clock::now();
				const auto sinceFirst = std::chrono::duration<float>(now - s_firstTapTime).count();
				if (s_tapCount == 0 || sinceFirst > Settings::TripleTapWindowSeconds()) {
					s_tapCount = 1;
					s_firstTapTime = now;
				} else if (++s_tapCount >= 3) {
					s_tapCount = 0;
					WeaponSwapLogic::TriggerSlot(a_event, 3);
					return;
				}
			} else {
				s_tapCount = 0;
			}

			WeaponSwapLogic::Trigger(a_event);
		}

		// Tap-chain state for Triple Tap (Slot 4). steady_clock rather than an engine timer, because
		// this is a real-time input gesture and the hotkey is already gated out entirely while a menu
		// owns input, so no paused-game window can leave a stale chain accumulating.
		static inline std::int32_t s_tapCount = 0;
		static inline std::chrono::steady_clock::time_point s_firstTapTime{};

		// Next / Previous Favorite Slot: a plain one-step cycle on release. Deliberately none of the
		// swap button's gestures - no hold, no tap chain, and no effect on the swap button's chain.
		static void HandleCycleEvent(RE::ButtonEvent& a_event, bool a_forward)
		{
			if (!RE::QReleased(a_event)) {
				return;
			}
			TriggerCycle(a_event, a_forward);
		}

		// Mouse Wheel Favorite Slot Cycle: the wheel as Next / Previous Favorite Slot. Fires on the
		// notch itself rather than on release - QReleased needs heldDownSecs > 0, which a wheel notch
		// is not expected to have. The bDebugLog trace below shows what actually arrives.
		static void HandleWheelEvent(std::uint32_t a_keycode, RE::ButtonEvent& a_event)
		{
			const auto wheelUp = static_cast<std::uint32_t>(F4SE::InputMap::kMacro_MouseWheelOffset);
			if (Settings::wheelCycle == Settings::WheelCycle::kOff || (a_keycode != wheelUp && a_keycode != wheelUp + 1)) {
				return;
			}
			if (Settings::debugLog) {
				REX::DEBUG("Weapon Swap Button: [WHEEL] {} value={} heldDownSecs={}"sv,
					a_keycode == wheelUp ? "up"sv : "down"sv, a_event.value, a_event.heldDownSecs);
			}
			if (!a_event.QJustPressed()) {
				return;
			}
			TriggerCycle(a_event, (a_keycode == wheelUp) == (Settings::wheelCycle == Settings::WheelCycle::kOn));
		}

		// Favorite Slot Next/Previous Pause Time. One shared clock for Next and Previous on every
		// device, measured from the last change that fired; a press inside it is dropped, not queued.
		static void TriggerCycle(RE::ButtonEvent& a_event, bool a_forward)
		{
			const auto now = std::chrono::steady_clock::now();
			if (std::chrono::duration<float>(now - s_lastCycleTime).count() < Settings::fCycleCooldown) {
				return;
			}
			s_lastCycleTime = now;
			WeaponSwapLogic::TriggerCycle(a_event, a_forward);
		}

		// steady_clock's epoch is system boot, so the default is always further back than the 1s cap.
		static inline std::chrono::steady_clock::time_point s_lastCycleTime{};

		enum class Binding
		{
			kNone,
			kWeaponSwap,
			kNextFavorite,
			kPreviousFavorite
		};

		// Every action fires on release, but modifiers are judged on the press: the binding is
		// resolved once when the key goes down and replayed for its held and release events, so
		// letting go of Shift a moment before X still counts as Shift+X. The latch is cleared on
		// release, so a press this hook never saw (swallowed by a menu block) cannot fire later.
		[[nodiscard]] static Binding ResolveBinding(std::uint32_t a_keycode, const RE::ButtonEvent& a_event)
		{
			if (a_keycode >= s_latched.size()) {
				return Binding::kNone;
			}
			auto& latched = s_latched[a_keycode];
			if (a_event.QJustPressed()) {
				latched = MatchKeycode(a_keycode);
			}
			const auto binding = latched;
			if (RE::QReleased(a_event)) {
				latched = Binding::kNone;
			}
			return binding;
		}

		// One key bound to several actions fires only one of them, never two equips in a frame.
		// A keyboard binding with modifiers is more specific than one without, so it is tried
		// first and the most modifiers wins: X on swap and Shift+X on Next both work. Past that,
		// first match wins in swap, Next, Previous order.
		[[nodiscard]] static Binding MatchKeycode(std::uint32_t a_keycode)
		{
			static constexpr std::array kOrder{ Binding::kWeaponSwap, Binding::kNextFavorite, Binding::kPreviousFavorite };

			auto best = Binding::kNone;
			int  bestCount = 0;
			for (const auto binding : kOrder) {
				const auto& keybind = KeyboardBindingFor(binding);
				if (keybind && keybind->modifiers != 0 && static_cast<std::uint32_t>(keybind->keycode) == a_keycode &&
					ModifiersHeld(keybind->modifiers)) {
					if (const auto count = std::popcount(static_cast<std::uint32_t>(keybind->modifiers)); count > bestCount) {
						best = binding;
						bestCount = count;
					}
				}
			}
			if (best != Binding::kNone) {
				return best;
			}

			for (const auto binding : kOrder) {
				if (const auto gamepad = GamepadKeycodeFor(binding); gamepad != 0 && gamepad == a_keycode && GamepadModifierSatisfied()) {
					return binding;
				}
				const auto& keybind = KeyboardBindingFor(binding);
				if (keybind && keybind->modifiers == 0 && static_cast<std::uint32_t>(keybind->keycode) == a_keycode) {
					return binding;
				}
			}
			return Binding::kNone;
		}

		// Keyboard modifier support ("allowModifierKeys": true on the hotkey widgets). Held state
		// comes from GetAsyncKeyState, consulted only when the hotkey's own key event arrives, so the
		// game has focus.
		//
		// A superset match, not an exact one: the bound modifiers must be held, but extra ones may
		// be too, and a binding with no modifiers ignores them entirely. Shift is
		// sprint by default, and an exact match would make a bare-key swap dead while sprinting.
		// MatchKeycode's most-modifiers-wins ordering is what keeps X and Shift+X distinct.
		//
		// MCM does not document the bitmask. Any bit outside the three known ones fails safe: that
		// binding never matches, rather than silently degrading into a bare-key binding.
		static constexpr std::int32_t kModShift = 1 << 0;
		static constexpr std::int32_t kModCtrl = 1 << 1;
		static constexpr std::int32_t kModAlt = 1 << 2;
		static constexpr std::int32_t kModAll = kModShift | kModCtrl | kModAlt;

		[[nodiscard]] static bool ModifiersHeld(std::int32_t a_modifiers)
		{
			if ((a_modifiers & ~kModAll) != 0) {
				return false;
			}
			const auto down = [](int a_vk) { return (::GetAsyncKeyState(a_vk) & 0x8000) != 0; };

			return ((a_modifiers & kModShift) == 0 || down(VK_SHIFT)) &&
			       ((a_modifiers & kModCtrl) == 0 || down(VK_CONTROL)) &&
			       ((a_modifiers & kModAlt) == 0 || down(VK_MENU));
		}

		[[nodiscard]] static const std::optional<Keybinds::Entry>& KeyboardBindingFor(Binding a_binding)
		{
			static const std::optional<Keybinds::Entry> kUnbound{};
			switch (a_binding) {
			case Binding::kWeaponSwap:
				return Keybinds::WeaponSwapKeyboard();
			case Binding::kNextFavorite:
				return Keybinds::NextFavoriteKeyboard();
			case Binding::kPreviousFavorite:
				return Keybinds::PreviousFavoriteKeyboard();
			default:
				return kUnbound;
			}
		}

		[[nodiscard]] static std::uint32_t GamepadKeycodeFor(Binding a_binding)
		{
			switch (a_binding) {
			case Binding::kWeaponSwap:
				return s_gamepadKeycode;
			case Binding::kNextFavorite:
				return s_gamepadNextKeycode;
			case Binding::kPreviousFavorite:
				return s_gamepadPreviousKeycode;
			default:
				return 0;
			}
		}

		static inline std::uint32_t s_gamepadKeycode = 0;
		static inline std::uint32_t s_gamepadNextKeycode = 0;
		static inline std::uint32_t s_gamepadPreviousKeycode = 0;
		static inline std::uint32_t s_gamepadModifierKeycode = 0;
		static inline bool          s_gamepadModifierHeld = false;

		// Tracked on every event, ahead of the menu block. Only read here - never acted on, never
		// swallowed, so the modifier button keeps its vanilla action.
		static void TrackGamepadModifier(std::uint32_t a_keycode, const RE::ButtonEvent& a_event)
		{
			if (s_gamepadModifierKeycode != 0 && a_keycode == s_gamepadModifierKeycode) {
				s_gamepadModifierHeld = RE::QPressed(a_event);
			}
		}

		// An unset modifier ("None") means no modifier is required.
		[[nodiscard]] static bool GamepadModifierSatisfied()
		{
			return s_gamepadModifierKeycode == 0 || s_gamepadModifierHeld;
		}

		// Binding latched on each key's press, indexed by unified keycode (0-281). See ResolveBinding.
		static inline std::array<Binding, 282> s_latched{};

		// Converts a ButtonEvent's device-relative idCode into F4SE::InputMap's unified 0-281
		// numbering.
		[[nodiscard]] static std::optional<std::uint32_t> ToUnifiedKeycode(const RE::ButtonEvent& a_event)
		{
			switch (a_event.device.get()) {
			case RE::INPUT_DEVICE::kKeyboard:
				return static_cast<std::uint32_t>(a_event.QIDCode());
			case RE::INPUT_DEVICE::kMouse:
				// MCM's "hotkey" widget can capture a mouse button or wheel too, even though this mod's
				// own settings text calls it a keyboard hotkey, so a mouse-bound hotkey still works.
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

		// Previous rendered diagnostic line, for the change-detected trace above.
		static inline std::string s_lastDiagLine;

		using OriginalFunc = void(RE::BSInputEventReceiver*, const RE::InputEvent*);
		static inline REL::Relocation<OriginalFunc*> _original;
	};
}
