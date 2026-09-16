#pragma once

namespace PipboyPipbindFix::Settings
{
	inline constexpr char kINI[] = "Data/MCM/Settings/WIO-PipboyBindings.ini";

	// [PipboyZoom] Gamepad button for Pipboy zoom (dropdown index, 0 = OFF,
	// 1-16 = buttons).
	inline int iZoomGamepadButton = 0;
	// Suppresses the vanilla zoom inputs while the Pipboy is open. The button configured above is
	// never suppressed, so setting Zoom to Back and turning this on still leaves Back zooming.
	inline bool bSuppressVanillaZoom = false;

	// [PipboyClose] Gamepad button for Pipboy close (dropdown index, 0 = SameAsOpen, 1 = OFF,
	// 2-17 = buttons).
	inline int iCloseGamepadButton = 0;

	// [CompanionOrder] Gamepad button that exits Companion Order Mode (dropdown index, same
	// option list as Pipboy Close: 0 = SameAsOpen, 1 = OFF, 2-17 = buttons).
	inline int iOrderExitGamepadButton = 1;
	// Suppresses the vanilla order-mode exit while order mode is active. The button configured
	// above is never suppressed, so setting the exit to the Pipboy button still works with this on.
	inline bool bSuppressVanillaOrderExit = false;

	// Hidden diagnostic switch, deliberately not in config.json, so it has no MCM control. Add
	// bDebugLog=1 under [Advanced] in Data/MCM/Settings/WIO-PipboyBindings.ini by hand and close
	// the pause menu - MCM leaves keys it does not own alone.
	inline bool bDebugLog = false;

	inline void Load()
	{
		const auto ReadInt = [](const char* a_section, const char* a_key, int a_default) -> int {
			char buf[16] = {};
			REX::W32::GetPrivateProfileStringA(a_section, a_key, "", buf, sizeof(buf), kINI);
			if (buf[0] == '\0') {
				return a_default;
			}
			char* end = nullptr;
			const long v = std::strtol(buf, &end, 10);
			return (end != buf) ? static_cast<int>(v) : a_default;
		};

		// Accepts both "1"/"0" and "true"/"false".
		const auto ReadBool = [](const char* a_section, const char* a_key) -> bool {
			return WIO::Ini::GetBool(a_section, a_key, false, kINI);
		};

		iZoomGamepadButton   = ReadInt("PipboyZoom",  "iZoomGamepadButton",  0);
		bSuppressVanillaZoom = ReadBool("PipboyZoom", "bSuppressVanillaZoom");
		iCloseGamepadButton  = ReadInt("PipboyClose", "iCloseGamepadButton", 0);

		iOrderExitGamepadButton   = ReadInt("CompanionOrder",  "iOrderExitGamepadButton", 1);
		bSuppressVanillaOrderExit = ReadBool("CompanionOrder", "bSuppressVanillaOrderExit");

		bDebugLog = ReadBool("Advanced", "bDebugLog");
		WIO::SetVerbose(bDebugLog);

		REX::DEBUG("Pip-Boy Bindings Fix: settings loaded - ZoomGP={} SuppressVanillaZoom={} CloseGP={} "
				   "OrderExitGP={} SuppressVanillaOrderExit={}",
			iZoomGamepadButton, bSuppressVanillaZoom, iCloseGamepadButton,
			iOrderExitGamepadButton, bSuppressVanillaOrderExit);
	}
}
