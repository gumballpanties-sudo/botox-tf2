#include "watermark.h"
#include "watermark_styles.h"   // unit E: extra style skins, same knobs
#include <string>
#include <vector>

// botox csgo-sdk draw_watermark() 1:1: dark filled box (25,25,25) + 1px grey outline
// (50,50,50), bold white brand then " | "-joined accent-coloured info fields, fixed
// box height, box top-right by default, 8px off each edge.

ImVec4 g_wmBox = { };

void RenderWatermark()
{
    g_wmBox = { };
    if (!g_wmEnable) return;

    // unit E style skins (watermark_styles.cpp) -- consumed and returned when g_wmStyle != 0.
    if (RenderWatermarkStyled()) return;

    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0.f || io.DisplaySize.y <= 0.f) return;

    ImFont* boldFont = g_menuBold ? g_menuBold : ImGui::GetFont();   // verdana bold 11
    ImFont* slimFont = g_menuReg  ? g_menuReg  : ImGui::GetFont();   // verdana 11

    const ImU32 accent = ImGui::GetColorU32(ImGuiCol_Accent);
    const ImU32 white  = IM_COL32(255, 255, 255, 255);

    // Info fields, same source data the old box used, each later joined with " | ".
    char buf[4][64];
    int  nb = 0;
    auto addPart = [&](const char* fmt, auto... args) {
        if (nb >= 4) return;
        snprintf(buf[nb], sizeof(buf[0]), fmt, args...);
        if (buf[nb][0]) ++nb;
    };
    if (g_wmShowFPS)
        addPart("%d fps", (int)(g_fps.load() + 0.5f));
    if (g_wmShowPing && g_conn.load())
        addPart("%d ms", g_ping.load());
    if (g_wmShowLoss && g_conn.load())
        addPart("%d%% loss", g_loss.load());
    if (g_wmShowTime) {
        time_t t = time(nullptr);
        struct tm lt;
        localtime_s(&lt, &t);
        addPart("%02d:%02d", lt.tm_hour, lt.tm_min);
    }

    struct Seg { std::string text; ImU32 color; ImFont* font; };
    std::vector<Seg> segs;
    if (g_wmShowLabel)
        segs.push_back({ g_wmLabel[0] ? g_wmLabel : "botox", white, boldFont });
    for (int i = 0; i < nb; ++i)
        segs.push_back({ std::string(" | ") + buf[i], accent, slimFont });

    if (segs.empty()) return;

    float contentW = 0.f, contentH = 0.f;
    for (auto& s : segs) {
        const ImVec2 sz = s.font->CalcTextSizeA(s.font->FontSize, FLT_MAX, 0.f, s.text.c_str());
        contentW += sz.x;
        if (sz.y > contentH) contentH = sz.y;
    }

    constexpr float padX = 14.f, boxH = 25.f, minW = 95.f, edge = 8.f;
    const float rounding = ImGui::GetStyle().WindowRounding;
    const float width = segs.size() > 1 ? (std::max)(contentW + padX * 2.f, minW) : contentW + padX * 2.f;

    float x = edge, y = edge;
    switch (g_wmPos) {
    case WM_TOP_LEFT:      x = edge;                                y = edge; break;
    case WM_TOP_CENTER:    x = (io.DisplaySize.x - width) * 0.5f;   y = edge; break;
    case WM_TOP_RIGHT:     x = io.DisplaySize.x - width - edge;     y = edge; break;
    case WM_BOTTOM_LEFT:   x = edge;                                y = io.DisplaySize.y - boxH - edge; break;
    case WM_BOTTOM_CENTER: x = (io.DisplaySize.x - width) * 0.5f;   y = io.DisplaySize.y - boxH - edge; break;
    case WM_BOTTOM_RIGHT:  x = io.DisplaySize.x - width - edge;     y = io.DisplaySize.y - boxH - edge; break;
    }
    x = floorf(x); y = floorf(y);
    g_wmBox = ImVec4(x, y, x + width, y + boxH);

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + boxH), IM_COL32(25, 25, 25, 255), rounding);
    dl->AddRect(ImVec2(x + 1.f, y + 1.f), ImVec2(x + width - 1.f, y + boxH - 1.f), IM_COL32(50, 50, 50, 255), rounding);

    float tx = x + (width - contentW) * 0.5f;
    const float ty = y + (boxH - contentH) * 0.5f;
    for (auto& s : segs) {
        dl->AddText(s.font, s.font->FontSize, ImVec2(tx, ty), s.color, s.text.c_str());
        tx += s.font->CalcTextSizeA(s.font->FontSize, FLT_MAX, 0.f, s.text.c_str()).x;
    }
}
