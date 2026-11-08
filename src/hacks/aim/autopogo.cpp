#include "sdk.h"
#include "aim_global.h"
#include "projsim.h"
#include "../movement/goomba_pure.h"

// Auto air pogo (demoman). Bind held + sticky launcher out. On the ground it puts one sticky by
// your feet for the launch; in the air it shoots stickies out in front of you and blows them
// MID-AIR the moment they push you best, chaining to keep you up. Never touches your movement:
// only IN_ATTACK (fire), IN_ATTACK2 (det) and the release cmd's silent aim (FixMovement keeps
// the wish direction).
//
// Runs from the aimbot, i.e. INSIDE engine prediction (hooks.cpp Ticks.Start): pLocal origin /
// velocity / ground are already "after this cmd", m_nTickBase is this cmd's. So q[0] = us after
// this cmd, q[k] = k cmds later, from our own ballistic + hull-trace sim (MoveSim swaps in an
// averaged velocity, movesim.cpp:154 -- wrong for our own flight).
//
//  fire -- every candidate aim: ProjSim the sticky's whole flight (s[m] = after m physics
//          steps; it steps once per server frame after the usercmds, so at the det cmd m ticks
//          after the release it has stepped m times). It sticks on world (pipebomb.cpp:928).
//          Score the blast on us at every det tick it is armed. Best -> hold IN_ATTACK kHold
//          cmds, release on the next (CTFPipebombLauncher::ItemPostFrame), re-planned with that
//          cmd's random seed.
//  det  -- server arm check is (curtime - m_flCreationTime) >= live time (pipebomblauncher.cpp
//          :551), both = tickbase of the release / det cmd, so armed = ticks since our release,
//          no ping guess. A mid-air sticky's spot is the plan's sim (the client copy lags by
//          interp); a stuck one uses its real spot. Blast runs in the det cmd's PostThink after
//          its movement: "now" = q[0], "next" = q[1]. Det on the peak, or now if the next cmd
//          lands you.
// Untouched radius = 0.85 ramping to 1 over tf_sticky_radius_ramp_time after the arm convar
// (GetDamageRadius, pipebomb.cpp:1429).

extern bool g_autoPogo;
extern int  g_autoPogoKey, g_autoPogoMode;
bool MovementKeyOK(int key, int mode);
void DebugLog(const char* fmt, ...);

namespace
{
	constexpr int   kPath = 132;        // our flight, 2 s
	constexpr int   kFlight = 132;      // sticky sim, 2 s
	constexpr float kMinPush = 150.f;   // up push below this = hp for nothing
	// press held this many cmds, released on the next. 2, not 1: anti-cheat compatibility
	// (hooks.cpp:680) re-adds IN_ATTACK after a 1-cmd press, which ate the planned release and
	// launched a cmd later at your real view.
	constexpr int   kHold = 2;

	struct Shot
	{
		int fireTick = 0, land = -1, arm = 0, best = 0;   // land = step it sticks (-1: still flying at kFlight)
		std::vector<Vec3> vPath;                          // vPath[m - 1] = after m steps (stuck: frozen there)
		float score = 0.f;
		bool seen = false;
	};
	std::vector<Shot> s_vShots;         // our released stickies not yet blown, kept across bind releases
	Shot s_tPressPlan;
	Vec3 s_vPressAng = {};
	int  s_iHold = 0;                   // > 0: our press is charging, cmds left before the release
	// after a det: the push is server-only (no client prediction of blast force), so for ~ping our
	// velocity is still the pre-det one and any plan would aim for a flight that is about to
	// change. Wait until the push shows up in our velocity (or give up after kPushWait).
	constexpr float kPushWait = 0.5f;
	bool  s_bWaitPush = false;
	int   s_iLastDet = 0;
	float s_flDetVz = 0.f, s_flKickZ = 0.f;

