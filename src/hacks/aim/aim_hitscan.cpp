#include "aim_hitscan.h"

#include "aimbot.h"
#include "ticks.h"
#include "movesim.h"
#include "weapon_tuning.h"
#include "backtrack.h"
#include <span>

std::vector<Target_t> CAimbotHitscan::GetTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	std::vector<Target_t> vTargets;

	Vec3 vLocalPos = F::Ticks.GetShootPos();
	Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

	{
		auto eGroupType = EntityEnum::Invalid;
		if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Players)
		{
			eGroupType = !F::AimbotGlobal.FriendlyFire() ? EntityEnum::PlayerEnemy : EntityEnum::PlayerAll;
			if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::ExtinguishTeam &&
				!F::AimbotGlobal.FriendlyFire() && SDK::AttribHookValue(0, "jarate_duration", pWeapon) > 0)
				eGroupType = EntityEnum::PlayerAll;
		}
		// the medigun has no aim target of its own
		if (pWeapon->GetWeaponID() == TF_WEAPON_MEDIGUN)
			eGroupType = EntityEnum::Invalid;

		for (auto pEntity : H::Entities.GetGroup(eGroupType))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			bool bTeam = pEntity->m_iTeamNum() == pLocal->m_iTeamNum();
			if (bTeam)
			{
				if (!F::AimbotGlobal.FriendlyFire() && SDK::AttribHookValue(0, "jarate_duration", pWeapon) > 0)
				{
					if (!pEntity->As<CTFPlayer>()->InCond(TF_COND_BURNING))
						continue;
				}
			}

			float flFOVTo; Vec3 vPos, vAngleTo;
			// botox gates every record on its own cosine: a usable record inside the fov keeps
			// a target whose live body is outside it
			if (!F::AimbotGlobal.PlayerBoneInFOV(pEntity->As<CTFPlayer>(), vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo, Vars::Aimbot::Hitscan::Hitboxes.Value, WeaponTuning::GetFOV(pWeapon))
				&& !F::Backtrack.RecordInFOV(pEntity, vLocalPos, vLocalAngles, WeaponTuning::GetFOV(pWeapon), flFOVTo, vPos, vAngleTo))
				continue;

			int iPriority = F::AimbotGlobal.GetPriority(pEntity->entindex());

			float flDistTo = 0.f; // sorting is always FOV, the distance key is unused
			vTargets.emplace_back(pEntity, TargetEnum::Player, vPos, vAngleTo, flFOVTo, flDistTo, iPriority);
		}

		if (pWeapon->GetWeaponID() == TF_WEAPON_MEDIGUN)
			return vTargets;
	}

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Building)
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			Vec3 vPos = pEntity->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			if (flFOVTo > Vars::Aimbot::General::AimFOV.Value)
				continue;

			float flDistTo = 0.f; // sorting is always FOV, the distance key is unused
			vTargets.emplace_back(pEntity, pEntity->IsSentrygun() ? TargetEnum::Sentry : pEntity->IsDispenser() ? TargetEnum::Dispenser : TargetEnum::Teleporter, vPos, vAngleTo, flFOVTo, flDistTo);
		}
	}

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Stickies)
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldProjectile))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			Vec3 vPos = pEntity->m_vecOrigin();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			if (flFOVTo > Vars::Aimbot::General::AimFOV.Value)
				continue;

			float flDistTo = 0.f; // sorting is always FOV, the distance key is unused
			vTargets.emplace_back(pEntity, TargetEnum::Sticky, vPos, vAngleTo, flFOVTo, flDistTo);
		}
	}

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::NPCs)
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldNPC))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			Vec3 vPos = pEntity->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			if (flFOVTo > Vars::Aimbot::General::AimFOV.Value)
				continue;

			float flDistTo = 0.f; // sorting is always FOV, the distance key is unused
			vTargets.emplace_back(pEntity, TargetEnum::NPC, vPos, vAngleTo, flFOVTo, flDistTo);
		}
	}

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Bombs)
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldBomb))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			Vec3 vPos = pEntity->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			if (flFOVTo > Vars::Aimbot::General::AimFOV.Value)
				continue;

			float flDistTo = 0.f; // sorting is always FOV, the distance key is unused
			vTargets.emplace_back(pEntity, TargetEnum::Bomb, vPos, vAngleTo, flFOVTo, flDistTo);
		}
	}

	return vTargets;
}

std::vector<Target_t> CAimbotHitscan::SortTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	auto vTargets = GetTargets(pLocal, pWeapon);

	// target selection is always FOV now.
	F::AimbotGlobal.SortTargets(vTargets, Vars::Aimbot::General::TargetSelectionEnum::FOV);
	vTargets.resize(std::min(size_t(Vars::Aimbot::General::MaxTargets.Value), vTargets.size()));
	F::AimbotGlobal.SortPriority(vTargets);
	return vTargets;
}



