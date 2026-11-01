// texturebug.inl -- textually included by movement.cpp (above HeadSurf), not its own TU.
// Uses ~30 file-static helpers from movement.cpp (Pred_Simulate, TraceHull/TraceLine,
// MV_DetectWallLines, MV_SupportDistance, s_textureBugHit/s_tbOwnsCmd).

// ==== TB pin tests + ceiling detect ====

// negative half-gravity per tick (TB naming kept for the 1:1 ports)
static float TB_HalfGravityPerTick() { return TargetPredictZVel(); }
// |vz + flHalf| < tol with flHalf = -6 tests vz == +6. +halfgrav is the RIDE accept
// (head_surf, flat brush bottom); head_bounce wants -halfgrav but is not ported.
static bool TB_IsPositiveHalfGravity(float vz, float flHalf, float tol) { return fabsf(vz + flHalf) < tol; }

// xy inset for the flush-face retry below . Used by BOTH overhead detects
static constexpr float k_tbOverInset = 2.f;

// ceiling brush within flReach above the head.
// A hull already flush against a VERTICAL face (what a TB ride holds it against) reports
// frac 0 nz 0 whichever way the sweep points -- blind. Retry inset in xy so that face is
// off contact. Detect is a GATE: under-detecting kills the feature, so inset is the safe side
static bool TB_DetectCeiling(void* pLocal, const Vec3& vPos, const Vec3& vMins, const Vec3& vMaxs,
                             float flReach, GameTrace_t& traceOut)
{
    Vec3 vEnd = vPos;
    vEnd.z += flReach;
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    TraceHull(vPos, vEnd, vMins, vMaxs, MASK_PLAYERSOLID, &filter, &traceOut);
    if (traceOut.DidHit() && traceOut.plane.normal.z < -0.7f)
        return true;
    // nothing touched at all -> genuinely clear overhead, no retry (and no second trace)
    if (traceOut.fraction >= 1.f && !traceOut.startsolid && !traceOut.allsolid)
        return false;
    Vec3 vInMins = vMins, vInMaxs = vMaxs;
    vInMins.x += k_tbOverInset; vInMins.y += k_tbOverInset;
    vInMaxs.x -= k_tbOverInset; vInMaxs.y -= k_tbOverInset;
    if (vInMaxs.x <= vInMins.x || vInMaxs.y <= vInMins.y)
        return false;
    TraceHull(vPos, vEnd, vInMins, vInMaxs, MASK_PLAYERSOLID, &filter, &traceOut);
    return traceOut.DidHit() && traceOut.plane.normal.z < -0.7f;
}

// HeadBounce's own detect (coldtires texturebug_common.h:594 tb_detect_overhead). NOT the
// ceiling test: a crease between two displacements has no flat face, so the -0.7 ride filter
// rejects exactly the geometry a bounce lives on. -0.1 = "something overhead", the crease
// line does the work. Same xy-inset retry as TB_DetectCeiling, same reason.

struct TbOverDbg_t { float frac0 = 1.f, nz0 = 0.f; bool ss0 = false, retried = false; };

static bool TB_DetectOverhead(void* pLocal, const Vec3& vPos, const Vec3& vMins, const Vec3& vMaxs,
                              float flReach, GameTrace_t& traceOut, TbOverDbg_t& dbg)
{
    Vec3 vEnd = vPos;
    vEnd.z += flReach;
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    TraceHull(vPos, vEnd, vMins, vMaxs, MASK_PLAYERSOLID, &filter, &traceOut);
    dbg.frac0 = traceOut.fraction;
    dbg.nz0   = traceOut.plane.normal.z;
    dbg.ss0   = traceOut.startsolid || traceOut.allsolid;
    if (traceOut.fraction < 1.f && traceOut.plane.normal.z < -0.1f)
        return true;
    // nothing touched at all -> genuinely clear overhead, no retry (and no second trace)
    if (traceOut.fraction >= 1.f && !dbg.ss0)
        return false;
    Vec3 vInMins = vMins, vInMaxs = vMaxs;
    vInMins.x += k_tbOverInset; vInMins.y += k_tbOverInset;
    vInMaxs.x -= k_tbOverInset; vInMaxs.y -= k_tbOverInset;
    if (vInMaxs.x <= vInMins.x || vInMaxs.y <= vInMins.y)
        return false;
    dbg.retried = true;
    TraceHull(vPos, vEnd, vInMins, vInMaxs, MASK_PLAYERSOLID, &filter, &traceOut);
    return traceOut.fraction < 1.f && traceOut.plane.normal.z < -0.1f;
}

// ============================================================================
// PAPER 9.4.2: (s_perp - eps)/|d_perp| < (s_z - eps)/|d_z|. Every term is a world
// measurement, not a search parameter:
//   s_perp  hull face off the wall plane    -> TB_PlaneGap
//   s_z     brush top over our origin (D)   -> TB_FaceTopZ
//   face left ahead, for the park           -> TB_SeamRunout
// ============================================================================

