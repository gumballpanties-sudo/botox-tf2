// Gate test for the low fps viewmodel (src/hacks/visuals/viewmodel_fps.h).
//
//   1. STEP FRAME = LIVE BONES. On a frame that re-reads, Apply hands back the same matrices it
//      was given (camera space in, camera space out, one rigid transform each way).
//   2. HELD FRAME RIDES THE CAMERA. Camera moves, bones not re-read: out = cam1 * cam0^-1 * bones0,
//      worked out the long way. A gun glued to the screen is exactly this.
//   3. CADENCE. 300 fps game, 24 fps rate, 10 s: 240 steps +-1. Not a multiple of the game rate
//      on purpose -- whole-step rounding keeps the cadence even.
//   4. NEVER AN OLD POSE. Skeleton count change / not drawn last frame / clock jump: re-read.
//
// Build + run:
//   cmake --build build --config Release --target vmfps_test && build\Release\vmfps_test.exe
#include "../src/hacks/visuals/viewmodel_fps.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>

using VmFps::Mat34;

static int s_failed = 0, s_checks = 0;
#define CHECK(c) do { ++s_checks; if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++s_failed; } } while (0)

static float Rnd(float lo, float hi) { return lo + (hi - lo) * (float)std::rand() / (float)RAND_MAX; }

static void RandBones(Mat34* b, int n)
{
    for (int i = 0; i < n; ++i) {
        float r[3][3];
        const float ang[3] = { Rnd(-80, 80), Rnd(-180, 180), Rnd(-180, 180) };
        VmFps::AngleMatrix(ang, r);
        for (int row = 0; row < 3; ++row) {
            for (int c = 0; c < 3; ++c) b[i].m[row][c] = r[row][c];
            b[i].m[row][3] = Rnd(-50, 50) + 1000.f;
        }
    }
}

static bool Near(const Mat34& a, const Mat34& b, float eps)
{
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c)
        if (std::fabs(a.m[r][c] - b.m[r][c]) > eps) return false;
    return true;
}

// oracle: 3x4 compose, out = A * B (A rigid)
static void Mul(const float A[3][4], const Mat34& B, Mat34& out)
{
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c) {
        out.m[r][c] = A[r][0] * B.m[0][c] + A[r][1] * B.m[1][c] + A[r][2] * B.m[2][c];
        if (c == 3) out.m[r][c] += A[r][3];
    }
}

static void Cam(const float org[3], const float ang[3], float out[3][4])
{
    float r[3][3]; VmFps::AngleMatrix(ang, r);
    for (int row = 0; row < 3; ++row) { for (int c = 0; c < 3; ++c) out[row][c] = r[row][c]; out[row][3] = org[row]; }
}

static void Inv(const float c[3][4], float out[3][4])
{
    for (int r = 0; r < 3; ++r) {
        for (int k = 0; k < 3; ++k) out[r][k] = c[k][r];
        out[r][3] = -(c[0][r] * c[0][3] + c[1][r] * c[1][3] + c[2][r] * c[2][3]);
    }
}

