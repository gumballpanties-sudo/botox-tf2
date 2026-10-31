#include "aim_melee.h"

#include <chrono>

#include "aimbot.h"
#include "movesim.h"
#include "engineprediction.h"
#include "ticks.h"

// dih's log (src/dllmain.cpp). Off unless "debug log" is ticked in the menu.
void DebugLog(const char* fmt, ...);

// ============================================================================
// AUTO BACKSTAB -- dih-bs15, plus dih-bs16's turn guard (bs_math.h TurnGuarded, why=turn).
//
// The decision math, every server citation and the three bs14 defects it replaces are in
// bs_math.h, gated by tests/backstab_test.cpp (the server's own knife test is the ground truth
// there). This file only feeds that math the engine, in three places:
//
//   FRAME_NET_UPDATE_END  StoreBackstabSnapshots  one RAW record per player per new simulation
//                                                 time: yaw, origin, mins/maxs on the server clock
//   every command         UpdateBackstabTracks    one yaw bet per player for the moment the
//                                                 server will read it (audited when it arrives)
//   knife out             RunBackstab/JudgeKnife  every player's box where lag compensation will
//                                                 put it, the server's swing order, the odds
//
// Nothing here depends on cl_interp / cl_interp_ratio / cl_updaterate except through the server's
// own lerp formula (BsMath::ServerLerp), which is what makes it right on any net settings.
// ============================================================================
namespace
{
	constexpr int   k_maxPlayers = 65;
	// Past his newest record the server already has a newer one; this much of it may be guessed
	// from his velocity (lerp 0, low update rates). Beyond it Rewind prices the whole gap as error,
	// which the reach pad refuses.
	constexpr float k_maxExtrap  = 0.1f;
	// 1/8u origin grid (COORD_MP_LOWPRECISION, tf_player.cpp:748) + our own post-move prediction
	constexpr float k_posFloor   = 0.125f + 0.05f;
	// Silent aim: yaw offsets from the line to his centre. Facestab cannot bind inside +-17.46
	// (107.46 - 90), so those go first; the wide ones only matter to swing round a blocker.
	constexpr float k_yawOffsets[] = { 0.f, 8.f, -8.f, 16.f, -16.f, 30.f, -30.f, 45.f, -45.f };
	constexpr int   k_candMax    = sizeof(k_yawOffsets) / sizeof(k_yawOffsets[0]);

	BsMath::Track g_aTrack[k_maxPlayers];

	inline BsMath::V3 ToBs(const Vec3& v) { return { v.x, v.y, v.z }; }
	inline Vec3 FromBs(const BsMath::V3& v) { return { v.x, v.y, v.z }; }

	// The server-clock moment this command runs, which is when IsBehindAndFacingTarget reads his
	// LIVE pl.v_angle: our tick plus the round trip (the same estimate lag compensation makes,
	// player_lagcompensation.cpp:389-405, and Amalgam's backtrack, Backtrack.cpp:153).
	float BsDueTime(const CUserCmd* pCmd)
	{
		return TICKS_TO_TIME(pCmd->tick_count) + AimLatency();
	}

	// m_fLerpTime as the SERVER computes it from our userinfo (player.cpp:3484-3515).
	float BsServerLerp()
	{
		static auto cl_interpolate = H::ConVars.FindVar("cl_interpolate");
		static auto cl_interp = H::ConVars.FindVar("cl_interp");
		static auto cl_interp_ratio = H::ConVars.FindVar("cl_interp_ratio");
		static auto cl_updaterate = H::ConVars.FindVar("cl_updaterate");
		static auto sv_minupdaterate = H::ConVars.FindVar("sv_minupdaterate");
		static auto sv_maxupdaterate = H::ConVars.FindVar("sv_maxupdaterate");
		static auto sv_min_ratio = H::ConVars.FindVar("sv_client_min_interp_ratio");
		static auto sv_max_ratio = H::ConVars.FindVar("sv_client_max_interp_ratio");
		return BsMath::ServerLerp(
			cl_interpolate ? cl_interpolate->GetInt() != 0 : true,
			cl_interp ? cl_interp->GetFloat() : 0.1f,
			cl_interp_ratio ? cl_interp_ratio->GetFloat() : 1.f,
			cl_updaterate ? cl_updaterate->GetFloat() : 66.f,
			sv_minupdaterate ? sv_minupdaterate->GetFloat() : 0.f,
			sv_maxupdaterate ? sv_maxupdaterate->GetFloat() : 0.f,
			sv_min_ratio && sv_max_ratio ? sv_min_ratio->GetFloat() : -1.f,
			sv_max_ratio ? sv_max_ratio->GetFloat() : 5.f);
	}

	// ---- the swing's WORLD half --------------------------------------------------------------
	// Players are resolved analytically against the boxes lag compensation rewinds them to (the
	// client's copies sit a tick and a frame behind that), so this filter must never hit one.
	// Blacklist, not whitelist: the server swings with COLLISION_GROUP_NONE, which collides with
	// debris, weapons and projectiles alike (gamerules.cpp:713-751), so anything solid we do not
	// know to be passable is a blocker. A false block costs a tick; a false clear costs the swing.
	class CTraceFilterSwingWorld : public ITraceFilter
	{
	public:
		CBaseEntity* pSkip = nullptr;
		int iTeam = -1;

		bool ShouldHitEntity(IHandleEntity* pServerEntity, int nContentsMask) override
		{
			if (!pServerEntity || pServerEntity == pSkip)
				return false;
			if (pServerEntity->GetRefEHandle().GetSerialNumber() == (1 << 15))
				return I::ClientEntityList->GetClientEntity(0) != pSkip;   // the world

			auto pEntity = reinterpret_cast<CBaseEntity*>(pServerEntity);
			if (pEntity->entindex() < 0)
				return false;   // client-only (ragdolls, gibs, effects): the server never saw it
			if (iTeam == -1)
				iTeam = pSkip ? pSkip->m_iTeamNum() : 0;
			if (pSkip && pEntity->m_hOwnerEntity().Get() == pSkip)
				return false;   // our own projectiles: CTraceFilterSimple's owner pass-through

			switch (pEntity->GetClassID())
			{
			case ETFClassID::CTFPlayer:                  // resolved on rewound boxes instead
			case ETFClassID::CFuncRespawnRoomVisualizer: // respawn rooms only collide with players (tf_gamerules.cpp:18062)
			case ETFClassID::CFuncAreaPortalWindow:
			case ETFClassID::CTFAmmoPack:
				return false;
			// the only combat items (tf_weapon_medigun.h:262, tf_revive.h:47): own team is stepped
			// over by CTraceFilterIgnoreFriendlyCombatItems (tf_weaponbase.h:191). Buildings are NOT
			// combat items -- both teams' block the knife.
			case ETFClassID::CTFMedigunShield:
			case ETFClassID::CTFReviveMarker:
				return pEntity->m_iTeamNum() != iTeam;
			}
			return true;
		}
		TraceType_t GetTraceType() const override { return TRACE_EVERYTHING; }
	};

	// CanPerformBackstabAgainstTarget's refusals (knife:405-408) and the Razorback. A string hash
	// plus an attribute walk each, so memoised per target per command by the caller.
	bool BsRazorbackBlocked(CBaseEntity* pTarget)
	{
		if (SDK::AttribHookValue(0, "cannot_be_backstabbed", pTarget))
			return true;   // on the server this is not blocked, it is a 40-damage swing -- never worth it
		if (!(Vars::Aimbot::Melee::BackstabIgnore.Value & Vars::Aimbot::Melee::BackstabIgnoreEnum::Razorback))
			return false;
		CUtlVector<CBaseEntity*> itemList;
		int iBackstabShield = SDK::AttribHookValue(0, "set_blockbackstab_once", pTarget, &itemList);
		if (!iBackstabShield || !itemList.Count())
			return false;
		CBaseEntity* pShield = itemList.Element(0);
		return pShield && pShield->ShouldDraw();
	}
}

// FRAME_NET_UPDATE_END, straight after H::Entities.Store. PreDataUpdate has restored every
// interpolated var to its last networked value (c_baseentity.cpp:2166) and render-start
// interpolation has not run yet, so m_angEyeAngles / m_vecOrigin here ARE the server's record for
// m_flSimulationTime. A player with no new simulation time was not in the packet and his members
// may still hold last frame's interpolated values -- AddSnapshot refuses the duplicate time.
void CAimbotMelee::StoreBackstabSnapshots()
{
	if (!Vars::Aimbot::Melee::AutoBackstab.Value)
		return;

	const int iLocal = I::EngineClient->GetLocalPlayer();
	const int iMax = std::min(I::EngineClient->GetMaxClients(), k_maxPlayers - 1);
	const float flPacket = I::EngineClient->GetLastTimeStamp();   // the absolute server clock (bs_math.h: TWO CLOCKS)
	for (int i = 1; i <= iMax; i++)
	{
		BsMath::Track& tr = g_aTrack[i];
		auto pClient = I::ClientEntityList->GetClientEntity(i);
		auto pPlayer = pClient ? pClient->As<CTFPlayer>() : nullptr;
		if (i == iLocal || !pPlayer || !pPlayer->IsPlayer() || pPlayer->IsDormant() || !pPlayer->IsAlive())
		{	// a dormant or dead player's history must not survive into his next appearance
			if (tr.bLive)
				BsMath::Reset(tr);
			continue;
		}
		BsMath::AddSnapshot(tr, pPlayer->m_flSimulationTime(), flPacket, pPlayer->m_angEyeAnglesY(),
			ToBs(pPlayer->m_vecOrigin()), ToBs(pPlayer->m_vecMins()), ToBs(pPlayer->m_vecMaxs()));
	}
}

