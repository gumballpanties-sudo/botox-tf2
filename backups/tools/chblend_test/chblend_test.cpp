// GPU check of the crosshair blend shaders against Krita's OWN blend code.
//
// Builds Krita's KoCompositeOpFunctions.h (pinned commit, fetched by run.sh) against tiny Qt
// stubs, then for every mode id renders a 256x256 grid through the real fx_chblend_vs/ps
// bytecode on a D3D9 HAL device -- same FVF, same ortho, same VPOS lookup the DLL uses --
// and diffs each pixel with what Krita's 8-bit (quint8) path produces for the same src/dst.
//
// Pass = every channel of every pixel within TOL steps of Krita. Krita's integer maths
// rounds differently from float maths, so 1-2 steps is expected noise; a wrong formula,
// swapped arguments or a bad VPOS offset shows up as tens of steps.
//
// A few ids are deliberately NOT Krita's and are checked against their own definition
// (see OwnRef): binary ops, equivalence, invert, copy channel.

#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#include <KoCompositeOpFunctions.h>
#include <KoColorSpaceMaths.cpp>

#include "fx_chblend_vs.h"
#include "fx_chblend_ps.h"
#include "ch_blend_modes.h"

static constexpr int N   = 256;
static constexpr int TOL = 2;

using u8 = quint8;
using namespace KoCompositeOpClampPolicy;

// ---- reference --------------------------------------------------------------------------
template <class F> static void Sep(F f, const u8 s[3], u8 d[3]) { for (int i = 0; i < 3; ++i) d[i] = f(s[i], d[i]); }

template <class Functor> static void Hsl(const u8 s[3], u8 d[3])
{
    float sr = s[0] / 255.f, sg = s[1] / 255.f, sb = s[2] / 255.f;
    float dr = d[0] / 255.f, dg = d[1] / 255.f, db = d[2] / 255.f;
    Functor::composeChannels(sr, sg, sb, dr, dg, db);
    d[0] = KoColorSpaceMaths<float, quint8>::scaleToA(dr);
    d[1] = KoColorSpaceMaths<float, quint8>::scaleToA(dg);
    d[2] = KoColorSpaceMaths<float, quint8>::scaleToA(db);
}

#define C(...) [](u8 a, u8 b) -> u8 { return __VA_ARGS__(a, b); }   // variadic: template commas

