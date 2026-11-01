#include "world_fx3.h"
#include "world_fx3_math.h"
#include "visuals.h"                   // CachedCVar
#include "../movement/movement.h"      // Vec3, AngleVectors, Ray_mv, GameTrace_t, kTrace_TraceRay
#include "../movement/prediction.h"    // g_globalVars
#include "../../render/render.h"       // W2S_Project
#include <mutex>
#include <deque>
#include <unordered_map>

using wfx3::V3;

static inline V3 ToV3(const float* f) { return { f[0], f[1], f[2] }; }

// ==== engine pieces ==================================================================

// IViewRenderBeams, declared exactly as src/game/client/iviewrender_beams.h (and Amalgam's
// SDK/Definitions/Interfaces/ViewRenderBeams.h, same order): MSVC lays the overloads out the same
// way it did for client.dll, so CreateBeamPoints(BeamInfo_t&) is called by name, never by a counted slot.
struct BVec { float x, y, z; };
struct Beam_t;
struct BeamInfo_t
{
    int         m_nType = 0;
    void*       m_pStartEnt = nullptr;
    int         m_nStartAttachment = 0;
    void*       m_pEndEnt = nullptr;
    int         m_nEndAttachment = 0;
    BVec        m_vecStart{}, m_vecEnd{};
    int         m_nModelIndex = -1;
    const char* m_pszModelName = nullptr;
    int         m_nHaloIndex = -1;
    const char* m_pszHaloName = nullptr;
    float       m_flHaloScale = 0.f;
    float       m_flLife = 0.f, m_flWidth = 0.f, m_flEndWidth = 0.f, m_flFadeLength = 0.f, m_flAmplitude = 0.f;
    float       m_flBrightness = 0.f, m_flSpeed = 0.f;
    int         m_nStartFrame = 0;
    float       m_flFrameRate = 0.f;
    float       m_flRed = 0.f, m_flGreen = 0.f, m_flBlue = 0.f;
    bool        m_bRenderable = true;
    int         m_nSegments = -1;
    int         m_nFlags = 0;
    BVec        m_vecCenter{};
    float       m_flStartRadius = 0.f, m_flEndRadius = 0.f;
};
class IViewRenderBeams
{
public:
    virtual void    InitBeams() = 0;
    virtual void    ShutdownBeams() = 0;
    virtual void    ClearBeams() = 0;
    virtual void    UpdateTempEntBeams() = 0;
    virtual void    DrawBeam(void* pbeam, void* pEntityBeamTraceFilter) = 0;
    virtual void    DrawBeam(Beam_t* pbeam) = 0;
    virtual void    KillDeadBeams(void* pEnt) = 0;
    virtual Beam_t* CreateBeamEnts(BeamInfo_t& beamInfo) = 0;
    virtual Beam_t* CreateBeamEntPoint(BeamInfo_t& beamInfo) = 0;
    virtual Beam_t* CreateBeamPoints(BeamInfo_t& beamInfo) = 0;
    virtual Beam_t* CreateBeamRing(BeamInfo_t& beamInfo) = 0;
    virtual Beam_t* CreateBeamRingPoint(BeamInfo_t& beamInfo) = 0;
    virtual Beam_t* CreateBeamCirclePoints(BeamInfo_t& beamInfo) = 0;
    virtual Beam_t* CreateBeamFollow(BeamInfo_t& beamInfo) = 0;
    virtual void    FreeBeam(Beam_t* pBeam) = 0;
    virtual void    UpdateBeamInfo(Beam_t* pBeam, BeamInfo_t& beamInfo) = 0;
    virtual void    CreateBeamEnts(int, int, int, int, float, float, float, float, float, float, float, float, int, float, float, float, float, int) = 0;
    virtual void    CreateBeamEntPoint(int, const BVec*, int, const BVec*, int, int, float, float, float, float, float, float, float, float, int, float, float, float, float) = 0;
    virtual void    CreateBeamPoints(BVec&, BVec&, int, int, float, float, float, float, float, float, float, float, int, float, float, float, float) = 0;
    virtual void    CreateBeamRing(int, int, int, int, float, float, float, float, float, float, float, float, int, float, float, float, float, int) = 0;
    virtual void    CreateBeamRingPoint(const BVec&, float, float, int, int, float, float, float, float, float, float, float, float, int, float, float, float, float, int) = 0;
    virtual void    CreateBeamCirclePoints(int, BVec&, BVec&, int, int, float, float, float, float, float, float, float, float, int, float, float, float, float) = 0;
    virtual void    CreateBeamFollow(int, int, int, float, float, float, float, float, float, float, float, float) = 0;
};
enum { FBEAM_FADEIN = 0x4, FBEAM_ONLYNOISEONCE = 0x100, FBEAM_NOTILE = 0x200 };

// botox k_tracer_sprites. purplelaser1 is precached for every mod (server/client.cpp ClientPrecache)
// and laserbeam by W_Precache (basecombatweapon.cpp:114); a sprite the server did not precache has
// model index -1 and CreateGenericBeam drops it, so a null beam falls back to purplelaser1.
static const char* const k_sprites[] = { "sprites/purplelaser1.vmt", "sprites/physbeam.vmt",
                                         "sprites/laserbeam.vmt", "sprites/white.vmt" };
static constexpr int k_spriteCount = 4;

// Amalgam MAKE_INTERFACE_SIGNATURE(IViewRenderBeams, ..., "48 8B 0D ? ? ? ? 48 8B D3 48 8B 01 FF 50 ? 0F B7 93", 0, deref 1):
// mov rcx, [rip+disp32] -> the g_pViewRenderBeams global. 1 hit in the on-disk client.dll (checked 11-01).
static IViewRenderBeams** s_ppBeams = nullptr;

static IViewRenderBeams* Beams()
{
    if (!s_ppBeams) return nullptr;
    IViewRenderBeams* b = nullptr;
    __try { b = *s_ppBeams; if (b && !*reinterpret_cast<void**>(b)) b = nullptr; }
    __except (EXCEPTION_EXECUTE_HANDLER) { b = nullptr; }
    return b;
}

