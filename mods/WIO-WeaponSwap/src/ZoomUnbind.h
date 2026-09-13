#pragma once

// === F4RD RELOCATIONS ========================================================
// This file resolves no addresses, but its Papyrus dispatch reaches a per-runtime
// ABI offset through the shared compat layer: WIO::Papyrus hands the game a
// BSTThreadScrapFunction whose impl pointer it reads at 0x18 on OG and 0x38 on
// NG/AE. That banner is in lib/commonlibf4rd/compat/WattzIO/Papyrus.h.
// =============================================================================

#include <array>
#include <string>

#include "ApiConsumer.h"
#include "Settings.h"

// Unbind Vanilla Zoom In/Out (Keyboard), and its status line. A momentary No Change / Unbind /
// Rebind command, applied to both mouse wheel zoom events at once, resetting to No Change.
//
// Control Unbinder owns the result. ZoomIn and ZoomOut are in-memory entries the engine resets on
// every launch; Apply() stores them in Control Unbinder's own Zoom In / Zoom Out settings, which it
// re-applies on every launch. Nothing is stored here.
//
// The reset needs both the ini write (what this plugin reads next pass) and MCM.SetModSettingInt
// (what the menu shows this session), because MCM builds its settings store once per launch and
// never re-reads the ini. Only the ini write is correctness-critical.
namespace WS::ZoomUnbind
{
	enum class Command : std::int32_t
	{
		kNoChange = 0,
		kUnbind = 1,
		kRebind = 2
	};

	namespace detail
	{
		namespace Api = WattzIO::ControlUnbinderAPI;

		inline constexpr const char* kModName = "WIO-WeaponSwap";
		inline constexpr const char* kSection = "Input";
		inline constexpr const char* kCommandKey = "iUnbindZoomKeyboard";
		inline constexpr const char* kStatusKey = "sZoomKeyboardStatus:Input";
		inline constexpr const char* kZoomIn = "ZoomIn";
		inline constexpr const char* kZoomOut = "ZoomOut";

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

		inline void ResetCommand()
		{
			Settings::iUnbindZoomKeyboard = 0;
			if (!::WritePrivateProfileStringA(kSection, kCommandKey, "0", Settings::detail::kSettingsPath)) {
				REX::WARN("Weapon Swap Button: could not reset {} - the command may repeat"sv, kCommandKey);
			}

			// MCM's own copy, which never re-reads that file this session.
			if (const auto vm = VM(); vm) {
				const RE::BSFixedString modName{ kModName };
				const RE::BSFixedString key{ std::string{ kCommandKey } + ":" + kSection };
				WIO::Papyrus::DispatchStaticCall(vm, "MCM", "SetModSettingInt", nullptr, modName, key, 0);
			}
		}

		// Two requests, one right after the other. Control Unbinder applies and saves each.
		inline void Request(const Api::API& a_api, bool a_unbind)
		{
			for (const auto* eventID : { kZoomIn, kZoomOut }) {
				const auto result = a_api.Apply(eventID, Api::Slot::kKeyboard,
					a_unbind ? Api::Action::kUnbind : Api::Action::kRestore, kModName);
				REX::INFO("Weapon Swap Button: {} {} - {}"sv, a_unbind ? "unbind"sv : "rebind"sv, eventID,
					a_api.ResultName(result));
			}
		}

		// Live engine state, never parsed out of Control Unbinder's own translated text.
		//
		// Round-trips through the ini, which MCM reads back with the ANSI
		// GetPrivateProfileSection, so a non-codepage translation shows as mojibake there.
		[[nodiscard]] inline std::string StatusText()
		{
			const auto* api = ApiConsumer::Get();
			if (!api) {
				return WIO::Translations::Localize("$WS_Status_NeedsUnbinder"sv, "Requires Control Unbinder"sv);
			}

			const auto zoomIn = api->Query(kZoomIn, Api::Slot::kKeyboard);
			const auto zoomOut = api->Query(kZoomOut, Api::Slot::kKeyboard);

			if (zoomIn.bound && zoomOut.bound &&
				zoomIn.mouse == RE::kBSButtonCodeWheelUp && zoomOut.mouse == RE::kBSButtonCodeWheelDown) {
				return WIO::Translations::Localize("$WS_Status_ZoomBound"sv, "Bound (Mouse: Wheel Up and Wheel Down)"sv);
			}
			if (zoomIn.applicable && zoomOut.applicable && !zoomIn.bound && !zoomOut.bound) {
				return WIO::Translations::Localize("$WS_Status_Unbound"sv, "Unbound"sv);
			}

			// Anything else - one bound and one not, or a code other than the vanilla wheel - is
			// named per event from the provider's own status text.
			const auto describe = [&](const char* a_eventID) {
				const auto* described = api->DescribeBinding(a_eventID, Api::Slot::kKeyboard);
				return described ? std::string{ described } :
				                   WIO::Translations::Localize("$WS_Status_Unknown"sv, "Unknown"sv);
			};
			const auto inText = describe(kZoomIn);
			const auto outText = describe(kZoomOut);
			return WIO::Translations::Localize("$WS_Status_ZoomMixed"sv, "Zoom In: {IN}, Zoom Out: {OUT}"sv,
				{ { "{IN}", inText }, { "{OUT}", outText } });
		}
	}

	// Refreshes the status line from the engine, at kGameLoaded, on any menu opening, and after a
	// command.
	inline void RefreshStatus()
	{
		detail::PushString(detail::kStatusKey, detail::StatusText());
	}

	// Called on every PauseMenu close, after Settings::Load(). Acts only when the player moved the
	// dropdown.
	inline void Run()
	{
		const auto command = static_cast<Command>(Settings::iUnbindZoomKeyboard);
		if (command == Command::kNoChange) {
			return;
		}

		const auto* api = ApiConsumer::Get();
		if (!api) {
			REX::WARN("Weapon Swap Button: zoom unbind command set but WIO-ControlUnbinder is not installed"sv);
		} else if (command == Command::kUnbind || command == Command::kRebind) {
			detail::Request(*api, command == Command::kUnbind);
		}

		detail::ResetCommand();
		RefreshStatus();
	}
}
