#pragma once
#include "sdk.h"

#include "aim_global.h"

// Amalgam CAutoRocketJump (Features/Aimbot/AutoRocketJump), 1:1. Only change: it starts when
// the auto goomba (movement_ex.inl, type "rocket jump") says a falling player will reach our
// head right after the blast -- Amalgam's own bind starts it whenever it can.
class CAutoRocketJump
{
private:
	bool SetAngles(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);

	Vec3 m_vAngles = {};
	bool m_bFull = false;
	int m_iFrame = -1;
	int m_iDelay = 0;
	bool m_bRunning = false;

public:
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	bool IsRunning() { return m_bRunning; }
};

ADD_FEATURE(CAutoRocketJump, AutoRocketJump);
