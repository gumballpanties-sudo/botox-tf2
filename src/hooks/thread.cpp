#include "../globals/common.h"
#include "../globals/globals.h"
#include "../hacks/visuals/visuals.h"
#include "../hacks/visuals/chams_api.h"   // h_ForcedMaterialOverride (glow)
#include "../render/render.h"
#include "../hacks/hud/killfeed.h"
#include "../hacks/menu/config.h"
#include "../hacks/movement/movement.h"
#include "../hacks/hud/crosshair.h"
#include "../hacks/network/netregion.h"
#include "../hacks/network/mmfix.h"
#include "../hacks/hud/scoreboard.h"
#include "crashlog.h"
#include "../hacks/screen/colorcorrect.h"
#include "../hacks/screen/bloom.h"
#include "../hacks/screen/screenfx.h"
#include "../hacks/visuals/world_fx.h"
#include "../hacks/visuals/world_fx2.h"
#include "../hacks/visuals/world_fx3.h"   // unit world: eject restore (ragdoll proxy, cvars, modulation)
#include "../hacks/visuals/skins.h"
#include "../hacks/visuals/flipworld.h"
#include "../hacks/hud/mediaplayer.h"
#include "../hacks/misc/misc_overlays.h"   // unit misc: ConsoleTint_Install
#include "../hacks/misc/discord_ex.h"   // unit E: discord rich presence, its own IPC thread
#include "../hacks/misc/game_extras.h"  // unit game: bot names GetPlayerInfo hook + eject restore
#include "../hacks/aim/aim.h"

// ==== sv_pure bypass ============================================================
using CL_CheckForPureServerWhitelist_t = void(__fastcall*)(void** pFilesToReload);
static CL_CheckForPureServerWhitelist_t o_CL_CheckForPureServerWhitelist = nullptr;

// Unconditional, as it was on 2026-09-13 (and in Aletherium / fpsoverlay). A matchmade-
// server gate was added after that and made the bypass do nothing on casual -- removed.
static void __fastcall h_CL_CheckForPureServerWhitelist(void** pFilesToReload)
{
    MiscHookGuard _g;
    // botox sv_pure.cpp: safe mode turns the bypass off (menu shows it unticked, safe_checkbox)
    const bool bypass = g_pureBypass && !g_safeMode;
    DebugLog("pure: whitelist check -- bypass=%d safe=%d -> %s", (int)g_pureBypass, (int)g_safeMode,
             bypass ? "SKIPPED" : "ran");
    if (bypass) return;
    o_CL_CheckForPureServerWhitelist(pFilesToReload);
}

static void* s_presentAddr = nullptr;    // d3d9 Device::Present entry we patched

// Who owns the Present entry? Last hook installed wins it and runs FIRST, so this decides
// whether our h_Present draw lands before obs's capture (recorded) or after it (hidden) --
// and it has flipped between sessions. Read the jump a detour left at the entry rather
// than inferring the answer from a recording.
// SEH leaf: the jump target at addr, or null if there is no jump there.
static __declspec(noinline) const void* JumpTargetAt(const void* addr)
{
    if (!addr) return nullptr;
    const uint8_t* p = (const uint8_t*)addr;
    const void* target = nullptr;
    __try {
        if (p[0] == 0xE9)                                    // rel32 jmp
            target = (const void*)(p + 5 + *(const int32_t*)(p + 1));
        else if (p[0] == 0xFF && p[1] == 0x25)               // jmp [rip+rel32]
            target = *(void* const*)(p + 6 + *(const int32_t*)(p + 2));
    } __except (EXCEPTION_EXECUTE_HANDLER) { target = nullptr; }
    return target;
}

static const char* ModuleNameOf(const void* addr, char* buf, int cb)
{
    HMODULE hm = nullptr;
    if (addr && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCSTR)addr, &hm) && hm)
    {
        char path[MAX_PATH];
        if (GetModuleFileNameA(hm, path, MAX_PATH))
        {
            const char* b = strrchr(path, '\\');
            lstrcpynA(buf, b ? b + 1 : path, cb);
            return buf;
        }
    }
    lstrcpynA(buf, "(none)", cb);
    return buf;
}

