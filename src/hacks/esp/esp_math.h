#pragma once
// Pure math for Unit C's ESP overlays -- no SDK/engine types, so it is unit-testable standalone
// (tests/esp_math_test.cpp). Kept out of esp.cpp on purpose: esp.cpp needs the aim SDK headers,
// which pull in engine types this math has no business depending on.
#include <cmath>

namespace EspMath
{
	// Out-of-fov arrow, Amalgam Features/Visuals/OffscreenArrows COffscreenArrows::DrawArrowTo.
	// (sx,sy) = target's W2S(bAlways) screen point. Arrow sits `offset` px from screen center
	// along center->target, clamped inside the screen rect inset by `scale`; triangle of side
	// `scale`, tip pointing outward (toward the target).
	struct OofArrow { float x, y; float tip[2], a[2], b[2]; };
	inline OofArrow OofArrowFromScreen(float screenW, float screenH, float sx, float sy, float offset, float scale)
	{
		const float cx = screenW * 0.5f, cy = screenH * 0.5f;
		float dx = sx - cx, dy = sy - cy;
		if (dx == 0.f && dy == 0.f) dx = 0.0001f;   // Amalgam VectorAngles(0) = yaw 0; any finite dir
		const float len = sqrtf(dx * dx + dy * dy);
		const float ux = dx / len, uy = dy / len;

		float px = offset * ux, py = offset * uy;
		const float limX = cx - scale, limY = cy - scale;
		if (fabsf(px) > limX || fabsf(py) > limY)
		{
			const float kx = px != 0.f ? limX / fabsf(px) : 1e9f;
			const float ky = py != 0.f ? limY / fabsf(py) : 1e9f;
			const float k = kx < ky ? kx : ky;
			px *= k; py *= k;
		}

		OofArrow o;
		o.x = cx + px; o.y = cy + py;
		// Amalgam rotates v1 (0, s/2), v2 (0, -s/2), v3 (-s*sqrt3/2, 0) by the yaw of (center - screen),
		// whose cos/sin are (-ux, -uy): v3 lands on the far side, i.e. toward the target.
		const float c = -ux, s = -uy;
		auto Rot = [&](float vx, float vy, float* out) { out[0] = o.x + vx * c - vy * s; out[1] = o.y + vy * c + vx * s; };
		Rot(0.f, scale * 0.5f, o.a);
		Rot(0.f, -scale * 0.5f, o.b);
		Rot(-scale * 0.8660254f, 0.f, o.tip);
		return o;
	}

	// Amalgam DrawArrowTo alpha, in-front targets only: 0 while the screen point is within
	// min(cx,cy) of center, 1 at max(cx,cy), linear between.
	inline float OofScreenFade(float screenW, float screenH, float sx, float sy)
	{
		const float cx = screenW * 0.5f, cy = screenH * 0.5f;
		const float flMin = cx < cy ? cx : cy, flMax = cx < cy ? cy : cx;
		const float dist = sqrtf((sx - cx) * (sx - cx) + (sy - cy) * (sy - cy));
		const float t = (dist - flMin) / (flMin != flMax ? flMax - flMin : 1.f);
		return t < 0.f ? 0.f : t > 1.f ? 1.f : t;
	}

	// Amalgam DrawArrowTo world-distance alpha: RemapVal(dist, max, max*0.9, 0, 1) clamped.
	inline float OofDistFade(float dist, float maxDist)
	{
		if (maxDist <= 0.f) return 0.f;
		const float t = (maxDist - dist) / (maxDist * 0.1f);
		return t < 0.f ? 0.f : t > 1.f ? 1.f : t;
	}

	// Screen box from SDK::IsOnScreen-style bounds. flTop = MAX screen y (box bottom on screen),
	// flBottom = MIN screen y (box top) -- Amalgam ESP.cpp:1309 y = flBottom, h = flTop - flBottom.
	// Using y = flTop gave h < 0 on every player = "esp draws nothing" (10-31 fix).
	struct Box { int x, y, w, h; };
	inline Box BoxFromBounds(float flLeft, float flRight, float flTop, float flBottom)
	{
		return { (int)flLeft, (int)flBottom, (int)(flRight - flLeft), (int)(flTop - flBottom) };
	}

	// Distance fade: 1 at/under `near`, 0 at/over `far`, linear between. far <= near disables fade.
	inline float DistanceFadeAlpha(float dist, float nearD, float farD)
	{
		if (farD <= nearD) return 1.f;
		if (dist <= nearD) return 1.f;
		if (dist >= farD) return 0.f;
		return 1.f - (dist - nearD) / (farD - nearD);
	}
}
