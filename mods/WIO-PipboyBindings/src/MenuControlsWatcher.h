#pragma once

#include "ControlRemap.h"
#include "Keybinds.h"
#include "OrderExitDispatcher.h"
#include "PipboyWatcher.h"
#include "Settings.h"
#include "ZoomDispatcher.h"

namespace PipboyPipbindFix
{
	// A pure-observer RE::BSInputEventUser in RE::MenuControls::handlers. Walks the event chain once
	// per frame and drives Zoom (ZoomDispatcher), Close, and suppression of the vanilla order-mode
	// exit. Only that suppression lives here, because it has to beat PipboyHandler in this same array;
	// the rest of the order-mode feature is in GameplayInputWatcher.
	//
	// Close is issued through UIMessageQueue::kHide. The native Pipboy-toggle-close does not reliably
	// close the menu, so "Same as Pipboy Open" needs this detection too. A button configured for both
	// Close and Zoom resolves to Close, ending any zoom in progress first.
	class MenuControlsWatcher :
		public RE::BSInputEventUser
	{
	public:
		[[nodiscard]] static MenuControlsWatcher* GetSingleton()
		{
			static MenuControlsWatcher singleton;
			return std::addressof(singleton);
		}

		static void Install()
		{
			const auto controls = RE::MenuControls::GetSingleton();
			if (!controls) {
				REX::WARN("Pip-Boy Bindings Fix: MenuControls unavailable - watcher not installed"sv);
				return;
			}

			RE::RegisterMenuHandler(controls, GetSingleton());
			MoveToFrontOfHandlerChain(*controls);
		}

		bool ShouldHandleEvent(const RE::InputEvent* a_event) override
		{
			const bool pipboyOpen = PipboyWatcher::IsPipboyOpen();

			// Order mode is a gameplay state, not a menu, so it is checked independently of the Pipboy.
			// IsActive() also requires that no menu has input focus, or the configured button gets
			// swallowed inside menus opened on top of order mode.
			const bool commandMode = OrderExitDispatcher::IsActive();

			if (!pipboyOpen && !commandMode) {
				return false;
			}

			const auto closeTarget =
				pipboyOpen ? ResolveCloseTargetBitmask() : std::optional<std::int32_t>{};

			for (auto event = a_event; event; event = event->next) {
				const auto button = const_cast<RE::InputEvent*>(event)->As<RE::ButtonEvent>();
				if (!button) {
					continue;
				}

				if (pipboyOpen) {
					ZoomDispatcher::OnButtonEvent(*button);
					CheckForClosePress(*button, closeTarget);
				}
				if (commandMode) {
					OrderExitDispatcher::SuppressVanillaExit(*button);
				}
			}

			return false;
		}

	private:
		MenuControlsWatcher() = default;
		MenuControlsWatcher(const MenuControlsWatcher&) = delete;
		MenuControlsWatcher& operator=(const MenuControlsWatcher&) = delete;

		// Rotates this watcher to index 0 of RE::MenuControls::handlers, keeping every other handler's
		// relative order. ProcessEvent hands each handler the event objects themselves, so position
		// decides who mutates one first, and RegisterHandler only appends - behind the eight built-ins,
		// PipboyHandler last. The suppression has to set `disabled` before PipboyHandler reads the
		// event. Safe because the array carries no position-dependent bookkeeping and this handler
		// never consumes an event.
		static void MoveToFrontOfHandlerChain(RE::MenuControls& a_controls)
		{
			auto&      handlers = a_controls.handlers;
			const auto self     = static_cast<RE::BSInputEventUser*>(GetSingleton());

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

			REX::WARN("Pip-Boy Bindings Fix: handler not found in MenuControls::handlers - "
					  "vanilla order-mode exit suppression will not work"sv);
		}

		// nullopt when no custom gamepad close detection applies: dropdown 1 ("OFF"), or an
		// unresolvable live binding.
		[[nodiscard]] static std::optional<std::int32_t> ResolveCloseTargetBitmask()
		{
			const int closeGP = Settings::iCloseGamepadButton;

			if (closeGP == 0) {
				// "Same as Pipboy Open": read live rather than cached, so a rebind of
				// Open in the vanilla Controls menu is picked up mid-session.
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

			if (closeGP >= 2 && closeGP <= 17) {
				return ControlRemap::kCloseGPXInput[static_cast<std::size_t>(closeGP) - 2];
			}

			return std::nullopt;
		}

		static void CheckForClosePress(RE::ButtonEvent& a_button, std::optional<std::int32_t> a_targetBitmask)
		{
			if (!a_button.QJustPressed()) {
				return;
			}

			const bool gamepadMatch =
				a_targetBitmask &&
				a_button.device.get() == RE::INPUT_DEVICE::kGamepad &&
				static_cast<std::int32_t>(a_button.QIDCode()) == *a_targetBitmask;

			if (!gamepadMatch && !Keybinds::MatchesHotkey(a_button, Keybinds::iCloseKeyboardKeycode)) {
				return;
			}

			// Or examine mode stays latched with its input layer pushed and no menu to clear it.
			ZoomDispatcher::ReleaseIfHeld(a_button);

			if (const auto uimq = RE::UIMessageQueue::GetSingleton()) {
				static const RE::BSFixedString kPipboyMenu{ "PipboyMenu" };
				uimq->AddMessage(kPipboyMenu, RE::UI_MESSAGE_TYPE::kHide);
			}
		}
	};
}
