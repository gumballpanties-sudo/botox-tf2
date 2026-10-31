// Unit A port: small movement tricks from botox csgo-sdk, adapted to TF2's gamemovement.
// Textually included by movement.cpp (same convention as edgebug.inl/texturebug.inl) --
// shares every helper/static in that TU (On, EntV3/EntI/EntF, TraceHull, IN_*, FL_*...).
// Ported: nulls, hsw, delay hop, auto crouch, jumpbug kangaroo, smooth demo
// view (reuses engIsPlayingDemo=76, same confirmed vtable slot as fakemove.inl), jumpstats.
// edge skip + blockbot: movement_edge.inl. tung/bounce/pf modes/eb presets/ps fix:
// movement_tricks2.inl. No ladder tricks: TF2 has no ladders.

// (movement_ex_pure.h included near the top of movement.cpp -- LongJump's adaptive-cancel
// append needs it before this file is included)

// ==== nulls: opposing strafe keys cancel instead of fighting (botox movement.cpp:541) ====
static void Nulls(void* pLocal, UserCmd_t* pCmd)
{
    if (!g_nulls) return;
    const int buttons = pCmd->buttons;
    if ((buttons & (IN_MOVELEFT | IN_MOVERIGHT)) == (IN_MOVELEFT | IN_MOVERIGHT))
    {
        static int s_sideWin = 0; // last unopposed side press wins the tie, like the source
        if (!(buttons & IN_MOVELEFT))  s_sideWin = 0;
        else if (!(buttons & IN_MOVERIGHT)) s_sideWin = 0;
        pCmd->buttons &= ~(s_sideWin >= 0 ? IN_MOVELEFT : IN_MOVERIGHT);
        pCmd->sidemove = (s_sideWin >= 0 ? -1.f : 1.f) * CvarFloat("cl_sidespeed", 450.f);
    }
    if ((buttons & (IN_FORWARD | IN_BACK)) == (IN_FORWARD | IN_BACK))
    {
        pCmd->buttons &= ~IN_BACK;
        pCmd->forwardmove = CvarFloat("cl_forwardspeed", 450.f);
    }
}

// ==== half sideways bind: 1:1 port of botox movement.cpp:557 (mv_hsw) ====
static void Hsw(void* pLocal, UserCmd_t* pCmd)
{
    static int s_lastButtons = 0;
    const int last = s_lastButtons;
    s_lastButtons = pCmd->buttons;
    if (!On(g_hsw, g_hswKey, g_hswMode) || EntMoveType(pLocal) != MOVETYPE_WALK)
        return;

    const bool onGround = EntGround(pLocal);
    const bool autohop  = g_bhop || CvarFloat("sv_autobunnyhopping", 0.f) != 0.f;
    const bool jumping  = onGround && (pCmd->buttons & IN_JUMP) && (autohop || !(last & IN_JUMP));
    const bool grounded = onGround && !jumping;

    const int   s = HswSign(g_hswSide, grounded);
    const float f = (float)s * 450.f;

    if ((!grounded && g_hswAllowWS) || (grounded && g_hswPreGround))
    {
        if (pCmd->buttons & IN_FORWARD) {
            pCmd->forwardmove = 450.f; pCmd->sidemove = -f;
            pCmd->buttons |= s == 1 ? IN_MOVELEFT : IN_MOVERIGHT;
        } else if (pCmd->buttons & IN_BACK) {
            pCmd->forwardmove = -450.f; pCmd->sidemove = f;
            pCmd->buttons |= s == 1 ? IN_MOVERIGHT : IN_MOVELEFT;
        }
    }
    if (!grounded || (g_hswPreGround && g_hswAllowAD))
    {
        if (pCmd->buttons & IN_MOVELEFT) {
            pCmd->forwardmove = f; pCmd->sidemove = -450.f;
            pCmd->buttons |= IN_FORWARD;
        } else if (pCmd->buttons & IN_MOVERIGHT) {
            pCmd->forwardmove = -f; pCmd->sidemove = -450.f;
            pCmd->buttons |= IN_BACK;
        }
    }
}

