#pragma once
#include "sdk.h"
#include "projsim.h"   // ProjectileInfo / F::ProjSim -- grenade/projectile path draw reuses this
#include <array>

// Player + building esp = botox hacks/visuals/players/players.cpp, ported 1:1 onto the TF2 SDK.
// Recorded into hacks/esp/esp_queue.h (botox m_draw_data) on the game thread, replayed by
// render.cpp RenderEspQueue in the ImGui frame. World entities (ammo, weapons, projectiles,
// paths, sound pings) stay on the vgui surface (H::Draw) they were written for.
// Vars: hacks/esp/esp_vars.h (plain primitives, dih side), not the aim ConfigVar tree.
class CEsp
{
private:
	void DrawPlayers(CTFPlayer* pLocal);
	void DrawBuildings(CTFPlayer* pLocal);
	void DrawSoundArrows(CTFPlayer* pLocal);   // botox sound_arrows
	void DrawDamageNumbers();                  // botox damage_numbers
	void DrawHitHitboxes();                    // botox hit_hitboxes
	void DrawWorld(CTFPlayer* pLocal);
	void DrawProjectilePaths(CTFPlayer* pLocal);
	void DrawSoundPings();

	// botox damage_number_t / hit_hitboxes_t; game thread only (events + paint)
	struct DamageNumber { Vec3 vOrigin; int iDamage = 0, iVictim = 0; float flTime = 0.f; };
	struct HitBoxes { std::vector<std::array<Vec3, 8>> vBoxes; float flTime = 0.f; };
	std::vector<DamageNumber> m_vDamage;
	std::vector<HitBoxes> m_vHitBoxes;

	// botox m_backup_player_data m_animated_health / m_ammo
	float m_flAnimHealth[65] = {};
	float m_flAmmo[65] = {};
	float m_flAnimHealthBld[2048] = {};   // buildings, by entindex

	int m_nDrawn = 0;   // boxes drawn this frame (debug.log proof)

public:
	// aim/hooks.cpp IEngineVGui::Paint, game thread, once a frame
	void Draw(CTFPlayer* pLocal);
	// aim/events.cpp player_hurt (our hits only): damage numbers + on-hit hitboxes
	void OnPlayerHurt(CTFPlayer* pVictim, int iDamage);
};

ADD_FEATURE(CEsp, Esp);
