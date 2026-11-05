#pragma once

// Aim extras (gap.md): scope FOV scale, per-weapon aim tuning.
// One function, plain ImGui widgets -- menu.cpp's tab bodies are being rebuilt
// elsewhere, so this never touches one; it draws its own rows with the same shared helpers
// aim.cpp's own tab already uses (MenuSection/MenuBindKey, src/hacks/menu/menu.h).
void Menu_Aimbot_Config();   // aimbot tab "configuration" group
