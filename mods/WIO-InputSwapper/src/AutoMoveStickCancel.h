#pragma once

#include <atomic>

#include "Settings.h"

// Lets the movement stick cancel Auto-Move, the way the keyboard's Forward/Back already can.
//
// MovementHandler::OnThumbstickEvent writes the stick into moveInputVec and never touches
// PlayerControlsData::autoMove, so this wraps that slot: original first, then clear autoMove on
// a meaningful Y-axis push, matching vanilla's Forward/Back vs Strafe split. Sync() installs on
// first enable and uninstalls on disable, so the vtable is unmodified while the setting is off.
//
// === F4RD RELOCATIONS ========================================================
//   Kind   Site                                       ID / RVA     +Off
//   vtbl   RE::VTABLE::MovementHandler[0]             F4RD (577025)   -
//   vfunc  BSInputEventUser::OnThumbstickEvent        slot 4          -
// =============================================================================
namespace FalloutInputSwapper::AutoMoveStickCancel
{
	namespace detail
	{
		// RE::BSInputEventUser: ~dtor(0), ShouldHandleEvent(1), OnKinectEvent(2),
		// OnDeviceConnectEvent(3), OnThumbstickEvent(4).
		inline constexpr std::size_t kOnThumbstickEventSlot = 4;

		// Above the resting deadzone, so a centred stick cannot cancel Auto-Move.
		inline constexpr float kCancelThreshold = 0.2f;

		using OnThumbstickEvent_t = void(RE::PlayerInputHandler*, const RE::ThumbstickEvent*);

		// Non-zero only while this wrapper is actually in the vtable.
		inline std::atomic<std::uintptr_t> s_original{ 0 };

		// Set once uninstall has been declined. Sync cannot clear s_original in that case, so the
		// declining branch is reached on every call and its warning would otherwise repeat.
		inline std::atomic<bool> s_uninstallWarned{ false };

		inline void OnThumbstickEvent(RE::PlayerInputHandler* a_this, const RE::ThumbstickEvent* a_event)
		{
			// Vanilla's move-vector write happens first and unconditionally.
			if (const auto original = reinterpret_cast<OnThumbstickEvent_t*>(s_original.load(std::memory_order_relaxed));
				original) {
				original(a_this, a_event);
			}

			if (!a_this || !a_event) {
				return;
			}

			// Re-read every call, so turning the option off in MCM takes effect before Sync() uninstalls.
			if (!Settings::GetStopAutoMoveWithStick()) {
				return;
			}

			// MovementHandler::ShouldHandleEvent already filters to Forward/Back/StrafeLeft/StrafeRight
			// and "Move", so the look stick should never reach here. Checked anyway.
			if (a_event->strUserEvent != "Move"sv) {
				return;
			}

			// Y axis only: forward and back cancel, pure strafing does not, mirroring vanilla's own
			// Forward/Back vs StrafeLeft/Right split in OnButtonEvent.
			if (std::abs(a_event->yValue) <= kCancelThreshold) {
				return;
			}

			if (!a_this->data.autoMove) {
				return;  // nothing to cancel - also keeps the log quiet
			}

			a_this->data.autoMove = false;
			REX::DEBUG("FIS-DIAG AutoMoveStickCancel: cancelled Auto-Move (stick y={:.3f})"sv, a_event->yValue);
		}
	}

	// Idempotent, called per input event from DeviceTracker::ShouldHandleEvent. Installs on the
	// first call the option reads true and uninstalls when it reads false, so off leaves the vtable
	// unmodified rather than modified but inert.
	inline void Sync()
	{
		const bool want = Settings::GetStopAutoMoveWithStick();
		const bool installed = detail::s_original.load(std::memory_order_relaxed) != 0;
		if (want == installed) {
			return;
		}

		// F4RD:vtbl - [0] = MovementHandler's primary vtable, id from F4RD
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::MovementHandler[0] };
		const auto slot = reinterpret_cast<std::uintptr_t*>(
			vtbl.address() + (sizeof(void*) * detail::kOnThumbstickEventSlot));
		const auto ours = REX::UNRESTRICTED_CAST<std::uintptr_t>(&detail::OnThumbstickEvent);

		if (want) {
			// F4RD:vfunc - slot 4 = BSInputEventUser::OnThumbstickEvent
			const auto original = vtbl.write_vfunc(detail::kOnThumbstickEventSlot, &detail::OnThumbstickEvent);
			detail::s_original.store(original, std::memory_order_relaxed);
			detail::s_uninstallWarned.store(false, std::memory_order_relaxed);
			REX::INFO("Input Swapper: AutoMoveStickCancel installed (MovementHandler::OnThumbstickEvent wrapped)"sv);
			return;
		}

		// Uninstall only if the slot still holds this wrapper. Another plugin hooking it afterwards
		// would lose its hook if the saved pointer were written back, and clearing s_original while
		// the wrapper is still reachable would leave it calling a null original. Re-checked every call
		// rather than latched, so uninstall still succeeds if that plugin later restores it.
		if (*slot != ours) {
			if (!detail::s_uninstallWarned.exchange(true, std::memory_order_relaxed)) {
				REX::WARN(
					"Input Swapper: AutoMoveStickCancel left installed - another plugin now owns "
					"MovementHandler::OnThumbstickEvent, and restoring would discard its hook. The "
					"wrapper stays in the call chain and does nothing while the option is off."sv);
			}
			return;
		}

		// F4RD:vfunc - slot 4 again, restoring the original pointer
		vtbl.write_vfunc(detail::kOnThumbstickEventSlot, detail::s_original.load(std::memory_order_relaxed));
		detail::s_original.store(0, std::memory_order_relaxed);
		REX::INFO("Input Swapper: AutoMoveStickCancel uninstalled (vanilla vtable restored)"sv);
	}
}
