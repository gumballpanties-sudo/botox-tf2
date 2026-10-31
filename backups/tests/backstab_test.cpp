// Gate test for the auto backstab's engine-free math (src/hacks/aim/bs_math.h).
// Build + run:  cmake --build build --config Release --target backstab_test && build\Release\backstab_test.exe
// Ground truth is the server's own code, copied into BsMath::ServerBehindAndFacing from
// tf_weapon_knife.cpp:447-481 -- the model is checked against THAT, not against itself.
#include "../src/hacks/aim/bs_math.h"
#include <cstdio>
#include <random>
#include <vector>

using namespace BsMath;

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)
static bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

// k_tick comes from bs_math.h (TF2's 0.015 s)

static void TestWrap()
{
	CHECK(Near(Wrap(190.f), -170.f, 1e-3f));
	CHECK(Near(Wrap(-190.f), 170.f, 1e-3f));
	CHECK(Near(Wrap(720.f + 45.f), 45.f, 1e-3f));
	CHECK(Near(Wrap(180.f), -180.f, 1e-3f));
}

// With vanishing uncertainty the odds must BE the server's boolean, everywhere off the edges.
static void TestWindowsMatchServer()
{
	std::mt19937 rng(1234);
	std::uniform_real_distribution<float> pos(-60.f, 60.f), ang(-180.f, 180.f);
	int compared = 0, mismatched = 0;
	for (int i = 0; i < 200000; i++)
	{
		const V3 own{ 0.f, 0.f, 0.f }, tgt{ pos(rng), pos(rng), 0.f };
		if (Len2D(tgt) < 1.f)
			continue;
		const float ownYaw = ang(rng), tgtYaw = ang(rng);
		const float bearing = Deg(std::atan2(tgt.y, tgt.x));
		// skip anything within 0.02 degrees of one of the three edges: float noise decides those
		const float eB = std::fabs(std::fabs(Wrap(tgtYaw - bearing)) - k_behindHalf);
		const float eF = std::fabs(std::fabs(Wrap(ownYaw - bearing)) - k_facingHalf);
		const float eS = std::fabs(std::fabs(Wrap(tgtYaw - ownYaw)) - k_facestabHalf);
		if (eB < 0.02f || eF < 0.02f || eS < 0.02f)
			continue;
		const bool server = ServerBehindAndFacing(own, tgt, ownYaw, tgtYaw);
		const bool model = StabProbability(bearing, ownYaw, tgtYaw, 0.001f, 0.001f).p > 0.5f;
		++compared;
		if (server != model)
			++mismatched;
	}
	std::printf("  windows vs server: %d compared, %d mismatched\n", compared, mismatched);
	CHECK(compared > 150000);
	CHECK(mismatched == 0);
}

// The odds are a probability: sample his yaw and his position the way the model says they are
// distributed, run the SERVER's test, and the hit rate has to land on p.
static void TestOddsCalibrated()
{
	// ownOff / tgtOff: our yaw and his yaw relative to the bearing from us to him (his 0 = he faces
	// straight away from us = dead behind; +-90 = the sidestab line)
	struct Case { float dist, ownOff, tgtOff, sigYaw, sigPos; };
	const Case cases[] = {
		{ 40.f,   0.f,  80.f,  5.f, 0.2f },   // sidestab, quiet target
		{ 40.f,   0.f,  80.f, 20.f, 0.2f },   // sidestab, busy target
		{ 30.f,  25.f, -75.f,  8.f, 2.0f },   // facestab edge binds (25 + 75 = 100 of 107)
		{ 45.f,  -5.f,  10.f, 12.f, 1.0f },   // dead behind
		{ 35.f,  50.f,  30.f,  6.f, 4.0f },   // facing edge near its limit, noisy position
		{ 40.f,   0.f,  95.f, 10.f, 0.5f },   // just OUTSIDE behind at the estimate
	};
	std::mt19937 rng(99);
	std::normal_distribution<float> n01(0.f, 1.f);
	for (const auto& c : cases)
	{
		const float bearing = 30.f;   // arbitrary, not axis aligned
		const V3 tgt{ c.dist * std::cos(Rad(bearing)), c.dist * std::sin(Rad(bearing)), 0.f };
		const float ownYaw = bearing + c.ownOff;
		const float yawCentre = Wrap(bearing + c.tgtOff);
		const float sigBear = Deg(std::atan2(c.sigPos, c.dist));
		const StabOdds o = StabProbability(bearing, ownYaw, yawCentre, c.sigYaw, sigBear);

		int hits = 0;
		const int N = 200000;
		for (int i = 0; i < N; i++)
		{
			const float yaw = yawCentre + c.sigYaw * n01(rng);
			// position error perpendicular to the line of sight is what moves the bearing
			const V3 perp{ -std::sin(Rad(bearing)), std::cos(Rad(bearing)), 0.f };
			const V3 p = tgt + perp * (c.sigPos * n01(rng));
			if (ServerBehindAndFacing({}, p, ownYaw, yaw))
				++hits;
		}
		const float rate = float(hits) / N;
		std::printf("  odds case d=%.0f own=%+.0f his=%.0f sy=%.0f sp=%.1f: p=%.3f server=%.3f margin=%.1f\n",
			c.dist, c.ownOff, c.tgtOff, c.sigYaw, c.sigPos, o.p, rate, o.margin);
		CHECK(Near(o.p, rate, 0.02f));
	}
}

static void TestDecisions()
{
	// sidestab, 10 degrees inside the window: quiet target fires on the spot, a busy one still
	// fires once waiting buys nothing -- the old worst-case band refused both
	const StabOdds quiet = StabProbability(0.f, 0.f, 80.f, 5.f, 0.2f);
	const StabOdds busy = StabProbability(0.f, 0.f, 80.f, 15.f, 0.2f);
	CHECK(quiet.p > k_pSure);
	CHECK(Decide(quiet.p, quiet.p, true, 0) == DEC_FIRE);
	CHECK(busy.p > k_pTake && busy.p < k_pSure);
	CHECK(Decide(busy.p, busy.p, true, 0) == DEC_FIRE);            // flat -> take it
	CHECK(Decide(busy.p, busy.p + 0.1f, true, 0) == DEC_HOLD);     // improving -> wait
	CHECK(Decide(busy.p, busy.p + 0.1f, true, k_patience) == DEC_FIRE);
	CHECK(Decide(busy.p, busy.p + 0.1f, false, 0) == DEC_FIRE);    // reach closing -> now
	CHECK(Decide(0.5f, 0.9f, true, 99) == DEC_ODDS);                // never a coin flip
	// the facestab window: our yaw 40 right of him, his 75 left -> 115 apart, server refuses
	CHECK(!ServerBehindAndFacing({}, { 40.f, 0.f, 0.f }, 40.f, -75.f));
	CHECK(StabProbability(0.f, 40.f, -75.f, 1.f, 0.1f).p < 0.01f);
	// and aiming straight at him makes facestab unable to bind at all (107 > 90)
	for (float y = -89.f; y <= 89.f; y += 1.f)
		CHECK(StabProbability(0.f, 0.f, y, 0.01f, 0.01f).p > 0.99f);
}

