// ============================================================================
//  edgesurf.inl -- EDGE SURF. 1:1 port of botox csgo hacks/movement/edge_surf.cpp onto TF2's sims (tbx, texturebug.inl).
//  Included from movement.cpp right after texturebug.inl, same translation unit. The planner is edgesurf_core.h (the csgo
//  edge_surf_core.h text, 4 engine knobs lifted into the adapter below). State + gates (ESurf_Latched / _Armed / _EbArmed /
//  _Skip) sit in movement.cpp above edgebug.inl: eb, auto align, tb and as read them before this file.
//
//  Entry points:
//    ESurf_Online      tbx::OnCreateMove, before texture bug: true = edge surf owns the cmd (tb + head bounce skip it)
//    ESurf_EbEnd       edgebug.inl search, "edgesurf on eb": the bug tick must be an exact feet pin at a ledge top
//    ESurf_FinalStamp  Movement_AirStuckLastWriter, after the aimbot: later writers never stomp an owned cmd
//    ESurf_GetDrawData render.cpp RenderEdgeSurfOverlay
//
//  TF2 differences from botox (CS:GO), each forced by tf_gamemovement / gamemovement:
//   - hull 48 x 82 / 62 (tf_gamerules.cpp g_TFViewVectors) scaled by model scale: read off the live hull.
//   - air duck / unduck shifts the feet 20 (FinishDuck hullNormal - hullCrouch, 82 - 62), not 9: w.duck_dz.
//   - no air duck crop: CGameMovement::HandleDuckingSpeedCrop crops on the ground only (csgo 0.34): w.crop 1.
//   - no duck speed / 0.4 s re-duck timer: tf_clamp_airducks 1 refuses IN_DUCK in air once m_nAirDucked >= 2
//     (TF_AIRDUCKED_COUNT, counted per air RELEASE in OnUnDuck): w.relift / the eb lift read it (predicted field).
//   - sv_wallbug 0 (Valve Oct 2026 ClipVelocity) ends every pin at -halfgrav + 0.03125: pin tolerance as::k_pinTol.
//   - mv->m_flMaxSpeed = min(m_flMaxspeed, TF_MAX_SPEED 520) (ProcessMovement + CheckParameters), no stamina, sv_airaccelerate 10.
//   - player movement passes tf_gamerules.cpp / gamerules.cpp ShouldCollide's groups (ESurf_MovesGroup). No ladders.
// ============================================================================
#include "edgesurf_core.h"

