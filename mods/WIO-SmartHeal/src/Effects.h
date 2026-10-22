#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

// Reduces the engine's magic-effect model to the three axes this mod reasons about: flat health,
// health as a percent of maximum, and radiation. Shared by ItemCatalog.h (what an item would do)
// and PlayerState.h (what a running effect still has to deliver).
//
// The record side and the live side are not in the same units:
//
//                        EffectItem (the record)          ActiveEffect (running on the player)
//   magnitude sign       always positive; the             already signed - a restore on a
//                        kDetrimental flag says which     damage-type actor value reads negative
//                        direction it goes
//   kStimpak magnitude   a percent of max health per      absolute HP per second - the engine has
//                        second (vanilla Stimpak: 6.0)    already done the conversion
//   perks applied?       no                               yes, fully
//
// Applying the record-side rules to a live effect double-negates the sign and multiplies the
// stimpak by max health a second time, so there are two functions below rather than one.
namespace SH::Effects
{
	struct Contribution
	{
		float hpFlat = 0.0F;     // absolute HP restored (negative = damage)
		float hpPercent = 0.0F;  // percent of max HP restored (kStimpak archetype, record side only)
		float rads = 0.0F;       // radiation added (negative = removed)
		bool  mendsLimb = false;

		// True when this effect does something outside the three axes: a stat buff, a Fortify, an AP
		// restore, a script. Record side only, and not part of IsMeaningful(), so callers still skip a
		// buff-only item for arithmetic. ItemCatalog::Build reads this before that skip, so such an
		// item is still marked special.
		bool otherEffect = false;
	};

	inline void Accumulate(Contribution& a_into, const Contribution& a_from)
	{
		a_into.hpFlat += a_from.hpFlat;
		a_into.hpPercent += a_from.hpPercent;
		a_into.rads += a_from.rads;
		a_into.mendsLimb = a_into.mendsLimb || a_from.mendsLimb;
		a_into.otherEffect = a_into.otherEffect || a_from.otherEffect;
	}

	[[nodiscard]] inline bool IsMeaningful(const Contribution& a_c)
	{
		return a_c.mendsLimb || a_c.hpFlat != 0.0F || a_c.hpPercent != 0.0F || a_c.rads != 0.0F;
	}

	// The ActorValueInfo forms this mod compares against, resolved once then matched by pointer.
	// Resolve() is retried until the singleton is up, so it is safe to call repeatedly.
	namespace detail
	{
		struct AVCache
		{
			const RE::ActorValueInfo*                health = nullptr;
			const RE::ActorValueInfo*                rads = nullptr;
			const RE::ActorValueInfo*                radHealthMax = nullptr;
			std::array<const RE::ActorValueInfo*, 7> limbs{};
			// The seven SPECIAL stats, and only those. Used by the alcohol shelf rule: Health, Rads,
			// and Action Points do not count, so Vodka's +25 Health and Wine's +15 AP do not make
			// either drink top shelf.
			std::array<const RE::ActorValueInfo*, 7> special{};
			bool                                     valid = false;
		};
		inline AVCache g_av;
	}

	inline bool Resolve()
	{
		if (detail::g_av.valid) {
			return true;
		}

		const auto av = RE::ActorValue::GetSingleton();
		if (!av) {
			REX::WARN("Smart Heal Button: ActorValue singleton unavailable"sv);
			return false;
		}

		detail::g_av.health = av->health;
		detail::g_av.rads = av->rads;
		detail::g_av.radHealthMax = av->radHealthMax;
		// The engine's own six limb conditions plus Brain. `leftMobiltyCondition` is spelled that
		// way in CommonLibF4RD itself.
		detail::g_av.limbs = {
			av->perceptionCondition,     // head
			av->enduranceCondition,      // torso
			av->leftAttackCondition,     // left arm
			av->rightAttackCondition,    // right arm
			av->leftMobiltyCondition,    // left leg
			av->rightMobilityCondition,  // right leg
			av->brainCondition
		};
		detail::g_av.special = {
			av->strength,
			av->perception,
			av->endurance,
			av->charisma,
			av->intelligence,
			av->agility,
			av->luck
		};
		detail::g_av.valid = detail::g_av.health != nullptr && detail::g_av.rads != nullptr;
		return detail::g_av.valid;
	}

	[[nodiscard]] inline const RE::ActorValueInfo* HealthAV() { return detail::g_av.health; }
	[[nodiscard]] inline const RE::ActorValueInfo* RadsAV() { return detail::g_av.rads; }
	[[nodiscard]] inline const RE::ActorValueInfo* RadHealthMaxAV() { return detail::g_av.radHealthMax; }
	[[nodiscard]] inline const auto&               LimbAVs() { return detail::g_av.limbs; }

	[[nodiscard]] inline bool IsLimbAV(const RE::ActorValueInfo* a_av)
	{
		return a_av && std::ranges::find(detail::g_av.limbs, a_av) != detail::g_av.limbs.end();
	}

