#include "scoreboard.h"
#include "../../globals/common.h"
#include "../network/mmfix.h"     // MMFix_Pump -- CVGui::RunFrame is the main thread the GC calls need
#include "../../hooks/crashlog.h"  // breadcrumbs + the main-thread hitch trace, both into C:otox-tf2\crash.log
#include "../misc/names.h"        // right-click "Add to Alias List"
#include <intrin.h>

// ==== reveal scoreboard =============================================================
// See scoreboard.h for what the toggle does. This file is all disassembly-driven, so
// the evidence is written down next to each address -- RVAs are from the client.dll
// this was ported against and are here to identify the code, not to be used directly
// (every address below is resolved by pattern at runtime).
//
// CTFClientScoreBoardDialog::UpdatePlayerList, class-icon vs nemesis branch:
//
//   66f0fe  cmp  eax, 2                 ; GetTeam(player)
//   66f101  jne  66f152
//   66f103  mov  eax, [rsp+4a8]         ; local team
//   66f10a  cmp  eax, 3
//   66f10d  jne  66f163                 ; NOT opposite teams -> class-icon path
//   ...                                 ; opposite teams -> nemesis / dominated path
//   66f19d  mov  ebp, eax               <-- s_jmpClassIcon: head of the class-icon block
//   66f1dd  mov  ebp, eax               ; ebp (the class) is re-derived here on every
//                                       ; path an enemy can take, so the garbage eax we
//                                       ; land on at 66f19d never survives to be used
//   66f26c  call CTFPlayerShared::IsPlayerDominated
//   66f271  test al, al                 <-- s_retIsDominated
//   66f273  je   66f299                 ; not dominated -> nothing drawn at all
//   66f294  call KeyValues::SetInt      ; ("nemesis", icon)
//   66f299  mov  rax, [rip+aeb488]      <-- s_retSetInt
//
// Both enemy exits (nothing drawn / nemesis drawn) are sent to s_jmpClassIcon by
// rewriting our own return slot, which is the caller's frame -- the stack pointer is
// untouched, so this is a jump inside UpdatePlayerList, not a frame change.
//
// CTFTeamStatusPlayerPanel::Update:
//
//   5d6ef8  call CTFPlayerPanel::GetTeam
//   5d6efd  mov  ebx, [rdi+244]         <-- s_retPanelGetTeam  (+244 = player index)
//   5d6f09  mov  [rsp+90], eax          ; the team we just answered
//   5d7354  mov  rcx, [rdi+2b0]         ; health bar panel, null-checked right there
//   5d7376  cmp  eax, [rdi+270]         ; cached health -- equal means SKIP the bar
//   5d7387  mov  [rdi+270], eax
//   5d7395  cmp  esi, r15d              ; esi = the GetTeam answer, r15d = local team
//   5d7398  jne  5d7494                 ; different team -> bar stays empty
//
// so answering "local team" fills the bar, and +270 has to be poisoned on the toggle
// edge or the cached-health compare skips the whole block before the team ever matters.

// ---- resolved addresses (0 = not resolved, hook stands down) ------------------------
static uintptr_t s_retIsDominated   = 0;
static uintptr_t s_retSetInt        = 0;
static uintptr_t s_jmpClassIcon     = 0;
static uintptr_t s_retPanelGetTeam  = 0;

static bool   s_lastReveal  = false;     // toggle edge detector (CVGui::RunFrame)
static DWORD  s_dirtyUntil  = 0;         // poison the panels' health cache until this tick

// ---- the three reveal hooks are ARMED BY THE TOGGLE, not at install ---------------------
// KeyValues::SetInt is one of the hottest functions in client.dll -- every vgui KeyValues write
// in the game goes through it -- and IsPlayerDominated and GetTeam are on the scoreboard's own
// rebuild path. With the toggle off (the default) all three used to carry a detour, an atomic
// counter and a return-address compare for nothing. Created disarmed, enabled on the toggle
// edge from CVGui::RunFrame (main thread, and rare), disabled again when it goes off.
static void* s_addrGetTeam   = nullptr;
static void* s_addrDominated = nullptr;
static void* s_addrSetInt    = nullptr;
static bool  s_revealArmed   = false;
static bool  s_armStatic     = false;    // no RunFrame hook -> nothing can arm them, stay on

