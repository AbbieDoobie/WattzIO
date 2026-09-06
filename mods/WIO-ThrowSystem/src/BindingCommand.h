#pragma once

#include <array>
#include <string>

#include "ApiConsumer.h"
#include "Notify.h"
#include "Settings.h"

// Momentary binding commands, and the live status text under each one.
//
// Dropdowns rather than toggles: a toggle is a standing claim re-stated on every pause-menu
// close, so two mods managing one action overwrite each other. A command applies once and resets
// itself to "No Change".
//
// The reset needs both the ini write (what this plugin reads next pass) and MCM.SetModSettingInt
// (what the menu shows this session), because MCM builds its settings store once per launch and
// never re-reads the ini. Only the ini write is correctness-critical.
namespace TSO::BindingCommand
{
	enum class Command : std::int32_t
	{
		kNoChange = 0,
		kUnbind = 1,
		kRebind = 2
	};

	namespace detail
	{
		inline constexpr const char* kModName = "WIO-ThrowSystem";
		inline constexpr const char* kEvent = "Melee";

		struct Control
		{
			const char*                      section;   // MCM ini section
			const char*                      key;       // dropdown id
			const char*                      statusKey; // text widget id for the status line
			WattzIO::ControlUnbinderAPI::Slot slot;
			const char*                      labelKey;  // device name, as a translation key
			const char*                      label;     // English fallback for labelKey
		};

		inline constexpr std::array<Control, 2> kControls{ {
			{ "Settings", "iUnbindMeleeGamepad", "sMeleeGamepadStatus:Settings",
			  WattzIO::ControlUnbinderAPI::Slot::kGamepad, "$TSO_Dev_Gamepad", "Gamepad" },

			{ "Settings", "iUnbindMeleeKeyboard", "sMeleeKeyboardStatus:Settings",
			  WattzIO::ControlUnbinderAPI::Slot::kKeyboard, "$TSO_Dev_Keyboard", "Keyboard" },
		} };

		// Returned by value, not decayed to a raw pointer, so the reference stays held.
		[[nodiscard]] inline RE::BSTSmartPointer<RE::BSScript::IVirtualMachine> VM()
		{
			const auto game = RE::GameVM::GetSingleton();
			if (!game) {
				return {};
			}
			return game->GetVM();
		}

		// Pushed values round-trip through an ini file, so characters the format treats as
		// structure are neutralised here - a ';' would make the whole row vanish on read-back.
		[[nodiscard]] inline std::string SanitiseForIni(std::string a_text)
		{
			for (auto& c : a_text) {
				if (c == '=' || c == ';' || c == '[' || c == ']' || c == '"' ||
					static_cast<unsigned char>(c) < 0x20) {
					c = ' ';
				}
			}
			return a_text;
		}

		inline void PushString(const char* a_settingKey, const std::string& a_value)
		{
			if (const auto vm = VM(); vm) {
				const RE::BSFixedString modName{ kModName };
				const RE::BSFixedString key{ a_settingKey };
				const RE::BSFixedString value{ SanitiseForIni(a_value) };
				WIO::Papyrus::DispatchStaticCall(vm, "MCM", "SetModSettingString", nullptr, modName, key, value);
			}
		}

		[[nodiscard]] inline REX::TIniSetting<std::int32_t>& SettingFor(const Control& a_control)
		{
			return a_control.slot == WattzIO::ControlUnbinderAPI::Slot::kGamepad ?
			           Settings::iUnbindMeleeGamepad :
			           Settings::iUnbindMeleeKeyboard;
		}

		inline void ResetControl(const Control& a_control)
		{
			// Settings go through REX::TIniSetting rather than raw ini calls, so the write goes
			// through that and Save() is what reaches disk. This is what the plugin reads back
			// next pass, so it must not fail.
			SettingFor(a_control).SetValue(0);
			Settings::Save();

			// MCM's own copy, which never re-reads that file this session.
			if (const auto vm = VM(); vm) {
				const RE::BSFixedString modName{ kModName };
				const RE::BSFixedString key{ std::string{ a_control.key } + ":" + a_control.section };
				WIO::Papyrus::DispatchStaticCall(vm, "MCM", "SetModSettingInt", nullptr, modName, key, 0);
			}
		}