namespace esurf
{
inline n_es::v3 To3(const Vec3& v) { return n_es::mk(v.x, v.y, v.z); }
inline Vec3 ToV(const n_es::v3& v) { return Vec3(v.x, v.y, v.z); }

constexpr int   k_solidBsp      = 1;      // SOLID_BSP (const.h)
constexpr int   k_solidVphysics = 6;      // SOLID_VPHYSICS
constexpr int   k_airDucks      = 2;      // tf_gamemovement.cpp TF_AIRDUCKED_COUNT
constexpr float k_tfMaxSpeed    = 520.f;  // tf_shareddefs.h TF_MAX_SPEED ( 400 * 1.3 )

// COLLISION_GROUP_PLAYER_MOVEMENT (8) vs group g: false = movement walks through it (gamerules.cpp:676 + tf_gamerules.cpp:18000)
inline bool MovesGroup(const int g)
{
    switch (g) {
    case 1: case 2:   // DEBRIS, DEBRIS_TRIGGER: only NONE
    case 3:           // INTERACTIVE_DEBRIS vs player movement
    case 10:          // IN_VEHICLE
    case 11:          // WEAPON ( "Don't stand on COLLISION_GROUP_WEAPONs" )
    case 12:          // VEHICLE_CLIP: vehicles only
    case 13:          // PROJECTILE ( "Don't stand on projectiles" )
    case 14:          // DOOR_BLOCKER: NPCs only
    case 16:          // DISSOLVING: NONE only
    case 17:          // PUSHAWAY
    case 20:          // TF_COLLISIONGROUP_GRENADES
    case 23:          // TFCOLLISION_GROUP_COMBATOBJECT
    case 24: case 27: // TFCOLLISION_GROUP_ROCKETS ( + _BUT_NOT_WITH_OTHER_ROCKETS )
    case 26:          // TFCOLLISION_GROUP_TANK
        return false;
    default:
        return true;
    }
}

// IHandleEntity::GetRefEHandle (slot 2), 0xFFFFFFFF on a fault. Static prop = serial 1 << 15 (TraceFilterWorldProps)
inline unsigned RefHandle(void* e)
{
    unsigned h = 0xFFFFFFFFu;
    __try {
        if (unsigned* p = VCall<unsigned*>(e, 2)) h = *p;
    } __except (EXCEPTION_EXECUTE_HANDLER) { h = 0xFFFFFFFFu; }
    return h;
}

// es_moves_ent: movement collides with it -- world, a static prop, or an entity whose group player movement does not pass
inline bool MovesEnt(void* e)
{
    if (!e || e == GetClientEntity(0) || g_offCollGroup <= 0) return true;
    int g = 0;
    __try { g = EntI(e, g_offCollGroup); } __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    return MovesGroup(g);
}

inline int SolidType(void* e)
{
    if (!e || g_offSolidType <= 0) return 0;
    int s = 0;
    __try { s = EntB(e, g_offSolidType); } __except (EXCEPTION_EXECUTE_HANDLER) { s = 0; }
    return s;
}

// es_filter_t: tb's world + props filter minus what player movement passes (weapons, debris, projectiles, ...): a dropped
// gun / a sticky is no ledge, top or floor
struct Filter : TraceFilterWorldProps
{
    static bool __fastcall sHit(TraceFilterWorldProps* self, void* pEnt, int mask)
    {
        if (!TraceFilterWorldProps::sShouldHit(self, pEnt, mask)) return false;
        // a static prop handle has no entity behind it
        if ((RefHandle(pEnt) >> 16) == (1u << 15)) return true;
        return MovesEnt(pEnt);
    }
    void Init(void* skip)
    {
        Setup(skip);
        static void* vt[2] = { (void*)&sHit, (void*)&TraceFilterWorldProps::sTraceType };
        vfptr = vt;
    }
};

// es_max_speed: mv->m_flMaxSpeed = min(client max speed = m_flMaxspeed, TF_MAX_SPEED) (CheckParameters). Never stamina: TF2 has
// none. Capped at the press cap too: every press is <= n_es::k_wish_max 450 and AirAccelerate takes min(wish, m_flMaxSpeed), so a
// boosted 520 scout's planned authority would be 15% more than any press delivers ( csgo maxes 260, never hits it )
inline float MaxSpeed(void* pLocal)
{
    const float m = g_offMaxspeed > 0 ? EntF(pLocal, g_offMaxspeed) : 0.f;
    return m > 1.f ? (std::min)((std::min)(m, k_tfMaxSpeed), n_es::k_wish_max) : 300.f;
}

// TF2 air duck budget at this state: a press in air is refused once m_nAirDucked >= 2 (tf_clamp_airducks 1). `releases` more
// air releases first
inline bool AirDuckLeft(void* pLocal, const int releases)
{
    if (CvarFloat("tf_clamp_airducks", 1.f) < 0.5f || g_offAirDucked <= 0) return true;
    return EntI(pLocal, g_offAirDucked) + releases < k_airDucks;
}

// n_es adapter: sims = tbx's prediction (Restore / Simulate / SimsLeft, the same counters tb's resim reads), traces = the
// world + props hull trace
struct World
{
    void*     local = nullptr;
    int       frame = 0;
    UserCmd_t base{};
    float dt = 0.015f, pin = -6.f, k_acc = 0.f, max_speed = 300.f;
    // engine knobs (edgesurf_core.h header)
    float crop = 1.f, pin_tol = as::k_pinTol, duck_dz = 20.f, scale = 1.f;
    bool  relift_ok = true;
    bool  debug = false;
    int   sims = 0;
    Filter filter;

    explicit World(void* l) : local(l) { filter.Init(l); }

    // model scale off the live hull ( EbDuckShiftZ ): the stance's nominal height vs what the entity has
    void SetupHull()
    {
        const float h  = EntV3(local, g_offMaxs).z - EntV3(local, g_offMins).z;
        const float nm = (EntI(local, g_offFlags) & FL_DUCKING) ? 62.f : 82.f;
        scale   = h > 1.f ? h / nm : 1.f;
        duck_dz = 20.f * scale;
    }
    bool relift() const { return relift_ok; }

