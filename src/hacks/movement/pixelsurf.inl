// pixelsurf.inl -- botox csgo-sdk hacks/movement/movement.cpp pixel_surf( ) + ps_cmd_start( ) +
// ps_wall_ahead / ps_wall_touch + the auto-align-off wall probe (movement.cpp:456-475), 1:1.
// Textually included by movement.cpp (same TU: Pred_Simulate / RestoreToFrame / TraceHull /
// s_prePred* / s_airStuckHit / s_textureBugHit / s_tbOwnsCmd / s_wallClimbHit are its statics).
//
// botox -> tf2 shims:
//   g_prediction.backup_data.m_origin/m_velocity/m_flags -> s_prePredOrigin / s_prePredVel / s_prePredFlags
//   g_ctx.m_input_buttons                                 -> s_rawButtons (before any feature)
//   g_ctx.old_view_point                                  -> cmd->viewangles (movement runs before aim here)
//   n_tick::ticks / cmd_clock_left                        -> as::TicksScaled / TbBudgetExpired
//   g_air_stuck_holding || owns_cmd                       -> s_airStuckHit (AS runs before this now, as botox)
//   g_texturebug.m_hit / m_acted                          -> s_textureBugHit / s_tbOwnsCmd
//   g_wall_climb.active / caught                          -> s_wallClimbHit
//   m_auto_bounce_data.m_owns_cmd / fireman               -> none: tf2's AutoBounce has no owns flag, TF2 has no ladders
//   sv_air_max_wishspeed                                  -> CvarFloat(.., 30) (TF2 has no such cvar: AirAccelerate caps at 30)
// Readers elsewhere keep their names: s_shouldPixelSurf (latched either way), s_psShouldDuck /
// s_psAirDuckHeld (the duck latch), s_pixelSurfTicks (the ride timer), s_psRidingNow, s_psEv.

#include "airstuck_pure.h"

// TRACE_EVERYTHING_FILTER_PROPS with no entity ever accepted = world brushes only (botox
// c_trace_filter_world / c_trace_filter_trace_type_everything_filter_props). Shared with airstuck.inl
struct TraceFilterWorldOnly
{
    void** vfptr;
    static bool __fastcall sShouldHit(TraceFilterWorldOnly*, void*, int) { return false; }
    static int  __fastcall sTraceType(TraceFilterWorldOnly*) { return 3; }
    void Setup()
    {
        static void* vt[2] = { (void*)&sShouldHit, (void*)&sTraceType };
        vfptr = vt;
    }
};

static bool s_psShouldSurf   = false;   // data.should_pixel_surf  ( duck latched )
static bool s_psShouldUnduck = false;   // data.should_unduck      ( stand latched )
static bool s_psBail         = false;   // s_ps_bail
static int  s_psTicks        = 0;       // `ticks`: latch + flip + 16 ( scaled )
static int  s_psUnpinned     = 0;
static int  s_psLastBy       = 0;
static bool s_psParkHold     = false;   // this cmd starts parked: pinned, xy < 1, park on ( PixelSurf sets it )

/* botox ps_cmd_start: your own duck key flipped on a live ride = you bailed, PS lets go until the ground */
static void PsCmdStart(void* pLocal)
{
    static bool prev_key = false;
    const bool ground = (s_prePredFlags & FL_ONGROUND) != 0;
    const bool key    = (s_rawButtons & IN_DUCK) != 0;
    const bool ducked = (s_prePredFlags & FL_DUCKING) != 0;
    const bool riding = !ground && fabsf(s_prePredVel.z - TargetPredictZVel()) < 0.01f;
    if (ground || EntMoveType(pLocal) != MOVETYPE_WALK)
        s_psBail = false;
    else if (key != prev_key && riding && key != ducked && !s_psBail) {
        s_psBail = true;
        s_psShouldSurf = s_psShouldUnduck = false;
        DebugLog("PSU: bail key=%s on %s surf z=%.3f", key ? "duck" : "stand", ducked ? "duck" : "stand", s_prePredOrigin.z);
    }
    prev_key = key;
}

