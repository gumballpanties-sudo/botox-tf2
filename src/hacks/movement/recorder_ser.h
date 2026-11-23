#pragma once
// RECORDER, the pure half. Engine free on purpose: tests/recorder_test.cpp covers it, and
// recorder.cpp calls it instead of repeating it so the test covers the code that really runs.
// One frame = one tick's sent cmd + the position/velocity the hull had BEFORE that cmd ran
// (botox frame_t). Position drives the start check, the approach and the path line; playback
// itself is buttons/move/view only, never a teleport. NOT botox: the live half (RecFrame::live).
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include <algorithm>

inline constexpr int kRecFlOnGround = 1 << 0;   // m_fFlags FL_ONGROUND
inline constexpr int kRecFlDucking  = 1 << 1;   // m_fFlags FL_DUCKING (duck finished: ground move cropped 1/3)

struct RecFrame
{
    float pitch = 0.f, yaw = 0.f;        // view angles this tick
    float forward = 0.f, side = 0.f, up = 0.f;
    int   buttons = 0;
    short mouseDx = 0, mouseDy = 0;
    int   weaponDef = 0;                  // m_iItemDefinitionIndex of the held weapon
    int   weaponSlot = -1;                // its m_hMyWeapons index, -1 = unknown
    bool  hasVel = false;
    float posX = 0.f, posY = 0.f, posZ = 0.f;
    float velX = 0.f, velY = 0.f, velZ = 0.f;
    int   flags = -1;                     // m_fFlags before the cmd (ground / duck), -1 = REC2 clip, unknown
    // REC4, live replay. raw = the player's own cmd before any movement feature, keys = feature binds down
    // that cmd (vk bitmap). live = a feature was armed or rewrote the cmd: playback lays raw and the same
    // features run again off the real hull. A pin (edge surf, texture bug, air stuck ...) is exact float:
    // the recorded presses only fit the recorded position, 0.005u off the start = miss
    bool  live = false;
    float rawPitch = 0.f, rawYaw = 0.f, rawForward = 0.f, rawSide = 0.f, rawUp = 0.f;
    int   rawButtons = 0;
    uint64_t keys[4] = {};
};

// IN_JUMP DUCK FORWARD BACK MOVELEFT MOVERIGHT: the buttons movement features write. Attack etc. always
// replay from the sent cmd
inline constexpr int kRecMoveMask = (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 9) | (1 << 10);

// sent cmd vs raw: did the movement pipeline change it (view steer counts)
inline bool RecCmdChanged(const RecFrame& f)
{
    return f.rawPitch != f.pitch || f.rawYaw != f.yaw || f.rawForward != f.forward || f.rawSide != f.side
        || f.rawUp != f.up || ((f.rawButtons ^ f.buttons) & kRecMoveMask) != 0;
}

inline void RecKeySet(uint64_t keys[4], int vk) { if (vk > 0 && vk < 256) keys[vk >> 6] |= 1ull << (vk & 63); }
inline bool RecKeyDown(const uint64_t keys[4], int vk) { return vk > 0 && vk < 256 && ((keys[vk >> 6] >> (vk & 63)) & 1) != 0; }

// a feature bind asked while the recorder may own the cmd. state -1 = no playback (ask the keyboard),
// 0 = approach / raw frame (nothing was armed there: all off), 1 = live frame (bind mode 1 hold / 2 toggle =
// the recorded key, any other mode = always on). Returns -1 or the answer
inline int RecBindAnswer(int state, const uint64_t keys[4], int vk, int mode)
{
    if (state <= 0) return state < 0 ? -1 : 0;
    return (mode == 1 || mode == 2) ? (RecKeyDown(keys, vk) ? 1 : 0) : 1;
}

struct RecClipData
{
    std::string map;
    int tickrate = 67;                    // frames are per tick: other rate = wrong speed
    std::vector<RecFrame> frames;
};

inline constexpr uint32_t kRecMagicV2 = 0x32434552u;    // "REC2": no flags
inline constexpr uint32_t kRecMagicV3 = 0x33434552u;    // "REC3": + flags per frame
inline constexpr uint32_t kRecMagic   = 0x34434552u;    // "REC4": + live, raw input, feature keys
inline constexpr int      kRecMaxTick = 10 * 60 * 1000; // ~10 real minutes @ 1000 tps upper bound
inline constexpr size_t   kRecFrameBytesV2 = 4 * 5 + 4 + 2 * 2 + 4 + 4 + 1 + 4 * 6;
inline constexpr size_t   kRecFrameBytesV3 = kRecFrameBytesV2 + 4;
inline constexpr size_t   kRecFrameBytes   = kRecFrameBytesV3 + 1 + 4 * 5 + 4 + 8 * 4;

