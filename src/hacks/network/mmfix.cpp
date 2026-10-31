#include "mmfix.h"
#include "../../globals/globals.h"

// ==== matchmaker unstick ============================================================
// Symptom: main menu shows "match found" + "your party is in a match", Join Now does
// nothing, cannot queue. Restart fixes it; so does waiting ~20 min.
//
// Mechanism (client.dll x64, sdk-2013 tf_gc_client.cpp as the map):
//
//   CTFGCClientSystem::RequestAcceptMatchInvite   starts with
//       cmp qword [this+m_nAcceptingMatchLobbyID], 0 ; jne <bail>
//   CTFGCClientSystem::OnMatchInvitesUpdated      starts with the same compare, and
//       bails BEFORE firing the "match_invites_updated" event.
//
// m_nAcceptingMatchLobbyID is set when the accept msg goes out and cleared only in
// OnAcceptLobbyReply. If that reply never lands (GC hiccup / session drop mid-accept)
// the field stays hot forever, and then:
//   - Join Now is dead      (RequestAcceptMatchInvite bails)
//   - popup never updates   (OnMatchInvitesUpdated suppressed, so the panel never
//                            leaves STATE_LOBBY_INVITE_ACCEPTING and never cleans up)
//   - queue stays blocked   (still slotted for that invite)
// Restart = field starts at 0. ~20 min = reliable msg finally resolves. Matches every
// symptom, so that is what the fix button clears.
//
// Second suspect kept in the diag: m_eConnectState stuck on eConnectState_Connecting-
// ToMatchmade(1) after a failed connect -- BConnectedToMatchServer(true) then lies.
//
// Everything below is read out of the instructions, no hardcoded field offsets:
//   OnMatchInvitesUpdated  48 83 EC 28 48 83 B9 <off> 00 75 ? 48 8B 0D   -> accept off
//   AbandonCurrentMatch    (holds "Sending request to abandon current match")
//        8B 83 <off> FF C8 83 F8 01              -> m_eConnectState
//        48 8D 8B <off> E8 .. 84 C0 74 ? 80 BB <off>  -> server steamID, matchEnded
//        48 8B 8B <off> 48 85 C9                 -> m_pSOCache
//        BA D4 07 00 00 E8 ..                    -> CGCClientSharedObjectCache::
//                                                   FindBaseTypeCache
//   UpdateAssignedLobby    re-derives live-match state from the SO cache
// (verified offsets on the Jul 2026 client.dll: accept +0x750, conn +0x6C0,
//  svID +0x758, ended +0x761, socache +0x430)

extern void* g_tfgcClient;   // CTFGCClientSystem*, resolved by Region_Install()

bool g_mmFixReady = false;
char g_mmFixStatus[192] = "not scanned";

static constexpr int kSO_Lobby        = 2004;  // CTFGSLobby
static constexpr int kSO_LobbyInvite  = 2008;  // CTFLobbyInvite

static uintptr_t s_offAccept  = 0;   // m_nAcceptingMatchLobbyID (u64)
static uintptr_t s_offConn    = 0;   // m_eConnectState (int)
static uintptr_t s_offSvID    = 0;   // assigned match server CSteamID (u64)
static uintptr_t s_offEnded   = 0;   // m_bAssignedMatchEnded (byte)
static uintptr_t s_offSOCache = 0;   // m_pSOCache
static uintptr_t s_offChatSusp = 0;  // m_bAssignedMatchChatSuspension (byte)

static void  (__fastcall* s_OnInvitesUpdated)(void*)   = nullptr;
static bool  (__fastcall* s_UpdateAssignedLobby)(void*) = nullptr;
static void  (__fastcall* s_AbandonMatch)(void*)        = nullptr;
static void* (__fastcall* s_FindBaseTypeCache)(void*, int) = nullptr;

// ---- scan inside one function body (PatternScan wants a whole module) --------------
// Parser + matcher are common.h's now -- this used to carry a second, byte-for-byte
// copy of both, so a fix to one silently missed the other.
static uint8_t* ScanIn(uint8_t* start, size_t len, const char* ida)
{
    PatternScanToken pat[k_patternMaxTokens];
    const size_t plen = PatternParse(ida, pat, k_patternMaxTokens);
    if (!plen || plen > len) return nullptr;

    size_t anchor = 0;
    while (anchor < plen && pat[anchor].wild) ++anchor;
    if (anchor >= plen) return start;   // all wildcards: first offset matches

    return const_cast<uint8_t*>(PatternScanSpan(start, start + len, pat, plen, anchor));
}

