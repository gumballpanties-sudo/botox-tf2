#include "killeffects.h"
#include "../../render/render.h"       // W2S_Project
#include "../movement/movement.h"     // Vec3
#include "unitE_logic.h"  // n_unitE::McXpValue/McXpIcon
#include <vector>
#include <random>
#include <cmath>
#include <cfloat>
#include <algorithm>

// botox hacks/misc/kill_effects.cpp custom-particle styles (soul/smoke/balls/mc), ported as
// world-space ImGui billboards: circle/quad drawn at W2S_Project(pos), no raw D3D / no engine
// particle system. Through walls (no visibility trace) -- botox's OWN pass-1 behaviour before
// its 10-09 kfx_world depth-tested rewrite; same scoped-down tier, documented upgrade path.

namespace
{
    constexpr size_t kKfxCap = 2048;   // botox: 4096 -- halved, no perf budget proven here yet

    enum : int { kSoul = 5, kSmoke = 3, kBalls = 6, kMcCrit = 8, kMcXp = 9 };

    struct KfxParticle
    {
        int   style = 0;
        Vec3  pos{}, vel{};
        float age = 0.f, life = 1.f, size = 1.f, seed = 0.f;
        float floorZ = -FLT_MAX;   // balls/xp: clamp instead of a hull trace (see killeffects.h)
        bool  resting = false;
        float color[4] = { 1.f, 1.f, 1.f, 1.f };
        int   icon = 0;            // mc xp orb tier 0..10
        float tint = 1.f;          // mc crit shade
    };

    std::vector<KfxParticle> g_kfx;
    std::mt19937 g_rng(0x6b667821u);

    float Rand01() { return std::uniform_real_distribution<float>(0.f, 1.f)(g_rng); }
    float RandRange(float lo, float hi) { return lo + (hi - lo) * Rand01(); }

    void Push(const KfxParticle& p) { if (g_kfx.size() < kKfxCap) g_kfx.push_back(p); }

    void ParticleColor(float out[4])
    {
        if (g_dpMulticolor)
        {
            // HSB->RGB, same spread botox's particle_color() multicolor path picks (random hue,
            // fixed sat/val) -- kept inline, one call site, not worth its own header function.
            const float h = Rand01() * 6.f;
            const int i = (int)h; const float f = h - i;
            const float p = 0.f, q = 1.f - f, t = f;
            switch (i % 6)
            {
            case 0: out[0] = 1.f; out[1] = t; out[2] = p; break;
            case 1: out[0] = q;   out[1] = 1.f; out[2] = p; break;
            case 2: out[0] = p;   out[1] = 1.f; out[2] = t; break;
            case 3: out[0] = p;   out[1] = q; out[2] = 1.f; break;
            case 4: out[0] = t;   out[1] = p; out[2] = 1.f; break;
            default: out[0] = 1.f; out[1] = p; out[2] = q; break;
            }
        }
        else { out[0] = g_dpColor[0]; out[1] = g_dpColor[1]; out[2] = g_dpColor[2]; }
        out[3] = g_dpColor[3];
    }
}

// ==== spawn: soul (botox spawn_soul, 1:1 velocity math; no hitbox resolver -> stacked
// offsets, botox's OWN no-bones fallback: origin + 8 points 8..72u up) ======================
void KfxPush_Soul(const Vec3& origin)
{
    const int   style  = std::clamp(g_dpSoulStyle, 0, 3);
    const int   amount = std::clamp(g_dpAmount, 1, 40);
    const float size   = std::clamp(g_dpSize, 0.25f, 4.f);
    const float life   = std::clamp(g_dpLifetime, 0.5f, 10.f);

    Vec3 points[8];
    for (int i = 0; i < 8; ++i) points[i] = origin + Vec3(0.f, 0.f, 8.f + 8.f * i);
    const Vec3 core = points[0] + Vec3(0.f, 0.f, 40.f);

    for (const Vec3& point : points)
    {
        for (int i = 0; i < amount; ++i)
        {
            KfxParticle p{};
            p.pos = point + Vec3(RandRange(-5.f, 5.f), RandRange(-5.f, 5.f), RandRange(-5.f, 5.f));
            p.style = kSoul;

            const float angle = RandRange(0.f, 6.2831853f);
            switch (style)
            {
            case 0: { const float s = RandRange(8.f, 30.f);
                      p.vel = Vec3(cosf(angle) * s, sinf(angle) * s, RandRange(4.f, 15.f)); break; }
            case 1: p.vel = Vec3(RandRange(-2.5f, 2.5f), RandRange(-2.5f, 2.5f), RandRange(12.f, 45.f)); break;
            case 2: { const float s = RandRange(15.f, 50.f);
                      p.vel = Vec3(cosf(angle) * s, sinf(angle) * s, RandRange(-10.f, 10.f)); break; }
            default:
            {
                Vec3 toCore = core - p.pos;
                const float len = toCore.Length();
                if (len > 0.1f) toCore = toCore * (1.f / len);
                p.vel = toCore * RandRange(20.f, 40.f);
                break;
            }
            }

            p.size = RandRange(1.f, 2.f) * size;
            p.life = RandRange(0.7f, 1.f) * life;
            ParticleColor(p.color);
            Push(p);
        }
    }
}

