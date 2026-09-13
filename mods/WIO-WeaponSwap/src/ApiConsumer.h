#pragma once

// Windows.h brings in min/max and ERROR as macros. NOMINMAX suppresses the first pair;
// ERROR is undefined below because it collides with REX::ERROR.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef ERROR

#include "ControlUnbinderAPI.h"

// Consumer half of the shared binding API: this mod issues binding requests to Control Unbinder
// and never writes ControlMap itself. Shows no HUD message when the provider is missing; the zoom
// status line reports it.
namespace WS::ApiConsumer
{
	namespace Api = WattzIO::ControlUnbinderAPI;

	namespace detail
	{
		// Null until Acquire() resolves the provider, and null forever if it is not installed.
		inline const Api::API* g_api = nullptr;
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
			REX::WARN("Weapon Swap Button: {} is loaded but does not export {}"sv,
				Api::kModuleName, Api::kExportName);
			return;
		}

		const auto* api = getApi();
		if (!api) {
			return;
		}
		if (api->version != Api::kVersion) {
			REX::WARN(
				"Weapon Swap Button: Control Unbinder API v{} does not match the v{} this mod was "
				"built against - zoom unbind disabled"sv,
				api->version, Api::kVersion);
			return;
		}

		detail::g_api = api;
		REX::INFO("Weapon Swap Button: Control Unbinder API v{} acquired"sv, api->version);
	}
}
