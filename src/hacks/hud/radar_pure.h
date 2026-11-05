#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// ============================================================================================
// csgo radar, engine-free halves (tests/radar_test.cpp). Port of CS:GO's panorama radar,
// csgosrc2019 game/client/cstrike15/panorama/hud/csgo_hudradar.cpp + its layout
// (code.pbin panorama/layout/hud/hudradar.xml, styles/hud/hudradar.css):
//   NavParse     server/nav_file.cpp CNavArea::Load + tf_nav_area.cpp (bot nav mesh = map bounds)
//   FitOverview  stands in for resource/overviews/<map>.txt (pos_x / pos_y / scale): TF2 ships none
//   MakeView ..  UpdateRadarScalingValues / PositionRadarViewpoint / WorldToRadar / RadarToHud
//   NavRaster / Shade   the overview image, baked from world traces (aim/radar_aim.cpp)
// ============================================================================================

namespace rdr
{
    // ---- hudradar.css @defines + csgo_hudradar.cpp constants -------------------------------
    inline constexpr float kPanel        = 300.f;   // Radar_PanelSize
    inline constexpr float kVisRound     = 250.f;   // Radar_VisSize_Round
    inline constexpr float kOverlayRound = 254.f;   // Radar_Overlay_Round
    inline constexpr float kVisSquare    = 290.f;   // Radar_VisSize_Square
    inline constexpr float kBoxSquare    = 280.f;   // Radar_WithBorderSize_Square (the black box; clips the 290 inner)
    inline constexpr float kSfToPanorama = 768.f / 320.f;   // SF_TO_PANORAMA_RADAR_SCALE
    inline constexpr float kSourceImage  = 1024.f;           // m_fMapSourceImageSize: every overview is 1024
    inline constexpr float kAboveBelow   = 94.f;             // ABOVE_BELOW_HEIGHT
    inline constexpr float kDeadFade     = 4.f;              // DEAD_FADE_TIME
    inline constexpr float kGhostFade    = 6.f;              // GHOST_FADE_TIME

    // ---- bot nav mesh ----------------------------------------------------------------------
    struct NavArea { float x0, y0, x1, y1, zLo, zHi; };   // nw..se rect, lowest / highest corner z

    struct Reader
    {
        const uint8_t* p; size_t n; size_t at = 0; bool bad = false;
        template <class T> T Get()
        {
            T v{};
            if (n - at < sizeof(T)) { bad = true; at = n; return v; }
            memcpy(&v, p + at, sizeof(T)); at += sizeof(T);
            return v;
        }
        void Skip(size_t k) { if (n - at < k) { bad = true; at = n; } else at += k; }
    };

