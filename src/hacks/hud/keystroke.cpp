#include "keystroke.h"
#include "../movement/movement.h"
#include <algorithm>

// TF2 IN_* bits, same values movement.cpp defines file-local (confirmed there, duplicated
// here rather than exported -- these are fixed engine constants, not a guess).
static constexpr int IN_JUMP      = 1 << 1;
static constexpr int IN_DUCK      = 1 << 2;
static constexpr int IN_FORWARD   = 1 << 3;
static constexpr int IN_BACK      = 1 << 4;
static constexpr int IN_MOVELEFT  = 1 << 9;
static constexpr int IN_MOVERIGHT = 1 << 10;

struct KsCell { const char* text; int button; };
// row 0: strafe + forward; row 1: duck/back/jump -- botox's two layouts (WASD-left, WASD-right)
static const KsCell kGrid[2][2][3] = {
    { { {"A", IN_MOVELEFT}, {"W", IN_FORWARD}, {"D", IN_MOVERIGHT} },
      { {"C", IN_DUCK},     {"S", IN_BACK},     {"J", IN_JUMP} } },
    { { {"C", IN_DUCK},     {"W", IN_FORWARD},  {"J", IN_JUMP} },
      { {"A", IN_MOVELEFT}, {"S", IN_BACK},     {"D", IN_MOVERIGHT} } },
};

static float s_anim[2][3] = {};

void RenderKeystrokeIndicator()
{
    if (!g_ksEnable) return;

    MvInd_t s{};
    MV_GetIndicators(s);
    if (s.ms == 0 || GetTickCount64() - s.ms > 250) return;

    ImFont* font = g_fontMi ? g_fontMi : (g_fontBold ? g_fontBold : ImGui::GetFont());
    const float fs = font->FontSize * g_ksScale;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImGuiIO& io = ImGui::GetIO();

    const auto widthOf = [&](const char* t) { return font->CalcTextSizeA(fs, FLT_MAX, 0.f, t).x; };
    float cellW = std::max({ widthOf("_"), widthOf("<"), widthOf(">") });
    const auto& grid = kGrid[std::clamp(g_ksLayout, 0, 1)];
    for (auto& row : grid) for (auto& cell : row) cellW = std::max(cellW, widthOf(cell.text));

    const float pitchX = cellW + (float)g_ksGapX;
    const float pitchY = fs + (float)g_ksGapY;
    const float cx = io.DisplaySize.x * 0.5f + (float)g_ksOffsetX;
    const float top = io.DisplaySize.y - (float)g_ksPosition;
    const float step = g_ksFade > 0.f ? io.DeltaTime / g_ksFade : 1.f;

    const auto drawCell = [&](int row, int col, const char* text, bool down)
    {
        float& a = s_anim[row][col];
        a = std::clamp(a + (down ? step : -step), 0.f, 1.f);

        const float x = cx + (float)(col - 1) * pitchX;
        const float y = roundf(top + (float)row * pitchY);

        const auto put = [&](const char* t, ImU32 col, float alpha)
        {
            if (alpha <= 0.f) return;
            const float w = widthOf(t);
            const ImVec2 p(roundf(x - w * 0.5f), y);
            if (g_ksShadow) dl->AddText(font, fs, ImVec2(p.x + 1.f, p.y + 1.f), IM_COL32(0,0,0,(int)(alpha*255)), t);
            dl->AddText(font, fs, p, col, t);
        };
        const auto u32 = [&](const float* c, float k) { return IM_COL32((int)(c[0]*255),(int)(c[1]*255),(int)(c[2]*255),(int)(c[3]*k*255)); };

        if (g_ksReleased == 0) { put("_", u32(g_ksColorReleased, 1.f - a), 1.f - a); put(text, u32(g_ksColor, a), a); }
        else if (g_ksReleased == 1)
        {
            const float r = g_ksColorReleased[0] + (g_ksColor[0]-g_ksColorReleased[0])*a;
            const float gg= g_ksColorReleased[1] + (g_ksColor[1]-g_ksColorReleased[1])*a;
            const float b = g_ksColorReleased[2] + (g_ksColor[2]-g_ksColorReleased[2])*a;
            const float al= g_ksColorReleased[3] + (g_ksColor[3]-g_ksColorReleased[3])*a;
            put(text, IM_COL32((int)(r*255),(int)(gg*255),(int)(b*255),(int)(al*255)), al);
        }
        else put(text, u32(g_ksColor, a), a);
    };

    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 3; ++col)
        {
            const KsCell& cell = grid[row][col];
            if (!g_ksDuckJump && (cell.button & (IN_DUCK | IN_JUMP))) { s_anim[row][col] = 0.f; continue; }
            drawCell(row, col, cell.text, (s.buttons & cell.button) != 0);
        }

    if (g_ksMouse)
    {
        static float arrowAnim[2] = {};
        const auto arrow = [&](int idx, const char* t, bool down, int col)
        {
            float& a = arrowAnim[idx];
            a = std::clamp(a + (down ? step : -step), 0.f, 1.f);
            if (a <= 0.f) return;
            const float x = cx + (float)(col - 1) * pitchX;
            const float y = roundf(top + 2.f * pitchY);
            const ImVec2 p(roundf(x - widthOf(t) * 0.5f), y);
            const ImU32 col32 = IM_COL32((int)(g_ksColor[0]*255),(int)(g_ksColor[1]*255),(int)(g_ksColor[2]*255),(int)(g_ksColor[3]*a*255));
            if (g_ksShadow) dl->AddText(font, fs, ImVec2(p.x+1.f,p.y+1.f), IM_COL32(0,0,0,(int)(a*255)), t);
            dl->AddText(font, fs, p, col32, t);
        };
        arrow(0, "<", s.mouseDx < 0, 0);
        arrow(1, ">", s.mouseDx > 0, 2);
    }
}
