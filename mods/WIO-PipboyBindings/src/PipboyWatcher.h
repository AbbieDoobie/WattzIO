#pragma once

#include "ZoomDispatcher.h"

namespace PipboyPipbindFix
{
	// Tracks whether the Pipboy is open by watching PipboyMenu's MenuOpenCloseEvent, and tells
	// ZoomDispatcher when the menu goes away.
	class PipboyWatcher :
		public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static void Install()
		{
			const auto ui = RE::UI::GetSingleton();
			if (!ui) {
				REX::WARN("Pip-Boy Bindings Fix: RE::UI unavailable - PipboyWatcher not installed"sv);
				return;
			}

			static PipboyWatcher singleton;
			ui->GetEventSource<RE::MenuOpenCloseEvent>()->RegisterSink(&singleton);
		}

		[[nodiscard]] static bool IsPipboyOpen() noexcept { return s_pipboyOpen; }

	private:
		RE::BSEventNotifyControl ProcessEvent(
			const RE::MenuOpenCloseEvent& a_event,
			RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event.menuName == "PipboyMenu"sv) {
				s_pipboyOpen = a_event.opening;

				if (!a_event.opening) {
					ZoomDispatcher::NotifyPipboyClosed();
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		static inline bool s_pipboyOpen = false;
	};
}
