#include "watermark_styles.h"
#include "watermark.h"            // g_wmBox
#include "../misc/unitE_logic.h"
#include "avatar_cache.h"
#include "avatar_pure.h"   // n_avatar::FriendIdToSteam64
#include "gif_tex.h"
#include "../screen/blur_rect.h"
#include "../../dependencies/data/airflow_data.h"   // airflow watermark logo icon
#include "../../dependencies/imgui/imgui_internal.h"   // ImDrawListSharedData (evolve shadow corners), IM_PI
#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <utility>
#include <vector>

// Shared field list every style draws from (feedback-new-wm-spec-same-knobs: one knob set,
// every style reskins it) -- label/fps/ping/loss/time, same toggles as watermark.cpp's own
// default body. withFps=false for the styles that draw fps in their own slot (clarity v2).
static int BuildFields(char field[5][64], bool withFps = true)
{
    int nf = 0;
    auto add = [&](const char* fmt, auto... args) {
        if (nf >= 5) return;
        snprintf(field[nf], 64, fmt, args...);
        if (field[nf][0]) ++nf;
    };
    if (g_wmShowLabel) add("%s", g_wmLabel[0] ? g_wmLabel : "botox");
    if (g_wmShowFPS && withFps) add("%d fps", (int)(g_fps.load() + 0.5f));
    if (g_wmShowPing && g_conn.load()) add("%d ms", g_ping.load());
    if (g_wmShowLoss && g_conn.load()) add("%d%% loss", g_loss.load());
    if (g_wmShowTime) {
        time_t t = time(nullptr); struct tm lt; localtime_s(&lt, &t);
        add("%02d:%02d", lt.tm_hour, lt.tm_min);
    }
    return nf;
}

static void JoinFields(char out[320], const char field[5][64], int nf, const char* sep)
{
    out[0] = 0;
    for (int i = 0; i < nf; ++i) { if (i) strcat_s(out, 320, sep); strcat_s(out, 320, field[i]); }
}

static ImU32 PackColor(const float c[4])
{
    return IM_COL32((int)(c[0] * 255.f), (int)(c[1] * 255.f), (int)(c[2] * 255.f), (int)(c[3] * 255.f));
}
// botox accent_u32(alpha): where the donor reads ImGuiCol_Accent, the watermark text colour stands in
static ImU32 AccentA(int a) { return (PackColor(g_wmTextColor) & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT); }
static ImU32 Accent() { return AccentA(255); }
static const char* Brand() { return g_wmLabel[0] ? g_wmLabel : "botox"; }
static float TextW(ImFont* f, float px, const char* s) { return f->CalcTextSizeA(px, FLT_MAX, 0.f, s).x; }
// botox vgui_text_width / vgui_shadow_text: floored width, floored pos, shadow = black at the colour's alpha
static float VguiW(ImFont* f, const char* s) { return floorf(TextW(f, f->FontSize, s)); }
static void VguiShadowText(ImDrawList* dl, ImFont* f, ImVec2 pos, ImU32 color, const char* s)
{
    const ImVec2 at(floorf(pos.x), floorf(pos.y));
    dl->AddText(f, f->FontSize, ImVec2(at.x + 1.f, at.y + 1.f), color & IM_COL32_A_MASK, s);
    dl->AddText(f, f->FontSize, at, color, s);
}
static void SetBox(float x0, float y0, float x1, float y1) { g_wmBox = ImVec4(x0, y0, x1, y1); }

// style 1: kamibebra -- misc.cpp draw_watermark_kamibebra(). rubik 13, dark rounded box,
// fixed top-right (donor ignores its own position setting too), text centred, extra fields in
// a dim grey after " | " separators, the fps NUMBER in the main colour. Width eases toward its
// target at 12/s so it never pops. Donor defaults kept: regular face, no outline, rounding 4;
// typing / rgb-cycle / custom-rounding knobs are menu-only extras, skipped.
static void DrawKamibebra(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = g_fontKamibebra13 ? g_fontKamibebra13 : ImGui::GetFont();
    const float fs = font->FontSize;

    char field[5][64]; char extra[320] = {};
    const int nf = BuildFields(field);
    JoinFields(extra, field, nf, " | ");

    char full[384]; snprintf(full, sizeof(full), extra[0] ? "%s | %s" : "%s", Brand(), extra);

    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.f, full);
    constexpr float minWidth = 95.f, height = 28.f, padX = 14.f;

    static float curWidth = minWidth;
    const float target = nf > 0 ? (std::max)(ts.x + padX * 2.f, minWidth) : minWidth;
    curWidth += (target - curWidth) * (12.f * io.DeltaTime);

    const float x = io.DisplaySize.x - curWidth - 8.f, y = 8.f;
    SetBox(x, y, x + curWidth, y + height);

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + curWidth, y + height), IM_COL32(16, 16, 16, 255), 4.f);

    const ImU32 main = PackColor(g_wmTextColor);
    constexpr ImU32 info = IM_COL32(146, 144, 145, 255);

    float tx = x + (curWidth - ts.x) * 0.5f;
    const float ty = y + (height - ts.y) * 0.5f - 1.f;
    const auto put = [&](const char* s, ImU32 c) { dl->AddText(font, fs, ImVec2(tx, ty), c, s); tx += TextW(font, fs, s); };
    put(Brand(), main);
    for (int i = 0; i < nf; ++i) {
        put(" | ", info);
        const char* fps = strstr(field[i], " fps");
        if (fps && g_wmShowFPS) {   // "123 fps": number main, unit info (donor)
            char num[64]; snprintf(num, sizeof(num), "%.*s", (int)(fps - field[i]), field[i]);
            put(num, main); put(" fps", info);
        } else put(field[i], info);
    }
}

// style 2: clarity -- misc.cpp draw_watermark_clarity(). verdana bold 11, bare text, 1px drop
// shadow, right/top fixed (donor hardcodes (w-6-tw, 5), ignores position knob).
static void DrawClarity(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = g_menuBold ? g_menuBold : ImGui::GetFont();
    const float fs = font->FontSize;

    char field[5][64]; char extra[320] = {};
    const int nf = BuildFields(field);
    JoinFields(extra, field, nf, " | ");

    char line[384]; snprintf(line, sizeof(line), nf > 0 ? "%s | %s" : "%s", Brand(), extra);

    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.f, line);
    const ImVec2 pos(io.DisplaySize.x - 6.f - ts.x, 5.f);
    SetBox(pos.x, pos.y, pos.x + ts.x, pos.y + ts.y);

    dl->AddText(font, fs, ImVec2(pos.x + 1.f, pos.y + 1.f), IM_COL32(0, 0, 0, 255), line);
    dl->AddText(font, fs, pos, PackColor(g_wmTextColor), line);
}

// style 4: delusional -- misc.cpp draw_watermark_delusional(). tahoma 12 mono, vertical
// accent-fade outline (full alpha top -> 0 bottom) + dark fill, shadowed text, fixed right/top.
static void DrawDelusional(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = g_fontTahoma12 ? g_fontTahoma12 : ImGui::GetFont();
    constexpr float fs = 12.f;

    char field[5][64]; char extra[320] = {};
    const int nf = BuildFields(field);
    JoinFields(extra, field, nf, " | ");

    char line[384];
    snprintf(line, sizeof(line), nf > 0 ? "%s | %s" : "%s", Brand(), extra);

    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.f, line);
    const ImVec2 pad(7.f, 7.f), margin(4.f, 3.f);
    const float w = io.DisplaySize.x;

    const ImVec2 bmin(w - ts.x - pad.x - margin.x * 2.f - 1.f, pad.y - 1.f);
    const ImVec2 bmax(w - pad.x + 1.f, ts.y + pad.y + margin.y * 2.f + 1.f);
    SetBox(bmin.x, bmin.y, bmax.x, bmax.y);

    dl->AddRectFilledMultiColor(bmin, bmax, Accent(), Accent(), AccentA(0), AccentA(0));
    dl->AddRectFilled(ImVec2(w - ts.x - pad.x - margin.x * 2.f, pad.y), ImVec2(w - pad.x, ts.y + pad.y + margin.y * 2.f), IM_COL32(20, 20, 20, 255));

    const ImVec2 tp(w - ts.x - pad.x - margin.x, pad.y + margin.y);
    dl->AddText(font, fs, ImVec2(tp.x + 1.f, tp.y + 1.f), IM_COL32(0, 0, 0, 255), line);
    dl->AddText(font, fs, tp, IM_COL32(255, 255, 255, 255), line);
}

