// dih -- debloated TF2 visual overlay
// DX9 Present hook + ImGui menu. INSERT = menu, F11 = eject.
// Global definitions live here; features in src/hacks/*/*.cpp.

#include "globals/common.h"
#include "globals/globals.h"

// -- Core state ------------------------------------------------------------------
ICvar*             g_cvar             = nullptr;
HANDLE             g_hPollThread      = nullptr;
IEngineClient*     g_engine           = nullptr;
void*              g_gameEventManager = nullptr;
void*              g_matSystem        = nullptr;
void*              g_modelRender      = nullptr;
void*              g_modelInfo        = nullptr;
void*              g_renderView       = nullptr;
HWND               g_hwnd             = nullptr;
bool               g_imguiInit        = false;
IDirect3DDevice9*  g_d3dDev           = nullptr;
std::atomic<bool>  g_eject  {false};
std::atomic<bool>  g_ejectRequest {false};
std::atomic<int>   g_hookDepth     {0};
std::atomic<int>   g_miscHookDepth {0};

// -- Menu ------------------------------------------------------------------------
bool  g_showMenu       = false;
int   g_menuKey        = VK_INSERT;
ImFont* g_fontBold     = nullptr;
ImFont* g_fontReg      = nullptr;   // Tahoma 13 regular -- io.FontDefault used to be this;
                                    // now verdana 11 (g_menuReg) is the default (botox menu
                                    // spec section 6/9), so every HUD caller that used to get
                                    // the default font with NO g_fontBold-style fallback reads
                                    // this instead, and keeps its old size unchanged.
ImFont* g_fontMi       = nullptr;
ImFont* g_fontIcon13   = nullptr;   // botox font_name_icon_13 (cog glyph U+F013, OptionPopup)
ImFont* g_fontClarityInter = nullptr;
ImFont* g_fontClarityIcon  = nullptr;
ImFont* g_menuReg      = nullptr;
ImFont* g_menuBold     = nullptr;
ImFont* g_fontTahoma12 = nullptr, *g_fontTahomaBd12 = nullptr, *g_fontChillware13 = nullptr;
ImFont* g_fontLobotomy13 = nullptr, *g_fontLobotomyBold13 = nullptr;
ImFont* g_fontKamibebra13 = nullptr, *g_fontKamibebraBold13 = nullptr, *g_fontKamibebra22 = nullptr;
ImFont* g_fontCumhacck35 = nullptr, *g_fontAirplaneTitle = nullptr, *g_fontAirplaneRows = nullptr;
ImFont* g_fontInterBold15 = nullptr, *g_fontInterMedium15 = nullptr, *g_fontSunflower30 = nullptr;
ImFont* g_fontInterwebzCalibri = nullptr, *g_fontHavocTahoma13 = nullptr, *g_fontAirflow14 = nullptr;
ImFont* g_fontEvolve16 = nullptr, *g_fontEvolveBold16 = nullptr;
ImFont* g_fontLegendwareVerdana12 = nullptr, *g_fontLegendwareLucida10 = nullptr;
ImFont* g_fontInteriumDroid24 = nullptr, *g_fontSkebobNunito14 = nullptr;
ImFont* g_fontSkebobArial12 = nullptr, *g_fontSkebobArial13 = nullptr;
ImFont* g_fontCumidereTahoma16 = nullptr, *g_fontIllusoryTahoma13 = nullptr;
ImFont* g_fontLumiRubik16 = nullptr, *g_fontLumiProggy13 = nullptr, *g_fontInbaTahoma13 = nullptr;
ImFont* g_fontDnaMontserrat15 = nullptr, *g_fontDnaPtRootBold15 = nullptr, *g_fontDnaClarityIcon50 = nullptr;
ImFont* g_fontCucumberTahoma14 = nullptr, *g_fontCucumberVerdana14 = nullptr;
ImFont* g_fontHowewewareMinecraft14 = nullptr, *g_fontGamesenseSfBold15 = nullptr;
ImFont* g_fontHeroinArialBd16 = nullptr, *g_fontHeroinProggy13 = nullptr, *g_fontHeroinTahoma16 = nullptr;
ImFont* g_fontHeroinTahomaBd14 = nullptr, *g_fontHeroinTahoma14 = nullptr;
ImFont* g_fontMenuTahoma9 = nullptr, *g_fontMenuTahomaBd9 = nullptr;
ImFont* g_fontMs19 = nullptr;

int   g_menuStyle      = 0;
float g_accentColor[4] = { 160.f / 255.f, 170.f / 255.f, 220.f / 255.f, 1.f };   // matches render.cpp's old fixed kAccent
float g_dpiScale       = 100.f;
bool  g_dpiScalePanels = false;
bool  g_safeMode       = false;

// -- Watermark -------------------------------------------------------------------
// Defaults = botox csgo-sdk (m_watermark true/tickrate/fps/time, ping/velocity off, text "botox").
bool  g_wmEnable       = true;
bool  g_wmShowLabel    = true;
char  g_wmLabel[64]    = "botox";
bool  g_wmShowFPS      = true;
bool  g_wmShowPing     = false;
bool  g_wmShowLoss     = false;
bool  g_wmShowTime     = true;
int   g_wmPos          = WM_TOP_RIGHT;
// Style 0 box is the botox dark fill (25,25,25) + grey outline (50,50,50) now; brand is
// always white, info parts always the menu accent, so this stays unused by style 0.
float g_wmTextColor[4] = { 0.86f, 0.86f, 0.90f, 1.f };

// -- Media player ------------------------------------------------------------------
bool  g_mpEnable       = false;
bool  g_mpCoverArt     = true;
bool  g_mpBackground   = true;
bool  g_mpProgressBar  = true;
bool  g_mpSimple       = false;
bool  g_mpTime         = false;
int   g_mpPos          = WM_TOP_RIGHT;   // same corner as the watermark = stacked under it
int   g_mpLayout       = 0;

// -- misc HUD ---------------------------------------------------------------
bool  g_hmEnable        = false;
float g_hmColor[4]      = { 1.f, 1.f, 1.f, 1.f };
float g_hmSize          = 10.f;   // botox m_hit_marker_size default

bool  g_dpEnable        = false;
int   g_dpStyle         = 0;
float g_dpColor[4]      = { 140.f / 255.f, 200.f / 255.f, 1.f, 1.f };   // botox m_death_particles_* defaults
bool  g_dpMulticolor    = false;
int   g_dpAmount        = 8;
float g_dpSize          = 1.f;
float g_dpLifetime      = 3.f;
int   g_dpSoulStyle     = 0;
int   g_dpMcMode        = 0;
float g_dpMcXpTime      = 10.f;

bool  g_ptsEnable       = false;

bool  g_specEnable      = false;
bool  g_specAlwaysShow  = false;
int   g_specStyle       = 0;
int   g_specPos         = WM_TOP_LEFT;
float g_specOffX        = 0.f;   // drag offset on top of g_specPos (menu open, LMB drag / RMB reset)
float g_specOffY        = 0.f;
bool  g_specShowTarget    = true;
bool  g_specDelusionalLeft = false;
int   g_specListWidth     = 230;                          // botox m_spectators_list_width (slider 195..500)
float g_specColorOne[4]   = { 1.f, 1.f, 1.f, 1.f };       // botox text_color_one: rows watching the local player
float g_specColorTwo[4]   = { 1.f, 1.f, 1.f, 1.f };       // botox text_color_two: everyone else

int   g_wmStyle         = 0;

bool  g_discordEnable   = false;
char  g_discordAppId[32]     = "";
char  g_discordState[128]    = "In-game";
char  g_discordDetails[128]  = "Team Fortress 2";
char  g_discordImageKey[128] = "";

// fpsoverlay minesweeper (hacks/misc/minesweeper.cpp): diff 0..2 presets, 3 = custom
bool  g_msOpen   = false;
bool  g_msNight  = true;
int   g_msDiff   = 1;
int   g_msWidth  = 20;
int   g_msHeight = 20;
int   g_msMines  = 60;

float g_consoleColor[4] = { 0.f, 1.f, 1.f, 1.f };   // matches the hardcoded cyan it replaces

