#include "aim_global.h"

#include "aimbot.h"
#include "ticks.h"
#include "aim_fovreject.h"   // bounding-sphere FOV reject (pure, gate-tested)

void CAimbotGlobal::SortTargets(std::vector<Target_t>& vTargets, int iMethod)
{	// Sort by preference
	std::sort(vTargets.begin(), vTargets.end(), [&](const Target_t& a, const Target_t& b) -> bool
		{
			switch (iMethod)
			{
			case Vars::Aimbot::General::TargetSelectionEnum::FOV: return a.m_flFOVTo < b.m_flFOVTo;
			case Vars::Aimbot::General::TargetSelectionEnum::Distance: return a.m_flDistTo < b.m_flDistTo;
			default: return false;
			}
		});
}

void CAimbotGlobal::SortPriority(std::vector<Target_t>& vTargets)
{	// Sort by priority. STABLE, because this always runs straight after SortTargets and every
	// target's priority is currently 0 (CAimbotGlobal::GetPriority) -- an unstable sort with a
	// comparator that is false for every pair is free to hand back any permutation it likes, and
	// the order it would be destroying is the distance order the knife picks its first swing off.
	std::stable_sort(vTargets.begin(), vTargets.end(), [&](const Target_t& a, const Target_t& b) -> bool
		{
			return a.m_nPriority > b.m_nPriority;
		});
}

// Hitboxes can sit slightly outside the collision bbox (arms out, a taunt), so the reject
// sphere below is padded before it is allowed to say no. Deliberately generous: this test
// exists to drop the far side of the map, not to be tight.
static constexpr float k_fovRejectScale = 1.5f;
static constexpr float k_fovRejectPad   = 16.f;

// this won't prevent shooting bones outside of fov
bool CAimbotGlobal::PlayerBoneInFOV(CTFPlayer* pTarget, Vec3 vLocalPos, Vec3 vLocalAngles, float& flFOVTo, Vec3& vPos, Vec3& vAngleTo, int iHitboxes, float flFOVOverride)
{
	const float flFOV = flFOVOverride >= 0.f ? flFOVOverride : Vars::Aimbot::General::AimFOV.Value;

	// ---- BOUNDING-SPHERE REJECT, BEFORE ANY BONE WORK -------------------------------------
	// This runs for EVERY enemy on the server, every command, from all three target gathers
	// (hitscan/projectile/melee). SetupBones below is a full bone rebuild (see the mask note),
	// and on a target whose every hitbox is 60 degrees off-screen the whole rebuild is thrown
	// away. Bound the player by a SPHERE and ask whether the sphere's nearest edge is still
	// outside the FOV cone: a sphere, unlike the bbox, cannot be rotated out from under the
	// test, so nothing that would have passed the loop below can be rejected here.
	{
		const Vec3 vMins = pTarget->m_vecMins(), vMaxs = pTarget->m_vecMaxs();
		const Vec3 vCenter = pTarget->m_vecOrigin() + (vMins + vMaxs) / 2;
		const float flRadius = (vMaxs - vMins).Length() / 2 * k_fovRejectScale + k_fovRejectPad;
		const float flDist = vLocalPos.DistTo(vCenter);
		const float flFovToCenter = Math::CalcFov(vLocalAngles, Math::CalcAngle(vLocalPos, vCenter));
		if (aimfov::SphereOutsideFov(flFovToCenter, flDist, flRadius, flFOV))
			return false;
	}

	// static, not a local: matrix3x4[MAXSTUDIOBONES] is 6144 bytes of stack per call and this
	// is called once per enemy per command. Single-threaded (CreateMove), consumed before return
	static matrix3x4 aBones[MAXSTUDIOBONES];
	// ---- BONE_USED_BY_HITBOX, NOT BONE_USED_BY_ANYTHING ---------------------------------
	// c_baseanimating.cpp:2911 hits the bone cache only when
	// `(GetReadableBones() & boneMask) == boneMask`. BONE_USED_BY_ANYTHING is 0x0007FF00, all
	// eleven "used by" bits, so it is NEVER a cache hit: every call was a full rebuild
	// (StandardBlendingRules + CalculateIKLocks + BuildTransformations).
	// Worse, c_baseanimating.cpp:2904 does `m_iAccumulatedBoneMask |= boneMask` and the next
	// frame does `boneMask |= m_iPrevBoneMask` -- so asking for ANYTHING once upgraded the
	// GAME'S OWN render-path SetupBones for that player to full-bone setup, permanently. That
	// cost frames nowhere near the aim path.
	// BONE_USED_BY_HITBOX (0x100, studio.h:415) is a subset the renderer already builds for any
	// player on screen, and it is exactly what every read here needs: every consumer of this
	// array indexes it as aBones[pBox->bone], hitbox bones and nothing else.
	if (!pTarget->SetupBones(aBones, MAXSTUDIOBONES, BONE_USED_BY_HITBOX, I::GlobalVars->curtime))
		return false;

	// ---- THE HITBOX SET, RESOLVED ONCE --------------------------------------------------
	// GetNumOfHitboxes() was the loop CONDITION and GetHitboxCenter() ran inside the body, and
	// each of them re-walks GetModel() -> GetStudiomodel() -> pHitboxSet() -> pHitbox()
	// (defs.cpp:7-70). Two of those are virtual calls into the engine and GetStudiomodel goes
	// through the MDL cache. ~18 hitboxes x ~7 lookups x every enemy = thousands per command,
	// for a pointer that cannot change inside one loop. Resolve it once, exactly as
	// CAimbotHitscan::CanHit already does (aim_hitscan.cpp, its own hitbox loop).
	auto pModel = pTarget->GetModel();
	if (!pModel) return false;
	auto pHDR = I::ModelInfoClient->GetStudiomodel(pModel);
	if (!pHDR) return false;
	auto pSet = pHDR->pHitboxSet(pTarget->m_nHitboxSet());
	if (!pSet) return false;

	float flMinFOV = 180.f;
	for (int nHitbox = 0; nHitbox < pSet->numhitboxes; nHitbox++)
	{
		if (!IsHitboxValid(pTarget, nHitbox, iHitboxes))
			continue;

		auto pBox = pSet->pHitbox(nHitbox);
		if (!pBox)
			continue;

		Vec3 vCurPos; Math::VectorTransform((pBox->bbmin + pBox->bbmax) / 2, aBones[pBox->bone], vCurPos);
		Vec3 vCurAngleTo = Math::CalcAngle(vLocalPos, vCurPos);
		float flCurFOVTo = Math::CalcFov(vLocalAngles, vCurAngleTo);

		if (flCurFOVTo < flMinFOV)
		{
			vPos = vCurPos;
			vAngleTo = vCurAngleTo;
			flFOVTo = flMinFOV = flCurFOVTo;
		}
	}

	return flMinFOV < flFOV;
}

