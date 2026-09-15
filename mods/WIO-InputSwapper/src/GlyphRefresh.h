#pragma once

#include "GlyphState.h"
#include "PipboyCursorRetarget.h"
#include "Settings.h"

namespace FalloutInputSwapper::GlyphRefresh
{
	// Cursor visibility reads the two redirected functions live, but on-screen button-prompt icons
	// are pushed to rather than pulled from, so something has to tell the engine the active device
	// changed.
	//
	// a_iconGamepad feeds UpdateGamepadDependentButtonCodes, which is the signal icon selection
	// reads. kUpdateController carries no value; RefreshPlatform() re-queries current state for both
	// icon and cursor when it runs.
	inline void RefreshAllMenus(bool a_iconGamepad)
	{
		RE::UIUtils::UpdateGamepadDependentButtonCodes(a_iconGamepad);

		const auto ui = RE::UI::GetSingleton();
		const auto msgq = RE::UIMessageQueue::GetSingleton();
		if (!ui || !msgq) {
			return;
		}

		RE::BSAutoReadLock lock{ RE::UI::GetMenuMapRWLock() };
		for (const auto& [name, entry] : ui->menuMap) {
			msgq->AddMessage(name, RE::UI_MESSAGE_TYPE::kUpdateController);
		}
	}

	// Called on every IsGamepadConnected query; only acts on an actual transition. Tracks GlyphState's own signal rather
	// than raw DeviceTracker state, so an MCM setting change alone still triggers a refresh.
	inline void Update()
	{
		static bool s_last = false;
		static bool s_initialized = false;

		const bool active = GlyphState::IsGamepadActive();

		if (!s_initialized || active != s_last) {
			RefreshAllMenus(active);

			PipboyCursorRetarget::Sync(active);
			s_last = active;
			s_initialized = true;
			REX::DEBUG("Input Swapper: glyph refresh sent to all open menus (gamepadActive={})"sv, active);
		}
	}
}