int CAimbotHitscan::GetHitboxPriority(int nHitbox, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pTarget)
{
	if (!F::AimbotGlobal.IsHitboxValid(pTarget, nHitbox, Vars::Aimbot::Hitscan::Hitboxes.Value))
		return -1;

	bool bHeadshot = false;
	if (pTarget->IsPlayer())
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_SNIPERRIFLE:
		case TF_WEAPON_SNIPERRIFLE_DECAP:
		case TF_WEAPON_SNIPERRIFLE_CLASSIC:
		{
			auto pSniperRifle = pWeapon->As<CTFSniperRifle>();

			if (G::CanHeadshot
				|| pLocal->InCond(TF_COND_AIMING) && (pSniperRifle->GetRifleType() == RIFLE_JARATE && SDK::AttribHookValue(0, "jarate_duration", pWeapon) > 0 || Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::WaitForHeadshot))
				bHeadshot = true;
			break;
		}
		case TF_WEAPON_REVOLVER:
		{
			if (SDK::AttribHookValue(0, "set_weapon_mode", pWeapon) == 1 && (pWeapon->AmbassadorCanHeadshot() || Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::WaitForHeadshot))
				bHeadshot = true;
		}
		}

		if (Vars::Aimbot::Hitscan::Hitboxes.Value & Vars::Aimbot::Hitscan::HitboxesEnum::BodyaimIfLethal && bHeadshot)
		{
			auto pPlayer = pTarget->As<CTFPlayer>();

			switch (pWeapon->GetWeaponID())
			{
			case TF_WEAPON_SNIPERRIFLE:
			case TF_WEAPON_SNIPERRIFLE_DECAP:
			case TF_WEAPON_SNIPERRIFLE_CLASSIC:
			{
				auto pSniperRifle = pWeapon->As<CTFSniperRifle>();

				int iDamage = std::ceil(std::max(pSniperRifle->m_flChargedDamage(), 50.f) * pSniperRifle->GetBodyshotMult(pPlayer));
				if (pPlayer->m_iHealth() <= iDamage)
					bHeadshot = false;
				break;
			}
			case TF_WEAPON_REVOLVER:
			{
				if (SDK::AttribHookValue(0, "set_weapon_mode", pWeapon) == 1)
				{
					float flDistTo = pTarget->m_vecOrigin().DistTo(pLocal->m_vecOrigin());

					float flMult = SDK::AttribHookValue(1, "mult_dmg", pWeapon);
					int iDamage = std::ceil(Math::RemapVal(flDistTo, 90.f, 900.f, 60.f, 21.f) * flMult);
					if (pPlayer->m_iHealth() <= iDamage)
						bHeadshot = false;
				}
			}
			}
		}
	}
	
	bool bHeadOnly = bHeadshot && Vars::Aimbot::Hitscan::Hitboxes.Value & Vars::Aimbot::Hitscan::HitboxesEnum::HeadshotOnly;

	int iHeadPriority = bHeadOnly || bHeadshot ? 0 : 1;
	int iBodyPriority = bHeadOnly ? -1 : bHeadshot ? 1 : 0;
	int iMiscPriority = bHeadOnly ? -1 : 2;
	int iLimbPriority = bHeadOnly ? -1 : 3;

	switch (pTarget->GetHitboxToBase(nHitbox))
	{
	case HITBOX_HEAD: return iHeadPriority;
	case HITBOX_SPINE0:
	case HITBOX_SPINE1:
	case HITBOX_SPINE2:
	case HITBOX_SPINE3: return iBodyPriority;
	case HITBOX_PELVIS: return iMiscPriority;
	}

	return iLimbPriority;
};

