#pragma once

#include <algorithm>
#include <chrono>

#include "Notify.h"
#include "QuickSlots.h"
#include "SearchEquip.h"
#include "Settings.h"

namespace TSO::ThrowMelee
{
	// Two throw/melee paths, selected by iThrowInputType / iMeleeInputType:
	//
	//   Animation-only  PlayerControls::DoAction(kActionThrow / kActionMelee).
	//   Engine          Pokes the vanilla combined Melee/Throw handler so its real
	//                   tap/hold-cook-release behaviour runs.
	//
	// The Engine recipe follows jarari's Melee and Throw (github.com/jarari/MeleeAndThrow): reuse
	// the real ButtonEvent that arrived for this mod's hotkey rather than synthesizing one, these
	// being novtable types, rewrite its heldDownSecs to fake a hold duration, set
	// MeleeThrowHandler::pressRegistered, and call the handler's own OnButtonEvent.
	//
	//   Melee  fires on release only, heldDownSecs forced just under the vanilla throw-charge
	//          threshold (fThrowDelay:Controls), simulating a quick tap.
	//   Throw  fires on press and release, heldDownSecs forced to threshold + 20s, simulating an
	//          already-charged hold so it throws without a physical hold on this mod's key.
	//
	// bThrowCheckThrowableEquipped is checked before either path runs: the vanilla handler's
	// no-throwable fallback is a melee, so poking it with nothing equipped produces one.
	//
	// The auto-equip waterfall (iThrowAutoEquipMode / bThrowAutoEquipImmediateThrow) tries Quick
	// Slots then Search. Each method's own failure notification is suppressed only in Both mode, so
	// the combined $TSO_Note_FailBothEquipMethods message shows instead when both fail.

	namespace detail
	{
		[[nodiscard]] inline float GetVanillaThrowDelay()
		{
			const auto setting = RE::GetINISetting("fThrowDelay:Controls"sv);
			return setting ? setting->GetFloat() : 0.4f;  // vanilla default fallback if somehow missing
		}

		// Every known MeleeThrowHandler state field, plus meleeAttackState, the separate ActorState
		// field tracking whether the character is mid-swing. Fires per throw and melee, so DEBUG.
		inline void LogHandlerState(const char* a_label, const RE::MeleeThrowHandler& a_handler, const RE::Actor& a_actor)
		{
			REX::DEBUG(
				"Throwing System Overhaul: [{}] pressRegistered={} buttonHoldDebounce={} queueThrow={} heldStateActive={} triggerReleaseEvent={} meleeAttackState={}"sv,
				a_label,
				a_handler.pressRegistered,
				a_handler.buttonHoldDebounce,
				a_handler.queueThrow,
				a_handler.heldStateActive,
				a_handler.triggerReleaseEvent,
				static_cast<std::uint32_t>(a_actor.meleeAttackState));
		}

		// Equip index 2 is the throwable slot, the same index QuickSlots.h reads and writes.
		[[nodiscard]] inline bool IsThrowableEquipped(RE::Actor* a_player)
		{
			const auto equipped = a_player->GetEquippedItem(RE::BGSEquipIndex{ 2 });
			return equipped.object != nullptr;
		}

		// Deferred auto-equip throw. Equip::Item uses queueEquip=true, so the equip does not land in
		// the same frame, and throwing inside that press acts while slot 2 is still empty: the vanilla
		// handler falls back to a melee (Engine) or animates with nothing thrown (Animation-only).
		//
		// So a press that had to auto-equip records a pending request, and Update() fires the throw
		// once the equip has landed plus a couple of settle frames, giving up after a timeout. It goes
		// through DoAction even in Engine mode, the ButtonEvent that started the gesture being gone.
		inline constexpr auto kDeferredThrowTimeout = std::chrono::milliseconds{ 1500 };
		inline constexpr int  kDeferredThrowSettleFrames = 2;

		inline bool                                  s_deferredThrowPending = false;
		inline int                                   s_deferredThrowSettle = 0;
		inline std::chrono::steady_clock::time_point s_deferredThrowDeadline{};
		// Whether the Throw key is still down since the press that requested the deferred throw. In
		// Engine mode a tap wants the instant DoAction and a hold wants a real cook and arc on
		// release; see the hold-adoption branch in TryThrow.
		inline bool                                  s_deferredThrowKeyHeld = false;

		inline void ClearDeferredThrow()
		{
			s_deferredThrowPending = false;
			s_deferredThrowKeyHeld = false;
		}

