#pragma once

#include "Settings.h"

// Diagnostic logging of input-device, context-stack and look-vector state, for diagnosing one
// device's movement and look going dead while context-independent buttons such as the Pipboy
// toggle keep working.
//
// Every line is prefixed "FIS-DIAG" so it can be grepped out of the log. All of it is REX::DEBUG
// and is dropped unless the verbose setting is on.
namespace FalloutInputSwapper::DiagnosticLog
{
	// Full state snapshot. a_glyphIsGamepad and a_mcmMode are passed in rather than read from
	// DeviceTracker, so this file does not depend on it and cannot create an include cycle.
	inline void LogSnapshot(std::string_view a_reason, bool a_glyphIsGamepad, std::int32_t a_mcmMode)
	{
		const auto controlMap = RE::ControlMap::GetSingleton();
		const auto deviceManager = RE::BSInputDeviceManager::GetSingleton();
		const auto playerControls = RE::PlayerControls::GetSingleton();

		const bool ignoreKBM = controlMap ? controlMap->ignoreKeyboardMouse : false;
		const bool queuedGPEnable = deviceManager ? deviceManager->queuedGamepadEnableValue : false;
		const bool valueQueued = deviceManager ? deviceManager->valueQueued : false;
		const bool pollingEnabled = deviceManager ? deviceManager->pollingEnabled : false;
		const bool gamepadConnected = deviceManager ? deviceManager->IsGamepadConnected() : false;
		const bool gamepadDevicePtr = deviceManager ? deviceManager->GetGamepad() != nullptr : false;

		float moveX = 0.0f, moveY = 0.0f, lookX = 0.0f, lookY = 0.0f;
		bool blockPlayerInput = false, notifyingHandlers = false;
		if (playerControls) {
			moveX = playerControls->data.moveInputVec.x;
			moveY = playerControls->data.moveInputVec.y;
			lookX = playerControls->data.lookInputVec.x;
			lookY = playerControls->data.lookInputVec.y;
			blockPlayerInput = playerControls->blockPlayerInput;
			notifyingHandlers = playerControls->notifyingHandlers;
		}

		// RE::ControlMap's input-context priority stack. An imbalance here buries kMainGameplay
		// (see PipboyContextFix), so both its depth and its top entry are logged.
		std::size_t contextStackSize = 0;
		std::int32_t contextStackTop = -1;
		if (controlMap) {
			contextStackSize = controlMap->contextPriorityStack.size();
			if (contextStackSize > 0) {
				contextStackTop = controlMap->contextPriorityStack.back().underlying();
			}
		}

		REX::DEBUG(
			"FIS-DIAG snapshot[{}] ignoreKBM={} queuedGPEnable={} valueQueued={} pollingEnabled={} "
			"gamepadConnected={} gamepadDevicePtr={} blockPlayerInput={} notifyingHandlers={} "
			"contextStackSize={} contextStackTop={} "
			"move=({:.3f},{:.3f}) look=({:.3f},{:.3f}) glyphDevice={} mcmMode={}"sv,
			a_reason, ignoreKBM, queuedGPEnable, valueQueued, pollingEnabled,
			gamepadConnected, gamepadDevicePtr, blockPlayerInput, notifyingHandlers,
			contextStackSize, contextStackTop,
			moveX, moveY, lookX, lookY,
			a_glyphIsGamepad ? "gamepad"sv : "kbm"sv, a_mcmMode);
	}

	// One compact line per observed event, unthrottled: volume tracks input activity rather than
	// wall-clock time.
	inline void LogEvent(std::string_view a_device, std::string_view a_kind, float a_x, float a_y)
	{
		REX::DEBUG("FIS-DIAG event device={} kind={} x={:.3f} y={:.3f}"sv, a_device, a_kind, a_x, a_y);
	}

	// Periodic full snapshot, throttled so normal play does not flood the log. LogSnapshot()
	// itself is unthrottled and can be called directly.
	inline void MaybeLogPeriodicSnapshot(bool a_glyphIsGamepad, std::int32_t a_mcmMode)
	{
		static auto s_lastLog = std::chrono::steady_clock::now();
		constexpr auto kInterval = std::chrono::milliseconds(200);

		const auto now = std::chrono::steady_clock::now();
		if (now - s_lastLog >= kInterval) {
			s_lastLog = now;
			LogSnapshot("periodic"sv, a_glyphIsGamepad, a_mcmMode);
		}
	}

	// Logs every menu open and close with an unconditional state snapshot attached, so
	// "inside a menu, where move=(0,0) is expected" can be told apart from "locked during
	// gameplay", and a lockup that brackets a Pipboy transition is visible by timestamp.
	class MenuWatcher :
		public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static void Install()
		{
			const auto ui = RE::UI::GetSingleton();
			if (!ui) {
				REX::WARN("Input Swapper: RE::UI unavailable - diagnostic menu watcher not installed"sv);
				return;
			}

			static MenuWatcher singleton;
			ui->GetEventSource<RE::MenuOpenCloseEvent>()->RegisterSink(&singleton);
			REX::INFO("Input Swapper: diagnostic menu watcher installed"sv);
		}

	private:
		RE::BSEventNotifyControl ProcessEvent(
			const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			// Re-reads the verbose switch on every pause-menu close, so toggling it in MCM takes
			// effect without a restart. WIO::SetVerbose is what actually lifts the log level: a
			// release build is pinned to Info and every line here is REX::DEBUG.
			if (!a_event.opening && a_event.menuName == "PauseMenu"sv) {
				WIO::SetVerbose(Settings::GetVerboseDiagnostics());
			}

			REX::DEBUG("FIS-DIAG menu {} {}"sv, a_event.opening ? "OPEN"sv : "CLOSE"sv, a_event.menuName.c_str());
			// a_glyphIsGamepad and a_mcmMode are not known here, since this class does not depend
			// on DeviceTracker. Correlate against the nearest periodic snapshot, which is taken
			// every 200ms.
			LogSnapshot(a_event.opening ? "menu-open"sv : "menu-close"sv, false, -1);
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}
