#pragma once
// multihud: engine-free half (multihud.cpp glue, tests/multihud_test.cpp).
// A hud = a folder in C:\botox-tf2\huds. Switching = search path swap + the game's own reloads.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace mh {

// exists(path, wantDir): true when path is a file (wantDir false) / a directory (wantDir true)
using ExistsFn = std::function<bool(const std::string&, bool)>;

// every tf2 hud ships at least one of these. tf/custom crosshair / sound / model packs ship none.
inline bool IsHudDir(const std::string& d, const ExistsFn& ex)
{
    return ex(d + "\\scripts\\hudlayout.res", false) || ex(d + "\\resource\\clientscheme.res", false)
        || ex(d + "\\resource\\ui", true);
}

// a github zip unpacks one folder deep (huds\rayshud\rayshud-master\resource): look one level down.
// "" = not a hud.
inline std::string FindHudRoot(const std::string& d, const std::vector<std::string>& subdirs, const ExistsFn& ex)
{
    if (IsHudDir(d, ex)) return d;
    for (const auto& s : subdirs)
        if (IsHudDir(d + "\\" + s, ex)) return d + "\\" + s;
    return {};
}

// vgui2 CSchemeManager::LoadSchemeFromFileEx returns the scheme it already holds for a name
// (stricmp on the stored name, vgui2.dll e6d0), it never rereads the file. A fresh load needs a
// spelling it does not hold: same file, slashes flipped. "" = no slash to flip, leave it.
inline std::string AltSchemeName(const std::string& n)
{
    std::string a = n;
    bool any = false;
    for (char& c : a)
        if (c == '/')       { c = '\\'; any = true; }
        else if (c == '\\') { c = '/';  any = true; }
    return any ? a : std::string();
}

// "vgui\Replay\x.VTF" (relative to <hud>\materials) -> name "vgui/replay/x", ext "vtf".
// Material system names: lowercase, forward slash, no extension.
inline bool MaterialName(const std::string& rel, std::string& name, std::string& ext)
{
    const size_t dot = rel.find_last_of('.');
    const size_t sep = rel.find_last_of("\\/");
    if (dot == std::string::npos || (sep != std::string::npos && dot < sep) || dot == 0) return false;
    ext = rel.substr(dot + 1);
    for (char& c : ext) c = (char)tolower((unsigned char)c);
    if (ext != "vtf" && ext != "vmt") return false;
    name = rel.substr(0, dot);
    for (char& c : name) c = c == '\\' ? '/' : (char)tolower((unsigned char)c);
    return true;
}

// pick token for the stock hud: can not be a folder name ('<' is illegal on windows)
inline constexpr const char* kDefault = "<tf2 default>";

// menu -> pick: off = "" (tf/custom as the game mounted it); on with no folder = stock hud.
// name may still hold kDefault from an 11-15 cfg (the token was a cfg value then).
inline std::string Pick(bool on, const char* name)
{
    if (!on) return {};
    if (!name || !name[0] || !strcmp(name, kDefault)) return kDefault;
    return name;
}

// folder names only: no drive, no climbing out of huds\.
inline bool SafeName(const std::string& n)
{
    return !n.empty() && n.find(':') == std::string::npos && n.find("..") == std::string::npos
        && n.find('\\') == std::string::npos && n.find('/') == std::string::npos;
}

// ---- hud cfg (cfg\valve.rc, else cfg\autoexec.cfg) ----------------------------------------------
// A hud's valve.rc is the game's STARTUP script (it replaces tf2's own at boot): it re-runs the
// command line, opens the start menu, execs the user's autoexec. Mid game only the hud's own part
// may run: boot-only commands are skipped, exec follows into the hud's own cfg folder only.

inline std::string Lower(std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; }

// text -> commands: "//" comments cut, split on ';' and line ends, both only outside quotes
inline std::vector<std::string> SplitCommands(const std::string& text)
{
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    auto flush = [&]() {
        const size_t a = cur.find_first_not_of(" \t"), b = cur.find_last_not_of(" \t");
        if (a != std::string::npos) out.push_back(cur.substr(a, b - a + 1));
        cur.clear();
    };
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (c == '\n' || c == '\r') { quoted = false; flush(); continue; }
        if (c == '"') { quoted = !quoted; cur += c; continue; }
        if (!quoted && c == '/' && i + 1 < text.size() && text[i + 1] == '/')
        {
            while (i + 1 < text.size() && text[i + 1] != '\n' && text[i + 1] != '\r') ++i;
            continue;
        }
        if (!quoted && c == ';') { flush(); continue; }
        cur += c;
    }
    flush();
    return out;
}

