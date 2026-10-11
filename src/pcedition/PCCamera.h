#pragma once

// Zelda on PC - mouse to camera conversion.
//
// The game reads the right stick once per frame. Its stick pipeline (reverse engineered from U-King.rpx v1.5.0):
//   1. global stick getter (0x02E58D1C): radial deadzone 0.005 .. 0.995, rescale to 0..1, then |s|^2 response curve
//   2. camera controller (0x02B9B8A0): yaw rate is linear in the curved X value, pitch squares the curved Y value again
//   3. scaled by the in-game "Camera sensitivity" option, delta time and per-mode factors
// We compute the exact stick value which makes the camera rotate by the mouse displacement of this frame,
// and carry the remainder that could not be expressed (saturation) into the next frame.

#include "pcedition/PCSettings.h"

namespace pce
{
	struct CameraCalibration
	{
		// rotation speed (degrees/second) at full (post-curve) deflection
		// measured on foot, 60 FPS (FPS++ dynamic speed), in-game "Camera sensitivity" = Normal
		float yawRateFull = 85.39f;
		float pitchRateFull = 61.8f;
		// extra exponent applied by the camera controller after the global stick curve
		float yawExtraExp = 1.0f;
		float pitchExtraExp = 2.0f;
		// global stick curve parameters
		float innerDeadzone = 0.005f;
		float outerDeadzone = 0.995f;
		float curveExp = 2.0f;
		// maximum stick magnitude the game accepts (1.0 stock, larger if the clamp is patched out)
		float maxMagnitude = 1.0f;
		// maximum carried remainder in degrees (prevents the camera from drifting after a very fast flick)
		float maxCarryDeg = 12.0f;
	};

	CameraCalibration GetCameraCalibration();
	void SetCameraCalibration(const CameraCalibration& c);

	// mouse counts -> camera degrees (right / up positive), using the user's sensitivity settings
	void MouseToDegrees(const Settings& s, double mdx, double mdy, bool aiming, double& yawDeg, double& pitchDeg);

	class CameraConverter
	{
	public:
		void Configure(const Settings& s);
		void Reset();
		// desired camera rotation this frame (degrees, right/up positive), frame time (s); outputs raw stick values for VPAD
		void Update(double yawDeg, double pitchDeg, double dt, float keyX, float keyY, float& outX, float& outY);

		// inverse of the global stick curve: post-curve vector -> raw stick vector
		static void InverseStickCurve(const CameraCalibration& c, float px, float py, float& rx, float& ry);
		// forward curve (for testing)
		static void ForwardStickCurve(const CameraCalibration& c, float rx, float ry, float& px, float& py);

	private:
		Settings m_settings;
		double m_carryYaw = 0.0, m_carryPitch = 0.0;
	};
}
