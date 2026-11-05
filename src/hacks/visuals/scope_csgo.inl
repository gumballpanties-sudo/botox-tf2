// CS:GO sniper scope glue: csgo's scope drawn through vgui::ISurface around scope_csgo.h, from the
// HUD root's PaintTraverse -- the game's own UI pass, under the whole HUD, once per frame whatever
// mat_queue_mode is, and independent of HudScope (custom HUDs hide it). TF's scope is suppressed.
// SAME FILE in SimpleVisual and fpsoverlay (src/hacks/visuals/scope_csgo.inl) -- edit both.
// Included once, by crosshair.cpp. Before including, define SCSGO_COND_OFF as the project's
// m_nPlayerCond offset int, and SCSGO_OWN_PAINTTRAVERSE if the project has no IPanel::PaintTraverse
// hook (otherwise call ScopeCsgo_PrePaintTraverse first thing in it).
// Needs: g_eject, g_miscHookDepth, g_offFlags/g_offVelocity/g_offMaxspeed, g_scCsgo* + g_scChargeHide globals, GetLocalPlayerIndex/GetClientEntity, IsInGame/IsLocalPlayerAlive, PatternScan,
// GetInterface, DebugLog, MinHook, ImGui (DisplaySize only).
#include "scope_csgo.h"
#include "scope_csgo_tex.h"
#include "vm_csgo.h"
#include <atomic>

// ---- addresses (client.dll x64, 2026-09-17, each sig unique, checked offline) ------------
// CHudScopeCharge::Paint = slot 130 of .?AVCHudScopeCharge@@ (RVA 0x58cdb0): GetLocalTFPlayer -> InCond(1).
static const char* const kScCsChargeSig =
    "40 53 57 48 83 EC 68 48 8B D9 E8 ? ? ? ? 48 8B F8 E8 ? ? ? ? 85 C0 74 ? E8 ? ? ? ? 83 F8 04 75 ? E8 ? ? ? ?";
// CHudScope::Paint = vgui::Panel vtable slot 130 of .?AVCHudScope@@ (RVA 0x58c710). Identified by
// its inlined UpdateRefractTexture (GetRenderContext -> GetViewport) and the surface calls
// DrawSetColor 0x58 / DrawFilledRect 0x60 / DrawSetTexture 0x100 / DrawTexturedPolygon 0x330.
static const char* const kScCsPaintSig =
    "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC F8 00 00 00 0F 29 70 A8 4C 8B F1 48 8B 0D ? ? ? ?";
// CSniperDot::GetRenderingPositions(C_TFPlayer*, Vector&, Vector&, float&) (RVA 0x707080), the first
// call of C_SniperDot::DrawModel (IClientRenderable slot 10): false -> DrawModel returns -1, no dot.
static const char* const kScCsDotPosSig =
    "48 89 5C 24 18 48 89 74 24 20 55 57 41 57 48 8D 6C 24 90 48 81 EC 70 01 00 00 49 8B F1 49 8B F8 4C 8B FA 48 8B D9 48 85 D2";
// g_pMatSystemSurface load at the netgraph DrawColoredText call (same sig netgraph uses).
static const char* const kScCsSurfaceSig = "48 8B 0D ? ? ? ? 48 8D 55 ? 48 89 54 24 ? BF E5 00 00 00";

// ISurface slots, read off the calls in TF's own CHudScope::Paint / Init (MSVC puts the int
// DrawSetColor overload after the Color one, hence 11).
// Push/PopMakeCurrent sit between SetEmbeddedPanel and DrawSetColor in ISurface (sdk-2013 and
// csgo's public/vgui/ISurface.h both), and ISurface derives from IAppSystem, whose Connect /
// Disconnect / QueryInterface / Init take slots 0-3. That +4 is what turns the header's
// DrawSetColor(int) 7 into 11, DrawFilledRect 8 into 12, DrawTexturedPolygon 98 into 102 and
// DrawGetAlphaMultiplier 105 into 109 -- five anchors, so Push 8 / Pop 9 are counted, not guessed.
enum : int {
    kSfPushMakeCurrent = 8, kSfPopMakeCurrent = 9,
    kSfDrawSetColor = 11, kSfDrawFilledRect = 12, kSfDrawSetTextureRGBA = 31, kSfDrawSetTexture = 32,
    kSfCreateNewTextureID = 37, kSfDrawTexturedPolygon = 102,
    kSfDrawSetAlphaMultiplier = 108, kSfDrawGetAlphaMultiplier = 109,   // 109 = movss xmm0,[rcx+18h]; ret
};
struct ScCsVertex { float x, y, u, v; };   // vgui::Vertex_t

