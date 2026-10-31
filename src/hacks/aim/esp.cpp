#include "esp.h"
#include "backtrack.h"
#include "../esp/esp_math.h"
#include "../esp/esp_fx.h"
#include "../esp/esp_queue.h"
#include "../esp/esp_vars.h"
#include "../visuals/chams_layers.h"   // ChamsLayers::SoundAlpha = botox n_sound_esp::alpha
#include "../hud/avatar_pure.h"        // n_avatar::FriendIdToSteam64 -- plain arithmetic, no Steam/d3d9 types
#include <cfloat>
#include <cstdio>

void DebugLog(const char* fmt, ...);   // dllmain.cpp
bool Chams_SoundStamp(int entindex, float& ms, float* origin);   // aim/chams.cpp, botox g_sound_esp store

// ============================================================================================
// botox hacks/visuals/players/players.cpp, line for line where TF2 allows. TF2 departures:
//   - bones: TF2 skeletons have non-hitbox bones between hitbox ones, so an edge runs to the
//     nearest hitbox ANCESTOR (botox: direct parent; CS:GO's breast-bone fixups are CS bone ids)
//   - hitboxes are boxes in TF2, not capsules: drawn as their 12 edges
//   - weapon name: TF2 weapon-type table (no localized hud name to read); weapon icon: no TF2
//     icon font, row not ported
//   - flags: the TF2 condition set (EspFlagNames), same plain / alert colouring
//   - frustum test: SDK::IsOnScreen (botox: engine cull_box)
//   - no team avatars for bots (TF2 has no T/CT portrait textures); bots draw no avatar
// ============================================================================================

namespace
{
	inline float FontH() { return g_espQFontH.load(std::memory_order_relaxed); }
	inline uint32_t Black(float a) { return EspColRGBA(0.f, 0.f, 0.f, a); }

	struct SBox { int l, t, r, b, w, h; };

	// c_base_entity::get_bounding_box: the 8 collision-box corners, every one in front of the camera
	bool ScreenBox(const Vec3& o, const Vec3& mn, const Vec3& mx, SBox& box)
	{
		float l = FLT_MAX, t = FLT_MAX, r = -FLT_MAX, b = -FLT_MAX;
		for (int i = 0; i < 8; i++)
		{
			const Vec3 p = { o.x + ((i & 1) ? mx.x : mn.x), o.y + ((i & 2) ? mx.y : mn.y), o.z + ((i & 4) ? mx.z : mn.z) };
			Vec3 s;
			if (!SDK::W2S(p, s))
				return false;
			l = std::min(l, s.x); t = std::min(t, s.y); r = std::max(r, s.x); b = std::max(b, s.y);
		}
		box = { (int)l, (int)t, (int)r, (int)b, (int)r - (int)l, (int)b - (int)t };
		return true;
	}

	// g_utilities.truncate_utf8: first n code points
	std::string TruncateUtf8(const char* s, size_t n)
	{
		size_t i = 0, cps = 0;
		while (s[i] && cps < n)
		{
			const unsigned char c = (unsigned char)s[i];
			i += c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
			++cps;
		}
		std::string out(s, s + i);
		if (s[i])
			out += "...";
		return out;
	}

	const char* WeaponName(int iWeaponID)
	{
		// ETFWeaponType order (aim/defs.h), generated from the enum; lower case like botox's towlower
		static const char* const k[] = {
			"none", "bat", "sandman", "bottle", "fire axe", "club", "crowbar", "knife", "fists", "shovel", "wrench", "bonesaw",
			"shotgun", "shotgun", "shotgun", "shotgun", "scattergun", "sniper rifle", "minigun", "smg", "syringe gun", "tranq",
			"rocket launcher", "grenade launcher", "stickybomb launcher", "flamethrower", "grenade normal", "grenade concussion",
			"grenade nail", "grenade mirv", "grenade mirv demoman", "grenade napalm", "grenade gas", "grenade emp",
			"grenade caltrop", "grenade pipebomb", "grenade smoke bomb", "grenade heal", "grenade stunball", "grenade jar",
			"grenade jar milk", "pistol", "pistol", "revolver", "nailgun", "pda", "build pda", "destroy pda", "disguise kit",
			"builder", "medigun", "grenade mirvbomb", "flamethrower rocket", "grenade demoman", "sentry bullet", "sentry rocket",
			"dispenser", "invis watch", "flare gun", "lunchbox", "jarate", "bow", "banner", "pumpkin bomb", "sword",
			"direct hit", "lifeline", "wrangler", "dispenser gun", "frontier justice", "mad milk", "shortstop", "fish",
			"crusader's crossbow", "caber", "winger", "soda popper", "bazaar bargain", "righteous bison", "cow mangler",
			"short circuit", "pomson", "wrap assassin", "grenade ornament ball", "manmelter", "baby face blaster", "cleaver",
			"grenade cleaver", "sticky ball launcher", "grenade sticky ball", "rescue ranger", "loose cannon", "throwable",
			"grenade throwable", "sapper", "grenade waterballoon", "saw", "spellbook", "spellbook projectile", "classic",
			"parachute", "grappling hook", "passtime gun", "cleaner's carbine", "sign", "thermal thruster", "slap", "gas passer",
			"grenade jar gas", "dragon's fury" };
		static_assert(sizeof(k) / sizeof(k[0]) == TF_WEAPON_FLAME_BALL + 1, "weapon table out of step with ETFWeaponType");
		return iWeaponID > 0 && iWeaponID <= TF_WEAPON_FLAME_BALL ? k[iWeaponID] : nullptr;
	}

	// mstudiobone_t (studio.h): parent at +0x4, flags at +0xA0, stride 0xD8. No pointers: x86 == x64.
	int BoneParent(const studiohdr_t* h, int i) { return *reinterpret_cast<const int*>(reinterpret_cast<const byte*>(h) + h->boneindex + i * 0xD8 + 0x4); }
	int BoneFlags(const studiohdr_t* h, int i)  { return *reinterpret_cast<const int*>(reinterpret_cast<const byte*>(h) + h->boneindex + i * 0xD8 + 0xA0); }
	Vec3 BonePos(const matrix3x4* m, int b) { return { m[b][0][3], m[b][1][3], m[b][2][3] }; }

	// screen-space polyline from world points, split where a point goes behind the camera (never bridged)
	struct Lines
	{
		std::vector<std::vector<float>> v;
		std::vector<float> cur;
		void Add(const Vec3& w) { Vec3 s; if (SDK::W2S(w, s)) { cur.push_back(s.x); cur.push_back(s.y); } else Flush(); }
		void Flush() { if (cur.size() >= 4) v.push_back(cur); cur.clear(); }
		void Push(uint32_t col, float thick) { Flush(); for (auto& l : v) EspQ::Polyline(l.data(), int(l.size() / 2), col, thick); }
	};