// Every command, every player, knife out or not: the audit behind the odds has to be warm the
// moment the knife comes out. One prediction each -- a few float ops.
void CAimbotMelee::UpdateBackstabTracks()
{
	if (!Vars::Aimbot::Melee::AutoBackstab.Value || !G::CurrentUserCmd)
		return;
	const float flDue = BsDueTime(G::CurrentUserCmd);
	for (int i = 1; i < k_maxPlayers; i++)
		BsMath::PushPrediction(g_aTrack[i], flDue);
}

bool CAimbotMelee::IsKnifeBackstab(CTFWeaponBase* pWeapon)
{
	// BackstabOn(), not the raw toggle: this is the gate every acting path runs through
	// (aim type, target pick, the swing), so the key lands on all three at once.
	return pWeapon && pWeapon->GetWeaponID() == TF_WEAPON_KNIFE && F::Aimbot.BackstabOn();
}

// the KNIFE dropdown stores 0 (Silent) / 1 (Legit).
int CAimbotMelee::GetBackstabAimType()
{
	return Vars::Aimbot::Melee::BackstabAimType.Value == Vars::Aimbot::Melee::BackstabAimTypeEnum::Legit
		? Vars::Aimbot::General::AimTypeEnum::Smooth
		: Vars::Aimbot::General::AimTypeEnum::Silent;
}

int CAimbotMelee::GetAimType(CTFWeaponBase* pWeapon)
{
	return IsKnifeBackstab(pWeapon) ? GetBackstabAimType() : Vars::Aimbot::General::AimType.Value;
}

// Legit is a flat 180 (it never moves the view, so a cone there could only ever refuse); Silent
// uses the Backstab FOV slider. The knife's cone does NOT cull in GetTargets any more -- it is a
// per-target gate inside JudgeKnife, so what it refuses shows up in the log as why=fov instead of
// the target simply never existing. It is the one knob left that can throw away a finished
// backstab, and it has been found parked at ~45 three sessions running.
float CAimbotMelee::GetFOV(CTFWeaponBase* pWeapon)
{
	if (IsKnifeBackstab(pWeapon))
		return m_bBackstabLegit ? 180.f : Vars::Aimbot::Melee::BackstabFOV.Value;
	return Vars::Aimbot::General::AimFOV.Value;
}

std::vector<Target_t> CAimbotMelee::GetTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	std::vector<Target_t> vTargets;

	const Vec3 vLocalPos = F::Ticks.GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

	// The knife never comes through here: RunBackstab builds its targets from the rewound boxes.
	const int iIgnoreOverride = -1;

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Players)
	{
		auto eGroupType = !F::AimbotGlobal.FriendlyFire() ? EntityEnum::PlayerEnemy : EntityEnum::PlayerAll;

		for (auto pEntity : H::Entities.GetGroup(eGroupType))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon, iIgnoreOverride))
				continue;

			float flFOVTo = 0.f; Vec3 vPos, vAngleTo;
			if (!F::AimbotGlobal.PlayerBoneInFOV(pEntity->As<CTFPlayer>(), vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo,
				Vars::Aimbot::Hitscan::HitboxesEnum::Head | Vars::Aimbot::Hitscan::HitboxesEnum::Body | Vars::Aimbot::Hitscan::HitboxesEnum::Pelvis | Vars::Aimbot::Hitscan::HitboxesEnum::Arms | Vars::Aimbot::Hitscan::HitboxesEnum::Legs,
				GetFOV(pWeapon)))
				continue;

			bool bTeam = pEntity->m_iTeamNum() == pLocal->m_iTeamNum();
			int iPriority = F::AimbotGlobal.GetPriority(pEntity->entindex());
			if (bTeam && !F::AimbotGlobal.FriendlyFire())
				iPriority = 0;

			float flDistTo = vLocalPos.DistTo(vPos);
			vTargets.emplace_back(pEntity, TargetEnum::Player, vPos, vAngleTo, flFOVTo, flDistTo, iPriority);
		}
	}

	{
		auto eGroupType = EntityEnum::Invalid;
		if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Building)
			eGroupType = EntityEnum::BuildingEnemy;
		// friendly buildings are never a target -- there is no repair path here.
		bool bWrench = pWeapon->GetWeaponID() == TF_WEAPON_WRENCH, bSapper = SDK::AttribHookValue(0, "set_dmg_apply_to_sapper", pWeapon);
		for (auto pEntity : H::Entities.GetGroup(eGroupType))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon, iIgnoreOverride))
				continue;

			bool bTeam = pEntity->m_iTeamNum() == pLocal->m_iTeamNum();
			if (bTeam && (bWrench && !AimFriendlyBuilding(pEntity->As<CBaseObject>()) || bSapper && !pEntity->As<CBaseObject>()->m_bHasSapper()))
				continue;

			Vec3 vPos = pEntity->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			if (flFOVTo > Vars::Aimbot::General::AimFOV.Value)
				continue;

			int iPriority = 0;

			float flDistTo = vLocalPos.DistTo(vPos);
			vTargets.emplace_back(pEntity, pEntity->IsSentrygun() ? TargetEnum::Sentry : pEntity->IsDispenser() ? TargetEnum::Dispenser : TargetEnum::Teleporter, vPos, vAngleTo, flFOVTo, flDistTo, iPriority);
		}
	}

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::NPCs)
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldNPC))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon, iIgnoreOverride))
				continue;

			Vec3 vPos = pEntity->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			if (flFOVTo > Vars::Aimbot::General::AimFOV.Value)
				continue;

			float flDistTo = vLocalPos.DistTo(vPos);
			vTargets.emplace_back(pEntity, TargetEnum::NPC, vPos, vAngleTo, flFOVTo, flDistTo);
		}
	}

	return vTargets;
}

bool CAimbotMelee::AimFriendlyBuilding(CBaseObject* pBuilding)
{
	if (!pBuilding->m_bMiniBuilding() && pBuilding->m_iUpgradeLevel() != 3 || pBuilding->m_iHealth() < pBuilding->m_iMaxHealth() || pBuilding->m_bHasSapper())
		return true;

	if (pBuilding->IsSentrygun())
	{
		int iShells, iMaxShells, iRockets, iMaxRockets; pBuilding->As<CObjectSentrygun>()->GetAmmoCount(iShells, iMaxShells, iRockets, iMaxRockets);
		if (iShells < iMaxShells || iRockets < iMaxRockets)
			return true;
	}

	return false;
}

std::vector<Target_t> CAimbotMelee::SortTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	auto vTargets = GetTargets(pLocal, pWeapon);

	F::AimbotGlobal.SortTargets(vTargets, Vars::Aimbot::General::TargetSelectionEnum::Distance);
	// the knife never comes through here (RunBackstab has no target cap at all)
	vTargets.resize(std::min(size_t(Vars::Aimbot::General::MaxTargets.Value), vTargets.size()));
	F::AimbotGlobal.SortPriority(vTargets);
	return vTargets;
}



int CAimbotMelee::GetSwingTime(CTFWeaponBase* pWeapon, bool bVar)
{
	if (pWeapon->GetWeaponID() == TF_WEAPON_KNIFE)
		return 0;
	int iSmackTicks = ceilf(pWeapon->GetSmackDelay() / TICK_INTERVAL);
	if (bVar)
		iSmackTicks = std::max(iSmackTicks + Vars::Aimbot::Melee::SwingOffset.Value, 0);
	return iSmackTicks;
}