static void TestReach()
{
	const V3 eye{ 0.f, 0.f, 0.f }, fwd{ 1.f, 0.f, 0.f };
	const V3 lo{ 50.f, -24.f, -40.f }, hi{ 98.f, 24.f, 42.f };
	CHECK(SegmentEntry(eye, fwd, 48.f, lo, hi, 0.f) < 0.f);        // bare ray stops 2u short
	CHECK(Near(SegmentEntry(eye, fwd, 48.f, lo, hi, 18.f), 32.f, 1e-3f));
	CHECK(Near(ReachSlack(eye, fwd, 48.f, lo, hi, 18.f), 16.f, 0.01f));
	CHECK(ReachSlack(eye, fwd, 48.f, { 67.f, -24.f, -40.f }, { 99.f, 24.f, 42.f }, 18.f) < 0.f);
	CHECK(Near(SegmentEntry(eye, fwd, 48.f, { -5.f, -5.f, -5.f }, { 5.f, 5.f, 5.f }, 0.f), 0.f, 1e-6f)); // inside
	// the graze test: a ray 0.1u inside his edge survives no shrink of 0.3u (JudgeKnife's pad)
	CHECK(SegmentEntry(eye, fwd, 48.f, { 20.f, -24.f, -40.f }, { 68.f, 0.1f, 42.f }, 0.f) >= 0.f);
	CHECK(SegmentEntry(eye, fwd, 48.f, { 20.f, -24.f, -40.f }, { 68.f, 0.1f, 42.f }, -0.3f) < 0.f);
	CHECK(SegmentEntry(eye, fwd, 48.f, { 20.f, -24.f, -40.f }, { 68.f, 5.f, 42.f }, -0.3f) >= 0.f);
	// a shrink bigger than the box leaves nothing to enter (the slab swap must not turn it into a hit)
	CHECK(SegmentEntry(eye, fwd, 48.f, { 20.f, -1.f, -1.f }, { 22.f, 1.f, 1.f }, -5.f) < 0.f);

	// DoSwingTraceInternal's order
	CHECK(ResolveSwing(48.f, 0, 30.f, 0.5f, -1, 0.f, 1.f) == SWING_WORLD);   // wall at 24 before him at 30
	CHECK(ResolveSwing(48.f, 0, 20.f, 0.5f, -1, 0.f, 1.f) == 0);             // him at 20 before the wall
	CHECK(NeedsHullPass(-1, 1.f));
	CHECK(!NeedsHullPass(-1, 0.9f));
	CHECK(ResolveSwing(48.f, -1, 0.f, 0.9f, 3, 10.f, 1.f) == SWING_WORLD);   // ray hit world: no hull pass
	CHECK(ResolveSwing(48.f, -1, 0.f, 1.f, 3, 32.f, 1.f) == 3);              // hull reaches him
	CHECK(ResolveSwing(48.f, -1, 0.f, 1.f, 3, 32.f, 0.25f) == SWING_WORLD);  // hull touches a wall first
	CHECK(ResolveSwing(48.f, -1, 0.f, 1.f, -1, 0.f, 1.f) == SWING_MISS);

	Box boxes[2];
	boxes[0] = { { 60.f, -24.f, -40.f }, { 108.f, 24.f, 42.f }, 7 };
	boxes[1] = { { 40.f, 10.f, -40.f }, { 88.f, 58.f, 42.f }, 9 };
	float t = 0.f;
	CHECK(FirstBox(eye, fwd, 48.f, 0.f, boxes, 2, t) == -1);
	CHECK(FirstBox(eye, fwd, 48.f, 18.f, boxes, 2, t) == 1 && Near(t, 22.f, 1e-3f));
}

static void TestLerp()
{
	// TF2 defaults: cl_interp 0.1, ratio 1, updaterate 66; sv 10..66 rate, ratio 1..5
	CHECK(Near(ServerLerp(true, 0.1f, 1.f, 66.f, 10.f, 66.f, 1.f, 5.f), 0.1f, 1e-6f));
	CHECK(Near(ServerLerp(true, 0.f, 1.f, 66.f, 10.f, 66.f, 1.f, 5.f), 1.f / 66.f, 1e-6f));
	CHECK(Near(ServerLerp(true, 0.f, 1.f, 128.f, 10.f, 66.f, 1.f, 5.f), 1.f / 66.f, 1e-6f));   // rate clamped
	CHECK(Near(ServerLerp(true, 0.f, 0.f, 66.f, 10.f, 66.f, -1.f, 5.f), 1.f / 66.f, 1e-6f));   // ratio 0 -> 1
	CHECK(Near(ServerLerp(true, 0.f, 0.5f, 66.f, 10.f, 66.f, 1.f, 5.f), 1.f / 66.f, 1e-6f));  // ratio clamped up
	CHECK(Near(ServerLerp(true, 0.f, 2.f, 20.f, 10.f, 66.f, 1.f, 5.f), 0.1f, 1e-6f));
	CHECK(ServerLerp(false, 0.1f, 1.f, 66.f, 10.f, 66.f, 1.f, 5.f) == 0.f);
	// a misconfigured server (min > max): Source's clamp macro gives max, never UB
	CHECK(Near(ServerLerp(true, 0.f, 3.f, 66.f, 10.f, 66.f, 4.f, 2.f), 2.f / 66.f, 1e-6f));
	CHECK(Near(ServerLerp(true, 0.f, 1.f, 100.f, 80.f, 40.f, 1.f, 5.f), 1.f / 40.f, 1e-6f));
	CHECK(TimeToTicks(0.1f, k_tick) == 7);
	CHECK(TimeToTicks(1.f / 66.f, k_tick) == 1);
}