	// push_smooth_skeleton: chains of degree-2 nodes become centripetal Catmull-Rom splines
	struct SkelEdge { int child, parent; Vec3 cpos, ppos; };
	Vec3 SkeletonSpline(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float u)
	{
		auto knot = [](const Vec3& a, const Vec3& b) { return std::max(std::sqrt((b - a).Length()), 1e-3f); };
		auto mix = [](const Vec3& a, const Vec3& b, float ta, float tb, float t) { return a * ((tb - t) / (tb - ta)) + b * ((t - ta) / (tb - ta)); };
		const float t1 = knot(p0, p1), t2 = t1 + knot(p1, p2), t3 = t2 + knot(p2, p3);
		const float t = t1 + (t2 - t1) * u;
		const Vec3 a1 = mix(p0, p1, 0.f, t1, t), a2 = mix(p1, p2, t1, t2, t), a3 = mix(p2, p3, t2, t3, t);
		return mix(mix(a1, a2, 0.f, t2, t), mix(a2, a3, t1, t3, t), t1, t2, t);
	}
	void SmoothSkeleton(const std::vector<SkelEdge>& edges, uint32_t col, float thick)
	{
		constexpr int k_nodes = MAXSTUDIOBONES + 1, k_samples = 8;
		int degree[k_nodes] = {};
		for (const auto& e : edges) degree[e.child]++, degree[e.parent]++;
		std::vector<bool> used(edges.size(), false);
		Lines lines;
		auto emit = [&](const std::vector<Vec3>& p)
		{
			const int m = int(p.size());
			for (int j = 0; j + 1 < m; j++)
			{
				const Vec3 p0 = j > 0 ? p[j - 1] : p[0] * 2.f - p[1];
				const Vec3 p3 = j + 2 < m ? p[j + 2] : p[m - 1] * 2.f - p[m - 2];
				for (int s = 0; s < k_samples; s++)
					lines.Add(SkeletonSpline(p0, p[j], p[j + 1], p3, float(s) / k_samples));
			}
			lines.Add(p[m - 1]);
			lines.Flush();
		};
		for (int pass = 0; pass < 2; pass++)
			for (size_t first = 0; first < edges.size(); first++)
			{
				if (used[first]) continue;
				const auto& e0 = edges[first];
				const bool fromChild = degree[e0.child] != 2;
				if (pass == 0 && !fromChild && degree[e0.parent] == 2) continue;
				int node = fromChild || pass == 1 ? e0.child : e0.parent;
				std::vector<Vec3> chain{ node == e0.child ? e0.cpos : e0.ppos };
				for (size_t cur = first;;)
				{
					used[cur] = true;
					const auto& e = edges[cur];
					const bool fwd = e.child == node;
					node = fwd ? e.parent : e.child;
					chain.push_back(fwd ? e.ppos : e.cpos);
					if (degree[node] != 2) break;
					size_t next = edges.size();
					for (size_t k = 0; k < edges.size(); k++)
						if (!used[k] && (edges[k].child == node || edges[k].parent == node)) next = k;
					if (next == edges.size()) break;
					cur = next;
				}
				emit(chain);
			}
		const float c = thick;
		for (auto& l : lines.v)
		{
			EspQ::Polyline(l.data(), int(l.size() / 2), col, c);
			if (c > 1.5f)
			{
				EspQ::Circle(l[0], l[1], c * 0.5f, col, -1.f);   // thick < 0 = filled (round caps)
				EspQ::Circle(l[l.size() - 2], l[l.size() - 1], c * 0.5f, col, -1.f);
			}
		}
	}

