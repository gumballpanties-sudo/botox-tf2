#include "res_spoof.h"
#include "../../globals/globals.h"
#include <atomic>
#include <iterator>

// botox csgo hacks/visuals/screen/resolution_spoof.cpp on_present 1:1, minus what TF2 cannot use:
//  - no overlay pass (botox draws its ImGui frame into the low-res target first): our menu and
//    overlays draw at Present, after this, sharp, widened by the aspect stretch (res_spoof_math.h);
//  - no classic crosshair capture (CWeaponCSBase::DrawCrosshair rebuilt at the low res): TF2's
//    crosshair is a texture panel and crunches with the frame like any real low res would;
//  - no debug modes / frame dump (botox dev-only toggles, never in its menu).
// Where: botox runs it at Present ("the back buffer at end_scene is a frame stale"). Here the
// frame is finished at the vgui pass point (render.cpp: the last panel has painted, our crosshair
// is in), which is also the recorded half -- a capture hook owning Present copies the backbuffer
// before our Present runs. So it runs there, inside the game's scene: no BeginScene of its own.

namespace {

// botox k_render_states / k_stage_states / k_sampler_states: exactly what the quad touches, saved
// and put back by hand (never a D3DSBT_ALL state block under the engine's state cache)
constexpr D3DRENDERSTATETYPE k_rs[] = { D3DRS_ZENABLE,           D3DRS_ZWRITEENABLE,      D3DRS_ALPHABLENDENABLE,
                                        D3DRS_ALPHATESTENABLE,   D3DRS_CULLMODE,          D3DRS_LIGHTING,
                                        D3DRS_FOGENABLE,         D3DRS_SCISSORTESTENABLE, D3DRS_STENCILENABLE,
                                        D3DRS_CLIPPING,          D3DRS_SRGBWRITEENABLE,   D3DRS_COLORWRITEENABLE,
                                        D3DRS_CLIPPLANEENABLE,   D3DRS_FILLMODE };
constexpr DWORD k_rsVal[] = { FALSE, FALSE, FALSE, FALSE, D3DCULL_NONE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, 0x0f, 0, D3DFILL_SOLID };

struct StageState { DWORD stage; D3DTEXTURESTAGESTATETYPE type; DWORD value; };
constexpr StageState k_ts[] = {
    { 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1 },   { 0, D3DTSS_COLORARG1, D3DTA_TEXTURE },
    { 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1 },   { 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE },
    { 0, D3DTSS_TEXCOORDINDEX, 0 },             { 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE },
    { 1, D3DTSS_COLOROP, D3DTOP_DISABLE },      { 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE },
};

constexpr D3DSAMPLERSTATETYPE k_ss[] = { D3DSAMP_ADDRESSU,  D3DSAMP_ADDRESSV,    D3DSAMP_MINFILTER,   D3DSAMP_MAGFILTER,
                                         D3DSAMP_MIPFILTER, D3DSAMP_MAXMIPLEVEL, D3DSAMP_SRGBTEXTURE, D3DSAMP_MIPMAPLODBIAS };
constexpr DWORD k_ssVal[] = { D3DTADDRESS_CLAMP, D3DTADDRESS_CLAMP, D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_LINEAR, 0, FALSE, 0 };

static_assert(std::size(k_rs) == std::size(k_rsVal));
static_assert(std::size(k_ss) == std::size(k_ssVal));

struct Saved
{
    IDirect3DVertexShader9*      vs = nullptr;
    IDirect3DPixelShader9*       ps = nullptr;
    IDirect3DVertexDeclaration9* decl = nullptr;
    IDirect3DBaseTexture9*       tex = nullptr;
    IDirect3DSurface9*           rt = nullptr;
    IDirect3DSurface9*           ds = nullptr;
    IDirect3DVertexBuffer9*      stream = nullptr;
    IDirect3DIndexBuffer9*       indices = nullptr;
    UINT  streamOffset = 0, streamStride = 0;
    DWORD fvf = 0;
    D3DVIEWPORT9 vp{};
    RECT  scissor{};
    bool  scissorOk = false;
    DWORD rs[std::size(k_rs)]{};
    DWORD ts[std::size(k_ts)]{};
    DWORD ss[std::size(k_ss)]{};
};

void Save(IDirect3DDevice9* dev, Saved& s)
{
    dev->GetVertexShader(&s.vs);
    dev->GetPixelShader(&s.ps);
    dev->GetVertexDeclaration(&s.decl);
    dev->GetFVF(&s.fvf);
    dev->GetTexture(0, &s.tex);
    dev->GetViewport(&s.vp);
    s.scissorOk = SUCCEEDED(dev->GetScissorRect(&s.scissor));   // SetRenderTarget resets it too
    if (FAILED(dev->GetRenderTarget(0, &s.rt))) s.rt = nullptr;
    if (FAILED(dev->GetDepthStencilSurface(&s.ds))) s.ds = nullptr;
    dev->GetStreamSource(0, &s.stream, &s.streamOffset, &s.streamStride);
    dev->GetIndices(&s.indices);
    for (size_t i = 0; i < std::size(k_rs); ++i) dev->GetRenderState(k_rs[i], &s.rs[i]);
    for (size_t i = 0; i < std::size(k_ts); ++i) dev->GetTextureStageState(k_ts[i].stage, k_ts[i].type, &s.ts[i]);
    for (size_t i = 0; i < std::size(k_ss); ++i) dev->GetSamplerState(0, k_ss[i], &s.ss[i]);
}

void SetQuadState(IDirect3DDevice9* dev)
{
    for (size_t i = 0; i < std::size(k_rs); ++i) dev->SetRenderState(k_rs[i], k_rsVal[i]);
    for (size_t i = 0; i < std::size(k_ts); ++i) dev->SetTextureStageState(k_ts[i].stage, k_ts[i].type, k_ts[i].value);
    for (size_t i = 0; i < std::size(k_ss); ++i) dev->SetSamplerState(0, k_ss[i], k_ssVal[i]);
    dev->SetVertexShader(nullptr);
    dev->SetPixelShader(nullptr);
    dev->SetVertexDeclaration(nullptr);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
}

// DrawPrimitiveUP nulls stream 0 + indices, and source skips the re-bind when its cache matches:
// they go back with everything else. SetFVF and SetVertexDeclaration write the same slot, so only
// the one that was in force is restored.
void Restore(IDirect3DDevice9* dev, Saved& s)
{
    if (s.rt) dev->SetRenderTarget(0, s.rt);   // resets viewport + scissor rect: the saved ones go back after
    if (s.scissorOk) dev->SetScissorRect(&s.scissor);
    dev->SetDepthStencilSurface(s.ds);
    dev->SetStreamSource(0, s.stream, s.streamOffset, s.streamStride);
    dev->SetIndices(s.indices);
    dev->SetVertexShader(s.vs);
    dev->SetPixelShader(s.ps);
    if (s.decl) dev->SetVertexDeclaration(s.decl); else dev->SetFVF(s.fvf);
    dev->SetTexture(0, s.tex);
    dev->SetViewport(&s.vp);
    for (size_t i = 0; i < std::size(k_rs); ++i) dev->SetRenderState(k_rs[i], s.rs[i]);
    for (size_t i = 0; i < std::size(k_ts); ++i) dev->SetTextureStageState(k_ts[i].stage, k_ts[i].type, s.ts[i]);
    for (size_t i = 0; i < std::size(k_ss); ++i) dev->SetSamplerState(0, k_ss[i], s.ss[i]);
    for (IUnknown* held : { (IUnknown*)s.vs, (IUnknown*)s.ps, (IUnknown*)s.decl, (IUnknown*)s.tex, (IUnknown*)s.rt,
                            (IUnknown*)s.ds, (IUnknown*)s.stream, (IUnknown*)s.indices })
        if (held) held->Release();
    s = Saved{};
}

// botox draw_quad: the whole texture onto a w x h target, texel centres on pixel centres.
// Fixed function vertex AND pixel (no shader): legal on every card.
bool DrawQuad(IDirect3DDevice9* dev, IDirect3DTexture9* tex, int w, int h)
{
    if (!tex) return false;
    struct Vtx { float x, y, z, rhw, u, v; };
    const float l = -0.5f, t = -0.5f, r = (float)w - 0.5f, b = (float)h - 0.5f;
    const Vtx q[4] = { { l, t, 0.f, 1.f, 0.f, 0.f }, { r, t, 0.f, 1.f, 1.f, 0.f },
                       { l, b, 0.f, 1.f, 0.f, 1.f }, { r, b, 0.f, 1.f, 1.f, 1.f } };
    dev->SetTexture(0, tex);
    return SUCCEEDED(dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vtx)));
}

