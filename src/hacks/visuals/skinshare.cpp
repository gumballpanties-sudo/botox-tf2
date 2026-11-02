#include "../../globals/common.h"
#include "../../globals/globals.h"
#include "../../globals/pinned_thread.h"
#include "skinshare.h"
#include "skins.h"
#include <atomic>
#include <cstddef>
#include <mutex>
#include <random>

// ==== peer-to-peer skin sharing =================================================================
// Nikogram (Desktop\hats\Nikogram-Public-x64-Release.dll, strings): "Automatic discovery: {} peers
// ready / {} players; {} Steam connections established; 10-second probes; no Nikogram backend",
// "network_handshake_ready peer={} same_server=true nonce_echo_verified=true remote_share={}
// remote_receive={}", "peer_expired_timeout_30_seconds", SteamNetworkingMessages002. The state
// machine itself is ShareCore (skinshare_pure.h). This file is the glue:
//   game thread (SkinShare_Tick, FSN stage 5): once a second the player list (GetPlayerInfo) and
//     our loadout body go into the mailbox; once a frame the worker's results come out of it and
//     the changed loadouts are decoded into s_peers (what skins.cpp / hats.cpp read, same thread).
//   worker (Worker): every 50 ms copies the mailbox in, pumps ISteamNetworkingMessages (receive,
//     probes, loadout sends) through ShareCore, copies the results out. ISteamNetworkingMessages is
//     thread-safe; the accessor is resolved on the game thread first. A send that blocks in
//     steamclient costs the worker, not a frame (the 5-12 ms "fsn/share" spikes, crash.log 11-02).
// Steam calls are the flat C exports of steam_api64.dll (the avatar cache's pattern).

bool g_shareNet  = false;
bool g_shareSend = false;
bool g_shareRecv = false;

// ---- Steam flat API -----------------------------------------------------------------------------
// steamnetworkingtypes.h: SteamNetworkingIdentity = { int m_eType; int m_cbSize; union(128) } = 136
// bytes, type 16 = steam id, size 8. SteamNetworkingMessage_t: only the leading fields are read.
struct SNIdentity { int32_t type; int32_t size; uint64_t steamID64; uint8_t pad[120]; };
static_assert(sizeof(SNIdentity) == 136, "SteamNetworkingIdentity is 136 bytes");
struct SNMessage {
    void*      data;
    int32_t    size;
    uint32_t   conn;
    SNIdentity peer;
    int64_t    connUserData;
    int64_t    usecReceived;
    int64_t    msgNumber;
    void*      pfnFree;
    void*      pfnRelease;
    int32_t    channel;
    int32_t    flags;
    int64_t    userData;
};
static_assert(offsetof(SNMessage, peer) == 16 && offsetof(SNMessage, channel) == 192, "SteamNetworkingMessage_t layout");

static constexpr int kChannel   = 0x4B53;                 // 'KS'
static constexpr int kSendFlags = 8 | 1 | 32;             // Reliable | NoNagle | AutoRestartBrokenSession

using SNM_Get_t     = void* (*)();
using SNM_Send_t    = int   (*)(void*, const SNIdentity*, const void*, uint32_t, int, int);
using SNM_Recv_t    = int   (*)(void*, int, SNMessage**, int);
using SNM_Accept_t  = bool  (*)(void*, const SNIdentity*);
using SNM_Close_t   = bool  (*)(void*, const SNIdentity*);
using SNM_Release_t = void  (*)(SNMessage*);

static struct {
    void*         self = nullptr;
    SNM_Send_t    send = nullptr;
    SNM_Recv_t    recv = nullptr;
    SNM_Accept_t  accept = nullptr;
    SNM_Close_t   close = nullptr;
    SNM_Release_t release = nullptr;
    bool          tried = false;
} s_api;

template <typename Fn>
static Fn Export(HMODULE h, const char* name) { return (Fn)GetProcAddress(h, name); }