// -- panel field offsets, all three read straight out of Update above ----------------
static constexpr uintptr_t kPanelPlayerIndex = 0x244;
static constexpr uintptr_t kPanelHealthBar   = 0x2b0;
static constexpr uintptr_t kPanelHealthCache = 0x270;

using PanelGetTeam_t       = int (__fastcall*)(void*);
using IsPlayerDominated_t  = bool(__fastcall*)(void*, int);
using KeyValuesSetInt_t    = void(__fastcall*)(void*, const char*, int);
using SbNeedsUpdate_t      = bool(__fastcall*)(void*);
using VGuiRunFrame_t       = void(__fastcall*)(void*);

static PanelGetTeam_t      o_PanelGetTeam      = nullptr;
static IsPlayerDominated_t o_IsPlayerDominated = nullptr;
static KeyValuesSetInt_t   o_KeyValuesSetInt   = nullptr;
static SbNeedsUpdate_t     o_SbNeedsUpdate     = nullptr;
static VGuiRunFrame_t      o_VGuiRunFrame      = nullptr;

// ---- helpers -----------------------------------------------------------------------
// Local team off the local player entity's m_iTeamNum. Only 2 (RED) and 3 (BLU) count:
// as a spectator the local team is 1, and answering 1 for every panel would blank the
// match hud instead of revealing anything.
static int LocalPlayerTeam()
{
    if (g_offTeamNum <= 0) return 0;
    const int idx = GetLocalPlayerIndex();
    if (idx <= 0) return 0;
    void* pEnt = GetClientEntity(idx);
    if (!pEnt) return 0;

    int team = 0;
    __try { team = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pEnt) + g_offTeamNum); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return (team == 2 || team == 3) ? team : 0;
}

static bool RevealOn() { return g_revealScoreboard && !g_eject; }

// ---- CTFPlayerPanel::GetTeam -------------------------------------------------------
// Answer "local team" only for the one call site inside CTFTeamStatusPlayerPanel::Update.
// Every other caller of GetTeam gets the truth, so nothing else in the hud is touched.
// __try body is split out: the guard object below cannot share a frame with SEH (C2712).
static void PanelForceRefresh(void* panel)
{
    __try {
        if (*reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(panel) + kPanelHealthBar))
            *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(panel) + kPanelHealthCache) = -1;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static int __fastcall h_PanelGetTeam(void* thisptr)
{
    MiscHookGuard _g;
    int ret = o_PanelGetTeam(thisptr);

    if (!thisptr || !RevealOn() || reinterpret_cast<uintptr_t>(_ReturnAddress()) != s_retPanelGetTeam)
        return ret;

    const int localTeam = LocalPlayerTeam();
    if (localTeam == 0) return ret;

    // toggle edge: the panel skips its whole health-bar block while the cached health
    // matches, so flipping the toggle would otherwise change nothing until the player
    // next took damage
    if (GetTickCount() < s_dirtyUntil) PanelForceRefresh(thisptr);

    return localTeam;
}

// ---- CTFPlayerShared::IsPlayerDominated --------------------------------------------
// Enemy, not dominated: UpdatePlayerList would draw nothing in that column. Return into
// the class-icon block instead. NO MiscHookGuard object here on purpose -- the return
// slot is rewritten by hand and this frame is kept trivial; the depth counter is bumped
// and dropped explicitly instead.
static bool __fastcall h_IsPlayerDominated(void* thisptr, int index)
{
    ++g_miscHookDepth;
    const bool ret = o_IsPlayerDominated(thisptr, index);

    if (!ret && RevealOn() && s_jmpClassIcon &&
        reinterpret_cast<uintptr_t>(_ReturnAddress()) == s_retIsDominated)
    {
        // the riskiest thing this dll does: leave a breadcrumb so a fault that lands in
        // client.dll a few instructions later still names who sent it there
        CrashLog_Mark("sb/ret-rewrite(dominated)");
        *static_cast<uintptr_t*>(_AddressOfReturnAddress()) = s_jmpClassIcon;
    }

    --g_miscHookDepth;
    return ret;
}

