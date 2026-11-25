#include "aimbot.h"
#include "charge_release.h"

#include "aim_hitscan.h"
#include "backtrack.h"
#include "aim_projectile.h"
#include "aim_melee.h"
#include "autodetonate.h"
#include "autoairblast.h"
#include "../movement/autorocketjump.h"

void AutoPogo_Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
bool Binds_Blocked();   // render.cpp: chat / console / menu text box -- a press there is typing
void DebugLog(const char* fmt, ...);   // dllmain.cpp

// activation bind -- the same three modes dih's own movement features
// use (MovementKeyOK). Mode 0 = always on, 1 = while the key is held, 2 = the key toggles it.
// Edge-detected here rather than in the menu so a toggle press is counted once per command.
bool CAimbot::KeyOK()
{
	const int iKey = Vars::Aimbot::General::AimKey.Value;
	const int iMode = Vars::Aimbot::General::AimKeyMode.Value;

	if (iMode != Vars::Aimbot::General::AimKeyModeEnum::OnHotkey
		&& iMode != Vars::Aimbot::General::AimKeyModeEnum::Toggle)
		return true;     // always on
	if (!iKey)
		return true;     // a mode with no key bound would be an off switch nobody can turn back on

	// GetAsyncKeyState, not the engine's button bits: this has to work for mouse buttons and for
	// keys the game does not bind. Same source dih's own BindKey widget captures from. Typed
	// keys read up, so neither hold nor the toggle latch below sees them.
	const bool bDown = !Binds_Blocked() && (GetAsyncKeyState(iKey) & 0x8000) != 0;

	if (iMode == Vars::Aimbot::General::AimKeyModeEnum::OnHotkey)
		return bDown;

	static bool s_bWasDown = false;
	if (bDown && !s_bWasDown)
		m_bToggled = !m_bToggled;
	s_bWasDown = bDown;
	return m_bToggled;
}

// Auto backstab's own gate: the master aim key never reached it (the feature is deliberately
// outside the aimbot's enable), so this is the only key that can turn it off mid-round. Same three
// modes, own toggle latch -- sharing one would make the aim key's toggle flip the knife too.
bool CAimbot::BackstabOn()
{
	if (!Vars::Aimbot::Melee::AutoBackstab.Value)
		return false;

	const int iKey = Vars::Aimbot::Melee::BackstabKey.Value;
	const int iMode = Vars::Aimbot::Melee::BackstabKeyMode.Value;

	if (iMode != Vars::Aimbot::Melee::BackstabKeyModeEnum::OnHotkey
		&& iMode != Vars::Aimbot::Melee::BackstabKeyModeEnum::Toggle)
		return true;
	if (!iKey)
		return true;

	const bool bDown = !Binds_Blocked() && (GetAsyncKeyState(iKey) & 0x8000) != 0;

	if (iMode == Vars::Aimbot::Melee::BackstabKeyModeEnum::OnHotkey)
		return bDown;

	static bool s_bWasDown = false;
	if (bDown && !s_bWasDown)
		m_bBackstabToggled = !m_bBackstabToggled;
	s_bWasDown = bDown;
	return m_bBackstabToggled;
}

bool CAimbot::ShouldRun(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	if (!pWeapon || !pLocal->CanAttack()
		|| !SDK::AttribHookValue(1, "mult_dmg", pWeapon)
		|| I::EngineVGui->IsGameUIVisible())
		return false;

	return true;
}

