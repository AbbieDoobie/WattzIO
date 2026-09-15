#pragma once

namespace FalloutInputSwapper
{
	// PipboyMenu pushes kVirtualController/kCursor/kLThumbCursor onto ControlMap's input-context
	// priority stack for cursor navigation and does not reliably pop them all on close, burying
	// kMainGameplay so stick and mouse look stop moving the player while context-independent
	// actions keep working. This pops those contexts repeatedly until each is gone.
	class PipboyContextFix :
		public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static void Install()
		{
			const auto ui = RE::UI::GetSingleton();
			if (!ui) {
				REX::WARN("Input Swapper: RE::UI unavailable - Pipboy context fix not installed"sv);
				return;
			}

			static PipboyContextFix singleton;
			ui->GetEventSource<RE::MenuOpenCloseEvent>()->RegisterSink(&singleton);
			REX::INFO("Input Swapper: Pipboy context fix installed"sv);
		}

	private:
		RE::BSEventNotifyControl ProcessEvent(
			const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event.opening || a_event.menuName != "PipboyMenu"sv) {
				return RE::BSEventNotifyControl::kContinue;
			}

			const auto controlMap = RE::ControlMap::GetSingleton();
			if (!controlMap) {
				return RE::BSEventNotifyControl::kContinue;
			}

			using Context = RE::UserEvents::INPUT_CONTEXT_ID;
			std::size_t poppedTotal = 0;
			for (const auto context :
				{ Context::kThumbNav, Context::kVirtualController, Context::kCursor, Context::kLThumbCursor }) {
				while (controlMap->PopInputContext(context)) {
					++poppedTotal;
				}
			}

			if (poppedTotal > 0) {
				REX::DEBUG("Input Swapper: cleared {} stuck Pipboy input context(s) on close"sv, poppedTotal);
			}

			return RE::BSEventNotifyControl::kContinue;
		}
	};
}
