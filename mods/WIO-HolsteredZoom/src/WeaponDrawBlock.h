#pragma once

#include "InputState.h"
#include "Settings.h"

namespace ZoomOffhand
{
	// Pressing SecondaryAttack (Aim/Block) while the weapon is holstered also readies the weapon, so
	// GetWeaponMagicDrawn() flips true within a frame or two and ZoomEffect's gate cuts the zoom in
	// favour of vanilla ADS. The event is therefore suppressed at its source, RE::AttackBlockHandler,
	// which is registered for both Attack and Block/Aim. Everything else it processes passes through.
	// === F4RD RELOCATIONS ========================================================
	//   Kind   Site                                          ID / RVA     +Off
	//   vtbl   RE::VTABLE::AttackBlockHandler[0]             F4RD            -
	//   vfunc  BSInputEventUser::OnButtonEvent               slot 8          -
	// =============================================================================
	class WeaponDrawBlock
	{
	public:
		static void Install()
		{
			// F4RD:vtbl - [0] = AttackBlockHandler's primary vtable
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::AttackBlockHandler[0] };
			// F4RD:vfunc - slot 8 = BSInputEventUser::OnButtonEvent
			_original = vtbl.write_vfunc(8, &WeaponDrawBlock::OnButtonEvent);
			REX::INFO("Holstered Zoom: AttackBlockHandler::OnButtonEvent hook installed"sv);
		}

	private:
		static void OnButtonEvent(RE::AttackBlockHandler* a_this, const RE::ButtonEvent* a_event)
		{
			if (a_event && a_event->QUserEvent() == "SecondaryAttack"sv) {
				// Shared with ZoomEffect.h's per-frame blend. Updated before the suppress/allow
				// decision below, so ZoomEffect sees the live press state whether or not this
				// event is suppressed.
				InputState::s_secondaryAttackValue = a_event->QAnalogValue();

				// Latched at the press edge and held for the gesture: a change of weapon-drawn state mid-hold
				// could otherwise suppress a press but pass its release, desyncing the handler's own tracking.
				if (a_event->QJustPressed()) {
					s_suppressingHold = ShouldSuppress();
				}
				if (s_suppressingHold) {
					if (RE::QReleased(*a_event)) {
						s_suppressingHold = false;
					}
					return;  // swallow entirely - the real handler never sees this press/hold/release
				}
			}
			_original(a_this, a_event);
		}

		// Recomputed rather than read from a ZoomEffect cache: this fires from button-event dispatch,
		// whose ordering against ZoomEffect's per-frame hook is unconfirmed.
		[[nodiscard]] static bool ShouldSuppress()
		{
			const auto player = RE::PlayerCharacter::GetSingleton();
			const auto ui = RE::UI::GetSingleton();
			const auto camera = RE::PlayerCamera::GetSingleton();
			if (!player || !ui || !camera) {
				return false;
			}

			const auto state = camera->currentState;
			const bool firstPerson = state && state->id == RE::CameraStates::kFirstPerson;
			const bool thirdPerson = state && state->id == RE::CameraStates::k3rdPerson;
			const bool perspectiveOk = (firstPerson && Settings::bApplyInFirstPerson.GetValue()) ||
			                           (thirdPerson && Settings::bApplyInThirdPerson.GetValue());

			return Settings::bEnabled.GetValue() &&
			       perspectiveOk &&
			       !player->GetWeaponMagicDrawn() &&
			       ui->menuMode == 0 &&
			       !camera->pipboyMode;
		}

		static inline bool s_suppressingHold = false;

		using OriginalFunc = void(RE::AttackBlockHandler*, const RE::ButtonEvent*);
		static inline REL::Relocation<OriginalFunc*> _original;
	};
}
