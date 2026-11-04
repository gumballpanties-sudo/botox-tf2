// pjseam.h -- heroin pixeljump seam height test, no engine deps (tests/pjseam_test.cpp)
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
}
