#include "InputDevicePatches.h"
#include "DeviceTracker.h"
#include "DiagnosticLog.h"
#include "LookHandlerPatches.h"
#include "PipboyContextFix.h"
#include "Settings.h"
#include "TranslationRegistration.h"

namespace
{
	// kPostPostLoad, not kInputLoaded, which never fires for this plugin. MenuControls, ControlMap
	// and RE::UI are already live at the main menu, so this covers it as well as gameplay.
	// Idempotent, so the kGameLoaded call below is a safe fallback.
	void TryInstallCore()
	{
		FalloutInputSwapper::DeviceTracker::Install();
	}

	void MessageHandler(F4SE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case F4SE::MessagingInterface::kPostPostLoad:
			TryInstallCore();
			break;
		case F4SE::MessagingInterface::kGameLoaded:
			TryInstallCore();

			// RE::UI-dependent, so these wait for gameplay.
			FalloutInputSwapper::DiagnosticLog::MenuWatcher::Install();
			FalloutInputSwapper::PipboyContextFix::Install();
			FalloutInputSwapper::TranslationRegistration::Register();
			break;
		default:
			break;
		}
	}
}

WIO_PLUGIN_VERSION("WIO-InputSwapper", "Abbie Doobie", 1, 0, 0);

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	// Opt-in: at Info level every FIS-DIAG line is dropped before it is formatted. Read straight
	// from the ini, because Settings has no cached state this early.
	if (!WIO::Init(a_f4se, "WIO-InputSwapper"sv,
			FalloutInputSwapper::Settings::GetVerboseDiagnostics())) {
		return false;
	}

	// Backs the nine call-site redirects in InputDevicePatches and LookHandlerPatches, each of
	// which needs a branch destination in an executable page near the game module.
	F4SE::AllocTrampoline(1024);

	// Code patches, dependent on no live object, so they install immediately.
	FalloutInputSwapper::InputDevicePatches::Install();
	FalloutInputSwapper::LookHandlerPatches::Install();

	REX::INFO("Input Swapper (native) loaded"sv);

	if (const auto messaging = F4SE::GetMessagingInterface(); messaging) {
		messaging->RegisterListener(MessageHandler);
	}

	return true;
}
