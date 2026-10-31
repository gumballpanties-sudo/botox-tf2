#pragma once
#include "../../globals/globals.h"

// Unit D port: sharpen / film grain / vignette(+invert) / invert filter / posterize / deband /
// channel shift.
//
// All of these are "one more pass appended to the already-shared DoPostScreenSpaceEffects
// pipeline" per docs/port/gap.md Unit D -- no new hook. They run where bloom and colour
// correction already run (render.cpp's three call sites), same lifecycle shape as bloom.cpp:
// ScreenFxPasses_Frame draws at most once per frame (first caller whose render target is the
// backbuffer), ScreenFxPasses_NewFrame clears that latch, ScreenFxPasses_Destroy drops the
// DEFAULT-pool scene copies + shaders on device loss.

bool ScreenFxPasses_Frame(IDirect3DDevice9* dev, bool forceBackbuffer = false);
void ScreenFxPasses_NewFrame();
void ScreenFxPasses_Destroy();

// Emphasize + show depth (botox depth_view.cpp). Depth based, so screenfx.cpp's ScreenFx_Frame
// draws it from the game's post slot right after its depth resolve, into target.
void ScreenFxPasses_DepthView(IDirect3DDevice9* dev, IDirect3DSurface9* target,
                              const D3DSURFACE_DESC& desc, IDirect3DTexture9* depth);
