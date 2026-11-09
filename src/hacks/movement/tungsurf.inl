// tungsurf.inl -- botox csgo-sdk hacks/movement/movement.cpp tung_surf( ) + tung_surf_apply( ) /
// tung_surf_apply_sv( ) (hooks/functions/process_movement.cpp), 1:1. LOCAL SERVER ONLY.
// Textually included by movement.cpp, last (same TU: Pred_Simulate / RestoreToFrame / TraceHull /
// s_prePred* / PsWallTouch / PsWallAhead / s_psBail / AsClock / EbTimeGuard / s_fmServerSeenMs).
//
// What it does: falling beside a wall, it searches the z planes around this tick's natural end
// for one that, snapped onto, rides as a pixel surf (vz pinned, xy sliding) for 2+ ticks. The
// snap is a slot {cmd, tick base, from, to}; the ProcessMovement hooks (fakemove.inl) put `to` on
// the cmd's start origin: client side for our sims + the engine's replays, server side for the
// real move. Every candidate is proven by sims before it is taken (the slot is live during them).
//
// botox -> tf2 shims (same as pixelsurf.inl):
//   g_prediction.backup_data / s_cmd_start         -> s_prePredOrigin / s_prePredVel / s_prePredFlags
//   restore_entity_to_predicted_frame / begin+end  -> RestoreToFrame / Pred_Simulate (EbTimeGuard = tick frametime)
//   get_duck_amount                                -> TungDuckAmt: m_bDucking = mid (0.5), FL_DUCKING 1 / 0.
//                                                     TF2 air ducks are instant (gamemovement.cpp Duck bInAir)
//   col->get_obb_mins/maxs                         -> m_vecMins/Maxs off the restored frame; stance hull 62 / 82 (csgo 54 / 72)
//   is_loopback                                    -> fakemove's server hook saw our cmd in the last second
//   n_tick::ticks / cmd_clock_left / c_sim_budget  -> as::TicksScaled / TbBudgetLeftUs / AsClock
//   g_ctx.old_view_point                           -> cmd->viewangles (movement runs before aim here)
//   eb_cmd_taken / tb acted / air stuck / ps latch -> TungCmdTaken (last cmd's flags, as botox reads them)
//   y6 assist ("y6_ast", a csgo server command)    -> not ported, TF2 has no such command
//   edge surf's slot clear                         -> none, TF2 has no edge surf
// One deviation: the client hook gates on the start origin (< 8u) inside our sims too, not just
// the tick base -- PixelFinder / route calc sim a TELEPORTED hull on this cmd at the same tick base.

struct TungSnap_t
{
    int   cmd      = -1;
    int   tickBase = -1;
    Vec3  from{}, to{};
    bool  svDone = false;
    float yaw = 0.f, len = 0.f;
};
static TungSnap_t s_tung[128];

struct TungPlane_t
{
    float p = 0.f, d = 0.f;
    Vec3  n{}, at{};
    float a0 = 0.f;
};
static TungPlane_t s_tungLearn[16];
static int s_tungLearnN = 0, s_tungLearnNext = 0;
static int s_tungStance = -1, s_tungGrace = 0;
static Vec3 s_tungMins{}, s_tungMaxs{};   // start hull, read per cmd off the restored frame

static constexpr float k_tungDuckH  = 62.f;   // TF2 duck hull height (csgo 54)
static constexpr float k_tungStandH = 82.f;   // TF2 standing hull height (csgo 72)

static float TungDuckAmt(void* p)
{
    if (g_offDucking > 0 && EntB(p, g_offDucking)) return 0.5f;
    return (EntI(p, g_offFlags) & FL_DUCKING) ? 1.f : 0.f;
}

static void TungTrace(void* pLocal, const Vec3& a, const Vec3& b, const Vec3& mn, const Vec3& mx, GameTrace_t& tr)
{
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    tr = GameTrace_t{};
    TraceHull(a, b, mn, mx, MASK_PLAYERSOLID, &fil, &tr);
}

/* largest hull move along dir that the trace still calls clear, bisected
   over the float's bit pattern (exact to the ulp). -1 = nothing within 1u */
