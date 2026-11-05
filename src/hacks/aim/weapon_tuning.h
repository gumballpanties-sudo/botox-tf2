#pragma once
#include "sdk.h"

// Per-weapon-slot aim FOV (gap.md "MISSING per-TF2-weapon-slot granularity"). Narrow
// on purpose -- the 3 weapons the gap analysis names (sniper/minigun/scattergun), not a generic
// N-weapon table nobody asked for. FOV only: it's the knob that actually differs per weapon
// class (a sniper wants a tiny cone, a minigun a wide one); smooth (AssistStrength) and
// hitboxes have no per-call override point today (General::AssistStrength is read straight out
// of Vars:: at 4 call sites in aim_hitscan.cpp with no pWeapon in scope at 2 of them) -- adding
// one would mean widening CAimbot{Hitscan,Projectile,Melee}::Aim()'s signature for a knob this
// gap analysis didn't call for specifically. Add if that's wanted later.
namespace WeaponTuning
{
	// -1.f (no override) when disabled or the weapon isn't one of the 3 -- the exact sentinel
	// CAimbotGlobal::PlayerBoneInFOV's flFOVOverride parameter already accepts.
	inline float GetFOV(CTFWeaponBase* pWeapon)
	{
		if (!Vars::Aimbot::WeaponTuning::Enabled.Value || !pWeapon)
			return -1.f;

		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_SNIPERRIFLE:
		case TF_WEAPON_SNIPERRIFLE_DECAP:
		case TF_WEAPON_SNIPERRIFLE_CLASSIC:
			return Vars::Aimbot::WeaponTuning::SniperFOV.Value;
		case TF_WEAPON_MINIGUN:
			return Vars::Aimbot::WeaponTuning::MinigunFOV.Value;
		case TF_WEAPON_SCATTERGUN:
			return Vars::Aimbot::WeaponTuning::ScattergunFOV.Value;
		default:
			return -1.f;
		}
	}
}
