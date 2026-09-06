#pragma once

#include <string_view>
#include <utility>

#include "Bindings.h"

// The one place that writes ControlMap.
//
// 1. RemapButton returns a meaningful bool, and when an entry is not remappable it refuses and
//    skips the write, so a false return means nothing changed.
//
// 2. A raw inputKey write has to be followed by a kick on the same device array.
//    deviceMappings[device] is sorted by inputKey and live input dispatch binary-searches it,
//    while GetMappedKey and the Controls menu scan linearly by eventID. A raw write updates the
//    value without restoring the order, so the key shows in the menu and does nothing in game
//    until the next launch rebuilds the array. Any RemapButton call on that array re-sorts it.
//
// 3. Keyboard and Mouse are one logical slot. RemapButton on either clears the same event on the
//    other, so no order of RemapButton calls leaves both bound. Restoring an action with both
//    defaults takes the keyboard RemapButton first, then a raw mouse write, then a mouse-side
//    kick. TogglePOV is the only vanilla gameplay action affected.
//
// 4. Applicability is two questions: does the action have a default on this device, and is the
//    live mapping remappable.
namespace UnbindAny::ControlMapService
{
	using Context = RE::UserEvents::INPUT_CONTEXT_ID;

	// The two devices a player sees. Mouse is deliberately not a separate slot - the engine
	// treats keyboard and mouse as one binding per action, and so does the MCM UI.
	enum class Slot
	{
		kKeyboard,
		kGamepad
	};

	enum class Action
	{
		kUnbind,
		kRestore
	};

	enum class Result
	{
		kSuccess,         // verified: the engine now reads what was asked for
		kAlreadyInState,  // nothing to do; not an error
		kNotApplicable,   // no vanilla default on this slot - the request is meaningless
		kRefused,         // the entry exists but is not remappable and no raw path applied
		kVerifyFailed,    // written, but the read-back disagrees
		kUnknownAction,   // not in the generated table
		kNoControlMap
	};

	[[nodiscard]] inline std::string_view ResultName(Result a_result)
	{
		switch (a_result) {
		case Result::kSuccess:
			return "success"sv;
		case Result::kAlreadyInState:
			return "already-in-state"sv;
		case Result::kNotApplicable:
			return "not-applicable"sv;
		case Result::kRefused:
			return "refused"sv;
		case Result::kVerifyFailed:
			return "verify-failed"sv;
		case Result::kUnknownAction:
			return "unknown-action"sv;
		default:
			return "no-controlmap"sv;
		}
	}

	namespace detail
	{
		[[nodiscard]] inline RE::ControlMap::UserEventMapping* FindMapping(
			RE::ControlMap& a_cm, Context a_ctx, RE::INPUT_DEVICE a_device, std::string_view a_eventID)
		{
			const auto context = a_cm.controlMaps[std::to_underlying(a_ctx)];
			if (!context) {
				return nullptr;
			}
			auto&                   mappings = context->deviceMappings[std::to_underlying(a_device)];
			const RE::BSFixedString target(a_eventID);
			for (auto& mapping : mappings) {
				if (mapping.eventID == target) {
					return &mapping;
				}
			}
			return nullptr;
		}

		// Rule 2. Re-sorts one device array by writing some entry's current value straight back.
		// Any remappable entry in the same array will do, because RemapButton re-sorts the whole
		// array. Chosen at run time rather than hardcoded, skipping any entry whose own
		// sibling-clear would destroy a real binding.
		inline bool Kick(RE::ControlMap& a_cm, Context a_ctx, RE::INPUT_DEVICE a_device,
			std::string_view a_avoidEventID)
		{
			const auto context = a_cm.controlMaps[std::to_underlying(a_ctx)];
			if (!context) {
				return false;
			}
			const RE::BSFixedString avoid(a_avoidEventID);
			for (auto& mapping : context->deviceMappings[std::to_underlying(a_device)]) {
				if (!mapping.remappable || mapping.eventID == avoid) {
					continue;
				}
				// A KBM RemapButton also clears the same event on the sibling device (rule 3),
				// so only kick with an entry that has nothing on the other side to lose.
				if (a_device == RE::INPUT_DEVICE::kKeyboard || a_device == RE::INPUT_DEVICE::kMouse) {
					const auto sibling = a_device == RE::INPUT_DEVICE::kKeyboard ?
					                         RE::INPUT_DEVICE::kMouse :
					                         RE::INPUT_DEVICE::kKeyboard;
					const auto* other = FindMapping(a_cm, a_ctx, sibling, mapping.eventID);
					if (other && other->inputKey != Bindings::kUnbound) {
						continue;
					}
				}
				const auto current = RE::GetMappedKey(&a_cm, mapping.eventID, a_device, a_ctx);
				a_cm.RemapButton(mapping.eventID, a_device, static_cast<std::int32_t>(current));
				return true;
			}
			return false;
		}

