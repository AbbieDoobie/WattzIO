#pragma once

#include "OrderExitDispatcher.h"

namespace PipboyPipbindFix
{
	// A pure-observer RE::PlayerInputHandler at index 0 of RE::PlayerControls::handlers, driving
	// the Companion Order Mode exit (OrderExitDispatcher).
	//
	// MenuControls is the wrong place for it: a button bound to a consuming gameplay action (Sneak,
	// or Aim with a weapon drawn) is marked handled == kStop by that action's handler, and
	// MenuControls::ProcessEvent skips its whole array for such an event, so a MenuControls-registered
	// handler never sees it. Index 0 here is also the right place to swallow, landing before Jump,
	// Sneak, ReadyWeapon, and the rest of the array.
	class GameplayInputWatcher :
		public RE::PlayerInputHandler
	{
	public:
		[[nodiscard]] static GameplayInputWatcher* GetSingleton()
		{
			// PlayerInputHandler holds a PlayerControlsData&, so this cannot be built before
			// PlayerControls exists. First use is Install() at kGameLoaded.
			static GameplayInputWatcher singleton{ RE::PlayerControls::GetSingleton()->data };
			return std::addressof(singleton);
		}

		static void Install()
		{
			const auto controls = RE::PlayerControls::GetSingleton();
			if (!controls) {
				REX::WARN("Pip-Boy Bindings Fix: PlayerControls unavailable - order-mode watcher not installed"sv);
				return;
			}

			controls->RegisterHandler(static_cast<RE::PlayerInputHandler*>(GetSingleton()));
			MoveToFrontOfHandlerChain(*controls);
		}

		bool ShouldHandleEvent(const RE::InputEvent* a_event) override
		{
			const bool active = OrderExitDispatcher::IsActive();
			if (!active) {
				OrderExitDispatcher::NotifyCommandModeEnded();
				return false;
			}

			for (auto event = a_event; event; event = event->next) {
				const auto button = const_cast<RE::InputEvent*>(event)->As<RE::ButtonEvent>();
				if (!button) {
					continue;
				}
				OrderExitDispatcher::OnGameplayButtonEvent(*button);
			}

			return false;
		}

	private:
		explicit GameplayInputWatcher(RE::PlayerControlsData& a_data) noexcept :
			RE::PlayerInputHandler(a_data)
		{}

		GameplayInputWatcher(const GameplayInputWatcher&) = delete;
		GameplayInputWatcher& operator=(const GameplayInputWatcher&) = delete;

		// Rotates this watcher to index 0 of RE::PlayerControls::handlers, keeping every other
		// handler's relative order. Safe because RegisterHandler only appends, the array carries no
		// position-dependent bookkeeping, and this handler never consumes an event. Being first is
		// what makes the swallow work: every PlayerControls handler matches on the user-event name,
		// and QUserEvent() returns "DISABLED" once `disabled` is set.
		static void MoveToFrontOfHandlerChain(RE::PlayerControls& a_controls)
		{
			auto&      handlers = a_controls.handlers;
			const auto self     = static_cast<RE::PlayerInputHandler*>(GetSingleton());

			for (std::uint32_t i = 0; i < handlers.size(); ++i) {
				if (handlers[i] != self) {
					continue;
				}
				for (; i > 0; --i) {
					handlers[i] = handlers[i - 1];
				}
				handlers[0] = self;
				return;
			}

			REX::WARN("Pip-Boy Bindings Fix: handler not found in PlayerControls::handlers - "
					  "order-mode exit will not see gameplay input"sv);
		}
	};
}
