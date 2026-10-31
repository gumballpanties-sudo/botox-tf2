#pragma once
// TF2 build placement, ported from the SDK so a practice building lands where the game would
// actually let an engineer put one: on the ground, not in a wall, not on top of another one.
// Engine-free, so tests/buildplace_test.cpp runs it against a box world -- templated on the
// vector type (needs x/y/z floats) and on the two traces, the same shape fm_slide.h uses.
//
// One to one with source-sdk-2013:
//   CBaseObject::CalculatePlacementPos   game/shared/tf/baseobject_shared.cpp:509
//   CBaseObject::VerifyCorner            game/shared/tf/baseobject_shared.cpp:639
//   CBaseObject::IsPlacementPosValid     game/shared/tf/baseobject_shared.cpp:674
//   CBaseObject::EstimateValidBuildPos   game/server/tf/tf_obj.cpp:684
//
// ONE DELIBERATE DIFFERENCE. The game anchors the whole search to the BUILDER: the box goes at
// WorldSpaceCenter + forward * (objectRadius + playerRadius + 4), and the z window is "the top of
// the box cannot go below your feet, the bottom cannot go above your head". A practice bind aims
// with the crosshair instead, so the anchor here is the aimed point and `anchorHalfZ` is the
// window that replaces the player's half height. Every rule that decides yes/no is unchanged.
//
// NOT ported, because the entities are server-only and never reach a client: func_nobuild,
// trigger_hurt, func_tracktrain. Respawn rooms are done by the caller against what the client
// does see (PrScanEnts in practice.inl).
#include <cmath>

enum BpResult
{
    BP_OK = 0,
    BP_NO_GROUND,   // nothing under the spot at all
    BP_NO_ROOM,     // the box does not fit in the gap
    BP_SLOPE,       // steeper than 45 degrees, or a corner hanging over an edge
    BP_BLOCKED,     // something solid is already in the box (player, building, prop)
    BP_NO_LOS,      // the spot is not visible from where you stand
    BP_OBJECT,      // another building is in the spot
    BP_RESPAWN,     // a respawn room
};

inline const char* BpWhy(BpResult r)
{
    switch (r)
    {
    case BP_OK:        return "ok";
    case BP_NO_GROUND: return "no ground under it";
    case BP_NO_ROOM:   return "no room for it there";
    case BP_SLOPE:     return "too steep / over an edge";
    case BP_BLOCKED:   return "something is in the way";
    case BP_NO_LOS:    return "cannot see the spot from here";
    case BP_OBJECT:    return "a building is already there";
    case BP_RESPAWN:   return "respawn room";
    }
    return "?";
}

template<class V>
struct BpHit
{
    float frac = 1.f;
    V     end{};
    V     n{};
    bool  startSolid = false;
};

inline constexpr float k_bpGroundClearance = 32.f;   // TF_OBJ_GROUND_CLEARANCE, baseobject_shared.h:34
inline constexpr float k_bpSlopeDot        = 0.65f;  // baseobject_shared.cpp:661, "> 45 degrees"
inline constexpr int   k_bpIterations      = 8;      // baseobject_shared.cpp:567
inline constexpr float k_bpObjBoxSize      = 24.f;   // baseobject_shared.cpp:713 flBoxSize
inline constexpr float k_bpObjBoxDepth     = 32.f;   // baseobject_shared.cpp:714 flBoxDepth

// ---- point / box maths -------------------------------------------------------------------
template<class V>
inline bool BpPointIn(const V& p, const V& mn, const V& mx)
{
    return p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y && p.z >= mn.z && p.z <= mx.z;
}

template<class V>
inline bool BpBoxesOverlap(const V& amn, const V& amx, const V& bmn, const V& bmx)
{
    return amn.x <= bmx.x && amx.x >= bmn.x
        && amn.y <= bmx.y && amx.y >= bmn.y
        && amn.z <= bmx.z && amx.z >= bmn.z;
}

// Segment vs AABB, slab method. This is PointsCrossRespawnRoomVisualizer's question: does the
// line from you to the spot pass through this brush.
template<class V>
inline bool BpSegHitsBox(const V& a, const V& b, const V& mn, const V& mx)
{
    const float s[3] = { a.x, a.y, a.z };
    const float d[3] = { b.x - a.x, b.y - a.y, b.z - a.z };
    const float lo[3] = { mn.x, mn.y, mn.z };
    const float hi[3] = { mx.x, mx.y, mx.z };
    float tEnter = 0.f, tExit = 1.f;
    for (int i = 0; i < 3; ++i)
    {
        if (std::fabs(d[i]) < 1e-6f)
        {
            if (s[i] < lo[i] || s[i] > hi[i]) return false;   // parallel and outside the slab
            continue;
        }
        float t0 = (lo[i] - s[i]) / d[i], t1 = (hi[i] - s[i]) / d[i];
        if (t0 > t1) { const float t = t0; t0 = t1; t1 = t; }
        if (t0 > tEnter) tEnter = t0;
        if (t1 < tExit)  tExit  = t1;
        if (tEnter > tExit) return false;
    }
    return true;
}

