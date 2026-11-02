// wallclimb.inl -- botox csgo-sdk hacks/movement/wall_climb.cpp (lab P71, 11-02), 1:1. Textually included by
// movement.cpp right after airstuck.inl (same TU: Pred_Simulate / RestoreToFrame / TraceHull / AsClock /
// AsSetOrigin / AsSurfaceFriction / s_asHolding / s_asStamp* are statics of the files above it).
//
// botox -> tf2 shims, nothing else differs:
//   g_prediction.begin+end / restore_entity_to_predicted_frame -> Pred_Simulate / RestoreToFrame (bounds speed is Pred_Simulate's)
//   n_tick::c_sim_budget                     -> AsClock (0.6 of a tick, capped at what is left of the cmd clock)
//   snapshot slots 62 / 63                   -> k_wcSpotSlot / k_wcSpotRoll, after EB's bend slots. edge skip's Pred_SnapReset
//                                               can wipe them: a failed load drops the spot job, it restarts next ground tick
//   g_movement.m_user_*_move_raw             -> s_rawFwd / s_rawSide (captured with s_rawJumpHeld, before any feature)
//   g_air_stuck_holding / _owns_cmd          -> s_asHolding / s_airStuckHit
//   HITGODA (ps assist plan live)            -> s_psAssistHopActive || s_psAssistDuckHeld. HITGODA2/3: run after wc here / not ported
//   g_texturebug.m_hit / m_hs_hit            -> s_textureBugHit / s_headSurfHit; g_edgebug.m_found -> s_ebDetected
//   + TF2 runs TB / HS / HB BEFORE wc (botox: after, and tb steps aside for caught()). So wc also yields to s_headBounceHit,
//     s_tbOwnsCmd and a running tb ride (s_tbRideHold): their cmd already went into this one
//   fireman yield                            -> none, TF2 has no ladders
//   0.34 ducked wish crop                    -> none: HandleDuckingSpeedCrop is ground only (gamemovement.cpp:4308), every
//                                               wish here is simmed in the air
//   k_rise_vz 150 (CS:GO grounds vz <= 140)  -> 260 (CTFGameMovement::CategorizePosition: vz > 250, tf_gamemovement.cpp:2358)
//   jump impulse * stamina                   -> 289 (tf_gamemovement.cpp:1315), TF2 has no stamina
//   seam trace hull -16..16 x 0..72          -> the player's own mins / maxs (TF2 hull 48 x 82)
//   caught() / active() / m_hit              -> s_wallClimbHit / s_wallClimbActive / s_wcLanded, all for THIS cmd
//   sync_arm( k_sync_wc )                    -> s_wcSyncWant, read by AS_Sync (one cl_pred_optimize owner for both)
//   render( ) paint draw data                -> WC_GetDrawData, drawn by render.cpp RenderWallClimbOverlay
//   ponytail: not ported = show wall slope (own key, movement_add_window) and the debug-only check_stomp / trace_stomp /
//   DIV lines; port them when a log needs to tell a later feature's rewrite from a sim miss
#include <mutex>
#include "wallclimb_pure.h"

namespace wc { struct SimOut { Vec3 origin{}, vel{}; int flags = 0; }; }

static constexpr int k_wcSpotSlot = k_ebBendSnap + 16;   // botox 62: after every EB slot
static constexpr int k_wcSpotRoll = k_wcSpotSlot + 1;    // botox 63

static int   s_wcCount[wc::c_max]{};
static long long s_wcUs = 0ll, s_wcUsWorst = 0ll;
static unsigned long long s_wcLogNext = 0ull;
static int   s_wcDiagTick = -1000;
static float s_wcGroundZ = -1e9f;
static bool  s_wcHolding = false;
static int   s_wcHoldButtons = 0; /* stance the hold plan was proven in */
static int   s_wcFreeCmd = -1;    /* cmd wc sent off a sim that ended unpinned */
static bool  s_wcParkOff = false;

/* ground: one wc hop replayed from each spot along the wall within reach, sliced over ticks, best one walked to */
static struct WcSpotJob {
    bool valid = false, rolling = false, done = false;
    Vec3 base{}, n{};
    float reach = 0.f;
    int count = 0, idx = 0, ticks = 0, sims = 0, slices = 0;
    int score[33]{};
} s_wcSpot;

/* last wall tick, written in create move, copied out for the render thread */
static std::mutex s_wcVizLock;
static WcDrawData s_wcViz, s_wcVizOut;

void WC_GetDrawData(WcDrawData& out)
{
    std::lock_guard<std::mutex> l(s_wcVizLock);
    out = s_wcVizOut;
}

