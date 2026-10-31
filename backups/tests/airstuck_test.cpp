// Gate test for the botox air stuck / pixel surf pure math (src/hacks/movement/airstuck_pure.h):
// stamp rotation (pin proven in one yaw frame, sent in another), tick scaling, the clarity grid
// step, the along-wall strafe yaw and the surf-pin test.
// Build + run: cmake --build build --config Release --target airstuck_test && build\Release\airstuck_test.exe
#include "../src/hacks/movement/airstuck_pure.h"
#include <cmath>
#include <cstdio>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)
static bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

int main()
{
    // ---- StampRotate ----
    float f = 0.f, s = 0.f;
    as::StampRotate(90.f, 90.f, 5.f, 0.f, f, s);          // same frame = identity
    CHECK(Near(f, 5.f) && Near(s, 0.f));
    as::StampRotate(0.f, 90.f, 5.f, 0.f, f, s);           // view 90 deg right of the pin yaw
    CHECK(Near(f, 0.f) && Near(s, -5.f));
    as::StampRotate(90.f, 0.f, 5.f, 0.f, f, s);           // ...and left of it
    CHECK(Near(f, 0.f) && Near(s, 5.f));
    as::StampRotate(180.f, -180.f, 3.f, 4.f, f, s);       // wrap: -180 == 180
    CHECK(Near(f, 3.f) && Near(s, 4.f));
    // a stamp rotated into a view and back is the stamp
    as::StampRotate(37.f, 120.f, 7.f, -2.f, f, s);
    float f2 = 0.f, s2 = 0.f;
    as::StampRotate(120.f, 37.f, f, s, f2, s2);
    CHECK(Near(f2, 7.f) && Near(s2, -2.f));
    // MoveTowardYaw agrees with StampRotate for a pure forward press
    as::MoveTowardYaw(10.f, 70.f, 450.f, f, s);
    as::StampRotate(10.f, 70.f, 450.f, 0.f, f2, s2);
    CHECK(Near(f, f2, 1e-2f) && Near(s, s2, 1e-2f));

    // ---- TicksScaled (TF2 66.67 tick ~ 64, 128 halves, 32 doubles) ----
    CHECK(as::TicksScaled(8, 0.015f) == 8);
    CHECK(as::TicksScaled(16, 0.015f) == 17);
    CHECK(as::TicksScaled(4, 0.015f) == 4);
    CHECK(as::TicksScaled(8, 1.f / 128.f) == 16);
    CHECK(as::TicksScaled(8, 1.f / 32.f) == 4);
    CHECK(as::TicksScaled(1, 1.f / 16.f) == 1);           // never 0

    // ---- GridStep: one step = 1/32u of wishvel ----
    const float step = as::GridStep(10.f, 0.015f, 1.f, 1.f);
    CHECK(step > 0.f);
    CHECK(Near(step * 100.f * 10.f * 0.015f * 0.015f, 0.03125f, 1e-6f));
    CHECK(Near(as::GridStep(10.f, 0.015f, 1.f, 0.34f) * 0.34f, step, 1e-6f));   // ducked = bigger step

    // ---- WallStrafeYaw ----
    float yaw = 0.f;
    CHECK(!as::WallStrafeYaw(1.f, 0.f, 0.f, 0.f, 30.f, yaw));            // standing still: no yaw
    CHECK(as::WallStrafeYaw(1.f, 0.f, 0.f, 10.f, 30.f, yaw));            // slow along +y, wall normal +x
    CHECK(Near(yaw, 90.f));                                             // under the cap: straight along
    CHECK(as::WallStrafeYaw(1.f, 0.f, 0.f, 300.f, 30.f, yaw));           // fast: lean into the wall
    CHECK(yaw > 90.f && yaw < 180.f);
    CHECK(as::WallStrafeYaw(1.f, 0.f, 0.f, -300.f, 30.f, yaw));          // other way along the face
    CHECK(yaw < -90.f && yaw > -180.f);

    // ---- SurfPinned ----
    CHECK(as::SurfPinned(-6.f, 200.f, -6.f));
    CHECK(as::SurfPinned(-6.005f, 1.f, -6.f));
    CHECK(!as::SurfPinned(-6.f, 0.5f, -6.f));            // hang, not a surf
    CHECK(!as::SurfPinned(-6.02f, 200.f, -6.f));

    std::printf("airstuck_test: %d checks, %d failed\n", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
