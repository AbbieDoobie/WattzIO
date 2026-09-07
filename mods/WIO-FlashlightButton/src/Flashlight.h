#pragma once

// Toggles the Pip-Boy light through vanilla's own path.
//
// PlayerCharacter::TogglePipBoyLight plays the UI sound, calls ShowPipboyLight, and updates the
// controller light state. Where it is dispatched from matters: ShowPipboyLight's "on" path attaches
// the new light to Get3D(false) - the third-person 3D, unconditionally - and handles first person
// only by detaching that light from its parent again, in a branch gated on Is3rdPersonVisible().
// Run outside TaskQueueInterface's drain, the detach does not take, and the light stays parented to
// the third-person arm or weapon: offset in first person, and unlit while a weapon is drawn.
//
// TaskQueueInterface::QueueTogglePipboyLight is the queue entry vanilla's own Pip-Boy handler uses,
// and is the only dispatch that produces a correct first-person light. F4SE's task interface is not
// interchangeable with it: that queue is pumped from a hook on TaskQueueInterface::ProcessTasks and
// therefore runs after the drain rather than inside it. It is kept below as a fallback because it
// still yields the sound and a light, on a runtime where the engine entry cannot be resolved.
//
// === F4RD RELOCATIONS ========================================================
//   Kind   Site                                        OG       NG/AE
//   id     TaskQueueInterface::QueueTogglePipboyLight   588241   2229290
// =============================================================================
// Confirmed against 1.10.163, 1.10.984, 1.11.221 and 1.11.240: each body writes the same task code,
// and the four differ only in their relocations. The resolved address is byte-checked for that code
// anyway, so a runtime this was not checked on fails over to the F4SE queue instead of calling
// whatever the id landed on.
namespace FMB::Flashlight
{
	namespace detail
	{
		// `mov dword ptr [rsp+disp8], 0x67` - the TogglePipboyLight task code, written into the task
		// struct before the queue push. Identical on 1.10.163, 1.10.984, 1.11.221 and 1.11.240,
		// carries no relocation, and sits within the first 0x40 bytes on all four.
		[[nodiscard]] inline bool CarriesTaskCode(std::uintptr_t a_address)
		{
			const auto* const bytes = reinterpret_cast<const std::uint8_t*>(a_address);
			for (std::size_t i = 0; i + 8 <= 0x40; ++i) {
				if (bytes[i] == 0xC7 && bytes[i + 1] == 0x44 && bytes[i + 2] == 0x24 &&
					bytes[i + 4] == 0x67 && bytes[i + 5] == 0x00 &&
					bytes[i + 6] == 0x00 && bytes[i + 7] == 0x00) {
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] inline bool QueueEngineToggle()
		{
			using func_t = void (*)(RE::TaskQueueInterface*);
			static const auto func = []() -> func_t {
				// F4RD:id - see the banner above.
				constexpr REL::ID kQueueTogglePipboyLight{ 588241, 2229290 };
				const auto address = WIO::Reloc::Address(kQueueTogglePipboyLight);
				if (address == 0) {
					REX::WARN("Flashlight Button: QueueTogglePipboyLight did not resolve - using the F4SE task queue, which leaves the first-person light attached to the third-person model"sv);
					return nullptr;
				}
				if (!CarriesTaskCode(address)) {
					REX::WARN("Flashlight Button: QueueTogglePipboyLight resolved to 0x{:X}, which does not carry the expected task code - using the F4SE task queue instead"sv, address);
					return nullptr;
				}
				return reinterpret_cast<func_t>(address);
			}();

			const auto queue = RE::TaskQueueInterface::GetSingleton();
			if (func == nullptr || queue == nullptr) {
				return false;
			}
			func(queue);
			return true;
		}

		inline void ToggleNow()
		{
			if (const auto player = RE::PlayerCharacter::GetSingleton(); player) {
				player->TogglePipBoyLight();
			}
		}
	}

	inline void Toggle()
	{
		if (detail::QueueEngineToggle()) {
			return;
		}

		// Ordered by fidelity: F4SE's queue runs in the same frame, one step past the engine's own
		// drain; an inline call runs during input processing.
		if (const auto tasks = F4SE::GetTaskInterface(); tasks) {
			tasks->AddTask([] { detail::ToggleNow(); });
			return;
		}
		detail::ToggleNow();
	}
}
