#pragma once
#include "types.h"
#include <unordered_map>

// Extern declarations for globals defined in dllmain.cpp.

// -- Core state ------------------------------------------------------------------
extern ICvar*             g_cvar;
extern HANDLE             g_hPollThread;
extern IEngineClient*     g_engine;
extern void*              g_gameEventManager;
extern void*              g_matSystem;
extern void*              g_modelRender;
extern void*              g_modelInfo;
extern void*              g_renderView;   // IVRenderView "VEngineRenderView014" -- SetBlend/GetBlend
extern IClientEntityList* g_entityList;
extern HWND               g_hwnd;
extern bool               g_imguiInit;
extern IDirect3DDevice9*  g_d3dDev;   // live device, set in h_Present (blend state)
extern std::atomic<bool>  g_eject;
extern std::atomic<int>   g_hookDepth;
extern std::atomic<int>   g_miscHookDepth;
extern WNDPROC            g_origWndProc;

// Eject drains these to zero before MH_Uninitialize() frees the trampolines -- a thread
// parked inside a detour otherwise returns into freed memory. EVERY hook must be counted;
// for a long time only Present/DrawSkyBox/CalcViewModelView were, and the two hottest
// hooks in the DLL (SetupBones, DrawModelExecute) were not.
// MSVC forbids an object with a destructor in a function that also uses __try (C2712),
// so a hook that needs SEH keeps its body in an _impl and puts the guard in a thin wrapper.
struct MiscHookGuard {
    MiscHookGuard()  { ++g_miscHookDepth; }
    ~MiscHookGuard() { --g_miscHookDepth; }
};

// -- Menu ------------------------------------------------------------------------
extern bool g_showMenu;
extern int  g_menuKey;
extern ImFont* g_fontBold;
extern ImFont* g_fontReg;      // Tahoma 13 regular -- the old io.FontDefault, named now that
                                // verdana 11 took the default slot (botox menu spec section 6)
extern ImFont* g_fontIcon13;   // botox icon font 13 -- OptionPopup cog
extern ImFont* g_fontMi;       // movement indicator tags, rasterized AT g_miScale px
extern ImFont* g_fontClarityInter; // clarity v2 watermark body (inter_semibold, embedded)
extern ImFont* g_fontClarityIcon;  // clarity v2 watermark icon glyph (clarity_icon_font, embedded)
extern ImFont* g_menuReg;      // botox menu chrome only: verdana 11 (menu_chrome.h/menu.cpp)
extern ImFont* g_menuBold;     // botox menu chrome only: verdana bold 11

// -- Watermark/spectator/media donor fonts: botox render.h e_font_names, same face, same px,
//    same FreeType flags (render.cpp BuildFontAtlas). Name = botox font_name_* minus prefix.
extern ImFont* g_fontTahoma12;           // tahoma 12  Monochrome|MonoHinting
extern ImFont* g_fontTahomaBd12;         // tahoma bd 12  Monochrome|MonoHinting
extern ImFont* g_fontChillware13;        // verdana bd 13  NoHinting
extern ImFont* g_fontLobotomy13;         // sf_pro_display_semibold 13  Bitmap
extern ImFont* g_fontLobotomyBold13;     // sf_pro_display_bold 13  Bitmap
extern ImFont* g_fontKamibebra13;        // rubik_regular 13  Bitmap
extern ImFont* g_fontKamibebraBold13;    // rubik_medium 13  Bitmap|LightHinting
extern ImFont* g_fontCumhacck35;         // verdana bd 35  Monochrome|MonoHinting
extern ImFont* g_fontAirplaneTitle;      // montserrat_regular 10
extern ImFont* g_fontAirplaneRows;       // tahoma bd 11  MonoHinting
extern ImFont* g_fontInterBold15;        // inter_bold 15
extern ImFont* g_fontInterMedium15;      // inter_medium 15
extern ImFont* g_fontSunflower30;        // sunflower_medium 30 (aimware, drawn at 15/14)
extern ImFont* g_fontInterwebzCalibri;   // calibri 16  Monochrome|MonoHinting
extern ImFont* g_fontHavocTahoma13;      // tahoma 13  Monochrome|MonoHinting
extern ImFont* g_fontAirflow14;          // sf_ui_display_semibold 14
extern ImFont* g_fontEvolve16;           // segoeui 16  NoHinting
extern ImFont* g_fontEvolveBold16;       // segoeuib 16  NoHinting
extern ImFont* g_fontLegendwareVerdana12;// verdana 12  MonoHinting
extern ImFont* g_fontLegendwareLucida10; // lucon 10  Monochrome|MonoHinting
extern ImFont* g_fontInteriumDroid24;    // droid_sans_bold 24 (drawn at 16)
extern ImFont* g_fontSkebobNunito14;     // nunito_medium 14
extern ImFont* g_fontSkebobArial12;      // arial 12 (default flags)
extern ImFont* g_fontSkebobArial13;      // arial 13 (default flags)
extern ImFont* g_fontCumidereTahoma16;   // tahoma 16  LightHinting
extern ImFont* g_fontIllusoryTahoma13;   // tahoma 13  MonoHinting|Bold
extern ImFont* g_fontLumiRubik16;        // rubik_regular 16  MonoHinting
extern ImFont* g_fontLumiProggy13;       // ImGui ProggyClean  MonoHinting|Monochrome
extern ImFont* g_fontInbaTahoma13;       // tahoma 13  NoHinting
extern ImFont* g_fontDnaMontserrat15;    // dna_montserrat_medium 15  NoHinting
extern ImFont* g_fontDnaPtRootBold15;    // pt_root_ui_bold 15  NoHinting
extern ImFont* g_fontDnaClarityIcon50;   // dna_clarity_icon 50  NoHinting
extern ImFont* g_fontCucumberTahoma14;   // tahoma 14  NoHinting
extern ImFont* g_fontCucumberVerdana14;  // verdana 14  NoHinting (cucumber measures the user with it)
extern ImFont* g_fontHowewewareMinecraft14; // minecraft ttf 14  NoHinting
extern ImFont* g_fontGamesenseSfBold15;  // sf_pro_display_bold 15  Bitmap (drawn at 13)

// -- Menu settings tab (stage 4: appearance + safety) ------------------------------
extern float g_accentColor[4]; // live ImGuiCol_Accent, pushed every frame in RenderMenu
extern float g_dpiScale;       // whole-menu scale (style.ScaleAllSizes + FontGlobalScale)
extern bool  g_safeMode;       // master gate for risky hooks -- config var only this pass

// -- Watermark (simplified) --------------------------------------------------------
extern bool  g_wmEnable;
extern bool  g_wmShowLabel;
extern char  g_wmLabel[64];
extern bool  g_wmShowFPS;
extern bool  g_wmShowPing;
extern bool  g_wmShowLoss;
extern bool  g_wmShowTime;
extern int   g_wmPos;
extern float g_wmTextColor[4];

// -- Media player (Windows "now playing", hacks/hud/mediaplayer.cpp) -----------------
// botox misc.cpp draw_media_player: look + fonts follow g_wmStyle, knobs = botox's own.
extern bool g_mpEnable;
extern bool g_mpCoverArt;      // m_media_player_cover_art
extern bool g_mpBackground;    // m_media_player_background
extern bool g_mpProgressBar;   // m_media_player_progress_bar
extern bool g_mpSimple;        // m_media_player_simple -- one line "title - artist"
extern bool g_mpTime;          // m_media_player_time -- "precise time" 1:23 / 4:56 row
extern int  g_mpPos;           // WatermarkPos; under the watermark box when it shares the column

// -- Unit E: misc HUD (hit marker/sound, death particles, points, spectator list,
//    watermark style skins, discord rpc, debug menu, console color) -----------------
extern bool  g_hmEnable;
extern float g_hmColor[4];
extern float g_hmSize;

extern bool  g_dpEnable;
extern int   g_dpStyle;        // botox death_particles_type: 0 blood 1 sparks(->blood) 2 confetti
                                // 3 smoke 4 explosion 5 soul 6 balls 7 melt(BLOCKED, falls to blood) 8 mc
extern float g_dpColor[4];
extern bool  g_dpMulticolor;
extern int   g_dpAmount;       // soul/smoke/balls particle count per spawn point
extern float g_dpSize;
extern float g_dpLifetime;
extern int   g_dpSoulStyle;    // 0 fountain 1 rise 2 burst 3 converge-to-core
extern int   g_dpMcMode;       // 0 crits only 1 crits+xp 2 xp only
extern float g_dpMcXpTime;

