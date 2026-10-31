#pragma once
#include "../../globals/globals.h"

// Keybind indicators: short lowercase tags at the bottom centre, one per held movement bind,
// flashing the active colour on the tick that bind's trick lands. 1:1 with coldtires
// indicators.cpp keybind_indicators. Plain ImGui background-drawlist text, no window, no box.
void RenderMoveIndicators();

// Velocity indicator: 2D speed centred at the bottom, 1:1 with coldtires indicators.cpp velocity().
void RenderVelocityIndicator();

// Drops the mitosis morph texture. Called from h_Reset / a device rebind: the MANAGED pool
// survives a Reset but the DEFAULT-pool fallback does not, and neither survives a new device.
void MoveInd_ReleaseTextures();

// Slot order = draw order = bit order in g_miShowMask. Menu and renderer share this so a
// dropdown row can never point at a different tag than the one it turns off
// (coldtires e_keybind_indicators + its MultiCombo label list, same pairing).
enum MiSlot { MI_TB = 0, MI_AIR, MI_WC, MI_PS, MI_AST, MI_EB, MI_JB,
              MI_ST, MI_EJ, MI_LJ, MI_MJ, MI_COUNT };
extern const char* const kMiNames[MI_COUNT];   // menu rows: full words
extern const char* const kMiTags [MI_COUNT];   // on screen: short tags
