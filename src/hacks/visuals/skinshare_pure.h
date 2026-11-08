#pragma once
// Peer-to-peer skin sharing: the wire format AND the peer state machine, no engine and no Steam
// in either -- tests/skinshare_test.cpp runs both (two cores talking through a fake transport).
// Reference: Nikogram's "Networking" page (Share my cosmetics / Show shared cosmetics,
// Steam connections, 10-second probes, nonce-echo handshake, 30 s peer expiry, no backend).
//
// One message, text, same lines as the cfg file:
//   BTXSK 1 <nonce> <echo> <share> <receive>\n      header, always
//   skin_<def>_<field>=value\n ...                   enabled weapons only (SkinPure_Format)
//   hat_<slot>=def style unusual paint\n             worn hats
// A peer is "ready" once a message from it echoes OUR nonce: proves it is live, sees us, and
// is on this session (the nonce is rerolled per server join). Loadout lines ride only on
// messages whose header says share=1, and only once the sender knows the receiver wants them.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include "skins_pure.h"
#include "hats.h"   // HatSlot, kHatSlots

constexpr int    kShareVer      = 1;
constexpr size_t kShareMaxBytes = 16384;   // one message; SteamNetworkingMessages caps at 512 KiB
constexpr int    kShareMaxSkins = 64;      // per peer; the menu has ~50 weapons
constexpr int    kShareMaxPeers = 8;       // ponytail: hat entity indices cap this (hats.cpp); raise both together

struct ShareHdr { uint32_t nonce = 0, echo = 0; bool share = false, receive = false; };

struct PeerLoadout {
    std::unordered_map<int, SkinCfg> skins;
    HatSlot hats[kHatSlots] = {};
    int     nSkins = 0;   // enabled entries (what the menu shows)
    int     nHats  = 0;
};

// ---- codec --------------------------------------------------------------------------------------
inline int Share_EncodeHdr(char* out, size_t cap, const ShareHdr& h)
{
    const int n = snprintf(out, cap, "BTXSK %d %u %u %d %d\n", kShareVer, h.nonce, h.echo,
                           h.share ? 1 : 0, h.receive ? 1 : 0);
    return (n > 0 && (size_t)n < cap) ? n : 0;
}

// The loadout lines alone. 0 = no room (never truncated).
inline size_t Share_EncodeBody(char* out, size_t cap, const std::unordered_map<int, SkinCfg>* skins, const HatSlot* hats)
{
    size_t len = 0;
    if (cap) out[0] = 0;
    if (skins) {
        int count = 0;
        for (const auto& [id, c] : *skins) {
            if (!c.enabled || count >= kShareMaxSkins) continue;
            const int w = SkinPure_Format(out + len, cap - len, id, c);
            if (!w) return 0;
            len += (size_t)w; ++count;
        }
    }
    if (hats)
        for (int i = 0; i < kHatSlots; ++i) {
            if (hats[i].def <= 0) continue;
            const int n = snprintf(out + len, cap - len, "hat_%d=%d %d %d %d\n", i, hats[i].def, hats[i].style,
                                   hats[i].unusual, hats[i].paint);
            if (n <= 0 || (size_t)n >= cap - len) return 0;
            len += (size_t)n;
        }
    return len;
}

inline size_t Share_Encode(char* out, size_t cap, const ShareHdr& h,
                           const std::unordered_map<int, SkinCfg>* skins, const HatSlot* hats)
{
    const int n = Share_EncodeHdr(out, cap, h);
    if (!n) return 0;
    if (!h.share) return (size_t)n;   // probe: header only
    const size_t b = Share_EncodeBody(out + n, cap - n, skins, hats);
    if (!b && (skins || hats)) {
        // nothing enabled is a legal empty body; a real encode failure leaves out[n] untouched
        bool any = false;
        if (skins) for (const auto& [id, c] : *skins) any |= c.enabled;
        if (hats) for (int i = 0; i < kHatSlots; ++i) any |= hats[i].def > 0;
        if (any) return 0;
    }
    return (size_t)n + b;
}

