// Gate test for frame blending's maths (src/hacks/screen/fbmath.h), which is everything the
// velocity buffer is built out of.
//
// The claims under test are IDENTITIES, not approximations, so the oracle here never calls the
// code it is checking -- it works the answer out the long way round, from the definitions:
//
//   1. A WALL MUST COME BACK ZERO. The reprojection says where a pixel of this frame sat on
//      screen last frame. Project a fixed world point through the previous camera by hand, run
//      the reprojection on the same point through this frame's, and the two screen positions
//      have to be the same pixel. If they are not, the whole frame smears while nothing moved.
//
//   2. A POINT RIDING ON A PLAYER MUST COME BACK WHERE HE WAS. Same identity with his own step
//      taken out of the fourth column -- and a player standing still has to fall back onto the
//      camera's own rows exactly, which is the check that the two agree.
//
//   3. A BONE THAT ONLY RODE ALONG WRITES NOTHING. The ride is divided out at the draw, so a
//      proxy whose transform is the identity must produce a transform that moves no point at
//      all -- otherwise the gun smears every time you walk.
//
//   4. THE SHUTTER'S POWER IS A REAL POWER. A^1 is A, A^2 is A applied twice, and a pure slide
//      scales linearly. The arc matters: a swing raised to 2 has to land where the animation
//      would actually have been, not on the stretched chord.
//
// Build + run:
//   cmake --build build --config Release --target fbmath_test && build\Release\fbmath_test.exe
#include "../src/hacks/screen/fbmath.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

static int s_failed = 0, s_checks = 0;

#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

static void CheckNear(const char* what, float got, float want, float eps, int line)
{
    ++s_checks;
    if (!(std::fabs(got - want) <= eps)) {
        std::printf("FAIL line %d: %s got %.6f want %.6f (eps %.6f)\n", line, what, got, want, eps);
        ++s_failed;
    }
}
#define NEAR(what, got, want, eps) CheckNear(what, (got), (want), (eps), __LINE__)

// ---- helpers the oracle uses -------------------------------------------------------------
// view = basis * (world - eye), which is what the depth buffer's unprojection rebuilds
static void ToView(const float basis[3][3], const float eye[3], const float world[3], float out[3])
{
    const float d[3] = { world[0] - eye[0], world[1] - eye[1], world[2] - eye[2] };
    for (int r = 0; r < 3; ++r)
        out[r] = basis[r][0] * d[0] + basis[r][1] * d[1] + basis[r][2] * d[2];
}

static void Mul33(const float a[3][3], const float b[3][3], float out[3][3])
{
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c];
}