// whitespace split, "quoted words" kept whole, quotes dropped
inline std::vector<std::string> Tokens(const std::string& cmd)
{
    std::vector<std::string> t;
    std::string cur;
    bool quoted = false, any = false;
    for (const char c : cmd)
    {
        if (c == '"') { quoted = !quoted; any = true; continue; }
        if (!quoted && (c == ' ' || c == '\t')) { if (any) t.push_back(cur); cur.clear(); any = false; continue; }
        cur += c;
        any = true;
    }
    if (any) t.push_back(cur);
    return t;
}

// the cvar a command writes ("toggle x", "incrementvar x ..." write x), lowercase; "" = none
inline std::string CvarName(const std::string& cmd)
{
    const auto t = Tokens(cmd);
    if (t.empty()) return {};
    const std::string a = Lower(t[0]);
    if (a == "incrementvar" || a == "multvar" || a == "toggle") return t.size() > 1 ? Lower(t[1]) : std::string();
    return a;
}

// ALLOWLIST (11-15: a blocklist ran femboy's ent_remove_all_entities.cfg = ~200 ent_remove_all on
// the listen server -> server.dll crash; its script.cfg con_logfile-writes into tf/cfg). A line runs
// only if it is:
//  - a client-side cvar set (or toggle / incrementvar / multvar of one); the caller decides "client
//    side" from the cvar's flags (not GAMEDLL / REPLICATED / CHEAT / USERINFO); never mat_* (video
//    settings), sv_*, or con_logfile (writes files)
//  - "alias name body" (Alias: caller records it), or a call of an alias this cfg defined (Expand:
//    caller runs the body through this same filter, never the alias by name)
//  - testhudanim / echo
//  - the hud "script" file writers (user's call 11-15): con_logfile (a client cvar, passes the cvar
//    rule) + sixense_write_bindings / sixense_clear_bindings. femboy / m0re write cfg/*.txt with
//    them, which their .res files #base to pick a layout (the caller links the paths, see
//    CustomNamesIn)
//  - exec (Exec: caller follows it into the hud's own cfg folder only)
// Everything else (ent_*, mp_*, bot commands, binds, boot commands, unknown) = Skip.
enum class CfgAct { Run, Skip, Exec, Alias, Expand };
using NameFn = std::function<bool(const std::string&)>;

inline bool CvarAllowedName(const std::string& lowerName)
{
    return !lowerName.empty() && lowerName.rfind("mat_", 0) != 0 && lowerName.rfind("sv_", 0) != 0
        && lowerName != "vgui_cache_res_files";
}

// A script hud's .res does #base "../../../../cfg/x.txt" = <mount root>/../../cfg (tf/cfg when the
// hud sits in tf/custom/<hud>), and the cfg/x.txt its switch cfgs log says
// #base "../custom/<hud>/resource/..." = back down through tf/custom. The folder names a hud expects
// there: every "../custom/<name>/" in its cfg files (lowercase, once). Empty = not a script hud.
inline std::vector<std::string> CustomNamesIn(const std::string& text)
{
    std::vector<std::string> out;
    const std::string lower = Lower(text);
    const std::string key = "../custom/";
    for (size_t at = lower.find(key); at != std::string::npos; at = lower.find(key, at + key.size()))
    {
        const size_t b = at + key.size();
        const size_t e = lower.find_first_of("/\\\"", b);
        if (e == std::string::npos || e == b || lower[e] == '"') continue;
        const std::string name = lower.substr(b, e - b);
        if (name == "." || name == ".." || name.find(':') != std::string::npos) continue;
        if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
    }
    return out;
}