void CAimbotMelee::UpdateInfo(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, std::vector<Target_t> vTargets)
{
	m_mRecordMap.clear(); m_mPaths.clear();
	m_iDoubletapTicks = F::Ticks.GetTicks(pWeapon);
	m_vEyePos = pLocal->GetShootPos();
	m_flRange = pWeapon->GetSwingRange();

	int iSimTicks = GetSwingTime(pWeapon), iSwingTicks = GetSwingTime(pWeapon, false);

	if ((Vars::Aimbot::Melee::SwingPrediction.Value && iSimTicks || m_iDoubletapTicks) && G::CanPrimaryAttack && pWeapon->m_flSmackTime() < 0.f)
	{
		std::unordered_map<int, MoveStorage> mStorage;

		F::MoveSim.Initialize(pLocal, mStorage[I::EngineClient->GetLocalPlayer()], false, !m_iDoubletapTicks);
		for (auto& tTarget : vTargets)
			F::MoveSim.Initialize(tTarget.m_pEntity, mStorage[tTarget.m_pEntity->entindex()], false);

		int iMax = std::max(iSimTicks, m_iDoubletapTicks);
		int iTicks = iMax; bool bSwung = false;
		for (int i = 0; i < iTicks; i++) // intended for plocal to collide with targets
		{
			{
				auto& tStorage = mStorage[I::EngineClient->GetLocalPlayer()];

				if (!bSwung && (!m_iDoubletapTicks || Vars::Doubletap::AntiWarp.Value && pLocal->m_hGroundEntity() || iMax - i <= iSwingTicks))
				{
					iTicks = std::min(i + iSwingTicks, iMax), bSwung = true;
					if (!iSwingTicks)
						break;

					if (pLocal->InCond(TF_COND_SHIELD_CHARGE))
					{	// demo charge fix for swing pred
						tStorage.m_MoveData.m_flMaxSpeed = tStorage.m_MoveData.m_flClientMaxSpeed = SDK::MaxSpeed(pLocal, false, true);
						pLocal->m_flMaxspeed() = tStorage.m_MoveData.m_flMaxSpeed;
						pLocal->RemoveCond(TF_COND_SHIELD_CHARGE);
					}
				}
				if (m_iDoubletapTicks && Vars::Doubletap::AntiWarp.Value && pLocal->m_hGroundEntity())
					F::Ticks.AntiWarp(pLocal, pCmd->viewangles.y, tStorage.m_MoveData.m_flForwardMove, tStorage.m_MoveData.m_flSideMove, iMax - i - 1);

				F::MoveSim.RunTick(tStorage);
				m_mRecordMap[I::EngineClient->GetLocalPlayer()].emplace_front(
					pLocal->m_flSimulationTime() + TICKS_TO_TIME(i + 1),
					tStorage.m_MoveData.m_vecAbsOrigin,
					pLocal->m_vecMins(), pLocal->m_vecMaxs()
				);
			}

			if (i < iSimTicks - m_iDoubletapTicks)
			{
				for (auto& tTarget : vTargets)
				{
					auto& tStorage = mStorage[tTarget.m_pEntity->entindex()];
					if (tStorage.m_bFailed)
						continue;

					F::MoveSim.RunTick(tStorage);
					m_mRecordMap[tTarget.m_pEntity->entindex()].emplace_front(
						!Vars::Aimbot::Melee::SwingPredictLag.Value || tStorage.m_bPredictNetworked ? tTarget.m_pEntity->m_flSimulationTime() + TICKS_TO_TIME(i + 1) : 0.f,
						Vars::Aimbot::Melee::SwingPredictLag.Value ? tStorage.m_vPredictedOrigin : tStorage.m_MoveData.m_vecAbsOrigin,
						tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs()
					);
				}
			}
		}
		m_vEyePos = mStorage[I::EngineClient->GetLocalPlayer()].m_MoveData.m_vecAbsOrigin + pLocal->m_vecViewOffset();
		m_flRange = pWeapon->GetSwingRange();

		if (Vars::Visuals::Simulation::SwingLines.Value && Vars::Visuals::Simulation::PlayerPath.Value)
		{
			for (auto& [iIndex, tStorage] : mStorage)
				m_mPaths[iIndex] = tStorage.m_vPath;

			const bool bAlwaysDraw = !Vars::Aimbot::General::AutoShoot.Value || Vars::Debug::Info.Value;
			if (bAlwaysDraw)
			{
				G::LineStorage.clear();
				G::BoxStorage.clear();
				G::PathStorage.clear();

				for (auto& [_, vPath] : m_mPaths)
				{
					if (Vars::Colors::PlayerPathIgnoreZ.Value.a)
						G::PathStorage.emplace_back(vPath, I::GlobalVars->curtime + Vars::Visuals::Simulation::DrawDuration.Value, Vars::Colors::PlayerPathIgnoreZ.Value, Vars::Visuals::Simulation::PlayerPath.Value);
					if (Vars::Colors::PlayerPath.Value.a)
						G::PathStorage.emplace_back(vPath, I::GlobalVars->curtime + Vars::Visuals::Simulation::DrawDuration.Value, Vars::Colors::PlayerPath.Value, Vars::Visuals::Simulation::PlayerPath.Value, true);
				}
			}
		}

		for (auto& [_, tStorage] : mStorage)
			F::MoveSim.Restore(tStorage);
	}

	m_bShouldSwing = m_iDoubletapTicks <= iSwingTicks || Vars::Doubletap::AntiWarp.Value && pLocal->m_hGroundEntity();

	// Snapshot the finalized (un-predicted) eye so each CanHit can restore it before optionally
	// advancing one move-tick for its own target (see BackstabPredictMovement).
	m_vEyePosBase = m_vEyePos;
}

const char* BsGateName(int iGate)
{
	switch (iGate)
	{
	case BSG_FIRE:      return "fire";
	case BSG_COOLDOWN:  return "cooldown";
	case BSG_NOTARGET:  return "notarget";
	case BSG_NOTRACK:   return "notrack";
	case BSG_RANGE:     return "range";
	case BSG_FOV:       return "fov";
	case BSG_BLOCKED:   return "blocked";
	case BSG_TRACEMISS: return "tracemiss";
	case BSG_IGNORED:   return "ignored";
	case BSG_REACH:     return "reach";
	case BSG_FACING:    return "facing";
	case BSG_ODDS:      return "odds";
	case BSG_TURN:      return "turn";
	case BSG_HOLD:      return "hold";
	case BSG_THIN:      return "thin";
	}
	return "?";
}

