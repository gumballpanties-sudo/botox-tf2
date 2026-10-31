// Gate test for the pixel surf INDICATOR rule (src/hacks/movement/ps_surf.h ps::IsSurf) -- the question
// "is a surf actually happening", as opposed to "is vz pinned at -halfgrav", which is what the tag
// used to light on and what an air stuck, a texture bug's crease catch and a hull wedged on a
// crate all read too.
//
// The split under test is TRAVEL: a surf slides along the face (the log's own ride carried
// xy=356 u/s with vin=-0.00), a wedge is nailed in place by the crease that zeroed its velocity.
// The numbers below are distances in units from where the pin began, at the tick rate the game
// runs (0.015s): 356 u/s carries 5.4u per tick, 90 u/s carries 1.4u.
//
// Build + run: cmake --build build --config Release --target pssurf_test && build\Release\pssurf_test.exe
#include "../src/hacks/movement/ps_surf.h"
#include <cstdio>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

// the ride as PixelSurf publishes it: pinned, flush against a brush face, travel measured
static ps::SurfEvidence Ride(float travel)
{
    ps::SurfEvidence e;
    e.riding = true;
    e.wall   = true;
    e.gap    = 0.f;
    e.travel = travel;
    return e;
}

int main()
{
    // ---- the real thing ----------------------------------------------------------------
    // the ride out of the log: 356 u/s along the face, so two ticks of pin carry 10.7u
    CHECK(ps::IsSurf(Ride(10.7f)));
    // a slow ride still gets there, it just takes more ticks: 90 u/s, 3 ticks
    CHECK(ps::IsSurf(Ride(4.2f)));
    // parked ON the surf: the park brake stops the hull, but travel is measured from where the
    // pin BEGAN, so a braked ride keeps the tag lit instead of dropping it the tick it stops
    CHECK(ps::IsSurf(Ride(60.f)));
    // flush-ish rather than dead flush (a corner) still counts
    {
        ps::SurfEvidence e = Ride(10.7f);
        e.gap = ps::k_gapMax;
        CHECK(ps::IsSurf(e));
    }

    // ---- what used to light the tag and must not ----------------------------------------
    // AIR STUCK: the crease zeroed the velocity, the hull has not moved since. AirStuck's own
    // surf-yield calls anything under 0.01u a frozen origin
    CHECK(!ps::IsSurf(Ride(0.f)));
    CHECK(!ps::IsSurf(Ride(0.01f)));
    // TEXTURE BUG: same, and it is dark even in the first ticks before the hull settles
    CHECK(!ps::IsSurf(Ride(ps::k_travelMin - 0.1f)));
    CHECK(ps::IsSurf(Ride(ps::k_travelMin)));   // ...and the threshold itself passes

    // ...and whichever of the two it is, a tick THEY landed a pin on is theirs, however far the
    // hull carried into it
    {
        ps::SurfEvidence e = Ride(40.f);
        e.otherOwns = true;
        CHECK(!ps::IsSurf(e));
    }

    // FREE FALL beside a wall: vz grazes -halfgrav for a tick, the ride never sustains -- and a
    // fall carries plenty of distance, so travel alone would have lit it
    {
        ps::SurfEvidence e = Ride(40.f);
        e.riding = false;
        CHECK(!ps::IsSurf(e));
    }

    // the HUNT'S LATCH on its own is not a ride: with nothing pinned there is no evidence at all
    CHECK(!ps::IsSurf(ps::SurfEvidence{}));

    // no brush face in reach (displacement, ramp, trigger, prop -- ProbeFace refuses them all)
    {
        ps::SurfEvidence e = Ride(10.7f);
        e.wall = false;
        CHECK(!ps::IsSurf(e));
    }

    // a wall across the way is not the wall we are riding. Travelling PAST one while something
    // else holds the pin is exactly this case
    {
        ps::SurfEvidence e = Ride(10.7f);
        e.gap = ps::k_gapMax + 0.1f;
        CHECK(!ps::IsSurf(e));
    }

    std::printf(s_failed ? "pssurf_test: %d/%d FAILED\n" : "pssurf_test: %d/%d ok\n",
                s_failed ? s_failed : s_checks, s_checks);
    return s_failed ? 1 : 0;
}
