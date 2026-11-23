// ==== CORNER TIE (sv_wallbug 0): ride a wall seam past Valve's 1/32 push ========================
// Included by movement.cpp, last (rides texturebug.inl's HullPlaneDist world rule, airstuck.inl's
// AsSurfaceFriction, the shared sim pool). Math = as::Tie* (airstuck_pure.h, airstuck_test).
// Why + model proof: tests/push_tie_lab.cpp, docs/port/STATUS.md 11-23.
//
// The push: every pin tick clips the seam top, then the wall face; each clip leaves 1/32 u/s off its
// plane and the rest of the tick flies it. Feet lift 0.00047u, hull drifts 0.00047u off the wall, both
// every tick, and the pin dies once either passes 1/32 (~1 s at best).
// The tie: wall gap == feet height bit for bit, and the hull closes on the wall at the fall speed.
// Then top and face enter at the SAME fraction, the top wins, the hull lands back on (1/32, 1/32):
// the move itself takes the drift back, every tick.
//
// Two jobs, both proven by engine sims before a cmd goes out (no proof = cmd untouched):
//   STEER  the free-fall tick that ENDS in a seam's skin: one across wish so it also ends with
//          gap == height. Pin ticks lift both alike, so the catch decides it for the whole ride.
//   HOLD   a ride / catch tick where the cmd the other features left no longer pins: the tie press.
// UNKNOWN until flown: who wins an exact tie > 0 in the engine (recorded pins only show it at 0).
// The sims are the engine, so they answer it: debug.log `CT: hold` = it works, `CT: miss ... eq=1
// bin=1` = the engine says no.
// Limits: exact axis world-brush wall, integer brush top (pjs::SeamFace), across coordinate and feet
// z on one float grid (as::SameBinade), max speed >= the ride press (320 at 66 tick: Scout / Medic / Spy).
// ponytail: no final stamp -- a later view writer (aimbot) re-rotates the move; the press window is
//   +-0.0005 u/s, a rotation keeps it. Add AS_FinalStamp's twin if `CT: miss` follows aimbot ticks.

static constexpr float k_ctEps = 0.03125f;   // DIST_EPSILON
static constexpr int   k_ctSyncTicks = 33;   // AS_SyncArm writes a cvar + a log line per flip: no flicker along a wall

static struct
{
    int steerCmd = -1, holdCmd = -1;   // cmd numbers our steer / tie press went out on
    int syncCmd = -1000;               // last cmd beside a wall: the pred sync stays armed k_ctSyncTicks past it
    int run = 0;                       // tie presses in a row
    int steers = 0, holds = 0, misses = 0, sims = 0, sec = -1;
    int tries = 0, noCatch = 0;        // steers flown / of those the engine refused (no seat step, or no catch after it)
    int missLog = -1000;
    bool classTold = false;
} s_ct;

struct CtWall { int ax = -1; float s = 0.f, dist = 0.f; Vec3 n{}; };

// nearest exact axis world-brush face within 0.5u of the hull. A pinned or falling hull beside a wall:
// the sweep has no vertical travel, so a lip's top plane is never the hit (brush skipped), the face is
static bool CtFindWall(void* pLocal, const Vec3& org, const Vec3& mins, const Vec3& maxs, CtWall& w)
{
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    float best = 1.f;
    for (int i = 0; i < 4; ++i)
    {
        const Vec3 d(i == 0 ? 1.f : i == 1 ? -1.f : 0.f, i == 2 ? 1.f : i == 3 ? -1.f : 0.f, 0.f);
        GameTrace_t tr{};
        TraceHull(org, org + d * 0.5f, mins, maxs, MASK_PLAYERSOLID, &fil, &tr);
        if (!tr.DidHit() || tr.startsolid || tr.fraction >= best) continue;
        const Vec3& n = tr.plane.normal;
        if (n.z != 0.f || (fabsf(n.x) != 1.f && fabsf(n.y) != 1.f)) continue;
        // same test as HullPlaneDist's raw branch: only there is plane.dist the brush's own
        if ((tr.dispFlags & DISPSURF_SURFACE) || (tr.m_pEnt && tr.m_pEnt != GetClientEntity(0)) || tr.hitbox != 0) continue;
        best = tr.fraction;
        w.n = n; w.ax = fabsf(n.x) == 1.f ? 0 : 1; w.s = w.ax == 0 ? n.x : n.y; w.dist = tr.plane.dist;
    }
    return w.ax >= 0;
}

