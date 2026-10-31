#include "colorcorrect.h"
#include "shaders/cc_shader.h"   // k_ccShaderBytes -- compiled from cc_shader.hlsl by fxc

// ==== curves =======================================================================
// Monotone cubic Hermite (Fritsch-Carlson). A plain Catmull-Rom through the same points
// overshoots between them, and in a grade an overshoot is a halo or a clipped highlight
// the user never asked for. This one cannot leave the range its own control points span.

void CcResetCurve(CcCurve& c)
{
    c.n = 2;
    c.x[0] = 0.f; c.y[0] = 0.f;
    c.x[1] = 1.f; c.y[1] = 1.f;
}

bool CcCurveIsIdentity(const CcCurve& c)
{
    return c.n == 2 && c.x[0] == 0.f && c.y[0] == 0.f && c.x[1] == 1.f && c.y[1] == 1.f;
}

float CcEval(const CcCurve& c, float x)
{
    const int n = (c.n < 2) ? 2 : (c.n > k_ccMaxPts ? k_ccMaxPts : c.n);
    x = (x < 0.f) ? 0.f : (x > 1.f ? 1.f : x);
    if (x <= c.x[0])     return c.y[0];
    if (x >= c.x[n - 1]) return c.y[n - 1];

    // Secant slopes, then the tangents they imply.
    float d[k_ccMaxPts], m[k_ccMaxPts];
    for (int i = 0; i < n - 1; ++i)
    {
        const float h = c.x[i + 1] - c.x[i];
        d[i] = (h > 1e-6f) ? (c.y[i + 1] - c.y[i]) / h : 0.f;
    }
    m[0] = d[0];
    m[n - 1] = d[n - 2];
    for (int i = 1; i < n - 1; ++i)
        m[i] = (d[i - 1] * d[i] <= 0.f) ? 0.f : (d[i - 1] + d[i]) * 0.5f;

    // Fritsch-Carlson limiter: keep every tangent inside the circle of radius 3 around
    // its segment's secant. This is the step that makes the result monotone.
    for (int i = 0; i < n - 1; ++i)
    {
        if (d[i] == 0.f) { m[i] = 0.f; m[i + 1] = 0.f; continue; }
        const float a = m[i] / d[i], b = m[i + 1] / d[i];
        const float s = a * a + b * b;
        if (s > 9.f)
        {
            const float t = 3.f / sqrtf(s);
            m[i]     = t * a * d[i];
            m[i + 1] = t * b * d[i];
        }
    }

    int i = 0;
    while (i < n - 2 && x > c.x[i + 1]) ++i;
    const float h = c.x[i + 1] - c.x[i];
    if (h <= 1e-6f) return c.y[i + 1];

    const float t  = (x - c.x[i]) / h;
    const float t2 = t * t, t3 = t2 * t;
    const float h00 =  2.f * t3 - 3.f * t2 + 1.f;
    const float h10 =        t3 - 2.f * t2 + t;
    const float h01 = -2.f * t3 + 3.f * t2;
    const float h11 =        t3 -       t2;
    const float y = h00 * c.y[i] + h10 * h * m[i] + h01 * c.y[i + 1] + h11 * h * m[i + 1];
    return (y < 0.f) ? 0.f : (y > 1.f ? 1.f : y);
}

// A point may not cross its neighbours: its x is clamped into the gap between them, so a
// drag never reorders the list. Degenerate (zero-width) segments are impossible too,
// which is what keeps the tangent solve above well conditioned.
inline constexpr float k_ccMinDx = 1.f / 128.f;

void CcMovePoint(CcCurve& c, int i, float x, float y)
{
    if (i < 0 || i >= c.n) return;
    if (y < 0.f) y = 0.f; else if (y > 1.f) y = 1.f;
    c.y[i] = y;

    if (i == 0)          { c.x[0] = 0.f; return; }   // endpoints slide vertically only
    if (i == c.n - 1)    { c.x[i] = 1.f; return; }

    const float lo = c.x[i - 1] + k_ccMinDx;
    const float hi = c.x[i + 1] - k_ccMinDx;
    c.x[i] = (lo >= hi) ? c.x[i] : ((x < lo) ? lo : (x > hi ? hi : x));
}

