// Gate test for the mitosis membrane's envelope (src/hacks/hud/metaball.h). No device, no game:
// the whole shape is one quadratic per slice, and that is checkable offline.
//
// WHAT WOULD BREAK WITHOUT IT
//   * a sign slip in the quadratic -- the surface would still draw, just not through the tag
//     centres, so the "cell" would sit beside the text instead of on it.
//   * the wrong root -- the inner root traces the HOLE between the blobs, and the membrane would
//     render inside out.
//   * the neck never pinching (or pinching immediately): the split is the whole effect. The
//     break distance for two equal blobs is exactly d = 2*sqrt(2)*r, which is checked here.
//   * Pad() too small -- the strip would be cut off flat at the tips instead of closing.
//
// Build + run: cmake --build build --config Release --target metaball_test && build\Release\metaball_test.exe

#include <cstdio>
#include <cmath>
#include "../src/hacks/hud/metaball.h"

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

// the field the header solves -- the test asks it independently instead of trusting the algebra
static float Field(float s, float h, float d, float r1, float r2)
{
    const float d1 = s * s + h * h;
    const float d2 = (s - d) * (s - d) + h * h;
    return r1 * r1 / d1 + r2 * r2 / d2;
}

int main()
{
    const float r = 10.f;

    // 1. one blob, far from the other: the surface through its own centre is its own radius, and
    //    a lone blob's tip is at r
    Near(mb::HalfWidth(0.f, 1000.f, r, r), r, 0.02f, "far apart: half width at centre = r");
    Check(mb::HalfWidth(-r - 0.5f, 1000.f, r, r) == 0.f, "far apart: nothing past the tip");

    // 2. coincident centres = one round cell of radius r*sqrt(2) (both blobs add up)
    Near(mb::HalfWidth(0.f, 0.f, r, r), r * 1.41421f, 0.01f, "d=0: half width = r*sqrt(2)");

    // 3. every sampled point on the returned surface must actually sit on the iso surface F = 1
    for (float d = 0.f; d <= 4.f * r; d += 1.7f)
        for (float s = -1.5f * r; s <= d + 1.5f * r; s += 0.9f)
        {
            const float h = mb::HalfWidth(s, d, r, r);
            if (h <= 0.f) continue;
            Near(Field(s, h, d, r, r), 1.f, 1e-3f, "surface point is on F=1");
        }

    // 4. unequal radii, same rule
    for (float s = -12.f; s <= 40.f; s += 1.3f)
    {
        const float h = mb::HalfWidth(s, 26.f, 8.f, 14.f);
        if (h > 0.f) Near(Field(s, h, 26.f, 8.f, 14.f), 1.f, 1e-3f, "unequal radii on F=1");
    }

    // 5. the pinch. On the axis at the midpoint F = 2*r^2/(d/2)^2 = 8r^2/d^2, so the neck breaks
    //    at d = 2*sqrt(2)*r. Just inside it is still joined, just outside it is two cells
    const float dBreak = 2.f * 1.41421356f * r;
    Check(mb::HalfWidth(dBreak * 0.5f, dBreak * 0.98f, r, r) > 0.f, "neck joined just before break");
    Check(mb::HalfWidth(dBreak * 0.5f, dBreak * 1.02f, r, r) == 0.f, "neck broken just after break");

    // ...and that the two lobes are still drawn after the break (the strip must not go empty,
    // the tags keep their cells until the fade retires them)
    Check(mb::HalfWidth(0.f, dBreak * 1.02f, r, r) > 0.f, "lobes survive the break");

    // 6. the neck only ever thins as the cells separate -- no bulge back, or the split stutters
    float prev = mb::HalfWidth(r, 2.f * r, r, r);
    for (float d = 2.f * r; d <= dBreak; d += 0.25f)
    {
        const float h = mb::HalfWidth(d * 0.5f, d, r, r);
        Check(h <= prev + 1e-4f, "neck never widens as d grows");
        prev = h;
    }

    // 7. symmetry about the midpoint, equal blobs
    for (float t = 0.f; t < 12.f; t += 1.5f)
        Near(mb::HalfWidth(-t, 25.f, r, r), mb::HalfWidth(25.f + t, 25.f, r, r), 1e-4f,
             "equal blobs are mirror symmetric");

    // 8. Pad covers the reach at every separation: the slice range must start outside the surface
    for (float d = 0.f; d <= 5.f * r; d += 0.5f)
        Check(mb::HalfWidth(-mb::Pad(r), d, r, r) == 0.f, "Pad starts the strip outside the shape");

    // 9. a zero radius blob contributes nothing (the membrane is gone at the end of the split)
    Check(mb::HalfWidth(0.f, 20.f, 0.f, 0.f) == 0.f, "r=0: no surface");

    printf(g_fail ? "\n%d FAILED\n" : "\nall metaball checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
