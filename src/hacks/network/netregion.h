#pragma once
#include "../../globals/common.h"

// ==== convenience: region selector + net settings ====================================

// Region selector: hook ISteamNetworkingUtils::GetPingToDataCenter (slot 8). When
// enabled + regions picked, forces matchmaking to only see selected datacenters.
extern void* g_steamNetUtils;
void Region_Install();               // resolve interface + install hook (poll thread)
void Region_Tick();                  // per-frame; drops cached fake pings on disable

// 29 region labels, index = bit position in g_regionMask
extern const char* const kRegionNames[29];

// Net settings: exec one of several network cfgs and snapshot/restore prior convars.
// Every set goes through the console with the ENGINE'S OWN CLAMPS AND CONNECT RULES
// INTACT -- a preset can only reach a state the player could type in vanilla, and the
// FCVAR_NOT_CONNECTED family (rate / cl_interp(_ratio) / cl_updaterate / cl_cmdrate)
// still only applies in the main menu. Values are read back and retried at 1 Hz.
void NetSettings_Tick();             // per-frame; applies/restores on state change
void NetSettings_Shutdown();         // eject: best-effort restore of the player's values
// One line for the menu: applied / waiting / gave up. "Nothing changed" is usually the
// engine refusing a FCVAR_NOT_CONNECTED set, and that used to be invisible.
void NetSettings_Status(char* buf, int cb);

// Net graph text color: hook IMatSystemSurface::DrawColoredText (slot 162) and swap
// the engine's hardcoded 229/229/178 netgraph text color for the picked one.
//
// Net graph position: hook the draws CNetGraphPanel::Paint hands its finished x/y to
// (PaintLineArt / DrawTimes / DrawTextFields, plus the inlined DrawLargePacketSizes via
// the DrawColoredText hook) and add a free pixel offset. Past every engine clamp, so the
// graph goes anywhere -- off screen included. TF2 itself has no y knob at all.
void NetGraph_Install();             // creates all detours, leaves them disarmed
void* FindMatSystemSurface();        // client.dll's g_pMatSystemSurface, CreateInterface fallback
void NetGraph_Tick();                // arms/disarms with the toggles (the recolor hook
                                     // formats every colored VGUI string and the position
                                     // hook runs for every panel, so "off" must mean gone)
