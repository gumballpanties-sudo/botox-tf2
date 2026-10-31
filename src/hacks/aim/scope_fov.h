#pragma once
#include "sdk.h"

// Sniper scope FOV scale (gap.md Unit F): TF2's scope zoom is one fixed FOV per zoom level,
// server-set into CBasePlayer::m_iFOV (netvar) and replicated once per zoom-state change --
// confirmed in tf_weapon_sniperrifle.cpp's Zoom()/ZoomIn()/ZoomOut(). Scaling it here is the
// same trick CTFWeaponBase::CanFireCriticalShot already uses on the same var (defs.cpp,
// temporarily forcing m_iFOV to 70 around one virtual call): client-only, self-affecting,
// overwritten back to the real server value automatically on the next zoom-state network
// update, so there's nothing to restore.
namespace ScopeFOV
{
	// Called every CreateMove (hooks.cpp). No-ops instantly when off or not zoomed.
	inline void Run(CTFPlayer* pLocal)
	{
		static int s_iLastWritten = -1;
		static int s_iBaseFOV = 0;

		if (!Vars::Aimbot::ScopeFOV::Enabled.Value || !pLocal || !pLocal->InCond(TF_COND_ZOOMED))
		{
			s_iLastWritten = -1;
			return;
		}

		int& iFOV = pLocal->m_iFOV();
		// A value that isn't what we last wrote means the server sent a fresh zoom FOV (zoom
		// level changed, or we just entered zoom) -- that's the new base to scale from.
		if (iFOV != s_iLastWritten)
			s_iBaseFOV = iFOV;

		iFOV = int(s_iBaseFOV * (Vars::Aimbot::ScopeFOV::Scale.Value / 100.f));
		s_iLastWritten = iFOV;
	}
}