void CAimbot::RunAimbot(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, bool bSecondaryType)
{
	m_bRunningSecondary = bSecondaryType;
	EWeaponType eWeaponType = !m_bRunningSecondary ? G::PrimaryWeaponType : G::SecondaryWeaponType;

	bool bOriginal;
	if (m_bRunningSecondary)
		bOriginal = G::CanPrimaryAttack, G::CanPrimaryAttack = G::CanSecondaryAttack;

	// each weapon type has its own enable (the menu section's checkbox). Auto backstab is its
	// own row, so melee runs for it with Melee off -- and in a backstab-only pass nothing else runs.
	switch (eWeaponType)
	{
	case EWeaponType::HITSCAN:
		if (!m_bBackstabOnly && Vars::Aimbot::Hitscan::Enabled.Value) F::AimbotHitscan.Run(pLocal, pWeapon, pCmd);
		break;
	case EWeaponType::PROJECTILE:
		if (!m_bBackstabOnly && Vars::Aimbot::Projectile::Enabled.Value) F::AimbotProjectile.Run(pLocal, pWeapon, pCmd);
		break;
	case EWeaponType::MELEE:
		if (Vars::Aimbot::Melee::Enabled.Value || BackstabOn()) F::AimbotMelee.Run(pLocal, pWeapon, pCmd);
		break;
	}

	if (m_bRunningSecondary)
		G::CanPrimaryAttack = bOriginal;
}

void CAimbot::RunMain(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (F::AimbotProjectile.m_iLastTickCancel)
	{
		pCmd->weaponselect = F::AimbotProjectile.m_iLastTickCancel;
		F::AimbotProjectile.m_iLastTickCancel = 0;
	}

	m_bRan = false;
	if (abs(G::AimTarget.m_iTickCount - I::GlobalVars->tickcount) > G::AimTarget.m_iDuration)
		G::AimTarget = {};
	if (abs(G::AimPoint.m_iTickCount - I::GlobalVars->tickcount) > G::AimPoint.m_iDuration)
		G::AimPoint = {};

	if (pCmd->weaponselect)
		return;

	F::AutoRocketJump.Run(pLocal, pWeapon, pCmd);   // Amalgam RunMain slot; driven by the auto goomba
	AutoPogo_Run(pLocal, pWeapon, pCmd);            // autopogo.cpp, own bind
	if (!ShouldRun(pLocal, pWeapon))
		return;

	F::AutoDetonate.Run(pLocal, pWeapon, pCmd);
	F::AutoAirblast.Run(pLocal, pWeapon, pCmd);

	// the master enable and the activation bind gate the AIM only. Auto detonate / airblast
	// above are their own toggles and keep running -- the key is "aim now", not "features off".
	// auto backstab is a feature row of its own -- it still runs with the master enable off,
	// the aim key not held, and melee aim off. m_bBackstabOnly marks that pass.
	// hold key on mouse1: huntsman / sticky release cmd = key up cmd. charge_release.h
	const int nWeaponID = pWeapon->GetWeaponID();
	const bool bCharging = (nWeaponID == TF_WEAPON_COMPOUND_BOW || nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER)
		&& pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime() > 0.f;
	const bool bKey = KeyOK();
	m_bReleasePass = ChargeReleasePass(m_bChargeKey, bCharging, bKey, G::Attacking);   // G::Attacking from user cmd (hooks.cpp:636)

	const bool bAimOK = Vars::Aimbot::General::Enabled.Value && (bKey || m_bReleasePass);
	if (!bAimOK && !BackstabOn())
		return;
	m_bBackstabOnly = !bAimOK;

	RunAimbot(pLocal, pWeapon, pCmd);
	RunAimbot(pLocal, pWeapon, pCmd, true);

	m_bBackstabOnly = m_bReleasePass = false;
}

void CAimbot::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	Store(false);

	// Before every weapon gate below: the backstab's yaw/velocity history has to be sampled for
	// every live player on every command, or it is cold for the first ~12 snapshots after a
	// weapon switch -- exactly when the knife comes out.
	F::AimbotMelee.UpdateBackstabTracks();

	RunMain(pLocal, pWeapon, pCmd);

	G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
	F::Backtrack.RunBacktrack(pLocal, pWeapon, pCmd);   // botox run_backtrack: fire cmds the aim did not commit
	F::Backtrack.RunMeleeSmack(pLocal, pWeapon, pCmd);  // melee lag comp happens at smack, not at the press
	LedgerExpire();
	LedgerFire(pLocal, pWeapon, pCmd);   // after backtrack: bt= is the final tick_count
}

