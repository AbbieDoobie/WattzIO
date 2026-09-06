#pragma once

#include <chrono>
#include <functional>
#include <random>
#include <unordered_map>
#include <vector>

#include "EditorIDPatch.h"
#include "Equip.h"
#include "Notify.h"
#include "QuickSlots.h"
#include "SearchEquip.h"
#include "Settings.h"

namespace TSO::RestockOnKill
{
	// Restock on Kill: grants a throwable when the player kills something.
	//
	// RE::TESDeathEvent's actorDying and actorKiller identify the kill.
	// TESDataHandler::GetFormArray<T>() enumerates every loaded form of a type, used once and cached
	// to build the list of every throwable and mine matching the configured keywords, which the
	// Random type needs because it can grant something the player does not have. Keyword matching
	// comes from SearchEquip and slot resolution from QuickSlots.
	//
	// bKillBonusDeferToProximity defers the same reward, rolled once at kill time, until the player
	// walks near the corpse. Two cleanup paths bound the pending list: time-based expiry
	// (iKillBonusProximityTimeoutSecs), which bounds size by kill rate times max age rather than by
	// playtime, and handle invalidation. A same-cell check runs before the distance check, because a
	// stale but resolvable corpse elsewhere could false-positive on raw coordinates.
	//
	// The list is not persisted. The proximity check piggybacks on the input hook's per-frame call,
	// throttled to roughly twice a second, and is a plain distance check.

	namespace detail
	{
		inline std::vector<RE::TESForm*> g_cachedThrowables;
		inline std::vector<RE::TESForm*> g_cachedMines;
		inline bool                      g_cacheBuilt = false;
		// The cache must invalidate when the Grenade/Mine Keyword Strings change on the Advanced
		// page, or the Random restock pool would ignore an edit until restart. This tracks the
		// keyword strings the cache was last built from and rebuilds only when they change,
		// checked lazily here as a string comparison rather than through a separate
		// invalidation hook.
		inline std::string g_cachedGrenadeKeywordString;
		inline std::string g_cachedMineKeywordString;

		inline void BuildCacheIfNeeded()
		{
			const auto grenadeKeywordString = Settings::sGrenadeKeywordStrings.GetValue();
			const auto mineKeywordString = Settings::sMineKeywordStrings.GetValue();

			if (g_cacheBuilt && grenadeKeywordString == g_cachedGrenadeKeywordString && mineKeywordString == g_cachedMineKeywordString) {
				return;
			}
			g_cacheBuilt = true;
			g_cachedGrenadeKeywordString = grenadeKeywordString;
			g_cachedMineKeywordString = mineKeywordString;
			g_cachedThrowables.clear();
			g_cachedMines.clear();

			const auto grenadeKeywords = SearchEquip::detail::ParseKeywordList(grenadeKeywordString);
			const auto mineKeywords = SearchEquip::detail::ParseKeywordList(mineKeywordString);

			const auto dataHandler = RE::TESDataHandler::GetSingleton();
			if (!dataHandler) {
				return;
			}
			for (auto* weap : dataHandler->GetFormArray<RE::TESObjectWEAP>()) {
				if (!weap) {
					continue;
				}
				if (SearchEquip::detail::MatchesAnyKeyword(weap, grenadeKeywords)) {
					g_cachedThrowables.push_back(weap);
				}
				if (SearchEquip::detail::MatchesAnyKeyword(weap, mineKeywords)) {
					g_cachedMines.push_back(weap);
				}
			}

			REX::INFO("Throwing System Overhaul: restock-on-kill cache built - {} throwable(s), {} mine(s)"sv, g_cachedThrowables.size(), g_cachedMines.size());
		}

		[[nodiscard]] inline bool IsPlayable(const RE::TESForm* a_form)
		{
			return (a_form->GetFormFlags() & 0x4) == 0;
		}

		// One pass over the player's inventory, keyed by base form. The Random pool is every
		// throwable in the load order, so a per-candidate GetItemCount would walk the whole
		// inventory once per candidate.
		[[nodiscard]] inline std::unordered_map<const RE::TESForm*, std::uint32_t> BuildInventoryCounts(RE::Actor* a_player)
		{
			std::unordered_map<const RE::TESForm*, std::uint32_t> counts;
			if (!a_player || !a_player->inventoryList) {
				return counts;
			}
			for (auto& item : a_player->inventoryList->data) {
				if (item.object) {
					counts[item.object] += item.GetCount();
				}
			}
			return counts;
		}