		// Writes one device. Uses RemapButton when the entry allows it, and falls back to a raw
		// write plus a kick when it does not (rule 1 + rule 2). a_forceRaw exists for the one
		// case rule 3 requires it: the mouse half of a both-bound restore.
		inline bool Write(RE::ControlMap& a_cm, Context a_ctx, RE::INPUT_DEVICE a_device,
			std::string_view a_eventID, std::int32_t a_value, bool a_forceRaw)
		{
			auto* mapping = FindMapping(a_cm, a_ctx, a_device, a_eventID);
			if (!mapping) {
				return false;
			}

			if (!a_forceRaw && mapping->remappable) {
				return a_cm.RemapButton(a_eventID, a_device, a_value);
			}

			mapping->inputKey = a_value;
			Kick(a_cm, a_ctx, a_device, a_eventID);
			return true;
		}

		[[nodiscard]] inline std::int32_t Read(RE::ControlMap& a_cm, Context a_ctx,
			RE::INPUT_DEVICE a_device, std::string_view a_eventID)
		{
			return static_cast<std::int32_t>(RE::GetMappedKey(&a_cm, a_eventID, a_device, a_ctx));
		}
	}

	// What the engine currently holds. Drives the MCM status sub-line, and is the only accurate
	// source for it: a mod's own settings say what was asked for, not what is true.
	struct State
	{
		bool         applicable{ false };  // this slot has a vanilla default for this action
		bool         bound{ false };
		std::int32_t keyboard{ Bindings::kUnbound };
		std::int32_t mouse{ Bindings::kUnbound };
		std::int32_t gamepad{ Bindings::kUnbound };
	};

	[[nodiscard]] inline State Query(std::string_view a_eventID, Slot a_slot,
		Context a_ctx = Context::kMainGameplay)
	{
		State      state;
		const auto binding = Bindings::Find(a_eventID);
		const auto cm = RE::ControlMap::GetSingleton();
		if (!binding || !cm) {
			return state;
		}

		state.keyboard = detail::Read(*cm, a_ctx, RE::INPUT_DEVICE::kKeyboard, a_eventID);
		state.mouse = detail::Read(*cm, a_ctx, RE::INPUT_DEVICE::kMouse, a_eventID);
		state.gamepad = detail::Read(*cm, a_ctx, RE::INPUT_DEVICE::kGamepad, a_eventID);

		if (a_slot == Slot::kKeyboard) {
			state.applicable = binding->keyboardDefault != Bindings::kUnbound ||
			                   binding->mouseDefault != Bindings::kUnbound;
			state.bound = state.keyboard != Bindings::kUnbound || state.mouse != Bindings::kUnbound;
		} else {
			state.applicable = binding->gamepadDefault != Bindings::kUnbound;
			state.bound = state.gamepad != Bindings::kUnbound;
		}
		return state;
	}

	// Raw write with no kick and no save, for callers that batch several entries: all the writes,
	// then one kick, then one save. A kick and save after each individual write does not work.
	[[nodiscard]] inline bool WriteRawNoKick(std::string_view a_eventID, Slot a_slot,
		std::int32_t a_value, Context a_ctx = Context::kMainGameplay)
	{
		const auto cm = RE::ControlMap::GetSingleton();
		if (!cm) {
			return false;
		}
		const auto device = a_slot == Slot::kGamepad ? RE::INPUT_DEVICE::kGamepad :
		                                               RE::INPUT_DEVICE::kKeyboard;
		auto* mapping = detail::FindMapping(*cm, a_ctx, device, a_eventID);
		if (!mapping) {
			return false;
		}
		mapping->inputKey = a_value;
		return true;
	}

	// The tail of that batch: one kick to repair the sort, then one save. The save is what makes a
	// remappable=0 entry go live in the engine's own input-dispatch cache. Saving only on the pass
	// a value changed is unreliable.
	inline void KickAndSave(Slot a_slot, Context a_ctx = Context::kMainGameplay)
	{
		const auto cm = RE::ControlMap::GetSingleton();
		if (!cm) {
			return;
		}
		const auto device = a_slot == Slot::kGamepad ? RE::INPUT_DEVICE::kGamepad :
		                                               RE::INPUT_DEVICE::kKeyboard;
		detail::Kick(*cm, a_ctx, device, ""sv);
		cm->SaveRemappings();
	}

