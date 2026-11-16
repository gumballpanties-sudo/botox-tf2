#include "vgui_stretch.h"
#include "vgui_stretch_math.h"
#include "aspect_stretch_math.h"     // AsScale: the crosshair widens exactly like our ImGui one
#include "res_spoof.h"               // ResSpoof_Vars + rsp:: aspect gates (aspect ratio / stretched res)
#include "../../globals/globals.h"
#include "../hud/multihud.h"         // MultiHud_RebuildPanels: the per-branch OnScreenSizeChanged post
#include "../../render/render.h"     // o_PaintTraverse (the per-panel half), VGui_ResetPanelCache
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <intrin.h>   // _ReturnAddress (GetFullscreenViewport: client callers only)

// Every slot below was read off the shipping x64 dlls (11-16, scratchpad rex.py: RTTI vtable +
// capstone), not counted from headers alone:
//  vguimatsurface CMatSystemSurface (vtable rva c93c8)
//    24 DrawFlushText      text is batched (+0x103a8 count); flushed before the matrix moves
//    38 GetScreenSize      honours the override first (+0x104b0 flag, +0x104b4/b8 size)
//    53 IsCursorVisible    always-visible flag (+0x3a0) || cursor != dc_none (+0x39c)
//     6 GetEmbeddedPanel   104 IsCursorLocked (jmp into the platform helper; 61/62 Unlock/Lock)
//   147 GetFullscreenViewport -> 172: stored viewport if one was set (+0x104d4 h != 0), else
//                          (0, 0, GetScreenSize) = the override
//   render context 39 GetViewport (StartDrawing's own call), IPanel 3 GetPos 4 SetSize 5 GetSize
//                          17 GetChildCount 18 GetChild (multihud.cpp, live)
//    90 GetWorkspaceBounds honours the override too (Frame::MoveToCenterOfScreen centres in it)
//    96 / 97 SurfaceGet/SetCursorPos   12-byte thunks into the platform window helper
//   111 OnScreenSizeChanged (fallback relayout, multihud.cpp notes)
//   114 PaintTraverseEx    StartDrawing (13710: ortho = render context VIEWPORT size, MODEL and VIEW
//                          identity, current mode left at VIEW) -> IPanel::PaintTraverse(root) ->
//                          popups -> FinishDrawing. DrawPanelIn3DSpace (166, world screens) does
//                          not come through it, so 3D vgui screens are never widened.
//   133 ForceScreenSizeOverride(bool, w, h)   field writes only, returns the old flag
//   139 IsScreenSizeOverrideActive
//   155 End3DPaint         model panels (render_texture 1, the default) blit their RT through
//                          DrawSetTexture + a 2D textured rect -> widened with everything else
//  vgui2 CInputSystem (vtable rva 5e7d8)
//    66 InternalCursorMoved(x, y)   the surface's input handler (77b0, IE_AnalogValueChanged
//                          MOUSE_XY) feeds it raw window pixels; it posts SetCursorPosInternal ->
//                          80 UpdateCursorPosInternal (the stored cursor every hit test reads)
//  materialsystem CMatRenderContext (bcab0) / queued (b6c28): 1 Release 2 BeginRender 3 EndRender
//    20 MatrixMode 21 PushMatrix 22 PopMatrix 36 Translate 37 Scale (both cur = cur * op)
//    IMaterialSystem 31 GetBackBufferDimensions, 98 GetRenderContext
//  IPanel (VGUI_Panel009) 20 GetParent 36 GetName
// Nothing in TF2 touches the override outside VR (client_virtualreality.cpp:1435) and
// Panel::ApplySettings, which drops it for one GetScreenSize and puts it back (Panel.cpp:4381).
namespace {

constexpr int kSfDrawFlushText = 24, kSfIsCursorVisible = 53, kSfGetCursorPos = 96, kSfSetCursorPos = 97;
constexpr int kSfIsCursorLocked = 104, kSfGetFullscreenViewport = 147, kSfGetEmbeddedPanel = 6, kSfGetScreenSize = 38;
constexpr int kRcGetViewport = 39;
constexpr int kPnlGetPos = 3, kPnlSetSize = 4, kPnlGetSize = 5, kPnlGetChildCount = 17, kPnlGetChild = 18;
constexpr int kSfOnScreenSizeChanged = 111, kSfPaintTraverseEx = 114, kSfForceScreenSizeOverride = 133;
constexpr int kInInternalCursorMoved = 66;
constexpr int kMsGetBackBufferDimensions = 31, kMsGetRenderContext = 98;
constexpr int kRcRelease = 1, kRcBeginRender = 2, kRcEndRender = 3;
constexpr int kRcMatrixMode = 20, kRcPushMatrix = 21, kRcPopMatrix = 22, kRcTranslate = 36, kRcScale = 37;
constexpr int kMatView = 0, kMatModel = 10;   // MaterialMatrixMode_t
constexpr int kPnlGetParent = 20, kPnlGetName = 36;
constexpr int kEngIsDrawingLoadingImage = 28;

void* Surface()
{
    static const char* const k[] = { "VGUI_Surface031", "VGUI_Surface030", nullptr };
    static void* p = nullptr;
    if (!p) p = GetInterfaceAny("vguimatsurface.dll", k);
    return p;
}

// ---- state ------------------------------------------------------------------------------------
int s_appliedW = 0, s_appliedH = 0;           // the override we set (main thread), 0 = none
std::atomic<float> s_hudScale{ 0.f };         // > 0: narrow layout live, paint + cursor widen by it
std::atomic<float> s_xhairScale{ 0.f };       // > 0: crosshair-only widen (hud stretch off)
std::atomic<int>   s_realW{ 0 }, s_realH{ 0 };
std::atomic<bool>  s_holding{ false };

// ---- paint ------------------------------------------------------------------------------------
using PaintTraverseEx_t = void(__fastcall*)(void*, uintptr_t, bool);
PaintTraverseEx_t o_PaintTraverseEx = nullptr;
thread_local int       t_pass = 0;            // inside ISurface::PaintTraverseEx on this thread
thread_local uintptr_t t_scope = 0;           // the panel whose subtree is widened right now

void __fastcall h_PaintTraverseEx(void* s, uintptr_t panel, bool popups)
{
    MiscHookGuard _g;
    ++t_pass;
    o_PaintTraverseEx(s, panel, popups);
    --t_pass;
}

void* RenderContext()
{
    if (!g_matSystem) return nullptr;
    void* rc = VCall<void*>(g_matSystem, kMsGetRenderContext);   // AddRef'd, like CMatRenderContextPtr
    return reinterpret_cast<uintptr_t>(rc) < 0x10000 ? nullptr : rc;
}

// x' = pivot + (x - pivot) * s on every vgui vertex until Pop, StartDrawing's own bracket
// (BeginRender .. EndRender) around the ops. Text queued so far goes out first under the matrix it
// was laid out for. Push keeps its context reference for Pop (released there): a Pop can never
// miss the context its Push used, so the model stack always comes back even.
thread_local void* t_rc = nullptr;

// StartDrawing's ortho is the pass's render viewport (13710). Only a pass drawing onto the whole
// screen is widened: hud = layout 0..W' onto 0..vw, crosshair = about vw / 2. A vgui pass into a
// render target or a sub-viewport (stereo, panel snapshots) is left exactly as the game drew it.
// layoutW 0 = crosshair mode with scale s.
bool Push(int layoutW, float s)
{
    void* rc = RenderContext();
    if (!rc) return false;
    int vx = -1, vy = -1, vw = 0, vh = 0;
    VCall<void>(rc, kRcGetViewport, &vx, &vy, &vw, &vh);
    const int rw = s_realW.load(std::memory_order_relaxed), rh = s_realH.load(std::memory_order_relaxed);
    const bool screen = vx == 0 && vy == 0 && vw > 0 && std::abs(vw - rw) <= 1 && std::abs(vh - rh) <= 1;
    float pivot = 0.f;
    if (layoutW > 0) s = float(vw) / float(layoutW);
    else pivot = vw * 0.5f;
    if (!screen || !(s > 0.f) || fabsf(s - 1.f) < 0.0005f)
    {
        static bool s_offLogged = false;
        if (!screen && !s_offLogged)
        {
            s_offLogged = true;
            DebugLog("vgui stretch: a vgui pass on viewport %d,%d %dx%d (screen %dx%d) left unwidened", vx, vy, vw, vh, rw, rh);
        }
        VCall<void>(rc, kRcRelease);
        return false;
    }
    static bool s_logged = false;
    if (!s_logged)
    {
        s_logged = true;
        DebugLog("vgui stretch: first widened paint (%s, x%.4f about x=%.0f, pass viewport %d,%d %dx%d)",
                 layoutW > 0 ? "whole vgui pass" : "crosshair", s, pivot, vx, vy, vw, vh);
    }
    t_rc = rc;
    if (void* sf = Surface()) VCall<void>(sf, kSfDrawFlushText);
    vs::Op ops[3];
    const int n = vs::PivotOps(pivot, s, ops);
    VCall<void>(rc, kRcBeginRender);
    VCall<void>(rc, kRcMatrixMode, kMatModel);
    VCall<void>(rc, kRcPushMatrix);
    for (int i = 0; i < n; ++i)
    {
        if (ops[i].kind == 0) VCall<void>(rc, kRcTranslate, ops[i].v, 0.f, 0.f);
        else                  VCall<void>(rc, kRcScale, ops[i].v, 1.f, 1.f);
    }
    VCall<void>(rc, kRcMatrixMode, kMatView);   // StartDrawing leaves VIEW current
    VCall<void>(rc, kRcEndRender);
    return true;
}

void Pop()
{
    if (void* sf = Surface()) VCall<void>(sf, kSfDrawFlushText);   // the subtree's last text, widened
    void* rc = t_rc;
    t_rc = nullptr;
    if (!rc) return;
    VCall<void>(rc, kRcBeginRender);
    VCall<void>(rc, kRcMatrixMode, kMatModel);
    VCall<void>(rc, kRcPopMatrix);
    VCall<void>(rc, kRcMatrixMode, kMatView);
    VCall<void>(rc, kRcEndRender);
    VCall<void>(rc, kRcRelease);
}

// Crosshair-only mode: a crosshair-named panel inside the game hud (CBaseViewport's subtree), so an
// options dialog's crosshair preview is never moved.
bool HudCrosshairPanel(void* panels, uintptr_t vpanel)
{
    if (!vs::IsCrosshairName(VCall<const char*>(panels, kPnlGetName, vpanel))) return false;
    uintptr_t p = VCall<uintptr_t>(panels, kPnlGetParent, vpanel);
    for (int i = 0; p && i < 32; ++i, p = VCall<uintptr_t>(panels, kPnlGetParent, p))
    {
        const char* n = VCall<const char*>(panels, kPnlGetName, p);
        if (n && !strcmp(n, "CBaseViewport")) return true;
    }
    return false;
}

// ---- cursor -----------------------------------------------------------------------------------
// > 0 = map: the narrow layout is live, vgui shows the cursor (a menu, the scoreboard's mouse mode)
// AND has not locked it. Mouse look goes through these same calls (in_mouse.cpp GetMousePos /
// ResetMouse -> vgui input) in real window pixels, and it only accumulates while IsCursorLocked
// (in_mouse.cpp:641): locked = never mapped, whatever the visibility flag says that frame.
float CursorScale(void* surface)
{
    const float k = s_hudScale.load(std::memory_order_relaxed);
    if (k <= 0.f || g_eject || !surface) return 0.f;
    if (!VCall<bool>(surface, kSfIsCursorVisible) || VCall<bool>(surface, kSfIsCursorLocked)) return 0.f;
    return k;
}

void LogCursorOnce(const char* what, int from, int to)
{
    static std::atomic<int> s_logged{ 0 };
    if (s_logged.fetch_add(1) < 3) DebugLog("vgui stretch: cursor %s x %d -> %d", what, from, to);
}

using GetCursor_t = void(__fastcall*)(void*, int*, int*);
using SetCursor_t = void(__fastcall*)(void*, int, int);
using CursorMoved_t = bool(__fastcall*)(void*, int, int);
GetCursor_t   o_GetCursor = nullptr;
SetCursor_t   o_SetCursor = nullptr;
CursorMoved_t o_CursorMoved = nullptr;

void __fastcall h_GetCursor(void* s, int* x, int* y)
{
    MiscHookGuard _g;
    o_GetCursor(s, x, y);
    const float k = CursorScale(s);
    if (k > 0.f && x) *x = vs::ToLayout(*x, k);
}

void __fastcall h_SetCursor(void* s, int x, int y)
{
    MiscHookGuard _g;
    const float k = CursorScale(s);
    // mouse look's recentre (ActivateMouse -> ResetMouse can run before vgui hides the cursor) is
    // real pixels already; anything else is a layout position. A position the real cursor is
    // already inside stays put (in_mouse.cpp:664 Get -> clamp -> Set every frame a menu is up).
    if (k > 0.f && !vs::IsLookCentre(x, y, s_realW.load(std::memory_order_relaxed), s_realH.load(std::memory_order_relaxed)))
    {
        int cx = -1, cy = -1;
        if (o_GetCursor) o_GetCursor(s, &cx, &cy);
        const int sx = vs::SetTarget(x, cx, k);
        if (sx != cx) LogCursorOnce("set", x, sx);
        x = sx;
    }
    o_SetCursor(s, x, y);
}

bool __fastcall h_CursorMoved(void* in, int x, int y)
{
    MiscHookGuard _g;
    const float k = CursorScale(Surface());
    if (k > 0.f) { const int lx = vs::ToLayout(x, k); LogCursorOnce("moved", x, lx); x = lx; }
    return o_CursorMoved(in, x, y);
}

// ---- layout -----------------------------------------------------------------------------------
void RealSize(int& w, int& h)
{
    w = h = 0;
    if (g_matSystem) VCall<void>(g_matSystem, kMsGetBackBufferDimensions, &w, &h);
}

// The panels the per-branch post never reaches: the embedded root and its direct children (client
// root, GameUI root, tools). RebuildPanels walks INTO the viewport's parents without posting to
// them, so Panel::OnScreenSizeChanged's "full screen stays full screen" (Panel.cpp:1017) never runs
// there. Same rule, done here: at (0,0) and old-sized = full screen -> the new GetScreenSize. Without
// it a real resolution change while stretched leaves a root at the stale size, clipping everything
// under it (vgui clips children to the parent) even after stretch is turned off.
void ResizeRoots(int oldW, int oldH)
{
    void* pn = VGui_GetPanelInterface();
    void* s = Surface();
    if (!pn || !s) return;
    int nw = 0, nh = 0;
    VCall<void>(s, kSfGetScreenSize, &nw, &nh);
    const uintptr_t root = VCall<uintptr_t>(s, kSfGetEmbeddedPanel);
    if (nw <= 0 || nh <= 0 || !root) return;
    int resized = 0;
    auto fit = [&](uintptr_t p) {
        int x = -1, y = -1, w = 0, h = 0;
        VCall<void>(pn, kPnlGetPos, p, &x, &y);
        VCall<void>(pn, kPnlGetSize, p, &w, &h);
        if (x == 0 && y == 0 && w == oldW && h == oldH && (w != nw || h != nh))
        {
            VCall<void>(pn, kPnlSetSize, p, nw, nh);
            ++resized;
        }
    };
    fit(root);
    const int n = VCall<int>(pn, kPnlGetChildCount, root);
    for (int i = 0; i < n && i < 256; ++i)
        if (const uintptr_t c = VCall<uintptr_t>(pn, kPnlGetChild, root, i)) fit(c);
    if (resized) DebugLog("vgui stretch: %d root panels %dx%d -> %dx%d", resized, oldW, oldH, nw, nh);
}

// Every panel re-reads GetScreenSize and lays out again, the way a resolution change does it.
// multihud's per-branch post (viewport first, then the other branches, popups, the dashboard
// panels hung on the main menu) when it can; the surface's own resolution-change path otherwise.
// Each one remakes the viewport's panels (CBaseViewport::OnScreenSizeChanged): an open scoreboard
// or class menu closes, as on a real resolution change.
void Relayout(int oldW, int oldH)
{
    ResizeRoots(oldW, oldH);
    const int posted = MultiHud_RebuildPanels(oldW, oldH);
    if (posted < 0)
        if (void* s = Surface()) VCall<void>(s, kSfOnScreenSizeChanged, oldW, oldH);
    VGui_ResetPanelCache();   // render.cpp's FocusOverlayPanel handle: re-found by name next paint
    DebugLog("vgui stretch: relayout from %dx%d (%s)", oldW, oldH,
             posted < 0 ? "surface OnScreenSizeChanged" : "per-branch post");
}

void Apply(const vs::Change& c, int realW)
{
    if (!c.any) return;
    void* s = Surface();
    if (!s) return;
    VCall<bool>(s, kSfForceScreenSizeOverride, c.on, c.w, c.h);
    s_appliedW = c.w;
    s_appliedH = c.h;
    s_hudScale.store(c.on ? vs::Scale(realW, c.w) : 0.f);
    s_holding.store(c.on);
    if (c.on) DebugLog("vgui stretch: hud laid out at %dx%d, widened x%.4f onto %dx%d", c.w, c.h,
                       vs::Scale(realW, c.w), realW, c.h);
    else      DebugLog("vgui stretch: hud back on the real screen");
    Relayout(c.oldW, c.oldH);
}

// ---- eject state --------------------------------------------------------------------------------
// The FSN eject branch (main thread) and thread.cpp's fallback (eject thread) can overlap: the FSN
// hook stays live until MH_DisableHook. One lock over every state change, the tick included.
SRWLOCK s_lock = SRWLOCK_INIT;
struct Lock { Lock() { AcquireSRWLockExclusive(&s_lock); } ~Lock() { ReleaseSRWLockExclusive(&s_lock); } };
int s_pendW = 0, s_pendH = 0;   // override already dropped, the relayout back is still owed

void DropOverrideOwed()
{
    if (void* s = Surface()) VCall<bool>(s, kSfForceScreenSizeOverride, false, 0, 0);
    s_hudScale.store(0.f);
    s_pendW = s_appliedW;
    s_pendH = s_appliedH;
    s_appliedW = s_appliedH = 0;
}

bool Loading() { return g_engine && VCall<bool>(g_engine, kEngIsDrawingLoadingImage); }

// ---- GetFullscreenViewport (ISurface 147 -> 172) -------------------------------------------------
// CHudCrosshair::GetDrawPosition centres the crosshair on it (hud_crosshair.cpp:150), not on
// GetScreenSize. 172 answers (0, 0, GetScreenSize) = the layout while nobody set a fullscreen
// viewport (+0x104d4 == 0), which is the normal path; a stored one (VR, stereo) would be the real
// screen and put the crosshair at W/2 inside a W' layout. Client callers only: the engine sizes
// render viewports from it.
using GetFsVp_t = void(__fastcall*)(void*, int*, int*, int*, int*);
GetFsVp_t o_GetFsVp = nullptr;
uintptr_t s_clientLo = 0, s_clientHi = 0;

void __fastcall h_GetFsVp(void* s, int* x, int* y, int* w, int* h)
{
    MiscHookGuard _g;
    o_GetFsVp(s, x, y, w, h);
    const uintptr_t ret = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const int lw = s_appliedW;
    if (lw <= 0 || s_hudScale.load(std::memory_order_relaxed) <= 0.f || ret < s_clientLo || ret >= s_clientHi) return;
    if (!x || !y || !w || !h || *x != 0 || *y != 0) return;
    if (*w != s_realW.load(std::memory_order_relaxed) || *h != s_realH.load(std::memory_order_relaxed)) return;
    *w = lw;
    static bool s_logged = false;
    if (!s_logged) { s_logged = true; DebugLog("vgui stretch: fullscreen viewport was the real screen for the hud -- answered the layout"); }
}

void ClientRange()
{
    const HMODULE c = GetModuleHandleA("client.dll");
    if (!c) return;
    __try {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(c);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(c) + dos->e_lfanew);
        s_clientLo = reinterpret_cast<uintptr_t>(c);
        s_clientHi = s_clientLo + nt->OptionalHeader.SizeOfImage;
    } __except (EXCEPTION_EXECUTE_HANDLER) { s_clientLo = s_clientHi = 0; }
}

} // namespace

