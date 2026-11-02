#pragma once
// Skinchanger pieces with no engine in them -- tests/skins_test.cpp runs these.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../../globals/types.h"   // SkinCfg

// One weapon's config as "skin_<def>_<field>=value\n" lines. Same text in the cfg file
// (Skins_SaveCfg) and on the wire (skinshare_pure.h). Returns chars written, 0 = no room.
inline int SkinPure_Format(char* out, size_t cap, int id, const SkinCfg& c)
{
    const int n = snprintf(out, cap,
        "skin_%d_enabled=%d\nskin_%d_paintKitId=%d\nskin_%d_pattern=%llu\nskin_%d_wear=%.4f\n"
        "skin_%d_ksTier=%d\nskin_%d_ksSheen=%d\nskin_%d_ksEffect=%d\nskin_%d_ksCustomColor=%d\n"
        "skin_%d_ksColor=%.4f %.4f %.4f\nskin_%d_unusual=%d\nskin_%d_australium=%d\n"
        "skin_%d_festivized=%d\nskin_%d_festive=%d\nskin_%d_statClock=%d\nskin_%d_statKills=%d\n"
        "skin_%d_spellFlags=%d\nskin_%d_weaponSkinDef=%d\nskin_%d_reskinDef=%d\nskin_%d_reskinStyle=%d\n",
        id, c.enabled ? 1 : 0, id, c.paintKitId, id, (unsigned long long)c.pattern, id, c.wear,
        id, c.ksTier, id, c.ksSheen, id, c.ksEffect, id, c.ksCustomColor ? 1 : 0,
        id, c.ksColor[0], c.ksColor[1], c.ksColor[2], id, c.unusual, id, c.australium ? 1 : 0,
        id, c.festivized ? 1 : 0, id, c.festive ? 1 : 0, id, c.statClock ? 1 : 0, id, c.statKills,
        id, (int)c.spellFlags, id, c.weaponSkinDef, id, c.reskinDef, id, c.reskinStyle);
    return (n > 0 && (size_t)n < cap) ? n : 0;
}

// "skin_<def>_" already stripped: field + value -> the struct. false = unknown field.
inline bool SkinPure_SetField(SkinCfg& c, const char* field, const char* val)
{
    const bool b = (atoi(val) != 0);
    if      (strcmp(field, "enabled")       == 0) c.enabled       = b;
    else if (strcmp(field, "paintKitId")    == 0) c.paintKitId    = atoi(val);
    else if (strcmp(field, "pattern")       == 0) c.pattern       = strtoull(val, nullptr, 10);
    else if (strcmp(field, "wear")          == 0) c.wear          = (float)atof(val);
    else if (strcmp(field, "ksTier")        == 0) c.ksTier        = atoi(val);
    else if (strcmp(field, "ksSheen")       == 0) c.ksSheen       = atoi(val);
    else if (strcmp(field, "ksEffect")      == 0) c.ksEffect      = atoi(val);
    else if (strcmp(field, "ksCustomColor") == 0) c.ksCustomColor = b;
    else if (strcmp(field, "ksColor")       == 0)
        sscanf(val, "%f %f %f", &c.ksColor[0], &c.ksColor[1], &c.ksColor[2]);
    else if (strcmp(field, "unusual")       == 0) c.unusual       = atoi(val);
    else if (strcmp(field, "australium")    == 0) c.australium    = b;
    else if (strcmp(field, "festivized")    == 0) c.festivized    = b;
    else if (strcmp(field, "festive")       == 0) c.festive       = b;
    else if (strcmp(field, "statClock")     == 0) c.statClock     = b;
    else if (strcmp(field, "statKills")     == 0) c.statKills     = atoi(val);
    else if (strcmp(field, "spellFlags")    == 0) c.spellFlags    = (uint8_t)atoi(val);
    else if (strcmp(field, "weaponSkinDef") == 0) c.weaponSkinDef = atoi(val);
    else if (strcmp(field, "reskinDef")     == 0) c.reskinDef     = atoi(val);
    else if (strcmp(field, "reskinStyle")   == 0) c.reskinStyle   = atoi(val);
    else return false;
    return true;
}