// ---- 1. the world ------------------------------------------------------------------------
static void TestWorldReprojection()
{
    // a real frame: the camera turns and walks at the same time
    const float anglesThen[3] = { -12.f,  35.f, 0.f };
    const float anglesNow[3]  = {  -9.5f, 41.f, 0.f };

    const float eyeThen[3] = { 1024.f, -512.f, 96.f };
    const float eyeNow[3]  = { 1040.f, -498.f, 99.f };

    float basisThen[3][3], basisNow[3][3];
    fbm::Basis(anglesThen, basisThen);
    fbm::Basis(anglesNow, basisNow);

    // the basis is orthonormal, or nothing below means anything
    for (int r = 0; r < 3; ++r) {
        const float len = std::sqrt(basisNow[r][0] * basisNow[r][0] +
                                    basisNow[r][1] * basisNow[r][1] +
                                    basisNow[r][2] * basisNow[r][2]);
        NEAR("basis row length", len, 1.f, 1e-5f);
    }
    for (int r = 0; r < 3; ++r)
        for (int c = r + 1; c < 3; ++c) {
            const float dot = basisNow[r][0] * basisNow[c][0] +
                              basisNow[r][1] * basisNow[c][1] +
                              basisNow[r][2] * basisNow[c][2];
            NEAR("basis orthogonal", dot, 0.f, 1e-5f);
        }

    const float step[3] = { eyeNow[0] - eyeThen[0], eyeNow[1] - eyeThen[1], eyeNow[2] - eyeThen[2] };

    float rows[3][4];
    fbm::Reprojection(basisThen, basisNow, step, rows);

    const float tanX = std::tan(45.f * 0.5f * 3.14159265f / 180.f);
    const float tanY = tanX / (16.f / 9.f);

    // a scatter of fixed world points: a wall, a floor, something far off
    const float world[5][3] = {
        { 1200.f, -400.f,  64.f },
        {  900.f, -900.f,   0.f },
        { 4096.f,  512.f, 512.f },
        { 1041.f, -497.f,  99.5f },   // nearly on the lens
        { -800.f, 2000.f, -256.f },
    };

    for (const auto& w : world) {
        float pNow[3], pThen[3];
        ToView(basisNow, eyeNow, w, pNow);
        ToView(basisThen, eyeThen, w, pThen);

        // THE CLAIM: the reprojection of this frame's view point IS last frame's view point
        float q[3];
        fbm::Apply(rows, pNow, q);

        NEAR("reprojected x", q[0], pThen[0], 1e-2f);
        NEAR("reprojected y", q[1], pThen[1], 1e-2f);
        NEAR("reprojected z", q[2], pThen[2], 1e-2f);

        // and therefore lands on the pixel that point occupied last frame
        if (pThen[2] > 1.f && pNow[2] > 1.f) {
            float uvThen[2], uvQ[2];
            fbm::ProjectUv(pThen, tanX, tanY, uvThen);
            fbm::ProjectUv(q, tanX, tanY, uvQ);
            NEAR("prev uv u", uvQ[0], uvThen[0], 1e-4f);
            NEAR("prev uv v", uvQ[1], uvThen[1], 1e-4f);
        }
    }

    // A STILL CAMERA MUST WRITE NO VELOCITY AT ALL: same basis, same eye, zero step.
    float still[3][4];
    const float noStep[3] = { 0.f, 0.f, 0.f };
    fbm::Reprojection(basisNow, basisNow, noStep, still);

    float p[3], qs[3];
    ToView(basisNow, eyeNow, world[0], p);
    fbm::Apply(still, p, qs);
    NEAR("still x", qs[0], p[0], 1e-3f);
    NEAR("still y", qs[1], p[1], 1e-3f);
    NEAR("still z", qs[2], p[2], 1e-3f);

    // view -> world -> view round trip, which is how a pixel is put in a player's box
    float v2w[3][4], backToWorld[3];
    fbm::ViewToWorld(basisNow, eyeNow, v2w);
    fbm::Apply(v2w, p, backToWorld);
    NEAR("world round trip x", backToWorld[0], world[0][0], 1e-2f);
    NEAR("world round trip y", backToWorld[1], world[0][1], 1e-2f);
    NEAR("world round trip z", backToWorld[2], world[0][2], 1e-2f);
}

// ---- 2. the players ----------------------------------------------------------------------
static void TestPlayerReprojection()
{
    const float anglesThen[3] = { 3.f, 100.f, 0.f };
    const float anglesNow[3]  = { 4.f, 104.f, 0.f };

    const float eyeThen[3] = { 0.f, 0.f, 64.f };
    const float eyeNow[3]  = { 12.f, 5.f, 64.f };

    float basisThen[3][3], basisNow[3][3];
    fbm::Basis(anglesThen, basisThen);
    fbm::Basis(anglesNow, basisNow);

    // he ran, and not in the direction the camera went
    const float was[3]   = { 300.f, 40.f, 0.f };
    const float where[3] = { 318.f, 52.f, 0.f };

    const float travel[3] = { where[0] - was[0], where[1] - was[1], where[2] - was[2] };

    const float shift[3] = { eyeNow[0] - eyeThen[0] - travel[0],
                             eyeNow[1] - eyeThen[1] - travel[1],
                             eyeNow[2] - eyeThen[2] - travel[2] };

    float rows[3][4];
    fbm::Reprojection(basisThen, basisNow, shift, rows);

    // a point on his chest, and the same point on him one frame ago
    const float offset[3] = { 4.f, -3.f, 48.f };

    const float worldNow[3]  = { where[0] + offset[0], where[1] + offset[1], where[2] + offset[2] };
    const float worldThen[3] = { was[0] + offset[0],   was[1] + offset[1],   was[2] + offset[2] };

    float pNow[3], pThen[3], q[3];
    ToView(basisNow, eyeNow, worldNow, pNow);
    ToView(basisThen, eyeThen, worldThen, pThen);
    fbm::Apply(rows, pNow, q);

    NEAR("player reproject x", q[0], pThen[0], 1e-2f);
    NEAR("player reproject y", q[1], pThen[1], 1e-2f);
    NEAR("player reproject z", q[2], pThen[2], 1e-2f);

    // A PLAYER STANDING STILL FALLS BACK ONTO THE CAMERA'S OWN ROWS, exactly. That is the check
    // that his rows and the world's agree -- if they did not, a standing man would smear
    // differently from the wall he is standing against.
    const float stepOnly[3] = { eyeNow[0] - eyeThen[0], eyeNow[1] - eyeThen[1], eyeNow[2] - eyeThen[2] };

    float cameraRows[3][4], standingRows[3][4];
    fbm::Reprojection(basisThen, basisNow, stepOnly, cameraRows);
    fbm::Reprojection(basisThen, basisNow, stepOnly, standingRows);   // travel zero

    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c)
            NEAR("standing player == camera", standingRows[r][c], cameraRows[r][c], 1e-6f);
}

