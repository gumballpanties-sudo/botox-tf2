// Gate test for src/hacks/esp/esp_math.h (oof arrow placement + distance fade).
// Build + run: cmake --build build --config Release --target esp_math_test && build\Release\esp_math_test.exe
#include "../src/hacks/esp/esp_math.h"
#include <cstdio>
#include <cmath>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

static float Dist(float ax, float ay, float bx, float by) { return sqrtf((ax - bx) * (ax - bx) + (ay - by) * (ay - by)); }

int main()
{
    const float W = 1920.f, H = 1080.f, CX = 960.f, CY = 540.f;
    const float OFF = 100.f, S = 25.f;

    // target straight right (off screen): arrow 100px right of center, tip points right
    {
        auto a = EspMath::OofArrowFromScreen(W, H, W + 300.f, CY, OFF, S);
        CHECK(fabsf(a.x - (CX + OFF)) < 0.01f && fabsf(a.y - CY) < 0.01f);
        CHECK(a.tip[0] > a.a[0] + 10.f && a.tip[0] > a.b[0] + 10.f);   // tip is the outward vertex
        CHECK(fabsf(a.tip[1] - CY) < 0.01f);
        CHECK(fabsf(Dist(a.a[0], a.a[1], a.b[0], a.b[1]) - S) < 0.01f); // base = side length
    }
    // target behind/above (screen point above center): arrow above center, tip up
    {
        auto a = EspMath::OofArrowFromScreen(W, H, CX, -500.f, OFF, S);
        CHECK(fabsf(a.x - CX) < 0.01f && fabsf(a.y - (CY - OFF)) < 0.01f);
        CHECK(a.tip[1] < a.a[1] - 10.f && a.tip[1] < a.b[1] - 10.f);
    }
    // big offset gets clamped to the screen rect inset by scale (Amalgam vPos *= |max(c)|)
    {
        auto a = EspMath::OofArrowFromScreen(W, H, W + 300.f, CY, 5000.f, S);
        CHECK(fabsf(a.x - (W - S)) < 0.05f && fabsf(a.y - CY) < 0.01f);
        auto d = EspMath::OofArrowFromScreen(W, H, CX + 300.f, CY + 400.f, 5000.f, S);   // diagonal, hits bottom first
        CHECK(fabsf(d.y - (H - S)) < 0.05f);
        CHECK(d.x >= S - 0.05f && d.x <= W - S + 0.05f);
    }
    // degenerate (target projects onto center) must not produce NaN/inf
    {
        auto a = EspMath::OofArrowFromScreen(W, H, CX, CY, OFF, S);
        CHECK(std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.tip[0]) && std::isfinite(a.a[1]));
    }
    // screen fade: 0 inside min(cx,cy), 1 at max(cx,cy)
    CHECK(EspMath::OofScreenFade(W, H, CX + 100.f, CY) == 0.f);
    CHECK(EspMath::OofScreenFade(W, H, CX + CY, CY) == 0.f);
    CHECK(EspMath::OofScreenFade(W, H, CX + CX, CY) == 1.f);
    CHECK(fabsf(EspMath::OofScreenFade(W, H, CX + (CX + CY) * 0.5f, CY) - 0.5f) < 1e-4f);
    // distance fade: RemapVal(dist, 1000, 900, 0, 1)
    CHECK(EspMath::OofDistFade(0.f, 1000.f) == 1.f);
    CHECK(EspMath::OofDistFade(900.f, 1000.f) == 1.f);
    CHECK(fabsf(EspMath::OofDistFade(950.f, 1000.f) - 0.5f) < 1e-4f);
    CHECK(EspMath::OofDistFade(1000.f, 1000.f) == 0.f);
    CHECK(EspMath::OofDistFade(5000.f, 1000.f) == 0.f);

    // distance fade
    CHECK(EspMath::DistanceFadeAlpha(0.f, 500.f, 2000.f) == 1.f);
    CHECK(EspMath::DistanceFadeAlpha(500.f, 500.f, 2000.f) == 1.f);
    CHECK(EspMath::DistanceFadeAlpha(2000.f, 500.f, 2000.f) == 0.f);
    CHECK(EspMath::DistanceFadeAlpha(3000.f, 500.f, 2000.f) == 0.f);
    CHECK(fabsf(EspMath::DistanceFadeAlpha(1250.f, 500.f, 2000.f) - 0.5f) < 1e-5f);
    CHECK(EspMath::DistanceFadeAlpha(1000.f, 500.f, 400.f) == 1.f); // far<=near disables fade

    // screen box (10-31 "esp draws nothing"): IsOnScreen bounds have flTop = max screen y
    // (feet), flBottom = min screen y (head). A standing player must give a positive box whose
    // top edge is the head.
    {
        const float left = 900.f, right = 960.f, top = 700.f /*feet*/, bottom = 560.f /*head*/;
        auto b = EspMath::BoxFromBounds(left, right, top, bottom);
        CHECK(b.w == 60 && b.h == 140);
        CHECK(b.x == 900 && b.y == 560);
    }

    std::printf("%s -- %d checks, %d failed\n", s_failed ? "FAILED" : "PASS", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
