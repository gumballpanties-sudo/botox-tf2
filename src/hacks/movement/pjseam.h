// pjseam.h -- heroin pixeljump seam height test + pixel hop search table, no engine deps (tests/pjseam_test.cpp)
//
// heroin FUN_18007dcc0(z, pz) 1:1 (decomp: both sign branches reduce to the same rule):
//   same integer part (trunc toward zero), and the hull's hundredths bucket is the point's
//   or up to 2 BELOW it. Seam 638.029 -> z in [638.00, 638.03). 637.984 is under the lip's
//   integer = falls past, never pins (the old |dz| <= 0.05 window fired on it, rode=0).
#pragma once

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

// pixel hop (pixelhop.inl = botox css sergo.inl SgPixelHop = sergowebz z.dll FUN_18002a420): the 384-step rolling
// land search, 12 yaw offsets off the face x 8 wish sizes x side x duck flip. Tables z.dll 0x18009d470 / 0x18009d4a0
constexpr float k_phYaw[]  = { 0.5f, 3.f, 8.f, 15.f, 22.f, 30.f, 40.f, 50.f, 60.f, 70.f, 80.f, 89.9f };
constexpr float k_phMove[] = { 0.001f, 0.05f, 0.25f, 1.f, 5.f, 15.f, 60.f, 450.f };
constexpr int   k_phSteps  = 384;
struct PhStep { float yawOff; float move; bool flipSide; bool flipDuck; };
inline PhStep PhAt(int i)
{
    i = ((i % k_phSteps) + k_phSteps) % k_phSteps;
    PhStep s;
    s.yawOff   = k_phYaw[i % 12];
    s.move     = k_phMove[(i / 12) & 7];
    s.flipSide = ((i / 96) & 1) != 0;
    s.flipDuck = ((i / 192) & 1) != 0;
    return s;
}
// the rolling search only runs while hung on a pin (vz on -halfgrav)
inline bool PhPinned(float vz, float halfGrav) { return (vz + halfGrav) <= 0.05f && (vz + halfGrav) >= -0.05f; }
}