extern bool  g_ptsEnable;

extern bool  g_specEnable;
extern bool  g_specAlwaysShow;
extern int   g_specStyle;
extern int   g_specPos;      // WatermarkPos
extern float g_specOffX;     // screen-px drag offset on top of the g_specPos anchor
extern float g_specOffY;
extern bool  g_specShowTarget;     // botox m_spectators_list_show_target
extern bool  g_specDelusionalLeft; // botox m_spectators_list_delusional_left
extern int   g_specListWidth;      // botox m_spectators_list_width (interwebz style)
extern float g_specColorOne[4];    // botox text_color_one -- row watching the local player
extern float g_specColorTwo[4];    // botox text_color_two -- row watching someone else

// 0 default (watermark.cpp's own body), 1-20 = botox's own style index (watermark_styles.cpp).
// Not every index is ported yet -- RenderWatermarkStyled() falls through to the default body
// for any index it does not implement, same as it already did for the untouched defaults.
extern int   g_wmStyle;

extern bool  g_discordEnable;
extern char  g_discordAppId[32];
extern char  g_discordState[128];
extern char  g_discordDetails[128];
extern char  g_discordImageKey[128];

extern bool  g_debugMenuEnable;
extern float g_consoleColor[4];   // ConsoleColorPrint tint (rgb; a unused, FLOAT4 config slot)

// -- Movement indicators (centered stack under the crosshair) ----------------------
extern bool  g_miEnable;
extern bool  g_miHorizontal;   // one centred row instead of the vertical stack
extern bool  g_miMitosis;      // tags divide off each other, letters morphing (migoo.h)
extern int   g_miShowMask;     // which tags may show, bit per MiSlot (coldtires m_key_indicators)
extern float g_miOffsetY;      // px up from the BOTTOM edge (coldtires key_indicators_position)
extern float g_miScale;        // text scale on top of the bold menu font
extern float g_miIdleColor[4];
extern float g_miActiveColor[4];
// velocity indicator (coldtires indicators.cpp velocity)
extern bool  g_viEnable;
extern bool  g_viPreSpeed;      // " (takeoff speed)" after the number while airborne + 2s
extern bool  g_viFadeAlpha;     // alpha = speed / 250
extern bool  g_viCustomColor;   // color1->color2 blend by speed instead of same/down/up colours
extern float g_viOffsetY;       // px up from the BOTTOM edge
extern float g_viColor1[4], g_viColor2[4], g_viColor3[4], g_viColor4[4], g_viColor5[4];

// -- Viewmodel ---------------------------------------------------------------------
extern bool  g_vmSwayEnable;
extern float g_vmSwayScale;
extern float g_vmSwayInterp;
extern bool  g_vmOffsetEnable;
extern float g_vmOffsetX;
extern float g_vmOffsetY;
extern float g_vmOffsetZ;
extern bool  g_vmRemoveBob;
extern bool  g_vmCsBob;           // CS:GO bob replaces TF's (vm_csgo.h)
extern float g_vmCsBobCycle, g_vmCsBobUp, g_vmCsBobVert, g_vmCsBobLat, g_vmCsBobLower;
extern bool  g_vmCsGunLower;      // CS:GO gun drops while airborne
extern float g_vmCsGunLowerAngle, g_vmCsGunLowerSpeed;
extern bool  g_vmCsLandDip;       // CS:GO landing dip, viewmodel only
extern float g_vmCsLandDipAmt, g_vmCsLandDipTime;
// transparent viewmodel -- alpha the weapon + arms materials get drawn with. 1 = opaque.
extern bool  g_vmAlphaEnable;
extern float g_vmAlpha;
// flat alpha: force $basemapalphaphongmask so the shader stops multiplying the weapon's
// base-texture alpha (its phong mask, mostly dark) into the blend. Costs the normal map.
extern bool  g_vmFlatAlpha;
// low fps viewmodel (hacks/visuals/viewmodel_fps.h): bones held in camera space, re-read
// g_vmLowFpsRate times a second, re-attached to the live camera every frame.
extern bool  g_vmLowFps;
extern float g_vmLowFpsRate;

// -- Fog -----------------------------------------------------------------------------
extern bool  g_fogEnable;
extern float g_fogColor[4];   // alpha unused by engine, kept for picker
extern float g_fogStart;
extern float g_fogEnd;
extern float g_fogDensity;

// -- Screen effects (hacks/screen/screenfx.cpp) ---------------------------------------------
// The shared depth source and its readback probe. Ambient occlusion and depth of field
// hang off the same resolved z buffer.
// Log what the depth resolve actually produced, every ~5s. The resolve itself only runs on
// the frames this reads, so a ticked probe with ao and dof off costs nothing per frame.
extern bool g_fxDepthProbe;

// Hand the passes to the material system's own call queue (ICallQueue) instead of forcing
// mat_queue_mode 0. Queued from the game's post slot, they replay on the render thread at
// that exact frame position -- where the depth is still live -- and multicore rendering
// stays on. This is what makes ao/dof cost their own passes and nothing else.
extern bool g_fxUseCallQueue;

// mat_queue_mode 0 while AO OR DOF is on -- not for the probe, which must not change what
// it measures. Multicore rendering moves the game's own screen-space slot off the render
// thread, and the depth surface bound where the passes then have to land holds nothing --
// measured: every probe tap came back at the far plane. Off = the passes still draw, but
// off a dead depth buffer. This is the most expensive switch in the DLL (~100fps).
extern bool g_fxForceQueueMode;

// PARTICLES ARE NOT IN THE DEPTH BUFFER. Crits, muzzle flashes, explosions and every other
// translucent thing draw with z writes off, so the depth under a crit spark is still the
// wall behind it -- and the blur reads that wall and smears the spark with it. The fix is a
// copy of the frame taken the instant the engine finishes opaque geometry (its own
// UpdateFullScreenDepthTexture, caught at the d3d level in h_SetRenderState): anything that
// changed between that copy and the finished frame was painted by something with no depth,
// and is held sharp. 0 = off, 1 = fully sharp.
extern float g_fxParticleGuard;

// Ambient occlusion (hacks/screen/ssao.cpp) -- three fullscreen ps_3_0 passes off that depth.
extern bool  g_aoEnable;
extern float g_aoIntensity;    // exponent on the term, 1 = as gathered
extern float g_aoRadius;       // world units the gather reaches
extern int   g_aoSamples;      // taps per pixel, 4..64
extern float g_aoBias;         // angle bias, throws away taps too close to the surface plane
extern float g_aoBlur;         // bilateral blur radius, in occlusion texels
extern float g_aoScale;        // gather resolution, 0.25..1 of the frame
extern float g_aoFadeStart;    // units where the term starts fading out
extern float g_aoFadeEnd;      // and where it is gone
extern bool  g_aoViewmodel;    // keep the gun out of it (raw depth < 0.1 is the viewmodel)
extern bool  g_aoFogFade;      // stop the term where dih's own fog override has taken over
extern bool  g_aoDebug;        // draw the term instead of multiplying it into the frame

// Depth of field (hacks/screen/dof.cpp) -- up to five passes off the same depth.
extern bool  g_dofEnable;
extern bool  g_dofAutoFocus;      // focus on whatever the crosshair is pointing at
extern float g_dofDistance;       // manual focal distance, units, when auto focus is off
extern float g_dofFocusSpeed;     // how fast auto focus pulls, per second
extern float g_dofFocusLimit;     // furthest auto focus will pull to
extern float g_dofRange;          // sharp band, centred on the focus
extern float g_dofRamp;           // units the blur ramps in over past that band
extern float g_dofMaxBlur;        // biggest blur radius, in pixels
extern float g_dofTapDensity;     // gather taps per pixel of blur radius, 0.5..2.5
extern float g_dofBokehBoost;     // how hard a highlight pulls
extern float g_dofBokehThreshold; // luma a highlight starts pulling at
extern bool  g_dofNearBlur;       // blur what is in FRONT of the focus, with the soft edge
extern bool  g_dofViewmodelSharp; // on: hold the gun sharp. off: the gun gets its OWN blur
                                  // pass after it is drawn (IVRenderView::ViewDrawFade)
extern bool  g_dofSmooth;         // the separable gaussian that closes the spiral's gaps