// One target, 66 snapshots/s, ping 80ms: push a prediction per command and a snapshot per tick
// the way aim_melee.cpp does, with yaw(t) given on the ABSOLUTE server clock. His simulation time
// runs `lead` seconds ahead of it, the way m_nTickBase does (player.cpp:3140-3172). The yaw the
// packet carries is quantised to the 10-bit SendPropAngle grid (tf_player.cpp:752).
static float Quant10(float yaw) { const float s = 360.f / 1024.f; return std::round(yaw / s) * s; }
template <class F>
static void Drive(Track& tr, F yawAt, float from, float to, float ping = 0.08f, float lead = 0.f)
{
	for (float t = from; t < to - 1e-4f; t += k_tick)
	{
		AddSnapshot(tr, t + lead, t, Wrap(Quant10(yawAt(t))), { t * 100.f, 0.f, 0.f }, { -24, -24, 0 }, { 24, 24, 82 });
		PushPrediction(tr, t + k_tick + ping);
	}
}

static void TestTrack()
{
	// a quiet target warms to the floor
	{
		Track tr; Reset(tr);
		Drive(tr, [](float) { return 30.f; }, 0.f, 0.6f);
		const YawEstimate e = Estimate(tr, 0.6f + 0.08f);
		std::printf("  still: sigma=%.2f centre=%.2f k=%d\n", e.sigma, e.centre, e.k);
		CHECK(e.sigma < 3.5f);   // floor + turn-onset over a ~95 ms horizon, nothing measured on top
		CHECK(e.sigma > 2.f);    // ... but never the 1-degree certainty a fresh turn would break
		CHECK(Near(e.centre, 30.f, 0.5f));
	}
	// a steady turn is extrapolated at full rate and the audit says so
	{
		Track tr; Reset(tr);
		Drive(tr, [](float t) { return 200.f * t; }, 0.f, 0.8f);
		const float tLast = 0.8f - k_tick;
		const YawEstimate e = Estimate(tr, tLast + 0.1f);
		std::printf("  turn200: sigma=%.2f k=%d rate=%.1f centre=%.2f want=%.2f\n", e.sigma, e.k, e.rate, e.centre, Wrap(200.f * (tLast + 0.1f)));
		CHECK(e.k == 2);
		CHECK(Near(e.rate, 200.f, 2.f));
		CHECK(std::fabs(Wrap(e.centre - 200.f * (tLast + 0.1f))) < 1.f);
		CHECK(e.sigma < 8.f);    // measured ~0 + onset + a tick of timing + in-flight spread at 200 deg/s
	}
	// THE TWO CLOCKS (critic defect 1). His sim time leads the absolute tick by 6 ticks. Yaw keyed by
	// packet time lands on the truth; keyed by sim time -- what the first bs15 draft did -- it is
	// short by 6 ticks of turn and the audit cannot see it, because its truth is on the same clock.
	{
		const float lead = 6 * k_tick, ping = 0.03f;
		Track good; Reset(good);
		Drive(good, [](float t) { return 300.f * t; }, 0.f, 0.8f, ping, lead);
		Track bad; Reset(bad);
		for (float t = 0.f; t < 0.8f - 1e-4f; t += k_tick)
		{
			AddSnapshot(bad, t + lead, t + lead, Wrap(Quant10(300.f * t)), {}, {}, {});
			PushPrediction(bad, t + k_tick + ping);
		}
		const float tLast = 0.8f - k_tick, tDue = tLast + k_tick + ping;
		const YawEstimate eg = Estimate(good, tDue), eb = Estimate(bad, tDue);
		const float errGood = std::fabs(Wrap(eg.centre - 300.f * tDue)), errBad = std::fabs(Wrap(eb.centre - 300.f * tDue));
		std::printf("  tickbase lead 6: packet-clock err=%.2f sigma=%.2f | sim-clock err=%.2f sigma=%.2f\n",
			errGood, eg.sigma, errBad, eb.sigma);
		CHECK(errGood < 1.5f);
		CHECK(errBad > 10.f);
		CHECK(errBad > 2.f * eb.sigma);   // and the wrong clock is confidently wrong: why it mattered
	}
	// a target who snaps once and holds: after the snap, holding beats extrapolating the snap
	{
		Track tr; Reset(tr);
		Drive(tr, [](float t) { return t < 0.5f ? 0.f : 60.f; }, 0.f, 1.2f);
		const YawEstimate e = Estimate(tr, 1.2f + 0.09f);
		CHECK(e.k == 0);                   // after the snap, holding beats extrapolating a snap
		CHECK(Near(e.centre, 60.f, 0.5f));
	}
	// the "back only visible for a second" case: a hard flick, then he stands still. bs14's max-of-16
	// band stayed wide for the whole ring; the odds have to reopen quickly.
	{
		Track tr; Reset(tr);
		auto yaw = [](float t) { return t < 0.5f ? 0.f : t < 0.65f ? 600.f * (t - 0.5f) : 90.f; };
		Drive(tr, yaw, 0.f, 0.65f);
		float sPeak = 0.f, take = -1.f, sure = -1.f;
		for (float t = 0.65f; t < 2.f; t += k_tick)
		{
			Drive(tr, yaw, t, t + k_tick);
			const YawEstimate e = Estimate(tr, t + k_tick + 0.08f);
			sPeak = std::max(sPeak, e.sigma);
			// he now faces 90; we stand on bearing 15 aiming along it -> his yaw is 75 off the
			// bearing: a sidestab 15 degrees inside the behind window
			const float p = StabProbability(15.f, 15.f, e.centre, e.sigma, 0.3f).p;
			// while the rate fit still carries the flick, the centre overshoots past 90: no swing
			if (t + k_tick - 0.65f < 0.07f)
				CHECK(p < k_pTake);
			if (take < 0.f && p >= k_pTake) take = t + k_tick - 0.65f;
			if (sure < 0.f && p >= k_pSure) { sure = t + k_tick - 0.65f; break; }
		}
		std::printf("  flick: peak sigma=%.1f, 15-degree sidestab takeable after %.3fs, sure after %.3fs\n", sPeak, take, sure);
		CHECK(sPeak > 25.f);
		// Takeable once the fit has let go of the flick (~0.1 s): sigma is still ~26 degrees, but only
		// the near edge 15 degrees away can lose it, so p = Phi(15/26) = 0.72 is the honest number.
		// SURE waits for the slow memory to forget the flick; neither may take the whole second.
		CHECK(take > 0.07f && take < 0.5f);
		CHECK(sure >= take && sure < 0.8f);
	}
	// wrap: 179 -> -179 is +2 degrees, not -358
	{
		Track tr; Reset(tr);
		Drive(tr, [](float t) { return 170.f + 60.f * t; }, 0.f, 0.4f);
		CHECK(Near(Rate(tr), 60.f, 4.f));   // 0.35-degree grid over a 0.1 s fit
	}
	// duplicates and breaks
	{
		Track tr; Reset(tr);
		CHECK(AddSnapshot(tr, 1.f, 1.f, 0.f, {}, {}, {}));
		CHECK(!AddSnapshot(tr, 1.f, 1.1f, 5.f, {}, {}, {}));
		CHECK(AddSnapshot(tr, 1.f + k_tick, 1.f + k_tick, 0.f, { 3.f, 0.f, 0.f }, {}, {}));
		CHECK(tr.iCount == 2);
		CHECK(AddSnapshot(tr, 1.f + 2 * k_tick, 1.f + 2 * k_tick, 0.f, { 2000.f, 0.f, 0.f }, {}, {}));   // teleport
		CHECK(tr.iCount == 1);
		CHECK(AddSnapshot(tr, 0.5f, 2.f, 0.f, {}, {}, {}));                                              // respawn clock
		CHECK(tr.iCount == 1);
		// the packet clock never stands still between two records (two records, one frame)
		CHECK(AddSnapshot(tr, 0.5f + k_tick, 2.f, 0.f, {}, {}, {}));
		CHECK(tr.aTy[Idx(tr, 0)] > tr.aTy[Idx(tr, 1)]);
	}
	// a spinner: the unwrapped yaw is re-centred, the estimate is not disturbed
	{
		Track tr; Reset(tr);
		Drive(tr, [](float t) { return 1000.f * t; }, 0.f, 12.f);
		CHECK(std::fabs(tr.aY[Idx(tr, 0)]) <= 3600.f + 400.f);
		CHECK(Near(Rate(tr), 1000.f, 15.f));
	}
}

