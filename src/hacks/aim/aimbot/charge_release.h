#pragma once
// Charge-shot release grace for a HOLD aim key (tests/chargerelease_test.cpp).
// Huntsman / sticky fire on the cmd where IN_ATTACK lifts. With the hold key on mouse1 that is
// the same cmd the key reads up, so the shot itself got no aim. Latch = key down at ANY cmd of
// the current charge (GetAsyncKeyState can read up one cmd before the engine drops IN_ATTACK).
// Latch clears once the charge ends (m_flChargeBeginTime back to 0 after the shot).
//   bCharging  : bow / sticky with m_flChargeBeginTime > 0
//   bKey       : KeyOK() this cmd
//   iAttacking : G::Attacking from the user's cmd (1 = this cmd fires)
// Returns true = run aim for this cmd although the key is up (the release pass).
inline bool ChargeReleasePass(bool& bLatch, bool bCharging, bool bKey, int iAttacking)
{
	bLatch = bCharging && (bLatch || bKey);
	return !bKey && bLatch && iAttacking == 1;
}
