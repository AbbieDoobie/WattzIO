// === F4RD RELOCATIONS ========================================================
// This plugin's own source resolves no addresses, but it does reach one through
// the shared compat layer: WIO::Translations loads the MCM translation file, and
// that resolves the game's translation-map insert. Its banner is in
// lib/commonlibf4rd/compat/WattzIO/Translations.h.
//
// It also reaches a per-runtime ABI offset there: ControlRemap's MCM pushes go
// through WIO::Papyrus, which hands the game a BSTThreadScrapFunction whose impl
// pointer it reads at 0x18 on OG and 0x38 on NG/AE. Banner in
// lib/commonlibf4rd/compat/WattzIO/Papyrus.h.
//
// This banner covers everything the compat layer pins to a specific runtime,
// not just its ids. An ABI offset counts; anything reached through the layer
// belongs here even though this file resolves nothing itself.
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

WIO_PLUGIN_VERSION("WIO-ControlUnbinder", "Abbie Doobie", 1, 0, 1);

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
