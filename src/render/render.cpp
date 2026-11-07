#include "render.h"
#include "../hooks/crashlog.h"   // CrashSpan: stutter probe
#include "../hacks/visuals/visuals.h"
#include "../hacks/hud/watermark.h"
#include "../hacks/misc/boot_screen.h"
#include "../dependencies/fonts/icon_font.h"   // botox icon_font_compressed_data (cog glyph)
#include "../hacks/hud/mediaplayer.h"
#include "../hacks/hud/avatar_cache.h"   // steam avatar textures
#include "../hacks/hud/gif_tex.h"        // embedded gif cache
#include "../hacks/screen/blur_rect.h"      // panel background blur
#include "../hacks/hud/indicators.h"   // botox indicators.cpp: velocity, graph, keybinds, keystrokes
#include "../hacks/hud/crosshair.h"
#include "../hacks/hud/hud_extra.h"   // fov circle, net graph, their fonts
#include "../hacks/hud/radar.h"       // csgo radar
#include "../hacks/menu/menu.h"
#include "../hacks/movement/movement.h"
#include "../hacks/network/netregion.h"
#include "../hacks/network/equipregion.h"
#include "../hacks/network/mmfix.h"
#include "../hacks/screen/colorcorrect.h"
#include "../hacks/screen/bloom.h"
#include "../hacks/screen/screenfx.h"
#include "../hacks/movement/detections.h"    // movement extras
#include "../hacks/movement/recorder.h"      // movement extras
#include "../hacks/movement/ps_lines.h"      // movement extras (vars only, draw loop appended into RenderPsOverlay below)
#include "../hacks/movement/points_style.h"  // movement > points page look (botox points_ui.cpp styles)
#include "../hacks/screen/screenfx_passes.h"   // sharpen/grain/vignette/invert/posterize/deband/channel-shift/emphasize/show-depth
#include "../hacks/visuals/world_fx.h"          // fullbright/disable-interp tick + aspect ratio dest rect
#include "../hacks/hud/hitmarker.h"         // misc HUD
#include "../hacks/misc/killeffects.h"
#include "../hacks/hud/points.h"
#include "../hacks/hud/spectator_list.h"
#include "../hacks/esp/esp_queue.h"  // botox m_draw_data: esp recorded on the game thread, replayed in RenderEspQueue
#include "../hacks/visuals/player_stencil.h"  // PlayerStencil_ReleaseTargets in h_Reset
#include "../hacks/visuals/lsd_chams.h"       // SheenNormal_Tick
#include "../hacks/menu/debugmenu.h"
#include "../hacks/menu/fonts.h"          // fonts tab: indicator face picked at runtime
#include "../hacks/lua/lua.h"             // lua paint callback (h_PaintTraverse)
#include "../hacks/lua/lua_draw.h"        // lua render.* replay (RenderLuaQueue)
#include "../dependencies/imgui/freetype/imgui_freetype.h"
#include "../dependencies/imgui/imgui_internal.h"
// botox dependencies/fonts/spectator_fonts.h: donor ttf bytes, FontDataOwnedByAtlas = false
#include "../dependencies/fonts/inter_semibold.h"
#include "../dependencies/fonts/clarity_icon_font.h"
#include "../dependencies/fonts/sf_pro_display_semibold.h"
#include "../dependencies/fonts/sf_pro_display_bold.h"
#include "../dependencies/fonts/rubik_regular.h"
#include "../dependencies/fonts/rubik_medium.h"
#include "../dependencies/fonts/inter_bold.h"
#include "../dependencies/fonts/inter_medium.h"
#include "../dependencies/fonts/sf_ui_display_semibold.h"
#include "../dependencies/fonts/montserrat_regular.h"
#include "../dependencies/fonts/montserrat_medium_italic.h"   // points big font
#include "../hacks/misc/misc_overlays.h"                      // healthshot / low fps / logs / console color
#include "../dependencies/fonts/sunflower_medium.h"
#include "../dependencies/fonts/droid_sans_bold.h"
#include "../dependencies/fonts/nunito_medium.h"
#include "../dependencies/fonts/dna_montserrat_medium.h"
#include "../dependencies/fonts/pt_root_ui_bold.h"
#include "../dependencies/fonts/dna_clarity_icon.h"
#include "../dependencies/fonts/minecraft_ttf.h"
#include <timeapi.h>
#include "../hacks/screen/aspect_stretch.h"   // AspectStretch_Run (aspect ratio overlay stretch)
#include "dpi.h"                              // botox dpi scale + dpi scale panels
#pragma comment(lib, "winmm.lib")

// ==== PS finder/assist world overlay ==============================================
// Marker look is a 1:1 port of delusional-fix features/movement/assist.cpp -- every size,
// duration, alpha and colour below quotes its reference line. Three departures, marked DIH:
//   DIH-1 project with dih's engine matrix and snap to the pixel grid. Without the snap a
//         1px AA stroke lands on subpixel coords and re-weights every frame the camera
//         breathes, which reads as marker shiver.
//   DIH-2 (gone 11-03: saved points now take botox's movement > points look, points_style.h)
//   DIH-3 their pulsing 3-axis world cross is dropped (user ask), so a marker never draws
//         over what it marks.
// Points come from the movement thread (PS_GetDrawData snapshot) and project with
// IVEngineClient::WorldToScreenMatrix, slot 36 (counted against Time=14 / SetViewAngles=20).

// Per-point ENTER window: aim at a saved point, press ENTER -> window opens and the view
// freezes (h_SetCursorPos eats the recenter, same as the menu). ENTER/ESC close, BACKSPACE
// or the button deletes. RenderPsOverlay sets the hover each frame, WndProcHook opens and
// closes it -- window thread writes, render thread reads, same pattern as g_showMenu.
static int  s_psPointHover = -1;               // assist point under the crosshair (-1 = none)
static int  s_psPointWin = -1;                 // open window's point index (-1 = closed)
static unsigned s_psPointWinGen = 0;           // points generation at open -- list changed = close
static unsigned long long s_psPointWinOpenMs = 0; // debounce: opening ENTER must not instantly close

namespace {

// ---- finder dots: botox points_ui.cpp pf_render_dots, 1:1 ---------------------------------
// botox's lists[] order: headbounce (red), pixeljump lip (yellow), surf (green), texturebug
// (cyan), then ours: jump surf (orange, a pin only a jump reaches). Dots of different types on one
// spot share ONE ring split into arcs.
constexpr int   kPfTypes     = 5;
constexpr float kPfDotSize   = 9.f;
constexpr float kPfHoverSize = 12.f;
constexpr float kPfFadeTime  = 0.5f;
constexpr float kPfTop       = -3.14159265f * 0.5f;
constexpr float kPfTurn      = 6.2831853f;
constexpr float kPfSameXY    = 2.f;
constexpr float kPfSameZ     = 0.5f;
constexpr int   kPfRgb[kPfTypes][3] = { { 255, 0, 0 }, { 255, 220, 0 }, { 0, 255, 0 }, { 0, 200, 255 }, { 255, 130, 0 } };

// ---- RenderPoints: saved targets (assist.cpp:2318-2395) --------------------------
constexpr float kPtNear       = 300.f;  // full size out to here
constexpr float kPtLerpSpan   = 300.f;  // then 11 -> 4 over this many units
constexpr float kPtRNear      = 11.0f;  // baseRadius
constexpr float kPtRFar       = 4.0f;
constexpr float kPtRMin       = 3.0f;
constexpr float kPtRingThick  = 0.5f;   // outlineThickness
constexpr float kPtScaleSpeed = 10.0f;  // LerpFloat(current, target, dt * scaleSpeed)
constexpr int   kSegments     = 32;     // both circles of both features
// cull distance, fade, size, fill, hover size, colours: movement > points (cfg/vars_points.h)

// botox AnimatedPoint. Render-thread only: the movement thread owns the point lists, this
// owns how they animate, so there is nothing to lock.
struct FinderAnim
{
    Vec3  pos;
    float prog      = 0.f;   // animation_progress
    bool  removing  = false;
    float size      = 0.f;   // current_size
};
std::vector<FinderAnim> s_fndAnim[kPfTypes];
std::vector<float>      s_ptScale;   // points_check_t::currentScale, one per saved point

inline bool SameSpot(const Vec3& a, const Vec3& b)   // vec3_t operator== on the reference
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

inline float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
inline float LerpF(float a, float b, float t) { return a + (b - a) * t; }

void PsOverlayResetAnim() { for (auto& l : s_fndAnim) l.clear(); s_ptScale.clear(); }

// botox pf_fade: live points fade in, dropped ones fade out over kPfFadeTime, then go
void PfFade(std::vector<FinderAnim>& list, const std::vector<Vec3>& points, float dt)
{
    for (auto& a : list) a.removing = true;
    for (const Vec3& p : points)
    {
        auto it = std::find_if(list.begin(), list.end(), [&p](const FinderAnim& a) { return SameSpot(a.pos, p); });
        if (it == list.end()) list.push_back({ p });
        else                  it->removing = false;
    }
    for (auto& a : list)
        a.prog = std::clamp(a.prog + (a.removing ? -dt : dt) / kPfFadeTime, 0.f, 1.f);
    list.erase(std::remove_if(list.begin(), list.end(), [](const FinderAnim& a) { return a.removing && a.prog <= 0.f; }),
               list.end());
}

} // namespace

// SEH-only leaf (no dtors -- C2712). The engine's CURRENT matrix: right on the thread that
// set up the view (main thread; mat_queue_mode 0 = the only thread).
static __declspec(noinline) const float(*PsEngineW2S())[4]
{
    typedef float Mat44[4][4];
    Mat44* pm = nullptr;
    __try {
        pm = VCall<Mat44*>(g_engine, 36);
    } __except (EXCEPTION_EXECUTE_HANDLER) { pm = nullptr; }
    return pm ? *pm : nullptr;
}

// MARKER JITTER. With multicore rendering the overlay is built on the render thread
// while the main thread is already a frame ahead and has rewritten the engine matrix for the
// NEXT view. Which one a build read depended on thread timing, so every world marker flipped
// between two frames' projections while the camera moved. The fix hands the render thread the
// matrix of the frame it is drawing (captured at that frame's vgui paint, OvQueue below);
// only set for the length of that build.
static float s_rtMatrix[4][4];
static ImFont* s_fontEsp = nullptr;          // BuildFontAtlas, esp replay
static std::vector<ImVec2> s_espPolyScratch;  // RenderEspQueue polyline points, render thread only
static bool  s_rtMatrixOk = false;
static uint32_t s_rtEspSeq = 0;   // esp_queue.h frame of the build in progress, 0 = newest (see EspQ::CopyFrame)

static const float(*PsW2SMatrix())[4]
{
    return s_rtMatrixOk ? s_rtMatrix : PsEngineW2S();
}

// Shared projector (render.h). Same matrix + same pixel-snap every W2S lambda in this file
// already uses -- one copy so killeffects.cpp stops re-deriving engine slot 36 math itself.
bool W2S_Project(const Vec3& v, ImVec2& out)
{
    const float(*m)[4] = PsW2SMatrix();
    if (!m) return false;
    const float w = m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3];
    if (w <= 0.001f) return false;
    const float inv = 1.f / w;
    const ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    out.x = W * 0.5f + (m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3]) * inv * W * 0.5f;
    out.y = H * 0.5f - (m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3]) * inv * H * 0.5f;
    out.x = floorf(out.x) + 0.5f;
    out.y = floorf(out.y) + 0.5f;
    return true;
}