// ---- shot ledger. realtime on both ends: CreateMove curtime is tickbase, events run on frame time.
// AIMSHOT: FIRE/HIT/MISS + running hits/total per type. MISS lines carry dist / target speed /
// on-ground at fire time: misses clustering on fast or airborne targets = prediction, far = drop/time.
void CAimbot::LedgerLine(const char* szWhat, const Shot_t& tShot, int iDamage, int iCustom)
{
	DebugLog("AIMSHOT: %s wpn=%d idx=%d dmg=%d custom=%d t=%.2f dist=%.0f spd=%.0f gnd=%d hs=%d bt=%d chg=%.2f run=%d z=%.1f  %s %d/%d",
		szWhat, tShot.m_iWeapon, tShot.m_iIndex, iDamage, iCustom, I::GlobalVars->realtime - tShot.m_flFired, tShot.m_flDist, tShot.m_flSpeed,
		tShot.m_bGround, tShot.m_bHs, tShot.m_iBt, tShot.m_flCharge, tShot.m_iRun, tShot.m_flZ,
		tShot.m_bProj ? "proj" : "hitscan", m_aHits[tShot.m_bProj], m_aTotal[tShot.m_bProj]);
}

void CAimbot::LedgerFire(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!pWeapon || G::Attacking != 1 || !G::AimTarget.m_iEntIndex || !G::AimTarget.m_iDuration
		|| G::AimTarget.m_iTickCount != I::GlobalVars->tickcount)
		return;   // only shots the aim solved this cmd
	const bool bProj = G::PrimaryWeaponType == EWeaponType::PROJECTILE;
	if (!bProj && G::PrimaryWeaponType != EWeaponType::HITSCAN)
		return;   // melee: auto backstab has its own ledger
	auto pEntity = I::ClientEntityList->GetClientEntity(G::AimTarget.m_iEntIndex);
	if (!pEntity || !pEntity->As<CBaseEntity>()->IsPlayer())
		return;   // buildings send no player_hurt
	auto pTarget = pEntity->As<CTFPlayer>();

	const int iIndex = G::AimTarget.m_iEntIndex, iWeapon = pWeapon->GetWeaponID();
	auto pNet = I::EngineClient->GetNetChannelInfo();
	const float flPing = pNet ? pNet->GetLatency(FLOW_INCOMING) + pNet->GetLatency(FLOW_OUTGOING) : 0.f;
	const float flFlight = bProj ? std::min(F::AimbotProjectile.TimeTo(), 5.f) : 0.f;
	const float flDue = I::GlobalVars->realtime + flFlight + flPing + 0.5f;   // ponytail: flat 0.5 s slack, widen if HITs log as late MISS

	Shot_t* pFree = nullptr;
	for (auto& tShot : m_aShots)
	{
		if (!bProj && tShot.m_flDue && tShot.m_iIndex == iIndex && tShot.m_iWeapon == iWeapon)
		{
			tShot.m_flDue = flDue;   // hitscan stream (minigun, smg) on one target = one shot
			return;
		}
		if (!tShot.m_flDue && !pFree)
			pFree = &tShot;
	}
	if (!pFree)
		return;   // 8 in flight: drop, the ledger is a sample
	Shot_t tShot;
	tShot.m_iIndex = iIndex, tShot.m_iWeapon = iWeapon, tShot.m_flDue = flDue, tShot.m_flFired = I::GlobalVars->realtime;
	tShot.m_flDist = pLocal->GetShootPos().DistTo(pTarget->m_vecOrigin());
	tShot.m_flSpeed = pTarget->m_vecVelocity().Length2D(), tShot.m_bGround = pTarget->IsOnGround(), tShot.m_bProj = bProj;
	tShot.m_bHs = G::CanHeadshot;
	tShot.m_iBt = G::OriginalCmd.tick_count - pCmd->tick_count;
	if (iWeapon == TF_WEAPON_COMPOUND_BOW)
	{
		const float flBegin = pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime();
		tShot.m_flCharge = flBegin > 0.f ? I::GlobalVars->curtime - flBegin : 0.f;
		if (F::AimbotProjectile.m_iZoneIdx == iIndex)
			tShot.m_iRun = F::AimbotProjectile.m_iZoneRun, tShot.m_flZ = F::AimbotProjectile.m_flZoneZ;
	}
	*pFree = tShot;
	m_aTotal[bProj]++;
}

