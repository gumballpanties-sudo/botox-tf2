// Gate test for Flip World's engine-free logic (src/hacks/visuals/flipmath.h).
// Build + run:  cmake --build build --config Release --target flipworld_test && build\Release\flipworld_test.exe
// Every model below is written from the disassembly quoted next to it, not from memory, so a
// change that breaks the real game's contract breaks a CHECK here.
#include "../src/hacks/visuals/flipmath.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

static int s_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

// sdk view_scene.cpp FrustumTransform + cdll_util.cpp GetVectorInScreenSpace, verbatim math.
static bool ToScreen(const float m[4][4], float px, float py, float pz, float W, float H, float& sx, float& sy)
{
    float x = m[0][0] * px + m[0][1] * py + m[0][2] * pz + m[0][3];
    float y = m[1][0] * px + m[1][1] * py + m[1][2] * pz + m[1][3];
    float w = m[3][0] * px + m[3][1] * py + m[3][2] * pz + m[3][3];
    const bool behind = w < 0.001f;
    if (behind) { x *= 100000; y *= 100000; }
    else        { x /= w; y /= w; }
    sx = 0.5f * (1.0f + x) * W;
    sy = 0.5f * (1.0f - y) * H;
    return !behind;
}

static bool Near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) < eps; }

// materialsystem.dll rva 0x7e940 (CMatRenderContext::DrawScreenSpaceRectangle's body): the quad
// spans pixel edges destx-0.5 .. destx+w-0.5 (d3d9 centres sit on integers), and
//   u0 = (x0 + 0.5 - 0.5*tpp) / texW,  u1 = (x1 + 0.5 + 0.5*tpp) / texW,  tpp = (x1-x0)/(w-1)
// (0 when w <= 1), same for v. Returns the texel the pixel centre (px, py) samples.
static void SampleTexel(int destx, int desty, int w, int h, const float src[4], int texW, int texH,
                        int px, int py, int& tx, int& ty)
{
    const float tppX = w > 1 ? (src[2] - src[0]) / float(w - 1) : 0.f;
    const float tppY = h > 1 ? (src[3] - src[1]) / float(h - 1) : 0.f;
    const float u0 = (src[0] + 0.5f - 0.5f * tppX) / texW, u1 = (src[2] + 0.5f + 0.5f * tppX) / texW;
    const float v0 = (src[1] + 0.5f - 0.5f * tppY) / texH, v1 = (src[3] + 0.5f + 0.5f * tppY) / texH;
    const float fx = (float(px) - (destx - 0.5f)) / float(w);
    const float fy = (float(py) - (desty - 0.5f)) / float(h);
    tx = int(std::floor((u0 + fx * (u1 - u0)) * texW));
    ty = int(std::floor((v0 + fy * (v1 - v0)) * texH));
}

// sdk tf_hud_damageindicator.cpp:151 / tf_hud_mediccallers.cpp:229, as compiled at client rva
// 0x533e60 (float math, atan2 on doubles). shippedSign -1 is the game, +1 the patched window.
static void DamagePosition(float dx, float dy, float yawDeg, float radius, int W, int H,
                           float shippedSign, float& xpos, float& ypos, float& rot)
{
    const double pi = 3.14159265358979323846;
    const float fx = float(std::cos(yawDeg * pi / 180.0)), fy = float(std::sin(yawDeg * pi / 180.0));
    const float rx = -fy, ry = fx;                     // CrossProduct(up, forward) = left
    const float side = dx * rx + dy * ry, front = dx * fx + dy * fy;
    xpos = radius * shippedSign * side;
    ypos = radius * -front;
    rot = float((std::atan2(double(xpos), double(ypos)) + pi) * (180.0 / pi));
    const double yawRad = double(-rot) * (pi / 180.0);
    const float ca = float(std::cos(yawRad)), sa = float(std::sin(yawRad));
    xpos = float(int(float(W / 2) + radius * sa));
    ypos = float(int(float(H / 2) - radius * ca));
}

// client.dll ConVar as disassembled: primary 12 InternalSetIntValue returns when the int is
// unchanged; primary 14 ChangeStringValue copies the new string in and runs the change callbacks
// only when strcmp(old, new) != 0; the engine's global callback (rva 0x1763f0) then sends
// NET_SetConVar with the cvar's string. engine rva 0x1777b0 WriteVariables skips cvars without
// FCVAR_ARCHIVE (0x80) and those whose string equals the default, and prints m_pszString.
struct SimCvar {
    int   n = 0;
    float f = 0.f;
    char  str[8] = "0";
    const char* def = "0";
    int   flags = 0x400280;
    int   sends = 0;
    char  sent = 0;             // last value the server was told