// no-header string compare -- surface.name is usually shared per material but not
// guaranteed, so pointer equality alone would split one brush
static bool TB_SameStr(const char* a, const char* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

// EXACT s_perp. PAPER 9.1: s = r.n - (d + E.|n|), and 9.4.2 needs s_perp < eps STRICTLY.
// MV_SupportDistance cannot answer it: engine ray, so DIST_EPSILON is already in its
// fraction and it answers for whatever brush the ray hit first, not the sweep's plane.
// Zero traces, exact. Positive = outside the face, 0 = touching, negative = inside.
static float TB_PlaneGap(const cplane_mv& plane, const Vec3& origin,
                         const Vec3& mins, const Vec3& maxs)
{
    const Vec3 center(origin.x + (mins.x + maxs.x) * 0.5f,
                      origin.y + (mins.y + maxs.y) * 0.5f,
                      origin.z + (mins.z + maxs.z) * 0.5f);
    const Vec3 ext((maxs.x - mins.x) * 0.5f, (maxs.y - mins.y) * 0.5f,
                   (maxs.z - mins.z) * 0.5f);
    const float support = fabsf(ext.x * plane.normal.x) + fabsf(ext.y * plane.normal.y)
                        + fabsf(ext.z * plane.normal.z);
    return (center.x * plane.normal.x + center.y * plane.normal.y + center.z * plane.normal.z)
         - (plane.dist + support);
}

// SMALLEST MOVE INTO THE WALL THE ENGINE CAN MAKE.
// TryPlayerMove builds end = origin + dt*v in float (gamemovement.cpp:2593) and traces
// m_Delta = end - start (cmodel.h:86), so the move into an axis-aligned wall is
// k * ulp(origin coord), never v*dt: at |x| in [2048,4096) one step is 2.44e-4u against a
// 7.2e-5u window at vz -12, D 78. A band target under ulp/2 rounds to zero perpendicular
// motion = nothing to pin on. f'_perp only gets more negative as d_perp shrinks, so if any
// k pins, k=1 pins. Returns the step toward the wall (0 = not axis-aligned, caller keeps
// the old search). axis_out: 0 = x, 1 = y
static float TB_UlpStepIntoWall(const Vec3& normal, const Vec3& origin, int& axis_out)
{
    axis_out = -1;
    if (fabsf(normal.x) > 0.9999f)      axis_out = 0;
    else if (fabsf(normal.y) > 0.9999f) axis_out = 1;
    else return 0.f;
    const float c = axis_out == 0 ? origin.x : origin.y;
    const float n = axis_out == 0 ? normal.x : normal.y;
    // normal points OUT of the wall, so into the wall is -n
    return fabsf(nextafterf(c, n > 0.f ? -FLT_MAX : FLT_MAX) - c);
}

// two face probes are the SAME BRUSH FACE. plane.dist cannot separate two flush brushes,
// so the texture name carries the boundary -- hence TEXTURE bug.
// tex=false asks SAME WALL instead: a pixel surf rides straight through a texture change.
// Only TB_SeamRunout's park walk passes false; asking the texture question there stopped
// the walk mid-wall, since maps build long walls from differently skinned brushes.
static bool TB_FaceSame(const GameTrace_t& a, const GameTrace_t& b, bool tex = true)
{
    if (a.fraction >= 1.f || b.fraction >= 1.f) return false;
    if (a.plane.normal.x * b.plane.normal.x + a.plane.normal.y * b.plane.normal.y
      + a.plane.normal.z * b.plane.normal.z < 0.9995f) return false;
    if (fabsf(a.plane.dist - b.plane.dist) > 0.10f) return false;
    return !tex || TB_SameStr(a.surface.name, b.surface.name);
}

static constexpr float k_tbFaceOut   = 6.f;   // probe ray starts this far OUTSIDE the face
static constexpr float k_tbFaceIn    = 3.f;   // ... and ends this far inside it
static constexpr float k_tbFaceStep  = 8.f;
static constexpr int   k_tbFaceSteps = 24;    // 192u of face, then the caller keeps the column ray
static constexpr int   k_tbFaceRefine = 5;    // 8u -> 0.25u
// cached top is an absolute z, so D drifts as we fall -- both caches expire on MOVEMENT too
static constexpr int   k_tbFaceRefresh = 8;   // ticks a cached walk survives on one plane
static constexpr float k_tbFaceOrgDrift = 8.f;// ... and how far the origin may fall under it
static constexpr int   k_tbEdgeRefresh = 8;   // the edge is a world point, but the plane is not
static constexpr float k_tbHeadProbe = 2.f;   // probe this far ABOVE the head = inside the brush
                                              // whose bottom the head is crossing (PAPER 9.4.2)
// how far ahead the runout walk looks. 192u = over a second of surf at 240 u/s and 8x the
// longest stopping distance a ride needs, so a face reaching the end has nothing to brake for
static constexpr float k_tbRunoutMax = 192.f;

// WHERE THE PROBES START. `primary.endpos` is NOT a point on the wall: MV_DetectWallLines
// fills it from a z-column hull sweep, a per-height line trace or the hull prefilter, and
// only the line case lands on the surface -- the hull cases stop ~24u short in xy, at the
// origin's z. The PLANE is exact and always there, so project onto it instead.
// Slide along the FLAT normal until the point at height z is exactly on the plane.
// Projecting along the full 3D normal and stamping z leaves the point off the plane by
// n.z*(...), i.e. drifting with HEIGHT, and TB_FaceTopZ walks 192u through that: the walk
// runs off the side of the face, calls the first rung the top, understates D and opens the
// aim window eps*|vz|/(D+eps) far too wide on tilted walls. For a vertical face n.z is 0,
// nl is 1, and this is the old arithmetic term for term
static Vec3 TB_FaceAnchor(const cplane_mv& plane, const Vec3& origin, float z)
{
    const float nl = plane.normal.Length2D();
    if (nl < 1e-4f) return Vec3(origin.x, origin.y, z);   // no horizontal normal = no face to slide to
    const float d_z = origin.x * plane.normal.x + origin.y * plane.normal.y
                    + z * plane.normal.z - plane.dist;
    const float t = d_z / nl;
    return Vec3(origin.x - (plane.normal.x / nl) * t,
                origin.y - (plane.normal.y / nl) * t, z);
}

// one probe of the wall face at (anchor xy, slid `slide` along dir, height z)
static void TB_FaceProbe(void* pLocal, const Vec3& anchor, const Vec3& out,
                         const Vec3& along, float slide, float z, GameTrace_t* tr)
{
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    const Vec3 base(anchor.x + along.x * slide, anchor.y + along.y * slide, z);
    TraceLine(Vec3(base.x + out.x * k_tbFaceOut, base.y + out.y * k_tbFaceOut, z),
              Vec3(base.x - out.x * k_tbFaceIn,  base.y - out.y * k_tbFaceIn,  z),
              MASK_PLAYERSOLID, &fil, tr);
}

// D = the paper's |s_z| = this brush's top - our origin. A 1024u column ray reports the top
// of the WHOLE STACK (3x too big on a 3-brush wall, window eps*|vz|/(D+eps) closes to
// nothing), so walk the FACE and stop where plane or texture changes. Over-stating D is the
// safe side, so a face running past the ladder returns false and the caller keeps the column.
static float TB_FaceTopZ(void* pLocal, const cplane_mv& plane, const Vec3& origin,
                         float z_from, bool& ok)
{
    ok = false;
    const float nl = plane.normal.Length2D();
    if (nl < 1e-4f) return 0.f;
    const Vec3 out(plane.normal.x / nl, plane.normal.y / nl, 0.f);
    const Vec3 none(0.f, 0.f, 0.f);
    const Vec3 anchor = TB_FaceAnchor(plane, origin, z_from);

    GameTrace_t ref{};
    TB_FaceProbe(pLocal, anchor, out, none, 0.f, z_from, &ref);
    if (ref.fraction >= 1.f) return 0.f;          // nothing at that height -> no walk to make

    float good = z_from, bad = 0.f;
    bool  have_bad = false;
    for (int i = 1; i <= k_tbFaceSteps; ++i)
    {
        GameTrace_t tr{};
        const float z = z_from + k_tbFaceStep * (float)i;
        TB_FaceProbe(pLocal, anchor, out, none, 0.f, z, &tr);
        if (TB_FaceSame(ref, tr)) { good = z; continue; }
        bad = z; have_bad = true; break;
    }
    if (!have_bad) return 0.f;
    for (int i = 0; i < k_tbFaceRefine; ++i)
    {
        const float mid = (good + bad) * 0.5f;
        GameTrace_t tr{};
        TB_FaceProbe(pLocal, anchor, out, none, 0.f, mid, &tr);
        if (TB_FaceSame(ref, tr)) good = mid; else bad = mid;
    }
    ok = true;
    return good;
}

// ...and the same walk DOWNWARD, the other end of D.
// PAPER (10) `(H_brush + H_player)/eps < |vz|/|v_in|`: D belongs to the BRUSH. `top - origin`
// only equals it on the tick the head is level with the brush bottom; 30u under it D reads
// 30 too big and the window closes by the same factor. Walking both ends measures H_brush,
// so the answer stops moving as we fall past. Same stop rule, negative step; `good` is the
// lowest z still on this face, so it IS the bottom to k_tbFaceRefine resolution.
static float TB_FaceBotZ(void* pLocal, const cplane_mv& plane, const Vec3& origin,
                         float z_from, bool& ok)
{
    ok = false;
    const float nl = plane.normal.Length2D();
    if (nl < 1e-4f) return 0.f;
    const Vec3 out(plane.normal.x / nl, plane.normal.y / nl, 0.f);
    const Vec3 none(0.f, 0.f, 0.f);
    const Vec3 anchor = TB_FaceAnchor(plane, origin, z_from);

    GameTrace_t ref{};
    TB_FaceProbe(pLocal, anchor, out, none, 0.f, z_from, &ref);
    if (ref.fraction >= 1.f) return 0.f;          // nothing at that height -> no walk to make

    float good = z_from, bad = 0.f;
    bool  have_bad = false;
    for (int i = 1; i <= k_tbFaceSteps; ++i)
    {
        GameTrace_t tr{};
        const float z = z_from - k_tbFaceStep * (float)i;
        TB_FaceProbe(pLocal, anchor, out, none, 0.f, z, &tr);
        if (TB_FaceSame(ref, tr)) { good = z; continue; }
        bad = z; have_bad = true; break;
    }
    if (!have_bad) return 0.f;                    // face runs past the ladder -> no measurement
    for (int i = 0; i < k_tbFaceRefine; ++i)
    {
        const float mid = (good + bad) * 0.5f;
        GameTrace_t tr{};
        TB_FaceProbe(pLocal, anchor, out, none, 0.f, mid, &tr);
        if (TB_FaceSame(ref, tr)) good = mid; else bad = mid;
    }
    ok = true;
    return good;
}

// HOW MUCH FACE IS LEFT AHEAD, along `along` (unit, horizontal, tangent to the wall).
// "Park at the end of a surf" cannot be a reaction: by the time the pin stops landing the
// hull is past the edge and falling (MEMORY law 2). The wall is static, so measure it first.
// tex_stop false = a texture change is NOT the end of the wall. TB needs the texture
// boundary (that is the bug it flies); the pixel surf park needs the plane -- see TB_FaceSame
static float TB_SeamRunout(void* pLocal, const cplane_mv& plane, const Vec3& origin,
                           const Vec3& along, float z_at, float max_len, Vec3& out_edge,
                           bool tex_stop)
{
    const float nl = plane.normal.Length2D();
    if (nl < 1e-4f) return -1.f;
    const Vec3 out(plane.normal.x / nl, plane.normal.y / nl, 0.f);
    const Vec3 anchor = TB_FaceAnchor(plane, origin, z_at);

    GameTrace_t ref{};
    TB_FaceProbe(pLocal, anchor, out, along, 0.f, z_at, &ref);
    // -1 = NO MEASUREMENT, never "zero units of face left" -- else the brake reads a probe
    // that could not see the face as "the edge is here" and stops dead on every surf
    if (ref.fraction >= 1.f) return -1.f;

    const int steps = (int)(max_len / k_tbFaceStep) + 1;
    float good = 0.f, bad = 0.f;
    bool  have_bad = false;
    for (int i = 1; i <= steps; ++i)
    {
        const float d = fminf(k_tbFaceStep * (float)i, max_len);
        GameTrace_t tr{};
        TB_FaceProbe(pLocal, anchor, out, along, d, z_at, &tr);
        if (TB_FaceSame(ref, tr, tex_stop)) { good = d; if (d >= max_len) break; continue; }
        bad = d; have_bad = true; break;
    }
    if (!have_bad) { out_edge = anchor; return max_len; }
    for (int i = 0; i < k_tbFaceRefine; ++i)
    {
        const float mid = (good + bad) * 0.5f;
        GameTrace_t tr{};
        TB_FaceProbe(pLocal, anchor, out, along, mid, z_at, &tr);
        if (TB_FaceSame(ref, tr, tex_stop)) good = mid; else bad = mid;
    }
    out_edge = Vec3(anchor.x + along.x * good, anchor.y + along.y * good, z_at);
    return good;
}

// ============================================================================
// PREDICTIVE LEDGE INTERCEPT
// ----------------------------------------------------------------------------
// MEASURED (C:otox-tf2\debug.log, 79 wall-contact episodes): 1-2 ticks 0/25 pinned, 3-5 ticks
// 0/1, 6+ ticks 30/53 (57%). Short contacts -- small ledges, short walkways -- read
// minsup 20..32 with sims=1: the face really is 20-32u past the hull and nothing can touch
// it. The creep closes ~0.09u/tick, so 27u takes ~300 ticks, and TB only starts once the
// hull is flush, i.e. after a short face is gone. The catch has to START EARLIER.
// So: roll the ballistic path forward, find the face before it is in reach, spend the
// airtime closing the gap.
//
// AirAccelerate (gamemovement.cpp:1990) sets every constant here:
//     accelspeed = sv_airaccelerate * forward * dt, capped at min(forward,30) - (vel.wishdir)
// With forward >= 200 one tick adds the whole cap, so perpendicular closing speed saturates
// at 30 u/s (27u in 60 ticks, a 6u miss in 13) and one OUTWARD tick nulls it again. That is
// required: the paper window is ~0.03 u/s, so arriving at 30 u/s is a smash, not a
// texturebug. Hence the proportional law below -- ask for gap/dt, land on the face with
// v_in already collapsed.
// LAW 1 (never let xy die) holds by construction: the wish is written along +-plane normal,
// so the tangential velocity is never touched. The inward arm only ever ADDS.
static constexpr int   k_tbPredMaxProbes = 8;   // full scans per rescan
// TRACES are this feature's cost and nothing fences them -- the clock and k_cmdSimCap guard
// SIMS. One live MV_DetectWallLines runs 120-230 traces. A predicted scan only needs the
// PLANE, not the seam: the centre-column sweep finds short lips whatever the line grid is,
// so probes run at the coarsest step and the live tick keeps the user's own
static constexpr float k_tbPredScanStep = 8.f;
static constexpr int   k_tbPredTraceCap = 400;  // hard stop on one rescan, whatever it found
static constexpr int   k_tbPredSeamMul  = 3;    // seam crossing solved this many horizons out
// 30 is AirAccelerate's ceiling ON GAIN, not a speed limit: it never raises inward speed
// past 30, but never lowers what the flight already carries. Read as a hard cap it
// certified nothing reachable all session (43 PRED lines, 43 skips, reach=0). It is the
// FLOOR of the closing rate; vin is the other half
static constexpr float k_tbPredVinCap   = 30.f;
static constexpr float k_tbPredFwdCap   = 450.f;
// be flush this many ticks BEFORE the seam: arriving on the seam builds the 1-2 tick
// episode that never pins, arriving early builds the 6+ tick one that pins 57%. Costs a
// higher closing rate, which the reach gate still has to certify
static constexpr int   k_tbPredLead     = 5;
static constexpr int   k_tbPredMaxPath  = 128;  // rolled ticks the path array holds
// where the approach STOPS and the sweep takes over. One capped tick covers 30*dt = 0.45u,
// so under half a unit is inside the taper and the creep (~0.09u/tick) closes the rest.
// Handing over closer courts the overshoot that ends as a wallbug glue.
// land test: the path is a parabola, a hull sweep is a chord, so the chord sags under the
// flight by g*T^2/8 and reports floors it never touches. 6 ticks = 0.09s keeps that under 1u
static constexpr int   k_tbPredLandChunk = 6;
static constexpr int   k_tbPredLandSegs  = 16;  // ... and the whole test is <= 16 traces, 96 ticks
static constexpr float k_tbPredArrive   = 0.25f;
static constexpr int   k_tbPredRefresh  = 4;    // ticks a scanned face is reused for
static constexpr float k_tbPredOrgDrift = 24.f; // ... and how far we may fall under it first
static int s_tbPredDiagTick = -1000;

struct TbPred_t
{
    bool      ok        = false;
    cplane_mv plane{};
    int       hit_tick  = 0;      // first rolled tick the widened hull meets the face
    int       seam_tick = -1;     // tick the hull straddles the face TOP (the catchable window)
    float     seam_z    = 0.f;
    float     gap       = 0.f;    // hull face -> plane RIGHT NOW (TB_PlaneGap, exact, 0 traces)
    float     vin       = 0.f;    // inward perpendicular speed right now
    int       traces    = 0;
    int       probes    = 0;      // diag: probes the fan actually ran before it hit / ran out
    int       first     = 0;      // ...and the tick span they covered. p1 - p0 against the span
    int       last      = 0;      //    says whether the trace cap or the geometry ended the fan
    float     face_h    = 0.f;    // seam top -> face bottom. 1e9 = taller than the ladder
};

// Roll the path with the engine's own tick shape (gamemovement.cpp):
//   StartGravity :1501  vz += t      (t = TargetPredictZVel(), negative)
//   TryPlayerMove:2968  org += v*dt
//   FinishGravity:1950  vz += t
// so the displacement of a tick uses the MID-tick vz, which is what the sweeps already sim.
static void TB_RollPath(const Vec3& org, const Vec3& vel, float dt, float half,
                        int horizon, Vec3* out, int max_out)
{
    Vec3  p = org;
    float vz = vel.z;
    const int n = horizon < max_out ? horizon : max_out;
    for (int i = 0; i < n; ++i)
    {
        vz += half;
        p.x += vel.x * dt; p.y += vel.y * dt; p.z += vz * dt;
        vz += half;
        out[i] = p;
    }
}

// Find the first vertical face the flight passes WITHIN `reach` of, and when the hull straddles
// its top. `reach` is the whole point: the live scan only sees what already touches the hull, so
// a lip 20u to the side is invisible until the tick it is gone.
static void TB_PredictLedge(void* pLocal, const Vec3& org, const Vec3& vel,
                            const Vec3& mins, const Vec3& maxs,
                            int horizon, float reach, float ramp_band, TbPred_t& out)
{
    out = TbPred_t{};
    if (!g_engineTrace || horizon < 2) return;
    if (TbBudgetExpired()) return;      // the clock guards the whole TB family, traces included

    const float dt   = TickInterval();
    const float half = TargetPredictZVel();
    if (dt <= 0.f) return;
    const int tr_start = s_tracesThisCmd;   // real count, not an accounted one

    Vec3 path[k_tbPredMaxPath];
    const int max_path = (int)(sizeof(path) / sizeof(path[0]));
    const int span = horizon < max_path ? horizon : max_path;
    TB_RollPath(org, vel, dt, half, span, path, max_path);

    // one WIDE reject sweep first. The chord is straight and the flight is a parabola, so the
    // box has to swallow the sag: gravity puts the real path above the chord by <= g*T^2/8.
    // Miss = no geometry near the whole flight, and the feature costs one trace that tick
    const float T   = (float)span * dt;
    const float sag = fabsf(half) * 2.f / dt * T * T * 0.125f;   // half = -g*dt/2  ->  g = -2*half/dt
    const Vec3  wide_mins(mins.x - reach, mins.y - reach, mins.z);
    const Vec3  wide_maxs(maxs.x + reach, maxs.y + reach, maxs.z + sag);
    TraceFilterWorldBrush fil; fil.Setup(pLocal);
    GameTrace_t fat{};
    TraceHull(org, path[span - 1], wide_mins, wide_maxs, MASK_PLAYERSOLID, &fil, &fat);
    if (!fat.DidHit()) { out.traces = s_tracesThisCmd - tr_start; return; }

    // ---- THE SCAN IS MV_DetectWallLines, RUN AT A FUTURE ORIGIN ---------------------------
    // Not a widened swept box: widening the hull by `reach` makes the sweep report startsolid
    // next to ANY geometry, and a startsolid trace carries no plane. MV_DetectWallLines is
    // already startsolid-safe (16 directions, hull prefilter, centre-column sweep, facing +
    // max_normal_z filters) and takes the origin as an argument, so it works at a place we
    // have not arrived at yet. Nearest probe first = engage the FIRST ledge on the flight.
    //
    // Probe SPACING: `span / probes` ticks (128/8 = 16) is 0.24s, so probes only overlap at
    // 400 u/s -- at 600 there is a 48u blind gap between every pair, at 800 96u, and a short
    // ledge is exactly what fits in one. So start at the fat sweep's own hit (one step back,
    // chord under parabola) and step by the distance ONE probe covers, on two axes since a
    // slow fall moves in z and a fast run in xy:
    //     horizontal -- 2*(hull half + reach), the along-path width of one scan
    //     vertical   -- the hull's z span, which the line grid and column sweep cross
    // Tighter wins, re-evaluated per probe because |vz| grows all the way down.
    const float cover_h = (fminf(fminf(fabsf(mins.x), fabsf(maxs.x)),
                                 fminf(fabsf(mins.y), fabsf(maxs.y))) + reach) * 2.f;
    const float cover_v = fmaxf(maxs.z - mins.z, 1.f);
    const float per_xy  = vel.Length2D() * dt;
    // ticks one probe at `at_tick` owns before the next has to start
    const auto probe_adv = [&](int at_tick) -> int
    {
        const float dz = fabsf(vel.z + 2.f * half * (float)(at_tick + 1)) * dt;
        const float a_h = per_xy > 0.01f ? cover_h / per_xy : (float)span;
        const float a_v = dz     > 0.01f ? cover_v / dz     : (float)span;
        const int a = (int)fminf(fminf(a_h, a_v), (float)span);
        return a < 1 ? 1 : a;
    };

    int i_hit = (int)(fat.fraction * (float)span);
    if (i_hit < 0)     i_hit = 0;
    if (i_hit >= span) i_hit = span - 1;
    int i = i_hit - probe_adv(i_hit);
    if (i < 0) i = 0;

    const float saved_step = g_tbScanStep;
    g_tbScanStep = k_tbPredScanStep;
    for (int p = 0; p < k_tbPredMaxProbes && i < span; ++p)
    {
        if (s_tracesThisCmd - tr_start > k_tbPredTraceCap || TbBudgetExpired()) break;
        out.probes = p + 1;
        out.last   = i;
        if (p == 0) out.first = i;
        GameTrace_t tr{};
        float ang = 0.f;
        // pass a velocity: the scan's fall lookahead (`scan_mins.z -= |vz| * dt`, the
        // short-ledge fix) only runs when pVel is given, and a rolled tick's velocity is
        // known exactly from TB_RollPath's shape
        const Vec3 pvel(vel.x, vel.y, vel.z + 2.f * half * (float)(i + 1));
        // displacement reject, same as the live hands-off at the top of TextureBug: a texture
        // bug is a BRUSH-plane artifact, a displacement is triangle soup and TB_FaceSame fails
        // at the first rung. Keep scanning past it -- a brush face may sit further along
        if (MV_DetectWallLines(pLocal, path[i], mins, maxs, 0.f, tr, ang, ramp_band, reach, &pvel)
            && !(tr.dispFlags & DISPSURF_SURFACE))
        {
            out.plane    = tr.plane;
            out.hit_tick = i + 1;
            out.ok       = true;
            break;
        }
        i += probe_adv(i);
    }
    g_tbScanStep = saved_step;
    if (!out.ok) { out.traces = s_tracesThisCmd - tr_start; return; }

    // outside the face is the only side this works from, and it is also the proof the scan did
    // not just answer with the wall we are already inside
    out.gap = TB_PlaneGap(out.plane, org, mins, maxs);
    out.vin = -(vel.x * out.plane.normal.x + vel.y * out.plane.normal.y);
    if (out.gap <= 0.f) { out.ok = false; return; }
    // ... and NOT absurdly outside it. A plane the probe hull touched sits at most one flight
    // length plus the reach off this origin; the log carried gaps of 2870-6032 on a 233u
    // flight. Rejecting here saves the 400-trace rescan they trigger every k_tbPredRefresh
    const float span_xy = vel.Length2D() * (float)span * dt;
    const float hull_sup = fabsf(maxs.x - mins.x) + fabsf(maxs.y - mins.y);
    if (out.gap > span_xy + reach + hull_sup + 8.f) { out.ok = false; return; }

    // THE SEAM. A face with no measurable top is a wall, not a ledge. Walk it from just above
    // the feet at the predicted contact, same walk the live tick uses. THREE heights because
    // the walk needs a height the face EXISTS at, and a probe in open air returns "no walk to
    // make", not "no ledge": feet (the ledge we drop past), mid-hull, then under the feet
    // where the scan's fall lookahead found it
    const Vec3& at = path[out.hit_tick - 1];
    bool walked = false;
    out.seam_z = TB_FaceTopZ(pLocal, out.plane, at, at.z + mins.z + 2.f, walked);
    if (!walked)
        out.seam_z = TB_FaceTopZ(pLocal, out.plane, at, at.z + (mins.z + maxs.z) * 0.5f, walked);
    if (!walked)
        out.seam_z = TB_FaceTopZ(pLocal, out.plane, at, at.z + mins.z - 8.f, walked);
    if (!walked) { out.traces = s_tracesThisCmd - tr_start; out.ok = false; return; }

    // log only, NOT a gate: a seam can sit anywhere up a wall, so "short face" does not mean
    // "step" -- a 64u face 300u off the ground is a fine seam. The gate is the FLIGHT, below
    bool bot_ok = false;
    const float bot_z = TB_FaceBotZ(pLocal, out.plane, at, out.seam_z - 2.f, bot_ok);
    out.face_h = bot_ok ? (out.seam_z - bot_z) : 1e9f;

    // WHEN the hull straddles that top. Pure z arithmetic, so it runs past the traced horizon --
    // the face is a plane and it does not move, and driving early is the entire point of this
    float vz = vel.z, z = org.z;
    const int  far_span = span * k_tbPredSeamMul;
    for (int i = 1; i <= far_span; ++i)
    {
        vz += half; z += vz * dt; vz += half;
        if (out.seam_z >= z + mins.z && out.seam_z <= z + maxs.z) { out.seam_tick = i; break; }
        if (z + maxs.z < out.seam_z) break;   // fallen clean under the ledge, never coming back
    }
    if (out.seam_tick < 1) { out.traces = s_tracesThisCmd - tr_start; out.ok = false; return; }

    // ---- A HOP IS NOT A FALL --------------------------------------------------------
    // Bhopping on uneven ground used to trip this: a lip in the ground is a VERTICAL brush
    // face (nz 0.00), the ramp band waves it through, its top IS a seam by every test above,
    // and the loop above finds the hull straddling it on the way down -- because that is the
    // higher ground about to be landed on. Nothing about the FACE separates the two cases.
    // The FLIGHT does: a texturebug ride is a fall, a hop LANDS first.
    // So sweep the hull along the rolled path to the seam crossing and see what it meets.
    // nz > 0.7 is the engine's standable test (CategorizePosition). The wall case is safe by
    // construction: falling PAST a ledge keeps the hull outside it in xy, so the sweep meets
    // the face (nz ~ 0) or nothing; only a path going OVER the top can meet a floor.
    // startsolid/allsolid carry no plane and are not a reject -- near-flush is where a real
    // approach lives. Ending at the seam keeps ground below the target seam out of it, and
    // past k_tbPredLandChunk * k_tbPredLandSegs it fails OPEN (a landing 1.4s out does not
    // decide this tick's cmd)
    TraceFilterWorldBrush land_fil; land_fil.Setup(pLocal);
    const int land_span = out.seam_tick < k_tbPredLandChunk * k_tbPredLandSegs
                        ? out.seam_tick : k_tbPredLandChunk * k_tbPredLandSegs;
    Vec3  seg_from = org, cur = org;
    float lz = org.z, lvz = vel.z;
    for (int i = 1; i <= land_span; ++i)
    {
        lvz += half; lz += lvz * dt; lvz += half;
        cur.x += vel.x * dt; cur.y += vel.y * dt; cur.z = lz;
        if (i % k_tbPredLandChunk != 0 && i != land_span) continue;
        GameTrace_t land{};
        TraceHull(seg_from, cur, mins, maxs, MASK_PLAYERSOLID, &land_fil, &land);
        seg_from = cur;
        if (land.startsolid || land.allsolid || land.fraction >= 1.f) continue;
        if (land.plane.normal.z <= 0.7f) continue;
        if (g_debugLogEnable && DiagOk(s_tbPredDiagTick, false))
            DebugLog("TB PRED: hop reject landtick=%d/%d landnz=%.2f seam=%.1f left=%d faceh=%.1f "
                     "gap=%.1f nz=%.2f xy=%.0f vz=%.1f",
                     i, out.seam_tick, land.plane.normal.z, out.seam_z - org.z, out.seam_tick,
                     out.face_h, out.gap, out.plane.normal.z,
                     vel.Length2D(), vel.z);
        out.ok = false;
        break;
    }
    out.traces = s_tracesThisCmd - tr_start;
}

// ---- TB sweep helpers (coldtires 81st/103rd/106th/107th) ----

// closed-form AirAccelerate (gamemovement.cpp:1990). Answers "does this variant move the
// player anywhere an already-simmed one did not" for ZERO sims. wishspd caps at 30, so at
// ride speed most of the fwd/angle grid collapses to the same null move
static Vec3 TB_AirAccelDelta(const Vec3& base_vel, float world_yaw, float forward,
                             float accel, float dt)
{
    float wishspeed = forward, yaw = world_yaw;
    if (wishspeed < 0.f) { wishspeed = -wishspeed; yaw += 180.f; } // negative fwd = wish backwards
    if (wishspeed <= 0.f) return Vec3();

    const float rad = DEG2RADF(yaw);
    const Vec3  wishdir(cosf(rad), sinf(rad), 0.f);
    const float wishspd = fminf(wishspeed, 30.f);
    const float currentspeed = base_vel.x * wishdir.x + base_vel.y * wishdir.y;
    const float addspeed = wishspd - currentspeed;
    if (addspeed <= 0.f) return Vec3();   // engine returns early -- velocity untouched
    // surface friction is 1 in air; over-estimating only makes two variants look MORE
    // different than they are = the safe direction for a dedup
    float accelspeed = accel * wishspeed * dt;
    if (accelspeed > addspeed) accelspeed = addspeed;
    return wishdir * accelspeed;
}

// same closed form, wish direction already in hand -- the ladders below run one direction
// against a whole forward list, so the trig belongs outside that loop
static Vec3 TB_AirAccelDeltaDir(const Vec3& base_vel, const Vec3& wishdir, float forward,
                                float accel, float dt)
{
    if (forward <= 0.f) return Vec3();
    const float wishspd = fminf(forward, 30.f);
    const float addspeed = wishspd - (base_vel.x * wishdir.x + base_vel.y * wishdir.y);
    if (addspeed <= 0.f) return Vec3();
    float accelspeed = accel * forward * dt;
    if (accelspeed > addspeed) accelspeed = addspeed;
    return wishdir * accelspeed;
}

// ---- COLDTIRES 103rd: THE ANALYTIC FAST PROBE -------------------------------------
// The tick cost was never the SEARCH: every variant paid a full CPrediction RunCommand
// (restore + PreThink + ProcessMovement + PostThink) only to be told it flew through empty
// air -- 75 sims a tick in the log, every one reading mindz = |vz| - 6, i.e. pin == 0.
// A contact-free tick has a closed form (gamemovement.cpp):
//     StartGravity  :1501   vz += t          (t = TargetPredictZVel())
//     AirAccelerate :1960   v.xy += delta    (TB_FastAccel below)
//     TryPlayerMove :2968   fraction 1 -> origin = org + v*dt, velocity UNTOUCHED
//     FinishGravity :1950   vz += t
// so vz lands on vz0 + 2t whatever the variant -- which also means the fast path can never
// CHOOSE a pin, and the tick guard refuses it outright near the band. Two hull traces prove
// no contact (the sweep AND the endpos stuck re-test, which ZEROES velocity when it fires,
// i.e. it IS a pin, so it is reproduced, never assumed) and one support ray reproduces the
// creep measurement exactly. Anything else -- contact, support inside the retry band, a
// duck, a calibration miss -- falls through to a real sim, so the pick is unchanged
struct TraceFilterSolidAll
{
    void** vfptr;
    void*  pSkip;
    // handle-safe: static props arrive as IHandleEntity and are never dereferenced here
    static bool __fastcall sShouldHit(TraceFilterSolidAll* self, void* pEnt, int /*mask*/)
    { return pEnt && pEnt != self->pSkip; }
    static int __fastcall sTraceType(TraceFilterSolidAll* /*self*/) { return 0; } // TRACE_EVERYTHING
    void Setup(void* skip)
    {
        static void* vt[2] = { (void*)&sShouldHit, (void*)&sTraceType };
        vfptr = vt;
        pSkip = skip;
    }
};
// the engine's move filter hits players and team clip, which the TB scan filter drops. A
// CLEARANCE proof needs the SUPERSET: anything we pass through that the engine would hit
// turns an occupied tick into an "empty" one
static constexpr unsigned CONTENTS_TEAM1_MV = 0x800, CONTENTS_TEAM2_MV = 0x1000;
static constexpr unsigned k_tbFastMask = MASK_PLAYERSOLID | CONTENTS_TEAM1_MV | CONTENTS_TEAM2_MV;

struct TbFast_t
{
    bool  ok = false;
    bool  duck_seen = false;   // a duck sim ran -- the collideable may hold the duck hull now
    Vec3  org{}, vel{}, mins{}, maxs{};
    float dt = 0.f;
    float a = 0.f;             // accelspeed per unit forward -- MEASURED, see the calibration
    float vz_start = 0.f;      // vz the move runs with (after StartGravity)
    float vz_free = 0.f;       // vz at the end of a contact-free tick
    float dz_free = 0.f;
};
// per-cmd ceiling, so a pathological tick can never trade a sim spike for a trace spike
static int s_tbFastProbes = 0;
static constexpr int k_tbFastProbeCap = 512;

static Vec3 TB_FastAccel(const TbFast_t& f, const Vec3& wishdir, float forward)
{
    if (forward <= 0.f) return Vec3();
    const float wishspd = fminf(forward, 30.f);   // :1975 cap, addspeed arm only
    const float addspeed = wishspd - (f.vel.x * wishdir.x + f.vel.y * wishdir.y);
    if (addspeed <= 0.f) return Vec3();           // :1985 engine returns, velocity untouched
    float accelspeed = f.a * forward;
    if (accelspeed > addspeed) accelspeed = addspeed;
    return wishdir * accelspeed;
}

// the two traces TryPlayerMove itself runs on a clean tick. false = something is in the way and
// the caller owes a real sim; the endpos re-test is not optional -- its failure IS a pin
static bool TB_FastClear(void* pLocal, const TbFast_t& f, const Vec3& vel_move, Vec3& out_end)
{
    if (!g_engineTrace) return false;
    out_end = f.org + vel_move * f.dt;
    TraceFilterSolidAll fil; fil.Setup(pLocal);
    GameTrace_t sweep{};
    TraceHull(f.org, out_end, f.mins, f.maxs, k_tbFastMask, &fil, &sweep);
    if (sweep.startsolid || sweep.allsolid || sweep.fraction < 1.f) return false;
    GameTrace_t stuck{};
    TraceHull(out_end, out_end, f.mins, f.maxs, k_tbFastMask, &fil, &stuck);
    return !stuck.startsolid && !stuck.allsolid && stuck.fraction >= 1.f;
}

// below this two variants move the player <1.6e-4u apart over a tick -- 200x under the
// 0.03125 surface extension that decides every collision here
static constexpr float k_tbDupVelEps = 0.01f;

// ONE dedup list per hull for the whole tick. A delta an earlier pass evaluated reproduces
// that answer exactly: it cannot pin (the sweep would have stopped there) and it cannot take
// the creep pick either (equal support never passes the strict <)
struct TbDedup_t
{
    static constexpr int k_max = 288;   // sweep + solve, worst case, one sign each
    Vec3 seen[k_max];
    int  count = 0;

    bool SeenOrAdd(const Vec3& d)
    {
        for (int i = 0; i < count; ++i)
            if (fabsf(d.x - seen[i].x) < k_tbDupVelEps && fabsf(d.y - seen[i].y) < k_tbDupVelEps)
                return true;
        if (count < k_max) seen[count++] = d;
        return false;
    }
};
// ============================================================================
// TB/HS/HB REWRITTEN 1:1 OFF COLDTIRES texturebug.cpp (109b .. 124th)
// ----------------------------------------------------------------------------
// The port replaced a dih-only machine grown on top of it (seam scanner, phase ladder,
// engage gate, ride speed floor, window keeper, plant probe, hold replayer) -- every one
// a hypothesis that lost. What is kept from dih is marked "dih keep" and is additive
// only: it can widen a search, never narrow one.
//
// THE THREE BODIES AND WHAT EACH ONE PINS (do not mix these up):
//   TextureBug  wall seam, vz clipped by the brush TOP  -> pin lands on -halfgrav
//   HeadSurf    flat brush bottom ridden from below     -> pin lands on +halfgrav
//   HeadBounce  crease between two tilted faces, one    -> the CUT off free fall,
//               tick only, never a ride                    any size (123rd)
// ============================================================================

// CLEAN PIN CANDIDATE (coldtires 106th). Committing the FIRST pin let a steep variant
// keeping xy 1.2 (park-grade glue) beat a graze keeping 220, purely on evaluation order.
// The first pin stays available -- the comparator only swaps in a strictly better one
struct TbPin_t
{
    bool  have = false;
    float fwd = 0.f, side = 0.f;
    int   buttons = 0;
    float xy = 0.f;         // predicted xy the pin kept -- the ride-quality number
    float in_fwd = 0.f;     // REQUESTED forward (pre-CorrectMovement); warm start replays it
    float support = 0.f;
    float angle_off = 0.f;  // signed off the wall normal; warm start replays it
    bool  duck = false;
    bool  keeps_xy = false; // kept >= k_tbXyKeepFrac of the xy this tick started with
};

// SPEED PRESERVATION (107th). A pin only had to keep xy >= 1 u/s to be clean, so braking
// 220 -> 3 ranked the same as carrying 218. Fraction of the tick's ENTRY xy, so it means
// the same on a 50 u/s creep and a 250 u/s bhop.
// RANKING ONLY, NEVER A VETO -- a wallbug pin is still committed when nothing cleaner
// exists (found_stuck_pin). LAW 1 stands
static constexpr float k_tbXyKeepFrac = 0.85f;
// creep hard-brake veto: a micro-move that eats half your speed is not a hold
static constexpr float k_tbXyCreepFrac = 0.5f;
// 113th -- RIDE ticks only: the wish tangent points AGAINST the slide, so the bleed is
// 10*dt*fwd -- fwd 15 flush is ~2.25 u/tick and the ride grinds to a stop, fwd <= 1 is
// ~0.15 u/tick = free. An APPROACH still needs the big forwards to close the gap
static constexpr float k_tbCreepFwdMax = 1.f;
// ...unless the sim says the variant kept this much of the tick's entry xy (u/s)
static constexpr float k_tbCreepXyLoss = 0.5f;
// closing rungs: land the hull at these fractions of the gap left (see the solve pass)
static constexpr float k_tbCloseQ[] = { 0.02f, 0.1f, 0.35f };
static constexpr int   k_tbCloseN   = (int)(sizeof(k_tbCloseQ) / sizeof(k_tbCloseQ[0]));

// 106b: rank SMALLEST FORWARD first (least disturbance = the ride), xy only breaks
// near-equal forwards. Inside pass 1 this is the old first-pin pick; it only bites when the
// first pin came out of the solve or fan pass with a big forward
static bool TB_PinBetter(const TbPin_t& a, const TbPin_t& b)
{
    if (a.keeps_xy != b.keeps_xy) return a.keeps_xy;          // speed first
    if (fabsf(a.in_fwd - b.in_fwd) > 1e-4f) return a.in_fwd < b.in_fwd;
    if (fabsf(a.xy - b.xy) > 0.5f) return a.xy > b.xy;
    return a.support < b.support;
}

// variants the sweep may still evaluate AFTER a pin is in hand, purely to out-score it.
// Bounded so a pin-rich wall can never trade the old early exit for a sim spike
static constexpr int k_tbPinRefine = 16;

// SIMS THE BLIND GRID MAY NOT SPEND. The grid is 13 forwards x 5 grazing angles of
// guesswork; the solve pass inverts AirAccelerate for the forward the PAPER window asks
// for. AirStuck had the same shape: 866 of 1037 ticks drained the pool and stuck 0.0% of
// them, the 171 with room left hit 19.3%. The analytic pass must never be the one starved
static constexpr int k_tbSolveReserve = 30;   // 26 -> 30, +3 closing rungs
// and its own ceiling, so a six-rung target ladder cannot turn into a sim spike
static constexpr int k_tbSolveCap = 30;       // 22 -> 30, same reason
// ride "coyote time": ticks the HOLD state (duck latch, plane lock) survives a miss -- one
// no-pin tick uncrouching us mid-surf was ride over.
// dih keep: 12, not coldtires' 6. Grace holds STATE only, it never fakes a pin
static constexpr int k_tbRideGrace = 12;
// 107th -- one steer-away tick no longer ends a ride: mid-surf half of every strafe cycle
// points off the wall. RIDE ticks only, so an approach cannot pre-charge it
static constexpr int k_tbSteerRunEnd = 2;
// creep hugs from twice as far out while the last tick pinned -- the gap the hold has to
// close is what ends rides, and only creep ticks close it
static constexpr float k_tbHugRideScale = 2.f;
static constexpr bool  k_tbPressHold = false;   // P/Q analytic press branch, off for the creep A/B
// retry budget on a ride tick. Cap 8 ran out on warm + tiny fwds
static constexpr int   k_tbRideRetries = 32;
// 116th -- pinned ticks in a row after which a ride counts as PROVEN and the smash guard
// stands down. On a ride that has held, riding the pin is the better bet
static constexpr int k_tbRideSettled = 3;
// ...but the stand-down is capped by SPEED. All 6 logged velocity collapses (xy_base
// 195..307 -> 0.00 next tick) happened at rt 20..71 with the stand-down active. The guard
// only costs speed where there is little to lose, so above this a probe saying the next
// tick zeroes velocity is worth believing even 70 ticks into a ride
static constexpr float k_tbSmashGuardXy = 50.f;
// how far off the half-gravity target a CROUCH pin may drift over a 2-tick probe and still
// latch. A holding pin reads ~0 every tick; one that lost the seam is two gravity steps
// down (~24) by tick 2, so 12 splits them with a step of margin either way
static constexpr float k_tbDuckConfirmDz = 12.f;

// 118th PARK BRIDGE. A wallbug tick costs the WHOLE xy (its own gate is xy < 1), so parking
// while the ride still carries speed trades a 240 u/s surf for a 0 u/s hang.
// NOT /47/48/49/53: those RELEASED on dead xy and dropped the user off the wall. Dead xy
// still parks -- the law is "a texturebug ride sits at xy 0, it is the destination". What
// is refused is parking at 272 u/s
static constexpr float k_tbParkXyMax  = 30.f;
// the 2-tick cap was the biggest ride killer: 135 of 214 logged ride ends (63%) were flush,
// supported, xy 0.00, a wallbug pin in hand, and the only failing park condition was
// s_tbParkRun < 2. That is the park expiring on the exact state it is FOR. The xy gate
// above is untouched, so 118th's real point -- never park a 272 u/s surf -- still stands
static constexpr int   k_tbParkRunMax = 2;   // used when g_tbParkAtEnd is off
// ...and peel ticks in a row: a peel that runs on is TB hovering beside a wall it left
static constexpr int k_tbEscRunMax = 3;
static int s_tbParkRun = 0;

// face-walk cache. ~30 rays, and the answer only moves when the plane does, so it is taken
// once per plane per ride and re-taken on a refresh timer
static bool  s_tbFaceValid  = false;
static float s_tbFaceTopZ   = 0.f;
// ...and the BOTTOM of the same face. The pair gives H_brush, what PAPER (10) asks for
static bool  s_tbFaceBotOk  = false;
static float s_tbFaceBotZ   = 0.f;
static Vec3  s_tbFaceNormal{};
static float s_tbFacePlaneD = 0.f;
static float s_tbFaceOrgZ   = 0.f;   // origin z the walk was taken at
static int   s_tbFaceTick   = -1000;
// the edge the runout found, in WORLD space: it does not move, we do, so later ticks
// measure the distance to it instead of re-walking the face
static bool  s_tbEdgeValid  = false;
static Vec3  s_tbEdgePos{};
static int   s_tbEdgeTick   = -1000;
// last live ride tangent. At the end of a brake xy is ~0 and the tangent undefined, but
// the direction we were travelling is what the brake needs
static Vec3  s_tbRideTan{};
static bool  s_tbRideTanValid = false;
static int s_tbEscRun  = 0;

// dih keep RIDE RESCUE. Both scans run at the CATCH reach (g_tbReach, default 2), so a ride
// that drifted a few units off the face reads "no wall" and dies with grace in hand.
// ADDITIVE: one extra widened scan, grace ticks only, ride plane first (108th)
static constexpr float k_tbRideReachScale = 3.f;
static constexpr float k_tbRideReachMin   = 8.f;

// 119th, ceiling side: how far a head-surf brush bottom may sit off flat (-1.00). Anything
// slacker is a ramp underside and HeadSurf may not lock a cmd to one
static constexpr float k_hsRampNz = 0.05f;

// HeadBounce: ticks one variant is held for. 3 = ~1u of steering at bhop speed; 123rd
// sizes the chain off the notch, capped at 6
static constexpr int   k_hbChain     = 3;
static constexpr int   k_hbChainMax  = 6;
static constexpr float k_hbGapMargin = 2.f;  // probe traces from the cmd END, sims start at its base
// how much rise something overhead must eat to count as a bounce. A wish cannot change vz,
// so free fall is a KNOWN number and anything under it is contact -- 1 u/s is well below
// the ~2.9 u/s a crease clip costs and far above float noise
static constexpr float k_hbCutMin = 1.f;
// 40 (coldtires) -> 160. The grid is 3 fwd x 14 yaw x chain 6 = 252 sims worst case and
// every HB log ended `sims=30/30` after FIVE variants. The pool is 384 and TbBudgetExpired
// is the real frame guard, so the count must not truncate the only pass that finds a bounce
static constexpr int k_hbSimCap = 160;

// ---- TB phase, for the on-screen indicator (hud/indicators.cpp, which has its own labels) ----
enum TbPhase_t { TBP_OFF = 0, TBP_SCAN, TBP_HUG, TBP_ARM, TBP_PIN, TBP_RIDE, TBP_PARK };
static TbPhase_t s_tbPhase   = TBP_OFF;
static int   s_tbRideLen = 0;   // pinned ticks in the current ride
static float s_tbIndD    = 0.f; // band_max  -- PAPER (10) window ceiling
static float s_tbIndVin  = 0.f; // v_in_now  -- live speed INTO the plane
static float s_tbIndAim  = 0.f; // press_target -- where the hold aims inside the band

void TB_GetInd(int& phase, int& rideLen, int& seams, float& D, float& vin, float& aim)
{
    phase = (int)s_tbPhase; rideLen = s_tbRideLen; seams = 0;
    D = s_tbIndD; vin = s_tbIndVin; aim = s_tbIndAim;
}

// diag throttles (DiagOk), one per body
static int s_tbDiagTick = -1000;
static int s_hsDiagTick = -1000;
static int s_hbDiagTick = -1000;

// ============================================================================
// TEXTURE BUG, cumidere style -- 1:1 port of Documents\!\cumidere tf2.dll (seo TF2, debug
// build), read with botox tools\sigtool.py + Ghidra:
//   FUN_1801cbee0  body          FUN_1801cb3c0  wall scan       FUN_1801cd340  gap score
// cumidere calls it BEFORE its own engine prediction; our pipeline calls it after, so the
// whole thing works off the restore base (= its pre-prediction state).
// Engine numbers only: 16 dirs, 10 forwards x 5 grazes = 50 sims max, accept vz on +-half
// gravity, else the flushest variant if its gap is under 0.2u.
// ============================================================================
static float s_tbcScanStart = 0.f;   // DAT_1804d6f30: direction the last wall was found at
static int   s_tbcDiagTick  = -1000;
static int   s_tbcRun       = 0;     // consecutive pinned ticks

// FUN_1801cb3c0. Line traces every 1u of hull height, centre out to the hull edge + 2 along
// each of 16 dirs starting at `start`; first dir with a facing near-vertical face wins, its
// closest hit is the wall. The two hull traces are exact supersets of the lines (perf only):
// every line lies inside the box, and the zero-xy column covers every line of one dir
static bool TBC_FindWall(void* pLocal, const Vec3& org, const Vec3& mins, const Vec3& maxs,
                         float start, GameTrace_t& out, float& out_angle)
{
    const float range = MV_PI * 2.f, step = MV_PI / 8.f;
    const float ex = fmaxf(fabsf(maxs.x), fabsf(mins.x));
    const float ey = fmaxf(fabsf(maxs.y), fabsf(mins.y));
    TraceFilterWorldProps fil; fil.Setup(pLocal);

    GameTrace_t box{};   // |line end| <= ex + 2 on x (ey + 2 on y); +1 slack for touch
    TraceHull(org, org, Vec3(-ex - 3.f, -ey - 3.f, mins.z - 1.f), Vec3(ex + 3.f, ey + 3.f, maxs.z + 1.f),
              MASK_PLAYERSOLID, &fil, &box);
    if (!box.DidHit()) return false;

    for (float a = start; a < start + range; a += step)
    {
        const float r = fmodf(a, range);
        const float c = cosf(r), s = sinf(r);
        const float reach = fminf(ey / fmaxf(fabsf(s), 1e-6f), ex / fmaxf(fabsf(c), 1e-6f)) + 2.f;
        const Vec3  end(org.x + c * reach, org.y + s * reach, org.z);

        GameTrace_t col{};
        TraceHull(org, end, Vec3(0.f, 0.f, mins.z), Vec3(0.f, 0.f, maxs.z), MASK_PLAYERSOLID, &fil, &col);
        if (!col.DidHit()) continue;

        bool hit = false;
        GameTrace_t best{};
        best.fraction = 1.f;
        for (float z = org.z + mins.z; z <= org.z + maxs.z; z += 1.f)
        {
            GameTrace_t tr{};
            TraceLine(Vec3(org.x, org.y, z), Vec3(end.x, end.y, z), MASK_PLAYERSOLID, &fil, &tr);
            if (tr.fraction >= 1.f) continue;
            if (fabsf(tr.plane.normal.z) > 0.35f) continue;
            if (tr.plane.normal.x * c + tr.plane.normal.y * s > -0.1f) continue;
            hit = true;
            if (tr.fraction < best.fraction) best = tr;
        }
        if (hit) { out = best; out_angle = r; return true; }
    }
    return false;
}

// FUN_1801cd340: hull face -> wall gap after a sim. One line at the wall hit's z, 32 along
// -normal, fraction * 32 minus the hull support. 1:1 incl. the quirk that the ray is 32*|n.xy|
// long but scored as 32 (identical on vertical walls, the only ones the scan accepts near)
static float TBC_Gap(void* pLocal, const GameTrace_t& wall)
{
    const Vec3 o = EntV3(pLocal, g_offOrigin);
    const Vec3 n = wall.plane.normal;
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    GameTrace_t tr{};
    TraceLine(Vec3(o.x, o.y, wall.endpos.z), Vec3(o.x - n.x * 32.f, o.y - n.y * 32.f, wall.endpos.z),
              MASK_ALL_MV, &fil, &tr);
    return tr.fraction * 32.f - TB_WallSupportDistance(pLocal, Vec3(n.x, n.y, 0.f));
}

// ---- cumidere misc_auto_align = the RIDE half of its texture bug ---------------------------
// cumidere's createmove (FUN_1801f9b10) runs FUN_1801d0170 (auto align) a few features before
// FUN_1801cbee0 (tb), both on the pre-prediction state. TB finds a pin, align is what keeps one:
// into-wall press with the least force that pins, and while pinned (base vz = -halfgrav) one held
// WASD key steers along the wall at up to 450 and a 450 press tilted atan(15..25/speed) off the
// wall normal gains speed (air accel caps the wish at 30, so vel.wish ~= 15..25 leaves 5..15 u/s
// of accel the wall does not eat). Every move is re-proven pinned by a 1-tick sim.

// FUN_1801d1a80: origin x or y sits 0.0075..0.03125u off a whole unit
static bool TBC_NearPixel(const Vec3& o)
{
    const auto near_px = [](float v)
    {
        const float f = v - floorf(v), g = 1.f - f;
        return (g >= 0.0075f && g <= 0.03125f) || (f >= 0.0075f && f <= 0.03125f);
    };
    return near_px(o.x) || near_px(o.y);
}

// FUN_1801cfca0: 16 hull traces 2u out, the first that hits a dead-vertical face wins. Zero-xy
// normal refused (start solid), cumidere would steer toward yaw 0 on it
static bool TBC_AlignWall(void* pLocal, const Vec3& org, const Vec3& mins, const Vec3& maxs, Vec3& n)
{
    TraceFilterWorldProps fil; fil.Setup(pLocal);
    for (int i = 0; i < 16; ++i)
    {
        const float a = float(i) * 0.3926991f;
        GameTrace_t tr{};
        TraceHull(org, Vec3(org.x + cosf(a) * 2.f, org.y + sinf(a) * 2.f, org.z), mins, maxs,
                  MASK_PLAYERSOLID, &fil, &tr);
        if (tr.fraction != 1.f && tr.plane.normal.z == 0.f
            && (tr.plane.normal.x != 0.f || tr.plane.normal.y != 0.f))
            { n = tr.plane.normal; return true; }
    }
    return false;
}

// TBC_Align result bits, for the TBC: log
enum { TBCA_WALL = 1, TBCA_PRESS = 2, TBCA_PINNED = 4, TBCA_STEER = 8, TBCA_GAIN = 16 };

// FUN_1801d0170. Entity must sit on the restore base; it is left there
static int TBC_Align(void* pLocal, UserCmd_t* pCmd, int nBase)
{
    const auto Restore = [&]() { RestoreToFrame(nBase - 1); };
    const Vec3 org  = EntV3(pLocal, g_offOrigin);
    const Vec3 mins = EntV3(pLocal, g_offMins);
    const Vec3 maxs = EntV3(pLocal, g_offMaxs);
    Vec3 n{};
    if (!TBC_AlignWall(pLocal, org, mins, maxs, n)) return 0;
    int res = TBCA_WALL;

    const Vec3  vel    = EntV3(pLocal, g_offVelocity);
    const float speed  = vel.Length2D();
    const float half   = TargetPredictZVel();   // cumidere hardcodes -6
    // cumidere: 0.01. Valve Oct 2026 ClipVelocity subtracts DIST_EPSILON, so a top-plane pin now
    // ends at -6 + 0.03125 and 0.01 never fires on Valve servers (sv_wallbug 0)
    const auto  pin    = [&](float vz) { return fabsf(vz - half) < 0.05f; };
    const bool  pinned = pin(vel.z);
    const float user_fwd = pCmd->forwardmove, user_side = pCmd->sidemove;
    const float view_yaw = pCmd->viewangles.y;
    const int   b        = pCmd->buttons;

    const float into_yaw = VectorAngles(Vec3(-n.x, -n.y, 0.f)).y;
    const float rel = DEG2RADF(NormalizeAngle(into_yaw - view_yaw));
    const float c = cosf(rel), s = sinf(rel);

    // one tick of pCmd with (f, sd); out of budget = a miss (no pin, no speed)
    const auto sim = [&](float f, float sd) -> Vec3
    {
        if (SimsLeft() <= 0) return Vec3(0.f, 0.f, 1e30f);
        UserCmd_t fake = *pCmd;
        fake.forwardmove = f; fake.sidemove = sd;
        Restore();
        Pred_Simulate(pLocal, &fake);
        const Vec3 v = EntV3(pLocal, g_offVelocity);
        Restore();
        return v;
    };

    // press straight in, 0..100: least force that pins, else 10
    float best_f = c * 10.f, best_s = -s * 10.f;
    for (float m = 0.f; m <= 100.f; m += 10.f)
        if (pin(sim(c * m, -s * m).z)) { best_f = c * m; best_s = -s * m; res |= TBCA_PRESS; break; }
    pCmd->forwardmove = best_f;
    pCmd->sidemove    = best_s;
    // off a pixel offset, not riding and steering: the player's own move
    if (!TBC_NearPixel(org) && !pinned && (b & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT)))
        { pCmd->forwardmove = user_fwd; pCmd->sidemove = user_side; }
    if (!pinned) return res;
    res |= TBCA_PINNED;

    // riding: exactly one WASD key held
    float kyaw = 0.f;
    bool  key  = false;
    if ((b & IN_FORWARD)   && !(b & (IN_BACK | IN_MOVELEFT | IN_MOVERIGHT)))    { kyaw = view_yaw;         key = true; }
    if ((b & IN_BACK)      && !(b & (IN_FORWARD | IN_MOVELEFT | IN_MOVERIGHT))) { kyaw = view_yaw + 180.f; key = true; }
    if ((b & IN_MOVELEFT)  && !(b & (IN_FORWARD | IN_BACK | IN_MOVERIGHT)))     { kyaw = view_yaw + 90.f;  key = true; }
    if ((b & IN_MOVERIGHT) && !(b & (IN_FORWARD | IN_BACK | IN_MOVELEFT)))      { kyaw = view_yaw - 90.f;  key = true; }
    if (!key) return res;

    TraceFilterWorldProps fil; fil.Setup(pLocal);
    GameTrace_t tr{};
    const float kr = DEG2RADF(kyaw);
    TraceLine(org, Vec3(org.x + cosf(kr) * 128.f, org.y + sinf(kr) * 128.f, org.z), MASK_PLAYERSOLID, &fil, &tr);
    if (tr.fraction < 1.f)
    {
        // the held key's axis 0..450, the other axis keeps the press; the LAST pinned step wins
        float kf = best_f, ks = best_s;
        for (int k = 0; k < 451; k += 45)
        {
            const float f  = (b & IN_FORWARD)  ? float(k)  : (b & IN_BACK)      ? -float(k) : kf;
            const float sd = (b & IN_MOVELEFT) ? -float(k) : (b & IN_MOVERIGHT) ? float(k)  : ks;
            if (pin(sim(f, sd).z)) { kf = f; ks = sd; res |= TBCA_STEER; }
        }
        pCmd->forwardmove = kf;
        pCmd->sidemove    = ks;

        // fast and not gaining: 450 tilted a hair off the wall normal toward the drift
        const float speed2 = sim(kf, ks).Length2D();
        if (speed > 250.f && fabsf(speed - speed2) < 5.f)
        {
            float gain = 0.f, gf = kf, gs = ks;
            const float drift = NormalizeAngle(VectorAngles(Vec3(vel.x, vel.y, 0.f)).y - into_yaw);
            for (float a = 15.f; a < 30.f; a += 5.f)
            {
                const float ang = RAD2DEGF(atanf(a / speed));
                const float r   = DEG2RADF(NormalizeAngle(into_yaw + (drift < 0.f ? -ang : ang) - view_yaw));
                const float f = cosf(r) * 450.f, sd = -sinf(r) * 450.f;
                const Vec3  v = sim(f, sd);
                if (pin(v.z) && v.Length2D() - speed > gain) { gain = v.Length2D() - speed; gf = f; gs = sd; }
            }
            if (gain > 0.f) { pCmd->forwardmove = gf; pCmd->sidemove = gs; res |= TBCA_GAIN; }
        }
    }
    // never push against the held key
    if (pCmd->forwardmove < 0.f && (b & IN_FORWARD))   pCmd->forwardmove = 450.f;
    if (pCmd->forwardmove > 0.f && (b & IN_BACK))      pCmd->forwardmove = -450.f;
    if (pCmd->sidemove    < 0.f && (b & IN_MOVERIGHT)) pCmd->sidemove    = 450.f;
    if (pCmd->sidemove    > 0.f && (b & IN_MOVELEFT))  pCmd->sidemove    = -450.f;
    return res;
}

