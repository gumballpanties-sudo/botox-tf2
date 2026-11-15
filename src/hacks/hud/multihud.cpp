#include "multihud.h"
#include "multihud_pure.h"
#include "../../render/render.h"   // VGui_ResetPanelCache
#include <windows.h>
#include "junction.h"   // script hud junctions
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
//                                      4 SetSize 14 SetVisible 29 IsAutoDeleteSet 55 GetPanel(vp, module)
//  IVGui                              15 PanelToHandle 16 HandleToPanel (index 20 bits + serial)
//  ISurface                            6 GetEmbeddedPanel 79 GetPopupCount 80 GetPopup
//                                     29 DrawGetTextureFile 30 DrawSetTextureFile (x64: filter + force
//                                      ignored, rebinds only on a NEW name -> size cached per id)
//  client EditablePanel +0x1f8 BuildGroup* (vt 213 LoadControlSettings, 95c3fe);
//  BuildGroup +0x70 m_pResourceName, +0x78 path id (new[] + strcpy in LoadControlSettings, 97a7c6)
//  IMaterialSystem 73 FirstMaterial 74 NextMaterial 75 InvalidMaterial 76 GetMaterial (u16 handles)

// aim/helpers.cpp: KeyValues "<name>" { ka a kb b } for IVGui::PostMessage, null without the aim half
void* Aim_NewVguiMsg2i(const char* name, const char* ka, int a, const char* kb, int b);