static void TestRewind()
{
	Track tr; Reset(tr);
	for (int i = 0; i < 10; i++)
		AddSnapshot(tr, 1.f + i * k_tick, 1.f + i * k_tick, 0.f, { i * 4.5f, 0.f, 0.f }, { -24, -24, 0 }, { 24, 24, i < 5 ? 82.f : 62.f });
	V3 o, mn, mx; float ex = 0.f;
	CHECK(Rewind(tr, 1.f + 2.5f * k_tick, 0.1f, o, mn, mx, ex));
	CHECK(Near(o.x, 11.25f, 1e-3f) && ex == 0.f);
	CHECK(Rewind(tr, 1.f + 4 * k_tick, 0.1f, o, mn, mx, ex));
	CHECK(Near(o.x, 18.f, 1e-3f) && Near(mx.z, 82.f, 1e-3f));             // exact record
	CHECK(Rewind(tr, 1.f + 4.5f * k_tick, 0.1f, o, mn, mx, ex));
	CHECK(Near(mx.z, 72.f, 1e-2f));                                         // ducking mid-lerp
	CHECK(Rewind(tr, 1.f + 11 * k_tick, 0.1f, o, mn, mx, ex));             // 2 ticks past newest
	CHECK(Near(ex, 2 * k_tick, 1e-4f) && Near(o.x, 40.5f + 9.f, 0.05f));
	CHECK(Rewind(tr, 5.f, 0.05f, o, mn, mx, ex));                            // guess capped, price not
	CHECK(Near(o.x, 40.5f + 300.f * 0.05f, 0.1f) && Near(ex, 2.f * (5.f - (1.f + 9 * k_tick)), 1e-3f));
	// older than every record (just out of dormancy): the server's history may hold a 64u step or a
	// death that leaves him at his live origin -- unknowable, so unknown (critic rounds 6-7)
	CHECK(Rewind(tr, 1.f - 2 * k_tick, 0.1f, o, mn, mx, ex) && ex >= k_unknown);
	CHECK(Rewind(tr, 0.f, 0.1f, o, mn, mx, ex) && ex >= k_unknown);
	{
		// a STANDING man with one record: the old price was 0.5 x 0 speed x anything = nothing
		Track one; Reset(one);
		AddSnapshot(one, 2.f, 2.f, 0.f, {}, {}, {});
		CHECK(Rewind(one, 2.f, 0.1f, o, mn, mx, ex) && ex >= k_unknown);
		CHECK(Rewind(one, 2.1f, 0.1f, o, mn, mx, ex) && ex >= k_unknown);
		// ... and the second record makes him judgeable the moment the rewind tick is inside them
		AddSnapshot(one, 2.f + k_tick, 2.f + k_tick, 0.f, {}, {}, {});
		CHECK(Rewind(one, 2.f + 0.5f * k_tick, 0.1f, o, mn, mx, ex) && ex == 0.f);
	}
	// the acceleration term: a man who can go from 0 to moving within the guess
	CHECK(Near(PositionError(0.f, 0.075f, 0.175f), 0.175f + 0.5f * 3000.f * 0.075f * 0.075f, 1e-3f));   // 1/2 a t^2
	CHECK(PositionError(0.f, 0.075f, 0.175f) > 8.f);

	// high lerp: 0.5s back must still be inside the ring at 66/s (critic defect 7)
	Track big; Reset(big);
	for (int i = 0; i < 60; i++)
		AddSnapshot(big, i * k_tick, i * k_tick, 0.f, { float(i), 0.f, 0.f }, {}, {});
	CHECK(Rewind(big, 59 * k_tick - 0.5f, 0.1f, o, mn, mx, ex) && ex == 0.f && Near(o.x, 59.f - 0.5f / k_tick, 0.05f));
}

