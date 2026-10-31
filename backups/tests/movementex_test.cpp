// Gate test for Unit A's pure decisions (src/hacks/movement/movement_ex_pure.h): hsw sign,
// delay hop's tick gate, long jump's adaptive-cancel reasons.
// Build + run:  cmake --build build --config Release --target movementex_test && build\Release\movementex_test.exe
#include "../src/hacks/movement/movement_ex_pure.h"
#include <cstdio>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

int main()
{
    // HswSign: side 0 = right-biased, side 1 = left-biased; flips again once airborne.
    CHECK(HswSign(0, true)  ==  1);
    CHECK(HswSign(0, false) == -1);
    CHECK(HswSign(1, true)  == -1);
    CHECK(HswSign(1, false) ==  1);

    // DelayHopShouldJump: strictly greater than lastAirTick + wait, not >=.
    CHECK(DelayHopShouldJump(6, 0, 6)   == false); // exactly at the boundary: not yet
    CHECK(DelayHopShouldJump(7, 0, 6)   == true);  // one tick past: go
    CHECK(DelayHopShouldJump(5, 10, 6)  == false); // still well inside the wait window

    // LjAdaptiveShouldCancel: fires on the first ENABLED reason, never on a disabled one.
    CHECK(LjAdaptiveShouldCancel(true, false, false, /*keyUp*/true,  false, false) == true);
    CHECK(LjAdaptiveShouldCancel(true, false, false, /*keyUp*/false, false, false) == false);
    CHECK(LjAdaptiveShouldCancel(false, true, false, false, /*ground*/true, false) == true);
    CHECK(LjAdaptiveShouldCancel(false, false, true, false, false, /*timeUp*/true) == true);
    CHECK(LjAdaptiveShouldCancel(false, false, false, true, true, true) == false); // nothing enabled

    std::printf("%s: %d/%d checks passed\n", s_failed ? "FAIL" : "PASS", s_checks - s_failed, s_checks);
    return s_failed ? 1 : 0;
}
