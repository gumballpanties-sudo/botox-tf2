#pragma once
// Skinchanger pieces with no engine in them -- tests/skins_test.cpp runs these.
#include "../../globals/types.h"   // SkinCfg

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
