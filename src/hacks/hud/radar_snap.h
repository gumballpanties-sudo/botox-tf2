#pragma once
#include "radar_pure.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

// csgo radar: game thread (aim/radar_aim.cpp, IEngineVGui::Paint) -> render thread (hud/radar.cpp).
// Same split as hud_snap.h: BuildImGuiFrame may run on the render thread, which must not walk
// entities or trace, so the game thread publishes POD copies here.

inline constexpr int kRadarSlots = 102;   // player entindex 1..101 (TF2 MAX_PLAYERS)

struct RadarPlayer
{
    float x = 0, y = 0, z = 0, yaw = 0;
    int   team = 0;                 // TF_TEAM_RED 2 / TF_TEAM_BLUE 3
    bool  valid = false;            // a connected player in this slot
    bool  active = false;           // !dormant: position + angle are live this frame
    bool  alive = false;
    bool  spotted = false;          // enemy seen by the local player or a teammate (or show all)
    bool  local = false;
    bool  selected = false;         // the observer target
};

struct RadarSnap
{
    uint64_t stampMs = 0;           // 0 / stale = draw nothing
    bool     haveMap = false;       // ov below is valid
    uint32_t bakeId = 0;            // the overview image for this map = RadarImage::id == bakeId
    rdr::Overview ov;
    float    camX = 0, camY = 0, camZ = 0, viewYaw = 0;
    int      localTeam = 0;
    bool     spec = false;          // spectator / SourceTV: every player in team colours
    RadarPlayer players[kRadarSlots];
};

inline std::mutex g_radarSnapLock;
inline RadarSnap  g_radarSnap;      // under g_radarSnapLock

// the baked overview, published once per map when the bake finishes
struct RadarImage
{
    uint32_t id = 0;                // bumps per bake
    int      n = 0;                 // n x n texels, D3DFMT_A8R8G8B8
    std::vector<uint32_t> px;
};
inline std::mutex g_radarImageLock;
inline RadarImage g_radarImage;     // under g_radarImageLock
inline std::atomic<uint32_t> g_radarImageId{ 0 };

// menu status line: source 0 none / 1 nav mesh / 2 world bounds, bake 0..100 (-1 idle)
inline std::atomic<int> g_radarSource{ 0 };
inline std::atomic<int> g_radarAreas{ 0 };
inline std::atomic<int> g_radarBakePct{ -1 };
