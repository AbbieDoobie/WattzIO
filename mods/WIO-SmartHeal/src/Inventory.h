#pragma once

#include <cstdint>
#include <vector>

#include "ItemCatalog.h"
#include "PlayerState.h"
#include "Settings.h"

// The player's aid items as the solver sees them: one row per distinct item, already filtered to
// the categories the MCM currently allows, already resolved to absolute HP and rads for this
// player at this moment.
namespace SH::Inventory
{
	struct Available
	{
		const ItemCatalog::Entry* entry = nullptr;
		std::uint32_t             count = 0;  // how many are actually spendable

		// Resolved for the player as they are right now. hpPercent has already been folded into
		// `hp` using the live maximum, so the solver only ever adds plain numbers.
		float hp = 0.0F;
		float rads = 0.0F;
		bool  mendsLimb = false;

		// Tie-break material only. Never part of deciding whether a target is met.
		std::int32_t goldValue = 0;

		// Alcohol only; everything else sits at the neutral middle rank and never reads it.
		// Resolved here rather than in the solver so the settings-driven rule lives beside the
		// rest of the classification. See ItemCatalog::AlcoholRankFor.
		std::int32_t alcoholRank = ItemCatalog::kAlcoholRankPlain;

		[[nodiscard]] ItemCatalog::Category Category() const { return entry->category; }
		[[nodiscard]] const char*           Name() const { return entry->item->GetFullName(); }
	};

	// Snapshot of everything usable. Reads the inventory under its own read lock and does not hold
	// on to any engine pointer past the call except the AlchemyItem forms, which are permanent.
	[[nodiscard]] inline std::vector<Available> Snapshot(const PlayerState::State& a_state)
	{
		std::vector<Available> out;

		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->inventoryList) {
			return out;
		}

		auto& list = *player->inventoryList;
		const RE::BSAutoReadLock lock{ list.rwLock };

		out.reserve(list.data.size());

		// Alcohol the include-toggles turned away, and alcohol that survived them. Counted so the
		// exclusion a player is likely to ask about is not silent: with all three toggles off, a
		// drink that heals, irradiates or buffs hard is skipped, and in a modded load order that
		// can be most of the bar - one tested case had only 50 of 168 drinks eligible by default.
		std::size_t shelfRejected = 0;
		std::size_t alcoholAllowed = 0;

		for (auto& item : list.data) {
			const auto entry = ItemCatalog::Find(item.object);
			if (!entry) {
				continue;
			}
			// Allowed(), not CategoryEnabled(): special food that also carries radiation has to
			// clear the Include Irradiated Food gate as well - see ItemCatalog::Allowed.
			if (!ItemCatalog::Allowed(*entry)) {
				// CategoryEnabled first: a drink skipped because Alcohol is Off, or because the
				// cooldown is running, is not a shelf problem and must not be reported as one.
				if (entry->category == ItemCatalog::Category::kAlcohol &&
					ItemCatalog::CategoryEnabled(entry->category) &&
					!ItemCatalog::AlcoholIncluded(*entry) && item.GetCount() > 0) {
					++shelfRejected;
				}
				continue;
			}

			const auto total = item.GetCount();
			if (total == 0) {
				continue;
			}
			// Quest items are never spent. GetQuestItemCount reports how many of this stack are
			// flagged, so the rest stay available rather than the whole entry being dropped.
			const auto quest = RE::GetQuestItemCount(&list, item.object);
			if (quest >= total) {
				// Logged: a quest-flagged item is one of the two silent reasons an item is
				// skipped, and the catalogue dump only explains the other.
				if (Settings::debugLog) {
					REX::DEBUG("Smart Heal Button: [INVENTORY] skipped {} - all {} are quest items"sv,
						entry->item->GetFullName(), total);
				}
				continue;
			}

			const auto value = ItemCatalog::Evaluate(*entry, player);

			Available available{};
			available.entry = entry;
			available.count = total - quest;
			available.hp = value.hpFlat + (value.hpPercent * 0.01F * a_state.maxHP);
			available.rads = value.rads;
			// From the catalogue, not from the per-press evaluation: whether an item can mend a
			// limb is a property of the item, not of whether its restore effect is the active
			// mechanism right now. See ItemCatalog::Entry::canMendLimb.
			available.mendsLimb = entry->canMendLimb;
			available.goldValue = item.GetInventoryValue(0, false);
			available.alcoholRank = ItemCatalog::AlcoholRank(*entry);

			if (entry->category == ItemCatalog::Category::kAlcohol) {
				++alcoholAllowed;
			}

			out.push_back(available);
		}

		// Warned rather than logged only under bDebugLog when the include-toggles left the player
		// with no drink at all: that is the case where the mod looks broken. Anything less is a
		// debug line, since turning some drinks away is the setting working.
		if (shelfRejected > 0 && alcoholAllowed == 0) {
			REX::WARN("Smart Heal Button: Alcohol is on, but all {} drink(s) carried are excluded by "
			          "the Include Healing/Top-Shelf/Irradiated Alcohol settings - none will be used"sv,
				shelfRejected);
		} else if (shelfRejected > 0 && Settings::debugLog) {
			REX::DEBUG("Smart Heal Button: [INVENTORY] {} drink(s) excluded by the Include Alcohol "
			           "settings, {} still eligible"sv,
				shelfRejected, alcoholAllowed);
		}

		if (Settings::debugLog) {
			REX::DEBUG("Smart Heal Button: [INVENTORY] {} usable stacks"sv, out.size());
			for (const auto& available : out) {
				REX::DEBUG("Smart Heal Button: [INVENTORY]   x{:<3} cat={:<15} hp={:>7.1f} rads={:>7.1f} limb={} value={} name={}"sv,
					available.count, ItemCatalog::CategoryName(available.Category()),
					available.hp, available.rads, available.mendsLimb, available.goldValue, available.Name());
			}
		}

		return out;
	}
}