static bool PsWallAhead(void* pLocal, const int n, Vec3* normal = nullptr)
{
    const float dt   = TickInterval() * static_cast<float>(n);
    const Vec3 vel   = s_prePredVel;
    const Vec3 from  = s_prePredOrigin;
    const Vec3 to(from.x + vel.x * dt, from.y + vel.y * dt, from.z);
    const Vec3 grow(1.f, 1.f, 0.f);
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    GameTrace_t tr{};
    TraceHull(from, to, EntV3(pLocal, g_offMins) - grow, EntV3(pLocal, g_offMaxs) + grow, MASK_PLAYERSOLID, &fil, &tr);
    const bool hit = tr.DidHit() && fabsf(tr.plane.normal.z) < 0.7f;
    if (hit && normal)
        *normal = tr.plane.normal;
    return hit;
}

static bool PsWallTouch(void* pLocal, Vec3& normal, const Vec3& from)
{
    const Vec3 mins = EntV3(pLocal, g_offMins), maxs = EntV3(pLocal, g_offMaxs);
    const float dx[4] = { 0.05f, -0.05f, 0.f, 0.f }, dy[4] = { 0.f, 0.f, 0.05f, -0.05f };
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    float best = 1.f;
    for (int k = 0; k < 4; k++) {
        GameTrace_t tr{};
        TraceHull(from, Vec3(from.x + dx[k], from.y + dy[k], from.z), mins, maxs, MASK_PLAYERSOLID, &fil, &tr);
        if (tr.DidHit() && !tr.startsolid && tr.plane.normal.z == 0.f && tr.fraction < best) {
            best   = tr.fraction;
            normal = tr.plane.normal;
        }
    }
    return best < 1.f;
}
static bool PsWallTouch(void* pLocal, Vec3& normal) { return PsWallTouch(pLocal, normal, EntV3(pLocal, g_offOrigin)); }

/* botox movement.cpp:456-475: auto align off = pixel surf finds its own wall ( 16 x 1u hull rays ) */
static void PsWallDetectFallback(void* pLocal)
{
    s_aaWallDetected = false;
    if (!g_pixelSurf || (s_prePredFlags & FL_ONGROUND) || (EntI(pLocal, g_offFlags) & FL_ONGROUND))
        return;
    const Vec3 start = EntV3(pLocal, g_offOrigin);
    const Vec3 mins = EntV3(pLocal, g_offMins), maxs = EntV3(pLocal, g_offMaxs);
    TraceFilterWorldOnly fil; fil.Setup();
    GameTrace_t tr{};
    TraceHull(start, start, mins - Vec3(1.f, 1.f, 0.f), maxs + Vec3(1.f, 1.f, 0.f), MASK_PLAYERSOLID, &fil, &tr);
    for (int k = 0; k < 16 && tr.DidHit() && !s_aaWallDetected; k++) {
        const float a = static_cast<float>(k) * (as::k_pi * 2.f / 16.f);
        GameTrace_t hit{};
        TraceHull(start, Vec3(start.x + cosf(a), start.y + sinf(a), start.z), mins, maxs, MASK_PLAYERSOLID, &fil, &hit);
        s_aaWallDetected = hit.fraction != 1.f && hit.plane.normal.z == 0.f;
    }
}