int CAimbotHitscan::CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Unsimulated && H::Entities.GetChoke(tTarget.m_pEntity->entindex()) > Vars::Aimbot::General::TickTolerance.Value)
		return false;

	m_vEyePos = pLocal->GetShootPos();
	const float flMaxRange = powf(pWeapon->GetRange(), 2.f);

	auto pModel = tTarget.m_pEntity->GetModel();
	if (!pModel) return false;
	auto pHDR = I::ModelInfoClient->GetStudiomodel(pModel);
	if (!pHDR) return false;
	auto pSet = pHDR->pHitboxSet(tTarget.m_pEntity->As<CBaseAnimating>()->m_nHitboxSet());
	if (!pSet) return false;

	// No tick history: one record, the target's live bones.
	// Fields set one by one, NOT `g_tLiveRecord = { ... }`: TickRecord carries
	// matrix3x4 m_aBones[MAXSTUDIOBONES] = 6144 bytes, so the aggregate built a zeroed temp and
	// copy-assigned 6.2 KB -- per target, per command -- straight before SetupBones overwrote it.
	g_tLiveRecord.m_flSimTime = tTarget.m_pEntity->m_flSimulationTime();
	g_tLiveRecord.m_vOrigin = tTarget.m_pEntity->m_vecOrigin();
	g_tLiveRecord.m_vMins = g_tLiveRecord.m_vMaxs = Vec3();
	g_tLiveRecord.m_bOnShot = g_tLiveRecord.m_bInvalid = false;
	// BONE_USED_BY_HITBOX, not BONE_USED_BY_ANYTHING -- see the long note in
	// CAimbotGlobal::PlayerBoneInFOV (aim_global.cpp). Short version: ANYTHING can never hit the
	// bone cache, so it forced a full rebuild here AND permanently upgraded the game's own
	// render-path bone setup for this player. Every read below is aBones[pBox->bone].
	if (!tTarget.m_pEntity->SetupBones(g_tLiveRecord.m_aBones, MAXSTUDIOBONES, BONE_USED_BY_HITBOX, tTarget.m_pEntity->m_flSimulationTime()))
		return false;

	// botox scan_targets: live record (scan.m_live) + every usable lag record (scan.m_records),
	// see m_iBtPass. Usable on THIS cmd's clock: a record the server rejects is rewound to the
	// window centre = miss. Players only: buildings are not lag compensated.
	static TickRecord* vRecordBuf[1 + CBacktrack::kMaxRecords];
	std::span<TickRecord*> vRecords;
	{
		int nRecords = 0;
		if (m_iBtPass == 0)
			vRecordBuf[nRecords++] = &g_tLiveRecord;
		if (tTarget.m_iTargetType == TargetEnum::Player && (m_iBtPass == 1 || F::Backtrack.AimAt()))
		{
			if (auto pList = F::Backtrack.GetRecords(tTarget.m_pEntity->entindex()))
			{
				for (int i = 0; i < F::Backtrack.Count(); i++)
					if (F::Backtrack.Usable(tTarget.m_pEntity, pList[i].m_flSimTime))
						vRecordBuf[nRecords++] = &pList[i];
			}
		}

		// botox ranks every candidate point by crosshair cosine and takes the best. CanHit takes
		// the first record that hits, so order them by fov to their body centre first.
		// ponytail: body centre, not per hitbox; per-hitbox rank if a far-off record keeps winning
		if (nRecords > 1)
		{
			const Vec3 vView = G::CurrentUserCmd->viewangles, vHalf = (tTarget.m_pEntity->m_vecMins() + tTarget.m_pEntity->m_vecMaxs()) / 2;
			auto Fov = [&](const TickRecord* p) { return Math::CalcFov(vView, Math::CalcAngle(m_vEyePos, p->m_vOrigin + vHalf)); };
			std::sort(vRecordBuf, vRecordBuf + nRecords, [&](const TickRecord* a, const TickRecord* b) { return Fov(a) < Fov(b); });
		}
		vRecords = { vRecordBuf, size_t(nRecords) };
	}
	if (vRecords.empty())
		return false;

	bool bPeekCheck = false;
	if (Vars::Aimbot::Hitscan::PeekAmount.Value && pWeapon->GetWeaponSpread())
	{
		switch (Vars::Aimbot::Hitscan::PeekCheck.Value)
		{
		case Vars::Aimbot::Hitscan::PeekCheckEnum::Off: break;
		case Vars::Aimbot::Hitscan::PeekCheckEnum::DoubletapOnly: bPeekCheck = F::Ticks.GetTicks(pWeapon); break;
		case Vars::Aimbot::Hitscan::PeekCheckEnum::Always: bPeekCheck = true; break;
		}
	}
	Vec3 vPeekPos = bPeekCheck ? m_vEyePos + pLocal->m_vecVelocity() * TICKS_TO_TIME(-Vars::Aimbot::Hitscan::PeekAmount.Value) : Vec3();

	// THE DOUBLETAP RECORD SORT IS GONE, and it could never have done anything.
	// It ordered vRecords by "closest bone to the frozen doubletap angle" -- a backtrack idea.
	// This build keeps NO tick history (see aim_record.h): vRecords is one element, the live
	// record, so the sort had nothing to order. Worse, it could not have ordered anything even
	// with history: vPosA and vPosB both read `a->m_aBones`, so the comparator was f(a) < f(a),
	// false for every pair. And `iTargetBone` was a static that nothing ever assigned -- bone 0.
	// Re-add it WITH a record store, not before one.

	int iReturn = false;
	for (auto pRecord : vRecords)
	{
		bool bRunPeekCheck = bPeekCheck;

		if (pWeapon->GetWeaponID() == TF_WEAPON_LASER_POINTER)
		{
			tTarget.m_vPos = tTarget.m_pEntity->m_vecOrigin();

			// not lag compensated (i assume) so run movesim based on ping
			MoveStorage tStorage;
			F::MoveSim.Initialize(tTarget.m_pEntity, tStorage);
			if (!tStorage.m_bFailed)
			{
				for (int i = 1 - TIME_TO_TICKS(AimLatency()); i <= 0; i++)
				{
					F::MoveSim.RunTick(tStorage);
					tTarget.m_vPos = tStorage.m_vPredictedOrigin;
				}
			}
			F::MoveSim.Restore(tStorage);

			float flBoneScale = std::max(Vars::Aimbot::Hitscan::BoneSizeMinimumScale.Value, Vars::Aimbot::Hitscan::MultipointScale.Value / 100.f);
			float flBoneSubtract = Vars::Aimbot::Hitscan::BoneSizeSubtract.Value;

			Vec3 vMins = tTarget.m_pEntity->m_vecMins();
			Vec3 vMaxs = tTarget.m_pEntity->m_vecMaxs();
			Vec3 vCheckMins = (vMins + flBoneSubtract) * flBoneScale;
			Vec3 vCheckMaxs = (vMaxs - flBoneSubtract) * flBoneScale;

			const matrix3x4 mTransform = { { 1, 0, 0, tTarget.m_vPos.x }, { 0, 1, 0, tTarget.m_vPos.y }, { 0, 0, 1, tTarget.m_vPos.z } };

			tTarget.m_vPos += tTarget.m_pEntity->GetOffset() / 2;
			if (m_vEyePos.DistToSqr(tTarget.m_vPos) > flMaxRange)
				break;

			if (SDK::VisPosWorld(pLocal, tTarget.m_pEntity, m_vEyePos, tTarget.m_vPos))
			{
				Vec3 vAngles; bool bChanged = Aim(G::CurrentUserCmd->viewangles, Math::CalcAngle(m_vEyePos, tTarget.m_vPos), vAngles);
				Vec3 vForward; Math::AngleVectors(vAngles, &vForward);
				float flDist = m_vEyePos.DistTo(tTarget.m_vPos);

				if (!bChanged || Math::RayToOBB(m_vEyePos, vForward, vCheckMins, vCheckMaxs, mTransform) && SDK::VisPos(pLocal, tTarget.m_pEntity, m_vEyePos, m_vEyePos + vForward * flDist))
				{
					tTarget.m_vAngleTo = vAngles;
					tTarget.m_pRecord = pRecord;
					return true;
				}
				else if (iReturn == 2 ? vAngles.DeltaAngle(G::CurrentUserCmd->viewangles).Length2D() < tTarget.m_vAngleTo.DeltaAngle(G::CurrentUserCmd->viewangles).Length2D() : true)
					tTarget.m_vAngleTo = vAngles;
				iReturn = 2;
			}

			break;
		}

		if (tTarget.m_iTargetType == TargetEnum::Player)
		{
			auto aBones = pRecord->m_aBones;
			if (!aBones)
				continue;

			std::vector<std::tuple<const mstudiobbox_t*, int, int>> vHitboxes;
			for (int nHitbox = 0; nHitbox < pSet->numhitboxes; nHitbox++)
			{
				int iPriority = GetHitboxPriority(nHitbox, pLocal, pWeapon, tTarget.m_pEntity);
				if (iPriority == -1)
					continue;

				auto pBox = pSet->pHitbox(nHitbox);
				if (!pBox) continue;

				vHitboxes.emplace_back(pBox, nHitbox, iPriority);
			}
			std::sort(vHitboxes.begin(), vHitboxes.end(), [&](const auto& a, const auto& b) -> bool
				{
					return std::get<2>(a) < std::get<2>(b);
				});

			float flModelScale = tTarget.m_pEntity->As<CBaseAnimating>()->m_flModelScale();
			float flBoneScale = Vars::Aimbot::Hitscan::BoneSizeMinimumScale.Value;
			float flBoneSubtract = Vars::Aimbot::Hitscan::BoneSizeSubtract.Value;

			auto pGameRules = I::TFGameRules();
			auto pViewVectors = pGameRules ? pGameRules->GetViewVectors() : nullptr;
			Vec3 vHullMins = (pViewVectors ? pViewVectors->m_vHullMin : Vec3(-24, -24, 0)) * flModelScale;
			Vec3 vHullMaxs = (pViewVectors ? pViewVectors->m_vHullMax : Vec3(24, 24, 82)) * flModelScale;

			const matrix3x4 mTransform = { { 1, 0, 0, pRecord->m_vOrigin.x }, { 0, 1, 0, pRecord->m_vOrigin.y }, { 0, 0, 1, pRecord->m_vOrigin.z } };

			for (auto& [pBox, nHitbox, _] : vHitboxes)
			{
				Vec3 vMins = pBox->bbmin;
				Vec3 vMaxs = pBox->bbmax;
				Vec3 vCheckMins = (vMins + flBoneSubtract / flModelScale) * flBoneScale;
				Vec3 vCheckMaxs = (vMaxs - flBoneSubtract / flModelScale) * flBoneScale;
				Vec3 vAngle; Math::MatrixAngles(aBones[pBox->bone], vAngle);

				Vec3 vOffset;
				{
					Vec3 vOrigin, vCenter;
					Math::VectorTransform({}, aBones[pBox->bone], vOrigin);
					Math::VectorTransform((vMins + vMaxs) / 2, aBones[pBox->bone], vCenter);
					vOffset = vCenter - vOrigin;
				}

				std::vector<Vec3> vPoints = { Vec3() };
				if (F::AimbotGlobal.ShouldMultipoint(tTarget.m_pEntity, nHitbox, Vars::Aimbot::Hitscan::MultipointHitboxes.Value))
				{
					flBoneScale = std::max(flBoneScale, Vars::Aimbot::Hitscan::MultipointScale.Value / 100.f);
					bool bTriggerbot = (Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Smooth
						|| Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Assistive)
						&& !Vars::Aimbot::General::AssistStrength.Value;

					if (!bTriggerbot)
					{
						float flScale = Vars::Aimbot::Hitscan::MultipointScale.Value / 100;
						Vec3 vMinsS = (vMins - vMaxs) / 2 * flScale;
						Vec3 vMaxsS = (vMaxs - vMins) / 2 * flScale;

						vPoints = {
							Vec3(),
							Vec3(vMinsS.x, vMinsS.y, vMaxsS.z),
							Vec3(vMaxsS.x, vMinsS.y, vMaxsS.z),
							Vec3(vMinsS.x, vMaxsS.y, vMaxsS.z),
							Vec3(vMaxsS.x, vMaxsS.y, vMaxsS.z),
							Vec3(vMinsS.x, vMinsS.y, vMinsS.z),
							Vec3(vMaxsS.x, vMinsS.y, vMinsS.z),
							Vec3(vMinsS.x, vMaxsS.y, vMinsS.z),
							Vec3(vMaxsS.x, vMaxsS.y, vMinsS.z)
						};
					}
				}

				for (auto& vPoint : vPoints)
				{
					Vec3 vOrigin; Math::VectorTransform(vPoint, aBones[pBox->bone], vOrigin); vOrigin += vOffset;

					if (m_vEyePos.DistToSqr(vOrigin) > flMaxRange)
						continue;

					if (bRunPeekCheck)
					{
						bRunPeekCheck = false;
						if (!SDK::VisPos(pLocal, tTarget.m_pEntity, vPeekPos, vOrigin))
							goto nextTick; // if we can't hit our primary hitbox, don't bother
					}

					Vec3 vAngles; bool bChanged = Aim(G::CurrentUserCmd->viewangles, Math::CalcAngle(m_vEyePos, vOrigin), vAngles);
					Vec3 vForward; Math::AngleVectors(vAngles, &vForward);
					float flDist = m_vEyePos.DistTo(vOrigin);

					if (bChanged || SDK::VisPos(pLocal, tTarget.m_pEntity, m_vEyePos, vOrigin))
					{
						// for the time being, no vischecks against other hitboxes
						if ((!bChanged || Math::RayToOBB(m_vEyePos, vForward, vCheckMins, vCheckMaxs, aBones[pBox->bone], flModelScale) && SDK::VisPos(pLocal, tTarget.m_pEntity, m_vEyePos, m_vEyePos + vForward * flDist))
							&& Math::RayToOBB(m_vEyePos, vForward, vHullMins, vHullMaxs, mTransform))
						{
							// (the `iTargetBone = pBox->bone` that stood here fed the record sort
							// deleted above -- its only reader)
							tTarget.m_vAngleTo = vAngles;
							tTarget.m_pRecord = pRecord;
							tTarget.m_vPos = vOrigin;
							tTarget.m_nAimedHitbox = nHitbox;
							return true;
						}
						else if (bChanged && SDK::VisPos(pLocal, tTarget.m_pEntity, m_vEyePos, vOrigin))
						{
							if (iReturn != 2 || vAngles.DeltaAngle(G::CurrentUserCmd->viewangles).Length2D() < tTarget.m_vAngleTo.DeltaAngle(G::CurrentUserCmd->viewangles).Length2D())
								tTarget.m_vAngleTo = vAngles;
							iReturn = 2;
						}
					}
				}
			}
		}
		else
		{
			float flBoneScale = std::max(Vars::Aimbot::Hitscan::BoneSizeMinimumScale.Value, Vars::Aimbot::Hitscan::MultipointScale.Value / 100.f);
			float flBoneSubtract = Vars::Aimbot::Hitscan::BoneSizeSubtract.Value;

			Vec3 vMins = tTarget.m_pEntity->m_vecMins();
			Vec3 vMaxs = tTarget.m_pEntity->m_vecMaxs();
			Vec3 vCheckMins = (vMins + flBoneSubtract) * flBoneScale;
			Vec3 vCheckMaxs = (vMaxs - flBoneSubtract) * flBoneScale;

			const matrix3x4& mTransform = tTarget.m_pEntity->m_Collision()->CollisionToWorldTransform();

			std::vector<Vec3> vPoints = { Vec3() };
			//if (Vars::Aimbot::Hitscan::MultipointScale.Value > 0.f)
			{
				bool bTriggerbot = (Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Smooth
					|| Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Assistive)
					&& !Vars::Aimbot::General::AssistStrength.Value;

				if (!bTriggerbot)
				{
					float flScale = 0.5f; //Vars::Aimbot::Hitscan::MultipointScale.Value / 100;
					Vec3 vMinsS = (vMins - vMaxs) / 2 * flScale;
					Vec3 vMaxsS = (vMaxs - vMins) / 2 * flScale;

					vPoints = {
						Vec3(),
						Vec3(vMinsS.x, vMinsS.y, vMaxsS.z),
						Vec3(vMaxsS.x, vMinsS.y, vMaxsS.z),
						Vec3(vMinsS.x, vMaxsS.y, vMaxsS.z),
						Vec3(vMaxsS.x, vMaxsS.y, vMaxsS.z),
						Vec3(vMinsS.x, vMinsS.y, vMinsS.z),
						Vec3(vMaxsS.x, vMinsS.y, vMinsS.z),
						Vec3(vMinsS.x, vMaxsS.y, vMinsS.z),
						Vec3(vMaxsS.x, vMaxsS.y, vMinsS.z)
					};
				}
			}

			for (auto& vPoint : vPoints)
			{
				Vec3 vOrigin = tTarget.m_pEntity->GetCenter() + vPoint;

				if (m_vEyePos.DistToSqr(vOrigin) > flMaxRange)
					continue;

				Vec3 vAngles; bool bChanged = Aim(G::CurrentUserCmd->viewangles, Math::CalcAngle(m_vEyePos, vOrigin), vAngles);
				Vec3 vForward; Math::AngleVectors(vAngles, &vForward);
				float flDist = m_vEyePos.DistTo(vOrigin);

				if (bChanged || SDK::VisPos(pLocal, tTarget.m_pEntity, m_vEyePos, vOrigin))
				{
					if (!bChanged || Math::RayToOBB(m_vEyePos, vForward, vCheckMins, vCheckMaxs, mTransform) && SDK::VisPos(pLocal, tTarget.m_pEntity, m_vEyePos, m_vEyePos + vForward * flDist))
					{
						tTarget.m_vAngleTo = vAngles;
						tTarget.m_pRecord = pRecord;
						tTarget.m_vPos = vOrigin;
						return true;
					}
					else if (bChanged && SDK::VisPos(pLocal, tTarget.m_pEntity, m_vEyePos, vOrigin))
					{
						if (iReturn != 2 || vAngles.DeltaAngle(G::CurrentUserCmd->viewangles).Length2D() < tTarget.m_vAngleTo.DeltaAngle(G::CurrentUserCmd->viewangles).Length2D())
							tTarget.m_vAngleTo = vAngles;
						iReturn = 2;
					}
				}
			}
		}

		nextTick:
		continue;
	}

	return iReturn;
}