inline CfgAct Classify(const std::string& cmd, std::string* target, const NameFn& clientCvar, const NameFn& knownAlias)
{
    const auto t = Tokens(cmd);
    if (t.empty()) return CfgAct::Skip;
    const std::string a = Lower(t[0]);
    if (a == "exec")
    {
        if (t.size() < 2) return CfgAct::Skip;
        if (target) *target = t[1];
        return CfgAct::Exec;
    }
    if (a == "alias")
    {
        if (t.size() < 2) return CfgAct::Skip;
        if (target) *target = Lower(t[1]);
        return CfgAct::Alias;
    }
    if (a == "testhudanim" || a == "echo" || a == "sixense_write_bindings" || a == "sixense_clear_bindings")
        return CfgAct::Run;
    if (knownAlias && knownAlias(a)) { if (target) *target = a; return CfgAct::Expand; }
    const std::string v = CvarName(cmd);
    if (!CvarAllowedName(v)) return CfgAct::Skip;
    return clientCvar && clientCvar(v) ? CfgAct::Run : CfgAct::Skip;
}

// "alias name body" -> body (quoted or the rest of the line), "" = none
inline std::string AliasBody(const std::string& cmd)
{
    const auto t = Tokens(cmd);
    if (t.size() < 3) return {};
    // a quoted body is one token; an unquoted one may be several words
    const size_t at = cmd.find(t[1]) + t[1].size();
    std::string rest = cmd.substr(at);
    const size_t a = rest.find_first_not_of(" \t\"");
    const size_t b = rest.find_last_not_of(" \t\"");
    return a == std::string::npos ? std::string() : rest.substr(a, b - a + 1);
}

// ---- bitmap font file (.vbf) ------------------------------------------------------------------
// vguimatsurface CBitmapFont::Create copies a 0x116-byte header ("VFNT", version 3, glyph count
// short at +0x14) then count * 14 bytes of glyphs. Glyph count the in-place refresh may copy, or -1:
// bad file, or more glyphs than the block the game allocated for the first copy (loadedCount).
inline constexpr size_t kVbfHeader = 0x116, kVbfGlyph = 14;
inline int VbfGlyphs(const unsigned char* p, size_t n, int loadedCount)
{
    if (!p || n < kVbfHeader) return -1;
    uint32_t magic; int version; short count;
    memcpy(&magic, p, 4); memcpy(&version, p + 4, 4); memcpy(&count, p + 0x14, 2);
    if (magic != 0x544e4656u || version != 3 || count <= 0) return -1;
    if (n < kVbfHeader + (size_t)count * kVbfGlyph || count > loadedCount) return -1;
    return count;
}

// ---- paths ------------------------------------------------------------------------------------
// lowercase, forward slashes, "." and "x/.." collapsed: vgui binds hud images as "vgui/../hud/x",
// the files on disk are materials/hud/x.
inline std::string NormalizePath(const std::string& in)
{
    std::vector<std::string> parts;
    std::string cur;
    auto push = [&]() {
        if (cur.empty() || cur == ".") {}
        else if (cur == ".." && !parts.empty() && parts.back() != "..") parts.pop_back();
        else parts.push_back(cur);
        cur.clear();
    };
    for (const char ch : in)
    {
        const char c = ch == '\\' ? '/' : (char)tolower((unsigned char)ch);
        if (c == '/') push(); else cur += c;
    }
    push();
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) { if (i) out += '/'; out += parts[i]; }
    return out;
}

// ---- .res control tree --------------------------------------------------------------------------
// Names-only view of a vgui .res: every block key (controls, nested controls, if_ blocks), leaves
// (properties) dropped, lowercase. "#base"/"#include" file names collected for the caller to load
// (relative to the including file) and merge: KeyValues merges a base file's keys in by name.
// geometry keys Panel::ApplySettings applies only when present (Panel.cpp:4410 xpos/ypos keep GetPos,
// ComputeWide/Tall keep the size, 4519 zpos): a key the new hud leaves out kept the old hud's value
enum : unsigned { kGeoX = 1, kGeoY = 2, kGeoZ = 4, kGeoW = 8, kGeoT = 16 };
inline unsigned GeoBit(const std::string& lowerKey)
{
    return lowerKey == "xpos" ? kGeoX : lowerKey == "ypos" ? kGeoY : lowerKey == "zpos" ? kGeoZ
         : lowerKey == "wide" ? kGeoW : lowerKey == "tall" ? kGeoT : 0;
}

struct ResNode
{
    std::string name;
    std::vector<ResNode> kids;
    unsigned geo = 0;   // kGeo* keys the block sets
};

