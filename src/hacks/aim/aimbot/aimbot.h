#pragma once
#include "../sdk/sdk.h"

class CAimbot
{
private:
	bool KeyOK();   // activation bind (always on / hold / toggle)
	bool ShouldRun(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	void RunMain(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void RunAimbot(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, bool bSecondaryType = false);

	size_t m_iSize = 0;
	int m_iPlayer = 0;

	// shot ledger (debug.log AIMSHOT lines): aim-solved fire = promise, our player_hurt = result.
	// The data accuracy tuning reads; without it every tweak is a guess.
	// hs = could headshot at fire (G::CanHeadshot), bt = ticks the cmd was rewound, chg = bow charge s,
	// run/z = huntsman head zone at release (-1 = not a zone shot)
	struct Shot_t { int m_iIndex = 0, m_iWeapon = 0, m_iBt = 0, m_iRun = -1; float m_flDue = 0.f, m_flFired = 0.f, m_flDist = 0.f, m_flSpeed = 0.f, m_flCharge = -1.f, m_flZ = 0.f; bool m_bGround = false, m_bProj = false, m_bHs = false; };
	Shot_t m_aShots[8] = {};
	int m_aHits[2] = {}, m_aTotal[2] = {};   // [0] hitscan, [1] projectile
	void LedgerFire(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void LedgerLine(const char* szWhat, const Shot_t& tShot, int iDamage, int iCustom);
	void LedgerExpire();

public:
	void LedgerHurt(int iVictim, int iDamage, int iCustom);   // aim/sdk/events.cpp player_hurt
	bool BackstabOn();   // auto backstab toggle AND its own activation bind
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Store(CBaseEntity* pEntity, size_t iSize);
	void Store(bool bFrameStageNotify = true);

	bool m_bRan = false;
	bool m_bToggled = false;          // latch for AimKeyMode == Toggle
	bool m_bBackstabToggled = false;  // latch for BackstabKeyMode == Toggle
	// set for the pass where the master aimbot / aim key is off and ONLY auto backstab is
	// allowed to act. Read by CAimbotMelee::Run so non-knife melee aim stays off.
	bool m_bBackstabOnly = false;
	bool m_bRunningSecondary = false;
	// huntsman / sticky fire on the tick attack lifts. Hold key on mouse1 reads up that same tick,
	// so aim runs one extra pass for the release when the key was down during this charge.
	// m_bReleasePass marks that pass: aim + autoshoot only, never re-hold attack.
	bool m_bChargeKey = false;
	bool m_bReleasePass = false;

	DrawPath_t m_tPath = {};
};

ADD_FEATURE(CAimbot, Aimbot);