    n_es::st_t read() const
    {
        n_es::st_t s;
        s.o          = To3(EntV3(local, g_offOrigin));
        s.v          = To3(EntV3(local, g_offVelocity));
        const int fl = EntI(local, g_offFlags);
        s.ground     = (fl & FL_ONGROUND) != 0;
        s.duck       = (fl & FL_DUCKING) != 0;
        s.walk       = EntMoveType(local) == MOVETYPE_WALK;
        return s;
    }
    n_es::st_t start()
    {
        tbx::Restore(frame);
        return read();
    }
    void reset() { tbx::Restore(frame); }
    UserCmd_t cmd_of(const n_es::mv_t& m) const
    {
        UserCmd_t c = base;
        c.buttons &= ~(IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT | IN_JUMP | IN_DUCK);
        if (m.duck)
            c.buttons |= IN_DUCK;
        c.forwardmove = m.wish;
        c.sidemove    = 0.f;
        if (m.wish > 0.f)
            tbx::MoveFixToYaw(m.yaw, c);
        return c;
    }
    n_es::st_t sim(const n_es::mv_t& m)
    {
        UserCmd_t c = cmd_of(m);
        tbx::Simulate(local, &c);
        ++sims;
        return read();
    }
    bool save(const int i) { return Pred_SnapSave(local, i); }
    // botox restores the frame before its snapshot_load; ours is a full datamap transfer already (prediction.h PREFIX
    // SNAPSHOTS) and it dirties the restore flag itself
    void load(const int i) { Pred_SnapLoad(local, i); }
    // each prop / entity a sweep met, once per 10 s: names a prop= face
    static void note_obj(void* he, const bool ent, const int solid, const int hitbox)
    {
        static int s_seen[32]{};
        static int s_n = 0;
        static unsigned long long s_reset = 0ull;
        if (const unsigned long long t = GetTickCount64(); t >= s_reset) {
            s_n     = 0;
            s_reset = t + 10000ull;
        }
        const int idx  = ent ? (int)(RefHandle(he) & 0xFFFF) : 0;
        const int code = ent ? idx : -hitbox;
        if (std::find(s_seen, s_seen + (std::min)(s_n, 32), code) != s_seen + (std::min)(s_n, 32))
            return;
        s_seen[s_n++ % 32] = code;
        if (!ent) {
            DebugLog("ESURF: obj sprop=%d", hitbox - 1);
            return;
        }
        const char* name = nullptr;
        if (g_entityList && idx > 0 && idx < 0x1000) {
            __try {
                if (void* net = VCall<void*>(g_entityList, 0, idx))
                    if (const ClientClass* cc = VCall<ClientClass*>(net, 2))
                        name = cc->m_pNetworkName;
            } __except (EXCEPTION_EXECUTE_HANDLER) { name = nullptr; }
        }
        DebugLog("ESURF: obj ent=%d %s solid=%d group=%d", idx, name ? name : "?", solid,
                 g_offCollGroup > 0 ? EntI(he, g_offCollGroup) : -1);
    }
    // g_TFViewVectors, scaled
    n_es::hull_t hull(const bool duck) const
    {
        const float r = 24.f * scale;
        return { n_es::mk(-r, -r, 0.f), n_es::mk(r, r, (duck ? 62.f : 82.f) * scale) };
    }
    bool trace(const n_es::v3& a, const n_es::v3& b, const n_es::v3& mins, const n_es::v3& maxs, n_es::hit_t& h)
    {
        GameTrace_t tr{};
        TraceHull(ToV(a), ToV(b), ToV(mins), ToV(maxs), MASK_PLAYERSOLID, &filter, &tr);
        h.frac        = tr.fraction;
        h.start_solid = tr.startsolid;
        h.n           = To3(tr.plane.normal);
        h.end         = To3(tr.endpos);
        // ponytail: brush entity planes come back rebuilt from endpos ( tbx::HullPlaneDist, a few ulp ); csgo adds the collision
        // origin to plane.dist for SOLID_BSP. Exact only once TF2 x64 CM_TransformedBoxTrace's plane.dist space is checked in engine.dll
        h.d           = tbx::HullPlaneDist(tr, ToV(mins), ToV(maxs));
        // the surface name, never dispFlags: only disp code writes those, a closer brush hit later in the same trace keeps them
        // ( cmodel.cpp:1670 ). exact name ( cmodel_disp.cpp:40 ): a brush material with "displacement" in its path is a brush
        h.disp        = tr.surface.name && !strcmp(tr.surface.name, "**displacement**");
        h.ladder      = false;   // TF2 has no ladders
        h.moves       = MovesEnt(tr.m_pEnt);
        /* world = box rules: a world brush or a brush entity ( SOLID_BSP ). vp = vphysics: a static prop ( the world entity with
           hitbox = prop index + 1 ) or SOLID_VPHYSICS. an entity bbox / obb = neither */
        void* const he  = tr.m_pEnt;
        const bool ent  = he && he != GetClientEntity(0);
        const int solid = ent ? SolidType(he) : 0;
        h.world         = tr.hitbox == 0 && (!ent || solid == k_solidBsp);
        h.vp            = (!ent && tr.hitbox != 0) || solid == k_solidVphysics;
        if (debug && tr.fraction < 1.f && !h.world)
            note_obj(he, ent, solid, tr.hitbox);
        return tr.fraction < 1.f;
    }
    int sims_left() const { return tbx::SimsLeft(); }
    int used() const { return sims; }
    // owned ticks log every tick, hands off lines 4 a second
    void note(const char act, const char* line) const
    {
        static unsigned long long s_next = 0ull;
        static char s_last = 0;
        const unsigned long long t = GetTickCount64();
        if (act == 'Y' && act == s_last && t < s_next)
            return;
        s_next = t + 250ull;
        s_last = act;
        DebugLog("%s", line);
    }
};

static n_es::mem_t s_mem{};
static unsigned long long s_vizMs = 0ull;
static bool s_vizPinned = false;
static Vec3 s_vizOrigin{};
static float s_vizHalf = 24.f;

// later features may rewrite the cmd: one closing ulp over the top = standing on the ledge
static struct { int cmd = -1, buttons = 0; float yaw = 0.f, fwd = 0.f, side = 0.f; } s_sent;

// ---- visualize snapshot (movement thread -> render thread) ----
static std::mutex s_vizLock;
static ESurfDrawData s_vizOut;

// edge_surf_render's geometry, built here: a stretch of the edge under the hull centred on `s`, ticks 8u down from its ends
static void EdgeAt(const n_es::edge_t& e, const float s, const float half, ESurfDrawData::Edge& out)
{
    const auto at = [&](const float t, const float z) { return Vec3(e.n.x * e.d - e.n.y * t, e.n.y * e.d + e.n.x * t, z); };
    out.a = at(s - half, e.top);
    out.b = at(s + half, e.top);
}

static void VizPublish()
{
    ESurfDrawData d;
    d.ms     = s_vizMs;
    d.pinned = s_vizPinned;
    d.act    = s_mem.act;
    d.own    = s_mem.act != 'N' && s_mem.act != 'Y';
    const n_es::v3 o3 = To3(s_vizOrigin);
    for (int i = 0; i < s_mem.n_cand && d.nCand < ESurfDrawData::k_max; ++i)
        if (!s_mem.pick_ok || !n_es::same(s_mem.cand[i], s_mem.pick))
            EdgeAt(s_mem.cand[i], n_es::along(s_mem.cand[i], o3), s_vizHalf, d.cand[d.nCand++]);
    d.pickOk = s_mem.pick_ok;
    if (d.pickOk) {
        const n_es::edge_t& f = s_mem.pick;
        const float s = s_mem.arrive_ok ? n_es::along(f, s_mem.arrive) : n_es::along(f, o3);
        EdgeAt(f, s, s_vizHalf, d.pick);
        d.arriveOk = s_mem.arrive_ok;
        d.arrive   = Vec3(f.n.x * f.d - f.n.y * s, f.n.y * f.d + f.n.x * s, f.top);
        d.label    = Vec3(d.arrive.x, d.arrive.y, f.top + 6.f);
    }
    std::lock_guard<std::mutex> l(s_vizLock);
    s_vizOut = d;
}
} // namespace esurf