namespace {

constexpr int kFsAddSearchPath = 7, kFsRemoveSearchPath = 8;
constexpr int kEngExecuteClientCmd = 102, kEngIsDrawingLoadingImage = 28;
constexpr int kSmGetIScheme = 8, kSmLoadSchemeFromFileEx = 12, kScGetSizingPanel = 12;
constexpr int kSurfGetScreenSize = 38, kSurfOnScreenSizeChanged = 111;
constexpr int kSurfGetEmbeddedPanel = 6, kSurfGetPopupCount = 79, kSurfGetPopup = 80;
constexpr int kPnlGetChildCount = 17, kPnlGetChild = 18, kPnlGetParent = 20, kPnlIsAutoDeleteSet = 29, kPnlGetName = 36;
constexpr int kVguiPostMessage = 20, kVguiPanelToHandle = 15, kVguiHandleToPanel = 16;
constexpr int kPnlSetSize = 4, kPnlSetZPos = 8, kPnlSetVisible = 14, kPnlGetPanel = 55;
constexpr int kSurfDrawGetTextureFile = 29, kSurfDrawSetTextureFile = 30;
constexpr int kMsFirstMaterial = 73, kMsNextMaterial = 74, kMsInvalidMaterial = 75, kMsGetMaterial = 76;
constexpr int kBfsFileExists = 10;                              // IBaseFileSystem (fs+8)
constexpr size_t kEditableBuildGroup = 0x1f8, kBuildGroupResName = 0x70;
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

// defined with the bitmap fonts below, used earlier by the removal pass
void* ReadPtr(const void* p);
bool ReadGameFile(const std::string& name, std::vector<unsigned char>& out, const char* pathId = "GAME");

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
// fn(rel) for every file under base (rel = "sub\\file.ext", relative to base)
template <class Fn> void WalkFiles(const std::string& base, const std::string& rel, Fn&& fn, int depth = 0)
{
    if (depth > 16) return;
    WIN32_FIND_DATAA fd;
    const HANDLE h = FindFirstFileA((base + (rel.empty() ? "" : "\\" + rel) + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do
    {
        if (fd.cFileName[0] == '.') continue;
        const std::string r = rel.empty() ? std::string(fd.cFileName) : rel + "\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) WalkFiles(base, r, fn, depth + 1);
        else fn(r);
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
// bounded strcpy under SEH. Stops at the NUL: a fixed-size memcpy read past short heap strings
// (panel / .res names) into unmapped memory (first-chance AV in crash.log 11-15 12:47:54).
bool CopyName(const char* src, char* dst, size_t n)
{
    __try
    {
        size_t i = 0;
        for (; i + 1 < n && src[i]; ++i) dst[i] = src[i];
        dst[i] = 0;
        return true;
    }
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
bool GameFileExists(const std::string& rel)
{
    void* fs = Fs();
    return fs && VCall<bool>(static_cast<char*>(fs) + 8, kBfsFileExists, rel.c_str(), "GAME");
}
// normalized names (hud/d_images) of every .vtf / .vmt the old or new hud ships
std::vector<std::string> HudMaterialNames(const std::vector<std::string>& dirs, std::vector<std::string>* texOut)
{
    std::vector<std::string> tex, mat;
    for (const auto& d : dirs)
        WalkFiles(d + "\\materials", "", [&](const std::string& r) {
            std::string name, ext;
            if (mh::MaterialName(r, name, ext)) (ext == "vtf" ? tex : mat).push_back(name);
        });
    for (auto* v : { &tex, &mat }) { std::sort(v->begin(), v->end()); v->erase(std::unique(v->begin(), v->end()), v->end()); }
    std::vector<std::string> all = tex;
    all.insert(all.end(), mat.begin(), mat.end());
    std::sort(all.begin(), all.end());
    all.erase(std::unique(all.begin(), all.end()), all.end());
    if (texOut) *texOut = std::move(tex);
    return all;
}
// A file only the hud being left had is gone from the search path: reloading it would turn it into
// the error texture / material (magenta checkers, 11-15 target id). Those are left as they are; the
// reapply stops anything from pointing at them.
int RefreshMaterials(const std::vector<std::string>& names, const std::vector<std::string>& tex)
{
    if (!g_matSystem || names.empty()) return 0;
    int n = 0, gone = 0;
    // Download locks the material system itself (one sync with the render thread per texture)
    for (const auto& t : tex)
    {
        if (!VCall<bool>(g_matSystem, kMsIsTextureLoaded, t.c_str())) continue;
        if (!GameFileExists("materials/" + t + ".vtf")) { ++gone; continue; }
        void* tx = VCall<void*>(g_matSystem, kMsFindTexture, t.c_str(), "VGUI", false, 0);
        if (!tx || VCall<bool>(tx, kTexIsError)) continue;
        VCall<void>(tx, kTexDownload, (void*)nullptr, 0);
        ++n;
    }
    // every loaded material, matched on its normalized name: vgui loads hud images as
    // "vgui/../hud/x", which a plain IsMaterialLoaded("hud/x") never finds. A material named like a
    // reloaded .vtf is refreshed too (vtf-only override: its mapping size follows the new texture).
    std::vector<void*> todo;
    const unsigned short inv = VCall<unsigned short>(g_matSystem, kMsInvalidMaterial);
    int walked = 0;
    for (unsigned short h = VCall<unsigned short>(g_matSystem, kMsFirstMaterial); h != inv && walked < 200000;
         h = VCall<unsigned short>(g_matSystem, kMsNextMaterial, h), ++walked)
    {
        void* m = VCall<void*>(g_matSystem, kMsGetMaterial, h);
        if (!m || VCall<bool>(m, kMatIsError)) continue;
        const char* nm = VCall<const char*>(m, 0);
        if (!nm) continue;
        const std::string norm = mh::NormalizePath(nm);
        if (!std::binary_search(names.begin(), names.end(), norm)) continue;
        if (!GameFileExists("materials/" + norm + ".vmt")) { ++gone; continue; }
        todo.push_back(m);
    }
    if (!todo.empty())
    {
        // Refresh does not lock: hold the render thread off while the vmts reparse
        void* lock = VCall<void*>(g_matSystem, kMsLock);
        for (void* m : todo) VCall<void>(m, kMatRefresh);
        VCall<void>(g_matSystem, kMsUnlock, lock);
    }
    if (gone) DebugLog("multihud: %d loaded materials/textures only the old hud had, left as they were", gone);
    return n + (int)todo.size();
}
// vgui caches each texture id's size when its file is bound and only rebinds on a NEW name, so a
// hud image with other dimensions kept the old size: CHud::RefreshHudTextures turns kill icon pixel
// rects into uvs with it (hud.cpp:842) = broken kill icons. Bounce every affected id through
// vgui/white and back: the rebind reads the refreshed material's size.
int RebindVguiTextures(const std::vector<std::string>& names)
{
    void* s = Surface();
    if (!s || names.empty()) return 0;
    char buf[512];
    int rebound = 0, miss = 0;
    for (int id = 1; id < 65536 && miss < 4096; ++id)
    {
        buf[0] = 0;
        if (!VCall<bool>(s, kSurfDrawGetTextureFile, id, (char*)buf, (int)sizeof buf)) { ++miss; continue; }
        miss = 0;
        buf[sizeof buf - 1] = 0;
        if (!buf[0] || !std::binary_search(names.begin(), names.end(), mh::NormalizePath(buf))) continue;
        const std::string file = buf;
        VCall<void>(s, kSurfDrawSetTextureFile, id, "vgui/white", 0, false);
        VCall<void>(s, kSurfDrawSetTextureFile, id, file.c_str(), 0, false);
        ++rebound;
    }
    return rebound;
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
// client-side cvar = exists and is not the server's / replicated / cheat / userinfo (name, team...)
bool ClientCvar(const std::string& name)
{
    constexpr int kFcvarGameDll = 1 << 2, kFcvarUserInfo = 1 << 9, kFcvarReplicated = 1 << 13, kFcvarCheat = 1 << 14;
    if (!g_cvar) return false;
    ConVar* cv = g_cvar->FindVar(name.c_str());
    if (!cv) return false;
    const int f = cv->GetFlags();
    return f != -1 && !(f & (kFcvarGameDll | kFcvarUserInfo | kFcvarReplicated | kFcvarCheat));
}
struct CfgRun
{
    std::string dir;
    std::vector<std::string> seen;                              // files, no exec loops
    std::vector<std::pair<std::string, std::string>> aliases;   // defined by this cfg run: name -> body
    int ran = 0, skipped = 0;
};
void RunCfgText(CfgRun& r, const std::string& text, int depth);
void RunCfgFile(CfgRun& r, std::string name, int depth)
{
    std::replace(name.begin(), name.end(), '/', '\\');
    std::string path = r.dir + "\\" + name;
    if (!Exists(path, false)) path += ".cfg";   // "exec hud" = hud.cfg, as the console does it
    if (!Exists(path, false)) { DebugLog("multihud: cfg exec %s skipped (not in the hud's cfg)", name.c_str()); return; }
    const std::string key = mh::Lower(path);
    if (depth > 8 || std::find(r.seen.begin(), r.seen.end(), key) != r.seen.end()) return;
    r.seen.push_back(key);
    std::string text;
    if (!ReadText(path, text)) return;
    DebugLog("multihud: cfg %s", path.c_str());
    RunCfgText(r, text, depth);
}
void RunCfgText(CfgRun& r, const std::string& text, int depth)
{
    auto known = [&r](const std::string& n) {
        return std::any_of(r.aliases.begin(), r.aliases.end(), [&](const auto& a) { return a.first == n; });
    };
    for (const auto& cmd : mh::SplitCommands(text))
    {
        std::string target;
        switch (mh::Classify(cmd, &target, ClientCvar, known))
        {
        case mh::CfgAct::Exec: RunCfgFile(r, target, depth + 1); break;
        case mh::CfgAct::Alias:
        {
            const std::string body = mh::AliasBody(cmd);
            auto it = std::find_if(r.aliases.begin(), r.aliases.end(), [&](const auto& a) { return a.first == target; });
            if (it != r.aliases.end()) it->second = body; else r.aliases.emplace_back(target, body);
            Exec(cmd.c_str());   // defining is harmless: it only runs if someone calls it
            ++r.ran;
            break;
        }
        case mh::CfgAct::Expand:
        {
            auto it = std::find_if(r.aliases.begin(), r.aliases.end(), [&](const auto& a) { return a.first == target; });
            if (it != r.aliases.end() && depth < 8) RunCfgText(r, it->second, depth + 1);
            break;
        }
        case mh::CfgAct::Skip: ++r.skipped; DebugLog("multihud: cfg skip '%s'", cmd.c_str()); break;
        case mh::CfgAct::Run:  BackupCvar(mh::CvarName(cmd)); Exec(cmd.c_str()); ++r.ran; break;
        }
    }
}

// ---- script huds (femboy, m0re): the paths their layout switches assume ---------------------------
// Their switch cfgs (run from valve.rc, or by hand: "exec switchm0re") con_logfile cfg/<panel>.txt
// into tf/cfg holding #base "../custom/<hud>/resource/ui/layouts/...", and each .res does
// #base "../../../../cfg/<panel>.txt". KeyValues joins those onto the relative name
// (KeyValues.cpp:2083) and the filesystem resolves it per search path root, so from our mount the
// first hop is <root>/<n up>/cfg and the second <root>/<n up>/custom/<hud> (n = 2 for resource/ui/x.res
// "../../../../cfg", 1 for femboy's scripts/layouts "../../../cfg": mh::AddCfgClimbs reads each n off
// the hud's files). Directory junctions at every such n make both land where they do from tf/custom,
// live, for every switch: <n up>/cfg -> tf/cfg and <n up>/custom/<name> -> <root> for each name the
// hud's cfgs use (mh::CustomNamesIn).
// Junctions: no admin, and removing one never touches its target.
std::string GameDir()   // <tf2>\tf
{
    const std::string c = CustomDir();
    return c.substr(0, c.size() - strlen("\\custom"));
}
bool IsReparse(const std::string& p) { return JunctionIsReparse(p); }
// link -> target, replacing an older junction of ours; a real folder there is the user's: moved
// aside (renamed, nothing deleted) only for the cfg link, never for anything else
bool EnsureJunction(const std::string& link, const std::string& target, bool moveRealDirAside)
{
    if (IsReparse(link)) RemoveDirectoryA(link.c_str());   // removes the link only
    else if (Exists(link, true))
    {
        if (!moveRealDirAside) { DebugLog("multihud: %s is a real folder, left alone (script layouts may not resolve)", link.c_str()); return false; }
        char aside[64];
        snprintf(aside, sizeof aside, "_old_%lu", (unsigned long)GetTickCount());
        if (!MoveFileA(link.c_str(), (link + aside).c_str())) { DebugLog("multihud: cannot move %s aside", link.c_str()); return false; }
        DebugLog("multihud: moved real folder %s aside to %s%s", link.c_str(), link.c_str(), aside);
    }
    const bool ok = MakeJunction(link, target);
    DebugLog("multihud: junction %s -> %s %s", link.c_str(), target.c_str(), ok ? "made" : "FAILED");
    return ok;
}
std::vector<std::string> s_customLinks;   // <grand>\custom\<name> junctions we made (removed on leave)
void DropScriptLinks()
{
    for (const auto& l : s_customLinks) if (IsReparse(l)) RemoveDirectoryA(l.c_str());
    s_customLinks.clear();
}
void EnsureScriptLinks(const std::string& root)
{
    DropScriptLinks();
    if (root.empty()) return;
    std::vector<std::string> names;
    WIN32_FIND_DATAA fd;
    const HANDLE h = FindFirstFileA((root + "\\cfg\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::string text;
            if (!ReadText(root + "\\cfg\\" + fd.cFileName, text)) continue;
            for (const auto& n : mh::CustomNamesIn(text))
                if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    if (names.empty()) return;   // not a script hud
    // every depth the hud's #base "…/cfg/x.txt" climbs to (femboy: killfeed 1, the rest 2)
    std::vector<int> climbs;
    for (const char* sub : { "resource", "scripts" })
        WalkFiles(root + "\\" + sub, "", [&](const std::string& r) {
            const std::string low = mh::Lower(r);
            if (low.size() < 4 || (low.compare(low.size() - 4, 4, ".res") && low.compare(low.size() - 4, 4, ".txt"))) return;
            std::string text;
            if (ReadText(root + "\\" + sub + "\\" + r, text)) mh::AddCfgClimbs(std::string(sub) + "/" + r, mh::ResText(text), climbs);
        });
    if (climbs.empty()) climbs.push_back(2);   // tf/custom/<hud> -> tf: what every script hud seen so far uses
    for (const int n : climbs)
    {
        std::string at = root;   // <root> n levels up = where that #base lands
        for (int i = 0; i < n && !at.empty(); ++i)
        {
            const size_t s = at.find_last_of('\\');
            at = s == std::string::npos ? std::string() : at.substr(0, s);
        }
        const size_t cl = strlen(CONFIG_DIR);
        if (at.size() < cl || _strnicmp(at.c_str(), CONFIG_DIR, cl))   // never outside C:\botox-tf2
        {
            DebugLog("multihud: #base climbs %d above %s = outside %s, not linked", n, root.c_str(), CONFIG_DIR);
            continue;
        }
        EnsureJunction(at + "\\cfg", GameDir() + "\\cfg", true);
        CreateDirectoryA((at + "\\custom").c_str(), nullptr);
        for (const auto& nm : names)
            if (EnsureJunction(at + "\\custom\\" + nm, root, false)) s_customLinks.push_back(at + "\\custom\\" + nm);
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
    CfgRun r;
    r.dir = dir;
    RunCfgFile(r, entry, 0);
    DebugLog("multihud: hud cfg -- %d commands run, %d skipped", r.ran, r.skipped);
    return r.ran;
}

// ---- controls the new hud removed ----------------------------------------------------------------
// LoadControlSettings deletes and remakes only the controls its own .res created; a control the
// panel made in C++ (health cross, dashboard top bar parts) or one a nested .res block made inside a
// code-made container keeps whatever the old hud's .res set when the new .res leaves it out. At boot
// such a control is a bare constructor default and draws nothing (a working hud can't rely on it), so
// the boot-equivalent is hidden. Before the switch every live client EditablePanel that loaded a
// .res is snapshotted (handle + the old .res control tree); a few frames after the rebuild, the same
// panel (handle still alive = not remade) diffs old vs new tree and hides each child the old tree
// named and the new one does not, nested blocks included.
struct ResSnap { unsigned long handle; std::string res; mh::ResNode tree; };
std::vector<ResSnap> s_resSnaps;
int s_removalFrames = 0;

// complete-object RTTI of the panel names `td` (".?AVEditablePanel@vgui@@") among its bases
// (multiple inheritance safe)
bool HasRttiBase(void* obj, const char* td)
{
    __try
    {
        void** vt = *static_cast<void***>(obj);
        const uint8_t* col = static_cast<const uint8_t*>(vt[-1]);
        if (*reinterpret_cast<const uint32_t*>(col) != 1) return false;
        const uintptr_t base = reinterpret_cast<uintptr_t>(col) - *reinterpret_cast<const uint32_t*>(col + 20);
        const uint8_t* chd = reinterpret_cast<const uint8_t*>(base + *reinterpret_cast<const uint32_t*>(col + 16));
        const uint32_t n = *reinterpret_cast<const uint32_t*>(chd + 8);
        const uint32_t* arr = reinterpret_cast<const uint32_t*>(base + *reinterpret_cast<const uint32_t*>(chd + 12));
        for (uint32_t i = 0; i < n && i < 64; ++i)
        {
            const uint32_t tdRva = *reinterpret_cast<const uint32_t*>(base + arr[i]);
            if (!strcmp(reinterpret_cast<const char*>(base + tdRva + 16), td)) return true;
        }
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool HasBaseCached(void* obj, const char* td, std::vector<std::pair<void*, bool>>& cache)   // vtable -> answer
{
    void* vt = ReadPtr(obj);
    if (!vt) return false;
    for (const auto& c : cache) if (c.first == vt) return c.second;
    const bool r = HasRttiBase(obj, td);
    if (cache.size() < 4096) cache.emplace_back(vt, r);
    return r;
}
bool IsEditablePanel(void* obj)
{
    static std::vector<std::pair<void*, bool>> cache;
    return HasBaseCached(obj, ".?AVEditablePanel@vgui@@", cache);
}
bool IsVguiPanel(void* obj)
{
    static std::vector<std::pair<void*, bool>> cache;
    return HasBaseCached(obj, ".?AVPanel@vgui@@", cache);
}
// the .res its BuildGroup last loaded, "" = none (nested container, code-only panel)
std::string ResNameOf(void* editablePanel)
{
    void* bg = ReadPtr(static_cast<char*>(editablePanel) + kEditableBuildGroup);
    const char* nm = bg ? static_cast<const char*>(ReadPtr(static_cast<char*>(bg) + kBuildGroupResName)) : nullptr;
    char buf[260];
    if (!nm || !CopyName(nm, buf, sizeof buf)) return {};
    return buf;
}
// read like LoadControlSettings does (custom_mod, then vgui = tf2_misc.vpk), #base merged
bool LoadResTree(const std::string& rel, mh::ResNode& out, int depth)
{
    std::vector<unsigned char> buf;
    bool ok = false;
    for (const char* id : { "custom_mod", "vgui", "GAME" })
        if (ReadGameFile(rel, buf, id)) { ok = true; break; }
    if (!ok) return false;
    const std::string text = mh::ResText(std::string(buf.begin(), buf.end()));
    std::vector<std::string> inc;
    out = mh::ParseRes(text, &inc);
    if (depth < 8)
        for (const auto& i : inc)
        {
            mh::ResNode b;
            if (LoadResTree(mh::ResIncludePath(rel, i), b, depth + 1)) mh::ResMerge(out, b);
        }
    return true;
}
template <class Fn> void ForEachVPanel(void* pn, uintptr_t root, Fn&& fn)
{
    std::vector<std::pair<uintptr_t, int>> stack{ { root, 0 } };
    int visited = 0;
    while (!stack.empty() && visited < 60000)
    {
        const auto [vp, depth] = stack.back();
        stack.pop_back();
        ++visited;
        fn(vp);
        if (depth >= 48) continue;
        const int n = VCall<int>(pn, kPnlGetChildCount, vp);
        for (int i = 0; i < n && i < 4096; ++i)
            if (const uintptr_t c = VCall<uintptr_t>(pn, kPnlGetChild, vp, i)) stack.push_back({ c, depth + 1 });
    }
}
void SnapshotRes()
{
    s_resSnaps.clear();
    s_removalFrames = 0;
    void* pn = VGui_GetPanelInterface();
    void* s = Surface();
    void* iv = Ivgui();
    const uintptr_t root = s ? VCall<uintptr_t>(s, kSurfGetEmbeddedPanel) : 0;
    if (!pn || !iv || !root) return;
    std::vector<std::pair<std::string, mh::ResNode>> cache;
    ForEachVPanel(pn, root, [&](uintptr_t vp) {
        void* p = VCall<void*>(pn, kPnlGetPanel, vp, "ClientDLL");
        if (!p || !IsEditablePanel(p)) return;
        std::string res = ResNameOf(p);
        if (res.empty()) return;
        res = mh::NormalizePath(res);
        auto it = std::find_if(cache.begin(), cache.end(), [&](const auto& c) { return c.first == res; });
        if (it == cache.end())
        {
            mh::ResNode t;
            if (!LoadResTree(res, t, 0)) return;
            cache.emplace_back(res, std::move(t));
            it = cache.end() - 1;
        }
        s_resSnaps.push_back({ VCall<unsigned long>(iv, kVguiPanelToHandle, vp), res, it->second });
    });
    DebugLog("multihud: snapshot %zu panels with a .res (%zu files)", s_resSnaps.size(), cache.size());
}
void HideRemoved(void* pn, uintptr_t vp, const mh::ResNode& o, const mh::ResNode& n, int depth, int& hidden)
{
    const int count = VCall<int>(pn, kPnlGetChildCount, vp);
    for (int i = 0; i < count && i < 4096; ++i)
    {
        const uintptr_t c = VCall<uintptr_t>(pn, kPnlGetChild, vp, i);
        const char* nm = c ? VCall<const char*>(pn, kPnlGetName, c) : nullptr;
        char buf[128];
        if (!nm || !CopyName(nm, buf, sizeof buf) || !buf[0]) continue;
        const std::string name = mh::Lower(buf);
        const mh::ResNode* oc = mh::ResChild(o, name);
        if (!oc) continue;   // the old hud never set it: not ours to touch (runtime rows, code panels)
        const mh::ResNode* nc = mh::ResChild(n, name);
        if (!nc)
        {
            VCall<void>(pn, kPnlSetVisible, c, false);
            VCall<void>(pn, kPnlSetSize, c, 0, 0);
            if (hidden < 40) DebugLog("multihud: hid '%s' (old hud's control, not in the new .res)", buf);
            ++hidden;
            continue;
        }
        if (depth < 8 && !oc->kids.empty()) HideRemoved(pn, c, *oc, *nc, depth + 1, hidden);
    }
}
void ApplyRemovals()
{
    void* pn = VGui_GetPanelInterface();
    void* iv = Ivgui();
    if (!pn || !iv || s_resSnaps.empty()) { s_resSnaps.clear(); return; }
    std::vector<std::pair<std::string, mh::ResNode>> cache;
    int hidden = 0, alive = 0;
    for (const auto& snap : s_resSnaps)
    {
        const uintptr_t vp = VCall<uintptr_t>(iv, kVguiHandleToPanel, snap.handle);
        if (!vp) continue;   // remade by the rebuild = already boot-fresh
        ++alive;
        auto it = std::find_if(cache.begin(), cache.end(), [&](const auto& c) { return c.first == snap.res; });
        if (it == cache.end())
        {
            mh::ResNode t;
            LoadResTree(snap.res, t, 0);   // missing = every old control is gone
            cache.emplace_back(snap.res, std::move(t));
            it = cache.end() - 1;
        }
        HideRemoved(pn, vp, snap.tree, it->second, 0, hidden);
    }
    DebugLog("multihud: removal pass -- %d of %zu snapshotted panels still live, %d controls hidden", alive,
             s_resSnaps.size(), hidden);
    s_resSnaps.clear();
}

// ---- keys the new hud leaves out ------------------------------------------------------------------
// Panel::ApplySettings only touches what the .res names: absent xpos / ypos / wide / tall / zpos keep
// the current value (Panel.cpp:4410, 4519), absent CPanelAnimationVars (killfeed TextFont, colors,
// CornerRadius...) too (InternalApplySettings, 6348). At boot "current" = the constructor's, so a
// code-made panel (hud elements, their children) kept the OLD hud's values. Before the reapply:
//  - anim vars: re-arm NEEDS_DEFAULT_SETTINGS_APPLIED, the bit only the ctor sets (Panel.cpp:672);
//    the next ApplySettings (4345) / PerformApplySchemeSettings (4179) resets every anim var to its
//    declared default first, exactly as at boot, then the new .res keys go on top.
//  - geometry the old .res set and the new one doesn't: Panel::Init's defaults (0, 0, 64x24, z 0).
//    Code that sizes in ApplySchemeSettings (crosshair = screen size) still runs after, as at boot.
//    ponytail: a ctor that SetSize/SetPos's its own panel gets Init's values instead, read the ctor if
//    one shows up wrong.
// Panel::_flags = CUtlFlags<unsigned short> at +0x80 (client x64 11-15: ApplySettings 93f187 tests bit 9
// there before InternalInitDefaultValues; the Set*Enabled setters 94ceb0.. mask the other PanelFlags_t
// bits 0x400 0x40 0x8 0x4 0x10 0x20 0x2000 at the same offset).
constexpr size_t kPanelFlags = 0x80;
constexpr unsigned short kNeedsDefaultSettings = 0x200;
constexpr int kPnlSetPos = 2, kPnlGetPos = 3, kPnlGetSize = 5;
constexpr int kInitWide = 64, kInitTall = 24;   // Panel::Panel -> Init(0, 0, 64, 24), Panel.cpp:619

bool ArmDefaultsRaw(void* p)
{
    __try { *reinterpret_cast<unsigned short*>(static_cast<char*>(p) + kPanelFlags) |= kNeedsDefaultSettings; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
struct StaleCount { int armed = 0, geo = 0; };
// oc = the old hud's block for this panel (null = it never named it), nc = the new hud's
void ResetOne(void* pn, uintptr_t vp, const mh::ResNode* oc, const mh::ResNode& nc, const char* name, StaleCount& cnt)
{
    void* p = VCall<void*>(pn, kPnlGetPanel, vp, "ClientDLL");   // client.dll panels only (+0x80 read off client)
    if (p && IsVguiPanel(p) && ArmDefaultsRaw(p)) ++cnt.armed;
    const unsigned stale = oc ? oc->geo & ~nc.geo : 0;
    if (!stale) return;
    if (stale & (mh::kGeoX | mh::kGeoY))
    {
        int x = 0, y = 0;
        VCall<void>(pn, kPnlGetPos, vp, &x, &y);
        if (stale & mh::kGeoX) x = 0;
        if (stale & mh::kGeoY) y = 0;
        VCall<void>(pn, kPnlSetPos, vp, x, y);
    }
    if (stale & (mh::kGeoW | mh::kGeoT))
    {
        int w = 0, t = 0;
        VCall<void>(pn, kPnlGetSize, vp, &w, &t);
        if (stale & mh::kGeoW) w = kInitWide;
        if (stale & mh::kGeoT) t = kInitTall;
        VCall<void>(pn, kPnlSetSize, vp, w, t);
    }
    if (stale & mh::kGeoZ) VCall<void>(pn, kPnlSetZPos, vp, 0);
    if (cnt.geo < 40)
        DebugLog("multihud: '%s'%s%s%s%s%s back to the panel default (old hud set it, the new .res doesn't)", name,
                 stale & mh::kGeoX ? " xpos" : "", stale & mh::kGeoY ? " ypos" : "", stale & mh::kGeoW ? " wide" : "",
                 stale & mh::kGeoT ? " tall" : "", stale & mh::kGeoZ ? " zpos" : "");
    ++cnt.geo;
}
void ResetBranch(void* pn, uintptr_t vp, const mh::ResNode& o, const mh::ResNode& n, int depth, StaleCount& cnt)
{
    static const mh::ResNode kNone;
    const int count = VCall<int>(pn, kPnlGetChildCount, vp);
    for (int i = 0; i < count && i < 4096; ++i)
    {
        const uintptr_t c = VCall<uintptr_t>(pn, kPnlGetChild, vp, i);
        const char* nm = c ? VCall<const char*>(pn, kPnlGetName, c) : nullptr;
        char buf[128];
        if (!nm || !CopyName(nm, buf, sizeof buf) || !buf[0]) continue;
        const std::string name = mh::Lower(buf);
        const mh::ResNode* nc = mh::ResChild(n, name);
        if (!nc) continue;   // not the new hud's to set (removed ones: ApplyRemovals)
        const mh::ResNode* oc = mh::ResChild(o, name);
        ResetOne(pn, c, oc, *nc, buf, cnt);
        if (depth < 8 && !nc->kids.empty()) ResetBranch(pn, c, oc ? *oc : kNone, *nc, depth + 1, cnt);
    }
}
// main thread, new mounts + script links + hud cfg in place, right before the reapply
void ResetStale()
{
    void* pn = VGui_GetPanelInterface();
    void* iv = Ivgui();
    if (!pn || !iv || s_resSnaps.empty()) return;
    std::vector<std::pair<std::string, mh::ResNode>> cache;
    StaleCount cnt;
    int alive = 0;
    for (const auto& snap : s_resSnaps)
    {
        const uintptr_t vp = VCall<uintptr_t>(iv, kVguiHandleToPanel, snap.handle);
        if (!vp) continue;
        ++alive;
        auto it = std::find_if(cache.begin(), cache.end(), [&](const auto& c) { return c.first == snap.res; });
        if (it == cache.end())
        {
            mh::ResNode t;
            LoadResTree(snap.res, t, 0);   // missing = names nothing = nothing to reset
            cache.emplace_back(snap.res, std::move(t));
            it = cache.end() - 1;
        }
        // a panel's own block sits in its own .res under its own name (HudPlayerHealth in hudplayerhealth.res)
        const char* nm = VCall<const char*>(pn, kPnlGetName, vp);
        char buf[128];
        if (nm && CopyName(nm, buf, sizeof buf) && buf[0])
            if (const mh::ResNode* own = mh::ResChild(it->second, mh::Lower(buf)))
                ResetOne(pn, vp, mh::ResChild(snap.tree, mh::Lower(buf)), *own, buf, cnt);
        ResetBranch(pn, vp, snap.tree, it->second, 0, cnt);
    }
    DebugLog("multihud: stale pass -- %d live panels, %d armed for default anim vars, %d geometry resets", alive,
             cnt.armed, cnt.geo);
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
// "OnScreenSizeChanged" posted, in this queue order (vgui dispatches FIFO, targets kept as handles,
// so a panel deleted before its turn is skipped):
//  1. the viewport: CBaseViewport::OnScreenSizeChanged (baseviewport.cpp:204) deletes + remakes its
//     panels = boot-fresh objects. The main menu override has to be remade, not reapplied: its
//     ApplySchemeSettings only sets m_pVRModeButton when the .res has VRModeButton
//     (tf_hud_mainmenuoverride.cpp:553), so a hud without one left it pointing at the button the res
//     reload just deleted -> OnThink IsVisible() on freed memory (crash 11-15, client+0x559322). A
//     fresh object starts at NULL, like at boot, and has no other hud's code-made panels either.
//  2. every other branch (GameUI, popups): Panel::OnScreenSizeChanged (Panel.cpp:1003) = repost to
//     the children + scheme update + layout, nothing deleted.
//  3. the panels the matchmaking dashboard parent manager hung on the old main menu (autodelete off:
//     top bar, mode selection, notifications): step 1 orphans them before any parent could forward
//     the message, so they get it directly; CTFMatchmakingDashboard::ApplySchemeSettings then calls
//     UpdateParenting (tf_matchmaking_dashboard.cpp:306) and they move onto the new main menu.
// -1 = could not (no KeyValues without the aim half, interface missing): caller takes the surface path.
int RebuildPanels(int w, int h)
{
    void* pn = VGui_GetPanelInterface();
    void* s = Surface();
    if (!pn || !s || !Ivgui()) return -1;
    const uintptr_t root = VCall<uintptr_t>(s, kSurfGetEmbeddedPanel);
    const uintptr_t vp = root ? FindPanel(pn, root, "CBaseViewport", 0) : 0;
    if (!vp) { DebugLog("multihud: CBaseViewport not in the vgui tree"); return -1; }
    std::vector<uintptr_t> foreign;
    if (const uintptr_t mm = FindPanel(pn, root, "MainMenuOverride", 0))
    {
        const int n = VCall<int>(pn, kPnlGetChildCount, mm);
        for (int i = 0; i < n && i < 4096; ++i)
            if (const uintptr_t c = VCall<uintptr_t>(pn, kPnlGetChild, mm, i))
                if (!VCall<bool>(pn, kPnlIsAutoDeleteSet, c)) foreign.push_back(c);
    }
    if (!PostSizeChanged(vp, w, h)) return -1;
    std::vector<uintptr_t> chain;   // the viewport and its parents: walked into, never posted to
    for (uintptr_t p = vp; p && chain.size() < 64; p = VCall<uintptr_t>(pn, kPnlGetParent, p)) chain.push_back(p);
    auto inChain = [&](uintptr_t p) { return std::find(chain.begin(), chain.end(), p) != chain.end(); };
    int posted = 1;
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
            if (!PostSizeChanged(c, w, h)) return posted;
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
    for (const uintptr_t f : foreign)
        if (PostSizeChanged(f, w, h)) ++posted;
    DebugLog("multihud: rebuild posted to %d panels (%zu dashboard panels off the main menu)", posted, foreign.size());
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
bool ReadGameFile(const std::string& name, std::vector<unsigned char>& out, const char* pathId)
{
    void* fs = Fs();
    if (!fs) return false;
    void* bfs = static_cast<char*>(fs) + 8;
    void* fh = VCall<void*>(bfs, kBfsOpen, name.c_str(), "rb", pathId);
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
    std::vector<std::string> tex;
    const std::vector<std::string> matNames = HudMaterialNames(dirs, &tex);
    const int mats = RefreshMaterials(matNames, tex);
    const int rebound = RebindVguiTextures(matNames);   // before the reapply: kill icon uvs use the size
    const int vbf = RefreshBitmapFonts(dirs);  // metrics for the atlas pages the material pass reread
    const unsigned long msMats = lap();
    const int strings = ReloadStrings(dirs);   // before the reapply: labels resolve #tokens on apply
    const bool captions = ReloadCaptions(dirs);
    const unsigned long msStrings = lap();
    RestoreCvars();                            // the hud being left
    EnsureScriptLinks(root);                   // script huds: cfg/ + custom/<hud> junctions before anything logs or loads
    const int cfg = RunHudCfg(root);           // the hud being entered (cl_hud_minmode etc. shape the layout)
    const unsigned long msCfg = lap();
    const bool snd = std::any_of(dirs.begin(), dirs.end(), [](const std::string& d) { return Exists(d + "\\sound", true); });
    if (snd) Exec("snd_restart");              // cached hud sounds (hitsound, killsound)
    const unsigned long msSound = lap();
    ResetStale();                              // keys the new hud leaves out: back to the panel defaults
    Exec("hud_reloadscheme");                  // viewport + hud elements on the new scheme + cvars
    int w = 0, h = 0, posted = -1;
    if (void* s = Surface()) VCall<void>(s, kSurfGetScreenSize, &w, &h);
    if (w > 0 && h > 0) posted = RebuildPanels(w, h);
    if (posted < 0)
    {
        DebugLog("multihud: no per-branch reapply (aim half off?), resolution change path instead");
        if (void* s = Surface(); s && w > 0 && h > 0) VCall<void>(s, kSurfOnScreenSizeChanged, w, h);
    }
    VGui_ResetPanelCache();
    // the posted messages dispatch in this frame's vgui RunFrame; diff once they all have
    s_removalFrames = s_resSnaps.empty() ? 0 : 3;
    const unsigned long msPanels = lap();
    DebugLog("multihud: %s -- %d schemes, %d materials (%d vgui ids rebound), %d bitmap fonts, %d string files%s, "
             "%d cfg commands%s, %d branches | ms: flush %lu schemes %lu materials %lu strings %lu cfg %lu sound %lu "
             "panels %lu", label.c_str(), schemes, mats, rebound, vbf, strings, captions ? ", captions" : "", cfg,
             snd ? ", sounds" : "", posted, msFlush, msSchemes, msMats, msStrings, msCfg, msSound, msPanels);
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
    std::vector<std::string> customHuds, mods;
    {
        const std::string c = CustomDir();
        for (const auto& s : SubDirs(c))
        {
            const std::string full = c + "\\" + s;
            const bool hud = mh::IsHudDir(full, Exists);
            if (hud && customOff && s_mount.customOff.empty()) customHuds.push_back(full);
            if (!hud) mods.push_back(full);   // SubDirs is alphabetical = the boot mount order
        }
    }
    const auto ops = mh::PlanMount(s_mount, root, customOff, customHuds, mods);
    // up before the first mount changes: an eject that lands during the rebuild waits for it, and
    // then for the restore its FrameStageNotify runs (thread.cpp)
    if (!ops.empty()) s_holding = true;
    if (!ops.empty()) SnapshotRes();   // the old hud's .res trees, read through the mounts still live
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
        // folders only: a .vpk search path owns its open pack (async sound reads go through it),
        // re-adding one mid game is not safe, so a hud file can still beat a .vpk mod's
        case mh::Op::RaiseMod:
            if (FsRemove(o.path)) { FsAdd(o.path); DebugLog("multihud: tf/custom mod %s kept above the hud", o.path.c_str()); }
            break;
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
    if (s_removalFrames > 0 && --s_removalFrames == 0) ApplyRemovals();
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
    auto list = SubDirs(HudsDir());
    // a hud one folder deep puts the script hud junctions (cfg, custom) into huds\ itself
    list.erase(std::remove_if(list.begin(), list.end(), [](const std::string& n) {
        return !_stricmp(n.c_str(), "cfg") || !_stricmp(n.c_str(), "custom"); }), list.end());
    return list;
}

const char* MultiHud_Status() { return s_status; }
