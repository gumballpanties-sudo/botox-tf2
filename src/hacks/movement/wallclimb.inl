// wallclimb.inl -- the pre-csgo-port wall climb (sweep remake on the creator base), brought back and tightened.
// Textually included by movement.cpp right after airstuck.inl (same TU: Pred_Simulate / RestoreToFrame / SimsLeft /
// MV_DetectWallLines / MV_SupportDistance / TB_SteeringAwayFromWall / AsClock / s_prePred* / s_raw* are statics above).
//
// A wall climb is ground -> hop -> sweep -> ground, repeated. Airborne beside a displacement wall this sweeps silent
// into-wall moves for ONE simulated tick and commits the first that ends FL_ONGROUND flush on that face; the win arms a
// hop that the real grounded tick stamps (CheckJumpButton reads the ground state a cmd STARTS on, so the press cannot
// go on the cmd that grounds). Three proofs stand between the scan and any write, all engine facts:
//   DISP    the face is a displacement (trace.dispFlags): a brush face is one plane and cannot hand a hull a standable facet
//   NEAR    the hull face is within k_engage_support of it (one tick of fwd=450 closes ~0.45u, 16u out no variant reaches)
//   GROUND  a swept variant ends the tick FL_ONGROUND and <= k_land_support from THAT face (grounded 8u out = a landing)
// No win = no write, the player keeps his own cmd. Hands off the keys = hands off the cmd.
//
// Kept from the csgo port, the parts that were real upgrades:
//   wall-clock budget (AsClock k_time_share) on top of SimsLeft()      crouch catches option (g_wcDuck = try the other stance)
//   prevent slow (g_wcPreventSlow: no win that halves xy > 200)        visualize (WC_GetDrawData -> render.cpp RenderWallClimbOverlay)
//   TF2 rise threshold 250 (CategorizePosition), not CS:GO's 140      TB's non-catch creep no longer starves wc on a shared key:
//   the sweep starts from the keys TB was handed (s_wcPreTb*), TB's cmd stays unless wc wins
// Dropped: parks / holds / exit pin / spot scan / jump fix / snapshot slots -- the csgo pin model, TF2 grounds instead.
#include <mutex>
#include "wallclimb_pure.h"

static wc::Hop s_wcHop;
static int     s_wcWallTick  = -100000; /* last tick a disp face was engaged (the unlatch window) */
static float   s_wcScanStart = 0.f;
static float   s_wcGroundZ   = -1e9f;   /* z of the last ground tick, viz "+h" only */

/* per-second counters, debug log */
static int s_wcN[9]{}; /* ticks scan disp near sweep win hop strip wall */
static Vec3 s_wcBrushN{};  /* last brush face the disp gate skipped (log bn=) */
static int s_wcSims = 0; static long long s_wcUs = 0ll, s_wcUsWorst = 0ll;
static unsigned long long s_wcLogNext = 0ull;

/* last wall tick, written in create move, copied out for the render thread */
static std::mutex s_wcVizLock;
static WcDrawData s_wcViz, s_wcVizOut;

void WC_GetDrawData(WcDrawData& out)
{
    std::lock_guard<std::mutex> l(s_wcVizLock);
    out = s_wcVizOut;
}

