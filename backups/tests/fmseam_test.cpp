// Gate test for the fake movement seam gate (src/hacks/movement/fm_seam.h): box brushes + one wedge,
// window = hull box at the wall contact pushed 1u into the wall, like fakemove.inl FM_Seam.
// Build + run:  cmake --build build --config Release --target fmseam_test && build\Release\fmseam_test.exe
#include "../src/hacks/movement/fm_seam.h"
#include <cstdio>
#include <vector>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

using Brush = std::vector<FmPlane>;

// engine box brush order (enginetrace GetBrushInfo IsBox): +x +y +z maxs, -x -y -z -mins
static Brush Box(float x0, float y0, float z0, float x1, float y1, float z1)
{
    return { { 1, 0, 0, x1 }, { 0, 1, 0, y1 }, { 0, 0, 1, z1 },
             { -1, 0, 0, -x0 }, { 0, -1, 0, -y0 }, { 0, 0, -1, -z0 } };
}

// how many brushes count for a player hull (48x48x62 duck) touching the wall face x = 100 (normal -x).
// `top` = how far up the hull the window reaches: 62 = whole hull (wall jump), 2 = the foot slab
// the wall surf passes (fakemove.inl k_fmFoot), z = the hull's BOTTOM either way.
static int Count(const std::vector<Brush>& world, float y, float z, float slack = 0.f, float top = 62.f,
                 float hx = -1, float hy = 0, float hz = 0, float hd = -100)
{
    const float pad = 1.f + slack;
    const float cx = 100.f - 24.f;   // hull center flush with the wall
    const float mn[3] = { cx - 24 - pad, y - 24 - pad, z - pad };
    const float mx[3] = { cx + 24 + pad, y + 24 + pad, z + top + pad };
    int n = 0;
    for (const Brush& b : world)
        if (FmSeam_BrushCounts(b.data(), (int)b.size(), mn, mx, hx, hy, hz, hd)) ++n;
    return n;
}

// head surf: counting brush bottom nearest the top for a hull at (x, y) with its top at `top`,
// scanned the way fakemove.inl FM_BottomEdge does (window top-1-down .. top+1+up, sliver 4).
// pad 2 = cfg wall reach. Normal of the pick in s_nx/s_ny.
static float s_nx = 0.f, s_ny = 0.f;
static bool Edge(const std::vector<Brush>& world, float x, float y, float top, float& bz,
                 float pad = 2.f, float up = 18.f, float down = 0.f)
{
    const float hmn[2] = { x - 24, y - 24 }, hmx[2] = { x + 24, y + 24 };
    bool found = false;
    for (const Brush& b : world)
    {
        float z = 0.f, nx = 0.f, ny = 0.f;
        if (FmSeam_BottomEdge(b.data(), (int)b.size(), hmn, hmx, pad, 4.f, top - 1.f - down, top + 1.f + up,
                              z, nx, ny)
            && (!found || std::fabs(z - top) < std::fabs(bz - top)))
        { bz = z; s_nx = nx; s_ny = ny; found = true; }
    }
    return found;
}

static bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