// -- Viewmodel ---------------------------------------------------------------------
bool  g_vmSwayEnable   = false;
float g_vmSwayScale    = 0.f;
float g_vmSwayInterp   = 0.f;
bool  g_vmOffsetEnable = false;
float g_vmOffsetX      = 0.f;
float g_vmOffsetY      = 0.f;
float g_vmOffsetZ      = 0.f;
bool  g_vmRemoveBob    = false;
// CS:GO viewmodel (vm_csgo.h) -- defaults = CS:GO cvar defaults
bool  g_vmCsBob            = false;
float g_vmCsBobCycle       = 0.98f;  // cl_bobcycle
float g_vmCsBobUp          = 0.5f;   // cl_bobup
float g_vmCsBobVert        = 0.25f;  // cl_bobamt_vert
float g_vmCsBobLat         = 0.4f;   // cl_bobamt_lat
float g_vmCsBobLower       = 21.f;   // cl_bob_lower_amt
bool  g_vmCsGunLower       = false;
float g_vmCsGunLowerAngle  = 2.f;    // cl_gunlowerangle
float g_vmCsGunLowerSpeed  = 0.1f;   // cl_gunlowerspeed
bool  g_vmCsLandDip        = false;
float g_vmCsLandDipAmt     = 4.f;    // cl_headbob_land_dip_amt
float g_vmCsLandDipTime    = 0.25f;  // seconds to the bottom (CS:GO 0.25)
bool  g_vmAlphaEnable  = false;
float g_vmAlpha        = 0.5f;   // 1 = opaque (no-op), 0 = invisible viewmodel
bool  g_vmFlatAlpha    = true;   // one alpha over the whole weapon, not alpha x phong mask
bool  g_vmLowFps       = false;  // low fps viewmodel: gun anim stepped at g_vmLowFpsRate, camera live
float g_vmLowFpsRate   = 24.f;   // botox default 24, slider 15..60

// -- Debug log --------------------------------------------------------------------
bool  g_debugLogEnable = false;

// -- Fog -----------------------------------------------------------------------------
bool  g_fogEnable    = false;
float g_fogColor[4]  = { 0.5f, 0.5f, 0.5f, 1.f };
float g_fogStart     = 0.f;
float g_fogEnd       = 0.f;
float g_fogDensity   = 0.f;

// -- Screen effects (depth source -- hacks/screen/screenfx.cpp) ------------------------------
bool  g_fxDepthProbe  = false;
bool  g_fxForceQueueMode = true;
bool  g_fxUseCallQueue = true;
float g_fxParticleGuard = 1.0f;

// Ambient occlusion (hacks/screen/ssao.cpp). Defaults are coldtires' own, unchanged.
bool  g_aoEnable      = false;
float g_aoIntensity   = 1.6f;
float g_aoRadius      = 40.f;
int   g_aoSamples     = 12;
float g_aoBias        = 0.2f;
float g_aoBlur        = 1.f;
float g_aoScale       = 0.75f;
float g_aoFadeStart   = 1200.f;
float g_aoFadeEnd     = 3000.f;
bool  g_aoViewmodel   = true;
bool  g_aoFogFade     = true;
bool  g_aoDebug       = false;

// Depth of field (hacks/screen/dof.cpp). Defaults are coldtires' own, unchanged.
bool  g_dofEnable        = false;
bool  g_dofAutoFocus     = true;
float g_dofDistance      = 400.f;
float g_dofFocusSpeed    = 8.f;
float g_dofFocusLimit    = 4000.f;
float g_dofRange         = 300.f;
float g_dofRamp          = 600.f;
float g_dofMaxBlur       = 20.f;
float g_dofTapDensity    = 1.25f;
float g_dofBokehBoost    = 4.f;
float g_dofBokehThreshold= 0.6f;
bool  g_dofNearBlur      = true;
bool  g_dofViewmodelSharp= true;
bool  g_dofSmooth        = true;

// Frame blending (hacks/screen/frameblend.cpp). 8ms is a 1/125s shutter, which is about what a
// camera shooting 60fps footage of a game runs at -- long enough to see, short enough that the
// picture never stops being readable.
bool  g_fbEnable     = false;
float g_fbShutter    = 8.f;
float g_fbAmount     = 1.f;
bool  g_fbPlayers    = true;
bool  g_fbViewmodel  = true;
float g_fbVmAmount   = 1.f;
int   g_fbSamples    = 24;
float g_fbMaxStreak  = 0.06f;

// -- Bloom (hacks/screen/bloom.cpp) ----------------------------------------------------------
bool  g_bloomEnable    = false;
float g_bloomIntensity = 0.6f;
float g_bloomThreshold = 0.7f;
float g_bloomRadius    = 0.5f;
float g_bloomSat       = 1.2f;

// -- Color correction --------------------------------------------------------------------
bool  g_ccEnable      = false;
float g_ccGainR       = 1.f;
float g_ccGainG       = 1.f;
float g_ccGainB       = 1.f;
float g_ccExposure    = 0.f;
float g_ccTemp        = 0.f;
float g_ccHue         = 0.f;
float g_ccSat         = 1.f;
float g_ccContrast    = 1.f;
float g_ccLevInBlack  = 0.f;
float g_ccLevInWhite  = 1.f;
float g_ccGamma       = 1.f;
float g_ccLevOutBlack = 0.f;
float g_ccLevOutWhite = 1.f;
float g_ccHighlights  = 0.f;
float g_ccShadows     = 0.f;
// Identity: two points on the diagonal. CcIsNeutral() tests for exactly this, and the
// whole pass is skipped while all four curves match it.
CcCurve g_ccCurve[4] = {
    { 2, { 0.f, 1.f }, { 0.f, 1.f } },
    { 2, { 0.f, 1.f }, { 0.f, 1.f } },
    { 2, { 0.f, 1.f }, { 0.f, 1.f } },
    { 2, { 0.f, 1.f }, { 0.f, 1.f } },
};

// -- simple screen post-fx (hacks/screen/screenfx_passes.cpp) -----------------------
bool  g_sharpenEnable      = false;
float g_sharpenAmount      = 0.5f;
bool  g_filmGrainEnable    = false;
float g_filmGrainAmount    = 0.08f;
bool  g_vignetteEnable     = false;
float g_vignetteAmount     = 0.4f;
bool  g_vignetteInvert     = false;
bool  g_invertFilterEnable = false;
bool  g_posterizeEnable    = false;
float g_posterizeLevels    = 8.f;
bool  g_debandEnable       = false;
float g_debandAmount       = 8.f;
bool  g_channelShiftEnable = false;
float g_channelShiftAmount = 2.f;
bool  g_channelShiftDepth      = false;
int   g_channelShiftDepthMode  = 0;
float g_channelShiftDepthDist  = 200.f;
float g_channelShiftDepthRange = 0.f;
float g_channelShiftDepthRamp  = 600.f;
// depth distort, Warp-FX defaults
bool  g_warpEnable          = false;
int   g_warpEffect          = 1;
int   g_warpAnimate         = 0;
float g_warpAnimSpeed       = 1.f;
float g_warpCenter[2]       = { 0.5f, 0.5f };
bool  g_warpUseOffset       = false;
float g_warpOffset[2]       = { 0.5f, 0.5f };
float g_warpRadius          = 0.5f;
float g_warpTension         = 1.f;
float g_warpMagnitude       = -0.5f;
float g_warpAngle           = 180.f;
bool  g_warpInverse         = false;
float g_warpInnerRadius     = 0.f;
int   g_warpSplices         = 10;
float g_warpAmplitude       = 1.f;
float g_warpPeriod          = 0.25f;
float g_warpPhase           = 0.f;
int   g_warpWaveType        = 1;
float g_warpWaveAngle       = 0.f;
float g_warpWavePeriod      = 3.f;
float g_warpWaveAmplitude   = 0.075f;
float g_warpDrunkStrength[2] = { 25.f, 25.f };
float g_warpDrunkSpeed[2]   = { 2.f, 2.f };
float g_warpDrunkAngle      = 180.f;
float g_warpDrunkAngleSpeed = 1.f;
float g_warpPlanetRot[3]    = { 0.5f, 0.5f, 0.5f };
float g_warpPlanetScale     = 10.f;
float g_warpPlanetOffset[2] = { 0.f, 0.f };
float g_warpPlanetSeam      = 0.5f;
float g_warpSlitPos         = 0.5f;
float g_warpSlitSpeed       = 1.f;
int   g_warpSlitDir         = 0;
float g_warpBorderColor[4]  = { 1.f, 0.f, 0.f, 1.f };
float g_warpAspect          = 0.f;
float g_warpAspectAngle     = 0.f;
int   g_warpBlend           = 0;
float g_warpOpacity         = 1.f;
int   g_warpDepthMode       = 0;
float g_warpDepthDist       = 200.f;
float g_warpDepthRange      = 0.f;
float g_warpDepthRamp       = 600.f;
bool  g_warpDepthProtect    = true;
bool  g_warpFade            = false;
bool  g_emphasizeEnable    = false;
float g_emphasizeAmount    = 1.f;     // botox m_emphasize_effect_factor
bool  g_emphasizeSpherical = true;
bool  g_showDepthEnable    = false;
// botox depth_view (emphasize = reshade Emphasize.fx, show depth = DisplayDepth.fx), botox defaults
float g_emphasizeFocusDepth  = 0.f;
float g_emphasizeFocusRange  = 0.f;
float g_emphasizeFocusEdge   = 0.001f;
int   g_emphasizeSphereFov   = 180;
float g_emphasizeSphereX     = 0.537f;
float g_emphasizeSphereY     = 0.5f;
float g_emphasizeBlendColor[4] = { 0.f, 0.f, 0.f, 1.f };
float g_emphasizeBlendFactor = 0.2f;
int   g_showDepthType        = 0;       // 0 depth map, 1 normal map, 2 both
bool  g_showDepthBlend       = false;
bool  g_showDepthLive        = false;
bool  g_showDepthUpsideDown  = false;
bool  g_showDepthReversed    = false;
bool  g_showDepthLog         = false;
float g_showDepthScaleX      = 1.f;
float g_showDepthScaleY      = 1.f;
int   g_showDepthOffsetX     = 0;
int   g_showDepthOffsetY     = 0;
float g_showDepthFarPlane    = 1000.f;
float g_showDepthMultiplier  = 1.f;

