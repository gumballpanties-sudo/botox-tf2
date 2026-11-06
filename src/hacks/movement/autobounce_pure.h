#pragma once
// Auto bounce pure math: botox air_rollout.h (n_air world/accel/wish) + the analytic halves of
// botox auto_bounce.cpp (stop speed, gain reach, time to contact, clip, keep, side angle).
// No engine types, no traces -- plain floats in/out so tests/autobounce_test.cpp runs headless.
//
// TF2 deltas vs botox (CS:GO), all from source-sdk-2013 tf_gamemovement.cpp:
//   no stamina (CS sv_staminarecoveryrate)        -> max_speed is just m_flMaxspeed
//   air duck does not crop wish (HandleDuckingSpeedCrop is ground only, gamemovement.cpp:4308)
//   air friction stays 1 falling (0.25 is set only rising on a steep plane, tf_gamemovement.cpp:2433)
//   bhop crop = 1.2 * maxspeed (BUNNYJUMP_MAX_SPEED_FACTOR, :1087), no sv_enablebunnyhopping
//   ground snap only at vz <= 250 (CategorizePosition "check for a jump", :2358; CS = 140)
#include <algorithm>
#include <cmath>

namespace ab
{
    constexpr float kAirCap      = 30.f;    // GetAirSpeedCap
    constexpr float kWalkableNz  = 0.7f;
    constexpr float kBhopFactor  = 1.2f;    // BUNNYJUMP_MAX_SPEED_FACTOR
    constexpr float kNonJumpVz   = 250.f;   // TF2 CategorizePosition
    constexpr float kMaxVelocity = 3500.f;  // sv_maxvelocity
    constexpr float kHeadingMin  = 100.f;   // below this xy your heading is noise, the fall line rules
    constexpr float kPi          = 3.14159265358979323846f;

    struct World
    {
        float dt       = 0.015f;
        float g        = 800.f;
        float accel    = 10.f;     // sv_airaccelerate
        float maxSpeed = 300.f;    // m_flMaxspeed (<= 450, cl_forwardspeed)
    };

    inline float AccelSpeed(const World& w) { return w.accel * w.maxSpeed * w.dt; }

    // n_air::exact_wish: the wish whose one AirAccelerate tick adds exactly `need`
    inline float ExactWish(float need, const World& w)
    {
        const float per = w.accel * w.dt;
        return per > 0.0001f ? std::min(need / per, w.maxSpeed) : w.maxSpeed;
    }

    // botox ab_keep_speed: a walkable face grounds you, the landing jump crops to the bhop cap
    inline float KeepSpeed(float result, float nz, float maxSpeed)
    {
        return nz >= kWalkableNz ? std::min(result, maxSpeed * kBhopFactor) : result;
    }

    // botox ab_stop_speed: fastest closing speed that can still brake to 0 inside `dist`.
    // cap = sv_maxvelocity, not botox's 450 (CS speed): a 450 cap brakes every rocket-speed glide down to 450.
    // travel = closed form of botox's tick loop: v0 dt + dt sum_{k=1..m} (v0 - k b), m = floor(v0 / b)
    inline float StopSpeed(float dist, const World& w)
    {
        const float brakeTick = 0.9f * AccelSpeed(w);
        if (brakeTick <= 0.0001f) return 0.f;   // sv_airaccelerate 0: no brake, no steer
        const auto travel = [&](float v0) {
            const float m = std::floor(v0 / brakeTick);
            return w.dt * (v0 + m * v0 - brakeTick * m * (m + 1.f) * 0.5f);
        };
        float lo = 0.f, hi = kMaxVelocity;
        if (travel(hi) <= dist) return hi;
        for (int i = 0; i < 16; ++i) {
            const float mid = (lo + hi) * 0.5f;
            (travel(mid) <= dist ? lo : hi) = mid;
        }
        return lo;
    }

    // botox ab_gain_reach: best run-then-brake distance before contact (upper bound, the flight decides)
    inline float GainReach(float speed, float ttc, const World& w)
    {
        const float accel = AccelSpeed(w);
        const float a     = std::min(accel, kAirCap);
        const float brake = std::max(0.9f * accel / w.dt, 1.f);
        const int ticks   = std::min((int)(ttc / w.dt), 512);
        float v = speed, d = 0.f, best = 0.f;
        for (int k = 0; k <= ticks; ++k) {
            if ((float)k * w.dt + v / brake <= ttc)
                best = std::max(best, d + v * v / (2.f * brake));
            d += v * w.dt;
            v = v < kAirCap ? v + kAirCap : std::sqrt(v * v + a * a);
        }
        return best * 1.2f + 4.f;
    }