// The box IsPlacementPosValid asks UTIL_EntitiesInBox for: 24u out in x/y, 32u down, 24u up.
template<class V>
inline void BpObjectTestBox(const V& origin, V& mn, V& mx)
{
    mn = origin; mx = origin;
    mn.x -= k_bpObjBoxSize; mn.y -= k_bpObjBoxSize; mn.z -= k_bpObjBoxDepth;
    mx.x += k_bpObjBoxSize; mx.y += k_bpObjBoxSize; mx.z += k_bpObjBoxSize;
}

// ---- CBaseObject::VerifyCorner ------------------------------------------------------------
// A corner of the base has to have ground within 32u of it, and that ground cannot be steeper
// than 45 degrees. This is what stops a building half over a ledge or nosed into a ramp.
template<class V, class LineFn>
inline bool BpVerifyCorner(LineFn&& line, const V& bottomCenter, float xOffset, float yOffset)
{
    V start = bottomCenter;
    start.x += xOffset; start.y += yOffset; start.z += 2.f;   // the SDK's own 2.0 epsilon
    V end = start; end.z -= k_bpGroundClearance;

    const BpHit<V> tr = line(start, end);
    if (tr.frac < 1.f)
    {
        const float len = std::sqrt(tr.n.x * tr.n.x + tr.n.y * tr.n.y + tr.n.z * tr.n.z);
        if ((len > 1e-6f ? tr.n.z / len : 0.f) < k_bpSlopeDot) return false;
    }
    return !tr.startSolid && tr.frac < 1.f;
}

// ---- CBaseObject::CalculatePlacementPos ---------------------------------------------------
// Sweep the footprint (a flat box, no height) down through the window and take the first floor
// that leaves the box room to stand. `hull` traces brushes only (MASK_PLAYERSOLID_BRUSHONLY),
// `line` the same -- players and props are not what a building stands on.
template<class V, class HullFn, class LineFn>
inline BpResult BpCalcPlacementPos(HullFn&& hull, LineFn&& line, const V& aim, float anchorHalfZ,
                                   const V& bMins, const V& bMaxs, V& outOrigin, V& outCenter)
{
    V dims;  dims.x = bMaxs.x - bMins.x; dims.y = bMaxs.y - bMins.y; dims.z = bMaxs.z - bMins.z;
    V half;  half.x = dims.x * 0.5f;     half.y = dims.y * 0.5f;     half.z = dims.z * 0.5f;
    V hiXY;  hiXY.x = half.x;  hiXY.y = half.y;  hiXY.z = 0.f;
    V loXY;  loXY.x = -half.x; loXY.y = -half.y; loXY.z = 0.f;

    const float flBoxTopZ    = aim.z + anchorHalfZ + dims.z;
    const float flBoxBottomZ = aim.z - anchorHalfZ - dims.z;
    const float topZInc = (flBoxBottomZ - flBoxTopZ) / float(k_bpIterations - 1);

    float topZ = flBoxTopZ, bottomZ = 0.f;
    int i = 0;
    for (; i < k_bpIterations; ++i)
    {
        V a; a.x = aim.x; a.y = aim.y; a.z = topZ;
        V b; b.x = aim.x; b.y = aim.y; b.z = flBoxBottomZ;
        const BpHit<V> tr = hull(a, b, loXY, hiXY);
        bottomZ = tr.end.z;

        if (tr.frac >= 1.f) return BP_NO_GROUND;                  // nothing to stand on at all
        if (topZ - bottomZ > dims.z && !tr.startSolid) break;     // the box fits here
        topZ += topZInc;
    }
    if (i == k_bpIterations || topZ - bottomZ < dims.z) return BP_NO_ROOM;

    V bottomCenter; bottomCenter.x = aim.x; bottomCenter.y = aim.y; bottomCenter.z = bottomZ;
    if (!BpVerifyCorner(line, bottomCenter, -half.x, -half.y) ||
        !BpVerifyCorner(line, bottomCenter, +half.x, +half.y) ||
        !BpVerifyCorner(line, bottomCenter, +half.x, -half.y) ||
        !BpVerifyCorner(line, bottomCenter, -half.x, +half.y))
        return BP_SLOPE;

    // vBottomLeft - m_vecBuildMins: for a box that is symmetric in x/y with mins.z 0 (every TF2
    // building is) this is the aimed x/y standing on the floor the sweep found.
    outOrigin.x = aim.x - half.x - bMins.x;
    outOrigin.y = aim.y - half.y - bMins.y;
    outOrigin.z = bottomZ - bMins.z;
    outCenter = outOrigin; outCenter.z += half.z;
    return BP_OK;
}
