#pragma once

#include <algorithm>
#include <cmath>

#include "IHudReveal.h"
#include "InputState.h"
#include "Settings.h"

namespace ZoomOffhand
{
	// Tracks the vanilla "SecondaryAttack" button and applies a smooth FOV zoom while it is held
	// and the weapon is holstered or unequipped.
	//
	// Committed by scaling fDefault1stPersonFOV:Display and fDefaultWorldFOV:Display through
	// RE::INISettingCollection. PlayerCamera::worldFOV/firstPersonFOV and NiCamera::viewFrustum
	// produce no visible zoom.
	//
	// Those settings are static config values nothing in the engine recomputes, so re-reading the
	// live value each idle frame would read back this mod's own output and compound (80 -> 40 ->
	// 20 -> 10), while a one-off capture at Install() goes stale the moment another mod or the
	// vanilla FOV slider changes it. So this tracks what it last wrote (s_lastWritten*) and
	// compares it against the live value every frame; a difference beyond float noise becomes the
	// new base.
	// === F4RD RELOCATIONS ========================================================
	//   Kind   Site                                          ID / RVA     +Off
	//   vtbl   RE::VTABLE::PlayerCamera[1]                   F4RD            -
	//   vfunc  BSInputEventReceiver::PerformInputProcessing  slot 0          -
	// =============================================================================
	class ZoomEffect
	{
	public:
		static void Install()
		{
			// F4RD:vtbl - [1] = BSInputEventReceiver subobject (offset 0x038)
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::PlayerCamera[1] };
			// F4RD:vfunc - slot 0 = PerformInputProcessing
			_original = vtbl.write_vfunc(0, &ZoomEffect::PerformInputProcessing);
			REX::INFO("Holstered Zoom: BSInputEventReceiver hook installed on PlayerCamera"sv);

			const auto ini = RE::INISettingCollection::GetSingleton();
			s_firstPersonSetting = ini ? ini->GetSetting("fDefault1stPersonFOV:Display"sv) : nullptr;
			s_worldSetting = ini ? ini->GetSetting("fDefaultWorldFOV:Display"sv) : nullptr;

			// Seed values only. Both are re-synced to the live setting every frame afterwards.
			s_trueFirstPersonFOV = s_lastWrittenFirstPersonFOV = s_firstPersonSetting ? s_firstPersonSetting->GetFloat() : 0.0F;
			s_trueWorldFOV = s_lastWrittenWorldFOV = s_worldSetting ? s_worldSetting->GetFloat() : 0.0F;
		}

		// Called from SettingsReload's watcher the moment menu mode is entered. ApplyZoom cannot cover
		// it, hanging off PlayerCamera's BSInputEventReceiver, which stops being fed while a menu is
		// up. Idempotent, writing the same true-base value ApplyZoom's ungated branch writes.
		static void ForceRestore()
		{
			if (s_lastSetting && s_blend > 0.0F) {
				s_lastSetting->SetFloat(s_lastActiveTrueBase);
				if (s_lastSetting == s_firstPersonSetting) {
					s_lastWrittenFirstPersonFOV = s_lastActiveTrueBase;
				} else if (s_lastSetting == s_worldSetting) {
					s_lastWrittenWorldFOV = s_lastActiveTrueBase;
				}
			}
			s_blend = 0.0F;
		}

	private:
		// Does not read SecondaryAttack off a_queueHead: this hook's QUserEvent() check never matches,
		// while WeaponDrawBlock.h's does. Still the right tick point, running every frame regardless.
		static void PerformInputProcessing(RE::BSInputEventReceiver* a_this, const RE::InputEvent* a_queueHead)
		{
			ApplyZoom();

			_original(a_this, a_queueHead);
		}