// Frame blending (hacks/screen/frameblend.cpp) -- real motion blur off the same depth: a velocity
// buffer, a tile chain, and a reconstruction gather. Runs twice a frame -- the world and the
// players in the game's post slot, the gun from ViewDrawFade once it has been drawn.
extern bool  g_fbEnable;
extern float g_fbShutter;     // how long the shutter stays open, ms. The whole look lives here.
extern float g_fbAmount;      // the world's share of it, 0 = camera and players sharp
extern bool  g_fbPlayers;     // other players get their own velocity, not just the camera's
extern bool  g_fbViewmodel;   // the gun's animation, off its bone matrices (hacks/screen/fbbones.h)
extern float g_fbVmAmount;    // and how much of it, 0..64 -- animation streaks are small
extern int   g_fbSamples;     // tap CAP, 4..64. The streak picks the count; this is the ceiling.
extern float g_fbMaxStreak;   // longest streak, as a fraction of screen height

// Bloom (hacks/screen/bloom.cpp) -- the bright part of the frame bled into a glow. No depth,
// no view setup: it reads the finished frame and nothing else, so it works on any card
// that takes a ps_2_0 shader.
extern bool  g_bloomEnable;
extern float g_bloomIntensity;   // how much glow is added, 0..3
extern float g_bloomThreshold;   // luma a pixel has to reach before it glows, 0..1
extern float g_bloomRadius;      // how far it spreads, 0..1 (1..6 blur passes, widening)
extern float g_bloomSat;         // colour of the glow, 0 = white, 1 = as lit, >1 = pushed

// -- Color correction (fullscreen post-process -- hacks/screen/colorcorrect.cpp) ------------
// Applied in this order, which is fpsoverlay's unchanged:
//   gain+exposure -> temperature -> levels in + gamma -> saturation -> hue ->
//   curves -> levels out -> highlights/shadows -> contrast
extern bool  g_ccEnable;
extern float g_ccGainR;        // per-channel gain   [0..2],  1 = neutral
extern float g_ccGainG;
extern float g_ccGainB;
extern float g_ccExposure;     // EV                 [-3..3], 0 = neutral
extern float g_ccTemp;         // warm/cool          [-1..1], 0 = neutral
extern float g_ccHue;          // degrees            [-180..180]
extern float g_ccSat;          // saturation         [0..3],  1 = neutral
extern float g_ccContrast;     // contrast           [0.5..1.5]
extern float g_ccLevInBlack;   // levels input black [0..1]
extern float g_ccLevInWhite;   // levels input white [0..1]
extern float g_ccGamma;        // levels gamma       [0.1..10]
extern float g_ccLevOutBlack;  // levels output black
extern float g_ccLevOutWhite;  // levels output white
extern float g_ccHighlights;   // highlight lift     [-1..1]
extern float g_ccShadows;      // shadow lift        [-1..1]
extern CcCurve g_ccCurve[4];   // 0 = master, 1 = R, 2 = G, 3 = B

// -- Unit D: simple screen post-fx (hacks/screen/screenfx_passes.cpp), ported from botox's
// hacks/visuals/screen/*.cpp -- one shader, appended into the already-shared screen-fx
// pipeline (no new hook). Each pass is identity at amount 0 except show depth, which
// overrides the rest outright while it is on.
extern bool  g_sharpenEnable;
extern float g_sharpenAmount;
extern bool  g_filmGrainEnable;
extern float g_filmGrainAmount;
extern bool  g_vignetteEnable;
extern float g_vignetteAmount;
extern bool  g_vignetteInvert;
extern bool  g_invertFilterEnable;
extern bool  g_posterizeEnable;
extern float g_posterizeLevels;
extern bool  g_debandEnable;
extern float g_debandAmount;
extern bool  g_channelShiftEnable;
extern float g_channelShiftAmount;
extern bool  g_emphasizeEnable;
extern float g_emphasizeAmount;
extern bool  g_emphasizeSpherical;
extern bool  g_showDepthEnable;   // debug view -- reuses screenfx.cpp's resolved depth texture
// botox depth_view knobs (screenfx_passes.cpp ScreenFxPasses_DepthView, drawn from the post slot)
extern float g_emphasizeFocusDepth;
extern float g_emphasizeFocusRange;
extern float g_emphasizeFocusEdge;
extern int   g_emphasizeSphereFov;
extern float g_emphasizeSphereX;
extern float g_emphasizeSphereY;
extern float g_emphasizeBlendColor[4];
extern float g_emphasizeBlendFactor;
extern int   g_showDepthType;
extern bool  g_showDepthBlend;
extern bool  g_showDepthLive;
extern bool  g_showDepthUpsideDown;
extern bool  g_showDepthReversed;
extern bool  g_showDepthLog;
extern float g_showDepthScaleX;
extern float g_showDepthScaleY;
extern int   g_showDepthOffsetX;
extern int   g_showDepthOffsetY;
extern float g_showDepthFarPlane;
extern float g_showDepthMultiplier;

// -- Unit D: world fx (hacks/visuals/world_fx.cpp) -------------------------------------------
extern bool  g_fullbrightEnable;     // mat_fullbright poke, no hook
extern bool  g_disableInterpEnable;  // cl_interp 0 poke, no hook
extern bool  g_aspectRatioEnable;    // letterbox/pillarbox via the existing Present dest rect
extern float g_aspectRatioValue;     // target width/height, e.g. 1.333 for 4:3

// IClientMode::OverrideView, vtable slot 16 -- verified against Aletherium/Amalgam's own
// Hooks/CClientModeShared_OverrideView.cpp (same slot, same CViewSetup* param, already
// used there to drive FOV + third person on this engine branch). New hook, see world_fx.cpp.
extern bool  g_worldFovEnable;
extern float g_worldFovAmount;       // degrees, overrides CViewSetup::fov outright
extern bool  g_thirdPersonEnable;
extern float g_thirdPersonDistance;  // units behind the eye. no wall-collision sub-toggle yet

// -- Unit D2: world fx 2 (hacks/visuals/world_fx2.cpp) ---------------------------------------
extern bool  g_removeVisualRecoilEnable;   // CBasePlayer::CalcView, zeroes m_vecPunchAngle

extern bool  g_dlightEnable;               // IVEfx::CL_AllocDlight, keyed to the local player
extern float g_dlightRadius;
extern float g_dlightColor[4];

extern bool  g_sunAngleEnable;             // env_sun's DT_Sun::m_vDirection (glow sprite only)
extern float g_sunAnglePitch;
extern float g_sunAngleYaw;

extern bool  g_worldTintEnable;            // IMaterial::ColorModulate, TEXTURE_GROUP_WORLD
extern float g_worldTintColor[4];
extern bool  g_propTintEnable;             // IMaterial::ColorModulate, TEXTURE_GROUP_MODEL
extern float g_propTintColor[4];
extern bool  g_weatherTintEnable;          // rain/snow sprite material, same ColorModulate
extern float g_weatherTintColor[4];

// -- Skybox ----------------------------------------------------------------------------
extern bool  g_skyEnable;
extern char  g_skyName[64];

// -- Unit D3: reflections (hacks/visuals/reflections.cpp), ported from botox's
// hacks/visuals/screen/reflections.cpp (single-pass version, see fx_refl.hlsl). Reads
// screenfx.cpp's already-resolved depth texture, no new hook.
extern bool  g_reflEnable;
extern float g_reflIntensity;   // 0..1, schlick fresnel scale
extern float g_reflDistance;    // 0..1, maps to a 300..3000 unit ray budget

// -- Unit D3: minecraft sky + clouds (hacks/visuals/mc_world.cpp), ported from botox's
// hacks/visuals/screen/mc_clouds.cpp + hacks/visuals/edicts/mc_sky.h as a screen pass over
// sky pixels (same depth source), not a baked skybox replacement.
extern bool  g_mcWorldEnable;

// -- Weather (rain=1, snow=2) ------------------------------------------------------------
extern bool  g_weatherEnable;
extern int   g_weatherMode;
extern float g_rainAlpha;
extern float g_rainSpeed;
extern float g_rainWidth;
extern float g_rainLength;
extern float g_rainRadius;
extern float g_rainSideVel;
extern float g_snowAlpha;
extern float g_snowSpeed;
extern float g_snowWidth;
extern float g_snowLength;
extern float g_snowRadius;
extern float g_snowSideVel;
extern float g_snowDensity;

// -- No hats -------------------------------------------------------------------------------
extern bool g_noHats;
extern bool g_noUnusual;

// -- Killfeed --------------------------------------------------------------------------------
extern bool g_selfKillfeedOnly;

// -- Scoreboard ------------------------------------------------------------------------------
extern bool g_revealScoreboard;   // enemy class icons + enemy health in the match hud panels
extern bool g_steamHistory;       // "Steam History" item in the scoreboard right-click menu