	// An effect can be gated in two places: EffectItem::conditions holds the per-item conditions
	// xEdit shows under the effect on the ALCH record, and EffectSetting::conditions holds those on
	// the magic effect itself, shared by every item using it. The vanilla Stimpak's
	// RestoreRadsCompanion effect (-1000 rads) carries none on the ALCH record and two on its
	// EffectSetting, restricting it to a companion, so reading only item-level conditions makes
	// every stimpak look like it scrubs 1000 rads.
	[[nodiscard]] inline bool IsConditioned(const RE::EffectItem& a_effect)
	{
		if (a_effect.conditions.head) {
			return true;
		}
		return a_effect.effectSetting && a_effect.effectSetting->conditions.head;
	}

	[[nodiscard]] inline bool ConditionsPass(const RE::EffectItem& a_effect, RE::TESObjectREFR* a_player)
	{
		if (a_effect.conditions.head && !a_effect.conditions.IsTrue(a_player, a_player)) {
			return false;
		}
		if (a_effect.effectSetting && a_effect.effectSetting->conditions.head &&
			!a_effect.effectSetting->conditions.IsTrue(a_player, a_player)) {
			return false;
		}
		return true;
	}

	[[nodiscard]] inline bool IsSpecialAV(const RE::ActorValueInfo* a_av)
	{
		return a_av && std::ranges::find(detail::g_av.special, a_av) != detail::g_av.special.end();
	}

	// The ESM condition-function index for HasPerk. SCRIPT_OUTPUT does not name it, so this is a
	// number rather than an enumerator - but it is a number from the file format, which is fixed
	// across runtimes, not a code address. In Fallout4.esm's CTDA records,
	// every vanilla alcohol carries `fn448(PartyBoy03)` and `fn448(PerkMagLiveNLove05)`.
	//
	// PerkParamIsSane below confirms the condition's first parameter really is a perk form before
	// the effect is treated as perk-gated, so a wrong index reads as "not perk-gated". The
	// catalogue's top-shelf count in the log is what shows that happening.
	inline constexpr std::uint16_t kHasPerkFunction = 448;

	namespace detail
	{
		// Only dereferenced once the function index says param[0] is a perk pointer.
		[[nodiscard]] inline bool PerkParamIsSane(const RE::TESConditionItem& a_item)
		{
			const auto param = static_cast<const RE::TESForm*>(a_item.data.functionData.param[0]);
			return param && param->GetFormType() == RE::ENUM_FORM_ID::kPERK;
		}

		[[nodiscard]] inline bool MentionsPerk(const RE::TESCondition& a_conditions)
		{
			for (auto item = a_conditions.head; item; item = item->next) {
				if (item->data.functionData.function.underlying() == kHasPerkFunction &&
					PerkParamIsSane(*item)) {
					return true;
				}
			}
			return false;
		}
	}

	// Whether an effect only applies to a player who has some perk. Checked at both levels, for the
	// same reason IsConditioned is: the two Luck buffs every vanilla drink carries are gated on the
	// EffectSetting, not on the item.
	//
	// Narrower than IsConditioned on purpose: Fire Belly's health-gated buff and any Survival-gated
	// one still count. Only perks are the player's own loadout; everything else is a property of
	// the drink.
	[[nodiscard]] inline bool IsPerkGated(const RE::EffectItem& a_effect)
	{
		if (detail::MentionsPerk(a_effect.conditions)) {
			return true;
		}
		return a_effect.effectSetting && detail::MentionsPerk(a_effect.effectSetting->conditions);
	}

	// The base magnitude of a beneficial SPECIAL stat buff, or 0 for anything else. Record side, so
	// perks that scale a drink (Party Boy rank 2 doubles alcohol's effects) are absent by
	// construction: the engine applies those when the item is used and they appear nowhere here.
	//
	// Not multiplied by duration, unlike Reduce(): a Fortify's magnitude is the "+1", and a
	// three-minute beer is not a bigger buff than a one-minute one.
	[[nodiscard]] inline float SpecialBuff(const RE::EffectItem& a_effect)
	{
		const auto setting = a_effect.effectSetting;
		if (!setting) {
			return 0.0F;
		}

		const auto& data = setting->data;
		// Magnitudes are unsigned on the record and the flag carries the direction, so this is what
		// drops the -1 Intelligence every vanilla drink has, Dirty Wastelander's -2 and Nuka-Cola
		// Dark's -1 Perception.
		if (data.flags.any(RE::EffectSetting::EffectSettingData::Flag::kDetrimental)) {
			return 0.0F;
		}

		const auto archetype = data.archetype.get();
		// kPeakValueModifier is what every vanilla Fortify uses; kValueModifier is accepted too, so
		// a mod granting a permanent SPECIAL point is not missed.
		if (archetype != RE::EffectArchetypes::ArchetypeID::kValueModifier &&
			archetype != RE::EffectArchetypes::ArchetypeID::kPeakValueModifier) {
			return 0.0F;
		}
		if (!IsSpecialAV(data.primaryAV) || IsPerkGated(a_effect)) {
			return 0.0F;
		}
		return a_effect.data.magnitude;
	}

