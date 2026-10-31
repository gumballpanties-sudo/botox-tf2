#include "reflections.h"
#include "../screen/shaders/fx_refl.h"   // k_fxReflBytes -- compiled from fx_refl.hlsl by fxc
#include <vector>

// Screen-space reflections. One ps_3_0 pass, see fx_refl.hlsl for the math and the
// simplifications against botox's five-pass reshade-derived version.

static IDirect3DPixelShader9* s_ps      = nullptr;
static IDirect3DTexture9*     s_sceneTex  = nullptr;
static IDirect3DSurface9*     s_sceneSurf = nullptr;
static int  s_w = 0, s_h = 0;
static bool s_failed = false;

void Reflections_Destroy()
{
    if (s_sceneSurf) { s_sceneSurf->Release(); s_sceneSurf = nullptr; }
    if (s_sceneTex)  { s_sceneTex->Release();  s_sceneTex  = nullptr; }
    if (s_ps)        { s_ps->Release();        s_ps        = nullptr; }
    s_w = s_h = 0;
}

static IDirect3DPixelShader9* ShaderReady(IDirect3DDevice9* dev)
{
    if (s_ps) return s_ps;
    if (s_failed) return nullptr;
    std::vector<DWORD> code((sizeof(k_fxReflBytes) + 3) / 4);
    memcpy(code.data(), k_fxReflBytes, sizeof(k_fxReflBytes));
    if (FAILED(dev->CreatePixelShader(code.data(), &s_ps)) || !s_ps) {
        s_ps = nullptr;
        s_failed = true;
        DebugLog("reflections: the card refused the ssr shader -- feature is off");
        return nullptr;
    }
    DebugLog("reflections: shader built");
    return s_ps;
}

// Same copy-the-backbuffer shape as screenfx_passes.cpp's s_sceneTex: the reflection's hit
// point is a DIFFERENT pixel than the one being drawn, so it has to come from a texture, not
// from the render target currently being written.
static bool Ensure(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc)
{
    if (s_sceneTex && s_w == (int)desc.Width && s_h == (int)desc.Height) return true;

    if (s_sceneSurf) { s_sceneSurf->Release(); s_sceneSurf = nullptr; }
    if (s_sceneTex)  { s_sceneTex->Release();  s_sceneTex  = nullptr; }

    HRESULT hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET,
                                    desc.Format, D3DPOOL_DEFAULT, &s_sceneTex, nullptr);
    if (FAILED(hr))
        hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET,
                                D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &s_sceneTex, nullptr);
    if (FAILED(hr) || FAILED(s_sceneTex->GetSurfaceLevel(0, &s_sceneSurf))) {
        DebugLog("reflections: scene copy target failed 0x%08lx", (unsigned long)hr);
        Reflections_Destroy();
        return false;
    }
    s_w = (int)desc.Width; s_h = (int)desc.Height;
    return true;
}

struct ReflSaved
{
    IDirect3DPixelShader9*       ps;
    IDirect3DVertexShader9*      vs;
    IDirect3DVertexDeclaration9* decl;
    IDirect3DBaseTexture9*       tex[2];
    IDirect3DVertexBuffer9*      stream;
    IDirect3DIndexBuffer9*       indices;
    UINT  streamOffset, streamStride;
    DWORD fvf;
    D3DVIEWPORT9 vp;
    DWORD rs[9];
    DWORD samp[2][6];
    float psc[3][4];   // the shaderapi caches ps constants -- the gun and hud draw after this
};
static const D3DRENDERSTATETYPE k_rsRefl[9] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE,
    D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_CULLMODE, D3DRS_SCISSORTESTENABLE,
    D3DRS_SRGBWRITEENABLE,
};
static const D3DSAMPLERSTATETYPE k_sampRefl[6] = {
    D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER,
    D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE,
};

static void ReflSave(IDirect3DDevice9* dev, ReflSaved& s)
{
    dev->GetPixelShader(&s.ps);
    dev->GetVertexShader(&s.vs);
    dev->GetVertexDeclaration(&s.decl);
    dev->GetStreamSource(0, &s.stream, &s.streamOffset, &s.streamStride);
    dev->GetIndices(&s.indices);
    dev->GetFVF(&s.fvf);
    dev->GetViewport(&s.vp);
    for (int i = 0; i < 9; ++i) dev->GetRenderState(k_rsRefl[i], &s.rs[i]);
    for (int u = 0; u < 2; ++u) {
        dev->GetTexture(u, &s.tex[u]);
        for (int i = 0; i < 6; ++i) dev->GetSamplerState(u, k_sampRefl[i], &s.samp[u][i]);
    }
    dev->GetPixelShaderConstantF(0, s.psc[0], 3);
}
static void ReflRestore(IDirect3DDevice9* dev, ReflSaved& s)
{
    dev->SetPixelShaderConstantF(0, s.psc[0], 3);
    dev->SetPixelShader(s.ps);
    dev->SetVertexShader(s.vs);
    if (s.fvf) dev->SetFVF(s.fvf);
    dev->SetVertexDeclaration(s.decl);
    dev->SetStreamSource(0, s.stream, s.streamOffset, s.streamStride);
    dev->SetIndices(s.indices);
    for (int u = 0; u < 2; ++u) {
        dev->SetTexture(u, s.tex[u]);
        for (int i = 0; i < 6; ++i) dev->SetSamplerState(u, k_sampRefl[i], s.samp[u][i]);
    }
    for (int i = 0; i < 9; ++i) dev->SetRenderState(k_rsRefl[i], s.rs[i]);
    dev->SetViewport(&s.vp);
    if (s.tex[0])  s.tex[0]->Release();
    if (s.tex[1])  s.tex[1]->Release();
    if (s.ps)      s.ps->Release();
    if (s.vs)      s.vs->Release();
    if (s.decl)    s.decl->Release();
    if (s.stream)  s.stream->Release();
    if (s.indices) s.indices->Release();
}

