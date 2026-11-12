// pixelhop.inl -- textually included by movement.cpp after psassist.inl (same TU). Pixeljump's "pixel hop" =
// botox css sergo.inl SgPixelHop (webz.dll FUN_180016480; createmove 18000c0b0 runs it only when the TB key is
// NOT held, and skips wall stuck while it is). Every tick, from START:
//   1. jump test: cmd + IN_JUMP flown one tick; END vz > max(START vz, 0) + 100 = the jump took -> send it
//   2. else the planner (1800098c0, sim 180008f40): a cmd that GROUNDS you within the horizon, ranked by the
//      xy speed it lands with, stop once within 0.5 of yours. Candidates: your cmd, your cmd crouch-flipped,
//      last tick's plan, then a face (PhFindWall = webz 18000a300): offsets turned away from your along-face
//      velocity x sizes x unduck/duck (1 tick), and if none lands, a coarse grid over 3 ticks (sooner wins).
//      No face: offsets off your velocity yaw x {450, 30, 5}.
//   3. no landing at all: texture bug (webz calls its TB here), skipped only while AA hold rides a surf.
// Pixel surf stands down while the key is held (s_pixelHopOn): its 8-tick catch ducks / presses you off the
// ledge PH would land on. Hop gate (css "no auto jump"): step 1 runs while you hold jump, or on the first ground
// tick after a land PH steered itself. Land cmds make PS / WC / AS / auto bounce / bounce assist yield
// (s_pixelHopHit).
// TF2 shims:
//   no ladders                 -> MOVETYPE_WALK check only
//   jump test                  -> grounded START only: an air IN_JUMP sim is a scout double jump and "takes"
//   ducked ground jump refused -> (tf_gamemovement.cpp:1221) retry with IN_DUCK off, send that stance
//   air cmd PH did not write   -> keeps your space (scout double jump); the land cmd drops it (m_nOldButtons latch)
//   SgTextureBugTick           -> TextureBugBotox forced on (texturebug.inl tbx, TB key not needed)
//   pixeljump's own planned hop (s_psAssistHopActive, PS_AssistTick ran before) owns its cmd: PH yields
// Pure rules + tables: pjseam.h pjs:: (tests/pjseam_test.cpp). Log `PH:`.

// per-second diag (PH line): air = air ticks planned, lands = plans sent (own = your own cmd landed), steer =
// landed on a PH wish, nowall = face find failed, tb = TB fallback wrote, hops = jumps sent, unduck = TF2 duck retry
static struct { bool armed = false; pjs::PhPlan last{};
                int sims = 0, lands = 0, hops = 0, air = 0, pin = 0, nowall = 0, pinLands = 0,
                    own = 0, steer = 0, tb = 0, cut = 0, floor = 0, unduck = 0; } s_ph;
static const char* s_phStage = "";   // which candidate grounded (land log)
// last land sent: the cmd as PH wrote it + the END its sim gave. Next cmd checks the engine's copy and START
static struct { int cmd = -1; UserCmd_t c{}; Vec3 endO{}; float endVz = 0.f; } s_phSent;
static Vec3  s_phEndO{};   static float s_phEndVz = 0.f;   // END of the best plan's sim
static Vec3  s_phSimO{};   static float s_phSimVz = 0.f;   // END of the last grounded PhSim
// webz's planner has no clock. Worst plan = 3 + 160 + 40x3 = 283 ticks (~2.5 ms at ~9 us/sim), so this cap
// only bites on a perf spike. ponytail: 5 ms safety cap. `PH: ... cut=` > 0 = it bit, raise it
constexpr long long k_phClockUs = 5000;

static float PhHalfGrav() { return CvarFloat("sv_gravity", 800.f) * TickInterval() * 0.5f; }

// FUN_18002b370: one engine tick of `c`; attack bits never ride a probe
static void PhSimTick(void* pLocal, UserCmd_t c)
{
    c.buttons &= ~(IN_ATTACK | IN_ATTACK2);
    c.hasbeenpredicted = false;
    Pred_Simulate(pLocal, &c);
    ++s_ph.sims;
}

