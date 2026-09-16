// === F4RD RELOCATIONS ========================================================
// This plugin resolves no addresses.
// =============================================================================

#include "pch.h"

#include "GameplayInputWatcher.h"
#include "Keybinds.h"
#include "MenuControlsWatcher.h"
#include "PipboyWatcher.h"
#include "Settings.h"
#include "SettingsReload.h"
#include "TranslationRegistration.h"

namespace PipboyPipbindFix
{
	static void MessageHandler(F4SE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg || a_msg->type != F4SE::MessagingInterface::kGameLoaded) {
			return;
		}

		// BSScaleformManager is not available at plugin-load time.
		TranslationRegistration::Register();

		MenuControlsWatcher::Install();

		GameplayInputWatcher::Install();

		PipboyWatcher::Install();

		SettingsReload::MenuWatcher::Install();

		REX::INFO("Pip-Boy Bindings Fix: initialised"sv);
	}
}

WIO_PLUGIN_VERSION("WIO-PipboyBindings", "Abbie Doobie", 1, 0, 0);

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	if (!WIO::Init(a_f4se, "WIO-PipboyBindings"sv)) {
		return false;
	}

	const auto messaging = F4SE::GetMessagingInterface();
	if (!messaging) {
		REX::ERROR("Pip-Boy Bindings Fix: F4SE messaging interface unavailable"sv);
		return false;
	}

	// Loaded here as well as at kGameLoaded, so nothing running earlier reads defaults.
	PipboyPipbindFix::Settings::Load();
	PipboyPipbindFix::Keybinds::Load();

	messaging->RegisterListener(PipboyPipbindFix::MessageHandler);

	return true;
}