static float TungWallGap(void* pLocal, const Vec3& from, const Vec3& dir)
{
    const auto clear = [&](const float d) {
        GameTrace_t tr;
        TungTrace(pLocal, from, Vec3(from.x + dir.x * d, from.y + dir.y * d, from.z), s_tungMins, s_tungMaxs, tr);
        return !tr.startsolid && !tr.DidHit();
    };
    if (clear(1.f))
        return -1.f;
    const auto to_f = [](const uint32_t b) { float f; memcpy(&f, &b, 4); return f; };
    const float one = 1.f;
    uint32_t lo = 0u, hi;
    memcpy(&hi, &one, 4);
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo + 1u) / 2u;
        if (clear(to_f(mid)))
            lo = mid;
        else
            hi = mid - 1u;
    }
    return to_f(lo);
}

static bool TungSlantTouch(void* pLocal, const Vec3& at)
{
    const float dx[5] = { 0.05f, -0.05f, 0.f, 0.f, 0.f }, dy[5] = { 0.f, 0.f, 0.05f, -0.05f, 0.f }, dz[5] = { 0.f, 0.f, 0.f, 0.f, -0.05f };
    for (int k = 0; k < 5; k++) {
        GameTrace_t tr;
        TungTrace(pLocal, at, Vec3(at.x + dx[k], at.y + dy[k], at.z + dz[k]), s_tungMins, s_tungMaxs, tr);
        if (const float nz = fabsf(tr.plane.normal.z); tr.DidHit() && !tr.startsolid && nz > 0.01f && nz < 0.99f)
            return true;
    }
    return false;
}

static bool TungStartsOnPin()
{
    return !(s_prePredFlags & FL_ONGROUND) && fabsf(s_prePredVel.z - TargetPredictZVel()) < as::k_pinTol;
}

/* diagonal wall / seam noise zeroes the whole vector, gravity then lands vz exactly on the pin: xy 0 = hang, not a surf */
static bool TungStartStuck() { return TungStartsOnPin() && s_prePredVel.Length2D() < 1.f; }

/* botox eb_cmd_taken + tb acted + air stuck + ps latch. Tung runs before all of them, so these
   are last cmd's flags -- botox reads the same */
static bool TungCmdTaken()
{
    return s_textureBugHit || s_tbOwnsCmd || s_headSurfHit || s_headBounceHit || s_ab.ownsCmd || s_esActive
        || s_airStuckHit || s_asHolding || s_psShouldSurf || s_psShouldUnduck;
}

