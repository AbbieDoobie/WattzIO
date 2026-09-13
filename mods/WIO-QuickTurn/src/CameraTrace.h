#pragma once

#include <cstdint>
#include <numbers>

#include "Settings.h"

namespace QT::CameraTrace
{
	// === F4RD RELOCATIONS ========================================================
	//   Kind   Site                                   ID / RVA     +Off
	//   off    ThirdPersonState::freeRotation         CLib field   0x0C0
	//   off    ThirdPersonState::targetYaw            CLib field   0x0F0
	//   off    ThirdPersonState::currentYaw           CLib field   0x0F4
	//   off    ThirdPersonState::freeRotationEnabled  CLib field   0x128
	// =============================================================================

	namespace detail
	{
		constexpr std::int32_t kTailFrames = 60;

		inline std::int32_t s_framesLeft = 0;
		inline std::uint32_t s_frame = 0;
		inline std::uint32_t s_traceId = 0;

		[[nodiscard]] inline float Deg(float a_radians)
		{
			return a_radians * (180.0f / std::numbers::pi_v<float>);
		}

		struct Sample
		{
			bool valid = false;
			std::uint32_t stateId = 0xFFFFFFFF;
			float heading = 0.0f;
			bool third = false;
			bool freeRotEnabled = false;
			float freeRotX = 0.0f;
			float freeRotY = 0.0f;
			float targetYaw = 0.0f;
			float currentYaw = 0.0f;
		};

		[[nodiscard]] inline Sample Capture()
		{
			Sample s;
			const auto player = RE::PlayerCharacter::GetSingleton();
			const auto camera = RE::PlayerCamera::GetSingleton();
			if (!player || !camera) {
				return s;
			}
			s.valid = true;
			s.heading = player->data.angle.z;

			const auto state = camera->currentState.get();
			if (!state) {
				return s;
			}
			s.stateId = static_cast<std::uint32_t>(state->id.get());
			if (state->id == RE::CameraStates::k3rdPerson) {
				// F4RD:off - ThirdPersonState fields, see banner
				const auto tps = static_cast<RE::ThirdPersonState*>(state);
				s.third = true;
				s.freeRotEnabled = tps->freeRotationEnabled;
				s.freeRotX = tps->freeRotation.x;
				s.freeRotY = tps->freeRotation.y;
				s.targetYaw = tps->targetYaw;
				s.currentYaw = tps->currentYaw;
			}
			return s;
		}

		inline Sample s_pre;
	}

	[[nodiscard]] inline bool Active()
	{
		return Settings::debugLog && detail::s_framesLeft > 0;
	}

	inline void Begin(float a_deltaYaw, float a_durationSecs, float a_stickX, float a_stickY, bool a_fwd, bool a_back, bool a_left, bool a_right,
		bool a_turnHeading, bool a_turnCamera)
	{
		if (!Settings::debugLog) {
			return;
		}
		const auto s = detail::Capture();
		++detail::s_traceId;
		detail::s_frame = 0;
		detail::s_framesLeft = static_cast<std::int32_t>(a_durationSecs * 60.0f) + detail::kTailFrames;

		REX::DEBUG(
			"Quick Turn: [CAM#{}] TRIGGER delta={:.1f} dur={:.0f}ms state={} third={} freeRotOn={} heading={:.1f} freeRot=({:.1f},{:.1f}) tgtYaw={:.1f} curYaw={:.1f} stick=({:.2f},{:.2f}) keys=F{}B{}L{}R{} turnHeading={} turnCamera={}",
			detail::s_traceId, detail::Deg(a_deltaYaw), a_durationSecs * 1000.0f, s.stateId, s.third, s.freeRotEnabled,
			detail::Deg(s.heading), detail::Deg(s.freeRotX), detail::Deg(s.freeRotY), detail::Deg(s.targetYaw), detail::Deg(s.currentYaw),
			a_stickX, a_stickY, a_fwd ? 1 : 0, a_back ? 1 : 0, a_left ? 1 : 0, a_right ? 1 : 0, a_turnHeading, a_turnCamera);
	}

	inline void PreUpdate()
	{
		if (!Active()) {
			return;
		}
		detail::s_pre = detail::Capture();
	}

