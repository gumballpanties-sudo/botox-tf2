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
#include <unordered_map>

void Hats_HudRemade();   // hats.cpp: forget the CTFHudPlayerClass it held
bool Hats_IsOurs(int entIdx);   // hats.cpp: our own hat entities (prop diag skips them)

// multihud: pick a hud folder, the game swaps to it live. Every step is the game's own reload path:
//  1. search paths: our hud at the HEAD of GAME / mod / custom_mod (gameinfo's tf/custom/* ids:
//     BuildGroup::LoadControlSettings reads .res from custom_mod then vgui = tf2_misc.vpk,
//     BuildGroup.cpp:923), tf/custom huds unmounted, so what the pick lacks = the default file.
//  2. vgui_cache_res_files 0 during the switch (back a few frames after, SetResCache): the .res cache is
//     a static of vgui_controls, so client, GameUI, ServerBrowser, engine and vguimatsurface each hold
//     one (BuildGroup.cpp:61), and only client's is ever cleared (hud_reloadscheme). One shared cvar.
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
constexpr int kPnlSetSize = 4, kPnlSetZPos = 8, kPnlSetVisible = 14, kPnlIsVisible = 15, kPnlGetPanel = 55;
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
    // Download locks the material system itself (one sync with the render thread per texture).
    // A texture loaded while its file was missing is cached as the error texture under its name and
    // every material asking for it gets that one: Download rereads it from the file there is now.
    int errFixed = 0, errStill = 0;
    for (const auto& t : tex)
    {
        if (!VCall<bool>(g_matSystem, kMsIsTextureLoaded, t.c_str())) continue;
        if (!GameFileExists("materials/" + t + ".vtf")) { ++gone; continue; }
        void* tx = VCall<void*>(g_matSystem, kMsFindTexture, t.c_str(), "VGUI", false, 0);
        if (!tx) continue;
        const bool wasError = VCall<bool>(tx, kTexIsError);
        VCall<void>(tx, kTexDownload, (void*)nullptr, 0);
        ++n;
        if (!wasError) continue;
        if (VCall<bool>(tx, kTexIsError)) { if (errStill++ < 10) DebugLog("multihud: texture %s still the error texture after a reread", t.c_str()); }
        else ++errFixed;
    }
    if (errFixed || errStill) DebugLog("multihud: %d error textures reread from their file, %d still error", errFixed, errStill);
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
// Off for one switch: from Reload until the posted rebuild has dispatched (MultiHud_MainTick, 3 frames),
// then the user's value back. Off for as long as a pick was live (round 3) = every LoadControlSettings
// read + parsed its .res and #base chain from disk mid-game (freeze panel, spawn meters, quest
// trackers...): the lag after a switch (11-15). The client's cache is flushed by hud_reloadscheme and
// refills from the new hud; the other modules' caches keep their boot entries.
// ponytail: GameUI / ServerBrowser dialogs hidden during the switch apply that boot .res when next shown
// (same ceiling as STATUS "GameUI dialogs ... not covered"); clear their BuildGroup::m_dictCachedResFiles
// if a hud ever needs it.
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

// ---- vgui texture ids stuck on the error material --------------------------------------------------
// vguimatsurface binds a texture id once per name (CTextureDictionary::BindTextureToFile, x64 14370:
// CRC of the name as asked, case kept, entry +0x10; material +0x18; a name with no file still binds,
// to the error material, and keeps that CRC). vgui2 caches each hud image's Bitmap + id for the
// session, so an image whose file was missing once (mid switch, a hud without it) stayed magenta
// checkers through every later switch. The name is gone, the CRC is not: every quoted value of every
// hud .res / .txt seen this session is a candidate ("x" and "vgui/x", as Bitmap asks), and an id on
// the error material whose CRC matches one with a file now is bound again (white, then the name:
// the bind only runs on a NEW name).
// Dictionary (x64 11-15): CMatSystemSurface vt 28 DrawGetTextureId = 40 53 48 83 EC 20 48 8B DA E8
// <TextureDictionary()>; entries at dict+8, 0x58 each, id = index, list head u16 dict+0x18, next u16
// entry+0x52 (FindTextureIdForTextureFile 14850).
constexpr int kSurfDrawGetTextureId = 28;
constexpr size_t kTdEntries = 8, kTdHead = 0x18, kTdEntry = 0x58, kTdCrc = 0x10, kTdMat = 0x18, kTdNext = 0x52;
std::unordered_map<uint32_t, std::string> s_vguiNames;   // CRC -> name, grows with every hud seen
int s_repairFrames = 0;