bool CAimbotHitscan::ShouldFire(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, const Target_t& tTarget)
{
	if (!Vars::Aimbot::General::AutoShoot.Value)
		return false;

	if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::WaitForHeadshot
		&& tTarget.m_pEntity->IsPlayer())
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_SNIPERRIFLE:
		case TF_WEAPON_SNIPERRIFLE_DECAP:
			if (!G::CanHeadshot && pLocal->InCond(TF_COND_AIMING) && pWeapon->As<CTFSniperRifle>()->GetRifleType() != RIFLE_JARATE)
				return false;
			break;
		case TF_WEAPON_SNIPERRIFLE_CLASSIC:
			if (!G::CanHeadshot)
				return false;
			break;
		case TF_WEAPON_REVOLVER:
			if (SDK::AttribHookValue(0, "set_weapon_mode", pWeapon) == 1 && !pWeapon->AmbassadorCanHeadshot())
				return false;
		}
	}

	if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::WaitForCharge)
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_SNIPERRIFLE:
		case TF_WEAPON_SNIPERRIFLE_DECAP:
		case TF_WEAPON_SNIPERRIFLE_CLASSIC:
		{
			auto pSniperRifle = pWeapon->As<CTFSniperRifle>();
			if (!pLocal->InCond(TF_COND_AIMING) || pSniperRifle->m_flChargedDamage() == 150.f)
				break;

			if (tTarget.m_pEntity->IsPlayer())
			{
				auto pPlayer = tTarget.m_pEntity->As<CTFPlayer>();
				if (tTarget.m_nAimedHitbox == HITBOX_HEAD && (pWeapon->GetWeaponID() != TF_WEAPON_SNIPERRIFLE_CLASSIC || pSniperRifle->m_flChargedDamage() == 150.f))
				{
					int iDamage = std::ceil(std::max(pSniperRifle->m_flChargedDamage(), 50.f) * pSniperRifle->GetHeadshotMult(pPlayer));
					if (pPlayer->m_iHealth() <= iDamage && (G::CanHeadshot || pLocal->IsCritBoosted()))
						break;
				}
				else
				{
					int iDamage = std::ceil(std::max(pSniperRifle->m_flChargedDamage(), 50.f) * pSniperRifle->GetBodyshotMult(pPlayer));
					if (pPlayer->m_iHealth() <= iDamage)
						break;
				}
			}
			else if (tTarget.m_pEntity->IsBuilding())
			{
				auto pBuilding = tTarget.m_pEntity->As<CBaseObject>();
				int iDamage = std::ceil(std::max(pSniperRifle->m_flChargedDamage(), 50.f));
				if (pBuilding->m_iHealth() <= iDamage)
					break;
			}
			else
				break;
			
			return false;
		}
		}
	}

	return true;
}

