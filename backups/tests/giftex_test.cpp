// Gate test for unit E5's gif frame math (src/hacks/hud/gif_frame.h): which frame is showing
// at a given elapsed time, with variable per-frame delays and infinite loop wrap. No GDI+/d3d9
// pulled in -- this is the one piece of gif_tex.cpp that is pure.
// Build + run: cmake --build build --config Release --target giftex_test && build\Release\giftex_test.exe
#include "../src/hacks/hud/gif_frame.h"
#include <cstdio>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

int main()
{
    using namespace n_gif;

    // No delays at all: always frame 0, never a crash on an empty vector.
    CHECK(FrameIndexAt({}, 0.0) == 0);
    CHECK(FrameIndexAt({}, 12345.0) == 0);

    // Uniform delays: a plain modulo walk.
    {
        std::vector<uint32_t> d = { 100, 100, 100, 100 };   // total 400ms
        CHECK(FrameIndexAt(d, 0.0) == 0);
        CHECK(FrameIndexAt(d, 50.0) == 0);
        CHECK(FrameIndexAt(d, 100.0) == 1);
        CHECK(FrameIndexAt(d, 250.0) == 2);
        CHECK(FrameIndexAt(d, 399.0) == 3);
        CHECK(FrameIndexAt(d, 400.0) == 0);    // exact wrap
        CHECK(FrameIndexAt(d, 450.0) == 0);
        CHECK(FrameIndexAt(d, 850.0) == 0);    // two full loops (800) + 50ms in -- same as t=50
    }

    // Variable delays, the real donor-gif shape: each frame holds a different length.
    {
        std::vector<uint32_t> d = { 50, 200, 30, 120 };   // total 400ms
        CHECK(FrameIndexAt(d, 0.0)   == 0);
        CHECK(FrameIndexAt(d, 49.0)  == 0);
        CHECK(FrameIndexAt(d, 50.0)  == 1);
        CHECK(FrameIndexAt(d, 249.0) == 1);
        CHECK(FrameIndexAt(d, 250.0) == 2);
        CHECK(FrameIndexAt(d, 279.0) == 2);
        CHECK(FrameIndexAt(d, 280.0) == 3);
        CHECK(FrameIndexAt(d, 399.0) == 3);
        CHECK(FrameIndexAt(d, 400.0) == 0);     // wraps back to frame 0, not past the end
        CHECK(FrameIndexAt(d, 1050.0) == 2);    // 2*400 + 250 -> same spot as the 250ms check
    }

    // Zero-delay frame: must not divide by zero or stall -- treated as 1ms (FrameDelayMs).
    {
        std::vector<uint32_t> d = { 0, 10 };   // total 1 + 10 = 11ms
        CHECK(FrameIndexAt(d, 0.0) == 0);
        CHECK(FrameIndexAt(d, 0.5) == 0);
        CHECK(FrameIndexAt(d, 1.0) == 1);
        CHECK(FrameIndexAt(d, 10.0) == 1);
        CHECK(FrameIndexAt(d, 11.0) == 0);   // wraps
    }

    // All-zero delays: each still takes 1ms (FrameDelayMs's floor) -- total 3ms, never a
    // divide-by-zero and never an infinite-loop stall.
    {
        std::vector<uint32_t> d = { 0, 0, 0 };
        CHECK(FrameIndexAt(d, 0.0) == 0);
        CHECK(FrameIndexAt(d, 1.0) == 1);
        CHECK(FrameIndexAt(d, 2.0) == 2);
        CHECK(FrameIndexAt(d, 3.0) == 0);     // wraps after 3ms
        CHECK(FrameIndexAt(d, 500.0) == 2);   // 500 % 3 == 2 -> third frame's window
    }

    // Negative time (clock jitter) clamps to 0 rather than underflowing the uint64 modulo.
    {
        std::vector<uint32_t> d = { 100, 100 };
        CHECK(FrameIndexAt(d, -50.0) == 0);
    }

    // Single frame: always itself, loop is a no-op.
    {
        std::vector<uint32_t> d = { 250 };
        CHECK(FrameIndexAt(d, 0.0) == 0);
        CHECK(FrameIndexAt(d, 249.0) == 0);
        CHECK(FrameIndexAt(d, 250.0) == 0);
        CHECK(FrameIndexAt(d, 9999.0) == 0);
    }

    std::printf("%s: %d checks, %d failed\n", s_failed ? "FAIL" : "PASS", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