		[[nodiscard]] inline std::uint32_t CountIn(
			const std::unordered_map<const RE::TESForm*, std::uint32_t>& a_counts, const RE::TESForm* a_form)
		{
			const auto it = a_counts.find(a_form);
			return it != a_counts.end() ? it->second : 0;
		}

		[[nodiscard]] inline RE::TESForm* PickRandom(const std::vector<RE::TESForm*>& a_candidates)
		{
			if (a_candidates.empty()) {
				return nullptr;
			}
			static std::mt19937                         rng{ std::random_device{}() };
			std::uniform_int_distribution<std::size_t> dist{ 0, a_candidates.size() - 1 };
			return a_candidates[dist(rng)];
		}

		// Pulls from the global cache of every throwable and mine in the game rather than from
		// inventory, so it can grant something the player does not currently have.
		[[nodiscard]] inline RE::TESForm* GetRandomThrowableFromKeywords(RE::Actor* a_player, bool a_checkLimit, int a_limit)
		{
			BuildCacheIfNeeded();

			const auto counts = BuildInventoryCounts(a_player);
			const bool checkPlayable = Settings::bAdvancedCheckPlayable.GetValue();

			std::vector<RE::TESForm*> valid;
			const auto                considerCandidate = [&](RE::TESForm* a_candidate) {
                if (checkPlayable && !IsPlayable(a_candidate)) {
                    return;
                }
                if (a_checkLimit && a_limit > 0 && CountIn(counts, a_candidate) >= static_cast<std::uint32_t>(a_limit)) {
                    return;
                }
                valid.push_back(a_candidate);
			};

			for (auto* form : g_cachedThrowables) {
				considerCandidate(form);
			}
			if (Settings::bKillBonusIncludeMines.GetValue()) {
				for (auto* form : g_cachedMines) {
					considerCandidate(form);
				}
			}

			return PickRandom(valid);
		}

		// Only among the four configured Quick Slots.
		[[nodiscard]] inline RE::TESForm* GetRandomThrowableFromQuickSlots(RE::Actor* a_player, bool a_checkLimit, int a_limit)
		{
			std::vector<RE::TESForm*> valid;
			for (int slot = 1; slot <= 4; ++slot) {
				if (const auto item = QuickSlots::detail::ResolveSlotItem(slot); item) {
					if (!a_checkLimit || a_limit <= 0 || Equip::GetCount(a_player, item) < static_cast<std::uint32_t>(a_limit)) {
						valid.push_back(item);
					}
				}
			}
			return PickRandom(valid);
		}

		inline void GiveItem(RE::Actor* a_player, RE::TESForm* a_item, std::int32_t a_count)
		{
			const auto bound = a_item->As<RE::TESBoundObject>();
			if (!bound) {
				return;
			}
			// AddObjectToContainer fires the engine's own "item added" HUD message and pickup
			// audio whenever the container is the player, and takes no silent argument.
			// ScopedInventoryChangeMessageContext is the engine's own RAII gate
			// for this: construct it, do the add, let it fall out of scope. Message and audio
			// are independent channels on it, so the sound toggle is orthogonal to the message
			// mode.
			const auto mode = Settings::iKillBonusNotify.GetValue();
			// Mode 0 (Off) suppresses neither channel, which falls out of the two constants
			// below.
			constexpr int kModeMinimal = 1;  // engine's own message, the closest thing to vanilla
			constexpr int kModeFullText = 2;  // ours instead
			{
				const RE::PlayerCharacter::ScopedInventoryChangeMessageContext suppress{
					mode != kModeMinimal,                    // suppressMessages
					!Settings::bKillBonusSound.GetValue()    // suppressAudio
				};
				a_player->AddObjectToContainer(bound, RE::BSTSmartPointer<RE::ExtraDataList>{}, a_count, nullptr, RE::ITEM_REMOVE_REASON::kNone);
			}
			if (mode == kModeFullText) {
				Notify::Show(Notify::Format(
					WIO::Translations::Localize("$TSO_Note_KillBonusGiven"sv, "Restocked: {ITEM}"sv),
					{ { "{ITEM}", Notify::CleanName(Notify::NameOf(a_item)) } }));
			}
		}

