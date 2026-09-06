#pragma once

namespace UnbindAny
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
				REX::WARN("Control Unbinder: BSScaleformManager unavailable - translations not registered"sv);
				return;
			}

			// GetTranslator() dereferences `loader` without a null check of its own.
			if (!manager->loader) {
				REX::WARN("Control Unbinder: BSScaleformManager has no loader - translations not registered"sv);
				return;
			}

			const auto translator = RE::GetScaleformTranslator(manager);
			if (!translator) {
				REX::WARN("Control Unbinder: BSScaleformTranslator unavailable - translations not registered"sv);
				return;
			}

			// English first as a fallback, then the player's configured language.
			WIO::Translations::LoadFile("Data/Interface/Translations/WIO-ControlUnbinder_en.txt");
			WIO::Translations::LoadForMod("WIO-ControlUnbinder"sv);
		}
	};
}