// ==== spawn: smoke puff (botox spawn_smoke; no hitbox resolver -> single emit at origin,
// same amount/size/life knobs) ===============================================================
void KfxPush_Smoke(const Vec3& origin)
{
    const int   amount = std::clamp(g_dpAmount, 1, 40);
    const float size   = std::clamp(g_dpSize, 0.25f, 3.f);
    const float life   = std::clamp(g_dpLifetime, 0.5f, 10.f);

    for (int i = 0; i < amount; ++i)
    {
        const float z = RandRange(-1.f, 1.f), angle = RandRange(0.f, 6.2831853f);
        const float r = sqrtf(std::max(0.f, 1.f - z * z));
        const Vec3 dir(cosf(angle) * r, sinf(angle) * r, z);

        KfxParticle puff{};
        puff.style = kSmoke;
        puff.pos   = origin;
        puff.vel   = dir * RandRange(2.f, 9.f) + Vec3(0.f, 0.f, RandRange(2.f, 8.f));
        puff.size  = RandRange(0.75f, 1.15f) * size * 6.f;   // botox radius is per-hitbox-capsule units
        puff.life  = RandRange(0.6f, 1.f) * life;
        puff.seed  = Rand01();
        const float shade = RandRange(150.f, 205.f) / 255.f;
        puff.color[0] = puff.color[1] = puff.color[2] = shade; puff.color[3] = 1.f;
        Push(puff);
    }
}

// ==== spawn: bouncy balls (botox spawn_balls; floor-clamped at spawn Z instead of a hull
// trace -- see killeffects.h upgrade note) ===================================================
void KfxPush_Balls(const Vec3& origin)
{
    const int   amount = std::clamp(g_dpAmount, 5, 100);
    const float size   = std::clamp(g_dpSize, 0.5f, 4.f);
    const float life   = std::clamp(g_dpLifetime, 1.f, 8.f);
    const Vec3  at = origin; // caller already added the +40 "at" offset

    for (int i = 0; i < amount; ++i)
    {
        KfxParticle ball{};
        ball.pos = at + Vec3(RandRange(-8.f, 8.f), RandRange(-8.f, 8.f), RandRange(-4.f, 6.f));
        const float angle = RandRange(0.f, 6.2831853f);
        const float speed = RandRange(80.f, 260.f);
        ball.vel    = Vec3(cosf(angle) * speed, sinf(angle) * speed, RandRange(180.f, 420.f));
        ball.style  = kBalls;
        ball.size   = RandRange(1.6f, 3.2f) * size;
        ball.life   = RandRange(0.8f, 1.2f) * life;
        ball.floorZ = at.z - 40.f;   // feet of the victim who died here
        ParticleColor(ball.color);
        Push(ball);
    }
}

// ==== spawn: minecraft crit x's + xp orbs (botox spawn_mc/spawn_mc_xp, 1:1 math, fixed
// OBB fallback -- the one botox itself uses when no collideable is found) ====================
void KfxPush_Mc(const Vec3& origin, const Vec3& obbMins, const Vec3& obbMaxs)
{
    const float size = std::clamp(g_dpSize, 0.25f, 3.f);
    const int   mode = std::clamp(g_dpMcMode, 0, 2);
    const float xpLife = std::clamp(g_dpMcXpTime, 1.f, 60.f);

    if (mode != 0)
    {
        const int count = std::uniform_int_distribution<int>(4, 10)(g_rng);
        for (int i = 0; i < count; ++i)
        {
            KfxParticle orb{};
            orb.style = kMcXp;
            orb.life  = xpLife;
            orb.pos   = origin;
            orb.vel   = Vec3((Rand01() * 0.2f - 0.1f) * 2.f, (Rand01() * 0.2f - 0.1f) * 2.f, Rand01() * 0.2f * 2.f) * 40.f;
            orb.size  = size;
            orb.floorZ = origin.z - 40.f;
            orb.icon  = n_unitE::McXpIcon(n_unitE::McXpValue(std::uniform_int_distribution<int>(1, 40)(g_rng)));
            Push(orb);
        }
    }
    if (mode == 2) return;

    const Vec3 feet = origin - Vec3(0.f, 0.f, 40.f);   // caller's "at" is feet+40
    const float width  = obbMaxs.x - obbMins.x;
    const float height = obbMaxs.z - obbMins.z;

    for (int burst = 0; burst < 3; ++burst)
        for (int i = 0; i < 16; ++i)
        {
            const float d = RandRange(-1.f, 1.f), e = RandRange(-1.f, 1.f), f = RandRange(-1.f, 1.f);
            if (d * d + e * e + f * f > 1.f) continue;

            KfxParticle crit{};
            crit.style = kMcCrit;
            crit.life  = 10.f;
            crit.age   = (float)burst * -1.f;   // negative age = later burst, matches botox's tick_acc stagger
            crit.pos   = feet + Vec3(width * d / 4.f, width * f / 4.f, obbMins.z + height * (0.5f + e / 4.f));

            Vec3 base(RandRange(-1.f, 1.f), RandRange(-1.f, 1.f), RandRange(-1.f, 1.f));
            const float len = base.Length();
            base = len > 1e-4f ? base * ((Rand01() + Rand01() + 1.f) * 0.15f * 0.4f / len) : Vec3();
            base.z += 0.1f;
            crit.vel = (base * 0.1f + Vec3(d, f, e + 0.2f) * 0.4f) * 40.f;

            crit.tint = Rand01() * 0.3f + 0.6f;
            crit.size = 0.1f * (Rand01() * 0.5f + 0.5f) * 2.f * 0.75f * 40.f * size;
            Push(crit);
        }
}

