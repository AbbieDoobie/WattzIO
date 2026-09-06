#pragma once

#include "EditorIDPatch.h"
#include "Equip.h"
#include "Notify.h"
#include "SearchEquip.h"
#include "Settings.h"

namespace TSO::QuickSlots
{
	// The Quick Slot system. Slots store an item's EditorID, which needs EditorIDPatch.h for
	// TESObjectWEAP forms.
	//
	// UpdateSlot has two branches: a set slot equips its item after confirming it is still in
	// inventory, an empty slot saves whatever is currently in the throwable slot (equip index 2).
	//
	// CycleToNextSlot starts from whichever slot holds the equipped throwable and walks forward
	// with wraparound, equipping the first that resolves to a real, in-inventory item.
	//
	// bCycleSearchOnFail is gated by a_allowSearchFallback, which ThrowMelee.h's auto-equip
	// waterfall passes false because it has its own iThrowAutoEquipMode mechanism.

	namespace detail
	{
		[[nodiscard]] inline REX::TIniSetting<std::string>* GetSlotSetting(int a_slot)
		{
			switch (a_slot) {
			case 1:
				return &Settings::sQuickSwap1Ref;
			case 2:
				return &Settings::sQuickSwap2Ref;
			case 3:
				return &Settings::sQuickSwap3Ref;
			case 4:
				return &Settings::sQuickSwap4Ref;
			default:
				return nullptr;
			}
		}

		// The ini can carry leading spaces after '=' (e.g. "sQuickSwap1Ref= fragGrenade"), so
		// trim before an EditorID lookup, which would never match otherwise.
		[[nodiscard]] inline std::string Trim(std::string_view a_str)
		{
			const auto first = a_str.find_first_not_of(" \t");
			if (first == std::string_view::npos) {
				return std::string{};
			}
			const auto last = a_str.find_last_not_of(" \t");
			return std::string{ a_str.substr(first, last - first + 1) };
		}

		// The setting value does not equal the max slot count directly; the mapping is
		// inverted.
		[[nodiscard]] inline int GetMaxSlots()
		{
			switch (Settings::iQuickSwapCycleSlots.GetValue()) {
			case 1:
				return 3;
			case 2:
				return 4;
			default:
				return 2;  // minimum
			}
		}

		[[nodiscard]] inline RE::TESForm* ResolveSlotItem(int a_slot)
		{
			const auto setting = GetSlotSetting(a_slot);
			if (!setting) {
				return nullptr;
			}
			const auto ref = Trim(setting->GetValue());
			return ref.empty() ? nullptr : RE::TESForm::GetFormByEditorID(ref);
		}

		// Guards against re-entrant cycling.
		inline bool g_cycling = false;

		// Pushes a Quick Slot reference change into MCM's live settings cache. Without it MCM shows
		// the previous value until the next restart, because this plugin writes the ini directly and
		// MCM never re-reads it mid-session.
		//
		// MCM.SetModSettingString is a global Papyrus function under the "MCM" script name, so a
		// native plugin can invoke it through DispatchStaticCall with no Quest, Script or ESL of its
		// own. RefreshMenu() is not needed: these writes only happen while the menu is closed.
		inline void SyncRefToMCM(int a_slot, std::string_view a_value)
		{
			const auto game = RE::GameVM::GetSingleton();
			const auto vm = game ? game->GetVM() : nullptr;
			if (!vm) {
				return;
			}

			const RE::BSFixedString modName{ "WIO-ThrowSystem" };
			const RE::BSFixedString settingKey{ "sQuickSwap" + std::to_string(a_slot) + "Ref:QuickSwap" };
			const RE::BSFixedString value{ std::string{ a_value } };
			WIO::Papyrus::DispatchStaticCall(vm, "MCM", "SetModSettingString", nullptr, modName, settingKey, value);
		}
	}