// ---- targets (render thread) --------------------------------------------------------------------
struct Level { IDirect3DTexture9* tex = nullptr; IDirect3DSurface9* surf = nullptr; int w = 0, h = 0; };

IDirect3DTexture9* s_capture = nullptr;       // full-res copy of the frame
IDirect3DSurface9* s_captureSurf = nullptr;
bool               s_captureMips = false;     // AUTOGENMIPMAP: the sampler picks the mip that fits
IDirect3DTexture9* s_target = nullptr;        // the spoofed res
IDirect3DSurface9* s_targetSurf = nullptr;
Level              s_chain[8];                // halving steps when the capture has no mips
int                s_chainN = 0;
int s_w = 0, s_h = 0, s_screenBuiltW = 0, s_screenBuiltH = 0;
D3DFORMAT s_fmt = D3DFMT_UNKNOWN;
bool s_done = false;                          // crunched (or decided not to) this frame
std::atomic<int> s_screenW{ 0 }, s_screenH{ 0 };

template <class T> void Drop(T*& p) { if (p) { p->Release(); p = nullptr; } }

void Release()
{
    Drop(s_targetSurf);
    Drop(s_target);
    Drop(s_captureSurf);
    Drop(s_capture);
    s_captureMips = false;
    for (Level& l : s_chain) { Drop(l.surf); Drop(l.tex); l.w = l.h = 0; }
    s_chainN = 0;
    s_w = s_h = s_screenBuiltW = s_screenBuiltH = 0;
    s_fmt = D3DFMT_UNKNOWN;
}

