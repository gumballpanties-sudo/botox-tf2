#pragma once
#include "sdk.h"

#include "aim_global.h"

class CAutoDetonate
{
private:
	bool CheckDetonation(CTFPlayer* pLocal, EntityEnum::EntityEnum iGroup, float flRadiusScale, CUserCmd* pCmd);
	bool CheckSelf(CTFPlayer* pLocal, EntityEnum::EntityEnum iGroup);

	void PredictPlayers(CTFPlayer* pLocal, float flLatency, bool bLocal = false);
	void RestorePlayers();

	std::unordered_map<CBaseEntity*, Vec3> m_mRestore = {};

public:
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
};

ADD_FEATURE(CAutoDetonate, AutoDetonate);