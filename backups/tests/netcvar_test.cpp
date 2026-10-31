// Gate test for the two engine-free rules behind the "packet loss / net settings do nothing"
// fix:
//   src/hacks/network/netcvar.h   -- which preset line may be written right now, and why not
//   src/hacks/movement/cmdbudget.h -- how much wall clock one movement-pipeline run may spend
//
// Build + run:
//   cmake --build build --config Release --target netcvar_test && build\Release\netcvar_test.exe

#include "../src/hacks/network/netcvar.h"
#include "../src/hacks/movement/cmdbudget.h"

#include <cstdio>
#include <cmath>
#include <cstring>

static int g_fail = 0, g_checks = 0;

#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        ++g_checks;                                                             \
        if (!(cond)) {                                                          \
            ++g_fail;                                                           \
            std::printf("FAIL %s:%d  ", __FILE__, __LINE__);                    \
            std::printf(__VA_ARGS__);                                           \
            std::printf("\n");                                                  \
        }                                                                       \
    } while (0)

// ---------------------------------------------------------------------------------------
// 1. Classify: every name any preset in netregion.cpp sets, with the sdk-2013 flag that
//    decides its tier. A name that moves tier here is a behaviour change in the pump.
// ---------------------------------------------------------------------------------------
static void TestClassify()
{
    struct Case { const char* name; netcv::Class want; const char* why; };
    static const Case k[] = {
        // the bandwidth trio -- the server reads these to size what it sends (player.cpp:3484)
        { "rate",               netcv::kRate,       "userinfo, sizes the snapshot budget" },
        { "cl_cmdrate",         netcv::kRate,       "userinfo" },
        { "cl_updaterate",      netcv::kRate,       "userinfo, clamped to sv_maxupdaterate" },
        // FCVAR_NOT_CONNECTED -- engine refuses while alive on a team
        { "cl_interp",          netcv::kConnection, "cdll_bounded_cvars.cpp:103" },
        { "cl_interp_ratio",    netcv::kConnection, "cdll_bounded_cvars.cpp:69" },
        { "cl_predict",         netcv::kConnection, "cdll_bounded_cvars.cpp:29" },
        { "cl_predictweapons",  netcv::kConnection, "prediction.cpp:37" },
        { "cl_lagcompensation", netcv::kConnection, "prediction.cpp:38" },
        // no flag -- applies mid-match
        { "cl_smooth",          netcv::kFree,       "c_baseplayer.cpp:96, flags 0" },
        { "cl_smoothtime",      netcv::kFree,       "c_baseplayer.cpp:98, flags 0" },
        { "cl_pred_optimize",   netcv::kFree,       "prediction.cpp:46, flags 0" },
        { "cl_interp_all",      netcv::kFree,       "c_baseentity.cpp:82, flags 0" },
        // USERINFO|DEVELOPMENTONLY, NO NOT_CONNECTED -- the old table held this wrongly
        { "cl_interpolate",     netcv::kFree,       "c_baseentity.cpp:3216" },
    };
    for (const auto& c : k)
        CHECK(netcv::Classify(c.name) == c.want, "%s classified %d, want %d (%s)",
              c.name, (int)netcv::Classify(c.name), (int)c.want, c.why);

    // Prefix traps: NameIs is a full compare, not a startswith. "cl_interp" must not swallow
    // "cl_interp_all", and "rate" must not swallow "cl_cmdrate" (or the other way round).
    CHECK(netcv::Classify("cl_interp_all") == netcv::kFree, "cl_interp_all ate cl_interp's tier");
    CHECK(netcv::Classify("cl_interp")     == netcv::kConnection, "cl_interp lost its tier");
    CHECK(netcv::Classify("cl_rate")       == netcv::kFree, "cl_rate is not rate");
    CHECK(netcv::Classify("rat")           == netcv::kFree, "rat is not rate");
    CHECK(netcv::Classify("rateX")         == netcv::kFree, "rateX is not rate");
    CHECK(netcv::Classify("")              == netcv::kFree, "empty name");
    CHECK(netcv::Classify(nullptr)         == netcv::kFree, "null name must not crash");

    // An unknown name is free: the pump then sets it through the console and the engine's own
    // rules still apply. Fail-open here is the safe direction -- fail-closed would hold a line
    // forever with nothing to release it.
    CHECK(netcv::Classify("cl_showerror")  == netcv::kFree, "unknown name defaults free");
}