		// a_keyHeld is false when the request came from something other than a Throw-key press. A
		// Quick Slot hotkey has no gesture to adopt, so Update() takes DoAction in both modes.
		inline void RequestDeferredThrow(bool a_keyHeld = true)
		{
			s_deferredThrowPending = true;
			s_deferredThrowSettle = kDeferredThrowSettleFrames;
			s_deferredThrowDeadline = std::chrono::steady_clock::now() + kDeferredThrowTimeout;
			s_deferredThrowKeyHeld = a_keyHeld;
		}

		// The player-configurable gates on whether a throw may happen at all. Shared with the
		// Quick Slot throw, which offers "skip checks" as its own option.
		[[nodiscard]] inline bool ThrowChecksPass(RE::PlayerCharacter* a_player)
		{
			if (Settings::bThrowCheckWeaponDrawn.GetValue() && !a_player->GetWeaponMagicDrawn()) {
				return false;
			}
			if (Settings::bThrowBlockIfWeaponBlocked.GetValue() && a_player->gunState == RE::GUN_STATE::kBlocked) {
				return false;
			}
			if (Settings::bThrowBlockIfWeaponRelaxed.GetValue() && a_player->gunState == RE::GUN_STATE::kRelaxed) {
				return false;
			}
			if (Settings::bThrowBlockIfReloading.GetValue() && a_player->gunState == RE::GUN_STATE::kReloading) {
				return false;
			}
			return true;
		}

		// Runs the auto-equip waterfall if nothing is equipped, then decides whether the throw should
		// proceed. It may equip an item and show notifications, so the caller runs it once per press.
		[[nodiscard]] inline bool ResolveThrowReadiness(RE::Actor* a_player)
		{
			if (IsThrowableEquipped(a_player)) {
				return true;
			}

			const auto mode = Settings::iThrowAutoEquipMode.GetValue();
			const bool both = mode == 3;
			bool       autoEquipped = false;

			if (mode == 1 || both) {
				// a_allowSearchFallback=false: this waterfall sequences Quick Slots then Search itself via
				// `both` below, and bCycleSearchOnFail is a separate toggle.
				autoEquipped = QuickSlots::CycleToNextSlot(!both, false);
			}
			if (!autoEquipped && (mode == 2 || both)) {
				autoEquipped = SearchEquip::FindAndEquipFirstThrowable(!both) != nullptr;
			}

			if (autoEquipped) {
				// Never throw from this press: the item is only queued to be equipped. Hand off
				// to Update() instead.
				if (Settings::bThrowAutoEquipImmediateThrow.GetValue()) {
					RequestDeferredThrow();
				}
				return false;
			}

			// Nothing got equipped. The generic "nothing equipped" notification only fires when no
			// auto-equip attempt was configured: a single method already showed its own failure, and
			// Both shows the combined message instead.
			if (mode == 0) {
				if (Settings::bThrowNotifyOnNothingEquipped.GetValue()) {
					Notify::Show(WIO::Translations::Localize(
						"$TSO_Note_FailNoneEquipOnThrow"sv, "No throwable equipped"sv));
				}
			} else if (both) {
				if (Settings::bQuickSwapNotifyOnCycleFail.GetValue() || Settings::bSearchNotifyOnFail.GetValue()) {
					Notify::Show(WIO::Translations::Localize(
						"$TSO_Note_FailBothEquipMethods"sv, "No throwable found through Cycling or Search"sv));
				}
			}

			// bThrowCheckThrowableEquipped decides whether to block the throw or attempt it
			// anyway with nothing confirmed equipped.
			return !Settings::bThrowCheckThrowableEquipped.GetValue();
		}
	}

	// Quick Slots - Throw After Equipping (iQuickSwapSlotThrowMode): 0 = No, 1 = only if the enabled
	// throw checks pass, 2 = regardless. Called by InputHook after a Quick Slot hotkey actually
	// equipped something; the Cycle hotkey deliberately does not reach here. The equip is queued, so
	// this never throws inline: it hands over to the same deferred request Update() drives.
	inline void TryQuickSlotThrow()
	{
		const auto mode = Settings::iQuickSwapSlotThrowMode.GetValue();
		if (mode <= 0) {
			return;
		}

		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		if (mode == 1 && !detail::ThrowChecksPass(player)) {
			return;
		}

		detail::RequestDeferredThrow(false);
		REX::DEBUG("Throwing System Overhaul: quick slot throw queued (mode {})"sv, mode);
	}