inline const ResNode* ResChild(const ResNode& n, const std::string& lowerName)
{
    for (const auto& k : n.kids) if (k.name == lowerName) return &k;
    return nullptr;
}

// union by name, recursive (a key both sides have keeps one node with both sides' children)
inline void ResMerge(ResNode& into, const ResNode& from)
{
    for (const auto& k : from.kids)
    {
        ResNode* hit = nullptr;
        for (auto& m : into.kids) if (m.name == k.name) { hit = &m; break; }
        if (hit) { hit->geo |= k.geo; ResMerge(*hit, k); } else into.kids.push_back(k);
    }
}

namespace detail {
struct ResLexer
{
    const std::string& s;
    size_t i = 0;
    // next token; "" at end. Quotes stripped, '{' '}' are tokens, // comments skipped
    bool Next(std::string& tok, bool& quoted)
    {
        tok.clear(); quoted = false;
        for (;;)
        {
            while (i < s.size() && (unsigned char)s[i] <= ' ') ++i;
            if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') { while (i < s.size() && s[i] != '\n') ++i; continue; }
            break;
        }
        if (i >= s.size()) return false;
        const char c = s[i];
        if (c == '{' || c == '}') { tok = c; ++i; return true; }
        if (c == '"')
        {
            ++i; quoted = true;
            while (i < s.size() && s[i] != '"') tok += s[i++];
            if (i < s.size()) ++i;
            return true;
        }
        while (i < s.size() && (unsigned char)s[i] > ' ' && s[i] != '{' && s[i] != '}' && s[i] != '"') tok += s[i++];
        return true;
    }
};
inline bool IsCond(const std::string& t, bool quoted) { return !quoted && t.size() >= 2 && t.front() == '[' && t.back() == ']'; }

// geo = the owner block's kGeo* bits (null at the file level)
inline void ParseBlock(ResLexer& lx, std::vector<ResNode>& out, std::vector<std::string>* includes, int depth,
                       unsigned* geo = nullptr)
{
    std::string tok; bool q;
    while (lx.Next(tok, q))
    {
        if (!q && tok == "}") return;
        if (!q && tok == "{") { std::vector<ResNode> junk; if (depth < 64) ParseBlock(lx, junk, nullptr, depth + 1); continue; }
        if (IsCond(tok, q)) continue;
        if (!q && (tok == "#base" || tok == "#include"))
        {
            std::string f; bool fq;
            if (lx.Next(f, fq) && includes) includes->push_back(f);
            continue;
        }
        ResNode node; node.name = Lower(tok);
        // value or block; a conditional may sit between the key and either
        std::string v; bool vq;
        size_t save;
        do { save = lx.i; if (!lx.Next(v, vq)) return; } while (IsCond(v, vq));
        if (!vq && v == "{")
        {
            if (depth < 64) ParseBlock(lx, node.kids, nullptr, depth + 1, &node.geo);
            out.push_back(std::move(node));
        }
        else if (!vq && v == "}") { lx.i = save; }   // key with no value right before a close: tolerate
        else if (geo) *geo |= GeoBit(node.name);
        // else: a property, dropped
    }
}
} // namespace detail

// The .res root is one block named after the file; the controls are its children. Returns a node
// whose kids are the controls (every top-level block's children, merged).
inline ResNode ParseRes(const std::string& text, std::vector<std::string>* includes)
{
    detail::ResLexer lx{ text };
    std::vector<ResNode> top;
    detail::ParseBlock(lx, top, includes, 0);
    ResNode root;
    for (const auto& t : top) ResMerge(root, t);
    return root;
}

// "#base" target relative to the including file's folder
inline std::string ResIncludePath(const std::string& includingFile, const std::string& inc)
{
    const std::string f = NormalizePath(includingFile);
    const size_t sl = f.find_last_of('/');
    return NormalizePath((sl == std::string::npos ? std::string() : f.substr(0, sl + 1)) + inc);
}

// .res / .txt bytes -> text. utf-16 le (FF FE): the low bytes are enough for names and paths.
inline std::string ResText(const std::string& b)
{
    if (b.size() < 2 || (unsigned char)b[0] != 0xFF || (unsigned char)b[1] != 0xFE) return b;
    std::string t;
    for (size_t i = 2; i + 1 < b.size(); i += 2) t += b[i];
    return t;
}