// ---------------------------------------------------------------------------------------
// 2. UserInfoChangeAllowed: CTFGameRules::IsConnectedUserInfoChangeAllowed, client half
//    (tf_gamerules.cpp:20561), conservative subset. The ONE direction that must never be
//    wrong: it may not say yes where TF2 says no.
// ---------------------------------------------------------------------------------------
static void TestAllowed()
{
    // main menu -- every state allowed, team value irrelevant
    for (int team = -1; team <= 5; ++team) {
        CHECK(netcv::UserInfoChangeAllowed(false, true,  team), "menu alive team %d", team);
        CHECK(netcv::UserInfoChangeAllowed(false, false, team), "menu dead team %d", team);
    }

    // in game
    CHECK(!netcv::UserInfoChangeAllowed(true, true, netcv::kTeamRed),  "alive on red must HOLD");
    CHECK(!netcv::UserInfoChangeAllowed(true, true, netcv::kTeamBlue), "alive on blue must HOLD");
    CHECK( netcv::UserInfoChangeAllowed(true, false, netcv::kTeamRed),  "dead on red allowed");
    CHECK( netcv::UserInfoChangeAllowed(true, false, netcv::kTeamBlue), "dead on blue allowed");
    CHECK( netcv::UserInfoChangeAllowed(true, true, 0), "unassigned allowed");   // TEAM_UNASSIGNED
    CHECK( netcv::UserInfoChangeAllowed(true, true, 1), "spectator allowed");    // TEAM_SPECTATOR

    // The subset claim: our answer implies TF2's answer. TF2 also allows a set within 2 s of a
    // respawn and for the prediction-suppression carve-out, so TF2 >= us on every input. The
    // reverse -- us yes where TF2 is no -- is the failure this asserts cannot happen.
    for (int inGame = 0; inGame < 2; ++inGame)
        for (int alive = 0; alive < 2; ++alive)
            for (int team = -2; team <= 6; ++team) {
                const bool ours = netcv::UserInfoChangeAllowed(inGame != 0, alive != 0, team);
                // TF2's rule, without the two widening carve-outs we deliberately omit
                const bool tf2core = (inGame == 0) || (alive == 0) ||
                                     (team != netcv::kTeamRed && team != netcv::kTeamBlue);
                CHECK(ours == tf2core, "rule drift inGame=%d alive=%d team=%d", inGame, alive, team);
            }
}

// ---------------------------------------------------------------------------------------
// 3. Decide: the pump's three outcomes. The bug this replaces: cl_lagcompensation (set by 9
//    of the 11 presets) was not in the old connection list, so it was retried as a normal
//    set, never verified while alive on a team, and burned all 20 tries -- abandoning the
//    whole preset. Here that is kHold, and a hold never burns a try.
// ---------------------------------------------------------------------------------------
static void TestDecide()
{
    // rates opt-in OFF: the trio is never touched, in ANY player state
    for (int allowed = 0; allowed < 2; ++allowed) {
        CHECK(netcv::Decide(netcv::kRate, false, allowed != 0) == netcv::kSkip,
              "rate trio must be skipped with the opt-in off (allowed=%d)", allowed);
        // ...and the other two tiers do not care about the rates toggle at all
        CHECK(netcv::Decide(netcv::kFree, false, allowed != 0) ==
              netcv::Decide(netcv::kFree, true,  allowed != 0), "free tier reads the rates toggle");
        CHECK(netcv::Decide(netcv::kConnection, false, allowed != 0) ==
              netcv::Decide(netcv::kConnection, true, allowed != 0), "conn tier reads the rates toggle");
    }

    // rates opt-in ON: trio behaves like any other connection var
    CHECK(netcv::Decide(netcv::kRate, true, true)  == netcv::kSet,  "rate set when allowed");
    CHECK(netcv::Decide(netcv::kRate, true, false) == netcv::kHold, "rate held when refused");

    // free lines apply mid-match -- that is the whole point of the tier
    CHECK(netcv::Decide(netcv::kFree, true,  false) == netcv::kSet, "free must not be held");
    CHECK(netcv::Decide(netcv::kFree, false, false) == netcv::kSet, "free must not be held");

    CHECK(netcv::Decide(netcv::kConnection, true,  true)  == netcv::kSet,  "conn set when allowed");
    CHECK(netcv::Decide(netcv::kConnection, false, false) == netcv::kHold, "conn held when refused");

    // The regression, named: alive on red, rates off, running cfg_30lerp. Nothing may be
    // written to the connection family, nothing at all to the rate family, and the three
    // free lines must still land -- that is what makes switching presets in-game visible.
    struct Line { const char* name; netcv::Action want; };
    static const Line k30lerp[] = {
        { "cl_cmdrate",         netcv::kSkip },
        { "cl_interp",          netcv::kHold },
        { "cl_interp_ratio",    netcv::kHold },
        { "cl_lagcompensation", netcv::kHold },   // was the try-burner
        { "cl_pred_optimize",   netcv::kSet  },
        { "cl_smooth",          netcv::kSet  },
        { "cl_smoothtime",      netcv::kSet  },
        { "cl_updaterate",      netcv::kSkip },
        { "rate",               netcv::kSkip },
    };
    const bool bAlive = netcv::UserInfoChangeAllowed(true, true, netcv::kTeamRed);
    CHECK(!bAlive, "fixture: alive on red is not allowed");
    int nSet = 0, nHold = 0, nSkip = 0;
    for (const auto& l : k30lerp) {
        const netcv::Action got = netcv::Decide(netcv::Classify(l.name), false, bAlive);
        CHECK(got == l.want, "30lerp/%s -> %d, want %d", l.name, (int)got, (int)l.want);
        if (got == netcv::kSet)  ++nSet;
        if (got == netcv::kHold) ++nHold;
        if (got == netcv::kSkip) ++nSkip;
    }
    CHECK(nSet == 3 && nHold == 3 && nSkip == 3, "30lerp alive split %d/%d/%d", nSet, nHold, nSkip);

    // ...and in the main menu with the opt-in ON, the same preset lands whole.
    const bool bMenu = netcv::UserInfoChangeAllowed(false, true, netcv::kTeamRed);
    for (const auto& l : k30lerp)
        CHECK(netcv::Decide(netcv::Classify(l.name), true, bMenu) == netcv::kSet,
              "30lerp/%s must set in the menu with rates on", l.name);
}