    void ChangeString(const char* s) {
        char old[8]; std::memcpy(old, str, sizeof(old));
        std::snprintf(str, sizeof(str), "%s", s);
        if (std::strcmp(old, str) != 0) { ++sends; sent = str[0]; }
    }
    void SetInt(int v) {                      // IConVar 0
        if (v == n) return;
        n = v; f = float(v);
        char t[8]; std::snprintf(t, sizeof(t), "%d", v);
        ChangeString(t);
    }
    void SetStr(const char* s) {              // IConVar 2: console, cfg
        f = float(std::atof(s)); n = int(f);
        ChangeString(s);
    }
    std::string WriteConfig() const {         // CCvarUtilities::WriteVariables
        if (!(flags & 0x80) || std::strcmp(str, def) == 0) return "";
        return std::string("cl_flipviewmodels \"") + str + "\"";
    }
};

// FlipTick's hand half, as both flipworld.cpp run it.
static void HandTick(SimCvar& cv, bool wanted, int target, int& pref)
{
    const int want = FlipVmStep(wanted, cv.n ? 1 : 0, target, pref);
    if (want >= 0) cv.SetInt(want);
}

static std::string ConfigWithGuard(SimCvar& cv, int pref)
{
    std::string out;
    FlipCfgWrite(cv.str, pref, [&] { out = cv.WriteConfig(); });
    return out;
}

static int PatchSigTokens(const char* sig, const char* tokens[64])
{
    static char buf[512];
    std::snprintf(buf, sizeof(buf), "%s", sig);
    int n = 0;
    for (char* t = std::strtok(buf, " "); t && n < 64; t = std::strtok(nullptr, " ")) tokens[n++] = t;
    return n;
}

