#include "radar.h"
#include "radar_snap.h"
#include "gif_tex.h"
#include "../../globals/globals.h"
#include "../../render/dpi.h"
#include "../../dependencies/imgui/imgui_internal.h"   // GImGui->HoveredWindow, TexUvWhitePixel
#include <cfloat>
#include <cmath>
#include <string>

// ============================================================================================
// csgo radar, render half. CS:GO's panorama radar (csgo_hudradar.cpp + hudradar.xml/.css) drawn
// with ImGui: the 300 px #Radar panel at 1080p layout px, top left (.hud-radar margin-top 20 +
// #Radar y 40), scaled by screen height like panorama and by cl_hud_radar_scale.
//   round   250 px black disc, the overview clipped to it, rotating with the view
//           (cl_radar_rotate), zoomed by cl_radar_scale, player always centred, the 254 px
//           radial vignette, north arrow; icons past the rim become off-map arrows
//   square  280 px black box with the whole 1024 overview fitted to 290, no rotation
// The overview is the per-map world bake from aim/radar_aim.cpp; every icon is CS:GO's own art
// (pak01 materials/panorama/images, botox-assets radar/).
// ============================================================================================

namespace
{
    // svgs rasterised at 4x their css size; the washed icons (on-map dot, enemy diamond, X,
    // off-map arrow) are whitened: panorama wash-color FF replaces rgb and keeps alpha, which a
    // white icon times the ImGui vertex colour reproduces
    constexpr const char* kArtBase = "https://raw.githubusercontent.com/gumballpanties-sudo/botox-assets/main/radar/";
    constexpr const char* kArt[rdr::IC_COUNT] = {
        "icon-on-map.png", "icon-enemy-on-map.png", "icon-direction-indicator.png",
        "icon-ct-ghost.png", "icon-t-ghost.png", "icon-enemy-ghost.png",
        "icon-radar-bomb-above.png", "icon-radar-bomb-below.png",
        "map_death.png", "map_enemy_offmap.png", "map_view_angle.png", "map_view_selected.png",
        "direction.png",
    };
    GifTex* s_art[rdr::IC_COUNT] = {};
    double  s_artRetry[rdr::IC_COUNT] = {};

    ImTextureID Art(int i)
    {
        // pending download / missing file: ask again once a second, not on every quad
        if (!s_art[i] && ImGui::GetTime() >= s_artRetry[i])
        {
            s_artRetry[i] = ImGui::GetTime() + 1.0;
            s_art[i] = GifTex_GetFile((std::string(kArtBase) + kArt[i]).c_str());
        }
        return s_art[i] ? GifTex_Frame(s_art[i], 0.0) : ImTextureID(0);
    }

    // hudradar.css per icon: size, offset from the package centre, opacity, wash, img-shadow
    enum { WASH_NONE, WASH_TEAM, WASH_GREY };
    enum { SHADOW_NONE, SHADOW_BLACK, SHADOW_RED };
    struct ArtSpec { float w, h, ox, oy, opacity; int wash, shadow; };
    constexpr ArtSpec kSpec[rdr::IC_COUNT] = {
        { 11.f, 11.f,   0.f,   0.f, 1.f,   WASH_TEAM, SHADOW_BLACK },   // .PI_OnMap
        { 13.f, 13.f,   0.f,   0.f, 0.95f, WASH_TEAM, SHADOW_RED },     // .PI_EnemyOnMap
        { 11.f, 3.88f,  0.f,  -8.f, 1.f,   WASH_NONE, SHADOW_NONE },    // #DirectionalIndicator: 11x20 box at y 32, 17x6 art fitted
        {  9.f, 15.f,   0.f,   0.f, 0.8f,  WASH_NONE, SHADOW_NONE },    // .PI_Ghost
        {  9.f, 15.f,   0.f,   0.f, 0.8f,  WASH_NONE, SHADOW_NONE },
        {  9.f, 15.f,   0.f,   0.f, 0.8f,  WASH_NONE, SHADOW_NONE },
        { 13.f,  7.f,  -1.f,   8.f, 0.65f, WASH_GREY, SHADOW_NONE },    // .PI_Above
        { 13.f,  7.f,  -1.f,  -8.f, 0.65f, WASH_GREY, SHADOW_NONE },    // .PI_Below
        { 19.f, 19.f,   0.f,   0.f, 1.f,   WASH_TEAM, SHADOW_NONE },    // .PI_Death
        { 22.f, 22.f,   0.f,  -1.f, 0.8f,  WASH_TEAM, SHADOW_NONE },    // .PI_OffMap
        { 128.f, 64.f,  0.f, -30.f, 0.08f, WASH_NONE, SHADOW_NONE },    // #ViewFrustrum: y -12 in the 100 px package
        { 42.f, 36.f,  -1.f, -10.f, 1.f,   WASH_NONE, SHADOW_NONE },    // #Selected
        { 10.f, 14.f,   0.f,   0.f, 1.f,   WASH_NONE, SHADOW_NONE },    // #DirectionArrow
    };

