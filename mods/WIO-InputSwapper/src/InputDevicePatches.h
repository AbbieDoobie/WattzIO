#pragma once

#include "DeviceTracker.h"
#include "GlyphRefresh.h"
#include "GlyphState.h"

// Layer A: raw patches against the engine's input-device state. Each verifies the bytes at its own
// site before writing.
//
//   DisableKBMIgnore          NOPs the ControlMap::ignoreKeyboardMouse write vanilla performs on
//                             every device connect or disconnect.
//   DisableDisconnectHandler  Skips one "reason" entry in the disconnect object, so an idle
//                             device is not treated as disconnected.
//   Glyph redirects           Point the IsConnected() call inside IsGamepadConnected and
//                             UsingGamepad at GlyphState::IsGamepadActive().
//   Gamepad look redirects    Point five look-scaling call sites at
//                             DeviceTracker::IsGamepadActiveLooking(), which carries no glyph
//                             hysteresis.
//
// === F4RD RELOCATIONS ========================================================
//   Patch                        OG id   OG off | NG/AE id  NG off  AE off
//   DisableKBMIgnore            647956   0x39   | 2268334   0x40    0x40
//   DisableDisconnectHandler    548136   0x98   | 2249389   0x9C    0x94
//   IsGamepadConnected          609928   0x0D   | 2268387   0x0D    0x0D
//   UsingGamepad                875683   0x0D   | 2268386   0x0D    0x0D
//   LevelUpMenu::ZoomGrid      1349441   0x58   | 2223308   0x58    0x58
//   ProcessLookInput            455462   0x43   | 2234801   0x3C    0x3C
//   ProcessLookControls          53721   0x56   | 2234829   0x56    0x56
//   CalcPitchOffsetChaseValue  1262531   0x1F   | 2248276   0x1F    0x1F
//   heading/yaw counterpart     744430  ABSENT  | 2248275   0x96    0x96
//
// Two traps when re-deriving:
//
//  * DisableKBMIgnore's fill length differs. AE/NG use r13 and carry a REX prefix
//    (7 bytes); OG uses rsi and does not (6). Filling 7 on OG NOPs a byte of the
//    next instruction.
//  * The heading/yaw site does not exist on OG - that function is 0xB2 bytes there
//    against 0x14F on NG/AE and never calls UsingGamepad. An unbounded scan runs
//    past its end into the next function, so offsets are found by scanning within
//    each function's own .pdata bounds.
// =============================================================================
namespace FalloutInputSwapper::InputDevicePatches
{
	// No runtime-version gate: every write compares against the real bytes at its own site first,
	// per patch rather than all-or-nothing, so a wrong address costs that patch and logs it.

	// All four groups are enabled; the flags remain so one can be switched off during triage.
	inline constexpr bool kEnableDisableKBMIgnore = true;
	inline constexpr bool kEnableDisableDisconnectHandler = true;
	inline constexpr bool kEnableGlyphRedirectPatches = true;

	// The glyph and look redirects are call-site patches (write_call<6> and write_call<5>) rather
	// than in-place byte fills, so main.cpp must initialise a trampoline before Install() runs.
	inline constexpr bool kEnableGamepadLookPatches = true;

	namespace detail
	{
		// Per-family ids and interior offsets. Each write is checked against the bytes it
		// expects to find before anything is written.
		[[nodiscard]] inline bool IsOG() { return REL::Module::get().is_og(); }

		// Resolves through a_id.id(), F4RD's family selector, not the REL::ID itself.
		// resolve(const ID&) pattern-scans the AE id first on OG, so a false-positive match can
		// outrank a correct OG id. Returns 0 rather than aborting: REL::Relocation's constructor
		// calls report_and_fail.
		[[nodiscard]] inline std::uintptr_t Resolve(const REL::ID& a_id)
		{
			const auto result = REL::IDDatabase::get().resolve(a_id.id());
			return result.rva ? REL::Module::get().base() + *result.rva : 0;
		}

