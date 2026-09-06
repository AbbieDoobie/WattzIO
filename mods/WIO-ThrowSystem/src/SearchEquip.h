#pragma once

#include <random>
#include <string>
#include <vector>

#include "Equip.h"
#include "Notify.h"
#include "Settings.h"

namespace TSO::SearchEquip
{
	// Equips a random throwable found anywhere in the player's inventory.
	//
	// Fallout 4's inventory is BGSInventoryList/BGSInventoryItem, not Skyrim's
	// ExtraContainerChanges: TESObjectREFR::inventoryList exposes a BSTArray<BGSInventoryItem>, and
	// BGSKeywordForm::HasKeywordString works natively without EditorIDPatch.h, because keyword
	// forms keep their EditorID.
	//
	// Builds separate throwable and mine candidate lists, coin-flips which to try first when both
	// have candidates, falls back to the other, then equips a random pick.
	//
	// The playable check reads the raw formFlags bit rather than TESForm::GetPlayable(), whose sign
	// convention is not obvious from the header.

	namespace detail
	{
		[[nodiscard]] inline std::vector<std::string> ParseKeywordList(std::string_view a_csv)
		{
			std::vector<std::string> result;
			std::size_t              start = 0;
			while (start <= a_csv.size()) {
				const auto comma = a_csv.find(',', start);
				const auto end = comma == std::string_view::npos ? a_csv.size() : comma;
				if (end > start) {
					result.emplace_back(a_csv.substr(start, end - start));
				}
				if (comma == std::string_view::npos) {
					break;
				}
				start = comma + 1;
			}
			return result;
		}

		[[nodiscard]] inline bool MatchesAnyKeyword(RE::TESObjectWEAP* a_weap, const std::vector<std::string>& a_keywords)
		{
			for (const auto& keyword : a_keywords) {
				if (a_weap->HasKeywordString(keyword)) {
					return true;
				}
			}
			return false;
		}

		// Bit 2 of formFlags is the vanilla "Non-Playable" record flag.
		[[nodiscard]] inline bool IsPlayable(const RE::TESForm* a_form)
		{
			return (a_form->GetFormFlags() & 0x4) == 0;
		}

		[[nodiscard]] inline std::vector<RE::TESForm*> CollectCandidates(RE::Actor* a_player, const std::vector<std::string>& a_keywords)
		{
			std::vector<RE::TESForm*> result;
			if (!a_player->inventoryList || a_keywords.empty()) {
				return result;
			}

			for (auto& item : a_player->inventoryList->data) {
				if (!item.object) {
					continue;
				}
				const auto weap = item.object->As<RE::TESObjectWEAP>();
				if (!weap || !MatchesAnyKeyword(weap, a_keywords)) {
					continue;
				}
				if (Settings::bAdvancedCheckPlayable.GetValue() && !IsPlayable(weap)) {
					continue;
				}
				// BGSInventoryItem::GetCount sums this entry's own stack chain. GetItemCount
				// would re-walk the whole inventory list for every candidate, inside a loop
				// already walking it.
				if (item.GetCount() > 0) {
					result.push_back(weap);
				}
			}
			return result;
		}

		[[nodiscard]] inline RE::TESForm* PickRandom(const std::vector<RE::TESForm*>& a_candidates)
		{
			if (a_candidates.empty()) {
				return nullptr;
			}
			static std::mt19937                    rng{ std::random_device{}() };
			std::uniform_int_distribution<std::size_t> dist{ 0, a_candidates.size() - 1 };
			return a_candidates[dist(rng)];
		}

		inline bool g_searching = false;
	}

	// Returns the form that was equipped, or nullptr on failure. The form itself rather than its
	// EditorID: an EditorID is incidental to whether an equip happened, so an item whose EditorID
	// does not resolve would otherwise report as a failure after being equipped.
	//
	// Both notifications are suppressible, because QuickSlots::CycleToNextSlot's Search-on-Fail
	// fallback shows its own combined $TSO_Note_CycleFailSearch* messages instead.
	inline RE::TESForm* FindAndEquipFirstThrowable(bool a_notifyOnFail = true, bool a_notifyOnSuccess = true)
	{
		if (detail::g_searching) {
			return nullptr;
		}
		detail::g_searching = true;

		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			detail::g_searching = false;
			return nullptr;
		}

		const auto throwables = detail::CollectCandidates(player, detail::ParseKeywordList(Settings::sGrenadeKeywordStrings.GetValue()));
		std::vector<RE::TESForm*> mines;
		if (Settings::bSearchIncludeMines.GetValue()) {
			mines = detail::CollectCandidates(player, detail::ParseKeywordList(Settings::sMineKeywordStrings.GetValue()));
		}

		bool checkMinesFirst = false;
		if (!throwables.empty() && !mines.empty()) {
			static std::mt19937               rng{ std::random_device{}() };
			std::uniform_int_distribution<int> coin{ 0, 1 };
			checkMinesFirst = coin(rng) == 1;
		} else if (!mines.empty()) {
			checkMinesFirst = true;
		}

		RE::TESForm* target = checkMinesFirst ? detail::PickRandom(mines) : detail::PickRandom(throwables);
		if (!target) {
			target = checkMinesFirst ? detail::PickRandom(throwables) : detail::PickRandom(mines);
		}

		if (!target) {
			REX::DEBUG("Throwing System Overhaul: search and equip - no throwable found"sv);
			if (a_notifyOnFail && Settings::bSearchNotifyOnFail.GetValue()) {
				Notify::Show(WIO::Translations::Localize(
					"$TSO_Note_FailSearchEquip"sv, "No throwable item found through Search"sv));
			}
			detail::g_searching = false;
			return nullptr;
		}

		Equip::Item(player, target);

		REX::DEBUG("Throwing System Overhaul: search and equip - equipped '{}'"sv,
			Notify::NameOf(target));
		if (a_notifyOnSuccess && Settings::bSearchNotifyOnEquip.GetValue()) {
			Notify::Show(Notify::Format(
				WIO::Translations::Localize("$TSO_Note_SearchEquipSuccess"sv, "Equipped: {ITEM}"sv),
				{ { "{ITEM}", Notify::CleanName(Notify::NameOf(target)) } }));
		}
		detail::g_searching = false;
		return target;
	}
}
