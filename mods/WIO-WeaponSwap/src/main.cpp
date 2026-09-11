#include "InputHook.h"
#include "Keybinds.h"
#include "Settings.h"
#include "SettingsReload.h"
#include "TranslationRegistration.h"

namespace
{
	void MessageHandler(F4SE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type == F4SE::MessagingInterface::kGameLoaded) {
			// None of this is safe any earlier: PlayerCamera's vtable, the UI singleton's event
			// source, and BSScaleformManager's translator all need the game to be up first.
			WS::InputHook::Install();
			WS::MenuContext::CrosshairWatcher::Install();
			WS::MenuContext::CrosshairModeWatcher::Install();
			WS::SettingsReload::MenuWatcher::Install();
			WS::TranslationRegistration::Register();
		}
	}
}

WIO_PLUGIN_VERSION("WIO-WeaponSwap", "Abbie Doobie", 1, 0, 0);

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	if (!WIO::Init(a_f4se, "WIO-WeaponSwap"sv)) {
		return false;
	}

	REX::INFO("Weapon Swap Button (native) loaded"sv);

	WS::Settings::Load();
	WS::Keybinds::Load();

	if (const auto messaging = F4SE::GetMessagingInterface(); messaging) {
		messaging->RegisterListener(MessageHandler);
	}

	return true;
}