// FUN_1801cbee0
static void TextureBugCumidere(void* pLocal, UserCmd_t* pCmd)
{
    s_textureBugHit = false;
    s_tbOwnsCmd = false;
    s_tbRideHold = 0;
    s_tbIndD = s_tbIndVin = s_tbIndAim = 0.f;   // botox-style band numbers, nothing here
    if (!EntAlive(pLocal)) { s_tbPhase = TBP_OFF; s_tbRideLen = s_tbcRun = 0; return; }

    const int  nBase = PredCommandsPredicted();
    const auto Restore = [&]() { RestoreToFrame(nBase - 1); };
    Restore();   // cumidere's view of the world: before this cmd ran

    if ((EntI(pLocal, g_offFlags) & FL_ONGROUND) || EntMoveType(pLocal) != MOVETYPE_WALK)
        { s_tbPhase = TBP_OFF; s_tbRideLen = s_tbcRun = 0; return; }

    const float user_fwd = pCmd->forwardmove, user_side = pCmd->sidemove;   // pre-align, for no-park
    const int al = g_tbcAlign ? TBC_Align(pLocal, pCmd, nBase) : 0;

    const Vec3 org  = EntV3(pLocal, g_offOrigin);
    const Vec3 mins = EntV3(pLocal, g_offMins);
    const Vec3 maxs = EntV3(pLocal, g_offMaxs);
    const Vec3 vel  = EntV3(pLocal, g_offVelocity);

    GameTrace_t wall{};
    float ang = 0.f;
    if (!TBC_FindWall(pLocal, org, mins, maxs, s_tbcScanStart, wall, ang))
        { s_tbcScanStart = 0.f; s_tbPhase = TBP_OFF; s_tbRideLen = s_tbcRun = 0; return; }
    s_tbcScanStart = ang;

    const float wall_yaw = NormalizeAngle(RAD2DEGF(atan2f(-wall.plane.normal.y, -wall.plane.normal.x)));
    const float vel_yaw  = RAD2DEGF(atan2f(vel.y, vel.x));
    // graze AGAINST the along-wall drift
    const float sign = NormalizeAngle(vel_yaw - wall_yaw) >= 0.f ? -1.f : 1.f;

    static constexpr float k_fwd[10] = { 1.f, 0.875f, 0.5f, 1.f, 1.3856f, 2.125f, 2.875f, 5.f, 15.f, 30.f };
    const float half = TargetPredictZVel();   // -6 at 66 tick / 800 gravity, cumidere hardcodes -6
    const Vec3  view = pCmd->viewangles;

    bool  found = false;
    float best_gap = 1.f;
    float out_fwd = pCmd->forwardmove, out_side = pCmd->sidemove, out_off = 0.f;
    int   sims = 0;
    float last_vz = 0.f;

    // NOT cumidere: a cmd that already pins (align's steer / speed move, or the player's own)
    // is kept. cumidere's grid overwrites it with fwd=1 at 86 deg, and its hug with a variant
    // that does not pin at all -- the steer and the speed gain never reached the server
    bool natural = false;
    if (SimsLeft() > 0)
    {
        UserCmd_t fake = *pCmd;
        Restore();
        Pred_Simulate(pLocal, &fake);
        ++sims;
        last_vz = EntV3(pLocal, g_offVelocity).z;
        natural = found = fabsf(last_vz - half) < 0.1f || fabsf(last_vz + half) < 0.1f;
    }

    for (int i = 0; i < 10 && !found; ++i)
    {
        for (float off = 85.999f; off < 90.f; off += 1.f)
        {
            if (SimsLeft() <= 0) break;
            UserCmd_t fake = *pCmd;
            fake.forwardmove = k_fwd[i];
            fake.sidemove    = 0.f;
            Vec3 wish = view; wish.y = wall_yaw + off * sign;
            CorrectMovement(&fake, wish, view);
            fake.viewangles = view;

            Restore();
            Pred_Simulate(pLocal, &fake);
            ++sims;
            const float vz  = EntV3(pLocal, g_offVelocity).z;
            const float gap = TBC_Gap(pLocal, wall);
            last_vz = vz;

            if (fabsf(vz - half) < 0.1f || fabsf(vz + half) < 0.1f)
            {
                found = true;
                out_fwd = fake.forwardmove; out_side = fake.sidemove; out_off = off;
                break;
            }
            if (gap < best_gap)
            {
                best_gap = gap;
                out_fwd = fake.forwardmove; out_side = fake.sidemove; out_off = off;
            }
        }
        if (SimsLeft() <= 0) break;
    }
    Restore();   // hand the entity back on the base, like the botox style does

    // NOT cumidere: botox style's "no off-grid stick". On a yaw-rotated wall a pin at park
    // speed (xy <= 30) and the hug just glue the hull into the clip, no seam to ride. Drop
    // both + align's press, player's own move back. A pin at speed still rides
    const bool off_grid = fminf(fabsf(wall.plane.normal.x), fabsf(wall.plane.normal.y)) > 0.10f;
    const bool no_park  = g_tbNoOffgridStick && off_grid
                       && (!found || vel.Length2D() <= k_tbParkXyMax);
    if (no_park)
    {
        found = false;
        best_gap = 1.f;
        pCmd->forwardmove = user_fwd;
        pCmd->sidemove    = user_side;
    }

    const bool hug = !found && best_gap < 0.2f;
    if (found || hug)
    {
        pCmd->forwardmove = out_fwd;
        pCmd->sidemove    = out_side;
    }
    s_textureBugHit = found;
    s_tbOwnsCmd     = hug;
    s_tbcRun        = found ? s_tbcRun + 1 : 0;   // own count: TextureBug's drop_ride zeroes s_tbRideLen
    s_tbRideLen     = s_tbcRun;
    s_tbPhase       = found ? (s_tbRideLen >= 2 ? TBP_RIDE : TBP_PIN) : hug ? TBP_HUG : TBP_SCAN;

    if (g_debugLogEnable && DiagOk(s_tbcDiagTick, found || hug || no_park))
        // al = TBC_Align bits: 1 wall, 2 press pinned, 4 riding, 8 key steer pinned, 16 speed gain
        DebugLog("TBC: pin=%d nat=%d al=%d hug=%d gap=%.3f off=%.3f fwd=%.3f side=%.3f vz=%.1f lvz=%.2f "
                 "sims=%d xy=%.0f ang=%.2f n=%.2f %.2f %.2f sign=%.0f ride=%d og=%d np=%d", (int)found,
                 (int)natural, al, (int)hug, best_gap, out_off, out_fwd, out_side, vel.z, last_vz, sims,
                 vel.Length2D(), ang, wall.plane.normal.x, wall.plane.normal.y, wall.plane.normal.z,
                 sign, s_tbRideLen, (int)off_grid, (int)no_park);
}

