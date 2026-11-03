#pragma once
// wall climb pure half (botox wall_climb.cpp anon namespace): constants + the wish / yaw math. No engine types:
// Cmd = anything with viewangles.{x,y,z} / forwardmove / sidemove, V = anything with .x .y .z. Used by wallclimb.inl,
// gate-tested by tests/wallclimb_test.cpp
#include <cmath>
#include <vector>

namespace wc
{
    constexpr float k_contact      = 0.05f;
    constexpr float k_pin_reach    = 1.f;
    constexpr float k_wall_nz      = 0.35f;
    constexpr float k_hull_step    = 0.03125f;
    constexpr float k_fine_lo      = 5e-4f; /* ladder rungs in F = the wish that moves the hull 1/32u in one tick */
    constexpr float k_fine_hi      = 0.05f;
    constexpr float k_fine_ratio   = 1.3f;
    constexpr float k_away_lo      = 3e-3f; /* away rungs from here: smaller ones move the hull under 1 float */
    constexpr float k_coarse[]     = { 0.1f, 0.2f, 0.4f, 0.84f, 1.2f, 2.f };
    constexpr float k_along_fwd[]  = { 5.f, 30.f, 0.875f, 15.f };
    constexpr float k_along_off[]  = { 85.999f, 86.999f, 87.999f }; /* 111 antoha offsets, side opposite velocity's tangent */
    constexpr float k_rise_vz      = 260.f; /* CTF CategorizePosition never grounds zvel > 250 */
    constexpr int   k_hold_ticks   = 8;
    constexpr int   k_pin_tries    = 3;
    constexpr int   k_exit_look    = 3;
    constexpr float k_keys_ulp     = 1.3e-4f;
    constexpr float k_park_keep    = 2.f; /* park ends this much slower in xy than your keys = your keys ( lab P42 ) */
    constexpr float k_land_above   = 2.f;
    constexpr float k_low_ledge    = 10.f; /* a land this far under your jump floor = a ledge you fall past, not a climb */
    constexpr int   k_reserve_sims = 24;
    constexpr int   k_commit_sims  = 3;
    constexpr float k_time_share   = 0.6f;
    constexpr float k_slow_xy      = 200.f;
    constexpr float k_slow_ratio   = 0.5f;
    constexpr float k_pin_sign_xy  = 5.f;
    constexpr float k_jump_share   = 0.8f; /* jump must reach 80% of ideal vz */
    constexpr float k_jump_impulse = 289.f;
    constexpr int   k_back_steps   = 16;
    constexpr float k_seam_off     = 0.03125f;
    constexpr float k_seam_below   = 64.f;
    constexpr float k_seam_floor   = 2.f;
    constexpr float k_leave_cos    = 0.5f;
    constexpr int   k_robust_parks = 3; /* lab P62 */
    constexpr float k_robust_vel   = 0.002f;
    constexpr float k_snag_along   = 2.f;   /* lab P69: along wall speed a seam snag is judged on */
    constexpr float k_snag_keep    = 0.25f; /* a sim that ends under this share of it snagged */
    constexpr float k_spot_step    = 2.f;
    constexpr float k_spot_rise    = 10.f;
    constexpr int   k_spot_ticks   = 64;
    constexpr int   k_spot_tick_sims = 20;
    constexpr float k_spot_snap    = 0.25f;
    constexpr float k_spot_look    = 4.f; /* ticks of along speed counted when walking to the spot */
    constexpr float k_spot_along[] = { 0.f, 10.f, -10.f, 25.f, -25.f, 60.f, -60.f, 150.f, -150.f, 250.f, -250.f };
    constexpr int   k_viz_ticks    = 16;

    inline float Rad(const float deg) { return deg * 0.017453292519943295f; }

    enum e_count {
        c_land, c_flip, c_pin_catch, c_hold, c_park, c_keys, c_along, c_own, c_leave, c_aa_leave, c_exit, c_exit_none, c_far, c_rise,
        c_scan_cut, c_cut, c_yield, c_ramp, c_skip, c_jump_back, c_jump_strip, c_as_take, c_pin_miss, c_no_park,
        c_park_keep, c_away, c_robust, c_spot, c_spot_sims, c_hold_land, c_low, c_leave_hang, c_seam, c_seam_park, c_pin_flip,
        c_tb_drop, c_div, c_spot_path, c_spot_floor, c_ticks, c_sims, c_max
    };

    inline const std::vector<float>& FineRungs()
    {
        static const std::vector<float> v = [] {
            std::vector<float> r{ 0.f };
            for (float m = k_fine_lo; m <= k_fine_hi; m *= k_fine_ratio)
                r.push_back(m);
            return r;
        }();
        return v;
    }

    template <class Cmd, class V>
    inline bool WishLeavesWall(const Cmd& c, const V& n)
    {
        const float p = Rad(c.viewangles.x), y = Rad(c.viewangles.y), r = Rad(c.viewangles.z);
        const float sp = sinf(p), cp = cosf(p), sy = sinf(y), cy = cosf(y), sr = sinf(r), cr = cosf(r);
        float fx = cp * cy, fy = cp * sy, rx = -sr * sp * cy + cr * sy, ry = -sr * sp * sy - cr * cy;
        if (const float l = sqrtf(fx * fx + fy * fy); l > 0.f)
            fx /= l, fy /= l;
        if (const float l = sqrtf(rx * rx + ry * ry); l > 0.f)
            rx /= l, ry /= l;
        const float wx = fx * c.forwardmove + rx * c.sidemove;
        const float wy = fy * c.forwardmove + ry * c.sidemove;
        const float l  = sqrtf(wx * wx + wy * wy);
        return l > 0.f && (wx * n.x + wy * n.y) > k_leave_cos * l;
    }

    template <class V>
    inline float FaceD(const V& o, const V& n) { return o.x * n.x + o.y * n.y + o.z * n.z; }

    inline void WorldWish(const float yaw, const float f, const float s, float& x, float& y)
    {
        const float a = Rad(yaw);
        x = f * cosf(a) + s * sinf(a);
        y = f * sinf(a) - s * cosf(a);
    }

    // botox n_tb::move_fix_to_yaw: fwd/side meant at world yaw, rotated onto the cmd's view yaw. never strips buttons
    template <class Cmd>
    inline void MoveFixToYaw(const float yaw, Cmd& c)
    {
        const float y = yaw >= 0.f ? yaw : yaw + 360.f;
        const float v = c.viewangles.y >= 0.f ? c.viewangles.y : c.viewangles.y + 360.f;
        float d       = y <= v ? 360.f - fabsf(y - v) : fabsf(v - y);
        d             = 360.f - d;
        const float f = c.forwardmove, s = c.sidemove;
        c.forwardmove = cosf(Rad(d)) * f + cosf(Rad(d + 90.f)) * s;
        c.sidemove    = sinf(Rad(d)) * f + sinf(Rad(d + 90.f)) * s;
    }

    inline float AtIdx(const int i) { return (i & 1 ? 1.f : -1.f) * k_spot_step * static_cast<float>((i + 1) / 2); }
}