// -- world fx (hacks/visuals/world_fx.cpp) --------------------------------------------
bool  g_fullbrightEnable    = false;
bool  g_disableInterpEnable = false;
bool  g_aspectRatioEnable   = false;
float g_aspectRatioValue    = 1.777778f; // botox m_aspect_ratio (16:9)
bool  g_worldFovEnable      = false;
float g_worldFovAmount      = 90.f;
bool  g_thirdPersonEnable   = false;
float g_thirdPersonDistance = 100.f;

// -- reflections + minecraft sky/clouds -----------------------------------------
bool  g_reflEnable    = false;
float g_reflIntensity = 1.f;    // botox m_reflections_intensity
float g_reflDistance  = 0.8f;   // botox m_reflections_distance
bool  g_mcWorldEnable = false;
// -- world fx 2 (hacks/visuals/world_fx2.cpp) ----------------------------------------
bool  g_removeVisualRecoilEnable = false;

bool  g_dlightEnable  = false;
float g_dlightRadius  = 150.f;
float g_dlightColor[4]= { 1.f, 0.6f, 0.2f, 1.f };


bool  g_worldTintEnable   = false;
float g_worldTintColor[4] = { 1.f, 1.f, 1.f, 1.f };
bool  g_propTintEnable    = false;
float g_propTintColor[4]  = { 1.f, 1.f, 1.f, 1.f };
bool  g_weatherTintEnable   = false;
float g_weatherTintColor[4] = { 1.f, 1.f, 1.f, 1.f };

// -- Skybox ----------------------------------------------------------------------------
bool g_skyEnable     = false;
char g_skyName[64]   = "sky_tf2_04";

// -- Weather ------------------------------------------------------------------------------
bool  g_weatherEnable = false;
int   g_weatherMode   = 0;    // 0=off, 1=rain, 2=snow
// Defaults are the engine's own (r_rain* convar defaults / SNOW_SPEED), NOT zero. Every one of
// these feeds a formula that collapses at 0: speed divides ComputeEmissionArea's fallTime,
// length scales the tracer delta to nothing, radius is the emission box edge.
float g_rainAlpha     = 0.40f;
float g_rainSpeed     = 600.f;
float g_rainWidth     = 0.50f;
float g_rainLength    = 0.10f;
float g_rainRadius    = 1500.f;
float g_rainSideVel   = 130.f;
float g_snowAlpha     = 0.60f;
float g_snowSpeed     = 80.f;   // SNOW_SPEED
float g_snowWidth     = 0.70f;  // SNOW_TRACER_WIDTH
float g_snowLength    = 0.05f;  // half the rain streak -- flake, not a line
float g_snowRadius    = 1500.f;
float g_snowSideVel   = 30.f;
float g_snowDensity   = 1.f;

// -- No hats ---------------------------------------------------------------------------------
bool g_noHats          = false;
bool g_noUnusual       = false;

// -- Weapon sheen blend (lsd_chams.cpp DrawSheenNormal) -------------------------------------
bool  g_sheenNormal      = false;   // killstreak sheen: flat tint masked by the sheen texture, normal blend
float g_sheenNormalAlpha = 1.f;
float g_sheenNormalEnv   = 3.f;     // envmap strength (stock pass = 10, blown out)

// -- Killfeed ------------------------------------------------------------------------------------
bool g_selfKillfeedOnly = false;
bool g_killfeedCsgo     = false;   // csgo killfeed with tf2 icons (aim/killfeed_csgo.cpp)

// -- Scoreboard (Aletherium "reveal scoreboard") --------------------------------------------------
bool g_revealScoreboard = false;
bool g_steamHistory     = false;   // right-click a player -> "Steam History" (steamhistory.net)

// -- Crosshair ----------------------------------------------------------------------------------------
bool  g_chEnable        = false;
// defaults = SimpleVisual's C:\dih\default.cfg -- at 0 thickness/length enabling drew nothing
bool  g_chAntialias     = true;
bool  g_chAdditive      = false;   // arms + dot blend ONE instead of INVSRCALPHA
int   g_chBlendMode     = 0;       // kChBlend id (ch_blend_modes.h); 0 falls back to g_chAdditive
float g_chColor[4]      = { 1.f, 1.f, 1.f, 1.f };
float g_chThickness     = 1.1f;
float g_chLength        = 3.f;
float g_chGap           = 1.6f;
float g_chAspect        = 1.f;     // arm length w:h. 1 = square, >1 wider, <1 taller
float g_chGapAspect     = 1.f;     // gap w:h, same rule
bool  g_chDot           = false;
bool  g_chDotCircle     = false;
bool  g_chOutline       = true;
float g_chOutlineColor[4] = { 0.f, 0.f, 0.f, 1.f };
float g_chOutlineThick  = 0.5f;
bool  g_staticAmbyCrosshair = false;

// CS:GO sniper scope (scope_csgo.inl) -- csgo defaults
bool  g_scCsgo            = false;
int   g_scCsgoWeapon      = 0;      // kScCsWeapons: 0 AWP, 1 SSG 08, 2 SCAR-20/G3SG1
int   g_scCsgoZoom        = 0;      // 0 = zoom fov 1, 1 = zoom fov 2
int   g_scCsgoWidth       = 1;      // cl_crosshair_sniper_width
bool  g_scCsgoNormalInacc = false;  // cl_crosshair_sniper_show_normal_inaccuracy
float g_scCsgoMoveSens    = 1.f;    // ground speed inaccuracy, 0 = still scope
float g_scCsgoAirSens     = 1.f;    // jump/air/land inaccuracy
bool  g_scCsgoScopeIn     = true;   // unscoped penalty carries in (blurred scope-in)
bool  g_scCsgoBob         = true;   // scope sways with the csgo bob
float g_scCsgoBobScale    = 1.f;
bool  g_scCsgoLens        = true;   // overlays/scope_lens dust
float g_scCsgoScale       = 1.f;    // arc box + lens size, 1 = csgo
bool  g_scCsgoNoDot       = true;   // hide your own sniper dot
bool  g_scChargeHide      = false;  // skip CHudScopeCharge::Paint
float g_scCsgoOpacity     = 1.f;    // whole scope alpha
float g_scCsgoBobLat      = 1.f;    // sway strength, sideways (x csgo cl_bobamt_lat 0.4)
float g_scCsgoBobVert     = 1.f;    // sway strength, up/down (x csgo cl_bobamt_vert 0.25)

