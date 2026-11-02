// Unit A port: small movement tricks from botox csgo-sdk, adapted to TF2's gamemovement.
// Textually included by movement.cpp (same convention as edgebug.inl/texturebug.inl) --
// shares every helper/static in that TU (On, EntV3/EntI/EntF, TraceHull, IN_*, FL_*...).
// Ported: nulls, hsw, delay hop, auto crouch, smooth demo
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

// ==== delay hop: space held → wait N ticks after landing, then jump (botox movement.cpp:623) ====
static void DelayHop(void* pLocal, UserCmd_t* pCmd)
{
    if (!On(g_delayHop, g_delayHopKey, g_delayHopMode) || !g_globalVars) return;
    const int mt = EntMoveType(pLocal);
    if (mt == MOVETYPE_NOCLIP) return;

    static int s_lastAirTick = 0;
    if (EntGround(pLocal))
    {
        // only on real space: no hold = no auto hop (air tick still tracked below)
        if (!s_rawJumpHeld) return;
        if (DelayHopShouldJump(pCmd->tick_count, s_lastAirTick, g_delayHopTicks))
        {
            pCmd->buttons |= IN_JUMP;
            s_lastAirTick = pCmd->tick_count;   // botox: reset on press
        }
        else
            pCmd->buttons &= ~IN_JUMP;
    }
    else
    {
        s_lastAirTick = pCmd->tick_count;
    }
}

// ==== auto goomba: launch under a falling player so they touch our head the tick after we
// leave -- they inherit our vz there (why: goomba_pure.h). Types: 0 sticky jump (det,
// IN_ATTACK2), 1 rocket jump (aim tree: autorocketjump.cpp, Amalgam ARJ port, picks the
// floor spot + ctap; this side only publishes the contact tick), 2 regular jump.
// Their velocity is the client's own simtime-delta estimate (C_BasePlayer::PostDataUpdate,
// c_baseplayer.cpp:853, refined by the aim tree's SetAbsVelocity velfix). Their origin is the
// snapshot origin (the m_vecOrigin netvar IS m_vecNetworkOrigin, c_baseentity.cpp:441), so
// it is pushed forward by (our tickbase time - their simtime): the server time our cmd runs at ====
#include "goomba_pure.h"

// IClientNetworkable::IsDormant = slot 8 (same read as world_fx3.cpp Dormant)
static bool Gmb_Dormant(int idx)
{
    void* net = g_entityList ? VCall<void*>(g_entityList, 0, idx) : nullptr;
    if (!net) return true;
    bool d = true;
    __try { d = VCall<bool>(net, 8); } __except (EXCEPTION_EXECUTE_HANDLER) { d = true; }
    return d;
}

static bool s_goombaFired = false;   // this cmd fired (bind indicator flash; publish clears it)
static int  s_goombaRocketK = -1;    // rocket type: best contact tick this cmd, aim ARJ reads it

// aim tree (autorocketjump.cpp) runs after the movement pipeline in the same cmd. Take = read
// and clear, so a cmd the pipeline skipped can never replay an old contact
int Movement_TakeGoombaRocketContact()
{
    const int k = s_goombaRocketK;
    s_goombaRocketK = -1;
    return k;
}
// m_flSimulationTime (DT_BaseEntity); -1 = not in this build
static int Gmb_SimOff()
{
    static int s_off = 0;
    if (s_off == 0)
    {
        RecvTable* t = FindNetworkTable("DT_BaseEntity");
        const int off = t ? FindPropOffset(t, "m_flSimulationTime") : -1;
        s_off = off > 0 ? off : -1;
    }
    return s_off;
}
static float Gmb_SimTime(void* p) { const int o = Gmb_SimOff(); return o > 0 ? *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(p) + o) : 0.f; }

// ---- outcome tracker: after every fire, watch the target 40 cmds and log what it did. The fire
// is a guess, this is the measurement: maxVz = the boost they actually got, land = server ticks
// from our fire cmd to the snapshot that shows them standing (on us) ----
struct GmbTrack { int idx = 0, left = 0, type = 0, pred = 0, lead = 0, fireTick = 0, landTick = -1; float vzFire = 0.f, maxVz = -1e9f, myMaxVz = -1e9f; };
static GmbTrack s_gmbTrk;
static int   s_gmbBestIdx = 0, s_gmbBestK = -1, s_gmbMyTick = 0;   // this cmd's scan
static float s_gmbBestVz = 0.f;

