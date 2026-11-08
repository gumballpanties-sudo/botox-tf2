// pjseam.h -- heroin pixeljump seam height test + the pixel hop's pure rules / tables, no engine deps
// (tests/pjseam_test.cpp)
//
// heroin FUN_18007dcc0(z, pz) 1:1 (decomp: both sign branches reduce to the same rule):
//   same integer part (trunc toward zero), and the hull's hundredths bucket is the point's
//   or up to 2 BELOW it. Seam 638.029 -> z in [638.00, 638.03). 637.984 is under the lip's
//   integer = falls past, never pins (the old |dz| <= 0.05 window fired on it, rode=0).
#pragma once
#include <algorithm>
#include <cmath>

namespace pjs
{
inline bool SeamHit(float z, float pz)
{
    const int iz = (int)z, ip = (int)pz;
    if (iz != ip)
        return false;
    const int hz = (int)((z - (float)iz) * 100.f);
    const int hp = (int)((pz - (float)ip) * 100.f);
    const int d = hp - hz;
    return d >= 0 && d <= 2;
}

// wall-only pixeljump takes ANY pin on the pressed wall. A pin at the launch floor's own height is the hull coming
// back down onto the floor beside the wall (11-04 log: 6/6 fires "catch z-0.005 need=0.00", rode=0 every time), not
// a surf. Within k_floorBand of the launch z the ground check (2u down trace) owns the hull anyway
constexpr float k_floorBand = 2.f;
inline bool FloorCatch(float z, float launchZ)
{
    const float d = z - launchZ;
    return d > -k_floorBand && d < k_floorBand;
}

// ---- pixel hop (pixelhop.inl = botox css sergo.inl SgPixelHop = webz.dll FUN_180016480 / planner 1800098c0 /
// sim 180008f40). Rules + tables = css sergo_pure.h sg:: 1:1
// FUN_180033c70: wrap to [-180, 180]
inline float NormYaw(float a)
{
    if (!std::isfinite(a)) return 0.f;
    if (a < -180.f || a > 180.f)
    {
        const float n = std::round(std::fabs(a * (1.f / 360.f)));
        a = a >= 0.f ? a - n * 360.f : a + n * 360.f;
    }
    return a;
}
// hung on a pin: |vz + h| <= 0.05 (h = half gravity per tick, 6.0 at 66 tick)
inline bool PhPinned(float vz, float h) { return std::fabs(vz + h) <= 0.05f; }
// FUN_180016480: the cmd + IN_JUMP sim took the jump = END vz beats max(START vz, 0) by 100
inline bool PhJumpTook(float vz0, float vz1) { return vz1 > std::max(vz0, 0.f) + 100.f; }
// one plan (DAT_180099200 keeps the last). keep = your own move; duck 0 yours / 1 on / 2 off
struct PhPlan { bool found = false, keep = true; float yaw = 0.f, move = 0.f; int duck = 0; float speed = 0.f; int ticks = 1; };
// 1-tick passes: faster landing wins (by > 0.01). 3-tick fallback (byTicks): sooner first, then faster
inline bool PhBetter(const PhPlan& c, const PhPlan& b, bool byTicks)
{
    if (!c.found) return false;
    if (!b.found) return true;
    if (byTicks && c.ticks != b.ticks) return c.ticks < b.ticks;
    return b.speed + 0.01f < c.speed;
}
// the search stops once a landing keeps your speed within 0.5
inline bool PhDone(const PhPlan& b, float speed0) { return b.found && speed0 - 0.5f <= b.speed; }
// not webz: a steered land on the floor your OWN move lands on anyway (css log 11-08: face3 move 0.05 for
// 3 ticks, z 17 -> 0) is a plain bhop that ate your strafe. A pixel hop is a surface you'd miss
inline bool PhSameFloor(bool ownFound, float ownZ, float landZ) { return ownFound && std::fabs(landZ - ownZ) < 1.f; }
// CPU skip (not webz, same answer): CategorizePosition reads vz after the half-gravity step and never grounds
// above NON_JUMP_VELOCITY 140 (gamemovement.cpp). 3-tick horizon = vz0 - 5h at its last check
inline bool PhCanLand(float vz0, float h) { return vz0 - 5.f * h <= 140.f; }
// world wish yaw = face yaw turned AWAY from your along-face velocity (webz sign)
inline float PhwSign(float velYaw, float wallYaw) { return NormYaw(velYaw - wallYaw) < 0.f ? 1.f : -1.f; }
constexpr float k_phwVelOff[]  = { 0.f, -20.f, 20.f, -45.f, 45.f, -70.f, 70.f, -90.f, 90.f };              // 0x180081190 (no face)
constexpr float k_phwVelMove[] = { 450.f, 30.f, 5.f };                                                     // 0x1800811b8
constexpr float k_phwOff[]     = { 1.f, 6.f, 13.f, 22.f, 30.f, 45.f, 60.f, 75.f, 85.f, 89.999f };          // 0x180081120 (face)
constexpr float k_phwMove[]    = { 0.001f, 0.05f, 0.25f, 1.f, 5.f, 15.f, 30.f, 450.f };                    // 0x180081148
constexpr float k_phwOff3[]    = { 6.f, 22.f, 45.f, 75.f, 89.999f };                                       // 0x180081168 (3-tick)
constexpr float k_phwMove3[]   = { 0.05f, 1.f, 15.f, 450.f };                                              // 0x180081180
constexpr int   k_phwDuck[]    = { 2, 1 };                                                                 // unduck first

// world wish (yaw, size) -> cmd forward/side for view yaw (FUN_18002dac0 / FUN_18002e0c0)
inline void WishToCmd(float wishYaw, float size, float viewYaw, float& fwd, float& side)
{
    const float d = NormYaw(wishYaw - viewYaw) * 0.017453292f;
    fwd  = std::cos(d) * size;
    side = -std::sin(d) * size;
}
}
