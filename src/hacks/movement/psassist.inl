// ============================================================================
// psassist.inl -- pixel finder + PIXEL SURF ASSIST (1:1 Aletherium port)
//
// Included from movement.cpp (same pattern as texturebug.inl) so it keeps the
// file-static prediction helpers: Pred_Simulate / RestoreToFrame / SimsLeft /
// EntV3 / TraceLine / s_prePredFlags / s_psRidingNow. (s_prePredVel is no
// longer read here -- the assist has no velocity gate left.)
//
// The finder half (PixelFinder, below) is dih's and unchanged.
// The ASSIST half follows the reference choreography exactly:
//     GridCheck -> SimJumpCatch -> PixelSurfAssist
// -- see the banner over the assist section for what dih was doing instead and
// why it never jumped. Everything ..invented is deleted.
//
// Seven types, six of them ground launches (bit k of a point's typeMask is
// aCands[k], readout order mini/regular/mini+duck/crouch/long/long+duck, bit 6
// is the scout dash):
//   REGULAR  = jump                        (~45u)
//   CROUCH   = jump, duck held after t1    (~65u)
//   MINI     = jump+duck on the launch cmd (~28u)
//   MINIDUCK = MINI + PixelSurf's descent duck (+20u at the catch)
//   LONG     = jump+duck for 3 cmds
//   LONGDUCK = LONG, duck held after t1
//   AIRDASH  = scout 2nd jump, fired from the air
//
// Points persist per map: C:otox-tf2\points\<map>.txt, "x y z [typemask]" per line
// (3-field lines from old files load as mask 127 = all types allowed).
// ============================================================================

enum { JT_NONE = 0, JT_REGULAR, JT_CROUCH, JT_MINI, JT_MINIDUCK, JT_LONG, JT_LONGDUCK, JT_AIRDASH };

// per-point allowed-jump bits, Aletherium readout order (ENTER window edits these)
static int PsTypeBit(int iType)
{
    switch (iType)
    {
    case JT_MINI:     return 0;
    case JT_REGULAR:  return 1;
    case JT_MINIDUCK: return 2;
    case JT_CROUCH:   return 3;
    case JT_LONG:     return 4;
    case JT_LONGDUCK: return 5;
    case JT_AIRDASH:  return 6;
    }
    return -1;
}

static const char* PsTypeName(int iType)
{
    static const char* kNames[] = {
        "", "regular jump", "crouch jump", "minijump", "minijump (duck)",
        "longjump", "longjump (duck)", "double jump",
    };
    return (iType >= JT_REGULAR && iType <= JT_AIRDASH) ? kNames[iType] : "jump";
}

// saved assist target + per-point settings (edited via on-point ENTER window)
struct PsAssistPt
{
    Vec3 pos = {};
    int typeMask = 127; // allowed jump types, bits in PsTypeBit order
    // per-point engage radius override. 0 = follow g_psAssistRadius. delusional keeps
    // its spot settings ON the spot for exactly this reason: one global distance cannot serve a
    // seam in a corridor (where a wide radius means the assist engages through a wall) and a
    // seam on an open skybox ledge (where a tight one means it never engages at all)
    float radius = 0.f;
    // ---- THE WALL NORMAL, RESOLVED ONCE AND CACHED (runtime only, not in the file) ----
    // The confirm's flush test needs to know which way the wall faces, because the hull's
    // support distance IS |maxs.x*n.x| + |maxs.y*n.y| -- 24 axis-aligned, 33.9 at 45 degrees.
    // recovered n from the origin->point direction, which is circular (the offset is
    // along the normal only AT a catch, which is the thing being tested) and the log
    // shows what it cost: both fires read sup33.9 on an axis-aligned wall, so 36.0 of open air
    // passed as "flush". Re-tracing the wall at pick time answers it directly, works on the
    // points already saved (the file has no normal field), and re-asks every tick until it
    // gets a clean answer, so a bad approach angle heals itself
    Vec3 normal = {};
    bool hasNormal = false;
};

// Shared with the render thread (PS_GetDrawData copies under this lock).
static std::mutex g_psLock;
static std::vector<Vec3> s_psFinderPts;        // finder results, current map only
static std::vector<Vec3> s_psTbPts;            // texturebug seams on the scanned column (red)
// whole-map scan output (psmap.inl): confirmed surf segments, A then B, one pair per line
static std::vector<Vec3> s_psMapLines;
// ...and the texturebug seams the same walk found. Its OWN list, not s_psTbPts: a column scan
// overwrites that one on every release, and a whole-map scan takes minutes to produce this. Both
// are published as red rings and both place a TEXTUREBUG marker (routecalc.inl RcPickFinder)
static std::vector<Vec3> s_psMapTbPts;
static std::vector<PsAssistPt> s_psAssistPts;  // saved assist targets, current map only
static unsigned s_psGen = 0;                   // bumped on load/add/erase -- open ENTER window closes on change
static char s_psMap[128] = {};            // map the lists belong to ("" = none loaded)
static int  s_psAssistActive = -1;        // assist target index for the draw (-1 = none)
static Vec3 s_psLocalOrigin = {};
static unsigned long long s_psAliveMs = 0; // last movement tick that refreshed state

// finder scan state
static bool s_psFinderHeld = false;  // key state last tick (edge detect; keeps needPred up on release)
static bool s_psFinderFirst = false; // scan column anchored this hold
static Vec3 s_psFinderStart = {}, s_psFinderEnd = {}, s_psFinderNormal = {};

// ---- the scan is CHUNKED, cursor lives here (see PixelFinder) ----
// One cmd used to run the whole column: 601 heights x 9 lateral offsets x 2 duck states x up to
// 12 Pred_Simulate = ~130k engine movement ticks in one frame. The work is now split at the
// lateral-offset boundary and spends at most g_psFinderBudget ms per cmd
static bool  s_pfBusy = false;       // scan in flight (also keeps needPred up -- movement.cpp)
static int   s_pfNextI = 0;          // next height step index
static int   s_pfNextLat = 0;        // next lateral offset index inside that height
static bool  s_pfHeightHit = false;  // this height already produced a pin (old bFoundThisHeight)
static int   s_pfSteps = 0;
static float s_pfLow = 0.f, s_pfHigh = 0.f;
static Vec3  s_pfNormal = {}, s_pfTangent = {}, s_pfColumn = {};
static int   s_pfBaseButtons = 0;    // buttons snapshot at release -- probes must not follow live keys
static std::vector<Vec3> s_pfFound;  // pins so far, pre-dedupe
static double s_pfMs = 0.0;          // wall clock actually spent scanning
static int   s_pfChunks = 0, s_pfProbes = 0, s_pfPruned = 0;
// pins found, and how many of them a FALL could actually catch (diag only)
static int   s_pfFalls = 0, s_pfConf = 0;
static int   s_pfCells = 0;          // cells actually visited (cells= vs sims= is the cost)
static int   s_pfNoStand = 0;        // standing pass skipped -- 82u hull does not fit here
// pfstrict diag: sealed = pins the hull cannot get out of (clip = of those, a clip brush blocked),
// deep = cells whose wall came from the 48u re-trace (buried start / recessed face)
static int   s_pfSealed = 0, s_pfClipBlk = 0, s_pfDeep = 0;
static int   s_pfProp = 0;           // cells skipped -- the face there is a prop, not brush
static int   s_pfVoid = 0;           // cells skipped -- the spot is inside a brush or off the map
// fall-check memo: successive heights converge on ONE settled catch, ask the fall once per catch
struct PfFallMemo { Vec3 at; bool duck; bool ok; };
static std::vector<PfFallMemo> s_pfFallMemo;

// Unit A3 port: PixelFinder's two MISSING detection sub-modes (gap.md). Own lists -- mixing
// either into s_pfFound would read as a plain pixel-surf pin to the render side.
static std::vector<Vec3> s_pfJumpPts;        // g_psFinderPixeljump: pins no FALL catches, kept instead of dropped
static std::vector<Vec3> s_pfHeadbouncePts;  // g_psFinderHeadbounce: ascent (ceiling) pins

// ---- who is driving the column scanner ------------------------------------------------------
// Two features anchor columns now: the crosshair finder below, and the whole-map scan
// (psmap.inl), which walks the map's own brush edges and hands this scanner one cell at a time.
// The probes, prunes and strict gates are the same code for both -- only the anchor differs.
// The crosshair finder has no idea WHERE on the wall a surf is, so it sweeps a +/-16u band in
// 4u steps; the map scan knows the seam's exact xy, so it sets the band to one cell.
static float s_pfBandHalf = 16.f, s_pfBandStep = 4.f;
// quiet = the map scan owns this job's result and its log line: do not publish the pins as
// crosshair-finder points, do not print a PF: line per cell, and leave s_pfFound to the caller
static bool  s_pfQuiet = false;
static void PmAbort();   // psmap.inl -- defined below fakemove.inl, same translation unit

static void PfCancelScan()
{
    s_pfBusy = false;
    s_pfNextI = 0; s_pfNextLat = 0; s_pfHeightHit = false;
    s_pfFound.clear();
    s_pfFallMemo.clear();
    s_pfJumpPts.clear(); s_pfHeadbouncePts.clear();
}

// Unit A3 port: publish the two new finder lists (copy under the render lock, same as PS_GetDrawData)
void PF_GetExtraPts(std::vector<Vec3>& jump, std::vector<Vec3>& headbounce)
{
    std::lock_guard<std::mutex> l(g_psLock);
    jump = s_pfJumpPts;
    headbounce = s_pfHeadbouncePts;
}

// IVEngineClient::GetLevelName = slot 51 (counted in the interface header against the
// confirmed anchors Time=14 / SetViewAngles=20)
static const char* EngineLevelName()
{
    if (!g_engine) return "";
    const char* n = nullptr;
    __try {
        void** vt = *(void***)g_engine;
        n = ((const char*(__fastcall*)(void*))vt[51])(g_engine);
    } __except (EXCEPTION_EXECUTE_HANDLER) { n = nullptr; }
    return n ? n : "";
}

static void PsPointsFile(const char* map, char* out, size_t n)
{
    char safe[128];
    snprintf(safe, sizeof(safe), "%s", (map && map[0]) ? map : "unknown");
    for (char* c = safe; *c; ++c)
        if (*c == '/' || *c == '\\' || *c == ':') *c = '_'; // workshop/x.ugc123 etc.
    snprintf(out, n, "%s\\points\\%s.txt", CONFIG_DIR, safe);
}

static void PsLoadPointsForMap(const char* map)
{
    std::vector<PsAssistPt> pts;
    char path[MAX_PATH];
    PsPointsFile(map, path, sizeof(path));
    if (FILE* f = fopen(path, "r"))
    {
        char line[256];
        while (fgets(line, sizeof(line), f))
        {
            PsAssistPt p;
            int m = 127;
            float r = 0.f;
            // 5th field is an earlier build's per-point radius; a 4-field line from an older build still
            // loads and simply keeps radius 0 = "use the global"
            const int n = sscanf(line, "%f %f %f %d %f", &p.pos.x, &p.pos.y, &p.pos.z, &m, &r);
            if (n >= 3)
            {
                p.typeMask = (n >= 4) ? (m & 127) : 127;
                p.radius = (n >= 5 && r > 0.f) ? r : 0.f;
                pts.push_back(p);
            }
        }
        fclose(f);
    }
    PmAbort();             // ...and the map scan's cursor points into the OLD map's brush list
    std::lock_guard<std::mutex> l(g_psLock);
    s_psAssistPts.swap(pts);
    s_psFinderPts.clear(); // finder results are per-session, drop on map change
    s_psTbPts.clear();
    s_psMapLines.clear();
    s_psMapTbPts.clear();
    PfCancelScan();        // a chunked scan is anchored to the OLD map's geometry
    s_psAssistActive = -1;
    s_psGen++;
    snprintf(s_psMap, sizeof(s_psMap), "%s", map ? map : "");
}

static void PsWritePoints()
{
    char dir[MAX_PATH], path[MAX_PATH];
    _mkdir(CONFIG_DIR);
    snprintf(dir, sizeof(dir), "%s\\points", CONFIG_DIR);
    _mkdir(dir);
    PsPointsFile(s_psMap, path, sizeof(path));
    FILE* f = fopen(path, "w");
    if (!f) return;
    for (const PsAssistPt& p : s_psAssistPts)
        fprintf(f, "%f %f %f %d %f\n", p.pos.x, p.pos.y, p.pos.z, p.typeMask, p.radius);
    fclose(f);
}

// ---- Pixel finder (hold = stretch a scan column on the aimed wall, release =
// brute-force it for pixel-surf catch heights; Aletherium PixelFinder) ----
//
// PERFORMANCE. Nothing about a probe changed; what changed is that the column no
// longer runs inside ONE cmd. The old release did the lot in a single frame:
//     601 heights x 9 lateral offsets x 2 duck states x up to 12 Pred_Simulate
// = ~130k full engine movement ticks, which is the multi-second hitch on release. Now:
//   * CHUNKED: the cursor (s_pfNextI / s_pfNextLat) survives the cmd, each cmd spends at most
//     g_psFinderBudget ms and the scan carries on next tick. Same probes, same order, same
//     points -- only the wall clock is spread out, and the game keeps running through it.
//   * REGION PRUNE: one stationary hull test, box grown to everything a probe can reach
//     (+/-6u out along the wall normal, 14u down -- the 10-tick fallback probe falls 10.2u at
//     sv_gravity 800 / 66 tick), replaces the 24 movement sims wherever the answer is "no
//     geometry here at all". Conservative by construction: nothing solid in that box means no
//     collision in any probe, so vz can never pin. Column ends, margin overhang and the air
//     above a short wall stop costing sims.
//   * results publish EVERY chunk, so spots appear on screen while the column fills in.
// Probes and prunes are counted and logged (probes= / pruned=) -- that is the evidence for
// whether a slow scan is geometry or budget.

// dedupe collapses runs of adjacent steps that converged on one catch; keep the FIRST pin
// of each run verbatim (the engine's exact settled height). Unchanged from the one-shot scan
static std::vector<Vec3> PfDedupe(const std::vector<Vec3>& vIn)
{
    if (vIn.size() <= 1) return vIn;
    std::vector<Vec3> vOut;
    vOut.push_back(vIn.front());
    float flPrevZ = vIn.front().z;
    for (size_t i = 1; i < vIn.size(); i++)
    {
        if (fabsf(vIn[i].z - flPrevZ) > 1.5f)
            vOut.push_back(vIn[i]);
        flPrevZ = vIn[i].z;
    }
    return vOut;
}

