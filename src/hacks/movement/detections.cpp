#include "detections.h"
#include "movement.h"
#include "detections_core.h"
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>

struct Toast { std::string text; unsigned long long bornMs; };
static std::vector<Toast> s_toasts;
static constexpr unsigned long long kToastLifeMs = 1400;

static void Announce(const char* text)
{
    if (s_toasts.size() > 24) s_toasts.erase(s_toasts.begin());   // runaway guard
    s_toasts.push_back({ text, GetTickCount64() });
}

void RenderDetections()
{
    if (!g_detections && !g_detectionStackEb) { s_toasts.clear(); return; }

    MvInd_t s{};
    MV_GetIndicators(s);
    static int s_lastTick = -1;
    const bool freshTick = s.ms != 0 && GetTickCount64() - s.ms <= 250 && s.tick != s_lastTick;
    if (freshTick)
    {
        s_lastTick = s.tick;

        if (g_detections)
        {
            if (s.tbHit) Announce("texture bug");
            if (s.asHit) Announce("air stuck");
            if (s.wcHit) Announce("wall climb");
            if (s.psHit) Announce("pixel surf");
            if (s.astHit) Announce("ps assist");
            if (s.mjHit) Announce("mini jump");
            if (s.jbHit) Announce("jump bug");
            // edgebug has its own streak counter below when stack detection is on; plain
            // single-hit announce only when the streak feature is off
            if (!g_detectionStackEb && s.ebHit) Announce("edge bug");
        }

        if (g_detectionStackEb)
        {
            static DetStack st;
            const int broke = DetStackUpdate(st, s.ebHit, g_detectionGraceTicks);
            if (broke > 1) { char buf[32]; snprintf(buf, sizeof(buf), "edge bug x%d", broke); Announce(buf); }
            else if (broke == 1 && g_detections) Announce("edge bug");
        }
    }

    if (s_toasts.empty()) return;
    const unsigned long long now = GetTickCount64();
    s_toasts.erase(std::remove_if(s_toasts.begin(), s_toasts.end(),
                   [&](const Toast& t) { return now - t.bornMs > kToastLifeMs; }), s_toasts.end());
    if (s_toasts.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImFont* font = g_fontMi ? g_fontMi : (g_fontBold ? g_fontBold : ImGui::GetFont());
    const float fs = font->FontSize;
    float y = io.DisplaySize.y * 0.3f;
    for (const Toast& t : s_toasts)
    {
        const float age = (now - t.bornMs) / (float)kToastLifeMs;
        const float a = age > 0.7f ? (1.f - age) / 0.3f : 1.f;
        const ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, t.text.c_str());
        const ImVec2 p(roundf(io.DisplaySize.x * 0.5f - sz.x * 0.5f), roundf(y));
        const ImU32 col = IM_COL32((int)(g_detectionColor[0]*255),(int)(g_detectionColor[1]*255),(int)(g_detectionColor[2]*255),(int)(g_detectionColor[3]*a*255));
        dl->AddText(font, fs, ImVec2(p.x+1.f,p.y+1.f), IM_COL32(0,0,0,(int)(a*255)), t.text.c_str());
        dl->AddText(font, fs, p, col, t.text.c_str());
        y += sz.y + 2.f;
    }
}