static bool ResolveApi()
{
    if (s_api.self) return true;
    if (s_api.tried) return false;
    s_api.tried = true;
    HMODULE h = GetModuleHandleA("steam_api64.dll");
    if (!h) { DebugLog("skinshare: steam_api64.dll not loaded"); return false; }
    auto get = Export<SNM_Get_t>(h, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002");
    s_api.send    = Export<SNM_Send_t>(h, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser");
    s_api.recv    = Export<SNM_Recv_t>(h, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel");
    s_api.accept  = Export<SNM_Accept_t>(h, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser");
    s_api.close   = Export<SNM_Close_t>(h, "SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser");
    s_api.release = Export<SNM_Release_t>(h, "SteamAPI_SteamNetworkingMessage_t_Release");
    s_api.self = (get && s_api.send && s_api.recv && s_api.accept && s_api.close && s_api.release) ? get() : nullptr;
    DebugLog("skinshare: ISteamNetworkingMessages %p (exports %d%d%d%d%d%d)", s_api.self, !!get, !!s_api.send,
             !!s_api.recv, !!s_api.accept, !!s_api.close, !!s_api.release);
    return s_api.self != nullptr;
}

static SNIdentity Ident(uint64_t sid) { SNIdentity id = {}; id.type = 16; id.size = 8; id.steamID64 = sid; return id; }

// ---- mailbox ------------------------------------------------------------------------------------
// Game thread -> worker: what to do. Worker -> game thread: what it found. One mutex, held only
// for memcpy; neither side ever calls Steam or the engine while holding it.
struct MailIn {
    bool      active = false;          // networking on AND on a server
    uint32_t  nonce  = 0;              // this session's; 0 = none
    bool      share = false, recv = false;
    ShareElig elig[64]; int nElig = 0; int nPlayers = 0;
    char      body[kShareMaxBytes]; size_t bodyLen = 0;
    uint32_t  gen = 0;                 // bumps on every write
};
struct MailPeer {
    uint64_t sid = 0; int idx = 0; bool ready = false, rShare = false, rRecv = false, hasLo = false;
    uint32_t loHash = 0; size_t loLen = 0; char lo[kShareMaxBytes];
};
struct MailOut {
    MailPeer p[kShareMaxPeers];
    uint32_t gen = 0;                  // bumps on every publish
    char     status[512] = "off";
};
static std::mutex s_mx;
static MailIn    s_in;
static MailOut   s_out;
static std::atomic<bool> s_stop{ false };
static HANDLE    s_thread = nullptr;

// ---- worker -------------------------------------------------------------------------------------
static void Worker()
{
    ShareCore* core = new ShareCore();   // ~300 KB: heap, not this thread's stack
    MailIn*    in   = new MailIn();
    MailOut*   out  = new MailOut();
    uint32_t   seenIn = ~0u, lastStatus = 0, lastGen = 0;
    bool       wasActive = false;
    SNMessage* msgs[16];

    auto send = [](uint64_t sid, const char* data, size_t len) -> bool {
        const SNIdentity id = Ident(sid);
        s_api.accept(s_api.self, &id);   // a session THEY opened is taken up; no-op otherwise
        return s_api.send(s_api.self, &id, data, (uint32_t)len, kSendFlags, kChannel) == 1;   // k_EResultOK
    };

    while (!s_stop) {
        { std::lock_guard<std::mutex> g(s_mx); if (s_in.gen != seenIn) { *in = s_in; seenIn = in->gen; } }
        const uint32_t now = GetTickCount();

        if (!in->active) {
            if (wasActive) {
                for (ShareCore::Peer& p : core->peers)
                    if (p.sid) { const SNIdentity id = Ident(p.sid); s_api.close(s_api.self, &id); }
                core->Reset();
                DebugLog("skinshare: session closed");
            }
            wasActive = false;
        } else {
            if (!wasActive) { DebugLog("skinshare: session nonce %08X", in->nonce); lastGen = core->gen; }
            wasActive = true;
            core->myNonce = in->nonce; core->share = in->share; core->recv = in->recv;
            core->SetEligible(in->elig, in->nElig, in->nPlayers);
            if (in->bodyLen != core->bodyLen || memcmp(in->body, core->body, in->bodyLen) != 0)
                core->SetBody(in->body, in->bodyLen);

            for (;;) {
                const int n = s_api.recv(s_api.self, kChannel, msgs, 16);
                if (n <= 0) break;
                for (int i = 0; i < n; ++i) {
                    SNMessage* m = msgs[i];
                    const uint64_t sid = (m->peer.type == 16) ? m->peer.steamID64 : 0;
                    ShareCore::Peer* p = sid ? core->Find(sid) : nullptr;
                    const bool wasReady = p && p->ready;
                    if (sid && m->size > 0) core->OnMessage(sid, (const char*)m->data, (size_t)m->size, now, send);
                    else ++core->dropped;
                    s_api.release(m);
                    p = sid ? core->Find(sid) : nullptr;
                    if (!p) continue;
                    if (!p->logged) { p->logged = true; DebugLog("skinshare: peer '%s' seen", p->name); }
                    if (p->ready && !wasReady)
                        DebugLog("skinshare: peer '%s' ready (share=%d receive=%d)", p->name, p->rShare, p->rRecv);
                }
            }
            const uint32_t probeBefore = core->lastProbe;
            core->Tick(now, send);
            if (core->lastProbe != probeBefore) {
                char st[160]; core->Status(st, sizeof(st));
                if (char* nl = strchr(st, '\n')) *nl = 0;
                DebugLog("skinshare: probe %d players, %s", core->nElig, st);
            }
        }

        // Publish: on any loadout change, and once a second for the status / flags.
        if (core->gen != lastGen || now - lastStatus >= 1000 || !in->active) {
            lastGen = core->gen; lastStatus = now;
            for (int i = 0; i < kShareMaxPeers; ++i) {
                const ShareCore::Peer& p = core->peers[i];
                MailPeer& o = out->p[i];
                o.sid = p.sid; o.idx = p.idx; o.ready = p.ready; o.rShare = p.rShare; o.rRecv = p.rRecv;
                o.hasLo = p.hasLo; o.loHash = p.loHash; o.loLen = p.loLen;
                if (p.hasLo && p.loLen) memcpy(o.lo, p.lo, p.loLen);
            }
            if (!in->active) snprintf(out->status, sizeof(out->status), g_shareNet ? "waiting for a server" : "off");
            else core->Status(out->status, sizeof(out->status));
            std::lock_guard<std::mutex> g(s_mx);
            ++out->gen;
            s_out = *out;
        }
        Sleep(50);
    }
    for (ShareCore::Peer& p : core->peers)
        if (p.sid) { const SNIdentity id = Ident(p.sid); s_api.close(s_api.self, &id); }
    delete core; delete in; delete out;
}

void SkinShare_Shutdown()
{
    s_stop = true;
    if (s_thread) {
        if (WaitForSingleObject(s_thread, 3000) != WAIT_OBJECT_0) DebugLog("skinshare: worker did not stop in 3 s");
        CloseHandle(s_thread); s_thread = nullptr;
    }
}

// ---- game thread --------------------------------------------------------------------------------
// TF2's player_info_t (scoreboard.cpp has the same): friendsID is the 32-bit account id.
struct PlayerInfoSdk_t {
    char name[32]; int userID; char guid[33]; uint32_t friendsID; char friendsName[32];
    bool fakeplayer; bool ishltv; uint32_t customFiles[4]; uint8_t filesDownloaded;
};
static uint64_t Steam64(uint32_t acct) { return acct ? (0x0110000100000000ULL | acct) : 0; }

static SharePeer s_peers[kShareMaxPeers];
static uint32_t  s_peerHash[kShareMaxPeers];
static uint32_t  s_gen = 1, s_seenOut = 0;
static char      s_status[512] = "off";
static MailOut*  s_outCopy = nullptr;   // game-side copy of the mailbox (128 KB, heap)

const SharePeer* SkinShare_Peer(int i) { return (i >= 0 && i < kShareMaxPeers && s_peers[i].sid) ? &s_peers[i] : nullptr; }
uint32_t SkinShare_LoadoutGen() { return s_gen; }
const char* SkinShare_Status() { return s_status; }

static uint32_t NewNonce()
{
    static std::mt19937 gen(std::random_device{}());
    uint32_t n; do { n = gen(); } while (!n);
    return n;
}

// Into the mailbox: the player list (minus us, minus bots) + our loadout body.
static void PostInputs(bool on, uint32_t nonce)
{
    static char body[kShareMaxBytes];
    ShareElig elig[64]; int nElig = 0, nPlayers = 0;
    size_t bodyLen = 0;
    if (on && g_engine) {
        const int local = GetLocalPlayerIndex();
        for (int i = 1; i <= 64; ++i) {
            PlayerInfoSdk_t pi = {};
            if (!VCall<bool>(g_engine, 8, i, &pi) || pi.fakeplayer || pi.ishltv) continue;
            ++nPlayers;
            const uint64_t sid = Steam64(pi.friendsID);
            if (!sid || i == local || nElig >= 64) continue;
            elig[nElig] = { sid, i, {} }; memcpy(elig[nElig].name, pi.name, 32); elig[nElig].name[31] = 0; ++nElig;
        }
        bodyLen = Share_EncodeBody(body, sizeof(body), &g_skinCfgs, g_hatSlots);
    }
    std::lock_guard<std::mutex> g(s_mx);
    s_in.active = on; s_in.nonce = nonce; s_in.share = g_shareSend; s_in.recv = g_shareRecv;
    s_in.nElig = nElig; s_in.nPlayers = nPlayers;
    if (nElig) memcpy(s_in.elig, elig, sizeof(ShareElig) * (size_t)nElig);
    s_in.bodyLen = bodyLen; if (bodyLen) memcpy(s_in.body, body, bodyLen);
    ++s_in.gen;
}

// Out of the mailbox: peers as skins.cpp / hats.cpp read them; only changed loadouts are decoded.
static void TakeOutputs()
{
    if (!s_outCopy) s_outCopy = new MailOut();
    {
        std::lock_guard<std::mutex> g(s_mx);
        if (s_out.gen == s_seenOut) return;
        s_seenOut = s_out.gen;
        *s_outCopy = s_out;
    }
    memcpy(s_status, s_outCopy->status, sizeof(s_status));
    for (int i = 0; i < kShareMaxPeers; ++i) {
        const MailPeer& o = s_outCopy->p[i];
        SharePeer& p = s_peers[i];
        if (p.sid != o.sid) {   // slot changed hands (or emptied): nothing of the old peer stays
            if (p.hasLoadout) ++s_gen;
            p = SharePeer{}; s_peerHash[i] = 0;
        }
        p.sid = o.sid; p.playerIdx = o.idx; p.ready = o.ready; p.remoteShare = o.rShare; p.remoteRecv = o.rRecv;
        if (o.sid && o.hasLo) {
            if (!p.hasLoadout || o.loHash != s_peerHash[i]) {
                Share_DecodeBody(o.lo, o.loLen, p.lo);
                s_peerHash[i] = o.loHash; p.hasLoadout = true; ++s_gen;
                DebugLog("skinshare: loadout from peer %d: %d skins, %d hats", i, p.lo.nSkins, p.lo.nHats);
            }
        } else if (p.hasLoadout) {
            p.hasLoadout = false; p.lo.skins.clear(); p.lo.nSkins = p.lo.nHats = 0; s_peerHash[i] = 0; ++s_gen;
        }
    }
}

void SkinShare_Tick()
{
    static DWORD    s_lastPost = 0;
    static bool     s_wasOn = false;
    static uint32_t s_nonce = 0;
    const bool on = g_shareNet && !g_eject && IsInGame();
    if (on && !ResolveApi()) { snprintf(s_status, sizeof(s_status), "steam networking unavailable"); return; }
    if (on && !s_thread && !s_stop) {
        s_thread = CreatePinnedThread([] { Worker(); });
        if (!s_thread) { DebugLog("skinshare: CreateThread failed (err %lu)", GetLastError()); return; }
    }
    if (!s_thread) { snprintf(s_status, sizeof(s_status), g_shareNet ? "waiting for a server" : "off"); return; }

    const DWORD now = GetTickCount();
    if (on != s_wasOn || !s_lastPost || now - s_lastPost >= 1000) {
        s_wasOn = on; s_lastPost = now;
        if (on && !s_nonce) s_nonce = NewNonce();
        if (!on) s_nonce = 0;
        PostInputs(on, s_nonce);
    }
    TakeOutputs();
}

void SkinShare_SaveCfg(FILE* f)
{
    fprintf(f, "shareNet=%d\nshareSend=%d\nshareRecv=%d\n", g_shareNet ? 1 : 0, g_shareSend ? 1 : 0, g_shareRecv ? 1 : 0);
}

bool SkinShare_LoadCfg(const char* key, const char* val)
{
    if      (strcmp(key, "shareNet")  == 0) g_shareNet  = atoi(val) != 0;
    else if (strcmp(key, "shareSend") == 0) g_shareSend = atoi(val) != 0;
    else if (strcmp(key, "shareRecv") == 0) g_shareRecv = atoi(val) != 0;
    else return false;
    return true;
}
