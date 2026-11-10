#pragma once
// ==== receive-buffer bump: the engine-free half ===========================================
// Tested by tests/netbuf_test.cpp. netbuf.cpp owns the ws2_32 hook and the Steam call.
//
// WHY. net_graph "loss" is incoming snapshots missing a sequence number
// (vgui_netgraphpanel.cpp:591, GetAvgLoss(FLOW_INCOMING)). crash.log shows ~1267 main-thread
// stalls of 100..999 ms outside map loads over 176 sessions. While the main thread is stalled
// nothing reads the socket; Windows hands a fresh UDP socket a 64 KB receive buffer
// (SO_RCVBUF 65536, measured), and a 24-32 player server at rate 196608 fills that in ~330 ms.
// Everything past it is dropped by the OS on THIS pc and shows up as loss. Bigger buffer =
// the stall still happens, the packets wait instead of dying.

#include <cstdint>

namespace netbuf {

// 4 MB: a 20 s stall at full rate. Windows takes it without SO_RCVBUFFORCE (measured 4194304).
constexpr int kWantRcvBuf = 4 * 1024 * 1024;

// Steam Datagram Relay (Valve casual/MM servers): no UDP socket of the engine's to grow, the
// game's traffic sits in steamclient's per-connection receive queue. Those two limits drop
// packets when full (steamnetworkingtypes.h: "If this limit is exceeded, packets will be
// dropped"). Defaults 512 KB / 2048 msgs. Both are GLOBAL config, so they apply to every
// connection opened after the set -- i.e. the next connect, not one already live.
constexpr int kSnsRecvBufferSize     = 47;   // k_ESteamNetworkingConfig_RecvBufferSize
constexpr int kSnsRecvBufferMessages = 48;   // k_ESteamNetworkingConfig_RecvBufferMessages
constexpr int kWantSnsBytes          = 8 * 1024 * 1024;
constexpr int kWantSnsMessages       = 16384;

// 0 = leave alone (already at least what we want, or unreadable), else the value to set.
inline int WantRcvBuf(int current)
{
    if (current < 0) return 0;              // getsockopt failed: do not write blind
    return current >= kWantRcvBuf ? 0 : kWantRcvBuf;
}

// Sockets already handled. recvfrom is the hottest import in the process (engine + steamclient,
// several threads), so the hit path is a scan of 8 words and the miss path is the only one
// that may do work. Fixed size: a game has one client socket, steamclient a handful; past 8
// we stop bumping rather than grow. Caller serialises Add; Contains is read-only.
struct Seen {
    static constexpr int kMax = 8;
    uintptr_t s[kMax] = {};
    int       n       = 0;

    bool Contains(uintptr_t sock) const
    {
        for (int i = 0; i < n; ++i) if (s[i] == sock) return true;
        return false;
    }
    // true = newly recorded (caller should bump it); false = known or full
    bool Add(uintptr_t sock)
    {
        if (Contains(sock) || n >= kMax) return false;
        s[n++] = sock;
        return true;
    }
};

} // namespace netbuf
