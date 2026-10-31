#pragma once
#include "../../globals/globals.h"

// Detections master + stack-edgebug: announces a landed trick as a fading toast line, and
// counts consecutive edgebugs into one "edgebug x3" line instead of one per tick.
// Render-thread only: polls MV_GetIndicators() the same way movind.cpp's tags do, so it needs
// no game-thread call and touches no shared file.
inline bool  g_detections       = false;
inline bool  g_detectionStackEb = false;
inline int   g_detectionGraceTicks = 8;   // miss-gap allowed before a streak is cut
inline float g_detectionColor[4] = { 1.f, 1.f, 1.f, 1.f };

void RenderDetections();