static void Gmb_TrackStart(int type)
{
    if (s_gmbBestIdx <= 0) return;
    if (s_gmbTrk.left > 0)   // a fire before the last one finished: log what it had
        DebugLog("GOOMBA: result type=%d pred=%d lead=%d land=%d vzFire=%.0f maxVz=%.0f myMaxVz=%.0f (cut short)",
                 s_gmbTrk.type, s_gmbTrk.pred, s_gmbTrk.lead,
                 s_gmbTrk.landTick >= 0 ? s_gmbTrk.landTick - s_gmbTrk.fireTick : -99, s_gmbTrk.vzFire, s_gmbTrk.maxVz, s_gmbTrk.myMaxVz);
    s_gmbTrk = { s_gmbBestIdx, 40, type, s_gmbBestK, (std::max)(g_goombaLead, 0), s_gmbMyTick, -1, s_gmbBestVz, -1e9f, -1e9f };
}

static void Gmb_TrackTick(void* pLocal, float dt)
{
    GmbTrack& t = s_gmbTrk;
    if (t.left <= 0) return;
    // ours: what the landing could hand them (their gain ~= this when the transfer lands clean)
    t.myMaxVz = (std::max)(t.myMaxVz, EntV3(pLocal, g_offVelocity).z);
    void* p = GetClientEntity(t.idx);
    const bool ok = p && EntAlive(p) && !Gmb_Dormant(t.idx);
    if (ok)
    {
        t.maxVz = (std::max)(t.maxVz, EntV3(p, g_offVelocity).z);
        if (t.landTick < 0 && (EntI(p, g_offFlags) & FL_ONGROUND))
            t.landTick = (int)(Gmb_SimTime(p) / dt + 0.5f);
    }
    if (!ok || --t.left == 0)
    {
        DebugLog("GOOMBA: result type=%d pred=%d lead=%d land=%d vzFire=%.0f maxVz=%.0f myMaxVz=%.0f%s",
                 t.type, t.pred, t.lead, t.landTick >= 0 ? t.landTick - t.fireTick : -99,
                 t.vzFire, t.maxVz, t.myMaxVz, ok ? "" : " (target lost)");
        t.left = 0;
    }
}

void Movement_GoombaFired() { s_goombaFired = true; Gmb_TrackStart(1); }
int  Movement_GoombaTarget() { return s_gmbBestIdx; }   // this cmd's best target (0 = none), for the rocket plan

