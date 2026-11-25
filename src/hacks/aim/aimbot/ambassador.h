#pragma once
// Ambassador head crit range (tests/ambassador_test.cpp). Server CTFRevolver::CanFireCriticalShot
// (sdk shared/tf/tf_weapon_revolver.cpp:107): owner-to-target abs origin 2D dist > 1200 -> no crit,
// a head hit there is plain damage (player_hurt custom=0). Head aim past it = smaller target, same dmg.
// Crit boost skips the check, but then every hit crits: head gains nothing either.
inline bool AmbassadorHeadCritRange(float dx, float dy)
{
	return dx * dx + dy * dy <= 1200.f * 1200.f;
}