static void TestBottomEdge()
{
    float bz = 0.f;
    const float flush = 100.f - 24.f - 0.03125f;   // hull against the wall face x = 100

    // wall of two stacked brushes, seam at z = 64: upper brush's bottom rides
    std::vector<Brush> stack = { Box(100, -200, 0, 116, 200, 64), Box(100, -200, 64, 116, 200, 200) };
    CHECK(Edge(stack, flush, 0, 63.75f, bz) && bz == 64.f);   // riding: top 0.25 under
    CHECK(Near(s_nx, -1.f) && Near(s_ny, 0.f));               // edge faces the hull: out of the wall
    CHECK(Edge(stack, flush, 0, 63.75f, bz, 2.f, 0.f));       // snap 0 still holds the ride
    CHECK(!Edge(stack, flush, 0, 60.f, bz, 2.f, 0.f));        // snap 0: 4 under never pulls up
    CHECK(Edge(stack, flush, 0, 50.f, bz) && bz == 64.f);     // 14 under, snap 18 pulls up
    CHECK(!Edge(stack, flush, 0, 40.f, bz));                  // 24 under: out of snap
    CHECK(Edge(stack, flush, 0, 64.9f, bz));                  // just past it: still in the 1u slack
    CHECK(!Edge(stack, flush, 0, 65.5f, bz));                 // risen past the seam
    CHECK(!Edge(stack, flush - 5.f, 0, 63.75f, bz));          // 5u off the wall, reach 2
    CHECK(Edge(stack, flush - 5.f, 0, 63.75f, bz, 8.f));      // reach 8 gets it
    CHECK(Edge(stack, flush, 223, 63.75f, bz));               // hull past the brush end by 1u: pad keeps it
    CHECK(!Edge(stack, flush, 227, 63.75f, bz));              // 3u past: ride ends with the brush

    // lower brush alone: a brush TOP at the hull top is a pixel surf, not a head surf
    std::vector<Brush> lower = { Box(100, -200, 0, 116, 200, 64) };
    CHECK(!Edge(lower, flush, 0, 63.75f, bz));

    // flat ceiling over the hull: never rides, open room or pushed into the wall under it
    std::vector<Brush> ceil = { Box(-500, -500, 64, 500, 500, 100) };
    CHECK(!Edge(ceil, 0, 0, 63.75f, bz));
    std::vector<Brush> ceilWall = { Box(-500, -500, 64, 116, 500, 100), Box(100, -500, 0, 116, 500, 64) };
    CHECK(!Edge(ceilWall, flush, 0, 63.75f, bz));
    // two-brush ceiling, hull straddling its seam: still a ceiling
    std::vector<Brush> ceil2 = { Box(-500, -500, 64, 0, 500, 100), Box(0, -500, 64, 500, 500, 100) };
    CHECK(!Edge(ceil2, 0, 0, 63.75f, bz));

    // overhang lip ending at x = 55, hull x = 76 -> footprint 52..100
    std::vector<Brush> lip3 = { Box(-500, -500, 100, 55, 500, 200) };
    CHECK(Edge(lip3, 76, 0, 99.75f, bz) && bz == 100.f);     // 3u under: sliver
    CHECK(Near(s_nx, 1.f) && Near(s_ny, 0.f));                // lip's +x face
    CHECK(Edge(lip3, 80, 0, 99.75f, bz));                     // 1u out, reach 2
    CHECK(!Edge(lip3, 81, 0, 99.75f, bz));                    // 2u out: grown box only touches
    std::vector<Brush> lip8 = { Box(-500, -500, 100, 60, 500, 200) };
    CHECK(!Edge(lip8, 76, 0, 99.75f, bz));                    // 8u under: that is a ceiling
    // corner of an overhang: 3u x 5u patch over the hull
    std::vector<Brush> corner = { Box(-500, -500, 100, 55, -19, 200) };
    CHECK(Edge(corner, 76, 0, 99.75f, bz));
    CHECK(Near(s_nx, 1.f) && Near(s_ny, 0.f));                // hull is further out in x (21) than y (19)

    // two edges in reach: the one nearest the top wins
    std::vector<Brush> two = { Box(-500, -500, 100, 55, 500, 200), Box(100, -500, 110, 116, 500, 200) };
    CHECK(Edge(two, 76, 0, 99.75f, bz) && bz == 100.f);
    CHECK(Edge(two, 76, 0, 108.f, bz) && bz == 110.f);

    // fast fall: 3500 u/s = 52.5u this tick. Top at 110 would cross the seam at 64 -> caught first
    CHECK(!Edge(stack, flush, 0, 110.f, bz));                 // no fall window: already above, missed
    CHECK(Edge(stack, flush, 0, 110.f, bz, 2.f, 18.f, 52.5f) && bz == 64.f);
    CHECK(!Edge(stack, flush, 0, 130.f, bz, 2.f, 18.f, 52.5f)); // won't reach it this tick
    // wall of brushes every 16u: the first seam the top falls past, not the furthest
    std::vector<Brush> bricks = { Box(100, -200, 0, 116, 200, 16), Box(100, -200, 16, 116, 200, 32),
                                  Box(100, -200, 32, 116, 200, 48), Box(100, -200, 48, 116, 200, 64),
                                  Box(100, -200, 64, 116, 200, 200) };
    CHECK(Edge(bricks, flush, 0, 70.f, bz, 2.f, 0.f, 52.5f) && bz == 64.f);
}

