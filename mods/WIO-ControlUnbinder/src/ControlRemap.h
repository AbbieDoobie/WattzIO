#pragma once

#include <map>
#include <string>
#include <string_view>

#include "ApiProvider.h"
#include "Bindings.h"
#include "ControlMapService.h"
#include "Settings.h"

// Behaviour; the generated Bindings.h it reads is the data.
//
// Two storage models.
//
// Momentary commands, for anything the engine persists. A normal binding goes into
// ControlMap_Custom.txt, so its control is a one-shot command (No Change / Unbind / Rebind)
// applied once and reset. A toggle would be a standing claim re-stated on every pause close,
// which is what lets two mods managing one action overwrite each other.
//
// Stored state, for in-memory bindings. The D-pad Quick Slot entries are remappable=0 and persist
// nowhere, so this mod is the source of truth: their setting is a stored Bound/Unbound state,
// defaulting to Bound and re-applied every pass. The MCM control keeps the same dropdown shape,
// without the No Change entry. Another mod's standing SetPersistentUnbind request is ORed into
// that state on the same pass, because it has nowhere else to live either.
//
// The command reset needs BOTH the ini write and MCM.SetModSettingInt, because MCM builds its
// settings store once per launch. Only the ini write is correctness-critical.
namespace UnbindAny::ControlRemap
{
	enum class Command : std::int32_t
	{
		kNoChange = 0,
		kUnbind = 1,
		kRebind = 2
	};

	namespace detail
	{
		inline constexpr const char* kModName = "WIO-ControlUnbinder";

		[[nodiscard]] inline bool Applicable(const Bindings::Binding& a_binding,
			ControlMapService::Slot a_slot)
		{
			return a_slot == ControlMapService::Slot::kGamepad ?
			           a_binding.gamepadDefault != Bindings::kUnbound :
			           (a_binding.keyboardDefault != Bindings::kUnbound ||
						   a_binding.mouseDefault != Bindings::kUnbound);
		}

		[[nodiscard]] inline auto VM()
		{
			const auto game = RE::GameVM::GetSingleton();
			if (!game) {
				return RE::BSTSmartPointer<RE::BSScript::IVirtualMachine>{};
			}
			return game->GetVM();
		}

		// One whole-sentence key per outcome, matching what the consumer mods show for a
		// request of their own. {EVENT} is the same label the MCM row carries.
		inline void Notify(const Bindings::Binding& a_binding, std::string_view a_section,
			Command a_command, bool a_succeeded)
		{
			const bool unbind = a_command == Command::kUnbind;
			const bool gamepad = a_section == "Gamepad"sv;

			const auto event = WIO::Translations::Localize(a_binding.labelKey, a_binding.displayName);
			const auto device = gamepad ?
			                        WIO::Translations::Localize("$UC_Dev_Gamepad"sv, "Gamepad"sv) :
			                        WIO::Translations::Localize("$UC_Dev_Keyboard"sv, "Keyboard"sv);

			const std::initializer_list<std::pair<std::string_view, std::string_view>> args{
				{ "{EVENT}", event }, { "{DEVICE}", device }
			};

			std::string text;
			if (a_succeeded) {
				text = unbind ?
				           WIO::Translations::Localize("$UC_Note_Unbound"sv,
							   "Control Unbinder: {EVENT} unbound ({DEVICE})"sv, args) :
				           WIO::Translations::Localize("$UC_Note_Restored"sv,
							   "Control Unbinder: {EVENT} restored ({DEVICE})"sv, args);
			} else {
				text = unbind ?
				           WIO::Translations::Localize("$UC_Note_UnbindFailed"sv,
							   "Control Unbinder: {EVENT} unbind failed ({DEVICE})"sv, args) :
				           WIO::Translations::Localize("$UC_Note_RebindFailed"sv,
							   "Control Unbinder: {EVENT} rebind failed ({DEVICE})"sv, args);
			}
			RE::SendHUDMessage::ShowHUDMessage(text.c_str(), nullptr, true, true);
		}