// Header line only. Returns the body offset (chars consumed incl. '\n'), 0 = not ours / malformed.
inline size_t Share_DecodeHdr(const char* data, size_t len, ShareHdr& h)
{
    if (!data || len < 6 || len > kShareMaxBytes || memcmp(data, "BTXSK ", 6) != 0) return 0;
    char line[96];
    size_t n = 0;
    while (n < len && n < sizeof(line) - 1 && data[n] != '\n') { line[n] = data[n]; ++n; }
    line[n] = 0;
    int ver = 0, share = 0, recv = 0; unsigned nonce = 0, echo = 0;
    if (sscanf(line, "BTXSK %d %u %u %d %d", &ver, &nonce, &echo, &share, &recv) != 5 || ver != kShareVer)
        return 0;
    h.nonce = nonce; h.echo = echo; h.share = share != 0; h.receive = recv != 0;
    return (n < len) ? n + 1 : n;
}

// Loadout lines -> a PeerLoadout (replaced whole). Trust boundary: everything is range-checked.
inline void Share_DecodeBody(const char* data, size_t len, PeerLoadout& lo)
{
    lo.skins.clear(); lo.nSkins = 0; lo.nHats = 0;
    for (HatSlot& s : lo.hats) s = { 0, 0, -1, -1 };
    if (!data || !len || len > kShareMaxBytes) return;
    char buf[kShareMaxBytes + 1];
    memcpy(buf, data, len); buf[len] = 0;

    char* save = nullptr;
    for (char* line = strtok_s(buf, "\n", &save); line; line = strtok_s(nullptr, "\n", &save)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char* key = line; const char* val = eq + 1;
        if (strncmp(key, "skin_", 5) == 0) {
            char* end = nullptr;
            const long def = strtol(key + 5, &end, 10);
            if (!end || *end != '_' || def <= 0 || def > 65535) continue;
            auto it = lo.skins.find((int)def);
            if (it == lo.skins.end()) {
                if ((int)lo.skins.size() >= kShareMaxSkins) continue;
                it = lo.skins.emplace((int)def, SkinCfg{}).first;
            }
            SkinPure_SetField(it->second, end + 1, val);
        } else if (strncmp(key, "hat_", 4) == 0) {
            const int i = atoi(key + 4);
            if (i < 0 || i >= kHatSlots) continue;
            HatSlot c = { 0, 0, -1, -1 };
            if (sscanf(val, "%d %d %d %d", &c.def, &c.style, &c.unusual, &c.paint) == 4 && c.def > 0 && c.def < 65536) {
                if (c.style < 0 || c.style > 15) c.style = 0;
                if (c.unusual < -1 || c.unusual > 10000) c.unusual = -1;
                if (c.paint < -1 || c.paint > 255) c.paint = -1;
                lo.hats[i] = c;
            }
        }
    }
    for (auto& [id, c] : lo.skins) { SkinPure_Sanitize(c); if (c.enabled) ++lo.nSkins; }
    for (const HatSlot& s : lo.hats) if (s.def > 0) ++lo.nHats;
}

// Whole message. false = not ours / malformed (drop it). `lo` is rewritten only when share=1.
inline bool Share_Decode(const char* data, size_t len, ShareHdr& h, PeerLoadout& lo)
{
    const size_t off = Share_DecodeHdr(data, len, h);
    if (!off) return false;
    if (h.share) Share_DecodeBody(data + off, len - off, lo);
    return true;
}

// FNV-1a over the encoded bytes: "did this peer's loadout change" without a field-wise diff.
inline uint32_t Share_Hash(const char* p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) { h ^= (uint8_t)p[i]; h *= 16777619u; }
    return h;
}

// ---- peer state machine -------------------------------------------------------------------------
// Owns nothing but bytes: the caller feeds it the server's player list, its own loadout body and
// every received message, and hands it a send callback `bool(uint64_t sid, const char*, size_t)`.
// Runs on whatever thread the transport lives on (skinshare.cpp: a worker, never the game thread --
// one SendMessageToUser / AcceptSessionWithUser was a 5-12 ms frame spike, debug.log 11-02).
struct ShareElig { uint64_t sid; int idx; char name[32]; };

