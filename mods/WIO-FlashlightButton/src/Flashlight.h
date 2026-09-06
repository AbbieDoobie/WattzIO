#pragma once

// The tap action: toggle the Pip-Boy light.
//
// IsPipboyLightOn / ShowPipboyLight are the engine's own functions and feed
// RE::PipboyLightEvent, so anything downstream that listens stays in the loop.
namespace FMB::Flashlight
{
	inline void Toggle()
	{
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		// a_skipEffects = false keeps the vanilla sound and visual.
		RE::ShowPipboyLight(player, !player->IsPipboyLightOn(), false);
	}
}