static void AutoGoomba(void* pLocal, UserCmd_t* pCmd)
{
    static float s_latchUntil = 0.f;   // one fire per drop: back-to-back cmds see the same contact
    s_goombaRocketK = -1;
    s_gmbBestIdx = 0; s_gmbBestK = -1;
    if (!g_goomba || !g_globalVars || !g_entityList) return;
    if (g_offOrigin <= 0 || g_offMins <= 0 || g_offMaxs <= 0 || g_offVelocity <= 0
        || g_offTeamNum <= 0 || g_offFlags <= 0) return;
    const float dtT = g_globalVars->interval_per_tick > 0.f ? g_globalVars->interval_per_tick : 0.015f;
    Gmb_TrackTick(pLocal, dtT);   // before the key gate: the outcome keeps playing after the key is let go
    s_gmbMyTick = g_offTickBase > 0 ? EntI(pLocal, g_offTickBase) : 0;
    if (!MovementKeyOK(g_goombaKey, g_goombaMode)) return;

    const float rt = g_globalVars->realtime;
    const float dt = g_globalVars->interval_per_tick > 0.f ? g_globalVars->interval_per_tick : 0.015f;
    const int   n  = (std::min)(g_globalVars->maxClients > 0 ? g_globalVars->maxClients : 101, 101);
    const int   lead = (std::max)(g_goombaLead, 0);
    const bool  rocket = g_goombaHow == 1;
    const bool  onGround = EntGround(pLocal);
    if (!rocket && rt < s_latchUntil) return;
    if (g_goombaHow == 2 && !onGround) return;   // a regular jump only leaves the floor

    const float myTime = (float)s_gmbMyTick * dt;

    const float g = CvarFloat("sv_gravity", 800.f);
    const bool  teamSolid = CvarFloat("tf_avoidteammates", 1.f) == 0.f;   // 1 = teammates pass through

    const Vec3  me = EntV3(pLocal, g_offOrigin), myVel = EntV3(pLocal, g_offVelocity);
    const Vec3  myMaxs = EntV3(pLocal, g_offMaxs);
    const float myHead = me.z + myMaxs.z;
    const int   myTeam = EntI(pLocal, g_offTeamNum);
    // jump = 289 set (tf_gamemovement.cpp:1315) then ballistic; a blast lands later, so until
    // then our head stands still on the floor or keeps its own air arc
    const bool  myFalls = g_goombaHow == 2 || !onGround;
    const float myVz0 = g_goombaHow == 2 ? 289.f : (onGround ? 0.f : myVel.z);
    // rocket: the ARJ decides against its own rocket flight time (up to ~10 ticks), so look further
    const int   maxK = rocket ? 14 + lead : 2 + lead;

    int best = -1;
    for (int i = 1; i <= n; ++i)
    {
        void* p = GetClientEntity(i);
        if (!p || p == pLocal || !EntAlive(p) || Gmb_Dormant(i)) continue;
        if (!teamSolid && EntI(p, g_offTeamNum) == myTeam) continue;
        if (EntI(p, g_offFlags) & FL_ONGROUND) continue;   // standing on us / anything: not falling

        // snapshot -> the server time our cmd lands at. Clamp: a stale simtime is a lag spike, not physics
        float t0 = 0.f;
        if (Gmb_SimOff() > 0 && myTime > 0.f)
            t0 = std::clamp(myTime - Gmb_SimTime(p), 0.f, 0.5f);

        const Vec3  org = EntV3(p, g_offOrigin);
        const Vec3  v = EntV3(p, g_offVelocity);
        const float vz = v.z - g * t0;
        if (vz >= -10.f) continue;   // only a drop (heroin goomba assist FUN_180075a10: vz < -10)
        const float gap = org.z + gmb::Rise(v.z, g, t0) + EntV3(p, g_offMins).z - myHead;
        if (gap <= 0.f || gap > 400.f) continue;

        const int k = gmb::ContactTick(gap, vz, myVz0, myFalls, g, dt, maxK);
        if (k < 0) continue;

        // still over our head at contact
        const float tk = t0 + dt * (float)k, tm = dt * (float)k;
        const float reach = myMaxs.x + EntV3(p, g_offMaxs).x - 1.f;
        if (fabsf(org.x + v.x * tk - (me.x + myVel.x * tm)) >= reach
            || fabsf(org.y + v.y * tk - (me.y + myVel.y * tm)) >= reach) continue;
        if (best < 0 || k < best) { best = k; s_gmbBestIdx = i; s_gmbBestVz = vz; }
    }
    s_gmbBestK = best;
    if (best < 0) return;
    if (rocket) { s_goombaRocketK = best; return; }

    // a held jump the tick before would make the fire tick's press a refusal
    if (gmb::ShouldPrime(best, lead)) { pCmd->buttons &= ~IN_JUMP; return; }
    if (!gmb::ShouldFire(best, lead)) return;

    // sticky = heroin's 0x806: det + jump + duck in one cmd off the floor (ctap sticky jump)
    if (g_goombaHow == 0) pCmd->buttons |= IN_ATTACK2 | (onGround ? IN_JUMP | IN_DUCK : 0);
    else                  pCmd->buttons |= IN_JUMP;
    s_goombaFired = true;
    s_latchUntil = rt + 0.3f;
    Gmb_TrackStart(g_goombaHow);
    DebugLog("GOOMBA: fire type=%d contact=%d lead=%d tick=%d", g_goombaHow, best, lead, s_gmbMyTick);
}

// ==== double jump strafe: scout AirDash writes velocity = wish vector (tf_gamemovement.cpp:1043,
// psdash.h), so the keys held on the dash tick DECIDE the new direction. Rewrite that one cmd's
// wish to the direction the hull already moves -> keys only pick speed, never turn. Dash tick =
// fresh IN_JUMP in the air with a dash in pocket; held jump is a pogo refusal (:1235) ====
static void DoubleJumpStrafe(void* pLocal, UserCmd_t* pCmd)
{
    if (!On(g_djStrafe, g_djStrafeKey, g_djStrafeMode) || g_offVelocity <= 0) return;
    if (!(pCmd->buttons & IN_JUMP) || s_prevCmdJump) return;
    if (EntGround(pLocal) || !CanAirDashNow(pLocal)) return;
    const Vec3 vel = EntV3(pLocal, g_offVelocity);
    if (vel.Length2D() < 1.f) return;   // no heading to keep
    // 450 = cl_forwardspeed: one key's worth, engine clamps to maxspeed anyway. No key = full too
    MV_WriteWishWorld(pCmd, vel, 450.f);
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
