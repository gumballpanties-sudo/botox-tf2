#pragma once
#include "../../globals/globals.h"

// Pixelsurf lines + full trajectory: connects the PS assist's saved targets with a route
// line (distinct from g_psAssistRender's points), and optionally the finder's scan order too.
// Pure data -- the draw loop lives inline in render.cpp's RenderPsOverlay, right after the
// assistPts point loop, so it reuses that function's own W2S/dl/d instead of a second copy.
inline bool  g_psLinesEnable = false;
inline bool  g_psLinesFull   = false;   // also connect the finder's scan-order points
inline float g_psLinesColor[4]     = { 1.f, 1.f, 1.f, 0.6f };
inline float g_psLinesFullColor[4] = { 0.6f, 0.6f, 1.f, 0.4f };
