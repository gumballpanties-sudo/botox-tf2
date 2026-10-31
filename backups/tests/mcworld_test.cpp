// Gate test for the minecraft sky/clouds screen pass's pure math (src/hacks/visuals/mc_world_math.h):
// AngleVectors orthonormality + known poses, CornerRays vs a built cViewProj, cloud cells,
// botox sky constants, CloudOffset wrap/drift.
// Build + run: cmake --build build --config Release --target mcworld_test && build\Release\mcworld_test.exe
#include "../src/hacks/visuals/mc_world_math.h"
#include <cstdio>
#include <cmath>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

using namespace n_mc_world;

static float Len(const Vec3f& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
static float Dot(const Vec3f& a, const Vec3f& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

int main()
{
    // Looking straight down +x, level (pitch 0, yaw 0, roll 0): forward=+x, up=+z, right=-y
    // (Source's convention: +y is LEFT, so "right" at identity is -y -- mathlib_base.cpp:919,
    // verified numerically: right.y = -sr*sp*sy - cr*cy = -1 at sr=sp=sy=0, cr=cy=1).
    {
        Vec3f f, r, u;
        AngleVectors(0.f, 0.f, 0.f, f, r, u);
        CHECK(std::fabs(f.x - 1.f) < 1e-5f && std::fabs(f.y) < 1e-5f && std::fabs(f.z) < 1e-5f);
        CHECK(std::fabs(u.z - 1.f) < 1e-5f);
        CHECK(std::fabs(r.y + 1.f) < 1e-5f);
    }

    // Every basis stays orthonormal across a spread of angles -- a wrong sign anywhere in
    // AngleVectors shows up as a non-unit length or a non-zero dot product, not a crash.
    {
        const float poses[][3] = { {0,0,0}, {45,0,0}, {-30,90,0}, {10,180,15}, {89,-120,-20} };
        for (auto& p : poses) {
            Vec3f f, r, u;
            AngleVectors(p[0], p[1], p[2], f, r, u);
            CHECK(std::fabs(Len(f) - 1.f) < 1e-4f);
            CHECK(std::fabs(Len(r) - 1.f) < 1e-4f);
            CHECK(std::fabs(Len(u) - 1.f) < 1e-4f);
            CHECK(std::fabs(Dot(f, r)) < 1e-4f);
            CHECK(std::fabs(Dot(f, u)) < 1e-4f);
            CHECK(std::fabs(Dot(r, u)) < 1e-4f);
        }
    }

    // CornerRays: build a D3D view-projection the way the engine does for a known camera
    // (rows = vs c8..c11, clip = rows * world), feed it back, and every corner ray must point
    // along fwd +- right*tanX +- up*tanY. A wrong inverse, a transposed row or a flipped ndc y
    // shows up as a corner pointing the wrong way.
    {
        const float pitch = 20.f, yaw = -65.f, roll = 0.f, tanX = 1.33f, tanY = 0.75f;
        const float o[3] = { 120.f, -340.f, 64.f }, zn = 7.f, zf = 28377.9f;
        Vec3f f, r, u;
        AngleVectors(pitch, yaw, roll, f, r, u);
        const float q = zf / (zf - zn);
        const Vec3f axes[3] = { r, u, f };
        float rows[4][4]{};
        for (int i = 0; i < 3; ++i) {
            const Vec3f a = axes[i];
            const float t = -(a.x * o[0] + a.y * o[1] + a.z * o[2]);
            const float sc = (i == 0) ? 1.f / tanX : (i == 1) ? 1.f / tanY : 1.f;
            float* row = rows[i == 2 ? 3 : i];          // w row = view z
            row[0] = a.x * sc; row[1] = a.y * sc; row[2] = a.z * sc; row[3] = t * sc;
            if (i == 2) { rows[2][0] = a.x * q; rows[2][1] = a.y * q; rows[2][2] = a.z * q; rows[2][3] = t * q - zn * q; }
        }
        float rays[4][3];
        CHECK(CornerRays(rows, rays));
        for (int i = 0; i < 4; ++i) {
            const float nx = (i & 1) ? 1.f : -1.f, ny = (i & 2) ? -1.f : 1.f;
            Vec3f want{ f.x + r.x * nx * tanX + u.x * ny * tanY,
                        f.y + r.y * nx * tanX + u.y * ny * tanY,
                        f.z + r.z * nx * tanX + u.z * ny * tanY };
            Vec3f got{ rays[i][0], rays[i][1], rays[i][2] };
            const float c = Dot(want, got) / (Len(want) * Len(got));
            CHECK(c > 0.99999f);
        }
        // the shader's bilinear mix at uv (0.5, 0.5) is the forward axis
        Vec3f mid{ (rays[0][0] + rays[1][0] + rays[2][0] + rays[3][0]) * 0.25f,
                   (rays[0][1] + rays[1][1] + rays[2][1] + rays[3][1]) * 0.25f,
                   (rays[0][2] + rays[1][2] + rays[2][2] + rays[3][2]) * 0.25f };
        CHECK(Dot(mid, f) / Len(mid) > 0.99999f);

        // a singular matrix (no view bound) is refused, not turned into NaN rays
        float zero[4][4]{};
        CHECK(!CornerRays(zero, rays));
    }

    // Cloud cells: botox's clouds.png bitmap, row 0 starts 0x00 0x00 0x80 0x3f -> cells 23..29
    // set, 22 clear. Wraps both ways with period 256.
    {
        CHECK(!CloudCell(22, 0));
        CHECK(CloudCell(23, 0) && CloudCell(29, 0));
        CHECK(!CloudCell(30, 0));
        CHECK(CloudCell(23 + 256, -256) == CloudCell(23, 0));
        CHECK(CloudCell(-233, 512) == CloudCell(23, 0));
        int n = 0;
        for (int z = 0; z < 256; ++z) for (int x = 0; x < 256; ++x) n += CloudCell(x, z);
        CHECK(n > 256 * 256 / 10 && n < 256 * 256 * 9 / 10);   // a cloud map, not empty or solid
    }

    // Sky constants: botox values. sky #78a7ff exactly, fog between fog #c0d8ff and sky,
    // sun angle at tick 1000 inside one turn, sun above the horizon (cos > 0 = mc up component).
    {
        float sky[3], fog[3];
        SkyColor(sky);
        FogColor(fog);
        CHECK(std::fabs(sky[0] - 0x78 / 255.f) < 1e-6f && std::fabs(sky[2] - 1.f) < 1e-6f);
        for (int i = 0; i < 3; ++i) CHECK(fog[i] >= sky[i] - 1e-6f && fog[i] <= 1.f);
        const float a = SunAngle();
        CHECK(std::isfinite(a) && a > 0.f && a < 6.2831853f && std::cos(a) > 0.f);
        CHECK(GammaScale(0.f) == 1.f && GammaScale(NAN) == 1.f && GammaScale(2.f) == 2.f);
    }

    // CloudOffset: result always lands inside [0, k_wrap), including negative-leaning inputs,
    // and never produces NaN/inf for a session that has run a long time (large curtime).
    {
        float out[2];
        CloudOffset(0.0, 0.f, 0.f, out);
        CHECK(out[0] >= 0.f && out[0] < (float)k_wrap);
        CHECK(out[1] >= 0.f && out[1] < (float)k_wrap);

        CloudOffset(100000.0, -50000.f, 80000.f, out);
        CHECK(std::isfinite(out[0]) && std::isfinite(out[1]));
        CHECK(out[0] >= 0.f && out[0] < (float)k_wrap);
        CHECK(out[1] >= 0.f && out[1] < (float)k_wrap);

        CloudOffset(-5.0, 0.f, 0.f, out);   // curtime clamps to 0 inside, must still be sane
        CHECK(std::isfinite(out[0]) && std::isfinite(out[1]));
    }

    // Drift: one second later, the x offset has moved by ticks*0.03 with ticks = 20/s -> 0.6
    // blocks/s (mc_clouds_math.h's own number), modulo the wrap.
    {
        float a[2], b[2];
        CloudOffset(10.0, 0.f, 0.f, a);
        CloudOffset(11.0, 0.f, 0.f, b);
        float d = b[0] - a[0];
        if (d < 0.f) d += (float)k_wrap;   // crossed the wrap boundary
        CHECK(std::fabs(d - 0.6f) < 1e-3f);
    }

    std::printf("%d/%d checks passed\n", s_checks - s_failed, s_checks);
    return s_failed ? 1 : 0;
}
