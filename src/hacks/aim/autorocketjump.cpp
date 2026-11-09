#include "autorocketjump.h"

#include "projsim.h"
#include "movesim.h"

// Amalgam Features/Aimbot/AutoRocketJump/AutoRocketJump.cpp, 1:1. Vars mapped to the auto
// goomba: AutoRocketJump = goomba on + type rocket jump + its bind held, AutoCTap off,
// ApplyAbove / TimingOffset / ChokeCount = Amalgam defaults (0 / 0 / 1). The one change:
// the angle + fire tick come from GoombaPlan (scored aim search), not Amalgam's single spot.

// dih side (movement_ex.inl, dllmain.cpp)
extern bool g_goomba;
extern int  g_goombaKey, g_goombaMode, g_goombaHow, g_goombaLead;
extern bool g_autoRJ;
extern int  g_autoRJKey, g_autoRJMode;
bool MovementKeyOK(int key, int mode);
int  Movement_TakeGoombaRocketContact();
void Movement_GoombaFired();
void DebugLog(const char* fmt, ...);

static bool s_bOn = false;   // Vars::Misc::Movement::AutoRocketJump
static bool s_bRJ = false;   // standalone (Unibox) auto rocket jump: g_autoRJ + its bind
static int  s_iGoombaK = -1; // ticks until the falling player reaches our standing head
static constexpr int kApplyAbove = 0, kTimingOffset = 0, kChokeCount = 1;
// Unibox debug vars (NOSAVE), at their defaults
static constexpr int kRJSkipGround = 0, kRJSkipAir = 1, kRJChokeGrounded = 1, kRJChokeAir = 1, kRJTimingOffset = 0, kRJApplyAbove = 0;

// ---- reject summary: one "GOOMBA R:" line per bind hold, the count of cmds each gate ate.
// Every gate below used to return silently, so a hold that never fired left no trace ----
static struct GmbDiag
{
	int cmds = 0, nowpn = 0, noK = 0, duck = 0, noAtk = 0, ang = 0, plans = 0;
	int cand = 0, noInfo = 0, noHit = 0, onUs = 0, low = 0, rising = 0, fire = 0;
	float bestNow = -1e9f, bestNext = -1e9f;
} s_tDiag;
static void Gmb_DiagFlush()
{
	GmbDiag& d = s_tDiag;
	if (d.cmds)
		DebugLog("GOOMBA R: cmds=%d nowpn=%d noK=%d duck=%d noAtk=%d ang=%d plans=%d cand=%d noInfo=%d noHit=%d onUs=%d low=%d rising=%d fire=%d bestNow=%.0f bestNext=%.0f",
			d.cmds, d.nowpn, d.noK, d.duck, d.noAtk, d.ang, d.plans, d.cand, d.noInfo, d.noHit, d.onUs, d.low, d.rising, d.fire, d.bestNow, d.bestNext);
	d = {};
}

// ---- goomba rocket plan: score every aim, fire on the best tick ---------------------------
// The boost has two sources (math + SDK refs in goomba_pure.h): the blast pushes the enemy
// (damage falls off with distance to their feet/centre), and a landing on our rising head hands
// them our vz. Both depend on where the rocket lands (floor slope = the impact normal), where
// they and we are at the blast tick, and our ctap launch. So: for each candidate aim, trace the
// rocket (straight line, m_flVelocity) to its impact, move both players to that tick, apply
// both impulses, fly them and read the target's resulting vz. Fire now when the best "now"
// beats the best "one tick later": that is the peak.
#include "../movement/goomba_pure.h"
int Movement_GoombaTarget();
int Movement_GoombaPath(const gmb::PathPt** ppPath);

static gmb::V GV(const Vec3& v) { return { v.x, v.y, v.z }; }

// ---- auto goomba sims, called from the movement pipeline (movement_ex.inl AutoGoomba) ------
// It runs pre-prediction, outside any bracket: MoveSim saves + restores the prediction flags
// and frametime itself, and Pred_Start / EnginePrediction set the move helper host per sim.