		[[nodiscard]] inline bool BytesAre(
			std::uintptr_t a_addr, std::initializer_list<std::uint8_t> a_expected)
		{
			const auto bytes = reinterpret_cast<const std::uint8_t*>(a_addr);
			std::size_t i = 0;
			for (const auto b : a_expected) {
				if (bytes[i++] != b) {
					return false;
				}
			}
			return true;
		}

		// A 5-byte E8 rel32 CALL reaching exactly a_callee.
		[[nodiscard]] inline bool IsCallTo(std::uintptr_t a_site, std::uintptr_t a_callee)
		{
			const auto bytes = reinterpret_cast<const std::uint8_t*>(a_site);
			if (bytes[0] != 0xE8) {
				return false;
			}
			std::int32_t rel{};
			std::memcpy(&rel, bytes + 1, sizeof(rel));
			return a_site + 5 + rel == a_callee;
		}

		// DisableKBMIgnore. Removes the engine's "gate keyboard/mouse input while gamepad is active"
		// path by NOPing `mov byte ptr [reg+0x141], al`, the write to
		// ControlMap::ignoreKeyboardMouse. Its length differs by family: AE and NG use r13 and carry
		// a REX prefix (7 bytes), OG uses rsi and does not (6).
		//   AE/NG  id 2268334  +0x40  41 88 85 41 01 00 00   (7)
		//   OG     id  647956  +0x39  88 86 41 01 00 00      (6)
		[[nodiscard]] inline bool DisableKBMIgnore()
		{
			// F4RD:id + F4RD:off
			constexpr REL::ID kTarget{ 647956, 2268334 };
			constexpr REL::VariantOffset kOffset{ 0x39, 0x40 };
			const auto fn = Resolve(kTarget);
			if (!fn) {
				return false;
			}
			const auto site = fn + kOffset.offset();
			const bool ok = IsOG() ? BytesAre(site, { 0x88, 0x86, 0x41, 0x01, 0x00, 0x00 })
			                       : BytesAre(site, { 0x41, 0x88, 0x85, 0x41, 0x01, 0x00, 0x00 });
			if (!ok) {
				REX::WARN("Input Swapper: DisableKBMIgnore site does not match - not patched"sv);
				return false;
			}
			REL::write_fill(site, REL::NOP, IsOG() ? 0x6 : 0x7);
			return true;
		}

		// DisableDisconnectHandler stops the engine treating the currently-idle device as disconnected.
		// The function builds six structurally identical "reason" entries at this+0x38 through +0x60;
		// this NOPs the first, the 0x4E fill being the distance to the second. Each block opens
		// `cmp dword ptr [rip+rel32], 2 ; je +0x13 ; lea rdx, ...`, the opcode framing the check below
		// matches, with the rel32s differing per build.
		//   AE  id 2249389  +0x94     NG  +0x9C     OG  id 548136  +0x98
		[[nodiscard]] inline bool DisableDisconnectHandler()
		{
			// F4RD:id + F4RD:off - the offset differs on all three families
			constexpr REL::ID kTarget{ 548136, 2249389 };
			constexpr REL::VariantOffset kOffset{ 0x98, 0x9C, 0x94 };
			const auto fn = Resolve(kTarget);
			if (!fn) {
				return false;
			}
			const auto site = fn + kOffset.offset();
			const auto bytes = reinterpret_cast<const std::uint8_t*>(site);
			const bool ok = bytes[0] == 0x83 && bytes[1] == 0x3D && bytes[6] == 0x02 &&
			                bytes[7] == 0x74 && bytes[8] == 0x13 &&
			                bytes[9] == 0x48 && bytes[10] == 0x8D && bytes[11] == 0x15;
			if (!ok) {
				REX::WARN("Input Swapper: DisableDisconnectHandler block does not match - not patched"sv);
				return false;
			}
			REL::write_fill(site, REL::NOP, 0x4E);
			return true;
		}

		// Redirects the engine's own "is the gamepad the active device" queries so they read this mod's
		// tracked state instead of vanilla's flip-on-every-event logic, which is what lets Layer B's
		// hysteresis control what the game displays. rcx holds devices[kGamepad] at the patch point,
		// not the manager, the replaced instructions being the load of that pointer's vtable and the
		// virtual call through it. The argument is unused.

