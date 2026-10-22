#pragma once

#include <algorithm>
#include <cstdint>

#include "Effects.h"
#include "Settings.h"

// Everything about the player the solver needs, read once per press.
//
// The arithmetic:
//
//   GetActorValue(Health)             current health
//   GetPermanentActorValue(Health)    maximum health, radiation not subtracted
//   GetActorValue(Rads)               current radiation, on a 0..1000 scale
//   GetActorValue(RadHealthMax)       the ceiling: maximum health with radiation already
//                                     subtracted, computed by the engine
//
// Radiation does not lower maximum health point for point. It runs on a 0..1000 scale and removes
// a proportion: 5 rads on a 163.50-max character moves RadHealthMax to 162.68, which is
// 163.50 x 5/1000. RadHealthMax is the engine's own answer and already accounts for the curve
// and for anything that fortifies it (Psycho raises it alongside Health), so it is read rather
// than derived.
namespace SH::PlayerState
{
	// Radiation's own scale. Derived live where possible (see RadCap) and only used as the
	// fallback when the player is clean and there is nothing to derive from.
	inline constexpr float kDefaultRadScale = 1000.0F;

	struct State
	{
		bool valid = false;

		float maxHP = 0.0F;        // absolute maximum, radiation ignored
		float curHP = 0.0F;        // current health
		float rads = 0.0F;         // current radiation, 0..radScale
		float reachableHP = 0.0F;  // RadHealthMax: the top of the white part of the bar
		float radScale = kDefaultRadScale;

		// Still to arrive from effects already running on the player. Perk-correct, the engine folding
		// Medic and friends in before writing the values this reads.
		float pendingHP = 0.0F;
		float pendingRads = 0.0F;

		bool anyLimbCrippled = false;

		// What the values become once everything in flight has landed. The solver aims from these, not
		// the raw current ones: almost every heal arrives over 5 or 10 seconds.
		[[nodiscard]] float ProjectedRads() const { return std::clamp(rads + pendingRads, 0.0F, radScale); }

		// Radiation clearing raises the ceiling, so the projection has to be clamped against the
		// ceiling this press is actually heading for, not the one in force right now.
		[[nodiscard]] float ProjectedReachableHP() const
		{
			return radScale > 0.0F ? maxHP * (1.0F - (ProjectedRads() / radScale)) : maxHP;
		}

		[[nodiscard]] float ProjectedHP() const
		{
			return std::clamp(curHP + pendingHP, 0.0F, ProjectedReachableHP());
		}
	};

	[[nodiscard]] inline State Read()
	{
		State state{};

		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player || !Effects::Resolve()) {
			return state;
		}

		const auto health = Effects::HealthAV();
		const auto rads = Effects::RadsAV();
		const auto radHealthMax = Effects::RadHealthMaxAV();

		state.maxHP = player->GetPermanentActorValue(*health);
		state.curHP = player->GetActorValue(*health);
		state.rads = player->GetActorValue(*rads);

		// The engine's own ceiling. Falling back to maxHP when the actor value is missing plans against
		// an optimistic ceiling and stops early, rather than force-feeding items it cannot use.
		state.reachableHP = radHealthMax ? player->GetActorValue(*radHealthMax) : state.maxHP;

		// Derived rather than hardcoded to 1000. The relationship is
		// reachableHP = maxHP x (1 - rads / scale), so the scale can be recovered from any sample
		// where the player is irradiated, which stays correct if a mod changes
		// fRadiationHealthCurve. Only meaningful while there is radiation to measure against; the
		// default carries the rest of the time.
		if (state.rads > 0.0F && state.maxHP > 0.0F) {
			const auto lostFraction = 1.0F - (state.reachableHP / state.maxHP);
			if (lostFraction > 0.0001F) {
				state.radScale = state.rads / lostFraction;
			}
		}

		// A limb is crippled when its condition actor value has reached zero.
		for (const auto limb : Effects::LimbAVs()) {
			if (limb && player->GetActorValue(*limb) <= 0.0F) {
				state.anyLimbCrippled = true;
				break;
			}
		}

		if (const auto active = player->GetActiveEffectList(); active) {
			for (const auto& effect : active->data) {
				if (!effect) {
					continue;
				}
				const auto pending = Effects::Pending(*effect);
				// hpPercent is not read here: the live path never produces one, because the
				// engine has already converted a stimpak to absolute HP. See Effects.h.
				state.pendingHP += pending.hpFlat;
				state.pendingRads += pending.rads;
			}
		}

		state.valid = true;

		if (Settings::debugLog) {
			REX::DEBUG("Smart Heal Button: [STATE] hp={:.1f}/{:.1f} reachable={:.1f} rads={:.1f}/{:.0f} "
			          "pendingHP={:+.1f} pendingRads={:+.1f} projected(hp={:.1f} rads={:.1f} ceiling={:.1f}) crippled={}"sv,
				state.curHP, state.maxHP, state.reachableHP, state.rads, state.radScale,
				state.pendingHP, state.pendingRads,
				state.ProjectedHP(), state.ProjectedRads(), state.ProjectedReachableHP(),
				state.anyLimbCrippled);
		}

		return state;
	}

	// The absolute HP the player is asking to end up at, and the tolerance either side of it.
	//
	// HP Target is a percentage of the health that is actually reachable, i.e. of the bar as
	// radiation has left it, not of the clean maximum. a_reachableHP is passed in rather than read
	// from the state so the caller can hand in the higher ceiling that clearing radiation in this
	// same press would produce.
	[[nodiscard]] inline float HPTargetValue(float a_reachableHP)
	{
		return a_reachableHP * static_cast<float>(Settings::iHPTarget) * 0.01F;
	}

	[[nodiscard]] inline float HPToleranceValue(float a_reachableHP)
	{
		return a_reachableHP * static_cast<float>(Settings::iHPHysteresis) * 0.01F;
	}

	// The absolute rads the player is willing to be left carrying, and the tolerance either side.
	// The denominator is radiation's own 0..1000 scale, not max health; the two are not in the
	// same units.
	[[nodiscard]] inline float RadTargetValue(const State& a_state)
	{
		return a_state.radScale * static_cast<float>(Settings::iMaxRads) * 0.01F;
	}

	[[nodiscard]] inline float RadToleranceValue(const State& a_state)
	{
		return a_state.radScale * static_cast<float>(Settings::iRadHysteresis) * 0.01F;
	}
}
