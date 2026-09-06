#pragma once

#include <cstdint>

// Shared contract between Control Unbinder, the provider, and the mods that consume it.
// Byte-identical in every one of them.
//
// The provider exports a plain C function; consumers resolve it with GetModuleHandle +
// GetProcAddress, so a null module handle means not installed. The ABI is plain C types and POD
// structs only.
namespace WattzIO::ControlUnbinderAPI
{
	inline constexpr const char*  kProviderName = "WIO-ControlUnbinder";
	inline constexpr const char*  kModuleName = "WIO-ControlUnbinder.dll";
	inline constexpr const char*  kExportName = "WIO_GetControlUnbinderAPI";
	inline constexpr std::uint32_t kVersion = 5;

	// The two devices a player sees. Mouse is not separate: the engine treats keyboard and
	// mouse as one binding per action (RemapButton on either clears the other), and the MCM
	// UI follows that.
	enum class Slot : std::uint32_t
	{
		kKeyboard = 0,
		kGamepad = 1
	};

	enum class Action : std::uint32_t
	{
		kUnbind = 0,
		kRestore = 1
	};

	enum class Result : std::uint32_t
	{
		kSuccess = 0,      // verified: the engine now reads what was asked for
		kAlreadyInState,   // nothing to do; not an error
		kNotApplicable,    // no vanilla default on this slot - the request is meaningless
		kRefused,          // the entry exists but could not be written
		kVerifyFailed,     // written, but the read-back disagrees
		kUnknownAction,    // not in the provider's bindings table
		kNoControlMap
	};

	// Live engine state, for status text. A mod's own settings say what was asked for; this says
	// what is actually true.
	struct State
	{
		bool         applicable;
		bool         bound;
		std::int32_t keyboard;
		std::int32_t mouse;
		std::int32_t gamepad;
	};

	struct API
	{
		std::uint32_t version;

		// eventID is a ControlMap UserEvent name; a_caller is the requesting mod's File token.
		Result (*Apply)(const char* a_eventID, Slot a_slot, Action a_action, const char* a_caller);
		State (*Query)(const char* a_eventID, Slot a_slot);

		// Player-facing name for notifications and status lines, e.g. "Toggle Camera View".
		// Returns nullptr for an unknown action.
		const char* (*DisplayName)(const char* a_eventID);

		// Human-readable form of a Result, for logging.
		const char* (*ResultName)(Result a_result);

		// One line for a status row: "Unbound", "Bound (V / Middle Click)", "Not bound on this
		// device by default", or a bare "Bound" for a live key code KeyNames has no name for.
		const char* (*DescribeBinding)(const char* a_eventID, Slot a_slot);

		// True when the provider must remember this binding for the caller. Most persist in
		// ControlMap_Custom.txt, so Apply() is one-shot. remappable=0 entries are written
		// straight into inputKey, persist nowhere, and need SetPersistentUnbind instead. The
		// Quick Slot D-pad entries are the current examples.
		bool (*IsInMemory)(const char* a_eventID);

		// Standing request for an in-memory binding, re-applied on every pass. Pass false to
		// withdraw. Harmless on a persistent binding, but Apply() is the right call there.
		Result (*SetPersistentUnbind)(const char* a_eventID, Slot a_slot, bool a_unbind,
			const char* a_caller);
	};
}