bool CAimbotGlobal::IsHitboxValid(CBaseEntity* pEntity, int nHitbox, int iHitboxes)
{
	switch (pEntity->GetHitboxToBase(nHitbox))
	{
	case -1: return true;
	case HITBOX_HEAD: return iHitboxes & Vars::Aimbot::Hitscan::HitboxesEnum::Head;
	case HITBOX_SPINE0:
	case HITBOX_SPINE1:
	case HITBOX_SPINE2:
	case HITBOX_SPINE3: return iHitboxes & Vars::Aimbot::Hitscan::HitboxesEnum::Body;
	case HITBOX_PELVIS: return iHitboxes & Vars::Aimbot::Hitscan::HitboxesEnum::Pelvis;
	case HITBOX_LEFT_UPPERARM:
	case HITBOX_LEFT_FOREARM:
	case HITBOX_LEFT_HAND:
	case HITBOX_RIGHT_UPPERARM:
	case HITBOX_RIGHT_FOREARM:
	case HITBOX_RIGHT_HAND: return iHitboxes & Vars::Aimbot::Hitscan::HitboxesEnum::Arms;
	case HITBOX_LEFT_THIGH:
	case HITBOX_LEFT_CALF:
	case HITBOX_LEFT_FOOT:
	case HITBOX_RIGHT_THIGH:
	case HITBOX_RIGHT_CALF:
	case HITBOX_RIGHT_FOOT: return iHitboxes & Vars::Aimbot::Hitscan::HitboxesEnum::Legs;
	}

	return false;
}

