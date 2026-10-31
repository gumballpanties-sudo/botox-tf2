// Gate test for the killstreak carry-over across demos (src/hacks/hud/demostreak.h): a new demo
// keeps the streak the last one ended on, a rewind (same file reloaded, tick backwards) goes back
// to what the demo was loaded with instead of counting the replayed kills twice, a death still
// ends it, and live play is untouched. Same file in dih and fpsoverlay.
//
// Build + run: cmake --build build --config Release --target demostreak_test && build\Release\demostreak_test.exe
#include "../src/hacks/hud/demostreak.h"
#include <cstdio>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

static int Total(const ds::Streaks& s) { int t = 0; for (const auto& [k, n] : s) t += n; return t; }

// What skins.cpp does: Sync, then the change.
static void Kill(ds::Demo& d, ds::Streaks& s, bool playing, const char* lvl, int len, int tick, int key)
{ ds::Sync(d, playing, lvl, len, tick, s); ++s[key]; }

// Returns true when the reset cleared the streak.
static bool Reset(ds::Demo& d, ds::Streaks& s, bool playing, const char* lvl, int len, int tick, bool death)
{
    if (!ds::ResetEnds(ds::Sync(d, playing, lvl, len, tick, s), death)) return false;
    s.clear();
    return true;
}

int main()
{
    // ---- 1. LIVE PLAY IS UNCHANGED -----------------------------------------------------------
    // every reset ends the streak, and Sync never touches it
    {
        ds::Demo d; ds::Streaks s;
        Kill(d, s, false, "", 0, 0, 7); Kill(d, s, false, "", 0, 0, 7);
        CHECK(Total(s) == 2);
        CHECK(Reset(d, s, false, "", 0, 0, false));      // player_spawn, round start ...
        CHECK(Total(s) == 0);
        CHECK(!d.active);
    }

    // ---- 2. A LIVE STREAK DOES NOT LEAK INTO A DEMO --------------------------------------------
    {
        ds::Demo d; ds::Streaks s;
        Kill(d, s, false, "", 0, 0, 7); Kill(d, s, false, "", 0, 0, 7);
        CHECK(ds::Sync(d, true, "maps/cp_process_final.bsp", 9000, 0, s) == ds::Load::NewDemo);
        CHECK(Total(s) == 0);
    }

    // ---- 3. DEMO LOAD EVENTS DO NOT RESET; THE NEXT DEMO CARRIES ON --------------------------
    {
        ds::Demo d; ds::Streaks s;
        const char* A = "maps/cp_process_final.bsp";
        const char* B = "maps/koth_product_final.bsp";
        CHECK(!Reset(d, s, true, A, 9000, 0, false));    // localplayer_changeteam at load
        CHECK(!Reset(d, s, true, A, 9000, 0, false));    // localplayer_changeclass at load
        Kill(d, s, true, A, 9000, 100, 7);
        Kill(d, s, true, A, 9000, 200, 7);
        Kill(d, s, true, A, 9000, 300, 9);
        CHECK(Total(s) == 3);
        CHECK(!Reset(d, s, true, A, 9000, 400, false));  // round start inside the demo: kept
        CHECK(Total(s) == 3);

        // demo B loads: its load events keep the 3, and kills go on from there
        CHECK(ds::Sync(d, true, B, 5000, 0, s) == ds::Load::NewDemo);
        CHECK(!Reset(d, s, true, B, 5000, 0, false));
        CHECK(Total(s) == 3);
        Kill(d, s, true, B, 5000, 50, 7);
        CHECK(Total(s) == 4);
        CHECK(s[7] == 3 && s[9] == 1);                  // per weapon too, for kill_streak_wep

        // ---- 4. REWIND: SAME FILE RELOADED, REPLAYED KILLS COUNT ONCE ----------------------
        Kill(d, s, true, B, 5000, 900, 7);               // 5
        CHECK(Total(s) == 5);
        CHECK(ds::Sync(d, true, B, 5000, 0, s) == ds::Load::Rewind);   // reload at tick 0
        CHECK(Total(s) == 3);                            // what B was loaded with
        Kill(d, s, true, B, 5000, 50, 7);                // the skip replays tick 50
        CHECK(Total(s) == 4);                            // not 6

        // ---- 5. DEATH STILL ENDS IT, AND A REWIND PAST THE DEATH COMES BACK ---------------
        CHECK(Reset(d, s, true, B, 5000, 1200, true));
        CHECK(Total(s) == 0);
        Kill(d, s, true, B, 5000, 1500, 7);
        CHECK(Total(s) == 1);
        CHECK(!Reset(d, s, true, B, 5000, 0, false));    // rewind's load event
        CHECK(Total(s) == 3);

        // ---- 6. FORWARD SKIP IS THE SAME PASS ---------------------------------------------
        CHECK(ds::Sync(d, true, B, 5000, 4000, s) == ds::Load::Same);
        CHECK(Total(s) == 3);
    }

    // ---- 7. SAME MAP, DIFFERENT DEMO (LENGTH) IS A NEW DEMO, NOT A REWIND ----------------------
    {
        ds::Demo d; ds::Streaks s;
        const char* A = "maps/cp_process_final.bsp";
        Kill(d, s, true, A, 9000, 100, 7);
        Kill(d, s, true, A, 9000, 200, 7);
        CHECK(ds::Sync(d, true, A, 7000, 0, s) == ds::Load::NewDemo);
        CHECK(Total(s) == 2);
    }

    // ---- 8. DEMO -> LIVE -> SAME DEMO AGAIN STARTS FRESH ---------------------------------------
    // live play forgets the demo, so replaying it later is not a "rewind" back to an old carry
    {
        ds::Demo d; ds::Streaks s;
        const char* A = "maps/cp_process_final.bsp";
        CHECK(ds::Sync(d, true, "maps/pl_upward.bsp", 100, 0, s) == ds::Load::NewDemo);
        Kill(d, s, true, "maps/pl_upward.bsp", 100, 50, 7);           // 1 carried into A
        CHECK(ds::Sync(d, true, A, 9000, 0, s) == ds::Load::NewDemo);
        CHECK(Total(s) == 1);
        CHECK(Reset(d, s, false, "", 0, 0, false));                   // live spawn
        CHECK(ds::Sync(d, true, A, 9000, 0, s) == ds::Load::NewDemo); // not Rewind
        CHECK(Total(s) == 0);
    }

    // ---- 9. NULL LEVEL NAME DOES NOT CRASH ---------------------------------------------------
    {
        ds::Demo d; ds::Streaks s;
        CHECK(ds::Sync(d, true, nullptr, 10, 0, s) == ds::Load::NewDemo);
        CHECK(ds::Sync(d, true, nullptr, 10, 5, s) == ds::Load::Same);
    }

    std::printf("%d/%d checks passed\n", s_checks - s_failed, s_checks);
    return s_failed ? 1 : 0;
}