// -- Crosshair (simplified) --------------------------------------------------------------------
extern bool  g_chEnable;
extern bool  g_chAntialias;
extern bool  g_chAdditive;
extern int   g_chBlendMode;   // kChBlend id; 0 = normal, or additive when an old cfg set g_chAdditive
extern float g_chColor[4];
extern float g_chThickness;   // units, 1 unit = 2 px
extern float g_chLength;
extern float g_chGap;
extern float g_chAspect;      // arm length w:h -- 1 = square, >1 wider, <1 taller
extern float g_chGapAspect;   // gap w:h, same rule
extern bool  g_chDot;
extern bool  g_chDotCircle;
extern bool  g_chOutline;
extern float g_chOutlineColor[4];
extern float g_chOutlineThick;
extern bool  g_staticAmbyCrosshair;

// -- CS:GO sniper scope (scope_csgo.inl, cs_hud_scope.cpp port) --------------------------------
extern bool  g_scCsgo;            // replace TF's scope, hides every crosshair while zoomed
extern int   g_scCsgoWeapon;      // inaccuracy stats: 0 AWP, 1 SSG 08, 2 SCAR-20/G3SG1
extern int   g_scCsgoZoom;        // 0 = zoom fov 1, 1 = zoom fov 2 (clamped 25)
extern int   g_scCsgoWidth;       // cl_crosshair_sniper_width
extern bool  g_scCsgoNormalInacc; // cl_crosshair_sniper_show_normal_inaccuracy
extern float g_scCsgoMoveSens;    // ground speed inaccuracy scale, 1 = csgo
extern float g_scCsgoAirSens;     // jump/air/land inaccuracy scale, 1 = csgo
extern bool  g_scCsgoScopeIn;     // unscoped penalty carries into the zoom (csgo)
extern bool  g_scCsgoBob;         // scope offset by csgo raw bob
extern float g_scCsgoBobScale;
extern bool  g_scCsgoLens;        // dust lens overlay
extern float g_scCsgoScale;       // arc box + lens size about the centre, 1 = csgo
extern bool  g_scCsgoNoDot;       // hide your own sniper dot (CSniperDot::GetRenderingPositions)
extern bool  g_scChargeHide;      // hide TF's scope charge meter (any scope, own toggle)
extern float g_scCsgoOpacity;     // whole scope alpha (surface alpha multiplier)
extern float g_scCsgoBobLat, g_scCsgoBobVert;   // sway per axis, 1 = csgo bob amounts

// -- CSGO default crosshair (panorama hudreticle port) -----------------------------------------
extern bool  g_chCsgoEnable;
extern int   g_chCsgoStyle;    // 0 = default (dynamic), 1 = default static
extern float g_chCsgoColor[4]; // wash-color for pips/arcs/dot -- csgo crosshairColor1 #82b116
extern float g_chCsgoGap;      // gap offset, added to GetReticleCrosshairGap
extern float g_chCsgoScale;    // whole-reticle zoom, 1.0 = csgo's own 1:1 screen pixels
extern bool  g_chCsgoCrisp;    // point-sample the art when zoomed (no bilinear mush)
extern bool  g_chCsgoLinear;   // blend in linear light, like panorama's sRGB render target
extern bool  g_chCsgoAdditive; // pips/arcs/dot ADD (csgo). off = plain alpha, no white-out
extern float g_chCsgoBlack;    // scales both black washes; 1 = csgo (#000000AA / #00000088)
extern float g_chCsgoVelSens;  // scales every velocity-driven gap/spread term, 1 = csgo
extern float g_chCsgoSmooth;   // ease time (s) on gap + spread, 0 = csgo's hard snap
extern bool  g_chCsgoArcs;     // draw the 4 animated spread lines (style 0)
extern bool  g_chCsgoPips;     // draw the 4 green gap arms (styles 0/1)
extern float g_chCsgoAspect;   // arm length w:h -- pips on 0/1, bar size on 2/3/4
extern float g_chCsgoGapAspect;// gap w:h -- pip offset on 0/1, bar distance on 2/3/4
// -- CSGO classic crosshair (styles 2/3/4, CWeaponCSBase::DrawCrosshair) ----------------
extern float g_csXSize;        // cl_crosshairsize
extern float g_csXThick;       // cl_crosshairthickness
extern float g_csXGap;         // cl_crosshairgap
extern bool  g_csXDot;         // cl_crosshairdot
extern bool  g_csXOutline;     // cl_crosshair_drawoutline
extern float g_csXOutlineTh;   // cl_crosshair_outlinethickness
extern bool  g_csXT;           // cl_crosshair_t
extern int   g_csXAlpha;       // cl_crosshairalpha
extern bool  g_csXUseAlpha;    // cl_crosshairusealpha (off = additive bars, alpha 200)
extern float g_csXSplitDist;   // cl_crosshair_dynamic_splitdist
extern float g_csXSplitRatio;  // cl_crosshair_dynamic_maxdist_splitratio
extern float g_csXInnerMod;    // cl_crosshair_dynamic_splitalpha_innermod
extern float g_csXOuterMod;    // cl_crosshair_dynamic_splitalpha_outermod

// -- Convenience (region selector + net settings) ----------------------------------------------
extern bool g_regionEnable;
extern int  g_regionMask;
extern bool g_netSettingsEnable;
extern int  g_netSettingsSel;
extern bool g_netSettingsRates;   // also set rate/cl_cmdrate/cl_updaterate (default off)
extern bool  g_ngColorEnable;
extern float g_ngTextColor[4];
extern float g_ngOrangeColor[4];
extern float g_ngRedColor[4];
extern float g_ngYellowColor[4];
extern bool  g_ngPosEnable;     // move TF2's net graph (engine has no y knob at all)
extern int   g_ngPosX;          // free px offset, + = right, unclamped (off screen is fine)
extern int   g_ngPosY;          // free px offset, + = down,  unclamped

// -- Equip region unlock -----------------------------------------------------------------------
extern bool  g_equipRegionUnlock;

// -- Skinchanger (hacks/visuals/skins.cpp) ------------------------------------------------------------
// One SkinCfg per weapon, keyed by the REDIRECTED def index (Skin_RedirectIndex): every
// festive/reskin variant of a weapon shares its base weapon's entry, so picking a skin on
// "Bat" also covers the Batsaber you are holding.
extern bool g_enableSkins;
extern bool g_skinNeedsUpdate;      // menu edited something -> re-resolve models + full update
extern std::unordered_map<int, SkinCfg> g_skinCfgs;

void Skins_Install();               // FrameStageNotify + SetSequence hooks (thread.cpp)
void Skin_RedirectIndex(int& d);    // stock/festive/reskin def index -> config key
int  Skin_FindSpecificDef(int weaponRedir, int pkId);   // weapon-specific 15xxx skin def
int  Skin_FindWeaponRedir(int skinDef);                 // and back the other way

// -- Bypass ------------------------------------------------------------------------------------
extern bool g_pureBypass;
extern bool g_cheatsBypass;
extern bool g_f2pChatBypass;

// -- Removals -------------------------------------------------------------------------------------
extern bool g_removeRagdolls;
extern bool g_removePropFade;
extern bool g_fpsBoost; // bones optimization: cached SetupBones for non-local players
extern bool g_removeScreenEffects; // swallow Shake/Fade/Rumble user messages

// -- Flip world (flipworld.cpp) ---------------------------------------------------------------
extern bool g_flipWorld;

// -- Lag resolver (lagres.cpp) -- visual anti-twitch for laggy players ---------------------
extern bool  g_lagResolver;
extern bool  g_lrYaw;
extern float g_lrSmoothMs;
extern float g_lrExtrapMs;
extern float g_lrMaxErr;

// -- Performance / latency (all opt-in, default OFF) -----------------------------------------------
// g_lowLatency / g_hiPriority deleted 2026-09-05 (cost fps, read as packet loss) -- see dllmain.cpp
extern bool g_hiTimerRes;  // timeBeginPeriod(1) -- engine fps_max sleeps get 1ms, not 15.6ms
extern bool g_perfEnable;  // botox misc "performance" master; g_hiTimerRes is its popup row
// botox misc "convenience" buttons (m_convenience_buttons): slot used when both strings set.
constexpr int kConvButtons = 8;
extern char g_convTitle[kConvButtons][32];
extern char g_convCmd[kConvButtons][160];

// -- fpsopt.cpp REMOVED: the nine perf convar groups were a console-command mirror ---------

