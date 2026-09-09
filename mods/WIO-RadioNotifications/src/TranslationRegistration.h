#pragma once

namespace RNF::TranslationRegistration
{
	// Registers the translation file directly with the engine's Scaleform translator. MCM's own
	// per-mod translation pass requires a loaded plugin (ESP/ESL); this mod ships none.
	inline void Register()
	{
		const auto manager = RE::BSScaleformManager::GetSingleton();
		// GetTranslator() dereferences `loader` without a null check of its own.
		if (!manager || !manager->loader) {
			REX::WARN("Radio Notification Filter: BSScaleformManager unavailable - MCM text will show untranslated keys"sv);
			return;
		}

		const auto translator = RE::GetScaleformTranslator(manager);
		if (!translator) {
			REX::WARN("Radio Notification Filter: BSScaleformTranslator unavailable - MCM text will show untranslated keys"sv);
			return;
		}

		// English first as a fallback, then the player's configured language.
		WIO::Translations::LoadFile("Data/Interface/Translations/WIO-RadioNotifications_en.txt");
		WIO::Translations::LoadForMod("WIO-RadioNotifications"sv);
	}
}