// ---------------------------------------------------------------------------------------
// 4. CmdBudgetSeconds. Two properties carry the whole fix:
//      (a) it never returns MORE than the old tick-share budget  -> cannot regress anything
//      (b) budget * fps <= share (+ the floor's own allowance)   -> the share is of wall clock
// ---------------------------------------------------------------------------------------
static void TestBudget()
{
    const float kTick  = 1.f / 66.6667f;   // TF2
    const float kShare = 0.40f;
    const float kOld   = kTick * kShare;   // 6.0 ms -- what every frame used to get

    // No measurement yet (first call, QPC went backwards) -> exactly the old behaviour.
    CHECK(std::fabs(CmdBudgetSeconds(kTick, 0.f,   kShare) - kOld) < 1e-9f, "no-gap != old budget");
    CHECK(std::fabs(CmdBudgetSeconds(kTick, -1.f,  kShare) - kOld) < 1e-9f, "negative gap != old budget");

    // Degenerate inputs return 0, not garbage.
    CHECK(CmdBudgetSeconds(0.f,   0.016f, kShare) == 0.f, "zero tick");
    CHECK(CmdBudgetSeconds(-1.f,  0.016f, kShare) == 0.f, "negative tick");
    CHECK(CmdBudgetSeconds(kTick, 0.016f, 0.f)    == 0.f, "zero share");

    // fps at or below the tick rate: unchanged. This is the case that must not regress,
    // because it is where the movement features were actually tuned.
    for (float fps = 5.f; fps <= 66.6667f; fps += 0.5f) {
        const float b = CmdBudgetSeconds(kTick, 1.f / fps, kShare);
        CHECK(std::fabs(b - kOld) < 1e-7f, "fps %.1f changed the budget: %.6f vs %.6f", fps, b, kOld);
    }

    // (a) never wider than before, at any frame rate at all
    for (float fps = 1.f; fps <= 2000.f; fps += 1.f) {
        const float b = CmdBudgetSeconds(kTick, 1.f / fps, kShare);
        CHECK(b <= kOld + 1e-7f, "fps %.0f widened the budget: %.6f > %.6f", fps, b, kOld);
        CHECK(b > 0.f, "fps %.0f produced a zero budget", fps);
    }

    // (b) the share means what it says. Above the floor's reach the DLL cannot take more than
    // `share` of wall clock however high the frame rate goes -- which is the defect: the old
    // budget hit 120% of a core at 200 fps.
    for (float fps = 1.f; fps <= 800.f; fps += 1.f) {
        const float b = CmdBudgetSeconds(kTick, 1.f / fps, kShare);
        CHECK(b * fps <= kShare + 1e-4f, "fps %.0f takes %.1f%% of wall clock", fps, b * fps * 100.f);
    }

    // The floor is the one place the share is knowingly exceeded, and only above ~800 fps.
    const float fpsFloor = kShare / k_cmdBudgetFloor;   // = 800 at 0.40 / 0.5ms
    CHECK(std::fabs(CmdBudgetSeconds(kTick, 1.f / fpsFloor, kShare) - k_cmdBudgetFloor) < 1e-7f,
          "the floor does not start where the arithmetic says");
    CHECK(CmdBudgetSeconds(kTick, 1.f / 5000.f, kShare) == k_cmdBudgetFloor, "floor not applied");

    // A tick shorter than the floor (a 1000-tick server, or a share small enough) still wins:
    // the ceiling is the last clamp, so the budget can never exceed one tick's share.
    const float tinyTick = 0.0002f;   // ceiling 0.08 ms, below k_cmdBudgetFloor
    CHECK(CmdBudgetSeconds(tinyTick, 0.5f, kShare) <= tinyTick * kShare + 1e-9f,
          "the floor climbed over a tiny tick's ceiling");

    // Throughput is conserved: the search gets the same seconds-per-second at 66 and at 144,
    // which is why halving the per-frame slice costs the features nothing.
    const float at66  = CmdBudgetSeconds(kTick, 1.f / 66.6667f, kShare) * 66.6667f;
    const float at144 = CmdBudgetSeconds(kTick, 1.f / 144.f,    kShare) * 144.f;
    CHECK(std::fabs(at66 - at144) < 1e-4f, "throughput drifted: %.4f vs %.4f s/s", at66, at144);
}

int main()
{
    TestClassify();
    TestAllowed();
    TestDecide();
    TestBudget();

    std::printf("netcvar_test: %s, %d checks, %d failed\n",
                g_fail ? "FAILED" : "ok", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
