#pragma once
#include "../../globals/globals.h"

// botox kill_effects.cpp 1:1 port (hacks/misc/kill_effects.cpp, csgo-sdk). "player_death" is a
// confirmed TF2 event (source-sdk-2013-master tf_player.cpp). botox's own rule: never draws on
// the LOCAL player's own death (skipped at the top of on_death_particles) -- this is feedback
// about OTHER deaths, now possible world-anchored at the victim's origin via render.h's
// W2S_Project (pass 1 could only do a screen-space local effect; that version is gone).
//
// Styles (g_dpStyle, botox's m_death_particles_type):
//   0 blood      -> real engine particle, CTE_DispatchEffect("ParticleEffect", ...) "blood_spray_red_01"
//                   (each engine style tries a name list, botox try_names(); unprecached -> blood)
//   1 sparks     -> CS:GO-only IEffects001 slot, no verified TF2 analog -> falls back to blood
//   2 confetti   -> engine particle "bday_confetti"
//   3 smoke      -> own billboard puffs (no hitbox resolver here -> origin + stacked offsets,
//                   botox's OWN no-bones fallback path, same as its code when bones are missing)
//   4 explosion  -> engine particle "ExplosionCore_Wall"
//   5 soul       -> own billboard particles, 4 sub-styles (fountain/rise/burst/converge)
//   6 balls      -> own billboard bouncy balls, attacker==local only (botox's own gate).
//                   No hull trace here: floor-clamped at spawn Z (ponytail: real IEngineTrace
//                   bounce needs a thread-safety read this pass chose not to risk; upgrade path
//                   = TraceLine the same way movement.cpp/dof.cpp already do, from the game
//                   thread, if an uneven-floor bounce is ever worth the risk).
//   7 melt       -> BLOCKED. botox's real version is a worker-thread fluid sim + marching-cubes
//                   mesh + model vvd/vtx skin + 4 compiled shaders (11 revisions, still flagged
//                   UNTESTED in its own project after all of them). Porting that 1:1 this pass
//                   is not a safe single-session change -- falls back to blood.
//   8 mc         -> minecraft crit x's + xp orbs, botox's own fallback OBB (no collideable found)
void KillEffects_OnDeath(int victimIdx, int attackerIdx, bool localDied);
void RenderKillEffects();   // ticks + draws the custom (non-engine) particle styles