static void* s_scCsSurface = nullptr;
static void** ScCs_Vt() { return *static_cast<void***>(s_scCsSurface); }
static void ScCs_SetColor(int r, int g, int b, int a)
{ ((void(__fastcall*)(void*, int, int, int, int))ScCs_Vt()[kSfDrawSetColor])(s_scCsSurface, r, g, b, a); }
static void ScCs_Rect(int x0, int y0, int x1, int y1)
{ ((void(__fastcall*)(void*, int, int, int, int))ScCs_Vt()[kSfDrawFilledRect])(s_scCsSurface, x0, y0, x1, y1); }
static void ScCs_SetTex(int id)
{ ((void(__fastcall*)(void*, int))ScCs_Vt()[kSfDrawSetTexture])(s_scCsSurface, id); }
static void ScCs_Poly(ScCsVertex* v)
{ ((void(__fastcall*)(void*, int, ScCsVertex*, bool))ScCs_Vt()[kSfDrawTexturedPolygon])(s_scCsSurface, 4, v, true); }
static void ScCs_Push(uintptr_t vpanel)
{ ((void(__fastcall*)(void*, uintptr_t, bool))ScCs_Vt()[kSfPushMakeCurrent])(s_scCsSurface, vpanel, false); }
static void ScCs_Pop(uintptr_t vpanel)
{ ((void(__fastcall*)(void*, uintptr_t))ScCs_Vt()[kSfPopMakeCurrent])(s_scCsSurface, vpanel); }

using ScCsPaint_t  = void(__fastcall*)(void* panel);
using ScCsDotPos_t = bool(__fastcall*)(void* dot, void* player, float* attach, float* end, float* size);
static ScCsPaint_t  o_ScCsPaint  = nullptr;
static ScCsDotPos_t o_ScCsDotPos = nullptr;
static uint8_t*     s_scCsPaintAddr = nullptr, *s_scCsDotAddr = nullptr;

// ---- textures: csgo's own art, DXT5 decoded, handed to vgui as procedural RGBA ---------------
// vgui regenerates procedural textures across device resets itself, so the ids live for the DLL.
static int  s_scCsTex[3] = {};   // 0 arc, 1 line blur, 2 lens
static bool s_scCsTexTried = false;

static int ScCs_MakeTex(const unsigned char* dxt5, int w, int h)
{
    const int id = ((int(__fastcall*)(void*, bool))ScCs_Vt()[kSfCreateNewTextureID])(s_scCsSurface, true);
    if (id <= 0) return 0;
    unsigned char* rgba = new unsigned char[w * h * 4];
    ScCs_DecodeDxt5(dxt5, w, h, rgba);
    ((void(__fastcall*)(void*, int, const unsigned char*, int, int, int, bool))ScCs_Vt()[kSfDrawSetTextureRGBA])
        (s_scCsSurface, id, rgba, w, h, 1, true);   // hardwareFilter 1 = linear, like the vmt art
    delete[] rgba;
    return id;
}

static bool ScCs_EnsureTextures()
{
    if (!s_scCsTexTried)
    {
        s_scCsTexTried = true;
        s_scCsTex[0] = ScCs_MakeTex(kScCsArcDxt5,  kScCsArcW,  kScCsArcH);
        s_scCsTex[1] = ScCs_MakeTex(kScCsBlurDxt5, kScCsBlurW, kScCsBlurH);
        s_scCsTex[2] = ScCs_MakeTex(kScCsLensDxt5, kScCsLensW, kScCsLensH);
        DebugLog("sccs: vgui textures arc=%d blur=%d lens=%d", s_scCsTex[0], s_scCsTex[1], s_scCsTex[2]);
    }
    return s_scCsTex[0] > 0;
}