	// TF2 hitboxes: 8 world corners each, through the bone matrix
	void HitboxCorners(const studiohdr_t* pHdr, const matrix3x4* aBones, std::vector<std::array<Vec3, 8>>& out)
	{
		auto pSet = pHdr->pHitboxSet(0);
		if (!pSet) return;
		for (int i = 0; i < pSet->numhitboxes; i++)
		{
			auto pBox = pSet->pHitbox(i);
			if (!pBox || pBox->bone < 0 || pBox->bone >= MAXSTUDIOBONES) continue;
			std::array<Vec3, 8> c;
			for (int k = 0; k < 8; k++)
				Math::VectorTransform(Vec3((k & 1) ? pBox->bbmax.x : pBox->bbmin.x, (k & 2) ? pBox->bbmax.y : pBox->bbmin.y,
					(k & 4) ? pBox->bbmax.z : pBox->bbmin.z), aBones[pBox->bone], c[k]);
			out.push_back(c);
		}
	}
	constexpr int k_boxEdges[12][2] = { { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 }, { 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
	void PushHitboxes(const std::vector<std::array<Vec3, 8>>& boxes, uint32_t col, float thick)
	{
		for (const auto& c : boxes)
		{
			Vec3 s[8]; bool ok[8];
			for (int k = 0; k < 8; k++) ok[k] = SDK::W2S(c[k], s[k]);
			for (const auto& e : k_boxEdges)
				if (ok[e[0]] && ok[e[1]])
					EspQ::Line(s[e[0]].x, s[e[0]].y, s[e[1]].x, s[e[1]].y, col, thick);
		}
	}

	// botox players() per-entity layout state: box, fade, the four padding stacks, rounding
	struct Part
	{
		SBox box = {};
		float a = 1.f;
		float pad[4] = {};   // e_padding_direction: left, top, right, bottom
		float round = 0.f;
		Vec3 vOrigin, vMins, vMaxs;
	};
	enum { PAD_LEFT, PAD_TOP, PAD_RIGHT, PAD_BOTTOM };

	void PartRounding(Part& p, bool boxOn)   // :747-754
	{
		p.round = EspMath::BoxRounding(g_espBoxRounding, boxOn, g_espBoxCorner, p.box.r - p.box.l, p.box.b - p.box.t);
	}

	void PartFill(const Part& p)   // :756-772
	{
		const auto& b = p.box;
		if (g_espBoxFillGradient)
			EspQ::Grad((float)b.l, (float)b.t, (float)b.r, (float)b.b, EspCol(g_espBoxFillColor, p.a), EspCol(g_espBoxFillBottomColor, p.a), p.round);
		else
			EspQ::Rect((float)b.l, (float)b.t, (float)b.r, (float)b.b, EspCol(g_espBoxFillColor, p.a), 0, true, p.round, 1.f, 0);
	}

	void PartBox(const Part& p, const float* col)   // :774-855
	{
		const auto& b = p.box;
		const uint8_t outline = !g_espBoxOutline ? 0
			: g_espBoxOutlineSide == 0 ? ESPF_OUT_INNER : g_espBoxOutlineSide == 1 ? ESPF_OUT_OUTER : uint8_t(ESPF_OUT_INNER | ESPF_OUT_OUTER);
		const float oth = std::max(g_espBoxOutlineThickness, 1.f);
		const float th = EspMath::BoxThickness(g_espBoxThickness, float(b.r - b.l), float(b.b - b.t));
		const uint32_t c = EspCol(col, p.a), oc = EspCol(g_espBoxOutlineColor, p.a);

		if (g_espBox3d)
		{
			Vec3 s[8];
			for (int i = 0; i < 8; i++)
			{
				const Vec3 w = { p.vOrigin.x + ((i & 1) ? p.vMaxs.x : p.vMins.x), p.vOrigin.y + ((i & 2) ? p.vMaxs.y : p.vMins.y),
					p.vOrigin.z + ((i & 4) ? p.vMaxs.z : p.vMins.z) };
				if (!SDK::W2S(w, s[i])) return;
			}
			if (outline)
				for (const auto& e : k_boxEdges)
					EspQ::Line(s[e[0]].x, s[e[0]].y, s[e[1]].x, s[e[1]].y, oc, th + oth * 2.f);
			for (const auto& e : k_boxEdges)
				EspQ::Line(s[e[0]].x, s[e[0]].y, s[e[1]].x, s[e[1]].y, c, th);
		}
		else if (!g_espBoxCorner)
			EspQ::Rect((float)b.l, (float)b.t, (float)b.r, (float)b.b, c, oc, false, p.round, th, outline, oth);
		else
		{
			const float inset = (th - 1.f) * 0.5f, cr = std::max(p.round - inset, 0.f);
			if (outline)
				EspQ::CornerRect(b.l + inset, b.t + inset, b.r - inset, b.b - inset, oc, th + oth, cr);
			EspQ::CornerRect(b.l + inset, b.t + inset, b.r - inset, b.b - inset, c, th, cr);
		}
	}

	// :860-928. Returns the name's top y (the name avatar centres on it), NAN if not drawn.
	float PartName(Part& p, const std::string& s, const float* col)
	{
		const auto& b = p.box;
		const float h = FontH();
		const float x = b.l + b.w * 0.5f;
		float y;
		switch (g_espNamePosition)
		{
		case 1:  y = b.b + 2 + p.pad[PAD_BOTTOM]; break;
		case 2:  y = b.t + 2.f; break;
		case 3:  y = b.b - 2 - h; break;
		default: y = b.t - 3 - h; break;
		}
		EspQ::Text(x, y, s.c_str(), EspCol(col, p.a), Black(p.a), 0.5f);
		if (g_espNamePosition == 0) p.pad[PAD_TOP] -= h - 20;
		else if (g_espNamePosition == 1) p.pad[PAD_BOTTOM] += h + 1.f;
		return y;
	}

	// below-the-box text row (health number standalone, weapon name, distance): centred, stacks
	void PartBottomText(Part& p, const char* s, uint32_t col)
	{
		EspQ::Text(p.box.l + p.box.w * 0.5f, p.box.b + 2 + p.pad[PAD_BOTTOM], s, col, Black(p.a), 0.5f);
		p.pad[PAD_BOTTOM] += FontH() + 1.f;
	}

	void PartDistance(Part& p, float dist)   // :1077-1105
	{
		char t[16];
		switch (g_espDistanceUnit)
		{
		case 1:  snprintf(t, sizeof(t), "%.0fu", dist); break;
		case 2:  snprintf(t, sizeof(t), "%.0fft", dist / 16.f); break;
		default: snprintf(t, sizeof(t), "%.0fm", dist / 52.49f); break;
		}
		PartBottomText(p, t, EspCol(g_espDistanceColor, p.a));
	}

	struct FlagRow { const char* label; bool alert; };
	void PartFlags(const Part& p, const FlagRow* rows, int n)   // :1160-1175
	{
		float y = (float)p.box.t;
		for (int i = 0; i < n; i++)
		{
			EspQ::Text(p.box.r + 4.f, y, rows[i].label, EspCol(rows[i].alert ? g_espFlagsAlertColor : g_espFlagsColor, p.a), Black(p.a));
			y += FontH() + 1.f;
		}
	}

	// :1178-1407. factor = animated health / max; hp = the number text. boxOn = botox m_players_box,
	// followText = health number on, style "follow health bar".
	void PartHealthBar(const Part& p, float factor, const char* hp, bool boxOn, bool followText)
	{
		const auto& b = p.box;
		float hue[4] = { 0, 0, 0, 1 };
		EspMath::Hsb(EspMath::HealthHue(factor), hue);
		const float th = g_espHealthBarThickness, h = FontH();
		const bool outline = g_espHealthBarOutline;
		const float white[4] = { 1, 1, 1, 1 };
		const uint32_t followCol = EspCol(g_espHealthTextCustomColor ? g_espHealthTextColor : white, p.a);

		if (g_espHealthBarSide == 4)
		{
			const int bw = b.r - b.l, bh = b.b - b.t;
			const float cornerSize = std::clamp(g_espHealthBarCornerSize, 0.1f, 1.f);
			const float evenArm = boxOn ? (float)std::min(bw / 3, bh / 5) : (float)std::min(bw, bh) * cornerSize;
			const bool rideBox = boxOn && g_espBoxCorner && !g_espBox3d;
			const float armX = rideBox ? (float)(bw / 3) : evenArm, armY = rideBox ? (float)(bh / 5) : evenArm;
			const float baseRound = boxOn ? p.round
				: std::max(g_espHealthBarCornerRounding, 0.f) * std::min((float)(b.b - b.t) / 200.f, 1.f);
			const int pick = std::clamp(g_espHealthBarCornerPick, 0, 3);
			const auto bar = EspMath::CornerBarPaths((float)b.l, (float)b.t, (float)b.r, (float)b.b, armX, armY, baseRound, th, pick, factor);

			float yTop = FLT_MAX, yBot = -FLT_MAX;
			for (const auto& q : bar.track) { yTop = std::min(yTop, q.y); yBot = std::max(yBot, q.y); }
			yTop -= th * 0.5f; yBot += th * 0.5f;

			auto flat = [](const std::vector<EspMath::V2>& v) { std::vector<float> f; for (auto& q : v) { f.push_back(q.x); f.push_back(q.y); } return f; };
			if (outline) { auto o = flat(bar.outer); EspQ::Polyline(o.data(), int(bar.outer.size()), Black(p.a), th + 2.f); }
			{ auto t = flat(bar.track); EspQ::Polyline(t.data(), int(bar.track.size()), EspCol(g_espHealthBarBgColor, p.a), th); }
			if (bar.fill.size() >= 2)
			{
				auto f = flat(bar.fill);
				const uint32_t fillCol = EspCol(g_espHealthBarCustomColor ? g_espHealthBarColor : hue, p.a);
				if (g_espHealthBarCustomColor && g_espHealthBarGradient)
					EspQ::PolylineGrad(f.data(), int(bar.fill.size()), fillCol, EspCol(g_espHealthBarBottomColor, p.a), th, yTop, yBot);
				else
					EspQ::Polyline(f.data(), int(bar.fill.size()), fillCol, th);
			}
			if (followText)
			{
				const auto tip = EspMath::CornerBarTextAnchor(bar, (float)b.l, (float)b.t, (float)b.r, (float)b.b, th, outline, pick);
				const bool flipX = pick & 1, flipY = pick >= 2;
				EspQ::Text(tip.x, flipY ? tip.y - h : tip.y, hp, followCol, Black(p.a), flipX ? 0.f : 1.f);
			}
			return;
		}

		const int side = g_espHealthBarSide;
		const bool vertical = side == 0 || side == 1;
		float x0, y0, x1, y1;
		switch (side)
		{
		case 1:  x0 = b.r + 3.f; y0 = (float)b.t; x1 = b.r + 3.f + th; y1 = (float)b.b; break;
		case 2:  x0 = (float)b.l; y0 = b.t - 3.f - th; x1 = (float)b.r; y1 = b.t - 3.f; break;
		case 3:  x0 = (float)b.l; y0 = b.b + 3.f; x1 = (float)b.r; y1 = b.b + 3.f + th; break;
		default: x0 = b.l - 3.f - th; y0 = (float)b.t; x1 = b.l - 3.f; y1 = (float)b.b; break;
		}
		float fx0 = x0, fy0 = y0, fx1 = x1, fy1 = y1;
		if (vertical) fy0 = y1 - (y1 - y0) * factor;
		else          fx1 = x0 + (x1 - x0) * factor;

		const uint8_t ofl = outline ? ESPF_OUT_OUTER : 0;
		const bool gradient = g_espHealthBarCustomColor && g_espHealthBarGradient;
		const uint32_t top = EspCol(g_espHealthBarColor, p.a);
		EspQ::Rect(x0, y0, x1, y1, EspCol(g_espHealthBarBgColor, p.a), Black(p.a), true, 0.f, 1.f, ofl);
		EspQ::Rect(fx0, fy0, fx1, fy1, gradient ? 0u : g_espHealthBarCustomColor ? top : EspCol(hue, p.a), Black(p.a), true, 0.f, 1.f, ofl);
		if (gradient)
		{	// gradient pinned to the track, the fill reveals it
			const float t = std::clamp((fy0 - y0) / std::max(y1 - y0, 1.f), 0.f, 1.f);
			const float* tc = g_espHealthBarColor; const float* bc = g_espHealthBarBottomColor;
			const float mixed[4] = { tc[0] + (bc[0] - tc[0]) * t, tc[1] + (bc[1] - tc[1]) * t, tc[2] + (bc[2] - tc[2]) * t, tc[3] + (bc[3] - tc[3]) * t };
			EspQ::Grad(fx0, fy0, fx1, fy1, EspCol(mixed, p.a), EspCol(bc, p.a));
		}
		if (followText)
		{
			if (vertical)
				EspQ::Text(side == 1 ? x1 + 2.f : x0 - 2.f, fy0, hp, followCol, Black(p.a), side == 1 ? 0.f : 1.f);
			else
				EspQ::Text(fx1, y0 - h, hp, followCol, Black(p.a), 0.5f);
		}
	}

	// botox n_sound_esp::player_gate, on the shared chams sound stamps
	float SoundGate(CTFPlayer* pPlayer, CTFPlayer* pLocal)
	{
		if (!g_espSoundOnly || pPlayer->m_iTeamNum() == pLocal->m_iTeamNum())
			return 1.f;
		float ms, o[3];
		if (!Chams_SoundStamp(pPlayer->entindex(), ms, o))
			return 0.f;
		if (g_chamsSoundMaxDistance > 0.f && (Vec3(o[0], o[1], o[2]) - pLocal->GetAbsOrigin()).Length() > g_chamsSoundMaxDistance)
			return 0.f;
		return ChamsLayers::SoundAlpha((EspFx_NowMs() - ms) / 1000.f, g_espSoundDuration, g_espSoundFade);
	}

	uint64_t SteamId(int iIndex)
	{
		player_info_t tInfo;
		if (!I::EngineClient->GetPlayerInfo(iIndex, &tInfo) || tInfo.fakeplayer)
			return 0;
		return n_avatar::FriendIdToSteam64(tInfo.friendsID);
	}

	// players() :629-701 / sound_arrows: ring position round the screen centre
	EspMath::V2 RingPos(float x, float y)
	{
		const float W = (float)H::Draw.m_nScreenW, Hh = (float)H::Draw.m_nScreenH;
		const float ring = (float)g_espOofDistance;
		const float rx = g_espOofAspect && Hh > 0.f ? ring * (W / Hh) : ring;
		return { W * 0.5f + x * rx, Hh * 0.5f + y * ring };
	}
	void PushRingArrow(EspMath::V2 pos, float x, float y, uint32_t col)
	{
		const bool interium = g_espOofStyle == 1;
		const auto a = EspMath::RingArrowPoints(pos, x, y, g_espOofWidth, g_espOofHeight, interium);
		const float pts[8] = { a.p[0].x, a.p[0].y, a.p[1].x, a.p[1].y, a.p[2].x, a.p[2].y, a.p[3].x, a.p[3].y };
		if (!interium)
		{
			EspQ::Poly(pts, 4, col);
			return;
		}
		// pyramid seen from above minus its back face: see-through notched arrow, rim, ridge notch -> tip
		const uint32_t alpha = (uint32_t)(((col >> 24) & 0xffu) * 0.45f);
		EspQ::Poly(pts, 4, (col & 0x00ffffffu) | (alpha << 24), true, col, 1.5f);
		EspQ::Line(a.p[2].x, a.p[2].y, a.p[0].x, a.p[0].y, col, 1.5f);
	}
}

void CEsp::OnPlayerHurt(CTFPlayer* pVictim, int iDamage)
{
	if (!g_espEnable || iDamage <= 0)
		return;
	const int iVictim = pVictim->entindex();
	const float flNow = I::GlobalVars->realtime;

	// botox hitboxes_on_hurt
	if (g_espHitboxes && g_espHitboxesMode == 1)
	{
		matrix3x4 aBones[MAXSTUDIOBONES];
		const auto pModel = pVictim->GetModel();
		const auto pHdr = pModel ? I::ModelInfoClient->GetStudiomodel(pModel) : nullptr;
		if (pHdr && pVictim->SetupBones(aBones, MAXSTUDIOBONES, BONE_USED_BY_HITBOX, I::GlobalVars->curtime))
		{
			HitBoxes t; t.flTime = flNow;
			HitboxCorners(pHdr, aBones, t.vBoxes);
			if (!t.vBoxes.empty())
			{
				if (m_vHitBoxes.size() >= 16) m_vHitBoxes.erase(m_vHitBoxes.begin());
				m_vHitBoxes.push_back(std::move(t));
			}
		}
	}

	// botox on_player_hurt (damage origin: TF2's player_hurt has no hit group -> botox's fallback, origin + 50)
	if (!g_espDamageNumbers)
		return;
	if (g_espDmgStack)
		for (auto& e : m_vDamage)
			if (e.iVictim == iVictim && flNow - e.flTime <= 0.35f)
			{
				e.iDamage += iDamage;
				e.flTime = flNow;
				return;
			}
	if (m_vDamage.size() >= 32) m_vDamage.erase(m_vDamage.begin());
	Vec3 vOrigin = pVictim->GetAbsOrigin(); vOrigin.z += 50.f;
	m_vDamage.push_back({ vOrigin, iDamage, iVictim, flNow });
}

void CEsp::DrawDamageNumbers()   // botox damage_numbers
{
	if (m_vDamage.empty()) return;
	if (!g_espDamageNumbers) { m_vDamage.clear(); return; }
	const float flNow = I::GlobalVars->realtime, h = FontH();
	char t[16];
	for (auto it = m_vDamage.begin(); it != m_vDamage.end();)
	{
		const float el = flNow - it->flTime;
		if (el < 0.f || el >= g_espDmgDuration || g_espDmgDuration <= 0.f) { it = m_vDamage.erase(it); continue; }
		Vec3 w = it->vOrigin; w.z += el * g_espDmgSpeed;
		Vec3 s;
		if (SDK::W2S(w, s))
		{
			const float a = EspMath::LifeAlpha(el, g_espDmgDuration);
			snprintf(t, sizeof(t), "%d", it->iDamage);
			EspQ::Text(s.x, s.y - h * 0.5f, t, EspCol(g_espDmgColor, a), Black(a), 0.5f);
		}
		++it;
	}
}

void CEsp::DrawHitHitboxes()   // botox hit_hitboxes
{
	if (m_vHitBoxes.empty()) return;
	if (!g_espHitboxes || g_espHitboxesMode != 1) { m_vHitBoxes.clear(); return; }
	const float dur = std::max(g_espHitboxesDuration, 0.1f), flNow = I::GlobalVars->realtime;
	for (auto it = m_vHitBoxes.begin(); it != m_vHitBoxes.end();)
	{
		const float el = flNow - it->flTime;
		if (el < 0.f || el >= dur) { it = m_vHitBoxes.erase(it); continue; }
		PushHitboxes(it->vBoxes, EspCol(g_espHitboxColor, EspMath::LifeAlpha(el, dur)), g_espHitboxesThickness);
		++it;
	}
}

void CEsp::DrawPlayers(CTFPlayer* pLocal)
{
	const Vec3 vView = I::EngineClient->GetViewAngles();
	const float flYaw = DEG2RAD(vView.y);
	const Vec3 vLocalOrigin = pLocal->GetAbsOrigin(), vEye = pLocal->GetEyePosition();
	const float h = FontH();
	static TickRecord s_aRecords[24];

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer || pPlayer == pLocal || !pPlayer->IsAlive() || pPlayer->IsDormant())
			continue;
		const bool bTeam = pPlayer->m_iTeamNum() == pLocal->m_iTeamNum();
		if (bTeam && !g_espTeammates)
			continue;
		const int iIndex = pPlayer->entindex();
		if (iIndex <= 0 || iIndex > 64)
			continue;

		const Vec3 vOrigin = pPlayer->GetAbsOrigin();
		const float flDist = vLocalOrigin.DistTo(vOrigin);
		float a = EspMath::DistanceAlpha(flDist, g_espMaxDistance, g_espDistanceFade) * SoundGate(pPlayer, pLocal);
		if (a <= 0.f)
			continue;
		a = std::min(a, 1.f);

		// visibility, traced once on first ask (botox is_player_visible: our eye -> his networked eye)
		int iVis = -1;
		auto visible = [&]() { if (iVis < 0) iVis = SDK::VisPos(pLocal, pPlayer, vEye, vOrigin + pPlayer->m_vecViewOffset()) ? 1 : 0; return iVis == 1; };

		// ---- out of fov arrows (players() :628-701) ----
		if (g_espOofArrows && g_espOofType != 1 && !SDK::IsOnScreen(pPlayer, vOrigin))
		{
			const Vec3 d = vLocalOrigin - vOrigin;
			const auto dir = EspMath::OofDir(flYaw, d.x, d.y);
			const uint32_t col = EspCol(visible() ? g_espOofColor : g_espOofWallColor, a);
			const auto pos = RingPos(dir.x, dir.y);
			const uint64_t sid = g_espOofAvatar ? SteamId(iIndex) : 0;
			if (sid)
			{
				const float size = std::min(g_espOofWidth, g_espOofHeight), r = size * 0.5f;
				EspQ::Avatar(pos.x - r, pos.y - r, pos.x + r, pos.y + r, sid, a, r);
				EspQ::Circle(pos.x, pos.y, r, col, 1.5f);
			}
			else
				PushRingArrow(pos, dir.x, dir.y, col);
		}

		Part p;
		p.a = a; p.vOrigin = vOrigin; p.vMins = pPlayer->m_vecMins(); p.vMaxs = pPlayer->m_vecMaxs();
		if (!ScreenBox(vOrigin, p.vMins, p.vMaxs, p.box))
			continue;
		++m_nDrawn;
		const auto& b = p.box;

		const int iHealth = pPlayer->m_iHealth();
		int iMaxHealth = 0;
		if (auto pResource = H::Entities.GetResource())
			iMaxHealth = pResource->m_iMaxHealth(iIndex);
		if (iMaxHealth <= 0) iMaxHealth = std::max(iHealth, 1);

		char hp[16];
		snprintf(hp, sizeof(hp), g_espHealthSuffix ? "%dhp" : "%d", iHealth);
		m_flAnimHealth[iIndex] = EspMath::AnimateHealth(m_flAnimHealth[iIndex], (float)iHealth, I::GlobalVars->frametime);
		// TF2 overheal goes past max: the bar tops out full (botox has no overheal)
		const float factor = std::clamp(m_flAnimHealth[iIndex] / (float)iMaxHealth, 0.f, 1.f);
		float hue[4] = { 0, 0, 0, 1 };
		EspMath::Hsb(EspMath::HealthHue(factor), hue);

		PartRounding(p, g_espBox);
		if (g_espBoxFill)
			PartFill(p);
		if (g_espBox)
			PartBox(p, g_espBoxVisColors ? (visible() ? g_espBoxVisibleColor : g_espBoxInvisibleColor) : g_espBoxColor);

		const uint64_t sid = g_espAvatar ? SteamId(iIndex) : 0;
		if (g_espName)
		{
			std::string sName;
			if (auto pResource = H::Entities.GetResource())
				if (const char* n = pResource->GetName(iIndex))
					sName = TruncateUtf8(n, 24);
			player_info_t tInfo;
			if (I::EngineClient->GetPlayerInfo(iIndex, &tInfo) && tInfo.fakeplayer)
				sName.insert(0, "[bot] ");
			const float y = PartName(p, sName, g_espNameVisColors ? (visible() ? g_espNameVisibleColor : g_espNameInvisibleColor) : g_espNameColor);
			if (sid && g_espAvatarPosition == 0 && !sName.empty())   // an empty name queues no text to sit left of
			{
				const float size = g_espAvatarSize;
				const float ay = y + (h - size) * 0.5f;
				EspQ::Avatar(-size - 3.f, ay, -3.f, ay + size, sid, a, size * 0.5f, true);
			}
		}

		if (g_espHealthText && g_espHealthTextStyle == 0)
			PartBottomText(p, hp, EspCol(g_espHealthTextCustomColor ? g_espHealthTextColor : hue, a));

		if (auto pWeapon = pPlayer->m_hActiveWeapon()->As<CTFWeaponBase>())
		{
			// :989-1021 ammo bar: guns only (TF2: a weapon with a clip)
			const int iMaxClip = pWeapon->GetMaxClip1();
			if (g_espAmmoBar && iMaxClip > 0 && pWeapon->m_iClip1() >= 0)
			{
				const float ammo = (float)pWeapon->m_iClip1();
				m_flAmmo[iIndex] = m_flAmmo[iIndex] > ammo ? m_flAmmo[iIndex] - 2.f * I::GlobalVars->frametime : ammo;
				const float f = std::clamp(m_flAmmo[iIndex] / (float)iMaxClip, 0.f, 1.f);
				EspQ::Rect((float)b.l, b.b + 3.f, (float)(b.l + b.w), b.b + 5.f, Black(a), Black(a), false, 0.f, 1.f, ESPF_OUT_OUTER);
				EspQ::Rect((float)b.l, b.b + 3.f, b.l + b.w * f, b.b + 5.f, EspCol(g_espAmmoBarColor, a), Black(a), false, 0.f, 1.f, ESPF_OUT_OUTER);
				p.pad[PAD_BOTTOM] += 6.f;
			}
			if (g_espWeaponName)
				if (const char* w = WeaponName(pWeapon->GetWeaponID()))
					PartBottomText(p, w, EspCol(g_espWeaponNameColor, a));
		}

		if (g_espDistance)
			PartDistance(p, flDist);

		if (g_espFlags)
		{
			const bool bOn[EF_COUNT] = {
				pPlayer->IsInvulnerable(), pPlayer->IsCritBoosted() || pPlayer->IsMiniCritBoosted(), pPlayer->InCond(TF_COND_STEALTHED),
				pPlayer->InCond(TF_COND_DISGUISED), pPlayer->InCond(TF_COND_ZOOMED), pPlayer->InCond(TF_COND_TAUNTING),
				pPlayer->InCond(TF_COND_BURNING), pPlayer->InCond(TF_COND_BLEEDING), pPlayer->InCond(TF_COND_URINE),
				pPlayer->InCond(TF_COND_MAD_MILK), pPlayer->InCond(TF_COND_MARKEDFORDEATH), iHealth > iMaxHealth,
				pPlayer->InCond(TF_COND_PHASE), pPlayer->InCond(TF_COND_SHIELD_CHARGE) };
			// alert = what changes whether you take the fight right now
			static const bool kAlert[EF_COUNT] = { true, true, false, false, false, false, false, false, false, false, false, false, true, true };
			FlagRow rows[EF_COUNT]; int n = 0;
			for (int i = 0; i < EF_COUNT; i++)
				if ((g_espFlagList & (1 << i)) && bOn[i])
					rows[n++] = { EspFlagNames()[i], kAlert[i] };
			PartFlags(p, rows, n);
		}

		if (g_espHealthBar)
			PartHealthBar(p, factor, hp, g_espBox, g_espHealthText && g_espHealthTextStyle == 1);

		// ---- bones: skeleton, hitboxes, trail, head avatar ----
		const bool bWantBones = g_espSkeleton || g_espHitboxes && g_espHitboxesMode == 0 || g_espBacktrackTrail || sid && g_espAvatarPosition == 3;
		const auto pModel = bWantBones ? pPlayer->GetModel() : nullptr;
		const auto pHdr = pModel ? I::ModelInfoClient->GetStudiomodel(pModel) : nullptr;
		matrix3x4 aBones[MAXSTUDIOBONES];
		if (pHdr && pPlayer->SetupBones(aBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime))
		{
			const int nRecords = g_espSkeleton && g_espSkeletonType == 1 || g_espBacktrackTrail
				? F::Backtrack.GetRecords(pPlayer, s_aRecords, int(sizeof(s_aRecords) / sizeof(s_aRecords[0]))) : 0;

			if (g_espSkeleton)   // :1409-1511
			{
				const uint32_t col = EspCol(g_espSkeletonVisColors ? (visible() ? g_espSkeletonVisibleColor : g_espSkeletonInvisibleColor) : g_espSkeletonColor, a);
				// lag compensated = the oldest record still inside the window (records come newest first)
				const matrix3x4* m = g_espSkeletonType == 1 && nRecords > 0 ? s_aRecords[nRecords - 1].m_aBones : aBones;
				const int nBones = std::min(pHdr->numbones, (int)MAXSTUDIOBONES);
				std::vector<SkelEdge> edges;
				for (int i = 0; i < nBones; i++)
				{
					if (!(BoneFlags(pHdr, i) & BONE_USED_BY_HITBOX)) continue;
					int par = BoneParent(pHdr, i);
					while (par >= 0 && par < nBones && !(BoneFlags(pHdr, par) & BONE_USED_BY_HITBOX))
						par = BoneParent(pHdr, par);
					if (par < 0 || par >= nBones) continue;
					const Vec3 c = BonePos(m, i), pp = BonePos(m, par);
					if (g_espSkeletonType == 2)
					{
						edges.push_back({ i, par, c, pp });
						continue;
					}
					Vec3 s0, s1;
					if (SDK::W2S(c, s0) && SDK::W2S(pp, s1))
						EspQ::Line(s0.x, s0.y, s1.x, s1.y, col, g_espSkeletonThickness);
				}
				if (g_espSkeletonType == 2)
					SmoothSkeleton(edges, col, g_espSkeletonThickness);
			}

			if (g_espHitboxes && g_espHitboxesMode == 0)
			{
				std::vector<std::array<Vec3, 8>> boxes;
				HitboxCorners(pHdr, aBones, boxes);
				PushHitboxes(boxes, EspCol(g_espHitboxColor, a), g_espHitboxesThickness);
			}


			if (g_espBacktrackTrail)   // :1517-1539, head of every record
				if (auto pSet = pHdr->pHitboxSet(0))
					if (auto pBox = pSet->numhitboxes > 0 ? pSet->pHitbox(0) : nullptr)
						for (int r = 0; r < nRecords; r++)
						{
							Vec3 w, s;
							Math::VectorTransform((pBox->bbmin + pBox->bbmax) * 0.5f, s_aRecords[r].m_aBones[pBox->bone], w);
							if (SDK::W2S(w, s))
								EspQ::Rect(s.x, s.y, s.x + 1, s.y + 1, EspColRGBA(1.f, 1.f, 1.f, a), 0, true, 0.f, 4.f, 0);
						}

			auto pHeadSet = pHdr->pHitboxSet(0);
			auto pHead = pHeadSet && pHeadSet->numhitboxes > 0 ? pHeadSet->pHitbox(0) : nullptr;   // TF2 hitbox 0 = head
			if (sid && g_espAvatarPosition == 3 && pHead && pHead->bone >= 0 && pHead->bone < MAXSTUDIOBONES)   // :1549-1583 cover head
			{
				// TF2 hitboxes have no radius: half the head box's longest side
				const Vec3 e = pHead->bbmax - pHead->bbmin;
				const float flHeadR = std::max(std::max(e.x, e.y), e.z) * 0.5f;
				auto pBox = pHead;
				Vec3 vHead, vRight, sh, se;
				Math::VectorTransform((pBox->bbmin + pBox->bbmax) * 0.5f, aBones[pBox->bone], vHead);
				Math::AngleVectors(vView, nullptr, &vRight);
				if (SDK::W2S(vHead, sh) && SDK::W2S(vHead + vRight * flHeadR, se))
				{
					const float r = fabsf(se.x - sh.x) * g_espAvatarHeadScale;
					if (r > 0.5f)
						EspQ::Avatar(sh.x - r, sh.y - r, sh.x + r, sh.y + r, sid, a, r);
				}
			}
		}

		if (sid && (g_espAvatarPosition == 1 || g_espAvatarPosition == 2))   // :1584-1601 above / below box
		{
			const float size = g_espAvatarSize;
			const bool below = g_espAvatarPosition == 2;
			const float x = b.l + (b.w - size) * 0.5f;
			const float y = below ? b.b + 2 + p.pad[PAD_BOTTOM] : b.t - 5 - size - p.pad[PAD_TOP];
			EspQ::Avatar(x, y, x + size, y + size, sid, a, size * 0.5f);
		}
	}
}