    // botox ab_time_to_contact: vz is signed (up +), drop positive = target below
    inline float TimeToContact(float vz, float drop, float g)
    {
        return g > 1.f ? std::max((vz + std::sqrt(std::max(vz * vz + 2.f * g * drop, 0.f))) / g, 0.f) : 0.f;
    }

    // botox ab_clip_xy: xy speed left after ClipVelocity on n, -1 if the move is not into the face
    inline float ClipXY(float vx, float vy, float vz, float nx, float ny, float nz)
    {
        const float into = vx * nx + vy * ny + vz * nz;
        if (into >= 0.f) return -1.f;
        const float ox = vx - nx * into, oy = vy - ny * into;
        return std::sqrt(ox * ox + oy * oy);
    }

    // botox ab_bounce: speed off a face, falling at vzHit (magnitude) with xy (gx, gy)
    inline float Bounce(float gx, float gy, float vzHit, float nx, float ny, float nz, float maxSpeed)
    {
        return KeepSpeed(ClipXY(gx, gy, -vzHit, nx, ny, nz), nz, maxSpeed);
    }

    // botox ab_keep_velocity: strip the into-face (uphill) part of xy, keep the rest
    inline void KeepVelocity(float vx, float vy, float nx, float ny, float& ox, float& oy)
    {
        ox = vx; oy = vy;
        const float flat = std::sqrt(nx * nx + ny * ny);
        if (flat < 0.001f) return;
        const float out = (vx * nx + vy * ny) / flat;
        if (out < 0.f) { ox -= nx / flat * out; oy -= ny / flat * out; }
    }

    // botox ab_side_deg: angle between exit xy and straight downhill
    inline float SideDeg(float ox, float oy, float nx, float ny)
    {
        const float flat = std::sqrt(nx * nx + ny * ny);
        const float spd  = std::sqrt(ox * ox + oy * oy);
        if (flat < 0.01f || spd < 1.f) return 0.f;
        return std::acos(std::clamp((ox * nx + oy * ny) / (flat * spd), -1.f, 1.f)) * 180.f / kPi;
    }

    // fast entry: an exit within maxDeg of your own heading keeps your line, it is not a throw.
    // botox's fall-line-only gate is CS speed, where the exit is mostly the face's push
    inline bool HeadingKept(float ox, float oy, float vx, float vy, float maxDeg)
    {
        return vx * vx + vy * vy >= kHeadingMin * kHeadingMin && SideDeg(ox, oy, vx, vy) <= maxDeg;
    }

    // reach = steer budget, not distance: how far target (dx, dy from origin) sits off the coast segment
    // origin -> origin + v ttc. a fast fall lands far ahead with no input at all
    inline float ReachDist(float dx, float dy, float vx, float vy, float ttc)
    {
        const float ex = vx * ttc, ey = vy * ttc, ee = ex * ex + ey * ey;
        const float t  = ee > 0.0001f ? std::clamp((dx * ex + dy * ey) / ee, 0.f, 1.f) : 0.f;
        const float rx = dx - ex * t, ry = dy - ey * t;
        return std::sqrt(rx * rx + ry * ry);
    }

    // botox ab_glides: drop in glides in on time unless the approach runs uphill
    inline bool Glides(float tx, float ty, float dist, float nx, float ny)
    {
        return dist < 8.f || tx * nx + ty * ny >= 0.f;
    }

    // n_air::fly's per-tick air accel: velocity (vx, vy) steered toward want, one tick
    inline void AirStep(float& vx, float& vy, float wx, float wy, float deadband, const World& w, bool& wrote)
    {
        wrote = false;
        const float dx = wx - vx, dy = wy - vy;
        const float need = std::sqrt(dx * dx + dy * dy);
        if (need <= deadband) return;
        const float ux = dx / need, uy = dy / need;
        const float wish = ExactWish(need, w);
        const float add  = std::min(wish, kAirCap) - (vx * ux + vy * uy);
        if (add > 0.f) {
            const float acc = std::min(w.accel * wish * w.dt, add);
            vx += ux * acc; vy += uy * acc;
        }
        wrote = true;
    }
}