// ---- public -----------------------------------------------------------------------------------
void VguiStretch_Install()
{
    void* s = Surface();
    if (!s) { DebugLog("vgui stretch: VGUI_Surface030 not found -- stretch hud / crosshair off"); return; }
    InstallVTableHook(s, kSfPaintTraverseEx, "vgui stretch: ISurface::PaintTraverseEx",
                      (void*)&h_PaintTraverseEx, (void**)&o_PaintTraverseEx);
    InstallVTableHook(s, kSfGetCursorPos, "vgui stretch: ISurface::SurfaceGetCursorPos",
                      (void*)&h_GetCursor, (void**)&o_GetCursor);
    InstallVTableHook(s, kSfSetCursorPos, "vgui stretch: ISurface::SurfaceSetCursorPos",
                      (void*)&h_SetCursor, (void**)&o_SetCursor);
    if (void* in = GetInterface("vgui2.dll", "VGUI_InputInternal001"))
        InstallVTableHook(in, kInInternalCursorMoved, "vgui stretch: IInputInternal::InternalCursorMoved",
                          (void*)&h_CursorMoved, (void**)&o_CursorMoved);
    else DebugLog("vgui stretch: VGUI_InputInternal001 not found -- menus under stretch hud click off target");
    ClientRange();
    InstallVTableHook(s, kSfGetFullscreenViewport, "vgui stretch: ISurface::GetFullscreenViewport",
                      (void*)&h_GetFsVp, (void**)&o_GetFsVp);
}

