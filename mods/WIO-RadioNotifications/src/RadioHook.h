#pragma once

#include <cstring>

#include "Classify.h"
#include "Diagnostics.h"
#include "Settings.h"

// Patches the two engine functions that emit the radio "signal found" / "signal lost"
// notifications. Each resolves the station reference into RCX, calls GetDisplayFullName, formats
// the message, then calls the emitter. Two write_call patches per function: the station is only
// available at the name-getter call, and the decision can only be acted on at the emit call.
// Found and lost are told apart by which patch ran, not by the message text.
namespace RNF::RadioHook
{
	namespace detail
	{
		enum class Kind
		{
			kFound,
			kLost
		};

		// Main thread only, and the gap between capture and use is a few instructions inside one
		// function, so no synchronisation is needed.
		inline RE::TESObjectREFR* g_pendingStation = nullptr;

		using GetDisplayFullName_t = const char* (*)(RE::TESObjectREFR*);
		using Emit_t = void (*)(const char*);

		inline REL::Relocation<GetDisplayFullName_t> _foundGetName;
		inline REL::Relocation<GetDisplayFullName_t> _lostGetName;
		inline REL::Relocation<Emit_t> _foundEmit;
		inline REL::Relocation<Emit_t> _lostEmit;

		// Captures the station, then calls through so the game still gets its name string.
		const char* FoundGetName(RE::TESObjectREFR* a_station)
		{
			g_pendingStation = a_station;
			return _foundGetName(a_station);
		}

		const char* LostGetName(RE::TESObjectREFR* a_station)
		{
			g_pendingStation = a_station;
			return _lostGetName(a_station);
		}

		// The capture is cleared on every emit, not only when one was made. The engine skips the
		// name getter when the station reference is null and still emits, so without this a later
		// notification could be classified against a stale reference.
		[[nodiscard]] bool ShouldEmit(Kind a_kind, const char* a_message)
		{
			const auto station = g_pendingStation;
			g_pendingStation = nullptr;

			const bool isBeacon = Classify::IsSettlementBeacon(station);

			const bool hide = (a_kind == Kind::kFound)
				? (isBeacon ? Settings::bHideBeaconFound : Settings::bHideStationFound)
				: (isBeacon ? Settings::bHideBeaconLost : Settings::bHideStationLost);

			Diagnostics::ReportNotification(
				a_kind == Kind::kFound ? "FOUND" : "LOST",
				a_message,
				station,
				isBeacon,
				!hide);

			return !hide;
		}

		void FoundEmit(const char* a_message)
		{
			if (ShouldEmit(Kind::kFound, a_message)) {
				_foundEmit(a_message);
			}
		}

		void LostEmit(const char* a_message)
		{
			if (ShouldEmit(Kind::kLost, a_message)) {
				_lostEmit(a_message);
			}
		}

		// === F4RD RELOCATIONS ========================================================
		//   Kind   Site                                OG id / +off   NG+AE id / +off
		//   id     "signal found" fn                   848510         2227745
		//   off    -> GetDisplayFullName call          0xE8           0xE0
		//   off    -> emitter call                     0x11C          0x114
		//   id     "signal lost" fn                    1302608        2227785
		//   off    -> GetDisplayFullName call          0x1DB          0x1F0
		//   off    -> emitter call                     0x20D          0x224
		//   id     TESObjectREFR::GetDisplayFullName   1212056        2201126
		//   id     notification emitter                1011474        2222474
		//
		// Re-derive:
		//   1. The "%s signal found." / "%s signal lost." literals are not referenced
		//      from code - they sit in .data BSFixedString slots held by absolute
		//      pointer. Find the literal, find the 8-byte absolute pointer to it, then
		//      scan .text for rip-relative references to that address.
		//   2. Map the hit to its function via the .pdata exception table, following
		//      UNW_FLAG_CHAININFO back to the first fragment ("signal lost" is split).
		//   3. Inside each, the call order is name-getter -> formatter -> emitter, with
		//      the emitter ~0x34 bytes after the name getter on every build.
		//   4. Reverse-map each function start through the legacy OG Address Library
		//      table embedded in f4rd-runtime.bin to get the OG id.
		// =============================================================================
		// F4RD:id - (OG, AE); NG falls back to the AE value, which is correct here.
		constexpr REL::ID kRadioFound{ 848510, 2227745 };
		constexpr REL::ID kRadioLost{ 1302608, 2227785 };
		constexpr REL::ID kGetDisplayFullName{ 1212056, 2201126 };
		constexpr REL::ID kRadioEmitter{ 1011474, 2222474 };

