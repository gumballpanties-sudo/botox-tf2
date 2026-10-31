// Gate test for the strafe air-accel math (src/hacks/movement/airlean.h): the closed-form cosGain /
// cosTurn against a brute-force sweep of the same AirAccelerate tick, over the TF2 class speeds
// and the odd cvar cases, plus the lean blend AutoStrafe drives with it.
// Build + run:  cmake --build build --config Release --target airlean_test && build\Release\airlean_test.exe
#include "../src/hacks/movement/airlean.h"
#include <cmath>
#include <cstdio>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)
#define CHECKM(cond, fmt, ...) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: " fmt "\n", __LINE__, __VA_ARGS__); ++s_failed; } } while (0)

static const float kCap = 30.f;      // CTFGameMovement::GetAirSpeedCap
static const float kDt  = 0.015f;    // TF2 tick

// how far velocity rotates in one tick with the wish at cos(theta) = c
static float TurnDeg(float v, float c, float wishSpeed, float accel, float dt, float airCap)
{
    const float cap = wishSpeed < airCap ? wishSpeed : airCap;
    const float add = cap - v * c;
    if (add <= 0.f) return 0.f;
    float a = accel * wishSpeed * dt;
    if (a > add) a = add;
    const float s = sqrtf(1.f - c * c > 0.f ? 1.f - c * c : 0.f);
    return fabsf(atan2f(a * s, v + a * c)) * 57.29578f;
}

// AutoStrafe's blend: cosGain at a closed heading, cosTurn once the error is 30 deg or more
static float BlendCos(const airlean::Lean& l, float errDeg)
{
    float t = (fabsf(errDeg) - 5.f) / 25.f;
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    return l.cosGain + (l.cosTurn - l.cosGain) * t;
}

