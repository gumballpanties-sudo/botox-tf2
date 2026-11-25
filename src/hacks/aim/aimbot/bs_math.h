#pragma once
// ============================================================================================
// AUTO BACKSTAB MATH -- engine-free, deterministic, gate-tested by tests/backstab_test.cpp.
// aim_melee.cpp feeds it engine values; nothing here touches the game.
//
// WHAT THE SERVER DOES (source-sdk-2013, re-read for dih-bs15):
//   CTFKnife::PrimaryAttack          tf_weapon_knife.cpp:176  lag comp -> DoSwingTrace -> enemy
//                                    player? -> CanPerformBackstabAgainstTarget -> IsBehindAndFacing
//   IsBehindAndFacingTarget          tf_weapon_knife.cpp:447  WorldSpaceCenter both ends, z = 0,
//                                    behind > 0, facing > 0.5, facestab > -0.3
//   pTarget->EyeAngles()             baseplayer_shared.cpp:310  returns pl.v_angle -- LIVE. Lag comp
//                                    restores origin/mins/maxs/body angles only (lagcomp.cpp:525).
//   targettick                       player_lagcompensation.cpp:393-402
//                                    = cmd->tick_count - TIME_TO_TICKS(m_fLerpTime)
//   BacktrackPlayer                  player_lagcompensation.cpp:440  newest record <= target time,
//                                    linear lerp toward the next newer one
//   m_fLerpTime                      player.cpp:3484-3515  max(cl_interp, ratio / updaterate), both
//                                    clamped by the sv_ cvars; 0 when cl_interpolate 0
//   DoSwingTraceInternal             tf_weaponbase_melee.cpp:392  ray first, +-18 hull ONLY if the
//                                    ray hit nothing (fraction >= 1)
//   m_angEyeAngles[1]                tf_player.cpp:752  SendPropAngle 10 bits (0.35 deg grid)
//   non-local m_vecOrigin            tf_player.cpp:748  COORD_MP_LOWPRECISION (1/8 unit)
//
// THE THREE THINGS THAT WERE WRONG IN bs14, AND WHAT REPLACED THEM:
//   1. HIS YAW WAS SAMPLED OFF THE INTERPOLATED NETVAR. That copy is older than the newest
//      snapshot by the whole interp window, so every net setting with cl_interp 0.1 (the TF2
//      default) added 100+ ms to the horizon the yaw has to be guessed over. The track below is
//      fed RAW snapshots at FRAME_NET_UPDATE_END (PreDataUpdate restores every interpolated var
//      to its last networked value first, c_baseentity.cpp:2166). The horizon is now ping +
//      snapshot age and nothing else, whatever cl_interp / cl_interp_ratio / cl_updaterate say.
//   2. THE BAND WAS A WORST CASE. The max of the last 16 prediction errors means one flick in
//      the last quarter second refused every sidestab and every short window. Replaced by
//      measured odds: the RMS error of whichever estimator (hold / half rate / full rate) has
//      been most accurate on THIS player lately, and a normal CDF over the server's own yaw
//      window. The swing takes the odds, not a worst case.
//   3. HIS POSITION WAS THE RENDERED ONE, which sits 1 tick + 1 frame behind the tick lag
//      compensation rewinds to. Rewind() reproduces BacktrackPlayer on the same records the
//      server keeps, at the exact tick the server will pick.
//
// dih-bs23 (the server re-read for it changed nothing above -- the geometry was right -- and found the
// last two guesses, both on the yaw side):
//   4. WHEN THE SERVER READS HIM was tick_count + both netchan latencies: 0.5-1.5 ticks late for a
//      command sent on its tick, and blind to the tick (or two) a silent-aim stab is choked for. Now
//      measured off the server's own command acks, plus the stab's choke (CmdClock, DueTime).
//   5. THE ODDS WERE A GAUSSIAN round the bet, and closed loop that p did not sort swings (fires at
//      p .7-.8 landed 97.6%, at p > .999 only 98.6%). Now, wherever there is data for the bet, the mass
//      of a residual histogram learned per MOTION STATE from every scored bet of every player
//      (ResidTable, StabMix), with Decide's bands (.90 / .99) on it. Where there is none it is bs22
//      unchanged. Closed loop: TestMeasuredOdds.
// Still not modelled, on purpose: MvM's "sapped robot is always a backstab" (knife:432-439), powerup
// mode's grapple refusals (knife:411-425), cl_lagcompensation 0, the engine's own choke under
// cl_cmdrate < tickrate.
//
// TWO CLOCKS, AND THEY ARE NOT THE SAME. m_flSimulationTime is set from curtime inside HIS command
// run (player.cpp:4730), and there curtime is his m_nTickBase (player_command.cpp:336-342), which
// the server keeps 0..2*sv_clockcorrection_msecs AHEAD of the absolute tick (player.cpp:3140-3172,
// 0-120 ms by default, per player, drifting). So:
//   * records for Rewind are keyed by SIMULATION time -- lag compensation compares exactly those
//     against the absolute target tick (lagcomp.cpp:487), mismatch and all, and so do we;
//   * his YAW is keyed by the PACKET's server tick (engine GetLastTimeStamp) -- the absolute clock
//     our command's tick_count and the server's read of pl.v_angle live on.
// ============================================================================================
#include <algorithm>
#include <cmath>