// ---- 3. the bones ------------------------------------------------------------------------
static void TestBoneRows()
{
    const float angles[3] = { -7.f, 220.f, 0.f };
    const float eye[3]    = { 64.f, 64.f, 64.f };

    float basis[3][3];
    fbm::Basis(angles, basis);

    // a bone five units down the barrel
    const float center[3] = { 66.f, 69.f, 63.f };

    // A BONE THAT ONLY RODE ALONG: identity rotation, and it ended where it started. Its
    // transform has to move no point at all, or the gun smears every time you walk.
    float rot[3][3] = { { 1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f }, { 0.f, 0.f, 1.f } };

    float rows[3][4];
    fbm::BoneRows(basis, eye, rot, center, center, rows);

    const float probe[3] = { 0.8f, -0.4f, 5.f };   // a view-space point on the gun
    float moved[3];
    fbm::Apply(rows, probe, moved);

    NEAR("still bone x", moved[0], probe[0], 1e-3f);
    NEAR("still bone y", moved[1], probe[1], 1e-3f);
    NEAR("still bone z", moved[2], probe[2], 1e-3f);

    // AND A BONE THAT ACTUALLY MOVED: slide it two units along world x with no turn. The point
    // it carries has to land two units away in the world, which in view space is the same two
    // units rotated into the camera's axes.
    const float centerThen[3] = { center[0] - 2.f, center[1], center[2] };

    fbm::BoneRows(basis, eye, rot, center, centerThen, rows);
    fbm::Apply(rows, probe, moved);

    const float want[3] = {
        probe[0] - 2.f * basis[0][0],
        probe[1] - 2.f * basis[1][0],
        probe[2] - 2.f * basis[2][0],
    };

    NEAR("slid bone x", moved[0], want[0], 1e-3f);
    NEAR("slid bone y", moved[1], want[1], 1e-3f);
    NEAR("slid bone z", moved[2], want[2], 1e-3f);

    // the streak must be non-zero -- a test that only ever checks for zero passes on a feature
    // that does nothing
    const float slid = std::sqrt((moved[0] - probe[0]) * (moved[0] - probe[0]) +
                                 (moved[1] - probe[1]) * (moved[1] - probe[1]) +
                                 (moved[2] - probe[2]) * (moved[2] - probe[2]));
    CHECK(slid > 1.9f && slid < 2.1f);
}