static void WallClimb(void* pLocal, UserCmd_t* cmd)
{
    using namespace wc;
    s_wallClimbHit = s_wallClimbActive = s_wcLanded = false;
    s_wcViz.valid = false;
    struct Publish { ~Publish() { std::lock_guard<std::mutex> l(s_wcVizLock); s_wcVizOut = s_wcViz; } } publish_viz;

    if (const unsigned long long now = GetTickCount64(); now >= s_wcLogNext) {
        s_wcLogNext = now + 1000ull;
        int* const n = s_wcCount;
        if (n[c_ticks] || n[c_yield] || n[c_ramp] || n[c_skip])
            DebugLog("WC: land=%d flip=%d pinc=%d hold=%d park=%d keys=%d along=%d own=%d leave=%d aa=%d exit=%d/%d far=%d rise=%d scut=%d cut=%d yield=%d ramp=%d skip=%d jump back=%d strip=%d as=%d miss=%d np=%d pk=%d away=%d robust=%d spot=%d/%d hland=%d low=%d lhang=%d seam=%d/%d pflip=%d ticks=%d sims=%d us=%lld worst=%lld",
                     n[c_land], n[c_flip], n[c_pin_catch], n[c_hold], n[c_park], n[c_keys], n[c_along], n[c_own], n[c_leave], n[c_aa_leave],
                     n[c_exit], n[c_exit_none], n[c_far], n[c_rise], n[c_scan_cut], n[c_cut], n[c_yield], n[c_ramp], n[c_skip], n[c_jump_back],
                     n[c_jump_strip], n[c_as_take], n[c_pin_miss], n[c_no_park], n[c_park_keep], n[c_away], n[c_robust], n[c_spot], n[c_spot_sims],
                     n[c_hold_land], n[c_low], n[c_leave_hang], n[c_seam], n[c_seam_park], n[c_pin_flip], n[c_ticks], n[c_sims], s_wcUs, s_wcUsWorst);
        for (int& c : s_wcCount)
            c = 0;
        s_wcUs = s_wcUsWorst = 0ll;
    }

    // botox reads START off the live entity (it runs pre-prediction); here the entity already holds this cmd's END, so
    // the cheap reads come from the pre-pred capture and the sims below from the restore base
    const bool key = On(g_wallClimb, g_wallClimbKey, g_wallClimbMode);
    s_wcSyncWant   = key && EntAlive(pLocal);
    if (!cmd || !EntAlive(pLocal) || !g_wallClimb || EntMoveType(pLocal) != MOVETYPE_WALK) {
        s_wcHolding = false;
        return;
    }
    const bool on_ground_pre = (s_prePredFlags & FL_ONGROUND) != 0;
    s_wcParkOff = s_wcParkOff && !on_ground_pre;
    if (on_ground_pre)
        s_wcGroundZ = s_prePredOrigin.z;
    if (!key) {
        s_wcHolding = false;
        return;
    }

    const int nBase = PredCommandsPredicted();
    RestoreToFrame(nBase - 1);
    const Vec3 origin    = EntV3(pLocal, g_offOrigin);
    const Vec3 start_vel = EntV3(pLocal, g_offVelocity);
    const bool on_ground = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
    const float friction = AsSurfaceFriction(pLocal);
    const Vec3 mins = EntV3(pLocal, g_offMins), maxs = EntV3(pLocal, g_offMaxs);
    if (on_ground)
        s_wcGroundZ = origin.z;

    const float pin         = TargetPredictZVel();
    const auto pinned       = [pin](const Vec3& v) { return v.x == 0.f && v.y == 0.f && v.z == pin; };
    const bool start_pinned = !on_ground && pinned(start_vel);
    // server pinned what wc simmed free = flush park is a coin flip here ( 10-07 log: 0 / 41 pins simmed ), park off till ground
    if (start_pinned && s_wcFreeCmd == cmd->command_number - 1) {
        ++s_wcCount[c_pin_miss];
        s_wcParkOff = true;
    }
    s_wcFreeCmd = -1;

    TraceFilterWorldProps filter; filter.Setup(pLocal);
    GameTrace_t wall_tr{};
    const auto find_wall = [&](const float reach, const bool count) {
        for (int i = 0; i < 4; ++i) {
            const float a = DEG2RADF(static_cast<float>(i * 90));
            wall_tr = GameTrace_t{};
            TraceHull(origin, origin + Vec3(cosf(a), sinf(a), 0.f) * reach, mins, maxs, MASK_PLAYERSOLID, &filter, &wall_tr);
            if (!wall_tr.DidHit())
                continue;
            if (wall_tr.startsolid) {   // players never hit: TraceFilterWorldProps skips them
                s_wcCount[c_skip] += count;
                continue;
            }
            if (fabsf(wall_tr.plane.normal.z) <= k_wall_nz)
                return true;
            s_wcCount[c_ramp] += count;
        }
        return false;
    };
    // pinned off the touch range ( an exit or your keys left a lip edge pin 0.18u out ): still exit it
    bool far_pin = false;
    if (!find_wall(k_contact, true)) {
        s_wcHolding = false;
        if (!start_pinned || !find_wall(k_pin_reach, false))
            return;
        far_pin = true;
        ++s_wcCount[c_far];
    }
    // no yield to a pixel surf latch: wc parks flush, so crease pins come often and a latched ride hung you 27-34 ticks
    // ( 10-06 22:20 log ). pixel_surf yields to active( ) instead, it runs after wc
    if (s_asHolding || s_psAssistHopActive || s_psAssistDuckHeld || s_textureBugHit || s_headSurfHit || s_ebDetected
        || s_headBounceHit || s_tbOwnsCmd || (On(g_textureBug, g_textureBugKey, g_textureBugMode) && s_tbRideHold > 0)) {
        s_wcHolding = false;
        ++s_wcCount[c_yield];
        return;
    }
    s_wallClimbActive = !on_ground;
    // air stuck on the same key publishes a wall hug every air tick by a wall, pin or not: only a catch takes the cmd from it
    const bool as_steers   = s_airStuckHit;
    const UserCmd_t as_cmd = *cmd;
    const Vec3 wall_n      = wall_tr.plane.normal;
    const bool disp_wall   = wall_tr.dispFlags != 0;
    const float wall_yaw   = RAD2DEGF(atan2f(-wall_n.y, -wall_n.x));

    const float airaccel = CvarFloat("sv_airaccelerate", 10.f);
    const float ipt      = TickInterval();

    const UserCmd_t keys = *cmd;
    const bool user_duck = (keys.buttons & IN_DUCK) != 0;
    // leave = your raw keys: auto align's push off the wall lost the park / wall ( 10-07 replay 5 -> 9 / 21 )
    UserCmd_t you   = keys;
    you.forwardmove = s_rawFwd;
    you.sidemove    = s_rawSide;
    const bool you_leave = WishLeavesWall(you, wall_n);

    AsClock budget;
    budget.Start(k_time_share, ipt);
    const auto cannot_fit = [&](const long long want_us) { return (AsClock::Now() - budget.start) + want_us * budget.freq / 1000000ll >= budget.span; };
    int sims = 0, commit_end = 0;
    bool cut = false, was_cut = false, free_sent = false;
    const char* branch = "-";
    const auto read_out = [&](SimOut& out) {
        out.origin = EntV3(pLocal, g_offOrigin);
        out.vel    = EntV3(pLocal, g_offVelocity);
        out.flags  = EntI(pLocal, g_offFlags);
    };
    const auto restore = [&]() { RestoreToFrame(nBase - 1); };
    const auto step    = [&](UserCmd_t c) { Pred_Simulate(pLocal, &c); };
    // chain = next tick from wherever the last sim left the entity ( air_stuck pin count does the same )
    const auto chain = [&](const UserCmd_t& c, SimOut& out) {
        if (sims >= commit_end && (cut || (sims > 0 && budget.Expired()))) {
            cut = was_cut = true;
            return false;
        }
        step(c);
        read_out(out);
        ++sims;
        return true;
    };
    const auto sim = [&](const UserCmd_t& c, SimOut& out) {
        if (sims >= commit_end && (cut || (sims > 0 && budget.Expired()))) {
            cut = was_cut = true;
            return false;
        }
        restore();
        return chain(c, out);
    };
    const auto slowed = [&](const Vec3& v) {
        if (!g_wcPreventSlow)
            return false;
        const float old_xy = start_vel.Length2D();
        return old_xy > k_slow_xy && v.Length2D() / old_xy < k_slow_ratio;
    };
    const auto with = [&](const float yaw, const float fwd, const bool duck) {
        UserCmd_t c   = keys;
        c.buttons     = duck ? (c.buttons | IN_DUCK) : (c.buttons & ~IN_DUCK);
        c.forwardmove = fwd;
        c.sidemove    = 0.f;
        MoveFixToYaw(yaw, c);
        return c;
    };
    const auto send = [&](const UserCmd_t& c) {
        cmd->forwardmove = c.forwardmove;
        cmd->sidemove    = c.sidemove;
        cmd->buttons     = c.buttons;
    };
    // entity sits on the state to hold from
    const auto hold_lands = [&](const UserCmd_t& hold) {
        for (int k = 0; k < k_hold_ticks; ++k) {
            SimOut q{};
            if (!chain(hold, q))
                return false;
            if (q.flags & FL_ONGROUND)
                return q.origin.z > s_wcGroundZ + k_land_above;
        }
        return false;
    };
    // first try that stays unpinned for k_exit_look ticks of your keys ( a one tick unpin that re-pins = staircase ), else first unpin
    const auto exit_pin = [&]() {
        const float r  = DEG2RADF(wall_yaw + 90.f);
        const float sg = start_vel.Length2D() >= k_pin_sign_xy && cosf(r) * start_vel.x + sinf(r) * start_vel.y < 0.f ? -1.f : 1.f;
        const UserCmd_t tries[] = { keys, with(wall_yaw + sg * 90.f, 30.f, user_duck), with(wall_yaw - sg * 90.f, 30.f, user_duck),
                                    with(wall_yaw, -5.f, user_duck), with(wall_yaw, -450.f, user_duck) };
        int pick = -1, first = -1;
        for (int i = 0; i < 5 && pick < 0; ++i) {
            SimOut o{};
            if (!sim(tries[i], o))
                break;
            if (pinned(o.vel) || slowed(o.vel))
                continue;
            if (first < 0)
                first = i;
            bool clean = true;
            for (int k = 0; k < k_exit_look && clean && !(o.flags & FL_ONGROUND); ++k) {
                if (!chain(keys, o))
                    break;
                clean = !pinned(o.vel);
            }
            if (clean)
                pick = i;
        }
        if (pick < 0)
            pick = first;
        if (pick < 0) {
            ++s_wcCount[c_exit_none];
            return;
        }
        send(tries[pick]);
        free_sent = true;
        ++s_wcCount[c_exit];
    };

    // spot ( ground, no jump ): the catch depends on where along the wall you jump ( lab: same wall 5% vs 85% a few u apart ). replay one
    // hop per spot ( 0, +-2, +-4 .. reach ) with your keys + a lite wc ( 0 + 5e-4 .. 0.032 F x2 both stances, land / tightest park ), sliced
    // over ground ticks in the two spot snapshot slots, then walk you there along the wall. lab ( fl_lab P64 as predictor ): reach 16 first
    // catches +20%, 32 +34%. your own along wish = no spot ( you walk on purpose )
    const auto spot = [&]() {
        auto& j           = s_wcSpot;
        const float reach = static_cast<float>(std::clamp(g_wcSpotReach, 0, 32));
        const Vec3 t(wall_n.y, -wall_n.x, 0.f);
        float rx, ry;
        WorldWish(keys.viewangles.y, s_rawFwd, s_rawSide, rx, ry);
        const float rl = sqrtf(rx * rx + ry * ry);
        if (reach >= k_spot_step)
            Pred_SnapReserve(k_wcSpotRoll + 1);
        if (reach < k_spot_step || user_duck || Pred_SnapCount() <= k_wcSpotRoll ||
            (rl > 0.f && fabsf(rx * t.x + ry * t.y) > k_leave_cos * rl)) {
            j.valid = false;
            return false;
        }
        const Vec3 off = origin - j.base;
        if (!j.valid || j.reach != reach || j.n.x * wall_n.x + j.n.y * wall_n.y < 0.999f || fabsf(off.z) > 1.f ||
            fabsf(off.x * wall_n.x + off.y * wall_n.y) > 1.f || fabsf(off.x * t.x + off.y * t.y) > reach + k_spot_step) {
            restore();
            j = {};
            if (!Pred_SnapSave(pLocal, k_wcSpotSlot))
                return false;
            j.valid = true, j.base = origin, j.n = wall_n, j.reach = reach;
            j.count = (std::min)(33, 1 + 2 * static_cast<int>(reach / k_spot_step));
        }
        const float F       = k_hull_step / (ipt * airaccel * ipt * friction);
        const float floor_z = j.base.z;
        // a slot another feature reset under us ( edge skip's Pred_SnapReset ) = the job is gone, start over next tick
        const auto load = [&](const int slot) {
            if (Pred_SnapLoad(pLocal, slot))
                return true;
            j.valid = false;
            return false;
        };
        const auto finish = [&](const int score) {
            j.score[j.idx] = score;
            j.rolling      = false;
            if (++j.idx >= j.count) {
                j.done = true;
                int best = 0;
                for (int i = 1; i < j.count; ++i)
                    best = j.score[i] > j.score[best] ? i : best;
                DebugLog("WC SPOT: n=%d best=%+.0f score=%d here=%d sims=%d slices=%d z=%.2f wall=%.2f,%.2f", j.count, AtIdx(best), j.score[best],
                         j.score[0], j.sims, j.slices, floor_z, wall_n.x, wall_n.y);
            }
        };
        ++j.slices;
        while (j.valid && !j.done && !cannot_fit((sims ? budget.UsedUs() / sims : 40ll) * k_spot_tick_sims)) {
            if (!j.rolling) {
                if (!load(k_wcSpotSlot))
                    break;
                const Vec3 o = j.base + t * AtIdx(j.idx);
                GameTrace_t tr{};
                TraceHull(j.base, o, mins, maxs, MASK_PLAYERSOLID, &filter, &tr);
                GameTrace_t gr{};
                TraceHull(o, o - Vec3(0.f, 0.f, 2.f), mins, maxs, MASK_PLAYERSOLID, &filter, &gr);
                if (tr.startsolid || tr.fraction < 1.f || gr.startsolid || gr.fraction >= 1.f || gr.plane.normal.z < 0.7f) {
                    finish(-1);
                    continue;
                }
                AsSetOrigin(pLocal, o);
                UserCmd_t jc = keys;
                jc.buttons |= IN_JUMP;
                step(jc);
                ++sims, ++j.sims;
                if ((EntI(pLocal, g_offFlags) & FL_ONGROUND) || !Pred_SnapSave(pLocal, k_wcSpotRoll)) {
                    finish(0);
                    continue;
                }
                j.rolling = true, j.ticks = 0;
                continue;
            }
            int score      = -2;
            float best_d   = FLT_MAX;
            UserCmd_t pick = keys;
            for (float m = 0.f; m <= 0.032f && score < 0 && j.valid; m = m > 0.f ? m * 2.f : 5e-4f)
                for (int pass = 0; pass < 2 && score < 0 && j.valid; ++pass) {
                    if (pass && !g_wcDuck)
                        continue;
                    const bool duck     = pass ? !user_duck : user_duck;
                    const UserCmd_t c   = with(wall_yaw, m * F, duck);
                    if (!load(k_wcSpotRoll))
                        break;
                    step(c);
                    ++sims, ++j.sims;
                    SimOut o{};
                    read_out(o);
                    if (o.flags & FL_ONGROUND) {
                        if (o.origin.z > floor_z + k_spot_rise)
                            score = 1000 + static_cast<int>(o.origin.z - floor_z);
                    } else if (!pass && !pinned(o.vel) && FaceD(o.origin, wall_n) < best_d)
                        best_d = FaceD(o.origin, wall_n), pick = c;
                }
            if (!j.valid)
                break;
            if (score < 0) {
                if (!load(k_wcSpotRoll))
                    break;
                step(pick);
                ++sims, ++j.sims;
                SimOut o{};
                read_out(o);
                if (o.flags & FL_ONGROUND)
                    score = o.origin.z > floor_z + k_spot_rise ? 1000 + static_cast<int>(o.origin.z - floor_z) : 0;
                else if (pinned(o.vel) || ++j.ticks >= k_spot_ticks || !Pred_SnapSave(pLocal, k_wcSpotRoll))
                    score = 0; /* a pin here = a sticky spot */
            }
            if (score >= 0)
                finish(score);
        }
        s_wcCount[c_spot_sims] += j.sims;
        if (!j.valid)
            return false;
        // walk to the best spot found so far ( ties: nearest to where you are )
        const Vec3 rel   = origin - j.base;
        const float here = rel.x * t.x + rel.y * t.y;
        cut              = false;
        commit_end       = sims + static_cast<int>(std::size(k_spot_along));
        int best = -1;
        for (int i = 0; i < j.idx; ++i)
            if (j.score[i] > 0 && (best < 0 || j.score[i] > j.score[best] ||
                                   (j.score[i] == j.score[best] && fabsf(AtIdx(i) - here) < fabsf(AtIdx(best) - here))))
                best = i;
        if (best < 0 || fabsf(AtIdx(best) - here) <= k_spot_snap)
            return false;
        const float target = AtIdx(best);
        float wx, wy;
        WorldWish(keys.viewangles.y, keys.forwardmove, keys.sidemove, wx, wy);
        const float in_amt = (std::max)(0.f, -(wx * wall_n.x + wy * wall_n.y));
        const float yr = DEG2RADF(keys.viewangles.y), cy = cosf(yr), sy = sinf(yr);
        float best_cost = FLT_MAX;
        UserCmd_t walk  = keys;
        for (const float a : k_spot_along) {
            const float x = -wall_n.x * in_amt + t.x * a, y = -wall_n.y * in_amt + t.y * a;
            UserCmd_t c   = keys;
            c.forwardmove = x * cy + y * sy;
            c.sidemove    = x * sy - y * cy;
            SimOut o{};
            if (!sim(c, o))
                break;
            const Vec3 d     = o.origin - j.base;
            const float cost = fabsf(d.x * t.x + d.y * t.y + (o.vel.x * t.x + o.vel.y * t.y) * ipt * k_spot_look - target);
            if (cost < best_cost)
                best_cost = cost, walk = c;
        }
        if (best_cost == FLT_MAX)
            return false;
        send(walk);
        ++s_wcCount[c_spot];
        branch = "spot";
        return true;
    };

    bool landed = false, held = false, jump_fix = false;
    if (!on_ground) {
        s_wcSpot.valid = false;
        const bool was_holding = s_wcHolding;
        s_wcHolding            = false;
        s_wcCount[c_leave_hang] += (start_pinned || was_holding) && you_leave;
        if ((start_pinned || was_holding) && !you_leave) {
            restore();
            const UserCmd_t hold = with(wall_yaw, 0.f, was_holding ? (s_wcHoldButtons & IN_DUCK) != 0 : user_duck);
            if (hold_lands(hold)) {
                // lab P67: a land now beats one more hang tick ( hang -14%, catches same )
                const float F    = k_hull_step / (ipt * airaccel * ipt * friction);
                const bool hd    = (hold.buttons & IN_DUCK) != 0;
                const auto& fine = FineRungs();
                for (size_t i = 1; i < fine.size() && !held; ++i) {
                    const UserCmd_t c = with(wall_yaw, fine[i] * F, hd);
                    SimOut o{};
                    if (!sim(c, o))
                        break;
                    if ((o.flags & FL_ONGROUND) && o.origin.z > s_wcGroundZ + k_land_above) {
                        send(c);
                        landed = held = true;
                        ++s_wcCount[c_hold_land];
                        branch = "hold_land";
                    }
                }
                if (!held) {
                    send(hold);
                    held = s_wcHolding = true;
                    s_wcHoldButtons    = hold.buttons;
                    ++s_wcCount[c_hold];
                    branch = "hold";
                }
            }
        }
        if (!held && start_pinned) {
            exit_pin();
            branch = "exit";
        } else if (!held && !far_pin) {
            const float F = k_hull_step / (ipt * airaccel * ipt * friction);
            // crouch catches off = no crouch pops in demos: other stance never sent ( lab P61 vs P57: rows -30% )
            bool live[2] = { true, g_wcDuck };
            float best_d = FLT_MAX, best_xy = 0.f;
            UserCmd_t best_c{};
            bool have_park = false, best_snag = false;
            int pin_tries  = 0;
            SimOut last{};
            struct Park {
                float d, xy;
                UserCmd_t c;
                bool snag;
            };
            std::vector<Park> parks;
            // lab P69: a flush park sits 1e-4..3e-3 off the face, inside both DIST_EPSILONs, so a disp patch 0.0024 proud across a vertical
            // seam stops it dead ( keys rest 1/32 out and slide past ). moving along, a sim that keeps under k_snag_keep of it snagged
            const Vec3 ft(wall_n.y, -wall_n.x, 0.f);
            const float along0 = start_vel.x * ft.x + start_vel.y * ft.y;
            const bool seam_on = fabsf(along0) > k_snag_along;
            const auto snags   = [&](const Vec3& v) {
                return seam_on && (v.x * ft.x + v.y * ft.y) * (along0 < 0.f ? -1.f : 1.f) < k_snag_keep * fabsf(along0);
            };
            // 1 caught, 0 go on, -1 out of clock
            const auto test = [&](const UserCmd_t& c, const bool flip, const bool park) {
                if (!sim(c, last))
                    return -1;
                if (last.flags & FL_ONGROUND) {
                    if (last.origin.z < s_wcGroundZ - k_low_ledge) {
                        ++s_wcCount[c_low];
                        branch = "low";
                        return 1;
                    }
                    SimOut own{};
                    if (sim(keys, own) && (own.flags & FL_ONGROUND)) {
                        ++s_wcCount[c_own];
                        branch = "own";
                        return 1;
                    }
                    send(c);
                    landed = true;
                    ++s_wcCount[c_land];
                    s_wcCount[c_flip] += flip;
                    branch = flip ? "land_flip" : "land";
                    DebugLog("WC LAND: duck=%d fwd=%.5f side=%.5f z=%.3f vz=%.2f n=%.3f,%.3f,%.3f disp=%d", (c.buttons & IN_DUCK) != 0, c.forwardmove,
                             c.sidemove, origin.z, start_vel.z, wall_n.x, wall_n.y, wall_n.z, disp_wall ? 1 : 0);
                    return 1;
                }
                if (pinned(last.vel)) {
                    if (pin_tries < k_pin_tries && !you_leave) {
                        ++pin_tries;
                        const bool cd = (c.buttons & IN_DUCK) != 0;
                        bool hd = cd, lands = hold_lands(with(wall_yaw, 0.f, cd));
                        // lab P71: a zero hold in the other stance that lands counts too ( first +1.3%, rows +1% ), the hold follows in it
                        if (!lands && g_wcDuck && sim(c, last) && pinned(last.vel)) {
                            hd    = !cd;
                            lands = hold_lands(with(wall_yaw, 0.f, hd));
                            s_wcCount[c_pin_flip] += lands;
                        }
                        if (lands) {
                            send(c);
                            landed = held = s_wcHolding = true;
                            s_wcHoldButtons = hd ? (c.buttons | IN_DUCK) : (c.buttons & ~IN_DUCK);
                            ++s_wcCount[c_pin_catch];
                            branch = "pin_catch";
                            DebugLog("WC PIN CATCH: duck=%d hold duck=%d fwd=%.5f z=%.3f vz=%.2f n=%.3f,%.3f disp=%d", cd, hd, c.forwardmove, origin.z,
                                     start_vel.z, wall_n.x, wall_n.y, disp_wall ? 1 : 0);
                            return 1;
                        }
                    }
                    return 0;
                }
                if (park && !flip && !slowed(last.vel)) {
                    const float d = FaceD(last.origin, wall_n);
                    parks.push_back({ d, last.vel.Length2D(), c, snags(last.vel) });
                    if (d < best_d)
                        best_d = d, best_c = c, best_xy = last.vel.Length2D(), have_park = true;
                }
                return 0;
            };
            const auto scan_cut = [&]() {
                if (!sims || !cannot_fit(budget.UsedUs() * k_reserve_sims / sims))
                    return false;
                ++s_wcCount[c_scan_cut];
                return true;
            };
            const auto& fine = FineRungs();
            int r = 0;
            for (size_t i = 0; i < fine.size() && !r; ++i)
                for (int pass = 0; pass < 2 && !r; ++pass) {
                    if (!live[pass])
                        continue;
                    if (scan_cut()) {
                        r = -1;
                        break;
                    }
                    const bool duck = pass ? !user_duck : user_duck;
                    r               = test(with(wall_yaw, fine[i] * F, duck), pass != 0, true);
                    if (!r && !i && pass && last.vel.z > k_rise_vz) {
                        live[1] = false; /* flip stance never grounds rising this fast; your stance still parks */
                        ++s_wcCount[c_rise];
                    }
                }
            // park sits at contact, a lip lands 1-2 floats out ( 10-08 lab P54 ): away rungs land / pin catch only, never park
            for (size_t i = 1; i < fine.size() && !r; ++i) {
                if (fine[i] < k_away_lo)
                    continue;
                if (scan_cut()) {
                    r = -1;
                    break;
                }
                r = test(with(wall_yaw, -fine[i] * F, user_duck), false, false);
                s_wcCount[c_away] += r > 0 && (landed || held);
            }
            for (const float m : k_coarse) {
                if (r || scan_cut()) {
                    r = r ? r : -1;
                    break;
                }
                r = test(with(wall_yaw, m * F, user_duck), false, true);
            }
            const float vel_yaw = RAD2DEGF(atan2f(start_vel.y, start_vel.x));
            const float side    = std::remainder(vel_yaw - wall_yaw, 360.f) < 0.f ? 1.f : -1.f;
            for (const float f : k_along_fwd)
                for (const float a : k_along_off)
                    for (int pass = 0; pass < 2 && !r; ++pass) {
                        if (!live[pass])
                            continue;
                        if (scan_cut()) {
                            r = -1;
                            break;
                        }
                        r = test(with(wall_yaw + side * a, f, pass ? !user_duck : user_duck), pass != 0, false);
                        s_wcCount[c_along] += r > 0;
                    }
            if (r <= 0) {
                // a clock cut mid ladder must still compare your keys and exit a pin ( lab: 3 sims past the cap ), + the seam away rungs
                cut        = false;
                commit_end = sims + k_commit_sims + 3 * k_robust_parks + static_cast<int>(std::size(k_coarse));
                // 10-31 09:57 log: 21 parks the server pinned that wc simmed free ( 14 rising = jump killed ). START drifts z 1 ulp + 1e-3 u/s a
                // tick ( lab FL_NOISE 1 ): the park must stay free from START nudged into the face, lab P62 pinned ticks -53%, catches same.
                // parks that keep their along speed go first ( seam )
                if (have_park) {
                    std::sort(parks.begin(), parks.end(), [](const Park& a, const Park& b) { return a.snag != b.snag ? b.snag : a.d < b.d; });
                    have_park = false;
                    for (int p = 0; p < static_cast<int>(parks.size()) && p < k_robust_parks && !have_park; ++p) {
                        bool ok = true;
                        for (int v = 0; v < 3 && ok; ++v) {
                            Vec3 o = origin, vel = start_vel;
                            if (v < 2) {
                                for (int u = 0; u <= v; ++u) {
                                    if (fabsf(wall_n.x) > 0.05f)
                                        o.x = std::nextafter(o.x, wall_n.x > 0.f ? -FLT_MAX : FLT_MAX);
                                    if (fabsf(wall_n.y) > 0.05f)
                                        o.y = std::nextafter(o.y, wall_n.y > 0.f ? -FLT_MAX : FLT_MAX);
                                }
                            } else {
                                vel.x -= wall_n.x * k_robust_vel;
                                vel.y -= wall_n.y * k_robust_vel;
                            }
                            restore();
                            AsSetOrigin(pLocal, o);
                            EntSetV3(pLocal, g_offVelocity, vel);
                            SimOut q{};
                            ok = chain(parks[p].c, q) && !pinned(q.vel);
                        }
                        if (ok)
                            best_d = parks[p].d, best_c = parks[p].c, best_xy = parks[p].xy, best_snag = parks[p].snag, have_park = true;
                    }
                    s_wcCount[c_robust] += !have_park;
                    s_wcCount[c_seam_park] += have_park && !best_snag && std::any_of(parks.begin(), parks.end(), [&](const Park& p) { return p.snag && p.d < best_d; });
                }
                SimOut k{};
                const bool kok   = sim(keys, k);
                const bool kpin  = kok && !(k.flags & FL_ONGROUND) && pinned(k.vel);
                const bool ksnag = kok && !kpin && !(k.flags & FL_ONGROUND) && snags(k.vel);
                // lab P69: every park snags ( or none is robust ) and your keys snag too = away rungs, one step out of DIST_EPSILON for the
                // crossing tick ( 0.2 F = 1/160u clears a 0.0024 seam ). wallA seam snags 128 -> 27, first + rows +1%
                bool seam_away = false;
                if (ksnag && !you_leave && (!have_park || best_snag))
                    for (const float m : k_coarse) {
                        const UserCmd_t c = with(wall_yaw, -m * F, user_duck);
                        SimOut o{};
                        if (!sim(c, o))
                            break;
                        if (!(o.flags & FL_ONGROUND) && !pinned(o.vel) && !snags(o.vel)) {
                            best_d = FaceD(o.origin, wall_n), best_c = c, best_xy = o.vel.Length2D(), best_snag = false;
                            have_park = seam_away = true;
                            ++s_wcCount[c_seam];
                            break;
                        }
                    }
                s_wcCount[c_aa_leave] += !you_leave && WishLeavesWall(keys, wall_n);
                if (you_leave) {
                    ++s_wcCount[c_leave];
                    branch = "leave";
                    if (kpin)
                        exit_pin();
                } else if (have_park && (kpin || !s_wcParkOff)) {
                    if (kok && !kpin && !(k.flags & FL_ONGROUND) && FaceD(k.origin, wall_n) <= best_d + k_keys_ulp && !(ksnag && !best_snag)) {
                        ++s_wcCount[c_keys];
                        branch = "keys";
                    } else if (kok && !kpin && !(k.flags & FL_ONGROUND) && best_xy < k.vel.Length2D() - k_park_keep && !WishLeavesWall(keys, wall_n)) {
                        // park sends ~0 wish: froze w+strafe gain ( 10-08 log u 37 s 32 x10 ) and sat in corners at xy 0. not auto align's push off
                        ++s_wcCount[c_park_keep];
                        branch = "keys_pk";
                    } else {
                        send(best_c);
                        ++s_wcCount[c_park];
                        branch = seam_away ? "seam" : "park";
                    }
                    free_sent = true;
                } else if (kpin) {
                    exit_pin();
                    branch = "exit";
                } else if (have_park) {
                    ++s_wcCount[c_no_park];
                    branch    = "keys_np";
                    free_sent = kok;
                } else if (!parks.empty())
                    branch = "keys_rob";
            }
        }
    } else {
        s_wcHolding = false;
        branch      = "ground";
        SimOut out{};
        const bool spotted = !(cmd->buttons & IN_JUMP) && spot();
        if (!spotted && sim(*cmd, out)) {
            if (pinned(out.vel) || slowed(out.vel))
                exit_pin();
            else if (cmd->buttons & IN_JUMP) {
                GameTrace_t tr{};
                const Vec3 from = origin + wall_n * k_seam_off;
                TraceHull(from, from - Vec3(0.f, 0.f, k_seam_below), mins, maxs, MASK_PLAYERSOLID, &filter, &tr);
                if (tr.fraction >= 1.f || from.z - tr.endpos.z > k_seam_floor) {
                    const float gravity = CvarFloat("sv_gravity", 800.f);
                    const float target  = k_jump_share * (k_jump_impulse - gravity * ipt);
                    if (out.vel.z < target) {
                        bool reached = false;
                        for (int i = 0; i <= k_back_steps && !reached; ++i) {
                            const UserCmd_t c = with(wall_yaw, -(static_cast<float>(i) / static_cast<float>(k_back_steps)), user_duck);
                            SimOut back{};
                            if (!sim(c, back))
                                break;
                            if (!(back.vel.z < target)) {
                                cmd->forwardmove = c.forwardmove;
                                cmd->sidemove    = c.sidemove;
                                reached = jump_fix = true;
                                ++s_wcCount[c_jump_back];
                                branch = "jump_back";
                            }
                        }
                        if (!reached && !cut) {
                            cmd->buttons &= ~IN_JUMP;
                            jump_fix = true;
                            ++s_wcCount[c_jump_strip];
                            branch = "jump_strip";
                            DebugLog("WC JUMP STRIP: vz=%.2f target=%.2f z=%.3f", out.vel.z, target, origin.z);
                        }
                    }
                }
            }
        }
    }

    // visualize: tick 1 = what goes out ( air stuck's cmd when it keeps the steering ), then wc's hold on a catch ( what every catch is
    // proven with ), else that cmd again. extra sims only with the option on, off the clock
    const long long us = budget.UsedUs();
    if (g_wcViz) {
        WcDrawData& v = s_wcViz;
        v        = {};
        v.valid  = true;
        v.ms     = GetTickCount64();
        v.ground = on_ground;
        v.branch = branch;
        v.n      = wall_n;
        v.sims   = sims;
        v.half   = maxs.x;
        v.height = maxs.z - mins.z;
        v.groundZ = s_wcGroundZ;
        v.origin = origin;
        const Vec3 mid  = wall_tr.endpos + Vec3(0.f, 0.f, (mins.z + maxs.z) * 0.5f);
        const float off = FaceD(mid, wall_n) - wall_tr.plane.dist;
        v.wall = fabsf(off) <= 32.f ? mid - wall_n * off : mid - wall_n * (maxs.x * (fabsf(wall_n.x) + fabsf(wall_n.y)));
        if (!on_ground) {
            const bool catching   = landed || held;
            const UserCmd_t out   = as_steers && !catching && !jump_fix ? as_cmd : *cmd;
            const UserCmd_t next  = catching ? with(wall_yaw, 0.f, (out.buttons & IN_DUCK) != 0) : out;
            restore();
            v.path[v.pathN++] = origin;
            for (int k = 0; k < k_viz_ticks; ++k) {
                step(k ? next : out);
                SimOut o{};
                read_out(o);
                v.path[v.pathN++] = o.origin;
                if (o.flags & FL_ONGROUND) {
                    v.lands = o.origin.z > s_wcGroundZ + k_land_above;
                    v.land  = o.origin;
                    break;
                }
            }
        } else if (s_wcSpot.valid) {
            const auto& j = s_wcSpot;
            v.spotValid = true;
            v.spotBase  = j.base;
            v.spotN     = j.n;
            v.spotCount = j.count;
            v.spotIdx   = j.idx;
            std::copy(std::begin(j.score), std::end(j.score), v.spotScore);
        }
    }

    // wc t: every wall tick. u = end of your cmd, s = end of what wall climb sent ( s.xy < u.xy = it cost speed ). org / vel / yaw
    // = START + your keys, enough to replay the tick in fl_lab. extra sims, debug log only, not in us=. quiet ticks 1 in 33 ( DiagOk )
    if (g_debugLogEnable && DiagOk(s_wcDiagTick, landed || held || jump_fix || was_cut)) {
        const auto raw = [&](const UserCmd_t& c, SimOut& out) {
            restore();
            step(c);
            read_out(out);
        };
        SimOut u{}, s{};
        raw(keys, u);
        if (cmd->forwardmove != keys.forwardmove || cmd->sidemove != keys.sidemove || cmd->buttons != keys.buttons)
            raw(*cmd, s);
        else
            s = u;
        DebugLog("WC T: %s g=%d n=%.2f,%.2f disp=%d xy=%.1f u=%.1f/%.1f/%d s=%.1f/%.1f/%d fs=%.1f,%.1f->%.3f,%.3f j=%d/%d d=%d/%d sims=%d%s org=%.4f,%.4f,%.4f vel=%.3f,%.3f,%.3f yaw=%.3f gz=%.3f",
                 branch, on_ground, wall_n.x, wall_n.y, disp_wall ? 1 : 0, start_vel.Length2D(), u.vel.Length2D(), u.vel.z,
                 (u.flags & FL_ONGROUND) != 0, s.vel.Length2D(), s.vel.z, (s.flags & FL_ONGROUND) != 0, keys.forwardmove, keys.sidemove,
                 cmd->forwardmove, cmd->sidemove, (keys.buttons & IN_JUMP) != 0, (cmd->buttons & IN_JUMP) != 0, user_duck,
                 (cmd->buttons & IN_DUCK) != 0, sims, was_cut ? " CUT" : "", origin.x, origin.y, origin.z, start_vel.x, start_vel.y,
                 start_vel.z, keys.viewangles.y, s_wcGroundZ);
    }
    restore();
    s_wcCount[c_sims] += sims;
    ++s_wcCount[c_ticks];
    s_wcUs += us;
    s_wcUsWorst = (std::max)(s_wcUsWorst, us);
    if (was_cut)
        ++s_wcCount[c_cut];
    const bool caught_now = landed || held || jump_fix;
    if (as_steers) {
        if (caught_now) {
            s_airStuckHit = s_asAuthoredViewValid = s_asStampValid = false;
            ++s_wcCount[c_as_take];
        } else
            *cmd = as_cmd;
    }
    s_wcLanded     = landed;
    s_wallClimbHit = caught_now;
    s_wcFreeCmd    = free_sent && (!as_steers || caught_now) ? cmd->command_number : -1;
}
