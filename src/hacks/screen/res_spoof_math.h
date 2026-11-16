#pragma once
// Resolution spoofer: botox csgo hacks/visuals/screen/resolution_spoof.cpp (n_resolution_spoof::impl_t)
// target size, aspect gates, net graph and menu list 1:1, pure (tests/res_spoof_test.cpp).
//
// The TF2 port has no "overlay pass": botox draws its ImGui frame into the low-res target before
// the upscale; here our overlays + menu draw at Present over the finished frame (render.cpp), so
// botox's own fallback for "pass not running" is the whole behaviour (AspectCheatOverlay below).
#include <algorithm>
#include <cmath>

namespace rsp {

// The menu state the gates read, snapshotted once per caller (res_spoof.cpp ResSpoof_Vars).
struct Vars
{
    bool  spoof = false, stretch = true;        // "resolution spoofer", "stretch to fill"
    int   width = 1024, height = 768;           // the spoofed res (botox defaults)
    bool  aspectEnable = false;                 // "aspect ratio"
    float aspectRatio = 1.777778f;
    bool  aspectHud = false, aspectCrosshair = false, aspectCheatOverlay = false;
    bool  eject = false;                        // botox m_eject_restoring
    int   screenW = 0, screenH = 0;             // back buffer, 0 = not known yet
};

inline bool Enabled(const Vars& v) { return v.spoof && !v.eject; }

// botox target_size: the res's line count; stretch off = that line count at the screen's own shape
// (square pixels, a 4:3 res unstretched would be black bars otherwise)
inline void TargetSize(const Vars& v, int screenW, int screenH, int& w, int& h)
{
    h = std::clamp(v.height, 32, std::max(screenH, 32));
    const int wanted = v.stretch || screenH <= 0 ? v.width
                                                 : (int)std::lround((double)h * screenW / screenH);
    w = std::clamp(wanted, 32, std::max(screenW, 32));
}

// botox on_present: a target not smaller than the screen = nothing to crunch
inline bool CrunchesScreen(int w, int h, int screenW, int screenH) { return !(w >= screenW && h >= screenH); }

// The pass will crunch this screen (unknown screen = assume it will). botox's gates skip this, so a
// res the screen shrank under (1280x1024 picked, game dropped to 720p) kept the world + hud
// stretched with no low res behind it; here the stretch goes with the crunch.
inline bool Crunches(const Vars& v)
{
    if (!Enabled(v)) return false;
    if (v.screenW <= 0 || v.screenH <= 0) return true;
    int w = 0, h = 0;
    TargetSize(v, v.screenW, v.screenH, w, h);
    return CrunchesScreen(w, h, v.screenW, v.screenH);
}

inline bool StretchRes(const Vars& v) { return Enabled(v) && v.stretch && Crunches(v); }
// eject = every aspect half off: r_aspectratio 0, hud / crosshair widen undone by their own off branches
inline bool AspectVar(const Vars& v, bool value) { return value && v.aspectEnable && !v.eject; }

// res spoof "stretch" = a 4:3 res fills a 16:9 screen like a real stretched res: world + hud +
// crosshair take the res shape, cheat overlays get it from the aspect stretch
inline bool  AspectOn(const Vars& v) { return StretchRes(v) || AspectVar(v, true); }
inline float AspectWanted(const Vars& v)
{
    if (!StretchRes(v)) return v.aspectRatio;
    return float(std::max(v.width, 32)) / float(std::max(v.height, 32));
}
inline bool AspectHud(const Vars& v)       { return StretchRes(v) || AspectVar(v, v.aspectHud); }
inline bool AspectCrosshair(const Vars& v) { return StretchRes(v) || AspectVar(v, v.aspectCrosshair); }
// botox: stretched res + overlay pass = the upscale widens the overlays; pass not running = widen
// them here instead. TF2 never runs that pass, so a stretched res always widens them here.
inline bool AspectCheatOverlay(const Vars& v)
{
    if (StretchRes(v)) return true;
    return AspectVar(v, v.aspectCheatOverlay);
}

// botox build_chain: halve the screen while the next half still covers the target, at most 8 steps
// (the fallback when the capture texture can't autogen its mips). Returns the level count.
inline int ChainLevels(int screenW, int screenH, int w, int h, int out[8][2])
{
    int lw = screenW, lh = screenH, n = 0;
    for (int i = 0; i < 8; ++i)
    {
        if (lw / 2 < w || lh / 2 < h) break;
        lw /= 2;
        lh /= 2;
        out[n][0] = lw;
        out[n][1] = lh;
        ++n;
    }
    return n;
}

// botox net_graph_scale: the graph keeps the height it would have at the spoofed res
inline int NetGraphHeight(int stored, int screenH, int lowH)
{
    if (stored <= 0 || screenH <= 0 || lowH <= 0) return stored;
    return (int)(stored * ((float)screenH / (float)lowH));
}

// botox net_graph_scale's state, one step per FRAME_START. stored = the player's own height while
// ours is live (0 = nothing live), written = the value we set. cur = net_graphheight now.
// Returns the value to SetValue, -1 = leave it (caller sets written = the value after a good write).
// A cur that is not ours means the player (or a cfg) changed it while scaled: that is their own
// height now and the scale restarts from it (botox overwrote it with the stale original).
struct NetGraph { int stored = 0, written = 0; };
inline int NetGraphStep(NetGraph& n, bool on, int cur, int screenH, int lowH)
{
    if (!on)
    {
        if (!n.stored) return -1;
        const int back = n.stored;
        n = NetGraph{};
        return back;
    }
    if (screenH <= 0 || lowH <= 0) return -1;
    if (n.stored && n.written && cur != n.written) { n.stored = cur > 0 ? cur : 64; n.written = 0; }
    if (!n.stored) n.stored = cur > 0 ? cur : 64;
    const int want = NetGraphHeight(n.stored, screenH, lowH);
    return want > 0 && want != n.written ? want : -1;
}

// config.cfg must never save our scaled height (net_graphheight is FCVAR_ARCHIVE): around TF2's
// config write the cvar string reads the player's own digits. They go in place, so only when they
// fit the live string's buffer (scaled >= stored, so in practice always). Returns false = leave it.
inline bool CfgDigits(int stored, char* out, int cap, int liveLen)
{
    if (stored <= 0 || !out || cap < 2) return false;
    char tmp[16];
    int n = 0;
    for (int v = stored; v > 0 && n < 15; v /= 10) tmp[n++] = char('0' + v % 10);
    if (n > liveLen || n + 1 > cap) return false;
    for (int i = 0; i < n; ++i) out[i] = tmp[n - 1 - i];
    out[n] = '\0';
    return true;
}

// botox menu list: nothing bigger than the screen and not the screen itself (0 = list everything)
inline bool Listed(int w, int h, int screenW, int screenH)
{
    if (screenW <= 0) return true;
    return !(w > screenW || h > screenH || (w == screenW && h == screenH));
}

struct Res { int w, h; const char* aspect; };
inline constexpr Res k_resolutions[] = {
    { 1920, 1440, "4:3" },  { 1920, 1080, "16:9" }, { 1680, 1050, "16:10" }, { 1600, 1200, "4:3" },
    { 1600, 900, "16:9" },  { 1440, 1080, "4:3" },  { 1440, 900, "16:10" },  { 1366, 768, "16:9" },
    { 1280, 1024, "5:4" },  { 1280, 960, "4:3" },   { 1280, 800, "16:10" },  { 1280, 720, "16:9" },
    { 1200, 900, "4:3" },   { 1152, 864, "4:3" },   { 1024, 768, "4:3" },    { 1024, 576, "16:9" },
    { 960, 720, "4:3" },    { 960, 540, "16:9" },   { 800, 600, "4:3" },     { 640, 480, "4:3" },
};

} // namespace rsp
