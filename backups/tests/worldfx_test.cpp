// Gate test for the aspect ratio letterbox/pillarbox math (src/hacks/visuals/world_fx.h
// WorldFx_ComputeAspectRect) -- the only non-trivial pure logic Unit D's port added
// (fullbright/disable-interp are one-line convar pokes, aspect ratio is the one real branch).
// Build + run: cmake --build build --config Release --target worldfx_test && build\Release\worldfx_test.exe
#include "../src/hacks/visuals/world_fx.h"
#include <cstdio>
#include <cmath>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

int main()
{
    // 16:9 window, ask for 4:3 -> pillarbox (bars left/right), full height kept
    {
        RECT r = WorldFx_ComputeAspectRect(1920, 1080, 4.f / 3.f);
        CHECK(r.top == 0 && r.bottom == 1080);
        CHECK(r.left > 0 && r.right < 1920);
        const int w = r.right - r.left;
        CHECK(std::fabs((float)w / 1080.f - 4.f / 3.f) < 0.01f);
        CHECK(r.left == 1920 - r.right);   // centred
    }

    // 4:3 window, ask for 16:9 -> letterbox (bars top/bottom), full width kept
    {
        RECT r = WorldFx_ComputeAspectRect(1600, 1200, 16.f / 9.f);
        CHECK(r.left == 0 && r.right == 1600);
        CHECK(r.top > 0 && r.bottom < 1200);
        const int h = r.bottom - r.top;
        CHECK(std::fabs((float)1600 / (float)h - 16.f / 9.f) < 0.01f);
        CHECK(r.top == 1200 - r.bottom);   // centred
    }

    // window already at the target ratio -> identity, no bars
    {
        RECT r = WorldFx_ComputeAspectRect(1920, 1080, 16.f / 9.f);
        CHECK(r.left == 0 && r.top == 0 && r.right == 1920 && r.bottom == 1080);
    }

    // degenerate inputs must not crash or invert the rect
    {
        RECT a = WorldFx_ComputeAspectRect(0, 1080, 1.333f);
        CHECK(a.right == 0 && a.bottom == 1080);
        RECT b = WorldFx_ComputeAspectRect(1920, 1080, 0.f);
        CHECK(b.left == 0 && b.top == 0 && b.right == 1920 && b.bottom == 1080);
        RECT c = WorldFx_ComputeAspectRect(1920, 1080, -1.f);
        CHECK(c.left == 0 && c.top == 0 && c.right == 1920 && c.bottom == 1080);
    }

    // every rect WorldFx_AspectDstRect would consider "override" is strictly inside the window
    for (int w = 640; w <= 3840; w += 400)
    for (int h = 480; h <= 2160; h += 300)
    for (float ar : { 1.0f, 1.333f, 1.6f, 1.778f, 2.35f })
    {
        RECT r = WorldFx_ComputeAspectRect(w, h, ar);
        CHECK(r.left >= 0 && r.top >= 0 && r.right <= w && r.bottom <= h);
        CHECK(r.right > r.left && r.bottom > r.top);
    }

    std::printf("%s -- %d checks, %d failed\n", s_failed ? "FAILED" : "PASS", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
