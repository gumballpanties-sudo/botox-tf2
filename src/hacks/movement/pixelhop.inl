// pixelhop.inl -- textually included by movement.cpp after psassist.inl (same TU). Pixeljump's "pixel hop":
// botox css sergo.inl SgPixelHop (sergowebz z.dll FUN_18002a420 / 1d320) on the pixeljump key, space held.
//   air, falling: find a cmd that GROUNDS this very tick (a lip / pixel on the wall next to you) -- your cmd,
//                 crouch-flipped, into the face and 45 off it at 450, then (only while hung on a pin) a rolling
//                 384-step search (pjs::PhAt) under a 1.5 ms clock that resumes where it stopped next tick
//   ground:       jump. That ground tick is also the one pixeljump's planner (PS_AssistTick, runs earlier)
//                 plans a seam catch from, so a pixel becomes a launch floor halfway up the wall
// TF2 diff: CheckJumpButton refuses a ground jump while FL_DUCKING (tf_gamemovement.cpp:1221) where CS:S takes
// it, so a land only counts when the NEXT tick's jump (duck off) is simmed and the engine takes it -- a ducked
// land on a lip is a dead stop, not a hop. Yields to pixeljump's own arc / pending launch / ride hold (its sim
// proved that cmd). PixelSurf / WallClimb / AutoBounce / BounceAssist yield to a pixel hop cmd. Log `PH:`.

static struct { bool landed = false; int roll = 0; int sims = 0, lands = 0, hops = 0, refused = 0; } s_ph;

static void PhSetMoveKeys(UserCmd_t& c)
{
    c.buttons &= ~(IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);
    if (c.forwardmove > 0.01f) c.buttons |= IN_FORWARD; else if (c.forwardmove < -0.01f) c.buttons |= IN_BACK;
    if (c.sidemove > 0.01f) c.buttons |= IN_MOVERIGHT; else if (c.sidemove < -0.01f) c.buttons |= IN_MOVELEFT;
}

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