struct ShareCore {
    struct Peer {
        uint64_t sid = 0; int idx = 0; char name[32] = {};
        uint32_t nonce = 0;                       // theirs; a change = they rejoined, handshake again
        bool     ready = false, rShare = false, rRecv = false;
        bool     logged = false;                  // owner's: "peer seen" line written
        uint32_t lastRx = 0;
        bool     hasLo = false; uint32_t loHash = 0; size_t loLen = 0;
        char     lo[kShareMaxBytes];              // their loadout lines, raw -- decoded by the owner
    };
    static constexpr uint32_t kProbeMs = 10000, kExpireMs = 30000, kMineMs = 1000;
    // A player who never answered gets kColdProbes probes, then none: each one is a Steam P2P
    // session attempt and steamclient prints "[SteamNetworkingSockets] No ISteamUtils: Using local
    // time!" per attempt (24-player server = 23 lines every 10 s). Discovery is two-way: a botox
    // player joining later probes us, and first contact is answered on the spot (OnMessage).
    static constexpr int kColdProbes = 2;
    struct Cold { uint64_t sid; int n; };
    Cold      cold[64]; int nCold = 0;            // only sids in elig (SetEligible prunes)

    Peer      peers[kShareMaxPeers];
    ShareElig elig[64]; int nElig = 0; int nPlayers = 0;
    uint32_t  myNonce = 0;                        // 0 = no session (not on a server)
    bool      share = false, recv = false;
    char      body[kShareMaxBytes]; size_t bodyLen = 0; uint32_t bodyHash = 0, sentHash = 0;
    uint32_t  lastProbe = 0, lastBody = 0;
    uint32_t  gen = 1;                            // bumps when any peer loadout appears / changes / leaves
    int       sent = 0, received = 0, dropped = 0, failed = 0;
    char      msg[kShareMaxBytes];                // send scratch

    Peer* Find(uint64_t sid) { for (Peer& p : peers) if (p.sid == sid) return &p; return nullptr; }
    const ShareElig* FindElig(uint64_t sid) const { for (int i = 0; i < nElig; ++i) if (elig[i].sid == sid) return &elig[i]; return nullptr; }

    void Drop(Peer& p) { if (p.hasLo) ++gen; p.sid = 0; p.idx = 0; p.ready = p.rShare = p.rRecv = p.hasLo = p.logged = false; p.nonce = p.lastRx = p.loHash = 0; p.loLen = 0; p.name[0] = 0; }
    void Reset() { for (Peer& p : peers) if (p.sid) Drop(p); myNonce = 0; nElig = 0; nPlayers = 0; lastProbe = 0; sentHash = 0; nCold = 0; }

    // false once sid has had its kColdProbes
    bool ColdProbe(uint64_t sid)
    {
        for (int i = 0; i < nCold; ++i)
            if (cold[i].sid == sid) return cold[i].n < kColdProbes ? (++cold[i].n, true) : false;
        if (nCold < 64) cold[nCold++] = { sid, 1 };
        return true;
    }

    // The server's player list, minus us. Peers that left it are dropped (same_server by construction).
    void SetEligible(const ShareElig* e, int n, int players)
    {
        nElig = n < 64 ? n : 64; nPlayers = players;
        if (n > 0) memcpy(elig, e, sizeof(ShareElig) * (size_t)nElig);
        for (Peer& p : peers) {
            if (!p.sid) continue;
            const ShareElig* el = FindElig(p.sid);
            if (!el) Drop(p); else p.idx = el->idx;
        }
        int k = 0;
        for (int i = 0; i < nCold; ++i) if (FindElig(cold[i].sid)) cold[k++] = cold[i];
        nCold = k;
    }
    // Our loadout lines; sent to ready peers that want them when the bytes change.
    void SetBody(const char* b, size_t n)
    {
        if (n > sizeof(body)) n = 0;
        if (n) memcpy(body, b, n);
        bodyLen = n; bodyHash = Share_Hash(body, n);
    }

