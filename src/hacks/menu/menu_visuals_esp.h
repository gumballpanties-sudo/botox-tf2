#pragma once
// Visuals ESP menu bodies: visuals/esp and visuals/world rows. menu.cpp's tab bodies are being
// rebuilt elsewhere, so these are NOT called from menu.cpp yet -- wire a single call to each
// into Tab_Visuals() once that rebuild lands.
void Menu_Visuals_Esp();
void Menu_Visuals_Buildings();
void Menu_Visuals_WorldEsp();
void Menu_Visuals_Chams();