// style 5: interwebz -- misc.cpp draw_watermark_interwebz(). calibri 16 mono, uppercase brand
// + game tag, 8-way outline, centred at a fixed point in botox's 640x480 virtual canvas; TF2
// has no such canvas (render.cpp draws straight in io.DisplaySize px) so the point is scaled
// proportionally (n_unitE::ScaleVirtual640x480). "CS:GO" literal swapped for "TF2" -- the one
// thing the game itself forces.
static void DrawInterwebz(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = g_fontInterwebzCalibri ? g_fontInterwebzCalibri : ImGui::GetFont();
    const float fs = font->FontSize;

    char brand[64];
    snprintf(brand, sizeof(brand), "%s", Brand());
    for (char* p = brand; *p; ++p) *p = (char)toupper((unsigned char)*p);

    char field[5][64]; char extra[320] = {};
    const int nf = BuildFields(field);
    JoinFields(extra, field, nf, " | ");

    char line[384];
    snprintf(line, sizeof(line), nf > 0 ? "%s TF2 | %s" : "%s TF2", brand, extra);

    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.f, line);
    float vx, vy;
    n_unitE::ScaleVirtual640x480(io.DisplaySize.x, io.DisplaySize.y, 250.f, 36.f, vx, vy);
    const ImVec2 pos(vx - floorf(ts.x * 0.5f), vy - roundf(font->Ascent));
    SetBox(pos.x, pos.y, pos.x + ts.x, pos.y + ts.y);

    constexpr ImVec2 off[8] = { {-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1} };
    for (auto& o : off) dl->AddText(font, fs, ImVec2(pos.x + o.x, pos.y + o.y), IM_COL32(0, 0, 0, 255), line);
    dl->AddText(font, fs, pos, PackColor(g_wmTextColor), line);
}

// style 7: onetap -- misc.cpp draw_watermark_onetap(). tahoma 12 mono, fixed 280x20
// translucent pink/red bar, top-right, fields joined with " | ".
static void DrawOnetap(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = g_fontTahoma12 ? g_fontTahoma12 : ImGui::GetFont();
    const float fs = font->FontSize;

    char field[5][64]; char extra[320] = {};
    const int nf = BuildFields(field);
    JoinFields(extra, field, nf, " | ");

    char line[384];
    snprintf(line, sizeof(line), nf > 0 ? "%s | %s" : "%s", Brand(), extra);

    const float w = io.DisplaySize.x;
    SetBox(w - 280.f, 10.f, w - 10.f, 30.f);
    dl->AddRectFilled(ImVec2(w - 280.f, 10.f), ImVec2(w - 10.f, 30.f), IM_COL32(185, 90, 105, 160));
    dl->AddText(font, fs, ImVec2(w - 275.f, 14.f), IM_COL32(255, 255, 255, 150), line);
}

// style 3: clarity v2 -- misc.cpp draw_watermark_clarity_v2(). inter semibold 14, dark rounded
// box with three rings, fixed top-right (donor ignores the position knob too), icomoon icon
// glyph bled into the left edge at low alpha. Segments: brand(accent) | fields | fps slot "fps",
// a 1px (50,50,50) vertical hairline in each 12px gap -- no " | " text. fps slot = digits *
// width("9") + 3, eased at 15/s so the box never jitters per digit; fps is drawn here always,
// so the shared field list is built without it. Text is drawn twice at dx 0 / 0.5 with the
// alpha pre-compensated (1 - sqrt(1 - a)) -- the donor's faux-bold.
static void DrawClarityV2(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = g_fontClarityInter ? g_fontClarityInter : ImGui::GetFont();
    ImFont* iconFont = g_fontClarityIcon ? g_fontClarityIcon : font;

    const auto textSize = [](ImFont* f, const char* text) {
        const ImVec2 sz = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.f, text);
        return std::pair{ (int)sz.x, (int)sz.y };
    };

    char field[5][64]; char extra[320] = {};
    const int nf = BuildFields(field, false);
    JoinFields(extra, field, nf, " | ");

    char fpsText[16]; snprintf(fpsText, sizeof(fpsText), "%d", (int)(g_fps.load() + 0.5f));

    static float slotWidth = 0.f;
    const float slotTarget = (float)((int)strlen(fpsText) * textSize(font, "9").first + 3);
    if (slotWidth != slotTarget) slotWidth += (slotTarget - slotWidth) * std::clamp(15.f * io.DeltaTime, 0.f, 1.f);

    const ImU32 accent = Accent();
    struct Seg { const char* text; ImU32 color; int width; };   // width -1 = measure, text "" = gap
    Seg segs[6]; int ns = 0;
    segs[ns++] = { Brand(), accent, -1 };
    if (nf > 0) { segs[ns++] = { "", 0, 12 }; segs[ns++] = { extra, IM_COL32(255, 255, 255, 150), -1 }; }
    segs[ns++] = { "", 0, 12 };
    segs[ns++] = { fpsText, IM_COL32(255, 255, 255, 255), (int)slotWidth };
    segs[ns++] = { "fps", IM_COL32(255, 255, 255, 150), -1 };
    const auto segW = [&](const Seg& s) { return s.width == -1 ? textSize(font, s.text).first : s.width; };

    int textWidth = 0;
    for (int i = 0; i < ns; ++i) textWidth += segW(segs[i]);

    constexpr int fontH = 10;
    const int w = textWidth + 10 * 2, h = fontH + 7 * 2;
    const int x = (int)io.DisplaySize.x - 7 - w, y = 7;

    const auto rect = [&](int grow) {
        return std::pair{ ImVec2((float)(x - grow), (float)(y - grow)), ImVec2((float)(x + w + grow), (float)(y + h + grow)) };
    };

    const auto [fillMin, fillMax] = rect(3);
    dl->AddRectFilled(fillMin, fillMax, IM_COL32(25, 25, 25, 255), 5.f);
    SetBox(fillMin.x, fillMin.y, fillMax.x, fillMax.y);

    {
        const auto [iconW, iconH] = textSize(iconFont, "A");
        dl->PushClipRect(fillMin, fillMax, true);
        dl->AddText(iconFont, iconFont->FontSize, ImVec2((float)(x + 15 - iconW / 2), (float)(y + h / 2 - 1 - iconH / 2)),
            (accent & ~IM_COL32_A_MASK) | (35u << IM_COL32_A_SHIFT), "A");
        dl->PopClipRect();
    }

    const ImU32 rings[3] = { IM_COL32(0, 0, 0, 255), IM_COL32(50, 50, 50, 255), IM_COL32(0, 0, 0, 255) };
    for (int grow = 0; grow < 3; ++grow) {
        const auto [rMin, rMax] = rect(grow);
        dl->AddRect(rMin, rMax, rings[grow], 5.f);
    }

    int cursor = x + 10;
    const int mid = y + h / 2;
    for (int i = 0; i < ns; ++i) {
        const Seg& s = segs[i];
        if (s.text[0]) {
            const float ty = (float)(mid - textSize(font, s.text).second / 2);
            const float a = (float)((s.color >> IM_COL32_A_SHIFT) & 0xFF) / 255.f;
            const ImU32 passA = (ImU32)((1.f - sqrtf(1.f - a)) * 255.f + 0.5f);
            const ImU32 color = (s.color & ~IM_COL32_A_MASK) | (passA << IM_COL32_A_SHIFT);
            for (const float dx : { 0.f, 0.5f })
                dl->AddText(font, font->FontSize, ImVec2((float)cursor + dx, ty), color, s.text);
        } else {
            constexpr int half = fontH / 2;
            dl->AddLine(ImVec2((float)(cursor + 6), (float)(mid - half)), ImVec2((float)(cursor + 6), (float)(mid + half)), IM_COL32(50, 50, 50, 255));
        }
        cursor += segW(s);
    }
}

// below: styles 6,9-21 -- donor bodies all live in botox's spectator_list.cpp (not misc.cpp),
// ported the same way: shared field set (BuildFields/JoinFields), each on its own botox face.

static ImU32 DimGrey(int a) { return IM_COL32(146, 144, 145, a); }