// FUN_180018910: line traces out of the hull centre, 2u past its edge, at 5 heights feet -> head, 16 directions.
// First direction with a vertical face (|nz| <= 0.05) facing back at us wins. yaw = straight into the face
struct PhWall { bool ok = false; float yaw = 0.f; };
static PhWall PhFindWall(void* pLocal, const Vec3& o)
{
    PhWall w;
    const Vec3 mn = EntV3(pLocal, g_offMins), mx = EntV3(pLocal, g_offMaxs);
    const float ex = (std::max)(fabsf(mn.x), fabsf(mx.x)), ey = (std::max)(fabsf(mn.y), fabsf(mx.y));
    const float z0 = o.z + mn.z, z1 = o.z + mx.z;
    TraceFilterWorldOnly fil; fil.Setup();
    for (int k = 0; k < 16; ++k)
    {
        const float a = (float)k * 0.3926991f;
        const float c = cosf(a), s = sinf(a);
        const float d = (std::min)(ex / (std::max)(fabsf(c), 1e-6f), ey / (std::max)(fabsf(s), 1e-6f));
        float bestFrac = 1.f;
        Vec3 n{};
        for (int i = 0; i < 5; ++i)
        {
            const float z = (float)i * (z1 - z0) * 0.25f + z0;
            GameTrace_t tr{};
            TraceLine(Vec3(o.x, o.y, z), Vec3(o.x + (d + 2.f) * c, o.y + (d + 2.f) * s, z), MASK_PLAYERSOLID, &fil, &tr);
            if (tr.fraction < bestFrac && fabsf(tr.plane.normal.z) <= 0.05f
                && tr.plane.normal.y * s + tr.plane.normal.x * c <= -0.1f)
            {
                bestFrac = tr.fraction;
                n = tr.plane.normal;
            }
        }
        if (bestFrac < 1.f)
        {
            w.ok  = true;
            w.yaw = RAD2DEGF(atan2f(-n.y, -n.x));
            return w;
        }
    }
    return w;
}

// webz FUN_180005b50: a plan onto a cmd. Wish -> forward/side off the view yaw (keys / upmove untouched, as webz)
static void PhApply(UserCmd_t& c, const pjs::PhPlan& p)
{
    if (!p.keep) pjs::WishToCmd(p.yaw, p.move, c.viewangles.y, c.forwardmove, c.sidemove);
    if (p.duck == 1) c.buttons |= IN_DUCK;
    else if (p.duck == 2) c.buttons &= ~IN_DUCK;
}

// webz FUN_180008f40: fly plan `p` from START for up to `horizon` ticks; found = FL_ONGROUND on tick `ticks`,
// speed = xy speed there. Leaves the entity wherever the sim stopped (the caller restores)
static bool PhSim(void* pLocal, int nBase, const UserCmd_t& base, pjs::PhPlan& p, int horizon)
{
    p.found = false;
    RestoreToFrame(nBase - 1);
    UserCmd_t c = base;
    c.buttons &= ~IN_JUMP;
    PhApply(c, p);
    for (int t = 1; t <= horizon; ++t)
    {
        PhSimTick(pLocal, c);
        if (EntMoveType(pLocal) != MOVETYPE_WALK) break;
        if (EntI(pLocal, g_offFlags) & FL_ONGROUND)
        {
            p.found = true;
            p.speed = EntV3(pLocal, g_offVelocity).Length2D();
            p.ticks = t;
            s_phSimO = EntV3(pLocal, g_offOrigin); s_phSimVz = EntV3(pLocal, g_offVelocity).z;
            break;
        }
    }
    return p.found;
}

// webz FUN_1800098c0: the landing planner. Candidate order and tables are webz's (pjseam.h k_phw*)
static pjs::PhPlan PhPlanLand(void* pLocal, int nBase, const UserCmd_t& cmd)
{
    const Vec3  v      = s_prePredVel;
    const float speed0 = v.Length2D();
    pjs::PhPlan best;
    LARGE_INTEGER f0{}, q0{}; QueryPerformanceFrequency(&f0); QueryPerformanceCounter(&q0);
    const long long span = f0.QuadPart * k_phClockUs / 1000000ll;
    bool cut = false;
    const auto expired = [&]() { LARGE_INTEGER q{}; QueryPerformanceCounter(&q); return cut = q.QuadPart - q0.QuadPart > span; };
    bool ownFound = false; float ownZ = 0.f;   // where your own move lands (set after the own passes)
    // true = stop searching (a landing keeps your speed)
    const auto offer = [&](pjs::PhPlan k, int horizon, bool byTicks, const char* tag) {
        if (PhSim(pLocal, nBase, cmd, k, horizon))
        {
            if (pjs::PhSameFloor(ownFound, ownZ, s_phSimO.z)) ++s_ph.floor;
            else if (pjs::PhBetter(k, best, byTicks)) { best = k; s_phStage = tag; s_phEndO = s_phSimO; s_phEndVz = s_phSimVz; }
        }
        return pjs::PhDone(best, speed0);
    };
    const auto wish = [](float yaw, float move, int duck) {
        pjs::PhPlan k; k.keep = false; k.yaw = pjs::NormYaw(yaw); k.move = move; k.duck = duck; return k;
    };

    pjs::PhPlan own;
    bool done = offer(own, 1, false, "own");
    own.duck = (cmd.buttons & IN_DUCK) ? 2 : 1;
    if (!done) done = offer(own, 1, false, "own duck");
    if (!done && s_ph.last.found) done = offer(s_ph.last, 1, false, "last");
    if (done) return best;
    // own move flown 6 ticks (> the 3-tick pass): its floor is what every steered plan must beat
    {
        pjs::PhPlan ref;
        ownFound = PhSim(pLocal, nBase, cmd, ref, 6);
        ownZ = s_phSimO.z;
    }

    RestoreToFrame(nBase - 1);
    const PhWall wall = PhFindWall(pLocal, s_prePredOrigin);
    const float velYaw = RAD2DEGF(atan2f(v.y, v.x));
    if (!wall.ok)
    {
        ++s_ph.nowall;
        for (const float off : pjs::k_phwVelOff) for (const float mv : pjs::k_phwVelMove) for (const int d : pjs::k_phwDuck)
            if (expired() || offer(wish(velYaw + off, mv, d), 1, false, "vel")) { s_ph.cut += cut; return best; }
        return best;
    }
    const float sign = pjs::PhwSign(velYaw, wall.yaw);
    for (const float off : pjs::k_phwOff) for (const float mv : pjs::k_phwMove) for (const int d : pjs::k_phwDuck)
        if (expired() || offer(wish(wall.yaw + sign * off, mv, d), 1, false, "face")) { s_ph.cut += cut; return best; }
    if (best.found) return best;
    for (const float off : pjs::k_phwOff3) for (const float mv : pjs::k_phwMove3) for (const int d : pjs::k_phwDuck)
    {
        if (expired()) { ++s_ph.cut; return best; }
        offer(wish(wall.yaw + sign * off, mv, d), 3, true, "face3");
    }
    return best;
}