// =============================================================================================
// THE KNIFE, ONE TARGET, ONE COMMAND -- dih-bs15
// =============================================================================================
// 1. candidate swing angles: Legit has exactly one (the command's own), Silent a fan of yaws round
//    the line to his centre
// 2. each one is resolved the way DoSwingTraceInternal would resolve it -- ray first, hull only if
//    the ray hit nothing -- against every player's REWOUND box (analytic) and the world (traced),
//    best odds first, until one reaches a player that can be judged
// 3. whoever that returns is scored: odds of a backstab on the server, reach slack, and the same
//    one tick later, and BsMath::Decide says fire / hold / refuse
bool CAimbotMelee::JudgeKnife(int iBox, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, BsJudge& tOut)
{
	tOut = {};
	const BsMath::Box tBox = m_aBoxes[iBox];
	const int iIndex = tBox.id;
	CBaseEntity* pTarget = m_aBoxEnt[iBox];
	tOut.m_iIndex = iIndex;

	if (!g_aTrack[iIndex].bLive || m_aBoxExtrap[iBox] >= BsMath::k_unknown)
	{	// not enough of him yet to know where lag compensation puts him (just out of dormancy)
		tOut.m_iGate = BSG_NOTRACK;
		return false;
	}

	const float flRange = m_flKnifeRange, flHull = m_flKnifeHull;
	const BsMath::V3 vEye = ToBs(m_vKnifeEye);
	const BsMath::V3 vCentre = (tBox.lo + tBox.hi) * 0.5f;
	tOut.m_flDist = BsMath::Len2D(vCentre - vEye);

	// nothing of the swept cube can reach his rewound box along any angle
	{
		const BsMath::V3 vNear = { std::clamp(vEye.x, tBox.lo.x, tBox.hi.x), std::clamp(vEye.y, tBox.lo.y, tBox.hi.y),
			std::clamp(vEye.z, tBox.lo.z, tBox.hi.z) };
		const BsMath::V3 d = vNear - vEye;
		if (sqrtf(d.x * d.x + d.y * d.y + d.z * d.z) > flRange + flHull * 1.7320508f)
		{
			tOut.m_iGate = BSG_RANGE;
			return false;
		}
	}

	if (Vars::Aimbot::Melee::BackstabIgnore.Value & Vars::Aimbot::General::IgnoreEnum::Unsimulated
		&& H::Entities.GetChoke(iIndex) > Vars::Aimbot::General::TickTolerance.Value)
	{
		tOut.m_iGate = BSG_IGNORED;
		return false;
	}

	// ---- 1. candidates ----------------------------------------------------------------------------
	struct Cand
	{
		Vec3 vAng;
		BsMath::V3 vDir;
		int iRay = -1, iHull = -1;
		float tRay = 0.f, tHull = 0.f, flScore = -2.f;
	};
	Cand aCand[k_candMax];
	int nCand = 0;
	if (m_bBackstabLegit)
		aCand[nCand++].vAng = m_vKnifeView;
	else
	{
		const float flBearing = BsMath::Deg(atan2f(vCentre.y - vEye.y, vCentre.x - vEye.x));
		const Vec3 vPoint = { vCentre.x, vCentre.y, std::clamp(vEye.z, tBox.lo.z + 1.f, tBox.hi.z - 1.f) };
		const float flPitch = Math::CalcAngle(m_vKnifeEye, vPoint).x;
		const float flFOV = GetFOV(pWeapon);
		for (int i = 0; i < k_candMax; i++)
		{
			Vec3 vAng = { flPitch, BsMath::Wrap(flBearing + k_yawOffsets[i]), 0.f };
			Math::ClampAngles(vAng);
			if (flFOV < 180.f && Math::CalcFov(I::EngineClient->GetViewAngles(), vAng) > flFOV)
				continue;
			aCand[nCand++].vAng = vAng;
		}
		if (!nCand)
		{	// the leash refused every angle that could swing at him -- out loud, it is a user knob
			tOut.m_iGate = BSG_FOV;
			return false;
		}
	}

	// ---- 2a. analytic resolution against every player, and a cheap score to trace the best first --
	const int iTeam = pLocal->m_iTeamNum();
	auto fPosErr = [&](int b) { return BsMath::PositionError(m_aBoxSpeed[b], m_aBoxExtrap[b], k_posFloor); };
	// turn-guarded (bs_math.h TurnGuarded): the swing is decided on .p, the smaller of the odds on
	// his predicted yaw and at the guard point (seen + 40% of the turn). flSeenLead > 0 for a later tick.
	auto fOdds = [&](int b, BsMath::V3 vFrom, BsMath::V3 vLo, BsMath::V3 vHi, float flYaw, float flDue, float flSeenLead = 0.f) -> BsMath::GuardedOdds
	{
		const BsMath::V3 c = (vLo + vHi) * 0.5f;
		const float flDist = std::max(BsMath::Len2D(c - vFrom), 1.f);
		const BsMath::YawEstimate e = BsMath::Estimate(g_aTrack[m_aBoxes[b].id], flDue);
		return BsMath::TurnGuarded(BsMath::Deg(atan2f(c.y - vFrom.y, c.x - vFrom.x)), flYaw, e,
			BsMath::Deg(atan2f(fPosErr(b), flDist)), flSeenLead);
	};
	for (int i = 0; i < nCand; i++)
	{
		Cand& c = aCand[i];
		Vec3 vFwd; Math::AngleVectors(c.vAng, &vFwd);
		c.vDir = ToBs(vFwd);
		c.iRay = BsMath::FirstBox(vEye, c.vDir, flRange, 0.f, m_aBoxes, m_iBoxes, c.tRay);
		c.iHull = BsMath::FirstBox(vEye, c.vDir, flRange, flHull, m_aBoxes, m_iBoxes, c.tHull);
		const int iHit = c.iRay >= 0 ? c.iRay : c.iHull;
		if (iHit < 0)
			continue;   // reaches nobody even before the world gets a say
		const bool bEnemy = m_aBoxTeam[iHit] != iTeam && g_aTrack[m_aBoxes[iHit].id].bLive;
		c.flScore = bEnemy ? fOdds(iHit, vEye, m_aBoxes[iHit].lo, m_aBoxes[iHit].hi, c.vAng.y, m_flTDue).p : -1.f;   // guarded
	}
	std::stable_sort(aCand, aCand + nCand, [](const Cand& a, const Cand& b) { return a.flScore > b.flScore; });
	if (aCand[0].flScore < -1.5f)
	{
		tOut.m_iGate = BSG_RANGE;
		return false;
	}

	// ---- 2b. the world, best odds first, until one reaches a player we can judge ------------------
	CTraceFilterSwingWorld filter = {};
	filter.pSkip = pLocal;
	CGameTrace trace = {};
	const Vec3 vHullMin = { -flHull, -flHull, -flHull }, vHullMax = { flHull, flHull, flHull };
	for (int iC = 0; iC < nCand; iC++)
	{
		const Cand& c = aCand[iC];
		if (c.flScore < -1.5f)
			break;   // sorted: nobody along any of the rest
		auto fGate = [&](int iGate) { if (iGate > tOut.m_iGate) tOut.m_iGate = iGate; };

		const Vec3 vEnd = m_vKnifeEye + FromBs(c.vDir) * flRange;
		SDK::TraceHull(m_vKnifeEye, vEnd, {}, {}, MASK_SOLID, &filter, &trace);
		const float flRayFrac = trace.fraction;
		float flHullFrac = -1.f;   // not traced yet
		auto fHullFrac = [&]()
		{
			if (flHullFrac < 0.f)
			{
				SDK::TraceHull(m_vKnifeEye, vEnd, vHullMin, vHullMax, MASK_SOLID, &filter, &trace);
				flHullFrac = trace.startsolid ? 0.f : trace.fraction;
			}
			return flHullFrac;
		};
		const int iRes = BsMath::ResolveSwing(flRange, c.iRay, c.tRay, flRayFrac, c.iHull, c.tHull,
			BsMath::NeedsHullPass(c.iRay, flRayFrac) ? fHullFrac() : 1.f);
		if (iRes == BsMath::SWING_WORLD) { fGate(BSG_BLOCKED); continue; }
		if (iRes < 0) { fGate(BSG_TRACEMISS); continue; }

		const int iHitIdx = m_aBoxes[iRes].id;
		CBaseEntity* pHit = m_aBoxEnt[iRes];
		if (m_aBoxTeam[iRes] == iTeam) { fGate(BSG_BLOCKED); continue; }   // teammates stop the knife
		if (m_aRazorTick[iHitIdx] != I::GlobalVars->tickcount)
		{
			m_aRazorTick[iHitIdx] = I::GlobalVars->tickcount;
			m_aRazorBlocked[iHitIdx] = BsRazorbackBlocked(pHit);
		}
		if (m_aRazorBlocked[iHitIdx]
			|| pHit != pTarget && (F::AimbotGlobal.ShouldIgnore(pHit, pLocal, pWeapon, Vars::Aimbot::Melee::BackstabIgnore.Value)
				|| Vars::Aimbot::Melee::BackstabIgnore.Value & Vars::Aimbot::General::IgnoreEnum::Unsimulated
					&& H::Entities.GetChoke(iHitIdx) > Vars::Aimbot::General::TickTolerance.Value))
		{
			fGate(BSG_IGNORED);
			continue;
		}
		const BsMath::Track& trHit = g_aTrack[iHitIdx];
		if (!trHit.bLive || m_aBoxExtrap[iRes] >= BsMath::k_unknown) { fGate(BSG_NOTRACK); continue; }

		// ---- 3. the numbers -----------------------------------------------------------------------
		const BsMath::Box& h = m_aBoxes[iRes];
		const BsMath::V3 vHC = (h.lo + h.hi) * 0.5f;
		const BsMath::YawEstimate e = BsMath::Estimate(trHit, m_flTDue);
		const BsMath::GuardedOdds g = fOdds(iRes, vEye, h.lo, h.hi, c.vAng.y, m_flTDue);
		const BsMath::StabOdds& o = g.best;   // margin / facing: the geometry of the bet
		const float flPos = fPosErr(iRes);
		const float flPad = 0.25f + flPos;
		const float flSlack = BsMath::ReachSlack(vEye, c.vDir, flRange, h.lo, h.hi, flHull);
		// the trace connected; a slab disagreement would mean the analytic box is wrong, not the swing
		bool bReach = flSlack < 0.f || flSlack >= flPad;
		// A RAY THAT ONLY GRAZES HIM is a ray the server may miss by the pad -- and then it does not
		// stop, it runs the hull pass (melee:509), which has to land on him as well. Past a world hit
		// the ray does not get a hull pass at all.
		if (bReach && iRes == c.iRay && BsMath::SegmentEntry(vEye, c.vDir, flRange, h.lo, h.hi, -flPad) < 0.f)
			bReach = flRayFrac >= 1.f && BsMath::ResolveSwing(flRange, -1, 0.f, 1.f, c.iHull, c.tHull, fHullFrac()) == iRes;

		// one tick on: both of us move, he turns, and the server reads him a tick later. Silent keeps
		// the same offset from the (new) line to him; Legit keeps the player's own angle.
		const BsMath::V3 vStep = BsMath::Velocity(trHit) * TICK_INTERVAL;
		const BsMath::V3 vEyeNext = vEye + ToBs(pLocal->m_vecVelocity()) * TICK_INTERVAL;
		const BsMath::V3 vLoN = h.lo + vStep, vHiN = h.hi + vStep, vCN = (vLoN + vHiN) * 0.5f;
		Vec3 vAngNext = c.vAng;
		if (!m_bBackstabLegit)
		{
			const float flBearNow = BsMath::Deg(atan2f(vHC.y - vEye.y, vHC.x - vEye.x));
			const float flBearNext = BsMath::Deg(atan2f(vCN.y - vEyeNext.y, vCN.x - vEyeNext.x));
			vAngNext.y = BsMath::Wrap(c.vAng.y + BsMath::Wrap(flBearNext - flBearNow));
		}
		Vec3 vFwdN; Math::AngleVectors(vAngNext, &vFwdN);
		const float flSlackNext = BsMath::ReachSlack(vEyeNext, ToBs(vFwdN), flRange, vLoN, vHiN, flHull);
		const bool bReachNext = flSlack < 0.f ? true : flSlackNext >= flPad;
		// a choker's next packet shows nothing new of him: no seen lead then (else the next tick's
		// guard is overstated and the swing holds for a tick that does not come)
		const BsMath::GuardedOdds gN = fOdds(iRes, vEyeNext, vLoN, vHiN, vAngNext.y, m_flTDue + TICK_INTERVAL,
			BsMath::Choking(trHit) ? 0.f : TICK_INTERVAL);

		BsHold& tHold = m_aHold[iHitIdx];
		if (I::GlobalVars->tickcount - tHold.m_iTick > 1 || I::GlobalVars->tickcount < tHold.m_iTick)
			tHold.m_iCount = 0;   // a fresh hold, not the tail of an old one

		// thin reach (bs_math.h ThinPad): our own server position is a prediction too
		const BsMath::V3 vRel = BsMath::Velocity(trHit) - ToBs(pLocal->m_vecVelocity());
		const float flRel = BsMath::Len2D(vRel);
		const float flThin = BsMath::ThinPad(flPad, flRel);
		const bool bThin = flSlack >= 0.f && flSlack < flThin;
		const bool bSolidNext = flSlackNext >= flThin;

		const BsMath::EDecision eDec = BsMath::Decide(g.p, gN.p, bReachNext, tHold.m_iCount, bThin, bSolidNext);
		// why=turn: bs15 would have swung on the bet alone this tick and the guard stopped it -- by
		// refusing outright or by turning a sure swing into a hold
		const bool bGuardStopped = eDec != BsMath::DEC_FIRE
			&& BsMath::Decide(o.p, gN.best.p, bReachNext, tHold.m_iCount, bThin, bSolidNext) == BsMath::DEC_FIRE;
		// why=thin: the hold is the reach's, not the odds' (without it this tick would fire)
		const bool bThinHeld = eDec == BsMath::DEC_HOLD
			&& BsMath::Decide(g.p, gN.p, bReachNext, tHold.m_iCount) == BsMath::DEC_FIRE;
		const int iGate = !bReach ? BSG_REACH
			: o.facing <= 0.f ? BSG_FACING
			: bGuardStopped ? BSG_TURN
			: eDec == BsMath::DEC_ODDS ? BSG_ODDS
			: bThinHeld ? BSG_THIN
			: eDec == BsMath::DEC_HOLD ? BSG_HOLD
			: BSG_FIRE;

		tOut.m_iGate = iGate;
		tOut.m_bFire = iGate == BSG_FIRE;
		tOut.m_iIndex = iHitIdx;
		tOut.m_vAim = c.vAng;
		tOut.m_flP = g.p; tOut.m_flPNext = gN.p;
		tOut.m_flPBest = o.p; tOut.m_flPGuard = g.pGuard;
		tOut.m_flGuardMargin = g.guardMargin; tOut.m_flSigCore = e.sigmaCore;
		tOut.m_flMargin = o.margin; tOut.m_flFacing = o.facing;
		tOut.m_flSigma = e.sigma;
		tOut.m_flSigBear = BsMath::Deg(atan2f(flPos, std::max(BsMath::Len2D(vHC - vEye), 1.f)));
		tOut.m_flRate = e.rate; tOut.m_flHorizon = e.horizon; tOut.m_flExtrap = m_aBoxExtrap[iRes];
		tOut.m_flSlack = flSlack; tOut.m_flPad = flPad;
		tOut.m_flDist = BsMath::Len2D(vHC - vEye);
		tOut.m_flSpeed = m_aBoxSpeed[iRes];
		tOut.m_flRel = flRel;
		tOut.m_iK = e.k; tOut.m_iHeld = tHold.m_iCount;
		tOut.m_bReachNext = bReachNext;

		if (tOut.m_bFire)
		{
			tHold = {};
			return true;
		}
		if (iGate == BSG_REACH)
			continue;   // an angle with slightly worse odds may connect solidly
		// counted on the DECISION, not the label: a guard-made hold logs why=turn and must still run
		// out after k_patience ticks
		if ((iGate == BSG_HOLD || iGate == BSG_TURN || iGate == BSG_THIN) && eDec == BsMath::DEC_HOLD && tHold.m_iTick != I::GlobalVars->tickcount)
		{	// once per command even when two targets' swings resolve onto the same man
			++tHold.m_iCount;
			tHold.m_iTick = I::GlobalVars->tickcount;
		}
		// The candidates are sorted by these same odds, so the first one that reaches a judgeable
		// player IS the best this target offers: the rest could only be worse or reach somebody else,
		// and every other enemy in reach gets his own JudgeKnife.
		return false;
	}
	return false;
}

