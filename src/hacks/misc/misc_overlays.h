#pragma once
#include "../../globals/globals.h"

// botox overlays group, the small ones (misc):
//  - healthshot effect: botox sets CS:GO's m_flHealthShotBoostExpirationTime and the engine draws
//    its effects/healthboost screen overlay. TF2 has neither the netvar nor the material, so the
//    overlay is drawn here (edge vignette + pulse) for the same duration. Triggers = botox's:
//    0 kill (player_death, attacker = you), 1 edgebug, 2 pixelsurf (detections.cpp).
//  - low fps warning: botox indicators.cpp fps_warning 1:1 (fps under the tickrate).
//  - displayed logs: botox g_logger (top-left feed) + console line, off player_hurt / vote_cast.
//  - console color: botox draw_set_color.cpp -- ISurface::DrawSetColor tinted while the console
//    panel paints.
void Healthshot_Trigger(int trigger);                    // any thread
void RenderMiscOverlays();                               // imgui frame build
void MiscLogs_OnGameEvent(void* pEvent, const char* name, int localIdx);   // FireEvent thread
void MiscLogs_Push(const char* prefix, const char* text);                    // any thread (lua client.log)

void ConsoleTint_Install();                              // PollThread, after vgui is up
bool ConsoleTint_Begin(void* panels, uintptr_t vpanel);  // PaintTraverse, before the original
void ConsoleTint_End();                                  // ... and after it, when Begin said true