// style 6: havoc-1337 -- spectator_list.cpp draw_watermark_havoc(). tahoma 13 mono, nested
// grey box shells, crossing diagonal lines after the brand (accent + the menu accent), vgui
// shadow text. botox: accent = m_watermark_havoc_color (-> text colour), accent2 = m_accent
// (-> ImGuiCol_Accent).
static void HavocBox(ImDrawList* dl, ImVec2 pos, ImVec2 size, float alpha)
{
    auto shell = [&](float inset, int shade) {
        dl->AddRectFilled(ImVec2(pos.x + inset, pos.y + inset), ImVec2(pos.x + size.x - inset, pos.y + size.y - inset),
            IM_COL32(shade, shade, shade, (int)(255.f * alpha)), 5.f);
    };
    shell(-1.f, 10); shell(0.f, 55); shell(1.f, 10); shell(2.f, 20);
}
static void HavocLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 color)
{
    dl->AddLine(ImVec2(a.x + 0.5f, a.y + 0.5f), ImVec2(b.x + 0.5f, b.y + 0.5f), color);
}
static ImU32 HavocAccent2() { return ImGui::GetColorU32(ImGuiCol_Accent); }
static void DrawHavoc(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = g_fontHavocTahoma13 ? g_fontHavocTahoma13 : ImGui::GetFont();

    char field[5][64]; char extra[320] = {};
    const int nf = BuildFields(field);
    JoinFields(extra, field, nf, " | ");

    char name[80]; snprintf(name, sizeof(name), "%s ", Brand());
    const float nameW = VguiW(font, name);

    const ImVec2 size(nameW + VguiW(font, extra) + 37.f, 20.f);
    const ImVec2 pos(io.DisplaySize.x - size.x - 20.f, 15.f);
    SetBox(pos.x, pos.y, pos.x + size.x, pos.y + size.y);

    HavocBox(dl, pos, size, 1.f);

    const ImU32 accent = Accent(), accent2 = HavocAccent2();
    for (int i = 0; i < 3; ++i) {
        const float x = pos.x + nameW + 8.f + (float)i;
        HavocLine(dl, ImVec2(x + 19.f, pos.y + 2.f), ImVec2(x + 4.f, pos.y + 17.f), accent2);
        HavocLine(dl, ImVec2(x + 12.f, pos.y + 2.f), ImVec2(x - 3.f, pos.y + 17.f), accent);
    }

    VguiShadowText(dl, font, ImVec2(pos.x + 6.f, pos.y + 3.f), accent, name);
    VguiShadowText(dl, font, ImVec2(pos.x + nameW + 33.f, pos.y + 3.f), accent2, extra);
}

// style 9: ev0lve -- spectator_list.cpp draw_watermark_evolve(). segoe ui 16 (brand bold),
// soft drop shadow, dark header block with an accent underline, rest in (220) grey.
static void EvolveShadow(ImDrawList* dl, ImVec2 min, ImVec2 max, float radius, float alpha)
{
    const ImU32 dark = IM_COL32(0, 0, 0, (int)(255.f * alpha)), clear = IM_COL32(0, 0, 0, 0);
    dl->AddRectFilled(min, max, dark);
    dl->AddRectFilledMultiColor(ImVec2(min.x - radius, min.y), ImVec2(min.x, max.y), clear, dark, dark, clear);
    dl->AddRectFilledMultiColor(ImVec2(max.x, min.y), ImVec2(max.x + radius, max.y), dark, clear, clear, dark);
    dl->AddRectFilledMultiColor(ImVec2(min.x, min.y - radius), ImVec2(max.x, min.y), clear, clear, dark, dark);
    dl->AddRectFilledMultiColor(ImVec2(min.x, max.y), ImVec2(max.x, max.y + radius), dark, dark, clear, clear);

    // donor: rounded shadow corners, 8 fading triangles each
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;
    const auto corner = [&](ImVec2 center, float from) {
        constexpr int segments = 8;
        dl->PrimReserve(segments * 3, segments * 3);
        for (int i = 0; i < segments; ++i) {
            const float a0 = from + IM_PI * 0.5f * (float)i / segments, a1 = from + IM_PI * 0.5f * (float)(i + 1) / segments;
            dl->PrimVtx(center, uv, dark);
            dl->PrimVtx(ImVec2(center.x + cosf(a0) * radius, center.y + sinf(a0) * radius), uv, clear);
            dl->PrimVtx(ImVec2(center.x + cosf(a1) * radius, center.y + sinf(a1) * radius), uv, clear);
        }
    };
    corner(min, IM_PI); corner(ImVec2(max.x, min.y), IM_PI * 1.5f); corner(max, 0.f); corner(ImVec2(min.x, max.y), IM_PI * 0.5f);
}
static void EvolveHeader(ImDrawList* dl, ImVec2 min, float width, float alpha)
{
    dl->AddRectFilled(min, ImVec2(min.x + width, min.y + 24.f), IM_COL32(29, 29, 29, (int)(255.f * alpha)), 4.f, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilled(ImVec2(min.x, min.y + 24.f), ImVec2(min.x + width, min.y + 25.f), AccentA((int)(255.f * alpha)));
}
static void DrawEvolve(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = g_fontEvolve16 ? g_fontEvolve16 : ImGui::GetFont();
    ImFont* bold = g_fontEvolveBold16 ? g_fontEvolveBold16 : font;

    char field[5][64]; char extra[320] = {};
    const int nf = BuildFields(field);
    JoinFields(extra, field, nf, " | ");

    char rest[328] = {}; if (nf > 0) snprintf(rest, sizeof(rest), " | %s", extra);

    // botox imgui_text_width: floor(w + 0.99999)
    const float brandW = floorf(TextW(bold, bold->FontSize, Brand()) + 0.99999f);
    const float width = floorf(brandW + floorf(TextW(font, font->FontSize, rest) + 0.99999f) + 20.f);

    const ImVec2 min(io.DisplaySize.x - width - 10.f, 10.f), max(min.x + width, min.y + 25.f);
    SetBox(min.x, min.y, max.x, max.y);

    EvolveShadow(dl, min, max, 12.f, 0.25f);
    EvolveHeader(dl, min, width, 1.f);

    const float ty = floorf(min.y + (24.f - bold->FontSize) * 0.5f);
    dl->AddText(bold, bold->FontSize, ImVec2(min.x + 10.f, ty), Accent(), Brand());
    dl->AddText(font, font->FontSize, ImVec2(min.x + 10.f + brandW, ty), IM_COL32(220, 220, 220, 255), rest);
}

// style 10: legendware -- spectator_list.cpp draw_watermark_legendware(). verdana 12 mono,
// accent gradient hairlines top+bottom, dark translucent fill, vgui-shadowed text at 220 alpha.
static void LegendwareGradient(ImDrawList* dl, ImVec2 pos, ImVec2 size, const int rgb[3], int left, int right)
{
    auto col = [&](int a) { return IM_COL32(rgb[0], rgb[1], rgb[2], a); };
    const ImVec2 end(pos.x + size.x, pos.y + size.y);
    dl->AddRectFilledMultiColor(pos, end, col(left), col(0), col(0), col(left));
    dl->AddRectFilledMultiColor(pos, end, col(0), col(right), col(right), col(0));
}
static void DrawLegendware(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = g_fontLegendwareVerdana12 ? g_fontLegendwareVerdana12 : ImGui::GetFont();

    char field[5][64]; char extra[320] = {};
    const int nf = BuildFields(field);
    JoinFields(extra, field, nf, " | ");

    char text[384];
    snprintf(text, sizeof(text), nf > 0 ? "%s | %s" : "%s", Brand(), extra);

    const int boxW = (int)VguiW(font, text) + 10;
    const int half = boxW / 2;
    const float x = (float)((int)io.DisplaySize.x - 10 - boxW);
    SetBox(x, 10.f, x + (float)boxW, 30.f);

    const int rgb[3] = { (int)(g_wmTextColor[0] * 255.f), (int)(g_wmTextColor[1] * 255.f), (int)(g_wmTextColor[2] * 255.f) };

    for (float lineY : { 10.f, 29.f }) {
        LegendwareGradient(dl, ImVec2(x, lineY), ImVec2((float)half, 1.f), rgb, 170, 240);
        LegendwareGradient(dl, ImVec2(x + (float)half, lineY), ImVec2((float)half, 1.f), rgb, 240, 170);
    }

    dl->AddRectFilled(ImVec2(x, 11.f), ImVec2(x + (float)boxW, 29.f), IM_COL32(10, 10, 10, 150));
    VguiShadowText(dl, font, ImVec2(x + 5.f, 13.f), IM_COL32(255, 255, 255, 220), text);
}

// style 11: interium -- spectator_list.cpp draw_watermark_interium(). droid sans bold 24 drawn
// at 16, slow 4-corner hue cycle border, black fill (donor's interium bg default), shadowed text.
static float s_interiumHues[4] = { 0.f, 0.25f, 0.5f, 0.75f };
static ImU32 InteriumHsv(float hue, int alpha)
{
    float r = 0.f, g = 0.f, b = 0.f;
    ImGui::ColorConvertHSVtoRGB(hue, 1.f, 0.7f, r, g, b);
    return IM_COL32((int)(r * 255.f), (int)(g * 255.f), (int)(b * 255.f), alpha);
}
static void InteriumStep(float dt)
{
    const float step = dt * 0.25f;
    for (float& hue : s_interiumHues) { hue += step; if (hue > 1.f) hue -= 1.f; }
}
static void DrawInterium(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = g_fontInteriumDroid24 ? g_fontInteriumDroid24 : ImGui::GetFont();
    InteriumStep(io.DeltaTime);
    const float* h = s_interiumHues;

    char field[5][64]; char extra[320] = {};
    const int nf = BuildFields(field);
    JoinFields(extra, field, nf, " | ");

    char text[384];
    snprintf(text, sizeof(text), nf > 0 ? "%s | %s" : "%s", Brand(), extra);

    const ImVec2 ts = font->CalcTextSizeA(16.f, FLT_MAX, 0.f, text);
    constexpr float x = 5.f, y = 5.f;
    const float right = x + ts.x + 10.f, bottom = y + ts.y + 8.f;
    SetBox(x - 1.f, y - 1.f, right + 1.f, bottom + 1.f);

    const auto trunc = [](float v) { return (float)(int)v; };

    dl->AddRectFilledMultiColor(ImVec2(x - 1.f, y - 1.f), ImVec2(trunc(right + 1.f), trunc(bottom + 1.f)),
        InteriumHsv(h[0], 100), InteriumHsv(h[1], 100), InteriumHsv(h[2], 100), InteriumHsv(h[3], 100));
    dl->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(trunc(right), trunc(bottom)),
        InteriumHsv(h[0], 255), InteriumHsv(h[1], 255), InteriumHsv(h[2], 255), InteriumHsv(h[3], 255));
    dl->AddRectFilled(ImVec2(x + 1.f, y + 1.f), ImVec2(trunc(right - 1.f), trunc(bottom - 1.f)), IM_COL32(0, 0, 0, 255));

    dl->AddText(font, 16.f, ImVec2(11.f, 10.f), IM_COL32(0, 0, 0, 255), text);
    dl->AddText(font, 16.f, ImVec2(10.f, 9.f), IM_COL32(255, 255, 255, 255), text);
}

