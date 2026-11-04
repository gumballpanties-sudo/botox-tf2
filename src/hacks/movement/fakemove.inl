// ==== FAKE MOVEMENT (local server only) =====================================================
// Included by movement.cpp (same trick as texturebug.inl): rides its file-static TraceHull,
// CvarFloat, On() and entity helpers.
//
// How it works: IGameMovement::ProcessMovement (slot 1) is hooked in BOTH server.dll and
// client.dll. Server hook = the real effect (a listen server runs your usercmds in-process).
// Client hook = same edit on the prediction side, so the client predicts what the server does
// and nothing rubber-bands. Every feature is a pure edit of CMoveData + world traces, no
// player-field offsets, so both sides compute the same answer off the same inputs.
//
// Local-only gate: the server hook only touches the host's own player (handle index == local
// index). The client hook only runs while the server hook has seen us in the last second -- on
// a remote server server.dll never runs our cmds, so nothing applies and prediction is vanilla.
//
// Engine facts used (sdk-2013 tf_gamemovement.cpp / gamemovement.cpp):
//  - PlayerMove clears the ground entity up front when vz > 250, before FullWalkMove runs
//    its ground `vz = 0` -> a pre-move vz of 289 leaves the ground, even when ducked.
//  - m_flFallVelocity = -mv->m_vecVelocity.z is latched at PlayerMove start while airborne,
//    and CheckFalling hurts above 650 (TF_PLAYER_MAX_SAFE_FALL_SPEED).
//  - StartGravity/FinishGravity each take half a tick of gravity -> pre-move vz = +halfG
//    means zero vertical travel this tick.
//  - m_outJumpVel is reset in PlayerMove and set > 0 by CheckJumpButton on a real jump.
//  - FinishMove writes m_nOldButtons = m_nButtons, so skipping ProcessMovement is clean.
//  - Oct 2026: ClipVelocity does `backoff = min(backoff,0) - DIST_EPSILON` -> 0.03125 u/s off
//    every plane, which is what killed wallbugs / pixelwalks. `sv_wallbug 1` (replicated,
//    added Oct 5 2026) is Valve's own switch back. The wallbug fix bit flips it while hosting.

#include "fm_slide.h"
#include "fm_seam.h"

using ProcessMovement_t = void(__fastcall*)(void* thisptr, void* player, CMoveData* mv);
static ProcessMovement_t o_ProcessMovementCl = nullptr;
static ProcessMovement_t o_ProcessMovementSv = nullptr;

enum : int {
    FM_WALLSURF   = 1 << 0,
    FM_WALLJUMP   = 1 << 1,
    FM_HIGHJUMP   = 1 << 2,
    FM_AIRSTUCK   = 1 << 3,
    FM_NOFALL     = 1 << 4,
    FM_CROUCHJUMP = 1 << 5,
    FM_RAMPBOOST  = 1 << 6,
    FM_HEADSURF   = 1 << 7,
};
static int s_fmBits = 0;                    // bind snapshot, written once per cmd
static int   s_fmWjSeq   = 0;               // wall jumps fired (server hook) -> MvInd_t
static float s_fmWjSpeed = 0.f;             // 2D speed right after the last one
static unsigned long long s_fmServerSeenMs = 0;

static constexpr float k_fmJumpVel  = 289.f;  // CTFGameMovement::CheckJumpButton flMul
static constexpr float k_fmSafeFall = 600.f;  // under TF_PLAYER_MAX_SAFE_FALL_SPEED (650)
static constexpr float k_fmHover    = 2.5f;   // above CategorizePosition's 2u ground probe
static constexpr float k_fmSliver   = 4.f;    // head surf: max hull overlap on a ledge that still rides
static constexpr float k_fmFoot     = 2.f;    // wall surf: hull slab that rides (bottom of the box)
static const Vec3 k_fmMins(-24.f, -24.f, 0.f);
static const Vec3 k_fmMaxs( 24.f,  24.f, 62.f); // duck hull: same floor as standing, never startsolid
static const Vec3 k_fmStandMaxs(24.f, 24.f, 82.f); // anti-snag moves: standing hull, never clips a head in
// ponytail: duck hull for every probe, a wall only above 62u (standing head) is missed

static void FM_Trace(const Vec3& a, const Vec3& b, const Vec3& mins, const Vec3& maxs, GameTrace_t& tr)
{
    TraceFilterWorldProps filter;
    const int idx = GetLocalPlayerIndex();
    filter.Setup(idx > 0 ? GetClientEntity(idx) : nullptr);
    TraceHull(a, b, mins, maxs, MASK_PLAYERSOLID, &filter, &tr);
}

// standable floor under the hull: trace from org+up down to org-down
static bool FM_Floor(const Vec3& org, float down, float up, GameTrace_t& tr)
{
    FM_Trace(org + Vec3(0.f, 0.f, up), org - Vec3(0.f, 0.f, down), k_fmMins, k_fmMaxs, tr);
    return tr.fraction < 1.f && !tr.startsolid && tr.plane.normal.z >= 0.7f;
}

// ---- seam gate: wall hit sits on 2+ coplanar brushes (fm_seam.h) ----
// IEngineTrace slots, sdk-2013 IEngineTrace.h order == Amalgam x64 header:
// TraceRay 4 (kTrace_TraceRay, confirmed) ... GetStatByIndex 12, GetBrushesInAABB 13,
// GetCollidableFromDisplacementsInAABB 14, GetBrushInfo 15. Collision BSP is one copy in
// engine.dll, so the server and client hooks read the same brushes -> prediction agrees.
static constexpr int k_fmTraceBrushesInAABB = 13;
static constexpr int k_fmTraceBrushInfo     = 15;

// CUtlVector<T> x64: CUtlMemory { T* mem; int alloc; int grow; } int size; T* dbg
struct FmUtlVec { void* mem; int alloc; int grow; int size; void* dbg; };

