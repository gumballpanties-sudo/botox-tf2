// Gate test for the stack-edgebug detection counter (src/hacks/movement/detections_core.h):
// consecutive hits accumulate, a miss within the grace window does not cut the streak, and the
// streak reports its final length exactly once, on the tick it breaks.
// Build + run: cmake --build build --config Release --target detections_test && build\Release\detections_test.exe
#include "../src/hacks/movement/detections_core.h"
#include <cstdio>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

int main()
{
    // 1. three hits back to back, then a long miss run -> breaks at exactly 3
    {
        DetStack st;
        CHECK(DetStackUpdate(st, true, 8) == 0);
        CHECK(DetStackUpdate(st, true, 8) == 0);
        CHECK(DetStackUpdate(st, true, 8) == 0);
        int broke = 0;
        for (int i = 0; i < 20 && broke == 0; ++i) broke = DetStackUpdate(st, false, 8);
        CHECK(broke == 3);
        // the break reports once; the next call on an already-reset stack is a no-op
        CHECK(DetStackUpdate(st, false, 8) == 0);
    }

    // 2. a miss inside the grace window does not cut the streak -- it keeps counting after
    {
        DetStack st;
        CHECK(DetStackUpdate(st, true, 2) == 0);
        CHECK(DetStackUpdate(st, false, 2) == 0);   // 1 miss, grace is 2 -- still alive
        CHECK(DetStackUpdate(st, true, 2) == 0);     // resumes, sinceHit resets
        CHECK(DetStackUpdate(st, true, 2) == 0);
        int broke = 0;
        for (int i = 0; i < 20 && broke == 0; ++i) broke = DetStackUpdate(st, false, 2);
        CHECK(broke == 3);   // the 3 hits counted, the lone miss in the middle did not cost one
    }

    // 3. no hit ever recorded -> a miss run never reports a break (nothing to break)
    {
        DetStack st;
        bool anyBreak = false;
        for (int i = 0; i < 50; ++i) if (DetStackUpdate(st, false, 4) != 0) anyBreak = true;
        CHECK(!anyBreak);
    }

    std::printf("detections_test: %d/%d checks passed\n", s_checks - s_failed, s_checks);
    return s_failed ? 1 : 0;
}