		inline bool IsGamepadConnected([[maybe_unused]] const RE::BSInputDevice* a_gamepad)
		{
			// The glyph-refresh transition check rides here, the engine already calling this function
			// regularly, which is what makes cursor visibility track. See GlyphRefresh.h for why a push
			// is also needed to redraw button-prompt icons.
			GlyphRefresh::Update();

			// One signal drives both icons and cursor visibility and must stay that way: vanilla derives
			// icon selection, cursor visibility and the Scaleform SetPlatform push from this one boolean
			// inside IMenu::RefreshPlatform, so pinning icons to gamepad suppresses the mouse cursor on
			// every menu except the Pipboy, which has its own PipboyCursorRetarget::Sync path.
			return GlyphState::IsGamepadActive();
		}

		inline bool UsingGamepad([[maybe_unused]] const RE::BSInputDevice* a_gamepad)
		{
			// See IsGamepadConnected's comment above - same reasoning.
			return GlyphState::IsGamepadActive();
		}

		// IsGamepadConnected and UsingGamepad are two separate 0x25-byte functions sitting 0x30
		// apart, UsingGamepad first. Their bodies are byte-identical to each other and to
		// themselves across OG, NG and AE:
		//
		//   +0x00  48 83 EC 28     sub  rsp, 0x28
		//   +0x04  48 8B 49 18     mov  rcx, [rcx+0x18]      devices[kGamepad]
		//   +0x08  48 85 C9        test rcx, rcx
		//   +0x0B  74 11           je   -> return false
		//   +0x0D  48 8B 01        mov  rax, [rcx]           vtable
		//   +0x10  FF 50 18        call [rax+0x18]           IsConnected()
		//   +0x13  84 C0           test al, al
		//
		// The single virtual call at +0x0D is what every caller funnels through, 22 and 47 direct
		// callers respectively on AE, so redirecting it reaches all of them through one 6-byte write per
		// function and leaves the rest, including the null guard, intact. Both bodies being identical,
		// one pattern covers both. The technique is from Exit-9B's Auto Input Switch
		// (github.com/Exit-9B/AutoInputSwitch, MIT), which patches the same two functions at the same
		// +0x0D on Skyrim.
		inline constexpr std::uint8_t kGamepadQueryCall[]{ 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x18 };
		inline constexpr std::size_t  kGamepadQueryCallOffset = 0x0D;

		// Identifies the function before the call site inside it is trusted.
		inline constexpr std::uint8_t kGamepadQueryBody[]{
			0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x49, 0x18, 0x48, 0x85, 0xC9, 0x74, 0x11
		};

		template <class F>
		[[nodiscard]] inline bool PatchGamepadQuery(std::uintptr_t a_fn, F a_replacement, std::string_view a_name)
		{
			if (!a_fn) {
				return false;
			}
			const auto bytes = reinterpret_cast<const std::uint8_t*>(a_fn);
			for (std::size_t i = 0; i < sizeof(kGamepadQueryBody); ++i) {
				if (bytes[i] != kGamepadQueryBody[i]) {
					REX::WARN("Input Swapper: {} does not match the expected body - not patched"sv, a_name);
					return false;
				}
			}
			const auto site = a_fn + kGamepadQueryCallOffset;
			const auto call = reinterpret_cast<const std::uint8_t*>(site);
			for (std::size_t i = 0; i < sizeof(kGamepadQueryCall); ++i) {
				if (call[i] != kGamepadQueryCall[i]) {
					REX::WARN("Input Swapper: {} does not call IsConnected at +{:#x} - not patched"sv,
						a_name, kGamepadQueryCallOffset);
					return false;
				}
			}

			// write_call<6> emits FF 15 rel32, a 6-byte indirect call through a trampoline slot,
			// which is exactly the span of the two instructions being replaced.
			REL::write_call<6>(REL::Relocation<std::uintptr_t>{ site }, a_replacement);
			return true;
		}

