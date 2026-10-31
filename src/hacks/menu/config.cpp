#include "config.h"
#include "../hud/movind.h"   // MI_AST -- the miShowMask migration below is written in slot names
#include "../screen/colorcorrect.h"   // CcResetCurve -- a curve that fails to parse goes back to identity
#include "../aim/aim.h"   // the aimbot's vars ride in this same file
#include "../visuals/skins.h"     // and so does the skinchanger's per-weapon block
#include "../hud/velgraph.h"      // unit B
#include "../hud/keystroke.h"     // unit B
#include "../hud/keyind_fx.h"     // unit B
#include "../movement/detections.h"    // unit B
#include "../movement/ps_lines.h"      // unit B
#include "../movement/recorder.h"      // unit B
#include "fonts.h"         // fonts tab
#include "visual_keys.h"   // settings tab "visuals config"
#include <cfloat>      // FLT_MAX -- the "this end was never bounded" sentinel below
#include <algorithm>   // std::sort -- ListConfigs

// Tiny key=value config. One entry table drives save + load.

struct CfgEntry {
    const char* key;
    enum { BOOL, INT, FLOAT, FLOAT4, STR, CURVE } type;
    void*  ptr;
    size_t size; // STR only
};

// CURVE: "n;x,y;x,y;..." -- a colour correction curve, whose point count varies. Anything
// malformed, out of order or out of range is refused whole and the curve resets to the
// identity diagonal: a half-parsed curve would be baked straight into the lookup table.
static bool ParseCurve(const char* val, CcCurve& out)
{
    const int n = atoi(val);
    if (n < 2 || n > k_ccMaxPts) return false;

    CcCurve c{};
    c.n = n;
    const char* p = val;
    for (int i = 0; i < n; ++i)
    {
        p = strchr(p, ';');
        if (!p) return false;
        ++p;
        if (sscanf(p, "%f,%f", &c.x[i], &c.y[i]) != 2) return false;
        if (c.y[i] < 0.f) c.y[i] = 0.f;
        if (c.y[i] > 1.f) c.y[i] = 1.f;
        if (i > 0 && !(c.x[i] > c.x[i - 1])) return false;   // must be strictly increasing
    }
    c.x[0] = 0.f;          // the endpoints are pinned; only their height is the user's
    c.x[n - 1] = 1.f;
    if (n > 2 && !(c.x[1] > 0.f && c.x[n - 2] < 1.f)) return false;

    out = c;
    return true;
}

