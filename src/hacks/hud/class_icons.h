#pragma once
#include "../../globals/globals.h"

// TF2 scoreboard class icons (materials/hud/leaderboard_class_<class>.vtf as the GAME path serves
// them: a custom hud's own icons win over the vpk) as ImGui textures, for the esp class icon
// (aim/visuals/esp.cpp pushes ESPC_ICON, render.cpp replays it). Pixels decode once on the game thread,
// textures upload lazily on the render thread -- avatar_cache.cpp's split, without the per-id pump.
bool ClassIcons_Have(int cls);        // GAME THREAD: reads + decodes all 9 on the first call; true = TF_CLASS_* cls has an icon
ImTextureID ClassIcon_Get(int cls);   // RENDER THREAD: texture for cls, 0 = none / not decoded yet
void ClassIcons_ReleaseAll();         // RENDER THREAD: device Reset / eject (DEFAULT pool); pixels stay, Get rebuilds