bool CAimbotGlobal::IsHitboxValid(int nHitbox, int iHitboxes)
{
	switch (nHitbox)
	{
	case BOUNDS_HEAD: return iHitboxes & Vars::Aimbot::Projectile::HitboxesEnum::Head;
	case BOUNDS_BODY: return iHitboxes & Vars::Aimbot::Projectile::HitboxesEnum::Body;
	case BOUNDS_FEET: return iHitboxes & Vars::Aimbot::Projectile::HitboxesEnum::Feet;
	}

	return false;
}

bool CAimbotGlobal::ShouldMultipoint(CBaseEntity* pEntity, int nHitbox, int iHitboxes)
{
	if (Vars::Aimbot::Hitscan::MultipointScale.Value <= 0.f)
		return false;

	if (!iHitboxes)
		return true;

	switch (pEntity->GetHitboxToBase(nHitbox))
	{
	case HITBOX_HEAD: return iHitboxes & Vars::Aimbot::Hitscan::HitboxesEnum::Head;
	case HITBOX_SPINE0:
	case HITBOX_SPINE1:
	case HITBOX_SPINE2:
	case HITBOX_SPINE3:
	case HITBOX_PELVIS: return iHitboxes & Vars::Aimbot::Hitscan::HitboxesEnum::Body;
	case HITBOX_LEFT_UPPERARM:
	case HITBOX_LEFT_FOREARM:
	case HITBOX_LEFT_HAND:
	case HITBOX_RIGHT_UPPERARM:
	case HITBOX_RIGHT_FOREARM:
	case HITBOX_RIGHT_HAND: return iHitboxes & Vars::Aimbot::Hitscan::HitboxesEnum::Arms;
	case HITBOX_LEFT_THIGH:
	case HITBOX_LEFT_CALF:
	case HITBOX_LEFT_FOOT:
	case HITBOX_RIGHT_THIGH:
	case HITBOX_RIGHT_CALF:
	case HITBOX_RIGHT_FOOT: return iHitboxes & Vars::Aimbot::Hitscan::HitboxesEnum::Legs;
	}

	return false;
}

bool CAimbotGlobal::ShouldIgnore(CBaseEntity* pEntity, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iIgnoreOverride)
{
	// Resolve which Ignore bitmask to honour: the caller's override (e.g. the knife Backstab ignore)
	// or, when -1, the General aim Ignore dropdown. Same IgnoreEnum bit layout either way.
	const int iIgnore = iIgnoreOverride >= 0 ? iIgnoreOverride : Vars::Aimbot::General::Ignore.Value;

	if (pEntity->IsDormant())
		return true;

	if (auto pGameRules = I::TFGameRules())
	{
		if (pGameRules->m_bTruceActive() && (FriendlyFire() || pLocal->m_iTeamNum() != pEntity->m_iTeamNum()))
			return true;
	}

	switch (pEntity->GetClassID())
	{
	case ETFClassID::CTFPlayer:
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (pPlayer == pLocal || !pPlayer->IsAlive() || pPlayer->IsAGhost())
			return true;

		if (!FriendlyFire() && pLocal->m_iTeamNum() == pEntity->m_iTeamNum())
			return false;

		if (iIgnore & Vars::Aimbot::General::IgnoreEnum::Invulnerable && pPlayer->IsInvulnerable() && SDK::AttribHookValue(0, "crit_forces_victim_to_laugh", pWeapon) <= 0
			|| iIgnore & Vars::Aimbot::General::IgnoreEnum::Invisible && pPlayer->IsInvisible(Vars::Aimbot::General::IgnoreInvisible.Value / 100.f)
			|| iIgnore & Vars::Aimbot::General::IgnoreEnum::DeadRinger && pPlayer->m_bFeignDeathReady()
			|| iIgnore & Vars::Aimbot::General::IgnoreEnum::Taunting && pPlayer->IsTaunting()
			|| iIgnore & Vars::Aimbot::General::IgnoreEnum::Disguised && pPlayer->InCond(TF_COND_DISGUISED))
			return true;
		if (iIgnore & Vars::Aimbot::General::IgnoreEnum::Vaccinator)
		{
			switch (G::PrimaryWeaponType)
			{
			case EWeaponType::HITSCAN:
				if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST) && SDK::AttribHookValue(0, "mod_pierce_resists_absorbs", pWeapon) != 0)
					return true;
				break;
			case EWeaponType::PROJECTILE:
				switch (pWeapon->GetWeaponID())
				{
				case TF_WEAPON_FLAMETHROWER:
				case TF_WEAPON_FLAREGUN:
					if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_FIRE_RESIST))
						return true;
					break;
				case TF_WEAPON_COMPOUND_BOW:
					if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST))
						return true;
					break;
				default:
					if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BLAST_RESIST))
						return true;
				}
			}
		}

		return false;
	}
	case ETFClassID::CObjectSentrygun:
	case ETFClassID::CObjectDispenser:
	case ETFClassID::CObjectTeleporter:
	{
		auto pBuilding = pEntity->As<CBaseObject>();
		if (pLocal->m_iTeamNum() == pBuilding->m_iTeamNum())
			return false;

		if (!(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Sentry) && pBuilding->IsSentrygun()
			|| !(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Dispenser) && pBuilding->IsDispenser()
			|| !(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Teleporter) && pBuilding->IsTeleporter())
			return true;

		// A building is judged by its type alone -- its owner does not change the decision.
		return false;
	}
	case ETFClassID::CTFGrenadePipebombProjectile:
	{
		auto pProjectile = pEntity->As<CTFGrenadePipebombProjectile>();

		if (!(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Stickies))
			return true;

		if (pLocal->m_iTeamNum() == pEntity->m_iTeamNum())
			return true;

		if (pProjectile->m_iType() != TF_GL_MODE_REMOTE_DETONATE || !pProjectile->m_bTouched())
			return true;

		return false;
	}
	case ETFClassID::CTFBaseBoss:
	case ETFClassID::CTFTankBoss:
	case ETFClassID::CMerasmus:
	case ETFClassID::CEyeballBoss:
	case ETFClassID::CHeadlessHatman:
	case ETFClassID::CZombie:
	{
		if (!(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::NPCs))
			return true;

		if (pEntity->GetClassID() == ETFClassID::CEyeballBoss
			? pLocal->m_iTeamNum() != TF_TEAM_HALLOWEEN
			: pLocal->m_iTeamNum() == pEntity->m_iTeamNum())
			return true;

		return false;
	}
	case ETFClassID::CTFGenericBomb:
	case ETFClassID::CTFPumpkinBomb:
	{
		if (!(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Bombs))
			return true;

		if (!ValidBomb(pLocal, pWeapon, pEntity))
			return true;

		return false;
	}
	}

	return true;
}