// -- Auto backstab: the LIVE feature is the aim tree's (src/hacks/aim/aim_melee.cpp,
// Vars::Aimbot::Melee::*). dih's own g_backstab* globals are gone -- backstab.cpp was
// dropped from the build at bs11 and nothing had read them since.

// -- Movement (Aletherium core port; per-feature bind: key + mode 0 always/1 hold/2 toggle) --------
extern bool g_bhop; // plain on/off, no bind
extern bool g_jumpbug;     extern int g_jumpbugKey;     extern int g_jumpbugMode;
extern bool g_jbYieldSurf;         // jumpbug yields the cmd while a pixel surf ride is live
extern bool g_jbPredict;           // fly the fall first -- no band tick ahead = never press (bhop lives)
extern bool g_autoStrafe;  extern int g_autoStrafeKey;  extern int g_autoStrafeMode;
extern bool  g_autoStrafeAvoidWalls;  // unit A: steer the other way if the heading is blocked
extern float g_autoStrafeAvoidDist;   // trace reach for the above (def 50, 0-200)
extern bool g_strafeOpt;   // plain on/off, no bind
extern float g_strafeOptGain;      // % of optimal correction (Aletherium default 100)
extern float g_strafeOptMinSpeed;  // 2D speed floor (Aletherium default 30)
extern bool g_edgeJump;    extern int g_edgeJumpKey;    extern int g_edgeJumpMode;
extern bool g_longJump;    extern int g_longJumpKey;    extern int g_longJumpMode;
extern bool g_longJumpEdge;        // edgejump on lj: walk-off edgejump while long jump active
extern bool  g_longJumpAdaptive;       // unit A: cancel the duck hold early instead of a fixed 2 ticks
extern bool  g_longJumpAdaptiveLetGo;  // cancel reason: key released
extern bool  g_longJumpAdaptiveLanded; // cancel reason: touched ground
extern bool  g_longJumpAdaptiveTimed;  // cancel reason: timer elapsed
extern float g_longJumpAdaptiveTime;   // seconds for the timed reason (def 0.3, 0.02-1)
extern bool g_miniJump;    extern int g_miniJumpKey;    extern int g_miniJumpMode;
extern bool g_miniJumpHoldDuck;    // hold duck airborne after mini takeoff (release before landing)
extern bool g_miniJumpEdge;        // edgejump on mj: walk-off edgejump while mini jump active
extern bool g_fastStop;    // plain on/off, no bind
extern bool g_fastAccel;   // plain on/off, no bind
extern bool g_noPush;      // skip C_TFPlayer::AvoidPlayers -- teammates stop shoving you
// wave 2 (Aletherium): edge bug + pixel surf family
extern bool g_edgeBug;     extern int g_edgeBugKey;     extern int g_edgeBugMode;
// botox n_edgebug knobs (edgebug.inl)
extern float g_edgeBugPredictTime;   // search window seconds (def 0.35, 0.02-2)
extern int   g_edgeBugDetectMode;    // 0 normal / 1 enhanced / 2 ebanat
extern bool  g_edgeBugToEdge;        // autostrafe to edge = the search's autostrafe switch
extern float g_edgeBugStrafeAngleMax; // max path bend off course (def 180 = off)
extern bool  g_edgeBugMouseSteer;    // re-plan + pick between paths by where the mouse turns
extern int   g_edgeBugPaths;         // plans kept per search when steering (1-4)
extern int   g_edgeBugLockType;      // 0 smooth / 1 full
extern float g_edgeBugLockStrength;  // 0-100 %
extern int   g_edgeBugStance;        // 0 any (jump-eating ducked find = fallback) / 1 ducked / 2 stand
extern int   g_edgeBugDelTicks;     extern bool g_edgeBugDelAdvanced; extern float g_edgeBugDelAngleLimit;
extern int   g_edgeBugDelSearch;    extern bool g_edgeBugDelSilent;   extern bool  g_edgeBugDelMouseFix;
extern float g_edgeBugDnaScan;      extern bool g_edgeBugDnaAdvanced; extern int   g_edgeBugDnaRange;
extern bool  g_edgeBugDnaCustomAngle; extern float g_edgeBugDnaAngleLimit;
extern bool  g_edgeBugViz;        // draw the latched plan: its flight path + a pad on the bug tick
extern float g_edgeBugVizSize;     // half-width of that pad, world units (def 16 = the hull)
extern bool g_pixelSurf;   extern int g_pixelSurfKey;   extern int g_pixelSurfMode;
extern int  g_pixelSurfPredictTicks; // seam look-ahead horizon, ticks (def 8, 1-32)
// approach aids for the pixel surf FEATURE (both off by default)
// -- park at the end of a surf (measure the runout, brake the tangent into it)
extern bool g_autoAlign;   // plain on/off, no bind
extern bool g_textureBug;  extern int g_textureBugKey;  extern int g_textureBugMode;
extern int g_tbStyle;              // 0 cumidere (texturebug.inl TextureBugCumidere) / 1 botox
extern bool g_tbcAlign;            // cumidere style: cumidere auto align first (texturebug.inl TBC_Align)
extern float g_tbReach;            // wall scan reach on top of the 1-tick lookahead (def 2 = base, 1-48)
extern float g_tbScanStep;         // wall scan z step (def 1 = base, 1-8) -- perf lever, >1 misses thin seams
extern float g_tbRampNz;           // |plane.normal.z| past this = a RAMP: TB hands the cmd back (def 0.05)
extern float g_tbHugRange;         // support gap the creep hold starts hugging from (def 0.2, x2 on a ride)
extern bool  g_tbSolveForward;     // exact-solve pass: invert AirAccelerate for the pinning forward
extern bool  g_tbNoOffgridStick;   // no creep/press/drive/park on a yaw-rotated wall (clean pins still ride)
extern bool  g_tbParkAtEnd;        // when a surf runs out of clean pins, hold the park instead of dropping
extern int   g_tbParkTicks;        // max park ticks in a row (0 = hold it forever)
extern float g_tbParkSpeed;        // top xy speed the park may engage at (was the fixed 30)
extern bool  g_tbParkBrake;        // brake to a stop at the measured end of the face (no wallbug needed)
extern float g_tbParkLead;         // extra units of margin on the brake's stopping distance
extern bool  g_tbPredict;          // predictive intercept: catch ledges the live scan sees too late
extern int   g_tbPredictTicks;     // ticks of flight the prediction rolls (def 40, 8-128)
extern float g_tbPredictReach;     // sideways reach of the predicted-path scan (def 24, 1-96)
extern bool g_wallClimb;   extern int g_wallClimbKey;   extern int g_wallClimbMode;
extern bool g_airStuck;    extern int g_airStuckKey;    extern int g_airStuckMode;

// -- Unit A port (movement_ex.inl): small tricks from botox csgo-sdk, defaults OFF ---------------
extern bool g_nulls;
extern bool g_hsw;         extern int g_hswKey;         extern int g_hswMode;
extern int  g_hswSide;          // 0 = right-biased, 1 = left-biased
extern bool g_hswAllowWS;        // apply while holding W/S
extern bool g_hswAllowAD;        // apply while holding A/D on the ground
extern bool g_hswPreGround;      // allow the grounded half at all
extern bool g_delayHop;    extern int g_delayHopKey;    extern int g_delayHopMode;
extern int  g_delayHopTicks;    // air ticks to wait before pressing jump (def 6)
extern bool g_autoCrouch;  extern int g_autoCrouchKey;  extern int g_autoCrouchMode;
extern int  g_autoCrouchTicks;  // look-ahead ticks for the landing trace (def 6)
extern bool g_jumpbugKangaroo;  // alt duck-timing variant of jumpbug, see JumpbugKangaroo
extern bool g_silentView;       // smooth demo view: lock recorded viewangles during demo playback
extern bool g_jumpstats;        // count clean bhop/lj/mj/wallclimb takeoffs
void MV_GetJumpstats(int& bhop, int& longjump, int& minijump, int& wallclimb);