// one budgeted slice of the anchored column
static void PfScanChunk(void* pLocal, UserCmd_t* pCmd)
{
    // QPF is fixed for the life of the process -- asking once per chunk is a syscall-shaped
    // call inside the thing this function exists to budget.
    static LARGE_INTEGER s_liFreq = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER liStart{}, liNow{};
    QueryPerformanceCounter(&liStart);
    // THE CHUNK MUST FIT IN A TICK. The slider was a raw ms number with a 32 max and
    // the cfg had it at 32: a tick is 15.15 ms at 66, so every chunk overran the whole command by
    // 2x. The game cannot make cmds while we hold the movement thread, so the scan bought its
    // speed straight out of the frame rate -- 15 chunks x 32 ms is the half second of stutter in
    // the log. Half a tick leaves the rest of the cmd its own time; the slider stays the ceiling,
    // never the floor, and `eff=` on the scan line is what actually ran
    const double flTickMs = (double)TickInterval() * 1000.0;
    const double flBudgetMs = std::min((double)std::clamp(g_psFinderBudget, 0.25f, 32.f),
                                       flTickMs > 0.0 ? flTickMs * 0.5 : 7.5);
    auto ElapsedMs = [&]() -> double
    {
        QueryPerformanceCounter(&liNow);
        return double(liNow.QuadPart - liStart.QuadPart) * 1000.0 / double(s_liFreq.QuadPart);
    };

    TraceFilterWorldProps filter; filter.Setup(pLocal);

    // tick-rate-correct surf velocity: engine pins vz to -halfgravity-per-tick while riding
    const float flHalfGrav = CvarFloat("sv_gravity", 800.f) * TickInterval() * 0.5f;
    constexpr float flSurfTol = 1.5f; // same pin tolerance PixelSurf uses

    const int iBackupButtons = pCmd->buttons;
    const float flBackupForward = pCmd->forwardmove;
    const float flBackupSide = pCmd->sidemove;
    const Vec3 vMins = EntV3(pLocal, g_offMins);
    const Vec3 vMaxs = EntV3(pLocal, g_offMaxs);
    // the player is standing in the map, so the out-of-bounds test has to say so (movement.cpp)
    MV_ValidateOutsideWorld(EntV3(pLocal, g_offOrigin) + Vec3(0.f, 0.f, 40.f));

    // pin the base prediction frame; every probe restores to it (bare restore drifts)
    const int nBasePredicted = PredCommandsPredicted();

    const Vec3 vWallNormal = s_pfNormal;
    const Vec3 vTangent = s_pfTangent;      // wall tangent for the lateral band sweep
    const float flBandHalf = s_pfBandHalf;                       // crosshair finder: 16
    const float flBandStep = s_pfBandStep > 0.f ? s_pfBandStep : 4.f;
    const int   iLatCount = int(flBandHalf * 2.f / flBandStep) + 1;   // 9: -16..+16 by 4

    // one probe per height: PLACE the hull at DNA's exact sub-unit catch offsets, seed the
    // already-pinned descent speed, ask the engine with a predicted tick (pinned vz = catch)
    auto TryPin = [&](const Vec3& vTestOrigin, const Vec3& vN, bool bDuck, float& flPinZ) -> bool
    {
        const Vec3 vInto = Vec3(-vN.x, -vN.y, 0.f);
        Vec3 vIntoAngle = VectorAngles(vInto);
        ClampAnglesMv(vIntoAngle);
        const float flRot = DEG2RADF(vIntoAngle.y - pCmd->viewangles.y);
        const float flCos = cosf(flRot), flSin = sinf(flRot);

        // flFloorZ: stop the drop once the hull has fallen past what THIS cell is
        // responsible for. Free fall can never pin (|vz + halfgrav| only grows), and every
        // height below is scanned by its own cell, so the ticks after that are pure duplication.
        // It is a FLOOR, not a tick count: if geometry slows the hull it stays above the floor
        // for longer and keeps all 10 ticks, which is exactly the case worth paying for
        auto Probe = [&](float flFeetZ, float flSeedVz, int iTicks, float flFloorZ = -1e9f) -> bool
        {
            RestoreToFrame(nBasePredicted - 1);

            const Vec3 vAt = Vec3(vTestOrigin.x, vTestOrigin.y, flFeetZ);
            g_fnSetAbsOrigin(pLocal, vAt);
            EntSetV3(pLocal, g_offOrigin, vAt);
            EntSetI(pLocal, g_offFlags, EntI(pLocal, g_offFlags) & ~FL_ONGROUND); // airborne on the lip
            // light into-wall press only -- big speed pins flat on the main wall and falls past the lip
            EntSetV3(pLocal, g_offVelocity, vInto * 10.f + Vec3(0.f, 0.f, flSeedVz));

            // s_pfBaseButtons, not the live cmd: the scan spans cmds now, and a key the user
            // taps mid-scan must not change the probe half the column already ran with
            pCmd->buttons = s_pfBaseButtons & ~IN_JUMP;
            if (bDuck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
            pCmd->forwardmove = flCos * 10.f;
            pCmd->sidemove = -flSin * 10.f;

            for (int t = 0; t < iTicks; t++)
            {
                Pred_Simulate(pLocal, pCmd);
                s_pfProbes++;
                pCmd->buttons &= ~IN_JUMP;

                if (fabsf(EntV3(pLocal, g_offVelocity).z + flHalfGrav) < flSurfTol)
                {
                    flPinZ = EntV3(pLocal, g_offOrigin).z; // the engine's own settled catch height
                    return true;
                }
                if (EntV3(pLocal, g_offOrigin).z < flFloorZ)
                    break;                                 // fell out of this cell's own step
            }
            return false;
        };

        // exact placement at the DNA catch offset (1 tick decisive), then a gentle
        // fallback drop sweeping the rest of this 1u step
        //
        // cut this to a flat 4 ticks and put the 10 back (two probe changes shipped
        // in one build; the flat-face prune was the guilty one). keeps all 10 ticks and
        // adds a FLOOR instead: 26000 sims / 1376 cells = 18.9, i.e. the fallback is the whole
        // cost, and at -8 u/s under sv_gravity 800/66t it falls 10.2u -- ten 1u steps the loop
        // scans again on their own. Free fall cannot pin, so once the hull is below this cell's
        // own step the remaining ticks are duplication. Geometry that slows the hull keeps it
        // above the floor and keeps every tick, which is the only case those ticks ever earned
        //
        // the floor was flExact - 1.2, a WHOLE UNIT below the step this cell owns, so
        // every cell re-flew the cell below's unit as well. The drop from +0.9 at -8 u/s reads
        // +0.69, +0.30, -0.27, -1.02, -1.95 (sv_gravity 800, 66.67 tick) and the last two are the
        // next cell down's job. Floored at its own step (-0.1; the pin check runs BEFORE the floor
        // check, so the -0.27 tick still counts) that unit is still swept by the cell below --
        // from a FRESH -8, i.e. slower than this hull arrives there and easier to pin, not harder.
        // 5 sims -> 3 on the fallback, which is where the scan spends nearly all of its time
        const float flT = truncf(vTestOrigin.z);
        const float flExact = vTestOrigin.z >= 0.f ? flT + 0.0287018f : flT - 0.972092f;
        if (Probe(flExact, -flHalfGrav, 2))
            return true;
        return Probe(flExact + 0.9f, -8.f, 10, flExact - 0.1f);
    };

    // ---- HEADBOUNCE (g_psFinderHeadbounce): the same pin test, mirrored for an ASCENT catch
    // against a ceiling. TryPin above seeds a descent speed and asks vz to settle at -halfgrav;
    // this seeds a rise and asks vz to settle at +halfgrav -- same discriminator HeadBounce
    // (movement.cpp) already trusts, just the other sign. One probe, not TryPin's two-step
    // fallback: an ascent pin either catches at the seeded offset or it is not this cell's catch
    auto TryHeadbounce = [&](const Vec3& vTestOrigin, bool bDuck, float& flPinZ) -> bool
    {
        RestoreToFrame(nBasePredicted - 1);
        const Vec3 vAt = vTestOrigin;
        g_fnSetAbsOrigin(pLocal, vAt);
        EntSetV3(pLocal, g_offOrigin, vAt);
        EntSetI(pLocal, g_offFlags, EntI(pLocal, g_offFlags) & ~FL_ONGROUND);
        EntSetV3(pLocal, g_offVelocity, Vec3(-vWallNormal.x, -vWallNormal.y, 0.f) * 10.f + Vec3(0.f, 0.f, flHalfGrav));
        pCmd->buttons = s_pfBaseButtons & ~IN_JUMP;
        if (bDuck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
        for (int t = 0; t < 4; t++)
        {
            Pred_Simulate(pLocal, pCmd);
            s_pfProbes++;
            if (fabsf(EntV3(pLocal, g_offVelocity).z - flHalfGrav) < flSurfTol)
            {
                flPinZ = EntV3(pLocal, g_offOrigin).z;
                return true;
            }
        }
        return false;
    };

    // ---- CAN A FALL CATCH THIS, or does it only HOLD a pin already there? ----------
    // TryPin above proves a point by TELEPORTING the hull to the catch offset with the pin's
    // own velocity already on it, then asking whether the next tick keeps it. That is not the
    // question the assist has to answer. The assist ARRIVES -- falling at 50 to 95 u/s, with
    // the ladder's sub-unit phase decided back at the launch -- and a point that only holds a
    // pin can never be jumped onto, whatever the picker does. Three builds of picker work went
    // into a question that may never have had a yes in it, and nothing has ever measured which.
    // So every pin the scan keeps is re-asked as a FALL: swept across one whole tick of travel
    // (the phase an arc cannot choose) at two real arrival speeds.
    // pfstrict: with "strict" on (default) a pin that no fall catches is DROPPED -- it is a
    // teleport-only artifact, not a surf. This is a movement sim, not a trace screen, so the
    // lesson (traces cannot see seams) does not apply. strict off = count only, as before.
    // `conf=N/M` on the scan line; conf far below M with strict off = how much noise it removes
    auto TryFall = [&](const Vec3& vTestOrigin, const Vec3& vN, float flPinZ, bool bDuck) -> bool
    {
        const Vec3 vInto = Vec3(-vN.x, -vN.y, 0.f);
        Vec3 vIntoAngle = VectorAngles(vInto);
        ClampAnglesMv(vIntoAngle);
        const float flRot = DEG2RADF(vIntoAngle.y - pCmd->viewangles.y);

        // pfstrict -- this used to bail on SimsLeft(), which is the TB/PS 0.40-tick clock + the
        // 384 cap. The finder's own probes (7.5ms chunks) had already spent both, so TryFall
        // mostly returned false for budget reasons and conf= never measured anything. The finder
        // is bounded by its own chunk clock; one TryFall is at most 30 x 4 sims.
        // Speeds: apex-ish hop arrivals (-30) up to a real drop (-260). Span = one tick of travel
        // at THAT speed -- a fast fall crosses more height per tick, so its phase window is wider
        static const float kFallVz[5] = { -30.f, -60.f, -100.f, -170.f, -260.f };
        constexpr int iFallSteps = 6;

        for (int s = 0; s < 5; s++)
            for (int i = 0; i < iFallSteps; i++)
            {
                const float flFallSpan = std::max(1.f, -kFallVz[s] * TickInterval());
                RestoreToFrame(nBasePredicted - 1);
                const Vec3 vAt = Vec3(vTestOrigin.x, vTestOrigin.y,
                                      flPinZ + flFallSpan * (float(i) / float(iFallSteps)));
                g_fnSetAbsOrigin(pLocal, vAt);
                EntSetV3(pLocal, g_offOrigin, vAt);
                EntSetI(pLocal, g_offFlags, EntI(pLocal, g_offFlags) & ~FL_ONGROUND);
                EntSetV3(pLocal, g_offVelocity, vInto * 10.f + Vec3(0.f, 0.f, kFallVz[s]));

                pCmd->buttons = s_pfBaseButtons & ~IN_JUMP;
                if (bDuck) pCmd->buttons |= IN_DUCK; else pCmd->buttons &= ~IN_DUCK;
                pCmd->forwardmove = cosf(flRot) * 10.f;
                pCmd->sidemove = -sinf(flRot) * 10.f;

                for (int t = 0; t < 4; t++)
                {
                    const float flPreVz = EntV3(pLocal, g_offVelocity).z;
                    Pred_Simulate(pLocal, pCmd);
                    s_pfProbes++;
                    pCmd->buttons &= ~IN_JUMP;
                    if (EntI(pLocal, g_offFlags) & FL_ONGROUND)
                        break;                  // that is a landing, not a surf
                    const float flPostVz = EntV3(pLocal, g_offVelocity).z;
                    // the same pair PixelSurf and PsSimHop use: at -halfgrav AND off the
                    // free-fall ladder, so a graze through the window is refused
                    if (fabsf(flPostVz + flHalfGrav) < flSurfTol
                        && fabsf(flPostVz - (flPreVz - flHalfGrav * 2.f)) > flSurfTol)
                        return true;
                    // same floor TryPin's fallback has, and this pass never had one.
                    // -260 crosses 3.9u a tick: one tick puts the hull under the pin and ticks
                    // 2..4 were free fall 8, 12, 16u below it. A hull SLOWED by the seam stays
                    // above the floor and keeps all 4, which is the only case they earned
                    if (EntV3(pLocal, g_offOrigin).z < flPinZ - 2.f)
                        break;
                }
            }
        return false;
    };

    // pfstrict -- CAN THE HULL GET HERE AT ALL? Every probe TELEPORTS the hull onto the seam, so
    // a seam sealed off from the open map (behind a clip brush, inside a pocket) pins as well as a
    // real one. A real arrival comes from above or from out in front, so the catch hull must be
    // able to leave the spot one of those two ways: 48u straight up, or 32u straight out.
    // 0.25u off the wall first -- the probe origin is embedded 0.022u into brush walls.
    // ponytail: local test only -- a sealed pocket bigger than 48u up / 32u out still passes;
    // a real flood fill from the player's position is the upgrade if that shows up
    auto Reachable = [&](const Vec3& vAt, const Vec3& vN, bool bDuck) -> bool
    {
        constexpr unsigned kContentsPlayerClip = 0x10000;   // bspflags.h CONTENTS_PLAYERCLIP
        const Vec3 vHullMaxs(vMaxs.x, vMaxs.y, bDuck ? 62.f : 82.f);
        const Vec3 vStart = vAt + vN * 0.25f;
        GameTrace_t up{}, out{};
        TraceHull(vStart, vStart + Vec3(0.f, 0.f, 48.f), vMins, vHullMaxs, MASK_PLAYERSOLID, &filter, &up);
        if (!up.startsolid && up.fraction >= 1.f)
            return true;
        TraceHull(vStart, vStart + vN * 32.f, vMins, vHullMaxs, MASK_PLAYERSOLID, &filter, &out);
        if (!out.startsolid && out.fraction >= 1.f)
            return true;
        s_pfSealed++;
        if ((unsigned)(up.contents | out.contents) & kContentsPlayerClip)
            s_pfClipBlk++;
        return false;
    };

    // fall check, memoized per settled catch (see s_pfFallMemo)
    auto FallCatches = [&](const Vec3& vTestOrigin, const Vec3& vN, float flPinZ, bool bDuck) -> bool
    {
        const Vec3 vAt(vTestOrigin.x, vTestOrigin.y, flPinZ);
        for (const PfFallMemo& m : s_pfFallMemo)
            if (m.duck == bDuck && fabsf(m.at.z - vAt.z) < 0.01f
                && fabsf(m.at.x - vAt.x) < 0.5f && fabsf(m.at.y - vAt.y) < 0.5f)
                return m.ok;
        s_pfFalls++;
        const bool bOk = TryFall(vTestOrigin, vN, flPinZ, bDuck);
        if (bOk) s_pfConf++;
        s_pfFallMemo.push_back({ vAt, bDuck, bOk });
        return bOk;
    };

    // one (height, lateral offset) cell -- the indivisible unit of the budget
    auto ScanCell = [&](float flZ, float flLat)
    {
        // re-trace the wall at THIS height/offset (short +/-12u ray; the lip protrudes
        // at its own height only, and a long ray skims run-up terrain on low surfs)
        const Vec3 vProbeMid = Vec3(s_pfColumn.x, s_pfColumn.y, flZ) + vTangent * flLat;
        GameTrace_t wallTrace{};
        TraceLine(vProbeMid + vWallNormal * 12.f, vProbeMid - vWallNormal * 12.f,
                  MASK_PLAYERSOLID, &filter, &wallTrace);

        // pfstrict -- two cases the 12u ray got wrong, both common on CLIP brushes:
        //  * START BURIED: something thicker than 12u stands in front of the anchor plane at this
        //    height (smoothing clip over a detailed wall). The ray skipped it, hit the wall BEHIND,
        //    the hull spawned inside the clip and the cell died on startsolid -- the face the hull
        //    actually touches was never scanned. Re-ask from 48u out.
        //  * MISS: the face at this height is recessed past 12u. Re-ask 48u deeper.
        // The retry must face the same way as the anchored wall (dot > 0.7), which is what keeps
        // it off run-up terrain and off unrelated walls/props in front
        if (wallTrace.startsolid || wallTrace.fraction >= 1.f)
        {
            const bool bBuried = wallTrace.startsolid;
            GameTrace_t retry{};
            TraceLine(vProbeMid + vWallNormal * (bBuried ? 48.f : 12.f),
                      vProbeMid - vWallNormal * (bBuried ? 12.f : 48.f),
                      MASK_PLAYERSOLID, &filter, &retry);
            Vec3 vRn(retry.plane.normal.x, retry.plane.normal.y, 0.f);
            if (!retry.startsolid && retry.fraction < 1.f && fabsf(retry.plane.normal.z) <= 0.7f
                && vRn.Length2D() > 0.1f)
            {
                vRn.Normalize();
                if (vRn.x * vWallNormal.x + vRn.y * vWallNormal.y > 0.7f)
                {
                    wallTrace = retry;
                    s_pfDeep++;
                }
            }
        }

        // A PROP IN THE COLUMN IS NOT A WALL. The re-trace above hits whatever stands
        // in front of the anchored face first, and on a crate/barrel/sign that is the prop: the
        // cell then spawns the hull on the prop's own plane and every pin the 24 sims find is the
        // prop's. That is the report -- the finder saving points on props, and the assist then
        // firing at them. Cheap and exact: the trace already knows
        // now behind the "ignore props" checkbox (g_psFinderNoProps, on by default);
        // off puts the pre-behaviour back and props count as walls again
        if (g_psFinderNoProps && wallTrace.fraction < 1.f && MV_TraceIsProp(wallTrace))
            { s_pfProp++; return; }

        Vec3 vN, vWallXY;
        bool bDispWall = false;
        bool bReacquired = false;
        if (wallTrace.fraction < 1.f && fabsf(wallTrace.plane.normal.z) <= 0.7f
            && Vec3(wallTrace.plane.normal.x, wallTrace.plane.normal.y, 0.f).Length2D() > 0.1f)
        {
            bReacquired = true;
            vN = Vec3(wallTrace.plane.normal.x, wallTrace.plane.normal.y, 0.f);
            vN.Normalize();
            vWallXY = wallTrace.endpos;
            bDispWall = (wallTrace.dispFlags & DISPSURF_SURFACE) != 0;
        }
        else
        {
            // no clean re-acquire here -- fall back to the anchored wall plane; the
            // embed check + probes reject anything not actually there
            vN = vWallNormal;
            vWallXY = vProbeMid;
        }

        // feet origin at the AABB support distance for THIS column's normal; embed
        // 0.022u INTO brush walls (DNA), sit 0.001u OFF displacements (triangle soup)
        const float flSupport = fabsf(vMaxs.x) * fabsf(vN.x) + fabsf(vMaxs.y) * fabsf(vN.y);
        const float flGap = flSupport + (bDispWall ? 0.001f : -0.022f);
        const Vec3 vTestOrigin = Vec3(vWallXY.x, vWallXY.y, flZ) + vN * flGap;

        // ---- IS THIS SPOT ANYWHERE A PLAYER COULD EVER BE? -----------------------------------
        // Every test below this line is a SWEPT trace or a sim, and a sweep reports where it
        // stopped, never whether the place it started from is inside the map. A whole-map brush
        // walk hands this scanner the top edge of every wall face in the BSP, including the ones
        // buried inside other brushes and the OUTSIDE faces of the shell that seals the map, and
        // those are the "surfs inside walls / out of bounds" on the overlay. One leaf lookup each,
        // ahead of ~24 sims, so it is a speed win on the cells it kills as well
        if (MV_PointUnusable(vTestOrigin + Vec3(0.f, 0.f, 8.f))) { s_pfVoid++; return; }

        // ---- THE FLAT-FACE PRUNE IS DELETED. FALSIFIED BY ITS OWN DIAG --------------
        // It swept three player hulls over the slabs a probe can reach and skipped the cell when
        // all three stopped at the same distance. The log: `cells=1620 sims=216 flat=1602` ->
        // **0 spot(s)**, on five separate columns. 98.9% of every column pruned and the finder
        // stopped finding anything at all.
        // WHY IT CANNOT WORK, and this is the reason an earlier build's note gave in advance: a pixel surf
        // pin is an artifact of the COLLISION SOLVER, not of visible geometry. The classic TF2
        // seam is the junction between two flush, coplanar brush faces -- there is nothing there
        // for a trace to measure, the hull sweeps across it at a constant fraction, and the catch
        // only exists because CGameMovement clips against the two brushes separately. **No trace
        // can screen for it. Only the movement sim can answer, which is why the finder sims.**
        // Do not re-derive this with a finer eps, more slabs, or a different trace shape.

        // REGION PRUNE -- ONLY on the fallback path, i.e. the +/-12u ray found no wall at
        // this height and the cell is running on the anchored plane on spec. Is there anything
        // here at all for a probe to hit? The box is the hull grown by every distance a probe can
        // travel: 1.5u of into-wall creep (10 u/s for 10 ticks) padded to 6, and 10.2u of fall on
        // the second probe padded to 14. Empty box = no collision on any of the 24 sims below =
        // vz never pins, so the answer is already known. Costs one hull trace instead.
        // Gated on !bReacquired on purpose: where the ray DID hit a wall the sims are probing
        // real geometry, and a stationary hull test is not the thing to arbitrate that with --
        // displacements are triangle soup with no inside for startsolid to report
        if (!bReacquired)
        {
            const Vec3 vRegionMins(vMins.x - 6.f, vMins.y - 6.f, vMins.z - 14.f);
            const Vec3 vRegionMaxs(vMaxs.x + 6.f, vMaxs.y + 6.f, 62.f);
            GameTrace_t regionTrace{};
            TraceHull(vTestOrigin, vTestOrigin, vRegionMins, vRegionMaxs,
                      MASK_PLAYERSOLID, &filter, &regionTrace);
            if (!regionTrace.startsolid && !regionTrace.allsolid)
            {
                s_pfPruned++;
                return;
            }
        }

        // ---- does the hull the probe spawns FIT where it spawns it? ---------------------------
        // under a lip the 82u hull (TF2 VEC_HULL_MAX, tf_gamerules.cpp:1314) is buried
        // in the ceiling and the standing pass spent 12 sims proving it. NOT the falsified
        // flat-face prune: it screens nothing about the seam, only the spawn.
        // ORDER. This was two hull traces ahead of the region prune, so an empty cell
        // paid three traces to reach a return. The prune goes first, and the 82u hull CONTAINS
        // the 62u one (same mins, taller maxs), so a standing fit IS a ducked fit and the duck
        // trace only runs where standing failed. Empty cell: 1 trace, was 3
        const Vec3 vStandMaxs = Vec3(vMaxs.x, vMaxs.y, 82.f);
        GameTrace_t standTrace{};
        TraceHull(vTestOrigin, vTestOrigin, vMins, vStandMaxs, MASK_PLAYERSOLID, &filter, &standTrace);
        const bool bStandFits = !standTrace.startsolid && !standTrace.allsolid;
        if (!bStandFits)
        {
            s_pfNoStand++;
            // reject embedded spawns -- DUCK hull (62 tall): low surfs fail a standing fit
            const Vec3 vDuckMaxs = Vec3(vMaxs.x, vMaxs.y, 62.f);
            GameTrace_t solidTrace{};
            TraceHull(vTestOrigin, vTestOrigin, vMins, vDuckMaxs, MASK_PLAYERSOLID, &filter, &solidTrace);
            if (solidTrace.startsolid || solidTrace.allsolid)
                return;
        }

        // standing, then ducked (most TF2 pixel surfs are ducked)
        for (int iDuck = 0; iDuck < 2 && !s_pfHeightHit; iDuck++)
        {
            if (iDuck == 0 && !bStandFits) continue;   // 82u hull does not fit here

            float flPinZ = flZ;
            if (TryPin(vTestOrigin, vN, iDuck != 0, flPinZ))
            {
                const bool bDuck = iDuck != 0;
                // AND ASK THE PIN WHAT HELD IT. The gate above proves the FACE the cell
                // aimed at is brush; it cannot prove the thing that clipped vz was. A crate parked
                // against a real wall (every TF2 map) pins the sim exactly like a seam, which is
                // an earlier build's own finding for the live surf -- the finder never asked. MV_PinOnProp
                // is that question, run at the settled origin: nearest brush face vs nearest prop
                // face, prop closer = the prop is what we are resting on. 16 hull traces, and only
                // on a pin, so it costs nothing on the cells that find nothing
                if (g_psFinderNoProps
                    && MV_PinOnProp(Vec3(vTestOrigin.x, vTestOrigin.y, flPinZ), vMins,
                                    Vec3(vMaxs.x, vMaxs.y, bDuck ? 62.f : 82.f), filter))
                {
                    s_pfProp++;
                    continue;
                }
                // pfstrict: a failed pin no longer ends this height -- the next lateral offset /
                // duck state gets its turn, which is where the extra real surfs come from
                const bool bFall = FallCatches(vTestOrigin, vN, flPinZ, bDuck);
                const bool bReach = Reachable(Vec3(vTestOrigin.x, vTestOrigin.y, flPinZ), vN, bDuck);
                // g_psFinderPixeljump: a pin no FALL catches but the pocket is open is a real
                // catch too -- just one a JUMP into the seam has to make, not a drop onto it.
                // Own list, own marker: mixing it into s_pfFound would read as a plain surf
                if (!bFall && g_psFinderPixeljump && bReach)
                {
                    std::lock_guard<std::mutex> l(g_psLock);
                    s_pfJumpPts.push_back(Vec3(vWallXY.x, vWallXY.y, flPinZ));
                    s_pfHeightHit = true;
                    DebugLog("PF: pixeljump pin z=%.1f duck=%d", flPinZ, (int)bDuck);
                    continue;
                }
                // THE MAP SCAN IS ALWAYS STRICT. The checkbox is about a column you aimed at, where
                // you can see what the extra rings are and judge them yourself. A whole-map scan
                // draws thousands of lines on geometry you never looked at, so a pin no fall can
                // catch, or one sealed off from the open map, is noise with nothing to weigh it
                // against -- and that is the "finds a lot of surfs inside walls" half of the report
                if ((g_psFinderStrict || s_pfQuiet) && (!bFall || !bReach))
                    continue;
                // store ON the wall surface at the engine's settled height
                s_pfFound.push_back(Vec3(vWallXY.x, vWallXY.y, flPinZ));
                s_pfHeightHit = true;
            }
            else if (g_psFinderHeadbounce)
            {
                // g_psFinderHeadbounce: TryPin found no descent catch here -- try the ceiling
                // (ascent) catch instead. Same column, same height step, opposite sign
                float flHbZ = flZ;
                if (TryHeadbounce(vTestOrigin, iDuck != 0, flHbZ))
                {
                    std::lock_guard<std::mutex> l(g_psLock);
                    s_pfHeadbouncePts.push_back(Vec3(vWallXY.x, vWallXY.y, flHbZ));
                    s_pfHeightHit = true;
                    DebugLog("PF: headbounce pin z=%.1f duck=%d", flHbZ, (int)(iDuck != 0));
                }
            }
        }
    };

    while (s_pfNextI <= s_pfSteps)
    {
        // height done (a pin was found, or all 9 offsets are spent) -- step up
        if (s_pfHeightHit || s_pfNextLat >= iLatCount)
        {
            s_pfNextI++;
            s_pfNextLat = 0;
            s_pfHeightHit = false;
            continue;
        }

        const float flZ = s_pfLow + float(s_pfNextI);
        const float flLat = -flBandHalf + flBandStep * float(s_pfNextLat);
        s_pfNextLat++;
        s_pfCells++;
        ScanCell(flZ, flLat);

        // ONE CLOCK, OURS. This also stopped on TbBudgetExpired(), the 0.40-of-a-tick
        // share the whole TB/PS family searches against, and the finder runs LAST in the cmd:
        // with PS assist or TB active they spend that share before the finder is even called, so
        // every chunk did exactly ONE cell and a 600-step column took thousands of ticks. A scan
        // is a deliberate keypress with a visible result, not a background search -- it gets its
        // own budget on top, and the budget slider is what bounds it
        if (ElapsedMs() >= flBudgetMs)
            break;
    }

    // restore everything the chunk touched
    pCmd->buttons = iBackupButtons;
    pCmd->forwardmove = flBackupForward;
    pCmd->sidemove = flBackupSide;
    RestoreToFrame(nBasePredicted - 1);

    s_pfChunks++;
    s_pfMs += ElapsedMs();

    // publish what we have every chunk -- points appear while the column is still scanning.
    // The map scan's jobs are one cell each and its own markers are LINES, so it keeps its pins
    // (s_pfFound) and publishes nothing here -- otherwise every cell would wipe the crosshair
    // finder's points and repaint the screen with single dots.
    int nFound = 0;
    if (!s_pfQuiet)
    {
        std::vector<Vec3> vPub = PfDedupe(s_pfFound);
        nFound = (int)vPub.size();
        std::lock_guard<std::mutex> l(g_psLock);
        s_psFinderPts = std::move(vPub);
    }

    if (s_pfNextI > s_pfSteps)
    {
        s_pfBusy = false;
        if (s_pfQuiet)
            return;   // the map scan reads s_pfFound, logs its own totals, and clears
        // conf=N/M -- of M pins found, N are catchable by a FALL (TryFall). READ IT:
        // conf=0/M means every saved point on this wall only HOLDS a pin and the assist can
        // never jump onto one, however good the picker is
        DebugLog("PF: scan z=%.1f..%.1f steps=%d margin=%.0f -> %d spot(s) | %d chunk(s) %.1f ms "
                 "cells=%d sims=%d pruned=%d nostand=%d conf=%d/%d sealed=%d clip=%d deep=%d "
                 "prop=%d void=%d strict=%d budget=%.1f eff=%.1f",
                 s_pfLow, s_pfHigh, s_pfSteps, g_psFinderMargin, nFound,
                 s_pfChunks, s_pfMs, s_pfCells, s_pfProbes, s_pfPruned, s_pfNoStand,
                 s_pfConf, s_pfFalls, s_pfSealed, s_pfClipBlk, s_pfDeep, s_pfProp, s_pfVoid,
                 (int)g_psFinderStrict, (double)g_psFinderBudget, flBudgetMs);
        s_pfFound.clear();
    }
}

// ============================================================================
// TEXTURE BUG SEAMS ON THE SAME COLUMN 
// ----------------------------------------------------------------------------
// A pixel surf is a solver artifact with nothing for a trace to see, which is why
// the scan above sims. A TEXTURE BUG is the opposite: it IS a visible boundary --
// two coplanar brush faces the collision solver clips separately -- and TB already
// owns the test that decides it, TB_FaceSame (same plane within 0.10, same surface
// name). So this half is a TRACE pass: walk the anchored column a unit at a time,
// keep every height where the face changes while the plane stays flush, bisect the
// unit down to ~0.03u and mark it. One line trace per unit of column (<= 601) plus
// 5 per seam, run once on release -- cheap next to one cell of the sim scan.
// ponytail: centre column only, and open-air is a thin box in front of the seam.
// Sweep the lateral band / re-ask the ride as a sim if a real wall proves it needs it.
static int s_pfSeamTr = 0;   // traces the seam pass spent (diag)

// the filter is built by the caller -- this ran Setup() on all ~600 traces of a column
static bool PfSeamProbe(TraceFilterWorldProps& filter, const Vec3& vColumn, const Vec3& vN, float flZ,
                        GameTrace_t& tr)
{
    const Vec3 vMid(vColumn.x, vColumn.y, flZ);
    TraceLine(vMid + vN * 12.f, vMid - vN * 12.f, MASK_PLAYERSOLID, &filter, &tr);
    s_pfSeamTr++;
    // and never a prop. A texturebug is two COPLANAR BRUSH FACES the solver clips
    // apart; a crate's .phy hull has no pair to be, but it does trace as an upright face with
    // its own material name, so PfIsSeam read every crate-on-wall as a boundary and rang it
    return tr.fraction < 1.f && !tr.startsolid && fabsf(tr.plane.normal.z) <= 0.7f
        && !(g_psFinderNoProps && MV_TraceIsProp(tr));   // same checkbox as the sim half
}

// flush enough to be one surface to the eye, not the same face to the solver.
// A corner (normals apart) or a ledge (dist apart) is a wall, not a seam
static bool PfIsSeam(const GameTrace_t& a, const GameTrace_t& b)
{
    if (a.plane.normal.x * b.plane.normal.x + a.plane.normal.y * b.plane.normal.y
      + a.plane.normal.z * b.plane.normal.z < 0.9995f) return false;
    if (fabsf(a.plane.dist - b.plane.dist) > 0.5f) return false;
    return !TB_FaceSame(a, b);
}

// THE BAND, NOT THE ONE COLUMN. The pass used to walk the crosshair's centre line and nothing
// else, so a texture boundary that stops 20u to the side of where you happened to aim did not
// exist -- and a boundary is a thing with ENDS, which is exactly when that happens. It sweeps the
// same lateral band the sim half does now, and the count is bounded by the trace budget rather
// than by the band: a 600u column gets 3 offsets, a 60u one gets all 9, and neither costs more
// than ~1800 line traces on the release command that runs the whole pass inline.
static constexpr int k_pfSeamTraceCap = 1800;

// two marks are the same seam when they are at the same height on the same stretch of wall. The
// band is swept per offset, so without this a boundary that really does run the width of the
// column comes back as nine rings in a row. Radius = whole band width: a fixed 8u let every
// other 4u offset through, so one seam rang 5 times
static bool PfSeamKnown(const std::vector<Vec3>& v, const Vec3& p, float flRadius)
{
    for (const Vec3& q : v)
        if (std::fabs(q.z - p.z) < 0.5f && (q - p).Length2D() < flRadius) return true;
    return false;
}

static void PfScanSeams(void* pLocal)
{
    std::vector<Vec3> vOut;
    s_pfSeamTr = 0;
    const int iSteps = s_pfSteps;
    const Vec3 vN = s_pfNormal;

    // A DIAGONAL WALL CANNOT TEXTUREBUG, so it gets no markers. The bug is the ulp step
    // into the wall (TB_UlpStepIntoWall): the trace's delta has to cross the wall's own float
    // boundary, which only exists on x or on y. Off-axis there is nothing to step onto and every
    // ring drawn on one was a ride nobody could take
    {
        int iAxis = -1;
        TB_UlpStepIntoWall(vN, s_pfColumn, iAxis);
        if (iAxis < 0)
        {
            { std::lock_guard<std::mutex> l(g_psLock); s_psTbPts.clear(); }
            DebugLog("PF: seam pass skipped -- wall is diagonal (n=%.4f %.4f), no texturebug on it",
                     vN.x, vN.y);
            return;
        }
    }

    const Vec3 vMins = EntV3(pLocal, g_offMins);
    const Vec3 vMaxs = EntV3(pLocal, g_offMaxs);
    TraceFilterWorldProps filter; filter.Setup(pLocal);

    const float flBandHalf = s_pfBandHalf;
    const float flBandStep = s_pfBandStep > 0.f ? s_pfBandStep : 4.f;
    const int   iLatFull   = int(flBandHalf * 2.f / flBandStep) + 1;   // the sim half's own 9
    const int   iLat = std::clamp(k_pfSeamTraceCap / std::max(1, iSteps + 1), 1, iLatFull);
    // one offset = the centre, as before; more than one spreads over the WHOLE band so the ends
    // are always asked (that is where a boundary stops)
    const float flLatStep = iLat > 1 ? (flBandHalf * 2.f) / float(iLat - 1) : 0.f;

    for (int j = 0; j < iLat && (int)vOut.size() < 64; j++)
    {
        const float flLat = iLat > 1 ? -flBandHalf + flLatStep * float(j) : 0.f;
        const Vec3 vColumn = s_pfColumn + s_pfTangent * flLat;

        GameTrace_t prev{};
        bool bPrev = PfSeamProbe(filter, vColumn, vN, s_pfLow, prev);
        float flPrevZ = s_pfLow;

        for (int i = 1; i <= iSteps && (int)vOut.size() < 64; i++)
        {
            const float flZ = s_pfLow + float(i);
            GameTrace_t cur{};
            const bool bCur = PfSeamProbe(filter, vColumn, vN, flZ, cur);

            if (bPrev && bCur && PfIsSeam(prev, cur))
            {
                // the ride is ON the boundary -- a marker a whole unit off is a marker for
                // a different height. lo stays on the OLD face, hi is the first z on the new one
                float flLo = flPrevZ, flHi = flZ;
                for (int k = 0; k < 5; k++)
                {
                    const float flMid = (flLo + flHi) * 0.5f;
                    GameTrace_t mid{};
                    if (PfSeamProbe(filter, vColumn, vN, flMid, mid) && TB_FaceSame(prev, mid))
                        flLo = flMid;
                    else
                        flHi = flMid;
                }

                // open air in front of the seam, or nothing can ride it. Thin box: the hull
                // that rides a TB is falling past, so its feet are not at the seam
                Vec3 vFace(cur.plane.normal.x, cur.plane.normal.y, 0.f);
                if (vFace.Length2D() > 0.1f) vFace.Normalize(); else vFace = vN;
                const float flSup = fabsf(vMaxs.x) * fabsf(vFace.x) + fabsf(vMaxs.y) * fabsf(vFace.y);
                const Vec3 vAt = Vec3(cur.endpos.x, cur.endpos.y, flHi) + vFace * (flSup + 0.5f);
                GameTrace_t fit{};
                TraceHull(vAt, vAt, Vec3(vMins.x, vMins.y, -4.f), Vec3(vMaxs.x, vMaxs.y, 4.f),
                          MASK_PLAYERSOLID, &filter, &fit);
                const Vec3 vMark(cur.endpos.x, cur.endpos.y, flHi);
                // ...and the ride has to be somewhere a player can be. The thin box above is a
                // sweep that started where it ended, so it says nothing about a spot sealed
                // inside a brush or sitting off the edge of the map
                if (!fit.startsolid && !fit.allsolid && !PfSeamKnown(vOut, vMark, flBandHalf * 2.f + 1.f)
                    && !MV_PointUnusable(vAt))
                    vOut.push_back(vMark);
            }

            prev = cur; bPrev = bCur; flPrevZ = flZ;
        }
    }

    const int nFound = (int)vOut.size();
    {
        std::lock_guard<std::mutex> l(g_psLock);
        s_psTbPts = std::move(vOut);
    }
    DebugLog("PF: seam pass z=%.1f..%.1f -> %d texturebug(s) | %d offset(s) of %d, %d trace(s)",
             s_pfLow, s_pfHigh, nFound, iLat, iLatFull, s_pfSeamTr);
}

static void PixelFinder(void* pLocal, UserCmd_t* pCmd)
{
    const bool bHeld = On(g_psFinder, g_psFinderKey, g_psFinderMode);
    const bool bRising = bHeld && !s_psFinderHeld;
    const bool bFalling = !bHeld && s_psFinderHeld;
    s_psFinderHeld = bHeld;

    // a new column throws away the one still scanning -- the cursor points into the old anchor.
    // unticking the feature mid-scan stops it too: the checkbox is an off switch, not a pause
    if (bRising || !g_psFinder)
        PmAbort();   // a column press (or the checkbox) outranks a map scan: one scanner, one job
    if (s_pfBusy && (bRising || !g_psFinder))
    {
        DebugLog("PF: scan cancelled at step %d/%d -- %s", s_pfNextI, s_pfSteps,
                 bRising ? "new column anchored" : "feature turned off");
        PfCancelScan();
    }

    if (!bHeld && !bFalling)
    {
        // key long gone, chunked scan still running: keep spending its slice each cmd
        if (s_pfBusy && g_offViewOffset > 0)
            PfScanChunk(pLocal, pCmd);
        return;
    }
    if (g_offViewOffset <= 0)
        return;

    // where the crosshair hits the world
    Vec3 vForward; AngleVectors(pCmd->viewangles, &vForward);
    const Vec3 vEye = EntV3(pLocal, g_offOrigin) + EntV3(pLocal, g_offViewOffset);
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    GameTrace_t trace{};
    TraceLine(vEye, vEye + vForward * 6000.f, MASK_PLAYERSOLID, &filter, &trace);
    const Vec3 vHit = trace.endpos;

    if (bRising)
    {
        // first press: clear old results, anchor the scan column (ignore triggers/empty aim)
        std::lock_guard<std::mutex> l(g_psLock);
        s_psFinderPts.clear();
        s_psTbPts.clear();
        s_psFinderFirst = false;
        // a prop anchor is refused outright. Aiming at a crate used to anchor the
        // column on its .phy hull and scan the whole thing; every cell would now be skipped, so
        // saying so once beats 601 silent misses
        if (trace.fraction >= 1.f || (trace.surface.flags & SURF_TRIGGER_MV)
            || (g_psFinderNoProps && MV_TraceIsProp(trace)))   // the same checkbox
        {
            DebugLog("PF: press ignored (frac=%.2f trigger=%d prop=%d) -- aim at a BRUSH wall to anchor",
                     trace.fraction, (int)((trace.surface.flags & SURF_TRIGGER_MV) != 0),
                     (int)MV_TraceIsProp(trace));
            return;
        }
        s_psFinderStart = vHit;
        s_psFinderEnd = vHit;
        s_psFinderNormal = trace.plane.normal;
        s_psFinderFirst = true;
        DebugLog("PF: anchor z=%.1f n=(%.2f %.2f %.2f)",
                 vHit.z, trace.plane.normal.x, trace.plane.normal.y, trace.plane.normal.z);
        return;
    }

    if (bHeld)
    {
        // while held: stretch the scan column to the current aim height
        if (s_psFinderFirst)
        {
            std::lock_guard<std::mutex> l(g_psLock);
            s_psFinderEnd.z = vHit.z;
        }
        return;
    }

    // release: brute-force the wall column for pixel-surf catch positions
    if (!s_psFinderFirst)
        return;
    s_psFinderFirst = false;

    Vec3 vWallNormal = s_psFinderNormal;
    vWallNormal.z = 0.f;
    if (vWallNormal.Length() < 0.1f)
    {
        DebugLog("PF: scan refused -- anchored face is flat (nz=%.2f), not a wall", s_psFinderNormal.z);
        return;
    }
    vWallNormal.Normalize();

    // anchor the chunked scan; the cursor + column live in the s_pf* statics and the work
    // itself runs in PfScanChunk, a budgeted slice per cmd 
    Vec3 vTangent = Vec3(vWallNormal.y, -vWallNormal.x, 0.f);   // wall tangent for the band sweep
    if (vTangent.Length() > 0.1f)
        vTangent.Normalize();

    const float flMargin = g_psFinderMargin;
    s_pfLow  = std::min(s_psFinderStart.z, s_psFinderEnd.z) - flMargin;
    s_pfHigh = std::max(s_psFinderStart.z, s_psFinderEnd.z) + flMargin;
    s_pfSteps = std::clamp(int(s_pfHigh - s_pfLow), 0, 600);

    s_pfNormal  = vWallNormal;
    s_pfTangent = vTangent;
    s_pfColumn  = s_psFinderStart;
    s_pfBandHalf = 16.f; s_pfBandStep = 4.f;   // the crosshair column's own band (map scan: 1 cell)
    s_pfQuiet = false;
    s_pfBaseButtons = pCmd->buttons;   // frozen for the whole scan, not re-read per cmd

    s_pfFound.clear();
    s_pfJumpPts.clear(); s_pfHeadbouncePts.clear();
    s_pfNextI = 0; s_pfNextLat = 0; s_pfHeightHit = false;
    s_pfMs = 0.0; s_pfChunks = 0; s_pfProbes = 0; s_pfPruned = 0;
    s_pfCells = 0; s_pfFalls = 0; s_pfConf = 0; s_pfNoStand = 0;
    s_pfSealed = 0; s_pfClipBlk = 0; s_pfDeep = 0; s_pfProp = 0; s_pfVoid = 0;
    s_pfFallMemo.clear();   // memo is per column -- a new anchor is new geometry
    s_pfBusy = true;

    DebugLog("PF: scan start z=%.1f..%.1f steps=%d margin=%.0f budget=%.1f ms/tick",
             s_pfLow, s_pfHigh, s_pfSteps, flMargin, (double)g_psFinderBudget);

    // the texturebug half is OPTIONAL now (g_psFinderTb). It is a different ride on a
    // different thing (two flush faces the solver clips apart, not a sub-unit lip), so a column
    // scanned for pixel surfs alone should not come back ringed in red. Off = no pass, no markers
    if (g_psFinderTb)
        PfScanSeams(pLocal);     // texturebugs are traces, not sims -- the whole pass fits here
    else
    {
        std::lock_guard<std::mutex> l(g_psLock);
        s_psTbPts.clear();
    }
    PfScanChunk(pLocal, pCmd);   // first slice runs on the release cmd itself
}

// ============================================================================
// PIXEL SURF ASSIST -- ONE AUTHORITY. THE SIM CATCHES IT, OR NOTHING FIRES.
// ============================================================================
// Full rewrite. ..built a tower of four tiers and six screens on top
// of a sim that HAS NEVER ONCE SEEN A CATCH: `c0 z0` on every candidate of every
// log ever taken, including the three arcs that rode live. So every fire this
// feature has ever made was decided by a PROXY -- an analytic grid ladder, an apex
// window, a height gap, a flat cap, a measured band -- and the standing lesson is
// exactly this: AN EXTRAPOLATION CANNOT CARRY A GUARANTEE, SIMULATE THE CLAIM.
//
// WHY THE SIM COULD NOT SEE IT, and it is one line: zeroed forwardmove and
// sidemove for the whole arc, while the LIVE tail presses wish 10 into
// the wall for 19-22 ticks of every hop (`press=21` on the outcome lines). The sim
// and the hull were flying two different commands. PixelFinder pins with that press
// (it is how every point got saved) and PsMeasureBand pinned with it too
// (`bandmax=-60` on every line) -- the ONLY probe that never had it is the one the
// picker actually fires on.
//
// So:
//   1. `PsHopInputs` is the ONE choreography. The sim calls it and the live tail
//      calls it, tick for tick, from the same launch index. Divergence is now
//      impossible by construction rather than by five builds of argument.
//   2. `PsSimHop` flies it and reports ONE thing: did the hull get PINNED at this
//      seam and STAY pinned. The pin test is PixelSurf's own discriminator
//      (at -halfgrav AND off the free-fall ladder), which admits a real clip on
//      tick one and refuses the free-fall graze.
//   3. The picker fires on `caught` and on nothing else. Tier 0/1/2/3, GridCheck,
//      arrives, gapFlat, perp, the flush trace, the band sweep, EffApex, maxover,
//      flSimHighBias and every constant they carried are DELETED. If no candidate
//      catches, no press goes out -- that is the "no misfires" half, and it is not
//      a tuning knob, it is the absence of one.
//
// WHAT THIS DOES NOT CLAIM. A pixel surf catch is decided by the LAUNCH: the hull's
// descent is a fixed ladder of ~1.2-1.4u steps and the seam is a sub-unit window,
// so any one arc is a lottery ticket (LAW 3). Ducking cannot fix that -- an air duck
// moves the origin by EXACTLY 20.000u (gamemovement.cpp:4113 FinishUnDuck, negated
// for FinishDuck), so it shifts the whole ladder and leaves its sub-unit phase
// untouched. What CAN fix it is re-rolling: the picker re-plans every command from a
// new stance, so a 30-tick approach offers ~30 x 7 draws and each one is now VERIFIED
// before it is spent. The assist waits, and the tick it fires on is a tick the engine
// has already shown catches.
//
// Types, and each one is a distinct simulated arc (bit k of a point's typeMask is
// aCands[k], readout order mini/regular/mini+duck/crouch/long/long+duck, bit 6 dash):
//   REGULAR  = jump
//   CROUCH   = jump, duck held from t1
//   MINI     = jump + duck on the launch cmd
//   MINIDUCK = MINI + the catch duck on the descent (a real +20u origin lift, and
//              the reason it is no longer byte-identical to MINI -- an earlier build
//              "a constant the sim cannot distinguish is fiction", fixed properly)
//   LONG     = jump + duck for cmds 0..2
//   LONGDUCK = LONG, duck held from t1
//   AIRDASH  = scout second jump, offered from the air (now just a 7th candidate
//              through the same authority instead of its own hand-rolled block)
// ============================================================================

// how the pin is recognised, everywhere in this file. PixelSurf uses the same pair
// (movement.cpp, ) and its counters are what proved them: `clip=6 stand=6`
// against `pin1=297` refused free-fall grazes in the log
static constexpr float k_psSurfTol = 1.5f;

// PixelFinder's probe number, verbatim (TryPin, above): "light into-wall press only --
// big speed pins flat on the main wall and falls past the lip". It is the wish that
// PROVED the point catchable, so it is the wish that reproduces it
static constexpr float k_psAssistPressWish = 10.f;
// the window the seam press is allowed in, unchanged from . Outside it the
// movement keys are the player's -- this is a press, not a steer lock
static constexpr float k_psPressZWin = 40.f;
static constexpr float k_psPressPad  = 32.f;

static constexpr float k_psCatchZ  = 8.f;   // a pin this close to the saved z IS this seam
static constexpr float k_psPastZ   = 16.f;  // fell this far under it: stop simming, nothing below can be it
static constexpr int   k_psConfirm = 3;     // airborne pin ticks that make a CATCH and not a graze
static constexpr float k_psMinApex = 8.f;   // smaller than any real launch (mini gains ~16)
static constexpr float k_psLaunchVz    = 100.f; // a real launch reads ~262 after FinishGravity
static constexpr int   k_psLaunchTries = 2;     // press, release, press -- more means it simply cannot
static constexpr int   k_psApproachTicks = 20;  // airborne: how far ahead ground is hunted (was 40 --
                                                // the picker re-plans every cmd, so a launch 30 ticks
                                                // out is found when it is 20, at half the sim cost)
static constexpr int   k_psMaxHopTicks   = 80;

// ---- THE ONE CHOREOGRAPHY -------------------------------------------------------
// Five builds argued about where the catch duck belongs and where the seam press
// belongs, because the sim's schedule and the live tail's schedule were two pieces of
// code that had to be kept in step by hand. They are one piece of code now. Whatever
// this returns is what the sim flew AND what the cmd carries.
//
// It reads: the type, the tick index since launch, the hull's own origin/velocity, and
// the target. It does NOT read a key, a view angle, or the player's speed -- /128
// still hold. The wish comes out in WORLD space and MV_WriteWishWorld rotates it into
// whatever frame the cmd happens to be in, so a player turning mid-hop changes the
// forwardmove/sidemove pair and never the direction the hull is actually pushed.
struct PsInputs_t
{
    bool  jump = false;
    bool  duck = false;
    bool  wish = false;      // overwrite forwardmove/sidemove with wishDir * wishMag
    Vec3  wishDir = {};
    float wishMag = 0.f;
};

static PsInputs_t PsHopInputs(int iType, int iSince, const Vec3& vOrigin, const Vec3& vVel,
                              const Vec3& vTarget, float flHullReachXY)
{
    PsInputs_t o;
    if (iSince < 0)
        return o;

    const bool bMini = iType == JT_MINI || iType == JT_MINIDUCK;
    const bool bLong = iType == JT_LONG || iType == JT_LONGDUCK;
    const bool bHold = iType == JT_CROUCH || iType == JT_LONGDUCK;

    // the launch press rides tick 0, and the launch-type duck rides the SAME cmd:
    // a FRESH duck on the launch cmd is taken, only an ALREADY-held one eats the jump
    o.jump = (iSince == 0);
    if ((bMini || bLong) && iSince == 0) o.duck = true;
    if (bLong && iSince >= 1 && iSince <= 2) o.duck = true;
    if (bHold && iSince >= 1) o.duck = true;

    // THE CATCH DUCK, and it belongs to exactly one type. An air duck adds the whole
    // hull delta to the origin (FinishDuck, 82-62 = 20u) the tick it is pressed, so
    // MINIDUCK is genuinely "MINI, 20u higher from the apex on" -- a different arc, in
    // the sim as well as live. An earlier build deleted the +20 because it was a number bolted
    // onto a candidate the sim could not tell apart; it is a simulated press now
    if (iType == JT_MINIDUCK && iSince >= 1 && vVel.z < 0.f) o.duck = true;

    // THE SEAM PRESS. PixelFinder proved every one of these points with the hull driven
    // gently into the face (vInto * 10); a hull merely falling PAST the wall has nothing
    // for the solver to clip and vz walks the free-fall ladder straight by. Descending
    // only, near the seam's height, and within reach of it -- the same window since
    // and now the sim flies it too, which is the whole rewrite
    if (iSince >= 1 && vVel.z < 0.f)
    {
        const Vec3 vFlat(vTarget.x - vOrigin.x, vTarget.y - vOrigin.y, 0.f);
        const float flFlat = vFlat.Length2D();
        if (fabsf(vOrigin.z - vTarget.z) < k_psPressZWin
            && flFlat <= flHullReachXY + k_psPressPad && flFlat > 0.1f)
        {
            o.wish = true;
            o.wishDir = vFlat;
            o.wishMag = k_psAssistPressWish;
        }
    }
    return o;
}

// ---- one simulated hop, and the only question it answers ------------------------
struct PsHop_t
{
    int   launchDelay = -1;   // ticks from now to the cmd the engine took the jump on
    float launchZ  = 0.f;
    float apexGain = 0.f;
    bool  sawApex  = false;   // a descending airborne tick happened, so apexGain is real
    bool  landed   = false;   // ended on the FLOOR -- this hop finishes on the ground
    bool  truncated = false;  // cut by the sim clock: apexGain is a lower bound, refuse it
    int   refused  = 0;       // launch presses CheckJumpButton threw away
    // THE ANSWER
    bool  caught    = false;  // pinned at this seam and STAYED pinned for k_psConfirm ticks
    int   catchSince = -1;    // ticks after launch the pin landed on
    float catchZ    = 0.f;    // where it pinned (target.z +- k_psCatchZ)
    float catchFlat = 0.f;    // 2D distance to the point at the pin
    int   pinTicks  = 0;      // pin ticks seen at all -- `pin0` forever means the sim STILL
                              // cannot see the catch, and that is the next thing to read
    float gap       = 1e9f;   // closest the descent came to the seam's height (diag only)
    float gapVz     = 0.f;
    int   sims      = 0;
};

static PsHop_t PsSimHop(void* pLocal, UserCmd_t* pCmd, int iType, const Vec3& vTarget,
                        float flHullReachXY)
{
    PsHop_t r;

    const float flGravity  = CvarFloat("sv_gravity", 800.f);
    const float flTick     = TickInterval();
    const float flHalfGrav = flGravity * flTick * 0.5f;

    const int   iBtnBackup   = pCmd->buttons;
    const float flFwdBackup  = pCmd->forwardmove;
    const float flSideBackup = pCmd->sidemove;

    // pin the base prediction frame -- a bare restore drifts when several sims run back to back
    const int nBase = PredCommandsPredicted();
    RestoreToFrame(nBase - 1);

    int   iLaunch = -1;
    Vec3  vLaunch = EntV3(pLocal, g_offOrigin);
    float flPeakZ = -1e9f;

    // THE LAUNCH IS CONFIRMED, NOT ASSUMED. TF2 refuses the press outright on
    // FL_DUCKING and on the m_nOldButtons IN_JUMP latch (tf_gamemovement.cpp:1160), and only
    // a cmd sent WITHOUT IN_JUMP clears that latch (gamemovement.cpp:2545). Offer the press,
    // then read the engine back: without this a grounded slide is recorded as a 0u hop
    int   iPressTick = -2;
    int   iTries     = 0;
    Vec3  vPress     = vLaunch;
    float flPressVz  = 0.f;

    int  iGroundTicks = 0;
    int  iPinRun      = 0;
    bool bPinAtSeam   = false;

    for (int t = 0; t < k_psMaxHopTicks; t++)
    {
        if (SimsLeft() <= 0)
        {
            // SAY SO. An arc stopped here has its peak wherever the clock landed
            if (iLaunch >= 0)
                r.truncated = true;
            break;
        }

        const Vec3 vNowPre = EntV3(pLocal, g_offOrigin);
        const Vec3 vVelPre = EntV3(pLocal, g_offVelocity);

        pCmd->buttons = iBtnBackup & ~(IN_JUMP | IN_DUCK);
        // Z ONLY is the DEFAULT, not the whole story: no wish unless the choreography
        // asks for one, so nothing but the seam press can bend this arc and neither the view
        // angle nor a movement key is ever read
        pCmd->forwardmove = 0.f;
        pCmd->sidemove = 0.f;

        bool bPressNow = false;
        if (iLaunch < 0)
        {
            const bool bGroundedNow = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
            if ((iType == JT_AIRDASH || bGroundedNow)
                && iPressTick != t - 1 && iTries < k_psLaunchTries)
            {
                bPressNow  = true;
                iPressTick = t;
                iTries++;
                vPress    = vNowPre;
                flPressVz = vVelPre.z;
            }
            else if (t >= k_psApproachTicks || iTries >= k_psLaunchTries)
                break;      // no ground in the window, or this stance simply cannot jump
        }

        const int iSince = iLaunch >= 0 ? t - iLaunch : (bPressNow ? 0 : -1);
        const PsInputs_t in = PsHopInputs(iType, iSince, vNowPre, vVelPre, vTarget, flHullReachXY);
        if (in.jump) pCmd->buttons |= IN_JUMP;
        if (in.duck) pCmd->buttons |= IN_DUCK;
        if (in.wish) MV_WriteWishWorld(pCmd, in.wishDir, in.wishMag);

        const float flPreVz = vVelPre.z;
        Pred_Simulate(pLocal, pCmd);
        r.sims++;

        if (iLaunch < 0)
        {
            // did the ENGINE take it? A refused press leaves the hull grounded with
            // vz untouched. The second clause is for the dash, offered while already airborne:
            // vz alone can be over the bar with no dash having happened
            if (bPressNow)
            {
                const bool  bAirNow = (EntI(pLocal, g_offFlags) & FL_ONGROUND) == 0;
                const float flVzNow = EntV3(pLocal, g_offVelocity).z;
                if (bAirNow && flVzNow > k_psLaunchVz && flVzNow > flPressVz)
                {
                    iLaunch = t;
                    vLaunch = vPress;
                    flPeakZ = vPress.z;
                }
                else
                    r.refused++;
            }
            if (iLaunch < 0)
                continue;
        }

        const Vec3  vNow    = EntV3(pLocal, g_offOrigin);
        const Vec3  vVelNow = EntV3(pLocal, g_offVelocity);
        const float flZ     = vNow.z;
        const float flVz    = vVelNow.z;
        const bool  bAir    = (EntI(pLocal, g_offFlags) & FL_ONGROUND) == 0;
        if (flZ > flPeakZ)
            flPeakZ = flZ;

        const float flFlatNow = Vec3(vTarget.x - vNow.x, vTarget.y - vNow.y, 0.f).Length2D();
        const float flStepXY  = Vec3(vVelNow.x, vVelNow.y, 0.f).Length2D() * flTick;
        // is the hull OVER the point at all? A pin two hundred units away is somebody else's
        // lip. One tick of travel added because a window narrower than one tick of movement
        // gets stepped clean over (LAW 2)
        const bool bOverPoint = flFlatNow <= flHullReachXY + flStepXY;

        if (bAir && flVz < 0.f)
        {
            r.sawApex = true;
            const float flGap = fabsf(flZ - vTarget.z);
            if (flGap < r.gap) { r.gap = flGap; r.gapVz = flVz; }
        }

        // ---- THE PIN, and it is the whole feature ----------------------------------------
        // Gravity is the entire model of an untouched tick: StartGravity + FinishGravity take
        // vz down by exactly one full step, so a free tick lands on flPreVz - 2*half BY
        // CONSTRUCTION. Reading -half because a SURFACE clipped the velocity means vz is
        // somewhere that ladder cannot put it. An earlier build required the pin to repeat and got
        // `latch=0` against `pin1=689`; an earlier build's off-the-ladder test admits the real clip on
        // tick one and refuses the graze, and PixelSurf's counters have carried it since
        const float flFree    = flPreVz - flHalfGrav * 2.f;
        const bool  bClipped  = fabsf(flVz - flFree) > k_psSurfTol;
        const bool  bAtSurfVz = fabsf(flVz + flHalfGrav) < k_psSurfTol;

        if (bAtSurfVz && bAir && (bClipped || iPinRun > 0))
        {
            r.pinTicks++;
            if (iPinRun == 0)
            {
                // a first pin only counts as OURS if it happens at this seam
                if (bOverPoint && fabsf(flZ - vTarget.z) <= k_psCatchZ)
                {
                    bPinAtSeam = true;
                    r.catchSince = iSince;
                    r.catchZ = flZ;
                    r.catchFlat = flFlatNow;
                }
            }
            if (bPinAtSeam)
            {
                // A CATCH IS A RIDE, NOT A TOUCH. One clipped tick is a graze off a lip the
                // hull is already past; the thing the user asked for is the surf holding. So
                // the sim keeps flying the SAME choreography the live tail will and requires
                // the pin to still be there k_psConfirm ticks later
                if (++iPinRun >= k_psConfirm)
                {
                    r.caught = true;
                    break;
                }
            }
        }
        else
        {
            iPinRun = 0;
            bPinAtSeam = false;
        }

        // landing check: a single grounded tick is a bump clipped mid-flight
        iGroundTicks = bAir ? 0 : iGroundTicks + 1;
        if (iSince > 2 && iGroundTicks >= 2)
        {
            r.landed = true;
            break;
        }
        if (flVz < 0.f && flZ < vTarget.z - k_psPastZ)
            break;      // fell past the seam -- nothing under it is the point we were sent at
    }

    pCmd->buttons = iBtnBackup;
    pCmd->forwardmove = flFwdBackup;
    pCmd->sidemove = flSideBackup;
    RestoreToFrame(nBase - 1);

    r.launchDelay = iLaunch;
    r.launchZ = vLaunch.z;
    r.apexGain = iLaunch >= 0 ? flPeakZ - vLaunch.z : 0.f;
    return r;
}

// ---- live hop state -------------------------------------------------------------
static int   s_asJumpType = JT_NONE;
static int   s_asHopTick  = 0;      // ticks since the launch cmd, OUR counter. g_globalVars
                                    // ->tickcount is rewritten by Pred_Start's sims and a
                                    // choked/rebuilt cmd stream can skip it, so the live
                                    // choreography counts commands the way the sim counts sims
static Vec3  s_asHopTarget = {};
static bool  s_asSetHeld = false, s_asDelHeld = false;
// the promise, so the outcome line is promise-vs-result and not another guess
static int   s_asPromCatch = -1;
static float s_asPromApex = 0.f, s_asPromNeed = 0.f;

// the pending launch: a pick armed mid-air fires on the REAL ground contact
static int   s_asPendingType = JT_NONE;
static int   s_asPendingTicks = 0;      // countdown, not a tickcount deadline (see s_asHopTick)
static Vec3  s_asPendingTarget = {};
static int   s_asPendingIdx = -1;
static float s_asPendingLaunchZ = 0.f;  // feet height the sim proved this arc from (see the guard)

// ---- THE RIDE HOLD ------------------------------------------------------
// The hop is over the moment s_psRidingNow goes true, and until now that also ended the
// seam press -- on the very tick the surf was caught. PixelSurf's own hold only presses
// when the player's wish is EXACTLY zero (movement.cpp, the else branch), so anyone still
// holding W drops straight off the lip they just landed on. The sim proved the pin holds
// under a continued press, so live continues it: released by ground, death, or the feature
static bool  s_asRideHold = false;
static Vec3  s_asRideTarget = {};
static int   s_asRideTicks = 0, s_asRideMiss = 0;
static constexpr int k_psRideHoldMax = 400;   // ~6s; a ride longer than that holds itself
static constexpr int k_psRideMissMax = 8;     // pinless ticks that mean the ride is over

// ---- COMBOS. THE POINT YOU JUST SURFED IS NOT THE ONE YOU ARE JUMPING TO ----
// Targets are ranked by distance, and the seam you are standing on -- or just left -- is
// always the nearest one there is, so on the second surf of a combo slot 0 of the sweep is
// the OLD seam and it drains the clock planning a jump you already made. A PENALTY, never a
// ban: alone in range it is picked and fired exactly as before
static int s_psUsedIdx  = -1;
static int s_psUsedTick = 0;
static constexpr float k_psOnPointDist = 96.f;
static constexpr float k_psUsedPenalty = 1e6f;

// ---- the outcome line: what the hull ACTUALLY did -------------------------------
static bool  s_asOutOn = false;
static int   s_asOutType = JT_NONE;
static Vec3  s_asOutTarget = {};
static float s_asOutLaunchZ = 0.f, s_asOutPeakZ = -1e9f;
static float s_asOutGap = 1e9f, s_asOutGapVz = 0.f;
static float s_asOutPerp = -1.f, s_asOutLat = -1.f;
static bool  s_asOutRode = false;
static int   s_asOutRideTick = -1;     // the tick the ride actually started on
static int   s_asOutTicks = 0, s_asOutPressed = 0;

static void PsOutcomeBegin(float flLaunchZ, const Vec3& vTarget, int iType,
                           float flPromApex, float flPromNeed, int iPromCatch)
{
    s_asOutOn = true;
    s_asOutType = iType;
    s_asOutTarget = vTarget;
    s_asOutLaunchZ = flLaunchZ;
    s_asOutPeakZ = flLaunchZ;
    s_asOutGap = 1e9f; s_asOutGapVz = 0.f;
    s_asOutPerp = -1.f; s_asOutLat = -1.f;
    s_asOutRode = false; s_asOutRideTick = -1;
    s_asOutTicks = 0; s_asOutPressed = 0;
    s_asPromApex = flPromApex; s_asPromNeed = flPromNeed; s_asPromCatch = iPromCatch;
}

// scout double jump budget (CTFPlayer::CanAirDash lite): 1, or 2 with the Atomizer
static bool CanAirDashNow(void* pLocal)
{
    if (g_offAirDash <= 0 || g_offClass <= 0)
        return false;
    if (EntI(pLocal, g_offClass) != 1 /* TF_CLASS_SCOUT */)
        return false;
    int iDashMax = 1;
    if (g_offActiveWeapon > 0 && g_offItemDefIndex > 0)
    {
        const unsigned uWpnHandle = *(unsigned*)((uintptr_t)pLocal + g_offActiveWeapon);
        const int iWpnIdx = (int)(uWpnHandle & 0xFFFF);
        if (uWpnHandle != 0xFFFFFFFF && iWpnIdx >= 1 && iWpnIdx < 2048)
            if (void* pWeapon = GetClientEntity(iWpnIdx))
                if (EntI(pWeapon, g_offItemDefIndex) == 450 /* Atomizer */)
                    iDashMax = 2;
    }
    return EntI(pLocal, g_offAirDash) < iDashMax;
}

// ---- UNEVEN GROUND, PART ONE: WALK ME UNDER THE SEAM ------------------------------------------
// PsSimHop flies a Z-ONLY arc (no wish at all but the seam press), so the assist can only fire
// from roughly under the point -- `worth=0` is the sweep saying "you are not standing there".
// On flat ground you line that up by eye. On stairs, a slope or a stack of crates the spot that
// works is a few units off the one you walked to, and no amount of clock finds a hop from the
// wrong place. So while you are on the ground with a target in range, YOUR OWN move is rotated
// toward it, capped so it stays a walk and not a snap.
// Nothing is ever pressed: with no movement key held forwardmove/sidemove are 0 and this is a
// no-op, which is the whole reason it is safe to leave on.
static void PsGroundHelp(UserCmd_t* pCmd, const Vec3& vOrigin, const Vec3& vTarget)
{
    if (pCmd->forwardmove == 0.f && pCmd->sidemove == 0.f)
        return;
    const Vec3 vFlat(vTarget.x - vOrigin.x, vTarget.y - vOrigin.y, 0.f);
    if (vFlat.Length2D() < 1.f)
        return;                                      // standing on it already
    const float flWant = RAD2DEGF(atan2f(vFlat.y, vFlat.x));
    const float flCap  = std::clamp(g_psGroundHelpAngle, 5.f, 90.f);
    const float flDiff = std::clamp(NormalizeAngle(flWant - pCmd->viewangles.y), -flCap, flCap);
    if (fabsf(flDiff) < 0.01f)
        return;
    Vec3 vSteer = pCmd->viewangles;
    vSteer.y = NormalizeAngle(vSteer.y + flDiff);
    CorrectMovement(pCmd, vSteer, pCmd->viewangles);   // silent: the view never moves
}

// jump box popup ("minijump to <z>"), render thread reads
static char s_asBoxText[64] = {};
static unsigned long long s_asBoxMs = 0;

static void PsStampJumpBox(int iType, float flTargetZ)
{
    std::lock_guard<std::mutex> l(g_psLock);
    snprintf(s_asBoxText, sizeof(s_asBoxText), "%s to %.3f", PsTypeName(iType), flTargetZ);
    s_asBoxMs = GetTickCount64();
}

// draw heartbeat + map tracking, and NOTHING else -- no sim, no press, no bookkeeping.
// the points overlay drops off the screen after 500ms of silence (render.cpp:189), so a cmd
// that runs no assist at all -- noclip, water, a taunt -- still has to stamp this or the finder's
// own results vanish while you fly around looking at them. Movement_RunImpl calls it on those cmds
static void PS_PublishAlive(void* pLocal)
{
    const char* szMap = EngineLevelName();
    if (strncmp(szMap, s_psMap, sizeof(s_psMap) - 1) != 0)
        PsLoadPointsForMap(szMap);

    const Vec3 vOrigin = EntV3(pLocal, g_offOrigin);
    std::lock_guard<std::mutex> l(g_psLock);
    s_psLocalOrigin = vOrigin;
    s_psAliveMs = GetTickCount64();
}

static void PS_AssistTick(void* pLocal, UserCmd_t* pCmd, bool bPredicted)
{
    // draw heartbeat + map tracking run whenever the pipeline is alive
    PS_PublishAlive(pLocal);
    const Vec3 vOrigin = EntV3(pLocal, g_offOrigin);

    s_psAssistPressIn = false;   // per-cmd, re-earned by the choreography below
    // real ground contact is the PRE-prediction flag (post-prediction already reflects the
    // cmd we are building) -- same as MiniJump / LongJump
    const bool bOnGround = (s_prePredFlags & FL_ONGROUND) != 0;
    const bool bAssistOn = On(g_psAssist, g_psAssistKey, g_psAssistMode);
    const bool bAlive    = EntAlive(pLocal);
    const float flHullReachXY = g_offMaxs > 0 ? EntV3(pLocal, g_offMaxs).Length2D() : 34.6f;

    // ---- hop bookkeeping, UNCONDITIONAL and before any early return ----------------------
    if (s_asJumpType != JT_NONE)
    {
        s_asHopTick++;
        // the ride ENDS the hop and STARTS the hold: the press that catches the seam is the
        // press that keeps the hull on it, and PixelSurf's own hold refuses to run while the
        // player is still pressing a movement key
        if (s_psRidingNow)
        {
            s_asRideHold = true;
            s_asRideTarget = s_asHopTarget;
            s_asRideTicks = 0;
            s_asRideMiss = 0;
            s_asJumpType = JT_NONE;
        }
        else if ((bOnGround && s_asHopTick > 0) || !bAssistOn || !bAlive
                 || s_asHopTick > k_psMaxHopTicks + 40)
            s_asJumpType = JT_NONE;
    }
    if (s_asRideHold)
    {
        // the hold ends when the RIDE does, not when the feet land. A few pinless ticks are a
        // gap in the surf and the press is what closes them; a long run of them means the hull
        // is falling away and the picker has to be free to plan the next seam on the way down
        s_asRideMiss = s_psRidingNow ? 0 : s_asRideMiss + 1;
        if (bOnGround || !bAssistOn || !bAlive || s_asRideMiss > k_psRideMissMax
            || ++s_asRideTicks > k_psRideHoldMax)
            s_asRideHold = false;
    }
    if (s_asPendingType != JT_NONE)
    {
        if (--s_asPendingTicks <= 0 || s_psRidingNow || !bAssistOn || !bAlive)
        {
            s_asPendingType = JT_NONE;
            s_asPendingIdx = -1;
        }
    }

    // ---- THE CATCH DUCK OUTLIVES THE HOP -----------------------------------------
    // bHold's crouch is the duck the sim flew all the way to the seam. The hop is cleared on
    // the very tick the surf is caught, so without this the crouch stops there, airborne
    // FinishUnDuck drops the origin 20u and the seam is gone -- "catches the ps with a crouch,
    // then uncrouches and falls off", exactly. Only ground, death or the feature ends it
    if (s_psAssistDuckHeld)
    {
        if (bOnGround || !g_psAssist || !bAlive)
            s_psAssistDuckHeld = false;
        else
            pCmd->buttons |= IN_DUCK;
    }

    // ---- the outcome sampler --------------------------------------------------------------
    if (s_asOutOn)
    {
        s_asOutTicks++;
        if (vOrigin.z > s_asOutPeakZ) s_asOutPeakZ = vOrigin.z;
        if (s_psRidingNow && !s_asOutRode)
        {
            s_asOutRode = true;
            s_asOutRideTick = s_asOutTicks;
        }
        if (s_prePredVel.z < 0.f)
        {
            const float flGap = fabsf(vOrigin.z - s_asOutTarget.z);
            if (flGap < s_asOutGap)
            {
                s_asOutGap = flGap;
                s_asOutGapVz = s_prePredVel.z;
                // perp / lat: the 2D miss split along the face normal. perp is the axis a pin
                // lives or dies on; lat is slide along the seam, which is a strip and free
                Vec3 vTo = Vec3(s_asOutTarget.x - vOrigin.x, s_asOutTarget.y - vOrigin.y, 0.f);
                const float flTo = vTo.Length2D();
                if (flTo > 0.1f)
                {
                    Vec3 vDir = vTo; vDir.Normalize();
                    TraceFilterWorldProps f; f.Setup(pLocal);
                    GameTrace_t tr{};
                    TraceHull(vOrigin, vOrigin + vDir * 48.f,
                              EntV3(pLocal, g_offMins), EntV3(pLocal, g_offMaxs),
                              MASK_PLAYERSOLID, &f, &tr);
                    if (tr.fraction < 1.f && fabsf(tr.plane.normal.z) < 0.7f)
                    {
                        Vec3 vNn(tr.plane.normal.x, tr.plane.normal.y, 0.f);
                        if (vNn.Length2D() > 0.1f)
                        {
                            vNn.Normalize();
                            s_asOutPerp = fabsf(vTo.x * vNn.x + vTo.y * vNn.y);
                            const float flSq = flTo * flTo - s_asOutPerp * s_asOutPerp;
                            s_asOutLat = flSq > 0.f ? sqrtf(flSq) : 0.f;
                        }
                    }
                }
            }
        }
        // print when the hop AND the hold are both done -- the ride is part of the result
        if ((s_asJumpType == JT_NONE && !s_asRideHold) || s_asOutTicks > 240)
        {
            // promCatch is the tick the SIM said the pin would land on; rideAt is the tick the
            // engine actually latched one. They should agree to a tick or two. If rode=0 with
            // promCatch >= 0, the sim promised a catch the live hull did not get -- and THAT is
            // a divergence between the choreography the sim flew and the one the cmd carried,
            // which is the only way this build can misfire
            DebugLog("PS: outcome %s promA=%.1f promNeed=%.1f promCatch=%d | liveA=%.1f "
                     "gap=%.3f step=%.2f zf=%.3f perp=%.3f lat=%.2f press=%d rode=%d rideAt=%d "
                     "ticks=%d",
                     PsTypeName(s_asOutType), s_asPromApex, s_asPromNeed, s_asPromCatch,
                     s_asOutPeakZ - s_asOutLaunchZ,
                     s_asOutGap > 1e8f ? -1.f : s_asOutGap,
                     fabsf(s_asOutGapVz) * TickInterval(),
                     s_asOutTarget.z - truncf(s_asOutTarget.z),
                     s_asOutPerp, s_asOutLat, s_asOutPressed,
                     (int)s_asOutRode, s_asOutRideTick, s_asOutTicks);
            s_asOutOn = false;
        }
    }

    // ---- where the crosshair hits the world (set-point / delete-point) --------------------
    auto AimHit = [&](Vec3& vOut) -> bool
    {
        if (g_offViewOffset <= 0) return false;
        Vec3 vForward; AngleVectors(pCmd->viewangles, &vForward);
        const Vec3 vEye = vOrigin + EntV3(pLocal, g_offViewOffset);
        TraceFilterWorldProps filter; filter.Setup(pLocal);
        GameTrace_t trace{};
        TraceLine(vEye, vEye + vForward * 4000.f, MASK_PLAYERSOLID, &filter, &trace);
        vOut = trace.endpos;
        return trace.fraction < 1.f;
    };

    const bool bSetHeld = g_psSetPointKey != 0 && MovementKeyOK(g_psSetPointKey, 1);
    if (bSetHeld && !s_asSetHeld)
    {
        Vec3 vHit;
        if (AimHit(vHit))
        {
            std::lock_guard<std::mutex> l(g_psLock);
            const Vec3* pSnap = nullptr;
            float flBest = g_psAssistSnap;
            for (const Vec3& p : s_psFinderPts)
            {
                const float flDist = vHit.DistTo(p);
                if (flDist < flBest) { flBest = flDist; pSnap = &p; }
            }
            if (pSnap)
            {
                s_psAssistPts.push_back({ *pSnap, 127 });
                s_psGen++;
                PsWritePoints();
            }
        }
    }
    s_asSetHeld = bSetHeld;

    const bool bDelHeld = g_psDeletePointKey != 0 && MovementKeyOK(g_psDeletePointKey, 1);
    if (bDelHeld && !s_asDelHeld)
    {
        Vec3 vHit;
        if (AimHit(vHit))
        {
            std::lock_guard<std::mutex> l(g_psLock);
            int iErase = -1;
            float flBest = g_psAssistSnap;
            for (int i = 0; i < (int)s_psAssistPts.size(); i++)
            {
                const float flDist = vHit.DistTo(s_psAssistPts[i].pos);
                if (flDist < flBest) { flBest = flDist; iErase = i; }
            }
            if (iErase >= 0)
            {
                s_psAssistPts.erase(s_psAssistPts.begin() + iErase);
                s_psGen++;
                PsWritePoints();
            }
            else
            {
                flBest = g_psAssistSnap;
                for (int i = 0; i < (int)s_psFinderPts.size(); i++)
                {
                    const float flDist = vHit.DistTo(s_psFinderPts[i]);
                    if (flDist < flBest) { flBest = flDist; iErase = i; }
                }
                if (iErase >= 0)
                    s_psFinderPts.erase(s_psFinderPts.begin() + iErase);
            }
        }
    }
    s_asDelHeld = bDelHeld;

    // ---- every active saved point inside its own radius, nearest first -------------------
    struct PsTarget_t { float dist; float rank; Vec3 pos; int mask; float engage; int idx; };
    static constexpr int k_psMaxTargets = 8;   // == the slider's ceiling
    PsTarget_t aTargets[k_psMaxTargets];
    int nTargets = 0;
    {
        std::lock_guard<std::mutex> l(g_psLock);
        if (s_psRidingNow)
        {
            int iOn = -1; float flBest = k_psOnPointDist;
            for (int i = 0; i < (int)s_psAssistPts.size(); i++)
            {
                const float flDist = vOrigin.DistTo(s_psAssistPts[i].pos);
                if (flDist < flBest) { flBest = flDist; iOn = i; }
            }
            if (iOn >= 0) { s_psUsedIdx = iOn; s_psUsedTick = g_globalVars->tickcount; }
        }
        if (s_psUsedIdx >= 0)
        {
            const float flTick = TickInterval();
            const int iHold = flTick > 0.f ? (int)(g_psAssistReuseDelay / flTick) : 0;
            const int iNowT = g_globalVars->tickcount;
            if (iNowT < s_psUsedTick || iNowT - s_psUsedTick > iHold
                || s_psUsedIdx >= (int)s_psAssistPts.size())
                s_psUsedIdx = -1;
        }
    }
    {
        std::lock_guard<std::mutex> l(g_psLock);
        s_psAssistActive = -1;
        if (bAssistOn)
        {
            const int nWant = g_psAssistMaxPoints < 1 ? 1
                            : (g_psAssistMaxPoints > k_psMaxTargets ? k_psMaxTargets
                                                                    : g_psAssistMaxPoints);
            for (int i = 0; i < (int)s_psAssistPts.size(); i++)
            {
                const PsAssistPt& pt = s_psAssistPts[i];
                const float flRadius = pt.radius > 0.f ? pt.radius : g_psAssistRadius;
                const float flDist = vOrigin.DistTo(pt.pos);
                if (flDist > flRadius)
                    continue;
                const float flRank = flDist + (i == s_psUsedIdx ? k_psUsedPenalty : 0.f);
                if (nTargets == nWant && flRank >= aTargets[nTargets - 1].rank)
                    continue;
                int k = nTargets < nWant ? nTargets++ : nWant - 1;
                for (; k > 0 && aTargets[k - 1].rank > flRank; k--)
                    aTargets[k] = aTargets[k - 1];
                aTargets[k] = { flDist, flRank, pt.pos, pt.typeMask, flRadius, i };
            }
            if (nTargets > 0)
                s_psAssistActive = aTargets[0].idx;
            if (s_asPendingType != JT_NONE && s_asPendingIdx >= 0
                && s_asPendingIdx < (int)s_psAssistPts.size())
                s_psAssistActive = s_asPendingIdx;
        }
    }

    // ---- the live choreography, run at the very end so a hop fired THIS cmd gets tick 0 ---
    // Declared here as a lambda because both the early-out path and the normal path have to
    // reach it: a hop already in flight must keep flying even on a cmd where the picker never
    // runs (out of range, out of clock, feature key released mid-hop)
    auto FlyLiveTail = [&]()
    {
        // s_prePredOrigin / s_prePredVel, NOT the post-prediction entity: PsSimHop reads the hull
        // at the START of each simulated tick, so the live tail has to ask the same question of
        // the same tick or the two schedules are one command out of step at the window edges
        const Vec3& vAt = s_prePredOrigin;
        if (s_asJumpType != JT_NONE)
        {
            const PsInputs_t in = PsHopInputs(s_asJumpType, s_asHopTick, vAt, s_prePredVel,
                                              s_asHopTarget, flHullReachXY);
            // ---- THE HOP OWNS THESE TWO BITS, SET *AND* CLEAR ---------------------------
            // PsSimHop starts every simulated tick from `buttons & ~(IN_JUMP|IN_DUCK)` and then
            // adds exactly what PsHopInputs asks for. A live tail that only ORs is therefore
            // NOT flying the arc that was validated the moment the player is holding crouch --
            // an air duck they own is a 20u origin lift the sim never saw, which is the whole
            // hop. So while a hop is in flight the assist owns both bits, exactly as the sim
            // had them. The hop is already cleared by the time the feet are back on the ground
            // (the bookkeeping above reads s_prePredFlags), so a bhop's own jump is never eaten
            if (in.jump) pCmd->buttons |= IN_JUMP; else pCmd->buttons &= ~IN_JUMP;
            if (in.duck || s_psAssistDuckHeld) pCmd->buttons |= IN_DUCK;
            else                               pCmd->buttons &= ~IN_DUCK;
            if (in.wish)
            {
                MV_WriteWishWorld(pCmd, in.wishDir, in.wishMag);
                s_psAssistPressIn = true;   // AutoAlign runs after us and writes the same pair
                s_asOutPressed++;
            }
            // the crouch types' duck is the one that must survive the hop being cleared at the
            // catch; long's t1-2 launch duck is a launch shape and is meant to be released
            if (s_asJumpType == JT_CROUCH || s_asJumpType == JT_LONGDUCK
                || s_asJumpType == JT_MINIDUCK)
            {
                if (in.duck) s_psAssistDuckHeld = true;
            }
        }
        else if (s_asRideHold)
        {
            // keep the seam press on a caught surf. Same wish, same window, no duck decisions
            const Vec3 vFlat(s_asRideTarget.x - vAt.x, s_asRideTarget.y - vAt.y, 0.f);
            const float flFlat = vFlat.Length2D();
            if (flFlat <= flHullReachXY + k_psPressPad && flFlat > 0.1f)
            {
                MV_WriteWishWorld(pCmd, vFlat, k_psAssistPressWish);
                s_psAssistPressIn = true;
            }
        }
        else if (bOnGround && g_psGroundHelp && nTargets > 0)
        {
            // no arc owns the cmd: the walk-up is ours to help with (see PsGroundHelp)
            PsGroundHelp(pCmd, vAt, aTargets[0].pos);
        }
        s_psAssistHopActive = s_asJumpType != JT_NONE || s_asPendingType != JT_NONE
                            || s_asRideHold;
    };

    if (!bAssistOn || nTargets == 0)
    {
        FlyLiveTail();
        return;
    }

    // ---- pending-launch service: the sim already validated the whole approach -------------
    if (s_asPendingType != JT_NONE && s_asJumpType == JT_NONE && bOnGround)
    {
        const int iPick = s_asPendingType;
        s_asPendingType = JT_NONE;
        s_asPendingIdx = -1;

        // ---- UNEVEN GROUND, PART TWO: THE FLOOR MOVED UNDER THE PLAN ---------------------
        // A queued launch waits for the REAL ground contact, which can be several cmds and
        // several steps later. The arc it is holding was proved from one feet height, and a
        // hop's whole job is arriving at the seam within a couple of units -- so firing it off
        // a stair tread 8u above the one the sim launched from is a miss the sweep already
        // knows how to avoid. Drop it instead and re-plan from where the feet actually are:
        // the picker runs again next cmd and costs the same sims it was going to cost anyway.
        const float flDrift = fabsf(vOrigin.z - s_asPendingLaunchZ);
        if (g_psLaunchZSlack > 0.f && flDrift > g_psLaunchZSlack)
        {
            DebugLog("PS: pending dropped=%s floor moved %.2fu (slack %.2f)",
                     PsTypeName(iPick), flDrift, g_psLaunchZSlack);
            FlyLiveTail();
            return;
        }

        s_asJumpType = iPick;
        s_asHopTick = 0;
        s_asHopTarget = s_asPendingTarget;
        PsOutcomeBegin(vOrigin.z, s_asPendingTarget, iPick, s_asPromApex, s_asPromNeed,
                       s_asPromCatch);
        if (g_psAssistJumpBox)
            PsStampJumpBox(iPick, s_asPendingTarget.z);
        DebugLog("PS: pending fired=%s z=%.3f", PsTypeName(iPick), s_asPendingTarget.z);
        FlyLiveTail();      // tick 0 of the hop is THIS cmd
        return;
    }

    // ---- the sweep ------------------------------------------------------------------------
    // The clock is shared with every other movement feature. Each target gets an even cut of
    // what is LEFT (SimsLeft() returns 0 the instant TbBudgetExpired(), so one target
    // used to drain the whole share and slot 1 was never planned at all)
    const long long usClockTotal = (long long)(TickInterval() * k_cmdTimeShare * 1000000.f);
    int nPlanned = 0, nSimmedCands = 0, nCaught = 0;

    // apex memory, per type. The sim is the authority and nothing is PRUNED by this -- it only
    // decides the ORDER the candidates are simmed in, so the arc whose reach matches the seam
    // is tried first and the break-on-catch below usually pays for one or two arcs instead of
    // seven. A stale entry costs order, never a decision
    static float s_apexSeen[JT_AIRDASH + 1] = {};

    for (int iT = 0; iT < nTargets; iT++)
    {
        const long long usUsedNow = TbBudgetUsedUs();
        if (SimsLeft() < 32 || usUsedNow >= usClockTotal)
            break;
        const long long usTargetEnd = usUsedNow + (usClockTotal - usUsedNow) / (nTargets - iT);

        const int   iTargetMask = aTargets[iT].mask;
        const float flEngage    = aTargets[iT].engage;
        const int   iTargetIdx  = aTargets[iT].idx;
        const Vec3  vTarget     = aTargets[iT].pos;
        const Vec3  vDelta      = vTarget - vOrigin;
        const float flFlat      = Vec3(vDelta.x, vDelta.y, 0.f).Length2D();

        if (!bPredicted || s_asJumpType != JT_NONE || s_asPendingType != JT_NONE || s_asRideHold)
            break;

        // ---- what is worth paying for, stated from the sim's OWN ceiling ------------------
        // No jump gains 100u even with the catch duck's +20 on top of a held-duck long, and the
        // hull can never travel further than its current 2D speed carries it in k_psMaxHopTicks
        // (the wish is zeroed except for the seam press, which only pushes INTO the wall, and
        // ground friction only subtracts). This is not a screen in front of the authority, it
        // is the authority's own ceiling stated before paying for it
        const float flSimTravelCap = Vec3(s_prePredVel.x, s_prePredVel.y, 0.f).Length2D()
                                   * TickInterval() * float(k_psMaxHopTicks) + flHullReachXY;
        const bool bWorthSimming = vDelta.z < 100.f && flFlat <= flSimTravelCap;
        if (bWorthSimming)
            nPlanned++;

        struct Cand_t { int type; bool allowed; PsHop_t res; };
        Cand_t aCands[7] =
        {
            { JT_MINI,     false, {} },
            { JT_REGULAR,  false, {} },
            { JT_MINIDUCK, false, {} },
            { JT_CROUCH,   false, {} },
            { JT_LONG,     false, {} },
            { JT_LONGDUCK, false, {} },
            { JT_AIRDASH,  false, {} },
        };
        for (int k = 0; k < 7; k++)
            aCands[k].allowed = (iTargetMask & (1 << k)) != 0;
        // the dash is an AIR press and costs a jump charge -- offer it only where it exists
        if (aCands[6].allowed && !(!bOnGround && CanAirDashNow(pLocal)))
            aCands[6].allowed = false;

        // sim order: closest remembered apex to the height actually needed, first
        const float flNeedNow = vDelta.z;
        int aOrder[7] = { 0, 1, 2, 3, 4, 5, 6 };
        for (int a = 0; a < 7; a++)
            for (int b = a + 1; b < 7; b++)
            {
                const float flA = fabsf(s_apexSeen[aCands[aOrder[a]].type] - flNeedNow);
                const float flB = fabsf(s_apexSeen[aCands[aOrder[b]].type] - flNeedNow);
                if (flB < flA) { const int t = aOrder[a]; aOrder[a] = aOrder[b]; aOrder[b] = t; }
            }

        int iPick = JT_NONE;
        const PsHop_t* pPick = nullptr;
        if (bWorthSimming)
        {
            for (int o = 0; o < 7; o++)
            {
                const int k = aOrder[o];
                if (!aCands[k].allowed)
                    continue;
                // do not START an arc there is no clock to FINISH: a cut arc is refused
                // outright (PsHop_t::truncated), so half an arc is worse than none
                if (SimsLeft() < 24 || TbBudgetUsedUs() + 600 > usTargetEnd)
                    break;
                aCands[k].res = PsSimHop(pLocal, pCmd, aCands[k].type, vTarget, flHullReachXY);
                nSimmedCands++;
                if (aCands[k].res.launchDelay >= 0 && aCands[k].res.apexGain > 0.f)
                    s_apexSeen[aCands[k].type] = aCands[k].res.apexGain;

                // ---- THE ONLY ADMISSION TEST IN THIS FILE ----------------------------------
                // The engine flew the exact commands the live tail is about to send and the hull
                // ended up pinned at this seam for k_psConfirm ticks. There is nothing to rank,
                // nothing to cap and nothing to extrapolate: take it
                const PsHop_t& c = aCands[k].res;
                // the dash is an AIR press with no ground contact to arm against, so a dash plan
                // that cannot fire on this very cmd is not a plan -- re-planned next cmd instead
                const bool bDashLate = aCands[k].type == JT_AIRDASH && c.launchDelay > 0;
                if (c.caught && !c.truncated && !bDashLate
                    && c.launchDelay >= 0 && c.apexGain >= k_psMinApex)
                {
                    iPick = aCands[k].type;
                    pPick = &aCands[k].res;
                    nCaught++;
                    break;
                }
            }
        }

        // ---- one diag line per decision ---------------------------------------------------
        {
            char szCands[720]; int nC = 0; szCands[0] = 0;
            static const char* kShort[] = { "mini", "reg", "mnid", "crch", "long", "lngd", "dash" };
            for (int k = 0; k < 7 && nC < (int)sizeof(szCands) - 110; k++)
            {
                const PsHop_t& c = aCands[k].res;
                if (!aCands[k].allowed)
                {
                    nC += snprintf(szCands + nC, sizeof(szCands) - nC, "%s%s:off",
                                   nC ? " " : "", kShort[k]);
                    continue;
                }
                // d = launch delay, r = presses the engine REFUSED ("d-1 r2" = this stance
                // cannot jump at all). a carries 'T' when the clock cut the arc and '!' when
                // it ended on the floor. gp = closest the descent came to the seam's height.
                // ---- pin / ok ARE THE WHOLE REPORT NOW -------------------------------------
                // pin = pin ticks the sim saw at all, ok = a confirmed sustained catch. Every
                // log before read c0 z0 forever because the sim flew no seam press; if
                // `pin0` is still universal here then the sim STILL cannot reproduce the catch
                // and the next build is the probe, not the picker. `pin>0 ok0` means the seam
                // is being touched and not held -- that is a k_psConfirm / choreography
                // question. `ok1` lines are fires, and they should ride
                nC += snprintf(szCands + nC, sizeof(szCands) - nC,
                               "%s%s(d%d r%d a%.1f%s gp%.2f st%.2f pin%d ok%d ct%d)",
                               nC ? " " : "", kShort[k], c.launchDelay, c.refused, c.apexGain,
                               c.truncated ? "T" : (c.landed ? "!" : (c.sawApex ? "" : "^")),
                               c.gap > 1e8f ? -1.f : c.gap,
                               fabsf(c.gapVz) * TickInterval(),
                               c.pinTicks, (int)c.caught, c.catchSince);
            }

            char szOff[112]; int nO = 0; szOff[0] = 0;
            {
                static const char* kOffNames[7] =
                    { "mini", "regular", "mini+duck", "crouch", "long", "long+duck", "dash" };
                for (int k = 0; k < 7 && nO < (int)sizeof(szOff) - 16; k++)
                    if (!(iTargetMask & (1 << k)))
                        nO += snprintf(szOff + nO, sizeof(szOff) - nO, "%s%s",
                                       nO ? "," : " OFF=[", kOffNames[k]);
                if (nO) snprintf(szOff + nO, sizeof(szOff) - nO, "]");
            }

            if (iPick != JT_NONE && pPick)
                DebugLog("PS: pick=%s pt=%d/%d i%d dist=%.0f used=%d need=%.2f apex=%.1f "
                         "catch=t%d z%+.3f flat=%.1f zf=%.3f gnd=%d rad=%.0f mask=%d%s "
                         "worth=%d [%s] sims=%d",
                         PsTypeName(iPick), iT, nTargets, iTargetIdx, aTargets[iT].dist,
                         s_psUsedIdx, vTarget.z - pPick->launchZ, pPick->apexGain,
                         pPick->catchSince, pPick->catchZ - vTarget.z, pPick->catchFlat,
                         vTarget.z - truncf(vTarget.z), (int)bOnGround, flEngage,
                         iTargetMask, szOff, (int)bWorthSimming, szCands,
                         k_cmdSimCap - SimsLeft());
            else
            {
                // ONE DiagOk PER TARGET SLOT : a rate limiter shared by the things
                // being compared does not sample them, it hides all but the first
                static int s_psDiagTick[k_psMaxTargets] = {};
                if (DiagOk(s_psDiagTick[iT], false))
                    DebugLog("PS: none pt=%d/%d i%d dist=%.0f used=%d need=%.2f flat=%.1f "
                             "zf=%.3f gnd=%d rad=%.0f mask=%d%s worth=%d [%s] sims=%d",
                             iT, nTargets, iTargetIdx, aTargets[iT].dist, s_psUsedIdx,
                             vDelta.z, flFlat, vTarget.z - truncf(vTarget.z),
                             (int)bOnGround, flEngage, iTargetMask, szOff,
                             (int)bWorthSimming, szCands, k_cmdSimCap - SimsLeft());
            }
        }

        if (iPick == JT_NONE || !pPick)
            continue;       // this target had nothing; the next one may

        // ---- COMMIT ----------------------------------------------------------------------
        { std::lock_guard<std::mutex> l(g_psLock); s_psAssistActive = iTargetIdx; }
        s_asPromApex  = pPick->apexGain;
        s_asPromNeed  = vTarget.z - pPick->launchZ;
        s_asPromCatch = pPick->catchSince;

        const int iDelay = pPick->launchDelay;
        const bool bDash = iPick == JT_AIRDASH;
        const bool bFireNow = iDelay <= 0 && (bOnGround || bDash);

        // the live commit needs the same clear latch the sim flew. With m_nOldButtons
        // carrying IN_JUMP the engine throws the press away (ground jump AND air dash alike), and
        // our own bit keeps the latch set, so re-pressing every tick latches the assist OFF for
        // as long as the player holds space (a bhop stamps IN_JUMP on every grounded cmd). Spend
        // one cmd without the bit and re-plan next tick against a clear latch. s_prevCmdJump is
        // the previous cmd's committed bit -- ours or the player's, either one sets the latch
        if (bFireNow && s_prevCmdJump)
        {
            pCmd->buttons &= ~IN_JUMP;
            break;          // this cmd is spent clearing the latch; no second target may press
        }
        if (bFireNow)
        {
            // a duck latched by the PREVIOUS hop is not part of this arc's schedule, and the sim
            // flew this arc from `buttons & ~IN_DUCK`. Hand the bit to the new plan
            s_psAssistDuckHeld = false;
            s_asJumpType = iPick;
            s_asHopTick = 0;
            s_asHopTarget = vTarget;
            PsOutcomeBegin(vOrigin.z, vTarget, iPick, s_asPromApex, s_asPromNeed, s_asPromCatch);
            if (g_psAssistJumpBox)
                PsStampJumpBox(iPick, vTarget.z);
        }
        else
        {
            // the launch sits a few ticks out (or we are airborne): arm it and fire on the REAL
            // ground contact. +8 ticks of slack, and it is a countdown so a choked cmd stream
            // cannot leave it armed forever
            s_asPendingType = iPick;
            s_asPendingTicks = iDelay + 8;
            s_asPendingTarget = vTarget;
            s_asPendingIdx = iTargetIdx;
            s_asPendingLaunchZ = pPick->launchZ;
        }
        break;              // this target owns the cmd -- never let a second one write buttons
    }

    if (bPredicted && nTargets > 0)
    {
        static int s_psSweepDiagTick = 0;
        if (DiagOk(s_psSweepDiagTick, false))
            DebugLog("PS: sweep n=%d planned=%d cands=%d caught=%d sims=%d us=%lld/%lld",
                     nTargets, nPlanned, nSimmedCands, nCaught, k_cmdSimCap - SimsLeft(),
                     TbBudgetUsedUs(), usClockTotal);
    }

    FlyLiveTail();
}

// Jump-box popup only -- no vector copies. The overlay wants this every frame even
// when point rendering is off, and copying both point lists for it was pure waste.
void PS_GetBox(PsDrawData& out)
{
    std::lock_guard<std::mutex> l(g_psLock);
    memcpy(out.boxText, s_asBoxText, sizeof(out.boxText));
    out.boxMs = s_asBoxMs;
}

// render-thread snapshot (movement.h PsDrawData)
void PS_GetDrawData(PsDrawData& out)
{
    std::lock_guard<std::mutex> l(g_psLock);
    out.finderPts = s_psFinderPts;
    out.tbPts = s_psTbPts;
    out.mapLines = s_psMapLines;
    out.mapTbPts = s_psMapTbPts;
    out.assistPts.resize(s_psAssistPts.size());
    for (size_t i = 0; i < s_psAssistPts.size(); i++)
        out.assistPts[i] = s_psAssistPts[i].pos;
    out.assistActive = s_psAssistActive;
    out.lineActive = s_psFinderFirst && s_psFinderHeld;
    out.lineA = s_psFinderStart;
    out.lineB = s_psFinderEnd;
    out.localOrigin = s_psLocalOrigin;
    memcpy(out.boxText, s_asBoxText, sizeof(out.boxText));
    out.boxMs = s_asBoxMs;
    out.aliveMs = s_psAliveMs;
}

// ---- assist point access for the render-thread ENTER window (movement.h) ----
unsigned PS_PointsGen()
{
    std::lock_guard<std::mutex> l(g_psLock);
    return s_psGen;
}

bool PS_PointGet(int i, Vec3& pos, int& mask, float& radius)
{
    std::lock_guard<std::mutex> l(g_psLock);
    if (i < 0 || i >= (int)s_psAssistPts.size())
        return false;
    pos = s_psAssistPts[i].pos;
    mask = s_psAssistPts[i].typeMask;
    radius = s_psAssistPts[i].radius;
    return true;
}

// 0 = follow the global radius slider. Persists per edit, same as the mask
void PS_PointSetRadius(int i, float radius)
{
    std::lock_guard<std::mutex> l(g_psLock);
    if (i < 0 || i >= (int)s_psAssistPts.size())
        return;
    s_psAssistPts[i].radius = radius > 0.f ? std::min(radius, 1024.f) : 0.f;
    PsWritePoints();
}

void PS_PointSetMask(int i, int mask)
{
    std::lock_guard<std::mutex> l(g_psLock);
    if (i < 0 || i >= (int)s_psAssistPts.size())
        return;
    s_psAssistPts[i].typeMask = mask & 127;
    PsWritePoints(); // persist per toggle -- close/crash never loses the edit
}

void PS_PointDelete(int i)
{
    std::lock_guard<std::mutex> l(g_psLock);
    if (i < 0 || i >= (int)s_psAssistPts.size())
        return;
    s_psAssistPts.erase(s_psAssistPts.begin() + i);
    s_psGen++;
    PsWritePoints();
}
