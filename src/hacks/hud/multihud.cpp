#include "multihud.h"
#include "multihud_pure.h"
#include "../../render/render.h"   // VGui_ResetPanelCache
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

// multihud: pick a hud folder, the game swaps to it live. Every step is the game's own reload path:
//  1. search paths: our hud at the HEAD of GAME / mod / custom_mod (gameinfo's tf/custom/* ids:
//     BuildGroup::LoadControlSettings reads .res from custom_mod then vgui = tf2_misc.vpk,
//     BuildGroup.cpp:923), tf/custom huds unmounted, so what the pick lacks = the default file.
//  2. vgui_cache_res_files 0 while our mounts are live: the .res cache is a static of vgui_controls,
//     so client, GameUI, ServerBrowser, engine and vguimatsurface each hold one (BuildGroup.cpp:61),
//     and only client's is ever cleared (hud_reloadscheme). The cvar is shared by all of them.
//  3. hud_reloadscheme: KeyValues cache + client res cache flush (clientmode_shared.cpp:311).
//  4. schemes: vgui2 never rereads a loaded scheme (LoadSchemeFromFileEx returns the one it has,
//     ReloadSchemes deletes borders under live panels), so each scheme file the hud touches is
//     loaded again under a second spelling and the two CScheme bodies are swapped: same handle,
//     new fonts / colors / borders, the old body kept alive (nothing a panel holds dangles).
//  5. materials (loaded .vtf / .vmt the old or new hud ships, reread), string files, sounds.
//  6. the hud's own cfg (valve.rc / autoexec.cfg) through a boot-only filter; every cvar it sets is
//     snapshotted first and put back when the hud is left.
//  7. hud_reloadscheme again (viewport + every hud element reapply, new scheme + cvars), then
//     "OnScreenSizeChanged" posted to every OTHER branch of the vgui tree (GameUI, main menu, the
//     matchmaking dashboard, popups). Not ISurface::OnScreenSizeChanged: that also reaches the
//     viewport, which deletes + remakes its panels (baseviewport.cpp:204), and the main menu dies
//     with them. TF2 parents the dashboard (top bar, mode selection) to the main menu
//     (tf_matchmaking_dashboard_parent_manager.cpp:206), so it was orphaned before its message
//     arrived and kept the old hud's look. Kept as the fallback when there is no KeyValues (aim off).
//
// Slots, read off the game's x64 dlls 11-15 (RTTI vtables + disasm):
//  filesystem_stdio CFileSystem_Stdio  7 AddSearchPath(path, id, add)  8 RemoveSearchPath(path, id)
//  engine CEngineClient              102 ExecuteClientCmd = Cbuf_AddText + Cbuf_Execute (runs now)
//  vgui2 CSchemeManager                8 GetIScheme(h)  12 LoadSchemeFromFileEx(sizing, file, tag)
//                                      m_Schemes CUtlVector<CScheme*>: count +0x18, handle = index
//  vgui2 CScheme (ctor c610, new 0x208) name[256] +8, tag[64] +0x108, +0x148..0x208 = data, border
//                                      list, fonts, colors, sizing panel: heap pointers + ints only
//  IScheme                            12 GetSizingPanel (the Ex compare, vgui2 e760)
//  vguimatsurface CMatSystemSurface   38 GetScreenSize  111 OnScreenSizeChanged (ResetFontCaches,
//                                      posts OnScreenSizeChanged to the root, IVGui::RunFrame)
//  materialsystem CMaterialSystem     71 FindMaterial 72 IsMaterialLoaded 79 FindTexture
//                                      80 IsTextureLoaded 102 Lock 103 Unlock
//  ITexture 13 Download (no rect = ReconstructTexture = disk reread) 15 IsError
//  IMaterial 37 Refresh (Uncache + Precache = vmt reparse) 42 IsErrorMaterial
//  vgui2 CLocalizedStringTable (VGUI_Localize005) 0 AddFile(name, pathID, fallback) 11 file count
//                                      12 file name. AddFile with fallback off loads only the FIRST
//                                      copy on the search path (8f80 loop, 9d23); AddString replaces
//                                      a token that exists (9ed0 -> a2a7). ReloadLocalizationFiles
//                                      (14) = AddFile again per file with its old flag, so a hud's
//                                      partial chat_english.txt kept the old hud's other tokens.
//  CScheme +0x1a8 = font alias tree allocation count: ReloadFontGlyphs' loop bound (vgui2 ed2f)
//  vgui2 IVGui (VGUI_ivgui008)        20 PostMessage(target, kv, from, delay) (vguimatsurface 1165d)
//  IPanel (VGUI_Panel009)             17 GetChildCount 18 GetChild 20 GetParent 36 GetName
//  ISurface                            6 GetEmbeddedPanel 79 GetPopupCount 80 GetPopup

// aim/helpers.cpp: KeyValues "<name>" { ka a kb b } for IVGui::PostMessage, null without the aim half
void* Aim_NewVguiMsg2i(const char* name, const char* ka, int a, const char* kb, int b);