int CcAddPoint(CcCurve& c, float x, float y)
{
    if (c.n >= k_ccMaxPts) return -1;
    if (x <= c.x[0] || x >= c.x[c.n - 1]) return -1;

    int i = 1;
    while (i < c.n && c.x[i] < x) ++i;          // insert before the first point past x
    if (x - c.x[i - 1] < k_ccMinDx || c.x[i] - x < k_ccMinDx) return -1;

    for (int k = c.n; k > i; --k) { c.x[k] = c.x[k - 1]; c.y[k] = c.y[k - 1]; }
    c.x[i] = x;
    c.y[i] = (y < 0.f) ? 0.f : (y > 1.f ? 1.f : y);
    ++c.n;
    return i;
}

void CcRemovePoint(CcCurve& c, int i)
{
    if (c.n <= 2) return;                       // the two endpoints always stay
    if (i <= 0 || i >= c.n - 1) return;
    for (int k = i; k < c.n - 1; ++k) { c.x[k] = c.x[k + 1]; c.y[k] = c.y[k + 1]; }
    --c.n;
}

void CcResetAll()
{
    g_ccGainR = g_ccGainG = g_ccGainB = 1.f;
    g_ccExposure = 0.f;
    g_ccTemp     = 0.f;
    g_ccHue      = 0.f;
    g_ccSat      = 1.f;
    g_ccContrast = 1.f;
    g_ccLevInBlack  = 0.f; g_ccLevInWhite  = 1.f;
    g_ccGamma       = 1.f;
    g_ccLevOutBlack = 0.f; g_ccLevOutWhite = 1.f;
    g_ccHighlights  = 0.f; g_ccShadows     = 0.f;
    for (int i = 0; i < 4; ++i) CcResetCurve(g_ccCurve[i]);
}

// Exact compares are the right test: every default below is an exact float and any touch
// of a slider moves it off that value. Neutral means the whole pass -- both fullscreen
// passes of it -- is skipped, so a ticked checkbox on default settings costs nothing.
bool CcIsNeutral()
{
    if (g_ccGainR != 1.f || g_ccGainG != 1.f || g_ccGainB != 1.f) return false;
    if (g_ccExposure != 0.f || g_ccTemp != 0.f || g_ccHue != 0.f) return false;
    if (g_ccSat != 1.f || g_ccContrast != 1.f)                    return false;
    if (g_ccLevInBlack != 0.f || g_ccLevInWhite != 1.f)           return false;
    if (g_ccGamma != 1.f)                                         return false;
    if (g_ccLevOutBlack != 0.f || g_ccLevOutWhite != 1.f)         return false;
    if (g_ccHighlights != 0.f || g_ccShadows != 0.f)              return false;
    for (int i = 0; i < 4; ++i) if (!CcCurveIsIdentity(g_ccCurve[i])) return false;
    return true;
}

// ==== device objects ================================================================
static IDirect3DPixelShader9* s_shader   = nullptr;
static IDirect3DTexture9*     s_sceneTex = nullptr;   // backbuffer copy, sampled by the pass
static IDirect3DSurface9*     s_sceneSurf= nullptr;   // level 0 of s_sceneTex
static IDirect3DTexture9*     s_lutTex   = nullptr;   // 256x1 curve + levels-out table
static int  s_texW = 0, s_texH = 0;
static bool s_done = false;                           // graded this frame already
static int  s_lutBakes = 0;

// What the baked LUT was built from. Compared with memcmp, so it is only ever written
// with memcpy -- padding bytes then match too.
struct CcLutKey { CcCurve cv[4]; float outB, outW; };
static CcLutKey s_lutKey;
static bool     s_lutValid = false;

void ColorCorr_NewFrame() { s_done = false; }

void ColorCorr_Destroy()
{
    if (s_lutTex)    { s_lutTex->Release();    s_lutTex    = nullptr; }
    if (s_sceneSurf) { s_sceneSurf->Release(); s_sceneSurf = nullptr; }
    if (s_sceneTex)  { s_sceneTex->Release();  s_sceneTex  = nullptr; }
    if (s_shader)    { s_shader->Release();    s_shader    = nullptr; }
    s_texW = s_texH = 0;
    s_lutValid = false;
}

