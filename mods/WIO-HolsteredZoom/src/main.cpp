#include "IHudReveal.h"
#include "Settings.h"
#include "SettingsReload.h"
#include "TranslationRegistration.h"
#include "WeaponDrawBlock.h"
#include "ZoomEffect.h"

namespace
{
	void MessageHandler(F4SE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type == F4SE::MessagingInterface::kGameLoaded) {
			// PlayerCamera::GetSingleton() and RE::UI's live state are not safely touchable
			// before the game is up, so all hooks install here rather than at plugin load.
			ZoomOffhand::ZoomEffect::Install();
			ZoomOffhand::WeaponDrawBlock::Install();
			ZoomOffhand::IHudReveal::Install();
			ZoomOffhand::SettingsReload::MenuWatcher::Install();
			ZoomOffhand::TranslationRegistration::Register();
		}
	}
}

WIO_PLUGIN_VERSION("WIO-HolsteredZoom", "Abbie Doobie", 1, 0, 0);

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	if (!WIO::Init(a_f4se, "WIO-HolsteredZoom"sv)) {
		return false;
	}

	REX::INFO("Holstered Zoom (native) loaded"sv);

	ZoomOffhand::Settings::Load();

	if (const auto messaging = F4SE::GetMessagingInterface(); messaging) {
		messaging->RegisterListener(MessageHandler);
	}

	return true;
}