void VguiStretch_MainTick()
{
    Lock _l;
    int w = 0, h = 0;
    RealSize(w, h);
    if (w > 0 && h > 0) { s_realW.store(w); s_realH.store(h); }
    // botox aspect_hud() / aspect_crosshair() / aspect_wanted(): the aspect ratio row, or a
    // stretched resolution spoofer res, which turns both on at its own shape (res_spoof_math.h)
    const rsp::Vars av = ResSpoof_Vars();
    const float ratio = rsp::AspectWanted(av);
    // the narrow layout needs every hook: without PaintTraverseEx nothing would widen it back
    const bool hudWanted = rsp::AspectHud(av) && o_PaintTraverseEx && o_PaintTraverse;
    // IVEngineClient 28 IsDrawingLoadingImage (multihud.cpp): the viewport is not remade mid map
    // load, a change waits for the first frame after it (what is live keeps being widened)
    if (!Loading()) Apply(vs::Next(s_appliedW, s_appliedH, hudWanted, w, h, ratio), w);
    // a width-only change (window resize, 16:9 -> 21:9 at the same height) keeps the layout: the
    // widen follows the screen every frame instead
    if (s_appliedW > 0 && w > 0) s_hudScale.store(vs::Scale(w, s_appliedW));

    float xs = 0.f;
    if (rsp::AspectCrosshair(av) && s_appliedW == 0 && w > 0 && h > 0)
    {
        const float r = ratio < 0.5f ? 0.5f : (ratio > 4.f ? 4.f : ratio);
        xs = AsScale((float)w, (float)h, r);
        if (fabsf(xs - 1.f) < 0.001f) xs = 0.f;
    }
    s_xhairScale.store(xs);
}

