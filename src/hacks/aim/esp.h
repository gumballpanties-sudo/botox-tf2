#pragma once
#include "sdk.h"
#include "projsim.h"   // ProjectileInfo / F::ProjSim -- grenade/projectile path draw reuses this
#include "../esp/esp_vars.h"   // EF_COUNT -- EspPlayer::m_flags size
#include <array>
#include <cstdint>
#include <string>

// Player + building esp = botox hacks/visuals/players/players.cpp, ported 1:1 onto the TF2 SDK.
// Recorded into hacks/esp/esp_queue.h (botox m_draw_data) on the game thread, replayed by
// render.cpp RenderEspQueue in the ImGui frame. World entities (ammo, weapons, projectiles,
// paths, sound pings) stay on the vgui surface (H::Draw) they were written for.
// Vars: hacks/esp/esp_vars.h (plain primitives, dih side), not the aim ConfigVar tree.

// botox n_players::esp_player_t, ported 1:1. Everything CEsp::DrawPlayer reads, gathered from the
// entity (CEsp::DrawPlayers) or made up (esp preview, on a fake player). DrawPlayer must never read
// H::Entities / pLocal: every number it needs lives here. esp.h is aim-only (hacks/aim/*.cpp), so
// this struct is safe to define here rather than in a separate aim-only header -- esp_preview.cpp
// (the orchestrator's unit, also under hacks/aim/) includes esp.h to fill one of these.
struct EspPlayer
{
	float m_alpha = 1.f;
	Vec3 m_origin{}, m_mins{}, m_maxs{};   // world hull: ScreenBox tight (2d box) + the box_3d corners
	float m_viewYaw = 0.f;                 // radians; ScreenBox's tight trim (live: camera; preview: its own)

	std::string m_name;                    // truncated to 24 + "[bot] " prefix already applied
	int m_health = 0;
	float m_healthFactor = 0.f;            // animated health / max, clamped 0..1
	bool m_visible = false;                // visibility trace result (lazy: only run if a vis-colour feature asked)
	float m_distance = 0.f;
	int m_index = 0;                       // entindex: label declutter stack key (esp_declutter.h); 0 = labels fixed

	uint64_t m_steamId = 0;                // 0 = no avatar (bot / lookup failed)
	int m_weaponId = 0;                    // ETFWeaponType; 0 = no active weapon -> WeaponName() returns null
	bool m_hasAmmo = false;                // active weapon has a clip and the ammo bar is on (bakes in g_espAmmoBar: it also gates the m_flAmmo animation, a shared mutable state update, not just a draw toggle)
	float m_ammoFactor = 0.f;              // animated clip / max clip, clamped 0..1

	bool m_flags[EF_COUNT] = {};           // EspFlagBit row shown (all false if g_espFlags is off)

	const studiohdr_t* m_studio = nullptr; // null = no skeleton / hitboxes / head avatar this player
	const matrix3x4* m_bones = nullptr;    // caller-owned array (stack local in DrawPlayers); must outlive the DrawPlayer call

	bool m_hasHead = false;                // true only if bones resolved AND hitbox 0 (head) is valid
	Vec3 m_head{}, m_viewRight{};          // world head centre + camera right vector, for the head avatar's screen radius
	float m_headRadius = 0.f;
};

class CEsp
{
private:
	void DrawPlayers(CTFPlayer* pLocal);
	void DrawBuildings(CTFPlayer* pLocal);
	void DrawSoundArrows(CTFPlayer* pLocal);   // botox sound_arrows
	void DrawDamageNumbers();                  // botox damage_numbers
	void DrawHitHitboxes();                    // botox hit_hitboxes
	void DrawWorld(CTFPlayer* pLocal);           // botox edicts: dropped weapons (+ TF2 ammo packs)
	void DrawThrown(CTFPlayer* pLocal);          // botox edicts projectiles(): thrown objects name / icon
	void DrawGrenadePath(CTFPlayer* pLocal);     // botox grenade_path: your projectile weapon's trajectory
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
	// botox n_players::draw_player, split out of DrawPlayers so esp preview can feed it a fake
	// player. Pure draw: every EspQ:: emission below, no entity / H::Entities / pLocal reads.
	void DrawPlayer(const EspPlayer& p);
	// aim/events.cpp player_hurt (our hits only): damage numbers + on-hit hitboxes
	void OnPlayerHurt(CTFPlayer* pVictim, int iDamage);
};

ADD_FEATURE(CEsp, Esp);