void* TextureDict()
{
    static void* d = nullptr;
    static bool tried = false;
    if (tried) return d;
    tried = true;
    void* s = Surface();
    if (!s) return nullptr;
    __try
    {
        const uint8_t* fn = static_cast<const uint8_t*>((*static_cast<void***>(s))[kSurfDrawGetTextureId]);
        static const uint8_t k[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0xE8 };
        if (memcmp(fn, k, sizeof k)) return nullptr;
        d = reinterpret_cast<void* (*)()>(fn + 14 + *reinterpret_cast<const int32_t*>(fn + 10))();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { d = nullptr; }
    return d;
}
struct VgId { int id; uint32_t crc; void* mat; };
int ReadVguiIds(void* dict, VgId* out, int cap)
{
    __try
    {
        const uint8_t* base = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(dict) + kTdEntries);
        int idx = *reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(dict) + kTdHead), n = 0;
        for (int guard = 0; base && idx != 0xFFFF && guard < 65536 && n < cap; ++guard)
        {
            const uint8_t* e = base + (size_t)idx * kTdEntry;
            out[n++] = { idx, *reinterpret_cast<const uint32_t*>(e + kTdCrc), *reinterpret_cast<void* const*>(e + kTdMat) };
            idx = *reinterpret_cast<const uint16_t*>(e + kTdNext);
        }
        return n;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
void* VguiIdMaterial(void* dict, int id)
{
    __try
    {
        const uint8_t* base = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(dict) + kTdEntries);
        return base ? *reinterpret_cast<void* const*>(base + (size_t)id * kTdEntry + kTdMat) : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
std::vector<std::string> s_vguiLearned;   // hud dirs already read (lower case)
void LearnVguiNames(const std::vector<std::string>& dirs)
{
    for (const auto& d : dirs)
    {
        const std::string key = mh::Lower(d);
        if (std::find(s_vguiLearned.begin(), s_vguiLearned.end(), key) != s_vguiLearned.end()) continue;
        s_vguiLearned.push_back(key);
        for (const char* sub : { "resource", "scripts" })
            WalkFiles(d + "\\" + sub, "", [&](const std::string& r) {
                const std::string low = mh::Lower(r);
                if (low.size() < 4 || (low.compare(low.size() - 4, 4, ".res") && low.compare(low.size() - 4, 4, ".txt"))) return;
                std::string text;
                if (!ReadText(d + "\\" + sub + "\\" + r, text)) return;
                for (const auto& t : mh::QuotedTokens(mh::ResText(text)))
                {
                    if (t.find_first_of("/\\") == std::string::npos && t.size() < 3) continue;
                    s_vguiNames.emplace(mh::Crc32(t), t);
                    s_vguiNames.emplace(mh::Crc32("vgui/" + t), "vgui/" + t);
                }
            });
    }
}
// "when" names the log line; returns ids still on the error material
int RepairVguiIds(const char* when)
{
    void* dict = TextureDict();
    void* s = Surface();
    if (!dict || !s) { DebugLog("multihud: vgui texture dictionary not found (%s), stuck images not checked", when); return -1; }
    static VgId ids[65536];
    const int n = ReadVguiIds(dict, ids, 65536);
    if (n < 0) { DebugLog("multihud: vgui texture dictionary unreadable (%s)", when); return -1; }
    int err = 0, fixed = 0, noFile = 0, unknown = 0;
    for (int i = 0; i < n; ++i)
    {
        if (!ids[i].mat || !VCall<bool>(ids[i].mat, kMatIsError)) continue;
        ++err;
        const auto it = s_vguiNames.find(ids[i].crc);
        if (it == s_vguiNames.end()) { ++unknown; continue; }
        if (!GameFileExists("materials/" + mh::NormalizePath(it->second) + ".vmt")) { ++noFile; continue; }
        VCall<void>(s, kSurfDrawSetTextureFile, ids[i].id, "vgui/white", 0, false);
        VCall<void>(s, kSurfDrawSetTextureFile, ids[i].id, it->second.c_str(), 0, false);
        void* now = VguiIdMaterial(dict, ids[i].id);
        const bool ok = now && !VCall<bool>(now, kMatIsError);
        fixed += ok;
        if (fixed <= 20 || !ok)
            DebugLog("multihud: vgui id %d (%s) was on the error material, bound again: %s", ids[i].id, it->second.c_str(),
                     ok ? "ok" : "STILL error");
    }
    if (err)
        DebugLog("multihud: %s -- %d of %d vgui ids on the error material: %d bound again, %d no file on the search path "
                 "(error at boot too), %d name never seen", when, err, n, fixed, noFile, unknown);
    return err - fixed;
}

// ---- hud elements: remade like boot ----------------------------------------------------------
// The reapply (hud_reloadscheme) only re-runs ApplySettings on panels that already exist, and a .res
// key the new hud leaves out keeps whatever the old hud set: position, colors, border, font, text,
// image, code-made children. Viewport panels are remade by RebuildPanels; the hud elements (every
// DECLARE_HUDELEMENT: health, ammo, killfeed, chat ...) are made once, by CHud::Init at client start.
// So they are made again here the same way and the old ones deleted = the objects a game started
// with this hud has.
//  1. detach: gHUD.m_HudList count -> 0. The old elements stay alive until step 4, so no address is
//     reused and every pointer maps one way; their dtor's RemoveHudElement then finds nothing.
//  2. CHudElementHelper::CreateAllElements (hud_element_helper.cpp:72) + Init on each new one in list
//     order (CHud::Init, hud.cpp:411). Usermessage hooks are blocked meanwhile: the boot ones are
//     still registered and dispatch through gHUD.FindElement (hud_macros.h:24), a second
//     registration would run every handler twice.
//  3. every pointer in client.dll's .data to an old element (any non-virtual base) or to one of its
//     panels (matched by path + class) is re-pointed: ClientModeTFNormal caches 13 of them
//     (clientmode_tf.cpp:403..433, function static = .data), ClientModeShared 3, tf_hud_statpanel one.
//     No counterpart = nulled once the old one is deleted.
//  4. render group lockers cleared (old element pointers, hud.cpp:534), old elements deleted tail
//     first (CHud::Shutdown, hud.cpp:507), then any .data word an old dtor nulled that held a NEW
//     pointer is put back (dtor "g_pX = NULL" after the new ctor set it).
//  5. in a game: CHud::VidInit + CHud::LevelInit, what a map load runs on the elements.
// client x64 11-15: gHUD +0x20 m_HudList memory, +0x30 count; element vt 0 scalar deleting dtor,
// 2 Init, 8 GetName (CHud::Init 28cc10, FindElement 28c8f0); gHUD +0xa0 render group map nodes
// (0x18 each, group ptr +0x10), +0xb2 count, group +0x18 locker count (CHud::LevelInit 28d3d0).
// Kill switch: file C:\botox-tf2\nohudremake = the old in-place reapply only.
constexpr size_t kHudListMem = 0x20, kHudListCount = 0x30, kHudGroups = 0xa0, kHudGroupCount = 0xb2;
constexpr size_t kGroupNode = 0x18, kGroupNodeValue = 0x10, kGroupLockers = 0x18;
constexpr int kElemDtor = 0, kElemInit = 2, kElemLevelShutdown = 5, kElemGetName = 8, kEngIsInGame = 26;

struct HudFns
{
    uint8_t* ghud = nullptr;
    void (*create)() = nullptr;
    void (*vidInit)(void*) = nullptr;
    void (*levelInit)(void*) = nullptr;
    void (*add)(void*, uintptr_t) = nullptr;   // CHud::AddHudElement (the call in CreateAllElements)
    bool tried = false, ok = false;
};
HudFns s_hf;

using HookMsg_t = void(__fastcall*)(void*, const char*, void*);
HookMsg_t o_HookMsg = nullptr;
bool s_blockMsgHooks = false;   // main thread only (the only thread that hooks messages)
int  s_blockedMsgHooks = 0;
void __fastcall h_HookMsg(void* um, const char* name, void* fn)
{
    if (s_blockMsgHooks) { ++s_blockedMsgHooks; return; }
    o_HookMsg(um, name, fn);
}
// ISurface 87 SolveTraverse (vguimatsurface x64 134c0). SETUP_PANEL in a hud element's ctor runs it =
// ApplySchemeSettings + Think + PerformLayout on a half-built object. At boot there is no game and
// those return early; mid-game CHudArenaClassLayout::PerformLayout (tf_hud_arena_class_layout.cpp:80
// -> 165) read m_ClassImages[0] before the ctor made it (crash 11-15 17:52, client+0x9451B1). Skipped
// while elements are built: hud_reloadscheme right after applies + lays out every one, built.
using Solve_t = void(__fastcall*)(void*, uintptr_t, bool);
Solve_t o_Solve = nullptr;
bool s_quietSolve = false;      // main thread
int  s_skippedSolves = 0;
void __fastcall h_Solve(void* surf, uintptr_t panel, bool force)
{
    if (s_quietSolve) { ++s_skippedSolves; return; }
    o_Solve(surf, panel, force);
}
constexpr int kSurfSolveTraverse = 87;

bool ResolveHud()
{
    if (s_hf.tried) return s_hf.ok;
    s_hf.tried = true;
    const HMODULE c = GetModuleHandleA("client.dll");
    if (!c) return false;
    uint8_t* cr = PatternScan(c, "40 53 48 83 EC 20 48 8B 1D ? ? ? ? 48 85 DB 74 ? 48 8B 43 08 FF D0 48 85 C0 74 ? 48 8B D0 48 8D 0D ? ? ? ? E8");
    uint8_t* li = PatternScan(c, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 20 45 33 F6 48 8B F1 "
                                 "41 8B DE 39 59 30 7E ? 0F 1F 84 00 00 00 00 00 48 8B 46 20 8B D3 48 8B 0C D0 48 8B 01 FF 50 20");
    uint8_t* vi = PatternScan(c, "48 89 5C 24 08 57 48 83 EC 20 33 DB 48 8B F9 39 59 30 7E ? 0F 1F 40 00 0F 1F 84 00 00 00 00 00 "
                                 "48 8B 47 20 48 8B 0C D8 48 8B 01 FF 50 18");
    uint8_t* hm = PatternScan(c, "40 53 55 41 57 48 83 EC 20 4D 8B F8 48 8B DA 48 8B E9 E8 ? ? ? ? 83 F8 FF 75 ? 48 8B D3 48 8D 0D");
    if (!cr || !li || !vi || !hm)
    {
        DebugLog("multihud: hud element remake OFF, pattern missing (create %p levelinit %p vidinit %p hookmsg %p)",
                 (void*)cr, (void*)li, (void*)vi, (void*)hm);
        return false;
    }
    // AddHudElement's this inside CreateAllElements: lea rcx, [gHUD] at +0x20
    s_hf.ghud = cr + 0x20 + 7 + *reinterpret_cast<int32_t*>(cr + 0x20 + 3);
    s_hf.create = reinterpret_cast<void (*)()>(cr);
    s_hf.levelInit = reinterpret_cast<void (*)(void*)>(li);
    s_hf.vidInit = reinterpret_cast<void (*)(void*)>(vi);
    // ... lea rcx, [gHUD] (+0x20) ; call CHud::AddHudElement (+0x27)
    s_hf.add = reinterpret_cast<void (*)(void*, uintptr_t)>(cr + 0x27 + 5 + *reinterpret_cast<int32_t*>(cr + 0x28));
    if (!InstallHookAt(hm, "multihud: CUserMessages::HookMessage", (void*)&h_HookMsg, (void**)&o_HookMsg)) return false;
    if (!InstallVTableHook(Surface(), kSurfSolveTraverse, "multihud: ISurface::SolveTraverse", (void*)&h_Solve, (void**)&o_Solve))
        return false;
    DebugLog("multihud: hud remake ready (gHUD %p)", (void*)s_hf.ghud);
    s_hf.ok = true;
    return true;
}

// complete object + its non-virtual base subobject offsets, from the x64 RTTI complete object locator
struct RttiInfo { uintptr_t complete = 0, vt = 0; int n = 0; int mdisp[32] = {}; };
bool ReadRtti(void* obj, RttiInfo& out)
{
    __try
    {
        void** vt = *static_cast<void***>(obj);
        const uint8_t* col = static_cast<const uint8_t*>(vt[-1]);
        if (*reinterpret_cast<const uint32_t*>(col) != 1) return false;
        const uint32_t off = *reinterpret_cast<const uint32_t*>(col + 4);
        const uintptr_t base = reinterpret_cast<uintptr_t>(col) - *reinterpret_cast<const uint32_t*>(col + 20);
        const uint8_t* chd = reinterpret_cast<const uint8_t*>(base + *reinterpret_cast<const uint32_t*>(col + 16));
        const uint32_t nb = *reinterpret_cast<const uint32_t*>(chd + 8);
        const uint32_t* arr = reinterpret_cast<const uint32_t*>(base + *reinterpret_cast<const uint32_t*>(chd + 12));
        out.complete = reinterpret_cast<uintptr_t>(obj) - off;
        out.vt = *reinterpret_cast<const uintptr_t*>(out.complete);
        out.n = 0;
        for (uint32_t i = 0; i < nb && i < 64 && out.n < 32; ++i)
        {
            const uint8_t* bcd = reinterpret_cast<const uint8_t*>(base + arr[i]);
            const int mdisp = *reinterpret_cast<const int*>(bcd + 8), pdisp = *reinterpret_cast<const int*>(bcd + 12);
            if (pdisp != -1) continue;   // in a virtual base: no fixed offset
            bool dup = false;
            for (int k = 0; k < out.n; ++k) dup |= out.mdisp[k] == mdisp;
            if (!dup) out.mdisp[out.n++] = mdisp;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
int ReadHudList(uint8_t* ghud, uintptr_t* out, int cap)
{
    __try
    {
        const int n = *reinterpret_cast<int*>(ghud + kHudListCount);
        uintptr_t* mem = *reinterpret_cast<uintptr_t**>(ghud + kHudListMem);
        if (n < 0 || n > cap || (n && !mem)) return -1;
        for (int i = 0; i < n; ++i) out[i] = mem[i];
        return n;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
bool SetHudListCount(uint8_t* ghud, int n)
{
    __try { *reinterpret_cast<int*>(ghud + kHudListCount) = n; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// the old list back after a failed make: the new ones were appended over it from index 0
constexpr size_t kHudListAlloc = 0x28;
bool RestoreHudList(uint8_t* ghud, const uintptr_t* list, int n)
{
    __try
    {
        uintptr_t* mem = *reinterpret_cast<uintptr_t**>(ghud + kHudListMem);
        if (!mem || *reinterpret_cast<int*>(ghud + kHudListAlloc) < n) return false;
        for (int i = 0; i < n; ++i) mem[i] = list[i];
        *reinterpret_cast<int*>(ghud + kHudListCount) = n;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
int ClearLockers(uint8_t* ghud)
{
    __try
    {
        uint8_t* nodes = *reinterpret_cast<uint8_t**>(ghud + kHudGroups);
        const int n = *reinterpret_cast<uint16_t*>(ghud + kHudGroupCount);
        if (!nodes || n > 1024) return -1;
        int cleared = 0;
        for (int i = 0; i < n; ++i)
        {
            uint8_t* g = *reinterpret_cast<uint8_t**>(nodes + (size_t)i * kGroupNode + kGroupNodeValue);
            if (!g) continue;
            int* cnt = reinterpret_cast<int*>(g + kGroupLockers);
            if (*cnt > 0) { cleared += *cnt; *cnt = 0; }
        }
        return cleared;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
bool CallElem(uintptr_t e, int slot)
{
    __try { VCall<void>(reinterpret_cast<void*>(e), slot); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool DeleteElem(uintptr_t e)
{
    __try { VCall<void*>(reinterpret_cast<void*>(e), kElemDtor, 1u); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallCreate(void (*fn)())
{
    __try { fn(); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallHud(void (*fn)(void*), void* ghud)
{
    __try { fn(ghud); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallHudAdd(void (*fn)(void*, uintptr_t), void* ghud, uintptr_t e)
{
    __try { fn(ghud, e); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CopyWords(const uint64_t* src, uint64_t* dst, size_t n)
{
    __try { memcpy(dst, src, n * 8); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CasWord(uint64_t* p, uint64_t want, uint64_t to)
{
    __try { return (uint64_t)InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(p), (LONG64)to, (LONG64)want) == want; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// client.dll's writable data (.data, .bss folded in) as 8-byte words
bool ClientData(uint64_t*& words, size_t& count)
{
    const HMODULE m = GetModuleHandleA("client.dll");
    if (!m) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(m);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(reinterpret_cast<const uint8_t*>(m) + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (!memcmp(sec[i].Name, ".data", 6) && (sec[i].Characteristics & IMAGE_SCN_MEM_WRITE))
        {
            words = reinterpret_cast<uint64_t*>(reinterpret_cast<uint8_t*>(m) + sec[i].VirtualAddress);
            count = sec[i].Misc.VirtualSize / 8;
            return true;
        }
    return false;
}

// handle: vgui, 0 = none; vp = its VPANEL (elements only); owner = index of its element (children)
struct HudObj { uintptr_t ptr = 0; RttiInfo r; std::string path; unsigned long handle = 0; uintptr_t vp = 0; int owner = -1; };
constexpr int kPnlSetParent = 16;   // IPanel: 14 SetVisible 15 IsVisible 16 SetParent 17 GetChildCount
const char* ElemNameRaw(uintptr_t e)
{
    __try { return VCall<const char*>(reinterpret_cast<void*>(e), kElemGetName); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
std::string ElemName(uintptr_t e)
{
    const char* nm = ElemNameRaw(e);
    char buf[128];
    return nm && CopyName(nm, buf, sizeof buf) ? std::string(buf) : std::string();
}
// the hud elements (path = element name) and every client panel under each (path = name/child/...)
void ReadHud(const uintptr_t* list, int n, std::vector<HudObj>& elems, std::vector<HudObj>& kids)
{
    void* pn = VGui_GetPanelInterface();
    void* s = Surface();
    void* iv = Ivgui();
    const uintptr_t root = s ? VCall<uintptr_t>(s, kSurfGetEmbeddedPanel) : 0;
    auto handleOf = [iv](uintptr_t vp) { return iv ? VCall<unsigned long>(iv, kVguiPanelToHandle, vp) : 0ul; };
    std::unordered_map<uintptr_t, uintptr_t> vpOf;   // complete client object -> its VPANEL
    if (pn && root)
        ForEachVPanel(pn, root, [&](uintptr_t vp) {
            if (void* cp = VCall<void*>(pn, kPnlGetPanel, vp, "ClientDLL"))
            {
                RttiInfo r;
                if (ReadRtti(cp, r)) vpOf.emplace(r.complete, vp);
            }
        });
    for (int i = 0; i < n; ++i)
    {
        HudObj e;
        e.ptr = list[i];
        if (!ReadRtti(reinterpret_cast<void*>(e.ptr), e.r)) continue;
        e.path = ElemName(e.ptr);
        const auto it = vpOf.find(e.r.complete);
        if (it != vpOf.end()) { e.vp = it->second; e.handle = handleOf(it->second); }
        elems.push_back(e);
        if (!pn || it == vpOf.end()) continue;
        // children, depth first, path = element name + vgui names
        std::vector<std::pair<uintptr_t, std::string>> stack{ { it->second, e.path } };
        while (!stack.empty() && kids.size() < 40000)
        {
            const auto [vp, path] = stack.back();
            stack.pop_back();
            const int cn = VCall<int>(pn, kPnlGetChildCount, vp);
            for (int k = 0; k < cn && k < 4096; ++k)
            {
                const uintptr_t c = VCall<uintptr_t>(pn, kPnlGetChild, vp, k);
                if (!c) continue;
                const char* nm = VCall<const char*>(pn, kPnlGetName, c);
                char buf[128];
                if (!nm || !CopyName(nm, buf, sizeof buf)) buf[0] = 0;
                const std::string p = path + "/" + buf;
                if (void* cp = VCall<void*>(pn, kPnlGetPanel, c, "ClientDLL"))
                {
                    HudObj o;
                    o.ptr = reinterpret_cast<uintptr_t>(cp);
                    o.handle = handleOf(c);
                    o.owner = (int)elems.size() - 1;
                    if (ReadRtti(cp, o.r)) { o.path = p; kids.push_back(o); }
                }
                if (p.size() < 1024) stack.push_back({ c, p });
            }
        }
    }
}
std::vector<std::pair<uint64_t, uint64_t>> Match(const std::vector<HudObj>& o, const std::vector<HudObj>& n)
{
    std::vector<mh::PanelNode> a, b;
    for (const auto& x : o) a.push_back({ x.r.complete, x.path, x.r.vt });
    for (const auto& x : n) b.push_back({ x.r.complete, x.path, x.r.vt });
    return mh::MatchByPath(a, b);
}

// false = nothing changed (could not start); the in-place reapply still runs after
bool RemakeHudElements()
{
    if (Exists(std::string(CONFIG_DIR) + "\\nohudremake", false)) { DebugLog("multihud: hud remake skipped (nohudremake file)"); return false; }
    if (!ResolveHud()) return false;
    const DWORD t0 = GetTickCount();
    static uintptr_t oldList[2048], newList[2048];
    const int nOld = ReadHudList(s_hf.ghud, oldList, 2048);
    if (nOld <= 0) { DebugLog("multihud: hud list unreadable (%d), no remake", nOld); return false; }
    std::vector<HudObj> oldElems, oldKids;
    ReadHud(oldList, nOld, oldElems, oldKids);

    // 1 + 2: detach, make, Init. The old elements leave the vgui tree too: at boot a new ctor that
    // looks a sibling up by name (FindChildByName on the viewport) finds nothing or a new one, never
    // an old one it would keep after the delete.
    void* pn = VGui_GetPanelInterface();
    std::vector<std::pair<uintptr_t, uintptr_t>> parents;   // old element vp -> parent vp
    if (pn)
        for (const auto& e : oldElems)
            if (e.vp)
            {
                parents.push_back({ e.vp, VCall<uintptr_t>(pn, kPnlGetParent, e.vp) });
                VCall<void>(pn, kPnlSetParent, e.vp, (uintptr_t)0);
            }
    // every panel in the tree now: a ctor that faults leaves a half-built one parented in
    std::vector<uintptr_t> before;
    void* s = Surface();
    const uintptr_t root = s ? VCall<uintptr_t>(s, kSurfGetEmbeddedPanel) : 0;
    if (pn && root) ForEachVPanel(pn, root, [&](uintptr_t vp) { before.push_back(vp); });
    std::sort(before.begin(), before.end());
    if (!SetHudListCount(s_hf.ghud, 0)) return false;
    // In a game every old element had LevelInit (map load or HudMapInit): LevelShutdown each first, as a
    // level end does (CHud::LevelShutdown, hud.cpp:541). The dtors do not undo LevelInit:
    // CHudItemAttributeTracker adds itself as a GC SO cache listener there and only LevelShutdown removes
    // it (tf_hud_item_progress_tracker.cpp:1580/1592, dtor 546fe0 = no remove) -> deleted listener, the
    // next item update (strange kill count) called it: crash 11-15 19:56:50, client+0x7888A1
    // CGCClientSharedObjectContext::PreSOUpdate. Also CTargetID's floating health icon + the CP icons
    // (freed there, leaked otherwise). Kept (runtime-made) ones too: HudMapInit LevelInits the whole list.
    // Same IsInGame gate as HudMapInit, same frame.
    const bool inGame = g_engine && VCall<bool>(g_engine, kEngIsInGame);
    int shutFail = 0;
    if (inGame)
        for (int i = 0; i < nOld; ++i) shutFail += !CallElem(oldList[i], kElemLevelShutdown);
    s_blockMsgHooks = true;
    s_blockedMsgHooks = 0;
    s_quietSolve = true;
    s_skippedSolves = 0;
    const bool made = CallCreate(s_hf.create);
    const int nNew = ReadHudList(s_hf.ghud, newList, 2048);
    int initFail = 0;
    for (int i = 0; made && i < nNew; ++i) initFail += !CallElem(newList[i], kElemInit);
    s_quietSolve = false;
    s_blockMsgHooks = false;
    if (!made || nNew <= 0)
    {
        // old list + tree back as they were; whatever the make put in the tree leaves it (leaked, never
        // drawn or thought: a half-built panel would crash its first Think)
        int dropped = 0;
        if (pn && root)
        {
            std::vector<uintptr_t> added;
            ForEachVPanel(pn, root, [&](uintptr_t vp) {
                if (!std::binary_search(before.begin(), before.end(), vp)) added.push_back(vp);
            });
            std::sort(added.begin(), added.end());
            for (const uintptr_t vp : added)
                if (VCall<uintptr_t>(pn, kPnlGetParent, vp) && !std::binary_search(added.begin(), added.end(), VCall<uintptr_t>(pn, kPnlGetParent, vp)))
                {
                    VCall<void>(pn, kPnlSetVisible, vp, false);
                    VCall<void>(pn, kPnlSetParent, vp, (uintptr_t)0);
                    ++dropped;
                }
        }
        const bool back = RestoreHudList(s_hf.ghud, oldList, nOld);
        for (const auto& [vp, parent] : parents) VCall<void>(pn, kPnlSetParent, vp, parent);
        if (back && inGame) CallHud(s_hf.levelInit, s_hf.ghud);   // the LevelShutdown above, undone
        DebugLog("multihud: CreateAllElements FAULTED (%d made, %d half-built panels dropped), old hud elements %s",
                 nNew, dropped, back ? "kept" : "NOT restorable");
        return false;
    }
    std::vector<HudObj> newElems, newKids;
    ReadHud(newList, nNew, newElems, newKids);

    // 3: old -> new for every non-virtual base of every element / panel
    std::vector<mh::PtrFix> fixes;
    std::vector<uint64_t> newAddrs;
    std::unordered_map<uint64_t, unsigned long> oldHandle;   // old base address -> vgui handle (outlives the delete?)
    auto addObj = [&](const HudObj& o, uint64_t to) {
        for (int k = 0; k < o.r.n; ++k)
        {
            fixes.push_back({ o.r.complete + o.r.mdisp[k], to ? to + o.r.mdisp[k] : 0 });
            oldHandle.emplace(o.r.complete + o.r.mdisp[k], o.handle);
        }
    };
    std::unordered_map<uint64_t, uint64_t> twin;
    for (const auto& p : Match(oldElems, newElems)) twin.emplace(p.first, p.second);
    for (const auto& p : Match(oldKids, newKids)) twin.emplace(p.first, p.second);
    // Elements no factory makes (named like no new one): game code adds them at runtime and keeps them
    // itself (tf_hud_itemeffectmeter.cpp:67, the charge meters, DHANDLEs in g_ItemEffectMeterManager,
    // remade only on the next post_inventory_application). Deleting them = meters gone until resupply.
    // They stay: back in the tree and the hud list, reapplied in place like before.
    std::vector<char> keep(oldElems.size(), 0);
    int kept = 0;
    for (size_t i = 0; i < oldElems.size(); ++i)
    {
        if (twin.count(oldElems[i].r.complete)) continue;
        const bool named = std::any_of(newElems.begin(), newElems.end(),
                                       [&](const HudObj& n) { return !_stricmp(n.path.c_str(), oldElems[i].path.c_str()); });
        if (named) continue;
        keep[i] = 1;
        ++kept;
        for (const auto& [vp, parent] : parents)
            if (vp == oldElems[i].vp) VCall<void>(pn, kPnlSetParent, vp, parent);
        CallHudAdd(s_hf.add, s_hf.ghud, oldElems[i].ptr);
        DebugLog("multihud: hud element '%s' kept (runtime-made, no factory)", oldElems[i].path.c_str());
    }
    auto kept_ = [&](const HudObj& o, bool isKid) {
        const int e = isKid ? o.owner : (int)(&o - oldElems.data());
        return e >= 0 && e < (int)keep.size() && keep[e];
    };
    for (const auto* v : { &newElems, &newKids })
        for (const auto& o : *v) for (int k = 0; k < o.r.n; ++k) newAddrs.push_back(o.r.complete + o.r.mdisp[k]);
    std::sort(newAddrs.begin(), newAddrs.end());
    for (const auto* v : { &oldElems, &oldKids })
        for (const auto& o : *v)
        {
            if (kept_(o, v == &oldKids)) continue;
            if (std::binary_search(newAddrs.begin(), newAddrs.end(), (uint64_t)o.r.complete)) continue;   // shared, not dying
            const auto it = twin.find(o.r.complete);
            addObj(o, it == twin.end() ? 0 : it->second);
        }
    mh::SortFixes(fixes);

    uint64_t* data = nullptr;
    size_t words = 0;
    std::vector<uint64_t> copy;
    mh::FixHits hits;
    std::vector<std::pair<size_t, uint64_t>> nulls, keeps;   // after the delete: old -> 0, new put back
    int mapped = 0;
    if (ClientData(data, words))
    {
        copy.resize(words);
        if (CopyWords(data, copy.data(), words))
        {
            hits = mh::ScanWords(copy.data(), words, fixes);
            for (const size_t i : hits.mapped)
            {
                const mh::PtrFix* f = mh::FindFix(fixes, copy[i]);
                if (!CasWord(data + i, copy[i], f->to)) continue;
                keeps.push_back({ i, f->to });   // an old dtor's "g_pX = NULL" must not take it back
                if (++mapped <= 40)
                    DebugLog("multihud: client.dll .data +0x%zx re-pointed to the new hud object", (size_t)((uint8_t*)(data + i) - (uint8_t*)GetModuleHandleA("client.dll")));
            }
            for (const size_t i : hits.unmapped) nulls.push_back({ i, copy[i] });
            for (size_t i = 0; i < words; ++i)
                if (copy[i] && std::binary_search(newAddrs.begin(), newAddrs.end(), copy[i])) keeps.push_back({ i, copy[i] });
        }
    }
    else DebugLog("multihud: client.dll .data not found, cached hud pointers NOT re-pointed");

    // 4: lockers, delete old tail first, then the .data words the dtors touched
    const int lockers = ClearLockers(s_hf.ghud);
    int delFail = 0;
    for (int i = nOld - 1; i >= 0; --i)
    {
        const bool isKept = std::any_of(oldElems.begin(), oldElems.end(), [&](const HudObj& e) {
            return e.ptr == oldList[i] && keep[&e - oldElems.data()]; });
        if (!isKept) delFail += !DeleteElem(oldList[i]);
    }
    Hats_HudRemade();
    int nulled = 0, putBack = 0, alive = 0;
    void* iv = Ivgui();
    for (const auto& [i, v] : nulls)
    {
        // a panel the old element did not own (autodelete off, parented in) is still there: keep it
        const auto h = oldHandle.find(v);
        if (iv && h != oldHandle.end() && h->second && VCall<uintptr_t>(iv, kVguiHandleToPanel, h->second)) { ++alive; continue; }
        nulled += CasWord(data + i, v, 0);
    }
    for (const auto& [i, v] : keeps) putBack += CasWord(data + i, 0, v);

    DebugLog("multihud: %s", inGame ? (shutFail ? "old elements LevelShutdown, SOME FAULTED" : "old elements LevelShutdown ok")
                                    : "not in a game, no LevelShutdown");
    DebugLog("multihud: hud elements remade in %lu ms -- %d old (%zu panels) deleted (%d dtor faults), %d new (%zu panels), "
             "%d Init faults, %d message hooks held back, %d ctor solves deferred, .data: %d re-pointed, %d nulled (%d left: "
             "panel outlived its element), %d new pointers put back, %d render group locks cleared, %d old elements without "
             "rtti, %d runtime-made kept",
             GetTickCount() - t0, nOld - kept, oldKids.size(), delFail, nNew, newKids.size(), initFail, s_blockedMsgHooks,
             s_skippedSolves, mapped, nulled, alive, putBack, lockers, nOld - (int)oldElems.size(), kept);
    return true;
}
// The loading screen / stats panel: CTFStatsSummaryPanel "TFStatsSummary" on the GameUI root, made by
// GStatsSummaryPanel() on first use into a plain static (tf_statsummary.cpp:90..125). The game's own
// DestroyStatsSummaryPanel = MarkForDeletion + static = NULL; done the same way here (the static is
// found as the .data word pointing at the panel), the next use makes it boot-fresh. The copy embedded in
// the loadout dialog goes with reload_char_info. No .data word found = left alone (someone else owns it).
constexpr int kVguiMarkForDeletion = 17;   // IVGui: 15 PanelToHandle 16 HandleToPanel 17 MarkPanelForDeletion
void RecreateStatsSummary()
{
    void* pn = VGui_GetPanelInterface();
    void* s = Surface();
    void* iv = Ivgui();
    const uintptr_t root = s ? VCall<uintptr_t>(s, kSurfGetEmbeddedPanel) : 0;
    if (!pn || !iv || !root) return;
    std::vector<uintptr_t> found;
    ForEachVPanel(pn, root, [&](uintptr_t vp) {
        char nm[64];
        const char* n = VCall<const char*>(pn, kPnlGetName, vp);
        if (!n || !CopyName(n, nm, sizeof nm) || strcmp(nm, "TFStatsSummary")) return;
        for (uintptr_t p = VCall<uintptr_t>(pn, kPnlGetParent, vp); p; p = VCall<uintptr_t>(pn, kPnlGetParent, p))
        {
            const char* pnm = VCall<const char*>(pn, kPnlGetName, p);
            char b[64];
            if (pnm && CopyName(pnm, b, sizeof b) && !strcmp(b, "character_info")) return;   // the embedded one
        }
        found.push_back(vp);
    });
    uint64_t* data = nullptr;
    size_t words = 0;
    if (found.empty() || !ClientData(data, words)) return;
    std::vector<uint64_t> copy(words);
    if (!CopyWords(data, copy.data(), words)) return;
    for (const uintptr_t vp : found)
    {
        void* cp = VCall<void*>(pn, kPnlGetPanel, vp, "ClientDLL");
        RttiInfo r;
        if (!cp || !ReadRtti(cp, r)) continue;
        std::vector<mh::PtrFix> fixes;
        for (int k = 0; k < r.n; ++k) fixes.push_back({ r.complete + r.mdisp[k], 0 });
        mh::SortFixes(fixes);
        const mh::FixHits h = mh::ScanWords(copy.data(), words, fixes);
        int nulled = 0;
        for (const size_t i : h.unmapped) nulled += CasWord(data + i, copy[i], 0);
        if (!nulled) { DebugLog("multihud: stats summary panel: no static points at it, left alone"); continue; }
        VCall<void>(iv, kVguiMarkForDeletion, vp);
        DebugLog("multihud: stats summary panel marked for deletion (%d .data refs nulled), next use makes it fresh", nulled);
    }
}

// ---- closed vgui Frames left at alpha 0 (report 11-15: console invisible after a switch) ------------
// vgui Frame (Frame.cpp:1818) fades out on close when the scheme's Frame.TransitionEffectTime > 0 =
// alpha animated to 0; FinishClose never puts it back. Opening fades in only while that time is still
// > 0 (OnThink, Frame.cpp:1045). Closed under a fading hud, opened under a hud with 0 (m0re) = drawn
// at alpha 0: visible, on top, focused, unseen (diag 11-15: GameConsole vis 1, popup 158 of 159).
// A game started with one hud never mixes the two. After a switch every CLOSED Frame, any module
// (GameUI console, options, server browser, client dialogs), gets alpha 255 = how it starts at boot.
// vgui_controls Panel::m_flAlpha = +0x1BC (x64 11-15: GetAlpha = cvttss2si eax,[rcx+1BCh] in client
// and gameui; gameui SetAlpha a0d40 movss [rcx+1BCh]). VPanel vt 38 = Client() (vguimatsurface 10e68).
constexpr size_t kPanelAlpha = 0x1bc;
constexpr int kVPanelClient = 38;
bool FixAlphaRaw(void* panel, float& was)
{
    __try
    {
        float* a = reinterpret_cast<float*>(static_cast<char*>(panel) + kPanelAlpha);
        was = *a;
        if (was >= 255.f) return false;
        *a = 255.f;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void* VPanelClient(uintptr_t vp)
{
    __try { return VCall<void*>(reinterpret_cast<void*>(vp), kVPanelClient); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
void RestoreFrameAlpha()
{
    void* pn = VGui_GetPanelInterface();
    void* s = Surface();
    const uintptr_t root = s ? VCall<uintptr_t>(s, kSurfGetEmbeddedPanel) : 0;
    if (!pn || !root) return;
    static std::vector<std::pair<void*, bool>> frameCache;   // vtable -> is a vgui::Frame
    std::vector<uintptr_t> all;
    ForEachVPanel(pn, root, [&](uintptr_t vp) { all.push_back(vp); });
    const int pc = VCall<int>(s, kSurfGetPopupCount);
    for (int i = 0; i < pc && i < 1024; ++i)
        if (const uintptr_t p = VCall<uintptr_t>(s, kSurfGetPopup, i)) all.push_back(p);
    std::sort(all.begin(), all.end());
    all.erase(std::unique(all.begin(), all.end()), all.end());
    int fixed = 0;
    for (const uintptr_t vp : all)
    {
        if (VCall<bool>(pn, kPnlIsVisible, vp)) continue;   // open (or fading): its own fade owns alpha
        void* cp = VPanelClient(vp);
        if (!cp || !HasBaseCached(cp, ".?AVFrame@vgui@@", frameCache)) continue;
        float was = 0.f;
        if (!FixAlphaRaw(cp, was)) continue;
        char nm[64] = "?";
        if (const char* n = VCall<const char*>(pn, kPnlGetName, vp)) CopyName(n, nm, sizeof nm);
        if (fixed++ < 20) DebugLog("multihud: closed frame '%s' was at alpha %.0f (faded out under the old hud), back to 255", nm, was);
    }
    if (fixed) DebugLog("multihud: %d closed frames back to alpha 255", fixed);
}

// ---- the matchmaking dashboard (top bar: chat, party, find a game / resume / disconnect) -------------
// CTFMatchmakingDashboard "MMDashboard" is made once by GetMMDashboard() into a function static and
// handed to CMMDashboardParentManager::AddPanel (raw Panel* in its CUtlSortVector m_vecPanels,
// tf_matchmaking_dashboard.cpp:250, _parent_manager.cpp:69). On a resolution change TF2 remakes only
// the side panels and Reload()s it in place (tf_viewport.cpp:453), so its code-made TopBar kept the old
// hud's look (grey bar 11-15). Remade like RemovePanel + DestroyStatsSummaryPanel do it: out of the
// manager's list, static nulled, MarkPanelForDeletion; TFViewport::OnScreenSizeChanged (RebuildPanels)
// then calls GetMMDashboard() -> a new one. The manager is found as the .data-held object whose vtable
// is CMMDashboardParentManager's (RTTI), its list as the CUtlVector holding the dashboard.
// client module image: RTTI type name -> complete object vtable VA, 0 = not found
uintptr_t FindVtableRaw(HMODULE mod, const char* typeName);
uintptr_t FindVtable(HMODULE mod, const char* typeName)
{
    __try { return FindVtableRaw(mod, typeName); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
uintptr_t FindVtableRaw(HMODULE mod, const char* typeName)
{
    if (!mod) return 0;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(mod);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(reinterpret_cast<const uint8_t*>(mod) + dos->e_lfanew);
    const uint8_t* img = reinterpret_cast<const uint8_t*>(mod);
    const size_t size = nt->OptionalHeader.SizeOfImage, nlen = strlen(typeName) + 1;
    const uint8_t* td = nullptr;
    for (size_t i = 0x10; i + nlen < size && !td; ++i)
        if (img[i] == '.' && !memcmp(img + i, typeName, nlen)) td = img + i - 0x10;
    if (!td) return 0;
    const uint32_t tdRva = (uint32_t)(td - img);
    for (size_t i = 0; i + 24 <= size; i += 4)
    {
        const uint32_t* c = reinterpret_cast<const uint32_t*>(img + i);
        if (c[3] != tdRva || c[0] != 1 || c[1] != 0) continue;   // COL: sig 1, offset 0, type descriptor
        const uint64_t colVa = reinterpret_cast<uint64_t>(img + i);
        for (size_t j = 0; j + 8 <= size; j += 8)
            if (*reinterpret_cast<const uint64_t*>(img + j) == colVa) return reinterpret_cast<uintptr_t>(img + j + 8);
    }
    return 0;
}
bool ReadU64(uint64_t p, uint64_t& out)
{
    __try { out = *reinterpret_cast<const uint64_t*>(p); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// removes `item` from a CUtlVector<T*> member of obj (first 0x80 bytes), true = removed
bool RemoveFromObjVector(uint8_t* obj, uint64_t item)
{
    __try
    {
        for (size_t off = 8; off <= 0x60; off += 8)
        {
            uint64_t* mem = *reinterpret_cast<uint64_t**>(obj + off);
            const int alloc = *reinterpret_cast<int*>(obj + off + 8);
            int* size = reinterpret_cast<int*>(obj + off + 0x10);
            if (!mem || *size <= 0 || *size > alloc || alloc > 4096) continue;
            for (int i = 0; i < *size; ++i)
                if (mem[i] == item)
                {
                    for (int k = i; k + 1 < *size; ++k) mem[k] = mem[k + 1];
                    --*size;
                    return true;
                }
        }
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void RecreateDashboard()
{
    void* pn = VGui_GetPanelInterface();
    void* s = Surface();
    void* iv = Ivgui();
    const uintptr_t root = s ? VCall<uintptr_t>(s, kSurfGetEmbeddedPanel) : 0;
    const HMODULE client = GetModuleHandleA("client.dll");
    if (!pn || !iv || !root || !client) return;
    const uintptr_t dvp = FindPanel(pn, root, "MMDashboard", 0);
    void* dcp = dvp ? VCall<void*>(pn, kPnlGetPanel, dvp, "ClientDLL") : nullptr;
    RttiInfo dr;
    if (!dcp || !HasRttiBase(dcp, ".?AVCTFMatchmakingDashboard@@") || !ReadRtti(dcp, dr)) return;
    static uintptr_t mgrVt = FindVtable(client, ".?AVCMMDashboardParentManager@@");
    uint64_t* data = nullptr;
    size_t words = 0;
    if (!mgrVt || !ClientData(data, words)) { DebugLog("multihud: dashboard not remade (parent manager vtable %p)", (void*)mgrVt); return; }
    std::vector<uint64_t> copy(words);
    if (!CopyWords(data, copy.data(), words)) return;
    // the manager: a .data word pointing at an object with its vtable
    uint8_t* mgr = nullptr;
    for (size_t i = 0; i < words && !mgr; ++i)
    {
        const uint64_t w = copy[i];
        if (w < 0x10000 || w > 0x7FFFFFFFFFFF || (w & 7)) continue;
        uint64_t vt = 0;
        if (ReadU64(w, vt) && vt == mgrVt) mgr = reinterpret_cast<uint8_t*>(w);
    }
    if (!mgr) { DebugLog("multihud: dashboard not remade (parent manager not found in .data)"); return; }
    // GetMMDashboard's static first: without it the getter would hand out the deleted one
    std::vector<mh::PtrFix> fixes;
    for (int k = 0; k < dr.n; ++k) fixes.push_back({ dr.complete + dr.mdisp[k], 0 });
    mh::SortFixes(fixes);
    const std::vector<size_t> refs = mh::ScanWords(copy.data(), words, fixes).unmapped;
    if (refs.empty()) { DebugLog("multihud: dashboard not remade (no .data static points at it)"); return; }
    uint64_t dashPanel = 0;   // the Panel* AddPanel stored: try every non-virtual base
    for (int k = 0; k < dr.n && !dashPanel; ++k)
        if (RemoveFromObjVector(mgr, dr.complete + dr.mdisp[k])) dashPanel = dr.complete + dr.mdisp[k];
    if (!dashPanel) { DebugLog("multihud: dashboard not remade (not in the parent manager's list)"); return; }
    int nulled = 0;
    for (const size_t i : refs) nulled += CasWord(data + i, copy[i], 0);
    VCall<void>(iv, kVguiMarkForDeletion, dvp);
    DebugLog("multihud: matchmaking dashboard out of the parent manager, %d .data refs nulled, marked for deletion: "
             "the viewport rebuild makes it again", nulled);
}

// 5: a map load's element calls, once the reapply has applied scheme + layout to the new elements (at
// boot both ran long before the first map)
void HudMapInit()
{
    if (!s_hf.ok || !g_engine || !VCall<bool>(g_engine, kEngIsInGame)) return;
    const bool vid = CallHud(s_hf.vidInit, s_hf.ghud);
    const bool lvl = CallHud(s_hf.levelInit, s_hf.ghud);
    DebugLog("multihud: in a game: hud VidInit %s, LevelInit %s", vid ? "ok" : "FAULTED", lvl ? "ok" : "FAULTED");
}

// ---- console watch (diag, report 11-15: console window gone after a switch, typing still works) ------
// GameUI's CGameConsoleDialog ("GameConsole", gameui.dll) is never touched by name here. When it turns
// visible in the first opens after a switch, log what decides if it draws: its own state, every
// ancestor, and what paints after it (later popups, later siblings).
constexpr int kPnlGetZPos = 9, kPnlIsPopup = 24, kPnlIsFullyVisible = 26;
unsigned long s_conHandle = 0;
bool s_conWasVisible = false;
int s_conLogIn = 0, s_conLogs = 0;
std::string PanelTag(void* pn, uintptr_t vp)
{
    char nm[96] = "?";
    if (const char* n = VCall<const char*>(pn, kPnlGetName, vp)) CopyName(n, nm, sizeof nm);
    int x = 0, y = 0, w = 0, h = 0;
    VCall<void>(pn, kPnlGetPos, vp, &x, &y);
    VCall<void>(pn, kPnlGetSize, vp, &w, &h);
    char buf[256];
    snprintf(buf, sizeof buf, "'%s' vis %d full %d popup %d z %d at %d,%d %dx%d", nm, (int)VCall<bool>(pn, kPnlIsVisible, vp),
             (int)VCall<bool>(pn, kPnlIsFullyVisible, vp), (int)VCall<bool>(pn, kPnlIsPopup, vp),
             VCall<int>(pn, kPnlGetZPos, vp), x, y, w, h);
    return buf;
}
void LogConsoleState(void* pn, uintptr_t con)
{
    DebugLog("multihud: console opened -- %s", PanelTag(pn, con).c_str());
    for (uintptr_t p = VCall<uintptr_t>(pn, kPnlGetParent, con), d = 0; p && d < 16; p = VCall<uintptr_t>(pn, kPnlGetParent, p), ++d)
        DebugLog("multihud:   parent %d %s", (int)d, PanelTag(pn, p).c_str());
    if (const uintptr_t par = VCall<uintptr_t>(pn, kPnlGetParent, con))
    {
        const int n = VCall<int>(pn, kPnlGetChildCount, par);
        bool after = false;
        for (int i = 0; i < n && i < 256; ++i)
        {
            const uintptr_t c = VCall<uintptr_t>(pn, kPnlGetChild, par, i);
            if (c == con) { after = true; continue; }
            if (after && c && VCall<bool>(pn, kPnlIsVisible, c)) DebugLog("multihud:   sibling drawn after it %s", PanelTag(pn, c).c_str());
        }
    }
    void* s = Surface();
    const int pc = VCall<int>(s, kSurfGetPopupCount);
    int at = -1;
    for (int i = 0; i < pc && i < 1024; ++i)
    {
        const uintptr_t p = VCall<uintptr_t>(s, kSurfGetPopup, i);
        if (p == con) { at = i; continue; }
        if (at >= 0 && p && VCall<bool>(pn, kPnlIsVisible, p)) DebugLog("multihud:   popup after it [%d] %s", i, PanelTag(pn, p).c_str());
    }
    DebugLog("multihud:   console popup index %d of %d", at, pc);
}
void ConsoleWatch()
{
    void* pn = VGui_GetPanelInterface();
    void* iv = Ivgui();
    void* s = Surface();
    if (!pn || !iv || !s || s_conLogs <= 0) return;
    uintptr_t con = s_conHandle ? VCall<uintptr_t>(iv, kVguiHandleToPanel, s_conHandle) : 0;
    if (!con)
    {
        static DWORD next = 0;
        if (GetTickCount() < next) return;
        next = GetTickCount() + 2000;
        const int pc = VCall<int>(s, kSurfGetPopupCount);
        for (int i = 0; i < pc && i < 1024 && !con; ++i)
        {
            const uintptr_t p = VCall<uintptr_t>(s, kSurfGetPopup, i);
            const char* n = p ? VCall<const char*>(pn, kPnlGetName, p) : nullptr;
            char nm[64];
            if (n && CopyName(n, nm, sizeof nm) && !strcmp(nm, "GameConsole")) con = p;
        }
        if (!con)
            if (const uintptr_t root = VCall<uintptr_t>(s, kSurfGetEmbeddedPanel)) con = FindPanel(pn, root, "GameConsole", 0);
        if (!con) return;
        s_conHandle = VCall<unsigned long>(iv, kVguiPanelToHandle, con);
    }
    const bool vis = VCall<bool>(pn, kPnlIsVisible, con);
    if (vis && !s_conWasVisible) s_conLogIn = 3;   // after Activate's MoveToFront + first layout
    s_conWasVisible = vis;
    if (s_conLogIn > 0 && --s_conLogIn == 0) { LogConsoleState(pn, con); --s_conLogs; }
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
    LearnVguiNames(dirs);                      // image names both huds use: CRC -> name for stuck ids
    RepairVguiIds("switch");
    const bool remade = RemakeHudElements();   // boot-fresh hud elements, new scheme + cfg already in place
    const unsigned long msRemake = lap();
    ResetStale();                              // keys the new hud leaves out (panels still reapplied in place)
    Exec("hud_reloadscheme");                  // viewport + hud elements on the new scheme + cvars
    if (remade) HudMapInit();
    // loadout / char info: a GameUI-root singleton the viewport rebuild never reaches. TF2's own
    // "reload_char_info" (character_info_panel.cpp:36) marks it for deletion and makes it again =
    // boot-fresh. Only when it exists: it is made lazily on first use, like at boot.
    if (void* pn = VGui_GetPanelInterface(); pn && Surface())
        if (const uintptr_t root = VCall<uintptr_t>(Surface(), kSurfGetEmbeddedPanel); root && FindPanel(pn, root, "character_info", 0))
            Exec("reload_char_info");
    RecreateStatsSummary();
    RecreateDashboard();                       // before RebuildPanels: TFViewport remakes it there
    s_conLogs = 5;                             // console diag: the next opens after this switch
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
    s_repairFrames = 3;                        // images first bound by this frame's paint
    RestoreFrameAlpha();                       // closed frames the old hud faded out
    const unsigned long msPanels = lap();
    DebugLog("multihud: %s -- %d schemes, %d materials (%d vgui ids rebound), %d bitmap fonts, %d string files%s, "
             "%d cfg commands%s, hud elements %s, %d branches | ms: flush %lu schemes %lu materials %lu strings %lu "
             "cfg %lu sound %lu remake %lu panels %lu", label.c_str(), schemes, mats, rebound, vbf, strings,
             captions ? ", captions" : "", cfg, snd ? ", sounds" : "", remade ? "remade" : "reapplied in place", posted,
             msFlush, msSchemes, msMats, msStrings, msCfg, msSound, msRemake, msPanels);
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

// ---- prop diag (round 12) ------------------------------------------------------------------------
// User 11-16: after a switch SOME map props go invisible, stay solid, come back on the next map, any
// hud. Not found from code (STATUS round 11 list). Opt-in file C:\botox-tf2\mhpropdiag: a switch in a
// game first records kPdFrames frames of what draws (static props handed to
// CStaticPropMgr::DrawStaticProps, map entities through DrawModelExecute), then switches, waits
// kPdWaitMs, records again and logs every one that stopped drawing with its state then vs now: which
// layer drops it (leaf system handle, fade alpha, ShouldDraw, dormancy, model). Stand still while it
// runs: turning changes what is in view. Main thread only (FRAME_START, view render, both hooks).
// A map load or disconnect mid-diag drops it: the recorded pointers die with the map.
struct PdStatic { const void* mdl; int rh, alpha, flags, draw; };
struct PdEnt { const void* mdl; void* ent; };
struct PropDiag
{
    int phase = 0;   // 0 off, 1 recording before (the pick waits), 2 switched (waiting), 3 recording after
    int frames = 0;
    DWORD t = 0;
    std::unordered_map<void*, PdStatic> sb, sa;   // renderable -> state when first seen
    std::unordered_map<int, PdEnt> eb, ea;         // entity index -> model, entity
};
PropDiag s_pd;
constexpr int kPdFrames = 20;
constexpr DWORD kPdWaitMs = 3000;
constexpr int kPdList = 30;                    // per kind, so a whole-map loss does not flood the log
constexpr int kPdFirstMapEnt = 102;            // 1..101 = player slots (TF2 x64 MAX_PLAYERS 101)
// CStaticProp (visuals.cpp CStaticProp_Layout = Aletherium's): m_Alpha +60, m_Flags +63
constexpr int kSpAlpha = 60, kSpFlags = 63;
// IClientRenderable 3 ShouldDraw, 8 RenderHandle, 9 GetModel; IClientUnknown 5 GetClientRenderable;
// IClientNetworkable 8 IsDormant; IClientEntityList 0 GetClientNetworkable (hats.cpp, world_fx3.cpp, live)
constexpr int kRendShouldDraw = 3, kRendHandle = 8, kRendModel = 9, kUnkRenderable = 5, kNetDormant = 8;

int ReadByteAt(uintptr_t p)
{
    __try { return *reinterpret_cast<const uint8_t*>(p); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
// rh -1 = no handle call, 0xFFFF = INVALID_CLIENT_RENDER_HANDLE (out of the leaf system)
void ReadRend(void* r, const void*& mdl, int& rh, int& draw)
{
    mdl = nullptr; rh = -1; draw = -1;
    __try
    {
        mdl = VCall<const void*>(r, kRendModel);
        draw = VCall<bool>(r, kRendShouldDraw) ? 1 : 0;
        if (const uint16_t* h = VCall<const uint16_t*>(r, kRendHandle)) rh = *h;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
PdStatic ReadStatic(void* r)
{
    PdStatic s{ nullptr, -1, -1, -1, -1 };
    ReadRend(r, s.mdl, s.rh, s.draw);
    RttiInfo ri;   // props[] are IClientRenderable* (+8 in CStaticProp): fields off the complete object
    if (ReadRtti(r, ri))
    {
        s.alpha = ReadByteAt(ri.complete + kSpAlpha);
        s.flags = ReadByteAt(ri.complete + kSpFlags);
    }
    return s;
}
const char* PdModelName(const void* mdl)
{
    if (!mdl || !g_modelInfo) return "?";
    const char* n = nullptr;
    __try { n = VCall<const char*>(g_modelInfo, 3, mdl); } __except (EXCEPTION_EXECUTE_HANDLER) { n = nullptr; }
    return n ? n : "?";
}
struct PdEntNow { void* ent = nullptr; const void* mdl = nullptr; int dormant = -1, rh = -1, draw = -1; };
PdEntNow ReadEntNow(int idx)
{
    PdEntNow o;
    o.ent = GetClientEntity(idx);
    if (!o.ent) return o;
    void* rend = nullptr;
    __try
    {
        if (void* net = VCall<void*>(g_entityList, 0, idx)) o.dormant = VCall<bool>(net, kNetDormant) ? 1 : 0;
        rend = VCall<void*>(o.ent, kUnkRenderable);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { rend = nullptr; }
    if (rend) ReadRend(rend, o.mdl, o.rh, o.draw);
    return o;
}
void PropDiagAbort(const char* why)
{
    if (!s_pd.phase) return;
    DebugLog("multihud: propdiag dropped (%s)", why);
    s_pd = PropDiag{};
    g_mhPropDiag = false;
}
void PropDiagReport()
{
    std::unordered_map<const void*, int> modelsAfter;
    for (const auto& kv : s_pd.sa) ++modelsAfter[kv.second.mdl];
    for (const auto& kv : s_pd.ea) ++modelsAfter[kv.second.mdl];
    DebugLog("multihud: propdiag -- static props drawn before %zu, after %zu | map entities drawn before %zu, after %zu",
             s_pd.sb.size(), s_pd.sa.size(), s_pd.eb.size(), s_pd.ea.size());
    int gs = 0;
    for (const auto& [r, then] : s_pd.sb)
    {
        if (s_pd.sa.count(r)) continue;
        if (++gs > kPdList) continue;
        const PdStatic now = ReadStatic(r);   // static props live as long as the map
        DebugLog("multihud: propdiag static GONE %s | then rh %d alpha %d flags 0x%x draw %d | now rh %d alpha %d "
                 "flags 0x%x draw %d | same model drawn after: %s", PdModelName(then.mdl), then.rh, then.alpha,
                 then.flags, then.draw, now.rh, now.alpha, now.flags, now.draw, modelsAfter.count(then.mdl) ? "yes" : "no");
    }
    int ge = 0;
    for (const auto& [idx, then] : s_pd.eb)
    {
        if (s_pd.ea.count(idx)) continue;
        if (++ge > kPdList) continue;
        const PdEntNow now = ReadEntNow(idx);
        DebugLog("multihud: propdiag entity GONE #%d %s | now %s, dormant %d, shoulddraw %d, rh %d, model %s | same model "
                 "drawn after: %s", idx, PdModelName(then.mdl),
                 !now.ent ? "no entity" : now.ent != then.ent ? "another entity" : "same entity",
                 now.dormant, now.draw, now.rh, now.ent ? PdModelName(now.mdl) : "-", modelsAfter.count(then.mdl) ? "yes" : "no");
    }
    DebugLog("multihud: propdiag -- %d static props + %d map entities stopped drawing (first %d of each listed). "
             "rh 65535 = out of the leaf system, alpha 0 = faded, dormant 1 = server says out of PVS",
             gs, ge, kPdList);
}
// per frame, before the pick check: abort / wait / finish
void PropDiagTick()
{
    if (!s_pd.phase) return;
    if (!VCall<bool>(g_engine, kEngIsInGame)) { PropDiagAbort("left the game"); return; }
    if (s_pd.phase == 2 && GetTickCount() - s_pd.t >= kPdWaitMs)
    {
        s_pd.phase = 3;
        s_pd.frames = 0;
        g_mhPropDiag = true;
        DebugLog("multihud: propdiag recording after the switch (%d frames)", kPdFrames);
    }
    else if (s_pd.phase == 3 && ++s_pd.frames >= kPdFrames)
    {
        g_mhPropDiag = false;
        PropDiagReport();
        s_pd = PropDiag{};
    }
}
// true = hold the pick this frame (recording what draws before it)
bool PropDiagDefer()
{
    if (s_pd.phase == 1)
    {
        if (++s_pd.frames < kPdFrames) return true;
        g_mhPropDiag = false;
        return false;
    }
    if (s_pd.phase) return false;   // another pick mid-diag: switch now, PropDiagSwitched restarts the wait
    if (!VCall<bool>(g_engine, kEngIsInGame) || !Exists(std::string(CONFIG_DIR) + "\\mhpropdiag", false)) return false;
    s_pd = PropDiag{};
    s_pd.phase = 1;
    g_mhPropDiag = true;
    DebugLog("multihud: propdiag recording before the switch (%d frames), stand still", kPdFrames);
    return true;
}
void PropDiagSwitched()
{
    if (!s_pd.phase) return;
    s_pd.phase = 2;
    s_pd.t = GetTickCount();
    s_pd.sa.clear();
    s_pd.ea.clear();
    g_mhPropDiag = false;
}

} // namespace

bool g_mhPropDiag = false;

void MultiHud_PropDiagStatic(void** props, int count)
{
    auto& m = s_pd.phase == 1 ? s_pd.sb : s_pd.sa;
    for (int i = 0; i < count; ++i)
        if (props[i] && !m.count(props[i])) m.emplace(props[i], ReadStatic(props[i]));
}
void MultiHud_PropDiagEnt(int entIdx, const void* model)
{
    if (entIdx < kPdFirstMapEnt || entIdx >= 2048 || !model || Hats_IsOurs(entIdx)) return;
    auto& m = s_pd.phase == 1 ? s_pd.eb : s_pd.ea;
    if (!m.count(entIdx)) m.emplace(entIdx, PdEnt{ model, GetClientEntity(entIdx) });
}

void MultiHud_MainTick()
{
    if (g_eject) return;
    // IVEngineClient::IsDrawingLoadingImage (28, after IsInGame 26 / IsConnected 27): no rebuild
    // mid map load, the pick waits for the first frame after it
    if (!g_engine) return;
    if (VCall<bool>(g_engine, kEngIsDrawingLoadingImage)) { PropDiagAbort("map load"); return; }
    if (s_removalFrames > 0 && --s_removalFrames == 0) ApplyRemovals();
    if (s_repairFrames > 0 && --s_repairFrames == 0)
    {
        RepairVguiIds("3 frames after the switch");
        RestoreFrameAlpha();                   // frames whose close fade finished during the switch
        SetResCache(false);                    // rebuild dispatched: .res from memory again while playing
    }
    ConsoleWatch();
    PropDiagTick();
    const bool again = s_reapply.exchange(false);
    const std::string pick = mh::Pick(g_multiHudOn, g_multiHud);
    if (!again && s_applied == pick) { if (s_pd.phase == 1) PropDiagAbort("pick changed back"); return; }
    if (PropDiagDefer()) { if (again) s_reapply = true; return; }   // "before" frames first
    s_applied = pick;
    DebugLog("multihud: switch to '%s'", s_applied.c_str());
    Apply(s_applied);
    PropDiagSwitched();
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
int  MultiHud_RebuildPanels(int oldW, int oldH) { return RebuildPanels(oldW, oldH); }

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