static const CfgEntry kEntries[] = {
    { "menuKey",          CfgEntry::INT,    &g_menuKey, 0 },

    { "wmEnable",         CfgEntry::BOOL,   &g_wmEnable, 0 },
    { "wmShowLabel",      CfgEntry::BOOL,   &g_wmShowLabel, 0 },
    { "wmLabel",          CfgEntry::STR,    g_wmLabel, sizeof(g_wmLabel) },
    { "wmShowFPS",        CfgEntry::BOOL,   &g_wmShowFPS, 0 },
    { "wmShowPing",       CfgEntry::BOOL,   &g_wmShowPing, 0 },
    { "wmShowLoss",       CfgEntry::BOOL,   &g_wmShowLoss, 0 },
    { "wmShowTime",       CfgEntry::BOOL,   &g_wmShowTime, 0 },
    { "wmPos",            CfgEntry::INT,    &g_wmPos, 0 },
    { "wmTextColor",      CfgEntry::FLOAT4, g_wmTextColor, 0 },
    { "mpEnable",         CfgEntry::BOOL,   &g_mpEnable, 0 },
    { "mpCoverArt",       CfgEntry::BOOL,   &g_mpCoverArt, 0 },
    { "mpBackground",     CfgEntry::BOOL,   &g_mpBackground, 0 },
    { "mpProgressBar",    CfgEntry::BOOL,   &g_mpProgressBar, 0 },
    { "mpSimple",         CfgEntry::BOOL,   &g_mpSimple, 0 },
    { "mpTime",           CfgEntry::BOOL,   &g_mpTime, 0 },
    { "mpPos",            CfgEntry::INT,    &g_mpPos, 0 },

    { "hmEnable",         CfgEntry::BOOL,   &g_hmEnable, 0 },
    { "hmColor",          CfgEntry::FLOAT4, g_hmColor, 0 },
    { "hmSize",           CfgEntry::FLOAT,  &g_hmSize, 0 },

    { "dpEnable",         CfgEntry::BOOL,   &g_dpEnable, 0 },
    { "dpStyle",          CfgEntry::INT,    &g_dpStyle, 0 },
    { "dpColor",          CfgEntry::FLOAT4, g_dpColor, 0 },
    { "dpMulticolor",     CfgEntry::BOOL,   &g_dpMulticolor, 0 },
    { "dpAmount",         CfgEntry::INT,    &g_dpAmount, 0 },
    { "dpSize",           CfgEntry::FLOAT,  &g_dpSize, 0 },
    { "dpLifetime",       CfgEntry::FLOAT,  &g_dpLifetime, 0 },
    { "dpSoulStyle",      CfgEntry::INT,    &g_dpSoulStyle, 0 },
    { "dpMcMode",         CfgEntry::INT,    &g_dpMcMode, 0 },
    { "dpMcXpTime",       CfgEntry::FLOAT,  &g_dpMcXpTime, 0 },

    { "ptsEnable",        CfgEntry::BOOL,   &g_ptsEnable, 0 },

    { "specEnable",       CfgEntry::BOOL,   &g_specEnable, 0 },
    { "specAlwaysShow",   CfgEntry::BOOL,   &g_specAlwaysShow, 0 },
    { "specStyle",        CfgEntry::INT,    &g_specStyle, 0 },
    { "specPos",          CfgEntry::INT,    &g_specPos, 0 },
    { "specOffX",         CfgEntry::FLOAT,  &g_specOffX, 0 },
    { "specOffY",         CfgEntry::FLOAT,  &g_specOffY, 0 },
    { "specShowTarget",   CfgEntry::BOOL,   &g_specShowTarget, 0 },
    { "specDelusionalLeft", CfgEntry::BOOL, &g_specDelusionalLeft, 0 },
    { "specListWidth",    CfgEntry::INT,    &g_specListWidth, 0 },
    { "specColorOne",     CfgEntry::FLOAT4, g_specColorOne, 0 },
    { "specColorTwo",     CfgEntry::FLOAT4, g_specColorTwo, 0 },

    { "wmStyle",          CfgEntry::INT,    &g_wmStyle, 0 },

    { "discordEnable",    CfgEntry::BOOL,   &g_discordEnable, 0 },
    { "discordAppId",     CfgEntry::STR,    g_discordAppId, sizeof(g_discordAppId) },
    { "discordState",     CfgEntry::STR,    g_discordState, sizeof(g_discordState) },
    { "discordDetails",   CfgEntry::STR,    g_discordDetails, sizeof(g_discordDetails) },
    { "discordImageKey",  CfgEntry::STR,    g_discordImageKey, sizeof(g_discordImageKey) },

    { "debugMenuEnable",  CfgEntry::BOOL,   &g_debugMenuEnable, 0 },
    { "consoleColor",     CfgEntry::FLOAT4, g_consoleColor, 0 },

    { "miEnable",         CfgEntry::BOOL,   &g_miEnable, 0 },
    { "miHorizontal",     CfgEntry::BOOL,   &g_miHorizontal, 0 },
    { "miMitosis",        CfgEntry::BOOL,   &g_miMitosis, 0 },
    { "miShowMask",       CfgEntry::INT,    &g_miShowMask, 0 },
    { "miOffsetY",        CfgEntry::FLOAT,  &g_miOffsetY, 0 },
    { "miScale",          CfgEntry::FLOAT,  &g_miScale, 0 },
    { "miIdleColor",      CfgEntry::FLOAT4, g_miIdleColor, 0 },
    { "miActiveColor",    CfgEntry::FLOAT4, g_miActiveColor, 0 },

    { "viEnable",         CfgEntry::BOOL,   &g_viEnable, 0 },
    { "viPreSpeed",       CfgEntry::BOOL,   &g_viPreSpeed, 0 },
    { "viFadeAlpha",      CfgEntry::BOOL,   &g_viFadeAlpha, 0 },
    { "viCustomColor",    CfgEntry::BOOL,   &g_viCustomColor, 0 },
    { "viOffsetY",        CfgEntry::FLOAT,  &g_viOffsetY, 0 },
    { "viColor1",         CfgEntry::FLOAT4, g_viColor1, 0 },
    { "viColor2",         CfgEntry::FLOAT4, g_viColor2, 0 },
    { "viColor3",         CfgEntry::FLOAT4, g_viColor3, 0 },
    { "viColor4",         CfgEntry::FLOAT4, g_viColor4, 0 },
    { "viColor5",         CfgEntry::FLOAT4, g_viColor5, 0 },

    { "vmSwayEnable",     CfgEntry::BOOL,   &g_vmSwayEnable, 0 },
    { "vmSwayScale",      CfgEntry::FLOAT,  &g_vmSwayScale, 0 },
    { "vmSwayInterp",     CfgEntry::FLOAT,  &g_vmSwayInterp, 0 },
    { "vmOffsetEnable",   CfgEntry::BOOL,   &g_vmOffsetEnable, 0 },
    { "vmOffsetX",        CfgEntry::FLOAT,  &g_vmOffsetX, 0 },
    { "vmOffsetY",        CfgEntry::FLOAT,  &g_vmOffsetY, 0 },
    { "vmOffsetZ",        CfgEntry::FLOAT,  &g_vmOffsetZ, 0 },
    { "vmRemoveBob",      CfgEntry::BOOL,   &g_vmRemoveBob, 0 },
    { "vmCsBob",          CfgEntry::BOOL,   &g_vmCsBob, 0 },
    { "vmCsBobCycle",     CfgEntry::FLOAT,  &g_vmCsBobCycle, 0 },
    { "vmCsBobUp",        CfgEntry::FLOAT,  &g_vmCsBobUp, 0 },
    { "vmCsBobVert",      CfgEntry::FLOAT,  &g_vmCsBobVert, 0 },
    { "vmCsBobLat",       CfgEntry::FLOAT,  &g_vmCsBobLat, 0 },
    { "vmCsBobLower",     CfgEntry::FLOAT,  &g_vmCsBobLower, 0 },
    { "vmCsGunLower",     CfgEntry::BOOL,   &g_vmCsGunLower, 0 },
    { "vmCsGunLowerAngle",CfgEntry::FLOAT,  &g_vmCsGunLowerAngle, 0 },
    { "vmCsGunLowerSpeed",CfgEntry::FLOAT,  &g_vmCsGunLowerSpeed, 0 },
    { "vmCsLandDip",      CfgEntry::BOOL,   &g_vmCsLandDip, 0 },
    { "vmCsLandDipAmt",   CfgEntry::FLOAT,  &g_vmCsLandDipAmt, 0 },
    { "vmCsLandDipTime",  CfgEntry::FLOAT,  &g_vmCsLandDipTime, 0 },
    { "vmAlphaEnable",    CfgEntry::BOOL,   &g_vmAlphaEnable, 0 },
    { "vmAlpha",          CfgEntry::FLOAT,  &g_vmAlpha, 0 },
    { "vmFlatAlpha",      CfgEntry::BOOL,   &g_vmFlatAlpha, 0 },
    { "vmLowFps",         CfgEntry::BOOL,   &g_vmLowFps, 0 },
    { "vmLowFpsRate",     CfgEntry::FLOAT,  &g_vmLowFpsRate, 0 },

    { "debugLog",         CfgEntry::BOOL,   &g_debugLogEnable, 0 },
    { "accentColor",      CfgEntry::FLOAT4, g_accentColor, 0 },
    { "dpiScale",         CfgEntry::FLOAT,  &g_dpiScale, 0 },
    { "safeMode",         CfgEntry::BOOL,   &g_safeMode, 0 },

    { "fogEnable",        CfgEntry::BOOL,   &g_fogEnable, 0 },
    { "fogColor",         CfgEntry::FLOAT4, g_fogColor, 0 },
    { "fogStart",         CfgEntry::FLOAT,  &g_fogStart, 0 },
    { "fogEnd",           CfgEntry::FLOAT,  &g_fogEnd, 0 },
    { "fogDensity",       CfgEntry::FLOAT,  &g_fogDensity, 0 },

    { "fxDepthProbe",     CfgEntry::BOOL,   &g_fxDepthProbe, 0 },
    { "fxForceQueueMode", CfgEntry::BOOL,   &g_fxForceQueueMode, 0 },
    { "fxUseCallQueue",   CfgEntry::BOOL,   &g_fxUseCallQueue, 0 },

    { "aoEnable",         CfgEntry::BOOL,   &g_aoEnable, 0 },
    { "aoIntensity",      CfgEntry::FLOAT,  &g_aoIntensity, 0 },
    { "aoRadius",         CfgEntry::FLOAT,  &g_aoRadius, 0 },
    { "aoSamples",        CfgEntry::INT,    &g_aoSamples, 0 },
    { "aoBias",           CfgEntry::FLOAT,  &g_aoBias, 0 },
    { "aoBlur",           CfgEntry::FLOAT,  &g_aoBlur, 0 },
    { "aoScale",          CfgEntry::FLOAT,  &g_aoScale, 0 },
    { "aoFadeStart",      CfgEntry::FLOAT,  &g_aoFadeStart, 0 },
    { "aoFadeEnd",        CfgEntry::FLOAT,  &g_aoFadeEnd, 0 },
    { "aoViewmodel",      CfgEntry::BOOL,   &g_aoViewmodel, 0 },
    { "aoFogFade",        CfgEntry::BOOL,   &g_aoFogFade, 0 },
    { "aoDebug",          CfgEntry::BOOL,   &g_aoDebug, 0 },

    { "dofEnable",        CfgEntry::BOOL,   &g_dofEnable, 0 },
    { "dofAutoFocus",     CfgEntry::BOOL,   &g_dofAutoFocus, 0 },
    { "dofDistance",      CfgEntry::FLOAT,  &g_dofDistance, 0 },
    { "dofFocusSpeed",    CfgEntry::FLOAT,  &g_dofFocusSpeed, 0 },
    { "dofFocusLimit",    CfgEntry::FLOAT,  &g_dofFocusLimit, 0 },
    { "dofRange",         CfgEntry::FLOAT,  &g_dofRange, 0 },
    { "dofRamp",          CfgEntry::FLOAT,  &g_dofRamp, 0 },
    { "dofMaxBlur",       CfgEntry::FLOAT,  &g_dofMaxBlur, 0 },
    { "dofTapDensity",    CfgEntry::FLOAT,  &g_dofTapDensity, 0 },
    { "dofBokehBoost",    CfgEntry::FLOAT,  &g_dofBokehBoost, 0 },
    { "dofBokehThresh",   CfgEntry::FLOAT,  &g_dofBokehThreshold, 0 },
    { "dofNearBlur",      CfgEntry::BOOL,   &g_dofNearBlur, 0 },
    { "dofViewmodelSharp",CfgEntry::BOOL,   &g_dofViewmodelSharp, 0 },
    { "fxParticleGuard",  CfgEntry::FLOAT,  &g_fxParticleGuard, 0 },
    { "dofSmooth",        CfgEntry::BOOL,   &g_dofSmooth, 0 },

    { "fbEnable",         CfgEntry::BOOL,   &g_fbEnable, 0 },
    { "fbShutter",        CfgEntry::FLOAT,  &g_fbShutter, 0 },
    { "fbAmount",         CfgEntry::FLOAT,  &g_fbAmount, 0 },
    { "fbPlayers",        CfgEntry::BOOL,   &g_fbPlayers, 0 },
    { "fbViewmodel",      CfgEntry::BOOL,   &g_fbViewmodel, 0 },
    { "fbVmAmount",       CfgEntry::FLOAT,  &g_fbVmAmount, 0 },
    { "fbSamples",        CfgEntry::INT,    &g_fbSamples, 0 },
    { "fbMaxStreak",      CfgEntry::FLOAT,  &g_fbMaxStreak, 0 },

    { "bloomEnable",      CfgEntry::BOOL,   &g_bloomEnable, 0 },
    { "bloomIntensity",   CfgEntry::FLOAT,  &g_bloomIntensity, 0 },
    { "bloomThreshold",   CfgEntry::FLOAT,  &g_bloomThreshold, 0 },
    { "bloomRadius",      CfgEntry::FLOAT,  &g_bloomRadius, 0 },
    { "bloomSat",         CfgEntry::FLOAT,  &g_bloomSat, 0 },

    { "ccEnable",         CfgEntry::BOOL,   &g_ccEnable, 0 },
    { "ccGainR",          CfgEntry::FLOAT,  &g_ccGainR, 0 },
    { "ccGainG",          CfgEntry::FLOAT,  &g_ccGainG, 0 },
    { "ccGainB",          CfgEntry::FLOAT,  &g_ccGainB, 0 },
    { "ccExposure",       CfgEntry::FLOAT,  &g_ccExposure, 0 },
    { "ccTemp",           CfgEntry::FLOAT,  &g_ccTemp, 0 },
    { "ccHue",            CfgEntry::FLOAT,  &g_ccHue, 0 },
    { "ccSat",            CfgEntry::FLOAT,  &g_ccSat, 0 },
    { "ccContrast",       CfgEntry::FLOAT,  &g_ccContrast, 0 },
    { "ccLevInBlack",     CfgEntry::FLOAT,  &g_ccLevInBlack, 0 },
    { "ccLevInWhite",     CfgEntry::FLOAT,  &g_ccLevInWhite, 0 },
    { "ccGamma",          CfgEntry::FLOAT,  &g_ccGamma, 0 },
    { "ccLevOutBlack",    CfgEntry::FLOAT,  &g_ccLevOutBlack, 0 },
    { "ccLevOutWhite",    CfgEntry::FLOAT,  &g_ccLevOutWhite, 0 },
    { "ccHighlights",     CfgEntry::FLOAT,  &g_ccHighlights, 0 },
    { "ccShadows",        CfgEntry::FLOAT,  &g_ccShadows, 0 },
    { "ccCurveM",         CfgEntry::CURVE,  &g_ccCurve[0], 0 },
    { "ccCurveR",         CfgEntry::CURVE,  &g_ccCurve[1], 0 },
    { "ccCurveG",         CfgEntry::CURVE,  &g_ccCurve[2], 0 },
    { "ccCurveB",         CfgEntry::CURVE,  &g_ccCurve[3], 0 },

    // -- Unit D: simple screen post-fx --------------------------------------------------
    { "sharpenEnable",     CfgEntry::BOOL,   &g_sharpenEnable, 0 },
    { "sharpenAmount",     CfgEntry::FLOAT,  &g_sharpenAmount, 0 },
    { "filmGrainEnable",   CfgEntry::BOOL,   &g_filmGrainEnable, 0 },
    { "filmGrainAmount",   CfgEntry::FLOAT,  &g_filmGrainAmount, 0 },
    { "vignetteEnable",    CfgEntry::BOOL,   &g_vignetteEnable, 0 },
    { "vignetteAmount",    CfgEntry::FLOAT,  &g_vignetteAmount, 0 },
    { "vignetteInvert",    CfgEntry::BOOL,   &g_vignetteInvert, 0 },
    { "invertFilterEnable",CfgEntry::BOOL,   &g_invertFilterEnable, 0 },
    { "posterizeEnable",   CfgEntry::BOOL,   &g_posterizeEnable, 0 },
    { "posterizeLevels",   CfgEntry::FLOAT,  &g_posterizeLevels, 0 },
    { "debandEnable",      CfgEntry::BOOL,   &g_debandEnable, 0 },
    { "debandAmount",      CfgEntry::FLOAT,  &g_debandAmount, 0 },
    { "channelShiftEnable",CfgEntry::BOOL,   &g_channelShiftEnable, 0 },
    { "channelShiftAmount",CfgEntry::FLOAT,  &g_channelShiftAmount, 0 },
    { "emphasizeEnable",   CfgEntry::BOOL,   &g_emphasizeEnable, 0 },
    { "emphasizeAmount",   CfgEntry::FLOAT,  &g_emphasizeAmount, 0 },
    { "emphasizeSpherical",CfgEntry::BOOL,   &g_emphasizeSpherical, 0 },
    { "showDepthEnable",   CfgEntry::BOOL,   &g_showDepthEnable, 0 },
    { "emphasizeFocusDepth",  CfgEntry::FLOAT,  &g_emphasizeFocusDepth, 0 },
    { "emphasizeFocusRange",  CfgEntry::FLOAT,  &g_emphasizeFocusRange, 0 },
    { "emphasizeFocusEdge",   CfgEntry::FLOAT,  &g_emphasizeFocusEdge, 0 },
    { "emphasizeSphereFov",   CfgEntry::INT,    &g_emphasizeSphereFov, 0 },
    { "emphasizeSphereX",     CfgEntry::FLOAT,  &g_emphasizeSphereX, 0 },
    { "emphasizeSphereY",     CfgEntry::FLOAT,  &g_emphasizeSphereY, 0 },
    { "emphasizeBlendColor",  CfgEntry::FLOAT4, g_emphasizeBlendColor, 0 },
    { "emphasizeBlendFactor", CfgEntry::FLOAT,  &g_emphasizeBlendFactor, 0 },
    { "showDepthType",        CfgEntry::INT,    &g_showDepthType, 0 },
    { "showDepthBlend",       CfgEntry::BOOL,   &g_showDepthBlend, 0 },
    { "showDepthLive",        CfgEntry::BOOL,   &g_showDepthLive, 0 },
    { "showDepthUpsideDown",  CfgEntry::BOOL,   &g_showDepthUpsideDown, 0 },
    { "showDepthReversed",    CfgEntry::BOOL,   &g_showDepthReversed, 0 },
    { "showDepthLog",         CfgEntry::BOOL,   &g_showDepthLog, 0 },
    { "showDepthScaleX",      CfgEntry::FLOAT,  &g_showDepthScaleX, 0 },
    { "showDepthScaleY",      CfgEntry::FLOAT,  &g_showDepthScaleY, 0 },
    { "showDepthOffsetX",     CfgEntry::INT,    &g_showDepthOffsetX, 0 },
    { "showDepthOffsetY",     CfgEntry::INT,    &g_showDepthOffsetY, 0 },
    { "showDepthFarPlane",    CfgEntry::FLOAT,  &g_showDepthFarPlane, 0 },
    { "showDepthMultiplier",  CfgEntry::FLOAT,  &g_showDepthMultiplier, 0 },

    // -- Unit D: world fx ----------------------------------------------------------------
    { "fullbrightEnable",    CfgEntry::BOOL,  &g_fullbrightEnable, 0 },
    { "disableInterpEnable", CfgEntry::BOOL,  &g_disableInterpEnable, 0 },
    { "aspectRatioEnable",   CfgEntry::BOOL,  &g_aspectRatioEnable, 0 },
    { "aspectRatioValue",    CfgEntry::FLOAT, &g_aspectRatioValue, 0 },
    { "worldFovEnable",      CfgEntry::BOOL,  &g_worldFovEnable, 0 },
    { "worldFovAmount",      CfgEntry::FLOAT, &g_worldFovAmount, 0 },
    { "thirdPersonEnable",   CfgEntry::BOOL,  &g_thirdPersonEnable, 0 },
    { "thirdPersonDistance", CfgEntry::FLOAT, &g_thirdPersonDistance, 0 },

    // -- Unit D2: world fx 2 ---------------------------------------------------------------
    { "removeVisualRecoilEnable", CfgEntry::BOOL,   &g_removeVisualRecoilEnable, 0 },
    { "dlightEnable",             CfgEntry::BOOL,   &g_dlightEnable, 0 },
    { "dlightRadius",             CfgEntry::FLOAT,  &g_dlightRadius, 0 },
    { "dlightColor",              CfgEntry::FLOAT4, g_dlightColor, 0 },
    { "sunAngleEnable",           CfgEntry::BOOL,   &g_sunAngleEnable, 0 },
    { "sunAnglePitch",            CfgEntry::FLOAT,  &g_sunAnglePitch, 0 },
    { "sunAngleYaw",               CfgEntry::FLOAT, &g_sunAngleYaw, 0 },
    { "worldTintEnable",          CfgEntry::BOOL,   &g_worldTintEnable, 0 },
    { "worldTintColor",           CfgEntry::FLOAT4, g_worldTintColor, 0 },
    { "propTintEnable",           CfgEntry::BOOL,   &g_propTintEnable, 0 },
    { "propTintColor",            CfgEntry::FLOAT4, g_propTintColor, 0 },
    { "weatherTintEnable",        CfgEntry::BOOL,   &g_weatherTintEnable, 0 },
    { "weatherTintColor",         CfgEntry::FLOAT4, g_weatherTintColor, 0 },

    { "skyEnable",        CfgEntry::BOOL,   &g_skyEnable, 0 },
    { "skyName",          CfgEntry::STR,    g_skyName, sizeof(g_skyName) },

    // -- Unit D3: reflections + minecraft sky/clouds -------------------------------------
    { "reflEnable",       CfgEntry::BOOL,   &g_reflEnable, 0 },
    { "reflIntensity",    CfgEntry::FLOAT,  &g_reflIntensity, 0 },
    { "reflDistance",     CfgEntry::FLOAT,  &g_reflDistance, 0 },
    { "mcWorldEnable",    CfgEntry::BOOL,   &g_mcWorldEnable, 0 },

    { "weatherEnable",    CfgEntry::BOOL,   &g_weatherEnable, 0 },
    { "weatherMode",      CfgEntry::INT,    &g_weatherMode, 0 },
    { "rainAlpha",        CfgEntry::FLOAT,  &g_rainAlpha, 0 },
    { "rainSpeed",        CfgEntry::FLOAT,  &g_rainSpeed, 0 },
    { "rainWidth",        CfgEntry::FLOAT,  &g_rainWidth, 0 },
    { "rainLength",       CfgEntry::FLOAT,  &g_rainLength, 0 },
    { "rainRadius",       CfgEntry::FLOAT,  &g_rainRadius, 0 },
    { "rainSideVel",      CfgEntry::FLOAT,  &g_rainSideVel, 0 },
    { "snowAlpha",        CfgEntry::FLOAT,  &g_snowAlpha, 0 },
    { "snowSpeed",        CfgEntry::FLOAT,  &g_snowSpeed, 0 },
    { "snowWidth",        CfgEntry::FLOAT,  &g_snowWidth, 0 },
    { "snowLength",       CfgEntry::FLOAT,  &g_snowLength, 0 },
    { "snowRadius",       CfgEntry::FLOAT,  &g_snowRadius, 0 },
    { "snowSideVel",      CfgEntry::FLOAT,  &g_snowSideVel, 0 },
    { "snowDensity",      CfgEntry::FLOAT,  &g_snowDensity, 0 },

    { "noHats",           CfgEntry::BOOL,   &g_noHats, 0 },
    { "noUnusual",        CfgEntry::BOOL,   &g_noUnusual, 0 },
    { "selfKillfeedOnly", CfgEntry::BOOL,   &g_selfKillfeedOnly, 0 },
    { "revealScoreboard", CfgEntry::BOOL,   &g_revealScoreboard, 0 },
    { "steamHistory",     CfgEntry::BOOL,   &g_steamHistory, 0 },
    { "removeRagdolls",   CfgEntry::BOOL,   &g_removeRagdolls, 0 },
    { "removePropFade",   CfgEntry::BOOL,   &g_removePropFade, 0 },
    { "fpsBoost",         CfgEntry::BOOL,   &g_fpsBoost, 0 },
    { "removeScreenEffects", CfgEntry::BOOL, &g_removeScreenEffects, 0 },
    { "flipWorld",        CfgEntry::BOOL,   &g_flipWorld, 0 },

    { "lagResolver",      CfgEntry::BOOL,   &g_lagResolver, 0 },
    { "lrYaw",            CfgEntry::BOOL,   &g_lrYaw, 0 },
    { "lrSmoothMs",       CfgEntry::FLOAT,  &g_lrSmoothMs, 0 },
    { "lrExtrapMs",       CfgEntry::FLOAT,  &g_lrExtrapMs, 0 },
    { "lrMaxErr",         CfgEntry::FLOAT,  &g_lrMaxErr, 0 },


    // lowLatency / hiPriority keys deleted 2026-09-05 -- an old cfg carrying them is simply
    // ignored by the parser, which is what should happen: both features are gone.
    { "hiTimerRes",       CfgEntry::BOOL,   &g_hiTimerRes, 0 },
    { "perfEnable",       CfgEntry::BOOL,   &g_perfEnable, 0 },
    // misc convenience buttons, one title/command pair per slot
    { "convTitle0", CfgEntry::STR, g_convTitle[0], sizeof(g_convTitle[0]) }, { "convCmd0", CfgEntry::STR, g_convCmd[0], sizeof(g_convCmd[0]) },
    { "convTitle1", CfgEntry::STR, g_convTitle[1], sizeof(g_convTitle[1]) }, { "convCmd1", CfgEntry::STR, g_convCmd[1], sizeof(g_convCmd[1]) },
    { "convTitle2", CfgEntry::STR, g_convTitle[2], sizeof(g_convTitle[2]) }, { "convCmd2", CfgEntry::STR, g_convCmd[2], sizeof(g_convCmd[2]) },
    { "convTitle3", CfgEntry::STR, g_convTitle[3], sizeof(g_convTitle[3]) }, { "convCmd3", CfgEntry::STR, g_convCmd[3], sizeof(g_convCmd[3]) },
    { "convTitle4", CfgEntry::STR, g_convTitle[4], sizeof(g_convTitle[4]) }, { "convCmd4", CfgEntry::STR, g_convCmd[4], sizeof(g_convCmd[4]) },
    { "convTitle5", CfgEntry::STR, g_convTitle[5], sizeof(g_convTitle[5]) }, { "convCmd5", CfgEntry::STR, g_convCmd[5], sizeof(g_convCmd[5]) },
    { "convTitle6", CfgEntry::STR, g_convTitle[6], sizeof(g_convTitle[6]) }, { "convCmd6", CfgEntry::STR, g_convCmd[6], sizeof(g_convCmd[6]) },
    { "convTitle7", CfgEntry::STR, g_convTitle[7], sizeof(g_convTitle[7]) }, { "convCmd7", CfgEntry::STR, g_convCmd[7], sizeof(g_convCmd[7]) },
    // fonts tab (fonts.h)
    { "fontIndName",      CfgEntry::STR,    g_fontIndName, sizeof(g_fontIndName) },
    { "fontIndSize",      CfgEntry::INT,    &g_fontIndSize, 0 },
    { "fontIndFlags",     CfgEntry::INT,    &g_fontIndFlags, 0 },
    { "fontEspName",      CfgEntry::STR,    g_fontEspName, sizeof(g_fontEspName) },
    { "fontEspSize",      CfgEntry::INT,    &g_fontEspSize, 0 },
    { "fontEspFlags",     CfgEntry::INT,    &g_fontEspFlags, 0 },


    { "bhop",             CfgEntry::BOOL,   &g_bhop, 0 },
    { "jumpbug",          CfgEntry::BOOL,   &g_jumpbug, 0 },
    { "jumpbugKey",       CfgEntry::INT,    &g_jumpbugKey, 0 },
    { "jumpbugMode",      CfgEntry::INT,    &g_jumpbugMode, 0 },
    { "jbYieldSurf",      CfgEntry::BOOL,   &g_jbYieldSurf, 0 },
    { "jbPredict",        CfgEntry::BOOL,   &g_jbPredict, 0 },
    { "autoStrafe",       CfgEntry::BOOL,   &g_autoStrafe, 0 },
    { "autoStrafeKey",    CfgEntry::INT,    &g_autoStrafeKey, 0 },
    { "autoStrafeMode",   CfgEntry::INT,    &g_autoStrafeMode, 0 },
    { "autoStrafeAvoidWalls", CfgEntry::BOOL, &g_autoStrafeAvoidWalls, 0 },
    { "autoStrafeAvoidDist",  CfgEntry::FLOAT,&g_autoStrafeAvoidDist, 0 },
    { "strafeOpt",        CfgEntry::BOOL,   &g_strafeOpt, 0 },
    { "strafeOptGain",    CfgEntry::FLOAT,  &g_strafeOptGain, 0 },
    { "strafeOptMinSpeed",CfgEntry::FLOAT,  &g_strafeOptMinSpeed, 0 },
    { "edgeJump",         CfgEntry::BOOL,   &g_edgeJump, 0 },
    { "edgeJumpKey",      CfgEntry::INT,    &g_edgeJumpKey, 0 },
    { "edgeJumpMode",     CfgEntry::INT,    &g_edgeJumpMode, 0 },
    { "longJump",         CfgEntry::BOOL,   &g_longJump, 0 },
    { "longJumpKey",      CfgEntry::INT,    &g_longJumpKey, 0 },
    { "longJumpMode",     CfgEntry::INT,    &g_longJumpMode, 0 },
    { "longJumpEdge",     CfgEntry::BOOL,   &g_longJumpEdge, 0 },
    { "longJumpAdaptive",       CfgEntry::BOOL,  &g_longJumpAdaptive, 0 },
    { "longJumpAdaptiveLetGo",  CfgEntry::BOOL,  &g_longJumpAdaptiveLetGo, 0 },
    { "longJumpAdaptiveLanded", CfgEntry::BOOL,  &g_longJumpAdaptiveLanded, 0 },
    { "longJumpAdaptiveTimed",  CfgEntry::BOOL,  &g_longJumpAdaptiveTimed, 0 },
    { "longJumpAdaptiveTime",   CfgEntry::FLOAT, &g_longJumpAdaptiveTime, 0 },
    { "miniJump",         CfgEntry::BOOL,   &g_miniJump, 0 },
    { "miniJumpKey",      CfgEntry::INT,    &g_miniJumpKey, 0 },
    { "miniJumpMode",     CfgEntry::INT,    &g_miniJumpMode, 0 },
    { "miniJumpHoldDuck", CfgEntry::BOOL,   &g_miniJumpHoldDuck, 0 },
    { "miniJumpEdge",     CfgEntry::BOOL,   &g_miniJumpEdge, 0 },
    { "fastStop",         CfgEntry::BOOL,   &g_fastStop, 0 },
    { "fastAccel",        CfgEntry::BOOL,   &g_fastAccel, 0 },
    { "noPush",           CfgEntry::BOOL,   &g_noPush, 0 },

    // RESERVED, do not re-use: autoBackstab / backstabKey / backstabMode / backstabSilent /
    // backstabFOV / backstabIgnoreInvuln / backstabIgnoreCloaked / cleanPovDemos. Unknown
    // keys are ignored on load, so an old cfg is harmless -- unless a name is recycled,
    // and then its stale value lands in whatever took the slot.

    { "edgeBug",          CfgEntry::BOOL,   &g_edgeBug, 0 },
    { "edgeBugKey",       CfgEntry::INT,    &g_edgeBugKey, 0 },
    { "edgeBugMode",      CfgEntry::INT,    &g_edgeBugMode, 0 },
    { "edgeBugPredictTime", CfgEntry::FLOAT, &g_edgeBugPredictTime, 0 },
    { "edgeBugDetectMode",  CfgEntry::INT,   &g_edgeBugDetectMode, 0 },
    { "edgeBugMaxBend", CfgEntry::FLOAT, &g_edgeBugStrafeAngleMax, 0 },
    { "edgeBugStance",    CfgEntry::INT,    &g_edgeBugStance, 0 },
    { "edgeBugToEdge",    CfgEntry::BOOL,   &g_edgeBugToEdge, 0 },
    { "edgeBugMouseSteer",CfgEntry::BOOL,   &g_edgeBugMouseSteer, 0 },
    { "edgeBugPaths",     CfgEntry::INT,    &g_edgeBugPaths, 0 },
    { "edgeBugLockType",  CfgEntry::INT,    &g_edgeBugLockType, 0 },
    { "edgeBugLockStrength", CfgEntry::FLOAT, &g_edgeBugLockStrength, 0 },
    { "edgeBugDelTicks",  CfgEntry::INT,    &g_edgeBugDelTicks, 0 },
    { "edgeBugDelAdvanced", CfgEntry::BOOL, &g_edgeBugDelAdvanced, 0 },
    { "edgeBugDelAngleLimit", CfgEntry::FLOAT, &g_edgeBugDelAngleLimit, 0 },
    { "edgeBugDelSearch", CfgEntry::INT,    &g_edgeBugDelSearch, 0 },
    { "edgeBugDelSilent", CfgEntry::BOOL,   &g_edgeBugDelSilent, 0 },
    { "edgeBugDelMouseFix", CfgEntry::BOOL, &g_edgeBugDelMouseFix, 0 },
    { "edgeBugDnaScan",   CfgEntry::FLOAT,  &g_edgeBugDnaScan, 0 },
    { "edgeBugDnaAdvanced", CfgEntry::BOOL, &g_edgeBugDnaAdvanced, 0 },
    { "edgeBugDnaRange",  CfgEntry::INT,    &g_edgeBugDnaRange, 0 },
    { "edgeBugDnaCustomAngle", CfgEntry::BOOL, &g_edgeBugDnaCustomAngle, 0 },
    { "edgeBugDnaAngleLimit", CfgEntry::FLOAT, &g_edgeBugDnaAngleLimit, 0 },
    { "edgeBugViz",       CfgEntry::BOOL,   &g_edgeBugViz, 0 },
    { "edgeBugVizSize",   CfgEntry::FLOAT,  &g_edgeBugVizSize, 0 },
    { "pixelSurf",        CfgEntry::BOOL,   &g_pixelSurf, 0 },
    { "pixelSurfKey",     CfgEntry::INT,    &g_pixelSurfKey, 0 },
    { "pixelSurfMode",    CfgEntry::INT,    &g_pixelSurfMode, 0 },
{ "pixelSurfPredictTicks", CfgEntry::INT, &g_pixelSurfPredictTicks, 0 },
    { "psWallStrafe",     CfgEntry::INT,    &g_psWallStrafe, 0 },
    { "psParkAtEnd",      CfgEntry::BOOL,   &g_psParkAtEnd, 0 },
    { "autoAlign",        CfgEntry::BOOL,   &g_autoAlign, 0 },
    { "textureBug",       CfgEntry::BOOL,   &g_textureBug, 0 },
    { "textureBugKey",    CfgEntry::INT,    &g_textureBugKey, 0 },
    { "textureBugMode",   CfgEntry::INT,    &g_textureBugMode, 0 },
    { "tbStyle",          CfgEntry::INT,    &g_tbStyle, 0 },
    { "tbcAlign",         CfgEntry::BOOL,   &g_tbcAlign, 0 },
    { "tbReach",          CfgEntry::FLOAT,  &g_tbReach, 0 },
    { "tbScanStep",       CfgEntry::FLOAT,  &g_tbScanStep, 0 },
    { "tbRampNz",         CfgEntry::FLOAT,  &g_tbRampNz, 0 },
    { "tbHugRange",       CfgEntry::FLOAT,  &g_tbHugRange, 0 },
    { "tbSolveForward",   CfgEntry::BOOL,   &g_tbSolveForward, 0 },
    { "tbNoOffgridStick", CfgEntry::BOOL,   &g_tbNoOffgridStick, 0 },
    { "tbParkAtEnd",      CfgEntry::BOOL,   &g_tbParkAtEnd, 0 },
    { "tbParkTicks",      CfgEntry::INT,    &g_tbParkTicks, 0 },
    { "tbParkSpeed",      CfgEntry::FLOAT,  &g_tbParkSpeed, 0 },
    { "tbParkBrake",      CfgEntry::BOOL,   &g_tbParkBrake, 0 },
    { "tbParkLead",       CfgEntry::FLOAT,  &g_tbParkLead, 0 },
    { "tbPredict",        CfgEntry::BOOL,   &g_tbPredict, 0 },
    { "tbPredictTicks",   CfgEntry::INT,    &g_tbPredictTicks, 0 },
    { "tbPredictReach",   CfgEntry::FLOAT,  &g_tbPredictReach, 0 },
    { "wallClimb",        CfgEntry::BOOL,   &g_wallClimb, 0 },
    { "wallClimbKey",     CfgEntry::INT,    &g_wallClimbKey, 0 },
    { "wallClimbMode",    CfgEntry::INT,    &g_wallClimbMode, 0 },
    { "airStuck",         CfgEntry::BOOL,   &g_airStuck, 0 },
    { "asExtraReach",     CfgEntry::FLOAT,  &g_asExtraReach, 0 },
    { "asBudget",         CfgEntry::FLOAT,  &g_asBudget, 0 },
    { "airStuckKey",      CfgEntry::INT,    &g_airStuckKey, 0 },
    { "airStuckMode",     CfgEntry::INT,    &g_airStuckMode, 0 },

    // -- Unit A port: small tricks from botox csgo-sdk --
    { "nulls",            CfgEntry::BOOL,   &g_nulls, 0 },
    { "hsw",               CfgEntry::BOOL,  &g_hsw, 0 },
    { "hswKey",            CfgEntry::INT,   &g_hswKey, 0 },
    { "hswMode",           CfgEntry::INT,   &g_hswMode, 0 },
    { "hswSide",           CfgEntry::INT,   &g_hswSide, 0 },
    { "hswAllowWS",        CfgEntry::BOOL,  &g_hswAllowWS, 0 },
    { "hswAllowAD",        CfgEntry::BOOL,  &g_hswAllowAD, 0 },
    { "hswPreGround",      CfgEntry::BOOL,  &g_hswPreGround, 0 },
    { "delayHop",          CfgEntry::BOOL,  &g_delayHop, 0 },
    { "delayHopKey",       CfgEntry::INT,   &g_delayHopKey, 0 },
    { "delayHopMode",      CfgEntry::INT,   &g_delayHopMode, 0 },
    { "delayHopTicks",     CfgEntry::INT,   &g_delayHopTicks, 0 },
    { "autoCrouch",        CfgEntry::BOOL,  &g_autoCrouch, 0 },
    { "autoCrouchKey",     CfgEntry::INT,   &g_autoCrouchKey, 0 },
    { "autoCrouchMode",    CfgEntry::INT,   &g_autoCrouchMode, 0 },
    { "autoCrouchTicks",   CfgEntry::INT,   &g_autoCrouchTicks, 0 },
    { "jumpbugKangaroo",   CfgEntry::BOOL,  &g_jumpbugKangaroo, 0 },
    { "silentView",        CfgEntry::BOOL,  &g_silentView, 0 },
    { "jumpstats",         CfgEntry::BOOL,  &g_jumpstats, 0 },

    // -- Unit A3 port: tung/bounce/pf modes/eb presets/ps fix --
    { "tungSurf",          CfgEntry::BOOL,  &g_tungSurf, 0 },
    { "tungSurfKey",       CfgEntry::INT,   &g_tungSurfKey, 0 },
    { "tungSurfMode",      CfgEntry::INT,   &g_tungSurfMode, 0 },
    { "tungMaxNz",         CfgEntry::FLOAT, &g_tungMaxNz, 0 },
    { "autoBounce",        CfgEntry::BOOL,  &g_autoBounce, 0 },
    { "autoBounceKey",     CfgEntry::INT,   &g_autoBounceKey, 0 },
    { "autoBounceMode",    CfgEntry::INT,   &g_autoBounceMode, 0 },
    { "bounceAssist",      CfgEntry::BOOL,  &g_bounceAssist, 0 },
    { "bounceAssistKey",   CfgEntry::INT,   &g_bounceAssistKey, 0 },
    { "bounceAssistMode",  CfgEntry::INT,   &g_bounceAssistMode, 0 },
    { "bounceReach",       CfgEntry::FLOAT, &g_bounceReach, 0 },
    { "bounceMinSpeed",    CfgEntry::FLOAT, &g_bounceMinSpeed, 0 },
    { "psFinderHeadbounce",CfgEntry::BOOL,  &g_psFinderHeadbounce, 0 },
    { "psFinderPixeljump", CfgEntry::BOOL,  &g_psFinderPixeljump, 0 },
    { "psSurfFix",         CfgEntry::BOOL,  &g_psSurfFix, 0 },
    { "edgeBugStyle",      CfgEntry::INT,   &g_edgeBugStyle, 0 },
    { "edgeBugMouseLock",  CfgEntry::BOOL,  &g_edgeBugMouseLock, 0 },
    { "edgeBugSound",      CfgEntry::BOOL,  &g_edgeBugSound, 0 },
    { "edgeBugSoundId",    CfgEntry::INT,   &g_edgeBugSoundId, 0 },
    // -- Unit A2 port: edge skip + blockbot from botox csgo-sdk --
    { "edgeSkip",          CfgEntry::BOOL,  &g_edgeSkip, 0 },
    { "edgeSkipKey",       CfgEntry::INT,   &g_edgeSkipKey, 0 },
    { "edgeSkipMode",      CfgEntry::INT,   &g_edgeSkipMode, 0 },
    { "edgeSkipTicks",     CfgEntry::INT,   &g_edgeSkipTicks, 0 },
    { "edgeSkipDepth",     CfgEntry::INT,   &g_edgeSkipDepth, 0 },
    { "edgeSkipMaxBend",   CfgEntry::FLOAT, &g_edgeSkipMaxBend, 0 },
    { "blockBot",          CfgEntry::BOOL,  &g_blockBot, 0 },
    { "blockBotKey",       CfgEntry::INT,   &g_blockBotKey, 0 },
    { "blockBotMode",      CfgEntry::INT,   &g_blockBotMode, 0 },

    { "fmEnabled",        CfgEntry::BOOL,   &g_fmEnabled, 0 },
    { "fmWallSurf",       CfgEntry::BOOL,   &g_fmWallSurf, 0 },
    { "fmWallSurfKey",    CfgEntry::INT,    &g_fmWallSurfKey, 0 },
    { "fmWallSurfMode",   CfgEntry::INT,    &g_fmWallSurfMode, 0 },
    { "fmWallJump",       CfgEntry::BOOL,   &g_fmWallJump, 0 },
    { "fmWallJumpKey",    CfgEntry::INT,    &g_fmWallJumpKey, 0 },
    { "fmWallJumpMode",   CfgEntry::INT,    &g_fmWallJumpMode, 0 },
    { "fmWallReach",      CfgEntry::FLOAT,  &g_fmWallReach, 0 },
    { "fmWallJumpPush",   CfgEntry::FLOAT,  &g_fmWallJumpPush, 0 },
    { "fmWallJumpHold",   CfgEntry::BOOL,   &g_fmWallJumpHold, 0 },
    { "fmWallSurfSeam",   CfgEntry::BOOL,   &g_fmWallSurfSeam, 0 },
    { "fmWallJumpSeam",   CfgEntry::BOOL,   &g_fmWallJumpSeam, 0 },
    { "fmSeamSlack",      CfgEntry::FLOAT,  &g_fmSeamSlack, 0 },
    { "fmHighJump",       CfgEntry::BOOL,   &g_fmHighJump, 0 },
    { "fmHighJumpKey",    CfgEntry::INT,    &g_fmHighJumpKey, 0 },
    { "fmHighJumpMode",   CfgEntry::INT,    &g_fmHighJumpMode, 0 },
    { "fmHighJumpMult",   CfgEntry::FLOAT,  &g_fmHighJumpMult, 0 },
    { "fmAirStuck",       CfgEntry::BOOL,   &g_fmAirStuck, 0 },
    { "fmAirStuckKey",    CfgEntry::INT,    &g_fmAirStuckKey, 0 },
    { "fmAirStuckMode",   CfgEntry::INT,    &g_fmAirStuckMode, 0 },
    { "fmAirStuckKeepVel",CfgEntry::BOOL,   &g_fmAirStuckKeepVel, 0 },
    { "fmAirStuckNearOnly",CfgEntry::BOOL,  &g_fmAirStuckNearOnly, 0 },
    { "fmAirStuckNearDist",CfgEntry::FLOAT, &g_fmAirStuckNearDist, 0 },
    { "fmNoFall",         CfgEntry::BOOL,   &g_fmNoFall, 0 },
    { "fmNoFallKey",      CfgEntry::INT,    &g_fmNoFallKey, 0 },
    { "fmNoFallMode",     CfgEntry::INT,    &g_fmNoFallMode, 0 },
    { "fmWallbugFix",     CfgEntry::BOOL,   &g_fmWallbugFix, 0 },
    { "fmCrouchJump",     CfgEntry::BOOL,   &g_fmCrouchJump, 0 },
    { "fmCrouchJumpKey",  CfgEntry::INT,    &g_fmCrouchJumpKey, 0 },
    { "fmCrouchJumpMode", CfgEntry::INT,    &g_fmCrouchJumpMode, 0 },
    { "fmRampBoost",      CfgEntry::BOOL,   &g_fmRampBoost, 0 },
    { "fmRampBoostKey",   CfgEntry::INT,    &g_fmRampBoostKey, 0 },
    { "fmRampBoostMode",  CfgEntry::INT,    &g_fmRampBoostMode, 0 },
    { "fmRampBoostMult",  CfgEntry::FLOAT,  &g_fmRampBoostMult, 0 },
    { "fmHeadSurf",       CfgEntry::BOOL,   &g_fmHeadSurf, 0 },
    { "fmHeadSurfKey",    CfgEntry::INT,    &g_fmHeadSurfKey, 0 },
    { "fmHeadSurfMode",   CfgEntry::INT,    &g_fmHeadSurfMode, 0 },
    { "fmHeadSurfSnap",   CfgEntry::FLOAT,  &g_fmHeadSurfSnap, 0 },
    { "fmHeadSurfLedgeOnly", CfgEntry::BOOL, &g_fmHeadSurfLedgeOnly, 0 },
    { "fmHeadSurfBottoms", CfgEntry::BOOL,  &g_fmHeadSurfBottoms, 0 },
    { "psFinder",         CfgEntry::BOOL,   &g_psFinder, 0 },
    { "psFinderKey",      CfgEntry::INT,    &g_psFinderKey, 0 },
    { "psFinderMode",     CfgEntry::INT,    &g_psFinderMode, 0 },
    { "psFinderMargin",   CfgEntry::FLOAT,  &g_psFinderMargin, 0 },
    { "psFinderBudget",   CfgEntry::FLOAT,  &g_psFinderBudget, 0 },
    { "psFinderStrict",   CfgEntry::BOOL,   &g_psFinderStrict, 0 },
    { "psFinderTb",       CfgEntry::BOOL,   &g_psFinderTb, 0 },
    { "psFinderNoProps",  CfgEntry::BOOL,   &g_psFinderNoProps, 0 },
    { "psMapLines",       CfgEntry::BOOL,   &g_psMapLines, 0 },
    { "psAssist",         CfgEntry::BOOL,   &g_psAssist, 0 },
    { "psAssistKey",      CfgEntry::INT,    &g_psAssistKey, 0 },
    { "psAssistMode",     CfgEntry::INT,    &g_psAssistMode, 0 },
    { "psSetPointKey",    CfgEntry::INT,    &g_psSetPointKey, 0 },
    { "psDeletePointKey", CfgEntry::INT,    &g_psDeletePointKey, 0 },
    { "psAssistRadius",   CfgEntry::FLOAT,  &g_psAssistRadius, 0 },
    { "psAssistSnap",     CfgEntry::FLOAT,  &g_psAssistSnap, 0 },
    { "psAssistMaxPoints", CfgEntry::INT,   &g_psAssistMaxPoints, 0 },
    { "psAssistReuseDelay", CfgEntry::FLOAT, &g_psAssistReuseDelay, 0 },
    // psAssistSteer deleted (steer lock). Stale keys in an old cfg are ignored on load
    { "psAssistRender",   CfgEntry::BOOL,   &g_psAssistRender, 0 },
    { "psAssistJumpBox",  CfgEntry::BOOL,   &g_psAssistJumpBox, 0 },
    { "psGroundHelp",     CfgEntry::BOOL,   &g_psGroundHelp, 0 },
    { "psGroundHelpAngle",CfgEntry::FLOAT,  &g_psGroundHelpAngle, 0 },
    { "psLaunchZSlack",   CfgEntry::FLOAT,  &g_psLaunchZSlack, 0 },

    { "routeCalc",        CfgEntry::BOOL,   &g_routeCalc, 0 },
    { "rcAddKey",         CfgEntry::INT,    &g_rcAddKey, 0 },
    { "rcDeleteKey",      CfgEntry::INT,    &g_rcDeleteKey, 0 },
    { "rcClearKey",       CfgEntry::INT,    &g_rcClearKey, 0 },
    { "rcSolveKey",       CfgEntry::INT,    &g_rcSolveKey, 0 },
    // the move list is 6 bits with new meanings, so the key is new too -- an old rcMoves
    // (13 bits) would read as the wrong moves. Unmatched old keys are skipped on load.
    // rcBudget removed (dihrc worker thread): the solve runs off the game thread now, nothing
    // left to budget per cmd. An old rcBudget line in a saved cfg is just skipped.
    { "rcMoveBits",       CfgEntry::INT,    &g_rcMoveMask, 0 },   // was rcMoveSet (6 bits): renamed so the delay hop bit (7th) starts on
    { "rcMaxShown",       CfgEntry::INT,    &g_rcMaxShown, 0 },
    { "rcStartHere",      CfgEntry::BOOL,   &g_rcStartHere, 0 },
    // rcShowMarkers removed: botox always draws the points (an old line is skipped on load)
    { "rcShowPath",       CfgEntry::BOOL,   &g_rcShowPath, 0 },
    { "rcShowPanel",      CfgEntry::BOOL,   &g_rcShowPanel, 0 },
    { "rcPopupTime",      CfgEntry::INT,    &g_rcPopupTime, 0 },
    { "rcStartMoves",     CfgEntry::INT,    &g_rcStartMoves, 0 },
    { "rcDelayTicks",     CfgEntry::INT,    &g_rcDelayTicks, 0 },
    { "rcShowKeys",       CfgEntry::BOOL,   &g_rcShowKeys, 0 },
    { "rcPathAlpha",      CfgEntry::INT,    &g_rcPathAlpha, 0 },
    { "rcPathThick",      CfgEntry::FLOAT,  &g_rcPathThick, 0 },
    { "rcAllowInvalid",   CfgEntry::BOOL,   &g_rcAllowInvalid, 0 },
    { "rcSnapEdge",       CfgEntry::BOOL,   &g_rcSnapEdge, 0 },
    { "rcAdvanced",       CfgEntry::BOOL,   &g_rcAdvanced, 0 },
    { "pixelCalc",        CfgEntry::BOOL,   &g_pixelCalc, 0 },
    { "pcPlaceKey",       CfgEntry::INT,    &g_pcPlaceKey, 0 },
    { "pcSolveKey",       CfgEntry::INT,    &g_pcSolveKey, 0 },
    { "pcShowMarker",     CfgEntry::BOOL,   &g_pcShowMarker, 0 },
    { "pcShowPanel",      CfgEntry::BOOL,   &g_pcShowPanel, 0 },

    { "prInfAmmo",        CfgEntry::BOOL,   &g_prInfAmmo, 0 },
    { "prInfHealth",      CfgEntry::BOOL,   &g_prInfHealth, 0 },
    { "prInfTime",        CfgEntry::BOOL,   &g_prInfTime, 0 },
    { "prNoclipKey",      CfgEntry::INT,    &g_prNoclipKey, 0 },
    { "prSaveKey",        CfgEntry::INT,    &g_prSaveKey, 0 },
    { "prTpKey",          CfgEntry::INT,    &g_prTpKey, 0 },
    { "prBotEnemyKey",    CfgEntry::INT,    &g_prBotEnemyKey, 0 },
    { "prBotMateKey",     CfgEntry::INT,    &g_prBotMateKey, 0 },
    { "prSentryKey",      CfgEntry::INT,    &g_prSentryKey, 0 },
    { "prDispenserKey",   CfgEntry::INT,    &g_prDispenserKey, 0 },
    { "prTeleporterKey",  CfgEntry::INT,    &g_prTeleporterKey, 0 },
    { "prBotClass",       CfgEntry::INT,    &g_prBotClass, 0 },

    { "espEnable",               CfgEntry::BOOL,   &g_espEnable, 0 },
    { "espTeammates",            CfgEntry::BOOL,   &g_espTeammates, 0 },
    { "espMaxDistance",          CfgEntry::FLOAT,  &g_espMaxDistance, 0 },
    { "espDistanceFade",         CfgEntry::BOOL,   &g_espDistanceFade, 0 },
    { "espSoundOnly",            CfgEntry::BOOL,   &g_espSoundOnly, 0 },
    { "espSoundDuration",        CfgEntry::FLOAT,  &g_espSoundDuration, 0 },
    { "espSoundFade",            CfgEntry::BOOL,   &g_espSoundFade, 0 },
    { "espBox",                  CfgEntry::BOOL,   &g_espBox, 0 },
    { "espBoxVisColors",         CfgEntry::BOOL,   &g_espBoxVisColors, 0 },
    { "espBoxVisibleColor",      CfgEntry::FLOAT4, g_espBoxVisibleColor, 0 },
    { "espBoxInvisibleColor",    CfgEntry::FLOAT4, g_espBoxInvisibleColor, 0 },
    { "espBoxColor",             CfgEntry::FLOAT4, g_espBoxColor, 0 },
    { "espBox3d",                CfgEntry::BOOL,   &g_espBox3d, 0 },
    { "espBoxCorner",            CfgEntry::BOOL,   &g_espBoxCorner, 0 },
    { "espBoxThickness",         CfgEntry::FLOAT,  &g_espBoxThickness, 0 },
    { "espBoxRounding",          CfgEntry::FLOAT,  &g_espBoxRounding, 0 },
    { "espBoxOutline",           CfgEntry::BOOL,   &g_espBoxOutline, 0 },
    { "espBoxOutlineColor",      CfgEntry::FLOAT4, g_espBoxOutlineColor, 0 },
    { "espBoxOutlineThickness",  CfgEntry::FLOAT,  &g_espBoxOutlineThickness, 0 },
    { "espBoxOutlineSide",       CfgEntry::INT,    &g_espBoxOutlineSide, 0 },
    { "espBoxFill",              CfgEntry::BOOL,   &g_espBoxFill, 0 },
    { "espBoxFillColor",         CfgEntry::FLOAT4, g_espBoxFillColor, 0 },
    { "espBoxFillGradient",      CfgEntry::BOOL,   &g_espBoxFillGradient, 0 },
    { "espBoxFillBottomColor",   CfgEntry::FLOAT4, g_espBoxFillBottomColor, 0 },
    { "espName",                 CfgEntry::BOOL,   &g_espName, 0 },
    { "espNamePosition",         CfgEntry::INT,    &g_espNamePosition, 0 },
    { "espNameVisColors",        CfgEntry::BOOL,   &g_espNameVisColors, 0 },
    { "espNameVisibleColor",     CfgEntry::FLOAT4, g_espNameVisibleColor, 0 },
    { "espNameInvisibleColor",   CfgEntry::FLOAT4, g_espNameInvisibleColor, 0 },
    { "espNameColor",            CfgEntry::FLOAT4, g_espNameColor, 0 },
    { "espHealthBar",            CfgEntry::BOOL,   &g_espHealthBar, 0 },
    { "espHealthBarSide",        CfgEntry::INT,    &g_espHealthBarSide, 0 },
    { "espHealthBarCornerPick",  CfgEntry::INT,    &g_espHealthBarCornerPick, 0 },
    { "espHealthBarCornerRounding", CfgEntry::FLOAT,  &g_espHealthBarCornerRounding, 0 },
    { "espHealthBarCornerSize",  CfgEntry::FLOAT,  &g_espHealthBarCornerSize, 0 },
    { "espHealthBarThickness",   CfgEntry::FLOAT,  &g_espHealthBarThickness, 0 },
    { "espHealthBarOutline",     CfgEntry::BOOL,   &g_espHealthBarOutline, 0 },
    { "espHealthBarCustomColor", CfgEntry::BOOL,   &g_espHealthBarCustomColor, 0 },
    { "espHealthBarColor",       CfgEntry::FLOAT4, g_espHealthBarColor, 0 },
    { "espHealthBarGradient",    CfgEntry::BOOL,   &g_espHealthBarGradient, 0 },
    { "espHealthBarBottomColor", CfgEntry::FLOAT4, g_espHealthBarBottomColor, 0 },
    { "espHealthBarBgColor",     CfgEntry::FLOAT4, g_espHealthBarBgColor, 0 },
    { "espHealthText",           CfgEntry::BOOL,   &g_espHealthText, 0 },
    { "espHealthSuffix",         CfgEntry::BOOL,   &g_espHealthSuffix, 0 },
    { "espHealthTextStyle",      CfgEntry::INT,    &g_espHealthTextStyle, 0 },
    { "espHealthTextCustomColor", CfgEntry::BOOL,   &g_espHealthTextCustomColor, 0 },
    { "espHealthTextColor",      CfgEntry::FLOAT4, g_espHealthTextColor, 0 },
    { "espWeaponName",           CfgEntry::BOOL,   &g_espWeaponName, 0 },
    { "espWeaponNameColor",      CfgEntry::FLOAT4, g_espWeaponNameColor, 0 },
    { "espAmmoBar",              CfgEntry::BOOL,   &g_espAmmoBar, 0 },
    { "espAmmoBarColor",         CfgEntry::FLOAT4, g_espAmmoBarColor, 0 },
    { "espDistance",             CfgEntry::BOOL,   &g_espDistance, 0 },
    { "espDistanceUnit",         CfgEntry::INT,    &g_espDistanceUnit, 0 },
    { "espDistanceColor",        CfgEntry::FLOAT4, g_espDistanceColor, 0 },
    { "espFlags",                CfgEntry::BOOL,   &g_espFlags, 0 },
    { "espFlagList",             CfgEntry::INT,    &g_espFlagList, 0 },
    { "espFlagsColor",           CfgEntry::FLOAT4, g_espFlagsColor, 0 },
    { "espFlagsAlertColor",      CfgEntry::FLOAT4, g_espFlagsAlertColor, 0 },
    { "espSkeleton",             CfgEntry::BOOL,   &g_espSkeleton, 0 },
    { "espSkeletonVisColors",    CfgEntry::BOOL,   &g_espSkeletonVisColors, 0 },
    { "espSkeletonVisibleColor", CfgEntry::FLOAT4, g_espSkeletonVisibleColor, 0 },
    { "espSkeletonInvisibleColor", CfgEntry::FLOAT4, g_espSkeletonInvisibleColor, 0 },
    { "espSkeletonColor",        CfgEntry::FLOAT4, g_espSkeletonColor, 0 },
    { "espSkeletonType",         CfgEntry::INT,    &g_espSkeletonType, 0 },
    { "espSkeletonThickness",    CfgEntry::FLOAT,  &g_espSkeletonThickness, 0 },
    { "espHitboxes",             CfgEntry::BOOL,   &g_espHitboxes, 0 },
    { "espHitboxesMode",         CfgEntry::INT,    &g_espHitboxesMode, 0 },
    { "espHitboxColor",          CfgEntry::FLOAT4, g_espHitboxColor, 0 },
    { "espHitboxesThickness",    CfgEntry::FLOAT,  &g_espHitboxesThickness, 0 },
    { "espHitboxesDuration",     CfgEntry::FLOAT,  &g_espHitboxesDuration, 0 },
    { "espBacktrackTrail",       CfgEntry::BOOL,   &g_espBacktrackTrail, 0 },
    { "espAvatar",               CfgEntry::BOOL,   &g_espAvatar, 0 },
    { "espAvatarPosition",       CfgEntry::INT,    &g_espAvatarPosition, 0 },
    { "espAvatarSize",           CfgEntry::FLOAT,  &g_espAvatarSize, 0 },
    { "espAvatarHeadScale",      CfgEntry::FLOAT,  &g_espAvatarHeadScale, 0 },
    { "espDamageNumbers",        CfgEntry::BOOL,   &g_espDamageNumbers, 0 },
    { "espDmgColor",             CfgEntry::FLOAT4, g_espDmgColor, 0 },
    { "espDmgDuration",          CfgEntry::FLOAT,  &g_espDmgDuration, 0 },
    { "espDmgSpeed",             CfgEntry::FLOAT,  &g_espDmgSpeed, 0 },
    { "espDmgStack",             CfgEntry::BOOL,   &g_espDmgStack, 0 },
    { "espOofArrows",            CfgEntry::BOOL,   &g_espOofArrows, 0 },
    { "espOofType",              CfgEntry::INT,    &g_espOofType, 0 },
    { "espOofColor",             CfgEntry::FLOAT4, g_espOofColor, 0 },
    { "espOofWallColor",         CfgEntry::FLOAT4, g_espOofWallColor, 0 },
    { "espOofSoundColor",        CfgEntry::FLOAT4, g_espOofSoundColor, 0 },
    { "espOofSoundDuration",     CfgEntry::FLOAT,  &g_espOofSoundDuration, 0 },
    { "espOofSoundFade",         CfgEntry::BOOL,   &g_espOofSoundFade, 0 },
    { "espOofStyle",             CfgEntry::INT,    &g_espOofStyle, 0 },
    { "espOofWidth",             CfgEntry::FLOAT,  &g_espOofWidth, 0 },
    { "espOofHeight",            CfgEntry::FLOAT,  &g_espOofHeight, 0 },
    { "espOofDistance",          CfgEntry::INT,    &g_espOofDistance, 0 },
    { "espOofAvatar",            CfgEntry::BOOL,   &g_espOofAvatar, 0 },
    { "espOofAspect",            CfgEntry::BOOL,   &g_espOofAspect, 0 },
    { "espSoundPings",           CfgEntry::BOOL,   &g_espSoundPings, 0 },
    { "espSoundColor",           CfgEntry::FLOAT4, g_espSoundColor, 0 },
    { "espBuildings",            CfgEntry::BOOL,   &g_espBuildings, 0 },
    { "espBuildingColor",        CfgEntry::FLOAT4, g_espBuildingColor, 0 },
    { "espBldEnemyBox",          CfgEntry::BOOL,   &g_espBldEnemyBox, 0 },
    { "espBldEnemyName",         CfgEntry::BOOL,   &g_espBldEnemyName, 0 },
    { "espBldEnemyHealthBar",    CfgEntry::BOOL,   &g_espBldEnemyHealthBar, 0 },
    { "espBldEnemyHealthText",   CfgEntry::BOOL,   &g_espBldEnemyHealthText, 0 },
    { "espBldEnemyFlags",        CfgEntry::BOOL,   &g_espBldEnemyFlags, 0 },
    { "espBldEnemyDistance",     CfgEntry::BOOL,   &g_espBldEnemyDistance, 0 },
    { "espBldTeam",              CfgEntry::BOOL,   &g_espBldTeam, 0 },
    { "espBldTeamOwnOnly",       CfgEntry::BOOL,   &g_espBldTeamOwnOnly, 0 },
    { "espBldTeamColor",         CfgEntry::FLOAT4, g_espBldTeamColor, 0 },
    { "espBldTeamBox",           CfgEntry::BOOL,   &g_espBldTeamBox, 0 },
    { "espBldTeamName",          CfgEntry::BOOL,   &g_espBldTeamName, 0 },
    { "espBldTeamHealthBar",     CfgEntry::BOOL,   &g_espBldTeamHealthBar, 0 },
    { "espBldTeamHealthText",    CfgEntry::BOOL,   &g_espBldTeamHealthText, 0 },
    { "espBldTeamFlags",         CfgEntry::BOOL,   &g_espBldTeamFlags, 0 },
    { "espBldTeamDistance",      CfgEntry::BOOL,   &g_espBldTeamDistance, 0 },
    { "espWorldEnable",          CfgEntry::BOOL,   &g_espWorldEnable, 0 },
    { "espAmmoPacks",            CfgEntry::BOOL,   &g_espAmmoPacks, 0 },
    { "espDroppedWeapons",       CfgEntry::BOOL,   &g_espDroppedWeapons, 0 },
    { "espProjectiles",          CfgEntry::BOOL,   &g_espProjectiles, 0 },
    { "espProjPath",             CfgEntry::BOOL,   &g_espProjPath, 0 },
    { "espProjPathColor",        CfgEntry::FLOAT4, g_espProjPathColor, 0 },
    { "espItemColor",            CfgEntry::FLOAT4, g_espItemColor, 0 },
    { "espProjColor",            CfgEntry::FLOAT4, g_espProjColor, 0 },

    { "chamsEnable",       CfgEntry::BOOL,   &g_chamsEnable, 0 },
    { "chamsEnemy",        CfgEntry::BOOL,   &g_chamsEnemy, 0 },
    { "chamsTeam",         CfgEntry::BOOL,   &g_chamsTeam, 0 },
    { "chamsEnemyColor",   CfgEntry::FLOAT4, g_chamsEnemyColor, 0 },
    { "chamsTeamColor",    CfgEntry::FLOAT4, g_chamsTeamColor, 0 },

    { "glowEnable",        CfgEntry::BOOL,   &g_glowEnable, 0 },
    { "glowEnemy",         CfgEntry::BOOL,   &g_glowEnemy, 0 },
    { "glowTeam",          CfgEntry::BOOL,   &g_glowTeam, 0 },
    { "glowEnemyColor",    CfgEntry::FLOAT4, g_glowEnemyColor, 0 },
    { "glowTeamColor",     CfgEntry::FLOAT4, g_glowTeamColor, 0 },
    { "glowBuildings",     CfgEntry::BOOL,   &g_glowBuildings, 0 },
    { "glowBuildingColor", CfgEntry::FLOAT4, g_glowBuildingColor, 0 },
    { "glowAlpha",         CfgEntry::FLOAT,  &g_glowAlpha, 0 },

    // 10-31 chams/glow rework (aim/chams.cpp)
    { "chamsLocalEnable",       CfgEntry::BOOL,   &g_chamsLocalEnable, 0 },
    { "chamsRagdolls",          CfgEntry::BOOL,   &g_chamsRagdolls, 0 },
    { "chamsBacktrackXqz",      CfgEntry::BOOL,   &g_chamsBacktrackXqz, 0 },
    { "chamsBacktrackType",     CfgEntry::INT,    &g_chamsBacktrackType, 0 },
    { "chamsBacktrackMaxGhosts",CfgEntry::INT,    &g_chamsBacktrackMaxGhosts, 0 },
    { "chamsBacktrackGradient", CfgEntry::BOOL,   &g_chamsBacktrackGradient, 0 },
    { "chamsSoundDuration",     CfgEntry::FLOAT,  &g_chamsSoundDuration, 0 },
    { "chamsSoundFade",         CfgEntry::BOOL,   &g_chamsSoundFade, 0 },
    { "chamsSoundMinVolume",    CfgEntry::FLOAT,  &g_chamsSoundMinVolume, 0 },
    { "chamsSoundMaxDistance",  CfgEntry::FLOAT,  &g_chamsSoundMaxDistance, 0 },
    { "chamsVisible",           CfgEntry::STR,    g_chamsVisibleCfg, sizeof(g_chamsVisibleCfg) },
    { "chamsOccluded",          CfgEntry::STR,    g_chamsOccludedCfg, sizeof(g_chamsOccludedCfg) },
    { "chamsBacktrack",         CfgEntry::STR,    g_chamsBacktrackCfg, sizeof(g_chamsBacktrackCfg) },
    { "chamsSound",             CfgEntry::STR,    g_chamsSoundCfg, sizeof(g_chamsSoundCfg) },
    { "chamsTeamVisible",       CfgEntry::STR,    g_chamsTeamVisibleCfg, sizeof(g_chamsTeamVisibleCfg) },
    { "chamsTeamOccluded",      CfgEntry::STR,    g_chamsTeamOccludedCfg, sizeof(g_chamsTeamOccludedCfg) },
    { "chamsWeapon",            CfgEntry::STR,    g_chamsWeaponCfg, sizeof(g_chamsWeaponCfg) },
    { "chamsArms",              CfgEntry::STR,    g_chamsArmsCfg, sizeof(g_chamsArmsCfg) },
    { "chamsSleeve",            CfgEntry::STR,    g_chamsSleeveCfg, sizeof(g_chamsSleeveCfg) },
    { "glowVisColor",           CfgEntry::FLOAT4, g_glowVisColor, 0 },
    { "glowInvisColor",         CfgEntry::FLOAT4, g_glowInvisColor, 0 },
    { "glowLegacy",             CfgEntry::BOOL,   &g_glowLegacy, 0 },
    { "glowLegacyStyle",        CfgEntry::INT,    &g_glowLegacyStyle, 0 },
    { "glowNormalBlend",        CfgEntry::BOOL,   &g_glowNormalBlend, 0 },
    { "glowThickness",          CfgEntry::FLOAT,  &g_glowThickness, 0 },
    { "glowGradient",           CfgEntry::BOOL,   &g_glowGradient, 0 },
    { "glowVisOuterColor",      CfgEntry::FLOAT4, g_glowVisOuterColor, 0 },
    { "glowInvisOuterColor",    CfgEntry::FLOAT4, g_glowInvisOuterColor, 0 },
    { "glowGradientInner",      CfgEntry::FLOAT,  &g_glowGradientInner, 0 },
    { "glowWave",               CfgEntry::BOOL,   &g_glowWave, 0 },
    { "glowWaveColor",          CfgEntry::FLOAT4, g_glowWaveColor, 0 },
    { "glowWaveSpeed",          CfgEntry::FLOAT,  &g_glowWaveSpeed, 0 },
    { "glowWaveSize",           CfgEntry::FLOAT,  &g_glowWaveSize, 0 },
    { "glowWaveStrength",       CfgEntry::FLOAT,  &g_glowWaveStrength, 0 },
    { "glowPulse",              CfgEntry::BOOL,   &g_glowPulse, 0 },
    { "glowPulseSpeed",         CfgEntry::FLOAT,  &g_glowPulseSpeed, 0 },
    { "glowPulseMin",           CfgEntry::FLOAT,  &g_glowPulseMin, 0 },

    { "chEnable",         CfgEntry::BOOL,   &g_chEnable, 0 },
    { "chAntialias",      CfgEntry::BOOL,   &g_chAntialias, 0 },
    { "chAdditive",       CfgEntry::BOOL,   &g_chAdditive, 0 },   // kept: old cfgs' additive
    { "chBlendMode",      CfgEntry::INT,    &g_chBlendMode, 0 },
    { "chColor",          CfgEntry::FLOAT4, g_chColor, 0 },
    { "chThickness",      CfgEntry::FLOAT,  &g_chThickness, 0 },
    { "chLength",         CfgEntry::FLOAT,  &g_chLength, 0 },
    { "chGap",            CfgEntry::FLOAT,  &g_chGap, 0 },
    { "chAspect",         CfgEntry::FLOAT,  &g_chAspect, 0 },
    { "chGapAspect",      CfgEntry::FLOAT,  &g_chGapAspect, 0 },
    { "chDot",            CfgEntry::BOOL,   &g_chDot, 0 },
    { "chDotCircle",      CfgEntry::BOOL,   &g_chDotCircle, 0 },
    { "chOutline",        CfgEntry::BOOL,   &g_chOutline, 0 },
    { "chOutlineColor",   CfgEntry::FLOAT4, g_chOutlineColor, 0 },
    { "chOutlineThick",   CfgEntry::FLOAT,  &g_chOutlineThick, 0 },
    { "scCsgo",           CfgEntry::BOOL,   &g_scCsgo, 0 },
    { "scCsgoWeapon",     CfgEntry::INT,    &g_scCsgoWeapon, 0 },
    { "scCsgoZoom",       CfgEntry::INT,    &g_scCsgoZoom, 0 },
    { "scCsgoWidth",      CfgEntry::INT,    &g_scCsgoWidth, 0 },
    { "scCsgoNormalInacc",CfgEntry::BOOL,   &g_scCsgoNormalInacc, 0 },
    { "scCsgoMoveSens",   CfgEntry::FLOAT,  &g_scCsgoMoveSens, 0 },
    { "scCsgoAirSens",    CfgEntry::FLOAT,  &g_scCsgoAirSens, 0 },
    { "scCsgoScopeIn",    CfgEntry::BOOL,   &g_scCsgoScopeIn, 0 },
    { "scCsgoBob",        CfgEntry::BOOL,   &g_scCsgoBob, 0 },
    { "scCsgoBobScale",   CfgEntry::FLOAT,  &g_scCsgoBobScale, 0 },
    { "scCsgoLens",       CfgEntry::BOOL,   &g_scCsgoLens, 0 },
    { "scCsgoScale",      CfgEntry::FLOAT,  &g_scCsgoScale, 0 },
    { "scCsgoNoDot",      CfgEntry::BOOL,   &g_scCsgoNoDot, 0 },
    { "scChargeHide",     CfgEntry::BOOL,   &g_scChargeHide, 0 },
    { "scCsgoOpacity",    CfgEntry::FLOAT,  &g_scCsgoOpacity, 0 },
    { "scCsgoBobLat",     CfgEntry::FLOAT,  &g_scCsgoBobLat, 0 },
    { "scCsgoBobVert",    CfgEntry::FLOAT,  &g_scCsgoBobVert, 0 },
    { "chCsgoEnable",     CfgEntry::BOOL,   &g_chCsgoEnable, 0 },
    { "chCsgoStyle",      CfgEntry::INT,    &g_chCsgoStyle, 0 },
    { "chCsgoColor",      CfgEntry::FLOAT4, g_chCsgoColor, 0 },
    { "chCsgoGap",        CfgEntry::FLOAT,  &g_chCsgoGap, 0 },
    { "chCsgoScale",      CfgEntry::FLOAT,  &g_chCsgoScale, 0 },
    { "chCsgoCrisp",      CfgEntry::BOOL,   &g_chCsgoCrisp, 0 },
    { "chCsgoLinear",     CfgEntry::BOOL,   &g_chCsgoLinear, 0 },
    { "chCsgoAdditive",   CfgEntry::BOOL,   &g_chCsgoAdditive, 0 },
    { "chCsgoBlack",      CfgEntry::FLOAT,  &g_chCsgoBlack, 0 },
    { "chCsgoVelSens",    CfgEntry::FLOAT,  &g_chCsgoVelSens, 0 },
    { "chCsgoSmooth",     CfgEntry::FLOAT,  &g_chCsgoSmooth, 0 },
    { "chCsgoArcs",       CfgEntry::BOOL,   &g_chCsgoArcs, 0 },
    { "chCsgoPips",       CfgEntry::BOOL,   &g_chCsgoPips, 0 },
    { "chCsgoAspect",     CfgEntry::FLOAT,  &g_chCsgoAspect, 0 },
    { "chCsgoGapAspect",  CfgEntry::FLOAT,  &g_chCsgoGapAspect, 0 },
    { "csXSize",          CfgEntry::FLOAT,  &g_csXSize, 0 },
    { "csXThick",         CfgEntry::FLOAT,  &g_csXThick, 0 },
    { "csXGap",           CfgEntry::FLOAT,  &g_csXGap, 0 },
    { "csXDot",           CfgEntry::BOOL,   &g_csXDot, 0 },
    { "csXOutline",       CfgEntry::BOOL,   &g_csXOutline, 0 },
    { "csXOutlineTh",     CfgEntry::FLOAT,  &g_csXOutlineTh, 0 },
    { "csXT",             CfgEntry::BOOL,   &g_csXT, 0 },
    { "csXAlpha",         CfgEntry::INT,    &g_csXAlpha, 0 },
    { "csXUseAlpha",      CfgEntry::BOOL,   &g_csXUseAlpha, 0 },
    { "csXSplitDist",     CfgEntry::FLOAT,  &g_csXSplitDist, 0 },
    { "csXSplitRatio",    CfgEntry::FLOAT,  &g_csXSplitRatio, 0 },
    { "csXInnerMod",      CfgEntry::FLOAT,  &g_csXInnerMod, 0 },
    { "csXOuterMod",      CfgEntry::FLOAT,  &g_csXOuterMod, 0 },
    { "staticAmbyCrosshair", CfgEntry::BOOL, &g_staticAmbyCrosshair, 0 },

    { "pureBypass",       CfgEntry::BOOL,   &g_pureBypass, 0 },
    { "cheatsBypass",     CfgEntry::BOOL,   &g_cheatsBypass, 0 },
    { "f2pChatBypass",    CfgEntry::BOOL,   &g_f2pChatBypass, 0 },

    { "regionEnable",      CfgEntry::BOOL,  &g_regionEnable, 0 },
    { "regionMask",        CfgEntry::INT,   &g_regionMask, 0 },
    { "netSettingsEnable", CfgEntry::BOOL,  &g_netSettingsEnable, 0 },
    { "netSettingsSel",    CfgEntry::INT,   &g_netSettingsSel, 0 },
    { "netSettingsRates",  CfgEntry::BOOL,  &g_netSettingsRates, 0 },
    { "ngColorEnable",     CfgEntry::BOOL,  &g_ngColorEnable, 0 },
    { "ngTextColor",       CfgEntry::FLOAT4, g_ngTextColor, 0 },
    { "ngOrangeColor",     CfgEntry::FLOAT4, g_ngOrangeColor, 0 },
    { "ngRedColor",        CfgEntry::FLOAT4, g_ngRedColor, 0 },
    { "ngYellowColor",     CfgEntry::FLOAT4, g_ngYellowColor, 0 },
    { "ngPosEnable",       CfgEntry::BOOL,  &g_ngPosEnable, 0 },
    { "ngPosX",            CfgEntry::INT,   &g_ngPosX, 0 },
    { "ngPosY",            CfgEntry::INT,   &g_ngPosY, 0 },

    { "equipRegionUnlock", CfgEntry::BOOL,  &g_equipRegionUnlock, 0 },

    // ---- unit B: velocity graph ----
    { "vgEnable",       CfgEntry::BOOL,   &g_vgEnable, 0 },
    { "vgLength",        CfgEntry::INT,    &g_vgLength, 0 },
    { "vgWidth",         CfgEntry::INT,    &g_vgWidth, 0 },
    { "vgHeight",        CfgEntry::INT,    &g_vgHeight, 0 },
    { "vgOffsetX",       CfgEntry::INT,    &g_vgOffsetX, 0 },
    { "vgPosition",      CfgEntry::INT,    &g_vgPosition, 0 },
    { "vgMax",           CfgEntry::FLOAT,  &g_vgMax, 0 },
    { "vgFadeSides",     CfgEntry::BOOL,   &g_vgFadeSides, 0 },
    { "vgFade",          CfgEntry::FLOAT,  &g_vgFade, 0 },
    { "vgColor",         CfgEntry::FLOAT4, g_vgColor, 0 },
    { "vgColorGain",     CfgEntry::FLOAT4, g_vgColorGain, 0 },
    { "vgColorLoss",     CfgEntry::FLOAT4, g_vgColorLoss, 0 },
    { "vgFillColor",     CfgEntry::FLOAT4, g_vgFillColor, 0 },
    { "vgGainColors",    CfgEntry::BOOL,   &g_vgGainColors, 0 },
    { "vgFill",          CfgEntry::BOOL,   &g_vgFill, 0 },
    { "vgThickness",     CfgEntry::FLOAT,  &g_vgThickness, 0 },
    { "vgJumpSpeeds",    CfgEntry::BOOL,   &g_vgJumpSpeeds, 0 },
    { "vgTextColor",     CfgEntry::FLOAT4, g_vgTextColor, 0 },
    { "vgTextShadow",    CfgEntry::BOOL,   &g_vgTextShadow, 0 },
    { "vgTextScale",     CfgEntry::FLOAT,  &g_vgTextScale, 0 },

    // ---- unit B: keystroke indicator ----
    { "ksEnable",        CfgEntry::BOOL,   &g_ksEnable, 0 },
    { "ksLayout",        CfgEntry::INT,    &g_ksLayout, 0 },
    { "ksDuckJump",       CfgEntry::BOOL,   &g_ksDuckJump, 0 },
    { "ksMouse",          CfgEntry::BOOL,   &g_ksMouse, 0 },
    { "ksReleased",      CfgEntry::INT,    &g_ksReleased, 0 },
    { "ksColor",         CfgEntry::FLOAT4, g_ksColor, 0 },
    { "ksColorReleased", CfgEntry::FLOAT4, g_ksColorReleased, 0 },
    { "ksFade",          CfgEntry::FLOAT,  &g_ksFade, 0 },
    { "ksScale",         CfgEntry::FLOAT,  &g_ksScale, 0 },
    { "ksGapX",          CfgEntry::INT,    &g_ksGapX, 0 },
    { "ksGapY",          CfgEntry::INT,    &g_ksGapY, 0 },
    { "ksOffsetX",       CfgEntry::INT,    &g_ksOffsetX, 0 },
    { "ksPosition",      CfgEntry::INT,    &g_ksPosition, 0 },
    { "ksShadow",        CfgEntry::BOOL,   &g_ksShadow, 0 },

    // ---- unit B: keybind indicator decorations ----
    { "miParticles",     CfgEntry::BOOL,   &g_miParticles, 0 },
    { "miRandomColor",   CfgEntry::BOOL,   &g_miRandomColor, 0 },
    { "miCustomLabels",  CfgEntry::BOOL,   &g_miCustomLabels, 0 },
    { "miLabel0",  CfgEntry::STR, g_miLabels[0],  sizeof(g_miLabels[0]) },
    { "miLabel1",  CfgEntry::STR, g_miLabels[1],  sizeof(g_miLabels[1]) },
    { "miLabel2",  CfgEntry::STR, g_miLabels[2],  sizeof(g_miLabels[2]) },
    { "miLabel3",  CfgEntry::STR, g_miLabels[3],  sizeof(g_miLabels[3]) },
    { "miLabel4",  CfgEntry::STR, g_miLabels[4],  sizeof(g_miLabels[4]) },
    { "miLabel5",  CfgEntry::STR, g_miLabels[5],  sizeof(g_miLabels[5]) },
    { "miLabel6",  CfgEntry::STR, g_miLabels[6],  sizeof(g_miLabels[6]) },
    { "miLabel7",  CfgEntry::STR, g_miLabels[7],  sizeof(g_miLabels[7]) },
    { "miLabel8",  CfgEntry::STR, g_miLabels[8],  sizeof(g_miLabels[8]) },
    { "miLabel9",  CfgEntry::STR, g_miLabels[9],  sizeof(g_miLabels[9]) },
    { "miLabel10", CfgEntry::STR, g_miLabels[10], sizeof(g_miLabels[10]) },

    // ---- unit B: detections ----
    { "detections",         CfgEntry::BOOL,   &g_detections, 0 },
    { "detectionStackEb",   CfgEntry::BOOL,   &g_detectionStackEb, 0 },
    { "detectionTypes",     CfgEntry::INT,    &g_detectionTypes, 0 },
    { "detectionLabel",     CfgEntry::STR,    g_detectionLabel, sizeof(g_detectionLabel) },

    // ---- unit B: pixelsurf lines ----
    { "psLinesEnable",     CfgEntry::BOOL,   &g_psLinesEnable, 0 },
    { "psLinesFull",       CfgEntry::BOOL,   &g_psLinesFull, 0 },
    { "psLinesColor",      CfgEntry::FLOAT4, g_psLinesColor, 0 },
    { "psLinesFullColor",  CfgEntry::FLOAT4, g_psLinesFullColor, 0 },

    // ---- unit B: movement recorder ----
    { "recEnable",          CfgEntry::BOOL, &g_recEnable, 0 },
    { "recShowLine",        CfgEntry::BOOL, &g_recShowLine, 0 },
    { "recRender",          CfgEntry::BOOL, &g_recRender, 0 },
    { "recPosition",        CfgEntry::INT,  &g_recPosition, 0 },
    { "recLockVa",          CfgEntry::BOOL, &g_recLockVa, 0 },
    { "recLockGoingToStart",CfgEntry::BOOL, &g_recLockGoingToStart, 0 },
    { "recClipper",         CfgEntry::BOOL, &g_recClipper, 0 },
    { "recClipperBox",      CfgEntry::BOOL, &g_recClipperBox, 0 },
    { "recClipSeconds",     CfgEntry::FLOAT,&g_recClipSeconds, 0 },
    { "recStopOnMove",      CfgEntry::BOOL, &g_recStopOnMove, 0 },
    { "recForceWeapon",     CfgEntry::BOOL, &g_recForceWeapon, 0 },
    { "recYaw",             CfgEntry::INT,  &g_recYaw, 0 },
    { "recSpinSpeed",       CfgEntry::FLOAT,&g_recSpinSpeed, 0 },
    { "recSmoothStart",     CfgEntry::FLOAT,&g_recSmoothStart, 0 },
    { "recSmoothEnd",       CfgEntry::FLOAT,&g_recSmoothEnd, 0 },
    { "recKeyStartRec",     CfgEntry::INT,  &g_recKeyStartRec, 0 },
    { "recKeyStopRec",      CfgEntry::INT,  &g_recKeyStopRec, 0 },
    { "recKeySave",         CfgEntry::INT,  &g_recKeySave, 0 },
    { "recKeyStartPlay",    CfgEntry::INT,  &g_recKeyStartPlay, 0 },
    { "recKeyStopPlay",     CfgEntry::INT,  &g_recKeyStopPlay, 0 },
    { "recKeyClear",        CfgEntry::INT,  &g_recKeyClear, 0 },
    { "recKeyClip",         CfgEntry::INT,  &g_recKeyClip, 0 },
};