static void RenderPsOverlay()
{
    s_psPointHover = -1; // never let a stale hover open the ENTER window from a dead frame

    // the finder draws its OWN results whenever the finder is on. "show points" is
    // the ASSIST's toggle and lives in the assist group, so a finder-only user pressed the
    // bind, the scan ran, and every marker was thrown away un-drawn -- "nothing happens"
    // and the other way round -- a feature that is OFF leaves nothing on screen.
    // "show points" is a switch inside the assist, not a switch that outlives it
    const bool bFinder = g_psFinder;
    const bool bAssist = g_psAssist && g_psAssistRender;
    // map-scan lines have their own switch and outlive the finder, so they keep the snapshot
    // (and the overlay) alive on their own -- the copy is free when the list is empty
    const bool bPoints = bFinder || bAssist || g_psMapLines;
    // each feature gates its own stamp (psassist.inl), so either switch keeps the stack alive
    const bool bBox = g_psAssistJumpBox || (g_pixelJump && g_pjReadout);
    if (!bPoints && !bBox) { PsOverlayResetAnim(); return; }

    // One snapshot per frame. Point rendering off (jump box on) copies no vectors at all.
    static PsDrawData d;
    if (bPoints) PS_GetDrawData(d);
    else         PS_GetBox(d);
    const unsigned long long now = GetTickCount64();

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;

    // botox movement_add_window (movement.cpp WindowDetect): stack at 0.8 H growing up, newest at
    // the bottom, 25 tall, 14 pad, 95 min wide, fade 0.25 / show 1.8 / fade 0.2, top accent line
    // shrinking over the life, verdana 11 with a 1px quarter-alpha shadow, max 5, same text
    // dropped inside 2 s
    if (bBox)
    {
        struct Win { std::string text; float t0; };
        static std::vector<Win> s_wins;
        static unsigned long long s_seenMs = 0;
        constexpr float kIn = 0.25f, kShow = 1.8f, kOut = 0.2f, kLife = kIn + kShow + kOut;
        const float tNow = now / 1000.f;
        if (d.boxMs && d.boxMs != s_seenMs && d.boxText[0])
        {
            s_seenMs = d.boxMs;
            bool dup = false;
            for (const Win& w : s_wins)
                dup |= w.text == d.boxText && tNow - w.t0 < 2.f;
            if (!dup)
                s_wins.push_back({ d.boxText, tNow });
        }
        s_wins.erase(std::remove_if(s_wins.begin(), s_wins.end(),
                     [&](const Win& w) { return tNow - w.t0 >= kLife; }), s_wins.end());
        while (s_wins.size() > 5)
            s_wins.erase(s_wins.begin());

        ImFont* font = g_menuReg ? g_menuReg : ImGui::GetFont();
        const float round = ImGui::GetStyle().WindowRounding;
        const int n = (int)s_wins.size();
        for (int i = 0; i < n; i++)
        {
            const Win& w = s_wins[i];
            const float age = tNow - w.t0;
            const float alpha = age < kIn ? age / kIn : age < kIn + kShow ? 1.f : std::max(0.f, 1.f - (age - kIn - kShow) / kOut);
            const int a = int(alpha * 255.f);
            const ImVec2 ts = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, w.text.c_str());
            const ImVec2 sz(std::max(ts.x + 28.f, 95.f), 25.f);
            const int stack = n - 1 - i;
            const float cy = H * 0.8f - stack * (sz.y + 4.f);
            const ImVec2 p(W * 0.5f - sz.x * 0.5f, cy - sz.y * 0.5f);
            dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(15, 15, 15, a), round);
            dl->AddRect(ImVec2(p.x + 1.f, p.y + 1.f), ImVec2(p.x + sz.x - 1.f, p.y + sz.y - 1.f),
                        IM_COL32(50, 50, 50, a), round);
            const float prog = std::min(1.f, (age + 0.2f) / (kLife + 0.4f));
            const float lw = (sz.x - 2.f) * (1.f - prog);
            if (lw > 0.f)
                dl->AddRectFilled(ImVec2(p.x + sz.x * 0.5f - lw * 0.5f, p.y), ImVec2(p.x + sz.x * 0.5f + lw * 0.5f, p.y + 1.f),
                                  ImGui::GetColorU32(ImVec4(g_accentColor[0], g_accentColor[1], g_accentColor[2], alpha)));
            const ImVec2 tp(p.x + (sz.x - ts.x) * 0.5f, p.y + (sz.y - ts.y) * 0.5f);
            dl->AddText(font, font->FontSize, ImVec2(tp.x + 1.f, tp.y + 1.f), IM_COL32(0, 0, 0, a / 4), w.text.c_str());
            dl->AddText(font, font->FontSize, tp, IM_COL32(240, 240, 240, a), w.text.c_str());
        }
    }

    if (!bPoints) return;
    if (!g_engine || !IsInGame()) { PsOverlayResetAnim(); return; } // map change = start clean
    if (now < d.aliveMs || now - d.aliveMs > 500) return;           // movement pipeline idle/dead

    const float(*m)[4] = PsW2SMatrix();
    if (!m) return;

    const float dt = io.DeltaTime;

    // DIH-1: project, then snap the stroke centre onto a pixel centre.
    auto W2S = [&](const Vec3& v, ImVec2& out) -> bool
    {
        const float w = m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3];
        if (w <= 0.001f) return false;
        const float inv = 1.f / w;
        out.x = W * 0.5f + (m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3]) * inv * W * 0.5f;
        out.y = H * 0.5f - (m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3]) * inv * H * 0.5f;
        out.x = floorf(out.x) + 0.5f;
        out.y = floorf(out.y) + 0.5f;
        return true;
    };

    // InCrosshair (assist.cpp:260): screen-centre distance, radius in pixels
    auto InCross = [&](const ImVec2& s, float r) -> bool
    {
        const float dx = W * 0.5f - s.x, dy = H * 0.5f - s.y;
        return dx * dx + dy * dy <= r * r;
    };

    // ---- finder dots: botox pf_render_dots (points_ui.cpp), 1:1 ---------------------------------
    // Per type list fades in/out over 0.5s. Dots within 2u xy / 0.5u z share one ring: one type =
    // full circle in its colour, several = the ring split into one arc per type, clockwise from the
    // top in list order. Hovered ring (nearest live one within 9px of the crosshair) grows 9 -> 12.
    // Map-scan seams ride the texturebug list: botox has no map scan, they are the same mark.
    {
        static std::vector<Vec3> hb, pj, tb, js;
        static const std::vector<Vec3> none;
        hb.clear(); pj.clear(); tb.clear(); js.clear();
        if (bFinder) PF_GetExtraPts(js, hb, pj);
        if (!g_psFinderHeadbounce) hb.clear();
        if (!g_psFinderPixeljump)  pj.clear();
        if (!g_psFinderJumpSurf)   js.clear();
        if (bFinder && g_psFinderTb) tb = d.tbPts;
        if (g_psMapLines) tb.insert(tb.end(), d.mapTbPts.begin(), d.mapTbPts.end());

        const std::vector<Vec3>* live[kPfTypes] = { &hb, &pj, bFinder ? &d.finderPts : &none, &tb, &js };
        for (int t = 0; t < kPfTypes; t++) PfFade(s_fndAnim[t], *live[t], dt);

        struct Group
        {
            Vec3 pos;
            FinderAnim* dot[kPfTypes] = {};
            int   slot[kPfTypes] = {};
            int   types = 0;
            float progress = 0.f, size = 0.f;
            bool  live = false, drawn = false;
            ImVec2 screen;
        };
        struct Member { FinderAnim* dot; int group; };
        static std::vector<Group> groups;
        static std::vector<Member> members;
        groups.clear(); members.clear();
        for (int t = 0; t < kPfTypes; t++)
            for (auto& a : s_fndAnim[t])
            {
                auto g = std::find_if(groups.begin(), groups.end(), [&](const Group& q) {
                    const float dx = q.pos.x - a.pos.x, dy = q.pos.y - a.pos.y;
                    return std::fabs(q.pos.z - a.pos.z) < kPfSameZ && dx * dx + dy * dy < kPfSameXY * kPfSameXY;
                });
                if (g == groups.end()) { g = groups.emplace(groups.end()); g->pos = a.pos; }
                if (!g->dot[t]) { g->dot[t] = &a; g->slot[t] = g->types++; }
                g->progress = std::max(g->progress, a.prog);
                g->live |= !a.removing;
                members.push_back({ &a, int(g - groups.begin()) });
            }

        int hovered = -1;
        float hoveredSq = FLT_MAX;
        for (int i = 0; i < (int)groups.size(); i++)
        {
            Group& g = groups[i];
            g.drawn = W2S(g.pos, g.screen);
            if (!g.drawn || !g.live || !InCross(g.screen, kPfDotSize)) continue;
            const float dx = W * 0.5f - g.screen.x, dy = H * 0.5f - g.screen.y;
            if (dx * dx + dy * dy < hoveredSq) { hoveredSq = dx * dx + dy * dy; hovered = i; }
        }

        // botox: dot size * point size, fill = background opacity * opacity, ring = finder style
        // colour * opacity, outline thickness. Jump surf (ours) keeps its fixed orange
        const float ease = 1.f - expf(-5.f * dt);
        for (auto& m : members)
        {
            const float target = (m.group == hovered ? kPfHoverSize : kPfDotSize) * m.dot->prog * PointsSize();
            m.dot->size += (target - m.dot->size) * ease;
            groups[m.group].size = std::max(groups[m.group].size, m.dot->size);
        }

        constexpr int kPfStyle[kPfTypes] = { PS_STYLE_PF_HB, PS_STYLE_PF_PJ, PS_STYLE_PF_PS, PS_STYLE_PF_TB, -1 };
        const float op = std::clamp(g_pointsOpacity, 0.f, 1.f);
        const float pfThick = PointsThickness(1.f);
        for (const Group& g : groups)   // render finder points off = draw only, fades keep running
        {
            if (!g_pointsRenderFinder || !g.drawn || g.size <= 0.f) continue;
            dl->AddCircleFilled(g.screen, g.size,
                                IM_COL32(0, 0, 0, int(g.progress * 255.f * std::clamp(g_pointsFill, 0.f, 1.f) * op)), kSegments);
            float a0[kPfTypes] = {}, a1[kPfTypes] = {};
            ImU32 col[kPfTypes] = {};
            for (int t = 0; t < kPfTypes; t++)
            {
                if (!g.dot[t]) continue;
                const int s = g.slot[t];
                a0[s] = kPfTop + s * kPfTurn / g.types;
                a1[s] = a0[s] + kPfTurn / g.types;
                col[s] = kPfStyle[t] >= 0 ? PointStyleU32(kPfStyle[t], g.dot[t]->prog)
                                          : IM_COL32(kPfRgb[t][0], kPfRgb[t][1], kPfRgb[t][2], int(g.dot[t]->prog * 255.f * op));
            }
            if (g.types == 1) { dl->AddCircle(g.screen, g.size, col[0], kSegments, pfThick); continue; }
            for (int i = 0; i < g.types; i++)
            {
                dl->PathArcTo(g.screen, g.size, a0[i], a1[i], kSegments / g.types);
                dl->PathStroke(col[i], 0, pfThick);
            }
        }
    }

    // ---- whole-map scan: one line along each confirmed surf (psmap.inl) ------------------
    // Own switch (g_psMapLines), not the finder's: a scan is minutes of work and its lines are
    // worth keeping on the world after the aim scan is switched back off.
    // Drawn in pieces, not end to end: a seam is often hundreds of units long, and a single
    // AddLine dies the moment ONE end goes behind the camera -- which is exactly when you are
    // standing on the surf. Pieces whose own ends both project keep the rest of the line alive.
    // The pieces carry the END FADE too: alpha per piece over the last kFadeU of each end, so a
    // line tapers out instead of stopping dead on a hard pixel. Only the fade zones are cut fine
    // (kFine); the middle stays on the old 64u grid, so a long seam costs ~len/64 + 8 lines.
    // the finder's surf lengths (g_psFinderLength) are the same kind of mark, drawn the same way
    // in the finder's surf colour
    auto DrawSegs = [&](const std::vector<Vec3>& segs, int r, int g, int b8)
    {
        constexpr float kPiece = 64.f;   // middle of the line
        constexpr float kFine  = 6.f;    // inside a fade zone: one alpha step per piece
        constexpr float kFadeU = 48.f;   // world units of taper at each end
        constexpr float kLineA = 220.f;
        ImVec2 p0, p1;
        for (size_t i = 0; i + 1 < segs.size(); i += 2)
        {
            const Vec3& a = segs[i];
            const Vec3& b = segs[i + 1];
            const Vec3 dir = b - a;
            const float len = dir.Length();
            if (len < 0.01f) continue;
            // a short run cannot spend 48u per side -- it gets a third of itself instead, so a
            // 32u stub still reads as a fading mark and not as a solid dash
            const float fade = std::min(kFadeU, len * 0.34f);
            for (float dA = 0.f; dA < len - 0.001f; )
            {
                // step from where this piece STARTS, plus one fine step of slack, or a coarse
                // piece entering the far zone would swallow the whole taper
                const bool fine = std::min(dA, len - dA) < fade + kFine;
                const float dB = std::min(dA + (fine ? kFine : kPiece), len);
                const float mid = (dA + dB) * 0.5f;
                const float fa = Clamp01(std::min(mid, len - mid) / fade);
                const float sm = fa * fa * (3.f - 2.f * fa);   // smoothstep: no banding at the tip
                if (W2S(a + dir * (dA / len), p0) && W2S(a + dir * (dB / len), p1))
                    dl->AddLine(p0, p1, IM_COL32(r, g, b8, int(kLineA * sm)), 1.f);
                dA = dB;
            }
        }
    };
    if (g_psMapLines && d.mapLines.size() >= 2)
        DrawSegs(d.mapLines, 255, 255, 255);
    if (bFinder && g_psFinderLength && d.finderLines.size() >= 2)
        DrawSegs(d.finderLines, kPfRgb[2][0], kPfRgb[2][1], kPfRgb[2][2]);

    // finder scan column while stretching -- dih's own, the reference has no equivalent.
    // 1px on the pixel grid like everything else
    if (bFinder && d.lineActive)
    {
        ImVec2 a, b;
        if (W2S(d.lineA, a) && W2S(d.lineB, b))
            dl->AddLine(a, b, IM_COL32(133, 133, 133, 255), 1.f);
    }

    // ---- saved points: botox points_ui.cpp RenderPoints 1:1, look from movement > points ----
    // (vars cfg/vars_points.h, styles movement/points_style.h). Not ported: botox's per-point
    // "3d radius ring" + free points (TF2 points have neither field), assist render height.
    const float maxDrawDist = std::max(g_pointsMaxDist, 1.f);
    const float fadeStart   = maxDrawDist * 0.6f;
    const float opacity     = std::clamp(g_pointsOpacity, 0.f, 1.f);
    const float size        = PointsSize();
    const float fill        = std::clamp(g_pointsFill, 0.f, 1.f);
    const float hoverScale  = std::clamp(g_pointsHoverScale, 1.f, 3.f);
    ImFont* labelFont = g_menuReg ? g_menuReg : ImGui::GetFont();   // botox verdana 11
    // alpha passed in already holds opacity. radius color = active point while you stand outside its radius
    auto StyleColor = [&](int style, bool inRadius, float alpha) -> ImU32
    {
        if (g_pointsRadiusColor && !inRadius) return PointColU32(g_pointsRadiusClr, alpha);
        return PointStyleRaw(style, alpha);
    };
    // botox project_world_axis: screen direction of a world axis through pos + how much of it faces
    // the camera (0 = straight at you, 1 = flat on screen)
    auto ProjectAxis = [&](const Vec3& pos, const Vec3& dir, float probe, float& ox, float& oy, float& ratio) -> bool
    {
        auto span = [&](const Vec3& axis, float& sx, float& sy) -> bool
        {
            ImVec2 a, b;
            if (!W2S(pos + axis * probe, a) || !W2S(pos - axis * probe, b)) return false;
            sx = a.x - b.x; sy = a.y - b.y;
            return true;
        };
        float dx, dy, xx, xy, yx, yy, zx, zy;
        if (!span(dir, dx, dy)) return false;
        if (!span(Vec3(1.f, 0.f, 0.f), xx, xy) || !span(Vec3(0.f, 1.f, 0.f), yx, yy) || !span(Vec3(0.f, 0.f, 1.f), zx, zy))
            return false;
        const float full = sqrtf((xx * xx + xy * xy + yx * yx + yy * yy + zx * zx + zy * zy) * 0.5f);
        if (full < 0.0001f) return false;
        const float len = sqrtf(dx * dx + dy * dy);
        ratio = std::clamp(len / full, 0.f, 1.f);
        if (len < 0.0001f) { ox = 1.f; oy = 0.f; return true; }
        ox = dx / len; oy = dy / len;
        return true;
    };
    auto Design = [&](int style, const ImVec2& s, float effR, float dirX, float dirY, float ratio, ImU32 color)
    {
        const float half = PointDesignHalf(effR) * PointArrowLengthScale(ratio);
        const int a = int(((color >> IM_COL32_A_SHIFT) & 0xFFu) * PointArrowFade(ratio));
        if (a > 0) PointDesignEmit(dl, style, s, half, dirX, dirY, PointWithAlpha(color, a));
    };

    if (!bAssist) return;   // saved points + the ENTER window are the assist's

    // ---- saved assist targets: botox RenderPoints pixelsurf loop as style pixelsurf ------
    // every saved point is botox "active" (TF2 points have no enable flag): style colour, or the
    // radius colour while you stand outside its engage radius. Arrow = along the wall, from the
    // normal the assist caches on the point; not resolved yet = botox's no-probe fallback (1,0)
    if (s_ptScale.size() != d.assistPts.size())
        s_ptScale.assign(d.assistPts.size(), 1.f);

    for (int i = 0; g_pointsRenderPs && i < (int)d.assistPts.size(); i++)
    {
        const Vec3& p = d.assistPts[i];

        ImVec2 s;
        if (!W2S(p, s)) continue;

        const float dist = d.localOrigin.DistTo(p);
        if (dist > maxDrawDist) continue;

        float baseR = kPtRNear;
        if (g_pointsDistanceShrink && dist > kPtNear)
            baseR = LerpF(kPtRNear, kPtRFar, Clamp01((dist - kPtNear) / kPtLerpSpan));
        baseR = std::max(baseR, kPtRMin) * size;

        float aMul = 1.f;
        if (g_pointsDistanceFade && dist > fadeStart) aMul = Clamp01((maxDrawDist - dist) / (maxDrawDist - fadeStart));
        aMul *= opacity;

        const bool inRadius = i < (int)d.assistRadius.size() && dist <= d.assistRadius[i];
        const ImU32 ring = StyleColor(PS_STYLE_PS, inRadius, aMul);
        const ImU32 fillC = IM_COL32(0, 0, 0, int(255.f * fill * aMul));

        // hover is measured on the UNSCALED radius, so a point cannot grow itself into the
        // crosshair and latch there. First one hovered owns ENTER/BACKSPACE.
        const bool hov = InCross(s, baseR);
        if (hov && s_psPointHover < 0) s_psPointHover = i;

        s_ptScale[i] = LerpF(s_ptScale[i], hov ? hoverScale : 1.f, Clamp01(dt * kPtScaleSpeed));
        const float r = baseR * s_ptScale[i] * PointsPulse(inRadius);

        dl->AddCircleFilled(s, r, fillC, kSegments);
        PointRingEmit(dl, PS_STYLE_PS, s, r, ring, kPtRingThick);

        float dx = 1.f, dy = 0.f, ratio = 1.f;
        if (PointStyleDesign(PS_STYLE_PS) >= 0 && i < (int)d.assistNormal.size())
        {
            const Vec3& n = d.assistNormal[i];
            const float len = sqrtf(n.x * n.x + n.y * n.y);
            float x, y, q;
            if (len > 0.0001f && ProjectAxis(p, Vec3(-n.y / len, n.x / len, 0.f), 16.f, x, y, q)) { dx = x; dy = y; ratio = q; }
        }
        Design(PS_STYLE_PS, s, r, dx, dy, ratio, StyleColor(PS_STYLE_PS, inRadius, aMul * 200.f / 255.f));
        PointLabelEmit(dl, labelFont, PS_STYLE_PS, s, r, aMul);
    }

    // ---- pixelsurf lines (movement extras): connect the saved targets in index order, a route line
    // distinct from the points above. "full trajectory" also connects the finder's live hits
    // in scan order -- reuses this function's own W2S/dl, no second projection copy.
    if (g_psLinesEnable)
    {
        const auto connect = [&](const std::vector<Vec3>& pts, const float* color)
        {
            const ImU32 col = IM_COL32((int)(color[0]*255),(int)(color[1]*255),(int)(color[2]*255),(int)(color[3]*255));
            ImVec2 prev; bool havePrev = false;
            for (const Vec3& p : pts)
            {
                ImVec2 s;
                if (!W2S(p, s)) { havePrev = false; continue; }
                if (havePrev) dl->AddLine(prev, s, col, 1.5f);
                prev = s; havePrev = true;
            }
        };
        connect(d.assistPts, g_psLinesColor);
        if (g_psLinesFull) connect(d.finderPts, g_psLinesFullColor);
    }
}

// ==== edge bug plan overlay ======================================================================
// Two shapes and no knobs beyond size: the flight path of the latched plan, and a flat square on
// the tick the bug lands. The line is drawn from where the replay has got to, so it is eaten tick
// by tick and what is left IS the countdown. No sims and no traces -- it only draws the path the
// search already paid for (movement.cpp EbVizLatch).
static void RenderEdgeBugOverlay()
{
    if (!g_edgeBug || !g_edgeBugViz) return;
    if (!g_engine || !IsInGame()) return;

    static EbDrawData d;
    EB_GetDrawData(d);
    if (!d.active || d.path.size() < 2) return;

    const float(*m)[4] = PsW2SMatrix();
    if (!m) return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;

    auto W2S = [&](const Vec3& v, ImVec2& out) -> bool
    {
        const float w = m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3];
        if (w <= 0.001f) return false;
        const float inv = 1.f / w;
        out.x = W * 0.5f + (m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3]) * inv * W * 0.5f;
        out.y = H * 0.5f - (m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3]) * inv * H * 0.5f;
        out.x = floorf(out.x) + 0.5f;   // pixel grid, same as every other world marker here
        out.y = floorf(out.y) + 0.5f;
        return true;
    };

    ImVec2 prev; bool bHavePrev = false;
    for (const Vec3& p : d.path)
    {
        ImVec2 s;
        if (!W2S(p, s)) { bHavePrev = false; continue; }
        if (bHavePrev) dl->AddLine(prev, s, IM_COL32(235, 235, 235, 200), 1.f);
        prev = s; bHavePrev = true;
    }

    // the pad: the bug tick's own footprint, built in WORLD space so it keeps its perspective
    // and reads as a mark lying on the map instead of a sticker floating over it
    const float r = g_edgeBugVizSize;
    const Vec3& p = d.at;
    const Vec3 corner[4] = { { p.x - r, p.y - r, p.z }, { p.x - r, p.y + r, p.z },
                             { p.x + r, p.y + r, p.z }, { p.x + r, p.y - r, p.z } };
    ImVec2 s[4];
    bool bAll = true;
    for (int i = 0; i < 4; i++) bAll = W2S(corner[i], s[i]) && bAll;
    if (bAll)
    {
        dl->AddQuadFilled(s[0], s[1], s[2], s[3], IM_COL32(0, 0, 0, 90));
        dl->AddQuad(s[0], s[1], s[2], s[3], IM_COL32(235, 235, 235, 255), 1.f);
    }
}

// ==== auto bounce overlay (botox auto_bounce_render) =============================================
// The armed plan's verified flight from your body to the contact, a ring lying on the face it drops
// onto, and an arrow = last flight segment clipped onto the face (ClipVelocity). Draws the movement
// thread's copy only -- botox's paint-thread snap trace is skipped, land is already the hull's feet
// on the face (ponytail: no render-thread trace; add one if the ring floats on bevels).
static void RenderAutoBounceOverlay()
{
    if (!g_autoBounce || !g_abViz) return;
    if (!g_engine || !IsInGame()) return;

    static AbDrawData d;
    AB_GetDrawData(d);
    if (!d.active || d.path.size() < 2) return;

    const float(*m)[4] = PsW2SMatrix();
    if (!m) return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    auto W2S = [&](const Vec3& v, ImVec2& out) -> bool
    {
        const float w = m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3];
        if (w <= 0.001f) return false;
        const float inv = 1.f / w;
        out.x = W * 0.5f + (m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3]) * inv * W * 0.5f;
        out.y = H * 0.5f - (m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3]) * inv * H * 0.5f;
        return true;
    };
    const ImU32 col  = ImGui::ColorConvertFloat4ToU32(ImVec4(g_abVizColor[0], g_abVizColor[1], g_abVizColor[2], g_abVizColor[3]));
    const ImU32 fill = ImGui::ColorConvertFloat4ToU32(ImVec4(g_abVizColor[0], g_abVizColor[1], g_abVizColor[2], g_abVizColor[3] * 0.25f));
    auto line = [&](const Vec3& a, const Vec3& b) {
        ImVec2 sa, sb;
        if (W2S(a, sa) && W2S(b, sb)) dl->AddLine(sa, sb, col, 1.5f);
    };

    for (size_t i = 0; i + 1 < d.path.size(); ++i) line(d.path[i], d.path[i + 1]);

    Vec3 n = d.normal;
    const float nl = n.Length();
    n = nl > 0.5f ? n * (1.f / nl) : Vec3(0.f, 0.f, 1.f);
    const Vec3 centre = d.land + n * 0.5f;
    const auto cross = [](const Vec3& a, const Vec3& b) { return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); };
    const Vec3 upRef = fabsf(n.z) < 0.99f ? Vec3(0.f, 0.f, 1.f) : Vec3(1.f, 0.f, 0.f);
    Vec3 u = cross(n, upRef); u.Normalize();
    const Vec3 v = cross(n, u);

    constexpr int kPts = 24;
    constexpr float kRad = 16.f;
    Vec3 ring[kPts];
    ImVec2 sp[kPts];
    bool onScreen = true;
    for (int i = 0; i < kPts; ++i) {
        const float a = 6.28318530718f * (float)i / (float)kPts;
        ring[i] = centre + u * (cosf(a) * kRad) + v * (sinf(a) * kRad);
        onScreen = W2S(ring[i], sp[i]) && onScreen;
    }
    // one convex poly, never a centre fan (imgui AA spokes). skipped when a vertex is off screen
    if (onScreen) dl->AddConvexPolyFilled(sp, kPts, fill);
    for (int i = 0; i < kPts; ++i) line(ring[i], ring[(i + 1) % kPts]);

    const Vec3 in = d.path[d.path.size() - 1] - d.path[d.path.size() - 2];
    Vec3 dir = in - n * in.Dot(n);
    if (dir.Length() > 0.01f) {
        dir.Normalize();
        const Vec3 side = cross(n, dir);
        const Vec3 tip  = centre + dir * (kRad + 12.f);
        line(centre + dir * kRad, tip);
        line(tip, tip - dir * 6.f + side * 4.f);
        line(tip, tip - dir * 6.f - side * 4.f);
    }
}

