#include "menu.h"
#include "../aim/aim.h"
#include "config.h"
#include "../network/mmfix.h"
#include "../hud/movind.h"
#include "../network/netregion.h"
#include "../hud/scoreboard.h"
#include "../screen/colorcorrect.h"
#include "../hud/crosshair.h"   // Crosshair_BlendCombo
#include "../visuals/lagres.h"
#include "../movement/movement.h"   // RC_* marker access for the route calculator's marker list
#include "../visuals/skins.h"      // skinchanger tab: weapon/warpaint/reskin tables + particle list
#include "../../dependencies/data/skindata.h"
#include "../../dependencies/imgui/imgui_internal.h"
#include "menu_chrome.h"  // the panels, the group box flow and every metric they use

#include <cmath>   // isfinite -- SliderF refuses a NaN/inf value as a range bound
#include <cctype>  // tolower -- the searchable warpaint/particle combos
#include <algorithm>
#include <ctime>     // BuildDateText: localtime_s off the PE timestamp
#include <cstdint>
#include <string>
#include "unitA_menu.h"
#include "unitB_menu.h"
#include "unitC_menu.h"
#include "unitD_menu.h"
#include "unitD3_menu.h"
#include "unitE_menu.h"
#include "unitF_menu.h"

// ==== tooltips: OFF ==============================================================
// Every menu tooltip goes through here instead of ImGui::SetTooltip. Body is empty, so
// nothing draws and no tooltip window is ever created -- which also keeps the tooltip
// out of the draw-data split in render.cpp (a tooltip is its own root window and would
// otherwise have to be classified). Text is kept at every call site: to bring tooltips
// back, uncomment the one line below.
static void MenuTooltip(const char* fmt, ...)
{
    (void)fmt;
    // va_list a; va_start(a, fmt); ImGui::SetTooltipV(fmt, a); va_end(a);
}

// ==== botox imgui fork widgets (dependencies/imgui/imgui_widgets.cpp), ported 1:1 =========
// Declared in menu.h so unit menus and aim.cpp port botox tab code verbatim. Fonts:
// BoldFont() = font_name_verdana_bd_11, g_fontIcon13 = font_name_icon_13.

// botox key_names, 256 entries, one per vk
static const char* const kKeyNames[256] = {
    "[-]",   "[m1]",  "[m2]",  "[cn]",  "[m3]",  "[m4]",  "[m5]",  "[-]",   "[bac]", "[tab]", "[-]",   "[-]",   "[clr]", "[ret]", "[-]",   "[-]",
    "[shi]", "[ctl]", "[men]", "[pau]", "[cap]", "[kan]", "[-]",   "[jun]", "[fin]", "[kan]", "[-]",   "[esc]", "[con]", "[nco]", "[acc]", "[mad]",
    "[spa]", "[pgu]", "[pgd]", "[end]", "[hom]", "[lef]", "[up]",  "[rig]", "[dow]", "[sel]", "[pri]", "[exe]", "[pri]", "[ins]", "[del]", "[hel]",
    "[0]",   "[1]",   "[2]",   "[3]",   "[4]",   "[5]",   "[6]",   "[7]",   "[8]",   "[9]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",
    "[-]",   "[a]",   "[b]",   "[c]",   "[d]",   "[e]",   "[f]",   "[g]",   "[h]",   "[i]",   "[j]",   "[k]",   "[l]",   "[m]",   "[n]",   "[o]",
    "[p]",   "[q]",   "[r]",   "[s]",   "[t]",   "[u]",   "[v]",   "[w]",   "[x]",   "[y]",   "[z]",   "[win]", "[win]", "[app]", "[-]",   "[sle]",
    "[num]", "[num]", "[num]", "[num]", "[num]", "[num]", "[num]", "[num]", "[num]", "[num]", "[mul]", "[add]", "[sep]", "[min]", "[dec]", "[div]",
    "[f1]",  "[f2]",  "[f3]",  "[f4]",  "[f5]",  "[f6]",  "[f7]",  "[f8]",  "[f9]",  "[f10]", "[f11]", "[f12]", "[f13]", "[f14]", "[f15]", "[f16]",
    "[f17]", "[f18]", "[f19]", "[f20]", "[f21]", "[f22]", "[f23]", "[f24]", "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",
    "[num]", "[scr]", "[equ]", "[mas]", "[toy]", "[oya]", "[oya]", "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",
    "[shi]", "[shi]", "[ctr]", "[ctr]", "[alt]", "[alt]", "[bbk]", "[bfw]", "[brf]", "[bst]", "[bse]", "[bfa]", "[bho]", "[mut]", "[vo-]", "[vo+]",
    "[nxt]", "[prv]", "[stp]", "[ply]", "[mai]", "[sel]", "[ap1]", "[ap2]", "[-]",   "[-]",   "[;]",   "[=]",   "[,]",   "[dsh]", "[.]",   "[/]",
    "[`]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",
    "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[-]",   "[[]",   "[\\]",  "[]]",   "[']",   "[oe8]",
    "[-]",   "[oem]", "[<>]",  "[oem]", "[oem]", "[ime]", "[oem]", "[pkt]", "[-]",   "[oem]", "[oem]", "[oem]", "[oem]", "[oem]", "[oem]", "[oem]",
    "[oem]", "[oem]", "[oem]", "[oem]", "[oem]", "[oem]", "[att]", "[crs]", "[exs]", "[eof]", "[ply]", "[zoo]", "[-]",   "[pa1]", "[clr]", "[-]"
};

static ImU32 BlendU32(const ImVec4& a, const ImVec4& b, float t)
{
    return ImGui::GetColorU32(ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t));
}

// botox popup chrome (OptionPopup / save_popup): 20px band, centred bold title, outline, accent line
static void PopupChrome(const char* title, const char* title_end, ImGuiID anim_id)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
    const float rnd = ImGui::GetStyle().WindowRounding - 2.f;
    ImGui::PushClipRect(p, ImVec2(p.x + sz.x, p.y + 20.f), false);
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + 20.f), kBandBg, rnd, ImDrawFlags_RoundCornersTop);
    ImGui::PopClipRect();
    ImGui::PushClipRect(p, ImVec2(p.x + sz.x, p.y + sz.y), false);
    dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), kTabBorder, rnd);
    auto anim = ImAnimationHelper(anim_id, ImGui::GetIO().DeltaTime);
    anim.Update(ImGui::IsMouseHoveringRect(p, ImVec2(p.x + sz.x, p.y + sz.y)) ? 3.f : -2.f, 1.f, 0.5f, 1.f);
    ImGui::PopClipRect();
    ImFont* f = BoldFont();
    const ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.f, title, title_end);
    dl->AddText(f, f->FontSize, ImVec2(p.x + (sz.x - ts.x) * 0.5f, p.y + (20.f - ts.y) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Text, anim.AnimationData->second), title, title_end);
    RenderFadedGradientLine(dl, ImVec2(p.x, p.y + 20.f), ImVec2(sz.x, 1.f), ImColor(Accent()));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20.f);
}

// Right-edge lane of the row just drawn: a cog and a keybind on one row stack leftward
// (first caller gets the right edge) instead of drawing on top of each other.
static float RowLaneX(float w)
{
    static ImGuiID s_win = 0;
    static float s_y = -1.f, s_used = 0.f;
    static int s_frame = -1;
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    const float y = win->DC.CursorPosPrevLine.y;
    if (s_win != win->ID || s_y != y || s_frame != ImGui::GetFrameCount())
    {
        s_win = win->ID; s_y = y; s_used = 0.f; s_frame = ImGui::GetFrameCount();
    }
    const float x = ImGui::GetRowFrameRight() - s_used - w;
    s_used += w + 6.f;
    return x;
}

void ImGui::OptionPopup(const char* str_id, const std::function<void()>& function, const ImVec2& window_size)
{
    ImGuiWindow* window = GetCurrentWindow();
    const char* title_end = FindRenderedTextEnd(str_id);
    {
        ImFont* icon = g_fontIcon13 ? g_fontIcon13 : GetFont();
        const char* cog = "\xef\x80\x93";   // U+F013
        const ImVec2 cs = icon->CalcTextSizeA(icon->FontSize, FLT_MAX, 0.f, cog);
        // pinned to the row's right edge, on the row just drawn (cursor is already one row below)
        const float cog_x = RowLaneX(15.f);
        const float cog_y = GetWindowPos().y + GetCursorPosY() - window->Scroll.y - 20.f;
        const bool hovered = IsMouseHoveringRect(ImVec2(cog_x, cog_y), ImVec2(cog_x + 15.f, cog_y - 1.f + cs.y), false);
        window->DrawList->AddText(icon, icon->FontSize, ImVec2(cog_x, cog_y),
                                  hovered ? IM_COL32_WHITE : IM_COL32(190, 190, 190, 255), cog);
        if (hovered && (IsMouseClicked(ImGuiMouseButton_Right) || IsMouseClicked(ImGuiMouseButton_Left)))
            OpenPopup(str_id);
    }
    SetNextWindowSize(window_size);
    if (BeginPopup(str_id))
    {
        PopupChrome(str_id, title_end, ImHashStr(str_id));
        function();
        EndPopup();
    }
}

// botox Keybind: "[key]" on the row above, right edge, 50% text -> accent while listening.
// Left click = listen (esc clears, click away cancels), right click = style popup.
// mode: 0 always on, 1 hold, 2 toggle (same order as botox EKeyStyles).
bool ImGui::Keybind(const char* label, int* key, int* mode, bool has_style)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;
    static ImGuiID s_listening = 0;
    static bool s_clean = false;   // every key seen up since listening began

    const ImGuiID id = window->GetID(label);
    if (*key < 0 || *key > 255) *key = 0;
    const bool listening = (s_listening == id);
    const char* disp = listening ? "[...]" : (*key ? kKeyNames[*key] : "[none]");

    SameLine(0.f, 0.f);
    const ImVec2 ts = CalcTextSize(disp);
    const ImVec2 ls = CalcTextSize(kKeyNames[*key]);   // botox label_size: popup anchor, read before input
    window->DC.CursorPos.x = RowLaneX(ts.x);
    const ImRect bb(window->DC.CursorPos, ImVec2(window->DC.CursorPos.x + ts.x, window->DC.CursorPos.y + ts.y));
    ItemSize(ts, 0.f);
    if (!ItemAdd(bb, id)) return false;

    bool changed = false;
    const bool hovered = ItemHoverable(bb, id);
    if (hovered && IsMouseClicked(0)) { if (!listening) { s_listening = id; s_clean = false; *key = 0; } }
    else if (IsMouseClicked(0)) { if (listening) s_listening = 0; }
    else if (hovered && IsMouseClicked(1) && has_style) OpenPopup(label);

    if (s_listening == id)
    {
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { *key = 0; s_listening = 0; }
        else
        {
            bool anyDown = false;
            int pressed = 0;
            for (int vk = 0x01; vk <= 0xFE && !pressed; ++vk)
            {
                if (vk == VK_ESCAPE || vk == 0x07 || vk == 0x0A || vk == 0x0B || vk == 0xE5 || vk == 0xE7) continue;
                if (GetAsyncKeyState(vk) & 0x8000) { anyDown = true; if (s_clean) pressed = vk; }
            }
            if (!anyDown) s_clean = true;   // the click that started listening must be released first
            if (pressed) { *key = pressed; s_listening = 0; changed = true; }
        }
    }

    PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 8.f));
    SetNextWindowSize(ImVec2(61.f, 65.f), ImGuiCond_Always);
    // botox frame_bb.Max = cursor - label size: popup opens up-left of the key text
    SetNextWindowPos(ImVec2(bb.Min.x - ls.x + 5.f, bb.Min.y - ls.y), ImGuiCond_Always);
    if (BeginPopup(label, ImGuiWindowFlags_NoMove))
    {
        ImGuiWindow* pw = GetCurrentWindow();
        pw->DrawList->AddRect(pw->Pos, ImVec2(pw->Pos.x + pw->Size.x, pw->Pos.y + pw->Size.y), kTabBorder, GetStyle().WindowRounding);
        if (Selectable("always on", *mode == 0, ImGuiSelectableFlags_DontClosePopups)) *mode = 0;
        if (Selectable("hold",      *mode == 1, ImGuiSelectableFlags_DontClosePopups)) *mode = 1;
        if (Selectable("toggle",    *mode == 2, ImGuiSelectableFlags_DontClosePopups)) *mode = 2;
        EndPopup();
    }
    PopStyleVar();

    auto anim = ImAnimationHelper(id + ImHashStr("text-animation"), GetIO().DeltaTime);
    anim.Update(2.f, (s_listening == id) ? 2.f : -2.f);
    ImVec4 tc = GetStyleColorVec4(ImGuiCol_Text); tc.w *= 0.5f;
    ImVec4 ac = GetStyleColorVec4(ImGuiCol_Accent); ac.w = 1.f;
    window->DrawList->AddText(bb.Min, BlendU32(tc, ac, anim.AnimationData->second), disp);
    return changed;
}

// botox MultiCombo on a bitmask (bit i = item i): preview = picked items joined ", ", word-wrapped
// to the frame with "...", "none" when empty; rows toggle without closing.
bool ImGui::MultiCombo(const char* label, int* mask, const char* const items[], int count)
{
    if (GetCurrentWindow()->SkipItems) return false;
    std::string buf;
    for (int i = 0; i < count; ++i)
        if (*mask & (1 << i)) { if (!buf.empty()) buf += ", "; buf += items[i]; }
    if (buf.empty()) buf = "none";
    else
    {
        ImGuiContext& g = *GImGui;
        const float w = GetRowFrameWidth();
        const char* wrap = g.Font->CalcWordWrapPositionA(GetCurrentWindow()->FontWindowScale, buf.data(), buf.data() + buf.size(),
                                                        w - g.Style.FramePadding.x * 2.f);
        const size_t n = (size_t)(wrap - buf.data());
        if (n > 0 && n < buf.size()) { buf.resize(n); buf += "..."; }
    }
    bool changed = false;
    if (BeginCombo(label, buf.c_str(), 0))
    {
        for (int i = 0; i < count; ++i)
            if (Selectable(items[i], (*mask & (1 << i)) != 0, ImGuiSelectableFlags_DontClosePopups)) { *mask ^= (1 << i); changed = true; }
        EndCombo();
    }
    return changed;
}

// botox ColorEdit4(label, col, flags, swatch_slot): ONE row -- label at the row's left edge,
// swatch pinned to the row's right edge (slot N = N swatches further left), click = botox picker
// popup. The body is the imgui fork's ColorEdit4 (botox port); this name stays for the call sites.
bool ImGui::ColorRow(const char* label, float col[4], ImGuiColorEditFlags flags, int swatch_slot)
{
    return ColorEdit4(label, col, flags, swatch_slot);
}

void MenuSectionHeader(const char* label) { SectionHeader(label); }

void MenuBindKey(const char* id, int* key, int* mode) { ImGui::Keybind(id, key, mode, true); }

// key capture only, no style popup (edge-press actions: set/delete point)
static void BindKeyNoMode(const char* id, int* key)
{
    int unused = 1;
    ImGui::Keybind(id, key, &unused, false);
}

// ==== sliders =======================================================================
// A slider's min/max bound the DRAG only, never the value. Ctrl+click (or right-click)
// any slider to type a value -- negative fog start, a fog end past 10000 -- imgui keeps
// it as typed (no ImGuiSliderFlags_AlwaysClamp anywhere in the menu) and config.cpp
// round-trips it through a plain %.4f / atof, so it saves and reloads unchanged.
//
// What was missing is the DRAW: a value outside [lo,hi] pinned the thumb at an end, and
// the very next drag snapped it back into range with no way to get out again. These two
// widen the drawn range to hold the current value, so an out-of-range value shows where
// it actually sits and stays draggable. The normal range is untouched while the value is
// inside it -- widening happens only for a value that is ALREADY out, so no drag can
// reach a region the slider didn't already offer.
//
// Non-finite is refused (NaN/inf would make the range itself garbage) -- the value is
// still whatever was typed, only the bar falls back to its declared range.
static bool SliderF(const char* label, float* v, float lo, float hi, const char* fmt = "%.3f")
{
    float dlo = lo, dhi = hi;
    if (isfinite(*v))
    {
        if (*v < dlo) dlo = *v;
        if (*v > dhi) dhi = *v;
    }
    return ImGui::SliderFloat(label, v, dlo, dhi, fmt);
}

static bool SliderI(const char* label, int* v, int lo, int hi, const char* fmt = "%d")
{
    const int dlo = *v < lo ? *v : lo;
    const int dhi = *v > hi ? *v : hi;
    return ImGui::SliderInt(label, v, dlo, dhi, fmt);
}

