#pragma once
#include "../../globals/globals.h"

// Port round 11-01, botox world/removals/viewmodel rows that need new engine paths.
//   bullets   CTFPlayer::FireBullet (Amalgam Hooks/CTFPlayer_FireBullet.cpp sig, 1 hit in client.dll)
//             -> tracer beams (IViewRenderBeams, Amalgam ViewRenderBeams.h sig) + client impact boxes;
//             player_hurt (game_events.cpp) -> world hit marker. TF2 has no bullet_impact event.
//   trail     movement trail beams, per frame from OverrideView (main thread).
//   dlight    enemies + local player, per frame (botox edicts.cpp dlights 1:1).
//   lighting  modulation: dlights on lamp static props (CStaticPropMgr::DrawStaticProps records them)
//             + light glow/bloom materials tinted through the world modulation walk (group 4).
//   ragdoll   DT_TFRagdoll::m_vecForce recv proxy swap (botox kill_effects.cpp ragdoll_force_proxy).
//   hud       remove hud elements: IPanel::PaintTraverse skipped for the named TF2 vgui panels.
//   cvars     r_3dsky, viewmodel_fov (raw value poke, the string -- what config.cfg saves -- is
//             never written).

void WorldFx3_Install();                    // PollThread (from WorldFx2_InstallHooks)
void WorldFx3_Tick();                       // 100ms tick (from WorldFx2_Tick)
void WorldFx3_Frame(const float eye[3]);    // main thread, once a frame (from h_OverrideView, view origin)
void WorldFx3_LevelShutdown();              // game thread, map change/disconnect (h_FlipLevelShutdown)
void WorldFx3_OnPlayerHurt(int victimIdx);  // player_hurt, attacker == local (game_events.cpp)
bool WorldFx3_SkipPanel(void* panels, uintptr_t vpanel);   // h_PaintTraverse: true = don't paint it
void RenderWorldFx3();                      // BuildImGuiFrame: impact boxes + world hit markers
void WorldFx3_Release();                    // eject: proxy + cvars back
