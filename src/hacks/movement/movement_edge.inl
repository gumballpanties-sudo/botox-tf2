// Unit A2 port: edge skip + blockbot from botox csgo-sdk, adapted to TF2's gamemovement.
// Textually included by movement.cpp right after movement_ex.inl AND after edgebug.inl (edge
// skip reuses edgebug.inl's own proven sim/budget primitives: EbBudget_t, TraceFilterWorldProps,
// GameTrace_t, TraceHull, MASK_PLAYERSOLID -- same file-scope statics, same TU).
//
// edge skip (1:1 port of botox hacks/movement/edge_skip.cpp's search/grab/family/post): the
// per-side strafe-and-ledge-grab search, using this tree's own sim/snapshot primitives
// (Pred_Simulate / Pred_SnapSave/Load / RestoreToFrame -- the exact pattern edgebug.inl already
// proves crash-safe for this same family of search, see EdgeBugPost/EbReplayHolds). Botox's
// perfect_angle() (CS:GO friction-based ideal strafe angle) is replaced by this project's own
// airlean::Solve (already used by AutoStrafe/StrafeOptimizer) -- same purpose, TF2-verified
// instead of transplanted. Botox's CS:GO hull constants (54/72/9/4.5) are replaced by this
// file's own already-confirmed TF2 duck/stand heights (62/82, the same pair edgebug.inl's
// EbDuckShiftZ uses) rather than guessed numbers. Dropped: botox's mouse-lock/sound/visualize
// polish (edgebug's own cosmetic layer, not the search itself -- out of scope here) and its
// use_eb() knob-borrowing from edgebug's vars (edge skip gets its own standalone config instead).
//
// blockbot (botox hacks/movement/movement.cpp:2231 blockbot()): 1:1 port. Below+close -> ride
// the nearest player's head by matching their velocity (this is the "head mode = velocity
// follow" behavior named in the task spec -- botox auto-selects it by geometry, not a toggle).
// Otherwise -> side-strafe to walk into their path. Dropped: botox's lag-compensation lead term
// (g_lagcomp.lerp_time()+real_latency()) -- no verified TF2 lerp-rewind helper exists in this
// project yet (no speculative code), so blockbot rides the target's raw velocity, no lead.

// ==== edge skip: per-side strafe + ledge-grab search (botox hacks/movement/edge_skip.cpp) ====
struct EsTick_t { float yaw = 0.f, side = 0.f; bool duck = false; Vec3 origin{}; };

static constexpr int k_esMaxTicks = k_ebMaxTicks;   // reuse edgebug's tick-rate-derived window ceiling
static constexpr int k_esMaxSims  = 2048;           // botox k_max_sims

static EsTick_t s_esPlan[k_esMaxTicks] = {};
// s_esActive lives in edgebug.inl: edge bug yields to a flying edge skip plan
static bool  s_esEbLatched = false;   // botox m_eb_latched: EB held a plan when this cmd began
static int   s_esHit = 0, s_esLast = 0, s_esMaxWindow = 0;
static float s_esPlanGroundZ = 0.f;
static Vec3  s_esView{};
static int   s_esBaseButtons = 0;
static float s_esCourseYaw = 0.f, s_esMaxBend = 180.f;
static int   s_esBends = 0, s_esFoundBug = -1, s_esSims = 0;
static EbBudget_t s_esBudget;

// per-cmd pre-prediction snapshot (EsPre, mirrors EdgeBugPre's timing)
static int   s_esStartFlags = 0, s_esStartMoveType = 0, s_esUserButtons = 0;
static Vec3  s_esStartVel{};
static float s_esGroundZ = 0.f;
static bool  s_esHadGround = false;
static int   s_esAir = 0;
static float s_esPrevYaw = 0.f;
static bool  s_esYawValid = false;
static bool  s_esTurnLeft = false;

static void EsReset(bool bForget)
{
    s_esActive = false;
    s_esHit = 0;
    if (bForget) { s_esHadGround = false; s_esYawValid = false; }
}