// Who is ABOVE and BELOW us in the Present chain. Owning the entry is not the question --
// what matters is whether obs's capture runs before our draw or after it:
//   obs ABOVE us (owns the entry)         -> it captures, then calls us  => menu hidden
//   obs BELOW us (we own it, or it hooked -> we draw, then it captures   => menu RECORDED
//                 first and our trampoline leads to it)
// Only the module names separate those: "OTHER" alone cannot, because the steam overlay
// hooks Present too and reads the same.
static void LogPresentHookOwner()
{
    if (!s_presentAddr) { DebugLog("present entry: address unknown"); return; }
    char m[64];

    const void* target = JumpTargetAt(s_presentAddr);
    const bool ours = (target == (const void*)&h_Present);
    DebugLog("present entry: owner=%s target=%p (%s)", ours ? "BOTOX" : "OTHER",
             target, ModuleNameOf(target, m, sizeof(m)));

    // Our trampoline holds whatever bytes were at the entry when WE hooked. If a detour was
    // already there, those bytes are its jump -- so this names the hooker directly BELOW us.
    const void* below = JumpTargetAt((const void*)o_Present);
    DebugLog("below botox: %p (%s)", below, ModuleNameOf(below, m, sizeof(m)));

    static const wchar_t* kHookers[] = { L"graphics-hook64.dll", L"gameoverlayrenderer64.dll",
                                         L"nvspcap64.dll", nullptr };
    for (int i = 0; kHookers[i]; ++i)
        DebugLog("  capture module %S: %s", kHookers[i],
                 GetModuleHandleW(kHookers[i]) ? "loaded" : "not loaded");
}

// ==== Client init gate =================================================================
// Every install below runs ONCE. Loaded at game start, client.dll is not even mapped for the
// first several seconds, and once it is, CHLClient::Init still has to create what the installs
// read: g_pClientMode (screenfx, flipworld), `view` (flipworld), convars, the HUD. A miss
// there is permanent, so wait for `view` -- cdll_client_int.cpp sets it after ClientMode,
// gHUD and IGameSystem init, just before input/vgui panels/user messages.
// Same mov rcx,[rip+disp32] site flipworld reads g_pViewRender from.
static constexpr const char* k_viewRenderSig =
    "48 8B 0D ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 48 8B 03 48 8D 54 24";

