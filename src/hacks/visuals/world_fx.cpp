#include "world_fx.h"
#include "visuals.h"    // CachedCVar, CVar_ForceInt
#include "../movement/movement.h"   // Vec3, AngleVectors -- third person's eye-to-camera offset
#include "../screen/screenfx.h"   // ScreenFx_FindClientMode -- shared IClientMode lookup
#include "../movement/prediction.h"   // g_globalVars -- framecount + absoluteframetime, main thread only
#include "viewmodel_fps.h"            // g_vmFps.Frame -- the camera the low fps viewmodel re-attaches to

// ==== fullbright + disable interp ===================================================
// Both are plain convar pokes -- gap.md flags them "trivial... no hook needed". Ridden on
// visuals.cpp's existing 100ms tick (UpdateVisualsCvars), not a new one: one more
// CachedCVar lookup a tick is free next to what that function already does for fog/sway.
void WorldFx_Tick()
{
    if (!g_cvar) return;

    static CachedCVar cc_fullbright;
    static bool s_fbWas = false;
    if (g_fullbrightEnable) {
        CVar_ForceInt(cc_fullbright.Get("mat_fullbright"), 1);
        s_fbWas = true;
    } else if (s_fbWas) {
        CVar_ForceInt(cc_fullbright.Get("mat_fullbright"), 0);
        s_fbWas = false;
    }

    static CachedCVar cc_interp;
    static float s_interpOrig = -1.f;
    ConVar* interp = cc_interp.Get("cl_interp");
    if (g_disableInterpEnable) {
        if (interp && s_interpOrig < 0.f) s_interpOrig = interp->GetFloat();
        CVar_ForceFloat(interp, 0.f);
    } else if (s_interpOrig >= 0.f) {
        CVar_ForceFloat(interp, s_interpOrig);
        s_interpOrig = -1.f;
    }

    // ==== aspect ratio: botox screen.cpp aspect_ratio() 1:1 -- r_aspectratio ====
    // engine.dll registers r_aspectratio (string checked in TF2 x64 engine.dll); its
    // GetScreenAspectRatio feeds CViewSetup::m_flAspectRatio for the world AND viewmodel
    // (sdk view.cpp:1116, viewrender.cpp:1075) -> whole 3D view stretches. 0 = engine default.
    // Off / eject -> 0 once (botox on_release restores the same way). This tick runs on
    // Present, which keeps rendering past g_eject until the hooks come off.
    static CachedCVar cc_aspect;
    static bool s_arWas = false, s_arMissLogged = false;
    const bool arOn = g_aspectRatioEnable && !g_eject;
    if (arOn || s_arWas)
    {
        ConVar* ar = cc_aspect.Get("r_aspectratio");
        if (!ar)
        {
            if (!s_arMissLogged) { s_arMissLogged = true; DebugLog("aspect ratio: r_aspectratio not found"); }
        }
        else
        {
            float want = arOn ? g_aspectRatioValue : 0.f;
            if (arOn) want = want < 0.5f ? 0.5f : (want > 4.f ? 4.f : want);   // botox slider range
            if (ar->GetFloat() != want)
            {
                CVar_ForceFloat(ar, want);
                static ULONGLONG s_lastLog = 0;   // something reverting it every tick must not spam
                if (GetTickCount64() - s_lastLog > 2000)
                {
                    s_lastLog = GetTickCount64();
                    DebugLog("aspect ratio: r_aspectratio -> %.3f (reads back %.3f)", want, ar->GetFloat());
                }
            }
            s_arWas = arOn;
        }
    }
}

// ==== world fov + third person -- IClientMode::OverrideView, vtable slot 16 ==========
// NEW hook, verified before writing it: Aletherium/Amalgam's own
// Hooks/CClientModeShared_OverrideView.cpp hooks U::Memory.GetVirtual(I::ClientModeShared, 16)
// with the identical (void* rcx, CViewSetup* pView) signature and uses it for exactly these
// two features (F::Visuals.FOV / F::Visuals.ThirdPerson) on this same TF2 x64 engine branch --
// this is not a guessed slot. The CViewSetup field offsets (fov=56, origin=64, angles=76) are
// not re-derived here either: they are screenfx.cpp's own k_vsFov/k_vsOrigin/k_vsAngles,
// already confirmed live by that file's sanity check + one-time log on DoPostScreenSpaceEffects
// (slot 39, same interface, same struct). Kept as separate literals here rather than pulled
// from screenfx.cpp's file-local constexprs, which are not exported.
inline constexpr size_t k_vsFov    = 56;
inline constexpr size_t k_vsOrigin = 64;   // Vector, 3 floats
inline constexpr size_t k_vsAngles = 76;   // QAngle, 3 floats

using OverrideView_t = void(__fastcall*)(void*, void*);
static OverrideView_t o_OverrideView = nullptr;

static void __fastcall h_OverrideView(void* rcx, void* pView)
{
    o_OverrideView(rcx, pView);
    if (!pView || (!g_worldFovEnable && !g_thirdPersonEnable && !g_vmLowFps)) return;

    const uintptr_t p = reinterpret_cast<uintptr_t>(pView);
    __try
    {
        if (g_worldFovEnable && g_worldFovAmount > 1.f && g_worldFovAmount < 179.f)
            *reinterpret_cast<float*>(p + k_vsFov) = g_worldFovAmount;

        if (g_thirdPersonEnable)
        {
            float* o = reinterpret_cast<float*>(p + k_vsOrigin);
            const float* a = reinterpret_cast<float*>(p + k_vsAngles);
            const Vec3 angles(a[0], a[1], a[2]);
            if (angles.x > -91.f && angles.x < 91.f)   // sane pitch, or the pattern/offsets are wrong
            {
                Vec3 fwd;
                AngleVectors(angles, &fwd);
                const float dist = g_thirdPersonDistance < 0.f ? 0.f :
                                   (g_thirdPersonDistance > 400.f ? 400.f : g_thirdPersonDistance);
                // No wall-collision trace (botox's "+collision" sub-toggle) -- ponytail: the
                // camera can clip into geometry at distances a trace would have pulled in.
                // Add a TraceRay-gated clamp here if that turns out to matter in play.
                o[0] -= fwd.x * dist;
                o[1] -= fwd.y * dist;
                o[2] -= fwd.z * dist;
            }
        }

        // Low fps viewmodel: this CViewSetup, as edited above, is the camera DrawViewModels draws
        // through -- botox reads the same numbers in its DrawViewModels hook. Main thread, once a
        // frame, before any model draw.
        if (g_vmLowFps && g_globalVars)
            g_vmFps.Frame(g_globalVars->framecount, g_globalVars->absoluteframetime, g_vmLowFpsRate,
                          reinterpret_cast<const float*>(p + k_vsOrigin),
                          reinterpret_cast<const float*>(p + k_vsAngles));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void WorldFx_InstallViewHook()
{
    if (o_OverrideView) return;
    void* mode = ScreenFx_FindClientMode();
    if (!mode) return;
    InstallVTableHook(mode, 16, "worldfx: OverrideView",
                      (void*)h_OverrideView, (void**)&o_OverrideView);
}
