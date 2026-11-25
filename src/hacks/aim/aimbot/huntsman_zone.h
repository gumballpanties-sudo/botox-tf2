#pragma once
// HUNTSMAN HEADSHOT ZONE -- which hitbox the SERVER gives an arrow, and which aim heights land on
// the head (tests/huntsman_zone_test.cpp). Port of CTFProjectile_Arrow::ArrowTouch
// (sdk server/tf/tf_projectile_arrow.cpp 800-844):
//   1. arrow (hull 1) touches the player bbox at E.
//   2. direct: line E -> E + vel*frametime vs hitbox OBBs, nearest wins. 27u at min charge, 39u full.
//   3. else fallback: start = E + launchFwd*16 (arrow angles never turn after launch), per hitbox
//      ray start -> bone origin vs UNROTATED box bone origin + bbmin..bbmax, smallest dist wins,
//      ties -> lower index.
// From behind E is the back face, 24u + head lean from the head: step 2 runs short, step 3's start
// sits next to the spine boxes. Old aim (lerp 50% toward bbox top) leaned on step 3 = body shot.
// Pure floats, no SDK, so the test includes it.
#include <cmath>

namespace hszone
{
struct V { float x, y, z; };
inline V operator+(V a, V b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline V operator-(V a, V b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline V operator*(V a, float s) { return { a.x * s, a.y * s, a.z * s }; }
inline float Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Len(V a) { return std::sqrt(Dot(a, a)); }
inline float Axis(V a, int i) { return i == 0 ? a.x : i == 1 ? a.y : a.z; }

struct Box
{
	float m[3][4];  // bone -> world (matrix3x4 layout)
	V mins, maxs;   // pBox->bbmin / bbmax
	bool head;      // pBox->group == HITGROUP_HEAD (1)
};

// collisionutils.cpp IntersectRayWithBox(BoxTraceInfo_t) port, tolerance 0.
// returns fraction of delta where endpos lands: inside = 0, hit = t1, miss = -1.
inline float SegBox(V s, V d, V lo, V hi)
{
	float t1 = -1.f, t2 = 1.f;
	bool startsolid = true;
	for (int i = 0; i < 6; i++)
	{
		float d1, d2;
		if (i >= 3) { d1 = Axis(s, i - 3) - Axis(hi, i - 3); d2 = d1 + Axis(d, i - 3); }
		else        { d1 = -Axis(s, i) + Axis(lo, i);       d2 = d1 - Axis(d, i); }
		if (d1 > 0 && d2 > 0) return -1.f;
		if (d1 <= 0 && d2 <= 0) continue;
		if (d1 > 0) startsolid = false;
		const float f = (d1 > d2 ? (d1 > 0 ? d1 : 0.f) : d1) / (d1 - d2);
		if (d1 > d2) { if (f > t1) t1 = f; }
		else if (f < t2) t2 = f;
	}
	if (startsolid) return 0.f;
	return t1 < t2 && t1 >= 0.f ? t1 : -1.f;
}

inline V BoneOrigin(const Box& b) { return { b.m[0][3], b.m[1][3], b.m[2][3] }; }

// segment vs OBB: into bone space (rotation part orthonormal -> transpose = inverse)
inline float SegObb(V s, V d, const Box& b)
{
	const V o = s - BoneOrigin(b);
	const V ls = { b.m[0][0] * o.x + b.m[1][0] * o.y + b.m[2][0] * o.z,
	               b.m[0][1] * o.x + b.m[1][1] * o.y + b.m[2][1] * o.z,
	               b.m[0][2] * o.x + b.m[1][2] * o.y + b.m[2][2] * o.z };
	const V ld = { b.m[0][0] * d.x + b.m[1][0] * d.y + b.m[2][0] * d.z,
	               b.m[0][1] * d.x + b.m[1][1] * d.y + b.m[2][1] * d.z,
	               b.m[0][2] * d.x + b.m[1][2] * d.y + b.m[2][2] * d.z };
	return SegBox(ls, ld, b.mins, b.maxs);
}

// steps 2 + 3. step = arrival velocity * tick interval, fwd = unit launch dir. returns box index.
inline int ServerPick(const Box* pBoxes, int n, V vEntry, V vStep, V vFwd)
{
	int iBest = -1; float flBest = 2.f;
	for (int i = 0; i < n; i++)
	{
		const float t = SegObb(vEntry, vStep, pBoxes[i]);
		if (t >= 0.f && t < flBest) flBest = t, iBest = i;
	}
	if (iBest != -1)
		return iBest;

	const V vStart = vEntry + vFwd * 16.f;
	float flClosest = 99999.f;
	for (int i = 0; i < n; i++)
	{
		const V vPos = BoneOrigin(pBoxes[i]);
		const V vDelta = vPos - vStart;
		const float t = SegBox(vStart, vDelta, vPos + pBoxes[i].mins, vPos + pBoxes[i].maxs);
		const float flDist = Len(vDelta) * (t < 0.f ? 1.f : t);   // miss: endpos = start + delta
		if (flDist < flClosest) flClosest = flDist, iBest = i;
	}
	return iBest;
}

// where a straight line through P along unit dir first touches box lo..hi (grown by hull).
// back-traces 256u from P. false = line never enters (P outside and pointing away).
inline bool Entry(V vP, V vDir, V lo, V hi, float flHull, V& vOut)
{
	const V g = { flHull, flHull, flHull };
	const V vFrom = vP - vDir * 256.f;
	const float t = SegBox(vFrom, vDir * 256.f, lo - g, hi + g);
	if (t < 0.f) return false;
	vOut = vFrom + vDir * (256.f * t);
	return true;
}

// Shot model for one aim point P (world): straight-line approximation of the arc near the target,
// with the arc's launch and arrival directions. vEye = shooter, flSpeed = launch speed,
// flGrav = gravity * 800, flTick = tick interval. true = server gives the head.
inline bool HeadAt(const Box* pBoxes, int n, V lo, V hi, V vEye, V vP, float flSpeed, float flGrav, float flTick)
{
	const V d = vP - vEye;
	const float flDist = Len(d);
	if (flDist < 1.f || flSpeed <= 0.f) return false;
	const float t = flDist / flSpeed;
	const float flDrop = flGrav * t * t * 0.5f;
	const V vArrive = { d.x / t, d.y / t, (d.z - flDrop) / t };
	const V vLaunch = { d.x, d.y, d.z + flDrop };
	const float flA = Len(vArrive), flL = Len(vLaunch);
	V vEntry;
	if (!Entry(vP, vArrive * (1.f / flA), lo, hi, 1.f, vEntry)) return false;
	const int i = ServerPick(pBoxes, n, vEntry, vArrive * flTick, vLaunch * (1.f / flL));
	return i >= 0 && pBoxes[i].head;
}

// Scan aim heights z = flLo..flHi step flStep at (x, y) and return the middle of the longest run
// that the server gives the head: max margin for prediction error either way. false = no run.
inline bool BestZ(const Box* pBoxes, int n, V lo, V hi, V vEye, float x, float y, float flLo, float flHi, float flStep,
	float flSpeed, float flGrav, float flTick, float& flOut, int* pRun = nullptr)
{
	int iRun = 0, iBest = 0; float flRunLo = 0.f, flBestLo = 0.f;
	for (float z = flLo; z <= flHi + 0.001f; z += flStep)
	{
		if (HeadAt(pBoxes, n, lo, hi, vEye, { x, y, z }, flSpeed, flGrav, flTick))
		{
			if (!iRun++) flRunLo = z;
			if (iRun > iBest) iBest = iRun, flBestLo = flRunLo;
		}
		else iRun = 0;
	}
	if (pRun) *pRun = iBest;
	if (!iBest) return false;
	flOut = flBestLo + flStep * (iBest - 1) * 0.5f;
	return true;
}
}
