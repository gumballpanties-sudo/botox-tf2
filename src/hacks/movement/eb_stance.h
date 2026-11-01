#pragma once
// EDGE BUG, the pure half (no engine types). edgebug.inl calls these, tests/ebstance_test.cpp
// holds their truth tables, so the test covers the code that really runs.
// Port of botox hacks/movement/edgebug.cpp (detection bands, duck sweep window, plan pick)
// plus the TF2 stance rule (a ducked bug that lands still ducked eats the next jump).
#include <cmath>
#include <cstddef>

// ================= detection bands (botox edgebug.cpp:29-124) ================================
// vz0 = vel.z BEFORE the sim tick, vz = after, g = half gravity per tick, s = tick scale
// (64 * interval, 1 at 64 tick, 0.96 at TF2's 66.67). Per-tick tolerances scale by s, fall
// speeds never do.
struct MvEbGravBand  { float mul, off, fall, tol; };  // vz0 < -(g*mul + off*s + fall) && |vz + g| < tol*s
struct MvEbAbsBand   { float fall, tol; };            // vz0 < -fall && |vz| < tol*s
struct MvEbChgBand   { float change, fall, tol; };    // abs band + |vz - vz0| > change*s
struct MvEbRatioBand { float ratio, fall; };          // |vz/vz0| < ratio && vz0 < -fall

template <std::size_t N>
inline bool MV_EbAny(const MvEbGravBand (&b)[N], float vz0, float vz, float g, float s)
{
    for (const auto& x : b)
        if (vz0 < -(g * x.mul + x.off * s + x.fall) && fabsf(vz + g) < x.tol * s) return true;
    return false;
}
template <std::size_t N>
inline bool MV_EbAny(const MvEbAbsBand (&b)[N], float vz0, float vz, float s)
{
    for (const auto& x : b)
        if (vz0 < -x.fall && fabsf(vz) < x.tol * s) return true;
    return false;
}
template <std::size_t N>
inline bool MV_EbAny(const MvEbChgBand (&b)[N], float vz0, float vz, float chg, float s)
{
    for (const auto& x : b)
        if (chg > x.change * s && vz0 < -x.fall && fabsf(vz) < x.tol * s) return true;
    return false;
}
template <std::size_t N>
inline bool MV_EbAny(const MvEbRatioBand (&b)[N], float vz0, float ratio)
{
    for (const auto& x : b)
        if (ratio < x.ratio && vz0 < -x.fall) return true;
    return false;
}

