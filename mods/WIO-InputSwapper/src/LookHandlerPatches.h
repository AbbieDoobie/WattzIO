#pragma once

#include <atomic>

#include "InputDevicePatches.h"  // detail::UsingGamepadLook and the shared patch helpers
#include "Settings.h"

// LookInputSource enforcement at vanilla's own write site.
//
// LookHandler::OnMouseMoveEvent and OnThumbstickEvent are the final writers of
// PlayerControls::data.lookInputVec, and they run on a separate dispatch from DeviceTracker, so
// they overwrite anything it writes. This redirects the existing gating call inside each, calling
// through to preserve the original check and additionally blocking the write when LookInputSource
// excludes that device. Buttons, menus and UI handling are untouched.
//
// === F4RD RELOCATIONS ========================================================
//   Kind   Site                                       ID / RVA     +Off
//   vtbl   RE::VTABLE::LookHandler[0]                 REL::ID 44789   -
//   vfunc  BSInputEventUser::OnThumbstickEvent        slot 4          -
//   vfunc  BSInputEventUser::OnMouseMoveEvent         slot 6          -
//   (the gate and both call sites are derived from those - see ResolveSites)
// =============================================================================
namespace FalloutInputSwapper::LookHandlerPatches
{
	namespace detail
	{
		// The "is input blocked" gate both overrides call before writing lookInputVec: menu and
		// text-entry state, unrelated to device selection. Called through, so this only adds a
		// condition. Unset until Install() succeeds.
		using OrigGateFunc_t = bool(std::uintptr_t);
		inline REL::Relocation<OrigGateFunc_t> OrigGate;

		// Guards the gamepad-to-mouse camera swing: the first raw mouse delta after the OS cursor
		// has gone unpolled can carry the whole accumulated drift, thousands of units, rather than
		// one frame. Tracked here rather than read from DeviceTracker, which sits on a different
		// receiver.
		inline std::atomic<bool> s_lastLookWasGamepad{ false };

		// Everything is derived from VTABLE::LookHandler at install time, so no hardcoded RVA can
		// go stale. Slot 4 is OnThumbstickEvent, slot 6 is OnMouseMoveEvent. The gate is the first
		// call in each and must agree between them; the redirected site is the second call to it.
		struct Sites
		{
			std::uintptr_t gate{};
			std::uintptr_t mouseCall{};
			std::uintptr_t stickCall{};
			[[nodiscard]] explicit operator bool() const noexcept
			{
				return gate && mouseCall && stickCall;
			}
		};

		// Records the first in-image CALL target found, and the address of every
		// CALL reaching a_wanted, over a bounded window.
		inline void ScanCalls(std::uintptr_t a_fn, std::size_t a_window,
			std::uintptr_t& a_firstTarget, std::uintptr_t a_wanted,
			std::vector<std::uintptr_t>& a_sites)
		{
			const auto& module = REL::Module::get();
			const auto lo = module.base();
			const auto hi = lo + module.image_size();
			const auto bytes = reinterpret_cast<const std::uint8_t*>(a_fn);
			for (std::size_t i = 0; i + 5 <= a_window; ++i) {
				if (bytes[i] != 0xE8) {
					continue;
				}
				std::int32_t rel{};
				std::memcpy(&rel, bytes + i + 1, sizeof(rel));
				const auto target = a_fn + i + 5 + rel;
				// A 0xE8 byte also occurs inside unrelated instructions; requiring
				// the target to land inside the image discards those.
				if (target < lo || target >= hi) {
					continue;
				}
				if (!a_firstTarget) {
					a_firstTarget = target;
				}
				if (a_wanted && target == a_wanted) {
					a_sites.push_back(a_fn + i);
				}
			}
		}

		[[nodiscard]] inline Sites ResolveSites()
		{
			constexpr std::size_t kWindow = 0x140;
			constexpr std::size_t kThumbstickSlot = 4;
			constexpr std::size_t kMouseMoveSlot = 6;

			// F4RD:vtbl - VTABLE::LookHandler, REL::ID(44789), per-family in F4RD
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::LookHandler[0] };
			const auto slots = reinterpret_cast<const std::uintptr_t*>(vtbl.address());
			const auto onStick = slots[kThumbstickSlot];
			const auto onMouse = slots[kMouseMoveSlot];
			if (!onStick || !onMouse) {
				return {};
			}