// -- Unit A3 port (movement_tricks2.inl + small hooks): tung/bounce/pf modes/eb presets/
// ps fix, from botox csgo-sdk. Simplified where the botox source is a full sim-planner or a
// second search engine (see movement_tricks2.inl's header comment) -- defaults OFF.
extern bool g_tungSurf;    extern int g_tungSurfKey;    extern int g_tungSurfMode;
extern float g_tungMaxNz;       // texturebug.inl's ramp_band widened to this while tung is on (def .25, .05-.5)
extern bool g_autoBounce;  extern int g_autoBounceKey;  extern int g_autoBounceMode;
extern bool g_bounceAssist; extern int g_bounceAssistKey; extern int g_bounceAssistMode;
extern float g_bounceReach;     // wall scan reach, units (def 48, 8-128)
extern float g_bounceMinSpeed;  // min xy speed to bother redirecting (def 150, 0-600)
extern bool g_psFinderHeadbounce; // PixelFinder: also probe for ceiling (ascent) pins
extern bool g_psFinderPixeljump;  // PixelFinder: keep jump-only (no-fall) pins, own list
extern bool g_psSurfFix;        // hold duck for a couple ticks into a ride so a flicker can't drop it
extern int  g_edgeBugStyle;     // 0 botox search / 1 delusional donor / 2 dna donor
extern bool g_edgeBugMouseLock; // scale mouse x in OverrideMouseInput while a plan flies
extern bool g_edgeBugSound;     // TF2 game sound the tick after a real edgebug
extern int  g_edgeBugSoundId;   // which TF2 game sound plays on a real EB
void EdgeBug_MouseInput(float* x, float* y);   // IClientMode::OverrideMouseInput hook body
void EdgeBug_InstallMouseHook();
// -- Unit A2 port (movement_edge.inl): edge skip + blockbot, defaults OFF ------------------------
extern bool g_edgeSkip;    extern int g_edgeSkipKey;    extern int g_edgeSkipMode;
extern int  g_edgeSkipTicks;     // search window, ticks (def 23, ~botox's 0.35s predict_time)
extern int  g_edgeSkipDepth;     // budget multiplier 1-4 (botox m_edge_skip_search_amount, def 2)
extern float g_edgeSkipMaxBend;  // max degrees off course before a strafe counts as bent (def 180 = off)
extern bool g_blockBot;    extern int g_blockBotKey;    extern int g_blockBotMode;

// -- Fake movement (fakemove.inl): ProcessMovement edits, LOCAL SERVER ONLY -----------------------
extern bool g_fmEnabled;
extern bool g_fmWallSurf;   extern int g_fmWallSurfKey;   extern int g_fmWallSurfMode;
extern bool g_fmWallJump;   extern int g_fmWallJumpKey;   extern int g_fmWallJumpMode;
extern float g_fmWallReach;       // wall surf + wall jump: how far a wall still counts
extern float g_fmWallJumpPush;    // optional push off the wall (0 = plain ground jump)
extern bool  g_fmWallJumpHold;    // holding jump at a wall re-jumps once you stop rising
extern bool  g_fmWallSurfSeam;    // wall surf only where the wall is 2+ brushes meeting (fm_seam.h)
extern bool  g_fmWallJumpSeam;    // wall jump only off a brush seam
extern float g_fmSeamSlack;       // extra units around the hull a seam may sit
extern bool g_fmHighJump;   extern int g_fmHighJumpKey;   extern int g_fmHighJumpMode;
extern float g_fmHighJumpMult;
extern bool g_fmAirStuck;   extern int g_fmAirStuckKey;   extern int g_fmAirStuckMode;
extern bool g_fmAirStuckKeepVel;  // resume momentum on release
extern bool g_fmAirStuckNearOnly; // only freeze with a brush/prop within g_fmAirStuckNearDist
extern float g_fmAirStuckNearDist;
extern bool g_fmNoFall;     extern int g_fmNoFallKey;     extern int g_fmNoFallMode;
extern bool g_fmWallbugFix;       // sv_wallbug 1 while hosting (pre Oct-2026 ClipVelocity)
void FakeMove_Restore();          // eject: sv_wallbug back to the value it had
extern bool g_fmCrouchJump; extern int g_fmCrouchJumpKey; extern int g_fmCrouchJumpMode;
extern bool g_fmRampBoost;  extern int g_fmRampBoostKey;  extern int g_fmRampBoostMode;
extern float g_fmRampBoostMult;
extern bool g_fmHeadSurf;   extern int g_fmHeadSurfKey;   extern int g_fmHeadSurfMode;
extern float g_fmHeadSurfSnap;    // how far up a ledge top still catches you
extern bool g_fmHeadSurfLedgeOnly;
extern bool g_fmHeadSurfBottoms;    // also ride brush bottom EDGES at the hull top (never full ceilings)
void FakeMove_DemoTick();   // FrameStageNotify stage 3: replays fake jump anim/sound in demo playback
// botox air_stuck knobs (m_air_stuck_extra_reach / m_air_stuck_budget)
extern float g_asExtraReach;       // wall scan reach past the hull, units (def 4, 0-32; > 2 arms the wide B scan)
extern float g_asBudget;           // search clock, percent of one tick (def 50, 10-90)
// wave 3 (Aletherium): pixel surf finder + assist
extern bool g_psFinder;    extern int g_psFinderKey;    extern int g_psFinderMode;
extern float g_psFinderMargin;     // scan margin above/below the aimed column (def 8, 0-64)
extern float g_psFinderBudget;     // ms of scan per cmd (def 6, 0.25-32). The column is
                                   // chunked across cmds, so this trades scan time for framerate.
                                   // no longer shares the TB clock -- this is the bound
extern bool  g_psFinderStrict;     // pfstrict: keep only pins a real FALL catches + hull can reach
extern bool  g_psFinderTb;         // also mark texturebug seams on the column (red rings)
extern bool  g_psFinderNoProps;    // finder traces refuse props (static + prop_* ents)
extern bool  g_psMapLines;         // show whole-map scan lines -- survives the finder going off
extern bool g_psAssist;    extern int g_psAssistKey;    extern int g_psAssistMode;
extern int  g_psSetPointKey;       // save aimed finder point as assist target (edge press)
extern int  g_psDeletePointKey;    // delete aimed finder/assist point (edge press)
extern float g_psAssistRadius;     // engage radius around saved targets (def 300, 40-1024).
                                   // the ONE engage distance; a point may override it
extern float g_psAssistSnap;       // set/delete snap distance (def 48, 4-256)
extern int  g_psAssistMaxPoints;   // how many in-range points get simmed per cmd,
                                   // nearest first (def 3, 1-8). delusional's pixelsurf_max_points
extern float g_psAssistReuseDelay; // seconds a surf you just rode is planned LAST for,
                                   // so a combo's next jump is not aimed at the seam behind you
                                   // (def 3, 0-8; 0 = off, nearest-first as before)
// -- the ONE physical knob the assist has: how fast the feet may still be falling
// when they reach the seam (u/s). The finder pins its own probes at 6-8, so 30 = strict and
// catches; 120 is an earlier build's behaviour, which fired constantly and caught nothing. The height
// band and the apex-overshoot bound are DERIVED from it (over = v^2/2g), so there is one
// number to turn, not three
// g_psAssistSteer (steer lock) deleted -- the last viewangle read in the assist
extern bool g_psAssistRender;      // draw finder/assist points on the world
extern bool g_psAssistJumpBox;     // "minijump to <z>" popup after an assist jump
// ---- uneven ground ----
extern bool  g_psGroundHelp;       // grounded: silently steer your own move at the target you are
                                   // planning for, so walking up to a seam lands you under it
extern float g_psGroundHelpAngle;  // cap on that steer, degrees off your own heading (def 45)
extern float g_psLaunchZSlack;     // refuse a queued launch whose floor moved this far since the
                                   // sim validated it, u (def 4; 0 = fire anyway)

// Route calculator : every press sequence that completes the markers, from a z-only port
// of TF2's own movement code (hacks/movement/routesim.h), checked against Pred_Simulate each solve.
extern bool g_routeCalc;
extern int  g_rcAddKey;            // place a marker where you aim (edge press)
extern int  g_rcDeleteKey;         // delete the marker you aim at, else the newest
extern int  g_rcClearKey;          // drop every marker on this map
extern int  g_rcSolveKey;          // calculate (press again while solving = cancel)
extern int   g_rcMoveMask;         // moves a route may use (rs::MoveBit bits, def all 7)
extern int   g_rcStartMoves;       // the starting jump's own moves (rs::MoveBit bits, botox start styles)
extern int   g_rcDelayTicks;       // grounded cmds a delay hop waits (botox delay hop ticks, 0-8)
extern bool  g_rcShowKeys;         // route calc key list box (botox show key list)
extern int   g_rcPathAlpha;        // path line alpha 0-255
extern float g_rcPathThick;        // path line thickness 1-8 px
extern bool  g_rcAllowInvalid;     // a wall hit with no finder ring is a pixelsurf marker anyway
extern bool  g_rcSnapEdge;         // ground markers move onto their nearest ledge lip
extern bool  g_rcAdvanced;         // long placement readout (botox advanced readout)
extern int   g_rcMaxShown;         // combos printed on the panel (def 8, 1-50)
extern bool  g_rcStartHere;        // start where you stand / in the air (off = marker 1 is the start)
extern bool  g_rcShowPath;         // draw the line between them (the solved arc once there is one)
extern bool  g_rcShowPanel;        // draw the answer panel
extern int   g_rcPopupTime;        // seconds the route/pixel popup stays up (botox def 6, 1-30)

