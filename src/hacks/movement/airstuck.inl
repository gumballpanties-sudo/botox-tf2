// airstuck.inl -- botox csgo-sdk hacks/movement/air_stuck.cpp, 1:1. Textually included by
// movement.cpp (same translation unit: Pred_Simulate / RestoreToFrame / TraceHull / TraceLine /
// EntV3 / TB_WallSupportDistance / s_airStuckHit are this file's statics).
//
// botox -> tf2 shims, nothing else differs:
//   g_prediction.begin+end            -> Pred_Simulate (one tick, state persists across sims)
//   restore_entity_to_predicted_frame -> RestoreToFrame (-1 = SLOT_ORIGINALDATA, as the engine's)
//   set_abs_origin / get_origin()=    -> AsSetOrigin (g_fnSetAbsOrigin + the netvar)
//   get_velocity()=                   -> EntSetV3 (abs velocity recomputes off the restore's dirty
//                                        flag, so every velocity write here follows a restore --
//                                        clarity robust() restores per probe for that reason)
//   get_engine_target_predict_z_velocity / is_target_predict_z_velocity -> TargetPredictZVel / IsTargetVz
//   c_trace_filter_trace_type_everything_filter_props -> TraceFilterWorldOnly (world brushes only)
//   n_tick::c_sim_budget              -> AsClock (QPC, capped at what is left of the cmd clock)
//   m_last_command_ack                -> Aim_LastCommandAck() (aim half's CClientState)
//   get_surface_friction              -> m_surfaceFriction off the prediction datamap
//   movement_add_window               -> DebugLog (the movement indicator already lights on the hold)
//   0.34 ducked speed crop            -> 0.33333333 (gamemovement.cpp:4310 HandleDuckingSpeedCrop)
#include "airstuck_pure.h"
#include <cfloat>
#include <cstring>

int Aim_LastCommandAck();   // aim/aim.cpp

// ---- the stamp (botox g_air_stuck_* globals): what AS sent, in the frame it proved it in ----
static bool  s_asStampValid = false;
static float s_asStampYaw = 0.f, s_asStampForward = 0.f, s_asStampSide = 0.f, s_asStampPitch = 0.f;
static int   s_asStampButtons = 0;
static Vec3  s_asAuthoredView{};
static bool  s_asAuthoredViewValid = false;
static bool  s_asHolding = false;   // m_air_stuck_data.m_air_stuck: a proven pin went out this cmd

static struct {
    bool valid = false;
    bool exact = false;
    Vec3 origin{};
    Vec3 velocity{};
} s_asProof;

static struct {
    bool valid = false;
    Vec3 origin{};
} s_asExpect;

/* last sent pin, view latched: pins live on wishdir float bits ( AngleVectors + VectorNormalize, gamemovement.cpp:2015-2032 ), so live pitch/yaw must never reach it */
static struct {
    bool  valid = false;
    float pitch = 0.0f, yaw = 0.0f, fwd = 0.0f;
    int   ticks = 0;
} s_asHold;

// TraceFilterWorldOnly (the botox world-only filter) lives in pixelsurf.inl, included first