int CAimbotGlobal::GetPriority(int iIndex)
{
	return 0; // every target has the same priority
}

bool CAimbotGlobal::FriendlyFire()
{
	static auto mp_friendlyfire = H::ConVars.FindVar("mp_friendlyfire");
	return mp_friendlyfire->GetBool();
}

bool CAimbotGlobal::ShouldAim()
{
	switch (Vars::Aimbot::General::AimType.Value)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
	case Vars::Aimbot::General::AimTypeEnum::Silent:
		if (!G::CanPrimaryAttack && !G::Reloading && !F::Ticks.IsTimingUnsure())
			return false;
	}

	return true;
}

// will not predict for projectile weapons
bool CAimbotGlobal::ValidBomb(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pBomb)
{
	Vec3 vOrigin = pBomb->m_vecOrigin();

	CBaseEntity* pEntity;
	for (CEntitySphereQuery sphere(vOrigin, 300.f);
		pEntity = sphere.GetCurrentEntity();
		sphere.NextEntity())
	{
		if (pEntity == pLocal || pEntity->IsPlayer() && (!pEntity->As<CTFPlayer>()->IsAlive() || pEntity->As<CTFPlayer>()->IsAGhost())
			|| !FriendlyFire() && pEntity->m_iTeamNum() == pLocal->m_iTeamNum())
			continue;

		Vec3 vPos; pEntity->m_Collision()->CalcNearestPoint(vOrigin, &vPos);
		if (vOrigin.DistTo(vPos) > 300.f)
			continue;

		if (pEntity->IsPlayer() || pEntity->IsBuilding() || pEntity->IsNPC())
		{
			if (ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			if (!SDK::VisPosCollideable(pBomb, pEntity, vOrigin, pEntity->IsPlayer() ? pEntity->m_vecOrigin() + pEntity->As<CTFPlayer>()->GetViewOffset() : pEntity->GetCenter(), MASK_SHOT))
				continue;

			return true;
		}
	}

	return false;
}