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
//     Same clip lifts a ride 0.00047 a tick: the hold's closing window is 0.0013 face ulp wide: w.pin_res.
//   - mv->m_flMaxSpeed = min(m_flMaxspeed, TF_MAX_SPEED 520) (ProcessMovement + CheckParameters), no stamina, sv_airaccelerate 10.
//   - player movement passes tf_gamerules.cpp / gamerules.cpp ShouldCollide's groups (ESurf_MovesGroup). No ladders.
//
//  NOT botox: WALK OFF ENTRY (edgesurf_walkoff.h, "walk off entry" option, default off). On the ledge with the key down:
//  ESurf_WalkOff creeps you to the edge and steps off so the first air tick is already a ride (its pin proven before the
//  step is sent). Your raw jump key hands the cmd back; an auto jump on an owned cmd is stripped (s_sent.nojump).
//
//  NOT botox: RAMP SLIDE (edgesurf_ramp.h, "ramp slide" option). The same pin on the side lip of a walkable ramp: no ground =
//  no friction, gravity runs it like a surf ramp. The csgo core flies it unchanged on a sheared world (n_esr::view) with its
//  own memory (s_slide). Order per cmd: an owned ramp chain, else the flat core, else a fresh ramp read. A ramp pin's START
//  has the slope's vz, not the pin vz: s_esurfRampStart says so to the eb gate / detections.
// ============================================================================
#include "edgesurf_core.h"
#include "edgesurf_ramp.h"
#include "edgesurf_walkoff.h"

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
    // wb0 ride window = 0.0013 face ulp a tick ( top hit at fraction 0.0026 ), halves when z ulp eats the 0.00047 lift
    float pin_res = 2.5e-4f;
    // ramp slide: CategorizePosition's ground probe is off over this vz ( tf_gamemovement.cpp:2358 )
    float probe_vz = 250.f;
    bool  hit_ent = false;   // ramp slide: the last trace met an entity ( a brush entity reads h.world too ), not the static world
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
        hit_ent         = ent;
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
static n_esr::slide_t s_slide{};   // ramp slide: this fall's ramp + the same core's memory on its sheared world
static n_esw::state_t s_walk{};    // walk off entry: this stand's tries + the first air cmd's pin press
static unsigned long long s_vizMs = 0ull;
static bool s_vizPinned = false;
static Vec3 s_vizOrigin{};
static float s_vizHalf = 24.f;

// later features may rewrite the cmd: one closing ulp over the top = standing on the ledge
// nojump = a walk off ground cmd: a jump added after us would throw the step away
static struct { int cmd = -1, buttons = 0; float yaw = 0.f, fwd = 0.f, side = 0.f; bool nojump = false; } s_sent;

// ---- visualize snapshot (movement thread -> render thread) ----
static std::mutex s_vizLock;
static ESurfDrawData s_vizOut;

// edge_surf_render's geometry, built here: a stretch of the edge under the hull centred on `s`, ticks 8u down from its ends
// rp = a ramp's read: its core's top is the sheared one, the lip is drawn at its real height end to end
static void EdgeAt(const n_es::edge_t& e, const float s, const float half, ESurfDrawData::Edge& out, const n_esr::ramp_t* rp = nullptr)
{
    const auto at = [&](const float t) {
        const float x = e.n.x * e.d - e.n.y * t, y = e.n.y * e.d + e.n.x * t;
        return Vec3(x, y, rp ? (float)rp->ztop(x, y) : e.top);
    };
    out.a = at(s - half);
    out.b = at(s + half);
}

