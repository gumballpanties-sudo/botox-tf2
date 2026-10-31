// Gate test for the MV_DetectWallLines broadphase bound (src/hacks/movement/mv_scanbound.h).
//
// The broadphase is one zero-length hull trace that lets the 16-direction scan return false
// without tracing anything. It is only safe while the box it traces CONTAINS every trace the
// loop would have run: too small by a unit and TextureBug / AirStuck / WallClimb silently stop
// seeing walls they used to see, on geometry nobody thinks to retest.
//
// So this test re-implements the loop's own per-direction arithmetic, verbatim from
// movement.cpp, and asserts ScanReachXY covers the furthest xy reach of every family of trace
// it runs, over every direction, for a wide spread of hulls / reaches / velocities:
//
//   lines + column sweep : ray_length = support + extra_reach + lookahead, from the centre
//   hull prefilter       : pre_length = extra_reach + lookahead, PLUS the swept hull's own
//                          corner, since that box is not a point
//
// Build + run: cmake --build build --config Release --target mvscan_test && build\Release\mvscan_test.exe
#include "../src/hacks/movement/mv_scanbound.h"
#include <cstdio>
#include <cmath>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

static constexpr float kPi = 3.14159265358979323846f;

// the furthest any trace of one scan reaches from start_pos in xy, computed the way
// MV_DetectWallLines computes it -- one direction at a time, no shortcuts
static float LoopWorstReach(float flHalfX, float flHalfY, float flExtraReach,
                            float flVelX, float flVelY, float flDt, float flScanStart)
{
    const float flMaxRadius = kPi * 2.f;
    const float flStep = flMaxRadius / 16.f;
    float flWorst = 0.f;

    for (float a = flScanStart; a < flScanStart + flMaxRadius; a += flStep)
    {
        const float flAngle = std::fmod(a, flMaxRadius);
        const float dir_x = std::cos(flAngle);
        const float dir_y = std::sin(flAngle);

        const float ax = std::fmax(std::fabs(dir_x), 1e-6f);
        const float ay = std::fmax(std::fabs(dir_y), 1e-6f);
        const float flLookahead = std::fmax(0.f, (flVelX * dir_x + flVelY * dir_y) * flDt);
        const float flSupport = std::fmin(flHalfX / ax, flHalfY / ay);

        // column sweep and every line trace leave the centre and run this far
        const float flRayLength = flSupport + flExtraReach + flLookahead;
        if (flRayLength > flWorst) flWorst = flRayLength;

        // the hull prefilter sweeps a BOX this far, so its furthest point is the sweep plus
        // the box's own far corner from the centre
        const float flPreLength = std::fmax(flExtraReach + flLookahead, 0.f);
        const float flPreReach = flPreLength + std::sqrt(flHalfX * flHalfX + flHalfY * flHalfY);
        if (flPreReach > flWorst) flWorst = flPreReach;
    }
    return flWorst;
}

int main()
{
    // ---- hand cases ----------------------------------------------------------------------

    // standing still, 24x24 player hull, reach 8 -> diagonal 33.94 + 8 + slack
    {
        const float r = mv::ScanReachXY(24.f, 24.f, 8.f, 0.f, 0.015f);
        CHECK(r > 41.9f && r < 43.1f);
    }
    // the `support` trap: a square hull's worst support is the DIAGONAL, not half_x
    CHECK(mv::ScanReachXY(24.f, 24.f, 0.f, 0.f, 0.f) > 24.f + mv::k_scanBoundSlack);
    // velocity adds exactly one tick of travel
    {
        const float flStill = mv::ScanReachXY(24.f, 24.f, 8.f, 0.f, 0.015f);
        const float flFast = mv::ScanReachXY(24.f, 24.f, 8.f, 400.f, 0.015f);
        CHECK(std::fabs((flFast - flStill) - 400.f * 0.015f) < 1e-3f);
    }
    // no velocity passed (WC / AS call it that way) must not shrink the box
    CHECK(mv::ScanReachXY(24.f, 24.f, 8.f, 0.f, 0.f) >= 24.f);
    // degenerate inputs must never produce a bound SMALLER than the hull itself
    CHECK(mv::ScanReachXY(24.f, 24.f, -50.f, -100.f, -1.f) >= 33.9f);

    // ---- the real claim: the bound contains the loop, over the whole input space ----------
    // hull sizes span ducked (24x24 stays, z is not our axis) through oversized, reaches span
    // the TB slider's floor to well past its ceiling, speeds span standing to a rocket ride.
    int nCases = 0;
    float flWorstMargin = 1e9f;
    for (float flHalfX = 8.f; flHalfX <= 64.f; flHalfX += 4.f)
    for (float flHalfY = 8.f; flHalfY <= 64.f; flHalfY += 4.f)
    for (float flReach = 0.f; flReach <= 64.f; flReach += 8.f)
    for (float flSpeed = 0.f; flSpeed <= 1200.f; flSpeed += 150.f)
    for (int iDir = 0; iDir < 7; ++iDir)
    {
        const float flDt = 0.015f;
        // velocity pointed anywhere, including off the scan's own 16 spokes
        const float flVelAng = (kPi * 2.f) * iDir / 7.f + 0.13f;
        const float flVelX = std::cos(flVelAng) * flSpeed;
        const float flVelY = std::sin(flVelAng) * flSpeed;

        const float flBound = mv::ScanReachXY(flHalfX, flHalfY, flReach,
                                              std::sqrt(flVelX * flVelX + flVelY * flVelY), flDt);

        // scan_start is a live value in the shipped code (the ride's own angle), so the loop's
        // spokes land anywhere -- sweep it too
        for (int iStart = 0; iStart < 5; ++iStart)
        {
            const float flScanStart = (kPi * 2.f) * iStart / 5.f;
            const float flWorst = LoopWorstReach(flHalfX, flHalfY, flReach, flVelX, flVelY, flDt, flScanStart);
            ++nCases;
            CHECK(flBound >= flWorst);
            if (flBound < flWorst)
            {
                std::printf("  hx=%.1f hy=%.1f reach=%.1f speed=%.1f start=%.2f bound=%.3f worst=%.3f\n",
                            flHalfX, flHalfY, flReach, flSpeed, flScanStart, flBound, flWorst);
                goto done;   // one counterexample answers the question
            }
            if (flBound - flWorst < flWorstMargin) flWorstMargin = flBound - flWorst;
        }
    }
done:
    CHECK(nCases > 10000);
    // the bound must be TIGHT as well as safe -- a bound 200u too wide would pass the assert
    // above while making the broadphase hit on everything and save nothing
    CHECK(flWorstMargin >= 0.f && flWorstMargin < 2.f);
    std::printf("%d cases, tightest margin %.4fu\n", nCases, flWorstMargin);

    std::printf("%s -- %d checks, %d failed\n", s_failed ? "FAILED" : "ok", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