void CEsp::DrawSoundArrows(CTFPlayer* pLocal)   // botox sound_arrows
{
	if (!g_espOofArrows || g_espOofType == 0)
		return;
	const float flYaw = DEG2RAD(I::EngineClient->GetViewAngles().y);
	const Vec3 vLocal = pLocal->GetAbsOrigin();
	const float W = (float)H::Draw.m_nScreenW, Hh = (float)H::Draw.m_nScreenH;
	const float flNow = EspFx_NowMs();
	for (int i = 1; i <= 64; i++)
	{
		float ms, o[3];
		if (i == pLocal->entindex() || !Chams_SoundStamp(i, ms, o))
			continue;
		auto pClient = I::ClientEntityList->GetClientEntity(i);
		auto pEnt = pClient ? pClient->As<CBaseEntity>() : nullptr;
		if (!pEnt || pEnt->m_iTeamNum() == pLocal->m_iTeamNum())
			continue;   // botox think(): enemies only
		const Vec3 vSound(o[0], o[1], o[2]);
		if (g_chamsSoundMaxDistance > 0.f && vLocal.DistTo(vSound) > g_chamsSoundMaxDistance)
			continue;
		const float a = ChamsLayers::SoundAlpha((flNow - ms) / 1000.f, g_espOofSoundDuration, g_espOofSoundFade);
		if (a <= 0.f)
			continue;
		Vec3 s;
		if (SDK::W2S(vSound, s) && s.x >= 0.f && s.y >= 0.f && s.x <= W && s.y <= Hh)
			continue;
		const Vec3 d = vLocal - vSound;
		const auto dir = EspMath::OofDir(flYaw, d.x, d.y);
		PushRingArrow(RingPos(dir.x, dir.y), dir.x, dir.y, EspCol(g_espOofSoundColor, a));
	}
}