// Fixed layout, explicit field writes (not a struct memcpy) so padding/compiler ABI never
// matters. Header: magic, tickrate, map length + bytes, frame count. Always writes REC4.
inline bool RecSerialize(const RecClipData& clip, std::vector<uint8_t>& out)
{
    if (clip.frames.size() > (size_t)kRecMaxTick || clip.map.size() > 255) return false;
    out.clear();
    out.reserve(16 + clip.map.size() + clip.frames.size() * kRecFrameBytes);

    const auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back((uint8_t)(v >> (i * 8))); };
    const auto putF  = [&](float v) { uint32_t u; std::memcpy(&u, &v, 4); put32(u); };
    const auto putI  = [&](int v)   { put32((uint32_t)v); };
    const auto putS  = [&](short v) { out.push_back((uint8_t)(v & 0xFF)); out.push_back((uint8_t)((v >> 8) & 0xFF)); };

    put32(kRecMagic);
    putI(clip.tickrate);
    put32((uint32_t)clip.map.size());
    out.insert(out.end(), clip.map.begin(), clip.map.end());
    put32((uint32_t)clip.frames.size());
    for (const RecFrame& f : clip.frames)
    {
        putF(f.pitch); putF(f.yaw);
        putF(f.forward); putF(f.side); putF(f.up);
        putI(f.buttons);
        putS(f.mouseDx); putS(f.mouseDy);
        putI(f.weaponDef); putI(f.weaponSlot);
        out.push_back(f.hasVel ? 1 : 0);
        putF(f.posX); putF(f.posY); putF(f.posZ);
        putF(f.velX); putF(f.velY); putF(f.velZ);
        putI(f.flags);
        out.push_back(f.live ? 1 : 0);
        putF(f.rawPitch); putF(f.rawYaw);
        putF(f.rawForward); putF(f.rawSide); putF(f.rawUp);
        putI(f.rawButtons);
        for (const uint64_t k : f.keys) { put32((uint32_t)k); put32((uint32_t)(k >> 32)); }
    }
    return true;
}

inline bool RecDeserialize(const std::vector<uint8_t>& in, RecClipData& out)
{
    out = RecClipData{};
    size_t p = 0;
    const auto have  = [&](size_t n) { return in.size() - p >= n; };
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

    if (!have(12)) return false;
    const uint32_t magic = get32();
    if (magic != kRecMagic && magic != kRecMagicV3 && magic != kRecMagicV2) return false;
    const bool v4 = magic == kRecMagic, v3 = v4 || magic == kRecMagicV3;
    const size_t frameBytes = v4 ? kRecFrameBytes : v3 ? kRecFrameBytesV3 : kRecFrameBytesV2;
    out.tickrate = getI();
    const uint32_t mapLen = get32();
    if (mapLen > 255 || !have(mapLen + 4)) return false;
    out.map.assign((const char*)in.data() + p, mapLen);
    p += mapLen;
    const uint32_t count = get32();
    if (count > (uint32_t)kRecMaxTick || !have((size_t)count * frameBytes)) return false;

    out.frames.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        RecFrame f{};
        f.pitch = getF(); f.yaw = getF();
        f.forward = getF(); f.side = getF(); f.up = getF();
        f.buttons = getI();
        f.mouseDx = getS(); f.mouseDy = getS();
        f.weaponDef = getI(); f.weaponSlot = getI();
        f.hasVel = in[p++] != 0;
        f.posX = getF(); f.posY = getF(); f.posZ = getF();
        f.velX = getF(); f.velY = getF(); f.velZ = getF();
        if (v3) f.flags = getI();
        if (v4)   // older clip: live stays false = raw replay, as before
        {
            f.live = in[p++] != 0;
            f.rawPitch = getF(); f.rawYaw = getF();
            f.rawForward = getF(); f.rawSide = getF(); f.rawUp = getF();
            f.rawButtons = getI();
            for (uint64_t& k : f.keys) { const uint64_t lo = get32(); k = lo | ((uint64_t)get32() << 32); }
        }
        out.frames.push_back(f);
    }
    return true;
}

// ---- playback math (botox movement_recorder.cpp, 1:1) ------------------------------------

inline float RecWrap180(float a)
{
    while (a > 180.f) a -= 360.f;
    while (a < -180.f) a += 360.f;
    return a;
}