static void CornerTie(void* pLocal, UserCmd_t* pCmd)
{
    const bool on = g_psCornerTie
                 && (On(g_pixelSurf, g_pixelSurfKey, g_pixelSurfMode) || On(g_textureBug, g_textureBugKey, g_textureBugMode));
    {
        const int since = pCmd->command_number - s_ct.syncCmd;
        s_ctSyncWant = on && since >= 0 && since < k_ctSyncTicks;
    }
    if (g_debugLogEnable && g_globalVars)
    {
        const int sec = (int)g_globalVars->curtime;
        if (sec != s_ct.sec)
        {
            s_ct.sec = sec;
            if (s_ct.steers || s_ct.holds || s_ct.misses || s_ct.tries)
                DebugLog("CT: sec steers=%d/%d nocatch=%d holds=%d misses=%d sims=%d", s_ct.steers, s_ct.tries, s_ct.noCatch,
                         s_ct.holds, s_ct.misses, s_ct.sims);
            s_ct.steers = s_ct.holds = s_ct.misses = s_ct.sims = s_ct.tries = s_ct.noCatch = 0;
        }
    }
    // IN_JUMP in the air = the user leaves (Scout: air dash). AS / edge surf (cmd or latched approach) / wall climb /
    // pixel hop / head surf / auto bounce own their cmd, a ps assist arc is flown as proven.
    // No SimsLeft() gate: the tie tick is exactly the tick AA / TB / PS find no pin and search the pool + the cmd
    // clock dry. At most 5 sims a cmd here, counted (s_ct.sims), never the pool's
    if (!on || !EntAlive(pLocal) || EntMoveType(pLocal) != MOVETYPE_WALK || (s_prePredFlags & FL_ONGROUND)
        || CvarFloat("sv_wallbug", 0.f) >= 0.5f || Recorder_PlayState() == 0 || (pCmd->buttons & IN_JUMP)
        || s_airStuckHit || s_esurfActive || ESurf_Latched(pCmd) || s_wallClimbHit || s_pixelHopHit || s_headSurfHit
        || AutoBounceOwnsCmd() || s_psAssistArcOwns || s_psAssistHopActive)
        return;

    const float dt    = TickInterval();
    const float accel = CvarFloat("sv_airaccelerate", 10.f);
    const float grav  = CvarFloat("sv_gravity", 800.f);
    const float wmax  = g_offMaxspeed > 0 ? EntF(pLocal, g_offMaxspeed) : 450.f;
    const float pin   = TargetPredictZVel();
    // a ride tick starts 1/32 off the wall at the pin's 0.25 friction and must get the whole fall speed back
    const float need  = as::TieWish(grav * dt, accel, dt, 0.25f);
    if (wmax + 0.5f < need)
    {
        if (!s_ct.classTold) DebugLog("CT: off, max speed %.0f < ride press %.1f (Scout / Medic / Spy)", wmax, need);
        s_ct.classTold = true;
        return;
    }
    s_ct.classTold = false;

    const int nBase = PredCommandsPredicted();
    RestoreToFrame(nBase - 1);
    const Vec3 org = EntV3(pLocal, g_offOrigin), vel = EntV3(pLocal, g_offVelocity);
    const Vec3 mins = EntV3(pLocal, g_offMins), maxs = EntV3(pLocal, g_offMaxs);
    CtWall w;
    if (!CtFindWall(pLocal, org, mins, maxs, w)) return;
    s_ctSyncWant = true;   // pins are exact floats: AS_Sync re-predicts off the server snapshot (read next cmd)
    s_ct.syncCmd = pCmd->command_number;
    {   // your own keys (before any feature) pressing AWAY from the wall = you are leaving: never hold you on it
        const float yr = DEG2RADF(pCmd->viewangles.y), cy = cosf(yr), sy = sinf(yr);
        const float rx = cy * s_rawFwd + sy * s_rawSide, ry = sy * s_rawFwd - cy * s_rawSide;   // forward (cy, sy), right (sy, -cy)
        const float rl = sqrtf(rx * rx + ry * ry);
        if (rl > 1.f && (rx * w.n.x + ry * w.n.y) / rl > 0.5f) return;
    }

    // hull centre + half extents, the engine's box (Ray_t::Init): the distances below are its floats
    const float extA = (w.ax == 0 ? maxs.x - mins.x : maxs.y - mins.y) * 0.5f;
    const float offA = (w.ax == 0 ? mins.x + maxs.x : mins.y + maxs.y) * 0.5f;
    const float extZ = (maxs.z - mins.z) * 0.5f, offZ = (mins.z + maxs.z) * 0.5f;
    const auto axis  = [&](const Vec3& v) { return w.ax == 0 ? v.x : v.y; };
    const auto gapOf = [&](const Vec3& o) { return as::TieD(axis(o) + offA, w.s, w.dist, extA); };
    const auto hOf   = [&](const Vec3& o, float top) { return as::TieD(o.z + offZ, 1.f, top, extZ); };
    const auto air   = [&]() { return !(EntI(pLocal, g_offFlags) & FL_ONGROUND); };
    const auto pinnedNow = [&]() {
        const Vec3 v = EntV3(pLocal, g_offVelocity);
        return air() && as::SurfPinned(v.z, v.Length2D(), pin);
    };
    // feet in this top's skin (a tie tick ends 1/32 + one lift over the brush, a deep catch lower)
    const auto inSkin = [&](float top) {
        const float h = hOf(EntV3(pLocal, g_offOrigin), top);
        return h > 0.f && h < k_ctEps + 0.002f;
    };
    // our own press must leave a RIDE: pinned and still sliding
    const auto onSeam = [&](float top) { return pinnedNow() && inSkin(top); };
    // the cmd the others left: vz alone. A brake / park that ends pinned at xy < 1 is their stop, not a lost pin
    const auto heldNow = [&](float top) {
        return air() && fabsf(EntV3(pLocal, g_offVelocity).z - pin) < as::k_pinTol && inSkin(top);
    };
    // command_number restarts on a new connection: a stale stamp must not mute the log
    const auto missDue = [&]() { const int d = pCmd->command_number - s_ct.missLog; return d > 8 || d < 0; };
    // the tie press off the entity's state right now: across velocity -> the fall speed, straight into the face
    const auto press = [&](UserCmd_t& c, float fric) -> float {
        const Vec3 v = EntV3(pLocal, g_offVelocity);
        const float m = (std::min)(as::TieWish(axis(v) * w.s - as::TieFallVz(v.z, grav, dt), accel, dt, fric), wmax);
        MV_WriteWishWorld(&c, Vec3(-w.n.x, -w.n.y, 0.f), m);
        return m;
    };
    const auto take = [&](const UserCmd_t& c) {
        pCmd->forwardmove = c.forwardmove;
        pCmd->sidemove    = c.sidemove;
        tbx::s_tb.reset_command();   // TB_PreserveMove (after Pred_End) would lay its own move back on
    };

    const float gap0 = gapOf(org);
    float top = 0.f;
    const bool seam0   = pjs::SeamFace(org.z, top);
    const bool pinned0 = as::SurfPinned(vel.z, vel.Length2D(), pin);
    const bool mine    = s_ct.steerCmd == pCmd->command_number - 1 || s_ct.holdCmd == pCmd->command_number - 1;

    // ---- HOLD: a ride tick, or the catch tick right after our steer ------------------------------
    if (seam0 && (pinned0 || mine) && gap0 > 0.f && gap0 <= k_ctEps + 0.001f)
    {
        const float h0 = hOf(org, top);
        Pred_Simulate(pLocal, pCmd); ++s_ct.sims;   // the cmd as the other features left it
        const bool ok0 = heldNow(top);
        RestoreToFrame(nBase - 1);
        if (ok0) return;   // still rides: every pin tick lifts gap and height alike, the tie keeps

        UserCmd_t c = *pCmd;
        float m = 0.f, fr = AsSurfaceFriction(pLocal);
        bool ok = false;
        // the START's own friction first (0.25 on a ride, 1 on the catch), then the other one
        for (int t = 0; t < 2 && !ok; ++t)
        {
            if (t) fr = fr < 0.5f ? 1.f : 0.25f;
            RestoreToFrame(nBase - 1);
            m = press(c, fr);
            Pred_Simulate(pLocal, &c); ++s_ct.sims;
            ok = onSeam(top);
        }
        const Vec3 e = EntV3(pLocal, g_offOrigin), ev = EntV3(pLocal, g_offVelocity);
        RestoreToFrame(nBase - 1);
        const int eq = gap0 == h0 ? 1 : 0, bin = as::SameBinade(axis(org) + offA, org.z + offZ) ? 1 : 0;
        if (ok)
        {
            take(c);
            s_ct.run = s_ct.holdCmd == pCmd->command_number - 1 ? s_ct.run + 1 : 1;
            s_ct.holdCmd = pCmd->command_number;
            ++s_ct.holds;
            if (g_debugLogEnable && (s_ct.run == 1 || s_ct.run % 66 == 0))
                DebugLog("CT: hold run=%d wish=%.4f fric=%.2f gap=%.7f h=%.7f eq=%d bin=%d xy=%.1f pinned=%d -> gap=%.7f h=%.7f",
                         s_ct.run, m, fr, gap0, h0, eq, bin, vel.Length2D(), pinned0 ? 1 : 0, gapOf(e), hOf(e, top));
            return;
        }
        ++s_ct.misses;
        if (g_debugLogEnable && missDue())
        {
            s_ct.missLog = pCmd->command_number;
            DebugLog("CT: miss run=%d gap=%.7f h=%.7f eq=%d bin=%d wish=%.4f/%.0f fric=%.2f vz=%.4f xy=%.1f pinned=%d mine=%d "
                     "ax=%d wall=%.3f top=%.0f -> z=%.5f vz=%.3f",
                     s_ct.run, gap0, h0, eq, bin, m, wmax, fr, vel.z, vel.Length2D(), pinned0 ? 1 : 0, mine ? 1 : 0,
                     w.ax, w.dist * w.s, top, e.z, ev.z);
        }
        s_ct.run = 0;
        return;
    }

    // ---- STEER: this free-fall tick ends in a seam's skin -> make it end with gap == height ------
    // A TB hug on this tick yields to it: the steer goes out only with a proven catch behind it, the hug has none
    // yet (a TB pin this tick ends pinned = not a free tick = no steer below)
    if (pinned0 || vel.z >= 0.f || gap0 <= 0.f) return;
    {
        float t0 = 0.f;   // cheap gate before any sim: ~3% of falling ticks pass
        if (!pjs::SeamFace(org.z + as::TieFallVz(vel.z, grav, dt) * dt, t0)) return;
    }
    Pred_Simulate(pLocal, pCmd); ++s_ct.sims;   // END z as the engine flies it (the wish cannot move z)
    const Vec3 eA = EntV3(pLocal, g_offOrigin);
    const bool freeA = air() && !pinnedNow() && pjs::SeamFace(eA.z, top);
    const float hE = freeA ? hOf(eA, top) : 0.f;
    RestoreToFrame(nBase - 1);
    if (!freeA || !(hE > 0.f && hE <= k_ctEps)) return;

    const float a0 = axis(org);
    float cSeat = 0.f, vAx = 0.f;
    if (!as::TieSeat(hE, w.s, w.dist, extA, cSeat) || !as::TieStepVel(a0, cSeat - offA, dt, vAx))
    {
        ++s_ct.misses;
        if (g_debugLogEnable && missDue())
        {
            s_ct.missLog = pCmd->command_number;
            DebugLog("CT: no seat h=%.7f bin=%d ax=%d wall=%.3f top=%.0f (wall coordinate and height on different float grids)",
                     hE, as::SameBinade(a0 + offA, eA.z + offZ) ? 1 : 0, w.ax, w.dist * w.s, top);
        }
        return;
    }
    const float aSeat = cSeat - offA;
    UserCmd_t c1 = *pCmd;
    float m1 = 0.f;
    bool ok = false;
    ++s_ct.tries;
    for (int t = 0; t < 3 && !ok; ++t)
    {
        RestoreToFrame(nBase - 1);
        const float dv = vAx - axis(vel);   // across velocity change, + = along the axis
        m1 = (std::min)(as::TieWish(fabsf(dv), accel, dt, AsSurfaceFriction(pLocal)), wmax);
        MV_WriteWishWorld(&c1, w.ax == 0 ? Vec3(dv > 0.f ? 1.f : -1.f, 0.f, 0.f) : Vec3(0.f, dv > 0.f ? 1.f : -1.f, 0.f), m1);
        Pred_Simulate(pLocal, &c1); ++s_ct.sims;
        const Vec3 e1 = EntV3(pLocal, g_offOrigin);
        ok = air() && !pinnedNow() && gapOf(e1) == hE && hOf(e1, top) == hE;
        if (!ok) vAx += (aSeat - axis(e1)) / dt;   // landed a float off: aim the difference
    }
    if (ok)
    {   // ...and the next tick must catch from there: chained, the friction is this END's
        UserCmd_t c2 = *pCmd;
        press(c2, AsSurfaceFriction(pLocal));
        Pred_Simulate(pLocal, &c2); ++s_ct.sims;
        ok = onSeam(top);
    }
    RestoreToFrame(nBase - 1);
    if (!ok) { ++s_ct.noCatch; return; }   // no lip there, or the wish cannot make the step: the cmd stays as it was
    take(c1);
    s_ct.steerCmd = pCmd->command_number;
    ++s_ct.steers;
    if (g_debugLogEnable)
        DebugLog("CT: steer ax=%d wall=%.3f top=%.0f h=%.7f gap0=%.5f wish=%.4f xy=%.1f vz=%.1f",
                 w.ax, w.dist * w.s, top, hE, gap0, m1, vel.Length2D(), vel.z);
}
