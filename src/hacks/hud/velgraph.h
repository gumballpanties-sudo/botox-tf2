#pragma once
#include "../../globals/globals.h"

// Velocity graph: a scrolling 2D-speed line graph, distinct widget from the velocity number
// (render.cpp RenderVelocityIndicator). 1:1 with botox indicators.cpp velocity_graph().
// Self-contained vars (not in globals.h/dllmain.cpp) -- config.cpp only takes their address.
inline bool  g_vgEnable        = false;
inline int   g_vgLength        = 128;   // ticks of history, 2..2048
inline int   g_vgWidth         = 200;
inline int   g_vgHeight        = 50;
inline int   g_vgOffsetX       = 0;
inline int   g_vgPosition      = 140;   // px up from the bottom
inline float g_vgMax           = 0.f;   // 0 = auto-scale to the tallest sample + ease
inline bool  g_vgFadeSides     = false;
inline float g_vgFade          = 0.15f; // fraction of width faded at each edge
inline float g_vgColor[4]        = { 1.f, 1.f, 1.f, 1.f };
inline float g_vgColorGain[4]    = { 0.3f, 1.f, 0.3f, 1.f };
inline float g_vgColorLoss[4]    = { 1.f, 0.3f, 0.3f, 1.f };
inline float g_vgFillColor[4]    = { 1.f, 1.f, 1.f, 0.15f };
inline bool  g_vgGainColors    = false;
inline bool  g_vgFill          = false;
inline float g_vgThickness     = 1.5f;
inline bool  g_vgJumpSpeeds    = false; // label the takeoff sample with its speed
inline float g_vgTextColor[4]    = { 1.f, 1.f, 1.f, 1.f };
inline bool  g_vgTextShadow    = true;
inline float g_vgTextScale     = 1.f;

void RenderVelocityGraph();
