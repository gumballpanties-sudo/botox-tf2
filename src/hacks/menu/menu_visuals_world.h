#pragma once
#include "../../globals/globals.h"

// Visuals world/camera widgets. menu.cpp's tab bodies are being rebuilt elsewhere, so this file
// never touches them -- the function below is a free-standing drop-in a later pass wires
// into the visuals/screen sub-tab with one call. Plain ImGui widgets only (no FeatureCheckbox/
// SliderF/GroupToggle -- those are menu.cpp file-local helpers this file does not own).
void Menu_Visuals_Camera();

// dlight, sun angle, world/prop texture tint, weather particle colour. Same
// drop-in contract as Menu_Visuals_Camera -- a later pass wires it into the
// visuals/world sub-tab with one call.
void Menu_Visuals_World();
