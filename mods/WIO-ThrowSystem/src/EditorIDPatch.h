#pragma once

#include <string>
#include <unordered_map>

namespace TSO::EditorIDPatch
{
	// Makes TESForm::GetFormEditorID() and GetFormByEditorID() work for weapon forms (grenades and
	// mines). The engine receives each form's real EditorID via SetFormEditorID at load time, but
	// most form types' vtables discard it with a no-op stub. shad0wshayd3's Baka Framework is
	// credited in the readme and the MCM credits widget for establishing that it can be recovered
	// this way; the vtable slots below are properties of TESForm rather than of any implementation.
	//
	// Hooks GetFormEditorID (slot 0x3A) and SetFormEditorID (0x3B) for TESObjectWEAP only: the
	// setter intercepts the string the engine already passes at form-load time and adds it to the
	// same global AllFormsByEditorID map GetFormByEditorID() reads, plus a local reverse map keyed
	// by FormID. Must install before ESM/ESP data loads, i.e. before those setter calls happen.

	namespace detail
	{
		inline std::unordered_map<std::uint32_t, std::string> g_editorIDsByFormID;

		// The setter runs during data load and the getter from anything asking a weapon for its
		// EditorID, this being a vtable hook rather than a private entry point. An insert can rehash,
		// reallocating the bucket array a concurrent lookup is walking, so the map is guarded the way
		// the engine guards the global one below: shared reads, exclusive insert.
		inline RE::BSReadWriteLock g_editorIDLock;

		inline void AddToGlobalMap(RE::TESForm* a_form, const char* a_editorID)
		{
			const auto& [map, lock] = RE::TESForm::GetAllFormsByEditorID();
			const RE::BSAutoWriteLock l{ lock };
			if (map) {
				map->emplace(a_editorID, a_form);
			}
		}
	}

	// === F4RD RELOCATIONS ========================================================
	//   Kind   Site                                          ID / RVA     +Off
	//   vtbl   T::VTABLE[0]  (T = RE::TESObjectWEAP)         F4RD            -
	//   vfunc  TESForm::GetFormEditorID                      slot 0x3A       -
	//   vfunc  TESForm::SetFormEditorID                      slot 0x3B       -
	//
	// Re-derive: count the virtuals in RE/T/TESForm.h up to GetFormEditorID and
	// SetFormEditorID in the CommonLibF4RD version being built against.
	// =============================================================================
	template <class T>
	class Hook
	{
	public:
		static void Install()
		{
			// F4RD:vtbl - [0] = T's primary vtable
			REL::Relocation<std::uintptr_t> vtbl{ T::VTABLE[0] };
			// F4RD:vfunc - slots 0x3A / 0x3B = TESForm::Get/SetFormEditorID
			_getFormEditorID = vtbl.write_vfunc(0x3A, &Hook::GetFormEditorID);
			_setFormEditorID = vtbl.write_vfunc(0x3B, &Hook::SetFormEditorID);
		}

	private:
		static const char* GetFormEditorID(RE::TESForm* a_this)
		{
			{
				const RE::BSAutoReadLock l{ detail::g_editorIDLock };
				const auto               it = detail::g_editorIDsByFormID.find(a_this->GetFormID());
				if (it != detail::g_editorIDsByFormID.end()) {
					// Safe past the lock: a node is never erased or reassigned once inserted, and a rehash
					// relinks nodes rather than moving the strings they own.
					return it->second.c_str();
				}
			}
			return _getFormEditorID(a_this);
		}

		static bool SetFormEditorID(RE::TESForm* a_this, const char* a_editorID)
		{
			if (a_editorID && a_editorID[0] != '\0') {
				{
					const RE::BSAutoWriteLock l{ detail::g_editorIDLock };
					detail::g_editorIDsByFormID[a_this->GetFormID()] = a_editorID;
				}
				detail::AddToGlobalMap(a_this, a_editorID);
			}
			return _setFormEditorID(a_this, a_editorID);
		}

		using GetFunc = const char*(RE::TESForm*);
		using SetFunc = bool(RE::TESForm*, const char*);
		static inline REL::Relocation<GetFunc*> _getFormEditorID;
		static inline REL::Relocation<SetFunc*> _setFormEditorID;
	};

	inline void Install()
	{
		Hook<RE::TESObjectWEAP>::Install();
		REX::INFO("Throwing System Overhaul: EditorID patch installed for TESObjectWEAP"sv);
	}

	// GetFormEditorID returns a raw const char*, and only TESObjectWEAP is hooked above: every other
	// form type reaches the engine's own implementation, whose return is not guaranteed non-null.
	// Constructing or formatting a std::string from a null pointer is undefined, so every caller
	// goes through here.
	[[nodiscard]] inline std::string EditorIDOf(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return {};
		}
		const char* const editorID = a_form->GetFormEditorID();
		return editorID ? std::string{ editorID } : std::string{};
	}
}