// ============================================================================
// TEXTURE BUG -- coldtires texture_bug(), ported whole
// ----------------------------------------------------------------------------
// The trick (PAPER 9.4.2): a box brush picks the side it collides with by the raw
// fraction f' = (s - eps)/|delta| BEFORE the max(0,..) clamp, highest wins. Wall-hugging
// puts s_perp inside the eps band, so the TOP side wins and clips vz, while
// |v_perp|/|vz| < eps/(D + eps). The body is that inequality, searched: every variant is
// one engine-simmed tick of AirAccelerate, and it "pins" when end-of-move vz lands on
// -halfgravity.
//
// Pass order, and what each one exists for:
//   natural  the player's own cmd already pins -> leave it alone (1 sim, fresh catch)
//   warm     replay last tick's winner (106th) -- a ride ends the tick in ONE sim
//   1 grid   fwd low-first x 86..90 deg graze, base sign (+ mirror on a fresh catch)
//   2 duck   the same grid on the crouch hull, only if standing missed everywhere
//   3 solve  INVERT AirAccelerate for the forward that puts v_in ON target (83rd); the
//            grid's 13 values are too sparse to land in the window
//   4 fan    steep angles, 10..84 deg (cumidere FUN_10044290) -- a bounce arrives near
//            head-on and the graze grid never tries the move that pins it
// then the commit ladder: pin > park > peel > press > creep > drive-in > hands back.
// ============================================================================

