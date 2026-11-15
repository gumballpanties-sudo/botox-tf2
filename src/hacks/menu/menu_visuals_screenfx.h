#pragma once
#include "../../globals/globals.h"

// Visuals screen fx widgets. Same stance as menu_visuals_world.h: menu.cpp's tab bodies are being
// rebuilt elsewhere, so this is a free-standing drop-in a later pass wires in with one call.
void Menu_Visuals_ScreenFx();
// visuals > images page (botox menu_tab_visuals.cpp case 3): list + the picked image's knobs
void Page_VisualsImages();