    // nav_file.cpp CNavMesh::Load header + CNavArea::Load per area, every version branch kept.
    // TF meshes add one u32 of attribute flags per area at sub-version >= 2 (CTFNavArea::Load).
    inline bool NavParse(const uint8_t* data, size_t size, std::vector<NavArea>& out)
    {
        out.clear();
        Reader r{ data, size };
        if (r.Get<uint32_t>() != 0xFEEDFACEu) return false;            // NAV_MAGIC_NUMBER
        const uint32_t version = r.Get<uint32_t>();
        if (r.bad || version > 16 || version < 1) return false;        // NavCurrentVersion = 16
        const uint32_t sub = version >= 10 ? r.Get<uint32_t>() : 0;
        if (version >= 4) r.Get<uint32_t>();                            // bsp size
        if (version >= 14) r.Get<uint8_t>();                            // m_isAnalyzed
        if (version >= 5)                                               // PlaceDirectory::Load
        {
            const uint16_t places = r.Get<uint16_t>();
            for (uint16_t i = 0; i < places && !r.bad; ++i) r.Skip(r.Get<uint16_t>());
            if (version > 11) r.Get<uint8_t>();                         // m_hasUnnamedAreas
        }
        const uint32_t count = r.Get<uint32_t>();
        if (r.bad || !count || count > (size - r.at) / 32) return false;   // an area is >= 32 bytes
        out.reserve(count);

        for (uint32_t a = 0; a < count && !r.bad; ++a)
        {
            r.Get<uint32_t>();                                          // id
            if (version <= 8) r.Get<uint8_t>(); else if (version < 13) r.Get<uint16_t>(); else r.Get<int32_t>();
            float nw[3], se[3];
            for (float& f : nw) f = r.Get<float>();
            for (float& f : se) f = r.Get<float>();
            const float neZ = r.Get<float>(), swZ = r.Get<float>();
            for (int d = 0; d < 4; ++d) r.Skip(size_t(r.Get<uint32_t>()) * 4);   // NUM_DIRECTIONS connections
            const uint8_t spots = r.Get<uint8_t>();
            r.Skip(size_t(spots) * (version == 1 ? 12 : 17));        // HidingSpot::Load: id, pos, flags
            if (version < 15) r.Skip(size_t(r.Get<uint8_t>()) * 14);   // approach areas, eaten
            const uint32_t enc = r.Get<uint32_t>();
            for (uint32_t e = 0; e < enc && !r.bad; ++e)
            {
                if (version < 3) { r.Skip(32); r.Skip(size_t(r.Get<uint8_t>()) * 16); }
                else             { r.Skip(10); r.Skip(size_t(r.Get<uint8_t>()) * 5); }
            }
            if (version >= 5) r.Get<uint16_t>();                        // place
            if (version >= 7) for (int d = 0; d < 2; ++d) r.Skip(size_t(r.Get<uint32_t>()) * 4);   // ladders up / down
            if (version >= 8) r.Skip(2 * 4);                            // earliest occupy, MAX_NAV_TEAMS = 2
            if (version >= 11) r.Skip(4 * 4);                           // light intensity, NUM_CORNERS
            if (version >= 16) { r.Skip(size_t(r.Get<uint32_t>()) * 5); r.Get<uint32_t>(); }   // visible areas + inherit
            if (sub >= 2) r.Get<uint32_t>();                            // CTFNavArea m_attributeFlags
            if (r.bad) break;

            const float zs[4] = { nw[2], se[2], neZ, swZ };
            for (float f : { nw[0], nw[1], se[0], se[1], nw[2], se[2], neZ, swZ })
                if (!std::isfinite(f) || std::fabs(f) > 1e6f) { out.clear(); return false; }   // junk would raster the whole grid
            out.push_back({ std::min(nw[0], se[0]), std::min(nw[1], se[1]), std::max(nw[0], se[0]), std::max(nw[1], se[1]),
                            *std::min_element(zs, zs + 4), *std::max_element(zs, zs + 4) });
        }
        if (r.bad) { out.clear(); return false; }
        return true;
    }

    struct Bounds { float x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0; };

    inline Bounds NavBounds(const std::vector<NavArea>& areas)
    {
        Bounds b{ 1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f };
        for (const NavArea& a : areas)
        {
            b.x0 = std::min(b.x0, a.x0); b.y0 = std::min(b.y0, a.y0); b.z0 = std::min(b.z0, a.zLo);
            b.x1 = std::max(b.x1, a.x1); b.y1 = std::max(b.y1, a.y1); b.z1 = std::max(b.z1, a.zHi);
        }
        return b;
    }

    // ---- overview (resource/overviews/<map>.txt) -------------------------------------------
    // pos_x / pos_y = the world point at the image's top-left, scale = world units per pixel of
    // the 1024 image. Square around the bounds plus a 4% margin, like Valve's hand-made ones.
    struct Overview { float ox = 0, oy = 0, scale = 1; };

    inline Overview FitOverview(const Bounds& b)
    {
        const float side = std::max({ b.x1 - b.x0, b.y1 - b.y0, 256.f }) * 1.04f;
        const float cx = (b.x0 + b.x1) * 0.5f, cy = (b.y0 + b.y1) * 0.5f;
        return { cx - side * 0.5f, cy + side * 0.5f, side / kSourceImage };
    }

    // world point -> texel of an n x n bake of this overview (fractional)
    inline void WorldToTexel(const Overview& ov, int n, float x, float y, float& u, float& v)
    {
        const float k = float(n) / (ov.scale * kSourceImage);
        u = (x - ov.ox) * k;
        v = (ov.oy - y) * k;
    }
    inline void TexelToWorld(const Overview& ov, int n, float u, float v, float& x, float& y)
    {
        const float k = ov.scale * kSourceImage / float(n);
        x = ov.ox + u * k;
        y = ov.oy - v * k;
    }