static void TestStick()
{
    // lip outward normal +x (lip is at -x of the hull)
    float vx = -30.f, vy = 200.f;
    // strafing into the lip: the move and speed into it are gone, along stays
    CHECK(Near(FmSeam_Stick(1, 0, -450, 0, -0.45f, 3.f, vx, vy), -0.45f) && Near(vx, 0.f) && Near(vy, 200.f));
    // into with the wish away: into still never happens
    vx = -5.f; vy = 0.f;
    CHECK(Near(FmSeam_Stick(1, 0, 450, 0, -0.2f, 0.f, vx, vy), -0.2f) && Near(vx, 0.f));
    // drifting off with no input: held
    vx = 20.f; vy = 0.f;
    CHECK(Near(FmSeam_Stick(1, 0, 0, 0, 0.3f, 0.f, vx, vy), 0.3f) && Near(vx, 0.f));
    // strafing along the edge: held on it
    vx = 20.f; vy = 100.f;
    CHECK(Near(FmSeam_Stick(1, 0, 0, 450, 0.3f, 2.f, vx, vy), 0.3f) && Near(vx, 0.f) && Near(vy, 100.f));
    // 70 deg off straight away (cos 0.34): still held
    vx = 20.f; vy = 0.f;
    CHECK(Near(FmSeam_Stick(1, 0, 0.342f * 450, 0.94f * 450, 0.3f, 0.f, vx, vy), 0.3f) && Near(vx, 0.f));
    // strafing away (straight and 45 deg): let go, speed kept
    vx = 20.f; vy = 0.f;
    CHECK(FmSeam_Stick(1, 0, 450, 0, 0.3f, 0.f, vx, vy) == 0.f && Near(vx, 20.f));
    CHECK(FmSeam_Stick(1, 0, 318, 318, 0.3f, 0.f, vx, vy) == 0.f && Near(vx, 20.f));
    // no edge normal: nothing touched
    vx = -30.f; vy = 5.f;
    CHECK(FmSeam_Stick(0, 0, -450, 0, -0.4f, 0.f, vx, vy) == 0.f && Near(vx, -30.f));
}

// ride loop against the lip: engine air accel (sv_airaccelerate 10, 30 u/s air cap), move, stick,
// then the next tick's edge test. Returns ticks the ride lasted (max = ticks).
static int RideTicks(float wx, float wy, int ticks)
{
    const std::vector<Brush> lip = { Box(-500, -5000, 100, 55, 5000, 200) };   // long: 300 ticks at 250 u/s fit
    float x = 76.f, y = 0.f, vx = 0.f, vy = 250.f, bz = 0.f;
    const float dt = 0.015f;
    for (int t = 0; t < ticks; ++t)
    {
        if (!Edge(lip, x, y, 99.75f, bz, 2.f, 0.f)) return t;
        const float wl = std::sqrt(wx * wx + wy * wy);
        if (wl > 0.f)
        {
            const float dx = wx / wl, dy = wy / wl;
            const float add = (std::min)(wl, 30.f) - (vx * dx + vy * dy);
            if (add > 0.f) { const float acc = (std::min)(10.f * wl * dt, add); vx += acc * dx; vy += acc * dy; }
        }
        const float px = x, py = y;
        x += vx * dt; y += vy * dt;
        const float dn = FmSeam_Stick(s_nx, s_ny, wx, wy, x - px, y - py, vx, vy);
        x -= s_nx * dn; y -= s_ny * dn;
    }
    return ticks;
}

static void TestStickRide()
{
    CHECK(RideTicks(-450, 0, 300) == 300);   // strafe into the lip 4.5s: never slides under, still surfing
    CHECK(RideTicks(0, 450, 300) == 300);    // strafe along it
    CHECK(RideTicks(0, 0, 300) == 300);      // hands off
    const int away = RideTicks(450, 0, 300); // strafe away: lets go, but not instantly
    CHECK(away > 1 && away < 30);
    std::printf("  stick: strafe away left the edge after %d ticks\n", away);
}