// A fakelagging target: the server runs his 4 commands in one tick every 4 ticks, so 3 packets out
// of 4 carry no new simulation time for him.
static void TestChoke()
{
	Track tr; Reset(tr);
	for (int p = 0; p < 40; p++)
	{
		const float tPacket = p * k_tick;
		const float tSim = (p / 4) * 4 * k_tick + 2 * k_tick;   // steps every 4 packets, tickbase lead 2
		const float yaw = (p / 4) * 20.f;                        // and his yaw steps with it
		AddSnapshot(tr, tSim, tPacket, Wrap(yaw), { (p / 4) * 20.f, 0.f, 0.f }, { -24, -24, 0 }, { 24, 24, 82 });
		PushPrediction(tr, tPacket + k_tick + 0.05f);
	}
	CHECK(Choking(tr));
	CHECK(tr.iCount == 10);

	V3 o, mn, mx; float ex = 1.f;
	// newer than his newest record: his batch has either not landed (he is on it) or has (a full
	// velocity step on) -- centred halfway, half priced, the gap never capped (critic rounds 3-4)
	{
		const float gap = 39 * k_tick + 0.03f - (36 + 2) * k_tick;
		const float v = Velocity(tr).x;
		CHECK(v > 0.f);
		CHECK(Rewind(tr, 39 * k_tick + 0.03f, 0.01f, o, mn, mx, ex) && Near(ex, gap, 1e-4f));   // cap ignored
		CHECK(Near(o.x, 180.f + 0.5f * v * gap, 1e-2f));
	}

	// the same motion without the choke extrapolates
	Track smooth; Reset(smooth);
	for (int p = 0; p < 40; p++)
		AddSnapshot(smooth, p * k_tick, p * k_tick, 0.f, { p * 5.f, 0.f, 0.f }, {}, {});
	CHECK(!Choking(smooth));
	CHECK(Rewind(smooth, 39 * k_tick + 0.05f, 0.1f, o, mn, mx, ex) && Near(ex, 0.05f, 1e-4f) && o.x > 195.f + 10.f);

	// 64u between two records (a 14-tick fakelag at full speed): the server gives up on the rewind
	// and leaves him where he is (lagcomp.cpp:479-484)
	auto fChoker = [](Track& t, float step)
	{
		Reset(t);
		for (int p = 0; p < 6 * 14; p++)   // a packet every tick, a new record every 14
			AddSnapshot(t, (p / 14) * 14 * k_tick, p * k_tick, 0.f, { (p / 14) * step, 0.f, 0.f }, {}, {});
	};
	Track lag; fChoker(lag, 70.f);
	CHECK(Rewind(lag, 3.5f * 14 * k_tick, 0.1f, o, mn, mx, ex) && Near(o.x, 350.f, 1e-3f) && Near(ex, 2 * 14 * k_tick, 1e-4f));
	// and priced so JudgeKnife's reach pad (0.25 + 0.175 + 0.5 speed ext) exceeds the 18u hull: refused
	CHECK(0.25f + 0.175f + 0.5f * Len2D(Velocity(lag)) * ex > 18.f);
	// his next 70u batch may land before our command: newer than his newest, still a whole batch of error
	CHECK(Rewind(lag, 5 * 14 * k_tick + 2 * k_tick, 0.1f, o, mn, mx, ex) && ex >= 2 * 14 * k_tick - 1e-4f);
	CHECK(0.25f + 0.175f + 0.5f * Len2D(Velocity(lag)) * ex > 18.f);
	// round 6: a choker whose PREVIOUS batch was 54u but whose next one is 67u -- if the server can
	// have it by the time our command runs, the swing is priced as the whole move
	{
		Track c; Reset(c);
		for (int p = 0; p < 4 * 12; p++)   // 12-tick batches at 300 u/s = 54u each
			AddSnapshot(c, (p / 12) * 12 * k_tick, p * k_tick, 0.f, { (p / 12) * 54.f, 0.f, 0.f }, {}, {});
		CHECK(Choking(c));
		const float tLastRecord = 36 * k_tick;   // packet time of his newest record
		const float T = 30 * k_tick;             // inside our records (0..36): RewindCore alone lerps, extrap 0
		CHECK(Rewind(c, T, 0.1f, o, mn, mx, ex) && ex == 0.f);
		// his batches are 12 ticks: at 300 u/s one step can be 54u + acceleration 48u > 64 -> a break
		// is possible once half a batch has passed since his newest record reached us
		CHECK(Rewind(c, T, 0.1f, o, mn, mx, ex, tLastRecord + 0.05f) && ex == 0.f);
		CHECK(Rewind(c, T, 0.1f, o, mn, mx, ex, tLastRecord + 0.25f) && Near(ex, 0.5f, 1e-3f));
		CHECK(0.25f + PositionError(Len2D(Velocity(c)), ex, 0.175f) > 18.f);
		// one late packet from a 60 fps walker at 200 ms ping is NOT a choke (critic round 7)
		{
			Track j; Reset(j);
			for (int p = 0; p < 30; p++)
				AddSnapshot(j, p * k_tick, p * k_tick, 0.f, { p * 4.5f, 0.f, 0.f }, {}, {});
			CHECK(!AddSnapshot(j, 29 * k_tick, 30 * k_tick, 0.f, { 29 * 4.5f, 0.f, 0.f }, {}, {}));   // one dup
			CHECK(Choking(j));
			CHECK(Rewind(j, 25 * k_tick, 0.1f, o, mn, mx, ex, 30 * k_tick + 0.23f) && ex == 0.f);
			CHECK(AddSnapshot(j, 31 * k_tick, 31 * k_tick, 0.f, { 31 * 4.5f, 0.f, 0.f }, {}, {}));   // 2-tick batch
			CHECK(Rewind(j, 27 * k_tick, 0.1f, o, mn, mx, ex, 31 * k_tick + 0.23f) && ex == 0.f);
		}
		// a 30 fps client sending 3-tick batches at 300 u/s, 200 ms ping: 14u steps never break the
		// server's rewind, so no refusal however old the record (critic round 8)
		{
			Track b; Reset(b);
			for (int p = 0; p < 45; p++)
				AddSnapshot(b, (p / 3) * 3 * k_tick, p * k_tick, 0.f, { (p / 3) * 13.5f, 0.f, 0.f }, {}, {});
			CHECK(Choking(b));
			CHECK(Rewind(b, 35 * k_tick, 0.1f, o, mn, mx, ex, 44 * k_tick + 0.23f) && ex == 0.f);
		}
		// a smooth (non-choking) walker at the same age is not touched
		Track w; Reset(w);
		for (int p = 0; p < 48; p++)
			AddSnapshot(w, p * k_tick, p * k_tick, 0.f, { p * 4.5f, 0.f, 0.f }, {}, {});
		CHECK(!Choking(w) && Rewind(w, 44 * k_tick, 0.1f, o, mn, mx, ex, 47 * k_tick + 0.25f) && ex == 0.f);
	}
	// a long gap on a smooth player (our own packet loss): capped guess, uncapped price
	{
		Track s; Reset(s);
		for (int p = 0; p < 20; p++)
			AddSnapshot(s, p * k_tick, p * k_tick, 0.f, { p * 4.5f, 0.f, 0.f }, {}, {});
		CHECK(Rewind(s, 19 * k_tick + 0.25f, 0.1f, o, mn, mx, ex) && Near(ex, 0.5f, 1e-4f) && Near(o.x, 85.5f + 30.f, 0.1f));
	}
	Track walk; fChoker(walk, 60.f);
	CHECK(Rewind(walk, 3.5f * 14 * k_tick, 0.1f, o, mn, mx, ex) && Near(o.x, 210.f, 1e-2f) && ex == 0.f);   // under 64: lerp
	// cl_updaterate 22: a rocket jumper 66u apart per OUR record is ~22u per server tick -- no break
	Track jump; Reset(jump);
	for (int r = 0; r < 8; r++)
		AddSnapshot(jump, r * 3 * k_tick, r * 3 * k_tick, 0.f, { r * 66.f, 0.f, 0.f }, {}, {});
	CHECK(!Choking(jump));
	CHECK(Rewind(jump, 4.5f * 3 * k_tick, 0.1f, o, mn, mx, ex) && Near(o.x, 297.f, 1e-2f) && ex == 0.f);

	// sim-time rollover: choking across a 100-tick base arrives as a "record" at the base itself
	{
		Track r; Reset(r);
		CHECK(AddSnapshot(r, 97 * k_tick, 97 * k_tick, 40.f, { 5.f, 6.f, 7.f }, {}, {}));
		CHECK(!AddSnapshot(r, 97 * k_tick, 98 * k_tick, 40.f, { 5.f, 6.f, 7.f }, {}, {}));   // choke
		CHECK(!AddSnapshot(r, 100 * k_tick, 101 * k_tick, 40.f, { 5.f, 6.f, 7.f }, {}, {})); // rollover: same record
		CHECK(r.iCount == 1 && r.iDupRun == 2 && Choking(r));
		CHECK(AddSnapshot(r, 102 * k_tick, 102 * k_tick, 44.f, { 9.f, 6.f, 7.f }, {}, {}));  // his real batch
		CHECK(r.iCount == 2 && r.aChoked[Idx(r, 0)]);
	}
	// the same at real server scale -- tick 15000 at TF2's 0.015 s, plus the client's +256 re-centre
	{
		CHECK(TimeToTicks(15000 * 0.015f, k_tick) == 15000);
		Track r; Reset(r);
		CHECK(AddSnapshot(r, 14997 * 0.015f, 14997 * 0.015f, 12.f, { 1.f, 2.f, 3.f }, {}, {}));
		CHECK(!AddSnapshot(r, 15000 * 0.015f, 15001 * 0.015f, 12.f, { 1.f, 2.f, 3.f }, {}, {}));
		CHECK(!AddSnapshot(r, 15256 * 0.015f, 15002 * 0.015f, 12.f, { 1.f, 2.f, 3.f }, {}, {}));
		CHECK(r.iCount == 1 && r.iDupRun == 2);
		// a genuine record on a round tick that MOVED is kept
		CHECK(AddSnapshot(r, 15003 * 0.015f, 15003 * 0.015f, 12.f, { 4.f, 2.f, 3.f }, {}, {}));   // his batch
		CHECK(r.iCount == 2 && r.aChoked[Idx(r, 0)]);
		Track m; Reset(m);   // a genuine record on a round tick that MOVED is kept
		CHECK(AddSnapshot(m, 14999 * 0.015f, 14999 * 0.015f, 12.f, { 1.f, 2.f, 3.f }, {}, {}));
		CHECK(AddSnapshot(m, 15000 * 0.015f, 15000 * 0.015f, 12.f, { 1.5f, 2.f, 3.f }, {}, {}));
		CHECK(m.iCount == 2);
	}
	// a stalled packet clock must not hide the next choke
	{
		Track s; Reset(s);
		CHECK(AddSnapshot(s, 10 * k_tick, 10 * k_tick, 0.f, {}, {}, {}));
		CHECK(AddSnapshot(s, 11 * k_tick, 10 * k_tick, 0.f, {}, {}, {}));   // same packet time, synthetic yaw clock
		CHECK(!AddSnapshot(s, 11 * k_tick, 11 * k_tick, 0.f, {}, {}, {}));  // the real next packet, no new sim
		CHECK(s.iDupRun == 1);
	}

	// his yaw jumps 20 degrees a batch: whatever estimator wins, the odds must know he is uncertain
	const YawEstimate e = Estimate(tr, 40 * k_tick + 0.05f);
	std::printf("  choker: k=%d sigma=%.1f\n", e.k, e.sigma);
	CHECK(e.sigma > 5.f);
}

