#pragma once
#include "../../globals/globals.h"

// Movement indicators/detections/recorder widgets. Plain ImGui, no Section()/GroupToggle chrome (menu.cpp tab bodies are
// being rebuilt elsewhere) -- one function per botox sub-tab, wired into the real tab body by
// whoever lands the menu rebrand. See docs/port/gap.md section 4 for the placement:
//   movement/indicators -> Menu_Movement_Indicators
//   movement/indicators -> Menu_Movement_Detections (botox "detections" group)
//   movement/recorder    -> Menu_Movement_Recorder
void Menu_Movement_Indicators();
void Menu_Movement_Detections();
void Menu_Movement_Recorder();