namespace BsMath
{
	struct V3
	{
		float x = 0.f, y = 0.f, z = 0.f;
	};
	inline V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	inline V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	inline V3 operator*(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
	inline float Len2D(V3 a) { return std::sqrt(a.x * a.x + a.y * a.y); }

	constexpr float k_pi = 3.14159265f;
	constexpr float k_behindHalf   = 90.f;        // acos( 0.0)  knife:472/481
	constexpr float k_facingHalf   = 60.f;        // acos( 0.5)  knife:473/481
	constexpr float k_facestabHalf = 107.4576f;   // acos(-0.3)  knife:474/481

	inline float Deg(float r) { return r * (180.f / k_pi); }
	inline float Rad(float d) { return d * (k_pi / 180.f); }

	// [-180, 180)
	inline float Wrap(float a)
	{
		a = std::fmod(a + 180.f, 360.f);
		if (a < 0.f)
			a += 360.f;
		return a - 180.f;
	}

	inline float Phi(float z) { return 0.5f * std::erfc(-z * 0.70710678f); }

	// ---- the server's test, verbatim (knife:447-481) -------------------------------------------
	// Used by the tests as the ground truth the angle-window model must agree with.
	inline bool ServerBehindAndFacing(V3 ownerCentre, V3 targetCentre, float ownerYaw, float targetYaw)
	{
		float tx = targetCentre.x - ownerCentre.x, ty = targetCentre.y - ownerCentre.y;
		const float l = std::sqrt(tx * tx + ty * ty);
		if (l > 1e-6f) { tx /= l; ty /= l; } else { tx = ty = 0.f; }   // NormalizeInPlace on zero = zero
		const float ox = std::cos(Rad(ownerYaw)), oy = std::sin(Rad(ownerYaw));
		const float gx = std::cos(Rad(targetYaw)), gy = std::sin(Rad(targetYaw));
		const float behind = tx * gx + ty * gy;
		const float facing = tx * ox + ty * oy;
		const float facestab = gx * ox + gy * oy;
		return behind > 0.f && facing > 0.5f && facestab > -0.3f;
	}

	// ---- the odds ---------------------------------------------------------------------------------
	// The three dots are cosines between unit vectors, so the test is three angle windows:
	//   behind:   |wrap(targetYaw - bearing)| < 90
	//   facing:   |wrap(ownerYaw  - bearing)| < 60
	//   facestab: |wrap(targetYaw - ownerYaw)| < 107.4576
	// His live yaw is ~ N(centre, sigYaw), the bearing ~ N(bearing, sigBear), independent; our own yaw
	// we write. Relative to the bearing estimate (y = his yaw, d = the bearing's error) the windows are
	// three strips in the (y, d) plane:
	//   facing |dOwn - d| < 60,   behind |y - d| < 90,   facestab |y - dOwn| < 107.46
	// p = the Gaussian mass inside all three (StabJoint). margin / lo / hi stay the centre geometry --
	// behind and facestab as one arc of his yaw -- for the turn guard and the log.
	//
	// dih-bs22: p WAS pYaw(the tighter edge per side only) x pFacing. Dropping the looser edge is wrong
	// once the two sit close and the bearing is wide, and so is the product when facing and behind
	// both lean on the same bearing error. bs21 log L506, a front: d 7.7, brate -3469 (bear 15.7), aim
	// offset 30 -- read .972, the server's test says .821. And the error PICKED that offset: the
	// candidates are sorted by p, and offset 0 was an honest .849. The other way round, facing + behind
	// on the same side read .47 for .65 -- a stab refused that was there (TestOddsJoint).
	struct StabOdds
	{
		float p = 0.f;        // chance the server calls it a backstab
		float margin = 0.f;   // degrees inside [lo, hi] at the centre estimate (< 0 = outside)
		float facing = 0.f;   // degrees inside the facing window
		float lo = 0.f, hi = 0.f;
		float r = 0.f;        // his yaw - bearing at the estimate, unwrapped next to the window
	};

	// The mass of N(r, sigYaw) x N(0, sigBear) inside the three strips (dOwn = our yaw - bearing, r = his
	// yaw - bearing, unwrapped next to the window). The outer integral runs over the NARROWER of the two,
	// clipped by its own strip: 6-point Gauss-Legendre over +-5 sigma, pieces split at the mean and at the
	// two kinks where the inner window's edge changes test. The inner one is a Phi difference. Worst error
	// 4.4e-4 against an exact reference over random geometry; ~0.5 us a call (the old product ~0.15).
	// ponytail: both Gaussians on a line, not a circle -- past sigma ~40 the wrap copies are lost (p low,
	// e.g. .51 for .55 at sigYaw 130). Errs safe and never above k_pTake; sum the +-360 copies if it matters.
	inline float StabJoint(float dOwn, float r, float sigYaw, float sigBear)
	{
		constexpr float x[3] = { 0.2386191861f, 0.6612093865f, 0.9324695142f };
		constexpr float w[3] = { 0.4679139346f, 0.3607615730f, 0.1713244924f };
		const bool bYaw = sigYaw <= sigBear;   // outer = his yaw (clipped by facestab), else the bearing (by facing)
		const float mu = bYaw ? r : 0.f, s = bYaw ? sigYaw : sigBear;
		const float half = bYaw ? k_facestabHalf : k_facingHalf;
		const float a = std::max(mu - 5.f * s, dOwn - half), b = std::min(mu + 5.f * s, dOwn + half);
		if (b <= a)
			return 0.f;
		const float kink = bYaw ? k_behindHalf - k_facingHalf : k_facestabHalf - k_behindHalf;
		float cut[5] = { a, dOwn - kink, mu, dOwn + kink, b };
		std::sort(cut + 1, cut + 4);
		float p = 0.f;
		for (int i = 0; i < 4; i++)
		{
			const float lo = std::clamp(cut[i], a, b), hi = std::clamp(cut[i + 1], a, b);
			if (hi <= lo)
				continue;
			const float m = 0.5f * (lo + hi), h = 0.5f * (hi - lo);
			for (int j = 0; j < 6; j++)
			{
				const float v = m + (j & 1 ? h : -h) * x[j >> 1], z = (v - mu) / s;
				float in = 0.f;
				if (bYaw)
				{	// his yaw is v: the bearing error must keep facing and behind
					const float l = std::max(dOwn - k_facingHalf, v - k_behindHalf), u = std::min(dOwn + k_facingHalf, v + k_behindHalf);
					if (u > l) in = Phi(u / sigBear) - Phi(l / sigBear);
				}
				else
				{	// the bearing error is v: his yaw must keep behind and facestab
					const float l = std::max(dOwn - k_facestabHalf, v - k_behindHalf), u = std::min(dOwn + k_facestabHalf, v + k_behindHalf);
					if (u > l) in = Phi((u - r) / sigYaw) - Phi((l - r) / sigYaw);
				}
				p += h * w[j >> 1] * std::exp(-0.5f * z * z) * in;
			}
		}
		return std::clamp(p * (0.39894228f / s), 0.f, 1.f);   // 1/sqrt(2 pi)
	}

	// dih-bs23: the same mass with his yaw drawn from a residual HISTOGRAM round the bet instead of a
	// Gaussian (ResidTable, below: how bets made in his motion state have actually landed). n / N = that
	// state's counts, dir = the way its positive residuals point. A cell's weight is
	//     (count + k_resPrior x the Gaussian's share of the cell) / (N + k_resPrior)
	// so an empty state IS the Gaussian (to the cell grid), and his yaw is uniform inside a cell. Given
	// his yaw the three strips leave the bearing error one interval: a Phi difference, as in StabJoint.
	constexpr int   k_resCells = 19;
	constexpr float k_resEdge[k_resCells + 1] = { -180.f, -90.f, -60.f, -40.f, -25.f, -15.f, -8.f, -4.f, -2.f, -1.f,
		1.f, 2.f, 4.f, 8.f, 15.f, 25.f, 40.f, 60.f, 90.f, 180.f };
	constexpr float k_resPrior = 25.f;   // pseudo-samples the Gaussian is worth
	inline float StabMix(float dOwn, float r, float sigYaw, float sigBear, const float* n, float N, float dir)
	{
		sigYaw = std::max(sigYaw, 0.01f);
		sigBear = std::max(sigBear, 0.01f);
		float p = 0.f;
		for (int c = 0; c < k_resCells; c++)
		{
			const float a = k_resEdge[c], b = k_resEdge[c + 1];
			// the outermost cells take the Gaussian's tails
			const float prior = (c == k_resCells - 1 ? 1.f : Phi(b / sigYaw)) - (c == 0 ? 0.f : Phi(a / sigYaw));
			const float w = (n[c] + k_resPrior * prior) / (N + k_resPrior);
			if (w < 1e-5f)
				continue;
			const int m = std::clamp(int((b - a) * 0.25f + 0.999f), 2, 23);   // midpoints, at most 4 degrees apart
			float in = 0.f;
			for (int j = 0; j < m; j++)
			{
				// his yaw (relative to the bearing), unwrapped next to our own
				const float y = dOwn + Wrap(r + dir * (a + (b - a) * (float(j) + 0.5f) / float(m)) - dOwn);
				if (std::fabs(y - dOwn) >= k_facestabHalf)
					continue;
				const float l = std::max(dOwn - k_facingHalf, y - k_behindHalf), u = std::min(dOwn + k_facingHalf, y + k_behindHalf);
				if (u > l)
					in += Phi(u / sigBear) - Phi(l / sigBear);
			}
			p += w * in / float(m);
		}
		return std::clamp(p, 0.f, 1.f);
	}

	inline StabOdds StabProbability(float bearing, float ownerYaw, float yawCentre, float sigYaw, float sigBear)
	{
		StabOdds o;
		sigYaw = std::max(sigYaw, 0.01f);
		sigBear = std::max(sigBear, 0.01f);

		const float dOwn = Wrap(ownerYaw - bearing);
		o.facing = k_facingHalf - std::fabs(dOwn);
		if (std::fabs(dOwn) >= 90.f)
			return o;   // pointed away from him: nothing below is a single arc any more, and it is ~0 anyway

		const float ownU = bearing + dOwn;   // our yaw, unwrapped next to the bearing
		const float lo = std::max(bearing - k_behindHalf, ownU - k_facestabHalf);
		const float hi = std::min(bearing + k_behindHalf, ownU + k_facestabHalf);
		o.lo = lo; o.hi = hi;

		const float mid = 0.5f * (lo + hi);
		const float c = mid + Wrap(yawCentre - mid);
		o.margin = std::min(hi - c, c - lo);
		o.r = c - bearing;
		if (hi <= lo)
			return o;

		o.p = StabJoint(dOwn, c - bearing, sigYaw, sigBear);
		return o;
	}

	// ---- the swing's reach, analytically ---------------------------------------------------------
	// A +-h cube swept along a segment touches an AABB exactly when the segment enters that box
	// grown by h (slab test). The same set the engine's hull trace tests against a SOLID_BBOX player.
	// Returns the entry distance along the segment, or -1 when it never enters. 0 = starts inside
	// (the engine's startsolid, which still reports fraction 0 on that entity).
	inline float SegmentEntry(V3 s, V3 d, float len, V3 lo, V3 hi, float grow)
	{
		const float S[3] = { s.x, s.y, s.z }, D[3] = { d.x, d.y, d.z };
		const float L[3] = { lo.x - grow, lo.y - grow, lo.z - grow };
		const float H[3] = { hi.x + grow, hi.y + grow, hi.z + grow };
		float t0 = 0.f, t1 = len;
		for (int a = 0; a < 3; a++)
		{
			if (L[a] > H[a])
				return -1.f;   // a negative grow bigger than the box: nothing left to enter
			if (std::fabs(D[a]) < 1e-7f)
			{
				if (S[a] < L[a] || S[a] > H[a])
					return -1.f;
				continue;
			}
			float ta = (L[a] - S[a]) / D[a], tb = (H[a] - S[a]) / D[a];
			if (ta > tb)
				std::swap(ta, tb);
			t0 = std::max(t0, ta);
			t1 = std::min(t1, tb);
			if (t0 > t1)
				return -1.f;
		}
		return t0;
	}

	// How far the hull could shrink and still connect. -1 = it does not connect at all.
	inline float ReachSlack(V3 s, V3 d, float len, V3 lo, V3 hi, float hull)
	{
		if (SegmentEntry(s, d, len, lo, hi, hull) < 0.f)
			return -1.f;
		if (SegmentEntry(s, d, len, lo, hi, 0.f) >= 0.f)
			return hull;
		float gLo = 0.f, gHi = hull;   // gHi always connects, gLo never does
		for (int i = 0; i < 12; i++)
		{
			const float g = 0.5f * (gLo + gHi);
			if (SegmentEntry(s, d, len, lo, hi, g) >= 0.f)
				gHi = g;
			else
				gLo = g;
		}
		return hull - gHi;
	}

	struct Box
	{
		V3 lo, hi;
		int id = 0;
	};

	// The first box along the segment at this grow; -1 = none. tHit = its entry distance.
	inline int FirstBox(V3 s, V3 d, float len, float grow, const Box* boxes, int n, float& tHit)
	{
		int best = -1;
		tHit = len + 1.f;
		for (int i = 0; i < n; i++)
		{
			const float t = SegmentEntry(s, d, len, boxes[i].lo, boxes[i].hi, grow);
			if (t >= 0.f && t < tHit)
			{
				tHit = t;
				best = i;
			}
		}
		return best;
	}

	// DoSwingTraceInternal's ORDER, with players resolved analytically and the world by fraction
	// (1 = the world trace hit nothing). The hull world fraction is only read when the ray missed
	// everything, exactly like the server, so callers may pass anything there when they have not
	// run that trace yet -- check NeedsHullPass first.
	enum ESwing { SWING_MISS = -1, SWING_WORLD = -2 };
	inline bool NeedsHullPass(int rayBox, float worldRayFrac) { return rayBox < 0 && worldRayFrac >= 1.f; }
	inline int ResolveSwing(float len, int rayBox, float tRay, float worldRayFrac,
		int hullBox, float tHull, float worldHullFrac)
	{
		if (worldRayFrac < 1.f && (rayBox < 0 || tRay >= worldRayFrac * len))
			return SWING_WORLD;
		if (rayBox >= 0)
			return rayBox;
		if (hullBox >= 0 && (worldHullFrac >= 1.f || tHull < worldHullFrac * len))
			return hullBox;
		return worldHullFrac < 1.f ? SWING_WORLD : SWING_MISS;
	}

	// ---- the server's lerp and the tick it rewinds to ----------------------------------------------
	// player.cpp:3484-3515, with the client's own cvar values (they are what the userinfo carries).
	// Pass minRatio = -1 when sv_client_min_interp_ratio is -1 or missing (no ratio clamp).
	inline float ServerLerp(bool interpolate, float clInterp, float clInterpRatio, float clUpdateRate,
		float svMinRate, float svMaxRate, float svMinRatio, float svMaxRatio)
	{
		if (!interpolate)
			return 0.f;
		// Source's clamp macro, not std::clamp: defined (max wins) when a server sets min > max
		auto srcClamp = [](float x, float lo, float hi) { return x > hi ? hi : x < lo ? lo : x; };
		float rate = float(int(clUpdateRate));
		if (svMaxRate > 0.f)
			rate = srcClamp(rate, float(int(svMinRate)), float(int(svMaxRate)));
		rate = std::max(rate, 1.f);
		float ratio = clInterpRatio == 0.f ? 1.f : clInterpRatio;
		if (svMinRatio != -1.f)
			ratio = srcClamp(ratio, svMinRatio, svMaxRatio);
		return std::max(clInterp, ratio / rate);
	}

	inline int TimeToTicks(float t, float tick) { return int(0.5f + t / tick); }

	// ---- WHEN THE SERVER RUNS OUR COMMAND: MEASURED (dih-bs23) -------------------------------------
	// Up to bs22 it was guessed: tick_count + FLOW_INCOMING + FLOW_OUTGOING. Those two carry both ends'
	// HOLD time on top of the wire -- the server's wait for its next snapshot, ours for the next send --
	// and the command waits for neither. The sum lands on the snapshot that ACKS the command plus however
	// far our tick clock leads the newest snapshot: 0.4 to 1.5 ticks past the server's read, by where the
	// clock sits in its drift dead zone (TestCmdClock walks the netchan's own bookkeeping). So his yaw was
	// bet up to ~23 ms further ahead than the server reads it and every bet was scored at that moment --
	// it erred slow, not wrong: about the same front stabs, every stab ~25-105 ms later (TestStabTiming).
	// The server says when instead. A snapshot's command_ack is the last command whose RESULT is in that
	// snapshot (prediction.cpp:155 checks the predicted slot against exactly it), so
	//     off = (tick of the first snapshot that acks command n) - (n's tick_count)
	// is how long after its tick_count a command runs, on the packet clock his yaw is keyed by. Inside
	// that frame he runs before or after us (sim list order, not knowable): the read sits half a tick
	// before that snapshot.
	// That is for a command SENT on its own tick. A silent-aim stab is not: psilent chokes it, it leaves
	// with the next command's packet and runs a tick later (two when anti-cheat compatibility holds that
	// one as well). DueTime takes those ticks from the caller; CcWatch measures what a stab really waited,
	// for the log.
	// ponytail: below cl_cmdrate = tickrate the engine chokes commands of its own (stock 30: about every
	// other one, a tick late each). Not modelled -- hand the engine's send flag to the caller if it matters.
	constexpr int k_ccCmds = 128;   // command numbers remembered (power of 2), 1.9 s
	constexpr int k_ccWin  = 32;    // offsets averaged: 0.5 s of snapshots at 66/s
	constexpr int k_ccMin  = 8;     // fewer than this = not measured, the latency guess stands
	struct CmdClock
	{
		int   aNum[k_ccCmds]{}, aTick[k_ccCmds]{};   // command number -> its tick_count as created
		int   iAck = 0, iPacketTick = 0;              // the previous snapshot's
		int   aOff[k_ccWin]{}, nOff = 0, iOff = 0;
		int   iGap = 0;                               // log only: ticks between the last two snapshots
		float flOff = 0.f;                            // ticks from tick_count to the acking snapshot
		bool  bValid = false;
		int   iWatch = 0;                             // a command whose own ack is wanted (CcWatch)
		int   iWatchOff = -1;                         // ...ticks from ITS tick_count to the snapshot that acked it
	};

	// Log only: how long did THIS command (a stab) wait for its ack? iWatchOff - flOff = the ticks it was
	// held back before it was sent.
	inline void CcWatch(CmdClock& c, int num)
	{
		c.iWatch = num;
		c.iWatchOff = -1;
	}

	// Every command, BEFORE anything rewrites its tick_count (knife backtrack does).
	inline void CcCommand(CmdClock& c, int num, int tickCount)
	{
		c.aNum[num & (k_ccCmds - 1)] = num;
		c.aTick[num & (k_ccCmds - 1)] = tickCount;
	}

	// Every snapshot: its server tick and the command it acks. Only the LAST acked command is sampled --
	// the ones choked in front of it waited for its packet, it did not. Returns true when it forgot what
	// it had measured: another server or map (ticks or acks ran backwards), or a stall over a second.
	inline bool CcPacket(CmdClock& c, int packetTick, int ack)
	{
		const int gap = packetTick - c.iPacketTick;
		if (gap == 0)
			return false;   // the same snapshot twice
		if (c.iWatch && ack >= c.iWatch)
		{
			if (gap > 0 && c.aNum[c.iWatch & (k_ccCmds - 1)] == c.iWatch)
				c.iWatchOff = packetTick - c.aTick[c.iWatch & (k_ccCmds - 1)];
			c.iWatch = 0;
		}
		const bool bForget = gap < 0 || gap > 66 || ack < c.iAck;
		if (bForget)
		{	// another server, or a stall: nothing measured before it still holds
			c.nOff = c.iOff = 0;
			c.bValid = false;
		}
		else if (ack > c.iAck && c.aNum[ack & (k_ccCmds - 1)] == ack)
		{
			const int off = packetTick - c.aTick[ack & (k_ccCmds - 1)];
			if (off >= 0 && off <= 66)
			{
				c.aOff[c.iOff] = off;
				c.iOff = (c.iOff + 1) % k_ccWin;
				c.nOff = std::min(c.nOff + 1, k_ccWin);
				// interquartile mean: a lost snapshot acks late (one sided), jitter is a tick either way,
				// and a ping sitting on a frame boundary splits the window in two -- the mean of the
				// middle half is the expected frame without the strays
				int a[k_ccWin];
				std::copy(c.aOff, c.aOff + c.nOff, a);
				std::sort(a, a + c.nOff);
				const int q = c.nOff / 4;
				int sum = 0;
				for (int i = q; i < c.nOff - q; i++)
					sum += a[i];
				c.flOff = float(sum) / float(c.nOff - 2 * q);
				c.bValid = c.nOff >= k_ccMin;
			}
		}
		c.iGap = gap;
		c.iPacketTick = packetTick;
		c.iAck = ack;
		return bForget;
	}

	// TICKS A STAB IS HELD BACK BEFORE IT IS SENT -- the DLL's own two rules, replayed by TestFireDelay:
	//   ticks.cpp ManagePacket          silent aim sets G::PSilentAngles -> the stab's command is choked
	//                                   (while CanChoke) and leaves with the next one: 1 tick
	//   hooks.cpp AntiCheatCompatibility (anti-cheat compat on) on that NEXT command: its view differs from
	//                                   the stab's silent angle by > PSILENT_EPSILON and is within
	//                                   REAL_EPSILON of the view BEFORE the stab -> it copies the stab's send
	//                                   flag (choked) onto itself: 2 ticks
	// Whether that second hold happens is decided by the NEXT command's view, which does not exist yet: a
	// view that stops or turns back lands within REAL_EPSILON of the one before the stab whatever this
	// command's step was (critic round 3: reading the step instead was a tick EARLY on 25-40% of stabs
	// at ordinary sensitivity). So with compat on it is always 2 -- a tick LATE when the view keeps moving
	// or the silent angle sits within a degree of it; never early on a steady or jittery hand (late on ~1
	// stab in 4, TestFireDelay). Two rare paths it cannot see, pinned in TestFireDelay (critic round 4):
	// a flick of 10+ degrees whose landing is within 0.56 of the silent angle (snap check: 2 ticks LATE),
	// and a still view that jumps onto the silent angle the command after (3 ticks, one EARLY). Late errs
	// slow: about the same front stabs, later (a 1.5-tick-late due time costs 25-90 ms a stab,
	// TestStabTiming); a tick early doubles them at 30 ms. The knife no longer psilents (bs24): for it all
	// of this is 0.
	// ponytail: a Lua create_move script runs between the two (hooks.cpp:772) and can write send_packet;
	// not modelled -- `BS: sent fd= foff=` shows it if one does.
	constexpr float k_accEps     = 1.f / 16.f;             // hooks.cpp MATH_EPSILON
	constexpr float k_accPsilent = 1.f - k_accEps;         // hooks.cpp PSILENT_EPSILON
	constexpr float k_accReal    = 0.1f + k_accEps;        // hooks.cpp REAL_EPSILON
	// psilent = the stab sets G::PSilentAngles. The knife does not any more (bs24, aim_melee.cpp RunBackstab):
	// its silent angle goes out with its own command, 0 ticks -- TestFireDelay replays that too.
	inline int FireDelay(bool psilent, bool canChoke, bool antiCheatCompat)
	{
		if (!psilent || !canChoke)
			return 0;
		return antiCheatCompat ? 2 : 1;
	}

	// The packet-clock moment the server reads his live yaw for a command with this tick_count that is
	// held back `chokeTicks` before it is sent. `latency` (FLOW_INCOMING + FLOW_OUTGOING) is the bs22
	// guess, used until the clock has samples -- it runs 0.5-1.5 ticks late by itself, which is about
	// one choked tick, so the choke is not added to it.
	inline float DueTime(const CmdClock& c, int tickCount, float latency, float tick, int chokeTicks = 0)
	{
		return c.bValid ? (float(tickCount + chokeTicks) + c.flOff - 0.5f) * tick : float(tickCount) * tick + latency;
	}

	// ---- per-player snapshot track ------------------------------------------------------------------
	// Fed at every FRAME_NET_UPDATE_END. Holds the records BacktrackPlayer walks (sim clock), his yaw
	// on the absolute clock (packet time), whether he is choking, and a running audit of three yaw
	// estimators at the horizon they are actually used at.
	constexpr int   k_trackN     = 48;       // snapshots: 0.72s at 66/s covers lerp 0.5 + tickbase lead
	constexpr int   k_predN      = 128;      // outstanding predictions (>= horizon / command)
	constexpr int   k_est        = 3;        // hold, half rate, full rate
	constexpr float k_estFrac[k_est] = { 0.f, 0.5f, 1.f };
	// TF2's interval_per_tick: DEFAULT_TICK_INTERVAL (const.h:31), and -tickrate is refused for TF
	// (gameinterface.cpp:838). NOT 1/66 -- at tick 15000 that is 150 ticks off.
	constexpr float k_tick       = 0.015f;
	constexpr float k_rateWindow = 0.1f;     // seconds of snapshots in the turn-rate fit
	constexpr float k_priorSigma = 15.f;     // deg, a track with no audit behind it
	constexpr float k_sigmaFloor = 1.f;      // deg, the 10-bit grid and float noise
	// deg/s: a turn that STARTS inside the horizon is invisible to every estimator, and the audit
	// only ever saw the turns that already happened. Priced per second of horizon, in quadrature.
	// Not measured -- calibrate against the outcome lines (p= vs BACKSTAB / NOT A BACKSTAB).
	constexpr float k_onsetRate  = 30.f;
	// s: WHEN the server reads him is only known to about a tick -- half a tick of player order in
	// the server frame, a frame of execution delay, latency jitter. The audit cannot see it (its truth
	// is indexed by the same estimated time), so it is priced as |rate| x this, in quadrature.
	constexpr float k_timeSigma  = k_tick;
	// Two memories of the error: FAST (~5 scored bets, ~75 ms) reacts to a man who just started
	// turning, SLOW (~20, ~0.3 s) remembers that he flicks. The larger one is used -- a quiet 75 ms
	// is not proof of a quiet player. FAST starts at the prior (a fresh track is cautious); SLOW
	// starts empty and is bias-corrected, so it never drags the prior along for half a second.
	constexpr float k_errFast    = 0.2f;
	constexpr float k_errSlow    = 0.05f;
	// A turn in progress whose errors are still in flight (bets not due yet) is uncertain before the
	// audit can say so: the estimators disagree by |rate| x horizon, a quarter of that is charged.
	constexpr float k_rateSpread = 0.25f;
	constexpr float k_maxHorizon = 0.6f;     // s, past this his yaw is a coin toss anyway
	constexpr float k_breakGap   = 1.0f;     // s between snapshots = a different life
	constexpr float k_teleport2D = 64.f;     // sv_lagcompensation_teleport_dist (lagcomp.cpp:28, :480)
	constexpr float k_unknown    = 1000.f;   // Rewind extrap sentinel: where the server puts him cannot be priced -> never swing at him

	struct Track
	{
		bool  bLive = false;
		int   iHead = 0, iCount = 0;         // newest sample at iHead - 1
		float aT[k_trackN]{};                // simulation time (his tickbase clock) -- Rewind
		float aTy[k_trackN]{};               // packet server time (absolute clock) -- yaw
		float aY[k_trackN]{};                // unwrapped yaw
		V3    aP[k_trackN]{}, aMin[k_trackN]{}, aMax[k_trackN]{};
		bool  aChoked[k_trackN]{};           // packets came in without him before this record

		float flLastPacket = -1.f;           // newest packet time this track was offered
		int   iDupRun = 0;                   // packets since the newest record that did not carry him

		int   iPredHead = 0, iPredTail = 0;
		float aDue[k_predN]{}, aHor[k_predN]{};
		float aPred[k_predN][k_est]{};
		float aBet[k_predN]{};               // the bet itself: the estimator's prediction that was best then
		signed char aBetBin[k_predN]{}, aBetDir[k_predN]{};   // his motion state when it was made (ResidTable)

		float aMsFast[k_est]{}, aMsSlow[k_est]{};   // EMAs of squared error per estimator, deg^2
		float flHorRef = 0.f;                // EMA of the horizon those errors were scored at
		int   iScored = 0;
	};

	inline int Idx(const Track& tr, int back) { return (tr.iHead - 1 - back + 2 * k_trackN) % k_trackN; }

	inline void Reset(Track& tr)
	{
		tr.bLive = false;
		tr.iHead = tr.iCount = 0;
		tr.iPredHead = tr.iPredTail = 0;
		for (int k = 0; k < k_est; k++)
		{
			tr.aMsFast[k] = k_priorSigma * k_priorSigma;
			tr.aMsSlow[k] = 0.f;
		}
		tr.flLastPacket = -1.f;
		tr.iDupRun = 0;
		tr.flHorRef = 0.f;
		tr.iScored = 0;
	}

	// He is choking right now: the server has no record of him newer than our newest either.
	inline bool Choking(const Track& tr) { return tr.iCount && (tr.iDupRun > 0 || tr.aChoked[Idx(tr, 0)]); }

	// Least squares slope over the last k_rateWindow seconds of the yaw clock, deg/s. Two samples minimum.
	inline float Rate(const Track& tr)
	{
		if (tr.iCount < 2)
			return 0.f;
		const float tNew = tr.aTy[Idx(tr, 0)];
		int n = 0;
		float sx = 0.f, sy = 0.f;
		for (int b = 0; b < tr.iCount; b++)
		{
			const int i = Idx(tr, b);
			if (b >= 2 && tNew - tr.aTy[i] > k_rateWindow)
				break;
			sx += tr.aTy[i] - tNew; sy += tr.aY[i]; ++n;
		}
		const float mx = sx / n, my = sy / n;
		float num = 0.f, den = 0.f;
		for (int b = 0; b < n; b++)
		{
			const int i = Idx(tr, b);
			const float x = tr.aTy[i] - tNew - mx;
			num += x * (tr.aY[i] - my);
			den += x * x;
		}
		return den > 1e-9f ? num / den : 0.f;
	}

	// Mean velocity over the rate window (end to end, simulation clock = his motion's own), units/s.
	inline V3 Velocity(const Track& tr)
	{
		if (tr.iCount < 2)
			return {};
		const int iNew = Idx(tr, 0);
		int iOld = Idx(tr, 1);
		for (int b = 1; b < tr.iCount; b++)
		{
			const int i = Idx(tr, b);
			iOld = i;
			if (tr.aT[iNew] - tr.aT[i] >= k_rateWindow)
				break;
		}
		const float dt = tr.aT[iNew] - tr.aT[iOld];
		return dt > 1e-4f ? (tr.aP[iNew] - tr.aP[iOld]) * (1.f / dt) : V3{};
	}

	inline void Predict(const Track& tr, float tDue, float out[k_est])
	{
		const float y = tr.aY[Idx(tr, 0)];
		const float h = std::clamp(tDue - tr.aTy[Idx(tr, 0)], 0.f, k_maxHorizon);
		const float r = Rate(tr);
		for (int k = 0; k < k_est; k++)
			out[k] = y + k_estFrac[k] * r * h;
	}

	inline float MeasuredMs(const Track& tr, int k)
	{
		const float slow = tr.iScored ? tr.aMsSlow[k] / (1.f - std::pow(1.f - k_errSlow, float(std::min(tr.iScored, 400)))) : 0.f;
		return std::max(tr.aMsFast[k], slow);
	}

	// the estimator the audit trusts most on this player right now (half rate until it has spoken)
	inline int BestEst(const Track& tr)
	{
		int best = 1;
		if (tr.iScored)
			for (int k = 0; k < k_est; k++)
				if (MeasuredMs(tr, k) < MeasuredMs(tr, best))
					best = k;
		return best;
	}

	// ---- WHERE THE BETS REALLY LAND (dih-bs23) -------------------------------------------------------
	// bs22 priced his yaw as a Gaussian round the bet, sigma = the recent RMS error. Closed loop that
	// number does not sort swings (lab, 75 ms, humans): fires at p .7-.8 landed 97.6%, fires at p > .999
	// only 98.6%. Two reasons, both structural:
	//   * the error is a MIXTURE -- nothing at all while he holds still, tens of degrees the moment a
	//     flick starts -- and an RMS is neither. |z| > 3 came up 4.4% of the time, not 0.27%
	//   * the RMS is scored a full horizon late, so it is high AFTER a flick (he has stopped) and low
	//     BEFORE the next one
	// What does tell them apart is visible in the last few snapshots: is he still, moving, speeding up,
	// slowing down. So every bet is filed under that MOTION STATE, and when its moment arrives the
	// residual (truth - bet, signed along his motion) goes into that state's histogram. The odds are
	// then the mass of that histogram inside the server's window (StabMix) -- how bets made in this
	// state on this server at this ping have actually landed, tails and one-sidedness included.
	// One table for all players: a state fills from everyone at once (a flick's onset is ~3 ticks per
	// player per flick; alone it would take minutes).
	// MEASURED MEANS MEASURED. The table is believed only for a bet it has data for (TurnGuarded bMix):
	//   * a readable motion state -- not a choker (his snapshots say nothing about now), not a gap
	//   * with k_resTrust scored bets in it (a thinner one borrows every state's, pooled, once the table
	//     as a whole has four times that)
	//   * at the horizon those bets were scored at (ResidOnHorizon). Residuals are plain degrees: a turn
	//     has twice the time at twice the ping, and no rescaling of a histogram says where it gets to
	//     (the first cut scaled linearly and folded a 150-degree snap inside the window at 30 ms ping)
	// Everything else is bs22 to the letter: the Gaussian, the turn guard, and the rule they were tuned
	// on (Decide, measured = false). Nothing is seeded -- the first seconds of a map ARE bs22, until the
	// players in view have filled the states. A ping that moves empties the table (ResidAdd) and it
	// refills at the new horizon.
	constexpr int   k_resSpeeds = 6, k_resTrends = 3, k_resBins = k_resSpeeds * k_resTrends;
	constexpr float k_resSpeedEdge[k_resSpeeds - 1] = { 12.f, 30.f, 80.f, 200.f, 500.f };   // deg/s; 12 = 1 step of the 10-bit grid in 45 ms
	constexpr float k_resSpan   = 0.04f;    // s of snapshots a speed is measured over
	constexpr float k_resMemory = 3000.f;   // samples a state remembers (a rare tail needs the count)
	constexpr float k_resTrust  = 150.f;    // samples before a state's histogram is believed
	constexpr int   k_resOffMax = 600;      // off-horizon bets (net of on-horizon ones) that mean the ping moved

	struct ResidTable
	{
		float n[k_resBins][k_resCells]{};
		float N[k_resBins]{};
		float flHor = 0.f;   // the horizon its bets were scored at, s (EMA); 0 = empty
		int   iOff = 0;      // bets lately that were NOT at that horizon, net of those that were
	};

	// a bet this table can speak for: within a quarter + a tick of the horizon its own were scored at
	inline bool ResidOnHorizon(const ResidTable& t, float horizon)
	{
		return t.flHor > 0.f && std::fabs(horizon - t.flHor) <= 0.25f * t.flHor + k_tick;
	}

	// His motion state from the newest snapshots (yaw clock): speed class of the last k_resSpan x trend
	// against the k_resSpan before it (0 steady, 1 speeding up / reversed, 2 slowing down). dir = the way
	// he is turning (+1 when it cannot be told). -1 = no state to read: too few records, a gap, or a man
	// who is choking hard -- his newest snapshot is where he WAS, and "still" is the one thing it does not show.
	// Hard = two packets without him, or a newest record that came as a batch of three ticks or more. One
	// skipped packet is not: a 33 fps client sends like that, and so does every man standing still when
	// AddSnapshot folds a sim-time rollover away (twice per 100 ticks) -- with plain Choking() those ticks
	// had no state, went to bs22's looser rule and swung early (16% of all fires at 150 ms in the closed loop).
	// A client under ~30 fps (3-tick batches) IS hard: bs22's odds for him (TestMeasuredOdds, low fps).
	inline bool ChokingHard(const Track& tr)
	{
		if (!tr.iCount)
			return false;
		if (tr.iDupRun >= 2)
			return true;
		return tr.iCount >= 2 && tr.aChoked[Idx(tr, 0)] && tr.aT[Idx(tr, 0)] - tr.aT[Idx(tr, 1)] > 2.5f * k_tick;
	}
	inline int MotionBin(const Track& tr, int& dir)
	{
		dir = 1;
		if (!tr.bLive || tr.iCount < 2 || ChokingHard(tr))
			return -1;
		const int i0 = Idx(tr, 0);
		int b1 = 1;
		while (b1 < tr.iCount - 1 && tr.aTy[i0] - tr.aTy[Idx(tr, b1)] < k_resSpan - 1e-4f)
			++b1;
		const int i1 = Idx(tr, b1);
		const float dt1 = tr.aTy[i0] - tr.aTy[i1];
		if (dt1 <= 1e-4f || dt1 > 2.f * k_resSpan)
			return -1;   // a gap: nothing recent to read a motion from
		const float v1 = (tr.aY[i0] - tr.aY[i1]) / dt1;
		float v0 = v1;
		int b2 = b1 + 1;
		while (b2 < tr.iCount - 1 && tr.aTy[i1] - tr.aTy[Idx(tr, b2)] < k_resSpan - 1e-4f)
			++b2;
		if (b2 < tr.iCount)
		{
			const int i2 = Idx(tr, b2);
			const float dt0 = tr.aTy[i1] - tr.aTy[i2];
			if (dt0 > 1e-4f && dt0 <= 2.f * k_resSpan)
				v0 = (tr.aY[i1] - tr.aY[i2]) / dt0;
		}
		const float a1 = std::fabs(v1), a0 = v0 * v1 > 0.f ? std::fabs(v0) : 0.f;   // a reversal = a start from rest
		int s = 0;
		while (s < k_resSpeeds - 1 && a1 >= k_resSpeedEdge[s])
			++s;
		const int trend = a1 > 1.5f * a0 + 10.f ? 1 : a1 < 0.67f * std::fabs(v0) - 5.f ? 2 : 0;
		dir = v1 > 0.f ? 1 : v1 < 0.f ? -1 : v0 < 0.f ? -1 : 1;
		return s * k_resTrends + trend;
	}

	inline int ResidCell(float r)
	{
		int c = 0;
		while (c < k_resCells - 1 && r >= k_resEdge[c + 1])
			++c;
		return c;
	}

	// One scored bet: resid = (truth - bet) x dir, degrees, made `horizon` s ahead. bin < 0 (no readable
	// state when it was made) is not filed.
	inline void ResidAdd(ResidTable& t, int bin, float resid, float horizon)
	{
		if (bin < 0 || bin >= k_resBins)
			return;
		if (t.flHor <= 0.f)
			t.flHor = horizon;
		if (ResidOnHorizon(t, horizon))
			t.iOff = std::max(t.iOff - 1, 0);
		else if (++t.iOff <= k_resOffMax)
			return;   // somebody's long reach (he choked, we hitched): not this table's bet
		else
		{	// they keep coming: the ping moved. What was learned at the old horizon is not about this one
			t = {};
			t.flHor = horizon;
		}
		t.flHor += 0.01f * (horizon - t.flHor);
		const float keep = 1.f - 1.f / k_resMemory;
		for (int c = 0; c < k_resCells; c++)
			t.n[bin][c] *= keep;
		t.n[bin][ResidCell(Wrap(resid))] += 1.f;   // an angle: a spin's +200 lands where -160 does
		t.N[bin] = t.N[bin] * keep + 1.f;
	}

	// Once per FRAME_NET_UPDATE_END: tSim = his m_flSimulationTime, tPacket = the packet's server time.
	// Returns false when it carried nothing new of him (duplicate simulation time). pTab (optional)
	// collects where each scored bet landed.
	inline bool AddSnapshot(Track& tr, float tSim, float tPacket, float yaw, V3 origin, V3 mins, V3 maxs, ResidTable* pTab = nullptr)
	{
		if (tr.bLive && tr.iCount)
		{
			const int iNew = Idx(tr, 0);
			const float dt = tSim - tr.aT[iNew];
			// SIMULATION TIME ROLLOVER: a sim tick older than the packet's network base is sent as 0
			// (baseentity.cpp:136-145) and decodes to the base itself -- a multiple of 100 ticks
			// (globalvars_base.h:95-99, c_baseentity.cpp:354-369). A man choking across that boundary
			// arrives as a "new" record with his old origin and yaw. It is the same record.
			// (and the client's +-127 re-centring can push that base 256 ticks ahead, :363-366)
			const int iSimTick = TimeToTicks(tSim, k_tick);
			const bool bRollover = dt > 0.f && (iSimTick % 100 == 0 || (iSimTick - 256) % 100 == 0) && origin.x == tr.aP[iNew].x
				&& origin.y == tr.aP[iNew].y && origin.z == tr.aP[iNew].z && Wrap(yaw - tr.aY[iNew]) == 0.f;
			if (dt == 0.f || bRollover)
			{	// a packet came and he was not simulated in it: that is a choke, not a low update rate
				if (tPacket > tr.flLastPacket)
				{
					tr.flLastPacket = tPacket;
					++tr.iDupRun;
				}
				return false;
			}
			// time ran backwards (respawn / map change / a new player in the slot), a long gap, or a
			// jump no movement can make: none of the history is the same motion any more
			const bool bBreak = dt < 0.f || dt > k_breakGap || tPacket < tr.aTy[iNew] - k_breakGap
				|| Len2D(origin - tr.aP[iNew]) > 3500.f * dt + 64.f;
			if (bBreak)
				Reset(tr);
		}
		if (!tr.bLive)
			Reset(tr);

		const bool bChoked = tr.iDupRun > 0;
		const float tPacketReal = tPacket;   // flLastPacket must only ever see the engine's clock
		float yU = yaw;
		if (tr.iCount)
		{
			const int iPrev = Idx(tr, 0);
			const float tPrev = tr.aTy[iPrev], yPrev = tr.aY[iPrev];
			// the yaw clock must advance between records; if the packet clock did not, his own sim
			// step is the best measure of how much time passed
			if (tPacket <= tPrev)
				tPacket = tPrev + std::max(tSim - tr.aT[iPrev], k_tick);
			yU = yPrev + Wrap(yaw - yPrev);   // continuous: no 359 -> 0 step in the fit

			// Score every prediction whose moment has now been observed. Normally against the line
			// between the two records: the server's pl.v_angle steps once a tick, but our command also
			// runs on an integer tick with half a tick of player order either way, so the line is the
			// unbiased truth (a step would teach the audit to under-extrapolate every smooth turn).
			// After a CHOKE the server really held the old yaw until his batch ran, so it is a step.
			while (tr.iPredTail != tr.iPredHead && tr.aDue[tr.iPredTail] <= tPacket)
			{
				const float due = tr.aDue[tr.iPredTail];
				const float truth = due <= tPrev ? yPrev
					: bChoked ? (due < tPacket ? yPrev : yU)
					: yPrev + (yU - yPrev) * ((due - tPrev) / (tPacket - tPrev));
				for (int k = 0; k < k_est; k++)
				{
					const float e = tr.aPred[tr.iPredTail][k] - truth;
					tr.aMsFast[k] += k_errFast * (e * e - tr.aMsFast[k]);
					tr.aMsSlow[k] += k_errSlow * (e * e - tr.aMsSlow[k]);
				}
				if (pTab)
					ResidAdd(*pTab, tr.aBetBin[tr.iPredTail], (truth - tr.aBet[tr.iPredTail]) * float(tr.aBetDir[tr.iPredTail]), tr.aHor[tr.iPredTail]);
				tr.flHorRef = tr.iScored ? tr.flHorRef + k_errFast * (tr.aHor[tr.iPredTail] - tr.flHorRef)
					: tr.aHor[tr.iPredTail];
				++tr.iScored;
				tr.iPredTail = (tr.iPredTail + 1) % k_predN;
			}

			// a spinner's unwrapped yaw must not walk off float precision: re-centre everything
			if (std::fabs(yU) > 3600.f)
			{
				const float shift = 360.f * std::floor(yU / 360.f);
				yU -= shift;
				for (int b = 0; b < tr.iCount; b++)
					tr.aY[Idx(tr, b)] -= shift;
				for (int p = tr.iPredTail; p != tr.iPredHead; p = (p + 1) % k_predN)
				{
					for (int k = 0; k < k_est; k++)
						tr.aPred[p][k] -= shift;
					tr.aBet[p] -= shift;
				}
			}
		}

		tr.aT[tr.iHead] = tSim; tr.aTy[tr.iHead] = tPacket; tr.aY[tr.iHead] = yU;
		tr.aP[tr.iHead] = origin; tr.aMin[tr.iHead] = mins; tr.aMax[tr.iHead] = maxs;
		tr.aChoked[tr.iHead] = bChoked;
		tr.iHead = (tr.iHead + 1) % k_trackN;
		tr.iCount = std::min(tr.iCount + 1, k_trackN);
		tr.flLastPacket = std::max(tr.flLastPacket, tPacketReal);
		tr.iDupRun = 0;
		tr.bLive = true;
		return true;
	}

	// Once per command: bet on his yaw at the moment the server will read it (absolute clock).
	inline void PushPrediction(Track& tr, float tDue)
	{
		if (!tr.bLive || !tr.iCount)
			return;
		const int iLast = (tr.iPredHead + k_predN - 1) % k_predN;
		if (tr.iPredHead != tr.iPredTail && tr.aDue[iLast] >= tDue)
			return;   // same command twice, or the clock stepped back: keep the ring in time order
		Predict(tr, tDue, tr.aPred[tr.iPredHead]);
		tr.aBet[tr.iPredHead] = tr.aPred[tr.iPredHead][BestEst(tr)];
		int dir;
		tr.aBetBin[tr.iPredHead] = (signed char)MotionBin(tr, dir);
		tr.aBetDir[tr.iPredHead] = (signed char)dir;
		tr.aDue[tr.iPredHead] = tDue;
		tr.aHor[tr.iPredHead] = std::clamp(tDue - tr.aTy[Idx(tr, 0)], 0.f, k_maxHorizon);
		tr.iPredHead = (tr.iPredHead + 1) % k_predN;
		if (tr.iPredHead == tr.iPredTail)
			tr.iPredTail = (tr.iPredTail + 1) % k_predN;   // full: drop the oldest unscored bet
	}

	struct YawEstimate
	{
		float centre = 0.f, sigma = k_priorSigma, rate = 0.f, horizon = 0.f;
		float seen = 0.f;                   // his yaw in the newest snapshot -- what he has actually been seen doing
		float sigmaCore = k_priorSigma;     // sigma without the in-flight spread term (TurnGuarded prices that itself)
		int   k = 1;
		int   bin = -1, dir = 1;            // his motion state now (MotionBin): which residual histogram the bet draws on; -1 none
	};

	inline YawEstimate Estimate(const Track& tr, float tDue)
	{
		YawEstimate e;
		if (!tr.bLive || !tr.iCount)
			return e;
		float pred[k_est];
		Predict(tr, tDue, pred);
		const int best = BestEst(tr);
		e.k = best;
		e.bin = MotionBin(tr, e.dir);
		e.centre = Wrap(pred[best]);
		e.seen = Wrap(pred[0]);   // estimator 0 = hold = the newest snapshot's yaw
		e.rate = Rate(tr);
		e.horizon = std::clamp(tDue - tr.aTy[Idx(tr, 0)], 0.f, k_maxHorizon);
		// errors were measured at flHorRef; a longer reach (he started choking) is proportionally worse
		const float stretch = tr.iScored && tr.flHorRef > 1e-3f ? std::max(1.f, e.horizon / tr.flHorRef) : 1.f;
		const float onset = k_onsetRate * e.horizon;
		const float timing = std::fabs(e.rate) * k_timeSigma;
		const float spread = k_rateSpread * std::fabs(e.rate) * e.horizon;
		e.sigmaCore = std::sqrt(MeasuredMs(tr, best) * stretch * stretch + k_sigmaFloor * k_sigmaFloor
			+ onset * onset + timing * timing);
		e.sigma = std::sqrt(e.sigmaCore * e.sigmaCore + spread * spread);
		return e;
	}

	// THE TURN GUARD (dih-bs16). bs15's first live log (108 swings, Legit, ~0.1 s horizon): the
	// swings whose bet extrapolated a turn at least as big as the bet's margin landed 13 of 22 and
	// made 9 of the 12 front stabs; every other swing landed 78 of 81. (`margin` is unsigned, so
	// that log cannot say which of them were turning INTO the window -- the ones this guard binds
	// on. bs16 logs gm=, the signed guard margin, so the next log can.) The audit's RMS cannot see
	// the failure: a flick's error is one sided (he STOPS, usually early), and the stop is exactly
	// the moment the swing is judged. So a swing must ALSO be a backstab at the guard point: the
	// yaw last seen plus only k_turnTrust of the turn the bet assumes (Aletherium's
	// BackstabDoubleTest made the all-or-nothing version of this call). A still target is
	// untouched (seen == bet); a turn INTO the window waits until 40% of it would already do; a
	// turn out of it was refused by the bet anyway; a spinner still gets the long stretch where
	// both are in.
	// k_turnTrust = 0.4 from a closed-loop sweep (0 .. 0.6, 30/80/150 ms ping; the bs15-vs-bs16 half
	// of it is TestTurnGuardClosedLoop): 0.4 fronts as rarely as trusting NONE of the turn and
	// swings ~50 ms sooner; 0.5 already leaks fronts. Physics agrees: a min-jerk flick at peak
	// speed has 0.3-0.7 of speed x horizon left to turn. Calibrate from gm= / sc= on the outcomes.
	// ponytail: KNOWN CEILING -- a spy checker (180 away, a short look, 180 back) at 150+ ms ping
	// shows his back for less than the horizon; bs15's early bet caught a few more of those and
	// bs16 fronts slightly more on them (the test bounds it). No variant in the sweep did better
	// there; a per-player "how long does he hold" model is the upgrade if logs show it matters.
	// The guard point is judged on sigmaCore: the spread term priced estimator disagreement, and
	// that is what the guard now prices explicitly (with it, a spinner at 150 ms ping was locked
	// out for ~0.8 s in the sweep).
	// `seenLead` s: judging a later tick, the snapshots that arrive by then will have shown the turn
	// carried on at the best estimator's share of the rate. JudgeKnife passes k_tick for its
	// next-tick odds, and 0 while he chokes (no snapshot will show anything new).
	constexpr float k_turnTrust = 0.4f;
	struct GuardedOdds
	{
		StabOdds best;             // the odds on the best estimator's yaw (bs15's number)
		float pGuard = 0.f;        // the odds at the guard point
		float guardMargin = 0.f;   // degrees inside his yaw window at the guard point (< 0 = outside)
		float p = 0.f;             // what the swing is decided on: the measured odds (bMix), else the smaller
		bool  bMix = false;        // the odds are MEASURED (ResidTable); Decide's bands go with it
	};
	// pTab (bs23): where it has something measured for this bet (bMix: a readable state, enough scored
	// bets, the horizon they were scored at) the odds are that state's histogram inside the window and
	// nothing else -- its one-sided stops are IN it, the guard would only count them twice. Everywhere
	// else it is bs22: the Gaussian and the guard.
	inline GuardedOdds TurnGuarded(float bearing, float ownYaw, const YawEstimate& e, float sigBear, float seenLead = 0.f,
		const ResidTable* pTab = nullptr)
	{
		GuardedOdds g;
		g.best = StabProbability(bearing, ownYaw, e.centre, e.sigma, sigBear);
		const float* pCells = nullptr;
		float flCells = 0.f, aPool[k_resCells];
		if (pTab && e.bin >= 0 && e.bin < k_resBins && ResidOnHorizon(*pTab, e.horizon))
		{
			if (pTab->N[e.bin] >= k_resTrust)
			{
				pCells = pTab->n[e.bin];
				flCells = pTab->N[e.bin];
			}
			else
			{	// a thin state on a live table: every state's bets pooled stand in for it (wide, so cautious).
				// A guess here would go to bs22's looser rule, and the swings the bands hold back leak out
				// through whichever state happens to be rare (rare now: 2.7% of fires at 150 ms ping, which
				// without the pool front 245 times for 212 -- critic round 2)
				for (int c = 0; c < k_resCells; c++)
					aPool[c] = 0.f;
				for (int b = 0; b < k_resBins; b++)
				{
					flCells += pTab->N[b];
					for (int c = 0; c < k_resCells; c++)
						aPool[c] += pTab->n[b][c];
				}
				if (flCells >= 4.f * k_resTrust)
					pCells = aPool;
			}
		}
		g.bMix = pCells != nullptr;
		if (g.bMix && g.best.hi > g.best.lo)
			g.best.p = StabMix(Wrap(ownYaw - bearing), g.best.r, e.sigma, sigBear, pCells, flCells, float(e.dir));
		// unwrapped, as Predict built the bet: a spin can carry the bet past 180 degrees from seen.
		// The lead is capped at the horizon: a due time that close to his newest packet (zero-ping
		// listen server) has no newer snapshot coming before it, and the guard must stay between
		// seen and the bet.
		const float flLead = std::min(seenLead, e.horizon);
		const float flSeen = e.seen + k_estFrac[e.k] * e.rate * flLead;
		const float flTurn = k_estFrac[e.k] * e.rate * (e.horizon - flLead);
		const StabOdds og = StabProbability(bearing, ownYaw, Wrap(flSeen + k_turnTrust * flTurn), e.sigmaCore, sigBear);
		g.pGuard = og.p;
		g.guardMargin = og.margin;
		g.p = g.bMix ? g.best.p : std::min(g.best.p, g.pGuard);
		return g;
	}

	// BacktrackPlayer (lagcomp.cpp:440-538) on our copy of his records, simulation clock:
	//   * walked newest -> oldest; a 2D step over 64u between records before the target time means
	//     the server gives up and leaves him at his LIVE origin (lagcomp.cpp:479-484). We do not have
	//     that: the newest record, priced as a full maxExtrap guess
	//   * target newer than every record, not choking: the server has records we have not received,
	//     extrapolated along his velocity (capped at maxExtrap) and the gap priced
	//   * target newer than every record, CHOKING: his next batch either has not landed (he is on the
	//     newest record) or has (he is up to v x gap along). Centred halfway, the half priced, and the
	//     gap NOT capped -- a long choke is a big error, and the caller's reach pad refuses it
	//   * older than every record we hold: the oldest (a guess -- flagged)
	// `extrap` is how far (s) the answer may be from what the server uses; the caller prices it.
	inline bool RewindCore(const Track& tr, float T, float maxExtrap, V3& origin, V3& mins, V3& maxs, float& extrap)
	{
		extrap = 0.f;
		if (!tr.bLive || !tr.iCount)
			return false;
		const int iNew = Idx(tr, 0);
		auto fNewest = [&]()
		{
			origin = tr.aP[iNew]; mins = tr.aMin[iNew]; maxs = tr.aMax[iNew];
			return true;
		};
		if (T >= tr.aT[iNew])
		{
			fNewest();
			const float gap = T - tr.aT[iNew];
			if (Choking(tr))
			{
				origin = origin + Velocity(tr) * (0.5f * gap);
				extrap = gap;
				// batches over 64u: once his next one lands the server does not lerp, it leaves him at
				// the whole batch away (lagcomp.cpp:479-484) -- price the full batch, which refuses
				if (tr.iCount >= 2 && Len2D(tr.aP[iNew] - tr.aP[Idx(tr, 1)]) > k_teleport2D)
					extrap = std::max(extrap, 2.f * (tr.aT[iNew] - tr.aT[Idx(tr, 1)]));
			}
			else
			{
				origin = origin + Velocity(tr) * std::min(gap, maxExtrap);
				// past the cap the guess stops moving him but the error keeps growing: price all of it
				extrap = gap <= maxExtrap ? gap : 2.f * gap;
			}
			return true;
		}
		for (int b = 1; b < tr.iCount; b++)
		{
			const int i = Idx(tr, b), iNext = Idx(tr, b - 1);
			// The server keeps a record per tick; we keep one per packet. Only a gap the server has too
			// -- consecutive ticks, or a batch after a choke -- can trip its 64u test.
			const bool bServerGap = tr.aT[iNext] - tr.aT[i] <= 1.5f * k_tick || tr.aChoked[iNext];
			if (bServerGap && Len2D(tr.aP[i] - tr.aP[iNext]) > k_teleport2D)
			{	// "lost track, too much difference": he stays at his live origin, which may be a whole
				// batch past our newest -- priced as that batch
				fNewest();
				extrap = std::max(maxExtrap, 2.f * (tr.aT[iNext] - tr.aT[i]));
				return true;
			}
			if (tr.aT[i] <= T)
			{
				const float f = tr.aT[i] < T ? (T - tr.aT[i]) / (tr.aT[iNext] - tr.aT[i]) : 0.f;
				origin = tr.aP[i] + (tr.aP[iNext] - tr.aP[i]) * f;
				mins = tr.aMin[i] + (tr.aMin[iNext] - tr.aMin[i]) * f;
				maxs = tr.aMax[i] + (tr.aMax[iNext] - tr.aMax[i]) * f;
				return true;
			}
		}
		// Older than every record we hold -- he just came out of dormancy (Reset) while the server kept
		// his history. That history may hold a 64u step or a death, where the server leaves him at his
		// LIVE origin instead (lagcomp.cpp:473-484), so no walk-back from here can be priced honestly.
		// Unknown: refused until the rewind tick is inside our records (lerp + his lead, ~0.1-0.2 s).
		const int iOld = Idx(tr, tr.iCount - 1);
		origin = tr.aP[iOld]; mins = tr.aMin[iOld]; maxs = tr.aMax[iOld];
		extrap = k_unknown;
		return true;
	}

	// RewindCore plus two things a record walk cannot see:
	//   * one record has no velocity: every extrapolation from it is unpriced -> unknown
	//   * a choker's next BATCH may have reached the server already, and the server's 64u test is per
	//     record step (lagcomp.cpp:479-484) -- one batch, not the whole time since. His next batch is
	//     predicted as long as his last choked one (or the choke running now, if longer); if that one
	//     step can carry him over 64u and has had time to land (half a batch since our newest record
	//     reached us), the server may leave him at a live origin that far away -> priced as the whole
	//     move. A 30 fps client's 3-tick batches (14u) never trip it; a 14-tick fakelag does.
	// tDue < 0 skips the choke test.
	inline bool Rewind(const Track& tr, float T, float maxExtrap, V3& origin, V3& mins, V3& maxs, float& extrap, float tDue = -1.f)
	{
		if (!RewindCore(tr, T, maxExtrap, origin, mins, maxs, extrap))
			return false;
		if (tr.iCount < 2)
		{
			extrap = k_unknown;
			return true;
		}
		if (tDue < 0.f)
			return true;
		const int iNew = Idx(tr, 0), iPrev = Idx(tr, 1);
		const float span = std::max(tr.aChoked[iNew] ? tr.aT[iNew] - tr.aT[iPrev] : 0.f, (tr.iDupRun + 1) * k_tick);
		const float stepMove = Len2D(Velocity(tr)) * span + 1500.f * span * span;
		const float age = tDue - tr.aTy[iNew];
		if (stepMove > k_teleport2D && age >= 0.5f * span)
			extrap = std::max(extrap, 2.f * age);
		return true;
	}

	// What the caller's reach pad must cover for a box rewound with `extrap`: the half-step of his
	// velocity the guess can be off by, plus what 3000 u/s^2 of ground acceleration (sv_accelerate 10
	// x 300) bends a path by over that time, 1/2 a t^2 -- a man standing still when the last record
	// left can be moving.
	inline float PositionError(float speed, float extrap, float floor)
	{
		return floor + 0.5f * speed * extrap + 1500.f * extrap * extrap;
	}

	// BEARING TIMING (dih-bs20). The bearing the server reads is between OUR post-move centre (a
	// client prediction: a body-block on his live box, which our prediction collides with a stale
	// copy of, moves it) and his rewound one. Off by a few ms of relative motion it is the same
	// geometry a little earlier or later, so the line to him is priced |its turn rate| x
	// k_bearTime on top of the position term, in quadrature. Nothing at range: a side-on pass at
	// 18u turns the line ~2000 deg/s, at 65u ~500. bs19 log: the one front of 19 swings was that
	// 18u pass, margin 3.8 at p=0.999; with this it is p=0.67 (refused, next tick is deeper behind).
	// ponytail: k_bearTime fit to ONE front (0.25 tick lets it through, 0.4 refuses a good stab) --
	// recalibrate on bear= / brate= of STAB lines vs NOT A BACKSTAB as fronts come in.
	constexpr float k_bearTime = 0.3f * k_tick;
	// rel = his centre - ours (2D used), vRel = his velocity - ours, posErr = PositionError's
	inline float BearingSigma(V3 rel, V3 vRel, float posErr)
	{
		const float d2 = std::max(rel.x * rel.x + rel.y * rel.y, 1.f);
		const float pos = Deg(std::atan2(posErr, std::sqrt(d2)));
		const float turn = Deg((rel.x * vRel.y - rel.y * vRel.x) / d2) * k_bearTime;   // deg/s x s
		return std::sqrt(pos * pos + turn * turn);
	}

	// ---- THE REWIND WINDOW (dih-bs18, knife backtrack) ---------------------------------------------
	// Lag compensation puts every player where OUR tick_count says (targettick = tick_count -
	// lerpTicks), but his yaw stays LIVE. So asking for an older tick moves his box (reach, bearing)
	// and nothing else -- the knife can stab where he was a few ticks ago. The server only takes the
	// ask while (lagcomp.cpp:396-412)
	//     |correct - (its tick - targettick)| <= 0.2 s,   correct = clamp(its outgoing latency + lerp)
	// and past that rewinds to its own guess (tick - correct) -- the default box, the one we just
	// judged and did not like. ServerTakesRewind is that test verbatim (the tests' ground truth).
	constexpr float k_unlagWindow = 0.2f;
	// Our side of it is an estimate: its tick is tDue (half a tick before the snapshot that acks us,
	// bs23) give or take jitter, and its latency is not quite ours. 3 ticks of the 13 the window has.
	constexpr float k_btSafety = 0.045f;
	constexpr int   k_btMax    = 12;

	inline bool ServerTakesRewind(int cmdTick, int lerpTicks, int serverTick, float correct, float tick)
	{
		const int targettick = cmdTick - lerpTicks;
		const float deltaTime = correct - float(serverTick - targettick) * tick;
		return !(std::fabs(deltaTime) > k_unlagWindow);
	}

	// How many ticks back from the default rewind (tRewind0) the server will still take. Each tick
	// back moves deltaTime down by one tick; 0 = only the default.
	inline int RewindBackTicks(float tDue, float tRewind0, float correct, float tick, int maxBack = k_btMax)
	{
		const float delta0 = correct - (tDue - tRewind0);
		const float room = delta0 + (k_unlagWindow - k_btSafety);
		if (room <= 0.f)
			return 0;
		return std::min(maxBack, int(room / tick + 1e-4f));
	}

	// ---- when to swing ----------------------------------------------------------------------------
	// dih-bs23. On MEASURED odds (TurnGuarded bMix) p means what it says, and the rule is three bands:
	//   p >= k_pSure            swing
	//   k_pTake <= p < k_pSure  a chancy stab: k_patience ticks to become a sure one, then it is taken
	//                           (at once when the reach is about to go)
	//   p < k_pTake             no swing
	// Swept in a closed-loop lab over floor .80-.90 x sure .97-.99 x wait 4-12 on two mixed servers,
	// scored 4 x fronts + chances lost: these 313, bs22's rule on bs22's odds 578. What the shipped pair
	// does is TestMeasuredOdds' printout; the numbers are in its comment, not here.
	// On odds that are NOT measured (a cold state, a choker, a ping that just moved) it is bs22's rule on
	// bs22's thresholds, which is what that Gaussian + guard were tuned on: take it from k_pTakeGuess
	// unless the next tick is clearly better (bs21). Its bands would be wrong there -- the Gaussian reads
	// .99 on swings that land 98%, and .75 on swings that land 97%.
	constexpr float k_pSure      = 0.99f;
	constexpr float k_pTake      = 0.90f;
	constexpr float k_pSureGuess = 0.90f;   // bs22's k_pSure / k_pTake
	constexpr float k_pTakeGuess = 0.70f;
	constexpr float k_pGain      = 0.03f;   // "clearly better": bs22's rule, and an older tick over a newer (knife backtrack)
	constexpr int   k_patience   = 4;       // most ticks any hold may last (60 ms); the user's wait is 0..this
	// bs24, THE WAIT IS THE USER'S (Backstab wait slider, default 0). Closed loop, front stabs and ms
	// from the first clear chance, the knife's stab not held back (bs24), 30 / 75 / 150 ms ping:
	//   wait 0   3.71% -43 ms | 6.04% -19 ms | 10.2% +100 ms     (the default: 35-40 ms sooner than bs22 at 30 / 75, level at 150)
	//   wait 1   1.32% -30    | 3.75%  +5    |  7.45% +173
	//   wait 2   0.54% -21    | 2.18% +26    |  5.51% +242
	//   wait 4   0.22% -15    | 1.25% +66    |  3.97% +390
	//   bs22     1.87%  -8    | 3.60% +22    | 11.75% +104      (held back a tick, its own rule)
	// TestShippedDefault holds the first row and the wait-2 row.

	// THIN REACH (bs17). The pad above prices HIS position only; ours on the server is a client
	// prediction, and with both of us running one tick of relative motion is ~9u. bs16's whiff
	// (slack 0.6, pad 0.43, both ~300 u/s) had a solid swing one tick later -- a whiff costs the
	// whole 0.8 s swing. So a swing that clears the pad by less than k_relLead ticks of relative
	// motion waits, ONLY while the next tick reaches past that and keeps its odds. Never a refusal.
	// ponytail: k_relLead is a guess -- calibrate on slack= / rel= of STAB lines vs WHIFF.
	constexpr float k_relLead = 0.5f;
	inline float ThinPad(float pad, float relSpeed2D) { return pad + k_relLead * relSpeed2D * k_tick; }

	enum EDecision { DEC_FIRE, DEC_ODDS, DEC_HOLD };
	// thin = this tick's slack is under ThinPad, solidNext = the next tick's is not. wait = ticks a stab may
	// be held for better odds (the user's, 0..k_patience). The thin-reach hold is NOT the wait: a whiff
	// costs the whole 0.8 s swing, so it holds its one tick at any wait (k_patience still ends it).
	inline EDecision Decide(float p, float pNext, bool reachNext, int held, bool thin = false, bool solidNext = false, bool measured = true,
		int wait = k_patience)
	{
		const float take = measured ? k_pTake : k_pTakeGuess, sure = measured ? k_pSure : k_pSureGuess;
		if (p < take)
			return DEC_ODDS;
		if (held >= k_patience)
			return DEC_FIRE;   // waited on long enough
		if (thin && solidNext && pNext >= std::min(p, sure))
			return DEC_HOLD;   // a solid swing one tick on, no worse odds: do not risk the whiff
		if (held >= wait || !reachNext)
			return DEC_FIRE;   // the wait is over, or the window is closing
		if (measured)
			return p >= k_pSure ? DEC_FIRE : DEC_HOLD;   // sure, or chancy: let it become sure
		return pNext < p + k_pGain ? DEC_FIRE : DEC_HOLD;   // bs21: a clearly better next tick is waited for
	}

	// ---- A HOLD, COUNTED (JudgeKnife) -------------------------------------------------------------
	// How many ticks a stab on one man has been waited on, for Decide's k_patience. Counted once per
	// command however many targets' swings (or rewind ticks) land on him. A hold survives k_holdGap ticks
	// of something else in between (HOLD, REACH, REACH, HOLD is one hold) -- a candidate that only grazes
	// him (why=reach) alternating with a held one reset it every other tick before, and a chancy stab
	// waited for ever (critic round 2; the off-by-one that kept it at one tick, round 3).
	constexpr int k_holdGap = 2;
	struct Hold { int tick = -1000, count = 0; };
	// ticks held BEFORE this command (a stale hold starts over): the default rewind tick may already have
	// counted this command, and an older one judged after it must not see its patience a tick short
	inline int HeldBefore(Hold& h, int now)
	{
		if (now - h.tick > k_holdGap + 1 || now < h.tick)
			h.count = 0;
		return h.tick == now && h.count > 0 ? h.count - 1 : h.count;
	}
	inline void HoldCount(Hold& h, int now)
	{
		if (h.tick != now)
		{
			++h.count;
			h.tick = now;
		}
	}

	// ---- KNIFE BACKTRACK, WHICH TICK (RunBackstab) ------------------------------------------------
	// Every rewind tick from the default (back 0) to the oldest the server takes is judged; the best fire
	// is the shot. Rules (bs19 / bs23):
	//   * an older tick has to beat the shot so far by k_pGain (newer = further from the window's edge)
	//   * while the default tick HOLDS for a man, an older tick's fire on him has to reach what that hold
	//     waits for -- k_pSure on measured odds, the next tick's odds on guessed (bs22) -- or the wait wins
	//   * the search stops at a sure shot: k_pSure measured, k_pSureGuess guessed (bs22's .90)
	// An older box does not move a tick on (NextTickStep): the next command can ask for the same tick, so
	// "the reach is about to go" cannot be read off it.
	constexpr int k_btPlayers = 65;
	struct BtSearch
	{
		float aBar[k_btPlayers]{};
		bool  bFired = false, bShotMeasured = false;
		float flShot = 0.f;
		int   iBack = 0;
		// the default tick holds for player idx
		void Hold(int idx, bool measured, float pNext)
		{
			if (idx >= 0 && idx < k_btPlayers)
				aBar[idx] = std::max(aBar[idx], measured ? k_pSure : pNext);
		}
		// a fire judged `back` ticks back; true = it is the shot now
		bool Offer(int back, int idx, float p, bool measured)
		{
			if (back && idx >= 0 && idx < k_btPlayers && p < aBar[idx])
				return false;   // waiting on the default tick is better
			if (bFired && p <= flShot + k_pGain)
				return false;
			bFired = true;
			flShot = p;
			bShotMeasured = measured;
			iBack = back;
			return true;
		}
		bool Done() const { return bFired && flShot >= (bShotMeasured ? k_pSure : k_pSureGuess); }
	};
	inline V3 NextTickStep(int back, V3 velocity) { return back ? V3{} : velocity * k_tick; }
}
