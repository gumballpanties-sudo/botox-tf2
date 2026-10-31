#include "points.h"
#include <vector>
#include <cstring>

struct PtsLine { char text[48]; float color[4]; float t; };
static std::vector<PtsLine> s_lines;   // render thread only, never touched off-thread

static constexpr float kLife = 1.6f;
static constexpr int   kMax  = 6;      // cap: a frag chain must not grow this unbounded

void Points_Push(const char* text, const float color[4])
{
    if (!g_ptsEnable || !text) return;
    if ((int)s_lines.size() >= kMax) s_lines.erase(s_lines.begin());
    PtsLine l{};
    strncpy_s(l.text, text, sizeof(l.text) - 1);
    memcpy(l.color, color, sizeof(l.color));
    l.t = kLife;
    s_lines.push_back(l);
}

void RenderPoints()
{
    if (!g_ptsEnable || s_lines.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImFont* font = g_fontBold ? g_fontBold : ImGui::GetFont();
    const float fs = font->FontSize;

    // Age + drop expired, oldest first -- same top-center stack every kill-feed-adjacent
    // popup uses, fading the newest line in from the top.
    float y = io.DisplaySize.y * 0.22f;
    for (size_t i = 0; i < s_lines.size();)
    {
        s_lines[i].t -= io.DeltaTime;
        if (s_lines[i].t <= 0.f) { s_lines.erase(s_lines.begin() + i); continue; }
        ++i;
    }

    for (auto& l : s_lines)
    {
        const float a = l.t > 0.3f ? 1.f : (l.t / 0.3f);   // fade the last 0.3s only
        const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.f, l.text);
        const float x = (io.DisplaySize.x - ts.x) * 0.5f;
        const ImU32 col = IM_COL32((int)(l.color[0] * 255.f), (int)(l.color[1] * 255.f),
                                   (int)(l.color[2] * 255.f), (int)(l.color[3] * 255.f * a));
        dl->AddText(font, fs, ImVec2(floorf(x), floorf(y)), col, l.text);
        y += ts.y + 2.f;
    }
}
