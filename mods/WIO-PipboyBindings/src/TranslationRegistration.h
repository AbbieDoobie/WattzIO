#pragma once

namespace PipboyPipbindFix::TranslationRegistration
{
	// MCM's own per-mod translation pass only runs for mods with a loaded ESP/ESL, and this mod
	// ships none, so without this every MCM label renders as its literal $KEY.
	inline void Register()
	{
		const auto manager = RE::BSScaleformManager::GetSingleton();
		if (!manager) {
			REX::WARN("Pip-Boy Bindings Fix: BSScaleformManager unavailable - translations not registered"sv);
			return;
		}

		// GetTranslator() dereferences `loader` without a null check of its own.
		if (!manager->loader) {
			REX::WARN("Pip-Boy Bindings Fix: BSScaleformManager has no loader - translations not registered"sv);
			return;
		}

		const auto translator = RE::GetScaleformTranslator(manager);
		if (!translator) {
			REX::WARN("Pip-Boy Bindings Fix: BSScaleformTranslator unavailable - translations not registered"sv);
			return;
		}

		// English first as a fallback, then the player's configured language.
		WIO::Translations::LoadFile("Data/Interface/Translations/WIO-PipboyBindings_en.txt");
		WIO::Translations::LoadForMod("WIO-PipboyBindings"sv);
	}
}
