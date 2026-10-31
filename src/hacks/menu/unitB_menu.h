#pragma once
#include "../../globals/globals.h"

// Unit B menu widgets. Plain ImGui, no Section()/GroupToggle chrome (menu.cpp tab bodies are
// being rebuilt elsewhere) -- one function per botox sub-tab, wired into the real tab body by
// whoever lands the menu rebrand. See docs/port/gap.md section 4 for the placement:
//   movement/indicators -> UnitB_Menu_movement_indicators
//   movement/calculator -> UnitB_Menu_movement_calculator
//   movement/recorder    -> UnitB_Menu_movement_recorder
void UnitB_Menu_movement_indicators();
void UnitB_Menu_movement_calculator();
void UnitB_Menu_movement_recorder();
