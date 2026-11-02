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
