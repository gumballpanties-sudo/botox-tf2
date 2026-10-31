#include "hitmarker.h"

// botox hitmarker.cpp RenderHitmarker 1:1 at botox defaults (speed 1, thickness 1, no
// outline -- this DLL has no knobs for those): 4 lines at 45 deg growing out of screen
// centre over 0.1s, then fading out while sliding 5px outward over 0.4s.
static float s_markerT = 0.f;   // seconds left to draw, counts down in RenderHitMarker
constexpr float kHmDuration = 0.5f, kHmExtend = 0.1f, kHmSlide = 5.f, kHmThickness = 1.f;

void Hitmarker_OnHurt(int damage, bool crit, bool minicrit, bool killedTarget)
{
    (void)crit; (void)minicrit;
    if (damage <= 0) return;
    (void)killedTarget;
    if (g_hmEnable) s_markerT = kHmDuration;
    // no hit sound: TF2 has its own (tf_dingalingaling)
}

void RenderHitMarker()
{
    if (!g_hmEnable || s_markerT <= 0.f) return;

    ImGuiIO& io = ImGui::GetIO();
    const float elapsed = kHmDuration - s_markerT;
    float len = g_hmSize, alpha = 1.f, offset = 0.f;
    if (elapsed < kHmExtend) len = g_hmSize * elapsed / kHmExtend;
    else
    {
        const float fade = (elapsed - kHmExtend) / (kHmDuration - kHmExtend);
        alpha = 1.f - fade;
        offset = fade * kHmSlide;
    }

    const ImU32 col = ImColor(g_hmColor[0], g_hmColor[1], g_hmColor[2], g_hmColor[3] * alpha);
    const float cx = io.DisplaySize.x * 0.5f, cy = io.DisplaySize.y * 0.5f;
    const float d = 0.70710678f;   // cos 45 == sin 45
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddLine(ImVec2(cx + offset * d, cy + offset * d), ImVec2(cx + (offset + len) * d, cy + (offset + len) * d), col, kHmThickness);
    dl->AddLine(ImVec2(cx - offset * d, cy - offset * d), ImVec2(cx - (offset + len) * d, cy - (offset + len) * d), col, kHmThickness);
    dl->AddLine(ImVec2(cx + offset * d, cy - offset * d), ImVec2(cx + (offset + len) * d, cy - (offset + len) * d), col, kHmThickness);
    dl->AddLine(ImVec2(cx - offset * d, cy + offset * d), ImVec2(cx - (offset + len) * d, cy + (offset + len) * d), col, kHmThickness);

    s_markerT -= io.DeltaTime;
}