    // Highest nav floor under each texel, -inf where no area covers it (not walkable).
    inline void NavRaster(const std::vector<NavArea>& areas, const Overview& ov, int n, std::vector<float>& top)
    {
        top.assign(size_t(n) * n, -INFINITY);
        for (const NavArea& a : areas)
        {
            float u0, v0, u1, v1;
            WorldToTexel(ov, n, a.x0, a.y1, u0, v0);   // nw corner of the rect in image space
            WorldToTexel(ov, n, a.x1, a.y0, u1, v1);
            const int iu0 = std::max(0, int(std::floor(u0))), iu1 = std::min(n - 1, int(std::ceil(u1)) - 1);
            const int iv0 = std::max(0, int(std::floor(v0))), iv1 = std::min(n - 1, int(std::ceil(v1)) - 1);
            for (int v = iv0; v <= iv1; ++v)
                for (int u = iu0; u <= iu1; ++u)
                {
                    float& t = top[size_t(v) * n + u];
                    t = std::max(t, a.zHi);
                }
        }
    }

    // ---- overview shading ------------------------------------------------------------------
    // height[i] = floor z (NaN = void), nz[i] = floor normal z. Output D3DFMT_A8R8G8B8 texels.
    // CS:GO overview look: void transparent (the radar's black shows), floors a cool grey ramp by
    // height (higher = lighter) with a hillshade from the normal, a light rim where a floor meets
    // void or drops / rises more than a step (walls, ledges).
    inline uint32_t Argb(float r, float g, float b, float a)
    {
        auto c = [](float f) { return uint32_t(std::clamp(f, 0.f, 1.f) * 255.f + 0.5f); };
        return (c(a) << 24) | (c(r) << 16) | (c(g) << 8) | c(b);
    }

    // albedo[i] = the floor material's average colour, 0xRRGGBB sRGB (0 = unknown: grey). Its hue
    // tints the grey ramp by `tint` (0 grey .. 1 full hue); brightness stays the height ramp's, so
    // the overview still reads by level. Rims take less of it so walls stay light.
    inline uint32_t PackAlbedo(float r, float g, float b)   // linear reflectivity -> sRGB 0xRRGGBB, never 0
    {
        auto c = [](float f) { return std::isfinite(f) ? uint32_t(std::pow(std::clamp(f, 0.f, 1.f), 1.f / 2.2f) * 255.f + 0.5f) : 0u; };
        const uint32_t v = (c(r) << 16) | (c(g) << 8) | c(b);
        return v ? v : 1u;
    }

    inline void Shade(const std::vector<float>& height, const std::vector<float>& nz, int n, float zLo, float zHi,
                      std::vector<uint32_t>& out, const std::vector<uint32_t>* albedo = nullptr, float tint = 0.65f)
    {
        out.assign(size_t(n) * n, 0u);
        const float span = std::max(zHi - zLo, 64.f);
        constexpr float kStep = 40.f;   // more than a stair step (18) + slope slack = an edge
        for (int v = 0; v < n; ++v)
            for (int u = 0; u < n; ++u)
            {
                const size_t i = size_t(v) * n + u;
                const float z = height[i];
                if (std::isnan(z)) continue;

                bool edge = false;
                const int du[4] = { 1, -1, 0, 0 }, dv[4] = { 0, 0, 1, -1 };
                for (int k = 0; k < 4 && !edge; ++k)
                {
                    const int uu = u + du[k], vv = v + dv[k];
                    if (uu < 0 || vv < 0 || uu >= n || vv >= n) { edge = true; break; }
                    const float zz = height[size_t(vv) * n + uu];
                    edge = std::isnan(zz) || std::fabs(zz - z) > kStep;
                }

                const float t = std::clamp((z - zLo) / span, 0.f, 1.f);
                const float light = 0.72f + 0.28f * std::clamp(nz[i], 0.f, 1.f);
                float g = (0.24f + 0.46f * t) * light;
                if (edge) g = std::max(g + 0.22f, 0.78f);
                float hr = 0.92f, hg = 0.96f, hb = 1.f;   // the cool grey
                if (albedo && albedo->size() > i && (*albedo)[i])
                {
                    const uint32_t a = (*albedo)[i];
                    float ar = ((a >> 16) & 0xFF) / 255.f, ag = ((a >> 8) & 0xFF) / 255.f, ab = (a & 0xFF) / 255.f;
                    const float m = std::max({ ar, ag, ab, 0.05f });   // hue + saturation, max channel 1
                    ar /= m; ag /= m; ab /= m;
                    const float t = edge ? tint * 0.4f : tint;
                    hr += (ar - hr) * t; hg += (ag - hg) * t; hb += (ab - hb) * t;
                }
                out[i] = Argb(g * hr, g * hg, g * hb, 1.f);
            }
    }

