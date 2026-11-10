#pragma once
#include "../../globals/globals.h"
#include <cstdint>
#include <vector>

// cached Steam avatar textures by steamid64. Steam calls only happen on the GAME
// thread (Avatar_Pump, e.g. from h_CreateMove); the render thread never touches
// ISteamFriends/ISteamUtils and never reads a game-thread vector/string directly -- pixels
// cross over as a mutex-protected byte copy, same pattern mediaplayer.cpp's poll thread uses.
ImTextureID Avatar_Get(uint64_t steamid64, bool medium);   // RENDER THREAD: cached texture or 0, arms a fetch if missing

void Avatar_Pump();                       // GAME THREAD: call every tick, internally rate-limited per id
void Avatar_Tick(IDirect3DDevice9* dev);  // RENDER THREAD: uploads pixels Pump fetched
void Avatar_ReleaseAll();                 // RENDER THREAD: device Reset / eject -- drop every DEFAULT-pool texture
void Avatar_OnMapChange();                // GAME THREAD (LevelInit): old map's steamids are stale, drop next Avatar_Tick
// GAME THREAD: the 64x64 medium avatar as RGBA, for vgui surface textures (aim/teamcounter.cpp).
// false = not downloaded yet (asks Steam for it); the caller retries later, rate-limited
bool Avatar_FetchRGBA(uint64_t steamid64, std::vector<uint8_t>& rgba, int& w, int& h);
