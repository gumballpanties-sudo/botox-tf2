// Textually included by movind.cpp (same translation unit) -- see keyind_fx.h for its vars.
// Four call sites in RenderMoveIndicators: label text, tag colour, hit edge, end-of-frame draw.
#include "keyind_fx.h"
#include <cstdlib>
#include <cmath>

// custom label: the slot's saved text if the toggle is on and it is non-empty, else the
// short tag already used everywhere else (keeps the fade/slot-layout logic untouched).
static const char* KeyIndFx_Label(int slot, const char* baseTag)
{
    if (g_miCustomLabels && slot >= 0 && slot < MI_COUNT && g_miLabels[slot][0]) return g_miLabels[slot];
    return baseTag;
}

// stable per-slot hue, not time-seeded -- a tag's colour must not flicker frame to frame.
static void KeyIndFx_Color(int slot, float& r, float& g, float& b)
{
    if (!g_miRandomColor) return;
    const unsigned h = (unsigned)(slot * 2654435761u);
    const float hue = (float)(h % 360) / 360.f;
    const float x = 1.f - fabsf(fmodf(hue * 6.f, 2.f) - 1.f);
    if (hue < 1.f/6)      { r = 1.f; g = x;   b = 0.f; }
    else if (hue < 2.f/6) { r = x;   g = 1.f; b = 0.f; }
    else if (hue < 3.f/6) { r = 0.f; g = 1.f; b = x;   }
    else if (hue < 4.f/6) { r = 0.f; g = x;   b = 1.f; }
    else if (hue < 5.f/6) { r = x;   g = 0.f; b = 1.f; }
    else                  { r = 1.f; g = 0.f; b = x;   }
}

// a landed trick spawns a few dots that drift out and fade over ~0.4s. Capped count: a
// stacked edgebug chain cannot grow this past the cap, it just resets the oldest slots.
struct KeyIndFxParticle { ImVec2 pos; ImVec2 vel; float life; ImU32 col; };
static constexpr int kKeyIndFxMax = 64;
static KeyIndFxParticle s_keyIndFxParts[kKeyIndFxMax];
static int s_keyIndFxHead = 0;

static void KeyIndFx_OnHit(ImVec2 at, ImU32 col)
{
    if (!g_miParticles) return;
    for (int i = 0; i < 5; ++i)
    {
        const float ang = (float)(rand() % 360) * (3.14159265f / 180.f);
        KeyIndFxParticle& p = s_keyIndFxParts[s_keyIndFxHead];
        s_keyIndFxHead = (s_keyIndFxHead + 1) % kKeyIndFxMax;
        p.pos = at;
        p.vel = ImVec2(cosf(ang) * 40.f, sinf(ang) * 40.f - 20.f);
        p.life = 1.f;
        p.col = col;
    }
}

static void KeyIndFx_DrawParticles(ImDrawList* dl, float dt)
{
    if (!g_miParticles) return;
    for (auto& p : s_keyIndFxParts)
    {
        if (p.life <= 0.f) continue;
        p.life -= dt * 2.5f;
        if (p.life <= 0.f) continue;
        p.pos.x += p.vel.x * dt;
        p.pos.y += p.vel.y * dt;
        ImU32 c = (p.col & 0x00FFFFFF) | ((ImU32)(((p.col >> 24) & 0xFF) * p.life) << 24);
        dl->AddCircleFilled(p.pos, 1.5f, c, 6);
    }
}
