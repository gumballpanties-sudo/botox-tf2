// Gate test for fake movement anti-snag (src/hacks/movement/fm_slide.h) against an axis-aligned box world.
// Build + run:  cmake --build build --config Release --target fmslide_test && build\Release\fmslide_test.exe
// The trace sweeps the player hull (Minkowski-expanded boxes, slab test) and stops DIST_EPSILON short
// like the engine. "post" = where engine air TryPlayerMove left us (stuck = pre).
#include "../src/hacks/movement/fm_slide.h"
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
static const V k_hMin = MakeV(-24, -24, 0), k_hMax = MakeV(24, 24, 82);   // standing hull

static FmHit<V> TraceTop(const V& a, const V& b, float top);
static FmHit<V> Trace(const V& a, const V& b) { return TraceTop(a, b, k_hMax.z); }

static FmHit<V> TraceTop(const V& a, const V& b, float top)
{
    const V k_hMax = MakeV(24, 24, top);   // hull height per call: 82 standing, 62 ducked
    FmHit<V> out; out.end = b;
    const float d[3] = { b.x - a.x, b.y - a.y, b.z - a.z };
    const float s[3] = { a.x, a.y, a.z };
    for (const Box& bx : s_world)
    {
        const float lo[3] = { bx.mn.x - k_hMax.x, bx.mn.y - k_hMax.y, bx.mn.z - k_hMax.z };
        const float hi[3] = { bx.mx.x - k_hMin.x, bx.mx.y - k_hMin.y, bx.mx.z - k_hMin.z };
        bool inside = true;
        for (int i = 0; i < 3; ++i) inside &= s[i] > lo[i] + 1e-3f && s[i] < hi[i] - 1e-3f;
        if (inside) { out.startSolid = true; return out; }

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

static constexpr float k_dt = 0.015f, k_step = 18.f;

static void TestLipStepsUp()
{
    s_world = { { MakeV(100, -500, -1000), MakeV(200, 500, 10) } };   // 10u lip, face at x=100
    const V pre = MakeV(75, 0, 0), vel = MakeV(300, 0, 0);
    V org, ov;
    CHECK(FmUnsnag(Trace, pre, vel, k_dt, k_step, MakeV(75.97f, 0, 0), org, ov));
    CHECK(Near(org.x, 79.5f, 0.05f));
    CHECK(Near(org.z, 10.f, 0.05f));
    CHECK(Near(ov.x, 300.f, 1e-3f));
}

static void TestTallWallStays()
{
    s_world = { { MakeV(100, -500, -1000), MakeV(200, 500, 1000) } };
    const V pre = MakeV(75.97f, 0, 0), vel = MakeV(300, 0, 0);
    V org, ov;
    CHECK(!FmUnsnag(Trace, pre, vel, k_dt, k_step, pre, org, ov));
}

static void TestLipUnderCeilingStays()
{
    s_world = { { MakeV(100, -500, -1000), MakeV(200, 500, 10) },
                { MakeV(-500, -500, 90), MakeV(500, 500, 200) } };   // 8u of headroom
    const V pre = MakeV(75.97f, 0, 0), vel = MakeV(300, 0, 0);
    V org, ov;
    CHECK(!FmUnsnag(Trace, pre, vel, k_dt, k_step, pre, org, ov));
}

static void TestSeamSlides()
{
    // two coplanar brushes, face at y=30, player touching it strafing slightly into it;
    // engine snagged on the seam at x=100 (post == pre)
    s_world = { { MakeV(-500, 30, -1000), MakeV(100, 500, 1000) },
                { MakeV(100, 30, -1000), MakeV(600, 500, 1000) } };
    const V pre = MakeV(98, 5.97f, 0), vel = MakeV(300, 40, 0);
    V org, ov;
    CHECK(FmUnsnag(Trace, pre, vel, k_dt, k_step, pre, org, ov));
    CHECK(Near(org.x, 102.5f, 0.05f));
    CHECK(org.y < 6.f && org.y > 5.9f);            // still outside the face (hull edge y=30)
    CHECK(Near(org.z, 0.f, 1e-3f));                 // flat slide, no pointless step
    CHECK(Near(ov.x, 300.f, 1e-3f) && Near(ov.y, 0.f, 1e-3f));
}

static void TestCornerCreaseStays()
{
    // inner corner, moving diagonally into both faces: nowhere to go, must not invent motion
    s_world = { { MakeV(100, -500, -1000), MakeV(200, 500, 1000) },
                { MakeV(-500, 30, -1000), MakeV(200, 500, 1000) } };
    const V pre = MakeV(75.97f, 5.97f, 0), vel = MakeV(300, 300, 0);
    V org, ov;
    CHECK(!FmUnsnag(Trace, pre, vel, k_dt, k_step, pre, org, ov));
}

static void TestEngineFineUntouched()
{
    s_world = {};
    const V pre = MakeV(0, 0, 0), vel = MakeV(300, 0, 0);
    V org, ov;
    CHECK(!FmUnsnag(Trace, pre, vel, k_dt, k_step, MakeV(4.5f, 0, 0), org, ov));
    CHECK(!FmUnsnag(Trace, pre, MakeV(5, 0, 0), k_dt, k_step, pre, org, ov));   // too slow to judge
}

static void TestStartSolidStays()
{
    s_world = { { MakeV(-10, -10, -10), MakeV(10, 10, 10) } };
    const V pre = MakeV(0, 0, 0), vel = MakeV(300, 0, 0);
    V org, ov;
    CHECK(!FmUnsnag(Trace, pre, vel, k_dt, k_step, pre, org, ov));
}

static void TestOuterCornerWraps()
{
    // riding along a wall face (y=30) to its end at x=100, then wall turns away: slide runs on
    s_world = { { MakeV(-500, 30, -1000), MakeV(100, 500, 1000) } };
    const V pre = MakeV(120, 5.97f, 0), vel = MakeV(300, 60, 0);   // hull past the end, slight drift in
    V org, ov;
    // engine already free here: not a snag, leave it
    CHECK(!FmUnsnag(Trace, pre, vel, k_dt, k_step, MakeV(124.5f, 6.87f, 0), org, ov));
}

int main()
{
    TestLipStepsUp();
    TestTallWallStays();
    TestLipUnderCeilingStays();
    TestSeamSlides();
    TestCornerCreaseStays();
    TestEngineFineUntouched();
    TestStartSolidStays();
    TestOuterCornerWraps();
    std::printf("%s: %d/%d checks\n", s_failed ? "FAILED" : "ok", s_checks - s_failed, s_checks);
    return s_failed ? 1 : 0;
}