// style 12: skebob -- spectator_list.cpp draw_watermark_skebob(). nunito 14, double dark
// frame box, accent brand, fields in white after a "|". Donor's typing animation skipped.
static void DrawSkebob(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = g_fontSkebobNunito14 ? g_fontSkebobNunito14 : ImGui::GetFont();
    const float size = font->FontSize;

    char field[5][64]; char extra[320] = {};
    const int nf = BuildFields(field);
    JoinFields(extra, field, nf, " | ");

    const float wBrand = TextW(font, size, Brand());
    const float wExtra = TextW(font, size, extra);

    const float width = wBrand + wExtra + 55.f;
    const float wx = io.DisplaySize.x - width;

    const ImVec2 min(wx + 10.f, 10.f), max(wx + width - 5.f, 40.f);
    SetBox(min.x, min.y, max.x, max.y);

    dl->AddRectFilled(min, max, IM_COL32(40, 40, 40, 150), 5.f);
    dl->AddRect(min, max, IM_COL32(0, 0, 0, 200), 5.f, 0, 1.f);
    dl->AddRectFilled(ImVec2(min.x + 3.f, min.y + 3.f), ImVec2(max.x - 3.f, max.y - 3.f), IM_COL32(20, 20, 20, 255), 5.f);
    dl->AddRect(ImVec2(min.x + 3.f, min.y + 3.f), ImVec2(max.x - 3.f, max.y - 3.f), IM_COL32(0, 0, 0, 255), 5.f, 0, 1.f);

    dl->AddText(font, size, ImVec2(wx + 20.f, 20.f), Accent(), Brand());
    if (nf > 0) {
        dl->AddText(font, size, ImVec2(wx + wBrand + 25.f, 20.f), IM_COL32(255, 255, 255, 255), "|");
        dl->AddText(font, size, ImVec2(wx + wBrand + 35.f, 20.f), IM_COL32(255, 255, 255, 255), extra);
    }
}

// style 13: cumidere -- spectator_list.cpp draw_watermark_cumidere(). tahoma 16 light, dark
// box, open bottom (top+left+right accent lines only), brand accent, number (208) grey, unit
// (136) grey.
static void DrawCumidere(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = g_fontCumidereTahoma16 ? g_fontCumidereTahoma16 : ImGui::GetFont();
    const float size = font->FontSize;

    char field[5][64]; char extra[320] = {};
    const int nf = BuildFields(field);
    JoinFields(extra, field, nf, " | ");

    const char* sep = nf > 0 ? " | " : "";
    const float wB = TextW(font, size, Brand());
    const float wSep = TextW(font, size, sep);
    const float wN = TextW(font, size, extra);

    const float right = io.DisplaySize.x - 10.f;
    const float left = right - (wB + wSep + wN + 16.f);
    const float bottom = size + 20.f;
    SetBox(left, 10.f, right, bottom);

    const ImU32 accent = Accent();

    dl->AddRectFilled(ImVec2(left, 10.f), ImVec2(right, bottom), IM_COL32(23, 23, 23, 255));
    dl->AddLine(ImVec2(left, 10.f), ImVec2(right, 10.f), accent, 1.f);
    dl->AddLine(ImVec2(left, 10.f), ImVec2(left, bottom), accent, 1.f);
    dl->AddLine(ImVec2(right, 10.f), ImVec2(right, bottom), accent, 1.f);

    float x = left + 8.f;
    dl->AddText(font, size, ImVec2(x, 15.f), accent, Brand()); x += wB;
    dl->AddText(font, size, ImVec2(x, 15.f), IM_COL32(27, 27, 27, 255), sep); x += wSep;
    dl->AddText(font, size, ImVec2(x, 15.f), IM_COL32(208, 208, 208, 255), extra);
}

// style 14: illusory -- spectator_list.cpp draw_watermark_illusory(). tahoma 13 mono+bold,
// rounded dark box, thin grey border, accent(230) under white double-drawn text.
static void DrawIllusory(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = g_fontIllusoryTahoma13 ? g_fontIllusoryTahoma13 : ImGui::GetFont();
    const float fs = 13.f;

    char field[5][64]; char extra[320] = {};
    const int nf = BuildFields(field);
    JoinFields(extra, field, nf, " | ");

    char text[384];
    snprintf(text, sizeof(text), nf > 0 ? "%s | %s" : "%s", Brand(), extra);

    const ImVec2 size = font->CalcTextSizeA(fs, FLT_MAX, 0.f, text);
    const float w = (float)(int)io.DisplaySize.x;
    SetBox(w - size.x - 18.f, 4.f, w - 4.f, size.y + 16.f);

    dl->AddRectFilled(ImVec2(w - size.x - 17.f, 5.f), ImVec2(w - 5.f, size.y + 15.f), IM_COL32(28, 28, 28, 255), 5.5f);
    dl->AddRect(ImVec2(w - size.x - 18.f, 4.f), ImVec2(w - 4.f, size.y + 16.f), IM_COL32(56, 56, 56, 128), 5.5f, 0, 1.f);

    dl->AddText(font, fs, ImVec2(w - size.x - 10.f, 10.f), AccentA(230), text);
    dl->AddText(font, fs, ImVec2(w - size.x - 11.f, 10.f), IM_COL32(255, 255, 255, 255), text);
}