#define RS_WARN_ONCE(...) do { static bool s_w_ = false; if (!s_w_) { s_w_ = true; DebugLog(__VA_ARGS__); } } while (0)

// CreateTexture(AUTOGENMIPMAP) also "succeeds" on a format that cannot autogen (D3DOK_NOAUTOGEN):
// the mips then never fill and the downscale would be one bilinear tap of level 0. Only a plain
// D3D_OK from CheckDeviceFormat (and from the create) counts; anything else takes the halving chain.
bool AutogenOk(IDirect3DDevice9* dev, D3DFORMAT fmt)
{
    IDirect3D9* d3d = nullptr;
    D3DDEVICE_CREATION_PARAMETERS cp{};
    D3DDISPLAYMODE mode{};
    bool ok = false;
    if (SUCCEEDED(dev->GetDirect3D(&d3d)) && d3d)
    {
        if (SUCCEEDED(dev->GetCreationParameters(&cp)) && SUCCEEDED(d3d->GetAdapterDisplayMode(cp.AdapterOrdinal, &mode)))
            ok = d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType, mode.Format,
                                        D3DUSAGE_RENDERTARGET | D3DUSAGE_AUTOGENMIPMAP, D3DRTYPE_TEXTURE, fmt) == D3D_OK;
        d3d->Release();
    }
    return ok;
}

// A build that failed is not retried every frame (2-3 CreateTexture calls under VRAM pressure):
// only once the size / format asked for changes.
struct BuildKey { int sw, sh, w, h; D3DFORMAT fmt; };
BuildKey s_failedKey{ 0, 0, 0, 0, D3DFMT_UNKNOWN };

