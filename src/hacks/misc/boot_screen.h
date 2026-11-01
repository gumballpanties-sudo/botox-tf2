#pragma once
#include "../../globals/globals.h"

// botox injection splash, ported from csgo-sdk hacks/misc/boot_screen.cpp. Draws itself
// from the first hooked frame until the inject steps finish, then is gone for the session.
void RenderBootScreen();

// botox boot_screen_fade_others: between ImGui::Render and the draw, scales every vertex
// alpha except the splash's own by the reveal ramp -- the overlay fades in after the splash.
void BootScreen_FadeOthers(ImDrawData* dd);
