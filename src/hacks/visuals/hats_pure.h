#pragma once
// Hat changer pieces with no engine in them -- tests/hats_test.cpp runs these against the real
// generated table (src/dependencies/data/hatdata.h, tools/gen_hatdata.py).
#include <cstdint>
#include <cstring>
#include <cctype>

struct HatStyle { const char* name; const char* mdl; const char* hide; };
struct HatDef   { int def; const char* name; uint16_t classMask; bool paintable; int nStyles; const HatStyle* styles; };
struct HatPaint { const char* name; int rgb; int rgb2; };

// TF class index -> the schema's class usability string (tf_item_schema.cpp:505).
inline const char* Hat_ClassName(int cls)
{
    static const char* const k[10] = { "", "scout", "sniper", "soldier", "demoman",
                                       "medic", "heavy", "pyro", "spy", "engineer" };
    return (cls >= 1 && cls <= 9) ? k[cls] : "";
}

// HatStyle::mdl -> the model path for one class. Three forms, see gen_hatdata.py encode():
//   "a|b|...|i"  nine paths scout..engineer, empty = that class has none
//   "...%s..."   basename, every %s = class name, "demo" for demoman (tf_item_schema.cpp:526)
//   else         one path for every class
inline bool Hat_ModelFor(const char* mdl, int cls, char* out, size_t n)
{
    if (!mdl || !out || n < 2 || cls < 1 || cls > 9) return false;
    out[0] = 0;
    if (strchr(mdl, '|')) {
        const char* p = mdl;
        for (int i = 1; i < cls; ++i) {
            p = strchr(p, '|');
            if (!p) return false;
            ++p;
        }
        const char* e = strchr(p, '|');
        const size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == 0 || len >= n) return false;
        memcpy(out, p, len); out[len] = 0;
        return true;
    }
    const char* nm = (cls == 4) ? "demo" : Hat_ClassName(cls);
    const size_t nl = strlen(nm);
    size_t o = 0;
    for (const char* p = mdl; *p; ) {
        if (p[0] == '%' && p[1] == 's') {
            if (o + nl >= n) return false;
            memcpy(out + o, nm, nl); o += nl; p += 2;
        } else {
            if (o + 1 >= n) return false;
            out[o++] = *p++;
        }
    }
    out[o] = 0;
    return o > 0;
}

// studio.cpp SetBodygroup on a raw body int: the group's digit in the mixed-radix body number.
inline int Hat_SetBodygroup(int body, int base, int numModels, int value)
{
    if (base < 1 || numModels < 1 || value < 0 || value >= numModels) return body;
    const int cur = (body / base) % numModels;
    return body - cur * base + value * base;
}

// Is `name` one of the comma separated entries of `csv` (case-insensitive, whole entry)?
inline bool Hat_ListHas(const char* csv, const char* name)
{
    if (!csv || !name || !name[0]) return false;
    const size_t nl = strlen(name);
    for (const char* p = csv; *p; ) {
        const char* e = strchr(p, ',');
        const size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == nl && _strnicmp(p, name, nl) == 0) return true;
        if (!e) break;
        p = e + 1;
    }
    return false;
}

// Case-insensitive substring, empty needle matches all (menu search).
inline bool Hat_NameMatch(const char* hay, const char* needle)
{
    if (!needle || !needle[0]) return true;
    if (!hay) return false;
    for (const char* h = hay; *h; ++h) {
        const char* a = h; const char* b = needle;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { ++a; ++b; }
        if (!*b) return true;
    }
    return false;
}

// TF2 loadout screen effect messages, as they leave for the GC (hats_inv.cpp GC hook):
// [u32 emsg | 0x80000000][u32 header len][CMsgProtoBufHeader][body], body = { uint64 item_id = 1;
// float offset = 2 } (1087 k_EMsgGCSetItemEffectVerticalOffset) or { uint64 item_id = 1;
// bool use_head = 2 } (1088 k_EMsgGCSetHatEffectUseHeadOrigin). false = not one of those / malformed.
struct HatGcMsg { uint32_t type; uint64_t id; bool useHead; float offset; };

inline bool Hat_Varint(const uint8_t*& p, const uint8_t* end, uint64_t& v)
{
    v = 0;
    for (int sh = 0; p < end && sh < 64; sh += 7) {
        const uint8_t b = *p++;
        v |= uint64_t(b & 0x7F) << sh;
        if (!(b & 0x80)) return true;
    }
    return false;
}

inline bool Hat_ParseGcEffect(uint32_t type, const void* data, uint32_t size, HatGcMsg& m)
{
    const uint32_t t = type & 0x7FFFFFFFu;
    if ((t != 1087 && t != 1088) || !data || size < 8) return false;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t emsg, hl;
    memcpy(&emsg, p, 4); memcpy(&hl, p + 4, 4);
    if ((emsg & 0x7FFFFFFFu) != t || hl > size - 8) return false;
    const uint8_t* q = p + 8 + hl, *end = p + size;
    m = { t, 0, false, 0.f };
    bool have = false;
    while (q < end) {
        uint64_t tag, v;
        if (!Hat_Varint(q, end, tag)) return false;
        const int field = (int)(tag >> 3), wt = (int)(tag & 7);
        if (wt == 0) {
            if (!Hat_Varint(q, end, v)) return false;
            if (field == 1) m.id = v;
            else if (field == 2 && t == 1088) { m.useHead = v != 0; have = true; }
        } else if (wt == 5) {
            if (end - q < 4) return false;
            if (field == 2 && t == 1087) { memcpy(&m.offset, q, 4); have = true; }
            q += 4;
        } else if (wt == 1) {
            if (end - q < 8) return false;
            if (field == 1) memcpy(&m.id, q, 8);
            q += 8;
        } else if (wt == 2) {
            if (!Hat_Varint(q, end, v) || v > uint64_t(end - q)) return false;
            q += v;
        } else return false;
    }
    return have && m.id != 0;
}
