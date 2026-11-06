#pragma once

// settings > lua: botox menu_tab_lua.cpp 1:1. Left = scripts + load/unload, right = each loaded
// script's settings group. Render thread (menu build), touches g_lua only under g_lua.m_mutex.
void Page_Lua();