static void TextureBug(void* pLocal, UserCmd_t* pCmd)
{
    const bool caught_last_tick = s_textureBugHit;
    const bool owned_last_tick  = s_tbOwnsCmd;   // creep/press/drive: band setup, still on the wall
    s_textureBugHit = false;
    s_tbOwnsCmd = false;

    static float wall_scan_start = 0.f;
    static bool  duck_needed = false;        // a duck pass WON a sim -- hold it through this ride
    static Vec3  ride_normal{};              // plane this ride is pinned on (seam lock)
    static float ride_plane_d = 0.f;         // 108th -- and its offset, so a parallel wall cannot pass
    static bool  ride_locked = false;
    static bool  ride_had_clean_pin = false; // this ride began on a CLEAN pin = we really surfed
    static bool  s_warm_valid = false;       // 106th warm start -- the variant that pinned last time
    static float s_warm_fwd = 0.f, s_warm_angle = 0.f;
    static bool  s_warm_duck = false;
    static int   s_warm_tick = -1000;
    static int   s_ride_grace = 0;           // ticks the HOLD state survives a miss
    static int   s_steer_run  = 0;           // consecutive RIDE ticks the input pointed off the wall
    static int   s_ride_ticks = 0;           // consecutive ticks this ride actually PINNED

    // NOT s_tbParkRun / s_tbEscRun: the stuck branch calls this the moment a cap fires, so
    // zeroing them here would hand the next tick a fresh budget and the cap would never
    // bind. They expire off the ride instead (see `riding` below)
    const auto drop_ride = [&]()
    {
        duck_needed = false; ride_locked = false; ride_had_clean_pin = false;
        s_ride_grace = 0; s_steer_run = 0; s_ride_ticks = 0;
        s_tbPhase = TBP_OFF; s_tbRideLen = 0; s_tbRideHold = 0;
        // the measured face, its edge and the ride tangent all belong to ONE ride
        s_tbFaceValid = false; s_tbEdgeValid = false; s_tbRideTanValid = false;
    };

    // Unit A3 port: TUNG SURF, as an option of this catch rather than a second engine (botox
    // movement.cpp tung_surf is its own ~700-line sim/replay/learn machine, itself flagged
    // UNTESTED upstream and "low priority" per gap.md -- re-deriving a second engine was not
    // worth the risk). What tung surf actually buys over a plain texture bug is tolerance: it
    // attempts the SAME catch on a shallower face than TB's ramp_band normally hands off. Own
    // key, and it only widens the band when the real TB bind isn't already claiming the tick.
    const bool bTbOn   = On(g_textureBug, g_textureBugKey, g_textureBugMode);
    const bool bTungOn = !bTbOn && On(g_tungSurf, g_tungSurfKey, g_tungSurfMode);
    if (!bTbOn && !bTungOn)
        { wall_scan_start = 0.f; drop_ride(); return; }
    if (bTbOn && g_tbStyle == 0)
        { wall_scan_start = 0.f; drop_ride(); TextureBugCumidere(pLocal, pCmd); return; }
    if (!EntAlive(pLocal))                      { drop_ride(); return; }
    if (EntI(pLocal, g_offFlags) & FL_ONGROUND) { drop_ride(); return; }
    if (EntMoveType(pLocal) != MOVETYPE_WALK)   { return; }

    const bool  debug    = g_debugLogEnable;
    const int   tick_now = g_globalVars ? g_globalVars->tickcount : 0;
    const float tick_dt  = TickInterval();
    const float sv_airaccel = CvarFloat("sv_airaccelerate", 10.f);

    const Vec3  original_view    = pCmd->viewangles;
    const float original_forward = pCmd->forwardmove;
    const float original_side    = pCmd->sidemove;
    int         original_buttons = pCmd->buttons;

    // silent move along a world wish yaw, written into pCmd (the view never moves)
    const auto apply_wish = [&](float world_yaw, float fwd, bool duck)
    {
        pCmd->viewangles = original_view;
        pCmd->buttons    = original_buttons;
        if (duck) pCmd->buttons |= IN_DUCK;
        pCmd->forwardmove = fwd;
        pCmd->sidemove    = 0.f;
        Vec3 wish = original_view; wish.y = world_yaw;
        CorrectMovement(pCmd, wish, original_view);
        pCmd->viewangles = original_view;
    };

    const Vec3 start_pos = EntV3(pLocal, g_offOrigin);
    const Vec3 mins      = EntV3(pLocal, g_offMins);
    const Vec3 maxs      = EntV3(pLocal, g_offMaxs);
    const Vec3 vel_now   = EntV3(pLocal, g_offVelocity);

    // ================= 1. THE WALL =========================================
    GameTrace_t primary{};
    float found_angle = 0.f;
    bool  wall_found  = false;

    // riding: scan the ride plane FIRST -- an unlocked scan can hand back a seam neighbour,
    // flipping `sign` and aiming the sweep at the wrong plane (the random stop)
    const bool riding = caught_last_tick || owned_last_tick;
    // 118th -- park/peel budgets belong to a contact. Off the wall they expire, so a fresh
    // catch always starts with the full bridge
    if (!riding) { s_tbParkRun = 0; s_tbEscRun = 0; }
    // 106th -- grace covers the HOLD state only (duck latch + plane lock). Deliberately NOT
    // used for the steer-away bail or ride_had_clean_pin: both of those SUPPRESS search
    const bool hold_grace = riding || s_ride_grace > 0;

    // NO xy signal here, in any form. xy == 0 with vz == -half, flush, mindz 0 is the PARK
    // and the user WANTS it. Five builds died on "dead xy is a fault" (/47/48/49/53)
    const float tb_reach     = g_tbReach;
    const bool  had_lock     = ride_locked;
    const Vec3  lock_normal  = ride_normal;
    const float lock_plane_d = ride_plane_d;
    TbWallHit_t scan_fallback{};
    const bool  locked_scan  = hold_grace && ride_locked;
    if (locked_scan)
        wall_found = MV_DetectWallLines(pLocal, start_pos, mins, maxs, wall_scan_start,
                                        primary, found_angle, 0.35f, tb_reach, &vel_now,
                                        &ride_normal, &ride_plane_d, &scan_fallback);
    if (!wall_found)
    {
        ride_locked = false;
        // a failed locked pass already traced every direction the unlocked retry would check
        // and kept its first any-plane hit -- that IS the retry's answer, reuse it (120th)
        if (locked_scan)
        {
            if (scan_fallback.valid)
                { primary = scan_fallback.trace; found_angle = scan_fallback.angle; wall_found = true; }
        }
        else
            wall_found = MV_DetectWallLines(pLocal, start_pos, mins, maxs, wall_scan_start,
                                            primary, found_angle, 0.35f, tb_reach, &vel_now);
    }
    // dih keep RIDE RESCUE -- one widened scan, grace ticks only, ride plane first
    int rescue_scan = 0;
    if (!wall_found && hold_grace)
    {
        const float rescue_reach = fmaxf(tb_reach * k_tbRideReachScale, tb_reach + k_tbRideReachMin);
        TbWallHit_t rescue_fallback{};
        if (had_lock)
        {
            wall_found = MV_DetectWallLines(pLocal, start_pos, mins, maxs, wall_scan_start,
                                            primary, found_angle, 0.35f, rescue_reach, &vel_now,
                                            &lock_normal, &lock_plane_d, &rescue_fallback);
            if (wall_found) rescue_scan = 1;
        }
        if (!wall_found)
        {
            if (had_lock)
            {
                if (rescue_fallback.valid)
                    { primary = rescue_fallback.trace; found_angle = rescue_fallback.angle; wall_found = true; }
            }
            else
                wall_found = MV_DetectWallLines(pLocal, start_pos, mins, maxs, wall_scan_start,
                                                primary, found_angle, 0.35f, rescue_reach, &vel_now);
            if (wall_found) rescue_scan = 2;
        }
    }

    // 83rd diag: the two silent exits (no wall in reach, steering away) left no log line, so
    // a spot that "never catches" looked identical to one TB never saw
    if (debug)
    {
        static bool s_had_wall = false;
        if (wall_found != s_had_wall)
        {
            s_had_wall = wall_found;
            DebugLog("TB: wall=%d reach=%.1f rescue=%d xy=%.0f vz=%.1f",
                     (int)wall_found, tb_reach, rescue_scan, vel_now.Length2D(), vel_now.z);
        }
    }

    const float flTbBand = g_tbRampNz > 0.f ? g_tbRampNz : 0.05f;
    // TUNG: widen the hand-off band so a shallow face TB would normally refuses gets the same
    // catch attempt. TungRampBand (movement_tricks2_pure.h) clamps it above the real TB band so
    // tung never gets STRICTER than the feature it borrows from -- gate-tested
    const float ramp_band = TungRampBand(bTungOn, g_tungMaxNz, flTbBand);
    if (bTungOn && fabsf(primary.plane.normal.z) > flTbBand && fabsf(primary.plane.normal.z) <= ramp_band)
        DebugLog("TUNG: relaxed-band attempt nz=%.3f band=%.3f (tb band=%.3f)",
                 primary.plane.normal.z, ramp_band, flTbBand);

    // ================= 1a. RAMP / DISPLACEMENT = HANDS OFF ==================
    // coldtires texturebug.cpp:427-450 returns outright when the scanned plane is tilted past
    // ramp_nz_band(). Why a tilt is glue and not a catch: contact on a ramp clips vz to the
    // along-plane part, so vz crosses -halfgrav constantly and the sweep reads a CLEAN PIN on
    // a face that can never hold one. Committing it hands TB the cmd, and on a ramp the
    // strafe IS the speed (log: nz=0.21, nz=0.16; every real brush wall read 0.00/0.02).
    // Displacements go with them: a texture bug is a BRUSH-PLANE artifact, a displacement is
    // triangle soup with a plane per facet, so TB_FaceSame fails at the first 8u rung -- D
    // and the runout come back tiny and the brake parks the ride dead. The band rejects
    // nearly all of them anyway, but the flag is already in the trace.
    // pCmd is untouched here, so returning preserves the player's own move exactly
    if (wall_found && (fabsf(primary.plane.normal.z) > ramp_band
                       || (primary.dispFlags & DISPSURF_SURFACE)))
    {
        if (debug)
        {
            // 120th: per second, not once per session -- a carve-out that eats every catch on
            // a map has to be visible in the log it ate them from
            static int s_ramp_ticks = 0, s_ramp_sec = -1;
            ++s_ramp_ticks;
            const int sec = g_globalVars ? (int)g_globalVars->curtime : 0;
            if (sec != s_ramp_sec)
            {
                s_ramp_sec = sec;
                DebugLog("TB: ramp hands-off nz=%.3f band=%.3f disp=%d ticks=%d xy=%.0f vz=%.1f",
                         primary.plane.normal.z, ramp_band,
                         (int)((primary.dispFlags & DISPSURF_SURFACE) != 0), s_ramp_ticks,
                         vel_now.Length2D(), vel_now.z);
                s_ramp_ticks = 0;
            }
        }
        drop_ride();
        return;
    }

    // ================= 1b. PREDICTIVE INTERCEPT =====================
    // Runs BEFORE the no-wall return on purpose: the ledges this exists for are the ones the
    // live scan never reports, or reports for one tick at 27u. Owns the cmd only while a real
    // gap is left, then hands the last fraction of a unit to the sweep, which does the pinning
    float live_gap = 1e9f;
    if (wall_found) live_gap = TB_PlaneGap(primary.plane, start_pos, mins, maxs);
    // `riding` is safe to gate on here because the approach takes neither s_textureBugHit nor
    // s_tbOwnsCmd: an approach tick leaves `riding` false for the next one and cannot switch
    // itself off, while a live ride (pin, creep, press, park) keeps its own cmd untouched
    if (g_tbPredict && !riding && live_gap > k_tbPredArrive)
    {
        bool pred_owns = false;
        // the plane does not move, so a scan is reusable and only gap/vin have to be fresh --
        // plane arithmetic, zero traces. Expire on ticks AND on fall distance: an absolute
        // world number cached across a fall goes wrong silently
        static TbPred_t s_pred{};
        static int      s_predTick = -1000;
        static float    s_predOrgZ = 0.f;
        static int      s_predSeamLeft = -1;   // last tick's countdown, for the cadence below

        int horizon = g_tbPredictTicks;
        if (horizon < 8)   horizon = 8;
        if (horizon > 128) horizon = 128;
        const float p_reach = g_tbPredictReach > 1.f ? g_tbPredictReach : 1.f;

        // cadence is on the CLOCK, never on `!s_pred.ok`: finding nothing is the common case,
        // and rescanning every tick runs the probes -- the expensive half -- flat out for the
        // whole flight. But 4 ticks is the whole miss window on a short ledge (1-2 tick
        // contact episodes), so rescan every tick once the crossing is inside the lead, the
        // only stretch where staleness costs the catch
        const int pred_refresh = (s_pred.ok && s_predSeamLeft >= 0
                                  && s_predSeamLeft <= k_tbPredLead * 2)
                               ? 1 : k_tbPredRefresh;
        if (tick_now - s_predTick >= pred_refresh
            || fabsf(start_pos.z - s_predOrgZ) > k_tbPredOrgDrift)
        {
            TB_PredictLedge(pLocal, start_pos, vel_now, mins, maxs,
                            horizon, p_reach, ramp_band, s_pred);
            s_predTick = tick_now;
            s_predOrgZ = start_pos.z;
        }

        if (s_pred.ok)
        {
            const Vec3& pn   = s_pred.plane.normal;
            const float gap  = TB_PlaneGap(s_pred.plane, start_pos, mins, maxs);
            const float vin  = -(vel_now.x * pn.x + vel_now.y * pn.y);
            const float tanx = vel_now.x + pn.x * vin, tany = vel_now.y + pn.y * vin;
            const float tang = sqrtf(tanx * tanx + tany * tany);

            // RE-SOLVE THE CROSSING EVERY TICK. seam_tick is a countdown, and a cached one is
            // wrong by exactly the ticks it was cached for
            int   seam_left = -1;
            float z = start_pos.z, vz = vel_now.z;
            const float halfg = TB_HalfGravityPerTick();
            for (int i = 1; i <= horizon * k_tbPredSeamMul; ++i)
            {
                vz += halfg; z += vz * tick_dt; vz += halfg;
                if (s_pred.seam_z >= z + mins.z && s_pred.seam_z <= z + maxs.z) { seam_left = i; break; }
                if (z + maxs.z < s_pred.seam_z) break;
            }
            s_predSeamLeft = seam_left;   // the cadence above reads this next tick

            // the tick we AIM to be flush on -- a few before the seam, so the contact is the
            // 6+ tick episode (57% pin) not the 1-2 tick one (0/25). The GATE below still
            // uses seam_left: that is the real deadline
            int aim_left = seam_left - k_tbPredLead;
            if (aim_left < 1) aim_left = 1;

            // CAN WE EVEN MAKE IT? Against a flat 30 u/s the answer was NO on 43 of 43 logged
            // ticks: that gate passes a 1.45u gap at left=1 and 5.5u at left=10, while the
            // scan reaches 24 and returns gaps of 7-120 -- the whole population rejected. The
            // engine caps the GAIN at 30, never the carried speed (a flight closing at 104
            // u/s does cover 14u in 9 ticks), so the rate is max(vin, 30) with 30 the floor
            const float close_rate = fmaxf(vin, k_tbPredVinCap);
            const bool reachable = seam_left > 0
                                && gap <= close_rate * (float)seam_left * tick_dt + 1.f;
            const bool gone = gap <= k_tbPredArrive || seam_left < 0;

            if (!gone && reachable)
            {
                // ARRIVE ON THE AIM TICK. A flat 30 target BRAKES a flight already closing
                // fast: gap 14 / left 9 / vin 104 is reachable, and 30 throws 74 u/s away at
                // fwd 450, then needs 27 ticks to walk a 9-tick window -- the controller
                // destroying the reach the gate certified. `need` is the rate that puts the
                // hull on the face at aim_left; 30 stays the floor because under it the taper
                // is free. Clamped to the one-tick close, so it never overshoots
                const float need   = (gap - k_tbPredArrive) / ((float)aim_left * tick_dt);
                // ---- AND IT MUST BE BRAKEABLE ------------------------------------------
                // log: `gap=11.36 vin=286.90 want=740.48 fwd=450 dir=in` -- arriving at 287
                // u/s perpendicular, still pushing IN, against a ~0.03 u/s paper band. Cause:
                // once seam_left <= k_tbPredLead, aim_left pins to 1 and `need` becomes "close
                // the whole gap in ONE tick", so the lead turns violent exactly when it binds.
                // The addspeed cap is 30 u/s a tick both ways, so braking from v costs
                // dt*(v^2/2A + v/2) of room; the largest v that fits is the positive root of
                // v^2 + A*v - 2A*(gap - arrive)/dt = 0, times 0.7 for the discrete ramp (same
                // law as v_enter_brake). At gap 11.36 the cap is 139, so dv goes NEGATIVE.
                const float pred_room  = fmaxf(gap - k_tbPredArrive, 0.f);
                const float pred_brake = 0.7f * 0.5f
                    * (sqrtf(k_tbPredVinCap * k_tbPredVinCap
                             + 8.f * k_tbPredVinCap * pred_room / tick_dt) - k_tbPredVinCap);
                const float v_want = fminf(fminf((gap - k_tbPredArrive) / tick_dt,
                                                 fmaxf(k_tbPredVinCap, need)),
                                           pred_brake);
                const float dv     = v_want - vin;
                // sv_airaccelerate * fwd * dt = the delta; invert for the forward that asks
                // exactly dv. The engine caps at addspeed anyway, so overshoot is free
                const float per_fwd = fmaxf(sv_airaccel * tick_dt, 1e-4f);
                const float fwd     = fminf(fabsf(dv) / per_fwd, k_tbPredFwdCap);
                // wish along +-plane normal ONLY, so the tangential velocity is never in the
                // wish and LAW 1 holds on the inward arm. The outward arm spends xy, but only
                // the part the wall was going to take anyway
                const Vec3  in_dir(-pn.x, -pn.y, 0.f);
                Vec3 wish_ang = VectorAngles(dv >= 0.f ? in_dir : pn);
                wish_ang.y = NormalizeAngle(wish_ang.y);

                if (fwd > 0.5f)
                {
                    apply_wish(wish_ang.y, fwd, false);
                    pred_owns = true;
                    s_tbPhase = TBP_ARM;
                    s_tbRideLen = 0;
                }
                if (debug && DiagOk(s_tbPredDiagTick, fabsf(dv) > 1.f))
                    DebugLog("TB PRED: gap=%.2f vin=%.2f want=%.2f need=%.2f rate=%.1f dv=%.2f "
                             "fwd=%.0f dir=%s seam=%.1f left=%d aim=%d hit=%d tan=%.0f xy=%.0f "
                             "vz=%.1f nz=%.2f faceh=%.1f tr=%d pb=%d p0=%d p1=%d",
                             gap, vin, v_want, need, close_rate, dv, fwd,
                             dv >= 0.f ? "in" : "out", s_pred.seam_z - start_pos.z,
                             seam_left, aim_left, s_pred.hit_tick,
                             tang, vel_now.Length2D(), vel_now.z, pn.z, s_pred.face_h,
                             s_pred.traces, s_pred.probes, s_pred.first, s_pred.last);
                if (pred_owns) return;     // approach owns the tick; the sweep gets it at `arrive`
            }
            else
            {
                // the s_pred.ok = false below is NEVER inside the debug arm: a dead answer
                // that only expires when diagnostics are on behaves differently in the log.
                // Print `need` vs `rate` side by side -- need > rate is the only honest
                // unreachable, and a skip line without `vin` cannot say why the gate failed
                if (debug && DiagOk(s_tbPredDiagTick, false))
                    DebugLog("TB PRED: skip gap=%.2f vin=%.2f rate=%.1f need=%.1f left=%d aim=%d "
                             "reach=%d gone=%d seam=%.1f tan=%.0f xy=%.0f vz=%.1f nz=%.2f tr=%d "
                             "pb=%d p0=%d p1=%d",
                             gap, vin, close_rate,
                             (gap - k_tbPredArrive) / ((float)aim_left * tick_dt),
                             seam_left, aim_left, (int)reachable, (int)gone,
                             s_pred.seam_z - start_pos.z, tang, vel_now.Length2D(),
                             vel_now.z, pn.z, s_pred.traces,
                             s_pred.probes, s_pred.first, s_pred.last);
                s_pred.ok = false;         // re-scan next tick rather than sit on a dead answer
            }
        }
    }

    if (!wall_found)
    {
        wall_scan_start = 0.f;
        drop_ride();
        return;
    }
    wall_scan_start = found_angle;

    // ---- 119th RAMP = HANDS OFF ------------------------------------------------------------
    // The 118th took the holds off ramps but left the PIN and the PEEL, which is what eats a
    // surf. A texturebug seam is a VERTICAL face; a plane tilted past the band is a surf, so
    // leave it alone. pCmd is untouched here, so returning preserves the velocity exactly
    const float plane_nz   = primary.plane.normal.z;
    const bool  ramp_plane = fabsf(plane_nz) > ramp_band;
    if (ramp_plane)
    {
        if (debug && DiagOk(s_tbDiagTick, false))
            DebugLog("TB: ramp hands-off nz=%.3f band=%.3f xy=%.0f vz=%.1f",
                     plane_nz, ramp_band, vel_now.Length2D(), vel_now.z);
        drop_ride();
        return;
    }

    // 108th -- locking a ride records the plane's OFFSET as well as its normal
    const auto lock_plane = [&]() { ride_normal = primary.plane.normal; ride_plane_d = primary.plane.dist; };

    // seamless leave: steering off the wall = the user keeps the cmd and the ride ends.
    // 83rd -- RIDE ONLY. Bhopping alongside a wall means strafing, so half of every strafe
    // cycle points away and this returned before the sweep ran. Off a ride the tick is
    // searched anyway and steer_away only forbids the HOLD commits
    const bool steer_away = TB_SteeringAwayFromWall(pCmd, primary.plane.normal,
                                                    original_forward, original_side);
    if (!riding || !steer_away) s_steer_run = 0;
    if (steer_away && riding && ++s_steer_run >= k_tbSteerRunEnd)
    {
        if (debug) DebugLog("TB: leave steer run=%d vz=%.1f", s_steer_run, vel_now.z);
        drop_ride();
        return;
    }

    // autocrouch is SIM-DECIDED: stand sweep first, duck pass only on a total miss. A duck
    // win latches duck_needed so a later stand win cannot uncrouch us off the surf
    if (!hold_grace) duck_needed = false;
    // not on a ride any more -> the next park has to earn itself a fresh clean pin
    if (!riding) ride_had_clean_pin = false;

    if (duck_needed) { pCmd->buttons |= IN_DUCK; original_buttons |= IN_DUCK; }

    const Vec3 approach_normal = primary.plane.normal * -1.f;
    Vec3 angle_wall = VectorAngles(approach_normal);
    angle_wall.y = NormalizeAngle(angle_wall.y);

    // ================= 2. THE SWEEP BASE ===================================
    const int nBase = PredCommandsPredicted();
    const auto Restore = [&]() { RestoreToFrame(nBase - 1); };
    s_tbSupportFallback = 0;

    // 81st -- the velocity every variant starts from, read ONCE off the restore base so the
    // closed form can answer "does this move the player anywhere new" without a sim.
    // start_pos / vel_now above are the LIVE (end of cmd) state, a different place
    Restore();
    const Vec3  sweep_base_vel = EntV3(pLocal, g_offVelocity);
    const Vec3  sweep_base_org = EntV3(pLocal, g_offOrigin);
    const float xy_base  = sweep_base_vel.Length2D();
    // 107th -- standing still (< 1) there is nothing to preserve, so the floors go to 0
    const float xy_floor = xy_base >= 1.f ? xy_base * k_tbXyKeepFrac : 0.f;
    const float xy_brake = xy_base >= 1.f ? xy_base * k_tbXyCreepFrac : 0.f;

    // base sign, verbatim
    const float velocity_yaw = RAD2DEGF(atan2f(vel_now.y, vel_now.x));
    const float relative_yaw = NormalizeAngle(velocity_yaw - angle_wall.y);
    const float sign         = (relative_yaw >= 0.f) ? -1.f : 1.f;

    float best_support = 1.f;
    float best_forward = original_forward, best_side = original_side;
    int   best_buttons = original_buttons;
    // 107th -- the same creep search, minus the variants that brake
    float best_hold_support = 1.f;
    float best_hold_forward = original_forward, best_hold_side = original_side;
    int   best_hold_buttons = original_buttons;
    float best_hold_in_fwd  = 0.f;
    // 114th -- best_hold_support starts at 1.0 as a SENTINEL and the creep gate is a slider
    // that goes past it, so a no-candidate tick used to commit the player's own raw move and
    // take m_owns_cmd with it. A creep has to be a move the sweep actually MEASURED
    bool  best_hold_valid = false;

    bool  found_bug = false;        // CLEAN pin (predicted xy alive) = a real texturebug
    TbPin_t best_pin{};
    // 117th -- the best WALLBUG pin (predicted xy < 1). KEPT, not discarded: on a ride this
    // is the park, and committing it is the only thing that re-collides and snaps z back
    TbPin_t best_stuck{};
    bool  found_stuck_pin = false;

    // 106b -- REFINE ON A FRESH CATCH ONLY. Mid-ride the tick must stay first-pin-wins: warm
    // start replays the variant that is already holding, it pins, and the tick ends there
    int   pin_refine_left  = riding ? 0 : k_tbPinRefine;
    // 107th -- mid-ride stays first-pin-wins ONLY while that pin carries the speed. A pin
    // that brakes re-opens the budget once
    bool  refine_regranted = false;

    // eps = base VERBATIM 0.1, both signs: real pins land EXACT (mindz=0.000). +half stays
    // accepted on a fresh catch -- rejecting it dropped real catches
    const float eps = 0.1f;
    // band entry often completes 1 tick late -- capped retry re-sims flush near-miss
    // variants one extra tick (cap 8 = no lag repeat)
    int retries_left = caught_last_tick ? k_tbRideRetries : 8;
    int dbg_rty = 0, dbg_rtp = 0;   // retries used / retries that pinned

    // ---- 103rd analytic no-contact probe: context + the ONE calibration sim ----------------
    TbFast_t fast{};
    int dbg_fast = 0;
    {
        const int player_flags = EntI(pLocal, g_offFlags);
        // any duck at all is out: HandleDuckingSpeedCrop scales forward_move, and a duck
        // transition moves the origin AND the hull mid-tick. Both break the closed form
        const bool duck_free = !(original_buttons & IN_DUCK)
                            && !(player_flags & (FL_DUCKING | FL_ANIMDUCKING));
        const bool env_ok = !(player_flags & (FL_WATERJUMP | FL_FROZEN | FL_INWATER
                                            | FL_SWIM | FL_CONVEYOR | FL_BASEVELOCITY))
                         && !EntSwimming(pLocal);
        if (duck_free && env_ok && tick_dt > 0.f && fabsf(sweep_base_vel.z) < 3000.f && SimsLeft() > 4)
        {
            fast.org = sweep_base_org;  fast.vel  = sweep_base_vel;
            fast.mins = mins;           fast.maxs = maxs;
            fast.dt = tick_dt;
            fast.vz_start = sweep_base_vel.z + TargetPredictZVel();
            fast.vz_free  = sweep_base_vel.z + TargetPredictZVel() * 2.f;
            fast.dz_free  = fminf(fabsf(fast.vz_free - TargetPredictZVel()),
                                  fabsf(fast.vz_free + TargetPredictZVel()));
            // a contact-free tick landing ON the pin would have to be COMMITTED, and every
            // contact-free variant lands on the same vz -- so near the band, real sims only
            if (fast.dz_free > eps + 1.f)
            {
                const float cal_yaw = (sweep_base_vel.Length2D() > 1.f)
                    ? RAD2DEGF(atan2f(-sweep_base_vel.y, -sweep_base_vel.x))
                    : angle_wall.y + 180.f;
                const float cal_rad = DEG2RADF(cal_yaw);
                const Vec3  cal_dir(cosf(cal_rad), sinf(cal_rad), 0.f);
                const float a_full = sv_airaccel * tick_dt;

                UserCmd_t cal = *pCmd;
                cal.buttons = original_buttons;
                cal.viewangles = original_view;
                cal.forwardmove = 30.f;
                cal.sidemove = 0.f;
                Vec3 cal_wish = original_view; cal_wish.y = cal_yaw;
                CorrectMovement(&cal, cal_wish, original_view);

                const Vec3 cal_move(sweep_base_vel.x + cal_dir.x * a_full * 30.f,
                                    sweep_base_vel.y + cal_dir.y * a_full * 30.f, fast.vz_start);
                Vec3 cal_end{};
                if (TB_FastClear(pLocal, fast, cal_move, cal_end))
                {
                    Restore();
                    Pred_Simulate(pLocal, &cal);
                    const Vec3 v_after = EntV3(pLocal, g_offVelocity);
                    const Vec3 o_after = EntV3(pLocal, g_offOrigin);
                    Restore();

                    const float dx = v_after.x - sweep_base_vel.x;
                    const float dy = v_after.y - sweep_base_vel.y;
                    const float d_len  = sqrtf(dx * dx + dy * dy);
                    const float d_perp = dx * -cal_dir.y + dy * cal_dir.x;
                    const float a_meas = d_len / 30.f;
                    const Vec3  expect = fast.org + Vec3(sweep_base_vel.x + dx,
                                                         sweep_base_vel.y + dy, fast.vz_start) * tick_dt;
                    const Vec3  org_err = o_after - expect;
                    const auto near_a = [&](float f)
                        { return a_meas > a_full * f * 0.97f && a_meas < a_full * f * 1.03f; };
                    fast.ok = fabsf(v_after.z - fast.vz_free) < 0.02f
                           && org_err.LengthSqr() < 0.0009f
                           && fabsf(d_perp) < 0.01f
                           && (near_a(1.f) || near_a(0.25f));
                    fast.a = a_meas;   // measured beats derived -- this is the number the engine used
                }
            }
        }
    }

    // true + the three measurements when the variant provably flies through empty air; false
    // = the caller owes a real sim (contact, duck, retry band, budget out, not calibrated).
    // 117th -- NEVER ON A RIDE: the probe answers from traces, and a texturebug pin is
    // exactly the state where a trace says clear and the engine collides anyway (the
    // 0.03125 surface extension IS the trick)
    const auto fast_probe = [&](float world_yaw, float forward, bool duck,
                                float& out_z, float& out_xy, float& out_support) -> bool
    {
        if (hold_grace) return false;
        if (!fast.ok || fast.duck_seen || duck || s_tbFastProbes >= k_tbFastProbeCap) return false;
        const float rad = DEG2RADF(world_yaw);
        const Vec3  wishdir(cosf(rad), sinf(rad), 0.f);
        const Vec3  d = TB_FastAccel(fast, wishdir, forward);
        const Vec3  v_move(fast.vel.x + d.x, fast.vel.y + d.y, fast.vz_start);
        Vec3 end{};
        ++s_tbFastProbes;
        if (!TB_FastClear(pLocal, fast, v_move, end)) return false;
        // same trace, same filter, exact origin -> the creep pick reads the identical number
        const float support = MV_SupportDistance(pLocal, primary, &end);
        // the retry arm re-sims a flush near miss one extra tick and the fast path cannot
        // chain, so anything that could reach it stays on the real sim
        if (support <= 0.1f) return false;
        out_z  = fast.vz_free;
        out_xy = v_move.Length2D();
        out_support = support;
        return true;
    };

    // low-fwd-first grid; tiny fwds {0.001,0.01,0.05,0.25} catch precision/tight seams the
    // 0.5+ grid cannot. Angles stay 86-90 grazing -- widening them ate xy and shortened rides
    static constexpr float forward_values[] =
        { 0.001f, 0.01f, 0.05f, 0.25f, 1.f, 0.875f, 0.5f, 1.3856f, 2.125f, 2.875f, 5.f, 15.f, 30.f };

    float dbg_min_dz = 999.f, dbg_min_sup = 999.f, dbg_min_dz_alive = 999.f;
    int   dbg_sims = 0, dbg_rej = 0, dbg_pins = 0, dbg_warm = 0, dbg_dup = 0, dbg_mirror = 0;
    int   dbg_duck_rej = 0;   // a crouch pin the 2-tick confirm threw out
    float dbg_pin_xy = 0.f;

    // the sweep may keep going after a pin, but only k_tbPinRefine variants deep -- past that
    // this is the old first-pin-wins early exit, byte for byte
    const auto sweep_done = [&]() -> bool { return found_bug && pin_refine_left <= 0; };

    // angle_off signed off the wall normal
    const auto try_variant = [&](float forward, float angle_off, bool duck) -> bool
    {
        if (found_bug)
        {
            if (pin_refine_left <= 0) return true;
            --pin_refine_left;
        }
        const float world_yaw = angle_wall.y + angle_off;

        UserCmd_t fake = *pCmd;
        fake.buttons = original_buttons;     // duck rides in originals, base style --
        if (duck) fake.buttons |= IN_DUCK;   // NEVER stripped (a strip = mid-surf uncrouch)
        fake.viewangles  = original_view;
        fake.forwardmove = forward;
        fake.sidemove    = 0.f;
        Vec3 wish = original_view; wish.y = world_yaw;
        CorrectMovement(&fake, wish, original_view);   // silent -- the view stays put

        // both signs on a FRESH catch (dropping +half globally costs real catches). MID-RIDE
        // +half only means "landed on top": the sim crosses the seam, reads a ground state,
        // we commit it and stand the player up = the random mid-surf stop
        const auto pin_dz = [&](float vz) -> float
        {
            const float dz_neg = fabsf(vz - TargetPredictZVel());
            if (caught_last_tick) return dz_neg;
            return fminf(dz_neg, fabsf(vz + TargetPredictZVel()));
        };

        // 103rd -- closed form first. It answers only the variants that touch nothing, which
        // is what the sweep was burning its budget on
        float predicted_z = 0.f, predicted_xy = 0.f, support = 0.f;
        const bool fast_used = fast_probe(world_yaw, forward, duck, predicted_z, predicted_xy, support);
        if (fast_used) ++dbg_fast;
        else
        {
            Restore();
            Pred_Simulate(pLocal, &fake);
            ++dbg_sims;
            if (duck) fast.duck_seen = true;   // the collideable may hold the duck hull from here on
            const Vec3 v = EntV3(pLocal, g_offVelocity);
            predicted_z  = v.z;
            predicted_xy = v.Length2D();
            support      = MV_SupportDistance(pLocal, primary);
        }
        float dz = pin_dz(predicted_z);
        if (dz < dbg_min_dz)   dbg_min_dz = dz;
        if (support < dbg_min_sup) dbg_min_sup = support;
        // xy = diag only, never a gate
        if (predicted_xy < 1.f) ++dbg_rej;
        else if (dz < dbg_min_dz_alive) dbg_min_dz_alive = dz;

        bool  pinned = dz < eps;              // base pin = vz ALONE; support feeds the CREEP pick
        float pin_xy = predicted_xy;
        float pin_support = support;

        // flush near-miss while falling: a seam crossed mid-tick pins 1 tick late. !fast_used
        // is REQUIRED, not defensive -- this chains a second sim off the entity state the
        // first left, and a fast probe never simulated. NOT catch-only: forbidding it on
        // caught_last_tick left every landed pin followed by 2-6 free-fall ticks, sliding the
        // hull past the face bottom and off the wall
        if (!fast_used && !pinned && predicted_z < 0.f && support <= 0.1f
            && retries_left > 0 && SimsLeft() > 0)
        {
            --retries_left;
            ++dbg_rty;
            Pred_Simulate(pLocal, &fake);
            ++dbg_sims;
            const Vec3 v2 = EntV3(pLocal, g_offVelocity);
            const float xy2  = v2.Length2D();
            const float sup2 = MV_SupportDistance(pLocal, primary);
            const float dz2  = pin_dz(v2.z);
            if (dz2 < dbg_min_dz) dbg_min_dz = dz2;
            if (xy2 < 1.f) ++dbg_rej;
            else if (dz2 < dbg_min_dz_alive) dbg_min_dz_alive = dz2;
            if (sup2 < dbg_min_sup) dbg_min_sup = sup2;
            pinned = dz2 < eps;
            if (pinned) ++dbg_rtp;
            pin_xy = xy2;
            pin_support = sup2;
        }

        // xy PREFERENCE, never a veto: a wallbug pin is still committed if nothing cleaner
        // exists -- the sweep only stops preferring it when a clean pin also exists
        if (pinned)
        {
            if (pin_xy >= 1.f)
            {
                found_bug = true;
                ++dbg_pins;
                TbPin_t cand{};
                cand.have = true;
                cand.fwd  = fake.forwardmove;  cand.side = fake.sidemove;
                cand.buttons = fake.buttons;
                cand.xy = pin_xy;              cand.in_fwd = forward;
                cand.support = pin_support;    cand.angle_off = angle_off;
                cand.duck = duck;
                cand.keeps_xy = pin_xy >= xy_floor;
                if (!best_pin.have || TB_PinBetter(cand, best_pin))
                    { best_pin = cand; dbg_pin_xy = pin_xy; }
                // the pin in hand brakes -> buy one refine window to try to beat it on speed
                if (!best_pin.keeps_xy && !refine_regranted)
                    { refine_regranted = true; pin_refine_left = k_tbPinRefine; }
                return pin_refine_left <= 0;
            }

            // 117th -- KEEP THE MOVE. A stuck pin is only a wallbug on a FRESH grab; once the
            // ride has surfed the plane it is the park. Best xy among the stuck candidates,
            // so the park costs as little speed as it can
            found_stuck_pin = true;
            if (!best_stuck.have || pin_xy > best_stuck.xy)
            {
                best_stuck.have = true;
                best_stuck.fwd  = fake.forwardmove;  best_stuck.side = fake.sidemove;
                best_stuck.buttons = fake.buttons;
                best_stuck.xy = pin_xy;              best_stuck.in_fwd = forward;
                best_stuck.angle_off = angle_off;    best_stuck.duck = duck;
            }
            return false;
        }
        if (support < best_support)
        {
            best_support = support;
            best_forward = fake.forwardmove;
            best_side    = fake.sidemove;
            best_buttons = fake.buttons;
        }
        // 107th -- the pick that gets COMMITTED, with a hard-brake veto: the creep runs on
        // every no-pin tick, so a half-speed micro-move is where TB bled xy for whole rides.
        // Separate from best_support so the autocrouch gate still sees the true closest
        // variant (vetoing inside it would skip the duck pass). The ride fwd cap is for xy
        // bleed (a graze wish at fwd 15 eats ~2 u/s a tick); a near-perpendicular wish does
        // not bleed and is the only move that closes eps in one post-pin tick, so judge it on
        // the speed it kept
        if (support < best_hold_support && predicted_xy >= xy_brake
            && (!caught_last_tick || forward <= k_tbCreepFwdMax
                || predicted_xy >= xy_base - k_tbCreepXyLoss))
        {
            best_hold_support = support;
            best_hold_forward = fake.forwardmove;
            best_hold_side    = fake.sidemove;
            best_hold_buttons = fake.buttons;
            best_hold_in_fwd  = forward;
            best_hold_valid   = true;
        }
        return false;
    };

    // ---- natural pass: the user's raw cmd already pins -> keep it (1 sim, fresh catch only) --
    if (!caught_last_tick && (fabsf(original_forward) > 0.01f || fabsf(original_side) > 0.01f)
        && SimsLeft() > 0)
    {
        UserCmd_t natural = *pCmd;
        Restore();
        Pred_Simulate(pLocal, &natural);
        ++dbg_sims;
        const Vec3  nv   = EntV3(pLocal, g_offVelocity);
        const float nsup = MV_SupportDistance(pLocal, primary);
        const float ndz  = fminf(fabsf(nv.z - TargetPredictZVel()), fabsf(nv.z + TargetPredictZVel()));
        if (nv.Length2D() < 1.f) ++dbg_rej;
        if (nsup < dbg_min_sup) dbg_min_sup = nsup;
        if (ndz < eps)
        {
            Restore();
            s_textureBugHit = true;      // the user cmd stays untouched in pCmd
            lock_plane();
            ride_locked = true;
            ride_had_clean_pin = true;
            s_ride_grace = k_tbRideGrace;
            ++s_ride_ticks;
            s_tbPhase = s_ride_ticks >= 2 ? TBP_RIDE : TBP_PIN;
            s_tbRideLen = s_ride_ticks;
            s_tbRideHold = s_ride_grace;
            if (debug) DebugLog("TB: natural pin vz=%.1f xy=%.0f", nv.z, nv.Length2D());
            return;
        }
    }

    // dual sign, fresh-approach only: head-on, `sign` is a coin flip and the wrong one kills
    // xy on every variant. Mid-ride stays one sign (widening shortens rides)
    const bool dual_sign = !caught_last_tick && !ride_had_clean_pin;
    constexpr float angle_top = 91.f;   // one extra angle at 89.999 = a pure graze

    // dedup: grid variants collapse constantly (the 30 u/s wishspd cap zeroes accel on a
    // ride), so a collapsed variant is skipped instead of re-simulated for the same answer
    TbDedup_t stand_dedup{}, crouch_dedup{};

    // fence: leave the analytic solve pass its own sims (see k_tbSolveReserve)
    const int grid_floor = g_tbSolveForward ? k_tbSolveReserve : 0;

    const auto sweep = [&](bool duck)
    {
        TbDedup_t& pool = duck ? crouch_dedup : stand_dedup;
        const float sign_order[2] = { sign, -sign };
        const int   sign_count    = dual_sign ? 2 : 1;

        for (int s = 0; s < sign_count && !sweep_done(); ++s)
        {
            for (const float forward : forward_values)
            {
                if (sweep_done() || SimsLeft() <= grid_floor) break;
                for (float angle = 85.999f; angle < angle_top && SimsLeft() > grid_floor; angle += 1.f)
                {
                    const Vec3 d = TB_AirAccelDelta(sweep_base_vel,
                                                    angle_wall.y + angle * sign_order[s],
                                                    forward, sv_airaccel, tick_dt);
                    if (pool.SeenOrAdd(d)) { ++dbg_dup; continue; }
                    if (try_variant(forward, angle * sign_order[s], duck)) break;
                }
            }
            if (found_bug && s == 1) dbg_mirror = 1;
        }
    };

    // pass 0 = WARM START (106th): replay the exact (forward, angle_off, duck) that pinned
    // last time. On a ride the winner barely moves, so this ends the tick in ONE sim.
    // Compared on the EFFECTIVE duck bit: a stand-pass pin on a latched duck ride carries
    // IN_DUCK through original_buttons with duck=false, so `s_warm_duck == duck_needed`
    // never matched and warm was 0 for whole rides
    const bool duck_bit_now = (original_buttons & IN_DUCK) != 0;
    if (!found_bug && s_warm_valid && s_warm_duck == duck_bit_now && SimsLeft() > 0
        && (riding || tick_now - s_warm_tick <= 3))
    {
        TbDedup_t& warm_pool = s_warm_duck ? crouch_dedup : stand_dedup;
        warm_pool.SeenOrAdd(TB_AirAccelDelta(sweep_base_vel, angle_wall.y + s_warm_angle,
                                             s_warm_fwd, sv_airaccel, tick_dt));
        dbg_warm = 1;
        try_variant(s_warm_fwd, s_warm_angle, s_warm_duck);
    }

    // the ONE FLOAT STEP pass is deleted -- ~150 stand sims read din=0 uvin=0, the straight-in
    // step never moved the hull into the wall. The step SIZE is kept for the log: at vz -6 the
    // pin window is one float ulp wide, which is why a vz -6 tick re-pins rarely and -18 does
    int   ulp_axis = -1;
    const float ulp_step = TB_UlpStepIntoWall(primary.plane.normal, sweep_base_org, ulp_axis);

    // pass 1: exactly what the player holds. No crouch added, ever
    if (!found_bug) sweep(false);

    // pass 2 = autocrouch: the standing hull failed on every fwd/angle, try the duck hull.
    // A pin here latches duck_needed; a miss rolls the creep pick back to standing
    if (!found_bug && !duck_needed && best_support < 0.5f && SimsLeft() > 0)
    {
        const float s_sup = best_support, s_fwd = best_forward, s_side = best_side;
        const int   s_but = best_buttons;
        const float h_sup = best_hold_support, h_fwd = best_hold_forward, h_side = best_hold_side;
        const int   h_but = best_hold_buttons;
        const float h_in  = best_hold_in_fwd;
        const bool  h_val = best_hold_valid;
        const bool    d_stuck = found_stuck_pin;   // rolled back with the pin below
        const TbPin_t d_best  = best_stuck;

        sweep(true);

        // CONFIRM THE CROUCH PIN BEFORE LATCHING IT. In air a duck teleports the origin +20z,
        // so the move that wins the pin can carry the hull off the seam it just caught (all 9
        // short duck runs read one pinned tick, ride dropped, stand back up = the "randomly
        // crouches/uncrouches"). A standing miss costs nothing, a ducked miss costs the ride.
        // Same 2-sim probe as the smash guard: a held pin sits on the half-gravity target
        // every tick (dz ~ 0), a lost one is two gravity steps down by tick 2 (dz ~ 24)
        if (found_bug && SimsLeft() > 1)
        {
            UserCmd_t probe = *pCmd;
            probe.viewangles  = original_view;
            probe.forwardmove = best_pin.fwd;
            probe.sidemove    = best_pin.side;
            probe.buttons     = best_pin.buttons;
            Restore();
            Pred_Simulate(pLocal, &probe);
            Pred_Simulate(pLocal, &probe);
            const float duck_dz = fabsf(EntV3(pLocal, g_offVelocity).z - TargetPredictZVel());
            dbg_sims += 2;
            Restore();
            if (duck_dz > k_tbDuckConfirmDz)
            {
                found_bug    = false;     // roll the whole crouch pass back to standing
                best_pin     = TbPin_t{};
                found_stuck_pin = d_stuck;   // a ducked wallbug is the same trap, one tick later
                best_stuck      = d_best;
                dbg_duck_rej = 1;
            }
        }

        if (found_bug) duck_needed = true;
        else
        {
            best_support = s_sup; best_forward = s_fwd; best_side = s_side; best_buttons = s_but;
            best_hold_support = h_sup; best_hold_forward = h_fwd; best_hold_side = h_side;
            best_hold_buttons = h_but; best_hold_in_fwd = h_in;   best_hold_valid = h_val;
        }
    }

    // ---- the PAPER numbers the solve and the press both aim at -----------------------------
    // 118th RAMP-AWARE v_in. On a vertical face nz is 0 and this is the xy-only form exactly.
    // vz_mid = the vz the move runs at: StartGravity applies the half-tick BEFORE the move
    const float vz_mid = sweep_base_vel.z + TargetPredictZVel();
    const float v_in_z = ramp_plane ? -(vz_mid * plane_nz) : 0.f;
    const auto plane_v_in = [&](float vx, float vy) -> float
    {
        return -(vx * primary.plane.normal.x + vy * primary.plane.normal.y) + v_in_z;
    };
    const float v_in_now = plane_v_in(sweep_base_vel.x, sweep_base_vel.y);

    // the paper's s_z, TWO ways, both logged. STACK = 1024u column ray (the top of everything
    // above us). FACE = the walk up this brush's own face
    float brush_s_z = 0.f;
    {
        const Vec3 inside = primary.endpos - primary.plane.normal * 1.f;
        TraceFilterWorldProps top_filter; top_filter.Setup(pLocal);
        GameTrace_t top{};
        TraceLine(Vec3(inside.x, inside.y, start_pos.z + 1024.f),
                  Vec3(inside.x, inside.y, start_pos.z), MASK_PLAYERSOLID, &top_filter, &top);
        if (top.fraction < 1.f && top.plane.normal.z > 0.7f)
        {
            const float s_z = top.endpos.z - start_pos.z;
            if (s_z > 0.5f) brush_s_z = s_z;
        }
    }
    // face walk, cached per plane. Probed just ABOVE the head, because that is the brush
    // whose bottom the head is crossing and therefore the brush whose top is the paper's s_z
    const float player_h = maxs.z - mins.z;
    const float probe_z  = start_pos.z + maxs.z + k_tbHeadProbe;
    float face_s_z = 0.f;
    {
        const bool same_plane = s_tbFaceValid
            && fabsf(s_tbFacePlaneD - primary.plane.dist) < 0.10f
            && (s_tbFaceNormal.x * primary.plane.normal.x
              + s_tbFaceNormal.y * primary.plane.normal.y
              + s_tbFaceNormal.z * primary.plane.normal.z) > 0.9995f;
        // the cache expires on HEIGHT as well as ticks: s_tbFaceTopZ is an absolute world z
        // and face_s_z is that minus a falling origin, so a cache held across a 24-tick fall
        // at 400 u/s drifts 144u and goes NEGATIVE (log: 141 of 262 walked answers negative,
        // 22 more under the hull height the paper calls the floor)
        const bool same_height = fabsf(start_pos.z - s_tbFaceOrgZ) < k_tbFaceOrgDrift;
        if (!same_plane || !same_height || tick_now - s_tbFaceTick > k_tbFaceRefresh)
        {
            bool walked = false;
            float top_z = TB_FaceTopZ(pLocal, primary.plane, start_pos, probe_z, walked);
            // head is already above this wall's top -> nothing to walk up there. Retry at
            // mid-hull so a seam at the very top of a face still measures
            if (!walked)
                top_z = TB_FaceTopZ(pLocal, primary.plane, start_pos,
                                    start_pos.z + maxs.z * 0.5f, walked);
            // the OTHER end of the same face. Only worth taking when the top walked --
            // H_brush needs both ends and a lone bottom answers nothing
            bool bot_ok = false;
            float bot_z = 0.f;
            if (walked)
            {
                bot_z = TB_FaceBotZ(pLocal, primary.plane, start_pos,
                                    fminf(probe_z, top_z - k_tbHeadProbe), bot_ok);
            }
            s_tbFaceValid  = walked;
            s_tbFaceTopZ   = top_z;
            s_tbFaceBotOk  = bot_ok;
            s_tbFaceBotZ   = bot_z;
            s_tbFaceNormal = primary.plane.normal;
            s_tbFacePlaneD = primary.plane.dist;
            s_tbFaceOrgZ   = start_pos.z;
            s_tbFaceTick   = tick_now;
            if (!same_plane) s_tbEdgeValid = false;   // the edge belonged to the old plane
        }
        if (s_tbFaceValid) face_s_z = s_tbFaceTopZ - start_pos.z;
    }
    // D MEASURED OFF THE BRUSH, NOT OFF THE FALL.
    // PAPER (10): D = H_brush + H_player, both fixed while we fall. `face_s_z` (top - origin)
    // equals D on exactly ONE tick -- head level with the brush bottom -- and is wrong the
    // rest of the time in the direction that shuts the window: 30u under it reads D 30 too
    // big, band_max = room*|vz|/(D+eps) shrinks by the same, and the creep never collides.
    // Walking both ends gives H_brush directly, so the sum stops drifting.
    // Same-texture caveat applies to both numbers: two flush brushes sharing a plane and a
    // name are one face to any trace, so this is the height of the same-texture RUN -- the
    // error the ladder below exists to cover.
    const float face_bot_h = (s_tbFaceValid && s_tbFaceBotOk)
                           ? (s_tbFaceTopZ - s_tbFaceBotZ) : 0.f;
    const float D_hb = face_bot_h > 0.5f ? face_bot_h + player_h : 0.f;

    // ---- THE PAPER WINDOW, MEASURED INSTEAD OF ASSUMED -------------------------------
    // PAPER (7): the top side of a box brush wins while
    //      (s_perp - eps)/|d_perp|  <  (s_z - eps)/|d_z|,  i.e.
    //      |v_in|  <  (eps - gap) * |vz_move| / (D + eps)
    // Three terms were wrong, all in the same direction:
    //   * gap assumed ZERO. At gap == eps the window is zero wide and nothing can pin.
    //   * |vz| read PRE-StartGravity. The move runs on vz_mid: -12 against a stored -6.
    //   * D came from the column ray = top of the WHOLE STACK, 3x too big on a 3-brush wall.
    // 116th saw real pins riding at ~5.5x the old v_target and multiplied the target by 5.
    // That factor was the error, not the physics
    const float gap  = TB_PlaneGap(primary.plane, sweep_base_org, mins, maxs);
    const float room = MV_SURF_EPS - gap;         // how much of the eps band is left to work in
    // THE FLOOR IS THE HULL. PAPER (10): at the crossing the head IS the brush bottom, so
    // D = Hbrush + Hplayer and D can never be under the player's height. Anything smaller is
    // a broken measurement, broken in the direction that hurts: D is a denominator, so small
    // D = huge band_max = TB presses 20x too hard = no catch.
    // dsrc 2 = brush-height pair (phase-free, preferred), 1 = top - origin, 0 = column ray,
    // -1 = nothing walked, hull floor only
    const float D_raw = D_hb > player_h ? D_hb
                      : (face_s_z > player_h ? face_s_z : brush_s_z);
    const float D_use = fmaxf(D_raw, player_h);
    const int   d_src = D_hb > player_h ? 2
                      : (face_s_z > player_h ? 1 : (brush_s_z > player_h ? 0 : -1));
    const bool  band_ok = room > 1e-4f;

    // room <= 0 = the hull is at or outside the eps face and 9.4.2 says the window is shut:
    // nothing pins this tick, so aim at the smallest approach that still collides. Rare (gap
    // sat in [0, 0.005) on 932 of 1118 ticks), so the fallback is a guard, not a code path.
    // Hard ceiling 1.0: the widest window physics allows is room=eps at D=player_h, which is
    // 0.40 u/s even at |vz|=800, so it can only ever catch a broken measurement
    const float band_max = fminf(band_ok
        ? room * fabsf(vz_mid) / (D_use + MV_SURF_EPS)
        : MV_SURF_EPS * fabsf(vz_mid) / (D_use + MV_SURF_EPS) * 0.05f, 1.f);

    // A LADDER, NOT A POINT. D cannot be measured exactly (two flush same-texture brushes
    // share a plane AND a name), and a 4x error in D is a 4x error in the target. Six
    // fractions span a 50x range for six candidates, so the catch stops depending on brush
    // height and arrival speed. Never 0 and never the ceiling: at |v_in| == 0 nothing collides
    static constexpr float k_bandFrac[] = { 0.70f, 0.45f, 0.25f, 0.12f, 0.05f, 0.015f };

    // ---- THE ENTRY LAW. `room <= 0` IS NOT "ASK FOR ALMOST NOTHING" --------------------
    // log: 69 of 107 TB ticks read `vin=0.0000 gap=0.03125 room=0.00000 bok=0`, pins=0 on
    // every one. That plateau is the engine's rest state: a collision places the hull at
    // EXACTLY eps and zeroes v_in (PAPER 9.1), and the old fallback then asked for 0.0029 u/s
    // = 0.00004u a tick, 700 ticks to cross the band. It never crossed, so TB sat against the
    // wall in the one state 9.4.2 says can never bug.
    // band_max is right -- it is the BUG window and at room <= 0 it really is shut. The error
    // was treating "no bug this tick" as "do nothing": the job at gap >= eps is to GET INSIDE.
    // A swept trace collides only in case 2 (s1 > 0, s2 <= 0), so with s1 = gap:
    //     v_in <  gap / dt          -> no collision, the hull just moves to r2
    //     v_in > (gap - eps) / dt   -> r2 lands inside the eps band
    // eps/dt wide at EVERY gap (2.08 u/s at 66 tick). Aim at its middle, eps/2 inside.
    const float v_enter_greedy = fmaxf((gap - MV_SURF_EPS * 0.5f) / tick_dt, 0.f);

    // ...AND IT MUST NOT ARRIVE FASTER THAN IT CAN BRAKE. AirAccelerate adds at most
    // `addspeed = 30 - dot(vel, wishdir)` a tick (gamemovement.cpp:1720-1731), so 30 u/s is
    // the authority in BOTH directions and a v_in of 287 needs ~9.5 ticks and ~20u of room.
    // Braking from v to rest costs `dt * (v^2/2A + v/2)` -- the integral plus the half tick
    // the discrete ramp overshoots by -- so the largest v that fits the room left is the
    // positive root of v^2 + A*v - 2*A*(gap - eps/2)/dt = 0, times 0.7 because a tight cap
    // overshoots on the last tick. A 1.44u gap converges in four ticks; greedy alone
    // overshoots and collides on the third.
    constexpr float k_tbAccelAuth = 30.f;   // AirAccelerate's addspeed cap, both directions
    const float room_to_band = fmaxf(gap - MV_SURF_EPS * 0.5f, 0.f);
    const float v_enter_brake = 0.7f * 0.5f
        * (sqrtf(k_tbAccelAuth * k_tbAccelAuth + 8.f * k_tbAccelAuth * room_to_band / tick_dt)
           - k_tbAccelAuth);
    const float v_enter = fminf(v_enter_greedy, v_enter_brake);

    const float v_target = band_ok ? fmaxf(band_max * k_bandFrac[1], 0.001f)
                                   : fmaxf(v_enter, 0.001f);
    // the 0.05 ceiling is a BAND number and must not clip an entry: at gap == eps the entry
    // is 1.04 u/s, and clipping it to 0.05 is the 700-tick creep again
    const float press_target = band_ok
        ? fminf(fmaxf(band_max * k_bandFrac[1], 0.001f), 0.05f)
        : fmaxf(v_enter, 0.001f);

    // ---- pass 3 = EXACT SOLVE (83rd) -------------------------------------------------------
    // Every variant changes speed INTO the wall by accel*fwd*dt*cos(angle_off), so 13 fwds x
    // 5 angles can only express a sparse SET of approach speeds, and the window (|v_in| under
    // ~eps*|vz|/s_z) essentially never lands on one. Brute force cannot fix that, resolution
    // can. Angles run past 90 too: cos flips sign there, the only way to BRAKE an approach.
    // ---- RIDE SEARCH UNLOCK ----------------------------------------------------------
    // Solve and fan were gated `!ride_had_clean_pin`, i.e. off for every real surf: 528 of 528
    // riding no-pin ticks ran sol=0 fan=0 at avg dup=99 vs sims=56. At ride speed the
    // wishspd-30 cap makes every 86-91 deg graze a null move, so the dedup eats the whole grid
    // (the tick that ended a 71-tick ride read sims=2 dup=77) and the steep angles the fan
    // exists for were the forbidden case. The gate's real job was protecting the creep pick,
    // so snapshot that instead: a ride keeps its hold move unless the steep passes land a pin
    const bool ride_search = ride_had_clean_pin;

    int   dbg_solve = 0, dbg_close = 0;
    float dbg_solve_fwd = 0.f;
    if (!found_bug && g_tbSolveForward && SimsLeft() > 0)
    {
        const bool save_stuck = found_stuck_pin;
        const Vec3 n(primary.plane.normal.x, primary.plane.normal.y, 0.f);

        static constexpr float solve_angles[] =
            { 89.999f, 89.f, 87.999f, 85.999f, 83.999f, 80.999f, 70.999f, 55.999f,
              40.999f, 30.999f, 22.999f, 17.f, 13.f, 10.f, 90.001f, 91.f,
              92.001f, 94.001f, 96.001f, 99.001f, 109.001f, 124.001f };
        const float solve_sign[2] = { sign, -sign };
        // the k_bandFrac ladder, not two points: each rung is a different assumption about D.
        // When the band is SHUT the same six fractions span the ENTRY window instead -- floor
        // (gap - eps)/dt, eps/dt wide at any gap -- so the rungs walk the hull from the far
        // edge of the band to just inside it, covering error in `gap` rather than in D.
        // Capped by the brake law: a rung that cannot be stopped is a smash.
        // THE BAND HAS A FLOOR TOO. PAPER 9.1: a swept trace only collides in case 2,
        // s2 = gap - v_in*dt <= 0, so below v_in = gap/dt the hull stops short and nothing
        // pins (the "mindz=12, zero contact" rows). band_max is the pin window's CEILING,
        // gap/dt its floor, and the old rungs sat under the floor whenever
        // gap > band_max*frac*dt. Floor each rung just past it; a floor above the ceiling
        // collapses the rungs onto one value the dedup sims once, which is a D-error probe.
        float targets[6 + k_tbCloseN];
        int   n_targets = 0;
        const float enter_lo = fmaxf((gap - MV_SURF_EPS) / tick_dt, 0.f);
        const float pin_lo   = (gap > 0.f ? gap / tick_dt : 0.f) * 1.15f + 0.0005f;
        for (int i = 0; i < 6; ++i)
        {
            targets[n_targets++] = band_ok
                ? fmaxf(band_max * k_bandFrac[i], pin_lo)
                : fmaxf(fminf(enter_lo + (MV_SURF_EPS / tick_dt) * k_bandFrac[i], v_enter_brake),
                        0.0005f);
        }
        // CLOSING RUNGS. The pin needs gap < band_max*dt (~1e-4u at a ride's vz) and nothing
        // aimed there: band rungs move ~0.0004u/tick, entry rungs stop at >= 0.3 eps. Aim the
        // hull at a fraction of the gap left, never AT the wall (s2 <= 0 collides and the
        // engine puts it back at eps). These never pin themselves -- they are CREEP
        // candidates, and the creep's min-support pick keeps the deepest non-colliding one.
        // Near only (gap < 2 eps => v <= ~4 u/s, one fwd<=30 tick brakes it); far approaches
        // keep the brake-law entry rungs above
        if (gap > 0.f && gap < 2.f * MV_SURF_EPS)
        {
            const float s2_floor = fmaxf(4.f * ulp_step, 2e-5f);
            for (const float q : k_tbCloseQ)
            {
                const float s2 = fmaxf(q * fminf(gap, MV_SURF_EPS), s2_floor);
                if (s2 < gap) targets[n_targets++] = (gap - s2) / tick_dt;
            }
        }
        int solve_sims = k_tbSolveCap;

        for (const float angle : solve_angles)
        {
            if (sweep_done() || SimsLeft() <= 0 || solve_sims <= 0) break;
            for (int s = 0; s < 2 && !sweep_done() && SimsLeft() > 0 && solve_sims > 0; ++s)
            {
                const float off = angle * solve_sign[s];
                const float yaw = angle_wall.y + off;
                const float rad = DEG2RADF(yaw);
                const Vec3  wishdir(cosf(rad), sinf(rad), 0.f);
                // speed into the wall gained per unit of accelspeed along this wish direction
                const float c = -(wishdir.x * n.x + wishdir.y * n.y);
                if (fabsf(c) < 0.01f) continue;
                for (int ti = 0; ti < n_targets; ++ti)
                {
                    const float target = targets[ti];
                    if (SimsLeft() <= 0 || solve_sims <= 0) break;
                    const float need = (target - v_in_now) / c;   // accelspeed the engine must apply
                    if (need <= 0.f) continue;                    // wrong way round -- its mirror covers it
                    const float fwd = need / fmaxf(sv_airaccel * tick_dt, 1e-6f);
                    if (fwd < 0.001f || fwd > 30.f) continue;     // not reachable inside one tick
                    // what the engine will REALLY do with that forward (the wishspd 30 cap and
                    // the addspeed clamp both live in TB_AirAccelDelta)
                    const Vec3  d = TB_AirAccelDelta(sweep_base_vel, yaw, fwd, sv_airaccel, tick_dt);
                    const float v_in_hit = plane_v_in(sweep_base_vel.x + d.x, sweep_base_vel.y + d.y);
                    if (fabsf(v_in_hit - target) > fmaxf(target * 0.5f, 0.001f)) continue;
                    if (stand_dedup.SeenOrAdd(d)) { ++dbg_dup; continue; }
                    ++dbg_solve; --solve_sims;
                    if (ti >= 6) ++dbg_close;
                    if (try_variant(fwd, off, false)) { dbg_solve_fwd = fwd; break; }
                }
            }
        }
        if (!found_bug) found_stuck_pin = save_stuck;   // creep survives, wallbug does not
    }

    // ---- pass 4 = BOUNCE FAN (cumidere FUN_10044290 angle ladder) --------------------------
    // The grid samples 85.999-90 = pure GRAZE. A bounce arrives near head-on and the move that
    // pins it sits at a steep angle the grid never tries. At ride speed every graze variant is
    // a null move (addspeed <= 0 along travel), so these are the only candidates that move the
    // hull toward the wall at all.
    // Snapshot taken HERE, after the solve: the rollback is for the fan's blind steep moves,
    // while the solve's closing rungs are aimed v_in targets and are the only thing that gets
    // a ride deep enough to re-pin. Rolling them back handed every post-pin tick to the grid
    const float rs_sup = best_support, rs_fwd = best_forward, rs_side = best_side;
    const int   rs_but = best_buttons;
    const float rh_sup = best_hold_support, rh_fwd = best_hold_forward, rh_side = best_hold_side;
    const int   rh_but = best_hold_buttons;
    const float rh_in  = best_hold_in_fwd;
    const bool  rh_val = best_hold_valid;

    int   dbg_fan = 0;
    float dbg_fan_ang = 0.f;
    if (!found_bug && SimsLeft() > 0)
    {
        const bool save_stuck = found_stuck_pin;
        static constexpr float fan_angles[]  =
            { 83.999f, 80.999f, 70.999f, 55.999f, 40.999f, 30.999f, 22.999f, 17.f, 13.f, 10.f };
        static constexpr float fan_forward[] = { 0.001f, 0.01f, 0.05f, 0.25f, 0.75f, 1.f, 5.f, 15.f };
        const float fan_sign[2] = { sign, -sign };

        for (const float angle : fan_angles)
        {
            if (sweep_done() || SimsLeft() <= 0) break;
            for (int s = 0; s < 2 && !sweep_done() && SimsLeft() > 0; ++s)
            {
                const float off = angle * fan_sign[s];
                for (const float forward : fan_forward)
                {
                    if (SimsLeft() <= 0) break;
                    const Vec3 d = TB_AirAccelDelta(sweep_base_vel, angle_wall.y + off,
                                                    forward, sv_airaccel, tick_dt);
                    if (stand_dedup.SeenOrAdd(d)) { ++dbg_dup; continue; }
                    ++dbg_fan;
                    if (try_variant(forward, off, false)) { dbg_fan_ang = off; break; }
                }
            }
        }
        if (!found_bug) found_stuck_pin = save_stuck;
    }

    // the steep passes may hunt a pin, but on a RIDE they may not repick the hold: a 55 deg
    // fan variant is a huge move and the grid's creep is what keeps the eps band alive
    if (ride_search && !found_bug)
    {
        best_support = rs_sup; best_forward = rs_fwd; best_side = rs_side;
        best_buttons = rs_but;
        best_hold_support = rh_sup; best_hold_forward = rh_fwd; best_hold_side = rh_side;
        best_hold_buttons = rh_but; best_hold_in_fwd = rh_in;   best_hold_valid = rh_val;
    }

    // ---- 108th PRESS HOLD ------------------------------------------------------------------
    // The tick after every real contact reads minsup ~ -0.03 (flush) and vin = 0.000, and no
    // sim variant touches the seam again: contact clips the into-wall velocity to zero and the
    // hold move is picked by MIN SUPPORT, which is degenerate once flush. So when flush with
    // no pin, aim the tick at the PAPER band instead. Analytic: no sims, no traces.
    float press_fwd = 0.f, press_angle = 0.f, press_vin = 0.f, press_err = 999.f;
    float esc_fwd = 0.f, esc_angle = 0.f, esc_vin = 0.f, esc_err = 999.f;
    // Gating this on caught_last_tick excluded the one case it is for. Log, 1579 TB ticks:
    // press never ran on 1373 (87%), vin=0.0000 on 1174 (74%), and on 1038 (66%) not one of
    // ~136 simmed variants collided with anything. PAPER 9.4.2: at |v_in| == 0 the swept
    // trace is case 1, the hull is just moved and no side is tested -- so TB sat perfectly
    // flush waiting for a seam that could do nothing when it arrived. A long wall still
    // catches eventually (88% at 31+ ticks) off min-support jitter; a short ledge does not.
    // At the logged numbers (gap 0.00003, vz -353, D 173) the top side wins while
    // v_in < room*|vz|/(D+eps) = 0.065, press_target is 0.029, and s2 = gap - v_in*dt < 0
    // makes it a case-2 COLLISION, which the bug requires.
    // So run the press whenever the hull is inside the eps band, pinned or not. `band_ok` IS
    // "flush", so an approach still gets the creep and the entry law.
    if (caught_last_tick || band_ok)
    {
        // own ladder, NOT the 86-90 graze grid: at ride speed a graze reaches ~0.4 u/s of
        // press, an order under the band. 114th -- stopping at 10.999 killed every hold:
        // addspeed = min(wishspeed,30) - vel.wishdir and a ride's velocity is tangent, so a
        // wish `off` deg off the normal stays positive only while |v|*sin(off) < 30, which
        // at 216 u/s is off < 8 deg
        static constexpr float press_angles[] =
            { 0.001f, 2.f, 5.f, 8.f, 10.999f, 20.999f, 30.999f, 40.999f, 50.999f,
              60.999f, 70.999f, 80.999f, 85.999f, 87.999f, 89.999f };
        const float press_sign[2] = { sign, -sign };
        for (int s = 0; s < 2; ++s)
            for (const float angle : press_angles)
            {
                const float off = angle * press_sign[s];
                const float rad = DEG2RADF(angle_wall.y + off);
                const Vec3  wishdir(cosf(rad), sinf(rad), 0.f);
                for (const float fwd : forward_values)
                {
                    const Vec3 d = TB_AirAccelDeltaDir(sweep_base_vel, wishdir, fwd, sv_airaccel, tick_dt);
                    const Vec3 v(sweep_base_vel.x + d.x, sweep_base_vel.y + d.y, 0.f);
                    if (v.Length2D() < xy_brake) continue;   // same hard-brake veto as the creep
                    const float vin = plane_v_in(v.x, v.y);
                    if (vin <= 0.f) continue;
                    const float err = fabsf(vin - press_target);
                    if (err < press_err)
                        { press_err = err; press_fwd = fwd; press_angle = off; press_vin = vin; }
                }
            }
    }
    // 109b -- the escape search, SEPARATE and always on. Mirroring it inside the press ladder
    // (+-89.999 off the wall-facing yaw) gave every entry vin >= 0 and esc_err never left
    // 999. Outward needs |off| > 90
    {
        static constexpr float esc_angles[] = { 90.001f, 92.f, 95.f, 100.f, 110.f, 125.f, 145.f, 170.f };
        const float esc_sign[2] = { sign, -sign };
        for (int s = 0; s < 2; ++s)
            for (const float angle : esc_angles)
            {
                const float off = angle * esc_sign[s];
                const float rad = DEG2RADF(angle_wall.y + off);
                const Vec3  wishdir(cosf(rad), sinf(rad), 0.f);
                for (const float fwd : forward_values)
                {
                    const Vec3 d = TB_AirAccelDeltaDir(sweep_base_vel, wishdir, fwd, sv_airaccel, tick_dt);
                    const Vec3 v(sweep_base_vel.x + d.x, sweep_base_vel.y + d.y, 0.f);
                    if (v.Length2D() < xy_brake) continue;   // the whole point is leaving WITH the speed
                    const float vin = plane_v_in(v.x, v.y);
                    if (vin >= 0.f) continue;
                    const float err = fabsf(vin + press_target);   // smallest peel that clears
                    if (err < esc_err)
                        { esc_err = err; esc_fwd = fwd; esc_angle = off; esc_vin = vin; }
                }
            }
    }

    // ---- BRAKE INTO THE PARK -------------------------------------------------------
    // The 117th park is a WALLBUG: it only fires when found_stuck_pin is true, i.e. when the
    // engine's three float checks disagree on this exact plane at this exact coordinate
    // (PAPER 9.2). On walls where they never disagree there is no park and the ride runs off
    // the edge. But a surf ends because the face runs out AHEAD of us, the face is static, and
    // one tick of AirAccelerate takes a*fwd u/s off the tangent -- so measure the runout and
    // brake into it.
    // The wish is (-tangent) rotated just far enough toward the wall to put v_in on the same
    // PAPER band the press aims at, so the catch keeps firing the whole way down. forward is
    // solved to zero the tangent exactly (fwd = xy/a) and capped at walk speed, so it cannot
    // overshoot into reverse. LAW 1 safe by construction: it is the one place dead xy was
    // asked for, it engages only when a measured edge is inside the measured stopping
    // distance, and it never touches xy on any other tick
    bool  brake_now = false, brake_valid = false, brake_pin = false, brake_hard = false;
    float brake_fwd = 0.f, brake_off = 0.f, brake_vin = 0.f, dbg_runout = -1.f;
    if (g_tbParkBrake && ride_had_clean_pin && !steer_away && !ramp_plane)
    {
        // the tangent we are actually travelling along this face
        Vec3  tan(sweep_base_vel.x, sweep_base_vel.y, 0.f);
        const float into = tan.x * primary.plane.normal.x + tan.y * primary.plane.normal.y;
        tan.x -= primary.plane.normal.x * into;
        tan.y -= primary.plane.normal.y * into;
        tan.z = 0.f;
        const float tl = tan.Length2D();
        bool have_tan = false;
        if (tl > 1.f)
        {
            tan.x /= tl; tan.y /= tl;
            s_tbRideTan = tan; s_tbRideTanValid = true; have_tan = true;
        }
        else if (s_tbRideTanValid) { tan = s_tbRideTan; have_tan = true; }

        if (have_tan)
        {
            const bool fresh = s_tbEdgeValid && tick_now - s_tbEdgeTick <= k_tbEdgeRefresh;
            if (!fresh)
            {
                Vec3 edge{};
                float run = TB_SeamRunout(pLocal, primary.plane, start_pos, tan, probe_z,
                                          k_tbRunoutMax, edge, true);
                // head already over this wall's top -> measure the face at mid-hull instead,
                // same retry the top walk makes. An edge you cannot see is an edge you fall off
                if (run < 0.f)
                    run = TB_SeamRunout(pLocal, primary.plane, start_pos, tan,
                                        start_pos.z + maxs.z * 0.5f, k_tbRunoutMax, edge, true);
                // run < 0 = the probe never found the face to measure along; run == max = the
                // face outruns the ladder. Neither is an edge, and neither may brake
                s_tbEdgePos   = edge;
                s_tbEdgeValid = run >= 0.f && run < k_tbRunoutMax;
                s_tbEdgeTick  = tick_now;
                dbg_runout    = run;
            }
            else
            {
                const Vec3 here = TB_FaceAnchor(primary.plane, start_pos, probe_z);
                dbg_runout = (s_tbEdgePos.x - here.x) * tan.x + (s_tbEdgePos.y - here.y) * tan.y;
            }

            if (s_tbEdgeValid && dbg_runout >= 0.f)
            {
                // distance the brake needs: xy falls by decel every tick, so the ticks are
                // xy/decel and the ground covered is the average speed over them
                const float decel = sv_airaccel * tick_dt * fminf(fmaxf(xy_base, 1.f), 240.f);
                const float stop_ticks = decel > 1e-3f ? xy_base / decel : 0.f;
                const float stop_dist  = xy_base * tick_dt * (stop_ticks + 1.f) * 0.5f;
                brake_now  = dbg_runout <= stop_dist + g_tbParkLead;
                // the last chance to stop at all: past this the ride leaves the face whatever
                // it does, so an unpinning brake is still the better of the two outcomes
                brake_hard = dbg_runout <= stop_dist * 1.10f + 8.f;
            }
        }

        // THE BRAKE HAS TO EARN THE TICK. It outranks the pin, so an unverified brake that
        // misses the band spends a tick with NO collision, and a no-collision tick falls
        // 0.18u against a 0.03125 band -- MEMORY law 2, one skipped tick ends the ride.
        // So sim it over three rungs of the solve's band ladder and take the first that pins.
        // An unverified brake only wins when the edge is unstoppably close, where the
        // alternative is falling off, not riding
        if (brake_now && SimsLeft() > 0)
        {
            const float A = fmaxf(sv_airaccel * tick_dt, 1e-6f);
            const float tan_yaw = RAD2DEGF(atan2f(-tan.y, -tan.x));   // straight back down the ride
            const float to_wall = NormalizeAngle(angle_wall.y - tan_yaw);
            const float sv_f = pCmd->forwardmove, sv_s = pCmd->sidemove;
            const int   sv_b = pCmd->buttons;

            static constexpr int k_brakeRungs = 3;
            static constexpr int k_brakeFrac[k_brakeRungs] = { 1, 3, 4 };   // into k_bandFrac
            for (int r = 0; r < k_brakeRungs && SimsLeft() > 0; ++r)
            {
                const float tgt = fmaxf(band_max * k_bandFrac[k_brakeFrac[r]], 0.0005f);
                const float q   = fmaxf(tgt - v_in_now, 0.f);
                const float fwd = fminf(240.f, fmaxf(fmaxf(xy_base, q), 0.05f) / A);
                const float acc = A * fwd;
                const float phi = RAD2DEGF(asinf(acc > 1e-6f ? fminf(q / acc, 1.f) : 0.f));
                const float yaw = tan_yaw + (to_wall >= 0.f ? phi : -phi);
                const float off = NormalizeAngle(yaw - angle_wall.y);
                const Vec3  d   = TB_AirAccelDelta(sweep_base_vel, yaw, fwd, sv_airaccel, tick_dt);

                apply_wish(angle_wall.y + off, fwd, duck_needed);
                UserCmd_t probe = *pCmd;
                probe.viewangles = original_view;
                pCmd->forwardmove = sv_f; pCmd->sidemove = sv_s; pCmd->buttons = sv_b;

                Restore();
                Pred_Simulate(pLocal, &probe);
                ++dbg_sims;
                const Vec3 bv = EntV3(pLocal, g_offVelocity);
                const bool pinned = fminf(fabsf(bv.z - TargetPredictZVel()),
                                          fabsf(bv.z + TargetPredictZVel())) < eps;
                Restore();

                if (!brake_valid || pinned)
                {
                    brake_fwd = fwd; brake_off = off; brake_pin = pinned;
                    brake_vin = plane_v_in(sweep_base_vel.x + d.x, sweep_base_vel.y + d.y);
                    brake_valid = true;
                }
                if (pinned) break;
            }
        }
    }

    // ---- 108c SMASH GUARD ------------------------------------------------------------------
    // How a ride actually ends: 11 held ticks at 233 u/s, then one tick with live xy = 0.00
    // and stuck = 1. Not a lost seam -- the engine zeroed the velocity, because a ride sits
    // INSIDE the 0.03125 surface extension and a step out along the wall puts the origin in
    // solid. The pin sim looks ONE tick ahead, so it green-lights the move that smashes on the
    // next. 116th -- NOT ON A SETTLED RIDE: firing on a proven hold cost all the speed it
    // claimed to save
    bool  smash = false;
    float dbg_xy2 = 0.f;
    char  act = 'H';   // B pin, E escape, K park, P press (after a pin), Q press (v_in=0 plateau),
                       // C creep, D drive-in, H handed back
    if (found_bug && caught_last_tick && xy_base > 1.f && SimsLeft() > 1
        && (s_ride_ticks < k_tbRideSettled || xy_base > k_tbSmashGuardXy))
    {
        UserCmd_t probe = *pCmd;
        probe.viewangles  = original_view;
        probe.forwardmove = best_pin.fwd;   // already CorrectMovement'd, same as the sim that pinned
        probe.sidemove    = best_pin.side;
        probe.buttons     = best_pin.buttons;
        Restore();
        Pred_Simulate(pLocal, &probe);
        Pred_Simulate(pLocal, &probe);
        dbg_xy2 = EntV3(pLocal, g_offVelocity).Length2D();
        dbg_sims += 2;
        smash = dbg_xy2 < xy_base * 0.5f;
    }

    Restore();
    pCmd->viewangles = original_view;

    // on a yaw-rotated (off-grid) wall the hold paths glue the hull into TryPlayerMove's clip
    // with no seam to pin on. CLEAN pins (act=B) still ride, those are a real surf. The park
    // is gated too: it commits best_stuck, a WALLBUG pin, so it is the branch that glues
    const bool off_grid_wall =
        fminf(fabsf(primary.plane.normal.x), fabsf(primary.plane.normal.y)) > 0.10f;
    const bool no_offgrid_hold = g_tbNoOffgridStick && off_grid_wall;
    // park run cap. 0 = hold it until the wall / support / steering ends the ride
    const int park_run_max = g_tbParkAtEnd ? (g_tbParkTicks > 0 ? g_tbParkTicks : INT_MAX)
                                           : k_tbParkRunMax;

    // ================= 3. THE COMMIT LADDER ================================
    if (found_bug && smash)
    {
        // the pin survives this tick and dies the next. Peel off the seam with a small OUTWARD
        // v_in and keep the speed; ride state stays armed, so if the wall carries on past the
        // step the very next tick pins again. No escape reachable -> hand back, never drive in
        if (esc_err < 999.f)
        {
            apply_wish(angle_wall.y + esc_angle, esc_fwd, duck_needed);
            s_tbOwnsCmd = true;
            act = 'E';
        }
        else
        {
            pCmd->forwardmove = original_forward;
            pCmd->sidemove    = original_side;
            pCmd->buttons     = original_buttons;
        }
    }
    else if (brake_valid && (brake_pin || brake_hard || !found_bug))
    {
        // Outranks the pin ON PURPOSE: the pin's move is the smallest forward that holds the
        // band, so at the edge it holds the seam one more tick and then leaves the face at
        // full speed. The brake holds the same band AND kills the tangent. It only outranks a
        // CLEAN pin once the sim above proved it pins too (brake_pin), or once the edge is
        // too close for anything else to matter (brake_hard)
        apply_wish(angle_wall.y + brake_off, brake_fwd, duck_needed);
        s_tbOwnsCmd = true;
        if (brake_pin) { s_textureBugHit = true; ride_had_clean_pin = true; }
        act = 'S';
        lock_plane();
        ride_locked = true;
    }
    else if (found_bug)
    {
        pCmd->forwardmove = best_pin.fwd;
        pCmd->sidemove    = best_pin.side;
        pCmd->buttons     = best_pin.buttons;
        s_textureBugHit   = true;
        act = 'B';
        lock_plane();
        ride_locked = true;
        ride_had_clean_pin = true;          // a real surf
        s_warm_valid = true;                // remember the winner for next tick's warm start
        s_warm_fwd   = best_pin.in_fwd;
        s_warm_angle = best_pin.angle_off;
        s_warm_duck  = (best_pin.buttons & IN_DUCK) != 0;   // effective bit, see warm pass
        s_warm_tick  = tick_now;
    }
    else if (found_stuck_pin && ride_had_clean_pin && best_stuck.have && !steer_away && !ramp_plane
             && !no_offgrid_hold && xy_base <= g_tbParkSpeed && s_tbParkRun < park_run_max)
    {
        // ---- 117th THE PARK ---------------------------------------------------------
        // This ride already surfed this plane and this tick has no clean pin. Every other
        // branch SKIPS the collision, and a no-collision tick falls ~0.18u against a 0.03125
        // band -- ~6 bands under the seam with no way back, so ONE skipped tick ends the
        // ride. The pin re-lands BECAUSE it collides and snaps z back, so take the wallbug:
        // it holds vz, and holding IS the park. 118th caps it to a low-speed bridge
        pCmd->forwardmove = best_stuck.fwd;
        pCmd->sidemove    = best_stuck.side;
        pCmd->buttons     = best_stuck.buttons;
        s_textureBugHit   = true;
        act = 'K';
        lock_plane();
        ride_locked = true;
        s_warm_valid = true;
        s_warm_fwd   = best_stuck.in_fwd;
        s_warm_angle = best_stuck.angle_off;
        s_warm_duck  = (best_stuck.buttons & IN_DUCK) != 0;
        s_warm_tick  = tick_now;
    }
    else if (found_stuck_pin)
    {
        // a wallbug pin is not a surf, just glued vz. Handing the raw cmd back is what EATS
        // THE SPEED: the player is still pushing at the wall, the origin already sits inside
        // the 0.03125 extension, and the stuck re-test zeroes the velocity (147 -> 18 -> 4 ->
        // 2 u/s, then glued). Peel OUTWARD with the speed on.
        // THE PEEL IS NOT A HOLD, so no_offgrid_hold does not gate it -- gating it made
        // off-grid walls stick HARDER (577 of 705 off-grid ticks held a wallbug pin and took
        // act=H). The option blocks the branches that ATTACH (park/press/creep/drive-in)
        bool peeled = false;
        if (xy_base > 1.f && esc_err < 999.f && !steer_away)
        {
            apply_wish(angle_wall.y + esc_angle, esc_fwd, duck_needed);
            s_tbOwnsCmd = true;
            act = 'E';
            peeled = true;
        }
        else
        {
            pCmd->forwardmove = original_forward;
            pCmd->sidemove    = original_side;
            pCmd->buttons     = original_buttons;
        }
        // 118th -- a peel that kept real speed leaves the ride ARMED: the step we peeled off
        // is one lip of the same wall and the next tick usually pins again. Capped
        if (!peeled || xy_base <= k_tbParkXyMax || s_tbEscRun >= k_tbEscRunMax)
            drop_ride();
    }
    // ---- PRESS, AHEAD OF THE CREEP (off by default, k_tbPressHold) ----------------------
    // Opening the press to every flush tick let it take over the whole approach, replacing a
    // SIM-VALIDATED move (best_hold_*) with an analytic wish that writes sidemove = 0, and
    // firing where best_hold_valid was false -- ticks TB used to HAND BACK -- taking
    // s_tbOwnsCmd with it (movement.cpp:3846 / 4160: WallClimb and AirStuck stand down on it).
    // The measurement only said vin=0.0000 on 74% of ticks, and 9.4.2 says v_in == 0 collides
    // with nothing. So press ONLY into that state and only where a creep would have run
    // anyway. v_in_now is last tick's move measured: a healthy ride reads ~band and keeps its
    // creep, the dead plateau reads 0.0000 and gets the press.
    // A/B says press OFF: every P/Q tick is an unsimmed move, vin=0, gap frozen, next tick
    // never pins -- the simmed creep holds instead
    else if (k_tbPressHold && press_err < 999.f && dbg_min_sup <= 0.1f && !steer_away && !no_offgrid_hold && !ramp_plane
             && (caught_last_tick
                 || (band_ok && best_hold_valid && best_hold_support < g_tbHugRange
                     && v_in_now < press_target * 0.1f)))
    {
        // flush + no pin = the drop tick. Press into the band instead of re-picking a support
        // the sweep cannot improve. Owns the cmd, so grace/lock survive like a creep. The
        // hold's own duck is kept -- apply_wish rebuilds buttons from original_buttons
        apply_wish(angle_wall.y + press_angle, press_fwd,
                   duck_needed || (best_hold_buttons & IN_DUCK) != 0);
        s_tbOwnsCmd = true;
        act = caught_last_tick ? 'P' : 'Q';   // Q = the plateau press, so the log separates them
        lock_plane();
        ride_locked = true;
    }
    else if (best_hold_valid
             && best_hold_support < g_tbHugRange * (caught_last_tick ? k_tbHugRideScale : 1.f)
             && !steer_away && !no_offgrid_hold && !ramp_plane)
    {
        // base creep = THE HOLD: every no-pin tick commits the freshest min-support micro-move
        // to keep the eps band alive. One tick of AirAccelerate moves the hull ~0.09u
        // sideways, so only many creep ticks close even half a unit. g_tbHugRange is the knob
        // for how far out the hug starts
        pCmd->forwardmove = best_hold_forward;
        pCmd->sidemove    = best_hold_side;
        pCmd->buttons     = best_hold_buttons;
        s_tbOwnsCmd = true;
        act = 'C';
        lock_plane();
        ride_locked = true;                 // creep = still our plane
    }
    else if (fabsf(original_forward) < 0.01f && fabsf(original_side) < 0.01f
             && !no_offgrid_hold && !ramp_plane)
    {
        // hands-off, wall in reach but not flush: silent drive into it. Flush by the seam tick
        // = an instant catch on touch instead of a drift wait
        apply_wish(angle_wall.y, 10.f, false);
        s_tbOwnsCmd = true;
        act = 'D';
    }
    else
    {
        pCmd->forwardmove = original_forward;
        pCmd->sidemove    = original_side;
        pCmd->buttons     = original_buttons;
    }

    // ---- bookkeeping ----------------------------------------------------------------------
    // 106th hold-state coyote time. A held tick re-arms it, a miss burns one. drop_ride()
    // zeroes it, so ground / death / no wall / steer-off end the ride the same tick
    s_ride_grace = (s_textureBugHit || s_tbOwnsCmd) ? k_tbRideGrace
                                                    : (s_ride_grace > 0 ? s_ride_grace - 1 : 0);
    // 116th -- pinned ticks in a row. Only a REAL pin counts: a creep/press hold keeps the
    // ride alive but does not prove it holds, which is what the smash stand-down asks
    s_ride_ticks = s_textureBugHit ? s_ride_ticks + 1 : 0;
    // 118th -- park/peel are BRIDGES: counted here so the ladder can cap them next tick
    s_tbParkRun = (act == 'K') ? s_tbParkRun + 1 : 0;
    s_tbEscRun  = (act == 'E') ? s_tbEscRun + 1 : 0;

    s_tbRideLen = s_ride_ticks;
    s_tbRideHold = s_ride_grace;
    s_tbIndD    = band_max;
    s_tbIndVin  = v_in_now;
    s_tbIndAim  = press_target;
    s_tbPhase = act == 'B' ? (s_ride_ticks >= 2 ? TBP_RIDE : TBP_PIN)
              : (act == 'K' || act == 'S') ? TBP_PARK
              : act == 'E' ? TBP_ARM
              : (act == 'P' || act == 'C' || act == 'D') ? TBP_HUG
              : TBP_SCAN;

    // per wall-tick sweep summary. stuck=1 = committed a wallbug pin, the sweep found no clean
    // pin anywhere. pin=1 stuck=0 = a real texturebug ride. act= is the branch that took it
    if (debug && DiagOk(s_tbDiagTick, act != 'H' || found_bug || found_stuck_pin))
        DebugLog("TB: act=%c vz=%.1f mindz=%.3f alivedz=%.3f minsup=%.3f hsup=%.3f pin=%d stuck=%d "
                 "sims=%d fast=%d fok=%d dup=%d sol=%d solf=%.4f fan=%d fang=%.0f pins=%d warm=%d "
                 "mirror=%d dual=%d ride=%d duck=%d lock=%d grace=%d rt=%d rej=%d xy=%.2f xyb=%.0f "
                 "keep=%d pinxy=%.1f pina=%.2f pinf=%.3f vin=%.4f vt=%.4f bmax=%.3f ptgt=%.4f "
                 "pvin=%.4f pfwd=%.3f evin=%.4f cfwd=%.3f smash=%d xy2=%.1f nz=%.2f sz=%.1f "
                 "pk=%d er=%d resc=%d supf=%d tr=%d us=%lld cap=%d og=%d nx=%.2f ny=%.2f rsrch=%d "
                 "dr=%d gap=%.5f room=%.5f dface=%.1f duse=%.1f dsrc=%d bok=%d run=%.1f brk=%d "
                 "bfwd=%.1f boff=%.1f bvin=%.4f bpin=%d bhard=%d hbr=%.1f dhb=%.1f "
                 "vent=%.4f vgr=%.4f vbk=%.4f u=%.2e rty=%d rtp=%d cls=%d plo=%.4f",
                 act, vel_now.z, dbg_min_dz, dbg_min_dz_alive, dbg_min_sup, best_hold_support,
                 (int)found_bug, (int)(!found_bug && found_stuck_pin), dbg_sims, dbg_fast,
                 (int)fast.ok, dbg_dup, dbg_solve, dbg_solve_fwd, dbg_fan, dbg_fan_ang, dbg_pins,
                 dbg_warm, dbg_mirror, (int)dual_sign, (int)caught_last_tick, (int)duck_needed,
                 (int)ride_locked, s_ride_grace, s_ride_ticks, dbg_rej, vel_now.Length2D(), xy_base,
                 (int)(found_bug && best_pin.keeps_xy), dbg_pin_xy, best_pin.angle_off,
                 best_pin.in_fwd, v_in_now, v_target, band_max, press_target, press_vin, press_fwd,
                 esc_vin, best_hold_in_fwd, (int)smash, dbg_xy2, plane_nz, brush_s_z,
                 s_tbParkRun, s_tbEscRun, rescue_scan, s_tbSupportFallback, s_tracesThisCmd,
                 TbBudgetUsedUs(), (int)(SimsLeft() <= 0), (int)off_grid_wall,
                 primary.plane.normal.x, primary.plane.normal.y, (int)ride_search, dbg_duck_rej,
                 gap, room, face_s_z, D_use, d_src, (int)band_ok, dbg_runout, (int)brake_valid,
                 brake_fwd, brake_off, brake_vin, (int)brake_pin, (int)brake_hard,
                 face_bot_h, D_hb, v_enter, v_enter_greedy, v_enter_brake, ulp_step, dbg_rty, dbg_rtp,
                 dbg_close, gap / fmaxf(tick_dt, 1e-6f));
}