// CSGO default: cl_crosshaircolor 1 -> crosshair.css crosshairColor1 #82b116, gap offset 0
bool  g_chCsgoEnable    = false;
int   g_chCsgoStyle     = 0;
float g_chCsgoColor[4]  = { 130.f/255.f, 177.f/255.f, 22.f/255.f, 1.f };
float g_chCsgoGap       = 0.f;
float g_chCsgoScale     = 1.f;
bool  g_chCsgoCrisp     = true;
bool  g_chCsgoLinear    = true;
bool  g_chCsgoAdditive  = true;     // csgo's SRGBadditive on pips/arcs/dot
float g_chCsgoBlack     = 1.0f;     // 1 = csgo's own wash strength
float g_chCsgoVelSens   = 1.f;      // 1 = csgo's own step sizes, 0 = movement moves nothing
float g_chCsgoSmooth    = 0.f;      // seconds to ease into a new gap/spread, 0 = snap (csgo)
bool  g_chCsgoArcs      = true;     // the 4 thin spread lines
bool  g_chCsgoPips      = true;     // the 4 green gap arms
float g_chCsgoAspect    = 1.f;      // arm length w:h, both style families
float g_chCsgoGapAspect = 1.f;      // gap w:h, both style families
// classic styles 2/3/4 -- csgo convar defaults, one for one
float g_csXSize         = 5.f;      // cl_crosshairsize
float g_csXThick        = 0.5f;     // cl_crosshairthickness
float g_csXGap          = 1.f;      // cl_crosshairgap
bool  g_csXDot          = true;     // cl_crosshairdot
bool  g_csXOutline      = true;     // cl_crosshair_drawoutline
float g_csXOutlineTh    = 1.f;      // cl_crosshair_outlinethickness
bool  g_csXT            = false;    // cl_crosshair_t
int   g_csXAlpha        = 200;      // cl_crosshairalpha
bool  g_csXUseAlpha     = true;     // cl_crosshairusealpha
float g_csXSplitDist    = 7.f;      // cl_crosshair_dynamic_splitdist
float g_csXSplitRatio   = 0.35f;    // cl_crosshair_dynamic_maxdist_splitratio
float g_csXInnerMod     = 1.f;      // cl_crosshair_dynamic_splitalpha_innermod
float g_csXOuterMod     = 0.5f;     // cl_crosshair_dynamic_splitalpha_outermod

// -- Convenience (region selector + net settings) ------------------------------------------------------
bool g_regionEnable       = false;
int  g_regionMask         = 0;      // bitmask, bit i = kRegionNames[i]
bool g_netSettingsEnable  = false;
int  g_netSettingsSel     = 0;      // 0..10 klown/murmur/boobsy/xcd/0lerp/15lerp/30lerp/spy/koy/lenny/snappy
// rate / cl_cmdrate / cl_updaterate are the bandwidth trio, not a lerp setting: the server
// trims what it sends to fit `rate` (player.cpp:3484), so raising it does not widen a thin
// connection, it stops the server making the snapshots fit and the excess is dropped on the
// wire instead. Every preset asked for 196608 vs TF2's own 80000. OFF unless asked for --
// see the block above kNetCfgs in netregion.cpp.
bool g_netSettingsRates   = false;
bool  g_ngColorEnable     = false;  // recolor TF2 net_graph text
float g_ngTextColor[4]    = { 0.9f, 0.9f, 0.7f, 1.f }; // = engine default 229/229/178
// netgraph warn text (vgui_netgraphpanel.cpp): lerp/cpu warn = 255/125/31,
// bad lerp + sv fps<10 + cpu warn2 = 255/31/31
float g_ngOrangeColor[4]  = { 1.f, 0.490f, 0.122f, 1.f };
float g_ngRedColor[4]     = { 1.f, 0.122f, 0.122f, 1.f };
// yellow warn: interp below cl_updaterate (255/255/31) and sv fps 10..20 (255/255/0).
// Two engine triples, one picker -- both are the same "yellow warn" tier.
float g_ngYellowColor[4]  = { 1.f, 1.f, 0.122f, 1.f };

// net graph position. TF2 gives x only (net_graphpos = left/center/right, or a pixel
// value that is then XRES-scaled AND clamped) and NO y at all -- vgui_netgraphpanel.cpp
// GraphGetXY hardcodes *y = rect->y + rect->height - LERP_HEIGHT - 5. These two are free
// pixel offsets added to the finished x/y, past every engine clamp: any value, on screen
// or off. See netregion.cpp.
bool g_ngPosEnable        = false;
int  g_ngPosX             = 0;      // px, + = right
int  g_ngPosY             = 0;      // px, + = down

// -- Equip region unlock ---------------------------------------------------------------------
// Client-side loadout gate removal: cosmetics that share an equip_region (two hats, hat +
// whole_head, ...) stop greying each other out. Three client.dll hooks, see equipregion.cpp.
bool g_equipRegionUnlock  = false;

// -- Bypass ---------------------------------------------------------------------------------------------
bool g_pureBypass   = false;
bool g_cheatsBypass = false;
bool g_f2pChatBypass = false;   // clears the GC's match chat suspension, see mmfix.cpp

// -- Removals -----------------------------------------------------------------------------------------------
bool g_removeRagdolls = false;
bool g_removePropFade = false;
bool g_fpsBoost       = false; // bones optimization: cached SetupBones for non-local players
bool g_removeScreenEffects = false; // swallow Shake/Fade/Rumble user messages

// -- Flip world (flipworld.cpp) -- mirror the 3D frame, the HUD's world anchors, the hand + input --
bool g_flipWorld = false;

// -- Lag resolver (lagres.cpp) -- draw a laggy player smooth instead of frozen-then-jumped --
bool  g_lagResolver = false;  // off by default: it trades a little accuracy for no twitch
bool  g_lrYaw       = true;   // smooth the model's yaw too, not just his position
float g_lrSmoothMs  = 60.f;   // ease time constant, ms. bigger = smoother AND later
float g_lrExtrapMs  = 200.f;  // how long a stalled player is carried at his last velocity
float g_lrMaxErr    = 64.f;   // units of error we refuse to smear -- snap past this

// -- Performance / latency (default OFF -- opt in from the menu) ------------------------------------------
// "low latency (gpu flush)" and "high process priority" DELETED 2026-09-05, DO NOT RE-ADD.
// The flush spun on a D3DQUERYTYPE_EVENT after every Present, so frame time stopped being
// max(CPU, GPU) and became CPU + GPU -- measured as net_graph out packets/s falling 60 -> 50
// (that row is one send per rendered FRAME, so it reads as packet loss and is really fps).
// HIGH_PRIORITY_CLASS made it worse: the spin was a PAUSE loop with no yield to the OS,
// holding a core against the rest of hl2.exe for the whole GPU frame. Neither bought a
// measurable win. Only the 1ms timer survives -- it costs nothing and is not a spin.
bool g_hiTimerRes  = false; // timeBeginPeriod(1) -- engine fps_max sleeps get 1ms, not 15.6ms
bool g_perfEnable  = false; // botox m_performance default
// botox m_convenience_buttons defaults, TF2 commands (its ak47/awp/csgo-server rows have none)
char g_convTitle[kConvButtons][32]  = { "kill all bots", "kick all bots" };
// tf_bot_kill / tf_bot_kick take "all" (SDK tf_bot.cpp:447/527)
char g_convCmd[kConvButtons][160]   = { "tf_bot_kill all", "tf_bot_kick all" };

// -- fpsopt.cpp DELETED: its nine convar groups only mirrored console commands the player can
// set themselves, so the DLL was holding settings, not running a feature. cfg keys went too.

// -- Auto backstab: lives in the aim tree now (src/hacks/aim/aim_melee.cpp, knobs under
// Vars::Aimbot::Melee). dih's g_backstab* / g_cleanPovDemos globals are deleted -- their
// last reader went with backstab.cpp at bs11, and their cfg keys with it.