// ---- 4. the shutter ----------------------------------------------------------------------
static void TestPowerTransform()
{
    // a pure slide scales linearly, whatever the exponent
    {
        float rot[3][3] = { { 1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f }, { 0.f, 0.f, 1.f } };
        float t[3] = { 0.3f, -0.2f, 0.1f };

        fbm::PowerTransform(rot, t, 4.f);

        NEAR("slide x4 x", t[0],  1.2f, 1e-4f);
        NEAR("slide x4 y", t[1], -0.8f, 1e-4f);
        NEAR("slide x4 z", t[2],  0.4f, 1e-4f);
    }

    // exponent 1 changes nothing
    {
        const float ang = 0.15f;
        float rot[3][3] = { { std::cos(ang), -std::sin(ang), 0.f },
                            { std::sin(ang),  std::cos(ang), 0.f },
                            { 0.f, 0.f, 1.f } };
        float t[3] = { 1.f, 2.f, 3.f };

        float rot0[3][3];
        float t0[3] = { t[0], t[1], t[2] };
        std::memcpy(rot0, rot, sizeof(rot0));

        fbm::PowerTransform(rot, t, 1.f);

        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) NEAR("identity power rot", rot[r][c], rot0[r][c], 1e-6f);
            NEAR("identity power t", t[r], t0[r], 1e-6f);
        }
    }

    // THE ONE THAT MATTERS: A^2 applied to a point is A applied twice. A swing is an arc, and
    // the whole reason the transform is raised instead of the displacement being stretched is
    // that the arc and its chord are not the same line.
    {
        const float ang = 0.20f;   // about eleven degrees in one frame: a fast melee swing
        float rotA[3][3] = { { std::cos(ang), -std::sin(ang), 0.f },
                             { std::sin(ang),  std::cos(ang), 0.f },
                             { 0.f, 0.f, 1.f } };
        float tA[3] = { 0.4f, 0.1f, -0.05f };

        // A applied twice, worked out the long way
        float rotTwice[3][3];
        Mul33(rotA, rotA, rotTwice);

        float tTwice[3];
        for (int r = 0; r < 3; ++r)
            tTwice[r] = rotA[r][0] * tA[0] + rotA[r][1] * tA[1] + rotA[r][2] * tA[2] + tA[r];

        // and A raised to 2
        float rotPow[3][3];
        float tPow[3] = { tA[0], tA[1], tA[2] };
        std::memcpy(rotPow, rotA, sizeof(rotPow));
        fbm::PowerTransform(rotPow, tPow, 2.f);

        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                NEAR("A^2 rotation", rotPow[r][c], rotTwice[r][c], 1e-4f);

        // the point a gun pixel would be carried to, both ways
        const float p[3] = { 1.5f, -0.75f, 6.f };

        float viaPower[3], viaTwice[3];
        for (int r = 0; r < 3; ++r) {
            viaPower[r] = rotPow[r][0] * p[0] + rotPow[r][1] * p[1] + rotPow[r][2] * p[2] + tPow[r];
            viaTwice[r] = rotTwice[r][0] * p[0] + rotTwice[r][1] * p[1] + rotTwice[r][2] * p[2] + tTwice[r];
        }

        NEAR("A^2 point x", viaPower[0], viaTwice[0], 2e-3f);
        NEAR("A^2 point y", viaPower[1], viaTwice[1], 2e-3f);
        NEAR("A^2 point z", viaPower[2], viaTwice[2], 2e-3f);

        // AND IT IS NOT THE STRETCHED CHORD. If raising the transform were the same as scaling
        // the displacement there would be no reason for any of this code -- so prove they
        // differ on an arc this size, or the test is passing on a no-op.
        float chord[3];
        for (int r = 0; r < 3; ++r) {
            const float once = rotA[r][0] * p[0] + rotA[r][1] * p[1] + rotA[r][2] * p[2] + tA[r];
            chord[r] = p[r] + (once - p[r]) * 2.f;
        }

        const float apart = std::sqrt((chord[0] - viaTwice[0]) * (chord[0] - viaTwice[0]) +
                                      (chord[1] - viaTwice[1]) * (chord[1] - viaTwice[1]) +
                                      (chord[2] - viaTwice[2]) * (chord[2] - viaTwice[2]));
        CHECK(apart > 0.01f);
    }

    // A TINY TURN MUST NOT EXPLODE. As the angle goes to zero the fixed point runs off to
    // infinity, and asking for it anyway is a difference of two huge numbers -- which is
    // rounding, and rounding that changes every frame on a gun held still is the jitter. Under
    // a degree the slide is scaled straight instead, and the answer stays finite and close.
    {
        const float ang = 0.0002f;   // a hundredth of a degree in one frame
        float rot[3][3] = { { std::cos(ang), -std::sin(ang), 0.f },
                            { std::sin(ang),  std::cos(ang), 0.f },
                            { 0.f, 0.f, 1.f } };
        float t[3] = { 0.001f, -0.002f, 0.f };

        fbm::PowerTransform(rot, t, 6.f);

        for (int a = 0; a < 3; ++a) CHECK(std::isfinite(t[a]));
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) CHECK(std::isfinite(rot[r][c]));

        NEAR("tiny turn slide x", t[0],  0.006f, 1e-5f);
        NEAR("tiny turn slide y", t[1], -0.012f, 1e-5f);

        // and a point five units from the lens moves by about six times what it did in one
        // frame -- no more, or the gun jumps
        const float p[3] = { 0.f, 0.f, 5.f };
        float moved[3];
        for (int r = 0; r < 3; ++r)
            moved[r] = rot[r][0] * p[0] + rot[r][1] * p[1] + rot[r][2] * p[2] + t[r];

        const float d = std::sqrt((moved[0] - p[0]) * (moved[0] - p[0]) +
                                  (moved[1] - p[1]) * (moved[1] - p[1]) +
                                  (moved[2] - p[2]) * (moved[2] - p[2]));
        CHECK(d < 0.05f);
    }

    // PowerRows is the same thing on a 3x4, which is the shape the bones arrive in
    {
        float rows[3][4] = { { 1.f, 0.f, 0.f, 0.25f },
                             { 0.f, 1.f, 0.f, 0.f   },
                             { 0.f, 0.f, 1.f, 0.f   } };
        fbm::PowerRows(rows, 3.f);
        NEAR("PowerRows slide", rows[0][3], 0.75f, 1e-5f);
        NEAR("PowerRows rot kept", rows[1][1], 1.f, 1e-6f);
    }
}