static void PixelHop(void* pLocal, UserCmd_t* cmd)
{
    s_pixelHopHit = false;
    {
        static int s_sec = -1;
        const int sec = g_globalVars ? (int)g_globalVars->curtime : 0;
        if (sec != s_sec)
        {
            s_sec = sec;
            // printed on any search, so "never lands" still leaves a line saying why
            if (g_debugLogEnable && (s_ph.lands || s_ph.hops || s_ph.air))
                DebugLog("PH: lands=%d own=%d steer=%d pinLands=%d hops=%d unduck=%d air=%d pin=%d nowall=%d tb=%d cut=%d floor=%d sims=%d",
                         s_ph.lands, s_ph.own, s_ph.steer, s_ph.pinLands, s_ph.hops, s_ph.unduck, s_ph.air, s_ph.pin,
                         s_ph.nowall, s_ph.tb, s_ph.cut, s_ph.floor, s_ph.sims);
            s_ph.lands = s_ph.own = s_ph.steer = s_ph.pinLands = s_ph.hops = s_ph.unduck = s_ph.air = s_ph.pin = 0;
            s_ph.nowall = s_ph.tb = s_ph.cut = s_ph.floor = s_ph.sims = 0;
        }
    }
    const bool key = g_pjHop && On(g_pixelJump, g_pixelJumpKey, g_pixelJumpMode) && EntAlive(pLocal);
    s_phSyncWant = key;   // armed on the key, before the jump: cl_pred_optimize only bites from the next packet
    // last cmd's land: did the engine send what PH proved, and did it ground? took=0 same=1 = sim vs real
    // state differ; same=0 = a writer after PH changed the cmd (d* = what changed)
    if (s_phSent.cmd >= 0 && s_phSent.cmd == cmd->command_number - 1)
    {
        const UserCmd_t* s = AsUserCmd(s_phSent.cmd);
        const UserCmd_t& p = s_phSent.c;
        const bool same = s && s->forwardmove == p.forwardmove && s->sidemove == p.sidemove && s->buttons == p.buttons
                       && s->viewangles.x == p.viewangles.x && s->viewangles.y == p.viewangles.y;
        DebugLog("PH: verify took=%d same=%d dfwd=%.4g dside=%.4g dyaw=%.4g btn=%x/%x | sim z=%.4f vz=%.2f got z=%.4f vz=%.2f",
                 (s_prePredFlags & FL_ONGROUND) ? 1 : 0, same ? 1 : 0, s ? s->forwardmove - p.forwardmove : 0.f,
                 s ? s->sidemove - p.sidemove : 0.f, s ? s->viewangles.y - p.viewangles.y : 0.f, s ? s->buttons : 0, p.buttons,
                 s_phSent.endO.z, s_phSent.endVz, s_prePredOrigin.z, s_prePredVel.z);
    }
    s_phSent.cmd = -1;
    // webz 18000c0b0: the TB key wins (PH never runs with it); webz 180016480: noclip drops the plan
    if (!key || On(g_textureBug, g_textureBugKey, g_textureBugMode) || EntMoveType(pLocal) != MOVETYPE_WALK)
    {
        s_pixelHopOn = false;
        s_ph.armed = false;
        s_ph.last = {};
        return;
    }
    s_pixelHopOn = true;
    // pixeljump's own hop / pending launch / ride hold (PS_AssistTick, this cmd) proved its cmd: hands off
    if (s_psAssistHopActive) return;
    const int  nBase    = PredCommandsPredicted();
    const bool grounded = (s_prePredFlags & FL_ONGROUND) != 0;
    // grounded START + IN_JUMP without your space = another feature's sim-proven takeoff (edge jump,
    // LJ / MJ edge), all post-pred before this. PH owns YOUR space, not their press
    if (grounded && (cmd->buttons & IN_JUMP) && !s_rawJumpHeld)
    {
        s_ph.armed = false;
        s_ph.last = {};
        return;
    }

    // 1. jump test (TF2: grounded START only, an air IN_JUMP sim is a scout double jump and "takes")
    if (grounded && (s_rawJumpHeld || s_ph.armed))
    {
        cmd->buttons &= ~IN_JUMP;
        const auto took = [&](UserCmd_t j) {
            RestoreToFrame(nBase - 1);
            j.buttons |= IN_JUMP;
            PhSimTick(pLocal, j);
            const bool ok = pjs::PhJumpTook(s_prePredVel.z, EntV3(pLocal, g_offVelocity).z);
            RestoreToFrame(nBase - 1);
            return ok;
        };
        bool ok = took(*cmd);
        // TF2 CheckJumpButton refuses a ground jump while FL_DUCKING: the same press standing
        if (!ok && (cmd->buttons & IN_DUCK))
        {
            UserCmd_t u = *cmd;
            u.buttons &= ~IN_DUCK;
            if (took(u)) { cmd->buttons &= ~IN_DUCK; ok = true; ++s_ph.unduck; }
        }
        if (ok)
        {
            if (s_ph.armed) DebugLog("PH: hop z=%.3f xy=%.1f by=%s", s_prePredOrigin.z, s_prePredVel.Length2D(), s_rawJumpHeld ? "space" : "land");
            cmd->buttons |= IN_JUMP;
            s_pixelHopHit = true;
            s_ph.armed = false;
            ++s_ph.hops;
            return;
        }
    }
    if (grounded) { s_ph.armed = false; s_ph.last = {}; return; }   // standing: your move, nothing to land

    // 2. land plan
    ++s_ph.air;
    const float h      = PhHalfGrav();
    const bool  pinned = pjs::PhPinned(s_prePredVel.z, h);
    if (pinned) ++s_ph.pin;
    if (!pjs::PhCanLand(s_prePredVel.z, h)) return;   // rising too fast to ground inside the horizon
    const pjs::PhPlan plan = PhPlanLand(pLocal, nBase, *cmd);
    RestoreToFrame(nBase - 1);
    if (plan.found)
    {
        // the land cmd carries no IN_JUMP (as its sim): that is what clears the m_nOldButtons latch for the hop after it
        cmd->buttons &= ~IN_JUMP;
        PhApply(*cmd, plan);
        s_ph.last = plan.keep ? pjs::PhPlan{} : plan;   // webz stores a kept own move as "no plan"
        // a land you steered into yourself (a plain fall) is not a hop: space still gates that one
        s_ph.armed = s_ph.armed || !plan.keep || pinned;
        s_pixelHopHit = true;
        ++s_ph.lands;
        ++(plan.keep ? s_ph.own : s_ph.steer);
        if (pinned) ++s_ph.pinLands;
        if (!plan.keep || pinned)
            DebugLog("PH: land by=%s t=%d pin=%d z=%.3f vz=%.2f xy=%.1f -> %.1f yaw=%.2f move=%.3f duck=%d",
                     s_phStage, plan.ticks, pinned ? 1 : 0, s_prePredOrigin.z, s_prePredVel.z, s_prePredVel.Length2D(),
                     plan.speed, plan.yaw, plan.move, plan.duck);
        if (plan.ticks == 1)
        {
            s_phSent.cmd = cmd->command_number;
            s_phSent.c = *cmd;
            s_phSent.endO = s_phEndO;
            s_phSent.endVz = s_phEndVz;
        }
        return;
    }
    // 3. nothing lands: webz falls back to its texture bug -- the face hug it writes is what walks the hull
    // onto the ledge corner (css log 11-08: tb= > 0 every working hop). AA stick (css s_aaHolding) keeps a live surf instead
    if (s_aaStickOwns) return;
    TextureBugBotox(pLocal, cmd, true);
    if (s_textureBugHit || s_tbOwnsCmd) { s_pixelHopHit = true; ++s_ph.tb; }
}