static void EsPre(void* pLocal, UserCmd_t* pCmd)
{
    if (!pLocal || !pCmd || !EntAlive(pLocal)) { s_esHadGround = false; s_esYawValid = false; return; }
    s_esStartFlags    = EntI(pLocal, g_offFlags);
    s_esStartMoveType = EntMoveType(pLocal);
    s_esStartVel      = EntV3(pLocal, g_offVelocity);
    s_esUserButtons   = pCmd->buttons;
    s_esEbLatched     = s_ebDetected;
    if ((s_esStartFlags & FL_ONGROUND) && s_esStartMoveType == MOVETYPE_WALK)
    {
        s_esGroundZ  = EntV3(pLocal, g_offOrigin).z;
        s_esHadGround = true;
        s_esAir = 0;
    }
    else if (s_esAir < 100000) s_esAir++;
    const float flYaw = RAD2DEGF(atan2f(s_esStartVel.y, s_esStartVel.x));
    s_esTurnLeft = s_esYawValid && NormalizeAngle(flYaw - s_esPrevYaw) > 0.f;
    s_esPrevYaw  = flYaw;
    s_esYawValid = true;
}

// ideal strafe angle off velocity, TF2-verified (airlean::Solve, same math AutoStrafe/
// StrafeOptimizer already use) in place of botox's CS:GO friction-based perfect_angle()
static float EsPerfectAngleDeg(void* pLocal, const Vec3& vel)
{
    const float flSpeed = vel.Length2D();
    if (flSpeed <= 1.f) return 0.f;
    const float flMaxSpeed = g_offMaxspeed > 0 ? EntF(pLocal, g_offMaxspeed) : 0.f;
    const float flWishSpeed = (flMaxSpeed > 1.f && flMaxSpeed < 450.f) ? flMaxSpeed : 450.f;
    const airlean::Lean lean = airlean::Solve(flSpeed, flWishSpeed, CvarFloat("sv_airaccelerate", 10.f),
                                               TickInterval(), 30.f);
    // botox: |90 - acos(diff / speed)|, the yaw offset of a SIDE wish (side +-450 is already 90
    // off the view). acos alone was the wish-vs-velocity angle: in TF2 the air cap always binds
    // (cosGain 0), so it answered 90 and put the side wish ALONG the velocity -- no steer at all
    return EsSideWishOffsetDeg(lean.cosGain);
}

// botox n_tick::ticks(n): a tick count written for a 64-tick reference, scaled to the game's
// real rate -- same derivation edgebug.inl's own k_ebMaxTicks comment documents (132 not 128).
static int EsTicks(int ticksAt64)
{
    return EsTicksPure(ticksAt64, 1.f / TickInterval());
}

// is there standable-but-not-flat ground right under this point (botox slope_under) -- 62/82
// are this file's own confirmed TF2 duck/stand heights (edgebug.inl EbDuckShiftZ), not CS:GO's
static bool EsSlopeUnder(void* pLocal, const Vec3& at, bool bDucked)
{
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    GameTrace_t tr{};
    TraceHull(at, Vec3(at.x, at.y, at.z - 2.f), Vec3(-16.f, -16.f, 0.f), Vec3(16.f, 16.f, bDucked ? 62.f : 82.f),
              MASK_PLAYERSOLID, &filter, &tr);
    return tr.fraction < 1.f && tr.plane.normal.z > 0.f && tr.plane.normal.z < 0.7f;
}

// has the strafe search drifted more than g_edgeSkipMaxBend off its original course (botox bent())
static bool EsBent(void* pLocal)
{
    const Vec3 vel = EntV3(pLocal, g_offVelocity);
    if (vel.Length2D() <= 1.f) return false;
    const float flYaw = RAD2DEGF(atan2f(vel.y, vel.x));
    if (!EsBentPure(NormalizeAngle(flYaw - s_esCourseYaw), s_esMaxBend)) return false;
    s_esBends++;
    return true;
}

// silent like botox apply(): the wish yaw folded into fwd/side, the real view sent
static void EsApply(UserCmd_t* pCmd, const EsTick_t& t, const Vec3& vView)
{
    pCmd->forwardmove = 0.f;
    pCmd->sidemove    = t.side;
    EbFold(pCmd, Vec3(vView.x, t.yaw, vView.z), vView);
    pCmd->buttons &= ~(IN_DUCK | IN_JUMP);
    if (t.duck) pCmd->buttons |= IN_DUCK;
}

static void EsSim(void* pLocal, UserCmd_t* pCmd, const EsTick_t& t)
{
    pCmd->buttons = s_esBaseButtons;
    EsApply(pCmd, t, s_esView);
    Pred_Simulate(pLocal, pCmd);
    s_esSims++;
}

static bool EsOut() { return s_esSims >= k_esMaxSims || s_esBudget.Expired(); }

