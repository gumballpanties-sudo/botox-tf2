#pragma once
#include <cmath>
#include <algorithm>

// Pure secant root-finder for the grenade lob's pitch (grenade_lob.cpp's SolvePitch). No
// engine types -- the engine-coupled part (simulating where a given pitch actually lands,
// F::ProjSim) is the caller's fnError; this file is only the numerical method, so it's
// gate-testable the same way airlean.h/fbmath.h are.
namespace LobSolve
{
	// fnError(pitch) -> signed landing-distance error (actual - target), caller-supplied.
	// Converges when |error| < flTolerance; *out is only written on success. Returns false
	// (leaving *out untouched) if the two most recent errors are too close to divide by (a
	// stall) or iMaxIter steps run out without reaching tolerance.
	template <typename Fn>
	inline bool Secant(Fn fnError, float flStartPitch, float flTolerance, int iMaxIter, float flPitchMin, float flPitchMax, float* out)
	{
		float p0 = flStartPitch;
		float p1 = std::clamp(flStartPitch - 10.f, flPitchMin, flPitchMax);
		float e0 = fnError(p0);

		for (int i = 0; i < iMaxIter; i++)
		{
			float e1 = fnError(p1);
			if (std::fabs(e1) < flTolerance)
			{
				*out = p1;
				return true;
			}
			if (std::fabs(e1 - e0) < 1e-4f)
				return false;   // stalled -- would divide by ~0

			float p2 = std::clamp(p1 - e1 * (p1 - p0) / (e1 - e0), flPitchMin, flPitchMax);
			p0 = p1; e0 = e1; p1 = p2;
		}
		return false;
	}
}
