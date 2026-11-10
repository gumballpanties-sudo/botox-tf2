#pragma once
// the pure-math decisions out of movement_tricks2.inl, pulled out so they're
// testable without the engine (see tests/movementtricks2_test.cpp). No engine types, no
// includes -- plain in/out, same idiom as movement_ex_pure.h.

// Tung surf (tungsurf.inl) plane rank, botox tung_grid_rank: map brushes sit on the 16u grid,
// so a snap plane on 16 is tried first, odd planes last. 0 best .. 4 worst
inline int TungGridRank(float p)
{
    const int i = static_cast<int>(p);
    return i % 16 == 0 ? 0 : i % 8 == 0 ? 1 : i % 4 == 0 ? 2 : i % 2 == 0 ? 3 : 4;
}