// NORMAL (mode 0)
inline constexpr MvEbGravBand k_mvEbNormalGrav[] = {
    { 0.65f, 0.f, 0.f, 2.8f },  { 1.00f, 0.25f, 0.f, 4.8f }, { 1.40f, 0.f, 0.f, 5.8f },
    { 1.44f, 0.f, 0.f, 1.4f },  { 0.50f, 0.f, 0.f, 2.5f },   { 2.00f, 0.f, 0.f, 6.5f },
    { 0.40f, 0.f, 0.f, 2.0f },  { 2.50f, 0.f, 0.f, 7.0f },   { 0.30f, 0.f, 0.f, 1.7f },
    { 3.00f, 0.f, 0.f, 7.5f },  { 0.25f, 0.f, 0.f, 1.5f },   { 3.50f, 0.f, 0.f, 8.0f },
};
inline constexpr MvEbAbsBand k_mvEbNormalAbs[] = {
    { 75.f, 14.5f }, { 18.f, 7.5f }, { 5.f, 2.2f },   { 95.f, 16.f },  { 3.f, 1.8f },
    { 105.f, 17.5f },{ 2.f, 1.5f },  { 115.f, 19.f }, { 1.5f, 1.2f },  { 125.f, 20.5f },
};
// ENHANCED (mode 1)
inline constexpr MvEbGravBand k_mvEbEnhGrav[] = {
    { 0.60f, 0.f, 0.f, 3.0f },  { 1.12f, 0.f, 0.f, 1.8f },  { 1.10f, 0.f, 0.f, 3.8f },
    { 0.40f, 0.f, 0.f, 2.2f },  { 0.30f, 0.f, 0.f, 2.0f },  { 1.50f, 0.f, 0.f, 4.5f },
    { 2.00f, 0.f, 0.f, 5.5f },  { 0.25f, 0.f, 0.f, 1.8f },  { 2.50f, 0.f, 0.f, 6.0f },
    { 0.20f, 0.f, 0.f, 1.5f },  { 3.00f, 0.f, 0.f, 7.0f },  { 4.00f, 0.f, 0.f, 9.0f },
    { 5.00f, 0.f, 0.f, 10.5f },
};
inline constexpr MvEbAbsBand k_mvEbEnhAbs[] = {
    { 20.f, 10.f }, { 4.f, 2.5f },  { 35.f, 13.f },  { 55.f, 18.f }, { 75.f, 23.f },
    { 3.f, 2.0f },  { 90.f, 28.f }, { 100.f, 32.f }, { 2.5f, 1.5f }, { 110.f, 35.f },
    { 120.f, 38.f },{ 2.0f, 1.2f }, { 1.0f, 0.8f },  { 130.f, 22.f },{ 140.f, 24.f },
};
inline constexpr MvEbGravBand k_mvEbEdgeGrav[] = { { 0.f, 0.f, 12.f, 4.5f }, { 0.f, 0.f, 18.f, 6.5f } };
inline constexpr MvEbAbsBand k_mvEbEdgeAbs[] = {
    { 25.f, 9.f },  { 8.f, 4.5f },  { 65.f, 28.f }, { 5.f, 3.5f },   { 40.f, 14.f }, { 80.f, 32.f },
    { 3.f, 2.5f },  { 50.f, 17.f }, { 90.f, 35.f }, { 100.f, 38.f }, { 2.f, 1.8f },
};
inline constexpr MvEbChgBand k_mvEbEnhChg[] = {
    { 15.f, 20.f, 11.f }, { 25.f, 35.f, 15.f }, { 40.f, 50.f, 20.f }, { 10.f, 15.f, 8.f },
    { 35.f, 45.f, 18.f }, { 50.f, 60.f, 23.f }, { 60.f, 70.f, 25.f }, { 80.f, 85.f, 30.f },
};
inline constexpr MvEbRatioBand k_mvEbEnhRatio[] = {
    { 0.30f, 15.f }, { 0.20f, 10.f }, { 0.15f, 8.f }, { 0.25f, 12.f },
    { 0.18f, 9.f },  { 0.12f, 7.f },  { 0.08f, 5.f }, { 0.05f, 3.f },
};
inline constexpr MvEbAbsBand k_mvEbEnhRise[] = { { 30.f, 18.f }, { 45.f, 28.f } };

// botox EdgeBugCheck. nearEdge / steepRing are traces, asked only when a band needs them
// (they run per SIM tick). Returns the verdict for one sim tick.
template <class NearEdgeFn, class SteepFn>
inline bool MV_EbCheck(int mode, float vz0, float vz, float g, float s, bool wasFalling, bool onGround,
                       NearEdgeFn nearEdge, SteepFn steepRing)
{
    if (!wasFalling || onGround) return false;
    if (mode == 2)   // ebanat: snapped to -g, or fell a step, slowed, still falls a step
    {
        if (vz0 < -g && roundf(vz) == -roundf(g)) return true;
        return vz0 < -g + 0.25f && vz0 < vz && vz < -g + 0.25f && !steepRing();
    }
    if (mode == 0)
    {
        if (MV_EbAny(k_mvEbNormalGrav, vz0, vz, g, s) || MV_EbAny(k_mvEbNormalAbs, vz0, vz, s)) return true;
        return vz0 < -g * 0.7f && roundf(vz) == -roundf(g);   // exact landing tick
    }
    const float chg = fabsf(vz - vz0);
    const float ratio = vz0 != 0.f ? fabsf(vz / vz0) : 0.f;
    if (MV_EbAny(k_mvEbEnhGrav, vz0, vz, g, s) || MV_EbAny(k_mvEbEnhAbs, vz0, vz, s)
        || MV_EbAny(k_mvEbEnhChg, vz0, vz, chg, s) || MV_EbAny(k_mvEbEnhRatio, vz0, ratio))
        return true;
    if ((MV_EbAny(k_mvEbEdgeGrav, vz0, vz, g, s) || MV_EbAny(k_mvEbEdgeAbs, vz0, vz, s)) && nearEdge())
        return true;
    for (const auto& b : k_mvEbEnhRise)
        if (vz0 < -b.fall && vz > vz0 + b.tol * s) return true;
    if (vz0 < -g * 0.9f && roundf(vz) == -roundf(g)) return true;
    if (vz0 < -12.f && fabsf(vz - vz0) < 1.8f * s) return true;   // fall taken away
    return vz0 < -15.f && vz > -2.f * s && vz < 2.f * s;
}

