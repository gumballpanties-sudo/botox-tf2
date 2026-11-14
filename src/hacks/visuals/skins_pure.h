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

// Bounded copy into a name tag buffer; null = "". Cuts at a byte, never past the 0.
inline void SkinPure_CopyName(char (&out)[kNameTagLen], const char* in)
{
    int i = 0;
    if (in) for (; i + 1 < kNameTagLen && in[i]; ++i) out[i] = in[i];
    out[i] = 0;
}

// "skin_<def>_" already stripped: field + value -> the struct. false = unknown field.
// "name" is read here but never written by SkinPure_Format: Skins_SaveCfg adds it on its own
// line, so the p2p wire and SkinPure_Same (backpack copy rebuild) never see it.
inline bool SkinPure_SetField(SkinCfg& c, const char* field, const char* val)
{
    const bool b = (atoi(val) != 0);
    if      (strcmp(field, "name")          == 0) SkinPure_CopyName(c.nameTag, val);
    // "enabled" opens every block (SkinPure_Format) and a name line only follows when there is
    // one: clear it here, or a loaded cfg with no name for this weapon keeps the old one.
    else if (strcmp(field, "enabled")       == 0) { c.enabled = b; c.nameTag[0] = 0; }
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

// Quality of the backpack copy (econ_item_constants.h: 5 unusual, 6 unique, 11 strange, 15
// decorated weapon). TF2's own rules: every australium is strange, a stat clock makes a strange,
// unusual outranks both (the name then reads "Strange Unusual", econ_item_description.cpp:809),
// a painted weapon with none of those is a Decorated Weapon.
inline int SkinPure_InvQuality(const SkinCfg& c)
{
    if (c.unusual >= 0)                          return 5;
    if (c.statClock || c.australium)             return 11;
    if (c.paintKitId > 0 || c.weaponSkinDef > 0) return 15;
    return 6;
}

// Quality a REAL item shows once our config is laid over it: it keeps its own (a strange stays
// strange, a genuine stays genuine) unless ours outranks a plain one. Unusual over a strange
// with kills reads "Strange Unusual".
inline int SkinPure_MergeQuality(int real, int ours)
{
    if (ours == 5) return 5;
    if (ours == 11 && (real == 6 || real == 15)) return 11;
    if (ours == 15 && real == 6) return 15;
    return real;
}

// ---- patching a real item's attribute rows -------------------------------------------------
// CEconItemCustomData::m_vecAttributes rows: 16 bytes, def u16 @0, value @8 (float or int bits
// for every def we write; pointer-valued rows are the item's own and are never touched).
struct AttrRows { uint8_t* rows; int* count; int cap; };
inline int      Rows_Find(const AttrRows& a, uint16_t def)
{
    for (int i = 0; i < *a.count; ++i) if (*reinterpret_cast<uint16_t*>(a.rows + i * 16) == def) return i;
    return -1;
}
inline uint32_t Rows_Get(const AttrRows& a, int i)             { return *reinterpret_cast<uint32_t*>(a.rows + i * 16 + 8); }
inline void     Rows_Set(const AttrRows& a, int i, uint32_t v) { *reinterpret_cast<uint32_t*>(a.rows + i * 16 + 8) = v; }

// One row we own or overrode. had = the item had this def before us (saved = its value);
// wrote = what we last put there. A value that is no longer `wrote` was written by the GC
// (CEconItem::operator= overwrites per def), and the GC's value wins from then on.
struct PatchRow   { uint16_t def; bool had; uint32_t saved, wrote; };
struct PatchState { PatchRow r[32]; int n = 0; };

// Undo one row. Returns true if the rows changed.
inline bool SkinPure_PatchUndoRow(const AttrRows& a, const PatchRow& t)
{
    const int i = Rows_Find(a, t.def);
    if (i < 0 || Rows_Get(a, i) != t.wrote) return false;   // gone, or the GC's now: leave it
    if (t.had) { Rows_Set(a, i, t.saved); return t.saved != t.wrote; }
    memmove(a.rows + i * 16, a.rows + (i + 1) * 16, (size_t)(*a.count - i - 1) * 16);
    --*a.count;
    return true;
}

// Rows the item still lacks out of `want` (what Apply would append).
inline int SkinPure_PatchNeed(const AttrRows& a, const uint16_t* defs, int nWant)
{
    int need = 0;
    for (int k = 0; k < nWant; ++k) if (Rows_Find(a, defs[k]) < 0) ++need;
    return need;
}

// Lay `want` over the item's rows: overwrite or append, undo rows we no longer want. The caller
// guarantees room (cap >= count + SkinPure_PatchNeed). Returns rows changed.
inline int SkinPure_PatchApply(const AttrRows& a, PatchState& st, const uint16_t* defs, const uint32_t* vals, int nWant)
{
    int changed = 0;
    for (int j = 0; j < st.n; ) {   // dropped from the config: give the row back
        bool keep = false;
        for (int k = 0; k < nWant && !keep; ++k) keep = (defs[k] == st.r[j].def);
        if (keep) { ++j; continue; }
        changed += SkinPure_PatchUndoRow(a, st.r[j]) ? 1 : 0;
        st.r[j] = st.r[--st.n];
    }
    for (int k = 0; k < nWant; ++k) {
        PatchRow* t = nullptr;
        for (int j = 0; j < st.n; ++j) if (st.r[j].def == defs[k]) t = &st.r[j];
        int i = Rows_Find(a, defs[k]);
        if (!t) {
            if (st.n >= 32) continue;
            t = &st.r[st.n++];
            *t = { defs[k], i >= 0, i >= 0 ? Rows_Get(a, i) : 0u, 0u };
        } else if (i >= 0 && Rows_Get(a, i) != t->wrote) {
            t->had = true; t->saved = Rows_Get(a, i);         // the GC wrote it since
        }
        if (i < 0) {
            if (*a.count >= a.cap) continue;
            i = (*a.count)++;
            memset(a.rows + i * 16, 0, 16);
            *reinterpret_cast<uint16_t*>(a.rows + i * 16) = defs[k];
            t->had = false;   // row gone (data rebuilt): it is ours again
            ++changed;
        } else if (Rows_Get(a, i) != vals[k]) ++changed;
        Rows_Set(a, i, vals[k]);
        t->wrote = vals[k];
    }
    return changed;
}

// Undo every row. Returns rows changed.
inline int SkinPure_PatchRestore(const AttrRows& a, PatchState& st)
{
    int changed = 0;
    for (int j = 0; j < st.n; ++j) changed += SkinPure_PatchUndoRow(a, st.r[j]) ? 1 : 0;
    st.n = 0;
    return changed;
}

// ---- name tags: item id -> name (hats_inv.cpp, CEconItem::GetCustomName hook) ----------------
// Set: "" removes the row. true = the name that id shows changed, so the caller re-announces
// the item (SOUpdated) and its panels rebuild the name line. Full table = no tag (real name).
template <int N>
struct NameTagTable {
    struct Row { uint64_t id; char name[kNameTagLen]; };
    Row rows[N] = {};
    int n = 0;

    const char* Find(uint64_t id) const
    {
        for (int i = 0; i < n; ++i) if (rows[i].id == id) return rows[i].name;
        return nullptr;
    }
    bool Set(uint64_t id, const char* name)
    {
        char v[kNameTagLen];
        SkinPure_CopyName(v, name);
        int i = 0;
        while (i < n && rows[i].id != id) ++i;
        if (!v[0]) {
            if (i == n) return false;
            rows[i] = rows[--n];
            return true;
        }
        if (i < n) {
            if (strcmp(rows[i].name, v) == 0) return false;
            memcpy(rows[i].name, v, sizeof(v));
            return true;
        }
        if (n == N) return false;
        rows[n].id = id;
        memcpy(rows[n].name, v, sizeof(v));
        ++n;
        return true;
    }
};

// Every field equal, live ones included (the backpack copy's name and description show them all).
// Not the name tag: it is no attribute, a rename only re-announces the copy (SkinsInv_Tick).
inline bool SkinPure_Same(const SkinCfg& a, const SkinCfg& b)
{
    char x[1024], y[1024];
    return SkinPure_Format(x, sizeof(x), 1, a) && SkinPure_Format(y, sizeof(y), 1, b) && strcmp(x, y) == 0;
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