bool CAimbotHitscan::Aim(Vec3 vCurAngle, Vec3 vToAngle, Vec3& vOut, int iMethod)
{
	auto pLocal = H::Entities.GetLocal();
	Vec3 vPunch = pLocal ? pLocal->m_vecPunchAngle() : Vec3();

	if (Vec3* pDoubletapAngle = F::Ticks.GetShootAngle())
	{
		vOut = *pDoubletapAngle - vPunch;
		return true;
	}

	bool bReturn = false;
	vToAngle -= vPunch;
	switch (iMethod)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
	case Vars::Aimbot::General::AimTypeEnum::Silent:
	case Vars::Aimbot::General::AimTypeEnum::Locking:
		vOut = vToAngle;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
		vOut = vCurAngle.LerpAngle(vToAngle, Vars::Aimbot::General::AssistStrength.Value / 100.f);
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
void CAimbotHitscan::Aim(CUserCmd* pCmd, Vec3& vAngle, int iMethod)
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
		pCmd->viewangles = vAngle;
		I::EngineClient->SetViewAngles(vAngle);
		break;
	case Vars::Aimbot::General::AimTypeEnum::Silent:
		if (G::Attacking == 1 || bUnsure)
		{
			SDK::FixMovement(pCmd, vAngle);
			pCmd->viewangles = vAngle;
			G::SilentAngles = true;
		}
		break;
	case Vars::Aimbot::General::AimTypeEnum::Locking:
		SDK::FixMovement(pCmd, vAngle);
		pCmd->viewangles = vAngle;
		G::SilentAngles = true;
	}
}

