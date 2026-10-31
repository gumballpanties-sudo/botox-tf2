// Gate test for the mitosis warp (src/hacks/hud/miwarp.h). No device, no game: the whole effect is
// a pivot and two scales per label, and that is checkable offline.
//
// WHAT WOULD BREAK WITHOUT IT
//   * a non-zero warp at t = 0 or t = 1 -- the tag would POP deformed on its first and last frame,
//     which is exactly what the old metaball membrane did (full radius on frame one, a blob
//     spawning on top of the text). The zeros at both ends are the fix, so they are asserted.
//   * the pivot on the wrong edge -- the label would stretch AWAY from its partner, off the end of
//     the row, instead of reaching across the join. The fixed-point checks pin it to the outer
//     edge and the reach checks pin the direction, for a partner on either side.
//   * sp not being 1/sa -- the pair would fatten instead of necking, and the pinch is the read.
//   * the peak drifting off the half-way point, or the curve not being one rise and one fall.
//
// Build + run: cmake --build build --config Release --target miwarp_test && build\Release\miwarp_test.exe

#include <cstdio>
#include <cmath>
#include "../src/hacks/hud/miwarp.h"

static int g_fail = 0;

static void Check(bool ok, const char* what)
{
    if (!ok) { printf("FAIL  %s\n", what); ++g_fail; }
}

static void Near(float got, float want, float tol, const char* what)
{
    if (!(fabsf(got - want) <= tol))
    {
        printf("FAIL  %s: got %.4f want %.4f (tol %.4f)\n", what, got, want, tol);
        ++g_fail;
    }
}

// what the draw pass does with the header's answer: scale a point about the pivot
static float Map(const miw::Warp& w, float p) { return w.piv + (p - w.piv) * w.sa; }

int main()
{
    const float c = 100.f, e = 12.f;    // a tag centred at 100, 24 px across the split axis
    const float pR = 140.f, pL = 60.f;  // partner to the right / to the left

    // --- no warp at either end: nothing pops in on top of the text -----------------------------
    const float ends[2] = { 0.f, 1.f };
    for (int i = 0; i < 2; ++i)
    {
        const miw::Warp w = miw::SplitWarp(c, e, pR, ends[i]);
        Near(w.sa, 1.f, 1e-6f, "sa == 1 at the ends of the split");
        Near(w.sp, 1.f, 1e-6f, "sp == 1 at the ends of the split");
        Near(Map(w, c + e), c + e, 1e-4f, "inner edge sits still at the ends");
    }
    // out of range is clamped, not wrapped back into a second bulge
    Near(miw::SplitWarp(c, e, pR, -0.5f).sa, 1.f, 1e-6f, "t < 0 clamps to no warp");
    Near(miw::SplitWarp(c, e, pR,  1.7f).sa, 1.f, 1e-6f, "t > 1 clamps to no warp");

    // --- the pinch: peak at half way, area preserved --------------------------------------------
    const miw::Warp mid = miw::SplitWarp(c, e, pR, 0.5f);
    Near(mid.sa, 1.f + miw::kStretch, 1e-6f, "peak stretch at t = 0.5");
    Near(mid.sa * mid.sp, 1.f, 1e-6f, "sa * sp == 1: the label necks, it does not fatten");
    Check(mid.sp < 1.f, "across the axis it thins");

    // one rise then one fall, no second hump
    float prev = miw::SplitWarp(c, e, pR, 0.f).sa;
    for (int i = 1; i <= 50; ++i)
    {
        const float s = miw::SplitWarp(c, e, pR, i / 100.f).sa;
        Check(s >= prev - 1e-6f, "sa rises to the half-way pinch");
        prev = s;
    }
    for (int i = 51; i <= 100; ++i)
    {
        const float s = miw::SplitWarp(c, e, pR, i / 100.f).sa;
        Check(s <= prev + 1e-6f, "sa falls back after the pinch");
        prev = s;
    }

    // --- direction: pivot on the OUTER edge, reach across the join ------------------------------
    {
        const miw::Warp w = miw::SplitWarp(c, e, pR, 0.5f);   // partner on the right
        Near(w.piv, c - e, 1e-4f, "partner right: pivot is the left (outer) edge");
        Near(Map(w, c - e), c - e, 1e-4f, "partner right: the outer edge does not move");
        Check(Map(w, c + e) > c + e, "partner right: the inner edge reaches toward the partner");
        Check(Map(w, c + e) < pR,    "partner right: the reach stops short of the partner centre");
    }
    {
        const miw::Warp w = miw::SplitWarp(c, e, pL, 0.5f);   // partner on the left, mirrored
        Near(w.piv, c + e, 1e-4f, "partner left: pivot is the right (outer) edge");
        Near(Map(w, c + e), c + e, 1e-4f, "partner left: the outer edge does not move");
        Check(Map(w, c - e) < c - e, "partner left: the inner edge reaches toward the partner");
        Check(Map(w, c - e) > pL,    "partner left: the reach stops short of the partner centre");
    }

    // both halves of a real pair lean into the gap -- a touch, 0.12 closing 70% of it read as too
    // aggressive in game; the liquid strand spans the rest. They must NOT
    // cross: the joining is migoo.h's membrane, and two strings genuinely overlapping reads as
    // one smeared word. The warp's job is to bring the gap inside the membrane's reach
    {
        const float cL = 100.f, cR = 130.f;   // 30 apart, 24 wide each: a 6 px gap at rest
        const miw::Warp wl = miw::SplitWarp(cL, e, cR, 0.5f);
        const miw::Warp wr = miw::SplitWarp(cR, e, cL, 0.5f);
        const float innerL = Map(wl, cL + e), innerR = Map(wr, cR - e);
        const float gap0 = (cR - e) - (cL + e), gapW = innerR - innerL;
        Check(innerL > cL + e && innerR < cR - e, "both halves reach into the join");
        Check(gapW < gap0 * 0.8f,                 "the lean visibly narrows the gap");
        Check(gapW > -1.f,                        "but the two labels do not cross into each other");
    }

    // a tag on the way out runs the identical curve backwards -- same shape, no hysteresis
    for (int i = 0; i <= 10; ++i)
    {
        const float t = i / 10.f;
        Near(miw::SplitWarp(c, e, pR, t).sa, miw::SplitWarp(c, e, pR, 1.f - t).sa, 1e-6f,
             "the merge is the split reversed");
    }

    // degenerate first frame: the slot has not opened, partner centre == own centre
    {
        const miw::Warp w = miw::SplitWarp(c, e, c, 0.f);
        Near(w.sa, 1.f, 1e-6f, "coincident centres at t = 0: no warp, so the side does not matter");
    }

    printf(g_fail ? "\n%d FAILED\n" : "\nall mitosis warp checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