// ==== wall climb overlay (botox wall_climb render) ===============================================
// face wc reads, where the sent cmd takes you ( green end = a land the sweep won )
static void RenderWallClimbOverlay()
{
    if (!g_wallClimb || !g_wcViz) return;
    if (!g_engine || !IsInGame()) return;

    static WcDrawData d;
    WC_GetDrawData(d);
    if (!d.valid || GetTickCount64() - d.ms > 250ull) return;   // wc stopped running (key up / no wall / dead)

    const float(*m)[4] = PsW2SMatrix();
    if (!m) return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    auto W2S = [&](const Vec3& v, ImVec2& out) -> bool
    {
        const float w = m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3];
        if (w <= 0.001f) return false;
        const float inv = 1.f / w;
        out.x = W * 0.5f + (m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3]) * inv * W * 0.5f;
        out.y = H * 0.5f - (m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3]) * inv * H * 0.5f;
        return true;
    };
    const float* c = g_wcVizColor;
    const ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3]));
    const ImU32 fill  = ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3] * 0.18f));
    const ImU32 green = IM_COL32(90, 255, 120, 255);
    const ImU32 red   = IM_COL32(255, 80, 80, 255);
    const ImU32 grey  = IM_COL32(160, 160, 160, 170);
    ImFont* font = g_menuReg ? g_menuReg : ImGui::GetFont();   // botox verdana 11

    auto line = [&](const Vec3& a, const Vec3& b, ImU32 col, float w) {
        ImVec2 sa, sb;
        if (W2S(a, sa) && W2S(b, sb)) dl->AddLine(sa, sb, col, w);
    };
    auto dot = [&](const Vec3& p, float r, ImU32 col) {
        ImVec2 s;
        if (W2S(p, s)) dl->AddCircleFilled(s, r, col, 12);
    };
    auto label = [&](const Vec3& p, const char* str, ImU32 col) {
        ImVec2 s;
        if (!W2S(p, s)) return;
        const float fs = font->FontSize;
        const ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, str);
        const ImVec2 at(floorf(s.x - sz.x * 0.5f), floorf(s.y - fs));
        dl->AddText(font, fs, ImVec2(at.x + 1.f, at.y + 1.f), IM_COL32(0, 0, 0, 200), str);   // botox text_flag_dropshadow
        dl->AddText(font, fs, at, col, str);
    };

    const Vec3& n = d.n;
    const float nl = sqrtf(n.x * n.x + n.y * n.y);
    const Vec3 t   = nl > 1e-3f ? Vec3(n.y / nl, -n.x / nl, 0.f) : Vec3(1.f, 0.f, 0.f);
    const Vec3 up(0.f, 0.f, d.height * 0.5f);
    const Vec3 side = t * d.half;
    const Vec3 face[4] = { d.wall - side - up, d.wall + side - up, d.wall + side + up, d.wall - side + up };
    ImVec2 sp[4];
    bool onScreen = true;
    for (int i = 0; i < 4 && onScreen; i++) onScreen = W2S(face[i], sp[i]);
    if (onScreen) dl->AddConvexPolyFilled(sp, 4, fill);
    for (int i = 0; i < 4; i++) line(face[i], face[(i + 1) % 4], color, 1.f);
    line(d.wall, d.wall + n * 8.f, color, 1.f);
    char buf[64];
    snprintf(buf, sizeof(buf), "wc %s %d", d.branch ? d.branch : "-", d.sims);
    label(d.wall + up + Vec3(0.f, 0.f, 4.f), buf, color);

    if (d.ground) return;
    for (int i = 0; i + 1 < d.pathN; i++) line(d.path[i], d.path[i + 1], color, 1.5f);
    if (d.pathN > 1) dot(d.path[d.pathN - 1], d.lands ? 5.f : 3.f, d.lands ? green : color);
    if (d.lands) {
        snprintf(buf, sizeof(buf), "+%.0f", d.land.z - d.groundZ);
        label(d.land + Vec3(0.f, 0.f, 8.f), buf, green);
    }
}

// ==== route + pixel calculator overlay ===========================================================
// Same contract as the ps overlay above: the movement thread owns the markers and the solve, this
// draws a per-frame copy. Monochrome like everything else -- a marker is a ring with its number
// in it, the path between them is a 1px line, and the answer is botox's timed popup (draw_popup).
// botox route_calc.cpp key_label: the short name the key list prints, "none" when unbound
static std::string RcKeyLabel(int k)
{
    if (k <= 0) return "none";
    switch (k)
    {
    case VK_LBUTTON: return "m1";   case VK_RBUTTON: return "m2";   case VK_MBUTTON: return "m3";
    case VK_XBUTTON1: return "m4";  case VK_XBUTTON2: return "m5";  case VK_SPACE: return "spa";
    case VK_SHIFT: return "shi";    case VK_CONTROL: return "ctl";  case VK_MENU: return "alt";
    case VK_TAB: return "tab";      case VK_RETURN: return "ret";   case VK_BACK: return "bac";
    case VK_INSERT: return "ins";   case VK_DELETE: return "del";   case VK_END: return "end";
    case VK_HOME: return "hom";     case VK_PRIOR: return "pgu";    case VK_NEXT: return "pgd";
    case VK_CAPITAL: return "cap";  case VK_OEM_4: return "[";      case VK_OEM_6: return "]";
    case VK_OEM_1: return ";";      case VK_OEM_7: return "'";      case VK_OEM_COMMA: return ",";
    case VK_OEM_PERIOD: return ".";
    default: break;
    }
    if (k >= '0' && k <= '9') return std::string(1, (char)k);
    if (k >= 'A' && k <= 'Z') return std::string(1, (char)(k + 32));
    if (k >= VK_NUMPAD0 && k <= VK_NUMPAD9) return "num" + std::to_string(k - VK_NUMPAD0);
    if (k >= VK_F1 && k <= VK_F24) return "f" + std::to_string(k - VK_F1 + 1);
    return "key";
}

// botox route_calc_ui "show key list": an ImGui window (draggable while the menu is open) in the
// route calculator box chrome -- 25px band, centred bold title, faded accent line, 1px inner outline
static void RenderRouteKeys()
{
    ImFont* font = g_menuReg ? g_menuReg : ImGui::GetFont();   // botox verdana 11
    ImFont* bold = g_menuBold ? g_menuBold : font;              // botox verdana bold 11
    constexpr float k_band = 25.f, k_pad = 8.f, k_gap = 22.f;
    struct Hint { const char* label; std::string key; };
    const Hint hints[] = {
        { "add point",        RcKeyLabel(g_rcAddKey) },
        { "calculate combos", RcKeyLabel(g_rcSolveKey) },
        { "delete point",     RcKeyLabel(g_rcDeleteKey) },
        { "clear all points", RcKeyLabel(g_rcClearKey) },
    };
    auto TextW = [](ImFont* f, const char* s) { return f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.f, s).x; };
    const float row_h = font->FontSize + 2.f;
    float body_w = 0.f;
    for (const Hint& h : hints) body_w = std::max(body_w, TextW(font, h.label) + k_gap + TextW(font, h.key.c_str()));

    int flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse;
    if (!g_showMenu) flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
    ImGui::SetNextWindowSize(ImVec2(body_w + k_pad * 2.f, k_band + k_pad + row_h * (float)IM_ARRAYSIZE(hints)), ImGuiCond_Always);
    ImGui::Begin("botox-route-calc-keys", nullptr, flags);
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        ImDrawList* wdl = w->DrawList;
        const ImVec2 pos = w->Pos, size = w->Size;
        const float rounding = ImGui::GetStyle().WindowRounding;
        const char* title = "route calculator";

        ImGui::PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + k_band), false);
        wdl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + k_band), IM_COL32(25, 25, 25, 255), rounding, ImDrawFlags_RoundCornersTop);
        ImGui::PopClipRect();
        RenderFadedGradientLine(wdl, ImVec2(pos.x, pos.y + k_band - 1.f), ImVec2(size.x, 1.f),
                                ImColor(ImGui::GetColorU32(ImGuiCol_Accent)));
        const ImVec2 ts = bold->CalcTextSizeA(bold->FontSize, FLT_MAX, 0.f, title);
        wdl->AddText(bold, bold->FontSize, ImVec2(pos.x + (size.x - ts.x) * 0.5f, pos.y + (k_band - ts.y) * 0.5f),
                     IM_COL32(255, 255, 255, 255), title);
        ImGui::PushClipRect(ImVec2(pos.x + 1.f, pos.y + 1.f), ImVec2(pos.x + size.x - 1.f, pos.y + size.y - 1.f), false);
        wdl->AddRect(ImVec2(pos.x + 1.f, pos.y + 1.f), ImVec2(pos.x + size.x - 1.f, pos.y + size.y - 1.f),
                     IM_COL32(50, 50, 50, 255), rounding);
        ImGui::PopClipRect();

        float y = pos.y + k_band + 2.f;
        for (const Hint& h : hints)
        {
            wdl->AddText(font, font->FontSize, ImVec2(pos.x + k_pad, y), ImColor(0.63f, 0.63f, 0.63f, 1.f), h.label);
            wdl->AddText(font, font->FontSize, ImVec2(pos.x + size.x - k_pad - TextW(font, h.key.c_str()), y),
                         IM_COL32(255, 255, 255, 255), h.key.c_str());
            y += row_h;
        }
    }
    ImGui::End();
}

static void RenderRouteOverlay()
{
    if (!g_routeCalc && !g_pixelCalc) return;
    if (!g_engine || !IsInGame()) return;

    static RcDrawData d;
    RC_GetDrawData(d);

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;

    // two features, two sets of points. Neither one draws while its own feature is off
    const bool bRoutePts = g_routeCalc && !d.pts.empty();   // botox: points always drawn
    const bool bPixelPt  = g_pixelCalc && g_pcShowMarker && d.hasPixel;

    // ---- world markers + the path ---------------------------------------------------------
    if (bRoutePts || bPixelPt)
    {
        const float(*m)[4] = PsW2SMatrix();
        if (m)
        {
            auto W2S = [&](const Vec3& v, ImVec2& out) -> bool
            {
                const float w = m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3];
                if (w <= 0.001f) return false;
                const float inv = 1.f / w;
                out.x = W * 0.5f + (m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3]) * inv * W * 0.5f;
                out.y = H * 0.5f - (m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3]) * inv * H * 0.5f;
                out.x = floorf(out.x) + 0.5f;
                out.y = floorf(out.y) + 0.5f;
                return true;
            };

            // the route reads in ORDER, so the line between two markers is half the drawing:
            // it is the only thing on screen that says which way round they are.
            // once there is an answer the line IS the answer: RcSolveFinish flies the
            // first printed combination again and hands over the port's feet height after every
            // cmd, so the path drawn here is the real arc (launch, duck, ride, drop) instead of a
            // straight marker-to-marker line that no jump has ever had. Brighter than the plain
            // link, because this one is a solved route and not just an ordering.
            // No answer yet (markers just placed, or nothing completes them) -> the plain line.
            // the arc belongs to the feature that solved it, and follows that feature's own switch
            // botox route_calc_render 1:1: accent path (path alpha / thickness), grey point-order
            // line before a solve, markers = black disc + accent ring + accent number, white label
            const ImU32 accent = ImGui::ColorConvertFloat4ToU32(ImVec4(g_accentColor[0], g_accentColor[1], g_accentColor[2], 1.f));
            ImFont* mf = g_menuReg ? g_menuReg : ImGui::GetFont();   // botox verdana 11
            const int   pathA = std::clamp(g_rcPathAlpha, 0, 255);
            const float thick = std::clamp(g_rcPathThick, 1.f, 8.f);
            const bool bPath = g_rcShowPath;
            const bool bArc  = bPath && !d.path.empty() && (d.pixelSolve ? bPixelPt : bRoutePts);
            ImVec2 prev; bool bHavePrev = false;
            if (bArc)
            {
                const ImU32 col = (accent & ~IM_COL32_A_MASK) | ((ImU32)pathA << IM_COL32_A_SHIFT);
                for (size_t i = 0; i < d.path.size(); i++)
                {
                    ImVec2 s;
                    if (!W2S(d.path[i], s)) { bHavePrev = false; continue; }
                    if (bHavePrev)
                        dl->AddLine(prev, s, col, thick);
                    prev = s; bHavePrev = true;
                }
            }
            for (size_t i = 0; bPath && bRoutePts && !bArc && i < d.pts.size(); i++)
            {
                ImVec2 s;
                if (!W2S(d.pts[i].pos, s)) { bHavePrev = false; continue; }
                if (bHavePrev)
                    dl->AddLine(prev, s, IM_COL32(110, 110, 110, pathA * 160 / 255), thick);
                prev = s; bHavePrev = true;
            }

            // markers follow movement > points: "render calculator points", calculator style (shape, gaps,
            // accent * opacity), point size, outline thickness; black disc at opacity
            auto TextW = [&](const char* t) { return mf->CalcTextSizeA(mf->FontSize, FLT_MAX, 0.f, t).x; };
            const float op = std::clamp(g_pointsOpacity, 0.f, 1.f);
            const ImU32 markCol = PointStyleU32(PS_STYLE_CALC, 1.f);
            auto Ring = [&](const ImVec2& s, float base, const char* inner, const char* label)
            {
                const float r = base * PointsSize();
                dl->AddCircleFilled(s, r, IM_COL32(0, 0, 0, int(255.f * op)), kSegments);
                PointRingEmit(dl, PS_STYLE_CALC, s, r, markCol, 1.f);
                dl->AddText(mf, mf->FontSize, ImVec2(s.x - TextW(inner) * 0.5f, s.y - mf->FontSize * 0.5f - 1.f), markCol, inner);
                if (label)
                    dl->AddText(mf, mf->FontSize, ImVec2(s.x - TextW(label) * 0.5f, s.y - r - mf->FontSize - 3.f),
                                IM_COL32(255, 255, 255, int(255.f * op)), label);
            };

            for (size_t i = 0; bRoutePts && g_pointsRenderCalc && i < d.pts.size(); i++)
            {
                ImVec2 s;
                if (!W2S(d.pts[i].pos, s)) continue;
                const RcDrawPt& p = d.pts[i];
                // the label is what the marker IS (what an arc has to ARRIVE at it as); a *
                // means its own departure list is filtered (botox: styles narrowed)
                char num[8], lbl[64];
                snprintf(num, sizeof(num), "%d", (int)i + 1);
                snprintf(lbl, sizeof(lbl), "%s%s%s%s%s", RC_TypeName(p.type), p.filtered ? " *" : "",
                         p.enabled ? "" : " (off)", p.player ? " (player)" : "",
                         !p.stand ? " (ducked)" : !p.duck ? " (stand)" : "");
                Ring(s, 11.f, num, lbl);
            }

            // pixel calculator marker: botox pixel_calc 12px ring with "px" in it (not part of the
            // numbered route). The pixeljump label is ours: TF2 pixel calc also answers lips
            ImVec2 s;
            if (bPixelPt && g_pointsRenderCalc && W2S(d.pixel, s)) Ring(s, 12.f, "px", d.pixelType == 5 ? "pixeljump" : nullptr);
        }
    }

    if (g_routeCalc && g_rcShowKeys) RenderRouteKeys();

    // ---- the answer popup: botox route_calc.cpp n_route::draw_popup 1:1 ----------------------
    // the answer belongs to whichever of the two features produced it, and so does its toggle
    if (!(d.pixelSolve ? (g_pixelCalc && g_pcShowPanel) : (g_routeCalc && g_rcShowPanel))) return;

    // botox popup_row_kind_t. Kind rides as the row's first byte (routecalc.inl RC_HEAD/NOTE/ERR)
    enum { kCombo = 0, kHead = 1, kNote = 2, kErr = 3 };
    struct Row { std::string text; int kind; };
    auto MakeRow = [](const std::string& s, int untagged) -> Row
    {
        const unsigned char k = s.empty() ? 0 : (unsigned char)s[0];
        if (k >= 1 && k <= 3) return { s.substr(1), (int)k };
        return { s, untagged };
    };

    std::vector<Row> rows;
    if (d.solving)
        rows.push_back({ "calculating (" + std::to_string((int)(d.progress * 100.f)) + "%)", kCombo });
    else
    {
        const size_t kMaxRows = 22;
        for (size_t i = 0; i < d.lines.size() && i < kMaxRows; i++)
            rows.push_back(MakeRow(d.lines[i], kCombo));
        if (d.lines.size() > kMaxRows)
            rows.push_back({ "... " + std::to_string(d.lines.size() - kMaxRows) + " more line(s) in the console", kNote });
        if (!d.message.empty())
            rows.push_back(MakeRow(d.message, kNote));
    }

    // botox publishes a start time with every answer; here a new answer = the rows changed
    const float now = (float)(GetTickCount64() / 1000.0);
    static std::vector<std::string> s_lastLines;
    static std::string s_lastMsg;
    static bool  s_lastPixel = false, s_lastSolving = false;
    static float s_started = 0.f;
    if (d.lines != s_lastLines || d.message != s_lastMsg || d.pixelSolve != s_lastPixel || d.solving != s_lastSolving)
    {
        s_lastLines = d.lines; s_lastMsg = d.message; s_lastPixel = d.pixelSolve; s_lastSolving = d.solving;
        s_started = now;
    }
    // botox: busy popup "started one fade-in ago: full alpha, never times out while the worker runs"
    const float started = d.solving ? now - 0.25f : s_started;
    if (rows.empty() || started <= 0.f) return;

    ImFont* font = g_menuReg ? g_menuReg : ImGui::GetFont();   // botox verdana 11
    ImFont* bold = g_menuBold ? g_menuBold : font;              // botox verdana bold 11
    const char* title = d.pixelSolve ? "pixel calculator" : "route calculator";
    auto TextW = [](ImFont* f, const char* s) { return f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.f, s).x; };

    constexpr float k_pad      = 8.f;
    constexpr float k_fade_in  = 0.25f;
    constexpr float k_fade_out = 0.2f;
    const float visible_time = (float)std::clamp(g_rcPopupTime, 1, 30);
    const float total_time   = k_fade_in + visible_time + k_fade_out;
    const float age          = now - started;
    if (age < 0.f || age >= total_time) return;

    float alpha = 1.f;
    if (age < k_fade_in)                       alpha = age / k_fade_in;
    else if (age > k_fade_in + visible_time)   alpha = 1.f - (age - k_fade_in - visible_time) / k_fade_out;
    alpha = std::clamp(alpha, 0.f, 1.f);

    auto RowFont = [&](const Row& r) { return r.kind == kHead ? bold : font; };

    const float row_h = font->FontSize + 2.f;   // botox GetTextLineHeight() + 2 with verdana 11 current
    float body_w = 0.f;
    for (const Row& r : rows) body_w = std::max(body_w, TextW(RowFont(r), r.text.c_str()));
    body_w = std::max(body_w + k_pad * 4.f, TextW(bold, title) + k_pad * 4.f);

    constexpr float k_edge = 6.f;
    constexpr float k_gap  = 6.f;
    const float title_h   = bold->FontSize;
    const float content_h = row_h * (float)rows.size() - 2.f;
    const float body_h    = k_edge + title_h + k_gap + content_h + k_edge;

    const float box_x = (W - body_w) * 0.5f;
    // top is the anchor (a one-line box centred at 0.72), list grows down so it never climbs over
    // the crosshair. moved only if it would run off the bottom
    const float k_one_line_h = k_edge + title_h + k_gap + (row_h - 2.f) + k_edge;
    float box_y = H * 0.72f - k_one_line_h * 0.5f;
    if (box_y + body_h > H - 4.f) box_y = H - 4.f - body_h;
    box_y = std::max(box_y, 4.f);

    const int   a        = (int)(alpha * 255.f);
    const float rounding = ImGui::GetStyle().WindowRounding;
    DpiPanelScope block(dl);   // botox draw_popup: one stretch block

    dl->AddRectFilled(ImVec2(box_x, box_y), ImVec2(box_x + body_w, box_y + body_h), IM_COL32(15, 15, 15, a), rounding);
    dl->AddRect(ImVec2(box_x + 1.f, box_y + 1.f), ImVec2(box_x + body_w - 1.f, box_y + body_h - 1.f),
                IM_COL32(50, 50, 50, a), rounding);

    // accent countdown bar: shrinks to the centre over the popup's life
    ImColor accent_line = ImColor(ImGui::GetColorU32(ImGuiCol_Accent));
    accent_line.Value.w = alpha;
    const float bar_w = (body_w - 2.f) * (1.f - std::clamp(age / total_time, 0.f, 1.f));
    if (bar_w > 0.f)
    {
        const float centre_x = box_x + body_w * 0.5f;
        dl->AddRectFilled(ImVec2(centre_x - bar_w * 0.5f, box_y), ImVec2(centre_x + bar_w * 0.5f, box_y + 1.f), accent_line);
    }

    auto CentredText = [&](ImFont* f, const std::string& text, float y, ImU32 colour)
    {
        const float x = box_x + (body_w - TextW(f, text.c_str())) * 0.5f;
        dl->AddText(f, f->FontSize, ImVec2(x + 1.f, y + 1.f), IM_COL32(0, 0, 0, a / 4), text.c_str());
        dl->AddText(f, f->FontSize, ImVec2(x, y), colour, text.c_str());
    };

    CentredText(bold, title, box_y + k_edge, IM_COL32(255, 255, 255, a));

    float row_y = box_y + k_edge + title_h + k_gap;
    for (const Row& r : rows)
    {
        ImU32 colour = IM_COL32(240, 240, 240, a);
        if (r.kind == kErr)       colour = IM_COL32(199, 102, 102, a);
        else if (r.kind == kNote) colour = IM_COL32(140, 140, 140, a);
        else if (r.kind == kHead) colour = IM_COL32(170, 170, 170, a);
        CentredText(RowFont(r), r.text, row_y, colour);
        row_y += row_h;
    }
}