// ---- KeyValues::SetInt -------------------------------------------------------------
// Hot function -- the toggle is checked before anything else, and the key string is only
// compared once the return address has already pinned us to the one call site that sets
// "nemesis" (the class-icon paths reach the same SetInt with their own key, which is why
// the name has to be checked and not just the site).
static void __fastcall h_KeyValuesSetInt(void* thisptr, const char* keyName, int value)
{
    ++g_miscHookDepth;
    o_KeyValuesSetInt(thisptr, keyName, value);

    if (RevealOn() && s_jmpClassIcon &&
        reinterpret_cast<uintptr_t>(_ReturnAddress()) == s_retSetInt &&
        keyName && strcmp(keyName, "nemesis") == 0)
    {
        CrashLog_Mark("sb/ret-rewrite(nemesis)");
        *static_cast<uintptr_t*>(_AddressOfReturnAddress()) = s_jmpClassIcon;
    }

    --g_miscHookDepth;
}

// ---- CClientScoreBoardDialog::NeedsUpdate ------------------------------------------
// The scoreboard only rebuilds its rows when this says so. Force one rebuild on the tick
// the toggle changes, otherwise the icons stay as they were until the next score event.
static bool __fastcall h_SbNeedsUpdate(void* thisptr)
{
    MiscHookGuard _g;
    static bool sbLast = false;
    const bool now = RevealOn();
    if (now != sbLast) { sbLast = now; CrashLog_Mark("sb/forced-rebuild"); return true; }
    return o_SbNeedsUpdate(thisptr);
}

// ---- arm / disarm the three reveal detours ------------------------------------------
// Main thread (CVGui::RunFrame), on the toggle edge only. MinHook freezes the OTHER threads to
// patch, never the caller, and a thread already inside a detour body returns through its
// trampoline, which MH_DisableHook does not free (see thread.cpp's MH_Uninitialize note).
static void RevealArm(bool on)
{
    if (s_armStatic || on == s_revealArmed) return;
    s_revealArmed = on;
    CrashLog_Mark(on ? "sb/arm" : "sb/disarm");
    void* const kAddrs[] = { s_addrGetTeam, s_addrDominated, s_addrSetInt };
    for (void* a : kAddrs)
        if (a) { if (on) MH_EnableHook(a); else MH_DisableHook(a); }
    DebugLog("scoreboard: reveal hooks %s", on ? "armed" : "disarmed");
}

// ---- CVGui::RunFrame (vgui2.dll) ---------------------------------------------------
// The toggle edge, and the one per-frame main-thread callback the dll has.
//
// The out-of-band `o_TeamStatusOnTick(s_pTeamStatus)` that used to live here is GONE, DO NOT
// RE-ADD. It called a CTFTeamStatus panel pointer cached in that panel's own OnTick, from a
// different callback -- and vgui deletes those panels on every hud scheme reload, level change
// and match end. The cached pointer outlived the panel and the next toggle flip called a
// virtual on freed memory. All it ever bought was one frame: s_dirtyUntil already poisons the
// health cache for 500ms, and the panels tick every frame anyway, so the reveal still fills in
// on the next natural tick.
static void __fastcall h_VGuiRunFrame(void* thisptr)
{
    MiscHookGuard _g;
    CrashSpan _s("vgui/runframe");   // our part + the engine's RunFrame: a HITCH here = vgui itself

    // This is the MAIN thread and it ticks in the menu as well as in game, which is exactly
    // what the matchmaker unstick needs: its GC calls must not run on the render thread the
    // menu button is pressed from. Nothing to do unless a button queued something.
    if (!g_eject) MMFix_Pump();
    CrashLog_Frame();   // main-thread frame gap -> a HITCH line in C:otox-tf2\crash.log

    const bool now = RevealOn();
    if (now != s_lastReveal)
    {
        s_lastReveal = now;
        s_dirtyUntil = GetTickCount() + 500;
        RevealArm(now);
    }

    o_VGuiRunFrame(thisptr);
}

