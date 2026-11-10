#include "world_fx2.h"
#include "world_fx3.h"
#include "../../hooks/crashlog.h"   // CrashSpan: stutter probe on CalcView

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
    CrashSpan _s("view/calc");
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

void WorldFx2_Tick()
{
    WorldFx3_Tick();   // cvar pokes + ragdoll proxy, same 100ms cadence
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