		inline void PushString(const std::string& a_id, const std::string& a_value)
		{
			if (const auto vm = VM(); vm) {
				WIO::Papyrus::DispatchStaticCall(vm, "MCM", "SetModSettingString", nullptr,
					RE::BSFixedString{ kModName }, RE::BSFixedString{ a_id },
					RE::BSFixedString{ a_value });
			}
		}

		// In-memory entries live under bUnbind_<stem> as a stored state (0 bound, 1 unbound)
		// rather than iUnbind_<stem> as a command, because nothing else remembers them.
		//
		// Read through GetDesired rather than GetPrivateProfileIntA, so the value is parsed the
		// same way every other bool in this family is: 1 or true, case-insensitively.
		[[nodiscard]] inline bool StoredUnbind(const char* a_section, const std::string& a_stem)
		{
			return Settings::GetDesired(a_section, ("bUnbind_" + a_stem).c_str());
		}

		// Last text pushed for each status row, keyed by MCM setting id. Compared against on the
		// next refresh so an unchanged row costs no Papyrus call.
		inline std::map<std::string, std::string> g_lastStatus;

		inline void Reset(const char* a_section, const std::string& a_stem)
		{
			const std::string key = "iUnbind_" + a_stem;
			if (!::WritePrivateProfileStringA(a_section, key.c_str(), "0",
					Settings::kMCMSettingsPath)) {
				REX::WARN("Control Unbinder: could not reset {} - the command may repeat"sv, key);
			}
			if (const auto vm = VM(); vm) {
				WIO::Papyrus::DispatchStaticCall(vm, "MCM", "SetModSettingInt", nullptr,
					RE::BSFixedString{ kModName },
					RE::BSFixedString{ key + ":" + a_section }, 0);
			}
		}
	}

	// Reads live engine state for every control. Called when the pause menu OPENS as well as
	// after a command: MCM's store may not exist yet at kGameLoaded, which leaves the rows blank
	// until something else happens to push them.
	//
	// There are 43 applicable rows, and every push is a Papyrus DispatchStaticCall, so a row whose
	// text has not moved since the last push is skipped. a_record is false for a push that may not
	// land at all - see the kGameLoaded call site - which pushes every row and forgets it did, so
	// the next refresh sends them all again.
	inline void RefreshStatus(bool a_record = true)
	{
		if (!a_record) {
			detail::g_lastStatus.clear();
		}

		std::size_t pushed = 0;
		std::size_t total = 0;

		for (const auto& binding : Bindings::kAll) {
			const std::string stem{ Bindings::SettingStem(binding.eventID) };
			for (const auto& [slot, section] : { std::pair{ ControlMapService::Slot::kKeyboard, "Keyboard" },
					 std::pair{ ControlMapService::Slot::kGamepad, "Gamepad" } }) {
				if (!detail::Applicable(binding, slot)) {
					continue;
				}
				++total;

				const auto* described = ApiProvider::detail::DescribeBinding(binding.eventID.data(),
					static_cast<WattzIO::ControlUnbinderAPI::Slot>(slot));
				const std::string key = "sStatus_" + stem + ":" + section;
				std::string       text = described ?
				                             std::string{ described } :
				                             WIO::Translations::Localize("$UC_Status_Unknown"sv, "Unknown"sv);

				if (a_record) {
					const auto it = detail::g_lastStatus.find(key);
					if (it != detail::g_lastStatus.end() && it->second == text) {
						continue;
					}
					detail::g_lastStatus[key] = text;
				}

				detail::PushString(key, text);
				++pushed;
			}
		}

		REX::DEBUG("Control Unbinder: status rows pushed {} of {}"sv, pushed, total);
	}

