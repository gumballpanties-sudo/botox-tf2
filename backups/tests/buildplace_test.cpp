// Gate test for the build placement port (src/hacks/movement/buildplace.h) against an axis-aligned box
// world. Build + run:
//   cmake --build build --config Release --target buildplace_test && build\Release\buildplace_test.exe
// The trace sweeps a box hull (Minkowski-expanded boxes, slab test) and stops DIST_EPSILON short
// like the engine, the same box world fmslide_test.cpp uses.
#include "../src/hacks/movement/buildplace.h"
#include <cstdio>
#include <vector>
#include <algorithm>

struct V { float x = 0.f, y = 0.f, z = 0.f; };
static V MakeV(float x, float y, float z) { V v; v.x = x; v.y = y; v.z = z; return v; }

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)
static bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

struct Box { V mn, mx; };
static std::vector<Box> s_world;

// the SDK build boxes: collision box grown by 4 in x/y (tf_obj.cpp:895)
static const V k_sentryMins = MakeV(-24, -24, 0), k_sentryMaxs = MakeV(24, 24, 66);
static const V k_teleMins   = MakeV(-28, -28, 0), k_teleMaxs   = MakeV(28, 28, 12);
static constexpr float k_anchor = 18.f;   // what practice.inl passes: one step of slack

static BpHit<V> Sweep(const V& a, const V& b, const V& hMin, const V& hMax)
{
    BpHit<V> out; out.end = b;
    const float d[3] = { b.x - a.x, b.y - a.y, b.z - a.z };
    const float s[3] = { a.x, a.y, a.z };
    for (const Box& bx : s_world)
    {
        const float lo[3] = { bx.mn.x - hMax.x, bx.mn.y - hMax.y, bx.mn.z - hMax.z };
        const float hi[3] = { bx.mx.x - hMin.x, bx.mx.y - hMin.y, bx.mx.z - hMin.z };
        bool inside = true;
        for (int i = 0; i < 3; ++i) inside &= s[i] > lo[i] + 1e-3f && s[i] < hi[i] - 1e-3f;
        if (inside) { out.startSolid = true; out.frac = 0.f; out.end = a; return out; }

        float tEnter = -1.f, tExit = 1e9f; int axis = -1; float sign = 0.f;
        bool miss = false;
        for (int i = 0; i < 3 && !miss; ++i)
        {
            if (std::fabs(d[i]) < 1e-6f) { if (s[i] <= lo[i] || s[i] >= hi[i]) miss = true; continue; }
            float t0 = (lo[i] - s[i]) / d[i], t1 = (hi[i] - s[i]) / d[i];
            float sg = -1.f;
            if (t0 > t1) { std::swap(t0, t1); sg = 1.f; }
            if (t0 > tEnter) { tEnter = t0; axis = i; sign = sg; }
            tExit = (std::min)(tExit, t1);
        }
        if (miss || axis < 0 || tEnter > tExit || tEnter < -1e-4f || tEnter >= out.frac) continue;
        const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        out.frac = (std::max)(0.f, tEnter - 0.03125f / len);
        out.n = V{}; (axis == 0 ? out.n.x : axis == 1 ? out.n.y : out.n.z) = sign;
    }
    out.end = MakeV(a.x + d[0] * out.frac, a.y + d[1] * out.frac, a.z + d[2] * out.frac);
    return out;
}

static BpHit<V> Hull(const V& a, const V& b, const V& mn, const V& mx) { return Sweep(a, b, mn, mx); }
static BpHit<V> Line(const V& a, const V& b) { return Sweep(a, b, V{}, V{}); }

static BpResult Place(const V& aim, const V& bMins, const V& bMaxs, V& org)
{
    V center;
    return BpCalcPlacementPos(Hull, Line, aim, k_anchor, bMins, bMaxs, org, center);
}

// ---- the ground sweep ---------------------------------------------------------------------

static void TestFlatFloor()
{
    s_world = { { MakeV(-500, -500, -100), MakeV(500, 500, 0) } };
    V org;
    CHECK(Place(MakeV(0, 0, 2), k_sentryMins, k_sentryMaxs, org) == BP_OK);
    CHECK(Near(org.x, 0.f, 1e-3f) && Near(org.y, 0.f, 1e-3f));
    CHECK(Near(org.z, 0.f, 0.05f));            // stands on the floor, not floating at the aim point
}

