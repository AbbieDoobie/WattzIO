#include "InputHook.h"
#include "ItemCatalog.h"
#include "Keybinds.h"
#include "MenuContext.h"
#include "Settings.h"
#include "SettingsReload.h"
#include "TranslationRegistration.h"

namespace
{
	void MessageHandler(F4SE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type == F4SE::MessagingInterface::kGameLoaded) {
			// Nothing below is safely touchable before kGameLoaded: PlayerCamera's vtable, the UI
			// singleton's event source, BSScaleformManager's translator and TESDataHandler's form
			// arrays all need the game to be up.
			SH::InputHook::Install();
			SH::MenuContext::CrosshairWatcher::Install();
			SH::MenuContext::CrosshairModeWatcher::Install();
			SH::SettingsReload::MenuWatcher::Install();
			SH::TranslationRegistration::Register();

			// Built once here: the item catalogue is a pure function of the load order, so a
			// keypress never has to walk the form arrays. Retried on pause-menu close if the
			// singletons it needs were not up.
			SH::ItemCatalog::Build();
		}
	}
}

WIO_PLUGIN_VERSION("WIO-SmartHeal", "Abbie Doobie", 1, 0, 0);

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	if (!WIO::Init(a_f4se, "WIO-SmartHeal"sv)) {
		return false;
	}

	REX::INFO("Smart Heal Button (native) loaded"sv);

	SH::Settings::Load();
	SH::Keybinds::Load();

	if (const auto messaging = F4SE::GetMessagingInterface(); messaging) {
		messaging->RegisterListener(MessageHandler);
	}

	return true;
}
