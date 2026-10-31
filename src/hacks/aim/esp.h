#pragma once
#include "sdk.h"
#include "projsim.h"   // ProjectileInfo / F::ProjSim -- grenade/projectile path draw reuses this

// Unit C: player + world ESP. Vars are dih's own plain g_esp* globals (src/globals.h,
// src/hacks/menu/config.cpp), NOT the aim subsystem's ConfigVar tree -- this feature is
// visual/cosmetic per the project's two-config-system split (docs/port/tf2_features.md
// "Notes for the porter"). Lives under src/hacks/aim/ only to reach CTFPlayer/H::Draw/
// H::Entities/SDK::W2S/F::ProjSim without pulling the aim SDK into a dih-native TU (aim.h's
// own comment says that's deliberately avoided).
class CEsp
{
private:
	void DrawPlayers(CTFPlayer* pLocal);
	void DrawWorld(CTFPlayer* pLocal);
	void DrawProjectilePaths(CTFPlayer* pLocal);
	void DrawOofArrows(CTFPlayer* pLocal);
	void DrawSoundPings();      // Unit C2: CSoundEmitterSystem_EmitSound pings (visuals.cpp hook)
	void DrawDamageNumbers();   // Unit C2: player_hurt floating numbers (aim/events.cpp)

	int m_nDrawn = 0;   // boxes that passed the screen-rect check this frame (debug.log proof)

	void DrawBoxName(CBaseEntity* pEntity, const char* sName, Color_t tColor, float flDistU, bool bHealthBar = false, int iHealth = 0, int iMaxHealth = 0);

public:
	// Called once a frame from the aim subsystem's own IEngineVGui::Paint hook (src/hacks/
	// aim/hooks.cpp), right alongside F::Ticks.Draw/F::CritHack.Draw -- no new hook installed.
	void Draw(CTFPlayer* pLocal);
};

ADD_FEATURE(CEsp, Esp);
