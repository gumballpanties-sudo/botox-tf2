#include "boot_screen.h"
#include "../screen/blur_rect.h"
#include <cmath>
#include <cstdio>
#include <algorithm>

// botox csgo-sdk boot_screen.cpp 1:1 look (12-dot spinner, growing bar, centred label),
// minus its per-init-stage timing: botox times real loader stages (modules/interfaces/
// netvars/.../effects) that TF2 injection has no equivalent of, so this runs on one fixed
// clock instead. ponytail: skipped the eject-splash half (F11 unload) and real stage
// timing -- add real stage hooks later if the fixed clock ever feels wrong in game.
namespace
{
    constexpr float kShowFor = 1.2f;   // "injecting" duration
    constexpr float kHoldFor = 0.35f;  // "injected" hold before fade

    float Cubic(float a, float b, float s)
    {
        const float r = 1.f - s;
        return 3.f * a * s * r * r + 3.f * b * s * s * r + s * s * s;
    }

    // svg keySplines = css cubic-bezier: x -> y
    float Ease(float x1, float y1, float x2, float y2, float x)
    {
        float lo = 0.f, hi = 1.f;
        for (int i = 0; i < 16; ++i)
        {
            const float mid = (lo + hi) * 0.5f;
            (Cubic(x1, x2, mid) < x ? lo : hi) = mid;
        }
        return Cubic(y1, y2, (lo + hi) * 0.5f);
    }

    // 6-dots-scale: r 0 -> 2 at 20% -> 0 over 0.6s, then frozen at 0
    float DotRadius(float t)
    {
        const float u = t / 0.6f;
        if (u >= 1.f) return 0.f;
        if (u < 0.2f) return 2.f * Ease(0.f, 1.f, 0.f, 1.f, u / 0.2f);
        return 2.f * (1.f - Ease(0.53f, 0.f, 0.61f, 0.73f, (u - 0.2f) / 0.8f));
    }
}

void RenderBootScreen()
{
    static bool  started = false;
    static float clock_  = 0.f;
    static float ramp    = 0.f;
    static bool  gone    = false;

    if (gone) return;

    if (!started) { started = true; clock_ = 0.f; }

    const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.f, 1.f / 30.f);
    clock_ += dt;

    const bool done = clock_ > kShowFor;
    const bool show = !done || (clock_ - kShowFor) < kHoldFor;
    ramp = show ? (std::min)(ramp + dt / 0.35f, 1.f) : (std::max)(ramp - dt / 0.45f, 0.f);

    if (!show && ramp <= 0.f) { gone = true; return; }

    const float fade = ramp * ramp * (3.f - 2.f * ramp);
    const float fraction = (std::min)(clock_ / kShowFor, 1.f);

    ImGuiIO& io = ImGui::GetIO();
    const float w = io.DisplaySize.x, h = io.DisplaySize.y, u = h / 1080.f;
    if (w <= 0.f || h <= 0.f) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const auto color = [fade](float a) { return IM_COL32(255, 255, 255, (int)(a * fade * 255.f + 0.5f)); };

    BlurRect(dl, ImVec2(0.f, 0.f), ImVec2(w, h), fade, 0.f);
    dl->AddRectFilled(ImVec2(0.f, 0.f), ImVec2(w, h), IM_COL32(0, 0, 0, (int)(0.18f * fade * 255.f)));

    const float grow = 0.94f + 0.06f * fade;
    const ImVec2 center(w * 0.5f, h * 0.5f + 10.f * u * (1.f - fade));

    // svg: 24 unit box, 12 dots on r 9 clockwise from top, 0.1s apart, 1.2s loop
    const float scale = 52.f * u * grow / 24.f;
    const ImVec2 spin(center.x, center.y - 22.f * u * grow);
    for (int i = 0; i < 12; ++i)
    {
        const float r = DotRadius(std::fmod(clock_ + 1.2f - (float)i * 0.1f, 1.2f)) * scale;
        if (r < 0.25f) continue;
        const float angle = (float)i * (3.14159265f / 6.f);
        dl->AddCircleFilled(ImVec2(spin.x + std::sin(angle) * 9.f * scale, spin.y - std::cos(angle) * 9.f * scale),
                            r, color(1.f));
    }

    const float barW = 260.f * u * grow, barH = (std::max)(std::round(4.f * u), 2.f);
    const ImVec2 barMin(std::floor(center.x - barW * 0.5f), std::floor(center.y + 24.f * u * grow));
    const ImVec2 barMax(barMin.x + barW, barMin.y + barH);
    dl->AddRectFilled(barMin, barMax, color(0.15f), barH * 0.5f);
    if (fraction > 0.001f)
        dl->AddRectFilled(barMin, ImVec2(barMin.x + barW * fraction, barMax.y), color(0.95f), barH * 0.5f);

    char text[32];
    if (done) snprintf(text, sizeof(text), "injected");
    else      snprintf(text, sizeof(text), "injecting   %d%%", (int)(fraction * 100.f));

    ImFont* font = g_menuBold ? g_menuBold : ImGui::GetFont();   // menu "botox" label face
    const ImVec2 extent = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, text);
    dl->AddText(font, font->FontSize, ImVec2(std::floor(center.x - extent.x * 0.5f), std::floor(barMax.y + 12.f * u)),
               color(1.f), text);
}