// Pixel calculator : the same solver with ONE pixelsurf marker, always from where you
// stand. Its own feature so it can be used -- and drawn -- without the route calculator. The
// moves / hop speed / solve budget it uses are the route calculator's (one solver, one set).
extern bool  g_pixelCalc;
extern int   g_pcPlaceKey;         // pixel calculator: put the one pixel marker where you aim
extern int   g_pcSolveKey;         // pixel calculator: combos from where you stand to it
extern bool  g_pcShowMarker;       // draw the pixel marker in the world
extern bool  g_pcShowPanel;        // draw its answer panel

// Practice (practice.inl): console commands on a server you host, sv_cheats 1. Nothing online.
extern bool g_prInfAmmo;           // impulse 101 twice a second (ammo, metal, clips, meters)
extern bool g_prInfHealth;         // `god` -- a toggle, so the flip is what gets sent
extern bool g_prInfTime;           // mp_timelimit 0, old value back when it goes off
extern int  g_prNoclipKey;         // noclip (edge press)
extern int  g_prSaveKey;           // remember where you stand + where you look
extern int  g_prTpKey;             // setpos/setang back to it, velocity killed on arrival
extern int  g_prBotEnemyKey;       // bot of the other team at the crosshair
extern int  g_prBotMateKey;        // bot of your team at the crosshair
extern int  g_prSentryKey;         // enemy sentry where you look, Disable'd (= it never fires)
extern int  g_prDispenserKey;      // enemy dispenser where you look
extern int  g_prTeleporterKey;     // enemy teleporter entrance where you look
extern int  g_prBotClass;          // 0-8: scout sniper soldier demoman medic heavy pyro spy engi

// -- Skybox ConVar handle. The sky-name / fog-colour redirect buffers are visuals.cpp's own
// file-local scratch and are deliberately NOT declared here.
extern ConVar* g_cv_sv_skyname;

// -- Weather entity state is visuals.cpp's own; eject asks Precip_IsLive() (visuals.h).
// g_offMins / g_offMaxs are NOT declared here either -- one owner only, netvars.cpp
// (sentinel 0 = unresolved, -1 = not found). See the movement netvar block below.

// -- Net stats ------------------------------------------------------------------------------------------------
extern std::atomic<float> g_fps;
extern std::atomic<int>   g_ping;
extern std::atomic<int>   g_loss;
extern std::atomic<bool>  g_conn;

// -- Debug log ----------------------------------------------------------------------------------------------------
// Master switch, default OFF. The per-tick movement/TB/PS diags print ~250-8000 byte lines
// every tick and none of them are being read any more, so DebugLog returns before it formats
// anything unless this is on. Flip it in the menu (debug section) to get the log back.
extern bool g_debugLogEnable;
void DebugLog(const char* fmt, ...);
void DebugLog_FlushEarly();   // dump the lines that happened before the toggle went on
// Eject only: closes the persistent log handle. Every later DebugLog is a no-op. The critical
// section is deliberately NOT deleted -- see dllmain.cpp.
void DebugLog_Shutdown();
void ConsoleColorPrint(uint8_t r, uint8_t g, uint8_t b, const char* msg);

// ==== Netvars =====================================================================
struct RecvProp;
struct RecvTable
{
    RecvProp*   m_pProps;
    int         m_nProps;
    void*       m_pDecoder;
    const char* m_pNetTableName;
    bool        m_bInitialized;
    bool        m_bInMainList;
};
struct RecvProp
{
    const char* m_pVarName;
    int         m_RecvType;
    int         m_Flags;
    int         m_StringBufferSize;
    bool        m_bInsideArray;
    const void* m_pExtraData;
    RecvProp*   m_pArrayProp;
    void*       m_ArrayLengthProxy;
    void*       m_ProxyFn;
    void*       m_DataTableProxyFn;
    RecvTable*  m_pDataTable;
    int         m_Offset;
    int         m_ElementStride;
    int         m_nElements;
    const char* m_pParentArrayPropName;
};
struct ClientClass {
    void*        m_pCreateFn;
    void*        m_pCreateEventFn;
    const char*  m_pNetworkName;
    RecvTable*   m_pRecvTable;
    ClientClass* m_pNext;
    int          m_ClassID;
};
int  FindPropOffset(RecvTable* pTable, const char* name, int baseOffset = 0, int depth = 0);
RecvTable* FindNetworkTable(const char* netTableName);
ClientClass* GetAllClasses();
void ResolveNetvars();

extern int  g_offClass;
extern int  g_offLifeState;
extern int  g_offBody;
extern int  g_offModelIndex;
extern int  g_offMoveParent;
extern int  g_offWeaponOwner;
extern int  g_offActiveWeapon;
extern int  g_offMyWeapons;    // m_hMyWeapons[0], CHandle<CTFWeaponBase>[48] -- index = slot
extern int  g_offFlags;        // m_fFlags (FL_ONGROUND/FL_DUCKING) for bhop
extern int  g_offGroundEntity; // m_hGroundEntity -- Aletherium ground check
extern int  g_offCachedBones;  // C_BaseAnimating m_CachedBoneData (m_hLightingOrigin - 88)
// movement netvars (Movement_ResolveExtraNetvars)
extern int  g_offOrigin;
extern int  g_offVelocity;
extern int  g_offMins;
extern int  g_offMaxs;
extern int  g_offMaxspeed;
extern int  g_offTickBase;
extern int  g_offConstraintEnt; // m_pCurrentCommand = this - 8
extern int  g_offWaterLevel;    // m_MoveType = this - 4
extern int  g_offModelScale;
extern int  g_offCond;          // m_nPlayerCond
// -- duck state the mini jump has to read (all four are networked to the local player) --
extern int  g_offDucking;       // m_Local.m_bDucking -- duck TRANSITION (mini jump lives here)
extern int  g_offDucked;        // m_Local.m_bDucked  -- transition finished
extern int  g_offDuckTime;      // m_Local.m_flDucktime -- ms left in the transition
extern int  g_offDuckTimer;     // m_Shared.m_flDuckTimer -- grounded IN_DUCK is STRIPPED until this
extern int  g_offAirDucked;     // m_Shared.m_nAirDucked -- 2 air ducks per air event, then stripped
extern int  g_offSolidFlags;    // m_usSolidFlags (m_Collision) -- brush filter trigger reject
extern int  g_offViewOffset;    // m_vecViewOffset[0] -- eye pos (PS finder/assist aim)
extern int  g_offAirDash;       // m_iAirDash -- scout double jump budget
extern int  g_offItemDefIndex;  // m_iItemDefinitionIndex (DT_EconEntity) -- Atomizer detect
extern int  g_offTeamNum;       // m_iTeamNum -- friendly team brushes are not solid to us
extern int  g_offCollGroup;     // m_CollisionGroup -- solid-to-player-movement objects still are
// The interp-removal netvars that used to sit here (m_flSimulationTime, m_flOldSimulationTime,
// m_iObserverMode, m_hObserverTarget, m_PlayerAnimState, m_flTauntYaw, m_bAllowMoveDuringTaunt,
// m_nPlayerCondEx2, m_flCycle, m_nSequence, m_bSequenceLoops) went with that feature. Eight of
// them were extern-only -- no definition anywhere -- and the other three were resolved every
// map and read by nothing but their own DebugLog line. m_flDuckJumpTime went the same way:
// resolved, logged, never read by the mini jump it is named after.
void Movement_ResolveExtraNetvars();

extern bool g_netvarsReady;

// -- Unit C: ESP (src/hacks/aim/esp.cpp draws these; values read via plain extern, same
// primitive-only declarations duplicated there -- see esp.h's banner comment for why esp.cpp
// does not include this header) ----------------------------------------------------------
#include "../hacks/esp/esp_vars.h"

