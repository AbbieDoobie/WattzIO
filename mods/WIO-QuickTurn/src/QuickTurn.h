#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <string_view>

#include "CameraTrace.h"
#include "Settings.h"

namespace QT::QuickTurn
{
	// === F4RD RELOCATIONS ========================================================
	//   Kind   Site                                   ID / RVA     +Off
	//   off    ThirdPersonState::freeRotation         CLib field   0x0C0
	//   off    ThirdPersonState::freeRotationEnabled  CLib field   0x128
	// =============================================================================

	namespace detail
	{
		// Movement held-state, updated per-event since there is no engine poll API.
		inline std::int32_t s_fwdKey   = -1;
		inline std::int32_t s_backKey  = -1;
		inline std::int32_t s_leftKey  = -1;
		inline std::int32_t s_rightKey = -1;

		inline bool s_fwdHeld   = false;
		inline bool s_backHeld  = false;
		inline bool s_leftHeld  = false;
		inline bool s_rightHeld = false;

		// Left thumbstick tilt (-1..1), player-relative (same space as WASD).
		inline float s_stickX = 0.0f;
		inline float s_stickY = 0.0f;

		// In-progress smooth turn state. The eased offset (0..s_deltaYaw) is added to whichever of
		// the two start angles the turn targets.
		inline bool  s_turning = false;
		inline bool  s_turnHeading = false;  // player heading (data.angle.z)
		inline bool  s_turnCamera = false;   // third-person free-rotation camera yaw
		inline float s_startYaw = 0.0f;      // radians, player heading at trigger
		inline float s_startCamYaw = 0.0f;   // radians, ThirdPersonState::freeRotation.x at trigger
		inline float s_deltaYaw = 0.0f;      // radians
		inline float s_elapsedSecs = 0.0f;
		inline float s_durationSecs = 0.0f;

		constexpr float kPi = std::numbers::pi_v<float>;
		constexpr float kTwoPi = 2.0f * kPi;
		constexpr float kHalfPi = kPi / 2.0f;

		// Below this magnitude, no direction is considered held. Primarily softens light
		// gamepad stick tilt; keyboard is always exactly 0 or >= 1.
		constexpr float kMovementDeadzone = 0.35f;

		[[nodiscard]] inline float NormalizeAngle(float a_radians)
		{
			float a = std::fmod(a_radians + kPi, kTwoPi);
			if (a < 0.0f) {
				a += kTwoPi;
			}
			return a - kPi;
		}

		// Rounds to the nearest multiple of 90 degrees, result normalized to (-pi, pi].
		[[nodiscard]] inline float SnapToNearestCardinal(float a_radians)
		{
			return NormalizeAngle(std::round(a_radians / kHalfPi) * kHalfPi);
		}

		struct MoveVec
		{
			float x = 0.0f;  // +right
			float y = 0.0f;  // +forward
		};

		// Prefers the gamepad stick when past the deadzone; otherwise uses keyboard state.
		[[nodiscard]] inline MoveVec GetCombinedMoveVec()
		{
			const float gpMag = std::sqrt(s_stickX * s_stickX + s_stickY * s_stickY);
			if (gpMag >= kMovementDeadzone) {
				return { s_stickX, s_stickY };
			}

			MoveVec kb{};
			if (s_fwdHeld) {
				kb.y += 1.0f;
			}
			if (s_backHeld) {
				kb.y -= 1.0f;
			}
			if (s_rightHeld) {
				kb.x += 1.0f;
			}
			if (s_leftHeld) {
				kb.x -= 1.0f;
			}
			return kb;
		}

		inline void ApplyYaw(RE::PlayerCharacter& a_player, float a_yawRadians)
		{
			const auto current = a_player.data.angle;
			a_player.SetAngleOnReference(RE::NiPoint3{ current.x, current.y, a_yawRadians });
		}

		enum class Perspective
		{
			kOther,  // VATS, furniture, free camera, transitions: heading-only turn, not gated by Perspective settings
			kFirst,
			kThird,
		};

		// a_freeCamera is set only in third person while ThirdPersonState's free rotation is on
		// (weapon holstered). Only then does the camera have a yaw of its own; otherwise the engine
		// derives it from the player heading every frame.
		[[nodiscard]] inline Perspective GetPerspective(RE::ThirdPersonState** a_freeCamera = nullptr)
		{
			if (a_freeCamera) {
				*a_freeCamera = nullptr;
			}
			const auto camera = RE::PlayerCamera::GetSingleton();
			const auto state = camera ? camera->currentState.get() : nullptr;
			if (!state) {
				return Perspective::kOther;
			}
			switch (state->id.get()) {
			case RE::CameraStates::kFirstPerson:
			case RE::CameraStates::kIronSights:  // first-person aim, gated by bApplyInFirstPerson
				return Perspective::kFirst;
			case RE::CameraStates::k3rdPerson:
				{
					// F4RD:off - ThirdPersonState::freeRotationEnabled, see banner
					const auto third = static_cast<RE::ThirdPersonState*>(state);
					if (a_freeCamera && third->freeRotationEnabled) {
						*a_freeCamera = third;
					}
					return Perspective::kThird;
				}
			default:
				return Perspective::kOther;
			}
		}

