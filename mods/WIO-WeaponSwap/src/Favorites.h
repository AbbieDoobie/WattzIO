#pragma once

#include <array>
#include <optional>

// Reads the player's numbered Favorites - the 12-slot list keys 1-9/0/-/= assign to - out of
// inventory extra data, and resolves what is currently equipped or drawn. Nothing here mutates
// state; WeaponSwapLogic.h owns the swap/equip action.
//
// Slot index is RE::ExtraFavorite::quickkeyIndex (0-11) on the favorited stack's own
// RE::ExtraDataList; equipped state is Stack::IsEquipped(), reading its kSlotIndex1/2/3 flags.
namespace WS::Favorites
{
	struct Slot
	{
		RE::TESBoundObject* object = nullptr;
		bool                isWeapon = false;
		bool                isThrowable = false;
		bool                isEquipped = false;
	};

	using SlotArray = std::array<std::optional<Slot>, 12>;

	// Fallout4.esm's GrenadeSlot equip type [00046AAC]. Every grenade, mine, Molotov, flare and
	// beacon carries it, and it is what sends an item to equip index 2 rather than the hands.
	// Read from the item's own equip type, not a keyword list, so modded throwables need no setup.
	inline constexpr RE::TESFormID kGrenadeSlotFormID = 0x00046AAC;

	[[nodiscard]] inline bool IsThrowable(RE::TESBoundObject* a_object)
	{
		const auto weap = a_object ? a_object->As<RE::TESObjectWEAP>() : nullptr;
		if (!weap) {
			return false;
		}
		const auto slot = static_cast<const RE::BGSEquipType*>(weap)->GetEquipSlot(nullptr);
		return slot && slot->GetFormID() == kGrenadeSlotFormID;
	}

	// One pass over the player's inventory, indexed by ExtraFavorite::quickkeyIndex (0-11). A
	// slot with nothing favorited there is std::nullopt - never conflate "missing" with "index 0".
	[[nodiscard]] inline SlotArray BuildSlots(RE::Actor* a_actor)
	{
		SlotArray slots{};
		if (!a_actor || !a_actor->inventoryList) {
			return slots;
		}

		RE::BSAutoReadLock lock{ a_actor->inventoryList->rwLock };
		for (auto& item : a_actor->inventoryList->data) {
			for (auto stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
				const auto extra = stack->extra.get();
				const auto fav = extra ? extra->GetByType<RE::ExtraFavorite>() : nullptr;
				if (!fav) {
					continue;
				}

				const auto idx = static_cast<std::int32_t>(fav->quickkeyIndex);
				if (idx < 0 || static_cast<std::size_t>(idx) >= slots.size()) {
					continue;
				}

				slots[static_cast<std::size_t>(idx)] = Slot{
					.object = item.object,
					.isWeapon = item.object && item.object->Is<RE::TESObjectWEAP>(),
					.isThrowable = IsThrowable(item.object),
					.isEquipped = stack->IsEquipped()
				};
			}
		}
		return slots;
	}

	// Weapon currently drawn - RE::ActorState::WEAPON_STATE >= kDrawn. Also false
	// while transitioning into a draw, deliberately: a press that lands mid-animation counts as
	// holstered, because the weapon is not out yet.
	[[nodiscard]] inline bool IsWeaponDrawn(RE::Actor* a_actor)
	{
		return a_actor && a_actor->GetWeaponMagicDrawn();
	}

	// Best-effort "is a real weapon equipped at all" check, independent of favorites tracking,
	// used only to tell "holstered with a weapon equipped" apart from "nothing equipped at all"
	// for the holstered fallback. Equip index 0 is taken to be the weapon slot.
	[[nodiscard]] inline bool HasAnyWeaponEquipped(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return false;
		}
		// CommonLibF4RD returns BGSObjectInstance by value rather than through an
		// out-parameter.
		const auto item = a_actor->GetEquippedItem(RE::BGSEquipIndex{ 0 });
		return item.object && item.object->Is<RE::TESObjectWEAP>();
	}

	// Index of the favorite slot holding the weapon in hand - equipped, a weapon, not a throwable -
	// or std::nullopt if the hands hold nothing favorited.
	[[nodiscard]] inline std::optional<std::int32_t> HandWeaponSlot(const SlotArray& a_slots)
	{
		for (std::size_t i = 0; i < a_slots.size(); ++i) {
			if (a_slots[i] && a_slots[i]->isEquipped && a_slots[i]->isWeapon && !a_slots[i]->isThrowable) {
				return static_cast<std::int32_t>(i);
			}
		}
		return std::nullopt;
	}

	// Index of the slot (0-11) whose favorited stack is currently equipped, or std::nullopt if
	// none of the tracked favorite slots match what's equipped right now (includes the case where
	// the player is wielding an unfavorited weapon, or is unarmed).
	//
	// The weapon in hand wins over any other equipped favorite. A favorited grenade in the grenade
	// slot, or worn armor, is equipped too, and taking the first equipped slot let one sitting
	// ahead of the gun pin the position there - every cycle press landed on the same slot.
	[[nodiscard]] inline std::optional<std::int32_t> CurrentlyEquippedSlot(const SlotArray& a_slots)
	{
		if (const auto hand = HandWeaponSlot(a_slots)) {
			return hand;
		}
		for (std::size_t i = 0; i < a_slots.size(); ++i) {
			if (a_slots[i] && a_slots[i]->isEquipped) {
				return static_cast<std::int32_t>(i);
			}
		}
		return std::nullopt;
	}
}