static bool EsReplayTo(void* pLocal, UserCmd_t* pCmd, int end)
{
    RestoreToFrame(PredCommandsPredicted() - 1);
    for (int i = 0; i < end; i++)
    {
        if (EsOut()) return false;
        EsSim(pLocal, pCmd, s_esPlan[i]);
    }
    return true;
}

// per side (turn side first), find a tick window where ducking catches a landing a standing
// hull would miss (botox grab()). Snapshot-save/load mirrors edgebug.inl's own pattern exactly.
static bool EsGrab(void* pLocal, UserCmd_t* pCmd, int prefix, float flGroundZ, bool bFirstNeg, int window)
{
    const int last = std::min(prefix + window, k_esMaxTicks) - 1;
    if (prefix < 0 || last < prefix) return false;
    const float dt = TickInterval();
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    // the air duck lifts the feet by the hull difference: botox's 9 (CS:GO 72 - 54, halved) is
    // TF2's 20 (82 - 62), so the catch band reaches 20u up, not the 2u the first port used
    const float shift = EbDuckShiftZ(pLocal);

    // necessary, never sufficient: a duck hull swept from the tick's travel down to the catch
    // band. Empty = no floor. Start solid (wall in the box) = can't tell, sim it (botox may_land)
    auto MayLand = [&](const Vec3& start, const Vec3& vel) -> bool
    {
        const float flTop = start.z + shift + fmaxf(vel.z, 0.f) * dt;
        if (flTop < flGroundZ - 2.f) return false;
        const float r = (vel.Length2D() + 30.f) * dt + 1.f;
        GameTrace_t tr{};
        TraceHull(Vec3(start.x, start.y, flTop + 1.f), Vec3(start.x, start.y, flGroundZ - shift * 0.5f),
                  Vec3(-16.f - r, -16.f - r, 0.f), Vec3(16.f + r, 16.f + r, 62.f),
                  MASK_PLAYERSOLID, &filter, &tr);
        return tr.startsolid || tr.fraction < 1.f;
    };

    const bool bBaseSaved = prefix > 0 && Pred_SnapSave(pLocal, 0);
    for (int pass = 0; pass < 2; pass++)
    {
        const bool neg = (pass == 0) == bFirstNeg;
        if (pass > 0 && bBaseSaved)
        {
            RestoreToFrame(PredCommandsPredicted() - 1);
            Pred_SnapLoad(pLocal, 0);
        }
        else if (pass > 0 || prefix == 0)
        {
            if (!EsReplayTo(pLocal, pCmd, prefix)) return false;
        }
        constexpr int k_maxCand = 64;
        int cand[k_maxCand], slot[k_maxCand];
        int nCand = 0;
        for (int k = prefix; k <= last && !EsOut(); k++)
        {
            const Vec3 start   = EntV3(pLocal, g_offOrigin);
            const Vec3 vel     = EntV3(pLocal, g_offVelocity);
            const float ang    = EsPerfectAngleDeg(pLocal, vel);
            const float velYaw = RAD2DEGF(atan2f(vel.y, vel.x));
            EsTick_t& t = s_esPlan[k];
            t.yaw    = neg ? velYaw - ang : velYaw + ang;
            t.side   = neg ? -450.f : 450.f;
            t.duck   = false;
            t.origin = start;
            const bool bCandK = MayLand(start, vel);
            const int slotIdx = (bCandK && nCand < k_maxCand && Pred_SnapSave(pLocal, 1 + nCand)) ? 1 + nCand : -1;
            EsSim(pLocal, pCmd, t);
            // no-duck landed: test the twin, but never take it on this tick
            if ((EntI(pLocal, g_offFlags) & FL_ONGROUND) || EntMoveType(pLocal) != MOVETYPE_WALK)
                break;
            if (EsBent(pLocal)) break;
            if (bCandK && nCand < k_maxCand) { cand[nCand] = k; slot[nCand++] = slotIdx; }
            if (EntV3(pLocal, g_offOrigin).z + shift < flGroundZ - 2.f && EntV3(pLocal, g_offVelocity).z <= 0.f)
                break;
        }
        for (int c = 0; c < nCand; c++)
        {
            const int k = cand[c];
            if (slot[c] >= 0)
            {
                if (EsOut()) return false;
                RestoreToFrame(PredCommandsPredicted() - 1);
                Pred_SnapLoad(pLocal, slot[c]);
            }
            else if (!EsReplayTo(pLocal, pCmd, k) || EsOut()) return false;
            EsTick_t twin = s_esPlan[k];
            twin.duck = true;
            EsSim(pLocal, pCmd, twin);
            if ((EntI(pLocal, g_offFlags) & FL_ONGROUND) && EntV3(pLocal, g_offOrigin).z >= flGroundZ - 2.f)
            {
                s_esPlan[k].duck = true;
                s_esLast         = k;
                s_esPlanGroundZ  = flGroundZ;
                return true;
            }
        }
    }
    return false;
}

