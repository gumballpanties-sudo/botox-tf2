#include "world_fx.h"
#include "visuals.h"    // CachedCVar, CVar_ForceInt
#include "../movement/movement.h"   // Vec3, AngleVectors -- third person's eye-to-camera offset
#include "../screen/screenfx.h"   // ScreenFx_FindClientMode -- shared IClientMode lookup
#include "../movement/prediction.h"   // g_globalVars -- framecount + absoluteframetime, main thread only
#include "viewmodel_fps.h"            // g_vmFps.Frame -- the camera the low fps viewmodel re-attaches to
#include "../network/netregion.h"     // FindMatSystemSurface -- cloak meter hook
#include "world_fx3.h"                // WorldFx3_Frame -- trail, dlights, modulation, per frame
#include "../../render/render.h"      // W2S_Project -- third person crosshair
#include <mutex>

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

    // disable interp = aim/hooks.cpp Interpolate + anim hooks via G::RemoveInterp (cl_interp poke did nothing)

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

    // ==== disable post processing: botox misc.cpp disable_post_processing() ====
    // CS:GO's mat_postprocess_enable does not exist in TF2 (no such string in TF2 x64
    // engine/client/materialsystem). TF2's DoEnginePostProcessing is bloom + tonemap +
    // colour correction (sdk viewpostprocess.cpp); mat_disable_bloom (engine/client) and
    // mat_colorcorrection (engine/client/materialsystem) are its two switches. Safe mode keeps
    // it off, as botox does. Off / eject -> the values we found.
    static CachedCVar cc_bloom, cc_ccorr;
    static int s_bloomOrig = -1, s_ccorrOrig = -1;
    const bool ppOff = g_sfxDisablePostProcessing && !g_safeMode && !g_eject;
    ConVar* bloom = cc_bloom.Get("mat_disable_bloom");
    ConVar* ccorr = cc_ccorr.Get("mat_colorcorrection");
    if (ppOff) {
        if (bloom && s_bloomOrig < 0) s_bloomOrig = (int)bloom->GetFloat();
        if (ccorr && s_ccorrOrig < 0) s_ccorrOrig = (int)ccorr->GetFloat();
        if (bloom && (int)bloom->GetFloat() != 1) CVar_ForceInt(bloom, 1);
        if (ccorr && (int)ccorr->GetFloat() != 0) CVar_ForceInt(ccorr, 0);
    } else if (s_bloomOrig >= 0 || s_ccorrOrig >= 0) {
        if (bloom && s_bloomOrig >= 0) CVar_ForceInt(bloom, s_bloomOrig);
        if (ccorr && s_ccorrOrig >= 0) CVar_ForceInt(ccorr, s_ccorrOrig);
        DebugLog("disable post processing: restored mat_disable_bloom %d mat_colorcorrection %d", s_bloomOrig, s_ccorrOrig);
        s_bloomOrig = s_ccorrOrig = -1;
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

// ==== third person: botox screen.cpp third_person() ====================================
// botox forces CInput::m_camera_in_third_person + m_camera_offset; TF2's CInput exposes the
// same switch as virtuals (Amalgam IInput.h, counted: CAM_IsThirdPerson 31, CAM_ToThirdPerson 32,
// CAM_ToFirstPerson 33) and C_TFPlayer::ThirdPersonSwitch (Amalgam CTFPlayer.h VIRTUAL 256)
// re-flushes the local player's draw state, as tf's own taunt camera does. ClientModeShared::
// OverrideView then lays the camera back by cam_idealdist; the hook below overwrites that with
// eye - forward * distance (hull-traced when "wall collision" is on), Amalgam ThirdPerson().
static constexpr int k_inCamIsThirdPerson  = 31;
static constexpr int k_inCamToThirdPerson  = 32;
static constexpr int k_inCamToFirstPerson  = 33;
static constexpr int k_plThirdPersonSwitch = 256;

// botox key_bind_t styles: 0 = always (off while the menu is open), 1 = hold, 2 = toggle
static bool ThirdPersonKey()
{
    if (g_sfxThirdPersonKeyMode == 0) return !g_showMenu;
    if (g_hwnd && GetForegroundWindow() != g_hwnd) return false;
    const int key = g_sfxThirdPersonKey;
    if (key <= 0 || key > 0xFE) return false;
    return g_sfxThirdPersonKeyMode == 1 ? !Binds_Blocked() && (GetAsyncKeyState(key) & 0x8000) != 0
                                        : Binds_Toggled(key);
}

// IEngineTrace::TraceRay slot 4 (movement.h kTrace_TraceRay). World + static props only, the
// shape of Amalgam's CTraceFilterWorldAndPropsOnly: players and the local model never stop it.
struct TpTraceFilter
{
    void** vfptr;
    static bool __fastcall sShouldHit(TpTraceFilter*, void* pEnt, int)
    {
        unsigned int h = 0xFFFFFFFF;
        __try {
            unsigned int* pH = VCall<unsigned int*>(pEnt, 2);   // IHandleEntity::GetRefEHandle
            if (pH) h = *pH;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        return (h >> 16) == (1u << 15);                       // static prop serial
    }
    static int __fastcall sTraceType(TpTraceFilter*) { return 3; }   // TRACE_EVERYTHING_FILTER_PROPS
    TpTraceFilter() { static void* vt[2] = { (void*)&sShouldHit, (void*)&sTraceType }; vfptr = vt; }
};

// __try lives alone here: C2712 refuses it next to objects that need unwinding.
static bool TpTraceRaw(void* trace, const Ray_mv& ray, void* filter, GameTrace_t* tr, unsigned mask = 0x200400B /* MASK_SOLID */)
{
    using TraceRay_t = void(__fastcall*)(void*, const Ray_mv&, unsigned, void*, GameTrace_t*);
    __try {
        void** vt = *(void***)trace;
        ((TraceRay_t)vt[kTrace_TraceRay])(trace, ray, mask, filter, tr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static float ThirdPersonCollide(const Vec3& eye, const Vec3& cam)
{
    static void* s_trace = GetInterface("engine.dll", "EngineTraceClient003");
    if (!s_trace) return 1.f;
    constexpr float hull = 9.f;   // botox / Amalgam / tf taunt camera hull
    Ray_mv ray;
    ray.Init(eye, cam, Vec3(-hull, -hull, -hull), Vec3(hull, hull, hull));
    TpTraceFilter filter;
    GameTrace_t tr{};
    if (!TpTraceRaw(s_trace, ray, &filter, &tr)) return 1.f;
    return tr.fraction < 0.f ? 0.f : (tr.fraction > 1.f ? 1.f : tr.fraction);
}

// third person crosshair: the game's stays screen centre, which is not where shots go once the
// camera sits behind / beside the eye. Main thread writes the eye trace hit, render thread draws it.
struct TpAimFilter
{
    void** vfptr;
    void*  skip;
    static bool __fastcall sShouldHit(TpAimFilter* self, void* pEnt, int) { return pEnt != self->skip; }
    static int __fastcall sTraceType(TpAimFilter*) { return 0; }   // TRACE_EVERYTHING
    explicit TpAimFilter(void* s) : skip(s) { static void* vt[2] = { (void*)&sShouldHit, (void*)&sTraceType }; vfptr = vt; }
};

static std::mutex s_tpAimMx;
static Vec3       s_tpAim;
static ULONGLONG  s_tpAimMs = 0;   // 0 = none; stale after 250 ms (third person off, menu, loading)

static void ThirdPersonAimUpdate(const Vec3& eye, const Vec3& fwd)
{
    static void* s_trace = GetInterface("engine.dll", "EngineTraceClient003");
    if (!s_trace) return;
    Ray_mv ray;
    ray.Init(eye, eye + fwd * 8192.f);
    TpAimFilter filter(GetClientEntity(GetLocalPlayerIndex()));
    GameTrace_t tr{};
    if (!TpTraceRaw(s_trace, ray, &filter, &tr, 0x4600400B /* MASK_SHOT */)) return;
    std::lock_guard<std::mutex> lk(s_tpAimMx);
    s_tpAim   = tr.endpos;
    s_tpAimMs = GetTickCount64();
}

// render thread, background list (render.cpp, next to RenderWorldFx3)
void RenderThirdPersonCrosshair()
{
    if (!g_thirdPersonEnable || !g_sfxThirdPersonCrosshair) return;
    Vec3 at;
    {
        std::lock_guard<std::mutex> lk(s_tpAimMx);
        if (!s_tpAimMs || GetTickCount64() - s_tpAimMs > 250) return;
        at = s_tpAim;
    }
    ImVec2 s;
    if (!W2S_Project(at, s)) return;
    const float* c = g_sfxThirdPersonCrosshairColor;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddCircleFilled(s, 3.5f, ImGui::ColorConvertFloat4ToU32(ImVec4(0.f, 0.f, 0.f, c[3])), 12);
    dl->AddCircleFilled(s, 2.5f, ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3])), 12);
}

// Main thread, inside OverrideView. Returns true while the camera is ours.
// No sv_cheats needed: CThirdPersonManager::Update (cam_thirdperson.cpp:82, run by CAM_Think at
// render start, before OverrideView) calls CAM_ToFirstPerson every frame while sv_cheats is 0.
// So re-force whenever it kicked us, Amalgam ThirdPerson() style -- this frame draws third person.
static bool ThirdPersonSet(bool wanted)
{
    static bool s_forced = false;
    if (!g_input) return false;
    __try {
        void** vt = *(void***)g_input;
        const bool isTp = ((int(__fastcall*)(void*))vt[k_inCamIsThirdPerson])(g_input) != 0;
        if (wanted && (!s_forced || !isTp)) {
            ((void(__fastcall*)(void*))vt[k_inCamToThirdPerson])(g_input);
            if (void* local = GetClientEntity(GetLocalPlayerIndex()))
                VCall<void>(local, k_plThirdPersonSwitch, true);
            if (!s_forced) DebugLog("third person: on (CAM_ToThirdPerson)");
            s_forced = true;
        } else if (!wanted && s_forced) {
            ((void(__fastcall*)(void*))vt[k_inCamToFirstPerson])(g_input);
            if (void* local = GetClientEntity(GetLocalPlayerIndex()))
                VCall<void>(local, k_plThirdPersonSwitch, false);
            s_forced = false;
            DebugLog("third person: off (CAM_ToFirstPerson)");
        }
        return s_forced && ((int(__fastcall*)(void*))vt[k_inCamIsThirdPerson])(g_input) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { s_forced = false; return false; }
}

// botox screen.cpp: the world fov offset only while alive and not scoped (TF_COND_ZOOMED = bit 1
// of m_nPlayerCond), so a sniper's zoom is never undone by it.
static bool WorldFov_Wanted()
{
    if (!g_worldFovEnable || g_wdWorldFov == 0.f || g_eject || !IsLocalPlayerAlive()) return false;
    if (g_offCond <= 0) return true;
    void* pLocal = GetClientEntity(GetLocalPlayerIndex());
    int cond = 0;
    __try { cond = pLocal ? *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pLocal) + g_offCond) : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { cond = 0; }
    return (cond & (1 << 1)) == 0;
}

static void __fastcall h_OverrideView(void* rcx, void* pView)
{
    // the eye, before ClientModeShared::OverrideView lays a third person camera back from it
    float eye[3] = { 0.f, 0.f, 0.f }, eyeAng[3] = { 0.f, 0.f, 0.f };
    if (pView)
        __try {
            memcpy(eye, reinterpret_cast<const float*>(reinterpret_cast<uintptr_t>(pView) + k_vsOrigin), sizeof(eye));
            memcpy(eyeAng, reinterpret_cast<const float*>(reinterpret_cast<uintptr_t>(pView) + k_vsAngles), sizeof(eyeAng));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

    o_OverrideView(rcx, pView);

    // main thread, once a frame, before any model draw
    WorldFx3_Frame();   // movement trail, dlights, world/prop/skybox modulation walk
    const bool fovOn = WorldFov_Wanted();

    const bool tpOn = ThirdPersonSet(g_thirdPersonEnable && !g_eject && ThirdPersonKey() && IsLocalPlayerAlive());
    if (!pView || (!fovOn && !tpOn && !g_vmLowFps)) return;

    const uintptr_t p = reinterpret_cast<uintptr_t>(pView);
    __try
    {
        if (fovOn)   // botox screen.cpp: setup->m_fov += m_world_fov
        {
            float* fov = reinterpret_cast<float*>(p + k_vsFov);
            const float want = *fov + g_wdWorldFov;
            if (*fov > 1.f && *fov < 179.f) *fov = want < 1.f ? 1.f : (want > 179.f ? 179.f : want);
        }

        if (tpOn)
        {
            float* o = reinterpret_cast<float*>(p + k_vsOrigin);
            const float* a = reinterpret_cast<float*>(p + k_vsAngles);
            const Vec3 angles(a[0], a[1], 0.f);
            if (angles.x > -91.f && angles.x < 91.f)   // sane pitch, or the pattern/offsets are wrong
            {
                Vec3 fwd, right, up;
                AngleVectors(angles, &fwd, &right, &up);
                float dist = g_thirdPersonDistance < 20.f ? 20.f :
                             (g_thirdPersonDistance > 300.f ? 300.f : g_thirdPersonDistance);   // botox slider range
                const Vec3 from(eye[0], eye[1], eye[2]);
                if (g_sfxThirdPersonCollision)
                    dist *= ThirdPersonCollide(from, from - fwd * dist);
                Vec3 cam = from - fwd * dist;

                // shoulder offset on top of the laid back camera ( c_thirdpersonshoulder math )
                if (g_sfxThirdPersonOffsetSide != 0.f || g_sfxThirdPersonOffsetUp != 0.f)
                {
                    const Vec3 shift = right * g_sfxThirdPersonOffsetSide + up * g_sfxThirdPersonOffsetUp;
                    cam = cam + shift * (g_sfxThirdPersonCollision ? ThirdPersonCollide(cam, cam + shift) : 1.f);
                }
                o[0] = cam.x;
                o[1] = cam.y;
                o[2] = cam.z;

                if (g_sfxThirdPersonCrosshair)
                {
                    Vec3 aimFwd;
                    AngleVectors(Vec3(eyeAng[0], eyeAng[1], 0.f), &aimFwd);
                    ThirdPersonAimUpdate(from, aimFwd);
                }
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

// ==== low fps viewmodel: cloak meter -- IMatSystemSurface::DrawPanelIn3DSpace, slot 166 ====
// The spy watch meter is a C_VGuiScreen (vgui_pda_panel.cpp) parented to a viewmodel
// attachment. It reads LIVE bones, so it slid smooth while the held watch stepped.
// C_VGuiScreen::DrawModel (c_vguiscreen.cpp:552) hands &m_PanelToWorld here, then
// DrawScreenOverlay draws its z quad from the same member -- so stepping it in place moves
// both. VMatrix rows 0..2 are byte-identical to a bone matrix3x4: run it through the same
// Core as one bone, keyed by the vpanel, and it steps on the same frames as the gun.
// Slot: IMatSystemSurface.h order, DrawColoredText 162 (confirmed live, netregion.cpp) + 4,
// no overloads in between.
// Gate: ViewmodelDrawn = the viewmodel already went through Apply this frame, i.e. we are in
// DrawViewModels. World vgui screens (dispenser etc.) draw before it and stay live.
static constexpr int k_drawPanelIn3DSlot = 166;
using DrawPanelIn3D_t = void(__fastcall*)(void*, uintptr_t, const void*, int, int, float, float);
static DrawPanelIn3D_t o_DrawPanelIn3D = nullptr;

static void VmLowFps_Panel(uintptr_t vpanel, const void* mat)
{
    __try {
        auto* m = reinterpret_cast<VmFps::Mat34*>(const_cast<void*>(mat));
        const VmFps::Mat34* held = g_vmFps.Apply(reinterpret_cast<const void*>(vpanel), m, 1);
        if (held != m) *m = *held;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void __fastcall h_DrawPanelIn3D(void* rcx, uintptr_t vpanel, const void* mat,
                                       int pw, int ph, float ww, float wh)
{
    MiscHookGuard _g;
    if (g_vmLowFps && !g_eject && mat && vpanel && g_globalVars
        && g_vmFps.OnFrame(g_globalVars->framecount) && g_vmFps.ViewmodelDrawn())
        VmLowFps_Panel(vpanel, mat);
    o_DrawPanelIn3D(rcx, vpanel, mat, pw, ph, ww, wh);
}

void WorldFx_InstallViewHook()
{
    static bool s_panelTried = false;
    if (!s_panelTried) {
        s_panelTried = true;
        if (void* surf = FindMatSystemSurface())
            InstallVTableHook(surf, k_drawPanelIn3DSlot, "worldfx: DrawPanelIn3DSpace",
                              (void*)h_DrawPanelIn3D, (void**)&o_DrawPanelIn3D);
        else DebugLog("worldfx: surface NOT found, cloak meter stays live");
    }

    if (o_OverrideView) return;
    void* mode = ScreenFx_FindClientMode();
    if (!mode) return;
    InstallVTableHook(mode, 16, "worldfx: OverrideView",
                      (void*)h_OverrideView, (void**)&o_OverrideView);
}