	// one "POGO R:" line per bind hold: what every cmd was refused for
	struct Diag { int cmds = 0, waiting = 0, noAtk = 0, plans = 0, cand = 0, noInfo = 0, noReach = 0, fired = 0, dets = 0, lastChance = 0, armedCmds = 0; float bestPush = 0.f, bestDetPush = 0.f; } s_tDiag;
	void DiagFlush()
	{
		const Diag& d = s_tDiag;
		if (d.cmds)
			DebugLog("POGO R: cmds=%d waiting=%d noAtk=%d plans=%d cand=%d noInfo=%d noReach=%d fired=%d dets=%d lastChance=%d armedCmds=%d bestPush=%.0f bestDetPush=%.0f",
				d.cmds, d.waiting, d.noAtk, d.plans, d.cand, d.noInfo, d.noReach, d.fired, d.dets, d.lastChance, d.armedCmds, d.bestPush, d.bestDetPush);
		s_tDiag = {};
	}

	gmb::V GV(const Vec3& v) { return { v.x, v.y, v.z }; }

	// velocity we leave the blast with: up (stay afloat) + along the view, sideways off the
	// view line costs double -- straight lines where you look, not wherever the blast throws you
	constexpr float kSideCost = 2.f;
	float Score(const gmb::Body& me, const gmb::V& push, const Vec3& vFwd)
	{
		const gmb::V v = me.vel + push;
		const float flFwd = v.x * vFwd.x + v.y * vFwd.y, flSide = v.x * -vFwd.y + v.y * vFwd.x;
		return v.z + flFwd - kSideCost * fabsf(flSide);
	}

	struct Blast { float dmg, radius; bool duck; };

	// a sticky m ticks after our release: where it is and how big its blast is
	Vec3 ShotPos(const Shot& t, int m)
	{
		return t.vPath[std::clamp(m, 1, int(t.vPath.size())) - 1];
	}
	float ShotRadius(const Shot& t, const Blast& b, int m)
	{
		if (t.land >= 0 && m >= t.land)
			return b.radius;   // stuck: m_bTouched, full radius
		static auto tf_grenadelauncher_livetime = H::ConVars.FindVar("tf_grenadelauncher_livetime");
		static auto tf_sticky_radius_ramp_time = H::ConVars.FindVar("tf_sticky_radius_ramp_time");
		static auto tf_sticky_airdet_radius = H::ConVars.FindVar("tf_sticky_airdet_radius");
		const float flLive = tf_grenadelauncher_livetime ? tf_grenadelauncher_livetime->GetFloat() : 0.8f;
		const float flRamp = tf_sticky_radius_ramp_time ? tf_sticky_radius_ramp_time->GetFloat() : 2.f;
		const float flAir = tf_sticky_airdet_radius ? tf_sticky_airdet_radius->GetFloat() : 0.85f;
		const float t01 = flRamp > 0.f ? std::clamp((TICKS_TO_TIME(m) - flLive) / flRamp, 0.f, 1.f) : 1.f;
		return b.radius * (flAir + (1.f - flAir) * t01);
	}
	gmb::V ShotPush(const Shot& t, const Blast& b, int m, const gmb::Body& me)
	{
		return gmb::SelfStickyPush(GV(ShotPos(t, m)), me, b.dmg, ShotRadius(t, b, m), b.duck);
	}