			std::uintptr_t stickFirst{};
			std::uintptr_t mouseFirst{};
			std::vector<std::uintptr_t> unused;
			ScanCalls(onStick, kWindow, stickFirst, 0, unused);
			ScanCalls(onMouse, kWindow, mouseFirst, 0, unused);
			if (!stickFirst || stickFirst != mouseFirst) {
				REX::WARN(
					"Input Swapper: LookHandler's two look overrides do not share a first "
					"callee (stick 0x{:X}, mouse 0x{:X}) - LookHandlerPatches not installed."sv,
					stickFirst, mouseFirst);
				return {};
			}

			const auto gate = stickFirst;
			std::vector<std::uintptr_t> stickSites;
			std::vector<std::uintptr_t> mouseSites;
			std::uintptr_t ignored{};
			ScanCalls(onStick, kWindow, ignored, gate, stickSites);
			ignored = 0;
			ScanCalls(onMouse, kWindow, ignored, gate, mouseSites);

			// The second gate call in each is the one guarding the lookInputVec write.
			if (stickSites.size() < 2 || mouseSites.size() < 2) {
				REX::WARN(
					"Input Swapper: expected at least two gate calls in each LookHandler "
					"override (found stick {}, mouse {}) - LookHandlerPatches not installed."sv,
					stickSites.size(), mouseSites.size());
				return {};
			}

			return Sites{ gate, mouseSites[1], stickSites[1] };
		}

		// Return value matches the original function's convention: TEST AL,AL / JNZ-skip-write
		// means nonzero blocks the write.
		inline bool ShouldBlockMouseLook(std::uintptr_t a_arg)
		{
			if (detail::OrigGate(a_arg)) {
				return true;  // preserve original menu/text-entry gating
			}

			// Consumed regardless of what happens next: a real mouse event always resets the
			// transition window, whether or not LookInputSource also blocks it.
			const bool wasGamepad = s_lastLookWasGamepad.exchange(false, std::memory_order_relaxed);

			const auto lookSource = Settings::GetLookInputSource();

			if (lookSource == Settings::LookInputSource::kGamepadOnly) {
				REX::DEBUG("FIS-DIAG LookHandlerPatches: blocked mouse lookInputVec write at the real write site"sv);
				return true;
			}

			// In kMouseOnly the gamepad's look is blocked outright, so no handoff can occur and
			// the guard below would only be acting on a stale flag.
			if (lookSource == Settings::LookInputSource::kMouseOnly) {
				return false;
			}

			if (wasGamepad) {
				REX::DEBUG(
					"FIS-DIAG LookHandlerPatches: transition guard - skipped first post-gamepad mouse "
					"lookInputVec write (likely stale-cursor-position artifact)"sv);
				return true;
			}

			return false;
		}

		inline bool ShouldBlockGamepadLook(std::uintptr_t a_arg)
		{
			if (detail::OrigGate(a_arg)) {
				return true;
			}

			if (Settings::GetLookInputSource() == Settings::LookInputSource::kMouseOnly) {
				REX::DEBUG("FIS-DIAG LookHandlerPatches: blocked gamepad lookInputVec write at the real write site"sv);
				// Deliberately does not set s_lastLookWasGamepad: a blocked stick is not
				// driving look, so it must not arm the mouse-side transition guard.
				return true;
			}

			s_lastLookWasGamepad.store(true, std::memory_order_relaxed);
			return false;
		}
	}

	// Safe from F4SEPlugin_Load: static instruction addresses, no live objects. ResolveSites()
	// validates the structure it patches, so no version gate is needed.
	inline void Install()
	{
		static bool s_installed = false;
		if (s_installed) {
			return;
		}

		const auto sites = detail::ResolveSites();
		if (!sites) {
			return;  // ResolveSites has already said why
		}

		s_installed = true;

		detail::OrigGate = REL::Relocation<detail::OrigGateFunc_t>{ sites.gate };
		REL::write_call<5>(REL::Relocation<std::uintptr_t>{ sites.mouseCall }, &detail::ShouldBlockMouseLook);
		REL::write_call<5>(REL::Relocation<std::uintptr_t>{ sites.stickCall }, &detail::ShouldBlockGamepadLook);

		const auto base = REL::Module::get().base();
		REX::INFO(
			"Input Swapper: LookHandlerPatches installed - gate rva 0x{:X}, mouse call rva 0x{:X}, "
			"stick call rva 0x{:X} (all derived from VTABLE::LookHandler)"sv,
			sites.gate - base, sites.mouseCall - base, sites.stickCall - base);
	}
}
