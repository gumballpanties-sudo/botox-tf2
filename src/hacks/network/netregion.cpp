#include "netregion.h"
#include "../../globals/globals.h"
#include "netcvar.h"  // which preset line may be written right now, and why not
#include <intrin.h>   // _ReturnAddress -- net graph position hook gates on the call site
#include <math.h>     // fabsf -- net settings read-back

// ==== convenience: region selector + net settings ==================================
// Region selector ported from Aletherium (ISteamNetworkingUtils_GetPingToDataCenter).
// Net settings = exec cfg + snapshot/restore, ALL through the console with the engine's
// own clamps and connect rules left alone (the clamp strip is gone -- see below).

void* g_steamNetUtils = nullptr;

// bit index -> label (menu order). Matches g_regionMask bits.
const char* const kRegionNames[29] = {
    "Atlanta", "Chicago", "Dallas", "Los Angeles", "Seattle", "Virginia",      // NA 0-5
    "Amsterdam", "Falkenstein", "Frankfurt", "Helsinki", "London", "Madrid",   // EU 6-11
    "Paris", "Stockholm", "Vienna", "Warsaw",                                   // EU 12-15
    "Buenos Aires", "Lima", "Santiago", "Sao Paulo",                           // SA 16-19
    "Chennai", "Dubai", "Hong Kong", "Mumbai", "Seoul", "Singapore", "Tokyo",  // AS 20-26
    "Sydney", "Johannesburg",                                                   // AU/AF 27-28
};

// SDR POP 4-char code -> region bit index. Aletherium GetDatacenter map.
struct PopMap { const char* code; int bit; };
static const PopMap kPops[] = {
    {"atl",0},{"ord",1},{"dfw",2},{"lax",3},{"sea",4},{"eat",4},{"iad",5},
    {"ams",6},{"ams4",6},{"fsn",7},{"fra",8},{"hel",9},{"lhr",10},{"mad",11},
    {"par",12},{"sto",13},{"sto2",13},{"vie",14},{"waw",15},
    {"eze",16},{"lim",17},{"scl",18},{"gru",19},
    {"maa2",20},{"dxb",21},{"hkg",22},{"bom2",23},{"seo",24},{"sgp",25},{"tyo",26},
    {"syd",27},{"jnb",28},
};

// SteamNetworkingPOPID (uint32) -> 4-char code (Steam packs it big-endian-ish)
static void PopIdName(uint32_t popID, char* out)
{
    out[0] = static_cast<char>(popID >> 16);
    out[1] = static_cast<char>(popID >> 8);
    out[2] = static_cast<char>(popID);
    out[3] = static_cast<char>(popID >> 24);
    out[4] = 0;
    // trim trailing nulls/spaces so "atl\0" compares clean
    for (int i = 3; i >= 0; --i) {
        if (out[i] == 0 || out[i] == ' ') out[i] = 0; else break;
    }
}

static int PopBit(const char* code)
{
    for (const auto& p : kPops)
        if (strcmp(p.code, code) == 0) return p.bit;
    return -1;
}

// ---- GetPingToDataCenter hook (ISteamNetworkingUtils slot 8) --------------------
using GetPingToDataCenter_t = int(__fastcall*)(void*, uint32_t, uint32_t*);
static GetPingToDataCenter_t o_GetPingToDataCenter = nullptr;

// true once a faked ping has been handed out. TF2 CACHES what it reads, so the fake
// survives in that cache after the menu toggle goes off -- the hook going passthrough
// does NOT undo it. Flag says "cache is dirty, needs one real refresh".
static volatile bool s_pingsFaked = false;

static int __fastcall h_GetPingToDataCenter(void* rcx, uint32_t popID, uint32_t* pViaRelayPoP)
{
    MiscHookGuard _g;
    int iReturn = o_GetPingToDataCenter(rcx, popID, pViaRelayPoP);

    if (!g_regionEnable || g_regionMask == 0 || iReturn < 0)
        return iReturn;

    char code[5];
    PopIdName(popID, code);
    int bit = PopBit(code);
    if (bit >= 0) {
        s_pingsFaked = true;
        return (g_regionMask & (1 << bit)) ? 1 : 1000;
    }

    return iReturn;
}

// ---- force ping refresh at queue time (Aletherium) ------------------------------
// TF2 caches datacenter pings. The GetPingToDataCenter hook only changes what a
// FRESH ping read returns -- but the game queues with the pings it measured earlier
// (real ones). CTFPartyClient::RequestQueueForMatch is hooked to force a ping refresh
// right before queueing: SetPendingPingRefresh(true) (bool at CTFGCClientSystem+1116)
// + PingThink() re-reads every datacenter through our hook, so the FAKED pings land
// in the queue request. Without this the region force does nothing in-game.
void* g_tfgcClient = nullptr;   // also used by mmfix.cpp
using PingThink_t = void(__fastcall*)(void*);
static PingThink_t g_PingThink = nullptr;

using RequestQueue_t = void(__fastcall*)(void*, int);
static RequestQueue_t o_RequestQueueForMatch = nullptr;

// re-measure every datacenter through the hook. Clear the dirty flag FIRST -- the
// hook re-sets it if it is still faking, so after this call the flag tells the truth
// about what sits in the cache.
static void RefreshPings()
{
    if (!g_tfgcClient || !g_PingThink) return;
    s_pingsFaked = false;
    __try {
        *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(g_tfgcClient) + 1116) = true; // SetPendingPingRefresh
        g_PingThink(g_tfgcClient);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DebugLog("region: SEH during ping refresh");
    }
}