bool Build(IDirect3DDevice9* dev, int sw, int sh, int w, int h, D3DFORMAT fmt)
{
    Release();
    if (s_failedKey.sw == sw && s_failedKey.sh == sh && s_failedKey.w == w && s_failedKey.h == h && s_failedKey.fmt == fmt)
        return false;
    bool ok = false;
    if (AutogenOk(dev, fmt))
        ok = dev->CreateTexture(sw, sh, 0, D3DUSAGE_RENDERTARGET | D3DUSAGE_AUTOGENMIPMAP, fmt,
                                D3DPOOL_DEFAULT, &s_capture, nullptr) == D3D_OK;
    if (!ok) Drop(s_capture);
    s_captureMips = ok;
    if (!ok)
    {
        ok = SUCCEEDED(dev->CreateTexture(sw, sh, 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT, &s_capture, nullptr));
        RS_WARN_ONCE("resolution spoofer: no autogen mips on the capture, falling back to the halving chain");
    }
    ok = ok && SUCCEEDED(s_capture->GetSurfaceLevel(0, &s_captureSurf)) &&
         SUCCEEDED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT, &s_target, nullptr)) &&
         SUCCEEDED(s_target->GetSurfaceLevel(0, &s_targetSurf));
    if (!ok)
    {
        Release();
        s_failedKey = { sw, sh, w, h, fmt };
        DebugLog("resolution spoofer: CreateTexture( RENDERTARGET ) failed for %dx%d on %dx%d -- off until the res changes", w, h, sw, sh);
        return false;
    }
    s_failedKey = { 0, 0, 0, 0, D3DFMT_UNKNOWN };
    if (!s_captureMips)
    {
        int sizes[8][2];
        const int n = rsp::ChainLevels(sw, sh, w, h, sizes);
        for (int i = 0; i < n; ++i)
        {
            Level& l = s_chain[i];
            if (FAILED(dev->CreateTexture(sizes[i][0], sizes[i][1], 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT, &l.tex, nullptr)) ||
                FAILED(l.tex->GetSurfaceLevel(0, &l.surf)))
            {
                Drop(l.tex);
                RS_WARN_ONCE("resolution spoofer: downsample chain incomplete");
                break;
            }
            l.w = sizes[i][0];
            l.h = sizes[i][1];
            s_chainN = i + 1;
        }
    }
    s_w = w;
    s_h = h;
    s_fmt = fmt;
    s_screenBuiltW = sw;
    s_screenBuiltH = sh;
    DebugLog("resolution spoofer: %dx%d -> %dx%d, mips %d, chain %d", w, h, sw, sh, s_captureMips ? 1 : 0, s_chainN);
    return true;
}

bool BackbufferSize(int& w, int& h)
{
    w = h = 0;
    if (g_matSystem) VCall<void>(g_matSystem, 31 /*IMaterialSystem::GetBackBufferDimensions*/, &w, &h);
    return w > 0 && h > 0;
}

// ---- net graph (botox net_graph_scale, game thread) ---------------------------------------------
// net_graphheight is FCVAR_ARCHIVE: the real SetValue below would put our scaled height into
// config.cfg on quit / options apply, and the next session would scale the scaled one (64 -> 90 ->
// 126 ...). TF2's config writer (CCvarUtilities::WriteVariables, hooked in flipworld.cpp) gets the
// player's own digits for the write, ResSpoof_CfgWriteBegin/End.
ConVar* s_cvNetGraphHeight = nullptr;
rsp::NetGraph s_ng;
std::atomic<bool> s_ngLive{ false };   // our height is in the cvar (eject waits on it)
constexpr unsigned k_cvString = 0x48;  // ConVar parent m_pszString (flipmath.h flipslot::cvString)

ConVar* NetGraphCvar()
{
    if (!s_cvNetGraphHeight && g_cvar) s_cvNetGraphHeight = g_cvar->FindVarWalk("net_graphheight");
    return s_cvNetGraphHeight;
}

// The cvar's live string buffer, null = unreadable (SEH leaf)
char* CvarString(ConVar* cv)
{
    char* s = nullptr;
    __try { s = *reinterpret_cast<char**>(ConVarParent(cv) + k_cvString); }
    __except (EXCEPTION_EXECUTE_HANDLER) { s = nullptr; }
    return s;
}

int CvarStringLen(const char* s)
{
    int n = -1;
    __try { n = (int)strnlen(s, 15); }
    __except (EXCEPTION_EXECUTE_HANDLER) { n = -1; }
    return n;
}

