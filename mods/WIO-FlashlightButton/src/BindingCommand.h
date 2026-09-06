#pragma once

#include <array>
#include <string>

#include "ApiConsumer.h"
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
namespace FMB::BindingCommand
{
	enum class Command : std::int32_t
	{
		kNoChange = 0,
		kUnbind = 1,
		kRebind = 2
	};

	namespace detail
	{
		inline constexpr const char* kModName = "WIO-FlashlightButton";
		inline constexpr const char* kEvent = "TogglePOV";

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
			{ "Gamepad", "iUnbindTogglePOVGamepad", "sTogglePOVGamepadStatus:Gamepad",
			  WattzIO::ControlUnbinderAPI::Slot::kGamepad, "$FMB_Dev_Gamepad", "Gamepad" },
			{ "Keyboard", "iUnbindTogglePOVKeyboard", "sTogglePOVKeyboardStatus:Keyboard",
			  WattzIO::ControlUnbinderAPI::Slot::kKeyboard, "$FMB_Dev_Keyboard", "Keyboard" },
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

		inline void ResetControl(const Control& a_control)
		{
			// The ini write is authoritative: it is what this plugin reads next pass.
			if (!::WritePrivateProfileStringA(a_control.section, a_control.key, "0",
					Settings::detail::kSettingsPath)) {
				REX::WARN(
					"Flashlight Button: could not reset {} in the MCM ini - the command may repeat"sv,
					a_control.key);
			}

			// And the menu's own copy, which never re-reads that file this session.
			if (const auto vm = VM(); vm) {
				const RE::BSFixedString modName{ kModName };
				const RE::BSFixedString key{ std::string{ a_control.key } + ":" + a_control.section };
				WIO::Papyrus::DispatchStaticCall(vm, "MCM", "SetModSettingInt", nullptr, modName, key, 0);
			}
		}

		// Live engine state. The provider owns the key-code naming.
		[[nodiscard]] inline std::string StatusText(const Control& a_control)
		{
			const auto* api = ApiConsumer::Get();
			if (!api) {
				return WIO::Translations::Localize(
					"$FMB_Status_NeedsUnbinder"sv, "Requires Control Unbinder"sv);
			}

			const auto* described = api->DescribeBinding(kEvent, a_control.slot);
			return described ? std::string{ described } :
							   WIO::Translations::Localize("$FMB_Status_Unknown"sv, "Unknown"sv);
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
			const auto command = static_cast<Command>(
				Settings::detail::GetInt(control.section, control.key, 0));
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

			const auto show = [&](const std::string& a_text) {
				RE::SendHUDMessage::ShowHUDMessage(a_text.c_str(), nullptr, true, true);
			};
			const std::initializer_list<std::pair<std::string_view, std::string_view>> args{
				{ "{EVENT}", eventName }, { "{DEVICE}", device }
			};

			switch (result) {
			case Api::Result::kSuccess:
				show(command == Command::kUnbind ?
						 WIO::Translations::Localize("$FMB_Note_BindUnbound"sv,
							 "Control Unbinder: {EVENT} unbound ({DEVICE})"sv, args) :
						 WIO::Translations::Localize("$FMB_Note_BindRestored"sv,
							 "Control Unbinder: {EVENT} restored ({DEVICE})"sv, args));
				break;
			case Api::Result::kAlreadyInState:
				break;
			case Api::Result::kNotApplicable:
				show(WIO::Translations::Localize("$FMB_Note_BindNoBinding"sv,
					"Control Unbinder: {EVENT} - no {DEVICE} binding"sv, args));
				break;
			default:
				show(command == Command::kUnbind ?
						 WIO::Translations::Localize("$FMB_Note_BindUnbindFailed"sv,
							 "Control Unbinder: {EVENT} unbind failed ({DEVICE})"sv, args) :
						 WIO::Translations::Localize("$FMB_Note_BindRebindFailed"sv,
							 "Control Unbinder: {EVENT} rebind failed ({DEVICE})"sv, args));
				break;
			}

			REX::INFO("Flashlight Button: command {} on {} - {}"sv, action, control.label,
				api->ResultName(result));

			detail::ResetControl(control);
		}

		if (acted) {
			RefreshStatus();
		}
	}
}
