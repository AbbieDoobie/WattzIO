#include "ContextualRemap.h"
#include "InputHook.h"
#include "Keybinds.h"
#include "QuickTurn.h"
#include "Settings.h"
#include "SettingsReload.h"
#include "TranslationRegistration.h"

namespace
{
	void MessageHandler(F4SE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type == F4SE::MessagingInterface::kGameLoaded) {
			QT::PlayerCameraHook::Install();
			QT::PlayerControlsHook::Install();
			QT::MenuContext::CrosshairWatcher::Install();
			QT::MenuContext::CrosshairModeWatcher::Install();
			QT::SettingsReload::MenuWatcher::Install();
			QT::QuickTurn::RefreshMovementKeyBindings();
			QT::ContextualRemap::Apply();
			QT::TranslationRegistration::Register();
		}
	}
}

WIO_PLUGIN_VERSION("WIO-QuickTurn", "Abbie Doobie", 1, 0, 0);

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	if (!WIO::Init(a_f4se, "WIO-QuickTurn"sv)) {
		return false;
	}

	REX::INFO("Quick Turn (native) loaded"sv);

	QT::Settings::Load();
	QT::Keybinds::Load();

	if (const auto messaging = F4SE::GetMessagingInterface(); messaging) {
		messaging->RegisterListener(MessageHandler);
	}

	return true;
}