// style 15: lumi -- spectator_list.cpp draw_watermark_lumi(). rubik 16 mono, dark box with a
// black border, accent diagonal-stripe divider after the white title, " / "-joined accent
// elements, width eases to its target at 8/s. Donor's rain design, animated title and
// glow-on-trick are menu-only extras -- skipped.
static void DrawLumi(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = g_fontLumiRubik16 ? g_fontLumiRubik16 : ImGui::GetFont();
    const float size = font->FontSize;

    char field[5][64]; char extra[320] = {};
    const int nf = BuildFields(field);
    JoinFields(extra, field, nf, " / ");

    const char* title = Brand();
    const float titleW = TextW(font, size, title);
    const float gap = 27.f;

    float content = titleW;
    if (nf > 0) content += gap + TextW(font, size, extra);

    static float width = 0.f, lastEase = -10.f;
    const float now = (float)ImGui::GetTime();
    const float target = content + 12.f;
    width = now - lastEase > 1.f || fabsf(width - target) > 200.f ? target : (target - width) * io.DeltaTime * 8.f + width;
    lastEase = now;

    const float height = size + 12.f;
    const float x = io.DisplaySize.x - width - 10.f, y = 10.f;
    SetBox(x, y, x + width, y + height);

    const ImU32 accent = Accent();

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height), IM_COL32(20, 20, 20, 255), 5.f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), IM_COL32(0, 0, 0, 255), 0.f, 0, 1.f);

    const float stripeX = x + titleW + 11.f;
    for (int i = 15; i < 18; ++i) {
        const float sx = stripeX + (float)i;
        dl->AddLine(ImVec2(sx, y + 4.f), ImVec2(sx - 12.f, y + height - 5.f), accent, 1.f);
        dl->AddLine(ImVec2(sx - 5.f, y + 4.f), ImVec2(sx - 17.f, y + height - 5.f), accent, 1.f);
    }

    float tx = (width - content) * 0.5f + x;
    const float ty = floorf((height - size) * 0.5f + y);
    const auto text = [&](float at, ImU32 color, const char* s) {
        dl->AddText(font, size, ImVec2(at + 1.f, ty + 1.f), IM_COL32(0, 0, 0, 255), s);
        dl->AddText(font, size, ImVec2(at, ty), color, s);
    };
    text(tx, IM_COL32(255, 255, 255, 255), title); tx += titleW + gap;
    if (nf > 0) text(tx, accent, extra);
}

// style 16: billware -- spectator_list.cpp draw_watermark_billware(). tahoma bold 12 mono,
// nested rounded-corner gradient rings, 10 slow-drifting accent dots inside, accent under
// white double-drawn text.
static void BillwareMulticolorRounded(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 bg, ImU32 ul, ImU32 ur, ImU32 br, ImU32 bl, float rounding)
{
    const float r = std::min(rounding, std::min(fabsf(max.x - min.x) * 0.5f - 1.f, fabsf(max.y - min.y) * 0.5f - 1.f));
    if (r <= 0.f) return;
    dl->AddRectFilledMultiColor(min, max, ul, ur, br, bl);
    struct { ImVec2 corner, center; float aMin, aMax; } corners[] = {
        { min, ImVec2(min.x + r, min.y + r), 4.82f, 3.1f },
        { ImVec2(max.x, min.y), ImVec2(max.x - r, min.y + r), 6.34f, 4.62f },
        { max, ImVec2(max.x - r, max.y - r), 7.96f, 6.24f },
        { ImVec2(min.x, max.y), ImVec2(min.x + r, max.y - r), 9.5f, 7.77f },
    };
    for (auto& c : corners) { dl->PathLineTo(c.corner); dl->PathArcTo(c.center, r, c.aMin, c.aMax, 0); dl->PathFillConvex(bg); }
}
static void BillwareBox(ImDrawList* dl, ImVec2 min, ImVec2 max, float alpha)
{
    auto grey = [](float v, float a) { return IM_COL32((int)(v * 255.f), (int)(v * 255.f), (int)(v * 255.f), (int)(a * 255.f)); };
    auto grown = [&](float g) { return std::pair{ ImVec2(min.x - g, min.y - g), ImVec2(max.x + g, max.y + g) }; };

    const ImU32 dark = grey(0.07f, alpha), light = grey(0.14f, alpha);
    const auto [fillMin, fillMax] = grown(2.f);
    BillwareMulticolorRounded(dl, fillMin, fillMax, dark, dark, dark, light, light, 5.5f);
    BillwareMulticolorRounded(dl, fillMin, fillMax, dark, light, light, dark, dark, 5.5f);

    const ImU32 rings[3] = { grey(0.15f, alpha), grey(0.3f, alpha * 0.7f), grey(0.15f, alpha) };
    for (int i = 0; i < 3; ++i) { const auto [a, b] = grown(2.f + (float)i); dl->AddRect(a, b, rings[i], 5.5f, 0, 1.f); }
}
static void DrawBillware(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = g_fontTahomaBd12 ? g_fontTahomaBd12 : ImGui::GetFont();
    const float fs = 12.f;

    char field[5][64]; char extra[320] = {};
    const int nf = BuildFields(field);
    JoinFields(extra, field, nf, " | ");

    char text[384];
    snprintf(text, sizeof(text), nf > 0 ? " %s | %s" : " %s", Brand(), extra);

    const ImVec2 size = font->CalcTextSizeA(fs, FLT_MAX, 0.f, text);
    const float w = (float)(int)io.DisplaySize.x;
    const ImVec2 min(w - size.x - 13.f, 7.f), max(w - 7.f, size.y + 13.f);
    SetBox(min.x - 4.f, min.y - 4.f, max.x + 4.f, max.y + 4.f);

    BillwareBox(dl, min, max, 1.f);

    const float x0 = min.x - 2.f, x1 = max.x + 2.f, y0 = min.y - 2.f, y1 = max.y + 2.f;
    auto randomIn = [](float lo, float hi) { return (float)(rand() % (std::max)((int)(hi - lo), 1)) + lo; };

    static std::vector<ImVec2> dots;
    if (dots.empty()) for (int i = 0; i < 10; ++i) dots.emplace_back(randomIn(x0, x1), randomIn(y0, y1));

    const ImU32 dotColor = AccentA(102);
    for (auto& dot : dots) {
        dl->AddCircle(dot, 2.5f, dotColor, 0, 1.f);
        dl->AddCircleFilled(dot, 0.5f, dotColor, 0);
        dot.y += 0.07f;
        if (dot.y > y1 - 1.f) { dot.y = randomIn(y0, y1); dot.x = randomIn(x0, x1); }
    }

    dl->AddText(font, fs, ImVec2(w - size.x - 12.f, 9.f), Accent(), text);
    dl->AddText(font, fs, ImVec2(w - size.x - 11.f, 10.f), IM_COL32(255, 255, 255, 255), text);
}

// styles 17/18: dna / dna clarity -- spectator_list.cpp draw_watermark_dna[_clarity]().
// montserrat 15 / pt root bold 15: nested corner-shaded ring box, brand accent (white-shadowed
// in dna) + dim fields. dna draws a 60-step accent gradient strip over the text, dna clarity
// bleeds a huge faint icon glyph into the left edge instead. Donor's glow-on-spot is dropped,
// no TF2 "enemy spotted" concept to drive it.
static ImU32 DnaGrey(float v, float a = 1.f) { return ImGui::ColorConvertFloat4ToU32(ImVec4(v, v, v, a)); }
static void DnaBox(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 ring, ImU32 midRing)
{
    const ImU32 corner = IM_COL32(10, 10, 12, 50), dark = IM_COL32(10, 10, 12, 255), light = DnaGrey(0.14f);
    BillwareMulticolorRounded(dl, min, max, corner, dark, dark, light, light, 5.5f);
    BillwareMulticolorRounded(dl, min, max, corner, light, light, dark, dark, 5.5f);
    dl->AddRect(min, max, ring, 5.5f);
    dl->AddRect(ImVec2(min.x - 1.f, min.y - 1.f), ImVec2(max.x + 1.f, max.y + 1.f), midRing, 5.5f);
    dl->AddRect(ImVec2(min.x - 2.5f, min.y - 2.5f), ImVec2(max.x + 2.5f, max.y + 2.5f), ring, 5.5f);
}
static void DrawDnaCommon(ImDrawList* dl, ImGuiIO& io, bool clarityIcon)
{
    ImFont* font = clarityIcon ? g_fontDnaPtRootBold15 : g_fontDnaMontserrat15;
    if (!font) font = ImGui::GetFont();
    const float fs = 15.f;

    char field[5][64]; char extra[320] = {};
    const int nf = BuildFields(field);
    JoinFields(extra, field, nf, " | ");

    const char* sep = nf > 0 ? " | " : "";
    const float wB = TextW(font, fs, Brand());
    const float wSep = TextW(font, fs, sep);
    const float wE = TextW(font, fs, extra);
    const float textW = wB + wSep + wE;

    const float w = (float)(int)io.DisplaySize.x;
    const ImVec2 textPos(w - textW - 10.f, 10.f);
    const ImVec2 min(w - textW - 15.f, 5.f), max(w - 5.f, 30.f);
    SetBox(min.x - 3.f, min.y - 3.f, max.x + 3.f, max.y + 3.f);

    const ImU32 accent = Accent(), white = DnaGrey(0.8f);

    if (clarityIcon) {
        DnaBox(dl, min, max, IM_COL32(20, 20, 22, 250), IM_COL32(4, 4, 5, 100));
        ImFont* icon = g_fontDnaClarityIcon50 ? g_fontDnaClarityIcon50 : g_fontClarityIcon;
        if (icon) {
            const float iconW = icon->CalcTextSizeA(50.f, FLT_MAX, 0.f, "A").x;
            dl->PushClipRect(min, max, true);
            dl->AddText(icon, 50.f, ImVec2(min.x + 6.f - iconW * 0.3f, textPos.y - 17.f), AccentA((int)(0.15f * 255.f)), "A");
            dl->PopClipRect();
        }
    } else {
        DnaBox(dl, min, max, IM_COL32(10, 10, 12, 200), IM_COL32(4, 4, 5, 50));
        const float gradW = textW * 1.1f, center = textPos.x + textW * 0.5f, top = textPos.y - 6.f;
        for (int i = 0; i < 60; ++i) {
            const float x0 = center - gradW * 0.5f + gradW / 60.f * (float)i, x1 = x0 + gradW / 60.f;
            const float a = (std::max)(0.f, 1.f - fabsf((x0 + x1) * 0.5f - center) / (gradW * 0.5f));
            dl->AddRectFilled(ImVec2(x0, top), ImVec2(x1, top + 3.f), AccentA((int)(0.75f * a * 255.f)), 2.f, 0);
        }
    }

    float x = textPos.x;
    if (!clarityIcon) dl->AddText(font, fs, ImVec2(x + 1.f, textPos.y + 1.f), white, Brand());   // donor: brand shadow, dna only
    dl->AddText(font, fs, ImVec2(x, textPos.y), accent, Brand()); x += wB;
    dl->AddText(font, fs, ImVec2(x, textPos.y), DnaGrey(clarityIcon ? 0.2f : 0.4f), sep); x += wSep;
    dl->AddText(font, fs, ImVec2(x, textPos.y), white, extra);
}
static void DrawDna(ImDrawList* dl, ImGuiIO& io) { DrawDnaCommon(dl, io, false); }
static void DrawDnaClarity(ImDrawList* dl, ImGuiIO& io) { DrawDnaCommon(dl, io, true); }