// ============================================================================
// HEAD SURF -- coldtires head_surf(), ported whole
// ----------------------------------------------------------------------------
// The mirror of the wall pin: a flat brush BOTTOM above the head clips a rise and the engine
// holds real vz at +halfgravity while the surf lasts. No own toggle -- the TB bind drives it,
// it runs right after TextureBug and may TAKE OVER a TB-owned tick (a ceiling ride outranks a
// wall pin), which is the "stuck -> headsurf" transition.
// The +halfgrav accept is CORRECT, do not "fix" it: head_surf accepts +half, head_bounce
// lands on -half. Opposite on purpose
// ============================================================================
static void HeadSurf(void* pLocal, UserCmd_t* pCmd)
{
    static bool  s_hs_locked = false;
    static float s_hs_cached_forward = -1.f;
    static float s_hs_cached_angle_offset = 0.f;
    static bool  s_hs_cached_duck = false;
    static float s_hs_locked_forward = 0.f, s_hs_locked_side = 0.f;
    static bool  s_hs_locked_duck = false;
    static bool  s_hs_surfing_now = false;
    static bool  s_hs_prev_surf_vel = false;
    static int   s_hs_cold_skip = 0;
    static int   s_hs_surf_ticks = 0;     // consecutive hit ticks -- the coast gate
    static int   s_hs_coast_left = 0;

    s_hs_surf_ticks = s_headSurfHit ? s_hs_surf_ticks + 1 : 0;
    s_headSurfHit = false;

    if (!On(g_textureBug, g_textureBugKey, g_textureBugMode) || !pCmd || !EntAlive(pLocal))
        { s_hs_locked = false; s_hs_cached_forward = -1.f; s_hs_coast_left = 0; return; }

    // TB caught a wall surf this tick. Do NOT hard-return: if a ceiling is in reach and the pin
    // window is open, HS takes over. A miss restores TB's move untouched
    const bool tb_owned = s_textureBugHit;
    const bool debug    = g_debugLogEnable;
    const float half_grav = TB_HalfGravityPerTick();

    if (EntI(pLocal, g_offFlags) & FL_ONGROUND)
        { s_hs_cached_forward = -1.f; s_hs_surfing_now = false; s_hs_locked = false; return; }

    const int move_type = EntMoveType(pLocal);
    if (move_type == MOVETYPE_NOCLIP
        || move_type == MOVETYPE_FLY || move_type == MOVETYPE_OBSERVER)
        return;

    const Vec3 start_pos = EntV3(pLocal, g_offOrigin);
    const Vec3 mins = EntV3(pLocal, g_offMins);
    const Vec3 maxs = EntV3(pLocal, g_offMaxs);
    // cold reach scales with rise speed: a fast rise crosses the catch window in 1-2 ticks
    const float vz_now = EntV3(pLocal, g_offVelocity).z;
    const float reach  = s_hs_surfing_now
        ? 12.f : fminf(8.f + fmaxf(vz_now, 0.f) * TickInterval() * 3.f, 30.f);

    GameTrace_t ceil_trace{};
    // 119th -- the same ramp rule as TextureBug, ceiling side. The base gate nz < -0.7 also
    // admits the UNDERSIDE of a 45 deg surf ramp, and grabbing one locks the cmd and eats the
    // strafe like the wall pin did. A head-surf brush bottom is flat (nz -1.00)
    const bool ceiling_ok = TB_DetectCeiling(pLocal, start_pos, mins, maxs, reach, ceil_trace)
                         && ceil_trace.plane.normal.z <= -(1.f - k_hsRampNz);
    if (debug && DiagOk(s_hsDiagTick, ceiling_ok))
        DebugLog("HS: ceil=%d nz=%.3f frac=%.3f reach=%.1f vz=%.1f gate=%d tbown=%d surft=%d "
                 "lock=%d coast=%d cold=%d",
                 (int)ceiling_ok, ceil_trace.plane.normal.z, ceil_trace.fraction, reach, vz_now,
                 (int)(vz_now > 3.f * half_grav), (int)tb_owned, s_hs_surf_ticks,
                 (int)s_hs_locked, s_hs_coast_left, s_hs_cold_skip);

    if (!ceiling_ok)
    {
        if (tb_owned) { s_hs_locked = false; return; }   // wall surf stands, nothing overhead
        // ceiling seam: an established surf coasts on the locked move -- scan gaps between
        // flush brushes dropped real surfs
        if (s_hs_surf_ticks >= 2 && s_hs_coast_left > 0)
        {
            --s_hs_coast_left;
            pCmd->forwardmove = s_hs_locked_forward;
            pCmd->sidemove    = s_hs_locked_side;
            if (s_hs_locked_duck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
            s_headSurfHit = true;
            return;
        }
        s_hs_cached_forward = -1.f;
        s_hs_surfing_now = false;
        s_hs_locked = false;
        s_hs_cold_skip = 0;
        return;
    }

    {
        const float vz = EntV3(pLocal, g_offVelocity).z;
        const bool surf_vel = TB_IsPositiveHalfGravity(vz, half_grav, 1.5f);
        s_hs_surfing_now   = surf_vel && s_hs_prev_surf_vel;
        s_hs_prev_surf_vel = surf_vel;
    }

    // a pin needs upward motion into the ceiling (or an active surf): vz below 3x halfgrav is
    // falling away and cannot pin, so skip the sweep budget on those ticks
    if (!s_hs_surfing_now && vz_now <= 3.f * half_grav)
    {
        if (!tb_owned) { s_hs_cached_forward = -1.f; s_hs_locked = false; }
        return;
    }

    const Vec3  original_view    = pCmd->viewangles;
    const float original_forward = pCmd->forwardmove;
    const float original_side    = pCmd->sidemove;
    const int   original_buttons = pCmd->buttons;

    // locked: the engine pins real vz to +halfgravity while the surf holds -- replay verbatim
    if (s_hs_locked)
    {
        if (TB_IsPositiveHalfGravity(EntV3(pLocal, g_offVelocity).z, half_grav, 3.f))
        {
            pCmd->viewangles  = original_view;
            pCmd->forwardmove = s_hs_locked_forward;
            pCmd->sidemove    = s_hs_locked_side;
            if (s_hs_locked_duck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
            s_headSurfHit   = true;
            s_textureBugHit = false;      // HS owns the tick
            s_hs_surfing_now = true;
            s_hs_coast_left  = 3;
            return;
        }
        s_hs_locked = false;    // slipped -- re-catch this tick
        s_hs_cold_skip = 0;     // never let a stale cooldown block the re-catch sweep
    }

    Vec3 ceil_angle = original_view;
    {
        const Vec3 n_xy(ceil_trace.plane.normal.x, ceil_trace.plane.normal.y, 0.f);
        if (n_xy.Length2D() > 0.05f)
        {
            Vec3 into_c = n_xy * -1.f;
            ceil_angle = VectorAngles(into_c);
            ClampAnglesMv(ceil_angle);
        }
    }

    const int nBasePredicted = PredCommandsPredicted();
    const auto RestoreOriginal = [&]() { RestoreToFrame(nBasePredicted - 1); };

    // shared pool slice: a full hunt when free, a capped probe when TB already owns the tick
    int budget = SimsLeft();
    if (budget > 160) budget = 160;
    if (tb_owned && budget > 60) budget = 60;
    if (budget < 0) budget = 0;
    int sims_used = 0;

    const auto try_config = [&](float forward, float angle_offset, bool duck) -> bool
    {
        if (sims_used >= budget) return false;
        ++sims_used;
        RestoreOriginal();
        Vec3 wish_angle = original_view;
        wish_angle.y = ceil_angle.y + angle_offset;
        pCmd->viewangles = original_view;
        pCmd->buttons    = original_buttons;
        if (duck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
        pCmd->forwardmove = forward;
        pCmd->sidemove    = 0.f;
        CorrectMovement(pCmd, wish_angle, original_view);
        Pred_Simulate(pLocal, pCmd);
        return TB_IsPositiveHalfGravity(EntV3(pLocal, g_offVelocity).z, half_grav, 0.5f);
    };

    const auto commit = [&](float cfg_forward, float cfg_offset, bool duck)
    {
        RestoreOriginal();     // pCmd already holds the winning silent move
        pCmd->viewangles = original_view;
        s_hs_cached_forward      = cfg_forward;
        s_hs_cached_angle_offset = cfg_offset;
        s_hs_cached_duck         = duck;
        s_hs_locked         = true;
        s_hs_locked_forward = pCmd->forwardmove;
        s_hs_locked_side    = pCmd->sidemove;
        s_hs_locked_duck    = duck;
        s_headSurfHit    = true;
        s_textureBugHit  = false;    // takeover: HS owns the tick now
        s_hs_surfing_now = true;
        s_hs_cold_skip   = 0;
        s_hs_coast_left  = 3;
        if (debug)
            DebugLog("HS LOCK: fwd=%.2f ang=%.1f duck=%d sims=%d/%d vz=%.1f",
                     cfg_forward, cfg_offset, (int)duck, sims_used, budget,
                     EntV3(pLocal, g_offVelocity).z);
    };

    // does the pin sustain N more sim ticks? (pCmd still holds the tested move after try_config)
    const auto sustain_ticks = [&](int n_max) -> int
    {
        int n = 0;
        for (; n < n_max && sims_used < budget; ++n)
        {
            ++sims_used;
            Pred_Simulate(pLocal, pCmd);
            if (!TB_IsPositiveHalfGravity(EntV3(pLocal, g_offVelocity).z, half_grav, 0.5f)) break;
        }
        return n;
    };

    // natural pass: the user's own cmd already headsurf-pins -> keep it (manual play wins).
    // INITIAL catch only. On a TB-owned tick pCmd holds TB's wall move, not the user's
    if (!tb_owned && s_hs_surf_ticks == 0
        && (fabsf(original_forward) > 0.01f || fabsf(original_side) > 0.01f) && budget > 0)
    {
        RestoreOriginal();
        UserCmd_t natural = *pCmd;
        ++sims_used;
        Pred_Simulate(pLocal, &natural);
        const bool nat_pin = TB_IsPositiveHalfGravity(EntV3(pLocal, g_offVelocity).z, half_grav, 0.5f);
        RestoreOriginal();
        if (nat_pin)
        {
            pCmd->viewangles  = original_view;
            pCmd->forwardmove = original_forward;
            pCmd->sidemove    = original_side;
            pCmd->buttons     = original_buttons;
            s_headSurfHit = true;
            s_hs_surfing_now = true;
            s_hs_locked = false;      // the user owns it -- no verbatim replay of user input
            s_hs_coast_left = 3;
            if (debug) DebugLog("HS: natural pin vz=%.1f", EntV3(pLocal, g_offVelocity).z);
            return;
        }
    }

    // fast path: last tick's winning config (1 sim)
    if (s_hs_cached_forward >= 0.f
        && try_config(s_hs_cached_forward, s_hs_cached_angle_offset, s_hs_cached_duck))
        { commit(s_hs_cached_forward, s_hs_cached_angle_offset, s_hs_cached_duck); return; }

    // cold skip only while falling -- rising = the catch window is 1-2 ticks out, and skipping
    // ate catches
    if (s_hs_cold_skip > 0 && vz_now <= 0.f)
    {
        --s_hs_cold_skip;
        RestoreOriginal();
        pCmd->viewangles  = original_view;
        pCmd->forwardmove = original_forward;
        pCmd->sidemove    = original_side;
        pCmd->buttons     = original_buttons;
        return;
    }

    const Vec3  vel_hs = EntV3(pLocal, g_offVelocity);
    const float vel_yaw_hs = RAD2DEGF(atan2f(vel_hs.y, vel_hs.x));
    const float rel_yaw_hs = NormalizeAngle(vel_yaw_hs - ceil_angle.y);
    const float hs_sign    = (rel_yaw_hs >= 0.f) ? -1.f : 1.f;

    static constexpr float k_hs_fwd[] =
        { 0.f, 0.1f, 0.25f, 0.5f, 0.75f, 1.f, 1.5f, 2.125f, 2.875f, 5.f, 10.f, 20.f, 30.f };
    constexpr float coarse_step = 5.f, fine_step = 1.f;

    float sh_fwd = -1.f, sh_ang = 0.f;
    bool  sh_duck = false;
    int   sh_sustain = -1;
    float sh_move_fwd = 0.f, sh_move_side = 0.f;   // saved silent move -- commit must not need budget

    // mid-surf re-catch tries the CURRENT duck state first (first pin wins)
    const bool first_duck = (s_hs_surf_ticks > 0 || s_hs_locked) ? s_hs_locked_duck : false;
    for (const float fwd : k_hs_fwd)
    {
        if (sims_used >= budget) break;
        for (int i_duck = 0; i_duck <= 1 && sims_used < budget; ++i_duck)
        {
            const bool duck = (i_duck == 0) ? first_duck : !first_duck;
            for (float ang = 0.f; ang < 90.f && sims_used < budget; ang += coarse_step)
            {
                if (!try_config(fwd, ang * hs_sign, duck)) continue;

                // coarse hit -- save its silent move (try_config is deterministic from the
                // pinned restore frame, so no re-sim is needed after a failed refinement)
                const float coarse_fwd  = pCmd->forwardmove;
                const float coarse_side = pCmd->sidemove;

                // durability: the pin must survive 2 more ticks -- 1-tick flukes poison the lock
                // and the fast path (the same lesson as TB); the shallow best stays as a fallback
                const int sus = sustain_ticks(2);
                if (sus < 2)
                {
                    if (sus > sh_sustain)
                    {
                        sh_sustain = sus; sh_fwd = fwd; sh_ang = ang * hs_sign; sh_duck = duck;
                        sh_move_fwd = coarse_fwd; sh_move_side = coarse_side;
                    }
                    continue;
                }

                for (float d = fine_step; d <= coarse_step; d += fine_step)      // refine around it
                    for (int s = 0; s <= 1; ++s)
                    {
                        const float fa = ang + (s ? d : -d);
                        if (fa < 0.f) continue;
                        if (try_config(fwd, fa * hs_sign, duck))
                            { commit(fwd, fa * hs_sign, duck); return; }
                    }

                // refinement found nothing better -- replay the saved coarse move
                pCmd->forwardmove = coarse_fwd;
                pCmd->sidemove    = coarse_side;
                pCmd->buttons     = original_buttons;
                if (duck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
                commit(fwd, ang * hs_sign, duck);
                return;
            }
        }
    }

    // no durable config -> the best shallow pin still beats nothing. NOT on a TB-owned tick:
    // a takeover clears the wall hit, so a 1-tick HS fluke would trade an established wall surf
    // for nothing. Stealing TB's tick demands a DURABLE pin
    if (sh_sustain >= 0 && !tb_owned)
    {
        RestoreOriginal();
        pCmd->viewangles  = original_view;
        pCmd->forwardmove = sh_move_fwd;
        pCmd->sidemove    = sh_move_side;
        pCmd->buttons     = original_buttons;
        if (sh_duck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
        commit(sh_fwd, sh_ang, sh_duck);
        return;
    }

    // established surf, momentary miss -> coast on the locked move (band drift re-pins)
    if (!tb_owned && s_hs_surf_ticks >= 2 && s_hs_coast_left > 0)
    {
        --s_hs_coast_left;
        RestoreOriginal();
        pCmd->viewangles  = original_view;
        pCmd->forwardmove = s_hs_locked_forward;
        pCmd->sidemove    = s_hs_locked_side;
        pCmd->buttons     = original_buttons;
        if (s_hs_locked_duck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
        s_headSurfHit = true;
        return;
    }

    // full miss: hand movement back (on a TB-owned tick the originals ARE TB's committed move,
    // so the wall surf survives the failed takeover untouched), then cool down
    RestoreOriginal();
    pCmd->viewangles  = original_view;
    pCmd->forwardmove = original_forward;
    pCmd->sidemove    = original_side;
    pCmd->buttons     = original_buttons;
    s_hs_cached_forward = -1.f;
    s_hs_cold_skip = 1;     // the catch window is 1-2 ticks; a long cooldown ate headsurfs
    if (debug)
        DebugLog("HS MISS: sus=%d sims=%d/%d vz=%.1f nz=%.2f", sh_sustain, sims_used, budget,
                 vz_now, ceil_trace.plane.normal.z);
}

// ============================================================================
// HEAD BOUNCE -- coldtires head_bounce(), ported whole (2026-08-17 / 123rd)
// ----------------------------------------------------------------------------
// HeadSurf RIDES flat brush ceilings. A notch between two DISPLACEMENTS fails all its gates
// -- both faces are tilted, and one tilted face slides the rise off instead of pinning it.
// A CREASE is different: TryPlayerMove's two-plane branch projects velocity onto the crease
// line, a horizontal crease zeroes vz, and FinishGravity puts back one half step, landing on
// -halfgravity.
// So this is a CATCH only: no plane lock, no coast, no duck latch, no hold, every tick
// re-derived from scratch. A tick that verifies nothing hands the cmd back byte-identical.
// 123rd -- the accept is the CUT off free fall (any size), not the full crease, and the
// chain is COUNTED to the notch instead of fixed at 3
// ============================================================================
static void HeadBounce(void* pLocal, UserCmd_t* pCmd)
{
    s_headBounceHit = false;

    if (!On(g_textureBug, g_textureBugKey, g_textureBugMode)) return;
    if (!pCmd || !EntAlive(pLocal)) return;
    // a wall pin or a ceiling ride already owns the tick -- both outrank a one-tick clip
    if (s_textureBugHit || s_headSurfHit) return;
    if (EntI(pLocal, g_offFlags) & FL_ONGROUND) return;
    if (EntMoveType(pLocal) != MOVETYPE_WALK) return;

    const float half = TargetPredictZVel();   // NEGATIVE
    const float vz_now = EntV3(pLocal, g_offVelocity).z;
    // must be RISING into it -- a falling hull cannot have its rise cut. The one entry speed
    // where gravity alone fakes the bounce is filtered per chain tick instead (step_result)
    if (vz_now <= 0.f) return;

    const Vec3  start_pos = EntV3(pLocal, g_offOrigin);
    const Vec3  mins = EntV3(pLocal, g_offMins);
    const Vec3  maxs = EntV3(pLocal, g_offMaxs);
    const float dt   = TickInterval();
    const bool  debug = g_debugLogEnable;

    // only as far as the chain could rise, plus a nub margin -- anything higher is out of
    // this search's reach and probing for it only costs traces
    const float reach = fminf(4.f + vz_now * dt * k_hbChainMax, 48.f);
    GameTrace_t over{};
    TbOverDbg_t odbg{};
    if (!TB_DetectOverhead(pLocal, start_pos, mins, maxs, reach, over, odbg))
    {
        if (debug && DiagOk(s_hbDiagTick, false))
            DebugLog("HB: none reach=%.1f vz=%.1f frac0=%.3f nz0=%.2f ss=%d retry=%d",
                     reach, vz_now, odbg.frac0, odbg.nz0, (int)odbg.ss0, (int)odbg.retried);
        return;
    }

    const Vec3  original_view    = pCmd->viewangles;
    const float original_forward = pCmd->forwardmove;
    const float original_side    = pCmd->sidemove;
    const int   original_buttons = pCmd->buttons;

    const int nBase = PredCommandsPredicted();
    const auto RestoreOriginal = [&]() { RestoreToFrame(nBase - 1); };

    int budget = SimsLeft();
    if (budget > k_hbSimCap) budget = k_hbSimCap;
    if (budget <= 0) return;
    int sims_used = 0;

    // the tick's own numbers, read off the restore base -- every variant starts here
    RestoreOriginal();
    const Vec3  base_vel = EntV3(pLocal, g_offVelocity);
    const float base_vz  = base_vel.z;
    const float xy_base  = base_vel.Length2D();
    // a bounce that costs half the speed is not a catch, it is a crash into the ceiling
    const float xy_floor = xy_base >= 1.f ? xy_base * k_tbXyCreepFrac : 0.f;
    const float vz_free  = base_vz + 2.f * half;
    const float sv_airaccel = CvarFloat("sv_airaccelerate", 10.f);

    // THE COST GATE. A wish cannot change vz, so whether the head reaches the notch is
    // decided before any sim: the hull rises (vz + half) * dt. DUCK cannot help either -- in
    // air FinishDuck moves the origin up by the hull delta and FinishUnDuck back down, so the
    // hull TOP is invariant and only the feet move. 123rd: the tick count is COUNTED, not
    // assumed (vz_i = base_vz + 2*half*(i-1), exact). Peaking below the notch returns 0
    const float gap = over.fraction * reach;
    const auto hb_ticks_to_gap = [&](float need) -> int
    {
        float climbed = 0.f, vz_i = base_vz;
        for (int i = 1; i <= k_hbChainMax; ++i)
        {
            const float step = (vz_i + half) * dt;
            if (step <= 0.f) return 0;
            climbed += step;
            if (climbed >= need) return i;
            vz_i += 2.f * half;
        }
        return 0;
    };
    const int need_ticks = hb_ticks_to_gap(gap - k_hbGapMargin);
    if (need_ticks <= 0)
    {
        if (debug && DiagOk(s_hbDiagTick, false))
            DebugLog("HB: far gap=%.2f nz=%.2f vz=%.1f reach=%.1f", gap,
                     over.plane.normal.z, base_vz, reach);
        return;
    }
    // one tick past the arrival tick: the contact happens ON the tick the head reaches it, and
    // the tick after is where a graze that only slowed the rise still shows up as a cut
    int chain = need_ticks + 1;
    if (chain < k_hbChain)    chain = k_hbChain;
    if (chain > k_hbChainMax) chain = k_hbChainMax;

    // diag: the BEST any variant did, not the last one tried. cut=0.00 on a miss = nothing
    // touched the notch at all; a cut over the floor with xy under it = a real bounce vetoed
    float best_dz = 1e9f, best_vz = 0.f, best_xy = 0.f, best_cut = 0.f;

    // one simulated tick, judged against the tick it started from. THE TEST IS THE CUT, not
    // the landing value: a wish cannot change vz, so a contact-free tick lands on
    // vz_in + 2*half exactly and anything under that was taken by something overhead
    const auto step_result = [&](float vz_in) -> int
    {
        const Vec3  v = EntV3(pLocal, g_offVelocity);
        const float free_vz = vz_in + 2.f * half;
        const float cut = free_vz - v.z;
        const float dz  = fabsf(v.z - half);
        if (cut > best_cut) { best_cut = cut; best_vz = v.z; best_xy = v.Length2D(); }
        if (dz < best_dz) best_dz = dz;
        // landing eats the rise the same way a ceiling does -- that is a floor, not a bounce
        if (EntI(pLocal, g_offFlags) & FL_ONGROUND) return -1;
        if (cut > k_hbCutMin) return v.Length2D() >= xy_floor ? 1 : -1;
        // still climbing -> the chain may still reach the notch; falling -> it never will
        return v.z > 0.f ? 0 : -1;
    };

    const auto log_result = [&](const char* how)
    {
        if (!debug) return;
        DebugLog("HB %s: nz=%.2f gap=%.2f vz=%.1f->%.1f free=%.1f tgt=%.2f cut=%.2f dz=%.2f "
                 "xy=%.0f->%.0f/%.0f chain=%d/%d sims=%d/%d retry=%d",
                 how, over.plane.normal.z, gap, base_vz, best_vz, vz_free, half, best_cut,
                 best_dz, xy_base, best_xy, xy_floor, need_ticks, chain, sims_used, budget,
                 (int)odbg.retried);
    };

    // NATURAL FIRST -- the player's own cmd already clips the notch, so keep their input and
    // just publish the catch. The same rule HeadSurf uses: manual play wins
    {
        RestoreOriginal();
        UserCmd_t natural = *pCmd;
        bool nat = false;
        for (int i = 0; i < chain && sims_used < budget; ++i)
        {
            const float vz_in = EntV3(pLocal, g_offVelocity).z;
            ++sims_used;
            Pred_Simulate(pLocal, &natural);
            const int r = step_result(vz_in);
            if (r != 0) { nat = (r > 0); break; }
        }
        RestoreOriginal();
        pCmd->viewangles  = original_view;
        pCmd->forwardmove = original_forward;
        pCmd->sidemove    = original_side;
        pCmd->buttons     = original_buttons;
        if (nat) { s_headBounceHit = true; log_result("natural"); return; }
    }

    // wish directions relative to TRAVEL, not to the ceiling normal: on a crease the useful nudge
    // is sideways off the path, and a near-flat underside has no usable xy normal at all
    const float base_yaw = (xy_base > 1.f) ? RAD2DEGF(atan2f(base_vel.y, base_vel.x))
                                           : original_view.y;
    static constexpr float k_hb_yaw[] =
        { 0.f, 15.f, -15.f, 30.f, -30.f, 60.f, -60.f, 90.f, -90.f, 120.f, -120.f, 150.f, -150.f, 180.f };
    // 124th -- AirAccelerate caps only ADDSPEED at 30; accelspeed = accel * wishspeed * dt
    // uses the UNCAPPED wishspeed. So fwd is the whole authority, not a fine-tuning axis:
    // fwd 1 -> 0.19 u/s a tick, fwd 450 -> the entire addspeed in ONE tick. Descending, since
    // ascending order spent every sim on the 0.19 rows
    static constexpr float k_hb_fwd[] = { 450.f, 60.f, 15.f };
    // NO duck variant: an air duck moves the origin and the hull by the same delta, so the
    // head top does not move and the notch stays as far away (see the cost gate). The duck is
    // the edgebug/pixel lever -- it moves the FEET

    TbDedup_t dedup{};
    const auto try_variant = [&](float fwd, float yaw_off) -> bool
    {
        if (sims_used >= budget) return false;
        const float world_yaw = NormalizeAngle(base_yaw + yaw_off);
        const Vec3  delta = TB_AirAccelDelta(base_vel, world_yaw, fwd, sv_airaccel, dt);
        // 124th -- a provable NO-OP used to cost a whole chain: addspeed = 30 - vel.wishdir,
        // so at 198 u/s every yaw inside ~81 deg of travel returns a zero delta
        if (delta.Length2D() < k_tbDupVelEps) return false;
        if (dedup.SeenOrAdd(delta)) return false;
        RestoreOriginal();
        pCmd->viewangles  = original_view;
        pCmd->buttons     = original_buttons;
        pCmd->forwardmove = fwd;
        pCmd->sidemove    = 0.f;
        Vec3 wish = original_view; wish.y = world_yaw;
        CorrectMovement(pCmd, wish, original_view);
        pCmd->viewangles = original_view;
        // hold the SAME cmd for the chain: the view is fixed across the sims, so replaying it
        // is the same world wish every tick (same as HeadSurf's sustain_ticks)
        for (int i = 0; i < chain && sims_used < budget; ++i)
        {
            const float vz_in = EntV3(pLocal, g_offVelocity).z;
            ++sims_used;
            Pred_Simulate(pLocal, pCmd);
            const int r = step_result(vz_in);
            if (r != 0) return r > 0;
        }
        return false;
    };

    for (const float fwd : k_hb_fwd)
        for (const float yaw_off : k_hb_yaw)
        {
            if (sims_used >= budget) break;
            if (!try_variant(fwd, yaw_off)) continue;
            // pCmd already holds the winning silent move -- restore the entity, commit as is
            RestoreOriginal();
            pCmd->viewangles = original_view;
            s_headBounceHit  = true;
            log_result("sweep");
            return;
        }

    // miss: the cmd goes back exactly as the player wrote it
    RestoreOriginal();
    pCmd->viewangles  = original_view;
    pCmd->forwardmove = original_forward;
    pCmd->sidemove    = original_side;
    pCmd->buttons     = original_buttons;
    log_result("miss");
}
