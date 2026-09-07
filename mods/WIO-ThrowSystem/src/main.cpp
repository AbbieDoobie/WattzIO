#include "ApiConsumer.h"
#include "BindingCommand.h"
#include "EditorIDPatch.h"
#include "InputHook.h"
#include "Keybinds.h"
#include "RestockOnKill.h"
#include "Settings.h"
#include "SettingsMigration.h"
#include "SettingsReload.h"
#include "TranslationRegistration.h"

namespace
{
	void MessageHandler(F4SE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type == F4SE::MessagingInterface::kGameLoaded) {
			// Must be first: anything localizing before this gets the compiled-in English.
			// Registered straight with the engine's translator, because MCM's own pass needs
			// the mod to have a loaded ESP/ESL and this one ships none.
			TSO::TranslationRegistration::Register();
			// Every plugin DLL is in the process by now, so the provider export either
			// resolves or Control Unbinder genuinely is not installed.
			TSO::ApiConsumer::Acquire();
			// This mod ships no Papyrus or Quest, so MCM's own hotkey dispatch has nothing to
			// call into and this hook is what detects every hotkey press, in both Engine and
			// Animation-only modes. It must always install.
			TSO::InputHook::Install();
			TSO::MenuContext::CrosshairWatcher::Install();
			TSO::MenuContext::CrosshairModeWatcher::Install();
			TSO::RestockOnKill::DeathSink::Install();
			TSO::RestockOnKill::LoadGameSink::Install();
			// Only registers on the session the settings carry-over actually ran. See
			// SettingsMigration.h for why MCM shows defaults on that one session and why the
			// warning has to wait for a save load rather than firing here.
			TSO::SettingsMigration::LoadGameSink::InstallIfMigrated();
			// ControlMap's live state is not safely touchable earlier than kGameLoaded.
			TSO::BindingCommand::RefreshStatus();
			// kGameLoaded fires once per process, so without this watcher an MCM change
			// would need a full game restart. Re-applies on every pause-menu close.
			TSO::SettingsReload::MenuWatcher::Install();
		}
	}
}

WIO_PLUGIN_VERSION("WIO-ThrowSystem", "Abbie Doobie", 2, 0, 1);

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	if (!WIO::Init(a_f4se, "WIO-ThrowSystem"sv)) {
		return false;
	}

	REX::INFO("Throwing System Overhaul (native) loaded"sv);

	// Must install before ESM/ESP data loads. Vtable hooking only needs the module's static
	// vtable data mapped in memory, not any game logic to have run, so this happens
	// synchronously here rather than on a later messaging-interface event.
	TSO::EditorIDPatch::Install();

	// Must run before Settings::Load(), and before MCM (all Papyrus, so strictly later than
	// plugin load) can create a defaults file at the new path. One time only; see
	// SettingsMigration.h for the two guard conditions.
	TSO::SettingsMigration::Run();

	TSO::Settings::Load();
	TSO::Keybinds::Load();

	if (const auto messaging = F4SE::GetMessagingInterface(); messaging) {
		messaging->RegisterListener(MessageHandler);
	}

	return true;
}
