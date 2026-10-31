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
	// Behind and facestab both constrain HIS yaw, so together they are one arc [lo, hi]. His live
	// yaw is ~ N(centre, sigYaw); the bearing is ~ N(bearing, sigBear) and moves the behind edges and
	// the facing test. Facestab's edges hang off our own yaw, which we write, so they only pay sigYaw.
	struct StabOdds
	{
		float p = 0.f;        // chance the server calls it a backstab
		float margin = 0.f;   // degrees inside [lo, hi] at the centre estimate (< 0 = outside)
		float facing = 0.f;   // degrees inside the facing window
		float lo = 0.f, hi = 0.f;
	};

	inline StabOdds StabProbability(float bearing, float ownerYaw, float yawCentre, float sigYaw, float sigBear)
	{
		StabOdds o;
		sigYaw = std::max(sigYaw, 0.01f);
		sigBear = std::max(sigBear, 0.01f);

		const float dOwn = Wrap(ownerYaw - bearing);
		o.facing = k_facingHalf - std::fabs(dOwn);
		const float pFacing = Phi((k_facingHalf - dOwn) / sigBear) - Phi((-k_facingHalf - dOwn) / sigBear);
		if (std::fabs(dOwn) >= 90.f)
			return o;   // pointed away from him: nothing below is a single arc any more, and it is 0 anyway

		const float ownU = bearing + dOwn;   // our yaw, unwrapped next to the bearing
		const float sigEdge = std::sqrt(sigYaw * sigYaw + sigBear * sigBear);

		float lo = bearing - k_behindHalf, sigLo = sigEdge;
		if (ownU - k_facestabHalf > lo) { lo = ownU - k_facestabHalf; sigLo = sigYaw; }
		float hi = bearing + k_behindHalf, sigHi = sigEdge;
		if (ownU + k_facestabHalf < hi) { hi = ownU + k_facestabHalf; sigHi = sigYaw; }
		o.lo = lo; o.hi = hi;

		const float mid = 0.5f * (lo + hi);
		const float c = mid + Wrap(yawCentre - mid);
		o.margin = std::min(hi - c, c - lo);
		if (hi <= lo)
			return o;

		const float pYaw = Phi((hi - c) / sigHi) - Phi((lo - c) / sigLo);
		o.p = std::clamp(pYaw, 0.f, 1.f) * std::clamp(pFacing, 0.f, 1.f);
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

	// Once per FRAME_NET_UPDATE_END: tSim = his m_flSimulationTime, tPacket = the packet's server time.
	// Returns false when it carried nothing new of him (duplicate simulation time).
	inline bool AddSnapshot(Track& tr, float tSim, float tPacket, float yaw, V3 origin, V3 mins, V3 maxs)
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
					for (int k = 0; k < k_est; k++)
						tr.aPred[p][k] -= shift;
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
	};

	inline float MeasuredMs(const Track& tr, int k)
	{
		const float slow = tr.iScored ? tr.aMsSlow[k] / (1.f - std::pow(1.f - k_errSlow, float(std::min(tr.iScored, 400)))) : 0.f;
		return std::max(tr.aMsFast[k], slow);
	}

	inline YawEstimate Estimate(const Track& tr, float tDue)
	{
		YawEstimate e;
		if (!tr.bLive || !tr.iCount)
			return e;
		float pred[k_est];
		Predict(tr, tDue, pred);
		int best = 1;   // half rate until the audit has spoken
		if (tr.iScored)
			for (int k = 0; k < k_est; k++)
				if (MeasuredMs(tr, k) < MeasuredMs(tr, best))
					best = k;
		e.k = best;
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
		float p = 0.f;             // what the swing is decided on: the smaller
	};
	inline GuardedOdds TurnGuarded(float bearing, float ownYaw, const YawEstimate& e, float sigBear, float seenLead = 0.f)
	{
		GuardedOdds g;
		g.best = StabProbability(bearing, ownYaw, e.centre, e.sigma, sigBear);
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
		g.p = std::min(g.best.p, g.pGuard);
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

	// ---- when to swing ----------------------------------------------------------------------------
	constexpr float k_pSure    = 0.90f;   // take it on the spot
	constexpr float k_pTake    = 0.70f;   // take it unless the next tick is clearly better (a failed swing alerts him)
	constexpr float k_pGain    = 0.03f;   // "clearly better"
	constexpr int   k_patience = 4;       // ticks a better tick may be waited for (60 ms)

	enum EDecision { DEC_FIRE, DEC_ODDS, DEC_HOLD };
	inline EDecision Decide(float p, float pNext, bool reachNext, int held)
	{
		if (p >= k_pSure)
			return DEC_FIRE;
		if (p < k_pTake)
			return DEC_ODDS;
		if (!reachNext || pNext < p + k_pGain || held >= k_patience)
			return DEC_FIRE;   // the window is closing, flat, or has been waited on long enough
		return DEC_HOLD;
	}
}
