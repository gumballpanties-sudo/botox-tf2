#pragma once
// airstuck_pure.h -- pure math shared by airstuck.inl / pixelsurf.inl (botox air_stuck.cpp +
// tick_scale.h ports). No engine, so tests/airstuck_test.cpp can run it.
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace as
{

constexpr float k_pi = 3.14159265358979323846f;

// botox stamp_rotate: a move proven in the `yaw` frame, re-expressed in the `view_yaw` frame
inline void StampRotate(float view_yaw, float yaw, float fwd, float side, float& out_fwd, float& out_side)
{
    const float delta = std::remainder(view_yaw - yaw, 360.0f) * (k_pi / 180.f);
    const float cos_d = std::cos(delta);
    const float sin_d = std::sin(delta);
    out_fwd  = cos_d * fwd - sin_d * side;
    out_side = sin_d * fwd + cos_d * side;
}

// botox set_move_toward_yaw: world yaw + magnitude -> forward/side in the cmd's view frame
inline void MoveTowardYaw(float view_yaw, float target_yaw, float forward, float& out_fwd, float& out_side)
{
    const float rotation = (target_yaw - view_yaw) * (k_pi / 180.f);
    out_fwd  = std::cos(rotation) * forward;
    out_side = -std::sin(rotation) * forward;
}

// botox n_tick::scale / n_tick::ticks: counts tuned at 64 tick, scaled to the live interval
inline float TickScale(float interval) { return std::clamp(64.f * interval, 0.25f, 4.f); }
inline int TicksScaled(int ticks_at_64, float interval)
{
    const float s = TickScale(interval);
    if (s <= 0.f) return ticks_at_64;
    const int out = static_cast<int>(std::round(static_cast<float>(ticks_at_64) / s));
    return out < 1 ? 1 : out;
}

// botox air_stuck clarity grid: forward per step so one step moves wishvel 1/32u
// ( AirAccelerate: accelspeed = airaccel * wishspeed * ipt * friction, wishspeed = fwd/100*.. )
inline float GridStep(float airaccel, float ipt, float friction, float duck_mul)
{
    return (0.03125f / (airaccel * ipt * friction * duck_mul * ipt)) / 100.0f;
}

// botox tb_wallstrafe_yaw: the along-wall strafe yaw that keeps the most speed under the 30 u/s
// air wish cap. FLT_MAX-style sentinel returned as `ok = false`
inline bool WallStrafeYaw(float nx_in, float ny_in, float vx, float vy, float cap, float& out_yaw)
{
    if (std::sqrt(vx * vx + vy * vy) < 1.f) return false;
    const float nl = std::sqrt(nx_in * nx_in + ny_in * ny_in);
    if (nl <= 0.f) return false;
    const float nx = nx_in / nl, ny = ny_in / nl;
    float tx = -ny, ty = nx;
    if (tx * vx + ty * vy < 0.f) { tx = -tx; ty = -ty; }
    const float s = tx * vx + ty * vy;
    float c = 1.f, sn = 0.f;
    if (s >= cap * 0.5f)
    {
        c  = std::clamp(cap / (s * 2.f), -1.f, 1.f);
        sn = std::sqrt(1.f - c * c);
    }
    out_yaw = std::atan2(c * ty - sn * ny, c * tx - sn * nx) * (180.f / k_pi);
    return true;
}

// park brake: wish magnitude straight against xy that takes xy to EXACTLY 0 in one AirAccelerate
// tick when it can ( accelspeed = airaccel * wish * ipt ), else the full press. Never reverses:
// a maxspeed crop below the result only brakes less
inline float BrakeWish(float xy, float airaccel, float ipt, float cap = 450.f)
{
    const float a = airaccel * ipt;
    if (a <= 0.f || xy <= 0.f) return 0.f;
    return std::min(cap, xy / a);
}

// cumidere misc_airstuck (tf2.dll FUN_1801cd900) accept: xy stopped AND |vz| on half gravity
// ( FUN_1801cade0 = -0.5 * sv_gravity * interval ), either sign
inline bool CumiStuck(float vz, float vxy, float half)
{
    return vxy < 0.1f && std::fabs(std::fabs(vz) - std::fabs(half)) < 0.1f;
}

// cumidere graze sign: yaw = wall_yaw + off * sign, AGAINST the along-wall drift
inline float CumiSign(float vel_yaw, float wall_yaw)
{
    return std::remainder(vel_yaw - wall_yaw, 360.0f) < 0.0f ? 1.0f : -1.0f;
}

// how close vz must sit to -halfgrav to count as a pin. botox (csgo) uses 0.01, but Valve's Oct 2026
// ClipVelocity (backoff -= DIST_EPSILON) ends a TF2 top-plane pin at -halfgrav + 0.03125 = -5.97:
// 0.01 made pixel surf blind to every ride on those servers (11-08 log: all pins -5.97, zero PSU:)
inline constexpr float k_pinTol = 0.05f;

// botox pixel_surf `pinned`: vz on the pin AND still sliding ( xy < 1 = a wall hang, not a surf )
inline bool SurfPinned(float vz, float vxy, float pin) { return std::fabs(vz - pin) < k_pinTol && vxy >= 1.f; }

// auto align surf stick: no candidate held = ride over, ONLY if the search ran. Pool empty before the
// stick (simsUsed 0) = nothing flown = keep. Search cut by the sim pool / cmd clock (cut) = no proof
// either: keep while the real START still pins (it is the last tick's move that pinned it)
inline bool StickLost(int heldDrift, int best, int simsUsed, bool cut, bool pinnedNow)
{
    if (heldDrift > 0 || best > 0 || simsUsed == 0) return false;
    return !(cut && pinnedNow);
}

// sv_wallbug 0 lip hang: the wish whose AirAccelerate (gamemovement.cpp:1705) takes the pre-move xy
// velocity (vx, vy) to `closing` u/s INTO the face (n = unit face normal, out of the wall) and 0 along it.
// k = airaccel * dt * friction (u/s per unit wish), wmax = m_flMaxSpeed (AirMove clamps the wish to it).
// Out of reach in one tick: the into part is kept (it is the pin) and the along part brakes with the rest.
// Out: unit dir (dx, dy) + magnitude m, m 0 = no wish
inline void HangWish(float vx, float vy, float nx, float ny, float closing, float k, float wmax,
                     float& dx, float& dy, float& m)
{
    dx = dy = m = 0.f;
    if (!(k > 0.f) || !(wmax > 0.f)) return;
    const float tx = -ny, ty = nx;
    const float along = vx * tx + vy * ty, across = vx * nx + vy * ny;
    const float smax = k * wmax;
    const float dAcross = std::clamp(-closing - across, -smax, smax);
    const float rem = std::sqrt(std::max(smax * smax - dAcross * dAcross, 0.f));
    const float dAlong = std::clamp(-along, -rem, rem);
    const float gx = dAlong * tx + dAcross * nx, gy = dAlong * ty + dAcross * ny;
    const float s = std::sqrt(gx * gx + gy * gy);
    if (s < 1e-6f) return;
    dx = gx / s; dy = gy / s; m = s / k;
}

// air stuck approach: wall face seen by the line column but hull not on it. Lumi's fan only pins
// a hull already flush ( its wishes are 1..120 at 78..90 deg off straight-in = ~0.3 u/s per tick of
// pull ), so a face further than k_fanGap u off the hull edge gets a straight-in full press instead
// ( AirAccelerate cap 30 u/s wish = 4.5 u/s per tick, 15x the pull ). frac * reach = hit distance
// from the box centre, edge = hull extent along that heading
inline constexpr float k_fanGap = 2.f;
inline float WallGap(float frac, float reach, float edge) { return frac * reach - edge; }
inline bool  WallFar(float frac, float reach, float edge) { return WallGap(frac, reach, edge) > k_fanGap; }

// ---- exact-axis freeze: botox csgo air_stuck.cpp n_as_h (11-15) 1:1. Vertical face only (n.z == 0),
// so z never enters d. Engine float order of cmodel.cpp's plane test ----
struct FzPlane { float nx, ny, dist; };      // trace plane: normal out of the wall + dist
struct FzHull  { float ex, ey, ox, oy; };    // Ray_t::Init: ext = (maxs - mins) / 2, off = (mins + maxs) / 2

// d of a hull-trace point at ray centre (px, py): Dot(p, n) - (dist + DotProductAbs(n, ext))
inline float FzEngDp(float px, float py, const FzPlane& P, const FzHull& H)
{
    float d = std::fabs(P.nx * H.ex) + std::fabs(P.ny * H.ey);
    d = P.dist + d;
    return (px * P.nx + py * P.ny) - d;
}
inline float FzEngD(float ox, float oy, const FzPlane& P, const FzHull& H) { return FzEngDp(ox + H.ox, oy + H.oy, P, H); }
// d at the END of a free move: end = origin + v dt, ray END = (origin + off) + (end - origin)
inline float FzEngDAfter(float ox, float oy, const FzPlane& P, const FzHull& H, float vx, float vy, float dt)
{
    const float ex = ox + vx * dt, ey = oy + vy * dt;
    return FzEngDp((ox + H.ox) + (ex - ox), (oy + H.oy) + (ey - oy), P, H);
}
// exact distance (double) and the float grid it is read on (q = ulp of the dot sum)
inline double FzTrueD(float ox, float oy, const FzPlane& P, const FzHull& H)
{
    const double D = (double)P.dist + (double)H.ex * std::fabs((double)P.nx) + (double)H.ey * std::fabs((double)P.ny);
    return ((double)ox + H.ox) * P.nx + ((double)oy + H.oy) * P.ny - D;
}
inline float FzGridQ(float ox, float oy, const FzPlane& P, const FzHull& H)
{
    const float px = ox + H.ox, py = oy + H.oy;
    const float v  = std::fabs(px * P.nx + py * P.ny);
    return std::nextafter(v, FLT_MAX) - v;
}
// AirAccelerate (gamemovement.cpp:1705) after AirMove's maxspeed clamp, xy only. Returns accelspeed
inline float FzAccel(float& vx, float& vy, float wx, float wy, float ws, float airaccel, float dt, float fric,
                     float cap, float maxSpeed)
{
    const float wishspeed = std::min(ws, maxSpeed);
    const float add = std::min(wishspeed, cap) - (vx * wx + vy * wy);
    if (add <= 0.f) return 0.f;
    const float a = std::min(airaccel * wishspeed * dt * fric, add);
    vx += a * wx;
    vy += a * wy;
    return a;
}
// freeze press: straight along n so the END lands at true d = k q / 16. dvn = the normal velocity change
// that does it; reachable when one AirAccelerate step gives exactly a = |dvn| (ws = a / coef within
// maxSpeed, a within addspeed). v0n = START velocity . n. Out: ws, sg (+1 = out along n, -1 = into the wall)
inline bool FzWish(int k, float q, double trueD, float v0n, float dt, float coef, float cap, float maxSpeed,
                   float& ws, float& sg)
{
    const float dvn = (float)((k * (double)q / 16.0 - trueD) / dt) - v0n;
    if (std::fabs(dvn) < 1e-7f || !(coef > 0.f)) return false;
    sg = dvn > 0.f ? 1.f : -1.f;
    const float a = std::fabs(dvn);
    ws = a / coef;
    return ws <= maxSpeed && a <= std::min(ws, cap) - sg * v0n;
}

}   // namespace as
