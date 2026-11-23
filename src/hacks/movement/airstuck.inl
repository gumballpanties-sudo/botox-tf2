// airstuck.inl -- textually included by movement.cpp (same translation unit: Pred_Simulate / RestoreToFrame /
// TraceHull / TraceLine / EntV3 / s_prePred* / s_airStuckHit are its statics). Air stuck = botox-css airstuck.inl
// 11-08b (botox csgo-sdk air_stuck.cpp "lumi" core) 1:1. The old cumidere / aletherium / clarity engines are gone.
// The top half is infra other files of this TU use: AsSetOrigin (autobounce.inl), AsClock (autobounce.inl),
// AsSurfaceFriction (movement_edge.inl, TB, WC), AS_Sync (cl_pred_optimize arm for every exact-float pin feature:
// as + tb + wc).
//
// css -> tf2 shims, nothing else differs:
//   AT_CanSim            -> SimsLeft() > 0 (already 0 once TbBudgetExpired)
//   sg::WishToCmd        -> as::StampRotate(viewYaw, yaw, move, 0)   (same rotation)
//   sg::NormYaw          -> NormalizeAngle
//   AT_Stamp / s_atStamp -> s_asStamp below (TF2 TB has no shared stamp, only AS stamps)
//   g_asScreenMessage    -> always logs (DebugLog)
#include "airstuck_pure.h"
#include <cfloat>
#include <cstring>

int Aim_LastCommandAck();   // aim/aim.cpp

static void AsSetOrigin(void* pLocal, const Vec3& o)
{
    if (g_fnSetAbsOrigin) g_fnSetAbsOrigin(pLocal, o);
    EntSetV3(pLocal, g_offOrigin, o);
}

// CInput::m_pCommands[seq % 90], the sent cmd for a command number (log only)
static const UserCmd_t* AsUserCmd(int seq)
{
    if (!g_input || seq < 0) return nullptr;
    const UserCmd_t* pCmds = *reinterpret_cast<UserCmd_t**>(reinterpret_cast<uintptr_t>(g_input) + 264);
    if (!pCmds) return nullptr;
    const UserCmd_t* c = pCmds + (seq % 90);
    return c->command_number == seq ? c : nullptr;
}

// botox n_tick::c_sim_budget: a wall-clock slice of one engine tick, never past the cmd clock
struct AsClock
{
    long long freq = 1ll, start = 0ll, span = 0ll;
    static long long Now() { LARGE_INTEGER c{}; QueryPerformanceCounter(&c); return c.QuadPart; }
    void Start(float fraction, float interval)
    {
        LARGE_INTEGER f{}; QueryPerformanceFrequency(&f);
        freq  = f.QuadPart > 0 ? f.QuadPart : 1ll;
        start = Now();
        span  = (long long)((double)freq * (double)interval * (double)std::clamp(fraction, 0.02f, 0.9f));
        const long long left = TbBudgetLeftUs() * freq / 1000000ll;
        if (span > left) span = left;
    }
    long long UsedUs() const { return (Now() - start) * 1000000ll / freq; }
    bool Expired() const { return Now() - start >= span; }
};

// m_surfaceFriction is a prediction datamap field (c_baseplayer.cpp:409), not a netvar
static int AsDatamapOffset(datamap_t* map, const char* name)
{
    constexpr int FIELD_EMBEDDED = 10;
    for (; map; map = map->baseMap)
    {
        for (int i = 0; i < map->dataNumFields; ++i)
        {
            const typedescription_t& td = map->dataDesc[i];
            if (td.fieldName && strcmp(td.fieldName, name) == 0) return td.fieldOffset[0];
            if (td.fieldType == FIELD_EMBEDDED && td.td)
            {
                const int o = AsDatamapOffset(td.td, name);
                if (o >= 0) return td.fieldOffset[0] + o;
            }
        }
    }
    return -1;
}
static float AsSurfaceFriction(void* pLocal)
{
    static int s_off = -2;   // -2 unresolved, -1 missing
    if (s_off == -2)
    {
        datamap_t* map = EntPredDescMap(pLocal);
        s_off = map ? AsDatamapOffset(map, "m_surfaceFriction") : -1;
        DebugLog("AS: m_surfaceFriction off=%d", s_off);
    }
    if (s_off <= 0) return 1.f;
    const float f = EntF(pLocal, s_off);
    return (f >= 0.1f && f <= 1.f) ? f : 1.f;   /* datamap miss reads garbage: never trust it */
}

static int s_asSvAcks = 0, s_asSvStops = 0;   // AS_Sync diag, printed + reset per second by AirStuck

/* online the engine keeps its own START while the server's END differs < 0.0625u / 0.5u/s ( cl_pred_optimize 2, c_baseentity.cpp:656/681,
   prediction.cpp:1681 ); pins are exact floats, so armed = full re-predict off the server snapshot every packet ( offline already is ).
   log: server END of the acked cmd vs the START we built on */
static void AS_SyncArm(const bool on)
{
    static int saved = -1;
    static bool armed = false;
    s_predSyncArmed = on;   // engine's own run drops the moved flag too (h_ProcessMovementCl)
    if (!g_cvar) return;
    ConVar* cv = g_cvar->FindVar("cl_pred_optimize");
    if (!cv) return;
    if (on && !armed)
    {
        saved = (int)cv->GetFloat();
        cv->SetValueInt("cl_pred_optimize", (std::min)(saved, 1));
        DebugLog("PRED: sync arm owner=as opt=%d->%d", saved, (int)cv->GetFloat());
        armed = true;
    }
    else if (!on && armed)
    {
        if (saved >= 0) cv->SetValueInt("cl_pred_optimize", saved);
        saved = -1;
        armed = false;
    }
}