// Per-point ENTER window body (opened/closed by WndProcHook). Mask edits apply +
// persist instantly via PS_PointSetMask; a stale index (map change, point deleted
// by the movement-thread delete key) closes the window via the generation check.
void RenderPsPointWindow()
{
    if (s_psPointWin < 0) return;
    if (g_showMenu || !IsInGame() || PS_PointsGen() != s_psPointWinGen)
    {
        s_psPointWin = -1; // main menu opened over us / left game / list changed
        return;
    }
    Vec3 pos; int mask = 127; float radius = 0.f;
    if (!PS_PointGet(s_psPointWin, pos, mask, radius)) { s_psPointWin = -1; return; }

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 view = ImGui::GetMainViewport()->Size;   // logical (dpi) px, io.DisplaySize is the screen
    ImGui::SetNextWindowPos(ImVec2(view.x * 0.5f + 48.f, view.y * 0.5f - 24.f), ImGuiCond_Appearing);
    if (ImGui::Begin("ps point", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar))
    {
        // same chrome as the main menu: bold title, then bold-label + line section
        // heads. Was TextDisabled labels between Separators -- a second look for
        // the same job.
        // Not the menu chrome -- a standalone HUD popup. Default font is verdana 11 now
        // (botox menu spec section 6/9), so this pushes back to its old Tahoma 13 for
        // every Checkbox/Slider label below (they draw with "whatever font is current").
        ImGui::PushFont(g_fontReg ? g_fontReg : ImGui::GetFont());
        ImGui::PushFont(g_fontBold);
        ImGui::TextUnformatted("ps point");
        ImGui::PopFont();
        ImGui::TextDisabled("z %.3f", pos.z);

        MenuSectionHeader("allowed jumps");
        // labels in mask-bit order (movement.h PsTypeBit). One per row, like every
        // other checkbox list in the menu (this was the only 2-up column layout).
        static const char* kLabels[7] = { "mini", "regular", "mini+duck", "crouch", "long", "long+duck", "double jump" };
        int newMask = mask;
        for (int k = 0; k < 7; k++)
        {
            bool on = (newMask >> k) & 1;
            if (ImGui::Checkbox(kLabels[k], &on))
                newMask = on ? (newMask | (1 << k)) : (newMask & ~(1 << k));
        }
        if (newMask != mask)
            PS_PointSetMask(s_psPointWin, newMask);

        // per-point engage radius. Unchecked follows the global slider (stored 0),
        // checked pins this spot to its own distance regardless of what the slider is doing
        MenuSectionHeader("engage radius");
        bool bOverride = radius > 0.f;
        if (ImGui::Checkbox("override radius", &bOverride))
            PS_PointSetRadius(s_psPointWin, bOverride ? (g_psAssistRadius > 0.f ? g_psAssistRadius : 300.f) : 0.f);
        if (bOverride)
        {
            float flNew = radius;
            ImGui::Indent(10.f);            // menu's kSubIndent for an option of an option
            // label must be visible: the menu's slider draws its label AND its value
            // in one row gated on label_size.x > 0, so a "##hidden" label ate the
            // number too. Width comes from the column, so no PushItemWidth here.
            if (ImGui::SliderFloat("radius", &flNew, 40.f, 1024.f, "%.0f"))
                PS_PointSetRadius(s_psPointWin, flNew);
            ImGui::Unindent(10.f);
        }
        else
            ImGui::TextDisabled("using global: %.0f", g_psAssistRadius);

        MenuSectionHeader("point");
        if (ImGui::Button("delete point", ImVec2(-1.f, 0.f)))
        {
            PS_PointDelete(s_psPointWin);
            s_psPointWin = -1;
        }
        ImGui::TextDisabled("enter: close   backspace: delete");
        ImGui::PopFont();
    }
    ImGui::End();
}

// ==== Performance / latency ====================================================================
// One opt-in thing, not touching game state.
//   g_hiTimerRes -- timeBeginPeriod(1). fps_max sleeps, and Win11's default 15.6ms timer
//                   granularity makes those sleeps overshoot and jitter.
//
// DELETED 2026-09-05, DO NOT RE-ADD:
//   g_lowLatency -- a D3DQUERYTYPE_EVENT issued after every o_Present and then SPUN on until
//                   the GPU finished the frame. That is not a cost you can tune away: it is
//                   the point of the feature. Killing the driver's 1-3 frame queue makes the
//                   frame cost CPU + GPU instead of max(CPU, GPU), and the user measured it
//                   as net_graph out packets/s falling 60 -> 50 -- that row is ONE SEND PER
//                   RENDERED FRAME, so an fps loss reads exactly like packet loss.
//   g_hiPriority -- HIGH_PRIORITY_CLASS, which turned the flush's YieldProcessor() loop (a
//                   PAUSE spin, no yield to the scheduler) into a core held against the rest
//                   of hl2.exe for the whole GPU frame.

static bool s_timerRaised = false;

// Applied on change only, off the 100ms tick in h_Present.
void Perf_Tick()
{
    const bool want = g_perfEnable && g_hiTimerRes;   // misc "performance" master + its popup row
    if (want != s_timerRaised) {
        if (want) { timeBeginPeriod(1); s_timerRaised = true;  }
        else      { timeEndPeriod(1);   s_timerRaised = false; }
    }
}

// Eject. Runs on MainThread, not the render thread.
void Perf_Shutdown()
{
    if (s_timerRaised) { timeEndPeriod(1); s_timerRaised = false; }
}

Present_t o_Present = nullptr;
Reset_t   o_Reset   = nullptr;
EndScene_t o_EndScene = nullptr;
SetCursorPos_t o_SetCursorPos = nullptr;

// ==== EndScene: everything that MUST be recorded ==================================
// When obs owns the Present entry, h_Present runs after its capture and nothing drawn there
// reaches the recording. EndScene runs before Present is entered at all, so this half lands
// in the backbuffer whoever wins that race.
// Backbuffer scenes per frame are NOT a fixed count -- measured 3, 4, then bursts of
// 735 / 4105 / 9010 between two Presents -- so "draw on the Nth scene" loses the crosshair
// on any frame that never reaches N. Draw in EVERY scene, which always includes the last.
// The imgui frame is built once; later scenes re-submit the same draw data.

// ==== Draw-data split =============================================================
// ONE imgui context, one frame, so input/hover/focus need nothing routed between contexts.
// Only the RENDER is split:
//   game lists -> vgui's PaintTraverse, i.e. the game's own UI pass, long before Present
//                 is entered                                       => always recorded
//   menu lists -> h_Present, after a capture hook owning the Present entry has already
//                 copied the backbuffer  => not recorded, and on top of the crosshair
// DO NOT re-add either of the two things this replaced. A second context made the menu
// undraggable and unclickable (hover hit-tests the PREVIOUS frame's rects and the two
// contexts never agreed which frame that was); a second window (the old overlaywin) put
// the menu outside the game and broke ShadowPlay.
// Since the overlay move: "game" = the crosshair ONLY (its own list, s_chList). Every other
// overlay (esp, hud panels, world fx, their windows) is "ov" and renders in h_Present like the
// menu: on top of the finished game frame instead of inside its UI pass.
static ImDrawList* s_gameLists[64]; static int s_gameCount = 0;
static ImDrawList* s_ovLists[64];   static int s_ovCount = 0;
static ImDrawList* s_menuLists[64]; static int s_menuCount = 0;
static ImDrawList* s_chList = nullptr;   // crosshair, drawn in place of the background list (RenderCrosshairList)
static bool        s_chOverBlur = false;   // menu blur drew this frame: crosshair moves to the Present pass, over it
static ImDrawList* s_fadeLists[64]; static int s_fadeCount = 0;   // botox menu window + its popups: one faded layer
static MenuFadedList s_popLists[16]; static int s_popCount = 0;    // confirm popups + ghosts, each at its own alpha, over the menu

// Per-frame state, all reset at the end of h_Present.
static bool s_frameBuilt  = false;   // imgui frame for THIS frame exists
static bool s_vguiDrew    = false;   // vgui pass rendered the game lists -> EndScene stands down
// True only while h_Present draws the menu subset. That block ends with dev->EndScene(),
// which goes through OUR OWN EndScene hook -- so without this flag the fallback path in
// h_EndScene draws the crosshair a second time, inside the menu's scene, i.e. ON TOP of
// the menu (and at double alpha). Recording is untouched either way: this extra draw
// happened after any capture hook copied the backbuffer, so it was screen-only.
static bool s_menuScene   = false;

static void SplitDrawData()
{
    s_gameCount = s_ovCount = s_menuCount = s_fadeCount = s_popCount = 0;
    ImDrawData* dd = ImGui::GetDrawData();
    if (!dd || !dd->Valid) return;

    // the crosshair is not in dd (swapped out of the viewport before Render). Inside the game's
    // frame, recorded -- unless the menu blur is up: then it goes over the blur, below.
    const bool chDrew = s_chList && s_chList->VtxBuffer.Size > 0;
    if (chDrew && !s_chOverBlur) s_gameLists[s_gameCount++] = s_chList;

    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiWindow* menuRoot = ImGui::FindWindowByName("botox");
    ImGuiWindow* psRoot   = ImGui::FindWindowByName("ps point");

    for (int i = 0; i < dd->CmdListsCount; ++i)
    {
        ImDrawList* dl = dd->CmdLists[i];
        bool menu = false, fade = false;

        // The foreground list holds the boot screen, a few panels' top layer and imgui's
        // software cursor (menu up only). It renders after every window, so it goes with the
        // menu layer: cursor over the menu, and both on top of the overlays.
        if (dl->_OwnerName && strcmp(dl->_OwnerName, "##Foreground") == 0)
            menu = true;

        // Walk the begin stack, not just the window: menu popups (colour picker, bind mode)
        // are their own root windows and would otherwise render in the recorded pass.
        for (int wi = 0; !menu && wi < g.Windows.Size; ++wi)
        {
            ImGuiWindow* w = g.Windows[wi];
            if (w->DrawList != dl) continue;
            for (ImGuiWindow* p = w; p; p = p->ParentWindowInBeginStack)
                if (p == menuRoot || p == psRoot) { menu = true; fade = (p == menuRoot); break; }
            break;
        }

        if (fade) { if (s_fadeCount < IM_ARRAYSIZE(s_fadeLists)) s_fadeLists[s_fadeCount++] = dl; }
        else if (menu) { if (s_menuCount < IM_ARRAYSIZE(s_menuLists)) s_menuLists[s_menuCount++] = dl; }
        else if (s_ovCount < IM_ARRAYSIZE(s_ovLists))
        {
            s_ovLists[s_ovCount++] = dl;
            // menu blur sits on the background list: the crosshair right after it, so the blur
            // and darken never reach it (they did not when it shared that list either)
            if (chDrew && s_chOverBlur && strcmp(dl->_OwnerName ? dl->_OwnerName : "", "##Background") == 0 &&
                s_ovCount < IM_ARRAYSIZE(s_ovLists))
                s_ovLists[s_ovCount++] = s_chList;
        }
    }
    s_popCount = Menu_SplitPopups(s_fadeLists, s_fadeCount, s_popLists, IM_ARRAYSIZE(s_popLists));
}

// Render one subset through a copy of the frame's ImDrawData. CmdLists is a plain array
// here, so the copy just points at ours; the ImDrawList objects stay owned by the context.
// slide = px down; DisplayPos feeds the backend's projection + scissor, so both move, no vertex touched
static void RenderSubset(ImDrawList** lists, int count, float fade = 1.f, float slide = 0.f)
{
    if (count <= 0) return;
    ImDrawData* src = ImGui::GetDrawData();
    if (!src || !src->Valid) return;

    ImDrawData dd = *src;
    dd.DisplayPos.y -= slide;
    dd.CmdLists      = lists;
    dd.CmdListsCount = count;
    dd.TotalVtxCount = dd.TotalIdxCount = 0;
    for (int i = 0; i < count; ++i)
    {
        dd.TotalVtxCount += lists[i]->VtxBuffer.Size;
        dd.TotalIdxCount += lists[i]->IdxBuffer.Size;
    }
    if (fade < 0.999f) ImGui_ImplDX9_RenderDrawDataFaded(&dd, fade);
    else               ImGui_ImplDX9_RenderDrawData(&dd);
}

// ==== fonts ======================================================================
// Menu = Tahoma 13 plain + bold. The movement indicator tags get a THIRD entry, built
// at the exact pixel size the scale slider asks for: drawing the 12px bold face at 2.5x
// is a bitmap stretch, and that stretch is the blur. A face rasterized at 30px is sharp.
static float s_miFontPx = 0.f;

// fonts tab (fonts.h): the base face at the indicator font page size (botox custom_font_name_indicator)
static float MiFontPx() { return FontIndPx(g_fontIndSize, 1.f); }

// botox indicator_font_set_t: the base face re-baked at every indicator's scale != 1 (velocity,
// keybinds, keystrokes; botox's 4th is stamina), so a 2x label is a 2x raster, not a stretch.
// IndicatorFont hands out the baked one while it matches, else the base face scaled.
struct IndScaleSet { int count = 0; float scales[3] = {}; bool operator==(const IndScaleSet&) const = default; };
static IndScaleSet s_indBuilt;
static ImFont*     s_indFonts[3] = {};
static FontSettle  s_indScaleSettle;

static IndScaleSet WantedIndScales()
{
    IndScaleSet r;
    for (float scale : { g_viScale, g_kiScale, g_kpScale })
    {
        scale = std::clamp(scale, kIndScaleMin, kIndScaleMax);
        if (std::fabs(scale - 1.f) < 0.001f || std::find(r.scales, r.scales + r.count, scale) != r.scales + r.count) continue;
        r.scales[r.count++] = scale;
    }
    return r;
}

static uint32_t IndScaleSig(const IndScaleSet& s)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < s.count; ++i)
    {
        uint32_t v; memcpy(&v, &s.scales[i], 4);
        for (int b = 0; b < 4; ++b) { h ^= (v >> (b * 8)) & 0xFF; h *= 16777619u; }
    }
    return h ? h : 1u;
}

ImFont* IndicatorFont(float scale, float& fs)
{
    ImFont* const base = g_fontMi;
    const float s = std::clamp(scale, kIndScaleMin, kIndScaleMax);
    fs = base ? base->FontSize * s : 0.f;
    for (int i = 0; i < s_indBuilt.count; ++i)
        if (s_indBuilt.scales[i] == s && s_indFonts[i]) return s_indFonts[i];
    return base;
}

// Indicator font picked in the fonts tab. One unloadable face fails the WHOLE freetype atlas
// build (ImFontAtlasBuildWithFreeTypeEx returns false on the first bad face -> no texture, no
// menu), so a custom face is test-built alone first; anything that fails or has no Latin
// glyphs falls back to the stock verdanab. Returns the path to add, "" for the stock face.
static uint32_t s_indApplied = 0;   // Fonts_IndSig() of what the atlas holds
static FontSettle s_indSettle;
// One unloadable face fails the whole atlas, so a picked one is built alone first.
static bool TestBuildFont(const std::string& path, float px, int builderFlags)
{
    ImFontAtlas test;
    ImFontConfig cfg;
    cfg.FontBuilderFlags = builderFlags;
    ImFont* f = test.AddFontFromFileTTF(path.c_str(), px, &cfg, test.GetGlyphRangesDefault());
    return f && test.Build() && f->FindGlyphNoFallback('0') && f->FindGlyphNoFallback('A');
}
static float EspFontPx() { return (float)std::clamp(g_fontEspSize, 6, 100); }

static std::string ValidIndicatorFont(float px, int builderFlags)
{
    if (g_fontIndSize <= 0) return "";   // botox: size 0 = stock font
    // the default pick IS the stock face: no font-folder scan, no test build at startup
    if (_stricmp(g_fontIndName, kFontIndDefName) == 0 && g_fontIndFlags == kFontIndDefFlags) return "";
    const std::string path = Fonts_FindPath(g_fontIndName);
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        DebugLog("fonts: indicator font '%s' not found (%s) -- stock verdanab", g_fontIndName, path.c_str());
        return "";
    }
    if (TestBuildFont(path, px, builderFlags)) return path;
    DebugLog("fonts: indicator font '%s' failed to build -- stock verdanab", path.c_str());
    return "";
}

// esp font page -> the ImGui face player/building esp draws with (aim/esp.cpp via RenderEspQueue).
// Same rules as the indicator page: size 0 or the untouched default pick = botox's stock esp face
// (verdana bd 11 mono); a face that will not build falls back to it. The page lists GDI families
// (they also drive the vgui face of the world-entity labels), mapped to a file by Fonts_FamilyPath.
static uint32_t s_espImApplied = 0;   // Fonts_EspSig() of what the atlas holds
static FontSettle s_espSettle;
static std::string ValidEspFont()
{
    if (g_fontEspSize <= 0) return "";
    if (_stricmp(g_fontEspName, kFontEspDefName) == 0 && g_fontEspSize == kFontEspDefSize && g_fontEspFlags == kFontEspDefFlags) return "";
    const std::string path = Fonts_FamilyPath(g_fontEspName);
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        DebugLog("fonts: esp font '%s' not found (%s) -- stock verdana bd 11", g_fontEspName, path.c_str());
        return "";
    }
    if (!TestBuildFont(path, EspFontPx(), g_fontEspFlags & 0xFF))
    {
        DebugLog("fonts: esp font '%s' failed to build -- stock verdana bd 11", path.c_str());
        return "";
    }
    return path;
}