// Buildings: TF2's take on botox's "edicts" group. Same box / health bar / text stack as players
// (one look for the whole esp), but enemy and friendly each pick their own parts and colour.
void CEsp::DrawBuildings(CTFPlayer* pLocal)
{
	if (!g_espBuildings && !g_espBldTeam)
		return;
	const Vec3 vLocalOrigin = pLocal->GetAbsOrigin();
	const float h = FontH();
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingAll))
	{
		auto pObj = pEntity->As<CBaseObject>();
		if (!pObj || pObj->IsDormant() || pObj->m_bPlacing() || pObj->m_bCarried())
			continue;
		const bool bTeam = pObj->m_iTeamNum() == pLocal->m_iTeamNum();
		if (bTeam ? !g_espBldTeam : !g_espBuildings)
			continue;
		if (bTeam && g_espBldTeamOwnOnly && pObj->m_hBuilder().Get() != pLocal)
			continue;
		const bool bBox = bTeam ? g_espBldTeamBox : g_espBldEnemyBox, bName = bTeam ? g_espBldTeamName : g_espBldEnemyName;
		const bool bBar = bTeam ? g_espBldTeamHealthBar : g_espBldEnemyHealthBar, bText = bTeam ? g_espBldTeamHealthText : g_espBldEnemyHealthText;
		const bool bFlags = bTeam ? g_espBldTeamFlags : g_espBldEnemyFlags, bDist = bTeam ? g_espBldTeamDistance : g_espBldEnemyDistance;
		const float* col = bTeam ? g_espBldTeamColor : g_espBuildingColor;

		const Vec3 vOrigin = pObj->GetAbsOrigin();
		const float flDist = vLocalOrigin.DistTo(vOrigin);
		const float a = EspMath::DistanceAlpha(flDist, g_espMaxDistance, g_espDistanceFade);
		if (a <= 0.f)
			continue;

		Part p;
		p.a = a; p.vOrigin = vOrigin; p.vMins = pObj->m_vecMins(); p.vMaxs = pObj->m_vecMaxs();
		if (!ScreenBox(vOrigin, p.vMins, p.vMaxs, p.box))
			continue;
		const int iIdx = pObj->entindex();
		if (iIdx <= 0 || iIdx >= 2048)
			continue;

		const int iHealth = pObj->m_iHealth(), iMax = std::max(pObj->m_iMaxHealth(), 1);
		m_flAnimHealthBld[iIdx] = EspMath::AnimateHealth(m_flAnimHealthBld[iIdx], (float)iHealth, I::GlobalVars->frametime);
		const float factor = std::clamp(m_flAnimHealthBld[iIdx] / (float)iMax, 0.f, 1.f);
		char hp[16];
		snprintf(hp, sizeof(hp), g_espHealthSuffix ? "%dhp" : "%d", iHealth);

		// the player group's box settings decide corner / 3d / outline / rounding; "box" here only gates it
		PartRounding(p, bBox);
		if (bBox)
			PartBox(p, col);

		if (bName)
		{
			const char* sType = pObj->IsSentrygun() ? (pObj->m_bMiniBuilding() ? "mini sentry" : "sentry")
				: pObj->IsDispenser() ? "dispenser" : pObj->IsTeleporter() ? "teleporter" : "building";
			char sName[48];
			if (pObj->m_bMiniBuilding()) snprintf(sName, sizeof(sName), "%s", sType);
			else snprintf(sName, sizeof(sName), "%s lv%d", sType, std::clamp(pObj->m_iUpgradeLevel(), 1, 3));
			EspQ::Text(p.box.l + p.box.w * 0.5f, p.box.t - 3 - h, sName, EspCol(col, a), Black(a), 0.5f);
		}
		if (bText && g_espHealthTextStyle == 0)
		{
			float hue[4] = { 0, 0, 0, 1 };
			EspMath::Hsb(EspMath::HealthHue(factor), hue);
			PartBottomText(p, hp, EspCol(g_espHealthTextCustomColor ? g_espHealthTextColor : hue, a));
		}
		if (bDist)
			PartDistance(p, flDist);
		if (bFlags)
		{
			char sBuild[24], sMetal[24];
			FlagRow rows[6]; int n = 0;
			if (pObj->m_bHasSapper())                      rows[n++] = { "sapped", true };
			if (pObj->m_bDisabled() || pObj->m_bPlasmaDisable()) rows[n++] = { "disabled", true };
			if (pObj->m_bBuilding())
			{
				snprintf(sBuild, sizeof(sBuild), "building %d%%", (int)(pObj->m_flPercentageConstructed() * 100.f));
				rows[n++] = { sBuild, false };
			}
			else if (!pObj->m_bMiniBuilding() && pObj->m_iUpgradeLevel() < 3 && pObj->m_iUpgradeMetalRequired() > 0)
			{
				snprintf(sMetal, sizeof(sMetal), "%d/%d metal", pObj->m_iUpgradeMetal(), pObj->m_iUpgradeMetalRequired());
				rows[n++] = { sMetal, false };
			}
			if (pObj->IsSentrygun())
			{
				int iShells, iMaxShells, iRockets, iMaxRockets;
				pObj->As<CObjectSentrygun>()->GetAmmoCount(iShells, iMaxShells, iRockets, iMaxRockets);
				if (iMaxShells > 0 && iShells * 4 < iMaxShells) rows[n++] = { "low ammo", false };
			}
			PartFlags(p, rows, n);
		}
		if (bBar)
			PartHealthBar(p, factor, hp, bBox, bText && g_espHealthTextStyle == 1);
	}
}

