#include "DiscoveredHotkeys.h"
#include "InputHook.h"
#include "Settings.h"
#include "SettingsReload.h"
#include "TranslationRegistration.h"

namespace
{
	void MessageHandler(F4SE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type == F4SE::MessagingInterface::kGameLoaded) {
			// PlayerCamera, BSScaleformManager, UI and the Papyrus VM's live state are not
			// safely touchable earlier than kGameLoaded.
			GMH::InputHook::Install();
			GMH::MenuContext::CrosshairWatcher::Install();
			GMH::MenuContext::CrosshairModeWatcher::Install();
			GMH::SettingsReload::MenuWatcher::Install();
			GMH::TranslationRegistration::Register();
			GMH::DiscoveredHotkeys::Push();
		}
	}
}

WIO_PLUGIN_VERSION("WIO-GamepadHotkeys", "Abbie Doobie", 1, 0, 0);

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	if (!WIO::Init(a_f4se, "WIO-GamepadHotkeys"sv)) {
		return false;
	}

	REX::INFO("Gamepad MCM Hotkeys (native) loaded"sv);

	// Pure file I/O, so it does not need the game engine up, unlike everything registered in
	// MessageHandler above. Re-read on every pause-menu close via SettingsReload.
	GMH::Settings::Load();

	if (const auto messaging = F4SE::GetMessagingInterface()) {
		messaging->RegisterListener(MessageHandler);
	}

	return true;
}