static void AS_Sync(void* pLocal, UserCmd_t* cmd, const bool armed)
{
    AS_SyncArm(armed || s_wcSyncWant || s_tbSyncWant || s_phSyncWant || s_esurfSyncWant);   // botox sync_arm owners: as + tb + wc + es (+ ph) ( all sub-ulp: pins are exact floats )

    struct rec_t {
        int cmd = -1, tick_base = 0;
        Vec3 origin{}, velocity{};
    };
    static rec_t ring[128];
    static int last_ack = -1, ok = 0;
    if (!armed) {
        last_ack = -1;
        return;
    }
    const Vec3 origin   = EntV3(pLocal, g_offOrigin);
    const Vec3 velocity = EntV3(pLocal, g_offVelocity);
    const int tick_base = EntI(pLocal, g_offTickBase);
    const int prev      = cmd->command_number - 1;
    ring[prev & 127]    = { prev, tick_base, origin, velocity };

    const int ack = Aim_LastCommandAck();
    if (ack < 0 || ack == last_ack || PredCommandsPredicted() <= 0 || ring[ack & 127].cmd != ack)
        return;
    last_ack = ack;

    RestoreToFrame(-1);
    const Vec3 sv_origin   = EntV3(pLocal, g_offOrigin);
    const Vec3 sv_velocity = EntV3(pLocal, g_offVelocity);
    const int sv_tick_base = EntI(pLocal, g_offTickBase);
    RestoreToFrame(PredCommandsPredicted() - 1);
    AsSetOrigin(pLocal, origin);
    EntSetV3(pLocal, g_offVelocity, velocity);
    EntSetI(pLocal, g_offTickBase, tick_base);

    // diag: acked cmds the SERVER ended on -halfgrav (stuck). wb0 sims never see these (11-16 log: 53 server
    // stops / ~500 ticks, 0 predicted), so this is the only measure of how well AS sticks there
    ++s_asSvAcks;
    if (IsTargetVz(sv_velocity.z, as::k_pinTol)) ++s_asSvStops;

    const Vec3 d  = sv_origin - ring[ack & 127].origin;
    const Vec3 dv = sv_velocity - ring[ack & 127].velocity;
    if (d.x == 0.0f && d.y == 0.0f && d.z == 0.0f && dv.x == 0.0f && dv.y == 0.0f && dv.z == 0.0f) {
        ++ok;
        return;
    }
    /* same cmd as the one before + dtb 0 + a pinned START = server float noise; dtb != 0 = server ran an extra / null cmd ( player.cpp:3603 ) */
    const UserCmd_t* const a = AsUserCmd(ack);
    const UserCmd_t* const b = AsUserCmd(ack - 1);
    const bool same_cmd = a && b && a->viewangles.x == b->viewangles.x && a->viewangles.y == b->viewangles.y
                       && a->forwardmove == b->forwardmove && a->sidemove == b->sidemove && a->buttons == b->buttons;
    DebugLog("AS: sync miss ack=%d ok=%d d=%.3g,%.3g,%.3g dv=%.3g,%.3g,%.3g vz=%.3f cmd=%.4f/%.4f v=%.4f/%.4f b=%x same=%d dtb=%d",
             ack, ok, d.x, d.y, d.z, dv.x, dv.y, dv.z, sv_velocity.z,
             a ? a->forwardmove : 0.f, a ? a->sidemove : 0.f, a ? a->viewangles.x : 0.f, a ? a->viewangles.y : 0.f,
             a ? a->buttons : 0, (int)same_cmd, sv_tick_base - ring[ack & 127].tick_base);
    ok = 0;
}

// the cmd AS proved this tick, keyed to its command number (css AT_Stamp / s_atStamp)
static struct { int cmd = -1; Vec3 view{}; float fwd = 0.f, side = 0.f; int buttons = 0; } s_asStamp;

// ============================================================================
// AIR STUCK -- botox air_stuck.cpp "lumi" core (csgo-sdk hacks/movement/air_stuck.cpp:628-868), css 11-08b 1:1.
// A wish almost PARALLEL to the face (78..90 deg off straight-in, turned with your drift) at 1..120 u/s,
// pinned = END vz on -h (0.05, nudge-aware IsTargetVz), ranked by how many ticks the cmd keeps pinning
// (repeat = fixed point = held for good), then by the smallest gap. The best cmd is SENT every tick even
// unpinned: that approach steers the hull into the catch.
//   hold    START pinned by last tick's 3+ pin -> replay it, 1 sim proves it
//   resend  START pinned, nothing re-pins 3+ -> resend last tick's cmd (botox :379, server pins our sim misses)
// Wall: 0.05u hull contact first (the face we touch), else 16-heading x 1u line column, reach hull edge + "extra
// reach" (0.05..32). Face past as::k_fanGap off the hull = approach (straight-in full press, no sims), else fan.
// ============================================================================
static bool s_asHolding = false;   // a 3+ tick pin (or its hold / resend) went out this cmd
static struct
{
    bool  hold = false; float yaw = 0.f, move = 0.f;                   // s_hold
    int   lastCmd = -1; float lastYaw = 0.f, lastMove = 0.f;          // s_last
    float startAng = 0.f;
    bool  latched = false; int len = 0;
    bool  freeze = false;   // last cmd = a freeze (or its hold replay): frozen = no move, skip the drift hang
    int   sims = 0, holds = 0, pins = 0, steers = 0, resends = 0, nowall = 0, drifts = 0, hangs = 0, freezes = 0;
} s_as;