static void TestNoGround()
{
    s_world.clear();
    V org;
    CHECK(Place(MakeV(0, 0, 2), k_sentryMins, k_sentryMaxs, org) == BP_NO_GROUND);
}

static void TestLowCeilingRefused()
{
    // 50u of headroom: a 66u sentry does not fit, a 12u teleporter does
    s_world = { { MakeV(-500, -500, -100), MakeV(500, 500, 0) },
                { MakeV(-500, -500, 50),   MakeV(500, 500, 120) } };
    V org;
    CHECK(Place(MakeV(0, 0, 2), k_sentryMins, k_sentryMaxs, org) == BP_NO_ROOM);
    CHECK(Place(MakeV(0, 0, 2), k_teleMins, k_teleMaxs, org) == BP_OK);
    CHECK(Near(org.z, 0.f, 0.05f));
}

static void TestHighCeilingOk()
{
    s_world = { { MakeV(-500, -500, -100), MakeV(500, 500, 0) },
                { MakeV(-500, -500, 140),  MakeV(500, 500, 200) } };
    V org;
    CHECK(Place(MakeV(0, 0, 2), k_sentryMins, k_sentryMaxs, org) == BP_OK);
    CHECK(Near(org.z, 0.f, 0.05f));
}

static void TestTightCeilingRefusedLikeTheGame()
{
    // 80u of headroom over a 66u sentry, and the game still says no. That is the SDK's search,
    // not a bug here: it starts the sweep a whole box height above the anchor and steps down in
    // 8 (flBoxTopZ - flBoxBottomZ)/7 jumps, so the first start that clears the ceiling is already
    // too low to leave 66u. Real TF2 refuses sentries under low vents and stairs for this reason.
    s_world = { { MakeV(-500, -500, -100), MakeV(500, 500, 0) },
                { MakeV(-500, -500, 80),   MakeV(500, 500, 120) } };
    V org;
    CHECK(Place(MakeV(0, 0, 2), k_sentryMins, k_sentryMaxs, org) == BP_NO_ROOM);
    CHECK(Place(MakeV(0, 0, 2), k_teleMins, k_teleMaxs, org) == BP_OK);   // 12u tall, no trouble
}

static void TestInsideSolidRefused()
{
    // aiming into the middle of a block: every sweep in the window starts in solid
    s_world = { { MakeV(-500, -500, -100), MakeV(500, 500, 200) } };
    V org;
    const BpResult r = Place(MakeV(0, 0, 100), k_sentryMins, k_sentryMaxs, org);
    CHECK(r == BP_NO_ROOM || r == BP_NO_GROUND);
}

static void TestStandsOnTheTableNotTheFloor()
{
    // a 40u table inside a room: aim at its top and the building stands on the table
    s_world = { { MakeV(-500, -500, -100), MakeV(500, 500, 0) },
                { MakeV(-60, -60, 0),      MakeV(60, 60, 40) } };
    V org;
    CHECK(Place(MakeV(0, 0, 42), k_sentryMins, k_sentryMaxs, org) == BP_OK);
    CHECK(Near(org.z, 40.f, 0.05f));
}

// ---- VerifyCorner -------------------------------------------------------------------------

static void TestOverTheEdgeRefused()
{
    // floor ends at x = 0; a sentry aimed 10u short of the edge hangs 14u of its base over it
    s_world = { { MakeV(-500, -500, -100), MakeV(0, 500, 0) } };
    V org;
    CHECK(Place(MakeV(-10, 0, 2), k_sentryMins, k_sentryMaxs, org) == BP_SLOPE);
    CHECK(Place(MakeV(-30, 0, 2), k_sentryMins, k_sentryMaxs, org) == BP_OK);   // fully on
}

static void TestSteepCornerRefused()
{
    // the box world is axis aligned, so feed VerifyCorner the normal directly: 45 deg is
    // dot 0.707 (allowed), 60 deg off vertical is 0.5 (refused)
    auto steep = [](const V&, const V&) { BpHit<V> h; h.frac = 0.5f; h.n = MakeV(0.866f, 0.f, 0.5f); return h; };
    auto ramp  = [](const V&, const V&) { BpHit<V> h; h.frac = 0.5f; h.n = MakeV(0.707f, 0.f, 0.707f); return h; };
    auto air   = [](const V&, const V&) { BpHit<V> h; return h; };                       // frac 1, nothing
    auto solid = [](const V&, const V&) { BpHit<V> h; h.startSolid = true; h.frac = 0.f; return h; };
    CHECK(!BpVerifyCorner(steep, MakeV(0, 0, 0), 0.f, 0.f));
    CHECK(BpVerifyCorner(ramp, MakeV(0, 0, 0), 0.f, 0.f));
    CHECK(!BpVerifyCorner(air, MakeV(0, 0, 0), 0.f, 0.f));
    CHECK(!BpVerifyCorner(solid, MakeV(0, 0, 0), 0.f, 0.f));
}