static void WallClimb(void* pLocal, UserCmd_t* pCmd)
{
    using namespace wc;
    s_wallClimbHit = s_wallClimbActive = s_wcLanded = false;
    s_wcViz.valid = false;
    struct Publish { ~Publish() { std::lock_guard<std::mutex> l(s_wcVizLock); s_wcVizOut = s_wcViz; } } publish_viz;

    if (const unsigned long long now = GetTickCount64(); g_debugLogEnable && now >= s_wcLogNext) {
        s_wcLogNext = now + 1000ull;
        if (s_wcN[0])
            DebugLog("WC: ticks=%d scan=%d wall=%d disp=%d near=%d sweep=%d win=%d hop=%d strip=%d sims=%d us=%lld worst=%lld bn=%.2f,%.2f,%.2f",
                     s_wcN[0], s_wcN[1], s_wcN[8], s_wcN[2], s_wcN[3], s_wcN[4], s_wcN[5], s_wcN[6], s_wcN[7], s_wcSims, s_wcUs, s_wcUsWorst,
                     s_wcBrushN.x, s_wcBrushN.y, s_wcBrushN.z);
        for (int& c : s_wcN) c = 0;
        s_wcSims = 0; s_wcUs = s_wcUsWorst = 0ll;
    }

    const bool key = On(g_wallClimb, g_wallClimbKey, g_wallClimbMode);
    s_wcSyncWant   = key && EntAlive(pLocal);
    if (!pCmd || !key || !EntAlive(pLocal) || EntMoveType(pLocal) != MOVETYPE_WALK) {
        s_wcHop.Reset();
        s_wcScanStart = 0.f;
        return;
    }

    const int iTickNow = g_globalVars->tickcount;
    if (s_wcWallTick > iTickNow) s_wcWallTick = -100000;   // map change rewound the clock
    // the entity holds this cmd's END here; START is the pre-pred capture
    const bool bGroundPre  = (s_prePredFlags & FL_ONGROUND) != 0;
    const bool bGroundPost = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
    if (bGroundPre) s_wcGroundZ = s_prePredOrigin.z;

    // armed-hop clock, before every yield: a hop armed in the air must survive the ticks TB/AS own
    const bool armed_hop = s_wcHop.Clock(bGroundPre);

    // UNLATCH. FinishMove copies this cmd's buttons into m_nOldButtons and a grounded jump is refused while IN_JUMP is set
    // there -- only a jumpless cmd clears it. Held space would block the very seam jump the climb exists to make
    const bool engaged = s_wcHop.pending > 0 || (iTickNow - s_wcWallTick) <= k_engage_ticks;
    const bool other_wants_jump = s_jumpbugHit || On(g_jumpbug, g_jumpbugKey, g_jumpbugMode) || s_psAssistHopActive;
    if (Unlatch(bGroundPre, bGroundPost, s_rawJumpHeld, engaged, other_wants_jump)) {
        pCmd->buttons &= ~IN_JUMP;
        ++s_wcN[7];
    }

    // yields. TB / HS / HB catches, AS pins (AS outranks WC, coldtires order), a running tb ride, EB's cmd, every PS owner:
    // the assist arc flies beside the very wall this sweeps and a win would overwrite its catch duck / steer
    if (s_textureBugHit || s_headSurfHit || s_headBounceHit || s_airStuckHit || s_asHolding || s_ebDetected
        || (On(g_textureBug, g_textureBugKey, g_textureBugMode) && s_tbRideHold > 0)
        || s_psAssistHopActive || s_shouldPixelSurf || s_psRidingNow || s_psAssistDuckHeld)
        return;

    // ---- THE LAUNCH ---- wc never jumps on its own: s_rawJumpHeld is the real space bar before AutoJump rewrites the cmd
    if (bGroundPre) {
        switch (s_wcHop.Launch(armed_hop, s_rawJumpHeld, s_prevCmdJump)) {
        case hop_release: pCmd->buttons &= ~IN_JUMP; return;
        case hop_press:
            pCmd->buttons |= IN_JUMP;
            pCmd->buttons &= ~IN_DUCK;   // FL_DUCKING is a flat refusal in CheckJumpButton
            s_wallClimbHit = true;
            ++s_wcN[6];
            return;
        default: return;
        }
    }
    s_wcHop.jumpedLast = false;
    ++s_wcN[0];

    // airborne at the cmd's start, grounded at its end = the player's own move already lands them
    if (bGroundPost) return;

    // provable skip: CategorizePosition nulls the ground entity when vz > 250 at the check (half a gravity tick in)
    const Vec3 velocity = EntV3(pLocal, g_offVelocity);
    if (velocity.z + TargetPredictZVel() > k_rise_vz) return;

    // the keys the sweep starts from: TB's creep / probe (no catch) is a band setup on a shared key -- it fired every air wall
    // tick and wc never got the cmd. Sweep from what TB was handed; TB's cmd goes out unless a variant wins
    const bool tb_creep = s_tbOwnsCmd && !s_textureBugHit;
    UserCmd_t base = *pCmd;
    if (tb_creep) {
        base.forwardmove = s_wcPreTbFwd;
        base.sidemove    = s_wcPreTbSide;
        base.buttons     = s_wcPreTbButtons;
    }
    // A CLIMB IS SOMETHING THE PLAYER DOES. No keys = no write, and no 120-230 trace scan either
    if (fabsf(base.forwardmove) < k_min_input && fabsf(base.sidemove) < k_min_input) return;

    const Vec3 start_pos = EntV3(pLocal, g_offOrigin);
    const Vec3 mins = EntV3(pLocal, g_offMins), maxs = EntV3(pLocal, g_offMaxs);

    GameTrace_t primary{};
    float found_angle = 0.f;
    ++s_wcN[1];
    // GATE 1: displacement or nothing. The scan hands back the FIRST face in angle order, so a brush face there (the
    // trim under a disp cliff, the far side of a corner) used to end the tick with a disp face sitting in the next
    // direction: 11-04 log read disp=0 on every second. Step past brush faces, one full turn at most
    // (wallclimb_pure.h NextScan). s_wcWallTick is not stamped on a brush, so the unlatch stays out of it too
    bool any_wall = false, have_disp = false;
    for (float a = s_wcScanStart; ScanLeft(s_wcScanStart, a); a = NextScan(found_angle)) {
        if (!MV_DetectWallLines(pLocal, start_pos, mins, maxs, a, primary, found_angle, k_scan_nz, k_scan_reach))
            break;
        any_wall = true;
        if (primary.dispFlags & DISPSURF_SURFACE) { have_disp = true; break; }
        s_wcBrushN = primary.plane.normal;
    }
    if (any_wall) ++s_wcN[8];
    if (!have_disp) {
        s_wcScanStart = 0.f;   // next scan fresh, do not pin on a brush
        return;
    }
    ++s_wcN[2];
    s_wcScanStart = WrapScan(found_angle);
    // GATE 2: near it, live, before any sim
    const float live_support = MV_SupportDistance(pLocal, primary, &start_pos);
    if (live_support > k_engage_support) return;
    ++s_wcN[3];
    s_wcWallTick      = iTickNow;
    s_wallClimbActive = true;   // an air wall tick: pixel surf yields

    // seamless leave: steering off the wall = the player keeps the cmd
    if (TB_SteeringAwayFromWall(&base, primary.plane.normal, base.forwardmove, base.sidemove)) return;

    const Vec3 approach_normal = primary.plane.normal * -1.f;
    Vec3 angle_wall = VectorAngles(approach_normal);
    angle_wall.y    = NormalizeAngle(angle_wall.y);
    const Vec3  original_view = pCmd->viewangles;
    const float velocity_yaw  = RAD2DEGF(atan2f(velocity.y, velocity.x));
    const float sign          = SweepSign(NormalizeAngle(velocity_yaw - angle_wall.y));

    AsClock budget;
    budget.Start(k_time_share, TickInterval());
    int sims = 0;
    const int  nBase   = PredCommandsPredicted();
    const auto restore = [&]() { RestoreToFrame(nBase - 1); };
    const auto can_sim = [&]() { return SimsLeft() > 0 && !(sims > 0 && budget.Expired()); };
    const auto sim     = [&](UserCmd_t c) { restore(); Pred_Simulate(pLocal, &c); ++sims; };
    const auto grounded = [&]() { return (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0; };
    struct Done { const decltype(restore)& r; ~Done() { r(); } } done_restore{ restore };

    // ONE SIM decides whether the rest may run: the player's own move (TB's, if it is creeping) is the baseline. If it already
    // grounds them the sweep has nothing to buy -- a hop armed off it would fire on whatever they happened to land on
    if (!can_sim()) return;
    sim(*pCmd);
    if (grounded()) return;
    ++s_wcN[4];

    const float start_xy = velocity.Length2D();
    const bool  duck_now = (base.buttons & IN_DUCK) != 0;
    UserCmd_t best{};
    bool found = false;
    float land_support = 0.f;
    // stance 0 is the player's own, so a catch the plain sweep makes is the first one tried; the other stance only with
    // crouch catches on (no crouch pops in demos otherwise)
    for (int stance = 0; stance < 2 && !found; ++stance) {
        if (stance == 1 && !g_wcDuck) break;
        for (const float forward : k_forward) {
            if (found) break;
            for (float offset = 0.f; offset < k_offset_max && !found; offset += k_offset_step) {
                if (!can_sim()) break;
                UserCmd_t fake  = base;
                fake.viewangles = original_view;
                if (stance == 1) fake.buttons = duck_now ? (fake.buttons & ~IN_DUCK) : (fake.buttons | IN_DUCK);
                fake.forwardmove = forward;
                fake.sidemove    = 0.f;
                Vec3 wish = original_view;
                wish.y    = angle_wall.y + offset * sign;
                CorrectMovement(&fake, wish, original_view);   // silent: the view is not touched
                sim(fake);
                if (!grounded()) continue;
                // GATE 3: grounded ON THAT FACE. On a disp map FL_ONGROUND is very often the floor you were landing on anyway
                const float support = MV_SupportDistance(pLocal, primary);
                if (support > k_land_support) continue;
                if (Slowed(start_xy, EntV3(pLocal, g_offVelocity).Length2D()) && g_wcPreventSlow) continue;
                found = true;
                best  = fake;
                land_support = support;
            }
        }
    }

    if (found) {
        pCmd->forwardmove = best.forwardmove;
        pCmd->sidemove    = best.sidemove;
        pCmd->buttons     = best.buttons;
        s_tbOwnsCmd       = false;   // wc's win is the cmd now
        s_wcHop.Arm();               // the ground tick this bought, stamped when it really arrives
        s_wallClimbHit = s_wcLanded = true;
        ++s_wcN[5];
        DebugLog("WC LAND: fwd=%.0f side=%.0f duck=%d live=%.2f land=%.2f n=%.3f,%.3f,%.3f xy=%.0f vz=%.1f sims=%d%s",
                 best.forwardmove, best.sidemove, (best.buttons & IN_DUCK) != 0, live_support, land_support,
                 primary.plane.normal.x, primary.plane.normal.y, primary.plane.normal.z, start_xy, velocity.z, sims,
                 tb_creep ? " tbdrop" : "");
    }

    // visualize: the face, then where the outgoing cmd takes you (green end = a land). Extra sims, off the clock
    if (g_wcViz) {
        WcDrawData& v = s_wcViz;
        v         = {};
        v.valid   = true;
        v.ms      = GetTickCount64();
        v.ground  = false;
        v.branch  = found ? "land" : "scan";
        v.n       = primary.plane.normal;
        v.sims    = sims;
        v.half    = maxs.x;
        v.height  = maxs.z - mins.z;
        v.groundZ = s_wcGroundZ;
        v.origin  = start_pos;
        const Vec3 mid = primary.endpos + Vec3(0.f, 0.f, (mins.z + maxs.z) * 0.5f);
        const float off = mid.x * v.n.x + mid.y * v.n.y + mid.z * v.n.z - primary.plane.dist;
        v.wall = fabsf(off) <= 32.f ? mid - v.n * off : mid - v.n * (maxs.x * (fabsf(v.n.x) + fabsf(v.n.y)));
        restore();
        v.path[v.pathN++] = start_pos;
        for (int k = 0; k < k_viz_ticks; ++k) {
            UserCmd_t c = *pCmd;
            Pred_Simulate(pLocal, &c);
            v.path[v.pathN++] = EntV3(pLocal, g_offOrigin);
            if (grounded()) {
                v.lands = found;
                v.land  = v.path[v.pathN - 1];
                break;
            }
        }
    }

    const long long us = budget.UsedUs();
    s_wcSims += sims;
    s_wcUs += us;
    s_wcUsWorst = (std::max)(s_wcUsWorst, us);
}
