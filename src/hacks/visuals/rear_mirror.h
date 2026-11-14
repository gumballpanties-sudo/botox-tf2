#pragma once
// botox csgo hacks/visuals/screen/rear_mirror, TF2 port: visuals > world "rear view mirror". CViewRender::RenderView
// renders the view turned 180 into an own target before the main view, a draggable rounded imgui box shows it.
// Every number below is asserted against the installed game by tools/rear_mirror_check.py.

namespace rmslot {
    inline constexpr int vrRenderView         = 6;     // IViewRender: RenderView(const CViewSetup&, int clear, int what)
    inline constexpr int matBackBufferFormat  = 32;    // IMaterialSystem
    inline constexpr int matFindTexture       = 79;
    inline constexpr int matCreateNamedRTEx   = 85;
    inline constexpr int matGetRenderContext  = 98;
    inline constexpr int ctxRelease           = 1;     // IMatRenderContext
    inline constexpr int ctxSetRenderTarget   = 6;
    inline constexpr int ctxPushRtAndViewport = 108;   // the no-arg overload: MSVC reverses the group, 105 = 6 args
    inline constexpr int ctxPopRtAndViewport  = 109;
    inline constexpr int texActualWidth       = 3;     // ITexture
    inline constexpr int texActualHeight      = 4;
    inline constexpr int texIsError           = 15;

    inline constexpr unsigned viewSize        = 0xC8;  // CViewSetup operator=: fields to 0x86, VMatrix at 0x88
    inline constexpr unsigned vsX = 0x00, vsUnscaledX = 0x04, vsY = 0x08, vsUnscaledY = 0x0C;
    inline constexpr unsigned vsWidth = 0x10, vsUnscaledWidth = 0x14, vsHeight = 0x18, vsStereoEye = 0x1C, vsUnscaledHeight = 0x20;
    inline constexpr unsigned vsFov = 0x38, vsAngles = 0x4C, vsSubrect = 0x68, vsAspect = 0x6C, vsOffCenter = 0x70, vsBloom = 0x84;

    inline constexpr unsigned vrTakeFreezeFrame  = 0x600;   // CViewRender: bool m_rbTakeFreezeFrame[3]
    inline constexpr unsigned vrFreezeFrameUntil = 0x604;   // float m_flFreezeFrameUntil
}

void RearMirror_Install();       // PollThread, next to FlipWorld_Install
bool RearMirror_Drawing();       // any hook under RenderView: true inside the mirror pass, botox passes stay out
void RearMirror_Draw();          // render thread, imgui frame (render.cpp BuildImGuiFrame)
void RearMirror_DropTexture();   // render.cpp h_Reset: the target's d3d texture is remade
