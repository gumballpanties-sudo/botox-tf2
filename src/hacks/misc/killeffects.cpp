#include "killeffects.h"
#include "../movement/movement.h"     // Vec3
#include "../movement/prediction.h"   // EntV3
#include <cstdint>

// ==== engine particle dispatch (blood / confetti / explosion) =====================
// x64 TF2 client.dll sigs, verified in Desktop/Aletherium-main/Amalgam/src/SDK/Helpers/
// Particles/Particles.h (CTE_DispatchEffect, Get_ParticleSystemIndex) -- never guessed.
// "ParticleEffect" is a real DECLARE_CLIENT_EFFECT name (source-sdk-2013-master
// game/client/c_particle_system.cpp:260), so CTE_DispatchEffect("ParticleEffect", data)
// is the same path C_TEParticleSystem / DispatchParticleEffect() uses engine-side.
namespace
{
    using DispatchEffect_t = void(__fastcall*)(const char*, const void*);
    using GetParticleIdx_t = int(__fastcall*)(const char*);
    DispatchEffect_t s_dispatchEffect = nullptr;
    GetParticleIdx_t s_getParticleIdx = nullptr;
    bool s_sigsFailed = false;

    // Layout copied verbatim from the verified Amalgam struct -- the engine reads this by
    // offset, so field order/types must match exactly, not just the fields this file uses.
    struct TfParticleControlPoint { int attach; Vec3 offset; };
    struct TfEffectData
    {
        Vec3  origin, start, normal, angles;
        int   flags = 0;
        int   entIndex = 0;
        float scale = 0.f, magnitude = 0.f, radius = 0.f;
        int   attachmentIndex = 0;
        short surfaceProp = 0;
        int   material = 0, damageType = 0, hitBox = 0;
        unsigned char color = 0;
        bool  customColors = false;
        Vec3  customColor1, customColor2;
        bool  controlPoint1 = false;
        TfParticleControlPoint cp1{};
        int   effectName = 0;
    };

    bool ResolveParticleSigs()
    {
        if (s_dispatchEffect && s_getParticleIdx) return true;
        if (s_sigsFailed) return false;

        HMODULE hClient = GetModuleHandleA("client.dll");
        if (!hClient) return false;

        if (!s_dispatchEffect)
            if (uint8_t* a = PatternScan(hClient,
                "48 89 5C 24 ? 57 48 83 EC ? 48 8B D9 48 8B FA 48 8D 4C 24"))
                s_dispatchEffect = reinterpret_cast<DispatchEffect_t>(a);

        if (!s_getParticleIdx)
            if (uint8_t* a = PatternScan(hClient,
                "40 53 48 83 EC ? 48 8B D9 48 85 C9 74 ? 48 8B 0D ? ? ? ? 48 8B D3 48 8B 01 FF 50 ? 3D"))
                s_getParticleIdx = reinterpret_cast<GetParticleIdx_t>(a);

        s_sigsFailed = !s_dispatchEffect || !s_getParticleIdx;
        if (s_sigsFailed)
            DebugLog("KFX: particle sigs missing (dispatch=%p getIdx=%p) -- engine styles off",
                     (void*)s_dispatchEffect, (void*)s_getParticleIdx);
        return !s_sigsFailed;
    }

    // log each unprecached name once (names are the static literals below)
    void LogMissingOnce(const char* name)
    {
        static const char* s_logged[16] = {};
        for (const char*& p : s_logged)
        {
            if (p == name) return;
            if (!p) { p = name; DebugLog("KFX: particle \"%s\" not precached, skipped", name); return; }
        }
    }

    int LookupParticle(const char* name)
    {
        __try { return s_getParticleIdx(name); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    bool DispatchEngineParticle(const char* name, const Vec3& origin, const Vec3& angles)
    {
        if (!ResolveParticleSigs()) return false;
        // GetParticleSystemIndex returns 0 (not -1) for an unprecached name
        // (sdk cdll_client_int.cpp:816) -- 0 = invalid, skip it.
        const int idx = LookupParticle(name);
        if (idx <= 0) { LogMissingOnce(name); return false; }
        __try
        {
            TfEffectData data{};
            data.origin = origin;
            data.start  = origin;
            data.angles = angles;
            data.scale  = 1.f;   // CEffectData ctor default
            // ParticleEffectCallback reads the system index from m_nHitBox (sdk
            // c_particle_system.cpp:186, particle_parse.cpp:409). m_iEffectName is the
            // EffectDispatch table slot -- putting idx there left hitBox 0 = wrong system.
            data.hitBox = idx;
            s_dispatchEffect("ParticleEffect", &data);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // botox try_names(): first precached system in the list wins
    template <size_t N>
    bool TryNames(const char* const (&names)[N], const Vec3& origin, const Vec3& angles)
    {
        for (const char* n : names)
            if (DispatchEngineParticle(n, origin, angles)) return true;
        return false;
    }
}

// ==== custom (non-engine) style pool, defined in killeffects_world.cpp ============
void KfxPush_Soul(const Vec3& origin);
void KfxPush_Smoke(const Vec3& origin);
void KfxPush_Balls(const Vec3& origin);
void KfxPush_Mc(const Vec3& origin, const Vec3& obbMins, const Vec3& obbMaxs);

void KillEffects_OnDeath(int victimIdx, int attackerIdx, bool localDied)
{
    if (!g_dpEnable || localDied) return;   // botox: never on the local player's own death
    if (victimIdx < 1) return;

    void* pVictim = GetClientEntity(victimIdx);
    if (!pVictim || g_offOrigin <= 0) return;

    Vec3 at{};
    __try { at = EntV3(pVictim, g_offOrigin); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    at.z += 40.f;
    const Vec3 up(0.f, 0.f, 0.f);

    const int localIdx = GetLocalPlayerIndex();
    const int style = g_dpStyle;

    switch (style)
    {
    case 3: KfxPush_Smoke(at); return;
    case 5: KfxPush_Soul(at);  return;
    case 8: KfxPush_Mc(at, Vec3(-16.f, -16.f, 0.f), Vec3(16.f, 16.f, 72.f)); return;
    case 6:
        if (attackerIdx == localIdx) KfxPush_Balls(at);   // botox: attacker-is-local gate
        return;
    default: break;   // 0 blood, 1 sparks(->blood), 2 confetti, 4 explosion, 7 melt(->blood)
    }

    // every name checked present in TF2's pcf files (tf2_misc_*.vpk); first = what TF2
    // itself dispatches (tf_fx_blood.cpp:117, c_tf_player.cpp:1113, tf_fx_explosions.cpp:90)
    static const char* const k_blood[]     = { "blood_spray_red_01", "blood_impact_red_01" };
    static const char* const k_confetti[]  = { "bday_confetti", "bday_confetti_colors" };
    static const char* const k_explosion[] = { "ExplosionCore_Wall", "ExplosionCore_MidAir", "ExplosionCore_buildings" };
    const Vec3 bloodAng(-90.f, 0.f, 0.f);   // botox: blood sprays up

    bool done = false;
    if (style == 2)      done = TryNames(k_confetti, at, up);
    else if (style == 4) done = TryNames(k_explosion, at, up);
    // style 1 (sparks) and 7 (melt) have no ported engine analog -- blood, and blood is
    // also the fallback when this map precached none of the picked style's systems
    if (!done) TryNames(k_blood, at, bloodAng);
}
