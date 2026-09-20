// === F4RD RELOCATIONS ========================================================
// This file resolves no addresses. KeyRedirect.h carries the plugin's only two.
// =============================================================================

#include "pch.h"

#include "KeyRedirect.h"
#include "Settings.h"
#include "SettingsReload.h"
#include "TranslationRegistration.h"

namespace GKK
{
	static void MessageHandler(F4SE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg || a_msg->type != F4SE::MessagingInterface::kGameLoaded) {
			return;
		}

		// BSScaleformManager is not available at plugin-load time.
		TranslationRegistration::Register();

		Settings::Load();
		KeyRedirect::Handler::Install();
		SettingsReload::MenuWatcher::Install();

		REX::INFO("Gamepad KBM Keys: initialised"sv);
	}
}

WIO_PLUGIN_VERSION("WIO-GamepadKbmKeys", "Abbie Doobie", 1, 0, 0);

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	if (!WIO::Init(a_f4se, "WIO-GamepadKbmKeys"sv)) {
		return false;
	}

	const auto messaging = F4SE::GetMessagingInterface();
	if (!messaging) {
		REX::ERROR("Gamepad KBM Keys: F4SE messaging interface unavailable"sv);
		return false;
	}

	// Loaded here as well as at kGameLoaded, so nothing running earlier reads defaults.
	GKK::Settings::Load();

	messaging->RegisterListener(GKK::MessageHandler);

	return true;
}