// Every FSN frame after g_eject until the hooks come off. Mid map load the override goes at once
// and the relayout back waits for a frame past the load (eject waits on Holding for it).
void VguiStretch_Eject()
{
    Lock _l;
    s_xhairScale.store(0.f);
    const bool loading = Loading();
    if (s_appliedW)
    {
        if (loading)
        {
            DropOverrideOwed();
            DebugLog("vgui stretch: eject mid map load -- override off, relayout after the load");
        }
        else
        {
            int w = 0, h = 0;
            RealSize(w, h);
            Apply(vs::Next(s_appliedW, s_appliedH, false, w, h, g_aspectRatioValue), w);
        }
    }
    if (s_pendW && !loading)
    {
        Relayout(s_pendW, s_pendH);
        s_pendW = s_pendH = 0;
    }
    s_holding.store(s_appliedW != 0 || s_pendW != 0);
}

bool VguiStretch_Holding() { return s_holding.load(); }

// thread.cpp, after waiting on Holding. Field writes only (no vgui calls off the main thread); the
// relayout stays owed, so a game frame that still comes before the hooks are off pays it.
void VguiStretch_EjectFallback()
{
    Lock _l;
    s_hudScale.store(0.f);
    s_xhairScale.store(0.f);
    if (!s_holding.load()) return;
    if (s_appliedW) DropOverrideOwed();
    s_holding.store(false);
    DebugLog("vgui stretch: eject saw no game frame past a load -- override dropped, panels keep the "
             "narrow layout until a later frame, a resolution change or hud_reloadscheme");
}

bool VguiStretch_PaintBegin(void* panels, uintptr_t vpanel)
{
    if (t_pass <= 0 || t_scope || g_eject || !panels) return false;
    int layoutW = 0;
    float s = s_hudScale.load(std::memory_order_relaxed);
    if (s > 0.f) layoutW = s_appliedW;
    if (layoutW <= 0)
    {
        s = s_xhairScale.load(std::memory_order_relaxed);
        if (s <= 0.f || !HudCrosshairPanel(panels, vpanel)) return false;
    }
    if (!Push(layoutW, s)) return false;
    t_scope = vpanel;
    return true;
}

void VguiStretch_PaintEnd()
{
    Pop();
    t_scope = 0;
}

bool VguiStretch_RealScreenSize(int& w, int& h)
{
    if (s_hudScale.load(std::memory_order_relaxed) <= 0.f) return false;
    w = s_realW.load(std::memory_order_relaxed);
    h = s_realH.load(std::memory_order_relaxed);
    return w > 0 && h > 0;
}

int VguiStretch_LayoutWidth()
{
    return s_hudScale.load(std::memory_order_relaxed) > 0.f ? s_appliedW : 0;
}