// ==== STEAM HISTORY ==================================================================
// "Steam History" in the scoreboard's right-click menu -> opens
// steamhistory.net/id/<steam64> for that player in the steam overlay. Ported from
// fpsoverlay's scoreboard_hook.cpp, with its "Add to Alias List" item (custom names,
// misc/names.cpp). Group marking is not brought over.
//
// Three hooks, because the menu never carries the player it was opened on:
//   CTFClientScoreBoardDialog::OnScoreBoardMouseRightRelease calls IsFakePlayer(index)
//   first -- that call site is the only place the index is in a register we can read,
//   so the hook stamps it. It then calls AddMenuItem for the "profile" category; that
//   call site is where our own item is appended. OnCommand catches the click.
// Both call sites are matched by RETURN ADDRESS (the pattern starts at the instruction
// after the call), so every other caller of these three functions is untouched.
static uintptr_t s_addrIsFakePlayerCall = 0;
static uintptr_t s_addrAddMenuProfile   = 0;
static char      s_shInstallLine[256]   = {};   // install result, replayed once the log is on
static int       s_shPlayerIndex        = 0;
static uint32_t  s_shAccountID          = 0;

using IsFakePlayer_t = bool  (__fastcall*)(void*, int);
using AddMenuItem_t  = void* (__fastcall*)(void*, const char*, const char*, const char*);
using OnCommand_t    = void  (__fastcall*)(void*, const char*);

static IsFakePlayer_t o_IsFakePlayer = nullptr;
static AddMenuItem_t  o_AddMenuItem  = nullptr;
static OnCommand_t    o_OnCommand    = nullptr;

// TF2's real player_info_t is name[32] / friendsName[32] -- only friendsID is read here,
// and it only lands at the right offset with this layout.
struct PlayerInfoSdk_t
{
    char     name[32];
    int      userID;
    char     guid[33];
    uint32_t friendsID;
    char     friendsName[32];
    bool     fakeplayer;
    bool     ishltv;
    uint32_t customFiles[4];
    uint8_t  filesDownloaded;
};

// IVEngineClient::GetPlayerInfo -- slot 8 (same table GetLocalPlayerIndex=12 comes from)
static uint32_t PlayerAccountID(int iIndex)
{
    if (!g_engine) return 0;
    PlayerInfoSdk_t pi = {};
    VCall<bool>(g_engine, 8, iIndex, &pi);
    return pi.friendsID;
}

static uint64_t AccountIDToSteam64(uint32_t acct) { return 0x0110000100000000ULL | (uint64_t)acct; }

// TF2 x64 loads steam_api64.dll. Flat export first, then the legacy one, then the
// version-agnostic FindOrCreate path.
static void* GetSteamFriends()
{
    HMODULE h = GetModuleHandleA("steam_api64.dll");
    if (!h) h = GetModuleHandleA("steam_api.dll");
    if (!h) return nullptr;

    if (auto pfnFlat = (void*(*)())GetProcAddress(h, "SteamAPI_SteamFriends_v017"))
        if (void* p = pfnFlat()) return p;

    if (auto pfnLegacy = (void*(*)())GetProcAddress(h, "SteamFriends"))
        if (void* p = pfnLegacy()) return p;

    using FindOrCreate_t = void*(*)(int, const char*);
    auto pfnFOC  = (FindOrCreate_t)GetProcAddress(h, "SteamInternal_FindOrCreateUserInterface");
    auto pfnUser = (int(*)())GetProcAddress(h, "SteamAPI_GetHSteamUser");
    if (pfnFOC && pfnUser)
        if (void* p = pfnFOC(pfnUser(), "SteamFriends017")) return p;

    return nullptr;
}

// ISteamFriends slot 30 = ActivateGameOverlayToWebPage(url, mode)
static void SteamOverlay_OpenURL(const char* url)
{
    if (void* friends = GetSteamFriends())
        VCall<void>(friends, 30, url, 0);
}

static bool SteamHistoryOn() { return g_steamHistory && !g_eject; }