static uint32_t Disp(uint8_t* at) { return *reinterpret_cast<uint32_t*>(at); }
static bool SaneOff(uintptr_t o) { return o > 0x10 && o < 0x4000; }

void MMFix_Install()
{
    if (g_mmFixReady) return;

    HMODULE hClient = GetModuleHandleA("client.dll");
    if (!hClient) return;

    // CTFGCClientSystem::UpdateAssignedLobby -- recomputes live-match state from SOs.
    // First, so the F2P chat bypass (needs only this one) survives the early return below.
    if (uint8_t* upd = PatternScan(hClient,
            "40 55 53 41 54 41 56 41 57 48 8B EC 48 81 EC ? ? ? ? 4C 8B F9 48 8B 89 ? ? ? ? 48 85 C9"))
    {
        s_UpdateAssignedLobby = reinterpret_cast<bool(__fastcall*)(void*)>(upd);
        // movzx ecx, byte [rax+45h]   lobby member proto chat_suspension
        // mov   [r15+off], cl         -> m_bAssignedMatchChatSuspension
        // (+0x760 on the Jul 2026 client.dll, unique in the module; Aletherium's
        //  SetNonPremiumAccount +1888 is this same byte)
        if (!s_offChatSusp)
            if (uint8_t* m = ScanIn(upd, 0x800, "0F B6 ? ? 41 88 8F ? ? 00 00"))
                s_offChatSusp = SaneOff(Disp(m + 7)) ? Disp(m + 7) : 0;
        if (!s_offChatSusp) DebugLog("mmfix: chat suspension store NOT FOUND, f2p chat bypass off");
    }
    else DebugLog("mmfix: UpdateAssignedLobby sig NOT FOUND");

    // CTFGCClientSystem::OnMatchInvitesUpdated -- also hands us the accept-id offset
    uint8_t* onInv = PatternScan(hClient, "48 83 EC 28 48 83 B9 ? ? ? ? 00 75 ? 48 8B 0D");
    if (!onInv) { DebugLog("mmfix: OnMatchInvitesUpdated sig NOT FOUND"); return; }
    s_OnInvitesUpdated = reinterpret_cast<void(__fastcall*)(void*)>(onInv);
    s_offAccept = Disp(onInv + 7);

    // CTFGCClientSystem::AbandonCurrentMatch -- body holds the rest of the offsets
    uint8_t* aban = PatternScan(hClient,
        "48 83 EC 68 48 89 5C 24 70 48 8B D9 48 8D 0D ? ? ? ? 48 89 74 24 78");
    if (aban)
    {
        s_AbandonMatch = reinterpret_cast<void(__fastcall*)(void*)>(aban);

        if (uint8_t* m = ScanIn(aban, 0x200, "8B 83 ? ? 00 00 FF C8 83 F8 01"))
            s_offConn = Disp(m + 2);
        if (uint8_t* m = ScanIn(aban, 0x200,
                "48 8D 8B ? ? 00 00 E8 ? ? ? ? 84 C0 74 ? 80 BB ? ? 00 00 00")) {
            s_offSvID  = Disp(m + 3);
            s_offEnded = Disp(m + 18);
        }
        if (uint8_t* m = ScanIn(aban, 0x200, "48 8B 8B ? ? 00 00 48 85 C9"))
            s_offSOCache = Disp(m + 3);
        // mov edx, 2004 / call FindBaseTypeCache
        if (uint8_t* m = ScanIn(aban, 0x200, "BA D4 07 00 00 E8"))
            s_FindBaseTypeCache = reinterpret_cast<void*(__fastcall*)(void*, int)>(
                m + 10 + *reinterpret_cast<int32_t*>(m + 6));
    }
    else DebugLog("mmfix: AbandonCurrentMatch sig NOT FOUND");

    if (!SaneOff(s_offAccept)) { DebugLog("mmfix: accept offset bogus (%llu)", (unsigned long long)s_offAccept); return; }
    if (!SaneOff(s_offConn))    s_offConn    = 0;
    if (!SaneOff(s_offSvID))    s_offSvID    = 0;
    if (!SaneOff(s_offEnded))   s_offEnded   = 0;
    if (!SaneOff(s_offSOCache)) s_offSOCache = 0;

    g_mmFixReady = true;
}