// NON-KNIFE MELEE ONLY. The knife's auto backstab left this function at dih-bs13 -- it lives in
// JudgeKnife above, where the trace mirrors DoSwingTraceInternal exactly (ray first, hull only if
// the ray missed everything), the swing is judged on whoever the trace RETURNS rather than on the
// target we picked, and the decision carries its own gate for the log. Nothing in here knows
// about backstabs any more.
int CAimbotMelee::CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	const int iAimType = GetAimType(pWeapon);

	if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Unsimulated
		&& H::Entities.GetChoke(tTarget.m_pEntity->entindex()) > Vars::Aimbot::General::TickTolerance.Value)
		return false;

	float flRange = SDK::AttribHookValue(m_flRange, "melee_range_multiplier", pWeapon);
	float flHull = SDK::AttribHookValue(18, "melee_bounds_multiplier", pWeapon);
	if (pLocal->m_flModelScale() > 1.0f)
	{
		flRange *= pLocal->m_flModelScale();
		flHull *= pLocal->m_flModelScale();
	}
	if (pWeapon->GetWeaponID() == TF_WEAPON_WRENCH && tTarget.m_pEntity->m_iTeamNum() == pLocal->m_iTeamNum())
	{
		flRange = 70;
		flHull = 18;
	}
	Vec3 vSwingMins = { -flHull, -flHull, -flHull };
	Vec3 vSwingMaxs = { flHull, flHull, flHull };

	m_vEyePos = m_vEyePosBase;
	auto& vSimRecords = m_mRecordMap[tTarget.m_pEntity->entindex()];

	// No tick history: melee always aims at the live position. Swing-prediction records still apply.
	std::vector<TickRecord*> vRecords = {};
	{
		// field by field, not `m_tLiveRecord = { ... }`: TickRecord carries
		// matrix3x4 m_aBones[MAXSTUDIOBONES] = 6144 bytes, so the aggregate built a zeroed temp
		// and copy-assigned 6.2 KB per target per command
		m_tLiveRecord.m_flSimTime = tTarget.m_pEntity->m_flSimulationTime();
		m_tLiveRecord.m_vOrigin = tTarget.m_pEntity->m_vecOrigin();
		m_tLiveRecord.m_vMins = tTarget.m_pEntity->m_vecMins();
		m_tLiveRecord.m_vMaxs = tTarget.m_pEntity->m_vecMaxs();
		m_tLiveRecord.m_bOnShot = m_tLiveRecord.m_bInvalid = false;
		// BONE_USED_BY_HITBOX, not BONE_USED_BY_ANYTHING -- see the note in
		// CAimbotGlobal::PlayerBoneInFOV (aim_global.cpp). Cheapest mask that still answers the
		// only question asked here: the melee path reads m_vOrigin / m_vMins / m_vMaxs off the
		// record and never touches m_aBones, so this call is purely the "are this model's bones
		// available" gate.
		if (!tTarget.m_pEntity->SetupBones(m_tLiveRecord.m_aBones, MAXSTUDIOBONES, BONE_USED_BY_HITBOX, tTarget.m_pEntity->m_flSimulationTime()))
			return false;

		vRecords = { &m_tLiveRecord };
		for (auto& tRecord : vSimRecords)
			vRecords.push_back(&tRecord);
	}

	CGameTrace trace = {};
	CTraceFilterHitscan filter = {};
	filter.pSkip = pLocal;
	for (auto pRecord : vRecords)
	{
		Vec3 vRestoreOrigin = tTarget.m_pEntity->GetAbsOrigin();
		Vec3 vRestoreMins = tTarget.m_pEntity->m_vecMins();
		Vec3 vRestoreMaxs = tTarget.m_pEntity->m_vecMaxs();

		// Only a record that really sits somewhere else (a swing-prediction / doubletap sim tick)
		// is worth moving him for -- moving a player onto the position he is already at dirties
		// his abs transform and churns his bbox behind the renderer's back for nothing.
		const bool bMoveEntity = pRecord->m_vOrigin.DistToSqr(vRestoreOrigin) > 0.0001f
			|| pRecord->m_vMins != vRestoreMins || pRecord->m_vMaxs != vRestoreMaxs;
		if (bMoveEntity)
		{
			tTarget.m_pEntity->SetAbsOrigin(pRecord->m_vOrigin);
			tTarget.m_pEntity->m_vecMins() = pRecord->m_vMins + 0.125f; // account for origin compression
			tTarget.m_pEntity->m_vecMaxs() = pRecord->m_vMaxs - 0.125f;
		}

		const Vec3 vBoxLo = pRecord->m_vOrigin + pRecord->m_vMins;
		const Vec3 vBoxHi = pRecord->m_vOrigin + pRecord->m_vMaxs;
		const float flClampZ = std::clamp(m_vEyePos.z, vBoxLo.z, vBoxHi.z);
		const Vec3 vNearest = { std::clamp(m_vEyePos.x, vBoxLo.x, vBoxHi.x),
								std::clamp(m_vEyePos.y, vBoxLo.y, vBoxHi.y), flClampZ };

		bool bReturn = false;
		Vec3 vForward;
		{
			tTarget.m_vPos = vNearest;
			Aim(G::CurrentUserCmd->viewangles, Math::CalcAngle(m_vEyePos, tTarget.m_vPos), tTarget.m_vAngleTo, iAimType);

			Math::AngleVectors(tTarget.m_vAngleTo, &vForward);
			Vec3 vTraceEnd = m_vEyePos + (vForward * flRange);

			// CTFWeaponBaseMelee::DoSwingTraceInternal (melee:496-538): a no-hull ray first, and
			// the 18u hull ONLY when that ray missed everything (fraction >= 1.0, melee:509).
			SDK::TraceHull(m_vEyePos, vTraceEnd, {}, {}, MASK_SOLID, &filter, &trace);
			bReturn = trace.fraction < 1.f && trace.m_pEnt == tTarget.m_pEntity;
			if (!bReturn && trace.fraction >= 1.f)
			{
				SDK::TraceHull(m_vEyePos, vTraceEnd, vSwingMins, vSwingMaxs, MASK_SOLID, &filter, &trace);
				bReturn = trace.fraction < 1.f && trace.m_pEnt == tTarget.m_pEntity;
			}
		}

		if (bMoveEntity)   // restoring a player we never moved would dirty him for nothing
		{
			tTarget.m_pEntity->SetAbsOrigin(vRestoreOrigin);
			tTarget.m_pEntity->m_vecMins() = vRestoreMins;
			tTarget.m_pEntity->m_vecMaxs() = vRestoreMaxs;
		}

		if (bReturn)
		{
			tTarget.m_pRecord = pRecord;
			tTarget.m_bBacktrack = pRecord != &m_tLiveRecord && tTarget.m_iTargetType == TargetEnum::Player;
			return true;
		}
		// Legit/smooth view-assist fallback: not a valid swing, but if the hull would still touch
		// the target keep the aim tracking it (return 2 = aim, don't swing).
		else switch (iAimType)
		{
		case Vars::Aimbot::General::AimTypeEnum::Smooth:
		case Vars::Aimbot::General::AimTypeEnum::Assistive:
		{
			auto vAngle = Math::CalcAngle(m_vEyePos, tTarget.m_vPos);

			Math::AngleVectors(vAngle, &vForward);
			Vec3 vTraceEnd = m_vEyePos + (vForward * flRange);

			SDK::TraceHull(m_vEyePos, vTraceEnd, vSwingMins, vSwingMaxs, MASK_SOLID, &filter, &trace);
			if (trace.m_pEnt && trace.m_pEnt == tTarget.m_pEntity)
				return 2;
		}
		}
	}

	return false;
}



bool CAimbotMelee::Aim(Vec3 vCurAngle, Vec3 vToAngle, Vec3& vOut, int iMethod)
{
	/*
	if (Vec3* pDoubletapAngle = F::Ticks.GetShootAngle())
	{
		vOut = *pDoubletapAngle;
		return true;
	}
	*/

	bool bReturn = false;
	switch (iMethod)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
	case Vars::Aimbot::General::AimTypeEnum::Silent:
	case Vars::Aimbot::General::AimTypeEnum::Locking:
		vOut = vToAngle;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
		// Legit backstab forces 0% assist => the angle never moves toward the target.
		vOut = vCurAngle.LerpAngle(vToAngle, (m_bBackstabLegit ? 0.f : Vars::Aimbot::General::AssistStrength.Value) / 100.f);
		bReturn = true;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Assistive:
		Vec3 vMouseDelta = G::CurrentUserCmd->viewangles.DeltaAngle(G::LastUserCmd->viewangles);
		Vec3 vTargetDelta = vToAngle.DeltaAngle(G::LastUserCmd->viewangles);
		float flMouseDelta = vMouseDelta.Length2D(), flTargetDelta = vTargetDelta.Length2D();
		vTargetDelta = vTargetDelta.Normalized() * std::min(flMouseDelta, flTargetDelta);
		vOut = vCurAngle - vMouseDelta + vMouseDelta.LerpAngle(vTargetDelta, Vars::Aimbot::General::AssistStrength.Value / 100.f);
		bReturn = true;
		break;
	}

	Math::ClampAngles(vOut);
	return bReturn;
}