static void PixelSurfImpl(void* pLocal, UserCmd_t* cmd)
{
    if (!On(g_pixelSurf, g_pixelSurfKey, g_pixelSurfMode)) {
        s_psShouldSurf = s_psShouldUnduck = false;
        return;
    }

    if (!EntAlive(pLocal)) {
        s_psTicks = 0;
        return;
    }
    if (EntMoveType(pLocal) == MOVETYPE_NOCLIP) {
        s_psShouldSurf = s_psShouldUnduck = false;
        return;
    }
    const int frame = PredCommandsPredicted() - 1;
    RestoreToFrame(frame);   // botox reads START here: its pre-stages all end on the restored frame
    if ((EntI(pLocal, g_offFlags) & FL_ONGROUND) || s_psBail)
        return;

    const bool latched = s_psShouldSurf || s_psShouldUnduck;
    const bool as_owns = s_airStuckHit;
    const bool tb_owns = s_textureBugHit || s_tbOwnsCmd;
    if (as_owns || tb_owns || s_wallClimbHit) {
        if (latched)
            DebugLog("PSU: yield by=%s", as_owns ? "as" : tb_owns ? "tb" : "wc");
        s_psShouldSurf = s_psShouldUnduck = false;
        return;
    }
    /* parked ( PsPark below owns the move ): keep the stance the ride had. botox's stuck release and
       hold() would unduck / push a hang that IS the goal here */
    if (s_psParkHold) {
        if (latched)
            cmd->buttons = s_psShouldSurf ? cmd->buttons | IN_DUCK : cmd->buttons & ~IN_DUCK;
        return;
    }

    const float ipt = TickInterval();
    if (!latched && !s_aaWallDetected && !PsWallAhead(pLocal, as::TicksScaled(8, ipt)))
        return;
    if (!latched && TbBudgetExpired())
        return;

    const float pin   = TargetPredictZVel();
    /* xy < 1 = wall hang ( stuck ), not a surf. 10-07 21:28 log: all 3 rides spd 0, one hung 53 ticks */
    const auto pinned = [pin](const Vec3& v) { return as::SurfPinned(v.z, v.Length2D(), pin); };

    const auto hunt = [&](const int buttons, const int n, const int delay = 0) {
        const int live = cmd->buttons;
        RestoreToFrame(frame);
        const int base = EntI(pLocal, g_offTickBase);
        int found = -1, run = 0;
        for (int z = 0; z <= n && found < 0; z++) {
            cmd->buttons = z < delay ? live : buttons;
            EntSetI(pLocal, g_offTickBase, base + z);
            Pred_Simulate(pLocal, cmd);
            if (EntI(pLocal, g_offFlags) & FL_ONGROUND)
                break;
            run = pinned(EntV3(pLocal, g_offVelocity)) ? run + 1 : 0;
            if (run == 2)
                found = z - 1;
        }
        cmd->buttons = live;
        RestoreToFrame(frame);
        return found;
    };

    const auto keeps_pin = [&]() {
        RestoreToFrame(frame);
        const int base = EntI(pLocal, g_offTickBase);
        bool ok        = true;
        for (int z = 0; z < 2 && ok; z++) {
            EntSetI(pLocal, g_offTickBase, base + z);
            Pred_Simulate(pLocal, cmd);
            ok = !(EntI(pLocal, g_offFlags) & FL_ONGROUND) && pinned(EntV3(pLocal, g_offVelocity));
        }
        RestoreToFrame(frame);
        return ok;
    };

    const bool keys_free = true;   /* botox: !auto_bounce.owns_cmd && !fireman.owns_cmd -- neither exists here */
    const auto hold      = [&]() -> int {
        RestoreToFrame(frame);
        Vec3 n{};
        if (!keys_free || !PsWallTouch(pLocal, n))
            return 0;
        const float fwd = cmd->forwardmove, side = cmd->sidemove;
        const float y   = DEG2RADF(cmd->viewangles.y);
        const Vec3 wish(fwd * cosf(y) + side * sinf(y), fwd * sinf(y) - side * cosf(y), 0.f);
        const float len = wish.Length2D();
        constexpr float k_leave_dot = 0.5f;
        if (len > 0.01f && (wish.x * n.x + wish.y * n.y) / len > k_leave_dot)
            return 0;
        /* strafing into the face = 450 at the best along-wall strafe yaw ( wall eats the into part ), only if it keeps the pin.
           g_psWallStrafe: 1 = botox ( your keys into the face ), 2 = every ride tick, keys or not ( leaving still wins, above ) */
        constexpr float k_gain_dot = 0.25f;
        const bool keys_in = len > 0.01f && -(wish.x * n.x + wish.y * n.y) / len > k_gain_dot;
        if (g_psWallStrafe == 2 || (g_psWallStrafe == 1 && keys_in)) {
            float gy = 0.f;
            if (as::WallStrafeYaw(n.x, n.y, s_prePredVel.x, s_prePredVel.y, CvarFloat("sv_air_max_wishspeed", 30.f), gy)) {
                as::MoveTowardYaw(cmd->viewangles.y, gy, 450.f, cmd->forwardmove, cmd->sidemove);
                if (keeps_pin())
                    return 4;
                cmd->forwardmove = fwd;
                cmd->sidemove    = side;
            }
        }
        if (keeps_pin())
            return 1;
        const float yaws[2]    = { RAD2DEGF(atan2f(wish.y, wish.x)), RAD2DEGF(atan2f(-n.y, -n.x)) };
        const float presses[2] = { 45.f, 450.f };
        for (int i = 0; i < 2; i++) {
            if (i == 0 && len <= 0.01f)
                continue;
            for (const float f : presses) {
                as::MoveTowardYaw(cmd->viewangles.y, yaws[i], f, cmd->forwardmove, cmd->sidemove);
                if (!keeps_pin())
                    continue;
                return 2 + i;
            }
        }
        cmd->forwardmove = fwd;
        cmd->sidemove    = side;
        return 0;
    };
    const auto log_hold = [&](const int by) {
        if (by != s_psLastBy)
            DebugLog("PSU: hold by=%s fwd=%.0f side=%.0f vz=%.2f", by == 0 ? "none" : by == 1 ? "keys" : by == 2 ? "wish" : by == 3 ? "wall" : "gain",
                     cmd->forwardmove, cmd->sidemove, s_prePredVel.z);
        s_psLastBy = by;
    };

    if (!latched) {
        const bool holds_duck = (cmd->buttons & IN_DUCK) != 0;
        const int held        = hold();
        log_hold(held);
        if (held)
            return;
        if (hunt(cmd->buttons, as::TicksScaled(8, ipt)) >= 0)
            return;
        const int flip_buttons = holds_duck ? cmd->buttons & ~IN_DUCK : cmd->buttons | IN_DUCK;
        const int live_buttons = cmd->buttons;
        int flip               = hunt(flip_buttons, as::TicksScaled(8, ipt));
        /* your crouch is dropped only on the last tick that still catches: one more crouched tick still pins = wait,
           so a guess 8 ticks out never uncrouches you ( path / keys change before the lip = nothing lost ) */
        if (holds_duck && flip >= 0 && hunt(flip_buttons, as::TicksScaled(8, ipt), 1) >= 0)
            return;
        if (flip < 0) {
            cmd->buttons = flip_buttons;
            const int by = hold();
            if (by < 2) {
                cmd->buttons = live_buttons;
                return;
            }
            log_hold(by);
            flip = 0;
        }
        (holds_duck ? s_psShouldUnduck : s_psShouldSurf) = true;
        s_psTicks    = cmd->tick_count + flip + as::TicksScaled(16, ipt);
        s_psUnpinned = 0;
        cmd->buttons = flip_buttons;
        DebugLog("PSU: latch %s in=%d z=%.3f vz=%.2f spd=%.1f", holds_duck ? "stand" : "duck", flip, s_prePredOrigin.z,
                 s_prePredVel.z, s_prePredVel.Length2D());
        return;
    }

    const int own_buttons = cmd->buttons;
    cmd->buttons          = s_psShouldSurf ? cmd->buttons | IN_DUCK : cmd->buttons & ~IN_DUCK;
    const int held        = hold();
    log_hold(held);
    const Vec3& v0   = s_prePredVel;
    const bool stuck = !held && fabsf(v0.z - pin) < 0.01f && v0.Length2D() < 1.f;
    if (stuck)
        cmd->buttons = own_buttons;
    else {
        if (cmd->tick_count <= s_psTicks)
            return;
        s_psUnpinned = pinned(v0) ? 0 : s_psUnpinned + 1;
        if (s_psUnpinned < 2)
            return;
        if (hunt(cmd->buttons, as::TicksScaled(4, ipt)) >= 0) {
            s_psUnpinned = 0;
            return;
        }
    }
    DebugLog("PSU: release %s vz=%.2f stuck=%d", s_psShouldSurf ? "duck" : "stand", v0.z, stuck ? 1 : 0);
    s_psShouldSurf = s_psShouldUnduck = false;
}

