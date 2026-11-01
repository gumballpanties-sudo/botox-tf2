#include "world_fx2.h"
#include "../movement/movement.h"   // Vec3, AngleVectors -- sun direction from pitch/yaw
#include "../movement/prediction.h" // g_globalVars -- sun drift clock
#include "world_fx3.h"
#include "world_fx3_math.h"

// ==== remove visual recoil: CBasePlayer::CalcView ===================================
// Pattern verified against Aletherium/Amalgam's Hooks/CBasePlayer_CalcView.cpp on this
// same TF2 x64 client.dll (see world_fx2.h). m_vecPunchAngle's offset is resolved the
// same way every other netvar in this DLL is (netvars.cpp's FindNetworkTable/
// FindPropOffset, walking the live RecvTable) -- not a hardcoded offset, so a game
// update that moves the field cannot silently corrupt memory, only fail to find it.
CalcView_t o_CalcView = nullptr;

// __try/__except cannot share a frame with MiscHookGuard's destructor (MSVC C2712), so the
// SEH body lives in its own noinline function -- same split visuals.cpp's own
// h_DrawModelExecute/h_DrawModelExecute_impl already uses for the same reason.
__declspec(noinline) static void h_CalcView_impl(void* rcx, void* eyeOrigin, void* eyeAngles,
                                                  float& zNear, float& zFar, float& fov)
{
    if (g_eject || !g_removeVisualRecoilEnable || !rcx) {
        o_CalcView(rcx, eyeOrigin, eyeAngles, zNear, zFar, fov);
        return;
    }

    static int s_offPunch = 0;
    if (s_offPunch == 0) {
        RecvTable* t = FindNetworkTable("DT_TFPlayer");
        s_offPunch = t ? FindPropOffset(t, "m_vecPunchAngle") : -1;
        if (s_offPunch <= 0) s_offPunch = -1;
    }
    if (s_offPunch <= 0) {
        o_CalcView(rcx, eyeOrigin, eyeAngles, zNear, zFar, fov);
        return;
    }

    float* punch = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(rcx) + s_offPunch);
    float saved[3] = {};
    bool captured = false;
    __try {
        saved[0] = punch[0]; saved[1] = punch[1]; saved[2] = punch[2];
        punch[0] = punch[1] = punch[2] = 0.f;
        captured = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { captured = false; }

    o_CalcView(rcx, eyeOrigin, eyeAngles, zNear, zFar, fov);

    if (captured) {
        __try { punch[0] = saved[0]; punch[1] = saved[1]; punch[2] = saved[2]; }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

void __fastcall h_CalcView(void* rcx, void* eyeOrigin, void* eyeAngles, float& zNear, float& zFar, float& fov)
{
    MiscHookGuard _g;
    h_CalcView_impl(rcx, eyeOrigin, eyeAngles, zNear, zFar, fov);
}

void WorldFx2_InstallHooks()
{
    if (o_CalcView) return;
    HMODULE hClient = GetModuleHandleA("client.dll");
    if (!hClient) return;
    InstallPatternHook(hClient, "removals: CBasePlayer::CalcView",
                        "40 57 48 83 EC ? 44 8B 91",
                        (void*)h_CalcView, (void**)&o_CalcView);
    WorldFx3_Install();   // FireBullet + ViewRenderBeams (tracers, impacts, trail)
}

// ==== dlight: moved to world_fx3.cpp (per frame, enemies + local, botox edicts.cpp dlights) ====

// ==== sun angle: env_sun's m_vDirection + shadow_control ===============================
// CSun's DT_Sun::m_vDirection points AT the sun (server sun.cpp:121 negates the light normal), so
// it is -forward(pitch, yaw): pitch 50 = sun 50 degrees up, light travelling down -- botox's
// cl_csm_rot_x reading. It only steers the corona sprite (c_sun.h). The real TF2 sun-shadow knob
// is shadow_control (c_shadowcontrol.cpp): m_shadowDirection = forward(pitch, yaw) (server
// shadowcontrol.cpp:110) and m_flShadowMaxDist, pushed into g_pClientShadowMgr by its own
// OnDataChanged -- IClientNetworkable slot 5, the same call Weather_UpdateChain makes.
// No roll: a direction vector has none (botox's roll is CS:GO's cl_csm_rot_z, a cascade-map spin).
// Entities found by class name (IClientEntityList slot 0 GetClientNetworkable, slot 6 highest
// index, IClientNetworkable slot 2 GetClientClass -- all confirmed elsewhere in this DLL).
// Main thread, once a frame (h_OverrideView): OnDataChanged reaches the shadow manager.
struct ClassEnt { void* ent; int idx; };

__declspec(noinline) static ClassEnt FindByClass(const char* cls, ClassEnt& cache)
{
    __try {
        if (cache.ent && cache.idx > 0 && GetClientEntity(cache.idx) == cache.ent) return cache;
        cache.ent = nullptr; cache.idx = 0;
        if (!g_entityList) return cache;
        const int highest = VCall<int>(g_entityList, 6);
        const int cap = (highest > 0 && highest < 2048) ? highest : 2048;
        for (int i = 1; i <= cap; ++i) {
            void* pNet = VCall<void*>(g_entityList, 0, i);
            if (!pNet) continue;
            ClientClass* cc = VCall<ClientClass*>(pNet, 2);
            if (!cc || !cc->m_pNetworkName || strcmp(cc->m_pNetworkName, cls) != 0) continue;
            cache.ent = GetClientEntity(i);
            cache.idx = i;
            return cache;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { cache.ent = nullptr; cache.idx = 0; }
    return cache;
}

static int PropOff(int& cache, const char* table, const char* prop)
{
    if (cache == 0) {
        RecvTable* t = FindNetworkTable(table);
        cache = t ? FindPropOffset(t, prop) : -1;
        if (cache <= 0) cache = -1;
    }
    return cache;
}

// shadow_control values we changed: the map's own, put back when sun angle goes off
struct ShadowHeld { void* ent; int idx; float dir[3]; float dist; float wrote[4]; };
static ShadowHeld s_shadow = {};

__declspec(noinline) static void Shadow_Write(void* ent, int idx, int offDir, int offDist, const float dir[3], float dist)
{
    __try {
        float* d = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(ent) + offDir);
        float* m = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(ent) + offDist);
        d[0] = dir[0]; d[1] = dir[1]; d[2] = dir[2];
        *m = dist;
        void* net = VCall<void*>(g_entityList, 0, idx);
        if (net) VCall<void>(net, 5, 1);   // OnDataChanged(DATA_UPDATE_DATATABLE_CHANGED)
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

__declspec(noinline) static bool Shadow_Read(void* ent, int offDir, int offDist, float dir[3], float& dist)
{
    __try {
        const float* d = reinterpret_cast<const float*>(reinterpret_cast<uintptr_t>(ent) + offDir);
        dir[0] = d[0]; dir[1] = d[1]; dir[2] = d[2];
        dist = *reinterpret_cast<const float*>(reinterpret_cast<uintptr_t>(ent) + offDist);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

__declspec(noinline) static void Sun_Write(void* ent, int off, const Vec3& toSun)
{
    __try {
        float* dir = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(ent) + off);
        dir[0] = toSun.x; dir[1] = toSun.y; dir[2] = toSun.z;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void Shadow_Release(int offDir, int offDist)
{
    if (s_shadow.ent && offDir > 0 && offDist > 0 && GetClientEntity(s_shadow.idx) == s_shadow.ent)
        Shadow_Write(s_shadow.ent, s_shadow.idx, offDir, offDist, s_shadow.dir, s_shadow.dist);
    s_shadow = ShadowHeld{};
}

static void SunAngle_Frame()
{
    static int s_offSun = 0, s_offShDir = 0, s_offShDist = 0;
    static ClassEnt s_sun = {}, s_ctl = {};
    static float s_drift = 0.f, s_last = 0.f;
    if (!g_entityList) return;
    const int offShDir  = PropOff(s_offShDir, "DT_ShadowControl", "m_shadowDirection");
    const int offShDist = PropOff(s_offShDist, "DT_ShadowControl", "m_flShadowMaxDist");

    if (!g_sunAngleEnable || g_eject || !IsInGame()) {
        if (s_shadow.ent) Shadow_Release(offShDir, offShDist);
        s_drift = 0.f;
        return;
    }

    const float now = g_globalVars ? g_globalVars->realtime : 0.f;
    s_drift = wfx3::SunDrift(s_drift, g_wdSunSpeed, now - s_last);
    s_last  = now;

    Vec3 fwd;
    AngleVectors(Vec3(g_sunAnglePitch, fmodf(g_sunAngleYaw + s_drift, 360.f), 0.f), &fwd);

    const int offSun = PropOff(s_offSun, "DT_Sun", "m_vDirection");
    if (offSun > 0) {
        const ClassEnt sun = FindByClass("CSun", s_sun);
        if (sun.ent) Sun_Write(sun.ent, offSun, Vec3(-fwd.x, -fwd.y, -fwd.z));
    }

    if (offShDir <= 0 || offShDist <= 0) return;
    const ClassEnt ctl = FindByClass("CShadowControl", s_ctl);
    if (!ctl.ent) return;
    if (s_shadow.ent != ctl.ent) {   // first touch of this map's entity: keep its own values
        s_shadow = ShadowHeld{};
        if (!Shadow_Read(ctl.ent, offShDir, offShDist, s_shadow.dir, s_shadow.dist)) return;
        s_shadow.ent = ctl.ent; s_shadow.idx = ctl.idx;
        s_shadow.wrote[3] = -1.f;   // force the first write
        DebugLog("sun angle: shadow_control #%d held (dir %.2f %.2f %.2f, dist %.0f)", ctl.idx,
                 s_shadow.dir[0], s_shadow.dir[1], s_shadow.dir[2], s_shadow.dist);
    }
    const float dir[3] = { fwd.x, fwd.y, fwd.z };
    const float dist = g_wdSunShadowDist > 0.f ? g_wdSunShadowDist : s_shadow.dist;
    if (s_shadow.wrote[0] != dir[0] || s_shadow.wrote[1] != dir[1] || s_shadow.wrote[2] != dir[2] || s_shadow.wrote[3] != dist) {
        Shadow_Write(ctl.ent, ctl.idx, offShDir, offShDist, dir, dist);
        s_shadow.wrote[0] = dir[0]; s_shadow.wrote[1] = dir[1]; s_shadow.wrote[2] = dir[2]; s_shadow.wrote[3] = dist;
    }
}

void WorldFx2_Tick()
{
    WorldFx3_Tick();   // cvar pokes + ragdoll proxy, same 100ms cadence
}

void WorldFx2_Frame()
{
    SunAngle_Frame();
}

// ==== world/prop texture (colour) replace + weather particle colour =================
// Appended onto the already-hooked h_FindMaterial (visuals.cpp, VMaterialSystem082 slot
// 71) -- no new hook. pTextureGroup is the engine's own classification
// (public/texture_group_names.h: TEXTURE_GROUP_WORLD = "World textures",
// TEXTURE_GROUP_MODEL = "Model textures"), not a name-pattern guess. ColorModulate is
// IMaterial vtable slot 28 (public/materialsystem/imaterial.h virtual order, identical in
// Aletherium/Amalgam's SDK/Definitions/Main/IMaterial.h -- cross-checked, not guessed).
void WorldFx2_OnFindMaterial(void* pMaterial, const char* pTextureGroup, const char* pName)
{
    (void)pTextureGroup;   // world/prop/skybox modulation is world_fx3.cpp's live material walk now
    if (!pMaterial || g_eject) return;

    __try {
        // Rain/snow sprite: h_FindMaterial already redirects the NAME for snow mode before
        // calling original, so pName here is "particle/rain" or "particle/snow" either way.
        if (g_weatherTintEnable && WorldFx2_IsWeatherSprite(pName))
            VCall<void>(pMaterial, 28, g_weatherTintColor[0], g_weatherTintColor[1], g_weatherTintColor[2]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