		// F4RD:off - (OG, modern); NG and AE share these, OG differs at all four.
		constexpr REL::VariantOffset kFoundGetNameOffset{ 0xE8, 0xE0 };
		constexpr REL::VariantOffset kFoundEmitOffset{ 0x11C, 0x114 };
		constexpr REL::VariantOffset kLostGetNameOffset{ 0x1DB, 0x1F0 };
		constexpr REL::VariantOffset kLostEmitOffset{ 0x20D, 0x224 };

		// Resolves through a_id.id(), F4RD's family selector, not the REL::ID itself.
		// resolve(const ID&) pattern-scans the AE id first on OG, so a false positive can outrank a
		// correct OG id. On 1.10.163 the AE emitter id matches 0x235E80, which is not the radio
		// emitter. Returns 0 rather than aborting: REL::Relocation's constructor calls
		// report_and_fail.
		[[nodiscard]] std::uintptr_t Resolve(const REL::ID& a_id)
		{
			const auto result = REL::IDDatabase::get().resolve(a_id.id());
			return result.rva ? REL::Module::get().base() + *result.rva : 0;
		}

		// Patching a wrong instruction corrupts code rather than failing cleanly, so
		// each site is checked before anything is written.
		[[nodiscard]] bool IsExpectedCall(std::uintptr_t a_site, std::uintptr_t a_expectedTarget)
		{
			const auto bytes = reinterpret_cast<const std::uint8_t*>(a_site);
			if (bytes[0] != 0xE8) {
				return false;
			}

			std::int32_t rel{};
			std::memcpy(&rel, bytes + 1, sizeof(rel));
			return a_site + 5 + rel == a_expectedTarget;
		}
	}

	// write_call patches the single call instruction at each site, so the name getter's many other
	// call sites elsewhere in the binary are unaffected.
	inline void Install()
	{
		// F4RD:id - the two owner functions and the two callees
		const auto found = detail::Resolve(detail::kRadioFound);
		const auto lost = detail::Resolve(detail::kRadioLost);
		const auto getName = detail::Resolve(detail::kGetDisplayFullName);
		const auto emitter = detail::Resolve(detail::kRadioEmitter);

		if (!found || !lost || !getName || !emitter) {
			REX::WARN("Radio Notification Filter: could not resolve the engine functions on this game version - hooks not installed, radio notifications are unchanged"sv);
			return;
		}

		// F4RD:off - VariantOffset::offset() is F4RD's own per-family selector
		const auto foundGetName = found + detail::kFoundGetNameOffset.offset();
		const auto foundEmit = found + detail::kFoundEmitOffset.offset();
		const auto lostGetName = lost + detail::kLostGetNameOffset.offset();
		const auto lostEmit = lost + detail::kLostEmitOffset.offset();

		const bool verified =
			detail::IsExpectedCall(foundGetName, getName) &&
			detail::IsExpectedCall(foundEmit, emitter) &&
			detail::IsExpectedCall(lostGetName, getName) &&
			detail::IsExpectedCall(lostEmit, emitter);

		// All four or none. A partial install would filter one notification and not the other,
		// which is harder to diagnose than doing nothing.
		if (!verified) {
			REX::WARN("Radio Notification Filter: call sites do not match the expected game version - hooks not installed, radio notifications are unchanged"sv);
			return;
		}

		detail::_foundGetName = REL::write_call<5>(REL::Relocation<std::uintptr_t>{ foundGetName }, detail::FoundGetName);
		detail::_foundEmit = REL::write_call<5>(REL::Relocation<std::uintptr_t>{ foundEmit }, detail::FoundEmit);
		detail::_lostGetName = REL::write_call<5>(REL::Relocation<std::uintptr_t>{ lostGetName }, detail::LostGetName);
		detail::_lostEmit = REL::write_call<5>(REL::Relocation<std::uintptr_t>{ lostEmit }, detail::LostEmit);

		REX::INFO("Radio Notification Filter: hooks installed on {}"sv,
			REL::Module::get().version().string());
	}
}
