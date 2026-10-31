#pragma once
#include "sdk.h"

// Grenade/projectile lob-to-point (gap.md Unit F): the non-target-seeking half of the CS:GO
// grenade aimbot -- CAimbotProjectile already does target-SEEKING lead for live enemies; this
// is for an empty point under the crosshair (a doorway, a corner), auto-correcting the throw
// PITCH so the arc lands there, and drawing the predicted path. Yaw is never touched -- only
// the pitch most players can't eyeball for a lob.
class CGrenadeLob
{
private:
	bool KeyOK();
	// Runs the physics sim (F::ProjSim, same engine-accurate integrator the projectile aimbot
	// already uses) for a candidate pitch and returns where it lands/hits world geometry.
	bool SimulateLanding(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, float flPitch, float flYaw, Vec3& vLandPoint, std::vector<Vec3>* pPath = nullptr);
	// Secant-method pitch solve toward vPoint's horizontal distance. ponytail: assumes the
	// landing-distance-vs-pitch curve is monotonic near the current pitch, true for the direct/
	// moderate-lob throws this is meant for; an extreme vertical lob (near +-89) can make the
	// secant overshoot past the far arc branch and fail to converge, which just leaves the
	// player's own aim untouched (Run() no-ops on failure). Upgrade: bracket+bisect if that
	// shows up in practice.
	bool SolvePitch(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, const Vec3& vEye, const Vec3& vPoint, float& flPitch);

	bool m_bToggled = false;
	float m_flLastPitch = 0.f;
	Vec3 m_vLastPoint = {};

public:
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
};

ADD_FEATURE(CGrenadeLob, GrenadeLob);
