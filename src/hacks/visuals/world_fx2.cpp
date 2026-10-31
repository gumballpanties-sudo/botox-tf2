#include "world_fx2.h"
#include "../movement/movement.h"   // Vec3, AngleVectors -- sun direction from pitch/yaw

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
}

// ==== dlight (player dynamic light) =================================================
// IVEfx (public/iefx.h): CL_AllocDlight is slot 4, counted off that file's virtual order.
// Resolved by the interface's own version string ("VEngineEffects001") -- Valve versions
// this interface by that exact string, so a successful GetInterface already guarantees
// the layout; nothing here is a guessed slot.
struct Wfx2Color32Exp { unsigned char r, g, b; signed char exponent; };
struct Wfx2Dlight
{
    int    flags;
    Vec3   origin;
    float  radius;
    Wfx2Color32Exp color;
    float  die;
    float  decay;
    float  minlight;
    int    key;
    int    style;
    Vec3   direction;
    float  innerAngle;
    float  outerAngle;
};

static void Dlight_Tick()
{
    static bool s_wasOn = false;

    if (!g_dlightEnable) {
        if (!s_wasOn) return;
        s_wasOn = false;
        // Force the slot's light dead rather than leaving it live with a stale origin --
        // CL_AllocDlight(key) always returns the SAME slot for a key we never changed.
        if (g_engine) {
            static void* s_effects = nullptr;
            if (!s_effects) s_effects = GetInterface("engine.dll", "VEngineEffects001");
            if (s_effects) {
                int idx = GetLocalPlayerIndex();
                if (idx > 0) {
                    Wfx2Dlight* light = VCall<Wfx2Dlight*>(s_effects, 4, idx);
                    if (light) { light->radius = 0.f; light->die = -1.f; }
                }
            }
        }
        return;
    }

    if (!g_engine || !IsInGame() || !IsLocalPlayerAlive() || g_offOrigin <= 0) return;
    const int idx = GetLocalPlayerIndex();
    if (idx <= 0) return;
    void* pLocal = GetClientEntity(idx);
    if (!pLocal) return;

    static void* s_effects = nullptr;
    if (!s_effects) s_effects = GetInterface("engine.dll", "VEngineEffects001");
    if (!s_effects) return;

    Vec3 origin;
    __try { origin = *reinterpret_cast<Vec3*>(reinterpret_cast<uintptr_t>(pLocal) + g_offOrigin); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }

    // IVEngineClient::Time -- slot 14, counted off GetLocalPlayer's own confirmed slot 12
    // (globals.h's GetLocalPlayerIndex) + LoadModel(13) + Time(14).
    const float now = g_engine ? VCall<float>(g_engine, 14) : 0.f;

    Wfx2Dlight* light = VCall<Wfx2Dlight*>(s_effects, 4, idx);
    if (!light) return;

    light->origin      = origin;
    light->radius      = g_dlightRadius;
    light->color.r     = WorldFx2_ColorByte(g_dlightColor[0]);
    light->color.g     = WorldFx2_ColorByte(g_dlightColor[1]);
    light->color.b     = WorldFx2_ColorByte(g_dlightColor[2]);
    light->color.exponent = 5;
    light->die          = now + 0.3f;   // refreshed every tick (100ms) while on
    light->decay        = 0.f;
    light->minlight     = 0.f;
    light->key          = idx;
    light->style        = 0;
    light->outerAngle   = 0.f;
    s_wasOn = true;
}

// ==== sun angle (env_sun's m_vDirection) =============================================
// CSun's DT_Sun::m_vDirection is a plain client-visible glow direction (c_sun.h), not a
// re-bake of the level's static lighting -- moving it only steers the corona sprite, same
// scope as botox's own CS:GO cl_csm_rot_* equivalent. Offset resolved by name like every
// other netvar here, entity found by class name the same way skins.cpp's
// FindPlayerResource finds CTFPlayerResource (IClientEntityList slot 0 =
// GetClientNetworkable, slot 6 = GetHighestEntityIndex, IClientNetworkable slot 2 =
// GetClientClass -- all three already confirmed elsewhere in this DLL).
static void* FindSunEntity()
{
    static void* s_cached  = nullptr;
    static int   s_lastIdx = 0;

    __try {
        if (s_cached && s_lastIdx > 0 && GetClientEntity(s_lastIdx) == s_cached) return s_cached;
        s_cached = nullptr; s_lastIdx = 0;
        if (!g_entityList) return nullptr;

        const int highest = VCall<int>(g_entityList, 6);
        const int cap = (highest > 0 && highest < 2048) ? highest : 2048;
        for (int i = 1; i <= cap; ++i) {
            void* pNet = VCall<void*>(g_entityList, 0, i);
            if (!pNet) continue;
            ClientClass* cc = VCall<ClientClass*>(pNet, 2);
            if (!cc || !cc->m_pNetworkName) continue;
            if (strcmp(cc->m_pNetworkName, "CSun") != 0) continue;
            s_cached  = GetClientEntity(i);
            s_lastIdx = i;
            return s_cached;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { s_cached = nullptr; s_lastIdx = 0; }
    return nullptr;
}

static void SunAngle_Tick()
{
    if (!g_sunAngleEnable) return;

    static int s_offDir = 0;
    if (s_offDir == 0) {
        RecvTable* t = FindNetworkTable("DT_Sun");
        s_offDir = t ? FindPropOffset(t, "m_vDirection") : -1;
        if (s_offDir <= 0) s_offDir = -1;
    }
    if (s_offDir <= 0) return;

    void* pSun = FindSunEntity();
    if (!pSun) return;

    Vec3 fwd;
    AngleVectors(Vec3(g_sunAnglePitch, g_sunAngleYaw, 0.f), &fwd);

    __try {
        float* dir = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pSun) + s_offDir);
        dir[0] = fwd.x; dir[1] = fwd.y; dir[2] = fwd.z;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void WorldFx2_Tick()
{
    Dlight_Tick();
    SunAngle_Tick();
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
    if (!pMaterial || g_eject) return;

    __try {
        const int cls = WorldFx2_ClassifyTextureGroup(pTextureGroup);
        if (g_worldTintEnable && cls == 1)
            VCall<void>(pMaterial, 28, g_worldTintColor[0], g_worldTintColor[1], g_worldTintColor[2]);
        else if (g_propTintEnable && cls == 2)
            VCall<void>(pMaterial, 28, g_propTintColor[0], g_propTintColor[1], g_propTintColor[2]);

        // Rain/snow sprite: h_FindMaterial already redirects the NAME for snow mode before
        // calling original, so pName here is "particle/rain" or "particle/snow" either way.
        if (g_weatherTintEnable && WorldFx2_IsWeatherSprite(pName))
            VCall<void>(pMaterial, 28, g_weatherTintColor[0], g_weatherTintColor[1], g_weatherTintColor[2]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