// Clear() frees every ImFont, so EVERY font pointer we keep is reassigned here and
// nothing may hold one across the call.
// lua fonts.* ids (lua_draw.h LuaFontId) -> faces. Render thread only, like every ImFont* read.
static ImFont* LuaFontFor(int id)
{
    ImFont* f = nullptr;
    switch (id)
    {
    case LUAF_VERDANA:          f = g_menuReg; break;
    case LUAF_VERDANA_BOLD:     f = g_menuBold; break;
    case LUAF_ICONS:            f = g_fontIcon13; break;
    case LUAF_INDICATOR:
    case LUAF_CUSTOM_INDICATOR: f = g_fontMi; break;
    case LUAF_TAHOMA:           f = g_fontTahoma12; break;
    case LUAF_TAHOMA_BOLD:      f = g_fontTahomaBd12; break;
    case LUAF_CUSTOM_ESP:       f = s_fontEsp; break;
    case LUAF_CUSTOM_NET_GRAPH: f = HudFonts_NetGraphFont(); break;
    }
    return f ? f : (g_menuReg ? g_menuReg : ImGui::GetIO().Fonts->Fonts.empty() ? nullptr : ImGui::GetIO().Fonts->Fonts[0]);
}

// after io.Fonts->Build(): the game thread measures render.text_size off these (it may not touch the atlas)
static void LuaPublishFontMetrics()
{
    static const int kIds[k_luaFontSlots] = { LUAF_VERDANA, LUAF_VERDANA_BOLD, LUAF_ICONS, LUAF_INDICATOR, LUAF_TAHOMA, LUAF_TAHOMA_BOLD,
                                              LUAF_CUSTOM_INDICATOR, LUAF_CUSTOM_ESP, LUAF_CUSTOM_NET_GRAPH };
    LuaFontMetrics out[k_luaFontSlots];
    for (int slot = 0; slot < k_luaFontSlots; ++slot)
    {
        ImFont* f = LuaFontFor(kIds[slot]);
        if (!f) continue;
        LuaFontMetrics& m = out[slot];
        m.ok = true;
        m.size = f->FontSize;
        m.fallback = f->FallbackAdvanceX;
        for (int c = 0; c < 256; ++c)
            m.adv[c] = c < f->IndexAdvanceX.Size ? f->IndexAdvanceX[c] : f->FallbackAdvanceX;
    }
    LuaDrawLockGuard lock;
    for (int slot = 0; slot < k_luaFontSlots; ++slot) g_luaFontMetrics[slot] = out[slot];
}

static void BuildFontAtlas()
{
    ImGuiIO& io = ImGui::GetIO();
    Dpi_DropFonts();   // twins point at the fonts Clear() frees
    io.Fonts->Clear();

    // Tahoma, ANTIALIASED. Was MonoHinting|Monochrome = 1-bit coverage, i.e.
    // every glyph edge hard on/off -- that is what made the old menu read as
    // aliased. LightHinting keeps the vertical stems snapped to the pixel grid
    // (so 12px stays crisp) while the rasterizer still emits 8-bit coverage.
    ImFontConfig cfg;
    cfg.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;

    // botox_menu_spec.md section 6/9: verdana 11 must be the FIRST font added, so it is
    // io.FontDefault -- every ImGui widget that draws with "whatever font is current" (every
    // Checkbox/Slider/Combo label in the menu) gets botox's chrome face, not Tahoma. Loaded
    // from the system TTF (spec section 6 explicitly allows this over embedding botox's
    // compressed blob) with the same FontBuilderFlags botox's build_fonts() uses.
    ImFontConfig verdanaCfg = { };
    verdanaCfg.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_Monochrome | ImGuiFreeTypeBuilderFlags_MonoHinting;
    g_menuReg = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\verdana.ttf", 11.f, &verdanaCfg,
        io.Fonts->GetGlyphRangesDefault());
    io.FontDefault = g_menuReg ? g_menuReg : io.Fonts->AddFontDefault();
    g_menuBold = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\verdanab.ttf", 11.f, &verdanaCfg,
        io.Fonts->GetGlyphRangesDefault());
    if (!g_menuBold) g_menuBold = io.FontDefault;

    // Tahoma 13 plain + bold: every OTHER menu/HUD label still draws with these, explicitly
    // (g_fontReg/g_fontBold), so switching the ImGui default above changes nothing for them.
    // 13, not 12: the menu was asked for bigger labels. Every row pitch and every box
    // height is measured off the font, so this is the only place the size lives.
    g_fontReg = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Tahoma.ttf", 13.f, &cfg,
        io.Fonts->GetGlyphRangesDefault());
    if (!g_fontReg) g_fontReg = io.FontDefault;
    // Bold = tahomabd.ttf, same size, same flags -- delusional-fix menu_font_bold
    g_fontBold = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\tahomabd.ttf", 13.f, &cfg,
        io.Fonts->GetGlyphRangesDefault());
    if (!g_fontBold) g_fontBold = io.FontDefault;

    s_miFontPx = MiFontPx();
    s_indApplied = Fonts_IndSig();
    {
        const std::string custom = ValidIndicatorFont(s_miFontPx, g_fontIndFlags & 0xFF);
        // botox font_name_indicator_29: verdana bold, freetype default flags
        ImFontConfig indCfg;
        indCfg.FontBuilderFlags = custom.empty() ? 0 : (g_fontIndFlags & 0xFF);   // FontFlagBit i == builder flag 1 << i
        const std::string path = custom.empty() ? std::string("C:\\Windows\\Fonts\\verdanab.ttf") : custom;
        g_fontMi = io.Fonts->AddFontFromFileTTF(path.c_str(), s_miFontPx, &indCfg, io.Fonts->GetGlyphRangesDefault());
        if (!custom.empty())
            DebugLog("fonts: indicator font %s %.0fpx flags 0x%x", custom.c_str(), s_miFontPx, g_fontIndFlags & 0xFF);

        s_indBuilt = WantedIndScales();
        s_indScaleSettle.seen = IndScaleSig(s_indBuilt);
        for (int i = 0; i < 3; ++i) s_indFonts[i] = nullptr;
        if (g_fontMi)
            for (int i = 0; i < s_indBuilt.count; ++i)
                s_indFonts[i] = io.Fonts->AddFontFromFileTTF(path.c_str(), std::clamp(s_miFontPx * s_indBuilt.scales[i], 1.f, 128.f),
                                                             &indCfg, io.Fonts->GetGlyphRangesDefault());
    }
    if (!g_fontMi) g_fontMi = g_fontBold;

    // ==== botox render.cpp build_fonts(), donor block 1:1 ==================================
    // Same face, same px, same ImGuiFreeTypeBuilderFlags per font -- the flags ARE the look
    // (Monochrome|MonoHinting = the crisp 1-bit tahoma every 2015-era donor had, Bitmap =
    // sf pro's embedded strikes, NoHinting = the soft segoe/montserrat). Cyrillic range where
    // botox passes it. Botox's merge_extended_glyphs (a second face merged for the glyphs the
    // donor lacks) is skipped: latin/cyrillic coverage is the donor's own, nothing visible
    // changes for the ascii every HUD string here is.
    // Merged blocks make the atlas wide: botox caps it so the driver never refuses the texture.
    io.Fonts->TexDesiredWidth = 4096;
    io.Fonts->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;

    const ImWchar* cyr = io.Fonts->GetGlyphRangesCyrillic();
    ImFontConfig mono = { };   // botox verdana_font_config / tahoma_font_config
    mono.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_Monochrome | ImGuiFreeTypeBuilderFlags_MonoHinting;
    const auto sys = [&](const char* file, float px, const ImFontConfig& c, const ImWchar* ranges = nullptr) -> ImFont* {
        char path[MAX_PATH]; snprintf(path, sizeof(path), "C:\\Windows\\Fonts\\%s", file);
        return io.Fonts->AddFontFromFileTTF(path, px, &c, ranges ? ranges : io.Fonts->GetGlyphRangesDefault());
    };
    const auto flagged = [&](unsigned flags) { ImFontConfig c = { }; c.FontBuilderFlags = flags; return c; };
    // botox add_donor_font: static bytes, PixelSnapH, 5x horizontal oversample, cyrillic
    const auto donor = [&](const unsigned char* data, int size, float px, unsigned flags) -> ImFont* {
        ImFontConfig c = { };
        c.FontDataOwnedByAtlas = false;
        c.PixelSnapH = true;
        c.OversampleH = 5;
        c.OversampleV = 1;
        c.FontBuilderFlags = flags;
        return io.Fonts->AddFontFromMemoryTTF((void*)data, size, px, &c, cyr);
    };
    // botox's plain AddFontFromMemoryTTF path (interium/skebob/dna/airflow configs): owned=false only
    const auto plain = [&](const unsigned char* data, int size, float px, unsigned flags, const ImWchar* ranges) -> ImFont* {
        ImFontConfig c = { };
        c.FontDataOwnedByAtlas = false;
        c.FontBuilderFlags = flags;
        return io.Fonts->AddFontFromMemoryTTF((void*)data, size, px, &c, ranges);
    };
    const auto orElse = [](ImFont*& f, ImFont* fallback) { if (!f) f = fallback; };

    g_fontTahoma12   = sys("tahoma.ttf", 12.f, mono);                      orElse(g_fontTahoma12, g_fontReg);
    g_fontTahomaBd12 = sys("tahomabd.ttf", 12.f, mono);                    orElse(g_fontTahomaBd12, g_fontBold);
    g_fontChillware13 = sys("verdanab.ttf", 13.f, flagged(ImGuiFreeTypeBuilderFlags_NoHinting)); orElse(g_fontChillware13, g_fontBold);

    // menu theme lobotomy v1 pair -- tahoma face, menu-only sizes
    g_fontMenuTahoma9  = sys("tahoma.ttf", 9.f, flagged(ImGuiFreeTypeBuilderFlags_Monochrome | ImGuiFreeTypeBuilderFlags_MonoHinting | ImGuiFreeTypeBuilderFlags_Bitmap), cyr);
    orElse(g_fontMenuTahoma9, g_fontReg);
    g_fontMenuTahomaBd9 = sys("tahomabd.ttf", 9.f, flagged(ImGuiFreeTypeBuilderFlags_Monochrome | ImGuiFreeTypeBuilderFlags_MonoHinting | ImGuiFreeTypeBuilderFlags_Bitmap), cyr);
    orElse(g_fontMenuTahomaBd9, g_fontBold);

    g_fontLobotomy13     = donor(sf_pro_display_semibold, sizeof(sf_pro_display_semibold), 13.f, ImGuiFreeTypeBuilderFlags_Bitmap);
    g_fontLobotomyBold13 = donor(sf_pro_display_bold, sizeof(sf_pro_display_bold), 13.f, ImGuiFreeTypeBuilderFlags_Bitmap);
    g_fontKamibebra13     = donor(rubik_regular, sizeof(rubik_regular), 13.f, ImGuiFreeTypeBuilderFlags_Bitmap);
    g_fontKamibebraBold13 = donor(rubik_medium, sizeof(rubik_medium), 13.f, ImGuiFreeTypeBuilderFlags_Bitmap | ImGuiFreeTypeBuilderFlags_LightHinting);
    orElse(g_fontLobotomy13, g_fontBold); orElse(g_fontLobotomyBold13, g_fontLobotomy13);
    orElse(g_fontKamibebra13, g_fontBold); orElse(g_fontKamibebraBold13, g_fontKamibebra13);
    g_fontKamibebra22 = donor(rubik_regular, sizeof(rubik_regular), 22.f, ImGuiFreeTypeBuilderFlags_Bitmap); orElse(g_fontKamibebra22, g_fontKamibebra13);

    g_fontCumhacck35    = sys("verdanab.ttf", 35.f, mono);                   orElse(g_fontCumhacck35, g_fontBold);
    g_fontAirplaneTitle = donor(montserrat_regular, sizeof(montserrat_regular), 10.f, 0); orElse(g_fontAirplaneTitle, g_fontBold);
    g_fontPoints18 = donor(montserrat_medium_italic, sizeof(montserrat_medium_italic), 18.f, ImGuiFreeTypeBuilderFlags_Bitmap); orElse(g_fontPoints18, g_fontBold);   // misc
    g_fontAirplaneRows  = sys("tahomabd.ttf", 11.f, flagged(ImGuiFreeTypeBuilderFlags_MonoHinting)); orElse(g_fontAirplaneRows, g_fontBold);

    g_fontClarityIcon  = donor(clarity_icon_font, sizeof(clarity_icon_font), 50.f, ImGuiFreeTypeBuilderFlags_NoHinting);
    g_fontClarityInter = donor(inter_semibold, sizeof(inter_semibold), 14.f, 0);     orElse(g_fontClarityInter, io.FontDefault);
    orElse(g_fontClarityIcon, g_fontClarityInter);
    g_fontInterBold15   = donor(inter_bold, sizeof(inter_bold), 15.f, 0);            orElse(g_fontInterBold15, g_fontBold);
    g_fontInterMedium15 = donor(inter_medium, sizeof(inter_medium), 15.f, 0);        orElse(g_fontInterMedium15, g_fontInterBold15);
    g_fontSunflower30   = donor(sunflower_medium, sizeof(sunflower_medium), 30.f, 0); orElse(g_fontSunflower30, g_fontBold);

    g_fontInterwebzCalibri = sys("calibri.ttf", 16.f, mono);                 orElse(g_fontInterwebzCalibri, g_fontBold);
    g_fontHavocTahoma13    = sys("tahoma.ttf", 13.f, mono);                  orElse(g_fontHavocTahoma13, g_fontReg);

    const ImFontConfig noHint = flagged(ImGuiFreeTypeBuilderFlags_NoHinting);   // botox evolve_config / inba_config
    g_fontEvolve16     = sys("segoeui.ttf", 16.f, noHint);                   orElse(g_fontEvolve16, io.FontDefault);
    g_fontEvolveBold16 = sys("segoeuib.ttf", 16.f, noHint);                  orElse(g_fontEvolveBold16, g_fontBold);

    g_fontLegendwareVerdana12 = sys("verdana.ttf", 12.f, flagged(ImGuiFreeTypeBuilderFlags_MonoHinting)); orElse(g_fontLegendwareVerdana12, g_fontReg);
    g_fontLegendwareLucida10  = sys("lucon.ttf", 10.f, mono);                orElse(g_fontLegendwareLucida10, g_fontReg);

    g_fontInteriumDroid24 = plain(droid_sans_bold, sizeof(droid_sans_bold), 24.f, 0, cyr); orElse(g_fontInteriumDroid24, g_fontBold);
    g_fontSkebobNunito14  = plain(nunito_medium, sizeof(nunito_medium), 14.f, 0, cyr);     orElse(g_fontSkebobNunito14, g_fontBold);
    g_fontSkebobArial12   = sys("arial.ttf", 12.f, ImFontConfig{ }, cyr);                   orElse(g_fontSkebobArial12, g_fontReg);
    g_fontSkebobArial13   = sys("arial.ttf", 13.f, ImFontConfig{ }, cyr);                   orElse(g_fontSkebobArial13, g_fontReg);

    g_fontCumidereTahoma16 = sys("tahoma.ttf", 16.f, flagged(ImGuiFreeTypeBuilderFlags_LightHinting)); orElse(g_fontCumidereTahoma16, g_fontReg);
    g_fontIllusoryTahoma13 = sys("tahoma.ttf", 13.f, flagged(ImGuiFreeTypeBuilderFlags_MonoHinting | ImGuiFreeTypeBuilderFlags_Bold)); orElse(g_fontIllusoryTahoma13, g_fontBold);
    g_fontLumiRubik16      = donor(rubik_regular, sizeof(rubik_regular), 16.f, ImGuiFreeTypeBuilderFlags_MonoHinting); orElse(g_fontLumiRubik16, g_fontReg);
    {
        // ImGui's built-in ProggyClean, botox lumi_proggy_config. A second AddFontDefault:
        // io.FontDefault above is verdana, so this is the only proggy in the atlas.
        ImFontConfig c = flagged(ImGuiFreeTypeBuilderFlags_MonoHinting | ImGuiFreeTypeBuilderFlags_Monochrome);
        g_fontLumiProggy13 = io.Fonts->AddFontDefault(&c);
    }
    g_fontInbaTahoma13 = sys("tahoma.ttf", 13.f, noHint);                    orElse(g_fontInbaTahoma13, g_fontReg);

    g_fontDnaMontserrat15  = plain(dna_montserrat_medium, sizeof(dna_montserrat_medium), 15.f, ImGuiFreeTypeBuilderFlags_NoHinting, cyr);
    g_fontDnaPtRootBold15  = plain(pt_root_ui_bold, sizeof(pt_root_ui_bold), 15.f, ImGuiFreeTypeBuilderFlags_NoHinting, cyr);
    g_fontDnaClarityIcon50 = plain(dna_clarity_icon, sizeof(dna_clarity_icon), 50.f, ImGuiFreeTypeBuilderFlags_NoHinting, nullptr);
    orElse(g_fontDnaMontserrat15, g_fontReg); orElse(g_fontDnaPtRootBold15, g_fontBold); orElse(g_fontDnaClarityIcon50, g_fontClarityIcon);

    g_fontCucumberTahoma14  = sys("tahoma.ttf", 14.f, noHint);               orElse(g_fontCucumberTahoma14, g_fontReg);
    g_fontCucumberVerdana14 = sys("verdana.ttf", 14.f, noHint);              orElse(g_fontCucumberVerdana14, g_fontCucumberTahoma14);
    g_fontHowewewareMinecraft14 = plain(n_mc_assets::k_minecraft_ttf, sizeof(n_mc_assets::k_minecraft_ttf), 14.f, ImGuiFreeTypeBuilderFlags_NoHinting, cyr);
    orElse(g_fontHowewewareMinecraft14, g_fontBold);

    // chifirium gamesense: 15 px atlas, drawn at 13
    g_fontGamesenseSfBold15 = donor(sf_pro_display_bold, sizeof(sf_pro_display_bold), 15.f, ImGuiFreeTypeBuilderFlags_Bitmap); orElse(g_fontGamesenseSfBold15, g_fontBold);

    g_fontAirflow14 = plain(sf_ui_display_semibold, sizeof(sf_ui_display_semibold), 14.f, 0, cyr); orElse(g_fontAirflow14, g_fontBold);

    // heroin-new (Documents\!) FUN_18004d980: arialbd 16 = music display,
    // AddFontDefault = menu + watermark + speclist "regular", tahoma 16 / tahomabd 14 = "interwebz", tahoma 14 = legacy
    {
        ImFontConfig h = flagged(ImGuiFreeTypeBuilderFlags_LightHinting);   // OversampleH 2, PixelSnapH
        h.OversampleH = 2; h.OversampleV = 1; h.PixelSnapH = true;
        g_fontHeroinArialBd16 = sys("arialbd.ttf", 16.f, h);                  orElse(g_fontHeroinArialBd16, g_fontBold);
        ImFontConfig p = h;
        g_fontHeroinProggy13  = io.Fonts->AddFontDefault(&p);                 orElse(g_fontHeroinProggy13, g_fontReg);
    }
    g_fontHeroinTahoma16   = sys("tahoma.ttf", 16.f, mono);                   orElse(g_fontHeroinTahoma16, g_fontReg);
    g_fontHeroinTahomaBd14 = sys("tahomabd.ttf", 14.f, mono);                 orElse(g_fontHeroinTahomaBd14, g_fontBold);
    g_fontHeroinTahoma14   = sys("tahoma.ttf", 14.f, mono);                   orElse(g_fontHeroinTahoma14, g_fontReg);

    // botox esp face: font_name_verdana_bd_11, verdana_font_config (Monochrome|MonoHinting), what
    // the "esp font" page defaults to. Cyrillic for player names.
    s_espImApplied = Fonts_EspSig();
    s_fontEsp = nullptr;
    if (const std::string custom = ValidEspFont(); !custom.empty())
    {
        ImFontConfig c = { };
        c.FontBuilderFlags = g_fontEspFlags & 0xFF;   // FontFlagBit i == builder flag 1 << i
        s_fontEsp = io.Fonts->AddFontFromFileTTF(custom.c_str(), EspFontPx(), &c, cyr);
        DebugLog("fonts: esp font %s %.0fpx flags 0x%x", custom.c_str(), EspFontPx(), g_fontEspFlags & 0xFF);
    }
    if (!s_fontEsp) s_fontEsp = sys("verdanab.ttf", 11.f, mono, cyr);
    orElse(s_fontEsp, g_menuBold);
    if (s_fontEsp) g_espQFontH.store(s_fontEsp->FontSize, std::memory_order_relaxed);

    // botox render.cpp: font_name_icon_13, compressed icon font, 0xe005..0xf8ff
    static const ImWchar kIconRanges[] = { 0xe005, 0xf8ff, 0 };
    g_fontIcon13 = io.Fonts->AddFontFromMemoryCompressedTTF(icon_font_compressed_data, icon_font_compressed_size, 13.f, nullptr, kIconRanges);

    HudFonts_Build();   // net graph face

    io.Fonts->Build();
    LuaPublishFontMetrics();
}