// ==== delay hop: hold N ticks of air time before pressing jump (botox movement.cpp:617) ====
static void DelayHop(void* pLocal, UserCmd_t* pCmd)
{
    if (!On(g_delayHop, g_delayHopKey, g_delayHopMode) || !g_globalVars) return;
    const int mt = EntMoveType(pLocal);
    if (mt == MOVETYPE_NOCLIP) return;

    static int s_lastAirTick = 0;
    if (EntGround(pLocal))
    {
        if (DelayHopShouldJump(pCmd->tick_count, s_lastAirTick, g_delayHopTicks))
            pCmd->buttons |= IN_JUMP;
        else
            pCmd->buttons &= ~IN_JUMP;
    }
    else
    {
        s_lastAirTick = pCmd->tick_count;
    }
}

// ==== auto crouch: duck ahead of a landing too low to stand in (simplified from botox
// movement.cpp:2144 -- that version compares a standing-land trace vs a ducking-land trace;
// here we trust CategorizePosition's own down-trace the way MJ_ClearBelow above does: one
// hull trace at the HULL'S STANDING height. A floor that trace cannot reach but a ducked
// hull (62u tall, routesim.h) would is exactly the "stand would clip, duck wouldn't" case) ====
static void AutoCrouch(void* pLocal, UserCmd_t* pCmd)
{
    if (!On(g_autoCrouch, g_autoCrouchKey, g_autoCrouchMode) || On(g_jumpbug, g_jumpbugKey, g_jumpbugMode))
        return;
    const int mt = EntMoveType(pLocal);
    if (mt == MOVETYPE_NOCLIP || EntGround(pLocal))
        return;
    if (g_offVelocity <= 0 || g_offOrigin <= 0 || g_offMins <= 0 || g_offMaxs <= 0 || !g_engineTrace)
        return;

    const Vec3 vel = EntV3(pLocal, g_offVelocity);
    if (vel.z >= 0.f) return;   // only while falling

    const rs::Params p = MV_SimParams(pLocal);
    const Vec3 vOrigin = EntV3(pLocal, g_offOrigin);
    const Vec3 vMins = EntV3(pLocal, g_offMins);
    const Vec3 vStandMaxs = EntV3(pLocal, g_offMaxs);   // current (standing) hull top
    TraceFilterWorldProps filter; filter.Setup(pLocal);

    const float flLookAhead = fabsf(vel.z) * p.dt * (float)g_autoCrouchTicks;
    GameTrace_t tr{};
    TraceHull(vOrigin, vOrigin - Vec3(0, 0, flLookAhead + 2.f), vMins, vStandMaxs,
              MASK_PLAYERSOLID, &filter, &tr);
    if (!tr.DidHit()) return;

    // headroom above the landing spot, standing hull height
    GameTrace_t headTr{};
    const Vec3 vLand = tr.endpos;
    TraceHull(vLand, vLand + Vec3(0, 0, vStandMaxs.z - vMins.z), vMins, vStandMaxs,
              MASK_PLAYERSOLID, &filter, &headTr);
    if (headTr.DidHit() && headTr.fraction < 0.98f)
        pCmd->buttons |= IN_DUCK;
}

