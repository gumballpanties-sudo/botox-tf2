#pragma once
// unit hud vars: `inline bool g_x = false;` style, defaults = botox visual_variables.inc / variables.h.
// botox menu_tab_visuals.cpp "hud" group. Colours = float[4] RGBA 0..1 (botox c_color / 255).

// sniper crosshair (botox m_sniper_crosshair_*)
inline bool  g_hudSniperCh              = false;
inline int   g_hudSniperChStyle         = 0;   // clarity / kamidere / interwebz / custom
inline float g_hudSniperChColor[4]      = { 1.f, 1.f, 1.f, 1.f };
inline float g_hudSniperChOutlineColor[4] = { 0.f, 0.f, 0.f, 1.f };
inline float g_hudSniperChDotColor[4]   = { 1.f, 0.f, 0.f, 1.f };
inline float g_hudSniperChIwSize        = 1.7f;
inline int   g_hudSniperChIwThick       = 2;
inline int   g_hudSniperChIwGap         = 3;
inline bool  g_hudSniperChIwArmCorners  = false;
inline bool  g_hudSniperChIwDotCorners  = false;
inline float g_hudSniperChSize          = 5.f;
inline float g_hudSniperChThick         = 0.5f;
inline float g_hudSniperChGap           = 1.f;
inline bool  g_hudSniperChDot           = true;
inline bool  g_hudSniperChT             = false;
inline bool  g_hudSniperChOutline       = true;
inline float g_hudSniperChOutlineThick  = 1.f;

// aimbot fov circle (botox m_aimbot_fov_circle_*)
inline bool  g_hudFovCircle             = false;
inline float g_hudFovCircleColor[4]     = { 1.f, 1.f, 1.f, 140.f / 255.f };
inline float g_hudFovCircleThick        = 1.f;
inline bool  g_hudFovCircleKeyOnly      = false;

// custom net graph (botox m_net_graph_*). Items = botox's 11-entry vector<bool>, as a bitmask.
inline bool  g_hudNg                    = false;
inline int   g_hudNgItems               = 0x7FF;
inline bool  g_hudNgInOut               = false;
inline bool  g_hudNgLerp                = false;
inline int   g_hudNgX                   = -1;   // -1 = stock position
inline int   g_hudNgY                   = -1;
inline float g_hudNgColor[4]            = { 229.f / 255.f, 229.f / 255.f, 178.f / 255.f, 1.f };
inline bool  g_hudNgWarn                = true;
inline float g_hudNgWarn1[4]            = { 1.f, 1.f, 31.f / 255.f, 1.f };
inline float g_hudNgWarn2[4]            = { 1.f, 125.f / 255.f, 31.f / 255.f, 1.f };
inline float g_hudNgWarn3[4]            = { 1.f, 31.f / 255.f, 31.f / 255.f, 1.f };
inline int   g_hudNgTextStyle           = 2;    // none / shadow / outline
inline float g_hudNgOutlineColor[4]     = { 0.f, 0.f, 0.f, 1.f };
inline bool  g_hudNgBg                  = false;
inline float g_hudNgBgColor[4]          = { 0.f, 0.f, 0.f, 120.f / 255.f };
// fonts tab > net graph (botox m_net_graph_font_settings "lucon" 14, flags all off)
inline char  g_hudNgFontName[64]        = "lucon";
inline int   g_hudNgFontSize            = 14;
inline int   g_hudNgFontFlags           = 0;

// csgo radar (CS:GO panorama csgo_hudradar.cpp; defaults = the cl_radar_* cvars)
inline bool  g_hudRadar                 = false;
inline bool  g_hudRadarSquare           = false;   // round (CS:GO default) / square (whole map)
inline bool  g_hudRadarRotate           = true;    // cl_radar_rotate 1
inline float g_hudRadarScale            = 0.7f;    // cl_radar_scale, 0.25..1
inline float g_hudRadarIconMin          = 0.6f;    // cl_radar_icon_scale_min, 0.4..1.25
inline float g_hudRadarHudScale         = 1.f;     // cl_hud_radar_scale, 0.8..1.3
inline bool  g_hudRadarShowAll          = false;   // enemies always shown, not only while spotted
inline float g_hudRadarOffX             = 0.f;     // drag offset, 1080p layout px (menu open: LMB drag, RMB reset)
inline float g_hudRadarOffY             = 0.f;
inline float g_hudRadarColorBlu[4]      = { 0x1e / 255.f, 0xd2 / 255.f, 1.f, 1.f };           // PI_Color_CT
inline float g_hudRadarColorRed[4]      = { 1.f, 0xdd / 255.f, 0x23 / 255.f, 1.f };           // PI_Color_T
inline float g_hudRadarColorEnemy[4]    = { 1.f, 0x19 / 255.f, 0x19 / 255.f, 1.f };           // PI_Color_Enemy


