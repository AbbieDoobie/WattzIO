#pragma once

namespace ZoomOffhand
{
	// MCM's own per-mod translation pass only runs for mods with a loaded ESP/ESL, and this mod
	// ships none, so without this every MCM label renders as its literal $KEY.
	class TranslationRegistration
	{
	public:
		static void Register()
		{
			const auto manager = RE::BSScaleformManager::GetSingleton();
			if (!manager) {
				REX::WARN("Holstered Zoom: BSScaleformManager unavailable - translations not registered"sv);
				return;
			}

			// GetTranslator() dereferences `loader` without a null check of its own.
			if (!manager->loader) {
				REX::WARN("Holstered Zoom: BSScaleformManager has no loader yet - translations not registered"sv);
				return;
			}

			const auto translator = RE::GetScaleformTranslator(manager);
			if (!translator) {
				REX::WARN("Holstered Zoom: BSScaleformTranslator unavailable - translations not registered"sv);
				return;
			}

			// English first as a fallback, then the player's configured language.
			WIO::Translations::LoadFile("Data/Interface/Translations/WIO-HolsteredZoom_en.txt");
			WIO::Translations::LoadForMod("WIO-HolsteredZoom"sv);

			REX::INFO("Holstered Zoom: translations registered directly with BSScaleformTranslator"sv);
		}
	};
}
