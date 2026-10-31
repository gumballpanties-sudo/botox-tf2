// Gate test for the grenade lob's pitch solver (src/hacks/aim/lob_solve.h): the secant
// root-finder against a closed-form frictionless-parabola model (independent of F::ProjSim,
// which this header has no dependency on -- that's the point of splitting it out).
// Build + run: cmake --build build --config Release --target lob_solve_test && build\Release\lob_solve_test.exe
#include "../src/hacks/aim/lob_solve.h"
#include <cmath>
#include <cstdio>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

static constexpr float kDeg2Rad = 3.14159265f / 180.f;

// Level-ground range for a launch angle (degrees above horizontal), frictionless: R = v^2 sin(2t) / g.
static float Range(float flPitchDeg, float v, float g)
{
	return v * v * std::sin(2.f * flPitchDeg * kDeg2Rad) / g;
}

int main()
{
	const float v = 1000.f, g = 800.f;
	const float flMaxRange = v * v / g;   // apex at 45 deg

	// ---- Case 1: a reachable low-angle shot, solver started near the right answer ----------
	// 20 deg is comfortably inside the low-angle (< 45) branch Run()'s throttle/secant start
	// (current viewangle minus a small nudge) is meant to track.
	{
		const float flWantPitch = 20.f;
		const float flTargetDist = Range(flWantPitch, v, g);

		auto fnError = [&](float p) { return Range(p, v, g) - flTargetDist; };
		float flOut = -999.f;
		bool bOK = LobSolve::Secant(fnError, 25.f, 1.f, 8, -89.f, 89.f, &flOut);

		CHECK(bOK);
		CHECK(std::fabs(flOut - flWantPitch) < 2.f);
	}

	// ---- Case 2: a different reachable angle/distance, started from a different pitch -----
	{
		const float flWantPitch = 35.f;
		const float flTargetDist = Range(flWantPitch, v, g);

		auto fnError = [&](float p) { return Range(p, v, g) - flTargetDist; };
		float flOut = -999.f;
		bool bOK = LobSolve::Secant(fnError, 40.f, 1.f, 8, -89.f, 89.f, &flOut);

		CHECK(bOK);
		CHECK(std::fabs(flOut - flWantPitch) < 2.f);
	}

	// ---- Case 3: unreachable distance (beyond the v^2/g apex range) must fail, not return a
	// bogus "converged" pitch -- this is what lets grenade_lob.cpp's Run() leave the player's
	// own aim alone instead of snapping to a wrong angle.
	{
		const float flTargetDist = flMaxRange * 2.f;
		auto fnError = [&](float p) { return Range(p, v, g) - flTargetDist; };
		float flOut = -999.f;
		bool bOK = LobSolve::Secant(fnError, 30.f, 1.f, 8, -89.f, 89.f, &flOut);

		CHECK(!bOK);
		CHECK(flOut == -999.f);   // untouched on failure
	}

	// ---- Case 4: the tolerance/iteration knobs are respected -- 0 iterations never converges
	{
		auto fnError = [&](float p) { return Range(p, v, g) - Range(10.f, v, g); };
		float flOut = -999.f;
		bool bOK = LobSolve::Secant(fnError, 10.1f, 1.f, 0, -89.f, 89.f, &flOut);
		CHECK(!bOK);
	}

	std::printf("%d/%d checks passed\n", s_checks - s_failed, s_checks);
	return s_failed ? 1 : 0;
}