// Target path. Heroin's goomba assist (FUN_180075a10) copies its target, runs the engine's
// ProcessMovement on it for the latency in ticks and tests the z window every tick. Same here
// through Amalgam's MoveSim: snapshot -> tCmd (the server time our cmd runs at), then n ticks
// recorded. 0 = no sim (aim interfaces down / init failed): the caller keeps the parabola.
int Aim_GoombaPath(int iIndex, float flCmdTime, int n, gmb::PathPt& tNow, gmb::PathPt* pOut)
{
	if (n <= 0 || !I::GameMovement || !I::MoveHelper || !I::Prediction || !H::Entities.GetLocal())
		return 0;
	auto pClient = I::ClientEntityList->GetClientEntity(iIndex);
	auto pEnt = pClient ? pClient->As<CBaseEntity>() : nullptr;
	if (!pEnt || !pEnt->IsPlayer())
		return 0;

	auto Pt = [](const CMoveData& m) -> gmb::PathPt
	{
		return { m.m_vecAbsOrigin.x, m.m_vecAbsOrigin.y, m.m_vecAbsOrigin.z, m.m_vecVelocity.x, m.m_vecVelocity.y, m.m_vecVelocity.z };
	};
	int w = 0;
	MoveStorage tStorage;
	if (F::MoveSim.Initialize(pEnt, tStorage, false, true))
	{
		// a stale snapshot is a lag spike, not physics: same 0.5 s clamp as the parabola
		const int iPre = std::clamp(TIME_TO_TICKS(flCmdTime - tStorage.m_flSimTime), 0, TIME_TO_TICKS(0.5f));
		for (int i = 0; i < iPre; i++)
			F::MoveSim.RunTick(tStorage, false);
		tNow = Pt(tStorage.m_MoveData);
		for (; w < n && !tStorage.m_bFailed; w++)
		{
			F::MoveSim.RunTick(tStorage, false);
			pOut[w] = Pt(tStorage.m_MoveData);
		}
		if (tStorage.m_bFailed)
			w = 0;
	}
	F::MoveSim.Restore(tStorage);
	return w;
}

// Our sticky launch: the vz our armed stickies add when the det cmd's PostThink sets them off.
// tMe = where that cmd's movement leaves us (after the 0x806 jump). Arm + radius = Amalgam's
// autodetonate GetRadius. -1 = nothing armed reaches us (a det would only be a crouch jump).
// ponytail: no blast LOS trace (stickies under our own feet are never blocked); add the
// ApplyToEntity trace (tf_gamerules.cpp:5771) if a wall between us ever fakes a boost.
float Aim_GoombaStickyKick(const gmb::Body& tMe, bool bDucked)
{
	static auto tf_grenadelauncher_livetime = H::ConVars.FindVar("tf_grenadelauncher_livetime");
	static auto tf_sticky_radius_ramp_time = H::ConVars.FindVar("tf_sticky_radius_ramp_time");
	static auto tf_sticky_airdet_radius = H::ConVars.FindVar("tf_sticky_airdet_radius");
	if (!H::Entities.GetLocal())
		return -1.f;

	float flKick = 0.f;
	bool bAny = false;
	for (auto pEnt : H::Entities.GetGroup(EntityEnum::LocalStickies))
	{
		auto pBomb = pEnt->As<CTFGrenadePipebombProjectile>();
		auto pWeapon = pBomb->m_hOriginalLauncher()->As<CTFWeaponBase>();
		if (!pWeapon)
			pWeapon = H::Entities.GetWeapon();
		if (!pWeapon || !pBomb->m_flCreationTime()
			|| I::GlobalVars->curtime < pBomb->m_flCreationTime() + SDK::AttribHookValue(0.8f, "sticky_arm_time", pWeapon))
			continue;

		float flRadius = 146.f;
		if (!pBomb->m_bTouched() && tf_grenadelauncher_livetime && tf_sticky_radius_ramp_time && tf_sticky_airdet_radius)
		{
			const float flLive = tf_grenadelauncher_livetime->GetFloat(), flRamp = tf_sticky_radius_ramp_time->GetFloat();
			flRadius *= Math::RemapVal(I::GlobalVars->curtime - pBomb->m_flCreationTime(), flLive, flLive + flRamp, tf_sticky_airdet_radius->GetFloat(), 1.f);
		}
		flRadius = SDK::AttribHookValue(flRadius, "mult_explosion_radius", pWeapon);
		const float flDmg = pBomb->m_flDamage() > 0.f ? pBomb->m_flDamage() : pWeapon->GetDamage();

		const gmb::V v = gmb::SelfStickyPush(GV(pBomb->GetAbsOrigin()), tMe, flDmg, flRadius, bDucked);
		if (v.x != 0.f || v.y != 0.f || v.z != 0.f)
		{
			bAny = true;
			flKick += v.z;
		}
	}
	return bAny ? flKick : -1.f;
}

