#pragma once
#include "res_spoof_math.h"
#include <d3d9.h>

// Resolution spoofer (botox csgo resolution_spoof.cpp): the real res stays, the finished frame is
// crunched to the spoofed res and scaled back up. "stretch to fill" also gives world, hud and
// crosshair the res's shape (the aspect gates below feed r_aspectratio, vgui_stretch and the
// overlay stretch).

rsp::Vars ResSpoof_Vars();   // the menu state, every aspect consumer reads its gates through it

// The crunch, raw d3d, inside the game's own scene: render.cpp's vgui pass sites, after the
// crosshair (the recorded half of the frame). Once a frame; true = it drew.
bool ResSpoof_Frame(IDirect3DDevice9* dev);
void ResSpoof_NewFrame();    // h_Present end: re-arms the once-a-frame latch
bool ResSpoof_Pending();     // on, and this frame not crunched yet (h_Present runs it then)
void ResSpoof_Destroy();     // Render_ReleaseDeviceObjects: DEFAULT-pool targets

void ResSpoof_FrameStage();  // skins.cpp FSN FRAME_START (main thread): net_graphheight
void ResSpoof_Eject();       // same hook's g_eject branch: net_graphheight back
bool ResSpoof_Holding();     // eject waits on this (thread.cpp): our net_graphheight is live
void ResSpoof_EjectFallback();   // thread.cpp after that wait: no frame came, put it back raw
// around TF2's config writer (flipworld.cpp h_FlipWriteVars): config.cfg gets the player's own
// net_graphheight, never our scaled one
void ResSpoof_CfgWriteBegin();
void ResSpoof_CfgWriteEnd();

// back buffer seen by the last pass, 0 before the first (menu resolution list)
void ResSpoof_ScreenSize(int& w, int& h);
