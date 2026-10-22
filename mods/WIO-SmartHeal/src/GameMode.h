#pragma once

#include <array>
#include <cstdint>
#include <string_view>

// What the game's own difficulty and Survival rules say, read live.
//
// Survival is not simply harder numbers: it switches individual rules on, each a global that
// vanilla records condition themselves on. The Stimpak's six limb-restore effects are gated on
// HC_Rule_NoLimbConditionHeal == 1, crippled limbs recovering on their own outside Survival.
// Read as globals rather than inferred from the difficulty level, which mods toggle separately.
namespace SH::GameMode
{
	// FormIDs in Fallout4.esm. Each is verified against its own editor ID when read rather than
	// trusted.
	struct Rule
	{
		std::uint32_t    formID;
		std::string_view editorID;
	};

	inline constexpr std::array kRules{
		Rule{ 0x00000846, "HC_Rule_NoLimbConditionHeal"sv },
		Rule{ 0x0000080D, "HC_Rule_NoFastTravel"sv },
		Rule{ 0x00000810, "HC_Rule_AdrenalineOn"sv },
		Rule{ 0x00000812, "HC_Rule_SleepEffects"sv },
		Rule{ 0x00000848, "HC_Rule_DamageWhenEncumbered"sv },
		Rule{ 0x0000084C, "HC_Rule_ScaleDamage"sv },
		Rule{ 0x00000854, "HC_Rule_SustenanceEffects"sv },
		Rule{ 0x0000088A, "HC_Rule_DiseaseEffects"sv },
		Rule{ 0x000008B1, "HC_Rule_CompanionNoHeal"sv },
	};

	// Returns the rule's value, or a_fallback when the form is missing or is not the record its
	// FormID is supposed to name.
	[[nodiscard]] inline float RuleValue(const Rule& a_rule, float a_fallback = 0.0F)
	{
		const auto global = RE::TESForm::GetFormByID<RE::TESGlobal>(a_rule.formID);
		if (!global) {
			return a_fallback;
		}
		if (const auto* editorID = global->formEditorID.c_str();
			!editorID || a_rule.editorID != editorID) {
			return a_fallback;
		}
		return global->GetValue();
	}

	// True when crippled limbs do not recover on their own, which is what makes an explicit
	// limb-restore effect the only way to fix one. Has no caller: if Always Use Stimpak if Limb is
	// Crippled is on, a crippled limb costs a stimpak on every difficulty, and a caller in the
	// decision path is a regression.
	[[nodiscard]] inline bool LimbsNeedExplicitHealing()
	{
		return RuleValue(kRules[0]) >= 1.0F;
	}

	[[nodiscard]] inline RE::DifficultyLevel Difficulty()
	{
		const auto player = RE::PlayerCharacter::GetSingleton();
		return player ? RE::GetDifficultyLevel(player) : RE::DifficultyLevel::kNormal;
	}

	[[nodiscard]] inline std::string_view DifficultyName(RE::DifficultyLevel a_level)
	{
		switch (a_level) {
		case RE::DifficultyLevel::kVeryEasy: return "Very Easy"sv;
		case RE::DifficultyLevel::kEasy:     return "Easy"sv;
		case RE::DifficultyLevel::kNormal:   return "Normal"sv;
		case RE::DifficultyLevel::kHard:     return "Hard"sv;
		case RE::DifficultyLevel::kVeryHard: return "Very Hard"sv;
		case RE::DifficultyLevel::kSurvival: return "Survival"sv;
		default:                             return "Unknown"sv;
		}
	}
}
