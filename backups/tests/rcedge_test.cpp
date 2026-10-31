// Gate test for the edgebug marker's z (src/hacks/movement/rc_edge.h rce::NearestEdge): the walk that
// finds the nearest lip of the floor a marker sits on, so an edgebug row reports the height you
// slide off at instead of the height you managed to click.
//
// Floors here are functions, not brushes: flat tops that end in void, a 45 deg slanted brush that
// ends in void (the case this exists for -- the click is short of the lip and its z is wrong by
// the slope), a step down inside a step (walkable, not an edge), a wall (a rise, never an edge),
// and a marker over nothing.
//
// Build + run: cmake --build build --config Release --target rcedge_test && build\Release\rcedge_test.exe
#include "../src/hacks/movement/rc_edge.h"
#include <cstdio>
#include <cmath>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)
#define NEAR(a, b, eps) do { ++s_checks; if (!(std::fabs((a) - (b)) <= (eps))) { \
    std::printf("FAIL line %d: %s = %.4f, want %.4f +-%.3f\n", __LINE__, #a, (double)(a), (double)(b), (double)(eps)); ++s_failed; } } while (0)

// a floor sampler in the shape rce::NearestEdge wants: kFloor / kVoid / kBlocked. The height
// functions below answer the first two; the wall case answers the third itself
template <class H>
static auto Floor(H height)
{
    return [height](float x, float y, float zRef, float& z) -> int
    { return height(x, y, zRef, z) ? rce::kFloor : rce::kVoid; };
}

int main()
{
    // ---- 1. FLAT TOP THAT ENDS AT x = 100 ------------------------------------------------
    // a table top at z = 64, void past its east edge. A marker 10u short of it keeps its xy, and
    // its z is the top's -- nothing about a flat floor moves, which is the control
    {
        auto top = [](float x, float, float, float& z) { if (x > 100.f) return false; z = 64.f; return true; };
        rce::Edge e{};
        CHECK(rce::NearestEdge(90.f, 0.f, 64.f, Floor(top), e));
        NEAR(e.z, 64.f, 0.001f);
        NEAR(e.dist, 10.f, 0.05f);      // the lip is 10u east, bisected onto it
        NEAR(e.x, 100.f, 0.05f);
        NEAR(e.y, 0.f, 0.05f);
    }

    // ---- 2. THE WHOLE POINT: A SLANTED BRUSH ----------------------------------------------
    // a 45 deg ramp falling east, z = 100 - x, ending in void at x = 100 (so the lip is z = 0).
    // Clicking 8u short of the lip stores z = 8: eight units of pure error on the one number the
    // edgebug is decided by. The walk gives back the lip's 0
    {
        auto ramp = [](float x, float, float, float& z) { if (x > 100.f) return false; z = 100.f - x; return true; };
        rce::Edge e{};
        CHECK(rce::NearestEdge(92.f, 0.f, 8.f, Floor(ramp), e));
        NEAR(e.dist, 8.f, 0.05f);
        NEAR(e.z, 0.f, 0.1f);           // the lip, not the 8.0 that was clicked
        NEAR(e.x, 100.f, 0.05f);
    }

    // ---- 3. NEAREST WINS, AND IT IS THE NEAREST IN ANY DIRECTION --------------------------
    // the same ramp with a second edge to the north at y = 10: a marker 30u from the east lip and
    // 4u from the north one takes the north one's z (the ramp is flat across y, so z = 100 - x)
    {
        auto ramp = [](float x, float y, float, float& z)
        { if (x > 100.f || y > 10.f) return false; z = 100.f - x; return true; };
        rce::Edge e{};
        CHECK(rce::NearestEdge(70.f, 6.f, 30.f, Floor(ramp), e));
        NEAR(e.dist, 4.f, 0.2f);
        NEAR(e.y, 10.f, 0.2f);
        NEAR(e.z, 30.f, 0.3f);
    }

    // ---- 4. A DROP TALLER THAN A STEP IS AN EDGE, A SMALLER ONE IS NOT ---------------------
    // a 24u drop at x = 50: past a step height, so it is a ledge you slide off and the lip's z is
    // the top's 64
    {
        auto twoTier = [](float x, float, float, float& z) { z = x > 50.f ? 40.f : 64.f; return true; };
        rce::Edge e{};
        CHECK(rce::NearestEdge(44.f, 0.f, 64.f, Floor(twoTier), e));
        NEAR(e.dist, 6.f, 0.05f);
        NEAR(e.z, 64.f, 0.001f);
    }
    // ...and the same shape with a 12u drop is a step the engine walks down: no edge inside 64u
    {
        auto stepDown = [](float x, float, float, float& z) { z = x > 50.f ? 52.f : 64.f; return true; };
        rce::Edge e{};
        CHECK(!rce::NearestEdge(44.f, 0.f, 64.f, Floor(stepDown), e));
    }

    // ---- 5. A WALL IS NEVER AN EDGE, WHICHEVER WAY THE SAMPLER SEES IT --------------------
    // In the engine the ray starts a step over the last floor, so a wall standing ON that floor
    // swallows it and comes back startsolid, not "no floor here" -- read as void, the base of
    // every wall in the map would be a lip. kBlocked is that answer, and it ends the ray
    {
        auto walled = [](float x, float, float, float& z) -> int
        { if (x > 50.f) return rce::kBlocked; z = 64.f; return rce::kFloor; };
        rce::Edge e{};
        CHECK(!rce::NearestEdge(44.f, 0.f, 64.f, walled, e));
    }
    // ...and the same wall with the floor really ending 20u the other way: the west lip is the
    // answer, the wall is still not one
    {
        auto walled = [](float x, float, float, float& z) -> int
        { if (x > 50.f) return rce::kBlocked; if (x < 24.f) return rce::kVoid; z = 64.f; return rce::kFloor; };
        rce::Edge e{};
        CHECK(rce::NearestEdge(44.f, 0.f, 64.f, walled, e));
        NEAR(e.x, 24.f, 0.2f);
        NEAR(e.z, 64.f, 0.001f);
    }

    // ---- 6. A RISE IS NOT AN EDGE EITHER -------------------------------------------------
    // floor at 64 up to x = 50, then a 40u block on top of it. The ray east dies on the block
    // instead of calling its base an edge; the floor is open the other way, so the answer is the
    // west lip at x = -64 ... which is out of reach, so: nothing found
    {
        auto wall = [](float x, float, float, float& z) { z = x > 50.f ? 104.f : 64.f; return true; };
        rce::Edge e{};
        CHECK(!rce::NearestEdge(44.f, 0.f, 64.f, Floor(wall), e));
    }
    // the same wall with real void 20u to the west: the west lip is the answer, the wall is still
    // not one
    {
        auto wall = [](float x, float, float, float& z)
        { if (x < 24.f) return false; z = x > 50.f ? 104.f : 64.f; return true; };
        rce::Edge e{};
        CHECK(rce::NearestEdge(44.f, 0.f, 64.f, Floor(wall), e));
        NEAR(e.x, 24.f, 0.2f);
        NEAR(e.z, 64.f, 0.001f);
    }

    // ---- 7. A MARKER OVER NOTHING -------------------------------------------------------
    // no floor under the marker at all: no answer, and the caller keeps the z that was placed
    {
        auto none = [](float, float, float, float&) { return false; };
        rce::Edge e{};
        CHECK(!rce::NearestEdge(0.f, 0.f, 0.f, Floor(none), e));
    }

    // ---- 8. OUT OF REACH IS NOT AN EDGE --------------------------------------------------
    // a marker 200u in from the lip of a big flat roof: past k_max, so nothing is offered rather
    // than a lip from across the map
    {
        auto roof = [](float x, float, float, float& z) { if (x > 100.f) return false; z = 64.f; return true; };
        rce::Edge e{};
        CHECK(!rce::NearestEdge(-100.f, 0.f, 64.f, Floor(roof), e));
    }

    // ---- 9. THE RAY FOLLOWS A LONG RAMP, IT DOES NOT CUT OUT ON IT ------------------------
    // a 45 deg ramp is 64u of z over the 64u the walk reaches: sampling against the ORIGINAL
    // height instead of the last sample's would call the ramp void at the first sample past a
    // step and report a lip 18u out that is not there. The lip is the ramp's real end
    {
        auto longRamp = [](float x, float, float, float& z) { if (x > 60.f) return false; z = 100.f - x; return true; };
        rce::Edge e{};
        CHECK(rce::NearestEdge(0.f, 0.f, 100.f, Floor(longRamp), e));
        NEAR(e.dist, 60.f, 0.1f);
        NEAR(e.z, 40.f, 0.2f);
    }

    // ---- 10. THE BISECTION IS WHAT MAKES THE NUMBER WORTH HAVING ---------------------------
    // a lip at a non-sample distance (11.3u) on a 45 deg ramp: sample spacing alone would answer
    // z = 90 (the 10u sample), which is 1.3u of error on a pixel-level number
    {
        auto ramp = [](float x, float, float, float& z) { if (x > 11.3f) return false; z = 100.f - x; return true; };
        rce::Edge e{};
        CHECK(rce::NearestEdge(0.f, 0.f, 100.f, Floor(ramp), e));
        NEAR(e.dist, 11.3f, rce::k_step / (1 << rce::k_bisect) + 0.001f);
        NEAR(e.z, 88.7f, 0.05f);
    }

    std::printf("%s -- %d checks, %d failed\n", s_failed ? "FAILED" : "PASSED", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
