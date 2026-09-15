#pragma once

namespace FalloutInputSwapper::Settings
{
	// MCM writes here on every change. This plugin only ever reads it.
	constexpr auto kMCMSettingsPath = R"(Data\MCM\Settings\WIO-InputSwapper.ini)";

	// "Preferred Button Glyphs" in MCM.
	//
	// There is deliberately no "icons pinned to gamepad, cursor follows the mouse" option: the
	// signal that would have to be pinned (IsGamepadConnected/UsingGamepad) also drives cursor
	// visibility on every menu besides the Pipboy, so pinning it breaks the cursor everywhere else.
	enum class InputPreference : std::int32_t
	{
		kAuto = 0,     // no preferred device - last real device wins, persists indefinitely
		kGamepad = 1,  // gamepad by default; falls back from KBM after the hysteresis window
		kKBM = 2,      // KBM by default; falls back from gamepad after the hysteresis window
	};

	[[nodiscard]] inline InputPreference GetInputPreference()
	{
		const auto value = ::GetPrivateProfileIntA("Settings", "iInputPreference", 0, kMCMSettingsPath);
		switch (value) {
		case 1:
			return InputPreference::kGamepad;
		case 2:
			return InputPreference::kKBM;
		default:
			return InputPreference::kAuto;
		}
	}

	// Fallback timer for the two preferred modes (kGamepad/kKBM), in seconds. 0-10, default 2.
	// 0 disables the grace period entirely, falling back to the preferred device as soon as the
	// away device goes quiet. Not used by kAuto.
	[[nodiscard]] inline std::int32_t GetHysteresisSeconds()
	{
		// GetPrivateProfileIntA returns UINT, not int, so cast before the clamp or the literal
		// bounds create an ambiguous overload.
		const auto value = static_cast<std::int32_t>(::GetPrivateProfileIntA("Settings", "iHysteresisSeconds", 2, kMCMSettingsPath));
		return std::clamp(value, 0, 10);
	}

	// Restricts which device's movement drives the camera, without affecting that device's other
	// uses: menu clicks, buttons and movement all keep working. Default kBoth is the unrestricted
	// behaviour.
	enum class LookInputSource : std::int32_t
	{
		kBoth = 0,          // default - both mouse and stick drive the camera
		kMouseOnly = 1,     // stick look suppressed; stick still works for movement/UI
		kGamepadOnly = 2,   // mouse look suppressed; mouse still works for clicks/UI
	};

	[[nodiscard]] inline LookInputSource GetLookInputSource()
	{
		const auto value = static_cast<std::int32_t>(::GetPrivateProfileIntA("Settings", "iLookInputSource", 0, kMCMSettingsPath));
		switch (value) {
		case 1:
			return LookInputSource::kMouseOnly;
		case 2:
			return LookInputSource::kGamepadOnly;
		default:
			return LookInputSource::kBoth;
		}
	}

	[[nodiscard]] inline bool GetBool(const char* a_section, const char* a_key, bool a_default)
	{
		return WIO::Ini::GetBool(a_section, a_key, a_default, kMCMSettingsPath);
	}

	// Default off, and off means untouched: the vtable hook is not installed until this reads true.
	[[nodiscard]] inline bool GetStopAutoMoveWithStick()
	{
		return GetBool("Settings", "bStopAutoMoveWithStick", false);
	}
	// Verbose diagnostics. Off by default, which initialises the logger at Info so every
	// FIS-DIAG line (all REX::DEBUG) is dropped before it is formatted. With this on, a ~3 minute
	// session produces roughly 20k lines / 2.2 MB.
	[[nodiscard]] inline bool GetVerboseDiagnostics()
	{
		return GetBool("Settings", "bVerboseDiagnostics", false);
	}

}
