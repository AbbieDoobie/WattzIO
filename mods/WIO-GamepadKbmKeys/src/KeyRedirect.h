#pragma once

// === F4RD RELOCATIONS ========================================================
// kind  what                                          OG        NG        AE
// id    BSInputEnableManager user-event group check   243830    2268248   2268248
// data  BSInputEnableManager singleton (pointer var)  781703    2689007   4796297
//
// Both were derived from the ControlMap user-event resolver (OG 647956, AE
// 2268334), which calls the group check while resolving a key, and from
// MenuOpenHandler's and PipboyHandler::OnButtonEvent, which call it with their
// own group flags. The OG twins were matched by aligning those call sites: the
// flag values (0x8, 0x840, 0x100, 0x40, 0x10) appear in the same order in both
// images, and the argument is the same global at every site.
// =============================================================================

#include "ButtonBlock.h"
#include "Controls.h"
#include "Settings.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

// Delivers a keyboard-only user event from a gamepad button, by borrowing the button's real
// ButtonEvent, renaming it to the target event, and handing it to the vanilla handler that consumes
// that event. Every consumer of the fifteen target controls decides by event name alone, with no
// device test, so the renamed event behaves as the key does.
//
// Nothing is enqueued. The input queue's read lock is held across every receiver's
// PerformInputProcessing, and appending takes the write lock, so enqueuing from any receiver
// deadlocks.
namespace GKK::KeyRedirect
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
				REX::WARN("Gamepad KBM Keys: PlayerControls unavailable - handler not installed"sv);
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
				const auto enableFn = WIO::Reloc::Address(kGroupEnabledID);
				const auto manager = WIO::Reloc::Address(kEnableManagerID);
				REX::INFO("Gamepad KBM Keys: handler at PlayerControls index 0 of {}, group check {}, manager {}"sv,
					handlers.size(), enableFn ? "resolved"sv : "UNRESOLVED"sv, manager ? "resolved"sv : "UNRESOLVED"sv);
				s_announced = true;
			}
		}

		static void Reset()
		{
			s_held.clear();
			s_latches.clear();
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
		struct Latch
		{
			std::uint32_t idCode;
			std::size_t   control;
		};

		// F4RD:id
		static constexpr REL::ID kGroupEnabledID{ 243830, 2268248 };
		// F4RD:data - a pointer variable, not a static object
		static constexpr REL::ID kEnableManagerID{ 781703, 2689007, 4796297 };

		explicit Handler(RE::PlayerControlsData& a_data) noexcept :
			RE::PlayerInputHandler(a_data)
		{}

		Handler(const Handler&) = delete;
		Handler& operator=(const Handler&) = delete;

		// An MCM dropdown stores its own 0-based option index, not a keycode. Index 1-16 map onto
		// F4SE::InputMap's gamepad button offsets in the order config.json lists them.
		[[nodiscard]] static std::uint32_t DropdownToKeycode(int a_index)
		{
			if (a_index <= 0 || a_index > 16) {
				return 0;  // None, or out of range
			}
			return static_cast<std::uint32_t>(F4SE::InputMap::kMacro_GamepadOffset) + static_cast<std::uint32_t>(a_index) - 1;
		}

		static void Process(RE::ButtonEvent& a_event)
		{
			const auto idCode = static_cast<std::uint32_t>(a_event.idCode);
			const auto keycode = F4SE::InputMap::GamepadMaskToKeycode(idCode);
			const bool pressed = a_event.QJustPressed();
			const bool released = a_event.value == 0.0F;

			if (released) {
				std::erase(s_held, keycode);
			} else if (std::find(s_held.begin(), s_held.end(), keycode) == s_held.end()) {
				s_held.push_back(keycode);
			}

			if (pressed) {
				// A latch still standing on a fresh press lost its release somewhere PlayerControls
				// was not dispatching.
				std::erase_if(s_latches, [idCode](const Latch& a_latch) { return a_latch.idCode == idCode; });
				ClaimModifierIfBlocked(idCode, keycode);
				TryLatch(idCode, keycode);
			}

			const auto latch = std::find_if(s_latches.begin(), s_latches.end(),
				[idCode](const Latch& a_latch) { return a_latch.idCode == idCode; });
			if (latch != s_latches.end()) {
				Deliver(a_event, latch->control);
				if (released) {
					s_latches.erase(latch);
				}
			}

			ButtonBlock::Apply(a_event);
		}

		// A modifier set to Main and Modifier is blocked from its own press through its release,
		// whether or not a bind fires. Blocking only from the moment a bind fired would swallow a
		// release whose press the vanilla handlers already saw, leaving held actions stuck.
		static void ClaimModifierIfBlocked(std::uint32_t a_idCode, std::uint32_t a_keycode)
		{
			for (std::size_t i = 0; i < Controls::kAll.size(); ++i) {
				if (Settings::gamepadButton[i] <= 0 ||
					DropdownToKeycode(Settings::gamepadModifier[i]) != a_keycode ||
					Settings::EffectiveBlockMode(i) != ButtonBlock::Mode::kMainAndModifier) {
					continue;
				}
				if (IsGameplayOrPrompt(ResolvingContext(a_idCode))) {
					ButtonBlock::Claim(a_idCode);
				}
				return;
			}
		}

		static void TryLatch(std::uint32_t a_idCode, std::uint32_t a_keycode)
		{
			std::optional<std::size_t> chosen;
			for (std::size_t i = 0; i < Controls::kAll.size(); ++i) {
				if (DropdownToKeycode(Settings::gamepadButton[i]) != a_keycode) {
					continue;
				}
				const auto modifier = DropdownToKeycode(Settings::gamepadModifier[i]);
				if (modifier == 0) {
					if (!chosen) {
						chosen = i;
					}
				} else if (std::find(s_held.begin(), s_held.end(), modifier) != s_held.end()) {
					chosen = i;  // a bind whose modifier is held outranks a plain one on the same button
					break;
				}
			}
			if (!chosen) {
				return;
			}

			const auto context = ResolvingContext(a_idCode);
			if (!IsGameplayOrPrompt(context)) {
				REX::DEBUG("Gamepad KBM Keys: {} yielded to input context {}"sv,
					Controls::kAll[*chosen].label, static_cast<std::int32_t>(context));
				return;
			}

			s_latches.push_back({ a_idCode, *chosen });
			if (Settings::EffectiveBlockMode(*chosen) != ButtonBlock::Mode::kOff) {
				ButtonBlock::Claim(a_idCode);
			}
		}

		// The engine resolves a button's user event from the top of the context stack down, taking
		// the first context that maps it. Unmapped everywhere counts as gameplay.
		[[nodiscard]] static RE::UserEvents::INPUT_CONTEXT_ID ResolvingContext(std::uint32_t a_idCode)
		{
			const auto cm = RE::ControlMap::GetSingleton();
			if (!cm) {
				return RE::UserEvents::INPUT_CONTEXT_ID::kMainGameplay;
			}
			const auto& stack = cm->contextPriorityStack;
			for (auto i = stack.size(); i > 0; --i) {
				const auto context = stack[i - 1].get();
				const auto index = static_cast<std::int32_t>(context);
				if (index < 0 || index >= static_cast<std::int32_t>(RE::UserEvents::INPUT_CONTEXT_ID::kTotal)) {
					continue;
				}
				const auto input = cm->controlMaps[index];
				if (!input) {
					continue;
				}
				for (const auto& mapping : input->deviceMappings[static_cast<std::int32_t>(RE::INPUT_DEVICE::kGamepad)]) {
					if (static_cast<std::uint32_t>(mapping.inputKey) == a_idCode) {
						return context;
					}
				}
			}
			return RE::UserEvents::INPUT_CONTEXT_ID::kMainGameplay;
		}

		// Crosshair rollover prompts lose to a bind; every other context wins over it, so a bind
		// never steals a button a menu, quick loot, V.A.T.S., or the Workshop is using.
		[[nodiscard]] static bool IsGameplayOrPrompt(RE::UserEvents::INPUT_CONTEXT_ID a_context) noexcept
		{
			using Context = RE::UserEvents::INPUT_CONTEXT_ID;
			return a_context == Context::kMainGameplay ||
			       a_context == Context::kSpecialActivateRollover ||
			       a_context == Context::kTwoButtonRollover;
		}

		// The target's event group, read from the gameplay context's own mappings, which is where
		// the engine reads it when resolving the keyboard key. The keyboard mapping is the one that
		// matches for these controls; mouse and gamepad are searched only as a fallback.
		[[nodiscard]] static std::uint32_t TargetGroupFlag(const RE::BSFixedString& a_target)
		{
			const auto cm = RE::ControlMap::GetSingleton();
			const auto input = cm ? cm->controlMaps[static_cast<std::int32_t>(RE::UserEvents::INPUT_CONTEXT_ID::kMainGameplay)] : nullptr;
			if (!input) {
				return 0;
			}
			for (const auto device : { RE::INPUT_DEVICE::kKeyboard, RE::INPUT_DEVICE::kMouse, RE::INPUT_DEVICE::kGamepad }) {
				for (const auto& mapping : input->deviceMappings[static_cast<std::int32_t>(device)]) {
					if (mapping.eventID == a_target) {
						return static_cast<std::uint32_t>(mapping.userEventGroupFlag.underlying());
					}
				}
			}
			return 0;
		}

		// Mirrors the resolver's own disabled test for a keyboard event, minus the keyboard-only
		// ignore-KBM and text-entry flags, which describe the device rather than the game state.
		// This is what keeps a bind out of Dialogue, V.A.T.S., and the Workshop, where the engine
		// switches whole event groups off.
		[[nodiscard]] static bool TargetGroupEnabled(std::uint32_t a_flag)
		{
			if ((a_flag & 0x10) == a_flag) {
				return true;  // no group, or console only
			}
			const auto cm = RE::ControlMap::GetSingleton();
			if (cm && cm->ignoreActivateDisabledEvents && (a_flag & 0x4) != 0) {
				return false;
			}
			if (a_flag == 0xFFFFFFFF) {
				return true;
			}

			static const auto fn = WIO::Reloc::Address(kGroupEnabledID);
			static const auto managerVar = WIO::Reloc::Address(kEnableManagerID);
			if (!fn || !managerVar) {
				return true;
			}
			const auto manager = *reinterpret_cast<void**>(managerVar);
			if (!manager) {
				return true;
			}
			using func_t = bool (*)(void*, std::uint32_t);
			return reinterpret_cast<func_t>(fn)(manager, a_flag);
		}

		// Offers the renamed event to one consumer, applying the same gate PlayerControls and
		// MenuControls apply themselves.
		[[nodiscard]] static std::uint32_t Offer(RE::BSInputEventUser* a_user, RE::ButtonEvent& a_event)
		{
			if (!a_user || !a_user->inputEventHandlingEnabled ||
				a_event.handled.get() == RE::InputEvent::HANDLED_RESULT::kStop ||
				!a_user->ShouldHandleEvent(&a_event)) {
				return 0;
			}
			a_user->HandleEvent(&a_event);
			return 1;
		}

		// The four PlayerControls handlers that consume these controls, reached through the engine's
		// own member pointers.
		//
		// Walking the handler array instead would mean calling ShouldHandleEvent on every registered
		// handler: a handler that takes any button whatever it is called (the Scaleform forwarder)
		// would get the same press twice, once renamed and once real, and a pure-observer handler
		// does its work inside ShouldHandleEvent, so another plugin's observer would be driven by
		// this plugin's synthetic event. The cost is that a plugin registering its own handler for
		// one of these user events does not see a bind.
		[[nodiscard]] static bool IsTargetHandler(const RE::PlayerInputHandler* a_handler, const RE::PlayerControls& a_controls) noexcept
		{
			const auto* const handler = static_cast<const void*>(a_handler);
			return handler &&
			       (handler == static_cast<const void*>(a_controls.movementHandler) ||
			           handler == static_cast<const void*>(a_controls.runHandler) ||
			           handler == static_cast<const void*>(a_controls.toggleRunHandler) ||
			           handler == static_cast<const void*>(a_controls.autoMoveHandler));
		}

		static void Deliver(RE::ButtonEvent& a_event, std::size_t a_control)
		{
			const RE::BSFixedString savedName = a_event.strUserEvent;
			const bool              savedDisabled = a_event.disabled;
			const auto              savedHandled = a_event.handled.get();

			const RE::BSFixedString target{ Controls::kAll[a_control].event };
			const auto              group = TargetGroupFlag(target);
			const bool              enabled = TargetGroupEnabled(group);

			a_event.strUserEvent = target;
			a_event.disabled = !enabled;
			a_event.handled = RE::InputEvent::HANDLED_RESULT::kUnhandled;

			std::uint32_t delivered = 0;
			if (enabled) {
				// Movement, Run, Always Run, and Auto-Move. Same per-handler sequence as
				// PlayerControls' own per-event loop: gate and dispatch, then the held-state update
				// for handlers that track one.
				if (const auto controls = RE::PlayerControls::GetSingleton()) {
					for (const auto handler : controls->handlers) {
						if (!IsTargetHandler(handler, *controls)) {
							continue;
						}
						delivered += Offer(handler, a_event);
						if (handler->ShouldHandleEvent(&a_event)) {
							for (const auto held : controls->heldStateHandlers) {
								if (static_cast<RE::PlayerInputHandler*>(held) == handler) {
									held->UpdateHeldStateActive(&a_event);
									held->triggerReleaseEvent = false;
									break;
								}
							}
						}
					}
				}

				// Quick Save, Quick Load, and the Pip-Boy quick pages.
				if (const auto menuControls = RE::MenuControls::GetSingleton()) {
					for (const auto handler : menuControls->handlers) {
						const auto* const user = static_cast<const void*>(handler);
						if (user != static_cast<const void*>(menuControls->quickSaveLoadHandler) &&
							user != static_cast<const void*>(menuControls->pipboyHandler)) {
							continue;
						}
						delivered += Offer(handler, a_event);
					}
				}

				// Favorites, which is its own input receiver rather than a registered handler.
				if (const auto favorites = RE::FavoritesManager::GetSingleton()) {
					delivered += Offer(static_cast<RE::BSInputEventUser*>(favorites), a_event);
				}
			}

			a_event.strUserEvent = savedName;
			a_event.disabled = savedDisabled;
			a_event.handled = savedHandled;

			if (Settings::bDebugLog && (a_event.QJustPressed() || a_event.value == 0.0F)) {
				REX::DEBUG("Gamepad KBM Keys: {} {} on 0x{:X} group 0x{:X} {} -> {} consumer(s)"sv,
					Controls::kAll[a_control].label, a_event.value == 0.0F ? "release"sv : "press"sv,
					a_event.idCode, group, enabled ? "enabled"sv : "disabled"sv, delivered);
			}
		}

		inline static std::vector<std::uint32_t> s_held;
		inline static std::vector<Latch>         s_latches;
		inline static RE::ButtonEvent*           s_lastEvent{ nullptr };
		inline static std::uint32_t              s_lastTimeCode{ 0 };
		inline static bool                       s_announced{ false };
	};
}
