#pragma once
#include "sdk.h"

class CAimbot
{
private:
	bool KeyOK();   // activation bind (always on / hold / toggle)
	bool ShouldRun(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	void RunMain(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void RunAimbot(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, bool bSecondaryType = false);

	size_t m_iSize = 0;
	int m_iPlayer = 0;

public:
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

	DrawPath_t m_tPath = {};
};

ADD_FEATURE(CAimbot, Aimbot);