#include "ApiConsumer.h"
#include "BindingCommand.h"
#include "InputHook.h"
#include "Keybinds.h"
#include "Settings.h"
#include "SettingsReload.h"
#include "TranslationRegistration.h"

namespace
{
	void MessageHandler(F4SE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}

		if (a_msg->type == F4SE::MessagingInterface::kGameLoaded) {
			// Every plugin DLL is in the process by now, so the provider export either
			// resolves or it genuinely is not installed.
			FMB::ApiConsumer::Acquire();

			// None of this is safe any earlier: PlayerCamera's and PlayerControls' vtables, the
			// UI singleton's event source, ControlMap, and BSScaleformManager's translator all
			// need the game to be up first.
			FMB::InputHook::Install();
			FMB::MenuContext::CrosshairWatcher::Install();
			FMB::MenuContext::CrosshairModeWatcher::Install();
			FMB::SettingsReload::MenuWatcher::Install();
			FMB::TranslationRegistration::Register();
			FMB::BindingCommand::RefreshStatus();
		}
	}
}

WIO_PLUGIN_VERSION("WIO-FlashlightButton", "Abbie Doobie", 1, 0, 0);

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	if (!WIO::Init(a_f4se, "WIO-FlashlightButton"sv)) {
		return false;
	}

	REX::INFO("Flashlight Button (native) loaded"sv);

	FMB::Settings::Load();
	FMB::Keybinds::Load();

	if (const auto messaging = F4SE::GetMessagingInterface(); messaging) {
		messaging->RegisterListener(MessageHandler);
	}

	return true;
}