void ESurf_GetDrawData(ESurfDrawData& out)
{
    std::lock_guard<std::mutex> l(esurf::s_vizLock);
    out = esurf::s_vizOut;
}

// edge_surf_final_stamp: after the aimbot / lua. A later writer that changed the wish or the keys gets the owned cmd back
static void ESurf_FinalStamp(UserCmd_t* cmd)
{
    using esurf::s_sent;
    constexpr int k_keys = IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT | IN_DUCK;
    if (!cmd || s_sent.cmd != cmd->command_number)
        return;
    const auto wish = [](const float fwd, const float side, const float yaw) {
        const float a = DEG2RADF(yaw);
        return std::pair<float, float>{ fwd * cosf(a) + side * sinf(a), fwd * sinf(a) - side * cosf(a) };
    };
    const auto [wx0, wy0] = wish(s_sent.fwd, s_sent.side, s_sent.yaw);
    const auto [wx1, wy1] = wish(cmd->forwardmove, cmd->sidemove, cmd->viewangles.y);
    const bool keys       = (cmd->buttons & k_keys) != (s_sent.buttons & k_keys);
    if (!keys && fabsf(wx1 - wx0) + fabsf(wy1 - wy0) < 1e-4f)
        return;
    if (g_debugLogEnable)
        DebugLog("ESURF: stomp cmd=%d wish %.5f,%.5f -> %.5f,%.5f keys %x -> %x yaw %.3f -> %.3f", cmd->command_number, wx0, wy0, wx1,
                 wy1, s_sent.buttons & k_keys, cmd->buttons & k_keys, s_sent.yaw, cmd->viewangles.y);
    cmd->forwardmove = s_sent.fwd;
    cmd->sidemove    = s_sent.side;
    /* same view = exact bits ( a pin press is ulp sized ) */
    if (cmd->viewangles.y != s_sent.yaw)
        tbx::MoveFixToYaw(s_sent.yaw, *cmd);
    cmd->buttons = (cmd->buttons & ~k_keys) | (s_sent.buttons & k_keys);
}