// true = handled, d holds the expected result
static bool KritaRef(int m, const u8 s[3], u8 d[3])
{
    switch (m) {
    case   2: Sep(C(cfAddition<u8>), s, d); return true;
    case   3: Sep(C(cfSubtract<u8>), s, d); return true;
    case   4: Sep(C(cfMultiply<u8>), s, d); return true;
    case   5: Sep(C(CFDivide<u8>::composeChannel), s, d); return true;
    case   6: Sep(C(CFInverseSubtract<u8>::composeChannel), s, d); return true;

    case  17: Sep(C(CFColorBurn<u8>::composeChannel), s, d); return true;
    case  18: Sep(C(CFLinearBurn<u8, ClampAsFloatHDR>::composeChannel), s, d); return true;
    case  19: Sep(C(cfDarkenOnly<u8>), s, d); return true;
    case  20: Sep(C(CFGammaDark<u8>::composeChannel), s, d); return true;
    case  21: Hsl<CFDarkerColor<HSYType, u8>>(s, d); return true;
    case  22: Sep(C(cfShadeIFSIllusions<u8>), s, d); return true;
    case  23: Sep(C(cfFogDarkenIFSIllusions<u8>), s, d); return true;
    case  24: Sep(C(cfEasyBurn<u8>), s, d); return true;

    case  25: Sep(C(FunctorWithSDRClampPolicy<CFColorDodge, u8>::composeChannel), s, d); return true;
    case  26: Sep(C(cfAddition<u8>), s, d); return true;
    case  27: Sep(C(cfLightenOnly<u8>), s, d); return true;
    case  28: Sep(C(cfLinearLight<u8>), s, d); return true;
    case  29: Sep(C(cfScreen<u8>), s, d); return true;
    case  30: Sep(C(CFPinLight<u8>::composeChannel), s, d); return true;
    case  31: Sep(C(FunctorWithSDRClampPolicy<CFVividLight, u8>::composeChannel), s, d); return true;
    case  32: Sep(C(CFFlatLight<u8>::composeChannel), s, d); return true;
    case  33: Sep(C(CFHardLight<u8>::composeChannel), s, d); return true;
    case  34: Sep(C(cfSoftLightIFSIllusions<u8>), s, d); return true;
    case  35: Sep(C(cfSoftLightPegtopDelphi<u8>), s, d); return true;
    case  36: Sep(C(CFSoftLight<u8>::composeChannel), s, d); return true;
    case  37: Sep(C(CFSoftLightSvg<u8>::composeChannel), s, d); return true;
    case  38: Sep(C(CFGammaLight<u8>::composeChannel), s, d); return true;
    case  39: Sep(C(CFGammaIllumination<u8>::composeChannel), s, d); return true;
    case  40: Hsl<CFLighterColor<HSYType, u8>>(s, d); return true;
    case  41: Sep(C(cfPNormA<u8>), s, d); return true;
    case  42: Sep(C(cfPNormB<u8>), s, d); return true;
    case  43: Sep(C(cfSuperLight<u8>), s, d); return true;
    case  44: Sep(C(cfTintIFSIllusions<u8>), s, d); return true;
    case  45: Sep(C(cfFogLightenIFSIllusions<u8>), s, d); return true;
    case  46: Sep(C(cfEasyDodge<u8>), s, d); return true;

    case  47: Sep(C(cfModulo<u8>), s, d); return true;
    case  48: Sep(C(cfModuloContinuous<u8>), s, d); return true;
    case  49: Sep(C(cfDivisiveModulo<u8>), s, d); return true;
    case  50: Sep(C(cfDivisiveModuloContinuous<u8>), s, d); return true;
    case  51: Sep(C(cfModuloShift<u8>), s, d); return true;
    case  52: Sep(C(cfModuloShiftContinuous<u8>), s, d); return true;

    case  53: Sep(C(cfDifference<u8>), s, d); return true;
    case  55: Sep(C(CFAdditiveSubtractive<u8>::composeChannel), s, d); return true;
    case  56: Sep(C(CFExclusion<u8>::composeChannel), s, d); return true;
    case  57: Sep(C(cfArcTangent<u8>), s, d); return true;
    case  58: Sep(C(CFNegation<u8>::composeChannel), s, d); return true;

    case  60: Sep(C(CFOverlay<u8>::composeChannel), s, d); return true;
    case  61: Hsl<CFLambertLighting<HSIType, u8>>(s, d); return true;
    case  62: Hsl<CFLambertLightingGamma2_2<HSIType, u8>>(s, d); return true;
    case  63: Sep(C(FunctorWithSDRClampPolicy<CFHardMix, u8>::composeChannel), s, d); return true;
    case  64: Sep(C(CFHardMixPhotoshop<u8>::composeChannel), s, d); return true;
    case  65: Sep(C(CFHardMixSofterPhotoshop<u8>::composeChannel), s, d); return true;
    case  66: Sep(C(CFGrainMerge<u8>::composeChannel), s, d); return true;
    case  67: Sep(C(CFGrainExtract<u8>::composeChannel), s, d); return true;
    case  68: Sep(C(CFParallel<u8>::composeChannel), s, d); return true;
    case  69: Sep(C(cfAllanon<u8>), s, d); return true;
    case  70: Sep(C(CFGeometricMean<u8>::composeChannel), s, d); return true;
    case  71: Sep(C(FunctorWithSDRClampPolicy<CFHardOverlay, u8>::composeChannel), s, d); return true;
    case  72: Sep(C(cfInterpolation<u8>), s, d); return true;
    case  73: Sep(C(cfInterpolationB<u8>), s, d); return true;
    case  74: Sep(C(CFPenumbraA<u8>::composeChannel), s, d); return true;
    case  75: Sep(C(CFPenumbraB<u8>::composeChannel), s, d); return true;
    case  76: Sep(C(cfPenumbraC<u8>), s, d); return true;
    case  77: Sep(C(cfPenumbraD<u8>), s, d); return true;

    case  81: Hsl<CFColor<HSYType, u8>>(s, d); return true;
    case  82: Hsl<CFHue<HSYType, u8>>(s, d); return true;
    case  83: Hsl<CFTint<HSYType, u8>>(s, d); return true;
    case  84: Hsl<CFSaturation<HSYType, u8>>(s, d); return true;
    case  85: Hsl<CFLightness<HSYType, u8>>(s, d); return true;
    case  86: Hsl<CFDecreaseSaturation<HSYType, u8>>(s, d); return true;
    case  87: Hsl<CFIncreaseSaturation<HSYType, u8>>(s, d); return true;
    case  88: Hsl<CFDecreaseLightness<HSYType, u8>>(s, d); return true;
    case  89: Hsl<CFIncreaseLightness<HSYType, u8>>(s, d); return true;

    case 114: Sep(C(cfReflect<u8>), s, d); return true;
    case 115: Sep(C(cfGlow<u8>), s, d); return true;
    case 116: Sep(C(cfFreeze<u8>), s, d); return true;
    case 117: Sep(C(cfHeat<u8>), s, d); return true;
    case 118: Sep(C(CFGleat<u8>::composeChannel), s, d); return true;
    case 119: Sep(C(CFHelow<u8>::composeChannel), s, d); return true;
    case 120: Sep(C(CFReeze<u8>::composeChannel), s, d); return true;
    case 121: Sep(C(CFFrect<u8>::composeChannel), s, d); return true;
    }

    if (m >= 90 && m <= 113) {
        const int op = (m - 90) % 8;
        switch ((m - 90) / 8) {
        #define HSX(T) \
            switch (op) { \
            case 0: Hsl<CFColor<T, u8>>(s, d); break; \
            case 1: Hsl<CFHue<T, u8>>(s, d); break; \
            case 2: Hsl<CFSaturation<T, u8>>(s, d); break; \
            case 3: Hsl<CFLightness<T, u8>>(s, d); break; \
            case 4: Hsl<CFDecreaseSaturation<T, u8>>(s, d); break; \
            case 5: Hsl<CFIncreaseSaturation<T, u8>>(s, d); break; \
            case 6: Hsl<CFDecreaseLightness<T, u8>>(s, d); break; \
            case 7: Hsl<CFIncreaseLightness<T, u8>>(s, d); break; \
            } return true;
        case 0: HSX(HSIType)
        case 1: HSX(HSLType)
        case 2: HSX(HSVType)
        #undef HSX
        }
    }
    return false;
}

