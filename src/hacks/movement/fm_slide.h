#pragma once
// Fake movement anti-snag: engine-free, so tests/fmslide_test.cpp runs it against a box world.
// Templated on the vector type (needs x/y/z floats) and on the hull trace, so fakemove.inl
// plugs in Vec3 + the engine trace and the test plugs in its own.
//
// Why: surf rides move with air physics. Air TryPlayerMove has no step-up, and at a crease
// (two planes) it zeroes velocity -- so a 1-18u lip, a brush seam or an outer corner kills
// the ride dead. Fix: when the engine got us < 90% of the way, redo the tick's horizontal move
// ourselves -- plain slide, and slide from sv_stepsize up then back down -- keep the furthest.
#include <cmath>

template<class V>
struct FmHit
{
    float frac = 1.f;
    V     end{};
    V     n{};
    bool  startSolid = false;
};

// Horizontal move with slide: each hit clips the rest of the move and the velocity against
// the plane's horizontal normal, like ClipVelocity with overbounce 1, but one plane at a time
// so a crease slides along the second face instead of zeroing. Never turns back on `move`.
// Returns false if the start is in solid (pos untouched).
template<class V, class TraceFn>
inline bool FmSlide(TraceFn&& trace, V& pos, V move, V& vel)
{
    const float wx = move.x, wy = move.y;
    for (int i = 0; i < 4; ++i)
    {
        if (move.x * move.x + move.y * move.y < 1e-4f) break;
        V end = pos; end.x += move.x; end.y += move.y;
        const FmHit<V> h = trace(pos, end);
        if (h.startSolid) return i > 0;
        pos = h.end;
        if (h.frac >= 1.f) break;

        float nx = h.n.x, ny = h.n.y;
        const float nl = std::sqrt(nx * nx + ny * ny);
        if (nl < 0.01f) break;               // flat floor/ceiling: nothing to slide along
        nx /= nl; ny /= nl;

        float rx = move.x * (1.f - h.frac), ry = move.y * (1.f - h.frac);
        const float d = rx * nx + ry * ny;
        if (d < 0.f) { rx -= nx * d; ry -= ny * d; }
        const float dv = vel.x * nx + vel.y * ny;
        if (dv < 0.f) { vel.x -= nx * dv; vel.y -= ny * dv; }
        if (rx * wx + ry * wy <= 0.f) break; // clipped into reverse: stop, never back up
        move.x = rx; move.y = ry;
    }
    return true;
}

// pre = origin + velocity the engine started the tick with, post = where the engine ended.
// On a snag, writes the better origin + horizontal velocity and returns true.
template<class V, class TraceFn>
inline bool FmUnsnag(TraceFn&& trace, const V& pre, const V& preVel, float dt, float stepH,
                     const V& post, V& outOrg, V& outVel)
{
    const float wx = preVel.x * dt, wy = preVel.y * dt;
    const float wl = std::sqrt(wx * wx + wy * wy);
    if (wl < 0.25f) return false;
    auto along = [&](const V& p) { return ((p.x - pre.x) * wx + (p.y - pre.y) * wy) / wl; };

    const float got = along(post);
    if (got >= wl * 0.9f) return false;      // engine moved us fine

    V move{}; move.x = wx; move.y = wy;
    float best = got + 0.25f;                // must beat the engine clearly, else leave it
    bool  found = false;

    // plain slide
    {
        V p = pre, v = preVel;
        if (FmSlide(trace, p, move, v) && along(p) > best)
        { best = along(p); outOrg = p; outVel = v; found = true; }
    }

    // step up, slide, step back down (lands on the lip, or back at pre height past it)
    if (stepH > 0.f)
    {
        V up = pre; up.z += stepH;
        const FmHit<V> u = trace(pre, up);
        if (!u.startSolid && u.end.z > pre.z + 0.5f)
        {
            V p = u.end, v = preVel;
            if (FmSlide(trace, p, move, v))
            {
                V down = p; down.z = pre.z;
                const FmHit<V> dn = trace(p, down);
                if (!dn.startSolid && along(dn.end) > best + 0.25f)   // flat slide wins ties
                { best = along(dn.end); outOrg = dn.end; outVel = v; found = true; }
            }
        }
    }
    return found;
}