    // ---- overview texture --------------------------------------------------------------------
    IDirect3DTexture9* s_mapTex = nullptr;
    uint32_t s_mapId = 0;
    bool s_mapGrid = false;   // the uploaded texture carries the grid
    bool s_mapFailLogged = false;

    void UploadMap()
    {
        const uint32_t id = g_radarImageId.load();
        if (!id || (s_mapTex && s_mapId == id && s_mapGrid == g_hudRadarGrid) || !g_d3dDev) return;

        std::lock_guard<std::mutex> lock(g_radarImageLock);
        const RadarImage& img = g_radarImage;
        if (img.id != id || img.n <= 0 || img.px.size() != size_t(img.n) * img.n) return;
        if (s_mapTex)
        {
            D3DSURFACE_DESC d{};
            if (FAILED(s_mapTex->GetLevelDesc(0, &d)) || int(d.Width) != img.n) { s_mapTex->Release(); s_mapTex = nullptr; }
        }
        // DYNAMIC + DEFAULT: MANAGED does not exist on this engine's d3d9ex device (gif_tex.cpp)
        if (!s_mapTex && (FAILED(g_d3dDev->CreateTexture(img.n, img.n, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
                                                         D3DPOOL_DEFAULT, &s_mapTex, nullptr)) || !s_mapTex))
        {
            s_mapTex = nullptr;
            if (!s_mapFailLogged) { s_mapFailLogged = true; DebugLog("radar: CreateTexture %dx%d failed", img.n, img.n); }
            return;
        }
        // grid on a copy: the bake stays clean, so toggling it off is just a re-upload
        std::vector<uint32_t> grid;
        if (g_hudRadarGrid) { grid = img.px; rdr::Grid(grid, img.n); }
        const uint32_t* src = g_hudRadarGrid ? grid.data() : img.px.data();
        D3DLOCKED_RECT lr{};
        if (FAILED(s_mapTex->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD))) return;
        for (int y = 0; y < img.n; ++y)
            memcpy((uint8_t*)lr.pBits + size_t(y) * lr.Pitch, src + size_t(y) * img.n, size_t(img.n) * 4);
        s_mapTex->UnlockRect(0);
        s_mapId = id;
        s_mapGrid = g_hudRadarGrid;
    }

    // ---- per player icon package state (CCSGO_HudRadarIconPackage) ---------------------------
    struct Pkg
    {
        bool   hasPos = false, dead = false, spotted = false;
        float  x = 0, y = 0, z = 0, yaw = 0;
        double deadAt = -1000.0, ghostAt = -1000.0;   // TIMER_INIT
    };
    Pkg s_pkg[kRadarSlots];
    uint32_t s_pkgBake = 0;

    // PlacePlayers, per player: SetIsDead / SetIsSpotted / the spotted-only position update
    void UpdatePkg(Pkg& k, const RadarPlayer& p, double now)
    {
        if (!p.valid) { k = Pkg{}; return; }
        const bool dead = !p.alive;
        if (dead != k.dead)
        {
            k.dead = dead;
            k.deadAt = dead ? now : -1000.0;
            if (!dead) { k.spotted = false; k.ghostAt = -1000.0; k.hasPos = false; }   // respawn: fresh package, no ghost at the death spot
        }
        if (!dead)
        {
            const bool spotted = (p.active || k.hasPos) && p.spotted;   // bHasValidPosition && visible
            if (spotted != k.spotted) { k.spotted = spotted; k.ghostAt = spotted ? -1000.0 : now; }
        }
        if (p.active && k.spotted)
        {
            if (!dead) { k.x = p.x; k.y = p.y; k.z = p.z; k.hasPos = true; }
            k.yaw = p.yaw;
        }
    }

    // ---- drawing helpers ---------------------------------------------------------------------
    struct Frame
    {
        float ox, oy, k;     // panel top-left (layout px) and 1080p -> layout px scale
        rdr::View v;
        ImVec2 P(float hx, float hy) const { return ImVec2(ox + hx * k, oy + hy * k); }
    };

    ImU32 Col(const float c[4], float a) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3] * a)); }
    ImU32 Rgba(float r, float g, float b, float a) { return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a)); }

    // one icon of a package: centre (hx, hy) in panel px, the package's scale-then-rotate
    // (Helper_SetPanelTransform3D) applied to the icon's own rect
    void Quad(ImDrawList* dl, const Frame& f, ImTextureID tex, float hx, float hy, float rotDeg, float scale,
              float w, float h, float ox, float oy, ImU32 col)
    {
        const float cx[4] = { ox - w * 0.5f, ox + w * 0.5f, ox + w * 0.5f, ox - w * 0.5f };
        const float cy[4] = { oy - h * 0.5f, oy - h * 0.5f, oy + h * 0.5f, oy + h * 0.5f };
        ImVec2 p[4];
        for (int i = 0; i < 4; ++i)
        {
            float rx, ry;
            rdr::Rotate(rotDeg, cx[i] * scale, cy[i] * scale, rx, ry);
            p[i] = f.P(hx + rx, hy + ry);
        }
        dl->AddImageQuad(tex, p[0], p[1], p[2], p[3], ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), col);
    }

    void DrawArt(ImDrawList* dl, const Frame& f, int id, float hx, float hy, float rotDeg, float scale, float alpha,
                 const float* wash)
    {
        const ImTextureID tex = Art(id);
        if (!tex) return;
        const ArtSpec& s = kSpec[id];
        const float a = s.opacity * alpha;
        if (s.shadow != SHADOW_NONE)   // img-shadow 0 0 1px: a 1 px halo under the icon
        {
            const ImU32 sc = s.shadow == SHADOW_BLACK ? Rgba(0.f, 0.f, 0.f, a * (0xAA / 255.f))
                                                      : Rgba(0x81 / 255.f, 0.f, 0.f, a * 0.75f);
            const float d[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            for (auto& o : d) Quad(dl, f, tex, hx + o[0], hy + o[1], rotDeg, scale, s.w, s.h, s.ox, s.oy, sc);
        }
        ImU32 col = Rgba(1.f, 1.f, 1.f, a);
        if (s.wash == WASH_TEAM && wash) col = Col(wash, a);
        else if (s.wash == WASH_GREY)    col = Rgba(0.8f, 0.8f, 0.8f, a * 0.8f);   // wash-color #CCCCCCCC
        Quad(dl, f, tex, hx, hy, rotDeg, scale, s.w, s.h, s.ox, s.oy, col);
    }

    // filled disc of radius r (panel px), colour by distance t = d / gradR, rings so the
    // gradient reads (hudradar.css radial gradients)
    template <class F>
    void RadialDisc(ImDrawList* dl, const Frame& f, float r, float gradR, F color)
    {
        constexpr int kSeg = 64, kRings = 8;
        const ImVec2 uv = dl->_Data->TexUvWhitePixel;
        dl->PrimReserve(kSeg * 3 + (kRings - 1) * kSeg * 6, 1 + kRings * kSeg);
        const ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;   // after: a 16-bit overflow restarts it at 0
        dl->PrimWriteVtx(f.P(150.f, 150.f), uv, color(0.f));
        for (int j = 1; j <= kRings; ++j)
        {
            const float rr = r * j / kRings;
            const ImU32 c = color(rr / gradR);
            for (int i = 0; i < kSeg; ++i)
            {
                const float a = 6.2831853f * i / kSeg;
                dl->PrimWriteVtx(f.P(150.f + std::cos(a) * rr, 150.f + std::sin(a) * rr), uv, c);
            }
        }
        auto ring = [&](int j, int i) { return ImDrawIdx(base + 1 + (j - 1) * kSeg + (i % kSeg)); };
        for (int i = 0; i < kSeg; ++i)
        {
            dl->PrimWriteIdx(base); dl->PrimWriteIdx(ring(1, i)); dl->PrimWriteIdx(ring(1, i + 1));
        }
        for (int j = 1; j < kRings; ++j)
            for (int i = 0; i < kSeg; ++i)
            {
                dl->PrimWriteIdx(ring(j, i)); dl->PrimWriteIdx(ring(j + 1, i)); dl->PrimWriteIdx(ring(j + 1, i + 1));
                dl->PrimWriteIdx(ring(j, i)); dl->PrimWriteIdx(ring(j + 1, i + 1)); dl->PrimWriteIdx(ring(j, i + 1));
            }
    }

    // the overview clipped to the round radar's disc: a fan whose uvs undo the rotation + zoom.
    // Past the image the sampler clamps (imgui_impl_dx9 sets CLAMP) onto the bake's transparent
    // margin, so nothing wraps.
    void MapDisc(ImDrawList* dl, const Frame& f, ImTextureID tex, float r)
    {
        constexpr int kSeg = 64;
        auto uvAt = [&](float hx, float hy) { ImVec2 uv; rdr::HudToUv(f.v, hx, hy, uv.x, uv.y); return uv; };
        dl->PushTextureID(tex);
        dl->PrimReserve(kSeg * 3, kSeg + 1);
        const ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
        dl->PrimWriteVtx(f.P(150.f, 150.f), uvAt(150.f, 150.f), IM_COL32_WHITE);
        for (int i = 0; i < kSeg; ++i)
        {
            const float a = 6.2831853f * i / kSeg, hx = 150.f + std::cos(a) * r, hy = 150.f + std::sin(a) * r;
            dl->PrimWriteVtx(f.P(hx, hy), uvAt(hx, hy), IM_COL32_WHITE);
        }
        for (int i = 0; i < kSeg; ++i)
        {
            dl->PrimWriteIdx(base); dl->PrimWriteIdx(ImDrawIdx(base + 1 + i)); dl->PrimWriteIdx(ImDrawIdx(base + 1 + (i + 1) % kSeg));
        }
        dl->PopTextureID();
    }

    // #Radar__Square--Overlay: radial #00000000 -> #00000044 over 125 px, on a 274 px box
    void SquareVignette(ImDrawList* dl, const Frame& f)
    {
        constexpr int kCells = 8;
        const float half = rdr::kBoxSquare * 0.5f - 3.f, step = 2.f * half / kCells;
        auto col = [](float hx, float hy) {
            const float t = std::min(std::sqrt((hx - 150.f) * (hx - 150.f) + (hy - 150.f) * (hy - 150.f)) / 125.f, 1.f);
            return Rgba(0.f, 0.f, 0.f, t * (0x44 / 255.f));
        };
        for (int y = 0; y < kCells; ++y)
            for (int x = 0; x < kCells; ++x)
            {
                const float x0 = 150.f - half + x * step, y0 = 150.f - half + y * step, x1 = x0 + step, y1 = y0 + step;
                dl->AddRectFilledMultiColor(f.P(x0, y0), f.P(x1, y1), col(x0, y0), col(x1, y0), col(x1, y1), col(x0, y1));
            }
    }

    // drag to move, menu open only: LMB drag, RMB resets (spectator_list.cpp SpecDrag)
    void RadarDrag(ImDrawList* dl, ImGuiIO& io, int v0, ImVec2 lmn, ImVec2 lmx)
    {
        static bool s_drag = false;
        if (!g_showMenu || dl->VtxBuffer.Size <= v0) { s_drag = false; return; }
        ImDrawVert* v = dl->VtxBuffer.Data;
        ImVec2 mn(FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX);
        for (int i = v0; i < dl->VtxBuffer.Size; ++i) { mn = ImMin(mn, v[i].pos); mx = ImMax(mx, v[i].pos); }

        const ImVec2 m = Dpi_ScreenMouse();
        const bool hovered = !GImGui->HoveredWindow && m.x >= mn.x && m.x <= mx.x && m.y >= mn.y && m.y <= mx.y;
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) s_drag = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) s_drag = false;
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { g_hudRadarOffX = g_hudRadarOffY = 0.f; s_drag = false; }

        if (s_drag)
        {
            const float k = Dpi_Scale();   // io.MouseDelta is logical px
            const ImVec2 d(fmaxf(-mn.x, fminf(io.MouseDelta.x * k, io.DisplaySize.x - mx.x)),
                           fmaxf(-mn.y, fminf(io.MouseDelta.y * k, io.DisplaySize.y - mx.y)));
            // move by the shown delta mapped back to layout px. Not SpecDrag's "at - lmn": the
            // drawn shapes start inside the 300 px panel (the disc at 23 px), so that adds the
            // inset again every frame and the radar runs off to the bottom right.
            const ImVec2 size(lmx.x - lmn.x, lmx.y - lmn.y);
            const ImVec2 from = Dpi_PanelLayoutPos(mn, size);
            const ImVec2 to   = Dpi_PanelLayoutPos(ImVec2(mn.x + d.x, mn.y + d.y), size);
            g_hudRadarOffX += to.x - from.x;
            g_hudRadarOffY += to.y - from.y;
        }
        if (hovered || s_drag)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            dl->AddRect(ImVec2(mn.x - 2.f, mn.y - 2.f), ImVec2(mx.x + 2.f, mx.y + 2.f), ImGui::GetColorU32(ImGuiCol_Accent));
        }
    }

    void Draw(ImDrawList* dl, const RadarSnap& snap, const Frame& f, double now)
    {
        const bool round = f.v.round;
        const ImTextureID map = s_mapTex && s_mapId == snap.bakeId ? ImTextureID(s_mapTex) : ImTextureID(0);

        // map: #Radar__Round--Inner / #Radar__Square, then the overlay above it
        if (round)
        {
            dl->AddCircleFilled(f.P(150.f, 150.f), rdr::kVisRound * 0.5f * f.k, IM_COL32_BLACK, 64);
            if (map) MapDisc(dl, f, map, rdr::kVisRound * 0.5f);
            // #Radar__Round--Overlay: gradient(radial, 0 -> 150 px, #22222211, 0.9 #000000CC, #000000DD)
            RadialDisc(dl, f, rdr::kOverlayRound * 0.5f, 150.f, [](float t) {
                const float a = t <= 0.9f ? 0x11 + (0xCC - 0x11) * (t / 0.9f) : 0xCC + (0xDD - 0xCC) * std::min((t - 0.9f) / 0.1f, 1.f);
                const float g = (0x22 / 255.f) * (1.f - std::min(t / 0.9f, 1.f));
                return Rgba(g, g, g, a / 255.f);
            });
        }
        else
        {
            const float h = rdr::kBoxSquare * 0.5f;
            dl->AddRectFilled(f.P(150.f - h, 150.f - h), f.P(150.f + h, 150.f + h), IM_COL32_BLACK);
            if (map)
            {
                ImVec2 uv0, uv1;
                rdr::HudToUv(f.v, 150.f - h, 150.f - h, uv0.x, uv0.y);
                rdr::HudToUv(f.v, 150.f + h, 150.f + h, uv1.x, uv1.y);
                dl->AddImage(map, f.P(150.f - h, 150.f - h), f.P(150.f + h, 150.f + h), uv0, uv1);
            }
            SquareVignette(dl, f);
        }

        // north arrow, round only (SwitchRadarToRound): y 2 at the panel top, turned with the map
        if (round)
        {
            float ax, ay;
            rdr::Rotate(f.v.rotation, 0.f, -141.f, ax, ay);
            DrawArt(dl, f, rdr::IC_NORTH, 150.f + ax, 150.f + ay, f.v.rotation, 1.f, 1.f, nullptr);
        }

        // players, entindex order (PlacePlayers)
        const float iconScale = rdr::IconScale(g_hudRadarScale, g_hudRadarIconMin);
        for (int i = 1; i < kRadarSlots; ++i)
        {
            const RadarPlayer& p = snap.players[i];
            const Pkg& k = s_pkg[i];
            if (!p.valid || !k.hasPos) continue;

            rdr::PkgState s;
            s.type = rdr::TypeFor(p.team, snap.localTeam, snap.spec);
            s.hasPos = k.hasPos; s.dead = k.dead; s.spotted = k.spotted;
            s.local = p.local; s.selected = p.selected; s.spec = snap.spec;
            s.level = k.z > snap.camZ + rdr::kAboveBelow ? 1 : k.z < snap.camZ - rdr::kAboveBelow ? -1 : 0;
            s.deadFor = float(now - k.deadAt);
            s.ghostFor = float(now - k.ghostAt);

            float rx, ry, offAngle = 0.f;
            rdr::WorldToRadar(f.v, k.x, k.y, rx, ry);
            s.offMap = rdr::ClampOffMap(f.v, rx, ry, offAngle);
            const rdr::PkgIcons icons = rdr::PickIcons(s);
            if (!icons.n) continue;

            float hx, hy;
            rdr::RadarToHud(f.v, rx, ry, hx, hy);
            const float rot = rdr::PackageRotation(f.v, s, offAngle, k.yaw);
            const float* wash = s.type == rdr::PT_ENEMY ? g_hudRadarColorEnemy
                              : s.type == rdr::PT_CT    ? g_hudRadarColorBlu : g_hudRadarColorRed;
            for (int j = 0; j < icons.n; ++j)
                DrawArt(dl, f, icons.ids[j], hx, hy, rot, iconScale, icons.alpha, wash);
        }
    }
}

