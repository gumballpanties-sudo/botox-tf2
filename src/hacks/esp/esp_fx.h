#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>

// Unit C2 cross-thread ESP extras: sound pings (CSoundEmitterSystem_EmitSound hook, visuals.cpp)
// and damage numbers (player_hurt event, aim/events.cpp). Both are pushed from the GAME thread;
// esp.cpp's Draw() runs off the render-thread IEngineVGui::Paint hook -- same cross-thread shape
// flipworld.cpp's SRWLOCK core already solves in this tree. A spinlock is used here instead so
// this header stays <atomic>/<chrono> only (no <windows.h>), since it is included from both
// dih-native TUs and the aim/ typed SDK world (which esp.h's own banner comment says must not
// see dih's globals.h / windows types). Critical sections are a few float writes -- bounded,
// so a spin is fine.
//
// The render thread NEVER reads live entities here: the push side snapshots position (and, for
// damage numbers, amount/crit) into these fixed arrays, and Draw() reads only that copy.

struct EspFxSoundPing { float x, y, z, bornMs; bool used; };
struct EspFxDamageNum { float x, y, z, bornMs; int amount; bool crit; bool used; };

inline constexpr int kEspFxMaxSoundPings = 16;
inline constexpr int kEspFxMaxDamageNums = 24;

inline std::atomic_flag  g_espFxLock = ATOMIC_FLAG_INIT;
inline EspFxSoundPing    g_espFxSoundPings[kEspFxMaxSoundPings] = {};
inline EspFxDamageNum    g_espFxDamageNums[kEspFxMaxDamageNums] = {};
inline int               g_espFxSoundNext  = 0;
inline int               g_espFxDamageNext = 0;

// ESP avatars (unit E5): esp.cpp's DrawPlayers() runs off IEngineVGui::Paint, BEFORE this
// frame's ImGui::NewFrame() -- Avatar_Get()/AddImage() need the ImGui context mid-frame, so
// the box rect + steamid is snapshotted here instead and drawn later from BuildImGuiFrame()
// (render.cpp's RenderEspAvatars()), same cross-thread shape as the sound pings above.
struct EspFxAvatarSlot { float minX, minY, maxX, maxY; uint64_t steamid64; };
inline constexpr int kEspFxMaxAvatars = 32;
inline EspFxAvatarSlot   g_espFxAvatars[kEspFxMaxAvatars] = {};
inline int               g_espFxAvatarCount = 0;

struct EspFxLockGuard
{
    EspFxLockGuard()  { while (g_espFxLock.test_and_set(std::memory_order_acquire)) {} }
    ~EspFxLockGuard() { g_espFxLock.clear(std::memory_order_release); }
};

inline void EspFx_BeginAvatarFrame()
{
    EspFxLockGuard _g;
    g_espFxAvatarCount = 0;
}

inline void EspFx_PushAvatar(float minX, float minY, float maxX, float maxY, uint64_t steamid64)
{
    EspFxLockGuard _g;
    if (g_espFxAvatarCount >= kEspFxMaxAvatars) return;
    g_espFxAvatars[g_espFxAvatarCount++] = { minX, minY, maxX, maxY, steamid64 };
}

inline int EspFx_CopyAvatars(EspFxAvatarSlot* out, int maxOut)
{
    EspFxLockGuard _g;
    const int n = g_espFxAvatarCount < maxOut ? g_espFxAvatarCount : maxOut;
    for (int i = 0; i < n; ++i) out[i] = g_espFxAvatars[i];
    return n;
}

inline float EspFx_NowMs()
{
    using namespace std::chrono;
    return (float)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

inline void EspFx_PushSoundPing(float x, float y, float z)
{
    EspFxLockGuard _g;
    g_espFxSoundPings[g_espFxSoundNext] = { x, y, z, EspFx_NowMs(), true };
    g_espFxSoundNext = (g_espFxSoundNext + 1) % kEspFxMaxSoundPings;
}

inline void EspFx_PushDamageNum(float x, float y, float z, int amount, bool crit)
{
    EspFxLockGuard _g;
    g_espFxDamageNums[g_espFxDamageNext] = { x, y, z, EspFx_NowMs(), amount, crit, true };
    g_espFxDamageNext = (g_espFxDamageNext + 1) % kEspFxMaxDamageNums;
}

// Draw-side: copies out everything still alive (age < lifetimeMs) under the lock. The render
// thread never walks the shared arrays directly.
inline int EspFx_CopySoundPings(EspFxSoundPing* out, int maxOut, float nowMs, float lifetimeMs)
{
    int n = 0;
    EspFxLockGuard _g;
    for (int i = 0; i < kEspFxMaxSoundPings && n < maxOut; ++i)
        if (g_espFxSoundPings[i].used && (nowMs - g_espFxSoundPings[i].bornMs) < lifetimeMs)
            out[n++] = g_espFxSoundPings[i];
    return n;
}

inline int EspFx_CopyDamageNums(EspFxDamageNum* out, int maxOut, float nowMs, float lifetimeMs)
{
    int n = 0;
    EspFxLockGuard _g;
    for (int i = 0; i < kEspFxMaxDamageNums && n < maxOut; ++i)
        if (g_espFxDamageNums[i].used && (nowMs - g_espFxDamageNums[i].bornMs) < lifetimeMs)
            out[n++] = g_espFxDamageNums[i];
    return n;
}