// botox draw_cached_data: replay the esp the game thread recorded (aim/esp.cpp, esp_queue.h) into
// the background list, first, so every other overlay sits on top of it. A frame older than
// 250 ms is a dead one (no local player, level change, paint stopped): nothing is drawn.
static void RecolourVertsY(ImDrawList* dl, int first, uint32_t topCol, uint32_t botCol, float y0, float y1)
{
    const ImVec4 top = ImGui::ColorConvertU32ToFloat4(topCol), bot = ImGui::ColorConvertU32ToFloat4(botCol);
    const float hgt = std::max(y1 - y0, 1.f);
    for (int i = first; i < dl->VtxBuffer.Size; ++i)
    {
        ImDrawVert& v = dl->VtxBuffer[i];
        const float t = std::clamp((v.pos.y - y0) / hgt, 0.f, 1.f), cov = ((v.col >> IM_COL32_A_SHIFT) & 0xFF) / 255.f;
        v.col = ImGui::ColorConvertFloat4ToU32(ImVec4(top.x + (bot.x - top.x) * t, top.y + (bot.y - top.y) * t,
            top.z + (bot.z - top.z) * t, (top.w + (bot.w - top.w) * t) * cov));
    }
}

static void RenderEspQueue()
{
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImFont* font = s_fontEsp ? s_fontEsp : ImGui::GetFont();
    // copied out under the lock, replayed outside it: Avatar_Get can create a texture, and the game
    // thread's Commit must never spin behind that. s_rtEspSeq = the frame this build is for (multicore)
    static EspFrame f;
    EspQ::CopyFrame(s_rtEspSeq, f);
    if (f.cmds.empty() || GetTickCount64() - f.stampMs > 250)
        return;
    // esp.cpp projects in ENGINE screen pixels (H::Draw size); ImGui draws in its DisplaySize (window
    // client rect). Windowed + DPI scaling makes them differ, and unscaled every box and name lands
    // off by that ratio. Everything this replay adds is rescaled at the end.
    const ImGuiIO& io = ImGui::GetIO();
    const float sx = f.srcW > 0.f ? io.DisplaySize.x / f.srcW : 1.f, sy = f.srcH > 0.f ? io.DisplaySize.y / f.srcH : 1.f;
    static bool s_loggedSize = false;
    if (!s_loggedSize)
    {
        s_loggedSize = true;
        DebugLog("esp: engine screen %.0fx%.0f, imgui display %.0fx%.0f -> replay scale %.3f x %.3f",
            f.srcW, f.srcH, io.DisplaySize.x, io.DisplaySize.y, sx, sy);
    }
    const int firstVtx = dl->VtxBuffer.Size;
    const auto pt = [&](int i) { return ImVec2(f.pts[i * 2], f.pts[i * 2 + 1]); };
    float lastTextLeft = 0.f;
    ImVec2 buf[64];
    for (const EspCmd& c : f.cmds)
    {
        switch (c.type)
        {
        case ESPC_LINE:
            dl->AddLine(ImVec2(c.x0, c.y0), ImVec2(c.x1, c.y1), c.col, c.thick);
            break;
        case ESPC_RECT:
        {   // botox impl_t::rect
            const float inset = (c.thick - 1.f) * 0.5f;
            if (c.flags & ESPF_FILLED)
                dl->AddRectFilled(ImVec2(c.x0, c.y0), ImVec2(c.x1, c.y1), c.col, c.round);
            else
                dl->AddRect(ImVec2(c.x0 + inset, c.y0 + inset), ImVec2(c.x1 - inset, c.y1 - inset), c.col, std::max(c.round - inset, 0.f), 0, c.thick);
            const float off = 0.5f + c.othick * 0.5f, inner = off + 2.f * inset;
            if (c.flags & ESPF_OUT_INNER)
                dl->AddRect(ImVec2(c.x0 + inner, c.y0 + inner), ImVec2(c.x1 - inner, c.y1 - inner), c.col2, std::max(c.round - inner, 0.f), 0, c.othick);
            if (c.flags & ESPF_OUT_OUTER)
                dl->AddRect(ImVec2(c.x0 - off, c.y0 - off), ImVec2(c.x1 + off, c.y1 + off), c.col2, c.round > 0.f ? c.round + off : 0.f, 0, c.othick);
            break;
        }
        case ESPC_GRAD:
        {   // draw_type_gradient_rect: rounded = a white fill re-coloured per vertex by height
            if (c.round <= 0.f)
            {
                dl->AddRectFilledMultiColor(ImVec2(c.x0, c.y0), ImVec2(c.x1, c.y1), c.col, c.col, c.col2, c.col2);
                break;
            }
            const int first = dl->VtxBuffer.Size;
            dl->AddRectFilled(ImVec2(c.x0, c.y0), ImVec2(c.x1, c.y1), IM_COL32_WHITE, c.round);
            RecolourVertsY(dl, first, c.col, c.col2, c.y0, c.y1);
            break;
        }
        case ESPC_CORNER:
        {   // draw_type_corner_rect
            const float l = c.x0, t = c.y0, r = c.x1, b = c.y1, ax = c.ax, ay = c.ay, rad = c.round;
            const auto corner = [&](ImVec2 from, ImVec2 centre, int aMin, int aMax, ImVec2 to)
            {
                dl->PathLineTo(from);
                dl->PathArcToFast(centre, rad, aMin, aMax);
                dl->PathLineTo(to);
                dl->PathStroke(c.col, 0, c.thick);
            };
            corner(ImVec2(l, t + ay), ImVec2(l + rad, t + rad), 6, 9, ImVec2(l + ax, t));
            corner(ImVec2(r - ax, t), ImVec2(r - rad, t + rad), 9, 12, ImVec2(r, t + ay));
            corner(ImVec2(r, b - ay), ImVec2(r - rad, b - rad), 0, 3, ImVec2(r - ax, b));
            corner(ImVec2(l + ax, b), ImVec2(l + rad, b - rad), 3, 6, ImVec2(l, b - ay));
            break;
        }
        case ESPC_POLY:
        {
            const int n = std::min(c.pn, 64);
            if (n < 3) break;
            for (int i = 0; i < n; i++) buf[i] = pt(c.p0 + i);
            dl->AddConvexPolyFilled(buf, n, c.col);
            if (c.flags & ESPF_OUTLINE)
                dl->AddPolyline(buf, n, c.col2, ImDrawFlags_Closed, c.thick);
            break;
        }
        case ESPC_POLYLINE:
        {
            std::vector<ImVec2>& v = s_espPolyScratch;
            v.clear();
            for (int i = 0; i < c.pn; i++) v.push_back(pt(c.p0 + i));
            const bool grad = (c.flags & ESPF_GRADIENT) != 0;
            const int first = dl->VtxBuffer.Size;
            dl->AddPolyline(v.data(), (int)v.size(), grad ? IM_COL32_WHITE : c.col, ImDrawFlags_None, c.thick);
            if (grad)   // botox corner health bar: the fill recoloured top -> bottom over the track's height
                RecolourVertsY(dl, first, c.col, c.col2, c.y0, c.y1);
            break;
        }
        case ESPC_CIRCLE:
            if (c.thick < 0.f) dl->AddCircleFilled(ImVec2(c.x0, c.y0), c.x1, c.col, 12);   // thick < 0 = filled
            else               dl->AddCircle(ImVec2(c.x0, c.y0), c.x1, c.col, 32, c.thick);
            break;
        case ESPC_TEXT:
        {   // botox impl_t::text, text_flag_dropshadow
            const char* s = &f.text[c.p0];
            const ImVec2 sz = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, s);
            const ImVec2 at(floorf(c.x0 - sz.x * c.ax), floorf(c.y0 - sz.y * c.ay));
            lastTextLeft = at.x;
            if (c.flags & ESPF_SHADOW)
                dl->AddText(font, font->FontSize, ImVec2(at.x + 1.f, at.y + 1.f), c.col2, s);
            dl->AddText(font, font->FontSize, at, c.col, s);
            break;
        }
        case ESPC_AVATAR:
        {
            const ImTextureID tex = Avatar_Get(c.sid, false);
            if (!tex) break;
            const float dx = (c.flags & ESPF_LEFT_OF_TEXT) ? lastTextLeft : 0.f;
            dl->AddImageRounded(tex, ImVec2(c.x0 + dx, c.y0), ImVec2(c.x1 + dx, c.y1), ImVec2(0, 0), ImVec2(1, 1), c.col, c.round, ImDrawFlags_RoundCornersAll);
            break;
        }
        }
    }
    if (fabsf(sx - 1.f) > 1e-3f || fabsf(sy - 1.f) > 1e-3f)
        for (int i = firstVtx; i < dl->VtxBuffer.Size; ++i)
            dl->VtxBuffer[i].pos.x *= sx, dl->VtxBuffer[i].pos.y *= sy;
}

// Build the whole imgui frame. Called once per frame by whoever gets there first: the
// vgui pass normally, h_EndScene when vgui never painted (loading screens, minimised).
// Everything is built in ONE frame even though the two halves render at different times --
// splitting the BUILD is what broke input the last time.
void RenderWorldFx3();   // world (visuals/world_fx3.cpp): bullet impact boxes + world hit markers

// The crosshair draws through ImGui::GetBackgroundDrawList() like every overlay, so for its call
// the viewport's background slot points at s_chList instead: every crosshair style (and the
// aspect stretch wrapped round it) lands there untouched, and SplitDrawData sends that one list
// to the game's frame. The real background list is fetched FIRST so imgui has already reset it
// this frame -- GetBackgroundDrawList only resets a slot whose frame stamp is stale, and the
// stamp is per slot, not per list, so the swap never resets or loses either one.
static void RenderCrosshairList()
{
    ImGuiViewportP* vp = (ImGuiViewportP*)ImGui::GetMainViewport();
    ImDrawList* const bg = ImGui::GetBackgroundDrawList();
    if (!s_chList)
    {
        s_chList = IM_NEW(ImDrawList)(ImGui::GetDrawListSharedData());
        s_chList->_OwnerName = "##Crosshair";
    }
    s_chList->_ResetForNewFrame();
    s_chList->PushTextureID(ImGui::GetIO().Fonts->TexID);
    // the screen, not the viewport: that is logical (dpi) px, the crosshair draws in screen px
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    s_chList->PushClipRect(ImVec2(0.f, 0.f), ImMax(screen, vp->Size), false);

    vp->DrawLists[0] = s_chList;
    AspectStretch_Run(RenderCrosshair, AsCrosshair);
    vp->DrawLists[0] = bg;
}

// lua render.* (hacks/lua/lua_api.cpp records in the paint callback, lua_draw.h). Background list,
// over esp + world fx, under the menu. s_rtLuaSeq = the frame this build is for (multicore), 0 = newest.
static uint32_t s_rtLuaSeq = 0;

static void RenderLuaQueue()
{
    static LuaDrawFrame f;
    LuaDraw::CopyFrame(s_rtLuaSeq, f);
    if (f.cmds.empty() || GetTickCount64() - f.stampMs > 250)
        return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    const float sx = f.srcW > 0.f ? io.DisplaySize.x / f.srcW : 1.f, sy = f.srcH > 0.f ? io.DisplaySize.y / f.srcH : 1.f;
    const int firstVtx = dl->VtxBuffer.Size;
    ImVec2 buf[32];

    for (const LuaDrawCmd& c : f.cmds)
    {
        switch (c.type)
        {
        case LUAD_TEXT:
        {
            ImFont* font = LuaFontFor(c.font);
            if (!font || c.pn <= 0 || c.p0 < 0 || c.p0 + c.pn > (int)f.text.size()) break;
            const char* b = f.text.data() + c.p0;
            const char* e = b + c.pn;
            // botox n_render::impl_t::text: shadow wins over outline, outline = two diagonal copies
            const float size = c.size > 0.f ? c.size : font->FontSize;
            const ImVec2 at(c.x0, c.y0);
            if (c.flags & 1)
                dl->AddText(font, size, ImVec2(at.x + 1.f, at.y + 1.f), c.col2, b, e);
            else if (c.flags & 2)
            {
                dl->AddText(font, size, ImVec2(at.x + 1.f, at.y - 1.f), c.col2, b, e);
                dl->AddText(font, size, ImVec2(at.x - 1.f, at.y + 1.f), c.col2, b, e);
            }
            dl->AddText(font, size, at, c.col, b, e);
            break;
        }
        case LUAD_LINE:
            dl->AddLine(ImVec2(c.x0, c.y0), ImVec2(c.x1, c.y1), c.col, c.thick);
            break;
        case LUAD_RECT:
        {   // botox impl_t::rect
            if (c.flags & 1)
                dl->AddRectFilled(ImVec2(c.x0, c.y0), ImVec2(c.x1, c.y1), c.col, c.round);
            else
            {
                const float inset = (c.thick - 1.f) * 0.5f;
                dl->AddRect(ImVec2(c.x0 + inset, c.y0 + inset), ImVec2(c.x1 - inset, c.y1 - inset), c.col, std::max(c.round - inset, 0.f), 0, c.thick);
            }
            break;
        }
        case LUAD_GRAD:
        {
            if (c.round <= 0.f)
            {
                dl->AddRectFilledMultiColor(ImVec2(c.x0, c.y0), ImVec2(c.x1, c.y1), c.col, c.col, c.col2, c.col2);
                break;
            }
            const int first = dl->VtxBuffer.Size;
            dl->AddRectFilled(ImVec2(c.x0, c.y0), ImVec2(c.x1, c.y1), IM_COL32_WHITE, c.round);
            RecolourVertsY(dl, first, c.col, c.col2, c.y0, c.y1);
            break;
        }
        case LUAD_CIRCLE:
            dl->AddCircle(ImVec2(c.x0, c.y0), c.x1, c.col, c.pn, c.thick);
            break;
        case LUAD_CIRCLE_FILLED:
            dl->AddCircleFilled(ImVec2(c.x0, c.y0), c.x1, c.col, c.pn);
            break;
        case LUAD_POLY:
        {
            const int n = std::min(c.pn, 32);
            if (n < 3 || c.p0 < 0 || (c.p0 + n) * 2 > (int)f.pts.size()) break;
            for (int i = 0; i < n; ++i) buf[i] = ImVec2(f.pts[(c.p0 + i) * 2], f.pts[(c.p0 + i) * 2 + 1]);
            if (c.col) dl->AddConvexPolyFilled(buf, n, c.col);
            if (c.flags & 1) dl->AddPolyline(buf, n, c.col2, ImDrawFlags_Closed, c.thick);
            break;
        }
        }
    }

    // engine screen px -> imgui display px (windowed + dpi scaling), same as RenderEspQueue
    if (sx != 1.f || sy != 1.f)
        for (int i = firstVtx; i < dl->VtxBuffer.Size; ++i)
        {
            dl->VtxBuffer[i].pos.x *= sx;
            dl->VtxBuffer[i].pos.y *= sy;
        }
}

static void BuildImGuiFrame()
{
    if (s_frameBuilt) return;
    s_frameBuilt = true;
    CrashSpan _span("overlay/build");   // stutter probe: every overlay + menu + esp replay, per frame

    const bool psWinOpen = s_psPointWin >= 0 || Recorder_EditorOpen();   // recorder clip editor = same input handoff
    const bool menuUp    = g_showMenu || psWinOpen;

    // An indicator scale slider settled -> re-bake its face at the new px (botox
    // update_indicator_fonts; until then IndicatorFont scales the base face). HERE and nowhere
    // else: the atlas is between frames (last frame's draw data is already presented, and
    // NewFrame below re-points g.Font / the shared draw data at the new atlas), and both
    // callers of this function are on the render thread.
    // Same safe point for the fonts tab's indicator face (debounced: a size drag or a click
    // through the font list rebuilds once it settles, not every frame).
    if (s_indScaleSettle.Due(IndScaleSig(WantedIndScales()), IndScaleSig(s_indBuilt), GetTickCount64()) ||
        s_indSettle.Due(Fonts_IndSig(), s_indApplied, GetTickCount64()) ||
        s_espSettle.Due(Fonts_EspSig(), s_espImApplied, GetTickCount64()) ||
        HudFonts_Due())   // fonts tab > net graph
    {
        BuildFontAtlas();
        ImGui_ImplDX9_InvalidateDeviceObjects();   // NewFrame re-uploads the atlas texture
        s_popCount = 0;
        Menu_DropPopupGhosts();                    // ghosts point at the old atlas texture
    }
    // dpi font twins: same safe point, same reason (botox update_dpi_fonts)
    if (Dpi_UpdateFonts(g_d3dDev))
    {
        s_popCount = 0;
        Menu_DropPopupGhosts();                    // ghosts point at the old twin texture
    }

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    Dpi_BeginFrame();      // io.DisplaySize -> logical px for NewFrame (botox on_end_scene)
    ImGui::NewFrame();
    Dpi_AfterNewFrame();   // ...and back to the screen for every overlay below
    ImGui::GetIO().MouseDrawCursor = menuUp;

    RenderEspQueue();     // botox: esp under every other overlay (background list, first in)
    RenderWorldFx3();     // background list too, under the HUD overlays
    s_chOverBlur = RenderMenuBlur();   // menu bg blur: blurs game + esp + world fx above, every HUD panel below stays sharp
    RenderBootScreen();   // foreground draw list draws last regardless of call order: always on top
    RenderPsOverlay();
    RenderRouteOverlay();
    RenderEdgeBugOverlay();
    RenderAutoBounceOverlay();
    RenderWallClimbOverlay();
    // AspectStretch_Run = botox aspect ratio "stretch cheat overlays" / "stretch
    // crosshair" (screen-space panels only; draws straight through while those are off)
    AspectStretch_Run(RenderWatermark, AsPanel);
    AspectStretch_Run(RenderMediaPlayer, AsPanel);         // after the watermark: it stacks under the box that just drew
    AspectStretch_Run(RenderIndicators, AsPanel);   // botox order: velocity, graph, keybinds, particles, keystrokes
    Recorder_Render();           // path, route starts, timer, clipper box, clip editor
    RenderHudExtras();           // aimbot fov circle + custom net graph
    AspectStretch_Run(RenderRadar, AsPanel);   // csgo radar (hud/radar.cpp)
    RenderCrosshairList();       // own list: the one overlay drawn inside the game's frame
    AspectStretch_Run(RenderSpectatorList, AsPanel);       // misc HUD
    AspectStretch_Run(RenderHitMarker, AsPanel);
    RenderKillEffects();
    AspectStretch_Run(RenderPoints, AsPanel);
    RenderMiscOverlays();        // healthshot effect, low fps warning, displayed logs
    RenderLuaQueue();            // lua render.* (botox: paint_traverse lua last, under the menu)
    RenderDebugMenu();
    RenderPsPointWindow();
    RenderMenu();   // self-gated: draws nothing once g_showMenu is false AND its close-fade has finished

    ImGui::EndFrame();
    Dpi_BeforeRender();    // the software cursor is drawn by Render on the 1:1 foreground list: screen mouse
    ImGui::Render();
    Dpi_ScaleDrawData(ImGui::GetDrawData());       // windows grow by the dpi scale (panels did at their block end)
    BootScreen_FadeOthers(ImGui::GetDrawData());   // overlay fades in after the boot splash
    SplitDrawData();
}

