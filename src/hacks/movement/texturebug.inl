// texturebug.inl -- textually included by movement.cpp, not its own TU.
// Two TB styles behind g_tbStyle: 0 cumidere (TextureBugCumidere + HeadSurf + HeadBounce),
// 1 botox (namespace tbx, botox csgo texturebug.cpp 1:1). TextureBug() at the bottom of the
// tbx block picks one. Rides movement.cpp's file-static helpers (Pred_Simulate, TraceHull/
// TraceLine, s_textureBugHit/s_tbOwnsCmd ...).

// ==== TB pin tests + ceiling detect ====

// negative half-gravity per tick (TB naming kept for the 1:1 ports)
static float TB_HalfGravityPerTick() { return TargetPredictZVel(); }
// |vz + flHalf| < tol with flHalf = -6 tests vz == +6. +halfgrav is the RIDE accept
// (head_surf, flat brush bottom); head_bounce wants -halfgrav but is not ported.
static bool TB_IsPositiveHalfGravity(float vz, float flHalf, float tol) { return fabsf(vz + flHalf) < tol; }

// xy inset for the flush-face retry below . Used by BOTH overhead detects
static constexpr float k_tbOverInset = 2.f;

// ceiling brush within flReach above the head.
// A hull already flush against a VERTICAL face (what a TB ride holds it against) reports
// frac 0 nz 0 whichever way the sweep points -- blind. Retry inset in xy so that face is
// off contact. Detect is a GATE: under-detecting kills the feature, so inset is the safe side
static bool TB_DetectCeiling(void* pLocal, const Vec3& vPos, const Vec3& vMins, const Vec3& vMaxs,
                             float flReach, GameTrace_t& traceOut)
{
    Vec3 vEnd = vPos;
    vEnd.z += flReach;
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    TraceHull(vPos, vEnd, vMins, vMaxs, MASK_PLAYERSOLID, &filter, &traceOut);
    if (traceOut.DidHit() && traceOut.plane.normal.z < -0.7f)
        return true;
    // nothing touched at all -> genuinely clear overhead, no retry (and no second trace)
    if (traceOut.fraction >= 1.f && !traceOut.startsolid && !traceOut.allsolid)
        return false;
    Vec3 vInMins = vMins, vInMaxs = vMaxs;
    vInMins.x += k_tbOverInset; vInMins.y += k_tbOverInset;
    vInMaxs.x -= k_tbOverInset; vInMaxs.y -= k_tbOverInset;
    if (vInMaxs.x <= vInMins.x || vInMaxs.y <= vInMins.y)
        return false;
    TraceHull(vPos, vEnd, vInMins, vInMaxs, MASK_PLAYERSOLID, &filter, &traceOut);
    return traceOut.DidHit() && traceOut.plane.normal.z < -0.7f;
}

// HeadBounce's own detect (coldtires texturebug_common.h:594 tb_detect_overhead). NOT the
// ceiling test: a crease between two displacements has no flat face, so the -0.7 ride filter
// rejects exactly the geometry a bounce lives on. -0.1 = "something overhead", the crease
// line does the work. Same xy-inset retry as TB_DetectCeiling, same reason.

struct TbOverDbg_t { float frac0 = 1.f, nz0 = 0.f; bool ss0 = false, retried = false; };

static bool TB_DetectOverhead(void* pLocal, const Vec3& vPos, const Vec3& vMins, const Vec3& vMaxs,
                              float flReach, GameTrace_t& traceOut, TbOverDbg_t& dbg)
{
    Vec3 vEnd = vPos;
    vEnd.z += flReach;
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    TraceHull(vPos, vEnd, vMins, vMaxs, MASK_PLAYERSOLID, &filter, &traceOut);
    dbg.frac0 = traceOut.fraction;
    dbg.nz0   = traceOut.plane.normal.z;
    dbg.ss0   = traceOut.startsolid || traceOut.allsolid;
    if (traceOut.fraction < 1.f && traceOut.plane.normal.z < -0.1f)
        return true;
    // nothing touched at all -> genuinely clear overhead, no retry (and no second trace)
    if (traceOut.fraction >= 1.f && !dbg.ss0)
        return false;
    Vec3 vInMins = vMins, vInMaxs = vMaxs;
    vInMins.x += k_tbOverInset; vInMins.y += k_tbOverInset;
    vInMaxs.x -= k_tbOverInset; vInMaxs.y -= k_tbOverInset;
    if (vInMaxs.x <= vInMins.x || vInMaxs.y <= vInMins.y)
        return false;
    dbg.retried = true;
    TraceHull(vPos, vEnd, vInMins, vInMaxs, MASK_PLAYERSOLID, &filter, &traceOut);
    return traceOut.fraction < 1.f && traceOut.plane.normal.z < -0.1f;
}

// ---- face helpers psassist.inl / psmap.inl ride ----

// no-header string compare -- surface.name is usually shared per material but not
// guaranteed, so pointer equality alone would split one brush
static bool TB_SameStr(const char* a, const char* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

// SMALLEST MOVE INTO THE WALL THE ENGINE CAN MAKE.
// TryPlayerMove builds end = origin + dt*v in float (gamemovement.cpp:2593) and traces
// m_Delta = end - start (cmodel.h:86), so the move into an axis-aligned wall is
// k * ulp(origin coord), never v*dt: at |x| in [2048,4096) one step is 2.44e-4u against a
// 7.2e-5u window at vz -12, D 78. A band target under ulp/2 rounds to zero perpendicular
// motion = nothing to pin on. f'_perp only gets more negative as d_perp shrinks, so if any
// k pins, k=1 pins. Returns the step toward the wall (0 = not axis-aligned, caller keeps
// the old search). axis_out: 0 = x, 1 = y
static float TB_UlpStepIntoWall(const Vec3& normal, const Vec3& origin, int& axis_out)
{
    axis_out = -1;
    if (fabsf(normal.x) > 0.9999f)      axis_out = 0;
    else if (fabsf(normal.y) > 0.9999f) axis_out = 1;
    else return 0.f;
    const float c = axis_out == 0 ? origin.x : origin.y;
    const float n = axis_out == 0 ? normal.x : normal.y;
    // normal points OUT of the wall, so into the wall is -n
    return fabsf(nextafterf(c, n > 0.f ? -FLT_MAX : FLT_MAX) - c);
}

// two face probes are the SAME BRUSH FACE. plane.dist cannot separate two flush brushes,
// so the texture name carries the boundary -- hence TEXTURE bug.
// tex=false asks SAME WALL instead: a pixel surf rides straight through a texture change.
// Only TB_SeamRunout's park walk passes false; asking the texture question there stopped
// the walk mid-wall, since maps build long walls from differently skinned brushes.
static bool TB_FaceSame(const GameTrace_t& a, const GameTrace_t& b, bool tex = true)
{
    if (a.fraction >= 1.f || b.fraction >= 1.f) return false;
    if (a.plane.normal.x * b.plane.normal.x + a.plane.normal.y * b.plane.normal.y
      + a.plane.normal.z * b.plane.normal.z < 0.9995f) return false;
    if (fabsf(a.plane.dist - b.plane.dist) > 0.10f) return false;
    return !tex || TB_SameStr(a.surface.name, b.surface.name);
}

// ---- TB sweep helpers (coldtires 81st/103rd/106th/107th) ----

// closed-form AirAccelerate (gamemovement.cpp:1990). Answers "does this variant move the
// player anywhere an already-simmed one did not" for ZERO sims. wishspd caps at 30, so at
// ride speed most of the fwd/angle grid collapses to the same null move
static Vec3 TB_AirAccelDelta(const Vec3& base_vel, float world_yaw, float forward,
                             float accel, float dt)
{
    float wishspeed = forward, yaw = world_yaw;
    if (wishspeed < 0.f) { wishspeed = -wishspeed; yaw += 180.f; } // negative fwd = wish backwards
    if (wishspeed <= 0.f) return Vec3();

    const float rad = DEG2RADF(yaw);
    const Vec3  wishdir(cosf(rad), sinf(rad), 0.f);
    const float wishspd = fminf(wishspeed, 30.f);
    const float currentspeed = base_vel.x * wishdir.x + base_vel.y * wishdir.y;
    const float addspeed = wishspd - currentspeed;
    if (addspeed <= 0.f) return Vec3();   // engine returns early -- velocity untouched
    // surface friction is 1 in air; over-estimating only makes two variants look MORE
    // different than they are = the safe direction for a dedup
    float accelspeed = accel * wishspeed * dt;
    if (accelspeed > addspeed) accelspeed = addspeed;
    return wishdir * accelspeed;
}

// below this two variants move the player <1.6e-4u apart over a tick -- 200x under the
// 0.03125 surface extension that decides every collision here
static constexpr float k_tbDupVelEps = 0.01f;

// ONE dedup list per hull for the whole tick. A delta an earlier pass evaluated reproduces
// that answer exactly: it cannot pin (the sweep would have stopped there) and it cannot take
// the creep pick either (equal support never passes the strict <)
struct TbDedup_t
{
    static constexpr int k_max = 288;   // sweep + solve, worst case, one sign each
    Vec3 seen[k_max];
    int  count = 0;

    bool SeenOrAdd(const Vec3& d)
    {
        for (int i = 0; i < count; ++i)
            if (fabsf(d.x - seen[i].x) < k_tbDupVelEps && fabsf(d.y - seen[i].y) < k_tbDupVelEps)
                return true;
        if (count < k_max) seen[count++] = d;
        return false;
    }
};
// creep hard-brake veto: a micro-move that eats half your speed is not a hold
static constexpr float k_tbXyCreepFrac = 0.5f;
// how far off the half-gravity target a CROUCH pin may drift over a 2-tick probe and still
// latch. A holding pin reads ~0 every tick; one that lost the seam is two gravity steps
// down (~24) by tick 2, so 12 splits them with a step of margin either way
static constexpr float k_tbDuckConfirmDz = 12.f;
// 118th PARK BRIDGE. A wallbug tick costs the WHOLE xy (its own gate is xy < 1), so parking
// while the ride still carries speed trades a 240 u/s surf for a 0 u/s hang
static constexpr float k_tbParkXyMax  = 30.f;

// 119th, ceiling side: how far a head-surf brush bottom may sit off flat (-1.00). Anything
// slacker is a ramp underside and HeadSurf may not lock a cmd to one
static constexpr float k_hsRampNz = 0.05f;

// HeadBounce: ticks one variant is held for. 3 = ~1u of steering at bhop speed; 123rd
// sizes the chain off the notch, capped at 6
static constexpr int   k_hbChain     = 3;
static constexpr int   k_hbChainMax  = 6;
static constexpr float k_hbGapMargin = 2.f;  // probe traces from the cmd END, sims start at its base
// how much rise something overhead must eat to count as a bounce. A wish cannot change vz,
// so free fall is a KNOWN number and anything under it is contact -- 1 u/s is well below
// the ~2.9 u/s a crease clip costs and far above float noise
static constexpr float k_hbCutMin = 1.f;
// 40 (coldtires) -> 160. The grid is 3 fwd x 14 yaw x chain 6 = 252 sims worst case and
// every HB log ended `sims=30/30` after FIVE variants. The pool is 384 and TbBudgetExpired
// is the real frame guard, so the count must not truncate the only pass that finds a bounce
static constexpr int k_hbSimCap = 160;

// ---- TB phase, for the on-screen indicator (hud/indicators.cpp, which has its own labels) ----
enum TbPhase_t { TBP_OFF = 0, TBP_SCAN, TBP_HUG, TBP_ARM, TBP_PIN, TBP_RIDE, TBP_PARK };
static TbPhase_t s_tbPhase   = TBP_OFF;
static int   s_tbRideLen = 0;   // pinned ticks in the current ride
static float s_tbIndD    = 0.f; // band_max  -- PAPER (10) window ceiling
static float s_tbIndVin  = 0.f; // v_in_now  -- live speed INTO the plane
static float s_tbIndAim  = 0.f; // press_target -- where the hold aims inside the band

void TB_GetInd(int& phase, int& rideLen, int& seams, float& D, float& vin, float& aim)
{
    phase = (int)s_tbPhase; rideLen = s_tbRideLen; seams = 0;
    D = s_tbIndD; vin = s_tbIndVin; aim = s_tbIndAim;
}

// diag throttles (DiagOk), one per body
static int s_tbDiagTick = -1000;
static int s_hsDiagTick = -1000;
static int s_hbDiagTick = -1000;

// ============================================================================
// TEXTURE BUG, cumidere style -- 1:1 port of Documents\!\cumidere tf2.dll (seo TF2, debug
// build), read with botox tools\sigtool.py + Ghidra:
//   FUN_1801cbee0  body          FUN_1801cb3c0  wall scan       FUN_1801cd340  gap score
// cumidere calls it BEFORE its own engine prediction; our pipeline calls it after, so the
// whole thing works off the restore base (= its pre-prediction state).
// Engine numbers only: 16 dirs, 10 forwards x 5 grazes = 50 sims max, accept vz on +-half
// gravity, else the flushest variant if its gap is under 0.2u.
// ============================================================================
static float s_tbcScanStart = 0.f;   // DAT_1804d6f30: direction the last wall was found at
static int   s_tbcDiagTick  = -1000;
static int   s_tbcRun       = 0;     // consecutive pinned ticks
static bool  s_tbcDuck      = false; // NOT cumidere: auto crouch latch, a confirmed duck pin holds IN_DUCK

// FUN_1801cb3c0. Line traces every 1u of hull height, centre out to the hull edge + 2 along
// each of 16 dirs starting at `start`; first dir with a facing near-vertical face wins, its
// closest hit is the wall. The two hull traces are exact supersets of the lines (perf only):
// every line lies inside the box, and the zero-xy column covers every line of one dir
static bool TBC_FindWall(void* pLocal, const Vec3& org, const Vec3& mins, const Vec3& maxs,
                         float start, GameTrace_t& out, float& out_angle)
{
    const float range = MV_PI * 2.f, step = MV_PI / 8.f;
    const float ex = fmaxf(fabsf(maxs.x), fabsf(mins.x));
    const float ey = fmaxf(fabsf(maxs.y), fabsf(mins.y));
    TraceFilterWorldProps fil; fil.Setup(pLocal);

    GameTrace_t box{};   // |line end| <= ex + 2 on x (ey + 2 on y); +1 slack for touch
    TraceHull(org, org, Vec3(-ex - 3.f, -ey - 3.f, mins.z - 1.f), Vec3(ex + 3.f, ey + 3.f, maxs.z + 1.f),
              MASK_PLAYERSOLID, &fil, &box);
    if (!box.DidHit()) return false;

    for (float a = start; a < start + range; a += step)
    {
        const float r = fmodf(a, range);
        const float c = cosf(r), s = sinf(r);
        const float reach = fminf(ey / fmaxf(fabsf(s), 1e-6f), ex / fmaxf(fabsf(c), 1e-6f)) + 2.f;
        const Vec3  end(org.x + c * reach, org.y + s * reach, org.z);

        GameTrace_t col{};
        TraceHull(org, end, Vec3(0.f, 0.f, mins.z), Vec3(0.f, 0.f, maxs.z), MASK_PLAYERSOLID, &fil, &col);
        if (!col.DidHit()) continue;

        bool hit = false;
        GameTrace_t best{};
        best.fraction = 1.f;
        for (float z = org.z + mins.z; z <= org.z + maxs.z; z += 1.f)
        {
            GameTrace_t tr{};
            TraceLine(Vec3(org.x, org.y, z), Vec3(end.x, end.y, z), MASK_PLAYERSOLID, &fil, &tr);
            if (tr.fraction >= 1.f) continue;
            if (fabsf(tr.plane.normal.z) > 0.35f) continue;
            if (tr.plane.normal.x * c + tr.plane.normal.y * s > -0.1f) continue;
            hit = true;
            if (tr.fraction < best.fraction) best = tr;
        }
        if (hit) { out = best; out_angle = r; return true; }
    }
    return false;
}

// FUN_1801cd340: hull face -> wall gap after a sim. One line at the wall hit's z, 32 along
// -normal, fraction * 32 minus the hull support. 1:1 incl. the quirk that the ray is 32*|n.xy|
// long but scored as 32 (identical on vertical walls, the only ones the scan accepts near)
static float TBC_Gap(void* pLocal, const GameTrace_t& wall)
{
    const Vec3 o = EntV3(pLocal, g_offOrigin);
    const Vec3 n = wall.plane.normal;
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    GameTrace_t tr{};
    TraceLine(Vec3(o.x, o.y, wall.endpos.z), Vec3(o.x - n.x * 32.f, o.y - n.y * 32.f, wall.endpos.z),
              MASK_ALL_MV, &fil, &tr);
    return tr.fraction * 32.f - TB_WallSupportDistance(pLocal, Vec3(n.x, n.y, 0.f));
}

// ---- cumidere misc_auto_align = the RIDE half of its texture bug ---------------------------
// cumidere's createmove (FUN_1801f9b10) runs FUN_1801d0170 (auto align) a few features before
// FUN_1801cbee0 (tb), both on the pre-prediction state. TB finds a pin, align is what keeps one:
// into-wall press with the least force that pins, and while pinned (base vz = -halfgrav) one held
// WASD key steers along the wall at up to 450 and a 450 press tilted atan(15..25/speed) off the
// wall normal gains speed (air accel caps the wish at 30, so vel.wish ~= 15..25 leaves 5..15 u/s
// of accel the wall does not eat). Every move is re-proven pinned by a 1-tick sim.

// FUN_1801d1a80: origin x or y sits 0.0075..0.03125u off a whole unit
static bool TBC_NearPixel(const Vec3& o)
{
    const auto near_px = [](float v)
    {
        const float f = v - floorf(v), g = 1.f - f;
        return (g >= 0.0075f && g <= 0.03125f) || (f >= 0.0075f && f <= 0.03125f);
    };
    return near_px(o.x) || near_px(o.y);
}

// FUN_1801cfca0: 16 hull traces 2u out, the first that hits a dead-vertical face wins. Zero-xy
// normal refused (start solid), cumidere would steer toward yaw 0 on it
static bool TBC_AlignWall(void* pLocal, const Vec3& org, const Vec3& mins, const Vec3& maxs, Vec3& n)
{
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    for (int i = 0; i < 16; ++i)
    {
        const float a = float(i) * 0.3926991f;
        GameTrace_t tr{};
        TraceHull(org, Vec3(org.x + cosf(a) * 2.f, org.y + sinf(a) * 2.f, org.z), mins, maxs,
                  MASK_PLAYERSOLID, &fil, &tr);
        if (tr.fraction != 1.f && tr.plane.normal.z == 0.f
            && (tr.plane.normal.x != 0.f || tr.plane.normal.y != 0.f))
            { n = tr.plane.normal; return true; }
    }
    return false;
}

// TBC_Align result bits, for the TBC: log
enum { TBCA_WALL = 1, TBCA_PRESS = 2, TBCA_PINNED = 4, TBCA_STEER = 8, TBCA_GAIN = 16 };

// FUN_1801d0170. Entity must sit on the restore base; it is left there
static int TBC_Align(void* pLocal, UserCmd_t* pCmd, int nBase)
{
    const auto Restore = [&]() { RestoreToFrame(nBase - 1); };
    const Vec3 org  = EntV3(pLocal, g_offOrigin);
    const Vec3 mins = EntV3(pLocal, g_offMins);
    const Vec3 maxs = EntV3(pLocal, g_offMaxs);
    Vec3 n{};
    if (!TBC_AlignWall(pLocal, org, mins, maxs, n)) return 0;
    int res = TBCA_WALL;

    const Vec3  vel    = EntV3(pLocal, g_offVelocity);
    const float speed  = vel.Length2D();
    const float half   = TargetPredictZVel();   // cumidere hardcodes -6
    // cumidere: 0.01. Valve Oct 2026 ClipVelocity subtracts DIST_EPSILON, so a top-plane pin now
    // ends at -6 + 0.03125 and 0.01 never fires on Valve servers (sv_wallbug 0)
    const auto  pin    = [&](float vz) { return fabsf(vz - half) < 0.05f; };
    const bool  pinned = pin(vel.z);
    const float user_fwd = pCmd->forwardmove, user_side = pCmd->sidemove;
    const float view_yaw = pCmd->viewangles.y;
    const int   b        = pCmd->buttons;

    const float into_yaw = VectorAngles(Vec3(-n.x, -n.y, 0.f)).y;
    const float rel = DEG2RADF(NormalizeAngle(into_yaw - view_yaw));
    const float c = cosf(rel), s = sinf(rel);

    // one tick of pCmd with (f, sd); out of budget = a miss (no pin, no speed)
    const auto sim = [&](float f, float sd) -> Vec3
    {
        if (SimsLeft() <= 0) return Vec3(0.f, 0.f, 1e30f);
        UserCmd_t fake = *pCmd;
        fake.forwardmove = f; fake.sidemove = sd;
        Restore();
        Pred_Simulate(pLocal, &fake);
        const Vec3 v = EntV3(pLocal, g_offVelocity);
        Restore();
        return v;
    };

    // press straight in, 0..100: least force that pins, else 10
    float best_f = c * 10.f, best_s = -s * 10.f;
    for (float m = 0.f; m <= 100.f; m += 10.f)
        if (pin(sim(c * m, -s * m).z)) { best_f = c * m; best_s = -s * m; res |= TBCA_PRESS; break; }
    pCmd->forwardmove = best_f;
    pCmd->sidemove    = best_s;
    // off a pixel offset, not riding and steering: the player's own move
    if (!TBC_NearPixel(org) && !pinned && (b & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT)))
        { pCmd->forwardmove = user_fwd; pCmd->sidemove = user_side; }
    if (!pinned) return res;
    res |= TBCA_PINNED;

    // riding: exactly one WASD key held
    float kyaw = 0.f;
    bool  key  = false;
    if ((b & IN_FORWARD)   && !(b & (IN_BACK | IN_MOVELEFT | IN_MOVERIGHT)))    { kyaw = view_yaw;         key = true; }
    if ((b & IN_BACK)      && !(b & (IN_FORWARD | IN_MOVELEFT | IN_MOVERIGHT))) { kyaw = view_yaw + 180.f; key = true; }
    if ((b & IN_MOVELEFT)  && !(b & (IN_FORWARD | IN_BACK | IN_MOVERIGHT)))     { kyaw = view_yaw + 90.f;  key = true; }
    if ((b & IN_MOVERIGHT) && !(b & (IN_FORWARD | IN_BACK | IN_MOVELEFT)))      { kyaw = view_yaw - 90.f;  key = true; }
    if (!key) return res;

    TraceFilterWorldProps fil; fil.Setup(pLocal);
    GameTrace_t tr{};
    const float kr = DEG2RADF(kyaw);
    TraceLine(org, Vec3(org.x + cosf(kr) * 128.f, org.y + sinf(kr) * 128.f, org.z), MASK_PLAYERSOLID, &fil, &tr);
    if (tr.fraction < 1.f)
    {
        // the held key's axis 0..450, the other axis keeps the press; the LAST pinned step wins
        float kf = best_f, ks = best_s;
        for (int k = 0; k < 451; k += 45)
        {
            const float f  = (b & IN_FORWARD)  ? float(k)  : (b & IN_BACK)      ? -float(k) : kf;
            const float sd = (b & IN_MOVELEFT) ? -float(k) : (b & IN_MOVERIGHT) ? float(k)  : ks;
            if (pin(sim(f, sd).z)) { kf = f; ks = sd; res |= TBCA_STEER; }
        }
        pCmd->forwardmove = kf;
        pCmd->sidemove    = ks;

        // fast and not gaining: 450 tilted a hair off the wall normal toward the drift
        const float speed2 = sim(kf, ks).Length2D();
        if (speed > 250.f && fabsf(speed - speed2) < 5.f)
        {
            float gain = 0.f, gf = kf, gs = ks;
            const float drift = NormalizeAngle(VectorAngles(Vec3(vel.x, vel.y, 0.f)).y - into_yaw);
            for (float a = 15.f; a < 30.f; a += 5.f)
            {
                const float ang = RAD2DEGF(atanf(a / speed));
                const float r   = DEG2RADF(NormalizeAngle(into_yaw + (drift < 0.f ? -ang : ang) - view_yaw));
                const float f = cosf(r) * 450.f, sd = -sinf(r) * 450.f;
                const Vec3  v = sim(f, sd);
                if (pin(v.z) && v.Length2D() - speed > gain) { gain = v.Length2D() - speed; gf = f; gs = sd; }
            }
            if (gain > 0.f) { pCmd->forwardmove = gf; pCmd->sidemove = gs; res |= TBCA_GAIN; }
        }
    }
    // never push against the held key
    if (pCmd->forwardmove < 0.f && (b & IN_FORWARD))   pCmd->forwardmove = 450.f;
    if (pCmd->forwardmove > 0.f && (b & IN_BACK))      pCmd->forwardmove = -450.f;
    if (pCmd->sidemove    < 0.f && (b & IN_MOVERIGHT)) pCmd->sidemove    = 450.f;
    if (pCmd->sidemove    > 0.f && (b & IN_MOVELEFT))  pCmd->sidemove    = -450.f;
    return res;
}

