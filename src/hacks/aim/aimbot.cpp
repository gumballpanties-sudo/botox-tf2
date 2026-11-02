#include "aimbot.h"

#include "aim_hitscan.h"
#include "aim_projectile.h"
#include "aim_melee.h"
#include "autodetonate.h"
#include "autoairblast.h"
#include "autorocketjump.h"

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
	// keys the game does not bind. Same source dih's own BindKey widget captures from.
	const bool bDown = (GetAsyncKeyState(iKey) & 0x8000) != 0;

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

	const bool bDown = (GetAsyncKeyState(iKey) & 0x8000) != 0;

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
	if (!ShouldRun(pLocal, pWeapon))
		return;

	F::AutoDetonate.Run(pLocal, pWeapon, pCmd);
	F::AutoAirblast.Run(pLocal, pWeapon, pCmd);

	// the master enable and the activation bind gate the AIM only. Auto detonate / airblast
	// above are their own toggles and keep running -- the key is "aim now", not "features off".
	// auto backstab is a feature row of its own -- it still runs with the master enable off,
	// the aim key not held, and melee aim off. m_bBackstabOnly marks that pass.
	const bool bAimOK = Vars::Aimbot::General::Enabled.Value && KeyOK();
	if (!bAimOK && !BackstabOn())
		return;
	m_bBackstabOnly = !bAimOK;

	RunAimbot(pLocal, pWeapon, pCmd);
	RunAimbot(pLocal, pWeapon, pCmd, true);

	m_bBackstabOnly = false;
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