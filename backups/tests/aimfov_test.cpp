// Gate test for the bounding-sphere FOV reject (src/hacks/aim/aim_fovreject.h), the cheap
// test that lets CAimbotGlobal::PlayerBoneInFOV skip a full SetupBones on an enemy who cannot
// be in FOV.
//
// The reject is only allowed to be WRONG IN ONE DIRECTION. Saying "may be in FOV" about a
// target that is not costs a bone rebuild. Saying "outside" about a target that has even one
// hitbox inside the cone silently stops the aimbot from ever seeing that player, and nothing
// in game would point at this function. So the test brute-forces the safe direction: sample
// points all over the sphere, and any time SphereOutsideFov returns true, assert that not one
// sampled point is within the FOV limit.
//
// Build + run: cmake --build build --config Release --target aimfov_test && build\Release\aimfov_test.exe
#include "../src/hacks/aim/aim_fovreject.h"
#include <cstdio>
#include <cmath>
#include <random>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

static constexpr float kPi = 3.14159265358979323846f;

struct V3 { float x = 0, y = 0, z = 0; };
static V3 Sub(const V3& a, const V3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static float Len(const V3& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
static float Dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// angle in degrees between the view axis and the direction eye -> point. This is what
// Math::CalcAngle + Math::CalcFov compute together in the shipped path.
static float AngleTo(const V3& vEye, const V3& vAxis, const V3& vPoint)
{
    V3 d = Sub(vPoint, vEye);
    float l = Len(d);
    if (l <= 0.f) return 0.f;
    float c = Dot(d, vAxis) / l;
    if (c > 1.f) c = 1.f;
    if (c < -1.f) c = -1.f;
    return std::acos(c) * (180.f / kPi);
}

int main()
{
    // ---- hand cases: the boundaries the formula is built on ----------------------------

    // dead ahead, tiny sphere, generous FOV -> never rejected
    CHECK(!aimfov::SphereOutsideFov(0.f, 1000.f, 40.f, 30.f));
    // straight behind, tiny sphere -> rejected
    CHECK(aimfov::SphereOutsideFov(180.f, 1000.f, 40.f, 30.f));
    // r/d = 0.5 -> half angle 30 deg. Centre at 61 deg with a 30 deg FOV: nearest edge is
    // 31 deg, still outside -> reject. At 59 deg the edge is 29 deg, inside -> no reject.
    CHECK(aimfov::SphereOutsideFov(61.f, 100.f, 50.f, 30.f));
    CHECK(!aimfov::SphereOutsideFov(59.f, 100.f, 50.f, 30.f));
    // eye inside the sphere: no angular bound exists, never reject however far off-axis
    CHECK(!aimfov::SphereOutsideFov(180.f, 30.f, 50.f, 1.f));
    CHECK(!aimfov::SphereOutsideFov(180.f, 50.f, 50.f, 1.f));   // exactly on the surface
    // a zero radius is a point, and a point behind you with FOV 0 is a legitimate reject
    CHECK(aimfov::SphereOutsideFov(180.f, 100.f, 0.f, 0.f));
    // degenerate inputs must fail OPEN (do the work), never reject
    CHECK(!aimfov::SphereOutsideFov(180.f, 0.f, 0.f, 30.f));
    CHECK(!aimfov::SphereOutsideFov(180.f, 100.f, -5.f, 30.f));

    // FOV 180 accepts the whole world, so nothing may ever be rejected
    for (float fovTo = 0.f; fovTo <= 180.f; fovTo += 5.f)
        CHECK(!aimfov::SphereOutsideFov(fovTo, 500.f, 50.f, 180.f));

    // ---- brute force: a reject must mean NO sampled sphere point is in FOV -------------
    std::mt19937 rng(20260922);
    std::uniform_real_distribution<float> dPos(-2000.f, 2000.f);
    std::uniform_real_distribution<float> dRad(8.f, 120.f);
    std::uniform_real_distribution<float> dFov(0.5f, 180.f);
    std::uniform_real_distribution<float> dU(-1.f, 1.f);

    int nRejects = 0, nKeeps = 0;
    for (int iCase = 0; iCase < 200000; ++iCase)
    {
        const V3 vEye{ dPos(rng), dPos(rng), dPos(rng) };
        const V3 vCenter{ dPos(rng), dPos(rng), dPos(rng) };
        const float flRadius = dRad(rng);
        const float flFov = dFov(rng);

        // a random unit view axis
        V3 vAxis{ dU(rng), dU(rng), dU(rng) };
        const float flAxisLen = Len(vAxis);
        if (flAxisLen < 1e-3f) continue;
        vAxis = { vAxis.x / flAxisLen, vAxis.y / flAxisLen, vAxis.z / flAxisLen };

        const float flDist = Len(Sub(vCenter, vEye));
        if (flDist < 1e-3f) continue;
        const float flFovToCenter = AngleTo(vEye, vAxis, vCenter);

        const bool bReject = aimfov::SphereOutsideFov(flFovToCenter, flDist, flRadius, flFov);
        if (!bReject) { ++nKeeps; continue; }
        ++nRejects;

        // rejected: prove it. Fibonacci sphere over the SURFACE (the surface holds the
        // extreme angles; an interior point is always closer to the axis than some surface
        // point in the same plane), plus the centre itself.
        bool bAnyInside = AngleTo(vEye, vAxis, vCenter) < flFov;
        const int nSamples = 512;
        const float a = kPi * (3.f - std::sqrt(5.f));
        for (int n = 0; n < nSamples && !bAnyInside; ++n)
        {
            const float t = a * n;
            const float y = 1.f - (n / (nSamples - 1.f)) * 2.f;
            const float r = std::sqrt(1.f - y * y);
            const V3 vPoint{ vCenter.x + std::cos(t) * r * flRadius,
                             vCenter.y + y * flRadius,
                             vCenter.z + std::sin(t) * r * flRadius };
            if (AngleTo(vEye, vAxis, vPoint) < flFov)
                bAnyInside = true;
        }
        CHECK(!bAnyInside);
        if (bAnyInside)
        {
            std::printf("  case %d: fovToCenter=%.3f dist=%.3f radius=%.3f fov=%.3f\n",
                        iCase, flFovToCenter, flDist, flRadius, flFov);
            break;   // one counterexample is the whole answer, do not spam 200k lines
        }
    }

    // a run where nothing was ever rejected would pass the assert above while testing nothing
    CHECK(nRejects > 1000);
    CHECK(nKeeps > 1000);
    std::printf("brute force: %d rejects proven, %d kept\n", nRejects, nKeeps);

    std::printf("%s -- %d checks, %d failed\n", s_failed ? "FAILED" : "ok", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