// botox g_config.folder(visuals): visuals files live one folder down
static constexpr const char* kVisualsDir = "C:\\botox-tf2\\visuals";

const char* ConfigFolder(bool visuals) { return visuals ? kVisualsDir : CONFIG_DIR; }

void SaveConfig(const char* path, bool visuals)
{
    _mkdir(CONFIG_DIR);
    if (visuals) _mkdir(kVisualsDir);
    FILE* f = fopen(path, "w");
    if (!f) return;

    for (const auto& e : kEntries)
    {
        if (visuals && !IsVisualCfgKey(e.key)) continue;
        switch (e.type)
        {
        case CfgEntry::BOOL:
            fprintf(f, "%s=%d\n", e.key, *(bool*)e.ptr ? 1 : 0);
            break;
        case CfgEntry::INT:
            fprintf(f, "%s=%d\n", e.key, *(int*)e.ptr);
            break;
        case CfgEntry::FLOAT:
            fprintf(f, "%s=%.4f\n", e.key, *(float*)e.ptr);
            break;
        case CfgEntry::FLOAT4: {
            float* c = (float*)e.ptr;
            fprintf(f, "%s=%.4f %.4f %.4f %.4f\n", e.key, c[0], c[1], c[2], c[3]);
            break;
        }
        case CfgEntry::STR:
            fprintf(f, "%s=%s\n", e.key, (char*)e.ptr);
            break;
        case CfgEntry::CURVE: {
            const CcCurve* c = (const CcCurve*)e.ptr;
            const int n = (c->n < 2) ? 2 : (c->n > k_ccMaxPts ? k_ccMaxPts : c->n);
            fprintf(f, "%s=%d", e.key, n);
            for (int i = 0; i < n; ++i) fprintf(f, ";%.4f,%.4f", c->x[i], c->y[i]);
            fputc('\n', f);
            break;
        }
        }
    }

    // botox: skins (inventory tab) and aimbot vars are never in a visuals file
    if (!visuals)
    {
        // Skinchanger: a variable number of "skin_<def>_<field>" lines, one block per weapon that
        // has an entry, so it cannot live in the flat kEntries table.
        Skins_SaveCfg(f);

        // Aimbot vars (src/hacks/aim/aim.cpp). Appended after dih's own entries;
        // they are keyed by their registered "Vars::..." name so they cannot collide with kEntries.
        Aim_SaveVars(f);
    }

    fclose(f);
    DebugLog("config saved%s: %s", visuals ? " (visuals)" : "", path);
}