// cumidere-style TB skips the botox tick while edge surf is neither armed nor latched: edge_surf_online's off( "key" ) by hand
// ( else the sync arm stays up and a fall's misses / seen ledges carry into the next one )
static void ESurf_Idle()
{
    s_esurfActive = s_esurfSyncWant = false;
    s_esurfLatchCmd   = -1;
    esurf::s_sent.cmd = -1;
    n_es::new_fall(esurf::s_mem);
}

/* edge_surf_online: true = edge surf owns this cmd ( tb + head bounce skip it ), pinned = a proven pin, held = nothing after tb may
   rewrite it */
static bool ESurf_Online(void* pLocal, UserCmd_t* cmd, bool& pinned, bool& held)
{
    using namespace esurf;
    pinned = held = false;
    s_esurfActive    = false;
    const bool key   = ESurf_Armed();
    const bool armed = cmd && pLocal && EntAlive(pLocal) && (key || ESurf_EbArmed());
    /* START must be the server's END, like texture bug */
    s_esurfSyncWant = armed;
    const auto off = [&](const char* why) {
        ESurf_Skip(cmd, why);
        s_esurfLatchCmd = -1;
        s_sent.cmd      = -1;
        s_mem.own       = false;
        return false;
    };
    if (!armed || (s_prePredFlags & FL_ONGROUND)) {
        n_es::new_fall(s_mem);
        return off(!armed ? "key" : "ground");
    }
    /* botox reads the START move type ( backup_data ): restore the frame first, the entity holds some sim's END here */
    tbx::Restore(PredCommandsPredicted() - 1);
    if (EntMoveType(pLocal) != MOVETYPE_WALK)
        return off("move");
    /* act Q stood you up: your held duck stays off until the feet pass that top ( a re-duck = feet +20 ), only while we own it */
    if (s_mem.up_cmd == cmd->command_number - 1 && s_prePredOrigin.z > s_mem.up_top && ESurf_Latched(cmd)) {
        s_mem.up_cmd = cmd->command_number;
        cmd->buttons &= ~IN_DUCK;
    }
    const bool debug   = g_debugLogEnable;
    const float pin    = TargetPredictZVel();
    const bool pin_now = fabsf(s_prePredVel.z - pin) < as::k_pinTol;
    /* eb key alone: a fresh pin START ( an edgebug just ended, its plan spent ) or our chain, nothing else */
    static int s_pin_cmd = -1;
    const bool fresh     = pin_now && s_pin_cmd != cmd->command_number - 1;
    if (pin_now)
        s_pin_cmd = cmd->command_number;
    const bool eb_live = s_ebDetected && s_ebCurrentTick <= s_ebLockTicks;   // m_found && !replay_spent( )
    if (!key && !ESurf_Latched(cmd) && (!fresh || eb_live))
        return off("eb");
    /* a locked edgebug plan flies first: a tick we own makes it drop ( EB: drop why=taken ) */
    if (s_ebDetected && !pin_now) {
        static int s_eb_cmd = -1;
        if (debug && s_eb_cmd != cmd->command_number - 1)
            DebugLog("ESURF: eb first cmd=%d vz=%.1f", cmd->command_number, s_prePredVel.z);
        s_eb_cmd = cmd->command_number;
        return off("eb");
    }
    /* botox g_air_stuck_owns_cmd: AS runs after tb here, so its last cmd ( it yields to our latch, so a cmd it owned was not ours ) */
    if (s_airStuckHit)
        return off("as");

    World w(pLocal);
    w.frame = PredCommandsPredicted() - 1;
    /* friction / duck state off the START, not this cmd's own END ( texture bug runs after the prediction ) */
    w.reset();
    w.base      = *cmd;
    w.dt        = TickInterval();
    w.pin       = pin;
    w.k_acc     = CvarFloat("sv_airaccelerate", 10.f) * w.dt * AsSurfaceFriction(pLocal);
    w.max_speed = MaxSpeed(pLocal);
    w.debug     = debug;
    w.SetupHull();
    /* act Q's release now counts one air duck: the lift after its pin needs one more press */
    w.relift_ok = AirDuckLeft(pLocal, 1);
    if (w.dt <= 0.f || w.k_acc <= 0.f)
        return off("dt");
    n_es::in_t in;
    in.range    = std::clamp(g_edgeSurfRange, 8.f, 512.f);
    in.reach    = std::clamp(g_edgeSurfReach, 0.f, 32.f);
    in.turn     = std::clamp(g_edgeSurfDeltaStrafe, 0.f, 15.f);
    in.takeover = !key;
    const n_es::out_t r = n_es::tick(w, s_mem, cmd->command_number, in);
    w.reset();
    s_vizMs     = GetTickCount64();
    s_vizPinned = r.pin;
    s_vizOrigin = s_prePredOrigin;
    s_vizHalf   = 24.f * w.scale;
    if (g_edgeSurfViz)
        VizPublish();
    if (!r.own) {
        s_esurfLatchCmd = -1;
        s_sent.cmd      = -1;
        return false;
    }
    const UserCmd_t c = w.cmd_of(r.mv);
    cmd->forwardmove  = c.forwardmove;
    cmd->sidemove     = c.sidemove;
    cmd->buttons      = c.buttons;
    pinned            = r.pin;
    held              = true;
    s_esurfActive     = true;
    if (r.pin)
        s_esurfPinCmd = cmd->command_number;
    s_esurfLatchCmd = s_mem.own ? cmd->command_number : -1;
    s_sent          = { cmd->command_number, cmd->buttons, cmd->viewangles.y, cmd->forwardmove, cmd->sidemove };
    return true;
}

