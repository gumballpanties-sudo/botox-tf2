#include "visuals.h"
#include "../movement/prediction.h"   // Vec3, EntF/EntI/EntV3/EntAlive, g_globalVars, TickInterval
#include "lagres.h"       // lag resolver: smoothed transform around the original SetupBones
#include "skins.h"        // Skins_OnCreateRagdoll -- the gold statue flag, set before the original
#include "hats.h"         // hat changer: our hats survive no hats, their bodygroups on the local body
#include "../network/mmfix.h"        // MMFix_OnMatchmadeServer -- sv_cheats must not be forced on casual
#include "../screen/screenfx.h"     // FrameBlend_CollectBones -- the gun's bones, taken at the draw
#include "viewmodel_fps.h"          // g_vmFps.Apply -- low fps viewmodel, swaps the gun's bones
#include "flipmath.h"     // flipslot:: -- IMaterialSystem FindMaterial/DecRef/IsErrorMaterial slots
#include "../esp/esp_fx.h"   // sound-ping ring, written from h_EmitSound below
#include "chams_api.h"    // 10-31: chams + glow passes (aim/chams.cpp)
#include "world_fx.h"     // WorldFx_Tick -- fullbright + disable interp pokes
#include "world_fx2.h"    // WorldFx2_Tick/_OnFindMaterial -- dlight, tints
#include "mc_world.h"     // MCWorld_OnSkyBoxDrawn -- minecraft sky behind the 2D sky draw
#include "../misc/game_extras.h"   // ChatTranslator_OnUserMessage -- DispatchUserMessage
#include "../../hooks/crashlog.h"     // CrashSpan: stutter probe on DrawModelExecute (chams, frame blend bones)
#include <intrin.h>       // _ReturnAddress -- net_graph must still read the real lerp

// ConVar-string redirect scratch, file-local because nothing outside this file has ever
// touched it. It lived in dllmain.cpp/globals.h next to the real settings, which is how the
// g_offMins duplicate-definition bug got in -- one owner per symbol.
//   *Buf     -- the buffer the ConVar's m_pszString is pointed at
//   *OrigPtr -- the engine's own string, put back when the feature goes off
static char  s_fogColorBuf[32]     = {};
static char* s_fogColorOrigPtr     = nullptr;
static int   s_fogColorOrigLen     = 0;
static char  s_fogColorSkyBuf[32]  = {};
static char* s_fogColorSkyOrigPtr  = nullptr;
static int   s_fogColorSkyOrigLen  = 0;
static char  s_skyNameBuf[128]     = {};

// Weather + no-hats scratch, same rule: this file is the only reader. thread.cpp asks
// Precip_IsLive() at eject instead of reaching for the two entity pointers.
static void* s_precipClass        = nullptr;
static void* s_precipEntity       = nullptr;
static void* s_precipNetworkable  = nullptr;
static int   s_offPrecipType      = -1;
static int   s_lastAppliedMode    = -99;
static int   s_offParticleProp    = -2;   // -2 = uncached, -1 = not found, >=0 = offset

// ==== Interface + CVar helpers ==================================================
void* GetInterface(const char* module, const char* name) {
    HMODULE mod = GetModuleHandleA(module);
    if (!mod) return nullptr;
    using CI_t = void*(*)(const char*, int*);
    auto ci = (CI_t)GetProcAddress(mod, "CreateInterface");
    return ci ? ci(name, nullptr) : nullptr;
}

void CVar_ForceFloat(ConVar* cv, float val) {
    if (cv) cv->ForceFloat(val);
}

void CVar_ForceInt(ConVar* cv, int val) {
    if (cv) cv->ForceInt(val);
}

