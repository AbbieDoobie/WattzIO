#include "CombatActivateBlock.h"
#include "ApiConsumer.h"
#include "BindingCommand.h"
#include "ControlRemap.h"
#include "InputHook.h"
#include "Keybinds.h"
#include "NoActivationSound.h"
#include "QuickContainerUIFix.h"
#include "Settings.h"
#include "SettingsReload.h"
#include "TranslationRegistration.h"

namespace
{
	void MessageHandler(F4SE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type == F4SE::MessagingInterface::kGameLoaded) {
			// Must be first: RefreshStatus below localizes, and a lookup before this one gets
			// the compiled-in English. Registered straight with the engine's translator,
			// because MCM's own pass needs a loaded ESP/ESL and this mod ships none.
			ARC::TranslationRegistration::Register();
			// Every plugin DLL is in the process by now, so the provider export either
			// resolves or Control Unbinder genuinely is not installed.
			ARC::ApiConsumer::Acquire();
			// Nothing UI/engine-facing below is safely touchable any earlier than
			// kGameLoaded - PlayerCamera's vtable, ControlMap's live state, the UI singleton's
			// event source, and BSScaleformManager's translator all need the game to be up first.
			ARC::InputHook::Install();
			ARC::NoActivationSound::Hook::Install();
			ARC::BindingCommand::RefreshStatus();
			// QCOpenTransferMenu/SecondaryActivate are remappable=0, so the engine never
			// persists them and they are back to vanilla on every launch - this plugin is
			// their only source of truth and has to re-assert them every pass, startup
			// included.
			ARC::ControlRemap::Apply();
			ARC::SettingsReload::MenuWatcher::Install();
			ARC::QuickContainerUIFix::HUDMenuWatcher::Install();

			// Needs a live PlayerCharacter/PlayerControls to derive real vtables,
			// none of which exist at the main menu - hence the watcher below plus
			// the pause-menu-close retry in SettingsReload.h.
			ARC::CombatActivateBlock::Install();
			ARC::CombatActivateBlock::GameplayStartWatcher::Install();

			// HUDMenu doesn't exist yet at this point (main menu, before a save is loaded) -
			// HUDMenuWatcher above picks up the moment it actually does. This call here is a
			// harmless no-op until then.
			ARC::QuickContainerUIFix::Apply();
		}
	}
}

WIO_PLUGIN_VERSION("WIO-ActivateCombo", "Abbie Doobie", 1, 0, 0);

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	if (!WIO::Init(a_f4se, "WIO-ActivateCombo"sv)) {
		return false;
	}

	REX::INFO("Activate/Reload Combo (native) loaded"sv);

	ARC::Settings::Load();
	ARC::Keybinds::Load();

	if (const auto messaging = F4SE::GetMessagingInterface(); messaging) {
		messaging->RegisterListener(MessageHandler);
	}

	return true;
}