static inline void DrawVisuals(CTFPlayer* pLocal, Target_t& tTarget, int nWeaponID)
{
	if (G::Attacking == 1 && nWeaponID != TF_WEAPON_LASER_POINTER)
	{
		bool bLine = false; // on-shot overlays off: dih draws its own
		bool bBoxes = false;
		if (G::CanPrimaryAttack && (bLine || bBoxes))
		{
			G::LineStorage.clear();
			G::BoxStorage.clear();
			G::PathStorage.clear();

			if (bLine)
			{
				Vec3 vEyePos = pLocal->GetShootPos();
				float flDist = vEyePos.DistTo(tTarget.m_vPos);
				Vec3 vForward; Math::AngleVectors(tTarget.m_vAngleTo + pLocal->m_vecPunchAngle(), &vForward);

				if (Vars::Colors::LineIgnoreZ.Value.a)
					G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vEyePos, vEyePos + vForward * flDist), I::GlobalVars->curtime + Vars::Visuals::Line::DrawDuration.Value, Vars::Colors::LineIgnoreZ.Value);
				if (Vars::Colors::Line.Value.a)
					G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vEyePos, vEyePos + vForward * flDist), I::GlobalVars->curtime + Vars::Visuals::Line::DrawDuration.Value, Vars::Colors::Line.Value, true);
			}
		}
	}
}

