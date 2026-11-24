#pragma once
// What dih's own TUs may call from the aim code (src/hacks/aim). The aim code keeps its own
// SDK layer (sdk/), so this header includes none of it: thread.cpp / menu.cpp / visuals.cpp can
// include it without SDK.h / Vars.h (their type names clash with dih's globals.h).
//
// Aim_Init is part of dih's normal hook install (thread.cpp PollThread, after client.dll and
// engine.dll are up). It is not all-or-nothing: a sig a game update broke turns off only what
// uses it (logged by name in debug.log), the rest runs.

// Sigs, interfaces, every aim hook, event listener, convar unlock. Only the first call does
// anything. false = an interface is missing (nothing else stops it).
bool Aim_Init();

// Aim_Init succeeded: the I:: interfaces are set. Gate for anything that reads them
// (chams, glow, csgo killfeed, lua api).
bool Aim_Active();

// The aim CreateMove hook went in (hooks.cpp CHLClient_CreateMove: aimbot + Movement_Run).
// false = thread.cpp installs dih's own h_CreateMove instead, so movement always runs.
bool Aim_OwnsCreateMove();

// dih's h_DispatchUserMessage (visuals.cpp) calls this before the original: no spread's
// playerperf TextMsg reply. true = consumed, return true without calling the original.
bool Aim_OnUserMessage(int type, void* msgData);

// vgui owns the keyboard: chat, console, pause menu, class/team menus. false until interfaces resolve.
bool Aim_VguiInputUp();

// Print a line to the game chat (\x07RRGGBB color codes). No-op until interfaces resolve.
// Game thread only. `text` is the ChatPrintf format, so it must not contain '%'.
void Aim_ChatPrint(const char* text);

// Disables the aim hooks. Called from dih's eject path before MH_Uninitialize.
void Aim_Unload();

// Aim-var persistence, folded into dih's OWN config file (src/hacks/aim/aim.cpp).
// src/hacks/menu/config.cpp calls these from SaveConfig / LoadConfig, so the aim settings sit in
// C:botox-tf2\default.cfg with everything else and the menu's save/load buttons already cover them.
#include <cstdio>
void Aim_SaveVars(FILE* f);                                  // append our keys
bool Aim_LoadVar(const char* szKey, const char* szValue);    // true = key was ours

// The aimbot tab body, drawn inside dih's own menu window. Lives on this side because it
// reads ConfigVar metadata; dih's menu.cpp only calls it.
void Aim_RenderMenuTab();