// bug family: per side (turn side first), strafe n=0..4 ticks then no input, never duck. A vz
// rise while airborne = a gravity-reset bug (unless a slope sits under both ends); a rise
// always ends the run, then a ledge grab onto its end z (botox family())
static bool EsFamily(void* pLocal, UserCmd_t* pCmd, bool bFirstNeg)
{
    for (int s = 0; s < 2; s++)
    {
        const bool neg = (s == 0) == bFirstNeg;
        for (int n = (s == 0 ? 0 : 1); n <= 4; n++)   // n=0 never strafes: same run both sides, once
        {
            if (EsOut()) return false;
            const int strafe = n == 0 ? 0 : EsTicks(n);
            RestoreToFrame(PredCommandsPredicted() - 1);
            float flPrevVz    = EntV3(pLocal, g_offVelocity).z;
            Vec3  vPrevEnd    = EntV3(pLocal, g_offOrigin);
            bool  bPrevDucked = (EntI(pLocal, g_offFlags) & FL_DUCKING) != 0;
            for (int t = 0; t < s_esMaxWindow && !EsOut(); t++)
            {
                EsTick_t& p = s_esPlan[t];
                p.origin = EntV3(pLocal, g_offOrigin);
                p.duck   = false;
                p.yaw    = s_esView.y;
                p.side   = 0.f;
                if (t < strafe)
                {
                    const Vec3 vel     = EntV3(pLocal, g_offVelocity);
                    const float ang    = EsPerfectAngleDeg(pLocal, vel);
                    const float velYaw = RAD2DEGF(atan2f(vel.y, vel.x));
                    p.yaw  = neg ? velYaw - ang : velYaw + ang;
                    p.side = neg ? -450.f : 450.f;
                }
                EsSim(pLocal, pCmd, p);
                if ((EntI(pLocal, g_offFlags) & FL_ONGROUND) || EntMoveType(pLocal) != MOVETYPE_WALK || EsBent(pLocal))
                    break;
                const Vec3 vEnd     = EntV3(pLocal, g_offOrigin);
                const float vz      = EntV3(pLocal, g_offVelocity).z;
                const bool bDucked  = (EntI(pLocal, g_offFlags) & FL_DUCKING) != 0;
                if (vz > flPrevVz)
                {
                    if (!EsSlopeUnder(pLocal, vEnd, bDucked) || !EsSlopeUnder(pLocal, vPrevEnd, bPrevDucked))
                    {
                        // first_neg hardcoded true here too -- matches botox's own grab() call site
                        if (t + 1 < s_esMaxWindow && EsGrab(pLocal, pCmd, t + 1, vEnd.z, true, s_esMaxWindow - (t + 1)))
                        {
                            s_esFoundBug = t;
                            return true;
                        }
                    }
                    break;
                }
                flPrevVz = vz; vPrevEnd = vEnd; bPrevDucked = bDucked;
            }
        }
    }
    return false;
}