// ================= lip pick for to-edge (TF2, not botox) =====================================
// One HULL trace straight down per lane (lanes = centre offsets across the path, `step` apart).
// The hull sees a lip anywhere under its 48u footprint, so a rail thinner than the lane step still
// shows up, and the support boundary IS where the hull edge leaves the lip: the lip-side lane is a
// centre the hull overlaps by <= step (botox's point rays + 8u put the centre ON the lip line =
// 24u overlap = a landing). Boundary = floor next to void, or floors more than a step apart.
// Wall lanes (start solid) and lip faces under `standable` never count. Returns the lip-side lane
// nearest the centre lane that `ok(lane)` accepts, -1 = none.
enum { kMvLaneVoid = 0, kMvLaneFloor = 1, kMvLaneWall = 2 };
struct MvEbLane { int kind; float z, nz; };

template <class OkFn>
inline int MV_EbPickLip(const MvEbLane* l, int n, float stepUp, float standable, OkFn ok)
{
    int best = -1, bestDist = 1 << 30;
    for (int i = 0; i + 1 < n; i++)
    {
        const MvEbLane& a = l[i];
        const MvEbLane& b = l[i + 1];
        if (a.kind == kMvLaneWall || b.kind == kMvLaneWall) continue;
        if (a.kind == kMvLaneVoid && b.kind == kMvLaneVoid) continue;
        int lip;
        if (a.kind != b.kind) lip = a.kind == kMvLaneFloor ? i : i + 1;
        else if (fabsf(a.z - b.z) <= stepUp) continue;
        else lip = a.z > b.z ? i : i + 1;
        if (l[lip].nz < standable || !ok(lip)) continue;
        const int d = lip > n / 2 ? lip - n / 2 : n / 2 - lip;
        if (d < bestDist) { bestDist = d; best = lip; }
    }
    return best;
}

// near-edge gate: ground ahead dropped more than a step below the feet (or nothing in reach).
// botox wanted > 52u of a 66u ray, which every crate / low ledge under 50u failed.
inline bool MV_EbDropAhead(bool hit, float floorZ, float feetZ, float stepUp)
{
    return !hit || floorZ < feetZ - stepUp;
}

// ================= duck press window (botox edgebug.cpp:1063-1075) ===========================
// centre = first contact tick of the variant's base run (or reach + 1 when it touched nothing,
// approx = true). The window is walked BACKWARDS from `last`. twinRan = the ducked twin's base
// run already flew "ducked from tick 0", so press 0 is not offered again.
struct MvEbPress { int first; int last; };   // last < first = nothing to press
inline MvEbPress MV_EbSweepWindow(int centre, bool approx, int runTicks, bool twinRan, int retry, int approxSlack)
{
    MvEbPress w{ 0, -1 };
    if (centre <= 0) return w;
    const int slack = approx ? approxSlack : 0;
    w.last = centre - 1 + slack;
    if (w.last > runTicks - 1) w.last = runTicks - 1;
    w.first = centre - retry - slack;
    if (w.first < (twinRan ? 1 : 0)) w.first = twinRan ? 1 : 0;
    return w;
}

// ================= prefix reuse (TF2 port, not botox) =========================================
// Variants 0..7 come in pairs p = v >> 1: even = unducked, odd = same move ducked from tick 0.
// A run shares its opening ticks with one of two FAMILIES of its pair:
//   fam 0 = the even base run (no duck),  fam 1 = the odd base run (ducked from 0).
// sweep   (even v, duckAt k)     = fam 0 for ticks < k, then pressed -> resumes at k
// release (even v, duckAt 0, k)  = fam 1 for ticks < k, then let go  -> resumes at k
// base runs (duckAt/undoAt -1) ARE their family for the whole run and only feed it.
// limit = ticks of this run that equal the family (states before tick `limit` may be saved),
// resumeAt = tick whose family state the run may start from (-1 = start at 0).
struct MvEbFam { int fam; int limit; int resumeAt; };
inline MvEbFam MV_EbFamily(int v, int duckAt, int undoAt, int bend)
{
    constexpr int kAll = 1 << 30;
    if (v < 0 || v >= 8 || bend != 0) return { -1, 0, -1 };
    const bool odd = (v & 1) != 0;
    if (duckAt < 0 && undoAt < 0) return { odd ? 1 : 0, kAll, -1 };
    if (odd) return { -1, 0, -1 };
    if (duckAt > 0 && undoAt < 0) return { 0, duckAt, duckAt };
    if (duckAt == 0 && undoAt > 0) return { 1, undoAt, undoAt };
    return { -1, 0, -1 };
}