static bool GoombaPlan(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, bool bGrounded, Vec3& vAngles, int& iDelay)
{
	if (s_iGoombaK < 1) return false;   // movement saw nobody falling onto us this cmd
	const int iTarget = Movement_GoombaTarget();
	auto pEnt = iTarget > 0 ? I::ClientEntityList->GetClientEntity(iTarget) : nullptr;
	auto pThem = pEnt ? pEnt->As<CTFPlayer>() : nullptr;
	if (!pThem || pThem->IsDormant() || !pThem->IsAlive())
		return false;

	const float dt = TICK_INTERVAL;
	static auto sv_gravity = H::ConVars.FindVar("sv_gravity");
	const float g = sv_gravity ? sv_gravity->GetFloat() : 800.f;
	const int   iLead = (std::max)(g_goombaLead, 0);
	// their snapshot -> the server time our fire cmd runs at, same shift as movement_ex.inl
	const float t0 = std::clamp(TICKS_TO_TIME(pLocal->m_nTickBase()) - pThem->m_flSimulationTime(), 0.f, 0.5f) + TICKS_TO_TIME(iLead);

	const Vec3  vThemOrg = pThem->m_vecOrigin(), vThemVel = pThem->m_vecVelocity();
	const float flThemHull = pThem->m_vecMaxs().z - pThem->m_vecMins().z, flThemHalf = pThem->m_vecMaxs().x;
	const float flThemForceZ = pThem->IsDucking() ? 62.f : 82.f;
	// T ticks past our fire cmd. The movement side's ProcessMovement path when it reaches that
	// far (path k = ticks past OUR cmd's server time, so k = lead + T), the parabola otherwise
	const gmb::PathPt* pPath = nullptr;
	const int nPath = Movement_GoombaPath(&pPath);
	auto ThemAt = [&](int T)
	{
		gmb::Body b;
		b.half = flThemHalf; b.hullZ = flThemHull;
		const int k = iLead + T;
		if (pPath && k >= 1 && k <= nPath)
		{
			const gmb::PathPt& p = pPath[k - 1];
			b.org = { p.x, p.y, p.z };
			b.vel = { p.vx, p.vy, p.vz };
			return b;
		}
		const float t = t0 + TICKS_TO_TIME(T);
		b.org = GV(vThemOrg) + GV(vThemVel) * t + gmb::V{ 0.f, 0.f, -0.5f * g * t * t };
		b.vel = GV(vThemVel); b.vel.z -= g * t;
		return b;
	};

	// us at the blast tick. Grounded = the ARJ ctap: jumped one cmd before the blast, air duck
	// lifts the feet 20u (62u hull, head stays) -> in air + ducked = scale 10, size 55.
	// Airborne = Amalgam fires at once, we keep our arc and our current duck state.
	const Vec3  vMeOrg = pLocal->m_vecOrigin(), vMeVel = pLocal->m_vecVelocity();
	const float flMeHalf = pLocal->m_vecMaxs().x;
	const bool  bMeDuck = bGrounded || pLocal->IsDucking();
	auto MeAt = [&](int T)
	{
		gmb::Body b;
		const float t = TICKS_TO_TIME(T);
		b.half = flMeHalf;
		if (bGrounded)
		{
			b.org = GV(vMeOrg) + gmb::V{ vMeVel.x * t, vMeVel.y * t, gmb::Rise(289.f, g, dt) + 20.f };
			b.vel = { vMeVel.x, vMeVel.y, 289.f - g * dt };
			b.hullZ = 62.f;
		}
		else
		{
			b.org = GV(vMeOrg) + GV(vMeVel) * t + gmb::V{ 0.f, 0.f, -0.5f * g * t * t };
			b.vel = GV(vMeVel); b.vel.z -= g * t;
			b.hullZ = pLocal->m_vecMaxs().z - pLocal->m_vecMins().z;
		}
		return b;
	};

	const float flDmg = pWeapon->GetDamage();
	const float flRadius = 146.f * SDK::AttribHookValue(1.f, "mult_explosion_radius", pWeapon);

	// candidates: Amalgam's own spot + a ring of pitches x yaws around our view
	std::vector<Vec3> vCands = { vAngles };
	const float flYaw = G::OriginalCmd.viewangles.y;
	for (float flPitch : { 35.f, 50.f, 62.f, 72.f, 80.f, 86.f, 89.f })
		for (int i = 0; i < 8; i++)
			vCands.push_back({ flPitch, Math::NormalizeAngle(flYaw + 45.f * i), 0.f });

	struct Pick { float flScore = -1e9f; Vec3 vAng = {}; int n = 0; gmb::Outcome o = {}; float flMePush = 0.f, flThemPush = 0.f; };
	Pick tNow, tNext;
	GmbDiag& d = s_tDiag;
	d.plans++;
	for (auto& vAng : vCands)
	{
		d.cand++;
		ProjectileInfo tInfo = {};
		if (!F::ProjSim.GetInfo(pLocal, pWeapon, vAng, tInfo, ProjSimEnum::Trace | ProjSimEnum::InitCheck | ProjSimEnum::NoRandomAngles)
			|| tInfo.m_flVelocity <= 0.f)
		{
			d.noInfo++;
			continue;
		}
		Vec3 vFwd; Math::AngleVectors(tInfo.m_vAng, &vFwd);
		const float flStep = tInfo.m_flVelocity * dt;

		CGameTrace tTrace = {};
		CTraceFilterCollideable tFilter = {}; tFilter.pSkip = pLocal;
		SDK::Trace(tInfo.m_vPos, tInfo.m_vPos + vFwd * (flStep * 12.f), MASK_SOLID, &tFilter, &tTrace);
		if (!tTrace.DidHit())
		{
			d.noHit++;
			continue;
		}
		const int n = (std::max)(1, int(ceilf((tTrace.endpos - tInfo.m_vPos).Length() / flStep)));
		const gmb::V vBlast = GV(tTrace.endpos + tTrace.plane.normal);   // slope lives in the normal

		for (int f = 0; f < 2; f++)   // fire this cmd / one cmd later
		{
			const int T = f + n;
			gmb::Body tMe = MeAt(T), tThem = ThemAt(T);
			if (tThem.org.z - (tMe.org.z + tMe.hullZ) <= 0.f)
			{
				d.onUs++;
				continue;   // they would already be on (or past) us when it goes off
			}
			const gmb::V vMePush = gmb::BlastImpulse(vBlast, tMe, flDmg, flRadius, 0.5f, 10.f /* in air at the blast: ctap or already flying */, bMeDuck ? 55.f : 82.f);
			const gmb::V vThemPush = gmb::BlastImpulse(vBlast, tThem, flDmg, flRadius, 0.5f, 6.f, flThemForceZ);
			tMe.vel = tMe.vel + vMePush; tThem.vel = tThem.vel + vThemPush;
			const gmb::Outcome o = gmb::Fly(tMe, tThem, g, dt, 30);
			Pick& tBest = f ? tNext : tNow;
			if (o.vz > tBest.flScore)
				tBest = { o.vz, vAng, n, o, vMePush.z, vThemPush.z };
		}
	}

	d.bestNow = (std::max)(d.bestNow, tNow.flScore); d.bestNext = (std::max)(d.bestNext, tNext.flScore);
	// beat a plain jump (289) or it is not a boost, and do not fire on a rising curve
	if (tNow.flScore < 289.f) { d.low++; return false; }
	if (tNow.flScore < tNext.flScore) { d.rising++; return false; }
	d.fire++;
	vAngles = tNow.vAng;
	iDelay = tNow.n;
	DebugLog("GOOMBA: rocket plan score=%.0f next=%.0f ticks=%d ang=%.0f/%.0f contact=%d myVz=%.0f pushMe=%.0f pushThem=%.0f",
		tNow.flScore, tNext.flScore, tNow.n, tNow.vAng.x, tNow.vAng.y, tNow.o.contact, tNow.o.myVz, tNow.flMePush, tNow.flThemPush);
	return true;
}

