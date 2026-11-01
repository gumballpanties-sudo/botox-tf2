#pragma once

// Unit F (gap.md "Aim extras"): scope FOV scale, per-weapon aim tuning, grenade
// lob assist. One function, plain ImGui widgets -- menu.cpp's tab bodies are being rebuilt
// elsewhere, so this never touches one; it draws its own rows with the same shared helpers
// aim.cpp's own tab already uses (MenuSection/MenuBindKey, src/hacks/menu/menu.h).
void UnitF_Menu_aimbot_config();   // aimbot tab "configuration" group
void UnitF_Menu_aimbot_lob();      // aimbot tab "grenade lob" group