static void EsSearch(void* pLocal, UserCmd_t* pCmd)
{
    s_esMaxWindow = std::clamp(g_edgeSkipTicks, 2, k_esMaxTicks);
    const bool bWalkOff = s_esHadGround && s_esAir >= 1 && s_esAir <= EsTicks(6);

    const int depth = std::clamp(g_edgeSkipDepth, 1, 4);
    s_esBudget.Start(0.15f * (float)depth);
    s_esSims = 0; s_esBends = 0; s_esFoundBug = -1;
    Pred_SnapReserve(65);   // slot 0 (grab()'s base) + up to 64 candidate slots per EsGrab call
    Pred_SnapReset();

    const Vec3  vView0    = pCmd->viewangles;
    const float flFwd0    = pCmd->forwardmove;
    const float flSide0   = pCmd->sidemove;
    const int   iButtons0 = pCmd->buttons;
    s_esView         = vView0;
    s_esBaseButtons  = s_esUserButtons & ~(IN_DUCK | IN_JUMP | IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);
    RestoreToFrame(PredCommandsPredicted() - 1);

    const Vec3 vOrg0   = EntV3(pLocal, g_offOrigin);
    const Vec3 vVel0   = EntV3(pLocal, g_offVelocity);
    const bool bDucked0 = (EntI(pLocal, g_offFlags) & FL_DUCKING) != 0;
    s_esCourseYaw = RAD2DEGF(atan2f(vVel0.y, vVel0.x));
    s_esMaxBend   = vVel0.Length2D() > 1.f ? std::clamp(g_edgeSkipMaxBend, 0.f, 180.f) : 180.f;

    int src = 0;
    if (bWalkOff && EsGrab(pLocal, pCmd, 0, s_esGroundZ, s_esTurnLeft, s_esMaxWindow))
        src = 1;

    bool bReach = false;
    if (!src)
    {
        const float flGrav   = CvarFloat("sv_gravity", 800.f);
        const float flWindow = (float)s_esMaxWindow * TickInterval();
        const float flGrow   = 2.f + 30.f * (float)EsTicks(4) * flWindow;
        const float flSegT   = flWindow / 3.f;
        const float flStandHeight = (g_offMaxs > 0 && g_offMins > 0)
            ? EntV3(pLocal, g_offMaxs).z - EntV3(pLocal, g_offMins).z : 82.f;
        const Vec3 vMins(-16.f - flGrow, -16.f - flGrow, bDucked0 ? -10.f : -1.f);
        const Vec3 vMaxs(16.f + flGrow, 16.f + flGrow, flStandHeight + flGrav * flSegT * flSegT * 0.125f);
        TraceFilterWorldProps filter; filter.Setup(pLocal);
        Vec3 vPrev = vOrg0;
        for (int seg = 1; seg <= 3 && !bReach; seg++)
        {
            const float ts = flSegT * (float)seg;
            Vec3 p = vOrg0 + vVel0 * ts;
            p.z = vOrg0.z + vVel0.z * ts - 0.5f * flGrav * ts * ts;
            GameTrace_t tr{};
            TraceHull(vPrev, p, vMins, vMaxs, MASK_PLAYERSOLID, &filter, &tr);
            bReach = tr.startsolid || tr.fraction < 1.f;
            vPrev = p;
        }
        if (bReach && EsFamily(pLocal, pCmd, s_esTurnLeft))
            src = 2;
    }

    pCmd->viewangles  = vView0;
    pCmd->forwardmove = flFwd0;
    pCmd->sidemove    = flSide0;
    pCmd->buttons     = iButtons0;
    if (s_esSims > 0) RestoreToFrame(PredCommandsPredicted() - 1);   // never leave a sim tick live

    if (bWalkOff || bReach)
        DebugLog("ES: src=%d wo=%d air=%d gz=%.2f z=%.2f vz=%.2f turn=%d win=%d reach=%d sims=%d "
                 "found=%d bug=%d at=%d bend=%d/%.0f depth=%d",
                 src, bWalkOff ? 1 : 0, s_esAir, s_esGroundZ, vOrg0.z, vVel0.z, s_esTurnLeft ? 1 : 0,
                 s_esMaxWindow, bReach ? 1 : 0, s_esSims, src ? 1 : 0, s_esFoundBug,
                 src ? s_esLast : -1, s_esBends, s_esMaxBend, depth);

    if (!src) return;
    s_esActive = true;
    s_esHit    = 0;
    EbDropPlan("es");   // botox: an edge skip find takes the fall from edge bug
}

