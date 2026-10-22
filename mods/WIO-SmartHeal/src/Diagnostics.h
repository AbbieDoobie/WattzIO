#pragma once

#include <cstdint>
#include <string>

#include "Effects.h"
#include "GameMode.h"
#include "ItemCatalog.h"
#include "PlayerState.h"
#include "Settings.h"

// The measurement rig. Inert unless bDebugLog=1 is hand-added under [Advanced] in
// Data/MCM/Settings/WIO-SmartHeal.ini; MCM leaves keys it does not own alone.
//
//   DumpFull()  on every hotkey press: every actor value this mod touches with all four of its
//               readings side by side, plus every active effect with base and live values.
//   Tick()      every frame, printing only when the rendered line changes, which catches a
//               stimpak's five-second effect without the player pressing anything.
namespace SH::Diagnostics
{
	namespace detail
	{
		inline std::string s_lastTickLine;

		[[nodiscard]] inline const char* AVName(const RE::ActorValueInfo* a_av)
		{
			return a_av ? a_av->GetFullName() : "<null>";
		}

		// All four readings of one actor value on one line, so a change shows which it moves.
		inline void DumpAV(RE::PlayerCharacter& a_player, const char* a_label, const RE::ActorValueInfo* a_av)
		{
			if (!a_av) {
				REX::DEBUG("Smart Heal Button: [AV] {:<20} <null>"sv, a_label);
				return;
			}
			REX::DEBUG("Smart Heal Button: [AV] {:<20} current={:>9.2f} base={:>9.2f} permanent={:>9.2f} "
			          "mod(perm={:>8.2f} temp={:>8.2f} damage={:>8.2f})"sv,
				a_label,
				a_player.GetActorValue(*a_av),
				a_player.GetBaseActorValue(*a_av),
				a_player.GetPermanentActorValue(*a_av),
				a_player.GetModifier(RE::kPermanentModifier, *a_av),
				a_player.GetModifier(RE::kTemporaryModifier, *a_av),
				a_player.GetModifier(RE::kDamageModifier, *a_av));
		}

		// One active effect, the record's base magnitude and duration printed next to the live ones the
		// engine gave it. The difference between the columns is the perk effect: Medic adds to
		// magnitude, Chemist multiplies duration.
		inline void DumpActiveEffect(const RE::ActiveEffect& a_active, std::size_t a_index)
		{
			const auto item = a_active.effect;
			const auto setting = item ? item->effectSetting : nullptr;
			if (!setting) {
				REX::DEBUG("Smart Heal Button: [FX{}] <no effect setting>"sv, a_index);
				return;
			}

			const auto baseMagnitude = item->data.magnitude;
			const auto baseDuration = static_cast<float>(item->data.duration);
			const auto liveMagnitude = a_active.magnitude;
			const auto liveDuration = a_active.duration;

			const auto baseTotal = baseMagnitude * std::max(baseDuration, 1.0F);
			const auto liveTotal = liveMagnitude * std::max(liveDuration, 1.0F);

			REX::DEBUG("Smart Heal Button: [FX{}] {:08X} arch={} pav={} | base mag={:.3f} dur={:.1f} total={:.2f}"
			          " | LIVE mag={:.3f} dur={:.1f} total={:.2f} | delta mag={:+.3f} dur={:+.1f} totalx{:.3f}"
			          " | elapsed={:.2f} source={} name={}"sv,
				a_index, setting->GetFormID(),
				std::to_underlying(setting->data.archetype.get()),
				AVName(setting->data.primaryAV),
				baseMagnitude, baseDuration, baseTotal,
				liveMagnitude, liveDuration, liveTotal,
				liveMagnitude - baseMagnitude, liveDuration - baseDuration,
				baseTotal != 0.0F ? liveTotal / baseTotal : 0.0F,
				a_active.elapsedSeconds,
				a_active.source ? a_active.source->GetFormID() : 0u,
				setting->GetFullName());
		}
	}

	// The full labelled block. Fires on every hotkey press while bDebugLog is on.
	inline void DumpFull(const char* a_reason)
	{
		if (!Settings::debugLog) {
			return;
		}

		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player || !Effects::Resolve()) {
			REX::DEBUG("Smart Heal Button: [DUMP] player or actor values unavailable"sv);
			return;
		}

		REX::DEBUG("Smart Heal Button: [DUMP] ======== begin ({}) ========"sv, a_reason);

		// -- Which reading of Health radiation moves, and the ceiling on Rads.
		detail::DumpAV(*player, "Health", Effects::HealthAV());
		detail::DumpAV(*player, "Rads", Effects::RadsAV());
		detail::DumpAV(*player, "RadHealthMax", Effects::RadHealthMaxAV());

		// -- Radiation resistance. Lead Belly and Rad Resistant work through these rather than a perk
		//    entry point, so an irradiated food's real rad gain is whatever survives them.
		if (const auto av = RE::ActorValue::GetSingleton(); av) {
			detail::DumpAV(*player, "RadResistIngestion", av->radIngestionResistance);
			detail::DumpAV(*player, "RadResistExposure", av->radExposureResistance);
		}

		// -- Limbs, so "crippled" can be checked against what the HUD shows.
		static constexpr std::array kLimbLabels{
			"Limb Head", "Limb Torso", "Limb LeftArm", "Limb RightArm",
			"Limb LeftLeg", "Limb RightLeg", "Limb Brain"
		};
		const auto& limbs = Effects::LimbAVs();
		for (std::size_t i = 0; i < limbs.size(); ++i) {
			detail::DumpAV(*player, kLimbLabels[i], limbs[i]);
		}