		[[nodiscard]] inline bool InstallGamepadConnectedPatch()
		{
			// F4RD:id + F4RD:off - the call site is at a fixed +0x0D on every family
			constexpr REL::ID kTarget{ 609928, 2268387 };
			return PatchGamepadQuery(Resolve(kTarget), &IsGamepadConnected, "IsGamepadConnected"sv);
		}

		[[nodiscard]] inline bool InstallUsingGamepadPatch()
		{
			// F4RD:id + F4RD:off - the call site is at a fixed +0x0D on every family
			constexpr REL::ID kTarget{ 875683, 2268386 };
			return PatchGamepadQuery(Resolve(kTarget), &UsingGamepad, "UsingGamepad"sv);
		}

		// Redirects the call sites asking vanilla whether the gamepad is the active look device to
		// DeviceTracker::IsGamepadActiveLooking(), updated same-frame on every real look event. A
		// call-site redirect (write_call<5>) rather than a byte fill or full-function replace. The call
		// counter and throttled log below are a tripwire for a silently dead redirect: a wrong offset
		// lands on non-call bytes and looks like a working patch from outside.
		inline std::atomic<std::uint64_t> s_lookQueryCallCount{ 0 };

		inline bool UsingGamepadLook([[maybe_unused]] const RE::BSInputDeviceManager& a_this)
		{
			s_lookQueryCallCount.fetch_add(1, std::memory_order_relaxed);

			const bool result = FalloutInputSwapper::DeviceTracker::GetSingleton()->IsGamepadActiveLooking();

			static std::atomic<std::int64_t> lastLogMs{ 0 };
			const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch())
			                       .count();
			auto last = lastLogMs.load(std::memory_order_relaxed);
			if (nowMs - last > 500 &&
			    lastLogMs.compare_exchange_strong(last, nowMs, std::memory_order_relaxed)) {
				REX::DEBUG("FIS-DIAG gamepad-look-query calls={} result={}"sv,
					s_lookQueryCallCount.load(std::memory_order_relaxed), result);
			}

			return result;
		}

		// Each site is a 5-byte E8 CALL to UsingGamepad, and every one is verified to reach it before
		// any is written, so a stale offset costs the feature rather than corrupting code. Offsets are
		// found by scanning within each function's own .pdata bounds: an unbounded scan runs past the
		// end of the OG heading/yaw function into the next function's call, making two sites patch one
		// address.
		//
		//   site                   OG id     OG off | NG/AE id  NG/AE off
		//   LevelUpMenu::ZoomGrid  1349441   0x58   | 2223308   0x58
		//   ProcessLookInput        455462   0x43   | 2234801   0x3C
		//   ProcessLookControls      53721   0x56   | 2234829   0x56
		//   CalcPitchOffsetChase   1262531   0x1F   | 2248276   0x1F
		//   heading/yaw sibling     744430   ABSENT | 2248275   0x96
		//
		// The fifth site is FirstPersonState's heading/yaw counterpart to CalculatePitchOffsetChaseValue:
		// same chase-value architecture and UsingGamepad gate, but feeding
		// IAnimationGraphManagerHolder::Retarget3D, so real camera rotation rather than a stored value.
		// Without it the site falls through to the global UsingGamepad replacement and reads the
		// hysteresis-smoothed glyph signal, which is wrong for a per-frame camera calculation. It does
		// not exist on OG, where that function is 0xB2 bytes against 0x14F on NG/AE and contains no
		// call to UsingGamepad. kAbsent skips it rather than failing.
		inline constexpr std::ptrdiff_t kAbsent = -1;
		struct LookSite
		{
			REL::ID id;
			REL::VariantOffset offset;
			const char* name;
		};

		inline constexpr LookSite kLookSites[]{
			{ REL::ID{ 1349441, 2223308 }, REL::VariantOffset{ 0x58, 0x58 }, "LevelUpMenu::ZoomGrid" },
			{ REL::ID{ 455462, 2234801 }, REL::VariantOffset{ 0x43, 0x3C }, "PlayerControls::ProcessLookInput" },
			{ REL::ID{ 53721, 2234829 }, REL::VariantOffset{ 0x56, 0x56 }, "PlayerControlsUtils::ProcessLookControls" },
			{ REL::ID{ 1262531, 2248276 }, REL::VariantOffset{ 0x1F, 0x1F }, "FirstPersonState::CalculatePitchOffsetChaseValue" },
			{ REL::ID{ 744430, 2248275 }, REL::VariantOffset{ kAbsent, 0x96 }, "FirstPersonState heading/yaw counterpart" },
		};