// The ids that are deliberately not Krita, checked against what they claim to be.
static bool OwnRef(int m, const u8 s[3], u8 d[3])
{
    for (int i = 0; i < 3; ++i) {
        const int a = s[i], b = d[i];
        int r;
        switch (m) {
        case  7: r = a ^ b; break;
        case  8: r = a | b; break;
        case  9: r = a & b; break;
        case 10: r = 255 & ~(a & b); break;
        case 11: r = 255 & ~(a | b); break;
        case 12: r = 255 & ~(a ^ b); break;
        case 13: r = (255 & ~a) | b; break;      // s -> d
        case 14: r = a & (255 & ~b); break;
        case 15: r = a | (255 & ~b); break;      // d -> s
        case 16: r = (255 & ~a) & b; break;
        case 54: r = 255 - abs(b - a); break;
        case 59: r = 255 - b; break;
        case 78: r = (i == 0) ? a : b; break;
        case 79: r = (i == 1) ? a : b; break;
        case 80: r = (i == 2) ? a : b; break;
        default: return false;
        }
        d[i] = (u8)r;
    }
    return true;
}

// Modulo modes at an exact multiple: Krita's answer there hinges on how float32 rounded k/255
// before its double maths (see fx_chblend_ps.hlsl), so it is reported, not failed.
static bool IsTie(int m, int s, int d)
{
    if (m >= 48 && m <= 50) return s > 0 && d > 0 && d % s == 0;
    if (m == 51 || m == 52) return s > 0 && d > 0 && (s + d) % 255 == 0;
    return false;
}

