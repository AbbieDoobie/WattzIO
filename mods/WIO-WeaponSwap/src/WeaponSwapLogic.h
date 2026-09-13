#pragma once

#include <algorithm>
#include <optional>
#include <vector>

#include "Favorites.h"
#include "Settings.h"

// The swap/equip decision, run once per qualifying button release (InputHook.h owns
// press/hold/release detection and hands off a real, engine-owned ButtonEvent).
//
// Equipping goes through RE::FavoritesManager::UseQuickkeyItem(index), the same call the engine
// makes for a number key, rather than reimplementing equip, ammo and sound handling.
//
// Only unholster is ever forced, and one synthetic press suffices because unholster is decided
// instantly on press. There is no ForceHolster: none of the four holstered options needs one, and
// a synthetic holster would have to be held across successive real frames to progress.
namespace WS::WeaponSwapLogic
{
	namespace detail
	{
		// Only Equip Weapons and Don't Equip Throwables filter independently. Grenades and mines are
		// weapon records, so only the second one removes them.
		[[nodiscard]] inline bool Qualifies(const std::optional<Favorites::Slot>& a_slot)
		{
			return a_slot.has_value() &&
			       (!Settings::bOnlyEquipWeapons || a_slot->isWeapon) &&
			       (!Settings::bSkipThrowables || !a_slot->isThrowable);
		}

		// Off-hand tracking. A throwable or armor favorite equips without changing what is in hand,
		// so a position read from equip state never moves past it: Rifle, Grenade, Pistol cycled
		// Rifle -> Grenade -> Grenade forever. The last off-hand slot a cycle or swap press landed
		// on stands in as the position while it is still equipped and the same weapon is still in
		// hand; otherwise it is dropped. Memory only - never saved, and cleared on every save load
		// and new game (main.cpp).
		//
		// Skipped entirely with Only Equip Weapons and Don't Equip Throwables both on (the
		// defaults): every qualifying slot is then a hand weapon, and there is nothing to track.
		inline std::optional<std::int32_t> s_offHandSlot;
		inline std::optional<std::int32_t> s_offHandHeldSlot;

		[[nodiscard]] inline bool OffHandTrackingActive()
		{
			return !(Settings::bOnlyEquipWeapons && Settings::bSkipThrowables);
		}

		inline void ResetOffHand()
		{
			s_offHandSlot.reset();
			s_offHandHeldSlot.reset();
		}

		[[nodiscard]] inline std::optional<std::int32_t> CurrentPosition(const Favorites::SlotArray& a_slots)
		{
			if (OffHandTrackingActive() && s_offHandSlot) {
				const auto& slot = a_slots[static_cast<std::size_t>(*s_offHandSlot)];
				if (slot && slot->isEquipped && Favorites::HandWeaponSlot(a_slots) == s_offHandHeldSlot) {
					return s_offHandSlot;
				}
			}
			ResetOffHand();
			return Favorites::CurrentlyEquippedSlot(a_slots);
		}

		// Recorded whether or not the equip then happens (Do Nothing while holstered, say): a slot
		// that did not end up equipped fails CurrentPosition's check on the next press anyway.
		inline void RememberLanding(const Favorites::SlotArray& a_slots, std::optional<std::int32_t> a_target)
		{
			if (!OffHandTrackingActive() || !a_target) {
				ResetOffHand();
				return;
			}
			const auto& slot = a_slots[static_cast<std::size_t>(*a_target)];
			if (!slot || (slot->isWeapon && !slot->isThrowable)) {
				ResetOffHand();
				return;
			}
			s_offHandSlot = a_target;
			s_offHandHeldSlot = Favorites::HandWeaponSlot(a_slots);
		}

		// Literal-target resolution for "Slots 1 and 2": the target is always exactly 1 or 2, never
		// a fallback search. If it does not qualify (empty, or a non-weapon with Only Equip Weapons
		// on), nothing happens.
		[[nodiscard]] inline std::optional<std::int32_t> ResolveToggleTarget(const Favorites::SlotArray& a_slots)
		{
			const auto equipped = CurrentPosition(a_slots);
			std::int32_t target = 0;  // default: "if you don't have 1 (nor 2) equipped, equip 1"
			if (equipped == 0) {
				target = 1;
			} else if (equipped == 1) {
				target = 0;
			}
			return Qualifies(a_slots[static_cast<std::size_t>(target)]) ? std::optional{ target } : std::nullopt;
		}

		// "All Slots" and Next / Previous Favorite Slot: cycles only the qualifying slots, so a press
		// always lands on a real next weapon regardless of gaps, falling back to the first if nothing
		// tracked is equipped - in both directions. The position comes from what is equipped right
		// now, never from a remembered index, so a manual equip mid-cycle is picked up for free.
		// Still covers all 12 even with Hold (Slot 3) / Triple Tap (Slot 4) on - those are additive
		// shortcuts and do not carve their slot out of the cycle.
		[[nodiscard]] inline std::optional<std::int32_t> ResolveCycleTarget(const Favorites::SlotArray& a_slots, bool a_forward = true)
		{
			std::vector<std::int32_t> qualifying;
			qualifying.reserve(a_slots.size());
			for (std::size_t i = 0; i < a_slots.size(); ++i) {
				if (Qualifies(a_slots[i])) {
					qualifying.push_back(static_cast<std::int32_t>(i));
				}
			}
			if (qualifying.empty()) {
				return std::nullopt;
			}

			const auto equipped = CurrentPosition(a_slots);
			if (!equipped) {
				return qualifying.front();
			}

			const auto it = std::ranges::find(qualifying, *equipped);
			if (it == qualifying.end()) {
				return qualifying.front();
			}
			if (!a_forward) {
				return it == qualifying.begin() ? qualifying.back() : *std::prev(it);
			}
			const auto next = std::next(it);
			return next == qualifying.end() ? qualifying.front() : *next;
		}

