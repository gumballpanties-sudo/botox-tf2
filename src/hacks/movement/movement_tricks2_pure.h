#pragma once
// the pure-math decisions out of movement_tricks2.inl, pulled out so they're
// testable without the engine (see tests/movementtricks2_test.cpp). No engine types, no
// includes -- plain in/out, same idiom as movement_ex_pure.h.

// AutoBounce's tangent pick: given the wall normal and the player's own horizontal direction
// (both unit 2D vectors), decide whether the perpendicular tangent (-ny, nx) has to be flipped
// so it points the same general way the player is already heading (never reverses their turn).
// Matches movement_tricks2.inl's `if (tangent.Dot(dir) < 0.f) tangent = tangent * -1.f;`
inline bool BounceTangentShouldFlip(float nx, float ny, float dirx, float diry)
{
    const float tx = -ny, ty = nx;
    return (tx * dirx + ty * diry) < 0.f;
}

// Tung surf (tungsurf.inl) plane rank, botox tung_grid_rank: map brushes sit on the 16u grid,
// so a snap plane on 16 is tried first, odd planes last. 0 best .. 4 worst
inline int TungGridRank(float p)
{
    const int i = static_cast<int>(p);
    return i % 16 == 0 ? 0 : i % 8 == 0 ? 1 : i % 4 == 0 ? 2 : i % 2 == 0 ? 3 : 4;
}