static void CcCurveEditor()
{
    static int s_chan = 0;      // 0 = master (rgb), 1/2/3 = R/G/B
    static int s_drag = -1;

    static const char* k_names[4]  = { "rgb", "r", "g", "b" };
    static const ImU32 k_bright[4] = { IM_COL32(235,235,235,240), IM_COL32(235, 95, 95,240),
                                       IM_COL32( 95,215,110,240), IM_COL32(100,150,255,240) };
    static const ImU32 k_dim[4]    = { IM_COL32(235,235,235, 60), IM_COL32(235, 95, 95, 60),
                                       IM_COL32( 95,215,110, 60), IM_COL32(100,150,255, 60) };

    {
        const float avail = ImGui::GetContentRegionAvail().x;
        const float gap   = ImGui::GetStyle().ItemSpacing.x;
        const float bw    = (avail - gap * 3.f) / 4.f;
        for (int ch = 0; ch < 4; ++ch)
        {
            if (ch) ImGui::SameLine(0.f, gap);
            const bool on = (s_chan == ch);
            ImGui::PushID(ch);
            if (on)
            {
                const ImVec4 c = ImGui::ColorConvertU32ToFloat4(k_bright[ch]);
                ImGui::PushStyleColor(ImGuiCol_Button,        c);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  c);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.10f, 0.10f, 0.10f, 1.f));
            }
            if (ImGui::Button(k_names[ch], ImVec2(bw, 0.f))) { s_chan = ch; s_drag = -1; }
            if (on) ImGui::PopStyleColor(4);
            ImGui::PopID();
        }
    }

    CcCurve& cv = g_ccCurve[s_chan];

    const float  w  = ImGui::GetContentRegionAvail().x;
    const float  h  = (w < 220.f) ? w : 220.f;      // square until it would eat the tab
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + w, p0.y + h);

    ImGui::InvisibleButton("##ccgraph", ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered();
    const bool act = ImGui::IsItemActive();

    auto ToScreen = [&](float x, float y) { return ImVec2(p0.x + x * w, p0.y + (1.f - y) * h); };

    const ImVec2 mp = ImGui::GetIO().MousePos;
    const float  mx = (mp.x - p0.x) / w;
    const float  my = 1.f - (mp.y - p0.y) / h;

    // Nearest control point to the cursor, in pixels. One hit test drives grab, add and
    // remove, so they can never disagree about which point is under the mouse.
    int   hit  = -1;
    float hitD = 1e9f;
    for (int i = 0; i < cv.n; ++i)
    {
        const ImVec2 s = ToScreen(cv.x[i], cv.y[i]);
        const float  d = hypotf(mp.x - s.x, mp.y - s.y);
        if (d < hitD) { hitD = d; hit = i; }
    }
    const bool onPoint = (hov && hitD <= 11.f);

    if (act && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        s_drag = onPoint ? hit : CcAddPoint(cv, mx, my);
    if (s_drag >= 0 && s_drag < cv.n && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        CcMovePoint(cv, s_drag, mx, my);
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) s_drag = -1;
    if (onPoint && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        CcRemovePoint(cv, hit);
        s_drag = -1;
        hit    = -1;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(18, 18, 18, 235), 3.f);
    for (int i = 1; i < 4; ++i)
    {
        const float gx = p0.x + w * i * 0.25f;
        const float gy = p0.y + h * i * 0.25f;
        dl->AddLine(ImVec2(gx, p0.y), ImVec2(gx, p1.y), IM_COL32(255, 255, 255, 16));
        dl->AddLine(ImVec2(p0.x, gy), ImVec2(p1.x, gy), IM_COL32(255, 255, 255, 16));
    }
    dl->AddLine(ImVec2(p0.x, p1.y), ImVec2(p1.x, p0.y), IM_COL32(255, 255, 255, 30));
    dl->AddRect(p0, p1, ImGui::GetColorU32(ImGuiCol_Border), 3.f);

    // Inactive channels first so the one being edited is never drawn under another. A
    // channel still on the diagonal is skipped: three identity lines lying on the
    // reference diagonal is noise, not information.
    const int kSeg = 96;
    ImVec2 pts[kSeg + 1];
    for (int pass = 0; pass < 2; ++pass)
        for (int ch = 0; ch < 4; ++ch)
        {
            const bool on = (ch == s_chan);
            if (on != (pass == 1)) continue;
            if (!on && CcCurveIsIdentity(g_ccCurve[ch])) continue;
            for (int i = 0; i <= kSeg; ++i)
            {
                const float x = (float)i / (float)kSeg;
                pts[i] = ToScreen(x, CcEval(g_ccCurve[ch], x));
            }
            dl->AddPolyline(pts, kSeg + 1, on ? k_bright[ch] : k_dim[ch],
                            ImDrawFlags_None, on ? 1.8f : 1.f);
        }

    for (int i = 0; i < cv.n; ++i)
    {
        const ImVec2 s   = ToScreen(cv.x[i], cv.y[i]);
        const bool   lit = (s_drag == i) || (onPoint && hit == i);
        const float  r   = lit ? 5.0f : 3.8f;
        dl->AddCircleFilled(s, r, lit ? IM_COL32(255, 255, 255, 255) : k_bright[s_chan]);
        dl->AddCircle(s, r, IM_COL32(15, 15, 15, 210), 0, 1.2f);
    }

    // Readout in 8-bit codes, because that is the unit the curve actually lands in.
    if (s_drag >= 0 && s_drag < cv.n)
        ImGui::Text("%s   in %3d   out %3d", k_names[s_chan],
                    (int)(cv.x[s_drag] * 255.f + 0.5f), (int)(cv.y[s_drag] * 255.f + 0.5f));
    else if (hov)
        ImGui::TextDisabled("left add / drag, right remove");
    else
        ImGui::TextDisabled("%s   %d points", k_names[s_chan], cv.n);

    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float bw2 = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
    if (ImGui::Button("reset curve", ImVec2(bw2, 0.f))) CcResetCurve(cv);
    ImGui::SameLine(0.f, gap);
    if (ImGui::Button("reset all curves", ImVec2(bw2, 0.f)))
        for (int i = 0; i < 4; ++i) CcResetCurve(g_ccCurve[i]);
}

// ==== tab bodies ====================================================================
// Was three side-by-side columns in one window. Same widgets, same order inside a
// section -- they just live in a Tab_* function now and only one runs per frame.
// PushItemWidth(-1) is done ONCE around the switch in RenderMenu, so nothing here
// pushes or pops it.

static void Page_VisualsEsp()
{
    menu_columns_begin();
    if (menu_group_begin("players"))
    {
        // UNIT_MENU_SLOT visuals_esp
        UnitC_Menu_visuals_esp();
    }
    menu_group_end();

    // botox puts "edicts" under "players" in this column; TF2's edicts that matter are buildings
    if (menu_group_begin("buildings"))
        UnitC_Menu_visuals_buildings();
    menu_group_end();

    menu_columns_next();
    if (menu_group_begin("chams"))
        UnitC_Menu_visuals_chams();
    menu_group_end();
    menu_columns_end();
}

// Skybox name combo, shared by the botox-named "sky" row below. Builtins first, then
// whatever the user last typed (custom), so the combo never loses the live value.
static void SkyboxCombo()
{
    static const char* kSkyList[] = {
        "sky_tf2_04", "sky_upward", "sky_dustbowl_01", "sky_goldrush_01",
        "sky_granary_01", "sky_well_01", "sky_gravel_01", "sky_badlands_01",
        "sky_hydro_01", "sky_night_01", "sky_nightfall_01", "sky_trainyard_01",
        "sky_stormfront_01", "sky_morningsnow_01", "sky_alpinestorm_01",
        "sky_harvest_01", "sky_harvest_night_01", "sky_halloween",
        "sky_halloween_night_01", "sky_halloween_night2014_01",
        "sky_island_01", "sky_jungle_01", "sky_invasion2fort_01",
        "sky_coastal_01", "sky_outpost_01", "sky_rainbow_01",
    };
    constexpr int kSkyCount = (int)(sizeof(kSkyList) / sizeof(kSkyList[0]));

    // -1 = custom; -2 = unsynced (match g_skyName once)
    static int s_skySel = -2;
    if (s_skySel == -2)
    {
        s_skySel = -1;
        for (int i = 0; i < kSkyCount; ++i)
            if (strcmp(kSkyList[i], g_skyName) == 0) { s_skySel = i; break; }
    }

    const char* preview = (s_skySel >= 0) ? kSkyList[s_skySel] : "custom";
    if (ImGui::BeginCombo("skybox", preview))
    {
        for (int i = 0; i < kSkyCount; ++i)
            if (ImGui::Selectable(kSkyList[i], s_skySel == i))
            {
                s_skySel = i;
                strncpy(g_skyName, kSkyList[i], sizeof(g_skyName) - 1);
                g_skyName[sizeof(g_skyName) - 1] = '\0';
            }
        if (ImGui::Selectable("custom", s_skySel == -1))
            s_skySel = -1;
        ImGui::EndCombo();
    }
    if (s_skySel == -1)
        ImGui::InputText("##skyname", g_skyName, sizeof(g_skyName));
}

static void Page_VisualsWorld()
{
    menu_columns_begin();
    if (menu_group_begin("world"))
    {
        ImGui::Checkbox("precipitation", &g_weatherEnable);
        if (g_weatherEnable)
            ImGui::OptionPopup("precipitation settings", [&]() {
                // combo index 0/1 = g_weatherMode 1/2 (0 is "off"). Binding the combo straight to
                // g_weatherMode made "rain" write 0 (off) and "snow" write 1 (rain).
                int type = (g_weatherMode == 2) ? 1 : 0;
                if (ImGui::Combo("type##precipitation", &type, "rain\0" "snow\0"))
                    g_weatherMode = type + 1;
                if (g_weatherMode != 2)
                {
                    // Floors are NOT taste: speed 0 divides by zero in ComputeEmissionArea,
                    // length 0 collapses the tracer quad, radius 0 is an empty emission box.
                    SliderF("alpha##rain", &g_rainAlpha, 0.05f, 1.f, "%.2f");
                    SliderF("speed##rain", &g_rainSpeed, 50.f, 2000.f, "%.0f");
                    SliderF("width##rain", &g_rainWidth, 0.05f, 5.f, "%.2f");
                    SliderF("length##rain", &g_rainLength, 0.01f, 1.f, "%.2f");
                    SliderF("radius##rain", &g_rainRadius, 256.f, 5000.f, "%.0f");
                    SliderF("side vel##rain", &g_rainSideVel, 0.f, 500.f, "%.0f");
                }
                else
                {
                    SliderF("alpha##snow", &g_snowAlpha, 0.05f, 1.f, "%.2f");
                    SliderF("speed##snow", &g_snowSpeed, 20.f, 500.f, "%.0f");
                    SliderF("size##snow", &g_snowWidth, 0.05f, 10.f, "%.1f");
                    // A flake is still a tracer quad -- r_rainlength 0 draws literally nothing.
                    SliderF("trail##snow", &g_snowLength, 0.01f, 1.f, "%.2f");
                    SliderF("radius##snow", &g_snowRadius, 256.f, 5000.f, "%.0f");
                    SliderF("drift##snow", &g_snowSideVel, 0.f, 100.f, "%.0f");
                    SliderF("density##snow", &g_snowDensity, 0.05f, 1.f, "%.2f");
                }
            }, ImVec2(220.f, -1));

        SkyboxCombo();

        ImGui::Checkbox("fog", &g_fogEnable);
        if (g_fogEnable)
            ImGui::OptionPopup("fog settings", [&]() {
                ImGui::ColorEdit4("color##fog", g_fogColor);
                SliderF("start##fog", &g_fogStart, 0.f, 5000.f, "%.0f");
                SliderF("end##fog", &g_fogEnd, 0.f, 10000.f, "%.0f");
                SliderF("density##fog", &g_fogDensity, 0.f, 1.f, "%.2f");
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("flip world", &g_flipWorld);

        // UNIT_MENU_SLOT visuals_world (world fov, fullbright, dlight, sun angle, world/prop
        // texture colour, weather particle colour, minecraft sky)
        UnitD_Menu_visuals_world();
    }
    menu_group_end();

    if (menu_group_begin("edicts"))
        UnitC_Menu_visuals_world();
    menu_group_end();

    menu_columns_next();
    if (menu_group_begin("removals"))
    {
        ImGui::Checkbox("no hats", &g_noHats);
        ImGui::Checkbox("no unusual effects", &g_noUnusual);
        ImGui::Checkbox("remove ragdolls", &g_removeRagdolls);
        ImGui::Checkbox("remove prop fade", &g_removePropFade);
        ImGui::Checkbox("fps boost", &g_fpsBoost);
        ImGui::Checkbox("remove screen effects", &g_removeScreenEffects);
        ImGui::Checkbox("disable interp", &g_disableInterpEnable);
        ImGui::Checkbox("remove visual recoil", &g_removeVisualRecoilEnable);
    }
    menu_group_end();

    if (menu_group_begin("viewmodel"))
    {
        ImGui::Checkbox("sway", &g_vmSwayEnable);
        if (g_vmSwayEnable)
            ImGui::OptionPopup("sway settings", [&]() {
                SliderF("sway scale", &g_vmSwayScale, 0.f, 10.f, "%.1f");
                SliderF("sway interp", &g_vmSwayInterp, 0.f, 1.f, "%.2f");
                if (ImGui::Button("csgo sway##vs", ImVec2(-1.f, 0.f)))
                {
                    g_vmSwayScale = 1.6f;    // cl_wpn_sway_scale  (CS:GO default)
                    g_vmSwayInterp = 0.1f;   // cl_wpn_sway_interp (CS:GO default)
                }
            }, ImVec2(200.f, -1));

        ImGui::Checkbox("offset", &g_vmOffsetEnable);
        if (g_vmOffsetEnable)
            ImGui::OptionPopup("offset settings", [&]() {
                SliderF("x##vmoffset", &g_vmOffsetX, -20.f, 20.f, "%.1f");
                SliderF("y##vmoffset", &g_vmOffsetY, -20.f, 20.f, "%.1f");
                SliderF("z##vmoffset", &g_vmOffsetZ, -20.f, 20.f, "%.1f");
            }, ImVec2(200.f, -1));

        ImGui::Checkbox("remove bob", &g_vmRemoveBob);

        ImGui::BeginDisabled(g_vmRemoveBob);
        ImGui::Checkbox("csgo bob", &g_vmCsBob);
        if (g_vmCsBob)
            ImGui::OptionPopup("csgo bob settings", [&]() {
                SliderF("lower amount##vcb", &g_vmCsBobLower, 5.f, 30.f, "%.1f");
                SliderF("bob up/down##vcb", &g_vmCsBobVert, 0.1f, 2.f, "%.2f");
                SliderF("bob side##vcb", &g_vmCsBobLat, 0.1f, 2.f, "%.2f");
                SliderF("bob cycle##vcb", &g_vmCsBobCycle, 0.1f, 2.f, "%.2f");
                SliderF("bob up time##vcb", &g_vmCsBobUp, 0.05f, 0.95f, "%.2f");
                if (ImGui::Button("csgo defaults##vcb", ImVec2(-1.f, 0.f)))
                {
                    g_vmCsBobLower = 21.f; g_vmCsBobVert = 0.25f; g_vmCsBobLat = 0.4f;
                    g_vmCsBobCycle = 0.98f; g_vmCsBobUp = 0.5f;
                }
            }, ImVec2(200.f, -1));
        ImGui::EndDisabled();

        ImGui::Checkbox("csgo jump lower", &g_vmCsGunLower);
        if (g_vmCsGunLower)
            ImGui::OptionPopup("csgo jump lower settings", [&]() {
                SliderF("angle##vcgl", &g_vmCsGunLowerAngle, 0.f, 20.f, "%.1f");
                SliderF("speed##vcgl", &g_vmCsGunLowerSpeed, 0.01f, 2.f, "%.2f");
            }, ImVec2(200.f, -1));

        ImGui::Checkbox("csgo landing dip", &g_vmCsLandDip);
        if (g_vmCsLandDip)
            ImGui::OptionPopup("csgo landing dip settings", [&]() {
                SliderF("dip##vcld", &g_vmCsLandDipAmt, 0.f, 20.f, "%.0f");
                SliderF("dip time##vcld", &g_vmCsLandDipTime, 0.03f, 0.5f, "%.2fs");
            }, ImVec2(200.f, -1));

        // The cfg has been found sitting on vmAlpha=1.0000 -- the value the feature itself
        // treats as off (h_DrawModelExecute gates on < 0.999f). Ticking the box there does
        // nothing and reads as a broken feature, so an enable at the opaque end lands on a
        // visible value instead.
        const bool wasVmAlpha = g_vmAlphaEnable;
        ImGui::Checkbox("transparent", &g_vmAlphaEnable);
        if (g_vmAlphaEnable && !wasVmAlpha && g_vmAlpha >= 0.999f)
            g_vmAlpha = 0.5f;
        if (g_vmAlphaEnable)
            ImGui::OptionPopup("transparent settings", [&]() {
                SliderF("alpha##vm", &g_vmAlpha, 0.f, 1.f, "%.2f");
                ImGui::Checkbox("flat alpha##vm", &g_vmFlatAlpha);
            }, ImVec2(200.f, -1));
    }
    menu_group_end();
    menu_columns_end();
}

static void Page_VisualsScreen()
{
    menu_columns_begin();
    if (menu_group_begin("screen"))
    {
        // UNIT_MENU_SLOT visuals_screen (third person, aspect ratio, sharpen, film grain,
        // vignette, invert filter, posterize, deband, channel shift, emphasize, show depth)
        UnitD_Menu_visuals_screen();

        ImGui::Checkbox("color correction", &g_ccEnable);
        if (g_ccEnable)
            ImGui::OptionPopup("color correction settings", [&]() {
                CcCurveEditor();

                ImGui::Spacing();
                SliderF("exposure",    &g_ccExposure, -3.f,  3.f,  "%+.2f EV");
                SliderF("contrast",    &g_ccContrast,  0.5f, 1.5f, "%.3f");
                SliderF("saturation",  &g_ccSat,       0.f,  3.f,  "%.2f");
                SliderF("temperature", &g_ccTemp,     -1.f,  1.f,  "%+.2f");
                SliderF("hue",         &g_ccHue,    -180.f, 180.f, "%.1f deg");
                SliderF("highlights",  &g_ccHighlights, -1.f, 1.f, "%+.2f");
                SliderF("shadows",     &g_ccShadows,    -1.f, 1.f, "%+.2f");

                ImGui::Spacing();
                ImGui::Label("levels");
                SliderF("in black",  &g_ccLevInBlack,  0.f, 0.99f, "%.3f");
                SliderF("in white",  &g_ccLevInWhite,  0.01f, 1.f, "%.3f");
                SliderF("gamma",     &g_ccGamma,       0.1f, 4.f,  "%.2f");
                SliderF("out black", &g_ccLevOutBlack, 0.f, 1.f,   "%.3f");
                SliderF("out white", &g_ccLevOutWhite, 0.f, 1.f,   "%.3f");
                // An input white at or under the input black leaves two colours on screen.
                if (g_ccLevInWhite < g_ccLevInBlack + 0.01f)
                    g_ccLevInWhite = g_ccLevInBlack + 0.01f;

                ImGui::Spacing();
                ImGui::Label("gain");
                SliderF("red",   &g_ccGainR, 0.f, 2.f, "%.2f");
                SliderF("green", &g_ccGainG, 0.f, 2.f, "%.2f");
                SliderF("blue",  &g_ccGainB, 0.f, 2.f, "%.2f");

                ImGui::Spacing();
                if (ImGui::Button("reset all##cc", ImVec2(-1.f, 0.f))) CcResetAll();
            }, ImVec2(260.f, -1));

        // UNIT_MENU_SLOT visuals_screen (reflections, minecraft sky moved to world)
        UnitD3_Menu_visuals_screen();

        ImGui::Checkbox("ambient occlusion", &g_aoEnable);
        if (g_aoEnable)
            ImGui::OptionPopup("ambient occlusion settings", [&]() {
                SliderF("intensity##ao",  &g_aoIntensity, 0.f, 4.f,    "%.2f");
                SliderF("radius##ao",     &g_aoRadius,    4.f, 200.f,  "%.0f u");
                SliderI("samples##ao",    &g_aoSamples,   4,   64);
                SliderF("bias##ao",       &g_aoBias,      0.f, 0.8f,   "%.2f");
                SliderF("blur##ao",       &g_aoBlur,      0.f, 4.f,    "%.2f");
                SliderF("render size##ao", &g_aoScale,    0.25f, 1.f,  "%.2f");

                ImGui::Label("range");
                SliderF("fade start##ao", &g_aoFadeStart, 0.f, 4000.f, "%.0f u");
                SliderF("fade end##ao",   &g_aoFadeEnd,   0.f, 8000.f, "%.0f u");
                if (g_aoFadeEnd < g_aoFadeStart + 1.f) g_aoFadeEnd = g_aoFadeStart + 1.f;

                ImGui::Checkbox("skip viewmodel##ao", &g_aoViewmodel);
                ImGui::Checkbox("fade under fog##ao", &g_aoFogFade);

                ImGui::Label("debug");
                ImGui::Checkbox("show occlusion##ao", &g_aoDebug);
            }, ImVec2(240.f, -1));

        ImGui::Checkbox("depth of field", &g_dofEnable);
        if (g_dofEnable)
            ImGui::OptionPopup("depth of field settings", [&]() {
                ImGui::Label("focus");
                ImGui::Checkbox("auto focus##dof", &g_dofAutoFocus);
                if (g_dofAutoFocus)
                {
                    SliderF("focus speed##dof", &g_dofFocusSpeed, 0.1f, 40.f,   "%.1f /s");
                    SliderF("focus limit##dof", &g_dofFocusLimit, 64.f, 8192.f, "%.0f u");
                }
                else
                    SliderF("focal distance##dof", &g_dofDistance, 16.f, 8192.f, "%.0f u");

                ImGui::Label("range");
                SliderF("sharp band##dof", &g_dofRange,   0.f, 2000.f, "%.0f u");
                SliderF("blur ramp##dof",  &g_dofRamp,    1.f, 4000.f, "%.0f u");

                ImGui::Label("blur");
                SliderF("max blur##dof",     &g_dofMaxBlur, 0.f, 60.f,   "%.1f px");
                SliderF("tap density##dof",  &g_dofTapDensity, 0.5f, 2.5f, "%.2f /px");
                SliderF("highlights##dof",   &g_dofBokehBoost,     0.f, 16.f, "%.1f");
                SliderF("highlight cutoff##dof", &g_dofBokehThreshold, 0.f, 1.f,  "%.2f");
                ImGui::Checkbox("near blur##dof", &g_dofNearBlur);
                ImGui::Checkbox("viewmodel stays sharp##dof", &g_dofViewmodelSharp);
                SliderF("particle guard##dof", &g_fxParticleGuard, 0.f, 1.f, "%.2f");
                ImGui::Checkbox("smoothing pass##dof", &g_dofSmooth);
            }, ImVec2(240.f, -1));

        ImGui::Checkbox("motion blur", &g_fbEnable);
        if (g_fbEnable)
            ImGui::OptionPopup("motion blur settings", [&]() {
                SliderF("shutter##fb", &g_fbShutter, 0.f, 60.f, "%.1f ms");
                SliderF("world amount##fb", &g_fbAmount, 0.f, 4.f, "%.2f");
                SliderI("samples##fb", &g_fbSamples, 4, 64);
                SliderF("max streak##fb", &g_fbMaxStreak, 0.01f, 0.2f, "%.3f h");

                ImGui::Checkbox("blur other players##fb", &g_fbPlayers);
                ImGui::Checkbox("blur the viewmodel##fb", &g_fbViewmodel);
                if (g_fbViewmodel)
                    SliderF("viewmodel amount##fb", &g_fbVmAmount, 0.f, 16.f, "%.2f");
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("bloom", &g_bloomEnable);
        if (g_bloomEnable)
            ImGui::OptionPopup("bloom settings", [&]() {
                SliderF("intensity##bloom", &g_bloomIntensity, 0.f, 3.f, "%.2f");
                SliderF("threshold##bloom", &g_bloomThreshold, 0.f, 1.f, "%.2f");
                SliderF("radius##bloom",    &g_bloomRadius,    0.f, 1.f, "%.2f");
                SliderF("saturation##bloom", &g_bloomSat,      0.f, 3.f, "%.2f");
            }, ImVec2(210.f, -1));

        // botox menu_tab_visuals.cpp:1688, same spot (screen group, after bloom)
        ImGui::Checkbox("low fps viewmodel", &g_vmLowFps);
        if (g_vmLowFps)
            ImGui::OptionPopup("low fps viewmodel settings", [&]() {
                SliderF("fps##low fps viewmodel", &g_vmLowFpsRate, 15.f, 60.f, "%.0f fps");
            }, ImVec2(210.f, -1));

        ImGui::Checkbox("lag resolver", &g_lagResolver);
        if (g_lagResolver)
            ImGui::OptionPopup("lag resolver settings", [&]() {
                SliderF("smoothing##lr", &g_lrSmoothMs, 15.f, 200.f, "%.0f ms");
                SliderF("carry##lr", &g_lrExtrapMs, 40.f, 400.f, "%.0f ms");
                SliderF("snap past##lr", &g_lrMaxErr, 8.f, 128.f, "%.0f u");
                ImGui::Checkbox("smooth turning##lr", &g_lrYaw);
            }, ImVec2(200.f, -1));

        ImGui::Checkbox("queue passes onto the render thread", &g_fxUseCallQueue);
        ImGui::Checkbox("force mat_queue_mode 0", &g_fxForceQueueMode);
        ImGui::Checkbox("depth probe", &g_fxDepthProbe);
    }
    menu_group_end();

    menu_columns_next();
    if (menu_group_begin("crosshair"))
    {
        // cog first, then the swatch one slot left of it, both on the feature row (SimpleVisual:
        // arrow then swatch). The swatch on its own row put slot 0 exactly under the cog, and the
        // swatch's click-release picker replaced the settings popup the cog opened on press.
        ImGui::Checkbox("enabled##ch", &g_chEnable);
        if (g_chEnable)
        {
            ImGui::OptionPopup("crosshair settings", [&]() {
                ImGui::Checkbox("antialias", &g_chAntialias);
                if (Crosshair_BlendCombo("blend##chblend"))
                    MenuTooltip("how the arms + dot mix with the frame under them. additive adds in\n"
                                "linear light so the colour survives instead of clipping to white;\n"
                                "the rest are krita's blending modes. alpha is the opacity, the\n"
                                "outline stays normal");
                SliderF("thickness##ch", &g_chThickness, 0.f, 10.f, "%.1f");
                SliderF("length##ch", &g_chLength, 0.f, 30.f, "%.1f");
                SliderF("arm aspect##ch", &g_chAspect, 0.25f, 4.f, "%.2f");
                if (ImGui::IsItemHovered())
                    MenuTooltip("width : height of the arm pair. 1.00 = square, above 1 stretches\n"
                                "the left/right arms, below 1 the top/bottom -- the short pair\n"
                                "always keeps the length above, so nothing shrinks");
                SliderF("gap##ch", &g_chGap, 0.f, 20.f, "%.1f");
                SliderF("gap aspect##ch", &g_chGapAspect, 0.25f, 4.f, "%.2f");
                if (ImGui::IsItemHovered())
                    MenuTooltip("width : height of the gap. above 1 pushes the left/right arms\n"
                                "out, below 1 the top/bottom -- the close pair keeps the gap\n"
                                "above, so nothing pulls in");
                ImGui::Checkbox("dot##ch", &g_chDot);
                ImGui::Checkbox("dot circle##ch", &g_chDotCircle);
                ImGui::Checkbox("outline##ch", &g_chOutline);
                if (g_chOutline)
                {
                    ImGui::ColorRow("outline color##choutcol", g_chOutlineColor);
                    SliderF("outline thickness##ch", &g_chOutlineThick, 0.f, 10.f, "%.1f");
                }
            }, ImVec2(210.f, -1));
            ImGui::SameLine();
            ImGui::ColorRow("##chcol", g_chColor, 0, 1);
        }

        ImGui::Checkbox("csgo crosshair", &g_chCsgoEnable);
        if (g_chCsgoEnable)
        {
            ImGui::OptionPopup("csgo crosshair settings", [&]() {
                // exact cl_crosshairstyle numbering: 0/1 = panorama reticle, 2/3/4 = vgui bars
                ImGui::Combo("style##chcsgostyle", &g_chCsgoStyle,
                    "default\0" "default static\0" "classic standard\0" "classic dynamic\0" "classic static\0");

                if (g_chCsgoStyle <= 1)
                {
                    SliderF("gap offset##csgo", &g_chCsgoGap, -20.f, 20.f, "%.1f");
                    SliderF("gap aspect##csgo", &g_chCsgoGapAspect, 0.25f, 4.f, "%.2f");
                    SliderF("scale##csgo", &g_chCsgoScale, 0.5f, 8.f, "%.2f");
                    ImGui::Checkbox("crisp##csgo", &g_chCsgoCrisp);
                    ImGui::Checkbox("linear##csgo", &g_chCsgoLinear);
                    ImGui::Checkbox("additive##csgo", &g_chCsgoAdditive);
                    SliderF("black##csgo", &g_chCsgoBlack, 0.f, 1.f, "%.2f");
                    SliderF("velocity sens##csgo",  &g_chCsgoVelSens, 0.f, 2.f, "%.2f");
                    SliderF("velocity smooth##csgo", &g_chCsgoSmooth, 0.f, 0.5f, "%.2f s");
                    SliderF("arm aspect##csgo", &g_chCsgoAspect, 0.25f, 4.f, "%.2f");
                    if (g_chCsgoStyle == 0)
                        ImGui::Checkbox("spread lines##csgo", &g_chCsgoArcs);
                    ImGui::Checkbox("arms##csgo", &g_chCsgoPips);
                }
                else
                {
                    SliderF("size##csx",      &g_csXSize,  0.f, 20.f, "%.1f");
                    SliderF("arm aspect##csx", &g_chCsgoAspect, 0.25f, 4.f, "%.2f");
                    SliderF("thickness##csx", &g_csXThick, 0.f, 6.f,  "%.1f");
                    SliderF("gap##csx",       &g_csXGap,  -5.f, 20.f, "%.1f");
                    SliderF("gap aspect##csx", &g_chCsgoGapAspect, 0.25f, 4.f, "%.2f");
                    SliderI("alpha##csx",       &g_csXAlpha, 0, 255);
                    ImGui::Checkbox("dot##csx",     &g_csXDot);
                    ImGui::Checkbox("outline##csx", &g_csXOutline);
                    if (g_csXOutline)
                        // whole pixels: csgo's outline is an int vgui rect
                        SliderF("outline thickness##csx", &g_csXOutlineTh, 1.f, 3.f, "%.0f");
                    ImGui::Checkbox("t style##csx",   &g_csXT);
                    ImGui::Checkbox("use alpha##csx", &g_csXUseAlpha);
                    if (g_chCsgoStyle <= 3)   // 2/3 are the dynamic ones; 4 never moves
                    {
                        SliderF("velocity sens##csx",   &g_chCsgoVelSens, 0.f, 2.f, "%.2f");
                        SliderF("velocity smooth##csx", &g_chCsgoSmooth,  0.f, 0.5f, "%.2f s");
                    }
                    if (g_chCsgoStyle == 2)
                    {
                        SliderF("split dist##csx",  &g_csXSplitDist,  0.f, 25.f, "%.1f");
                        SliderF("split ratio##csx", &g_csXSplitRatio, 0.f, 1.f,  "%.2f");
                        SliderF("split alpha in##csx",  &g_csXInnerMod, 0.f, 1.f, "%.2f");
                        SliderF("split alpha out##csx", &g_csXOuterMod, 0.f, 1.f, "%.2f");
                    }
                }
            }, ImVec2(220.f, -1));
            ImGui::SameLine();
            ImGui::ColorRow("##chcsgocol", g_chCsgoColor, 0, 1);
        }

        ImGui::Checkbox("csgo scope", &g_scCsgo);
        if (g_scCsgo)
            ImGui::OptionPopup("csgo scope settings", [&]() {
                ImGui::Combo("weapon##scweapon", &g_scCsgoWeapon, "awp\0" "ssg 08\0" "scar-20 / g3sg1\0");
                ImGui::Combo("zoom##sczoom", &g_scCsgoZoom, "zoom level 1\0" "zoom level 2\0");
                SliderI("line width##sc", &g_scCsgoWidth, 1, 10);
                ImGui::Checkbox("show normal inaccuracy##sc", &g_scCsgoNormalInacc);
                SliderF("opacity##sc", &g_scCsgoOpacity, 0.f, 1.f, "%.2f");
                SliderF("scale##sc", &g_scCsgoScale, 0.25f, 2.f, "%.2f");
                ImGui::Checkbox("dust lens##sc", &g_scCsgoLens);
                ImGui::Checkbox("hide your dot##sc", &g_scCsgoNoDot);
                SliderF("move blur##sc", &g_scCsgoMoveSens, 0.f, 2.f, "%.2f");
                SliderF("air blur##sc", &g_scCsgoAirSens, 0.f, 2.f, "%.2f");
                ImGui::Checkbox("scope-in blur##sc", &g_scCsgoScopeIn);
                ImGui::Checkbox("sway##sc", &g_scCsgoBob);
                if (g_scCsgoBob)
                {
                    SliderF("sway scale##sc", &g_scCsgoBobScale, 0.f, 10.f, "%.2f");
                    SliderF("sway sideways##sc", &g_scCsgoBobLat, 0.f, 5.f, "%.2f");
                    SliderF("sway up/down##sc", &g_scCsgoBobVert, 0.f, 5.f, "%.2f");
                }
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("hide scope charge meter", &g_scChargeHide);
        ImGui::Checkbox("static amby crosshair", &g_staticAmbyCrosshair);
    }
    menu_group_end();
    menu_columns_end();
}

static const char* kVisualsSub[3] = { "esp", "world", "screen" };
static int s_visualsSub = 0;
static void Tab_VisualsRoot()
{
    if      (s_visualsSub == 0) Page_VisualsEsp();
    else if (s_visualsSub == 1) Page_VisualsWorld();
    else                        Page_VisualsScreen();
}

// rs::MoveBit mask as a botox MultiCombo. MultiCombo is bit i = item i, so the drawn rows are the
// bits in `allowedBits`, in MoveBit order, and only those bits are rewritten. Never all off: a
// route that may press nothing solves nothing and blames the markers
static bool RcMoveMaskCombo(const char* id, int& mask, unsigned allowedBits = 0xFFFFFFFFu)
{
    const char* names[32]; int bits[32]; int n = 0;
    for (int b = 0; b < RC_MoveCount() && n < 32; b++)
        if (allowedBits & (1u << b)) { names[n] = RC_MoveName(b); bits[n++] = b; }
    int flags = 0;
    for (int i = 0; i < n; i++) if (mask & (1 << bits[i])) flags |= 1 << i;
    if (!ImGui::MultiCombo(id, &flags, names, n)) return false;
    int drawn = 0, built = 0;
    for (int i = 0; i < n; i++)
    {
        drawn |= 1 << bits[i];
        if (flags & (1 << i)) built |= 1 << bits[i];
    }
    if (!built) return false;
    mask = (mask & ~drawn) | built;
    return true;
}

static void Page_MovementMain()
{
    menu_columns_begin();

    if (menu_group_begin("movement"))
    {
        ImGui::Checkbox("bunnyhop", &g_bhop);

        ImGui::Checkbox("edge jump", &g_edgeJump);
        if (g_edgeJump)
            ImGui::OptionPopup("edge jump settings", [&]() {
                ImGui::Label("edge jump keybind");
                ImGui::Keybind("edge jump key", &g_edgeJumpKey, &g_edgeJumpMode);
            }, ImVec2(200.f, -1));

        ImGui::Checkbox("long jump", &g_longJump);
        if (g_longJump)
            ImGui::OptionPopup("long jump settings", [&]() {
                ImGui::Label("long jump keybind");
                ImGui::Keybind("long jump key", &g_longJumpKey, &g_longJumpMode);
                ImGui::Checkbox("edgejump on lj", &g_longJumpEdge);
                ImGui::Checkbox("adaptive key cancelling", &g_longJumpAdaptive);
                if (g_longJumpAdaptive)
                {
                    ImGui::Checkbox("stop: let go##ljadlg", &g_longJumpAdaptiveLetGo);
                    ImGui::Checkbox("stop: landed##ljadld", &g_longJumpAdaptiveLanded);
                    ImGui::Checkbox("stop: time passes##ljadtm", &g_longJumpAdaptiveTimed);
                    if (g_longJumpAdaptiveTimed)
                        ImGui::SliderFloat("time after jump##ljadtime", &g_longJumpAdaptiveTime, 0.02f, 1.f, "%.2f s");
                }
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("mini jump", &g_miniJump);
        if (g_miniJump)
            ImGui::OptionPopup("mini jump settings", [&]() {
                ImGui::Label("mini jump keybind");
                ImGui::Keybind("mini jump key", &g_miniJumpKey, &g_miniJumpMode);
                ImGui::Checkbox("hold duck", &g_miniJumpHoldDuck);
                ImGui::Checkbox("edgejump on mj", &g_miniJumpEdge);
            }, ImVec2(200.f, -1));

        ImGui::Checkbox("jumpbug", &g_jumpbug);
        if (g_jumpbug)
            ImGui::OptionPopup("jumpbug settings", [&]() {
                ImGui::Label("jumpbug keybind");
                ImGui::Keybind("jumpbug key", &g_jumpbugKey, &g_jumpbugMode);
                ImGui::Checkbox("predict the hit##jb", &g_jbPredict);
                ImGui::Checkbox("yield to pixel surf##jb", &g_jbYieldSurf);
                ImGui::Checkbox("kangaroo", &g_jumpbugKangaroo);
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("edge bug", &g_edgeBug);
        if (g_edgeBug)
            ImGui::OptionPopup("edgebug advanced settings", [&]() {
                ImGui::Label("edgebug keybind");
                ImGui::Keybind("edge bug key", &g_edgeBugKey, &g_edgeBugMode);
                ImGui::Combo("style##ebstyle", &g_edgeBugStyle, "botox\0delusional\0dna\0");
                const int style = g_edgeBugStyle;
                if (style == 1)
                {
                    ImGui::Checkbox("mouse fix##ebdelmousefix", &g_edgeBugDelMouseFix);
                    SliderI("ticks to predict##ebdelticks", &g_edgeBugDelTicks, 0, 128);
                    ImGui::Checkbox("advanced detection##ebdeladv", &g_edgeBugDelAdvanced);
                    if (g_edgeBugDelAdvanced)
                    {
                        SliderF("angle limit##ebdelangle", &g_edgeBugDelAngleLimit, 0.f, 180.f, "%.2f");
                        if (g_edgeBugDelAngleLimit > 0.f)
                        {
                            SliderI("search amount##ebdelsearch", &g_edgeBugDelSearch, 1, 15);
                            ImGui::Checkbox("silent edgebug##ebdelsilent", &g_edgeBugDelSilent);
                        }
                    }
                }
                else if (style == 2)
                {
                    SliderF("scan time##ebdnascan", &g_edgeBugDnaScan, 0.f, 1.f, "%.2f s");
                    ImGui::Checkbox("advanced mode##ebdnaadv", &g_edgeBugDnaAdvanced);
                    if (g_edgeBugDnaAdvanced)
                    {
                        SliderI("advanced range##ebdnarange", &g_edgeBugDnaRange, 1, 10);
                        ImGui::Checkbox("strafe custom angle##ebdnacustom", &g_edgeBugDnaCustomAngle);
                        SliderF("angle limit##ebdnaangle", &g_edgeBugDnaAngleLimit, 0.f, 45.f, "%.1f");
                    }
                }
                if (style == 0)
                {
                    SliderF("edge bug predict time", &g_edgeBugPredictTime, 0.02f, 2.f, "%.2f s");
                    ImGui::Checkbox("autostrafe to edge", &g_edgeBugToEdge);
                    if (g_edgeBugToEdge)
                        SliderF("max path bend##ebstrafemax", &g_edgeBugStrafeAngleMax, 0.f, 180.f, "%.0f deg");
                    ImGui::Checkbox("mouse steer##ebsteer", &g_edgeBugMouseSteer);
                    if (g_edgeBugMouseSteer)
                        SliderI("paths##ebpaths", &g_edgeBugPaths, 1, 4);
                    // TF2: a ducked bug that lands still ducked eats the next jump
                    ImGui::Combo("stance##ebstance", &g_edgeBugStance,
                                 "prefer standing\0" "ducked only\0" "standing only\0");
                }
                ImGui::Checkbox("lock mouse on find", &g_edgeBugMouseLock);
                if (g_edgeBugMouseLock)
                {
                    ImGui::Combo("lock type##eblocktype", &g_edgeBugLockType,
                                 style == 0 ? "smooth\0full\0" : "static\0full\0");
                    SliderF("lock strength##eblockstr", &g_edgeBugLockStrength, 0.f, 100.f, "%.0f%%");
                }
                ImGui::Checkbox("sound on hit##ebsound", &g_edgeBugSound);
                if (g_edgeBugSound)
                    ImGui::Combo("sound##ebsoundid", &g_edgeBugSoundId, "crit hit\0mini crit\0double donk\0achievement\0upgrade\0");
                if (style == 0)
                    ImGui::Combo("detection mode##ebdetectmode", &g_edgeBugDetectMode, "normal\0enhanced\0ebanat\0");
                ImGui::Checkbox("visualize##ebviz", &g_edgeBugViz);
                if (g_edgeBugViz)
                    SliderF("pad size##ebvizsize", &g_edgeBugVizSize, 2.f, 64.f, "%.0f");
            }, ImVec2(200.f, -1));

        ImGui::Checkbox("edge skip", &g_edgeSkip);
        if (g_edgeSkip)
            ImGui::OptionPopup("edge skip settings", [&]() {
                ImGui::Label("edge skip keybind");
                ImGui::Keybind("edge skip key", &g_edgeSkipKey, &g_edgeSkipMode);
                SliderI("search window (ticks)", &g_edgeSkipTicks, 2, 132);
                SliderI("search depth", &g_edgeSkipDepth, 1, 4);
                SliderF("max bend", &g_edgeSkipMaxBend, 0.f, 180.f, "%.0f deg (180 = off)");
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("auto strafe", &g_autoStrafe);
        if (g_autoStrafe)
            ImGui::OptionPopup("auto strafe settings", [&]() {
                ImGui::Label("auto strafe keybind");
                ImGui::Keybind("auto strafe key", &g_autoStrafeKey, &g_autoStrafeMode);
                ImGui::Checkbox("avoid wall collisions", &g_autoStrafeAvoidWalls);
                if (g_autoStrafeAvoidWalls)
                    SliderF("avoid distance", &g_autoStrafeAvoidDist, 0.f, 200.f, "%.0f units");
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("strafe optimizer", &g_strafeOpt);
        if (g_strafeOpt)
            ImGui::OptionPopup("strafe optimizer settings", [&]() {
                SliderF("gain", &g_strafeOptGain, 0.f, 100.f, "%.0f%%");
                SliderF("min speed", &g_strafeOptMinSpeed, 0.f, 320.f, "%.0f");
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("auto align", &g_autoAlign);

        ImGui::Checkbox("pixel surf", &g_pixelSurf);
        if (g_pixelSurf)
            ImGui::OptionPopup("pixel surf settings", [&]() {
                ImGui::Label("pixel surf keybind");
                ImGui::Keybind("pixel surf key", &g_pixelSurfKey, &g_pixelSurfMode);
                // route calc's pixel surf horizon only
                SliderI("route predict ticks", &g_pixelSurfPredictTicks, 1, 32);
                ImGui::Combo("wall strafe gain##ps", &g_psWallStrafe,
                             "off\0" "when pressing into wall\0" "always\0");
                ImGui::Checkbox("park at end of surf##ps", &g_psParkAtEnd);
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("tung surf", &g_tungSurf);
        if (g_tungSurf)
            ImGui::OptionPopup("tung surf settings", [&]() {
                ImGui::Label("tung surf keybind");
                ImGui::Keybind("tung surf key", &g_tungSurfKey, &g_tungSurfMode);
                SliderF("max surface tilt", &g_tungMaxNz, 0.05f, 0.5f, "%.2f nz");
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("auto bounce", &g_autoBounce);
        if (g_autoBounce)
            ImGui::OptionPopup("auto bounce settings", [&]() {
                ImGui::Label("auto bounce keybind");
                ImGui::Keybind("auto bounce key", &g_autoBounceKey, &g_autoBounceMode);
                SliderF("bounce reach", &g_bounceReach, 8.f, 128.f, "%.0f units");
                SliderF("bounce min speed", &g_bounceMinSpeed, 0.f, 600.f, "%.0f u/s");
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("bounce assist", &g_bounceAssist);
        if (g_bounceAssist)
            ImGui::OptionPopup("bounce assist settings", [&]() {
                ImGui::Label("bounce assist keybind");
                ImGui::Keybind("bounce assist key", &g_bounceAssistKey, &g_bounceAssistMode);
                SliderF("bounce reach##ba", &g_bounceReach, 8.f, 128.f, "%.0f units");
                SliderF("bounce min speed##ba", &g_bounceMinSpeed, 0.f, 600.f, "%.0f u/s");
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("air stuck", &g_airStuck);
        if (g_airStuck)
            ImGui::OptionPopup("air stuck settings", [&]() {
                ImGui::Label("air stuck keybind");
                ImGui::Keybind("air stuck key", &g_airStuckKey, &g_airStuckMode);
                SliderF("extra reach##as", &g_asExtraReach, 0.f, 32.f, "%.0f");
                SliderF("search budget##as", &g_asBudget, 10.f, 90.f, "%.0f%% tick");
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("texture bug", &g_textureBug);
        if (g_textureBug)
            // also drives head surf + head bounce, which have no toggle of their own
            ImGui::OptionPopup("texture bug settings", [&]() {
                ImGui::Label("texture bug keybind");
                ImGui::Keybind("texture bug key", &g_textureBugKey, &g_textureBugMode);
                ImGui::Combo("style##tbstyle", &g_tbStyle, "cumidere\0botox\0");
                if (g_tbStyle == 0)
                {
                    ImGui::Checkbox("ride assist##tbcalign", &g_tbcAlign);   // cumidere misc_auto_align
                    ImGui::Checkbox("no park on off-grid walls##tbc", &g_tbNoOffgridStick);
                    return;
                }
                SliderF("reach", &g_tbReach, 1.f, 48.f, "%.0f");
                SliderF("hug range", &g_tbHugRange, 0.05f, 4.f, "%.2f");
                ImGui::Checkbox("predict ledges", &g_tbPredict);
                if (g_tbPredict)
                {
                    SliderI("predict ticks", &g_tbPredictTicks, 8, 128);
                    SliderF("predict reach", &g_tbPredictReach, 1.f, 96.f, "%.0f");
                }
                SliderF("scan step", &g_tbScanStep, 1.f, 8.f, "%.0f");
                SliderF("ramp nz (hands off)", &g_tbRampNz, 0.02f, 0.25f, "%.2f");
                ImGui::Checkbox("exact solve pass", &g_tbSolveForward);
                ImGui::Checkbox("no off-grid stick", &g_tbNoOffgridStick);
                ImGui::Checkbox("park at end of surf##tb", &g_tbParkAtEnd);
                if (g_tbParkAtEnd)
                {
                    SliderI("park ticks (0 = forever)", &g_tbParkTicks, 0, 256);
                    SliderF("park speed cap", &g_tbParkSpeed, 5.f, 250.f, "%.0f");
                }
                ImGui::Checkbox("brake into the park", &g_tbParkBrake);
                if (g_tbParkBrake)
                    SliderF("brake lead", &g_tbParkLead, 0.f, 128.f, "%.0f");
            }, ImVec2(260.f, -1));

        ImGui::Checkbox("wall climb", &g_wallClimb);
        if (g_wallClimb)
            ImGui::OptionPopup("wall climb settings", [&]() {
                ImGui::Label("wall climb keybind");
                ImGui::Keybind("wall climb key", &g_wallClimbKey, &g_wallClimbMode);
            }, ImVec2(200.f, -1));

        ImGui::Checkbox("blockbot", &g_blockBot);
        if (g_blockBot)
            ImGui::OptionPopup("blockbot settings", [&]() {
                ImGui::Label("blockbot keybind");
                ImGui::Keybind("blockbot key", &g_blockBotKey, &g_blockBotMode);
            }, ImVec2(200.f, -1));

        ImGui::Checkbox("fast stop", &g_fastStop);
        ImGui::Checkbox("fast accelerate", &g_fastAccel);
        ImGui::Checkbox("no push", &g_noPush);

        // UNIT_MENU_SLOT movement_main
        UnitA_Menu_movement_main();
    }
    menu_group_end();

    menu_columns_next();

    if (menu_group_begin("fake movement"))
    {
        ImGui::Checkbox("enabled", &g_fmEnabled);
        ImGui::BeginDisabled(!g_fmEnabled);

        ImGui::Checkbox("surf on any wall", &g_fmWallSurf);
        if (g_fmWallSurf)
            ImGui::OptionPopup("surf on any wall settings", [&]() {
                ImGui::Label("surf on any wall keybind");
                ImGui::Keybind("fake wall surf key", &g_fmWallSurfKey, &g_fmWallSurfMode);
                SliderF("wall reach##fmws", &g_fmWallReach, 1.f, 32.f, "%.0f");
                ImGui::Checkbox("seams only##fmws", &g_fmWallSurfSeam);
                if (g_fmWallSurfSeam)
                    SliderF("seam slack##fmws", &g_fmSeamSlack, 0.f, 16.f, "%.0f");
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("jump off walls", &g_fmWallJump);
        if (g_fmWallJump)
            ImGui::OptionPopup("jump off walls settings", [&]() {
                ImGui::Label("jump off walls keybind");
                ImGui::Keybind("fake wall jump key", &g_fmWallJumpKey, &g_fmWallJumpMode);
                SliderF("wall reach##fmwj", &g_fmWallReach, 1.f, 32.f, "%.0f");
                SliderF("push##fmwj", &g_fmWallJumpPush, 0.f, 1000.f, "%.0f");
                ImGui::Checkbox("jump while held##fmwj", &g_fmWallJumpHold);
                ImGui::Checkbox("seams only##fmwj", &g_fmWallJumpSeam);
                if (g_fmWallJumpSeam)
                    SliderF("seam slack##fmwj", &g_fmSeamSlack, 0.f, 16.f, "%.0f");
            }, ImVec2(220.f, -1));

        ImGui::Checkbox("higher jumps", &g_fmHighJump);
        if (g_fmHighJump)
            ImGui::OptionPopup("higher jumps settings", [&]() {
                ImGui::Label("higher jumps keybind");
                ImGui::Keybind("fake high jump key", &g_fmHighJumpKey, &g_fmHighJumpMode);
                SliderF("multiplier##fmhj", &g_fmHighJumpMult, 1.f, 4.f, "%.2fx");
            }, ImVec2(200.f, -1));

        ImGui::Checkbox("air stuck##fm", &g_fmAirStuck);
        if (g_fmAirStuck)
            ImGui::OptionPopup("fake air stuck settings", [&]() {
                ImGui::Label("air stuck keybind");
                ImGui::Keybind("fake air stuck key", &g_fmAirStuckKey, &g_fmAirStuckMode);
                ImGui::Checkbox("keep momentum##fmas", &g_fmAirStuckKeepVel);
                ImGui::Checkbox("only near brushes/props##fmas", &g_fmAirStuckNearOnly);
                if (g_fmAirStuckNearOnly)
                    SliderF("distance##fmas", &g_fmAirStuckNearDist, 0.f, 64.f, "%.0f");
            }, ImVec2(240.f, -1));

        ImGui::Checkbox("no fall damage", &g_fmNoFall);
        if (g_fmNoFall)
            ImGui::OptionPopup("no fall damage settings", [&]() {
                ImGui::Label("no fall damage keybind");
                ImGui::Keybind("fake no fall key", &g_fmNoFallKey, &g_fmNoFallMode);
            }, ImVec2(200.f, -1));

        ImGui::Checkbox("wallbug fix (sv_wallbug 1)", &g_fmWallbugFix);

        ImGui::Checkbox("crouch jump", &g_fmCrouchJump);
        if (g_fmCrouchJump)
            ImGui::OptionPopup("crouch jump settings", [&]() {
                ImGui::Label("crouch jump keybind");
                ImGui::Keybind("fake crouch jump key", &g_fmCrouchJumpKey, &g_fmCrouchJumpMode);
            }, ImVec2(200.f, -1));

        ImGui::Checkbox("ramp boost", &g_fmRampBoost);
        if (g_fmRampBoost)
            ImGui::OptionPopup("ramp boost settings", [&]() {
                ImGui::Label("ramp boost keybind");
                ImGui::Keybind("fake ramp boost key", &g_fmRampBoostKey, &g_fmRampBoostMode);
                SliderF("multiplier##fmrb", &g_fmRampBoostMult, 1.f, 4.f, "%.2fx");
            }, ImVec2(200.f, -1));

        ImGui::Checkbox("head surf", &g_fmHeadSurf);
        if (g_fmHeadSurf)
            ImGui::OptionPopup("head surf settings", [&]() {
                ImGui::Label("head surf keybind");
                ImGui::Keybind("fake head surf key", &g_fmHeadSurfKey, &g_fmHeadSurfMode);
                SliderF("snap up##fmhs", &g_fmHeadSurfSnap, 0.f, 64.f, "%.0f");
                ImGui::Checkbox("ledges only##fmhs", &g_fmHeadSurfLedgeOnly);
                ImGui::Checkbox("brush bottoms##fmhs", &g_fmHeadSurfBottoms);
                if (g_fmHeadSurfBottoms)
                    SliderF("wall reach##fmhs", &g_fmWallReach, 1.f, 32.f, "%.0f");
            }, ImVec2(240.f, -1));

        ImGui::EndDisabled();
    }
    menu_group_end();

    if (menu_group_begin("practice"))
    {
        ImGui::Checkbox("infinite ammo", &g_prInfAmmo);
        ImGui::Checkbox("infinite health", &g_prInfHealth);
        ImGui::Checkbox("infinite map time", &g_prInfTime);

        ImGui::Label("noclip");
        BindKeyNoMode("##prnoclip", &g_prNoclipKey);
        ImGui::Label("save position");
        BindKeyNoMode("##prsave", &g_prSaveKey);
        ImGui::Label("teleport to saved");
        BindKeyNoMode("##prtp", &g_prTpKey);
    }
    menu_group_end();

    if (menu_group_begin("build at crosshair"))
    {
        ImGui::Combo("class", &g_prBotClass,
                     "scout\0" "sniper\0" "soldier\0" "demoman\0" "medic\0"
                     "heavy\0" "pyro\0" "spy\0" "engineer\0");
        ImGui::Label("enemy player");
        BindKeyNoMode("##prbotenemy", &g_prBotEnemyKey);
        ImGui::Label("friendly player");
        BindKeyNoMode("##prbotmate", &g_prBotMateKey);
        ImGui::Label("sentry");
        BindKeyNoMode("##prsentry", &g_prSentryKey);
        ImGui::Label("dispenser");
        BindKeyNoMode("##prdisp", &g_prDispenserKey);
        ImGui::Label("teleporter");
        BindKeyNoMode("##prtele", &g_prTeleporterKey);
    }
    menu_group_end();

    // pixel finder / assist / pixel calculator: botox keeps them on the main page ("other"), the
    // calculator page is the route calculator alone
    if (menu_group_begin("pixel surf"))
    {
        ImGui::Checkbox("ps finder", &g_psFinder);
        if (g_psFinder)
            ImGui::OptionPopup("ps finder settings", [&]() {
                ImGui::Label("ps finder keybind");
                ImGui::Keybind("ps finder key", &g_psFinderKey, &g_psFinderMode);
                SliderF("scan margin", &g_psFinderMargin, 0.f, 64.f, "%.0f");
                SliderF("scan budget", &g_psFinderBudget, 0.25f, 32.f, "%.2f ms");
                ImGui::Checkbox("strict##pf", &g_psFinderStrict);
                ImGui::Checkbox("find texturebugs##pf", &g_psFinderTb);
                ImGui::Checkbox("find headbounces##pf", &g_psFinderHeadbounce);
                ImGui::Checkbox("find pixeljumps##pf", &g_psFinderPixeljump);
                ImGui::Checkbox("ignore props##pf", &g_psFinderNoProps);
                ImGui::Checkbox("surf fix (hold duck into a ride)##pf", &g_psSurfFix);

                // whole-map scan: the same scanner, pointed at every brush edge in the map
                PsMapScanState ms;
                PS_MapScanGet(ms);
                char pmLabel[64];
                if (ms.busy) snprintf(pmLabel, sizeof(pmLabel), "cancel scan  %d%%", int(ms.progress * 100.f));
                else         snprintf(pmLabel, sizeof(pmLabel), "search whole map");
                if (ImGui::Button(pmLabel, ImVec2(-1.f, 0.f)))
                    PS_MapScanToggle();
                ImGui::Checkbox("show map lines##pm", &g_psMapLines);
                if (ms.busy || ms.lines > 0 || ms.tb > 0)
                {
                    char pmInfo[96];
                    snprintf(pmInfo, sizeof(pmInfo), "seam %d / %d   %d surf%s   %d texturebug%s",
                             ms.done, ms.total, ms.lines, ms.lines == 1 ? "" : "s",
                             ms.tb, ms.tb == 1 ? "" : "s");
                    ImGui::TextUnformatted(pmInfo);
                }
            }, ImVec2(260.f, -1));

        // the point list is shared by assist and finder
        ImGui::Checkbox("ps assist", &g_psAssist);
        if (g_psAssist || g_psFinder)
            ImGui::OptionPopup("ps assist settings", [&]() {
                ImGui::Label("ps assist keybind");
                ImGui::Keybind("ps assist key", &g_psAssistKey, &g_psAssistMode);
                ImGui::Label("set point");
                BindKeyNoMode("##pasetbind", &g_psSetPointKey);
                ImGui::Label("delete point");
                BindKeyNoMode("##padelbind", &g_psDeletePointKey);
                SliderF("radius", &g_psAssistRadius, 40.f, 1024.f, "%.0f");
                SliderI("points at once", &g_psAssistMaxPoints, 1, 8);
                SliderF("reuse delay", &g_psAssistReuseDelay, 0.f, 8.f, "%.1f s");
                SliderF("snap distance", &g_psAssistSnap, 4.f, 256.f, "%.0f");
                ImGui::Checkbox("ground help", &g_psGroundHelp);
                if (g_psGroundHelp)
                    SliderF("steer limit", &g_psGroundHelpAngle, 5.f, 90.f, "%.0f deg");
                SliderF("launch z slack", &g_psLaunchZSlack, 0.f, 32.f, "%.0f");
                ImGui::Checkbox("show points", &g_psAssistRender);
                ImGui::Checkbox("jump popup", &g_psAssistJumpBox);
            }, ImVec2(240.f, -1));

        ImGui::Checkbox("pixel calculator", &g_pixelCalc);
        if (g_pixelCalc)
            ImGui::OptionPopup("pixel calculator settings", [&]() {
                ImGui::Label("place pixel");
                BindKeyNoMode("##pcplacebind", &g_pcPlaceKey);
                ImGui::Label("calculate pixel");
                BindKeyNoMode("##pcsolvebind", &g_pcSolveKey);
                ImGui::Checkbox("show marker", &g_pcShowMarker);
                ImGui::Checkbox("show answer panel", &g_pcShowPanel);
                ImGui::Label("jump types (shared with route calculator)");
                RcMoveMaskCombo("##pcmoves", g_rcMoveMask);
            }, ImVec2(260.f, -1));

        // UNIT_MENU_SLOT movement_calculator
        UnitB_Menu_movement_calculator();
    }
    menu_group_end();

    menu_columns_end();
}

static void Page_MovementIndicators()
{
    menu_columns_begin();

    if (menu_group_begin("movement indicator"))
    {
        ImGui::Checkbox("enabled##mi", &g_miEnable);
        if (g_miEnable)
            ImGui::OptionPopup("movement indicator settings", [&]() {
                ImGui::Checkbox("horizontal##mi", &g_miHorizontal);
                ImGui::Checkbox("mitosis##mi", &g_miMitosis);

                // displayed binds: preview lists the tags that are on, rows toggle without closing
                int cnt = 0, only = -1;
                for (int i = 0; i < MI_COUNT; ++i)
                    if (g_miShowMask & (1 << i)) { ++cnt; only = i; }

                char preview[48];
                if (cnt == 0)             strcpy(preview, "none");
                else if (cnt == MI_COUNT) strcpy(preview, "all binds");
                else if (cnt == 1)        _snprintf(preview, sizeof(preview), "%s", kMiNames[only]);
                else                      _snprintf(preview, sizeof(preview), "%d binds", cnt);

                if (ImGui::BeginCombo("displayed binds##mishow", preview))
                {
                    for (int i = 0; i < MI_COUNT; ++i)
                    {
                        char row[40];
                        _snprintf(row, sizeof(row), "%s (%s)", kMiNames[i], kMiTags[i]);
                        const bool on = (g_miShowMask & (1 << i)) != 0;
                        if (ImGui::Selectable(row, on, ImGuiSelectableFlags_DontClosePopups))
                            g_miShowMask ^= (1 << i);
                    }
                    ImGui::EndCombo();
                }

                SliderF("height##mi", &g_miOffsetY, 0.f, 600.f, "%.0f");
                SliderF("scale##mi", &g_miScale, 0.5f, 2.5f, "%.2f");
                ImGui::ColorRow("idle##miidle", g_miIdleColor);
                ImGui::ColorRow("active##miact", g_miActiveColor);
            }, ImVec2(240.f, -1));

        // UNIT_MENU_SLOT movement_indicators
        UnitB_Menu_movement_indicators();
    }
    menu_group_end();

    menu_columns_next();

    if (menu_group_begin("velocity indicator"))
    {
        ImGui::Checkbox("enabled##vi", &g_viEnable);
        if (g_viEnable)
            ImGui::OptionPopup("velocity indicator settings", [&]() {
                ImGui::Checkbox("custom color##vi", &g_viCustomColor);
                // swatch order reads left to right: slot 0 is rightmost
                if (g_viCustomColor)
                {
                    ImGui::ColorRow("velocity color 1##vic1", g_viColor1, 0, 1);
                    ImGui::ColorRow("velocity color 2##vic2", g_viColor2, 0, 0);
                }
                else
                {
                    ImGui::ColorRow("losing speed##vic3", g_viColor3, 0, 2);
                    ImGui::ColorRow("same speed##vic4", g_viColor4, 0, 1);
                    ImGui::ColorRow("gaining speed##vic5", g_viColor5, 0, 0);
                }
                ImGui::Checkbox("show pre speed##vi", &g_viPreSpeed);
                ImGui::Checkbox("fade alpha##vi", &g_viFadeAlpha);
                SliderF("position##vi", &g_viOffsetY, 30.f, ImGui::GetIO().DisplaySize.y, "%.0f");
            }, ImVec2(240.f, -1));
    }
    menu_group_end();

    menu_columns_end();
}

static void Page_MovementCalculator()
{
    // botox menu_tab_movement.cpp route_calc_page 1:1: "route calculator" + "keys" | "points".
    // TF2 has no ducked ground jump (tf_gamemovement.cpp:1220 "Cannot jump while ducked"), so the
    // point list has no "jump (ducked)" row; pixeljump / headbounce point types are not modelled
    menu_columns_begin();

    if (menu_group_begin("route calculator"))
    {
        ImGui::Checkbox("route calculator", &g_routeCalc);

        if (ImGui::Checkbox("use current pos for start jump", &g_rcStartHere))
            RC_DropAnswer();
        if (ImGui::Checkbox("allow selecting invalid pixels", &g_rcAllowInvalid))
            RC_DropAnswer();

        ImGui::Checkbox("snap points to nearest edge", &g_rcSnapEdge);
        ImGui::Checkbox("show key list", &g_rcShowKeys);
        ImGui::Checkbox("show path", &g_rcShowPath);
        if (g_rcShowPath)
        {
            SliderI("path alpha", &g_rcPathAlpha, 0, 255, "%d");
            SliderF("path thickness", &g_rcPathThick, 1.f, 8.f, "%.1f px");
        }
        ImGui::Checkbox("show combos on screen", &g_rcShowPanel);
        ImGui::Checkbox("advanced readout##route calc", &g_rcAdvanced);

        SliderI("popup duration", &g_rcPopupTime, 1, 30, "%d sec");
        SliderI("max displayed combos", &g_rcMaxShown, 1, 64, "%d combos");

        if (SliderI("delay hop ticks", &g_rcDelayTicks, 0, 8, "%d ticks"))
            RC_DropAnswer();

        ImGui::Label("jump types to calculate");
        if (RcMoveMaskCombo("##global jump types", g_rcMoveMask))
            RC_DropAnswer();
    }
    menu_group_end();

    if (menu_group_begin("keys"))
    {
        ImGui::Label("add point");
        BindKeyNoMode("##rcaddbind", &g_rcAddKey);
        ImGui::Label("calculate combos");
        BindKeyNoMode("##rcsolvebind", &g_rcSolveKey);
        ImGui::Label("delete point");
        BindKeyNoMode("##rcdelbind", &g_rcDeleteKey);
        ImGui::Label("clear all points");
        BindKeyNoMode("##rcclrbind", &g_rcClearKey);
    }
    menu_group_end();

    menu_columns_next();

    if (menu_group_begin("points"))
    {
        // row 0 = the starting jump, row i = marker i (botox route.selected_row)
        static int s_rcRow = 0;
        const int nPts = RC_PointCount();
        if (ImGui::BeginListBox("##route points", ImVec2(-1.f, 160.f)))
        {
            if (ImGui::Selectable(g_rcStartHere ? "starting jump (current position)" : "starting jump (point 1)", s_rcRow == 0))
                s_rcRow = 0;
            for (int i = 0; i < nPts; i++)
            {
                RcPointInfo pt;
                if (!RC_PointGet(i, pt)) break;
                char row[96];
                snprintf(row, sizeof(row), "%d. %s   z %d%s##rcrow%d", i + 1, RC_TypeName(pt.type), (int)pt.pos.z,
                         pt.enabled ? "" : "   (off)", i);
                if (ImGui::Selectable(row, s_rcRow == i + 1))
                    s_rcRow = i + 1;
            }
            ImGui::EndListBox();
        }

        if (ImGui::Button("clear all points", ImVec2(-1.f, 15.f)))
            RC_PointsClear();

        if (s_rcRow > RC_PointCount())
            s_rcRow = 0;

        if (s_rcRow == 0)
        {
            ImGui::Label("types to calculate");
            if (RcMoveMaskCombo("##types to calculate", g_rcStartMoves, RC_StartMoves()))
                RC_DropAnswer();
        }
        else
        {
            const int row = s_rcRow - 1;
            RcPointInfo edit;
            if (RC_PointGet(row, edit))
            {
                if (ImGui::Checkbox("point enabled", &edit.enabled))
                    RC_PointSet(row, edit);

                const int n = RC_PointCount();
                if (ImGui::Button("move up", ImVec2(-1.f, 15.f)) && row > 0)
                {
                    RC_PointMove(row, -1);
                    s_rcRow -= 1;
                }
                else if (ImGui::Button("move down", ImVec2(-1.f, 15.f)) && row + 1 < n)
                {
                    RC_PointMove(row, 1);
                    s_rcRow += 1;
                }
                else if (ImGui::Button("delete point", ImVec2(-1.f, 15.f)))
                {
                    RC_PointDelete(row);
                    if (s_rcRow > RC_PointCount()) s_rcRow = RC_PointCount();
                }
                else if (RC_PointGet(row, edit))
                {
                    bool changed = false;
                    // order = rs::PointType, so the combo index IS the type. RC_PointSet refuses
                    // pixelsurf / texturebug on a marker no finder pinned (botox: "a pixelsurf
                    // point must be placed on a finder dot"), unless invalid pixels are allowed
                    static const char* kTypeItems = "ground\0pixelsurf\0edgebug\0headbang\0texturebug\0";
                    const int was = edit.type;
                    if (ImGui::Combo("point type", &edit.type, kTypeItems) && edit.type != was)
                        changed = true;
                    if (!edit.snapped && !g_rcAllowInvalid)
                        ImGui::TextDisabled("pixelsurf / texturebug: place it on a finder dot");

                    ImGui::Label("types to calculate");
                    if (edit.type == 0)
                    {
                        int mask = (int)edit.moves;
                        if (RcMoveMaskCombo("##types to calculate", mask, RC_MarkerMoves()))
                        {
                            edit.moves = (unsigned)mask;
                            changed = true;
                        }
                    }
                    else
                    {
                        changed |= ImGui::Checkbox("arrive standing", &edit.stand);
                        changed |= ImGui::Checkbox("arrive ducked", &edit.duck);
                    }
                    if (changed) RC_PointSet(row, edit);
                }
            }
        }
    }
    menu_group_end();

    menu_columns_end();
}

static void Page_MovementRecorder()
{
    // UNIT_MENU_SLOT movement_recorder -- botox case 3: the unit fn owns its columns + groups
    UnitB_Menu_movement_recorder();
}

static const char* kMoveSub[4] = { "main", "indicators", "calculator", "recorder" };
static int s_moveSub = 0;
static void Tab_MovementRoot()
{
    if      (s_moveSub == 0) Page_MovementMain();
    else if (s_moveSub == 1) Page_MovementIndicators();
    else if (s_moveSub == 2) Page_MovementCalculator();
    else                     Page_MovementRecorder();
}

// ==== misc / fonts / settings helpers ===============================================
#include "fonts.h"
#include <functional>
#include <shellapi.h>   // ShellExecuteA -- settings "open folder" (WIN32_LEAN_AND_MEAN drops it)

// botox menu.h popup_fade_t: confirm popups fade on their own. live = drawn this frame,
// ghost = last live frame's geometry, drawn while it fades out after the popup closed.
// Stepped once per built frame by Menu_SplitPopups (render.cpp SplitDrawData).
struct PopupFade { ImGuiID id; float alpha; bool live; ImDrawList* ghost; };
static ImVector<PopupFade> s_popups;

// botox popup_live
static void PopupLive(ImGuiID window_id)
{
    for (PopupFade& p : s_popups)
        if (p.id == window_id) { p.live = true; return; }
    s_popups.push_back({ window_id, 0.f, true, nullptr });
}

// botox menu.cpp save_popup: opens once per request, a click away drops the request (never
// reopens at the mouse), body under the same 20px band OptionPopup draws.
static void SavePopup(const char* str_id, bool& open, const ImVec2& size, const std::function<void()>& fn)
{
    if (!open) return;
    const ImGuiID shown = ImGui::GetID(str_id);
    ImGuiStorage* st = ImGui::GetStateStorage();
    if (!st->GetBool(shown)) { ImGui::OpenPopup(str_id); st->SetBool(shown, true); }
    ImGui::SetNextWindowSize(size);
    if (!ImGui::BeginPopup(str_id)) { open = false; st->SetBool(shown, false); return; }
    PopupLive(ImGui::GetCurrentWindow()->ID);
    PopupChrome(str_id, nullptr, ImHashStr(str_id));
    fn();
    if (!open) { ImGui::CloseCurrentPopup(); st->SetBool(shown, false); }
    ImGui::EndPopup();
}

// IVEngineClient::ClientCmd_Unrestricted, slot 106 -- the call skins.cpp / practice.inl /
// screenfx.cpp already make. botox's convenience buttons call client_cmd_unrestricted the
// same way, straight from the click.
static void MenuClientCmd(const char* cmd)
{
    DebugLog("menu: convenience button '%s'", cmd);
    if (!g_engine) return;
    __try {
        void** vt = *reinterpret_cast<void***>(g_engine);
        if (reinterpret_cast<uintptr_t>(vt[106]) > 0x10000)
            ((void(__fastcall*)(void*, const char*))vt[106])(g_engine, cmd);
    } __except (EXCEPTION_EXECUTE_HANDLER) { DebugLog("menu: ClientCmd_Unrestricted faulted"); }
}

// botox tab_misc "convenience": one button per saved title/command, an "x" to drop it, then
// "add button" opening a "new button" popup. Slots instead of botox's one tab/newline string
// (config.cpp is one key=value per line).
static void ConvenienceButtons()
{
    static char s_title[32] = "", s_command[160] = "";
    static bool s_openAdd = false;
    int remove = -1;
    const ImVec2 xs = ImGui::CalcTextSize("x");
    for (int i = 0; i < kConvButtons; ++i)
    {
        if (!g_convTitle[i][0] || !g_convCmd[i][0]) continue;
        ImGui::PushID(i);
        if (ImGui::Button(g_convTitle[i], ImVec2(ImGui::GetContentRegionAvail().x - 7.f - xs.x, 15.f)))
            MenuClientCmd(g_convCmd[i]);
        ImGui::SameLine(0.f, 7.f);
        if (ImGui::InvisibleButton("##delete", ImVec2(xs.x, 15.f))) remove = i;
        const ImVec2 xm = ImGui::GetItemRectMin();
        ImGui::GetWindowDrawList()->AddText(ImVec2(xm.x, ImFloor(xm.y + (15.f - xs.y) * 0.5f)),
            ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled), "x");
        ImGui::PopID();
    }
    if (remove >= 0) { g_convTitle[remove][0] = '\0'; g_convCmd[remove][0] = '\0'; }

    if (ImGui::Button("add button", ImVec2(-1.f, 15.f))) s_openAdd = true;
    SavePopup("new button", s_openAdd, ImVec2(220.f, -1.f), []() {
        ImGui::InputText("title", s_title, sizeof(s_title));
        ImGui::InputText("command", s_command, sizeof(s_command));
        auto blank = [](const char* t) { while (*t == ' ') ++t; return *t == '\0'; };
        int slot = -1;
        for (int i = 0; i < kConvButtons && slot < 0; ++i)
            if (!g_convTitle[i][0] || !g_convCmd[i][0]) slot = i;
        ImGui::BeginDisabled(blank(s_title) || blank(s_command) || slot < 0);
        if (ImGui::Button("add button", ImVec2(-1.f, 15.f)))
        {
            strncpy_s(g_convTitle[slot], s_title, _TRUNCATE);
            strncpy_s(g_convCmd[slot], s_command, _TRUNCATE);
            s_title[0] = s_command[0] = '\0';
            s_openAdd = false;
        }
        ImGui::EndDisabled();
        if (slot < 0) ImGui::TextDisabled("all %d slots used", kConvButtons);
    });
}

// botox menu_tab_settings.cpp tab_misc, main page. Same three groups, same order, same
// popup titles/widths. Rows botox has that this DLL has no feature for are left out (list
// in the commit / report); TF2-only rows sit in the group they fit.
static void Page_MiscMain()
{
    menu_columns_begin();

    if (menu_group_begin("overlays"))
    {
        ImGui::Checkbox("watermark", &g_wmEnable);
        if (g_wmEnable)
            ImGui::OptionPopup("watermark configuration", []() {
                ImGui::Combo("style##watermark style", &g_wmStyle,
                    "default\0kamibebra\0clarity\0clarity v2\0delusional\0interwebz\0havoc\0onetap\0airflow\0"
                    "ev0lve\0legendware\0interium\0skebob\0cumidere\0illusory\0lumi\0billware\0dna\0"
                    "dna clarity\0cucumber\0howeweware\0gamesense\0");
                ImGui::InputText("name##watermark text", g_wmLabel, sizeof(g_wmLabel));
                ImGui::Checkbox("title##watermark title", &g_wmShowLabel);
                ImGui::Checkbox("fps", &g_wmShowFPS);
                ImGui::Checkbox("ping", &g_wmShowPing);
                ImGui::Checkbox("loss", &g_wmShowLoss);
                ImGui::Checkbox("time", &g_wmShowTime);
                ImGui::Combo("position##watermark pos", &g_wmPos,
                    "top left\0top center\0top right\0bottom left\0bottom center\0bottom right\0");
                ImGui::ColorRow("text color##watermark text color", g_wmTextColor);
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("spectator list", &g_specEnable);
        if (g_specEnable)
            ImGui::OptionPopup("spectators configuration", []() {
                ImGui::ColorRow("spectating local color##spectator list text color one", g_specColorOne);
                ImGui::ColorRow("spectating other color##spectator list text color two", g_specColorTwo);
                // botox's own index order (spectator_list.h) -- indices this DLL does not
                // implement yet fall back to the default body, same as index 0.
                ImGui::Combo("style##spectatorlist style", &g_specStyle,
                    "default\0delusional\0interwebz\0windows xp\0clarity\0chillware\0lobotomy\0kamibebra\0"
                    "eyes\0cumhacck\0clarity v2\0airplane\0bhop cheat\0millionware\0aimware\0havoc\0onetap\0"
                    "airflow\0ev0lve\0legendware\0interium\0chillware v2\0skebob\0cumidere\0illusory\0lumi\0"
                    "billware\0inkabanium\0dna\0cucumber\0howeweware\0gamesense\0");
                ImGui::Checkbox("show targets##spectatorlist targets", &g_specShowTarget);
                ImGui::Checkbox("always show##spectatorlist always show", &g_specAlwaysShow);
                if (g_specStyle == 1)
                    ImGui::Checkbox("left side##spectatorlist delusional left", &g_specDelusionalLeft);
                if (g_specStyle == 2)
                    ImGui::SliderInt("width##spectatorlist width", &g_specListWidth, 80, 300);
                ImGui::Combo("position##spectatorlist pos", &g_specPos,
                    "top left\0top center\0top right\0bottom left\0bottom center\0bottom right\0");
            }, ImVec2(200.f, -1.f));

        ImGui::Checkbox("media player", &g_mpEnable);
        if (g_mpEnable)
            ImGui::OptionPopup("media player configuration", []() {
                // botox menu_tab_settings.cpp media player popup (minus waveform / lyrics /
                // keybinds: no visualizer thread, no lyrics fetch, no keybind widget here).
                // Look + fonts follow the watermark style; prev/pause/next buttons show in
                // the panel while the menu is open, same as the donor.
                ImGui::Combo("position##media player position", &g_mpPos,
                    "top left\0top center\0top right\0bottom left\0bottom center\0bottom right\0");
                ImGui::Checkbox("simple", &g_mpSimple);
                if (!g_mpSimple) {
                    ImGui::Checkbox("cover art", &g_mpCoverArt);
                    ImGui::Checkbox("background", &g_mpBackground);
                    ImGui::Checkbox("progress bar", &g_mpProgressBar);
                }
                ImGui::Checkbox("precise time", &g_mpTime);
            }, ImVec2(200.f, -1.f));

        ImGui::Checkbox("points", &g_ptsEnable);

        ImGui::Checkbox("hit marker", &g_hmEnable);
        if (g_hmEnable)
            ImGui::OptionPopup("hit marker configuration", []() {
                ImGui::SliderFloat("size##hitmarkersize", &g_hmSize, 4.f, 30.f, "%.0f");
                ImGui::ColorRow("color##hitmarkercolor", g_hmColor);
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("death particles", &g_dpEnable);
        if (g_dpEnable)
            ImGui::OptionPopup("death particles settings", []() {
                const char* styles[] = { "blood", "sparks (->blood)", "confetti", "smoke",
                                          "explosion", "soul", "balls", "melt (->blood)", "minecraft" };
                ImGui::Combo("effect##deathparticles", &g_dpStyle, styles, 9);
                if (g_dpStyle == 8)
                {
                    const char* mc[] = { "crits only", "crits + xp", "xp only" };
                    ImGui::Combo("drops##deathparticles", &g_dpMcMode, mc, 3);
                }
                if (g_dpStyle == 5)
                {
                    const char* soul[] = { "fountain", "rise", "burst", "converge" };
                    ImGui::Combo("soul##deathparticles", &g_dpSoulStyle, soul, 4);
                }
                ImGui::Checkbox("multicolor##deathparticles", &g_dpMulticolor);
                if (!g_dpMulticolor)
                    ImGui::ColorRow("color##deathparticles", g_dpColor);
                ImGui::SliderInt("amount##deathparticles", &g_dpAmount, 1, 100);
                ImGui::SliderFloat("size##deathparticles", &g_dpSize, 0.25f, 4.f, "%.1fx");
                ImGui::SliderFloat("lifetime##deathparticles", &g_dpLifetime, 0.5f, 10.f, "%.1f s");
                if (g_dpStyle == 8 && g_dpMcMode != 0)
                    ImGui::SliderFloat("xp stays##deathparticles", &g_dpMcXpTime, 1.f, 60.f, "%.0f s");
            }, ImVec2(220.f, -1.f));

        // botox "console color" tints the console window (DrawSetColor hook); this DLL's knob
        // colours its own console prints -- same slot in the group.
        ImGui::ColorRow("console color", g_consoleColor);

        // TF2: killfeed filter (botox "displayed logs" slot -- what the feed shows)
        ImGui::Checkbox("only your killfeed", &g_selfKillfeedOnly);
    }
    menu_group_end();

    menu_columns_next();

    if (menu_group_begin("game"))
    {
        // TF2 rows first (botox's clantag / chat / bot names / revive / deagle rows are CS:GO
        // only), then botox's sv_pure bypass, performance, discord rpc, debugger menu.
        ImGui::Checkbox("f2p chat bypass", &g_f2pChatBypass);
        ImGui::Checkbox("reveal scoreboard", &g_revealScoreboard);
        ImGui::Checkbox("steam history menu item", &g_steamHistory);

        ImGui::Checkbox("region selector", &g_regionEnable);
        if (g_regionEnable)
            ImGui::OptionPopup("region selector settings", []() {
                ImGui::MultiCombo("regions##regions", &g_regionMask, kRegionNames, 29);
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("net settings", &g_netSettingsEnable);
        if (g_netSettingsEnable)
            ImGui::OptionPopup("net settings configuration", []() {
                ImGui::Combo("preset##netcfg", &g_netSettingsSel,
                    "klown\0" "murmur\0" "boobsy\0" "xcd\0" "0lerp\0" "15lerp\0" "30lerp\0" "spy\0"
                    "koymu\0" "lenny\0" "snappy\0");
                ImGui::Checkbox("also set rate / cmdrate / updaterate##nsrates", &g_netSettingsRates);
                char ns[128];
                NetSettings_Status(ns, sizeof(ns));
                if (ns[0]) ImGui::TextDisabled("%s", ns);
            }, ImVec2(240.f, -1.f));

        ImGui::Checkbox("net graph color", &g_ngColorEnable);
        if (g_ngColorEnable)
            ImGui::OptionPopup("net graph color settings", []() {
                ImGui::ColorRow("text##ngtext", g_ngTextColor);
                ImGui::ColorRow("warn orange##ngorange", g_ngOrangeColor);
                ImGui::ColorRow("warn red##ngred", g_ngRedColor);
                ImGui::ColorRow("warn yellow##ngyellow", g_ngYellowColor);
            }, ImVec2(200.f, -1.f));

        ImGui::Checkbox("net graph position", &g_ngPosEnable);
        if (g_ngPosEnable)
            ImGui::OptionPopup("net graph position settings", []() {
                // DragInt min==max = unbounded, so no slider end can pin the graph off screen.
                ImGui::DragInt("x##ngposx", &g_ngPosX, 1.f, 0, 0, "x  %d px");
                ImGui::DragInt("y##ngposy", &g_ngPosY, 1.f, 0, 0, "y  %d px");
                if (ImGui::Button("reset offsets", ImVec2(-1.f, 0.f))) { g_ngPosX = 0; g_ngPosY = 0; }
            }, ImVec2(200.f, -1.f));

        ImGui::Checkbox("equip region unlock", &g_equipRegionUnlock);

        ImGui::Checkbox("sv_pure bypass", &g_pureBypass);
        ImGui::Checkbox("sv_cheats bypass", &g_cheatsBypass);

        ImGui::Checkbox("performance", &g_perfEnable);
        if (g_perfEnable)
            ImGui::OptionPopup("performance configuration", []() {
                ImGui::Checkbox("1ms timer", &g_hiTimerRes);
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("discord rpc", &g_discordEnable);
        if (g_discordEnable)
            ImGui::OptionPopup("discord rpc configuration", []() {
                ImGui::InputText("application id##discord rpc", g_discordAppId, sizeof(g_discordAppId));
                ImGui::InputText("state##discord rpc", g_discordState, sizeof(g_discordState));
                ImGui::InputText("details##discord rpc", g_discordDetails, sizeof(g_discordDetails));
                ImGui::InputText("large image key##discord rpc", g_discordImageKey, sizeof(g_discordImageKey));
            }, ImVec2(200.f, -1.f));

        ImGui::Checkbox("debugger menu", &g_debugMenuEnable);
    }
    menu_group_end();

    if (menu_group_begin("convenience"))
    {
        // TF2: the matchmaker fixes are fixed convenience buttons, above the user's own.
        const float gap  = ImGui::GetStyle().ItemSpacing.x;
        const float half = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
        if (ImGui::Button("fix stuck matchmaker", ImVec2(half, 15.f)))
            MMFix_Run();
        ImGui::SameLine(0.f, gap);
        if (ImGui::Button("abandon match (GC)", ImVec2(-1.f, 15.f)))
            MMFix_Abandon();
        ImGui::TextDisabled("%s", g_mmFixReady ? g_mmFixStatus : "sigs not resolved");

        ConvenienceButtons();
    }
    menu_group_end();

    menu_columns_end();

    // UNIT_MENU_SLOT misc_main
    UnitE_Menu_misc_main();
}

static int s_miscSub = 0;
static void Tab_MiscRoot()
{
    Page_MiscMain();
}

// ==== skins =========================================================================
// Every widget here edits one SkinCfg in g_skinCfgs, keyed by the selected weapon's
// REDIRECT index. Anything that changes which item the weapon reports (warpaint def,
// reskin, festive) sets g_skinNeedsUpdate, which makes the FSN pass re-resolve the
// models and ask the server for a full update -- a material override and a style skin
// are only read off the item definition when the entity's data changes.

// Case-insensitive substring, for the two searchable combos below.
static bool SkinFilterMatch(const char* haystack, const char* needle)
{
    if (!needle || !needle[0]) return true;
    for (const char* h = haystack; *h; ++h)
    {
        const char* hh = h;
        const char* nn = needle;
        while (*nn && tolower((unsigned char)*hh) == tolower((unsigned char)*nn)) { ++hh; ++nn; }
        if (!*nn) return true;
    }
    return false;
}

// The warpaint combo has to be re-pointed whenever the weapon changes, because a
// weapon-specific skin is stored as a 15xxx def and the list is keyed by paint kit id.
static void SkinSyncWarpaintSel(const SkinCfg& cfg, int& sel)
{
    sel = 0;
    if (cfg.weaponSkinDef > 0) {
        int pkForDef = -1;
        for (int j = 0; j < k_skinDefCount; j++)
            if (k_skinDefs[j].skinDef == cfg.weaponSkinDef) { pkForDef = k_skinDefs[j].pkId; break; }
        if (pkForDef >= 0)
            for (int i = 1; i < k_warpaintCount; i++)
                if (k_warpaints[i].defindex == pkForDef) { sel = i; break; }
    } else if (cfg.paintKitId > 0) {
        for (int i = 0; i < k_warpaintCount; i++)
            if (k_warpaints[i].defindex == cfg.paintKitId) { sel = i; break; }
    }
}

static void Tab_Skins()
{
    static int  s_wpnSel     = 0;
    static int  s_lastWpn    = -1;
    static int  s_warpSel    = 0;
    static char s_wpFilter[64]   = {};
    static char s_partFilter[64] = {};

    // alphabetical display order, built once
    static int  s_wpnOrder[k_wpnCount];
    static bool s_wpnOrderInit = false;
    if (!s_wpnOrderInit) {
        for (int i = 0; i < k_wpnCount; i++) s_wpnOrder[i] = i;
        std::sort(s_wpnOrder, s_wpnOrder + k_wpnCount, [](int a, int b) {
            return _stricmp(k_wpnList[a].name, k_wpnList[b].name) < 0;
        });
        s_wpnOrderInit = true;
    }

    Section("skinchanger");
    ImGui::Checkbox("enabled", &g_enableSkins);
    if (ImGui::IsItemHovered())
        MenuTooltip("client-side only. rewrites the item definition and the runtime\n"
                    "attributes of the weapons in your own loadout, which is what your\n"
                    "own renderer reads. nobody else sees any of it.");

    ImGui::Spacing();
    ImGui::TextDisabled("weapon");
    if (ImGui::BeginCombo("##skwpn", k_wpnList[s_wpnSel].name))
    {
        for (int oi = 0; oi < k_wpnCount; oi++) {
            const int i = s_wpnOrder[oi];
            const bool sel = (s_wpnSel == i);
            if (ImGui::Selectable(k_wpnList[i].name, sel)) s_wpnSel = i;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // Bound AFTER the combo: a pick inside it changes s_wpnSel this frame, and the rest of
    // the tab (and the change check below) has to see the weapon that is now selected.
    const WpnEntry& we = k_wpnList[s_wpnSel];
    const int selRedir = we.redir;
    SkinCfg& cfg = g_skinCfgs[selRedir];
    // redir != stock means the stock def cannot take a paint kit and the upgradeable
    // twin has to be worn instead; canPaint means the stock def already accepts one.
    const bool hasPaint = we.canPaint || (we.stock != we.redir);

    if (s_lastWpn != s_wpnSel) {
        s_lastWpn = s_wpnSel;
        SkinSyncWarpaintSel(cfg, s_warpSel);
        s_wpFilter[0] = '\0';
    }

    ImGui::Checkbox("enabled for this weapon", &cfg.enabled);

    // ---- warpaint ------------------------------------------------------------------
    Section("warpaint");
    if (!hasPaint) ImGui::BeginDisabled();
    ImGui::TextDisabled("paint");
    if (ImGui::BeginCombo("##skwarp", k_warpaints[s_warpSel].name))
    {
        ImGui::InputText("##skwpfilter", s_wpFilter, sizeof(s_wpFilter));
        ImGui::Separator();
        for (int i = 0; i < k_warpaintCount; i++)
        {
            const int pk = k_warpaints[i].defindex;
            // 0 is "(none)". 1-86 are weapon-specific skins, shown only when THIS weapon
            // has a matching 15xxx def; 87+ are generic warpaints, shown only if paintable.
            if (i != 0) {
                if (pk > 0 && pk <= 86) { if (Skin_FindSpecificDef(selRedir, pk) == 0) continue; }
                else if (!hasPaint)     { continue; }
            }
            if (!SkinFilterMatch(k_warpaints[i].name, s_wpFilter)) continue;

            const bool sel = (s_warpSel == i);
            if (ImGui::Selectable(k_warpaints[i].name, sel))
            {
                s_warpSel = i;
                if (i == 0)                 { cfg.paintKitId = 0;  cfg.weaponSkinDef = 0; }
                else if (pk > 0 && pk <= 86){ cfg.paintKitId = 0;  cfg.weaponSkinDef = Skin_FindSpecificDef(selRedir, pk); }
                else                        { cfg.paintKitId = pk; cfg.weaponSkinDef = 0; }
                g_skinNeedsUpdate = true;
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::TextDisabled("paint kit id");
    if (ImGui::InputInt("##skkit", &cfg.paintKitId, 1, 10))
    {
        if (cfg.paintKitId < 0) cfg.paintKitId = 0;
        // A hand-typed 1-86 means a weapon-specific skin, which is stored as its 15xxx def.
        if (cfg.paintKitId >= 1 && cfg.paintKitId <= 86) {
            const int sd = Skin_FindSpecificDef(selRedir, cfg.paintKitId);
            if (sd > 0) { cfg.weaponSkinDef = sd; cfg.paintKitId = 0; }
        } else {
            cfg.weaponSkinDef = 0;
        }
        SkinSyncWarpaintSel(cfg, s_warpSel);
        g_skinNeedsUpdate = true;
    }

    ImGui::TextDisabled("wear");
    // The band name alone is not a usable slider format: ImGui rounds the dragged value by
    // formatting it and PARSING THE RESULT BACK, so a format with no conversion specifier
    // reads back as nothing and the widget shows an empty bar. Keep the number in it.
    SliderF("##skwear", &cfg.wear,  0.f, 1.f,
            cfg.wear < 0.07f ? "factory new %.2f"  :
            cfg.wear < 0.15f ? "minimal wear %.2f" :
            cfg.wear < 0.38f ? "field-tested %.2f" :
            cfg.wear < 0.45f ? "well-worn %.2f"    : "battle scarred %.2f");

    ImGui::TextDisabled("pattern seed");
    {
        static uint64_t step = 1, stepFast = 1000;
        ImGui::InputScalar("##skpat", ImGuiDataType_U64, &cfg.pattern, &step, &stepFast);
    }
    if (!hasPaint) { ImGui::EndDisabled(); ImGui::TextDisabled("(this weapon takes no warpaint)"); }

    // ---- killstreak ----------------------------------------------------------------
    Section("killstreak");
    {
        static const char* const kTier[]  = { "none","basic","specialized","professional" };
        static const char* const kSheen[] = { "none","team shine","deadly daffodil","manndarin",
                                              "mean green","agonizing emerald","villainous violet","hot rod" };
        static const char* const kEff[]   = { "none","fire horns","cerebral discharge","tornado",
                                              "flames","singularity","incinerator","hypno-beam" };
        ImGui::TextDisabled("tier");
        ImGui::Combo("##sktier", &cfg.ksTier, kTier, 4);
        if (cfg.ksTier >= 2) {
            ImGui::TextDisabled("sheen");
            ImGui::Combo("##sksheen", &cfg.ksSheen, kSheen, 8);
            if (ImGui::Checkbox("custom color", &cfg.ksCustomColor)) g_skinNeedsUpdate = true;
            if (ImGui::IsItemHovered())
                MenuTooltip("recolors BOTH the sheen and the eye glow: the two read one row of\n"
                            "the same table in client.dll, so the row the sheen above picks is\n"
                            "rewritten with this color while the weapon is out. with no sheen\n"
                            "picked it uses team shine's row. other players holding that same\n"
                            "sheen are recolored too -- it is one table for the whole client.");
            if (cfg.ksCustomColor)
                ImGui::ColorEdit3("##skkscol", cfg.ksColor,
                                  ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        }
        if (cfg.ksTier == 3) {
            ImGui::TextDisabled("kill effect");
            ImGui::Combo("##skkseff", &cfg.ksEffect, kEff, 8);
        }
        if (cfg.ksTier > 0) {
            ImGui::TextDisabled("streak count is faked locally");
            if (ImGui::IsItemHovered())
                MenuTooltip("the attributes alone give a sheen once every 5 seconds and no\n"
                            "eye glow at all: both are paced by your NETWORKED kill streak,\n"
                            "which a server that never issued the item keeps at zero. dih\n"
                            "counts your kills itself and writes that count. eyes need 5.");
        }
    }

    // ---- particle ------------------------------------------------------------------
    // attrs 134 + 370. Every id the game's own schema defines is offered, not just the
    // four Gun Mettle weapon unusuals -- a cosmetic effect attaches to a weapon fine.
    Section("particle");
    {
        char preview[96];
        if (cfg.unusual < 0) snprintf(preview, sizeof(preview), "none");
        else snprintf(preview, sizeof(preview), "%s (%d)", Particle_Name(cfg.unusual), cfg.unusual);

        ImGui::TextDisabled("unusual effect");
        if (ImGui::BeginCombo("##skpart", preview))
        {
            ImGui::InputText("##skpartfilter", s_partFilter, sizeof(s_partFilter));
            ImGui::Separator();
            for (int g = 0; g < k_particleGroupCount; ++g)
            {
                const ParticleGroup& pg = k_particleGroups[g];
                bool headerDrawn = false;
                for (int i = 0; i < pg.count; ++i)
                {
                    const ParticleEntry& pe = pg.items[i];
                    if (!SkinFilterMatch(pe.name, s_partFilter)) continue;
                    if (!headerDrawn) {
                        if (g) ImGui::Separator();
                        ImGui::TextDisabled("%s", pg.name);
                        headerDrawn = true;
                    }
                    char lbl[96];
                    if (pe.id < 0) snprintf(lbl, sizeof(lbl), "%s", pe.name);
                    else           snprintf(lbl, sizeof(lbl), "%s (%d)", pe.name, pe.id);
                    const bool sel = (cfg.unusual == pe.id);
                    if (ImGui::Selectable(lbl, sel)) cfg.unusual = pe.id;
                    if (sel) ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("particle id");
        if (ImGui::InputInt("##skpartid", &cfg.unusual, 1, 10) && cfg.unusual < -1)
            cfg.unusual = -1;
    }

    // ---- variant -------------------------------------------------------------------
    Section("variant");
    {
        if (!we.canAustralia) ImGui::BeginDisabled();
        if (ImGui::Checkbox("australium", &cfg.australium)) g_skinNeedsUpdate = true;
        if (!we.canAustralia) ImGui::EndDisabled();

        // The lights belong to the def the weapon ends up WEARING (ResolveWeaponTarget's order),
        // and the Festive variants and most reskins have none -- k_festiveDefs is the schema's say.
        const int wornDef = (cfg.festive && we.festiveIdx > 0) ? we.festiveIdx
                          : (cfg.reskinDef > 0)                 ? cfg.reskinDef
                          : (cfg.weaponSkinDef > 0)             ? cfg.weaponSkinDef
                          :                                       we.redir;
        const bool canFest = Skin_DefHasFestive(wornDef);
        if (!canFest) ImGui::BeginDisabled();
        if (ImGui::Checkbox("festivized", &cfg.festivized)) g_skinNeedsUpdate = true;
        if (!canFest) ImGui::EndDisabled();
        if (!canFest && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            MenuTooltip(wornDef == we.redir
                ? "this weapon has no festivizer lights in the game's schema."
                : "the picked festive / reskin / warpaint variant has no festivizer lights.\n"
                  "clear it to festivize the base weapon.");

        const bool hasFestive = (we.festiveIdx > 0);
        if (!hasFestive) ImGui::BeginDisabled();
        if (ImGui::Checkbox("festive", &cfg.festive)) g_skinNeedsUpdate = true;
        if (!hasFestive) ImGui::EndDisabled();
    }

    // ---- reskin --------------------------------------------------------------------
    Section("reskin");
    {
        const ReskinGroup* rg = Skin_FindReskinGroup(selRedir);
        const char* preview = "(none)";
        if (rg && cfg.reskinDef > 0)
            for (int i = 0; i < rg->count; i++)
                if (rg->variants[i].defindex == cfg.reskinDef
                    && rg->variants[i].style == cfg.reskinStyle)
                    { preview = rg->variants[i].name; break; }

        if (!rg) ImGui::BeginDisabled();
        if (ImGui::BeginCombo("##skreskin", preview))
        {
            const bool noneSel = (cfg.reskinDef == 0);
            if (ImGui::Selectable("(none)", noneSel))
                { cfg.reskinDef = 0; cfg.reskinStyle = 0; g_skinNeedsUpdate = true; }
            if (noneSel) ImGui::SetItemDefaultFocus();
            if (rg) for (int i = 0; i < rg->count; i++) {
                const bool sel = (cfg.reskinDef   == rg->variants[i].defindex
                               && cfg.reskinStyle == rg->variants[i].style);
                if (ImGui::Selectable(rg->variants[i].name, sel)) {
                    cfg.reskinDef   = rg->variants[i].defindex;
                    cfg.reskinStyle = rg->variants[i].style;
                    g_skinNeedsUpdate = true;
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (!rg) { ImGui::EndDisabled(); ImGui::TextDisabled("(no reskins for this weapon)"); }

        // What the picked item carries in items_game.txt, so a wrong-looking Saxxy or
        // Gold Frying Pan can be told apart from a wrong pick.
        if (cfg.reskinDef > 0) {
            if (const ReskinInfo* ri = Skin_FindReskinInfo(cfg.reskinDef)) {
                if (ri->turnToGold) ImGui::TextDisabled("turns victims to gold");
                if (ri->specialTaunt) {
                    ImGui::TextDisabled("taunt kill: server-side only");
                    if (ImGui::IsItemHovered())
                        MenuTooltip("special_taunt (attr 551) is read by the SERVER when it picks\n"
                                    "the taunt to run. it never reaches your client, so this one\n"
                                    "cannot be faked locally. the gold statue can and is.");
                }
                if (ri->styleCount > 0)
                    ImGui::TextDisabled("skin %d / %d (red / blu)",
                                        (int)ri->skinRed[cfg.reskinStyle < ri->styleCount ? cfg.reskinStyle : 0],
                                        (int)ri->skinBlu[cfg.reskinStyle < ri->styleCount ? cfg.reskinStyle : 0]);
                if (ri->anim != Skin_BaseAnimSlot(selRedir))
                    ImGui::TextDisabled("animations remapped");
            } else {
                ImGui::TextDisabled("no model info -- def index only");
            }
        }
    }

    // ---- extras --------------------------------------------------------------------
    Section("extras");
    {
        // Available on every weapon. The clock model only exists on an item that carries a
        // warpaint def's static weapon_uses_stattrak_module string, so with no skin picked the
        // weapon is put in a warpaint def whose paint is then cancelled (skins.cpp, k_noPaintKit)
        // -- the clock appears, the weapon still looks stock.
        ImGui::Checkbox("stat clock", &cfg.statClock);
        if (cfg.statClock) {
            ImGui::TextDisabled("kills");
            if (ImGui::InputInt("##skstatk", &cfg.statKills, 1, 100) && cfg.statKills < 0)
                cfg.statKills = 0;
            if (cfg.reskinDef > 0)
                ImGui::TextDisabled("no clock on a reskin -- the reskin owns the def index");
        }
    }

    {
        const bool hasPumpkin = (we.spells & kSpell_Pumpkin) != 0;
        const bool hasFire    = (we.spells & kSpell_Fire)    != 0;
        bool exo = (cfg.spellFlags & kCfgSpell_Exorcism) != 0;
        bool pmp = (cfg.spellFlags & kCfgSpell_Pumpkin)  != 0;
        bool fir = (cfg.spellFlags & kCfgSpell_Fire)     != 0;
        if (ImGui::Checkbox("spell: exorcism", &exo))
            cfg.spellFlags = (cfg.spellFlags & ~kCfgSpell_Exorcism) | (exo ? kCfgSpell_Exorcism : 0);
        if (!hasPumpkin) ImGui::BeginDisabled();
        if (ImGui::Checkbox("spell: pumpkin bombs", &pmp))
            cfg.spellFlags = (cfg.spellFlags & ~kCfgSpell_Pumpkin) | (pmp ? kCfgSpell_Pumpkin : 0);
        if (!hasPumpkin) ImGui::EndDisabled();
        if (!hasFire) ImGui::BeginDisabled();
        if (ImGui::Checkbox("spell: halloween fire", &fir))
            cfg.spellFlags = (cfg.spellFlags & ~kCfgSpell_Fire) | (fir ? kCfgSpell_Fire : 0);
        if (!hasFire) ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (ImGui::Button("apply", ImVec2(-1.f, 0.f))) g_skinNeedsUpdate = true;
    if (ImGui::IsItemHovered())
        MenuTooltip("re-resolves the reskin models and asks the server for a full update.\n"
                    "only needed when something did not take on its own.");
}

// botox tab_fonts: one page per face, "<face> font" settings | "font list". botox pages
// indicators / esp map to TF2 (fonts.h); its chud hud / default hud pages rewrite CS:GO's
// panorama fonts.conf and have no TF2 equivalent. Applies live: render.cpp rebuilds the atlas
// (indicators) and aim/helpers.cpp recreates the vgui face (esp) once the pick settles.
static const char* kFontsSub[2] = { "indicators", "esp" };
static int s_fontsSub = 0;
static const char* const kFontFlagNames[FF_COUNT] = {
    "no hinting", "no autohint", "force autohint", "light hinting", "monohinting", "bold", "oblique", "monochrome" };

static void FontPage(const char* name, char* face, size_t faceSize, int* size, int* flags, void (*reset)(),
                     const std::vector<std::string>& list, ImGuiTextFilter& filter)
{
    menu_columns_begin();

    if (menu_group_begin(name))
    {
        ImGui::SliderInt("size", size, 0, 50, "%d");
        ImGui::MultiCombo("font flags", flags, kFontFlagNames, FF_COUNT);
        if (ImGui::Button("reset to default", ImVec2(-1.f, 15.f))) reset();
        if (ImGui::Button("reload fonts", ImVec2(-1.f, 15.f))) Fonts_RequestReload();
    }
    menu_group_end();

    menu_columns_next();

    if (menu_group_begin("font list"))
    {
        filter.Draw("search for a font");
        if (ImGui::BeginListBox("##font list", ImVec2(0.f, 300.f)))
        {
            for (const auto& n : list)
                if (filter.PassFilter(n.c_str()) && ImGui::Selectable(n.c_str(), _stricmp(face, n.c_str()) == 0))
                    strncpy_s(face, faceSize, n.c_str(), _TRUNCATE);
            ImGui::EndListBox();
        }
    }
    menu_group_end();

    menu_columns_end();
}

static void Tab_Fonts()
{
    if (s_fontsSub == 1)
    {
        static ImGuiTextFilter s_espFilter;
        FontPage("esp font", g_fontEspName, sizeof(g_fontEspName), &g_fontEspSize, &g_fontEspFlags,
                 Fonts_ResetEsp, Fonts_Families(), s_espFilter);
    }
    else
    {
        static ImGuiTextFilter s_indFilter;
        FontPage("indicator font", g_fontIndName, sizeof(g_fontIndName), &g_fontIndSize, &g_fontIndFlags,
                 Fonts_ResetIndicator, Fonts_FileNames(), s_indFilter);
    }
    // UNIT_MENU_SLOT fonts
}

// botox config_panel(visuals, ..., list_rows 12): file name, list, then create / save / load /
// remove / refresh / open folder, with yes/no confirmations. visuals = the second panel: only
// the visuals-tab keys (visual_keys.h), files in C:\botox-tf2\visuals. Names shown without ".cfg".
static bool CfgNameGetter(void* data, int idx, const char** out)
{
    *out = (*static_cast<std::vector<std::string>*>(data))[idx].c_str();
    return true;
}

static void ConfigPanel(const bool visuals)
{
    struct PanelState {
        std::vector<std::string> names;
        bool listed = false, save = false, load = false, remove = false;
        int  selected = 0;
        char file[64] = "";
    };
    static PanelState s_panels[2];
    PanelState& p = s_panels[visuals];

    auto refresh = [&p, visuals]() {
        p.names = ListConfigs(visuals);
        for (auto& n : p.names) if (n.size() > 4) n.resize(n.size() - 4);
    };
    if (!p.listed) { refresh(); p.listed = true; }

    ImGui::InputText(visuals ? "visuals file name" : "config file name", p.file, sizeof(p.file));

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 2.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 3.f));
    ImGui::ListBox(visuals ? "##visuals config list" : "##config list", &p.selected, CfgNameGetter, &p.names,
                   (int)p.names.size(), 12);
    ImGui::PopStyleVar();

    if (p.selected < 0 || p.selected >= (int)p.names.size()) p.selected = 0;
    const std::string sel = p.names.empty() ? "" : p.names[p.selected];

    if (ImGui::Button("create", ImVec2(-1.f, 15.f)) && p.file[0]) { SaveConfigNamed(p.file, visuals); refresh(); }
    if (ImGui::Button("save", ImVec2(-1.f, 15.f)))   p.save = true;
    if (ImGui::Button("load", ImVec2(-1.f, 15.f)))   p.load = true;
    if (ImGui::Button("remove", ImVec2(-1.f, 15.f))) p.remove = true;
    if (ImGui::Button("refresh", ImVec2(-1.f, 15.f))) refresh();
    if (ImGui::Button("open folder", ImVec2(-1.f, 15.f)))
    {
        // first press before anything was saved: the folder must exist to open
        CreateDirectoryA(CONFIG_DIR, nullptr);
        CreateDirectoryA(ConfigFolder(visuals), nullptr);
        ShellExecuteA(nullptr, "open", ConfigFolder(visuals), nullptr, nullptr, SW_SHOWNORMAL);
    }

    // nothing listed: a save/load/remove on "" would hit nothing
    if (p.names.empty()) p.save = p.load = p.remove = false;

    SavePopup("save confirmation", p.save, ImVec2(220.f, -1.f), [&]() {
        if (ImGui::Button("yes", ImVec2((ImGui::GetRowFrameWidth() - 14.f) * 0.5f, 15.f))) { SaveConfigNamed(sel.c_str(), visuals); p.save = false; }
        ImGui::SameLine();
        if (ImGui::Button("no", ImVec2(-1.f, 15.f))) p.save = false;
    });
    SavePopup("load confirmation", p.load, ImVec2(220.f, -1.f), [&]() {
        if (ImGui::Button("yes", ImVec2((ImGui::GetRowFrameWidth() - 14.f) * 0.5f, 15.f))) { LoadConfigNamed(sel.c_str(), visuals); p.load = false; }
        ImGui::SameLine();
        if (ImGui::Button("no", ImVec2(-1.f, 15.f))) p.load = false;
    });
    SavePopup("remove confirmation", p.remove, ImVec2(220.f, -1.f), [&]() {
        if (ImGui::Button("yes", ImVec2((ImGui::GetRowFrameWidth() - 14.f) * 0.5f, 15.f)))
        {
            DeleteConfigNamed(sel.c_str(), visuals);
            refresh();
            p.selected = 0;
            p.remove = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("no", ImVec2(-1.f, 15.f))) p.remove = false;
    });
}

// botox tab_settings: col1 "config" + "system", col2 "visuals config" + "menu". Left out, no
// TF2 counterpart: "botox network" (no TF2 server) and "dpi scale panels" (botox render.cpp's
// panel-dpi layer; this DLL's hud panels have no dpi path). menu key is TF2-only (botox
// hardcodes insert), kept in system.
static void Tab_Settings()
{
    // botox menu_internal.h color_picker_no_alpha_flags
    constexpr ImGuiColorEditFlags kPickerNoAlpha = ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_InputRGB |
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop |
        ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoBorder;

    menu_columns_begin();

    if (menu_group_begin("config"))
        ConfigPanel(false);
    menu_group_end();

    if (menu_group_begin("system"))
    {
        // install-time diags ran before this was ever ticked, so their lines went nowhere.
        // Replay the ones that were kept the moment the log opens.
        if (ImGui::Checkbox("debug log", &g_debugLogEnable) && g_debugLogEnable)
        {
            DebugLog_FlushEarly();
            Scoreboard_LogInstallOnce();
            LagRes_LogInstallOnce();
        }
        ImGui::Checkbox("safe mode", &g_safeMode);

        ImGui::Label("menu key");
        int menuKey = g_menuKey;
        int dummyMode = 1;
        ImGui::Keybind("menu keybind", &menuKey, &dummyMode, false);
        g_menuKey = menuKey ? menuKey : VK_INSERT;
    }
    menu_group_end();

    menu_columns_next();

    if (menu_group_begin("visuals config"))
        ConfigPanel(true);
    menu_group_end();

    if (menu_group_begin("menu"))
    {
        ImGui::ColorRow("accent color##accent color", g_accentColor, kPickerNoAlpha);

        // botox: edits a copy, commits on release -- the whole menu rescales under the cursor
        // otherwise. 50% floor, not botox's 25%: RenderMenu resets anything under 0.5 to 1.
        static float s_dpiEdit = 100.f;
        static bool s_dpiActive = false;
        if (!s_dpiActive) s_dpiEdit = g_dpiScale * 100.f;
        ImGui::SliderFloat("dpi scale", &s_dpiEdit, 50.f, 200.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
        s_dpiActive = ImGui::IsItemActive();
        // commit on release only: x*100/100 is not exact, and RenderMenu rescales the whole
        // style on any change, so a per-frame write would drift every size
        if (ImGui::IsItemDeactivatedAfterEdit()) g_dpiScale = s_dpiEdit / 100.f;
    }
    menu_group_end();

    menu_columns_end();
    // UNIT_MENU_SLOT settings
}

// ==== tab bar =======================================================================
// botox's own tab/page shape (botox_menu_spec.md section 2): aimbot | visuals (esp,
// world, screen) | movement (main, indicators, calculator, recorder) | misc (main,
// player list) | inventory (= skinchanger) | fonts | settings.
enum MenuTab { TAB_AIMBOT, TAB_VISUALS, TAB_MOVEMENT, TAB_MISC, TAB_INVENTORY, TAB_FONTS, TAB_SETTINGS, TAB_COUNT };
static const char* kTabNames[TAB_COUNT] = { "aimbot", "visuals", "movement", "misc", "inventory", "fonts", "settings" };
static int s_tab = TAB_AIMBOT;

// botox_menu_spec.md section 2: pages per tab. aimbot/inventory/fonts/settings have
// none; visuals/movement/misc carry the sidebar's flyout.
static const SidebarTab kSidebar[TAB_COUNT] = {
    { kTabNames[TAB_AIMBOT],    nullptr,     0 },
    { kTabNames[TAB_VISUALS],   kVisualsSub, 3 },
    { kTabNames[TAB_MOVEMENT],  kMoveSub,    4 },
    { kTabNames[TAB_MISC],      nullptr,     0 },
    { kTabNames[TAB_INVENTORY], nullptr,     0 },
    { kTabNames[TAB_FONTS],     kFontsSub,   2 },
    { kTabNames[TAB_SETTINGS],  nullptr,     0 },
};

// botox_menu_spec.md section 1, build_date_text() literal (menu.cpp lines 22-45): the
// PE timestamp of this module, via localtime_s, falling back to __DATE__ if that read
// fails. Format "<d> <mon> <yyyy>", lowercase 3-letter month -- NOT the day name (botox
// has no weekday in the title, that was this fork's own embellishment).
static const char* BuildDateText()
{
    static char s_text[20] = { 0 };
    if (s_text[0]) return s_text;

    static const char* kMon[12] = { "jan","feb","mar","apr","may","jun",
                                    "jul","aug","sep","oct","nov","dec" };

    HMODULE mod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&BuildDateText), &mod))
    {
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(mod);
        if (dos->e_magic == IMAGE_DOS_SIGNATURE)
        {
            const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
                reinterpret_cast<const std::uint8_t*>(mod) + dos->e_lfanew);
            std::tm local = { };
            const std::time_t stamp = nt->FileHeader.TimeDateStamp;
            if (nt->Signature == IMAGE_NT_SIGNATURE && localtime_s(&local, &stamp) == 0)
            {
                _snprintf(s_text, sizeof(s_text), "%d %s %d",
                          local.tm_mday, kMon[local.tm_mon], local.tm_year + 1900);
                return s_text;
            }
        }
    }

    // __DATE__ fallback: "Mmm dd yyyy", space-padded day ("Aug  5 2026")
    const int day  = (__DATE__[4] == ' ' ? 0 : (__DATE__[4] - '0') * 10) + (__DATE__[5] - '0');
    const int year = (__DATE__[7] - '0') * 1000 + (__DATE__[8] - '0') * 100 +
                     (__DATE__[9] - '0') * 10   + (__DATE__[10] - '0');
    int m = 0;
    for (int i = 0; i < 12; ++i)
        if (_strnicmp(__DATE__, kMon[i], 3) == 0) { m = i; break; }
    _snprintf(s_text, sizeof(s_text), "%d %s %d", day, kMon[m], year);
    return s_text;
}

// botox menu.cpp save_popup("eject confirmation", ..., ImVec2(220, -1)): fixed 220 wide,
// 20px #191919 title band with the id centred in verdana bold, accent fade line under it,
// then yes / no as two 15px buttons sharing the row. Goes through SavePopup like botox, so it
// fades with the other confirm popups.
static bool s_ejectOpen = false;
static void EjectPopup()
{
    SavePopup("eject confirmation", s_ejectOpen, ImVec2(220.f, -1.f), []() {
        if (ImGui::Button("yes", ImVec2((ImGui::GetRowFrameWidth() - 14.f) * 0.5f, 15.f)))
        {
            s_ejectOpen = false;
            g_eject = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("no", ImVec2(-1.f, 15.f))) s_ejectOpen = false;
    });
}

// botox m_anim_progress: render.cpp composites the menu's draw lists as ONE layer at this
// alpha (ImGui_ImplDX9_RenderDrawDataFaded), not style alpha -- that skipped every hand-drawn
// band, title, tab and border, so the chrome popped while the widgets faded.
static float s_animProgress = 0.f;
float Menu_FadeAlpha() { return ImSaturate(s_animProgress); }

static bool OwnsWindow(ImGuiWindow* w, ImGuiWindow* root)
{
    for (int d = 0; w && d < 32; ++d, w = w->ParentWindow)
        if (w == root) return true;
    return false;
}

static bool WindowDrawn(const ImGuiWindow* w) { return w && w->Active && !w->Hidden && w->DrawList; }

// botox fade_draw_lists, popup half: popups fade at 2x the menu speed. Lists owned by a live
// popup leave the menu layer for `out` at menu_fade * popup alpha; closed popups add their ghost.
int Menu_SplitPopups(ImDrawList** lists, int& count, MenuFadedList* out, int max)
{
    ImGuiWindow* root = ImGui::FindWindowByName("botox");
    const float step = (std::min)(ImGui::GetIO().DeltaTime, 1.f / 30.f) / (0.12f * 0.5f);
    const bool shown = s_animProgress > 0.f && WindowDrawn(root);

    for (int i = 0; i < s_popups.Size;)
    {
        PopupFade& p = s_popups[i];
        ImGuiWindow* w = ImGui::FindWindowByID(p.id);
        if (shown && p.live && WindowDrawn(w))
        {
            p.alpha = (std::min)(p.alpha + step, 1.f);
            if (!p.ghost) p.ghost = IM_NEW(ImDrawList)(w->DrawList->_Data);
            p.ghost->CmdBuffer = w->DrawList->CmdBuffer;
            p.ghost->IdxBuffer = w->DrawList->IdxBuffer;
            p.ghost->VtxBuffer = w->DrawList->VtxBuffer;
            p.ghost->Flags     = w->DrawList->Flags;
        }
        else if (!shown || !p.live)
            p.alpha = shown ? p.alpha - step : 0.f;

        // live but hidden = imgui auto-fit frame, hold at 0
        if (p.alpha <= 0.f && !(shown && p.live))
        {
            if (p.ghost) IM_DELETE(p.ghost);
            s_popups.erase(s_popups.Data + i);
            continue;
        }
        ++i;
    }

    int n = 0;
    if (!shown) return 0;   // !shown emptied s_popups above
    const float menu_fade = ImSaturate(s_animProgress);

    ImGuiContext& g = *GImGui;
    int keep = 0;
    for (int i = 0; i < count; ++i)
    {
        ImDrawList* dl = lists[i];
        const PopupFade* owner = nullptr;
        for (ImGuiWindow* w : g.Windows)
        {
            if (w->DrawList != dl) continue;
            for (const PopupFade& p : s_popups)
                if (p.live && OwnsWindow(w, ImGui::FindWindowByID(p.id))) owner = &p;
            break;
        }
        if (owner && n < max) out[n++] = { dl, menu_fade * owner->alpha };
        else lists[keep++] = dl;
    }
    count = keep;

    for (PopupFade& p : s_popups)
    {
        if (!p.live && p.ghost && n < max) out[n++] = { p.ghost, menu_fade * p.alpha };
        p.live = false;
    }
    return n;
}

// ghosts hold the font atlas texture id: drop them with it (reset, rebind, atlas rebuild)
void Menu_DropPopupGhosts()
{
    for (PopupFade& p : s_popups)
        if (p.ghost) { IM_DELETE(p.ghost); p.ghost = nullptr; }
}

void RenderMenu()
{
    ImGuiIO& io = ImGui::GetIO();

    // live accent (settings tab RowColor edits g_accentColor directly; this is the one
    // place it is pushed into the style every widget already reads it through)
    ImGui::GetStyle().Colors[ImGuiCol_Accent] =
        ImVec4(g_accentColor[0], g_accentColor[1], g_accentColor[2], g_accentColor[3]);

    // whole-menu dpi scale (settings tab slider). ScaleAllSizes mutates the CURRENT
    // style multiplicatively -- applying it every frame would compound, so it only runs
    // on the frame the value actually changed, as a ratio against what was last applied.
    {
        // a bad saved value (0, negative, NaN) would zero out the style forever via the
        // ratio below -- same floor/ceiling as the settings slider's own range.
        if (!(g_dpiScale >= 0.5f && g_dpiScale <= 2.f)) g_dpiScale = 1.f;
        static float s_appliedDpi = 1.f;
        if (g_dpiScale != s_appliedDpi)
        {
            ImGui::GetStyle().ScaleAllSizes(g_dpiScale / s_appliedDpi);
            io.FontGlobalScale = g_dpiScale;
            s_appliedDpi = g_dpiScale;
        }
    }

    // botox menu.cpp on_end_scene, verbatim (botox_menu_spec.md section 1): fixed
    // 750 x (640 - (menu_band_height - menu_bottom_band_height)) = 750x627, centred once,
    // never resized by the user. m_anim_progress is the open/close fade (section 7); while
    // it has not reached 1 the window eats no input, same as botox's eject-safety gate.
    if (g_eject) s_animProgress = 0.f;   // eject snaps shut, never fades under a release
    else
    {
        const float step = (std::min)(io.DeltaTime, 1.f / 30.f) / 0.12f;   // menu_anim_duration
        s_animProgress = ImClamp(s_animProgress + (g_showMenu ? step : -step), 0.f, 1.f);
    }
    if (!g_showMenu && s_animProgress <= 0.f) return;

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Once, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(750.f, 640.f - (25.f - 12.f)), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoScrollWithMouse;   // the ##content child scrolls, not the frame
    if (s_animProgress < 0.999f) flags |= ImGuiWindowFlags_NoInputs;

    if (!ImGui::Begin("botox", NULL, flags))
    {
        ImGui::End();
        return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const ImVec2 wz = ImGui::GetWindowSize();
    const float rnd = ImGui::GetStyle().WindowRounding;

    // ---- botox on_end_scene order: top band, window border, accent line under the band;
    // bottom band + accent line on its top edge. Lines = RenderFadedGradientLine (bright
    // centre, fading to both ends), full window width.
    // Bands + border each get botox's own PushClipRect(.., false): the window's clip rect is
    // inset WindowPadding.x/2 = 4px per side, which cut the bands and the side border short.
    dl->PushClipRect(wp, ImVec2(wp.x + wz.x, wp.y + mf::kHeaderH), false);
    dl->AddRectFilled(wp, ImVec2(wp.x + wz.x, wp.y + mf::kHeaderH), kBandBg, rnd, ImDrawFlags_RoundCornersTop);
    dl->PopClipRect();
    dl->PushClipRect(wp, ImVec2(wp.x + wz.x, wp.y + wz.y), false);
    dl->AddRect(wp, ImVec2(wp.x + wz.x, wp.y + wz.y), kWindowBorder, rnd);
    dl->PopClipRect();
    RenderFadedGradientLine(dl, ImVec2(wp.x, wp.y + mf::kHeaderH - 1.f), ImVec2(wz.x, 1.f), ImColor(Accent()));

    const float botY = wp.y + wz.y - 12.f;   // menu_bottom_band_height
    dl->PushClipRect(ImVec2(wp.x, botY), ImVec2(wp.x + wz.x, wp.y + wz.y), false);
    dl->AddRectFilled(ImVec2(wp.x, botY), ImVec2(wp.x + wz.x, wp.y + wz.y), kBandBg, rnd, ImDrawFlags_RoundCornersBottom);
    dl->PopClipRect();
    RenderFadedGradientLine(dl, ImVec2(wp.x, botY), ImVec2(wz.x, 1.f), ImColor(Accent()));

    // ---- title: "botox" bold white, then " | <date>" regular accent, same baseline,
    // centred as one unit in the top band.
    {
        ImGui::PushFont(BoldFont());
        char title[8] = "botox";
        const ImVec2 ws = ImGui::CalcTextSize(title);
        ImGui::PopFont();
        ImGui::PushFont(RegFont());
        char date[24];
        _snprintf(date, sizeof(date), " | %s", BuildDateText());
        const ImVec2 ds = ImGui::CalcTextSize(date);
        ImGui::PopFont();

        const float tx = wp.x + (wz.x - ws.x - ds.x) * 0.5f;
        const float ty = wp.y + (mf::kHeaderH - ws.y) * 0.5f;
        ImGui::PushFont(BoldFont());
        dl->AddText(ImVec2(tx, ty), IM_COL32_WHITE, title);
        ImGui::PopFont();
        ImGui::PushFont(RegFont());
        dl->AddText(ImVec2(tx + ws.x, ty), Accent(), date);
        ImGui::PopFont();
    }

    // ---- sidebar: botox's stacked boxed tabs (botox_menu_spec.md section 2) -----------
    // tab_x = window content-left (WindowPadding.x), tab_y = pos.y+25 in LOCAL coords,
    // i.e. 25 + WindowPadding.y once SidebarTab_Draw adds the window origin back on.
    const ImGuiStyle& st = ImGui::GetStyle();
    const float sidebarX = st.WindowPadding.x;
    const float sidebarTop = mf::kHeaderH + st.WindowPadding.y;
    // botox's tab_button is draw-list only; ours is an InvisibleButton, which moves the
    // cursor. Eject is the last one, at the window floor, so without this restore the
    // content below starts under eject -- off the bottom of the window, every tab blank.
    const ImVec2 contentStart = ImGui::GetCursorPos();
    {
        static int s_noPage = -1;
        float tabY = sidebarTop;
        for (int i = 0; i < TAB_COUNT; ++i)
        {
            int& page = (i == (int)TAB_VISUALS)  ? s_visualsSub
                      : (i == (int)TAB_MOVEMENT) ? s_moveSub
                      : (i == (int)TAB_MISC)     ? s_miscSub
                      : (i == (int)TAB_FONTS)    ? s_fontsSub
                      : s_noPage;
            if (SidebarTab_Draw(kSidebar[i], i, s_tab == i, page, sidebarX, tabY, io.DeltaTime))
                s_tab = i;
        }

        // eject: same box look, pinned to the sidebar's bottom (spec section 2):
        // tab_y = pos.y + size.y - WindowPadding.y - bottom_band_height - tab_height.
        float ejectY = wz.y - st.WindowPadding.y - 12.f - mf::kTabH;
        int ejectNoPage = -1;
        if (SidebarTab_Draw(SidebarTab{ "eject", nullptr, 0 }, TAB_COUNT, false, ejectNoPage,
                            sidebarX, ejectY, io.DeltaTime))
            s_ejectOpen = true;

        EjectPopup();
    }
    ImGui::SetCursorPos(contentStart);

    // ---- content: botox_menu_spec.md section 2 -- Indent past the sidebar, drop 25px
    // under the title band, PushID per tab so two tabs never share a cached group height.
    // One borderless, bg-less child stands in for botox's two column children: it clips
    // the boxes above the bottom band (avail.y - menu_bottom_band_height, same as
    // menu_columns_begin) and scrolls a tab taller than the window with the wheel.
    ImGui::Indent(mf::kTabW + st.ItemSpacing.x);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + mf::kHeaderH);
    ImGui::PushID(s_tab);
    ImGui::BeginChild("##content", ImVec2(0.f, ImGui::GetContentRegionAvail().y - 12.f), false,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);

    FlowBegin();
    switch (s_tab)
    {
    // Aimbot / triggerbot / melee. The body lives in src/hacks/aim/aim.cpp because it
    // reads that tree's ConfigVar metadata.
    case TAB_AIMBOT:    Aim_RenderMenuTab(); break;   // unitF groups render inside its columns
    case TAB_VISUALS:   Tab_VisualsRoot();    break;
    case TAB_MOVEMENT:  Tab_MovementRoot();   break;
    case TAB_MISC:      Tab_MiscRoot();       break;
    case TAB_INVENTORY: Tab_Skins();          break;
    case TAB_FONTS:     Tab_Fonts();          break;
    case TAB_SETTINGS:  Tab_Settings();       break;
    }
    FlowEnd();

    ImGui::EndChild();
    ImGui::PopID();
    ImGui::Unindent(mf::kTabW + st.ItemSpacing.x);

    ImGui::End();
}
