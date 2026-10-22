#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "Settings.h"

// Pipes the Papyrus VM's own log messages into WIO-SmartHeal.log for a short window around a
// dispatch this mod makes.
//
// IVirtualMachine::DispatchMethodCall returns true for "the call was queued" and nothing else.
// Everything that can go wrong after that - a type the native binding will not accept, a `self`
// the VM cannot resolve, a function not on the object's type - is reported through the VM's
// ErrorLogger and is invisible from C++. That is a BSTEventSource<LogEvent>, so a sink gets
// those messages interleaved with this mod's own trace. It only writes while armed, for a short
// window after a dispatch, and is not registered at all unless bDebugLog is on.
namespace SH::PapyrusLog
{
	namespace detail
	{
		// A deadline, not a frame countdown: the window has to cover the VM reaching a queued call,
		// which is tens of milliseconds, and a frame count means something different per machine.
		inline std::chrono::steady_clock::time_point g_openUntil{};

		[[nodiscard]] inline std::string_view SeverityName(RE::BSScript::ErrorLogger::Severity a_severity)
		{
			switch (a_severity) {
			case RE::BSScript::ErrorLogger::Severity::kWarning:
				return "warning"sv;
			case RE::BSScript::ErrorLogger::Severity::kError:
				return "error"sv;
			case RE::BSScript::ErrorLogger::Severity::kFatal:
				return "FATAL"sv;
			default:
				return "info"sv;
			}
		}

		class Sink : public RE::BSTEventSink<RE::BSScript::LogEvent>
		{
		public:
			[[nodiscard]] static Sink* GetSingleton()
			{
				static Sink singleton;
				return std::addressof(singleton);
			}

			// Runs on whichever thread the VM is on. It reads two fields and formats a string, so REX's
			// logger is the only shared state it touches.
			RE::BSEventNotifyControl ProcessEvent(
				const RE::BSScript::LogEvent&                  a_event,
				RE::BSTEventSource<RE::BSScript::LogEvent>*) override
			{
				if (std::chrono::steady_clock::now() < g_openUntil) {
					RE::BSFixedString message;
					a_event.errorMsg.GetErrorMsg(message);
					REX::DEBUG("Smart Heal Button: [PAPYRUS] {}: {}"sv,
						SeverityName(a_event.severity),
						message.empty() ? "<empty>" : message.c_str());
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		inline bool g_registered = false;
		inline bool g_typesDumped = false;
	}

	// Starts listening, if it is not already. Safe to call repeatedly; does nothing without
	// bDebugLog, and nothing if the VM is not up yet.
	inline void Arm(std::int32_t a_milliseconds)
	{
		if (!Settings::debugLog) {
			return;
		}

		if (!detail::g_registered) {
			const auto gameVM = RE::GameVM::GetSingleton();
			const auto vm = gameVM ? gameVM->GetVM() : nullptr;
			if (!vm) {
				return;
			}
			vm->RegisterForLogEvent(detail::Sink::GetSingleton());
			detail::g_registered = true;
			REX::DEBUG("Smart Heal Button: [PAPYRUS] listening to the VM's error logger"sv);
		}

		detail::g_openUntil = std::chrono::steady_clock::now() +
		                      std::chrono::milliseconds{ a_milliseconds };
	}

	// One-shot dump of the VM's own type tables, printed before the first dispatch this mod makes.
	// The EquipItem signature itself is not read: CommonLibF4RD only forward-declares
	// BSScript::IFunction, so walking the Actor type's member functions would mean hand-rolling a
	// vtable layout.
	inline void DumpTypes()
	{
		if (!Settings::debugLog || detail::g_typesDumped) {
			return;
		}

		const auto gameVM = RE::GameVM::GetSingleton();
		const auto vm = gameVM ? gameVM->GetVM() : nullptr;
		if (!vm) {
			return;
		}
		detail::g_typesDumped = true;

		const auto byID = [&](std::string_view a_label, std::uint32_t a_typeID) {
			RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> info;
			if (vm->GetScriptObjectType(a_typeID, info) && info) {
				REX::DEBUG("Smart Heal Button: [PAPYRUS] typeID {} ({}) -> '{}'"sv,
					a_typeID, a_label, info->name.c_str());
			} else {
				REX::DEBUG("Smart Heal Button: [PAPYRUS] typeID {} ({}) -> <not registered>"sv,
					a_typeID, a_label);
			}
		};

		byID("TESForm"sv, RE::BSScript::GetVMTypeID<RE::TESForm>());
		byID("AlchemyItem"sv, RE::BSScript::GetVMTypeID<RE::AlchemyItem>());
		byID("Actor"sv, RE::BSScript::GetVMTypeID<RE::Actor>());

		std::uint32_t formTypeID = 0;
		if (vm->GetTypeIDForScriptObject("Form"sv, formTypeID)) {
			REX::DEBUG("Smart Heal Button: [PAPYRUS] script type 'Form' -> typeID {}"sv, formTypeID);
		} else {
			REX::DEBUG("Smart Heal Button: [PAPYRUS] script type 'Form' has no type ID"sv);
		}

		// Potion -> ... -> Form, or wherever it actually stops.
		RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> potion;
		if (vm->GetScriptObjectType(RE::BSScript::GetVMTypeID<RE::AlchemyItem>(), potion) && potion) {
			std::string chain;
			for (auto iter = potion; iter; iter = iter->parentTypeInfo) {
				if (!chain.empty()) {
					chain += " -> ";
				}
				chain += iter->name.c_str();
			}
			REX::DEBUG("Smart Heal Button: [PAPYRUS] AlchemyItem type chain: {}"sv, chain);
		}
	}
}
