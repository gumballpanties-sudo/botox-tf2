#pragma once
// wall climb pure half: constants + the hop state machine. No engine types, gate-tested by tests/wallclimb_test.cpp.
//
// A climb is ground -> hop -> one-tick sweep that grounds the hull flush on a displacement seam -> hop again.
// The sweep lives in wallclimb.inl (engine sims). What can be tested cold is here: the armed hop (an air win marks
// the ground tick it bought, the launch fires on the REAL grounded tick, a refused press is released for one tick
// so m_nOldButtons clears) and the prevent-slow test.
#include <cmath>

namespace wc
{
    constexpr float k_scan_nz        = 0.65f; /* disp walls are bumpy: facets read up to ~0.7 below standable */
    constexpr float k_scan_reach     = 16.f;  /* detect out to here (2u threw away 43% of the air ticks) */
    constexpr float k_engage_support = 8.f;   /* hull face -> disp face, live, before any sim: commit range */
    constexpr float k_land_support   = 2.f;   /* post-sim: grounded ON the face, not near it */
    constexpr float k_min_input      = 0.01f; /* hands off the keys = hands off the cmd */
    constexpr float k_rise_vz        = 250.f; /* CTFGameMovement::CategorizePosition never grounds vz > 250 (tf_gamemovement.cpp:2358) */
    constexpr int   k_jump_pending   = 4;     /* ticks an air win stays armed waiting for its ground tick */
    constexpr int   k_engage_ticks   = 8;     /* "a face recently": the jump-bit unlatch needs a wall */
    constexpr float k_time_share     = 0.6f;  /* of one tick, wall clock, on top of the shared sim pool */
    constexpr float k_slow_xy        = 200.f; /* prevent slow: no win that halves xy above this */
    constexpr float k_slow_ratio     = 0.5f;
    constexpr int   k_viz_ticks      = 16;
    /* the three fwd values one simulated tick can tell apart: 450 spends the whole air addspeed cap, 30 is the cap, 5 is a
       crawl. The duck is the axis that really moves the hull (airborne Duck() finishes the same tick = 20u origin lift) */
    constexpr float k_forward[]      = { 450.f, 30.f, 5.f };
    constexpr float k_offset_step    = 5.f;
    constexpr float k_offset_max     = 45.f;  /* exclusive: 0, 5 .. 40 = 9 yaws */

    // disp-face scan past brush faces. MV_DetectWallLines walks 16 directions from its start and returns the first
    // face plus that direction's angle UNWRAPPED (start .. start+2pi). The next call starts one direction later; the walk
    // stops once a full turn from the first start is covered, so it costs at most the 16 directions one scan has
    constexpr float k_two_pi    = 6.28318530718f;
    constexpr float k_scan_step = k_two_pi / 16.f;
    inline float NextScan(const float found_angle) { return found_angle + k_scan_step; }
    inline bool  ScanLeft(const float start, const float a) { return a < start + k_two_pi - 1e-3f; }
    inline float WrapScan(const float a) { return std::fmod(a, k_two_pi); }

    inline bool Slowed(const float old_xy, const float new_xy)
    {
        return old_xy > k_slow_xy && new_xy / old_xy < k_slow_ratio;
    }

    // yaw sweep sign: skew the into-wall wish against the side the velocity comes from
    inline float SweepSign(const float relative_yaw) { return relative_yaw >= 0.f ? -1.f : 1.f; }

    enum e_hop { hop_none, hop_release, hop_press };

    struct Hop
    {
        int  pending   = 0;     /* ticks left on an armed hop (air win -> ground press) */
        bool jumpedLast = false; /* pressed last tick and STILL grounded = engine refused */

        void Arm() { pending = k_jump_pending; }
        void Reset() { pending = 0; jumpedLast = false; }

        // every tick, before any yield: returns true when the armed hop's ground tick arrived
        bool Clock(const bool ground_pre)
        {
            if (pending <= 0)
                return false;
            --pending;
            if (!ground_pre)
                return false;
            pending = 0;
            return true;
        }

        // grounded tick: what to do with IN_JUMP. raw_jump = the real space bar (wc never jumps on its own),
        // prev_jump = last cmd went out with IN_JUMP (m_nOldButtons has it = a press now is refused as a pogo)
        e_hop Launch(const bool armed, const bool raw_jump, const bool prev_jump)
        {
            if (!armed || !raw_jump) { jumpedLast = false; return hop_none; }
            if (jumpedLast || prev_jump) { jumpedLast = false; return hop_release; }
            jumpedLast = true;
            return hop_press;
        }
    };

    // airborne: strip a held IN_JUMP so the landing tick's press is not refused. Only beside a face wc has engaged, never
    // on the tick the ground is left / regained (ej / lj / mj own those), never while another feature wants the bit
    inline bool Unlatch(const bool ground_pre, const bool ground_post, const bool raw_jump, const bool engaged, const bool other_wants_jump)
    {
        const bool left = ground_pre && !ground_post, landing = !ground_pre && ground_post;
        return !ground_pre && !left && !landing && raw_jump && engaged && !other_wants_jump;
    }
}