// style 19: cucumber -- spectator_list.cpp draw_watermark_cucumber(). tahoma 14 draws, verdana 14
// measures the fields (donor quirk kept), pill bg, brand accent + "|" + fields white, accent
// underline bar, 5/s fade in/out on the enable toggle.
static constexpr ImU32 kCucumberBg = IM_COL32(18, 15, 18, 255);
static float CucumberW(ImFont* f, const char* s) { return (float)(int)(TextW(f, 14.f, s) + 0.95f); }
static ImU32 CucumberFade(ImU32 color, float alpha)
{
    return (color & ~IM_COL32_A_MASK) | ((ImU32)((float)(color >> IM_COL32_A_SHIFT) * alpha + 0.5f) << IM_COL32_A_SHIFT);
}
static void DrawCucumber(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = g_fontCucumberTahoma14 ? g_fontCucumberTahoma14 : ImGui::GetFont();
    ImFont* measure = g_fontCucumberVerdana14 ? g_fontCucumberVerdana14 : font;

    static float alpha = 0.f;
    alpha = std::clamp(alpha + io.DeltaTime * 5.f, 0.f, 1.f);

    char field[5][64]; char extra[320] = {};
    const int nf = BuildFields(field);
    JoinFields(extra, field, nf, " ");

    const float userW = CucumberW(measure, extra);

    constexpr float x = 10.f, y = 10.f;
    const ImVec2 max(x + 100.f + userW, y + 20.f);
    SetBox(x, y, max.x, max.y);

    dl->AddRectFilled(ImVec2(x, y), max, kCucumberBg, 15.f);

    const ImU32 white = CucumberFade(IM_COL32(255, 255, 255, 255), alpha);
    float tx = x + 16.f;
    dl->AddText(font, 14.f, ImVec2(tx, y + 2.f), CucumberFade(Accent(), alpha), Brand());
    tx += CucumberW(font, Brand()) + 8.f;
    if (nf > 0) {
        dl->AddText(font, 14.f, ImVec2(tx, y + 2.f), white, "|");
        tx += CucumberW(font, "|") + 8.f;
        dl->AddText(font, 14.f, ImVec2(tx, y + 2.f), white, extra);
    }

    dl->AddRectFilled(ImVec2(x + 15.f, y + 18.f), ImVec2(x + 85.f + userW, y + 20.f), Accent());
}

// style 20: howeweware -- spectator_list.cpp draw_watermark_howeweware(). minecraft 14, no
// background, stacked value(grey)+unit(accent) rows, top-left. Donor = imgui 1.70 window: text
// item width (int)(w + 0.95), padding 8, item spacing 8 x 4, min size 32.
static constexpr ImU32 kHowewewareGrey = IM_COL32(225, 225, 225, 255);
static float HowewewareW(ImFont* f, float size, const char* s) { return (float)(int)(TextW(f, size, s) + 0.95f); }
static void DrawHoweweware(ImDrawList* dl, ImGuiIO&)
{
    ImFont* font = g_fontHowewewareMinecraft14 ? g_fontHowewewareMinecraft14 : ImGui::GetFont();
    constexpr float size = 14.f, line = size + 4.f, pad = 8.f, pos = 5.f, spacing = 8.f;

    struct Row { char value[64]; char unit[16]; };
    Row rows[5]; int nr = 0;

    auto add = [&](const char* value, const char* unit) {
        if (nr >= 5) return;
        snprintf(rows[nr].value, sizeof(rows[nr].value), "%s", value);
        snprintf(rows[nr].unit, sizeof(rows[nr].unit), "%s", unit);
        ++nr;
    };

    add(Brand(), "");
    if (g_wmShowTime) {
        time_t t = time(nullptr); struct tm lt; localtime_s(&lt, &t);
        char v[32]; strftime(v, sizeof(v), "%X", &lt);
        add(v, "pm");
    }
    if (g_wmShowPing && g_conn.load()) { char v[16]; snprintf(v, sizeof(v), "%d", g_ping.load()); add(v, "ms"); }
    if (g_wmShowLoss && g_conn.load()) { char v[16]; snprintf(v, sizeof(v), "%d", g_loss.load()); add(v, "% loss"); }
    if (g_wmShowFPS) { char v[16]; snprintf(v, sizeof(v), "%d", (int)(g_fps.load() + 0.5f)); add(v, "fps"); }

    const ImU32 accent = Accent();
    float right = pos + pad;
    for (int i = 0; i < nr; ++i) {
        const float y = pos + pad + line * (float)i;
        float x = pos + pad;
        dl->AddText(font, size, ImVec2(x, y), kHowewewareGrey, rows[i].value);
        x += HowewewareW(font, size, rows[i].value) + spacing;
        dl->AddText(font, size, ImVec2(x, y), accent, rows[i].unit);
        right = (std::max)(right, x + HowewewareW(font, size, rows[i].unit));
    }
    const float bottom = pos + pad + line * (float)(nr - 1) + size;
    SetBox(pos, pos, pos + (std::max)(32.f, right - pos + pad), pos + (std::max)(32.f, bottom - pos + pad));
}