static void AirStuck(void* pLocal, UserCmd_t* cmd)
{
    s_asHolding = false;
    /* an edge surf feet approach owns this fall ( botox air_stuck.cpp:323 latch ): a hug / park toward its face over the top lands
       you on it. TF2 runs AS after TB ( botox: before ), so the cmd edge surf owns right now counts too */
    const bool esurf = ESurf_Latched(cmd) || s_esurfActive;
    const bool on = g_airStuck && MovementKeyOK(g_airStuckKey, g_airStuckMode) && EntAlive(pLocal) && !esurf;
    AS_Sync(pLocal, cmd, on);
    {
        static int s_sec = -1;
        const int sec = g_globalVars ? (int)g_globalVars->curtime : 0;
        if (sec != s_sec)
        {
            s_sec = sec;
            if (g_debugLogEnable && (s_as.holds || s_as.pins || s_as.steers || s_as.resends || s_as.nowall || s_as.drifts || s_as.hangs
                                     || s_as.freezes))
                DebugLog("AS: holds=%d hangs=%d drifts=%d freezes=%d pins=%d steers=%d resends=%d nowall=%d sims=%d svstop=%d/%d",
                         s_as.holds, s_as.hangs, s_as.drifts, s_as.freezes, s_as.pins, s_as.steers, s_as.resends, s_as.nowall,
                         s_as.sims, s_asSvStops, s_asSvAcks);
            s_as.holds = s_as.pins = s_as.steers = s_as.resends = s_as.nowall = s_as.sims = s_as.drifts = s_as.hangs = 0;
            s_as.freezes = 0;
            s_asSvAcks = s_asSvStops = 0;
        }
    }
    const auto end = [&](const char* why) {
        if (s_as.latched) DebugLog("AS: unstuck why=%s held=%d z=%.3f", why, s_as.len, s_prePredOrigin.z);
        s_as.latched = false; s_as.len = 0; s_as.hold = false; s_as.freeze = false;
    };
    // ps assist arc in flight / pending: sim-proven, AS move+duck stomp = off the arc (log: "AS: lumi" on
    // the launch tick, outcome liveA != promA, hop cut before the seam). Ride hold is NOT the arc: AS keeps it
    if (!on || (s_prePredFlags & FL_ONGROUND) || EntMoveType(pLocal) != MOVETYPE_WALK || s_pixelHopHit
        || s_psAssistArcOwns)
    {
        end(esurf ? "esurf" : !on ? "key" : s_pixelHopHit ? "ph" : s_psAssistArcOwns ? "ps" : "ground");
        return;
    }

    const int   nBase   = PredCommandsPredicted();
    const float dt      = TickInterval();
    const Vec3  origin  = s_prePredOrigin;
    const Vec3  vel     = s_prePredVel;
    const float viewYaw = cmd->viewangles.y;
    const UserCmd_t live = *cmd;
    RestoreToFrame(nBase - 1);
    const Vec3  mins = EntV3(pLocal, g_offMins), maxs = EntV3(pLocal, g_offMaxs);
    const float extX = (std::max)(fabsf(mins.x), fabsf(maxs.x)), extY = (std::max)(fabsf(mins.y), fabsf(maxs.y));
    const bool  startPinned = IsTargetVz(vel.z, 0.05f);
    // a freeze (or its hold) went out last cmd and START is not pinned: refused (offline the engine, online the server)
    if (s_as.freeze && !startPinned)
    {
        DebugLog("AS: freeze lost z=%.3f vel=%.3f,%.3f,%.3f cp=%d", origin.z, vel.x, vel.y, vel.z, nBase);
        s_as.freeze = false;
    }
    AsClock clock;
    clock.Start(std::clamp(g_asBudget, 5.f, 90.f) * 0.01f, dt);

    const auto canSim = []() { return SimsLeft() > 0; };
    const auto pinnedNow = [&]() {
        return !(EntI(pLocal, g_offFlags) & FL_ONGROUND) && EntMoveType(pLocal) == MOVETYPE_WALK
            && IsTargetVz(EntV3(pLocal, g_offVelocity).z, 0.05f);
    };
    const auto cmdFor = [&](float yaw, float move) {
        UserCmd_t c = live;
        as::StampRotate(viewYaw, yaw, move, 0.f, c.forwardmove, c.sidemove);
        c.upmove = 0.f;
        c.buttons &= ~(IN_JUMP | IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);
        return c;
    };
    // ticks the cmd keeps pinning from START (k_chain = fixed point), -1 = no sim budget
    constexpr int k_chain = 16;
    const auto chain = [&](const UserCmd_t& c, Vec3* endOut) -> int {
        RestoreToFrame(nBase - 1);
        if (!canSim()) return -1;
        UserCmd_t c0 = c;
        Pred_Simulate(pLocal, &c0);
        ++s_as.sims;
        int ticks = 0;
        if (pinnedNow())
        {
            ticks = 1;
            for (int k = 1; k < k_chain && canSim(); ++k)
            {
                const Vec3 from = EntV3(pLocal, g_offOrigin), fromV = EntV3(pLocal, g_offVelocity);
                UserCmd_t cc = c;
                Pred_Simulate(pLocal, &cc);
                ++s_as.sims;
                if (!pinnedNow()) break;
                ++ticks;
                const Vec3 o = EntV3(pLocal, g_offOrigin), v = EntV3(pLocal, g_offVelocity);
                if (o.x == from.x && o.y == from.y && o.z == from.z && v.x == fromV.x && v.y == fromV.y && v.z == fromV.z)
                    { ticks = k_chain; break; }
            }
        }
        if (endOut) *endOut = EntV3(pLocal, g_offOrigin);
        return ticks;
    };
    const auto send = [&](float yaw, float move, bool pin, const char* what) {
        *cmd = cmdFor(yaw, move);
        s_asStamp = { cmd->command_number, cmd->viewangles, cmd->forwardmove, cmd->sidemove, cmd->buttons };
        s_as.lastCmd = cmd->command_number; s_as.lastYaw = yaw; s_as.lastMove = move;
        s_as.hold = pin; s_as.yaw = yaw; s_as.move = move;
        s_as.freeze = false;   // only the freeze stage + its hold replay set / carry it
        s_asHolding = pin;
        if (pin && !s_as.latched)
            DebugLog("AS: stuck by=%s yaw=%.2f fwd=%.3f z=%.3f vz=%.2f xy=%.2f", what, yaw, move, origin.z, vel.z, vel.Length2D());
        if (pin) { s_as.latched = true; ++s_as.len; }
        else if (s_as.latched) end("lost");
    };
    // botox :380 resend: START pinned, the cmd that pinned it beats any unproven move
    const auto resend = [&]() {
        if (!startPinned || s_as.lastCmd != cmd->command_number - 1) return false;
        ++s_as.resends;
        send(s_as.lastYaw, s_as.lastMove, true, "resend");
        return true;
    };

    // ---- drift hang (sv_wallbug 0): START pinned against a face -> stop the hull where it is ----------
    // Valve's Oct 2026 clip leaves 0.03125 u/s off every plane it hits, so replaying a fixed cmd (the hold
    // below) re-clips each tick and the hull creeps off its lip in ~10 ticks; a flat face never nails it at
    // all (lumi: pin=0 on all 159 candidates). What still holds is a lip / seam pin with the hull NOT
    // drifting: MV_DriftHold (movement.cpp) brakes xy to 0 and leaves the across-face velocity a hair
    // away, re-solved every tick and proven 16 ticks closed loop. On sv_wallbug 1 / an exact-zero crease
    // the bare tick has no offset and it simply parks; if it cannot hold, the botox path below runs
    // a frozen START (freeze = no move, no clip) has no drift to hang against: its hold replay below owns it
    if (startPinned && g_asDriftHang && !s_as.freeze)
    {
        TraceFilterWorldProps cfil; cfil.Setup(pLocal);
        Vec3 cn{};
        float cbest = 1.f;
        for (int i = 0; i < 4; ++i)
        {
            const float a = (float)(i * 90) * 0.017453292f;
            GameTrace_t tr{};
            TraceHull(origin, Vec3(origin.x + cosf(a) * 0.1f, origin.y + sinf(a) * 0.1f, origin.z), mins, maxs,
                      MASK_PLAYERSOLID, &cfil, &tr);
            if (tr.DidHit() && !tr.startsolid && fabsf(tr.plane.normal.z) <= 0.35f && tr.fraction < cbest)
                { cbest = tr.fraction; cn = tr.plane.normal; }
        }
        const float cl = sqrtf(cn.x * cn.x + cn.y * cn.y);
        if (cbest < 1.f && cl > 1e-4f)
        {
            const int horizon = as::TicksScaled(16, dt);
            UserCmd_t c = cmdFor(viewYaw, 0.f);   // AS's own cmd shape: movement keys / jump stripped
            int sims = 0;
            const Vec3 cdir(cn.x / cl, cn.y / cl, 0.f);

            // ---- LIP HANG first on sv_wallbug 0: brake along the face, keep pressing INTO it ----------
            // A pin on wallbug 0 is a lip / seam top plane, and the trace only counts that brush while the
            // move CROSSES its front plane (a box ending in front of a plane = brush skipped). So the hull
            // must keep closing on the face; the hair-away hold below skips the lip and falls (drifts=0 in
            // every wallbug 0 log). A tick that pins clips top then face and is left (along, +1/32, +1/32);
            // with along ~0 that points against the pre-move velocity -> stop dead (gamemovement.cpp:2782):
            // the hull does not move at all, the next tick is the same tick = fixed point, and Valve's
            // 1/32 push rides in the bump that never runs. as::HangWish re-solves every tick for 0 along +
            // `cIn` u/s in; each press flown closed loop, sims judge. Model from SDK TryPlayerMove + the
            // engine brush clip rule (not in the SDK): UNVERIFIED in game
            if (CvarFloat("sv_wallbug", 0.f) < 0.5f)
            {
                const float k    = CvarFloat("sv_airaccelerate", 10.f) * dt * AsSurfaceFriction(pLocal);
                const float wmax = g_offMaxspeed > 0 ? EntF(pLocal, g_offMaxspeed) : 450.f;
                int   bestHeld = 0;
                float bestC = 0.f, bestFwd = 0.f, bestSide = 0.f;
                Vec3  bestWish{};
                // 4.5 = 30 wish from still; the press must cross the face shell each tick (>= ~2.1 u/s)
                for (const float cIn : { 4.5f, 3.f, 7.f, 11.f, 20.f })
                {
                    if (SimsLeft() < horizon || clock.Expired()) break;
                    RestoreToFrame(nBase - 1);
                    const int base = EntI(pLocal, g_offTickBase);
                    int held = 0;
                    float f0 = 0.f, s0 = 0.f;
                    Vec3 w0{};
                    for (int z = 0; z < horizon && canSim(); ++z)
                    {
                        const Vec3 v = EntV3(pLocal, g_offVelocity), o = EntV3(pLocal, g_offOrigin);
                        float dx, dy, m;
                        as::HangWish(v.x, v.y, cdir.x, cdir.y, cIn, k, wmax, dx, dy, m);
                        MV_WriteWishWorld(&c, Vec3(dx, dy, 0.f), m);
                        if (z == 0) { f0 = c.forwardmove; s0 = c.sidemove; w0 = Vec3(dx * m, dy * m, 0.f); }
                        EntSetI(pLocal, g_offTickBase, base + z);
                        Pred_Simulate(pLocal, &c);
                        ++sims;
                        if (!pinnedNow()) break;
                        ++held;
                        const Vec3 v2 = EntV3(pLocal, g_offVelocity), o2 = EntV3(pLocal, g_offOrigin);
                        if (o2.x == o.x && o2.y == o.y && o2.z == o.z && v2.x == v.x && v2.y == v.y && v2.z == v.z)
                            { held = horizon; break; }   // same state in, same state out: held for good
                    }
                    RestoreToFrame(nBase - 1);
                    if (held > bestHeld) { bestHeld = held; bestC = cIn; bestFwd = f0; bestSide = s0; bestWish = w0; }
                    if (bestHeld >= horizon) break;
                }
                s_as.sims += sims;
                sims = 0;
                if (bestHeld >= horizon)
                {
                    ++s_as.hangs;
                    send(RAD2DEGF(atan2f(bestWish.y, bestWish.x)), bestWish.Length2D(), true, "hang");
                    cmd->forwardmove = bestFwd;   // the proven bits, not send()'s re-rotation of them
                    cmd->sidemove    = bestSide;
                    s_asStamp.fwd    = bestFwd;
                    s_asStamp.side   = bestSide;
                    s_as.hold        = false;     // re-solved every tick, never replayed
                    static int s_hLog = -1000;
                    if (g_debugLogEnable && cmd->command_number - s_hLog > 16)
                    {
                        s_hLog = cmd->command_number;
                        DebugLog("AS: hang c=%.1f n=%.3f,%.3f wish=%.2f xy=%.2f z=%.3f vz=%.3f", bestC, cdir.x, cdir.y,
                                 bestWish.Length2D(), vel.Length2D(), origin.z, vel.z);
                    }
                    return;
                }
                static int s_hsLog = -1000;
                if (g_debugLogEnable && bestHeld > 0 && cmd->command_number - s_hsLog > 16 && (s_hsLog = cmd->command_number, true))
                    DebugLog("AS: hang short held=%d/%d c=%.1f xy=%.2f z=%.3f vz=%.3f", bestHeld, horizon, bestC,
                             vel.Length2D(), origin.z, vel.z);
            }

            MvDrift dh = MV_DriftHold(pLocal, &c, nBase - 1, cdir, true, horizon, sims);
            // short = CREEP INTO THE FACE: close half the gap to the trace shell every tick. The hull never
            // reaches the shell, so no clip fires and Valve's +DIST_EPSILON push never happens (MV_DriftHold
            // has the SDK why). Pressing harder than that only re-clips. The longer hold wins
            char dmode = 'd';
            if (dh.held < horizon)
            {
                const MvDrift dc = MV_DriftHold(pLocal, &c, nBase - 1, cdir, true, horizon, sims, nullptr, true);
                if (dc.held > dh.held) { dh = dc; dmode = 'c'; }
            }
            s_as.sims += sims;
            if (dh.held >= horizon)
            {
                ++s_as.drifts;
                send(RAD2DEGF(atan2f(dh.wish.y, dh.wish.x)), dh.wish.Length2D(), true, "drift");
                cmd->forwardmove = dh.fwd;   // the proven bits, not send()'s re-rotation of them
                cmd->sidemove    = dh.side;
                s_asStamp.fwd    = dh.fwd;
                s_asStamp.side   = dh.side;
                s_as.hold        = false;    // re-solved every tick, never replayed
                static int s_dLog = -1000;
                if (g_debugLogEnable && cmd->command_number - s_dLog > 16)
                {
                    s_dLog = cmd->command_number;
                    DebugLog("AS: drift %c n=%.3f,%.3f wish=%.4f k=%.4f va=%.5f doff=%.5f xy=%.2f z=%.3f vz=%.3f",
                             dmode, cn.x / cl, cn.y / cl, dh.wish.Length2D(), dh.k, dh.vaNone, dh.doff, vel.Length2D(),
                             origin.z, vel.z);
                }
                return;
            }
        }
        RestoreToFrame(nBase - 1);
    }

    // ---- hold: last tick's 3+ pin, START still pinned -> replay, 1 sim proves it (botox :312) ----
    if (s_as.hold && startPinned)
    {
        RestoreToFrame(nBase - 1);
        if (canSim())
        {
            UserCmd_t c = cmdFor(s_as.yaw, s_as.move);
            Pred_Simulate(pLocal, &c);
            ++s_as.sims;
            const bool held = pinnedNow();
            RestoreToFrame(nBase - 1);
            if (held)
            {
                const bool fz = s_as.freeze;   // csgo keep_freeze: the hold of a freeze is still a freeze
                ++s_as.holds;
                send(s_as.yaw, s_as.move, true, "hold");
                s_as.freeze = fz;
                return;
            }
            DebugLog("AS: hold break t=%d yaw=%.1f fwd=%.4f", s_as.len, s_as.yaw, s_as.move);
        }
    }

    // ---- wall: 0.05u hull contact, else 16-heading line column (botox :404-424, :628-683) ----
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    bool found = false;
    Vec3 n{};
    GameTrace_t ctr{};   // the face the hull touches (the freeze reads its plane dist)
    for (int i = 0; i < 4 && !found; ++i)
    {
        const float a = (float)(i * 90) * 0.017453292f;
        GameTrace_t tr{};
        TraceHull(origin, Vec3(origin.x + cosf(a) * 0.05f, origin.y + sinf(a) * 0.05f, origin.z), mins, maxs, MASK_PLAYERSOLID, &fil, &tr);
        if (tr.DidHit() && fabsf(tr.plane.normal.z) <= 0.35f) { found = true; n = tr.plane.normal; ctr = tr; }
    }
    const bool touching = found;
    if (found) s_as.startAng = atan2f(-n.y, -n.x);
    // the column reaches the full "extra reach" slider (was capped at 2u here, so 4 = 2): a face past
    // as::k_fanGap is an APPROACH target below, not a fan target
    const float extra = std::clamp(g_asExtraReach, 0.05f, 32.f);
    float gap = 0.f;   // hull edge -> face along the found heading, 0 = hull contact
    for (float a = s_as.startAng; !found && a < s_as.startAng + 6.2831853f; a += 0.3926991f)
    {
        const float c = cosf(a), s = sinf(a);
        const float edge  = (std::min)(extX / (std::max)(fabsf(c), 1e-6f), extY / (std::max)(fabsf(s), 1e-6f));
        const float reach = edge + extra;
        float bestFrac = 1.f;
        for (float z = origin.z + mins.z; z <= origin.z + maxs.z; z += 1.f)
        {
            GameTrace_t tr{};
            TraceLine(Vec3(origin.x, origin.y, z), Vec3(origin.x + c * reach, origin.y + s * reach, z), MASK_PLAYERSOLID, &fil, &tr);
            const Vec3& tn = tr.plane.normal;
            if (tr.fraction < 1.f && fabsf(tn.z) <= 0.35f && tn.x * c + tn.y * s <= -0.1f && tr.fraction < bestFrac)
                { found = true; bestFrac = tr.fraction; n = tn; }
        }
        if (found) { s_as.startAng = a; gap = (std::max)(0.f, as::WallGap(bestFrac, reach, edge)); }
    }
    if (!found)
    {
        ++s_as.nowall;
        if (!resend()) { s_as.startAng = 0.f; end("nowall"); }
        RestoreToFrame(nBase - 1);
        return;
    }

    // ---- freeze: exact-axis face, hull on it (botox csgo air_stuck.cpp 11-15) ------------------------------
    // TryPlayerMove re-tests a full-distance bump UNSWEPT at endpos (gamemovement.cpp:2636): startsolid there =
    // velocity 0 + break before SetAbsOrigin = no move, END vel (0, 0, pin). No ClipVelocity on that path, so
    // sv_wallbug never reaches it. csgo: box brushes count touching as inside in absolute coords while the swept
    // test is relative to START = a band at the face; END aimed straight in at true d = k q/16. Whether TF2's
    // engine has that band is UNVERIFIED (engine code not in the SDK): sims judge, `freezes=0` = it does not.
    // A fixed point (same cmd re-freezes, chain = k_chain) wins, else the longest; a 1-tick freeze is still sent:
    // frozen = no move, so next tick solves again from the same START at rest, where the fixed point is
    if (g_asFreeze && touching && !ctr.startsolid && (fabsf(n.x) == 1.f || fabsf(n.y) == 1.f))
    {
        const as::FzPlane P{ n.x, n.y, ctr.plane.dist };
        const as::FzHull  H{ (maxs.x - mins.x) * 0.5f, (maxs.y - mins.y) * 0.5f, (mins.x + maxs.x) * 0.5f, (mins.y + maxs.y) * 0.5f };
        const float d1 = as::FzEngD(origin.x, origin.y, P, H);
        if (d1 > 0.f)   // d <= 0 = startsolid, not a pin
        {
            const double dt1  = as::FzTrueD(origin.x, origin.y, P, H);
            const float  q1   = as::FzGridQ(origin.x, origin.y, P, H);
            const float  aa   = CvarFloat("sv_airaccelerate", 10.f), fric = AsSurfaceFriction(pLocal);
            const float  wmax = g_offMaxspeed > 0 ? EntF(pLocal, g_offMaxspeed) : 450.f;
            const float  v0n  = vel.x * n.x + vel.y * n.y;   // StartGravity only touches z
            constexpr int k_order[8] = { 16, 12, 20, 8, 24, 10, 14, 18 };
            float ends[8]{};
            int   nEnds = 0, tried = 0, bestK = 0, bestT = 0;
            float bestYaw = 0.f, bestWs = 0.f;
            const int simsAt = s_as.sims;
            for (const int k : k_order)
            {
                if (bestT >= k_chain || !canSim() || (tried > 0 && clock.Expired())) break;
                float ws = 0.f, sg = 0.f;
                if (!as::FzWish(k, q1, dt1, v0n, dt, aa * dt * fric, 30.f, wmax, ws, sg)) continue;
                float vx = vel.x, vy = vel.y;
                as::FzAccel(vx, vy, n.x * sg, n.y * sg, ws, aa, dt, fric, 30.f, wmax);
                const float dEnd = as::FzEngDAfter(origin.x, origin.y, P, H, vx, vy, dt);
                if (std::find(ends, ends + nEnds, dEnd) != ends + nEnds) continue;   // same END float = same sim
                ends[nEnds++] = dEnd;
                ++tried;
                const float yaw   = RAD2DEGF(atan2f(n.y * sg, n.x * sg));
                const int   ticks = chain(cmdFor(yaw, ws), nullptr);
                if (ticks > bestT) { bestT = ticks; bestK = k; bestYaw = yaw; bestWs = ws; }
            }
            RestoreToFrame(nBase - 1);
            static int s_fLog = -1000;
            const bool logNow = g_debugLogEnable && cmd->command_number - s_fLog > 16;
            if (bestT > 0)
            {
                ++s_as.freezes;
                send(bestYaw, bestWs, true, "freeze");
                s_as.freeze = true;
                if (logNow)
                {
                    s_fLog = cmd->command_number;
                    DebugLog("AS: freeze k=%d ticks=%d yaw=%.2f fwd=%.4f d=%.3g q=%.3g vn=%.3f xy=%.2f try=%d sims=%d cp=%d",
                             bestK, bestT, bestYaw, bestWs, dt1, q1, v0n, vel.Length2D(), tried, s_as.sims - simsAt, nBase);
                }
                return;
            }
            if (tried && logNow)
            {
                s_fLog = cmd->command_number;
                DebugLog("AS: freeze miss d=%.3g q=%.3g vn=%.3f try=%d sims=%d", dt1, q1, v0n, tried, s_as.sims - simsAt);
            }
        }
    }

    // ---- approach: face in reach, hull not on it -> straight-in full press, no sims ---------------------
    // Falling past a wall the window is a few ticks. Lumi's steer ( 1..120 wish, 78..90 deg off straight-in )
    // pulls ~0.3 u/s per tick; a full press straight in is the AirAccelerate cap ( 30 u/s wish = 4.5 u/s per
    // tick ), hull flush in the fewest ticks, THEN the fan pins it. Resend keeps a pinned START ( botox :380 )
    if (gap > as::k_fanGap)
    {
        if (resend()) return;
        const float wallYaw = RAD2DEGF(atan2f(-n.y, -n.x));
        ++s_as.steers;
        send(wallYaw, 450.f, false, "approach");
        static int s_aLog = -1000;
        if (g_debugLogEnable && cmd->command_number - s_aLog > 16)
        {
            s_aLog = cmd->command_number;
            DebugLog("AS: approach n=%.3f,%.3f gap=%.2f yaw=%.1f vz=%.2f xy=%.1f", n.x, n.y, gap, wallYaw, vel.z, vel.Length2D());
        }
        RestoreToFrame(nBase - 1);
        return;
    }

    // ---- lumi fan (botox :735-838) ----
    const float wallYaw = RAD2DEGF(atan2f(-n.y, -n.x));
    float drift = fmodf(RAD2DEGF(atan2f(vel.y, vel.x)) - wallYaw + 180.f, 360.f);
    if (drift < 0.f) drift += 360.f;
    const float side = drift < 180.f ? 1.f : -1.f;
    const float nl = sqrtf(n.x * n.x + n.y * n.y);
    const Vec3  n2 = nl > 1e-4f ? Vec3(n.x / nl, n.y / nl, 0.f) : Vec3(1.f, 0.f, 0.f);
    const float support = fabsf(n2.x) * extX + fabsf(n2.y) * extY;
    TraceFilterWorldOnly wfil; wfil.Setup();
    int cands = 0, bestTicks = 0;
    float bestGap = FLT_MAX, bestYaw = wallYaw, bestMove = 0.f;
    bool  bestWide = false, scoringWide = false;
    const auto outOfTime = [&]() { return cands >= 512 || (cands > 0 && clock.Expired()) || !canSim(); };
    const auto tryMove = [&](float rel, float move, float gapLen) {
        ++cands;
        const float yaw = NormalizeAngle(wallYaw + rel * side);
        Vec3 e{};
        const int ticks = chain(cmdFor(yaw, move), &e);
        if (ticks < 0) return;
        GameTrace_t tr{};
        TraceLine(e, Vec3(e.x - n2.x * gapLen, e.y - n2.y * gapLen, e.z), 0xFFFFFFFFu, &wfil, &tr);
        const float gap = tr.fraction * gapLen - support;
        if (ticks > bestTicks || (ticks == bestTicks && gap < bestGap))
            { bestTicks = ticks; bestGap = gap; bestYaw = yaw; bestMove = move; bestWide = scoringWide; }
    };
    // gap ray = support + 8 / + 40: botox's fixed 32 / 64 on an axis wall (support 24), but a 45 deg face
    // sits 33.9u from the box centre (corner contact), so the old 32u ray never reached it (log gap=-1.941
    // = a miss read as "inside") and the tiebreak was blind there
    // 250 / 450: hard presses every tick, not only when the wide cursor comes round (sims judge, a miss = 1 sim)
    for (const float move : { 1.f, 5.f, 30.f, 250.f, 450.f })
        for (float rel = 85.999f; rel < 90.f && !outOfTime(); rel += 1.f) tryMove(rel, move, support + 8.f);

    // ---- wide sweep: every heading, every press size -------------------------------------------------------
    // lumi's fan (78..90 deg off straight-in, 1..120) is tuned for an AXIS wall under the old clip. On a
    // non-aligned face the box meets the plane at a corner, with the brush's bevels and an inexact float
    // normal next to it -- a different clip set, and on sv_wallbug 0 the fan found pin=0 on all 159
    // candidates there (11-08 log, n=0.707,-0.707). The engine sims are the only judge of what can still
    // pin, so ask them: 72 headings x 6 presses = 432 candidates on a rotating cursor, k_widePerTick a tick
    // (a full sweep in ~9 ticks of standing at the wall), run BEFORE lumi's long pass so it gets the clock
    static int s_wideCursor = 0;
    constexpr int   k_wideYaws = 72;
    constexpr float k_wideMoves[] = { 1.f, 10.f, 30.f, 100.f, 250.f, 450.f };
    constexpr int   k_wideMovesN = (int)(sizeof(k_wideMoves) / sizeof(k_wideMoves[0]));
    constexpr int   k_widePerTick = 48;
    if (bestTicks < 3)
    {
        scoringWide = true;
        for (int c = 0; c < k_widePerTick && !outOfTime(); ++c)
        {
            const int idx = s_wideCursor++ % (k_wideYaws * k_wideMovesN);
            tryMove(5.f * (float)(idx / k_wideMovesN), k_wideMoves[idx % k_wideMovesN], support + 40.f);
        }
        scoringWide = false;
    }
    if (bestTicks < k_chain)
        for (const float move : { 1.f, 5.f, 30.f, 60.f, 90.f, 120.f, 250.f, 450.f })
            for (float rel = 78.f; rel < 90.f && !outOfTime(); rel += 0.5f) tryMove(rel, move, support + 40.f);
    RestoreToFrame(nBase - 1);

    static int s_logCmd = -1000;
    if (g_debugLogEnable && cmd->command_number - s_logCmd > 16)
    {
        s_logCmd = cmd->command_number;
        DebugLog("AS: lumi n=%.3f,%.3f,%.3f side=%.0f pin=%d%s gap=%.3f yaw=%.1f rel=%.1f fwd=%.0f cand=%d vz=%.2f xy=%.1f", n.x, n.y, n.z,
                 side, bestTicks, bestWide ? "w" : "", bestGap, bestYaw, NormalizeAngle(bestYaw - wallYaw), bestMove, cands, vel.z,
                 vel.Length2D());
    }
    // every wide find is logged once (not throttled): which heading / press pins this face is the data
    if (bestWide && bestTicks >= 1 && g_debugLogEnable)
        DebugLog("AS: wide pin=%d n=%.3f,%.3f rel=%.1f fwd=%.0f z=%.3f vz=%.2f xy=%.2f duck=%d", bestTicks, n2.x, n2.y,
                 NormalizeAngle(bestYaw - wallYaw), bestMove, origin.z, vel.z, vel.Length2D(), (live.buttons & IN_DUCK) ? 1 : 0);
    if (bestTicks >= 3) { ++s_as.pins; send(bestYaw, bestMove, true, bestWide ? "wide" : "lumi"); return; }
    if (resend()) return;
    // approach: the best unpinned / short-pin cmd still goes out, unarmed (botox :862)
    ++s_as.steers;
    send(bestYaw, bestMove, false, "steer");
}