bool CAutoRocketJump::SetAngles(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	m_vAngles = pCmd->viewangles;
	if (!s_bOn && !s_bRJ)
		return true;

	ProjectileInfo tProjInfo = {};
	if (!F::ProjSim.GetInfo(pLocal, pWeapon, {}, tProjInfo, ProjSimEnum::NoRandomAngles)
		|| !F::ProjSim.Initialize(tProjInfo, false))
		return false;

	Vec3 vOrigin = pLocal->m_vecOrigin();
	Vec3 vLocalPos = pLocal->GetShootPos();
	Vec3 vOffset = tProjInfo.m_vPos - vLocalPos; vOffset.x = 0; //vOffset.y *= -1;
	float flVelocity = F::ProjSim.GetVelocity().Length();

	Vec3 vPoint;
	if (pLocal->IsOnGround())
	{
		float flOffset = sqrtf(2 * powf(vOffset.y, 2.f) + powf(vOffset.z, 2.f));
		Vec3 vWishVel = { pCmd->forwardmove, pCmd->sidemove, 0.f }, vDir;
		if (vWishVel.IsZero())
			vDir = { 0.f, m_vAngles.y, 0.f };
		else
		{
			Vec3 vWishAng = Math::VectorAngles(vWishVel);
			vDir = { 0.f, m_vAngles.y - vWishAng.y, 0.f };
		}
		Vec3 vForward; Math::AngleVectors(vDir, &vForward);
		vPoint = pLocal->m_vecOrigin() - vForward * flOffset;
	}
	else
	{
		//float flOffset = pLocal->m_vecMaxs().x;
		float flOffset = sqrtf(2 * powf(vOffset.y, 2.f) + powf(vOffset.z, 2.f));
		bool bShouldReturn = true;
		MoveStorage tStorage;
		if (F::MoveSim.Initialize(pLocal, tStorage, false))
		{
			for (int n = 1; n < 10; n++)
			{
				F::MoveSim.RunTick(tStorage);
				if (!pLocal->IsOnGround() || pLocal->IsSwimming())
					continue;

				Vec3 vForward = tStorage.m_MoveData.m_vecVelocity.Normalized2D();
				vPoint = tStorage.m_MoveData.m_vecAbsOrigin - vForward * flOffset; //- Vec3(0, 0, 20);
				bShouldReturn = false;
				break;
			}
		}
		F::MoveSim.Restore(tStorage);
		if (bShouldReturn)
			return false;
	}

	float flPitch, flYaw;
	{	// basic trajectory pass
		Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPoint);
		m_vAngles.x = flPitch = vAngleTo.x, m_vAngles.y = flYaw = vAngleTo.y;
	}

	if (!F::ProjSim.GetInfo(pLocal, pWeapon, { flPitch, flYaw, 0 }, tProjInfo, ProjSimEnum::Trace | ProjSimEnum::NoRandomAngles))
		return false;

	{	// correct yaw
		Vec3 vShootPos = (tProjInfo.m_vPos - vLocalPos).To2D();
		Vec3 vTarget = vPoint - vLocalPos;
		Vec3 vForward; Math::AngleVectors(tProjInfo.m_vAng, &vForward); vForward.Normalize2D();
		float flA = 1.f;
		float flB = 2 * (vShootPos.x * vForward.x + vShootPos.y * vForward.y);
		float flC = vShootPos.Length2DSqr() - vTarget.Length2DSqr();
		auto vSolutions = Math::SolveQuadratic(flA, flB, flC);
		if (!vSolutions.empty())
		{
			vShootPos += vForward * vSolutions.front();
			m_vAngles.y = flYaw - (RAD2DEG(atan2(vShootPos.y, vShootPos.x)) - flYaw);
			flYaw = RAD2DEG(atan2(vShootPos.y, vShootPos.x));
		}
	}

	{	// correct pitch
		Vec3 vShootPos = Math::RotatePoint(tProjInfo.m_vPos - vLocalPos, {}, { 0, -flYaw, 0 }); vShootPos.y = 0;
		Vec3 vTarget = Math::RotatePoint(vPoint - vLocalPos, {}, { 0, -flYaw, 0 });
		Vec3 vForward; Math::AngleVectors(tProjInfo.m_vAng - Vec3(0, flYaw, 0), &vForward); vForward.y = 0; vForward.Normalize();
		float flA = 1.f;
		float flB = 2 * (vShootPos.x * vForward.x + vShootPos.z * vForward.z);
		float flC = (powf(vShootPos.x, 2) + powf(vShootPos.z, 2)) - (powf(vTarget.x, 2) + powf(vTarget.z, 2));
		auto vSolutions = Math::SolveQuadratic(flA, flB, flC);
		if (!vSolutions.empty())
		{
			vShootPos += vForward * vSolutions.front();
			m_vAngles.x = flPitch - (RAD2DEG(atan2(-vShootPos.z, vShootPos.x)) - flPitch);
		}
	}

	return true;
}