	// our flight, assuming you keep the keys + yaw you hold this cmd: air = exact per-tick gravity
	// (goomba_pure.h Rise) + CTFGameMovement::AirMove's AirAccelerate (wish from fmove/smove in the
	// yaw frame, clamped to maxspeed, added speed capped at the 30u air cap, gamemovement.cpp:1705).
	// Walls slide, a floor (normal z > 0.7) lands us, drops off ledges. Ground = CTFGameMovement
	// Friction + WalkMove (tf_gamemovement.cpp:1751): stopspeed bleed, sv_accelerate toward the
	// wish, 2D speed clamped to maxspeed, wish cropped to 1/3 ducked (HandleDuckingSpeedCrop).
	// Without it the sim slid on at landing speed: launch stickies went where you never got and the
	// det waited for a "better later tick" forever (log: armedCmds=38 bestDetPush=927 dets=0).
	// ponytail: no low-wishspeed accel boost / back-speed clamp / surface friction (1) -- add if
	// slow-walk or slide surfaces miss.
	void OurPath(CTFPlayer* pLocal, const CUserCmd* pCmd, gmb::Body* q, bool* pGround)
	{
		static auto sv_gravity = H::ConVars.FindVar("sv_gravity");
		static auto sv_airaccelerate = H::ConVars.FindVar("sv_airaccelerate");
		const float g = sv_gravity ? sv_gravity->GetFloat() : 800.f, dt = TICK_INTERVAL;
		const float flAirAccel = sv_airaccelerate ? sv_airaccelerate->GetFloat() : 10.f;
		static auto sv_accelerate = H::ConVars.FindVar("sv_accelerate");
		static auto sv_friction = H::ConVars.FindVar("sv_friction");
		static auto sv_stopspeed = H::ConVars.FindVar("sv_stopspeed");
		const float flAccel = sv_accelerate ? sv_accelerate->GetFloat() : 10.f;
		const float flFriction = sv_friction ? sv_friction->GetFloat() : 4.f;
		const float flStop = sv_stopspeed ? sv_stopspeed->GetFloat() : 100.f;
		const float flMax = pLocal->m_flMaxspeed();
		const Vec3 vMins = pLocal->m_vecMins(), vMaxs = pLocal->m_vecMaxs();
		Vec3 o = pLocal->m_vecOrigin(), v = pLocal->m_vecVelocity();
		bool bGround = pLocal->m_hGroundEntity();
		if (bGround) v.z = 0.f;

		Vec3 vF, vR, vU; Math::AngleVectors({ 0.f, pCmd->viewangles.y, 0.f }, &vF, &vR, &vU);
		Vec3 vWishDir = { vF.x * pCmd->forwardmove + vR.x * pCmd->sidemove, vF.y * pCmd->forwardmove + vR.y * pCmd->sidemove, 0.f };
		const float flWishRaw = vWishDir.Length();
		if (flWishRaw > 0.f)
			vWishDir = vWishDir * (1.f / flWishRaw);
		const float flWishSpeed = (std::min)(flWishRaw, flMax);
		const float flWishGround = (std::min)(flWishRaw * (pLocal->IsDucking() ? 0.33333333f : 1.f), flMax);

		CTraceFilterWorldAndPropsOnly tFilter = {}; tFilter.pSkip = pLocal;
		for (int k = 0; k < kPath; k++)
		{
			q[k] = { GV(o), GV(v), vMaxs.x, vMaxs.z - vMins.z };
			pGround[k] = bGround;

			if (bGround)
			{
				const float flSpeed = v.Length();
				if (flSpeed >= 0.1f)
					v = v * ((std::max)(flSpeed - (std::max)(flSpeed, flStop) * flFriction * dt, 0.f) / flSpeed);
				const float flAdd = flWishGround - v.Dot(vWishDir);
				if (flWishGround > 0.f && flAdd > 0.f)
					v = v + vWishDir * (std::min)(flAccel * flWishGround * dt, flAdd);
				const float flNew = v.Length();
				if (flNew > flMax)
					v = v * (flMax / flNew);
			}
			else if (flWishSpeed > 0.f)
			{
				const float flAdd = (std::min)(flWishSpeed, 30.f) - v.Dot(vWishDir);
				if (flAdd > 0.f)
					v = v + vWishDir * (std::min)(flAirAccel * flWishSpeed * dt, flAdd);
			}
			Vec3 vEnd = o + v * dt;
			if (!bGround)
				vEnd.z -= 0.5f * g * dt * dt;
			CGameTrace tTrace = {};
			SDK::TraceHull(o, vEnd, vMins, vMaxs, MASK_PLAYERSOLID, &tFilter, &tTrace);
			o = tTrace.endpos;
			if (!bGround)
				v.z -= g * dt;
			if (tTrace.DidHit())
			{
				if (tTrace.plane.normal.z > 0.7f)
					bGround = true, v.z = 0.f;
				else
					v = v - tTrace.plane.normal * v.Dot(tTrace.plane.normal);
			}
			else if (bGround)
			{
				CGameTrace tDown = {};
				SDK::TraceHull(o, o - Vec3(0.f, 0.f, 2.f), vMins, vMaxs, MASK_PLAYERSOLID, &tFilter, &tDown);
				bGround = tDown.DidHit() && tDown.plane.normal.z > 0.7f;
			}
		}
	}