// Nothing to release: the textures are the surface's. Kept so Reset/eject paths stay one call.
void ScopeCsgo_ReleaseTextures() {}

// ---- local player ------------------------------------------------------------------------
static bool ScCs_ReadLocal(ScCsInput& in)
{
    if (SCSGO_COND_OFF <= 0 || g_offFlags <= 0 || g_offVelocity <= 0) return false;
    const int idx = GetLocalPlayerIndex();
    if (idx <= 0) return false;
    void* ent = GetClientEntity(idx);
    const uintptr_t base = reinterpret_cast<uintptr_t>(ent);
    if (base < 0x10000) return false;
    __try {
        const int cond  = *reinterpret_cast<const int*>(base + SCSGO_COND_OFF);
        const int flags = *reinterpret_cast<const int*>(base + g_offFlags);
        const float* v  = reinterpret_cast<const float*>(base + g_offVelocity);
        in.zoomed   = (cond & (1 << 1)) != 0;       // TF_COND_ZOOMED = 1
        in.onGround = (flags & (1 << 0)) != 0;      // FL_ONGROUND
        in.ducking  = (flags & (1 << 1)) != 0;      // FL_DUCKING
        in.speed2D  = std::sqrt(v[0] * v[0] + v[1] * v[1]);
        in.velZ     = v[2];
        if (g_offMaxspeed > 0)
        {
            const float ms = *reinterpret_cast<const float*>(base + g_offMaxspeed);
            if (ms > 1.f) in.maxSpeed = ms;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// ---- draw (vgui, main thread) --------------------------------------------------------------
static ScCsState  s_scCs;
static VmBobState s_scCsBob{};
static double     s_scCsClock = 0.0;
static double     s_scCsLast  = -1.0;          // clock of the last frame step
static std::atomic<long long> s_scCsDrewQpc{ 0 };
static std::atomic<bool>      s_scCsDrawing{ false };   // drew on the last HUD pass
// Ours is in charge this HUD pass (alive, in game, art loaded) whether zoomed or not. HudScope's
// visibility comes from CHud::Think earlier in the frame, our draw reads the cond at paint time:
// on the unzoom frame they disagree and TF's scope flashed. Same vgui thread as h_ScCsPaint.
static bool                   s_scCsOwn = false;

static double ScCs_Now()
{
    static LARGE_INTEGER freq{};
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)freq.QuadPart;
}

static void ScCs_Quad(int tex, float ax, float ay, float au, float av, float bx, float by, float bu, float bv,
                      float cx, float cy, float cu, float cv, float dx, float dy, float du, float dv)
{
    ScCsVertex v[4] = { { ax, ay, au, av }, { bx, by, bu, bv }, { cx, cy, cu, cv }, { dx, dy, du, dv } };
    ScCs_SetTex(tex);
    ScCs_Poly(v);
}

static void ScCs_Draw(const ScCsFrame& f, int W, int H)
{
    const float u1 = 0.5f / 256.f, u2 = 1.f - u1;   // uv1 / uv2
    const float mx = (float)f.middleX, my = (float)f.middleY;
    const float x = (float)f.x, y = (float)f.y;

    if (g_scCsgoLens && s_scCsTex[2] > 0)
    {
        ScCs_SetColor(255, 255, 255, 200);
        ScCs_Quad(s_scCsTex[2],
                  mx + f.xMod, my + f.yMod, u2, u1,  mx - f.xMod, my + f.yMod, u1, u1,
                  mx - f.xMod, my - f.yMod, u1, u2,  mx + f.xMod, my - f.yMod, u2, u2);
    }

    // csgo runs both lines the whole screen and hides the outside half under an opaque surround.
    // Ours can be see-through: there the line blends first and the surround over it, so each end
    // reads as a dark spoke out to the screen edge. Butt the lines to the arc box when see-through
    // -- inside the lens they are csgo's exactly.
    const bool solid = !(g_scCsgoOpacity < 1.f);

    ScCs_SetColor(0, 0, 0, f.blurAlpha);
    if (!f.blurred || s_scCsTex[1] <= 0)
    {
        // DrawLine at width 1 == the 1px rect nStep 0 gives
        const int n = f.thickness / 2;
        ScCs_Rect(solid ? 0 : f.x1, f.y - n, solid ? W + f.offsetX : f.x2, f.y + f.thickness - n);
        ScCs_Rect(f.x - n, solid ? 0 : f.y1, f.x + f.thickness - n, solid ? H + f.offsetY : f.y2);
    }
    else
    {
        const float sb = f.screenBlurWidth;
        const float top = solid ? (float)f.offsetY : (float)f.y1;
        const float bot = solid ? (float)(H + f.offsetY) : (float)f.y2;
        const float lft = solid ? (float)f.offsetX : (float)f.x1;
        const float rgt = solid ? (float)(W + f.offsetX) : (float)f.x2;
        ScCs_Quad(s_scCsTex[1], mx - sb, top, u1, u1,  mx + sb, top, u2, u1,
                                mx + sb, bot, u2, u2,  mx - sb, bot, u1, u2);
        ScCs_Quad(s_scCsTex[1], rgt, my - sb, u1, u2,  rgt, my + sb, u2, u2,
                                lft, my + sb, u2, u1,  lft, my - sb, u1, u1);
    }

    ScCs_SetColor(0, 0, 0, 255);
    const float x1 = (float)f.x1, y1 = (float)f.y1, x2 = (float)f.x2, y2 = (float)f.y2;
    // csgo overlaps its black: the top-right arc grows 1px into its neighbours (seam guard) and the
    // left/right fills run to the screen bottom under the bottom fill. Black on black at full opacity,
    // double alpha below it (darker corners + seam lines) -- so butt the pieces when see-through.
    const float seam  = solid ? 1.f : 0.f;
    const int   sideB = solid ? H : f.y2;
    const int arc = s_scCsTex[0];
    ScCs_Quad(arc, x, y, u1, u1,          x2, y, u2, u1,      x2, y2, u2, u2,      x, y2, u1, u2);       // bottom right
    ScCs_Quad(arc, x - seam, y1, u1, u2,  x2, y1, u2, u2,     x2, y + seam, u2, u1, x - seam, y + seam, u1, u1); // top right
    ScCs_Quad(arc, x1, y, u2, u1,         x, y, u1, u1,       x, y2, u1, u2,       x1, y2, u2, u2);      // bottom left
    ScCs_Quad(arc, x1, y1, u2, u2,        x, y1, u1, u2,      x, y, u1, u1,        x1, y, u2, u1);       // top left

    ScCs_Rect(0, 0, W, f.y1);          // top
    ScCs_Rect(0, f.y2, W, H);          // bottom
    ScCs_Rect(0, f.y1, f.x1, sideB);   // left
    ScCs_Rect(f.x2, f.y1, W, sideB);   // right
}

// Once per frame, from the HUD root's PaintTraverse. Steps the model every frame (zoomed or not, like
// csgo's weapon does) and draws when zoomed. true = drawn.
static bool ScCs_FrameSafe(uintptr_t vpanel)
{
    s_scCsOwn = false;
    if (!g_scCsgo || g_eject || !s_scCsSurface || !g_imguiInit) return false;   // DisplaySize needs imgui
    ScCsInput in;
    if (!IsInGame() || !IsLocalPlayerAlive() || !ScCs_ReadLocal(in))           // dead / spectating -> TF's
    {
        s_scCs = ScCsState{};
        s_scCsLast = -1.0;
        return false;
    }
    s_scCsOwn = !s_scCsTexTried || s_scCsTex[0] > 0;   // art failed -> TF's stays as fallback

    const double now = ScCs_Now();
    float dt = s_scCsLast < 0.0 ? 0.f : (float)(now - s_scCsLast);
    if (dt > 0.1f) dt = 0.1f;   // hitch / load: don't bank seconds of decay in one frame
    s_scCsLast = now;
    s_scCsClock += dt;

    int wi = g_scCsgoWeapon;
    if (wi < 0 || wi >= kScCsWeaponCount) wi = 0;
    const ScCsWeapon& w = kScCsWeapons[wi];

    // scope-in off: the model lives in scoped mode, so zooming never starts from the unscoped penalty
    ScCsInput model = in;
    model.zoomed = in.zoomed || !g_scCsgoScopeIn;
    const float inacc = ScCs_Inaccuracy(s_scCs, model, w, dt, g_scCsgoMoveSens, g_scCsgoAirSens);

    // csgo's own cl_bob* defaults, NOT the viewmodel bob sliders: zeroing those to still the gun
    // must not freeze the scope. Strength is the sway sliders.
    const VmCsBobCfg c;
    float rawVert = 0.f, rawLat = 0.f;
    VmCs_CalcBob(s_scCsBob, in.speed2D, in.onGround, in.maxSpeed, (float)s_scCsClock, dt, c, &rawVert, &rawLat);

    if (!in.zoomed)
    {
        if (g_scCsgoScopeIn) ScCs_Unscoped(s_scCs);
        return false;
    }
    if (!ScCs_EnsureTextures()) return false;

    const ImVec2 ds = ImGui::GetIO().DisplaySize;   // the HUD root is full screen
    const int W = (int)ds.x, H = (int)ds.y;
    if (W <= 0 || H <= 0) return false;
    const float bob = g_scCsgoBob ? g_scCsgoBobScale : 0.f;
    const ScCsFrame f = ScCs_Layout(s_scCs, w, inacc, g_scCsgoZoom ? w.zoomFov2 : w.zoomFov1,
                                    g_scCsgoNormalInacc, g_scCsgoWidth, dt, W, H,
                                    rawLat * bob * g_scCsgoBobLat, rawVert * bob * g_scCsgoBobVert,
                                    g_scCsgoScale);

    // THE TRANSLATE AND CLIP RECT ARE THE LAST PANEL'S, NOT THE SCREEN'S. vgui wraps every
    // Paint in PushMakeCurrent/PopMakeCurrent, and CMatSystemSurface::PopMakeCurrent only
    // restores the entry BELOW the one it pops (MatSystemSurface.cpp:973 "if (top > 0)") --
    // popping the last entry leaves that panel's translate and scissor set, and StartDrawing
    // never pushes a base entry to fall back on. We draw before the HUD root traverses, with
    // the stack empty, so the full-screen fills came out shifted and clipped into whichever
    // small HUD panel painted last: the black box in a corner, and a different box (or none)
    // as the HUD changes from map to map. Push the viewport's own state -- it is the full
    // screen at 0,0 -- exactly as every Paint does, and pop it after.
    ScCs_Push(vpanel);

    // Outside a panel's own Paint the surface still holds the alpha multiplier of whatever panel
    // painted last (a HUD that fades HudScope to alpha 0 leaves 0 here) -- pin ours, put it back.
    // DrawSetColor multiplies every alpha by it, so this one value is the whole scope's opacity.
    using GetA_t = float(__fastcall*)(void*);
    using SetA_t = void(__fastcall*)(void*, float);
    const float oldA = ((GetA_t)ScCs_Vt()[kSfDrawGetAlphaMultiplier])(s_scCsSurface);
    const float opacity = g_scCsgoOpacity < 0.f ? 0.f : (g_scCsgoOpacity > 1.f ? 1.f : g_scCsgoOpacity);
    ((SetA_t)ScCs_Vt()[kSfDrawSetAlphaMultiplier])(s_scCsSurface, opacity);
    ScCs_Draw(f, W, H);
    ((SetA_t)ScCs_Vt()[kSfDrawSetAlphaMultiplier])(s_scCsSurface, oldA);

    ScCs_Pop(vpanel);

    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    s_scCsDrewQpc.store(t.QuadPart, std::memory_order_relaxed);
    s_scCsDrawing.store(true, std::memory_order_relaxed);
    return true;
}

// Every non-drawing path clears the flag here, so unscoping reads as unscoped on that same frame:
// we run at the top of the HUD traverse, the crosshair runs from EndScene after it.
static void ScCs_FrameGuarded(uintptr_t vpanel)
{
    __try {
        if (!ScCs_FrameSafe(vpanel)) s_scCsDrawing.store(false, std::memory_order_relaxed);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { s_scCsOwn = false; s_scCsDrawing.store(false, std::memory_order_relaxed); }
}

// eject drains g_miscHookDepth before freeing trampolines -- count every detour
struct ScCsDepth { ScCsDepth() { ++g_miscHookDepth; } ~ScCsDepth() { --g_miscHookDepth; } };

// Drew on the last HUD pass -> scoped right now. Read by the crosshair (any thread). The 100ms
// staleness check is only for the HUD pass stopping altogether (cl_drawhud 0, load screen): the
// flag alone would then stay true and hold the crosshair off forever.
bool ScopeCsgo_Active()
{
    if (!g_scCsgo || g_eject) return false;
    if (!s_scCsDrawing.load(std::memory_order_relaxed)) return false;
    const long long drew = s_scCsDrewQpc.load(std::memory_order_relaxed);
    if (!drew) return false;
    static LARGE_INTEGER freq{};
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return (t.QuadPart - drew) < freq.QuadPart / 10;
}

// ---- where it draws: right before the HUD root paints ---------------------------------------
// vgui::IPanel::PaintTraverse (VGUI_Panel009 slot 41, GetName 36 -- same counts SimpleVisual's
// crosshair hook uses). CBaseViewport (baseviewport.cpp:160) is the client mode's viewport: every
// HUD element, the scoreboard and the class/team menus are its children. Drawing just before it
// traverses puts the scope over the finished world and under the whole HUD, whatever the HUD file
// does with HudScope (most custom HUDs hide it outright).
// Call this BEFORE the original PaintTraverse runs.
void ScopeCsgo_PrePaintTraverse(void* panels, uintptr_t vpanel)
{
    if (!g_scCsgo || g_eject || !panels) return;
    // A VPANEL is a slot vgui hands out again once the panel behind it is gone, so a cached handle
    // can come back as some small HUD child. Drawing there put the whole scope through THAT panel's
    // clip rect: the full-screen fills came out as one black box in its corner of the screen (the
    // top fill plus the left fill, both clipped -- exactly the reported artifact). Confirm the name
    // on every match: one GetName per frame, and a bad match re-resolves instead of drawing wrong.
    static uintptr_t s_viewport = 0;
    if (s_viewport && vpanel != s_viewport) return;
    using GetName_t = const char* (__fastcall*)(void*, uintptr_t);
    const char* nm = ((GetName_t)(*static_cast<void***>(panels))[36])(panels, vpanel);
    if (!nm || strcmp(nm, "CBaseViewport") != 0)
    {
        if (s_viewport)
        {
            DebugLog("sccs: vpanel %p is no longer CBaseViewport (%s), re-resolving",
                     (void*)vpanel, nm ? nm : "?");
            s_viewport = 0;
        }
        return;
    }
    if (vpanel != s_viewport)
    {
        s_viewport = vpanel;
        DebugLog("sccs: CBaseViewport vpanel %p", (void*)vpanel);
    }
    ScCs_FrameGuarded(vpanel);
}

#ifdef SCSGO_OWN_PAINTTRAVERSE
// For a project with no PaintTraverse hook of its own.
using ScCsPaintTraverse_t = void(__fastcall*)(void*, uintptr_t, bool, bool);
static ScCsPaintTraverse_t o_ScCsPaintTraverse = nullptr;
static uint8_t* s_scCsPtAddr = nullptr;
static void __fastcall h_ScCsPaintTraverse(void* panels, uintptr_t vpanel, bool forceRepaint, bool allowForce)
{
    ScCsDepth _d;
    ScopeCsgo_PrePaintTraverse(panels, vpanel);
    o_ScCsPaintTraverse(panels, vpanel, forceRepaint, allowForce);
}
#endif

// ---- TF's own scope + charge meter ------------------------------------------------------------
// CHudScope::Paint: skipped while ours owns the scope (a HUD that keeps HudScope visible would put
// TF's scope ON TOP of ours -- it is a HUD child), zoomed or not, see s_scCsOwn. CHudScopeCharge::Paint: skipped when asked.
static void __fastcall h_ScCsPaint(void* panel)
{
    ScCsDepth _d;
    if ((g_scCsgo && !g_eject && s_scCsOwn) || ScopeCsgo_Active()) return;
    o_ScCsPaint(panel);
}

using ScCsChargePaint_t = void(__fastcall*)(void* panel);
static ScCsChargePaint_t o_ScCsChargePaint = nullptr;
static uint8_t*          s_scCsChargeAddr  = nullptr;
static void __fastcall h_ScCsChargePaint(void* panel)
{
    ScCsDepth _d;
    if (g_scChargeHide && !g_eject) return;
    o_ScCsChargePaint(panel);
}

static bool ScCs_IsLocal(void* player)
{
    __try {
        const int idx = GetLocalPlayerIndex();
        return idx > 0 && player && player == GetClientEntity(idx);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool __fastcall h_ScCsDotPos(void* dot, void* player, float* attach, float* end, float* size)
{
    ScCsDepth _d;
    if (g_scCsgo && g_scCsgoNoDot && !g_eject && ScCs_IsLocal(player))
        return false;   // DrawModel bails before binding the sprite
    return o_ScCsDotPos(dot, player, attach, end, size);
}

static uint8_t* ScCs_HookAt(uint8_t* a, LPVOID detour, LPVOID* orig, const char* name)
{
    if (!a) { DebugLog("sccs: %s NOT FOUND", name); return nullptr; }
    if (MH_CreateHook(a, detour, orig) != MH_OK) { *orig = nullptr; DebugLog("sccs: %s MH_CreateHook failed", name); return nullptr; }
    MH_EnableHook(a);
    DebugLog("sccs: %s hooked %p", name, a);
    return a;
}

static void* ScCs_ReadPtrAt(uint8_t* a)   // mov rcx,[rip+X] -> *X
{
    __try { return *reinterpret_cast<void**>(a + 7 + *reinterpret_cast<int32_t*>(a + 3)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

void ScopeCsgo_Install(HMODULE client)
{
    if (!client || o_ScCsDotPos) return;

    s_scCsChargeAddr = ScCs_HookAt(PatternScan(client, kScCsChargeSig), (LPVOID)h_ScCsChargePaint,
                                   (LPVOID*)&o_ScCsChargePaint, "CHudScopeCharge::Paint");
    s_scCsDotAddr    = ScCs_HookAt(PatternScan(client, kScCsDotPosSig), (LPVOID)h_ScCsDotPos,
                                   (LPVOID*)&o_ScCsDotPos, "CSniperDot::GetRenderingPositions");

    if (uint8_t* a = PatternScan(client, kScCsSurfaceSig))
        s_scCsSurface = ScCs_ReadPtrAt(a);
    static const char* const kNames[] = { "MatSystemSurface008", "VGUI_Surface031", "VGUI_Surface030" };
    for (int i = 0; !s_scCsSurface && i < 3; ++i)
        s_scCsSurface = GetInterface("vguimatsurface.dll", kNames[i]);
    DebugLog("sccs: surface %p", s_scCsSurface);
    if (!s_scCsSurface) { DebugLog("sccs: no surface, csgo scope off"); return; }

    s_scCsPaintAddr = ScCs_HookAt(PatternScan(client, kScCsPaintSig), (LPVOID)h_ScCsPaint,
                                  (LPVOID*)&o_ScCsPaint, "CHudScope::Paint");
#ifdef SCSGO_OWN_PAINTTRAVERSE
    if (void* panels = GetInterface("vgui2.dll", "VGUI_Panel009"))
        s_scCsPtAddr = ScCs_HookAt(static_cast<uint8_t*>((*static_cast<void***>(panels))[41]),
                                   (LPVOID)h_ScCsPaintTraverse, (LPVOID*)&o_ScCsPaintTraverse, "IPanel::PaintTraverse");
    else DebugLog("sccs: VGUI_Panel009 not found, csgo scope off");
#endif
}

// Eject: TF's scope, charge meter + dot back.
void ScopeCsgo_Restore()
{
    if (s_scCsPaintAddr)  MH_DisableHook(s_scCsPaintAddr);
    if (s_scCsChargeAddr) MH_DisableHook(s_scCsChargeAddr);
    if (s_scCsDotAddr)    MH_DisableHook(s_scCsDotAddr);
#ifdef SCSGO_OWN_PAINTTRAVERSE
    if (s_scCsPtAddr)     MH_DisableHook(s_scCsPtAddr);
#endif
    s_scCsDrewQpc.store(0);
    s_scCsDrawing.store(false);
}