// every solid world brush whose AABB meets [mn, mx], with its planes. fn(planes, n) true = stop.
// `cap` bounds the walk: 512 is plenty for a hull-sized window, and the whole-map scan
// (psmap.inl) passes the brush count of a map instead.
template<class Fn>
static void FM_Brushes(const float mn[3], const float mx[3], Fn&& fn, int cap = 512)
{
    using Brushes_t = void(__fastcall*)(void*, const Vec3&, const Vec3&, FmUtlVec*, int);
    using Info_t    = bool(__fastcall*)(void*, int, FmUtlVec*, int*);
    // engine grows these with its own allocator; reused forever, never freed (one buffer each)
    static thread_local FmUtlVec s_brushes{}, s_planes{};
    if (!g_engineTrace) return;
    __try {
        void** vt = *reinterpret_cast<void***>(g_engineTrace);
        s_brushes.size = 0;
        ((Brushes_t)vt[k_fmTraceBrushesInAABB])(g_engineTrace, Vec3(mn[0], mn[1], mn[2]),
            Vec3(mx[0], mx[1], mx[2]), &s_brushes, (int)MASK_PLAYERSOLID);
        for (int i = 0; i < s_brushes.size && i < cap; ++i)
        {
            const int brush = static_cast<int*>(s_brushes.mem)[i];
            int contents = 0;
            s_planes.size = 0;
            if (!((Info_t)vt[k_fmTraceBrushInfo])(g_engineTrace, brush, &s_planes, &contents)) continue;
            if (!(contents & MASK_PLAYERSOLID) || s_planes.size <= 0 || !s_planes.mem) continue;
            if (fn(static_cast<const FmPlane*>(s_planes.mem), s_planes.size)) return;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// tr = hull trace that hit the wall. Window = hull box at the contact, 1u into the wall
// (+ slack), so a seam counts while it is under the hull face. Brushes with a face on the hit
// plane in there, counting stops at 2 (= seam). `top` = how far up the hull the window reaches:
// the wall surf passes the foot slab, so the seam has to be AT the feet, not anywhere overhead.
static int FM_SeamCount(const GameTrace_t& tr, float top = k_fmMaxs.z)
{
    const float pad = 1.f + (std::max)(g_fmSeamSlack, 0.f);
    const Vec3 lo = tr.endpos + k_fmMins, hi = tr.endpos + Vec3(k_fmMaxs.x, k_fmMaxs.y, top);
    const float mn[3] = { lo.x - pad, lo.y - pad, lo.z - pad };
    const float mx[3] = { hi.x + pad, hi.y + pad, hi.z + pad };
    const Vec3& n = tr.plane.normal;
    int count = 0;
    FM_Brushes(mn, mx, [&](const FmPlane* p, int np) {
        return FmSeam_BrushCounts(p, np, mn, mx, n.x, n.y, n.z, tr.plane.dist) && ++count >= 2;
    });
    return count;
}

static bool FM_Seam(const GameTrace_t& tr) { return FM_SeamCount(tr) >= 2; }
static bool FM_FootSeam(const GameTrace_t& tr) { return FM_SeamCount(tr, k_fmFoot) >= 2; }

// entity hit whose model is a studio model (prop, building), not a brush model ("*N" = func_brush,
// door). IVModelInfoClient GetModel 1 / GetModelName 3 (visuals.cpp ModelByIndex).
static bool FM_StudioEnt(void* ent)
{
    if (!g_modelInfo || g_offModelIndex <= 0) return false;
    __try {
        // m_nModelIndex is a short (skins.cpp masks the int read); negative = client-side model
        const void* model = VCall<const void*>(g_modelInfo, 1, (int)(short)EntI(ent, g_offModelIndex));
        const char* name = reinterpret_cast<uintptr_t>(model) < 0x10000 ? nullptr
                         : VCall<const char*>(g_modelInfo, 3, model);
        return reinterpret_cast<uintptr_t>(name) >= 0x10000 && name[0] && name[0] != '*';
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// wall jump "seams only": a brush seam, or a face that is no brush at all -- a displacement or a
// prop's collision model. World hits are brushes, displacements (dispFlags) and static props
// (sdk gametrace.h: world m_pEnt, hitbox = prop index); a static prop has no brush face on the hit
// plane, so 0 brushes = static prop. Brush entities (func_brush, doors) never pass: not a studio
// model, and their brushes are not in the world list a seam is counted from.
static const char* s_fmJumpKind = "";   // last pass = FM_Wall's winner (later passes are nearer)
static bool FM_JumpSeam(const GameTrace_t& tr)
{
    const char* kind = nullptr;
    if (tr.dispFlags & DISPSURF_SURFACE)                   kind = "disp";
    else if (tr.m_pEnt && tr.m_pEnt != GetClientEntity(0)) kind = FM_StudioEnt(tr.m_pEnt) ? "prop" : nullptr;
    else switch (FM_SeamCount(tr)) { case 0: kind = "static prop"; break; case 2: kind = "seam"; break; }
    if (kind) s_fmJumpKind = kind;
    return kind != nullptr;
}

// nearest steep face within reach, 8 directions. gate = only faces it passes (seam options).
// `top` = probe hull height; the wall surf hands it the foot slab so only a face beside the
// BOTTOM of the box counts, the way a real pixel surf is held by a lip under your feet.
static bool FM_Wall(const Vec3& org, float reach, Vec3& normal, short* surf = nullptr,
                    bool (*gate)(const GameTrace_t&) = nullptr, float top = k_fmMaxs.z)
{
    float best = 1.f;
    bool hit = false;
    for (int i = 0; i < 8; ++i)
    {
        const float a = DEG2RADF(45.f * i);
        GameTrace_t tr;
        FM_Trace(org, org + Vec3(cosf(a) * reach, sinf(a) * reach, 0.f), k_fmMins,
                 Vec3(k_fmMaxs.x, k_fmMaxs.y, top), tr);
        if (tr.fraction < best && !tr.startsolid && fabsf(tr.plane.normal.z) < 0.7f
            && (!gate || gate(tr)))
        {
            best = tr.fraction;
            normal = tr.plane.normal;
            if (surf) *surf = tr.surface.surfaceProps;
            hit = true;
        }
    }
    return hit;
}

// wall/prop/building side within dist of the hull. Floors + ceilings never count: FM_Wall sweeps
// sideways and keeps only steep faces (|nz| < 0.7). +1u covers the DIST_EPSILON gap -> 0 = touching.
static bool FM_Near(const Vec3& org, float dist)
{
    Vec3 n;
    return FM_Wall(org, dist + 1.f, n);
}

// ---- head surf: brush bottom EDGE at the hull top (fm_seam.h FmSeam_BottomEdge) ----
static constexpr float k_fmCeilGap = 0.25f;   // hull top this far under the bottom while riding
// IServerUnknown / IClientUnknown::GetCollideable 3 (IHandleEntity dtor, SetRefEHandle,
// GetRefEHandle first), ICollideable::OBBMaxs 4 (GetEntityHandle, OBBMinsPreScaled,
// OBBMaxsPreScaled, OBBMins first) -- sdk-2013 public headers == Amalgam defs.h
static constexpr int k_fmEntGetCollideable = 3;
static constexpr int k_fmCollOBBMaxs       = 4;

// Hull height this move runs with. CBasePlayer::PostThink (player.cpp) and C_BasePlayer::PostThink
// (c_baseplayer.cpp) set the collision bounds from FL_DUCKING after every cmd, so pre-move OBBMaxs
// = the hull TracePlayerBBox is about to use, on both sides. An air duck toggle this tick shifts
// the origin by the height difference and keeps the hull TOP put (gamemovement.cpp FinishDuck /
// FinishUnDuck), and the top is what the ride pins.
static bool FM_HullHeight(void* player, float& h)
{
    void* coll = VCall<void*>(player, k_fmEntGetCollideable);
    if (reinterpret_cast<uintptr_t>(coll) < 0x10000) return false;
    const Vec3* mx = VCall<const Vec3*>(coll, k_fmCollOBBMaxs);
    if (reinterpret_cast<uintptr_t>(mx) < 0x10000 || !(mx->z > 30.f && mx->z < 100.f)) return false;
    h = mx->z;
    return true;
}

// brush bottom edge nearest the hull top at `org`, height h: bottom from 1u + `down` under the
// top to 1u + `up` over it (the 1u keeps a ride, top k_fmCeilGap under, alive at snap 0; down =
// this tick's fall, so a fast fall catches the edge before crossing it), edge within wall reach
// of the hull's side, at most a sliver over the hull. n = edge outward xy normal (stick).
static bool FM_BottomEdge(const Vec3& org, float h, float up, float down, float& bz, float& nx, float& ny)
{
    const float pad = (std::max)(g_fmWallReach, 1.f);
    const float top = org.z + h;
    const float zlo = top - 1.f - down, zhi = top + 1.f + up;
    const float hmn[2] = { org.x + k_fmMins.x, org.y + k_fmMins.y };
    const float hmx[2] = { org.x + k_fmMaxs.x, org.y + k_fmMaxs.y };
    const float mn[3] = { hmn[0] - pad, hmn[1] - pad, zlo };
    const float mx[3] = { hmx[0] + pad, hmx[1] + pad, zhi + 0.5f };
    bool found = false;
    FM_Brushes(mn, mx, [&](const FmPlane* p, int np) {
        float z = 0.f, ex = 0.f, ey = 0.f;
        if (FmSeam_BottomEdge(p, np, hmn, hmx, pad, k_fmSliver, zlo, zhi, z, ex, ey)
            && (!found || fabsf(z - top) < fabsf(bz - top)))
        { bz = z; nx = ex; ny = ey; found = true; }
        return false;
    });
    return found;
}

struct FmCtx
{
    bool  skip = false;       // air stuck: original not called
    bool  onGround = false;   // measured pre-move
    float groundNz = 1.f;
    bool  groundJump = false; // our own crouch jump fired off the ground
    bool  newJump = false;    // fresh jump press (m_nOldButtons is gone after the move)
    bool  fakeJump = false;   // any of our jumps fired -> anim + sound
    bool  wallJump = false;   // the wall jump fired -> velocity hud takeoff
    short jumpSurf = 0;       // surface prop it jumped off (step sound)
    bool  ride = false;       // head surf hover / wall surf pin this tick -> anti-snag post
    Vec3  rideOrg, rideVel;   // what the engine starts the move with
    float rideH = 0.f;        // real hull height when known (bottom edge ride), else standing
    bool  edgeRide = false;   // head surf bottom edge ride -> post stick
    float edgeNx = 0.f, edgeNy = 0.f;   // its outward xy normal
    bool  server = false;
};

// ---- jump anim + sound ----
// Real jump (CTFGameMovement::CheckJumpButton) does DoAnimationEvent(PLAYERANIMEVENT_JUMP) +
// PlayStepSound off the surface. Ours bypass that code, so replay both by hand. Fired from the
// SERVER hook only: it runs once per cmd, the client hook re-runs on every prediction replay.
static constexpr int k_fmAnimJump = 6;   // PlayerAnimEvent_t::PLAYERANIMEVENT_JUMP
// surfacedata_t: physics (5 floats) + audio (6 floats) -> sounds.stepleft @44, stepright @46
static constexpr int k_fmSurfStepLeft = 44;
static constexpr int k_fmPropsGetSurfaceData = 5;   // IPhysicsSurfaceProps (dtor 0)
static constexpr int k_fmPropsGetString      = 6;

static void FM_JumpFx(short surf)
{
    static bool s_init = false;
    static void* s_fnAnim = nullptr;
    static void* s_props  = nullptr;
    if (!s_init)
    {
        s_init = true;
        // C_TFPlayer::DoAnimationEvent(event, nData) -- Amalgam CTFPlayer_DoAnimationEvent sig
        s_fnAnim = PatternScan(GetModuleHandleA("client.dll"), "48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 41 8B E8 8B FA");
        s_props  = GetInterface("vphysics.dll", "VPhysicsSurfaceProps001");
        DebugLog("fakemove: jump fx anim=%p props=%p", s_fnAnim, s_props);
    }

    const int idx = GetLocalPlayerIndex();
    void* pLocal = idx > 0 ? GetClientEntity(idx) : nullptr;
    if (pLocal && s_fnAnim)
    {
        // C_TFPlayer::DoAnimationEvent drops local-player events unless
        // prediction->IsFirstTimePredicted() (c_tf_player.cpp). Server hook + demo tick run outside
        // prediction, where that flag is stale (always 0 in a demo) -> anim hit-or-miss. Force it on
        // for the call; main thread, prediction not mid-run here.
        unsigned char* first = g_prediction
            ? reinterpret_cast<unsigned char*>(g_prediction) + kPredOff_FirstTimePredicted : nullptr;
        const unsigned char old = first ? *first : 0;
        if (first) *first = 1;
        __try { ((void(__fastcall*)(void*, int, int))s_fnAnim)(pLocal, k_fmAnimJump, 0); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (first) *first = old;
    }

    // step sound name off the surface, alternating feet like PlayStepSound's m_nStepside
    static bool s_side = false;
    s_side = !s_side;
    const char* name = nullptr;
    if (s_props)
    {
        __try {
            void** vt = *reinterpret_cast<void***>(s_props);
            using GetData_t = void*(__fastcall*)(void*, int);
            using GetStr_t  = const char*(__fastcall*)(void*, unsigned short);
            if (void* data = ((GetData_t)vt[k_fmPropsGetSurfaceData])(s_props, surf))
            {
                const unsigned short snd = *reinterpret_cast<unsigned short*>(
                    reinterpret_cast<uintptr_t>(data) + k_fmSurfStepLeft + (s_side ? 0 : 2));
                if (snd) name = ((GetStr_t)vt[k_fmPropsGetString])(s_props, snd);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { name = nullptr; }
    }
    if (!name || !*name || strlen(name) > 96) name = s_side ? "Concrete.StepLeft" : "Concrete.StepRight";

    // playgamesound (client build) = C_BasePlayer::GetLocalPlayer()->EmitSound(name)
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "playgamesound %s", name);
    if (g_engine)
    {
        __try {
            void** vt = *reinterpret_cast<void***>(g_engine);
            ((void(__fastcall*)(void*, const char*))vt[106])(g_engine, cmd);   // ClientCmd_Unrestricted
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

static bool FM_Local(void* player, CMoveData* mv, bool server)
{
    if (g_eject || !player || !mv) return false;
    const int idx = GetLocalPlayerIndex();
    if (idx <= 0) return false;
    if (server)
    {
        // server calls this for EVERY player's usercmd (player_command.cpp RunCommand, handle =
        // that player's ref ehandle). Exact entry index (ENT_ENTRY_MASK 0xFFFF, const.h) -- a
        // narrower mask would alias index 4096+ onto ours. Anyone else: untouched vanilla move.
        if ((mv->m_nPlayerHandle & 0xFFFF) != (unsigned long)idx) return false;
        // stamp BEFORE the bits check: "we host" must hold with no bind held, or the
        // wallbug fix (not a bind) never sees the gate open
        s_fmServerSeenMs = GetTickCount64();
        return s_fmBits != 0;
    }
    return s_fmBits && player == GetClientEntity(idx) && GetTickCount64() - s_fmServerSeenMs < 1000;
}

static void FM_Pre(void* player, CMoveData* mv, FmCtx& c)
{
    const int b = s_fmBits;
    const float dt    = g_globalVars ? g_globalVars->interval_per_tick : 0.015f;
    const float halfG = 0.5f * CvarFloat("sv_gravity", 800.f) * dt;
    Vec3& vel = mv->m_vecVelocity;
    const Vec3 org = mv->m_vecAbsOrigin;
    const bool newJump = (mv->m_nButtons & IN_JUMP) && !(mv->m_nOldButtons & IN_JUMP);
    const float jumpVel = k_fmJumpVel * ((b & FM_HIGHJUMP) ? (std::max)(g_fmHighJumpMult, 1.f) : 1.f);

    // ---- air stuck: no move at all this tick ----
    // near-only: freezes just while a brush/prop is within reach, else the move runs normally
    if ((b & FM_AIRSTUCK) && (!g_fmAirStuckNearOnly || FM_Near(org, (std::max)(g_fmAirStuckNearDist, 0.f))))
    {
        if (!g_fmAirStuckKeepVel) vel.Set();
        mv->m_nOldButtons = mv->m_nButtons;
        c.skip = true;
        return;
    }
    if (!(b & ~FM_HIGHJUMP)) return;   // high jump alone is post-only, no traces

    GameTrace_t gtr;
    c.newJump = newJump;
    c.onGround = vel.z <= 250.f && FM_Floor(org, 2.f, 0.f, gtr);
    if (c.onGround) { c.groundNz = gtr.plane.normal.z; c.jumpSurf = gtr.surface.surfaceProps; }

    if (c.onGround)
    {
        // ---- crouch jump: TF refuses a jump while FL_DUCKING; vz > 250 lifts us anyway ----
        if ((b & FM_CROUCHJUMP) && newJump && (mv->m_nButtons & IN_DUCK))
        {
            vel.z = jumpVel;
            mv->m_nOldButtons |= IN_JUMP;   // engine jump (and scout air dash) stays out
            c.groundJump = true;
            c.fakeJump = true;
        }
        return;
    }

    bool jumped = false, held = false;
    const float reach = (std::max)(g_fmWallReach, 1.f);

    // ---- wall jump: a real ground jump off the wall ----
    // Ground jump = PreventBunnyJumping (speed over 1.2x m_flMaxspeed scaled down to it, vz ~0
    // on the ground) then vz 0 -> 289 (x higher jumps). Same here: horizontal cropped, fall speed
    // dropped, vz becomes the jump. Push off the wall is optional (def 0), added after the crop.
    // Fires on a fresh press, or (hold option) with jump still held once the last jump stops
    // rising -- bhop holds space the whole time, so "fresh press only" meant one jump per wall.
    const bool heldJump = g_fmWallJumpHold && (mv->m_nButtons & IN_JUMP) && vel.z <= 0.f;
    Vec3 n;
    // seams only: brush seams, displacements and props (FM_JumpSeam)
    if ((b & FM_WALLJUMP) && (newJump || heldJump)
        && FM_Wall(org, reach, n, &c.jumpSurf, g_fmWallJumpSeam ? &FM_JumpSeam : nullptr))
    {
        if (g_debugLogEnable && c.server && g_fmWallJumpSeam)
            DebugLog("fm wj: jump off %s nz=%.2f", s_fmJumpKind, n.z);
        // PreventBunnyJumping (tf_gamemovement.cpp:1092). m_flMaxspeed off the client entity:
        // server CBasePlayer layout differs, and mv->m_flMaxSpeed is only set inside the original.
        // Local listen server, so client value = server value -> both hooks crop the same.
        const int li = GetLocalPlayerIndex();
        void* lp = (li > 0 && g_offMaxspeed > 0) ? GetClientEntity(li) : nullptr;
        const float cap = lp ? 1.2f * EntF(lp, g_offMaxspeed) : 0.f;
        const float spd = sqrtf(vel.x * vel.x + vel.y * vel.y);
        if (cap > 0.f && spd > cap)
        {
            vel.x *= cap / spd;
            vel.y *= cap / spd;
        }
        if (g_fmWallJumpPush > 0.f)
        {
            n.z = 0.f; n.Normalize();
            const float into = vel.x * n.x + vel.y * n.y;
            if (into < 0.f) { vel.x -= n.x * into; vel.y -= n.y * into; }
            vel.x += n.x * g_fmWallJumpPush;
            vel.y += n.y * g_fmWallJumpPush;
        }
        vel.z = jumpVel;
        mv->m_nOldButtons |= IN_JUMP;
        jumped = true;
        c.fakeJump = true;
        c.wallJump = true;
    }

    // ---- head surf: ride the top of any ledge at hover height, air physics ----
    if (!jumped && (b & FM_HEADSURF) && vel.z <= 0.f)
    {
        const float snap = (std::max)(g_fmHeadSurfSnap, 0.f);
        GameTrace_t tr;
        const float down = -vel.z * dt + 2.f * halfG * dt + 4.f;
        if (FM_Floor(org, down, snap, tr))
        {
            const float top = tr.endpos.z;
            bool ledge = true;
            if (g_fmHeadSurfLedgeOnly)
            {
                // center over solid = standing on a floor, not hanging off a ledge -> land
                GameTrace_t ctr;
                FM_Trace(Vec3(org.x, org.y, top + 1.f), Vec3(org.x, org.y, top - 8.f), Vec3(), Vec3(), ctr);
                ledge = !(ctr.fraction < 1.f && !ctr.startsolid);

                // real pixel surf = the hull edge on a sliver of the ledge. More overlap than that
                // and TF just stands you on the edge, so land (ground physics, walk off normally)
                // instead of hovering there on air control. Drift off the sliver -> fall.
                if (ledge)
                {
                    const Vec3 in(k_fmSliver, k_fmSliver, 0.f);
                    GameTrace_t str;
                    FM_Trace(org + Vec3(0.f, 0.f, snap), org - Vec3(0.f, 0.f, down), k_fmMins + in, k_fmMaxs - in, str);
                    ledge = !(str.fraction < 1.f && !str.startsolid && str.plane.normal.z >= 0.7f);
                }
            }
            if (ledge)
            {
                mv->m_vecAbsOrigin.z = top + k_fmHover;
                mv->m_bGameCodeMovedPlayer = true;   // re-categorize off the snapped origin
                if (newJump)
                {
                    vel.z = jumpVel; mv->m_nOldButtons |= IN_JUMP; jumped = true;
                    c.fakeJump = true; c.jumpSurf = tr.surface.surfaceProps;
                }
                else           { vel.z = halfG; c.ride = true; }
                held = true;
            }
        }
    }

    // ---- head surf, brush bottoms: upside-down pixel surf on a brush bottom EDGE ----
    // A bottom at the hull top whose underside ends beside the hull (a seam in the wall next to
    // you, the lip of an overhang you are a sliver under) holds the top k_fmCeilGap under it:
    // zero vertical travel, air physics sideways. Within snap up (+ this tick's rise) under one =
    // pulled up to it; one the top would fall past this tick = caught on it first, any speed.
    // A bottom over the whole hull is a ceiling and never rides. FM_Post sticks the hull to the
    // edge (FmSeam_Stick): off it only by sliding past its end or strafing away.
    bool edgeRide = false;
    if (!jumped && !held && (b & FM_HEADSURF) && g_fmHeadSurfBottoms)
    {
        float h = 0.f, bz = 0.f, nx = 0.f, ny = 0.f;
        const float up   = (std::max)(g_fmHeadSurfSnap, 0.f) + (std::max)(vel.z, 0.f) * dt;
        const float down = (std::max)(halfG - vel.z, 0.f) * dt;   // StartGravity, then the move
        if (FM_HullHeight(player, h) && FM_BottomEdge(org, h, up, down, bz, nx, ny))
        {
            // the hull must get there: nothing between (a lip over a sliver of the hull is above
            // the target, the pull-up stops short of it), and nothing at the spot
            const Vec3 to(org.x, org.y, bz - k_fmCeilGap - h);
            GameTrace_t tr;
            FM_Trace(org, to, k_fmMins, Vec3(k_fmMaxs.x, k_fmMaxs.y, h), tr);
            if (!tr.startsolid && !tr.allsolid && tr.fraction >= 1.f)
            {
                if (g_debugLogEnable && c.server && fabsf(to.z - org.z) > 0.5f)
                    DebugLog("fm hs: catch bz=%.2f h=%.0f dz=%.2f vz=%.1f", bz, h, to.z - org.z, vel.z);
                mv->m_vecAbsOrigin = to;
                mv->m_bGameCodeMovedPlayer = true;   // re-categorize off the moved origin
                vel.z = halfG;
                c.ride = true;
                c.rideH = h;
                c.edgeRide = true; c.edgeNx = nx; c.edgeNy = ny;
                held = edgeRide = true;
            }
        }
    }
    if (c.server && (b & FM_HEADSURF))
    {
        static bool s_edgeWas = false;   // server runs once per cmd: ride start/end, not replays
        if (g_debugLogEnable && s_edgeWas && !edgeRide) DebugLog("fm hs: drop");
        s_edgeWas = edgeRide;
    }

    // ---- wall surf: pin vz while the BOTTOM of the hull is still alongside a wall face ----
    // A real pixel surf is held by a lip under your feet, so every probe here is the foot slab
    // (k_fmMins .. k_fmFoot), never the whole hull: a face that only reaches your chest or head
    // does nothing, and rising or dropping past the lip ends the ride. Ride also dies the tick
    // the wall does: no latch past the brush end, and a diagonal probe catching the wall's corner
    // doesn't count -- the face has to still be there straight along its own normal. Also ends on
    // landing, jump, or bind release. Seam option: the straight-on hit must sit on a brush seam
    // AT the feet (FM_FootSeam), i.e. a real pixel surf -- slide off the seam and you fall.
    const Vec3 footMaxs(k_fmMaxs.x, k_fmMaxs.y, k_fmFoot);
    if (!jumped && !held && (b & FM_WALLSURF) && vel.z < halfG
        && FM_Wall(org, reach, n, nullptr, g_fmWallSurfSeam ? &FM_FootSeam : nullptr, k_fmFoot))
    {
        n.z = 0.f; n.Normalize();
        GameTrace_t tr;
        FM_Trace(org, org - n * reach, k_fmMins, footMaxs, tr);
        if (tr.fraction < 1.f && !tr.startsolid
            && tr.plane.normal.x * n.x + tr.plane.normal.y * n.y > 0.99f
            && (!g_fmWallSurfSeam || FM_FootSeam(tr)))
        {
            vel.z = halfG;
            c.ride = true;
        }
    }

    if (c.ride) { c.rideOrg = mv->m_vecAbsOrigin; c.rideVel = vel; }

    // ---- no fall damage: cap vz on the ticks that can reach the floor ----
    if ((b & FM_NOFALL) && vel.z < -k_fmSafeFall)
    {
        GameTrace_t tr;
        if (FM_Floor(org, (-vel.z + 2.f * halfG) * dt * 2.f + 4.f, 0.f, tr))
            vel.z = -k_fmSafeFall;
    }
}

static void FM_Post(CMoveData* mv, FmCtx& c)
{
    const int b = s_fmBits;
    if (c.skip) return;
    Vec3& vel = mv->m_vecVelocity;
    const bool engineJump = mv->m_outJumpVel.z > 0.f;
    const float dt = g_globalVars ? g_globalVars->interval_per_tick : 0.015f;

    // ---- surf anti-snag: air move has no step-up and zeroes on creases, so lips, brush seams
    // and edges stopped the ride dead. Engine fell short -> redo the tick's horizontal move
    // (slide / step up + slide), fm_slide.h. Same traces both hooks, so prediction agrees.
    if (c.ride && !engineJump)
    {
        const Vec3 maxs = c.rideH > 0.f ? Vec3(k_fmMaxs.x, k_fmMaxs.y, c.rideH) : k_fmStandMaxs;
        auto trace = [&maxs](const Vec3& a, const Vec3& b) {
            GameTrace_t t;
            FM_Trace(a, b, k_fmMins, maxs, t);
            FmHit<Vec3> h;
            h.frac = t.fraction; h.end = t.endpos; h.n = t.plane.normal;
            h.startSolid = t.startsolid || t.allsolid;
            return h;
        };
        Vec3 org, v;
        if (FmUnsnag(trace, c.rideOrg, c.rideVel, dt, CvarFloat("sv_stepsize", 18.f),
                     mv->m_vecAbsOrigin, org, v))
        {
            mv->m_vecAbsOrigin = org;
            vel.x = v.x;
            vel.y = v.y;
        }
    }

    // ---- head surf stick: the edge holds you (fm_seam.h FmSeam_Stick). Wish = AirMove's:
    // view yaw forward/right flattened (roll 0) x forward/side move. Pull-back is traced so it
    // never lands in anything the engine slid around.
    if (c.edgeRide && !engineJump)
    {
        const float yaw = DEG2RADF(mv->m_vecViewAngles.y);
        const float cy = cosf(yaw), sy = sinf(yaw);
        const float wx = cy * mv->m_flForwardMove + sy * mv->m_flSideMove;
        const float wy = sy * mv->m_flForwardMove - cy * mv->m_flSideMove;
        const Vec3 post = mv->m_vecAbsOrigin;
        const float dn = FmSeam_Stick(c.edgeNx, c.edgeNy, wx, wy, post.x - c.rideOrg.x, post.y - c.rideOrg.y,
                                      vel.x, vel.y);
        if (dn != 0.f)
        {
            GameTrace_t tr;
            FM_Trace(post, post - Vec3(c.edgeNx * dn, c.edgeNy * dn, 0.f), k_fmMins,
                     Vec3(k_fmMaxs.x, k_fmMaxs.y, c.rideH), tr);
            if (!tr.startsolid && !tr.allsolid) mv->m_vecAbsOrigin = tr.endpos;
        }
    }

    // ---- crouch jump, mid duck/unduck: IN_DUCK already released but FL_DUCKING still set for
    // the 0.2s unduck, and CheckJumpButton refuses that too. Flags aren't readable server side,
    // so let the engine try first: fresh press, floor under us pre-move, engine said no, still
    // grounded (vz 0) -> jump now. Lifts one tick late: next tick starts at vz = jump, exactly
    // what the pre path starts its launch tick with, so the arc is identical, just shifted.
    if ((b & FM_CROUCHJUMP) && c.newJump && c.onGround && !c.groundJump && !engineJump && vel.z == 0.f)
    {
        vel.z = k_fmJumpVel * ((b & FM_HIGHJUMP) ? (std::max)(g_fmHighJumpMult, 1.f) : 1.f);
        c.groundJump = true;
        c.fakeJump = true;
    }

    // our own jumps already carry the multiplier
    if ((b & FM_HIGHJUMP) && engineJump)
        vel.z += mv->m_outJumpVel.z * (g_fmHighJumpMult - 1.f);

    if ((b & FM_RAMPBOOST) && c.onGround && (engineJump || c.groundJump) && c.groundNz < 0.985f)
    {
        vel.x *= g_fmRampBoostMult;
        vel.y *= g_fmRampBoostMult;
    }
}

static bool FM_PreSafe(void* player, CMoveData* mv, bool server, FmCtx& c)
{
    __try {
        if (!FM_Local(player, mv, server)) return false;
        c.server = server;
        FM_Pre(player, mv, c);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        c.skip = false;
        return false;
    }
}

static void FM_PostSafe(CMoveData* mv, FmCtx& c)
{
    __try { FM_Post(mv, c); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void __fastcall h_ProcessMovementSv(void* thisptr, void* player, CMoveData* mv)
{
    FmCtx c;
    const bool on = FM_PreSafe(player, mv, true, c);
    if (!c.skip) o_ProcessMovementSv(thisptr, player, mv);
    if (on) FM_PostSafe(mv, c);
    if (on && c.fakeJump) FM_JumpFx(c.jumpSurf);   // server side only: once per cmd
    // velocity hud: wall jump never leaves the ground flag, so hand the hud its takeoff here.
    // Server hook = once per real cmd; the client hook also runs inside every eb/ps sim
    if (on && c.wallJump)
    {
        s_fmWjSpeed = mv->m_vecVelocity.Length2D();
        ++s_fmWjSeq;
    }
}

static void __fastcall h_ProcessMovementCl(void* thisptr, void* player, CMoveData* mv)
{
    FmCtx c;
    const bool on = FM_PreSafe(player, mv, false, c);
    if (!c.skip) o_ProcessMovementCl(thisptr, player, mv);
    if (on) FM_PostSafe(mv, c);
}

// ---- demo playback: replay jump fx ----
// Nothing of ours reaches a demo: the FX are client-side calls, and even TF's own jump anim
// TE skips the jumper (tf_player.cpp TE_PlayerAnimEvent). What the demo DOES carry is the
// recorder's networked m_vecVelocity + m_fFlags, and a fake jump leaves a fingerprint there:
// vz = 289*m - 2*halfG one tick after launch (same number as a ground jump), but launched
// from the AIR (wall jump / head surf jump) or from the ground while FL_DUCKING (crouch jump),
// which TF itself never does. Scout air dash (~262), knockback and arcs never land on it.
// Called from FrameStageNotify stage 3 (net update end), which runs during playback.
static constexpr int k_fmEngIsPlayingDemo = 76;   // IVEngineClient, flipmath.h engIsPlayingDemo

void FakeMove_DemoTick()
{
    static int   s_prevTick = -1;
    static float s_prevVz = 0.f;
    static int   s_prevFlags = 0;

    if (g_eject || !g_fmEnabled || !g_engine || !g_globalVars || g_offVelocity <= 0 || g_offFlags <= 0) return;
    bool demo = false;
    __try { demo = VCall<bool>(g_engine, k_fmEngIsPlayingDemo); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (!demo) { s_prevTick = -1; return; }

    const int idx = GetLocalPlayerIndex();
    void* pLocal = idx > 0 ? GetClientEntity(idx) : nullptr;
    if (!pLocal || !EntAlive(pLocal)) { s_prevTick = -1; return; }

    const int tick = g_globalVars->tickcount;
    if (tick == s_prevTick) return;               // one sample per demo tick
    const float vz    = EntV3(pLocal, g_offVelocity).z;
    const int   flags = EntI(pLocal, g_offFlags);
    const int   gap   = tick - s_prevTick;
    const bool  have  = s_prevTick >= 0 && gap >= 1 && gap <= 3;
    const float prevVz = s_prevVz;
    const int   prevFlags = s_prevFlags;
    s_prevTick = tick; s_prevVz = vz; s_prevFlags = flags;
    if (!have || vz < prevVz + 100.f) return;

    const float dt    = g_globalVars->interval_per_tick;
    const float halfG = 0.5f * CvarFloat("sv_gravity", 800.f) * dt;
    auto matches = [&](float m) {
        // gap > 1: packet skipped ticks, the arc has lost 2*halfG per extra tick.
        // Post-path crouch jump (mid unduck) records plain jump vz, one tick behind (+2*halfG).
        const float target = k_fmJumpVel * m - 2.f * halfG * gap;
        return fabsf(vz - target) < 1.f || fabsf(vz - (target + 2.f * halfG)) < 1.f;
    };
    if (!matches(1.f) && !matches((std::max)(g_fmHighJumpMult, 1.f))) return;

    const bool fromAir     = !(prevFlags & FL_ONGROUND);
    const bool duckedFloor = (prevFlags & FL_ONGROUND) && (prevFlags & FL_DUCKING);
    if (!fromAir && !duckedFloor) return;         // plain ground jump: TF's own, not ours

    // surface for the step sound: the wall beside us, else the floor/ledge under us
    const Vec3 org = EntV3(pLocal, g_offOrigin);
    short surf = 0;
    Vec3 n;
    GameTrace_t tr;
    if (!(fromAir && FM_Wall(org, 24.f, n, &surf)) && FM_Floor(org, 24.f, 0.f, tr))
        surf = tr.surface.surfaceProps;
    FM_JumpFx(surf);
}

// bhop hook-in (AutoJump): wall jump bind live, local server, wall in reach of the origin the
// server's pre-move will trace from -- same FM_Wall, same reach, so both sides agree
static bool FakeMove_WallJumpReady(void* pLocal)
{
    if (!(s_fmBits & FM_WALLJUMP) || !pLocal || g_offOrigin <= 0) return false;
    if (GetTickCount64() - s_fmServerSeenMs >= 1000) return false;
    Vec3 n;
    return FM_Wall(EntV3(pLocal, g_offOrigin), (std::max)(g_fmWallReach, 1.f), n, nullptr,
                   g_fmWallJumpSeam ? &FM_JumpSeam : nullptr);
}

// ---- wallbug fix: sv_wallbug 1 while we host (gate: the server hook saw our cmd in the last
// second). Sent as a real console command (ClientCmd_Unrestricted, slot 106 -- same call as
// FM_StepSound), so the host's console runs it exactly like typing it: replicate callback,
// prediction copy, all of it. ConVar* only to read the old value handed back on toggle off,
// disconnect, eject.
static bool s_wbApplied = false;
static int  s_wbSaved = 0;

static ConVar* FM_WallbugVar()
{
    static ConVar* cv = nullptr;
    static unsigned long long nextFindMs = 0;
    if (cv || !g_cvar) return cv;
    const unsigned long long now = GetTickCount64();
    if (now < nextFindMs) return nullptr;
    nextFindMs = now + 2000;   // server.dll registers it when the listen server comes up
    cv = g_cvar->FindVarWalk("sv_wallbug");
    return cv;
}

static void FM_WallbugCvar(bool want)
{
    if (want == s_wbApplied || !g_engine) return;
    if (want)
    {
        ConVar* cv = FM_WallbugVar();
        s_wbSaved = cv ? static_cast<int>(cv->GetFloat()) : 0;
    }
    const int to = want ? 1 : s_wbSaved;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "sv_wallbug %d", to);
    __try {
        void** vt = *reinterpret_cast<void***>(g_engine);
        ((void(__fastcall*)(void*, const char*))vt[106])(g_engine, cmd);   // ClientCmd_Unrestricted
    } __except (EXCEPTION_EXECUTE_HANDLER) { DebugLog("fakemove: sv_wallbug cmd faulted"); return; }
    s_wbApplied = want;
    DebugLog("fakemove: exec \"%s\"", cmd);
}

// eject runs on the poll thread: the command buffer is game-thread only, so write the cvar
// directly there (and only if we changed it)
void FakeMove_Restore()
{
    if (!s_wbApplied) return;
    s_wbApplied = false;
    if (ConVar* cv = FM_WallbugVar())
    {
        if (!cv->SetValueInt("sv_wallbug", s_wbSaved)) cv->ForceInt(s_wbSaved);
        DebugLog("fakemove: sv_wallbug -> %d (eject)", s_wbSaved);
    }
}

// per cmd, after the bind snapshot + netvars: lazy hook install + resolve binds.
// Lazy because server.dll only matters once a listen server is up; installing from the game
// thread is safe -- neither ProcessMovement can be mid-call on this thread inside CreateMove.
static void FakeMove_Tick()
{
    static bool s_clDone = false, s_svDone = false;
    if (!s_clDone && g_gameMovement)
    {
        s_clDone = true;
        InstallVTableHook(g_gameMovement, kGM_ProcessMovement, "fakemove: client ProcessMovement",
                          (void*)h_ProcessMovementCl, (void**)&o_ProcessMovementCl);
    }
    if (!s_svDone)
        if (void* gm = GetInterface("server.dll", "GameMovement001"))
        {
            s_svDone = true;
            InstallVTableHook(gm, kGM_ProcessMovement, "fakemove: server ProcessMovement",
                              (void*)h_ProcessMovementSv, (void**)&o_ProcessMovementSv);
        }

    int b = 0;
    const int idx = GetLocalPlayerIndex();
    void* pLocal = idx > 0 ? GetClientEntity(idx) : nullptr;
    if (g_fmEnabled && pLocal && EntAlive(pLocal) && EntMoveType(pLocal) == MOVETYPE_WALK)
    {
        if (On(g_fmWallSurf,   g_fmWallSurfKey,   g_fmWallSurfMode))   b |= FM_WALLSURF;
        if (On(g_fmWallJump,   g_fmWallJumpKey,   g_fmWallJumpMode))   b |= FM_WALLJUMP;
        if (On(g_fmHighJump,   g_fmHighJumpKey,   g_fmHighJumpMode))   b |= FM_HIGHJUMP;
        if (On(g_fmAirStuck,   g_fmAirStuckKey,   g_fmAirStuckMode))   b |= FM_AIRSTUCK;
        if (On(g_fmNoFall,     g_fmNoFallKey,     g_fmNoFallMode))     b |= FM_NOFALL;
        if (On(g_fmCrouchJump, g_fmCrouchJumpKey, g_fmCrouchJumpMode)) b |= FM_CROUCHJUMP;
        if (On(g_fmRampBoost,  g_fmRampBoostKey,  g_fmRampBoostMode))  b |= FM_RAMPBOOST;
        if (On(g_fmHeadSurf,   g_fmHeadSurfKey,   g_fmHeadSurfMode))   b |= FM_HEADSURF;
    }
    s_fmBits = b;
    // not gated on g_fmEnabled: it is a server cvar, not a fake bind. wc / as / tb pins need it,
    // and cfg fmEnabled=0 + fmWallbugFix=1 silently never applied it
    FM_WallbugCvar(g_fmWallbugFix && !g_eject
                   && GetTickCount64() - s_fmServerSeenMs < 1000);
}
