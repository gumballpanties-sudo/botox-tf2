#pragma once
#include "../../globals/globals.h"

// misc HUD's menu widgets. menu.cpp's tab bodies are being rebuilt in parallel, so this file
// never edits them -- the widgets below are plain ImGui, ready for a one-line call from
// whichever Tab_Misc() lands, same shape every other sub-tab function in menu.cpp already is.
void Menu_Misc_Main();