// runs AFTER Pred_End, same timing as EdgeBugPost -- probes the engine's predicted frame with
// its own re-established window (botox post())
static void EdgeSkipPost(void* pLocal, UserCmd_t* pCmd)
{
    if (!pCmd || !pLocal || !EntAlive(pLocal) || !On(g_edgeSkip, g_edgeSkipKey, g_edgeSkipMode)
        || g_offTickBase <= 0)
    {
        EsReset(true);
        return;
    }
    if (s_esStartMoveType != MOVETYPE_WALK)
    {
        EsReset(true);
        return;
    }
    // don't fight a wall feature that owns this cmd (botox: air stuck / wall climb / pixel surf).
    // NOT edge bug: the first port reset here on any EB latch, and EB latches on nearly every
    // fall near geometry, so with both binds on edge skip never got to fly a plan. botox only
    // skips the SEARCH while EB holds one (m_eb_latched, below); a flying skip plan stays
    if (s_textureBugHit || s_tbOwnsCmd || s_headSurfHit || s_headBounceHit
        || s_wallClimbHit || s_airStuckHit || s_shouldPixelSurf)
    {
        EsReset(true);
        return;
    }
    // sims after Pred_End: the tick-time window, or every sim integrates a frame, not a tick
    const EbTimeGuard timeGuard(EntI(pLocal, g_offTickBase));
    if (s_esStartFlags & FL_ONGROUND)
    {
        if (s_esActive)
            DebugLog("ES: landed hit=%d last=%d gz=%.2f z=%.2f lip=%d", s_esHit - 1, s_esLast,
                     s_esPlanGroundZ, s_esGroundZ, s_esGroundZ >= s_esPlanGroundZ - 2.f ? 1 : 0);
        EsReset(true);
        return;
    }
    if (s_esActive)
    {
        RestoreToFrame(PredCommandsPredicted() - 1);
        if (s_esHit > s_esLast)
        {
            DebugLog("ES: missed spent=%d z=%.2f gz=%.2f", s_esLast, EntV3(pLocal, g_offOrigin).z, s_esPlanGroundZ);
            EsReset(false);
        }
        else if (const float flDist = (EntV3(pLocal, g_offOrigin) - s_esPlan[s_esHit].origin).Length(); flDist > 1.f)
        {
            DebugLog("ES: drop at %d/%d dist=%.3f", s_esHit, s_esLast, flDist);
            EsReset(false);
        }
    }
    if (!s_esActive && !s_esEbLatched && !(s_esStartVel.z > 0.f) && !(s_esUserButtons & IN_DUCK))
        EsSearch(pLocal, pCmd);
    if (!s_esActive) return;
    EsApply(pCmd, s_esPlan[s_esHit], pCmd->viewangles);
    s_esHit++;
}

// ==== blockbot: steer into the nearest other player's path (botox movement.cpp:2231) ====
static void BlockBot(void* pLocal, UserCmd_t* pCmd)
{
    if (!On(g_blockBot, g_blockBotKey, g_blockBotMode)) return;
    const int mt = EntMoveType(pLocal);
    if (mt != MOVETYPE_WALK) return;   // ladder dropped (TF2 has none worth this); noclip/other out
    if (g_offOrigin <= 0 || g_offVelocity <= 0) return;

    const Vec3 vMyOrigin = EntV3(pLocal, g_offOrigin);
    const int  myIdx = GetLocalPlayerIndex();

    float flBestDist = 3.4e38f;
    void* pBest = nullptr;
    for (int i = 1; i <= 32; ++i)
    {
        if (i == myIdx) continue;
        void* pEnt = GetClientEntity(i);
        if (!pEnt || pEnt == pLocal) continue;
        if (!EntAlive(pEnt)) continue;
        const float d = (EntV3(pEnt, g_offOrigin) - vMyOrigin).Length();
        if (d < flBestDist) { flBestDist = d; pBest = pEnt; }
    }
    if (!pBest) return;

    const Vec3 vTargetOrigin = EntV3(pBest, g_offOrigin);
    const Vec3 vTargetVel    = EntV3(pBest, g_offVelocity);
    const Vec3 vForward      = vTargetOrigin - vMyOrigin;

    // "head mode": target is below and close -- stand on them, match their velocity so the
    // engine doesn't carry you off (botox: bone_position(6).z < local.z && dist < 100)
    if (vTargetOrigin.z < vMyOrigin.z && flBestDist < 100.f)
    {
        const Vec3 vMyVel = EntV3(pLocal, g_offVelocity);
        const float flYaw = DEG2RADF(pCmd->viewangles.y);
        const float flWishX = BlockBotHeadWish(vTargetVel.x, vMyVel.x);
        const float flWishY = BlockBotHeadWish(vTargetVel.y, vMyVel.y);
        pCmd->forwardmove = std::clamp(cosf(flYaw) * flWishX + sinf(flYaw) * flWishY, -450.f, 450.f);
        pCmd->sidemove    = std::clamp(sinf(flYaw) * flWishX - cosf(flYaw) * flWishY, -450.f, 450.f);
        DebugLog("BB: head mode target dist=%.1f", flBestDist);
        return;
    }

    // otherwise: strafe sideways into the gap between you and them
    float flYawDelta = RAD2DEGF(atan2f(vForward.y, vForward.x)) - pCmd->viewangles.y;
    if (flYawDelta > 180.f) flYawDelta -= 360.f;
    else if (flYawDelta < -180.f) flYawDelta += 360.f;

    const float flTargetSpeed = vTargetVel.Length();
    if (const int sign = BlockBotStrafeSign(flYawDelta))
        pCmd->sidemove = (float)sign * flTargetSpeed;
}
