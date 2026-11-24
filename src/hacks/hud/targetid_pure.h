#pragma once
// csgo target id, the pure half: which entity the crosshair is "on" and when. Drives the
// panorama crosshair's enemy colour (.crosshair--enemy, #c93121) -- csgo_hudreticle.cpp:1026
// takes it from GetIDTarget(), so the red obeys the same timers the name text does.
//   C_CSPlayer::UpdateIDTarget  c_cs_player.cpp:6537  (the traces live in aim/visuals/hud_aim.cpp)
//   C_CSPlayer::GetIDTarget     c_cs_player.cpp:4495
//   CountdownTimer              util_shared.h:745
//   mp_playerid_delay 0.4 / mp_playerid_hold 0.2  cs_gamerules.cpp:2119
// No SDK types: shared by the aim TU and tests/targetid_test.cpp.
#include <cmath>

namespace tid
{
    constexpr float kDelay = 0.4f;   // mp_playerid_delay: hover this long before it counts
    constexpr float kHold  = 0.2f;   // mp_playerid_hold: keep the old target this long after

    // CountdownTimer: timestamp is the END time; never started = -1.
    struct Countdown
    {
        float ts = -1.f, dur = 0.f;
        void  Start(float now, float d) { ts = now + d; dur = d; }
        bool  Elapsed(float now) const  { return now > ts; }
        float Ratio(float now) const
        {
            if (ts <= 0.f || dur <= 0.f) return 0.f;
            const float left = (ts - now) / dur;
            return left < 0.f ? 0.f : (left > 1.f ? 1.f : left);
        }
    };

    struct TargetId
    {
        int idx = 0, old = 0;
        Countdown delay, hold;
        float last = -1.f;

        int Get(float now) const
        {
            if (!delay.Elapsed(now)) return 0;
            if (idx) return idx;
            if (old && !hold.Elapsed(now)) return old;
            return 0;
        }

        // Once a frame. hit = entity index the traces settled on, 0 = nothing.
        void Update(float now, int hit)
        {
            // csgo Resets both timers on spawn (c_cs_player.cpp:3084). curtime running BACKWARDS
            // (map change, demo seek) is our version of that: an old end time would hold the
            // delay shut until the clock caught up with it.
            if (now < last) *this = TargetId{};
            last = now;

            idx = 0;
            if (!hit) return;
            // order matters: Get() runs after idx is cleared, so only the hold can answer
            if (!Get(now) && (!old || (delay.Ratio(now) == 0.f && hold.Ratio(now) == 0.f)))
                delay.Start(now, kDelay);
            idx = hit;
            old = hit;
            hold.Start(now, kHold);
        }
    };

    // util_shared.h:399 DistanceToRay -- negative when pos is off either end of the segment.
    inline float DistanceToRay(const float pos[3], const float a[3], const float b[3])
    {
        float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
        const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len > 0.f) { d[0] /= len; d[1] /= len; d[2] /= len; }
        const float to[3] = { pos[0] - a[0], pos[1] - a[1], pos[2] - a[2] };
        const float along = d[0] * to[0] + d[1] * to[1] + d[2] * to[2];
        auto dist = [](const float* p, const float* q) {
            const float x = p[0] - q[0], y = p[1] - q[1], z = p[2] - q[2];
            return std::sqrt(x * x + y * y + z * z);
        };
        if (along < 0.f)  return -dist(pos, a);
        if (along > len)  return -dist(pos, b);
        const float on[3] = { a[0] + along * d[0], a[1] + along * d[1], a[2] + along * d[2] };
        return dist(pos, on);
    }
}