// Sized to the backbuffer; a resolution change comes through h_Reset, which destroys
// everything, but the size check here also covers a swapchain that changed under us.
static bool CcEnsureDevice(IDirect3DDevice9* dev, IDirect3DSurface9* bb)
{
    D3DSURFACE_DESC desc{};
    if (FAILED(bb->GetDesc(&desc))) return false;

    if (s_shader && s_sceneTex && s_lutTex &&
        s_texW == (int)desc.Width && s_texH == (int)desc.Height)
        return true;

    ColorCorr_Destroy();

    // Bytecode is compiled at build time (cc_shader.hlsl -> cc_shader.h), so there is no
    // d3dx9 to find at runtime and nothing to compile on the first graded frame. Copied
    // into DWORDs because a BYTE array carries no alignment guarantee.
    {
        std::vector<DWORD> code((sizeof(k_ccShaderBytes) + 3) / 4);
        memcpy(code.data(), k_ccShaderBytes, sizeof(k_ccShaderBytes));
        HRESULT hr = dev->CreatePixelShader(code.data(), &s_shader);
        if (FAILED(hr)) return false;
    }

    // StretchRect needs a render-target destination in the default pool. Matching the
    // backbuffer format is what makes the blit a straight copy; X8R8G8B8 is the fallback
    // for the rare backbuffer format that cannot back a render-target texture.
    HRESULT hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET,
                                    desc.Format, D3DPOOL_DEFAULT, &s_sceneTex, nullptr);
    if (FAILED(hr))
        hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET,
                                D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &s_sceneTex, nullptr);
    if (FAILED(hr)) { ColorCorr_Destroy(); return false; }
    if (FAILED(s_sceneTex->GetSurfaceLevel(0, &s_sceneSurf))) { ColorCorr_Destroy(); return false; }

    // DYNAMIC so the rebuild is a DISCARD lock. Default pool, not managed: managed is
    // illegal on a D3D9Ex device and TF2 can be started with -d3d9ex.
    hr = dev->CreateTexture(256, 1, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
                            D3DPOOL_DEFAULT, &s_lutTex, nullptr);
    if (FAILED(hr)) { ColorCorr_Destroy(); return false; }

    s_texW = (int)desc.Width;
    s_texH = (int)desc.Height;
    s_lutValid = false;
    return true;
}

// Curves and levels-out, baked once per change. Master maps first and each channel maps
// the master's output, which is the ordering every photo tool uses.
// Returns false while the table has never been built. A default-pool texture comes back
// from CreateTexture with undefined contents, so drawing before the first successful bake
// would put whatever was in that memory on screen.
static bool CcUpdateLut(IDirect3DDevice9* dev)
{
    (void)dev;
    CcLutKey key;
    memcpy(key.cv, g_ccCurve, sizeof(key.cv));
    key.outB = g_ccLevOutBlack;
    key.outW = g_ccLevOutWhite;
    if (s_lutValid && memcmp(&key, &s_lutKey, sizeof(key)) == 0) return true;

    D3DLOCKED_RECT lr{};
    if (FAILED(s_lutTex->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD))) return s_lutValid;

    // Baked from the snapshot, never from the globals: the menu edits these on the Present
    // thread while this runs on the render thread, and a table built half from one state and
    // half from the next would be stamped with a key that matches neither -- so it would
    // never be rebuilt. Key and contents come from the same bytes.
    const float outRng = key.outW - key.outB;
    uint8_t* row = (uint8_t*)lr.pBits;
    for (int i = 0; i < 256; ++i)
    {
        const float m = CcEval(key.cv[0], i / 255.f);
        for (int ch = 0; ch < 3; ++ch)
        {
            float v = key.outB + CcEval(key.cv[1 + ch], m) * outRng;
            v = (v < 0.f) ? 0.f : (v > 1.f ? 1.f : v);
            row[i * 4 + (2 - ch)] = (uint8_t)(v * 255.f + 0.5f);   // A8R8G8B8 = B,G,R,A
        }
        row[i * 4 + 3] = 255;
    }
    s_lutTex->UnlockRect(0);

    memcpy(&s_lutKey, &key, sizeof(key));
    s_lutValid = true;
    ++s_lutBakes;
    return true;
}

// Rodrigues rotation about the luminance axis.
static void CcHueMat(float theta, float m[3][3])
{
    const float c = cosf(theta), s = sinf(theta), ic = 1.f - c;
    const float lx = 0.213f, ly = 0.715f, lz = 0.072f;
    m[0][0] = c + ic*lx*lx;    m[0][1] = ic*lx*ly - s*lz;  m[0][2] = ic*lx*lz + s*ly;
    m[1][0] = ic*ly*lx + s*lz; m[1][1] = c + ic*ly*ly;     m[1][2] = ic*ly*lz - s*lx;
    m[2][0] = ic*lz*lx - s*ly; m[2][1] = ic*lz*ly + s*lx;  m[2][2] = c + ic*lz*lz;
}