static void __fastcall h_RequestQueueForMatch(void* rcx, int eMatchGroup)
{
    MiscHookGuard _g;
    DebugLog("region: RequestQueueForMatch group=%d (regionEnable=%d mask=0x%X faked=%d)",
             eMatchGroup, (int)g_regionEnable, (unsigned)g_regionMask, (int)s_pingsFaked);
    // refresh when forcing (fakes must be fresh) AND when merely dirty (fakes left
    // over from an earlier enable must be washed out before this queue goes up).
    if ((g_regionEnable && g_regionMask) || s_pingsFaked)
        RefreshPings();
    o_RequestQueueForMatch(rcx, eMatchGroup);
}

// per-frame: whenever the force stops or the picked set changes, the cached pings are
// stale. Wash them right away so a disabled selector stops steering matchmaking (and
// the menu's ping list shows real numbers again) without waiting for a queue.
void Region_Tick()
{
    static bool s_lastActive = false;
    static int  s_lastMask   = 0;

    bool active = g_regionEnable && g_regionMask != 0;

    if ((s_lastActive && !active) || (active && g_regionMask != s_lastMask))
        RefreshPings();

    s_lastActive = active;
    s_lastMask   = g_regionMask;
}

void Region_Install()
{
    if (o_GetPingToDataCenter) return;

    HMODULE hClient = GetModuleHandleA("client.dll");
    if (!hClient) return;

    // resolve interface via Aletherium sig (client.dll wrapper writes ptr to out arg)
    if (!g_steamNetUtils)
    {
        uint8_t* addr = PatternScan(hClient,
            "40 53 48 83 EC ? 48 8B D9 48 8D 15 ? ? ? ? 33 C9 FF 15 ? ? ? ? 33 C9");
        if (!addr) { DebugLog("region: SteamNetworkingUtils sig NOT FOUND"); return; }
        __try {
            void* out = nullptr;
            using Fn = void*(*)(void*);
            void* ret = ((Fn)addr)(&out);
            g_steamNetUtils = out ? out : ret;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            DebugLog("region: SEH resolving SteamNetworkingUtils");
            return;
        }
    }
    if (!g_steamNetUtils) { DebugLog("region: SteamNetworkingUtils null"); return; }

    InstallVTableHook(g_steamNetUtils, 8, "region: GetPingToDataCenter",
                      (void*)h_GetPingToDataCenter, (void**)&o_GetPingToDataCenter);

    // resolve CTFGCClientSystem interface (getter sig = lea rax,[if]; ret -> call it)
    if (uint8_t* addr = PatternScan(hClient,
            "48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 8B 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24")) {
        __try { g_tfgcClient = ((void*(*)())addr)(); }
        __except(EXCEPTION_EXECUTE_HANDLER) { g_tfgcClient = nullptr; }
    }
    if (!g_tfgcClient) DebugLog("region: CTFGCClientSystem NOT resolved");

    // PingThink sig -> direct fn ptr, called with gc as this
    if (uint8_t* pt = PatternScan(hClient, "40 55 41 54 41 55 48 8D AC 24"))
        g_PingThink = (PingThink_t)pt;
    else DebugLog("region: PingThink sig NOT FOUND");

    // CTFPartyClient::RequestQueueForMatch -> pre-queue ping refresh
    InstallPatternHook(hClient, "region: RequestQueueForMatch",
                       "40 55 56 48 81 EC ? ? ? ? 48 63 F2",
                       (void*)h_RequestQueueForMatch, (void**)&o_RequestQueueForMatch);
}

// ==== net settings =================================================================