// style 21: gamesense -- spectator_list.cpp draw_watermark_gamesense() (chifirium.dll
// FUN_1008e770). sf pro display bold 15 px atlas, every measure + draw at 13. 4-layer frame,
// skeet blue->pink->yellow bar under the inner line, brand split "game"|"sense" two tone
// (white | sense colour -> text colour), " | " separators in (210) grey, parts (255,230).
static constexpr float kGamesenseSize = 13.f;
static float GamesenseW(ImFont* f, const char* s) { return TextW(f, kGamesenseSize, s); }
static void GamesenseFrame(ImDrawList* dl, ImVec2 min, ImVec2 max)
{
    dl->AddRectFilled(ImVec2(min.x - 3.f, min.y - 3.f), ImVec2(max.x + 3.f, max.y + 3.f), IM_COL32(36, 36, 36, 255));
    dl->AddRect(ImVec2(min.x - 3.f, min.y - 3.f), ImVec2(max.x + 3.f, max.y + 3.f), IM_COL32(0, 0, 0, 255), 0.f, 0, 1.f);
    dl->AddRectFilled(ImVec2(min.x - 1.f, min.y - 1.f), ImVec2(max.x + 1.f, max.y + 1.f), IM_COL32(18, 18, 18, 255));
    dl->AddRectFilled(min, max, IM_COL32(12, 12, 12, 245));
    dl->AddRect(ImVec2(min.x + 1.f, min.y + 1.f), ImVec2(max.x - 1.f, max.y - 1.f), IM_COL32(62, 62, 62, 255), 0.f, 0, 1.f);
}
static void GamesenseBar(ImDrawList* dl, ImVec2 min, ImVec2 max)
{
    constexpr ImU32 blue = IM_COL32(59, 175, 222, 255), pink = IM_COL32(202, 70, 205, 255), yellow = IM_COL32(201, 227, 58, 255);
    const float mid = min.x + (max.x - min.x) * 0.5f;
    dl->AddRectFilledMultiColor(ImVec2(min.x + 2.f, min.y + 2.f), ImVec2(mid, min.y + 4.f), blue, pink, pink, blue);
    dl->AddRectFilledMultiColor(ImVec2(mid, min.y + 2.f), ImVec2(max.x - 2.f, min.y + 4.f), pink, yellow, yellow, pink);
}
void Watermark_GamesenseFrame(ImDrawList* dl, ImVec2 min, ImVec2 max, bool bar)
{
    GamesenseFrame(dl, min, max);
    if (bar) GamesenseBar(dl, min, max);
}
// split at half the codepoints ("gamesense" -> game | sense)
static size_t GamesenseSplit(const char* text)
{
    std::vector<size_t> starts;
    const size_t n = strlen(text);
    for (size_t i = 0; i < n; ++i) if (((unsigned char)text[i] & 0xc0) != 0x80) starts.push_back(i);
    return starts.size() < 2 ? n : starts[starts.size() / 2];
}
static void DrawGamesense(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = g_fontGamesenseSfBold15 ? g_fontGamesenseSfBold15 : ImGui::GetFont();

    // donor parts: user / fps / "{}ms" / "{}tick" / vel / HH:MM:SS -- shared knobs mapped
    char parts[5][64]; int np = 0;
    if (g_wmShowFPS) snprintf(parts[np++], 64, "%d fps", (int)(g_fps.load() + 0.5f));
    if (g_wmShowPing && g_conn.load()) snprintf(parts[np++], 64, "%dms", g_ping.load());
    if (g_wmShowLoss && g_conn.load()) snprintf(parts[np++], 64, "%d%% loss", g_loss.load());
    if (g_wmShowTime) {
        time_t t = time(nullptr); struct tm lt; localtime_s(&lt, &t);
        snprintf(parts[np++], 64, "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
    }

    char brand[64]; snprintf(brand, sizeof(brand), "%s", Brand());
    const size_t cut = GamesenseSplit(brand);
    char game[64], sense[64];
    snprintf(game, sizeof(game), "%.*s", (int)cut, brand);
    snprintf(sense, sizeof(sense), "%s", brand + cut);

    constexpr const char* separator = " | ";
    float width = GamesenseW(font, game) + GamesenseW(font, sense);
    for (int i = 0; i < np; ++i) width += GamesenseW(font, separator) + GamesenseW(font, parts[i]);
    width += 14.f;

    constexpr bool bar = true;   // m_watermark_gamesense_bar default
    const float textH = font->CalcTextSizeA(kGamesenseSize, FLT_MAX, 0.f, "A").y;
    const float height = textH + 10.f + (bar ? 3.f : 0.f);
    const float x = io.DisplaySize.x - width - 15.f, y = 15.f;
    const ImVec2 min(x, y), max(x + width, y + height);
    SetBox(x - 3.f, y - 3.f, max.x + 3.f, max.y + 3.f);

    GamesenseFrame(dl, min, max);
    if (bar) GamesenseBar(dl, min, max);

    const float textY = y + 3.f + (bar ? 2.f : 0.f);
    float textX = x + 7.f;
    const auto put = [&](const char* s, ImU32 c) { dl->AddText(font, kGamesenseSize, ImVec2(textX, textY), c, s); textX += GamesenseW(font, s); };

    put(game, IM_COL32(255, 255, 255, 255));
    put(sense, Accent());
    for (int i = 0; i < np; ++i) { put(separator, IM_COL32(210, 210, 210, 255)); put(parts[i], IM_COL32(255, 255, 255, 230)); }
}

// style 8: airflow -- spectator_list.cpp draw_watermark_airflow(). sf ui display semibold 14.
// Two-segment rounded bar: left pill (airflow_logo icon tinted accent + brand), right pill
// (local avatar + the shared field set). Donor's live backbuffer blur under both -> BlurRect
// (bloom's shader, straight replace instead of an additive combine). Donor box: right edge at
// w-16, y 11, 29 tall.
struct WmPlayerInfo_t
{
    char     name[32];
    int      userID;
    char     guid[33];
    uint32_t friendsID;
    char     friendsName[32];
    bool     fakeplayer;
    bool     ishltv;
    uint32_t customFiles[4];
    uint8_t  filesDownloaded;
};
static ImTextureID LocalAvatarTex()
{
    if (!g_engine) return 0;
    const int idx = GetLocalPlayerIndex();
    if (idx < 1) return 0;
    WmPlayerInfo_t pi = {};
    if (!VCall<bool>(g_engine, 8, idx, &pi) || pi.fakeplayer || !pi.friendsID) return 0;
    return Avatar_Get(n_avatar::FriendIdToSteam64(pi.friendsID), false);
}
static void DrawAirflow(ImDrawList* dl, ImGuiIO& io)
{
    ImFont* font = g_fontAirflow14 ? g_fontAirflow14 : ImGui::GetFont();

    char field[5][64]; char extra[320] = {};
    const int nf = BuildFields(field);
    JoinFields(extra, field, nf, " | ");

    const auto imguiW = [&](const char* s) { return floorf(TextW(font, 14.f, s) + 0.99999f); };
    const float prefixSize = imguiW(Brand()) + 16.f;
    const float leftW = prefixSize + 30.f;
    const float rightW = imguiW(extra) + 35.f;
    const float width = leftW + rightW;

    const float x = io.DisplaySize.x - width - 16.f, y = 11.f;
    SetBox(x, y, x + width, y + 29.f);

    const ImDrawListFlags oldFlags = dl->Flags;
    dl->Flags |= ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines;

    BlurRect(dl, ImVec2(x, y), ImVec2(x + width, y + 29.f), 1.f, 3.f);
    constexpr ImU32 white = IM_COL32(255, 255, 255, 255);

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + leftW, y + 29.f), IM_COL32(255, 255, 255, 60), 3.f, ImDrawFlags_RoundCornersLeft);
    GifTex* gif = GifTex_Get("airflow_logo", airflow_logo, sizeof(airflow_logo));
    const ImTextureID logo = GifTex_Frame(gif, 0.0);
    if (logo) dl->AddImage(logo, ImVec2(x + 10.f, y + 7.f), ImVec2(x + 26.f, y + 23.f), ImVec2(0, 0), ImVec2(1, 1), Accent());
    dl->AddText(font, 14.f, ImVec2(x + 32.f, y + 6.5f), white, Brand());

    const float rightX = x + leftW;
    dl->AddRectFilled(ImVec2(rightX, y), ImVec2(rightX + rightW, y + 29.f), IM_COL32(100, 100, 100, 180), 3.f, ImDrawFlags_RoundCornersRight);
    const ImTextureID avatar = LocalAvatarTex();
    const ImVec2 avMin(rightX + 7.f, y + 6.f), avMax(rightX + 23.f, y + 22.f);
    if (avatar) dl->AddImageRounded(avatar, avMin, avMax, ImVec2(0, 0), ImVec2(1, 1), white, 20.f);
    else dl->AddRectFilled(avMin, avMax, white, 20.f);
    dl->AddText(font, 14.f, ImVec2(rightX + 29.f, y + 6.5f), white, nf > 0 ? extra : Brand());

    dl->AddRect(ImVec2(x, y), ImVec2(x + width, y + 29.f), IM_COL32(120, 120, 120, 100), 3.f);
    dl->Flags = oldFlags;
}

