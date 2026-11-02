// psdash.h -- scout double jump math for the PS assist, no engine deps (tests/psdash_test.cpp)
//
// TF2 AirDash (tf_gamemovement.cpp:1023-1043) does NOT keep the hull's speed: it writes
//     m_vecVelocity = wish vector (forwardmove/sidemove in world), m_vecVelocity.z += 268.33
// so the cmd that presses the dash DECIDES the hull's horizontal velocity. Zero wish = hull stops
// dead in the air. The assist offers two wishes and lets the sim pick:
//   KEEP = the velocity it already has (the dash a player holding their line gets)
//   AIM  = toward the seam, at the speed that puts the hull over it when the arc comes back
//          down through the seam's height (AimSpeed below)
#pragma once
#include <cmath>

namespace psd
{
inline constexpr float k_dashVz = 268.3281573f;   // flDashZ, jump mod 1

// horizontal speed that lands the hull `flat` units away as the post-dash arc falls back through
// dz (= seam z - feet z, + is above). <0 = the dash never gets that high, or no travel needed
inline float AimSpeed(float dz, float flat, float vz, float g)
{
    if (g <= 0.f)
        return -1.f;
    const float v = vz + k_dashVz;
    const float d = v * v - 2.f * g * dz;
    if (d < 0.f)
        return -1.f;
    const float t = (v + std::sqrt(d)) / g;   // the DESCENDING crossing
    if (t <= 1e-4f)
        return -1.f;
    const float s = flat / t;
    return s > 450.f ? 450.f : s;             // a real client never sends more than cl_forwardspeed
}
}