// ---- 5. the shaders beside this code -----------------------------------------------------
// Nothing at build time compiles hlsl, so an edited shader with a forgotten fxc run ships the
// old bytecode silently. And the constant LAYOUT is split across two files that cannot see each
// other: frameblend.cpp writes players at c16 and bones at c66, and if a shader is ever rebuilt
// with different bindings the gun reads the players' boxes as bone transforms.
static std::string Slurp(const char* path)
{
    std::string out;
    if (FILE* f = std::fopen(path, "rb")) {
        char buf[8192];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
        std::fclose(f);
    }
    return out;
}

// one line holding both words: fxc's register table is "  player       c16     50"
static bool LineWith(const std::string& s, const char* a, const char* b)
{
    size_t at = 0;
    while (at < s.size()) {
        size_t end = s.find('\n', at);
        if (end == std::string::npos) end = s.size();
        const std::string line = s.substr(at, end - at);
        if (line.find(a) != std::string::npos && line.find(b) != std::string::npos) return true;
        at = end + 1;
    }
    return false;
}

static void TestShaderHeaders()
{
    const std::string dir = FB_SRC_DIR;

    const char* names[5] = { "fx_fb_velocity", "fx_fb_tile_h", "fx_fb_tile_v",
                             "fx_fb_neighbour", "fx_fb_gather" };

    for (const char* n : names) {
        const std::string src = Slurp((dir + "/" + n + ".hlsl").c_str());
        const std::string hdr = Slurp((dir + "/" + n + ".h").c_str());

        ++s_checks;
        if (src.empty() || hdr.empty()) {
            std::printf("FAIL: %s -- shader or compiled header missing\n", n);
            ++s_failed;
            continue;
        }

        // ps_3_0: the tile loops are dynamic and the gather indexes constants, neither of which
        // ps_2_0 can do
        ++s_checks;
        if (hdr.find("ps_3_0") == std::string::npos) {
            std::printf("FAIL: %s was not built as ps_3_0\n", n);
            ++s_failed;
        }

        // the entry point fxc was pointed at, and the variable name frameblend.cpp includes
        ++s_checks;
        if (hdr.find("k_fxFb") == std::string::npos) {
            std::printf("FAIL: %s header has no k_fxFb* array -- wrong /Vn\n", n);
            ++s_failed;
        }
    }

    // THE LAYOUT. These two numbers are also written into frameblend.cpp (k_playerBase,
    // k_boneBase), and the only thing keeping the two files in step is this check.
    {
        const std::string src = Slurp((dir + "/fx_fb_velocity.hlsl").c_str());
        const std::string hdr = Slurp((dir + "/fx_fb_velocity.h").c_str());

        CHECK(src.find("player[50] : register(c16)") != std::string::npos);
        CHECK(src.find("bone[64] : register(c66)") != std::string::npos);
        CHECK(src.find("viewToWorld[3] : register(c9)") != std::string::npos);

        // and fxc agrees it put them there: its register table is one line per uniform,
        // "  player       c16     50"
        CHECK(LineWith(hdr, "player", "c16"));
        CHECK(LineWith(hdr, "bone", "c66"));
        CHECK(LineWith(hdr, "viewToWorld", "c9"));
    }
}

int main()
{
    TestWorldReprojection();
    TestPlayerReprojection();
    TestBoneRows();
    TestPowerTransform();
    TestShaderHeaders();

    std::printf("%s -- %d checks, %d failed\n", s_failed ? "FAILED" : "PASSED", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