// -- Movement (all default OFF; mode 0 = always on, 1 = on hotkey, 2 = toggle) --------------------------------
bool g_bhop = false; // plain on/off, no bind
bool g_jumpbug = false;     int g_jumpbugKey = 0;     int g_jumpbugMode = 1;
bool g_jbYieldSurf = true;  // jumpbug hands the tick back while a pixel surf ride is live
bool g_jbPredict = true;    // no band tick in the simulated fall = leave the cmd alone
bool g_autoStrafe = false;  int g_autoStrafeKey = 0;  int g_autoStrafeMode = 1;
bool  g_autoStrafeAvoidWalls = false;
float g_autoStrafeAvoidDist  = 50.f;
bool g_strafeOpt = false;   // plain on/off, no bind
float g_strafeOptGain = 100.f;
float g_strafeOptMinSpeed = 30.f;
bool g_edgeJump = false;    int g_edgeJumpKey = 0;    int g_edgeJumpMode = 1;
bool g_longJump = false;    int g_longJumpKey = 0;    int g_longJumpMode = 1;
bool g_longJumpEdge = false;
bool  g_longJumpAdaptive = false;
bool  g_longJumpAdaptiveLetGo = true;
bool  g_longJumpAdaptiveLanded = true;
bool  g_longJumpAdaptiveTimed = false;
float g_longJumpAdaptiveTime = 0.3f;
bool g_miniJump = false;    int g_miniJumpKey = 0;    int g_miniJumpMode = 1;
bool g_miniJumpHoldDuck = false;
bool g_miniJumpEdge = false;
bool g_fastStop = false;    // plain on/off, no bind
bool g_fastAccel = false;   // plain on/off, no bind
bool g_noPush = false;      // skip C_TFPlayer::AvoidPlayers -- teammates stop shoving you
// wave 2 (Aletherium): edge bug + pixel surf family
bool g_edgeBug = false;     int g_edgeBugKey = 0;     int g_edgeBugMode = 1;
// botox n_edgebug knobs (variables.h m_edgebug_*), same defaults
float g_edgeBugPredictTime = 0.35f;   // search window, seconds
int   g_edgeBugDetectMode = 0;        // 0 normal / 1 enhanced / 2 ebanat
bool  g_edgeBugToEdge = true;         // autostrafe to edge (also the autostrafe switch)
bool  g_edgeBugPlayers = false;       // allow bugs off enemy players (off = map geometry only)
float g_edgeBugStrafeAngleMax = 180.f; // max path bend off course, 180 = off
bool  g_edgeBugMouseSteer = true;
int   g_edgeBugPaths = 3;
int   g_edgeBugLockType = 1;          // botox: smooth / full (donor: static / full)
float g_edgeBugLockStrength = 100.f;
// 0 = any (ducked find that eats the next jump only as fallback), 1 = ducked only, 2 = stand only
int   g_edgeBugStance = 0;
// donor styles (botox m_edgebug_del_* / m_edgebug_dna_*)
int   g_edgeBugDelTicks = 64;
bool  g_edgeBugDelAdvanced = false;
float g_edgeBugDelAngleLimit = 50.f;
int   g_edgeBugDelSearch = 5;
bool  g_edgeBugDelSilent = false;
bool  g_edgeBugDelMouseFix = true;
float g_edgeBugDnaScan = 0.4f;
bool  g_edgeBugDnaAdvanced = false;
int   g_edgeBugDnaRange = 6;
bool  g_edgeBugDnaCustomAngle = false;
float g_edgeBugDnaAngleLimit = 3.f;
int   g_edgeBugHwTicks = 32;            // botox m_edgebug_hw_* defaults
float g_edgeBugHwLockAmt = 0.3f;
bool  g_edgeBugHwAdvanced = false;
float g_edgeBugHwStrafeDeltaMax = 10.f;
bool  g_edgeBugViz = false;         // draw the latched plan (path + landing pad)
float g_edgeBugVizSize = 16.f;      // pad half-width; 16 = the player hull's own footprint
float g_edgeBugVizColor[4] = { 1.f, 1.f, 1.f, 1.f };     // path + pad outline (botox m_edgebug_visualize_color)
float g_edgeBugVizThick = 1.5f;
int   g_edgeBugVizShape = 0;        // 0 square, 1 circle
int   g_edgeBugVizFill = 0;         // 0 outline, 1 filled, 2 filled + outline
float g_edgeBugVizFillAlpha = 0.35f;
float g_edgeBugVizPadColor[4] = { 1.f, 1.f, 1.f, 1.f };
bool g_pixelSurf = false;   int g_pixelSurfKey = 0;   int g_pixelSurfMode = 1;
int  g_pixelSurfPredictTicks = 8;
int  g_psWallStrafe = 1;     // 0 off / 1 keys into wall (botox) / 2 always on a ride
bool g_psParkAtEnd  = false; // brake to a stop where the ride runs out, hang there
// approach aids (both OFF by default -- see the memory note on the guide/speed fix)
bool g_autoAlign = false;   // plain on/off, no bind
bool g_aaSurfStick = true;  // auto align: a pin seen once = press into that face every tick (tb style)
// -- TB knob set rebuilt off coldtires. The -knobs (autocrouch, seam scan,
// aim frac, engage dist, xy keep, window keeper) went with the machinery they drove; autocrouch
// is SIM-DECIDED now (a duck pass runs only when the standing hull missed everywhere)
bool g_textureBug = false;  int g_textureBugKey = 0;  int g_textureBugMode = 1;
int g_tbStyle = 1;             // 0 cumidere (TF2 dll port) / 1 botox (csgo texturebug.cpp 1:1, texturebug.inl tbx)
bool g_tbcAlign = true;        // both tb styles: cumidere misc_auto_align (ride steer + gain) before the tb
bool g_tbcAutoCrouch = false;  // cumidere style: duck grid pass when standing misses, latched through the ride
float g_tbReach = 8.f;         // botox style: 8-dir wall scan reach off the hull (botox m_texture_bug_reach, 0-48)
int   g_tbTypes = 3;           // botox style: bit0 ducking / bit1 standing allowed (m_texture_bug_types)
bool  g_tbAutoDuck = true;     // botox style: tb may crouch you (m_texture_bug_auto_duck)
bool  g_tbWallstrafe = false;  // botox style: keep along-wall speed while pinned (m_texture_bug_wallstrafe)
float g_tbScanStep = 1.f;      // z resolution of the scan's line grid
bool  g_tbNoOffgridStick = false; // refuse the HOLD paths on a yaw-rotated wall (pins still ride)
bool g_wallClimb = false;   int g_wallClimbKey = 0;   int g_wallClimbMode = 1;
bool g_wcPreventSlow = true;  bool g_wcDuck = true;   // botox wall climb defaults
bool g_wcSeamSteer = false;  float g_wcSteerScale = 1.f;   // botox m_wall_climb_seam_steer / steer_scale
bool g_wcViz = false;  float g_wcVizColor[4] = { 1.f, 190 / 255.f, 90 / 255.f, 1.f };
bool g_airStuck = false;    int g_airStuckKey = 0;    int g_airStuckMode = 1;

// -- Movement tricks port (movement_ex.inl): small tricks from botox csgo-sdk, defaults OFF ---------------
bool g_nulls = false;
bool g_hsw = false;        int g_hswKey = 0;        int g_hswMode = 1;
int  g_hswSide = 0;
bool g_hswAllowWS = true;
bool g_hswAllowAD = true;
bool g_hswPreGround = true;
bool g_delayHop = false;   int g_delayHopKey = 0;   int g_delayHopMode = 1;
int  g_delayHopTicks = 6;
bool g_goomba = false;     int g_goombaKey = 0;     int g_goombaMode = 1;
int  g_goombaHow = 2;      int g_goombaLead = 0;
bool g_autoPogo = false;   int g_autoPogoKey = 0;   int g_autoPogoMode = 1;
bool g_autoCrouch = false; int g_autoCrouchKey = 0; int g_autoCrouchMode = 1;
int  g_autoCrouchTicks = 6;
bool g_djStrafe = false;   int g_djStrafeKey = 0;   int g_djStrafeMode = 1;
bool g_silentView = false;
bool g_jumpstats = false;