/* edge_surf_eb_end ( edgebug search sim END ): true = an exact feet pin ( vz = -g dt / 2, airborne ). lift = standing beside a ledge
   face whose top it rides, drifting off it slower than 0.046 s of brake ( lab ER_EB sweep ): edge surf's air duck takes it from
   there. TF2: that duck must still be allowed ( m_nAirDucked off this sim, predicted ) */
static bool ESurf_EbEnd(void* pLocal, bool& lift)
{
    using namespace esurf;
    lift = false;
    if (!pLocal || !EntAlive(pLocal) || !g_globalVars)
        return false;
    const Vec3 o = EntV3(pLocal, g_offOrigin), v = EntV3(pLocal, g_offVelocity);
    const float pin = TargetPredictZVel();
    const int fl    = EntI(pLocal, g_offFlags);
    if ((fl & FL_ONGROUND) || fabsf(v.z - pin) >= as::k_pinTol)
        return false;
    if (fl & FL_DUCKING)
        return true;
    World w(pLocal);
    w.dt  = TickInterval();
    w.pin = pin;
    w.SetupHull();
    n_es::st_t s;
    s.o    = To3(o);
    s.v    = To3(v);
    s.walk = true;
    const n_es::hull_t h = w.hull(false);
    n_es::edge_t c[n_es::k_cand];
    const int nc = n_es::scan(w, s, h, 2.05f, true, c, n_es::k_cand);
    int best     = -1;
    float bg     = FLT_MAX;
    for (int i = 0; i < nc; ++i)
        if (const float g = n_es::gap(c[i], s.o, h); fabsf(o.z - (c[i].top + n_es::k_eps)) < n_es::k_ztol && g < bg) {
            bg   = g;
            best = i;
        }
    /* only vphysics: the lift's 20u fall back steps under 2u, its over the top pin needs more */
    if (best < 0 || n_es::vphys(c[best]))
        return true;
    const float away  = -n_es::closing(c[best], s.v);
    const float brake = CvarFloat("sv_airaccelerate", 10.f) * AsSurfaceFriction(pLocal) * MaxSpeed(pLocal) * w.crop;
    lift              = away >= -1e-3f && away <= 0.046f * brake && AirDuckLeft(pLocal, 0);
    return true;
}