// IVEngineClient::ClientCmd (slot 7) -- queue console command
static void EngineExec(const char* cmd)
{
    if (!g_engine) return;
    __try {
        void** vt = *(void***)g_engine;
        if (reinterpret_cast<uintptr_t>(vt) < 0x10000) return;
        using Fn = void(__fastcall*)(void*, const char*);
        ((Fn)vt[7])(g_engine, cmd);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static ConVar* CV(const char* n) { return g_cvar ? g_cvar->FindVarWalk(n) : nullptr; }

// UnclampCVar is DELETED, do not re-add. It stripped m_bHasMin/m_bHasMax and
// FCVAR_NOT_CONNECTED off every convar a preset touched and then ForceFloat'd the raw number
// over the engine's own change callback, so the DLL could reach values VANILLA CANNOT --
// cl_updaterate 1000000, rate 1048576, mid-game swaps the engine refuses. That is what made
// the connection feel wrong: the server reads the RAW value out of userinfo, the client asks
// for a snapshot rate no server sends, and the two ends disagree about lerp all session.
// Everything goes through the console (EngineExec) with the engine's clamps intact, so a
// preset can only reach a state the player could type. FCVAR_NOT_CONNECTED vars (rate,
// cl_interp, cl_interp_ratio, cl_updaterate, cl_cmdrate) only apply in the MAIN MENU, which
// is why apply/restore retry below.

static const char* CVarString(const char* n)
{
    ConVarStr slot = ConVarStrSlot(CV(n));
    if (!slot.ppStr) return nullptr;
    const char* s = nullptr;
    __try { s = *slot.ppStr; }
    __except(EXCEPTION_EXECUTE_HANDLER) { s = nullptr; }
    return s;
}

// Convars the presets touch -- snapshot these to restore on disable.
// THIS LIST MUST COVER EVERY NAME ANY PRESET SETS. A name that is set but not listed
// here is never put back: that is how the old tables left net_maxcleartime,
// net_maxroutable, net_splitrate and hud_escort_interp forced for the rest of the
// session, long after "net settings" was unticked.
static const char* const kNetCvars[] = {
    "rate", "cl_cmdrate", "cl_updaterate", "cl_interp", "cl_interp_ratio",
    "cl_interpolate", "cl_lagcompensation", "cl_pred_optimize",
    "cl_smooth", "cl_smoothtime",
    "cl_predict", "cl_predictweapons", "cl_interp_all",   // added by cfg_snappy
    nullptr,
};
// Sized off the list itself, so adding a name can never overrun the snapshot.
static char s_saved[sizeof(kNetCvars) / sizeof(kNetCvars[0])][64];
static bool s_haveSnapshot = false;

// ==== THE RATE TRIO IS OPT-IN, AND OFF ==============================================
// rate / cl_cmdrate / cl_updaterate are not a lerp setting, they are the shape of your
// connection, and the block below reasoned about them backwards. "66 / 66 / 196608
// everywhere, anything else is packet loss" is true of a link with headroom and false of
// one without: the server reads all three out of userinfo (player.cpp:3484) and trims what
// it sends to fit `rate`. Raise the ceiling and it stops trimming -- the snapshots that no
// longer fit are dropped on the wire instead of being made smaller, and the drop moves from
// the server's choke counter to real loss on yours. It shows up exactly where the extra
// bytes are: standing in spawn is fine, walking into a fight is not.
//
// TF2's own defaults are 66 / 66 / 80000. Every preset here asked for 196608, so ticking
// "net settings" silently multiplied the player's snapshot budget by ~2.4 from the main
// menu, before the DLL had drawn anything. That is a connection change nobody asked for.
//
// So: g_netSettingsRates, default OFF. Off, the three lines below are SKIPPED and the
// player's own rate / cmdrate / updaterate are never read, written or restored. What is
// left is what the presets were actually picked for -- lerp, smoothing, prediction.
// The lines stay in the tables so turning the toggle on still gives the preset as written.

// ==== WHAT EVERY PRESET LOST, and must not get back =================================
// 1. THE WHOLE net_* BLOCK (net_compresspackets, net_maxroutable, net_maxfragments,
//    net_maxcleartime, net_splitrate, net_maxpacketdrop, net_chokeloop, ...). That is the
//    packet layer, not a lerp setting: MTU below what the route carries, a 1ms clear time
//    that lets the client flood its own send buffer, packet-drop detection off, and
//    net_chokeloop 1 -- a debug var that deliberately chokes the loopback. These are what
//    make a connection feel broken, and NONE were ever restored.
// 2. hud_escort_interp / cl_setupallbones / cl_movehelper_process* / cl_pred_doresetlatch --
//    hud lerp, bone setup and dev latching, not net settings, and also never restored.
// 3. THE FANTASY NUMBERS. cl_updaterate 1000000 and cl_cmdrate 1000000 only "worked" because
//    UnclampCVar tore the bounds off. Valve servers cap at sv_maxupdaterate (66) and clamp
//    the userinfo copy anyway, so the raw value bought nothing and left the two ends
//    disagreeing about lerp. rate is capped at TF2's own ceiling (196608).
// What each preset KEEPS is what actually distinguished it -- interp, interp ratio,
// smoothing, prediction -- all typeable in vanilla.
//
// ==== WHY EVERY PRESET NOW SAYS 66 / 66 / 196608 ====================================
// The rates are not a preset's flavour, they are its packet loss:
//  * cl_cmdrate ABOVE the server tick (klown's 65 is under it, boobsy's 100 is over) sends
//    commands the server has no tick to consume. TF2 runs at 66 tick and sv_maxcmdrate caps
//    there, so the surplus is upstream traffic that gets dropped -- loss on the netgraph, and
//    a choked send buffer on a home connection.
//  * cl_updaterate 100 asks for more snapshots than sv_maxupdaterate (66) will ever send. The
//    server clamps what it sends but the CLIENT keeps computing lerp off 100, so the two ends
//    disagree about the interpolation window all match.
//  * rate below what a full 24-32 player server pushes (80000, 90000, 100000, 131072 here)
//    makes the SERVER choke its own snapshots to fit the budget -- that is what shows up as
//    loss/choke even on a clean connection. 196608 is TF2's own ceiling.
// So: 66 update, 66 cmd, 196608 rate everywhere. What is left per preset is the actual lerp
// and prediction character each one was picked for.
static const char* const cfg_klown[] = {
    "cl_interp 0", "cl_interp_ratio 0", "cl_cmdrate 66", "cl_updaterate 66",
    "rate 196608", nullptr,
};
static const char* const cfg_murmur[] = { // network.cfg
    "cl_lagcompensation 1", "cl_cmdrate 66", "cl_updaterate 66", "rate 196608",
    "cl_pred_optimize 1", "cl_smooth 0", "cl_smoothtime 0.03",
    "cl_interp_ratio 0", "cl_interp 0.023", nullptr,
};
static const char* const cfg_boobsy[] = { // bby.cfg
    "cl_pred_optimize 1", "cl_smooth 1", "cl_smoothtime .01",
    "cl_cmdrate 66", "cl_updaterate 66", "cl_interp 0", "cl_interp_ratio 0",
    "rate 196608", nullptr,
};
static const char* const cfg_xcd[] = {
    "rate 196608", "cl_cmdrate 66", "cl_updaterate 66", "cl_interp_ratio 2",
    "cl_interp 0.033", "cl_lagcompensation 1", "cl_pred_optimize 2",
    "cl_smooth 0", "cl_smoothtime 0.01", nullptr,
};
static const char* const cfg_0lerp[] = {
    "cl_cmdrate 66", "cl_interp 0", "cl_interp_ratio 0", "cl_lagcompensation 1",
    "cl_pred_optimize 2", "cl_smooth 0", "cl_smoothtime 0", "cl_updaterate 66",
    "rate 196608", nullptr,
};
static const char* const cfg_15lerp[] = {
    "cl_cmdrate 66", "cl_interp 0", "cl_interp_ratio 1", "cl_lagcompensation 1",
    "cl_pred_optimize 2", "cl_smooth 0", "cl_smoothtime 0.01", "cl_updaterate 66",
    "rate 196608", nullptr,
};
static const char* const cfg_30lerp[] = { // 15lerp but cl_interp_ratio 2
    "cl_cmdrate 66", "cl_interp 0", "cl_interp_ratio 2", "cl_lagcompensation 1",
    "cl_pred_optimize 2", "cl_smooth 0", "cl_smoothtime 0.01", "cl_updaterate 66",
    "rate 196608", nullptr,
};
// spy: best legit hitreg per sdk-2013 src.
// Lag comp rewinds by latency + EXACT client lerp (player_lagcompensation.cpp:396,
// m_fLerpTime from userinfo, player.cpp:3510) -- knife Smack + backstab dot checks
// run inside that rewind (tf_weapon_knife.cpp:192,206), victim angles restored too.
// => stabs land on what screen shows at ANY lerp; only real lever = freshest view.
// Lowest lerp Valve servers allow = ratio 1 @ 66 update = 15.2ms (ratio 0 forced
// to 1 server-side, player.cpp:3494 -- ratio-0 presets gain nothing).
// cl_interpolate MUST stay 1: off => m_fLerpTime 0, rewind = latency only + jitter.
static const char* const cfg_spy[] = {
    "cl_cmdrate 66", "cl_updaterate 66",
    "cl_interp 0.0152", "cl_interp_ratio 1", "cl_interpolate 1",
    "cl_lagcompensation 1", "cl_pred_optimize 2",
    "cl_smooth 0", "cl_smoothtime 0.01",
    "rate 196608", nullptr,
};
static const char* const cfg_koy[] = { // koymu's lerp (OCT-5-2022)
    "cl_cmdrate 66", "cl_interp 0", "cl_interp_ratio 0", "cl_lagcompensation 1",
    "cl_pred_optimize 0", "cl_smooth 0", "cl_smoothtime 0", "cl_updaterate 66",
    "rate 196608", nullptr,
};
static const char* const cfg_lenny[] = {
    "cl_cmdrate 66", "cl_interp 0", "cl_interp_ratio 0", "cl_lagcompensation 1",
    "cl_pred_optimize 2", "cl_smooth 0", "cl_smoothtime 0", "cl_updaterate 66",
    "rate 196608", nullptr,
};
// snappy: trades smoothing for immediacy. Every line verified against sdk-2013.
// 1. cl_smooth 0 -- GetPredictionErrorSmoothingVector (c_baseplayer.cpp:2718) returns a ZERO
//    offset at 0, so a prediction error SNAPS instead of bleeding away over cl_smoothtime
//    (:2724). The one lever that literally removes smoothing, and also what makes it look
//    jerky when the server disagrees. cl_smoothtime 0.01 = engine min (:102).
// 2. cl_pred_optimize 0 -- 2 restores the last predicted frame and repredicts only the new
//    commands when the ack was clean (prediction.cpp:1471); 0 repredicts from the ack every
//    frame. Same state when nothing mispredicts, freshest when it does.
// 3. Lowest lerp Valve servers honour. GetClientInterpAmount is MAX(cl_interp,
//    cl_interp_ratio / cl_updaterate) (cdll_bounded_cvars.cpp:134) and ratio 0 is forced to 1
//    server-side (player.cpp:3494), so ratio 1 @ 66 = 15.2ms and cl_interp 0 lets the ratio
//    win. cl_interpolate stays 1: 0 => m_fLerpTime 0 (:3514) and lag comp rewinds by latency
//    alone.
// 4. cl_predict 1 + cl_predictweapons 1 -- the server reads both out of userinfo (:3518,3523),
//    and cl_predict 0 also blanks the smoothing vector (:2718), so "snappy" without it is just
//    latency.
// 5. cl_interp_all 0 -- 1 forces EVERY entity onto the interpolation list
//    (c_baseentity.cpp:2869). 0 is default and leaner; stated, not assumed.
// NOT included: cl_extrapolate* are FCVAR_CHEAT (c_baseentity.cpp:80,
// interpolatedvar.cpp:29), so a Valve server refuses them and the pump would retry for 20s
// and give up. cl_showerror / cl_pred_doresetlatch are diagnostics, not feel.
static const char* const cfg_snappy[] = {
    "cl_cmdrate 66", "cl_updaterate 66",
    "cl_interp 0", "cl_interp_ratio 1", "cl_interpolate 1", "cl_interp_all 0",
    "cl_lagcompensation 1", "cl_pred_optimize 0",
    "cl_predict 1", "cl_predictweapons 1",
    "cl_smooth 0", "cl_smoothtime 0.01",
    "rate 196608", nullptr,
};
static const char* const* const kNetCfgs[11] = {
    cfg_klown, cfg_murmur, cfg_boobsy, cfg_xcd, cfg_0lerp, cfg_15lerp, cfg_30lerp,
    cfg_spy, cfg_koy, cfg_lenny, cfg_snappy,
};

static void SnapshotNet()
{
    if (s_haveSnapshot) return;
    for (int i = 0; kNetCvars[i]; ++i) {
        const char* v = CVarString(kNetCvars[i]);
        strncpy(s_saved[i], v ? v : "", 63);
        s_saved[i][63] = 0;
    }
    s_haveSnapshot = true;
}

// One console set, then a read-back. Returns true when the convar ALREADY reads the
// value asked for -- ClientCmd only queues, so the pass that issues the command always
// returns false and the next pass is what confirms it.
//
// The read-back is the whole point: rate / cl_interp / cl_interp_ratio / cl_updaterate /
// cl_cmdrate are FCVAR_NOT_CONNECTED, so the engine REFUSES them while connected to a
// server. The old code hid that by tearing the flag off; here a refused set simply does
// not verify, and the pump retries until the player is back in the main menu (or gives
// up). Nothing is ever written past the engine.
static bool SetAndCheck(const char* name, const char* value)
{
    ConVar* c = CV(name);
    if (!c) return true;                      // not in this build: nothing to hold
    const float want = (float)atof(value);
    if (fabsf(c->GetFloat() - want) < 0.0001f) return true;

    char cmd[128];
    _snprintf(cmd, sizeof(cmd), "%s \"%s\"", name, value);
    EngineExec(cmd);
    return false;
}

// what the pump is currently trying to reach
static const char* const* s_wantCfg     = nullptr;   // preset being applied
static bool               s_wantRestore = false;     // player's own values going back
static int                s_tries       = 0;

static void ApplyNet(int sel)
{
    if (sel < 0 || sel >= (int)(sizeof(kNetCfgs) / sizeof(kNetCfgs[0]))) return;
    SnapshotNet();
    s_wantCfg     = kNetCfgs[sel];
    s_wantRestore = false;
    s_tries       = 0;
}

static void RestoreNet()
{
    if (!s_haveSnapshot) return;
    s_wantCfg     = nullptr;
    s_wantRestore = true;
    s_tries       = 0;
}

// Is the engine willing to take a FCVAR_NOT_CONNECTED set right now? That is TF2's own
// question, not "am I connected" -- see netcvar.h. Read through SEH: the team netvar is
// resolved by the movement pipeline and may not be there yet, and an unresolved offset must
// read as "alive on a team" (the strict answer), never as "go ahead".
static bool UserInfoChangeAllowed()
{
    if (!IsInGame()) return true;
    const bool bAlive = IsLocalPlayerAlive();
    int iTeam = netcv::kTeamRed;                     // strict default
    if (g_offTeamNum > 0) {
        const int idx = GetLocalPlayerIndex();
        void* pEnt = idx > 0 ? GetClientEntity(idx) : nullptr;
        if (pEnt) {
            __try { iTeam = *reinterpret_cast<int*>(
                        reinterpret_cast<uintptr_t>(pEnt) + g_offTeamNum); }
            __except(EXCEPTION_EXECUTE_HANDLER) { iTeam = netcv::kTeamRed; }
        }
    }
    return netcv::UserInfoChangeAllowed(true, bAlive, iTeam);
}

// what the menu prints under the preset combo
static int s_statSet = 0, s_statHeld = 0, s_statSkip = 0;
static bool s_statDone = true, s_statGaveUp = false;

void NetSettings_Status(char* buf, int cb)
{
    if (!buf || cb <= 0) return;
    buf[0] = 0;
    if (!g_netSettingsEnable) return;
    if (s_statGaveUp)
        _snprintf(buf, cb, "gave up: %d line(s) the engine would not take", s_statHeld + s_statSet);
    else if (s_statDone)
        _snprintf(buf, cb, "applied (%d set%s)", s_statSet,
                  s_statSkip ? ", rates left alone" : "");
    else if (s_statHeld)
        _snprintf(buf, cb, "%d line(s) waiting -- die, spectate or go to the main menu", s_statHeld);
    else
        _snprintf(buf, cb, "applying...");
    buf[cb - 1] = 0;
}

// Runs at 1 Hz while there is something to reach. Every convar is set through the
// console with the engine's own clamps and connect rules intact, then read back; the
// job ends when they all read what was asked.
//
// Two outcomes for "not done": HELD (a connection var while in a server -- the job stays
// pending, unchanged, and lands the next time the player is in the menu) and REFUSED
// (anything else, ~20 s of retries then give up). Only a refusal burns a try, so a long
// match no longer times the preset out and leaves half of it applied.
static void NetPump()
{
    if (!s_wantCfg && !s_wantRestore) return;

    static ULONGLONG s_next = 0;
    const ULONGLONG now = GetTickCount64();
    if (now < s_next) return;
    s_next = now + 1000;

    const bool bAllowed = UserInfoChangeAllowed();
    bool done = true;
    bool held = false;
    int  nSet = 0, nHeld = 0, nSkip = 0;

    // One line of the job. `ratesOptIn` is the player's toggle on the way IN and always true
    // on the way OUT -- a rate we once set has to come back even after the toggle is off.
    auto step = [&](const char* name, const char* value, bool ratesOptIn)
    {
        switch (netcv::Decide(netcv::Classify(name), ratesOptIn, bAllowed))
        {
        case netcv::kSkip:
            ++nSkip;
            return;
        case netcv::kHold: {
            // Already reads right (the player's cfg agrees with the preset)? Then it is not
            // held, it is done -- do not park the whole job on a line with nothing to do.
            ConVar* c = CV(name);
            if (!c || fabsf(c->GetFloat() - (float)atof(value)) < 0.0001f) { ++nSet; return; }
            done = false; held = true; ++nHeld;
            return;
        }
        default:
            if (SetAndCheck(name, value)) ++nSet;
            else                          done = false;
            return;
        }
    };

    if (s_wantCfg)
    {
        for (const char* const* p = s_wantCfg; *p; ++p)
        {
            char name[64];                    // split "name value"
            int  i = 0;
            const char* s = *p;
            for (; *s && *s != ' ' && i < 63; ++s) name[i++] = *s;
            name[i] = 0;
            while (*s == ' ') ++s;            // s -> value text
            step(name, s, g_netSettingsRates);
        }
    }
    else
    {
        for (int i = 0; kNetCvars[i]; ++i) {
            if (!s_saved[i][0]) continue;
            step(kNetCvars[i], s_saved[i], true);
        }
    }

    s_statSet = nSet; s_statHeld = nHeld; s_statSkip = nSkip;
    s_statDone = done; s_statGaveUp = false;

    if (!done && held) return;            // waiting on the main menu: never times out
    if (!done && ++s_tries < 20) return;
    if (!done) s_statGaveUp = true;

    // Only a VERIFIED restore drops the snapshot. Giving up keeps the player's original
    // values on file, so the next disable can still put them back instead of the
    // preset's numbers becoming the new "original".
    if (s_wantRestore && done) s_haveSnapshot = false;
    s_wantCfg     = nullptr;
    s_wantRestore = false;
    s_tries       = 0;
}

// ==== net graph text color ===========================================================
// TF2 net_graph text is drawn by client.dll CNetGraphPanel (vgui_netgraphpanel.cpp)
// through IMatSystemSurface::DrawColoredText. The color is a compiled-in constant --
// GRAPH_RED/GREEN/BLUE = 0.9/0.9/0.7 * 255 = 229/229/178 -- no convar exists for it.
// Confirmed in the live binary (client.dll rva 0x32911B in DrawTextFields):
//   mov edi,0E5h ; mov r15d,0B2h ; ... ; call qword ptr [rax+510h]
// 0x510/8 = vtable slot 162 = DrawColoredText on MatSystemSurface008.
// So: hook that slot and swap only calls carrying a known netgraph triple. Three are
// recolorable (vgui_netgraphpanel.cpp):
//   229/229/178  normal text (GRAPH_RED/GREEN/BLUE)
//   255/125/31   orange warn -- bad lerp (:787), CPU freq warn1 (:908)
//   255/31/31    red warn    -- sv framerate < 10 (:822), CPU freq warn2 (:900)
//   255/255/31   yellow warn -- lerp below cl_updaterate (:780)
//   255/255/0    yellow warn -- sv framerate 10..20 (:839)
// Both yellows are the same warn tier, so one picker drives them. The legend lines and
// every non-netgraph VGUI string stay engine-default.
static constexpr int k_drawColoredTextSlot = 162;
static constexpr int k_graphR  = 229, k_graphG  = 229, k_graphB  = 178;
static constexpr int k_orangeR = 255, k_orangeG = 125, k_orangeB = 31;
static constexpr int k_redR    = 255, k_redG    = 31,  k_redB    = 31;
static constexpr int k_yel1R   = 255, k_yel1G   = 255, k_yel1B   = 31;
static constexpr int k_yel2R   = 255, k_yel2G   = 255, k_yel2B   = 0;

// SDK: IMatSystemSurface::DrawColoredText returns int, not void (IMatSystemSurface.h:82).
using DrawColoredText_t = int(*)(void*, unsigned int, int, int, int, int, int, int, const char*, ...);
static DrawColoredText_t o_DrawColoredText = nullptr;

// return address of the DrawColoredText call inlined into Paint from DrawLargePacketSizes
// -- resolved in NetGraph_Install, used by the position shift below.
static uintptr_t s_ngLargePktRet = 0;

// Varargs cannot be forwarded, so format here and hand the trampoline a plain "%s".
// Same output for every caller; only the color changes, and only on netgraph text.
//
// That formatting is unavoidable once we are in here -- 1KB of stack and a _vsnprintf on
// EVERY colored VGUI string in the game, netgraph or not -- so the feature being off must
// not mean "run the hook and do nothing", it must mean the hook is not installed.
// NetGraph_Tick() below enables/disables the detour with the toggle.
static int h_DrawColoredText(void* thisptr, unsigned int font, int x, int y,
                             int r, int g, int b, int a, const char* fmt, ...)
{
    ++g_miscHookDepth;
    struct Dec { ~Dec() { --g_miscHookDepth; } } _dec;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt ? fmt : "", ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;

    if (g_ngColorEnable) {
        const float* pick = nullptr;
        if      (r == k_graphR  && g == k_graphG  && b == k_graphB)  pick = g_ngTextColor;
        else if (r == k_orangeR && g == k_orangeG && b == k_orangeB) pick = g_ngOrangeColor;
        else if (r == k_redR    && g == k_redG    && b == k_redB)    pick = g_ngRedColor;
        else if (r == k_yel1R   && g == k_yel1G   && b == k_yel1B)   pick = g_ngYellowColor;
        else if (r == k_yel2R   && g == k_yel2G   && b == k_yel2B)   pick = g_ngYellowColor;
        if (pick) {
            r = (int)(pick[0] * 255.f);
            g = (int)(pick[1] * 255.f);
            b = (int)(pick[2] * 255.f);
            a = (int)(pick[3] * 255.f);
        }
    }

    // DrawLargePacketSizes is the one netgraph draw the compiler inlined into Paint, so it
    // has no function of its own to hook -- its numbers reach the screen through here.
    // Gate on that one call site's return address (see s_ngLargePktRet) so the shift lands
    // on those strings and on nothing else in the game.
    if (g_ngPosEnable && s_ngLargePktRet &&
        reinterpret_cast<uintptr_t>(_ReturnAddress()) == s_ngLargePktRet) {
        x += g_ngPosX;
        y += g_ngPosY;
    }

    return o_DrawColoredText(thisptr, font, x, y, r, g, b, a, "%s", buf);
}

// CreateInterface("MatSystemSurface008") returned null on the live build, so take the pointer
// client.dll itself uses -- g_pMatSystemSurface, loaded right at the netgraph draw call in
// CNetGraphPanel::DrawTextFields:
//   48 8B 0D ? ? ? ?   mov rcx,[rip+X]   -> g_pMatSystemSurface
//   BF E5 00 00 00     mov edi,0E5h      = GRAPH_RED 229
//   FF 90 10 05 00 00  call [rax+510h]   = slot 0x510/8 = 162
// Sig verified unique (1 hit) against tf/bin/x64/client.dll.
void* FindMatSystemSurface()
{
    if (HMODULE hClient = GetModuleHandleA("client.dll")) {
        uint8_t* a = PatternScan(hClient,
            "48 8B 0D ? ? ? ? 48 8D 55 ? 48 89 54 24 ? BF E5 00 00 00");
        if (a) {
            __try {
                void* surf = *reinterpret_cast<void**>(a + 7 + *reinterpret_cast<int32_t*>(a + 3));
                if (reinterpret_cast<uintptr_t>(surf) >= 0x10000)
                    return surf;
                DebugLog("netgraph: g_pMatSystemSurface not set yet");
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                DebugLog("netgraph: SEH reading g_pMatSystemSurface");
            }
        }
        else DebugLog("netgraph: g_pMatSystemSurface sig NOT FOUND");
    }

    // Fallback: CreateInterface. Same object either way -- CMatSystemSurface has one
    // vtable (IMatSystemSurface derives from vgui::ISurface), so ISurface works too.
    const char* names[] = { "MatSystemSurface008", "MatSystemSurface007",
                            "VGUI_Surface031", "VGUI_Surface030", nullptr };
    for (int i = 0; names[i]; ++i) {
        if (void* s = GetInterface("vguimatsurface.dll", names[i]))
            return s;
    }
    return nullptr;
}

// ==== net graph position =============================================================
// TF2 exposes x only, and badly: net_graphpos is left/right/center or a raw pixel value that
// goes through XRES() (640-space, ~3px steps at 1920) AND a clamp to [5, width - w - 5].
// There is no y knob -- GraphGetXY (vgui_netgraphpanel.cpp:949) ends with a hardcoded
// *y = rect->y + rect->height - LERP_HEIGHT - 5.
//
// A FAKE SCREEN SIZE DOES NOT WORK, do not retry it: Paint clamps the graph width to the rect
// it was handed (w = MIN(TIMINGS, m_EstimatedWidth); if (vrect.width < w + 10) w = vrect.width
// - 10) and the right-anchored x is rect->width - 5 - w, so under w + 10 the two cancel --
// x sticks at 5 and the graph SQUEEZES instead of moving. Hook the four draws Paint hands its
// finished x/y to instead; nothing clamps those, on screen or off.
//   PaintLineArt(x, y, w, graphtype, maxmsgbytes)          bars + hatches
//   DrawTimes(vrect, cmdinfo, x, w, graphtype)             lerp strip, y off vrect
//   DrawTextFields(graphvalue, x, y, w, graph, cmdinfo)    fps/ping/in/out text
//   DrawLargePacketSizes -- INLINED into Paint, no entry point of its own; its numbers shift
//     in h_DrawColoredText, gated on that call site's return address.
// DrawTimes takes y from vrect, not the x/y pair, so shifting GraphGetXY's output alone would
// leave the lerp strip on the screen bottom. The struct is >8 bytes, so the x64 ABI passes a
// per-call copy by pointer -- writing through it moves that strip and nothing else.
// All four sigs verified unique (1 hit each) against tf/bin/x64/client.dll.
struct ng_vrect_t { int x, y, width, height; };

using PaintLineArt_t   = void(__fastcall*)(void*, int, int, int, int, int);
using DrawTimes_t      = void(__fastcall*)(void*, ng_vrect_t*, void*, int, int, int);
using DrawTextFields_t = void(__fastcall*)(void*, int, int, int, int, void*, void*);

static PaintLineArt_t   o_PaintLineArt   = nullptr;
static DrawTimes_t      o_DrawTimes      = nullptr;
static DrawTextFields_t o_DrawTextFields = nullptr;

static void __fastcall h_PaintLineArt(void* thisptr, int x, int y, int w,
                                      int graphtype, int maxmsgbytes)
{
    MiscHookGuard _g;   // eject drains this before the trampolines are freed
    if (g_ngPosEnable) { x += g_ngPosX; y += g_ngPosY; }
    o_PaintLineArt(thisptr, x, y, w, graphtype, maxmsgbytes);
}

static void __fastcall h_DrawTimes(void* thisptr, ng_vrect_t* vrect, void* cmdinfo,
                                   int x, int w, int graphtype)
{
    MiscHookGuard _g;
    if (g_ngPosEnable) {
        x += g_ngPosX;
        // the strip sits at vrect.y + vrect.height - 4, and this is Paint's own scratch
        // copy of the rect (rbp-29h), rebuilt for every call -- writing it moves nothing else
        if (vrect) vrect->y += g_ngPosY;
    }
    o_DrawTimes(thisptr, vrect, cmdinfo, x, w, graphtype);
}

static void __fastcall h_DrawTextFields(void* thisptr, int graphvalue, int x, int y,
                                        int w, void* graph, void* cmdinfo)
{
    MiscHookGuard _g;
    if (g_ngPosEnable) { x += g_ngPosX; y += g_ngPosY; }
    o_DrawTextFields(thisptr, graphvalue, x, y, w, graph, cmdinfo);
}

// addresses of the hooked functions, so NetGraph_Tick can arm/disarm the detours
static void* s_ngHookAddr  = nullptr;
static bool  s_ngHookOn    = false;
static void* s_ngPosAddr[3] = { nullptr, nullptr, nullptr };
// per-hook, NOT one shared latch: NetGraph_Tick (Present) can run before
// NetGraph_Install (PollThread) -- windowed vs fullscreen flips which wins the race.
// A single latch went true against an empty addr array, armed nothing, and then matched
// wantPos forever, so the hooks made a frame later were never enabled.
static bool  s_ngPosOn[3]   = { false, false, false };

void NetGraph_Install()
{
    if (!o_DrawColoredText) {
        void* surf = FindMatSystemSurface();
        if (!surf) DebugLog("netgraph: surface NOT found");
        // Created DISARMED -- NetGraph_Tick enables it only while a toggle wants it, so
        // no VGUI string in the game pays for a feature nobody switched on.
        else InstallVTableHook(surf, k_drawColoredTextSlot, "netgraph: DrawColoredText",
                               (void*)h_DrawColoredText, (void**)&o_DrawColoredText,
                               false, &s_ngHookAddr);
    }

    if (o_PaintLineArt) return;

    HMODULE hClient = GetModuleHandleA("client.dll");
    if (!hClient) return;

    struct HookDef { const char* name; const char* sig; void* hook; void** orig; int slot; };
    const HookDef defs[] = {
        { "netgraph: PaintLineArt",
          "48 8B C4 44 89 48 ? 44 89 40 ? 89 50 ? 55 56 41 55 41 57 48 8D 68",
          (void*)h_PaintLineArt,   (void**)&o_PaintLineArt,   0 },
        { "netgraph: DrawTimes",
          "4C 8B DC 4D 89 43 ? 56 48 81 EC 80 00 00 00 48 8B 05 ? ? ? ? 4D 8B D0 48 8B F1",
          (void*)h_DrawTimes,      (void**)&o_DrawTimes,      1 },
        { "netgraph: DrawTextFields",
          "89 54 24 ? 4C 8B DC 55 53 41 54 41 56 49 8D AB",
          (void*)h_DrawTextFields, (void**)&o_DrawTextFields, 2 },
    };

    // Created disarmed, like DrawColoredText -- NetGraph_Tick arms them with the toggle.
    for (const auto& d : defs) {
        if (uint8_t* addr = PatternScan(hClient, d.sig)) {
            if (InstallHookAt(addr, d.name, d.hook, d.orig, false))
                s_ngPosAddr[d.slot] = addr;
        }
        else DebugLog("%s: pattern NOT FOUND", d.name);
    }

    // DrawLargePacketSizes' inlined DrawColoredText call -- the sig ends ON the call, so
    // the return address is one instruction past its start.
    {
        static constexpr int k_largePktSigLen = 53;
        uint8_t* a = PatternScan(hClient,
            "48 8D 45 ? 48 89 44 24 ? 4C 89 6C 24 ? C7 44 24 ? FF 00 00 00 C7 44 24 ? FF 00 00 00 "
            "C7 44 24 ? FF 00 00 00 C7 44 24 ? FF 00 00 00 41 FF 92 10 05 00 00");
        if (a) s_ngLargePktRet = reinterpret_cast<uintptr_t>(a + k_largePktSigLen);
        else   DebugLog("netgraph: DrawLargePacketSizes call site NOT FOUND");
    }
}

// Arm/disarm on the toggles. Off = the detour is not in the code path at all, so no
// VGUI string in the game pays for a feature nobody switched on. The position hooks are
// netgraph-only (a few calls a frame), but the same rule keeps them honest.
//
// The recolor hook is also the position feature's only lever on the inlined
// DrawLargePacketSizes numbers, so either toggle arms it.
void NetGraph_Tick()
{
    if (s_ngHookAddr) {
        const bool want = (g_ngColorEnable || g_ngPosEnable) && !g_eject;
        if (want != s_ngHookOn) {
            if (want) MH_EnableHook((LPVOID)s_ngHookAddr);
            else      MH_DisableHook((LPVOID)s_ngHookAddr);
            s_ngHookOn = want;
        }
    }

    const bool wantPos = g_ngPosEnable && !g_eject;
    for (int i = 0; i < 3; ++i) {
        void* a = s_ngPosAddr[i];
        if (!a || wantPos == s_ngPosOn[i]) continue;   // no addr yet = retry next tick
        if (wantPos) MH_EnableHook((LPVOID)a);
        else         MH_DisableHook((LPVOID)a);
        s_ngPosOn[i] = wantPos;
    }
}

// Eject: one best-effort pass of the player's own values. No pump left to retry with,
// so if the engine refuses (in a server, FCVAR_NOT_CONNECTED) the console set is all
// there is -- same as the player typing it.
void NetSettings_Shutdown()
{
    if (!s_haveSnapshot || !g_cvar || !g_engine) return;
    for (int i = 0; kNetCvars[i]; ++i) {
        if (!s_saved[i][0]) continue;
        char cmd[128];
        _snprintf(cmd, sizeof(cmd), "%s \"%s\"", kNetCvars[i], s_saved[i]);
        EngineExec(cmd);
    }
    s_haveSnapshot = false;
    s_wantCfg      = nullptr;
    s_wantRestore  = false;
}

void NetSettings_Tick()
{
    static bool s_lastEnable = false;
    static int  s_lastSel    = -1;

    if (!g_cvar || !g_engine) return;

    if (g_netSettingsEnable) {
        if (!s_lastEnable || s_lastSel != g_netSettingsSel)
            ApplyNet(g_netSettingsSel);
    } else if (s_lastEnable) {
        RestoreNet();
    }

    NetPump();   // 1 Hz: set through the console, read back, stop when it took

    s_lastEnable = g_netSettingsEnable;
    s_lastSel    = g_netSettingsSel;
}
