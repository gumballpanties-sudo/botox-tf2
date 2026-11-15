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

enum class CfgAct { Run, Skip, Exec };

inline CfgAct Classify(const std::string& cmd, std::string* execTarget)
{
    const auto t = Tokens(cmd);
    if (t.empty()) return CfgAct::Skip;
    const std::string a = Lower(t[0]);
    if (a == "exec")
    {
        if (t.size() < 2) return CfgAct::Skip;
        if (execTarget) *execTarget = t[1];
        return CfgAct::Exec;
    }
    // boot / session commands, the user's own binds, and the reloads multihud does itself
    static const char* const kSkip[] = {
        "stuffcmds", "startupmenu", "sv_unlockedchapters", "showconsole", "hideconsole", "toggleconsole",
        "wait", "quit", "exit", "map", "map_background", "connect", "disconnect", "retry", "reconnect",
        "host_writeconfig", "bind", "unbind", "unbindall", "bindtoggle", "hud_reloadscheme",
        "snd_restart", "cl_reload_localization_files", "kill", "explode", "jointeam", "joinclass" };
    for (const char* s : kSkip) if (a == s) return CfgAct::Skip;
    if (a[0] == '+' || a[0] == '-') return CfgAct::Skip;   // +attack / -forward
    const std::string v = CvarName(cmd);
    // video settings (the "transparent panels" incrementvar mat_aaquality trick) and server cvars
    if (v.rfind("mat_", 0) == 0 || v.rfind("sv_", 0) == 0) return CfgAct::Skip;
    return CfgAct::Run;
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

// ---- search path plan -------------------------------------------------------------------------
// The game mounts every tf/custom/* folder at boot (gameinfo "game+mod+custom_mod"). A hud there
// would answer every file the picked hud lacks, so while a multihud pick is live the tf/custom huds
// are unmounted: what the pick lacks falls through to tf2_misc.vpk = the default files.
struct Mount
{
    std::string hud;                       // our mounted hud root, "" = none
    std::vector<std::string> customOff;    // tf/custom huds we unmounted (restore order = this order)
};

enum class Op { RemoveHud, RemoveCustom, AddCustom, AddHud };
struct FsOp { Op op; std::string path; };

// pick: "" = off (tf/custom as the game mounted it), kDefault = no hud at all, else hud root dir.
// Removes run before adds; every add goes to the HEAD of the search path, so the custom huds are
// re-added before ours (ours ends up above them) and in reverse, which keeps their old order.
inline std::vector<FsOp> PlanMount(const Mount& cur, const std::string& hudRoot, bool customOff,
                                   const std::vector<std::string>& customHuds)
{
    std::vector<FsOp> ops;
    if (!cur.hud.empty()) ops.push_back({ Op::RemoveHud, cur.hud });
    if (customOff && cur.customOff.empty())
        for (const auto& c : customHuds) ops.push_back({ Op::RemoveCustom, c });
    if (!customOff)
        for (auto it = cur.customOff.rbegin(); it != cur.customOff.rend(); ++it)
            ops.push_back({ Op::AddCustom, *it });
    if (!hudRoot.empty()) ops.push_back({ Op::AddHud, hudRoot });
    return ops;
}

// the dirs whose files changed meaning: every path the plan touched, once (case-insensitive).
inline std::vector<std::string> Touched(const std::vector<FsOp>& ops)
{
    std::vector<std::string> out;
    auto lower = [](std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; };
    for (const auto& o : ops)
    {
        const std::string l = lower(o.path);
        if (std::none_of(out.begin(), out.end(), [&](const std::string& x) { return lower(x) == l; }))
            out.push_back(o.path);
    }
    return out;
}

} // namespace mh
