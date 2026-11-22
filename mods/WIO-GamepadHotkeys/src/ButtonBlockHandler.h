#pragma once

// === F4RD RELOCATIONS ========================================================
// This file resolves no addresses.
// =============================================================================

#include <algorithm>
#include <cstdint>
#include <vector>

#include "ButtonBlock.h"
#include "InputHook.h"
#include "MenuContext.h"
#include "Settings.h"

// Drives ButtonBlock: stops a slot's gamepad button doing its normal action while that slot's
// hotkey fires.
//
// It has to sit at index 0 of RE::PlayerControls::handlers, ahead of every vanilla handler, so the
// `disabled` flag is on the event before Jump, Sneak, and the rest read it. InputHook's camera
// receiver reads buttons by idCode, which the flag leaves alone, so the hotkey itself still fires.
namespace GMH::ButtonBlockHandler
{
	class Handler :
		public RE::PlayerInputHandler
	{
	public:
		[[nodiscard]] static Handler* GetSingleton()
		{
			// PlayerInputHandler holds a PlayerControlsData&, so this cannot be built before
			// PlayerControls exists. First use is Install() at kGameLoaded.
			static Handler singleton{ RE::PlayerControls::GetSingleton()->data };
			return std::addressof(singleton);
		}

		// Installed at kGameLoaded, and again on every PauseMenu close: another plugin registering
		// later sits ahead of this handler, and index 0 is what makes blocking work.
		static void Install()
		{
			const auto controls = RE::PlayerControls::GetSingleton();
			if (!controls) {
				REX::WARN("Gamepad MCM Hotkeys: PlayerControls unavailable - button block handler not installed"sv);
				return;
			}

			const auto self = static_cast<RE::PlayerInputHandler*>(GetSingleton());
			auto&      handlers = controls->handlers;
			if (std::find(handlers.begin(), handlers.end(), self) == handlers.end()) {
				controls->RegisterHandler(self);
			}

			// RegisterHandler only appends. Rotate to index 0, keeping everyone else's order.
			for (std::uint32_t i = 0; i < handlers.size(); ++i) {
				if (handlers[i] != self) {
					continue;
				}
				for (; i > 0; --i) {
					handlers[i] = handlers[i - 1];
				}
				handlers[0] = self;
				break;
			}

			if (!s_announced) {
				REX::INFO("Gamepad MCM Hotkeys: button block handler at PlayerControls index 0 of {}"sv, handlers.size());
				s_announced = true;
			}
		}

		// A release that lands while a menu has input focus never reaches PlayerControls, so held
		// state and claims are dropped whenever menu mode starts.
		static void Reset()
		{
			s_held.clear();
			ButtonBlock::Reset();
		}

		bool ShouldHandleEvent(const RE::InputEvent* a_event) override
		{
			// PlayerControls calls this once per event from its per-event loop, and a second time
			// for the held-state pass, so each event is de-duplicated by pointer and timeCode.
			const auto button = a_event ? const_cast<RE::InputEvent*>(a_event)->As<RE::ButtonEvent>() : nullptr;
			if (!button || button->device.get() != RE::INPUT_DEVICE::kGamepad) {
				return false;
			}
			if (button == s_lastEvent && button->timeCode == s_lastTimeCode) {
				return false;
			}
			s_lastEvent = button;
			s_lastTimeCode = button->timeCode;

			Process(*button);
			return false;  // pure observer - never consume
		}

	private:
		explicit Handler(RE::PlayerControlsData& a_data) noexcept :
			RE::PlayerInputHandler(a_data)
		{}

		Handler(const Handler&) = delete;
		Handler& operator=(const Handler&) = delete;

		static void Process(RE::ButtonEvent& a_event)
		{
			const auto idCode = static_cast<std::uint32_t>(a_event.idCode);
			const auto keycode = F4SE::InputMap::GamepadMaskToKeycode(idCode);

			if (a_event.value == 0.0F) {
				std::erase(s_held, keycode);
			} else if (std::find(s_held.begin(), s_held.end(), keycode) == s_held.end()) {
				s_held.push_back(keycode);
			}

			if (a_event.QJustPressed()) {
				ClaimOnPress(idCode, keycode);
			}

			ButtonBlock::Apply(a_event);
		}

		static void ClaimOnPress(std::uint32_t a_idCode, std::uint32_t a_keycode)
		{
			const auto state = MenuContext::Capture();

			for (int i = 0; i < Settings::kSlotCount; ++i) {
				const auto& slot = Settings::slots[i];
				const auto  trigger = InputHook::GamepadDropdownToKeycode(slot.gamepadKey);
				if (trigger == 0 || !VirtualKey::FromDropdownIndex(slot.virtualKey)) {
					continue;  // slot not set up, so nothing fires and nothing is blocked
				}

				const auto blockMode = Settings::EffectiveButtonBlockMode(i);
				if (blockMode == ButtonBlock::Mode::kOff ||
					MenuContext::ShouldBlock(state, Settings::EffectiveBlockMode(i))) {
					continue;  // the hotkey would not fire here, so the button keeps its action
				}

				const auto modifier = InputHook::GamepadDropdownToKeycode(slot.gamepadModifier);

				// A modifier set to Main and Modifier is blocked from its own press through its
				// release, whether or not the main button follows.
				if (modifier != 0 && modifier == a_keycode && blockMode == ButtonBlock::Mode::kMainAndModifier) {
					ButtonBlock::Claim(a_idCode);
				}

				// The main button is blocked when this press fires the hotkey: no modifier set, or
				// the modifier already held.
				if (trigger == a_keycode &&
					(modifier == 0 || std::find(s_held.begin(), s_held.end(), modifier) != s_held.end())) {
					ButtonBlock::Claim(a_idCode);
				}
			}
		}

		inline static std::vector<std::uint32_t> s_held;
		inline static RE::ButtonEvent*           s_lastEvent{ nullptr };
		inline static std::uint32_t              s_lastTimeCode{ 0 };
		inline static bool                       s_announced{ false };
	};
}