	namespace detail
	{
		inline std::uint32_t s_camKb = 0;
		inline std::uint32_t s_camMouse = 0;
		inline std::uint32_t s_camGp = 0;
		inline std::uint32_t s_camOther = 0;
		inline std::uint32_t s_ctlSeen = 0;
		inline std::uint32_t s_ctlSwallowed = 0;
		inline float s_heartbeatSecs = 0.0f;
	}

	inline void CountCameraEvent(const RE::InputEvent& a_event)
	{
		if (!Settings::debugLog) {
			return;
		}
		switch (a_event.device.get()) {
		case RE::INPUT_DEVICE::kKeyboard: ++detail::s_camKb; break;
		case RE::INPUT_DEVICE::kMouse:    ++detail::s_camMouse; break;
		case RE::INPUT_DEVICE::kGamepad:  ++detail::s_camGp; break;
		default:                          ++detail::s_camOther; break;
		}
	}

	inline void CountControlsEvent(bool a_swallowed)
	{
		if (!Settings::debugLog) {
			return;
		}
		++detail::s_ctlSeen;
		if (a_swallowed) {
			++detail::s_ctlSwallowed;
		}
	}

	inline void Heartbeat(bool a_swallowLatched)
	{
		if (!Settings::debugLog) {
			return;
		}
		const auto timer = RE::GetBSTimer();
		detail::s_heartbeatSecs += timer ? timer->realTimeDelta : 0.0f;
		if (detail::s_heartbeatSecs < 1.0f) {
			return;
		}
		detail::s_heartbeatSecs = 0.0f;

		const auto s = detail::Capture();
		const auto controls = RE::PlayerControls::GetSingleton();
		const auto controlMap = RE::ControlMap::GetSingleton();
		const auto ui = RE::UI::GetSingleton();

		REX::DEBUG(
			"Quick Turn: [BEAT] cam(kb={} mouse={} gp={} other={}) ctl(seen={} swallowed={}) latch={} menuMode={} ignoreKBM={} textEntry={} move=({:.2f},{:.2f}) look=({:.2f},{:.2f}) state={} freeRotOn={} heading={:.1f} freeRotX={:.1f}",
			detail::s_camKb, detail::s_camMouse, detail::s_camGp, detail::s_camOther,
			detail::s_ctlSeen, detail::s_ctlSwallowed, a_swallowLatched,
			ui ? static_cast<int>(ui->menuMode) : -1,
			controlMap ? controlMap->ignoreKeyboardMouse : false,
			controlMap ? static_cast<int>(controlMap->byTextEntryCount) : -1,
			controls ? controls->data.moveInputVec.x : 0.0f, controls ? controls->data.moveInputVec.y : 0.0f,
			controls ? controls->data.lookInputVec.x : 0.0f, controls ? controls->data.lookInputVec.y : 0.0f,
			s.stateId, s.freeRotEnabled, detail::Deg(s.heading), detail::Deg(s.freeRotX));

		detail::s_camKb = detail::s_camMouse = detail::s_camGp = detail::s_camOther = 0;
		detail::s_ctlSeen = detail::s_ctlSwallowed = 0;
	}

	inline void PostUpdate(bool a_turning)
	{
		if (!Active()) {
			return;
		}
		const auto& pre = detail::s_pre;
		const auto post = detail::Capture();
		const auto timer = RE::GetBSTimer();
		const float dtMs = timer ? timer->realTimeDelta * 1000.0f : 0.0f;

		REX::DEBUG(
			"Quick Turn: [CAM#{}] f{:03} dt={:.1f} turning={} state={} freeRotOn={} | in: heading={:.1f} freeRot=({:.1f},{:.1f}) tgtYaw={:.1f} curYaw={:.1f} cam-heading={:.1f} | out: heading={:.1f} freeRotX={:.1f}",
			detail::s_traceId, detail::s_frame, dtMs, a_turning, pre.stateId, pre.freeRotEnabled,
			detail::Deg(pre.heading), detail::Deg(pre.freeRotX), detail::Deg(pre.freeRotY), detail::Deg(pre.targetYaw), detail::Deg(pre.currentYaw),
			detail::Deg(pre.currentYaw - pre.heading),
			detail::Deg(post.heading), detail::Deg(post.freeRotX));

		++detail::s_frame;
		if (--detail::s_framesLeft == 0) {
			REX::DEBUG("Quick Turn: [CAM#{}] END", detail::s_traceId);
		}
	}
}
