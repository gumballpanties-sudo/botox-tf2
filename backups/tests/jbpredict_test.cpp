// Gate test for the jumpbug look-ahead (src/hacks/movement/routesim.h rs::JumpbugWillHit), the gate
// AutoJumpbug asks before it is allowed to hold IN_DUCK and strip IN_JUMP.
//
// The claim under test: the look-ahead's yes/no is EXACTLY what flying the bind produces. So the
// oracle below does not call JumpbugCmd at all -- it writes the arm rule out by hand off the
// engine's own numbers (floor within 22 + max(3 ticks of fall, 8), + 20 more while ducked; band =
// ducked with the floor 20..22 under) and flies it through the same rs::Tick. Predictor and oracle
// must agree on every case, or the bind and its gate have drifted apart.
//
// Build + run: cmake --build build --config Release --target jbpredict_test && build\Release\jbpredict_test.exe
#include "../src/hacks/movement/routesim.h"
#include <cstdio>
#include <cmath>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

// ---- the oracle: AutoJumpbug's rule written out, flown tick by tick -------------------------
// Returns true if a band tick (the unduck + jump) happens before the hull touches the floor.
static bool OracleHits(rs::State s, const rs::World& w, const rs::Params& p, bool jumpHeld,
                       int maxTicks = 64)
{
    const float unduck = 20.f * p.scale;
    const float reach  = unduck + 2.f;
    for (int i = 0; i < maxTicks; i++)
    {
        if (s.ground) return false;
        rs::Cmd c;
        c.jump = jumpHeld;
        if (s.vz < 0.f && w.floor && !w.floorPlayer && s.z >= w.floorEff - 0.001f)
        {
            const float extra = std::fabs(s.vz) * p.dt * 3.f;
            const float arm   = reach + (extra > 8.f ? extra : 8.f) + (s.fDuck ? unduck : 0.f);
            const float dist  = s.z - w.floorEff;
            if (dist < arm)
            {
                if (s.fDuck && dist < reach && dist >= unduck)
                    return true;                     // the unduck + jump cmd
                c.duck = true; c.jump = false;       // armed: hold the crouch, eat the press
            }
        }
        rs::Tick(s, c, w, p);
    }
    return false;
}

static rs::Params Tf2Params()
{
    rs::Params p;              // defaults are TF2's: 0.015 dt, 800 gravity, 82/62 hulls
    p.duckTimerTicks = (int)std::lround(0.3 / p.dt);
    return p;
}

// a falling hull `drop` units over the floor, standing, no duck history
static rs::State Falling(float drop, float vz, const rs::World& w)
{
    rs::State s;
    s.z  = w.floorEff + drop;
    s.vz = vz;
    return s;
}