int main()
{
    // TF2 m_flMaxspeed values after the engine clamp, plus two off-normal ones: a slowed heavy
    // (accel under the 30 cap, where cosGain leans IN) and a demo charge (way over).
    const float speeds[] = { 400.f, 320.f, 300.f, 280.f, 240.f, 230.f, 110.f, 750.f };
    const float accels[] = { 10.f, 5.f, 1.f, 100.f };
    const float vels[]   = { 5.f, 30.f, 80.f, 200.f, 320.f, 400.f, 550.f, 900.f };

    for (float wish : speeds)
    for (float acc  : accels)
    for (float v    : vels)
    {
        const airlean::Lean l = airlean::Solve(v, wish, acc, kDt, kCap);
        const float base = airlean::NewSpeed(v, 0.f, wish, acc, kDt, kCap);

        // ---- cosGain really is the best tick available ----
        float bestC = 0.f, bestV = -1.f;
        for (int i = -10000; i <= 10000; ++i)      // 0.0001 steps over the whole cos range
        {
            const float c = i * 0.0001f;
            const float nv = airlean::NewSpeed(v, c, wish, acc, kDt, kCap);
            if (nv > bestV) { bestV = nv; bestC = c; }
        }
        const float gainV = airlean::NewSpeed(v, l.cosGain, wish, acc, kDt, kCap);
        CHECKM(gainV >= bestV - 1e-3f, "gain v=%.0f wish=%.0f acc=%.0f: closed %.4f (%.4f u/s) vs brute %.4f (%.4f u/s)",
               v, wish, acc, l.cosGain, gainV, bestC, bestV);
        CHECK(gainV >= base - 1e-3f);                       // never worse than perpendicular
        CHECK(l.cosGain >= 0.f && l.cosGain <= 1.f);

        // ---- cosTurn costs nothing ----
        CHECKM(airlean::NewSpeed(v, l.cosTurn, wish, acc, kDt, kCap) >= v - 1e-2f,
               "turn loses speed: v=%.0f wish=%.0f acc=%.0f c=%.4f -> %.4f",
               v, wish, acc, l.cosTurn, airlean::NewSpeed(v, l.cosTurn, wish, acc, kDt, kCap));
        CHECK(l.cosTurn <= l.cosGain + 1e-6f);              // never leans the wrong way
        CHECK(l.cosTurn >= -0.9f - 1e-6f);

        // ---- and it is the EDGE of free: 3 deg further back and the tick costs speed ----
        // (only where the clamps did not bite -- at -0.9, or where cosTurn folded into cosGain,
        //  there is no edge to find)
        const float cFree = -(fminf(acc * wish * kDt * 0.5f, kCap)) / v;
        if (cFree > -0.88f && cFree < l.cosGain - 0.02f)
        {
            const float past = l.cosTurn - 0.03f;
            CHECKM(airlean::NewSpeed(v, past, wish, acc, kDt, kCap) < v,
                   "past the turn edge is still free: v=%.0f wish=%.0f acc=%.0f c=%.4f", v, wish, acc, past);
        }

        // ---- the whole blend AutoStrafe drives is free, end to end ----
        for (int e = 0; e <= 180; e += 5)
        {
            const float c = BlendCos(l, (float)e);
            CHECKM(airlean::NewSpeed(v, c, wish, acc, kDt, kCap) >= v - 1e-2f,
                   "blend loses speed: v=%.0f wish=%.0f acc=%.0f err=%d c=%.4f", v, wish, acc, e, c);
        }
        // closed heading -> pure gain angle, wide open -> the turn angle
        CHECK(fabsf(BlendCos(l, 0.f) - l.cosGain) < 1e-6f);
        CHECK(fabsf(BlendCos(l, 45.f) - l.cosTurn) < 1e-6f);
    }

    // ---- the point of the lean: TF2 scout, air accel 10, 400 u/s ----
    {
        const airlean::Lean l = airlean::Solve(400.f, 400.f, 10.f, kDt, kCap);
        const float turnGain = TurnDeg(400.f, l.cosGain, 400.f, 10.f, kDt, kCap);
        const float turnLean = TurnDeg(400.f, l.cosTurn, 400.f, 10.f, kDt, kCap);
        CHECKM(turnGain > 4.2f && turnGain < 4.4f, "perpendicular turn %.2f deg/tick", turnGain);
        CHECKM(turnLean > 8.5f && turnLean < 8.7f, "leaned turn %.2f deg/tick", turnLean);
        CHECKM(turnLean > turnGain * 1.98f, "lean only buys %.2fx", turnLean / turnGain);
        // and the speed it costs to do that: none
        CHECK(airlean::NewSpeed(400.f, l.cosTurn, 400.f, 10.f, kDt, kCap) >= 400.f - 1e-2f);
        // the gain angle is still the perpendicular one, and worth ~1.1 u/s a tick
        CHECK(fabsf(l.cosGain) < 1e-6f);
        const float g = airlean::NewSpeed(400.f, 0.f, 400.f, 10.f, kDt, kCap) - 400.f;
        CHECKM(g > 1.0f && g < 1.2f, "perpendicular gain %.3f u/s", g);
    }

    // ---- slowed heavy: accel (16.5) under the 30 cap, so the best wish leans IN, not across ----
    {
        const airlean::Lean l = airlean::Solve(200.f, 110.f, 10.f, kDt, kCap);
        CHECKM(l.cosGain > 0.f, "accel under the cap should lean in, got %.4f", l.cosGain);
        CHECK(airlean::NewSpeed(200.f, l.cosGain, 110.f, 10.f, kDt, kCap)
            > airlean::NewSpeed(200.f, 0.f, 110.f, 10.f, kDt, kCap));
    }

    // ---- the bug this replaces: feeding the UNCLAMPED 636 wish (old AutoStrafe) ----
    // it reported a = 95 where the engine uses 36, and the lean it would have taken is one the
    // engine charges speed for.
    {
        const airlean::Lean wrong = airlean::Solve(300.f, 636.f, 10.f, kDt, kCap);   // what the old code assumed
        const float real = airlean::NewSpeed(300.f, wrong.cosTurn, 240.f, 10.f, kDt, kCap);  // soldier, clamped
        CHECKM(real < 300.f, "unclamped lean should cost speed against the real wish, got %.3f", real);
        const airlean::Lean right = airlean::Solve(300.f, 240.f, 10.f, kDt, kCap);
        CHECK(airlean::NewSpeed(300.f, right.cosTurn, 240.f, 10.f, kDt, kCap) >= 300.f - 1e-2f);
    }

    // ---- degenerate inputs must not produce NaN or a wrong-side wish ----
    {
        const airlean::Lean l = airlean::Solve(0.f, 400.f, 10.f, kDt, kCap);
        CHECK(std::isfinite(l.cosGain) && std::isfinite(l.cosTurn));
        CHECK(l.cosTurn >= -0.9f - 1e-6f && l.cosGain <= 1.f);
        const airlean::Lean z = airlean::Solve(400.f, 400.f, 0.f, kDt, kCap);   // sv_airaccelerate 0
        CHECK(std::isfinite(z.cosGain) && std::isfinite(z.cosTurn));
        CHECK(fabsf(z.cosTurn) < 1e-6f);      // no accel, no free lean
    }

    // ---- THE EDGE BUG'S BRAKE (edgebug.inl EbPhaseMove): a wish at your own back -------------
    // The search's along-path knob is one wish pointed 180 degrees off velocity. Two things it
    // claims, both of them this file's own arithmetic:
    //   1. the brake never binds on addspeed -- at c = -1 addspeed is 30 + v, so the tick always
    //      spends the whole accelspeed = 0.15 * wish, whatever the speed
    //   2. it must not REVERSE the fall: NewSpeed returns a magnitude, so a flipped heading looks
    //      like a speed and only (v - a >= 0) says it stayed pointed the same way. That is where
    //      EbPhaseMove's 64 u/s floor comes from -- the biggest a in TF2 is a scout's 0.15 * 400
    {
        const float kFwd = 450.f;                       // cl_forwardspeed, what the attempt sends
        const float kFrac[3] = { 0.25f, 0.5f, 1.f };    // k_ebBrakeFrac
        const float kMaxSpeed[3] = { 240.f, 300.f, 400.f };   // soldier, pyro-ish, scout
        for (const float ms : kMaxSpeed)
            for (const float frac : kFrac)
            {
                const float ask = kFwd * frac;
                const float wish = ask < ms ? ask : ms;            // AirMove clamps to the class max
                const float a = 10.f * wish * kDt;                 // accelspeed, no friction scale
                CHECK(a <= 30.f + 64.f);                           // never capped by addspeed at c=-1
                // the tick's answer at exactly the floor EbPhaseMove refuses to brake under
                const float v = 64.f;
                CHECKM(v - a >= 0.f, "brake at the 64 u/s floor reverses the fall: a=%.2f", a);
                CHECKM(fabsf(airlean::NewSpeed(v, -1.f, wish, 10.f, kDt, kCap) - (v - a)) < 1e-2f,
                       "brake tick is not 0.15*wish: got %.4f want %.4f",
                       airlean::NewSpeed(v, -1.f, wish, 10.f, kDt, kCap), v - a);
                // and well under the floor a full brake really would flip it, which is the whole
                // reason the floor is there
                if (frac >= 1.f && ms >= 400.f) CHECK(50.f - a < 0.f);
            }
        // the phase the gentlest brake buys: 0.25 * 450 = 112 wish -> 16.875 u/s a tick, and five
        // ticks of it gives up sum(k) * a * dt = 3.8u of travel (edgebug.inl's own number)
        const float a = 10.f * (0.25f * 450.f) * kDt;
        CHECK(fabsf(a - 16.875f) < 1e-3f);
        float lost = 0.f;
        for (int k = 1; k <= 5; k++) lost += a * (float)k * kDt;
        CHECKM(fabsf(lost - 3.797f) < 0.01f,
               "five ticks of the gentlest brake: %.3fu of travel given up", lost);
        CHECK(lost > 300.f * kDt * 0.5f);    // more than half a tick of phase at 300 u/s
    }

    std::printf("%s -- %d checks, %d failed\n", s_failed ? "FAILED" : "PASS", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