	// Cancels a pending deferred throw. Called by InputHook when hotkeys are blocked, so a menu
	// opening between the press and the equip landing does not get thrown into.
	inline void CancelDeferredThrow()
	{
		detail::ClearDeferredThrow();
	}

	// Called every frame from InputHook::PerformInputProcessing. No-op unless a press had to
	// auto-equip and bThrowAutoEquipImmediateThrow is on.
	inline void Update()
	{
		if (!detail::s_deferredThrowPending) {
			return;
		}

		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			detail::ClearDeferredThrow();
			return;
		}

		const bool expired = std::chrono::steady_clock::now() >= detail::s_deferredThrowDeadline;

		if (!detail::IsThrowableEquipped(player)) {
			if (expired) {
				detail::ClearDeferredThrow();
				REX::DEBUG("Throwing System Overhaul: deferred auto-equip throw abandoned - equip never landed"sv);
			}
			return;
		}

		// Engine mode with the key still held: TryThrow's hold-adoption branch takes over on one of
		// the held ButtonEvents arriving this frame, poking the vanilla handler for a real cook and
		// arc. Firing DoAction here would throw out from under a still-held button.
		if (detail::s_deferredThrowKeyHeld && Settings::iThrowInputType.GetValue() != 1) {
			if (expired) {
				detail::ClearDeferredThrow();
				REX::DEBUG("Throwing System Overhaul: deferred auto-equip throw abandoned - engine hold never adopted"sv);
			}
			return;
		}

		// Equip has landed. Give it a couple of frames to settle before asking for the action
		// rather than throwing on the exact frame the slot fills.
		if (detail::s_deferredThrowSettle > 0) {
			--detail::s_deferredThrowSettle;
			return;
		}

		const auto pcon = RE::PlayerControls::GetSingleton();
		if (!pcon) {
			detail::ClearDeferredThrow();
			return;
		}

		if (!pcon->DoAction(RE::DEFAULT_OBJECT::kActionThrow, RE::ActionInput::ACTIONPRIORITY::kTry)) {
			// Refused, meaning something else owns the action this frame. Keep retrying until
			// the timeout rather than dropping the throw on one bad frame.
			if (expired) {
				detail::ClearDeferredThrow();
				REX::DEBUG("Throwing System Overhaul: deferred auto-equip throw abandoned - DoAction kept refusing"sv);
			}
			return;
		}