// ==== vgui pass: the crosshair's home =============================================
// "FocusOverlayPanel" is the last panel painted each frame, so drawing after it returns puts
// the crosshair over the whole HUD and still inside the game's UI pass -- in the backbuffer
// long before Present, so every capture path records it whoever owns the Present entry.
// GetName is a virtual call and this fires for hundreds of panels a frame, so the vpanel is
// cached after the first match and it is an integer compare after that. The cache drops on
// device Reset (resolution change remakes the panel) and when the panel goes missing for a
// while; the EndScene fallback covers the frames in between.
PaintTraverse_t o_PaintTraverse = nullptr;
static void*     s_vguiPanels    = nullptr;
static VPANEL_t  s_focusPanel    = 0;
static int       s_focusMissed   = 0;
static DWORD     s_presentTid    = 0;

// The thread that reaches Present, i.e. the one the material system talks to d3d on. Zero
// until the first Present. screenfx asks: a hook that finds itself on this thread may issue
// raw d3d in step with the engine, one that does not has to stand down.
DWORD Render_PresentTid() { return s_presentTid; }

void* VGui_GetPanelInterface()
{
    if (!s_vguiPanels) s_vguiPanels = GetInterface("vgui2.dll", "VGUI_Panel009");
    return s_vguiPanels;
}

void VGui_ResetPanelCache() { s_focusPanel = 0; s_focusMissed = 0; }

// IPanel::GetName -- slot 36, same count as PaintTraverse=41 (see render.h).
static const char* VGui_PanelName(void* panels, VPANEL_t vpanel)
{
    using GetName_t = const char* (*)(void*, VPANEL_t);
    void** vt = *(void***)panels;
    return ((GetName_t)vt[36])(panels, vpanel);
}

// ==== multicore: the vgui pass, replayed on the render thread ================
// The vgui pass below stands down on the main thread, and the EndScene fallback used to build
// the frame at the first backbuffer scene with whatever matrix the main thread had just set up
// for the NEXT frame -- the world markers jittered (see s_rtMatrix). Now the main thread queues
// this functor at the same vgui point through the engine's call queue (screenfx.cpp, slots
// proven there) carrying THIS frame's matrix, and the render thread draws exactly where the
// single-threaded vgui pass would: same position, same frame, same matrix.
// Ring of 8: the main thread runs at most a frame or two ahead of the replay, so a slot is
// never rewritten before its call ran. Faked CFunctor, never freed (screenfx.cpp:496).
struct OvFunctor { void** vfptr; float m[4][4]; bool ok; uint32_t espSeq; uint32_t luaSeq; };
static int   __fastcall OvFn_AddRef (void*)             { return 1; }
static int   __fastcall OvFn_Release(void*)             { return 1; }
static void* __fastcall OvFn_Dtor   (void* p, unsigned) { return p; }
static void  __fastcall OvFn_Call   (void* self);
static void* s_ovFnVt[4] = { (void*)&OvFn_AddRef, (void*)&OvFn_Release, (void*)&OvFn_Dtor, (void*)&OvFn_Call };
static OvFunctor s_ovRing[8];
static unsigned  s_ovNext  = 0;       // main thread only
static bool      s_ovRan   = false;   // render thread: the functor ran this frame
static bool      s_ovDefer = false;   // render thread: it ran LAST frame -> EndScene leaves the build to it

static void __fastcall OvFn_Call(void* self)
{
    ++g_hookDepth;   // counter FIRST, like h_Present: eject drains on it before tearing ImGui down
    struct Dec { ~Dec() { --g_hookDepth; } } _dec;

    s_ovRan = true;
    // s_frameBuilt: the first frame after the switch, EndScene already built and drew -- keep that
    if (g_eject || !g_imguiInit || s_vguiDrew || s_frameBuilt) return;
    IDirect3DDevice9* dev = g_d3dDev;
    if (!dev) return;

    Bloom_Frame(dev);       // the glow goes under the grade -- lens first, film after
    ColorCorr_Frame(dev);   // grade BEFORE the overlay, as the vgui pass does (latched once a frame)
    ScreenFxPasses_Frame(dev);   // sharpen/grain/vignette/invert/posterize/deband/channel-shift/emphasize/show-depth, same latch

    const OvFunctor* f = static_cast<const OvFunctor*>(self);
    if (f->ok) { memcpy(s_rtMatrix, f->m, sizeof(s_rtMatrix)); s_rtMatrixOk = true; }
    s_rtEspSeq = f->espSeq;
    s_rtLuaSeq = f->luaSeq;
    BuildImGuiFrame();
    s_rtMatrixOk = false;
    s_rtEspSeq = 0;
    s_rtLuaSeq = 0;
    if (s_gameCount > 0) RenderSubset(s_gameLists, s_gameCount);   // scene is open: vgui's own
    s_vguiDrew = true;
}

// main thread, at the latched vgui panel. false = no queue, the EndScene fallback stands
static bool OvQueue()
{
    OvFunctor& f = s_ovRing[s_ovNext++ & 7];
    f.vfptr = s_ovFnVt;
    const float(*m)[4] = PsEngineW2S();
    f.ok = m != nullptr;
    f.espSeq = g_espQSeq.load(std::memory_order_acquire);   // this frame's esp, committed earlier in this paint
    f.luaSeq = g_luaDrawSeq.load(std::memory_order_acquire);   // lua paint, committed just before (h_PaintTraverse)
    if (m) memcpy(f.m, m, sizeof(f.m));
    return ScreenFx_QueueFunctor(&f);
}

bool WorldFx3_SkipPanel(void* panels, uintptr_t vpanel);   // remove hud elements (world_fx3.cpp)

void h_PaintTraverse(void* panels, VPANEL_t vpanel, bool forceRepaint, bool allowForce)
{
    if (WorldFx3_SkipPanel(panels, vpanel)) return;   // a removed HUD panel: it and its children unpainted
    ScopeCsgo_PrePaintTraverse(panels, vpanel);   // csgo scope: just before the HUD root, under the HUD
    const bool consoleTint = ConsoleTint_Begin(panels, vpanel);   // botox console color
    o_PaintTraverse(panels, vpanel, forceRepaint, allowForce);
    if (consoleTint) ConsoleTint_End();

    // s_vguiDrew is NOT tested here any more: it is the render thread's, and under multicore
    // the main thread would read the previous frame's true and skip queueing this one
    if (g_eject || !g_imguiInit) return;

    // Identify the panel we draw after. Only the uncached path pays for GetName, and this
    // runs BEFORE the thread check on purpose: the integer compare rejects all but one of
    // the hundreds of panels a frame, so a GetCurrentThreadId ahead of it would be paid for
    // a decision this compare is about to discard.
    if (s_focusPanel)
    {
        if (vpanel != s_focusPanel) return;
    }
    else
    {
        const char* nm = VGui_PanelName(panels, vpanel);
        if (!nm || strcmp(nm, "FocusOverlayPanel") != 0) return;
        s_focusPanel = vpanel;
    }

    // THREADING. With multicore rendering on, vgui paints on the MAIN thread and the material
    // system replays the queued draws on the render thread -- the one that reaches Present.
    // Raw D3D from here would race Present. Draw only when this IS the Present thread;
    // otherwise stand down and let the EndScene fallback (always on the render thread) do it.
    const DWORD tid = GetCurrentThreadId();
    RC_RefreshLive();   // game thread, the frame about to be drawn: entity markers onto their entity
    {
        ++g_hookDepth;   // eject drains on it: never free the module under a running script
        struct Dec { ~Dec() { --g_hookDepth; } } _dec;
        g_lua.on_paint();   // game thread: lua paint callbacks record render.*, committed for this frame
    }
    if (s_presentTid && tid != s_presentTid)
    {
        OvQueue();          // the render thread draws it at this point (OvFn_Call)
        return;
    }
    if (s_vguiDrew) return;

    s_focusMissed = 0;

    ++g_hookDepth;
    struct Dec { ~Dec() { --g_hookDepth; } } _dec;

    IDirect3DDevice9* dev = g_d3dDev;
    if (!dev) return;

    // Colour correction goes in FIRST, before a single overlay pixel. The world and the
    // game's HUD are already in the backbuffer at this point and the crosshair is not, so
    // the crosshair keeps exactly the colour the user picked for it. This is also the
    // recorded half of the frame: h_Present is too late, a capture hook owns that entry
    // and has copied the backbuffer before we get there.
    Bloom_Frame(dev);
    ColorCorr_Frame(dev);
    ScreenFxPasses_Frame(dev);

    BuildImGuiFrame();
    if (s_gameCount <= 0) { s_vguiDrew = true; return; }

    // vgui paints between the game's own BeginScene/EndScene, so a scene is already open.
    // ImGui_ImplDX9_RenderDrawData saves and restores a full state block, so the material
    // system's render state survives this untouched.
    RenderSubset(s_gameLists, s_gameCount);
    s_vguiDrew = true;
}

HRESULT __stdcall h_EndScene(IDirect3DDevice9* dev)
{
    ++g_hookDepth;
    struct Dec { ~Dec() { --g_hookDepth; } } _dec;

    if (g_eject || !g_imguiInit) return o_EndScene(dev);
    g_d3dDev = dev;

    // Our own menu scene in h_Present closes through this hook. Never draw the game lists
    // into it -- that is what put the crosshair over the menu.
    if (s_menuScene) return o_EndScene(dev);

    // The depth resolve, and later the passes that read it. BEFORE the grade for the same
    // reason a camera focuses before the film is developed. This is the render thread, so
    // it is also the only place raw d3d is in step with the material system's playback.
    ScreenFx_Frame(dev);

    // Colour correction fallback. With multicore rendering the vgui paint runs on the main
    // thread and stands down, so the grade has to land here instead -- still inside the
    // game's own frame, still before Present, still recorded. It latches on the first scene
    // whose target is the backbuffer; after that (and whenever the feature is off) this is
    // a single bool test, so the fast path below is untouched.
    Bloom_Frame(dev);
    ColorCorr_Frame(dev);
    ScreenFxPasses_Frame(dev);

    // vgui already drew the crosshair in the right place this frame -- drawing it again here
    // would double every alpha.
    if (s_vguiDrew) return o_EndScene(dev);

    // multicore with a working call queue: OvFn_Call builds and draws at the vgui point, with
    // this frame's matrix. Building here would be earlier, with the wrong one. A frame the
    // functor skips (loading) shows no overlay once, then this falls back (h_Present)
    if (s_ovDefer) return o_EndScene(dev);

    // TF2 runs reflection/refraction/rendertarget passes through this same device. Drawing
    // the crosshair into those would put it in water reflections and RT monitors, so only
    // the scene whose render target IS the backbuffer gets it.
    bool onBackbuffer = false;
    IDirect3DSurface9* rt = nullptr;
    IDirect3DSurface9* bb = nullptr;
    if (SUCCEEDED(dev->GetRenderTarget(0, &rt)) && rt &&
        SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
        onBackbuffer = (rt == bb);
    if (rt) rt->Release();
    if (bb) bb->Release();
    if (!onBackbuffer) return o_EndScene(dev);

    BuildImGuiFrame();

    // Already inside the game's BeginScene/EndScene pair -- no scene of our own here.
    // GetDrawData stays valid until the next NewFrame, so the later scenes just re-submit.
    // Scenes per frame are NOT a fixed count (measured 3, 4, then bursts of 735 / 4105 /
    // 9010), so "draw on the Nth scene" cannot work -- draw in every one, which always
    // includes the last. Only reached when vgui did not paint this frame.
    RenderSubset(s_gameLists, s_gameCount);

    return o_EndScene(dev);
}

// The device ImGui and every dih-owned d3d object were built on.
static IDirect3DDevice9* s_imguiDev = nullptr;

// The game replaced its device (startup mode set, mat_dxlevel or video settings change).
// ImGui's font/buffers, colour correction, screenfx and the crosshair shaders all live on the
// old one, so without this every visual draws with foreign objects and shows nothing. The old
// device is still alive -- ImGui_ImplDX9_Init AddRef'd it -- so releasing on it is legal, and
// ImGui_ImplDX9_Shutdown drops that last ref.
static void Render_RebindDevice(IDirect3DDevice9* dev)
{
    DebugLog("render: device changed %p -> %p, rebuilding device objects", (void*)s_imguiDev, (void*)dev);
    CsgoCrosshair_ReleaseTextures();
    ColorCorr_Destroy();
    Bloom_Destroy();
    ScreenFx_Destroy();
    ScreenFxPasses_Destroy();
    MediaPlayer_ReleaseTexture();   // DEFAULT pool, and it belongs to the device being dropped
    Indicators_ReleaseTextures();      // mitosis morph texture, same story
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplDX9_Init(dev);
    s_imguiDev = dev;
    Dpi_DropFonts();   // twin texture is the old device's; re-baked for this one

    s_gameCount = s_ovCount = s_menuCount = s_fadeCount = s_popCount = 0;   // lists reference the dropped font texture
    Menu_DropPopupGhosts();
    s_frameBuilt = false;
    s_vguiDrew   = false;
    s_menuScene  = false;
    VGui_ResetPanelCache();

    D3DDEVICE_CREATION_PARAMETERS cp{};
    if (SUCCEEDED(dev->GetCreationParameters(&cp)) && cp.hFocusWindow && cp.hFocusWindow != g_hwnd)
    {
        DebugLog("render: focus window changed %p -> %p", (void*)g_hwnd, (void*)cp.hFocusWindow);
        if (g_origWndProc && IsWindow(g_hwnd))
            SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_origWndProc);
        ImGui_ImplWin32_Shutdown();
        g_hwnd = cp.hFocusWindow;
        ImGui_ImplWin32_Init(g_hwnd);
        g_origWndProc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)WndProcHook);
    }
}

// What the frame is rendered with, logged in game on the first tick and whenever it changes.
// dxlevel 80 vs 90+ changes sRGB writes, HDR, engine post and MSAA -- a "works on 80, not on
// 95" report starts here.
static void Render_LogSettings(IDirect3DDevice9* dev)
{
    static CachedCVar cc_dx, cc_hdr, cc_queue, cc_aa;
    ConVar* dx = cc_dx.Get("mat_dxlevel");
    ConVar* hdr = cc_hdr.Get("mat_hdr_level");
    ConVar* q = cc_queue.Get("mat_queue_mode");
    ConVar* aa = cc_aa.Get("mat_antialias");
    const int v[4] = { dx ? (int)dx->GetFloat() : -1, hdr ? (int)hdr->GetFloat() : -1,
                       q ? (int)q->GetFloat() : -99, aa ? (int)aa->GetFloat() : -1 };

    D3DSURFACE_DESC bd{};
    IDirect3DSurface9* bb = nullptr;
    if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) { bb->GetDesc(&bd); bb->Release(); }

    static int s_last[4] = { -2, -2, -2, -2 };
    static IDirect3DDevice9* s_lastDev = nullptr;
    if (!memcmp(v, s_last, sizeof(v)) && s_lastDev == dev) return;
    memcpy(s_last, v, sizeof(v));
    s_lastDev = dev;
    DebugLog("render: dev=%p mat_dxlevel=%d mat_hdr_level=%d mat_queue_mode=%d mat_antialias=%d "
             "bb=%ux%u fmt=%d ms=%d", (void*)dev, v[0], v[1], v[2], v[3],
             bd.Width, bd.Height, (int)bd.Format, (int)bd.MultiSampleType);
}

// Eject, between g_eject and the end of the splash: every other overlay, pass and hook is
// already standing down, so the splash gets a frame of its own -- one NewFrame, one scene on
// top of the finished game frame -- while it fades the game back in. MainThread waits for it
// (BootScreen_EjectEnd) before the hooks come off, and this runs under g_hookDepth, so the
// ImGui teardown can never overlap it.
static void RenderEjectFrame(IDirect3DDevice9* dev)
{
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    Dpi_BeginFrame();
    ImGui::NewFrame();
    Dpi_AfterNewFrame();
    ImGui::GetIO().MouseDrawCursor = false;
    RenderBootScreen();
    ImGui::EndFrame();
    Dpi_BeforeRender();
    ImGui::Render();
    Dpi_ScaleDrawData(ImGui::GetDrawData());
    if (SUCCEEDED(dev->BeginScene()))
    {
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        dev->EndScene();   // h_EndScene passes straight through at g_eject
    }
    BlurRect_NewFrame();
}

HRESULT __stdcall h_Present(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND win, const RGNDATA* dirty)
{
    // Counter FIRST, before the eject test. Reading g_eject and then bumping leaves a window
    // where a Present already in flight is invisible to the drain loop, which is what the
    // drain exists to close.
    ++g_hookDepth;
    struct Dec { ~Dec() { --g_hookDepth; } } _dec;

    if (g_eject)
    {
        if (g_imguiInit && dev == s_imguiDev && BootScreen_EjectDrawing()) RenderEjectFrame(dev);
        return o_Present(dev, src, dst, win, dirty);
    }

    g_d3dDev = dev;   // blend-state switches in the crosshair draw need it
    s_presentTid = GetCurrentThreadId();   // the vgui pass checks against this

    if (!g_imguiInit)
    {
        D3DDEVICE_CREATION_PARAMETERS cp{};
        dev->GetCreationParameters(&cp);
        g_hwnd = cp.hFocusWindow;
        if (!g_hwnd) goto chain;

        ImGui::CreateContext();
        {
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

            BuildFontAtlas();   // menu 12 plain + bold, indicator face at the current scale
        }

        SetupImGuiStyle();
        ImGui_ImplWin32_Init(g_hwnd);
        ImGui_ImplDX9_Init(dev);
        s_imguiDev = dev;
        g_origWndProc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)WndProcHook);
        g_imguiInit = true;
    }
    else if (dev != s_imguiDev)
        Render_RebindDevice(dev);