// -- Movement tricks 2 port (movement_tricks2.inl): tung/bounce/pf modes/eb presets/ps fix ---------
bool g_tungSurf = false;   int g_tungSurfKey = 0;   int g_tungSurfMode = 1;
float g_tungSnap = 0.f;   bool g_tungPress = false;
bool g_autoBounce = false; int g_autoBounceKey = 0; int g_autoBounceMode = 1;
// auto bounce = botox auto_bounce.cpp (autobounce.inl), defaults = botox variables.h:1250-1257
int   g_abMode = 0;           // 0 drop in / 1 keep speed / 2 stop
float g_abMinGain = 0.f;
float g_abReach = 128.f;
bool  g_abJump = true;
bool  g_abCrouch = false;
bool  g_abAlign = false;
bool  g_abViz = false;
float g_abVizColor[4] = { 120 / 255.f, 200 / 255.f, 1.f, 1.f };
bool g_bounceAssist = false; int g_bounceAssistKey = 0; int g_bounceAssistMode = 1;
float g_bounceReach = 48.f;
float g_bounceMinSpeed = 150.f;
bool g_psFinderHeadbounce = false;
bool g_psFinderPixeljump = true;    // botox m_pixel_finder_pixeljumps default
bool g_psFinderJumpSurf = false;
bool g_psSurfFix = false;
int  g_edgeBugStyle = 0;
bool g_edgeBugMouseLock = false;
bool g_edgeBugSound = false;
int  g_edgeBugSoundId = 0;   // index into movement_tricks2.inl kEbSounds
// -- Edge skip port (movement_edge.inl): edge skip + blockbot, defaults OFF ------------------------
bool g_edgeSkip = false;   int g_edgeSkipKey = 0;   int g_edgeSkipMode = 1;
int   g_edgeSkipTicks = 23;
int   g_edgeSkipDepth = 2;
float g_edgeSkipMaxBend = 180.f;
bool g_blockBot = false;   int g_blockBotKey = 0;   int g_blockBotMode = 1;

// fake movement (local server only)
bool g_fmEnabled = true;     // master switch: off = every fake movement feature off
bool g_fmWallSurf = false;   int g_fmWallSurfKey = 0;   int g_fmWallSurfMode = 1;
bool g_fmWallJump = false;   int g_fmWallJumpKey = 0;   int g_fmWallJumpMode = 1;
float g_fmWallReach = 4.f;
float g_fmWallJumpPush = 0.f;
bool  g_fmWallJumpHold = true;
bool  g_fmWallSurfSeam = false;
bool  g_fmWallJumpSeam = false;
float g_fmSeamSlack = 0.f;
bool g_fmHighJump = false;   int g_fmHighJumpKey = 0;   int g_fmHighJumpMode = 1;
float g_fmHighJumpMult = 1.5f;
bool g_fmAirStuck = false;   int g_fmAirStuckKey = 0;   int g_fmAirStuckMode = 1;
bool g_fmAirStuckKeepVel = true;
bool g_fmAirStuckNearOnly = false;
float g_fmAirStuckNearDist = 8.f;
bool g_fmNoFall = false;     int g_fmNoFallKey = 0;     int g_fmNoFallMode = 1;
bool g_fmWallbugFix = false; // local listen server: sv_wallbug 1 (Valve's Oct-2026 revert switch)
bool g_fmCrouchJump = false; int g_fmCrouchJumpKey = 0; int g_fmCrouchJumpMode = 1;
bool g_fmRampBoost = false;  int g_fmRampBoostKey = 0;  int g_fmRampBoostMode = 1;
float g_fmRampBoostMult = 1.5f;
bool g_fmHeadSurf = false;   int g_fmHeadSurfKey = 0;   int g_fmHeadSurfMode = 1;
float g_fmHeadSurfSnap = 18.f;
bool g_fmHeadSurfLedgeOnly = true;
bool g_fmHeadSurfBottoms = true;
float g_asExtraReach = 4.f;  // botox m_air_stuck_extra_reach
float g_asBudget = 50.f;     // botox m_air_stuck_budget (percent of a tick)
bool g_asMovementFix = true;    // botox m_movement_fix (air_stuck.cpp:168 -- off forces view+move onto cmd)
bool g_asDriftHang = true;      // sv_wallbug 0: hold a pinned hull still off the face (MV_DriftHold)
// wave 3 (Aletherium): pixel surf finder + assist
bool g_psFinder = false;    int g_psFinderKey = 0;    int g_psFinderMode = 1;
float g_psFinderMargin = 8.f;
bool  g_psFinderStrict = true;  // pfstrict: drop teleport-only pins + spots sealed off (clip in front)
bool  g_psFinderTb = true;      // run the texturebug seam pass too (the red rings)
bool  g_psFinderNoProps = true; // props are not map geometry -- finder traces refuse them
bool  g_psFinderLength = true;  // walk found surfs along the wall, draw their length
bool  g_psMapLines = true;      // draw the whole-map scan's lines (own switch, not the finder's)
float g_psMapMaxDist = 0.f;     // map-scan marks farther than this from you not drawn (0 = no limit)
float g_psFinderBudget = 16.f;  // ms per cmd the chunked finder scan may spend (;
                                // raised 2 -> 16: the scan no longer shares the TB clock, so this
                                // is the whole bound. A sim measures ~8.8us in the log, so
                                // a 150-step column is ~300ms of work = ~0.3s at this budget)
bool g_psAssist = false;    int g_psAssistKey = 0;    int g_psAssistMode = 1;
int  g_psSetPointKey = 0;
int  g_psDeletePointKey = 0;
float g_psAssistRadius = 300.f;
float g_psAssistSnap = 48.f;
int  g_psAssistMaxPoints = 3;
float g_psAssistReuseDelay = 3.f;
bool g_psAssistRender = true;
bool g_psAssistJumpBox = true;
bool g_pixelJump = false;  int g_pixelJumpKey = 0;  int g_pixelJumpMode = 1;
bool g_pjUnsafe = false;
bool g_pjCrouch = false;
bool g_pjReadout = true;
bool g_pjHop = true;
bool  g_psGroundHelp = false;
float g_psGroundHelpAngle = 45.f;
float g_psLaunchZSlack = 4.f;   // half k_psCatchZ: an arc shifted less than that still
                                // lands inside the seam's own catch tolerance

// -- Route calculator -------------------------------------------------------------
bool g_routeCalc = false;
int  g_rcAddKey = 0;
int  g_rcDeleteKey = 0;
int  g_rcClearKey = 0;
int  g_rcSolveKey = 0;
int  g_rcMoveMask = 0x7F;     // rs::MoveBit: jump mini long jumpbug walk off / crouch walk off (start) delay hop
int  g_rcStartMoves = 0x37;   // botox m_route_calc_start_styles: jump mini long + walk off / crouch walk off
int  g_rcDelayTicks = 2;      // botox m_route_calc_delay_ticks (0-8)
bool g_rcShowKeys = true;     // botox m_route_calc_show_keys
int  g_rcPathAlpha = 255;     // botox m_route_calc_path_alpha
float g_rcPathThick = 2.f;    // botox m_route_calc_path_thickness (1-8 px)
bool g_rcAllowInvalid = false; // botox m_route_calc_allow_invalid
bool g_rcSnapEdge = false;    // botox m_route_calc_snap_edge
bool g_rcAdvanced = false;    // botox m_route_calc_advanced_readout
int  g_rcMaxShown = 8;
bool g_rcStartHere = true;
bool g_rcShowPath = true;
bool g_rcShowPanel = true;
int  g_rcPopupTime = 6;       // botox m_route_calc_popup_time

// -- Pixel calculator , split out of the route calculator --------------------------
bool g_pixelCalc = false;
int  g_pcPlaceKey = 0;
int  g_pcSolveKey = 0;
bool g_pcShowMarker = true;
bool g_pcShowPanel = true;
bool g_pcBuildings = false;
int  g_pcBuildMask = 15;

// -- Practice (practice.inl) -- console commands on a local server, sv_cheats 1 ------
bool g_prInfAmmo = false;
bool g_prInfHealth = false;
bool g_prInfTime = false;
int  g_prNoclipKey = 0;
int  g_prSaveKey = 0;
int  g_prTpKey = 0;
int  g_prBotEnemyKey = 0;
int  g_prBotMateKey = 0;
int  g_prSentryKey = 0;
int  g_prDispenserKey = 0;
int  g_prTeleporterKey = 0;
std::atomic<int> g_prMenuReq{0};
int  g_prBotClass = 5;        // index into k_prClassArg -- 5 = heavy (biggest dummy)