// folders a normalized path climbs above its root ("../../cfg/x" = 2), rest = what follows the climb
inline int Climb(const std::string& norm, std::string* rest)
{
    int up = 0;
    size_t i = 0;
    while (norm.compare(i, 3, "../") == 0) { ++up; i += 3; }
    if (rest) *rest = norm.substr(i);
    return up;
}

// Script huds #base a cfg/<x>.txt their switch cfgs write into tf/cfg, from any depth: m0re's
// resource/ui/x.res says "../../../../cfg" (2 above the root = tf from tf/custom/m0re), femboy's
// scripts/layouts/layout_killfeed.res says "../../../cfg" (1 above). KeyValues joins it onto the file's
// folder as text, so it lands <root> + n levels up + "cfg". Adds each n this file uses to out (once).
inline void AddCfgClimbs(const std::string& fileRel, const std::string& text, std::vector<int>& out)
{
    std::vector<std::string> inc;
    ParseRes(text, &inc);
    for (const auto& i : inc)
    {
        std::string rest;
        const int n = Climb(ResIncludePath(fileRel, i), &rest);
        if (n > 0 && rest.rfind("cfg/", 0) == 0 && std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
    }
}

// ---- search path plan -------------------------------------------------------------------------
// The game mounts every tf/custom/* folder at boot (gameinfo "game+mod+custom_mod"). A hud there
// would answer every file the picked hud lacks, so while a multihud pick is live the tf/custom huds
// are unmounted: what the pick lacks falls through to tf2_misc.vpk = the default files.
struct Mount
{
    std::string hud;                       // our mounted hud root, "" = none
    std::vector<std::string> customOff;    // tf/custom huds we unmounted (restore order = this order)
};

enum class Op { RemoveHud, RemoveCustom, AddCustom, AddHud, RaiseMod };
struct FsOp { Op op; std::string path; };

// pick: "" = off (tf/custom as the game mounted it), kDefault = no hud at all, else hud root dir.
// Removes run before adds; every add goes to the HEAD of the search path, so the custom huds are
// re-added before ours (ours ends up above them) and in reverse, which keeps their old order.
// mods = the tf/custom FOLDERS that are not huds (models, animations, sounds, crosshair packs),
// alphabetical. After any hud add they are raised (remove + HEAD add, reverse = alphabetical on
// top) so a file both ship stays the mod's: femboy ships c_spy_animations.mdl, the user's
// tf/custom/ani has its own (at boot "ani" < "femboy" wins the same way).
inline std::vector<FsOp> PlanMount(const Mount& cur, const std::string& hudRoot, bool customOff,
                                   const std::vector<std::string>& customHuds,
                                   const std::vector<std::string>& mods = {})
{
    std::vector<FsOp> ops;
    if (!cur.hud.empty()) ops.push_back({ Op::RemoveHud, cur.hud });
    if (customOff && cur.customOff.empty())
        for (const auto& c : customHuds) ops.push_back({ Op::RemoveCustom, c });
    if (!customOff)
        for (auto it = cur.customOff.rbegin(); it != cur.customOff.rend(); ++it)
            ops.push_back({ Op::AddCustom, *it });
    if (!hudRoot.empty()) ops.push_back({ Op::AddHud, hudRoot });
    const bool added = std::any_of(ops.begin(), ops.end(), [](const FsOp& o) { return o.op == Op::AddHud || o.op == Op::AddCustom; });
    if (added)
        for (auto it = mods.rbegin(); it != mods.rend(); ++it) ops.push_back({ Op::RaiseMod, *it });
    return ops;
}

// the dirs whose files changed meaning: every hud path the plan touched, once (case-insensitive).
// Raised mods are not: their files answer exactly as before.
inline std::vector<std::string> Touched(const std::vector<FsOp>& ops)
{
    std::vector<std::string> out;
    auto lower = [](std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; };
    for (const auto& o : ops)
    {
        if (o.op == Op::RaiseMod) continue;
        const std::string l = lower(o.path);
        if (std::none_of(out.begin(), out.end(), [&](const std::string& x) { return lower(x) == l; }))
            out.push_back(o.path);
    }
    return out;
}

// ---- vgui texture ids (multihud.cpp RepairVguiIds) ------------------------------------------------
// tier1 CRC32 (checksum_crc.cpp): reflected 0xEDB88320, init + final xor 0xFFFFFFFF (= zlib crc32).
// vguimatsurface keys a texture id on the CRC of the exact name it was bound with, case kept
// (CTextureDictionary::BindTextureToFile, x64 11-15), and keeps that CRC when the bind found no file.
inline uint32_t Crc32(const char* s, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i)
    {
        c ^= (unsigned char)s[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c ^ 0xFFFFFFFFu;
}
inline uint32_t Crc32(const std::string& s) { return Crc32(s.data(), s.size()); }

// every "quoted" value on one line of a .res / .txt, as written (image names, hud_textures files)
inline std::vector<std::string> QuotedTokens(const std::string& text)
{
    std::vector<std::string> out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != '"') continue;
        const size_t e = text.find('"', i + 1);
        if (e == std::string::npos) break;
        const size_t nl = text.find('\n', i + 1);
        if (nl < e) { i = nl; continue; }   // unclosed on its line: not a value
        if (e > i + 1 && e - i - 1 < 260) out.push_back(text.substr(i + 1, e - i - 1));
        i = e;
    }
    return out;
}