    // ---- radar transforms ------------------------------------------------------------------
    struct View
    {
        bool  round = true;
        float radarSize = kVisRound;   // m_fRadarSize
        float mapSize = 0;             // m_fMapSize
        float worldToRadar = 0;        // m_fWorldToRadarScale
        float scale = 1;               // fScale: cl_radar_scale (round) or radar / map (square)
        float radiusSq = 0;            // m_fRadarRadiusSq
        float rotation = 0;            // m_RadarRotation, degrees
        float vpx = 0, vpy = 0;        // m_RadarViewpointMap
        Overview ov;
    };

    inline void WorldToRadar(const View& v, float x, float y, float& rx, float& ry)
    {
        const float s = v.worldToRadar * v.scale;
        rx = (x - v.ov.ox) * s;
        ry = (v.ov.oy - y) * s;
    }

    // MatrixBuildRotateZ in the y-down radar plane: positive = clockwise on screen
    inline void Rotate(float deg, float x, float y, float& ox, float& oy)
    {
        const float r = deg * 3.14159265f / 180.f, c = std::cos(r), s = std::sin(r);
        ox = x * c - y * s;
        oy = x * s + y * c;
    }

    // RadarToHud: rotate about the viewpoint, centre on the 300 px panel
    inline void RadarToHud(const View& v, float rx, float ry, float& hx, float& hy)
    {
        Rotate(v.rotation, rx - v.vpx, ry - v.vpy, hx, hy);
        hx += kPanel * 0.5f;
        hy += kPanel * 0.5f;
    }

    // inverse of WorldToRadar + RadarToHud down to overview uv (0..1 over the 1024 image)
    inline void HudToUv(const View& v, float hx, float hy, float& u, float& w)
    {
        float rx, ry;
        Rotate(-v.rotation, hx - kPanel * 0.5f, hy - kPanel * 0.5f, rx, ry);
        const float k = 1.f / (v.mapSize * v.scale);
        u = (rx + v.vpx) * k;
        w = (ry + v.vpy) * k;
    }

    // UpdateRadarScalingValues + PositionRadarViewpoint (cl_radar_always_centered 1, no coop scaler)
    inline View MakeView(const Overview& ov, bool round, bool rotate, float radarScale, float camX, float camY, float viewYaw)
    {
        View v;
        v.ov = ov;
        v.round = round;
        v.radarSize = round ? kVisRound : kVisSquare;
        v.mapSize = kSfToPanorama * v.radarSize;
        v.worldToRadar = (1.f / ov.scale) * (v.mapSize / kSourceImage);
        v.scale = round ? std::clamp(radarScale, 0.25f, 1.f) : v.radarSize / v.mapSize;
        const float radius = v.radarSize * 0.98f * 0.5f;
        v.radiusSq = radius * radius;
        if (round) WorldToRadar(v, camX, camY, v.vpx, v.vpy);
        else       v.vpx = v.vpy = v.scale * v.mapSize * 0.5f;
        v.rotation = round && rotate ? viewYaw - 90.f : 0.f;
        return v;
    }

    // cl_radar_icon_scale_min .. 1.25 by cl_radar_scale (SetIconPackagePosition)
    inline float IconScale(float radarScale, float iconMin)
    {
        return iconMin + std::clamp(radarScale, 0.f, 1.f) * (1.25f - iconMin);
    }

    // SetIconPackagePosition: radar point of an icon, pulled onto the rim when it is past the
    // round radar's radius. offAngle = the off-map arrow's angle in radar space (deg, 0 = up).
    inline bool ClampOffMap(const View& v, float& rx, float& ry, float& offAngle)
    {
        const float dx = rx - v.vpx, dy = ry - v.vpy, d = dx * dx + dy * dy;
        if (!v.round || d < v.radiusSq) return false;
        const float k = std::sqrt(v.radiusSq / d);
        rx = v.vpx + dx * k;
        ry = v.vpy + dy * k;
        offAngle = 180.f * std::atan2(dy, dx) / 3.141592f + 90.f;
        return true;
    }