// -- ESP (src/hacks/aim/esp.cpp, extern list hacks/esp/esp_vars.h) -- botox defaults --
bool  g_espEnable = false;
bool  g_espPreview = false;   // visuals > esp "esp preview" checkbox, src/hacks/visuals/esp_preview.h
bool  g_espTeammates = false;
float g_espMaxDistance = 0.f;
bool  g_espDistanceFade = false;
bool  g_espSoundOnly = false;
float g_espSoundDuration = 1.5f;
bool  g_espSoundFade = true;
bool  g_espBox = false;
bool  g_espBoxVisColors = false;
float g_espBoxVisibleColor[4] = { 1.f, 1.f, 1.f, 1.f };
float g_espBoxInvisibleColor[4] = { 1.f, 0.353f, 0.353f, 1.f };
float g_espBoxColor[4] = { 1.f, 1.f, 1.f, 1.f };
bool  g_espBox3d = false;
bool  g_espBoxCorner = false;
float g_espBoxThickness = 1.f;
float g_espBoxRounding = 0.f;
bool  g_espBoxOutline = false;
float g_espBoxOutlineColor[4] = { 0.f, 0.f, 0.f, 1.f };
float g_espBoxOutlineThickness = 1.f;
int   g_espBoxOutlineSide = 2;
bool  g_espBoxFill = false;
float g_espBoxFillColor[4] = { 0.f, 0.f, 0.f, 0.314f };
bool  g_espBoxFillGradient = false;
float g_espBoxFillBottomColor[4] = { 0.f, 0.f, 0.f, 0.f };
bool  g_espName = false;
int   g_espNamePosition = 0;
bool  g_espNameVisColors = false;
float g_espNameVisibleColor[4] = { 1.f, 1.f, 1.f, 1.f };
float g_espNameInvisibleColor[4] = { 1.f, 0.353f, 0.353f, 1.f };
float g_espNameColor[4] = { 1.f, 1.f, 1.f, 1.f };
bool  g_espHealthBar = false;
int   g_espHealthBarSide = 0;
int   g_espHealthBarCornerPick = 0;
float g_espHealthBarCornerRounding = 6.f;
float g_espHealthBarCornerSize = 0.33f;
float g_espHealthBarThickness = 2.f;
bool  g_espHealthBarOutline = true;
bool  g_espHealthBarCustomColor = false;
float g_espHealthBarColor[4] = { 1.f, 1.f, 1.f, 1.f };
bool  g_espHealthBarGradient = false;
float g_espHealthBarBottomColor[4] = { 1.f, 0.f, 0.f, 1.f };
float g_espHealthBarBgColor[4] = { 0.f, 0.f, 0.f, 1.f };
bool  g_espHealthText = false;
bool  g_espHealthSuffix = false;
int   g_espHealthTextStyle = 0;
bool  g_espHealthTextCustomColor = false;
float g_espHealthTextColor[4] = { 1.f, 1.f, 1.f, 1.f };
bool  g_espWeaponName = false;
float g_espWeaponNameColor[4] = { 1.f, 1.f, 1.f, 1.f };
bool  g_espAmmoBar = false;
float g_espAmmoBarColor[4] = { 1.f, 1.f, 1.f, 1.f };
bool  g_espDistance = false;
int   g_espDistanceUnit = 0;
float g_espDistanceColor[4] = { 1.f, 1.f, 1.f, 1.f };
bool  g_espFlags = false;
int   g_espFlagList = 0x1F;   // uber, crits, cloaked, disguised, zoomed: what the old [uber][cloak][spy] row covered
float g_espFlagsColor[4] = { 1.f, 1.f, 1.f, 1.f };
float g_espFlagsAlertColor[4] = { 1.f, 0.353f, 0.353f, 1.f };
bool  g_espSkeleton = false;
bool  g_espSkeletonVisColors = false;
float g_espSkeletonVisibleColor[4] = { 1.f, 1.f, 1.f, 1.f };
float g_espSkeletonInvisibleColor[4] = { 1.f, 0.353f, 0.353f, 1.f };
float g_espSkeletonColor[4] = { 1.f, 1.f, 1.f, 1.f };
int   g_espSkeletonType = 0;
float g_espSkeletonThickness = 1.f;
bool  g_espHitboxes = false;
int   g_espHitboxesMode = 0;
float g_espHitboxColor[4] = { 1.f, 1.f, 1.f, 0.588f };
float g_espHitboxesThickness = 1.f;
float g_espHitboxesDuration = 2.f;
bool  g_espAvatar = false;
int   g_espAvatarPosition = 1;
float g_espAvatarSize = 14.f;
float g_espAvatarHeadScale = 1.f;
bool  g_espDamageNumbers = false;
float g_espDmgColor[4] = { 1.f, 1.f, 1.f, 1.f };
float g_espDmgDuration = 1.5f;
float g_espDmgSpeed = 20.f;
bool  g_espDmgStack = true;
bool  g_espOofArrows = false;
int   g_espOofType = 0;
float g_espOofColor[4] = { 1.f, 1.f, 1.f, 1.f };
float g_espOofWallColor[4] = { 1.f, 0.353f, 0.353f, 1.f };
float g_espOofSoundColor[4] = { 1.f, 0.627f, 0.f, 1.f };
float g_espOofSoundDuration = 1.5f;
bool  g_espOofSoundFade = true;
int   g_espOofStyle = 0;
float g_espOofWidth = 18.f;
float g_espOofHeight = 22.f;
int   g_espOofDistance = 200;
bool  g_espOofAvatar = false;
bool  g_espOofAspect = false;
bool  g_espSoundPings = false;
float g_espSoundColor[4] = { 1.f, 0.9f, 0.47f, 0.86f };
bool  g_espBuildings = false;
float g_espBuildingColor[4] = { 1.f, 0.353f, 0.353f, 1.f };
bool  g_espBldEnemyBox = true, g_espBldEnemyName = true, g_espBldEnemyHealthBar = true, g_espBldEnemyHealthText = false, g_espBldEnemyFlags = true, g_espBldEnemyDistance = false;
bool  g_espBldTeam = false;
bool  g_espBldTeamOwnOnly = false;
float g_espBldTeamColor[4] = { 0.47f, 0.67f, 1.f, 1.f };
bool  g_espBldTeamBox = true, g_espBldTeamName = true, g_espBldTeamHealthBar = true, g_espBldTeamHealthText = false, g_espBldTeamFlags = true, g_espBldTeamDistance = false;
bool  g_espWorldEnable = false;
bool  g_espAmmoPacks = true;
bool  g_espDroppedWeapons = true;
bool  g_espProjectiles = true;
bool  g_espProjPath = false;
float g_espProjPathColor[4] = { 1.f, 0.9f, 0.3f, 1.f };
float g_espItemColor[4] = { 1.f, 0.9f, 0.47f, 1.f };
float g_espProjColor[4] = { 1.f, 1.f, 1.f, 0.78f };

// -- chams (src/hacks/visuals/visuals.cpp) -- defaults OFF ------------------------------
bool  g_chamsEnable = false;
bool  g_chamsEnemy = true;
bool  g_chamsTeam = false;
float g_chamsEnemyColor[4] = { 1.f, 0.15f, 0.1f, 1.f };
float g_chamsTeamColor[4]  = { 0.1f, 0.5f, 1.f, 1.f };

// -- glow (src/hacks/visuals/visuals.cpp) -- defaults OFF --------------------------------
bool  g_glowEnable = false;
bool  g_glowEnemy = true;
bool  g_glowTeam = false;
float g_glowEnemyColor[4] = { 1.f, 0.05f, 0.05f, 0.6f };
float g_glowTeamColor[4]  = { 0.05f, 0.6f, 1.f, 0.6f };
bool  g_glowBuildings = false;
float g_glowBuildingColor[4] = { 1.f, 0.8f, 0.05f, 0.6f };
float g_glowAlpha = 1.f;