static void VizPublish(const n_es::mem_t& m, const n_esr::ramp_t* rp)
{
    ESurfDrawData d;
    d.ms     = s_vizMs;
    d.pinned = s_vizPinned;
    d.act    = m.act;
    d.own    = m.act != 'N' && m.act != 'Y';
    const n_es::v3 o3 = To3(s_vizOrigin);
    // on the sheared world only the ramp's own face has a real height to draw at
    const auto shown = [&](const n_es::edge_t& e) { return !rp || (e.n.x * rp->fn.x + e.n.y * rp->fn.y > 0.9999f && fabsf(e.d - rp->fd) < 0.05f); };
    for (int i = 0; i < m.n_cand && d.nCand < ESurfDrawData::k_max; ++i)
        if (shown(m.cand[i]) && (!m.pick_ok || !n_es::same(m.cand[i], m.pick)))
            EdgeAt(m.cand[i], n_es::along(m.cand[i], o3), s_vizHalf, d.cand[d.nCand++], rp);
    d.pickOk = m.pick_ok && shown(m.pick);
    if (d.pickOk) {
        const n_es::edge_t& f = m.pick;
        const float s = m.arrive_ok ? n_es::along(f, m.arrive) : n_es::along(f, o3);
        EdgeAt(f, s, s_vizHalf, d.pick, rp);
        d.arriveOk = m.arrive_ok;
        const float ax = f.n.x * f.d - f.n.y * s, ay = f.n.y * f.d + f.n.x * s, az = rp ? (float)rp->ztop(ax, ay) : f.top;
        d.arrive   = Vec3(ax, ay, az);
        d.label    = Vec3(ax, ay, az + 6.f);
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
    if (s_sent.nojump)
        cmd->buttons &= ~IN_JUMP;
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
    s_esurfActive = s_esurfSyncWant = s_esurfRampStart = false;
    s_esurfLatchCmd   = -1;
    esurf::s_sent.cmd = -1;
    n_es::new_fall(esurf::s_mem);
    n_esr::drop(esurf::s_slide);
    n_esw::reset(esurf::s_walk);
    s_esurfWalkHold = 0;
}

/* walk off entry ( edgesurf_walkoff.h, NOT botox ), one ground cmd: true = it owns the cmd ( brake / creep / hold / the step ).
   the first air cmd after the step is ESurf_Online's ( n_esw::air ) */
static bool ESurf_WalkOff(void* pLocal, UserCmd_t* cmd)
{
    using namespace esurf;
    tbx::Restore(PredCommandsPredicted() - 1);
    if (EntMoveType(pLocal) != MOVETYPE_WALK)
        return false;
    World w(pLocal);
    w.frame = PredCommandsPredicted() - 1;
    w.base  = *cmd;
    w.dt    = TickInterval();
    w.pin   = TargetPredictZVel();
    /* the air tick after the step STARTs with friction 1: CategorizePosition resets it on the way off the ground ( vz 0, not rising ) */
    w.k_acc     = CvarFloat("sv_airaccelerate", 10.f) * w.dt;
    w.max_speed = MaxSpeed(pLocal);
    w.debug     = g_debugLogEnable;
    w.SetupHull();
    w.relift_ok = AirDuckLeft(pLocal, 1);
    if (w.dt <= 0.f || w.k_acc <= 0.f)
        return false;
    n_es::in_t in;
    in.range = std::clamp(g_edgeSurfRange, 8.f, 512.f);
    in.reach = std::clamp(g_edgeSurfReach, 0.f, 32.f);
    /* your keys as a world direction, raw: before auto strafe and the other movers touched the cmd */
    const float kf = (float)((s_rawButtons & IN_FORWARD) != 0) - (float)((s_rawButtons & IN_BACK) != 0);
    const float ks = (float)((s_rawButtons & IN_MOVERIGHT) != 0) - (float)((s_rawButtons & IN_MOVELEFT) != 0);
    const float ya = DEG2RADF(cmd->viewangles.y), kl = sqrtf(kf * kf + ks * ks);
    const float wx = kl > 0.f ? (kf * cosf(ya) + ks * sinf(ya)) / kl : 0.f, wy = kl > 0.f ? (kf * sinf(ya) - ks * cosf(ya)) / kl : 0.f;
    const n_es::out_t r = n_esw::ground(w, s_walk, cmd->command_number, in, wx, wy, (s_rawButtons & IN_DUCK) != 0);
    w.reset();
    if (!r.own)
        return false;
    const UserCmd_t c = w.cmd_of(r.mv);
    cmd->forwardmove  = c.forwardmove;
    cmd->sidemove     = c.sidemove;
    cmd->buttons      = c.buttons;
    s_esurfActive     = true;
    s_esurfLatchCmd   = cmd->command_number;
    s_sent            = { cmd->command_number, cmd->buttons, cmd->viewangles.y, cmd->forwardmove, cmd->sidemove, true };
    return true;
}

/* edge_surf_online: true = edge surf owns this cmd ( tb + head bounce skip it ), pinned = a proven pin, held = nothing after tb may
   rewrite it */
static bool ESurf_Online(void* pLocal, UserCmd_t* cmd, bool& pinned, bool& held)
{
    using namespace esurf;
    pinned = held = false;
    s_esurfActive    = false;
    s_esurfRampStart = false;
    const bool key   = ESurf_Armed();
    const bool armed = cmd && pLocal && EntAlive(pLocal) && (key || ESurf_EbArmed());
    /* START must be the server's END, like texture bug */
    s_esurfSyncWant = armed;
    const auto off = [&](const char* why) {
        ESurf_Skip(cmd, why);
        s_esurfLatchCmd = -1;
        s_sent.cmd      = -1;
        s_mem.own       = false;
        s_slide.m.own   = false;
        s_esurfWalkHold = 0;
        return false;
    };
    /* ramp slide: the last cmd sent a ramp pin. its START carries the slope's vz, the sheared one is the pin's */
    const float pin      = TargetPredictZVel();
    const bool ramp_sent = armed && s_slide.rp.ok && s_slide.m.sent_pin && s_slide.m.cmd == cmd->command_number - 1;
    const bool ramp_now  = ramp_sent && !(s_prePredFlags & FL_ONGROUND) && fabsf(s_slide.rp.flat_vz(To3(s_prePredVel)) - pin) < as::k_pinTol;
    s_esurfRampStart     = ramp_now;
    /* check line: last cmd sent a proven pin, this START is no pin = engine's run of that cmd left the sim. want 0 online */
    if (armed && g_debugLogEnable && s_esurfPinPrev &&
        (ramp_sent ? !ramp_now : ((s_prePredFlags & FL_ONGROUND) || fabsf(s_prePredVel.z - pin) >= as::k_pinTol)))
        DebugLog("ESURF: pred miss cmd=%d vz=%.2f z=%.3f ground=%d ramp=%d", cmd->command_number, s_prePredVel.z, s_prePredOrigin.z,
                 (s_prePredFlags & FL_ONGROUND) ? 1 : 0, ramp_sent ? 1 : 0);
    if (!armed || (s_prePredFlags & FL_ONGROUND)) {
        n_es::new_fall(s_mem);
        n_esr::drop(s_slide);
        s_esurfWalkHold = 0;
        /* walk off entry: on the ledge, your key down, no jump of your own ( an auto jump is stripped: the step is proven, it is not ) */
        if (armed && key && g_edgeSurfWalkOff && !(s_rawButtons & IN_JUMP) && !s_airStuckHit && ESurf_WalkOff(pLocal, cmd)) {
            held = true;
            return true;
        }
        return off(!armed ? "key" : "ground");
    }
    /* "auto strafe" mid-ride: your keys point off the ride, AutoStrafe flew this cmd ( pre-pred ). let go, keep the fall's memory */
    if (s_esurfSteerCmd == cmd->command_number) {
        static int s_steer_cmd = -1;
        if (g_debugLogEnable && s_steer_cmd != cmd->command_number - 1)
            DebugLog("ESURF: steer cmd=%d xy=%.1f vz=%.2f", cmd->command_number, s_prePredVel.Length2D(), s_prePredVel.z);
        s_steer_cmd = cmd->command_number;
        return off("steer");
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
    const bool pin_now = ramp_now || fabsf(s_prePredVel.z - pin) < as::k_pinTol;
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
    /* check line: a wb0 pin START carries 0.25 ( server keeps it, Pred_Simulate now too ). 1.00 here = that theory is wrong */
    if (debug && fresh)
        DebugLog("ESURF: pin start fr=%.2f", AsSurfaceFriction(pLocal));
    w.debug     = debug;
    w.SetupHull();
    /* act Q's release now counts one air duck: the lift after its pin needs one more press */
    w.relift_ok = AirDuckLeft(pLocal, 1);
    if (w.dt <= 0.f || w.k_acc <= 0.f)
        return off("dt");
    /* a ramp's act Q stood you up: the same duck hold as the flat one above, on the ramp's sheared z */
    if (s_slide.rp.ok && s_slide.m.up_cmd == cmd->command_number - 1 && ESurf_Latched(cmd)) {
        const n_es::hull_t hq = w.hull(false);
        if (s_slide.rp.flat_z(To3(s_prePredOrigin), hq.mins, hq.maxs) > s_slide.m.up_top) {
            s_slide.m.up_cmd = cmd->command_number;
            cmd->buttons    &= ~IN_DUCK;   // cmd_of strips the duck off w.base itself
        }
    }
    n_es::in_t in;
    in.range    = std::clamp(g_edgeSurfRange, 8.f, 512.f);
    in.reach    = std::clamp(g_edgeSurfReach, 0.f, 32.f);
    in.turn     = std::clamp(g_edgeSurfDeltaStrafe, 0.f, 15.f);
    in.takeover = !key;
    /* ramp slide: an owned ramp chain flies first ( its START is no flat pin ), else the flat core, then a fresh ramp read. your key
       only: an eb end is a flat pin */
    const int cn          = cmd->command_number;
    const bool ramp_on    = key && g_edgeSurfRamp;
    const bool ramp_chain = ramp_on && n_esr::chained(s_slide, cn);
    n_es::out_t r;
    bool ramp = false;
    /* walk off entry's first air cmd: the pin press its step proved, before anything else plans */
    const bool walk = key && g_edgeSurfWalkOff && (r = n_esw::air(w, s_walk, cn)).own;
    if (walk) {
        w.reset();
        s_esurfWalkHold = s_rawButtons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);   // AutoStrafe: no steer off these
    }
    if (!r.own && ramp_chain) {
        r    = n_esr::tick(w, s_slide, cn, in);
        ramp = r.own;
        w.reset();
    }
    if (!r.own) {
        r = n_es::tick(w, s_mem, cn, in);
        w.reset();
    }
    if (!r.own && ramp_on && !ramp_chain) {
        r    = n_esr::tick(w, s_slide, cn, in);
        ramp = r.own;
        w.reset();
    }
    if (!ramp_on)
        n_esr::drop(s_slide);
    /* check line: each ramp read once ( again after 2 s ). slope n.z 0.7..0.999, face square to it. top' = the core's sheared top */
    if (debug && s_slide.rp.ok && s_slide.m.cmd == cn) {
        static n_esr::ramp_t s_logged{};
        static unsigned long long s_log_ms = 0ull;
        if (const unsigned long long t = GetTickCount64(); !(s_logged.ok && n_esr::same(s_logged, s_slide.rp)) || t >= s_log_ms) {
            s_logged = s_slide.rp;
            s_log_ms = t + 2000ull;
            DebugLog("ESURF: ramp read slope n=%.4f %.4f %.4f ds=%.3f face n=%.4f %.4f d=%.3f top'=%.4f own=%d act=%c xy=%.1f vz=%.1f",
                     s_slide.rp.ax, s_slide.rp.ay, s_slide.rp.c, s_slide.rp.ds, s_slide.rp.fn.x, s_slide.rp.fn.y, s_slide.rp.fd, s_slide.rp.top,
                     ramp ? 1 : 0, s_slide.m.act, s_prePredVel.Length2D(), s_prePredVel.z);
        }
    }
    const n_es::mem_t& mm = ramp ? s_slide.m : s_mem;
    /* drawn: the ramp's read when it owns, or when it alone picked something */
    const bool rviz = ramp || (!r.own && s_slide.rp.ok && s_slide.m.cmd == cn && s_slide.m.pick_ok && !s_mem.pick_ok);
    s_vizMs     = GetTickCount64();
    s_vizPinned = r.pin;
    s_vizOrigin = s_prePredOrigin;
    s_vizHalf   = 24.f * w.scale;
    if (g_edgeSurfViz)
        VizPublish(rviz ? s_slide.m : s_mem, rviz ? &s_slide.rp : nullptr);
    if (!r.own) {
        s_esurfLatchCmd = -1;
        s_sent.cmd      = -1;
        s_esurfWalkHold = 0;
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
    s_esurfLatchCmd = (walk || mm.own) ? cmd->command_number : -1;
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