// All three carry a MiscHookGuard: they only READ _ReturnAddress(), so unlike the two
// return-slot rewrites above they may hold an object with a destructor. They were the last
// three hooks in the dll with no drain counter at all.
static bool __fastcall h_IsFakePlayer(void* thisptr, int index)
{
    MiscHookGuard _g;
    if (s_addrIsFakePlayerCall && uintptr_t(_ReturnAddress()) == s_addrIsFakePlayerCall)
        s_shPlayerIndex = index;
    return o_IsFakePlayer(thisptr, index);
}

static void* __fastcall h_AddMenuItem(void* thisptr, const char* pszButtonText,
                                      const char* pszCommand, const char* pszCategory)
{
    MiscHookGuard _g;
    if (!g_eject && s_addrAddMenuProfile
        && uintptr_t(_ReturnAddress()) == s_addrAddMenuProfile)
    {
        s_shAccountID = PlayerAccountID(s_shPlayerIndex);   // hooked GetPlayerInfo: also caches the real name
        void* ret = o_AddMenuItem(thisptr, pszButtonText, pszCommand, pszCategory);
        if (s_shAccountID && g_steamHistory)
            o_AddMenuItem(thisptr, "Steam History", "botox_history", "profile");
        // fpsoverlay scoreboard_hook: alias item, always on (custom names, misc > player list)
        if (s_shAccountID)
            o_AddMenuItem(thisptr, Names_HasAlias(s_shAccountID) ? "Edit Alias" : "Add to Alias List",
                          "botox_alias", "profile");
        return ret;   // the real item's panel, not ours -- the caller keeps using it
    }
    return o_AddMenuItem(thisptr, pszButtonText, pszCommand, pszCategory);
}

static void __fastcall h_OnCommand(void* thisptr, const char* command)
{
    MiscHookGuard _g;
    // SteamHistoryOn() gates the SWALLOW too, not just the open: with the feature off no item
    // of ours was ever added, so "botox_history" could only have come from somewhere else and
    // eating it is wrong.
    if (SteamHistoryOn() && command && strcmp(command, "botox_history") == 0)
    {
        if (s_shAccountID)
        {
            char url[128];
            snprintf(url, sizeof(url), "https://steamhistory.net/id/%llu",
                     (unsigned long long)AccountIDToSteam64(s_shAccountID));
            SteamOverlay_OpenURL(url);
        }
        return;   // ours: the game has no such command
    }
    // fpsoverlay iv_alias: add (real name as the start text); "Edit" = it is already there,
    // the name is edited in misc > player list
    if (!g_eject && command && strcmp(command, "botox_alias") == 0)
    {
        Names_AddAlias(s_shAccountID, s_shPlayerIndex);
        return;
    }
    o_OnCommand(thisptr, command);
}