int main()
{
    // ---- W2S mirror ----------------------------------------------------------------------------
    // A real-shaped world-to-screen: camera at C looking down +x, 90 deg hfov, 16:9.
    // Source view space: x forward, y left, z up -> clip x = -(y-Cy), clip y = (z-Cz), w = x-Cx.
    // Off the origin on purpose, so the translation column is live and has to mirror too.
    const float W = 1920.f, H = 1080.f, Cx = -300.f, Cy = 120.f, Cz = 64.f, A = W / H;
    float proj[4][4] = {
        { 0.f, -1.f,   0.f,   Cy       },
        { 0.f,  0.f,   A,    -A * Cz   },
        { 0.f,  0.f,   0.f,   0.f      },
        { 1.f,  0.f,   0.f,  -Cx       },
    };
    CHECK(FlipIsProjection(proj));

    float mirrored[4][4];
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) mirrored[r][c] = proj[r][c];
    FlipMirrorW2S(mirrored);

    // Points left of, right of, on, above and below the centre line: every one lands on W - x.
    const float pts[][3] = { {100, 30, 0}, {100, -45, 10}, {100, 0, -20}, {500, 200, 60}, {10, -9.9f, 5} };
    for (auto& p : pts) {
        float ax, ay, bx, by;
        const bool onA = ToScreen(proj,     Cx + p[0], Cy + p[1], Cz + p[2], W, H, ax, ay);
        const bool onB = ToScreen(mirrored, Cx + p[0], Cy + p[1], Cz + p[2], W, H, bx, by);
        CHECK(onA);                                   // the fixture really is in front of the camera
        CHECK(onA == onB);
        CHECK(Near(bx, W - ax));
        CHECK(Near(by, ay));
    }
    // Behind the camera: still reported behind, and still thrown to the mirrored side.
    {
        float ax, ay, bx, by;
        const bool onA = ToScreen(proj,     Cx - 50, Cy + 20, Cz, W, H, ax, ay);
        const bool onB = ToScreen(mirrored, Cx - 50, Cy + 20, Cz, W, H, bx, by);
        CHECK(!onA && !onB);
        CHECK((ax - W * 0.5f) * (bx - W * 0.5f) < 0.f);
    }
    // Mirroring twice is the identity.
    FlipMirrorW2S(mirrored);
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) CHECK(mirrored[r][c] == proj[r][c]);

    // A world-to-view matrix is refused.
    const float view[4][4] = { {0, -1, 0, 0}, {0, 0, 1, 0}, {1, 0, 0, 0}, {0, 0, 0, 1} };
    CHECK(!FlipIsProjection(view));

    // The world pass on the game thread sees the true matrix; everything else the mirrored one.
    CHECK( FlipW2SMirrored(true,  true,  false));      // HUD paint / think
    CHECK(!FlipW2SMirrored(true,  true,  true));       // beams, sprite glows, the unmirrored gun's beams
    CHECK( FlipW2SMirrored(true,  false, true));       // render-thread overlays
    CHECK(!FlipW2SMirrored(false, false, false));      // mirror not drawing: nothing

    // ---- frame mirror texels ---------------------------------------------------------------------
    // Full screen and a mat_viewportscale sub-rectangle: every pixel samples its exact mirror
    // inside the rectangle, the row it sits on, and the HUD's continuous W - x agrees.
    {
        struct Rect { int x, y, w, h, texW, texH; };
        const Rect rects[] = { { 0, 0, 1920, 1080, 1920, 1080 }, { 0, 0, 960, 540, 1920, 1080 },
                               { 37, 11, 101, 7, 256, 64 }, { 0, 0, 3, 2, 3, 2 } };
        for (const Rect& r : rects) {
            float src[4];
            FlipMirrorSrcRect(r.x, r.y, r.w, r.h, src);
            int bad = 0;
            for (int py = r.y; py < r.y + r.h; ++py)
                for (int px = r.x; px < r.x + r.w; px += (r.w > 200 ? 7 : 1)) {
                    int tx, ty;
                    SampleTexel(r.x, r.y, r.w, r.h, src, r.texW, r.texH, px, py, tx, ty);
                    if (tx != r.x + r.w - 1 - (px - r.x) || ty != py) ++bad;
                }
            CHECK(bad == 0);
        }
        // Round 1's full-screen call, src (w,0)-(0,h), is off by a row near the bottom: the model
        // above is sharp enough to tell.
        const float old[4] = { 4.f, 0.f, 0.f, 4.f };
        int wrongRow = 0;
        for (int py = 0; py < 4; ++py) { int tx, ty; SampleTexel(0, 0, 4, 4, old, 4, 4, 1, py, tx, ty); if (ty != py) ++wrongRow; }
        CHECK(wrongRow > 0);
        // A pixel column c is the continuous span [c, c+1); its mirror W - x is [W-1-c, W-c).
        CHECK(int(std::floor(W - (100.f + 0.5f))) == int(W) - 1 - 100);
    }

    // ---- frame tracking ----
    {
        FlipFrameTrack t;
        CHECK(!t.mirrored);
        FlipTrackFrameStart(t); CHECK(t.inWorldPass);
        FlipTrackWorldPass(t);  CHECK(t.inWorldPass);   // world drawn, mirror not yet: beams still see truth
        FlipTrackMirror(t);     CHECK(t.mirrored && t.inWorldPass);    // DrawViewModels follows: still the 3D pass
        FlipTrackTwoD(t);       CHECK(t.mirrored && !t.inWorldPass);   // HUD: mirrored W2S
        FlipTrackFrameStart(t); CHECK(t.mirrored);      // a hitch between two mirrored frames: still mirrored
        FlipTrackWorldPass(t);  FlipTrackMirror(t); FlipTrackTwoD(t);
        for (int f = 0; f < 240; ++f) {                 // freezecam: no world pass, 2D pushed every frame
            FlipTrackFrameStart(t); CHECK(t.inWorldPass);
            FlipTrackTwoD(t);       CHECK(!t.inWorldPass);
        }
        CHECK(t.mirrored);                              // the frozen capture was taken after the mirror
        FlipTrackFrameStart(t); FlipTrackWorldPass(t); FlipTrackTwoD(t);   // world drawn, no mirror (hook gone)
        CHECK(t.mirrored);                              // decided at the next frame start ...
        FlipTrackFrameStart(t); CHECK(!t.mirrored);     // ... not mirrored
        FlipTrackWorldPass(t); FlipTrackMirror(t);  CHECK(t.mirrored);
        FlipTrackReset(t);      CHECK(!t.mirrored);
    }

    // ---- angle-placed HUD patches ------------------------------------------------------------------
    // The patched side negation IS the mirror: patched(delta) == shipped(delta mirrored across the
    // view's forward axis), and on screen that is W - x and 360 - rotation.
    {
        const int SW = 1920, SH = 1080;
        const float radius = 100.f;
        const float yaws[] = { 0.f, 37.5f, -121.25f, 179.9f };
        const float deltas[][2] = { { 1.f, 0.f }, { 0.3f, 0.95f }, { -0.6f, 0.8f }, { -0.99f, -0.14f }, { 0.2f, -0.98f } };
        for (float yaw : yaws)
            for (auto& d : deltas) {
                const double pi = 3.14159265358979323846;
                const float fx = float(std::cos(yaw * pi / 180.0)), fy = float(std::sin(yaw * pi / 180.0));
                const float front = d[0] * fx + d[1] * fy;
                const float mx = 2.f * front * fx - d[0], my = 2.f * front * fy - d[1];   // mirror across forward
                float sx, sy, sr, px, py, pr, mxp, myp, mr;
                DamagePosition(d[0], d[1], yaw, radius, SW, SH, -1.f, sx, sy, sr);    // shipped
                DamagePosition(d[0], d[1], yaw, radius, SW, SH, +1.f, px, py, pr);    // patched
                DamagePosition(mx, my, yaw, radius, SW, SH, -1.f, mxp, myp, mr);      // shipped, mirrored world
                CHECK(std::fabs(px - mxp) <= 1.f && std::fabs(py - myp) <= 1.f);
                CHECK(std::fabs(py - sy) <= 1.f);
                // The mirrored pixel is W - 1 - x (FlipMirrorSrcRect), whenever the position is not
                // within float noise of a whole pixel.
                const float v = sx - float(SW / 2);
                const float raw = float(SW / 2) + radius * float(std::sin(-double(sr) * pi / 180.0));
                if (std::fabs(raw - std::round(raw)) > 1e-3f && std::fabs(v) < radius + 1.f)
                    CHECK(px == float(SW - 1) - sx);
                const float dr = std::fmod(pr + sr, 360.f);
                CHECK(dr < 0.01f || dr > 359.99f);
            }
    }
    // CTFArrowPanel::GetAngleRotation (flagstatus.cpp:167) as compiled at client 0x53f210: the angle is
    // negated when the flag is on the view's right-hand dot < 0 (jae skips the negation); the patch
    // takes jbe instead. patched(flag) == shipped(flag mirrored across forward), for every direction.
    {
        auto arrow = [](float fx, float fy, float dx, float dy, bool patched) {
            const float rx = fy, ry = -fx;                            // AngleVectors right = (sin y, -cos y)
            float ang = std::acos(std::fmin(1.f, std::fmax(-1.f, dx * fx + dy * fy)));
            const float dr = dx * rx + dy * ry;
            const bool skip = patched ? !(dr > 0.f) : (dr >= 0.f);   // jbe / jae after comiss dr, 0
            if (!skip) ang = -ang;
            return ang * 57.29578f;
        };
        const double pi = 3.14159265358979323846;
        for (float yaw = -170.f; yaw < 180.f; yaw += 37.f)
            for (float a = -175.f; a < 180.f; a += 23.f) {
                const float fx = float(std::cos(yaw * pi / 180)), fy = float(std::sin(yaw * pi / 180));
                const float dx = float(std::cos(a * pi / 180)), dy = float(std::sin(a * pi / 180));
                const float front = dx * fx + dy * fy;
                const float mx = 2.f * front * fx - dx, my = 2.f * front * fy - dy;
                CHECK(std::fabs(arrow(fx, fy, dx, dy, true) - arrow(fx, fy, mx, my, false)) < 0.05f);
                CHECK(std::fabs(arrow(fx, fy, dx, dy, true) + arrow(fx, fy, dx, dy, false)) < 0.05f);
            }
    }
    // PASS Time's arrow (passtime_ball_offscreen_arrow.cpp:163): hud = (-y / aspect, -z), angle =
    // atan2(hud.y, hud.x). The patch loads +1 instead of -1 into the x factor: that is the view-space
    // point mirrored left-right (y -> -y), and on screen the angle becomes pi - angle.
    {
        const float aspect = 16.f / 9.f;
        for (float y = -300.f; y <= 300.f; y += 75.f)
            for (float z = -200.f; z <= 200.f; z += 100.f) {
                if (y == 0.f && z == 0.f) continue;
                const float shippedMirror = std::atan2(-z, (-1.f / aspect) * -y);
                const float patched       = std::atan2(-z, (+1.f / aspect) * y);
                CHECK(std::fabs(patched - shippedMirror) < 1e-5f);
            }
    }
    // The table itself: small windows, a real change in each, only changed bytes differ, and every
    // changed byte that the signature covers is a wildcard there.
    for (int i = 0; i < k_flipHudPatchCount; ++i) {
        const FlipHudPatch& p = k_flipHudPatches[i];
        CHECK(p.len > 0 && p.len <= 12 && p.at >= 0);
        const char* tok[64];
        const int n = PatchSigTokens(p.sig, tok);
        CHECK(n >= 12);
        int changed = 0;
        for (int b = 0; b < p.len; ++b) {
            if (p.orig[b] != p.flip[b]) ++changed;
            const int s = p.at + b;
            if (s >= n) continue;
            if (p.orig[b] != p.flip[b]) CHECK(std::strcmp(tok[s], "?") == 0);
            else if (std::strcmp(tok[s], "?") != 0) CHECK(unsigned(std::strtoul(tok[s], nullptr, 16)) == p.orig[b]);
        }
        CHECK(changed >= 1 && changed <= 3);
        CHECK(FlipHudPatchBytes(p, true) == p.flip && FlipHudPatchBytes(p, false) == p.orig);
    }

    // ---- input ---------------------------------------------------------------------------------------
    {
        float side = -450.f; int buttons = k_flipInMoveLeft | 1;             // A + attack
        FlipMirrorCmd(side, buttons);
        CHECK(side == 450.f && buttons == (k_flipInMoveRight | 1));
        FlipMirrorCmd(side, buttons);                                          // twice = identity
        CHECK(side == -450.f && buttons == (k_flipInMoveLeft | 1));
        side = 0.f; buttons = k_flipInMoveLeft | k_flipInMoveRight;            // both held: still both
        FlipMirrorCmd(side, buttons);
        CHECK(buttons == (k_flipInMoveLeft | k_flipInMoveRight));
        buttons = 1 << 3;                                                      // W only: untouched
        FlipMirrorCmd(side, buttons);
        CHECK(buttons == (1 << 3));
    }
    {
        float held = 0.f, out = 0.f;
        // Inverted-look player, flip world never on: never written.
        for (int i = 0; i < 3; ++i) CHECK(!FlipYawStep(false, -0.022f, held, &out));
        // Flip on: held positive; stable; off: their negative back.
        CHECK(FlipYawStep(true, -0.022f, held, &out) && out == 0.022f);
        CHECK(!FlipYawStep(true, 0.022f, held, &out));
        CHECK(FlipYawStep(false, 0.022f, held, &out) && out == -0.022f && held == 0.f);
        CHECK(!FlipYawStep(false, -0.022f, held, &out));
        // Normal player; changes m_yaw while held: that is the new own value.
        CHECK(FlipYawStep(true, 0.022f, held, &out) && out == -0.022f);
        CHECK(FlipYawStep(true, 0.03f, held, &out) && out == -0.03f);
        CHECK(FlipYawStep(false, -0.03f, held, &out) && out == 0.03f);
        // Changed right before release: theirs stays.
        CHECK(FlipYawStep(true, 0.022f, held, &out));
        CHECK(!FlipYawStep(false, 0.05f, held, &out) && held == 0.f);
        // m_yaw 0: nothing to mirror, nothing held.
        CHECK(!FlipYawStep(true, 0.f, held, &out) && held == 0.f);
    }

    // ---- hand: which one ---------------------------------------------------------------------------
    {
        int t = 7;
        CHECK( FlipHandTarget(true,  true,  false, false, -1, t) && t == -1);   // live: the inverse
        CHECK(!FlipHandTarget(false, true,  false, false, -1, t));              // mirror not drawing
        CHECK(!FlipHandTarget(true,  false, false, false, -1, t));              // no config guard
        CHECK(!FlipHandTarget(true,  true,  true,  false, -1, t));              // SourceTV
        CHECK(!FlipHandTarget(true,  true,  true,  true,   1, t));              // SourceTV demo
        CHECK( FlipHandTarget(true,  true,  false, true,   1, t) && t == 1);    // POV demo, recorded left
        CHECK( FlipHandTarget(true,  true,  false, true,   0, t) && t == 0);
        CHECK(!FlipHandTarget(true,  true,  false, true,  -1, t) && t == -1);   // recorded hand unknown
    }

    // ---- client int: the other hand while held, the string's hand otherwise ----
    CHECK(FlipClientHand(true,  1, 1) == 0);      // held left (right-hander): gun on the right
    CHECK(FlipClientHand(true,  1, 0) == -1);     // already poked: silent
    CHECK(FlipClientHand(true,  0, 0) == 1);      // left-hander, held right
    CHECK(FlipClientHand(false, 0, 1) == 0);      // released: back to the string
    CHECK(FlipClientHand(false, 1, 1) == -1);
    CHECK(FlipClientHand(false, 1, 2) == -1);     // only the bool counts
    CHECK(FlipClientHand(true,  0, 2) == -1);

    // ---- weapon sway: CTFViewModel::CalcViewModelLag's offset, right term reflected ----
    {
        const double pi = 3.14159265358979323846;
        const float angs[][3] = { { 0, 0, 0 }, { 12.5f, 97, 0 }, { -63, -170, 4.5f }, { 88, 33, -12 } };
        const float lag[][3]  = { { 0.4f, 0.9f, -0.2f }, { -1.1f, -0.3f, 0.05f }, { 0, 2, 0 } };
        for (auto& a : angs)
            for (auto& l : lag) {
                // AngleVectors, mathlib_base.cpp:941-955, written out on its own
                const double sp = std::sin(a[0] * pi / 180), cp = std::cos(a[0] * pi / 180);
                const double sy = std::sin(a[1] * pi / 180), cy = std::cos(a[1] * pi / 180);
                const double sr = std::sin(a[2] * pi / 180), cr = std::cos(a[2] * pi / 180);
                const double f[3] = { cp * cy, cp * sy, -sp };
                const double r[3] = { -1 * sr * sp * cy + -1 * cr * -sy, -1 * sr * sp * sy + -1 * cr * cy, -1 * sr * cp };
                const double u[3] = { cr * sp * cy + -sr * -sy, cr * sp * sy + -sr * cy, cr * cp };
                const float before[3] = { 100.f, -250.f, 64.f };
                float game[3], want[3];
                for (int i = 0; i < 3; ++i) {   // tf_viewmodel.cpp:117 with vForwardDiff = l, then y negated
                    game[i] = float(before[i] + f[i] * l[0] + r[i] * -l[1] + u[i] * l[2]);
                    want[i] = float(before[i] + f[i] * l[0] + r[i] *  l[1] + u[i] * l[2]);
                }
                FlipMirrorSway(before, game, a);
                CHECK(Near(game[0], want[0], 1e-3f) && Near(game[1], want[1], 1e-3f) && Near(game[2], want[2], 1e-3f));
            }
        float o[3] = { 5, 6, 7 }; const float b[3] = { 5, 6, 7 }, z[3] = { 30, 60, 0 };
        FlipMirrorSway(b, o, z);                          // prediction / sway off: no offset, nothing moves
        CHECK(o[0] == 5 && o[1] == 6 && o[2] == 7);
    }

    // ---- cl_flipviewmodels hold, live play (target -1 = inverse of the player's own) ----
    int pref = -1;
    CHECK(FlipVmStep(false, 0, -1, pref) == -1 && pref == -1);   // off, nothing held: hands off
    CHECK(FlipVmStep(true, 0, -1, pref) == 1 && pref == 0);      // on: right-hander gets the left hand
    CHECK(FlipVmStep(true, 1, -1, pref) == -1 && pref == 0);     // held, stable: no writes per tick
    CHECK(FlipVmStep(true, 0, -1, pref) == 1 && pref == 0);      // class cfg put 0 back: re-invert
    CHECK(FlipVmStep(false, 1, -1, pref) == 0 && pref == -1);    // off: their own 0 back
    CHECK(FlipVmStep(false, 0, -1, pref) == -1 && pref == -1);   // and nothing after

    pref = -1;                                                   // a left-hander, same dance
    CHECK(FlipVmStep(true, 1, -1, pref) == 0 && pref == 1);
    CHECK(FlipVmStep(true, 0, -1, pref) == -1);
    CHECK(FlipVmStep(false, 0, -1, pref) == 1 && pref == -1);

    pref = -1;                                                   // player changes hand while off
    CHECK(FlipVmStep(false, 1, -1, pref) == -1);
    CHECK(FlipVmStep(true, 1, -1, pref) == 0 && pref == 1);      // hold starts from the NEW value

    // ---- POV demo (target = the recorded m_bFlipViewModels) ----
    pref = -1;                                                   // recorded left-handed, viewer is a right-hander
    CHECK(FlipVmStep(true, 0, 1, pref) == 1 && pref == 0);
    CHECK(FlipVmStep(true, 1, 1, pref) == -1 && pref == 0);
    CHECK(FlipVmStep(false, 1, 1, pref) == 0 && pref == -1);     // demo over / off: viewer's own hand back
    pref = -1;                                                   // recorded with the viewer's own hand
    CHECK(FlipVmStep(true, 0, 0, pref) == -1 && pref == -1);     // nothing to write, nothing held
    CHECK(FlipVmStep(false, 0, 0, pref) == -1 && pref == -1);    // so nothing to hand back

    // ---- the hold against the game's own ConVar, server and config writer ----
    for (int own = 0; own <= 1; ++own) {
        SimCvar cv; pref = -1;
        cv.SetStr(own ? "1" : "0"); cv.sends = 0;
        const char ownDigit = own ? '1' : '0', heldDigit = own ? '0' : '1';

        HandTick(cv, true, -1, pref);                                        // hold
        CHECK(cv.sends == 1 && cv.sent == heldDigit && cv.n == 1 - own && pref == own);
        HandTick(cv, true, -1, pref);                                        // stable: silent
        CHECK(cv.sends == 1);

        // Quit / host_writeconfig / advanced options OK mid-hold: config.cfg gets the player's
        // own hand (a right-hander's default 0 is simply omitted), the server hears nothing, and
        // the live cvar reads the held hand again straight after.
        const std::string cfg = ConfigWithGuard(cv, pref);
        CHECK(cfg == (own ? "cl_flipviewmodels \"1\"" : ""));
        CHECK(cv.sends == 1 && cv.str[0] == heldDigit && cv.str[1] == 0 && cv.n == 1 - own);
        CHECK(cv.flags == 0x400280);                                         // no flag games
        // Without the guard the held hand would be saved: the test can see the difference.
        CHECK(cv.WriteConfig() == (own ? "" : "cl_flipviewmodels \"1\""));

        // Signon resend reads the string: the server keeps getting the held hand on a reconnect.
        CHECK(cv.str[0] == heldDigit);

        // A class cfg puts the player's hand back (a real SetValue: the server hears it) -> re-hold.
        cv.SetStr(own ? "1" : "0");
        CHECK(cv.sent == ownDigit);
        HandTick(cv, true, -1, pref);
        CHECK(cv.sent == heldDigit && cv.n == 1 - own);

        // Release: the server is told the player's own hand, config writes it with no swap.
        HandTick(cv, false, -1, pref);
        CHECK(pref == -1 && cv.n == own && cv.sent == ownDigit);
        CHECK(ConfigWithGuard(cv, pref) == (own ? "cl_flipviewmodels \"1\"" : ""));
        CHECK(cv.str[0] == ownDigit);
    }
    // The swap refuses anything that is not a lone digit.
    CHECK(FlipCfgDigit(0, "1") == '0' && FlipCfgDigit(1, "0") == '1');
    CHECK(FlipCfgDigit(0, "0") == 0 && FlipCfgDigit(-1, "1") == 0 && FlipCfgDigit(0, "10") == 0 && FlipCfgDigit(0, nullptr) == 0);

    // ---- the tick against a fake engine ----
    {
        struct Env {
            bool ownerFree = true, ownerTaken = false;
            bool hasYaw = true, hasHand = true, hltv = false, demo = false;
            int  recorded = -1;
            float yaw = 0.022f;
            bool patches = false; int patchWrites = 0;
            SimCvar hand; bool handFails = false; int handWrites = 0; int pokes = 0;
            char savedDigit = 0;
            int  Str()        { return hand.str[0] == '1' ? 1 : 0; }   // what the server + config.cfg see
            bool TakeOwner()  { if (!ownerFree) return false; ownerFree = false; ownerTaken = true; return true; }
            void DropOwner()  { ownerFree = true; ownerTaken = false; }
            bool HasYaw()     { return hasYaw; }
            float Yaw()       { return yaw; }
            void SetYaw(float v) { yaw = v; }
            void SetHudPatches(bool on) { patches = on; ++patchWrites; }
            bool HasHand()    { return hasHand; }
            int  Hand()       { return Str(); }
            bool SetHand(int v, int, int, int) { ++handWrites; if (handFails) return false; hand.SetInt(v); return hand.n == v; }   // SetValueInt reads the float back
            int  ClientHand() { return hand.n; }
            void SetClientHand(int v) { hand.n = v; hand.f = float(v); ++pokes; }   // ForceValue
            bool IsHLTV()     { return hltv; }
            bool IsDemo()     { return demo; }
            int  RecordedHand() { return recorded; }
            void SaveOwnHandForConfig(int own) { hand.str[0] = own ? '1' : '0'; savedDigit = hand.str[0]; }
        };
        const FlipCoreIn game   = { true,  true, true,  false };
        const FlipCoreIn off    = { false, true, true,  false };
        auto mirrorFrame = [](FlipCore& c) { FlipTrackFrameStart(c.track); FlipTrackWorldPass(c.track); FlipTrackMirror(c.track); FlipTrackTwoD(c.track); };

        Env e; FlipCore c;
        CHECK(!FlipCoreTick(c, e, game));                    // on, owner taken, nothing mirrored yet: nothing held
        CHECK(c.owner && e.ownerTaken && e.yaw == 0.022f && !e.patches && e.Str() == 0 && e.hand.n == 0 && e.pokes == 0);
        mirrorFrame(c);
        CHECK(FlipCoreTick(c, e, game));                     // mirrored: all three held
        CHECK(e.yaw == -0.022f && e.patches && c.vmPref == 0);
        CHECK(e.Str() == 1 && e.hand.sends == 1 && e.hand.sent == '1');   // the server hears the left hand ...
        CHECK(e.hand.n == 0 && e.pokes == 1 && c.clientPoked);           // ... the client draws the gun right
        for (int i = 0; i < 50; ++i) CHECK(FlipCoreTick(c, e, game));   // hitch / freezecam: no events, still held
        CHECK(e.patchWrites == 1 && e.handWrites == 1 && e.pokes == 1);   // and nothing rewritten every tick

        // A class cfg at spawn: `cl_flipviewmodels 0` puts string and int back (the server hears 0).
        e.hand.SetStr("0");
        CHECK(e.hand.sent == '0');
        CHECK(FlipCoreTick(c, e, game));
        CHECK(e.Str() == 1 && e.hand.sent == '1' && e.hand.n == 0 && c.vmPref == 0);
        // Only the int reset (a raw write somewhere): poked again, nothing sent.
        e.hand.n = 1; { const int sends = e.hand.sends; FlipCoreTick(c, e, game); CHECK(e.hand.n == 0 && e.hand.sends == sends); }

        FlipTrackFrameStart(c.track); FlipTrackWorldPass(c.track); FlipTrackTwoD(c.track);   // world, no mirror
        FlipTrackFrameStart(c.track);
        CHECK(!FlipCoreTick(c, e, game));                    // let go of everything
        // The release really reaches the server: the poked int (0) equals the value written, and without the
        // un-poke InternalSetIntValue would return before the string changed.
        CHECK(e.yaw == 0.022f && !e.patches && e.Str() == 0 && e.hand.sent == '0' && e.hand.n == 0 && c.vmPref == -1 && c.owner);
        CHECK(!c.clientPoked && !FlipHolding(c));
        mirrorFrame(c);
        CHECK(FlipCoreTick(c, e, game) && e.Str() == 1 && e.hand.n == 0);

        // Toggle off: everything back, then the owner token goes.
        CHECK(!FlipCoreTick(c, e, off));
        CHECK(e.yaw == 0.022f && !e.patches && e.Str() == 0 && e.hand.sent == '0' && e.hand.n == 0 && !c.owner && e.ownerFree && !c.track.mirrored);
        // Back on: needs a fresh mirrored frame first (the old one does not count).
        CHECK(!FlipCoreTick(c, e, game) && e.Str() == 0);

        // A left-hander: server hears right, client int stays left.
        { Env g; g.hand.SetStr("1"); g.hand.sends = 0; FlipCore cg; FlipCoreTick(cg, g, game); mirrorFrame(cg); FlipCoreTick(cg, g, game);
          CHECK(g.Str() == 0 && g.hand.sent == '0' && g.hand.n == 1 && cg.vmPref == 1);
          FlipCoreTick(cg, g, off); CHECK(g.Str() == 1 && g.hand.sent == '1' && g.hand.n == 1 && !cg.clientPoked); }

        // Another DLL owns flip world: this copy never holds anything, even with the frame mirrored -- and
        // never touches the int the owner poked.
        Env busy; busy.ownerFree = false; FlipCore cb;
        mirrorFrame(cb);
        busy.hand.SetStr("1"); busy.hand.n = 0;              // the owner's hold: string 1, int poked 0
        CHECK(!FlipCoreTick(cb, busy, game) && !cb.owner && busy.yaw == 0.022f && !busy.patches && busy.handWrites == 0);
        CHECK(busy.hand.n == 0 && busy.pokes == 0);

        // No config guard: yaw and HUD follow, the hand does not.
        { Env g; FlipCore cg; FlipCoreIn in = game; in.cfgGuard = false;
          FlipCoreTick(cg, g, in); mirrorFrame(cg);
          CHECK(FlipCoreTick(cg, g, in) && g.yaw == -0.022f && g.patches && g.handWrites == 0 && g.pokes == 0); }
        // SourceTV: no hand.
        { Env g; g.hltv = true; FlipCore cg; FlipCoreTick(cg, g, game); mirrorFrame(cg); FlipCoreTick(cg, g, game); CHECK(g.handWrites == 0 && g.pokes == 0); }
        // POV demo recorded left, viewer left: no string write, but the gun still goes to the right (the
        // recorded rockets appear there), and comes back on release.
        { Env g; g.demo = true; g.recorded = 1; g.hand.SetStr("1"); FlipCore cg; FlipCoreTick(cg, g, game); mirrorFrame(cg); FlipCoreTick(cg, g, game);
          CHECK(g.handWrites == 0 && cg.vmPref == -1 && g.hand.n == 0 && cg.clientPoked && FlipHolding(cg));
          FlipCoreTick(cg, g, off); CHECK(g.hand.n == 1 && !cg.clientPoked && !FlipHolding(cg)); }
        // POV demo recorded left, viewer right: string 1, int 0.
        { Env g; g.demo = true; g.recorded = 1; FlipCore cg; FlipCoreTick(cg, g, game); mirrorFrame(cg); FlipCoreTick(cg, g, game);
          CHECK(g.Str() == 1 && g.hand.n == 0 && cg.vmPref == 0); }

        // A failed hand write is retried from the same state, and never commits a hold it did not make. The int
        // follows what the server really has: string still 0 -> gun on the other side from it.
        { Env g; g.handFails = true; FlipCore cg; FlipCoreTick(cg, g, game); mirrorFrame(cg);
          FlipCoreTick(cg, g, game); CHECK(cg.vmPref == -1 && g.handWrites == 1 && g.Str() == 0 && g.hand.n == 1);
          g.handFails = false; FlipCoreTick(cg, g, game); CHECK(cg.vmPref == 0 && g.Str() == 1 && g.hand.n == 0 && g.handWrites == 2); }

        // Eject, off the game thread, game thread still alive: nothing written except config.cfg's digit.
        {
            Env g; FlipCore cg; FlipCoreTick(cg, g, game); mirrorFrame(cg); FlipCoreTick(cg, g, game);
            const FlipCoreIn ejectAlive = { false, true, false, false };
            const int pokes = g.pokes;
            FlipCoreTick(cg, g, ejectAlive);
            CHECK(g.yaw == -0.022f && g.patches && g.hand.sent == '1' && g.hand.n == 0 && cg.owner && g.pokes == pokes);
            CHECK(g.savedDigit == '0' && g.hand.WriteConfig() == "");          // the right-hander's own 0
            const FlipCoreIn ejectGone = { false, true, false, true };
            FlipCoreTick(cg, g, ejectGone);                                   // game thread gone: float + bytes back
            CHECK(g.yaw == 0.022f && !g.patches && g.hand.sent == '1' && cg.owner && FlipHolding(cg)); // hand still held, token kept
            // Game thread comes back: releases the hand, then drops the token.
            FlipCoreTick(cg, g, off);
            CHECK(g.Str() == 0 && g.hand.n == 0 && cg.vmPref == -1 && !cg.clientPoked && !cg.owner);
        }
        // Off the game thread nothing is ever taken, even with flip world wanted and the frame mirrored.
        {
            Env g; FlipCore cg; mirrorFrame(cg);
            const FlipCoreIn render = { true, true, false, false };
            FlipCoreTick(cg, g, render);
            CHECK(!cg.owner && g.yaw == 0.022f && !g.patches && g.handWrites == 0 && g.pokes == 0);
        }
        // No m_yaw / no hand cvar found: the rest still works.
        { Env g; g.hasYaw = false; g.hasHand = false; FlipCore cg; FlipCoreTick(cg, g, game); mirrorFrame(cg);
          CHECK(FlipCoreTick(cg, g, game) && g.patches && g.yaw == 0.022f && g.handWrites == 0);
          CHECK(FlipHolding(cg));                            // HUD bytes alone still count: eject waits for them
          FlipCoreTick(cg, g, off); CHECK(!g.patches && !FlipHolding(cg)); }
    }

    std::printf(s_failed ? "flipworld_test: %d FAILED\n" : "flipworld_test: all passed\n", s_failed);
    return s_failed ? 1 : 0;
}
