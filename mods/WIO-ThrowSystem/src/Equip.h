#pragma once

namespace TSO::Equip
{
	// The single home for the 10-argument EquipObject call and the item-count query, both of
	// which several callers need.

	// How many of a_item does a_actor currently have.
	[[nodiscard]] inline std::uint32_t GetCount(RE::Actor* a_actor, RE::TESForm* a_item)
	{
		std::uint32_t count = 0;
		return (a_actor && a_item && a_actor->GetItemCount(count, a_item, false)) ? count : 0;
	}

	// Equips a_item on a_actor. queueEquip=true with forceEquip=false is load-bearing: inverted,
	// it produces a permanently un-unequippable item.
	inline void Item(RE::Actor* a_actor, RE::TESForm* a_item)
	{
		const auto manager = RE::ActorEquipManager::GetSingleton();
		if (!manager || !a_actor || !a_item) {
			return;
		}
		manager->EquipObject(
			a_actor,
			RE::BGSObjectInstance{ a_item, nullptr },
			0,        // stackID - 0 for a plain, unmodified item (grenades/mines carry no instance data)
			1,        // number - equip as a single unit, matching a normal weapon equip
			nullptr,  // slot - let the engine resolve it from the item's own equip-type data
			true,     // queueEquip
			false,    // forceEquip
			true,     // playSounds
			true,     // applyNow
			false);   // locked
	}
}