// did AS write the cmd this tick (every later mover yields on s_airStuckHit)
static void AirStuckTick(void* pLocal, UserCmd_t* pCmd)
{
    const UserCmd_t pre = *pCmd;
    AirStuck(pLocal, pCmd);
    s_airStuckHit = s_asHolding || pCmd->forwardmove != pre.forwardmove || pCmd->sidemove != pre.sidemove
                 || pCmd->buttons != pre.buttons || pCmd->viewangles.x != pre.viewangles.x || pCmd->viewangles.y != pre.viewangles.y;
}

// last writer (Movement_AirStuckLastWriter, after the aimbot): the cmd AS proved this tick goes out as
// proven. "use movement fix" on = your final view stays (silent aim keeps its shot) and the proven world
// wish is turned into it, which can cost a pin its float bits; off = the proven view and move both go
// out verbatim
static void AS_FinalStamp(UserCmd_t* cmd)
{
    if (!cmd || s_asStamp.cmd != cmd->command_number) return;
    s_asStamp.cmd = -1;
    constexpr int mask = IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT | IN_DUCK | IN_JUMP;
    const bool viewSame = cmd->viewangles.x == s_asStamp.view.x && cmd->viewangles.y == s_asStamp.view.y;
    const bool same = viewSame && cmd->forwardmove == s_asStamp.fwd && cmd->sidemove == s_asStamp.side
                   && (cmd->buttons & mask) == (s_asStamp.buttons & mask);
    if (same) return;
    if (g_asMovementFix && !viewSame)
        as::StampRotate(cmd->viewangles.y, s_asStamp.view.y, s_asStamp.fwd, s_asStamp.side, cmd->forwardmove, cmd->sidemove);
    else
    {
        cmd->viewangles  = s_asStamp.view;
        cmd->forwardmove = s_asStamp.fwd;
        cmd->sidemove    = s_asStamp.side;
    }
    cmd->upmove  = 0.f;
    cmd->buttons = (cmd->buttons & ~mask) | (s_asStamp.buttons & mask);
    static int s_last = -1000;
    if (cmd->command_number - s_last > 66)
    {
        s_last = cmd->command_number;
        DebugLog("AS: stamp fixed a later writer (view %s)", viewSame ? "same" : g_asMovementFix ? "kept, move rotated" : "forced");
    }
}
