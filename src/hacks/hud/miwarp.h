#pragma once
#include <math.h>

// The mitosis warp: what one half of a dividing pair of tags does to its own glyphs, along the
// split axis only (the layout picks the axis, this does not care which one it is).
//
// A dividing cell elongates along the division axis, necks down across it, then the two halves
// round off and spring apart. Applied to two text labels that are already being pushed apart by
// the slot animation, that is:
//   * stretch ALONG the axis about a pivot on the OUTER edge, so every pixel of the stretch is
//     reach ACROSS the join -- the two labels warp into each other -- and none of it runs off
//     the far end of the row
//   * thin ACROSS the axis by 1/sa, which is what makes the reaching pair read as a pinching
//     membrane instead of two labels getting fat. sa * sp == 1, so the area is preserved the way
//     a dividing cell's is
//   * sin(pi*t), so the warp is EXACTLY zero at both ends and peaks at the half-way pinch. The
//     zero at t=0 is the point: anything with a non-zero value on the first frame pops in on top
//     of the text (which is what the old metaball membrane did, and why it read as a blob
//     spawning rather than a cell dividing)
// t runs 0 (one cell) -> 1 (two settled cells) and is the younger tag's raw fade, so a tag on the
// way out runs the identical curve backwards and merges back in.
// Gate test: tests/miwarp_test.cpp.
namespace miw
{
// Peak elongation. Used twice: on the text when the migoo.h picture cannot draw, and INSIDE the
// picture (mig::Render) on top of the liquid split. 0.04: 0.12, then 0.07, read as too much in game.
constexpr float kStretch = 0.04f;
constexpr float kPi      = 3.14159265f;

struct Warp
{
    float piv;   // pivot along the axis: the outer edge, the one point that does not move
    float sa;    // scale along the split axis  (>= 1)
    float sp;    // scale across it             (= 1 / sa)
};

// c = this tag's centre along the axis, e = its half extent along it, p = the partner's centre,
// t = split progress. p == c (the first frame, before the slot has opened) leans toward +,
// which is harmless: sa is 1 there.
inline Warp SplitWarp(float c, float e, float p, float t)
{
    const float ct = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    const float sa = 1.f + kStretch * sinf(kPi * ct);
    const float dir = (p - c) >= 0.f ? 1.f : -1.f;
    return { c - dir * e, sa, 1.f / sa };
}
}
