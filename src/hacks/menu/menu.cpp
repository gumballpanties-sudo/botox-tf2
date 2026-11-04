#include "menu.h"
#include "../aim/aim.h"
#include "config.h"
#include "../network/mmfix.h"
#include "../hud/indicators.h"
#include "../network/netregion.h"
#include "../hud/scoreboard.h"
#include "../screen/colorcorrect.h"
#include "../hud/crosshair.h"   // Crosshair_BlendCombo
#include "../hud/hud_extra.h"   // unit hud: fonts tab net graph page reset
#include "../visuals/lagres.h"
#include "../movement/movement.h"   // RC_* marker access for the route calculator's marker list
#include "../movement/points_style.h"   // movement > points page: styles + preview
#include "../visuals/skins.h"      // skinchanger tab: weapon/warpaint/reskin tables + particle list
#include "../visuals/hats.h"       // inventory tab "hats" page
#include "../visuals/skinshare.h"  // skinchanger tab "networking" box
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
#include "../visuals/world_fx3_math.h"   // unit world: remove hud elements list (k_hudElems)
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
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + 20.f), kBandBg(), rnd, ImDrawFlags_RoundCornersTop);
    ImGui::PopClipRect();
    ImGui::PushClipRect(p, ImVec2(p.x + sz.x, p.y + sz.y), false);
    dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), kTabBorder(), rnd);
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
        pw->DrawList->AddRect(pw->Pos, ImVec2(pw->Pos.x + pw->Size.x, pw->Pos.y + pw->Size.y), kTabBorder(), GetStyle().WindowRounding);
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

    // botox "edicts" group under "players" (dropped weapons, thrown projectiles, grenade path + TF2 buildings)
    if (menu_group_begin("edicts"))
        UnitC_Menu_visuals_world();
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

    // botox k_skyboxes[0] = "off". g_skyEnable is what h_R_DrawSkyBox gates on -- nothing set it
    // before, so picking a sky did nothing.
    const char* preview = !g_skyEnable ? "off" : (s_skySel >= 0) ? kSkyList[s_skySel] : "custom";
    if (ImGui::BeginCombo("skybox", preview))
    {
        if (ImGui::Selectable("off", !g_skyEnable))
            g_skyEnable = false;
        for (int i = 0; i < kSkyCount; ++i)
            if (ImGui::Selectable(kSkyList[i], g_skyEnable && s_skySel == i))
            {
                s_skySel = i;
                g_skyEnable = true;
                strncpy(g_skyName, kSkyList[i], sizeof(g_skyName) - 1);
                g_skyName[sizeof(g_skyName) - 1] = '\0';
            }
        if (ImGui::Selectable("custom", g_skyEnable && s_skySel == -1))
        {
            s_skySel = -1;
            g_skyEnable = true;
        }
        ImGui::EndCombo();
    }
    if (g_skyEnable && s_skySel == -1)
        ImGui::InputText("##skyname", g_skyName, sizeof(g_skyName));
}