static void SteamHistory_Install(HMODULE hClient)
{
    uint8_t* pIsFakePlayer = PatternScan(hClient,
        "48 89 5C 24 ? 57 48 81 EC ? ? ? ? 48 8B 01 48 8B F9 48 63 DA");

    uint8_t* pAddMenuItem = PatternScan(hClient,
        "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B EA 49 8B F9 48 8B 51 ? "
        "49 8B F0 48 8B D9 48 85 D2 74 ? 49 8B C9 E8 ? ? ? ? 85 C0 74 ? 48 8B 0B 48 8B 01 "
        "FF 90 ? ? ? ? 48 8B 0B 4C 8B C6 4C 8B 4B ? 48 8B D5 48 89 7B ? 48 C7 44 24 ? ? ? ? ? "
        "48 8B 01 FF 90 ? ? ? ? 48 8B 13 3B 82 ? ? ? ? 73 ? 3B 82 ? ? ? ? 7F ? 48 8B 92 ? ? ? ? "
        "8B C8 48 03 C9 39 44 CA ? 75 ? 39 44 CA ? 75 ? 48 8B 04 CA EB ? 33 C0 "
        "48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 83 C4 ? 5F C3 CC CC CC CC 48 89 5C 24");

    uint8_t* pOnCommand = PatternScan(hClient,
        "48 89 5C 24 ? 57 48 83 EC ? 48 8B DA 48 8B F9 48 8B CB 48 8D 15 ? ? ? ? "
        "E8 ? ? ? ? 48 85 C0 74 ? 48 8B 0D");

    // call sites inside OnScoreBoardMouseRightRelease -- these patterns start AT the
    // return address, which is what _ReturnAddress() hands us
    uint8_t* pSiteIsFake  = PatternScan(hClient,
        "48 8B 0D ? ? ? ? 41 8B D6 48 81 C1 ? ? ? ? 44 0F B6 E8");
    uint8_t* pSiteProfile = PatternScan(hClient,
        "48 8B 0D ? ? ? ? 4C 8D 85 ? ? ? ? 48 8D 95 ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 44 8B 85");

    s_addrIsFakePlayerCall = reinterpret_cast<uintptr_t>(pSiteIsFake);
    s_addrAddMenuProfile   = reinterpret_cast<uintptr_t>(pSiteProfile);

    // Each hook is worthless without the one before it: the index hook needs its call
    // site, the menu hook needs the index, and no item is ever added without the menu
    // hook -- so the command hook stands down with it.
    if (s_addrIsFakePlayerCall)
        InstallHookAt(pIsFakePlayer, "steamhistory: IsFakePlayer",
                      (void*)h_IsFakePlayer, (void**)&o_IsFakePlayer);
    if (s_addrAddMenuProfile && o_IsFakePlayer)
        InstallHookAt(pAddMenuItem, "steamhistory: AddMenuItem",
                      (void*)h_AddMenuItem, (void**)&o_AddMenuItem);
    if (o_AddMenuItem)
        InstallHookAt(pOnCommand, "steamhistory: OnCommand",
                      (void*)h_OnCommand, (void**)&o_OnCommand);

    snprintf(s_shInstallLine, sizeof(s_shInstallLine),
             "steamhistory: isfake=%d addmenu=%d oncmd=%d | site_isfake=0x%IX site_profile=0x%IX "
             "steam_api64=%p",
             o_IsFakePlayer != nullptr, o_AddMenuItem != nullptr, o_OnCommand != nullptr,
             s_addrIsFakePlayerCall, s_addrAddMenuProfile,
             (void*)GetModuleHandleA("steam_api64.dll"));
    DebugLog("%s", s_shInstallLine);   // no-op while the log is off -- replayed when it goes on
}

void Scoreboard_LogInstallOnce()
{
    if (!g_debugLogEnable || !s_shInstallLine[0]) return;
    DebugLog("%s (replayed at log enable)", s_shInstallLine);
    s_shInstallLine[0] = '\0';
}