// assume angle calculated outside with other overload
void CAimbotMelee::Aim(CUserCmd* pCmd, Vec3& vAngle, int iMethod)
{
	bool bUnsure = F::Ticks.IsTimingUnsure();
	switch (iMethod)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
		if (G::Attacking != 1 && !bUnsure)
			break;
		[[fallthrough]];
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
	case Vars::Aimbot::General::AimTypeEnum::Assistive:
		if (m_bBackstabLegit)
			break; // Legit backstab: 0% assist => never moves the view, only auto-swings.
		pCmd->viewangles = vAngle;
		I::EngineClient->SetViewAngles(vAngle);
		break;
	case Vars::Aimbot::General::AimTypeEnum::Silent:
		if (G::Attacking == 1 || bUnsure)
		{
			SDK::FixMovement(pCmd, vAngle);
			pCmd->viewangles = vAngle;
			G::PSilentAngles = true;
		}
		break;
	case Vars::Aimbot::General::AimTypeEnum::Locking:
		SDK::FixMovement(pCmd, vAngle);
		pCmd->viewangles = vAngle;
		G::SilentAngles = true;
	}
}

static inline void DrawVisuals(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, Target_t& tTarget, std::unordered_map<int, std::vector<Vec3>>& mPaths)
{
	bool bPath = Vars::Visuals::Simulation::SwingLines.Value && Vars::Visuals::Simulation::PlayerPath.Value;
	bool bLine = false;  // on-shot overlays off: dih draws its own
	bool bBoxes = false;
	if (bPath || bLine || bBoxes)
	{
		if (pCmd->buttons & IN_ATTACK && G::CanPrimaryAttack && pWeapon->m_flSmackTime() < 0.f)
		{
			G::LineStorage.clear();
			G::BoxStorage.clear();
			G::PathStorage.clear();

			if (bPath)
			{
				if (Vars::Colors::PlayerPathIgnoreZ.Value.a)
				{
					G::PathStorage.emplace_back(mPaths[I::EngineClient->GetLocalPlayer()], I::GlobalVars->curtime + Vars::Visuals::Simulation::DrawDuration.Value, Vars::Colors::PlayerPathIgnoreZ.Value, Vars::Visuals::Simulation::PlayerPath.Value);
					G::PathStorage.emplace_back(mPaths[tTarget.m_pEntity->entindex()], I::GlobalVars->curtime + Vars::Visuals::Simulation::DrawDuration.Value, Vars::Colors::PlayerPathIgnoreZ.Value, Vars::Visuals::Simulation::PlayerPath.Value);
				}
				if (Vars::Colors::PlayerPath.Value.a)
				{
					G::PathStorage.emplace_back(mPaths[I::EngineClient->GetLocalPlayer()], I::GlobalVars->curtime + Vars::Visuals::Simulation::DrawDuration.Value, Vars::Colors::PlayerPath.Value, Vars::Visuals::Simulation::PlayerPath.Value, true);
					G::PathStorage.emplace_back(mPaths[tTarget.m_pEntity->entindex()], I::GlobalVars->curtime + Vars::Visuals::Simulation::DrawDuration.Value, Vars::Colors::PlayerPath.Value, Vars::Visuals::Simulation::PlayerPath.Value, true);
				}
			}
		}
		if (G::Attacking == 1)
		{
			if (bLine)
			{
				Vec3 vEyePos = pLocal->GetShootPos();
				float flDist = vEyePos.DistTo(tTarget.m_vPos);
				Vec3 vForward; Math::AngleVectors(tTarget.m_vAngleTo, &vForward);

				if (Vars::Colors::LineIgnoreZ.Value.a)
					G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vEyePos, vEyePos + vForward * flDist), I::GlobalVars->curtime + Vars::Visuals::Line::DrawDuration.Value, Vars::Colors::LineIgnoreZ.Value);
				if (Vars::Colors::Line.Value.a)
					G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vEyePos, vEyePos + vForward * flDist), I::GlobalVars->curtime + Vars::Visuals::Line::DrawDuration.Value, Vars::Colors::Line.Value, true);
			}
		}
	}
}