// ---- the snap, in the ProcessMovement hooks (fakemove.inl calls these before the move) ----
static void TungApplySv(CMoveData* mv)
{
    __try {
        if (g_eject || !mv) return;
        const int local = GetLocalPlayerIndex();
        if (local <= 0 || static_cast<int>(mv->m_nPlayerHandle & 0xFFFF) != local)
            return;
        for (auto& s : s_tung) {
            if (s.cmd < 0 || s.svDone || (mv->m_vecAbsOrigin - s.from).Length() > 0.05f)
                continue;
            DebugLog("TUNG: server run cmd=%d z %.3f -> %.3f xy %.5f", s.cmd, mv->m_vecAbsOrigin.z, s.to.z, (s.to - s.from).Length2D());
            mv->m_vecAbsOrigin         = s.to;
            mv->m_bGameCodeMovedPlayer = true;
            s.svDone                   = true;
            return;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void TungApplyCl(void* player, CMoveData* mv)
{
    __try {
        if (g_eject || !player || !mv || g_offConstraintEnt <= 0 || g_offTickBase <= 0) return;
        const int idx = GetLocalPlayerIndex();
        if (idx <= 0 || player != GetClientEntity(idx)) return;
        // m_pCurrentCommand = m_hConstraintEntity - 8: Pred_Simulate and the engine's RunCommand both set it
        const UserCmd_t* cur = *reinterpret_cast<UserCmd_t**>(reinterpret_cast<uintptr_t>(player) + g_offConstraintEnt - 8);
        if (!cur) return;
        const TungSnap_t& s = s_tung[cur->command_number & 127];
        if (s.cmd != cur->command_number) return;
        const float d = (mv->m_vecAbsOrigin - s.from).Length();
        if (d > 8.f || (g_predInSim && EntI(player, g_offTickBase) != s.tickBase))
            return;
        if (!g_predInSim) {
            static int logged = -1;
            if (logged != s.cmd)
                DebugLog("TUNG: engine run cmd=%d tb=%d/%d d=%.3f z %.3f -> %.3f", s.cmd, EntI(player, g_offTickBase), s.tickBase, d,
                         mv->m_vecAbsOrigin.z, s.to.z);
            logged = s.cmd;
        }
        /* same start = exact floats (the wall pull is ulp exact), a corrected start keeps the pull as an offset */
        if (d < 0.05f)
            mv->m_vecAbsOrigin = s.to;
        else
            mv->m_vecAbsOrigin = Vec3(mv->m_vecAbsOrigin.x + s.to.x - s.from.x, mv->m_vecAbsOrigin.y + s.to.y - s.from.y, s.to.z);
        mv->m_bGameCodeMovedPlayer = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- the search, per cmd before Pred_Start: the snap has to be in every sim after it ----
static void TungSurf(void* pLocal, UserCmd_t* cmd)
{
    const bool on = On(g_tungSurf, g_tungSurfKey, g_tungSurfMode) && !g_eject;
    static bool was_on = false;
    if (on != was_on)
        DebugLog("TUNG: bind %s", on ? "on" : "off");
    was_on = on;

    if (GetTickCount64() - s_fmServerSeenMs >= 1000) {   // not hosting: the server never runs the snap
        for (auto& s : s_tung)
            s.cmd = -1;
        s_tungStance = -1;
        return;
    }

    const int frame = PredCommandsPredicted() - 1;
    RestoreToFrame(frame);
    s_tungMins = EntV3(pLocal, g_offMins);
    s_tungMaxs = EntV3(pLocal, g_offMaxs);

    int &s_stance = s_tungStance, &s_stance_grace = s_tungGrace;
    if (s_psBail && s_stance >= 0) {
        DebugLog("TUNG: stance bail %s", s_stance ? "duck" : "stand");
        s_stance = -1;
    }
    if (s_stance >= 0) {
        const bool stuck  = TungStartStuck();
        const bool riding = !stuck && TungStartsOnPin() && !TungSlantTouch(pLocal, s_prePredOrigin);
        if (riding || (!stuck && s_stance_grace > 0)) {
            cmd->buttons   = (cmd->buttons & ~IN_DUCK) | s_stance;
            s_stance_grace = riding ? 0 : s_stance_grace - 1;
        } else {
            DebugLog("TUNG: stance release %s", s_stance ? "duck" : "stand");
            s_stance = -1;
        }
    }

    if (!on || EntMoveType(pLocal) != MOVETYPE_WALK)
        return;
    static float s_ban = FLT_MAX;
    if ((s_prePredFlags & FL_ONGROUND) || s_prePredVel.z > 0.f)
        s_ban = FLT_MAX;
    else if (TungStartsOnPin() && floorf(s_prePredOrigin.z) < s_ban) {
        s_ban = floorf(s_prePredOrigin.z);
        DebugLog("TUNG: ban plane>=%.0f", s_ban);
    }
    if (TungCmdTaken())
        return;
    if (s_psBail)
        return;
    const Vec3 o0 = s_prePredOrigin;
    const Vec3 v0 = s_prePredVel;
    static bool s_chain = false;
    if ((s_prePredFlags & FL_ONGROUND) || v0.z > 0.f) {
        s_chain = false;
        return;
    }
    Vec3 n{};
    if (!PsWallTouch(pLocal, n) && !PsWallAhead(pLocal, 1, &n))
        return;
    if (TbBudgetLeftUs() <= 0ll)
        return;
    const float ipt = TickInterval();
    AsClock budget;
    budget.Start(0.25f, ipt);
    const int cn = cmd->command_number;
    if (!s_chain && s_tung[(cn - 1) & 127].cmd == cn - 1 && s_tung[(cn - 2) & 127].cmd == cn - 2) {
        s_chain = true;
        DebugLog("TUNG: chain stop z=%.3f vz=%.2f", o0.z, v0.z);
    }

    constexpr float k_eps = 0.03125f;
    const float pin = TargetPredictZVel();
    const auto pin_vz = [&]() {
        return !(EntI(pLocal, g_offFlags) & FL_ONGROUND) && fabsf(EntV3(pLocal, g_offVelocity).z - pin) < as::k_pinTol;
    };
    const auto pinned = [&]() { return pin_vz() && EntV3(pLocal, g_offVelocity).Length2D() >= 1.f; };
    const auto stuck  = [&]() { return pin_vz() && EntV3(pLocal, g_offVelocity).Length2D() < 1.f; };
    RestoreToFrame(frame);
    const int base = EntI(pLocal, g_offTickBase);
    EbTimeGuard clock(base);   // sims integrate gpGlobals->frametime: a tick, not a frame
    const auto sim = [&](const int k) {
        EntSetI(pLocal, g_offTickBase, base + k);
        Pred_Simulate(pLocal, cmd);
    };
    TungSnap_t& slot = s_tung[cn & 127];
    slot.cmd       = -1;
    const float a0 = TungDuckAmt(pLocal);
    if (a0 > 0.f && a0 < 1.f)
        return;

    const float live_fwd = cmd->forwardmove, live_side = cmd->sidemove;
    const float vy   = DEG2RADF(cmd->viewangles.y);
    const float wx   = live_fwd * cosf(vy) + live_side * sinf(vy), wy = live_fwd * sinf(vy) - live_side * cosf(vy);
    const float wlen = sqrtf(wx * wx + wy * wy);
    const float nlen = sqrtf(n.x * n.x + n.y * n.y);
    const float away = wlen > 0.01f && nlen > 0.01f ? (wx * n.x + wy * n.y) / (wlen * nlen) : 0.f;
    const bool press = g_tungPress && nlen > 0.01f && away <= 0.5f;
    const float press_yaw = RAD2DEGF(atan2f(-n.y, -n.x));
    const auto use_press  = [&](const bool on_) {
        if (on_)
            as::MoveTowardYaw(cmd->viewangles.y, press_yaw, 450.f, cmd->forwardmove, cmd->sidemove);
        else {
            cmd->forwardmove = live_fwd;
            cmd->sidemove    = live_side;
        }
    };
    const auto commit_move  = [&](const float yaw, const float len) {
        as::MoveTowardYaw(cmd->viewangles.y, yaw, len, cmd->forwardmove, cmd->sidemove);
    };
    const auto commit_press = [&]() { commit_move(press_yaw, 450.f); };
    const Vec3 nu = nlen > 0.01f ? Vec3(n.x / nlen, n.y / nlen, 0.f) : Vec3(0.f, 0.f, 0.f);

    {
        static char s_map[128] = {};
        const char* map = EngineLevelName();
        if (map && strncmp(map, s_map, sizeof(s_map) - 1) != 0) {
            strncpy_s(s_map, map, _TRUNCATE);
            s_tungLearnN    = 0;
            s_tungLearnNext = 0;
        }
    }
    /* any stance: hull mins z = 0 both ways, feet = origin, so a seam ridden ducked sits on the same plane standing */
    const auto learned_at = [&](const float p) -> TungPlane_t* {
        if (nlen <= 0.01f)
            return nullptr;
        for (int i = 0; i < s_tungLearnN; i++) {
            TungPlane_t& l = s_tungLearn[i];
            if (l.p == p && l.n.x * nu.x + l.n.y * nu.y > 0.99f && fabsf(nu.x * o0.x + nu.y * o0.y - l.d) < 4.f
                && Vec3(o0.x - l.at.x, o0.y - l.at.y, 0.f).Length2D() < 1024.f)
                return &l;
        }
        return nullptr;
    };

    if (TungStartsOnPin()) {
        if (TungSlantTouch(pLocal, o0) || TungStartStuck()) {
            static int s_slant_cn = -1;
            if (s_slant_cn != cn - 1)
                DebugLog("TUNG: %s ride let go z=%.3f vz=%.2f spd=%.1f", TungStartStuck() ? "stuck" : "slant", o0.z, v0.z, v0.Length2D());
            s_slant_cn = cn;
            return;
        }
        s_chain = false;
        if (const float p = roundf(o0.z - 0.5f * k_eps); nlen > 0.01f && fabsf(o0.z - (p + 0.5f * k_eps)) < 0.01f) {
            if (TungPlane_t* l = learned_at(p)) {
                l->at = o0;
                l->a0 = a0;
            } else {
                s_tungLearn[s_tungLearnNext] = { p, nu.x * o0.x + nu.y * o0.y, nu, o0, a0 };
                s_tungLearnNext = (s_tungLearnNext + 1) % 16;
                s_tungLearnN    = (std::min)(s_tungLearnN + 1, 16);
                DebugLog("TUNG: learn plane=%.0f n=%.2f,%.2f duck=%.0f count=%d", p, nu.x, nu.y, a0, s_tungLearnN);
            }
        }
        static bool held = false;
        if (!press) {
            held = false;
            return;
        }
        const auto keeps = [&]() {
            bool ok = true;
            for (int k = 0; k < 2 && ok; k++) {
                sim(k);
                ok = pinned();
            }
            RestoreToFrame(frame);
            return ok;
        };
        bool now = false;
        if (!keeps()) {
            use_press(true);
            now = keeps();
            use_press(false);
            if (now)
                commit_press();
        }
        if (now != held)
            DebugLog("TUNG: hold %s fwd=%.0f side=%.0f away=%.2f", now ? "press" : "keys", cmd->forwardmove, cmd->sidemove, away);
        held = now;
        return;
    }

    /* live keys pin 2 ticks running before landing = the real surf is coming: never snap over it
       (same test as auto ps hunt). one log per yield run */
    {
        int run = 0, found = -1;
        for (int z = 0; z <= as::TicksScaled(8, ipt) && found < 0; z++) {
            sim(z);
            if (EntI(pLocal, g_offFlags) & FL_ONGROUND)
                break;
            run = pinned() ? run + 1 : 0;
            if (run == 2)
                found = z - 1;
        }
        RestoreToFrame(frame);
        static int s_yield_cn = -1;
        if (found >= 0) {
            if (s_yield_cn != cn - 1)
                DebugLog("TUNG: yield real surf in=%d z=%.3f vz=%.2f", found, o0.z, v0.z);
            s_yield_cn = cn;
            return;
        }
    }

    if (const TungSnap_t& prev = s_tung[(cn - 1) & 127]; prev.cmd == cn - 1) {
        bool ok = false;
        const bool can = g_tungPress && prev.len > 0.01f && away <= 0.5f;
        if (can) {
            commit_move(prev.yaw, prev.len);
            ok = true;
            for (int k = 0; k < 2 && ok; k++) {
                sim(k);
                ok = pinned();
            }
            RestoreToFrame(frame);
            cmd->forwardmove = live_fwd;
            cmd->sidemove    = live_side;
        }
        const float live_yaw = wlen > 0.01f ? RAD2DEGF(atan2f(wy, wx)) : 0.f;
        DebugLog("TUNG: snap keys %s live yaw=%.1f len=%.0f snap yaw=%.1f len=%.0f z=%.3f vz=%.2f", !can ? "off" : ok ? "hold" : "fail",
                 live_yaw, wlen, prev.yaw, prev.len, o0.z, v0.z);
        if (ok) {
            commit_move(prev.yaw, prev.len);
            return;
        }
    }

    const bool pressing = press && away >= -0.3f;
    use_press(pressing);

    const float fall    = (std::max)(0.f, -(v0.z + pin) * ipt);
    const float snap    = std::clamp(g_tungSnap, 0.f, 32.f);
    const int live_btn  = cmd->buttons;
    const Vec3 dir = nlen > 0.01f ? Vec3(-n.x / nlen, -n.y / nlen, 0.f) : Vec3(0.f, 0.f, 0.f);
    /* ~30 traces: only when a learned / lip plane flies a pull */
    float gap_v       = -2.f;
    const auto gap_of = [&]() {
        if (gap_v == -2.f)
            gap_v = nlen > 0.01f ? TungWallGap(pLocal, o0, dir) : -1.f;
        return gap_v;
    };
    int tried[2] = {}, count[2] = {}, n_cand[2] = {}, rest_tried = 0, f_path = 0, f_t0 = 0, f_t1 = 0, f_t2 = 0, f_slant = 0, f_stuck = 0, pulls = 0, flip_took = -1, f_stance = 0;
    int lips[2] = {}, lip_fail[3] = {};
    int banned = 0, learned = 0;
    int regaps = 0, f_pull_blk = 0;
    float z_nat0 = o0.z;
    for (int st = 0; st < 2 && !(st && budget.Expired()); st++) {
        cmd->buttons     = st ? live_btn ^ IN_DUCK : live_btn;
        const int stance = cmd->buttons & IN_DUCK;
        const auto take  = [&]() {
            use_press(false);
            if (pressing)
                commit_press();
            if (st) {
                s_stance       = stance;
                s_stance_grace = 1;
            }
        };
        sim(0);
        const Vec3 e        = EntV3(pLocal, g_offOrigin);
        const Vec3 ve       = EntV3(pLocal, g_offVelocity);
        const float z_nat   = e.z;
        const bool nat_pin  = pinned();
        const bool nat_land = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
        const bool e_duck   = (EntI(pLocal, g_offFlags) & FL_DUCKING) != 0;
        /* stance changed inside the tick = mid crouch catch: never snap / flip into it */
        const bool settled   = TungDuckAmt(pLocal) == a0;
        const bool nat_slant = nat_pin && TungSlantTouch(pLocal, e);
        if (st)
            flip_took = e_duck == (stance != 0) ? 1 : 0;
        RestoreToFrame(frame);
        if (!st)
            z_nat0 = z_nat;
        if (nat_land) {
            if (!st) {
                use_press(false);
                cmd->buttons = live_btn;
                return;
            }
            continue;
        }
        if (nat_pin && (!st || settled)) {
            if (nat_slant && (pressing || st)) {
                if (st)
                    continue;
                use_press(false);
                return;
            }
            take();
            if (pressing || st)
                DebugLog("TUNG: catch by %s z=%.3f vz=%.2f", st ? (stance ? "duck" : "stand") : "press", o0.z, v0.z);
            return;
        }
        if (!settled) {
            f_stance++;
            continue;
        }

        float planes[128];
        int& n_pl = count[st];
        for (float p = ceilf(z_nat - snap - 0.5f * k_eps); p + 0.5f * k_eps - z_nat <= fall + snap && n_pl < 128; p += 1.f) {
            if (p >= s_ban) {
                banned++;
                continue;
            }
            if (const float shift = p + 0.5f * k_eps - z_nat; snap > 0.f || shift > 0.f)
                planes[n_pl++] = p;
        }
        std::sort(planes, planes + n_pl, [z_nat](const float a, const float b) { return fabsf(a - z_nat) < fabsf(b - z_nat); });

        const auto outside = [&](const float p) {
            const float shift = p + 0.5f * k_eps - z_nat;
            return shift <= 0.f ? -shift : (std::max)(0.f, shift - fall);
        };
        struct Cand_t
        {
            float  p;
            int    pull;
            bool   lip;
            double rank;
        };
        const Vec3 hmin = s_tungMins;
        const Vec3 hmax(s_tungMaxs.x, s_tungMaxs.y, e_duck ? k_tungDuckH : k_tungStandH);
        /* lip probe: catch = tick 1 hull end enters a lower brush through its top face (reflect clip, vz 0), so the face
           under p must stick out past every face the hull spans above p. feet slice per band (exact same frac = flush),
           then hull confirm at p (stance height). columns at the natural end + one tick on. -1 = start solid / no wall */
        const Vec3 at_xy[2] = { e, Vec3(e.x + ve.x * ipt, e.y + ve.y * ipt, 0.f) };
        const int k_lo = static_cast<int>(ceilf(z_nat - snap - 0.5f * k_eps)) - 1;
        float band_f[2][130];
        for (auto& c : band_f)
            for (float& f : c)
                f = -2.f;
        const auto probe = [&](const Vec3& at, const float z, const Vec3& mn, const Vec3& mx) {
            GameTrace_t tr;
            TungTrace(pLocal, Vec3(at.x, at.y, z), Vec3(at.x + dir.x * 2.f, at.y + dir.y * 2.f, z), mn, mx, tr);
            return tr.startsolid ? -1.f : tr.fraction;
        };
        const auto band = [&](const int c, const int k) {
            const auto run = [&]() {
                return probe(at_xy[c], static_cast<float>(k) + 0.0625f, Vec3(hmin.x, hmin.y, 0.f), Vec3(hmax.x, hmax.y, 0.875f));
            };
            if (k - k_lo < 0 || k - k_lo >= 130)
                return run();
            float& f = band_f[c][k - k_lo];
            if (f == -2.f)
                f = run();
            return f;
        };
        const auto lip_of = [&](const float p) {
            if (nlen <= 0.01f)
                return false;
            const int k = static_cast<int>(p);
            for (int c = 0; c < 2; c++) {
                const float below = band(c, k - 1), above = band(c, k);
                if (below < 0.f || above < 0.f || below >= above)
                    continue;
                if (const float hull = probe(at_xy[c], p + 0.5f * k_eps, hmin, hmax); hull >= 0.f && below < hull)
                    return true;
            }
            return false;
        };
        /* tiers learned / lip / crossed / rest. a lip is real geometry: nearest first, no grid guess. pulls only on
           learned / lip (blank planes never needed one). rest = blind, capped 8 tries */
        const auto rank_of = [&](const float p, const bool lip) {
            const int tier = learned_at(p) ? 0 : lip ? 1 : outside(p) == 0.f ? 2 : 3;
            return tier * 1e6 + (tier == 1 ? 0 : TungGridRank(p)) * 1e4 + static_cast<double>(outside(p));
        };
        Cand_t cands[256];
        int n_c = 0;
        for (int i = 0; i < n_pl && !s_chain; i++) {
            const bool lip = lip_of(planes[i]);
            const double r = rank_of(planes[i], lip);
            learned += r < 1e6;
            lips[st] += lip;
            for (int pull = 0; pull < (r < 2e6 && nlen > 0.01f ? 2 : 1); pull++)
                cands[n_c++] = { planes[i], pull, lip, r };
        }
        n_cand[st] = n_c;
        std::stable_sort(cands, cands + n_c, [](const Cand_t& a, const Cand_t& b) { return a.rank < b.rank; });

        /* predicted ride length ranks proven snaps: a short one waits up to 8 more tries for a longer ride */
        const int ride_max = 2 + as::TicksScaled(6, ipt);
        int cap            = 32;
        struct
        {
            TungSnap_t   slot;
            float        p, shift, pull_gap;
            bool         regap, lip;
            TungPlane_t* l;
            int          ride, at_try;
        } best{};
        for (int i = 0; i < n_c && !s_chain && tried[st] < cap && !budget.Expired(); i++) {
            const bool rest = cands[i].rank >= 3e6;
            if (rest && rest_tried >= 8)
                break;
            const float p     = cands[i].p;
            const int pull    = cands[i].pull;
            const float shift = p + 0.5f * k_eps - z_nat;
            const float gap   = pull ? gap_of() : -1.f;
            if (pull && gap <= 0.f)
                continue;
            {
                GameTrace_t tr;
                TungTrace(pLocal, o0, Vec3(o0.x, o0.y, o0.z + shift), s_tungMins, s_tungMaxs, tr);
                if (tr.startsolid || tr.fraction < 1.f) {
                    f_path++;
                    continue;
                }
            }
            Vec3 to(o0.x, o0.y, o0.z + shift);
            float pull_gap = gap;
            if (pull) {
                const Vec3 at = to;
                to = Vec3(at.x + dir.x * gap, at.y + dir.y * gap, at.z);
                /* the wall at the snapped height may sit elsewhere: pull must stay clear there too */
                GameTrace_t tr;
                TungTrace(pLocal, at, to, s_tungMins, s_tungMaxs, tr);
                if (tr.startsolid || tr.DidHit()) {
                    pull_gap = tr.startsolid ? -1.f : TungWallGap(pLocal, at, dir);
                    if (pull_gap <= 0.f) {
                        f_pull_blk++;
                        continue;
                    }
                    regaps++;
                    to = Vec3(at.x + dir.x * pull_gap, at.y + dir.y * pull_gap, at.z);
                }
            }
            tried[st]++;
            rest_tried += rest;
            pulls += pull;
            slot = { cn, base, o0, to, false, pressing ? press_yaw : RAD2DEGF(atan2f(wy, wx)), pressing ? 450.f : wlen };
            int bad = -1, ride = 2;
            for (int k = 0; k < 3 && bad < 0; k++) {
                sim(k);
                if (k > 0 && stuck())
                    bad = 4;
                else if ((k == 0 ? (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0 : !pinned()) || TungDuckAmt(pLocal) != a0)
                    bad = k;
                else if (k == 2 && TungSlantTouch(pLocal, EntV3(pLocal, g_offOrigin)))
                    bad = 3;
            }
            /* ride on with the same keys: a slide that hits a seam / diagonal face hangs a few ticks later */
            for (int k = 3; bad < 0 && k < 3 + as::TicksScaled(6, ipt); k++, ride++) {
                sim(k);
                if (stuck())
                    bad = 4;
                else if (!pinned() || TungDuckAmt(pLocal) != a0)
                    break;
            }
            RestoreToFrame(frame);
            if (bad < 0) {
                if (ride > best.ride) {
                    if (!best.ride)
                        cap = (std::min)(cap, tried[st] + 8);
                    best.slot = slot; best.p = p; best.shift = shift; best.pull_gap = pull ? pull_gap : 0.f;
                    best.regap = pull && pull_gap != gap; best.lip = cands[i].lip; best.l = learned_at(p);
                    best.ride = ride; best.at_try = tried[st];
                }
                slot.cmd = -1;
                if (ride >= ride_max)
                    break;
                continue;
            }
            (bad == 0 ? f_t0 : bad == 1 ? f_t1 : bad == 2 ? f_t2 : bad == 3 ? f_slant : f_stuck)++;
            if (cands[i].lip && bad < 3)
                lip_fail[bad]++;
            slot.cmd = -1;
        }
        if (best.ride) {
            slot = best.slot;
            take();
            const TungPlane_t* l = best.l;
            DebugLog("TUNG: snap plane=%.0f shift=%.3f pull=%.6f regap=%d lip=%d learned=%d grid=%d z=%.3f nat=%.3f fall=%.2f vz=%.2f spd=%.1f duck=%d/%d flip=%d "
                     "try=%d/%d press=%d away=%.2f ride=%d/%d",
                     best.p, best.shift, best.pull_gap, best.regap ? 1 : 0, best.lip ? 1 : 0, !l ? 0 : l->a0 == a0 ? 1 : 2, TungGridRank(best.p), o0.z, z_nat,
                     fall, v0.z, v0.Length2D(), stance ? 1 : 0, (s_prePredFlags & FL_DUCKING) ? 1 : 0, st, best.at_try,
                     tried[st], pressing ? 1 : 0, away, best.ride, ride_max);
            return;
        }
        /* held stance settled: the flip always changes stance in its tick = mid crouch, never snaps. only a held
           transition leaves the flip as "keep stance" */
        if (settled)
            break;
    }
    cmd->buttons = live_btn;
    use_press(false);

    static unsigned long long next = 0ull;
    if (const unsigned long long now = GetTickCount64(); now >= next) {
        DebugLog("TUNG: miss us=%lld ban=%d learned=%d tried=%d+%d/%d+%d planes=%d+%d rest=%d lips=%d+%d lip_fail=%d/%d/%d chain=%d pulls=%d gap=%.6f regap=%d "
                 "pull_blk=%d flip_took=%d mid_crouch=%d path=%d t0=%d t1=%d t2=%d slant=%d stuck=%d z=%.3f nat=%.3f fall=%.2f vz=%.2f spd=%.1f duck=%d/%d press=%d away=%.2f",
                 budget.UsedUs(), banned, learned,
                 tried[0], tried[1], n_cand[0], n_cand[1], count[0], count[1], rest_tried, lips[0],
                 lips[1], lip_fail[0], lip_fail[1], lip_fail[2], s_chain ? 1 : 0, pulls, gap_v, regaps, f_pull_blk, flip_took, f_stance, f_path, f_t0, f_t1,
                 f_t2, f_slant, f_stuck,
                 o0.z, z_nat0, fall, v0.z, v0.Length2D(), (live_btn & IN_DUCK) ? 1 : 0,
                 (s_prePredFlags & FL_DUCKING) ? 1 : 0, pressing ? 1 : 0, away);
        next = now + 250ull;
    }
}
