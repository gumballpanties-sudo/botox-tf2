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

// Tung surf's widened ramp band: never stricter than TB's own band, clamped to a sane ceiling.
// Matches movement_tricks2.inl / texturebug.inl's hook:
// `bTungOn ? std::clamp(g_tungMaxNz, flTbBand, 0.5f) : flTbBand`
inline float TungRampBand(bool tungOn, float wantNz, float tbBand)
{
    if (!tungOn) return tbBand;
    float band = wantNz;
    if (band < tbBand) band = tbBand;
    if (band > 0.5f)   band = 0.5f;
    return band;
}