// name -> "<ConfigFolder>\<name>.cfg". Path chars stripped so a name can't leave the folder.
// Returns false if nothing usable left.
static bool CfgPathFromName(const char* name, char* out, size_t outSz, bool visuals)
{
    char clean[128]; size_t n = 0;
    for (const char* p = name; *p && n + 1 < sizeof(clean); ++p)
        if (!strchr("\\/:*?\"<>|", *p) && (unsigned char)*p >= 32) clean[n++] = *p;
    while (n && (clean[n-1] == ' ' || clean[n-1] == '.')) --n;   // Windows drops these anyway
    clean[n] = '\0';
    if (n > 4 && _stricmp(clean + n - 4, ".cfg") == 0) clean[n -= 4] = '\0';
    if (!n) return false;
    snprintf(out, outSz, "%s\\%s.cfg", ConfigFolder(visuals), clean);
    return true;
}

std::vector<std::string> ListConfigs(bool visuals)
{
    std::vector<std::string> out;
    char pat[MAX_PATH];
    snprintf(pat, sizeof(pat), "%s\\*.cfg", ConfigFolder(visuals));
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

void SaveConfigNamed(const char* name, bool visuals)
{
    char path[MAX_PATH];
    if (CfgPathFromName(name, path, sizeof(path), visuals)) SaveConfig(path, visuals);
}

void LoadConfigNamed(const char* name, bool visuals)
{
    char path[MAX_PATH];
    if (CfgPathFromName(name, path, sizeof(path), visuals)) LoadConfig(path, visuals);
}

bool DeleteConfigNamed(const char* name, bool visuals)
{
    char path[MAX_PATH];
    if (!CfgPathFromName(name, path, sizeof(path), visuals)) return false;
    const bool ok = DeleteFileA(path) != 0;
    DebugLog("config %s: %s (err %lu)", ok ? "removed" : "remove FAILED", path, ok ? 0ul : GetLastError());
    return ok;
}

void LoadConfig(const char* path, bool visuals)
{
    FILE* f = fopen(path, "r");
    if (!f) return;

    // MI_HS and MI_BH left the indicator table and MI_AST joined it, so every bit above
    // MI_TB in a saved miShowMask now points at a different tag. "miOnlyActive" was written by
    // every build before this one and by none after, so its presence IS the old layout -- remap
    // once off it instead of silently handing the user someone else's picks
    bool bLegacyMi = false;

    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = line;
        char* val = eq + 1;
        // strip trailing newline
        size_t vl = strlen(val);
        while (vl > 0 && (val[vl-1] == '\n' || val[vl-1] == '\r')) val[--vl] = '\0';

        // botox: a full cfg dropped in the visuals folder still only touches visuals
        if (visuals)
        {
            if (!IsVisualCfgKey(key)) continue;
        }
        else if (strcmp(key, "miOnlyActive") == 0) bLegacyMi = true;

        // Aimbot var? Its key is namespaced ("Vars::..."), so it can never match one of
        // dih's own keys, and consuming it here keeps it out of the kEntries walk.
        if (Aim_LoadVar(key, val)) continue;
        // Same for the skinchanger's "enableSkins" / "skin_<def>_<field>" block.
        if (Skins_LoadCfg(key, val)) continue;
        // esp rows from before the botox players port: one box combo (0 off, 1 corner, 2 full)
        // and one "health" row that drew bar + number. Never written now, read once into the new rows
        if (strcmp(key, "espBoxStyle") == 0) { const int v = atoi(val); g_espBox = v != 0; g_espBoxCorner = v == 1; continue; }
        if (strcmp(key, "espHealth") == 0)   { g_espHealthBar = g_espHealthText = atoi(val) != 0; continue; }

        for (const auto& e : kEntries)
        {
            if (strcmp(key, e.key) != 0) continue;
            switch (e.type)
            {
            case CfgEntry::BOOL:  *(bool*)e.ptr  = (atoi(val) != 0); break;
            case CfgEntry::INT:   *(int*)e.ptr   = atoi(val);        break;
            case CfgEntry::FLOAT: *(float*)e.ptr = (float)atof(val); break;
            case CfgEntry::FLOAT4: {
                float* c = (float*)e.ptr;
                sscanf(val, "%f %f %f %f", &c[0], &c[1], &c[2], &c[3]);
                break;
            }
            case CfgEntry::STR:
                strncpy((char*)e.ptr, val, e.size - 1);
                ((char*)e.ptr)[e.size - 1] = '\0';
                break;
            case CfgEntry::CURVE:
                if (!ParseCurve(val, *(CcCurve*)e.ptr))
                {
                    CcResetCurve(*(CcCurve*)e.ptr);
                    DebugLog("config: %s did not parse -- curve reset to identity", e.key);
                }
                break;
            }
            break;
        }
    }
    fclose(f);

    if (bLegacyMi)
    {
        // old bit -> new bit. -1 = the tag is gone (hs at 1, bhop at 7). MI_AST is new, so it
        // is not in the table at all and comes up ON, the way a fresh install shows it
        static const int kMiRemap[12] = { 0, -1, 1, 2, 3, 5, 6, -1, 7, 8, 9, 10 };
        const int old = g_miShowMask;
        int fresh = 1 << MI_AST;
        for (int i = 0; i < 12; ++i)
            if ((old & (1 << i)) && kMiRemap[i] >= 0) fresh |= 1 << kMiRemap[i];
        g_miShowMask = fresh;
        DebugLog("config: miShowMask %d -> %d (indicator slots renumbered: hs/bhop dropped, "
                 "ast added)", old, fresh);
    }

    // ---- post-load range clamps -------------------------------------------------------
    // a saved value below a slider floor survives forever -- load is a plain atof
    // round-trip and every later save writes it straight back. That is how tbEngageDist sat
    // at 8 for four builds while its floor moved to 32, which read as a code bug. Only knobs
    // whose out-of-range value DISABLES a feature are listed; a plain preference is left
    // exactly as saved. whyLo / whyHi name what that extreme breaks, and a clamp that has one
    // logs it -- a silently corrected cfg is how the case above went unnoticed for so long.
    // kNoLo / kNoHi = that end was never bounded. Do not invent a bound for it.
    constexpr float kNoLo = -FLT_MAX, kNoHi = FLT_MAX;

    struct FloatClamp { float* p; float lo, hi; const char* name; const char* whyLo; const char* whyHi; };
    static const FloatClamp kFloatClamps[] = {
        // tbHugRange replaced tbEngageDist as the one TB knob whose zero disables the
        // hold -- the creep is what keeps a ride alive between pins.
        { &g_tbHugRange,   0.05f, kNoHi,  "tbHugRange",   "the creep hold stops working", nullptr },
        { &g_tbReach,      1.f,   kNoHi,  "tbReach",      nullptr, nullptr },
        { &g_rcPathThick,  1.f,   8.f,    "rcPathThick",  nullptr, nullptr },
        { &g_tbScanStep,   1.f,   kNoHi,  "tbScanStep",   nullptr, nullptr },
        // the live scan's own max_normal_z is 0.35, so a saved 0.35 makes the hands-off
        // gate reject NOTHING and TB glues itself to every ramp and displacement it can see.
        // 0.25 is the loosest value that still refuses something the scan accepts.
        { &g_tbRampNz,     kNoLo, 0.25f,  "tbRampNz",     nullptr, "the ramp gate becomes a no-op" },
        // a park cap under the wallbug's own xy<1 gate disables the park outright.
        { &g_tbParkSpeed,  5.f,   250.f,  "tbParkSpeed",  nullptr, nullptr },
        // 0 lead is legal (brake at the exact solved distance); the ceiling stops a
        // garbage cfg braking the moment a wall is seen. Both ends still park.
        { &g_tbParkLead,   0.f,   128.f,  "tbParkLead",   nullptr, nullptr },
        // reach 1 = the hull's own width, i.e. the live scan again. The point of the
        // predicted scan is a face the flight passes BESIDE, so 1 is the useful minimum.
        { &g_tbPredictReach, 1.f, 96.f,   "tbPredictReach", nullptr, nullptr },
        // botox air_stuck: the scan reach is clamped 0-32 in the feature too; the clock is a
        // share of one tick, 90 is where botox's own c_sim_budget clamps
        { &g_asExtraReach, 0.f,   32.f,   "asExtraReach", nullptr, nullptr },
        { &g_asBudget,     10.f,  90.f,   "asBudget",     nullptr, nullptr },
        // the radius is the assist's single engage distance (a point may override it,
        // see PsAssistPt::radius). A hull flush on the seam's wall reads ~24u, so under 40 is
        // a radius it could never fire from.
        { &g_psAssistRadius, 40.f, 1024.f, "psAssistRadius",
          "the assist could never fire from it", nullptr },
        // 0 is a MEANING here, not an extreme -- no reuse penalty, plain nearest-first.
        { &g_psAssistReuseDelay, 0.f, 8.f, "psAssistReuseDelay", nullptr, nullptr },
        // the ground steer is a cap off your OWN heading: 0 would steer nowhere, 90 would let it
        // walk you sideways into the wall you are lining up against
        { &g_psGroundHelpAngle, 5.f, 90.f, "psGroundHelpAngle",
          "the ground steer could never turn you", nullptr },
        // 0 is a meaning: fire a queued launch whatever the floor did since it was validated
        { &g_psLaunchZSlack, 0.f, 32.f, "psLaunchZSlack", nullptr,
          "wider than the band any hop catches in, so the guard never refuses" },
        // the pad is the drawn footprint of the bug tick, not a hitbox -- both ends are taste
        { &g_edgeBugVizSize, 2.f, 64.f, "edgeBugVizSize", nullptr, nullptr },
        { &g_edgeBugPredictTime, 0.02f, 2.f, "edgeBugPredictTime", nullptr, nullptr },
        { &g_edgeBugStrafeAngleMax, 0.f, 180.f, "edgeBugMaxBend", nullptr, nullptr },
        { &g_edgeBugLockStrength, 0.f, 100.f, "edgeBugLockStrength", nullptr, nullptr },
        { &g_edgeBugDelAngleLimit, 0.f, 180.f, "edgeBugDelAngleLimit", nullptr, nullptr },
        { &g_edgeBugDnaScan, 0.f, 1.f, "edgeBugDnaScan", nullptr, nullptr },
        { &g_edgeBugDnaAngleLimit, 0.f, 45.f, "edgeBugDnaAngleLimit", nullptr, nullptr },
        // Transparent viewmodel: 0 is a real choice (invisible), but outside 0..1 is a blend
        // the engine would either clip or read as garbage.
        { &g_vmAlpha,      0.f,   1.f,    "vmAlpha",      nullptr, nullptr },
        // Lag resolver: each of the three has an extreme that turns the feature off while the
        // checkbox still reads on.
        { &g_lrSmoothMs,   15.f,  200.f,  "lrSmoothMs",   "there is no smoothing left", nullptr },
        { &g_lrExtrapMs,   40.f,  400.f,  "lrExtrapMs",   "stalls re-freeze", nullptr },
        { &g_lrMaxErr,     8.f,   128.f,  "lrMaxErr",     "it snaps every frame", nullptr },
        // Colour correction: only the values whose extreme is a BROKEN picture, not a taste.
        // Gains, exposure, contrast and the tone lifts are left exactly as saved.
        { &g_ccGamma,      0.01f, 10.f,   "ccGamma",      "the shader would divide by zero", nullptr },
        { &g_ccLevInBlack, 0.f,   0.99f,  "ccLevInBlack", nullptr, nullptr },
        { &g_ccLevInWhite, 0.f,   1.f,    "ccLevInWhite", nullptr, nullptr },
        { &g_ccLevOutBlack,0.f,   1.f,    "ccLevOutBlack",nullptr, nullptr },
        { &g_ccLevOutWhite,0.f,   1.f,    "ccLevOutWhite",nullptr, nullptr },
        { &g_ccSat,        0.f,   kNoHi,  "ccSat",        nullptr, nullptr },
        { &g_ccGainR,      0.f,   kNoHi,  "ccGainR",      nullptr, nullptr },
        { &g_ccGainG,      0.f,   kNoHi,  "ccGainG",      nullptr, nullptr },
        { &g_ccGainB,      0.f,   kNoHi,  "ccGainB",      nullptr, nullptr },
        // Weather. Every cfg written before the defaults were fixed holds 0 in all six snow
        // keys -- the whole "snow does nothing" report -- and three of those zeros kill it on
        // their own: speed divides ComputeEmissionArea's fallTime (NaN box, every particle
        // culled), length scales the tracer delta to nothing (Tracer_ComputeVerts drops every
        // flake on sqLength < 1e-3), radius is the emission box edge (0 area = 0 particles).
        // alpha 0 is legal on purpose, but 0 for BOTH modes only came from those zero defaults.
        { &g_rainAlpha,    0.05f, 1.f,    "rainAlpha",    "weather draws nothing", nullptr },
        { &g_rainSpeed,    50.f,  2000.f, "rainSpeed",    "weather draws nothing", nullptr },
        { &g_rainWidth,    0.05f, 5.f,    "rainWidth",    "weather draws nothing", nullptr },
        { &g_rainLength,   0.01f, 1.f,    "rainLength",   "weather draws nothing", nullptr },
        { &g_rainRadius,   256.f, 5000.f, "rainRadius",   "weather draws nothing", nullptr },
        { &g_rainSideVel,  0.f,   500.f,  "rainSideVel",  nullptr, nullptr },  // 0 = straight down, still rains
        { &g_snowAlpha,    0.05f, 1.f,    "snowAlpha",    "weather draws nothing", nullptr },
        { &g_snowSpeed,    20.f,  500.f,  "snowSpeed",    "weather draws nothing", nullptr },
        { &g_snowWidth,    0.05f, 10.f,   "snowWidth",    "weather draws nothing", nullptr },
        { &g_snowLength,   0.01f, 1.f,    "snowLength",   "weather draws nothing", nullptr },
        { &g_snowRadius,   256.f, 5000.f, "snowRadius",   "weather draws nothing", nullptr },
        { &g_snowSideVel,  0.f,   100.f,  "snowSideVel",  nullptr, nullptr },
        { &g_snowDensity,  0.05f, 1.f,    "snowDensity",  "weather draws nothing", nullptr },
    };

    struct IntClamp { int* p; int lo, hi; const char* name; const char* whyLo; const char* whyHi; };
    static const IntClamp kIntClamps[] = {
        // 0 means "hold the park forever" on purpose, so only a negative is nonsense.
        // 256 stops a garbage cfg gluing the player to a wall for four seconds.
        { &g_tbParkTicks,        0,  256, "tbParkTicks",        nullptr, nullptr },
        // combo index: off / keys / always
        { &g_psWallStrafe,       0,  2,   "psWallStrafe",       nullptr, nullptr },
        // 8 ticks of flight (0.12s) is short but still predicts; the ceiling is the
        // traced-path array.
        { &g_tbPredictTicks,     8,  128, "tbPredictTicks",     nullptr, nullptr },
        // the EB search array is 132 entries (2.0s at TF2's 66 tick, where the old
        // 128 was 2.0s at CSGO's 64). Above it the search clamps silently and the menu lies.
        // 0 any / 1 ducked / 2 standing. Anything else is a filter that matches no run
        { &g_edgeBugStance,      0,  2,   "edgeBugStance",
          "not a stance, so every attempt would be refused",
          "not a stance, so every attempt would be refused" },
        { &g_edgeBugDetectMode,  0,  2,   "edgeBugDetectMode",  nullptr, nullptr },
        { &g_edgeBugPaths,       1,  4,   "edgeBugPaths",       nullptr, nullptr },
        { &g_edgeBugLockType,    0,  1,   "edgeBugLockType",    nullptr, nullptr },
        { &g_edgeBugStyle,       0,  2,   "edgeBugStyle",       nullptr, nullptr },
        { &g_edgeBugDelTicks,    0,  128, "edgeBugDelTicks",    nullptr, nullptr },
        { &g_edgeBugDelSearch,   1,  15,  "edgeBugDelSearch",   nullptr, nullptr },
        { &g_edgeBugDnaRange,    1,  10,  "edgeBugDnaRange",    nullptr, nullptr },
        // 0 look-ahead runs no sims at all, so the slider's own floor kills the hunt.
        // Past 32 it only spends the command sim pool on flight free fall already describes.
        { &g_pixelSurfPredictTicks, 1, 32, "pixelSurfPredictTicks",
          "no look-ahead runs, so pixel surf could never latch", nullptr },
        // 0 points = the assist looks at nothing.
        { &g_psAssistMaxPoints,  1,  8,   "psAssistMaxPoints",
          "no point is simmed, so the assist could never fire", nullptr },
        // the mask is what a route is ALLOWED to press -- an empty one solves nothing
        // and blames the markers
        { &g_rcMoveMask,         1,  0x7F, "rcMoveBits",
          "no move is allowed, so every route is empty", nullptr },
        { &g_rcMaxShown,         1,  64,  "rcMaxShown",         nullptr, nullptr },
        { &g_rcStartMoves,       1,  0x7F, "rcStartMoves",
          "the starting jump may press nothing, so every route is empty", nullptr },
        { &g_rcDelayTicks,       0,  8,   "rcDelayTicks",       nullptr, nullptr },
        { &g_rcPathAlpha,        0,  255, "rcPathAlpha",        nullptr, nullptr },
    };

    for (const auto& c : kFloatClamps)
    {
        // NaN fails both comparisons below, so it would survive the clamp and poison every
        // formula downstream. atof("nan") in a hand-edited cfg is all it takes.
        if (!(*c.p == *c.p)) {
            const float safe = (c.lo > kNoLo) ? c.lo : ((c.hi < kNoHi) ? c.hi : 0.f);
            DebugLog("config: %s is not a number -- reset to %.3f", c.name, safe);
            *c.p = safe;
        }
        else if (*c.p < c.lo) {
            if (c.whyLo) DebugLog("config: %s=%.3f -- %s. clamped to %.3f", c.name, *c.p, c.whyLo, c.lo);
            *c.p = c.lo;
        } else if (*c.p > c.hi) {
            if (c.whyHi) DebugLog("config: %s=%.3f -- %s. clamped to %.3f", c.name, *c.p, c.whyHi, c.hi);
            *c.p = c.hi;
        }
    }
    for (const auto& c : kIntClamps)
    {
        if (*c.p < c.lo) {
            if (c.whyLo) DebugLog("config: %s=%d -- %s. clamped to %d", c.name, *c.p, c.whyLo, c.lo);
            *c.p = c.lo;
        } else if (*c.p > c.hi) {
            if (c.whyHi) DebugLog("config: %s=%d -- %s. clamped to %d", c.name, *c.p, c.whyHi, c.hi);
            *c.p = c.hi;
        }
    }

    // Cross-check, run on the values the table just settled: an input white at or under the
    // input black turns the whole frame into two colours.
    if (g_ccLevInWhite < g_ccLevInBlack + 0.01f)
    {
        DebugLog("config: ccLevInWhite=%.4f is at or under ccLevInBlack=%.4f (frame would be "
                 "two colours) -- lifted to %.4f", g_ccLevInWhite, g_ccLevInBlack,
                 g_ccLevInBlack + 0.01f);
        g_ccLevInWhite = g_ccLevInBlack + 0.01f;
    }

    DebugLog("config loaded%s: %s", visuals ? " (visuals)" : "", path);
}