// ---- world entities: vgui surface, unchanged look -------------------------------------------
static Color_t ToColor(const float* c)
{
	return { (byte)(c[0] * 255.f + 0.5f), (byte)(c[1] * 255.f + 0.5f), (byte)(c[2] * 255.f + 0.5f), (byte)(c[3] * 255.f + 0.5f) };
}

// World-space line in SCREEN space: this runs off IEngineVGui_Paint (2D surface pass), where the
// IRenderUtils 3D calls draw nothing.
static void Line3D(const Vec3& a, const Vec3& b, Color_t c)
{
	Vec3 sa, sb;
	if (SDK::W2S(a, sa) && SDK::W2S(b, sb))
		H::Draw.Line((int)sa.x, (int)sa.y, (int)sb.x, (int)sb.y, c);
}

static void WorldBoxName(CBaseEntity* pEntity, const char* sName, Color_t tColor)
{
	SBox b;
	if (!ScreenBox(pEntity->GetAbsOrigin(), pEntity->m_vecMins(), pEntity->m_vecMaxs(), b) || b.w <= 0 || b.h <= 0)
		return;
	H::Draw.LineCornerRectOutline(b.l, b.t, b.w, b.h, tColor);
	H::Draw.StringOutlined(H::Fonts.GetFont(FONT_INDICATORS), b.l + b.w / 2, b.t - 2, tColor, { 0, 0, 0, tColor.a }, ALIGN_BOTTOM, sName);
}

