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
// ---- WHERE A PIXEL SURF REALLY CATCHES: the brush top's 1/32 trace skin --------------------------
// A seam pins a hull whose move STARTS with the feet in (top, top + DIST_EPSILON]: the top plane
// enters at fraction 0 (CM_ClipBoxToBrush, f = d1 - eps clamped) and wins the trace. Over the skin
// the wall wins at fraction 0 and the hull falls past; at/under the top the plane is behind it.
// Measured: 5 recorded catches end their tick 0.0162..0.03124 over the top (routesim_test 5),
// 11-20 clip pinned at 192.00624, 11-11 log "the pin ends when the feet reach 0.03125 over the plane".
// The finder saves a seam at top + 0.0287 (z >= 0) / top + 0.0279 (z < 0), tops are integers there.
// false = pz is not such a placement (hand typed, non-integer brush top): caller keeps its old rule
constexpr float k_skin = 0.03125f;
inline bool SeamFace(float pz, float& face)
{
    face = std::floor(pz);
    return pz > face && pz - face <= k_skin + 0.001f;
}

// lift = how far over the skin still counts: 0 for a tick END that will pin, a hair for a hull
// already pinned (sv_wallbug 0 lifts it 0.03125 * dt a tick)
inline bool InSkin(float z, float face, float lift = 0.f) { return z > face && z - face <= k_skin + lift; }

// sv_wallbug 0: ticks a hull pinned at z keeps its pin. Each pinned tick lifts it k_skin * dt (Valve's
// clip leaves 1/32 u/s off the top plane) and a tick that starts over the skin falls. 11-11 log:
// 0.027 over the top rode 9 ticks, 0.021 -> 22, 0.017 -> 30 (pjseam_test). The catch's depth IS the ride
inline int SkinRideTicks(float z, float face, float dt)
{
    const float room = face + k_skin - z;      // every tick that STARTS at or under the skin's top pins
    return room >= 0.f && dt > 0.f ? (int)(room / (k_skin * dt)) + 1 : 0;
}

// ps assist's catch rank. life = ride ticks a flown catch has left, -1 = cannot tell (old clip, or the
// pin z is no skin z: an off-grid brush top). First catch is taken; after that only a KNOWN longer ride
// replaces it -- an unknown never beats a known one. enough = stop flying the list
constexpr int k_rideEnough = 12;   // past ~10 ticks the across-face drift ends a ride first (11-11 log)
inline int CatchLife(float catchZ, float dt, bool wallbug0)
{
    float face;
    return wallbug0 && SeamFace(catchZ, face) ? SkinRideTicks(catchZ, face, dt) : -1;
}
inline bool TakeCatch(bool have, int bestLife, int life) { return !have || (life >= 0 && bestLife >= 0 && life > bestLife); }
inline bool CatchEnough(int bestLife) { return bestLife < 0 || bestLife >= k_rideEnough; }

// lift: 0.001 for a hull that may already be pinned, 0 for a free-fall tick END (it must be IN the skin)
inline bool SeamHit(float z, float pz, float lift = 0.001f)
{
    // not heroin: its hundredths buckets drop the skin's top 4% (the recorded catch at top + 0.03124)
    float face;
    if (SeamFace(pz, face))
        return InSkin(z, face, lift);
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