		detail::ClearDeferredThrow();
		REX::DEBUG("Throwing System Overhaul: deferred auto-equip throw fired"sv);
	}

	inline void TryMelee(RE::ButtonEvent& a_event)
	{
		detail::ClearDeferredThrow();  // the player asked for a melee instead

		if (Settings::iMeleeInputType.GetValue() == 1) {
			if (!a_event.QJustPressed()) {
				return;
			}
			if (const auto pcon = RE::PlayerControls::GetSingleton(); pcon) {
				pcon->DoAction(RE::DEFAULT_OBJECT::kActionMelee, RE::ActionInput::ACTIONPRIORITY::kTry);
				REX::DEBUG("Throwing System Overhaul: melee (animation-only)"sv);
			}
			return;
		}

		// Engine mode: act on release only.
		if (!RE::QReleased(a_event)) {
			return;
		}

		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto pcon = RE::PlayerControls::GetSingleton();
		const auto handler = pcon ? pcon->meleeThrowHandler : nullptr;
		if (!player || !handler) {
			return;
		}

		detail::LogHandlerState("melee: before", *handler, *player);
		a_event.heldDownSecs = std::max(detail::GetVanillaThrowDelay() - 0.1f, 0.0f);
		handler->pressRegistered = true;
		handler->HandleEvent(&a_event);
		detail::LogHandlerState("melee: after", *handler, *player);
		REX::DEBUG("Throwing System Overhaul: melee (engine)"sv);
	}

	inline void TryThrow(RE::ButtonEvent& a_event)
	{
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		// Cleared before any early return below, so a release that goes no further still ends
		// the key-held state rather than leaving it stuck true.
		if (RE::QReleased(a_event)) {
			detail::s_deferredThrowKeyHeld = false;
		}

		// Shared by the auto-equip decision below and, in Engine mode, the meleeAttackState guard
		// further down. Neither has meaning on a release, so a release replays what the press decided.
		// Declared outside the Engine-only branch, so a press rejected by the equip-readiness gate
		// cannot leave stale state for a later release to misread.
		static bool s_throwPressValid = false;

		if (a_event.QJustPressed()) {
			detail::ClearDeferredThrow();  // a fresh press supersedes any pending deferred throw

			// Both run once per gesture, on the press only. ThrowChecksPass reads gunState and
			// GetWeaponMagicDrawn, which move during the throw, so evaluating it on the release could drop
			// the release poke Engine mode needs to finish the throw. ResolveThrowReadiness has side
			// effects for the same reason.
			s_throwPressValid = detail::ThrowChecksPass(player) && detail::ResolveThrowReadiness(player);
		}
		// Deferred auto-equip throw, hold case, Engine mode only. A held button dispatches a fresh
		// engine-owned ButtonEvent every frame, so on the frame the equip lands there is a real event
		// to poke the vanilla handler with, and the hold cooks and shows the arc normally. The handler
		// reads a held event as pressed (value > 0); only the release carries value == 0.
		// s_throwPressValid is then set so the release runs the normal release-poke below, and Update()
		// stands down so the two paths cannot both fire.
		if (detail::s_deferredThrowPending && Settings::iThrowInputType.GetValue() != 1 &&
			!a_event.QJustPressed() && !RE::QReleased(a_event)) {
			if (!detail::IsThrowableEquipped(player)) {
				return;  // equip hasn't landed yet - try again on the next frame's held event
			}
			if (player->meleeAttackState != 0) {
				return;  // mid-swing; same guard the ordinary press applies, just retried
			}
			const auto pcon = RE::PlayerControls::GetSingleton();
			const auto handler = pcon ? pcon->meleeThrowHandler : nullptr;
			if (!handler) {
				return;
			}

			detail::ClearDeferredThrow();
			detail::LogHandlerState("throw-deferred-press: before", *handler, *player);
			a_event.heldDownSecs = detail::GetVanillaThrowDelay() + 20.0f;
			handler->pressRegistered = true;
			handler->HandleEvent(&a_event);
			detail::LogHandlerState("throw-deferred-press: after", *handler, *player);

			s_throwPressValid = true;
			REX::DEBUG("Throwing System Overhaul: deferred auto-equip throw adopted as an engine hold"sv);
			return;
		}

		if (!s_throwPressValid) {
			return;
		}

		if (Settings::iThrowInputType.GetValue() == 1) {
			if (!a_event.QJustPressed()) {
				return;
			}
			if (const auto pcon = RE::PlayerControls::GetSingleton(); pcon) {
				pcon->DoAction(RE::DEFAULT_OBJECT::kActionThrow, RE::ActionInput::ACTIONPRIORITY::kTry);
				REX::DEBUG("Throwing System Overhaul: throw (animation-only)"sv);
			}
			return;
		}

		// Engine mode acts on the press and release edges only. A held button dispatches a fresh
		// ButtonEvent every frame, so without this guard a hold fires the sequence ~100 times a second.
		if (!a_event.QJustPressed() && !RE::QReleased(a_event)) {
			return;
		}

		const auto pcon = RE::PlayerControls::GetSingleton();
		const auto handler = pcon ? pcon->meleeThrowHandler : nullptr;
		if (!handler) {
			return;
		}

		// Capture press and release before any mutation: QJustPressed() requires
		// heldDownSecs == 0, so it becomes unreliable the instant that field is touched.
		const bool isPress = a_event.QJustPressed();

		// Rejects the press while meleeAttackState != 0 rather than letting it misfire into an unwanted
		// melee, so a throw cannot interrupt a melee mid-animation. That interrupt is not reachable
		// through the handler's visible state fields. Press only: a release already had
		// s_throwPressValid confirmed above.
		if (isPress && player->meleeAttackState != 0) {
			s_throwPressValid = false;
			REX::DEBUG("Throwing System Overhaul: throw press rejected - still mid-melee-animation (meleeAttackState={})"sv, static_cast<std::uint32_t>(player->meleeAttackState));
			return;
		}

		detail::LogHandlerState(isPress ? "throw-press: before" : "throw-release: before", *handler, *player);

		a_event.heldDownSecs = detail::GetVanillaThrowDelay() + 20.0f;
		handler->pressRegistered = true;
		handler->HandleEvent(&a_event);

		detail::LogHandlerState(isPress ? "throw-press: after" : "throw-release: after", *handler, *player);
		REX::DEBUG("Throwing System Overhaul: throw (engine)"sv);
	}
}