    // ---- icon package (CCSGO_HudRadarIconPackage) ------------------------------------------
    enum PType { PT_NONE, PT_CT, PT_T, PT_ENEMY };   // SetPlayerTeam: BLU -> CT, RED -> T, other team -> enemy
    enum Icon
    {
        IC_ONMAP, IC_ENEMY, IC_DIR, IC_GHOST_CT, IC_GHOST_T, IC_GHOST_ENEMY, IC_ABOVE, IC_BELOW,
        IC_DEATH, IC_OFFMAP, IC_FRUSTUM, IC_SELECTED, IC_NORTH, IC_COUNT
    };

    inline int TypeFor(int team, int localTeam, bool spec)
    {
        if (team == 3) return localTeam == 3 || spec ? PT_CT : PT_ENEMY;   // TF_TEAM_BLUE ~ TEAM_CT
        if (team == 2) return localTeam == 2 || spec ? PT_T : PT_ENEMY;    // TF_TEAM_RED  ~ TEAM_TERRORIST
        return PT_NONE;                                                     // spectator / unassigned: no icon
    }

    struct PkgState
    {
        int   type = PT_NONE;
        bool  hasPos = false, dead = false, spotted = false, offMap = false, local = false, selected = false, spec = false;
        int   level = 0;            // -1 below, 0 same, +1 above (ABOVE_BELOW_HEIGHT)
        float deadFor = 0.f;        // seconds since death
        float ghostFor = 0.f;       // seconds since last spotted
    };
    struct PkgIcons { int ids[6] = {}; int n = 0; float alpha = 1.f; };

    // SetupIconsFromStates: the visibility flags, as the icons to draw in layout order
    // (hudradar.xml PI_FirstRotated children top to bottom = back to front).
    inline PkgIcons PickIcons(const PkgState& s)
    {
        PkgIcons o;
        if (s.type == PT_NONE) return o;
        auto add = [&](int id) { o.ids[o.n++] = id; };
        const int base   = s.type == PT_ENEMY ? IC_ENEMY : IC_ONMAP;
        const int ghost  = s.type == PT_ENEMY ? IC_GHOST_ENEMY : s.type == PT_CT ? IC_GHOST_CT : IC_GHOST_T;

        if (s.dead)
        {
            // flags <<= 2: the type icon becomes its X, the frustum bit falls off the end
            if (!s.hasPos || !s.spotted || s.deadFor > kDeadFade) return o;
            o.alpha = std::clamp(1.f - s.deadFor / kDeadFade, 0.f, 1.f);
            add(IC_DEATH);
        }
        else if (!s.spotted)
        {
            // flags <<= 1 off the map (the off-map arrow), <<= 3 on it (the ? ghost)
            if (s.ghostFor > kGhostFade) return o;
            o.alpha = std::clamp(1.f - s.ghostFor / kGhostFade, 0.f, 1.f);
            add(s.offMap ? IC_OFFMAP : ghost);
        }
        else if (s.offMap)
        {
            // flags <<= 1: type -> its off-map arrow, PI_SELECTED -> PI_VIEWFRUSTRUM, frustum falls off
            if (s.selected) add(IC_FRUSTUM);
            add(IC_OFFMAP);
        }
        else
        {
            if (s.local) add(IC_FRUSTUM);
            if (s.level > 0) add(IC_ABOVE);
            else if (s.level < 0) add(IC_BELOW);
            add(base);
            if (s.level == 0 && !s.spec && s.type != PT_ENEMY) add(IC_DIR);
            if (s.selected) add(IC_SELECTED);
        }
        return o;
    }

    // SetIconPackagePosition: the rotated package's angle on the hud (deg, clockwise)
    inline float PackageRotation(const View& v, const PkgState& s, float offAngle, float yaw)
    {
        float mapAngle;
        if (s.offMap) mapAngle = offAngle;
        else if (s.dead || !s.spotted || s.level != 0 || (s.type == PT_ENEMY && !s.selected)) mapAngle = -v.rotation;
        else mapAngle = -yaw + 90.f;
        return v.rotation + mapAngle;   // m_RadarRotation is 0 unless round + rotate
    }
}
