#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

// Menu-vs-prompt classification for hotkey blocking.
//
// contextPriorityStack.back() != kMainGameplay treats every non-gameplay context as a menu,
// which over-blocks: the engine pushes its own contexts for ordinary crosshair button prompts
// like "A to Activate". kSpecialActivateRollover (0x1A) and kTwoButtonRollover (0x1B) are those
// prompt contexts. kQuickContainerMenu (0x19) looks like one but classifies as a menu, because
// its CustomControlMap.txt block carries its own Up/Down navigation entries.
//
// Depends on nothing but pch.h; only the namespace and log prefix differ between copies.
namespace TSO::MenuContext
{
	// Mirrors the MCM dropdown: raw 0-based option indices, not an engine enum.
	enum class BlockMode : std::int32_t
	{
		kMenusOnly = 0,        // block menus, allow crosshair prompts (default)
		kPromptsAndMenus = 1,  // block anything that isn't plain gameplay
		kOff = 2               // never block
	};

	namespace detail
	{
		using CtxID = RE::UserEvents::INPUT_CONTEXT_ID;

		// Contexts where the player is still playing: aiming, riding, or looking at a button prompt.
		// Anything unlisted counts as a menu, so a new context blocks rather than leaks. kMultiActivate
		// (0x0F) and kSitWait (0x12) are absent deliberately, a 4-way choice and a wait timer being
		// more than a basic prompt.
		inline constexpr std::array kPromptContexts{
			CtxID::kMainGameplay,             // 0x00
			CtxID::kScope,                    // 0x11 - aiming, not a menu at all
			CtxID::kSpecialActivateRollover,  // 0x1A
			CtxID::kTwoButtonRollover,        // 0x1B
			CtxID::kVertiBird                 // 0x1D
		};

		[[nodiscard]] constexpr bool IsPromptContext(CtxID a_ctx)
		{
			for (const auto ctx : kPromptContexts) {
				if (ctx == a_ctx) {
					return true;
				}
			}
			return false;
		}

		// Diagnostic only, so an unlisted value falling through to the hex-only branch is fine.
		[[nodiscard]] inline const char* ContextName(CtxID a_ctx)
		{
			switch (a_ctx) {
				case CtxID::kMainGameplay: return "MainGameplay";
				case CtxID::kBasicMenuNav: return "BasicMenuNav";
				case CtxID::kThumbNav: return "ThumbNav";
				case CtxID::kVirtualController: return "VirtualController";
				case CtxID::kCursor: return "Cursor";
				case CtxID::kLThumbCursor: return "LThumbCursor";
				case CtxID::kConsole: return "Console";
				case CtxID::kDebugText: return "DebugText";
				case CtxID::kBook: return "Book";
				case CtxID::kDebugOverlay: return "DebugOverlay";
				case CtxID::kTFC: return "TFC";
				case CtxID::kDebugMap: return "DebugMap";
				case CtxID::kLockpick: return "Lockpick";
				case CtxID::kVATS: return "VATS";
				case CtxID::kVATSPlayback: return "VATSPlayback";
				case CtxID::kMultiActivate: return "MultiActivate";
				case CtxID::kWorkshop: return "Workshop";
				case CtxID::kScope: return "Scope";
				case CtxID::kSitWait: return "SitWait";
				case CtxID::kLooksMenu: return "LooksMenu";
				case CtxID::kWorkshopAddendum: return "WorkshopAddendum";
				case CtxID::kPauseMenu: return "PauseMenu";
				case CtxID::kLevelupMenu: return "LevelUpMenu";
				case CtxID::kLevelupMenuPrevNext: return "LevelUpMenuPrevNext";
				case CtxID::kMainMenu: return "MainMenu";
				case CtxID::kQuickContainerMenu: return "QuickContainerMenu";
				case CtxID::kSpecialActivateRollover: return "SpecialActivateRollover";
				case CtxID::kTwoButtonRollover: return "TwoButtonRollover";
				case CtxID::kQuickContainerMenuPerk: return "QuickContainerMenuPerk";
				case CtxID::kVertiBird: return "Vertibird";
				case CtxID::kPlayBinkMenu: return "PlayBinkMenu";
				case CtxID::kRobotModAddendum: return "RobotModAddendum";
				case CtxID::kCreationClub: return "CreationClub";
				case CtxID::kNone: return "None";
				default: return "?";
			}
		}
	}

