// Gate test for the media player's cover art geometry (src/hacks/hud/mp_art.h): the scale +
// centre crop handed to winrt's BitmapTransform. A crop rect that runs past the scaled image
// throws inside GetSoftwareBitmapAsync, which shows up as "the cover art never appears" and
// nothing else, so the out-of-bounds case is what this pins.
// Build + run:  cmake --build build --config Release --target mediaplayer_test && build\Release\mediaplayer_test.exe
//
// It also pins the timeline math (src/hacks/hud/mp_time.h): the progress bar that did not follow
// the track's length or its position, because smc's Position is a snapshot dated
// LastUpdatedTime and was being read as if it were live.
#include "../src/hacks/hud/mp_art.h"
#include "../src/hacks/hud/mp_time.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>   // MSVC needs it for `for (x : { ... })`

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

static const uint32_t K = 64;   // what mediaplayer.cpp passes (kArt)

// Every property the transform needs, for one source size.
static void CheckSize(uint32_t w, uint32_t h)
{
    const mp::ArtCrop c = mp::ArtCropFor(w, h, K);

    // the crop is the tile, and it fits inside the scaled image
    CHECK(c.w == K && c.h == K);
    CHECK(c.scaledW >= K && c.scaledH >= K);
    CHECK(c.x + c.w <= c.scaledW);
    CHECK(c.y + c.h <= c.scaledH);

    // the SHORT side is the one that ends up at the tile size -- that is what makes this a
    // crop of the middle instead of a squash
    CHECK((w <= h) ? (c.scaledW == K) : (c.scaledH == K));

    // aspect survives the scale: the scaled ratio matches the source ratio to within a pixel
    // of rounding on the long side
    const double want = (w <= h) ? ((double)h / (double)w) * K : ((double)w / (double)h) * K;
    const double got  = (w <= h) ? (double)c.scaledH : (double)c.scaledW;
    CHECK(got >= want - 1.0 && got <= want + 1.0);

    // the cut is centred: the two margins differ by at most the odd pixel
    const uint32_t mx = c.scaledW - K - c.x, my = c.scaledH - K - c.y;
    CHECK(mx <= c.x + 1 && c.x <= mx + 1);
    CHECK(my <= c.y + 1 && c.y <= my + 1);
}

