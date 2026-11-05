#pragma once

// Movement tab menu widgets. One function per sub-tab, plain ImGui +
// MenuSection/MenuBindKey (menu.h) -- integration agent wires these into the real
// menu.cpp tab bodies (being rebuilt in parallel, not touched here).
void Menu_Movement_Main();