		// Live engine state. The provider owns the key-code naming.
		//
		// Round-trips through the ini, which MCM reads back with the ANSI
		// GetPrivateProfileSection, so a non-codepage translation shows as mojibake there.
		[[nodiscard]] inline std::string StatusText(const Control& a_control)
		{
			const auto* api = ApiConsumer::Get();
			if (!api) {
				return WIO::Translations::Localize("$TSO_Status_NeedsUnbinder"sv, "Requires Control Unbinder"sv);
			}

			const auto* described = api->DescribeBinding(kEvent, a_control.slot);
			return described ? std::string{ described } :
							   WIO::Translations::Localize("$TSO_Status_Unknown"sv, "Unknown"sv);
		}
	}

	// Refreshes every status line from the engine, at kGameLoaded and after any command.
	inline void RefreshStatus()
	{
		for (const auto& control : detail::kControls) {
			detail::PushString(control.statusKey, detail::StatusText(control));
		}
	}

	// Called on every PauseMenu close. Acts only on a control the player actually moved.
	inline void Run()
	{
		namespace Api = WattzIO::ControlUnbinderAPI;

		bool acted = false;

		for (const auto& control : detail::kControls) {
			const auto command = static_cast<Command>(detail::SettingFor(control).GetValue());
			if (command == Command::kNoChange) {
				continue;
			}

			acted = true;

			const auto* api = ApiConsumer::Get();
			if (!api) {
				ApiConsumer::WarnUnavailable();
				detail::ResetControl(control);
				continue;
			}

			const auto result = api->Apply(detail::kEvent, control.slot,
				command == Command::kUnbind ? Api::Action::kUnbind : Api::Action::kRestore,
				detail::kModName);

			// Reports the binding change only; the control resetting itself is bookkeeping.
			//
			// One key per outcome, whole sentences - words translated in isolation do not fit
			// other languages' grammar. {EVENT} is Control Unbinder's own string.
			const auto*       name = api->DisplayName(detail::kEvent);
			const std::string eventName{ name ? name : detail::kEvent };
			const std::string device = WIO::Translations::Localize(control.labelKey, control.label);
			const std::string action = command == Command::kUnbind ? "unbound" : "restored";

			switch (result) {
			case Api::Result::kSuccess:
				Notify::Show(Notify::Format(
					command == Command::kUnbind ?
						WIO::Translations::Localize("$TSO_Note_BindUnbound"sv,
							"Control Unbinder: {EVENT} unbound ({DEVICE})"sv) :
						WIO::Translations::Localize("$TSO_Note_BindRestored"sv,
							"Control Unbinder: {EVENT} restored ({DEVICE})"sv),
					{ { "{EVENT}", eventName }, { "{DEVICE}", device } }));
				break;
			case Api::Result::kAlreadyInState:
				break;
			case Api::Result::kNotApplicable:
				Notify::Show(Notify::Format(
					WIO::Translations::Localize("$TSO_Note_BindNoBinding"sv,
						"Control Unbinder: {EVENT} - no {DEVICE} binding"sv),
					{ { "{EVENT}", eventName }, { "{DEVICE}", device } }));
				break;
			default:
				Notify::Show(Notify::Format(
					command == Command::kUnbind ?
						WIO::Translations::Localize("$TSO_Note_BindUnbindFailed"sv,
							"Control Unbinder: {EVENT} unbind failed ({DEVICE})"sv) :
						WIO::Translations::Localize("$TSO_Note_BindRebindFailed"sv,
							"Control Unbinder: {EVENT} rebind failed ({DEVICE})"sv),
					{ { "{EVENT}", eventName }, { "{DEVICE}", device } }));
				break;
			}

			REX::INFO("Throwing System Overhaul: command {} on {} - {}"sv, action, control.label,
				api->ResultName(result));

			detail::ResetControl(control);
		}

		if (acted) {
			RefreshStatus();
		}
	}
}
