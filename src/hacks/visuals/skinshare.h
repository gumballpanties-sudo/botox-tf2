#pragma once
#include <cstdio>
#include "skinshare_pure.h"

// Peer-to-peer skin sharing (Nikogram's "Networking" page): other botox users on the same
// server see your skinchanger + hat changer loadout, you see theirs. Transport is Steam's
// ISteamNetworkingMessages between the players' own Steam accounts -- no server, no backend.
//
// Threads: every Steam call lives on one worker (skinshare.cpp). The game thread (skins.cpp
// h_FrameStageNotify stage 5) only hands it the player list + our loadout bytes once a second and
// swaps in its results once a frame, under one mutex, so a slow SendMessageToUser never lands in a
// frame. The menu reads the three bools and the status text.

extern bool g_shareNet;    // networking on: probes every player on the server, 10 s
extern bool g_shareSend;   // "share my cosmetics"
extern bool g_shareRecv;   // "show shared cosmetics"

struct SharePeer {
    uint64_t    sid       = 0;
    int         playerIdx = 0;
    bool        ready     = false;   // echoed our nonce
    bool        remoteShare = false, remoteRecv = false;
    bool        hasLoadout = false;
    PeerLoadout lo;
};

void SkinShare_Tick();                       // game thread, in-game or not
void SkinShare_Shutdown();                   // eject (thread.cpp): stops + joins the worker
const SharePeer* SkinShare_Peer(int i);      // i < kShareMaxPeers; null = empty slot. Fixed slots: a
                                             // peer keeps its index while it lives (hats.cpp keys on it)
uint32_t SkinShare_LoadoutGen();             // bumps when any peer loadout appears / changes / leaves
const char* SkinShare_Status();              // menu line(s)

void SkinShare_SaveCfg(FILE* f);
bool SkinShare_LoadCfg(const char* key, const char* val);
