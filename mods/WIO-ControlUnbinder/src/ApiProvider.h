#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include "ControlMapService.h"
#include "ControlUnbinderAPI.h"
#include "KeyNames.h"

// Provider half of the shared binding API. Exposes ControlMapService to consuming mods, so this
// is the only mod that writes ControlMap and no two mods can contradict each other on the same
// action.
namespace UnbindAny::ApiProvider
{
	namespace detail
	{
		using Api = WattzIO::ControlUnbinderAPI::API;
		using ApiSlot = WattzIO::ControlUnbinderAPI::Slot;
		using ApiAction = WattzIO::ControlUnbinderAPI::Action;
		using ApiResult = WattzIO::ControlUnbinderAPI::Result;
		using ApiState = WattzIO::ControlUnbinderAPI::State;

		// The API enums are declared separately from the service's own so the wire contract
		// can never drift silently when the internal ones are reordered. These asserts make
		// that a build error instead of a runtime mis-dispatch.
		static_assert(static_cast<std::uint32_t>(ApiSlot::kKeyboard) ==
					  static_cast<std::uint32_t>(ControlMapService::Slot::kKeyboard));
		static_assert(static_cast<std::uint32_t>(ApiSlot::kGamepad) ==
					  static_cast<std::uint32_t>(ControlMapService::Slot::kGamepad));
		static_assert(static_cast<std::uint32_t>(ApiAction::kUnbind) ==
					  static_cast<std::uint32_t>(ControlMapService::Action::kUnbind));
		static_assert(static_cast<std::uint32_t>(ApiAction::kRestore) ==
					  static_cast<std::uint32_t>(ControlMapService::Action::kRestore));
		static_assert(static_cast<std::uint32_t>(ApiResult::kSuccess) ==
					  static_cast<std::uint32_t>(ControlMapService::Result::kSuccess));
		static_assert(static_cast<std::uint32_t>(ApiResult::kNoControlMap) ==
					  static_cast<std::uint32_t>(ControlMapService::Result::kNoControlMap));

		inline ApiResult Apply(const char* a_eventID, ApiSlot a_slot, ApiAction a_action,
			const char* a_caller)
		{
			if (!a_eventID || !a_caller) {
				return ApiResult::kUnknownAction;
			}
			// No arbitration. Every caller issues a momentary command rather than a standing
			// claim, so nothing re-asserts and nothing can contradict anything: the engine's own
			// ControlMap is the only state.
			const auto result = ControlMapService::Apply(a_eventID,
				static_cast<ControlMapService::Slot>(a_slot),
				static_cast<ControlMapService::Action>(a_action));

			// Every call from another mod is logged, so a binding change that goes wrong can be
			// traced back to the mod that asked for it.
			if (result == ControlMapService::Result::kSuccess) {
				REX::INFO("Control Unbinder: API - {} {} on {} (requested by {})"sv,
					a_action == ApiAction::kUnbind ? "unbound"sv : "restored"sv, a_eventID,
					a_slot == ApiSlot::kGamepad ? "Gamepad"sv : "Keyboard"sv, a_caller);
			} else if (result != ControlMapService::Result::kAlreadyInState) {
				REX::WARN("Control Unbinder: API - {} {} on {} - {}"sv,
					a_action == ApiAction::kUnbind ? "unbind"sv : "restore"sv, a_eventID,
					a_slot == ApiSlot::kGamepad ? "Gamepad"sv : "Keyboard"sv,
					ControlMapService::ResultName(result));
			}
			return static_cast<ApiResult>(result);
		}

		inline ApiState Query(const char* a_eventID, ApiSlot a_slot)
		{
			if (!a_eventID) {
				return {};
			}
			const auto state = ControlMapService::Query(a_eventID,
				static_cast<ControlMapService::Slot>(a_slot));
			return { state.applicable, state.bound, state.keyboard, state.mouse, state.gamepad };
		}

		// The returned pointer is valid until the next call for the same event; callers copy it.
		inline const char* DisplayName(const char* a_eventID)
		{
			if (!a_eventID) {
				return nullptr;
			}
			const auto binding = Bindings::Find(a_eventID);
			if (!binding) {
				return nullptr;
			}

			// The same label the MCM page shows. The key is spelled out in Bindings.h rather than
			// assembled, so a translation checker can find it.
			std::string text = WIO::Translations::Localize(binding->labelKey, binding->displayName);

			static std::map<std::string, std::string> cache;
			auto& slot = cache[std::string{ a_eventID }];
			slot = std::move(text);
			return slot.c_str();
		}

		inline const char* ResultName(ApiResult a_result)
		{
			return ControlMapService::ResultName(
				static_cast<ControlMapService::Result>(a_result))
			    .data();
		}