int main()
{
    TestBottomEdge();
    TestStick();
    TestStickRide();

    // wall made of two brushes side by side, seam at y = 0
    std::vector<Brush> wall2 = { Box(100, -200, 0, 116, 0, 200), Box(100, 0, 0, 116, 200, 200) };
    CHECK(Count(wall2, 0.f, 50.f) == 2);      // hull straddles the seam
    CHECK(Count(wall2, 20.f, 50.f) == 2);     // seam still under the hull edge (y-24 = -4)
    CHECK(Count(wall2, 30.f, 50.f) == 1);     // seam 6u past the hull edge: no seam
    CHECK(Count(wall2, 30.f, 50.f, 8.f) == 2);// slack widens the window
    CHECK(Count(wall2, 100.f, 50.f) == 1);    // middle of one brush

    // one solid wall: never a seam
    std::vector<Brush> wall1 = { Box(100, -200, 0, 116, 200, 200) };
    CHECK(Count(wall1, 0.f, 50.f) == 1);

    // stacked brushes: horizontal seam at z = 64, hull 30..92 covers it
    std::vector<Brush> stack = { Box(100, -200, 0, 116, 200, 64), Box(100, -200, 64, 116, 200, 200) };
    CHECK(Count(stack, 0.f, 30.f) == 2);
    CHECK(Count(stack, 0.f, 100.f) == 1);

    // wall surf foot slab (top = 2): the seam has to be AT the feet, like a real pixel surf
    CHECK(Count(stack, 0.f, 64.f, 0.f, 2.f) == 2);    // feet level with the seam: rides
    CHECK(Count(stack, 0.f, 63.5f, 0.f, 2.f) == 2);   // half a unit under it: still rides
    CHECK(Count(stack, 0.f, 30.f, 0.f, 2.f) == 1);    // seam at chest height: whole hull said 2, feet say no
    CHECK(Count(stack, 0.f, 58.f, 0.f, 2.f) == 1);    // 6u under: dropped past the lip
    CHECK(Count(stack, 0.f, 70.f, 0.f, 2.f) == 1);    // risen past it
    CHECK(Count(stack, 0.f, 58.f, 8.f, 2.f) == 2);    // seam slack widens the window the same way
    CHECK(Count(wall2, 0.f, 50.f, 0.f, 2.f) == 2);    // vertical seam: rides at any height
    CHECK(Count(wall1, 0.f, 50.f, 0.f, 2.f) == 1);    // one solid brush still never a seam

    // stepped wall: second brush's face sits 2u further out -> different plane, no seam
    std::vector<Brush> step = { Box(100, -200, 0, 116, 0, 200), Box(98, 0, 0, 116, 200, 200) };
    CHECK(Count(step, 0.f, 50.f) == 1);

    // floor brush under the wall: touches the window but its top face is not the wall plane
    std::vector<Brush> floorWall = { Box(100, -200, 0, 116, 200, 200), Box(-500, -500, -16, 500, 500, 0) };
    CHECK(Count(floorWall, 0.f, 0.f) == 1);

    // inside corner: perpendicular wall's -y face has the same distance (-100) but a different
    // normal -> a corner is not a seam
    std::vector<Brush> corner = { Box(100, -200, 0, 116, 200, 200), Box(60, 100, 0, 100, 200, 200) };
    CHECK(Count(corner, 90.f, 50.f) == 1);

    // coplanar brush far away along the wall: shares the plane but is outside the window
    std::vector<Brush> far = { Box(100, -200, 0, 116, 0, 200), Box(100, 400, 0, 116, 600, 200) };
    CHECK(Count(far, -100.f, 50.f) == 1);

    // brush flush against the window from outside (exactly touching) does not count
    {
        const Brush b = Box(0, 0, 0, 10, 10, 10);
        const float mn[3] = { 10, 0, 0 }, mx[3] = { 20, 10, 10 };
        CHECK(!FmSeam_BoxInBrush(b.data(), (int)b.size(), mn, mx));
        const float mn2[3] = { 9, 0, 0 };
        CHECK(FmSeam_BoxInBrush(b.data(), (int)b.size(), mn2, mx));
    }

    // diagonal wedge: its AABB overlaps the window but the solid part does not
    // solid: x+y <= 0 inside box [-100,100]^3 (+ bevels). Window corner at (10,10) is outside.
    {
        const float s = 0.70710678f;
        Brush wedge = { { s, s, 0, 0 }, { 1, 0, 0, 100 }, { 0, 1, 0, 100 }, { 0, 0, 1, 100 },
                        { -1, 0, 0, 100 }, { 0, -1, 0, 100 }, { 0, 0, -1, 100 } };
        const float mn[3] = { 10, 10, 0 }, mx[3] = { 50, 50, 50 };
        CHECK(!FmSeam_BoxInBrush(wedge.data(), (int)wedge.size(), mn, mx));
        const float mn2[3] = { -5, -5, 0 };
        CHECK(FmSeam_BoxInBrush(wedge.data(), (int)wedge.size(), mn2, mx));
        // diagonal seam: two wedges sharing the face plane both count
        CHECK(FmSeam_HasFace(wedge.data(), (int)wedge.size(), s, s, 0, 0));
        CHECK(!FmSeam_HasFace(wedge.data(), (int)wedge.size(), s, s, 0, 1));
    }

    // empty plane list never counts
    {
        const float mn[3] = { 0, 0, 0 }, mx[3] = { 1, 1, 1 };
        CHECK(!FmSeam_BoxInBrush(nullptr, 0, mn, mx));
    }

    std::printf("%d/%d checks passed\n", s_checks - s_failed, s_checks);
    return s_failed ? 1 : 0;
}