// the ride detect every other reader keys on (PS assist, PsSurfFixTick, route calc, the indicator).
// Runs at the top of the probe block, before any mover, so every feature in this cmd reads this
// cmd's ride and not last tick's
static Vec3 s_psPinStart{};
static bool s_psPinAnchored = false;
static void PsRideDetect()
{
    const float flHalfGrav = -TargetPredictZVel();
    constexpr float flSurfTol = 1.5f;
    const bool bSurfVel = fabsf(s_prePredVel.z + flHalfGrav) < flSurfTol
        && (!(s_prePredFlags & FL_ONGROUND) || s_shouldPixelSurf);
    s_psRidingNow = bSurfVel && s_prevSurfVel;
    s_prevSurfVel = bSurfVel;
    // where this pin began -- the indicator's surf / wedge split (ps_surf.h)
    if (!s_psRidingNow) s_psPinAnchored = false;
    else if (!s_psPinAnchored) { s_psPinStart = s_prePredOrigin; s_psPinAnchored = true; }
    s_psEv = ps::SurfEvidence{};
}

// ---- PARK AT THE END OF THE SURF ---------------------------------------------------------
// A pixel surf seam is a solver artifact, so where it ends cannot be traced (psassist.inl, the
// falsified flat-face prune). It is SIMMED: every ride cmd flies the cmd as it stands; if the pin
// runs out inside the stopping window, it asks whether braking from the NEXT cmd still stops on
// the pin. Yes = ride one more tick at full speed (and gain). No = brake now, straight against xy
// (as::BrakeWish: exact stop once it fits in a tick, so it never reverses). Stopped (xy < 1, still
// pinned) = hold the hang bare, or with PixelFinder's 10 u/s press into the face if bare drops.
// Keys pointing off the wall (botox k_leave_dot) let go. Returns 0 none / 1 brake / 2 hold
static int PsPark(void* pLocal, UserCmd_t* cmd, int& end_out)
{
    end_out = -1;
    if (!g_psParkAtEnd || !On(g_pixelSurf, g_pixelSurfKey, g_pixelSurfMode) || s_psBail)
        return 0;
    if (!EntAlive(pLocal) || EntMoveType(pLocal) != MOVETYPE_WALK || (s_prePredFlags & FL_ONGROUND))
        return 0;
    if (s_airStuckHit || s_textureBugHit || s_tbOwnsCmd || s_wallClimbHit)
        return 0;
    const float pin = TargetPredictZVel();
    const Vec3  v0  = s_prePredVel;
    const float xy0 = v0.Length2D();
    if (fabsf(v0.z - pin) >= 0.01f)
        return 0;   // not on a pin this cmd
    const int frame = PredCommandsPredicted() - 1;
    RestoreToFrame(frame);
    Vec3 n{};
    if (!PsWallTouch(pLocal, n))
        return 0;

    const float fwd = cmd->forwardmove, side = cmd->sidemove;
    {
        const float y = DEG2RADF(cmd->viewangles.y);
        const Vec3 wish(fwd * cosf(y) + side * sinf(y), fwd * sinf(y) - side * cosf(y), 0.f);
        const float len = wish.Length2D();
        if (len > 0.01f && (wish.x * n.x + wish.y * n.y) / len > 0.5f)
            return 0;
    }

    const float ipt = TickInterval();
    const float aa  = CvarFloat("sv_airaccelerate", 10.f);
    const auto on_pin = [&]() {
        return !(EntI(pLocal, g_offFlags) & FL_ONGROUND) && fabsf(EntV3(pLocal, g_offVelocity).z - pin) < 0.01f;
    };
    const auto brake_into = [&](const Vec3& v) {
        MV_WriteWishWorld(cmd, Vec3(-v.x, -v.y, 0.f), as::BrakeWish(v.Length2D(), aa, ipt));
    };

    if (xy0 < 1.f) {
        for (int k = 0; k < 2; k++) {
            if (k == 0) cmd->forwardmove = cmd->sidemove = 0.f;
            else        MV_WriteWishWorld(cmd, Vec3(-n.x, -n.y, 0.f), 10.f);
            RestoreToFrame(frame);
            const int base = EntI(pLocal, g_offTickBase);
            bool ok = true;
            for (int z = 0; z < 2 && ok; z++) {
                EntSetI(pLocal, g_offTickBase, base + z);
                Pred_Simulate(pLocal, cmd);
                ok = on_pin();
            }
            RestoreToFrame(frame);
            if (ok)
                return 2;
        }
        cmd->forwardmove = fwd;
        cmd->sidemove    = side;
        return 0;
    }

    /* n ticks: `live` on the cmd as it stands, then braking. Returns the tick the pin broke ( -1 = held ),
       stopped = xy fell under 1 with the pin still on */
    const auto fly = [&](const int ticks, const int live, bool& stopped) -> int {
        stopped = false;
        RestoreToFrame(frame);
        const int base = EntI(pLocal, g_offTickBase);
        int broke = -1;
        for (int z = 0; z < ticks; z++) {
            if (z >= live) brake_into(EntV3(pLocal, g_offVelocity));
            else { cmd->forwardmove = fwd; cmd->sidemove = side; }
            EntSetI(pLocal, g_offTickBase, base + z);
            Pred_Simulate(pLocal, cmd);
            if (!on_pin()) { broke = z; break; }
            if (z >= live && EntV3(pLocal, g_offVelocity).Length2D() < 1.f) { stopped = true; break; }
        }
        cmd->forwardmove = fwd;
        cmd->sidemove    = side;
        RestoreToFrame(frame);
        return broke;
    };

    // worst-case stop: the wish is cropped to class maxspeed, 240 is TF2's slowest air class
    const float decel = std::max(aa * 240.f * ipt, 1e-3f);
    const int   ticks = std::min(static_cast<int>(ceilf(xy0 / decel)) + as::TicksScaled(4, ipt), 64);
    if (SimsLeft() < 2 * ticks + 4 || TbBudgetExpired())
        return 0;
    bool stopped = false;
    end_out = fly(ticks, ticks, stopped);
    if (end_out < 0)
        return 0;   // the ride outlasts the stopping window
    if (fly(ticks + 1, 1, stopped) < 0 && stopped)
        return 0;   // braking from next cmd still stops on it: keep the speed one more tick
    brake_into(v0);
    return 1;
}