		[[nodiscard]] inline bool InstallGamepadLookPatches()
		{
			// F4RD:id - the callee every site below must reach
			constexpr REL::ID kUsingGamepad{ 875683, 2268386 };
			const auto usingGamepad = Resolve(kUsingGamepad);
			if (!usingGamepad) {
				return false;
			}

			// F4RD:id + F4RD:off - resolve and verify every applicable site before
			// writing any of them.
			std::uintptr_t sites[std::size(kLookSites)]{};
			for (std::size_t i = 0; i < std::size(kLookSites); ++i) {
				const auto& s = kLookSites[i];
				const auto offset = s.offset.offset();
				if (offset == kAbsent) {
					REX::INFO("Input Swapper: {} has no UsingGamepad call on this runtime - skipped"sv, s.name);
					continue;
				}
				const auto fn = Resolve(s.id);
				if (!fn) {
					REX::WARN("Input Swapper: could not resolve {} - gamepad-look patches skipped"sv, s.name);
					return false;
				}
				const auto site = fn + offset;
				if (!IsCallTo(site, usingGamepad)) {
					REX::WARN(
						"Input Swapper: {} does not call UsingGamepad at the expected offset - "
						"gamepad-look patches skipped"sv,
						s.name);
					return false;
				}
				sites[i] = site;
			}

			for (const auto site : sites) {
				if (site) {
					REL::write_call<5>(REL::Relocation<std::uintptr_t>{ site }, &UsingGamepadLook);
				}
			}
			return true;
		}
	}

	// Safe from F4SEPlugin_Load: these patch code at static instruction addresses, mapped the
	// moment the exe loads, so unlike DeviceTracker's MenuControls hook there is no live engine
	// object to wait for. s_installed guards against double-patching.
	inline void Install()
	{
		static bool s_installed = false;
		if (s_installed) {
			return;
		}

		if constexpr (!kEnableDisableKBMIgnore && !kEnableDisableDisconnectHandler && !kEnableGlyphRedirectPatches &&
		              !kEnableGamepadLookPatches) {
			REX::INFO("Input Swapper: all Layer A raw patches disabled - see InputDevicePatches.h"sv);
			return;
		}

		s_installed = true;

		REX::INFO("Input Swapper: installing Layer A on {} ({})"sv,
			REL::Module::get().version().string(),
			REL::Module::get().is_og() ? "OG"sv
									   : (REL::Module::get().is_ng() ? "NG"sv : "AE"sv));

		if constexpr (kEnableDisableKBMIgnore) {
			REX::INFO("Input Swapper: DisableKBMIgnore {}"sv,
				detail::DisableKBMIgnore() ? "installed"sv : "SKIPPED"sv);
		}

		if constexpr (kEnableDisableDisconnectHandler) {
			REX::INFO("Input Swapper: DisableDisconnectHandler {}"sv,
				detail::DisableDisconnectHandler() ? "installed"sv : "SKIPPED"sv);
		}

		if constexpr (kEnableGlyphRedirectPatches) {
			const bool connected = detail::InstallGamepadConnectedPatch();
			const bool using_ = detail::InstallUsingGamepadPatch();
			REX::INFO("Input Swapper: glyph redirects - IsGamepadConnected {}, UsingGamepad {}"sv,
				connected ? "installed"sv : "SKIPPED"sv, using_ ? "installed"sv : "SKIPPED"sv);
		}

		if constexpr (kEnableGamepadLookPatches) {
			REX::INFO("Input Swapper: gamepad-look call-site redirects {}"sv,
				detail::InstallGamepadLookPatches() ? "installed"sv : "SKIPPED"sv);
		}
	}
}