// =============================================================================================
// AUTO BACKSTAB, THE WHOLE COMMAND -- dih-bs15
// =============================================================================================
void CAimbotMelee::RunBackstab(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	static bool s_bStamped = false; // tells which knife build wrote a log (bs14 tier=, bs15 p=, bs16 p= pb= pg=)
	if (!s_bStamped) { s_bStamped = true; DebugLog("BACKSTAB BUILD = dih-bs17 (bs16 + thin-reach hold). Thin hold UNVERIFIED IN GAME."); }
	m_bBackstabLegit = Vars::Aimbot::Melee::BackstabAimType.Value == Vars::Aimbot::Melee::BackstabAimTypeEnum::Legit;
	const int iAimType = GetAimType(pWeapon);
	// Wall clock for everything that has to line up with player_hurt: inside engine prediction
	// GlobalVars->tickcount is our m_nTickBase, which is not the clock events arrive on.
	const float flNow = I::GlobalVars->realtime;

	// DID THE LAST PRESS SWING, AND DID THE SWING LAND? A real swing puts m_flNextPrimaryAttack
	// ~0.8s out on our predicted weapon within a few ticks; a swing that lands brings player_hurt
	// back within a round trip (Event clears both). Neither failure is a reach or odds question,
	// so each gets its own line.
	if (m_flPendTime >= 0.f)
	{
		const float flAge = flNow - m_flPendTime;
		const float flNext = pWeapon->m_flNextPrimaryAttack();
		if (flAge < 0.f || flAge > 0.6f)
			m_flPendTime = -1.f;
		else if (flNext > m_flPendNext + 0.4f)
		{
			m_flPendTime = -1.f;
			m_flWhiffTime = flNow;
		}
		else if (flAge >= 0.12f)
		{
			m_flPendTime = -1.f;
			if (++m_iPressLost <= 8)
				DebugLog("BS: PRESS DID NOT SWING -- IN_ATTACK went out %.0f ms ago and m_flNextPrimaryAttack "
					"never moved (%.3f -> %.3f). The command lost the button or CTFPlayer::CanAttack refused "
					"it; not a reach or odds miss. lost=%d", flAge * 1000.f, m_flPendNext, flNext, m_iPressLost);
		}
	}
	if (m_flWhiffTime >= 0.f && flNow - m_flWhiffTime > AimLatency() + 1.5f)
		m_flWhiffTime = -1.f;   // the knife was holstered through the window: no verdict either way
	else if (m_flWhiffTime >= 0.f && flNow - m_flWhiffTime > AimLatency() + 0.5f)
	{
		m_flWhiffTime = -1.f;
		++m_iWhiffs;
		DebugLog("BS: WHIFF -- the swing went out and no player_hurt came back. idx=%d p=%.2f slack=%.1f "
			"pad=%.2f ext=%.3f spd=%.0f d=%.1f | whiffs=%d", m_tLastFire.m_iIndex, m_tLastFire.m_flP,
			m_tLastFire.m_flSlack, m_tLastFire.m_flPad, m_tLastFire.m_flExtrap, m_tLastFire.m_flSpeed,
			m_tLastFire.m_flDist, m_iWhiffs);
	}

	// no swing is possible this tick (CanPrimaryAttack on our m_nTickBase, knife:180's own test). A
	// press now would be held and fire the instant the cooldown ends, on a tick nothing judged.
	if (!G::CanPrimaryAttack || pWeapon->m_flSmackTime() >= 0.f)
	{
		++m_aGateHits[BSG_COOLDOWN];
		return;
	}

	// ---- this command's context, shared by every target -------------------------------------------
	m_iDoubletapTicks = F::Ticks.GetTicks(pWeapon);
	// Post-move eye: the engine prediction window is open around the aimbot (hooks.cpp:669), and the
	// server swings from Weapon_ShootPosition after ProcessMovement. Silent aim does not change it --
	// FixMovement keeps the world-space wish direction, so there is nothing to re-predict per angle.
	m_vKnifeEye = pLocal->GetShootPos();
	m_vKnifeView = pCmd->viewangles;
	m_flTDue = BsDueTime(pCmd);
	const float flLerp = BsServerLerp();
	m_flTRewind = TICKS_TO_TIME(pCmd->tick_count - BsMath::TimeToTicks(flLerp, TICK_INTERVAL));

	// the server's swing range and hull (melee:398-420)
	m_flKnifeRange = SDK::AttribHookValue(float(pWeapon->GetSwingRange()), "melee_range_multiplier", pWeapon);
	m_flKnifeHull = SDK::AttribHookValue(18.f, "melee_bounds_multiplier", pWeapon);
	if (pLocal->m_flModelScale() > 1.f)
	{	// scaled UP only -- small players keep the full range
		m_flKnifeRange *= pLocal->m_flModelScale();
		m_flKnifeHull *= pLocal->m_flModelScale();
	}

	// EVERY player's box where lag compensation will put him, both teams: with a melee weapon out
	// teammates are rewound too (tf_player.cpp:20105-20111) and the swing stops on them. Gathered on
	// the REWOUND box, not the raw origin -- a fleeing target's raw origin leads the box the swing
	// actually meets by lerp + his tickbase lead.
	m_iBoxes = 0;
	{
		const float flGather = m_flKnifeRange + m_flKnifeHull * 1.7320508f + 8.f;
		const BsMath::V3 vEye = ToBs(m_vKnifeEye);
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
		{
			const int i = pEntity->entindex();
			auto pPlayer = pEntity->As<CTFPlayer>();
			if (m_iBoxes >= k_boxMax || pEntity == pLocal || i < 1 || i >= k_maxPlayers
				|| pEntity->IsDormant() || !pPlayer->IsAlive()
				|| pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE))   // SOLID_NONE: neither target nor blocker
				continue;
			BsMath::V3 o, mn, mx;
			float flExtrap = 0.f;
			if (!BsMath::Rewind(g_aTrack[i], m_flTRewind, k_maxExtrap, o, mn, mx, flExtrap, m_flTDue))
			{	// no record yet: the raw snapshot is the best there is (only ever a blocker; JudgeKnife
				// refuses to stab a player with no track)
				o = ToBs(pEntity->m_vecOrigin()); mn = ToBs(pEntity->m_vecMins()); mx = ToBs(pEntity->m_vecMaxs());
			}
			const BsMath::V3 lo = o + mn, hi = o + mx;
			const BsMath::V3 d = BsMath::V3{ std::clamp(vEye.x, lo.x, hi.x), std::clamp(vEye.y, lo.y, hi.y),
				std::clamp(vEye.z, lo.z, hi.z) } - vEye;
			if (sqrtf(d.x * d.x + d.y * d.y + d.z * d.z) > flGather)
				continue;
			m_aBoxes[m_iBoxes] = { lo, hi, i };
			m_aBoxEnt[m_iBoxes] = pEntity;
			m_aBoxTeam[m_iBoxes] = pEntity->m_iTeamNum();
			{	// 3D: the extrapolation moves him in z too, and the reach test is a 3D box
				const BsMath::V3 v = BsMath::Velocity(g_aTrack[i]);
				m_aBoxSpeed[m_iBoxes] = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
			}
			m_aBoxExtrap[m_iBoxes] = flExtrap;
			++m_iBoxes;
		}
	}

	// the targets: every enemy box the knife's own ignore list allows, nearest first. No cap -- with
	// enemies stacked the nearest is often the one you cannot stab. Never a teammate, friendly fire
	// or not: the backstab path requires an enemy (knife:203).
	int aTargets[k_boxMax], nTargets = 0;
	{
		for (int b = 0; b < m_iBoxes; b++)
		{
			if (m_aBoxTeam[b] == pLocal->m_iTeamNum())
				continue;
			if (F::AimbotGlobal.ShouldIgnore(m_aBoxEnt[b], pLocal, pWeapon, Vars::Aimbot::Melee::BackstabIgnore.Value))
				continue;
			aTargets[nTargets++] = b;
		}
		const BsMath::V3 vEye = ToBs(m_vKnifeEye);
		std::sort(aTargets, aTargets + nTargets, [&](int a, int b)
		{
			const BsMath::V3 ca = (m_aBoxes[a].lo + m_aBoxes[a].hi) * 0.5f - vEye, cb = (m_aBoxes[b].lo + m_aBoxes[b].hi) * 0.5f - vEye;
			return ca.x * ca.x + ca.y * ca.y + ca.z * ca.z < cb.x * cb.x + cb.y * cb.y + cb.z * cb.z;
		});
	}
	if (!nTargets)
	{
		++m_aGateHits[BSG_NOTARGET];
		return;
	}

	BsJudge tBest = {};
	bool bFired = false;
	for (int t = 0; t < nTargets; t++)
	{
		BsJudge tJudge = {};
		if (JudgeKnife(aTargets[t], pLocal, pWeapon, tJudge))
		{
			tBest = tJudge;
			bFired = true;
			break;
		}
		// the line worth logging is the one that got furthest down the pipeline
		if (tJudge.m_iGate > tBest.m_iGate || tJudge.m_iGate == tBest.m_iGate && tJudge.m_flP > tBest.m_flP)
			tBest = tJudge;
	}
	++m_aGateHits[std::clamp(tBest.m_iGate, 0, int(BSG_COUNT) - 1)];

	if (bFired)
	{
		G::AimTarget = { tBest.m_iIndex, I::GlobalVars->tickcount };

		// the knife has no smack delay (knife:229-231): it always presses now, doubletap or not
		pCmd->buttons |= IN_ATTACK;
		if (m_iDoubletapTicks)
			F::Ticks.m_bDoubletap = true;

		++m_iFires;
		m_tLastFire = { flNow, tBest.m_iIndex, tBest.m_flP, tBest.m_flSigma, tBest.m_flMargin,
			tBest.m_flSlack, tBest.m_flPad, tBest.m_flHorizon, tBest.m_flExtrap, tBest.m_flDist, tBest.m_flSpeed, tBest.m_iK,
			tBest.m_flPBest, tBest.m_flPGuard, tBest.m_flRate, tBest.m_flGuardMargin, tBest.m_flSigCore };
		m_iQuietTick = I::GlobalVars->tickcount;
		m_flPendTime = flNow;
		m_flPendNext = pWeapon->m_flNextPrimaryAttack();
		m_flWhiffTime = -1.f;
		DebugLog("BS: STAB idx=%d p=%.3f pb=%.3f pg=%.3f gm=%.1f sc=%.1f next=%.3f margin=%.1f fac=%.1f sig=%.1f bear=%.2f k=%d rate=%.0f "
			"hor=%.3f lerp=%.3f ext=%.3f slack=%.1f pad=%.2f thinpad=%.2f rnext=%d held=%d d=%.1f spd=%.0f my=%.0f rel=%.0f legit=%d n=%d",
			tBest.m_iIndex, tBest.m_flP, tBest.m_flPBest, tBest.m_flPGuard, tBest.m_flGuardMargin, tBest.m_flSigCore,
			tBest.m_flPNext, tBest.m_flMargin, tBest.m_flFacing, tBest.m_flSigma,
			tBest.m_flSigBear, tBest.m_iK, tBest.m_flRate, tBest.m_flHorizon, flLerp, tBest.m_flExtrap,
			tBest.m_flSlack, tBest.m_flPad, BsMath::ThinPad(tBest.m_flPad, tBest.m_flRel), int(tBest.m_bReachNext),
			tBest.m_iHeld, tBest.m_flDist, tBest.m_flSpeed, pLocal->m_vecVelocity().Length2D(), tBest.m_flRel,
			int(m_bBackstabLegit), nTargets);

		G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
		Vec3 vAim = tBest.m_vAim;
		Aim(pCmd, vAim, iAimType);   // Silent writes it; Legit returns without touching the view
		return;
	}

	// ONE LINE PER HALF SECOND while somebody is inside the knife's reach and nothing fires. Every
	// number is from THIS command's furthest judgement.
	//   why=  the gate that refused      p= odds now   next= odds a tick later   margin= degrees
	//   inside the yaw window at the estimate   sig= his yaw sigma (measured)   k= estimator
	//   (0 hold, 1 half rate, 2 full rate)   hor= how far ahead his yaw is read   ext= position
	//   guessed past his records   slack/pad= reach
	if (I::GlobalVars->tickcount - m_iQuietTick >= 33 || I::GlobalVars->tickcount < m_iQuietTick)
	{
		m_iQuietTick = I::GlobalVars->tickcount;
		DebugLog("BS: miss why=%s idx=%d p=%.3f pb=%.3f pg=%.3f gm=%.1f sc=%.1f next=%.3f margin=%.1f fac=%.1f sig=%.1f bear=%.2f k=%d "
			"rate=%.0f hor=%.3f lerp=%.3f ext=%.3f slack=%.1f pad=%.2f held=%d d=%.1f spd=%.0f legit=%d "
			"fovcap=%.0f | us=%.0f/%.0f | gates cd=%d nt=%d ntr=%d rng=%d fov=%d blk=%d miss=%d ign=%d "
			"rch=%d fac=%d odds=%d turn=%d hold=%d thin=%d | fires=%d stabs=%d front=%d whiff=%d",
			BsGateName(tBest.m_iGate), tBest.m_iIndex, tBest.m_flP, tBest.m_flPBest, tBest.m_flPGuard, tBest.m_flGuardMargin,
			tBest.m_flSigCore, tBest.m_flPNext, tBest.m_flMargin,
			tBest.m_flFacing, tBest.m_flSigma, tBest.m_flSigBear, tBest.m_iK, tBest.m_flRate, tBest.m_flHorizon,
			flLerp, tBest.m_flExtrap, tBest.m_flSlack, tBest.m_flPad, tBest.m_iHeld, tBest.m_flDist,
			tBest.m_flSpeed, int(m_bBackstabLegit), GetFOV(pWeapon),
			m_iCostN ? m_flCostSum / m_iCostN : 0.0, m_flCostMax,
			m_aGateHits[BSG_COOLDOWN], m_aGateHits[BSG_NOTARGET], m_aGateHits[BSG_NOTRACK],
			m_aGateHits[BSG_RANGE], m_aGateHits[BSG_FOV], m_aGateHits[BSG_BLOCKED], m_aGateHits[BSG_TRACEMISS],
			m_aGateHits[BSG_IGNORED], m_aGateHits[BSG_REACH], m_aGateHits[BSG_FACING], m_aGateHits[BSG_ODDS],
			m_aGateHits[BSG_TURN], m_aGateHits[BSG_HOLD], m_aGateHits[BSG_THIN], m_iFires, m_iStabs, m_iFrontStabs, m_iWhiffs);
		memset(m_aGateHits, 0, sizeof(m_aGateHits));
		m_flCostSum = m_flCostMax = 0.0;
		m_iCostN = 0;
	}
}