	// Writes a specific value, bypassing the already-in-state check. Apply() is the right call for
	// anything driven by a setting; this exists for the Dialogue-menu D-pad workaround, which
	// forces vanilla Quick Slot binds on every pass regardless of what the engine reads.
	inline Result Force(std::string_view a_eventID, Slot a_slot, std::int32_t a_value,
		Context a_ctx = Context::kMainGameplay)
	{
		const auto cm = RE::ControlMap::GetSingleton();
		if (!cm) {
			return Result::kNoControlMap;
		}
		if (!Bindings::Find(a_eventID)) {
			return Result::kUnknownAction;
		}

		const auto device = a_slot == Slot::kGamepad ? RE::INPUT_DEVICE::kGamepad :
		                                               RE::INPUT_DEVICE::kKeyboard;
		if (!detail::Write(*cm, a_ctx, device, a_eventID, a_value, false)) {
			return Result::kRefused;
		}
		cm->SaveRemappings();
		return detail::Read(*cm, a_ctx, device, a_eventID) == a_value ? Result::kSuccess :
		                                                                Result::kVerifyFailed;
	}

	// The single entry point. Idempotent by construction: it consults the engine, never a cached
	// applied-flag, so an external reset (a save load, the vanilla Controls menu, another mod)
	// cannot leave it believing work is done that has since been undone.
	inline Result Apply(std::string_view a_eventID, Slot a_slot, Action a_action,
		Context a_ctx = Context::kMainGameplay)
	{
		const auto binding = Bindings::Find(a_eventID);
		if (!binding) {
			return Result::kUnknownAction;
		}
		const auto cm = RE::ControlMap::GetSingleton();
		if (!cm) {
			return Result::kNoControlMap;
		}

		const bool wantUnbind = a_action == Action::kUnbind;
		const auto before = Query(a_eventID, a_slot, a_ctx);
		if (!before.applicable) {
			return Result::kNotApplicable;
		}
		if (before.bound != wantUnbind) {
			return Result::kAlreadyInState;
		}

		bool wrote = false;

		if (a_slot == Slot::kGamepad) {
			const auto value = wantUnbind ? Bindings::kUnbound : binding->gamepadDefault;
			wrote = detail::Write(*cm, a_ctx, RE::INPUT_DEVICE::kGamepad, a_eventID, value, false);
		} else {
			const bool hasKeyboard = binding->keyboardDefault != Bindings::kUnbound;
			const bool hasMouse = binding->mouseDefault != Bindings::kUnbound;

			if (wantUnbind) {
				// Either call clears both sides via rule 3. Each applicable device is driven
				// explicitly anyway, so this does not depend on the sibling behaviour holding
				// for every entry.
				if (hasKeyboard) {
					wrote |= detail::Write(*cm, a_ctx, RE::INPUT_DEVICE::kKeyboard, a_eventID,
						Bindings::kUnbound, false);
				}
				if (hasMouse) {
					wrote |= detail::Write(*cm, a_ctx, RE::INPUT_DEVICE::kMouse, a_eventID,
						Bindings::kUnbound, false);
				}
			} else if (hasKeyboard && hasMouse) {
				// Rule 3. Keyboard through RemapButton first, because its sibling-clear would wipe a
				// mouse value written before it. Then the mouse raw, because only a raw write escapes
				// that clear. Then a mouse-side kick to repair the sort.
				wrote = detail::Write(*cm, a_ctx, RE::INPUT_DEVICE::kKeyboard, a_eventID,
					binding->keyboardDefault, false);
				wrote |= detail::Write(*cm, a_ctx, RE::INPUT_DEVICE::kMouse, a_eventID,
					binding->mouseDefault, true);
			} else if (hasKeyboard) {
				wrote = detail::Write(*cm, a_ctx, RE::INPUT_DEVICE::kKeyboard, a_eventID,
					binding->keyboardDefault, false);
			} else {
				wrote = detail::Write(*cm, a_ctx, RE::INPUT_DEVICE::kMouse, a_eventID,
					binding->mouseDefault, false);
			}
		}

		if (!wrote) {
			return Result::kRefused;
		}

		cm->SaveRemappings();

		// Read-back proves the value, not the routing - the sort invariant is what makes those
		// differ, and Write() is responsible for keeping it intact (rule 2).
		const auto after = Query(a_eventID, a_slot, a_ctx);
		return after.bound == !wantUnbind ? Result::kSuccess : Result::kVerifyFailed;
	}
}