namespace {

constexpr int kFsAddSearchPath = 7, kFsRemoveSearchPath = 8;
constexpr int kEngExecuteClientCmd = 102, kEngIsDrawingLoadingImage = 28;
constexpr int kSmGetIScheme = 8, kSmLoadSchemeFromFileEx = 12, kScGetSizingPanel = 12;
constexpr int kSurfGetScreenSize = 38, kSurfOnScreenSizeChanged = 111;
constexpr int kSurfGetEmbeddedPanel = 6, kSurfGetPopupCount = 79, kSurfGetPopup = 80;
constexpr int kPnlGetChildCount = 17, kPnlGetChild = 18, kPnlGetParent = 20, kPnlGetName = 36;
constexpr int kVguiPostMessage = 20;
constexpr int kMsFindMaterial = 71, kMsIsMaterialLoaded = 72, kMsFindTexture = 79, kMsIsTextureLoaded = 80;
constexpr int kMsLock = 102, kMsUnlock = 103;
constexpr int kTexDownload = 13, kTexIsError = 15, kMatRefresh = 37, kMatIsError = 42;
constexpr size_t kSmCount = 0x18;
constexpr size_t kScName = 0x8, kScTag = 0x108, kScBody = 0x148, kScSize = 0x208, kScFontAllocCount = 0x1a8;
constexpr int kLocAddFile = 0, kLocFileCount = 11, kLocFileName = 12;
constexpr int kPathAddToHead = 0;   // SearchPathAdd_t PATH_ADD_TO_HEAD
const char* const kPathIds[] = { "GAME", "mod", "custom_mod" };

std::string       s_applied;          // g_multiHud as last applied (main thread)
mh::Mount         s_mount;            // what we changed in the search path (main thread)
std::atomic<bool> s_holding{ false }; // our mounts are live (eject waits)
std::atomic<bool> s_reapply{ false }; // menu "reload": same pick again (hud files edited on disk)
char              s_status[256] = ""; // menu reads it from the render thread: a torn read is harmless
int               s_retired = 0;      // old scheme bodies, renamed mh_old_<n>
// cvars a hud cfg changed: name -> value before the hud touched it, put back when the hud is left
std::vector<std::pair<std::string, std::string>> s_cvarBackup;
std::string       s_resCacheOrig;     // vgui_cache_res_files before we turned it off, "" = not off

void SetStatus(const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vsnprintf(s_status, sizeof s_status, fmt, va);
    va_end(va);
    DebugLog("multihud: %s", s_status);
}

void* Fs()
{
    static void* p = nullptr;
    if (!p) p = GetInterface("filesystem_stdio.dll", "VFileSystem022");
    return p;
}
void* SchemeMgr()
{
    static void* p = nullptr;
    if (!p) p = GetInterface("vgui2.dll", "VGUI_Scheme010");
    return p;
}
void* Ivgui()
{
    static void* p = nullptr;
    if (!p) p = GetInterface("vgui2.dll", "VGUI_ivgui008");
    return p;
}
void* Localize()
{
    static void* p = nullptr;
    if (!p) p = GetInterface("vgui2.dll", "VGUI_Localize005");
    return p;
}
void* Surface()
{
    static const char* const k[] = { "VGUI_Surface031", "VGUI_Surface030", nullptr };
    static void* p = nullptr;
    if (!p) p = GetInterfaceAny("vguimatsurface.dll", k);
    return p;
}

void Exec(const char* cmd)
{
    DebugLog("multihud: exec %s", cmd);
    if (g_engine) VCall<void>(g_engine, kEngExecuteClientCmd, cmd);
}

// ---- files -----------------------------------------------------------------------------------
bool Exists(const std::string& p, bool dir)
{
    const DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && ((a & FILE_ATTRIBUTE_DIRECTORY) != 0) == dir;
}
std::vector<std::string> SubDirs(const std::string& d)
{
    std::vector<std::string> out;
    WIN32_FIND_DATAA fd;
    const HANDLE h = FindFirstFileA((d + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != '.') out.push_back(fd.cFileName);
    while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) < 0; });
    return out;
}
std::string HudsDir() { return std::string(CONFIG_DIR) + "\\huds"; }
std::string CustomDir()   // <tf2>\tf\custom, from the exe (tf_win64.exe sits in the game root)
{
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string d = exe;
    const size_t s = d.find_last_of("\\/");
    return (s == std::string::npos ? std::string(".") : d.substr(0, s)) + "\\tf\\custom";
}
// rel = "resource/ClientScheme.res"
bool AnyDirHas(const std::vector<std::string>& dirs, std::string rel)
{
    std::replace(rel.begin(), rel.end(), '/', '\\');
    return std::any_of(dirs.begin(), dirs.end(), [&](const std::string& d) { return Exists(d + "\\" + rel, false); });
}
void WalkMaterials(const std::string& base, const std::string& rel, std::vector<std::string>& tex,
                   std::vector<std::string>& mat, int depth)
{
    if (depth > 16) return;
    WIN32_FIND_DATAA fd;
    const HANDLE h = FindFirstFileA((base + (rel.empty() ? "" : "\\" + rel) + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do
    {
        if (fd.cFileName[0] == '.') continue;
        const std::string r = rel.empty() ? std::string(fd.cFileName) : rel + "\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { WalkMaterials(base, r, tex, mat, depth + 1); continue; }
        std::string name, ext;
        if (mh::MaterialName(r, name, ext)) (ext == "vtf" ? tex : mat).push_back(name);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

// ---- search path -----------------------------------------------------------------------------
// RemoveSearchPath lowercases + slash-fixes + appends a slash before it compares, so the plain
// absolute path matches what gameinfo's tf/custom/* mounted. True = at least one id was there.
bool FsRemove(const std::string& p)
{
    bool any = false;
    for (const char* id : kPathIds)
        if (VCall<bool>(Fs(), kFsRemoveSearchPath, p.c_str(), id)) any = true;
    return any;
}
void FsAdd(const std::string& p)
{
    for (const char* id : kPathIds) VCall<void>(Fs(), kFsAddSearchPath, p.c_str(), id, kPathAddToHead);
}

// ---- schemes (raw CScheme memory: SEH helpers, no C++ objects in these frames) ------------------
int SchemeCount(void* sm)
{
    __try { return *reinterpret_cast<int*>(static_cast<char*>(sm) + kSmCount); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
bool CopyName(const char* src, char* dst, size_t n)
{
    __try { memcpy(dst, src, n); dst[n - 1] = 0; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { dst[0] = 0; return false; }
}
// Same class (vtable) or no swap. The body is heap pointers + ints, so a memberwise swap moves the
// whole loaded state; name + tag (+8..+0x148) stay with each object.
bool SwapBody(char* a, char* b)
{
    __try
    {
        if (*reinterpret_cast<void**>(a) != *reinterpret_cast<void**>(b)) return false;
        char tmp[kScSize - kScBody];
        memcpy(tmp, a + kScBody, sizeof tmp);
        memcpy(a + kScBody, b + kScBody, sizeof tmp);
        memcpy(b + kScBody, tmp, sizeof tmp);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// ponytail: retired bodies are never freed (vgui2 has no scheme unload): one per scheme per switch,
// a few KB. Renamed so no name / tag lookup ever lands on them again, and their font alias tree
// emptied (allocation count 0 = ReloadFontGlyphs' loop never runs): every ResetFontCaches (each
// real mode change, the fallback rebuild) would rebuild every old hud's fonts again.
// Nothing reads a retired body's fonts: every panel holds the live handle, the rebuild reapplied it.
void Retire(char* b, int n)
{
    __try
    {
        _snprintf_s(b + kScName, 256, _TRUNCATE, "mh_old_%d", n);
        _snprintf_s(b + kScTag, 64, _TRUNCATE, "mh_old_%d", n);
        *reinterpret_cast<int*>(b + kScFontAllocCount) = 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

int ReloadSchemes(const std::vector<std::string>& dirs)
{
    void* sm = SchemeMgr();
    if (!sm) { DebugLog("multihud: VGUI_Scheme010 not found, schemes left as they were"); return 0; }
    const int count = SchemeCount(sm);
    if (count < 2 || count > 1024) { DebugLog("multihud: scheme count %d unreadable, schemes skipped", count); return 0; }
    int done = 0;
    for (int h = 1; h < count; ++h)   // 0 = vgui2's null scheme
    {
        char* a = VCall<char*>(sm, kSmGetIScheme, (unsigned long)h);
        char name[256], tag[64];
        if (!a || !CopyName(a + kScName, name, sizeof name) || !CopyName(a + kScTag, tag, sizeof tag)) continue;
        if (!name[0] || !strncmp(name, "mh_old_", 7) || !AnyDirHas(dirs, name)) continue;
        const std::string alt = mh::AltSchemeName(name);
        if (alt.empty()) continue;
        const uintptr_t sizing = VCall<uintptr_t>(a, kScGetSizingPanel);
        const int before = SchemeCount(sm);
        const unsigned long hb = VCall<unsigned long>(sm, kSmLoadSchemeFromFileEx, sizing, alt.c_str(), (const char*)tag);
        // a handle below `before` = vgui2 matched an existing scheme instead of loading: never swap that
        char* b = hb >= (unsigned long)before ? VCall<char*>(sm, kSmGetIScheme, hb) : nullptr;
        if (!b || b == a || !SwapBody(a, b))
        {
            // a fresh load left under the second spelling would answer it next switch (no reload) and
            // duplicate the tag: retire it too
            if (b && b != a) Retire(b, s_retired++);
            DebugLog("multihud: scheme %s (%s) NOT reloaded (handle %d, fresh load %lu, count %d)", name, tag, h, hb, before);
            continue;
        }
        Retire(b, s_retired++);
        ++done;
        DebugLog("multihud: scheme %s (%s) reloaded in place (handle %d, old body -> %lu)", name, tag, h, hb);
    }
    return done;
}

// ---- materials -------------------------------------------------------------------------------
int RefreshMaterials(const std::vector<std::string>& dirs)
{
    if (!g_matSystem) return 0;
    std::vector<std::string> tex, mat;
    for (const auto& d : dirs) WalkMaterials(d + "\\materials", "", tex, mat, 0);
    for (auto* v : { &tex, &mat }) { std::sort(v->begin(), v->end()); v->erase(std::unique(v->begin(), v->end()), v->end()); }
    int n = 0;
    // Download locks the material system itself (one sync with the render thread per texture)
    for (const auto& t : tex)
    {
        if (!VCall<bool>(g_matSystem, kMsIsTextureLoaded, t.c_str())) continue;
        void* tx = VCall<void*>(g_matSystem, kMsFindTexture, t.c_str(), "VGUI", false, 0);
        if (!tx || VCall<bool>(tx, kTexIsError)) continue;
        VCall<void>(tx, kTexDownload, (void*)nullptr, 0);
        ++n;
    }
    int loaded = 0;
    for (const auto& m : mat) if (VCall<bool>(g_matSystem, kMsIsMaterialLoaded, m.c_str())) ++loaded;
    if (!loaded) return n;
    // Refresh does not lock: hold the render thread off while the vmts reparse
    void* lock = VCall<void*>(g_matSystem, kMsLock);
    for (const auto& m : mat)
    {
        if (!VCall<bool>(g_matSystem, kMsIsMaterialLoaded, m.c_str())) continue;
        void* mt = VCall<void*>(g_matSystem, kMsFindMaterial, m.c_str(), "Other textures", false, (const char*)nullptr);
        if (!mt || VCall<bool>(mt, kMatIsError)) continue;
        VCall<void>(mt, kMatRefresh);
        ++n;
    }
    VCall<void>(g_matSystem, kMsUnlock, lock);
    return n;
}

// ---- localization ----------------------------------------------------------------------------
// Every registered string file (resource/chat_english.txt, tf_english.txt, ...) the old or new hud
// ships is added again WITH fallback search paths: every copy on the search path, lowest priority
// first, so the default file goes in and the hud's copy lands on top of it. A token the old hud
// changed and the new one lacks reverts to default; a partial hud file keeps the default rest.
int ReloadStrings(const std::vector<std::string>& dirs)
{
    void* loc = Localize();
    if (!loc) { DebugLog("multihud: VGUI_Localize005 not found, strings left as they were"); return 0; }
    const int count = VCall<int>(loc, kLocFileCount);
    if (count <= 0 || count > 4096) { DebugLog("multihud: localization file count %d unreadable", count); return 0; }
    std::vector<std::string> files;   // copied first: AddFile re-registers (removes + appends) its entry
    for (int i = 0; i < count; ++i)
        if (const char* f = VCall<const char*>(loc, kLocFileName, i))
            if (*f) files.emplace_back(f);
    int n = 0;
    for (const auto& f : files)
    {
        if (!AnyDirHas(dirs, f)) continue;
        const bool ok = VCall<bool>(loc, kLocAddFile, f.c_str(), (const char*)nullptr, true);
        DebugLog("multihud: strings %s %s", f.c_str(), ok ? "reloaded" : "FAILED to load");
        n += ok;
    }
    return n;
}

// ---- cvars -----------------------------------------------------------------------------------
bool ReadCvarRaw(void* cv, char* out, size_t n)
{
    __try
    {
        const ConVarStr s = ConVarStrSlot(cv);
        if (!s.ppStr) return false;
        strncpy_s(out, n, *s.ppStr ? *s.ppStr : "", _TRUNCATE);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// false = no such cvar (a command, an alias, a typo)
bool CvarValue(const std::string& name, std::string& out)
{
    if (!g_cvar || name.empty()) return false;
    ConVar* cv = g_cvar->FindVar(name.c_str());
    char buf[512];
    if (!cv || !ReadCvarRaw(cv, buf, sizeof buf)) return false;
    out = buf;
    return true;
}
void BackupCvar(const std::string& name)
{
    for (const auto& b : s_cvarBackup) if (!_stricmp(b.first.c_str(), name.c_str())) return;
    std::string v;
    if (CvarValue(name, v)) s_cvarBackup.emplace_back(name, v);
}
void RestoreCvars()
{
    if (s_cvarBackup.empty()) return;
    DebugLog("multihud: putting back %zu cvars the last hud's cfg set", s_cvarBackup.size());
    for (auto it = s_cvarBackup.rbegin(); it != s_cvarBackup.rend(); ++it)
    {
        std::string v = it->second;
        v.erase(std::remove(v.begin(), v.end(), '"'), v.end());
        Exec((it->first + " \"" + v + "\"").c_str());
    }
    s_cvarBackup.clear();
}
// off while our mounts are live, the user's value back after (eject / off)
void SetResCache(bool off)
{
    if (off && s_resCacheOrig.empty())
    {
        std::string v;
        if (!CvarValue("vgui_cache_res_files", v)) v = "1";
        if (v == "0") return;   // already off: nothing of ours to put back later
        s_resCacheOrig = v;
        Exec("vgui_cache_res_files 0");
    }
    else if (!off && !s_resCacheOrig.empty())
    {
        Exec(("vgui_cache_res_files " + s_resCacheOrig).c_str());
        s_resCacheOrig.clear();
    }
}

// ---- hud cfg ---------------------------------------------------------------------------------
bool ReadText(const std::string& path, std::string& out)
{
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") || !f) return false;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}
void RunCfgFile(const std::string& dir, std::string name, int depth, std::vector<std::string>& seen, int& ran)
{
    std::replace(name.begin(), name.end(), '/', '\\');
    std::string path = dir + "\\" + name;
    if (!Exists(path, false)) path += ".cfg";   // "exec hud" = hud.cfg, as the console does it
    if (!Exists(path, false)) { DebugLog("multihud: cfg exec %s skipped (not in the hud's cfg)", name.c_str()); return; }
    const std::string key = mh::Lower(path);
    if (depth > 8 || std::find(seen.begin(), seen.end(), key) != seen.end()) return;
    seen.push_back(key);
    std::string text;
    if (!ReadText(path, text)) return;
    DebugLog("multihud: cfg %s", path.c_str());
    for (const auto& cmd : mh::SplitCommands(text))
    {
        std::string target;
        switch (mh::Classify(cmd, &target))
        {
        case mh::CfgAct::Exec: RunCfgFile(dir, target, depth + 1, seen, ran); break;
        case mh::CfgAct::Skip: DebugLog("multihud: cfg skip '%s'", cmd.c_str()); break;
        case mh::CfgAct::Run:  BackupCvar(mh::CvarName(cmd)); Exec(cmd.c_str()); ++ran; break;
        }
    }
}
// what the hud would run at boot from tf/custom: its valve.rc (the startup script), else autoexec.cfg
int RunHudCfg(const std::string& root)
{
    if (root.empty()) return 0;
    const std::string dir = root + "\\cfg";
    const char* entry = Exists(dir + "\\valve.rc", false) ? "valve.rc"
                      : Exists(dir + "\\autoexec.cfg", false) ? "autoexec.cfg" : nullptr;
    if (!entry) return 0;
    std::vector<std::string> seen;
    int ran = 0;
    RunCfgFile(dir, entry, 0, seen, ran);
    return ran;
}

// ---- panels ----------------------------------------------------------------------------------
uintptr_t FindPanel(void* pn, uintptr_t node, const char* name, int depth)
{
    if (depth > 24) return 0;
    const char* nm = VCall<const char*>(pn, kPnlGetName, node);
    if (nm && !strcmp(nm, name)) return node;
    const int n = VCall<int>(pn, kPnlGetChildCount, node);
    for (int i = 0; i < n && i < 4096; ++i)
        if (const uintptr_t c = VCall<uintptr_t>(pn, kPnlGetChild, node, i))
            if (const uintptr_t r = FindPanel(pn, c, name, depth + 1)) return r;
    return 0;
}
bool PostSizeChanged(uintptr_t panel, int w, int h)
{
    void* kv = Aim_NewVguiMsg2i("OnScreenSizeChanged", "oldwide", w, "oldtall", h);
    if (!kv) return false;
    VCall<void>(Ivgui(), kVguiPostMessage, panel, kv, (uintptr_t)0, 0.f);
    return true;
}
// Panel::OnScreenSizeChanged (Panel.cpp:1003) = repost to the children + scheme update + layout,
// nothing deleted. Every branch but the viewport's (hud_reloadscheme did that one). -1 = could not
// (no KeyValues without the aim half, interface missing): caller takes the surface path.
int ReapplyOtherPanels(int w, int h)
{
    void* pn = VGui_GetPanelInterface();
    void* s = Surface();
    if (!pn || !s || !Ivgui()) return -1;
    const uintptr_t root = VCall<uintptr_t>(s, kSurfGetEmbeddedPanel);
    const uintptr_t vp = root ? FindPanel(pn, root, "CBaseViewport", 0) : 0;
    if (!vp) { DebugLog("multihud: CBaseViewport not in the vgui tree"); return -1; }
    std::vector<uintptr_t> chain;   // the viewport and its parents: walked into, never posted to
    for (uintptr_t p = vp; p && chain.size() < 64; p = VCall<uintptr_t>(pn, kPnlGetParent, p)) chain.push_back(p);
    auto inChain = [&](uintptr_t p) { return std::find(chain.begin(), chain.end(), p) != chain.end(); };
    int posted = 0;
    std::vector<std::pair<uintptr_t, int>> stack{ { root, 0 } };
    while (!stack.empty())
    {
        const auto [node, depth] = stack.back();
        stack.pop_back();
        const int n = VCall<int>(pn, kPnlGetChildCount, node);
        for (int i = 0; i < n && i < 4096; ++i)
        {
            const uintptr_t c = VCall<uintptr_t>(pn, kPnlGetChild, node, i);
            if (!c || c == vp) continue;
            if (inChain(c)) { if (depth < 32) stack.push_back({ c, depth + 1 }); continue; }
            if (!PostSizeChanged(c, w, h)) return posted ? posted : -1;
            ++posted;
        }
    }
    // popups with no parent sit only in the surface's list
    const int pc = VCall<int>(s, kSurfGetPopupCount);
    for (int i = 0; i < pc && i < 1024; ++i)
    {
        const uintptr_t p = VCall<uintptr_t>(s, kSurfGetPopup, i);
        if (!p || p == root || inChain(p) || VCall<uintptr_t>(pn, kPnlGetParent, p)) continue;
        if (!PostSizeChanged(p, w, h)) break;
        ++posted;
    }
    return posted;
}

// ---- bitmap fonts (.vbf) ---------------------------------------------------------------------
// vguimatsurface caches a .vbf twice (x64 RE 11-15): CBitmapFont::Create (CBitmapFont vt 18,
// 15d70) looks the file's base name up in g_BitmapFontTable and only reads the file when missing,
// and the font manager hands back an existing CBitmapFont for the same name + scale (17410). Huds
// ship the SAME names with different atlases (typoster_57.vbf in 9 of them): new texture page, old
// glyph metrics = garbled text. So each table entry the old/new hud ships is rewritten in place
// from the file the engine would read now, and every font built on it runs Create again (its found
// path only recomputes size fields from the header, 1600a..16118, nothing allocated).
//  table entry (32 bytes): +0 name symbol, +8 header (0x116 bytes, "VFNT" v3, glyph count short
//  +0x14), +0x10 glyphs (14 bytes each), +0x18 ITexture page "vgui/fonts/<base>"
//  CBitmapFont: +0x70 table index, +0x74 scale x, +0x78 scale y, +0x40 dword bit 0x100000 = flag 0x100
constexpr size_t kBfEntry = 32, kBfHeader = mh::kVbfHeader, kBfGlyph = mh::kVbfGlyph;
constexpr int kBfsRead = 0, kBfsOpen = 2, kBfsClose = 3, kBfsSeek = 4, kBfsTell = 5;   // IBaseFileSystem (fs+8)
constexpr int kTexGetName = 0, kFontGetFlags = 11, kBitmapFontCreate = 18, kSurfSetBitmapFontGlyphSet = 121;

bool AnyFile(const std::string& pattern)
{
    WIN32_FIND_DATAA fd;
    const HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    FindClose(h);
    return true;
}
int ReadI32(const void* p)
{
    __try { return *static_cast<const int*>(p); } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
void* ReadPtr(const void* p)
{
    __try { return *static_cast<void* const*>(p); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
float ReadF32(const void* p)
{
    __try { return *static_cast<const float*>(p); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0.f; }
}
short ReadI16(const void* p)
{
    __try { return *static_cast<const short*>(p); } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
bool BfOverwrite(char* header, char* glyphs, const unsigned char* file, int glyphCount)
{
    __try
    {
        memcpy(header, file, kBfHeader);
        memcpy(glyphs, file + kBfHeader, (size_t)glyphCount * kBfGlyph);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// g_BitmapFontTable (CUtlVector): the count load in CBitmapFont::Create, mem = count - 0x10
int* BitmapFontTableCount()
{
    static int* p = nullptr;
    static bool tried = false;
    if (!tried)
    {
        tried = true;
        if (HMODULE m = GetModuleHandleA("vguimatsurface.dll"))
            if (uint8_t* a = PatternScan(m, "44 8B 05 ? ? ? ? 45 33 F6 41 8B CE BD 80 00 00 00"))
                p = reinterpret_cast<int*>(a + 7 + ReadI32(a + 3));
        if (!p) DebugLog("multihud: bitmap font table sig not found, .vbf metrics not reloaded");
    }
    return p;
}
// FontManager(): ISurface::SetBitmapFontGlyphSet (slot 121) calls it at +0x17, and it is
// `lea rax, [rip+g_FontManager]; ret` (vguimatsurface 12b37 / 16a60). Null = bytes moved.
char* FindFontManager()
{
    __try
    {
        void* s = Surface();
        if (!s) return nullptr;
        const uint8_t* f = (*reinterpret_cast<uint8_t***>(s))[kSurfSetBitmapFontGlyphSet];
        if (f[0x17] != 0xE8) return nullptr;
        const uint8_t* acc = f + 0x17 + 5 + *reinterpret_cast<const int32_t*>(f + 0x18);
        if (acc[0] != 0x48 || acc[1] != 0x8D || acc[2] != 0x05 || acc[7] != 0xC3) return nullptr;
        return (char*)(acc + 7 + *reinterpret_cast<const int32_t*>(acc + 3));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
// the file as the engine reads it (GAME path: our hud first, else the vpk default)
bool ReadGameFile(const std::string& name, std::vector<unsigned char>& out)
{
    void* fs = Fs();
    if (!fs) return false;
    void* bfs = static_cast<char*>(fs) + 8;
    void* fh = VCall<void*>(bfs, kBfsOpen, name.c_str(), "rb", "GAME");
    if (!fh) return false;
    VCall<void>(bfs, kBfsSeek, fh, 0, 2);   // FILESYSTEM_SEEK_TAIL
    const unsigned size = VCall<unsigned>(bfs, kBfsTell, fh);
    VCall<void>(bfs, kBfsSeek, fh, 0, 0);   // FILESYSTEM_SEEK_HEAD
    bool ok = size > 0 && size < (16u << 20);
    if (ok)
    {
        out.resize(size);
        ok = VCall<int>(bfs, kBfsRead, (void*)out.data(), (int)size, fh) == (int)size;
    }
    VCall<void>(bfs, kBfsClose, fh);
    return ok;
}
int RefreshBitmapFonts(const std::vector<std::string>& dirs)
{
    std::vector<std::string> bases;   // .vbf base names the old / new hud ship
    for (const auto& d : dirs)
    {
        WIN32_FIND_DATAA fd;
        const HANDLE h = FindFirstFileA((d + "\\materials\\vgui\\fonts\\*.vbf").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do
        {
            std::string b = mh::Lower(fd.cFileName);
            b.resize(b.size() - 4);
            if (std::find(bases.begin(), bases.end(), b) == bases.end()) bases.push_back(b);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    if (bases.empty()) return 0;
    int* countp = BitmapFontTableCount();
    if (!countp) return 0;
    const int count = ReadI32(countp);
    char* mem = static_cast<char*>(ReadPtr(reinterpret_cast<char*>(countp) - 0x10));
    if (count <= 0 || count > 4096 || !mem) return 0;
    std::vector<std::pair<int, std::string>> changed;   // table index -> file name
    for (int i = 0; i < count; ++i)
    {
        char* e = mem + (size_t)i * kBfEntry;
        char* header = static_cast<char*>(ReadPtr(e + 8));
        char* glyphs = static_cast<char*>(ReadPtr(e + 0x10));
        void* page = ReadPtr(e + 0x18);
        const char* tn = page ? VCall<const char*>(page, kTexGetName) : nullptr;
        if (!header || !glyphs || !tn) continue;
        std::string base = mh::Lower(tn);
        if (const size_t sl = base.find_last_of("/\\"); sl != std::string::npos) base.erase(0, sl + 1);
        if (std::find(bases.begin(), bases.end(), base) == bases.end()) continue;
        const std::string file = "materials/vgui/fonts/" + base + ".vbf";
        std::vector<unsigned char> buf;
        if (!ReadGameFile(file, buf) || buf.size() < kBfHeader)
        {
            DebugLog("multihud: %s not readable, bitmap font left as it was", file.c_str());
            continue;
        }
        // ponytail: the glyph block was sized by the first load and is the game's allocation (tier0);
        // a file with more glyphs would need a realloc we can't free right, so it is skipped. Not seen
        // in practice: one tool, one charset.
        const int oldCount = ReadI16(header + 0x14);
        const int glyphCount = mh::VbfGlyphs(buf.data(), buf.size(), oldCount);
        if (glyphCount < 0)
        {
            DebugLog("multihud: %s not a v3 VFNT file or more glyphs than the loaded %d, skipped", file.c_str(), oldCount);
            continue;
        }
        if (!BfOverwrite(header, glyphs, buf.data(), glyphCount)) continue;
        changed.emplace_back(i, file);
        DebugLog("multihud: bitmap font %s reloaded (table %d, %d glyphs)", file.c_str(), i, glyphCount);
    }
    if (changed.empty()) return 0;
    static char* fm = FindFontManager();
    if (!fm) { DebugLog("multihud: FontManager not found, bitmap font sizes not recomputed"); return (int)changed.size(); }
    const int nFonts = ReadI32(fm + 0x30);
    char** fonts = static_cast<char**>(ReadPtr(fm + 0x20));
    int rebuilt = 0;
    for (int i = 0; fonts && i < nFonts && i < 65536; ++i)
    {
        char* f = static_cast<char*>(ReadPtr(fonts + i));
        if (!f || !(VCall<int>(f, kFontGetFlags) & 0x800)) continue;   // FONTFLAG_BITMAP
        const int idx = ReadI32(f + 0x70);
        const auto it = std::find_if(changed.begin(), changed.end(), [idx](const auto& c) { return c.first == idx; });
        if (it == changed.end()) continue;
        const int createFlags = (ReadI32(f + 0x40) & 0x100000) ? 0x100 : 0;
        if (VCall<bool>(f, kBitmapFontCreate, it->second.c_str(), ReadF32(f + 0x74), ReadF32(f + 0x78), createFlags)) ++rebuilt;
    }
    DebugLog("multihud: %d bitmap font sizes recomputed", rebuilt);
    return (int)changed.size();
}

// ---- close captions --------------------------------------------------------------------------
// CHudCloseCaption::InitCaptionDictionary returns early for the dictionary it already has
// (hud_closecaption.cpp:2578); cc_lang's change callback is the only caller (2759). Flip cc_lang to a
// language that has no file, then back: the second call rereads closecaption_<lang>.dat from every
// GAME path (hud first) and re-adds closecaption_<lang>.txt with fallback.
bool ReloadCaptions(const std::vector<std::string>& dirs)
{
    if (std::none_of(dirs.begin(), dirs.end(), [](const std::string& d) { return AnyFile(d + "\\resource\\closecaption_*"); }))
        return false;
    std::string lang;
    if (!CvarValue("cc_lang", lang)) return false;
    lang.erase(std::remove(lang.begin(), lang.end(), '"'), lang.end());
    Exec("cc_lang \"multihud_reload\"");
    Exec(("cc_lang \"" + lang + "\"").c_str());
    return true;
}

// ---- the switch ------------------------------------------------------------------------------
void Reload(const std::vector<std::string>& dirs, const std::string& label, const std::string& root)
{
    const DWORD t0 = GetTickCount();
    DWORD t = t0;
    auto lap = [&t]() { const DWORD now = GetTickCount(), d = now - t; t = now; return (unsigned long)d; };
    SetResCache(!s_mount.hud.empty() || !s_mount.customOff.empty());
    Exec("hud_reloadscheme");                  // KeyValues + client res cache flush before anything loads
    const unsigned long msFlush = lap();
    const int schemes = ReloadSchemes(dirs);
    const unsigned long msSchemes = lap();
    const int mats = RefreshMaterials(dirs);
    const int vbf = RefreshBitmapFonts(dirs);  // metrics for the atlas pages the material pass reread
    const unsigned long msMats = lap();
    const int strings = ReloadStrings(dirs);   // before the reapply: labels resolve #tokens on apply
    const bool captions = ReloadCaptions(dirs);
    const unsigned long msStrings = lap();
    RestoreCvars();                            // the hud being left
    const int cfg = RunHudCfg(root);           // the hud being entered (cl_hud_minmode etc. shape the layout)
    const unsigned long msCfg = lap();
    const bool snd = std::any_of(dirs.begin(), dirs.end(), [](const std::string& d) { return Exists(d + "\\sound", true); });
    if (snd) Exec("snd_restart");              // cached hud sounds (hitsound, killsound)
    const unsigned long msSound = lap();
    Exec("hud_reloadscheme");                  // viewport + hud elements on the new scheme + cvars
    int w = 0, h = 0, posted = -1;
    if (void* s = Surface()) VCall<void>(s, kSurfGetScreenSize, &w, &h);
    if (w > 0 && h > 0) posted = ReapplyOtherPanels(w, h);
    if (posted < 0)
    {
        DebugLog("multihud: no per-branch reapply (aim half off?), resolution change path instead");
        if (void* s = Surface(); s && w > 0 && h > 0) VCall<void>(s, kSurfOnScreenSizeChanged, w, h);
    }
    VGui_ResetPanelCache();
    const unsigned long msPanels = lap();
    DebugLog("multihud: %s -- %d schemes, %d materials, %d bitmap fonts, %d string files%s, %d cfg commands%s, "
             "%d branches | ms: flush %lu schemes %lu materials %lu strings %lu cfg %lu sound %lu panels %lu",
             label.c_str(), schemes, mats, vbf, strings, captions ? ", captions" : "", cfg, snd ? ", sounds" : "",
             posted, msFlush, msSchemes, msMats, msStrings, msCfg, msSound, msPanels);
    SetStatus("%s loaded (%.1f s)", label.c_str(), (GetTickCount() - t0) / 1000.f);
}

void Apply(const std::string& pick)
{
    const std::string label = pick.empty() ? "off" : pick == mh::kDefault ? "tf2 default" : pick;
    std::string root;
    if (!pick.empty() && pick != mh::kDefault)
    {
        if (!mh::SafeName(pick)) { SetStatus("'%s' is not a folder name", pick.c_str()); return; }
        const std::string d = HudsDir() + "\\" + pick;
        root = mh::FindHudRoot(d, SubDirs(d), Exists);
        if (root.empty())
        {
            SetStatus("%s is not a hud (no resource\\ui or scripts\\hudlayout.res)", pick.c_str());
            return;
        }
    }
    if (!Fs()) { SetStatus("filesystem not found, nothing switched"); return; }

    const bool customOff = !pick.empty();
    std::vector<std::string> customHuds;
    if (customOff && s_mount.customOff.empty())
    {
        const std::string c = CustomDir();
        for (const auto& s : SubDirs(c))
            if (mh::IsHudDir(c + "\\" + s, Exists)) customHuds.push_back(c + "\\" + s);
    }
    const auto ops = mh::PlanMount(s_mount, root, customOff, customHuds);
    // up before the first mount changes: an eject that lands during the rebuild waits for it, and
    // then for the restore its FrameStageNotify runs (thread.cpp)
    if (!ops.empty()) s_holding = true;
    mh::Mount next;
    next.hud = root;
    if (customOff) next.customOff = s_mount.customOff;
    for (const auto& o : ops)
    {
        switch (o.op)
        {
        case mh::Op::RemoveHud:    FsRemove(o.path); DebugLog("multihud: unmount %s", o.path.c_str()); break;
        case mh::Op::RemoveCustom:
            if (FsRemove(o.path)) { next.customOff.push_back(o.path); DebugLog("multihud: unmount tf/custom hud %s", o.path.c_str()); }
            else DebugLog("multihud: tf/custom hud %s was not mounted (path mismatch?), it still answers", o.path.c_str());
            break;
        case mh::Op::AddCustom:    FsAdd(o.path); DebugLog("multihud: remount tf/custom hud %s", o.path.c_str()); break;
        case mh::Op::AddHud:       FsAdd(o.path); DebugLog("multihud: mount %s", o.path.c_str()); break;
        }
    }
    s_mount = next;
    if (!ops.empty()) Reload(mh::Touched(ops), label, root);
    else SetStatus("%s", label.c_str());
    // after the rebuild: eject's wait (thread.cpp) must not move on while it still runs
    s_holding = !s_mount.hud.empty() || !s_mount.customOff.empty();
}

} // namespace

void MultiHud_MainTick()
{
    if (g_eject) return;
    // IVEngineClient::IsDrawingLoadingImage (28, after IsInGame 26 / IsConnected 27): no rebuild
    // mid map load, the pick waits for the first frame after it
    if (!g_engine || VCall<bool>(g_engine, kEngIsDrawingLoadingImage)) return;
    const bool again = s_reapply.exchange(false);
    const std::string pick = mh::Pick(g_multiHudOn, g_multiHud);
    if (!again && s_applied == pick) return;
    s_applied = pick;
    DebugLog("multihud: switch to '%s'", s_applied.c_str());
    Apply(s_applied);
}

void MultiHud_Eject()
{
    if (!s_holding) return;
    DebugLog("multihud: eject, putting the game's own huds back");
    s_applied.clear();
    Apply("");
}

bool MultiHud_Holding() { return s_holding; }
void MultiHud_Reapply() { s_reapply = true; }

std::vector<std::string> MultiHud_List()
{
    CreateDirectoryA(CONFIG_DIR, nullptr);
    CreateDirectoryA(HudsDir().c_str(), nullptr);
    return SubDirs(HudsDir());
}

const char* MultiHud_Status() { return s_status; }
