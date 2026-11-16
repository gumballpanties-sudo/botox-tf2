#pragma once
// aspect ratio "stretch hud" / "stretch crosshair" for TF2's own vgui (vgui_stretch.cpp), pure
// (tests/vgui_stretch_test.cpp).
//
// A real stretched res (1280x960 on a 1920x1080 monitor): vgui lays everything out in a 1280 wide
// screen and the monitor widens the picture. Here: vgui is told the screen is W' = H * ratio wide
// (ISurface::ForceScreenSizeOverride), lays out there, and every vgui paint is widened by W / W'
// about x = 0, so 0..W' lands on 0..W. Height is untouched (proportional hud sizes follow tall only).
#include <cmath>
#include <cstdlib>

namespace vs {

// Width vgui lays out at. 0 = nothing to do: junk input, or the wanted shape is the screen's own
// (within a pixel). Ratio clamped like the menu slider (botox 0.5..4), and never wider than the
// screen: botox csgo hud_aspect clamps the same way (std::clamp(wanted, 0.1f, screen)), and a
// layout wider than the screen would sit inside root panels the engine keeps at screen width --
// vgui clips children to the parent, so the right part would never paint or take a click.
inline int LayoutWidth(int screenW, int screenH, float ratio)
{
    if (screenW <= 0 || screenH <= 0 || !(ratio > 0.f)) return 0;
    ratio = ratio < 0.5f ? 0.5f : (ratio > 4.f ? 4.f : ratio);
    int w = (int)std::lround((double)screenH * ratio);
    if (w > screenW) w = screenW;
    if (w < 64 || screenW - w <= 1) return 0;
    return w;
}

// How much wider a layout pixel is on screen.
inline float Scale(int screenW, int layoutW) { return layoutW > 0 && screenW > 0 ? float(screenW) / float(layoutW) : 1.f; }

// The cursor: Windows hands vgui screen pixels, vgui hit-tests in layout pixels.
inline int ToLayout(int x, float s) { return s > 0.f ? (int)std::lround(x / s) : x; }
inline int ToScreen(int x, float s) { return (int)std::lround(x * s); }

// SetCursorPos(layout x) with the real cursor at curScreenX: a layout pixel covers s screen pixels,
// so a position already inside that layout pixel stays where it is. in_mouse.cpp:664 runs Get ->
// clamp -> Set every frame while a menu has the mouse; ToScreen(ToLayout(x)) is not x (1 in 3
// positions at s = 1.333 snap back), which would eat every slow 1 px move.
inline int SetTarget(int layoutX, int curScreenX, float s)
{
    return ToLayout(curScreenX, s) == layoutX ? curScreenX : ToScreen(layoutX, s);
}

// in_mouse.cpp ResetMouse: SetMousePos(engine->GetScreenSize / 2), real window pixels, the only
// place the game puts the cursor on the real centre. Its first call (ActivateMouse) can come while
// vgui still shows the cursor; mapping it would hand mouse look a centre off by W/2 * (s - 1).
inline bool IsLookCentre(int x, int y, int realW, int realH) { return x == (realW >> 1) && y == (realH >> 1); }

// What the main-thread tick has to do. applied = the override live now (0 = none), real = the
// backbuffer. old = the size panels are laid out at right now, which Panel::OnScreenSizeChanged
// needs to keep a full-screen panel full-screen (Panel.cpp:1017).
struct Change { bool any = false; bool on = false; int w = 0, h = 0, oldW = 0, oldH = 0; };
inline Change Next(int appliedW, int appliedH, bool wanted, int realW, int realH, float ratio)
{
    Change c;
    if (wanted && (realW <= 0 || realH <= 0)) return c;   // screen unknown this frame: hold
    const int w = wanted ? LayoutWidth(realW, realH, ratio) : 0;
    const int h = w ? realH : 0;
    if (w == appliedW && h == appliedH) return c;
    c.any = true;
    c.on = w > 0;
    c.w = w;
    c.h = h;
    c.oldW = appliedW ? appliedW : realW;
    c.oldH = appliedW ? appliedH : realH;
    return c;
}

// IMatRenderContext Translate / Scale multiply onto the RIGHT of the current matrix (materialsystem
// x64 CMatRenderContext slots 36 / 37: MatrixBuildTranslation / MatrixBuildScale, then
// MatrixMultiply(cur, op)), so the op issued LAST touches the vertex FIRST. x' = pivot + (x - pivot) * s
// is therefore Translate(+pivot), Scale(s), Translate(-pivot), in that order.
struct Op { int kind; float v; };   // kind 0 = translate x by v, 1 = scale x by v
inline int PivotOps(float pivot, float s, Op out[3])
{
    if (pivot == 0.f) { out[0] = { 1, s }; return 1; }
    out[0] = { 0, pivot };
    out[1] = { 1, s };
    out[2] = { 0, -pivot };
    return 3;
}

// TF2's crosshair is CHudCrosshair, panel name "HudCrosshair" (hud_crosshair.cpp:46); custom huds add
// font crosshairs as labels in hudlayout.res under names like "CustomCrosshair", "KnuCrosshair",
// "xHairDot". Case-insensitive "crosshair" or "xhair" anywhere in the name.
inline bool IsCrosshairName(const char* n)
{
    if (!n) return false;
    static const char* const k[] = { "crosshair", "xhair" };
    for (const char* key : k)
        for (const char* p = n; *p; ++p)
        {
            const char* a = p;
            const char* b = key;
            while (*a && *b && ((*a | 0x20) == *b)) { ++a; ++b; }
            if (!*b) return true;
        }
    return false;
}

} // namespace vs