// FUN_1801cbee0
static void TextureBugCumidere(void* pLocal, UserCmd_t* pCmd)
{
    s_textureBugHit = false;
    s_tbOwnsCmd = false;
    s_tbRideHold = 0;
    s_tbIndD = s_tbIndVin = s_tbIndAim = 0.f;   // botox-style band numbers, nothing here
    if (!g_tbcAutoCrouch) s_tbcDuck = false;
    if (!EntAlive(pLocal)) { s_tbPhase = TBP_OFF; s_tbRideLen = s_tbcRun = 0; s_tbcDuck = false; return; }

    const int  nBase = PredCommandsPredicted();
    const auto Restore = [&]() { RestoreToFrame(nBase - 1); };
    Restore();   // cumidere's view of the world: before this cmd ran

    if ((EntI(pLocal, g_offFlags) & FL_ONGROUND) || EntMoveType(pLocal) != MOVETYPE_WALK)
        { s_tbPhase = TBP_OFF; s_tbRideLen = s_tbcRun = 0; s_tbcDuck = false; return; }

    const float user_fwd = pCmd->forwardmove, user_side = pCmd->sidemove;   // pre-align, for no-park
    const int   user_buttons = pCmd->buttons;
    // latched duck ride: every sim below (align, natural, grid) runs on the crouch hull
    if (s_tbcDuck) pCmd->buttons |= IN_DUCK;
    const int al = g_tbcAlign ? TBC_Align(pLocal, pCmd, nBase) : 0;

    const Vec3 org  = EntV3(pLocal, g_offOrigin);
    const Vec3 mins = EntV3(pLocal, g_offMins);
    const Vec3 maxs = EntV3(pLocal, g_offMaxs);
    const Vec3 vel  = EntV3(pLocal, g_offVelocity);

    GameTrace_t wall{};
    float ang = 0.f;
    if (!TBC_FindWall(pLocal, org, mins, maxs, s_tbcScanStart, wall, ang))
    {
        s_tbcScanStart = 0.f; s_tbPhase = TBP_OFF; s_tbRideLen = s_tbcRun = 0;
        s_tbcDuck = false; pCmd->buttons = user_buttons;   // no wall = ride over, stand back up
        return;
    }
    s_tbcScanStart = ang;

    const float wall_yaw = NormalizeAngle(RAD2DEGF(atan2f(-wall.plane.normal.y, -wall.plane.normal.x)));
    const float vel_yaw  = RAD2DEGF(atan2f(vel.y, vel.x));
    // graze AGAINST the along-wall drift
    const float sign = NormalizeAngle(vel_yaw - wall_yaw) >= 0.f ? -1.f : 1.f;

    static constexpr float k_fwd[10] = { 1.f, 0.875f, 0.5f, 1.f, 1.3856f, 2.125f, 2.875f, 5.f, 15.f, 30.f };
    const float half = TargetPredictZVel();   // -6 at 66 tick / 800 gravity, cumidere hardcodes -6
    const Vec3  view = pCmd->viewangles;

    bool  found = false;
    float best_gap = 1.f;
    float out_fwd = pCmd->forwardmove, out_side = pCmd->sidemove, out_off = 0.f;
    int   sims = 0;
    float last_vz = 0.f;

    // NOT cumidere: a cmd that already pins (align's steer / speed move, or the player's own)
    // is kept. cumidere's grid overwrites it with fwd=1 at 86 deg, and its hug with a variant
    // that does not pin at all -- the steer and the speed gain never reached the server
    bool natural = false;
    if (SimsLeft() > 0)
    {
        UserCmd_t fake = *pCmd;
        Restore();
        Pred_Simulate(pLocal, &fake);
        ++sims;
        last_vz = EntV3(pLocal, g_offVelocity).z;
        natural = found = fabsf(last_vz - half) < 0.1f || fabsf(last_vz + half) < 0.1f;
    }

    const auto grid = [&](int buttons)
    {
        for (int i = 0; i < 10 && !found; ++i)
        {
            for (float off = 85.999f; off < 90.f; off += 1.f)
            {
                if (SimsLeft() <= 0) break;
                UserCmd_t fake = *pCmd;
                fake.buttons     = buttons;
                fake.forwardmove = k_fwd[i];
                fake.sidemove    = 0.f;
                Vec3 wish = view; wish.y = wall_yaw + off * sign;
                CorrectMovement(&fake, wish, view);
                fake.viewangles = view;
    
                Restore();
                Pred_Simulate(pLocal, &fake);
                ++sims;
                const float vz  = EntV3(pLocal, g_offVelocity).z;
                const float gap = TBC_Gap(pLocal, wall);
                last_vz = vz;
    
                if (fabsf(vz - half) < 0.1f || fabsf(vz + half) < 0.1f)
                {
                    found = true;
                    out_fwd = fake.forwardmove; out_side = fake.sidemove; out_off = off;
                    break;
                }
                if (gap < best_gap)
                {
                    best_gap = gap;
                    out_fwd = fake.forwardmove; out_side = fake.sidemove; out_off = off;
                }
            }
            if (SimsLeft() <= 0) break;
        }
    };
    grid(pCmd->buttons);

    // NOT cumidere: auto crouch, botox style's pass 2. Standing missed on every variant ->
    // same grid on the duck hull. An air duck lifts the origin by the hull delta, so a duck
    // win can carry the hull off the seam it caught: 2-tick probe first, a lost pin is two
    // gravity steps down by then (k_tbDuckConfirmDz). Miss or reject = standing picks back
    bool duck_won = false;
    int  duck_rej = 0;
    if (!found && g_tbcAutoCrouch && !(pCmd->buttons & IN_DUCK) && SimsLeft() > 0)
    {
        const float s_gap = best_gap, s_fwd = out_fwd, s_side = out_side, s_off = out_off;
        grid(pCmd->buttons | IN_DUCK);
        if (found)
        {
            float dz = 1e30f;
            if (SimsLeft() > 1)
            {
                UserCmd_t probe = *pCmd;
                probe.forwardmove = out_fwd;
                probe.sidemove    = out_side;
                probe.buttons    |= IN_DUCK;
                Restore();
                Pred_Simulate(pLocal, &probe);
                Pred_Simulate(pLocal, &probe);
                sims += 2;
                const float vz2 = EntV3(pLocal, g_offVelocity).z;
                dz = fminf(fabsf(vz2 - half), fabsf(vz2 + half));
            }
            if (dz > k_tbDuckConfirmDz) { found = false; duck_rej = 1; }
        }
        duck_won = found;
        if (!found) { best_gap = s_gap; out_fwd = s_fwd; out_side = s_side; out_off = s_off; }
    }
    Restore();   // hand the entity back on the base, like the botox style does

    // NOT cumidere: botox style's "no off-grid stick". On a yaw-rotated wall a pin at park
    // speed (xy <= 30) and the hug just glue the hull into the clip, no seam to ride. Drop
    // both + align's press, player's own move back. A pin at speed still rides
    const bool off_grid = fminf(fabsf(wall.plane.normal.x), fabsf(wall.plane.normal.y)) > 0.10f;
    const bool no_park  = g_tbNoOffgridStick && off_grid
                       && (!found || vel.Length2D() <= k_tbParkXyMax);
    if (no_park)
    {
        found = false;
        best_gap = 1.f;
        pCmd->forwardmove = user_fwd;
        pCmd->sidemove    = user_side;
    }

    const bool hug = !found && best_gap < 0.2f;
    if (found || hug)
    {
        pCmd->forwardmove = out_fwd;
        pCmd->sidemove    = out_side;
    }
    // duck latch: set by a confirmed duck pin, held while pinned or hugging (those sims ran
    // ducked), dropped on a full miss -> the player's own duck bit back
    if (duck_won) s_tbcDuck = true;
    if (!found && !hug) s_tbcDuck = false;
    pCmd->buttons = s_tbcDuck ? (pCmd->buttons | IN_DUCK)
                              : ((pCmd->buttons & ~IN_DUCK) | (user_buttons & IN_DUCK));
    s_textureBugHit = found;
    s_tbOwnsCmd     = hug;
    s_tbcRun        = found ? s_tbcRun + 1 : 0;   // own count: TextureBug's drop_ride zeroes s_tbRideLen
    s_tbRideLen     = s_tbcRun;
    s_tbPhase       = found ? (s_tbRideLen >= 2 ? TBP_RIDE : TBP_PIN) : hug ? TBP_HUG : TBP_SCAN;

    if (g_debugLogEnable && DiagOk(s_tbcDiagTick, found || hug || no_park))
        // al = TBC_Align bits: 1 wall, 2 press pinned, 4 riding, 8 key steer pinned, 16 speed gain
        DebugLog("TBC: pin=%d nat=%d al=%d hug=%d gap=%.3f off=%.3f fwd=%.3f side=%.3f vz=%.1f lvz=%.2f "
                 "sims=%d xy=%.0f ang=%.2f n=%.2f %.2f %.2f sign=%.0f ride=%d og=%d np=%d duck=%d dwon=%d drej=%d",
                 (int)found, (int)natural, al, (int)hug, best_gap, out_off, out_fwd, out_side, vel.z, last_vz,
                 sims, vel.Length2D(), ang, wall.plane.normal.x, wall.plane.normal.y, wall.plane.normal.z,
                 sign, s_tbRideLen, (int)off_grid, (int)no_park, (int)s_tbcDuck, (int)duck_won, duck_rej);
}

// ============================================================================
// TEXTURE BUG, botox style -- 1:1 port of botox csgo hacks/movement/texturebug.cpp
// (on_create_move, texture_bug, head_bounce, tb_auto_align, preserve_move) and its two
// headers texturebug_common.h + texturebug_wallstrafe.h. Same order, same constants, same
// act codes (R P B L K U W S F N) and log lines.
// TF2 deltas are physics numbers, never code shape:
//   hull 82 / 62 (csgo 72 / 54), an air duck moves the feet 20 (csgo 9)
//   no air duck speed crop: CGameMovement::HandleDuckingSpeedCrop is ground-only (csgo 0.34)
//   jump 289 flat (tf_gamemovement.cpp:1315), no stamina; sv_airaccelerate default 10
//   tung surf key arms texture_bug too, with its widened vertical gate (TungRampBand)
// Not ported, TF2 has no such thing here: ladders, edge surf, fireman, air freeze, the pixel
// finder trick log, the wall climb caught/active gates (WC runs AFTER tb in this pipeline and
// yields to s_tbRideHold itself).
// ponytail: botox `head` flag (m_last_face_dz, read off the move helper touch list) is not
// ported -- our Pred_Simulate never runs ProcessImpacts, so the touch list is never reset and
// would read stale. It only fed m_hs_hit + the log; s_headSurfHit stays false in this style.
// Add it with an IMoveHelper::ResetTouchList call per sim if a yield ever needs it.
// ============================================================================
// airstuck.inl (included after this file): m_surfaceFriction off the pred datamap. TF2
// CategorizePosition sets 0.25 airborne at 0 < vz <= 250 (tf_gamemovement.cpp:2433) and
// AirAccelerate scales by it -- botox reads get_surface_friction() for the same reason
static float AsSurfaceFriction(void* pLocal);
#include "texturebug_wallstrafe.h"   // tbx::ws, botox texturebug_wallstrafe.h (gate-tested: tbws_test)