	// Returns true only when the equip branch actually equipped something, which is what the
	// "Throw After Equipping" option (iQuickSwapSlotThrowMode) keys off - see
	// ThrowMelee::TryQuickSlotThrow. The save branch equips nothing and always returns false.
	inline bool UpdateSlot(int a_slot)
	{
		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto setting = detail::GetSlotSetting(a_slot);
		if (!player || !setting) {
			return false;
		}

		const auto ref = detail::Trim(setting->GetValue());

		if (!ref.empty()) {
			// Equip branch: resolve the stored EditorID, confirm it is still in inventory, then
			// equip it. Passing a null slot lets the engine pick the right one from the weapon's
			// own equip-type data.
			const auto form = RE::TESForm::GetFormByEditorID(ref);
			if (!form) {
				REX::DEBUG("Throwing System Overhaul: quick slot {} error - item not found for '{}'"sv, a_slot, ref);
				if (Settings::bQuickSwapNotifyOnEquip.GetValue()) {
					Notify::Show(Notify::Format(
						WIO::Translations::Localize("$TSO_Note_QuickSlotEquipError"sv, "Quick Slot {SLOT}: error - item not found"sv),
						{ { "{SLOT}", std::to_string(a_slot) } }));
				}
				return false;
			}

			if (Equip::GetCount(player, form) == 0) {
				REX::DEBUG("Throwing System Overhaul: quick slot {} empty - out of '{}'"sv, a_slot, ref);
				if (Settings::bQuickSwapNotifyOnEquip.GetValue()) {
					Notify::Show(Notify::Format(
						WIO::Translations::Localize("$TSO_Note_QuickSlotEquipEmpty"sv, "Quick Slot {SLOT}: out of {ITEM}"sv),
						{ { "{SLOT}", std::to_string(a_slot) }, { "{ITEM}", Notify::CleanName(Notify::NameOf(form)) } }));
				}
				return false;
			}

			Equip::Item(player, form);
			REX::DEBUG("Throwing System Overhaul: quick slot {} equipped '{}'"sv, a_slot, ref);
			if (Settings::bQuickSwapNotifyOnEquip.GetValue()) {
				Notify::Show(Notify::Format(
					WIO::Translations::Localize("$TSO_Note_QuickSlotEquipSuccess"sv, "Equipped Slot {SLOT}: {ITEM}"sv),
					{ { "{SLOT}", std::to_string(a_slot) }, { "{ITEM}", Notify::CleanName(Notify::NameOf(form)) } }));
			}
			return true;
		} else {
			// Save branch: nothing stored yet, so save whatever is currently equipped in the
			// throwable slot (equip index 2) into this slot.
			const auto result = player->GetEquippedItem(RE::BGSEquipIndex{ 2 });

			if (!result.object) {
				REX::DEBUG("Throwing System Overhaul: quick slot {} - no throwable equipped to save"sv, a_slot);
				if (Settings::bQuickSwapNotifyOnSave.GetValue()) {
					Notify::Show(Notify::Format(
						WIO::Translations::Localize("$TSO_Note_QuickSlotSaveNone"sv, "Quick Slot {SLOT}: no throwable equipped to save"sv),
						{ { "{SLOT}", std::to_string(a_slot) } }));
				}
				return false;
			}

			const std::string editorID = EditorIDPatch::EditorIDOf(result.object);
			if (editorID.empty()) {
				REX::DEBUG("Throwing System Overhaul: quick slot {} - equipped item has no EditorID, cannot save"sv, a_slot);
				if (Settings::bQuickSwapNotifyOnSave.GetValue()) {
					Notify::Show(Notify::Format(
						WIO::Translations::Localize("$TSO_Note_QuickSlotSaveError"sv, "Quick Slot {SLOT}: error - cannot save {ITEM}"sv),
						{ { "{SLOT}", std::to_string(a_slot) }, { "{ITEM}", Notify::CleanName(Notify::NameOf(result.object)) } }));
				}
				return false;
			}

			setting->SetValue(editorID);
			Settings::Save();
			detail::SyncRefToMCM(a_slot, editorID);
			REX::DEBUG("Throwing System Overhaul: quick slot {} saved '{}'"sv, a_slot, editorID);
			if (Settings::bQuickSwapNotifyOnSave.GetValue()) {
				Notify::Show(Notify::Format(
					WIO::Translations::Localize("$TSO_Note_QuickSlotSaveSuccess"sv, "Quick Slot {SLOT}: saved {ITEM}"sv),
					{ { "{SLOT}", std::to_string(a_slot) }, { "{ITEM}", Notify::CleanName(Notify::NameOf(result.object)) } }));
			}
		}
		return false;
	}

	// Maps the "Quick Slots - Clear on Hold" dropdown's option index to the hold duration it
	// represents. 0.0f means the feature is off.
	[[nodiscard]] inline float GetClearHoldThresholdSecs()
	{
		switch (Settings::iQuickSwapClearHoldMode.GetValue()) {
		case 1:
			return 10.0f;
		case 2:
			return 5.0f;
		case 3:
			return 2.0f;
		default:
			return 0.0f;  // Off
		}
	}

	// Empties a slot's stored reference, so the next press takes UpdateSlot's save branch
	// instead of needing the slot overwritten with a different item first.
	inline void ClearSlot(int a_slot)
	{
		const auto setting = detail::GetSlotSetting(a_slot);
		if (!setting || detail::Trim(setting->GetValue()).empty()) {
			return;
		}

		setting->SetValue("");
		Settings::Save();
		detail::SyncRefToMCM(a_slot, "");
		REX::DEBUG("Throwing System Overhaul: quick slot {} cleared (held)"sv, a_slot);
		if (Settings::bQuickSwapNotifyOnSave.GetValue()) {
			Notify::Show(Notify::Format(
				WIO::Translations::Localize("$TSO_Note_QuickSlotCleared"sv, "Quick Slot {SLOT}: cleared"sv),
				{ { "{SLOT}", std::to_string(a_slot) } }));
		}
	}