// ==== jumpbug kangaroo: alt duck-timing variant, standalone so AutoJumpbug (working) is
// never touched. Ported from botox movement.cpp:1816 kangaroo(), re-read against TF2 FL_*
// (confirmed FL_ONGROUND=1<<0 above) rather than CS:GO's enum ====
static void JumpbugKangaroo(void* pLocal, UserCmd_t* pCmd)
{
    static bool  s_ducked = false;
    static float s_armUntil = 0.f;
    if (!On(g_jumpbug, g_jumpbugKey, g_jumpbugMode) || !g_jumpbugKangaroo || !g_globalVars) return;
    if (g_offFlags <= 0 || g_offVelocity <= 0) return;

    const int   flags    = EntI(pLocal, g_offFlags);
    const bool  onGround = (flags & FL_ONGROUND) != 0;
    const bool  wasGround = (s_prePredFlags & FL_ONGROUND) != 0;
    const float vz = EntV3(pLocal, g_offVelocity).z;
    const float now = g_globalVars->curtime;

    if (onGround && vz >= 0.f) s_armUntil = now + 0.2f;

    if (!(pCmd->buttons & IN_JUMP))
    {
        if (onGround && !wasGround && !s_ducked)
        {
            pCmd->buttons |= IN_DUCK;
            s_ducked = true;
        }
        else
        {
            s_ducked = false;
        }
    }
    else
    {
        if (onGround && !wasGround)
            pCmd->buttons |= IN_DUCK;
        if (onGround)
            pCmd->buttons &= ~IN_JUMP;
    }
}

// ==== smooth demo view: lock the OUTGOING cmd's viewangles to the last sent angle while
// playing a demo back, so a silent-aim/strafe-opt snap the user made live is not re-shown
// in playback. engIsPlayingDemo = IVEngineClient vtable slot 76, already confirmed and used
// by fakemove.inl / flipworld.cpp -- no new hook. Ported from botox create_move_proxy.cpp:238 ====
// isolated: MSVC C2712 forbids __try in a function that also needs C++ object unwinding
// (see globals.h's MiscHookGuard note) -- SmoothDemoView itself has none of that, but
// keeping the SEH call in its own tiny function is the project's own idiom for this.
static bool SDV_IsPlayingDemo()
{
    bool demo = false;
    __try { demo = VCall<bool>(g_engine, 76); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return demo;
}

static void SmoothDemoView(void* pLocal, UserCmd_t* pCmd)
{
    static Vec3 s_lastSentAngles{};
    static bool s_have = false;
    if (!g_silentView || !g_engine) { s_have = false; return; }

    if (!SDV_IsPlayingDemo()) { s_have = false; return; }
    if (!pLocal || !EntAlive(pLocal)) { s_have = false; return; }

    if (s_have) pCmd->viewangles = s_lastSentAngles;
    s_lastSentAngles = pCmd->viewangles;
    s_have = true;
}

// ==== jumpstats: counts clean bhop/longjump/minijump/wallclimb takeoffs this life, no
// hook needed -- edge-detected off the same IN_JUMP/flags reads every other feature here
// already uses. Ported concept from botox bot_names/jumpstats memory note (remapped
// taxonomy: CS:GO's bhop/lj/ladderjump/wallclimb -> TF2's bhop/lj/mj/wallclimb) ====
struct JumpstatsCounts { int bhop, longjump, minijump, wallclimb; };
static JumpstatsCounts s_jsCounts{};

static void JumpstatsTick(void* pLocal, UserCmd_t* pCmd)
{
    static bool s_wasGround = true;
    if (!g_jumpstats || !pLocal) return;
    const bool onGround = EntGround(pLocal);
    const bool tookOff = s_wasGround && !onGround && (pCmd->buttons & IN_JUMP);
    s_wasGround = onGround;
    if (!tookOff) return;

    if (On(g_wallClimb, g_wallClimbKey, g_wallClimbMode))      s_jsCounts.wallclimb++;
    else if (On(g_longJump, g_longJumpKey, g_longJumpMode))    s_jsCounts.longjump++;
    else if (On(g_miniJump, g_miniJumpKey, g_miniJumpMode))    s_jsCounts.minijump++;
    else if (g_bhop)                                          s_jsCounts.bhop++;
    DebugLog("JSTAT: takeoff bhop=%d lj=%d mj=%d wc=%d",
             s_jsCounts.bhop, s_jsCounts.longjump, s_jsCounts.minijump, s_jsCounts.wallclimb);
}

void MV_GetJumpstats(int& bhop, int& longjump, int& minijump, int& wallclimb)
{
    bhop = s_jsCounts.bhop; longjump = s_jsCounts.longjump;
    minijump = s_jsCounts.minijump; wallclimb = s_jsCounts.wallclimb;
}