// ---- the turn guard (dih-bs16) ---------------------------------------------------------------
static void TestTurnGuard()
{
	// estimates built the way Estimate builds them: centre = seen + frac(k) x rate x horizon
	auto fEst = [](float seen, float rate, int k, float hor, float sigCore)
	{
		YawEstimate e; e.seen = seen; e.rate = rate; e.k = k; e.horizon = hor;
		e.centre = Wrap(seen + k_estFrac[k] * rate * hor);
		e.sigmaCore = sigCore;
		const float spread = k_rateSpread * std::fabs(rate) * hor;
		e.sigma = std::sqrt(sigCore * sigCore + spread * spread);
		return e;
	};
	// still target: seen == centre, the guard changes nothing
	{
		const YawEstimate e = fEst(80.f, 0.f, 1, 0.1f, 10.f);
		const GuardedOdds g = TurnGuarded(0.f, 0.f, e, 0.3f);
		CHECK(Near(g.p, g.best.p, 1e-6f) && Near(g.pGuard, g.best.p, 1e-6f));
	}
	// the live log's worst front stab (rate 1218, hor .084, k=2): bet ~47 inside, seen 55 outside
	{
		const YawEstimate e = fEst(145.f, -1218.f, 2, 0.084f, 38.f);   // turning INTO the window
		const GuardedOdds g = TurnGuarded(0.f, 0.f, e, 0.3f);
		CHECK(g.best.p >= k_pTake);   // bs15 swung here
		CHECK(g.p < k_pTake);         // bs16 does not
	}
	// turning OUT of the window: the bet is the lower number already, nothing new refuses
	{
		const YawEstimate e = fEst(40.f, 300.f, 2, 0.1f, 12.f);
		const GuardedOdds g = TurnGuarded(0.f, 0.f, e, 0.3f);
		CHECK(Near(g.p, g.best.p, 1e-6f));
	}
	// a turn in: seen 5 outside, the bet 35 inside. Trusting 40% of the 40-degree turn puts the
	// guard 11 inside
	{
		const YawEstimate e = fEst(95.f, -400.f, 2, 0.1f, 6.f);
		const GuardedOdds g = TurnGuarded(0.f, 0.f, e, 0.3f);
		CHECK(Near(g.best.margin, 35.f, 0.01f));
		CHECK(Near(g.pGuard, StabProbability(0.f, 0.f, 79.f, 6.f, 0.3f).p, 1e-5f));
		CHECK(g.pGuard > 0.9f && g.pGuard < g.best.p);
	}
	// a later tick's judgement, the way JudgeKnife makes it: the NEXT tick's estimate (horizon one
	// tick longer, same newest snapshot) with seenLead = one tick. The seen yaw carries on by the
	// estimator's share of the rate for that tick; the trusted part of the rest is unchanged.
	{
		const float h = 0.1f;
		const YawEstimate eN = fEst(-100.f, 400.f, 1, h + k_tick, 10.f);
		const float want = -100.f + 0.5f * 400.f * k_tick + k_turnTrust * 0.5f * 400.f * h;
		const GuardedOdds gN = TurnGuarded(0.f, 0.f, eN, 0.3f, k_tick);
		CHECK(Near(gN.pGuard, StabProbability(0.f, 0.f, want, 10.f, 0.3f).p, 1e-5f));
		CHECK(Near(gN.guardMargin, want + 90.f, 1e-3f));
		// without the lead (a choker: no snapshot will show the carry-on) the guard stays short of it
		const float wantNoLead = -100.f + k_turnTrust * 0.5f * 400.f * (h + k_tick);
		CHECK(Near(TurnGuarded(0.f, 0.f, eN, 0.3f).guardMargin, wantNoLead + 90.f, 1e-3f));
		// a hold estimate expects no carry-on at all
		const YawEstimate e0 = fEst(-100.f, 400.f, 0, h + k_tick, 10.f);
		CHECK(Near(TurnGuarded(0.f, 0.f, e0, 0.3f, k_tick).guardMargin, -10.f, 1e-3f));
	}
	// a due time that close to his newest packet (zero-ping listen server): horizon below the lead.
	// The guard never lands past the bet -- it IS the bet then
	{
		const YawEstimate e = fEst(60.f, 600.f, 2, 0.005f, 5.f);
		const GuardedOdds g = TurnGuarded(0.f, 0.f, e, 0.3f, k_tick);
		CHECK(Near(g.guardMargin, 90.f - (60.f + 600.f * 0.005f), 1e-3f));
		CHECK(Near(g.guardMargin, g.best.margin, 1e-3f));
	}
	// a spinner whose bet runs more than 180 past what was seen: the guard follows the spin, it does
	// not wrap back the short way (1500 deg/s x 0.15 s = 225 degrees)
	{
		const YawEstimate e = fEst(0.f, 1500.f, 2, 0.15f, 5.f);   // guard at +90 (the edge), short way -54 (inside)
		const float pGuard = TurnGuarded(0.f, 0.f, e, 0.3f).pGuard;
		CHECK(Near(pGuard, StabProbability(0.f, 0.f, 90.f, 5.f, 0.3f).p, 1e-5f));
		CHECK(pGuard < 0.6f);
	}
	// Estimate's seen is the newest snapshot, whatever estimator wins
	{
		Track tr; Reset(tr);
		Drive(tr, [](float t) { return 200.f * t; }, 0.f, 0.8f);
		float tLast = 0.f;   // Drive's own loop: the newest snapshot it fed
		for (float t = 0.f; t < 0.8f - 1e-4f; t += k_tick)
			tLast = t;
		const YawEstimate e = Estimate(tr, 0.8f + 0.1f);
		CHECK(e.k == 2 && Near(e.seen, Wrap(Quant10(200.f * tLast)), 1e-3f) && std::fabs(Wrap(e.centre - e.seen)) > 15.f);
		CHECK(e.sigmaCore <= e.sigma && e.sigmaCore >= k_sigmaFloor);
	}
}

