#pragma once
#include "../../globals/globals.h"
#include "movind.h"

// Decorative sub-features for movind.cpp's keybind tags (MI_*): particle burst on a trick
// landing, a stable per-slot random colour, and a custom label override. Self-contained vars.
inline bool g_miParticles    = false;
inline bool g_miRandomColor  = false;
inline bool g_miCustomLabels = false;
inline char g_miLabels[MI_COUNT][32] = {};
