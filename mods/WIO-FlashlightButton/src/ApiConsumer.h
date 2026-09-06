#pragma once

// Windows.h brings in min/max and ERROR as macros. NOMINMAX suppresses the first pair;
// ERROR is undefined below because it collides with REX::ERROR.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef ERROR

#include "ControlUnbinderAPI.h"

// Consumer half of the shared binding API: this mod issues binding requests to Control Unbinder
// and never writes ControlMap itself.
namespace FMB::ApiConsumer
{
	namespace Api = WattzIO::ControlUnbinderAPI;

	namespace detail
	{
		// Null until Acquire() resolves the provider, and null forever if it is not installed.
		inline const Api::API* g_api = nullptr;

		// Shown once per session. Every other message comes from Control Unbinder itself.
		inline bool g_warned = false;
	}

	[[nodiscard]] inline bool Available()
	{
		return detail::g_api != nullptr;
	}

	[[nodiscard]] inline const Api::API* Get()
	{
		return detail::g_api;
	}

	// Resolves the provider's exported function. Safe to call more than once. GetModuleHandle
	// loads nothing; it only reports whether the provider is already in the process.
	inline void Acquire()
	{
		if (detail::g_api) {
			return;
		}

		const auto module = ::GetModuleHandleA(Api::kModuleName);
		if (!module) {
			return;  // provider not installed - stays null, which is the signal
		}

		using GetApi = const Api::API* (*)();
		const auto getApi = reinterpret_cast<GetApi>(
			::GetProcAddress(module, Api::kExportName));
		if (!getApi) {
			REX::WARN("Flashlight Button: {} is loaded but does not export {}"sv,
				Api::kModuleName, Api::kExportName);
			return;
		}

		const auto* api = getApi();
		if (!api) {
			return;
		}
		if (api->version != Api::kVersion) {
			REX::WARN(
				"Flashlight Button: Control Unbinder API v{} does not match the v{} this mod was "
				"built against - binding options disabled"sv,
				api->version, Api::kVersion);
			return;
		}

		detail::g_api = api;
		REX::INFO("Flashlight Button: Control Unbinder API v{} acquired"sv, api->version);
	}

	// Only fires when a binding change was asked for, so a player who never touches these
	// options is never told about the dependency.
	inline void WarnUnavailable()
	{
		if (detail::g_warned) {
			return;
		}
		detail::g_warned = true;
		const auto msg = WIO::Translations::Localize(
			"$FMB_Note_UnbinderMissing"sv, "Flashlight Button: Control Unbinder not installed"sv);
		RE::SendHUDMessage::ShowHUDMessage(msg.c_str(), nullptr, true, true);
		REX::WARN(
			"Flashlight Button: a binding option is set but WIO-ControlUnbinder is not installed"sv);
	}
}