// ---- gpu --------------------------------------------------------------------------------
struct Vtx { float x, y, z; D3DCOLOR col; float u, v; };

static IDirect3DTexture9* MakeTex(IDirect3DDevice9* dev, const std::vector<u8>& rgb)
{
    IDirect3DTexture9* t = nullptr;
    if (FAILED(dev->CreateTexture(N, N, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr))) return nullptr;
    D3DLOCKED_RECT lr;
    t->LockRect(0, &lr, nullptr, 0);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const u8* p = &rgb[(y * N + x) * 3];
            u8* o = (u8*)lr.pBits + y * lr.Pitch + x * 4;
            o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; o[3] = 255;
        }
    t->UnlockRect(0);
    return t;
}

int main()
{
    if (kChBlendCount != (int)(sizeof(k_fxChBlendPs) / sizeof(k_fxChBlendPs[0]))) {
        printf("FAIL: kChBlend has %d modes, fx_chblend_ps.h has %d shaders -- rerun fx_chblend_build.sh\n",
               kChBlendCount, (int)(sizeof(k_fxChBlendPs) / sizeof(k_fxChBlendPs[0])));
        return 1;
    }

    // src / dst grids. First rows sweep the edges every formula special-cases, the rest random.
    std::vector<u8> src(N * N * 3), dst(N * N * 3);
    static const u8 edge[] = { 0, 1, 2, 63, 64, 126, 127, 128, 129, 191, 192, 253, 254, 255 };
    const int E = (int)sizeof(edge);
    std::mt19937 rng(1234);
    for (int i = 0; i < N * N; ++i) {
        for (int c = 0; c < 3; ++c) {
            if (i < E * E * 4) {
                src[i * 3 + c] = edge[(i / E + c) % E];
                dst[i * 3 + c] = edge[(i + c * 5) % E];
            } else {
                src[i * 3 + c] = (u8)(rng() & 255);
                dst[i * 3 + c] = (u8)(rng() & 255);
            }
        }
    }

    WNDCLASSA wc{}; wc.lpfnWndProc = DefWindowProcA; wc.lpszClassName = "chblend_test";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("chblend_test", "", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, nullptr, nullptr);
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{}; pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN; pp.hDeviceWindow = hwnd;
    IDirect3DDevice9* dev = nullptr;
    if (!d3d || FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                         D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev))) {
        printf("FAIL: no D3D9 HAL device\n");
        return 1;
    }

    IDirect3DTexture9* srcTex = MakeTex(dev, src);
    IDirect3DTexture9* dstTex = MakeTex(dev, dst);
    IDirect3DTexture9* rtTex = nullptr;
    dev->CreateTexture(N, N, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &rtTex, nullptr);
    IDirect3DSurface9* rt = nullptr; rtTex->GetSurfaceLevel(0, &rt);
    IDirect3DSurface9* sys = nullptr;
    dev->CreateOffscreenPlainSurface(N, N, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr);

    std::vector<DWORD> code((sizeof(k_fxChBlendVsBytes) + 3) / 4);
    memcpy(code.data(), k_fxChBlendVsBytes, sizeof(k_fxChBlendVsBytes));
    IDirect3DVertexShader9* vs = nullptr;
    dev->CreateVertexShader(code.data(), &vs);

    // ImGui's own vertices: positions in pixels, uv 0..1, white vertex colour
    const Vtx quad[6] = {
        { 0, 0, 0, 0xFFFFFFFF, 0, 0 }, { N, 0, 0, 0xFFFFFFFF, 1, 0 }, { 0, N, 0, 0xFFFFFFFF, 0, 1 },
        { N, 0, 0, 0xFFFFFFFF, 1, 0 }, { N, N, 0, 0xFFFFFFFF, 1, 1 }, { 0, N, 0, 0xFFFFFFFF, 0, 1 },
    };

    int failed = 0, checked = 0;
    for (int m = 2; m < kChBlendCount; ++m) {
        const FxChBlendPs& sh = k_fxChBlendPs[m];
        std::vector<DWORD> pc((sh.size + 3) / 4);
        memcpy(pc.data(), sh.bytes, sh.size);
        IDirect3DPixelShader9* ps = nullptr;
        if (FAILED(dev->CreatePixelShader(pc.data(), &ps))) { printf("FAIL %3d %s: CreatePixelShader\n", m, kChBlend[m].name); ++failed; continue; }

        dev->SetRenderTarget(0, rt);
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0);
        dev->BeginScene();
        dev->SetRenderState(D3DRS_ZENABLE, FALSE);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->SetRenderState(D3DRS_LIGHTING, FALSE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        dev->SetVertexShader(vs);
        dev->SetPixelShader(ps);
        const float vsc[4] = { (float)N, (float)N, 0, 0 };
        const float psc[4] = { 0, 1.f / N, 1.f / N, 0 };
        dev->SetVertexShaderConstantF(0, vsc, 1);
        dev->SetPixelShaderConstantF(0, psc, 1);
        dev->SetTexture(0, srcTex);
        dev->SetTexture(1, dstTex);
        for (int u = 0; u < 2; ++u) {
            dev->SetSamplerState(u, D3DSAMP_MINFILTER, D3DTEXF_POINT);
            dev->SetSamplerState(u, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            dev->SetSamplerState(u, D3DSAMP_SRGBTEXTURE, FALSE);
        }
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(Vtx));
        dev->EndScene();
        dev->GetRenderTargetData(rt, sys);
        ps->Release();

        D3DLOCKED_RECT lr;
        sys->LockRect(&lr, nullptr, D3DLOCK_READONLY);
        int worst = 0, bad = 0, ties = 0, tieMiss = 0, wx = 0, wy = 0, wc2 = 0;
        u8 wExp[3]{}, wGot[3]{};
        bool own = false;
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                const int i = y * N + x;
                u8 exp[3] = { dst[i * 3], dst[i * 3 + 1], dst[i * 3 + 2] };
                own = OwnRef(m, &src[i * 3], exp);
                if (!own && !KritaRef(m, &src[i * 3], exp)) { printf("FAIL %3d %s: no reference\n", m, kChBlend[m].name); y = N; ++bad; break; }
                const u8* o = (u8*)lr.pBits + y * lr.Pitch + x * 4;
                const u8 got[3] = { o[2], o[1], o[0] };
                for (int c = 0; c < 3; ++c) {
                    const int e = abs((int)got[c] - (int)exp[c]);
                    if (IsTie(m, src[i * 3 + c], dst[i * 3 + c])) { ++ties; if (e > TOL) ++tieMiss; continue; }
                    if (e > TOL) ++bad;
                    if (e > worst) { worst = e; wx = x; wy = y; wc2 = c; memcpy(wExp, exp, 3); memcpy(wGot, got, 3); }
                }
            }
        sys->UnlockRect();
        ++checked;
        const int wi = wy * N + wx;
        printf("%s %3d %-30s %s max %3d  over-tol %5d", bad ? "FAIL" : "ok  ", m, kChBlend[m].name,
               own ? "(own)  " : "(krita)", worst, bad);
        if (ties) printf("  ties %d (%d differ)", ties, tieMiss);
        if (bad) printf("   e.g. s=(%d,%d,%d) d=(%d,%d,%d) want (%d,%d,%d) got (%d,%d,%d) ch %d",
                        src[wi*3], src[wi*3+1], src[wi*3+2], dst[wi*3], dst[wi*3+1], dst[wi*3+2],
                        wExp[0], wExp[1], wExp[2], wGot[0], wGot[1], wGot[2], wc2);
        printf("\n");
        if (bad) ++failed;
    }

    printf("\n%d modes checked, %d failed\n", checked, failed);
    return failed ? 1 : 0;
}
