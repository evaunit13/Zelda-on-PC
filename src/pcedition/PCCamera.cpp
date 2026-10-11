#include "pcedition/PCCamera.h"

#include <mutex>

namespace pce
{
	namespace
	{
		std::mutex sCalMutex;
		CameraCalibration sCal;

		// base mouse sensitivity: degrees of camera rotation per mouse count at sensitivity 1.0
		constexpr double kDegPerCount = 0.022;
	}

	CameraCalibration GetCameraCalibration()
	{
		std::lock_guard lock(sCalMutex);
		return sCal;
	}

	void SetCameraCalibration(const CameraCalibration& c)
	{
		std::lock_guard lock(sCalMutex);
		sCal = c;
	}

	void CameraConverter::Configure(const Settings& s)
	{
		m_settings = s;
	}

	void CameraConverter::Reset()
	{
		m_carryYaw = m_carryPitch = 0.0;
	}

	void CameraConverter::ForwardStickCurve(const CameraCalibration& c, float rx, float ry, float& px, float& py)
	{
		float m = sqrtf(rx * rx + ry * ry);
		if (m <= c.innerDeadzone)
		{
			px = py = 0.0f;
			return;
		}
		float mm = std::min(m, c.outerDeadzone);
		float u = (mm - c.innerDeadzone) / (c.outerDeadzone - c.innerDeadzone);
		u = std::min(u, c.maxMagnitude);
		float curved = powf(u, c.curveExp);
		px = rx / m * curved;
		py = ry / m * curved;
	}

	void CameraConverter::InverseStickCurve(const CameraCalibration& c, float px, float py, float& rx, float& ry)
	{
		float p = sqrtf(px * px + py * py);
		if (p <= 1e-9f)
		{
			rx = ry = 0.0f;
			return;
		}
		float u = powf(p, 1.0f / c.curveExp);
		float m = c.innerDeadzone + u * (c.outerDeadzone - c.innerDeadzone);
		rx = px / p * m;
		ry = py / p * m;
	}

	void MouseToDegrees(const Settings& s, double mdx, double mdy, bool aiming, double& yawDeg, double& pitchDeg)
	{
		const double sens = kDegPerCount * s.mouseSensitivity * (aiming ? s.aimSensitivity : 1.0f);
		// mouse right -> camera turns right; mouse up (negative dy) -> camera looks up
		yawDeg = mdx * sens * (s.invertX ? -1.0 : 1.0);
		pitchDeg = -mdy * sens * s.mouseSensitivityY * (s.invertY ? -1.0 : 1.0);
	}

	void CameraConverter::Update(double yawIn, double pitchIn, double dt, float keyX, float keyY, float& outX, float& outY)
	{
		const CameraCalibration cal = GetCameraCalibration();
		double yawDeg = yawIn + m_carryYaw;
		double pitchDeg = pitchIn + m_carryPitch;

		// desired post-curve linear amounts (fraction of full rate)
		double ux = (yawDeg / dt) / cal.yawRateFull;
		double uy = (pitchDeg / dt) / cal.pitchRateFull;
		// undo the controller-specific extra exponent
		auto invExp = [](double v, double e) { return v >= 0 ? pow(v, 1.0 / e) : -pow(-v, 1.0 / e); };
		auto fwdExp = [](double v, double e) { return v >= 0 ? pow(v, e) : -pow(-v, e); };
		double px = invExp(ux, cal.yawExtraExp);
		double py = invExp(uy, cal.pitchExtraExp);
		// saturate to the maximum magnitude the game accepts (direction preserved)
		double pm = sqrt(px * px + py * py);
		const double maxP = pow(cal.maxMagnitude, cal.curveExp);
		if (pm > maxP)
		{
			px *= maxP / pm;
			py *= maxP / pm;
		}
		// achieved rotation -> carry the rest (fractional remainder is never lost)
		const double achievedYaw = fwdExp(px, cal.yawExtraExp) * cal.yawRateFull * dt;
		const double achievedPitch = fwdExp(py, cal.pitchExtraExp) * cal.pitchRateFull * dt;
		m_carryYaw = std::clamp(yawDeg - achievedYaw, (double)-cal.maxCarryDeg, (double)cal.maxCarryDeg);
		m_carryPitch = std::clamp(pitchDeg - achievedPitch, (double)-cal.maxCarryDeg, (double)cal.maxCarryDeg);
		if (fabs(m_carryYaw) < 1e-6) m_carryYaw = 0.0;
		if (fabs(m_carryPitch) < 1e-6) m_carryPitch = 0.0;

		float rx, ry;
		InverseStickCurve(cal, (float)px, (float)py, rx, ry);
		// keyboard camera keys add raw deflection
		outX = rx + keyX;
		outY = ry + keyY;
	}
}
