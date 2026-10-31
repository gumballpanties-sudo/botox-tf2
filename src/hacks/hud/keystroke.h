#pragma once
#include "../../globals/globals.h"

// Keystroke indicator: WASD + duck/jump grid + mouse-direction arrows. Distinct widget from
// movind.cpp's keybind tags. 1:1 with botox indicators.cpp key_press().
inline bool  g_ksEnable       = false;
inline int   g_ksLayout       = 0;     // 0/1 = the two botox grid variants
inline bool  g_ksDuckJump     = true;  // show the duck/jump row
inline bool  g_ksMouse        = true;  // show the < > mouse-direction arrows
inline int   g_ksReleased     = 0;     // 0 base/on cross, 1 colour lerp, 2 fade only
inline float g_ksColor[4]        = { 1.f, 1.f, 1.f, 1.f };
inline float g_ksColorReleased[4] = { 0.4f, 0.4f, 0.4f, 1.f };
inline float g_ksFade         = 0.08f; // seconds, 0 = instant snap
inline float g_ksScale        = 1.f;
inline int   g_ksGapX         = 4;
inline int   g_ksGapY         = 2;
inline int   g_ksOffsetX      = 0;
inline int   g_ksPosition     = 90;    // px up from the bottom
inline bool  g_ksShadow       = true;

void RenderKeystrokeIndicator();
