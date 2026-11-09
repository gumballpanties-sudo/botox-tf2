#pragma once
#include "sdk.h"

#include "aim_global.h"

// Amalgam CAutoRocketJump (Features/Aimbot/AutoRocketJump), 1:1. Only change: it starts when
// the auto goomba (movement_ex.inl, type "rocket jump") says a falling player will reach our
// head right after the blast -- Amalgam's own bind starts it whenever it can.
// RunRJ = Unibox's standalone auto rocket jump (own bind, g_autoRJ), 1:1.
class CAutoRocketJump
{
private:
	bool SetAngles(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void RunRJ(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, bool bLastGrounded, bool bCurrGrounded);

	Vec3 m_vAngles = {};
	bool m_bFull = false;
	int m_iFrame = -1;
	int m_iDelay = 0;
	int m_iChoke = 0;
	bool m_bRJ = false; // the sequence in flight (m_iFrame != -1) is RunRJ's, not the goomba's
	bool m_bRunning = false;

public:
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	bool IsRunning() { return m_bRunning; }
};

ADD_FEATURE(CAutoRocketJump, AutoRocketJump);
