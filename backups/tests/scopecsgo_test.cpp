// Gate test for the CS:GO sniper scope port (src/hacks/visuals/scope_csgo.h). Expected numbers are worked
// from csgosrc2019 cs_hud_scope.cpp / weapon_csbase.cpp; texture sums come from PIL's DXT5 decoder
// (scope_csgo_tex_gen.py output), an independent reference for ScCs_DecodeDxt5.
// SAME FILE in SimpleVisual and fpsoverlay.
// Build + run:  cmake --build build --config Release --target scopecsgo_test && build\Release\scopecsgo_test.exe
#include "../src/hacks/visuals/scope_csgo.h"
#include "../src/hacks/visuals/scope_csgo_tex.h"
#include "../src/hacks/visuals/vm_csgo.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)
static bool Near(double a, double b, double eps = 1e-4) { return std::fabs(a - b) <= eps; }

static const ScCsWeapon& AWP = kScCsWeapons[0];
static constexpr float kDt = 1.f / 144.f;
static constexpr int W = 1920, H = 1080;

static ScCsInput Still(bool zoomed) { ScCsInput in; in.zoomed = zoomed; in.maxSpeed = 80.f; return in; }

// settle model + layout for n frames, return the last frame
static ScCsFrame Run(ScCsState& s, const ScCsInput& in, int n, float moveSens = 1.f, float airSens = 1.f,
                     float fov = 40.f, int width = 1, float lat = 0.f, float vert = 0.f)
{
    ScCsFrame f{};
    for (int i = 0; i < n; ++i)
        f = ScCs_Layout(s, AWP, ScCs_Inaccuracy(s, in, AWP, kDt, moveSens, airSens), fov, false, width, kDt, W, H, lat, vert);
    return f;
}