	// Which slot (1..max), if any, currently holds the given form's EditorID.
	[[nodiscard]] inline int GetCurrentSlot(RE::TESForm* a_currentItem)
	{
		if (!a_currentItem) {
			return 0;
		}
		const std::string currentID = EditorIDPatch::EditorIDOf(a_currentItem);
		if (currentID.empty()) {
			return 0;
		}

		const auto maxSlot = detail::GetMaxSlots();
		for (int slot = 1; slot <= maxSlot; ++slot) {
			const auto setting = detail::GetSlotSetting(slot);
			if (setting && detail::Trim(setting->GetValue()) == currentID) {
				return slot;
			}
		}
		return 0;
	}

	// Starting from whichever slot holds the equipped throwable (0 if none), walks forward with
	// wraparound, equipping the first that resolves to a real, in-inventory item.
	//
	// a_notifyOnFail is false when ThrowMelee.h's waterfall has both Quick Slots and Search
	// enabled, so one combined failure message shows instead. a_allowSearchFallback gates whether
	// bCycleSearchOnFail is consulted at all - true only for a direct Cycle-hotkey press.
	inline bool CycleToNextSlot(bool a_notifyOnFail = true, bool a_allowSearchFallback = true)
	{
		if (detail::g_cycling) {
			return false;
		}
		detail::g_cycling = true;

		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			detail::g_cycling = false;
			return false;
		}

		const auto current = player->GetEquippedItem(RE::BGSEquipIndex{ 2 });
		const int startSlot = GetCurrentSlot(current.object);

		const auto maxSlot = detail::GetMaxSlots();
		auto       nextSlot = startSlot + 1;

		for (int count = 1; count <= maxSlot; ++count) {
			if (nextSlot > maxSlot) {
				nextSlot = 1;
			}

			if (const auto candidate = detail::ResolveSlotItem(nextSlot); candidate && Equip::GetCount(player, candidate) > 0) {
				Equip::Item(player, candidate);
				REX::DEBUG("Throwing System Overhaul: quick swap cycle equipped slot {} ('{}')"sv, nextSlot, EditorIDPatch::EditorIDOf(candidate));
				if (Settings::bQuickSwapNotifyOnCycleSuccess.GetValue()) {
					Notify::Show(Notify::Format(
						WIO::Translations::Localize("$TSO_Note_CycleEquipSuccess"sv, "Equipped Slot {SLOT}: {ITEM}"sv),
						{ { "{SLOT}", std::to_string(nextSlot) }, { "{ITEM}", Notify::CleanName(Notify::NameOf(candidate)) } }));
				}
				detail::g_cycling = false;
				return true;
			}

			++nextSlot;
		}

		REX::DEBUG("Throwing System Overhaul: quick swap cycle - no valid throwable found in any slot"sv);

		if (a_allowSearchFallback && Settings::bCycleSearchOnFail.GetValue()) {
			// Suppress SearchEquip's own notifications and show the combined
			// $TSO_Note_CycleFailSearch* message instead, so the player can tell this came
			// from the Cycle hotkey's fallback rather than a direct Search and Equip press.
			const auto equipped = SearchEquip::FindAndEquipFirstThrowable(false, false);
			detail::g_cycling = false;

			if (equipped) {
				REX::DEBUG("Throwing System Overhaul: quick swap cycle - search fallback equipped '{}'"sv,
					Notify::NameOf(equipped));
				if (a_notifyOnFail && Settings::bQuickSwapNotifyOnCycleSuccess.GetValue()) {
					Notify::Show(Notify::Format(
						WIO::Translations::Localize("$TSO_Note_CycleFailSearchSuccess"sv, "No throwable in Quick Slots, Search has Equipped: {ITEM}"sv),
						{ { "{ITEM}", Notify::CleanName(Notify::NameOf(equipped)) } }));
				}
				return true;
			}

			REX::DEBUG("Throwing System Overhaul: quick swap cycle - search fallback also found nothing"sv);
			if (a_notifyOnFail && Settings::bQuickSwapNotifyOnCycleFail.GetValue()) {
				Notify::Show(WIO::Translations::Localize(
					"$TSO_Note_CycleFailSearchFail"sv, "No throwable found through Cycling or Search"sv));
			}
			return false;
		}

		if (a_notifyOnFail && Settings::bQuickSwapNotifyOnCycleFail.GetValue()) {
			Notify::Show(WIO::Translations::Localize(
				"$TSO_Note_FailCycle"sv, "No throwable item found in any Quick Slot"sv));
		}
		detail::g_cycling = false;
		return false;
	}
}