// Ammo packs / dropped weapons are not grouped by CEntities (helpers.cpp Store), so this walks
// the entity list itself; projectiles ride the WorldProjectile group.
void CEsp::DrawWorld(CTFPlayer* pLocal)
{
	if (g_espAmmoPacks || g_espDroppedWeapons)
	{
		const int iHighest = I::ClientEntityList->GetHighestEntityIndex();
		for (int i = 1; i <= iHighest; i++)
		{
			auto pClient = I::ClientEntityList->GetClientEntity(i);
			auto pBase = pClient ? pClient->As<CBaseEntity>() : nullptr;
			if (!pBase || pBase->IsDormant()) continue;
			const bool bAmmo = pBase->GetClassID() == ETFClassID::CTFAmmoPack;
			const bool bDropped = pBase->GetClassID() == ETFClassID::CTFDroppedWeapon;
			if (bAmmo && g_espAmmoPacks || bDropped && g_espDroppedWeapons)
				WorldBoxName(pBase, bAmmo ? "Ammo" : "Weapon", ToColor(g_espItemColor));
		}
	}
	if (g_espProjectiles)
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldProjectile))
			if (!pEntity->IsDormant())
				WorldBoxName(pEntity, "Projectile", ToColor(g_espProjColor));
}

// Projectile path draw: F::ProjSim (aim/projsim.h), the same physics the projectile aimbot aims with.
void CEsp::DrawProjectilePaths(CTFPlayer* pLocal)
{
	if (!g_espProjPath) return;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldProjectile))
	{
		if (pEntity->IsDormant())
			continue;
		ProjectileInfo tInfo;
		F::ProjSim.GetInfo(pEntity, tInfo);
		if (!F::ProjSim.Initialize(tInfo))
			continue;
		// cap, not tInfo.m_flLifetime (60s default): a path worth drawing leaves view or detonates by then
		for (int n = 1; n <= 128; n++)
			F::ProjSim.RunTick(tInfo);
		const Color_t tColor = ToColor(g_espProjPathColor);
		for (size_t i = 1; i < tInfo.m_vPath.size(); i++)
			Line3D(tInfo.m_vPath[i - 1], tInfo.m_vPath[i], tColor);
	}
}