void CAutoRocketJump::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	// take every cmd, used or not: a contact from a cmd this skipped must never start a jump later
	s_iGoombaK = Movement_TakeGoombaRocketContact();
	s_bOn = g_goomba && g_goombaHow == 1 && MovementKeyOK(g_goombaKey, g_goombaMode);
	if (!s_bOn)
		Gmb_DiagFlush();   // bind let go: one line for the whole hold
	else
		s_tDiag.cmds++;

	// standalone ARJ: its own sequence runs to the end; a new one starts only when the goomba is idle.
	// Grounded history is sampled every cmd so "grounded last cmd too" never reads a stale hold
	s_bRJ = g_autoRJ && MovementKeyOK(g_autoRJKey, g_autoRJMode);
	static bool s_bRJGround = false;
	const bool bRJLastGround = s_bRJGround;
	s_bRJGround = pLocal->m_hGroundEntity();
	if (m_iFrame != -1 ? m_bRJ : !s_bOn && s_bRJ)
	{
		RunRJ(pLocal, pWeapon, pCmd, bRJLastGround, s_bRJGround);
		return;
	}
	// Amalgam with both vars off reaches no button write but the particle cannon's own
	// IN_ATTACK2 echo (a no-op), so idling here is the same behavior minus the sims
	if (!s_bOn && m_iFrame == -1)
	{
		m_bRunning = false;
		return;
	}

	if (!pWeapon || !pLocal->IsAlive() || pLocal->IsAGhost() || I::EngineVGui->IsGameUIVisible())
	{
		s_tDiag.nowpn++;
		m_iFrame = -1;
		return;
	}

	static bool bStaticGrounded = false;
	bool bLastGrounded = bStaticGrounded;
	bool bCurrGrounded = bStaticGrounded = pLocal->m_hGroundEntity();
	// grounded: the ctap needs a fresh duck, so not while ducked or inside the duck timer.
	// Amalgam's ARJ also refused an UNducked air shot (its landing ctap wants one); a goomba
	// in the air has no landing to ctap, so that half is gone -- it ate every air hold.
	if (m_iFrame == -1 && s_iGoombaK >= 1 && bCurrGrounded
		&& (pLocal->IsDucking() || I::GlobalVars->curtime < pLocal->m_flDuckTimer()))
	{
		s_tDiag.duck++;
		return;
	}

	bool bValidWeapon = false;
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_ROCKETLAUNCHER:
		case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
		case TF_WEAPON_PARTICLE_CANNON: bValidWeapon = true;
		}
	}
	if (!bValidWeapon)
	{
		s_tDiag.nowpn++;
		m_iFrame = -1;
		return;
	}
	else if (s_bOn)
		pCmd->buttons &= ~IN_ATTACK2; // fix for retarded issue

	if (m_iFrame == -1 && s_bOn)
	{
		if (s_iGoombaK < 1) s_tDiag.noK++;
		else if (!(pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka ? G::Attacking == 1 : G::CanPrimaryAttack || G::Reloading)) s_tDiag.noAtk++;
	}
	if (m_iFrame == -1 && s_iGoombaK >= 1
		&& (pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka ? G::Attacking == 1 : G::CanPrimaryAttack || G::Reloading))
	{
		// Amalgam's spot is only a seed: in the air it needs a landing inside 10 ticks, which a
		// goomba (target falling onto us mid-flight) usually lacks. No spot -> the view angle
		// seeds the search instead of killing it
		if (!SetAngles(pLocal, pWeapon, pCmd))
		{
			s_tDiag.ang++;
			m_vAngles = pCmd->viewangles;
		}
		// goomba: Amalgam's single-spot sim is replaced by the scored search (GoombaPlan), which
		// keeps Amalgam's spot (m_vAngles from SetAngles) as one of its candidates
		bool bWillHit = s_bOn && GoombaPlan(pLocal, pWeapon, bCurrGrounded, m_vAngles, m_iDelay);

		if (bWillHit)
		{
			if (bCurrGrounded && bCurrGrounded == bLastGrounded)
			{
				if (s_bOn)
					m_iFrame = 0;
				if (s_bOn)
					m_bFull = true;
			}
			else if (!bCurrGrounded)
			{
				if (pWeapon->m_iItemDefinitionIndex() != Soldier_m_TheBeggarsBazooka)
					pCmd->buttons |= IN_ATTACK;

				if (s_bOn && G::CanPrimaryAttack)
				{
					G::SilentAngles = true;
					pCmd->viewangles = m_vAngles;
					Movement_GoombaFired();
				}
			}

			if (m_iFrame != -1 && G::Reloading)
			{
				m_iFrame = -1;
				m_bFull = false;
				pCmd->buttons |= IN_ATTACK;
			}
		}

		if (m_iFrame == -1 && pWeapon->GetWeaponID() == TF_WEAPON_PARTICLE_CANNON && G::OriginalCmd.buttons & IN_ATTACK2)
			pCmd->buttons |= IN_ATTACK2;
	}

	if (m_iFrame != -1)
	{
		m_iFrame++;
		pCmd->buttons &= ~IN_JUMP;

		if (m_iFrame == 1)
		{
			if (pWeapon->m_iItemDefinitionIndex() != Soldier_m_TheBeggarsBazooka)
				pCmd->buttons |= IN_ATTACK;

			if (m_bFull)
			{
				G::SilentAngles = bCurrGrounded;
				pCmd->viewangles = m_vAngles;
			}
			Movement_GoombaFired();
		}

		if (m_iFrame <= kChokeCount)
			G::PSilentAngles = true, G::SilentAngles = false; // only con to this is that you may not be able to choke depending on the ticks charged

		if (m_iDelay > 1)
		{
			switch (m_iFrame - m_iDelay + 1)
			{
			case 0:
				pCmd->buttons |= IN_DUCK;
				break;
			case 1:
				pCmd->buttons |= IN_JUMP;
			}
		}
		else // won't ctap in time
			pCmd->buttons |= IN_DUCK | IN_JUMP;

		if (m_iFrame == m_iDelay + 3)
		{
			m_iFrame = -1;
			m_bFull = false;
		}
	}

	m_bRunning = m_iFrame != -1; // prevent stuff like antiaim messing with timing
}