// ---- "am I on a matchmade server" --------------------------------------------------
// m_eConnectState (tf_gc_client.h): 0 Disconnected, 1 ConnectingToMatchmade,
// 2 ConnectedToMatchmade, 3 NonmatchmadeServer. 1 is set by ConnectToServer BEFORE the
// connect command goes out, so this already reads true while the map is loading -- which
// is when the sv_cheats push in visuals.cpp has to stand down.
//
// No offset -> true. Losing custom content on a community server is a cosmetic loss; a
// matchmaking queue that never completes is not.
bool MMFix_OnMatchmadeServer()
{
    if (!g_mmFixReady || !g_tfgcClient || !s_offConn) {
        // Answering blind costs both bypasses AND fog's sv_cheats push everywhere, so say so
        // once instead of leaving three features quietly dead.
        static bool s_said = false;
        if (!s_said) {
            s_said = true;
            DebugLog("mmfix: connstate unresolved (ready=%d gc=%p off=%llu) -- assuming matchmade,"
                     " bypasses and fog's sv_cheats push stay off",
                     (int)g_mmFixReady, g_tfgcClient, (unsigned long long)s_offConn);
        }
        return true;
    }
    int conn = 1;
    __try {
        conn = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(g_tfgcClient) + s_offConn);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return true; }
    return conn == 1 || conn == 2;
}

// ---- state read -------------------------------------------------------------------
struct MMState
{
    unsigned long long accept;
    unsigned long long svID;
    int   conn;
    int   ended;
    int   invites;
    int   lobbies;
};

static int SOCount(int typeID)
{
    if (!s_FindBaseTypeCache || !s_offSOCache || !g_tfgcClient) return -1;
    int n = -1;
    __try {
        void* soc = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(g_tfgcClient) + s_offSOCache);
        if (!soc) return 0;
        void* tc = s_FindBaseTypeCache(soc, typeID);
        n = tc ? *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(tc) + 0x28) : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { n = -1; }
    return n;
}

// counts walk the SO cache through a game function -- only worth it when something
// is actually being reported, not on every idle tick
static bool ReadState(MMState* st, bool withCounts = true)
{
    if (!g_mmFixReady || !g_tfgcClient) return false;
    st->accept = 0; st->svID = 0; st->conn = -1; st->ended = -1;
    st->invites = -1; st->lobbies = -1;
    __try {
        uintptr_t gc = reinterpret_cast<uintptr_t>(g_tfgcClient);
        st->accept = *reinterpret_cast<unsigned long long*>(gc + s_offAccept);
        if (s_offConn)  st->conn  = *reinterpret_cast<int*>(gc + s_offConn);
        if (s_offSvID)  st->svID  = *reinterpret_cast<unsigned long long*>(gc + s_offSvID);
        if (s_offEnded) st->ended = *reinterpret_cast<unsigned char*>(gc + s_offEnded);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (withCounts) {
        st->invites = SOCount(kSO_LobbyInvite);
        st->lobbies = SOCount(kSO_Lobby);
    }
    return true;
}

// ---- deferral -----------------------------------------------------------------------
// 0 = nothing queued, 1 = run the fix, 2 = abandon. Written by the render thread (menu),
// read and cleared by MMFix_Pump on the main thread. s_pumpSeen latches the first time a
// pump actually reaches us: until it does, a button press has no main thread to run on and
// falls back to running inline.
static std::atomic<int>  s_request{0};
static std::atomic<bool> s_pumpSeen{false};

static void MMFix_RunNow();
static void MMFix_AbandonNow();

// Main-thread tick, log only. The queue -> match found -> load -> bounce loop is invisible
// from outside the process; this names which step drops it. Edge-triggered, so a stuck state
// prints once and a retry loop prints one pair of lines per lap.
static void ConnTrace()
{
    if (!g_debugLogEnable || !g_mmFixReady || !g_tfgcClient || !s_offConn) return;

    static int s_last = -2;
    int conn = -2;
    __try {
        conn = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(g_tfgcClient) + s_offConn);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }

    if (conn == s_last) return;
    s_last = conn;

    static const char* const kNames[] = {
        "Disconnected", "ConnectingToMatchmade", "ConnectedToMatchmade", "NonmatchmadeServer"
    };
    DebugLog("mmfix: connstate -> %d (%s)", conn,
             (conn >= 0 && conn <= 3) ? kNames[conn] : "?");
}