// SEH leaf: the g_pViewRender slot inside client.dll, or null.
static __declspec(noinline) void** ViewRenderSlot(HMODULE hClient)
{
    uint8_t* at = PatternScan(hClient, k_viewRenderSig);
    if (!at) return nullptr;
    void** slot = nullptr;
    __try { slot = reinterpret_cast<void**>(at + 7 + *reinterpret_cast<const int32_t*>(at + 3)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { slot = nullptr; }
    return slot;
}

static __declspec(noinline) bool SlotSet(void** slot)
{
    bool set = false;
    __try { set = reinterpret_cast<uintptr_t>(*slot) >= 0x10000; }
    __except (EXCEPTION_EXECUTE_HANDLER) { set = false; }
    return set;
}

// Sleep in 50ms steps so eject never waits on this.
static bool SleepUnlessEject(int ms)
{
    for (int t = 0; t < ms && !g_eject; t += 50) Sleep(50);
    return !g_eject;
}

// False = ejected while waiting.
static bool WaitForClientInit()
{
    DWORD start = GetTickCount();
    bool waited = false;
    HMODULE hClient = nullptr;
    while (!(hClient = GetModuleHandleA("client.dll"))) {
        waited = true;
        if (!SleepUnlessEject(250)) return false;
    }

    // No slot = the sig broke on a game update. Do not hang the DLL on it: a flat grace
    // period covers Init on any sane machine, and only when we actually saw the load.
    void** slot = ViewRenderSlot(hClient);
    if (!slot) {
        DebugLog("poll_thread: g_pViewRender sig NOT FOUND -- %s",
                 waited ? "flat 10s wait for client init" : "assuming client already up");
        return waited ? SleepUnlessEject(10000) : !g_eject;
    }
    while (!SlotSet(slot)) {
        waited = true;
        if (!SleepUnlessEject(100)) return false;
    }
    // `view` lands a little before the end of Init (input, vgui panels, user messages follow).
    if (waited && !SleepUnlessEject(2000)) return false;
    DebugLog("poll_thread: client init %s (%lu ms)", waited ? "waited for" : "already done",
             (unsigned long)(GetTickCount() - start));
    return true;
}

// ==== Poll thread: interface + hook install (runs once) ==============================
static void PollThread()
{
    // A flat opening Sleep(3000) outlived eject's 2000ms join, so an eject in the first
    // few seconds left this thread installing hooks straight into MH_Uninitialize.
    for (int i = 0; i < 60 && !g_eject; ++i) Sleep(50);
    if (g_eject || !WaitForClientInit()) { DebugLog("poll_thread: ejected before install"); return; }

    // vgui::IPanel::PaintTraverse -- slot 41, counted off public/vgui/IPanel.h including the
    // destructor slot IBaseInterface contributes. This is where the crosshair draws: inside
    // the game's own UI pass, so it is in the backbuffer before anyone's Present hook runs.
    // Here and not in MainThread: at game start d3d9.dll can load before vgui2.dll does.
    if (void* panels = VGui_GetPanelInterface())
        InstallVTableHook(panels, 41, "vgui: PaintTraverse", (void*)&h_PaintTraverse,
                          (void**)&o_PaintTraverse);
    else DebugLog("vgui: VGUI_Panel009 not found -- crosshair falls back to EndScene");
    ConsoleTint_Install();   // unit misc: botox console color (ISurface::DrawSetColor)

    static const char* const kEngineNames[]  = {"VEngineClient014","VEngineClient013","VEngineClient015",nullptr};
    static const char* const kCvarNames[]    = {"VEngineCvar004","VEngineCvar007","VEngineCvar003",nullptr};
    static const char* const kModelInfoNames[]= {"VModelInfoClient006","VModelInfoClient007",nullptr};
    static const char* const kRenderViewNames[]={"VEngineRenderView014","VEngineRenderView013",nullptr};
    static const char* const kEntListNames[] = {"VClientEntityList003","VClientEntityList002",nullptr};

    if (!g_engine) g_engine = (IEngineClient*)GetInterfaceAny("engine.dll", kEngineNames);
    if (!g_cvar) {
        g_cvar = (ICvar*)GetInterfaceAny("vstdlib.dll", kCvarNames);
        if (!g_cvar) g_cvar = (ICvar*)GetInterfaceAny("tier0.dll", kCvarNames);
    }
    if (!g_matSystem)  g_matSystem  = GetInterface("materialsystem.dll", "VMaterialSystem082");
    if (!g_modelRender)g_modelRender= GetInterface("engine.dll", "VEngineModel016");
    if (!g_modelInfo)  g_modelInfo  = GetInterfaceAny("engine.dll", kModelInfoNames);
    // IVRenderView -- SetBlend/GetBlend for the transparent viewmodel
    if (!g_renderView) g_renderView = GetInterfaceAny("engine.dll", kRenderViewNames);
    if (!g_entityList) g_entityList = (IClientEntityList*)GetInterfaceAny("client.dll", kEntListNames);

    InstallVTableHook(g_modelRender, 19, "nohats: DrawModelExecute",
                      (void*)h_DrawModelExecute, (void**)&o_DrawModelExecute);
    // IVModelRender::ForcedMaterialOverride slot 1 (Amalgam IVModelRender.h, chams.cpp kMdlForcedMaterialOverride)
    InstallVTableHook(g_modelRender, 1, "glow: ForcedMaterialOverride",
                      (void*)h_ForcedMaterialOverride, (void**)&o_ForcedMaterialOverride);
    InstallVTableHook(g_matSystem,   71, "weather: FindMaterial",
                      (void*)h_FindMaterial, (void**)&o_FindMaterial);

    {
        HMODULE hClient = GetModuleHandleA("client.dll");
        if (hClient) {
            // Sigs are Aletherium's where noted, each re-scanned against tf/bin/x64/client.dll.
            struct ClientHook { const char* tag; const char* sig; void* hook; void** orig; };
            static const ClientHook kClientHooks[] = {
                { "nohats: CParticleProperty::Create",
                  "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC ? 48 8B 59 ? 49 8B F1",
                  (void*)h_CParticlePropCreate,   (void**)&o_CParticlePropCreate },
                { "nohats: CParticleProperty::Create(pt)",
                  "44 89 4C 24 ? 44 89 44 24 ? 53",
                  (void*)h_CParticlePropCreatePt, (void**)&o_CParticlePropCreatePt },
                { "ragdolls: CreateTFRagdoll",
                  "48 89 4C 24 ? 55 53 56 57 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 8B 91",
                  (void*)h_CreateTFRagdoll,       (void**)&o_CreateTFRagdoll },
                { "fpsboost: SetupBones",
                  "48 8B C4 44 89 40 ? 48 89 50 ? 55 53",
                  (void*)h_SetupBones,            (void**)&o_SetupBones },
                { "removals: PerformScreenOverlay",
                  "4C 8B DC 49 89 5B ? 89 54 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 81 EC",
                  (void*)h_PerformScreenOverlay,  (void**)&o_PerformScreenOverlay },
                { "nopush: AvoidPlayers",
                  "48 89 54 24 ? 55 41 56",
                  (void*)h_AvoidPlayers,          (void**)&o_AvoidPlayers },
                { "amby: GetWeaponCrosshairScale",
                  "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B F2 48 8B F9 E8 ? ? ? ? 48 8B D8 48 85 C0 0F 84",
                  (void*)h_GetWeaponCrosshairScale, (void**)&o_GetWeaponCrosshairScale },
                { "soundesp: CSoundEmitterSystem::EmitSound",   // Amalgam sig, verified
                  "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 41 56 48 81 EC ? ? ? ? 49 8B D9",
                  (void*)h_EmitSound,             (void**)&o_EmitSound },
            };
            for (const auto& h : kClientHooks)
                InstallPatternHook(hClient, h.tag, h.sig, h.hook, h.orig);

            // Ported aimbot subsystem. Brought up BEFORE dih's own CreateMove hook: when it
            // succeeds it OWNS that hook (CHLClient_CreateMove, aim/hooks.cpp) and calls
            // Movement_Run itself -- MinHook allows one hook per target, and that one needs
            // the send-packet pointer off its own stack frame.
            Aim_Init();

            // CHLClient::CreateMove (VClient slot 21) -- bhop. Skipped when the aim half is
            // live (it installs the same hook and calls Movement_Run from it), but the rest
            // of the block -- input offsets, the movement port's interfaces/sigs,
            // DispatchUserMessage -- has to run either way.
            // o_CreateMove stays null when the aim half owns the hook, so it cannot serve as
            // this block's "already done" flag: ResolveInput/Movement_Install would rerun
            // every poll iteration.
            static bool s_bClientHooksDone = false;
            if (!s_bClientHooksDone) {
                s_bClientHooksDone = true;
                static const char* const kVClientNames[] = {"VClient018","VClient017","VClient016",nullptr};
                if (void* pClient = GetInterfaceAny("client.dll", kVClientNames)) {
                    if (Aim_Active())
                        DebugLog("bhop: CreateMove owned by aim hook, Movement_Run called from there");
                    else
                        InstallVTableHook(pClient, 21, "bhop: CreateMove",
                                          (void*)h_CreateMove, (void**)&o_CreateMove);
                    // CHLClient::DispatchUserMessage (slot 36) -- remove screen effects
                    InstallVTableHook(pClient, 36, "removals: DispatchUserMessage",
                                      (void*)h_DispatchUserMessage, (void**)&o_DispatchUserMessage);
                }
                ResolveInput();
                Movement_Install(); // trace/prediction interfaces + sigs (movement port)
            }

            // C_BaseViewModel::CalcViewModelView -- viewmodel offset
            static const char* const kCalcVmvSigs[] = {
                "48 89 74 24 ? 55 41 56 41 57 48 8D AC 24",
                "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 49 8B F0 48 8B EA 48 8B F9 33 DB",
                nullptr
            };
            InstallPatternHookAny(hClient, "viewmodel: CalcViewModelView", kCalcVmvSigs,
                                  (void*)h_CalcViewModelView, (void**)&o_CalcViewModelView);
            VmCsgo_Install(hClient);   // CalcViewModelBobHelper -- CS:GO viewmodel bob
            ScopeCsgo_Install(hClient); // CHudScope::Paint + sniper dot -- CS:GO scope

            // ISteamNetworkingUtils::GetPingToDataCenter -- region selector
            Region_Install();

            // CTFGCClientSystem sigs -- stuck "match found" unstick (needs the
            // singleton Region_Install just resolved)
            MMFix_Install();

            // IMatSystemSurface::DrawColoredText (slot 162) -- net graph text color
            NetGraph_Install();

            // UpdatePlayerList / CTFPlayerPanel::GetTeam sigs -- reveal scoreboard
            Scoreboard_Install();

            // IVEngineClient::GetPlayerInfo (slot 8) -- unit game: bot names
            GameExtras_Install();

            // IClientMode::DoPostScreenSpaceEffects (slot 39) -- the view setup the depth
            // passes need, and the thread the game's own screen-space slot runs on.
            ScreenFx_Install();

            // IClientMode::OverrideView (slot 16) -- unit D: world fov + third person
            WorldFx_InstallViewHook();

            // IClientMode::OverrideMouseInput (slot 20) -- edge bug mouse steer + mouse lock
            EdgeBug_InstallMouseHook();

            // CBasePlayer::CalcView, pattern-hooked -- unit D2: remove visual recoil
            WorldFx2_InstallHooks();

            // Flip world: WorldToScreenMatrix (HUD), ClientModeShared::CreateMove (A/D + hand),
            // CViewRender::LevelInit (its RT). The mirror itself rides screenfx's two hooks above.
            FlipWorld_Install();

            // Skinchanger: engine FrameStageNotify + CBaseAnimating::SetSequence. Installed
            // after the entity list and IVModelInfo are resolved above -- both are read from
            // inside the FSN hook the moment it goes live.
            Skins_Install();
        }
    }

    if (HMODULE engine = GetModuleHandleA("engine.dll"))
    {
        struct EngineHook { const char* tag; const char* sig; void* hook; void** orig; };
        static const EngineHook kEngineHooks[] = {
            { "skybox: R_DrawSkyBox",
              "48 8B C4 55 53 41 54 41 55",
              (void*)h_R_DrawSkyBox,      (void**)&o_R_DrawSkyBox },
            { "bypass: sv_pure",          // CL_CheckForPureServerWhitelist
              "40 56 48 83 EC ? 83 3D ? ? ? ? ? 48 8B F1 0F 8E",
              (void*)h_CL_CheckForPureServerWhitelist, (void**)&o_CL_CheckForPureServerWhitelist },
            { "removals: ComputePropOpacity",   // CStaticPropMgr, Aletherium sig
              "48 89 5C 24 ? 48 89 7C 24 ? 41 54 41 56 41 57 48 83 EC ? 48 8B 05",
              (void*)h_ComputePropOpacity, (void**)&o_ComputePropOpacity },
        };
        for (const auto& h : kEngineHooks)
            InstallPatternHook(engine, h.tag, h.sig, h.hook, h.orig);

        // IGameEventManager2 is not exposed through GetInterface's module table -- ask
        // engine.dll's CreateInterface directly.
        if (!g_gameEventManager) {
            using CI_t = void*(*)(const char*, int*);
            if (auto ci = (CI_t)GetProcAddress(engine, "CreateInterface")) {
                static const char* const kGemNames[] = {"GAMEEVENTSMANAGER002","GAMEEVENTSMANAGER001",nullptr};
                for (int i = 0; kGemNames[i] && !g_gameEventManager; ++i)
                    g_gameEventManager = ci(kGemNames[i], nullptr);
            }
        }
    }

    if (g_cvar && !g_cv_sv_skyname)
        g_cv_sv_skyname = g_cvar->FindVarWalk("sv_skyname");

    // IGameEventManager2::FireEvent (slot 8) -- killfeed filter
    InstallVTableHook(g_gameEventManager, 8, "killfeed: FireEvent",
                      (void*)h_FireEvent, (void**)&o_FireEvent);

    DebugLog("poll_thread: hooks installed");
    ConsoleColorPrint((uint8_t)(g_consoleColor[0]*255.f), (uint8_t)(g_consoleColor[1]*255.f),
                       (uint8_t)(g_consoleColor[2]*255.f), "botox on\n");
}

// ==== Main thread ==============================================================================
static void MainThread()
{
    // No timeout on either wait. Loaded at game start (launcher injection), a cold boot with
    // shader/cache work can take far longer than the old 30s + 15s, after which this thread
    // quietly exited and the DLL did nothing for the session.
    while (!g_eject && !(GetModuleHandleW(L"d3d9.dll") && GetModuleHandleW(L"engine.dll")))
        Sleep(250);
    Sleep(1000);

    void* vt[100]{};
    for (int i = 0; !g_eject && !(GetDX9VTable(vt, 100) && vt[17]); ++i)
        Sleep(i < 30 ? 500 : 5000);   // a dummy device every 500ms forever is not free
    if (!vt[17] || g_eject) { ExitThread(0); return; }

    MH_Initialize();

    if (HMODULE hUser32 = GetModuleHandleA("user32.dll"))
    {
        void* pSCP = GetProcAddress(hUser32, "SetCursorPos");
        if (pSCP) MH_CreateHook(pSCP, h_SetCursorPos, (void**)&o_SetCursorPos);
    }

    s_presentAddr = vt[17];
    MH_CreateHook(vt[17], h_Present,  (void**)&o_Present);
    MH_CreateHook(vt[16], h_Reset,    (void**)&o_Reset);
    // slot 42, verified against d3d9.h with Reset=16/Present=17 as anchors. Fallback frame
    // build for frames where vgui never paints (loading screens). See render.cpp.
    MH_CreateHook(vt[42], h_EndScene, (void**)&o_EndScene);

    // Slot 57. Watches for the RESZ code the engine writes from its own
    // UpdateFullScreenDepthTexture, which is the instant opaque geometry finishes and no
    // particle has drawn yet -- where the pre-translucent frame copy is taken.
    ScreenFx_InstallRenderStateHook(vt[57]);
    ScreenFx_InstallDepthStencilHooks(vt[39], vt[40]);   // unit post: "universal depth" z swap (d3d9.h slots 39/40)

    // PaintTraverse moved to PollThread (vgui2.dll may not be loaded yet here).

    MH_EnableHook(MH_ALL_HOOKS);

    _mkdir(CONFIG_DIR);
    if (GetFileAttributesA(CONFIG_PATH) == INVALID_FILE_ATTRIBUTES) SaveConfig();
    LoadConfig();

    DebugLog("botox loaded, hooks installed");
    LogPresentHookOwner();

    g_hPollThread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        PollThread();
        return 0;
    }, nullptr, 0, nullptr);

    // Now-playing poll. Its own thread and not PollThread's: the winrt session calls block for
    // milliseconds, and PollThread is a one-shot installer that has already finished by then.
    // It idles until the feature is switched on, so an unused media player costs one sleeping
    // thread and no winrt at all.
    MediaPlayer_Start();

    // Discord rich presence: same idle-until-enabled shape, its own IPC thread, no hook.
    DiscordRpc_Start();

    while (!g_eject) {
        if (GetAsyncKeyState(VK_F11) & 1) g_eject.store(true);
        Sleep(50);
    }
    DebugLog("eject triggered");
    ConsoleColorPrint((uint8_t)(g_consoleColor[0]*255.f), (uint8_t)(g_consoleColor[1]*255.f),
                       (uint8_t)(g_consoleColor[2]*255.f), "botox off\n");

    // Disable features so mid-flight hooks finish clean, start no new work
    g_weatherEnable = false;
    // Clearing this is what puts $blendtintbybasealpha back: the render slow tick sees the
    // feature off and reverts each material itself, on its own thread, before the hooks go
    // away. Writing those vars from here would race the render thread mid-draw.
    g_vmAlphaEnable = false;
    g_noHats        = false;
    g_noUnusual     = false;
    g_fogEnable     = false;
    g_skyEnable     = false;

    // Join PollThread FIRST. It calls MH_CreateHook/MH_EnableHook, so letting it live
    // past MH_DisableHook(MH_ALL_HOOKS) races MinHook's own state -- and it used to be
    // joined with a 2000ms wait against its own 3000ms opening sleep, i.e. never.
    if (g_hPollThread) {
        WaitForSingleObject(g_hPollThread, 6000);
        CloseHandle(g_hPollThread);
        g_hPollThread = nullptr;
    }

    // Media poll: touches no hook and no engine state, only winrt and its own buffers, so it
    // joins here on its own. Drops the cover texture with it.
    MediaPlayer_Shutdown();
    DiscordRpc_Shutdown();

    // Flip world holds cl_flipviewmodels, m_yaw and a few HUD code bytes while it mirrors. Only the
    // game thread may put the hand back (the write sends userinfo), and FrameStageNotify does all of
    // it the frame after g_eject -- give it up to a second before the hooks come off. If no frame
    // comes, Release restores m_yaw and the HUD bytes from here and logs the hand it had to leave.
    for (int i = 0; i < 100 && FlipWorld_Holding(); ++i) Sleep(10);
    FlipWorld_Release();

    // Aim subsystem: sets G::Unload (every aim hook early-outs on it), disables
    // its whitelisted hooks and restores the convars it unlocked. Before MH_DisableHook so the
    // aim pipeline stops feeding on half-torn-down state.
    Aim_Unload();

    MH_DisableHook(MH_ALL_HOOKS);
    Sleep(50);

    // Drain in-flight hook calls before touching shared state. Every hook dih owns bumps one
    // of these counters (MiscHookGuard in globals.h). The AIM half's ~28 hooks do NOT -- see
    // the MH_Uninitialize note below for why that is no longer a use-after-free.
    for (int i = 0; i < 300 && g_hookDepth.load()     > 0; ++i) Sleep(10);
    for (int i = 0; i < 300 && g_miscHookDepth.load() > 0; ++i) Sleep(10);
    FlipWorld_Unload();   // flip world's owner token must not outlive this DLL

    // Safe here and not earlier: releases the D3D event query, and the render thread is
    // provably out of h_Present by this point.
    Perf_Shutdown();
    GameExtras_Shutdown();   // unit game: knobs/pings back if no frame did it, http aborted, workers joined
    ScreenFx_RestoreQueueMode();   // hand mat_queue_mode back before the hooks come off

    // Tear down CPrecipitation before nulling entity list -- live slot-2047
    // entity crashes next map load otherwise
    if (g_entityList && Precip_IsLive())
        Precip_TearDown(2047);

    ResetFogCvars();
    WorldFx3_Release();    // ragdoll m_vecForce proxy, r_3dsky / viewmodel_fov, material colours back
    NetSettings_Shutdown();// net cvars back at the player's own value
    Crosshair_Restore();   // hand "crosshair" back at the player's value
    FakeMove_Restore();    // sv_wallbug back (listen server keeps cvars across maps)
    // sv_skyname needs nothing here -- h_R_DrawSkyBox restores it inside every draw, and the
    // hook drain above guarantees the last one finished.

    // MH_Uninitialize() DELETED, DO NOT RE-ADD. It is the only thing that frees MinHook's
    // trampolines, and under a soft eject (the DLL stays loaded, see the bottom of this
    // function) freeing them buys nothing and costs a use-after-free:
    //   * MH_DisableHook already put the original bytes back, so no NEW call can reach us.
    //   * MinHook's thread freeze only fixes instruction pointers. A thread parked in a hook
    //     BODY still has a return address into the trampoline on its stack, and no counter
    //     can drain what it does not count.
    //   * The aim half's ~28 hooks (features/aim/hooks.cpp, installed via MAKE_HOOK) bump
    //     neither depth counter, and the whitelist is a moving target -- so the drain above
    //     could never have covered them. Same for anything else that forgets a guard.
    // Keeping the trampoline pages mapped makes every one of those cases a harmless return
    // through dead-but-valid code. The cost is MinHook's own heap, leaked once, at eject.
    MH_DisableHook(MH_ALL_HOOKS);   // idempotent: catch anything armed after the first call

    if (g_showMenu) ShowCursor(FALSE);
    ClipCursor(nullptr);

    if (g_imguiInit)
    {
        if (g_origWndProc && g_hwnd)
            SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_origWndProc);
        CsgoCrosshair_ReleaseTextures();
        ColorCorr_Destroy();
        Bloom_Destroy();
        ScreenFx_Destroy();
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    DebugLog_Shutdown();   // last line written; hands C:otox-tf2\debug.log back to the user

    // Soft eject: DLL stays loaded so engine-held stale pointers can't crash
    ExitThread(0);
}

// ==== DllMain ====================================================================================
BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hMod);
        // Before the thread, before a single hook: a crash during startup is still a crash, and
        // this is the only recorder that does not need anyone to have ticked "debug log".
        // Touches no file and no CRT here -- PE headers, a lock, AddVectoredExceptionHandler.
        CrashLog_Install(hMod);
        HANDLE t = CreateThread(nullptr, 0,
            [](LPVOID) -> DWORD { MainThread(); return 0; },
            nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