void CAimbotHitscan::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	const int nWeaponID = pWeapon->GetWeaponID();

	static int iStaticAimType = Vars::Aimbot::General::AimType.Value;
	const int iLastAimType = iStaticAimType;
	const int iRealAimType = Vars::Aimbot::General::AimType.Value;

	switch (nWeaponID)
	{
	case TF_WEAPON_SNIPERRIFLE_CLASSIC:
		if (G::Attacking && !iRealAimType && iLastAimType)
			Vars::Aimbot::General::AimType.Value = iLastAimType;
	}
	iStaticAimType = Vars::Aimbot::General::AimType.Value;

	if (!Vars::Aimbot::General::AimType.Value
		|| !F::AimbotGlobal.ShouldAim() && (nWeaponID != TF_WEAPON_MINIGUN || pWeapon->As<CTFMinigun>()->m_iWeaponState() == AC_STATE_FIRING || pWeapon->As<CTFMinigun>()->m_iWeaponState() == AC_STATE_SPINNING))
		return;

	switch (nWeaponID)
	{
	case TF_WEAPON_MINIGUN:
		if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::AutoRev)
			pCmd->buttons |= IN_ATTACK2;
		if (pWeapon->As<CTFMinigun>()->m_iWeaponState() != AC_STATE_FIRING && pWeapon->As<CTFMinigun>()->m_iWeaponState() != AC_STATE_SPINNING)
			return;
		break;
	}

	auto vTargets = SortTargets(pLocal, pWeapon);
	if (vTargets.empty())
		return;

	switch (nWeaponID)
	{
	case TF_WEAPON_SNIPERRIFLE:
	case TF_WEAPON_SNIPERRIFLE_DECAP:
	{
		bool bScoped = pLocal->InCond(TF_COND_ZOOMED);
		if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::AutoScope && !bScoped)
		{
			pCmd->buttons |= IN_ATTACK2;
			return;
		}
		else if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::ScopedOnly && !bScoped)
			return;
		else if (!bScoped && SDK::AttribHookValue(0, "sniper_only_fire_zoomed", pWeapon))
			return;
		break;
	}
	case TF_WEAPON_SNIPERRIFLE_CLASSIC:
		if (iRealAimType)
			pCmd->buttons |= IN_ATTACK;
	}

	if (!G::AimTarget.m_iEntIndex)
		G::AimTarget = { vTargets.front().m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };

	// botox find_target: live (+ records with aim at backtrack) first; nothing hit and
	// backtrack on = second scan, records only (CanHit reads m_iBtPass)
	bool bDone = false;
	for (m_iBtPass = 0; m_iBtPass < 2 && !bDone; m_iBtPass++)
	{
		if (m_iBtPass == 1 && (!F::Backtrack.Enabled() || F::Backtrack.AimAt()))
			break;

		for (auto& tTarget : vTargets)
		{
			if (nWeaponID == TF_WEAPON_MEDIGUN && pWeapon->As<CWeaponMedigun>()->m_hHealingTarget().Get() == tTarget.m_pEntity)
			{
				if (G::LastUserCmd->buttons & IN_ATTACK)
					pCmd->buttons |= IN_ATTACK;
				return;
			}

			const auto iResult = CanHit(tTarget, pLocal, pWeapon);
			if (!iResult) continue;
			if (iResult == 2)
			{
				G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };
				Aim(pCmd, tTarget.m_vAngleTo);
				bDone = true;
				break;
			}

			G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
			G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };
			F::Backtrack.m_pTarget = tTarget.m_pRecord != &g_tLiveRecord ? tTarget.m_pRecord : nullptr;   // bt chams type 2

			if (ShouldFire(pLocal, pWeapon, pCmd, tTarget))
			{
				switch (nWeaponID)
				{
				case TF_WEAPON_MEDIGUN:
					if (!(G::LastUserCmd->buttons & IN_ATTACK))
						pCmd->buttons |= IN_ATTACK;
					break;
				case TF_WEAPON_SNIPERRIFLE_CLASSIC:
					if (pWeapon->As<CTFSniperRifle>()->m_flChargedDamage() && pLocal->m_hGroundEntity())
						pCmd->buttons &= ~IN_ATTACK;
					break;
				case TF_WEAPON_LASER_POINTER:
					pCmd->buttons |= IN_ATTACK | IN_ATTACK2;
					break;
				default:
					pCmd->buttons |= IN_ATTACK;
				}

				if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::Tapfire && pWeapon->GetWeaponSpread() != 0.f && !pLocal->InCond(TF_COND_RUNE_PRECISION)
					&& m_vEyePos.DistTo(tTarget.m_vPos) > Vars::Aimbot::Hitscan::TapfireDistance.Value)
				{
					const float flTimeSinceLastShot = (pLocal->m_nTickBase() * TICK_INTERVAL) - pWeapon->m_flLastFireTime();
					if (flTimeSinceLastShot <= (pWeapon->GetBulletsPerShot() > 1 ? 0.25f : 1.25f))
						pCmd->buttons &= ~IN_ATTACK;
				}
			}

			G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
			if (G::Attacking == 1 && nWeaponID != TF_WEAPON_LASER_POINTER && tTarget.m_iTargetType == TargetEnum::Player)
			{
				// botox commit_shot: the fire cmd asks lag comp for the record we aimed at
				tTarget.m_bBacktrack = tTarget.m_pRecord && tTarget.m_pRecord != &g_tLiveRecord;
				if (F::Backtrack.Enabled())
					F::Backtrack.CommitShot(tTarget.m_bBacktrack ? tTarget.m_pRecord : nullptr, pCmd);
			}
			DrawVisuals(pLocal, tTarget, nWeaponID);

			Aim(pCmd, tTarget.m_vAngleTo);
			if (G::SilentAngles)
			{
				switch (nWeaponID)
				{
				case TF_WEAPON_MEDIGUN:
				//case TF_WEAPON_LASER_POINTER: // we can psilent with the wrangler though probably with some hacks
					G::SilentAngles = false, G::PSilentAngles = true;
				}
			}
			bDone = true;
			break;
		}
	}
}