// ==== step + draw, one frame at a time, driven by RenderKillEffects (render.cpp's frame
// build, main thread -- same call site pass 1 used) ==========================================
namespace
{
    void StepParticle(KfxParticle& p, float dt)
    {
        p.age += dt;
        if (p.style == kBalls || p.style == kMcXp)
        {
            p.vel.z -= (p.style == kBalls ? 640.f : 20.f) * dt;   // mc xp orb gravity is ~1/32 of a ball's
            Vec3 next = p.pos + p.vel * dt;
            if (next.z <= p.floorZ)
            {
                next.z = p.floorZ;
                p.vel.z = -p.vel.z * 0.4f;             // bounce, botox's own restitution constant
                if (fabsf(p.vel.z) < 24.f) { p.vel.z = 0.f; p.resting = true; }
                p.vel.x *= 0.7f; p.vel.y *= 0.7f;       // ground friction while resting/skidding
            }
            p.pos = next;
        }
        else if (p.style == kMcCrit)
        {
            p.vel.z -= 0.5f * 40.f * dt;   // MC CritParticle gravity (1 block = 40u)
            p.vel = p.vel * powf(0.7f, dt * 20.f);   // MC friction, applied at 20hz in botox -- continuous here
            p.pos = p.pos + p.vel * dt;
        }
        else
        {
            p.vel.z -= 20.f * dt;   // mild drift gravity for soul/smoke, same shape botox's is
            p.pos = p.pos + p.vel * dt;
        }
    }

    bool IsDead(const KfxParticle& p) { return p.age >= p.life; }

    void DrawParticle(ImDrawList* dl, const KfxParticle& p)
    {
        ImVec2 s;
        if (!W2S_Project(p.pos, s)) return;
        const float frac = std::clamp(1.f - p.age / p.life, 0.f, 1.f);

        switch (p.style)
        {
        case kMcCrit:
        {
            const int shade = (int)(p.tint * 255.f);
            const ImU32 col = IM_COL32(shade, shade, shade, (int)(255.f * frac));
            const float h = std::max(2.f, p.size * 0.02f);
            dl->AddLine(ImVec2(s.x - h, s.y - h), ImVec2(s.x + h, s.y + h), col, 1.f);
            dl->AddLine(ImVec2(s.x + h, s.y - h), ImVec2(s.x - h, s.y + h), col, 1.f);
            break;
        }
        case kMcXp:
        {
            // tier 0..10 -> green..yellow, no texture upload for one orb icon (ponytail: the
            // real asset is a generated 185-line header for a quad botox itself draws tinted;
            // a flat tinted square keeps the tier readout without a texture-cache subsystem).
            const float t = p.icon / 10.f;
            const ImU32 col = IM_COL32((int)(255 * t), 220, (int)(60 + 80 * (1.f - t)), (int)(255.f * frac));
            const float h = std::max(2.f, p.size * 3.f);
            dl->AddRectFilled(ImVec2(s.x - h, s.y - h), ImVec2(s.x + h, s.y + h), col);
            break;
        }
        default:
        {
            const ImU32 col = IM_COL32((int)(p.color[0] * 255.f), (int)(p.color[1] * 255.f),
                                        (int)(p.color[2] * 255.f), (int)(p.color[3] * 255.f * frac));
            const float r = std::max(1.f, p.size * (p.style == kSmoke ? 1.6f : 1.f));
            dl->AddCircleFilled(s, r, col, 10);
            break;
        }
        }
    }
}

void RenderKillEffects()
{
    if (g_kfx.empty()) return;

    const float dt = ImGui::GetIO().DeltaTime;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    for (auto& p : g_kfx)
    {
        if (!(p.resting && (p.style == kBalls))) StepParticle(p, dt);
        else p.age += dt;   // resting balls still age out
        DrawParticle(dl, p);
    }

    g_kfx.erase(std::remove_if(g_kfx.begin(), g_kfx.end(), IsDead), g_kfx.end());
}