int main()
{
    const rs::Params p = Tf2Params();
    rs::World w; w.floor = true; w.floorEff = 0.f;

    // ---- 1. predictor == oracle, over a sweep of every entry a real fall can have ----------
    // 4..400 units of drop x 0..-900 u/s of entry speed. This is the whole claim: the gate only
    // ever refuses arcs the bind itself would have missed.
    int nHit = 0, nMiss = 0, nDisagree = 0;
    for (int drop10 = 40; drop10 <= 4000; drop10 += 7)          // 4.0 .. 400.0 u
    {
        for (int vz = 0; vz >= -900; vz -= 13)
        {
            const rs::State s = Falling(drop10 * 0.1f, (float)vz, w);
            const bool pred = rs::JumpbugWillHit(s, w, p, true);
            const bool real = OracleHits(s, w, p, true);
            if (pred != real) { ++nDisagree; if (nDisagree < 6) std::printf("  drop=%.1f vz=%d pred=%d real=%d\n", drop10 * 0.1f, vz, (int)pred, (int)real); }
            if (real) ++nHit; else ++nMiss;
        }
    }
    CHECK(nDisagree == 0);
    // the sweep has to contain both answers or it proves nothing
    CHECK(nHit > 0);
    CHECK(nMiss > 0);
    std::printf("sweep: %d hit, %d miss, %d disagreements\n", nHit, nMiss, nDisagree);

    // ---- 2. MISSING IS THE COMMON CASE, which is the whole reason the gate exists ----------
    // If the band were usually reachable the old unconditional arm would have been fine.
    CHECK(nMiss > nHit / 2);

    // ---- 3. no floor under you = nothing to bug off ---------------------------------------
    {
        rs::World none;                                   // floor = false
        CHECK(!rs::JumpbugWillHit(Falling(30.f, -200.f, w), none, p, true));
    }

    // ---- 4. a floor that is a PLAYER is refused (dih's traces skip players) ----------------
    {
        rs::World ply = w; ply.floorPlayer = true;
        for (int vz = -40; vz >= -600; vz -= 20)
            CHECK(!rs::JumpbugWillHit(Falling(60.f, (float)vz, w), ply, p, true));
    }

    // ---- 5. rising is not falling ----------------------------------------------------------
    // Straight up out of the arm: nothing may be pressed on the way, and by the time it falls
    // back the predictor's answer is the oracle's. (JumpbugCmd itself refuses vz >= 0.)
    {
        rs::State up = Falling(4.f, 271.f, w);
        CHECK(rs::JumpbugWillHit(up, w, p, true) == OracleHits(up, w, p, true));
    }

    // ---- 6. already IN the band, ducked = fire this tick -----------------------------------
    {
        rs::State s = Falling(21.f, -100.f, w);
        s.fDuck = true; s.ducked = true; s.duckTime = rs::kDuckMs;
        CHECK(rs::JumpbugWillHit(s, w, p, true));
        CHECK(OracleHits(s, w, p, true));
    }

    // ---- 7. ducked and already UNDER the band = the fall is spent ---------------------------
    {
        rs::State s = Falling(6.f, -300.f, w);
        s.fDuck = true; s.ducked = true; s.duckTime = rs::kDuckMs;
        CHECK(!rs::JumpbugWillHit(s, w, p, true));
        CHECK(!OracleHits(s, w, p, true));
    }

    // ---- 8. SPENT AIR DUCKS. tf_clamp_airducks strips the press at 2, so the arm cannot -----
    // crouch and no band tick can ever arrive. This is the case a distance-only arm cannot see
    // and it is exactly the one that eats a bhop.
    {
        int nSpentHit = 0, nFreeHit = 0;
        for (int drop = 8; drop <= 200; drop += 2)
        {
            for (int vz = -20; vz >= -500; vz -= 20)
            {
                rs::State spent = Falling((float)drop, (float)vz, w);
                spent.airDucked = rs::kAirDuckMax;
                if (rs::JumpbugWillHit(spent, w, p, true)) ++nSpentHit;
                if (rs::JumpbugWillHit(Falling((float)drop, (float)vz, w), w, p, true)) ++nFreeHit;
            }
        }
        CHECK(nSpentHit == 0);      // never arms: every duck press is thrown away
        CHECK(nFreeHit > 0);        // ...and the same falls DO hit with the ducks unspent
        std::printf("air ducks: spent=%d hit, unspent=%d hit\n", nSpentHit, nFreeHit);
    }

    // ---- 9. clamp off: the same spent-duck fall becomes reachable again ---------------------
    {
        rs::Params noClamp = p; noClamp.clampAirDucks = false;
        int n = 0;
        for (int drop = 8; drop <= 200; drop += 2)
            for (int vz = -20; vz >= -500; vz -= 20)
            {
                rs::State spent = Falling((float)drop, (float)vz, w);
                spent.airDucked = rs::kAirDuckMax;
                if (rs::JumpbugWillHit(spent, w, noClamp, true)) ++n;
            }
        CHECK(n > 0);
    }

    // ---- 10. the walk terminates: a long fall inside maxTicks answers no, never hangs -------
    {
        const rs::State s = Falling(4000.f, 0.f, w);
        CHECK(!rs::JumpbugWillHit(s, w, p, true, 8));      // 8 ticks nowhere near the floor
    }

    // ---- 11. model scale moves the band with the hull --------------------------------------
    // A 1.5x hull unducks 30u, so its band is 30..33 and a fall that bands at 1.0 need not at
    // 1.5. The predictor must read the scale, not a hardcoded 20 -- and the oracle agrees at
    // both, which is the same drift check as the sweep.
    {
        rs::Params big = p; big.scale = 1.5f;
        // already ducked IN each band = fire this tick, and only in its own band
        rs::State inBig = Falling(31.f, -60.f, w);
        inBig.fDuck = true; inBig.ducked = true; inBig.duckTime = rs::kDuckMs;
        rs::Cmd c; bool band = false; float margin = 0.f;
        CHECK(rs::JumpbugCmd(inBig, w, big, c, band, margin) && band);     // 31 is in 30..33
        band = false;
        CHECK(rs::JumpbugCmd(inBig, w, p, c, band, margin) && !band);      // ...not in 20..22

        int nDiff = 0, nDisagreeBig = 0;
        for (int drop = 8; drop <= 300; drop += 2)
            for (int vz = -20; vz >= -500; vz -= 20)
            {
                const rs::State s = Falling((float)drop, (float)vz, w);
                const bool a = rs::JumpbugWillHit(s, w, p, true);
                const bool b = rs::JumpbugWillHit(s, w, big, true);
                if (a != b) ++nDiff;
                if (b != OracleHits(s, w, big, true)) ++nDisagreeBig;
            }
        CHECK(nDiff > 0);              // the band really moved
        CHECK(nDisagreeBig == 0);      // and the oracle still agrees at 1.5x
    }

    std::printf("%s -- %d checks, %d failed\n", s_failed ? "FAILED" : "OK", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
