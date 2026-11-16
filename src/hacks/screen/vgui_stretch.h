#pragma once
#include <cstdint>

// Aspect ratio "stretch hud" / "stretch crosshair" on TF2's own vgui (the game hud, menus, console,
// the game's crosshair). Our ImGui overlays are aspect_stretch.cpp's.
//
// stretch hud = a real stretched res for the 2D layer: vgui is told the screen is H * ratio wide
// (ISurface::ForceScreenSizeOverride, the game's own split-screen override), every panel lays
// itself out there, and each vgui paint pass is widened back onto the real screen with the render
// context's model matrix. The cursor is mapped into the narrow layout while vgui shows it (menus),
// never while it is hidden (mouse look reads vgui's cursor too, in_mouse.cpp:684).
// stretch crosshair (hud off) = only the crosshair panels, widened about the screen centre.

void VguiStretch_Install();        // thread.cpp PollThread: PaintTraverseEx + the three cursor hooks
void VguiStretch_MainTick();       // skins.cpp h_FrameStageNotify FRAME_START: override + relayout (main thread, outside paint)
void VguiStretch_Eject();          // same hook's g_eject branch: override off + relayout back
bool VguiStretch_Holding();        // eject waits on this (thread.cpp): the narrow layout is live
void VguiStretch_EjectFallback();  // thread.cpp after that wait: no frame came, drop the override raw

// render.cpp h_PaintTraverse, around the original. Begin true = it pushed, call End after.
bool VguiStretch_PaintBegin(void* panels, uintptr_t vpanel);
void VguiStretch_PaintEnd();

// While the narrow layout is live, ISurface::GetScreenSize answers the layout, not the screen.
// Our own surface-space drawing (aim CDraw, esp W2S) wants the real screen: true + the real size.
bool VguiStretch_RealScreenSize(int& w, int& h);
// The width vgui lays out at while live, 0 = off. Drawing inside a vgui paint (scope_csgo) uses it.
int  VguiStretch_LayoutWidth();
