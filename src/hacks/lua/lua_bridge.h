#pragma once
#include <cstddef>

// lua_api.cpp is an aim TU (Amalgam SDK, /FI prefix.h, no dih globals.h). Everything it needs
// from the dih side comes through these plain functions (lua_dih.cpp, config.cpp).

const char* LuaB_Folder();                               // C:\botox-tf2\lua
void LuaB_Log(const char* prefix, const char* text);     // botox log feed (top left) + console line
void LuaB_Console(const char* text);                     // game console line
bool LuaB_MenuOpen();
bool LuaB_WindowActive();                                // tf2 is the foreground window
bool LuaB_KeyDown(int vk);
bool LuaB_BindActive(int key, int style);                // 0 always, 1 hold, 2 toggle
void LuaB_MousePos(int& x, int& y);                      // game window client px
bool LuaB_AimReady();                                    // aim half up = I:: interfaces valid

// config.cpp kEntries, every botox setting by its cfg key
struct LuaCfgVar
{
    const char* key;
    int         type;   // CfgEntry: 0 bool, 1 int, 2 float, 3 float4 (colour 0..1), 4 string, 5 curve
    void*       ptr;
    size_t      size;   // string buffer size
};
int  LuaB_CfgCount();
bool LuaB_CfgAt(int index, LuaCfgVar& out);
bool LuaB_CfgFind(const char* key, LuaCfgVar& out);
bool LuaB_CfgReset(const char* key);                     // back to the value before the first config load
