#pragma once

namespace ZoomOffhand::InputState
{
	// Shared 0.0-1.0 SecondaryAttack (Zoom/Off-Hand) press value.
	//
	// Written from WeaponDrawBlock.h's AttackBlockHandler hook, not ZoomEffect.h's PlayerCamera
	// hook: PlayerCamera sees raw, unresolved InputEvents, so QUserEvent() == "SecondaryAttack"
	// never matches there, while AttackBlockHandler gets events after ControlMap resolves them.
	inline float s_secondaryAttackValue = 0.0F;
}