void RenderRadar()
{
    if (!g_hudRadar) return;

    RadarSnap snap;
    {
        std::lock_guard<std::mutex> lock(g_radarSnapLock);
        snap = g_radarSnap;
    }
    if (!snap.stampMs || GetTickCount64() - snap.stampMs > 250 || !snap.haveMap) return;

    UploadMap();

    const double now = ImGui::GetTime();
    if (s_pkgBake != snap.bakeId) { for (Pkg& k : s_pkg) k = Pkg{}; s_pkgBake = snap.bakeId; }
    for (int i = 1; i < kRadarSlots; ++i) UpdatePkg(s_pkg[i], snap.players[i], now);

    ImGuiIO& io = ImGui::GetIO();
    Frame f;
    f.k = io.DisplaySize.y / 1080.f * std::clamp(g_hudRadarHudScale, 0.8f, 1.3f);
    f.ox = g_hudRadarOffX;
    f.oy = 60.f * f.k + g_hudRadarOffY;
    f.v = rdr::MakeView(snap.ov, !g_hudRadarSquare, g_hudRadarRotate, g_hudRadarScale, snap.camX, snap.camY, snap.viewYaw);

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const int v0 = dl->VtxBuffer.Size;
    {
        DpiPanelScope block(dl);
        Draw(dl, snap, f, now);
    }
    RadarDrag(dl, io, v0, ImVec2(f.ox, f.oy), ImVec2(f.ox + rdr::kPanel * f.k, f.oy + rdr::kPanel * f.k));
}

void Radar_ReleaseTextures()
{
    if (s_mapTex) { s_mapTex->Release(); s_mapTex = nullptr; }
    s_mapId = 0;   // re-uploaded from g_radarImage next frame
}