// ================= deep pass cost (TF2 port, not botox) =======================================
// botox spends the whole clock share whenever reach says geometry is near. 10-31 log: 515 of 667
// searches hit the cap, 6 of them found; far / no contact = 525 no-find searches at ~7ms each.
// FULL deep passes (bend, ebanat) only when a base run grounded within nearTicks; else they stop
// at half the cap and the next search picks their order up where this one stopped.
inline bool MV_EbDeepFull(int bestContact, int nearTicks)
{
    return bestContact >= 0 && bestContact <= nearTicks;
}

// bend pass order = botox's: bend-major (+1, -1, +2, -2, ... from bends[]), variant index minor.
// at = position in that order, total = 2 * nBends * searchCount.
struct MvEbBendAt { int bend; int idx; };
inline MvEbBendAt MV_EbBendEntry(int at, int searchCount, const int* bends)
{
    const int b = at / searchCount;
    return { (b & 1) ? -bends[b / 2] : bends[b / 2], at % searchCount };
}
// where this search's bend pass starts: full = botox order from 0, half = the carried cursor
inline int MV_EbBendStart(int cursor, int total, bool full)
{
    return (full || cursor <= 0 || cursor >= total) ? 0 : cursor;
}
// cursor for the next search: walked = entries consumed (run or skipped), found = a plan ended
// the pass. Finished or found = back to 0 (botox order); cut by the clock = carry on from there.
inline int MV_EbBendNext(int start, int walked, int total, bool found)
{
    return (found || walked >= total || total <= 0) ? 0 : (start + walked) % total;
}

// bend B pushes ticks 0..|B|-1, then flies the variant. Same variant + same sign, |B'| >= t: ticks
// 0..t-1 are pushes in both, so B' may start from B's state at tick t (saved at t = |B|).
// slot = (variant, sign), variants 0..7 only (the bend pass runs base variants).
inline int MV_EbBendSlot(int v, int bend)
{
    if (v < 0 || v >= 8 || bend == 0) return -1;
    return v * 2 + (bend < 0 ? 1 : 0);
}
inline bool MV_EbBendCanResume(int savedTick, int bend)
{
    const int n = bend < 0 ? -bend : bend;
    return savedTick >= 1 && savedTick <= n;
}

// ================= stance (TF2) ===============================================================
// iStance: 0 = prefer standing (any, but a ducked find that eats the jump is only a fallback),
// 1 = ducked only, 2 = standing only. bDucked = the hull's FL_DUCKING on the bug tick.
inline bool MV_EbStanceOk(int iStance, bool bDucked)
{
    return iStance == 0 || ((iStance == 1) == bDucked);
}

// does a ducked bug eat the next jump? Replay ends, crouch let go. In air FinishUnDuck only runs
// when CanUnduck's 20u down-trace is clear (gamemovement.cpp:4089-4105), so floor closer than that
// keeps the hull ducked till it lands, and CTFGameMovement::CheckJumpButton refuses a ground jump
// while FL_DUCKING (tf_gamemovement.cpp:1221). Judged one post-bug tick at a time.
enum MvEbJump { kEbJumpPending = 0, kEbJumpFree = 1, kEbJumpBlocked = 2 };
inline MvEbJump MV_EbJumpTick(bool bDucking, bool bOnGround)
{
    if (!bDucking) return kEbJumpFree;
    return bOnGround ? kEbJumpBlocked : kEbJumpPending;
}

// worth the post-bug sims? only a ducked bug, and only under "prefer standing"
inline bool MV_EbJumpCheck(int iStance, bool bDucked)
{
    return iStance == 0 && bDucked;
}

// ================= plan pick (botox edgebug.cpp:1132-1155 + TF2 stance) =======================
// err = heading error vs where the mouse turns (botox eb_aim_err). A plan that eats the jump is
// picked only when no other plan exists; between equals, lowest err wins with botox's 0.5 margin.
struct MvEbRank { float err; bool blocked; };
inline int MV_EbPickPlan(const MvEbRank* p, int n)
{
    int sel = -1;
    for (int i = 0; i < n; i++)
    {
        if (sel < 0) { sel = i; continue; }
        if (p[sel].blocked != p[i].blocked) { if (p[sel].blocked) sel = i; continue; }
        if (p[i].err < p[sel].err - 0.5f) sel = i;
    }
    return sel;
}
// re-plan while a plan flies: keep it unless the new one is clearly better. Standing beats a
// jump-eating ducked plan both ways; otherwise botox's 4 degree margin (no flicker between twins)
inline bool MV_EbKeepPlan(bool curBlocked, float curErr, bool selBlocked, float selErr)
{
    if (curBlocked != selBlocked) return !curBlocked;
    return selErr > curErr - 4.f;
}
