#pragma once
#include "../../globals/globals.h"

inline constexpr float kChUnitToPx = 2.0f;

void RenderCrosshair();

// CS:GO sniper scope (scope_csgo.inl): hooks CHudScope::Paint + the sniper dot. Active = painted
// on the last HUD pass; Restore = unhook (eject).
void ScopeCsgo_Install(HMODULE client);
// Draws the scope when vpanel is the HUD root (CBaseViewport). Call before the original PaintTraverse.
void ScopeCsgo_PrePaintTraverse(void* panels, uintptr_t vpanel);
bool ScopeCsgo_Active();
void ScopeCsgo_Restore();
void ScopeCsgo_ReleaseTextures();

// csgo reticle art + the custom crosshair's blend-mode shaders and frame copy (DEFAULT
// pool) -- drop them on Reset and eject.
void CsgoCrosshair_ReleaseTextures();

// Dropdown of the custom crosshair's blend modes (ch_blend_modes.h), one pick at a time.
// Returns true while the closed combo is hovered, for the caller's tooltip.
bool Crosshair_BlendCombo(const char* id);

// Put the game's "crosshair" convar back to the player's value (eject).
void Crosshair_Restore();

// Static amby crosshair: hook CTFRevolver::GetWeaponCrosshairScale, skip original
// when enabled so the Ambassador crosshair stays a fixed size instead of expanding.
using GetWeaponCrosshairScale_t = void(__fastcall*)(void*, float&);
extern GetWeaponCrosshairScale_t o_GetWeaponCrosshairScale;
void __fastcall h_GetWeaponCrosshairScale(void* rcx, float& flScale);