		inline void RefillAllQuickSlots(RE::Actor* a_player, int a_limit)
		{
			for (int slot = 1; slot <= 4; ++slot) {
				if (const auto item = QuickSlots::detail::ResolveSlotItem(slot); item) {
					const auto current = static_cast<int>(Equip::GetCount(a_player, item));
					if (current < a_limit) {
						GiveItem(a_player, item, a_limit - current);
					}
				}
			}
		}

		// Whether any Quick Slot has something assigned, independent of inventory count.
		[[nodiscard]] inline bool AnyQuickSlotAssigned()
		{
			for (int slot = 1; slot <= 4; ++slot) {
				if (QuickSlots::detail::ResolveSlotItem(slot)) {
					return true;
				}
			}
			return false;
		}

		// Rolls the reward decision and returns it as a deferred action rather than performing
		// it, so the instant-give and proximity-deferred paths share one implementation.
		// Returns an empty std::function if nothing would be granted.
		[[nodiscard]] inline std::function<void(RE::Actor*)> RollReward(RE::Actor* a_victim, RE::Actor* a_player)
		{
			static std::mt19937                rng{ std::random_device{}() };
			std::uniform_int_distribution<int> roll{ 1, 100 };

			const auto npc = a_victim->GetNPC();
			const bool isLegendary = npc && npc->HasKeywordString("EncTypeLegendary"sv);

			if (isLegendary && roll(rng) <= Settings::iKillBonusLegendaryChance.GetValue()) {
				// With no Quick Slot assigned, a legendary refill would be a no-op, so fall
				// through to the regular per-kill roll below as if this had not rolled as a
				// legendary kill.
				if (AnyQuickSlotAssigned()) {
					const auto limit = Settings::iKillBonusLimitCount.GetValue();
					return [limit](RE::Actor* a_recipient) {
						RefillAllQuickSlots(a_recipient, limit);
						REX::DEBUG("Throwing System Overhaul: restock on kill - legendary bonus, refilled quick slots"sv);
					};
				}
			}

			if (roll(rng) > Settings::iKillBonusChance.GetValue()) {
				return {};
			}

			const auto limitEnabled = Settings::bKillBonusLimitEnabled.GetValue();
			const auto limit = Settings::iKillBonusLimitCount.GetValue();

			RE::TESForm* toGive = nullptr;
			switch (Settings::iKillBonusType.GetValue()) {
			case 0:  // Quick Swap Slots Only
				toGive = GetRandomThrowableFromQuickSlots(a_player, limitEnabled, limit);
				break;
			case 1:  // Random (any throwable/mine)
				toGive = GetRandomThrowableFromKeywords(a_player, limitEnabled, limit);
				break;
			case 2:  // Quick Swap Slots, fallback Random
				toGive = GetRandomThrowableFromQuickSlots(a_player, limitEnabled, limit);
				if (!toGive) {
					toGive = GetRandomThrowableFromKeywords(a_player, limitEnabled, limit);
				}
				break;
			default:
				break;
			}

			if (!toGive) {
				return {};
			}
			return [toGive](RE::Actor* a_recipient) {
				GiveItem(a_recipient, toGive, 1);
				REX::DEBUG("Throwing System Overhaul: restock on kill - gave '{}'"sv, EditorIDPatch::EditorIDOf(toGive));
			};
		}

		struct PendingReward
		{
			// ObjectRefHandle, not ActorHandle: the BSPointerHandle(Y*) raw-pointer constructor
			// does not compile here (it calls get_handle(a_rhs) against a 0-arg get_handle()),
			// while TESObjectREFR::GetHandle() works and is enough, since only GetPosition() is
			// used on resolve.
			RE::ObjectRefHandle                   victim;
			std::function<void(RE::Actor*)>       grant;
			std::chrono::steady_clock::time_point created;
		};

		inline std::vector<PendingReward> g_pending;

		constexpr float kProximityRadius = 200.0f;  // game units, roughly a couple of body-lengths
	}

