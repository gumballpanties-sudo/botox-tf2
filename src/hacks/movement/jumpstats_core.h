#pragma once
// JUMPSTATS, the pure half (botox movement.cpp jump_stats 1:1). Engine free:
// tests/jumpstats_test.cpp covers it. Fed one (start, now) pair per cmd: start = the state the
// engine ran the cmd on, now = the state it left. Prints on a clean landing, like botox.
// TF2 remap: botox thresholds are CS:GO 250 knife speed + 301.99 jump impulse + 32 hull. TF2 runs
// each class at its own m_flMaxspeed, jumps 268.33 (tf_gamemovement CheckJumpButton) and the hull is
// 48 wide, so the gates scale the DISPLACEMENT (distance minus hull) by JsScale. Shown distance =
// displacement + 48, the same edge to edge measure botox prints.
#include <cmath>
#include <algorithm>

struct JsFrame
{
    bool  ground = false, walk = false;
    float x = 0.f, y = 0.f, z = 0.f, vx = 0.f, vy = 0.f, vz = 0.f;
    int   airDash = 0;   // m_iAirDash: a scout double jump is not a jump stat
};

struct JsCmd { int tick = 0; bool duck = false; int mouseDx = 0; bool trick = false; };

struct JsResult
{
    const char* name = "";
    int   tier = 0;          // 0..5 -> botox k_colors
    float distance = 0.f, pre = 0.f, max = 0.f, height = 0.f;
    int   strafes = 0, sync = 0;
};

struct JsState
{
    bool  active = false, jb = false, voided = false, ducked = false, duckOpen = false;
    int   chain = 0, groundTicks = 0, duckTicks = 0, lastTick = 0;
    int   strafes = 0, lastDir = 0, syncTicks = 0, gainTicks = 0;
    float pre = 0.f, max = 0.f, peak = 0.f, sx = 0.f, sy = 0.f, sz = 0.f;
};

constexpr float kJsCsHull = 32.f, kJsTfHull = 48.f;

// maxspeed/250 * (268.33/301.99): same airtime x run speed ratio as the CS:GO tiers were cut for
inline float JsScale(float maxspeed) { return maxspeed > 1.f ? maxspeed / 250.f * (268.3281573f / 301.993377f) : 1.f; }

// botox gate (CS units, hull included) -> TF2 displacement + TF2 hull
inline float JsGate(float cs, float scale) { return (cs - kJsCsHull) * scale + kJsTfHull; }

inline bool JsStep(JsState& s, const JsFrame& start, const JsFrame& now, const JsCmd& c,
                   float scale, bool showFails, JsResult& out)
{
    if (c.tick != s.lastTick + 1) s.active = false;
    s.lastTick = c.tick;

    if (!start.walk || !now.walk) { s.active = false; return false; }

    const float speedIn  = std::hypot(start.vx, start.vy);
    const float speedOut = std::hypot(now.vx, now.vy);

    s.groundTicks = start.ground ? s.groundTicks + 1 : 0;
    if (s.groundTicks > 2) s.chain = 0;

    const auto takeOff = [&](bool jb) {
        s.active = true; s.jb = jb; s.voided = false;
        s.sx = start.x; s.sy = start.y; s.sz = start.z; s.peak = start.z;
        s.pre = s.max = speedIn;
        s.strafes = s.lastDir = s.syncTicks = s.gainTicks = 0;
        s.ducked = s.duckOpen = c.duck;
        s.duckTicks = 0;
    };

    if (start.ground && !now.ground)
    {
        s.chain = s.groundTicks == 1 ? s.chain + 1 : 0;
        takeOff(false);
    }
    else if (!start.ground && !now.ground && start.vz <= 0.f && now.vz > 0.f)
        takeOff(true);

    if (!s.active) return false;

    if (!now.ground)
    {
        s.max  = std::max(s.max, speedOut);
        s.peak = std::max(s.peak, now.z);
        if (speedOut > 50.f)
        {
            ++s.syncTicks;
            if (speedOut > speedIn + 0.000001f) ++s.gainTicks;
        }
        const int dir = c.mouseDx > 0 ? 1 : c.mouseDx < 0 ? -1 : 0;
        if (dir && dir != s.lastDir) { ++s.strafes; s.lastDir = dir; }

        if (s.duckOpen && c.duck) ++s.duckTicks;
        else                      s.duckOpen = false;

        if (c.trick || now.airDash > start.airDash) s.voided = true;
        return false;
    }

    s.active = false;
    if (s.voided || std::fabs(now.z - s.sz) >= (s.jb ? 16.f : 2.f)) return false;

    const float disp = std::hypot(now.x - s.sx, now.y - s.sy);
    const float distance = disp + kJsTfHull;
    if (distance <= JsGate(showFails ? 150.f : 220.f, scale) || distance >= JsGate(280.f, scale)) return false;

    struct Type { const char* name; float tiers[5]; };
    static constexpr Type kJump  = { "J",   { 227.f, 232.f, 237.f, 241.f, 243.f } };
    static constexpr Type kLong  = { "LJ",  { 230.f, 235.f, 240.f, 244.f, 246.f } };
    static constexpr Type kMini  = { "MJ",  { 220.f, 225.f, 230.f, 234.f, 236.f } };
    static constexpr Type kBhop  = { "BH",  { 230.f, 234.f, 235.f, 238.f, 240.f } };
    static constexpr Type kMulti = { "MBH", { 235.f, 237.f, 242.f, 245.f, 247.f } };
    static constexpr Type kJb    = { "JB",  { 250.f, 255.f, 260.f, 265.f, 268.f } };

    const Type& t = s.jb            ? kJb
                  : s.chain > 1     ? kMulti
                  : s.chain == 1    ? kBhop
                  : !s.ducked       ? kJump
                  : s.duckTicks <= 1 ? kMini
                                     : kLong;

    int tier = 0;
    while (tier < 5 && distance >= JsGate(t.tiers[tier], scale)) ++tier;

    out.name = t.name; out.tier = tier; out.distance = distance;
    out.pre = s.pre; out.max = s.max; out.height = s.peak - s.sz;
    out.strafes = s.strafes;
    out.sync = s.syncTicks ? (int)(s.gainTicks * 100.f / s.syncTicks) : 0;
    return true;
}
