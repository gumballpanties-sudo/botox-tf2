#pragma once
// RECORDER SERIALIZATION, the pure half. Engine free on purpose: tests/recorder_test.cpp
// round-trips this, and recorder.cpp calls it instead of repeating it so the test covers the
// code that really runs. One frame = one recorded tick's input + the position/velocity it
// produced (position/velocity are read-only, used for the clipper preview and "lock going to
// start"; playback drives movement through buttons/move/viewangles only, never a teleport).
#include <cstdint>
#include <cstring>
#include <vector>

struct RecFrame
{
    float pitch = 0.f, yaw = 0.f;        // view angles this tick
    float forward = 0.f, side = 0.f, up = 0.f;
    int   buttons = 0;
    short mouseDx = 0, mouseDy = 0;
    int   weaponSlot = -1;                // -1 = not forced / unknown
    float posX = 0.f, posY = 0.f, posZ = 0.f;
    float velX = 0.f, velY = 0.f, velZ = 0.f;
};

inline constexpr uint32_t kRecMagic   = 0x31434552u;   // "REC1"
inline constexpr int      kRecMaxTick = 10 * 60 * 1000; // ~10 real minutes @ 1000 tps upper bound

// Fixed layout, explicit field writes (not a struct memcpy) so padding/compiler ABI never
// matters. Header: magic, frame count. Refuses an absurd count rather than trying to allocate it.
inline bool RecSerialize(const std::vector<RecFrame>& frames, std::vector<uint8_t>& out)
{
    if (frames.size() > (size_t)kRecMaxTick) return false;
    out.clear();
    out.reserve(8 + frames.size() * sizeof(RecFrame));

    const auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back((uint8_t)(v >> (i * 8))); };
    const auto putF   = [&](float v) { uint32_t u; std::memcpy(&u, &v, 4); put32(u); };
    const auto putI   = [&](int v)   { put32((uint32_t)v); };
    const auto putS   = [&](short v) { out.push_back((uint8_t)(v & 0xFF)); out.push_back((uint8_t)((v >> 8) & 0xFF)); };

    put32(kRecMagic);
    put32((uint32_t)frames.size());
    for (const RecFrame& f : frames)
    {
        putF(f.pitch); putF(f.yaw);
        putF(f.forward); putF(f.side); putF(f.up);
        putI(f.buttons);
        putS(f.mouseDx); putS(f.mouseDy);
        putI(f.weaponSlot);
        putF(f.posX); putF(f.posY); putF(f.posZ);
        putF(f.velX); putF(f.velY); putF(f.velZ);
    }
    return true;
}

inline bool RecDeserialize(const std::vector<uint8_t>& in, std::vector<RecFrame>& out)
{
    out.clear();
    if (in.size() < 8) return false;
    size_t p = 0;
    const auto get32 = [&]() -> uint32_t
    {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= (uint32_t)in[p + i] << (i * 8);
        p += 4;
        return v;
    };
    const auto getF = [&]() -> float { uint32_t u = get32(); float v; std::memcpy(&v, &u, 4); return v; };
    const auto getI = [&]() -> int   { return (int)get32(); };
    const auto getS = [&]() -> short { short v = (short)(in[p] | (in[p + 1] << 8)); p += 2; return v; };

    if (get32() != kRecMagic) return false;
    const uint32_t count = get32();
    if (count > (uint32_t)kRecMaxTick) return false;
    // 4 floats... let's just check remaining bytes cover the declared count exactly
    constexpr size_t kFrameBytes = 4*5 + 4 + 2*2 + 4 + 4*6;   // pitch,yaw,fwd,side,up(5f) + buttons + 2 shorts + weaponSlot + pos+vel(6f)
    if (in.size() < 8 + (size_t)count * kFrameBytes) return false;

    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        RecFrame f{};
        f.pitch = getF(); f.yaw = getF();
        f.forward = getF(); f.side = getF(); f.up = getF();
        f.buttons = getI();
        f.mouseDx = getS(); f.mouseDy = getS();
        f.weaponSlot = getI();
        f.posX = getF(); f.posY = getF(); f.posZ = getF();
        f.velX = getF(); f.velY = getF(); f.velZ = getF();
        out.push_back(f);
    }
    return true;
}