// F2P chat bypass (Aletherium). The GC puts chat_suspension on free accounts in the lobby,
// UpdateAssignedLobby copies it into this byte, and ClientModeShared::StartMessageMode refuses
// to open the chat box while it is set ("#TF_Chat_Unavailable", clientmode_shared.cpp:842).
// All three run on this thread, so the byte is live for at most the one frame between a lobby
// update and this tick. Client side only: a Valve server reads its own copy of the lobby in
// CTFPlayer::CanPlayerTalk (tf_player.cpp:4171) and can still drop the text.
static void ChatSuspTick()
{
    if (!g_f2pChatBypass || !g_tfgcClient || !s_offChatSusp) return;
    __try {
        uint8_t* p = reinterpret_cast<uint8_t*>(g_tfgcClient) + s_offChatSusp;
        if (*p) { *p = 0; DebugLog("mmfix: cleared match chat suspension (f2p chat bypass)"); }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void MMFix_Pump()
{
    ConnTrace();
    ChatSuspTick();
    s_pumpSeen.store(true);
    const int req = s_request.exchange(0);
    if      (req == 1) MMFix_RunNow();
    else if (req == 2) MMFix_AbandonNow();
}

// Queue if a main-thread pump exists, otherwise do it here and say so.
static bool MMFix_Queue(int req, const char* what)
{
    if (!s_pumpSeen.load()) return false;
    s_request.store(req);
    _snprintf(g_mmFixStatus, sizeof(g_mmFixStatus), "%s queued...", what);
    g_mmFixStatus[sizeof(g_mmFixStatus) - 1] = 0;
    return true;
}

// ---- the button -------------------------------------------------------------------
void MMFix_Run()
{
    if (MMFix_Queue(1, "fix")) return;
    MMFix_RunNow();
}

static void MMFix_RunNow()
{
    if (!g_mmFixReady || !g_tfgcClient) {
        strcpy(g_mmFixStatus, "not ready (sigs unresolved)");
        return;
    }

    MMState pre;
    if (!ReadState(&pre)) { strcpy(g_mmFixStatus, "read failed"); return; }

    int acted = 0;
    uintptr_t gc = reinterpret_cast<uintptr_t>(g_tfgcClient);

    // 1. drop the stuck in-flight accept -> Join Now lives again, invites resume
    //    updating, and the popup gets to leave its ACCEPTING state.
    if (pre.accept != 0) {
        __try {
            *reinterpret_cast<unsigned long long*>(gc + s_offAccept) = 0ull;
            acted |= 1;
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    // 2. connect state stuck mid-connect while sitting in the menu
    if (s_offConn && pre.conn == 1 /* eConnectState_ConnectingToMatchmade */ && !IsInGame()) {
        __try {
            *reinterpret_cast<int*>(gc + s_offConn) = 0 /* eConnectState_Disconnected */;
            acted |= 2;
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    // 3. re-derive live-match state from the SO cache (what the game itself does on
    //    client_disconnect), then republish invites so the panels re-evaluate.
    __try {
        if (s_UpdateAssignedLobby && s_UpdateAssignedLobby(g_tfgcClient)) acted |= 4;
        if (s_OnInvitesUpdated) s_OnInvitesUpdated(g_tfgcClient);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    MMState post;
    ReadState(&post);

    _snprintf(g_mmFixStatus, sizeof(g_mmFixStatus),
              "%s%s%s%s accept %llu->%llu conn %d->%d",
              acted ? "cleared:" : "nothing stuck;",
              (acted & 1) ? " accept" : "", (acted & 2) ? " connstate" : "",
              (acted & 4) ? " lobby" : "",
              pre.accept, post.accept, pre.conn, post.conn);
    g_mmFixStatus[sizeof(g_mmFixStatus) - 1] = 0;
}

void MMFix_Abandon()
{
    if (MMFix_Queue(2, "abandon")) return;
    MMFix_AbandonNow();
}

static void MMFix_AbandonNow()
{
    if (!g_mmFixReady || !s_AbandonMatch || !g_tfgcClient) {
        strcpy(g_mmFixStatus, "abandon unavailable");
        return;
    }
    __try { s_AbandonMatch(g_tfgcClient); }
    __except(EXCEPTION_EXECUTE_HANDLER) { DebugLog("mmfix: SEH in AbandonCurrentMatch"); }

    strcpy(g_mmFixStatus, "sent abandon to GC");
}
