#pragma once

#include "Settings.h"

// Silences the vanilla "nothing here to Activate" blip, which otherwise plays on every
// Reload/Holster tap not aimed at anything activatable.
//
// Despite RE::DEFAULT_OBJECT::kNoActivationSound's name the sound is not one of the
// BGSDefaultObjectManager slots, so it is identified by FormID (0x03ec24). The hook is on
// BSISoundDescriptor (BGSSoundDescriptorForm's second base, offset 0x20) and filters by FormID
// inside, so every other sound passes through. DoResolve is the gate, not DoAudibilityTest: the
// sound is non-positional and never triggers an audibility check.
namespace ARC::NoActivationSound
{
	namespace detail
	{
		inline constexpr RE::TESFormID kTargetFormID = 0x03ec24;
	}

	// === F4RD RELOCATIONS ========================================================
	//   Kind   Site                                          ID / RVA     +Off
	//   vtbl   RE::VTABLE::BGSSoundDescriptorForm[1]         F4RD            -
	//   vfunc  BSISoundDescriptor::DoResolve                 slot 1          -
	// =============================================================================
	class Hook
	{
	public:
		// Called once at kGameLoaded. The hook reads Settings::bSilenceActivationSound live, so toggling
		// the MCM switch takes effect on the next Activate press with no reload call.
		static void Install()
		{
			// F4RD:vtbl - [1] = BSISoundDescriptor subobject
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::BGSSoundDescriptorForm[1] };
			// F4RD:vfunc - slot 1 = DoResolve
			_original = vtbl.write_vfunc(1, &Hook::DoResolve);
			REX::INFO("Activate/Reload Combo: Silence Empty Activate Sound hook installed"sv);
		}

	private:
		static bool DoResolve(RE::BSISoundDescriptor* a_this, RE::BSISoundDescriptor::Resolution& a_resolution,
			float a_distance, RE::BSISoundDescriptor::ExtraResolutionData* a_data)
		{
			if (Settings::bSilenceActivationSound) {
				// a_this points at the BSISoundDescriptor subobject, 0x20 into the real BGSSoundDescriptorForm.
				const auto form = reinterpret_cast<RE::BGSSoundDescriptorForm*>(reinterpret_cast<std::byte*>(a_this) - 0x20);
				if (form->GetFormID() == detail::kTargetFormID) {
					return false;  // "failed to resolve" - the caller doesn't play anything
				}
			}
			return _original(a_this, a_resolution, a_distance, a_data);
		}

		using ResolveFunc = bool(RE::BSISoundDescriptor*, RE::BSISoundDescriptor::Resolution&, float, RE::BSISoundDescriptor::ExtraResolutionData*);
		static inline REL::Relocation<ResolveFunc*> _original;
	};
}