// Saturation and hue are both linear and the original applied them back to back with no
// clamp in between, so one 3x3 does exactly what the two did -- three dots in the shader
// instead of a dot, two blends and three dots.
static void CcColorMat(float m[3][3])
{
    const float L[3] = { 0.2126f, 0.7152f, 0.0722f };
    const float s = g_ccSat;
    float S[3][3];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            S[r][c] = (r == c ? s : 0.f) + (1.f - s) * L[c];

    float H[3][3];
    CcHueMat(g_ccHue * (3.14159265f / 180.f), H);

    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            m[r][c] = H[r][0]*S[0][c] + H[r][1]*S[1][c] + H[r][2]*S[2][c];
}

// Everything the pass touches is saved and put back. DrawPrimitiveUP is in that list for
// a reason most of these hooks miss: D3D9 sets stream source 0 to NULL behind it.
struct CcSavedState
{
    IDirect3DPixelShader9*       ps;
    IDirect3DVertexShader9*      vs;
    IDirect3DVertexDeclaration9* decl;
    IDirect3DBaseTexture9*       tex0;
    IDirect3DBaseTexture9*       tex1;
    IDirect3DVertexBuffer9*      stream;
    UINT   streamOffset, streamStride;
    DWORD  fvf;
    D3DVIEWPORT9 vp;
    DWORD  rs[13];
    DWORD  samp[2][6];
    float  psc[6][4];
};

static const D3DRENDERSTATETYPE k_ccRS[13] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_SEPARATEALPHABLENDENABLE,
    D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE, D3DRS_SCISSORTESTENABLE, D3DRS_SRGBWRITEENABLE,
    D3DRS_COLORWRITEENABLE, D3DRS_STENCILENABLE, D3DRS_FOGENABLE, D3DRS_CLIPPLANEENABLE,
    D3DRS_FILLMODE,
};
static const DWORD k_ccRSVal[13] = {
    FALSE, FALSE, FALSE, FALSE,
    FALSE, D3DCULL_NONE, FALSE, FALSE,
    D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE |
        D3DCOLORWRITEENABLE_ALPHA,
    FALSE, FALSE, 0,
    D3DFILL_SOLID,
};
static const D3DSAMPLERSTATETYPE k_ccSamp[6] = {
    D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER,
    D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE,
};

static void CcSave(IDirect3DDevice9* dev, CcSavedState& s)
{
    dev->GetPixelShader(&s.ps);
    dev->GetVertexShader(&s.vs);
    dev->GetVertexDeclaration(&s.decl);
    dev->GetTexture(0, &s.tex0);
    dev->GetTexture(1, &s.tex1);
    dev->GetStreamSource(0, &s.stream, &s.streamOffset, &s.streamStride);
    dev->GetFVF(&s.fvf);
    dev->GetViewport(&s.vp);
    for (int i = 0; i < 13; ++i) dev->GetRenderState(k_ccRS[i], &s.rs[i]);
    for (int u = 0; u < 2; ++u)
        for (int i = 0; i < 6; ++i) dev->GetSamplerState(u, k_ccSamp[i], &s.samp[u][i]);
    dev->GetPixelShaderConstantF(0, s.psc[0], 6);
}

static void CcRestore(IDirect3DDevice9* dev, CcSavedState& s)
{
    for (int i = 0; i < 13; ++i) dev->SetRenderState(k_ccRS[i], s.rs[i]);
    for (int u = 0; u < 2; ++u)
        for (int i = 0; i < 6; ++i) dev->SetSamplerState(u, k_ccSamp[i], s.samp[u][i]);
    dev->SetViewport(&s.vp);
    dev->SetPixelShaderConstantF(0, s.psc[0], 6);
    dev->SetTexture(0, s.tex0);
    dev->SetTexture(1, s.tex1);
    dev->SetPixelShader(s.ps);
    dev->SetVertexShader(s.vs);
    // SetFVF and SetVertexDeclaration write the same slot, so only the one that was in
    // force goes back: a decl if there was one, otherwise the FVF.
    if (s.decl) dev->SetVertexDeclaration(s.decl); else dev->SetFVF(s.fvf);
    dev->SetStreamSource(0, s.stream, s.streamOffset, s.streamStride);

    if (s.tex0)   s.tex0->Release();
    if (s.tex1)   s.tex1->Release();
    if (s.ps)     s.ps->Release();
    if (s.vs)     s.vs->Release();
    if (s.decl)   s.decl->Release();
    if (s.stream) s.stream->Release();
}

