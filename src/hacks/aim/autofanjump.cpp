#include "sdk.h"
#include "aim_global.h"

// Unibox Features/Misc/Misc.cpp CMisc::AutoFaNJump, 1:1. AutoFaNJump = g_autoFaN + its bind,
// debug vars at their defaults (on solid ticks 8, check ceiling on). Runs after the aimbot +
// nospread, Unibox's CreateMove slot (hooks.cpp). Scout + Force-a-Nature: shoot behind your
// velocity at 37 deg (more with speed) for the knockback jump; at speed in open air it aims
// straight down to keep the speed instead.

extern bool g_autoFaN;
extern int  g_autoFaNKey, g_autoFaNMode;
bool MovementKeyOK(int key, int mode);
void DebugLog(const char* fmt, ...);

static constexpr int  kOnSolidTicks = 8;
static constexpr bool kCheckCeiling = true;
// bspflags.h: everything that blocks player movement, brushes only
static constexpr unsigned kMaskPlayerSolidBrushOnly = CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_WINDOW | CONTENTS_PLAYERCLIP | CONTENTS_GRATE;

// CTFPlayerShared::m_bScattergunJump (tf_player_shared.h:1219) is not networked, only a pred
// field (tf_player_shared.cpp:481), so it is read through the pred datamap by name. Unibox's
// hardcoded m_flSpyTranqBuffDuration + 20 disagrees with the SDK's x64 layout (+24); the name
// survives game updates, an offset does not. 0 = not found.
static int FindPredField(datamap_t* pMap, const char* sName, int iDepth = 0)
{
	for (; pMap && iDepth < 16; pMap = pMap->baseMap)
	{
		for (int i = 0; i < pMap->dataNumFields; i++)
		{
			const typedescription_t& tField = pMap->dataDesc[i];
			if (!tField.fieldName)
				continue;
			if (!strcmp(tField.fieldName, sName))
				return tField.fieldOffset[TD_OFFSET_NORMAL];
			if (tField.fieldType == FIELD_EMBEDDED && tField.td)
				if (int iInner = FindPredField(tField.td, sName, iDepth + 1))
					return tField.fieldOffset[TD_OFFSET_NORMAL] + iInner;
		}
	}
	return 0;
}

static bool ScattergunJumped(CTFPlayer* pLocal)
{
	static int s_iOffset = -1;
	if (s_iOffset == -1)
	{
		s_iOffset = FindPredField(pLocal->GetPredDescMap(), "m_bScattergunJump");
		DebugLog("FAN: m_bScattergunJump pred offset 0x%X%s", s_iOffset, s_iOffset ? "" : " (missing, gate off)");
	}
	return s_iOffset > 0 && *reinterpret_cast<bool*>(uintptr_t(pLocal) + s_iOffset);
}

void AutoFaNJump_Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	static bool bDidJump = false, bCanJump = false, bShouldRun = false;
	static int iTicksOnSolid = kOnSolidTicks;

	bool bOnSolid = pLocal->m_hGroundEntity() || pLocal->IsOnGround(); // CBasePlayer::OnSolid
	if (!bOnSolid)
	{
		iTicksOnSolid = 0;
		if (pLocal->InCond(TF_COND_STUNNED))
			bCanJump = false;
	}
	else iTicksOnSolid += bCanJump = true;

	if (bDidJump)
	{
		if (iTicksOnSolid >= kOnSolidTicks)
		{
			bDidJump = false;
			bShouldRun = false;
		}
		return;
	}

	if (!g_autoFaN || !MovementKeyOK(g_autoFaNKey, g_autoFaNMode) || !bCanJump
		|| G::Attacking == 1 || !G::CanPrimaryAttack
		|| !pLocal->IsAlive() || pLocal->IsAGhost()
		|| ScattergunJumped(pLocal)
		|| pLocal->m_MoveType() != MOVETYPE_WALK || pLocal->IsSwimming()
		|| pLocal->IsTaunting() || pLocal->InCond(TF_COND_HALLOWEEN_KART))
		return;

	if (!pWeapon || pWeapon->m_iClip1() <= 0)
		return;

	int iDefIndex = pWeapon->m_iItemDefinitionIndex();
	if (iDefIndex != Scout_m_ForceANature && iDefIndex != Scout_m_FestiveForceANature)
		return;

	const Vec3 vVelocity = pLocal->m_vecVelocity();

	// If we dont have enough speed we wont be able to climb anything.
	// From my experience 150.f is enough to climb the grate dropdowns on 2fort bases
	if (vVelocity.Length2D() <= 150.f)
		return;

	Vec3 vAngles;
	float flScale = 0.f;
	if (vVelocity.Length2D() > 300.f && !bOnSolid)
	{
		flScale = Math::RemapVal(vVelocity.Length2D(), 300.f, 450.f, 0.f, 1.f);

		CGameTrace wallTrace = {};
		CTraceFilterWorldAndPropsOnly filter = {}; filter.pSkip = pLocal;
		Vec3 vTrace = pLocal->GetCenter(), vMins = pLocal->m_vecMins() * 3, vMaxs = pLocal->m_vecMaxs() * 3;
		vMins.z = -1;
		SDK::TraceHull(vTrace, vTrace, vMins, vMaxs, kMaskPlayerSolidBrushOnly, &filter, &wallTrace);
		if (!wallTrace.DidHit())
		{
			// Preserve speed if we are not attempting to climb
			vAngles = pCmd->viewangles;
			vAngles.x = 89.f;
		}
	}

	if (vAngles.x == 0.f)
	{
		Math::VectorAngles(vVelocity, vAngles);

		// 2fort sewers to bridge jump pitch angle if we have enough speed and not on solid
		vAngles.x = 37.f * (1 + 0.5f * flScale);
		vAngles.z = 0;
	}

	if (!bShouldRun)
	{
		if (!kCheckCeiling || vVelocity.z <= -300.f)
			bShouldRun = true;
		else
		{
			CGameTrace ceilingTrace = {};
			CTraceFilterWorldAndPropsOnly filter = {}; filter.pSkip = pLocal;
			Vec3 vStart = pLocal->m_vecOrigin(); vStart += Vec3(0, 0, pLocal->m_vecMaxs().z);
			Vec3 vEnd = vStart + Vec3(0, 0, pLocal->m_vecMaxs().z * 1.25f);

			// Increase hull size because if we change the pitch the trajectory also changes
			float flSizeScale = 1 + 0.75f * flScale;
			SDK::TraceHull(vStart, vEnd, pLocal->m_vecMins() * flSizeScale, pLocal->m_vecMaxs() * flSizeScale, kMaskPlayerSolidBrushOnly, &filter, &ceilingTrace);
			if (!ceilingTrace.DidHit())
				bShouldRun = true;
		}
	}

	if (bShouldRun)
	{
		if (bOnSolid)
			pCmd->buttons |= IN_JUMP;

		pCmd->buttons |= IN_ATTACK;
		G::Attacking = true;
		bDidJump = true;

		pCmd->viewangles = vAngles;
		G::SilentAngles = true;
	}
}