    template <class Send> void SendTo(Peer* p, uint64_t sid, Send& send)
    {
        ShareHdr h; h.nonce = myNonce; h.echo = p ? p->nonce : 0; h.receive = recv;
        h.share = share && p && p->ready && p->rRecv;
        const int n = Share_EncodeHdr(msg, sizeof(msg), h);
        if (!n) { ++failed; return; }
        size_t len = (size_t)n;
        if (h.share) {
            if (bodyLen + len > sizeof(msg)) { ++failed; return; }
            memcpy(msg + len, body, bodyLen); len += bodyLen;
        }
        if (send(sid, msg, len)) ++sent; else ++failed;
    }

    template <class Send> void OnMessage(uint64_t sid, const char* data, size_t len, uint32_t now, Send& send)
    {
        const ShareElig* el = myNonce ? FindElig(sid) : nullptr;
        ShareHdr h;
        const size_t off = el ? Share_DecodeHdr(data, len, h) : 0;
        if (!off) { ++dropped; return; }
        Peer* p = Find(sid);
        bool reply = false;
        if (!p) {
            for (Peer& c : peers) if (!c.sid) { p = &c; break; }
            if (!p) { ++dropped; return; }
            p->sid = sid; memcpy(p->name, el->name, sizeof(p->name)); p->name[31] = 0;
            reply = true;   // first contact: answer now, not at the next 10 s probe
        }
        ++received;
        p->lastRx = now; p->idx = el->idx; p->rShare = h.share; p->rRecv = h.receive;
        if (h.nonce != p->nonce) { p->nonce = h.nonce; p->ready = false; reply = true; }   // (re)joined
        const bool was = p->ready;
        if (h.echo == myNonce) p->ready = true;
        // A reply carries our echo of THEIR nonce, which is what makes them ready; one round trip
        // from first contact to both sides ready, loadout on the second leg.
        if (reply || (p->ready && !was)) SendTo(p, sid, send);
        if (h.share && recv) {
            const size_t bl = len - off;
            const uint32_t hash = Share_Hash(data + off, bl);
            if (!p->hasLo || hash != p->loHash) {
                memcpy(p->lo, data + off, bl); p->loLen = bl; p->loHash = hash; p->hasLo = true; ++gen;
            }
        } else if (p->hasLo) {
            p->hasLo = false; p->loLen = 0; p->loHash = 0; ++gen;
        }
    }

    template <class Send> void Tick(uint32_t now, Send& send)
    {
        if (!myNonce) return;
        for (Peer& p : peers)
            if (p.sid && p.lastRx && now - p.lastRx > kExpireMs) Drop(p);
        if (bodyHash != sentHash && now - lastBody >= kMineMs) {
            sentHash = bodyHash; lastBody = now;
            for (Peer& p : peers) if (p.sid && p.ready && p.rRecv && share) SendTo(&p, p.sid, send);
        }
        if (!lastProbe || now - lastProbe >= kProbeMs) {
            lastProbe = now;
            for (int i = 0; i < nElig; ++i) {
                Peer* p = Find(elig[i].sid);
                if (p || ColdProbe(elig[i].sid)) SendTo(p, elig[i].sid, send);
            }
        }
    }

    int Status(char* out, size_t cap) const
    {
        int ready = 0, shown = 0;
        for (const Peer& p : peers) { ready += p.sid && p.ready; shown += p.hasLo; }
        int n = snprintf(out, cap, "%d peers ready / %d players, %d shown. sent %d recv %d drop %d fail %d",
                         ready, nPlayers, shown, sent, received, dropped, failed);
        for (const Peer& p : peers)
            if (p.sid && n > 0 && (size_t)n < cap - 48)
                n += snprintf(out + n, cap - (size_t)n, "\n%s: %s%s%s", p.name, p.ready ? "ready" : "probing",
                              p.rShare ? ", shares" : "", p.hasLo ? ", shown" : "");
        return n;
    }
};