void CAimbot::LedgerExpire()
{
	for (auto& tShot : m_aShots)
	{
		if (!tShot.m_flDue || I::GlobalVars->realtime < tShot.m_flDue)
			continue;
		LedgerLine("MISS", tShot, 0, -1);
		tShot = {};
	}
}

void CAimbot::LedgerHurt(int iVictim, int iDamage, int iCustom)
{
	Shot_t* pOldest = nullptr;
	for (auto& tShot : m_aShots)
		if (tShot.m_flDue && tShot.m_iIndex == iVictim && (!pOldest || tShot.m_flFired < pOldest->m_flFired))
			pOldest = &tShot;
	if (!pOldest)
		return;   // not an aim shot (manual, afterburn after resolve, ...)
	m_aHits[pOldest->m_bProj]++;
	LedgerLine("HIT ", *pOldest, iDamage, iCustom);
	*pOldest = {};
}

void CAimbot::Store(CBaseEntity* pEntity, size_t iSize)
{
	if (!Vars::Visuals::Simulation::RealPath.Value)
		return;

	if (!pEntity->IsPlayer())
		return;

	if (auto pResource = H::Entities.GetResource())
	{
		m_tPath = { { pEntity->m_vecOrigin() }, I::GlobalVars->curtime + Vars::Visuals::Simulation::DrawDuration.Value, Color_t(), Vars::Visuals::Simulation::RealPath.Value };
		m_iSize = iSize;
		m_iPlayer = pResource->m_iUserID(pEntity->entindex());
	}
}

void CAimbot::Store(bool bFrameStageNotify)
{
	if (!Vars::Visuals::Simulation::RealPath.Value)
		return;

	int iLag = 1;
	if (bFrameStageNotify)
	{
		static int iStaticTickcout = I::GlobalVars->tickcount;
		iLag = I::GlobalVars->tickcount - iStaticTickcout;
		iStaticTickcout = I::GlobalVars->tickcount;
	}

	if (!m_tPath.m_flTime)
		return;
	else if (m_tPath.m_vPath.size() >= m_iSize || m_tPath.m_flTime < I::GlobalVars->curtime)
	{
		if (m_tPath.m_tColor = Vars::Colors::RealPath.Value, m_tPath.m_bZBuffer = true; m_tPath.m_tColor.a)
			G::PathStorage.push_back(m_tPath);
		if (m_tPath.m_tColor = Vars::Colors::RealPathIgnoreZ.Value, m_tPath.m_bZBuffer = false; m_tPath.m_tColor.a)
			G::PathStorage.push_back(m_tPath);
		m_tPath = {};
		return;
	}

	int iIndex = I::EngineClient->GetPlayerForUserID(m_iPlayer);
	if (bFrameStageNotify ? iIndex == I::EngineClient->GetLocalPlayer() : iIndex != I::EngineClient->GetLocalPlayer())
		return;

	auto pPlayer = I::ClientEntityList->GetClientEntity(iIndex)->As<CTFPlayer>();
	if (!pPlayer)
		return;

	for (int i = 0; i < iLag; i++)
		m_tPath.m_vPath.push_back(pPlayer->m_vecOrigin());
}