// -- 10-31 chams/glow rework (src/hacks/aim/chams.cpp), botox variables.h defaults --------
// Layer lists are config strings "mat:RRGGBBAA,..." (src/hacks/visuals/chams_layers.h). g_chamsTeam
// above is now the teammates group's own enable.
bool  g_chamsLocalEnable = true;
bool  g_chamsRagdolls = false;
float g_chamsSoundDuration = 1.f;
bool  g_chamsSoundFade = true;
float g_chamsSoundMinVolume = 0.f;
float g_chamsSoundMaxDistance = 0.f;
float g_chamsTwoToneThreshold = 0.5f;   // botox variables.h:604
float g_chamsTwoToneEdge = 0.f;         // botox variables.h:605
char  g_chamsVisibleCfg[160] = "";
char  g_chamsOccludedCfg[160] = "";
char  g_chamsSoundCfg[160] = "";
char  g_chamsTeamVisibleCfg[160] = "";
char  g_chamsTeamOccludedCfg[160] = "";
char  g_chamsWeaponCfg[160] = "";
char  g_chamsArmsCfg[160] = "";
char  g_chamsUberCfg[160] = "";        // enemy: replaces the visible list while ubered/bonked
char  g_chamsCloakCfg[160] = "";       // enemy: replaces the visible list while cloaked
char  g_chamsTeamUberCfg[160] = "";    // team: same pair for teammates
char  g_chamsTeamCloakCfg[160] = "";

float g_glowVisColor[4]        = { 224 / 255.f, 175 / 255.f, 86 / 255.f, 153 / 255.f };
float g_glowInvisColor[4]      = { 114 / 255.f, 155 / 255.f, 221 / 255.f, 153 / 255.f };
int   g_glowStyle = 0;     // 0 soft, 1 outline
float g_glowSize = 3.f;    // 1..10 soft blur / 1..6 outline px

// -- Skybox --------------------------------------------------------------------------------
// Only the ConVar handle is shared (thread.cpp resolves it). The sky-name and fog-colour
// redirect buffers are file-local to visuals.cpp now -- they are that file's scratch, not
// settings, and having them here is what let the g_offMins double definition happen.
ConVar* g_cv_sv_skyname = nullptr;

// -- Weather entity state lives in visuals.cpp (file-local). g_offMins / g_offMaxs have ONE
// owner, netvars.cpp: defining them here too made two COMDATs of the same symbol that /Gw
// folded silently, so which initialiser survived (-1 vs 0) was the linker's pick.

// -- Net stats -----------------------------------------------------------------------------------------------------------
std::atomic<float> g_fps  {0.f};
std::atomic<int>   g_ping {0};
std::atomic<int>   g_loss {0};
std::atomic<bool>  g_conn {false};

// -- WndProc -------------------------------------------------------------------------------------------------------------------
WNDPROC g_origWndProc = nullptr;

// ==== ICvar::ConsoleColorPrintf (slot 23) =====================================================
struct ConsoleColor4 { uint8_t r, g, b, a; };
using ConsoleColorPrintf_t = void(__fastcall*)(void*, const ConsoleColor4*, const char*, ...);

void ConsoleColorPrint(uint8_t r, uint8_t g, uint8_t b, const char* msg)
{
    if (!g_cvar) return;
    __try {
        auto fn = reinterpret_cast<ConsoleColorPrintf_t>((*reinterpret_cast<void***>(g_cvar))[23]);
        ConsoleColor4 col{ r, g, b, 255 };
        fn(g_cvar, &col, "%s", msg);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// ==== Debug log (persistent handle -- avoids Defender rescan stalls) =========================
// Lifted out of DebugLog's body so DebugLog_Shutdown below can reach the same object. Still a
// function-local static: the C++11 magic-static guard is what makes first use thread-safe, and
// it must NOT become a namespace-scope object (static init order vs the first log line).
namespace {
struct LogState {
    HANDLE           h;
    CRITICAL_SECTION cs;
    LogState() {
        InitializeCriticalSection(&cs);
        _mkdir("C:\\botox-tf2");
        h = CreateFileA("C:\\botox-tf2\\debug.log", GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
};
LogState& LogGet() { static LogState s; return s; }

// Early-line buffer. Every install diag runs in PollThread, seconds before anyone can reach
// the menu toggle, so with the log off they went nowhere -- which is why scoreboard and
// lagres each grew a bespoke "replay my one line later" hack. This is that hack once, for
// every call site. CAPPED on purpose: the per-tick movement/TB diags would otherwise pay a
// format on every call forever. It fills, and the off-path is two bool tests again.
constexpr int kEarlyLines = 96;
constexpr int kEarlyLen   = 512;
char s_early[kEarlyLines][kEarlyLen];
int  s_earlyCount   = 0;
bool s_earlyFlushed = false;
}

void DebugLog(const char* fmt, ...)
{
    // Master gate, default off. Every call site stays compiled -- the per-tick movement / TB /
    // PS diags are still the only way to debug those features, and they come back the moment
    // the menu toggle flips. Off, this costs two bool tests and no formatting.
    const bool on = g_debugLogEnable;
    if (!on && (s_earlyFlushed || s_earlyCount >= kEarlyLines)) return;

    // DO NOT SHRINK THIS BUFFER. : at 512 the diag stopped mid-field at `dface=` and all
    // 1118 TB lines came out exactly 510 chars, so the six numbers that build shipped to measure
    // never reached the file -- an instrument that cannot answer is worse than none, because the
    // missing line reads as a fact. : the BUILD tag was over the ceiling for several builds
    // (~7800 chars) and every one landed cut off mid-sentence. One stack buffer, no hot path.
    char buf[8192];
    va_list ap; va_start(ap, fmt);
    int n = _vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    // -1 means it did not fit. `if (n <= 0) return` DROPPED those lines silently, and exactly
    // the longest ones -- the movement diags. Truncate and log instead of losing it.
    if (n < 0) n = (int)sizeof(buf) - 2;
    if (n == 0) return;
    buf[n]   = '\n';
    buf[n+1] = '\0';

    LogState& s_log = LogGet();
    if (s_log.h == INVALID_HANDLE_VALUE) return;

    EnterCriticalSection(&s_log.cs);
    if (!on) {
        // log is off: park the line for DebugLog_FlushEarly. Re-tested under the lock, another
        // thread can have filled the last slot between the gate above and here.
        if (!s_earlyFlushed && s_earlyCount < kEarlyLines) {
            int c = (n + 1 < kEarlyLen) ? n + 1 : kEarlyLen - 1;   // n+1 keeps the newline
            memcpy(s_early[s_earlyCount], buf, (size_t)c);
            s_early[s_earlyCount][c] = '\0';
            ++s_earlyCount;
        }
    }
    // Re-tested INSIDE the lock: DebugLog_Shutdown closes the handle under this same lock, so
    // the check above can be stale by the time we get here.
    else if (s_log.h != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        WriteFile(s_log.h, buf, (DWORD)(n + 1), &w, nullptr);
    }
    LeaveCriticalSection(&s_log.cs);
}

// Called the moment the menu toggle goes on. Dumps everything that happened while the log was
// off -- which is every hook install, every sig that did NOT resolve, and the whole startup
// order. Once is enough: after this the buffer stays closed and the off-path goes cheap again.
void DebugLog_FlushEarly()
{
    if (!g_debugLogEnable) return;

    LogState& s_log = LogGet();
    EnterCriticalSection(&s_log.cs);
    if (!s_earlyFlushed && s_log.h != INVALID_HANDLE_VALUE) {
        s_earlyFlushed = true;
        static const char hdr[] = "---- replay: logged before the log was on ----\n";
        DWORD w = 0;
        WriteFile(s_log.h, hdr, (DWORD)(sizeof(hdr) - 1), &w, nullptr);
        for (int i = 0; i < s_earlyCount; ++i)
            WriteFile(s_log.h, s_early[i], (DWORD)strlen(s_early[i]), &w, nullptr);
        static const char end[] = "---- replay end ----\n";
        WriteFile(s_log.h, end, (DWORD)(sizeof(end) - 1), &w, nullptr);
    }
    LeaveCriticalSection(&s_log.cs);
}

// The handle used to live for the life of the process. Under a soft eject that is a handle
// held on a file the user is about to read, for no reason. Closing it makes every later
// DebugLog a no-op, which is what we want once the hooks are down.
//
// The CRITICAL_SECTION is NOT deleted and must not be: a hook that slips past the drain and
// logs one last line would then enter a deleted lock, which is a worse fault than the ~40
// bytes it holds. The object is a function-local static, so it outlives everything anyway.
void DebugLog_Shutdown()
{
    LogState& s_log = LogGet();
    EnterCriticalSection(&s_log.cs);
    if (s_log.h != INVALID_HANDLE_VALUE) { CloseHandle(s_log.h); s_log.h = INVALID_HANDLE_VALUE; }
    LeaveCriticalSection(&s_log.cs);
}
