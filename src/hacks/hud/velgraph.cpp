#include "velgraph.h"
#include "../movement/movement.h"
#include <vector>
#include <algorithm>
#include <cmath>

// History ring. Game-thread publishes speed via MvInd_t (movement.h, lock-free scalar
// snapshot); this reads it on the render thread same as RenderVelocityIndicator does --
// no new cross-thread access.
struct GraphSample { float speed; bool takeOff; };
static std::vector<GraphSample> s_hist;
static int   s_histTick = 0;
static bool  s_histGround = true;
static float s_histTop = 300.f;

void RenderVelocityGraph()
{
    if (!g_vgEnable) { s_hist.clear(); return; }

    MvInd_t s{};
    MV_GetIndicators(s);
    if (s.ms == 0 || GetTickCount64() - s.ms > 250) { s_hist.clear(); return; }

    const int length = std::clamp(g_vgLength, 2, 2048);
    const int tick = s.tick;

    if (tick < s_histTick || tick - s_histTick > length) s_hist.clear();

    const int add = s_hist.empty() ? 1 : tick - s_histTick;
    for (int i = 0; i < add; ++i)
        s_hist.push_back({ s.speed, i == add - 1 && s_histGround && !s.onGround });
    if (add > 0) { s_histTick = tick; s_histGround = s.onGround; }
    while ((int)s_hist.size() > length) s_hist.erase(s_hist.begin());

    const float width  = (float)std::max(g_vgWidth, 10);
    const float height = (float)std::max(g_vgHeight, 5);
    ImGuiIO& io = ImGui::GetIO();
    const float left  = roundf(io.DisplaySize.x * 0.5f + (float)g_vgOffsetX - width * 0.5f);
    const float baseY = roundf(io.DisplaySize.y - (float)g_vgPosition);
    const int count = (int)s_hist.size();
    if (count < 2) return;
    const float step = width / (float)(length - 1);

    float top = g_vgMax;
    if (top <= 0.f)
    {
        float peak = 300.f;
        for (const auto& g : s_hist) peak = std::max(peak, g.speed);
        s_histTop += (peak - s_histTop) * (1.f - expf(-6.f * io.DeltaTime));
        top = s_histTop;
    }

    const auto edgeAlpha = [&](float x) -> float
    {
        if (!g_vgFadeSides || g_vgFade <= 0.f) return 1.f;
        const float t = (x - left) / width;
        return std::clamp(std::min(t, 1.f - t) / g_vgFade, 0.f, 1.f);
    };

    struct Pt { ImVec2 at; float a; int speed; };
    std::vector<Pt> pts;
    pts.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        const float x = left + width - (float)(count - 1 - i) * step;
        const float y = baseY - std::min(s_hist[i].speed / top, 1.f) * height;
        pts.push_back({ ImVec2(x, y), edgeAlpha(x), (int)roundf(s_hist[i].speed) });
    }

    const auto withA = [](const float* c, float a) { return IM_COL32((int)(c[0]*255), (int)(c[1]*255), (int)(c[2]*255), (int)(c[3]*a*255)); };

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    if (g_vgFill)
    {
        const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
        for (int i = 1; i < count; ++i)
        {
            dl->PrimReserve(6, 4);
            const ImDrawIdx idx = (ImDrawIdx)dl->_VtxCurrentIdx;
            dl->PrimWriteIdx(idx); dl->PrimWriteIdx(idx + 1); dl->PrimWriteIdx(idx + 2);
            dl->PrimWriteIdx(idx); dl->PrimWriteIdx(idx + 2); dl->PrimWriteIdx(idx + 3);
            dl->PrimWriteVtx(pts[i-1].at, uv, withA(g_vgFillColor, pts[i-1].a));
            dl->PrimWriteVtx(pts[i].at,   uv, withA(g_vgFillColor, pts[i].a));
            dl->PrimWriteVtx(ImVec2(pts[i].at.x, baseY),   uv, withA(g_vgFillColor, 0.f));
            dl->PrimWriteVtx(ImVec2(pts[i-1].at.x, baseY), uv, withA(g_vgFillColor, 0.f));
        }
    }

    for (int i = 1; i < count; ++i)
    {
        const float* c = (!g_vgGainColors || pts[i].speed == pts[i-1].speed) ? g_vgColor
                        : (pts[i].speed > pts[i-1].speed ? g_vgColorGain : g_vgColorLoss);
        dl->AddLine(pts[i-1].at, pts[i].at, withA(c, (pts[i-1].a + pts[i].a) * 0.5f), g_vgThickness);
    }

    if (!g_vgJumpSpeeds) return;
    ImFont* font = g_fontMi ? g_fontMi : (g_fontBold ? g_fontBold : ImGui::GetFont());
    const float fs = font->FontSize * g_vgTextScale;
    for (int i = 0; i < count; ++i)
    {
        if (!s_hist[i].takeOff || pts[i].a <= 0.f) continue;
        char label[16]; snprintf(label, sizeof(label), "%d", pts[i].speed);
        const ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, label);
        ImU32 col = withA(g_vgTextColor, pts[i].a);
        const ImVec2 p(roundf(pts[i].at.x - sz.x * 0.5f), roundf(pts[i].at.y - sz.y - 2.f));
        if (g_vgTextShadow)
            dl->AddText(font, fs, ImVec2(p.x + 1.f, p.y + 1.f), IM_COL32(0, 0, 0, (int)(255 * pts[i].a)), label);
        dl->AddText(font, fs, p, col, label);
    }
}
