#pragma once

// Unit A (movement tricks) menu widgets. One function per sub-tab, plain ImGui +
// MenuSection/MenuBindKey (menu.h) -- integration agent wires these into the real
// menu.cpp tab bodies (being rebuilt in parallel, not touched here).
void UnitA_Menu_movement_main();