	// release from q[f] (f = 0: this cmd releases, kHold: this cmd presses). Det m cmds after the
	// release = q[f + m]. Only ever planned with no sticky of ours out (see the fire gate).
	// f = 0 knows the launch's random seed (this cmd's number, crithack-predicted): ProjSim then
	// replays the server's FirePipeBomb spread -- +-10 u/s right/up (tf_weaponbase_gun.cpp:687)
	// and the spin -- from that seed (projsim.cpp:54 seed + :461). The press cmd (f = kHold)
	// cannot know the release cmd's seed yet, so it plans the mean launch.
	bool Plan(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, const gmb::Body* q, const bool* pGround, int f,
		const Blast& b, const Vec3& vFwd, const Vec3& vView, Shot& tOut, Vec3& vOut)
	{
		Diag& d = s_tDiag;
		d.plans++;
		static auto tf_grenadelauncher_livetime = H::ConVars.FindVar("tf_grenadelauncher_livetime");
		const float flLive = SDK::AttribHookValue(tf_grenadelauncher_livetime ? tf_grenadelauncher_livetime->GetFloat() : 0.8f, "sticky_arm_time", pWeapon);
		const int   iArm = int(ceilf(flLive / TICK_INTERVAL - 0.01f));
		const Vec3  vShift = Vec3(q[f].org.x - q[0].org.x, q[f].org.y - q[0].org.y, q[f].org.z - q[0].org.z);   // shoot pos rides with us
		const bool  bLaunch = pGround[0];   // on the ground: the launch sticky, a grounded det is the point
		const int   iFlags = ProjSimEnum::Trace | ProjSimEnum::InitCheck | ProjSimEnum::PredictCmdNum | (f ? ProjSimEnum::NoRandomAngles : 0);

		// one aim -> its best det tick + score (-1e9 = no det tick pushes us >= kMinPush)
		Shot tSim;
		auto Eval = [&](const Vec3& vAng, Shot& tRes) -> float
		{
			d.cand++;
			ProjectileInfo tInfo = {};
			if (!F::ProjSim.GetInfo(pLocal, pWeapon, vAng, tInfo, iFlags))
			{
				d.noInfo++;
				return -1e9f;
			}
			tInfo.m_vPos += vShift;
			if (!F::ProjSim.Initialize(tInfo))
			{
				d.noInfo++;
				return -1e9f;
			}

			// whole flight: stuck on world = frozen (+1u off the wall, Explode), props / sky end it
			CGameTrace tTrace = {};
			CTraceFilterCollideable tFilter = {}; tFilter.pSkip = pLocal; tFilter.iPlayer = PLAYER_NONE;
			int nMask = MASK_SOLID;
			F::ProjSim.SetupTrace(tFilter, nMask, pWeapon);
			tSim.vPath.clear();
			tSim.land = -1;
			for (int k = 1; k <= kFlight; k++)
			{
				const Vec3 vOld = F::ProjSim.GetOrigin();
				F::ProjSim.RunTick(tInfo, false);
				SDK::TraceHull(vOld, F::ProjSim.GetOrigin(), tInfo.m_vHull * -1, tInfo.m_vHull, nMask, &tFilter, &tTrace);
				if (!tTrace.DidHit())
				{
					tSim.vPath.push_back(F::ProjSim.GetOrigin());
					continue;
				}
				if (!(tTrace.surface.flags & SURF_SKY) && (!tTrace.m_pEnt || !tTrace.m_pEnt->entindex()))
				{
					tSim.land = k;
					tSim.vPath.push_back(tTrace.endpos + tTrace.plane.normal);
				}
				break;
			}
			if (tSim.vPath.empty())
				return -1e9f;

			float flRes = -1e9f;
			const int iLast = tSim.land >= 0 ? kPath : int(tSim.vPath.size());   // gone after its last sim step
			for (int m = iArm; f + m < kPath && m <= iLast; m++)
			{
				if (!bLaunch && pGround[f + m])
					break;   // we touched down first: the pogo is over on this aim
				const gmb::V vPush = ShotPush(tSim, b, m, q[f + m]);
				d.bestPush = (std::max)(d.bestPush, vPush.z);
				if (vPush.z < kMinPush)
					continue;
				const float s = Score(q[f + m], vPush, vFwd) - 1.f * m;   // sooner = shorter guess of your steering
				if (s > flRes)
				{
					flRes = s;
					tRes = tSim;
					tRes.arm = iArm, tRes.best = m, tRes.score = s;
				}
			}
			if (flRes <= -1e9f)
				d.noReach++;
			return flRes;
		};

		// coarse: a straight push needs the blast ON the view line (ahead or behind): dense there
		float flBest = -1e9f;
		Shot tTry;
		auto Try = [&](const Vec3& vAng)
		{
			const float s = Eval(vAng, tTry);
			if (s <= flBest)
				return false;
			flBest = s, tOut = std::move(tTry), vOut = vAng;
			return true;
		};
		Try(vView);
		for (float flPitch : { -45.f, -30.f, -15.f, 0.f, 10.f, 20.f, 30.f, 45.f, 60.f, 75.f, 89.f })
			for (float flOff : { 0.f, 4.f, -4.f, 9.f, -9.f, 16.f, -16.f, 30.f, -30.f, 180.f, 174.f, -174.f, 165.f, -165.f })
				Try({ flPitch, Math::NormalizeAngle(vView.y + flOff), 0.f });
		if (flBest <= -1e9f)
			return false;

		// refine: hill-climb pitch / yaw around the best at 4, 2, 1 degrees -- the coarse grid is
		// 10-15 deg apart in pitch, which at 900 u/s is tens of units off the best blast spot
		for (float flStep : { 4.f, 2.f, 1.f })
			for (int iter = 0; iter < 3; iter++)
			{
				const Vec3 c = vOut;
				bool bMoved = false;
				for (const Vec3& o : { Vec3(flStep, 0.f, 0.f), Vec3(-flStep, 0.f, 0.f), Vec3(0.f, flStep, 0.f), Vec3(0.f, -flStep, 0.f) })
					bMoved |= Try({ std::clamp(c.x + o.x, -89.f, 89.f), Math::NormalizeAngle(c.y + o.y), 0.f });
				if (!bMoved)
					break;
			}
		return true;
	}
}

