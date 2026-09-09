#include "Classify.h"
#include "RadioHook.h"
#include "Settings.h"
#include "SettingsReload.h"
#include "TranslationRegistration.h"

namespace
{
	void MessageHandler(F4SE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type == F4SE::MessagingInterface::kGameLoaded) {
			// None of this is safe before kGameLoaded: the UI singleton's event source,
			// BSScaleformManager's translator and TESDataHandler all need the game up.
			RNF::SettingsReload::MenuWatcher::Install();
			RNF::TranslationRegistration::Register();
			RNF::Classify::detail::ResolveBeaconStations();

			// MCM's settings file may not have existed when the plugin loaded.
			RNF::Settings::Load();
		}
	}
}

WIO_PLUGIN_VERSION("WIO-RadioNotifications", "Abbie Doobie", 1, 0, 0);

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	if (!WIO::Init(a_f4se, "WIO-RadioNotifications"sv)) {
		return false;
	}

	// The trampoline backs the four write_call patches. CommonLibF4RD's F4SE::Init
	// takes no InitInfo, so the allocation is a separate call.
	F4SE::AllocTrampoline(64);

	RNF::Settings::Load();

	// Patches static call sites, so no game state is needed and no notification can slip through
	// before the hooks are in place.
	RNF::RadioHook::Install();

	if (const auto messaging = F4SE::GetMessagingInterface(); messaging) {
		messaging->RegisterListener(MessageHandler);
	}

	return true;
}