// botox's pixel_surf, then the flags published under their old names
static void PixelSurf(void* pLocal, UserCmd_t* pCmd)
{
    if (!g_autoAlign) PsWallDetectFallback(pLocal);

    s_psParkHold = g_psParkAtEnd && !(s_prePredFlags & FL_ONGROUND)
                && fabsf(s_prePredVel.z - TargetPredictZVel()) < 0.01f && s_prePredVel.Length2D() < 1.f;
    PixelSurfImpl(pLocal, pCmd);
    {
        static int s_lastAct = 0;
        int end = -1;
        const int act = PsPark(pLocal, pCmd, end);
        if (act != s_lastAct)
            DebugLog("PSU: park %s spd=%.1f end=%d z=%.3f", act == 1 ? "brake" : act == 2 ? "hold" : "off",
                     s_prePredVel.Length2D(), end, s_prePredOrigin.z);
        s_lastAct = act;
    }

    s_shouldPixelSurf = s_psShouldSurf || s_psShouldUnduck;
    s_psShouldDuck    = s_psShouldSurf;
    s_psAirDuckHeld   = s_psShouldSurf;
    s_pixelSurfTicks  = s_psTicks;

    if (s_psRidingNow)
    {
        RestoreToFrame(PredCommandsPredicted() - 1);
        Vec3 n{};
        const bool touch = PsWallTouch(pLocal, n);
        s_psEv.riding = true;
        s_psEv.wall   = touch;
        s_psEv.gap    = touch ? 0.f : 999.f;
        s_psEv.travel = Vec3(s_prePredOrigin.x - s_psPinStart.x, s_prePredOrigin.y - s_psPinStart.y, 0.f).Length2D();
    }
}