// ==== Per-tick cvar pushes ======================================================
void UpdateVisualsCvars()
{
    if (!g_cvar) return;

    // sv_cheats: force 1 while fog or cheats-bypass needs it, restore 0 once on release.
    //
    // NOT on a Valve matchmade server. sv_cheats is FCVAR_REPLICATED: the server owns its
    // value, and holding the client's copy at 1 against it is half of what made every casual
    // queue bounce at the loading screen and re-queue (sv_pure bypass in thread.cpp is the
    // other half). Fog rides the same force, so fog's cheat push stands down on casual too --
    // it comes back the moment you are on a community server or in the menu.
    // The bounce is a loading screen thing, so on casual it only stands down until signon is
    // done (IsInGame) -- once in the match the push is back on, off again on the next load.
    // Safe mode turns the bypass off (menu SafeCheckbox); fog's push is not the bypass, keeps going.
    {
        static CachedCVar sv_cheats;
        static bool       s_wasOn = false;
        bool wantOn = ((g_cheatsBypass && !g_safeMode) || g_fogEnable) && (!MMFix_OnMatchmadeServer() || IsInGame());
        if (wantOn)          CVar_ForceInt(sv_cheats.Get("sv_cheats"), 1);
        else if (s_wasOn)    CVar_ForceInt(sv_cheats.Get("sv_cheats"), 0);
        s_wasOn = wantOn;
    }

    // fog_override + fog params
    {
        static CachedCVar cc_fog_override, cc_fog_enable, cc_fog_enableskybox;
        static CachedCVar cc_fog_start, cc_fog_end, cc_fog_startskybox, cc_fog_endskybox;
        static CachedCVar cc_fog_maxdensity, cc_fog_maxdensityskybox;
        static CachedCVar cc_fog_color, cc_fog_colorskybox;
        ConVar* cv_fog_override         = cc_fog_override.Get("fog_override");
        ConVar* cv_fog_enable           = cc_fog_enable.Get("fog_enable");
        ConVar* cv_fog_enableskybox     = cc_fog_enableskybox.Get("fog_enableskybox");
        ConVar* cv_fog_start            = cc_fog_start.Get("fog_start");
        ConVar* cv_fog_end              = cc_fog_end.Get("fog_end");
        ConVar* cv_fog_startskybox      = cc_fog_startskybox.Get("fog_startskybox");
        ConVar* cv_fog_endskybox        = cc_fog_endskybox.Get("fog_endskybox");
        ConVar* cv_fog_maxdensity       = cc_fog_maxdensity.Get("fog_maxdensity");
        ConVar* cv_fog_maxdensityskybox = cc_fog_maxdensityskybox.Get("fog_maxdensityskybox");
        ConVar* cv_fog_color            = cc_fog_color.Get("fog_color");
        ConVar* cv_fog_colorskybox      = cc_fog_colorskybox.Get("fog_colorskybox");

        CVar_ForceInt(cv_fog_override, g_fogEnable ? 1 : 0);

        // Redirect ConVar m_pszString to our 32-byte buffer, write "R G B" each tick.
        auto WriteFogColorStr = [](ConVar* cv, char* buf, char** origPtr, int* origLen, const char* val)
        {
            if (!val) return;
            ConVarStr s = ConVarStrSlot(cv);
            if (!s.ppStr) return;
            __try {
                if (!*origPtr && *s.ppStr) {
                    *origPtr = *s.ppStr;
                    *origLen = *s.pLen;
                }
                strncpy(buf, val, 31);
                buf[31] = '\0';
                *s.ppStr = buf;
                *s.pLen  = 32;
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        };
        auto RestoreFogColorStr = [](ConVar* cv, char** origPtr, int* origLen)
        {
            if (!*origPtr) return;
            ConVarStr s = ConVarStrSlot(cv);
            if (s.ppStr) {
                __try {
                    *s.ppStr = *origPtr;
                    *s.pLen  = *origLen;
                } __except(EXCEPTION_EXECUTE_HANDLER) {}
            }
            *origPtr = nullptr;
        };

        static bool s_fogWasEnabled = false;
        if (g_fogEnable)
        {
            CVar_ForceInt(cv_fog_enable,       1);
            CVar_ForceInt(cv_fog_enableskybox, 1);
            CVar_ForceFloat(cv_fog_start,            g_fogStart);
            CVar_ForceFloat(cv_fog_end,              g_fogEnd);
            CVar_ForceFloat(cv_fog_startskybox,      g_fogStart);
            CVar_ForceFloat(cv_fog_endskybox,        g_fogEnd);
            CVar_ForceFloat(cv_fog_maxdensity,       g_fogDensity);
            CVar_ForceFloat(cv_fog_maxdensityskybox, g_fogDensity);

            char colorStr[32];
            snprintf(colorStr, sizeof(colorStr), "%d %d %d",
                (int)(g_fogColor[0] * 255.f), (int)(g_fogColor[1] * 255.f), (int)(g_fogColor[2] * 255.f));
            WriteFogColorStr(cv_fog_color,       s_fogColorBuf,    &s_fogColorOrigPtr,    &s_fogColorOrigLen,    colorStr);
            WriteFogColorStr(cv_fog_colorskybox, s_fogColorSkyBuf, &s_fogColorSkyOrigPtr, &s_fogColorSkyOrigLen, colorStr);
            s_fogWasEnabled = true;
        }
        else if (s_fogWasEnabled)
        {
            RestoreFogColorStr(cv_fog_color,       &s_fogColorOrigPtr,    &s_fogColorOrigLen);
            RestoreFogColorStr(cv_fog_colorskybox, &s_fogColorSkyOrigPtr, &s_fogColorSkyOrigLen);
            s_fogWasEnabled = false;
        }
    }

    // viewmodel sway
    {
        static CachedCVar cc_sway_scale, cc_sway_interp;
        ConVar* cv_sway_scale  = cc_sway_scale.Get("cl_wpn_sway_scale");
        ConVar* cv_sway_interp = cc_sway_interp.Get("cl_wpn_sway_interp");
        CVar_ForceFloat(cv_sway_scale,  g_vmSwayEnable ? g_vmSwayScale  : 0.f);
        CVar_ForceFloat(cv_sway_interp, g_vmSwayEnable ? g_vmSwayInterp : 0.f);
    }

    // remove viewmodel bob
    {
        static CachedCVar cc_bobcycle, cc_bobup;
        static bool       s_lastBob = false;
        ConVar* cv_bobcycle = cc_bobcycle.Get("cl_bobcycle");
        ConVar* cv_bobup    = cc_bobup.Get("cl_bobup");

        if (g_vmRemoveBob) {
            CVar_ForceFloat(cv_bobcycle, 999.f);
            CVar_ForceFloat(cv_bobup,    0.f);
        } else if (s_lastBob) {
            CVar_ForceFloat(cv_bobcycle, 0.8f);
            CVar_ForceFloat(cv_bobup,    0.5f);
        }
        s_lastBob = g_vmRemoveBob;
    }

    // NOTE: the game "crosshair" convar is owned solely by ChSuppressGameCrosshair
    // (crosshair.cpp). This block used to force it to g_chEnable ? 0 : 1 every 100ms,
    // which (a) fought that pin every 100ms whenever only the CSGO crosshair was on,
    // and (b) forced crosshair back to 1 for players who run it at 0 with no feature
    // of ours enabled at all. One writer only.

    WorldFx_Tick();   // fullbright + disable interp pokes, same 100ms tick
    WorldFx2_Tick();  // dlight, same 100ms tick
}

// ==== Net stats ==================================================================
#define FLOW_OUTGOING 0
#define FLOW_INCOMING 1

static int g_netInfoIndex = -1;

static void DetectNetChannelIndex() {
    if (!g_engine) return;
    int candidates[] = {72,74,76,78,80,81,82,84};
    for (int idx : candidates) {
        __try {
            auto nci = VCall<INetChannelInfo*>(g_engine, idx);
            if (!nci) continue;
            float test = VCall<float>(nci, 9, 0);
            if (test >= 0.f && test < 5.f) { g_netInfoIndex = idx; break; }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

void UpdateNetStats() {
    if (!g_engine) return;

    bool inGame = false;
    __try { inGame = VCall<bool>(g_engine, 26); }
    __except(EXCEPTION_EXECUTE_HANDLER) { g_conn.store(false); return; }

    if (!inGame) { g_conn.store(false); return; }

    if (g_netInfoIndex < 0) {
        DetectNetChannelIndex();
        if (g_netInfoIndex < 0) return;
    }

    __try {
        auto nci = VCall<INetChannelInfo*>(g_engine, g_netInfoIndex);
        if (!nci) { g_conn.store(false); return; }

        float in   = VCall<float>(nci, 9, FLOW_INCOMING);
        float out  = VCall<float>(nci, 9, FLOW_OUTGOING);
        float loss = VCall<float>(nci, 11, FLOW_INCOMING); // GetAvgLoss, 0..1

        int rawPing = (int)((in + out) * 1000.f);
        g_ping.store(rawPing > k_pingSubtract ? rawPing - k_pingSubtract : 0);
        g_loss.store((int)(loss * 100.f));
        g_conn.store(true);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_netInfoIndex = -1;
        g_conn.store(false);
    }
}

void UpdateFPS() {
    static LARGE_INTEGER freq{}, last{};
    static int frames = 0;
    static float acc = 0.f;

    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    if (last.QuadPart) {
        float dt = (float)(now.QuadPart - last.QuadPart) / (float)freq.QuadPart;
        acc += dt;
        frames++;
        if (acc >= 0.25f) {
            g_fps.store((float)frames / acc);
            frames = 0;
            acc    = 0.f;
        }
    }
    last = now;
}

// ==== Eject restore ================================================================
void ResetFogCvars()
{
    if (!g_cvar) return;
    auto RestorePtr = [](const char* name, char** origPtr, int* origLen) {
        if (!*origPtr) return;
        ConVarStr s = ConVarStrSlot(g_cvar->FindVarWalk(name));
        if (s.ppStr) {
            __try {
                *s.ppStr = *origPtr;
                *s.pLen  = *origLen;
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        *origPtr = nullptr;
    };
    RestorePtr("fog_color",       &s_fogColorOrigPtr,    &s_fogColorOrigLen);
    RestorePtr("fog_colorskybox", &s_fogColorSkyOrigPtr, &s_fogColorSkyOrigLen);
    CVar_ForceInt(g_cvar->FindVarWalk("fog_override"),     0);
    CVar_ForceInt(g_cvar->FindVarWalk("fog_enable"),       0);
    CVar_ForceInt(g_cvar->FindVarWalk("fog_enableskybox"), 0);
    CVar_ForceInt(g_cvar->FindVarWalk("sv_cheats"),        0);
    // "crosshair" is restored by Crosshair_Restore() -- forcing 1 here overwrote the
    // player's own value on every eject.
}

// NO eject restore for sv_skyname, and none is needed: h_R_DrawSkyBox is stateless per draw
// (SkyInstall then SkyRestore around the original), and eject drains g_miscHookDepth to zero
// before it tears anything down, so the restore has always run by then. RestoreSkyNamePtr and
// its four saved-pointer globals used to sit here; nothing ever wrote them, so it was a no-op
// call at eject. Do not re-add a cross-frame save either -- a map change reassigns
// m_pszString, so a stale "original" written back at eject is worse than doing nothing.

// ==== Skybox hook ====================================================================
R_DrawSkyBox_t o_R_DrawSkyBox = nullptr;

// Snapshot of engine's sv_skyname string slot, so we can restore it post-draw.
struct SkySave { char** ppStr; int* pLen; char* savedPtr; int savedLen; };

// Save engine's current sv_skyname, overwrite with g_skyName. SEH kept in its own
// function (h_R_DrawSkyBox has an unwind object -> can't host __try, C2712).
__declspec(noinline) static SkySave SkyInstall()
{
    ConVarStr slot = ConVarStrSlot(g_cv_sv_skyname);
    SkySave s{ slot.ppStr, slot.pLen, nullptr, 0 };
    if (!s.ppStr) return s;
    __try {
        s.savedPtr = *s.ppStr;
        s.savedLen = *s.pLen;

        strncpy(s_skyNameBuf, g_skyName, sizeof(s_skyNameBuf) - 1);
        s_skyNameBuf[sizeof(s_skyNameBuf) - 1] = '\0';
        *s.ppStr = s_skyNameBuf;
        *s.pLen  = sizeof(s_skyNameBuf);
    } __except(EXCEPTION_EXECUTE_HANDLER) { s.ppStr = nullptr; }
    return s;
}

__declspec(noinline) static void SkyRestore(const SkySave& s)
{
    if (!s.ppStr) return;
    __try {
        *s.ppStr = s.savedPtr;
        *s.pLen  = s.savedLen;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Stateless per-frame: save engine's current sv_skyname string, install ours,
// call original, restore engine's. No cross-frame caching -> survives map change
// (map load reassigns m_pszString; we re-read + restore fresh every draw).
void __fastcall h_R_DrawSkyBox(float zFar, int nDrawFlags)
{
    ++g_miscHookDepth;
    struct Dec { ~Dec() { --g_miscHookDepth; } } _dec;

    if (!g_cv_sv_skyname || g_eject || !g_skyEnable)
        o_R_DrawSkyBox(zFar, nDrawFlags);
    else
    {
        SkySave s = SkyInstall();
        o_R_DrawSkyBox(zFar, nDrawFlags);
        SkyRestore(s);
    }
    // minecraft sky: right behind this view's 2D sky draw (botox mc_clouds' point), z-tested so
    // it only lands where nothing opaque is, whichever side of the world the engine draws sky
    if (!g_eject && g_mcWorldEnable) MCWorld_OnSkyBoxDrawn();
}

// ==== CS:GO viewmodel bob / gun lower / landing dip =====================================
#define VMCS_REMOVE_BOB g_vmRemoveBob
#include "vm_csgo_hook.inl"
void VmCsgo_Install(HMODULE client) { VmCs_Install(client); }

// ==== Viewmodel offset hook ============================================================
CalcViewModelView_t o_CalcViewModelView = nullptr;

__declspec(noinline) static bool CalcViewModel_Copy(const void* pEyePosition, const void* pEyeAngles,
                                                    float eye[3], float ang[3])
{
    __try {
        memcpy(eye, pEyePosition, sizeof(float) * 3);
        memcpy(ang, pEyeAngles, sizeof(float) * 3);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// Writes eye+offset into out[3]. Never touches the caller's Vector -- see h_ below.
__declspec(noinline) static bool CalcViewModel_ApplyOffset(const void* pEyePosition,
                                                           const void* pEyeAngles,
                                                           float out[3])
{
    __try {
        constexpr float kDeg2Rad = 3.14159265f / 180.f;
        const float* ang = reinterpret_cast<const float*>(pEyeAngles);
        float pitch = ang[0], yaw = ang[1];

        float sy = sinf(yaw   * kDeg2Rad), cy = cosf(yaw   * kDeg2Rad);
        float sp = sinf(pitch * kDeg2Rad), cp = cosf(pitch * kDeg2Rad);

        float fwd[3]   = {  cp*cy,  cp*sy, -sp };
        float right[3] = {  sy,    -cy,     0.f };
        float up[3]    = {  sp*cy,  sp*sy,  cp  };

        const float* pos = reinterpret_cast<const float*>(pEyePosition);
        out[0] = pos[0] + right[0]*g_vmOffsetX + fwd[0]*g_vmOffsetY + up[0]*g_vmOffsetZ;
        out[1] = pos[1] + right[1]*g_vmOffsetX + fwd[1]*g_vmOffsetY + up[1]*g_vmOffsetZ;
        out[2] = pos[2] + right[2]*g_vmOffsetX + fwd[2]*g_vmOffsetY + up[2]*g_vmOffsetZ;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// CBasePlayer::CalcViewModelView loops MAX_VIEWMODELS(2) and hands EVERY viewmodel the
// SAME `const Vector& eyeOrigin` -- view.cpp's own local. Offsetting in place therefore
// leaked into the next iteration: vm0 (weapon) moved 1x, vm1 moved 2x. Spy's watch is
// vm1 (tf_weapon_invis.cpp: SetViewModelIndex(1)), so the watch drifted off the weapon --
// same for grenade/offhand v_models. Offset a COPY, pass the copy: one shift each, equal.
void __fastcall h_CalcViewModelView(void* rcx, void* owner, void* pEyePosition, void* pEyeAngles)
{
    if (g_eject) { o_CalcViewModelView(rcx, owner, pEyePosition, pEyeAngles); return; }
    ++g_miscHookDepth;
    struct Dec { ~Dec() { --g_miscHookDepth; } } _dec;

    const bool csAdjust = (g_vmCsGunLower || g_vmCsLandDip) && o_VmCsBobHelper;
    if ((g_vmOffsetEnable || csAdjust) && pEyePosition && pEyeAngles)
    {
        // CS:GO order: pitch/dip on the eye first, then the offset along the adjusted angles
        float eye[3], ang[3];
        if (CalcViewModel_Copy(pEyePosition, pEyeAngles, eye, ang))
        {
            if (csAdjust) VmCs_EyeAdjustSafe(owner, eye, ang);
            if (g_vmOffsetEnable) ang[2] += g_wdVmRoll;   // botox viewmodel_offset roll: the copy only
            float moved[3] = { eye[0], eye[1], eye[2] };
            if (!g_vmOffsetEnable || CalcViewModel_ApplyOffset(eye, ang, moved))
            {
                o_CalcViewModelView(rcx, owner, moved, ang);
                return;
            }
        }
    }

    o_CalcViewModelView(rcx, owner, pEyePosition, pEyeAngles);
}

// ==== No hats ============================================================================
// THE MODEL-INDEX NAME MEMO IS DELETED, do not re-add. A model INDEX is not stable inside a
// map on TF2: cosmetics are DYNAMIC models, loaded and dropped as players come and go, and a
// freed slot is handed to the next model. It also cached FAILURES -- an index queried while
// its dynamic model was still loading resolved to nothing and stayed "nothing" for the rest
// of the map. Both faults show up the same way on screen: a hat that is hidden one moment and
// drawn the next, i.e. the flicker. Everything below keys on the model_t* instead (stable for
// as long as the model is loaded, and a different model is a different pointer), and NOTHING
// caches an unresolved name -- a failed resolve is simply retried next tick.

// IVModelInfo::GetModel = slot 1, GetModelName = slot 3.
static const void* ModelByIndex(int nModelIndex)
{
    if (!g_modelInfo || nModelIndex <= 0) return nullptr;
    const void* pModel = VCall<const void*>(g_modelInfo, 1, nModelIndex);
    return (reinterpret_cast<uintptr_t>(pModel) < 0x10000) ? nullptr : pModel;
}

static const void* GetEntityModel(void* pEnt)
{
    if (!pEnt || !g_modelInfo || g_offModelIndex <= 0) return nullptr;

    int nModelIndex = 0;
    __try {
        nModelIndex = *reinterpret_cast<int*>(
            reinterpret_cast<uintptr_t>(pEnt) + g_offModelIndex);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    return ModelByIndex(nModelIndex);
}

static bool IsHatModel(const char* modelPath)
{
    if (!modelPath) return false;

    // Dirs BEFORE exceptions. Every exception path lives under one of these dirs, so
    // testing them first is the same answer -- but almost every model drawn is not a
    // cosmetic and now fails in 3 strstr instead of 8.
    static const char* kHatDirs[] = {
        "models/player/items",
        "models/workshop/player/items",
        "models/workshop_partner/player/items",
        nullptr
    };
    bool inHatDir = false;
    for (int i = 0; kHatDirs[i]; ++i)
        if (strstr(modelPath, kHatDirs[i])) { inHatDir = true; break; }
    if (!inHatDir) return false;

    // Path after "models/" so vanilla, workshop, workshop_partner all match.
    static const char* kExceptions[] = {
        "player/items/sniper/knife_shield",
        "player/items/demo/pegleg",
        "player/items/demo/demo_booties",
        "player/items/sniper/xms_sniper_commandobackpack",
        "player/items/sniper/croc_shield",
        nullptr
    };
    for (int i = 0; kExceptions[i]; ++i)
        if (strstr(modelPath, kExceptions[i])) return false;

    return true;
}

// The player's own base model, i.e. the one that carries the bodygroups no-hats rewrites.
// The three roots are the same three IsHatModel tests; "models/player/items" is a cosmetic,
// which is why it is excluded here and caught as kind 1 above.
static bool IsPlayerBaseModel(const char* p)
{
    if (!p) return false;
    if (!strstr(p, "models/player/") &&
        !strstr(p, "models/workshop/player/") &&
        !strstr(p, "models/workshop_partner/player/")) return false;
    return strstr(p, "models/player/items") == nullptr;
}

static const char* ModelNameOf(const void* pMdl)
{
    if (!g_modelInfo || reinterpret_cast<uintptr_t>(pMdl) < 0x10000) return nullptr;
    const char* n = VCall<const char*>(g_modelInfo, 3, pMdl);
    return (reinterpret_cast<uintptr_t>(n) < 0x10000) ? nullptr : n;
}

// Resolve the name at most once, and only if something asks. Every memo in this hook keys on
// model_t*, so a cache hit never needs the string. Trivially destructible on purpose: a local
// with a destructor cannot share a function with __try (C2712).
struct LazyModelName
{
    const void* mdl  = nullptr;
    const char* name = nullptr;
    bool        done = false;
    const char* Get() { if (!done) { done = true; name = ModelNameOf(mdl); } return name; }
};

// h_DrawModelExecute asks this for every model drawn, hundreds of times a frame, and each
// answer walks a path with strstr. model_t* is stable for the life of a map, so memoize on
// it: direct-mapped, 512 slots, one pointer compare on a hit. A map load frees model_t's and
// the heap hands the same addresses back, so NoHats_FlushModelCache wipes this on every
// re-entry to a game (render.cpp slow tick).
// ONE memo, one bitmask, every answer no-hats needs about a model. The draw hook reads the
// first two bits; the 100ms sweep reads the rest, which is what took the whole strstr chain
// (and, before it, a GetModelName per entity per tick) out of the sweep.
//   MK_HAT   cosmetic -- skip the draw, and the entity whose particles get suppressed
//   MK_BASE  player base model -- bodygroup write
// 0 is a real answer ("none of the above"); an UNRESOLVED name is not cached at all.
static const unsigned char MK_HAT        = 0x01;
static const unsigned char MK_BASE       = 0x02;
static const void*   s_hatCacheKey[512] = {};
static unsigned char s_hatCacheVal[512] = {};

// Same direct-mapped memo, for the transparent-viewmodel path.
// 0 = not a viewmodel model
// 1 = the viewmodel entity's OWN model: classic v_ models, and the c_ arms the econ
//     viewmodel draws. These paths exist nowhere but the viewmodel.
// 2 = a c_/w_ weapon model. Identical file whether it is in your hands or on someone
//     else's back, so it only counts as a viewmodel when entity_index is -1 -- see
//     IsViewmodelDraw below.
static const void*   s_vmCacheKey[512] = {};
static unsigned char s_vmCacheVal[512] = {};

// THE WEAPON BUG: kind 2 was strstr(p, "models/weapons/") -- the STOCK dir only. TF2 ships
// community weapons under models/workshop{,_partner}/weapons/, and CEconEntity builds the
// viewmodel attachment from pItem->GetPlayerDisplayModel() (econ_entity.cpp:1167), i.e.
// exactly those paths. So the arms went transparent (kind 1, never touched this test) and the
// weapon stayed opaque unless it was stock. The two tests below match all three roots.
static unsigned char ViewmodelModelKind(const char* p)
{
    if (!p) return 0;
    if (strstr(p, "/v_models/")) return 1;
    if (strstr(p, "_arms.mdl"))  return 1;
    if (strstr(p, "/c_models/")) return 2;   // models/{,workshop/,workshop_partner/}weapons/c_models/
    if (strstr(p, "/weapons/"))  return 2;   // v_/w_ leftovers under any of the same roots
    return 0;
}

// Defined further down with the rest of the flat-alpha path.
void VmFlatAlpha_FlushCache();

void NoHats_FlushModelCache()
{
    memset(s_hatCacheKey, 0, sizeof(s_hatCacheKey));
    memset(s_hatCacheVal, 0, sizeof(s_hatCacheVal));
    memset(s_vmCacheKey,  0, sizeof(s_vmCacheKey));
    memset(s_vmCacheVal,  0, sizeof(s_vmCacheVal));
    VmFlatAlpha_FlushCache();
}

// C_BaseViewModel::DrawModel (c_baseviewmodel.cpp:316) SetBlends then draws, through
// CEconEntity::DrawOverriddenViewmodel, BOTH the c_ weapon attachment and the arms -- each
// arriving here as its own DrawModelExecute call, so each needs re-blending. The engine reads
// r_blend inside DrawModelExecute, which is why setting it immediately before the original
// takes effect.
// The weapon attachment is a C_ViewmodelAttachmentModel, built through
// InitializeAsClientEntityByIndex, whose first line is `index = -1` (c_baseentity.cpp:1160).
// That -1 separates the weapon in your hands from the same c_ model worn in the world, which
// always draws under a real networked index.
// 0 = not the viewmodel, 1 = the arms stack, 2 = the weapon in your hands. Frame blending needs
// the two apart: they are separate draws sharing sixteen bone slots, and one pick over both
// leaves the gun with two or three proxies for its whole length (hacks/screen/fbbones.h).
static unsigned char ViewmodelDrawKind(const void* pMdl, LazyModelName& nm, int entIdx)
{
    unsigned char kind;
    bool fresh = false;
    if (!pMdl) {
        kind = ViewmodelModelKind(nm.Get());
    } else {
        const size_t i = (reinterpret_cast<uintptr_t>(pMdl) >> 4) & 511u;
        if (s_vmCacheKey[i] == pMdl) {
            kind = s_vmCacheVal[i];
        } else {
            kind = ViewmodelModelKind(nm.Get());
            s_vmCacheKey[i] = pMdl;
            s_vmCacheVal[i] = kind;
            fresh = true;
        }
    }
    (void)fresh;
    if (kind == 1) return 1;
    if (kind == 2 && entIdx < 0) return 2;
    return 0;
}

static bool IsViewmodelDraw(const void* pMdl, LazyModelName& nm, int entIdx)
{
    return ViewmodelDrawKind(pMdl, nm, entIdx) != 0;
}

// IVRenderView slots -- public/ivrenderview.h order is DrawBrushModel, DrawIdentityBrushModel,
// TouchLight, Draw3DDebugOverlays, SetBlend, GetBlend, SetColorModulation, GetColorModulation.
// Hand-rolled rather than VCall: a call that could not be made must read as 1.f (the
// no-op blend the restore falls back on), and VCall's zero return cannot be told apart
// from a genuine blend of 0.
static float RenderView_GetBlend()
{
    float r = 1.f;
    if (!g_renderView) return r;
    __try {
        void** vt = *reinterpret_cast<void***>(g_renderView);
        if (reinterpret_cast<uintptr_t>(vt) >= 0x10000 && vt[5])
            r = (reinterpret_cast<float(__fastcall*)(void*)>(vt[5]))(g_renderView);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}

static void RenderView_SetBlend(float blend)
{
    if (g_renderView) VCall<void>(g_renderView, 4, blend);
}

// ==== Flat viewmodel alpha ($blendtintbybasealpha) =================================
// WHY THE WEAPON GOES NEARLY INVISIBLE AT 0.50: SetBlend feeds the modulation alpha, and
// every model shader then multiplies the BASE TEXTURE's alpha into it -- which on a TF2
// weapon is the phong mask, mostly dark. So 0.50 renders as 0.50 * mask, per pixel, which
// no slider value can compensate for. Same multiply in all three paths:
//   dx9 phong/skin  skin_ps20b.fxc:373   alpha = lerp( baseColor.a * alpha, alpha, ... )
//   dx9 non-phong   vertexlit_and_unlit_generic_ps2x.fxc:376   alpha *= baseColor.a
//   dx8             VertexLitGeneric.psh  mul r0, t0, c3   ; r0.a = t0.a * c3.a
//
// $blendtintbybasealpha is the ONE switch that removes it from all three -- skin_ps20b.fxc:371
// and ..._ps2x.fxc:375 guard the multiply on it, and vertexlitgeneric_dx8.cpp:463 picks
// VertexLitGeneric_BlendTint.psh, whose last line is `+mov r0.a, c3`. Works phong or not, and
// unlike $basemapalphaphongmask (the first attempt) it does not flatten the normal map. Its
// other half -- tint by base alpha -- is identity here: both forms lerp between X and
// X * modulation.rgb, and modulation.rgb is white because SetBlend only writes alpha.
//
// COST: it is a STATIC decision (shader pick on dx8, SET_STATIC_PIXEL_SHADER_COMBO on dx9),
// so the material must RecomputeStateSnapshots() after the var moves -- never inside
// DrawModelExecute. VmFlatAlpha_Tick does it from the render slow tick, so the flat alpha
// arrives up to 100ms after a weapon is first drawn.
//
// WHERE THE MATERIALS COME FROM: NOT GetModelMaterialCount -- it returned 0 for every model
// tried (c_spy_arms, c_sapper, c_ambassador, v_watch_pocket_spy), so GetModelMaterials never
// ran and the checkbox did nothing. DrawModelExecute is handed the materials already: pState
// is a DrawModelState_t (ivmodelrender.h:38) whose m_pStudioHWData carries
// studioloddata_t::ppMaterials (studio.h:1611), the exact array about to be bound. Plain
// struct reads -- no vtable, no interface version. x64 layout, unchanged since this branch:
//   DrawModelState_t : m_pStudioHdr 0x00, m_pStudioHWData 0x08, m_pRenderable 0x10,
//                      m_pModelToWorld 0x18, m_decals 0x20, m_drawFlags 0x28, m_lod 0x2C
//   studiohwdata_t   : m_RootLOD 0x00, m_NumLODs 0x04, m_pLODs 0x08, m_NumStudioMeshes 0x10
//   studioloddata_t  : m_pMeshData 0x00, m_SwitchPoint 0x08, numMaterials 0x0C,
//                      ppMaterials 0x10, pMaterialFlags 0x18 -- stride 0x30
static const size_t kLodStride = 0x30;

// IMaterial: GetName(0), FindVar(11), RecomputeStateSnapshots(16).
// IMaterialVar: GetTextureValue(0), GetName(1), GetNameAsSymbol(2), SetFloatValue(3),
// SetIntValue(4), IsDefined(17); m_intVal follows the vtable ptr and m_pStringVal (+0x10).
static const char* Material_GetName(void* pMat) { return VCall<const char*>(pMat, 0); }

// $blendtintbybasealpha is baked into the shader pick / static combo at snapshot time,
// so a material that does not re-snapshot never reads the change.
static void Material_RecomputeStateSnapshots(void* pMat) { VCall<void>(pMat, 16); }

static void* Material_FindVar(void* pMat, const char* name)
{
    bool found = false;
    void* v = VCall<void*>(pMat, 11, name, &found, false);
    return found ? v : nullptr;
}

static int MatVar_GetInt(void* pVar)
{
    int v = -1;
    __try { v = *reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(pVar) + 0x10); }
    __except(EXCEPTION_EXECUTE_HANDLER) { v = -1; }
    return v;
}

static void MatVar_SetInt(void* pVar, int val)   { VCall<void>(pVar, 4, val); }
static bool MatVar_IsDefined(void* pVar)         { return VCall<bool>(pVar, 17); }

// One entry per viewmodel model_t*: every material of that model, its $blendtintbybasealpha
// var, and the value the var held when we found it. Direct-mapped like the other two model
// memos and wiped by the same NoHats_FlushModelCache, because model_t* addresses get
// recycled per map -- and the IMaterial*/IMaterialVar* behind them die with the map too,
// so the cache is what bounds their lifetime. n == 0 is a cached "nothing here".
static const int kVmMatSlots = 64;
static const int kVmMatMax   = 24;
struct VmMatEntry {
    const void*   key;
    void*         mat[kVmMatMax];    // IMaterial*, for the re-snapshot
    void*         var[kVmMatMax];    // its $blendtintbybasealpha
    unsigned char orig[kVmMatMax];
    int           n;
    bool          applied;           // is this entry currently flipped to 1?
    bool          evict;             // a different model wants this slot -- restore, then free it
};
static VmMatEntry s_vmMat[kVmMatSlots] = {};

// What the hook wants vs what the tick has done. The write and its RecomputeStateSnapshots
// happen only in VmFlatAlpha_Tick, never inside a draw call.
static bool s_vmFlatWant    = false;
static bool s_vmFlatPatched = false;
// A slot collision asked for an eviction. Part of the tick's early-out so a pending restore
// still runs with the feature already switched off.
static bool s_vmFlatEvict   = false;

// Walk the cache and make every entry match s_vmFlatWant. Runs from render.cpp's slow tick
// -- the same render thread as the draw, but outside DrawModelExecute, which is what makes
// the re-snapshot safe.
void VmFlatAlpha_Tick()
{
    // Derived HERE, not pushed in from the draw hook. The four terms are all globals, so the
    // per-draw VmFlatAlpha_SetWanted() call was a store executed hundreds of times a frame to
    // hand this function a value it can read directly.
    s_vmFlatWant = g_vmAlphaEnable && g_vmAlpha < 0.999f && g_renderView != nullptr && g_vmFlatAlpha;

    if (!s_vmFlatWant && !s_vmFlatPatched && !s_vmFlatEvict) return;
    s_vmFlatEvict = false;

    bool anyOn = false;
    for (int s = 0; s < kVmMatSlots; ++s) {
        VmMatEntry& e = s_vmMat[s];
        if (!e.key || e.n == 0) { if (e.evict) e = VmMatEntry{}; continue; }

        // An evicting entry is going back to its ORIGINAL values whatever the toggle says --
        // this is the last moment anything holds a pointer to those materials, so the restore
        // has to happen here or they stay flat-alpha for the rest of the map with nothing left
        // to undo them. Done on this tick (render thread, outside any draw) and not in
        // VmFlatAlpha_SetFor, which runs INSIDE DrawModelExecute where the write and its
        // RecomputeStateSnapshots are not safe.
        const bool want = e.evict ? false : s_vmFlatWant;
        if (e.applied != want) {
            for (int i = 0; i < e.n; ++i) {
                MatVar_SetInt(e.var[i], want ? 1 : e.orig[i]);
                Material_RecomputeStateSnapshots(e.mat[i]);
            }
            e.applied = want;
        }
        if (e.evict) { e = VmMatEntry{}; continue; }   // slot free -- the new model takes it next draw
        anyOn = anyOn || e.applied;
    }
    s_vmFlatPatched = anyOn;
}

void VmFlatAlpha_FlushCache()
{
    // No restore pass here on purpose: this runs on re-entry to a game, by which point
    // the previous map's materials may already be freed and every pointer below dangling.
    memset(s_vmMat, 0, sizeof(s_vmMat));
    s_vmFlatPatched  = false;
    s_vmFlatEvict    = false;
}

// Take one material: resolve its $blendtintbybasealpha, keep it.
// btba == -1 means the var does not exist on this material, i.e. its shader does not
// declare it and the VMT does not set it -- nothing to flip, so the material is skipped.
static void VmFlatAlpha_TakeMaterial(VmMatEntry* e, void* pMat)
{
    if (reinterpret_cast<uintptr_t>(pMat) < 0x10000 || e->n >= kVmMatMax) return;
    for (int i = 0; i < e->n; ++i)          // LODs repeat materials
        if (e->mat[i] == pMat) return;

    void* pVar = Material_FindVar(pMat, "$blendtintbybasealpha");
    const int  cur = pVar ? MatVar_GetInt(pVar) : -1;

    if (!pVar) return;
    e->orig[e->n] = (cur == 1) ? 1u : 0u;
    e->mat[e->n]  = pMat;
    e->var[e->n]  = pVar;
    ++e->n;
}

static VmMatEntry* VmFlatAlpha_SetFor(const void* pMdl, const void* pState, const char* mdlName)
{
    if (!pMdl || !pState) return nullptr;
    const size_t slot = (reinterpret_cast<uintptr_t>(pMdl) >> 4) & (kVmMatSlots - 1);
    VmMatEntry* e = &s_vmMat[slot];
    if (e->key == pMdl) return e;

    // SLOT COLLISION. The occupant used to be overwritten right here, and if it was applied
    // its materials kept $blendtintbybasealpha at 1 with no pointer left to put them back --
    // a viewmodel that stays flat-alpha for the rest of the map, feature off or not. The
    // restore cannot run from inside a draw call, so hand the entry to VmFlatAlpha_Tick and
    // leave this model unclaimed; it takes the slot on a later draw, once the tick has freed it.
    if (e->key && e->applied) { e->evict = true; s_vmFlatEvict = true; return nullptr; }

    e->key     = pMdl;
    e->n       = 0;
    e->applied = false;   // fresh entry: the tick has not flipped it yet
    e->evict   = false;

    int nLods = 0;
    __try {
        const uint8_t* st = reinterpret_cast<const uint8_t*>(pState);
        const uint8_t* hw = *reinterpret_cast<const uint8_t* const*>(st + 0x08);
        if (reinterpret_cast<uintptr_t>(hw) >= 0x10000)
        {
            nLods = *reinterpret_cast<const int*>(hw + 0x04);
            const uint8_t* lods = *reinterpret_cast<const uint8_t* const*>(hw + 0x08);
            if (nLods > 0 && nLods <= 16 && reinterpret_cast<uintptr_t>(lods) >= 0x10000)
            {
                // Every LOD, not just m_lod: a viewmodel sits on LOD 0 in practice, but the
                // array is per-LOD and the same IMaterial* repeats across them, so walking
                // all of them costs nothing (TakeMaterial dedups on the var pointer) and
                // cannot miss the one that is about to be bound.
                for (int l = 0; l < nLods; ++l) {
                    const uint8_t* lod = lods + static_cast<size_t>(l) * kLodStride;
                    const int nMat = *reinterpret_cast<const int*>(lod + 0x0C);
                    void** pp = *reinterpret_cast<void** const*>(lod + 0x10);
                    if (nMat <= 0 || nMat > 64 || reinterpret_cast<uintptr_t>(pp) < 0x10000)
                        continue;
                    for (int i = 0; i < nMat; ++i)
                        VmFlatAlpha_TakeMaterial(e, pp[i]);
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    return e;
}

// Every no-hats question about one model path, answered once.
static unsigned char ClassifyModelPath(const char* p)
{
    if (!p) return 0;
    unsigned char k = 0;
    if (IsHatModel(p))            k |= MK_HAT;
    else if (IsPlayerBaseModel(p)) k |= MK_BASE;

    return k;
}

// Name resolved on a miss only. An UNRESOLVED name is never written to the memo: the model is
// still loading (dynamic cosmetics load on demand), and caching that "no" is what left hats
// drawn for the rest of the map.
static unsigned char NoHatsModelKind(const void* pMdl, LazyModelName& nm)
{
    if (!pMdl) return ClassifyModelPath(nm.Get());

    const size_t i = (reinterpret_cast<uintptr_t>(pMdl) >> 4) & 511u;
    if (s_hatCacheKey[i] == pMdl) return s_hatCacheVal[i];

    const char* p = nm.Get();
    if (!p) return 0;
    const unsigned char k = ClassifyModelPath(p);
    s_hatCacheKey[i] = pMdl;
    s_hatCacheVal[i] = k;
    return k;
}

// Same answer for an entity, straight off its model pointer -- the sweep's per-entity path.
static unsigned char EntityModelKind(void* pEnt)
{
    const void* pMdl = GetEntityModel(pEnt);
    if (!pMdl) return 0;
    LazyModelName nm; nm.mdl = pMdl;
    return NoHatsModelKind(pMdl, nm);
}

// class index from player model path ("models/player/spy.mdl" etc.)
static int GetClassFromPlayerModelName(const char* mdlName) {
    if (!mdlName) return 0;
    static const struct { const char* tok; int cls; } kMap[] = {
        {"/scout",1},{"/sniper",2},{"/soldier",3},{"/demo",4},
        {"/medic",5},{"/heavy",6},{"/pyro",7},{"/spy",8},{"/engineer",9},
    };
    for (auto& m : kMap)
        if (strstr(mdlName, m.tok)) return m.cls;
    return 0;
}

// Defined with the rest of the particle code below; the sweep is the only caller that
// needs it early.
static void SetEntityParticlesSimulate(void* pEnt, bool bSim, bool bRemoveOnly = false);

// IClientEntityList::GetHighestEntityIndex = slot 6 (icliententitylist.h order:
// GetClientNetworkable 0, ...FromHandle 1, GetClientUnknownFromHandle 2, GetClientEntity 3,
// ...FromHandle 4, NumberOfEntities 5, GetHighestEntityIndex 6). The sweep walked to 2048
// unconditionally; a live TF2 server is usually a few hundred entities, and every slot past
// the top costs a virtual call to be told "nothing here".
static int EntityList_HighestIndex()
{
    if (!g_entityList) return 0;
    const int hi = VCall<int>(g_entityList, 6);
    if (hi < 33)   return 32;      // nothing above the players
    if (hi > 2048) return 2048;
    return hi;
}

// No-unusual suppression pass over cosmetics and players, on the 100ms tick.
void NoHats_Sweep(bool wantParticle)
{
    if (g_offModelIndex <= 0 || !wantParticle) return;

    __try {
        // Taunt unusuals hang off the PLAYER, not a wearable, so slots 33+ never see them.
        // Remove-only -- a player's other particles (fire, uber, healbeam) must keep
        // simulating.
        for (int i = 1; i <= 32; ++i) {
            void* pPlayer = GetClientEntity(i);
            if (pPlayer) SetEntityParticlesSimulate(pPlayer, false, true);
        }

        const int hi = EntityList_HighestIndex();
        for (int i = 33; i <= hi; ++i)
        {
            void* pEnt = GetClientEntity(i);
            if (pEnt && (EntityModelKind(pEnt) & MK_HAT))
                SetEntityParticlesSimulate(pEnt, false);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- Bodygroups with cosmetics hidden -----------------------------------------------
// The client rebuilds a player's m_nBody from every weapon and wearable he has
// (CTFPlayerShared::RecalculatePlayerBodygroups, tf_player_shared.cpp), so the body in
// ModelRenderInfo_t is exact and stable. No hats only takes back what cosmetics hid:
//   * groups only cosmetics touch  -> cleared
//   * groups only weapons touch    -> left alone (Gunslinger arm, Huntsman arrows, ...)
//   * groups both touch            -> kept while the player owns the functional item
// The old version rebuilt the body from zero off a 100ms entity sweep and wrote it into
// m_nBody, which the game rewrites: every missed tick popped a group back (flicker), and
// the Gunslinger has no world model to find, so its arm was always wiped.
// Tables: models/player/<class>.mdl bodyparts crossed with items_game.txt
// player_bodygroups / additional_hidden_bodygroups / wm_bodygroup_override.
enum BodyFn : uint8_t { kFnNone, kFnDemoBoots, kFnMedicPack, kFnHeavyFists, kFnThruster };
struct BodyRule { uint8_t base, count, fn; };   // fn = kFnNone: always cleared

static const BodyRule k_bodyRules[10][4] = {
    {},
    /* 1 scout    */ { {1,2,kFnNone}, {2,2,kFnNone}, {4,2,kFnNone}, {8,2,kFnNone} },   // hat headphones shoes_socks dogtags
    /* 2 sniper   */ { {2,2,kFnNone} },                                              // hat
    /* 3 soldier  */ { {2,2,kFnNone}, {4,2,kFnNone}, {8,2,kFnNone} },                 // hat medal grenades
    /* 4 demo     */ { {2,2,kFnDemoBoots}, {4,2,kFnNone} },                           // shoes grenades
    /* 5 medic    */ { {1,2,kFnMedicPack} },                                         // medic_backpack
    /* 6 heavy    */ { {1,3,kFnHeavyFists}, {3,2,kFnNone} },                          // hands bullets
    /* 7 pyro     */ { {1,2,kFnNone}, {2,2,kFnNone}, {4,2,kFnThruster} },             // head grenades backpack
    /* 8 spy      */ {},
    /* 9 engineer */ { {1,2,kFnNone} },                                              // hat
};

static bool DefIn(int def, BodyFn fn)
{
    switch (fn) {
    case kFnDemoBoots:  return def == 405 || def == 608;                  // Ali Baba's Wee Booties, Bootlegger
    case kFnMedicPack:  return def == 411 || def == 998;                  // Quick-Fix, Vaccinator
    case kFnThruster:   return def == 1179;                               // Thermal Thruster
    case kFnHeavyFists: return def == 43  || def == 239 || def == 331 || def == 587 || def == 656
                            || def == 1084 || def == 1100 || def == 1184; // every tf_weapon_fists that hides hands
    default:            return false;
    }
}

static uint8_t s_bodyFnOwned[33];
static int     s_offMyWeapons  = 0;
static int     s_offWearables  = 0;

// m_hMyWearables is a RecvPropUtlVector: the named prop is at offset 0 and the vector's real
// offset lives in the child props' CRecvPropExtra_UtlVector (4 pointers, stride, m_Offset).
static int FindWearablesOffset(RecvTable* tab, int base, int depth)
{
    if (!tab || depth > 6) return -1;
    for (int i = 0; i < tab->m_nProps; ++i) {
        RecvProp& p = tab->m_pProps[i];
        if (!p.m_pVarName) continue;
        if (strcmp(p.m_pVarName, "m_hMyWearables") == 0 && p.m_pDataTable &&
            p.m_pDataTable->m_nProps > 0 && p.m_pDataTable->m_pProps[0].m_pExtraData) {
            const uint8_t* extra = static_cast<const uint8_t*>(p.m_pDataTable->m_pProps[0].m_pExtraData);
            return base + p.m_Offset + *reinterpret_cast<const int*>(extra + 36);
        }
        if (p.m_pDataTable) {
            const int r = FindWearablesOffset(p.m_pDataTable, base + p.m_Offset, depth + 1);
            if (r >= 0) return r;
        }
    }
    return -1;
}

__declspec(noinline) static uint8_t OwnedBodyFns_SEH(void* pPlayer, int cls)
{
    uint8_t owned = 0;
    __try {
        if (cls == 4) {
            if (s_offWearables <= 0) return 0;
            const uint8_t* vec = reinterpret_cast<const uint8_t*>(pPlayer) + s_offWearables;
            const unsigned int* h = *reinterpret_cast<const unsigned int* const*>(vec);
            const int count = *reinterpret_cast<const int*>(vec + 16);     // CUtlVector m_Size
            if (!h || count <= 0 || count > 16) return 0;
            for (int i = 0; i < count; ++i) {
                void* pWear = GetClientEntity((int)(h[i] & 0x3FFF));
                if (pWear && DefIn(*reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(pWear) + g_offItemDefIndex), kFnDemoBoots))
                    owned |= 1 << kFnDemoBoots;
            }
            return owned;
        }
        if (s_offMyWeapons <= 0) return 0;
        const BodyFn fn = (cls == 5) ? kFnMedicPack : (cls == 6) ? kFnHeavyFists : kFnThruster;
        const uint8_t* wpns = reinterpret_cast<const uint8_t*>(pPlayer) + s_offMyWeapons;
        for (int i = 0; i < 48; ++i) {                                  // MAX_WEAPONS
            const unsigned int h = *reinterpret_cast<const unsigned int*>(wpns + i * 4);
            if (h == 0 || h == 0xFFFFFFFF) continue;
            void* pWpn = GetClientEntity((int)(h & 0x3FFF));
            if (pWpn && DefIn(*reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(pWpn) + g_offItemDefIndex), fn))
                owned |= 1 << fn;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return owned;
}

// FRAME_RENDER_START (game thread): which mixed groups each player owns the functional item for.
void NoHats_BodyTick()
{
    memset(s_bodyFnOwned, 0, sizeof(s_bodyFnOwned));
    if (!g_noHats) return;
    Movement_ResolveExtraNetvars();                                     // g_offItemDefIndex
    if (s_offMyWeapons == 0) {
        RecvTable* t = FindNetworkTable("DT_TFPlayer");
        if (!t) return;
        int off = FindPropOffset(t, "m_hMyWeapons");
        s_offMyWeapons = (off > 0) ? off : -1;
        __try { off = FindWearablesOffset(t, 0, 0); } __except(EXCEPTION_EXECUTE_HANDLER) { off = -1; }
        s_offWearables = (off > 0) ? off : -1;
        DebugLog("nohats: m_hMyWeapons=0x%X m_hMyWearables=0x%X itemdef=0x%X",
                 s_offMyWeapons, s_offWearables, g_offItemDefIndex);
    }
    if (g_offItemDefIndex <= 0) return;

    for (int p = 1; p <= 32; ++p) {
        const int cls = GetPlayerClass(p);
        if (cls < 4 || cls > 7) continue;                               // demo medic heavy pyro only
        if (void* pPlayer = GetClientEntity(p)) s_bodyFnOwned[p] = OwnedBodyFns_SEH(pPlayer, cls);
    }
}

// Body with every cosmetic-only group at its default, laid out for the model's class (a
// disguised spy's model gets the disguise class's layout).
static int CleanBody(int entIdx, int modelCls, int body)
{
    if (modelCls < 1 || modelCls > 9 || body <= 0) return body;
    const uint8_t owned = (entIdx >= 1 && entIdx <= 32) ? s_bodyFnOwned[entIdx] : 0;
    for (const BodyRule& r : k_bodyRules[modelCls]) {
        if (!r.base) break;
        if (r.fn != kFnNone && (owned & (1 << r.fn))) continue;
        body -= ((body / r.base) % r.count) * r.base;
    }
    return body;
}

// -- Particle suppression -------------------------------------------------------
// CParticleProperty found by scanning for m_pOuter back-pointer; cached in
// s_offParticleProp.
static int FindParticlePropOff(void* pEnt)
{
    if (s_offParticleProp >= 0)  return s_offParticleProp;
    if (s_offParticleProp == -1) return -1;

    const uintptr_t self = reinterpret_cast<uintptr_t>(pEnt);
    const uint8_t*  b    = reinterpret_cast<const uint8_t*>(pEnt);

    for (int off = 0x80; off < 0x2000; off += 8) {
        __try {
            if (*reinterpret_cast<const uintptr_t*>(b + off) != self) continue;
            int sz = *reinterpret_cast<const int*>(b + off + 24);
            if (sz < 0 || sz > 32) continue;
            int al = *reinterpret_cast<const int*>(b + off + 16);
            if (al < 0 || al > 64) continue;
            s_offParticleProp = off;
            return off;
        } __except(EXCEPTION_EXECUTE_HANDLER) { break; }
    }
    s_offParticleProp = -1;
    DebugLog("noHats: CParticleProperty not found");
    return -1;
}

// One-time calibration: byte offset of flags bitfield in CNewParticleEffect
// (bit 1 = m_bRemove, bit 7 = m_bSimulate). Toggle SetShouldSimulate, find
// which byte flips bit 7.
static int s_bRemoveOffset = -1; // -1=uncalibrated, -2=failed
static void __declspec(noinline) CalibrateBitfieldOffset(void* pEffect, void** vt)
{
    __try {
        using SetSim_t = void(__fastcall*)(void*, bool);
        auto fnSetSim = (SetSim_t)vt[k_setSimSlot];
        uint8_t* obj = (uint8_t*)pEffect;
        fnSetSim(pEffect, false);
        uint8_t snap[2048];
        memcpy(snap, obj, sizeof(snap));
        fnSetSim(pEffect, true);
        for (int off = 8; off < (int)sizeof(snap); off++) {
            if (!(snap[off] & 0x80) && (obj[off] & 0x80)) {
                s_bRemoveOffset = off;
                return;
            }
        }
        s_bRemoveOffset = -2;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        s_bRemoveOffset = -2;
    }
}

// Unusual particle system names, straight off the item schema's
// attribute_controlled_attached_particles block.
// 621 of its 645 systems are named "unusual_*", "superrare_*" or "utaunt_*", which is what
// the prefix test catches. Of the 24 that are not:
//   3 ARE cosmetic unusuals with legacy names (group "cosmetic_unusual_effects") and were
//     never removed -- listed below.
//  16 are group "killstreak_eyeglows" -- killstreak sheens and eyes, not unusuals, and the
//     skinchanger puts them on deliberately. Left alone.
//   5 are group "other_particles": burningplayer_red / burningplayer_flyingbits (afterburn),
//     particle_nemesis_burst_blue, community_sparkle, halopoint. Shared with real gameplay
//     and item effects, so blocking them by name would take out afterburn. Left alone.
static const char* const kUnusualNames[] = {
    "stamp_spin", "drg_pipe_smoke", "coin_spin",
    nullptr,
};

static inline bool IsUnusualParticleName(const char* pName)
{
    if (strstr(pName, "unusual_") || strstr(pName, "superrare_") || strstr(pName, "utaunt_"))
        return true;
    for (int i = 0; kUnusualNames[i]; ++i)
        if (!strcmp(pName, kUnusualNames[i])) return true;
    return false;
}

// bSim=false: kill unusual effects via m_bRemove, freeze others.
// bSim=true: re-enable simulation.
// bRemoveOnly: only kill the unusual ones, never touch the rest. Players carry
// burning / uber / healbeam effects on the same particle prop -- freezing those
// would leave a stuck fire or beam on screen, so the player sweep uses this.
static void SetEntityParticlesSimulate(void* pEnt, bool bSim, bool bRemoveOnly)
{
    if (!pEnt) return;
    int propOff = FindParticlePropOff(pEnt);
    if (propOff < 0) return;

    const uint8_t* b = reinterpret_cast<const uint8_t*>(pEnt);
    void* effectsArr = nullptr;
    int   numEffects = 0;
    __try {
        effectsArr = *reinterpret_cast<void* const*>(b + propOff + 8);
        numEffects = *reinterpret_cast<const int*>  (b + propOff + 24);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }

    if (!effectsArr || numEffects <= 0 || numEffects > 32) return;

    using SetSim_t  = void(__fastcall*)(void*, bool);
    using GetName_t = const char*(__fastcall*)(void*);
    for (int i = 0; i < numEffects; ++i) {
        void* pEffect = nullptr;
        __try {
            uint8_t* entry = reinterpret_cast<uint8_t*>(effectsArr) + i * k_particleEffectStride;
            pEffect = *reinterpret_cast<void**>(entry + k_particleEffectPtrOff);
        } __except(EXCEPTION_EXECUTE_HANDLER) { continue; }
        if (!pEffect || reinterpret_cast<uintptr_t>(pEffect) < 0x10000) continue;

        void** vt = nullptr;
        __try { vt = *reinterpret_cast<void***>(pEffect); } __except(EXCEPTION_EXECUTE_HANDLER) { continue; }
        if (!vt || reinterpret_cast<uintptr_t>(vt[k_setSimSlot]) < 0x10000) continue;

        if (!bSim) {
            const char* pName = nullptr;
            __try {
                if (reinterpret_cast<uintptr_t>(vt[11]) > 0x10000)
                    pName = ((GetName_t)vt[11])(pEffect);
            } __except(EXCEPTION_EXECUTE_HANDLER) {}

            if (pName && reinterpret_cast<uintptr_t>(pName) > 0x10000 &&
                IsUnusualParticleName(pName))
            {
                if (s_bRemoveOffset == -1)
                    CalibrateBitfieldOffset(pEffect, vt);
                if (s_bRemoveOffset >= 0) {
                    __try {
                        *((uint8_t*)pEffect + s_bRemoveOffset) |= 0x02; // m_bRemove
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                }
                continue;
            }

            if (bRemoveOnly) continue;   // not unusual -> leave it alone
        }

        __try {
            ((SetSim_t)vt[k_setSimSlot])(pEffect, bSim);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
}

// The off-transition only: put every cosmetic's particles back to simulating. The ON pass
// lives in NoHats_Sweep, which visits the same entities the body-target pass already visits.
void RestoreHatParticles()
{
    if (g_offModelIndex <= 0) return;
    __try {
        const int hi = EntityList_HighestIndex();
        for (int i = 33; i <= hi; ++i) {
            void* pEnt = GetClientEntity(i);
            if (!pEnt) continue;
            if (!(EntityModelKind(pEnt) & MK_HAT)) continue;
            SetEntityParticlesSimulate(pEnt, true, false);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// ==== Chams + glow (10-31 rework) ====================================================
// The passes, materials and settings live in aim/chams.cpp (chams_api.h): botox chams
// structure + Amalgam material/override flow, materials created from KeyValues with one
// IncrementReferenceCount each. The old FindMaterial'd botox_chams/botox_glow VMT files are gone
// -- they were never IncRef'd but DecRef'd every map change ("bad reference count 0" spam).
// Lifecycle still rides flipworld.cpp's CViewRender::LevelInit/LevelShutdown hook.
void Chams_LevelUnload() { Chams_ReleaseMaterials(); }
void Chams_LevelInit()   {}   // rebuilt lazily on the first chams draw of the map
// Glow_LevelInit / Glow_LevelUnload: aim/chams.cpp (render targets for the Amalgam glow pass)

// ==== DrawModelExecute hook (no hats + transparent viewmodel) ======================
DrawModelExecute_t o_DrawModelExecute = nullptr;

// Low fps viewmodel: the one game read (studiohdr_t::numbones off DrawModelState_t's first
// pointer, fbbones.h offsets) in its own __try, then the pure Core picks the array to draw with.
// Falls back to the live bones on any doubt -- a wrong count would index past the slot.
static const void* VmLowFps_Bones(const void* pState, const void* pBones, const void* pMdl)
{
    int count = 0;
    __try {
        const uint8_t* hdr = *reinterpret_cast<const uint8_t* const*>(pState);
        if (reinterpret_cast<uintptr_t>(hdr) >= 0x10000)
            count = *reinterpret_cast<const int*>(hdr + VmFps::kStudioNumBones);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return pBones; }

    if (count <= 0 || count > VmFps::kMaxBones) return pBones;
    return g_vmFps.Apply(pMdl, reinterpret_cast<const VmFps::Mat34*>(pBones), count);
}

// body kept in _impl: it uses __try, so the MiscHookGuard cannot live here (C2712)
static void __fastcall h_DrawModelExecute_impl(void* rcx, const void* pState, const void* pInfo, const void* pBones)
{
    // 1.f is a no-op blend, so treat it as off and keep the hook on its cheap path.
    // These read nothing but globals, so VmFlatAlpha_Tick derives s_vmFlatWant itself now --
    // the hook used to store it on every single model draw to tell the 100ms tick something
    // the tick could see for itself.
    const bool wantVmAlpha = g_vmAlphaEnable && g_vmAlpha < 0.999f && g_renderView != nullptr;
    const bool wantVmFlat  = wantVmAlpha && g_vmFlatAlpha;

    // Frame blending wants the viewmodel's BONE MATRICES, and this is the only place they
    // exist: the gun's animation is invisible to a depth reprojection, which can only ever see
    // what the camera did. See hacks/screen/fbbones.h.
    const bool wantFbBones = g_fbEnable && g_fbViewmodel;
    const bool wantChams   = Chams_AnyActive();   // chams or glow (aim/chams.cpp)
    // OnFrame: OverrideView fed this frame's camera (world_fx.cpp). Without it the hold would freeze.
    const bool wantVmLowFps = g_vmLowFps && g_globalVars != nullptr && g_vmFps.OnFrame(g_globalVars->framecount);

    const bool wantHatBody = Hats_WantBody();   // hats.cpp: our hats' bodygroups on the local player
    if (pInfo) {
        const int hatEnt = *reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(pInfo) + 0x44);
        Hats_NoteDraw(hatEnt);                 // diag: does a hat reach the renderer
        if (Hats_SkipDraw(hatEnt)) return;     // the real hat one of ours replaced in the loadout
    }
    if (g_eject || (!g_noHats && !wantVmAlpha && !wantFbBones && !wantChams && !wantVmLowFps && !wantHatBody)) {
        o_DrawModelExecute(rcx, pState, pInfo, pBones);
        return;
    }

    const uint8_t* pInfoB = pInfo ? reinterpret_cast<const uint8_t*>(pInfo) : nullptr;
    const void*    pMdl   = nullptr;
    if (pInfoB) {
        pMdl = *reinterpret_cast<const void* const*>(pInfoB + 0x20);
        if (reinterpret_cast<uintptr_t>(pMdl) < 0x10000) pMdl = nullptr;
    }
    LazyModelName nm; nm.mdl = pMdl;
    // ModelRenderInfo_t::entity_index (+0x44) -- one read, shared by no-hats/fb-bones/chams below.
    const int entIdx = pInfoB ? *reinterpret_cast<const int*>(pInfoB + 0x44) : -1;

    // g_noHats used to be the function's own early-out; the transparent viewmodel shares the
    // hook now, so the no-hats work has to carry its own gate or it would run with hats on.
    if (g_noHats && pMdl)
    {
        const unsigned char kind = NoHatsModelKind(pMdl, nm);

        // a model in a cosmetic item dir: never drawn -- except the hat changer's own, so
        // "no hats" + hats = only the hats you picked.
        if ((kind & MK_HAT) && !Hats_IsOurs(entIdx)) return;

        // The player's base model: ModelRenderInfo_t::body (+0x4C) is what this call draws
        // with, so the write is exact per draw and never touches the entity's m_nBody.
        if ((kind & MK_BASE) && pInfoB)
        {
            int* pBody = reinterpret_cast<int*>(const_cast<uint8_t*>(pInfoB) + 0x4C);
            *pBody = CleanBody(entIdx, GetClassFromPlayerModelName(nm.Get()), *pBody);
        }
    }

    // Hat changer: the groups the worn hats hide (the player's own head hat, headphones, ...),
    // on this draw's body only, same as no hats above. Hats_Body matches entity + model itself.
    if (wantHatBody && pInfoB && pMdl) {
        int* pBody = reinterpret_cast<int*>(const_cast<uint8_t*>(pInfoB) + 0x4C);
        *pBody = Hats_Body(entIdx, pMdl, *pBody);
    }

    // Glow pass (aim/chams.cpp Glow_Render) drawing its silhouettes through CBaseEntity::DrawModel:
    // after no-hats (a hidden hat must not glow), before everything else -- the engine's own draw,
    // under the glow material override. Amalgam's RenderHandler does the same.
    if (Glow_Rendering()) {
        o_DrawModelExecute(rcx, pState, pInfo, pBones);
        return;
    }

    // THE BONES, BEFORE THE DRAW: pBones is the array the renderer is about to use, and the
    // pose it goes with is in pInfo -- taking both here is what keeps the gun's rigid ride out
    // of the streak, because the two can never come from different frames. Taken before the
    // chams branch so a chammed viewmodel still feeds frame blending.
    unsigned char vmKind = 0;
    if ((wantFbBones || wantChams || wantVmLowFps) && pInfoB) vmKind = ViewmodelDrawKind(pMdl, nm, entIdx);

    // Motion blur bones BEFORE the low fps hold: the blur measures the live animation, and the
    // gun run (frameblend.cpp FbBuildBones) holds that blur with the gun between steps -- low fps
    // lands on the already-blurred gun. Chams and blend below still see the held pose.
    if (wantFbBones && pInfoB && pBones && vmKind)
        FrameBlend_CollectBones(pState, pInfo, pBones, vmKind == 2);

    if (wantVmLowFps && pState && pBones && pMdl && vmKind)
        pBones = VmLowFps_Bones(pState, pBones, pMdl);

    // Chams + glow (aim/chams.cpp, chams_api.h): every pass goes through o_DrawModelExecute,
    // never this hook, so there is no recursion. A viewmodel keeps the transparent-viewmodel
    // blend: chams reads the live blend as its alpha scale.
    if (wantChams)
    {
        const bool vmBlend = wantVmAlpha && vmKind;
        const float prev = vmBlend ? RenderView_GetBlend() : 1.f;
        if (vmBlend) RenderView_SetBlend(g_vmAlpha);
        bool drew = false;
        __try {
            drew = Chams_DrawModel(o_DrawModelExecute, rcx, pState, pInfo, pBones, entIdx, nm.Get(), vmKind);
        } __except(EXCEPTION_EXECUTE_HANDLER) { drew = false; }
        if (vmBlend) RenderView_SetBlend(prev);
        if (drew) return;
    }

    if (wantVmAlpha && pInfoB && IsViewmodelDraw(pMdl, nm, entIdx))
    {
        // Discovery only -- resolve this model's materials into the cache the first time
        // it is drawn. The tick picks them up and flips them.
        if (wantVmFlat)
            VmFlatAlpha_SetFor(pMdl, pState, nm.Get());

        // Put the blend back afterwards -- the engine keeps r_blend as file-scope state and
        // every model drawn after this one would inherit our alpha otherwise.
        const float prev = RenderView_GetBlend();
        RenderView_SetBlend(g_vmAlpha);
        o_DrawModelExecute(rcx, pState, pInfo, pBones);
        RenderView_SetBlend(prev);
        return;
    }

    o_DrawModelExecute(rcx, pState, pInfo, pBones);
}

void __fastcall h_DrawModelExecute(void* rcx, const void* pState, const void* pInfo, const void* pBones)
{
    MiscHookGuard _g;
    CrashSpan _s("dme/model");   // includes the engine's own draw: max = one slow model, sum = the frame's models
    h_DrawModelExecute_impl(rcx, pState, pInfo, pBones);
}

// ==== Particle create hooks (block new unusual effects while no-hats on) ==========
CParticlePropCreate_t   o_CParticlePropCreate   = nullptr;
CParticlePropCreatePt_t o_CParticlePropCreatePt = nullptr;

void* __fastcall h_CParticlePropCreate(
    void* pProp, const char* pszParticleName, int iAttachType, const char* pszAttachmentName)
{
    MiscHookGuard _g;
    if (pszParticleName && pProp && g_noUnusual) {
        if (IsUnusualParticleName(pszParticleName))
            return nullptr;
    }
    return o_CParticlePropCreate(pProp, pszParticleName, iAttachType, pszAttachmentName);
}

void* __fastcall h_CParticlePropCreatePt(
    void* pProp, const char* pszParticleName, int iAttachType,
    int iAttachmentPoint, PtVec vecOriginOffset)
{
    MiscHookGuard _g;
    if (pszParticleName && pProp && g_noUnusual) {
        // taunt unusuals come through this overload: Create(name, PATTACH_POINT_FOLLOW,
        // iAttachment, vec3_origin)
        if (IsUnusualParticleName(pszParticleName))
            return nullptr;
    }
    return o_CParticlePropCreatePt(pProp, pszParticleName, iAttachType, iAttachmentPoint, vecOriginOffset);
}

// ==== Weather (CPrecipitation, rain=precipType 0, snow=precipType 1) ================
// IClientEntityList::GetClientNetworkable -- slot 0 (GetClientEntity is slot 3).
static void* GetClientNetworkable(int idx)
{
    return g_entityList ? VCall<void*>(g_entityList, 0, idx) : nullptr;
}

// g_offMins / g_offMaxs are netvars.cpp's (0 = unresolved, -1 = not found, >0 = offset),
// so every test here is against 0, not -1. They were a second definition of the same
// symbol before, folded by /Gw, which left this block's sentinel a coin flip.
static void Weather_FindOffsets()
{
    if (s_offPrecipType > 0 && g_offMins > 0 && g_offMaxs > 0) return;

    if (s_offPrecipType < 0) {
        RecvTable* t = FindNetworkTable("DT_Precipitation");
        if (!t && s_precipClass)
            t = reinterpret_cast<ClientClass*>(s_precipClass)->m_pRecvTable;
        if (t)
            s_offPrecipType = FindPropOffset(t, "m_nPrecipType");
    }
    if (g_offMins <= 0 || g_offMaxs <= 0) {
        RecvTable* t = FindNetworkTable("DT_BaseEntity");
        if (t) {
            g_offMins = FindPropOffset(t, "m_vecMins");
            g_offMaxs = FindPropOffset(t, "m_vecMaxs");
        }
    }
}

// True only for an entity WE created with the client class's CreateFn. Release() destroys, so it
// must never run on a precipitation entity the map itself owned and we merely adopted.
static bool s_precipOurs = false;

// The precipType Precache() actually LATCHED into the live entity (material + ramp + speed),
// -1 = nothing latched / not ours. This is NOT s_lastAppliedMode: parking for "off" leaves the
// latch alone, so switching back on to the SAME mode needs no new entity. Only a real rain<->snow
// flip does. See Precip_Park / Precip_TearDown.
static int s_precipLatchedType = -1;

// IClientNetworkable update order: type -> PreDataUpdate -> OnPreDataChanged ->
// bounds -> OnDataChanged -> PostDataUpdate
__declspec(noinline) static void Weather_UpdateChain(void* pNet, void* pEnt,
    int precipType, bool bSetBounds)
{
    if (!pNet || !pEnt) return;
    __try {
        void** vt = *reinterpret_cast<void***>(pNet);
        using Fn = void(__fastcall*)(void*, int);

        if (s_offPrecipType > 0)
            *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pEnt) + s_offPrecipType)
                = precipType;

        ((Fn)vt[6])(pNet, 0); // PreDataUpdate
        ((Fn)vt[4])(pNet, 0); // OnPreDataChanged

        if (g_offMins > 0 && g_offMaxs > 0) {
            float* mins = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pEnt) + g_offMins);
            float* maxs = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pEnt) + g_offMaxs);
            if (bSetBounds) {
                mins[0] = mins[1] = mins[2] = -8192.f;
                maxs[0] = maxs[1] = maxs[2] =  8192.f;
            } else {
                memset(mins, 0, 12); memset(maxs, 0, 12);
            }
        }

        ((Fn)vt[5])(pNet, 0); // OnDataChanged
        ((Fn)vt[7])(pNet, 0); // PostDataUpdate
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        s_precipEntity = s_precipNetworkable = nullptr;
        s_lastAppliedMode = -99;
        s_precipOurs      = false; // whatever is at the slot is not a thing we may destroy now
    }
}

// C_Precipitation::Render draws EVERY live particle in one mb.Begin(MATERIAL_QUADS, Count()), and
// one dynamic mesh holds INDEX_BUFFER_SIZE 32768 indices = 5461 quads (imesh.h). Live count =
// emit rate (radius^2 * m_flDensity 0.001, c_effects.cpp EmitParticles) * fall time (spawn at
// player +512, dies on entering solid, so ~512u+ / r_rainspeed). Default snow (1500u, 80u/s)
// is ~16k flakes -- 3x over, the crash. Rain at 600u/s is ~2k. 768u = 512 + slack for slopes
// and displacements GetPointContents falls through; 5000 keeps headroom under 5461.
// ponytail: estimate, not a count -- a deep pit under the player can still outlive it.
static float Precip_RadiusCap(float radius, float speed)
{
    constexpr float kMaxLive = 5000.f, kFall = 768.f, kDensity = 0.001f;
    return std::min(radius, sqrtf(kMaxLive * std::max(speed, 1.f) / (kDensity * kFall)));
}

static void Precip_ApplyConvars()
{
    if (!g_cvar) return;
    // CachedCVar re-resolves by itself when g_cvar changes, which is what the hand-rolled
    // s_lastCvar reset used to do -- and it also stops re-walking the command list for a
    // name this build does not have.
    static CachedCVar cc_alpha, cc_speed, cc_width, cc_length, cc_radius, cc_sidevel;
    ConVar* cv_alpha   = cc_alpha.Get("r_rainalpha");
    ConVar* cv_speed   = cc_speed.Get("r_rainspeed");
    ConVar* cv_width   = cc_width.Get("r_rainwidth");
    ConVar* cv_length  = cc_length.Get("r_rainlength");
    ConVar* cv_radius  = cc_radius.Get("r_RainRadius");
    ConVar* cv_sidevel = cc_sidevel.Get("r_RainSideVel");

    if (g_weatherMode == 1) {
        if (cv_alpha)   cv_alpha->ForceFloat(g_rainAlpha);
        if (cv_speed)   cv_speed->ForceFloat(g_rainSpeed);
        if (cv_width)   cv_width->ForceFloat(g_rainWidth);
        if (cv_length)  cv_length->ForceFloat(g_rainLength);
        if (cv_radius)  cv_radius->ForceFloat(Precip_RadiusCap(g_rainRadius, g_rainSpeed));
        if (cv_sidevel) cv_sidevel->ForceFloat(g_rainSideVel);
    } else if (g_weatherMode == 2) {
        if (cv_alpha)   cv_alpha->ForceFloat(g_snowAlpha);
        if (cv_speed)   cv_speed->ForceFloat(g_snowSpeed);
        if (cv_width)   cv_width->ForceFloat(g_snowWidth);
        // r_rainlength used to be forced to 0 here for "round flakes". It is not a streak switch:
        // RenderParticle does delta *= GetLength() * m_Ramp and hands the result to Tracer_Draw,
        // which builds the quad from cross(delta, start). A zero delta makes that cross zero,
        // Tracer_ComputeVerts bails on sqLength < 1e-3, and EVERY flake is dropped before the
        // mesh -- snow simulated fine and drew nothing at any slider setting. Short, not zero.
        if (cv_length)  cv_length->ForceFloat(g_snowLength);
        if (cv_radius)  cv_radius->ForceFloat(Precip_RadiusCap(g_snowRadius * std::max(0.1f, g_snowDensity), g_snowSpeed));
        if (cv_sidevel) cv_sidevel->ForceFloat(g_snowSideVel);
    }
}

// C_Precipitation keeps its material in the ENTITY -- `IMaterial* m_MatHandle` -- and Precache()
// is gated on `if (!m_MatHandle)`. That same call is what picks m_Speed and m_InitialRamp off
// m_nPrecipType, so once it has run the entity is locked to that mode's sprite and ramp and
// flipping the netvar afterwards changes nothing visual. There is no client.dll static to
// invalidate (the 16-bit handle pattern scan that used to live here was hunting a symbol this
// build does not have -- it is a member, not a global), so a mode change destroys the entity and
// the next full tick builds a fresh one with the right type already set.
static void Precip_Release(void* pNet)
{
    // s_precipOurs is dropped on EVERY path, including the two early-outs. It used to survive
    // a null pNet -- which is exactly what a level change hands us, because the engine has
    // already destroyed our slot-2047 entity by then. The flag then stayed true into the next
    // map, where Rain_Tick's adopt branch would pick up whatever entity that map put at 2047
    // and a later rain<->snow flip would call Release() -- `delete this` -- on an entity the
    // MAP owns. That is the one thing the comment on this flag forbids.
    const bool bOurs = s_precipOurs;
    s_precipOurs = false;
    if (!pNet || !bOurs) return;
    __try {
        void** vt = *reinterpret_cast<void***>(pNet);
        using Fn = void(__fastcall*)(void*);
        ((Fn)vt[1])(pNet); // IClientNetworkable::Release
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// PARK -- the "off" path, and the entity stays ALIVE. Bounds to zero: ComputeEmissionArea
// intersects the entity box with the box around the player, so a zero box either fails the
// cull or yields size 0 and EmitParticles makes nothing.
// DO NOT make "off" tear down instead. That was `delete this` on a live engine entity from
// inside h_Present; the __except here swallowed the AV and the game died later, on something
// still holding the freed entity. Off needs no fresh entity -- Precache latches on the type,
// and the type is not changing.
void Precip_Park(int kSlot)
{
    void* pEnt = GetClientEntity(kSlot);
    void* pNet = GetClientNetworkable(kSlot);
    // Keep the latched type: nothing about the entity's identity changes, only its bounds.
    int keepType = (s_precipLatchedType >= 0) ? s_precipLatchedType : 0;
    if (pEnt && pNet) Weather_UpdateChain(pNet, pEnt, keepType, false);
}

// "Is there a precipitation entity to tear down?" -- the one thing eject needs to know, so
// the two pointers behind it stay this file's.
bool Precip_IsLive() { return s_precipEntity != nullptr || s_precipNetworkable != nullptr; }

// DESTROY -- only for a real rain<->snow flip, and for eject. No update chain first: PreDataUpdate
// with DATA_UPDATE_CREATED re-runs Spawn(), re-queues a g_DataChangedEvents entry keyed on
// &m_DataChangeEventRef and re-adds the entity to the leaf system, all on an object that is freed
// two lines later. Release() is the whole job.
void Precip_TearDown(int kSlot)
{
    void* pNet = GetClientNetworkable(kSlot);
    Precip_Release(pNet);
    s_precipEntity      = nullptr;
    s_precipNetworkable = nullptr;
    s_precipLatchedType = -1;
}

// A level change destroys every client entity, ours included, and leaves this file holding
// three stale pointers and an ownership flag about an object that no longer exists. Nothing
// used to clear them -- NoHats_FlushModelCache covers the model/material memos and not this.
// Called from the in-game edge in render.cpp's slow tick. NO Release() here: the entity is
// already gone, and calling through a dangling networkable is the crash this prevents.
void Precip_OnLevelChange()
{
    s_precipEntity      = nullptr;
    s_precipNetworkable = nullptr;
    s_precipOurs        = false;   // whatever lands at slot 2047 next is not ours to destroy
    s_precipLatchedType = -1;
    s_lastAppliedMode   = -99;     // force a full re-apply against the new map's entity
}

// -- Snow sprite: FindMaterial redirect ---------------------------------------------
// The material lives in the ENTITY (IMaterial* m_MatHandle), so there is nothing global to
// reset -- Precip_Release above is what makes Precache() re-run. This only redirects the
// lookup for engine builds whose Precache asks for the rain sprite in snow mode.
FindMaterial_t o_FindMaterial = nullptr;


void* __fastcall h_FindMaterial(
    void* rcx, const char* pName, const char* pTextureGroup, bool bComplain, const char* pComplainPrefix)
{
    MiscHookGuard _g;
    // Snow mode: swap rain streak sprite for round soft flake
    if (!g_eject && g_weatherEnable && g_weatherMode == 2 && pName)
    {
        if (strcmp(pName, "particle/rain") == 0 || strcmp(pName, "particles/rain") == 0)
            pName = "particle/snow";
    }
    void* pMat = o_FindMaterial(rcx, pName, pTextureGroup, bComplain, pComplainPrefix);
    WorldFx2_OnFindMaterial(pMat, pTextureGroup, pName);   // world/prop/weather tint
    return pMat;
}

// -- Sound ESP -----------------------------------------------------------------
// Overlays only the leading fields of the real engine EmitSound_t (Amalgam's
// Hooks/CSoundEmitterSystem_EmitSound.cpp has the full struct: int m_nChannel then
// char const* m_pSoundName) -- channel(4)+pad(4)+pSoundName(8) is all this hook reads, so a
// partial struct is safe: nothing here takes its sizeof or copies past m_pSoundName.
// m_flVolume follows m_pSoundName (source-sdk-2013 shareddefs.h EmitSound_t: int, pad, ptr, float).
struct EmitSoundPartial_t { int m_nChannel; const char* m_pSoundName; float m_flVolume; };

EmitSound_t o_EmitSound = nullptr;

static void __fastcall h_EmitSound_impl(void* rcx, void* filter, int entindex, const void* epRaw)
{
    // 10-31: sound chams (aim/chams.cpp) stamps the same peek; gated on its own list being set
    // esp "only on sound" and the sound arrows read the same stamps (botox sound_esp is one store)
    const bool wantChamsSound = entindex <= 64 && (g_chamsEnable && g_chamsSoundCfg[0]
        || g_espEnable && g_espSoundOnly || g_espEnable && g_espOofArrows && g_espOofType != 0);
    if (!g_eject && (g_espSoundPings || wantChamsSound) && entindex > 0 && epRaw && g_offOrigin > 0)
    {
        __try {
            auto* ep = reinterpret_cast<const EmitSoundPartial_t*>(epRaw);
            if (ep->m_pSoundName) {
                void* pEnt = GetClientEntity(entindex);
                if (pEnt) {
                    const uintptr_t addr = reinterpret_cast<uintptr_t>(pEnt) + g_offOrigin;
                    if (addr >= 0x10000) {
                        const float* o = reinterpret_cast<const float*>(addr);
                        if (g_espSoundPings) EspFx_PushSoundPing(o[0], o[1], o[2]);
                        if (wantChamsSound)  Chams_OnSound(entindex, ep->m_flVolume, o);
                    }
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    // Never drop a sound -- always calls through, SND_STOP included. This hook only peeks.
    o_EmitSound(rcx, filter, entindex, epRaw);
}

void __fastcall h_EmitSound(void* rcx, void* filter, int entindex, const void* epRaw)
{
    MiscHookGuard _g;
    h_EmitSound_impl(rcx, filter, entindex, epRaw);
}

// Game thread: skins.cpp h_FrameStageNotify stage 5, BEFORE the original (hats' spot -- its
// OnRenderStart delivers the OnDataChanged our update chain queues). It ran from h_Present
// before, i.e. the RENDER thread under mat_queue_mode 2, so the rain->snow flip's Release()
// freed the entity while the main thread was simulating it: "snow crashes the game".
void Rain_Tick()
{
    constexpr int kSlot = 2047;
    if (!g_entityList) return;
    // Eject: Release here, on this thread. thread.cpp waits for Precip_IsLive() to drop.
    if (g_eject) { if (Precip_IsLive()) Precip_TearDown(kSlot); return; }
    // Out of game the engine owns the teardown (level change / disconnect deletes every client
    // entity, ours too). Forget it, never Release it -- and never CreateFn in the main menu.
    if (!IsInGame()) { if (Precip_IsLive() || s_lastAppliedMode != -99) Precip_OnLevelChange(); return; }

    // The checkbox is the on/off; the combo shows "rain" for anything that is not snow. A fresh
    // config (mode 0) or one saved by the old combo binding (0 = "rain" picked) means rain.
    if (g_weatherEnable && g_weatherMode != 1 && g_weatherMode != 2) g_weatherMode = 1;

    if (!g_weatherEnable || g_weatherMode == 0) {
        if (s_lastAppliedMode == 0) return;
        // -99 (just spawned, never applied) used to fall through BOTH arms here: no teardown, and
        // s_lastAppliedMode went to 0, so the entity was orphaned live at the slot. Park covers it.
        Precip_Park(kSlot);
        s_lastAppliedMode = 0;
        return;
    }

    // Throttle entity management to every 6 frames; convar pushes stay per-frame.
    static int s_tickSkip = 0;
    bool bFullTick = (++s_tickSkip >= 6);
    if (bFullTick) s_tickSkip = 0;

    if (!bFullTick) {
        if (s_lastAppliedMode == g_weatherMode)
            Precip_ApplyConvars();
        return;
    }

    if (!s_precipClass) {
        for (auto* p = GetAllClasses(); p; p = p->m_pNext) {
            if (p->m_pNetworkName && strcmp(p->m_pNetworkName, "CPrecipitation") == 0) {
                s_precipClass = p;
                break;
            }
        }
        if (!s_precipClass) return;
    }
    Weather_FindOffsets();
    if (s_offPrecipType < 0 || g_offMins <= 0 || g_offMaxs <= 0) return;

    void* pEnt = GetClientEntity(kSlot);
    if (!pEnt) {
        auto* cls = reinterpret_cast<ClientClass*>(s_precipClass);
        if (!cls->m_pCreateFn) return;
        __try {
            using CreateFn = void*(__cdecl*)(int, int);
            s_precipNetworkable = reinterpret_cast<CreateFn>(cls->m_pCreateFn)(kSlot, 0);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            s_precipNetworkable = nullptr; return;
        }
        pEnt = GetClientEntity(kSlot);
        if (!pEnt) { s_precipNetworkable = nullptr; return; }
        s_precipEntity      = pEnt;
        s_precipNetworkable = GetClientNetworkable(kSlot);
        s_precipOurs        = true;
        s_precipLatchedType = -1;   // Precache has not run yet -- nothing latched
        s_lastAppliedMode   = -99;
    } else {
        // ADOPTING an entity that was already at the slot. It is not ours, so it may never be
        // Released -- and this branch is reached after a level change too, where the flag from
        // the previous map's entity would otherwise still read true.
        if (s_precipEntity != pEnt) {
            s_precipEntity      = pEnt;
            s_precipNetworkable = nullptr;
            s_precipOurs        = false;
            s_precipLatchedType = -1;
        }
        if (!s_precipNetworkable)
            s_precipNetworkable = GetClientNetworkable(kSlot);
    }

    Precip_ApplyConvars();

    const int precipType = (g_weatherMode == 2) ? 1 : 0;

    // rain <-> snow on a LIVE entity: Precache() has latched the old type's material and ramp
    // and is gated on a null m_MatHandle, so re-poking m_nPrecipType leaves the wrong sprite up
    // forever. Replace the entity; the next full tick spawns one whose type is set first.
    // The test is the LATCHED type, NOT s_lastAppliedMode -- parking for "off" sets that to 0
    // without touching the entity, so testing it made snow -> off -> snow look like a mode
    // change and freed a good entity, i.e. the crash above one toggle later. -1 = nothing
    // latched (freshly spawned, or the map's own entity, which is not ours to destroy).
    if (s_precipLatchedType >= 0 && s_precipLatchedType != precipType)
    {
        Precip_TearDown(kSlot);
        s_lastAppliedMode = 0;
        return;
    }

    if (s_lastAppliedMode != g_weatherMode)
    {
        Weather_UpdateChain(s_precipNetworkable, s_precipEntity, precipType, true);
        s_lastAppliedMode = g_weatherMode;
        if (s_precipOurs) s_precipLatchedType = precipType;
    }
}

// ==== Remove ragdolls =================================================================
CreateTFRagdoll_t o_CreateTFRagdoll = nullptr;

void __fastcall h_CreateTFRagdoll(void* rcx)
{
    MiscHookGuard _g;
    // set_turn_to_gold is a server attribute, so the client is never told a corpse should be
    // gold. Everything the flag drives IS client side and all of it runs inside this call, so
    // the skinchanger sets m_bGoldRagdoll here, before it.
    //
    // It also runs BEFORE the remove-ragdolls check now. With removeRagdolls on (it is, in
    // C:otox-tf2\default.cfg) the original never ran, so a Saxxy / Golden Wrench / Gold Frying Pan
    // kill produced no corpse and therefore no statue -- which reads exactly like "the gold
    // weapons behave as normal weapons". A gilded corpse is the effect the weapon was equipped
    // for, so that one is kept; every other ragdoll is still skipped.
    const bool bGold = Skins_OnCreateRagdoll(rcx);
    if (g_removeRagdolls && !g_eject && !bGold)
        return; // skip original -> no ragdoll spawns
    o_CreateTFRagdoll(rcx);
}

// ==== Remove prop fade =================================================================
// Field layout mirrors engine CStaticProp (offsets only) -- Vector/QAngle are plain float
// triples w/ no vtable, handle typedefs are all unsigned short, CBaseHandle is one 4B index.
struct CStaticProp_Layout
{
    unsigned char pad[20];
    float m_Origin[3];
    float m_Angles[3];
    void* m_pModel;
    unsigned short m_Partition;
    unsigned short m_ModelInstance;
    unsigned char m_Alpha;
};

CStaticPropMgr_ComputePropOpacity_t o_ComputePropOpacity = nullptr;

void __fastcall h_ComputePropOpacity(void* rcx, void* pProp)
{
    MiscHookGuard _g;
    if (g_removePropFade && !g_eject && pProp)
    {
        ((CStaticProp_Layout*)pProp)->m_Alpha = 255;
        return; // skip original -> no distance-fade opacity applied
    }
    o_ComputePropOpacity(rcx, pProp);
}

// ==== Remove screen effects: swallow Shake/Fade/Rumble user messages ================================
// CHLClient::DispatchUserMessage (VClient slot 36). Return true (msg handled) to drop it -> no screen
// shake, no fullscreen fade/flash, no controller rumble. Enum: Shake=10, Fade=11, Rumble=13 (TF2 x64).
DispatchUserMessage_t o_DispatchUserMessage = nullptr;

bool __fastcall h_DispatchUserMessage(void* rcx, int type, void* msgData)
{
    MiscHookGuard _g;
    ChatTranslator_OnUserMessage(type, msgData);   // reads SayText/SayText2 before the original does
    if (g_removeScreenEffects && !g_eject && (type == 10 || type == 11 || type == 13))
        return true; // swallow -> effect never plays
    return o_DispatchUserMessage(rcx, type, msgData);
}

// CViewRender::PerformScreenOverlay -- fullscreen material overlays (jarate, mad milk, uber, dead
// ringer feign, underwater). Skip original when enabled -> m_ScreenOverlayMaterial never drawn.
PerformScreenOverlay_t o_PerformScreenOverlay = nullptr;

void __fastcall h_PerformScreenOverlay(void* rcx, int x, int y, int w, int h)
{
    MiscHookGuard _g;
    if (g_removeScreenEffects && !g_eject)
        return; // don't draw overlay
    o_PerformScreenOverlay(rcx, x, y, w, h);
}

// ==== FPS boost: bones optimization ====================================================
// SetupBones dedup: every cosmetic/weapon bonemerge re-triggers the owner's full bone
// setup, so a hatted player gets 3-6 recomputes per frame. First call per player per
// frame runs the original (fills engine m_CachedBoneData), repeats within the same frame
// (same currentTime, no new mask bits) get a cache memcpy instead. Entity itself must be
// the player -- no root walk, never serve owner bones into a cosmetic's buffer.
// rcx = renderable this (entity + 8). m_CachedBoneData = CUtlVector: base @+0, count @+16;
// matrix3x4 = 48 bytes.
SetupBones_t o_SetupBones = nullptr;

static float s_boneTime[65] = {}; // currentTime of last original run per player slot
static int   s_boneMask[65] = {}; // accumulated boneMask covered by cache this frame

// ---- eligibility memo -------------------------------------------------------------------
// The same renderable arrives here 3-6 times per frame (each bonemerged cosmetic and weapon
// re-triggers its owner's setup) and "which player slot is this" is the same answer every
// time -- but it cost three interface calls per repeat, inside the optimisation itself.
// Keyed on the renderable pointer and dropped when currentTime moves, the frame boundary the
// bone cache already trusts. A freed entity whose address is reused inside one frame would
// answer from its previous owner, so the memo is only consulted on an exact frame match.
// THREAD_LOCAL: SetupBones runs on TF2's threaded bone job as well as the render thread, and
// the key/value pair is written unsynchronised. A reader matching a key that was already
// stored against a value that was not yet gets the WRONG player slot -- which is the index
// the lag resolver's per-player track and the bone cache are both keyed by. Per-thread the
// pair can never be torn, and the memo is a one-frame cache anyway.
static thread_local const void* s_boneIdxKey[128]  = {};
static thread_local short       s_boneIdxVal[128]  = {};
static thread_local float       s_boneIdxFrame     = -1.f;
static thread_local int         s_boneLocalIdx     = -1;

// Every C_TFPlayer shares one IClientRenderable vtable, and rcx IS that renderable, so a
// pointer compare rejects everything that is not a player in two loads. That matters because
// SetupBones runs for EVERY animating thing in the scene -- props, ragdolls, gibs, buildings,
// projectiles -- and each of them used to pay the full resolve (entindex + GetClientEntity,
// two interface calls) once per frame just to be told "not a player". Learned from the first
// player that resolves; until then the filter is simply off. It lives in client.dll, so it
// stays valid across maps.
static const void* s_playerRenderVt = nullptr;

static const void* RenderableVTable(void* rcx)
{
    const void* vt = nullptr;
    __try { vt = *reinterpret_cast<const void* const*>(rcx); }
    __except(EXCEPTION_EXECUTE_HANDLER) { vt = nullptr; }
    return vt;
}

// TC2's SetupBones LOD mask promotion was ported here 2026-08-30 and MEASURED DEAD in TF2:
// 0 saved on ~15k LOD calls / 2 s, evict 0 (the table never collided, so that 0 is real).
// The mask histogram says why -- TF2 only ever passes 400 (BONE_USED_BY_VERTEX_LOD0),
// 40000 (BONE_MERGE), 200 (ATTACHMENT), 100 (HITBOX). No coarse-LOD request exists to
// promote, and sdk c_baseanimating.cpp:2924 already does boneMask |= m_iPrevBoneMask, so
// the engine coalesces mask changes itself one frame later. Do not re-add.

static void BonesOpt_NewFrame(float currentTime)
{
    if (s_boneIdxFrame == currentTime) return;
    s_boneIdxFrame = currentTime;
    memset(s_boneIdxKey, 0, sizeof(s_boneIdxKey));
    s_boneLocalIdx = GetLocalPlayerIndex();   // once a frame, not once a bone setup
}

// -1 = not eligible; else player slot index (non-local, entity itself is the player)
static __declspec(noinline) int BonesOpt_PlayerIdxUncached(void* rcx)
{
    int r = -1;
    __try {
        uintptr_t ent = reinterpret_cast<uintptr_t>(rcx) - 8;
        void** ntw = *reinterpret_cast<void***>(ent + 16); // IClientNetworkable vtable
        if (reinterpret_cast<uintptr_t>(ntw) < 0x10000) return -1;
        using EntIndex_t = int(__fastcall*)(void*);
        int idx = ((EntIndex_t)ntw[9])(reinterpret_cast<void*>(ent + 16)); // entindex slot 9
        if (idx < 1 || idx > 64) return -1;
        if (idx == s_boneLocalIdx) return -1;
        if (reinterpret_cast<uintptr_t>(GetClientEntity(idx)) != ent) return -1;
        int cls = *reinterpret_cast<int*>(ent + g_offClass);
        if (cls < 1 || cls > 9) return -1;
        r = idx;
    } __except(EXCEPTION_EXECUTE_HANDLER) { r = -1; }
    return r;
}

static int BonesOpt_PlayerIdx(void* rcx)
{
    // Cheap reject first: not the player renderable vtable => not a player, no memo slot
    // spent on it and no interface call made. The LOCAL player passes this and is rejected
    // by the index test below, same as before.
    if (s_playerRenderVt && RenderableVTable(rcx) != s_playerRenderVt) return -1;

    const size_t i = (reinterpret_cast<uintptr_t>(rcx) >> 4) & 127u;
    if (s_boneIdxKey[i] == rcx) return s_boneIdxVal[i];
    const int idx = BonesOpt_PlayerIdxUncached(rcx);
    if (idx > 0 && !s_playerRenderVt) s_playerRenderVt = RenderableVTable(rcx);
    s_boneIdxKey[i] = rcx;
    s_boneIdxVal[i] = (short)idx;
    return idx;
}

// 0 = cache unusable, 1 = served (return true), 2 = buffer too small (return false)
static __declspec(noinline) int BonesOpt_ServeCache(void* rcx, void* pOut, int nMaxBones)
{
    int r = 0;
    __try {
        uintptr_t vec  = reinterpret_cast<uintptr_t>(rcx) - 8 + g_offCachedBones;
        void*     base = *reinterpret_cast<void**>(vec);
        int       cnt  = *reinterpret_cast<int*>(vec + 16);
        if (!base || cnt <= 0 || cnt > 128) return 0; // empty/garbage -> recompute
        if (pOut) {
            if (nMaxBones >= cnt) memcpy(pOut, base, 48ull * (unsigned)cnt);
            else return 2;
        }
        r = 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { r = 0; }
    return r;
}

// The bones are built from the entity's transform, so the lag resolver's smoothed transform
// has to be in place for the ORIGINAL call and gone again the moment it returns -- __finally
// so it is restored on an exception too. Every path through this hook that can run the
// original goes through here.
static bool LrRunOriginal(void* rcx, void* pOut, int nMaxBones, int boneMask, float currentTime)
{
    bool bWrote = false;
    if (g_lagResolver && !g_eject && g_offClass > 0)
    {
        // rcx is the renderable (entity + 8); the resolver reads/writes entity fields
        bWrote = LagRes_Begin(reinterpret_cast<char*>(rcx) - 8,
                              BonesOpt_PlayerIdx(rcx), currentTime);
    }
    __try { return o_SetupBones(rcx, pOut, nMaxBones, boneMask, currentTime); }
    __finally { if (bWrote) LagRes_End(); }
}

bool __fastcall h_SetupBones(void* rcx, void* pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime)
{
    MiscHookGuard _g;   // hottest hook in the DLL and it was never drained on eject
    if (g_lagResolver && !g_eject && g_netvarsReady && g_offClass > 0)
        BonesOpt_NewFrame(currentTime);   // resolver needs the local index too
    if (g_fpsBoost && !g_eject && g_netvarsReady && g_offClass > 0 && g_offCachedBones > 0)
    {
        BonesOpt_NewFrame(currentTime);   // drops the eligibility memo when the frame moves
        int idx = BonesOpt_PlayerIdx(rcx);
        if (idx > 0)
        {
            bool sameFrame = (s_boneTime[idx] == currentTime);
            bool maskOK    = sameFrame && (boneMask & ~s_boneMask[idx]) == 0;
            if (maskOK)
            {
                int r = BonesOpt_ServeCache(rcx, pBoneToWorldOut, nMaxBones);
                if (r == 1) return true;
                if (r == 2) return false;
            }
            bool ok = LrRunOriginal(rcx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);
            if (ok)
            {
                s_boneTime[idx] = currentTime;
                s_boneMask[idx] = sameFrame ? (s_boneMask[idx] | boneMask) : boneMask;
            }
            return ok;
        }
    }
    return LrRunOriginal(rcx, pBoneToWorldOut, nMaxBones, boneMask, currentTime);
}