bool RenderWatermarkStyled()
{
    if (!g_wmEnable || g_wmStyle <= 0) return false;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    switch (g_wmStyle) {
    case 1:  DrawKamibebra(dl, io);   return true;
    case 2:  DrawClarity(dl, io);     return true;
    case 3:  DrawClarityV2(dl, io);   return true;
    case 4:  DrawDelusional(dl, io);  return true;
    case 5:  DrawInterwebz(dl, io);   return true;
    case 6:  DrawHavoc(dl, io);       return true;
    case 7:  DrawOnetap(dl, io);      return true;
    case 8:  DrawAirflow(dl, io);     return true;
    case 9:  DrawEvolve(dl, io);      return true;
    case 10: DrawLegendware(dl, io);  return true;
    case 11: DrawInterium(dl, io);    return true;
    case 12: DrawSkebob(dl, io);      return true;
    case 13: DrawCumidere(dl, io);    return true;
    case 14: DrawIllusory(dl, io);    return true;
    case 15: DrawLumi(dl, io);        return true;
    case 16: DrawBillware(dl, io);    return true;
    case 17: DrawDna(dl, io);         return true;
    case 18: DrawDnaClarity(dl, io);  return true;
    case 19: DrawCucumber(dl, io);    return true;
    case 20: DrawHoweweware(dl, io);  return true;
    case 21: DrawGamesense(dl, io);   return true;
    default: return false;
    }
}

// ==== media player: botox media_player_look / media_player_frame + the donor halves ===========
MediaLook Watermark_MediaLook(int style)
{
    constexpr ImU32 white = IM_COL32(255, 255, 255, 255);
    const ImU32 accent = Accent();
    switch (style) {
    case 1:  return { true, white, IM_COL32(146, 144, 145, 255), 0 };
    case 2:  return { false, white, white, 1 };
    case 3:  return { true, accent, IM_COL32(255, 255, 255, 150), 0 };
    case 4:  return { true, white, white, 1 };
    case 5:  return { false, white, white, 2 };
    case 7:  return { true, IM_COL32(255, 255, 255, 150), IM_COL32(255, 255, 255, 150), 0 };
    // spectator_list.cpp media_player_donor_look
    case 6:  return { true, accent, HavocAccent2(), 1 };
    case 8:  return { true, white, IM_COL32(255, 255, 255, 102), 0 };
    case 9:  return { true, accent, IM_COL32(220, 220, 220, 255), 0 };
    case 10: return { true, IM_COL32(255, 255, 255, 220), IM_COL32(255, 255, 255, 220), 1 };
    case 11: return { true, white, white, 1 };
    case 12: return { false, white, IM_COL32(153, 153, 153, 255), 3 };
    case 13: return { false, white, IM_COL32(153, 153, 153, 255), 1 };
    case 14: return { false, white, white, 1 };
    case 15: return { true, white, accent, 0 };
    case 16: return { true, white, accent, 0 };
    case 17: return { true, accent, DnaGrey(0.8f), 0 };
    case 18: return { true, accent, DnaGrey(0.5f), 0 };
    case 19: return { true, accent, white, 0 };
    case 20: return { false, accent, kHowewewareGrey, 0 };
    case 21: return { true, accent, IM_COL32(255, 255, 255, 230), 0 };
    default: return { true, white, ImGui::GetColorU32(ImGuiCol_Accent), 0 };   // style 0: menu accent
    }
}

// the style's watermark box grown to [min, max]. bare styles (look.box false) pass 0
void Watermark_MediaFrame(ImDrawList* dl, ImVec2 min, ImVec2 max, int style)
{
    switch (style) {
    case 0:
        dl->AddRectFilled(min, max, IM_COL32(25, 25, 25, 255), ImGui::GetStyle().WindowRounding);
        dl->AddRect(ImVec2(min.x + 1.f, min.y + 1.f), ImVec2(max.x - 1.f, max.y - 1.f), IM_COL32(50, 50, 50, 255), ImGui::GetStyle().WindowRounding);
        break;
    case 1: dl->AddRectFilled(min, max, IM_COL32(16, 16, 16, 255), 4.f); break;   // kami rounding 4, outline off (donor default)
    case 3: {
        dl->AddRectFilled(min, max, IM_COL32(25, 25, 25, 255), 5.f);
        const ImU32 rings[] = { IM_COL32(0, 0, 0, 255), IM_COL32(50, 50, 50, 255), IM_COL32(0, 0, 0, 255) };
        for (int grow = 0; grow < 3; ++grow) {
            const float inset = (float)(3 - grow);
            dl->AddRect(ImVec2(min.x + inset, min.y + inset), ImVec2(max.x - inset, max.y - inset), rings[grow], 5.f);
        }
        break;
    }
    case 4:
        dl->AddRectFilledMultiColor(min, max, Accent(), Accent(), AccentA(0), AccentA(0));
        dl->AddRectFilled(ImVec2(min.x + 1.f, min.y + 1.f), ImVec2(max.x - 1.f, max.y - 1.f), IM_COL32(20, 20, 20, 255));
        break;
    case 7: dl->AddRectFilled(min, max, IM_COL32(185, 90, 105, 160)); break;
    // spectator_list.cpp media_player_donor_frame
    case 6: HavocBox(dl, min, ImVec2(max.x - min.x, max.y - min.y), 1.f); break;
    case 8:
        BlurRect(dl, min, max, 1.f, 3.f);
        dl->AddRectFilled(min, max, IM_COL32(100, 100, 100, 180), 3.f);
        break;
    case 9:
        EvolveShadow(dl, min, max, 12.f, 0.25f);
        dl->AddRectFilled(min, ImVec2(max.x, max.y - 1.f), IM_COL32(29, 29, 29, 255), 4.f, ImDrawFlags_RoundCornersTop);
        dl->AddRectFilled(ImVec2(min.x, max.y - 1.f), max, Accent());
        break;
    case 10: {
        const int rgb[3] = { (int)(g_wmTextColor[0] * 255.f), (int)(g_wmTextColor[1] * 255.f), (int)(g_wmTextColor[2] * 255.f) };
        const float half = floorf((max.x - min.x) / 2.f);
        for (const float lineY : { min.y, max.y - 1.f }) {
            LegendwareGradient(dl, ImVec2(min.x, lineY), ImVec2(half, 1.f), rgb, 170, 240);
            LegendwareGradient(dl, ImVec2(min.x + half, lineY), ImVec2(max.x - min.x - half, 1.f), rgb, 240, 170);
        }
        dl->AddRectFilled(ImVec2(min.x, min.y + 1.f), ImVec2(max.x, max.y - 1.f), IM_COL32(10, 10, 10, 150));
        break;
    }
    case 11: {
        const float* h = s_interiumHues;   // stepped by the watermark the same frame
        dl->AddRectFilledMultiColor(min, max, InteriumHsv(h[0], 100), InteriumHsv(h[1], 100), InteriumHsv(h[2], 100), InteriumHsv(h[3], 100));
        dl->AddRectFilledMultiColor(ImVec2(min.x + 1.f, min.y + 1.f), ImVec2(max.x - 1.f, max.y - 1.f),
            InteriumHsv(h[0], 255), InteriumHsv(h[1], 255), InteriumHsv(h[2], 255), InteriumHsv(h[3], 255));
        dl->AddRectFilled(ImVec2(min.x + 2.f, min.y + 2.f), ImVec2(max.x - 2.f, max.y - 2.f), IM_COL32(0, 0, 0, 255));
        break;
    }
    case 15:
        dl->AddRectFilled(min, max, IM_COL32(20, 20, 20, 255), 5.f);
        dl->AddRect(min, max, IM_COL32(0, 0, 0, 255), 0.f, 0, 1.f);
        break;
    case 16: BillwareBox(dl, ImVec2(min.x + 4.f, min.y + 4.f), ImVec2(max.x - 4.f, max.y - 4.f), 1.f); break;
    case 17: DnaBox(dl, ImVec2(min.x + 3.f, min.y + 3.f), ImVec2(max.x - 3.f, max.y - 3.f), IM_COL32(10, 10, 12, 200), IM_COL32(4, 4, 5, 50)); break;
    case 18: DnaBox(dl, ImVec2(min.x + 3.f, min.y + 3.f), ImVec2(max.x - 3.f, max.y - 3.f), IM_COL32(20, 20, 22, 250), IM_COL32(4, 4, 5, 100)); break;
    case 19:
        dl->AddRectFilled(min, max, kCucumberBg, 15.f);
        dl->AddRectFilled(ImVec2(min.x + 15.f, max.y - 2.f), ImVec2(max.x - 15.f, max.y), Accent());
        break;
    case 21:
        GamesenseFrame(dl, ImVec2(min.x + 3.f, min.y + 3.f), ImVec2(max.x - 3.f, max.y - 3.f));
        GamesenseBar(dl, ImVec2(min.x + 3.f, min.y + 3.f), ImVec2(max.x - 3.f, max.y - 3.f));
        break;
    default: break;
    }
}
