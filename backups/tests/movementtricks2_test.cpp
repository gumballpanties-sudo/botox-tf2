// Gate test for Unit A3's pure decisions (src/hacks/movement/movement_tricks2_pure.h): bounce
// tangent flip, tung surf's widened ramp band, edge bug style preset bundles.
// Build + run:  cmake --build build --config Release --target movementtricks2_test && build\Release\movementtricks2_test.exe
#include "../src/hacks/movement/movement_tricks2_pure.h"
#include <cstdio>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

int main()
{
    // BounceTangentShouldFlip: tangent (-ny,nx) dotted with dir -- flip only when that's negative.
    // wall normal (1,0) -> tangent (0,1). dir (0,1) agrees (dot=1) -> no flip.
    CHECK(BounceTangentShouldFlip(1.f, 0.f, 0.f, 1.f) == false);
    // dir (0,-1) opposes (dot=-1) -> flip.
    CHECK(BounceTangentShouldFlip(1.f, 0.f, 0.f, -1.f) == true);
    // wall normal (0,1) -> tangent (-1,0). dir (-1,0) agrees -> no flip.
    CHECK(BounceTangentShouldFlip(0.f, 1.f, -1.f, 0.f) == false);
    CHECK(BounceTangentShouldFlip(0.f, 1.f, 1.f, 0.f) == true);

    // TungRampBand: off = passthrough TB band, unaffected by the slider.
    CHECK(TungRampBand(false, 0.4f, 0.05f) == 0.05f);
    // on, wanted band above TB band and under the ceiling -> wanted value wins.
    CHECK(TungRampBand(true, 0.3f, 0.05f) == 0.3f);
    // on, wanted band BELOW TB's own band -> never stricter than TB, clamp up to TB band.
    CHECK(TungRampBand(true, 0.02f, 0.05f) == 0.05f);
    // on, wanted band above the 0.5 ceiling -> clamp down.
    CHECK(TungRampBand(true, 0.9f, 0.05f) == 0.5f);

    std::printf("%s: %d/%d checks passed\n", s_failed ? "FAIL" : "PASS", s_checks - s_failed, s_checks);
    return s_failed ? 1 : 0;
}
