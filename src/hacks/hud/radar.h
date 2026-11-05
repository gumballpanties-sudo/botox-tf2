#pragma once

// csgo radar (radar.cpp, CS:GO panorama csgo_hudradar.cpp). Game thread half: aim/radar_aim.cpp.
void RenderRadar();             // render.cpp BuildImGuiFrame, background list
void Radar_ReleaseTextures();   // render.cpp h_Reset: the overview texture is DEFAULT-pool
