#pragma once
// esp vars: `inline bool g_x = false;` style, defaults = botox visual_variables.inc / variables.h.
// Included by globals.h AND directly by the aim TU (aim/visuals/esp.cpp, aim/visuals/chams.cpp, aim/player_stencil.cpp).

// ---- players group (botox variables.h :511-558) ----
inline bool  g_esxStencil = false;                                      // m_players_stencil
inline float g_esxStencilVisColor[4]   = { 1.f, 1.f, 1.f, 1.f };
inline float g_esxStencilInvisColor[4] = { 114 / 255.f, 155 / 255.f, 221 / 255.f, 1.f };
inline float g_esxStencilThickness = 1.5f;
inline bool  g_esxStencilWave = false;                                  // m_players_stencil_wave
inline float g_esxStencilWaveColor[4] = { 1.f, 1.f, 1.f, 1.f };
inline float g_esxStencilWaveSpeed = 0.5f;
inline float g_esxStencilWaveSize = 0.35f;
inline float g_esxStencilWaveStrength = 0.8f;
inline bool  g_esxStencilWavePlayer = true;                             // wave rides each player's body, not the screen
inline float g_esxStencilWaveAngle = 0.f;                               // deg, turns the wave direction

inline bool  g_esxInnerGlow = false;                                    // m_inner_glow
inline float g_esxInnerGlowVisColor[4]   = { 1.f, 1.f, 1.f, 200 / 255.f };
inline float g_esxInnerGlowInvisColor[4] = { 114 / 255.f, 155 / 255.f, 221 / 255.f, 200 / 255.f };
inline float g_esxInnerGlowSize = 12.f;
inline float g_esxInnerGlowIntensity = 1.5f;
inline bool  g_esxInnerGlowBones = true;
inline float g_esxInnerGlowBoneWidth = 50.f;

// ---- edicts group (botox variables.h :628-659) ----
inline bool  g_esxDropped = false;                                      // m_dropped_weapons
inline bool  g_esxDroppedBox = false;
inline bool  g_esxDroppedBoxCorner = false;
inline bool  g_esxDroppedBoxOutline = false;
inline float g_esxDroppedBoxColor[4] = { 1.f, 1.f, 1.f, 1.f };
inline float g_esxDroppedBoxOutlineColor[4] = { 0.f, 0.f, 0.f, 1.f };
inline bool  g_esxDroppedName = false;
inline float g_esxDroppedNameColor[4] = { 1.f, 1.f, 1.f, 1.f };
inline bool  g_esxDroppedGlow = false;                                  // m_dropped_weapons_glow (aim/visuals/chams.cpp model glow)
inline float g_esxDroppedGlowVisColor[4]   = { 1.f, 1.f, 1.f, 153 / 255.f };
inline float g_esxDroppedGlowInvisColor[4] = { 114 / 255.f, 155 / 255.f, 221 / 255.f, 153 / 255.f };

inline bool  g_esxThrown = false;                                       // m_thrown_objects
inline bool  g_esxThrownName = false;
inline float g_esxThrownNameColor[4] = { 1.f, 1.f, 1.f, 1.f };

inline bool  g_esxGrenadePath = false;                                  // m_grenade_path
inline float g_esxGrenadePathColor[4]         = { 90 / 255.f, 160 / 255.f, 1.f, 1.f };
inline float g_esxGrenadePathBounceColor[4]   = { 1.f, 1.f, 1.f, 1.f };
inline float g_esxGrenadePathDetonateColor[4] = { 1.f, 90 / 255.f, 90 / 255.f, 1.f };
inline float g_esxGrenadePathThickness = 1.5f;
inline bool  g_esxGrenadePathRadius = true;

// TF2 extra edict row: ammo packs / health kits, same look as dropped weapons
inline bool  g_esxAmmo = false;
inline float g_esxAmmoColor[4] = { 1.f, 0.9f, 0.47f, 1.f };

// ---- viewmodel glow / stencil (botox m_viewmodel_glow* / m_viewmodel_stencil*, world > viewmodel). TF2
// parts: weapon (c_ attachment) + arms (c_ arms / classic v_ model); botox's gloves + sleeves = the arms.
// part off = no glow there, its coverage still stops the other part's ----
inline bool  g_vmGlow = false;
inline bool  g_vmGlowWeapon = true;
inline float g_vmGlowWeaponColor[4]      = { 224 / 255.f, 175 / 255.f, 86 / 255.f, 153 / 255.f };
inline float g_vmGlowWeaponOuterColor[4] = { 0.f, 0.f, 0.f, 1.f };
inline bool  g_vmGlowArms = true;
inline float g_vmGlowArmsColor[4]        = { 114 / 255.f, 155 / 255.f, 221 / 255.f, 153 / 255.f };
inline float g_vmGlowArmsOuterColor[4]   = { 0.f, 0.f, 0.f, 1.f };
inline bool  g_vmGlowGradient = false;
inline float g_vmGlowGradientInner = 60.f;
inline bool  g_vmGlowNormalBlend = false;
inline float g_vmGlowThickness = 15.f;
inline bool  g_vmGlowWave = false;
inline float g_vmGlowWaveColor[4] = { 1.f, 1.f, 1.f, 1.f };
inline float g_vmGlowWaveSpeed = 0.5f;
inline float g_vmGlowWaveSize = 0.35f;
inline float g_vmGlowWaveStrength = 0.8f;
inline float g_vmGlowWaveAngle = 0.f;
inline bool  g_vmGlowPulse = false;
inline float g_vmGlowPulseSpeed = 1.f;
inline float g_vmGlowPulseMin = 35.f;
inline bool  g_vmStencil = false;
inline bool  g_vmStencilWeapon = true;
inline float g_vmStencilWeaponColor[4] = { 1.f, 1.f, 1.f, 1.f };
inline bool  g_vmStencilArms = true;
inline float g_vmStencilArmsColor[4]   = { 114 / 255.f, 155 / 255.f, 221 / 255.f, 1.f };
inline float g_vmStencilThickness = 1.5f;
inline bool  g_vmStencilWave = false;
inline float g_vmStencilWaveColor[4] = { 1.f, 1.f, 1.f, 1.f };
inline float g_vmStencilWaveSpeed = 0.5f;
inline float g_vmStencilWaveSize = 0.35f;
inline float g_vmStencilWaveStrength = 0.8f;
inline float g_vmStencilWaveAngle = 0.f;