// CLOSED LOOP: bs15 (decide on the bet) vs bs16 (decide on the guarded odds), same timelines.
// His yaw is a human-shaped process on the absolute clock (min-jerk flicks, spy checks that come
// back, pans, spinners); snapshots every tick at 80 ms ping through the real Track / Estimate; each
// policy gets one swing per 3 s episode, judged by the server's window on his yaw at the due time.
// We stand on his bearing aiming at him (Legit), so the backstab is |his yaw - bearing| < 90.
struct YawProcess
{
	std::vector<float> y;
	static constexpr float dt = 0.001f;
	float At(float t) const
	{
		const float x = std::max(t, 0.f) / dt;
		const size_t i = std::min(size_t(x), y.size() - 2);
		return y[i] + (y[i + 1] - y[i]) * std::min(x - float(i), 1.f);
	}
};
static YawProcess MakeYaw(std::mt19937& rng, float T, int style)
{
	std::uniform_real_distribution<float> u(0.f, 1.f);
	auto U = [&](float a, float b) { return a + (b - a) * u(rng); };
	YawProcess p;
	float Y = U(-180.f, 180.f);
	auto hold = [&](float d, float drift) { for (int n = int(d / p.dt); n-- > 0;) { Y += drift * p.dt; p.y.push_back(Y); } };
	auto flick = [&](float A)   // min-jerk, duration grows with size (Fitts): 90-270 ms
	{
		const float D = 0.07f + std::fabs(A) / 900.f, Y0 = Y;
		for (int n = 1, N = int(D / p.dt); n <= N; n++)
		{
			const float s = float(n) / N;
			Y = Y0 + A * s * s * s * (10.f + s * (-15.f + 6.f * s));
			p.y.push_back(Y);
		}
	};
	auto pan = [&](float rate, float d)   // 40 ms ramps at both ends
	{
		for (int n = 0, N = int(d / p.dt); n < N; n++)
		{
			const float t = n * p.dt, ramp = std::min({ 1.f, t / 0.04f, (d - t) / 0.04f });
			Y += rate * ramp * p.dt;
			p.y.push_back(Y);
		}
	};
	const float spin = (u(rng) < 0.5f ? -1.f : 1.f) * U(400.f, 1000.f);
	while (p.y.size() * p.dt < T)
	{
		const float sgn = u(rng) < 0.5f ? -1.f : 1.f;
		switch (style)
		{
		case 0: hold(U(0.15f, 1.0f), U(-15.f, 15.f)); flick(sgn * U(20.f, 180.f)); break;               // looker
		case 1: { hold(U(0.1f, 0.6f), 0.f); const float A = sgn * U(120.f, 200.f); flick(A);            // spy checker
			hold(U(0.05f, 0.3f), 0.f); flick(-A * U(0.85f, 1.15f)); break; }
		case 2: pan(sgn * U(60.f, 250.f), U(0.2f, 1.f)); hold(U(0.1f, 0.5f), 0.f); break;               // panner
		default: pan(spin, 1.f); break;                                                                 // spinner
		}
	}
	return p;
}
struct PolicyStats { int fires = 0, stabs = 0, fronts = 0, possible = 0, captured = 0; double latency = 0.0; int latN = 0; };
static void RunEpisodes(int style, int episodes, uint32_t seed, float ping, PolicyStats& oldS, PolicyStats& newS, double& pairedDelta, int& pairedN)
{
	std::mt19937 rng(seed);
	const float epLen = 3.f, warm = 1.f;
	const YawProcess yaw = MakeYaw(rng, warm + episodes * epLen + 1.f, style);
	std::uniform_real_distribution<float> jit(-0.5f * k_tick, 0.5f * k_tick);
	Track tr; Reset(tr);
	auto inside = [&](float t, float m) { return std::fabs(Wrap(yaw.At(t))) < 90.f - m; };
	float t = 0.f;
	for (; t < warm; t += k_tick)
	{
		AddSnapshot(tr, t, t, Wrap(Quant10(yaw.At(t))), {}, { -24, -24, 0 }, { 24, 24, 82 });
		PushPrediction(tr, t + k_tick + ping);
	}
	for (int ep = 0; ep < episodes; ep++)
	{
		const float t0 = warm + ep * epLen;
		struct P { bool fired = false; int held = 0; float fireT = -1.f; bool ok = false; } po, pn;
		float firstOpen = -1.f; int run = 0;
		for (t = t0; t < t0 + epLen - 1e-4f; t += k_tick)
		{
			AddSnapshot(tr, t, t, Wrap(Quant10(yaw.At(t))), {}, { -24, -24, 0 }, { 24, 24, 82 });
			const float tDue = t + k_tick + ping;
			PushPrediction(tr, tDue);
			// a clear chance: 10 degrees inside for 4 ticks running
			run = inside(tDue, 10.f) ? run + 1 : 0;
			if (run == 4 && firstOpen < 0.f)
				firstOpen = tDue - 3 * k_tick;
			const YawEstimate e = Estimate(tr, tDue), eN = Estimate(tr, tDue + k_tick);
			const float truth = yaw.At(tDue + jit(rng));
			auto step = [&](P& s, float p, float pNext)
			{
				if (s.fired)
					return;
				const EDecision d = Decide(p, pNext, true, s.held);
				if (d == DEC_HOLD) { ++s.held; return; }
				s.held = 0;
				if (d != DEC_FIRE)
					return;
				s.fired = true; s.fireT = tDue; s.ok = std::fabs(Wrap(truth)) < 90.f;
			};
			step(po, StabProbability(0.f, 0.f, e.centre, e.sigma, 0.3f).p, StabProbability(0.f, 0.f, eN.centre, eN.sigma, 0.3f).p);
			step(pn, TurnGuarded(0.f, 0.f, e, 0.3f).p, TurnGuarded(0.f, 0.f, eN, 0.3f, k_tick).p);
		}
		auto tally = [&](PolicyStats& s, const P& p)
		{
			s.possible += firstOpen >= 0.f;
			if (!p.fired) return;
			++s.fires;
			if (!p.ok) { ++s.fronts; return; }
			++s.stabs;
			if (firstOpen >= 0.f) { ++s.captured; s.latency += p.fireT - firstOpen; ++s.latN; }
		};
		tally(oldS, po); tally(newS, pn);
		if (po.fired && po.ok && pn.fired && pn.ok) { pairedDelta += pn.fireT - po.fireT; ++pairedN; }
	}
}
static void TestTurnGuardClosedLoop()
{
	const char* names[] = { "looker", "checker", "panner", "spinner" };
	for (float ping : { 0.03f, 0.08f, 0.15f })
	{
		PolicyStats human[2];
		for (int style = 0; style < 4; style++)
		{
			PolicyStats o, n; double dl = 0.0; int dn = 0;
			// seeds 7000+: NOT the sweep's (1000+) that picked k_turnTrust, so this is out of sample
			RunEpisodes(style, 300, 7000u + style + int(ping * 1000.f), ping, o, n, dl, dn);
			std::printf("  %-7s %3.0f ms | bs15 stabs %3d fronts %3d (%4.1f%%) | bs16 stabs %3d fronts %3d (%4.1f%%) | bs16 %+3.0f ms on %d shared stabs\n",
				names[style], ping * 1000.f, o.stabs, o.fronts, o.fires ? 100.f * o.fronts / o.fires : 0.f,
				n.stabs, n.fronts, n.fires ? 100.f * n.fronts / n.fires : 0.f, dn ? 1000.0 * dl / dn : 0.0, dn);
			if (style < 3)
				for (int i = 0; i < 2; i++)
				{
					PolicyStats& h = human[i]; const PolicyStats& s = i ? n : o;
					h.fires += s.fires; h.stabs += s.stabs; h.fronts += s.fronts; h.possible += s.possible; h.captured += s.captured;
				}
			if (style == 0 || style == 2)
			{	// lookers and panners: what the guard is for, at every ping
				CHECK(n.fronts < o.fronts);
				CHECK(n.stabs >= o.stabs);
			}
			else if (style == 1)
			{	// spy checkers: the KNOWN CEILING (bs_math.h TurnGuarded). At 150 ms his back shows for
				// less than the horizon and bs16 fronts slightly MORE than bs15 did; bounded here so it
				// cannot quietly grow
				CHECK(n.fronts <= 1.15f * o.fronts + 3);
				CHECK(n.stabs >= 0.9f * o.stabs);
			}
			else
			{	// the guard must not lock a spinner out -- the first bs16 draft (seen yaw on the full
				// sigma) held one off for ~0.8 s at 150 ms ping. Within half the ping of bs15.
				CHECK(n.captured >= 0.9f * n.possible);
				CHECK(dn > 0 && dl / dn < 0.5 * ping);
				CHECK(n.fronts <= o.fronts + 3);
			}
		}
		std::printf("  ping %3.0f ms, humans: bs15 %d stabs / %d fronts, bs16 %d stabs / %d fronts\n", ping * 1000.f,
			human[0].stabs, human[0].fronts, human[1].stabs, human[1].fronts);
		CHECK(human[1].fronts <= 0.75f * human[0].fronts);   // the defect the live log showed, cut
		CHECK(human[1].stabs >= human[0].stabs);             // ... and no stab given away for it
	}
}

int main()
{
	TestWrap();
	TestWindowsMatchServer();
	TestOddsCalibrated();
	TestDecisions();
	TestReach();
	TestLerp();
	TestTrack();
	TestRewind();
	TestChoke();
	TestTurnGuard();
	TestTurnGuardClosedLoop();
	std::printf("%s: %d checks, %d failed\n", s_failed ? "FAIL" : "PASS", s_checks, s_failed);
	return s_failed ? 1 : 0;
}