// ---- install -----------------------------------------------------------------------
void Scoreboard_Install()
{
    HMODULE hClient = GetModuleHandleA("client.dll");
    HMODULE hVgui   = GetModuleHandleA("vgui2.dll");
    if (!hClient) { DebugLog("scoreboard: client.dll not loaded"); return; }

    // functions to detour -- an extra match here is survivable (the wrong function gets a
    // detour that forwards), so these stay on the plain scan.
    uint8_t* pGetTeam    = PatternScan(hClient, "8B 91 ? ? ? ? 83 FA ? 74 ? 48 8B 05");
    uint8_t* pDominated  = PatternScan(hClient, "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 63 F2 48 8B D9 E8");
    uint8_t* pSetInt     = PatternScan(hClient, "40 53 48 83 EC ? 41 8B D8 41 B0");
    uint8_t* pNeedsUpd   = PatternScan(hClient, "48 8B 05 ? ? ? ? F3 0F 10 41 ? 0F 2F 40 ? 0F 92 C0");

    // Call sites and the jump target: compared against _ReturnAddress(), or written INTO a
    // return slot, so each pattern starts at the instruction AFTER its call -- that address IS
    // the return address. These four are UNIQUE scans. Each one is short (8-9 bytes of very
    // ordinary x64: `mov ebp,eax / call / cmp eax,edi` is everywhere), and a second match means
    // the first hit is no longer provably the site this file was written against. Returning
    // into the wrong function is not a degraded feature, it is a crash on the next scoreboard
    // rebuild, which is exactly the "random crash when I open the scoreboard" shape.
    bool ambPanel = false, ambDom = false, ambSet = false, ambJmp = false;
    uint8_t* pRetPanel   = PatternScanUnique(hClient, "8B 9F ? ? ? ? 40 32 F6", &ambPanel);
    uint8_t* pRetDom     = PatternScanUnique(hClient, "84 C0 74 ? 45 84 FF 74", &ambDom);
    uint8_t* pRetSetInt  = PatternScanUnique(hClient, "48 8B 05 ? ? ? ? 83 78 ? ? 0F 84 ? ? ? ? 48 8B 0D ? ? ? ? 8B D7", &ambSet);
    uint8_t* pJump       = PatternScanUnique(hClient, "8B E8 E8 ? ? ? ? 3B C7", &ambJmp);

    s_retPanelGetTeam = reinterpret_cast<uintptr_t>(pRetPanel);
    s_retIsDominated  = reinterpret_cast<uintptr_t>(pRetDom);
    s_retSetInt       = reinterpret_cast<uintptr_t>(pRetSetInt);
    s_jmpClassIcon    = reinterpret_cast<uintptr_t>(pJump);

    // The two return-slot rewrites are all-or-nothing: without the jump target, or
    // without the call site that pins us inside UpdatePlayerList, they must not install.
    const bool sbOk = s_jmpClassIcon && s_retIsDominated && s_retSetInt;

    // RunFrame first: it is what arms the reveal detours, and MMFix's main-thread pump and the
    // hitch trace both ride it whether or not the scoreboard sigs resolved. It no longer waits
    // on the OnTick hook, which is gone.
    if (hVgui)
        InstallPatternHook(hVgui, "scoreboard: CVGui::RunFrame", "48 8B C4 53 48 81 EC",
                           (void*)h_VGuiRunFrame, (void**)&o_VGuiRunFrame);

    // No RunFrame = no toggle edge = nothing to arm them later, so in that one case they go in
    // live, as they always did.
    s_armStatic = (o_VGuiRunFrame == nullptr);
    const bool bArm = s_armStatic;

    if (s_retPanelGetTeam &&
        InstallHookAt(pGetTeam, "scoreboard: CTFPlayerPanel::GetTeam",
                      (void*)h_PanelGetTeam, (void**)&o_PanelGetTeam, bArm))
        s_addrGetTeam = pGetTeam;
    if (sbOk) {
        if (InstallHookAt(pDominated, "scoreboard: IsPlayerDominated",
                          (void*)h_IsPlayerDominated, (void**)&o_IsPlayerDominated, bArm))
            s_addrDominated = pDominated;
        if (InstallHookAt(pSetInt, "scoreboard: KeyValues::SetInt",
                          (void*)h_KeyValuesSetInt, (void**)&o_KeyValuesSetInt, bArm))
            s_addrSetInt = pSetInt;
    }
    // Cheap and cold: left armed so the toggle edge can still force one rebuild.
    InstallHookAt(pNeedsUpd, "scoreboard: NeedsUpdate",
                  (void*)h_SbNeedsUpdate, (void**)&o_SbNeedsUpdate);

    SteamHistory_Install(hClient);

    DebugLog("scoreboard: getteam=%d dom=%d setint=%d needsupd=%d runframe=%d armed_at_install=%d | "
             "retpanel=0x%IX retdom=0x%IX retset=0x%IX jmp=0x%IX sbOk=%d | "
             "AMBIGUOUS retpanel=%d retdom=%d retset=%d jmp=%d",
             o_PanelGetTeam != nullptr, o_IsPlayerDominated != nullptr,
             o_KeyValuesSetInt != nullptr, o_SbNeedsUpdate != nullptr,
             o_VGuiRunFrame != nullptr, (int)bArm,
             s_retPanelGetTeam, s_retIsDominated, s_retSetInt, s_jmpClassIcon, (int)sbOk,
             (int)ambPanel, (int)ambDom, (int)ambSet, (int)ambJmp);
}