	// Whether the crosshair is on something activatable, i.e. whether a button prompt is up.
	// RE::PlayerCharacter is a BSTEventSource<PickRefUpdateEvent>, which broadcasts whenever the
	// pick target changes. That event has no commonlibf4 header; its payload's first 4 bytes are a
	// live RE::ObjectRefHandle.
	class CrosshairWatcher :
		public RE::BSTEventSink<RE::PickRefUpdateEvent>
	{
	public:
		// Called at kGameLoaded and again on every pause-menu close, because PlayerCharacter's
		// singleton is not reliably populated at kGameLoaded. s_installed makes the repeats free.
		static void Install()
		{
			if (s_installed) {
				return;
			}
			const auto pc = RE::PlayerCharacter::GetSingleton();
			if (!pc) {
				return;  // not up yet - the pause-menu-close call will retry
			}
			static CrosshairWatcher singleton;
			// PlayerCharacter has several BSTEventSource<T> bases, so RegisterSink is ambiguous
			// through the derived pointer. Name the specific base.
			static_cast<RE::BSTEventSource<RE::PickRefUpdateEvent>*>(pc)->RegisterSink(&singleton);
			s_installed = true;
			REX::INFO("Throwing System Overhaul: crosshair-target watcher installed"sv);
		}

		[[nodiscard]] static bool HasTarget() { return s_hasTarget; }

	private:
		RE::BSEventNotifyControl ProcessEvent(const RE::PickRefUpdateEvent& a_event, RE::BSTEventSource<RE::PickRefUpdateEvent>*) override
		{
			// ObjectRefHandle has no public constructor from a raw handle value, and its layout is
			// one uint32 with no vtable, so the payload is read in place as the real type.
			const auto handle = *reinterpret_cast<const RE::ObjectRefHandle*>(&a_event);
			s_hasTarget = static_cast<bool>(handle.get());
			return RE::BSEventNotifyControl::kContinue;
		}

		static inline std::atomic<bool> s_hasTarget{ false };
		static inline bool              s_installed{ false };
	};