// ---- Unibox Features/Aimbot/AutoRocketJump/AutoRocketJump.cpp Run, 1:1 ---------------------
// AutoRocketJump on, AutoCTap off, debug vars = the k* defaults above. Dropped: the Debug::Info
// path drawing (no G::PathStorage overlay for it here), SDK::Output -> DebugLog.
void CAutoRocketJump::RunRJ(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, bool bLastGrounded, bool bCurrGrounded)
{
	if (!pLocal->IsAlive() || pLocal->IsAGhost() || !pWeapon || I::EngineVGui->IsGameUIVisible())
	{
		m_iFrame = -1;
		m_bRJ = m_bRunning = false;
		return;
	}

	if (m_iFrame == -1)
	{
		bool bDuck = pLocal->IsDucking();
		if ((bCurrGrounded ? bDuck || I::GlobalVars->curtime < pLocal->m_flDuckTimer() : !bDuck))
			return;
	}

	bool bValidWeapon = false, bBeggars = pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka;
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	case TF_WEAPON_PARTICLE_CANNON: bValidWeapon = true;
	}

	if (bValidWeapon && m_iFrame == -1 && (bCurrGrounded && bBeggars ? G::Attacking == 1 : G::CanPrimaryAttack || G::Reloading)
		&& SetAngles(pLocal, pWeapon, pCmd))
	{
		bool bWillHit = false;
		MoveStorage tMoveStorage;
		ProjectileInfo tProjInfo = {};

		bool bProjSimSetup = F::ProjSim.GetInfo(pLocal, pWeapon, m_vAngles, tProjInfo, ProjSimEnum::Trace | ProjSimEnum::InitCheck | ProjSimEnum::NoRandomAngles) && F::ProjSim.Initialize(tProjInfo);
		bool bMoveSimSetup = F::MoveSim.Initialize(pLocal, tMoveStorage, false); // do move sim after to not mess with proj sim
		if (bMoveSimSetup && bProjSimSetup)
		{
			int iSkip = bCurrGrounded ? kRJSkipGround : kRJSkipAir;

			CGameTrace trace = {};
			CTraceFilterCollideable filter = {}; filter.pSkip = pLocal;

			Vec3 vNew = F::ProjSim.GetOrigin();
			for (int n = 1; n < 10; n++)
			{
				F::MoveSim.RunTick(tMoveStorage);
				if (n <= iSkip || n == 1 && G::Reloading)
					continue;

				F::ProjSim.RunTick(tProjInfo);

				Vec3 vOld = vNew; vNew = F::ProjSim.GetOrigin();
				SDK::Trace(vOld, vNew, MASK_SOLID, &filter, &trace);
				if (trace.DidHit())
				{
					if (!pLocal->IsOnGround() || pLocal->IsSwimming())
						break;

					auto fWillHit = [](CTFPlayer* pLocal, const Vec3& vOrigin, const Vec3& vPoint)
					{
						const Vec3 vOriginal = pLocal->GetAbsOrigin();
						pLocal->SetAbsOrigin(vOrigin);
						Vec3 vPos; pLocal->m_Collision()->CalcNearestPoint(vPoint, &vPos);
						pLocal->SetAbsOrigin(vOriginal);

						float flRadiusSqr = powf(110.f * 1.1f, 2); // TF_ROCKET_RADIUS_FOR_RJS, tf_weaponbase_rocket.h:25
						return vPoint.DistToSqr(vPos) < flRadiusSqr && SDK::VisPosWorld(pLocal, pLocal, vPoint, vOrigin + pLocal->m_vecViewOffset(), MASK_SHOT);
					};

					bWillHit = fWillHit(pLocal, tMoveStorage.m_MoveData.m_vecAbsOrigin, trace.endpos + trace.plane.normal);
					if (bWillHit)
					{
						m_iDelay = n;
						if (kRJTimingOffset && n > kRJApplyAbove)
							m_iDelay = std::max(m_iDelay + kRJTimingOffset, 0);

						if (G::CanPrimaryAttack)
							DebugLog("ARJ: ticks to hit %d (%d) grounded=%d", m_iDelay, n, int(bCurrGrounded));
					}

					break;
				}
			}
		}
		F::MoveSim.Restore(tMoveStorage);

		if (bWillHit)
		{
			if (bCurrGrounded && bCurrGrounded == bLastGrounded)
				m_iFrame = 0;
			else if (!bCurrGrounded)
				m_iFrame = m_iDelay = 0;

			if (m_iFrame != -1)
			{
				m_bFull = true;
				m_iChoke = bCurrGrounded ? kRJChokeGrounded : kRJChokeAir;
			}
		}
	}

	if (m_iFrame != -1)
	{
		m_iFrame++;
		pCmd->buttons &= ~IN_JUMP;

		if (m_iFrame == 1)
		{
			if (!bBeggars)
				pCmd->buttons |= IN_ATTACK, pCmd->buttons &= ~IN_ATTACK2;

			if (m_bFull)
			{
				G::SilentAngles = bCurrGrounded;
				pCmd->viewangles = m_vAngles;
			}
		}

		if (bCurrGrounded && (m_iFrame - m_iDelay == 0 || !m_iDelay))
			pCmd->buttons |= IN_DUCK | IN_JUMP;

		if (m_iFrame <= m_iChoke)
			G::PSilentAngles = true, G::SilentAngles = false;

		if (m_iFrame == m_iDelay + 3)
			m_iFrame = -1;
	}

	m_bRunning = m_bRJ = m_iFrame != -1; // prevent stuff like antiaim messing with timing
}