		static void ApplyZoom()
		{
			const auto camera = RE::PlayerCamera::GetSingleton();
			const auto ui = RE::UI::GetSingleton();
			const auto player = RE::PlayerCharacter::GetSingleton();
			const auto timer = RE::GetBSTimer();
			if (!camera || !ui || !player || !timer) {
				return;
			}

			// CommonLibF4RD exposes the member directly rather than an accessor.
			const auto state = camera->currentState;
			const bool firstPerson = state && state->id == RE::CameraStates::kFirstPerson;
			const bool thirdPerson = state && state->id == RE::CameraStates::k3rdPerson;

			// Only these two gameplay camera states are eligible; VATS, Furniture, Mount, Dialogue, Free
			// camera, Iron Sights and Vanity match neither branch. Pip-Boy is an overlay flag on top of
			// these two rather than a state of its own.
			RE::Setting* const setting = firstPerson  ? s_firstPersonSetting :
			                              thirdPerson ? s_worldSetting :
			                                            nullptr;
			// Null when neither branch matches, mirroring `setting` above, so these three can never
			// form a mismatched pair.
			float* const trueBase = firstPerson  ? &s_trueFirstPersonFOV :
			                         thirdPerson ? &s_trueWorldFOV :
			                                       nullptr;
			float* const lastWritten = firstPerson  ? &s_lastWrittenFirstPersonFOV :
			                            thirdPerson ? &s_lastWrittenWorldFOV :
			                                          nullptr;

			// If the live value has drifted from what this mod last wrote by more than float
			// round-trip noise, adopt it as the new base.
			constexpr float kEpsilon = 0.01F;
			if (setting) {
				const float live = setting->GetFloat();
				if (std::fabs(live - *lastWritten) > kEpsilon) {
					*trueBase = live;
				}
			}

			// Stale-press guard. s_secondaryAttackValue is written only from WeaponDrawBlock's hook, which
			// stops being dispatched while a menu or the Pip-Boy is up, or outside normal first and third
			// person, so a press held into one of those never delivers its release and the value latches
			// at 1.0. A button still held repopulates it on the next event.
			//
			// WeaponDrawBlock::s_suppressingHold is not cleared alongside it: clearing mid-hold would let a
			// resumed held event reach the real handler without it having seen the press edge, which is the
			// weapon draw this mod exists to suppress.
			if (ui->menuMode != 0 || camera->pipboyMode || (!firstPerson && !thirdPerson)) {
				InputState::s_secondaryAttackValue = 0.0F;
			}

			const bool perspectiveOk = (firstPerson && Settings::bApplyInFirstPerson.GetValue()) ||
			                           (thirdPerson && Settings::bApplyInThirdPerson.GetValue());

			const bool gated = setting != nullptr &&
			                   Settings::bEnabled.GetValue() &&
			                   perspectiveOk &&
			                   !player->GetWeaponMagicDrawn() &&
			                   ui->menuMode == 0 &&
			                   !camera->pipboyMode;

			// Ahead of the early returns below, so it sees a true "is the zoom engaged" answer every
			// frame. Raw press state rather than s_blend, so it engages on the press edge instead of
			// trailing the FOV ramp. A reveal is binary, so analog trigger scaling is not consulted.
			IHudReveal::Update(gated && InputState::s_secondaryAttackValue > 0.0F);

			// A perspective switch mid-hold means a different setting object entirely: restore the old one
			// exactly and hard-reset the blend, rather than decaying smoothly to a slightly-off value.
			if (setting != s_lastSetting) {
				if (s_lastSetting && s_blend > 0.0F) {
					s_lastSetting->SetFloat(s_lastActiveTrueBase);
					// That setting's lastWritten* is left alone: the restore value is its true
					// base, which lastWritten* should already equal.
				}
				s_lastSetting = setting;
				s_blend = 0.0F;
			}

			if (!setting) {
				return;
			}

			s_lastActiveTrueBase = *trueBase;

			if (!gated) {
				if (s_blend > 0.0F) {
					setting->SetFloat(*trueBase);
					*lastWritten = *trueBase;
				}
				s_blend = 0.0F;
				return;
			}

			// With analog scaling on, the target blend tracks how far the trigger is pulled instead
			// of snapping to fully pressed.
			const float pressAmount = Settings::bAnalogTriggerScaling.GetValue() ?
			                               InputState::s_secondaryAttackValue :
			                               (InputState::s_secondaryAttackValue > 0.0F ? 1.0F : 0.0F);

			const float step = timer->realTimeDelta / Settings::GetZoomSpeedSeconds();

			if (s_blend < pressAmount) {
				s_blend = std::min(pressAmount, s_blend + step);
			} else if (s_blend > pressAmount) {
				s_blend = std::max(pressAmount, s_blend - step);
			}

			if (s_blend <= 0.0F) {
				// Gated, but the button is not held. Restores the true base exactly rather than
				// trusting where the smooth per-frame decay landed.
				setting->SetFloat(*trueBase);
				*lastWritten = *trueBase;
				return;
			}

			// s_blend is a linear 0-1 progress value driven by iZoomSpeedMs; the multiplier reshapes it
			// through a smoothstep curve, constant-speed motion over so short a duration reading as a
			// snap. The immediate-restore paths above stay hard cuts.
			const float eased = s_blend * s_blend * (3.0F - 2.0F * s_blend);
			const float multiplier = std::lerp(1.0F, Settings::GetZoomMultiplier(), eased);
			const float newFOV = *trueBase * multiplier;

			setting->SetFloat(newFOV);
			*lastWritten = newFOV;
		}

		static inline RE::Setting* s_firstPersonSetting = nullptr;
		static inline RE::Setting* s_worldSetting = nullptr;
		static inline RE::Setting* s_lastSetting = nullptr;
		static inline float s_trueFirstPersonFOV = 0.0F;
		static inline float s_trueWorldFOV = 0.0F;
		static inline float s_lastWrittenFirstPersonFOV = 0.0F;
		static inline float s_lastWrittenWorldFOV = 0.0F;
		static inline float s_lastActiveTrueBase = 0.0F;
		static inline float s_blend = 0.0F;

		using OriginalFunc = void(RE::BSInputEventReceiver*, const RE::InputEvent*);
		static inline REL::Relocation<OriginalFunc*> _original;
	};
}