void AutoPogo_Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!g_autoPogo || !MovementKeyOK(g_autoPogoKey, g_autoPogoMode) || !pWeapon || pWeapon->GetWeaponID() != TF_WEAPON_PIPEBOMBLAUNCHER
		|| !pLocal->IsAlive() || pLocal->IsAGhost() || I::EngineVGui->IsGameUIVisible())
	{
		if (s_iHold)
			pCmd->buttons &= ~IN_ATTACK;   // bind let go mid-press: the launch fires with your view, as a normal tap would
		s_iHold = 0;
		if (!pLocal->IsAlive() || pWeapon && pWeapon->GetWeaponID() != TF_WEAPON_PIPEBOMBLAUNCHER)
			s_vShots.clear();   // dead / swapped: those stickies are not ours to time any more
		DiagFlush();
		return;
	}
	s_tDiag.cmds++;

	const int iTick = pLocal->m_nTickBase();
	// 4 s old = long missed (or fizzled): forget it
	std::erase_if(s_vShots, [&](const Shot& t) { return iTick - t.fireTick > TIME_TO_TICKS(4.f); });
	// sticky jumper: mult_dmg 0 but the push still uses the force damage -> base damage
	const Blast tBlast = { pWeapon->GetDamage() > 0.f ? pWeapon->GetDamage() : pWeapon->GetDamage(false),
		146.f * SDK::AttribHookValue(1.f, "mult_explosion_radius", pWeapon), pLocal->IsDucking() };
	const Vec3 vView = G::OriginalCmd.viewangles;   // where you look (before any silent aim)
	Vec3 vFwd; Math::AngleVectors({ 0.f, vView.y, 0.f }, &vFwd);

	static gmb::Body q[kPath];
	static bool g[kPath];
	OurPath(pLocal, pCmd, q, g);

	// ---- det: every armed sticky we fired vs where you really are ----
	// The plan only picked the aim; you steer after the shot, so the det never waits for the
	// planned tick. It looks kLook cmds ahead along your predicted flight (sticky spot per tick
	// from the plan, a stuck one at its real spot) and dets now only if no later tick scores
	// better -- the real peak, not "now beats next", which a flat stretch or a wobble fooled.
	// A touchdown ends the look-ahead, so the tick before landing is the last chance.
	// ponytail: stickies you placed by hand have no exact arm tick, so they are not counted
	// (they still blow with ours).
	{
		constexpr int kLook = 30;
		auto& vBombs = H::Entities.GetGroup(EntityEnum::LocalStickies);
		// per shot: the real stuck spot once the client has it (exact, unlike a flying sticky's lagged copy)
		std::vector<const Vec3*> vReal(s_vShots.size(), nullptr);
		std::vector<Vec3> vRealPos(s_vShots.size());
		for (size_t i = 0; i < s_vShots.size(); i++)
		{
			Shot& t = s_vShots[i];
			if (t.land < 0 || iTick - t.fireTick < t.land)
				continue;
			for (auto pEnt : vBombs)
			{
				auto pBomb = pEnt->As<CTFGrenadePipebombProjectile>();
				if (!pBomb->m_bTouched() || pBomb->GetAbsOrigin().DistTo(t.vPath.back()) > 96.f)
					continue;
				if (!t.seen)   // aim check: how far the real sticky stuck from the plan
					t.seen = true, DebugLog("POGO: stuck m=%d off=%.0f", iTick - t.fireTick, pBomb->GetAbsOrigin().DistTo(t.vPath.back()));
				vRealPos[i] = pBomb->GetAbsOrigin(), vReal[i] = &vRealPos[i];
				break;
			}
		}
		// summed push of every shot armed by the det at k cmds from now (only those blow)
		auto PushAt = [&](int k, bool& bArmed) -> gmb::V
		{
			gmb::V v = {};
			bArmed = false;
			for (size_t i = 0; i < s_vShots.size(); i++)
			{
				const Shot& t = s_vShots[i];
				const int m = iTick - t.fireTick + k;
				if (m < t.arm || (t.land < 0 && m > int(t.vPath.size())))
					continue;   // not armed then / past its sim (left the map, fizzled)
				bArmed = true;
				v = v + (vReal[i] ? gmb::SelfStickyPush(GV(*vReal[i]), q[k], tBlast.dmg, tBlast.radius, tBlast.duck) : ShotPush(t, tBlast, m, q[k]));
			}
			return v;
		};

		bool bAny;
		const gmb::V vNow = PushAt(0, bAny);
		if (bAny)
		{
			s_tDiag.armedCmds++;
			s_tDiag.bestDetPush = (std::max)(s_tDiag.bestDetPush, vNow.z);
			const float flNow = Score(q[0], vNow, vFwd);
			float flLater = -1e9f;
			bool  bLanding = true;   // no airborne tick ahead = this is the last one
			for (int k = 1; k <= kLook && k < kPath; k++)
			{
				if (!g[0] && g[k])
					break;   // touched down before k
				bLanding = false;
				bool bArmed;
				const gmb::V v = PushAt(k, bArmed);
				if (v.z >= kMinPush)
					flLater = (std::max)(flLater, Score(q[k], v, vFwd));
			}
			if (vNow.z >= kMinPush && (flNow >= flLater || bLanding))
			{
				pCmd->buttons |= IN_ATTACK2;
				s_tDiag.dets++;
				s_tDiag.lastChance += bLanding;
				s_bWaitPush = true, s_iLastDet = iTick, s_flDetVz = q[0].vel.z, s_flKickZ = vNow.z;
				const gmb::V v = q[0].vel + vNow;   // what we leave with, in the view frame
				DebugLog("POGO: det push=%.0f/%.0f/%.0f vz %.0f -> %.0f fwd=%.0f side=%.0f%s", vNow.x, vNow.y, vNow.z, q[0].vel.z, v.z,
					v.x * vFwd.x + v.y * vFwd.y, v.x * -vFwd.y + v.y * vFwd.x, bLanding ? " (landing)" : "");
				std::erase_if(s_vShots, [&](const Shot& t) { return iTick - t.fireTick >= t.arm; });   // armed ones are gone
			}
		}
	}

	// ---- fire: press kHold cmds, release on the next at the re-planned angle ----
	// ONE sticky at a time: a second one planned while the first is out aims for a flight the
	// first one's det is about to change (log: shot 2 fired det@58 inside shot 1's det@101 ->
	// miss). A shot that blew past its planned det by 30 cmds without a det is given up on.
	std::erase_if(s_vShots, [&](const Shot& t)
	{
		const bool bGone = iTick - t.fireTick > t.best + 30;
		if (bGone)
			DebugLog("POGO: gave up on sticky, planned det %d, now %d", t.best, iTick - t.fireTick);
		return bGone;
	});
	if (s_bWaitPush)
	{
		static auto sv_gravity = H::ConVars.FindVar("sv_gravity");
		const float g0 = sv_gravity ? sv_gravity->GetFloat() : 800.f;
		const int   n = iTick - s_iLastDet;
		// what vz would be with no push: falling on from the det, or 0 once on the floor
		const float flNoPush = pLocal->m_hGroundEntity() ? 0.f : s_flDetVz - g0 * TICKS_TO_TIME(n);
		if (pLocal->m_vecVelocity().z > flNoPush + 0.5f * s_flKickZ)
			s_bWaitPush = false, DebugLog("POGO: push arrived after %d cmds", n);
		else if (n > TIME_TO_TICKS(kPushWait))
			s_bWaitPush = false, DebugLog("POGO: push never showed (det denied / blocked?)");
	}

	if (s_iHold > 1)
	{
		s_iHold--;
		pCmd->buttons |= IN_ATTACK;   // still charging
	}
	else if (s_iHold == 1)
	{
		s_iHold = 0;
		Shot tShot = s_tPressPlan; Vec3 vAng = s_vPressAng;
		Plan(pLocal, pWeapon, q, g, 0, tBlast, vFwd, vView, tShot, vAng);   // exact seed now; fails -> keep the press cmd's plan
		pCmd->buttons &= ~IN_ATTACK;
		SDK::FixMovement(pCmd, vAng);
		pCmd->viewangles = vAng;
		G::PSilentAngles = true;
		tShot.fireTick = iTick;
		DebugLog("POGO: fire ang=%.0f/%.0f %s=%d arm=%d det@%d score=%.0f", vAng.x, vAng.y,
			tShot.land >= 0 ? "land" : "air", tShot.land >= 0 ? tShot.land : int(tShot.vPath.size()), tShot.arm, tShot.best, tShot.score);
		s_vShots.push_back(std::move(tShot));
		s_tDiag.fired++;
	}
	else if (!s_vShots.empty() || s_bWaitPush)
		s_tDiag.waiting++;
	else if (pWeapon->m_iClip1() <= 0 || !(G::CanPrimaryAttack || G::Reloading) || pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime() > 0.f)
		s_tDiag.noAtk++;
	else if (Plan(pLocal, pWeapon, q, g, kHold, tBlast, vFwd, vView, s_tPressPlan, s_vPressAng))   // launches kHold cmds from now
	{
		pCmd->buttons |= IN_ATTACK;
		s_iHold = kHold;
	}
}
