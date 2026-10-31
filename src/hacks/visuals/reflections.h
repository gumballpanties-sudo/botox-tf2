#pragma once
#include "../../globals/globals.h"
#include "../screen/screenfx.h"   // FxViewSetup

// Screen-space reflections -- ported from botox's hacks/visuals/screen/reflections.cpp
// (single-pass version, see fx_refl.hlsl). Draws into target, which is already the bound
// render target screenfx.cpp's own AO/DoF/frame-blend passes write into.
void Reflections_Draw(IDirect3DDevice9* dev, IDirect3DSurface9* target,
                       const D3DSURFACE_DESC& desc, IDirect3DTexture9* depth,
                       const FxViewSetup& view);
void Reflections_Destroy();   // device reset / eject