	// Raw CrosshairMode, the only signal that identifies companion command mode. RE::CrosshairMode
	// is forward-declared with no enumerator names in this fork, hence the raw integer.
	class CrosshairModeWatcher :
		public RE::BSTEventSink<RE::PlayerCrosshairModeEvent>
	{
	public:
		static void Install()
		{
			if (s_installed) {
				return;
			}
			const auto source = RE::PlayerCrosshairModeEvent::GetEventSource();
			if (!source) {
				return;  // retried on pause-menu close
			}
			static CrosshairModeWatcher singleton;
			source->RegisterSink(&singleton);
			s_installed = true;
			REX::INFO("Throwing System Overhaul: crosshair-mode watcher installed"sv);
		}

		[[nodiscard]] static std::int32_t Mode() { return s_mode; }

	private:
		RE::BSEventNotifyControl ProcessEvent(const RE::PlayerCrosshairModeEvent& a_event, RE::BSTEventSource<RE::PlayerCrosshairModeEvent>*) override
		{
			// BSTValueEvent<CrosshairMode>::optionalValue. CrosshairMode is incomplete here, so
			// read the payload's value slot as a raw int rather than naming the enum.
			if (a_event.optionalValue.has_value()) {
				s_mode = *reinterpret_cast<const std::int32_t*>(std::addressof(a_event.optionalValue.value()));
			} else {
				s_mode = -1;
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		static inline std::atomic<std::int32_t> s_mode{ -1 };
		static inline bool                      s_installed{ false };
	};

	// One frame's worth of observed state. Captured once per frame by the input hook, then
	// consulted per slot, never re-captured per slot or per event.
	struct State
	{
		bool anyMenuContext = false;        // a context outside the prompt allow-list is on the stack
		bool anyNonGameplayContext = false; // anything other than kMainGameplay is on the stack
		bool promptActive = false;          // a crosshair button prompt is on screen
		bool commandMode = false;           // companion Direct/Order mode (pointer cursor)
		bool dialogueOpen = false;
		bool quickloot = false;
	};

	namespace detail
	{
		// Companion command mode (the pointer cursor from holding Activate on a companion) pushes no
		// ControlMap context and opens no menu, so CrosshairMode is the only signal: 1 in ordinary
		// play, 8 while looking at the companion, 7 while command mode is up. The COMMAND_TYPE family
		// tracks the companion's standing order rather than the ordering UI, so it reads true during
		// ordinary play.
		inline constexpr std::int32_t kCrosshairModeCommand = 7;
	}

	// Scans the whole contextPriorityStack, not just back(): quickloot (0x19) and a rollover
	// (0x1A/0x1B) can be live at once, and back() would let whichever landed on top decide.
	// Compares with .get(), never .all(): TEnumSet::all(x) is (impl & mask) == mask and
	// kMainGameplay is 0x0, so .all(kMainGameplay) is unconditionally true.
	[[nodiscard]] inline State Capture()
	{
		State state;

		const auto ui = RE::UI::GetSingleton();

		// Not redundant with the context scan: DialogueMenu's constructor sets currentContext = kNone
		// and never pushes a context, so a conversation is invisible to the stack.
		if (ui && ui->GetMenuOpen("DialogueMenu")) {
			state.dialogueOpen = true;
		}

		// Vanilla quickloot: a plain bool on PlayerInputHandler. ActivateHandler is only
		// forward-declared here, but its VTABLE array is size 1 with the base at offset 0, so the
		// same-address reinterpret_cast is safe.
		if (const auto pc = RE::PlayerControls::GetSingleton(); pc && pc->activateHandler) {
			const auto handler = reinterpret_cast<const RE::PlayerInputHandler*>(pc->activateHandler);
			if (handler->inQuickContainer) {
				state.quickloot = true;
			}
		}

		if (const auto controlMap = RE::ControlMap::GetSingleton(); controlMap) {
			for (const auto& entry : controlMap->contextPriorityStack) {
				const auto ctx = entry.get();
				if (ctx != detail::CtxID::kMainGameplay) {
					state.anyNonGameplayContext = true;
				}
				if (!detail::IsPromptContext(ctx)) {
					state.anyMenuContext = true;
				}
				if (ctx == detail::CtxID::kSpecialActivateRollover || ctx == detail::CtxID::kTwoButtonRollover) {
					state.promptActive = true;
				}
			}
		}

		// The reliable half of promptActive. The rollover contexts above contribute when they
		// are present, but they flicker and cannot be relied on alone.
		if (CrosshairWatcher::HasTarget()) {
			state.promptActive = true;
		}

		state.commandMode = CrosshairModeWatcher::Mode() == detail::kCrosshairModeCommand;

		return state;
	}

	[[nodiscard]] inline bool ShouldBlock(const State& a_state, BlockMode a_mode)
	{
		if (a_mode == BlockMode::kOff) {
			return false;
		}

		// Dialogue, quickloot and companion command mode block under both blocking modes. Quickloot is
		// a basic prompt that also puts a navigable window on screen; command mode is a targeting UI
		// with its own cursor.
		if (a_state.dialogueOpen || a_state.quickloot || a_state.commandMode) {
			return true;
		}

		if (a_state.anyMenuContext) {
			return true;  // a menu-class context blocks under both blocking modes
		}
		if (a_mode == BlockMode::kMenusOnly) {
			return false;
		}
		// kPromptsAndMenus additionally blocks on anything that is not plain gameplay: the
		// rollover contexts when they are up, and a live crosshair prompt.
		return a_state.anyNonGameplayContext || a_state.promptActive;
	}

	// Change-detected state trace, enabled by the hidden debug-log ini key. Returns the line rather
	// than logging it, so the caller logs only when it differs from the previous one. Anything
	// continuously varying must be quantised first, or every frame renders a different line, which
	// is why commandTimer is reduced to a bool.
	[[nodiscard]] inline std::string BuildDiagnosticLine(const State& a_state)
	{
		std::string ctx;
		if (const auto controlMap = RE::ControlMap::GetSingleton(); controlMap) {
			for (const auto& entry : controlMap->contextPriorityStack) {
				const auto id = entry.get();
				ctx += std::format("{:02X}:{} ", static_cast<std::int32_t>(id), detail::ContextName(id));
			}
		}
		if (!ctx.empty()) {
			ctx.pop_back();
		}

		// Every menu on the stack, with kAlwaysOpen ones suffixed "*" rather than hidden.
		// Filtering them out would conceal a menu that carries the flag.
		std::string menus;
		if (const auto ui = RE::UI::GetSingleton(); ui) {
			for (const auto& menu : ui->menuStack) {
				if (menu) {
					menus += std::format("{}{} ", menu->menuName.c_str(),
						menu->menuFlags.all(RE::UI_MENU_FLAGS::kAlwaysOpen) ? "*" : "");
				}
			}
		}
		if (!menus.empty()) {
			menus.pop_back();
		}

		// Raw command-family values. The derived booleans do not discriminate command mode.
		std::uint32_t actorDoingID = 0;
		std::uint32_t cmdTargetID = 0;
		std::int32_t  cmdTypeRaw = -1;
		std::int32_t  cmdCurRaw = -1;
		bool          cmdTimerRunning = false;
		std::uint32_t menuModeRaw = 0;
		if (const auto pc = RE::PlayerCharacter::GetSingleton(); pc) {
			if (const auto actor = pc->actorDoingPlayerCommand.get()) {
				actorDoingID = actor->GetFormID();
			}
			if (pc->commandTarget) {
				cmdTargetID = pc->commandTarget->GetFormID();
			}
			if (pc->playerCurrentCommandType.optionalValue.has_value()) {
				cmdTypeRaw = static_cast<std::int32_t>(pc->playerCurrentCommandType.optionalValue.value());
			}
			cmdCurRaw = static_cast<std::int32_t>(pc->currentCommand.get());
			cmdTimerRunning = pc->commandTimer > 0.0F;
		}
		if (const auto ui = RE::UI::GetSingleton(); ui) {
			menuModeRaw = ui->menuMode;
		}
		const auto pcon = RE::PlayerControls::GetSingleton();
		const bool blockInput = pcon && pcon->blockPlayerInput;

		return std::format(
			"ctx=[{}] menus=[{}] menuMode={} xhair(target={} mode={}) quickloot={} dialogue={} blockInput={} "
			"cmd(actor={:08X} type={} cur={} target={:08X} timer={}) -> menuCtx={} promptActive={} nonGameplayCtx={}",
			ctx, menus, menuModeRaw, CrosshairWatcher::HasTarget(), CrosshairModeWatcher::Mode(),
			a_state.quickloot, a_state.dialogueOpen, blockInput,
			actorDoingID, cmdTypeRaw, cmdCurRaw, cmdTargetID, cmdTimerRunning,
			a_state.anyMenuContext, a_state.promptActive, a_state.anyNonGameplayContext);
	}
}