void CAimbotMelee::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	// Auto backstab is a feature row of its own: it runs with the master aimbot off, the aim key
	// not held and melee aim off, on its own aim type (see CAimbot::RunMain).
	if (IsKnifeBackstab(pWeapon))
	{
		const auto tStart = std::chrono::high_resolution_clock::now();
		RunBackstab(pLocal, pWeapon, pCmd);
		const double flUs = std::chrono::duration<double, std::micro>(
			std::chrono::high_resolution_clock::now() - tStart).count();
		m_flCostSum += flUs;
		m_flCostMax = std::max(m_flCostMax, flUs);
		++m_iCostN;
		return;
	}
	m_bBackstabLegit = false;

	// melee aim off, or the master aimbot / aim key off -> nothing else in here may act.
	if (!Vars::Aimbot::Melee::Enabled.Value || F::Aimbot.m_bBackstabOnly)
		return;

	{	// general aim type restore (keybind toggled off mid-smack)
		static int iStaticAimType = Vars::Aimbot::General::AimType.Value;
		const int iLastAimType = iStaticAimType;
		const int iRealAimType = Vars::Aimbot::General::AimType.Value;

		if (pWeapon->m_flSmackTime() > 0.f && !iRealAimType && iLastAimType)
			Vars::Aimbot::General::AimType.Value = iLastAimType;
		iStaticAimType = Vars::Aimbot::General::AimType.Value;
	}

	const int iAimType = GetAimType(pWeapon);
	if (!iAimType
		|| !F::AimbotGlobal.ShouldAim() && pWeapon->m_flSmackTime() < 0.f)
		return;

	if (RunSapper(pLocal, pWeapon, pCmd))
		return;

	auto vTargets = SortTargets(pLocal, pWeapon);
	if (vTargets.empty())
		return;

	UpdateInfo(pLocal, pWeapon, pCmd, vTargets);
	// Track-only result (legit hull-touch) on the nearest target must not shadow a real hit on a
	// further one, so the loop finishes first and the tracking runs only if nothing connected.
	Target_t* pAimOnly = nullptr;
	for (auto& tTarget : vTargets)
	{
		const auto iResult = CanHit(tTarget, pLocal, pWeapon);
		if (!iResult) continue;
		if (iResult == 2)
		{
			if (!pAimOnly)
				pAimOnly = &tTarget;
			continue;
		}

		G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
		G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };

		if (Vars::Aimbot::General::AutoShoot.Value && pWeapon->m_flSmackTime() < 0.f)
		{
			if (m_bShouldSwing)
			{
				// Only on the tick the swing actually fires -- see aim_hitscan.cpp's Run() for
				// the same write and why (player_lagcompensation.cpp:402).
				if (tTarget.m_bBacktrack && tTarget.m_pRecord)
					pCmd->tick_count = TIME_TO_TICKS(tTarget.m_pRecord->m_flSimTime + BacktrackLerp());
				pCmd->buttons |= IN_ATTACK;
			}
			if (m_iDoubletapTicks)
				F::Ticks.m_bDoubletap = true;
		}

		G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
		if (G::Attacking != 1)
		{
			m_vEyePos = pLocal->GetShootPos();
			Aim(G::CurrentUserCmd->viewangles, Math::CalcAngle(m_vEyePos, tTarget.m_vPos), tTarget.m_vAngleTo, iAimType);
		}
		DrawVisuals(pLocal, pWeapon, pCmd, tTarget, m_mPaths);

		Aim(pCmd, tTarget.m_vAngleTo, iAimType);
		return;
	}

	if (pAimOnly)
	{
		G::AimTarget = { pAimOnly->m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };
		Aim(pCmd, pAimOnly->m_vAngleTo, iAimType);
	}
}

// PROMISE vs RESULT. A fire says "p of these swings are backstabs"; player_hurt says what this one
// was (custom 2 = TF_DMG_CUSTOM_BACKSTAB). Reading the log: bucket fires by p= -- the stab rate in
// each bucket should sit near its p. Front stabs at p >= 0.9 mean sigma is too small (check k= and
// hor=); stabs at p ~0.6 landing far above 60% mean the odds are too cautious. A WHIFF line is reach.
void CAimbotMelee::Event(IGameEvent* pEvent, uint32_t uHash, CTFPlayer* pLocal)
{
	if (uHash != FNV1A::Hash32Const("player_hurt") || !pLocal || !Vars::Aimbot::Melee::AutoBackstab.Value)
		return;

	const int iAttacker = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("attacker"));
	if (iAttacker != I::EngineClient->GetLocalPlayer())
		return;

	// wall clock: events do not run on the tickbase clock the fire was judged on
	const float flSince = I::GlobalVars->realtime - m_tLastFire.m_flTime;
	if (flSince < 0.f || flSince > 1.f)
		return;

	const int iVictim = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid"));
	if (iVictim != m_tLastFire.m_iIndex)
	{	// somebody else's damage is not this swing's stab result -- but if it came in the swing's own
		// window it may well be the swing landing on him, so it is no WHIFF either
		m_flPendTime = m_flWhiffTime = -1.f;
		return;
	}
	const int iCustom = pEvent->GetInt("custom");
	const bool bBackstab = iCustom == 2; // TF_DMG_CUSTOM_BACKSTAB, tf_shareddefs.h
	if (bBackstab)
		++m_iStabs;
	else
		++m_iFrontStabs;
	DebugLog("BS: outcome idx=%d(fired %d) dmg=%d custom=%d %s | p=%.3f pb=%.3f pg=%.3f gm=%.1f sc=%.1f rate=%.0f sig=%.1f margin=%.1f k=%d hor=%.3f "
		"slack=%.1f pad=%.2f ext=%.3f d=%.1f spd=%.0f dt=%.0fms | %d/%d",
		iVictim, m_tLastFire.m_iIndex, pEvent->GetInt("damageamount"), iCustom,
		bBackstab ? "BACKSTAB" : "NOT A BACKSTAB", m_tLastFire.m_flP, m_tLastFire.m_flPBest, m_tLastFire.m_flPGuard,
		m_tLastFire.m_flGuardMargin, m_tLastFire.m_flSigCore,
		m_tLastFire.m_flRate, m_tLastFire.m_flSigma,
		m_tLastFire.m_flMargin, m_tLastFire.m_iK, m_tLastFire.m_flHorizon, m_tLastFire.m_flSlack,
		m_tLastFire.m_flPad, m_tLastFire.m_flExtrap, m_tLastFire.m_flDist, m_tLastFire.m_flSpeed,
		flSince * 1000.f, m_iStabs, m_iStabs + m_iFrontStabs);
	m_tLastFire.m_flTime = -1000.f;       // one outcome per fire
	m_flPendTime = m_flWhiffTime = -1.f;  // it swung and it landed
}

static inline int GetAttachment(CBaseObject* pBuilding, int i)
{
	int iAttachment = pBuilding->GetBuildPointAttachmentIndex(i);
	if (pBuilding->IsSentrygun() && pBuilding->m_iUpgradeLevel() > 1)
		iAttachment = 3; // idk why this is needed
	return iAttachment;
}
bool CAimbotMelee::FindNearestBuildPoint(CBaseObject* pBuilding, CTFPlayer* pLocal, Vec3& vPoint)
{
	bool bFoundPoint = false;

	m_vEyePos = pLocal->GetShootPos();
	static auto tf_obj_max_attach_dist = H::ConVars.FindVar("tf_obj_max_attach_dist");
	float flNearestPoint = tf_obj_max_attach_dist->GetFloat();

	for (int i = 0; i < pBuilding->GetNumBuildPoints(); i++)
	{
		Vector vOrigin;
		if (pBuilding->GetAttachment(GetAttachment(pBuilding, i), vOrigin))
		{
			if (!SDK::VisPos(pLocal, pBuilding, m_vEyePos, vOrigin))
				continue;

			float flDist = (vOrigin - pLocal->m_vecOrigin()).Length();
			if (flDist < flNearestPoint)
			{
				flNearestPoint = flDist;
				vPoint = vOrigin;
				bFoundPoint = true;
			}
		}
	}

	return bFoundPoint;
}

bool CAimbotMelee::RunSapper(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (pWeapon->GetWeaponID() != TF_WEAPON_BUILDER)
		return false;

	const Vec3 vLocalPos = F::Ticks.GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

	std::vector<Target_t> vTargets;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
	{
		auto pBuilding = pEntity->As<CBaseObject>();
		if (pBuilding->m_bHasSapper() || !pBuilding->IsInValidTeam())
			continue;

		Vec3 vPoint;
		if (!FindNearestBuildPoint(pBuilding, pLocal, vPoint))
			continue;

		Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPoint);
		const float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
		const float flDistTo = vLocalPos.DistTo(vPoint);

		if (flFOVTo > Vars::Aimbot::General::AimFOV.Value)
			continue;

		vTargets.emplace_back(pBuilding, TargetEnum::Unknown, vPoint, vAngleTo, flFOVTo, flDistTo);
	}
	F::AimbotGlobal.SortTargets(vTargets, Vars::Aimbot::General::TargetSelectionEnum::Distance);
	if (vTargets.empty())
		return true;

	auto& tTarget = vTargets.front();

	bool bShouldAim = true;
	if (Vars::Aimbot::General::AutoShoot.Value)
		pCmd->buttons |= IN_ATTACK;
	else
		bShouldAim = pCmd->buttons & IN_ATTACK;
	if (Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Silent)
		bShouldAim = bShouldAim && (!I::ClientState->chokedcommands || !F::Ticks.CanChoke());
		
	if (bShouldAim)
	{
		G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
		G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };

		G::Attacking = true;

		Aim(pCmd->viewangles, Math::CalcAngle(m_vEyePos, tTarget.m_vPos), tTarget.m_vAngleTo);
		tTarget.m_vAngleTo.x = pCmd->viewangles.x; // we don't need to care about pitch
		Aim(pCmd, tTarget.m_vAngleTo);
	}

	return true;
}