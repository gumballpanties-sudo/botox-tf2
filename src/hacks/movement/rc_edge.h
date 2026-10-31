#pragma once
// ---- WHERE THE FLOOR UNDER AN EDGEBUG MARKER ENDS ------------------------------------------
// A ground marker is the height you LAND at, so the height under the crosshair is the answer.
// An edgebug is the height you SLIDE OFF at, and that is the lip, never the spot you managed to
// click: on a slanted brush every unit short of the edge is a unit of z the whole bug is decided
// by, and clicking a pixel-wide lip through a crosshair at range is not a thing anybody does.
// So an edgebug marker keeps the xy you placed and takes the z of the NEAREST edge.
//
// The walk: a fan of rays out from the marker, one floor sample every k_step, until the floor
// stops being one -- void under the sample, or a drop taller than a step (the engine's 18u).
// A rise taller than a step is a wall, not an edge, and ends that ray with nothing. The nearest
// boundary of all the rays wins and is then bisected, because 2u of slanted brush is up to 2u of
// z and that is the number this whole thing exists to get right.
//
// Pure, so tests/rcedge_test.cpp can fly flat / slanted / stepped / walled floors without the
// engine. The caller traces; this walks.
#include <cmath>

namespace rce
{
inline constexpr int   k_rays   = 16;   // directions swept out from the marker (22.5 deg apart)
inline constexpr float k_step   = 2.f;  // floor samples along a ray
inline constexpr float k_max    = 64.f; // further out than this is not "the edge this marker is on"
inline constexpr float k_drop   = 18.f; // engine step height: a smaller change is walkable, not an edge
inline constexpr int   k_bisect = 6;    // 2u / 2^6 = 0.03u on the lip

struct Edge { float x = 0.f, y = 0.f, z = 0.f, dist = 0.f; };

// what a sample found. BLOCKED is its own answer and not VOID: the sampler's ray starts a step
// over the last floor, so a wall standing on that floor swallows the ray and comes back solid --
// read as void, every wall along a room's floor would be a lip you slide off, which is the one
// shape an edgebug can never happen on. A blocked sample ends its ray with nothing, like a rise
enum { kVoid = 0, kFloor = 1, kBlocked = -1 };

// floor(x, y, zRef, z) -> kFloor and the height in z when standable ground sits under (x,y) near
// zRef, kVoid when nothing does, kBlocked when something solid is in the way of asking. zRef
// walks with the surface, so a long ramp is followed rather than cut off a step down it.
template <class F>
inline bool NearestEdge(float x0, float y0, float zRef, F floor, Edge& out)
{
    float z0 = 0.f;
    if (floor(x0, y0, zRef, z0) != kFloor) return false;   // the marker is not over a floor at all

    bool found = false;
    float best = k_max + 1.f;
    for (int i = 0; i < k_rays; i++)
    {
        const float a = 6.28318530718f * float(i) / float(k_rays);
        const float cx = std::cos(a), cy = std::sin(a);
        float zPrev = z0;
        for (float d = k_step; d <= k_max + 1e-3f; d += k_step)
        {
            float z = 0.f;
            const int s = floor(x0 + cx * d, y0 + cy * d, zPrev, z);
            if (s == kBlocked) break;                                     // a wall: no edge this way
            if (s == kFloor && z - zPrev > k_drop) break;                 // a step up is a wall too
            if (s == kFloor && zPrev - z <= k_drop) { zPrev = z; continue; }   // still the same floor

            // void, or a drop taller than a step: the lip is between the last two samples
            float lo = d - k_step, hi = d, zLip = zPrev;
            for (int k = 0; k < k_bisect; k++)
            {
                const float mid = (lo + hi) * 0.5f;
                float zm = 0.f;
                if (floor(x0 + cx * mid, y0 + cy * mid, zLip, zm) == kFloor && zLip - zm <= k_drop)
                { lo = mid; zLip = zm; }
                else hi = mid;
            }
            if (lo < best) { best = lo; out = { x0 + cx * lo, y0 + cy * lo, zLip, lo }; found = true; }
            break;
        }
    }
    return found;
}
}