inline float RecSmoothstep(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

// botox yaw_offset. mode 0 original, 1 left (+90), 2 right (-90), 3 backwards, 4 spinning.
// Smooth end: the offset eases to 0 (spin decelerates to a stop) over the last smoothEndTicks.
inline float RecYawOffset(int mode, size_t frameCount, size_t idx, float smoothEndTicks, float spinPerTick)
{
    if (mode <= 0 || frameCount == 0) return 0.f;
    const float n = (float)(frameCount - 1);
    const float m = std::clamp(smoothEndTicks, 0.f, n);
    const float i = std::min((float)idx, n);

    if (mode == 4)
    {
        const float e = n - m;
        if (m < 1.f || i <= e) return spinPerTick * i;
        const float d = i - e;
        return spinPerTick * (e + d - d * d / (2.f * m));
    }
    const float base = mode == 1 ? 90.f : mode == 2 ? -90.f : 180.f;
    return m < 1.f ? base : base * RecSmoothstep((n - i) / m);
}

// botox 11-17 approach_wish on TF2's WalkMove (tf_gamemovement.cpp:1751). Old P steer (20 x
// distance) crossed the spot mid stride and started the replay from the wrong speed. Ground
// accelerate is a speed command along the wish dir (v.dir -> min(v.dir + step, wish); TF2 lifts
// the step over friction for a low wish, :1781), so ask for the speed that still brakes to 0 on
// the spot; last tick = the exact remaining distance. In: offset to the spot, velocity before the
// cmd, friction = sv_friction * surface friction, crop = 1/3 while FL_DUCKING on ground. Out: world
// wish x/y (crop undone), 0 = nothing to do.
inline constexpr float kRecApproachBrake = 400.f;   // u/s^2, under friction + accel: lands without overshoot

inline void RecApproachWish(float dx, float dy, float vx, float vy, float dt, float friction, float stopspeed,
                            float crop, float& wx, float& wy)
{
    wx = wy = 0.f;
    // friction runs first (CGameMovement::Friction)
    if (const float s = std::sqrt(vx * vx + vy * vy); s >= 0.1f)
    {
        const float k = std::max(s - std::max(s, stopspeed) * friction * dt, 0.f) / s;
        vx *= k; vy *= k;
    }
    const float dist = std::sqrt(dx * dx + dy * dy);
    float tx = 0.f, ty = 0.f;
    if (dist > 1e-4f)
    {
        const float sp = std::min(dist / dt, std::sqrt(2.f * kRecApproachBrake * dist)) / dist;
        tx = dx * sp; ty = dy * sp;
    }
    float ex = tx - vx, ey = ty - vy;
    const float e = std::sqrt(ex * ex + ey * ey);
    if (e < 1e-3f) return;
    ex /= e; ey /= e;
    // WalkMove zeroes a speed under 1: 0.5 = dead stop
    const float wish = std::min(std::max(tx * ex + ty * ey, 0.5f) / std::max(crop, 0.1f), 450.f);
    wx = ex * wish; wy = ey * wish;
}

// world wish -> cmd move under view yaw (deg): Source forward (c, s), right (s, -c)
inline void RecWishToMove(float wx, float wy, float yawDeg, float& fwd, float& side)
{
    const float r = yawDeg * 3.14159265358979323846f / 180.f;
    const float c = std::cos(r), s = std::sin(r);
    fwd  = wx * c + wy * s;
    side = wx * s - wy * c;
}

// botox 11-17 clipper session (clip_tick / finish_clip). Starts on input from rest (frame 0 =
// standing: playback walks there and stands), ends after stopTicks at rest without input,
// trailing still ticks cut, under minTicks = a tap, dropped. Moving without a rest start (after
// playback, a bhop chain) never opens one. A jump past what the move allows = teleport/respawn:
// the run before it is the clip.
struct RecFinished
{
    std::vector<RecFrame> frames;
    bool rolled = false;                  // hit the cap, front cut: frame 0 is moving
};

struct RecClipSession
{
    std::deque<RecFrame> frames;
    bool active = false, rolled = false;
    int  still = 0;

    void Drop() { active = rolled = false; still = 0; frames.clear(); }

    // closes the session; true = kept (pushed to done)
    bool Finish(size_t minTicks, std::vector<RecFinished>& done)
    {
        const size_t keep = frames.size() - std::min((size_t)still, frames.size());
        const bool kept = active && keep >= std::max<size_t>(minTicks, 2);
        if (kept)
        {
            RecFinished c;
            c.frames.assign(frames.begin(), frames.begin() + (std::ptrdiff_t)keep);
            c.rolled = rolled;
            done.push_back(std::move(c));
        }
        Drop();
        return kept;
    }

    // one cmd's frame; returns the teleport jump that closed the session (0 = none)
    float Tick(const RecFrame& f, bool canStart, bool isStill, size_t cap, int stopTicks, float tickrate,
               size_t minTicks, std::vector<RecFinished>& done)
    {
        float teleport = 0.f;
        if (active && !frames.empty())
        {
            const RecFrame& l = frames.back();
            const float vl = std::sqrt(l.velX * l.velX + l.velY * l.velY + l.velZ * l.velZ);
            const float vf = std::sqrt(f.velX * f.velX + f.velY * f.velY + f.velZ * f.velZ);
            const float reach = std::max(vl, vf) / tickrate * 1.5f + 32.f;
            const float jx = f.posX - l.posX, jy = f.posY - l.posY, jz = f.posZ - l.posZ;
            const float jump = std::sqrt(jx * jx + jy * jy + jz * jz);
            if (jump > reach) { teleport = jump; Finish(minTicks, done); }
        }
        if (!active)
        {
            if (!canStart) return teleport;
            Drop();
            active = true;
        }
        frames.push_back(f);
        still = isStill ? still + 1 : 0;
        while (frames.size() > std::max<size_t>(cap, 2)) { frames.pop_front(); rolled = true; }
        if (still >= std::max(stopTicks, 1)) Finish(minTicks, done);
        return teleport;
    }
};
