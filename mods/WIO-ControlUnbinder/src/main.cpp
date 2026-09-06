// === F4RD RELOCATIONS ========================================================
// This plugin resolves no addresses.
// =============================================================================

#include "ApiProvider.h"
#include "ControlRemap.h"
#include "SettingsReload.h"
#include "TranslationRegistration.h"

namespace
{
	void MessageHandler(F4SE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type == F4SE::MessagingInterface::kGameLoaded) {
			// Must be first: anything localizing before this gets the compiled-in English.
			UnbindAny::TranslationRegistration::Register();
			// ControlMap and RE::UI are not safe to touch before the game is loaded. Apply() here
			// is load-bearing: in-memory bindings are gone on every launch and this is the only
			// thing that re-asserts them.
			UnbindAny::ControlRemap::Apply("kGameLoaded"sv);
			// Not recorded in the push cache: MCM builds its settings store once per launch and
			// that can happen after this point, in which case these pushes are discarded.
			UnbindAny::ControlRemap::RefreshStatus(false);
			UnbindAny::SettingsReload::MenuWatcher::Install();
			UnbindAny::ApiProvider::LogOffered();
		}
	}
}

WIO_PLUGIN_VERSION("WIO-ControlUnbinder", "Abbie Doobie", 1, 0, 0);

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	if (!WIO::Init(a_f4se, "WIO-ControlUnbinder"sv)) {
		return false;
	}

	REX::INFO("Control Unbinder (native) loaded"sv);

	if (const auto messaging = F4SE::GetMessagingInterface(); messaging) {
		messaging->RegisterListener(MessageHandler);
	}

	return true;
}
