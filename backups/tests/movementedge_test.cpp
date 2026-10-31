// Gate test for Unit A2's pure decisions (src/hacks/movement/movement_edge_pure.h): edge skip's
// tick-rate scaling + bend check, blockbot's head-mode wish + strafe sign.
// Build + run:  cmake --build build --config Release --target movementedge_test && build\Release\movementedge_test.exe
#include "../src/hacks/movement/movement_edge_pure.h"
#include <cstdio>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

int main()
{
    // EsTicksPure: a 64-tick reference count scaled to the real rate, never under 1.
    CHECK(EsTicksPure(4, 64.f)  == 4);     // 64 tick: identity
    CHECK(EsTicksPure(4, 66.667f) == 4);   // TF2 66.67 tick: rounds back to 4 (4*66.667/64=4.167)
    CHECK(EsTicksPure(1, 66.667f) == 1);   // floor of 1, never 0
    CHECK(EsTicksPure(6, 128.f) == 12);    // double rate -> double ticks

    // EsBentPure: maxBend>=180 disables the check entirely; otherwise |delta|>maxBend bends.
    CHECK(EsBentPure(10.f, 180.f)  == false);  // disabled
    CHECK(EsBentPure(170.f, 180.f) == false);  // disabled even at a huge delta
    CHECK(EsBentPure(10.f, 15.f)   == false);  // inside the cone
    CHECK(EsBentPure(16.f, 15.f)   == true);   // just past it
    CHECK(EsBentPure(-16.f, 15.f)  == true);   // symmetric, negative side

    // EsSideWishOffsetDeg (botox perfect_angle): REGRESSION -- the first port returned acos(cosGain),
    // which is 90 when the air cap binds (TF2's normal case: airaccel 10 * 300 * 0.015 = 45 > 30),
    // putting the side wish along the velocity and leaving edge skip with no steer at all.
    CHECK(EsSideWishOffsetDeg(0.f) < 0.001f);                     // cap binds: side wish square to vel
    {
        // botox: diff = 30 - accel, ang = |90 - acos(diff / speed)|. speed 300, accel 12 -> diff 18
        const float c = 18.f / 300.f;
        const float want = fabsf(90.f - acosf(c) * 57.29577951f);
        CHECK(fabsf(EsSideWishOffsetDeg(c) - want) < 1e-4f);
        CHECK(EsSideWishOffsetDeg(c) > 3.f && EsSideWishOffsetDeg(c) < 4.f);   // ~3.44 deg lean
    }
    CHECK(fabsf(EsSideWishOffsetDeg(1.f) - 90.f) < 1e-3f);        // slow: the whole lean
    CHECK(EsSideWishOffsetDeg(5.f) == EsSideWishOffsetDeg(1.f)); // clamped, never NaN

    // BlockBotHeadWish: 2x target minus own, matches botox's want*2 - lv term.
    CHECK(BlockBotHeadWish(100.f, 0.f)   == 200.f);
    CHECK(BlockBotHeadWish(100.f, 50.f)  == 150.f);
    CHECK(BlockBotHeadWish(-100.f, 0.f)  == -200.f);
    CHECK(BlockBotHeadWish(0.f, 0.f)     == 0.f);

    // BlockBotStrafeSign: deadzone +-0.25 degrees, signs flipped vs raw yaw delta (matches
    // botox: delta>0.25 -> side_move negative, delta<-0.25 -> side_move positive).
    CHECK(BlockBotStrafeSign(0.3f)   == -1);
    CHECK(BlockBotStrafeSign(-0.3f)  ==  1);
    CHECK(BlockBotStrafeSign(0.25f)  ==  0);  // boundary: inside the deadzone
    CHECK(BlockBotStrafeSign(-0.25f) ==  0);
    CHECK(BlockBotStrafeSign(0.f)    ==  0);

    std::printf("%s: %d/%d checks passed\n", s_failed ? "FAIL" : "PASS", s_checks - s_failed, s_checks);
    return s_failed ? 1 : 0;
}