	// Called on every PauseMenu close. Acts only on a control the player actually moved.
	inline void Apply(std::string_view a_cause = "unknown"sv)
	{
		REX::DEBUG("Control Unbinder: Apply() from {}"sv, a_cause);

		// In-memory entries first, as one batch. remappable=0, so nothing persists them and this
		// is the only thing that puts them back. Written unconditionally: the value going live
		// depends on SaveRemappings running, which is unreliable when done only on the pass a
		// value changed.
		//
		// Order matters: all the raw writes, then one kick, then one save.
		bool wroteInMemory = false;
		for (const auto& binding : Bindings::kAll) {
			if (!binding.inMemory || binding.gamepadDefault == Bindings::kUnbound) {
				continue;
			}
			const std::string stem{ Bindings::SettingStem(binding.eventID) };
			// This mod's own setting, plus any standing request another mod has made through
			// SetPersistentUnbind. Either asking for the binding to be gone keeps it gone.
			const bool        settingUnbind = detail::StoredUnbind("Gamepad", stem);
			const bool        apiUnbind = ApiProvider::detail::AnyCallerWantsUnbind(
                       binding.eventID, WattzIO::ControlUnbinderAPI::Slot::kGamepad);
			const bool        unbind = settingUnbind || apiUnbind;
			const auto        value = unbind ? Bindings::kUnbound : binding.gamepadDefault;

			const bool wrote = ControlMapService::WriteRawNoKick(binding.eventID,
				ControlMapService::Slot::kGamepad, value);
			wroteInMemory |= wrote;

			REX::DEBUG("Control Unbinder: [in-memory] {} setting={} api={} wrote={} value={:#x}"sv,
				binding.eventID, settingUnbind ? "Unbound"sv : "Bound"sv, apiUnbind, wrote, value);
		}
		if (wroteInMemory) {
			ControlMapService::KickAndSave(ControlMapService::Slot::kGamepad);
		}

		bool acted = false;

		for (const auto& binding : Bindings::kAll) {
			const std::string stem{ Bindings::SettingStem(binding.eventID) };
			for (const auto& [slot, section] : { std::pair{ ControlMapService::Slot::kKeyboard, "Keyboard" },
					 std::pair{ ControlMapService::Slot::kGamepad, "Gamepad" } }) {
				if (!detail::Applicable(binding, slot)) {
					continue;
				}

				// In-memory entries are handled in one batch above, not here.
				if (binding.inMemory) {
					continue;
				}

				const std::string key = "iUnbind_" + stem;
				const auto        command = static_cast<Command>(::GetPrivateProfileIntA(
					       section, key.c_str(), 0, Settings::kMCMSettingsPath));
				if (command == Command::kNoChange) {
					continue;
				}
				acted = true;

				const auto result = ControlMapService::Apply(binding.eventID, slot,
					command == Command::kUnbind ? ControlMapService::Action::kUnbind :
												  ControlMapService::Action::kRestore);

				// Reached only when the player moved one of this mod's own dropdowns. A request
				// that arrived through the binding API goes straight to ControlMapService and
				// never enters this loop, so the calling mod's own notification is the only one
				// shown for it.
				if (result == ControlMapService::Result::kSuccess) {
					REX::INFO("Control Unbinder: {} {} on {}"sv,
						command == Command::kUnbind ? "unbound"sv : "restored"sv,
						binding.eventID, section);
					detail::Notify(binding, section, command, true);
				} else if (result != ControlMapService::Result::kAlreadyInState &&
						   result != ControlMapService::Result::kNotApplicable) {
					REX::WARN("Control Unbinder: {} {} on {} - {}"sv,
						command == Command::kUnbind ? "unbind"sv : "rebind"sv, binding.eventID,
						section, ControlMapService::ResultName(result));
					detail::Notify(binding, section, command, false);
				}

				detail::Reset(section, stem);
			}
		}

		// wroteInMemory counts too: an in-memory entry changes what the engine holds without going
		// through a command, so its status row would otherwise be left stale.
		if (acted || wroteInMemory) {
			RefreshStatus();
		}
	}
}