static int AsEntIndex(void* pEnt)
{
    if (!pEnt) return -1;
    __try {
        unsigned int* pH = VCall<unsigned int*>(pEnt, 2);   // IHandleEntity::GetRefEHandle
        return pH ? (int)(*pH & 0xFFFF) : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

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

/* online the engine keeps its own START while the server's END differs < 0.0625u / 0.5u/s ( cl_pred_optimize 2, c_baseentity.cpp:656/681,
   prediction.cpp:1681 ); pins are exact floats, so armed = full re-predict off the server snapshot every packet ( offline already is ).
   log: server END of the acked cmd vs the START we built on */
static void AS_SyncArm(const bool on)
{
    static int saved = -1;
    static bool armed = false;
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
    AS_SyncArm(armed || s_wcSyncWant);   // botox sync_arm owners: as + wc ( wall climb's flush parks are sub-ulp too )

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

// ============================================================================
// AIR STUCK, cumidere style -- 1:1 port of Documents\!\cumidere tf2.dll misc_airstuck (bool
// 0x49e36b), body FUN_1801cd900. Its createmove (FUN_1801f9b10) runs it right after its tb
// (FUN_1801cbee0) on the pre-prediction state, same as our pipeline order (TB then AS), so it
// works off the restore base. Same wall scan (FUN_1801cb3c0) and gap (FUN_1801cd340) as its tb
// = TBC_FindWall / TBC_Gap (texturebug.inl). What differs from its tb:
//   grid    10 forwards x graze 86.5..89.5 step 0.5 = 70 sims max (tb: 5 grazes, 50)
//   accept  xy < 0.1 AND |vz| on half gravity = a dead stop (tb takes any pin, a ride too),
//           else the flushest variant if its gap is under 0.2u, else the cmd untouched
// Shims: its movement fix (FUN_18022c2e0) -> as::StampRotate, the rotation AS_FinalStamp re-applies
// after the aimbot, so the bits the sim proved are the bits that go out. Last forward FLT_MAX ->
// 450: wish dir is scale-free and the air cap clamps the wish to 30 either way, FLT_MAX only risks
// inf/NaN in wishvel. Its first sim (plain cmd, vz read, never used) skipped.
// ============================================================================
static float s_ascScanStart = 0.f;   // DAT_1804d6f5c: direction the last wall was found at
static int   s_ascDiagTick  = -1000;

// entity on the restore base in and out
static void AirStuckCumidere(void* pLocal, UserCmd_t* cmd)
{
    const int nBase = PredCommandsPredicted();
    RestoreToFrame(nBase - 1);

    const Vec3 org  = EntV3(pLocal, g_offOrigin);
    const Vec3 mins = EntV3(pLocal, g_offMins);
    const Vec3 maxs = EntV3(pLocal, g_offMaxs);
    const Vec3 vel  = EntV3(pLocal, g_offVelocity);

    GameTrace_t wall{};
    float ang = 0.f;
    if (!TBC_FindWall(pLocal, org, mins, maxs, s_ascScanStart, wall, ang)) { s_ascScanStart = 0.f; return; }
    s_ascScanStart = ang;

    const float wall_yaw = NormalizeAngle(RAD2DEGF(atan2f(-wall.plane.normal.y, -wall.plane.normal.x)));
    const float sign     = as::CumiSign(RAD2DEGF(atan2f(vel.y, vel.x)), wall_yaw);
    const float half     = TargetPredictZVel();   // FUN_1801cade0
    const Vec3  view     = cmd->viewangles;

    static constexpr float k_fwd[10] = { 1.f, 0.875f, 0.75f, 0.5f, 1.f, 2.125f, 2.875f, 5.f, 15.f, 450.f };
    bool  found = false;
    float best_gap = 1.f, out_yaw = view.y, out_fwd = 0.f, out_off = 0.f;
    float last_vz = 0.f, last_xy = 0.f;
    int   sims = 0;
    for (int i = 0; i < 10 && !found; ++i)
    {
        for (float off = 86.5f; off < 90.f; off += 0.5f)
        {
            if (SimsLeft() <= 0) break;
            const float yaw = wall_yaw + off * sign;
            UserCmd_t fake = *cmd;
            as::StampRotate(view.y, yaw, k_fwd[i], 0.f, fake.forwardmove, fake.sidemove);

            RestoreToFrame(nBase - 1);
            Pred_Simulate(pLocal, &fake);
            ++sims;
            const Vec3  v   = EntV3(pLocal, g_offVelocity);
            const float gap = TBC_Gap(pLocal, wall);
            last_vz = v.z;
            last_xy = v.Length2D();

            if (as::CumiStuck(v.z, last_xy, half))
            {
                found = true;
                out_yaw = yaw; out_fwd = k_fwd[i]; out_off = off;
                break;
            }
            if (gap < best_gap)
            {
                best_gap = gap;
                out_yaw = yaw; out_fwd = k_fwd[i]; out_off = off;
            }
        }
        if (SimsLeft() <= 0) break;
    }
    RestoreToFrame(nBase - 1);

    const bool hug = !found && best_gap < 0.2f;
    if (found || hug)
    {
        as::StampRotate(view.y, out_yaw, out_fwd, 0.f, cmd->forwardmove, cmd->sidemove);
        // the botox stamp, delivery only: AS_FinalStamp re-rotates it into the view that goes out
        s_asStampYaw     = out_yaw;
        s_asStampForward = out_fwd;
        s_asStampSide    = 0.f;
        s_asStampPitch   = view.x;
        s_asStampButtons = cmd->buttons;
        s_asStampValid   = true;
    }
    s_asHolding = found;

    if (g_debugLogEnable && DiagOk(s_ascDiagTick, found || hug))
        DebugLog("ASC: stuck=%d hug=%d gap=%.3f off=%.1f fwd=%.3f yaw=%.1f sims=%d vz=%.2f xy=%.1f lvz=%.2f lxy=%.2f "
                 "n=%.2f %.2f %.2f sign=%.0f ang=%.2f", (int)found, (int)hug, best_gap, out_off, out_fwd, out_yaw, sims,
                 vel.z, vel.Length2D(), last_vz, last_xy, wall.plane.normal.x, wall.plane.normal.y, wall.plane.normal.z,
                 sign, ang);
}

// ============================================================================
// AIR STUCK, aletherium style -- 1:1 port of Aletherium Misc.cpp CMisc::AirStuckCore
// (bSlantedOnly = false). Three phases a tick:
//   detect   TB_DetectWall: 16 hull traces `reach` out (72u), |nz| <= 0.10, no triggers, cached
//            angle re-tested first = ~1 trace steady state
//   hold     live vz already on the pin (+- catch eps): replay last tick's winning move, 0 sims
//   approach gap > flush tol: drive straight in, forward = gap * gain in [120, 650], 0 sims
//   catch    near flush: 5 near-tangent yaws (88..89.9, side flips per tick) x 5 forwards
//            (2..650), 2-sim confirm per candidate, capped at sim budget; no catch = the
//            flushest candidate goes out as a hug
// Shims: CorrectMovement -> as::StampRotate (+ the 450 clamp), IsTargetPredictZVelocity ->
// IsTargetVz, get_align -> TBC_Gap (same ray: 32u along -n at the hit z minus the support),
// StateGuard -> RestoreToFrame(nBase - 1) on every exit, tickcount -> cmd->tick_count.
// The 3 TB yield flags are ours (s_textureBugHit / s_headSurfHit / s_tbOwnsCmd). Sims also stop
// on the shared pool (SimsLeft) so a long sweep cannot starve the features after it.
// ============================================================================
static struct {
    int   hold_ticks = 0;
    float last_yaw   = FLT_MAX;   // FLT_MAX = no committed move yet
    float last_move  = FLT_MAX;
    float last_side  = 0.f;
    float cached_angle = -1.f;    // last tick's winning sweep angle, -1 = none
} s_asa;
static int s_asaDiagTick = -1000;

// Aletherium TB_DetectWall: nearest near-vertical world/brush face within `reach` of the centre
static bool AsA_DetectWall(void* pLocal, const Vec3& org, const Vec3& mins, const Vec3& maxs, float reach,
                           GameTrace_t& out, float& out_angle, float cached)
{
    TraceFilterWorldBrush fil; fil.Setup(pLocal);
    const auto test = [&](float c, float s, GameTrace_t& tr) {
        TraceHull(org, Vec3(org.x + c * reach, org.y + s * reach, org.z), mins, maxs, MASK_PLAYERSOLID, &fil, &tr);
        return tr.fraction < 1.f && fabsf(tr.plane.normal.z) <= 0.10f && !(tr.surface.flags & SURF_TRIGGER_MV);
    };
    if (cached >= 0.f)
    {
        GameTrace_t tr{};
        if (test(cosf(cached), sinf(cached), tr)) { out = tr; out_angle = cached; return true; }
    }
    constexpr float step = as::k_pi * 2.f / 16.f;
    float best = 1.f;
    bool hit = false;
    for (int k = 0; k < 16; ++k)
    {
        const float a = (float)k * step;
        GameTrace_t tr{};
        if (!test(cosf(a), sinf(a), tr) || tr.fraction >= best) continue;
        best = tr.fraction; out = tr; out_angle = a; hit = true;
    }
    return hit;
}

// entity on the restore base in and out
static void AirStuckAletherium(void* pLocal, UserCmd_t* cmd)
{
    if (s_textureBugHit || s_headSurfHit || s_tbOwnsCmd) { s_asa = {}; return; }

    const float k_eps    = std::clamp(g_asaCatchEps, 0.05f, 8.f);
    const float k_reach  = std::clamp(g_asaReach, 16.f, 128.f);
    const float k_flush  = std::clamp(g_asaFlushTol, 8.f, 96.f);
    const float k_gain   = std::clamp(g_asaDriftGain, 4.f, 40.f);
    const int   k_budget = std::clamp(g_asaSimBudget, 16, 128);

    const int nBase = PredCommandsPredicted();
    RestoreToFrame(nBase - 1);

    const Vec3 org  = EntV3(pLocal, g_offOrigin);
    const Vec3 vel  = EntV3(pLocal, g_offVelocity);
    const Vec3 mins = EntV3(pLocal, g_offMins);
    const Vec3 maxs = EntV3(pLocal, g_offMaxs);
    const Vec3 view = cmd->viewangles;

    GameTrace_t wall{};
    float ang = s_asa.cached_angle;
    if (!AsA_DetectWall(pLocal, org, mins, maxs, k_reach, wall, ang, s_asa.cached_angle))
    {
        s_asa.cached_angle = -1.f;
        s_asa.hold_ticks   = 0;
        return;   // not engaged: cmd untouched
    }
    s_asa.cached_angle = ang;

    const float wall_yaw = NormalizeAngle(RAD2DEGF(atan2f(-wall.plane.normal.y, -wall.plane.normal.x)));

    // CorrectMovement: a move in the wall frame, sent in the live view frame (view never moves)
    const auto apply_move = [&](float yaw, float fwd, float side) {
        float f = 0.f, s = 0.f;
        as::StampRotate(view.y, yaw, fwd, side, f, s);
        cmd->viewangles  = view;
        cmd->forwardmove = std::clamp(f, -450.f, 450.f);
        cmd->sidemove    = std::clamp(s, -450.f, 450.f);
    };
    const auto stamp = [&](float yaw, float fwd, float side) {
        s_asStampYaw = yaw; s_asStampForward = fwd; s_asStampSide = side;
        s_asStampPitch = view.x; s_asStampButtons = cmd->buttons; s_asStampValid = true;
    };
    const auto restore = [&]() {
        RestoreToFrame(nBase - 1);
        AsSetOrigin(pLocal, org);
        EntSetV3(pLocal, g_offVelocity, vel);
    };

    // hold: live vz already pinned = last tick's winner again, checked before any approach.
    // Aletherium replays it blind; 1 sim proves it (botox: never send a cmd that falls), a break
    // falls through to the search from this still-pinned START
    if (IsTargetVz(vel.z, k_eps))
    {
        const float yaw  = s_asa.last_yaw  != FLT_MAX ? s_asa.last_yaw  : wall_yaw;
        const float move = s_asa.last_move != FLT_MAX ? s_asa.last_move : 5.f;
        restore();
        apply_move(yaw, move, s_asa.last_side);
        Pred_Simulate(pLocal, cmd);
        const bool held = !(EntI(pLocal, g_offFlags) & FL_ONGROUND) && IsTargetVz(EntV3(pLocal, g_offVelocity).z, k_eps);
        restore();
        if (held)
        {
            apply_move(yaw, move, s_asa.last_side);
            stamp(yaw, move, s_asa.last_side);
            s_asHolding = true;
            if (s_asa.hold_ticks++ == 0)
                DebugLog("ASA: hold yaw=%.1f fwd=%.1f vz=%.2f", yaw, move, vel.z);
            return;
        }
        DebugLog("ASA: hold break t=%d yaw=%.1f fwd=%.1f", s_asa.hold_ticks, yaw, move);
    }
    s_asa.hold_ticks = 0;

    // approach: still gapped, drive in hard (0 sims). A catch needs the hull ON the wall after one
    // tick, so past one tick of reach (speed into the wall + the 30 u/s air wish + 1u slack) the
    // sweep can only ever return its drive-in fallback -- skip the 25-50 sims and drive outright.
    // k_flush stays the user's upper bound on that band
    const float align = TBC_Gap(pLocal, wall);
    const float v_in  = -(vel.x * wall.plane.normal.x + vel.y * wall.plane.normal.y);
    const float tick_reach = (fmaxf(v_in, 0.f) + 30.f) * TickInterval() + 1.f;
    if (align > fminf(k_flush, tick_reach))
    {
        const float drive = fminf(fmaxf(align * k_gain, 120.f), 650.f);
        apply_move(wall_yaw, drive, 0.f);
        cmd->buttons &= ~IN_DUCK;
        stamp(wall_yaw, drive, 0.f);
        if (g_debugLogEnable && DiagOk(s_asaDiagTick, false))
            DebugLog("ASA: drive gap=%.1f fwd=%.0f yaw=%.1f", align, drive, wall_yaw);
        return;
    }

    // near flush: budgeted near-tangent catch sweep
    const float side_sign = (cmd->tick_count % 2 == 0) ? 1.f : -1.f;
    static constexpr float k_yaws[] = { 88.0f, 88.5f, 89.0f, 89.5f, 89.9f };
    static constexpr float k_fwds[] = { 2.f, 10.f, 60.f, 250.f, 650.f };

    int   sims = 0;
    bool  found = false, fallback = false;
    float best_yaw = wall_yaw, best_fwd = 2.f, best_align = FLT_MAX;
    const auto budget_out = [&]() { return sims >= k_budget || SimsLeft() <= 0; };

    for (const float rel : k_yaws)
    {
        if (found || budget_out()) break;
        for (const float fwd : k_fwds)
        {
            if (budget_out()) break;
            restore();
            const float yaw = NormalizeAngle(wall_yaw + rel * side_sign);
            apply_move(yaw, fwd, 0.f);
            cmd->buttons &= ~IN_DUCK;   // catch is proven standing
            Pred_Simulate(pLocal, cmd); ++sims;
            if (IsTargetVz(EntV3(pLocal, g_offVelocity).z, k_eps))
            {
                Pred_Simulate(pLocal, cmd); ++sims;   // confirm: a real catch pins a 2nd tick
                if (IsTargetVz(EntV3(pLocal, g_offVelocity).z, k_eps))
                {
                    best_yaw = yaw; best_fwd = fwd; found = true;
                    break;
                }
                continue;   // one-tick blip
            }
            const float a = TBC_Gap(pLocal, wall);
            if (!fallback || a < best_align) { best_align = a; best_yaw = yaw; best_fwd = fwd; fallback = true; }
        }
    }
    restore();

    apply_move(best_yaw, best_fwd, 0.f);
    cmd->buttons &= ~IN_DUCK;
    stamp(best_yaw, best_fwd, 0.f);
    if (found)
    {
        s_asHolding    = true;
        s_asa.last_yaw = best_yaw; s_asa.last_move = best_fwd; s_asa.last_side = 0.f;
    }
    if (g_debugLogEnable && DiagOk(s_asaDiagTick, found))
        DebugLog("ASA: %s gap=%.2f yaw=%.1f fwd=%.0f sims=%d vz=%.2f n=%.2f %.2f side=%.0f",
                 found ? "catch" : "hug", found ? align : best_align, best_yaw, best_fwd, sims, vel.z,
                 wall.plane.normal.x, wall.plane.normal.y, side_sign);
}

static void AirStuck(void* pLocal, UserCmd_t* cmd)
{
    static float start_angle = 0.0f;
    static bool was_stuck    = false;

    s_asProof.valid = false;
    const bool hold_armed = s_asHold.valid;
    s_asHold.valid = false;
    s_asHolding = false;

    const bool feature = g_airStuck;
    const bool key     = MovementKeyOK(g_airStuckKey, g_airStuckMode);
    const bool alive   = EntAlive(pLocal);
    const bool cumi    = g_asStyle == 0;
    const bool aleth   = g_asStyle == 2;
    AS_Sync(pLocal, cmd, feature && key && alive && !cumi && !aleth);   // exact-pin sync is the botox style's

    if (!feature || !key || !alive || (EntI(pLocal, g_offFlags) & FL_ONGROUND)) {
        if (hold_armed)
            DebugLog("AS: end t=%d why=%s blk=%d", s_asHold.ticks, !feature ? "off" : !key ? "key" : !alive ? "dead" : "ground", (int)!s_bindAppActive);
        s_asHold.ticks  = 0;
        was_stuck       = false;
        s_asExpect.valid = false;
        s_asa = {};
        return;
    }
    const int move_type = EntMoveType(pLocal);
    if (move_type != MOVETYPE_WALK) {   // ladder / noclip / fly / observer ( TF2 has no ladders )
        was_stuck        = false;
        s_asExpect.valid = false;
        s_asa = {};
        return;
    }
    if (aleth) {
        s_asHold.ticks   = 0;
        was_stuck        = false;
        s_asExpect.valid = false;
        AirStuckAletherium(pLocal, cmd);
        return;
    }
    if (cumi) {
        s_asHold.ticks   = 0;
        was_stuck        = false;
        s_asExpect.valid = false;
        AirStuckCumidere(pLocal, cmd);
        return;
    }

    // botox reads the START state: every pre-stage there ends on the restored frame
    const int nBase = PredCommandsPredicted();
    RestoreToFrame(nBase - 1);

    AsClock scan_clock;
    scan_clock.Start(0.9f, TickInterval());

    const Vec3 origin         = EntV3(pLocal, g_offOrigin);
    const Vec3 live_velocity  = EntV3(pLocal, g_offVelocity); /* not backup_data: stale one cmd behind on choked/hitched cmds */
    const Vec3 live_view      = cmd->viewangles;
    const int live_buttons    = cmd->buttons;
    const float live_fwd      = cmd->forwardmove;
    const float live_side     = cmd->sidemove;
    const Vec3 mins           = EntV3(pLocal, g_offMins);
    const Vec3 maxs           = EntV3(pLocal, g_offMaxs);
    const float ext_x         = (std::max)(fabsf(mins.x), fabsf(maxs.x));
    const float ext_y         = (std::max)(fabsf(mins.y), fabsf(maxs.y));

    TraceFilterWorldOnly filter; filter.Setup();
    const auto line = [&](const Vec3& from, const Vec3& to, const unsigned int mask) {
        GameTrace_t tr{};
        TraceLine(from, to, mask, &filter, &tr);
        return tr;
    };

    const auto apply_predicted_state = [&]() noexcept {
        AsSetOrigin(pLocal, origin);
        EntSetV3(pLocal, g_offVelocity, live_velocity);
    };

    /* silent: the sent cmd always keeps your view; the proven move ( pin yaw frame ) is rotated into it and AS_ShotProof re-proves
       the final cmd ( 10-30 log: 125/125 live-view re-proofs pinned, <= 18 ulp tries ). zero move: wishvel 0, AirAccelerate returns on
       addspeed 0 ( gamemovement.cpp:2028/1985 ) = any view sims the same */
    const auto publish = [&](const float yaw, const float move, const bool stuck, const float pitch, const bool exact) {
        s_asProof        = { stuck, exact, origin, live_velocity };
        s_asHold.valid   = stuck;
        s_asHold.pitch   = pitch;
        s_asHold.yaw     = yaw;
        s_asHold.fwd     = move;
        cmd->viewangles  = live_view;
        as::StampRotate(live_view.y, yaw, move, 0.0f, cmd->forwardmove, cmd->sidemove);
        cmd->buttons     = live_buttons;
        s_asStampYaw     = yaw;
        s_asStampForward = move;
        s_asStampSide    = 0.0f;
        s_asStampButtons = cmd->buttons;
        s_asStampValid   = true;
        s_asStampPitch   = pitch;
        s_asAuthoredView      = cmd->viewangles;
        s_asAuthoredViewValid = true;

        s_asHolding = stuck;
        if (stuck && !was_stuck)
            DebugLog("AS: stuck with fw=%.3f sm=%.3f yaw=%.3f", cmd->forwardmove, cmd->sidemove, yaw);
        was_stuck = stuck;
    };

    const float pin_vz    = TargetPredictZVel();
    const auto vel_pinned = [pin_vz](const Vec3& v) { return v.x == 0.0f && v.y == 0.0f && v.z == pin_vz; };
    const auto pinned     = [&]() { return IsTargetVz(EntV3(pLocal, g_offVelocity).z, 0.05f); };
    const auto pin_held   = [&]() {
        return !(EntI(pLocal, g_offFlags) & FL_ONGROUND) && EntMoveType(pLocal) == MOVETYPE_WALK && vel_pinned(EntV3(pLocal, g_offVelocity));
    };

    /* server rounding != ours: a moving-START catch only ports if its first tick pins from START +-1 ulp on every origin / velocity axis
       ( 10-10 offline log: 71/83 sim-proven catches fell, pinned-START ones all held ). early out once it cannot beat `need` */
    constexpr int k_crob = 12;
    const auto catch_rob = [&](int& sims, const int need, const AsClock& clk) {
        int n = 0;
        for (int k = 0; k < k_crob && n + k_crob - k > need && !(k > 0 && clk.Expired()); ++k) {
            Vec3 o = origin, v = live_velocity;
            Vec3& w = k < 6 ? o : v;
            const int a = (k % 6) >> 1;
            float& c    = a == 0 ? w.x : a == 1 ? w.y : w.z;
            c           = std::nextafter(c, (k & 1) ? FLT_MAX : -FLT_MAX);
            RestoreToFrame(nBase - 1);
            AsSetOrigin(pLocal, o);
            EntSetV3(pLocal, g_offVelocity, v);
            Pred_Simulate(pLocal, cmd);
            ++sims;
            n += pin_held();
        }
        return n;
    };

    /* last tick sent a proven pin that did not hold: d = real START - our sim end */
    if (s_asExpect.valid) {
        s_asExpect.valid = false;
        if (!IsTargetVz(live_velocity.z, 0.05f)) {
            const Vec3 d = origin - s_asExpect.origin;
            DebugLog("AS: lost d=%.6f,%.6f,%.6f vel=%.3f,%.3f,%.3f cp=%d", d.x, d.y, d.z, live_velocity.x, live_velocity.y, live_velocity.z,
                     PredCommandsPredicted());
        }
    }

    /* hold: START pinned by last tick's pin = replay it in the latched view, 1 sim proves this tick ( clarity + lumi; was 0 sims / a full fan ).
       break = search below from this still-pinned START, never send a cmd that falls */
    if (hold_armed && vel_pinned(live_velocity)) {
        RestoreToFrame(nBase - 1);
        apply_predicted_state();
        cmd->viewangles.x = s_asHold.pitch;
        cmd->viewangles.y = s_asHold.yaw;
        cmd->forwardmove  = s_asHold.fwd;
        cmd->sidemove     = 0.0f;
        cmd->buttons      = live_buttons;
        Pred_Simulate(pLocal, cmd);
        const Vec3 end   = EntV3(pLocal, g_offOrigin);
        const bool held  = !(EntI(pLocal, g_offFlags) & FL_ONGROUND) && EntMoveType(pLocal) == MOVETYPE_WALK &&
                           vel_pinned(EntV3(pLocal, g_offVelocity));
        const bool fixed = end.x == origin.x && end.y == origin.y && end.z == origin.z;
        RestoreToFrame(nBase - 1);
        apply_predicted_state();
        cmd->viewangles  = live_view;
        cmd->forwardmove = live_fwd;
        cmd->sidemove    = live_side;
        if (held) {
            if (s_asHold.ticks++ == 0)
                DebugLog("AS: hold yaw=%.1f fwd=%.4f pitch=%.2f fixed=%d", s_asHold.yaw, s_asHold.fwd, s_asHold.pitch, (int)fixed);
            publish(s_asHold.yaw, s_asHold.fwd, true, s_asHold.pitch, true);
            s_asExpect = { true, end };
            return;
        }
        DebugLog("AS: hold break t=%d yaw=%.1f fwd=%.4f pitch=%.2f b=%x", s_asHold.ticks, s_asHold.yaw, s_asHold.fwd, s_asHold.pitch, live_buttons);
    }
    s_asHold.ticks = 0;

    {
        Vec3 cl_normal{};
        bool cl_contact = false;
        bool cl_disp    = false;
        int cl_ent      = -1;
        const char* cl_surf = "";
        for (int i = 0; i < 4 && !cl_contact; ++i) {
            const float a = DEG2RADF(static_cast<float>(i * 90));
            GameTrace_t tr{};
            TraceHull(origin, Vec3(origin.x + cosf(a) * 0.05f, origin.y + sinf(a) * 0.05f, origin.z), mins, maxs,
                      MASK_PLAYERSOLID, &filter, &tr);
            cl_contact = tr.DidHit();
            cl_normal  = tr.plane.normal;
            cl_disp    = tr.dispFlags != 0;
            cl_ent     = AsEntIndex(tr.m_pEnt);
            cl_surf    = tr.surface.name ? tr.surface.name : "";
        }
        bool cl_held   = false;
        bool cl_parked = false;
        int cl_rob     = -1;
        int cl_crob    = -1;
        int cl_score   = -1;
        int cl_pins    = 0;
        float cl_yaw = 0.f, cl_fwd = 0.f, cl_pitch = 0.f;
        Vec3 cl_end{};
        float cl_park_yaw = 0.f, cl_park_fwd = 0.f, cl_park_gap = -1.f;

        /* exact-axis brush: the cardinal ladder never pins there ( 10-30 21:19 log 0/61 ticks, lab 0% ) while lumi's tangent fan does
           ( 3/3 pin=3 held ) and was starved to cand=1 behind it = leave lumi the whole clock */
        const bool cl_axis = cl_contact && cl_normal.z == 0.0f && (fabsf(cl_normal.x) == 1.0f || fabsf(cl_normal.y) == 1.0f);
        if (cl_axis)
            DebugLog("AS: clarity axis n=%.3f,%.3f vz=%.3f", cl_normal.x, cl_normal.y, live_velocity.z);
        if (cl_contact && !cl_axis && cl_normal.z == 0.0f) {
            int cl_sims = 0;
            AsClock cl_clock;
            cl_clock.Start(g_asBudget * 0.01f, TickInterval());

            const float wall_yaw = RAD2DEGF(atan2f(-cl_normal.y, -cl_normal.x));
            const float base     = roundf(wall_yaw / 90.0f) * 90.0f;
            const float off      = std::remainder(wall_yaw - base, 360.0f);
            const float yaws[2]  = { std::remainder(base + (off > 0.0f ? 90.0f : -90.0f), 360.0f), std::remainder(base, 360.0f) };
            const int first_yaw  = off == 0.0f ? 1 : 0;

            const float airaccel = CvarFloat("sv_airaccelerate", 10.0f);
            const float ipt      = TickInterval();
            const float duck_mul = (live_buttons & IN_DUCK) ? 0.33333333f : 1.0f;
            const float step     = as::GridStep(airaccel, ipt, AsSurfaceFriction(pLocal), duck_mul);

            /* server float noise != ours ( online: near-axis pins die after 1 rtt ): prefer a pin that survives 1 ulp of origin each way */
            constexpr int k_rob  = 6;
            constexpr int k_best = k_crob * (k_rob + 1) + k_rob;
            const auto robust = [&]() {
                const Vec3 base_o = EntV3(pLocal, g_offOrigin);
                int n = 0;
                for (int k = 0; k < k_rob; ++k) {
                    Vec3 o   = base_o;
                    float& c = k < 2 ? o.x : k < 4 ? o.y : o.z;
                    c        = std::nextafter(c, (k & 1) ? FLT_MAX : -FLT_MAX);
                    /* botox flags EFL_DIRTY_ABSVELOCITY so SetupMove re-reads the pinned velocity ( prediction.cpp:623 );
                       m_iEFlags has no offset here, so the restore ( OnPostRestoreData dirties it ) does the same job */
                    RestoreToFrame(nBase - 1);
                    AsSetOrigin(pLocal, o);
                    EntSetV3(pLocal, g_offVelocity, Vec3(0.0f, 0.0f, pin_vz));
                    bool held = true;
                    for (int t = 0; t < 2 && held; ++t) {
                        Pred_Simulate(pLocal, cmd);
                        ++cl_sims;
                        held = !(EntI(pLocal, g_offFlags) & FL_ONGROUND) && EntMoveType(pLocal) == MOVETYPE_WALK &&
                               vel_pinned(EntV3(pLocal, g_offVelocity));
                    }
                    n += held;
                }
                return n;
            };

            /* i = 0 once: a zero-move pin is view-free ( turn / shoot / silent all sim the same ), it wins ties */
            for (int y = first_yaw; y < 2 && cl_score < k_best; ++y) {
                for (int i = y == first_yaw ? 0 : 1; i <= 100 && cl_score < k_best && !(cl_sims > 0 && cl_clock.Expired()); ++i) {
                    const float fwd = static_cast<float>(i) * step;
                    RestoreToFrame(nBase - 1);
                    apply_predicted_state();
                    cmd->viewangles   = live_view;
                    cmd->viewangles.y = yaws[y];
                    cmd->forwardmove  = fwd;
                    cmd->sidemove     = 0.0f;
                    cmd->buttons      = live_buttons;

                    bool ok = true;
                    Vec3 end0{};
                    for (int t = 0; t < 6 && ok; ++t) {
                        Pred_Simulate(pLocal, cmd);
                        ++cl_sims;
                        if (t == 0)
                            end0 = EntV3(pLocal, g_offOrigin);
                        ok = !(EntI(pLocal, g_offFlags) & FL_ONGROUND) && EntMoveType(pLocal) == MOVETYPE_WALK &&
                             vel_pinned(EntV3(pLocal, g_offVelocity));
                    }
                    if (!ok)
                        continue;
                    ++cl_pins;
                    const int rob = robust();
                    if (k_crob * (k_rob + 1) + rob <= cl_score)
                        continue;
                    const int crob  = catch_rob(cl_sims, cl_score < rob ? -1 : (cl_score - rob) / (k_rob + 1), cl_clock);
                    const int score = crob * (k_rob + 1) + rob;
                    if (score > cl_score) {
                        cl_held  = true;
                        cl_score = score;
                        cl_rob   = rob;
                        cl_crob  = crob;
                        cl_yaw   = yaws[y];
                        cl_fwd   = fwd;
                        cl_pitch = live_view.x;
                        cl_end   = end0;
                    }
                }
            }

            constexpr float k_park_gap   = 0.01f;
            constexpr float k_park_probe = 0.05f;
            if (!cl_held && !cl_disp) {
                const auto hull_hits = [&](const float d) {
                    GameTrace_t tr{};
                    TraceHull(origin, origin - cl_normal * d, mins, maxs, MASK_PLAYERSOLID, &filter, &tr);
                    return tr.DidHit() || tr.startsolid;
                };
                if (hull_hits(k_park_probe)) {
                    float glo = 0.f, ghi = k_park_probe;
                    for (int i = 0; i < 24; ++i) {
                        const float mid = (glo + ghi) * 0.5f;
                        (hull_hits(mid) ? ghi : glo) = mid;
                    }
                    cl_park_gap = glo;
                }
                if (cl_park_gap > k_park_gap) {
                    const float start_d = origin.x * cl_normal.x + origin.y * cl_normal.y;
                    float lo = 1.17549435e-38f, hi = step * 100.f, best_d = FLT_MAX;
                    bool best_pinned = false;
                    for (int i = 0; i < 16; ++i) {
                        const float mid = (lo + hi) * 0.5f;
                        RestoreToFrame(nBase - 1);
                        apply_predicted_state();
                        cmd->viewangles = live_view;
                        as::StampRotate(live_view.y, wall_yaw, mid, 0.f, cmd->forwardmove, cmd->sidemove);
                        cmd->buttons = live_buttons;
                        Pred_Simulate(pLocal, cmd);
                        ++cl_sims;
                        const Vec3 o    = EntV3(pLocal, g_offOrigin);
                        const float d   = o.x * cl_normal.x + o.y * cl_normal.y;
                        const bool back = start_d <= d;
                        if (!back && d < best_d && !(EntI(pLocal, g_offFlags) & FL_ONGROUND)) {
                            best_d      = d;
                            best_pinned = vel_pinned(EntV3(pLocal, g_offVelocity));
                            cl_park_fwd = mid;
                            cl_parked   = true;
                        }
                        (back ? hi : lo) = mid;
                    }
                    if (best_pinned)
                        cl_parked = false;
                    cl_park_yaw = wall_yaw;
                }
            }
            RestoreToFrame(nBase - 1);
            apply_predicted_state();
            cmd->viewangles  = live_view;
            cmd->forwardmove = live_fwd;
            cmd->sidemove    = live_side;
            cmd->buttons     = live_buttons;

            DebugLog("AS: clarity %s n=%.3f,%.3f yaw=%.0f fwd=%.4f sims=%d vz=%.3f vxy=%.1f gap=%.5f disp=%d rob=%d crob=%d np=%d cp=%d ent=%d surf=%s us=%lld",
                     cl_held ? "pin" : cl_parked ? "park" : "miss", cl_normal.x, cl_normal.y,
                     cl_parked ? cl_park_yaw : cl_yaw, cl_parked ? cl_park_fwd : cl_fwd, cl_sims, live_velocity.z, live_velocity.Length2D(), cl_park_gap,
                     (int)cl_disp, cl_rob, cl_crob, cl_pins, PredCommandsPredicted(), cl_ent, cl_surf, cl_clock.UsedUs());
            if (cl_held) {
                publish(cl_yaw, cl_fwd, true, cl_pitch, true);
                s_asExpect = { true, cl_end };
                return;
            }
            if (cl_parked) {
                publish(cl_park_yaw, cl_park_fwd, false, live_view.x, false);
                return;
            }
        }
    }

    /* wall, touch scan: 16 headings from last tick's, 1u ray column feet..head reaching 2u past the hull edge.
       |nz| <= 0.35 ( slopes / disps count ), must face us; lowest fraction in the column wins */
    const float extra_reach = std::clamp(g_asExtraReach, 0.0f, 32.0f);
    /* one box holds every A/B ray below: empty = no wall, skips ~1170 traces ( 4.5 ms/tick ate the cmd clock, eb starved ) */
    {
        const float half = (std::max)(ext_x, ext_y) + (std::max)(extra_reach, 0.05f) + 0.5f;
        GameTrace_t tr{};
        TraceHull(origin, origin, Vec3(-half, -half, mins.z - 1.0f), Vec3(half, half, maxs.z + 1.0f), MASK_PLAYERSOLID, &filter, &tr);
        if (!tr.DidHit()) {
            start_angle = 0.0f;
            was_stuck   = false;
            return;
        }
    }
    char scan   = 'A';
    bool found  = false;
    Vec3 normal{};
    float hit_z = 0.0f;
    int hit_ent = -1;
    for (float a = start_angle; a < start_angle + as::k_pi * 2.0f; a += as::k_pi / 8.0f) {
        const float ang   = fmodf(a, as::k_pi * 2.0f);
        const float c     = cosf(ang);
        const float s     = sinf(ang);
        const float reach = (std::min)(ext_x / (std::max)(fabsf(c), 1e-6f), ext_y / (std::max)(fabsf(s), 1e-6f)) +
                            std::clamp(extra_reach, 0.05f, 2.0f);
        float best_frac   = 1.0f;

        for (float z = origin.z + mins.z; z <= origin.z + maxs.z; z += 1.0f) {
            const GameTrace_t tr =
                line(Vec3(origin.x, origin.y, z), Vec3(origin.x + c * reach, origin.y + s * reach, z), MASK_PLAYERSOLID);
            const Vec3& n = tr.plane.normal;
            if (tr.fraction < 1.0f && fabsf(n.z) <= 0.35f && n.x * c + n.y * s <= -0.1f && tr.fraction < best_frac) {
                found     = true;
                best_frac = tr.fraction;
                normal    = n;
                hit_z     = tr.endpos.z;
                hit_ent   = AsEntIndex(tr.m_pEnt);
            }
        }
        if (found) {
            start_angle = a;
            break;
        }
    }

    if (!found && extra_reach > 2.0f) {
        scan               = 'B';
        const float radius = (std::max)(ext_x, ext_y) + extra_reach;
        const float z0     = origin.z + mins.z + 2.0f;
        const float dz     = ((origin.z + maxs.z - 2.0f) - z0) / 6.0f;
        int best_hits      = 0;
        float best_frac    = 1.0f;

        for (float a = 0.0f; a < as::k_pi * 2.0f; a += as::k_pi / 16.0f) {
            const float c = cosf(a);
            const float s = sinf(a);
            int hits      = 0;
            float frac    = 1.0f;
            Vec3 n_a{};
            float z_a = 0.0f;
            int e_a   = -1;

            for (int i = 0; i < 7; ++i) {
                const float z = static_cast<float>(i) * dz + z0;
                const GameTrace_t tr =
                    line(Vec3(origin.x, origin.y, z), Vec3(origin.x + c * radius, origin.y + s * radius, z), MASK_PLAYERSOLID);
                if (tr.fraction < 1.0f && fabsf(tr.plane.normal.z) <= 0.08f && (++hits == 1 || tr.fraction < frac)) {
                    frac = tr.fraction;
                    n_a  = tr.plane.normal;
                    z_a  = tr.endpos.z;
                    e_a  = AsEntIndex(tr.m_pEnt);
                }
            }
            if (hits > 0 && (!found || hits > best_hits || (hits == best_hits && frac < best_frac))) {
                found       = true;
                start_angle = a;
                best_hits   = hits;
                best_frac   = frac;
                normal      = n_a;
                hit_z       = z_a;
                hit_ent     = e_a;
            }
        }
    }

    if (!found) {
        start_angle = 0.0f;
        was_stuck   = false;
        return;
    }
    const long long scan_us = scan_clock.UsedUs();

    const float wall_yaw = RAD2DEGF(atan2f(-normal.y, -normal.x));
    float drift_m = fmodf(RAD2DEGF(atan2f(live_velocity.y, live_velocity.x)) - wall_yaw + 180.0f, 360.0f);
    if (drift_m < 0.0f)
        drift_m += 360.0f;
    const float side    = drift_m < 180.0f ? 1.0f : -1.0f;
    const float support = TB_WallSupportDistance(pLocal, Vec3(normal.x, normal.y, 0.0f));

    AsClock clock;
    clock.Start(g_asBudget * 0.01f, TickInterval());
    int cands        = 0;
    int crob_sims    = 0;
    int best_crob    = -1;
    int best_ticks   = 0;
    float best_gap   = FLT_MAX;
    float best_yaw   = wall_yaw;
    float best_move  = 0.0f;
    Vec3 best_end{};
    const auto out_of_time = [&]() { return cands >= 512 || (cands > 0 && clock.Expired()); };

    const auto try_move = [&](const float rel, const float move, const float gap_len) {
        ++cands;
        RestoreToFrame(nBase - 1);
        apply_predicted_state();

        cmd->viewangles.y = std::remainder(wall_yaw + rel * side, 360.0f);
        cmd->forwardmove  = move;
        cmd->sidemove     = 0.0f;
        cmd->buttons      = live_buttons;

        Pred_Simulate(pLocal, cmd);
        const Vec3 end1 = EntV3(pLocal, g_offOrigin);

        int ticks = 0;
        if (pinned()) {
            ticks = 1;
            for (int k = 0; k < 2; ++k) {
                Pred_Simulate(pLocal, cmd);
                if (!pinned())
                    break;
                ++ticks;
            }
        }

        const Vec3 end = EntV3(pLocal, g_offOrigin);
        const GameTrace_t tr = line(Vec3(end.x, end.y, hit_z),
                                    Vec3(end.x - normal.x * gap_len, end.y - normal.y * gap_len, hit_z), MASK_ALL_MV);
        const float gap = tr.fraction * gap_len - support;
        /* 10-30 21:19 log: pin=1 fell 28/28, pin=3 held 3/3; crob only breaks ties */
        const int crob  = ticks > 0 && ticks >= best_ticks ? catch_rob(crob_sims, ticks > best_ticks ? -1 : best_crob - 1, clock) : -1;

        if (ticks > best_ticks || (ticks == best_ticks && (crob > best_crob || (crob == best_crob && gap < best_gap)))) {
            best_crob  = crob;
            best_ticks = ticks;
            best_gap   = gap;
            best_yaw   = cmd->viewangles.y;
            best_move  = move;
            best_end   = end1;
        }
    };

    constexpr float k_moves_near[3] = { 1.0f, 5.0f, 30.0f };
    for (const float move : k_moves_near) {
        if (out_of_time())
            break;
        for (float rel = 85.999f; rel < 90.0f; rel += 1.0f) {
            if (out_of_time())
                break;
            try_move(rel, move, 32.0f);
        }
    }

    if (best_ticks < 3 && !out_of_time()) {
        constexpr float k_moves_far[6] = { 1.0f, 5.0f, 30.0f, 60.0f, 90.0f, 120.0f };
        for (const float move : k_moves_far) {
            if (out_of_time())
                break;
            for (float rel = 78.0f; rel < 90.0f; rel += 0.5f) {
                if (out_of_time())
                    break;
                try_move(rel, move, 64.0f);
            }
        }
    }

    RestoreToFrame(nBase - 1);
    apply_predicted_state();
    /* a hold is armed only by the full 3-tick chain; shorter pins go out as the approach move, unarmed */
    publish(best_yaw, best_move, best_ticks >= 3, live_view.x, false);
    s_asExpect = { best_ticks >= 3, best_end };

    DebugLog("AS: lumi scan=%c n=%.3f,%.3f,%.3f hz=%.1f side=%.0f pin=%d crob=%d/%d gap=%.3f yaw=%.1f fwd=%.0f cand=%d vz=%.2f ent=%d scan_us=%lld us=%lld", scan,
             normal.x, normal.y, normal.z, hit_z - origin.z, side, best_ticks, best_crob, crob_sims, best_gap, best_yaw, best_move, cands, live_velocity.z,
             hit_ent, scan_us, clock.UsedUs());
}

// botox movement.cpp:484-493: the per-cmd reset + "did AS write the cmd" (g_air_stuck_owns_cmd)
static void AirStuckTick(void* pLocal, UserCmd_t* pCmd)
{
    s_asStampValid        = false;
    s_asAuthoredViewValid = false;
    const float pre_fwd  = pCmd->forwardmove;
    const float pre_side = pCmd->sidemove;
    const int pre_buttons = pCmd->buttons;
    const Vec3 pre_view   = pCmd->viewangles;

    AirStuck(pLocal, pCmd);

    s_airStuckHit = s_asHolding || s_asAuthoredViewValid ||
                    pCmd->forwardmove != pre_fwd || pCmd->sidemove != pre_side ||
                    pCmd->buttons != pre_buttons || pCmd->viewangles.x != pre_view.x ||
                    pCmd->viewangles.y != pre_view.y;
}

static void AS_FinalStamp(UserCmd_t* cmd)
{
    if (!s_asStampValid || !cmd)
        return;

    float stamped_forward = 0.0f;
    float stamped_side    = 0.0f;
    as::StampRotate(cmd->viewangles.y, s_asStampYaw, s_asStampForward, s_asStampSide, stamped_forward, stamped_side);

    static bool was_drifting = false;
    const float drift   = (std::max)(fabsf(cmd->forwardmove - stamped_forward), fabsf(cmd->sidemove - stamped_side));
    const bool drifting = drift > 0.5f;
    if (drifting != was_drifting) {
        was_drifting = drifting;
        if (drifting)
            DebugLog("AS: stamp fixed drift %.1f (fwd %.1f -> %.1f)", drift, cmd->forwardmove, stamped_forward);
    }

    cmd->forwardmove = stamped_forward;
    cmd->sidemove    = stamped_side;
    cmd->upmove      = 0.0f;

    constexpr int direction_mask = IN_MOVERIGHT | IN_MOVELEFT | IN_BACK | IN_FORWARD | IN_DUCK;
    cmd->buttons = (cmd->buttons & ~direction_mask) | (s_asStampButtons & direction_mask);
}

/* last writer: the pin was proven in the stamp ( pin yaw ) view; the sent cmd keeps your view, so its rotated move is re-proven here
   every tick ( silent ). nothing pins in your view -> most robust pinned move, else the sent move; the view is never forced.
   caller opens prediction ( Pred_Start ) around this: every sim below is inside that bracket */
static void AS_ShotProof(void* pLocal, UserCmd_t* cmd)
{
    const bool armed = s_asProof.valid;
    s_asProof.valid  = false;
    if (!armed || !cmd || !s_asStampValid || !pLocal || !EntAlive(pLocal))
        return;
    if (cmd->viewangles.x == s_asStampPitch && cmd->viewangles.y == s_asStampYaw &&
        cmd->forwardmove == s_asStampForward && cmd->sidemove == s_asStampSide)
        return;
    /* zero-move pin: wishvel 0 in every view, the proof already covers this cmd */
    if (s_asStampForward == 0.0f && s_asStampSide == 0.0f && cmd->forwardmove == 0.0f && cmd->sidemove == 0.0f)
        return;

    const int nBase        = PredCommandsPredicted();
    const float pin_vz     = TargetPredictZVel();
    const float sent_f     = cmd->forwardmove;
    const float sent_s     = cmd->sidemove;
    const Vec3 sent_view   = cmd->viewangles;

    const auto reset = [&]() {
        RestoreToFrame(nBase - 1);
        AsSetOrigin(pLocal, s_asProof.origin);
        EntSetV3(pLocal, g_offVelocity, s_asProof.velocity);
    };
    const auto held = [&]() {
        if ((EntI(pLocal, g_offFlags) & FL_ONGROUND) || EntMoveType(pLocal) != MOVETYPE_WALK)
            return false;
        const Vec3 v = EntV3(pLocal, g_offVelocity);
        return s_asProof.exact ? v.x == 0.0f && v.y == 0.0f && v.z == pin_vz : IsTargetVz(v.z, 0.05f);
    };
    const auto same = [](const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; };

    int sims = 0;
    /* fixed point = next tick replays the sent cmd from the same state; else replay it 2 ticks */
    const auto pins = [&](const float f, const float s) {
        reset();
        cmd->viewangles  = sent_view;
        cmd->forwardmove = f;
        cmd->sidemove    = s;
        Pred_Simulate(pLocal, cmd);
        ++sims;
        if (!held())
            return false;
        if (same(EntV3(pLocal, g_offOrigin), s_asProof.origin) && same(EntV3(pLocal, g_offVelocity), s_asProof.velocity))
            return true;
        for (int t = 0; t < 2; ++t) {
            Pred_Simulate(pLocal, cmd);
            ++sims;
            if (!held())
                return false;
        }
        return true;
    };

    float rot_f = 0.0f, rot_s = 0.0f;
    as::StampRotate(cmd->viewangles.y, s_asStampYaw, s_asStampForward, s_asStampSide, rot_f, rot_s);
    const auto ulp = [](float x, int k) {
        for (; k > 0; --k)
            x = std::nextafter(x, FLT_MAX);
        for (; k < 0; ++k)
            x = std::nextafter(x, -FLT_MAX);
        return x;
    };

    /* online: server keeps only pins that survive its float noise ( clarity robust( ) ); a swapped move must be as robust as the stamp */
    const auto robust = [&](const Vec3& view, const float f, const float s) {
        int n = 0;
        for (int k = 0; k < 6; ++k) {
            reset();
            Vec3 o   = s_asProof.origin;
            float& c = k < 2 ? o.x : k < 4 ? o.y : o.z;
            c        = std::nextafter(c, (k & 1) ? FLT_MAX : -FLT_MAX);
            AsSetOrigin(pLocal, o);
            cmd->viewangles  = view;
            cmd->forwardmove = f;
            cmd->sidemove    = s;
            bool ok = true;
            for (int t = 0; t < 2 && ok; ++t) {
                Pred_Simulate(pLocal, cmd);
                ++sims;
                ok = held();
            }
            n += ok;
        }
        return n;
    };

    static struct {
        Vec3 origin{}, velocity{};
        float pitch = 0.0f, yaw = 0.0f, f = 0.0f, s = 0.0f;
        int buttons = 0, rob = -1;
    } s_need;
    /* bar on every tick: 10-07 23:18 log 283/330 shot pins took 1 sim rob 0, holds died 39-89t into a spray */
    if (!same(s_need.origin, s_asProof.origin) || !same(s_need.velocity, s_asProof.velocity) || s_need.pitch != s_asStampPitch ||
        s_need.yaw != s_asStampYaw || s_need.f != s_asStampForward || s_need.s != s_asStampSide ||
        s_need.buttons != cmd->buttons || s_need.rob < 0) {
        Vec3 stamp_view = sent_view;
        stamp_view.x    = s_asStampPitch;
        stamp_view.y    = s_asStampYaw;
        s_need = { s_asProof.origin, s_asProof.velocity, s_asStampPitch, s_asStampYaw, s_asStampForward, s_asStampSide,
                   cmd->buttons, robust(stamp_view, s_asStampForward, s_asStampSide) };
    }
    const int need = s_need.rob;

    AsClock clock;
    clock.Start(g_asBudget * 0.01f, TickInterval());
    constexpr int k_floor = 24;
    constexpr int k_cap   = 64;
    int tries = 0, keep = -1, best_rob = -1;
    float out_f = sent_f, out_s = sent_s, best_f = sent_f, best_s = sent_s;
    const auto attempt = [&](const float f, const float s) {
        if (keep >= 0 || tries >= k_cap || (tries >= k_floor && clock.Expired()))
            return;
        ++tries;
        if (!pins(f, s))
            return;
        if (need > 0) {
            if (best_rob >= 0 && clock.Expired())
                return;
            const int rob = robust(sent_view, f, s);
            if (rob > best_rob) {
                best_rob = rob;
                best_f   = f;
                best_s   = s;
            }
            if (rob < need)
                return;
        }
        keep  = tries;
        out_f = f;
        out_s = s;
    };

    attempt(sent_f, sent_s);
    if (rot_f != sent_f || rot_s != sent_s)
        attempt(rot_f, rot_s);
    for (int k = 1; k <= 6; ++k) {
        attempt(rot_f, ulp(rot_s, k));
        attempt(rot_f, ulp(rot_s, -k));
    }
    for (int k = 1; k <= 3; ++k) {
        attempt(ulp(rot_f, k), rot_s);
        attempt(ulp(rot_f, -k), rot_s);
    }
    for (int j = 1; j <= 8; ++j) {
        for (const float m : { 1.0f + j / 16.0f, 1.0f - j / 16.0f }) {
            float f = 0.0f, s = 0.0f;
            as::StampRotate(cmd->viewangles.y, s_asStampYaw, s_asStampForward * m, s_asStampSide * m, f, s);
            attempt(f, s);
        }
    }
    attempt(0.0f, 0.0f);

    /* nothing as robust as the stamp: the most robust pinned move beats the unproven sent one ( keep=0 ) */
    if (keep < 0 && best_rob >= 0) {
        keep  = 0;
        out_f = best_f;
        out_s = best_s;
    }

    reset();
    cmd->viewangles  = sent_view;
    cmd->forwardmove = out_f;
    cmd->sidemove    = out_s;
    Pred_Simulate(pLocal, cmd);

    static int s_last_kept = -1;
    const int kept         = keep >= 0 ? 1 : 0;
    if (kept == s_last_kept && !(cmd->buttons & IN_ATTACK))
        return;
    s_last_kept = kept;

    DebugLog("AS: shot keep=%d rob=%d/%d tries=%d sims=%d exact=%d fwd=%.4f/%.4f -> %.4f/%.4f view=%.2f/%.2f stamp=%.2f/%.2f av=%.2f/%.2f us=%lld",
             keep, best_rob, need, tries, sims, (int)s_asProof.exact, sent_f, sent_s, out_f, out_s, cmd->viewangles.x, cmd->viewangles.y,
             s_asStampPitch, s_asStampYaw, s_asAuthoredView.x, s_asAuthoredView.y, clock.UsedUs());
}