		[[nodiscard]] inline std::optional<std::int32_t> ResolveTarget(const Favorites::SlotArray& a_slots)
		{
			switch (Settings::swapType) {
			case Settings::SwapType::kTwoSlots:
				return ResolveToggleTarget(a_slots);
			case Settings::SwapType::kAll:
				return ResolveCycleTarget(a_slots);
			default:
				return std::nullopt;
			}
		}

		inline void EquipSlot(std::int32_t a_slotIndex)
		{
			const auto mgr = RE::FavoritesManager::GetSingleton();
			if (!mgr) {
				return;
			}
			if (!RE::UseQuickkeyItem(mgr, static_cast<std::uint32_t>(a_slotIndex))) {
				REX::DEBUG("Weapon Swap Button: UseQuickkeyItem({}) returned false"sv, a_slotIndex);
			}
		}

		// A single synthetic press to the real readyWeaponHandler, reusing the engine-owned event
		// already in hand rather than constructing one - RE::ButtonEvent is novtable, so a locally
		// built instance has no valid vtable pointer.
		inline void ForceUnholster(RE::Actor* a_actor, RE::ButtonEvent& a_event)
		{
			if (!a_actor || a_actor->GetWeaponMagicDrawn()) {
				return;
			}
			const auto pcon = RE::PlayerControls::GetSingleton();
			const auto handler = pcon ? pcon->readyWeaponHandler : nullptr;
			if (!handler) {
				return;
			}
			// value is zeroed too, so a mouse wheel notch - acted on at its press, not a release -
			// reaches the handler in the same shape as every button release does.
			const auto realValue = a_event.value;
			const auto realHeldDownSecs = a_event.heldDownSecs;
			a_event.value = 0.0f;
			a_event.heldDownSecs = 0.0f;
			handler->HandleEvent(&a_event);
			a_event.value = realValue;
			a_event.heldDownSecs = realHeldDownSecs;
		}

		// Normal press, Hold (Slot 3), Triple Tap (Slot 4), and Next / Previous Favorite Slot all
		// land here, so the holstered/none-equipped dropdown governs every one of them. A nullopt
		// target still runs this: holstered, "Equip Next and Unholster" then just draws whatever is
		// in hand.
		inline void Apply(RE::Actor* a_actor, RE::ButtonEvent& a_event, std::optional<std::int32_t> a_target)
		{
			if (Favorites::IsWeaponDrawn(a_actor)) {
				if (a_target) {
					EquipSlot(*a_target);
				}
				return;
			}

			// Holstered, or no weapon equipped at all.
			switch (Settings::holsteredBehavior) {
			case Settings::HolsteredBehavior::kEquipNextAndUnholster:
				if (a_target) {
					EquipSlot(*a_target);
				}
				ForceUnholster(a_actor, a_event);
				break;

			case Settings::HolsteredBehavior::kEquipNext:
				if (a_target) {
					EquipSlot(*a_target);
				}
				break;

			case Settings::HolsteredBehavior::kDoNothing:
				break;

			case Settings::HolsteredBehavior::kUnholsterDontSwap:
				if (Favorites::HasAnyWeaponEquipped(a_actor)) {
					ForceUnholster(a_actor, a_event);
				} else {
					// Nothing to unholster - falls back to Equip Next and Unholster's behavior.
					if (a_target) {
						EquipSlot(*a_target);
					}
					ForceUnholster(a_actor, a_event);
				}
				break;
			}
		}
	}

	// A normal press: the target comes from Weapon Swap Type (toggle 1<->2, or cycle all).
	inline void Trigger(RE::ButtonEvent& a_event)
	{
		const auto actor = RE::PlayerCharacter::GetSingleton();
		if (!actor) {
			return;
		}

		const auto slots = Favorites::BuildSlots(actor);
		const auto target = detail::ResolveTarget(slots);
		detail::RememberLanding(slots, target);
		detail::Apply(actor, a_event, target);
	}

	// Called on every save load and new game, so off-hand tracking never outlives the playthrough
	// it was recorded in.
	inline void ResetTracking()
	{
		detail::ResetOffHand();
	}

	// Next / Previous Favorite Slot: always the qualifying-slot cycle, whatever Weapon Swap Type is
	// set to, and through the same holstered/none-equipped dropdown as every other press.
	inline void TriggerCycle(RE::ButtonEvent& a_event, bool a_forward)
	{
		const auto actor = RE::PlayerCharacter::GetSingleton();
		if (!actor) {
			return;
		}

		const auto slots = Favorites::BuildSlots(actor);
		const auto target = detail::ResolveCycleTarget(slots, a_forward);
		detail::RememberLanding(slots, target);
		detail::Apply(actor, a_event, target);
	}

	// Hold (Slot 3) and Triple Tap (Slot 4): the target is that literal slot, independent of Weapon
	// Swap Type. Same no-fallback-search rule as the 1<->2 toggle - an empty slot, or a non-weapon
	// with Only Equip Weapons on, means there is nothing to equip.
	inline void TriggerSlot(RE::ButtonEvent& a_event, std::size_t a_slotIndex)
	{
		const auto actor = RE::PlayerCharacter::GetSingleton();
		if (!actor) {
			return;
		}

		const auto slots = Favorites::BuildSlots(actor);
		const auto target = (a_slotIndex < slots.size() && detail::Qualifies(slots[a_slotIndex])) ?
		                        std::optional{ static_cast<std::int32_t>(a_slotIndex) } :
		                        std::nullopt;
		detail::Apply(actor, a_event, target);
	}
}