// Trust boundary: a peer's values land in the engine's attribute list. Clamp to what the
// menu itself can produce (data tables bound the rest at apply time).
inline void SkinPure_Sanitize(SkinCfg& c)
{
    auto cl = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    c.paintKitId    = cl(c.paintKitId, 0, 100000);
    c.wear          = c.wear < 0.f ? 0.f : (c.wear > 1.f ? 1.f : c.wear);
    c.ksTier        = cl(c.ksTier, 0, 3);
    c.ksSheen       = cl(c.ksSheen, 0, 7);
    c.ksEffect      = cl(c.ksEffect, 0, 7);
    for (float& f : c.ksColor) f = f < 0.f ? 0.f : (f > 1.f ? 1.f : f);
    c.unusual       = cl(c.unusual, -1, 10000);
    c.statKills     = cl(c.statKills, 0, 100000000);
    c.spellFlags   &= 0x7;
    c.weaponSkinDef = (c.weaponSkinDef >= 15000 && c.weaponSkinDef < 16000) ? c.weaponSkinDef : 0;
    c.reskinDef     = cl(c.reskinDef, 0, 65535);
    c.reskinStyle   = cl(c.reskinStyle, 0, 15);
}

// What a config edit costs to show. Skin_Apply re-stamps every attribute every frame, so a value
// the game reads per bind (sheen row, kill effect, stat clock count, spell flags) shows on the
// next frame by itself. Anything the client only reads when it BUILDS the weapon -- def index,
// paint composite, material override, particles, attachment models -- needs the weapon rebuilt,
// i.e. a cl_fullupdate. So does taking an attribute away: SetRuntimeAttributeValue can only add
// or overwrite, the list keeps the old value until the server resends it.
enum SkinChange : unsigned {
    SC_None    = 0,
    SC_Rebuild = 1u << 0,   // cl_fullupdate
    SC_Typed   = 1u << 1,   // a typed / dragged field: wait for the edit to settle first
    SC_Fest    = 1u << 2,   // festivizer switched on: one attachment rebuild, no full update
};

// The sheen row Skin_Apply writes (attr 2014): custom color with no sheen picked uses row 1.
inline int SkinPure_SheenIdx(const SkinCfg& c)
{
    if (c.ksTier < 2) return 0;
    if (c.ksSheen > 0) return c.ksSheen;
    return c.ksCustomColor ? 1 : 0;
}

inline unsigned SkinPure_Classify(const SkinCfg& was, const SkinCfg& now)
{
    if (was.enabled != now.enabled) return SC_Rebuild;
    if (!now.enabled) return SC_None;   // edits to a disabled weapon show nothing

    unsigned r = SC_None;
    // Read once, when the weapon is built.
    if (was.weaponSkinDef != now.weaponSkinDef || was.reskinDef != now.reskinDef
        || was.reskinStyle != now.reskinStyle  || was.festive    != now.festive
        || was.australium  != now.australium   || was.statClock  != now.statClock
        || was.unusual     != now.unusual)
        r |= SC_Rebuild;
    // Same, but typed or dragged: one per keystroke / drag frame would be a full update each.
    if (was.paintKitId != now.paintKitId || was.pattern != now.pattern || was.wear != now.wear)
        r |= SC_Rebuild | SC_Typed;

    // Killstreak: live while the attribute stays on, rebuild when it goes away.
    if (was.ksTier != now.ksTier && now.ksTier == 0) r |= SC_Rebuild;
    {
        const int s0 = SkinPure_SheenIdx(was), s1 = SkinPure_SheenIdx(now);
        if (s0 != s1 && s1 == 0) r |= SC_Rebuild;
    }
    {
        const int e0 = (was.ksTier == 3) ? was.ksEffect : 0;
        const int e1 = (now.ksTier == 3) ? now.ksEffect : 0;
        if (e0 != e1 && e1 == 0) r |= SC_Rebuild;
    }
    // A spell bit cleared.
    if (was.spellFlags & ~now.spellFlags) r |= SC_Rebuild;
    // Festivizer: on = the lights' own rebuild, off = the attribute has to come off the list.
    if (was.festivized != now.festivized) r |= now.festivized ? SC_Fest : SC_Rebuild;
    return r;
}