	// Legendary-victim roll first, which refills all Quick Slots on success. Otherwise a second
	// independent roll decides whether to grant one random throwable, picked per iKillBonusType
	// (Quick Slots only, global random, or Quick Slots with random fallback).
	//
	// With bKillBonusDeferToProximity on, the same roll happens at kill time, so drop rates are
	// unaffected, but the grant waits until Update() detects the player near the corpse.
	inline void OnKill(RE::Actor* a_victim)
	{
		if (!Settings::bKillBonusEnabled.GetValue() || !a_victim) {
			return;
		}
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		auto grant = detail::RollReward(a_victim, player);
		if (!grant) {
			return;
		}

		if (Settings::bKillBonusDeferToProximity.GetValue()) {
			detail::g_pending.push_back({ a_victim->GetHandle(), std::move(grant), std::chrono::steady_clock::now() });
		} else {
			grant(player);
		}
	}

	// Piggybacks on the input hook's per-frame call rather than adding a second polling
	// mechanism. Throttled internally, so this is not a per-frame check.
	inline void Update()
	{
		if (detail::g_pending.empty()) {
			return;
		}

		static int frameCounter = 0;
		if (++frameCounter < 30) {  // ~twice a second at 60fps - proximity doesn't need frame precision
			return;
		}
		frameCounter = 0;

		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		const auto playerPos = player->GetPosition();
		const auto playerCell = player->GetParentCell();
		const auto now = std::chrono::steady_clock::now();
		const auto maxAge = std::chrono::seconds{ Settings::iKillBonusProximityTimeoutSecs.GetValue() };

		std::erase_if(detail::g_pending, [&](detail::PendingReward& a_pending) {
			const auto victim = a_pending.victim.get();
			if (!victim) {
				return true;  // corpse no longer resolvable - drop it
			}
			if (now - a_pending.created > maxAge) {
				return true;  // timed out - drop it, regardless of distance
			}
			// Guards against a stale but still-resolvable corpse in an unrelated cell, whose
			// local coordinates could numerically overlap the player's and false-positive on
			// raw distance.
			if (victim->GetParentCell() != playerCell) {
				return false;  // different cell/zone - not in proximity, keep waiting
			}
			if ((victim->GetPosition() - playerPos).Length() <= detail::kProximityRadius) {
				a_pending.grant(player);
				return true;  // granted - drop it
			}
			return false;  // still pending, keep it
		});
	}

	// Filters TESDeathEvent down to a real death where the player was the killer.
	class DeathSink :
		public RE::BSTEventSink<RE::TESDeathEvent>
	{
	public:
		static void Install()
		{
			const auto source = RE::TESDeathEvent::GetEventSource();
			if (!source) {
				REX::WARN("Throwing System Overhaul: death event source unavailable - restock on kill inactive"sv);
				return;
			}
			static DeathSink singleton;
			source->RegisterSink(&singleton);
			REX::INFO("Throwing System Overhaul: restock-on-kill death event sink installed"sv);
		}

	private:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent& a_event, RE::BSTEventSource<RE::TESDeathEvent>*) override
		{
			const auto player = RE::PlayerCharacter::GetSingleton();
			if (a_event.dead && player && a_event.actorKiller.get() == player) {
				if (const auto victim = a_event.actorDying ? a_event.actorDying->As<RE::Actor>() : nullptr; victim) {
					OnKill(victim);
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	// Discards the whole pending list on any save load. ObjectRefHandle's generation check
	// reliably detects a stale or reused handle within one continuous session, but is not
	// documented as holding across a save load, so nothing is left to misresolve. A pending
	// Restock reward from before the load is forgotten, which matches the not-persisted
	// design.
	class LoadGameSink :
		public RE::BSTEventSink<RE::TESLoadGameEvent>
	{
	public:
		static void Install()
		{
			const auto source = RE::TESLoadGameEvent::GetEventSource();
			if (!source) {
				REX::WARN("Throwing System Overhaul: load-game event source unavailable - pending kill rewards will not be discarded on load"sv);
				return;
			}
			static LoadGameSink singleton;
			source->RegisterSink(&singleton);
			REX::INFO("Throwing System Overhaul: restock-on-kill load-game sink installed"sv);
		}

	private:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESLoadGameEvent&, RE::BSTEventSource<RE::TESLoadGameEvent>*) override
		{
			if (!detail::g_pending.empty()) {
				REX::INFO("Throwing System Overhaul: save loaded - discarding {} pending kill reward(s)"sv, detail::g_pending.size());
				detail::g_pending.clear();
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}