		// The returned pointer is valid until the next call for the same (event, slot); callers
		// copy it.
		inline const char* DescribeBinding(const char* a_eventID, ApiSlot a_slot)
		{
			if (!a_eventID) {
				return nullptr;
			}
			const auto binding = Bindings::Find(a_eventID);
			if (!binding) {
				return nullptr;
			}

			const auto slot = static_cast<ControlMapService::Slot>(a_slot);
			const auto state = ControlMapService::Query(a_eventID, slot);

			std::string text;
			if (!state.applicable) {
				text = WIO::Translations::Localize(
					"$UC_State_NotApplicable"sv, "Not bound on this device by default"sv);
			} else if (!state.bound) {
				text = WIO::Translations::Localize("$UC_State_Unbound"sv, "Unbound"sv);
			} else {
				// Named from the live value, so a control the player remapped reads the same as
				// one still on its default. Empty only for a code KeyNames does not cover.
				const auto key = slot == ControlMapService::Slot::kGamepad ?
				                     KeyNames::Gamepad(state.gamepad) :
				                     KeyNames::KeyboardAndMouse(state.keyboard, state.mouse);

				text = key.empty() ?
				           WIO::Translations::Localize("$UC_State_Bound"sv, "Bound"sv) :
				           WIO::Translations::Localize("$UC_State_BoundTo"sv, "Bound ({KEY})"sv,
							   { { "{KEY}", key } });
			}

			static std::map<std::pair<std::string, ApiSlot>, std::string> cache;
			auto& slotText = cache[{ std::string{ a_eventID }, a_slot }];
			slotText = std::move(text);
			return slotText.c_str();
		}

		[[nodiscard]] inline bool IsInMemory(const char* a_eventID)
		{
			const auto binding = a_eventID ? Bindings::Find(a_eventID) : nullptr;
			return binding && binding->inMemory;
		}

		// Standing requests for in-memory bindings, per caller. Nothing else in the game
		// persists these entries: the engine drops them every launch, and the provider is the
		// only party that sees every request.
		inline std::map<std::pair<std::string, ApiSlot>, std::map<std::string, bool>> g_persistent;

		// The resolved wish across every caller. Any caller asking for the binding to be gone
		// keeps it gone; it returns once every caller has withdrawn. Read by ControlRemap::Apply,
		// which re-asserts in-memory entries on every pass.
		[[nodiscard]] inline bool AnyCallerWantsUnbind(std::string_view a_eventID, ApiSlot a_slot)
		{
			const auto it = g_persistent.find({ std::string{ a_eventID }, a_slot });
			return it != g_persistent.end() &&
			       std::any_of(it->second.begin(), it->second.end(),
					   [](const auto& e) { return e.second; });
		}

		// Applies the resolved wish immediately, so a caller sees the effect of its own request
		// without waiting for the next pass.
		inline ControlMapService::Result ReapplyOne(const char* a_eventID, ApiSlot a_slot)
		{
			return ControlMapService::Apply(a_eventID,
				static_cast<ControlMapService::Slot>(a_slot),
				AnyCallerWantsUnbind(a_eventID, a_slot) ? ControlMapService::Action::kUnbind :
														  ControlMapService::Action::kRestore);
		}

		inline ApiResult SetPersistentUnbind(const char* a_eventID, ApiSlot a_slot, bool a_unbind,
			const char* a_caller)
		{
			if (!a_eventID || !a_caller) {
				return ApiResult::kUnknownAction;
			}
			auto& callers = g_persistent[{ std::string{ a_eventID }, a_slot }];
			callers[std::string{ a_caller }] = a_unbind;

			const auto result = ReapplyOne(a_eventID, a_slot);
			REX::INFO("Control Unbinder: API - standing {} for {} on {} (requested by {}) - {}"sv,
				a_unbind ? "unbind"sv : "release"sv, a_eventID,
				a_slot == ApiSlot::kGamepad ? "Gamepad"sv : "Keyboard"sv, a_caller,
				ControlMapService::ResultName(result));
			return static_cast<ApiResult>(result);
		}

		inline constexpr Api kApi{
			WattzIO::ControlUnbinderAPI::kVersion,
			&Apply,
			&Query,
			&DisplayName,
			&ResultName,
			&DescribeBinding,
			&IsInMemory,
			&SetPersistentUnbind
		};
	}

	// Published as a plain exported function rather than through F4SE messaging: it is
	// synchronous, gives the consumer a real return value, and a null GetModuleHandle on the
	// consumer side is the "not installed" signal.
	inline void LogOffered()
	{
		REX::INFO("Control Unbinder: binding API v{} exported as {}"sv,
			WattzIO::ControlUnbinderAPI::kVersion,
			WattzIO::ControlUnbinderAPI::kExportName);
	}
}

extern "C" __declspec(dllexport) const WattzIO::ControlUnbinderAPI::API* WIO_GetControlUnbinderAPI()
{
	return &UnbindAny::ApiProvider::detail::kApi;
}
