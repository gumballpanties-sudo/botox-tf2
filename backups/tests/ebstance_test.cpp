// Gate test for the edge bug pure half (src/hacks/movement/eb_stance.h): botox detection bands at TF2's
// tick, the duck press window, the stance rule and the plan pick that prefers standing bugs.
// Build + run:  cmake --build build --config Release --target ebstance_test && build\Release\ebstance_test.exe
#include "../src/hacks/movement/eb_stance.h"
#include <cstdio>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

enum { kAny = 0, kDuckOnly = 1, kStandOnly = 2 };

// TF2: sv_gravity 800 over 66.67 tick
static const float kIpt = 0.015f;
static const float kG = 800.f * 0.5f * kIpt;   // half gravity per tick = 6.0
static const float kS = 64.f * kIpt;           // tick scale = 0.96

static int s_nearCalls = 0, s_steepCalls = 0;
static bool Check(int mode, float vz0, float vz, bool near = false, bool steep = false, bool ground = false)
{
    return MV_EbCheck(mode, vz0, vz, kG, kS, vz0 < -6.f * kS, ground,
                      [&] { ++s_nearCalls; return near; }, [&] { ++s_steepCalls; return steep; });
}

int main()
{
    // ================= detection bands ==========================================================
    // a free-fall tick (vz drops one full gravity step, 2g) is never an edgebug, in any mode
    for (int mode = 0; mode <= 2; ++mode)
        for (float vz0 = -40.f; vz0 >= -600.f; vz0 -= 37.f)
            CHECK(!Check(mode, vz0, vz0 - 2.f * kG));
    // the exact landing tick: vz snapped to one half step (-g) after a real fall
    CHECK(Check(0, -300.f, -kG));
    CHECK(Check(1, -300.f, -kG));
    CHECK(Check(2, -300.f, -kG));
    // grounded / not falling: nothing is an edgebug
    CHECK(!Check(0, -300.f, -kG, false, false, true));
    CHECK(!Check(0, -2.f, -kG));
    // mode 0 never asks for a trace (they cost a ray per SIM tick)
    s_nearCalls = s_steepCalls = 0;
    for (float vz0 = -10.f; vz0 >= -500.f; vz0 -= 13.f) { Check(0, vz0, vz0 * 0.5f); Check(0, vz0, -kG); }
    CHECK(s_nearCalls == 0 && s_steepCalls == 0);
    // ebanat: slowed-but-still-falling verdict is refused when a steep ramp clipped the fall
    CHECK(Check(2, -100.f, -kG + 0.1f, false, false));   // snapped to -g
    CHECK(Check(2, -100.f, -50.f, false, false));         // slowed, still falls a step
    CHECK(!Check(2, -100.f, -50.f, false, true));         // ...unless a ramp did the slowing
    // enhanced: fall parked near zero is a bug
    CHECK(Check(1, -200.f, -1.f));

    // ================= duck press window (botox edgebug.cpp:1063) ==============================
    {
        // exact centre 9, retry 3: presses 6..8, walked from 8 down
        const MvEbPress w = MV_EbSweepWindow(9, false, 40, false, 3, 2);
        CHECK(w.first == 6 && w.last == 8);
        // approx centre (reach chord): 2 ticks slack both ends
        const MvEbPress a = MV_EbSweepWindow(9, true, 40, false, 3, 2);
        CHECK(a.first == 4 && a.last == 10);
        // never past the run
        CHECK(MV_EbSweepWindow(30, true, 12, false, 3, 2).last == 11);
        // ducked twin ran: press 0 = its base run, skipped
        CHECK(MV_EbSweepWindow(2, false, 40, true, 3, 2).first == 1);
        CHECK(MV_EbSweepWindow(2, false, 40, false, 3, 2).first == 0);
        // no centre: nothing
        CHECK(MV_EbSweepWindow(0, false, 40, false, 3, 2).last < 0);
        CHECK(MV_EbSweepWindow(-1, true, 40, false, 3, 2).last < 0);
        for (int run = 2; run < 60; ++run)
        {
            const MvEbPress v = MV_EbSweepWindow(run / 2, run % 3 == 0, run, run % 2 == 0, 3, 2);
            CHECK(v.last < run);
            CHECK(v.first >= 0);
        }
    }

    // ================= stance ==================================================================
    CHECK(MV_EbStanceOk(kAny, true) && MV_EbStanceOk(kAny, false));
    CHECK(MV_EbStanceOk(kDuckOnly, true) && !MV_EbStanceOk(kDuckOnly, false));
    CHECK(!MV_EbStanceOk(kStandOnly, true) && MV_EbStanceOk(kStandOnly, false));
    CHECK(MV_EbJumpTick(false, false) == kEbJumpFree);
    CHECK(MV_EbJumpTick(false, true)  == kEbJumpFree);
    CHECK(MV_EbJumpTick(true,  false) == kEbJumpPending);
    CHECK(MV_EbJumpTick(true,  true)  == kEbJumpBlocked);   // landed ducked: CheckJumpButton says no
    CHECK(MV_EbJumpCheck(kAny, true));
    CHECK(!MV_EbJumpCheck(kAny, false));
    CHECK(!MV_EbJumpCheck(kDuckOnly, true));                 // the player asked for ducked
    CHECK(!MV_EbJumpCheck(kStandOnly, true));

    // ================= plan pick: standing beats a jump-eating ducked find =======================
    {
        // the user report: the ducked bug heads closer to the mouse, but it lands ducked on a
        // floor too close to unduck. The standing one must win anyway.
        const MvEbRank p[] = { { 1.f, true }, { 30.f, false } };
        CHECK(MV_EbPickPlan(p, 2) == 1);
        const MvEbRank q[] = { { 30.f, false }, { 1.f, true } };
        CHECK(MV_EbPickPlan(q, 2) == 0);
    }
    {
        // nothing standing: the ducked find still flies (fallback, never "no bug")
        const MvEbRank p[] = { { 5.f, true }, { 2.f, true } };
        CHECK(MV_EbPickPlan(p, 2) == 1);
        const MvEbRank one[] = { { 9.f, true } };
        CHECK(MV_EbPickPlan(one, 1) == 0);
    }
    {
        // equals: lowest heading error, botox's 0.5 deg margin keeps the first of near twins
        const MvEbRank p[] = { { 3.f, false }, { 2.7f, false }, { 1.f, false } };
        CHECK(MV_EbPickPlan(p, 3) == 2);
        const MvEbRank t[] = { { 3.f, false }, { 2.7f, false } };
        CHECK(MV_EbPickPlan(t, 2) == 0);
        CHECK(MV_EbPickPlan(t, 0) == -1);
    }
    // re-plan while flying
    CHECK(!MV_EbKeepPlan(true, 1.f, false, 50.f));   // flying the fallback: a standing bug swaps in
    CHECK(MV_EbKeepPlan(false, 50.f, true, 1.f));    // flying standing: never trade for a ducked eater
    CHECK(MV_EbKeepPlan(false, 10.f, false, 7.f));   // 3 deg better: inside the 4 deg margin, keep
    CHECK(!MV_EbKeepPlan(false, 10.f, false, 5.f));  // 5 deg better: swap

    std::printf("%s: %d/%d checks passed\n", s_failed ? "FAIL" : "PASS", s_checks - s_failed, s_checks);
    return s_failed ? 1 : 0;
}
