#include "../../globals/globals.h"
#include "../aim/aim.h"
#include "../misc/misc_overlays.h"

#include "lua_bridge.h"

#include <string>

const char* LuaB_Folder()
{
    static const std::string s_folder = std::string(CONFIG_DIR) + "\\lua";
    return s_folder.c_str();
}

void LuaB_Log(const char* prefix, const char* text)
{
    MiscLogs_Push(prefix, text);
}

void LuaB_Console(const char* text)
{
    ConsoleColorPrint(220, 220, 220, (std::string(text) + "\n").c_str());
}

bool LuaB_MenuOpen()
{
    return g_showMenu;
}

bool LuaB_WindowActive()
{
    return !g_hwnd || GetForegroundWindow() == g_hwnd;
}

bool LuaB_KeyDown(int vk)
{
    return vk > 0 && vk < 256 && LuaB_WindowActive() && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

/* movement.cpp BindKeyDown / MovementKeyOK, without the per-cmd memo */
bool LuaB_BindActive(int key, int style)
{
    if (style != 1 && style != 2) return true;
    if (key <= 0 || key >= 256 || !LuaB_WindowActive()) return false;
    return style == 1 ? !Binds_Blocked() && (GetAsyncKeyState(key) & 0x8000) != 0 : Binds_Toggled(key);
}

void LuaB_MousePos(int& x, int& y)
{
    POINT p{};
    GetCursorPos(&p);
    if (g_hwnd) ScreenToClient(g_hwnd, &p);
    x = p.x;
    y = p.y;
}

bool LuaB_AimReady()
{
    return Aim_Active();
}

// IVModelInfo ("VModelInfoClient006"): 3 GetModelName, 28 GetStudiomodel, 46 GetCacheHandle = skins.cpp's
// disasm-checked slots; 39 FindOrLoadModel = 46 - 1 - the six obsolete stubs between them
// (SDK public/engine/ivmodelinfo.h:71-77), the same slot esp_preview's I::ModelInfoClient call lands on.
// IMDLCache (datacache.dll "MDLCache004") 11 GetHardwareData: live x64 datacache.dll vtable via RTTI,
// 9 GetRef (movsx word [+0x3a]), 10 GetStudioHdr, 11 the two mod_test_*_not_available checks. The SDK
// header has no GetRef, so counting it gives 10 = GetStudioHdr. CSS x64 datacache: same vtable.
static void* s_mdlCache = nullptr;

static void* MdlCache()
{
    static const char* const kNames[] = { "MDLCache004", nullptr };
    if (!s_mdlCache) s_mdlCache = GetInterfaceAny("datacache.dll", kNames);
    return s_mdlCache;
}

static const void* MI_Studiomodel(const void* model)
{
    return ((const void*(__fastcall*)(void*, const void*))(*(void***)g_modelInfo)[28])(g_modelInfo, model);
}

const void* LuaB_LoadModel(const char* path)
{
    if (!g_modelInfo || !path || !*path) return nullptr;
    __try {
        const void* model = ((const void*(__fastcall*)(void*, const char*))(*(void***)g_modelInfo)[39])(g_modelInfo, path);
        const char* hdr   = model ? static_cast<const char*>(MI_Studiomodel(model)) : nullptr;
        // missing file / sv_pure rejected loose file: mdlcache hands back error.mdl (studiohdr_t::name +12)
        return hdr && !strstr(hdr + 12, "error.mdl") ? model : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

bool LuaB_ModelParts(const void* model, const void** hdr, const void** hw)
{
    void* cache = MdlCache();
    if (!g_modelInfo || !cache || !model) return false;
    __try {
        *hdr = MI_Studiomodel(model);
        const unsigned short handle = ((unsigned short(__fastcall*)(void*, const void*))(*(void***)g_modelInfo)[46])(g_modelInfo, model);
        *hw = handle != 0xFFFF ? ((const void*(__fastcall*)(void*, unsigned short))(*(void***)cache)[11])(cache, handle) : nullptr;
        if (!*hdr || !*hw) return false;
        // studiohwdata_t: int m_RootLOD, int m_NumLODs, studioloddata_t* m_pLODs (+8). a wrong slot hands
        // back a studiohdr ('IDST' first) or a vcollide, neither passes this
        const int* lod = static_cast<const int*>(*hw);
        return lod[1] > 0 && lod[1] <= 8 && lod[0] >= 0 && lod[0] < lod[1] &&
               *reinterpret_cast<const void* const*>(static_cast<const char*>(*hw) + 8) != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