// the player's own digits into the live string, in place; false = left alone
bool WriteOwnDigits(ConVar* cv, int stored, char* saved, int savedCap)
{
    char* s = cv ? CvarString(cv) : nullptr;
    const int len = s ? CvarStringLen(s) : -1;
    if (len <= 0 || len >= savedCap) return false;
    char own[16];
    if (!rsp::CfgDigits(stored, own, sizeof(own), len)) return false;
    __try {
        if (saved) { memcpy(saved, s, (size_t)len); saved[len] = '\0'; }
        memcpy(s, own, strlen(own) + 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

void NetGraphScale(bool enabled, int screenH, int lowH)
{
    ConVar* cv = NetGraphCvar();
    if (!cv) return;
    const int cur = (int)cv->GetFloat();
    const int stored = s_ng.stored;
    const int value = rsp::NetGraphStep(s_ng, enabled, cur, screenH, lowH);
    if (value < 0) return;
    // real SetValue: CNetGraphPanel only re-reads the height in the cvar's change callback
    // (vgui_netgraphpanel.cpp:46 NetgraphFontChangeCallback -> ComputeNetgraphHeight)
    const bool ok = cv->SetValueInt("net_graphheight", value);
    if (!enabled)
    {
        s_ngLive.store(false);
        DebugLog("resolution spoofer: net_graphheight back to %d%s", value, ok ? "" : " (SetValue refused)");
        return;
    }
    if (!ok) return;   // retried next frame (written stays behind)
    s_ng.written = value;
    s_ngLive.store(true);
    DebugLog("resolution spoofer: net_graphheight %d -> %d (spoofed %d lines of %d)%s", s_ng.stored, value, lowH, screenH,
             stored && stored != s_ng.stored ? ", the player changed it while scaled" : "");
}

char s_cfgSaved[16] = {};
bool s_cfgSwapped = false;

} // namespace

rsp::Vars ResSpoof_Vars()
{
    rsp::Vars v;
    v.spoof              = g_resSpoof;
    v.stretch            = g_resSpoofStretch;
    v.width              = g_resSpoofWidth;
    v.height             = g_resSpoofHeight;
    v.aspectEnable       = g_aspectRatioEnable;
    v.aspectRatio        = g_aspectRatioValue;
    v.aspectHud          = g_sfxAspectHud;
    v.aspectCrosshair    = g_sfxAspectCrosshair;
    v.aspectCheatOverlay = g_sfxAspectCheatOverlay;
    v.eject              = g_eject;
    BackbufferSize(v.screenW, v.screenH);   // the stretch only stands while the pass crunches (rsp::Crunches)
    return v;
}

void ResSpoof_CfgWriteBegin()
{
    s_cfgSwapped = false;
    if (!s_ngLive.load() || !s_ng.stored) return;
    s_cfgSwapped = WriteOwnDigits(NetGraphCvar(), s_ng.stored, s_cfgSaved, sizeof(s_cfgSaved));
}

void ResSpoof_CfgWriteEnd()
{
    if (!s_cfgSwapped) return;
    s_cfgSwapped = false;
    char* s = CvarString(NetGraphCvar());
    const int len = s ? CvarStringLen(s) : -1;
    const int back = (int)strlen(s_cfgSaved);
    if (len < 0 || back < len) return;   // the write grew nothing; a mismatch leaves the player's digits
    __try { memcpy(s, s_cfgSaved, (size_t)back + 1); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

bool ResSpoof_Holding() { return s_ngLive.load(); }

// thread.cpp, eject never saw a game frame: the player's height back without the setter (no
// callbacks off the game thread). The int + string read theirs again, so config.cfg is right; the
// open graph keeps our height until its next font change.
void ResSpoof_EjectFallback()
{
    if (!s_ngLive.load() || !s_ng.stored) return;
    if (ConVar* cv = NetGraphCvar())
    {
        WriteOwnDigits(cv, s_ng.stored, nullptr, 16);
        cv->ForceInt(s_ng.stored);
    }
    DebugLog("resolution spoofer: eject saw no game frame -- net_graphheight %d put back raw", s_ng.stored);
    s_ng = rsp::NetGraph{};
    s_ngLive.store(false);
}

void ResSpoof_ScreenSize(int& w, int& h)
{
    w = s_screenW.load(std::memory_order_relaxed);
    h = s_screenH.load(std::memory_order_relaxed);
}

void ResSpoof_NewFrame() { s_done = false; }
bool ResSpoof_Pending()  { return !s_done && g_resSpoof && !g_eject; }

void ResSpoof_Destroy()
{
    Release();
    s_failedKey = { 0, 0, 0, 0, D3DFMT_UNKNOWN };   // a reset / new device may well build it
}

bool ResSpoof_Frame(IDirect3DDevice9* dev)
{
    if (s_done || !dev) return false;
    const rsp::Vars v = ResSpoof_Vars();
    if (!rsp::Enabled(v))
    {
        s_done = true;
        if (s_target || s_capture) Release();
        return false;
    }

    // only the scene whose target IS the backbuffer (colorcorrect.cpp: reflections, monitors and
    // render-target passes run through the same device)
    IDirect3DSurface9* rt = nullptr;
    IDirect3DSurface9* bb = nullptr;
    bool onBB = false;
    if (SUCCEEDED(dev->GetRenderTarget(0, &rt)) && rt &&
        SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
        onBB = rt == bb;
    if (rt) rt->Release();
    if (!onBB) { if (bb) bb->Release(); return false; }
    s_done = true;

    D3DSURFACE_DESC desc{};
    if (FAILED(bb->GetDesc(&desc))) { bb->Release(); return false; }
    const int sw = (int)desc.Width, sh = (int)desc.Height;
    s_screenW.store(sw);
    s_screenH.store(sh);

    static bool s_announced = false;
    if (!s_announced) { s_announced = true; DebugLog("resolution spoofer: pass reached (vgui point, %dx%d)", sw, sh); }

    int w = 0, h = 0;
    rsp::TargetSize(v, sw, sh, w, h);
    if (!rsp::CrunchesScreen(w, h, sw, sh))
    {
        Release();
        bb->Release();
        RS_WARN_ONCE("resolution spoofer: target is not smaller than the screen");
        return false;
    }

    if (!s_targetSurf || s_w != w || s_h != h || s_fmt != desc.Format || s_screenBuiltW != sw || s_screenBuiltH != sh)
        if (!Build(dev, sw, sh, w, h, desc.Format)) { bb->Release(); return false; }

    if (FAILED(dev->StretchRect(bb, nullptr, s_captureSurf, nullptr, D3DTEXF_NONE)))
    {
        bb->Release();
        RS_WARN_ONCE("resolution spoofer: back buffer copy refused");
        return false;
    }
    if (s_captureMips) s_capture->GenerateMipSubLevels();

    Saved st;
    Save(dev, st);
    SetQuadState(dev);
    dev->SetDepthStencilSurface(nullptr);

    // capture -> (halving chain) -> target, each step a quad onto the next smaller surface
    IDirect3DTexture9* source = s_capture;
    bool stepped = true;
    for (int i = 0; i < s_chainN && stepped; ++i)
    {
        if (FAILED(dev->SetRenderTarget(0, s_chain[i].surf)) || !DrawQuad(dev, source, s_chain[i].w, s_chain[i].h))
        {
            RS_WARN_ONCE("resolution spoofer: chain step draw failed");
            stepped = false;
            break;
        }
        source = s_chain[i].tex;
    }
    if (stepped && (FAILED(dev->SetRenderTarget(0, s_targetSurf)) || !DrawQuad(dev, source, w, h)))
    {
        RS_WARN_ONCE("resolution spoofer: target draw failed");
        stepped = false;
    }

    // target -> the whole backbuffer: the low res, scaled back up
    bool drawn = false;
    if (stepped && SUCCEEDED(dev->SetRenderTarget(0, bb)))
    {
        const D3DVIEWPORT9 full{ 0, 0, desc.Width, desc.Height, 0.f, 1.f };
        dev->SetViewport(&full);
        drawn = DrawQuad(dev, s_target, sw, sh);
        if (!drawn) RS_WARN_ONCE("resolution spoofer: quad draw failed");
    }

    Restore(dev, st);
    bb->Release();
    return drawn;
}

void ResSpoof_FrameStage()
{
    const rsp::Vars v = ResSpoof_Vars();
    int sw = 0, sh = 0, w = 0, h = 0;
    const bool live = rsp::Enabled(v) && g_resSpoofNetGraph && BackbufferSize(sw, sh);
    if (live) rsp::TargetSize(v, sw, sh, w, h);
    NetGraphScale(live && rsp::CrunchesScreen(w, h, sw, sh), sh, h);
}

void ResSpoof_Eject() { NetGraphScale(false, 0, 0); }
