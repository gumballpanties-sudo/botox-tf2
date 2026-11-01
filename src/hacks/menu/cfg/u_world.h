#pragma once
// unit world vars: `inline bool g_x = false;` style, defaults = botox visual_variables.inc / variables.h.
// Primitive types only: included by globals.h AND directly by world_fx3.cpp.

// ---- world group (botox variables.h :660-765) ----
inline float g_wdWorldFov = 0.f;                                   // m_world_fov: ADDED to the view fov (-30..60)

inline float g_wdSunSpeed = 0.f;                                   // m_sun_angle_speed, deg/s of yaw drift
inline float g_wdSunShadowDist = 0.f;                              // m_sun_angle_shadow_distance, 0 = map's own

inline bool  g_wdFlipGunOtherSide = false;                         // m_flip_world_mirror_hand

inline bool  g_wdDlightLocal = false;                              // m_dlight_local
inline float g_wdDlightLocalColor[4] = { 120 / 255.f, 160 / 255.f, 1.f, 1.f };
inline int   g_wdDlightBrightness = 5;                             // m_dlight_brightness (dlight exponent)

inline int   g_wdRagdollGravity = 0;                               // m_ragdoll_gravity: off / away / fly up
inline float g_wdRagdollGravityStrength = 1.f;

inline float g_wdWorldModBright = 100.f;                           // m_world_modulation_brightness (%)
inline float g_wdPropModBright  = 100.f;                           // m_prop_modulation_brightness
inline bool  g_wdSkyMod = false;                                   // m_skybox_modulation
inline float g_wdSkyModColor[4] = { 1.f, 1.f, 1.f, 1.f };
inline float g_wdSkyModBright = 100.f;

inline bool  g_wdWorldTex = false;                                 // m_world_texture
inline int   g_wdWorldTexPreset[7] = {};                           // per category, world_fx3_math.h k_texPresets index
inline char  g_wdWorldTexCustom[7][128] = {};                      // "custom": materials/ path, no .vtf
inline bool  g_wdWorldTexDecals = false;                           // m_world_texture_remove_decals
inline bool  g_wdPropTex = false;                                  // m_prop_texture
inline int   g_wdPropTexPreset = 0;
inline char  g_wdPropTexCustom[128] = {};

inline bool  g_wdTracers = false;                                 // m_bullet_tracers
inline bool  g_wdTracersLocal = true;
inline bool  g_wdTracersEnemy = false;
inline bool  g_wdTracersTeam  = false;
inline float g_wdTracersLocalColor[4] = { 150 / 255.f, 130 / 255.f, 1.f, 1.f };
inline float g_wdTracersEnemyColor[4] = { 1.f, 80 / 255.f, 80 / 255.f, 1.f };
inline float g_wdTracersTeamColor[4]  = { 80 / 255.f, 160 / 255.f, 1.f, 1.f };
inline int   g_wdTracersSprite = 0;
inline float g_wdTracersLife  = 3.f;
inline float g_wdTracersWidth = 2.f;

inline bool  g_wdTrail = false;                                    // m_movement_trail
inline float g_wdTrailColor[4]       = { 150 / 255.f, 130 / 255.f, 1.f, 1.f };
inline float g_wdTrailCrouchColor[4] = { 1.f, 130 / 255.f, 200 / 255.f, 1.f };
inline bool  g_wdTrailRainbow = false;
inline bool  g_wdTrailAirOnly = true;
inline int   g_wdTrailSprite = 1;
inline float g_wdTrailLife  = 2.5f;
inline float g_wdTrailWidth = 3.f;

inline bool  g_wdImpacts = false;                                  // m_bullet_impacts (client colour only: TF2 has no server feed)
inline float g_wdImpactsClientColor[4] = { 1.f, 0.f, 0.f, 127 / 255.f };
inline float g_wdImpactsSize = 2.f;
inline float g_wdImpactsDuration = 4.f;

inline bool  g_wdHitMarker = false;                                // m_world_hit_marker
inline float g_wdHitMarkerColor[4] = { 1.f, 1.f, 1.f, 1.f };
inline float g_wdHitMarkerSize = 6.f;
inline float g_wdHitMarkerDuration = 1.f;

// ---- removals group (botox variables.h :710-730) ----
// m_disable_post_processing = unit screen's g_sfxDisablePostProcessing (u_screen.h, world_fx.cpp)
inline bool  g_wdNo3dSky = false;                                 // m_remove_3d_skybox
inline bool  g_wdNoHud = false;                                    // m_remove_hud_elements
inline constexpr int k_wdHudCount = 14;                            // world_fx3_math.h k_hudElems
inline bool  g_wdNoHudElems[k_wdHudCount] = {};                    // m_removed_hud_elements

// ---- viewmodel group (botox variables.h :1008-1031) ----
inline float g_wdVmRoll = 0.f;                                     // m_viewmodel_roll
inline bool  g_wdVmFovEnable = false;                              // m_viewmodel_fov_enable
inline float g_wdVmFov = 68.f;                                     // m_viewmodel_fov