// ==== timeline (mp_time.h) ======================================================================
static void TimelineTests()
{
    const long long kMin = 60 * 1000;

    // THE REGRESSION. A 10 minute video, the browser pushed its position 37s ago and has said
    // nothing since. Dating that snapshot NOW (what the old code did) reads 2:00 forever; aging
    // it reads 2:37 and keeps climbing.
    {
        const mp::Track t = mp::TrackAt(0, 10 * kMin, 0, 10 * kMin, 2 * kMin, 37000, 1.0, true);
        CHECK(t.totalMs == 10 * kMin);
        CHECK(t.curMs == 2 * kMin + 37000);
        const mp::Track stale = mp::TrackAt(0, 10 * kMin, 0, 10 * kMin, 2 * kMin, 0, 1.0, true);
        CHECK(stale.curMs == 2 * kMin);                  // the old reading
        CHECK(t.curMs - stale.curMs == 37000);           // exactly the push age, nothing else
    }

    // Paused: the snapshot is the truth, age means nothing.
    {
        const mp::Track t = mp::TrackAt(0, 10 * kMin, 0, 0, 2 * kMin, 90000, 1.0, false);
        CHECK(t.curMs == 2 * kMin);
    }

    // Playback rate: 2x video, 8s of wall clock is 16s of track.
    {
        const mp::Track t = mp::TrackAt(0, 10 * kMin, 0, 0, 60000, 8000, 2.0, true);
        CHECK(t.curMs == 60000 + 16000);
    }

    // Rates a player has no business reporting fall back to 1x rather than throwing the bar.
    for (double bad : { 0.0, -1.0, -0.5, 17.0, 1e9 }) {
        const mp::Track t = mp::TrackAt(0, 10 * kMin, 0, 0, 60000, 5000, bad, true);
        CHECK(t.curMs == 65000);
    }

    // A timestamp older than the cap is a lying clock, not a stale position: do not extrapolate.
    {
        const mp::Track t = mp::TrackAt(0, 10 * kMin, 0, 0, 2 * kMin, mp::kMaxAgeMs, 1.0, true);
        CHECK(t.curMs == 2 * kMin);
        const mp::Track ok = mp::TrackAt(0, 60 * kMin, 0, 0, 0, mp::kMaxAgeMs - 1, 1.0, true);
        CHECK(ok.curMs == 60 * kMin);                    // just inside: extrapolates, then clamps
    }

    // Clock ran backwards between the push and the read.
    {
        const mp::Track t = mp::TrackAt(0, 10 * kMin, 0, 0, 2 * kMin, -5000, 1.0, true);
        CHECK(t.curMs == 2 * kMin);
    }

    // Length: EndTime wins, the seek window is the fallback, and neither means no bar.
    {
        CHECK(mp::TrackAt(0, 5 * kMin, 0, 9 * kMin, 0, 0, 1.0, false).totalMs == 5 * kMin);
        CHECK(mp::TrackAt(0, 0, 0, 3 * kMin, 0, 0, 1.0, false).totalMs == 3 * kMin);
        CHECK(mp::TrackAt(0, 0, 0, 0, 0, 0, 1.0, false).totalMs == 0);
        CHECK(mp::TrackAt(0, 0, 0, 0, 0, 0, 1.0, false).curMs == 0);
        // live stream / chapter offsets: a timeline that does not start at zero
        const mp::Track t = mp::TrackAt(30000, 30000 + 4 * kMin, 30000, 30000 + 4 * kMin,
                                        30000 + 60000, 0, 1.0, true);
        CHECK(t.totalMs == 4 * kMin && t.curMs == 60000);
    }

    // A timeline left over from the previous track runs past the new one: clamp, never overflow
    // the bar past its box.
    {
        const mp::Track t = mp::TrackAt(0, 3 * kMin, 0, 0, 2 * kMin, 10 * kMin, 1.0, true);
        CHECK(t.curMs == 3 * kMin);
        CHECK(mp::ProgressOf(t) == 1.f);
        const mp::Track back = mp::TrackAt(0, 3 * kMin, 0, 0, -5000, 0, 1.0, false);
        CHECK(back.curMs == 0);
    }

    // ProgressOf: the ratio, clamped, and 0 when there is no length to divide by.
    {
        CHECK(std::fabs(mp::ProgressOf({ 30000, 120000 }) - 0.25f) < 1e-6f);
        CHECK(mp::ProgressOf({ 0, 0 }) == 0.f);
        CHECK(mp::ProgressOf({ 5000, 0 }) == 0.f);
        CHECK(mp::ProgressOf({ 0, 120000 }) == 0.f);
    }

    // The render side's use: same call with the track already resolved, aged by the time since
    // the poll. It must only ever go forward, one frame to the next, inside a poll interval.
    {
        float prev = -1.f;
        for (long long age = 0; age <= 1000; age += 16) {
            const float p = mp::ProgressOf(mp::TrackAt(0, 4 * kMin, 0, 0, 2 * kMin, age, 1.0, true));
            CHECK(p >= prev);
            prev = p;
        }
        CHECK(prev > 0.5f && prev < 0.51f);
    }

    // Sweep: nothing in the range a real session produces escapes [0, total] or [0, 1].
    for (long long total = 1000; total <= 3 * 60 * 60 * 1000; total *= 3)
        for (long long pos = 0; pos <= total; pos += total / 7 + 1)
            for (long long age : { 0LL, 1LL, 1000LL, 60000LL, 3LL * 60 * 60 * 1000 })
                for (int pi = 0; pi < 2; ++pi) {
                    const bool playing = pi != 0;
                    const mp::Track t = mp::TrackAt(0, total, 0, 0, pos, age, 1.5, playing);
                    CHECK(t.totalMs == total);
                    CHECK(t.curMs >= 0 && t.curMs <= total);
                    const float p = mp::ProgressOf(t);
                    CHECK(p >= 0.f && p <= 1.f);
                }
}

int main()
{
    // the real one, read off this machine's live session by the winrt probe: a 150x83 youtube
    // thumbnail scales to 116x64 and the middle 64 is cut out of it
    {
        const mp::ArtCrop c = mp::ArtCropFor(150, 83, K);
        CHECK(c.scaledW == 116 && c.scaledH == 64);
        CHECK(c.x == 26 && c.y == 0 && c.w == 64 && c.h == 64);
    }

    // square album art is the common case: no scale distortion, no crop offset
    {
        const mp::ArtCrop c = mp::ArtCropFor(640, 640, K);
        CHECK(c.scaledW == K && c.scaledH == K);
        CHECK(c.x == 0 && c.y == 0);
    }

    // already smaller than the tile: it is scaled UP, never left short of the crop
    CheckSize(1, 1);
    CheckSize(32, 32);
    CheckSize(40, 12);

    // the shapes that break a naive formula: extreme ratios, both ways, and the off-by-one
    // sizes either side of the tile
    CheckSize(1, 10000);
    CheckSize(10000, 1);
    CheckSize(63, 64);
    CheckSize(64, 63);
    CheckSize(65, 64);
    CheckSize(2048, 2047);

    // sweep: every source size in a range that covers real thumbnails, both orientations
    for (uint32_t w = 1; w <= 400; ++w)
        for (uint32_t h = 1; h <= 400; h += 7)
            CheckSize(w, h);

    TimelineTests();

    std::printf("%s: %d checks, %d failed\n", s_failed ? "FAIL" : "PASS", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
