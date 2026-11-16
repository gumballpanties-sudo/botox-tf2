#pragma once
#include "../../globals/globals.h"
#include <string>
#include <vector>

// multihud: C:\botox-tf2\huds\<name> = one hud, swapped live. g_multiHudOn off = tf/custom as the
// game mounted it; on = g_multiHud (folder name, "" = stock tf2 hud). mh::Pick turns them into one.
void MultiHud_MainTick();     // skins.cpp h_FrameStageNotify FRAME_START after the original: main thread, main menu too
void MultiHud_Eject();        // same hook's g_eject branch: puts the game's own mounts back
bool MultiHud_Holding();      // eject waits on this (thread.cpp): our mounts are still live
void MultiHud_Reapply();      // menu: run the current pick again next frame (hud files edited on disk)
// "OnScreenSizeChanged" (old size) to the viewport, every other branch, popups and the dashboard
// panels on the main menu, in that order (RebuildPanels notes). Main thread, outside paint.
// Panels posted to, -1 = could not (no aim KeyValues / interface): caller takes the surface path.
int  MultiHud_RebuildPanels(int oldW, int oldH);   // vgui_stretch.cpp: screen size override changed
std::vector<std::string> MultiHud_List();   // folder names in C:\botox-tf2\huds, sorted (menu)
const char* MultiHud_Status();              // last switch result (menu, any thread)

// prop diag (file C:\botox-tf2\mhpropdiag): true while a switch records what draws. Main thread.
extern bool g_mhPropDiag;
void MultiHud_PropDiagStatic(void** props, int count);   // world_fx3.cpp h_DrawStaticProps (not shadow depth)
void MultiHud_PropDiagEnt(int entIdx, const void* model); // visuals.cpp h_DrawModelExecute
