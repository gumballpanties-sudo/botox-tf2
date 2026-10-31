#include "grenade_lob.h"

#include "projsim.h"
#include "lob_solve.h"

// Same 3-mode bind pattern as CAimbot::KeyOK/BackstabOn (aimbot.cpp) -- always on / hold /
// toggle, edge-detected once per command.
bool CGrenadeLob::KeyOK()
{
	const int iKey = Vars::Aimbot::GrenadeLob::Key.Value;
	const int iMode = Vars::Aimbot::GrenadeLob::KeyMode.Value;

	if (iMode != Vars::Aimbot::GrenadeLob::KeyModeEnum::OnHotkey
		&& iMode != Vars::Aimbot::GrenadeLob::KeyModeEnum::Toggle)
		return true;
	if (!iKey)
		return true;

	const bool bDown = (GetAsyncKeyState(iKey) & 0x8000) != 0;
	if (iMode == Vars::Aimbot::GrenadeLob::KeyModeEnum::OnHotkey)
		return bDown;

	static bool s_bWasDown = false;
	if (bDown && !s_bWasDown)
		m_bToggled = !m_bToggled;
	s_bWasDown = bDown;
	return m_bToggled;
}

bool CGrenadeLob::SimulateLanding(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, float flPitch, float flYaw, Vec3& vLandPoint, std::vector<Vec3>* pPath)
{
	ProjectileInfo tProjInfo = {};
	const int iFlags = ProjSimEnum::InitCheck | ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum;
	if (!F::ProjSim.GetInfo(pLocal, pWeapon, { flPitch, flYaw, 0.f }, tProjInfo, iFlags)
		|| !F::ProjSim.Initialize(tProjInfo))
		return false;

	CTraceFilterWorldAndPropsOnly filter;
	filter.pSkip = pLocal;

	// ~2.6s of flight is plenty for every arcing TF2 weapon (stickies/pipes/jars/flares) and
	// caps the per-call cost -- this runs up to ~9 times per solve (SolvePitch's secant loop).
	const int iMaxTicks = 175;
	Vec3 vPrev = F::ProjSim.GetOrigin();
	if (pPath) pPath->push_back(vPrev);

	for (int n = 0; n < iMaxTicks; n++)
	{
		F::ProjSim.RunTick(tProjInfo);
		Vec3 vCur = F::ProjSim.GetOrigin();
		if (pPath) pPath->push_back(vCur);

		CGameTrace trace = {};
		SDK::TraceHull(vPrev, vCur, tProjInfo.m_vHull * -1, tProjInfo.m_vHull, MASK_SOLID, &filter, &trace);
		if (trace.DidHit())
		{
			vLandPoint = trace.endpos;
			return true;
		}
		vPrev = vCur;
	}

	vLandPoint = vPrev;   // never hit anything inside the budget -- caller sees it as a big miss
	return true;
}

bool CGrenadeLob::SolvePitch(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, const Vec3& vEye, const Vec3& vPoint, float& flPitch)
{
	const float flYaw = Math::CalcAngle(vEye, vPoint).y;
	const float flTargetDist = (vPoint - vEye).Length2D();

	bool bSimFailed = false;
	auto fnError = [&](float flPitchTry) -> float
		{
			Vec3 vLand;
			if (!SimulateLanding(pLocal, pWeapon, flPitchTry, flYaw, vLand))
			{
				bSimFailed = true;
				return 0.f;
			}
			return (vLand - vEye).Length2D() - flTargetDist;
		};

	float flOut = 0.f;
	if (!LobSolve::Secant(fnError, flPitch, 4.f, 8, -89.f, 89.f, &flOut) || bSimFailed)
		return false;

	flPitch = flOut;
	return true;
}

void CGrenadeLob::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!Vars::Aimbot::GrenadeLob::Enabled.Value || !pLocal || !pWeapon || !pCmd || !KeyOK())
		return;

	// Only arcing weapons -- CProjectileSimulation::GetGravity already encodes which ones do
	// (grenades = 1, flares/arrows/bolts partial, hitscan/rockets = 0), so that's the gate
	// instead of a second weapon-ID list to keep in sync with it.
	ProjectileInfo tProbe = {};
	if (!F::ProjSim.GetInfo(pLocal, pWeapon, pCmd->viewangles, tProbe, ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum)
		|| tProbe.m_flGravity <= 0.f)
		return;

	const Vec3 vEye = pLocal->GetShootPos();

	Vec3 vForward; Math::AngleVectors(pCmd->viewangles, &vForward);
	CTraceFilterWorldAndPropsOnly filter;
	filter.pSkip = pLocal;
	CGameTrace trace = {};
	SDK::Trace(vEye, vEye + vForward * 8192.f, MASK_SOLID, &filter, &trace);
	const Vec3 vPoint = trace.endpos;

	// Throttle: only re-run the secant solve (up to 9 physics sims) when the crosshair point
	// actually moved. Otherwise keep applying the last solved pitch -- cheap and stable while
	// the player holds still lining up a throw.
	if (vPoint.DistTo(m_vLastPoint) > 8.f)
	{
		float flPitch = pCmd->viewangles.x;
		if (!SolvePitch(pLocal, pWeapon, vEye, vPoint, flPitch))
			return;   // no convergence -- leave the player's own aim alone
		m_flLastPitch = flPitch;
		m_vLastPoint = vPoint;
	}

	pCmd->viewangles.x = m_flLastPitch;
	Math::ClampAngles(pCmd->viewangles);
	I::EngineClient->SetViewAngles(pCmd->viewangles);

	if (Vars::Aimbot::GrenadeLob::DrawPath.Value)
	{
		std::vector<Vec3> vPath;
		Vec3 vLand;
		if (SimulateLanding(pLocal, pWeapon, m_flLastPitch, Math::CalcAngle(vEye, vPoint).y, vLand, &vPath) && vPath.size() > 1)
			G::PathStorage.emplace_back(vPath, I::GlobalVars->curtime + 0.5f, Vars::Aimbot::GrenadeLob::PathColor.Value, 0);
	}
}