__declspec(noinline) static Beam_t* BeamPointsSEH(IViewRenderBeams* b, BeamInfo_t& info)
{
    __try { return b->CreateBeamPoints(info); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static void MakeBeam(const V3& a, const V3& b, int sprite, const float col[4], float life, float width, int flags)
{
    IViewRenderBeams* beams = Beams();
    if (!beams) return;
    BeamInfo_t info;
    info.m_vecStart     = { a.x, a.y, a.z };
    info.m_vecEnd       = { b.x, b.y, b.z };
    info.m_pszModelName = k_sprites[sprite < 0 ? 0 : (sprite >= k_spriteCount ? k_spriteCount - 1 : sprite)];
    info.m_flLife       = life;
    info.m_flWidth      = width;
    info.m_flEndWidth   = width;
    info.m_flFadeLength = (flags & FBEAM_FADEIN) ? 0.1f : 0.f;
    info.m_flBrightness = col[3] * 255.f;
    info.m_flSpeed      = (flags & FBEAM_FADEIN) ? 1.f : 0.f;
    info.m_flRed        = col[0] * 255.f;
    info.m_flGreen      = col[1] * 255.f;
    info.m_flBlue       = col[2] * 255.f;
    info.m_nSegments    = 2;
    info.m_nFlags       = flags;
    if (BeamPointsSEH(beams, info)) return;

    static bool s_logged = false;
    if (info.m_nModelIndex < 0 && info.m_pszModelName != k_sprites[0]) {
        if (!s_logged) { s_logged = true; DebugLog("worldfx3: sprite %s not precached here, using purplelaser1", info.m_pszModelName); }
        info.m_pszModelName = k_sprites[0];
        info.m_nModelIndex  = -1;
        BeamPointsSEH(beams, info);
    }
}

static void* s_engineTrace = nullptr;   // EngineTraceClient003, TraceRay = slot 4 (movement.h)
static void* s_effects     = nullptr;   // VEngineEffects001, CL_AllocDlight = slot 4 (public/iefx.h)
static void* s_matSys      = nullptr;   // VMaterialSystem082

static float Now()      { return g_globalVars ? g_globalVars->realtime : 0.f; }
static float CurTime()  { return g_globalVars ? g_globalVars->curtime : 0.f; }
static int   MaxClients() { const int m = g_globalVars ? g_globalVars->maxClients : 0; return m > 0 && m <= 101 ? m : 101; }

static int ReadInt(void* ent, int off)
{
    if (!ent || off <= 0) return 0;
    int v = 0;
    __try { v = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(ent) + off); }
    __except (EXCEPTION_EXECUTE_HANDLER) { v = 0; }
    return v;
}
static bool ReadVec(void* ent, int off, V3& out)
{
    if (!ent || off <= 0) return false;
    __try { out = ToV3(reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(ent) + off)); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool Alive(void* ent)
{
    if (!ent || g_offLifeState <= 0) return false;
    unsigned char ls = 1;
    __try { ls = *reinterpret_cast<unsigned char*>(reinterpret_cast<uintptr_t>(ent) + g_offLifeState); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return ls == 0;
}
// IClientNetworkable::IsDormant = slot 8 (public/iclientnetworkable.h order).
static bool Dormant(int idx)
{
    void* net = g_entityList ? VCall<void*>(g_entityList, 0, idx) : nullptr;
    if (!net) return true;
    bool d = true;
    __try { d = VCall<bool>(net, 8); } __except (EXCEPTION_EXECUTE_HANDLER) { d = true; }
    return d;
}

// ==== bullets: tracers, client impacts, world hit marker ===============================

// FireBulletsInfo_t head (Amalgam SDK/Definitions/Definitions.h:1204, shareddefs.h): only these are read.
struct FireBulletsHead { int m_iShots; float m_vecSrc[3]; float m_vecDirShooting[3]; float m_vecSpread[3]; float m_flDistance; };

// ITraceFilter: slot 0 ShouldHitEntity, slot 1 GetTraceType (same hand-built vtable as dof.cpp).
// TF2 bullets pass through teammates (tf_player_shared.cpp FireBullet), so they are skipped too.
struct BulletFilter
{
    void** vfptr;
    void*  skip;
    void*  mates[101];
    int    nMates;

    static bool __fastcall sShouldHit(BulletFilter* self, void* pEnt, int)
    {
        if (!pEnt || pEnt == self->skip) return false;
        for (int i = 0; i < self->nMates; ++i) if (self->mates[i] == pEnt) return false;
        return true;
    }
    static int __fastcall sTraceType(BulletFilter*) { return 0; }   // TRACE_EVERYTHING
    void Setup(void* s)
    {
        static void* vt[2] = { (void*)&sShouldHit, (void*)&sTraceType };
        vfptr = vt; skip = s; nMates = 0;
    }
};

// MASK_SHOT | CONTENTS_GRATE (Amalgam's FireBullet hook trace)
static constexpr unsigned k_maskShot = 0x1 | 0x2 | 0x8 | 0x4000 | 0x2000000 | 0x4000000 | 0x40000000;

static bool TraceShot(const V3& a, const V3& b, BulletFilter* f, V3& end)
{
    end = b;
    if (!s_engineTrace) s_engineTrace = GetInterface("engine.dll", "EngineTraceClient003");
    if (!s_engineTrace) return false;
    Ray_mv ray;
    ray.Init(Vec3(a.x, a.y, a.z), Vec3(b.x, b.y, b.z));
    GameTrace_t tr;
    using TraceRay_t = void(__fastcall*)(void*, const Ray_mv&, unsigned, void*, GameTrace_t*);
    __try {
        void** vt = *(void***)s_engineTrace;
        ((TraceRay_t)vt[kTrace_TraceRay])(s_engineTrace, ray, k_maskShot, f, &tr);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    end = { tr.endpos.x, tr.endpos.y, tr.endpos.z };
    return tr.fraction < 1.f;
}

// main thread only
static std::deque<wfx3::Impact> s_localImpacts;   // botox m_local_impacts (hit marker source), realtime

// main thread writes, render thread reads (RenderWorldFx3)
struct Drawn { V3 pos; ULONGLONG ms; };
static std::mutex        s_drawMx;
static std::deque<Drawn> s_boxes;     // client impacts
static std::deque<Drawn> s_markers;   // world hit markers

static void PushDrawn(std::deque<Drawn>& q, const V3& p, size_t cap)
{
    std::lock_guard<std::mutex> lk(s_drawMx);
    q.push_back({ p, GetTickCount64() });
    while (q.size() > cap) q.pop_front();
}

using FireBullet_t = void(__fastcall*)(void*, void*, const void*, bool, int, int);
static FireBullet_t o_FireBullet = nullptr;

__declspec(noinline) static void OnBullet(void* shooter, const void* pInfo)
{
    const bool wantTracer = g_wdTracers, wantImpact = g_wdImpacts, wantMarker = g_wdHitMarker;
    if (!wantTracer && !wantImpact && !wantMarker) return;

    FireBulletsHead info;
    __try { info = *reinterpret_cast<const FireBulletsHead*>(pInfo); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }

    const int localIdx = GetLocalPlayerIndex();
    void* local = localIdx > 0 ? GetClientEntity(localIdx) : nullptr;
    if (!local) return;
    const bool isLocal = shooter == local;

    const V3 src = ToV3(info.m_vecSrc), dir = ToV3(info.m_vecDirShooting);
    const float dist = info.m_flDistance > 0.f && info.m_flDistance < 16384.f ? info.m_flDistance : 8192.f;
    if (!wfx3::SanePos(src) || !std::isfinite(dir.x) || !std::isfinite(dir.y) || !std::isfinite(dir.z)) return;

    // prediction can run one shot more than once: the same ray from the same player inside 0.1s is one shot
    static void* s_lastShooter = nullptr;
    static V3    s_lastSrc{}, s_lastDir{};
    static float s_lastTime = -1.f;
    const float now = Now();
    if (shooter == s_lastShooter && now - s_lastTime < 0.1f && now >= s_lastTime &&
        wfx3::Dist(src, s_lastSrc) < 0.01f && wfx3::Dist(dir, s_lastDir) < 0.0001f) return;
    s_lastShooter = shooter; s_lastSrc = src; s_lastDir = dir; s_lastTime = now;

    const int shooterTeam = ReadInt(shooter, g_offTeamNum);
    const int localTeam   = ReadInt(local, g_offTeamNum);
    const bool isEnemy    = !isLocal && shooterTeam != localTeam;

    const bool tracer = wantTracer && (isLocal ? g_wdTracersLocal : isEnemy ? g_wdTracersEnemy : g_wdTracersTeam);
    if (!tracer && !isLocal) return;

    BulletFilter f;
    f.Setup(shooter);
    if (shooterTeam >= 2)
        for (int i = 1, n = MaxClients(); i <= n && f.nMates < 101; ++i) {
            void* e = GetClientEntity(i);
            if (e && e != shooter && ReadInt(e, g_offTeamNum) == shooterTeam) f.mates[f.nMates++] = e;
        }
    V3 end;
    const V3 rayEnd = { src.x + dir.x * dist, src.y + dir.y * dist, src.z + dir.z * dist };
    const bool hit = TraceShot(src, rayEnd, &f, end);

    if (tracer) {
        const float* col = isLocal ? g_wdTracersLocalColor : isEnemy ? g_wdTracersEnemyColor : g_wdTracersTeamColor;
        const float life  = g_wdTracersLife < 0.1f ? 0.1f : (g_wdTracersLife > 10.f ? 10.f : g_wdTracersLife);
        const float width = g_wdTracersWidth < 0.5f ? 0.5f : (g_wdTracersWidth > 10.f ? 10.f : g_wdTracersWidth);
        MakeBeam(src, end, g_wdTracersSprite, col, life, width, FBEAM_FADEIN | FBEAM_ONLYNOISEONCE | FBEAM_NOTILE);
    }

    if (isLocal && hit && wfx3::SanePos(end)) {
        if (wantImpact) PushDrawn(s_boxes, end, 256);
        s_localImpacts.push_back({ end, now });
        while (s_localImpacts.size() > 64) s_localImpacts.pop_front();
    }
}

static void __fastcall h_FireBullet(void* rcx, void* wpn, const void* info, bool fx, int dmg, int custom)
{
    MiscHookGuard _g;
    o_FireBullet(rcx, wpn, info, fx, dmg, custom);
    if (!g_eject && rcx && info) OnBullet(rcx, info);
}

void WorldFx3_OnPlayerHurt(int victimIdx)
{
    if (!g_wdHitMarker || victimIdx <= 0) return;
    void* victim = GetClientEntity(victimIdx);
    V3 org;
    if (!victim || !ReadVec(victim, g_offOrigin, org)) return;
    const V3 chest = { org.x, org.y, org.z + 40.f };

    wfx3::Impact list[64];
    int n = 0;
    for (const auto& i : s_localImpacts) if (n < 64) list[n++] = i;
    const int best = wfx3::NearestImpact(list, n, chest, Now());
    // TF2: projectiles / melee / flames never go through FireBullet (botox is hitscan-only CS:GO),
    // and a lagged hitscan hit can miss the 128u window -> no impact = mark the victim's chest
    PushDrawn(s_markers, best >= 0 ? list[best].pos : chest, 32);
}

// ==== movement trail (botox bullets.cpp movement_trail) ================================
static V3   s_trailLast{};
static bool s_trailValid = false;

static void Trail_Frame(void* local)
{
    if (!g_wdTrail || !Beams() || !Alive(local)) { s_trailValid = false; return; }

    const int flags    = ReadInt(local, g_offFlags);
    const int moveType = g_offWaterLevel > 4 ? (ReadInt(local, g_offWaterLevel - 4) & 0xFF) : 0;   // m_MoveType = m_nWaterLevel - 4
    const bool skip    = moveType == 8 /*NOCLIP*/ || moveType == 10 /*OBSERVER*/ || (g_wdTrailAirOnly && (flags & 1));

    V3 origin;
    if (!ReadVec(local, g_offOrigin, origin)) return;
    if (skip || !s_trailValid) { s_trailLast = origin; s_trailValid = !skip; return; }

    const int step = wfx3::TrailStep(s_trailLast, origin);
    if (step == 0) return;
    if (step == 2) { s_trailLast = origin; return; }

    float col[4];
    memcpy(col, (flags & 2) ? g_wdTrailCrouchColor : g_wdTrailColor, sizeof(col));
    if (g_wdTrailRainbow) wfx3::Rainbow(Now(), col);
    const float life  = g_wdTrailLife < 0.1f ? 0.1f : (g_wdTrailLife > 10.f ? 10.f : g_wdTrailLife);
    const float width = g_wdTrailWidth < 0.5f ? 0.5f : (g_wdTrailWidth > 20.f ? 20.f : g_wdTrailWidth);
    MakeBeam(s_trailLast, origin, g_wdTrailSprite, col, life, width, FBEAM_NOTILE);
    s_trailLast = origin;
}

// ==== dlight (botox edicts.cpp dlights 1:1) ============================================
// dlight_t (public/dlight.h), x64 = x86 layout: no pointers.
struct Wfx3Dlight
{
    int   flags;
    float origin[3];
    float radius;
    unsigned char r, g, b; signed char exponent;
    float die, decay, minlight;
    int   key, style;
    float direction[3];
    float innerAngle, outerAngle;
};
static_assert(offsetof(Wfx3Dlight, radius) == 0x10 && offsetof(Wfx3Dlight, die) == 0x18 &&
              offsetof(Wfx3Dlight, key) == 0x24 && offsetof(Wfx3Dlight, outerAngle) == 0x3c, "dlight_t layout");

static unsigned char ColByte(float f) { return !(f > 0.f) ? 0 : f >= 1.f ? 255 : (unsigned char)(f * 255.f); }

static bool s_dlLit[102] = {};

__declspec(noinline) static void Dlight_Set(int key, const V3* pos, const float* col)
{
    if (!s_effects) return;
    __try {
        Wfx3Dlight* l = VCall<Wfx3Dlight*>(s_effects, 4, key);
        if (!l) return;
        if (!pos) { l->radius = 0.f; l->die = -1.f; return; }
        const float radius = g_dlightRadius < 25.f ? 25.f : (g_dlightRadius > 1000.f ? 1000.f : g_dlightRadius);
        const int bright = g_wdDlightBrightness < 0 ? 0 : (g_wdDlightBrightness > 10 ? 10 : g_wdDlightBrightness);
        l->flags = 0;
        l->origin[0] = pos->x; l->origin[1] = pos->y; l->origin[2] = pos->z + 36.f;
        l->radius = radius;
        l->r = ColByte(col[0]); l->g = ColByte(col[1]); l->b = ColByte(col[2]);
        l->exponent = (signed char)bright;
        l->die = CurTime() + 0.1f;   // cl time: CL_DecayLights compares die against it
        l->decay = 0.f; l->minlight = 0.f;
        l->key = key; l->style = 0;
        l->direction[0] = l->direction[1] = l->direction[2] = 0.f;
        l->innerAngle = l->outerAngle = 0.f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void Dlight_Frame(void* local, int localIdx)
{
    if (!s_effects) s_effects = GetInterface("engine.dll", "VEngineEffects001");
    if (!s_effects) return;
    const bool on = g_dlightEnable && local;
    const int localTeam = ReadInt(local, g_offTeamNum);
    for (int i = 1, n = MaxClients(); i <= n && i < 102; ++i) {
        bool lit = false;
        if (on) {
            void* p = GetClientEntity(i);
            const bool isLocal = i == localIdx;
            V3 org;
            if (p && (isLocal ? g_wdDlightLocal : ReadInt(p, g_offTeamNum) != localTeam) &&
                Alive(p) && !Dormant(i) && ReadVec(p, g_offOrigin, org)) {
                Dlight_Set(i, &org, isLocal ? g_wdDlightLocalColor : g_dlightColor);
                lit = true;
            }
        }
        if (!lit && s_dlLit[i]) Dlight_Set(i, nullptr, nullptr);   // kill, don't leave it up to die
        s_dlLit[i] = lit;
    }
}

// ==== world / prop / skybox modulation (botox misc.cpp world_modulation) ===================
// IMaterialSystem: FirstMaterial 73, NextMaterial 74, InvalidMaterial 75, GetMaterial 76 -- counted
// off Amalgam IMaterialSystem.h with MSVC overload grouping; the same count lands FindMaterial on 71,
// GetBackBufferDimensions 31, CreateNamedRTEx 85 and GetRenderContext 98, all four confirmed live
// (flipmath.h flipslot). IMaterial: GetName 0, GetTextureGroupName 1, ColorModulate 28,
// IsErrorMaterial 42, GetColorModulation 45 (same count, anchors IncRef 12 / DecRef 13 / 28 / 42).
static int ModGroup(const char* group, const char* name)
{
    if (group) {
        if (!strcmp(group, "SkyBox textures"))     return 3;
        if (!strcmp(group, "World textures"))      return 1;
        if (!strcmp(group, "StaticProp textures")) return 2;
    }
    if (!name) return 0;
    // botox: never players, guns or hands. TF2 cosmetics + weapons live under models/player and
    // models/workshop(_partner) / models/weapons.
    if (strstr(name, "models/player") || strstr(name, "models/weapons") || strstr(name, "models/workshop") ||
        strstr(name, "weapons/") || strstr(name, "arms"))
        return 0;
    return strstr(name, "models/") ? 2 : 0;
}

static unsigned NameHash(const char* s)
{
    unsigned h = 2166136261u;
    if (s) for (; *s; ++s) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

struct ModSaved { unsigned hash; int group; float orig[3]; };
static std::unordered_map<void*, ModSaved> s_modSaved;
static float s_modNextWalk = 0.f;
static int   s_modLastMask = 0;
static float s_modLast[4][3] = {};

// SEH leaves (no dtors in a __try frame, C2712): the walk itself and the two IMaterial calls.
__declspec(noinline) static bool MatWalkStep(bool first, unsigned short& h, void*& m, const char*& name, const char*& group)
{
    __try {
        const unsigned short invalid = VCall<unsigned short>(s_matSys, 75);
        h = first ? VCall<unsigned short>(s_matSys, 73) : VCall<unsigned short>(s_matSys, 74, h);
        if (h == invalid) return false;
        m = VCall<void*>(s_matSys, 76, h);
        name = group = nullptr;
        if (m && !VCall<bool>(m, 42)) { name = VCall<const char*>(m, 0); group = VCall<const char*>(m, 1); }
        else m = nullptr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
__declspec(noinline) static void MatModulate(void* m, const float* rgb)
{
    __try { VCall<void>(m, 28, rgb[0], rgb[1], rgb[2]); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
__declspec(noinline) static void MatGetModulation(void* m, float* rgb)
{
    __try { VCall<void>(m, 45, &rgb[0], &rgb[1], &rgb[2]); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void Mod_Walk(int mask, const float wanted[4][3], bool restoreOff)
{
    if (!s_matSys) s_matSys = GetInterface("materialsystem.dll", "VMaterialSystem082");
    if (!s_matSys) return;
    int touched = 0;
    unsigned short h = 0;
    void* m = nullptr;
    const char *name = nullptr, *grp = nullptr;
    for (int guard = 0; guard < 65536 && MatWalkStep(guard == 0, h, m, name, grp); ++guard) {
        if (!m) continue;
        const int group = ModGroup(grp, name);
        if (!group) continue;
        const unsigned hash = NameHash(name);

        auto it = s_modSaved.find(m);
        if (it != s_modSaved.end() && it->second.hash != hash) { s_modSaved.erase(it); it = s_modSaved.end(); }

        if (mask & (1 << group)) {
            if (it == s_modSaved.end()) {
                ModSaved sv{ hash, group, { 1.f, 1.f, 1.f } };
                MatGetModulation(m, sv.orig);
                it = s_modSaved.emplace(m, sv).first;
            }
            MatModulate(m, wanted[group]);
            ++touched;
        } else if (restoreOff && it != s_modSaved.end()) {
            MatModulate(m, it->second.orig);
            s_modSaved.erase(it);
        }
    }
    static int s_logged = -1;
    if (touched && s_logged != mask) { s_logged = mask; DebugLog("worldfx3: modulation mask %d on %d materials", mask, touched); }
}

static void Mod_Frame()
{
    const bool en[4] = { false, g_worldTintEnable, g_propTintEnable, g_wdSkyMod };
    const float* col[4] = { nullptr, g_worldTintColor, g_propTintColor, g_wdSkyModColor };
    const float  bri[4] = { 0.f, g_wdWorldModBright, g_wdPropModBright, g_wdSkyModBright };
    int mask = 0;
    float wanted[4][3] = {};
    for (int g = 1; g < 4; ++g) {
        if (!en[g]) continue;
        mask |= 1 << g;
        float b = bri[g] * 0.01f;
        b = b < 0.f ? 0.f : (b > 4.f ? 4.f : b);
        for (int c = 0; c < 3; ++c) wanted[g][c] = col[g][c] * b;
    }
    const float now = Now();
    const bool changed = mask != s_modLastMask || memcmp(wanted, s_modLast, sizeof(wanted)) != 0;
    if (!mask && !s_modLastMask) return;
    if (!changed && now < s_modNextWalk && now >= s_modNextWalk - 2.f) return;
    Mod_Walk(mask, wanted, mask != s_modLastMask || !mask);
    s_modLastMask = mask;
    memcpy(s_modLast, wanted, sizeof(wanted));
    s_modNextWalk = now + 1.f;   // late-loaded materials (props streamed in) picked up within a second
    if (!mask) s_modSaved.clear();
}

// ==== world / prop texture + remove decals (botox misc.cpp world_texture / world_decals) ====
// IMaterial (same count as above): FindVar 11, IsTranslucent 17, IsAlphaTested 18, SetMaterialVarFlag 29,
// GetMaterialVarFlag 30, GetShaderName 49. IMaterialVar (Amalgam IMaterialVar.h, MSVC order):
// GetTextureValue 0, SetTextureValue 14, IsDefined 17. ITexture: IncRef 10 / DecRef 11 (flipslot, live),
// IsError 15. IMaterialSystem::FindTexture 79. GetTextureValue never returns null -- a var that is not a
// texture (yet) hands back the error texture -- so IsError is what "not a texture" reads as.
// Cut against botox: no $basetexturetransform rescale (the new texture keeps the old UV density),
// no $envmaptint / $detailblendfactor strip, no hr $decaltexture handling (CS:GO-only shaders).
static constexpr int k_texSlotCount = 4;
static const char* const k_texSlots[k_texSlotCount] = { "$basetexture", "$basetexture2", "$basetexture3", "$basetexture4" };
static const char* const k_texBumps[2] = { "$bumpmap", "$bumpmap2" };

__declspec(noinline) static void* MatVar(void* m, const char* name)
{
    __try {
        bool found = false;
        void* v = VCall<void*>(m, 11, name, &found, false);
        return found ? v : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
__declspec(noinline) static void* VarTexture(void* var)   // null = not a (loaded) texture
{
    if (!var) return nullptr;
    __try {
        void* t = VCall<void*>(var, 0);
        return t && !VCall<bool>(t, 15) ? t : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
__declspec(noinline) static void VarSetTexture(void* var, void* tex)
{
    if (!var || !tex) return;
    __try { VCall<void>(var, 14, tex); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
__declspec(noinline) static void TexRef(void* tex, bool add)
{
    if (!tex) return;
    __try { VCall<void>(tex, add ? 10 : 11); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
struct MatInfo { bool translucent, alphaTest, decalFlag, noDraw; const char* shader; };
__declspec(noinline) static bool MatInfoGet(void* m, MatInfo& i)
{
    __try {
        i.translucent = VCall<bool>(m, 17);
        i.alphaTest   = VCall<bool>(m, 18);
        i.decalFlag   = VCall<bool>(m, 30, 1 << 16);   // MATERIAL_VAR_DECAL
        i.noDraw      = VCall<bool>(m, 30, 1 << 2);    // MATERIAL_VAR_NO_DRAW
        i.shader      = VCall<const char*>(m, 49);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
__declspec(noinline) static void MatSetNoDraw(void* m, bool on)
{
    __try { VCall<void>(m, 29, 1 << 2, on); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
__declspec(noinline) static void* FindTex(const char* name)
{
    if (!s_matSys || !name || !name[0]) return nullptr;
    __try {
        void* t = VCall<void*>(s_matSys, 79, name, "World textures", false, 0);
        return t && !VCall<bool>(t, 15) ? t : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static bool ShaderHas(const char* shader, const char* a, const char* b = nullptr)
{
    char s[64];
    wfx3::TexNormalize(shader, s, sizeof(s));
    return strstr(s, a) || (b && strstr(s, b));
}

// originals captured once and IncRef'd: a re-read after our write returns our own texture
struct TexSaved { unsigned hash; int slot; void* orig[k_texSlotCount]; void* origBump[2]; void* applied; };
static std::unordered_map<void*, TexSaved> s_texSaved;
static std::unordered_map<void*, unsigned> s_decalHidden;   // material -> name hash, only ones WE hid
static unsigned s_texLastSig = 0;
static float    s_texNextWalk = 0.f, s_decalNextWalk = 0.f;

static void Tex_RestoreOne(void* m, TexSaved& e)
{
    for (int i = 0; i < k_texSlotCount; ++i) if (e.orig[i]) VarSetTexture(MatVar(m, k_texSlots[i]), e.orig[i]);
    for (int i = 0; i < 2; ++i) if (e.origBump[i]) VarSetTexture(MatVar(m, k_texBumps[i]), e.origBump[i]);
    e.applied = nullptr;
}
static void Tex_Release(TexSaved& e)
{
    for (void* t : e.orig) TexRef(t, false);
    for (void* t : e.origBump) TexRef(t, false);
}

// both walk the live list, never follow a stored pointer: a destroyed material is simply not met
static void Tex_RestoreAll(bool forget)
{
    if (s_matSys && !s_texSaved.empty()) {
        unsigned short h = 0; void* m = nullptr; const char *name = nullptr, *grp = nullptr;
        for (int guard = 0; guard < 65536 && MatWalkStep(guard == 0, h, m, name, grp); ++guard) {
            if (!m) continue;
            auto it = s_texSaved.find(m);
            if (it != s_texSaved.end() && it->second.applied && it->second.hash == NameHash(name)) Tex_RestoreOne(m, it->second);
        }
    }
    if (forget) {
        for (auto& kv : s_texSaved) Tex_Release(kv.second);
        s_texSaved.clear();
    }
    s_texLastSig = 0;
}

static void Decal_UnhideAll()
{
    if (s_matSys && !s_decalHidden.empty()) {
        unsigned short h = 0; void* m = nullptr; const char *name = nullptr, *grp = nullptr;
        for (int guard = 0; guard < 65536 && MatWalkStep(guard == 0, h, m, name, grp); ++guard) {
            if (!m) continue;
            auto d = s_decalHidden.find(m);
            if (d != s_decalHidden.end() && d->second == NameHash(name)) MatSetNoDraw(m, false);
        }
        DebugLog("worldfx3: decals shown %d", (int)s_decalHidden.size());
    }
    s_decalHidden.clear();
}

__declspec(noinline) static const char* TexName(void* t)
{
    if (!t) return nullptr;
    __try { return VCall<const char*>(t, 0); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
// $ssbump set: the bump decodes three basis weights, a flat tangent normal there tints the lightmap.
// IMaterialVar::GetIntValueInternal = slot 26 (same count as above).
__declspec(noinline) static bool IsSsbump(void* m)
{
    void* v = MatVar(m, "$ssbump");
    if (!v) return false;
    __try { return VCall<int>(v, 26) != 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
}

static const char* TexWanted(bool on, int preset, const char* custom)
{
    if (!on) return nullptr;
    if (preset == wfx3::k_texPresetCustom) return custom && custom[0] ? custom : nullptr;
    return preset > 0 && preset < wfx3::k_texPresetCount ? wfx3::k_texPresets[preset].texture : nullptr;
}

static void Tex_Frame()
{
    if (!s_matSys) s_matSys = GetInterface("materialsystem.dll", "VMaterialSystem082");
    if (!s_matSys) return;
    const float now = Now();

    // slot 0..6 = world categories, 7 = props
    const char* names[8] = {};
    unsigned sig = 0x9e3779b9u;
    bool any = false;
    for (int c = 0; c < 7; ++c) {
        names[c] = TexWanted(g_wdWorldTex, g_wdWorldTexPreset[c], g_wdWorldTexCustom[c]);
        sig = sig * 31u + NameHash(names[c]) + (unsigned)c;
        any |= names[c] != nullptr;
    }
    names[7] = TexWanted(g_wdPropTex, g_wdPropTexPreset, g_wdPropTexCustom);
    sig = sig * 31u + NameHash(names[7]) + 0x50u;
    any |= names[7] != nullptr;
    const bool decals = g_wdWorldTex && g_wdWorldTexDecals;

    if (!decals && !s_decalHidden.empty()) Decal_UnhideAll();
    static bool s_anyLast = false;
    if (!any && s_anyLast) Tex_RestoreAll(false);   // the last category just went off
    s_anyLast = any;
    if (!any && !decals) return;

    const bool changed = sig != s_texLastSig;
    const bool decalWalk = decals && (now >= s_decalNextWalk || now < s_decalNextWalk - 2.f);
    if (!changed && !decalWalk && now < s_texNextWalk && now >= s_texNextWalk - 2.f) return;
    s_texLastSig = sig;
    s_texNextWalk = now + 1.f;
    if (decals) s_decalNextWalk = now + 1.f;

    void* want[8] = {};
    for (int i = 0; i < 8; ++i) {
        want[i] = FindTex(names[i]);
        if (changed && names[i] && !want[i]) DebugLog("worldfx3: world texture '%s' not found", names[i]);
    }
    void* flat = FindTex("dev/flat_normal");

    int applied = 0, restored = 0, hidden = 0;
    unsigned short h = 0; void* m = nullptr; const char *name = nullptr, *grp = nullptr;
    for (int guard = 0; guard < 65536 && MatWalkStep(guard == 0, h, m, name, grp); ++guard) {
        if (!m) continue;
        const unsigned hash = NameHash(name);
        const bool world = grp && !strcmp(grp, "World textures");
        MatInfo info{};
        if (!MatInfoGet(m, info)) continue;

        if (decals && !info.noDraw && s_decalHidden.find(m) == s_decalHidden.end() &&
            wfx3::TexIsDecal(grp, info.decalFlag, info.shader, info.translucent || info.alphaTest, name)) {
            MatSetNoDraw(m, true);
            s_decalHidden.emplace(m, hash);
            ++hidden;
            continue;
        }
        if (!any || (!world && ModGroup(grp, name) != 2)) continue;

        auto it = s_texSaved.find(m);
        if (it != s_texSaved.end() && it->second.hash != hash) { Tex_Release(it->second); s_texSaved.erase(it); it = s_texSaved.end(); }
        if (it == s_texSaved.end()) {
            TexSaved e{};
            e.hash = hash;
            e.slot = -1;
            // alpha-tested / translucent: an opaque base would turn fences and glass solid
            if (!info.translucent && !info.alphaTest) {
                if (world && ShaderHas(info.shader, "lightmapped", "worldvertextransition") && !ShaderHas(info.shader, "reflective")) {
                    e.slot = wfx3::TexClassify(name, TexName(VarTexture(MatVar(m, "$basetexture"))));
                } else if (!world && ShaderHas(info.shader, "vertexlit"))
                    e.slot = 7;
            }
            it = s_texSaved.emplace(m, e).first;
        }
        TexSaved& e = it->second;
        if (e.slot < 0) continue;
        void* tex = want[e.slot];
        if (tex == e.applied) continue;

        if (tex) {
            bool wrote = false;
            for (int i = 0; i < k_texSlotCount; ++i) {
                void* var = MatVar(m, k_texSlots[i]);
                if (!e.orig[i]) { e.orig[i] = VarTexture(var); TexRef(e.orig[i], true); }   // not loaded yet: retried next walk
                if (e.orig[i]) { VarSetTexture(var, tex); wrote = true; }
            }
            if (!wrote) continue;
            if (flat && !IsSsbump(m))
                for (int i = 0; i < 2; ++i) {
                    void* var = MatVar(m, k_texBumps[i]);
                    if (!e.origBump[i]) { e.origBump[i] = VarTexture(var); TexRef(e.origBump[i], true); }
                    if (e.origBump[i]) VarSetTexture(var, flat);
                }
            e.applied = tex;
            ++applied;
        } else {
            Tex_RestoreOne(m, e);
            ++restored;
        }
    }
    if (changed || applied || restored || hidden)
        DebugLog("worldfx3: world texture applied %d restored %d decals hidden +%d (total %d) flat normal %d",
                 applied, restored, hidden, (int)s_decalHidden.size(), flat ? 1 : 0);
}

// ==== ragdoll gravity (botox kill_effects.cpp ragdoll_force_proxy) ========================
// DT_TFRagdoll::m_vecForce IS the ragdoll's launch force: C_TFRagdoll has no member of its own, the
// recv prop lands in C_BaseAnimating::m_vecForce, which InitAsClientRagdoll hands CreateRagdoll
// (c_baseanimating.cpp:4916). m_hPlayer is declared before it (c_tf_player.cpp:520), so it is
// already decoded when this proxy runs. CRecvProxyData: m_pRecvProp(8) then DVariant m_Value,
// m_Vector[3] at +8 (dt_recv.h).
using RecvVarProxy_t = void(__cdecl*)(const void*, void*, void*);
static RecvProp*      s_forceProp  = nullptr;
static RecvVarProxy_t o_forceProxy = nullptr;
static int            s_offRagPlayer = 0;

static void __cdecl RagdollForceProxy(const void* data, void* structure, void* out)
{
    const int mode = g_wdRagdollGravity;
    if (mode > 0 && data && structure && s_offRagPlayer > 0 && !g_eject) {
        __try {
            const unsigned handle = *reinterpret_cast<unsigned*>(reinterpret_cast<uintptr_t>(structure) + s_offRagPlayer);
            void* player = g_entityList ? VCall<void*>(g_entityList, 4, handle) : nullptr;   // GetClientEntityFromHandle
            const int localIdx = GetLocalPlayerIndex();
            void* local = localIdx > 0 ? GetClientEntity(localIdx) : nullptr;
            if (player && local && player != local && ReadInt(player, g_offTeamNum) != ReadInt(local, g_offTeamNum))
                wfx3::RagdollForce(mode, g_wdRagdollGravityStrength,
                                   reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(const_cast<void*>(data)) + 8));
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (o_forceProxy) o_forceProxy(data, structure, out);
}

static void Ragdoll_Hook(bool on)
{
    if (on && !s_forceProp) {
        RecvTable* t = FindNetworkTable("DT_TFRagdoll");
        if (!t) return;
        s_offRagPlayer = FindPropOffset(t, "m_hPlayer");
        for (int i = 0; i < t->m_nProps; ++i) {
            RecvProp* p = &t->m_pProps[i];
            if (p->m_pVarName && !strcmp(p->m_pVarName, "m_vecForce")) {
                if (p->m_ProxyFn == (void*)&RagdollForceProxy) break;
                o_forceProxy = (RecvVarProxy_t)p->m_ProxyFn;
                p->m_ProxyFn = (void*)&RagdollForceProxy;
                s_forceProp  = p;
                DebugLog("worldfx3: DT_TFRagdoll m_vecForce proxy hooked (m_hPlayer +%d)", s_offRagPlayer);
                break;
            }
        }
    } else if (!on && s_forceProp) {
        s_forceProp->m_ProxyFn = (void*)o_forceProxy;
        s_forceProp = nullptr;
    }
}

// ==== remove hud elements ================================================================
bool WorldFx3_SkipPanel(void* panels, uintptr_t vpanel)
{
    if (!g_wdNoHud || g_eject || !panels) return false;
    using GetName_t = const char* (*)(void*, uintptr_t);   // IPanel::GetName, slot 36 (render.h)
    const char* nm = nullptr;
    __try { nm = ((GetName_t)(*(void***)panels)[36])(panels, vpanel); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    const int i = wfx3::HudElemFor(nm);
    return i >= 0 && i < k_wdHudCount && g_wdNoHudElems[i];
}

// ==== cvars ===============================================================================
struct HeldCvar { CachedCVar cc; bool held = false; float orig = 0.f; };

static void Hold(HeldCvar& h, const char* name, bool on, float value)
{
    ConVar* cv = h.cc.Get(name);
    if (!cv) return;
    if (on) {
        if (!h.held) { h.orig = cv->GetFloat(); h.held = true; }
        if (cv->GetFloat() != value) cv->ForceFloat(value);
    } else if (h.held) {
        cv->ForceFloat(h.orig);
        h.held = false;
    }
}

static HeldCvar s_cv3dSky, s_cvVmFov;

// r_3dsky (viewrender.cpp:113, no FCVAR_CHEAT; 1 hit in the TF2 x64 client.dll) and viewmodel_fov
// (view.cpp:111) both exist; CS:GO's mat_postprocess_enable does not (unit screen's world_fx.cpp).
static void Cvars_Tick(bool release)
{
    Hold(s_cv3dSky, "r_3dsky", !release && g_wdNo3dSky, 0.f);
    // viewmodel_fov is what ClientModeTFNormal::GetViewModelFOV returns; view.cpp:1084 scales it by
    // the width ratio afterwards, exactly what botox's draw_view_models does by hand.
    const float vf = g_wdVmFov < 1.f ? 1.f : (g_wdVmFov > 179.f ? 179.f : g_wdVmFov);
    Hold(s_cvVmFov, "viewmodel_fov", !release && g_wdVmFovEnable, vf);
}

// ==== entry points ========================================================================
void WorldFx3_Install()
{
    HMODULE client = GetModuleHandleA("client.dll");
    if (!client) return;
    if (!s_ppBeams) {
        if (uint8_t* p = PatternScan(client, "48 8B 0D ? ? ? ? 48 8B D3 48 8B 01 FF 50 ? 0F B7 93")) {
            s_ppBeams = reinterpret_cast<IViewRenderBeams**>(p + 7 + *reinterpret_cast<int32_t*>(p + 3));
            DebugLog("worldfx3: g_pViewRenderBeams at %p", (void*)s_ppBeams);
        } else DebugLog("worldfx3: ViewRenderBeams pattern NOT FOUND -- tracers/trail off");
    }
    InstallPatternHook(client, "worldfx3: CTFPlayer::FireBullet",
                       "48 89 74 24 ? 55 57 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? F3 41 0F 10 58",
                       (void*)h_FireBullet, (void**)&o_FireBullet);
}

void WorldFx3_Tick()
{
    if (g_eject) return;
    Cvars_Tick(false);
    if (g_wdRagdollGravity > 0) Ragdoll_Hook(true);   // stays in once hooked: passes through at "off"
}

void WorldFx3_Frame()
{
    if (g_eject) return;
    static bool s_inGame = false;
    const bool inGame = IsInGame();
    if (inGame != s_inGame) {   // level change: every material/beam/impact belongs to the old map
        s_inGame = inGame;
        Tex_RestoreAll(true);   // live walk only, then the refs we hold go
        Decal_UnhideAll();
        s_localImpacts.clear();
        s_modSaved.clear(); s_modLastMask = 0;
        memset(s_dlLit, 0, sizeof(s_dlLit));
        s_trailValid = false;
        std::lock_guard<std::mutex> lk(s_drawMx);
        s_boxes.clear(); s_markers.clear();
    }
    if (!inGame) return;
    const int localIdx = GetLocalPlayerIndex();
    void* local = localIdx > 0 ? GetClientEntity(localIdx) : nullptr;
    Trail_Frame(local);
    Dlight_Frame(local, localIdx);
    Mod_Frame();
    Tex_Frame();
}

void WorldFx3_Release()
{
    Cvars_Tick(true);
    Ragdoll_Hook(false);
    Tex_RestoreAll(true);
    Decal_UnhideAll();
    if (s_modLastMask) {
        const float none[4][3] = {};
        Mod_Walk(0, none, true);
        s_modLastMask = 0;
    }
}

// ==== render thread: impact boxes + world hit markers =======================================
static ImU32 Col(const float c[4], float aMul = 1.f)
{
    return IM_COL32((int)(c[0] * 255.f), (int)(c[1] * 255.f), (int)(c[2] * 255.f), (int)(c[3] * aMul * 255.f));
}

void RenderWorldFx3()
{
    if (!g_wdImpacts && !g_wdHitMarker) return;
    const ULONGLONG now = GetTickCount64();
    std::lock_guard<std::mutex> lk(s_drawMx);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    if (!g_wdImpacts) s_boxes.clear();
    const float s   = g_wdImpactsSize < 0.5f ? 0.5f : (g_wdImpactsSize > 10.f ? 10.f : g_wdImpactsSize);
    const float dur = g_wdImpactsDuration < 0.1f ? 0.1f : (g_wdImpactsDuration > 30.f ? 30.f : g_wdImpactsDuration);
    while (!s_boxes.empty() && (now - s_boxes.front().ms) / 1000.f >= dur) s_boxes.pop_front();
    const ImU32 boxCol = Col(g_wdImpactsClientColor);
    // corners: bit 0 = x, bit 1 = y, bit 2 = z; faces as corner quads
    static const int k_faces[6][4] = { {0,1,3,2}, {4,5,7,6}, {0,1,5,4}, {2,3,7,6}, {0,2,6,4}, {1,3,7,5} };
    for (const Drawn& b : s_boxes) {
        ImVec2 p[8];
        bool ok = true;
        for (int i = 0; i < 8 && ok; ++i)
            ok = W2S_Project(Vec3(b.pos.x + ((i & 1) ? s : -s), b.pos.y + ((i & 2) ? s : -s), b.pos.z + ((i & 4) ? s : -s)), p[i]);
        if (!ok) continue;
        for (const auto& f : k_faces) dl->AddQuadFilled(p[f[0]], p[f[1]], p[f[2]], p[f[3]], boxCol);
    }

    if (!g_wdHitMarker) s_markers.clear();
    const float mdur = g_wdHitMarkerDuration < 0.1f ? 0.1f : g_wdHitMarkerDuration;
    while (!s_markers.empty() && (now - s_markers.front().ms) / 1000.f >= mdur) s_markers.pop_front();
    constexpr float k_gap = 2.f;
    const float size = g_wdHitMarkerSize;
    for (const Drawn& m : s_markers) {
        const float a = wfx3::MarkerAlpha((now - m.ms) / 1000.f, mdur);
        ImVec2 sc;
        if (a <= 0.f || !W2S_Project(Vec3(m.pos.x, m.pos.y, m.pos.z), sc)) continue;
        const ImU32 c = Col(g_wdHitMarkerColor, a);
        for (float sx : { -1.f, 1.f })
            for (float sy : { -1.f, 1.f })
                dl->AddLine(ImVec2(sc.x + sx * k_gap, sc.y + sy * k_gap),
                            ImVec2(sc.x + sx * (k_gap + size), sc.y + sy * (k_gap + size)), c, 1.f);
    }
}