namespace tbx
{
// texturebug.cpp:26-37
constexpr float k_xy_creep_frac = 0.5f;
constexpr int   k_hb_chain      = 3;
constexpr int   k_hb_chain_max  = 6;
constexpr float k_hb_cut_min    = 1.f;
constexpr int   k_hb_refine     = 4;
constexpr float k_hb_side_speed = 30.f;
constexpr float k_hb_unduck_dz  = 20.f;   // TF2 air duck hull delta (csgo 9)
constexpr float k_hb_stand_h    = 82.f;   // TF2 VEC_HULL_MAX.z (csgo 72)
constexpr float k_hb_duck_h     = 62.f;   // TF2 VEC_DUCK_HULL_MAX.z (csgo 54)
constexpr float k_duck_crop     = 1.f;    // TF2 crops ducked move on the ground only (csgo 0.34)

// texturebug_common.h
constexpr int k_sim_cap        = 192;
constexpr int k_as_sim_reserve = 16;
// clock gone before tb ran (hitch upstream) still verifies a ride: hold tick = user + W +
// both ladders ~13 sims
constexpr int k_sim_floor      = 16;

static int   s_sims = 0, s_restores = 0, s_reserve = 0;
static bool  s_budget_live = false;
static float s_pin = -6.f;   // s_target_predict_z_vel: -g*dt/2

static int SimsLeft()
{
    const int left = (std::min)(k_sim_cap - s_reserve - s_sims, k_cmdSimCap - g_simReserve - g_simsThisCmd);
    if (s_budget_live && TbBudgetExpired())
        return (std::max)(0, (std::min)(left, k_sim_floor - s_sims));
    return left;
}
static void Restore(int frame) { ++s_restores; RestoreToFrame(frame); }
static void Simulate(void* pLocal, UserCmd_t* cmd) { ++s_sims; Pred_Simulate(pLocal, cmd); }

// tick_scale.h ticks(): a count written for 64 tick, scaled to the engine interval
static int Ticks(int at_64)
{
    const float s = TickInterval() * 64.f;
    if (s <= 0.f) return at_64;
    const int out = (int)std::round((float)at_64 / s);
    return out < 1 ? 1 : out;
}

static void Hull(void* pLocal, const Vec3& a, const Vec3& b, const Vec3& mins, const Vec3& maxs, GameTrace_t& tr)
{
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    TraceHull(a, b, mins, maxs, MASK_PLAYERSOLID, &fil, &tr);
}

static float Dot3(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
// nearest hull corner to a plane: min over the box of n.(corner)
static float SupportMin(const Vec3& n, const Vec3& mins, const Vec3& maxs)
{
    return (std::min)(n.x * mins.x, n.x * maxs.x) + (std::min)(n.y * mins.y, n.y * maxs.y)
         + (std::min)(n.z * mins.z, n.z * maxs.z);
}

/* hull hit plane dist, world space. world brush = raw. disp / prop / brush entity = rebuilt from
   the end ( every hull hit stops DIST_EPSILON off its plane ).
   ponytail: botox adds the collision origin for a SOLID_BSP brush entity instead of rebuilding;
   no solid-type read here, so brush entities take the rebuild (exact to a float ulp) */
static float HullPlaneDist(const GameTrace_t& tr, const Vec3& mins, const Vec3& maxs)
{
    const Vec3& p = tr.plane.normal;
    const bool disp = (tr.dispFlags & DISPSURF_SURFACE) != 0;
    if (!disp && (!tr.m_pEnt || tr.m_pEnt == GetClientEntity(0)) && tr.hitbox == 0)
        return tr.plane.dist;
    return Dot3(tr.endpos, p) + SupportMin(p, mins, maxs) - 0.03125f;
}

// move_fix_to_yaw: fwd/side meant at world yaw, rotated onto the cmd's view yaw. never strips buttons, callers do
static void MoveFixToYaw(const float yaw, UserCmd_t& c)
{
    const float y = yaw >= 0.f ? yaw : yaw + 360.f;
    const float v = c.viewangles.y >= 0.f ? c.viewangles.y : c.viewangles.y + 360.f;
    float d = y <= v ? 360.f - fabsf(y - v) : fabsf(v - y);
    d = 360.f - d;
    const float f = c.forwardmove, s = c.sidemove;
    c.forwardmove = cosf(DEG2RADF(d)) * f + cosf(DEG2RADF(d + 90.f)) * s;
    c.sidemove    = sinf(DEG2RADF(d)) * f + sinf(DEG2RADF(d + 90.f)) * s;
}

static Vec3 AirAccelDelta(const Vec3& base_vel, float world_yaw, float forward)
{
    return TB_AirAccelDelta(base_vel, world_yaw, forward, CvarFloat("sv_airaccelerate", 10.f), TickInterval());
}

static bool SteeringAwayFromWall(const UserCmd_t* cmd, const Vec3& wall_normal, float forward_move, float side_move,
                                 float min_dot = 0.3f)
{
    if (forward_move == 0.f && side_move == 0.f)
        return false;
    Vec3 forward, right;
    AngleVectors(Vec3(0.f, cmd->viewangles.y, 0.f), &forward, &right, nullptr);
    Vec3 wish(forward.x * forward_move + right.x * side_move, forward.y * forward_move + right.y * side_move, 0.f);
    const float wish_len = wish.Length2D();
    if (wish_len < 1.f)
        return false;
    wish.x /= wish_len; wish.y /= wish_len;
    Vec3 normal(wall_normal.x, wall_normal.y, 0.f);
    const float norm_len = normal.Length2D();
    if (norm_len < 0.001f)
        return false;
    normal.x /= norm_len; normal.y /= norm_len;
    return (wish.x * normal.x + wish.y * normal.y) > min_dot;
}

/* steering away AND your air accel moves you off: a wish inside the no-gain cone ( v.wishdir >= 30 ) adds nothing.
   log 10-10 agency: W held looking off the wall, xy frozen 225.759, tb bailed ( S ) the whole slide */
static bool MoveLeavesWall(const UserCmd_t* cmd, const Vec3& wall_normal, float forward_move, float side_move,
                           const Vec3& vel, float max_speed, float crop)
{
    constexpr float k_leave_speed = 1.f;
    if (!SteeringAwayFromWall(cmd, wall_normal, forward_move, side_move))
        return false;
    const float nl = sqrtf(wall_normal.x * wall_normal.x + wall_normal.y * wall_normal.y);
    if (nl < 0.001f)
        return false;
    const float yaw   = cmd->viewangles.y - RAD2DEGF(atan2f(side_move, forward_move));
    const float speed = fminf(sqrtf(forward_move * forward_move + side_move * side_move) * crop, fmaxf(0.f, max_speed));
    const Vec3 v = vel + AirAccelDelta(vel, yaw, speed);
    return (v.x * wall_normal.x + v.y * wall_normal.y) / nl > k_leave_speed;
}

static bool DetectOverhead(void* pLocal, const Vec3& pos, const Vec3& mins, const Vec3& maxs, float reach, GameTrace_t& out)
{
    Vec3 end = pos;
    end.z += reach;
    Hull(pLocal, pos, end, mins, maxs, out);
    return out.fraction < 1.f && out.plane.normal.z < -0.1f;
}

// ---- texturebug.h impl_t ----
struct state_t
{
    bool hit = false;
    bool hs_hit = false;
    bool hb_hit = false;   // head bounce landed this tick, one tick only, never a ride
    bool acted = false;
    bool owns_cmd = false;
    bool assist = false;
    bool wallstrafed = false;
    bool resimmed = false;
    ws::owned_move_t owned_move{};
    float input_forward = 0.f, input_side = 0.f, input_yaw = 0.f;
    int run = 0;           // pinned ticks in a row, for the indicator
    void reset_command() { owned_move.clear(); wallstrafed = false; }
};
static state_t s_tb{};

static bool s_tb_owned_prev = false;
static int  s_wc_act_tick = -1000;   // g_wall_climb.m_act_tick: last cmd wall climb acted on

/* the sent cmd's simmed END ( resim at the bottom of OnCreateMove ). next cmd's START must equal it: TB line div= / dvz= is how far
   the server ( or a feature after tb rewriting the cmd ) moved off our sim. -1 = no sent sim last cmd */
static int  s_sent_cmd = -1;
static Vec3 s_sent_org{}, s_sent_vel{};

static bool JumpTrickKey()
{
    return On(g_miniJump, g_miniJumpKey, g_miniJumpMode) || On(g_longJump, g_longJumpKey, g_longJumpMode)
        || EdgeJumpOn() || On(g_jumpbug, g_jumpbugKey, g_jumpbugMode);
}

static void SteerInto(UserCmd_t* cmd, const Vec3& wall_normal)
{
    constexpr float k_steer_forward = 450.f;
    Vec3 angle_wall = VectorAngles(Vec3(-wall_normal.x, -wall_normal.y, -wall_normal.z));
    angle_wall.y = NormalizeAngle(angle_wall.y);
    const Vec3 original_view = cmd->viewangles;
    cmd->forwardmove = k_steer_forward;
    cmd->sidemove = 0.f;
    Vec3 wish = original_view;
    wish.y = angle_wall.y;
    CorrectMovement(cmd, wish, original_view);
    cmd->viewangles = original_view;
}

// After a confirmed surf, assistance needs a wall at head height rather than just beside the
// body. Initial approaches keep their original inward push. Native prediction still decides
// actual catches, including valid feet pins.
static bool HeadWallReachable(void* pLocal, const Vec3& origin, const Vec3& end, const Vec3& mins, const Vec3& maxs,
                              const Vec3& normal)
{
    constexpr float epsilon = 0.03125f;
    Vec3 head_mins = mins, head_maxs = maxs;
    head_mins.z = maxs.z - epsilon;
    head_maxs.z = maxs.z + epsilon;
    GameTrace_t wall{};
    // Start just outside the face so a flush hull is not reported start-solid.
    const Vec3 from(origin.x + normal.x * (2.f * epsilon), origin.y + normal.y * (2.f * epsilon),
                    origin.z + normal.z * (2.f * epsilon));
    Hull(pLocal, from, end, head_mins, head_maxs, wall);
    const float same_face = Dot3(wall.plane.normal, normal);
    return !wall.startsolid && !wall.allsolid && wall.fraction < 1.f && fabsf(wall.plane.normal.z) < 0.1f
        && same_face >= 0.98f;
}

/* texture bug = box-brush pin ( IntersectRayWithBoxBrush ): the head crosses a wall brush's bottom ( or feet a flush lower brush's top )
   as the move crosses the wall plane by dx: engine picks the +z face, vz clipped to -g*dt/2. needs gap ~1 ulp, a hard hit parks at 0.03125 */
static void TextureBugImpl(void* pLocal, UserCmd_t* cmd, const bool tb_on, const bool tung_on)
{
    constexpr float k_eps        = 0.03125f;
    constexpr float k_touch      = k_eps * 1.3f;
    constexpr float k_contact    = 0.05f;
    constexpr float k_ratio      = 1.4f;
    /* 10-06 log: a wall top bounce window was dx 1.08e-3..1.25e-3, x1.4 rungs 9.0e-4 -> 1.26e-3 stepped over it */
    constexpr float k_rise_ratio = 1.1f;
    constexpr int   k_ladder_cap = 48;
    constexpr float k_wish_max   = 450.f;
    constexpr int   k_move_keys  = IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT;
    static const float k_keep[]  = { 0.000001f, 0.0001f, 0.002f, 0.05f, 0.3f };
    constexpr int   k_keep_max   = 4;

    static int s_ride_duck = -1;
    static int s_ride_user = -1;
    static int s_miss      = 0;
    static int s_run       = 0;
    static int s_pin_cmd   = -1;
    static int s_park_margin = 0, s_park_cmd = -1;
    static float s_park_from = 0.f, s_park_to = 0.f;
    static bool s_rode = false;
    const auto drop = [&]() {
        s_rode        = false;
        s_ride_duck   = -1;
        s_miss        = 0;
        s_run         = 0;
        s_park_margin = 0;
        s_park_cmd    = -1;
        s_tb.run      = 0;
    };
    s_tb.hit = s_tb.hs_hit = s_tb.owns_cmd = false;

    const bool armed = (tb_on || tung_on) && EntAlive(pLocal);
    /* START must be the server's END: the shift ( dn ) is START - our sim of the sent cmd, blind under opt 2 */
    s_tbSyncWant = armed;
    if (!armed) { drop(); return; }
    if ((s_prePredFlags & FL_ONGROUND) || EntMoveType(pLocal) != MOVETYPE_WALK) { drop(); return; }

    const int frame = PredCommandsPredicted() - 1;
    Restore(frame);
    const Vec3  org       = EntV3(pLocal, g_offOrigin);
    const Vec3  vel       = EntV3(pLocal, g_offVelocity);
    const bool  ducked0   = (EntI(pLocal, g_offFlags) & FL_DUCKING) != 0;
    const Vec3  mins      = EntV3(pLocal, g_offMins);
    const Vec3  maxs      = EntV3(pLocal, g_offMaxs);
    const float ipt       = TickInterval();
    const float pin       = s_pin;
    const float max_speed = g_offMaxspeed > 0 ? EntF(pLocal, g_offMaxspeed) : 450.f;
    const float k_acc     = CvarFloat("sv_airaccelerate", 10.f) * ipt * AsSurfaceFriction(pLocal);
    const bool  debug     = g_debugLogEnable;
    if (debug) {
        static char s_env[128] = {};
        char env[128];
        snprintf(env, sizeof(env), "tick=%.0f airaccel=%.1f gravity=%.0f predopt=%.0f", ipt > 0.f ? 1.f / ipt : 0.f,
                 CvarFloat("sv_airaccelerate", -1.f), CvarFloat("sv_gravity", -1.f), CvarFloat("cl_pred_optimize", -1.f));
        if (strcmp(env, s_env) != 0) { memcpy(s_env, env, sizeof(env)); DebugLog("TBENV: %s", env); }
    }
    const bool  div_ok  = s_sent_cmd >= 0 && cmd->command_number == s_sent_cmd + 1;
    const float div_org = div_ok ? (org - s_sent_org).Length() : -1.f;
    const float div_vz  = div_ok ? vel.z - s_sent_vel.z : 0.f;

    const UserCmd_t in = *cmd;
    const bool wallstrafe_enabled = g_tbWallstrafe;
    int sims = 0;
    /* rising START: a pin here is a head bounce ( seam / ceiling face clipped the rise ), one tick, never a ride.
       START vz == -pin exactly lands on the pin with no contact */
    const bool rising = vel.z + pin > 1e-3f;
    struct end_t {
        bool ok = false;
        Vec3 org{}, vel{};
        bool ride = false, bounce = false, head = false, ground = false;
    };
    const auto sim = [&](UserCmd_t c) {
        end_t e;
        if (SimsLeft() <= 0)
            return e;
        Restore(frame);
        Simulate(pLocal, &c);
        ++sims;
        const int fl = EntI(pLocal, g_offFlags);
        e.org    = EntV3(pLocal, g_offOrigin);
        e.ground = (fl & FL_ONGROUND) != 0;
        e.vel    = EntV3(pLocal, g_offVelocity);
        const bool walk   = EntMoveType(pLocal) == MOVETYPE_WALK;
        const bool pinned = !(fl & FL_ONGROUND) && walk && fabsf(e.vel.z - pin) < 1e-3f && e.vel.Length2D() >= 1.f;
        e.ride   = pinned && vel.z + pin < 0.f;
        e.bounce = pinned && rising && (!wallstrafe_enabled || ws::RetainsSpeed(vel.Length2D(), e.vel.Length2D()));
        e.head   = false;   // ponytail: m_last_face_dz not ported, see banner
        e.ok     = walk;
        return e;
    };
    const auto with_duck = [](UserCmd_t c, const int duck) {
        c.buttons = duck ? (c.buttons | IN_DUCK) : (c.buttons & ~IN_DUCK);
        return c;
    };
    char  act = 'N';
    float gap = -1.f, dx_hit = 0.f, v_in = 0.f, div_n = 0.f, shift_comp = 0.f;
    bool  flipped = false, head = false;
    int   lk = -1, look_a = -1, look_b = -1;
    static char s_last_act = 'N';
    static int  s_touch_cmd = -1;
    const auto log = [&]() {
        s_last_act = act;
        s_tb.run   = s_run;
        if (debug && DiagOk(s_tbDiagTick, act != 'N'))
            DebugLog("TB: act=%c gap=%.6f vz=%.2f xy=%.1f duck=%d lock=%d run=%d flip=%d head=%d dx=%.3e sims=%d us=%lld div=%.3e dvz=%.4f mg=%d vin=%.3f ud=%d lk=%d la=%d lb=%d org=%.7f,%.7f,%.7f vel=%.3f,%.3f dn=%.3e cp=%.2e",
                     act, gap, vel.z, vel.Length2D(), (cmd->buttons & IN_DUCK) ? 1 : 0, s_ride_duck, s_run, (int)flipped, (int)head,
                     dx_hit, sims, TbBudgetUsedUs(), div_org, div_vz, s_park_margin, v_in, (in.buttons & IN_DUCK) ? 1 : 0, lk, look_a, look_b,
                     org.x, org.y, org.z, vel.x, vel.y, div_n, shift_comp);
    };

    const int user_duck = (in.buttons & IN_DUCK) ? 1 : 0;
    if (s_ride_duck >= 0 && user_duck != s_ride_user)
        s_ride_duck = -1;
    const int held = s_ride_duck >= 0 ? s_ride_duck : user_duck;
    bool allow_duck  = (g_tbTypes & 1) != 0;
    bool allow_stand = (g_tbTypes & 2) != 0;
    if (!allow_duck && !allow_stand)
        allow_duck = allow_stand = true;
    /* auto duck off: tb never crouches you, only keeps your own crouch */
    if (!g_tbAutoDuck && !user_duck) {
        allow_duck  = false;
        allow_stand = true;
    }
    const auto allowed = [&](const int duck) { return duck ? allow_duck : allow_stand; };
    /* rising = a bounce: your stance, never a flip, never kept for the fall */
    int stance = rising ? held : allowed(held) ? held : !held;
    const UserCmd_t user = with_duck(in, stance);
    const auto commit = [&](const UserCmd_t& c) {
        cmd->forwardmove = c.forwardmove;
        cmd->sidemove    = c.sidemove;
        cmd->buttons     = c.buttons;
    };

    /* nearest vertical face along 8 world yaws, within reach of the hull. tung surf widens the
       vertical gate to its own band (TungRampBand), that is all tung is on top of this */
    const float reach  = std::clamp(g_tbReach, 0.f, 48.f);
    const float nz_max = TungRampBand(tung_on, g_tungMaxNz, 0.05f);
    Vec3  n{};
    float yaw_in = 0.f;
    {
        float best = FLT_MAX;
        for (int i = 0; i < 8; ++i) {
            const float a = DEG2RADF((float)(i < 4 ? i * 90 : (i - 4) * 90 + 45));
            GameTrace_t tr{};
            const Vec3 dir(cosf(a), sinf(a), 0.f);
            Hull(pLocal, org, Vec3(org.x + dir.x * (reach + 0.05f), org.y + dir.y * (reach + 0.05f), org.z), mins, maxs, tr);
            if (tr.startsolid || tr.fraction >= 1.f || fabsf(tr.plane.normal.z) > nz_max)
                continue;
            const Vec3& tn = tr.plane.normal;
            /* nearest hull corner to the plane: a face leaning over you ( n.z < 0 ) meets the head, xy-only read it |n.z|*82 too far */
            const float g = Dot3(org, tn) - HullPlaneDist(tr, mins, maxs) + SupportMin(tn, mins, maxs);
            if (g < best) {
                best = g;
                n    = tn;
            }
        }
        if (best == FLT_MAX) {
            /* contact last cmd, no wall now = cleared the top ( rising ) or fell past the bottom */
            if (debug && s_touch_cmd == cmd->command_number - 1)
                DebugLog("TB: off wall last=%c vz=%.2f org=%.7f,%.7f,%.7f", s_last_act, vel.z, org.x, org.y, org.z);
            drop();
            return;
        }
        gap = best;
        if (gap <= k_contact)
            s_touch_cmd = cmd->command_number;
        yaw_in = RAD2DEGF(atan2f(-n.y, -n.x));
    }
    v_in = -(vel.x * n.x + vel.y * n.y);
    /* START - our sim of the sent cmd along the normal: + = server ended farther off the wall than we simmed */
    if (div_ok)
        div_n = (org.x - s_sent_org.x) * n.x + (org.y - s_sent_org.y) * n.y + (org.z - s_sent_org.z) * n.z;
    /* shift = how far the server moves past our sim into the wall: median dn of unclipped ticks ( free 0.5u+ off, or after a park ).
       sim == server -> ~0 -> no comp */
    {
        constexpr int k_dn = 16;
        static float s_dn[k_dn];
        static int   s_dn_n = 0;
        if (div_ok && (gap > 0.5f || (s_last_act == 'K' && gap > 0.f)) && fabsf(div_n) < 1e-4f)
            s_dn[s_dn_n++ % k_dn] = div_n;
        if (s_dn_n >= 6) {
            float d[k_dn];
            const int cnt = (std::min)(s_dn_n, k_dn);
            std::copy(s_dn, s_dn + cnt, d);
            std::nth_element(d, d + cnt / 2, d + cnt);
            shift_comp = std::clamp(-d[cnt / 2], 0.f, 4e-5f);
        }
    }
    /* one ulp of the coordinate the wall normal moves: rungs / park margins finer than that move the hull the same */
    float ulp = 1e-7f;
    {
        const float coord = (std::max)(fabsf(org.x * n.x), fabsf(org.y * n.y));
        int ex = 0;
        std::frexp(coord, &ex);
        if (coord > 0.f)
            ulp = std::ldexp(1.f, ex - 24);
    }
    if (s_park_cmd >= 0 && cmd->command_number == s_park_cmd + 1) {
        if (s_park_from - gap < 0.5f * (s_park_from - s_park_to) || (s_park_to < 0.5f * k_eps && gap > 0.9f * k_eps)) {
            s_park_margin = s_park_margin ? s_park_margin * 2 : 2;
            if (debug)
                DebugLog("TB: park refused from=%.6f plan=%.6f got=%.6f vin=%.3f margin=%d", s_park_from, s_park_to, gap, v_in, s_park_margin);
        } else
            s_park_margin = 0;
    }
    s_park_cmd = -1;
    const float xy0 = vel.Length2D();
    bool user_dies  = false;
    UserCmd_t raw = in;
    raw.viewangles.y = s_tb.input_yaw;
    raw.forwardmove  = s_tb.input_forward;
    raw.sidemove     = s_tb.input_side;
    if (wallstrafe_enabled && MoveLeavesWall(&raw, n, raw.forwardmove, raw.sidemove, vel, max_speed,
                                             (ducked0 || (raw.buttons & IN_DUCK)) ? k_duck_crop : 1.f)) {
        drop();
        cmd->forwardmove = raw.forwardmove;
        cmd->sidemove    = raw.sidemove;
        act = 'S';
        log();
        return;
    }
    float user_along_speed = -FLT_MAX;
    if (const end_t e = sim(user); e.ok && (e.ride || e.bounce)) {
        commit(user);
        if (e.bounce) {
            s_tb.hb_hit = true;
            act = 'B';
            log();
            return;
        }
        s_tb.hit = true;
        s_tb.hs_hit = head = e.head;
        s_miss = 0;
        s_rode = true;
        s_pin_cmd = cmd->command_number;
        ++s_run;
        act = 'R';
        log();
        return;
    } else {
        user_dies = e.ok && xy0 > 50.f && e.vel.Length2D() < 0.2f * xy0;
        if (e.ok && !e.ground)
            user_along_speed = (e.vel.x * -n.y + e.vel.y * n.x) / sqrtf(n.x * n.x + n.y * n.y);
    }

    static int s_away = 0;
    s_away = MoveLeavesWall(&in, n, in.forwardmove, in.sidemove, vel, max_speed, (ducked0 || user_duck) ? k_duck_crop : 1.f)
           ? s_away + 1 : 0;
    if (s_away > (s_run > 0 ? 1 : 0)) {
        drop();
        act = 'S';
        log();
        return;
    }

    /* v_plane = plane closing speed. a tilted face also closes by the fall ( vz + pin ) * n.z, the xy press makes that up.
       v_old_n / v_plane are along n, AirAccelerate along yaw_in: / |n.xy| */
    const float nxy = sqrtf(n.x * n.x + n.y * n.y);
    const Vec3  tangent(-n.y / nxy, n.x / nxy, 0.f);
    const float along = vel.x * tangent.x + vel.y * tangent.y;
    const float direction = ws::Direction(along, along);
    const auto press_from = [&](const float v_old_n, const bool dk0, const float v_plane, const int duck, const float vz, bool keep_speed = true) {
        const float v_old = v_old_n / nxy;
        const float v_t   = (v_plane + (vz + pin) * n.z) / nxy;
        const float crop  = (duck || dk0) ? k_duck_crop : 1.f;
        float w = v_t >= v_old ? (std::max)((v_t - v_old) / k_acc, v_t) : -(std::max)((v_old - v_t) / k_acc, -v_t);
        w = std::clamp(w / crop, -k_wish_max, k_wish_max);
        UserCmd_t c = with_duck(in, duck);
        c.forwardmove = fabsf(w);
        c.sidemove    = 0.f;
        if (wallstrafe_enabled && rising && keep_speed) {
            const float speed    = (std::min)(fabsf(w) * crop, (std::max)(0.f, max_speed));
            const float sign     = w < 0.f ? -1.f : 1.f;
            const float delta    = sign * (std::min)(k_acc * speed, (std::max)(0.f, (std::min)(speed, 30.f) - v_old * sign));
            const float max_wish = (std::min)(k_wish_max * crop, (std::max)(0.f, max_speed));
            const auto  wish     = ws::AlignedWish(v_old, along * direction, delta, max_wish, k_acc);
            if (wish.gain > 1e-4f) {
                c.forwardmove = wish.into / crop;
                c.sidemove    = wish.along * direction / crop;
                MoveFixToYaw(yaw_in, c);
                c.buttons &= ~k_move_keys;
                return c;
            }
        }
        MoveFixToYaw(w >= 0.f ? yaw_in : yaw_in + 180.f, c);
        c.buttons &= ~k_move_keys;
        return c;
    };
    const float v_old = -(vel.x * n.x + vel.y * n.y);
    const auto  press = [&](const float v_t, const int duck) { return press_from(v_old, ducked0, v_t, duck, vel.z); };

    // Probe a small shortlist once, from the same restored frame as the catch search. Keep it
    // pending until catch/precision corrections have had priority.
    UserCmd_t strafe_cmd{};
    bool have_strafe = false;
    bool head_assist_blocked = false;
    /* off contact only while you already close on the wall. log 10-10 22:04: sliding 2u off a ledge, vin 0, W leaned you in
       0 -> 14.7 -> 22.8 -> 26.9 u/s ( user: "leans onto edges i dont want to reach" ) */
    if (wallstrafe_enabled && SimsLeft() > 3 && (gap <= k_touch || v_in > 1.f)) {
        const float yaw = DEG2RADF(raw.viewangles.y);
        const Vec3 wish(cosf(yaw) * raw.forwardmove + sinf(yaw) * raw.sidemove,
                        sinf(yaw) * raw.forwardmove - cosf(yaw) * raw.sidemove, 0.f);
        const float wdir = ws::Direction(wish.x * tangent.x + wish.y * tangent.y, along);
        const float crop = (stance || ducked0) ? k_duck_crop : 1.f;
        const float wishspeed = (std::min)(k_wish_max * crop, (std::max)(0.f, max_speed));
        const auto  wishes = ws::Candidates(v_old / nxy, along * wdir, wishspeed, k_acc);
        float best_speed = (std::max)(along * wdir, user_along_speed == -FLT_MAX ? along * wdir : user_along_speed * wdir);
        for (const auto& w : wishes) {
            if (w.gain <= 1e-4f || SimsLeft() <= 0)
                continue;
            UserCmd_t c = with_duck(in, stance);
            c.forwardmove = w.into * k_wish_max;
            c.sidemove    = w.along * wdir * k_wish_max;
            MoveFixToYaw(yaw_in, c);
            c.buttons &= ~k_move_keys;
            const end_t e = sim(c);
            if (!e.ok || e.ground || !ws::RetainsSpeed(xy0, e.vel.Length2D()))
                continue;
            // Check the wall still exists at the predicted position, including corners; an old
            // infinite plane is insufficient here.
            GameTrace_t wall{};
            const Vec3 cmins = EntV3(pLocal, g_offMins), cmaxs = EntV3(pLocal, g_offMaxs);
            Hull(pLocal, e.org, Vec3(e.org.x - n.x * (reach + k_touch), e.org.y - n.y * (reach + k_touch), e.org.z - n.z * (reach + k_touch)),
                 cmins, cmaxs, wall);
            const float same_face = Dot3(wall.plane.normal, n);
            const float end_gap = gap + Dot3(e.org - org, n);
            if (wall.startsolid || wall.allsolid || wall.fraction >= 1.f || same_face < 0.98f
                || end_gap > (std::max)(gap, k_touch) + k_touch)
                continue;
            if (e.ride || e.bounce) {
                commit(c);
                s_tb.hit = e.ride;
                s_rode = s_rode || e.ride;
                s_tb.hb_hit = e.bounce;
                s_tb.hs_hit = e.ride && e.head;
                s_miss = 0;
                s_ride_duck = stance;
                s_ride_user = user_duck;
                s_pin_cmd = cmd->command_number;
                ++s_run;
                act = 'P';
                log();
                return;
            }
            if (s_rode && !rising
                && !HeadWallReachable(pLocal, e.org, Vec3(e.org.x - n.x * (reach + k_touch), e.org.y - n.y * (reach + k_touch),
                                                          e.org.z - n.z * (reach + k_touch)), cmins, cmaxs, n)) {
                head_assist_blocked = true;
                continue;
            }
            const float speed = (e.vel.x * tangent.x + e.vel.y * tangent.y) * wdir;
            if (speed > best_speed + 0.005f) {
                best_speed = speed;
                strafe_cmd = c;
                have_strafe = true;
            }
        }
    }
    const auto send_strafe = [&]() {
        commit(strafe_cmd);
        s_tb.wallstrafed = s_tb.owns_cmd = true;
        act = 'W';
        log();
    };

    /* touching: dx ladder from the smallest crossing press ( feet seam ) up to the head pin bound at Hbrush = 0.
       rungs closer than one ulp of the coordinate move the hull the same, so steps are at least one ulp */
    if (gap <= k_touch) {
        const float g  = (std::max)(gap, 1e-7f);
        const float dz = fabsf((vel.z + pin) * ipt) + 1e-4f;
        const auto ladder = [&](const int duck, UserCmd_t& out, bool& out_head) {
            const float hp = (duck || ducked0) ? k_hb_duck_h : k_hb_stand_h;
            const float hi = (std::max)((k_eps - g) * dz / (hp + k_eps), g * 1.5f);
            /* climb the pinning run, send its middle. rung dx is probed at dx + shift ( our sim of dx + shift = the server's of dx ).
               log 10-11 agency loopback: hold windows 0.9 ulp wide sat between our whole-ulp rungs ( ride 1.02 ). lab agency, shift
               1.3..2.3e-5: no comp 5483 ride ticks, middle - comp 10947, probe + comp 18955 ( exact comp = no shift ) */
            float run_dx[k_ladder_cap];
            bool  run_head[k_ladder_cap], run_plain[k_ladder_cap];
            int   run_n = 0, k = 0;
            const float step = rising ? k_rise_ratio : k_ratio;
            const float dx0  = g * 1.0005f + 1e-7f;
            for (float dx = dx0; dx <= hi && k < k_ladder_cap; dx = (std::max)(dx * step, dx + ulp), ++k) {
                UserCmd_t candidate = press((dx + shift_comp) / ipt, duck);
                end_t e = sim(candidate);
                bool plain_used = false;
                if (wallstrafe_enabled && rising && e.ok && !e.bounce && SimsLeft() > 0) {
                    const UserCmd_t plain = press_from(v_old, ducked0, (dx + shift_comp) / ipt, duck, vel.z, false);
                    if (plain.forwardmove != candidate.forwardmove || plain.sidemove != candidate.sidemove) {
                        e = sim(plain);
                        plain_used = true;
                    }
                }
                if (!e.ok && SimsLeft() <= 0)
                    break;
                if (e.ok && (e.ride || e.bounce)) {
                    run_dx[run_n]     = dx;
                    run_plain[run_n]  = plain_used;
                    run_head[run_n++] = e.head;
                } else if (run_n)
                    break;
            }
            if (!run_n)
                return false;
            dx_hit   = run_dx[run_n / 2];
            out      = press_from(v_old, ducked0, dx_hit / ipt, duck, vel.z, !run_plain[run_n / 2]);
            out_head = run_head[run_n / 2];
            return true;
        };
        UserCmd_t hit{};
        bool got = ladder(stance, hit, head);
        if (!got && !rising && allowed(!stance) && ladder(!stance, hit, head))
            got = flipped = true;
        if (got && !rising && ipt > 0.01f && cmd->command_number != s_pin_cmd + 1) {
            constexpr int   k_look       = 10;
            constexpr int   k_look_cap   = 100;
            constexpr float k_look_ratio = 2.f;
            /* branches capped by k_look_cap + the time share, not k_sim_cap: log 10-10 agency 6/25 skip branches starved at 192
               ( lk=2 = take ). lab TB_SIMCAP 192 vs off 5 maps: ride ticks dust2 +7% inferno +5%, catches same */
            const auto look_out = [&]() { return (s_budget_live && TbBudgetExpired()) || (s_reserve > 0 && SimsLeft() <= 0); };
            const auto pinned_now = [&](const float vz_start) {
                const Vec3 v = EntV3(pLocal, g_offVelocity);
                return !(EntI(pLocal, g_offFlags) & FL_ONGROUND) && EntMoveType(pLocal) == MOVETYPE_WALK
                    && fabsf(v.z - pin) < 1e-3f && v.Length2D() >= 1.f && vz_start + pin < 0.f;
            };
            const auto rollout = [&](UserCmd_t first, int st) -> int {
                const int s0 = sims;
                if (look_out())
                    return -1;
                Restore(frame);
                Simulate(pLocal, &first);
                ++sims;
                int r = pinned_now(vel.z) ? 1 : 0, last = 0; /* the hold must show inside the rollout ( lab twin ) */
                // botox restores the frame before its snapshot_load; ours is a full datamap
                // transfer already (prediction.h PREFIX SNAPSHOTS), so the load alone is the state
                const auto from_here = [&](UserCmd_t c) {
                    Pred_SnapLoad(pLocal, 0);
                    Simulate(pLocal, &c);
                    ++sims;
                };
                for (int i = 0; i < k_look && sims - s0 < k_look_cap; ++i) {
                    if ((EntI(pLocal, g_offFlags) & FL_ONGROUND) || EntMoveType(pLocal) != MOVETYPE_WALK)
                        break;
                    if (look_out() || !Pred_SnapSave(pLocal, 0))
                        return -1;
                    const Vec3 o = EntV3(pLocal, g_offOrigin), v = EntV3(pLocal, g_offVelocity);
                    const bool  dk  = (EntI(pLocal, g_offFlags) & FL_DUCKING) != 0;
                    const float gk  = (std::max)(gap + Dot3(o - org, n), 1e-7f);
                    const float vo  = -(v.x * n.x + v.y * n.y);
                    const float dzk = fabsf((v.z + pin) * ipt) + 1e-4f;
                    bool pinned = false;
                    for (int s = 0; s < 2 && !pinned && gk <= k_touch; ++s) {
                        const int d = s ? !st : st;
                        if (s && !allowed(d))
                            break;
                        const float hi = (std::max)((k_eps - gk) * dzk / (((d || dk) ? k_hb_duck_h : k_hb_stand_h) + k_eps), gk * 1.5f);
                        for (float dx = gk * 1.0005f + 1e-7f; dx <= hi && sims - s0 < k_look_cap; dx = (std::max)(dx * k_look_ratio, dx + ulp)) {
                            if (look_out())
                                return -1;
                            from_here(press_from(vo, dk, (dx + shift_comp) / ipt, d, v.z));
                            if (pinned_now(v.z)) {
                                pinned = true;
                                st     = d;
                                break;
                            }
                        }
                    }
                    if (pinned) {
                        ++r;
                        if (last)
                            return r + (k_look - 1 - i);
                        last = 1;
                        continue;
                    }
                    if (look_out())
                        return -1;
                    from_here(with_duck(in, st));
                    last = 0;
                }
                return r;
            };
            look_a = rollout(hit, (hit.buttons & IN_DUCK) ? 1 : 0);
            look_b = look_a >= 0 ? rollout(with_duck(in, held), held) : -1;
            lk     = look_a < 0 || look_b < 0 ? 2 : look_b > look_a ? 1 : 0;
            if (lk == 1) {
                if (++s_miss > 1) {
                    if (s_miss > k_keep_max)
                        s_ride_duck = -1;
                    s_run = 0;
                }
                const UserCmd_t skip = with_duck(in, held);
                commit(skip);
                s_tb.owns_cmd = skip.buttons != in.buttons && s_miss <= 1;
                act = 'L';
                log();
                return;
            }
        }
        if (got) {
            commit(hit);
            s_miss = 0;
            if (rising) {
                s_tb.hb_hit = true;
                act = 'B';
                log();
                return;
            }
            s_ride_duck = (hit.buttons & IN_DUCK) ? 1 : 0;
            s_ride_user = user_duck;
            ++s_run;
            s_rode      = true;
            s_pin_cmd   = cmd->command_number;
            s_tb.hit    = true;
            s_tb.hs_hit = head;
            act = 'P';
            log();
            return;
        }
    }

    if (++s_miss > 1) {
        if (gap > k_touch || s_miss > k_keep_max)
            s_ride_duck = -1;
        s_run = 0;
    }
    // A capped search is unknown, not a lost catch. Never resume assistance because the
    // remaining catch candidates could not be evaluated.
    if (SimsLeft() <= 0)
        have_strafe = false;
    if (have_strafe && gap > k_touch) {
        send_strafe();
        return;
    }
    if ((reach > 0.f || gap <= k_touch) && gap > 0.f && s_park_margin <= 16) {
        /* never under 1 ulp: a sub-ulp end is the plane or a hard hit on the server */
        const float floor_gap = (float)(std::max)(s_park_margin, 1) * ulp;
        float last_end = -1.f;
        for (const float keep : k_keep) {
            const float end = (std::max)(gap * keep, floor_gap);
            if (end >= gap)
                break;
            if (end == last_end)
                continue;
            last_end = end;
            const UserCmd_t c = press((gap - end) / ipt, stance);
            const end_t e = sim(c);
            if (!e.ok || e.ground || (wallstrafe_enabled && rising && !ws::RetainsSpeed(xy0, e.vel.Length2D())))
                continue;
            if (wallstrafe_enabled && s_rode && !rising && !e.ride
                && !HeadWallReachable(pLocal, e.org, Vec3(e.org.x - n.x * (reach + k_touch), e.org.y - n.y * (reach + k_touch),
                                                          e.org.z - n.z * (reach + k_touch)),
                                      EntV3(pLocal, g_offMins), EntV3(pLocal, g_offMaxs), n)) {
                head_assist_blocked = true;
                continue;
            }
            const float g1    = Dot3(e.org, n) - Dot3(org, n) + gap;
            const bool  touched = -(e.vel.x * n.x + e.vel.y * n.y) <= 1e-3f;
            if (g1 > 0.f && g1 < gap && !touched && g1 >= floor_gap * 0.5f) {
                commit(c);
                s_tb.owns_cmd = true;
                s_park_cmd  = cmd->command_number;
                s_park_from = gap;
                s_park_to   = g1;
                act = 'K';
                log();
                return;
            }
        }
    }
    /* unstick: your move dead-stops while flush = the next brush's lip sticks out under 0.03125. a plain hard hit slides past it,
       the 1 ulp park doesn't: step off the plane to the smallest gap that keeps the slide */
    if (user_dies && gap <= k_touch) {
        for (const float to : { k_eps, 0.125f, 0.5f }) {
            const UserCmd_t c = press(-(to - gap) / ipt, stance);
            const end_t e = sim(c);
            if (e.ok && e.vel.Length2D() >= 0.2f * xy0
                && (!wallstrafe_enabled || !rising || ws::RetainsSpeed(xy0, e.vel.Length2D()))) {
                commit(c);
                s_tb.owns_cmd = true;
                act = 'U';
                log();
                return;
            }
        }
    }
    if (have_strafe && SimsLeft() > 0) {
        send_strafe();
        return;
    }
    const bool fell = s_rode && s_miss > 1;
    const UserCmd_t mine = with_duck(head_assist_blocked ? raw : in, held);
    commit(mine);
    s_tb.owns_cmd = cmd->buttons != in.buttons && s_miss <= 1;
    if (fell)
        act = 'F';
    log();
}

static void HeadBounceImpl(void* pLocal, UserCmd_t* cmd, const bool tb_on)
{
    static int   s_plan_cmd = -1, s_plan_left = 0;
    static float s_plan_yaw = 0.f, s_plan_fwd = 0.f;
    static bool  s_plan_unduck = false, s_plan_own = false;
    const bool  plan_live = cmd && s_plan_left > 0 && cmd->command_number == s_plan_cmd + 1;
    const int   plan_left = s_plan_left;
    const float p_yaw = s_plan_yaw, p_fwd = s_plan_fwd;
    const bool  p_unduck = s_plan_unduck, p_own = s_plan_own;
    s_plan_left = 0;

    if (!tb_on)
        return;
    if (!cmd || !EntAlive(pLocal))
        return;
    const bool wallstrafe_enabled = g_tbWallstrafe;
    if (s_tb.hit || s_tb.hs_hit || s_tb.hb_hit)
        return;
    if ((s_prePredFlags & FL_ONGROUND) || EntMoveType(pLocal) != MOVETYPE_WALK)
        return;

    const float half = s_pin;

    const Vec3  start_pos = s_prePredOrigin;
    const Vec3  base_vel  = s_prePredVel;
    const float base_vz   = base_vel.z;
    if (base_vz <= 0.f)
        return;

    const Vec3 mins = EntV3(pLocal, g_offMins);
    Vec3       maxs = EntV3(pLocal, g_offMaxs);
    maxs.z          = (s_prePredFlags & FL_DUCKING) ? k_hb_duck_h : k_hb_stand_h;
    const float dt  = TickInterval();

    const bool debug = g_debugLogEnable;

    const Vec3  original_view    = cmd->viewangles;
    const float original_forward = cmd->forwardmove;
    const float original_side    = cmd->sidemove;
    const int   original_buttons = cmd->buttons;

    const int  n_base           = PredCommandsPredicted();
    const auto restore_original = [&]() { Restore(n_base - 1); };

    int budget = SimsLeft();
    if (budget > 40)
        budget = 40;
    if (budget <= 0 && !plan_live)
        return;
    int sims_used = 0;

    const float xy_base = base_vel.Length2D();
    /* a bounce that costs half the speed is not a catch, it is a crash into the ceiling */
    const float xy_floor = xy_base >= 1.f ? (wallstrafe_enabled ? xy_base - ws::k_speed_tolerance : xy_base * k_xy_creep_frac) : 0.f;
    const float vz_free  = base_vz + 2.f * half;

    const float rise = fmaxf(0.f, base_vz + half) * dt;
    const auto hb_ticks_to_gap = [&](float need) -> int {
        float climbed = 0.f, vz_i = base_vz;
        for (int i = 1; i <= k_hb_chain_max; ++i) {
            const float step = (vz_i + half) * dt;
            if (step <= 0.f)
                return 0;
            climbed += step;
            if (climbed >= need)
                return i;
            vz_i += 2.f * half;
        }
        return 0;
    };
    const float reach = fminf(4.f + base_vz * dt * k_hb_chain_max, 48.f);
    GameTrace_t over{};
    const bool  over_hit = DetectOverhead(pLocal, start_pos, mins, maxs, reach, over);
    const float gap      = over_hit ? over.fraction * reach : 0.f;

    GameTrace_t arc{};
    int   arc_tick  = 0;
    bool  arc_clear = true;
    float arc_gap   = 0.f;
    {
        Vec3  p       = start_pos;
        float vz_i    = base_vz;
        float climbed = 0.f;
        for (int i = 1; i <= k_hb_chain_max; ++i) {
            const float step = (vz_i + half) * dt;
            if (step <= 0.f)
                break;
            const Vec3 q(p.x + base_vel.x * dt, p.y + base_vel.y * dt, p.z + step);
            Hull(pLocal, p, q, mins, maxs, arc);
            if (arc.startsolid || arc.allsolid) {
                arc_clear = false;
                break;
            }
            if (arc.fraction < 1.f) {
                if (arc.plane.normal.z < -0.1f) {
                    arc_tick = i;
                    arc_gap  = climbed + step * arc.fraction;
                } else {
                    arc_clear = false;
                }
                break;
            }
            climbed += step;
            p = q;
            vz_i += 2.f * half;
        }
    }
    int side_hit = 0;
    if (arc_tick == 0 && !over_hit && xy_base > 1.f) {
        const Vec3 lat(-base_vel.y / xy_base, base_vel.x / xy_base, 0.f);
        for (int sd = 1; sd >= -1 && side_hit == 0; sd -= 2) {
            const float vx = base_vel.x + lat.x * k_hb_side_speed * (float)sd;
            const float vy = base_vel.y + lat.y * k_hb_side_speed * (float)sd;
            Vec3  p       = start_pos;
            float vz_i    = base_vz;
            float climbed = 0.f;
            for (int i = 1; i <= k_hb_chain_max; ++i) {
                const float step = (vz_i + half) * dt;
                if (step <= 0.f)
                    break;
                const Vec3 q(p.x + vx * dt, p.y + vy * dt, p.z + step);
                GameTrace_t tr{};
                Hull(pLocal, p, q, mins, maxs, tr);
                if (tr.startsolid || tr.allsolid || (tr.fraction < 1.f && tr.plane.normal.z >= -0.1f))
                    break;
                if (tr.fraction < 1.f) {
                    side_hit = sd;
                    arc      = tr;
                    arc_tick = i;
                    arc_gap  = climbed + step * tr.fraction;
                    break;
                }
                climbed += step;
                p = q;
                vz_i += 2.f * half;
            }
        }
    }

    int unduck_need = 0;
    if ((s_prePredFlags & FL_DUCKING) && (original_buttons & IN_DUCK) && !s_tb.owns_cmd) {
        const Vec3 org_u(start_pos.x, start_pos.y, start_pos.z - k_hb_unduck_dz);
        const Vec3 maxs_u(maxs.x, maxs.y, mins.z + k_hb_stand_h);
        GameTrace_t over_u{};
        if (DetectOverhead(pLocal, org_u, mins, maxs_u, reach, over_u))
            unduck_need = hb_ticks_to_gap(over_u.fraction * reach);
    }

    if (!over_hit && arc_tick == 0 && unduck_need <= 0)
        return;

    const int  need_ticks = arc_tick > 0 ? arc_tick : (over_hit ? hb_ticks_to_gap(gap) : 0);
    const bool brake      = arc_tick == 0 && arc_clear && over_hit;
    if (need_ticks <= 0 && unduck_need <= 0) {
        if (debug) {
            static int s_far_tick = -1000;
            const int now = g_globalVars ? g_globalVars->tickcount : 0;
            if (now - s_far_tick > 64) {
                s_far_tick = now;
                DebugLog("HB: far gap=%.2f rise=%.2f nz=%.2f vz=%.1f", gap, rise, over.plane.normal.z, base_vz);
            }
        }
        return;
    }
    const int chain   = std::clamp(need_ticks + 1, k_hb_chain, k_hb_chain_max);
    const int chain_u = std::clamp(unduck_need + 1, k_hb_chain, k_hb_chain_max);

    float best_dz = 1e9f, best_vz = 0.f, best_xy = 0.f, best_cut = 0.f;
    struct hb_best_t {
        bool  have = false;
        int   tick = 0, buttons = 0, refine = 0;
        float fwd = 0.f, side = 0.f, yaw = 0.f, wish = 0.f, xy = 0.f;
    };
    hb_best_t hb_best{};
    int dbg_bounces = 0;

    const auto step_result = [&](float vz_in) -> int {
        const Vec3  v       = EntV3(pLocal, g_offVelocity);
        const float free_vz = vz_in + 2.f * half;
        const float cut     = free_vz - v.z;
        const float dz      = fabsf(v.z - half);
        if (cut > best_cut) {
            best_cut = cut;
            best_vz  = v.z;
            best_xy  = v.Length2D();
        }
        if (dz < best_dz)
            best_dz = dz;
        if ((EntI(pLocal, g_offFlags) & FL_ONGROUND) || EntMoveType(pLocal) != MOVETYPE_WALK)
            return -1;
        if (wallstrafe_enabled && v.Length2D() < xy_floor)
            return -1;
        if (cut > k_hb_cut_min)
            return v.Length2D() >= xy_floor ? 1 : -1;
        return v.z > 0.f ? 0 : -1;
    };

    const GameTrace_t& hit = arc_tick > 0 ? arc : over;
    const char* src     = side_hit != 0 ? "side" : arc_tick > 0 ? "arc" : over_hit ? (brake ? "brake" : "up") : "none";
    const float gap_log = arc_tick > 0 ? arc_gap : gap;
    const auto log_result = [&](const char* how) {
        if (!debug)
            return;
        DebugLog("HB: %s src=%s nz=%.2f disp=%d gap=%.2f vz=%.1f->%.1f free=%.1f tgt=%.2f cut=%.2f dz=%.2f xy=%.0f->%.0f/%.0f chain=%d/%d sims=%d side=%d udn=%d plan=%d bn=%d bxy=%.0f",
                 how, src, hit.plane.normal.z, (int)((hit.dispFlags & DISPSURF_SURFACE) != 0),
                 gap_log, base_vz, best_vz, vz_free, half, best_cut, best_dz, xy_base, best_xy,
                 xy_floor, need_ticks, chain, sims_used, side_hit, unduck_need, plan_live ? plan_left : 0, dbg_bounces,
                 hb_best.xy);
    };

    const auto run_chain = [&](UserCmd_t* c, int n) -> int {
        for (int i = 0; i < n; ++i) {
            if (sims_used >= budget || (wallstrafe_enabled && SimsLeft() <= 0))
                return -1;
            const float vz_in = EntV3(pLocal, g_offVelocity).z;
            ++sims_used;
            Simulate(pLocal, c);
            const int r = step_result(vz_in);
            if (r != 0)
                return r > 0 ? i + 1 : 0;
        }
        return 0;
    };

    const auto set_plan = [&](int left, float yaw, float fwd, bool unduck, bool own) {
        s_plan_left   = left;
        s_plan_cmd    = cmd->command_number;
        s_plan_yaw    = yaw;
        s_plan_fwd    = fwd;
        s_plan_unduck = unduck;
        s_plan_own    = own;
    };
    const auto write_plan = [&]() {
        cmd->viewangles = original_view;
        cmd->buttons    = p_unduck ? original_buttons & ~IN_DUCK : original_buttons;
        if (p_own) {
            cmd->forwardmove = original_forward;
            cmd->sidemove    = original_side;
            return;
        }
        cmd->forwardmove = p_fwd;
        cmd->sidemove    = 0.f;
        Vec3 wish = original_view;
        wish.y    = p_yaw;
        CorrectMovement(cmd, wish, original_view);
        cmd->viewangles = original_view;
    };
    const auto keep_blind = [&]() {
        restore_original();
        if (wallstrafe_enabled) {
            // Budget exhaustion is not evidence that a stored braking plan is still safe for
            // speed. Fall back to this tick's original movement.
            cmd->forwardmove = original_forward;
            cmd->sidemove    = original_side;
            cmd->buttons     = original_buttons;
            cmd->viewangles  = original_view;
            return;
        }
        write_plan();
        s_tb.hb_hit = true;
        set_plan(plan_left - 1, p_yaw, p_fwd, p_unduck, p_own);
        log_result("keep-blind");
    };
    if (plan_live && sims_used >= budget) {
        keep_blind();
        return;
    }

    if (need_ticks > 0) {
        restore_original();
        UserCmd_t natural = *cmd;
        const bool nat = run_chain(&natural, chain) > 0;
        restore_original();
        cmd->viewangles  = original_view;
        cmd->forwardmove = original_forward;
        cmd->sidemove    = original_side;
        cmd->buttons     = original_buttons;
        if (nat) {
            s_tb.hb_hit = true;
            log_result("natural");
            return;
        }
    }

    if (plan_live) {
        restore_original();
        write_plan();
        const int r = run_chain(cmd, need_ticks > 0 ? chain : chain_u);
        if (r > 0) {
            restore_original();
            cmd->viewangles = original_view;
            s_tb.hb_hit = true;
            set_plan(r - 1, p_yaw, p_fwd, p_unduck, p_own);
            log_result("keep");
            return;
        }
        if (r < 0) {
            keep_blind();
            return;
        }
    }

    const float base_yaw = (xy_base > 1.f) ? RAD2DEGF(atan2f(base_vel.y, base_vel.x)) : original_view.y;
    static constexpr float k_hb_yaw[]       = { 0.f, 15.f, -15.f, 30.f, -30.f, 60.f, -60.f, 90.f, -90.f, 120.f, -120.f, 150.f, -150.f, 180.f };
    static constexpr float k_hb_yaw_brake[] = { 180.f, 150.f, -150.f, 120.f, -120.f, 90.f, -90.f, 60.f, -60.f, 30.f, -30.f, 15.f, -15.f, 0.f };
    static_assert(std::size(k_hb_yaw) == std::size(k_hb_yaw_brake));
    const float* yaws  = brake ? k_hb_yaw_brake : k_hb_yaw;
    const int    n_yaws = (int)std::size(k_hb_yaw);
    static constexpr float k_hb_fwd[] = { 450.f, 60.f, 15.f };

    float side_yaws[std::size(k_hb_yaw)];
    if (side_hit != 0) {
        std::copy(std::begin(k_hb_yaw), std::end(k_hb_yaw), side_yaws);
        const float aim = 90.f * (float)side_hit;
        std::stable_sort(std::begin(side_yaws), std::end(side_yaws), [aim](float a, float b) {
            return fabsf(NormalizeAngle(a - aim)) < fabsf(NormalizeAngle(b - aim));
        });
        yaws = side_yaws;
    }

    float speed_yaws[std::size(k_hb_yaw)];
    if (wallstrafe_enabled) {
        std::copy(yaws, yaws + n_yaws, speed_yaws);
        std::stable_sort(std::begin(speed_yaws), std::end(speed_yaws), [&](float a, float b) {
            return (base_vel + AirAccelDelta(base_vel, base_yaw + a, 450.f)).Length2D()
                 > (base_vel + AirAccelDelta(base_vel, base_yaw + b, 450.f)).Length2D();
        });
        yaws = speed_yaws;
    }

    TbDedup_t dedup{}, dedup_u{};
    if (plan_live && !p_own)
        (p_unduck ? dedup_u : dedup).SeenOrAdd(AirAccelDelta(base_vel, p_yaw, p_fwd));
    float tried_yaw = 0.f, tried_fwd = 0.f;
    const auto try_variant = [&](float fwd, float yaw_off, int n, int buttons, TbDedup_t& pool) -> int {
        if (sims_used >= budget)
            return 0;
        const float world_yaw = NormalizeAngle(base_yaw + yaw_off);
        const Vec3  delta     = AirAccelDelta(base_vel, world_yaw, fwd);
        if (delta.Length2D() < k_tbDupVelEps)
            return 0;
        if (pool.SeenOrAdd(delta))
            return 0;
        tried_yaw = world_yaw;
        tried_fwd = fwd;
        restore_original();
        cmd->viewangles  = original_view;
        cmd->buttons     = buttons;
        cmd->forwardmove = fwd;
        cmd->sidemove    = 0.f;
        Vec3 wish = original_view;
        wish.y    = world_yaw;
        CorrectMovement(cmd, wish, original_view);
        cmd->viewangles = original_view;
        return run_chain(cmd, n);
    };
    const auto commit = [&](const char* how, int hit_tick, bool unduck, bool own) {
        restore_original();
        cmd->viewangles = original_view;
        s_tb.hb_hit = true;
        set_plan(hit_tick - 1, tried_yaw, tried_fwd, unduck, own);
        log_result(how);
    };
    const auto refine_done = [&]() { return hb_best.have && hb_best.refine <= 0; };
    const auto keep_best = [&](int r, int sims_before) {
        if (hb_best.have && sims_used > sims_before)
            --hb_best.refine;
        if (r <= 0)
            return;
        ++dbg_bounces;
        const float xy = EntV3(pLocal, g_offVelocity).Length2D();
        if (hb_best.have && xy <= hb_best.xy + 0.5f)
            return;
        hb_best.refine  = hb_best.have ? hb_best.refine : k_hb_refine;
        hb_best.have    = true;
        hb_best.tick    = r;
        hb_best.buttons = cmd->buttons;
        hb_best.fwd     = cmd->forwardmove;
        hb_best.side    = cmd->sidemove;
        hb_best.yaw     = tried_yaw;
        hb_best.wish    = tried_fwd;
        hb_best.xy      = xy;
    };
    const auto take_best = [&]() {
        cmd->forwardmove = hb_best.fwd;
        cmd->sidemove    = hb_best.side;
        cmd->buttons     = hb_best.buttons;
        tried_yaw        = hb_best.yaw;
        tried_fwd        = hb_best.wish;
    };

    if (need_ticks > 0) {
        if (wallstrafe_enabled && xy_base > 1.f) {
            restore_original();
            const float acceleration = CvarFloat("sv_airaccelerate", 10.f) * dt * AsSurfaceFriction(pLocal);
            const float crop  = ((s_prePredFlags & FL_DUCKING) || (original_buttons & IN_DUCK)) ? k_duck_crop : 1.f;
            const float mspd  = g_offMaxspeed > 0 ? EntF(pLocal, g_offMaxspeed) : 450.f;
            const float speed = (std::min)(450.f * crop, (std::max)(0.f, mspd));
            const auto  wishes = ws::Candidates(0.f, xy_base, speed, acceleration);
            // Both lateral directions can reach a ledge. Try true acceleration angles before
            // the coarse legacy sweep, within its existing budget.
            for (const auto& wish : wishes) {
                if (wish.gain <= 1e-4f)
                    continue;
                const float offset = RAD2DEGF(atan2f(wish.into, wish.along));
                for (const float sign : { 1.f, -1.f }) {
                    if (sims_used >= budget || refine_done() || SimsLeft() <= 0)
                        break;
                    const int before = sims_used;
                    keep_best(try_variant(450.f, sign * offset, chain, original_buttons, dedup), before);
                }
            }
        }
        for (float fwd : k_hb_fwd) {
            for (int y = 0; y < n_yaws; ++y) {
                if (sims_used >= budget || refine_done())
                    break;
                const int before = sims_used;
                keep_best(try_variant(fwd, yaws[y], chain, original_buttons, dedup), before);
            }
        }
        if (hb_best.have) {
            take_best();
            commit("sweep", hb_best.tick, false, false);
            return;
        }
    }

    if (unduck_need > 0 && sims_used < budget) {
        const int unduck_buttons = original_buttons & ~IN_DUCK;
        restore_original();
        cmd->viewangles  = original_view;
        cmd->forwardmove = original_forward;
        cmd->sidemove    = original_side;
        cmd->buttons     = unduck_buttons;
        const int r_own  = run_chain(cmd, chain_u);
        if (r_own > 0) {
            commit("unduck", r_own, true, true);
            return;
        }
        for (float fwd : k_hb_fwd) {
            for (int y = 0; y < n_yaws; ++y) {
                if (sims_used >= budget || refine_done())
                    break;
                const int before = sims_used;
                keep_best(try_variant(fwd, k_hb_yaw[y], chain_u, unduck_buttons, dedup_u), before);
            }
        }
        if (hb_best.have) {
            take_best();
            commit("unduck", hb_best.tick, true, false);
            return;
        }
    }

    restore_original();
    cmd->viewangles  = original_view;
    cmd->forwardmove = original_forward;
    cmd->sidemove    = original_side;
    cmd->buttons     = original_buttons;
    log_result("miss");
}

// grounded half: aims the run at the face, holds jump until the arc meets the target height.
// texture_bug never sees a grounded tick
static void AutoAlignImpl(void* pLocal, UserCmd_t* cmd, const bool tb_on)
{
    constexpr float k_align_min_speed = 60.f;
    constexpr float k_align_face_dot  = 0.75f;
    constexpr float k_align_aimed_dot = 0.985f;
    constexpr int   k_align_hold_max  = 24;
    constexpr float k_align_min_dist  = 2.f;
    constexpr float k_align_scan_cap  = 192.f;
    constexpr float k_align_step_up   = 18.f;
    constexpr float k_align_band_z    = 36.f;
    constexpr int   k_align_min_ground = 5;
    constexpr int   k_align_air_look  = 24;
    constexpr float k_align_air_min   = 16.f;
    constexpr int   k_align_face_mem  = 32;

    static int  hold_run     = 0;
    static int  want_tick    = -1000;
    static int  ground_ticks = 0;
    static Vec3 face_normal{};
    static int  face_tick        = -1000;
    static int  ground_tick_last = -1000;
    static int  hop_period       = 0;

    const bool dbg      = g_debugLogEnable;
    const int  tick_now = g_globalVars ? g_globalVars->tickcount : 0;

    const bool on_ground = (s_prePredFlags & FL_ONGROUND) != 0;
    ground_ticks = on_ground ? ground_ticks + 1 : 0;
    if (on_ground) {
        const int gap = tick_now - ground_tick_last;
        if (ground_tick_last > -1000 && gap > 1 && gap <= Ticks(64))
            hop_period = gap;
        ground_tick_last = tick_now;
    }

    static int last_bail_id   = -1;
    static int last_bail_tick = -1000;
    auto bail = [&](const int id, const char* why, const float a = 0.f, const float b = 0.f) {
        if (hold_run > 0 && on_ground)
            cmd->buttons |= IN_JUMP;
        hold_run  = 0;
        want_tick = -1000;
        if (dbg && (id != last_bail_id || tick_now - last_bail_tick >= 64)) {
            last_bail_id   = id;
            last_bail_tick = tick_now;
            DebugLog("TB ALIGN: bail=%s a=%.2f b=%.2f gt=%d", why, a, b, ground_ticks);
        }
    };

    if (!tb_on) {
        bail(0, "off");
        return;
    }
    if (JumpTrickKey()) {
        bail(18, "trick_key");
        return;
    }
    if (!EntAlive(pLocal) || EntMoveType(pLocal) != MOVETYPE_WALK) {
        bail(1, "movetype");
        return;
    }

    if (s_rawButtons & IN_JUMP)
        want_tick = tick_now;
    const bool wants_jump = (tick_now - want_tick) <= Ticks(k_align_hold_max);

    const bool may_hold = on_ground && ground_ticks >= Ticks(k_align_min_ground);

    if (on_ground && !wants_jump) {
        hold_run = 0;
        if (dbg && (last_bail_id != 4 || tick_now - last_bail_tick >= 64)) {
            last_bail_id   = 4;
            last_bail_tick = tick_now;
            DebugLog("TB ALIGN: bail=no_ask a=0.00 b=0.00 gt=%d", ground_ticks);
        }
        return;
    }
    const bool wc_key   = On(g_wallClimb, g_wallClimbKey, g_wallClimbMode);
    const int  wc_since = tick_now - s_wc_act_tick;
    if (wc_key && wc_since >= 0 && wc_since <= Ticks(8)) {
        bail(5, "wc_owns");
        return;
    }

    Vec3 vel = EntV3(pLocal, g_offVelocity);
    vel.z = 0.f;
    const float speed = vel.Length2D();
    if (speed < k_align_min_speed) {
        bail(6, "slow", speed);
        return;
    }
    const Vec3  dir(vel.x / speed, vel.y / speed, 0.f);
    const float ipt  = TickInterval();
    const Vec3  mins = EntV3(pLocal, g_offMins), maxs = EntV3(pLocal, g_offMaxs);

    if (!on_ground) {
        const float look = fminf(k_align_scan_cap, speed * ipt * (float)Ticks(k_align_air_look) + 16.f);
        const Vec3  air_start = s_prePredOrigin;
        const Vec3  air_end(air_start.x + dir.x * look, air_start.y + dir.y * look, air_start.z);
        GameTrace_t air_tr{};
        Hull(pLocal, air_start, air_end, mins, maxs, air_tr);

        const Vec3 an = air_tr.plane.normal;
        if (air_tr.startsolid || air_tr.allsolid || air_tr.fraction >= 1.f || fabsf(an.z) >= 0.1f) {
            bail(13, "air_no_wall", look, an.z);
            return;
        }
        face_normal = an;
        face_tick   = tick_now;

        const float air_dist = air_tr.fraction * look;
        const float air_into = -(dir.x * an.x + dir.y * an.y);
        if (air_into < k_align_face_dot) {
            bail(9, "past_it", air_into);
            return;
        }
        if (air_dist <= k_align_air_min) {
            bail(14, "air_close", air_dist);
            return;
        }
        // g_air_stuck_owned_prev_tick: s_airStuckHit still holds last cmd's (AS runs after tb)
        if (s_tb_owned_prev || s_airStuckHit) {
            bail(15, "air_busy", air_dist);
            return;
        }
        if (SteeringAwayFromWall(cmd, an, cmd->forwardmove, cmd->sidemove)) {
            bail(10, "steer_away");
            return;
        }
        if (air_into < k_align_aimed_dot)
            SteerInto(cmd, an);
        if (dbg) {
            last_bail_id = -1;
            DebugLog("TB ALIGN AIR: d=%.1f into=%.2f look=%.0f vz=%.1f aim=%d", air_dist, air_into, look,
                     EntV3(pLocal, g_offVelocity).z, (int)(air_into < k_align_aimed_dot));
        }
        return;
    }

    // TF2: 289 flat (tf_gamemovement.cpp:1315), no stamina; botox reads sv_jump_impulse * stamina
    const float jump_vz = 289.f;
    const float gravity = CvarFloat("sv_gravity", 800.f) > 1.f ? CvarFloat("sv_gravity", 800.f) : 800.f;
    const float apex_h  = jump_vz * jump_vz / (2.f * gravity);
    const float want_h  = fmaxf(1.f, (jump_vz - gravity * ipt * 0.5f) * ipt);
    const float root    = sqrtf(fmaxf(0.f, jump_vz * jump_vz - 2.f * gravity * want_h));
    const float t_want  = (jump_vz - root) / gravity;

    const float max_speed = g_offMaxspeed > 0 ? EntF(pLocal, g_offMaxspeed) : 450.f;
    const float crop      = (max_speed > 1.f && speed > 1.1f * max_speed) ? (1.1f * max_speed / speed) : 1.f;
    const int   hold_max  = Ticks(k_align_hold_max);
    const float along     = speed * crop;
    const float scan      = fminf(k_align_scan_cap, along * t_want + along * ipt * (float)hold_max + 16.f);

    const Vec3 start_pos = s_prePredOrigin;
    const Vec3 end_pos(start_pos.x + dir.x * scan, start_pos.y + dir.y * scan, start_pos.z);
    Vec3 sweep_mins = mins;
    sweep_mins.z += k_align_step_up;
    GameTrace_t tr{};
    Hull(pLocal, start_pos, end_pos, sweep_mins, maxs, tr);

    const Vec3 n = tr.plane.normal;
    if (tr.startsolid || tr.allsolid || tr.fraction >= 1.f || fabsf(n.z) >= 0.1f) {
        bail(8, "no_wall", tr.fraction * scan, n.z);
        if ((tick_now - face_tick) <= Ticks(k_align_face_mem) && face_normal.Length2D() > 0.5f) {
            const float mem_into = -(dir.x * face_normal.x + dir.y * face_normal.y);
            if (mem_into >= k_align_face_dot && mem_into < k_align_aimed_dot
                && !SteeringAwayFromWall(cmd, face_normal, cmd->forwardmove, cmd->sidemove)) {
                SteerInto(cmd, face_normal);
                if (dbg)
                    DebugLog("TB ALIGN: mem=1 into=%.2f age=%d gt=%d", mem_into, tick_now - face_tick, ground_ticks);
            }
        }
        return;
    }
    face_normal = n;
    face_tick   = tick_now;
    const float into = -(dir.x * n.x + dir.y * n.y);
    if (into < k_align_face_dot) {
        bail(9, "past_it", into);
        return;
    }
    if (SteeringAwayFromWall(cmd, n, cmd->forwardmove, cmd->sidemove)) {
        bail(10, "steer_away");
        return;
    }
    const float dist   = tr.fraction * scan;
    const float d_want = along * t_want;
    const float travel = fmaxf(1.f, along * ipt);

    const float need = (dist - d_want) / travel;
    if (need > (float)hold_max) {
        hold_run = 0;
        if (dbg && (last_bail_id != 11 || tick_now - last_bail_tick >= 64)) {
            last_bail_id   = 11;
            last_bail_tick = tick_now;
            DebugLog("TB ALIGN: bail=too_far a=%.1f b=%.1f gt=%d", dist, need, ground_ticks);
        }
        return;
    }

    /* face must still exist at contact height, else it's a ledge we land on. one ray, mid-hull, arc height */
    GameTrace_t band{};
    {
        TraceFilterWorldProps fil; fil.Setup(pLocal);
        const Vec3 band_start(start_pos.x, start_pos.y, start_pos.z + want_h + k_align_band_z);
        const Vec3 band_end(band_start.x + dir.x * (dist + 32.f), band_start.y + dir.y * (dist + 32.f), band_start.z);
        TraceLine(band_start, band_end, MASK_PLAYERSOLID, &fil, &band);
    }
    if (band.fraction >= 1.f || fabsf(band.plane.normal.z) >= 0.1f) {
        bail(12, "ledge", want_h + k_align_band_z, band.fraction);
        return;
    }

    /* hull already on the face: hand the cmd back WITH the latched ask (else an eaten tap vanishes).
       threshold must stay under the comparator's press window (~1.5 travel) or it swallows it. */
    if (dist < fmaxf(k_align_min_dist, travel * 0.5f)) {
        cmd->buttons |= IN_JUMP;
        hold_run  = 0;
        want_tick = -1000;
        if (dbg)
            DebugLog("TB ALIGN: spend=on_face d=%.1f into=%.2f gt=%d", dist, into, ground_ticks);
        return;
    }

    const int   step_ticks = may_hold ? 1 : (std::max)(1, hop_period);
    const float err_now    = fabsf(dist - d_want);
    const float err_next   = fabsf(dist - travel * (float)step_ticks - d_want);
    bool hold = false;
    if (err_next < err_now && hold_run < hold_max && may_hold) {
        cmd->buttons &= ~IN_JUMP;
        hold = true;
        ++hold_run;
    } else if (err_next < err_now) {
        hold_run = 0;
    } else {
        cmd->buttons |= IN_JUMP;
        want_tick = -1000;
        hold_run  = 0;
    }

    if (into < k_align_aimed_dot)
        SteerInto(cmd, n);

    if (dbg) {
        last_bail_id = -1;
        DebugLog("TB ALIGN: d=%.1f want=%.1f need=%.1f close=%.0f into=%.2f h=%.1f apex=%.1f vz0=%.0f "
                 "scan=%.0f k=%.1f gt=%d mh=%d hold=%d run=%d hp=%d step=%.1f",
                 dist, d_want, need, along, into, want_h, apex_h, jump_vz, scan,
                 ipt > 0.f ? t_want / ipt : 0.f, ground_ticks, (int)may_hold, (int)hold, hold_run,
                 hop_period, travel * (float)step_ticks);
    }
}

// texturebug.cpp on_create_move
static void OnCreateMove(void* pLocal, UserCmd_t* cmd, const bool tb_on, const bool tung_on)
{
    // last cmd's wall climb catch is still up here (WC runs after tb): botox m_act_tick
    // (landed || jump fix). Not s_wallClimbActive: that is every air wall tick
    if (s_wallClimbHit)
        s_wc_act_tick = (g_globalVars ? g_globalVars->tickcount : 0) - 1;
    s_tb_owned_prev = s_tb.hit || s_tb.hs_hit || s_tb.hb_hit || s_tb.acted || s_tb.owns_cmd;
    s_tb.acted  = false;
    s_tb.assist = false;
    s_tb.reset_command();
    s_tb.hb_hit = false;
    s_sims = 0;
    s_restores = 0;
    s_budget_live = true;
    s_pin = TargetPredictZVel();
    s_reserve = On(g_airStuck, g_airStuckKey, g_airStuckMode) ? k_as_sim_reserve : 0;

    if (!cmd)
        return;

    if ((s_prePredFlags & FL_ONGROUND) && JumpTrickKey()) {
        s_tb.hit = s_tb.hs_hit = s_tb.owns_cmd = false;
        s_budget_live = false;
        return;
    }

    const float pre_forward = cmd->forwardmove;
    const float pre_side    = cmd->sidemove;
    const int   pre_buttons = cmd->buttons;
    s_tb.input_forward = pre_forward;
    s_tb.input_side    = pre_side;
    s_tb.input_yaw     = cmd->viewangles.y;

    AutoAlignImpl(pLocal, cmd, tb_on);
    const float align_forward = cmd->forwardmove;
    const float align_side    = cmd->sidemove;
    const int   align_buttons = cmd->buttons;
    TextureBugImpl(pLocal, cmd, tb_on, tung_on);
    HeadBounceImpl(pLocal, cmd, tb_on);
    const bool caught = s_tb.hit || s_tb.hb_hit;

    const long long used_us = TbBudgetUsedUs();
    const bool was_time_bound = SimsLeft() <= 0;
    s_budget_live = false;
    if (g_debugLogEnable && (tb_on || tung_on)) {
        static long long worst_us = 0ll, sum_us = 0ll;
        static int rolls = 0, worst_sims = 0, worst_tr = 0, capped = 0, last_sec = -1, res_sum = 0;
        res_sum += s_restores;
        worst_us   = (std::max)(worst_us, used_us);
        worst_sims = (std::max)(worst_sims, s_sims);
        worst_tr   = (std::max)(worst_tr, s_tracesThisCmd);
        capped += was_time_bound;
        sum_us += used_us;
        ++rolls;
        const int sec = g_globalVars ? (int)g_globalVars->curtime : 0;
        if (sec != last_sec) {
            last_sec = sec;
            DebugLog("TBP: worst=%lldus avg=%lldus sims_worst=%d tr_worst=%d capped=%d/%d res=%d",
                     worst_us, rolls > 0 ? sum_us / rolls : 0ll, worst_sims, worst_tr, capped, rolls, res_sum);
            worst_us = sum_us = 0ll;
            rolls = worst_sims = worst_tr = capped = res_sum = 0;
        }
    }

    s_tb.resimmed = false;
    s_sent_cmd = -1;
    if (s_sims > 0 || s_restores > 0) {
        Restore(PredCommandsPredicted() - 1);
        Simulate(pLocal, cmd);
        s_tb.resimmed = true;
        s_sent_cmd = cmd->command_number;
        s_sent_org = EntV3(pLocal, g_offOrigin);
        s_sent_vel = EntV3(pLocal, g_offVelocity);
    }

    s_tb.acted = cmd->forwardmove != pre_forward || cmd->sidemove != pre_side || cmd->buttons != pre_buttons;
    /* Only auto-align's run-up steer is an assist. Proven wallstrafe owns movement. */
    s_tb.assist = s_tb.acted && !s_tb.hit && !s_tb.hb_hit && !s_tb.owns_cmd
               && cmd->forwardmove == align_forward && cmd->sidemove == align_side && cmd->buttons == align_buttons;
    if (g_tbWallstrafe && (caught || s_tb.owns_cmd))
        s_tb.owned_move.capture(*cmd);
}

static void Off()
{
    s_tb = state_t{};
    s_tbSyncWant = false;
    s_sent_cmd = -1;
}
} // namespace tbx

// texturebug.cpp preserve_move: re-apply the owned wallstrafe move after everything later in
// the cmd (edge bug post, ...) touched it. Called at the end of Movement_RunImpl
static void TB_PreserveMove(void* pLocal, UserCmd_t* pCmd)
{
    const bool tb   = On(g_textureBug, g_textureBugKey, g_textureBugMode);
    const bool tung = !tb && On(g_tungSurf, g_tungSurfKey, g_tungSurfMode);   // tung rides tbx too
    if (!pCmd || !EntAlive(pLocal) || !g_tbWallstrafe || !(tung || (tb && g_tbStyle == 1)))
        return;
    if (Recorder_OwnsCmd() || On(g_airStuck, g_airStuckKey, g_airStuckMode) || s_wallClimbHit) {
        tbx::s_tb.reset_command();
        return;
    }
    constexpr int IN_RUN_MV = 1 << 12, IN_SPEED_MV = 1 << 17, IN_WALK_MV = 1 << 18, IN_BULLRUSH_MV = 1 << 22;
    constexpr int movement_mask = IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT | IN_JUMP | IN_DUCK
                                | IN_RUN_MV | IN_SPEED_MV | IN_WALK_MV | IN_BULLRUSH_MV;
    tbx::s_tb.owned_move.apply(*pCmd, movement_mask);
}

// ============================================================================
// THE ENTRY. TB bind: cumidere style (0) or botox style (1). Tung surf = the botox texture_bug
// with tung's widened vertical gate (no cumidere tung). Publishes the movement.cpp yield flags
// every other feature reads.
// ============================================================================
static void TextureBug(void* pLocal, UserCmd_t* pCmd)
{
    const bool bTbOn   = On(g_textureBug, g_textureBugKey, g_textureBugMode);
    const bool bTungOn = !bTbOn && On(g_tungSurf, g_tungSurfKey, g_tungSurfMode);
    if (bTbOn && g_tbStyle == 0)
    {
        tbx::Off();
        TextureBugCumidere(pLocal, pCmd);
        return;
    }

    s_textureBugHit = false;
    s_tbOwnsCmd     = false;
    tbx::OnCreateMove(pLocal, pCmd, bTbOn, bTungOn);
    const auto& t = tbx::s_tb;
    s_textureBugHit = t.hit;
    s_tbOwnsCmd     = t.owns_cmd || (t.acted && !t.assist && !t.hit);
    s_headSurfHit   = t.hs_hit;
    s_headBounceHit = t.hb_hit;
    s_tbRideHold    = (t.hit || t.owns_cmd) ? 1 : 0;   // wallclimb.inl yields while the tb ride runs
    s_tbIndD = s_tbIndVin = s_tbIndAim = 0.f;
    s_tbRideLen = t.hit ? (std::max)(t.run, 1) : 0;
    s_tbPhase   = t.hit ? (s_tbRideLen >= 2 ? TBP_RIDE : TBP_PIN) : t.owns_cmd ? TBP_HUG
                : (bTbOn || bTungOn) ? TBP_SCAN : TBP_OFF;
}

// ============================================================================
// HEAD SURF -- coldtires head_surf(), ported whole
// ----------------------------------------------------------------------------
// The mirror of the wall pin: a flat brush BOTTOM above the head clips a rise and the engine
// holds real vz at +halfgravity while the surf lasts. No own toggle -- the TB bind drives it,
// it runs right after TextureBug and may TAKE OVER a TB-owned tick (a ceiling ride outranks a
// wall pin), which is the "stuck -> headsurf" transition.
// The +halfgrav accept is CORRECT, do not "fix" it: head_surf accepts +half, head_bounce
// lands on -half. Opposite on purpose
// ============================================================================
static void HeadSurf(void* pLocal, UserCmd_t* pCmd)
{
    static bool  s_hs_locked = false;
    static float s_hs_cached_forward = -1.f;
    static float s_hs_cached_angle_offset = 0.f;
    static bool  s_hs_cached_duck = false;
    static float s_hs_locked_forward = 0.f, s_hs_locked_side = 0.f;
    static bool  s_hs_locked_duck = false;
    static bool  s_hs_surfing_now = false;
    static bool  s_hs_prev_surf_vel = false;
    static int   s_hs_cold_skip = 0;
    static int   s_hs_surf_ticks = 0;     // consecutive hit ticks -- the coast gate
    static int   s_hs_coast_left = 0;

    s_hs_surf_ticks = s_headSurfHit ? s_hs_surf_ticks + 1 : 0;
    s_headSurfHit = false;

    if (!On(g_textureBug, g_textureBugKey, g_textureBugMode) || !pCmd || !EntAlive(pLocal))
        { s_hs_locked = false; s_hs_cached_forward = -1.f; s_hs_coast_left = 0; return; }

    // TB caught a wall surf this tick. Do NOT hard-return: if a ceiling is in reach and the pin
    // window is open, HS takes over. A miss restores TB's move untouched
    const bool tb_owned = s_textureBugHit;
    const bool debug    = g_debugLogEnable;
    const float half_grav = TB_HalfGravityPerTick();

    if (EntI(pLocal, g_offFlags) & FL_ONGROUND)
        { s_hs_cached_forward = -1.f; s_hs_surfing_now = false; s_hs_locked = false; return; }

    const int move_type = EntMoveType(pLocal);
    if (move_type == MOVETYPE_NOCLIP
        || move_type == MOVETYPE_FLY || move_type == MOVETYPE_OBSERVER)
        return;

    const Vec3 start_pos = EntV3(pLocal, g_offOrigin);
    const Vec3 mins = EntV3(pLocal, g_offMins);
    const Vec3 maxs = EntV3(pLocal, g_offMaxs);
    // cold reach scales with rise speed: a fast rise crosses the catch window in 1-2 ticks
    const float vz_now = EntV3(pLocal, g_offVelocity).z;
    const float reach  = s_hs_surfing_now
        ? 12.f : fminf(8.f + fmaxf(vz_now, 0.f) * TickInterval() * 3.f, 30.f);

    GameTrace_t ceil_trace{};
    // 119th -- the same ramp rule as TextureBug, ceiling side. The base gate nz < -0.7 also
    // admits the UNDERSIDE of a 45 deg surf ramp, and grabbing one locks the cmd and eats the
    // strafe like the wall pin did. A head-surf brush bottom is flat (nz -1.00)
    const bool ceiling_ok = TB_DetectCeiling(pLocal, start_pos, mins, maxs, reach, ceil_trace)
                         && ceil_trace.plane.normal.z <= -(1.f - k_hsRampNz);
    if (debug && DiagOk(s_hsDiagTick, ceiling_ok))
        DebugLog("HS: ceil=%d nz=%.3f frac=%.3f reach=%.1f vz=%.1f gate=%d tbown=%d surft=%d "
                 "lock=%d coast=%d cold=%d",
                 (int)ceiling_ok, ceil_trace.plane.normal.z, ceil_trace.fraction, reach, vz_now,
                 (int)(vz_now > 3.f * half_grav), (int)tb_owned, s_hs_surf_ticks,
                 (int)s_hs_locked, s_hs_coast_left, s_hs_cold_skip);

    if (!ceiling_ok)
    {
        if (tb_owned) { s_hs_locked = false; return; }   // wall surf stands, nothing overhead
        // ceiling seam: an established surf coasts on the locked move -- scan gaps between
        // flush brushes dropped real surfs
        if (s_hs_surf_ticks >= 2 && s_hs_coast_left > 0)
        {
            --s_hs_coast_left;
            pCmd->forwardmove = s_hs_locked_forward;
            pCmd->sidemove    = s_hs_locked_side;
            if (s_hs_locked_duck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
            s_headSurfHit = true;
            return;
        }
        s_hs_cached_forward = -1.f;
        s_hs_surfing_now = false;
        s_hs_locked = false;
        s_hs_cold_skip = 0;
        return;
    }

    {
        const float vz = EntV3(pLocal, g_offVelocity).z;
        const bool surf_vel = TB_IsPositiveHalfGravity(vz, half_grav, 1.5f);
        s_hs_surfing_now   = surf_vel && s_hs_prev_surf_vel;
        s_hs_prev_surf_vel = surf_vel;
    }

    // a pin needs upward motion into the ceiling (or an active surf): vz below 3x halfgrav is
    // falling away and cannot pin, so skip the sweep budget on those ticks
    if (!s_hs_surfing_now && vz_now <= 3.f * half_grav)
    {
        if (!tb_owned) { s_hs_cached_forward = -1.f; s_hs_locked = false; }
        return;
    }

    const Vec3  original_view    = pCmd->viewangles;
    const float original_forward = pCmd->forwardmove;
    const float original_side    = pCmd->sidemove;
    const int   original_buttons = pCmd->buttons;

    // locked: the engine pins real vz to +halfgravity while the surf holds -- replay verbatim
    if (s_hs_locked)
    {
        if (TB_IsPositiveHalfGravity(EntV3(pLocal, g_offVelocity).z, half_grav, 3.f))
        {
            pCmd->viewangles  = original_view;
            pCmd->forwardmove = s_hs_locked_forward;
            pCmd->sidemove    = s_hs_locked_side;
            if (s_hs_locked_duck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
            s_headSurfHit   = true;
            s_textureBugHit = false;      // HS owns the tick
            s_hs_surfing_now = true;
            s_hs_coast_left  = 3;
            return;
        }
        s_hs_locked = false;    // slipped -- re-catch this tick
        s_hs_cold_skip = 0;     // never let a stale cooldown block the re-catch sweep
    }

    Vec3 ceil_angle = original_view;
    {
        const Vec3 n_xy(ceil_trace.plane.normal.x, ceil_trace.plane.normal.y, 0.f);
        if (n_xy.Length2D() > 0.05f)
        {
            Vec3 into_c = n_xy * -1.f;
            ceil_angle = VectorAngles(into_c);
            ClampAnglesMv(ceil_angle);
        }
    }

    const int nBasePredicted = PredCommandsPredicted();
    const auto RestoreOriginal = [&]() { RestoreToFrame(nBasePredicted - 1); };

    // shared pool slice: a full hunt when free, a capped probe when TB already owns the tick
    int budget = SimsLeft();
    if (budget > 160) budget = 160;
    if (tb_owned && budget > 60) budget = 60;
    if (budget < 0) budget = 0;
    int sims_used = 0;

    const auto try_config = [&](float forward, float angle_offset, bool duck) -> bool
    {
        if (sims_used >= budget) return false;
        ++sims_used;
        RestoreOriginal();
        Vec3 wish_angle = original_view;
        wish_angle.y = ceil_angle.y + angle_offset;
        pCmd->viewangles = original_view;
        pCmd->buttons    = original_buttons;
        if (duck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
        pCmd->forwardmove = forward;
        pCmd->sidemove    = 0.f;
        CorrectMovement(pCmd, wish_angle, original_view);
        Pred_Simulate(pLocal, pCmd);
        return TB_IsPositiveHalfGravity(EntV3(pLocal, g_offVelocity).z, half_grav, 0.5f);
    };

    const auto commit = [&](float cfg_forward, float cfg_offset, bool duck)
    {
        RestoreOriginal();     // pCmd already holds the winning silent move
        pCmd->viewangles = original_view;
        s_hs_cached_forward      = cfg_forward;
        s_hs_cached_angle_offset = cfg_offset;
        s_hs_cached_duck         = duck;
        s_hs_locked         = true;
        s_hs_locked_forward = pCmd->forwardmove;
        s_hs_locked_side    = pCmd->sidemove;
        s_hs_locked_duck    = duck;
        s_headSurfHit    = true;
        s_textureBugHit  = false;    // takeover: HS owns the tick now
        s_hs_surfing_now = true;
        s_hs_cold_skip   = 0;
        s_hs_coast_left  = 3;
        if (debug)
            DebugLog("HS LOCK: fwd=%.2f ang=%.1f duck=%d sims=%d/%d vz=%.1f",
                     cfg_forward, cfg_offset, (int)duck, sims_used, budget,
                     EntV3(pLocal, g_offVelocity).z);
    };

    // does the pin sustain N more sim ticks? (pCmd still holds the tested move after try_config)
    const auto sustain_ticks = [&](int n_max) -> int
    {
        int n = 0;
        for (; n < n_max && sims_used < budget; ++n)
        {
            ++sims_used;
            Pred_Simulate(pLocal, pCmd);
            if (!TB_IsPositiveHalfGravity(EntV3(pLocal, g_offVelocity).z, half_grav, 0.5f)) break;
        }
        return n;
    };

    // natural pass: the user's own cmd already headsurf-pins -> keep it (manual play wins).
    // INITIAL catch only. On a TB-owned tick pCmd holds TB's wall move, not the user's
    if (!tb_owned && s_hs_surf_ticks == 0
        && (fabsf(original_forward) > 0.01f || fabsf(original_side) > 0.01f) && budget > 0)
    {
        RestoreOriginal();
        UserCmd_t natural = *pCmd;
        ++sims_used;
        Pred_Simulate(pLocal, &natural);
        const bool nat_pin = TB_IsPositiveHalfGravity(EntV3(pLocal, g_offVelocity).z, half_grav, 0.5f);
        RestoreOriginal();
        if (nat_pin)
        {
            pCmd->viewangles  = original_view;
            pCmd->forwardmove = original_forward;
            pCmd->sidemove    = original_side;
            pCmd->buttons     = original_buttons;
            s_headSurfHit = true;
            s_hs_surfing_now = true;
            s_hs_locked = false;      // the user owns it -- no verbatim replay of user input
            s_hs_coast_left = 3;
            if (debug) DebugLog("HS: natural pin vz=%.1f", EntV3(pLocal, g_offVelocity).z);
            return;
        }
    }

    // fast path: last tick's winning config (1 sim)
    if (s_hs_cached_forward >= 0.f
        && try_config(s_hs_cached_forward, s_hs_cached_angle_offset, s_hs_cached_duck))
        { commit(s_hs_cached_forward, s_hs_cached_angle_offset, s_hs_cached_duck); return; }

    // cold skip only while falling -- rising = the catch window is 1-2 ticks out, and skipping
    // ate catches
    if (s_hs_cold_skip > 0 && vz_now <= 0.f)
    {
        --s_hs_cold_skip;
        RestoreOriginal();
        pCmd->viewangles  = original_view;
        pCmd->forwardmove = original_forward;
        pCmd->sidemove    = original_side;
        pCmd->buttons     = original_buttons;
        return;
    }

    const Vec3  vel_hs = EntV3(pLocal, g_offVelocity);
    const float vel_yaw_hs = RAD2DEGF(atan2f(vel_hs.y, vel_hs.x));
    const float rel_yaw_hs = NormalizeAngle(vel_yaw_hs - ceil_angle.y);
    const float hs_sign    = (rel_yaw_hs >= 0.f) ? -1.f : 1.f;

    static constexpr float k_hs_fwd[] =
        { 0.f, 0.1f, 0.25f, 0.5f, 0.75f, 1.f, 1.5f, 2.125f, 2.875f, 5.f, 10.f, 20.f, 30.f };
    constexpr float coarse_step = 5.f, fine_step = 1.f;

    float sh_fwd = -1.f, sh_ang = 0.f;
    bool  sh_duck = false;
    int   sh_sustain = -1;
    float sh_move_fwd = 0.f, sh_move_side = 0.f;   // saved silent move -- commit must not need budget

    // mid-surf re-catch tries the CURRENT duck state first (first pin wins)
    const bool first_duck = (s_hs_surf_ticks > 0 || s_hs_locked) ? s_hs_locked_duck : false;
    for (const float fwd : k_hs_fwd)
    {
        if (sims_used >= budget) break;
        for (int i_duck = 0; i_duck <= 1 && sims_used < budget; ++i_duck)
        {
            const bool duck = (i_duck == 0) ? first_duck : !first_duck;
            for (float ang = 0.f; ang < 90.f && sims_used < budget; ang += coarse_step)
            {
                if (!try_config(fwd, ang * hs_sign, duck)) continue;

                // coarse hit -- save its silent move (try_config is deterministic from the
                // pinned restore frame, so no re-sim is needed after a failed refinement)
                const float coarse_fwd  = pCmd->forwardmove;
                const float coarse_side = pCmd->sidemove;

                // durability: the pin must survive 2 more ticks -- 1-tick flukes poison the lock
                // and the fast path (the same lesson as TB); the shallow best stays as a fallback
                const int sus = sustain_ticks(2);
                if (sus < 2)
                {
                    if (sus > sh_sustain)
                    {
                        sh_sustain = sus; sh_fwd = fwd; sh_ang = ang * hs_sign; sh_duck = duck;
                        sh_move_fwd = coarse_fwd; sh_move_side = coarse_side;
                    }
                    continue;
                }

                for (float d = fine_step; d <= coarse_step; d += fine_step)      // refine around it
                    for (int s = 0; s <= 1; ++s)
                    {
                        const float fa = ang + (s ? d : -d);
                        if (fa < 0.f) continue;
                        if (try_config(fwd, fa * hs_sign, duck))
                            { commit(fwd, fa * hs_sign, duck); return; }
                    }

                // refinement found nothing better -- replay the saved coarse move
                pCmd->forwardmove = coarse_fwd;
                pCmd->sidemove    = coarse_side;
                pCmd->buttons     = original_buttons;
                if (duck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
                commit(fwd, ang * hs_sign, duck);
                return;
            }
        }
    }

    // no durable config -> the best shallow pin still beats nothing. NOT on a TB-owned tick:
    // a takeover clears the wall hit, so a 1-tick HS fluke would trade an established wall surf
    // for nothing. Stealing TB's tick demands a DURABLE pin
    if (sh_sustain >= 0 && !tb_owned)
    {
        RestoreOriginal();
        pCmd->viewangles  = original_view;
        pCmd->forwardmove = sh_move_fwd;
        pCmd->sidemove    = sh_move_side;
        pCmd->buttons     = original_buttons;
        if (sh_duck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
        commit(sh_fwd, sh_ang, sh_duck);
        return;
    }

    // established surf, momentary miss -> coast on the locked move (band drift re-pins)
    if (!tb_owned && s_hs_surf_ticks >= 2 && s_hs_coast_left > 0)
    {
        --s_hs_coast_left;
        RestoreOriginal();
        pCmd->viewangles  = original_view;
        pCmd->forwardmove = s_hs_locked_forward;
        pCmd->sidemove    = s_hs_locked_side;
        pCmd->buttons     = original_buttons;
        if (s_hs_locked_duck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
        s_headSurfHit = true;
        return;
    }

    // full miss: hand movement back (on a TB-owned tick the originals ARE TB's committed move,
    // so the wall surf survives the failed takeover untouched), then cool down
    RestoreOriginal();
    pCmd->viewangles  = original_view;
    pCmd->forwardmove = original_forward;
    pCmd->sidemove    = original_side;
    pCmd->buttons     = original_buttons;
    s_hs_cached_forward = -1.f;
    s_hs_cold_skip = 1;     // the catch window is 1-2 ticks; a long cooldown ate headsurfs
    if (debug)
        DebugLog("HS MISS: sus=%d sims=%d/%d vz=%.1f nz=%.2f", sh_sustain, sims_used, budget,
                 vz_now, ceil_trace.plane.normal.z);
}

// ============================================================================
// HEAD BOUNCE -- coldtires head_bounce(), ported whole (2026-08-17 / 123rd)
// ----------------------------------------------------------------------------
// HeadSurf RIDES flat brush ceilings. A notch between two DISPLACEMENTS fails all its gates
// -- both faces are tilted, and one tilted face slides the rise off instead of pinning it.
// A CREASE is different: TryPlayerMove's two-plane branch projects velocity onto the crease
// line, a horizontal crease zeroes vz, and FinishGravity puts back one half step, landing on
// -halfgravity.
// So this is a CATCH only: no plane lock, no coast, no duck latch, no hold, every tick
// re-derived from scratch. A tick that verifies nothing hands the cmd back byte-identical.
// 123rd -- the accept is the CUT off free fall (any size), not the full crease, and the
// chain is COUNTED to the notch instead of fixed at 3
// ============================================================================
static void HeadBounce(void* pLocal, UserCmd_t* pCmd)
{
    s_headBounceHit = false;

    if (!On(g_textureBug, g_textureBugKey, g_textureBugMode)) return;
    if (!pCmd || !EntAlive(pLocal)) return;
    // a wall pin or a ceiling ride already owns the tick -- both outrank a one-tick clip
    if (s_textureBugHit || s_headSurfHit) return;
    if (EntI(pLocal, g_offFlags) & FL_ONGROUND) return;
    if (EntMoveType(pLocal) != MOVETYPE_WALK) return;

    const float half = TargetPredictZVel();   // NEGATIVE
    const float vz_now = EntV3(pLocal, g_offVelocity).z;
    // must be RISING into it -- a falling hull cannot have its rise cut. The one entry speed
    // where gravity alone fakes the bounce is filtered per chain tick instead (step_result)
    if (vz_now <= 0.f) return;

    const Vec3  start_pos = EntV3(pLocal, g_offOrigin);
    const Vec3  mins = EntV3(pLocal, g_offMins);
    const Vec3  maxs = EntV3(pLocal, g_offMaxs);
    const float dt   = TickInterval();
    const bool  debug = g_debugLogEnable;

    // only as far as the chain could rise, plus a nub margin -- anything higher is out of
    // this search's reach and probing for it only costs traces
    const float reach = fminf(4.f + vz_now * dt * k_hbChainMax, 48.f);
    GameTrace_t over{};
    TbOverDbg_t odbg{};
    if (!TB_DetectOverhead(pLocal, start_pos, mins, maxs, reach, over, odbg))
    {
        if (debug && DiagOk(s_hbDiagTick, false))
            DebugLog("HB: none reach=%.1f vz=%.1f frac0=%.3f nz0=%.2f ss=%d retry=%d",
                     reach, vz_now, odbg.frac0, odbg.nz0, (int)odbg.ss0, (int)odbg.retried);
        return;
    }

    const Vec3  original_view    = pCmd->viewangles;
    const float original_forward = pCmd->forwardmove;
    const float original_side    = pCmd->sidemove;
    const int   original_buttons = pCmd->buttons;

    const int nBase = PredCommandsPredicted();
    const auto RestoreOriginal = [&]() { RestoreToFrame(nBase - 1); };

    int budget = SimsLeft();
    if (budget > k_hbSimCap) budget = k_hbSimCap;
    if (budget <= 0) return;
    int sims_used = 0;

    // the tick's own numbers, read off the restore base -- every variant starts here
    RestoreOriginal();
    const Vec3  base_vel = EntV3(pLocal, g_offVelocity);
    const float base_vz  = base_vel.z;
    const float xy_base  = base_vel.Length2D();
    // a bounce that costs half the speed is not a catch, it is a crash into the ceiling
    const float xy_floor = xy_base >= 1.f ? xy_base * k_tbXyCreepFrac : 0.f;
    const float vz_free  = base_vz + 2.f * half;
    const float sv_airaccel = CvarFloat("sv_airaccelerate", 10.f);

    // THE COST GATE. A wish cannot change vz, so whether the head reaches the notch is
    // decided before any sim: the hull rises (vz + half) * dt. DUCK cannot help either -- in
    // air FinishDuck moves the origin up by the hull delta and FinishUnDuck back down, so the
    // hull TOP is invariant and only the feet move. 123rd: the tick count is COUNTED, not
    // assumed (vz_i = base_vz + 2*half*(i-1), exact). Peaking below the notch returns 0
    const float gap = over.fraction * reach;
    const auto hb_ticks_to_gap = [&](float need) -> int
    {
        float climbed = 0.f, vz_i = base_vz;
        for (int i = 1; i <= k_hbChainMax; ++i)
        {
            const float step = (vz_i + half) * dt;
            if (step <= 0.f) return 0;
            climbed += step;
            if (climbed >= need) return i;
            vz_i += 2.f * half;
        }
        return 0;
    };
    const int need_ticks = hb_ticks_to_gap(gap - k_hbGapMargin);
    if (need_ticks <= 0)
    {
        if (debug && DiagOk(s_hbDiagTick, false))
            DebugLog("HB: far gap=%.2f nz=%.2f vz=%.1f reach=%.1f", gap,
                     over.plane.normal.z, base_vz, reach);
        return;
    }
    // one tick past the arrival tick: the contact happens ON the tick the head reaches it, and
    // the tick after is where a graze that only slowed the rise still shows up as a cut
    int chain = need_ticks + 1;
    if (chain < k_hbChain)    chain = k_hbChain;
    if (chain > k_hbChainMax) chain = k_hbChainMax;

    // diag: the BEST any variant did, not the last one tried. cut=0.00 on a miss = nothing
    // touched the notch at all; a cut over the floor with xy under it = a real bounce vetoed
    float best_dz = 1e9f, best_vz = 0.f, best_xy = 0.f, best_cut = 0.f;

    // one simulated tick, judged against the tick it started from. THE TEST IS THE CUT, not
    // the landing value: a wish cannot change vz, so a contact-free tick lands on
    // vz_in + 2*half exactly and anything under that was taken by something overhead
    const auto step_result = [&](float vz_in) -> int
    {
        const Vec3  v = EntV3(pLocal, g_offVelocity);
        const float free_vz = vz_in + 2.f * half;
        const float cut = free_vz - v.z;
        const float dz  = fabsf(v.z - half);
        if (cut > best_cut) { best_cut = cut; best_vz = v.z; best_xy = v.Length2D(); }
        if (dz < best_dz) best_dz = dz;
        // landing eats the rise the same way a ceiling does -- that is a floor, not a bounce
        if (EntI(pLocal, g_offFlags) & FL_ONGROUND) return -1;
        if (cut > k_hbCutMin) return v.Length2D() >= xy_floor ? 1 : -1;
        // still climbing -> the chain may still reach the notch; falling -> it never will
        return v.z > 0.f ? 0 : -1;
    };

    const auto log_result = [&](const char* how)
    {
        if (!debug) return;
        DebugLog("HB %s: nz=%.2f gap=%.2f vz=%.1f->%.1f free=%.1f tgt=%.2f cut=%.2f dz=%.2f "
                 "xy=%.0f->%.0f/%.0f chain=%d/%d sims=%d/%d retry=%d",
                 how, over.plane.normal.z, gap, base_vz, best_vz, vz_free, half, best_cut,
                 best_dz, xy_base, best_xy, xy_floor, need_ticks, chain, sims_used, budget,
                 (int)odbg.retried);
    };

    // NATURAL FIRST -- the player's own cmd already clips the notch, so keep their input and
    // just publish the catch. The same rule HeadSurf uses: manual play wins
    {
        RestoreOriginal();
        UserCmd_t natural = *pCmd;
        bool nat = false;
        for (int i = 0; i < chain && sims_used < budget; ++i)
        {
            const float vz_in = EntV3(pLocal, g_offVelocity).z;
            ++sims_used;
            Pred_Simulate(pLocal, &natural);
            const int r = step_result(vz_in);
            if (r != 0) { nat = (r > 0); break; }
        }
        RestoreOriginal();
        pCmd->viewangles  = original_view;
        pCmd->forwardmove = original_forward;
        pCmd->sidemove    = original_side;
        pCmd->buttons     = original_buttons;
        if (nat) { s_headBounceHit = true; log_result("natural"); return; }
    }

    // wish directions relative to TRAVEL, not to the ceiling normal: on a crease the useful nudge
    // is sideways off the path, and a near-flat underside has no usable xy normal at all
    const float base_yaw = (xy_base > 1.f) ? RAD2DEGF(atan2f(base_vel.y, base_vel.x))
                                           : original_view.y;
    static constexpr float k_hb_yaw[] =
        { 0.f, 15.f, -15.f, 30.f, -30.f, 60.f, -60.f, 90.f, -90.f, 120.f, -120.f, 150.f, -150.f, 180.f };
    // 124th -- AirAccelerate caps only ADDSPEED at 30; accelspeed = accel * wishspeed * dt
    // uses the UNCAPPED wishspeed. So fwd is the whole authority, not a fine-tuning axis:
    // fwd 1 -> 0.19 u/s a tick, fwd 450 -> the entire addspeed in ONE tick. Descending, since
    // ascending order spent every sim on the 0.19 rows
    static constexpr float k_hb_fwd[] = { 450.f, 60.f, 15.f };
    // NO duck variant: an air duck moves the origin and the hull by the same delta, so the
    // head top does not move and the notch stays as far away (see the cost gate). The duck is
    // the edgebug/pixel lever -- it moves the FEET

    TbDedup_t dedup{};
    const auto try_variant = [&](float fwd, float yaw_off) -> bool
    {
        if (sims_used >= budget) return false;
        const float world_yaw = NormalizeAngle(base_yaw + yaw_off);
        const Vec3  delta = TB_AirAccelDelta(base_vel, world_yaw, fwd, sv_airaccel, dt);
        // 124th -- a provable NO-OP used to cost a whole chain: addspeed = 30 - vel.wishdir,
        // so at 198 u/s every yaw inside ~81 deg of travel returns a zero delta
        if (delta.Length2D() < k_tbDupVelEps) return false;
        if (dedup.SeenOrAdd(delta)) return false;
        RestoreOriginal();
        pCmd->viewangles  = original_view;
        pCmd->buttons     = original_buttons;
        pCmd->forwardmove = fwd;
        pCmd->sidemove    = 0.f;
        Vec3 wish = original_view; wish.y = world_yaw;
        CorrectMovement(pCmd, wish, original_view);
        pCmd->viewangles = original_view;
        // hold the SAME cmd for the chain: the view is fixed across the sims, so replaying it
        // is the same world wish every tick (same as HeadSurf's sustain_ticks)
        for (int i = 0; i < chain && sims_used < budget; ++i)
        {
            const float vz_in = EntV3(pLocal, g_offVelocity).z;
            ++sims_used;
            Pred_Simulate(pLocal, pCmd);
            const int r = step_result(vz_in);
            if (r != 0) return r > 0;
        }
        return false;
    };

    for (const float fwd : k_hb_fwd)
        for (const float yaw_off : k_hb_yaw)
        {
            if (sims_used >= budget) break;
            if (!try_variant(fwd, yaw_off)) continue;
            // pCmd already holds the winning silent move -- restore the entity, commit as is
            RestoreOriginal();
            pCmd->viewangles = original_view;
            s_headBounceHit  = true;
            log_result("sweep");
            return;
        }

    // miss: the cmd goes back exactly as the player wrote it
    RestoreOriginal();
    pCmd->viewangles  = original_view;
    pCmd->forwardmove = original_forward;
    pCmd->sidemove    = original_side;
    pCmd->buttons     = original_buttons;
    log_result("miss");
}