		// While free rotation is on, freeRotation.x is the camera's absolute world yaw, wrapped to
		// +-pi, and is independent of the player heading: a heading change leaves it untouched and
		// nothing in the engine compensates. The engine reads it back every frame with no smoothing of
		// its own.
		inline void ApplyOffset(RE::PlayerCharacter& a_player, float a_offset)
		{
			if (s_turnHeading) {
				ApplyYaw(a_player, NormalizeAngle(s_startYaw + a_offset));
			}
			if (s_turnCamera) {
				RE::ThirdPersonState* freeCamera = nullptr;
				GetPerspective(&freeCamera);
				if (freeCamera) {
					// F4RD:off - ThirdPersonState::freeRotation, see banner
					freeCamera->freeRotation.x = NormalizeAngle(s_startCamYaw + a_offset);
				} else {
					// Left free rotation mid-turn (weapon drawn, perspective switch): the camera now
					// follows the heading, so there is nothing of its own left to turn.
					s_turnCamera = false;
				}
			}
		}
	}

	// Returns the turn delta (radians) for the given mode, or nullopt if the mode requires a
	// held direction and none is present. Mode: <=0=Off (180), 1=Back Only, 2=Cardinals, 3=Omni.
	[[nodiscard]] inline std::optional<float> ComputeTurnDeltaYaw(int a_mode)
	{
		if (a_mode <= 0) {
			return detail::kPi;  // Off - always a full 180, ignoring movement entirely
		}

		const auto move = detail::GetCombinedMoveVec();
		const float mag = std::sqrt(move.x * move.x + move.y * move.y);
		if (mag < detail::kMovementDeadzone) {
			return std::nullopt;  // no direction held - modifier gates the turn off
		}

		// atan2(x, y): 0 = forward, +half-pi = right, +-pi = back, -half-pi = left.
		const float angle = std::atan2(move.x, move.y);

		switch (a_mode) {
		case 1:  // Back Only - 180 if holding back, nullopt otherwise.
			{
				const float nearest = detail::SnapToNearestCardinal(angle);
				constexpr float kEpsilon = 0.01f;
				if (std::abs(std::abs(nearest) - detail::kPi) > kEpsilon) {
					return std::nullopt;
				}
				return detail::kPi;
			}
		case 2:  // Cardinals Only
			return detail::SnapToNearestCardinal(angle);
		case 3:  // Omnidirectional
		default:
			return angle;
		}
	}

	// Re-resolves which keyboard keys perform Forward/Back/Left/Right from the live ControlMap,
	// so rebound or non-WASD layouts are respected. Called at kGameLoaded and on pause-menu close.
	inline void RefreshMovementKeyBindings()
	{
		const auto controlMap = RE::ControlMap::GetSingleton();
		if (!controlMap) {
			return;
		}

		const auto resolve = [&](std::string_view a_eventID) -> std::int32_t {
			const auto key = RE::GetMappedKey(controlMap, a_eventID, RE::INPUT_DEVICE::kKeyboard);
			return key == RE::kInvalidMappedKey ? -1 : static_cast<std::int32_t>(key);
		};

		detail::s_fwdKey   = resolve("Forward"sv);
		detail::s_backKey  = resolve("Back"sv);
		detail::s_leftKey  = resolve("Left"sv);
		detail::s_rightKey = resolve("Right"sv);
	}

	// Updates held-state for whichever movement key (if any) matches a_keycode.
	inline void UpdateKeyboardMovementState(std::uint32_t a_keycode, bool a_pressed)
	{
		const auto code = static_cast<std::int32_t>(a_keycode);
		if (detail::s_fwdKey >= 0 && code == detail::s_fwdKey) {
			detail::s_fwdHeld = a_pressed;
		} else if (detail::s_backKey >= 0 && code == detail::s_backKey) {
			detail::s_backHeld = a_pressed;
		} else if (detail::s_leftKey >= 0 && code == detail::s_leftKey) {
			detail::s_leftHeld = a_pressed;
		} else if (detail::s_rightKey >= 0 && code == detail::s_rightKey) {
			detail::s_rightHeld = a_pressed;
		}
	}