int main()
{
    const double px640_40 = 320.0 / std::tan(40.0 * 3.14159265358979 / 180.0 * 0.5);   // 879.19
    const double px640_25 = 320.0 / std::tan(25.0 * 3.14159265358979 / 180.0 * 0.5);   // 1443.5

    // ---- still + settled: hard 1px black cross, arc box at screenTall/14 ----
    {
        ScCsState s;
        const ScCsFrame f = Run(s, Still(true), 400);
        CHECK(Near(s.penalty, AWP.alt.stand, 1e-6));
        CHECK(f.y1 == 77 && f.y2 == 1003 && f.x1 == 497 && f.x2 == 1423);   // 1080/14 = 77, (1920-1080)/2 = 420
        CHECK(f.x == 960 && f.y == 540 && f.xMod == 960 && f.yMod == 540);
        CHECK(!f.blurred && f.blurAlpha == 255 && f.thickness == 1);
    }

    // ---- scope in from unscoped standing: penalty decays pri.stand -> alt.stand over recovery ----
    {
        ScCsState s;
        Run(s, Still(false), 400);
        CHECK(Near(s.penalty, AWP.pri.stand, 1e-6));
        ScCs_Unscoped(s);
        CHECK(s.animInset == 2.f && s.lineSpread == 20.f);

        const float acc = ScCs_Inaccuracy(s, Still(true), AWP, kDt, 1.f, 1.f);
        const double want = AWP.alt.stand + (AWP.pri.stand - AWP.alt.stand) * std::exp(-kDt * std::log(10.0) / AWP.recStand);
        CHECK(Near(acc, want, 1e-6));
        const ScCsFrame f = ScCs_Layout(s, AWP, acc, 40.f, false, 1, kDt, W, H, 0.f, 0.f);
        const double raw = (want - AWP.alt.stand) * px640_40;                      // ~66 ref px
        CHECK(Near(s.lineSpread, 20.0 + (raw - 20.0) * (kDt * 140.0), 1e-3));     // RemapValClamped
        CHECK(f.blurred);

        // after exactly recStand seconds of scoped still: 10% of the excess left
        ScCsState s2;
        Run(s2, Still(false), 400);
        const int n = (int)std::lround(AWP.recStand / kDt);
        for (int i = 0; i < n; ++i) ScCs_Inaccuracy(s2, Still(true), AWP, kDt, 1.f, 1.f);
        CHECK(Near(s2.penalty, AWP.alt.stand + 0.1 * (AWP.pri.stand - AWP.alt.stand) * std::exp(-(n * kDt - AWP.recStand) * std::log(10.0) / AWP.recStand), 1e-5));
    }

    // ---- full speed scoped: move term, spread clamp 100, inset grows ----
    {
        ScCsState s;
        ScCsInput in = Still(true);
        in.speed2D = 80.f;                            // >= 0.95 * maxSpeed -> scale 1
        const ScCsFrame f = Run(s, in, 600);
        const float acc = ScCs_Inaccuracy(s, in, AWP, kDt, 1.f, 1.f);
        CHECK(Near(acc, AWP.alt.stand + AWP.alt.move, 1e-6));
        // raw 155 -> clamp 100 -> goal 1.3333 -> blur 0.05333 -> inset (int)(77 + 28.8) = 105
        CHECK(Near(s.animInset, 100.0 * 0.4 / 30.0, 1e-3));
        CHECK(f.y1 == 105 && f.x1 == 420 + 105);
        CHECK(f.blurred);
        CHECK(Near(s.lineSpread, AWP.alt.move * px640_40, 0.05));
        // blur alpha: sqrt(1.8 / 155^0.75 = 44) -> 0.20 -> 51 -> floor 140
        CHECK(f.blurAlpha == 140);

        // half speed: pow curve (0.25) from duck speed
        ScCsState s3;
        in.speed2D = 0.5f * (80.f * 0.34f + 80.f * 0.95f);
        const float acc3 = ScCs_Inaccuracy(s3, in, AWP, kDt, 1.f, 1.f);
        CHECK(Near(acc3, AWP.alt.stand + std::pow(0.5, 0.25) * AWP.alt.move, 1e-5));

        // moveSens 0: still scope
        ScCsState s2;
        in.speed2D = 80.f;
        const ScCsFrame f2 = Run(s2, in, 600, 0.f);
        CHECK(!f2.blurred && f2.y1 == 77);
    }

    // ---- crouch settles below stand -> clamped to 0 display ----
    {
        ScCsState s;
        ScCsInput in = Still(true);
        in.ducking = true;
        const ScCsFrame f = Run(s, in, 600);
        CHECK(Near(s.penalty, AWP.alt.crouch, 1e-6));
        CHECK(!f.blurred && f.y1 == 77);
    }

    // ---- airborne at jump impulse: stand+jump penalty + full initial air term ----
    {
        ScCsState s;
        Run(s, Still(true), 400);
        ScCsInput in = Still(true);
        in.onGround = false;
        in.velZ = kScCsJumpImpulse;
        float acc = ScCs_Inaccuracy(s, in, AWP, kDt, 1.f, 1.f);
        CHECK(Near(acc, AWP.alt.stand + AWP.alt.jump + AWP.jumpInitial, 1e-5));
        in.velZ = kScCsJumpImpulse * 4.f;             // sqrt 2x -> 2x initial, the cap
        acc = ScCs_Inaccuracy(s, in, AWP, kDt, 1.f, 1.f);
        CHECK(Near(s.penalty, AWP.alt.stand + AWP.alt.jump, 1e-6));
        CHECK(Near(acc, AWP.alt.stand + AWP.alt.jump + 2.0 * AWP.jumpInitial, 1e-5));

        ScCsState s0;
        Run(s0, Still(true), 400);
        in.velZ = kScCsJumpImpulse;
        CHECK(Near(ScCs_Inaccuracy(s0, in, AWP, kDt, 1.f, 0.f), AWP.alt.stand, 1e-6));   // airSens 0
    }

    // ---- landing banks land * |fall velocity| ----
    {
        ScCsState s;
        Run(s, Still(true), 400);
        ScCsInput air = Still(true);
        air.onGround = false; air.velZ = -500.f;
        ScCs_Inaccuracy(s, air, AWP, 0.f, 1.f, 1.f);   // dt 0: no decay, penalty = stand + jump
        const double before = s.penalty;
        ScCs_Inaccuracy(s, Still(true), AWP, 0.f, 1.f, 1.f);
        CHECK(Near(s.penalty, before + AWP.alt.land * 500.0, 1e-6));
    }

    // ---- zoom fov clamp + bob offset + thickness ----
    {
        ScCsState s;
        ScCsInput in = Still(true);
        in.speed2D = 80.f;
        Run(s, in, 600, 1.f, 1.f, 10.f);
        CHECK(Near(s.lineSpread, AWP.alt.move * px640_25, 0.05));   // AWP zoom 2 = 10 -> clamp 25

        ScCsState s2;
        const ScCsFrame f = Run(s2, Still(true), 400, 1.f, 1.f, 40.f, 3, 0.1f, -0.1f);
        CHECK(f.offsetX == 7 && f.offsetY == -7);                    // (int)(0.1*77), toward zero
        CHECK(f.x == 967 && f.y == 533 && f.x1 == 504 && f.x2 == 1430 && f.y1 == 70 && f.y2 == 996);
        CHECK(f.thickness == 3 && !f.blurred);                      // 3 + 0.5 tolerates more width
    }

    // ---- scale: box + lens about the centre, lines untouched ----
    {
        ScCsState s;
        Run(s, Still(true), 400);
        ScCsFrame f = ScCs_Layout(s, AWP, AWP.alt.stand, 40.f, false, 1, kDt, W, H, 0.f, 0.f, 0.5f);
        CHECK(f.y1 == 308 && f.y2 == 772 && f.x1 == 728 && f.x2 == 1192);   // 540-463*.5, 960-463*.5
        CHECK(f.xMod == 480 && f.yMod == 270 && f.x == 960 && f.y == 540);
        f = ScCs_Layout(s, AWP, AWP.alt.stand, 40.f, false, 1, kDt, W, H, 0.f, 0.f, 1.f);
        CHECK(f.y1 == 77 && f.x1 == 497 && f.x2 == 1423 && f.y2 == 1003 && f.xMod == 960);
        f = ScCs_Layout(s, AWP, AWP.alt.stand, 40.f, false, 1, kDt, 1920, 1081, 0.f, 0.f, 1.f);   // odd height
        CHECK(f.y1 == 77 && f.y2 == 1081 - 77 && f.x1 == (1920 - 1081) / 2 + 77);
    }

    // ---- vm_csgo raw bob = pre-clamp value ----
    {
        VmCsBobCfg cfg; cfg.amtVert = 2.f; cfg.lowerAmt = 0.f;
        VmBobState b{};
        float rv = -99.f, rl = -99.f;
        for (int i = 0; i < 200; ++i)
            VmCs_CalcBob(b, 320.f, true, 80.f, i * 0.01f, 0.01f, cfg, &rv, &rl);
        CHECK(rv != -99.f && rl != -99.f);
        CHECK(Near(rl, b.lat, 1e-5));                               // lat within +-8, no acc pos
        CHECK(b.vert <= 4.f && b.vert >= -7.f);
        CHECK(Near(b.vert, rv < -7.f ? -7.f : (rv > 4.f ? 4.f : rv), 1e-5));
    }

    // ---- DXT5 decoder vs PIL (sums within 0.5%, centre texel +-2) ----
    {
        struct T { const unsigned char* d; int w, h; long long sum[4]; int ctr[4]; };
        const T ts[] = {
            { kScCsArcDxt5,  kScCsArcW,  kScCsArcH,  { 0, 0, 0, 5018628 },                      { 0, 0, 0, 0 } },
            { kScCsBlurDxt5, kScCsBlurW, kScCsBlurH, { 0, 0, 0, 307519 },                       { 0, 0, 0, 216 } },
            { kScCsLensDxt5, kScCsLensW, kScCsLensH, { 11833860, 8102258, 3784611, 19057345 }, { 60, 41, 21, 59 } },
        };
        for (const T& t : ts)
        {
            std::vector<unsigned char> px(t.w * t.h * 4);
            ScCs_DecodeDxt5(t.d, t.w, t.h, px.data());
            for (int c = 0; c < 4; ++c)
            {
                long long sum = 0;
                for (size_t i = c; i < px.size(); i += 4) sum += px[i];
                if (std::llabs(sum - t.sum[c]) > t.sum[c] / 200 + 64)
                    std::printf("  dxt5 %dx%d ch%d: ours %lld PIL %lld\n", t.w, t.h, c, sum, t.sum[c]);
                CHECK(std::llabs(sum - t.sum[c]) <= t.sum[c] / 200 + 64);
                CHECK(std::abs(px[(t.h / 2 * t.w + t.w / 2) * 4 + c] - t.ctr[c]) <= 2);
            }
        }
    }

    std::printf("%d/%d checks passed\n", s_checks - s_failed, s_checks);
    return s_failed ? 1 : 0;
}