chain:
    if (g_imguiInit)
    {
        // Periodic engine-touching updates on render thread (safe)
        static DWORD s_lastUpdate = 0;
        static bool  s_wasInGame  = false;
        DWORD now = GetTickCount();
        const bool bSlowTick = (now - s_lastUpdate > 100);
        bool inGame = false;
        if (bSlowTick) {
            s_lastUpdate = now;
            UpdateNetStats();
            UpdateVisualsCvars();
            VmFlatAlpha_Tick();   // material re-snapshot: render thread, outside any draw
            ResolveNetvars();
            NetSettings_Tick();
            Region_Tick();
            NetGraph_Tick();
            EquipRegion_Tick();
            Perf_Tick();
            ScreenFx_Tick();   // mat_queue_mode 0 while a depth pass is on

            inGame = IsInGame();
            if (inGame) Render_LogSettings(dev);
            if (inGame && !s_wasInGame) {
                NoHats_FlushModelCache();   // new map -> model_t addresses recycled
                // Same reason, and it matters more here: frame blending keeps a pose per model
                // pointer, so a recycled address would pair last map's bones with this map's
                // skeleton. The camera history goes with it -- a map change is a teleport.
                FrameBlend_Reset();
                // The slot-2047 precipitation reset lives in Rain_Tick now (game thread, its
                // own !IsInGame edge) -- from here it raced the entity it was forgetting.
            }
            s_wasInGame = inGame;
        }

        UpdateFPS();
        // Rain_Tick: NOT here. Present is the render thread under mat_queue_mode 2, and it
        // creates/frees a client entity -- skins.cpp h_FrameStageNotify(5) runs it instead.
        // Cover art upload. Here and not in the frame build: the build can run from the vgui
        // pass on the MAIN thread, and CreateTexture/LockRect there would race Present.
        MediaPlayer_Tick(dev);
        Avatar_Tick(dev);   // uploads any avatar pixels Avatar_Pump fetched on the game thread
        SheenNormal_Tick(); // normal blend sheen: lazy DIP hook the first frame it is on

        // No unusual: suppression sweep on the 100ms tick, restore on the off-transition.
        // (No hats bodygroups are computed on the game thread, skins.cpp FrameStageNotify.)
        static bool s_noUnusualPrev = false;
        if (bSlowTick && inGame && g_noUnusual)
            NoHats_Sweep(true);

        if (!g_noUnusual && s_noUnusualPrev) RestoreHatParticles();
        s_noUnusualPrev = g_noUnusual;

        const bool psWinOpen = s_psPointWin >= 0 || Recorder_EditorOpen();   // recorder clip editor = same input handoff
        const bool menuUp    = g_showMenu || psWinOpen;
        if (menuUp) ClipCursor(nullptr);

        // The menu half of the frame built in h_EndScene. Rendered HERE, which is both
        // what puts it on top of the crosshair (every EndScene pass has already run) and
        // what keeps it out of the recording -- a capture hook that owns the Present entry
        // has copied the backbuffer before this line runs.
        // The overlays go in first, same scene: over the whole game frame (vgui, console,
        // pause menu included), under the menu. Panel blur callbacks sample the backbuffer as
        // they replay, so here they blur the finished frame -- what sits under them on screen.
        if ((s_ovCount > 0 || s_menuCount > 0 || s_fadeCount > 0 || s_popCount > 0) && SUCCEEDED(dev->BeginScene()))
        {
            s_menuScene = true;
            // open slides up into place, close slides down: ease out cubic on the fade clock, whole px keeps text sharp
            const float rest  = 1.f - Menu_FadeAlpha();
            const float slide = floorf(12.f * rest * rest * rest + 0.5f);
            RenderSubset(s_ovLists, s_ovCount);
            RenderSubset(s_fadeLists, s_fadeCount, Menu_FadeAlpha(), slide);
            for (int i = 0; i < s_popCount; ++i)   // botox: popups on top of the menu, each at its own alpha
                RenderSubset(&s_popLists[i].list, 1, s_popLists[i].alpha, slide);
            RenderSubset(s_menuLists, s_menuCount);
            dev->EndScene();          // re-enters h_EndScene -- s_menuScene stands it down
            s_menuScene = false;
        }

    }

    // aspect ratio is r_aspectratio now (world_fx.cpp WorldFx_Tick)
    HRESULT hr = o_Present(dev, src, dst, win, dirty);

    // End of frame. If vgui went a long stretch without painting the latched panel it was
    // destroyed (level change remakes the tree), so re-resolve it by name next frame.
    if (s_focusPanel && !s_vguiDrew && ++s_focusMissed > 120) VGui_ResetPanelCache();
    s_frameBuilt = false;
    s_vguiDrew   = false;
    s_ovDefer    = s_ovRan;   // the replayed vgui pass owns the next frame only while it keeps running
    s_ovRan      = false;
    ColorCorr_NewFrame();   // clears the once-a-frame latch the two call sites share
    Bloom_NewFrame();       // same latch, same three call sites
    ScreenFx_NewFrame();    // same latch for the depth resolve: one copy per frame
    ScreenFxPasses_NewFrame();   // same latch, same three call sites
    BlurRect_NewFrame();         // clears this frame's blur job slots

    return hr;
}

HRESULT __stdcall h_Reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
    ++g_hookDepth;
    struct Dec { ~Dec() { --g_hookDepth; } } _dec;
    CsgoCrosshair_ReleaseTextures();   // rebuilt next frame; a DEFAULT-pool atlas is lost here
    ColorCorr_Destroy();               // shader + two DEFAULT-pool textures, remade on the next graded frame
    Bloom_Destroy();                   // same story: two shaders and three DEFAULT-pool targets
    ScreenFx_Destroy();                // INTZ + probe targets, all DEFAULT-pool: a live one fails the Reset
    ScreenFxPasses_Destroy();          // one shader + one DEFAULT-pool scene copy
    MediaPlayer_ReleaseTexture();      // cover art is DEFAULT-pool too; the decoded pixels rebuild it
    Indicators_ReleaseTextures();         // mitosis morph texture; rebuilt on the next split
    Avatar_ReleaseAll();               // avatar textures are DEFAULT-pool too
    PlayerStencil_ReleaseTargets();    // esp: player stencil mask/blur targets, DEFAULT-pool
    GifTex_ReleaseAll();               // gif frame texture, GDI+ bitmap survives the reset
    SpectatorList_ReleaseTextures();   // spectator list: eyes style row textures, DEFAULT-pool
    Radar_ReleaseTextures();           // csgo radar overview, DEFAULT-pool; the baked pixels rebuild it
    BlurRect_Destroy();               // blur scratch targets + shader
    if (g_imguiInit) ImGui_ImplDX9_InvalidateDeviceObjects();
    // The split arrays point at draw lists from the frame being dropped -- a Present that
    // renders them after a Reset would submit freed geometry.
    s_gameCount = s_ovCount = s_menuCount = s_fadeCount = s_popCount = 0;
    Menu_DropPopupGhosts();
    s_frameBuilt = false;
    s_vguiDrew   = false;
    s_menuScene  = false;
    VGui_ResetPanelCache();   // resolution change rebuilds the panel tree
    HRESULT hr = o_Reset(dev, pp);
    if (g_imguiInit && SUCCEEDED(hr))
        ImGui_ImplDX9_CreateDeviceObjects();
    return hr;
}

BOOL WINAPI h_SetCursorPos(int x, int y) {
    MiscHookGuard _g;
    if (g_showMenu || s_psPointWin >= 0 || Recorder_EditorOpen()) return TRUE; // eat the recenter = view freezes
    return o_SetCursorPos(x, y);
}

// ==== WndProc hook ================================================================
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// Input the menu (or the ps point window) eats: imgui gets it, the game does not.
static bool IsMenuInputMsg(UINT msg)
{
    switch (msg)
    {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN: case WM_XBUTTONUP:
    case WM_MOUSEWHEEL:  case WM_MOUSEHWHEEL:
    case WM_KEYDOWN:     case WM_KEYUP:
    case WM_SYSKEYDOWN:  case WM_SYSKEYUP:
    case WM_CHAR:        case WM_UNICHAR:
    case WM_INPUT:
        return true;
    default:
        return false;
    }
}

LRESULT CALLBACK WndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Eject: ImGui is torn down under this, and a later subclasser may keep us chained.
    if (g_eject) return CallWindowProcW(g_origWndProc, hWnd, msg, wParam, lParam);
    if (msg == WM_KEYDOWN) {
        if ((int)wParam == g_menuKey) {
            g_showMenu = !g_showMenu;
            if (g_showMenu) { ShowCursor(TRUE);  ClipCursor(nullptr); }
            else            { ShowCursor(FALSE); }
            return 0;
        }
        if (wParam == VK_F11) { g_ejectRequest = true; return 0; }
    }

    // ps point ENTER window: fresh ENTER (no autorepeat, bit 30) on a hovered point
    // opens; while open ENTER/ESC close, BACKSPACE deletes. 250ms debounce so the
    // opening press can't instantly close. Eaten = never reaches the game's chat bind.
    if (!g_showMenu && msg == WM_KEYDOWN && !(lParam & (1u << 30)))
    {
        if (s_psPointWin < 0 && wParam == VK_RETURN && s_psPointHover >= 0)
        {
            s_psPointWin = s_psPointHover;
            s_psPointWinGen = PS_PointsGen();
            s_psPointWinOpenMs = GetTickCount64();
            return 0;
        }
        // botox movement > points "delete points": key on the hovered point deletes it, window
        // closed. botox keys_blocked: console up = the key is being typed (IVEngineClient slot 11)
        if (s_psPointWin < 0 && g_pointsDelete && g_pointsDeleteKey > 0 && (int)wParam == g_pointsDeleteKey
            && s_psPointHover >= 0 && !(g_engine && VCall<bool>(g_engine, 11)))
        {
            PS_PointDelete(s_psPointHover);
            s_psPointHover = -1;
            return 0;
        }
        if (s_psPointWin >= 0 && GetTickCount64() - s_psPointWinOpenMs > 250)
        {
            if (wParam == VK_RETURN || wParam == VK_ESCAPE) { s_psPointWin = -1; return 0; }
            if (wParam == VK_BACK) { PS_PointDelete(s_psPointWin); s_psPointWin = -1; return 0; }
        }
        // recorder: ENTER on a route start opens its clip editor, ESC/ENTER close it (botox)
        if (s_psPointWin < 0 && Recorder_EditorKey((unsigned)wParam)) return 0;
    }

    // Menu up, or the point window open: imgui gets the input and the game is starved.
    // One context, one window, so routing is a straight handoff to the win32 backend.
    if (g_showMenu || s_psPointWin >= 0 || Recorder_EditorOpen())
    {
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        if (IsMenuInputMsg(msg))
            return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return CallWindowProcW(g_origWndProc, hWnd, msg, wParam, lParam);
}

// ==== ImGui style -- catalyst base ========================================================
// Every number and colour below is catalyst's zui::style (USELESSSRCCODE\catalyst,
// project/external/zdraw/zui/zui.hpp), mapped onto the ImGui colours the fork's widgets
// actually read. The menu's own panels and group boxes are drawn by menu.cpp against the
// same values -- see src/hacks/menu/menu_flow.h for the metrics half.
//
// THE ACCENT IS ImGuiCol_CheckMark. catalyst drives its accent through
// checkbox_check / slider_fill / the header's selected tab, which is exactly the set of
// things ImGui already colours with CheckMark + SliderGrab, so no new style enum was
// added to the fork: menu.cpp reads the accent back out of CheckMark.
void SetupImGuiStyle()
{
    ImGui::StyleColorsDark();

    ImGuiStyle& s = ImGui::GetStyle();
    // botox menu stage 2: rounding + border vars match botox_menu_spec.md section 5.
    // Border vars go to 0 everywhere -- botox hand-draws every visible border as an
    // AddRect over a transparent ImGuiCol_Border, so the frame widgets (Checkbox /
    // SliderScalar / BeginCombo, ported stage 1) that already AddRect their own
    // 50,50,50,100 outline don't double up with a native one underneath.
    // botox_menu_spec.md section 5, verbatim.
    s.WindowRounding    = 5.0f;
    s.ChildRounding     = 0.0f;
    s.FrameRounding     = 2.0f;
    s.GrabRounding      = 3.0f;
    s.PopupRounding     = 3.0f;
    s.ScrollbarRounding = 0.0f;
    s.TabRounding       = 0.0f;
    s.FrameBorderSize   = 0.0f;   // hand-drawn by Checkbox/SliderScalar/BeginCombo now
    s.WindowBorderSize  = 0.0f;   // menu.cpp draws the window outline itself
    s.PopupBorderSize   = 0.0f;
    s.ChildBorderSize   = 0.0f;   // group boxes are a flat fill, no native border (spec 3a)
    s.ScrollbarSize     = 8.0f;
    s.GrabMinSize       = 0.0f;
    s.WindowPadding     = ImVec2(8, 8);
    s.FramePadding      = ImVec2(0, 0);
    s.ButtonTextAlign   = ImVec2(0.5f, 0.5f);
    s.ItemSpacing       = ImVec2(8, 8);
    s.ItemInnerSpacing  = ImVec2(4, 4);   // botox never sets it: imgui default. Checkbox label gap = this + 2

    // ---- palette (zui::style) ----------------------------------------------------
    // Greys are written as their 0-255 value / 255. The accent is zui's 160,170,220.
    auto G = [](float v255) { return v255 / 255.f; };
    const ImVec4 kAccent = ImVec4(G(160), G(170), G(220), 1.00f);

    ImVec4* c = s.Colors;
    // botox_menu_spec.md section 5, verbatim: WindowBg #0A0A0A, ChildBg #0F0F0F,
    // FrameBg #191919, PopupBg #141414, Button #141414.
    c[ImGuiCol_WindowBg]         = ImVec4(G(10), G(10), G(10), 1.00f);
    c[ImGuiCol_PopupBg]          = ImVec4(G(20), G(20), G(20), 1.00f);
    c[ImGuiCol_ChildBg]          = ImVec4(G(15), G(15), G(15), 1.00f);
    // botox keeps StyleColorsDark text: pure white, disabled 50%. Its widgets dim labels by
    // alpha (hover/tick anim 0.5 -> 1), not by a grey step.
    c[ImGuiCol_Text]             = ImVec4(1.f, 1.f, 1.f, 1.00f);
    c[ImGuiCol_TextHovered]      = ImVec4(1.f, 1.f, 1.f, 1.00f);
    c[ImGuiCol_TextActive]       = ImVec4(1.f, 1.f, 1.f, 1.00f);
    c[ImGuiCol_TextDisabled]     = ImVec4(0.5f, 0.5f, 0.5f, 1.00f);
    c[ImGuiCol_FrameBg]          = ImVec4(G(25), G(25), G(25), 1.00f);   // checkbox/slider/combo bg, #191919
    c[ImGuiCol_FrameBgHovered]   = ImVec4(G(30), G(30), G(30), 1.00f);
    c[ImGuiCol_FrameBgActive]    = ImVec4(G(34), G(34), G(34), 1.00f);   // combo_hovered
    c[ImGuiCol_CheckMark]        = kAccent;                              // checkbox_check == THE ACCENT
    c[ImGuiCol_SliderGrab]       = kAccent;                              // slider_fill
    // botox menu stage 2: the new ImGuiCol_Accent slot (imgui.h, stage 1) was never
    // written, so every ported Checkbox/SliderScalar/BeginCombo body that reads it
    // (imgui_widgets.cpp) was drawing at alpha 0 since stage 1 landed. Same value as
    // CheckMark -- one accent, both names, so old ImGuiCol_CheckMark call sites
    // (menu.cpp RowBindBox / color swatch) stay in sync with the new ones for free.
    c[ImGuiCol_Accent]           = kAccent;
    c[ImGuiCol_SliderGrabActive] = ImVec4(G(180), G(190), G(235), 1.00f);// slider_grab_active
    c[ImGuiCol_Button]           = ImVec4(G(20), G(20), G(20), 1.00f);   // #141414
    c[ImGuiCol_ButtonHovered]    = ImVec4(G(38), G(38), G(38), 1.00f);
    c[ImGuiCol_ButtonActive]     = ImVec4(G(22), G(22), G(22), 1.00f);
    // Selectable rows (combo popups, the marker list): selected = the accent at low
    // alpha, hover = a grey step, both zui combo_item_*.
    c[ImGuiCol_Header]           = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.14f);
    c[ImGuiCol_HeaderHovered]    = ImVec4(G(36), G(36), G(36), 1.00f);
    c[ImGuiCol_HeaderActive]     = ImVec4(G(44), G(44), G(44), 1.00f);
    c[ImGuiCol_Border]           = ImVec4(G(50), G(50), G(50), 1.00f);   // frame borders
    c[ImGuiCol_BorderShadow]     = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    // Separator doubles as the panel outline colour for everything drawn by hand
    // (UiPanel below, the menu window's own border): zui window_border is 45.
    c[ImGuiCol_Separator]        = ImVec4(G(45), G(45), G(45), 1.00f);
    c[ImGuiCol_SeparatorHovered] = ImVec4(G(70), G(70), G(70), 1.00f);
    c[ImGuiCol_SeparatorActive]  = ImVec4(G(92), G(92), G(92), 1.00f);
    c[ImGuiCol_ResizeGrip]       = ImVec4(G(50), G(50), G(50), 0.60f);
    c[ImGuiCol_ResizeGripHovered]= ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.60f);
    c[ImGuiCol_ResizeGripActive] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.90f);
    c[ImGuiCol_DragDropTarget]   = kAccent;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(G(235), G(235), G(235), 0.60f);
    c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.00f, 0.00f, 0.00f, 0.30f);
    c[ImGuiCol_ScrollbarBg]      = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(G(52), G(52), G(52), 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(G(70), G(70), G(70), 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(kAccent.x, kAccent.y, kAccent.z, 1.00f);
    c[ImGuiCol_TextSelectedBg]   = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.25f);
    c[ImGuiCol_NavHighlight]     = ImVec4(0.00f, 0.00f, 0.00f, 0.00f); // kills child nav ring
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.45f);
}

// The menu window's ground, for panels drawn outside the menu. Same diagonal ramp
// catalyst's begin_window lays down -- lighten 1.15 at the top left corner, the base 18
// along the other diagonal, darken 0.85 at the bottom right -- and the same 45 outline.
// Alpha 235 and not 255: these sit on the game, and the menu's own tooltip box already
// uses that value.
//
// FOUR FILLED STRIPS, NOT AddRect -- at thickness 1 AddRect takes ImGui's anti-aliased
// thin-line path, alpha ramps over +/-1px and the 45 lands on screen as ~38. Same reason
// menu.cpp draws its window border this way.
void UiPanel(ImDrawList* dl, const ImVec2& a, const ImVec2& b)
{
    const ImU32 cTL = IM_COL32(21, 21, 21, 235);
    const ImU32 cMid = IM_COL32(18, 18, 18, 235);
    const ImU32 cBR = IM_COL32(15, 15, 15, 235);
    dl->AddRectFilledMultiColor(a, b, cTL, cMid, cBR, cMid);

    const ImU32 cB = ImGui::GetColorU32(ImGuiCol_Separator);
    dl->AddRectFilled(ImVec2(a.x, a.y),        ImVec2(b.x,        a.y + 1.f), cB);  // top
    dl->AddRectFilled(ImVec2(a.x, b.y - 1.f),  ImVec2(b.x,        b.y),       cB);  // bottom
    dl->AddRectFilled(ImVec2(a.x, a.y),        ImVec2(a.x + 1.f,  b.y),       cB);  // left
    dl->AddRectFilled(ImVec2(b.x - 1.f, a.y),  ImVec2(b.x,        b.y),       cB);  // right
}

// ==== Dummy device vtable grab ================================================================
bool GetDX9VTable(void** vt, int n) {
    HMODULE d3d = GetModuleHandleW(L"d3d9.dll");
    if (!d3d) return false;
    auto Create = (IDirect3D9*(__stdcall*)(UINT))GetProcAddress(d3d, "Direct3DCreate9");
    if (!Create) return false;

    IDirect3D9* p = Create(D3D_SDK_VERSION);
    if (!p) return false;

    HWND w = CreateWindowExW(0, L"STATIC", nullptr, WS_POPUP, 0,0,2,2, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = w; pp.BackBufferFormat = D3DFMT_UNKNOWN;

    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = p->CreateDevice(0, D3DDEVTYPE_NULLREF, w, D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_DISABLE_DRIVER_MANAGEMENT, &pp, &dev);

    if (FAILED(hr) || !dev) {
        pp.BackBufferWidth = pp.BackBufferHeight = 2;
        hr = p->CreateDevice(0, D3DDEVTYPE_HAL, w, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    }

    bool ok = false;
    if (SUCCEEDED(hr) && dev) {
        void** dvt = *(void***)dev;
        for (int i = 0; i < n; ++i) vt[i] = dvt[i];
        dev->Release(); ok = true;
    }
    p->Release();
    DestroyWindow(w);
    return ok;
}