	inline void UpdateGamepadStickState(float a_x, float a_y)
	{
		detail::s_stickX = a_x;
		detail::s_stickY = a_y;
	}

	// Clears all movement held-state. Called on menu open so events swallowed by a menu
	// do not leave a direction stuck.
	inline void ResetMovementState()
	{
		detail::s_fwdHeld = detail::s_backHeld = detail::s_leftHeld = detail::s_rightHeld = false;
		detail::s_stickX = detail::s_stickY = 0.0f;
	}

	// Starts (or restarts) a smooth turn. Direct Input path: call with no argument, delta is
	// computed from iQuickTurnMovementModifier. Shared Action path: pass a pre-computed delta
	// from ComputeTurnDeltaYaw(), already validated by ContextualRemap before calling here.
	//
	// Returns false when no turn starts - no held direction, or the Perspective settings turn Quick
	// Turn off in the current view - so Shared Action can leave the vanilla press alone.
	inline bool Trigger(std::optional<float> a_precomputedDelta = std::nullopt)
	{
		const auto deltaYaw = a_precomputedDelta.has_value() ?
		                          a_precomputedDelta :
		                          ComputeTurnDeltaYaw(Settings::iQuickTurnMovementModifier);

		if (!deltaYaw) {
			return false;
		}

		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return false;
		}

		RE::ThirdPersonState* freeCamera = nullptr;
		bool turnHeading = false;
		bool turnCamera = false;
		switch (detail::GetPerspective(&freeCamera)) {
		case detail::Perspective::kFirst:
			if (!Settings::bApplyInFirstPerson) {
				return false;
			}
			turnHeading = true;
			break;
		case detail::Perspective::kThird:
			switch (Settings::thirdPersonMode) {
			case Settings::ThirdPersonMode::kOff:
				return false;
			case Settings::ThirdPersonMode::kTurnCamera:
				turnCamera = freeCamera != nullptr;
				turnHeading = !turnCamera;  // no free camera: it follows the heading, so turn that
				break;
			case Settings::ThirdPersonMode::kTurnPlayer:
				turnHeading = true;
				break;
			case Settings::ThirdPersonMode::kTurnBoth:
				turnHeading = true;
				turnCamera = freeCamera != nullptr;
				break;
			}
			break;
		case detail::Perspective::kOther:
			turnHeading = true;
			break;
		}

		// Start from the live angles so an in-progress turn restarts from its current position.
		const int durationMs = std::clamp(Settings::iQuickTurnDurationMs, 0, 500);

		detail::s_turnHeading = turnHeading;
		detail::s_turnCamera = turnCamera;
		detail::s_startYaw = player->data.angle.z;
		// F4RD:off - ThirdPersonState::freeRotation, see banner
		detail::s_startCamYaw = freeCamera ? freeCamera->freeRotation.x : 0.0f;
		detail::s_deltaYaw = *deltaYaw;
		detail::s_durationSecs = static_cast<float>(durationMs) / 1000.0f;
		detail::s_elapsedSecs = 0.0f;

		CameraTrace::Begin(*deltaYaw, detail::s_durationSecs, detail::s_stickX, detail::s_stickY,
			detail::s_fwdHeld, detail::s_backHeld, detail::s_leftHeld, detail::s_rightHeld, turnHeading, turnCamera);

		if (detail::s_durationSecs <= 0.0f) {
			detail::s_turning = false;
			detail::ApplyOffset(*player, detail::s_deltaYaw);
			return true;
		}

		detail::s_turning = true;
		return true;
	}

	[[nodiscard]] inline bool IsTurning()
	{
		return detail::s_turning;
	}

	// Advances the in-progress turn. Called once per frame from InputHook.
	inline void Update()
	{
		if (!detail::s_turning) {
			return;
		}

		const auto timer = RE::GetBSTimer();
		const float dt = timer ? timer->realTimeDelta : 0.0f;
		// realTimeDelta so the turn duration is unaffected by slow-mo/time-scale effects.
		// Clamped to guard against a large dt spike skipping the interpolation.
		detail::s_elapsedSecs += std::clamp(dt, 0.0f, 0.25f);

		const float t = std::clamp(detail::s_elapsedSecs / detail::s_durationSecs, 0.0f, 1.0f);
		const float eased = t * t * (3.0f - 2.0f * t);  // smoothstep
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			detail::s_turning = false;
			return;
		}
		detail::ApplyOffset(*player, detail::s_deltaYaw * eased);

		if (t >= 1.0f) {
			detail::s_turning = false;
		}
	}
}