// -- Unit C2: chams (visuals.cpp, piggybacked on the existing h_DrawModelExecute hook --
// no 2nd MinHook on DrawModelExecute). Players only: colour-modulated ForcedMaterialOverride,
// same technique Amalgam's CChams::DrawModel uses. Weapon/viewmodel chams are OUT of this
// pass's scope (see visuals.cpp's Chams_WantsEntity comment) -- colors double as rgba, a[3] is
// the blend alpha.
extern bool  g_chamsEnable;
extern bool  g_chamsEnemy;
extern bool  g_chamsTeam;
extern float g_chamsEnemyColor[4];
extern float g_chamsTeamColor[4];

// -- Unit C4: glow (visuals.cpp, same DrawModelExecute hook as chams -- extra $ignorez
// overlay pass, no 2nd MinHook). Players + buildings, same team/enemy split as chams.
// Tint only: TF2's native CGlowObjectManager outline has no verified slot/pattern yet.
extern bool  g_glowEnable;
extern bool  g_glowEnemy;
extern bool  g_glowTeam;
extern float g_glowEnemyColor[4];
extern float g_glowTeamColor[4];
extern bool  g_glowBuildings;
extern float g_glowBuildingColor[4];
extern float g_glowAlpha;          // 0..1, multiplies every glow pass's alpha

// -- 10-31 chams/glow rework: src/hacks/aim/chams.cpp draws, unitC_menu.cpp edits. The
// C2/C4 enemy/team colours + glow enemy/team/buildings/alpha above are no longer read.
// Layer lists = config strings (src/hacks/visuals/chams_layers.h, 160 = ChamsLayers::kCfgLen).
extern bool  g_chamsLocalEnable;
extern bool  g_chamsRagdolls;
extern bool  g_chamsBacktrackXqz;
extern int   g_chamsBacktrackType;      // 0 oldest record, 1 all records, 2 aimbot target record
extern int   g_chamsBacktrackMaxGhosts;
extern bool  g_chamsBacktrackGradient;
extern float g_chamsSoundDuration;
extern bool  g_chamsSoundFade;
extern float g_chamsSoundMinVolume;
extern float g_chamsSoundMaxDistance;
extern char  g_chamsVisibleCfg[160];
extern char  g_chamsOccludedCfg[160];
extern char  g_chamsBacktrackCfg[160];
extern char  g_chamsSoundCfg[160];
extern char  g_chamsTeamVisibleCfg[160];
extern char  g_chamsTeamOccludedCfg[160];
extern char  g_chamsWeaponCfg[160];
extern char  g_chamsArmsCfg[160];
extern char  g_chamsSleeveCfg[160];
extern float g_glowVisColor[4];
extern float g_glowInvisColor[4];
extern bool  g_glowLegacy;
extern int   g_glowLegacyStyle;         // 0 default, 1 rim, 2 outline, 3 pulse
extern bool  g_glowNormalBlend;
extern float g_glowThickness;
extern bool  g_glowGradient;
extern float g_glowVisOuterColor[4];
extern float g_glowInvisOuterColor[4];
extern float g_glowGradientInner;
extern bool  g_glowWave;
extern float g_glowWaveColor[4];
extern float g_glowWaveSpeed;
extern float g_glowWaveSize;
extern float g_glowWaveStrength;
extern bool  g_glowPulse;
extern float g_glowPulseSpeed;
extern float g_glowPulseMin;

// ==== Inline engine helpers ==============================================================
// IClientEntityList::GetClientEntity = slot 3. IVEngineClient: GetLocalPlayer = 12,
// IsInGame = 26, GetPlayerForUserID = 9.
inline void* GetClientEntity(int iIndex)
{
    return g_entityList ? VCall<void*>(g_entityList, 3, iIndex) : nullptr;
}

// -1 = no local player. VCall cannot tell a failed call from a returned 0, so normalise.
inline int GetLocalPlayerIndex()
{
    if (!g_engine) return -1;
    const int idx = VCall<int>(g_engine, 12);
    return idx > 0 ? idx : -1;
}

inline bool IsInGame()
{
    return g_engine && VCall<bool>(g_engine, 26);
}

inline bool IsLocalPlayerAlive()
{
    if (g_offLifeState <= 0) return false;
    int idx = GetLocalPlayerIndex();
    if (idx <= 0) return false;
    void* pEnt = GetClientEntity(idx);
    if (!pEnt) return false;
    uintptr_t addr = reinterpret_cast<uintptr_t>(pEnt) + g_offLifeState;
    if (addr < 0x10000) return false;
    uint8_t lifeState = 0;
    __try {
        lifeState = *reinterpret_cast<uint8_t*>(addr);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return lifeState == 0;
}

inline int GetPlayerForUserID(int userID)
{
    return (g_engine && userID > 0) ? VCall<int>(g_engine, 9, userID) : 0;
}

inline int GetPlayerClass(int iIndex)
{
    ResolveNetvars();
    if (g_offClass <= 0) return 0;
    void* pEnt = GetClientEntity(iIndex);
    if (!pEnt) return 0;
    int cls = 0;
    __try {
        cls = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pEnt) + g_offClass);
    } __except(EXCEPTION_EXECUTE_HANDLER) { cls = 0; }
    return (cls >= 1 && cls <= 9) ? cls : 0;
}

inline constexpr int k_pingSubtract = 20;

// Particle system constants (confirmed x64 offsets)
inline constexpr int k_particleEffectStride = 40;
inline constexpr int k_particleEffectPtrOff = 32;
inline constexpr int k_setSimSlot           = 4;

void* GetInterface(const char* module, const char* name);

// ==== Hook install helpers ========================================================
// Every install in the DLL is the same four steps -- resolve, MH_CreateHook,
// MH_EnableHook, log -- and every one of them wants the same log line when a step
// fails. `tag` names the feature so a missing sig is identifiable in debug.log.

// First interface in `names` (nullptr-terminated) that the module hands back.
inline void* GetInterfaceAny(const char* module, const char* const* names)
{
    for (int i = 0; names[i]; ++i)
        if (void* p = GetInterface(module, names[i])) return p;
    return nullptr;
}

// Hook an already-resolved address. bEnable false = created but left disarmed, for the
// hooks a toggle arms later (netgraph). A failed create leaves *orig null, so a caller
// can use "orig is set" as "this hook is live" without a second flag.
inline bool InstallHookAt(void* addr, const char* tag, void* detour, void** orig,
                          bool bEnable = true)
{
    if (!addr) return false;
    const MH_STATUS st = MH_CreateHook(addr, detour, orig);
    if (st != MH_OK) {
        if (orig) *orig = nullptr;
        DebugLog("%s: MH_CreateHook failed st=%d", tag, (int)st);
        return false;
    }
    if (bEnable) MH_EnableHook(addr);
    DebugLog("%s: hooked at %p", tag, addr);
    return true;
}

// Pattern-scan a module, then hook the hit. Already-installed (*orig set) is a no-op.
inline bool InstallPatternHook(HMODULE mod, const char* tag, const char* pattern,
                               void* detour, void** orig, bool bEnable = true)
{
    if (!mod || (orig && *orig)) return false;
    uint8_t* addr = PatternScan(mod, pattern);
    if (!addr) { DebugLog("%s: pattern NOT FOUND", tag); return false; }
    return InstallHookAt(addr, tag, detour, orig, bEnable);
}

// Same, over a nullptr-terminated pattern list -- first one that resolves wins.
inline bool InstallPatternHookAny(HMODULE mod, const char* tag, const char* const* patterns,
                                  void* detour, void** orig, bool bEnable = true)
{
    if (!mod || (orig && *orig)) return false;
    for (int i = 0; patterns[i]; ++i)
        if (uint8_t* addr = PatternScan(mod, patterns[i]))
            return InstallHookAt(addr, tag, detour, orig, bEnable);
    DebugLog("%s: pattern NOT FOUND", tag);
    return false;
}

// Hook one vtable slot. SEH-guarded: the interface pointer comes from the game.
// outAddr receives the hooked function, for a caller that arms/disarms it later.
__declspec(noinline) inline bool InstallVTableHook(void* iface, int slot, const char* tag,
                                                   void* detour, void** orig,
                                                   bool bEnable = true, void** outAddr = nullptr)
{
    if (!iface || (orig && *orig)) return false;
    void* fn = nullptr;
    __try {
        void** vt = *reinterpret_cast<void***>(iface);
        if (reinterpret_cast<uintptr_t>(vt) >= 0x10000) fn = vt[slot];
    } __except(EXCEPTION_EXECUTE_HANDLER) { fn = nullptr; }
    if (!fn) { DebugLog("%s: bad vtable/slot %d", tag, slot); return false; }
    if (!InstallHookAt(fn, tag, detour, orig, bEnable)) return false;
    if (outAddr) *outAddr = fn;
    return true;
}