int main()
{
    std::srand(7);
    const int   model = 1;
    const void* mdl   = &model;
    const int   n     = 40;
    const float dt    = 1.f / 300.f;

    // 1. step frame hands the live bones back
    {
        VmFps::Core core;
        Mat34 bones[n]; RandBones(bones, n);
        const float org[3] = { 100.f, -20.f, 64.f }, ang[3] = { 10.f, 45.f, 0.f };
        core.Frame(1, dt, 24.f, org, ang);
        CHECK(core.Stepped());   // first frame: gap < 0 path
        const Mat34* out = core.Apply(mdl, bones, n);
        CHECK(out != bones);
        for (int i = 0; i < n; ++i) CHECK(Near(out[i], bones[i], 2e-3f));
        // same frame, second call (depth pass): same array, no re-read
        CHECK(core.Apply(mdl, bones, n) == out);
    }

    // 2. held frame rides the camera
    {
        VmFps::Core core;
        Mat34 bones0[n]; RandBones(bones0, n);
        Mat34 bones1[n]; RandBones(bones1, n);   // the animation moved on, must NOT show
        const float org0[3] = { 100.f, -20.f, 64.f }, ang0[3] = { 10.f, 45.f, 0.f };
        const float org1[3] = { 130.f,  15.f, 70.f }, ang1[3] = { -5.f, 90.f, 2.f };
        core.Frame(1, dt, 24.f, org0, ang0);
        core.Apply(mdl, bones0, n);
        core.Frame(2, dt, 24.f, org1, ang1);
        CHECK(!core.Stepped());   // 1/300 s < 1/24 s
        const Mat34* out = core.Apply(mdl, bones1, n);

        float c0[3][4], c1[3][4], c0i[3][4]; Cam(org0, ang0, c0); Cam(org1, ang1, c1); Inv(c0, c0i);
        for (int i = 0; i < n; ++i) {
            Mat34 local, want; Mul(c0i, bones0[i], local); Mul(c1, local, want);
            CHECK(Near(out[i], want, 5e-3f));
        }
        // a bone that moved with the camera rigidly would have been the identity; prove bones1 is not shown
        CHECK(!Near(out[0], bones1[0], 1e-2f));
    }

    // 3. cadence: 300 fps, 24 fps rate, 10 s
    {
        VmFps::Core core;
        const float org[3] = {}, ang[3] = {};
        int steps = 0;
        for (int f = 1; f <= 3000; ++f) { core.Frame(f, dt, 24.f, org, ang); if (core.Stepped()) ++steps; }
        CHECK(steps >= 239 && steps <= 241);
        // odd rate still rounds to whole steps: 17 fps over 10 s
        VmFps::Core c2; steps = 0;
        for (int f = 1; f <= 3000; ++f) { c2.Frame(f, dt, 17.f, org, ang); if (c2.Stepped()) ++steps; }
        CHECK(steps >= 169 && steps <= 171);
        // rate clamp: 5 -> 15
        VmFps::Core c3; steps = 0;
        for (int f = 1; f <= 3000; ++f) { c3.Frame(f, dt, 5.f, org, ang); if (c3.Stepped()) ++steps; }
        CHECK(steps >= 149 && steps <= 151);
    }

    // 4. never an old pose
    {
        VmFps::Core core;
        Mat34 a[n]; RandBones(a, n);
        Mat34 b[n]; RandBones(b, n);
        const float org[3] = {}, ang[3] = {};
        core.Frame(1, dt, 24.f, org, ang); core.Apply(mdl, a, n);
        // skeleton count changed -> re-read
        core.Frame(2, dt, 24.f, org, ang);
        const Mat34* out = core.Apply(mdl, b, n - 1);
        CHECK(Near(out[0], b[0], 2e-3f));
        // not drawn last frame (frame 3 skipped) -> re-read
        core.Frame(3, dt, 24.f, org, ang);
        core.Frame(4, dt, 24.f, org, ang);
        out = core.Apply(mdl, a, n - 1);
        CHECK(Near(out[0], a[0], 2e-3f));
        // frame counter jumped (1 s added) -> step
        core.Frame(9, dt, 24.f, org, ang);
        CHECK(core.Stepped());
        // bad input: live bones back
        CHECK(core.Apply(mdl, a, 0) == a);
        CHECK(core.Apply(mdl, a, VmFps::kMaxBones + 1) == a);
        CHECK(core.Apply(nullptr, a, n) == a);
        // OnFrame: the draw's gate -- true only for the frame Frame() last ran on
        CHECK(core.OnFrame(9));
        CHECK(!core.OnFrame(10));
        CHECK(!VmFps::Core().OnFrame(0));
    }

    std::printf("vmfps_test: %d checks, %d failed\n", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