// ---- hud element remake (multihud.cpp RemakeHudElements) -----------------------------------------
// old object address -> new one, 0 = no counterpart (the pointer gets nulled once the old one is gone)
struct PtrFix { uint64_t from = 0, to = 0; };
inline void SortFixes(std::vector<PtrFix>& v)
{
    std::stable_sort(v.begin(), v.end(), [](const PtrFix& a, const PtrFix& b) { return a.from < b.from; });
    v.erase(std::unique(v.begin(), v.end(), [](const PtrFix& a, const PtrFix& b) { return a.from == b.from; }), v.end());
}
inline const PtrFix* FindFix(const std::vector<PtrFix>& v, uint64_t p)
{
    auto it = std::lower_bound(v.begin(), v.end(), p, [](const PtrFix& f, uint64_t x) { return f.from < x; });
    return it != v.end() && it->from == p ? &*it : nullptr;
}
// words = a copy of a module's writable data; indices of words holding an old pointer, split by
// whether the new hud has a counterpart
struct FixHits { std::vector<size_t> mapped, unmapped; };
inline FixHits ScanWords(const uint64_t* w, size_t n, const std::vector<PtrFix>& fixes)
{
    FixHits h;
    if (fixes.empty()) return h;
    const uint64_t lo = fixes.front().from, hi = fixes.back().from;
    for (size_t i = 0; i < n; ++i)
    {
        if (w[i] < lo || w[i] > hi) continue;
        if (const PtrFix* f = FindFix(fixes, w[i])) (f->to ? h.mapped : h.unmapped).push_back(i);
    }
    return h;
}
// a panel object: its path under the hud element ("CHudChat/HudChatHistory") + class (vtable)
struct PanelNode { uint64_t obj = 0; std::string path; uint64_t cls = 0; };
// old -> new where (path, class) names exactly one object on each side; the rest stay unmatched
inline std::vector<std::pair<uint64_t, uint64_t>> MatchByPath(const std::vector<PanelNode>& olds,
                                                              const std::vector<PanelNode>& news)
{
    auto key = [](const PanelNode& p) { return Lower(p.path) + '\x01' + std::to_string(p.cls); };
    auto byKey = [&](const std::vector<PanelNode>& v) {
        std::vector<std::pair<std::string, size_t>> k;
        for (size_t i = 0; i < v.size(); ++i) k.emplace_back(key(v[i]), i);
        std::sort(k.begin(), k.end());
        return k;
    };
    const auto ko = byKey(olds), kn = byKey(news);
    auto unique = [](const std::vector<std::pair<std::string, size_t>>& k, size_t i) {
        return (i == 0 || k[i - 1].first != k[i].first) && (i + 1 == k.size() || k[i + 1].first != k[i].first);
    };
    std::vector<std::pair<uint64_t, uint64_t>> out;
    size_t j = 0;
    for (size_t i = 0; i < ko.size(); ++i)
    {
        if (!unique(ko, i)) continue;
        while (j < kn.size() && kn[j].first < ko[i].first) ++j;
        if (j < kn.size() && kn[j].first == ko[i].first && unique(kn, j))
            out.emplace_back(olds[ko[i].second].obj, news[kn[j].second].obj);
    }
    return out;
}

} // namespace mh