// Sound pings: pushed from visuals.cpp's EmitSound hook, copied out under esp_fx.h's lock.
void CEsp::DrawSoundPings()
{
	if (!g_espSoundPings) return;
	EspFxSoundPing pings[kEspFxMaxSoundPings];
	const float flNow = EspFx_NowMs();
	constexpr float flLifeMs = 600.f;
	const int n = EspFx_CopySoundPings(pings, kEspFxMaxSoundPings, flNow, flLifeMs);
	for (int i = 0; i < n; i++)
	{
		const float flAge = (flNow - pings[i].bornMs) / flLifeMs;   // 0..1
		const Vec3 vCenter(pings[i].x, pings[i].y, pings[i].z);
		const float flRadius = 4.f + flAge * 20.f;
		Color_t tColor = ToColor(g_espSoundColor);
		tColor.a = (byte)(tColor.a * (1.f - flAge));
		constexpr int kSeg = 16;
		for (int s = 0; s < kSeg; s++)
		{
			const float a0 = s * (6.2831853f / kSeg), a1 = (s + 1) * (6.2831853f / kSeg);
			Line3D(vCenter + Vec3(cosf(a0) * flRadius, sinf(a0) * flRadius, 0.f),
			       vCenter + Vec3(cosf(a1) * flRadius, sinf(a1) * flRadius, 0.f), tColor);
		}
	}
}

void CEsp::Draw(CTFPlayer* pLocal)
{
	EspQ::Begin();
	if (pLocal)
	{
		static bool s_bLoggedCall = false, s_bLoggedDrawn = false;
		if (!s_bLoggedCall && g_espEnable)
		{
			s_bLoggedCall = true;
			DebugLog("esp: Draw reached (players group=%d, screen %dx%d)",
				int(H::Entities.GetGroup(EntityEnum::PlayerAll).size()), H::Draw.m_nScreenW, H::Draw.m_nScreenH);
		}

		m_nDrawn = 0;
		if (g_espEnable)
		{
			DrawPlayers(pLocal);
			DrawSoundArrows(pLocal);
			DrawDamageNumbers();
			DrawHitHitboxes();
		}
		else
			m_vDamage.clear(), m_vHitBoxes.clear();
		DrawBuildings(pLocal);
		if (!s_bLoggedDrawn && m_nDrawn > 0)
		{
			s_bLoggedDrawn = true;
			DebugLog("esp: queued %d players this frame (%d cmds)", m_nDrawn, int(g_espQBuild.cmds.size()));
		}

		if (g_espWorldEnable)
			DrawWorld(pLocal);
		DrawProjectilePaths(pLocal);
		DrawSoundPings();
	}
	EspQ::Commit(GetTickCount64(), (float)H::Draw.m_nScreenW, (float)H::Draw.m_nScreenH);
}
