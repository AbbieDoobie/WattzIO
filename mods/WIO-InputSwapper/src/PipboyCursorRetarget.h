#pragma once

namespace FalloutInputSwapper::PipboyCursorRetarget
{
	// Fills the two gaps PipboyMenu::RefreshCursor leaves on a device change: it returns early
	// when its cursor-enabled value is unchanged, which on the Map page it is for both devices, and
	// it never touches kAssignCursorToRenderer, which the PipboyMenu constructor sets and nothing
	// clears. The page check, the modal gate and kUsesCursor are vanilla's and are correct.
	inline void Sync(bool a_cursorGamepad)
	{
		const auto pipboy = RE::PipboyManager::GetSingleton();
		if (!pipboy || !RE::QPipboyActive(pipboy)) {
			return;  // nothing to sync if the Pipboy isn't open
		}

		const auto ui = RE::UI::GetSingleton();
		if (!ui) {
			return;
		}
		const auto menu = ui->GetMenu<RE::PipboyMenu>();
		if (!menu) {
			return;
		}
		const auto cursor = RE::MenuCursor::GetSingleton();
		if (!cursor) {
			return;
		}

		menu->UpdateFlag(RE::UI_MENU_FLAGS::kAssignCursorToRenderer, a_cursorGamepad);

		if (a_cursorGamepad) {
			// Back onto the renderer: re-centre, because the pointer is wherever the mouse left
			// it in screen space, and re-apply the constraint the flag implies.
			cursor->CenterCursor();
			pipboy->UpdateCursorConstraint(true);
		} else {
			// Off the renderer. Releasing the constraint is not enough on its own: CursorMenu is
			// still bound to the surface it was last shown on, so it has to be pointed at the
			// Pipboy's own renderer explicitly.
			cursor->ClearConstraints();
			if (const auto model = RE::FlatScreenModel::GetSingleton(); model) {
				const auto flags = static_cast<std::uint32_t>(RE::UI_MENU_FLAGS::kAssignCursorToRenderer);
				RE::BSUIMessageData::SendUIStringUIntMessage(
					RE::CursorMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, model->customRendererName, flags);
			}
		}

		ui->RefreshCursor();

		REX::DEBUG("Input Swapper: Pipboy cursor pass ran (gamepad={})"sv, a_cursorGamepad);
	}
}