static void CcDraw(IDirect3DDevice9* dev, IDirect3DSurface9* bb)
{
    // One copy out, one draw back. The original went backbuffer -> input -> output RT ->
    // backbuffer; the render target here is already the backbuffer, so the corrected quad
    // is drawn straight into it and a whole fullscreen blit disappears.
    HRESULT hr = dev->StretchRect(bb, nullptr, s_sceneSurf, nullptr, D3DTEXF_NONE);
    if (FAILED(hr))
        return;

    const float W = (float)s_texW, H = (float)s_texH;
    struct Vert { float x, y, z, rhw, u, v; };
    const Vert quad[4] = {
        { -0.5f,    -0.5f,   0.f, 1.f, 0.f, 0.f },
        { W - 0.5f, -0.5f,   0.f, 1.f, 1.f, 0.f },
        { -0.5f,    H - 0.5f,0.f, 1.f, 0.f, 1.f },
        { W - 0.5f, H - 0.5f,0.f, 1.f, 1.f, 1.f },
    };

    CcSavedState st{};
    CcSave(dev, st);

    for (int i = 0; i < 13; ++i) dev->SetRenderState(k_ccRS[i], k_ccRSVal[i]);
    const D3DVIEWPORT9 vp{ 0, 0, (DWORD)s_texW, (DWORD)s_texH, 0.f, 1.f };
    dev->SetViewport(&vp);
    dev->SetVertexShader(nullptr);          // a bound vertex shader would ignore the FVF
    dev->SetVertexDeclaration(nullptr);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    dev->SetPixelShader(s_shader);

    // Textures first, then their sampler states: nothing may re-force SRGBTEXTURE after
    // us. Sampling has to be raw -- an sRGB decode here would grade a different image
    // than the one on screen.
    dev->SetTexture(0, s_sceneTex);
    dev->SetTexture(1, s_lutTex);
    for (int u = 0; u < 2; ++u)
    {
        dev->SetSamplerState(u, D3DSAMP_ADDRESSU,    D3DTADDRESS_CLAMP);
        dev->SetSamplerState(u, D3DSAMP_ADDRESSV,    D3DTADDRESS_CLAMP);
        dev->SetSamplerState(u, D3DSAMP_MAGFILTER,   D3DTEXF_LINEAR);
        dev->SetSamplerState(u, D3DSAMP_MINFILTER,   D3DTEXF_LINEAR);
        dev->SetSamplerState(u, D3DSAMP_MIPFILTER,   D3DTEXF_NONE);
        dev->SetSamplerState(u, D3DSAMP_SRGBTEXTURE, 0);
    }

    const float expo   = expf(g_ccExposure * 0.693147f);                  // 2^EV
    const float inW    = (g_ccLevInWhite > g_ccLevInBlack + 0.001f)
                       ? g_ccLevInWhite : g_ccLevInBlack + 0.001f;
    const float invRng = 1.f / (inW - g_ccLevInBlack);
    const float invGam = (g_ccGamma > 0.001f) ? (1.f / g_ccGamma) : 1.f;

    float m[3][3];
    CcColorMat(m);

    const float c[6][4] = {
        { g_ccGainR * expo, g_ccGainG * expo, g_ccGainB * expo, g_ccContrast },
        { g_ccTemp * 0.15f, g_ccTemp * 0.05f, g_ccTemp * -0.15f, invGam },
        { g_ccLevInBlack, invRng, g_ccHighlights, g_ccShadows },
        { m[0][0], m[0][1], m[0][2], 0.f },
        { m[1][0], m[1][1], m[1][2], 0.f },
        { m[2][0], m[2][1], m[2][2], 0.f },
    };
    dev->SetPixelShaderConstantF(0, c[0], 6);

    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vert));

    CcRestore(dev, st);
}

bool ColorCorr_Frame(IDirect3DDevice9* dev)
{
    if (s_done || !dev || !g_ccEnable) return false;
    if (!IsInGame()) return false;              // the main menu is not graded
    if (CcIsNeutral()) { s_done = true; return false; }

    // Only the scene whose target IS the backbuffer. Reflection, refraction and rendertarget
    // passes go through the same device, and grading those would put the correction inside
    // water reflections and camera monitors.
    IDirect3DSurface9* rt = nullptr;
    IDirect3DSurface9* bb = nullptr;
    bool onBB = false;
    if (SUCCEEDED(dev->GetRenderTarget(0, &rt)) && rt &&
        SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
        onBB = (rt == bb);

    bool graded = false;
    if (onBB && CcEnsureDevice(dev, bb) && CcUpdateLut(dev))
    {
        CcDraw(dev, bb);
        s_done = true;
        graded = true;
    }

    if (rt) rt->Release();
    if (bb) bb->Release();

    return graded;
}