	// Record side: what one of an item's EffectItems would do if the item were used right now.
	// Magnitudes here are unsigned and the kStimpak archetype's is a percentage. Perks are not
	// applied; see PlayerState and Consume for how the real numbers get folded back in.
	[[nodiscard]] inline Contribution Reduce(const RE::EffectItem& a_effect)
	{
		Contribution out{};

		const auto setting = a_effect.effectSetting;
		if (!setting) {
			return out;
		}

		const auto& data = setting->data;
		const bool  detrimental = data.flags.any(RE::EffectSetting::EffectSettingData::Flag::kDetrimental);

		// Duration 0 applies the magnitude once; anything else applies it per second for that long.
		const auto duration = static_cast<float>(std::max(a_effect.data.duration, 1));
		const auto total = a_effect.data.magnitude * duration;

		switch (data.archetype.get()) {
		case RE::EffectArchetypes::ArchetypeID::kStimpak:
			// primaryAV is null on this archetype; the archetype itself means "percent of max
			// health". Vanilla Stimpak is magnitude 6.0 over 5 seconds, the 30% the game
			// advertises.
			out.hpPercent = detrimental ? -total : total;
			break;

		case RE::EffectArchetypes::ArchetypeID::kValueModifier:
			if (data.primaryAV == detail::g_av.health) {
				out.hpFlat = detrimental ? -total : total;
			} else if (data.primaryAV == detail::g_av.rads) {
				// Rads are the one axis where "restore" means the number goes down: RadAway's
				// RestoreRadsChem is a non-detrimental value modifier of 60.0 over 5 seconds,
				// which is the 300 rads it removes.
				out.rads = detrimental ? total : -total;
			} else if (IsLimbAV(data.primaryAV) && !detrimental) {
				out.mendsLimb = true;
			} else {
				// A value modifier on any other actor value: Fortify Endurance, Restore Action
				// Points, Fortify Carry Weight. Not predicted, but it is what makes a Radstag
				// Steak worth more than a Radstag Meat.
				out.otherEffect = true;
			}
			break;

		default:
			// kPeakValueModifier (a temporary fortify), kScript, kCureAddiction and the rest are
			// outside this mod's remit for predicting a heal, but not for deciding whether an item
			// is precious. It is often the only thing that distinguishes two foods (Deathclaw
			// Steak from Deathclaw Meat, Nuka-Cola from Purified Water), and it is the basis of
			// the Include Special/Buff Food setting.
			out.otherEffect = true;
			break;
		}

		return out;
	}

	// Live side: what an effect already running on the player still has to deliver. This is what
	// stops a second press one second after a stimpak from deciding the player is still hurt, and it
	// is the only perk-correct number in the mod - the engine applied Medic, Chemist and the rest
	// before writing these fields.
	//
	// Far less logic than Reduce() above, because the engine has already done the sign and the unit
	// conversion, so anything corrected here would corrupt an already-right value.
	[[nodiscard]] inline Contribution Pending(const RE::ActiveEffect& a_active)
	{
		Contribution out{};

		const auto item = a_active.effect;
		const auto setting = item ? item->effectSetting : nullptr;
		if (!setting) {
			return out;
		}

		// An effect whose conditions currently fail is listed but delivers nothing. Both variants
		// of a perk-gated pair show up in the active list simultaneously (a food item with a 2.5
		// and a 3.75 Restore Health lists both), so counting both would over-predict.
		if (a_active.conditionStatus == RE::ActiveEffect::ConditionStatus::kFalse) {
			return out;
		}

		// An instantaneous effect has already been applied in full by the time it appears here.
		if (a_active.duration <= 0.0F) {
			return out;
		}
		const auto remaining = std::max(a_active.duration - a_active.elapsedSeconds, 0.0F);
		if (remaining <= 0.0F) {
			return out;
		}

		const auto total = a_active.magnitude * remaining;

		switch (setting->data.archetype.get()) {
		case RE::EffectArchetypes::ArchetypeID::kStimpak:
			// Absolute HP, not a percentage. A Stimpak on a 163.5-max character with Medic 1
			// reads magnitude 13.08, which is 8% of 163.5: the engine has already converted
			// it.
			out.hpFlat = total;
			break;

		case RE::EffectArchetypes::ArchetypeID::kValueModifier:
			if (setting->data.primaryAV == detail::g_av.health) {
				out.hpFlat = total;
			} else if (setting->data.primaryAV == detail::g_av.rads) {
				// Sign comes from the engine. RadAway with Medic 1 reads magnitude -80.0, and negative
				// is already this struct's convention for "radiation removed".
				out.rads = total;
			} else if (IsLimbAV(setting->data.primaryAV) && total > 0.0F) {
				out.mendsLimb = true;
			}
			break;

		default:
			break;
		}

		return out;
	}
}