void Reflections_Draw(IDirect3DDevice9* dev, IDirect3DSurface9* target,
                       const D3DSURFACE_DESC& desc, IDirect3DTexture9* depth,
                       const FxViewSetup& view)
{
    if (!dev || !target || !depth || s_failed) return;
    if (!ScreenFx_QuadReady(dev)) return;   // ps_3_0 needs the shared passthrough vs, not FF
    if (!Ensure(dev, desc)) return;

    IDirect3DPixelShader9* ps = ShaderReady(dev);
    if (!ps) return;

    // Depth has to match the frame we are about to march across it -- a stale or
    // mis-sized texture would walk off into whatever the last resolution left behind.
    D3DSURFACE_DESC dd{};
    if (FAILED(depth->GetLevelDesc(0, &dd)) || dd.Width != desc.Width || dd.Height != desc.Height)
        return;

    if (FAILED(dev->StretchRect(target, nullptr, s_sceneSurf, nullptr, D3DTEXF_NONE)))
        return;

    ReflSaved st{};
    ReflSave(dev, st);

    const float aspect   = (s_h > 0) ? (float)s_w / (float)s_h : 1.777778f;
    const float fovClamp = (view.fov < 1.f) ? 1.f : (view.fov > 170.f ? 170.f : view.fov);
    const float tanX     = tanf(fovClamp * 0.5f * 3.14159265f / 180.f);
    const float tanY     = tanX / aspect;

    const float intensity = (g_reflIntensity < 0.f) ? 0.f : (g_reflIntensity > 1.f ? 1.f : g_reflIntensity);
    // 0..1 slider -> 300..3000 unit ray budget. Capped low end keeps a near-zero slider from
    // being a silent "feature off" rather than "very short rays".
    const float maxDist = 300.f + (g_reflDistance < 0.f ? 0.f : (g_reflDistance > 1.f ? 1.f : g_reflDistance)) * 2700.f;

    const float c0[4] = { view.zNear, view.zFar, tanX, tanY };
    const float c1[4] = { 1.f / (float)s_w, 1.f / (float)s_h, intensity, 0.1f };
    // botox "distance": surface cutoff in reshade-linear depth, 0.05 floor = botox's slider min
    const float cutoff  = (g_reflDistance < 0.05f) ? 0.05f : (g_reflDistance > 1.f ? 1.f : g_reflDistance);
    const float c2[4] = { maxDist, cutoff, 0.f, 0.f };
    dev->SetPixelShaderConstantF(0, c0, 1);
    dev->SetPixelShaderConstantF(1, c1, 1);
    dev->SetPixelShaderConstantF(2, c2, 1);

    dev->SetDepthStencilSurface(nullptr);
    ScreenFx_BindQuad(dev);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    // lerp(dest, reflColor, alpha) via the blend unit -- the shader never reads the
    // backbuffer it is writing, only the StretchRect copy, so this is safe to enable.
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    for (int u = 0; u < 2; ++u) {
        dev->SetSamplerState(u, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(u, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(u, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(u, D3DSAMP_SRGBTEXTURE, 0);
    }
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    // an INTZ surface is not filterable and the depth gradient must not be averaged
    dev->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_POINT);

    dev->SetTexture(0, s_sceneTex);
    dev->SetTexture(1, depth);
    dev->SetPixelShader(ps);
    dev->SetRenderTarget(0, target);

    const D3DVIEWPORT9 vp{ 0, 0, (DWORD)s_w, (DWORD)s_h, 0.f, 1.f };
    dev->SetViewport(&vp);
    ScreenFx_DrawQuad(dev, s_w, s_h);

    ReflRestore(dev, st);

    static bool logged = false;
    if (!logged) {
        logged = true;
        DebugLog("reflections: drew %dx%d at the post slot (intensity %.2f distance %.2f)", s_w, s_h,
                 intensity, cutoff);
    }
}