		// -- What the mod computes from the above, so a wrong assumption disagrees with the HUD.
		{
			const auto state = PlayerState::Read();
			REX::DEBUG("Smart Heal Button: [DERIVED] hpTarget={:.2f} (+/-{:.2f}) radTarget={:.2f} (+/-{:.2f}) radScale={:.0f}"sv,
				PlayerState::HPTargetValue(state.ProjectedReachableHP()),
				PlayerState::HPToleranceValue(state.ProjectedReachableHP()),
				PlayerState::RadTargetValue(state),
				PlayerState::RadToleranceValue(state),
				state.radScale);
		}

		// -- Game mode, then conditional effects and whether they currently pass. A perk-gated healing
		//    value read as zero drops that item from every plan, and nothing else logs it.
		{
			// Difficulty and every Survival rule. Each rule is a separate global that vanilla
			// records condition themselves on, and mods toggle them independently of the
			// difficulty setting, so both are worth seeing side by side.
			REX::DEBUG("Smart Heal Button: [MODE] difficulty={} ({})"sv,
				std::to_underlying(GameMode::Difficulty()),
				GameMode::DifficultyName(GameMode::Difficulty()));
			for (const auto& rule : GameMode::kRules) {
				REX::DEBUG("Smart Heal Button: [MODE]   {:<32} = {:.1f}"sv, rule.editorID, GameMode::RuleValue(rule));
			}

			if (player->inventoryList) {
				const RE::BSAutoReadLock lock{ player->inventoryList->rwLock };
				for (auto& item : player->inventoryList->data) {
					const auto entry = ItemCatalog::Find(item.object);
					if (!entry || entry->conditional.empty()) {
						continue;
					}
					std::size_t passed = 0;
					for (const auto& conditional : entry->conditional) {
						if (conditional.effect && Effects::ConditionsPass(*conditional.effect, player)) {
							++passed;
						}
					}
					REX::DEBUG("Smart Heal Button: [COND] {}/{} pass  canMendLimb={}  {}"sv,
						passed, entry->conditional.size(), entry->canMendLimb, entry->item->GetFullName());
				}
			}
		}

		// -- Every running effect, base against live. One stimpak plus one press inside the next
		//    five seconds measures Medic outright.
		if (const auto active = player->GetActiveEffectList(); active) {
			REX::DEBUG("Smart Heal Button: [FX] {} active effect(s)"sv, active->data.size());
			std::size_t index = 0;
			for (const auto& effect : active->data) {
				if (effect) {
					detail::DumpActiveEffect(*effect, index);
				}
				++index;
			}
		} else {
			REX::DEBUG("Smart Heal Button: [FX] active effect list unavailable"sv);
		}

		REX::DEBUG("Smart Heal Button: [DUMP] ======== end ========"sv);
	}

	// Runs every frame while bDebugLog is on and prints only on change, so a stimpak's effect
	// appears the frame it starts with its live magnitude and duration.
	inline void Tick()
	{
		if (!Settings::debugLog) {
			return;
		}

		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player || !Effects::Resolve()) {
			return;
		}

		const auto health = Effects::HealthAV();
		const auto rads = Effects::RadsAV();
		const auto radHealthMax = Effects::RadHealthMaxAV();
		if (!health || !rads) {
			return;
		}

		const auto maxHP = player->GetPermanentActorValue(*health);
		const auto curHP = player->GetActorValue(*health);
		const auto curRads = player->GetActorValue(*rads);

		// Almost every health effect in the game arrives over 5 or 10 seconds (182 of 187 vanilla
		// value modifiers on Health), so current health alone says nothing about whether the
		// player still needs an item. `projected` is current plus everything already in flight,
		// and is what the solver aims from: at the moment an effect starts it should already equal
		// the health the bar reaches when it finishes climbing.
		float pendingHP = 0.0F;
		float pendingRads = 0.0F;
		std::string effectList;

		if (const auto active = player->GetActiveEffectList(); active) {
			for (const auto& effect : active->data) {
				if (!effect || !effect->effect || !effect->effect->effectSetting) {
					continue;
				}
				const auto pending = Effects::Pending(*effect);
				// hpPercent is not read: the live path never produces one, because the engine has
				// already converted a stimpak's percentage to absolute HP.
				pendingHP += pending.hpFlat;
				pendingRads += pending.rads;

				// Magnitude and duration are part of the change key, so a perk-modified effect
				// whose live values differ from its record cannot be missed.
				effectList += std::format(" [{:08X} m={:.2f} d={:.1f}]",
					effect->effect->effectSetting->GetFormID(), effect->magnitude, effect->duration);
			}
		}

		// The ceiling comes from the engine's RadHealthMax, not from a formula.
		const auto ceiling = radHealthMax ? player->GetActorValue(*radHealthMax) : maxHP;

		// Rounded to whole points: unrounded values drift by fractions every frame during
		// regeneration and would print constantly.
		auto line = std::format("hp={:.0f}/{:.0f} ceiling={:.0f} ({:+.0f} pending -> projected {:.0f}) rads={:.0f} ({:+.0f} pending){}",
			curHP, maxHP, ceiling, pendingHP, std::min(curHP + pendingHP, ceiling),
			curRads, pendingRads, effectList);

		if (line != detail::s_lastTickLine) {
			REX::DEBUG("Smart Heal Button: [TICK] {}"sv, line);
			detail::s_lastTickLine = std::move(line);
		}
	}
}