static void Page_VisualsWorld()
{
    menu_columns_begin();
    if (menu_group_begin("world"))
    {
        // botox menu_tab_visuals.cpp world group, verbatim order / labels / ranges. TF2 cuts are
        // listed in unitD_menu.cpp (UnitD_Menu_visuals_world, which carries the rows from dlight on).
        ImGui::Checkbox("world fov", &g_worldFovEnable);
        if (g_worldFovEnable)
            ImGui::SliderFloat("fov##world", &g_wdWorldFov, -30.f, 60.f, "%.1f");

        ImGui::Checkbox("precipitation", &g_weatherEnable);
        if (g_weatherEnable)
            ImGui::OptionPopup("precipitation settings", [&]() {   // ours: TF2's r_rain* knobs
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
        if (g_weatherEnable)
        {
            // botox "rain\0ash\0rain storm\0snow\0": TF2's C_Precipitation only spawns ash where a
            // trace hits the func_precipitation brush (c_effects.cpp:886 UTIL_TraceModel), which the
            // entity this DLL injects has none of, and has no storm type (precipitation_shared.h).
            // combo index 0/1 = g_weatherMode 1/2 (0 is "off").
            int type = (g_weatherMode == 2) ? 1 : 0;
            if (ImGui::Combo("type##precipitation", &type, "rain\0" "snow\0"))
                g_weatherMode = type + 1;
        }

        SkyboxCombo();

        // env_sun corona + shadow_control (render-to-texture shadows) -- world_fx2.cpp. No roll:
        // a direction has none (botox's roll is CS:GO's cascade-map spin, cl_csm_rot_z).
        ImGui::Checkbox("sun angle", &g_sunAngleEnable);
        if (g_sunAngleEnable)
            ImGui::OptionPopup("sun angle settings", [&]() {
                ImGui::SliderFloat("pitch##sun angle", &g_sunAnglePitch, -180.f, 180.f, "%.1f");
                ImGui::SliderFloat("yaw##sun angle", &g_sunAngleYaw, -180.f, 180.f, "%.1f");
                ImGui::SliderFloat("rotation speed##sun angle", &g_wdSunSpeed, 0.f, 90.f, "%.1f deg/s");
                ImGui::SliderFloat("shadow distance##sun angle", &g_wdSunShadowDist, 0.f, 3000.f, "%.0f u");
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("fog", &g_fogEnable);
        if (g_fogEnable)
            ImGui::OptionPopup("fog settings", [&]() {
                ImGui::ColorEdit4("color##fog color picker", g_fogColor);
                ImGui::SliderFloat("start##fog", &g_fogStart, -5000.f, 5000.f, "%.0f u");
                ImGui::SliderFloat("end##fog", &g_fogEnd, -5000.f, 5000.f, "%.0f u");
                float pct = g_fogDensity * 100.f;   // ours is 0..1 (fog_maxdensity), botox shows %
                if (ImGui::SliderFloat("density##fog", &pct, 0.f, 100.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
                    g_fogDensity = pct * 0.01f;
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("fullbright", &g_fullbrightEnable);

        ImGui::Checkbox("flip world", &g_flipWorld);
        if (g_flipWorld)
            ImGui::Checkbox("gun other side##flip world", &g_wdFlipGunOtherSide);

        // UNIT_MENU_SLOT visuals_world: dlight .. world hit marker, then minecraft sky
        UnitD_Menu_visuals_world();
    }
    menu_group_end();

    menu_columns_next();
    if (menu_group_begin("removals"))
    {
        // botox safe_checkbox: greyed + shown off under safe mode (world_fx.cpp skips it too)
        {
            bool off = false;
            ImGui::BeginDisabled(g_safeMode);
            ImGui::Checkbox("disable post processing", g_safeMode ? &off : &g_sfxDisablePostProcessing);
            ImGui::EndDisabled();
            if (g_safeMode && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("safe mode enabled");
        }
        // botox order (remove panorama blur / smoke / flash: no panorama, smokes or flashbangs in TF2)
        ImGui::Checkbox("remove 3d skybox", &g_wdNo3dSky);

        ImGui::Checkbox("remove hud elements", &g_wdNoHud);
        if (g_wdNoHud)
        {
            // TF2 vgui panels (world_fx3_math.h k_hudElems): botox's radar and c4 icon have none
            const char* items[wfx3::k_hudElemCount];
            int mask = 0;
            for (int i = 0; i < wfx3::k_hudElemCount; ++i)
            {
                items[i] = wfx3::k_hudElems[i].label;
                if (g_wdNoHudElems[i]) mask |= 1 << i;
            }
            if (ImGui::MultiCombo("elements##remove hud", &mask, items, wfx3::k_hudElemCount))
                for (int i = 0; i < wfx3::k_hudElemCount; ++i) g_wdNoHudElems[i] = (mask >> i) & 1;
        }

        ImGui::Checkbox("remove ragdolls", &g_removeRagdolls);
        ImGui::Checkbox("remove visual recoil", &g_removeVisualRecoilEnable);
        ImGui::Checkbox("disable interp", &g_disableInterpEnable);

        // ours (TF2)
        ImGui::Checkbox("no hats", &g_noHats);
        ImGui::Checkbox("no unusual effects", &g_noUnusual);
        ImGui::Checkbox("remove prop fade", &g_removePropFade);
        ImGui::Checkbox("fps boost", &g_fpsBoost);
        ImGui::Checkbox("remove screen effects", &g_removeScreenEffects);
    }
    menu_group_end();

    if (menu_group_begin("viewmodel"))
    {
        // botox order + labels. weapon sheen: TF2's own killstreak sheen (skins tab, per weapon) is the
        // native version of it. bob changer's shift left/right: CS:GO shifts the gun by its live
        // inaccuracy (weapon_csbase.cpp:3399), TF2 weapons have a fixed spread -- nothing to drive it.
        ImGui::Checkbox("viewmodel offset", &g_vmOffsetEnable);
        if (g_vmOffsetEnable)
            ImGui::OptionPopup("viewmodel offset settings", [&]() {
                ImGui::SliderFloat("x##viewmodel", &g_vmOffsetX, -30.f, 30.f, "%.2f");
                ImGui::SliderFloat("y##viewmodel", &g_vmOffsetY, -30.f, 30.f, "%.2f");
                ImGui::SliderFloat("z##viewmodel", &g_vmOffsetZ, -30.f, 30.f, "%.2f");
                ImGui::SliderFloat("roll##viewmodel", &g_wdVmRoll, -180.f, 180.f, "%.1f");
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("viewmodel fov", &g_wdVmFovEnable);
        if (g_wdVmFovEnable)
            ImGui::OptionPopup("viewmodel fov settings", [&]() {
                ImGui::SliderFloat("fov##viewmodel", &g_wdVmFov, 1.f, 179.f, "%.1f");
            }, ImVec2(220.f, -1.f));

        // ours: blend mode for that native sheen. off = stock additive envmap pass
        ImGui::Checkbox("normal blend sheen", &g_sheenNormal);
        if (ImGui::IsItemHovered())
            MenuTooltip("killstreak sheen paints its tinted envmap reflection with the sheen\n"
                        "texture as the mask (normal blending), instead of the stock blown out\n"
                        "glow. env strength 10 = stock brightness, lower shows the reflection.\n"
                        "sheen + color come from the skins tab, per weapon.");
        if (g_sheenNormal)
            ImGui::OptionPopup("normal blend sheen settings", [&]() {
                SliderF("opacity##sheennormal", &g_sheenNormalAlpha, 0.f, 1.f, "%.2f");
                SliderF("env strength##sheennormal", &g_sheenNormalEnv, 0.f, 10.f, "%.2f");
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("weapon sway", &g_vmSwayEnable);
        if (g_vmSwayEnable)
            ImGui::OptionPopup("weapon sway settings", [&]() {
                ImGui::SliderFloat("scale##weapon sway", &g_vmSwayScale, 0.f, 50.f, "%.2f");
                SliderF("interp##weapon sway", &g_vmSwayInterp, 0.f, 1.f, "%.2f");   // ours: cl_wpn_sway_interp
                if (ImGui::Button("csgo sway##vs", ImVec2(-1.f, 0.f)))
                {
                    g_vmSwayScale = 1.6f;    // cl_wpn_sway_scale  (CS:GO default)
                    g_vmSwayInterp = 0.1f;   // cl_wpn_sway_interp (CS:GO default)
                }
            }, ImVec2(220.f, -1.f));

        ImGui::BeginDisabled(g_vmRemoveBob);
        ImGui::Checkbox("bob changer", &g_vmCsBob);   // CS:GO bob, ported (vm_csgo.h)
        if (g_vmCsBob)
            ImGui::OptionPopup("bob changer settings", [&]() {
                ImGui::SliderFloat("cycle", &g_vmCsBobCycle, 0.f, 5.f, "%.2f");
                ImGui::SliderFloat("vertical", &g_vmCsBobVert, 0.f, 10.f, "%.2f");
                ImGui::SliderFloat("lateral", &g_vmCsBobLat, 0.f, 10.f, "%.2f");
                ImGui::SliderFloat("lower amount", &g_vmCsBobLower, 0.f, 100.f, "%.1f");
                SliderF("bob up time##vcb", &g_vmCsBobUp, 0.05f, 0.95f, "%.2f");   // ours: cl_bobup
                if (ImGui::Button("csgo defaults##vcb", ImVec2(-1.f, 0.f)))
                {
                    g_vmCsBobLower = 21.f; g_vmCsBobVert = 0.25f; g_vmCsBobLat = 0.4f;
                    g_vmCsBobCycle = 0.98f; g_vmCsBobUp = 0.5f;
                }
            }, ImVec2(220.f, -1.f));
        ImGui::EndDisabled();

        // ours (TF2)
        ImGui::Checkbox("remove bob", &g_vmRemoveBob);

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
        // UNIT_MENU_SLOT visuals_screen (third person, aspect ratio)
        UnitD_Menu_visuals_screen();

        // botox menu_tab_visuals.cpp 1181-1237, 1:1 (one shader pass with the screen filters,
        // colorcorrect.cpp / fx_grade.hlsl)
        ImGui::Checkbox("color correction", &g_ccEnable);
        if (g_ccEnable)
            ImGui::OptionPopup("color correction settings", [&]() {
                ImGui::SliderFloat("exposure##color correction", &g_ccExposure, -3.f, 3.f, "%+.2f ev");

                ImGui::Label("levels");
                ImGui::SliderFloat("in black##color correction", &g_ccLevInBlack, 0.f, 0.99f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("in white##color correction", &g_ccLevInWhite, 0.01f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("gamma##color correction", &g_ccGamma, 0.1f, 10.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("out black##color correction", &g_ccLevOutBlack, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("out white##color correction", &g_ccLevOutWhite, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

                // the remap divides by ( in white - in black ), so they can never meet
                if (g_ccLevInBlack >= g_ccLevInWhite)
                    g_ccLevInWhite = g_ccLevInBlack + 0.01f;

                CcCurveEditor();

                ImGui::Label("tone");
                ImGui::SliderFloat("highlights##color correction", &g_ccHighlights, -1.f, 1.f, "%+.2f");
                ImGui::SliderFloat("shadows##color correction", &g_ccShadows, -1.f, 1.f, "%+.2f");

                ImGui::Label("color");
                ImGui::SliderFloat("red##color correction", &g_ccGainR, 0.f, 2.f, "%.2f");
                ImGui::SliderFloat("green##color correction", &g_ccGainG, 0.f, 2.f, "%.2f");
                ImGui::SliderFloat("blue##color correction", &g_ccGainB, 0.f, 2.f, "%.2f");
                ImGui::SliderFloat("hue##color correction", &g_ccHue, -180.f, 180.f, "%.1f");
                ImGui::SliderFloat("saturation##color correction", &g_ccSat, 0.f, 3.f, "%.2f");
                ImGui::SliderFloat("contrast##color correction", &g_ccContrast, 0.5f, 1.5f, "%.3f");
                ImGui::SliderFloat("temperature##color correction", &g_ccTemp, -1.f, 1.f, "%+.2f");

                if (ImGui::Button("reset##color correction", ImVec2(-1.f, 15.f))) CcResetAll();
            }, ImVec2(260.f, -1.f));

        // UNIT_MENU_SLOT visuals_screen (sharpen .. show depth, reflections between depth distort and emphasize)
        UnitD3_Menu_visuals_screen();

        // unit post: botox menu_tab_visuals.cpp:1627-1856, same order (dof, ao, motion blur, true
        // motion blur, bloom), same labels. Our own knobs ride after botox's rows in each popup.
        const char* ScreenFx_RouteName();    // hacks/screen/screenfx.cpp -- the depth readout
        const char* ScreenFx_VerifyName();

        ImGui::Checkbox("depth of field", &g_dofEnable);
        if (g_dofEnable)
            ImGui::OptionPopup("depth of field settings", [&]() {
                ImGui::Label("focus");
                ImGui::Checkbox("auto focus##depth of field", &g_dofAutoFocus);
                if (!g_dofAutoFocus)
                    SliderF("distance##depth of field", &g_dofDistance, 16.f, 4000.f, "%.0f u");
                else
                {
                    SliderF("focus point x##depth of field", &g_dofFocusX, 0.f, 1.f, "%.2f");
                    SliderF("focus point y##depth of field", &g_dofFocusY, 0.f, 1.f, "%.2f");
                    SliderF("focus speed##depth of field", &g_dofFocusSpeed, 0.5f, 40.f, "%.1f");
                    SliderF("focus limit##depth of field", &g_dofFocusLimit, 256.f, 8192.f, "%.0f u");
                    // glass, grates, debris don't steal focus
                    ImGui::Checkbox("see through glass##depth of field", &g_dofSeeThrough);
                }

                ImGui::Label("range");
                SliderF("sharp range##depth of field", &g_dofRange, 0.f, 3000.f, "%.0f u");
                SliderF("blur ramp##depth of field", &g_dofRamp, 1.f, 4000.f, "%.0f u");

                ImGui::Label("blur");
                SliderF("max blur##depth of field", &g_dofMaxBlur, 0.f, 60.f, "%.1f px");
                SliderF("highlights##depth of field", &g_dofBokehBoost, 0.f, 16.f, "%.1f");
                SliderF("highlight cutoff##depth of field", &g_dofBokehThreshold, 0.f, 1.f, "%.2f");
                ImGui::Combo("quality##depth of field", &g_dofQuality, "16 taps\0" "32 taps\0" "64 taps\0" "128 taps\0");
                ImGui::Checkbox("smooth blur##depth of field", &g_dofSmooth);
                ImGui::Checkbox("blur near side##depth of field", &g_dofNearBlur);
                if (g_dofNearBlur)
                    ImGui::Checkbox("show near bleed##depth of field", &g_dofDebugNear);
                ImGui::Checkbox("keep viewmodel sharp##depth of field", &g_dofViewmodelSharp);

                ImGui::Label("debug");
                ImGui::Checkbox("show depth##depth of field", &g_dofDebugDepth);
                if (g_dofDebugDepth)
                    SliderF("white at##depth of field", &g_dofDebugWhite, 100.f, 8000.f, "%.0f u");
                ImGui::Checkbox("universal depth##depth of field", &g_fxUniversalDepth);
                ImGui::TextDisabled("depth: %s %s", ScreenFx_RouteName(), ScreenFx_VerifyName());

                // ours, after botox's rows
                SliderF("tap density##dof", &g_dofTapDensity, 0.5f, 2.5f, "%.2f /px");
                SliderF("particle guard##dof", &g_fxParticleGuard, 0.f, 1.f, "%.2f");
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("ambient occlusion", &g_aoEnable);
        if (g_aoEnable)
            ImGui::OptionPopup("ambient occlusion settings", [&]() {
                ImGui::Label("shading");
                SliderF("radius##ambient occlusion", &g_aoRadius, 4.f, 300.f, "%.0f u");
                SliderF("strength##ambient occlusion", &g_aoIntensity, 0.f, 4.f, "%.2f");
                SliderF("bias##ambient occlusion", &g_aoBias, 0.f, 0.8f, "%.2f");
                SliderF("blur##ambient occlusion", &g_aoBlur, 0.f, 3.f, "%.2f");

                ImGui::Label("range");
                SliderF("fade from##ambient occlusion", &g_aoFadeStart, 0.f, 8000.f, "%.0f u");
                SliderF("fade to##ambient occlusion", &g_aoFadeEnd, 0.f, 16000.f, "%.0f u");
                if (g_aoFadeEnd < g_aoFadeStart + 1.f) g_aoFadeEnd = g_aoFadeStart + 1.f;
                ImGui::Checkbox("fade under fog##ambient occlusion", &g_aoFogFade);
                // botox "fade under smoke": csgo smoke grenades, TF2 has none (brief rule 8)
                ImGui::Checkbox("skip viewmodel##ambient occlusion", &g_aoViewmodel);

                ImGui::Label("performance");
                SliderI("samples##ambient occlusion", &g_aoSamples, 4, 64);
                SliderF("render size##ambient occlusion", &g_aoScale, 0.25f, 1.f, "%.2f");

                ImGui::Label("debug");
                ImGui::Checkbox("show occlusion##ambient occlusion", &g_aoDebug);
            }, ImVec2(220.f, -1.f));

        // botox "motion blur" = Source's own DoImageSpaceMotionBlur, which TF2 still runs off
        // mat_motion_blur_* (screenfx.cpp FxMotionBlurTick). "roll intensity" is not a cvar in
        // TF2 (hardcoded 0.3, viewpostprocess.cpp:2718), so that row is left out.
        ImGui::Checkbox("motion blur", &g_mbEnable);
        if (g_mbEnable)
            ImGui::OptionPopup("motion blur settings", [&]() {
                ImGui::Checkbox("blur forward motion", &g_mbForward);
                SliderF("blur strength", &g_mbStrength, 0.f, 10.f, "%.2f");
                SliderF("rotation intensity", &g_mbRotation, 0.f, 8.f, "%.2f");
                SliderF("falling intensity", &g_mbFalling, 0.f, 8.f, "%.2f");
                SliderF("falling min", &g_mbFallMin, 0.f, 50.f, "%.2f");
                SliderF("falling max", &g_mbFallMax, 0.f, 50.f, "%.2f");
            }, ImVec2(220.f, -1.f));

        // our frame blending (frameblend.cpp) is botox's "true motion blur"
        ImGui::Checkbox("true motion blur", &g_fbEnable);
        if (g_fbEnable)
            ImGui::OptionPopup("true motion blur settings", [&]() {
                SliderF("shutter##true motion blur", &g_fbShutter, 0.f, 40.f, "%.1f ms");

                ImGui::Label("amount");
                SliderF("strength##true motion blur", &g_fbAmount, 0.f, 3.f, "%.2f");
                ImGui::Checkbox("blur players##true motion blur", &g_fbPlayers);
                SliderF("player trail##true motion blur", &g_fbObject, 0.f, 12.f, "%.2f");
                ImGui::Checkbox("blur viewmodel##true motion blur", &g_fbViewmodel);
                // 1 = true streak. gun only, never the world
                if (g_fbViewmodel)
                {
                    SliderF("viewmodel amount##true motion blur", &g_fbVmAmount, 0.f, 32.f, "%.2f");
                    SliderF("viewmodel sway##true motion blur", &g_fbVmSway, 0.f, 8.f, "%.2f");
                }

                ImGui::Label("quality");
                SliderI("max samples##true motion blur", &g_fbSamples, 4, 64);
                SliderF("max streak##true motion blur", &g_fbMaxStreak, 0.005f, 0.1f, "%.3f");
                // 1 = blur/smoothie frame blend, 2.2 = bright blend (linear light). higher = brights win
                SliderF("blur gamma##true motion blur", &g_fbGamma, 1.f, 10.f, "%.2f");
                ImGui::Checkbox("universal depth##true motion blur", &g_fxUniversalDepth);
                ImGui::TextDisabled("depth: %s %s", ScreenFx_RouteName(), ScreenFx_VerifyName());
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("bloom", &g_bloomEnable);
        if (g_bloomEnable)
            ImGui::OptionPopup("bloom settings", [&]() {
                SliderF("intensity##bloom", &g_bloomIntensity, 0.f, 3.f, "%.2f");
                // ours: a luma knee 0..1 (botox's is a 0..10 curve on its own bloom pyramid)
                SliderF("threshold##bloom", &g_bloomThreshold, 0.f, 1.f, "%.2f");
                SliderF("radius##bloom", &g_bloomRadius, 0.f, 1.f, "%.2f");
                SliderF("saturation##bloom", &g_bloomSat, 0.f, 3.f, "%.2f");
                SliderF("aspect ratio##bloom", &g_bloomAspect, -1.f, 1.f, "%.2f");
            }, ImVec2(220.f, -1.f));

        // botox menu_tab_visuals.cpp:1688, same spot (screen group, after bloom)
        ImGui::Checkbox("low fps viewmodel", &g_vmLowFps);
        if (g_vmLowFps)
            ImGui::OptionPopup("low fps viewmodel settings", [&]() {
                SliderF("fps##low fps viewmodel", &g_vmLowFpsRate, 15.f, 60.f, "%.0f fps");
            }, ImVec2(220.f, -1.f));

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
    if (menu_group_begin("hud"))
    {
        // ---- botox menu_tab_visuals.cpp "hud" group, botox order (unit hud, 11-01) ----------
        // skipped rows: force crosshair (CS:GO weapon_debug_spread_show; TF2 already draws its
        // crosshair on an unscoped sniper rifle), classic crosshair (outline) color (CS:GO's vgui
        // bar crosshair), default crosshair color, chud hud (removed), square
        // radar / hud color / keep killfeed / scaleform / web on surface / force hud update
        // (panorama + scaleform only), minecraft + call of duty 4 hud (cancelled).
        ImGui::Checkbox("sniper crosshair", &g_hudSniperCh);
        if (g_hudSniperCh)
            ImGui::OptionPopup("sniper crosshair settings", [&]() {
                ImGui::Combo("style##sniper crosshair style", &g_hudSniperChStyle, "clarity\0kamidere\0interwebz\0custom\0");
                const int style = g_hudSniperChStyle;
                ImGui::ColorRow("color##sniper crosshair color", g_hudSniperChColor);
                if (style == 3)
                {
                    SliderF("size##sniper crosshair", &g_hudSniperChSize, 0.f, 10.f, "%.1f");
                    SliderF("thickness##sniper crosshair", &g_hudSniperChThick, 0.f, 3.f, "%.1f");
                    SliderF("gap##sniper crosshair", &g_hudSniperChGap, -5.f, 5.f, "%.1f");
                    ImGui::Checkbox("dot##sniper crosshair", &g_hudSniperChDot);
                    ImGui::Checkbox("t style##sniper crosshair", &g_hudSniperChT);
                    ImGui::Checkbox("outline##sniper crosshair", &g_hudSniperChOutline);
                }
                if (style == 2)
                {
                    ImGui::ColorRow("dot color##sniper crosshair dot color", g_hudSniperChDotColor);
                    SliderF("size##sniper crosshair interwebz size", &g_hudSniperChIwSize, 0.5f, 4.f, "%.1fx");
                    SliderI("thickness##sniper crosshair interwebz thickness", &g_hudSniperChIwThick, 1, 8, "%d px");
                    SliderI("gap##sniper crosshair interwebz gap", &g_hudSniperChIwGap, 0, 20, "%d px");
                    ImGui::Checkbox("arm corner outline##sniper crosshair interwebz arm corners", &g_hudSniperChIwArmCorners);
                    ImGui::Checkbox("dot corner outline##sniper crosshair interwebz dot corners", &g_hudSniperChIwDotCorners);
                }
                if (style == 1 || style == 2 || (style == 3 && g_hudSniperChOutline))
                    ImGui::ColorRow("outline color##sniper crosshair outline color", g_hudSniperChOutlineColor);
                if (style == 3 && g_hudSniperChOutline)
                    SliderF("outline thickness##sniper crosshair", &g_hudSniperChOutlineThick, 0.1f, 3.f, "%.1f");
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("aimbot fov circle", &g_hudFovCircle);
        if (g_hudFovCircle)
            ImGui::OptionPopup("aimbot fov circle settings", [&]() {
                ImGui::ColorRow("color##aimbot fov circle", g_hudFovCircleColor);
                SliderF("thickness##aimbot fov circle", &g_hudFovCircleThick, 0.5f, 5.f, "%.1f");
                ImGui::Checkbox("only while aim key held##aimbot fov circle", &g_hudFovCircleKeyOnly);
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("custom net graph", &g_hudNg);
        if (g_hudNg)
            ImGui::OptionPopup("net graph settings", [&]() {
                ImGui::TextDisabled("shown with net_graph 1");
                ImGui::TextDisabled("font: fonts > net graph");
                static const char* const kNgItems[11] = { "fps", "frame var", "ping", "up rate", "loss", "choke",
                                                          "cmd rate", "tick", "sv time", "sv var", "server type" };
                ImGui::MultiCombo("shown##net graph", &g_hudNgItems, kNgItems, 11);
                ImGui::Checkbox("lerp##net graph", &g_hudNgLerp);
                ImGui::Checkbox("in / out rows##net graph", &g_hudNgInOut);
                ImGui::ColorRow("text color##net graph", g_hudNgColor);
                ImGui::Checkbox("warning colors##net graph", &g_hudNgWarn);
                if (g_hudNgWarn)
                {
                    ImGui::ColorRow("low / mid / high##net graph warn3", g_hudNgWarn3);
                    ImGui::SameLine();
                    ImGui::ColorRow("##net graph warn2", g_hudNgWarn2, 0, 1);
                    ImGui::SameLine();
                    ImGui::ColorRow("##net graph warn1", g_hudNgWarn1, 0, 2);
                }
                ImGui::Combo("text style##net graph", &g_hudNgTextStyle, "none\0shadow\0outline\0");
                if (g_hudNgTextStyle != 0)
                    ImGui::ColorRow("outline color##net graph", g_hudNgOutlineColor);
                ImGui::Checkbox("background##net graph", &g_hudNgBg);
                if (g_hudNgBg)
                {
                    ImGui::SameLine();
                    ImGui::ColorRow("##net graph background color", g_hudNgBgColor);
                }
                const ImVec2 ds = ImGui::GetIO().DisplaySize;
                ImGui::SliderInt("x##net graph", &g_hudNgX, -1, (int)ds.x, g_hudNgX < 0 ? "stock" : "%d px");
                ImGui::SliderInt("y##net graph", &g_hudNgY, -1, (int)ds.y, g_hudNgY < 0 ? "stock" : "%d px");
            }, ImVec2(220.f, -1.f));

        // ---- TF2 crosshair rows (ours, after botox's) -------------------------------------------
        // cog first, then the swatch one slot left of it, both on the feature row (SimpleVisual:
        // arrow then swatch). The swatch on its own row put slot 0 exactly under the cog, and the
        // swatch's click-release picker replaced the settings popup the cog opened on press.
        ImGui::Checkbox("crosshair##ch", &g_chEnable);
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
                ImGui::Checkbox("bug off players##ebplayers", &g_edgeBugPlayers);
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
                // botox menu_tab_movement.cpp:596-606
                ImGui::Combo("mode##abmode", &g_abMode, "drop in\0keep speed\0stop\0");
                SliderF("min speed gain", &g_abMinGain, 0.f, 500.f, "%.0f");
                SliderF("max sim reach##abreach", &g_abReach, 16.f, 512.f, "%.0f u");
                ImGui::Checkbox("auto jump", &g_abJump);
                ImGui::Checkbox("crouch jump##abcrouch", &g_abCrouch);
                ImGui::Checkbox("auto align##abalign", &g_abAlign);
                ImGui::Checkbox("visualize##abviz", &g_abViz);
                ImGui::ColorEdit4("visualize color##abvizcolor", g_abVizColor,
                    ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoOptions |
                    ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip |
                    ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoBorder);
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
                ImGui::Combo("style##asstyle", &g_asStyle, "cumidere\0botox\0aletherium\0");
                if (g_asStyle == 0)
                    return;   // cumidere: engine numbers only, no knobs
                if (g_asStyle == 2)
                {
                    SliderF("catch tolerance##asa", &g_asaCatchEps, 0.05f, 8.f, "%.2f");
                    SliderF("reach##asa", &g_asaReach, 16.f, 128.f, "%.0f");
                    SliderF("flush tolerance##asa", &g_asaFlushTol, 8.f, 96.f, "%.0f");
                    SliderF("drift gain##asa", &g_asaDriftGain, 4.f, 40.f, "%.0f");
                    ImGui::SliderInt("sim budget##asa", &g_asaSimBudget, 16, 128);
                    return;
                }
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
                ImGui::Checkbox("prevent slow", &g_wcPreventSlow);
                ImGui::Checkbox("crouch catches", &g_wcDuck);
                ImGui::Checkbox("visualize##wcviz", &g_wcViz);
                ImGui::ColorEdit4("visualize color##wcvizcolor", g_wcVizColor,
                    ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoOptions |
                    ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip |
                    ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoBorder);
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

        // same actions without a bind: cmd thread runs them next tick at your crosshair
        static const char* kPrBtn[5] = { "spawn enemy##prb", "spawn friendly##prb",
            "build sentry##prb", "build dispenser##prb", "build teleporter##prb" };
        for (int i = 0; i < 5; i++)
            if (ImGui::Button(kPrBtn[i], ImVec2(-1.f, 0.f)))
                g_prMenuReq.fetch_or(1 << i);
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
                ImGui::Checkbox("find jump surfs##pf", &g_psFinderJumpSurf);
                ImGui::Checkbox("ignore props##pf", &g_psFinderNoProps);
                ImGui::Checkbox("draw pixelsurf length##pf", &g_psFinderLength);
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

        // heroin pixeljump: no points, the wall next to you (key popup has "always on")
        ImGui::Checkbox("pixeljump", &g_pixelJump);
        if (g_pixelJump)
            ImGui::OptionPopup("pixeljump settings", [&]() {
                ImGui::Label("pixeljump keybind");
                ImGui::Keybind("pixeljump key", &g_pixelJumpKey, &g_pixelJumpMode);
                ImGui::Checkbox("readout", &g_pjReadout);
                ImGui::Checkbox("unsafe pixeljump", &g_pjUnsafe);
                ImGui::Checkbox("pixeljump crouch", &g_pjCrouch);
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
                ImGui::Checkbox("building calculator", &g_pcBuildings);
                if (g_pcBuildings)
                {
                    static const char* kBld[] = { "teleporter", "dispenser", "mini sentry", "sentry" };
                    ImGui::MultiCombo("##pcbuildings", &g_pcBuildMask, kBld, 4);
                }
            }, ImVec2(260.f, -1));

    }
    menu_group_end();

    menu_columns_end();
}

static void Page_MovementIndicators()
{
    // botox menu_tab_movement.cpp case 1: "indicators" | "detections"
    menu_columns_begin();

    if (menu_group_begin("indicators"))
    {
        // UNIT_MENU_SLOT movement_indicators
        UnitB_Menu_movement_indicators();
    }
    menu_group_end();

    menu_columns_next();

    if (menu_group_begin("detections"))
        UnitB_Menu_movement_detections();
    menu_group_end();

    menu_columns_end();
}

static void Page_MovementCalculator()
{
    // botox menu_tab_movement.cpp route_calc_page 1:1: "route calculator" + "keys" | "points".
    // TF2 has no ducked ground jump (tf_gamemovement.cpp:1220 "Cannot jump while ducked"), so the
    // point list has no "jump (ducked)" row; headbounce is not modelled. pixeljump = botox's lip
    // (routesim PT_PIXELJUMP), placed on a yellow finder dot
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
                    static const char* kTypeItems = "ground\0pixelsurf\0edgebug\0headbang\0texturebug\0pixeljump\0";
                    const int was = edit.type;
                    if (ImGui::Combo("point type", &edit.type, kTypeItems) && edit.type != was)
                        changed = true;
                    if (!edit.snapped && !g_rcAllowInvalid)
                        ImGui::TextDisabled("pixelsurf / texturebug / pixeljump: place it on a finder dot");

                    ImGui::Label("types to calculate");
                    // a pixel jump is PRESSED off like a floor, so it keeps its list (botox route_calc)
                    if (edit.type == 0 || edit.type == 5)
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

// botox menu_tab_movement.cpp points_page 1:1 ("lumi points page: global look | per kind design,
// preview right"). Vars cfg/u_points.h, look movement/points_style.h, drawn by render.cpp
static void Page_MovementPoints()
{
    constexpr ImGuiColorEditFlags alphaFlags = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
        ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_NoInputs |
        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar |
        ImGuiColorEditFlags_NoBorder;   // botox color_picker_alpha_flags
    static int selected = PS_STYLE_PS;

    menu_columns_begin();

    if (menu_group_begin("points"))
    {
        ImGui::Checkbox("render ps assist points", &g_pointsRenderPs);
        ImGui::Checkbox("render bounce assist points", &g_pointsRenderBounce);
        ImGui::Checkbox("render calculator points", &g_pointsRenderCalc);
        ImGui::Checkbox("render finder points", &g_pointsRenderFinder);
        ImGui::SliderFloat("points opacity", &g_pointsOpacity, 0.1f, 1.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("max render distance", &g_pointsMaxDist, 200.f, 4000.f, "%.0f u", ImGuiSliderFlags_AlwaysClamp);
        ImGui::Checkbox("distance fade", &g_pointsDistanceFade);
        ImGui::Checkbox("distance shrink", &g_pointsDistanceShrink);
        ImGui::Checkbox("radius color", &g_pointsRadiusColor);
        if (g_pointsRadiusColor)
        {
            ImGui::SameLine();
            ImGui::ColorEdit4("##points radius color", g_pointsRadiusClr, alphaFlags);
        }
        ImGui::Checkbox("delete points", &g_pointsDelete);
        if (g_pointsDelete)
            BindKeyNoMode("delete points key", &g_pointsDeleteKey);
    }
    menu_group_end();

    if (menu_group_begin("appearance"))
    {
        ImGui::SliderFloat("point size", &g_pointsSize, 0.5f, 3.f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("outline thickness", &g_pointsThickness, 0.5f, 4.f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("background opacity", &g_pointsFill, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("hover size", &g_pointsHoverScale, 1.f, 2.5f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("design size", &g_pointsDesignSize, 0.2f, 1.2f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
        ImGui::Checkbox("glow", &g_pointsGlow);
        ImGui::Checkbox("pulse in radius", &g_pointsPulse);
        ImGui::Checkbox("point labels", &g_pointsLabels);
    }
    menu_group_end();

    menu_columns_next();

    if (menu_group_begin("point design"))
    {
        static const char* names[PS_STYLE_COUNT] = {};
        if (!names[0])
            for (int s = 0; s < PS_STYLE_COUNT; ++s) names[s] = PointStyleGet(s).name;
        ImGui::Combo("point type", &selected, names, PS_STYLE_COUNT);
        const PointStyle& style = PointStyleGet(selected);

        // assist + calculator points follow the accent
        if (style.color)
            ImGui::ColorEdit4("point color", style.color, alphaFlags);

        if (style.gaps)
        {
            ImGui::Combo("outline shape", style.shape, kPointShapeNames, kPointShapeCount);
            static const char* kGaps[] = { "top", "bottom", "left", "right" };
            ImGui::MultiCombo("outline gaps", style.gaps, kGaps, 4);
        }

        if (style.design)
        {
            ImGui::Checkbox("draw design", style.design);
            if (*style.design)
                ImGui::Combo("design type", style.designType, kPointDesignNames, kPointDesignCount);
        }
    }
    menu_group_end();

    if (menu_group_begin("preview"))
    {
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 220.f));
        const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
        const float cx = (lo.x + hi.x) * 0.5f, cy = (lo.y + hi.y) * 0.5f - 8.f;
        const float r = std::clamp((std::min)(hi.x - lo.x, hi.y - lo.y) * 0.15f * g_pointsSize, 2.5f, (hi.y - lo.y) * 0.35f);
        PointStylePreview(selected, cx, cy, r);
        const char* label = PointStyleGet(selected).name;
        const ImVec2 textS = ImGui::CalcTextSize(label);
        ImGui::GetWindowDrawList()->AddText(ImVec2(std::floor(cx - textS.x * 0.5f), cy + r + 10.f), ImGui::GetColorU32(ImGuiCol_Text), label);
    }
    menu_group_end();

    menu_columns_end();
}

static const char* kMoveSub[5] = { "main", "indicators", "calculator", "recorder", "points" };
static int s_moveSub = 0;
static void Tab_MovementRoot()
{
    if      (s_moveSub == 0) Page_MovementMain();
    else if (s_moveSub == 1) Page_MovementIndicators();
    else if (s_moveSub == 2) Page_MovementCalculator();
    else if (s_moveSub == 3) Page_MovementRecorder();
    else                     Page_MovementPoints();
}

// ==== misc / fonts / settings helpers ===============================================
#include "fonts.h"
#include <functional>
#include <shellapi.h>   // ShellExecuteA -- settings "open folder" (WIN32_LEAN_AND_MEAN drops it)
#include "../misc/game_extras.h"   // unit game: chat translator / bot names / cvar list / image url box
#include "../misc/names.h"         // custom names: cycle lists + misc > player list (aliases)
#include "../misc/game_pure.h"     // n_game::SplitCvars -- cvars "add" splits on ';'
#include "../screen/blur_rect.h"   // BlurRectFade -- settings "background blur"

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

// botox icon_button: a 16px button with its glyph drawn on top (0 down, 1 up, 2 x)
static bool IconButton(const char* id, const int icon, const float height)
{
    const bool pressed = ImGui::Button(id, ImVec2(16.f, height));
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    const float cx = ImFloor((mn.x + mx.x) * 0.5f) + 0.5f, cy = ImFloor((mn.y + mx.y) * 0.5f) + 0.5f;
    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (icon == 2)
    {
        // close-small svg: 24 box, arms +-4.32 from center, stroke 2, square ends
        const float s = height / 24.f, arm = 4.32f * s;
        dl->AddLine(ImVec2(cx - arm, cy - arm), ImVec2(cx + arm, cy + arm), col, 2.f * s);
        dl->AddLine(ImVec2(cx + arm, cy - arm), ImVec2(cx - arm, cy + arm), col, 2.f * s);
    }
    else
    {
        const float tip = icon == 0 ? 2.f : -2.f;
        dl->AddLine(ImVec2(cx - 3.f, cy - tip), ImVec2(cx, cy + tip), col);
        dl->AddLine(ImVec2(cx, cy + tip), ImVec2(cx + 3.f, cy - tip), col);
    }
    return pressed;
}

// botox menu_internal.h safe_checkbox: safe mode greys it out and shows it unticked
static void SafeCheckbox(const char* label, bool* v)
{
    bool off = false;
    ImGui::BeginDisabled(g_safeMode);
    ImGui::Checkbox(label, g_safeMode ? &off : v);
    ImGui::EndDisabled();
    if (g_safeMode && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("safe mode enabled");
}

// botox tab_misc "convenience": one button per saved title/command, an x icon to drop it,
// "no buttons" when empty, a rule, then "add button" opening a "new button" popup. Slots
// instead of botox's one tab/newline string (config.cpp is one key=value per line).
static void ConvenienceButtons()
{
    static char s_title[32] = "", s_command[160] = "";
    static bool s_openAdd = false;
    int remove = -1, shown = 0;
    for (int i = 0; i < kConvButtons; ++i)
    {
        if (!g_convTitle[i][0] || !g_convCmd[i][0]) continue;
        ++shown;
        ImGui::PushID(i);
        // frames draw 2 px past bb, 7 px gap = same 3 px seam as cvars rows
        if (ImGui::Button(g_convTitle[i], ImVec2(ImGui::GetContentRegionAvail().x - 7.f - 16.f, 15.f)))
            MenuClientCmd(g_convCmd[i]);
        ImGui::SameLine(0.f, 7.f);
        if (IconButton("##delete", 2, 15.f)) remove = i;
        ImGui::PopID();
    }
    if (!shown) ImGui::TextDisabled("no buttons");

    // line spans frame edges (bb + 2 px)
    const ImVec2 line = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(line.x - 2.f, line.y), ImVec2(line.x + ImGui::GetContentRegionAvail().x + 2.f, line.y),
                                        ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::Dummy(ImVec2(0.f, 1.f));

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
            // unit wm: botox menu_tab_settings.cpp "watermark settings" 1:1 (rows, order, gates, ranges,
            // width 220). Vars hacks/menu/cfg/u_wm.h; drawing hud/watermark.cpp + watermark_styles.cpp.
            ImGui::OptionPopup("watermark settings", []() {
                constexpr ImGuiColorEditFlags alphaFlags = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
                    ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_NoInputs |
                    ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar |
                    ImGuiColorEditFlags_NoBorder;   // botox color_picker_alpha_flags
                void GifTex_PathInput(const char*, const char*, char*, size_t);   // hud/gif_tex.cpp

                int& style = g_wmStyle;
                if (style < 0 || style > 23) style = 0;
                ImGui::Combo("style##watermark style", &style,
                    "default\0kamibebra\0clarity\0clarity v2\0delusional\0interwebz\0havoc\0onetap\0airflow\0ev0lve\0legendware\0"
                    "interium\0skebob\0cumidere\0illusory\0lumi\0billware\0dna\0dna clarity\0cucumber\0howeweware\0gamesense\0heroin\0"
                    "lobotomy v2\0");

                ImGui::InputTextWithHint("name##watermark text", "botox", g_wmLabel, 25);

                if (style == 0 || style == 2 || style == 15) {
                    ImGui::Checkbox("user##watermark user", &g_wmUser);
                    ImGui::Checkbox("tickrate", &g_wmTickrate);
                    ImGui::Checkbox("fps", &g_wmShowFPS);
                    ImGui::Checkbox("time", &g_wmShowTime);
                }
                if (style == 20) {
                    ImGui::Checkbox("time##watermark howeweware", &g_wmShowTime);
                    ImGui::Checkbox("ping##watermark howeweware", &g_wmShowPing);
                    ImGui::Checkbox("tickrate##watermark howeweware", &g_wmTickrate);
                    ImGui::Checkbox("fps##watermark howeweware", &g_wmShowFPS);
                    ImGui::Checkbox("velocity##watermark howeweware", &g_wmVelocity);
                }
                if (style == 21) {
                    ImGui::Checkbox("user##watermark gamesense", &g_wmUser);
                    ImGui::Checkbox("fps##watermark gamesense", &g_wmShowFPS);
                    ImGui::Checkbox("ping##watermark gamesense", &g_wmShowPing);
                    ImGui::Checkbox("tickrate##watermark gamesense", &g_wmTickrate);
                    ImGui::Checkbox("velocity##watermark gamesense", &g_wmVelocity);
                    ImGui::Checkbox("time##watermark gamesense", &g_wmShowTime);
                    ImGui::Checkbox("gradient bar##watermark gamesense", &g_wmGsBar);
                    ImGui::ColorRow("game color##watermark gamesense", g_wmGsGameColor, alphaFlags);
                    ImGui::ColorRow("sense color##watermark gamesense", g_wmGsSenseColor, alphaFlags);
                    ImGui::SliderFloat("offset x##watermark gamesense", &g_wmGsX, -1000.f, 1000.f, "%.0f px");
                    ImGui::SliderFloat("offset y##watermark gamesense", &g_wmGsY, -500.f, 500.f, "%.0f px");
                }

                if (style == 22) {   // heroin-new watermark popup: show fps / show time / x, y offset
                    ImGui::Checkbox("show fps##watermark heroin", &g_wmShowFPS);
                    ImGui::Checkbox("show time##watermark heroin", &g_wmShowTime);
                    ImGui::SliderFloat("x offset##watermark heroin", &g_wmHeroinX, 0.f, 1.f, "%.3f");
                    ImGui::SliderFloat("y offset##watermark heroin", &g_wmHeroinY, 0.f, 1.f, "%.3f");
                }
                if (style == 23) {
                    ImGui::Checkbox("user##watermark lobotomy v2", &g_wmUser);
                    ImGui::Checkbox("tickrate##watermark lobotomy v2", &g_wmTickrate);
                    ImGui::Checkbox("fps##watermark lobotomy v2", &g_wmShowFPS);
                    ImGui::Checkbox("time##watermark lobotomy v2", &g_wmShowTime);
                    ImGui::ColorRow("title color##watermark lobotomy v2", g_wmLobotomyTitleColor, alphaFlags);
                }

                if (style == 12)
                    ImGui::Checkbox("typing effect##watermark skebob", &g_wmSkebobTyping);

                const bool labelStyle = style == 4 || style == 5 || style == 6 || style == 7 || style == 9 || style == 11 ||
                                        style == 13 || style == 14 || style == 16 || style == 17 || style == 18;
                int& label = g_wmLabelType;
                if (label < 0 || label > 3) label = 0;
                if (labelStyle)
                    ImGui::Combo("label##watermark label", &label, "time\0fps\0user\0none\0");

                const bool showsUser = ((style == 0 || style == 2 || style == 15 || style == 21 || style == 23) && g_wmUser) || style == 12 ||
                                       (style == 1 && (g_wmKamiShowFlags & 1)) || style == 3 || style == 8 || style == 10 ||
                                       style == 17 || style == 18 || style == 19 || style == 20 || (labelStyle && label == 2);
                if (showsUser)
                    ImGui::InputTextWithHint("user name##watermark clarity user", "user", g_wmUserName, sizeof(g_wmUserName));

                if (style == 9)
                    ImGui::SliderFloat("alpha override##watermark evolve", &g_wmEvolveAlpha, 0.f, 100.f, "%.0f%%");
                if (style == 6)
                    ImGui::ColorRow("blue color##watermark havoc", g_wmHavocColor, alphaFlags);
                if (style == 17) {
                    ImGui::Checkbox("detection glow##watermark dna", &g_wmDnaGlow);
                    if (g_wmDnaGlow) {
                        ImGui::SliderInt("glow ticks##watermark dna", &g_wmDnaGlowTicks, 1, 64);
                        ImGui::Checkbox("glow on spotted enemy##watermark dna", &g_wmDnaGlowSpotted);
                    }
                }
                if (style == 11)
                    ImGui::ColorRow("background color##watermark interium", g_wmInteriumBg, alphaFlags);

                if (style == 15) {
                    int& design = g_wmLumiDesign;
                    if (design < 0 || design > 1) design = 0;
                    ImGui::Combo("design##watermark lumi", &design, "original\0new\0");
                    ImGui::Checkbox("animated title##watermark lumi", &g_wmLumiAnimTitle);
                    ImGui::Checkbox("no border##watermark lumi", &g_wmLumiNoBorder);
                    ImGui::ColorRow("title color##watermark lumi", g_wmLumiTitleColor, alphaFlags);
                    ImGui::Checkbox("glow##watermark lumi", &g_wmLumiGlow);
                    if (g_wmLumiGlow) {
                        ImGui::SliderFloat("glow radius##watermark lumi", &g_wmLumiGlowRadius, 1.f, 15.f, "%.1f px");
                        ImGui::SliderFloat("glow opacity##watermark lumi", &g_wmLumiGlowOpacity, 0.f, 1.f, "%.2f");
                        ImGui::Checkbox("shadow color##watermark lumi", &g_wmLumiShadowColor);
                    }
                    ImGui::SliderFloat("offset x##watermark lumi", &g_wmLumiX, -500.f, 500.f, "%.0f px");
                    ImGui::SliderFloat("offset y##watermark lumi", &g_wmLumiY, -500.f, 500.f, "%.0f px");
                }

                ImGui::Checkbox("gif next to watermark##watermark gif", &g_wmGif);
                if (g_wmGif) {
                    int& gifType = g_wmGifType;
                    if (gifType < 0 || gifType > 2) gifType = 0;
                    ImGui::Combo("gif type##watermark gif type", &gifType, "original\0shit\0custom\0");
                    if (gifType == 2)
                        GifTex_PathInput("gif file path##watermark gif path", "C:\\mascot.gif or imgur link", g_wmGifPath, sizeof(g_wmGifPath));

                    int& gifSide = g_wmGifSide;
                    if (gifSide < 0 || gifSide > 2) gifSide = 0;
                    ImGui::Combo("gif side##watermark gif side", &gifSide, "auto\0left\0right\0");

                    ImGui::SliderFloat("gif scale##watermark gif", &g_wmGifScale, 25.f, 500.f, "%.0f%%");
                    ImGui::SliderFloat("gif rotation##watermark gif", &g_wmGifRotation, -180.f, 180.f, "%.0f deg");
                    ImGui::SliderFloat("gif gap##watermark gif", &g_wmGifGap, -50.f, 50.f, "%.0f px");
                    ImGui::SliderFloat("gif offset x##watermark gif", &g_wmGifOffsetX, -500.f, 500.f, "%.0f px");
                    ImGui::SliderFloat("gif offset y##watermark gif", &g_wmGifOffsetY, -500.f, 500.f, "%.0f px");
                    ImGui::SliderFloat("gif alpha##watermark gif", &g_wmGifAlpha, 0.f, 100.f, "%.0f%%");
                    ImGui::Checkbox("gif mirror##watermark gif", &g_wmGifFlip);
                }

                if (style != 1)
                    return;

                ImGui::CheckboxFlags("user##watermark kami", &g_wmKamiShowFlags, 1);
                ImGui::CheckboxFlags("time##watermark kami", &g_wmKamiShowFlags, 2);
                ImGui::CheckboxFlags("fps##watermark kami", &g_wmKamiShowFlags, 4);

                ImGui::Checkbox("bold font##watermark kami", &g_wmKamiBold);

                ImGui::Checkbox("custom text##watermark kami", &g_wmKamiCustomText);
                if (g_wmKamiCustomText)
                    ImGui::InputTextWithHint("##watermark kami text", "type shit here...", g_wmKamiText, sizeof(g_wmKamiText));

                ImGui::Checkbox("typing animation##watermark kami", &g_wmKamiTyping);
                if (g_wmKamiTyping) {
                    ImGui::SliderInt("typing speed##watermark kami", &g_wmKamiTypingSpeed, 1, 30, "%d chars/s");
                    ImGui::SliderInt("deleting speed##watermark kami", &g_wmKamiDeletingSpeed, 1, 30, "%d chars/s");
                    ImGui::SliderFloat("hold typed##watermark kami", &g_wmKamiTypingHold, 0.f, 10.f, "%.1f s");
                    ImGui::SliderFloat("hold deleted##watermark kami", &g_wmKamiDeletingHold, 0.f, 10.f, "%.1f s");
                }

                ImGui::Checkbox("mikudere type##watermark kami", &g_wmKamiMikudere);

                ImGui::Checkbox("custom rounding##watermark kami", &g_wmKamiRounding);
                if (g_wmKamiRounding)
                    ImGui::SliderFloat("rounding##watermark kami", &g_wmKamiRoundingValue, 0.f, 15.f, "%.1f");

                ImGui::Checkbox("outline##watermark kami", &g_wmKamiOutline);
                if (g_wmKamiOutline) {
                    ImGui::ColorRow("outline color##watermark kami", g_wmKamiOutlineColor, alphaFlags);
                    ImGui::Checkbox("rgb outline##watermark kami", &g_wmKamiRgb);
                    if (g_wmKamiRgb)
                        ImGui::SliderFloat("rgb speed##watermark kami", &g_wmKamiRgbSpeed, 0.1f, 5.f, "%.1f");
                }

                ImGui::Checkbox("text color like outline##watermark kami", &g_wmKamiTextLikeStroke);
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("spectator list", &g_specEnable);
        if (g_specEnable)
            ImGui::OptionPopup("spectators settings", []() {
                // botox menu_tab_settings.cpp spectators popup, rows / gating / order 1:1. Position =
                // drag the panel while the menu is open (RMB resets), same as the donor's drag.
                void SpecList_PathInput(const char* label, const char* hint, char* buf, size_t size);   // spectator_list.cpp
                constexpr ImGuiColorEditFlags kA = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoOptions |
                    ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip |
                    ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoBorder;
                const int cs = g_specStyle;
                const int lumiType = cs == 25 ? g_specLumiType : -1;

                if (cs == 0 || cs == 5 || cs == 6 || cs == 8 || cs == 10 || cs == 23 || cs == 24 || cs == 25 || cs == 26 || cs == 29 || cs == 31 || cs == 32)
                    ImGui::ColorRow("spectating local color##spectator list text color one", g_specColorOne, kA);
                if (cs == 9)
                    ImGui::ColorRow("list color##spectator list text color one", g_specColorOne, kA);
                if (cs == 27) {
                    ImGui::Checkbox("rainbow##spectatorlist inba", &g_specInbaRainbow);
                    if (g_specInbaRainbow)
                        ImGui::SliderFloat("rainbow speed##spectatorlist inba", &g_specInbaRainbowSpeed, 0.f, 5.f, "%.2f");
                    else
                        ImGui::ColorRow("text color##spectator list text color one", g_specColorOne, kA);
                }
                if (cs == 0 || cs == 5 || cs == 8 || cs == 23 || cs == 24 || cs == 26 || cs == 29 || cs == 30 || cs == 31 || cs == 32 || lumiType == 2)
                    ImGui::ColorRow("spectating other color##spectator list text color two", g_specColorTwo, kA);
                if (cs != 9 && cs != 12 && cs != 13 && cs != 14 && cs != 16 && cs != 18 && cs != 19 && cs != 20 && cs != 21 && cs != 24 &&
                    cs != 26 && cs != 28 && cs != 29 && cs != 30 && cs != 31 && (cs != 25 || lumiType == 1))
                    ImGui::Combo("listed players##spectatorlist player", &g_specListType, "all spectators\0local spectators\0");

                if (g_specStyle > 33 || g_specStyle < 0) g_specStyle = 0;
                ImGui::Combo("style##spectatorlist style", &g_specStyle,
                    "default\0delusional\0interwebz\0windows xp\0clarity\0chillware\0lobotomy v2\0kamibebra\0eyes\0cumhacck\0clarity v2\0"
                    "airplane\0bhop cheat\0millionware\0aimware\0havoc\0onetap\0airflow\0ev0lve\0legendware\0interium\0chillware v2\0"
                    "skebob\0cumidere\0illusory\0lumi\0billware\0inkabanium\0dna\0cucumber\0howeweware\0gamesense\0kamidere\0heroin\0");
                const int style = g_specStyle;

                if (style == 33) {   // heroin-new "speclist mode"
                    if (g_specHeroinType < 0 || g_specHeroinType > 2) g_specHeroinType = 1;
                    ImGui::Combo("speclist mode##spectatorlist heroin", &g_specHeroinType, "legacy\0regular\0interwebz\0");
                }
                if (style == 25) {
                    if (g_specLumiType < 0 || g_specLumiType > 2) g_specLumiType = 0;
                    ImGui::Combo("type##spectatorlist lumi type", &g_specLumiType, "default\0new\0interwebz\0");
                }
                if (style == 1 || style == 2 || style == 3 || style == 15 || style == 22 || style == 23 || style == 24 || style == 26 || style == 28 ||
                    style == 29 || style == 30 || style == 31 || style == 32 || (style == 25 && g_specLumiType != 1))
                    ImGui::Checkbox("show targets##spectatorlist targets", &g_specShowTarget);
                if (style == 22) {
                    ImGui::Checkbox("draw background##spectatorlist skebob", &g_specSkebobBg);
                    ImGui::Checkbox("draw title##spectatorlist skebob", &g_specSkebobTitle);
                }
                if ((style == 0 && g_specListType == 0) || style == 4 || style == 5 || style == 6 || style == 11 || style == 15 || style == 17 ||
                    style == 18 || style == 21 || style == 22 || style == 23 || style == 25 || style == 26 || style == 27 || style == 28 ||
                    style == 29 || style == 30 || style == 31 || style == 32)
                    ImGui::Checkbox("always show##spectatorlist always show", &g_specAlwaysShow);
                if (style == 31)
                    ImGui::Checkbox("gradient bar##spectatorlist gamesense", &g_specGamesenseBar);
                if (style == 32) {
                    // TF2 has SourceTV where CS:GO has GOTV (player_info_t ishltv either way)
                    ImGui::Checkbox("hide SourceTV##spectatorlist kamidere", &g_specKamiHideTv);
                    ImGui::Checkbox("observer mode##spectatorlist kamidere", &g_specBhopMode);
                    ImGui::Checkbox("custom title##spectatorlist kamidere", &g_specKbCustomTitle);
                    if (g_specKbCustomTitle)
                        ImGui::InputTextWithHint("title text##spectatorlist kamidere", "Spectators", g_specKbTitle, sizeof(g_specKbTitle));
                    ImGui::Checkbox("blur background##spectatorlist kamidere", &g_specKamiBlur);
                    ImGui::Checkbox("circle avatars##spectatorlist kamidere", &g_specKamiCircle);
                    if (!g_specKamiBlur)
                        ImGui::SliderFloat("list opacity##spectatorlist kamidere", &g_specKamiOpacity, 0.f, 100.f, "%.0f%%");
                }
                if (style == 18)
                    ImGui::SliderFloat("alpha override##spectatorlist evolve", &g_wmEvolveAlpha, 0.f, 100.f, "%.0f%%");
                if (style == 15)
                    ImGui::ColorRow("blue color##spectatorlist havoc", g_specHavocColor, kA);
                if (style == 20)
                    ImGui::ColorRow("background color##spectatorlist interium", g_specInteriumBg, kA);
                if (style == 25) {
                    ImGui::Checkbox("hide SourceTV##spectatorlist lumi", &g_specLumiHideTv);
                    if (lumiType == 0)
                        ImGui::Checkbox("glow##spectatorlist lumi", &g_specLumiGlow);
                    if (lumiType != 2)
                        ImGui::Checkbox("blur background##spectatorlist lumi", &g_specLumiBlur);
                    if (lumiType == 1) {
                        ImGui::Checkbox("mascot##spectatorlist lumi", &g_specLumiMascot);
                        if (g_specLumiMascot) {
                            SpecList_PathInput("mascot file path##spectatorlist lumi", "C:\\mascot.gif or imgur link", g_specLumiMascotPath, sizeof(g_specLumiMascotPath));
                            ImGui::SliderFloat("mascot size##spectatorlist lumi", &g_specLumiMascotSize, 20.f, 150.f, "%.0f px");
                        }
                    }
                }
                if (style == 1)
                    ImGui::Checkbox("left side##spectatorlist delusional left", &g_specDelusionalLeft);
                if (style == 5)
                    ImGui::SliderFloat("max width##spectatorlist chillware width", &g_specChillwareMaxWidth, 190.f, 600.f, "%.0f");
                if (style == 2) {
                    ImGui::SliderInt("width##spectatorlist width", &g_specListWidth, 195, 500);
                    ImGui::Checkbox("background##spectatorlist interwebz background", &g_specInterwebzBg);
                    ImGui::SliderInt("label offset##spectatorlist interwebz label x", &g_specInterwebzLabelX, -150, 150);
                }
                if (style == 7) {
                    ImGui::Checkbox("animate new entries##spectatorlist kamibebra animate", &g_specKbAnimate);
                    ImGui::Checkbox("dynamic list##spectatorlist kamibebra dynamic", &g_specKbDynamic);
                    ImGui::Checkbox("always show##spectatorlist kamibebra always show", &g_specKbAlwaysShow);
                    ImGui::Checkbox("bold font##spectatorlist kamibebra bold", &g_specKbBold);
                    ImGui::Checkbox("window shadow##spectatorlist kamibebra window shadow", &g_specKbWindowShadow);
                    ImGui::ColorRow("local color##spectatorlist kamibebra local color", g_specKbLocalColor, kA);
                    ImGui::ColorRow("other color##spectatorlist kamibebra other color", g_specKbOtherColor, kA);
                    ImGui::Checkbox("local shadow##spectatorlist kamibebra local shadow", &g_specKbLocalShadow);
                    if (g_specKbLocalShadow) {
                        ImGui::SameLine();
                        ImGui::ColorRow("##spectatorlist kamibebra local shadow color", g_specKbLocalShadowColor, kA);
                    }
                    ImGui::Checkbox("other shadow##spectatorlist kamibebra other shadow", &g_specKbOtherShadow);
                    if (g_specKbOtherShadow) {
                        ImGui::SameLine();
                        ImGui::ColorRow("##spectatorlist kamibebra other shadow color", g_specKbOtherShadowColor, kA);
                    }
                    ImGui::Checkbox("custom title##spectatorlist kamibebra custom title", &g_specKbCustomTitle);
                    if (g_specKbCustomTitle)
                        ImGui::InputTextWithHint("title text##spectatorlist kamibebra title", "Spectators", g_specKbTitle, sizeof(g_specKbTitle));
                    ImGui::Checkbox("bebra gif##spectatorlist kamibebra gif", &g_specKbGif);
                    if (g_specKbGif) {
                        ImGui::Combo("gif type##spectatorlist kamibebra gif type", &g_specKbGifType, "original\0shit\0custom\0");
                        if (g_specKbGifType == 2)
                            SpecList_PathInput("gif file path##spectatorlist kamibebra gif path", "C:\\mascot.gif or imgur link", g_specKbGifPath, sizeof(g_specKbGifPath));
                        ImGui::SliderFloat("gif size##spectatorlist kamibebra gif size", &g_specKbGifSize, 32.f, 128.f, "%.0f px");
                    }
                }
                if (style == 8) {
                    ImGui::SliderFloat("size##spectatorlist eyes size", &g_specEyesSize, 16.f, 64.f, "%.0f px");
                    ImGui::Checkbox("emoji##spectatorlist eyes emoji", &g_specEyesEmoji);
                }
                if (style == 11) {
                    ImGui::ColorRow("title color##spectatorlist airplane title", g_specAirplaneTitleColor, kA);
                    ImGui::ColorRow("gradient color##spectatorlist airplane gradient", g_specAirplaneGradientColor, kA);
                    ImGui::ColorRow("local color##spectatorlist airplane local", g_specAirplaneLocalColor, kA);
                    ImGui::ColorRow("other color##spectatorlist airplane other", g_specAirplaneOtherColor, kA);
                    ImGui::Checkbox("outline text##spectatorlist airplane outline", &g_specAirplaneOutline);
                    ImGui::Combo("title case##spectatorlist airplane title case", &g_specAirplaneTitleCase, "Spectators\0spectators\0SPECTATORS\0");
                }
                if (style == 12)
                    ImGui::Checkbox("observer mode##spectatorlist bhopcheat mode", &g_specBhopMode);
                if ((style < 13 && style != 8 && style != 9 && style != 11) || style == 22 || style == 31 || style == 32 ||
                    (style == 25 && g_specLumiType != 2))
                    ImGui::Checkbox("spectator avatars", &g_specAvatar);
                if (style == 10)
                    ImGui::Checkbox("always show##spectatorlist clarity v2", &g_specClarityAlwaysShow);
            }, ImVec2(220.f, -1.f));

        // ---- unit misc: botox menu_tab_settings.cpp 1589..1788, media player .. displayed logs ----
        ImGui::Checkbox("media player", &g_mpEnable);
        if (g_mpEnable)
            ImGui::OptionPopup("media player settings", []() {
                // botox position order (tr / tl / tc / bc / bl / br) over this dll's WatermarkPos
                static constexpr int kToWm[6] = { 2, 0, 1, 4, 3, 5 }, kFromWm[6] = { 1, 2, 0, 4, 3, 5 };
                int position = kFromWm[std::clamp(g_mpPos, 0, 5)];
                if (ImGui::Combo("position##media player position", &position, "top right\0top left\0top center\0bottom center\0bottom left\0bottom right\0"))
                    g_mpPos = kToWm[std::clamp(position, 0, 5)];

                ImGui::Checkbox("simple", &g_mpSimple);
                if (!g_mpSimple) {
                    ImGui::Checkbox("cover art", &g_mpCoverArt);
                    ImGui::Checkbox("background", &g_mpBackground);
                    ImGui::Checkbox("progress bar", &g_mpProgressBar);

                    ImGui::Checkbox("waveform", &g_mpVisualizer);
                    if (g_mpVisualizer) {
                        ImGui::SliderFloat("height##visualizer", &g_mpVisHeight, 4.f, 64.f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
                        ImGui::Checkbox("smooth##visualizer", &g_mpVisSmooth);
                        if (!g_mpVisSmooth)
                            ImGui::SliderInt("bar width##visualizer", &g_mpVisBarWidth, 1, 12, "%d px", ImGuiSliderFlags_AlwaysClamp);
                        ImGui::Checkbox("custom color##visualizer", &g_mpVisCustomColor);
                        if (g_mpVisCustomColor) {
                            ImGui::SameLine();
                            ImGui::ColorRow("##visualizer color", g_mpVisColor);
                        }
                    }
                }

                ImGui::Checkbox("lyrics", &g_mpLyrics);
                if (g_mpLyrics) {
                    ImGui::SliderInt("rows##lyrics", &g_mpLyricsRows, 1, 9, "%d", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::SliderFloat("offset##lyrics", &g_mpLyricsOffset, -3000.f, 3000.f, "%.0f ms", ImGuiSliderFlags_AlwaysClamp);
                }

                ImGui::Checkbox("precise time", &g_mpTime);

                ImGui::Checkbox("in game control", &g_mpIngameControl);
                if (g_mpIngameControl) {
                    int unused = 1;   // botox Keybind( ..., false ): key only, no style popup
                    ImGui::Label("previous song");
                    ImGui::Keybind("previous##media key", &g_mpPrevKey, &unused, false);
                    ImGui::Label("pause / play");
                    ImGui::Keybind("pause##media key", &g_mpToggleKey, &unused, false);
                    ImGui::Label("next song");
                    ImGui::Keybind("next##media key", &g_mpNextKey, &unused, false);
                }
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("points", &g_ptsEnable);
        if (g_ptsEnable)
            ImGui::OptionPopup("points settings", []() {
                ImGui::Checkbox("big font##points", &g_ptsBigFont);
                ImGui::SliderFloat("appear speed##points", &g_ptsAppearSpeed, 0.5f, 20.f, "%.2fx");
                ImGui::SliderFloat("bob speed##points", &g_ptsBobSpeed, 0.1f, 10.f, "%.2fx");
                ImGui::SliderFloat("x offset##points", &g_ptsOffsetX, -300.f, 300.f, "%.0f px");
                ImGui::SliderFloat("y offset##points", &g_ptsOffsetY, -50.f, 50.f, "%.0f px");
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("low fps warning", &g_fpsWarning);

        ImGui::Checkbox("hit marker", &g_hmEnable);
        if (g_hmEnable)
            ImGui::OptionPopup("hit marker settings", []() {
                ImGui::SliderFloat("size##hitmarkersize", &g_hmSize, 4.f, 30.f, "%.0f");
                ImGui::SliderFloat("speed##hitmarkerspeed", &g_hmSpeed, 0.25f, 3.f, "%.2f");
                ImGui::SliderFloat("thickness##hitmarkerthickness", &g_hmThickness, 0.5f, 4.f, "%.1f");
                ImGui::ColorRow("color##hitmarkercolor", g_hmColor);

                ImGui::Checkbox("outline##hitmarkeroutline", &g_hmOutline);
                if (g_hmOutline) {
                    ImGui::ColorRow("outline color##hitmarkeroutlinecolor", g_hmOutlineColor);
                    ImGui::SliderFloat("outline thickness##hitmarkeroutlinethickness", &g_hmOutlineThickness, 0.5f, 4.f, "%.1f");
                }
            }, ImVec2(220.f, -1.f));

        // botox "hit sound" row skipped: the user removed it (TF2 has its own hit sound)

        ImGui::Checkbox("healthshot effect", &g_hshotEnable);
        if (g_hshotEnable)
            ImGui::OptionPopup("healthshot effect settings", []() {
                static const char* const kTriggers[] = { "kill", "edgebug", "pixelsurf" };
                ImGui::MultiCombo("trigger on##healthshot", &g_hshotTriggers, kTriggers, 3);
                ImGui::SliderFloat("duration##healthshot", &g_hshotDuration, 0.1f, 5.f, "%.1f s");
            }, ImVec2(220.f, -1.f));

        /* where a player died; a pick the map never precached falls back to an energy splash.
           smoke / soul / bouncy balls / minecraft are our own particles, balls only on your own kills.
           melt is not ported (falls back to blood), so it shows no rows of its own */
        ImGui::Checkbox("death particles", &g_dpEnable);
        if (g_dpEnable)
            ImGui::OptionPopup("death particles settings", []() {
                ImGui::Combo("effect##deathparticles", &g_dpStyle,
                    "blood burst\0sparks\0confetti\0smoke puff\0explosion\0soul\0bouncy balls\0melt\0minecraft\0");

                const int type = g_dpStyle;
                if ((type < 5 && type != 3) || type == 7) return;

                if (type == 8)
                    ImGui::Combo("drops##deathparticles", &g_dpMcMode, "only x\0both x and xp\0only xp\0");

                if (type == 5)
                    ImGui::Combo("soul##deathparticles", &g_dpSoulStyle, "energy\0heavy electrical\0light electrical\0core\0");

                if (type != 3 && type != 8) {
                    // colour of the body part each particle comes from; colour below = fallback when that part is off screen / behind a wall
                    ImGui::Checkbox("model colors##deathparticles", &g_dpModelColors);
                    ImGui::Checkbox("multicolor##deathparticles", &g_dpMulticolor);
                    if (!g_dpMulticolor)
                        ImGui::ColorRow("color##deathparticles", g_dpColor);
                    else {
                        int alpha = (int)(g_dpColor[3] * 255.f + 0.5f);
                        if (ImGui::SliderInt("alpha##deathparticles", &alpha, 0, 255, "%d", ImGuiSliderFlags_AlwaysClamp))
                            g_dpColor[3] = (float)alpha / 255.f;
                    }
                }
                if (type == 5) {
                    ImGui::Checkbox("glow##deathparticles", &g_dpGlow);
                    ImGui::Checkbox("stars##deathparticles", &g_dpStars);
                }

                if (type == 6)
                    ImGui::SliderInt("balls##deathparticles", &g_dpAmount, 5, 100, "%d", ImGuiSliderFlags_AlwaysClamp);
                else if (type != 8)
                    ImGui::SliderInt("amount##deathparticles", &g_dpAmount, 1, 40, "%d per bone", ImGuiSliderFlags_AlwaysClamp);

                ImGui::SliderFloat("size##deathparticles", &g_dpSize, 0.5f, type == 6 ? 4.f : 3.f, "%.1fx", ImGuiSliderFlags_AlwaysClamp);
                if (type != 8)
                    ImGui::SliderFloat("lifetime##deathparticles", &g_dpLifetime, 1.f, 8.f, "%.1f s", ImGuiSliderFlags_AlwaysClamp);
                else if (g_dpMcMode != 0)
                    ImGui::SliderFloat("xp stays##deathparticles", &g_dpMcXpTime, 1.f, 60.f, "%.0f s", ImGuiSliderFlags_AlwaysClamp);
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("console color", &g_consoleTintOn);
        if (g_consoleTintOn) {
            ImGui::SameLine();
            ImGui::ColorRow("##console color", g_consoleTint);
        }

        {
            static const char* const kLogs[] = { "damage", "votes" };
            ImGui::MultiCombo("displayed logs", &g_logTypes, kLogs, 2);
        }

        // TF2: killfeed filter (no botox row; keep killfeed is CS:GO only)
        ImGui::Checkbox("only your killfeed", &g_selfKillfeedOnly);
        // ---- unit misc end ----
    }
    menu_group_end();

    menu_columns_next();

    if (menu_group_begin("game"))
    {
        // botox game group 1:1 first. Left out: clantag, auto revive, deagle spinner (CS:GO
        // only), chat images (TF2 chat lines are vgui RichText: text + colour runs, no image),
        // insecure bypass (CS:GO trusted mode; TF2 has no -allow_third_party_software check),
        // random rank + medal (TF2's scoreboard draws neither). TF2-only rows after.
        ImGui::Checkbox("chat translator", &g_chatTranslator);
        if (g_chatTranslator)
            ImGui::OptionPopup("chat translator settings", []() {
                ImGui::Combo("translate to##chat translator", &g_chatTranslatorLang, kChatLanguageNames);
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("bot names", &g_botNames);
        if (g_botNames)
            ImGui::OptionPopup("bot names settings", []() {
                const int mode = g_botNamesMode;
                ImGui::Combo("names##botnames", &g_botNamesMode, "original\0same name\0random string\0steam names\0");
                if (mode == 1)
                    ImGui::InputText("name##botnames", g_botNamesSame, sizeof(g_botNamesSame));
                else if (mode == 2)
                    ImGui::SliderInt("length##botnames", &g_botNamesLength, 1, 31);   // TF2 names are 32 bytes
                else if (mode == 3)
                {
                    char status[96];
                    BotNames_Status(status, sizeof(status));
                    ImGui::Label("%s", status);
                }
                ImGui::Checkbox("remove bot prefix##botnames", &g_botNamesNoPrefix);
                ImGui::Checkbox("random ping##botnames", &g_botNamesPing);
                if (ImGui::Button("randomize##botnames", ImVec2(-1.f, 15.f)))
                    BotNames_Randomize();
            }, ImVec2(220.f, -1.f));

        // fpsoverlay names tab 1:1 (hacks/misc/names.cpp), split over two cogs. Other players'
        // own names (fpsoverlay aliases) = misc > player list.
        static auto namesCycleRows = []() {
            // fpsoverlay CYCLE section: shown whenever any cycle is active
            const bool anyCycle = (g_useCustomUsername && g_enableNameCycle)
                || (g_streamerMode == 2 && (g_enableTeamLabelCycle || g_enableEnemyLabelCycle))
                || (g_streamerMode == 3 && g_enableEveryoneLabelCycle);
            if (!anyCycle) return;
            ImGui::Combo("cycle on##names", &g_nameTrigger, "timer\0on kill\0");
            if (g_nameTrigger == 0)
                ImGui::SliderFloat("interval##names", &g_nameInterval, 0.1f, 60.f, "%.1f s");
        };

        ImGui::Checkbox("custom name", &g_useCustomUsername);
        if (g_useCustomUsername)
            ImGui::OptionPopup("custom name settings", []() {
                ImGui::InputText("your name##customname", g_customUsername, sizeof(g_customUsername));
                ImGui::Checkbox("cycle name##customname", &g_enableNameCycle);
                if (g_enableNameCycle)
                    Names_EntryList("##names", g_nameList, "+ add name");
                namesCycleRows();
            }, ImVec2(220.f, -1.f));

        {
            static int s_lastMode = 2;   // checkbox brings back the last mode picked
            bool streamer = g_streamerMode != 0;
            if (g_streamerMode != 0) s_lastMode = g_streamerMode;
            if (ImGui::Checkbox("streamer mode", &streamer))
                g_streamerMode = streamer ? s_lastMode : 0;
        }
        if (g_streamerMode != 0)
            ImGui::OptionPopup("streamer mode settings", []() {
                int shown = g_streamerMode - 1;
                if (ImGui::Combo("others shown as##streamer", &shown, "their class\0team labels\0one label\0"))
                    g_streamerMode = shown + 1;
                if (g_streamerMode == 2)
                {
                    ImGui::InputText("teammate label##streamer", g_streamerTeamLabel, sizeof(g_streamerTeamLabel));
                    ImGui::Checkbox("cycle teammate label##streamer", &g_enableTeamLabelCycle);
                    if (g_enableTeamLabelCycle)
                        Names_EntryList("##teamlabels", g_teamLabelList, "+ add label");
                    ImGui::InputText("enemy label##streamer", g_streamerEnemyLabel, sizeof(g_streamerEnemyLabel));
                    ImGui::Checkbox("cycle enemy label##streamer", &g_enableEnemyLabelCycle);
                    if (g_enableEnemyLabelCycle)
                        Names_EntryList("##enemylabels", g_enemyLabelList, "+ add label");
                }
                else if (g_streamerMode == 3)
                {
                    ImGui::InputText("everyone label##streamer", g_streamerEveryoneLabel, sizeof(g_streamerEveryoneLabel));
                    ImGui::Checkbox("cycle everyone label##streamer", &g_enableEveryoneLabelCycle);
                    if (g_enableEveryoneLabelCycle)
                        Names_EntryList("##evlabels", g_everyoneLabelList, "+ add label");
                }
                namesCycleRows();
            }, ImVec2(220.f, -1.f));

        SafeCheckbox("sv_pure bypass", &g_pureBypass);

        ImGui::Checkbox("performance", &g_perfEnable);
        if (g_perfEnable)
            ImGui::OptionPopup("performance settings", []() {
                ImGui::Checkbox("threaded bone setup", &g_perfThreadedBones);
                ImGui::Checkbox("preload assets", &g_perfForcePreload);
                ImGui::Checkbox("multicore rendering", &g_perfMulticore);
                ImGui::Checkbox("high priority", &g_perfHighPriority);
                ImGui::Checkbox("game on fast cores", &g_perfFastCores);
                ImGui::Checkbox("full fps when tabbed out", &g_perfNoFocusSleep);
                ImGui::Checkbox("1ms timer", &g_hiTimerRes);   // TF2-only row
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("discord rpc", &g_discordEnable);
        if (g_discordEnable)
            ImGui::OptionPopup("discord rpc settings", []() {
                ImagePick_Input("image url##discord rpc", "https://i.imgur.com/KDUzwZf.png", g_discordImageKey,
                                sizeof(g_discordImageKey), true);
            }, ImVec2(220.f, -1.f));

        ImGui::Checkbox("debugger menu", &g_debugMenuEnable);

        // ---- TF2-only rows
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
        ImGui::Checkbox("backpack expander", &g_backpackExpander);
        ImGui::Checkbox("sv_cheats bypass", &g_cheatsBypass);
    }
    menu_group_end();

    if (menu_group_begin("convenience"))
    {
        ConvenienceButtons();

        // TF2-only, after botox's rows: the matchmaker fixes as fixed buttons
        const float gap  = ImGui::GetStyle().ItemSpacing.x;
        const float half = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
        if (ImGui::Button("fix stuck matchmaker", ImVec2(half, 15.f)))
            MMFix_Run();
        ImGui::SameLine(0.f, gap);
        if (ImGui::Button("abandon match (GC)", ImVec2(-1.f, 15.f)))
            MMFix_Abandon();
        ImGui::TextDisabled("%s", g_mmFixReady ? g_mmFixStatus : "sigs not resolved");
    }
    menu_group_end();

    menu_columns_end();

    // UNIT_MENU_SLOT misc_main
    UnitE_Menu_misc_main();
}

// botox menu_tab_settings.cpp cvars_page 1:1: one full-height "cvars" box, input + "add" on
// top (';' splits into rows), a rule, the rows (edit / down / up / x) in their own scrolling
// child, "execute" at the bottom. Rows run top to bottom on map load / server join
// (game_extras.cpp). 16 slots of g_cvarRows instead of botox's one '\n' string.
static void Page_MiscCvars()
{
    static char s_input[512] = "";
    static int s_count = 0;
    static uint32_t s_synced = 0;
    auto rowsHash = []() {
        uint32_t h = 2166136261u;
        for (int i = 0; i < kCvarRows; ++i)
            for (const char* c = g_cvarRows[i]; ; ++c) { h = (h ^ (uint8_t)*c) * 16777619u; if (!*c) break; }
        return h | 1u;
    };
    // a config load changed the rows under us: botox re-splits list_var when it != synced
    if (rowsHash() != s_synced)
    {
        s_count = 0;
        for (int i = 0; i < kCvarRows; ++i) if (g_cvarRows[i][0]) s_count = i + 1;
        s_synced = rowsHash();
    }

    // custom InputText spans to WorkRect.Max.x, only a table cell narrows it; it also pushes itself 10 px down, frame 17 tall.
    // button frame = bb + 2 px each side
    constexpr float kButton = 16.f, kGap = 7.f, kSide = kButton * 3.f + kGap * 2.f + 4.f, kFrame = 17.f, kInputLift = 10.f;
    const float spacing = ImGui::GetStyle().WindowPadding.x;

    auto beginRowTable = [&](const char* id) {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ImGui::GetStyle().CellPadding.x, 0.f));
        const bool open = ImGui::BeginTable(id, 2);
        ImGui::PopStyleVar();
        if (!open) return false;
        ImGui::TableSetupColumn("##text", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##side", ImGuiTableColumnFlags_WidthFixed, kSide);
        return true;
    };
    auto nextRow = [&](float height) {
        ImGui::TableNextRow(ImGuiTableRowFlags_None, height);
        ImGui::TableNextColumn();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - kInputLift);
    };
    auto sideColumn = []() {
        ImGui::TableNextColumn();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.f);
    };
    bool changed = false, execute = false;

    if (menu_group_begin("cvars", 0.f, ImGui::GetContentRegionAvail().y - 20.f))   // 20 = the title band above the box
    {
        const float top = ImGui::GetCursorPosY();

        if (beginRowTable("##cvar add"))
        {
            nextRow(kFrame);
            const bool enter = ImGui::InputTextWithHint("##cvar input", "sensitivity 1; noclip; mp_timelimit 999", s_input,
                                                        sizeof(s_input), ImGuiInputTextFlags_EnterReturnsTrue);
            if (enter) ImGui::SetKeyboardFocusHere(-1);
            sideColumn();
            const bool add = ImGui::Button("add", ImVec2(-1.f, kFrame - 4.f));
            ImGui::EndTable();

            if (enter || add)
            {
                for (const std::string& cmd : n_game::SplitCvars(s_input, ';'))
                    if (s_count < kCvarRows) strncpy_s(g_cvarRows[s_count++], cmd.c_str(), _TRUNCATE);
                s_input[0] = '\0';
                changed = true;
            }
        }

        const float lineY = top + kFrame + spacing;
        ImGui::SetCursorPosY(lineY);
        const ImVec2 line = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(line, ImVec2(line.x + ImGui::GetContentRegionAvail().x, line.y),
                                            ImGui::GetColorU32(ImGuiCol_Separator));
        ImGui::SetCursorPosY(lineY + 1.f + spacing);

        // bottom = execute: standard 15 px button, frame 19
        if (ImGui::BeginChild("##cvar rows", ImVec2(0.f, -(15.f + 4.f + spacing)), false, 0))
        {
            int moveFrom = -1, moveTo = -1, remove = -1;
            if (!s_count)
                ImGui::TextDisabled("no commands, run top to bottom on map load / server join");
            else if (beginRowTable("##cvar list"))
            {
                for (int i = 0; i < s_count; ++i)
                {
                    ImGui::PushID(i);
                    nextRow(kFrame + spacing);
                    changed |= ImGui::InputText("##command", g_cvarRows[i], sizeof(g_cvarRows[i]));
                    sideColumn();
                    ImGui::BeginDisabled(i == s_count - 1);
                    if (IconButton("##down", 0, kFrame - 4.f)) moveFrom = i, moveTo = i + 1;
                    ImGui::EndDisabled();
                    ImGui::SameLine(0.f, kGap);
                    ImGui::BeginDisabled(i == 0);
                    if (IconButton("##up", 1, kFrame - 4.f)) moveFrom = i, moveTo = i - 1;
                    ImGui::EndDisabled();
                    ImGui::SameLine(0.f, kGap);
                    if (IconButton("##delete", 2, kFrame - 4.f)) remove = i;
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (moveFrom >= 0)
            {
                char tmp[sizeof(g_cvarRows[0])];
                memcpy(tmp, g_cvarRows[moveFrom], sizeof(tmp));
                memcpy(g_cvarRows[moveFrom], g_cvarRows[moveTo], sizeof(tmp));
                memcpy(g_cvarRows[moveTo], tmp, sizeof(tmp));
                changed = true;
            }
            if (remove >= 0)
            {
                for (int i = remove; i + 1 < kCvarRows; ++i) memcpy(g_cvarRows[i], g_cvarRows[i + 1], sizeof(g_cvarRows[i]));
                g_cvarRows[kCvarRows - 1][0] = '\0';
                --s_count;
                changed = true;
            }
        }
        ImGui::EndChild();

        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetItemRectMax().y + spacing + 2.f));
        ImGui::BeginDisabled(s_count == 0);
        execute = ImGui::Button("execute", ImVec2(-1.f, 15.f));
        ImGui::EndDisabled();
    }
    menu_group_end();

    if (changed) s_synced = rowsHash();
    if (execute) GameExtras_ExecCvars();
}

// botox misc pages: main, player list, cvars (botox order).
// botox misc > player list slot: here it holds custom names' per-player aliases (fpsoverlay
// names tab alias list), the page botox fills with revive / aimbot picks.
static void Page_MiscPlayerList()
{
    if (menu_group_begin("player list", 0.f, ImGui::GetContentRegionAvail().y - 20.f))   // 20 = the title band above the box
    {
        if (ImGui::BeginChild("##player list rows", ImVec2(0.f, 0.f), false, 0))
            Names_PlayerListMenu();
        ImGui::EndChild();
    }
    menu_group_end();
}

static const char* kMiscSub[3] = { "main", "player list", "cvars" };
static int s_miscSub = 0;
static void Tab_MiscRoot()
{
    if (s_miscSub == 1) { Page_MiscPlayerList(); return; }
    if (s_miscSub == 2) { Page_MiscCvars(); return; }
    Page_MiscMain();
}

// ==== skins =========================================================================
// Every widget here edits one SkinCfg in g_skinCfgs, keyed by the selected weapon's
// REDIRECT index. No widget asks for an update: the FSN pass diffs every SkinCfg each
// frame (skins.cpp DiffSkinCfgs, skins_pure.h SkinPure_Classify) and asks the server for
// a full update only when the edit needs the weapon rebuilt -- a material override and a
// style skin are only read off the item definition when the entity is created.

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

    if (ImGui::BeginCombo("weapon##skwpn", k_wpnList[s_wpnSel].name))
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
    if (ImGui::BeginCombo("paint##skwarp", k_warpaints[s_warpSel].name))
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
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // labels go on the widget itself, like every other tab: a separate TextDisabled line plus
    // the fork's own 10px input lead left these rows spaced unlike the combos around them.
    // step 0 = no +/- buttons; the fork's input fills the row, so they only sat clipped off it.
    if (ImGui::InputInt("paint kit id##skkit", &cfg.paintKitId, 0, 0))
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
    }

    // The band name alone is not a usable slider format: ImGui rounds the dragged value by
    // formatting it and PARSING THE RESULT BACK, so a format with no conversion specifier
    // reads back as nothing and the widget shows an empty bar. Keep the number in it.
    SliderF("wear##skwear", &cfg.wear,  0.f, 1.f,
            cfg.wear < 0.07f ? "factory new %.2f"  :
            cfg.wear < 0.15f ? "minimal wear %.2f" :
            cfg.wear < 0.38f ? "field-tested %.2f" :
            cfg.wear < 0.45f ? "well-worn %.2f"    : "battle scarred %.2f");

    ImGui::InputScalar("pattern seed##skpat", ImGuiDataType_U64, &cfg.pattern);
    if (!hasPaint) { ImGui::EndDisabled(); ImGui::TextDisabled("(this weapon takes no warpaint)"); }

    // ---- killstreak ----------------------------------------------------------------
    Section("killstreak");
    {
        static const char* const kTier[]  = { "none","basic","specialized","professional" };
        static const char* const kSheen[] = { "none","team shine","deadly daffodil","manndarin",
                                              "mean green","agonizing emerald","villainous violet","hot rod" };
        static const char* const kEff[]   = { "none","fire horns","cerebral discharge","tornado",
                                              "flames","singularity","incinerator","hypno-beam" };
        ImGui::Combo("tier##sktier", &cfg.ksTier, kTier, 4);
        if (cfg.ksTier >= 2) {
            ImGui::Combo("sheen##sksheen", &cfg.ksSheen, kSheen, 8);
            ImGui::Checkbox("custom color", &cfg.ksCustomColor);
            if (ImGui::IsItemHovered())
                MenuTooltip("recolors BOTH the sheen and the eye glow: the two read one row of\n"
                            "the same table in client.dll, so the row the sheen above picks is\n"
                            "rewritten with this color while the weapon is out. with no sheen\n"
                            "picked it uses team shine's row. other players holding that same\n"
                            "sheen are recolored too -- it is one table for the whole client.");
            if (cfg.ksCustomColor) {
                ImGui::SameLine();   // swatch on the checkbox's row, as the hud tab does
                ImGui::ColorEdit3("##skkscol", cfg.ksColor,
                                  ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            }
        }
        if (cfg.ksTier == 3) {
            ImGui::Combo("kill effect##skkseff", &cfg.ksEffect, kEff, 8);
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

        if (ImGui::BeginCombo("unusual effect##skpart", preview))
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
        if (ImGui::InputInt("particle id##skpartid", &cfg.unusual, 0, 0) && cfg.unusual < -1)
            cfg.unusual = -1;
    }

    // ---- variant -------------------------------------------------------------------
    Section("variant");
    {
        if (!we.canAustralia) ImGui::BeginDisabled();
        ImGui::Checkbox("australium", &cfg.australium);
        if (!we.canAustralia) ImGui::EndDisabled();

        // The lights belong to the def the weapon ends up WEARING (ResolveWeaponTarget's order),
        // and the Festive variants and most reskins have none -- k_festiveDefs is the schema's say.
        const int wornDef = (cfg.festive && we.festiveIdx > 0) ? we.festiveIdx
                          : (cfg.reskinDef > 0)                 ? cfg.reskinDef
                          : (cfg.weaponSkinDef > 0)             ? cfg.weaponSkinDef
                          :                                       we.redir;
        const bool canFest = Skin_DefHasFestive(wornDef);
        if (!canFest) ImGui::BeginDisabled();
        // On = attr 2053 + one attachment rebuild, next frame (skins.cpp RefreshAttachmentModels).
        // Off still needs the full update: the attribute can't be taken back off the list.
        ImGui::Checkbox("festivized", &cfg.festivized);
        if (!canFest) ImGui::EndDisabled();
        if (!canFest && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            MenuTooltip(wornDef == we.redir
                ? "this weapon has no festivizer lights in the game's schema."
                : "the picked festive / reskin / warpaint variant has no festivizer lights.\n"
                  "clear it to festivize the base weapon.");

        const bool hasFestive = (we.festiveIdx > 0);
        if (!hasFestive) ImGui::BeginDisabled();
        ImGui::Checkbox("festive", &cfg.festive);
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
                { cfg.reskinDef = 0; cfg.reskinStyle = 0; }
            if (noneSel) ImGui::SetItemDefaultFocus();
            if (rg) for (int i = 0; i < rg->count; i++) {
                const bool sel = (cfg.reskinDef   == rg->variants[i].defindex
                               && cfg.reskinStyle == rg->variants[i].style);
                if (ImGui::Selectable(rg->variants[i].name, sel)) {
                    cfg.reskinDef   = rg->variants[i].defindex;
                    cfg.reskinStyle = rg->variants[i].style;
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
            if (ImGui::InputInt("kills##skstatk", &cfg.statKills, 0, 0) && cfg.statKills < 0)
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

    // ---- networking (skinshare.cpp) -- Nikogram's page: share / show, steam p2p, no backend --
    Section("networking");
    ImGui::Checkbox("enabled##shnet", &g_shareNet);
    if (ImGui::IsItemHovered())
        MenuTooltip("peer to peer over steam, between botox users on this server. probes every\n"
                    "player every 10 s; only ready, consenting peers exchange cosmetics.\n"
                    "no server, no account, nothing leaves your steam connection.");
    ImGui::Checkbox("share my cosmetics", &g_shareSend);
    if (ImGui::IsItemHovered())
        MenuTooltip("peers that want them get your skinchanger + hats loadout.");
    ImGui::Checkbox("show shared cosmetics", &g_shareRecv);
    if (ImGui::IsItemHovered())
        MenuTooltip("peers' loadouts are drawn on their players (skinchanger and hats must be on).");
    ImGui::TextDisabled("%s", SkinShare_Status());
}

// ---- inventory: hats page (hats.cpp) -------------------------------------------------------
// lbox's Hats page shape: a searchable item list on the right ("Find item..."), the worn
// slots with style / unusual / paint on the left.
static void Page_Hats()
{
    static int  s_sel = 0;                 // worn slot being edited
    static char s_find[64] = {};
    static bool s_classOnly = true;
    static char s_partFilter[64] = {};
    static std::vector<int> s_list;        // k_hats indices passing the filter
    static char s_lastFind[64] = { 1 };
    static int  s_lastCls = -1;
    static bool s_lastClassOnly = false;

    const int cls = Hats_LocalClass();
    const int filtCls = s_classOnly ? cls : 0;
    if (strcmp(s_lastFind, s_find) != 0 || s_lastCls != filtCls || s_lastClassOnly != s_classOnly) {
        strcpy_s(s_lastFind, s_find); s_lastCls = filtCls; s_lastClassOnly = s_classOnly;
        s_list.clear();
        for (int i = 0; i < Hats_Count(); ++i) {
            const HatDef* d = Hats_At(i);
            if (filtCls && !(d->classMask >> filtCls & 1)) continue;
            if (Hat_NameMatch(d->name, s_find)) s_list.push_back(i);
        }
    }

    menu_columns_begin();

    if (menu_group_begin("hats"))
    {
        ImGui::Checkbox("enabled##hats", &g_hatsEnable);
        if (ImGui::IsItemHovered())
            MenuTooltip("client-side cosmetics on your own player. only you see them,\n"
                        "and only in third person / demos, like your real hats.\n"
                        "pick a slot, then click an item in the list to put it there.\n"
                        "tf2 rules: a change shows at your next spawn or resupply, and with\n"
                        "'show in backpack' the hat is worn only where you equipped it.");
        ImGui::Checkbox("show in backpack##hatsbp", &g_hatsBackpack);
        if (ImGui::IsItemHovered())
            MenuTooltip("each slot's hat is an item in tf2's own backpack and item lists\n"
                        "(client-side only, the item server never hears of it). equip it in\n"
                        "tf2's loadout screen like any hat: worn only on the classes you\n"
                        "equipped it on, the real hat in that slot is unequipped, equip\n"
                        "regions conflict as usual. off = no backpack copy, worn on every\n"
                        "class that can wear it.");
        if (ImGui::BeginListBox("##hatslots", ImVec2(-1.f, 8 * ImGui::GetTextLineHeightWithSpacing() + 4.f)))
        {
            for (int i = 0; i < kHatSlots; ++i) {
                const HatDef* d = Hats_Find(g_hatSlots[i].def);
                char lbl[160];
                snprintf(lbl, sizeof(lbl), "%d  %s##hs%d", i + 1, d ? d->name : "(empty)", i);
                if (ImGui::Selectable(lbl, s_sel == i)) s_sel = i;
            }
            ImGui::EndListBox();
        }

        HatSlot& s = g_hatSlots[s_sel];
        const HatDef* d = Hats_Find(s.def);
        if (d) {
            if (cls && !(d->classMask >> cls & 1)) ImGui::TextDisabled("(not for your current class)");
            if (g_hatsBackpack) {
                // worn only where tf2's loadout screen equipped it (hats_inv.cpp hook), like a real hat
                char on[96] = {};
                for (int k = 1; k <= 9; ++k)
                    if ((s.eqMask & 1) && (s.eqMask >> k & 1)) { strcat_s(on, on[0] ? ", " : ""); strcat_s(on, Hat_ClassName(k)); }
                ImGui::TextDisabled("loadout: %s", on[0] ? on : "not equipped (equip it in tf2's loadout)");
            }
            if (d->nStyles > 1) {
                if (s.style < 0 || s.style >= d->nStyles) s.style = 0;
                if (ImGui::BeginCombo("style##hatstyle", d->styles[s.style].name)) {
                    for (int k = 0; k < d->nStyles; ++k)
                        if (ImGui::Selectable(d->styles[k].name, s.style == k)) s.style = k;
                    ImGui::EndCombo();
                }
            }
            char prev[96];
            if (s.unusual < 0) snprintf(prev, sizeof(prev), "none");
            else snprintf(prev, sizeof(prev), "%s (%d)", Particle_Name(s.unusual), s.unusual);
            if (ImGui::BeginCombo("unusual effect##hatunu", prev)) {
                ImGui::InputText("##hatpartfilter", s_partFilter, sizeof(s_partFilter));
                ImGui::Separator();
                if (ImGui::Selectable("none", s.unusual < 0)) s.unusual = -1;
                for (int g = 0; g < k_particleGroupCount; ++g) {
                    const ParticleGroup& pg = k_particleGroups[g];
                    bool header = false;
                    for (int i = 0; i < pg.count; ++i) {
                        const ParticleEntry& pe = pg.items[i];
                        if (pe.id < 0 || !Hat_NameMatch(pe.name, s_partFilter)) continue;
                        if (!header) { ImGui::Separator(); ImGui::TextDisabled("%s", pg.name); header = true; }
                        char lbl[96]; snprintf(lbl, sizeof(lbl), "%s (%d)##hp%d_%d", pe.name, pe.id, g, i);
                        if (ImGui::Selectable(lbl, s.unusual == pe.id)) s.unusual = pe.id;
                    }
                }
                ImGui::EndCombo();
            }
            if (!d->paintable) ImGui::BeginDisabled();
            const HatPaint* cur = Hats_Paint(s.paint);
            if (ImGui::BeginCombo("paint##hatpaint", cur ? cur->name : "none")) {
                if (ImGui::Selectable("none", !cur)) s.paint = -1;
                for (int i = 0; i < Hats_PaintCount(); ++i)
                    if (ImGui::Selectable(Hats_Paint(i)->name, s.paint == i)) s.paint = i;
                ImGui::EndCombo();
            }
            if (!d->paintable) { ImGui::EndDisabled(); ImGui::TextDisabled("(not paintable)"); }
            if (ImGui::Button("remove##hatrm", ImVec2(-1.f, 15.f))) s = { 0, 0, -1, -1 };
        } else {
            ImGui::TextDisabled("empty slot -- pick an item on the right");
        }
    }
    menu_group_end();

    menu_columns_next();

    if (menu_group_begin("find item"))
    {
        ImGui::InputText("search##hatfind", s_find, sizeof(s_find));
        ImGui::Checkbox("current class only", &s_classOnly);
        ImGui::TextDisabled("%d items", (int)s_list.size());
        if (ImGui::BeginListBox("##hatlist", ImVec2(-1.f, 300.f)))
        {
            ImGuiListClipper clip;
            clip.Begin((int)s_list.size());
            while (clip.Step())
                for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                    const HatDef* d = Hats_At(s_list[r]);
                    char lbl[160]; snprintf(lbl, sizeof(lbl), "%s (%d)##hl%d", d->name, d->def, r);
                    if (ImGui::Selectable(lbl, g_hatSlots[s_sel].def == d->def))
                        g_hatSlots[s_sel] = { d->def, 0, g_hatSlots[s_sel].unusual, g_hatSlots[s_sel].paint };
                }
            ImGui::EndListBox();
        }
    }
    menu_group_end();

    menu_columns_end();
}

static const char* kInvSub[2] = { "weapons", "hats" };
static int s_invSub = 0;
static void Tab_InventoryRoot()
{
    if (s_invSub == 1) { Page_Hats(); return; }
    Tab_Skins();
}

// botox tab_fonts: one page per face, "<face> font" settings | "font list". botox pages
// indicators / esp map to TF2 (fonts.h); its chud hud / default hud pages rewrite CS:GO's
// panorama fonts.conf and have no TF2 equivalent. Applies live: render.cpp rebuilds the atlas
// (indicators) and aim/helpers.cpp recreates the vgui face (esp) once the pick settles.
static const char* kFontsSub[3] = { "indicators", "esp", "net graph" };   // botox case 4 = net graph (unit hud)
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
    else if (s_fontsSub == 2)   // unit hud: custom net graph face (hud_extra.cpp rebuilds the atlas)
    {
        static ImGuiTextFilter s_ngFilter;
        FontPage("net graph font", g_hudNgFontName, sizeof(g_hudNgFontName), &g_hudNgFontSize, &g_hudNgFontFlags,
                 HudFonts_ResetNetGraph, Fonts_FileNames(), s_ngFilter);
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

// ==== settings > menu: theme dropdown ============================================
// Look only -- colours, borders, rounding, fonts. Never layout, never animation, never accent.
struct MenuStyleRow
{
    const char* name;
    ImU32 window, band, child, frame, outline, windowOutline, popup, text, textDim;
    float windowRounding, frameRounding, popupRounding;
    ImFont* (*regFont)();    // nullptr = leave the chrome font alone (default style)
    ImFont* (*boldFont)();
};

static const MenuStyleRow k_menuStyles[] = {
    { "default", IM_COL32(10, 10, 10, 255), IM_COL32(25, 25, 25, 255), IM_COL32(15, 15, 15, 255), IM_COL32(25, 25, 25, 255),
      IM_COL32(50, 50, 50, 100), IM_COL32(50, 50, 50, 255), IM_COL32(20, 20, 20, 255), IM_COL32(255, 255, 255, 255),
      IM_COL32(128, 128, 128, 255), 5.f, 2.f, 3.f, nullptr, nullptr },
    { "kamibebra", IM_COL32(13, 13, 13, 255), IM_COL32(16, 16, 16, 255), IM_COL32(13, 13, 13, 255), IM_COL32(16, 16, 16, 255),
      IM_COL32(33, 33, 33, 255), IM_COL32(11, 11, 11, 255), IM_COL32(13, 13, 13, 255), IM_COL32(200, 200, 200, 255),
      IM_COL32(150, 150, 150, 255), 6.f, 4.f, 6.f, []() -> ImFont* { return g_fontKamibebra13; }, []() -> ImFont* { return g_fontKamibebraBold13; } },
    { "clarity v2", IM_COL32(23, 23, 23, 255), IM_COL32(28, 28, 28, 255), IM_COL32(23, 23, 23, 255), IM_COL32(28, 28, 28, 255),
      IM_COL32(55, 55, 55, 255), IM_COL32(7, 7, 7, 255), IM_COL32(23, 23, 23, 255), IM_COL32(255, 255, 255, 255),
      IM_COL32(100, 100, 100, 255), 5.f, 5.f, 5.f, []() -> ImFont* { return g_fontClarityInter; }, []() -> ImFont* { return g_fontInterBold15; } },
    { "lobotomy v1", IM_COL32(15, 15, 15, 255), IM_COL32(13, 13, 13, 255), IM_COL32(15, 15, 15, 255), IM_COL32(13, 13, 13, 255),
      IM_COL32(30, 30, 30, 255), IM_COL32(30, 30, 30, 255), IM_COL32(15, 15, 15, 255), IM_COL32(255, 255, 255, 255),
      IM_COL32(200, 200, 200, 255), 0.f, 0.f, 0.f, []() -> ImFont* { return g_fontMenuTahoma9; }, []() -> ImFont* { return g_fontMenuTahomaBd9; } },
    { "lobotomy v2", IM_COL32(7, 7, 7, 255), IM_COL32(10, 10, 10, 255), IM_COL32(10, 10, 10, 255), IM_COL32(25, 25, 25, 255),
      IM_COL32(25, 25, 25, 255), IM_COL32(25, 25, 25, 255), IM_COL32(7, 7, 7, 255), IM_COL32(255, 255, 255, 255),
      IM_COL32(180, 180, 180, 255), 6.f, 2.f, 4.f, []() -> ImFont* { return g_fontLobotomy13; }, []() -> ImFont* { return g_fontLobotomyBold13; } },
    { "dna", IM_COL32(9, 9, 9, 240), IM_COL32(9, 9, 9, 240), IM_COL32(20, 20, 20, 153), IM_COL32(13, 13, 13, 255),
      IM_COL32(26, 26, 26, 178), IM_COL32(255, 255, 255, 8), IM_COL32(20, 20, 20, 255), IM_COL32(242, 242, 242, 255),
      IM_COL32(77, 77, 77, 217), 4.f, 2.f, 4.f, []() -> ImFont* { return g_fontDnaMontserrat15; }, []() -> ImFont* { return g_fontDnaPtRootBold15; } },
    // heroin dll c_menu::render FUN_18004ce90 + style apply FUN_18004ea40, fresh-inject cfg (.data):
    // window = inner_color, "outer" child = outer_color, frame = inner (checkbox grad outer->inner),
    // Border = menu_color @ .25, main window border = menu_color, tab text 120 idle. Stock style = no rounding.
    // One font for everything: render loop FUN_18004ee80 pushes DAT_18035b6e8 (AddFontDefault =
    // ProggyClean 13) before the menu. arialbd 16 is only the music display's.
    { "heroin", IM_COL32(37, 39, 48, 255), IM_COL32(42, 44, 56, 255), IM_COL32(42, 44, 56, 255), IM_COL32(37, 39, 48, 255),
      IM_COL32(82, 161, 148, 64), IM_COL32(82, 161, 148, 255), IM_COL32(37, 39, 48, 255), IM_COL32(255, 255, 255, 255),
      IM_COL32(120, 120, 120, 255), 0.f, 0.f, 0.f, []() -> ImFont* { return g_fontHeroinProggy13; }, []() -> ImFont* { return g_fontHeroinProggy13; } },
};
static const int k_menuStyleCount = (int)(sizeof(k_menuStyles) / sizeof(k_menuStyles[0]));

static void MenuStyleApply(const MenuStyleRow& s)
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg]         = ImGui::ColorConvertU32ToFloat4(s.window);
    style.Colors[ImGuiCol_ChildBg]          = ImGui::ColorConvertU32ToFloat4(s.child);
    style.Colors[ImGuiCol_PopupBg]          = ImGui::ColorConvertU32ToFloat4(s.popup);
    style.Colors[ImGuiCol_Text]             = ImGui::ColorConvertU32ToFloat4(s.text);
    style.Colors[ImGuiCol_TextDisabled]     = ImGui::ColorConvertU32ToFloat4(s.textDim);
    style.Colors[ImGuiCol_MenuBand]         = ImGui::ColorConvertU32ToFloat4(s.band);
    style.Colors[ImGuiCol_MenuFrame]        = ImGui::ColorConvertU32ToFloat4(s.frame);
    style.Colors[ImGuiCol_MenuOutline]      = ImGui::ColorConvertU32ToFloat4(s.outline);
    style.Colors[ImGuiCol_MenuWindowOutline]= ImGui::ColorConvertU32ToFloat4(s.windowOutline);
    style.WindowRounding = s.windowRounding;
    style.FrameRounding  = s.frameRounding;
    style.PopupRounding  = s.popupRounding;
    // null = font not loaded (system calibri missing): keep the verdana pair, chrome calls CalcTextSize on it
    if (ImFont* f = s.regFont ? s.regFont() : nullptr)   g_menuReg  = f;
    if (ImFont* f = s.boldFont ? s.boldFont() : nullptr) g_menuBold = f;
}

// botox tab_settings: col1 "config" + "system", col2 "visuals config" + "menu". Left out, no
// TF2 counterpart: "botox network" (no TF2 server).
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
    }
    menu_group_end();

    menu_columns_next();

    if (menu_group_begin("visuals config"))
        ConfigPanel(true);
    menu_group_end();

    if (menu_group_begin("menu"))
    {
        if (g_menuStyle < 0 || g_menuStyle >= k_menuStyleCount) g_menuStyle = 0;
        const auto styleName = [](void*, int index, const char** out) { *out = k_menuStyles[index].name; return true; };
        ImGui::Combo("theme", &g_menuStyle, styleName, nullptr, k_menuStyleCount);

        ImGui::ColorRow("accent color##accent color", g_accentColor, kPickerNoAlpha);

        // botox menu_tab_settings: edits a copy, commits while not held -- the whole menu
        // rescales under the cursor otherwise (render/dpi.cpp)
        {
            static float s_dpiEdit = 100.f;
            static bool s_dpiActive = false;
            if (!s_dpiActive) s_dpiEdit = g_dpiScale;
            ImGui::SliderFloat("dpi scale", &s_dpiEdit, 25.f, 200.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
            s_dpiActive = ImGui::IsItemActive();
            if (!s_dpiActive) g_dpiScale = s_dpiEdit;

            ImGui::Checkbox("dpi scale panels", &g_dpiScalePanels);
        }

        ImGui::Checkbox("background blur", &g_menuBlur);
        if (g_menuBlur)
            ImGui::OptionPopup("background blur settings##menu blur settings", []() {
                ImGui::SliderFloat("max blur##menu blur", &g_menuBlurAmount, 0.f, 100.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("darken##menu blur", &g_menuBlurDarken, 0.f, 100.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
            }, ImVec2(220.f, -1.f));

        // botox 95ef94a: menu key lives here; none / lmb / rmb fall back to insert (RenderMenu)
        ImGui::Label("menu key");
        int menuKey = g_menuKey;
        int dummyMode = 1;
        ImGui::Keybind("menu key##menu key", &menuKey, &dummyMode, false);
        g_menuKey = menuKey;
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
    { kTabNames[TAB_MOVEMENT],  kMoveSub,    5 },
    { kTabNames[TAB_MISC],      kMiscSub,    3 },
    { kTabNames[TAB_INVENTORY], kInvSub,     2 },
    { kTabNames[TAB_FONTS],     kFontsSub,   3 },
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
            g_ejectRequest = true;
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

// botox 01e09de: blur on the background list, fading at the menu's speed. Crossfaded through
// BlurRectFade's alpha like botox's tint. The blur samples the backbuffer when its callback
// replays, so only what the background list drew BEFORE this call blurs: render.cpp calls it
// after esp + world fx, before the HUD panels (spec list, watermark...), which stay sharp.
// true = it blurred or darkened this frame (render.cpp then keeps the crosshair over it).
bool RenderMenuBlur()
{
    ImGuiIO& io = ImGui::GetIO();
    static float s_blurFade = 0.f;
    const float step = (std::min)(io.DeltaTime, 1.f / 30.f) / 0.12f;   // menu_anim_duration
    const bool want = g_showMenu && g_menuBlur;
    s_blurFade = (g_ejectRequest || g_eject) ? 0.f : ImClamp(s_blurFade + (want ? step : -step), 0.f, 1.f);
    if (s_blurFade <= 0.f) return false;

    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    const float amount = ImClamp(g_menuBlurAmount, 0.f, 100.f) / 100.f;
    const float darken = ImClamp(g_menuBlurDarken, 0.f, 100.f) / 100.f;
    if (amount > 0.f) BlurRectFade(bg, ImVec2(0.f, 0.f), io.DisplaySize, amount, s_blurFade);
    if (darken > 0.f)
        bg->AddRectFilled(ImVec2(0.f, 0.f), io.DisplaySize, IM_COL32(0, 0, 0, (int)(darken * s_blurFade * 255.f + 0.5f)));
    return amount > 0.f || darken > 0.f;
}

void RenderMenu()
{
    ImGuiIO& io = ImGui::GetIO();

    // live accent (settings tab RowColor edits g_accentColor directly; this is the one
    // place it is pushed into the style every widget already reads it through)
    ImGui::GetStyle().Colors[ImGuiCol_Accent] =
        ImVec4(g_accentColor[0], g_accentColor[1], g_accentColor[2], g_accentColor[3]);

    // dpi scale: render/dpi.cpp (logical layout + vertex scale + font twins), nothing here.

    // botox menu.cpp on_end_scene, verbatim (botox_menu_spec.md section 1): fixed
    // 750 x (640 - (menu_band_height - menu_bottom_band_height)) = 750x627, centred once,
    // never resized by the user. m_anim_progress is the open/close fade (section 7); while
    // it has not reached 1 the window eats no input, same as botox's eject-safety gate.
    if (g_ejectRequest || g_eject) s_animProgress = 0.f;   // eject snaps shut, never fades under a release
    else
    {
        const float step = (std::min)(io.DeltaTime, 1.f / 30.f) / 0.12f;   // menu_anim_duration
        s_animProgress = ImClamp(s_animProgress + (g_showMenu ? step : -step), 0.f, 1.f);
    }

    // botox menu_key(): none / lmb / rmb would lock the menu shut or toggle it on every click
    if (!(g_menuKey > VK_RBUTTON && g_menuKey < 256)) g_menuKey = VK_INSERT;

    if (!g_showMenu && s_animProgress <= 0.f) return;

    // menu style: colours/borders/rounding/fonts only. Restore once this window is built --
    // HUD panels elsewhere read GetStyle() (rounding, colours) and must never see the swap.
    struct MenuStyleScope
    {
        ImGuiStyle m_saved     = ImGui::GetStyle();
        ImFont*    m_savedReg  = g_menuReg;
        ImFont*    m_savedBold = g_menuBold;
        MenuStyleScope()
        {
            if (g_menuStyle < 0 || g_menuStyle >= k_menuStyleCount) g_menuStyle = 0;
            MenuStyleApply(k_menuStyles[g_menuStyle]);
            ImGui::PushFont(g_menuReg);
        }
        ~MenuStyleScope()
        {
            ImGui::PopFont();
            ImGui::GetStyle() = m_saved;
            g_menuReg  = m_savedReg;
            g_menuBold = m_savedBold;
        }
    } styleScope;

    // the main viewport, not io.DisplaySize: windows lay out in logical (dpi) px, io.DisplaySize
    // is the screen outside NewFrame (render/dpi.cpp)
    const ImVec2 view = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(view.x * 0.5f, view.y * 0.5f), ImGuiCond_Once, ImVec2(0.5f, 0.5f));
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

    // ---- botox on_end_scene order: top band, accent line under the band, bottom band,
    // window border, accent line on bottom band top edge. Lines = RenderFadedGradientLine (bright
    // centre, fading to both ends), full window width.
    // Bands + border each get botox's own PushClipRect(.., false): the window's clip rect is
    // inset WindowPadding.x/2 = 4px per side, which cut the bands and the side border short.
    dl->PushClipRect(wp, ImVec2(wp.x + wz.x, wp.y + mf::kHeaderH), false);
    dl->AddRectFilled(wp, ImVec2(wp.x + wz.x, wp.y + mf::kHeaderH), kBandBg(), rnd, ImDrawFlags_RoundCornersTop);
    dl->PopClipRect();
    RenderFadedGradientLine(dl, ImVec2(wp.x, wp.y + mf::kHeaderH - 1.f), ImVec2(wz.x, 1.f), ImColor(Accent()));

    const float botY = wp.y + wz.y - 12.f;   // menu_bottom_band_height
    dl->PushClipRect(ImVec2(wp.x, botY), ImVec2(wp.x + wz.x, wp.y + wz.y), false);
    dl->AddRectFilled(ImVec2(wp.x, botY), ImVec2(wp.x + wz.x, wp.y + wz.y), kBandBg(), rnd, ImDrawFlags_RoundCornersBottom);
    dl->PopClipRect();
    // border after both bands: bottom fill drawn over it hid the outline
    dl->PushClipRect(wp, ImVec2(wp.x + wz.x, wp.y + wz.y), false);
    dl->AddRect(wp, ImVec2(wp.x + wz.x, wp.y + wz.y), kWindowBorder(), rnd);
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
                      : (i == (int)TAB_INVENTORY) ? s_invSub
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
    case TAB_INVENTORY: Tab_InventoryRoot();  break;
    case TAB_FONTS:     Tab_Fonts();          break;
    case TAB_SETTINGS:  Tab_Settings();       break;
    }
    FlowEnd();

    ImGui::EndChild();
    ImGui::PopID();
    ImGui::Unindent(mf::kTabW + st.ItemSpacing.x);

    ImGui::End();
}
