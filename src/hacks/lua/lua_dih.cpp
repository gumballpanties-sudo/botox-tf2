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