static void TestStepDownCornerRefused()
{
    // a 40u drop under one corner: further than TF_OBJ_GROUND_CLEARANCE (32), so no ground there
    s_world = { { MakeV(-500, -500, -100), MakeV(0, 500, 0) },
                { MakeV(0, -500, -100),    MakeV(500, 500, -40) } };
    V org;
    CHECK(Place(MakeV(-10, 0, 2), k_sentryMins, k_sentryMaxs, org) == BP_SLOPE);
}

// ---- the box tests the entity scan runs ----------------------------------------------------

static void TestObjectOverlap()
{
    // IsPlacementPosValid's box is 24u out in x/y; a sentry's own AABB is 20u, so two sentries
    // closer than 44u in x collide and 45u apart do not
    V tmn, tmx;
    BpObjectTestBox(MakeV(0, 0, 0), tmn, tmx);
    CHECK(Near(tmn.z, -32.f, 1e-3f) && Near(tmx.z, 24.f, 1e-3f));

    auto sentryAt = [](float x, V& mn, V& mx) {
        mn = MakeV(x - 20.f, -20.f, 0.f); mx = MakeV(x + 20.f, 20.f, 66.f);
    };
    V mn, mx;
    sentryAt(43.f, mn, mx);  CHECK(BpBoxesOverlap(tmn, tmx, mn, mx));
    sentryAt(45.f, mn, mx);  CHECK(!BpBoxesOverlap(tmn, tmx, mn, mx));
    // straight below: the box reaches 32u down and a sentry is 66u tall, so one standing 90u
    // under still pokes into it (this is the stairs case flBoxDepth is there for) and one a
    // whole storey down (128u) does not
    sentryAt(0.f, mn, mx);   mn.z -= 90.f;  mx.z -= 90.f;   CHECK(BpBoxesOverlap(tmn, tmx, mn, mx));
    sentryAt(0.f, mn, mx);   mn.z -= 128.f; mx.z -= 128.f;  CHECK(!BpBoxesOverlap(tmn, tmx, mn, mx));
}

static void TestPointInRoom()
{
    const V mn = MakeV(-100, -100, 0), mx = MakeV(100, 100, 128);
    CHECK(BpPointIn(MakeV(0, 0, 64), mn, mx));
    CHECK(BpPointIn(MakeV(-100, 100, 0), mn, mx));     // on the face counts, like the engine's test
    CHECK(!BpPointIn(MakeV(0, 0, 129), mn, mx));
    CHECK(!BpPointIn(MakeV(101, 0, 64), mn, mx));
}

static void TestSegmentCrossesVisualizer()
{
    const V mn = MakeV(-8, -100, 0), mx = MakeV(8, 100, 128);   // a door-shaped brush at x = 0
    CHECK(BpSegHitsBox(MakeV(-200, 0, 40), MakeV(200, 0, 40), mn, mx));    // straight through
    CHECK(!BpSegHitsBox(MakeV(-200, 0, 40), MakeV(-20, 0, 40), mn, mx));   // stops short
    CHECK(!BpSegHitsBox(MakeV(-200, 0, 200), MakeV(200, 0, 200), mn, mx)); // over the top
    CHECK(BpSegHitsBox(MakeV(0, 0, 40), MakeV(200, 0, 40), mn, mx));       // starts inside
    CHECK(!BpSegHitsBox(MakeV(-200, 300, 40), MakeV(200, 300, 40), mn, mx)); // parallel, past it
}

int main()
{
    TestFlatFloor();
    TestNoGround();
    TestLowCeilingRefused();
    TestHighCeilingOk();
    TestTightCeilingRefusedLikeTheGame();
    TestInsideSolidRefused();
    TestStandsOnTheTableNotTheFloor();
    TestOverTheEdgeRefused();
    TestSteepCornerRefused();
    TestStepDownCornerRefused();
    TestObjectOverlap();
    TestPointInRoom();
    TestSegmentCrossesVisualizer();

    std::printf("%s: %d checks, %d failed\n", s_failed ? "FAILED" : "PASS", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