// FUN_18001d320 + the TF2 hop proof: `c` ends this tick FL_ONGROUND (still walking), and the next tick's
// jump (duck released) leaves the ground going up
static bool PhLandsAndHops(void* pLocal, int nBase, const UserCmd_t& c)
{
    RestoreToFrame(nBase - 1);
    if (SimsLeft() < 2) return false;
    PhSimTick(pLocal, c);
    bool ok = EntMoveType(pLocal) == MOVETYPE_WALK && (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
    if (ok)
    {
        UserCmd_t j = c;
        j.buttons = (j.buttons | IN_JUMP) & ~IN_DUCK;
        PhSimTick(pLocal, j);
        ok = (EntI(pLocal, g_offFlags) & FL_ONGROUND) == 0 && EntV3(pLocal, g_offVelocity).z > k_psLaunchVz;
        if (!ok) ++s_ph.refused;
    }
    RestoreToFrame(nBase - 1);
    return ok;
}

// the air half: true = `cmd` now lands this tick on a floor the next tick can jump off
static bool PhAir(void* pLocal, UserCmd_t* cmd)
{
    const int   nBase = PredCommandsPredicted();
    const float h     = CvarFloat("sv_gravity", 800.f) * TickInterval() * 0.5f;
    const auto test = [&](const UserCmd_t& c) { return PhLandsAndHops(pLocal, nBase, c); };
    const auto flipDuck = [](UserCmd_t c) { c.buttons ^= IN_DUCK; PhSetMoveKeys(c); return c; };
    const auto wish = [](UserCmd_t c, float yaw, float move) {
        const float r = DEG2RADF(yaw);
        MV_WriteWishWorld(&c, Vec3(cosf(r), sinf(r), 0.f), move);
        c.upmove = 0.f;
        PhSetMoveKeys(c);
        return c;
    };

    // the land cmd carries no IN_JUMP: that is what clears the m_nOldButtons latch for the hop after it
    UserCmd_t c = *cmd;
    c.buttons &= ~IN_JUMP;
    if (test(c)) { *cmd = c; return true; }
    const UserCmd_t f = flipDuck(c);
    if (test(f)) { *cmd = f; return true; }

    RestoreToFrame(nBase - 1);
    const PhWall wall = PhFindWall(pLocal, s_prePredOrigin);
    if (!wall.ok) return false;
    const Vec3  v      = s_prePredVel;
    const float velYaw = RAD2DEGF(atan2f(v.y, v.x));
    const float sign   = NormalizeAngle(velYaw - wall.yaw) < 0.f ? -1.f : 1.f;
    for (const float off : { 0.f, 45.f })
    {
        const UserCmd_t w = wish(c, NormalizeAngle(sign * off + wall.yaw), 450.f);
        if (test(w)) { *cmd = w; return true; }
        const UserCmd_t wf = flipDuck(w);
        if (test(wf)) { *cmd = wf; return true; }
    }

    const float dt = TickInterval();
    if (dt <= 0.f || !pjs::PhPinned(v.z, h)) { s_ph.roll = 0; return false; }
    AsClock clock;
    clock.Start(0.0015f / dt, dt);   // 1.5 ms, never past the cmd's own budget
    for (int n = 0; n < pjs::k_phSteps && !clock.Expired(); ++n)
    {
        const int i = s_ph.roll;
        s_ph.roll = (s_ph.roll + 1) % pjs::k_phSteps;
        const pjs::PhStep s = pjs::PhAt(i);
        UserCmd_t w = wish(c, NormalizeAngle((s.flipSide ? -sign : sign) * s.yawOff + wall.yaw), s.move);
        if (s.flipDuck) w = flipDuck(w);
        if (test(w)) { s_ph.roll = i; *cmd = w; return true; }   // same step leads next tick
    }
    return false;
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
            if (g_debugLogEnable && (s_ph.lands || s_ph.hops || s_ph.refused))
                DebugLog("PH: lands=%d hops=%d refused=%d sims=%d", s_ph.lands, s_ph.hops, s_ph.refused, s_ph.sims);
            s_ph.lands = s_ph.hops = s_ph.refused = s_ph.sims = 0;
        }
    }
    // space gates it (z.dll hops on the key alone). Pixeljump's own hop / pending launch / ride hold owns the
    // cmd it planned (s_psAssistHopActive, set by PS_AssistTick this cmd)
    const bool on = g_pjHop && On(g_pixelJump, g_pixelJumpKey, g_pixelJumpMode) && s_rawJumpHeld && EntAlive(pLocal)
                 && EntMoveType(pLocal) == MOVETYPE_WALK && !s_psAssistHopActive;
    if (!on)
    {
        s_ph.landed = false;
        s_ph.roll = 0;
        return;
    }
    if (s_prePredFlags & FL_ONGROUND)
    {
        // the no-pogo latch: last cmd's IN_JUMP (ours or anyone's) = this press is thrown away, so spend this
        // cmd releasing it (z.dll: every other ground tick)
        if (s_prevCmdJump) { cmd->buttons &= ~IN_JUMP; s_ph.landed = false; return; }
        cmd->buttons |= IN_JUMP;
        if (s_ph.landed) cmd->buttons &= ~IN_DUCK;   // the stance the land was proved to hop from
        s_ph.landed = false;
        s_pixelHopHit = true;
        ++s_ph.hops;
        return;
    }
    s_ph.landed = false;
    // rising: nothing to land on. The air cmd keeps your space (a scout's fresh press is a double jump);
    // only the land cmd drops it
    if (s_prePredVel.z > 0.f) return;
    if (PhAir(pLocal, cmd))
    {
        s_ph.landed = true;
        s_pixelHopHit = true;
        ++s_ph.lands;
        DebugLog("PH: land z=%.3f vz=%.2f xy=%.1f duck=%d fwd=%.3f side=%.3f", s_prePredOrigin.z, s_prePredVel.z,
                 s_prePredVel.Length2D(), (cmd->buttons & IN_DUCK) ? 1 : 0, cmd->forwardmove, cmd->sidemove);
    }
    RestoreToFrame(PredCommandsPredicted() - 1);
}
