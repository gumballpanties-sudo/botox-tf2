#pragma once
#include <math.h>
#include <vector>
#include <algorithm>
#include "../../dependencies/imgui/imgui.h"
#include "miwarp.h"

// The mitosis look for the keybind tags: a new tag DIVIDES off the tag next to it like a drop of
// liquid splitting in two.
//   1. the daughter is born as an exact copy of the parent's letters sitting on it (one tag) and
//      shoots out of it fast (ease-out), so the two overlap for a couple of frames only
//   2. its letters morph from the parent's into its own from the first frame, by signed distance
//      interpolation (strokes grow and shrink instead of cross-fading)
//   3. once they come apart the gap is bridged by a strand of the font's OWN stroke -- its stem
//      thickness, measured off the atlas, on the x-height midline -- flared into both tags with a
//      smooth-union fillet, so it reads as the letters' ink stretched between them (a flat bar
//      as tall as the letters read as an arrow in game; the letters' extruded edge silhouette
//      read as a slab)
//   4. the daughter pulls a little past its slot, the strand thins from the middle out (the
//      distance field is eroded), snaps, and the two halves taper back into the letters while
//      the daughter springs back to its slot
//   5. squash & stretch (miwarp.h) leans the two halves into each other all the way through
// A tag with nothing to divide off (a lone tag, or two born side by side) pops in from half size
// (PopAt) -- scaled on its distance field, so it stays sharp at every size
// (scaling the text quads was a bilinear stretch of the atlas: blurry).
// Fading out runs every step backwards: two tags flowing into one.
//
// No alpha anywhere but the 1px edge antialiasing. Shapes are rasterized SS x supersampled so the
// fields have sub-pixel outlines, and box-filtered back down. A settled tag comes out as the atlas
// text itself at the same sub-pixel spot, so handing over to the text pass does not show.
// Gate test: tests/migoo_test.cpp (synthetic shapes + the real tahoma atlas, frames dumped).
namespace mig
{
constexpr float kFar = 1e9f;   // "no shape anywhere near", pre-transform
constexpr float kOut = 1e4f;   // a sample outside a field's rect: nowhere near its shape

inline float Sat(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// Chamfer distance transform (1 / sqrt2 weights), two sweeps, in pixels, over d already SEEDED
// (each pixel's own distance, kFar = unknown). Exact EDT is not worth it here: the worst case
// error is ~8% on long diagonals, and the morph only ever reads distances a stroke width or two
// from an outline, where that is a fraction of a supersampled pixel. `s`, if given, is carried
// along: every pixel ends up with the value of the seed its distance came from.
inline void Chamfer(float* d, int w, int h, float* s = nullptr)
{
    constexpr float kOrtho = 1.f, kDiag = 1.41421356f;
    const auto relax = [&](int i, int j, float step, float& v)
    {
        if (d[j] + step < v) { v = d[j] + step; if (s) s[i] = s[j]; }
    };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const int i = y * w + x;
            float v = d[i];
            if (x > 0)              relax(i, i - 1,     kOrtho, v);
            if (y > 0)              relax(i, i - w,     kOrtho, v);
            if (y > 0 && x > 0)     relax(i, i - w - 1, kDiag,  v);
            if (y > 0 && x + 1 < w) relax(i, i - w + 1, kDiag,  v);
            d[i] = v;
        }
    for (int y = h - 1; y >= 0; --y)
        for (int x = w - 1; x >= 0; --x)
        {
            const int i = y * w + x;
            float v = d[i];
            if (x + 1 < w)              relax(i, i + 1,     kOrtho, v);
            if (y + 1 < h)              relax(i, i + w,     kOrtho, v);
            if (y + 1 < h && x + 1 < w) relax(i, i + w + 1, kDiag,  v);
            if (y + 1 < h && x > 0)     relax(i, i + w - 1, kDiag,  v);
            d[i] = v;
        }
}

// Distance to the nearest inside pixel (>= 128). A partly covered pixel starts at 0.5 rather than 0.
inline void DistTransform(const unsigned char* cov, int w, int h, float* d)
{
    for (int i = 0, n = w * h; i < n; ++i)
        d[i] = cov[i] >= 128 ? 0.f : (cov[i] > 0 ? 0.5f : kFar);
    Chamfer(d, w, h);
}

// Bilinear tap into the atlas, clamped to a texel rect: BlitLabel passes the glyph's own rect
// plus the one-texel padding ring the atlas packs round it (zeros, TexGlyphPadding 1) -- what the
// GPU's bilinear reads at a quad's edge, so a glyph's outer ink column fades as its quad slides
// off a pixel centre. Clamped to the glyph rect itself, that column read solid right up to the
// quad edge and every glyph edge snapped to the ss grid. Past the ring lies the next glyph.
inline float AtlasTap(const unsigned char* tex, int tw, float u, float v,
                      float cu0, float cu1, float cv0, float cv1)
{
    const auto T = [&](float x, float y) -> float
    {
        if (x < cu0) x = cu0; if (x > cu1) x = cu1;
        if (y < cv0) y = cv0; if (y > cv1) y = cv1;
        return tex[(int)y * tw + (int)x];
    };
    const float x0 = floorf(u), y0 = floorf(v);
    const float fu = u - x0,    fv = v - y0;
    const float a = T(x0, y0) * (1.f - fu) + T(x0 + 1.f, y0) * fu;
    const float b = T(x0, y0 + 1.f) * (1.f - fu) + T(x0 + 1.f, y0 + 1.f) * fu;
    return a * (1.f - fv) + b * fv;
}

// One label's coverage into a buffer whose (0,0) sits at `org` (same units as pos).
// org is integer-aligned by the caller, so the subpixel part of the tag's position is resolved
// HERE, the way the GPU resolves the text pass (mig::SubPixelText): each glyph quad padded half a
// texel into the atlas's zero ring, so the bilinear falls to 0 exactly where the quad ends.
// Unpadded, a tight bitmap's ink column was cut off at the quad edge and the pixel beside it
// flipped between nothing and half ink as the quad slid across its centre.
inline bool BlitLabel(unsigned char* cov, int bw, int bh, ImVec2 org, ImFont* font, float fs,
                      const char* text, ImVec2 pos)
{
    ImFontAtlas* atlas = font->ContainerAtlas;
    const unsigned char* tex = atlas ? atlas->TexPixelsAlpha8 : nullptr;
    if (!tex) return false;   // atlas CPU copy dropped: no morph this frame, plain text instead
    const int   tw = atlas->TexWidth, th = atlas->TexHeight;
    const float scale = fs / font->FontSize;

    float pen = pos.x;
    for (const char* s = text; *s; ++s)
    {
        const ImFontGlyph* g = font->FindGlyph((ImWchar)(unsigned char)*s);
        if (!g) continue;
        const float x0 = pen + g->X0 * scale - org.x,   x1 = pen + g->X1 * scale - org.x;
        const float y0 = pos.y + g->Y0 * scale - org.y, y1 = pos.y + g->Y1 * scale - org.y;
        pen += g->AdvanceX * scale;
        if (x1 - x0 < 0.01f || y1 - y0 < 0.01f) continue;

        const float u0 = g->U0 * tw, u1 = g->U1 * tw;
        const float v0 = g->V0 * th, v1 = g->V1 * th;
        // texels per px, and the quad padded half a texel all round
        const float ku = (u1 - u0) / (x1 - x0), kv = (v1 - v0) / (y1 - y0);
        const float qx0 = x0 - 0.5f / ku, qx1 = x1 + 0.5f / ku;
        const float qy0 = y0 - 0.5f / kv, qy1 = y1 + 0.5f / kv;
        // the padding ring's texels, kept inside the atlas
        const float cu0 = fmaxf(u0 - 1.f, 0.f), cu1 = fminf(u1, tw - 1.f);
        const float cv0 = fmaxf(v0 - 1.f, 0.f), cv1 = fminf(v1, th - 1.f);
        int px0 = (int)floorf(qx0), px1 = (int)ceilf(qx1);
        int py0 = (int)floorf(qy0), py1 = (int)ceilf(qy1);
        if (px0 < 0) px0 = 0; if (px1 > bw) px1 = bw;
        if (py0 < 0) py0 = 0; if (py1 > bh) py1 = bh;

        for (int py = py0; py < py1; ++py)
        {
            const float cy = py + 0.5f;
            if (cy < qy0 || cy >= qy1) continue;
            const float sv = v0 + (cy - y0) * kv - 0.5f;
            for (int px = px0; px < px1; ++px)
            {
                const float cx = px + 0.5f;
                if (cx < qx0 || cx >= qx1) continue;
                const float su = u0 + (cx - x0) * ku - 0.5f;
                const float val = AtlasTap(tex, tw, su, sv, cu0, cu1, cv0, cv1);
                unsigned char& dst = cov[py * bw + px];
                if (val > dst) dst = (unsigned char)(val + 0.5f);
            }
        }
    }
    return true;
}

// Signed distance to a coverage shape, negative inside, in pixels, zero line on the 50% contour,
// plus the edge's SLOPE (coverage per pixel across it) for every pixel.
// Seeds: each pixel with a 4-neighbour across 50% -- the contour runs between them, where the
// coverage, linear from centre to centre (the atlas is bilinear-upsampled), crosses 0.5. Its
// distance is that crossing's (both axes' crossings: the line through them), outside pixels +,
// inside -; the chamfer carries it out, the slope riding along. So:
//   * the zero line keeps the SUB-pixel place the coverage says, and the whole field moves with
//     it: a tag gliding 0.1 px moves its edges 0.1 px, near and far. Thresholded to a hard
//     shape first (the old way), every edge snapped to the pixel grid: at ss 3 a slowing tag
//     walked in 1/3 px steps with stalls -- "choppy" in game
//   * a pixel starting to touch the contour starts at the distance its neighbour already gave
//     it (the crossing sits on that neighbour then: 1 px), so nothing jumps when the set of
//     edge pixels changes. Seeded from its coverage alone (0.5 - c), a soft edge -- light hinting
//     leaves vertical ones two texels wide -- dropped a step whenever a pixel entered its ramp,
//     and the morph's blended letters and the strand's ends jittered with it
//   * 0.5 - distance * slope is the pixel's own coverage again (slope = |0.5 - c| / distance at a
//     seed, carried out along a ramp): Compose draws the letters' own antialiasing back, however
//     soft or hard each edge is
// dOut/dIn/sOut/sIn = w*h floats scratch.
inline void SignedDist(const unsigned char* cov, int w, int h, float* dOut, float* dIn,
                       float* sOut, float* sIn, float* sdf, float* slope)
{
    const int n = w * h;
    for (int i = 0; i < n; ++i) { dOut[i] = dIn[i] = kFar; sOut[i] = sIn[i] = 1.f; }
    const auto C = [&](int i) { return cov[i] / 255.f; };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const int   i  = y * w + x;
            const float c  = C(i);
            const bool  in = cov[i] >= 128;
            // nearest 50% crossing toward each neighbour across it, per axis (px), and its slope
            float t[2] = { kFar, kFar }, g[2] = { 0.f, 0.f };
            const auto cross = [&](int j, int axis)
            {
                if ((cov[j] >= 128) == in) return;
                const float dc = fabsf(C(j) - c), tt = fabsf(0.5f - c) / dc;
                if (tt < t[axis]) { t[axis] = tt; g[axis] = dc; }
            };
            if (x > 0)     cross(i - 1, 0);
            if (x + 1 < w) cross(i + 1, 0);
            if (y > 0)     cross(i - w, 1);
            if (y + 1 < h) cross(i + w, 1);
            if (t[0] >= kFar && t[1] >= kFar) continue;
            float d, s;
            if (t[0] < kFar && t[1] < kFar)
            {
                d = t[0] * t[1] / fmaxf(sqrtf(t[0] * t[0] + t[1] * t[1]), 1e-6f);
                s = sqrtf(g[0] * g[0] + g[1] * g[1]);
            }
            else { const int a = t[0] < kFar ? 0 : 1; d = t[a]; s = g[a]; }
            dOut[i] = in ? -d : d;
            dIn[i]  = in ? d : -d;
            sOut[i] = sIn[i] = s;
        }
    Chamfer(dOut, w, h, sOut);
    Chamfer(dIn, w, h, sIn);
    for (int i = 0; i < n; ++i)
    {
        const bool in = cov[i] >= 128;
        sdf[i]   = in ? -dIn[i] : dOut[i];
        slope[i] = in ? sIn[i] : sOut[i];
        // every pixel of a ramp its own slope, so 0.5 - distance * slope is its coverage exactly
        // (the one carried from the seed is a straight-line guess past a bilinear kink). Beside
        // the contour, where both are ~0, the carried one stays
        const float c = C(i);
        if (cov[i] > 0 && cov[i] < 255 && fabsf(0.5f - c) > 0.02f && fabsf(sdf[i]) > 1e-3f)
            slope[i] = fabsf(0.5f - c) / fabsf(sdf[i]);
    }
}

inline float Smooth(float e0, float e1, float x)
{
    const float t = Sat((x - e0) / (e1 - e0));
    return t * t * (3.f - 2.f * t);
}

// Polynomial smooth min (iq): min(a,b) with the corner rounded over radius r. r = 0 is min.
// Used ONLY where the bridge meets the letters -- the fillet that makes it read as liquid.
inline float SMin(float a, float b, float r)
{
    if (r <= 0.f) return a < b ? a : b;
    const float h = Sat(0.5f + 0.5f * (b - a) / r);
    return b + (a - b) * h - r * h * (1.f - h);
}

// Split timeline, t = the daughter's raw fade (0 = one tag, 1 = two settled tags).
constexpr float kSnap = 0.55f;   // the bridge's neck lets go here
constexpr float kPull = 1.12f;   // how far past its slot the daughter pulls before the snap
// daughter's centre: 0 = on the parent, 1 = in its own slot. Shoots out (ease-out cubic: fast
// start, so the overlapping copies are a frame or two, not a smear) to kPull, the strands
// stretching, and after the snap springs back to its slot
inline float SlideAt(float t)
{
    if (t <= kSnap) { const float x = 1.f - Sat(t / kSnap); return kPull * (1.f - x * x * x); }
    return 1.f + (kPull - 1.f) * (1.f - Smooth(kSnap, 0.85f, t));
}
// the strands thinning: 0 = the letters' own thickness, 1 = eroded through at the middle
inline float DepthAt(float t) { return Smooth(0.2f, kSnap, t); }
// after the snap: how much of each stub has pulled back into its letters (1 = gone)
inline float CutAt(float t) { return Smooth(kSnap, 0.75f, t); }
// daughter's letters: turning from the parent's into its own from the first frame (0 at t = 0,
// so frame one is still an exact copy), done before the snap
inline float MorphAt(float t) { return Smooth(0.f, 0.5f, t); }
// Soft ease-out-back: ~3% overshoot, then settle. The textbook constant (1.70158) overshoots
// 10%, which read as too springy in game. Slots opening (movind.cpp) and a lone tag popping in
// both run on it.
constexpr float kBack = 0.9f;
inline float EaseOut(float x)
{
    const float p = x - 1.f;
    return 1.f + (kBack + 1.f) * p * p * p + kBack * p * p;
}

// Frame-rate independent exponential follow: cur moves 1 - e^(-rate dt) of the way to want.
// movind.cpp's layout slots ride on it, so the row GLIDES (time constant 1/rate) however fast a
// bind fades -- tied straight to the fade, a new bind shoved the others over in ~6 frames.
// No overshoot, never passes want, and lands EXACTLY on it once within 1e-4 -- a slot stuck a
// hair short would leave a resting tag a fraction of a pixel off the grid, i.e. soft.
constexpr float kSlotRate = 20.f;   // ~0.15 s to settle (12 read as slow in game)
inline float Follow(float cur, float want, float dt, float rate)
{
    const float r = cur + (want - cur) * (1.f - expf(-rate * (dt > 0.f ? dt : 0.f)));
    return fabsf(want - r) < 1e-4f ? want : r;
}

// The glyph quads AddText appended since vtx0, made to glide:
//   * ImFont::RenderText floors the pen to whole pixels (imgui_draw.cpp:3565): they go back by
//     the fraction of `pos` it dropped. Without it a text tag froze while its slot glided the
//     last pixel, then jumped 1 px the frame Follow landed (0.24 s after every split)
//   * each quad is padded half a texel into the atlas's zero padding ring (pos and uv alike, so
//     the mapping is unchanged). A tight bitmap has ink in its border texels, so the quad edge
//     cut it off and the pixel beside it flipped between nothing and half ink as the edge slid
//     across its centre. Padded, the dx9 backend's LINEAR filter fades it to 0 at the edge.
// At rest on a whole pixel nothing shows: the fraction is 0 and the padding samples zeros, the
// crisp 1:1 blit. BlitLabel rasterizes the same padded quads for the mitosis picture.
inline void SubPixelText(ImDrawList* dl, int vtx0, ImVec2 pos, const ImFont* font)
{
    const float fx = pos.x - floorf(pos.x), fy = pos.y - floorf(pos.y);
    const ImFontAtlas* at = font->ContainerAtlas;
    const float hu = 0.5f / at->TexWidth, hv = 0.5f / at->TexHeight;   // half a texel, uv
    ImDrawVert* v   = dl->VtxBuffer.Data + vtx0;   // Data read AFTER AddText: it may have grown
    ImDrawVert* end = dl->VtxBuffer.Data + dl->VtxBuffer.Size;
    for (; v + 3 < end; v += 4)   // RenderText: 4 vertices a glyph, corners 0 and 2 opposite
    {
        const float cx = (v[0].pos.x + v[2].pos.x) * 0.5f, cy = (v[0].pos.y + v[2].pos.y) * 0.5f;
        const float du = fabsf(v[2].uv.x - v[0].uv.x), dv = fabsf(v[2].uv.y - v[0].uv.y);
        const float hx = du > 0.f ? hu * fabsf(v[2].pos.x - v[0].pos.x) / du : 0.f;   // half a texel, px
        const float hy = dv > 0.f ? hv * fabsf(v[2].pos.y - v[0].pos.y) / dv : 0.f;
        for (int k = 0; k < 4; ++k)
        {
            const float sx = v[k].pos.x < cx ? -1.f : 1.f, sy = v[k].pos.y < cy ? -1.f : 1.f;
            v[k].pos.x += fx + sx * hx; v[k].pos.y += fy + sy * hy;
            v[k].uv.x  += sx * hu;      v[k].uv.y  += sy * hv;
        }
    }
}

// The bind fade, one frame, mitosis mode (movind.cpp). In: linear -- the split's timeline
// (SlideAt, EaseOut slots) is shaped on it. Out: the same timeline runs backwards, and every
// ease-out in it played backwards on a linear fade is an ease-IN: the row sat still for 60% of the
// fade, then the daughter slammed into its parent and the slot shut in the last frames (peak
// 4 px/frame at 97% of the fade -- "the labels snap back into place"). So out runs on
// s = (1 - tau)^2: sqrt(s) falls linearly (exact at any dt), the same 1 / speed seconds, the
// reconnect comes sooner and the merge lands at zero speed. Only the clock differs, so no frame
// shows a shape the way in does not.
inline float FadeStep(float s, bool on, float dt, float speed)
{
    if (!(dt > 0.f)) return s;
    if (on) return Sat(s + dt * speed);
    const float r = sqrtf(Sat(s)) - dt * speed;
    return r > 0.f ? r * r : 0.f;
}

// A lone tag's pop-in scale: half size -> full, ease-out cubic, no overshoot. From nothing with an
// overshoot read as too much scale in game.
inline float PopAt(float t)
{
    const float x = 1.f - Sat(t);
    return 0.5f + 0.5f * (1.f - x * x * x);
}

// Which tag a splitting tag divides off: its clearly older neighbour (higher fade), the older of
// two if both are. -1 = no neighbour to come from (a lone tag, or two born side by side): it pops.
inline int PickParent(const float* raw, int n, int i)
{
    int   best = -1;
    float br   = raw[i] + 0.02f;
    if (i > 0     && raw[i - 1] > br) { best = i - 1; br = raw[i - 1]; }
    if (i + 1 < n && raw[i + 1] > br)   best = i + 1;
    return best;
}

// A signed distance field over one sub-rect of the box, supersampled px, and its edge slope
// (SignedDist; null = a hard edge, 1). kOut outside it.
struct Field { int x0 = 0, y0 = 0, w = 0, h = 0; const float* d = nullptr; const float* s = nullptr; };

// Bilinear sample at a continuous box position (pixel centres at +0.5); the slope too if asked.
inline float Sample(const Field& f, float x, float y, float* slope = nullptr)
{
    if (slope) *slope = 1.f;
    const float fx = x - f.x0 - 0.5f, fy = y - f.y0 - 0.5f;
    if (!f.d || fx < 0.f || fy < 0.f || fx > f.w - 1.f || fy > f.h - 1.f) return kOut;
    int ix = (int)fx, iy = (int)fy;
    if (ix > f.w - 2) ix = f.w - 2 < 0 ? 0 : f.w - 2;
    if (iy > f.h - 2) iy = f.h - 2 < 0 ? 0 : f.h - 2;
    const float ax = fx - ix, ay = fy - iy;
    const int   o  = ix + 1 < f.w ? 1 : 0, row = iy + 1 < f.h ? f.w : 0;
    const auto  at = [&](const float* g)
    {
        const float* r0 = g + iy * f.w + ix;
        const float  a  = r0[0] + (r0[o] - r0[0]) * ax;
        const float  b  = r0[row] + (r0[row + o] - r0[row]) * ax;
        return a + (b - a) * ay;
    };
    if (slope && f.s) *slope = at(f.s);
    return at(f.d);
}

// One tag in the composed picture.
struct Comp
{
    Field self;                  // its own letters, where it is drawn
    Field from;                  // parent >= 0: the PARENT's letters at this tag's spot
    int   parent = -2;           // -2 settled, -1 popping in, >= 0 dividing off comps[parent]
    float m = 1.f;               // MorphAt
    float col[3] = { 1.f, 1.f, 1.f };
    // squash & stretch (miwarp.h) or the pop-in: the letters scaled by (sx, sy) about (px, py),
    // box ss px. Done on the SAMPLE position, so the edge stays a 1 ss px ramp at any scale
    float px = 0.f, py = 0.f, sx = 1.f, sy = 1.f;
};

// Signed distance to one tag's letters as drawn this frame: the squash & stretch undone on the
// sample position, then its own letters (settled) or the morph from where it started. `slope`,
// if asked: the edge's, the morph blending it like the distance.
inline float CompDist(const Comp& c, float x, float y, float* slope = nullptr)
{
    const float qx = c.px + (x - c.px) / c.sx, qy = c.py + (y - c.py) / c.sy;
    float s = 1.f;
    float d = Sample(c.self, qx, qy, slope ? &s : nullptr);
    if (c.parent >= 0)
    {
        float s0 = 1.f;
        const float f0 = Sample(c.from, qx, qy, slope ? &s0 : nullptr);
        d = f0 + (d - f0) * c.m;
        s = s0 + (s - s0) * c.m;
    }
    if (slope) *slope = s;
    // back to SCREEN distance: a field read at 2.5x is 2.5x too flat, and its edge ramp would
    // smear over 2.5x the pixels -- the blur. The tighter axis keeps the edge sharp both ways
    return d * fminf(c.sx, c.sy);
}

// The liquid strand between a dividing pair, box ss px. u runs along the split axis, v across.
struct Bridge
{
    float eP = 0.f, eC = 0.f;        // along u: the parent's and the daughter's facing edges
    float side = 1.f;                // +1: the daughter is toward +u
    float cv = 0.f;                  // across: the line the strand runs along
    float hs = 0.f;                  // half the font's stem thickness (0 = no strand: still touching)
    float depth = 0.f;               // DepthAt
    float cut = 0.f;                 // CutAt
    float r = 0.f;                   // fillet into the letters
    int   parent = -1, child = -1;   // for the colour; -1 = a bare strand
};

// Signed distance to the strand. w = 0 at the parent's edge, 1 at the daughter's; x = 0 at either
// edge, 1 at the middle. k = depth * x^2 thins it from the middle out: half thickness
// hs * (1 - k) - 0.35 hs k, zero at k ~ 0.74, so the strand parts just BEFORE depth 1 and the
// snap has a visible gap even at 14px (a zero-width pinch left a grey sub-pixel hairline). After the snap the same k measured on halves `cut` of the way shorter: the tapered
// ends pull back into the letters. k is continuous across the snap.
// kOut past either end: the ends sit half a stem inside the letters (Render), so the fillet along
// the strand's sides already closes the notch beside it. A distance to the end cap there instead
// let the fillet (0.18 fs) wrap round into the letter and fill the counter of an 'a' solid.
inline float BridgeDist(const Bridge& br, float u, float v, float* kOut_ = nullptr)
{
    const float span = (br.eC - br.eP) * br.side;
    if (span <= 0.f || br.hs <= 0.f || br.cut >= 1.f) return kOut;
    const float w = (u - br.eP) * br.side / span;
    if (w < 0.f || w > 1.f) return kOut;
    const float x = 1.f - fabsf(2.f * w - 1.f);
    float k;
    if (br.cut > 0.f)
    {
        if (x >= 1.f - br.cut) return kOut;   // pulled back past here
        const float xs = x / (1.f - br.cut);
        k = xs * xs;
    }
    else
        k = br.depth * x * x;
    if (kOut_) *kOut_ = k;
    return fabsf(v - br.cv) - br.hs * (1.f - 1.35f * k);
}

struct Scene
{
    const Comp*   comps = nullptr;   int nc = 0;
    const Bridge* bridges = nullptr; int nb = 0;
    bool axisX = true;               // split axis: x for a horizontal row, y for the vertical stack
    int  w = 0, h = 0, ss = 1;       // supersampled box size (multiples of ss), and the factor
};

// out = (w/ss)*(h/ss) A8R8G8B8, solid colour per owner, alpha = edge coverage only.
inline void Compose(const Scene& s, unsigned int* out)
{
    constexpr int kMax = 16;
    const int nc = s.nc < kMax ? s.nc : kMax;
    const int ss = s.ss, ow = s.w / ss, oh = s.h / ss;
    const float inv = 1.f / (float)(ss * ss);
    for (int oy = 0; oy < oh; ++oy)
        for (int ox = 0; ox < ow; ++ox)
        {
            // box filter the ss x ss block: coverage and colour, premultiplied
            float a = 0.f, r = 0.f, g = 0.f, b = 0.f;
            for (int sy = 0; sy < ss; ++sy)
                for (int sx = 0; sx < ss; ++sx)
                {
                    const float x = (float)(ox * ss + sx) + 0.5f, y = (float)(oy * ss + sy) + 0.5f;
                    const float u = s.axisX ? x : y, v = s.axisX ? y : x;

                    // the edge's slope, coverage per ss px: the letters' own (SignedDist), so a
                    // settled tag comes out as its atlas antialiasing, soft edges soft and hard
                    // ones hard. The strand keeps its crisp 1 ss px edge -- given the letters'
                    // it left a grey smear where the neck snaps. The fillet between them blends
                    // the two by SMin's own weight, so no edge jumps from one to the other
                    float best = kOut, slope = 1.f;
                    int   own  = 0;
                    for (int k = 0; k < nc; ++k)
                    {
                        float sk;
                        const float d = CompDist(s.comps[k], x, y, &sk);
                        if (d < best) { best = d; own = k; slope = sk; }
                    }
                    for (int i = 0; i < s.nb; ++i)
                    {
                        const Bridge& br = s.bridges[i];
                        float k = 0.f;
                        const float   db = BridgeDist(br, u, v, &k);
                        if (db >= kOut) continue;
                        // the bridge takes the colour of the half it is on
                        if (db < best && br.child >= 0 && br.child < nc && br.parent >= 0 && br.parent < nc)
                            own = (u - 0.5f * (br.eP + br.eC)) * br.side >= 0.f ? br.child : br.parent;
                        // the flare fades with the pinch: where the strand is thin, a full-size
                        // fillet against the letters a few px away would fill the snap back in
                        const float fr = br.r * (1.f - k);
                        const float wl = fr > 0.f ? Sat(0.5f + 0.5f * (db - best) / fr) : (best < db ? 1.f : 0.f);
                        slope = 1.f + (slope - 1.f) * wl;
                        best  = SMin(best, db, fr);
                    }
                    const float cov = Sat(0.5f - best * slope);
                    if (cov <= 0.f) continue;
                    const float* col = s.comps[own].col;
                    a += cov; r += cov * col[0]; g += cov * col[1]; b += cov * col[2];
                }
            unsigned int& o = out[oy * ow + ox];
            if (a <= 0.f) { o = 0; continue; }
            const float al = a * inv;
            r /= a; g /= a; b /= a;
            o = ((unsigned int)(Sat(al) * 255.f + 0.5f) << 24) |
                ((unsigned int)(Sat(r)  * 255.f + 0.5f) << 16) |
                ((unsigned int)(Sat(g)  * 255.f + 0.5f) <<  8) |
                 (unsigned int)(Sat(b)  * 255.f + 0.5f);
        }
}

// ---------------------------------------------------------------------------------------------
// The whole frame, as movind.cpp draws it and migoo_test checks it.
constexpr int kMaxTags = 16;

// One tag taking part in a split. pos / size = its settled layout slot (screen px, the spot
// AddText would draw it at); parent as in Comp, indexing this same array.
struct Tag { const char* text; ImVec2 pos, size; float col[3]; float raw; int parent; };

// Buffers kept between frames, so a split allocates on its first frame only.
struct Work
{
    std::vector<unsigned char> cov;
    std::vector<float>         pool, dOut, dIn, sOut, sIn;
    std::vector<unsigned int>  px;       // the result: w*h A8R8G8B8
    Comp   comps[kMaxTags];
    Bridge bridges[kMaxTags];
};

// Rasterizes the splitting tags into wk.px. Out: the box's integer screen origin and size.
// false = nothing drawable (no atlas pixels, box too big) -- draw the tags as plain text.
inline bool Render(const Tag* tags, int n, ImFont* font, float fs, bool axisX, Work& wk,
                   int& ox, int& oy, int& w, int& h)
{
    if (n <= 0 || n > kMaxTags) return false;
    // supersampling: less at big sizes, where the glyphs already have the pixels
    const int   ss  = fs < 20.f ? 3 : 2;
    const float f   = (float)ss;
    const float pad = ceilf(fs * 0.16f + 3.f);

    // the x-height band, from the tag's top: the body of lowercase letters, where the strand
    // runs and what it meets in the vertical stack
    float xTop = fs * 0.35f, xBot = fs * 0.8f;
    if (const ImFontGlyph* gx = font->FindGlyph((ImWchar)'x'))
    {
        const float sc = fs / font->FontSize;
        if (gx->Y1 > gx->Y0) { xTop = gx->Y0 * sc; xBot = gx->Y1 * sc; }
    }
    // the font's stem, off the atlas: the ink across the middle row of 'l'. The strand is exactly
    // this thick, so it is the same weight as every stroke next to it
    float stem = fs * 0.16f;
    if (const ImFontGlyph* gl = font->FindGlyph((ImWchar)'l'))
        if (ImFontAtlas* at = font->ContainerAtlas)
            if (at->TexPixelsAlpha8 && gl->X1 > gl->X0)
            {
                const int row = (int)(((gl->V0 + gl->V1) * 0.5f) * at->TexHeight);
                const int c0  = (int)(gl->U0 * at->TexWidth), c1 = (int)(gl->U1 * at->TexWidth);
                float ink = 0.f;
                for (int c = c0; c < c1; ++c) ink += at->TexPixelsAlpha8[row * at->TexWidth + c] / 255.f;
                if (ink > 0.5f) stem = ink * (fs / font->FontSize);
            }

    // where each tag's letters are this frame: a daughter slides out of its parent's centre
    const auto slot = [&](int i)
    { return ImVec2(tags[i].pos.x + tags[i].size.x * 0.5f, tags[i].pos.y + fs * 0.5f); };
    ImVec2 ctr[kMaxTags];
    for (int i = 0; i < n; ++i)
    {
        ctr[i] = slot(i);
        const int p = tags[i].parent;
        if (p < 0) continue;
        const ImVec2 cp = slot(p);
        const float  k  = SlideAt(tags[i].raw);
        ctr[i] = ImVec2(cp.x + (ctr[i].x - cp.x) * k, cp.y + (ctr[i].y - cp.y) * k);
    }

    // each tag's field rect (screen px): its letters, and the parent's letters it starts as.
    // The pop's 3% overshoot stays inside the pad
    float rx0[kMaxTags], ry0[kMaxTags], rx1[kMaxTags], ry1[kMaxTags];
    float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
    for (int i = 0; i < n; ++i)
    {
        const int p = tags[i].parent;
        float hw = tags[i].size.x * 0.5f;
        if (p >= 0) hw = fmaxf(hw, tags[p].size.x * 0.5f);
        rx0[i] = ctr[i].x - hw - pad;        rx1[i] = ctr[i].x + hw + pad;
        ry0[i] = ctr[i].y - fs * 0.5f - pad; ry1[i] = ctr[i].y + fs * 0.5f + pad;
        bx0 = fminf(bx0, rx0[i]); by0 = fminf(by0, ry0[i]);
        bx1 = fmaxf(bx1, rx1[i]); by1 = fmaxf(by1, ry1[i]);
    }
    ox = (int)floorf(bx0); oy = (int)floorf(by0);
    w  = (int)ceilf(bx1) - ox; h = (int)ceilf(by1) - oy;
    if (w < 2 || h < 2 || w > 1024 || h > 1024) return false;

    // rects in box ss px, and the pool they all live in
    int X0[kMaxTags], Y0[kMaxTags], RW[kMaxTags], RH[kMaxTags];
    size_t total = 0, most = 0;
    for (int i = 0; i < n; ++i)
    {
        X0[i] = (int)floorf((rx0[i] - ox) * f); Y0[i] = (int)floorf((ry0[i] - oy) * f);
        RW[i] = (int)ceilf((rx1[i] - ox) * f) - X0[i];
        RH[i] = (int)ceilf((ry1[i] - oy) * f) - Y0[i];
        const size_t a = (size_t)RW[i] * RH[i];
        total += 2 * a * (tags[i].parent >= 0 ? 2 : 1);   // distances + slopes, per field
        if (a > most) most = a;
    }
    wk.pool.resize(total); wk.cov.resize(most);
    wk.dOut.resize(most);  wk.dIn.resize(most); wk.sOut.resize(most); wk.sIn.resize(most);

    float* dst = wk.pool.data();
    const auto field = [&](Field& F, int i, const char* text, float textW) -> bool
    {
        const size_t a = (size_t)RW[i] * RH[i];
        std::fill(wk.cov.begin(), wk.cov.begin() + a, (unsigned char)0);
        const ImVec2 at((ctr[i].x - textW * 0.5f) * f, (ctr[i].y - fs * 0.5f) * f);
        if (!BlitLabel(wk.cov.data(), RW[i], RH[i], ImVec2(ox * f + X0[i], oy * f + Y0[i]),
                       font, fs * f, text, at))
            return false;
        SignedDist(wk.cov.data(), RW[i], RH[i], wk.dOut.data(), wk.dIn.data(),
                   wk.sOut.data(), wk.sIn.data(), dst, dst + a);
        F.x0 = X0[i]; F.y0 = Y0[i]; F.w = RW[i]; F.h = RH[i]; F.d = dst; F.s = dst + a;
        dst += 2 * a;
        return true;
    };

    // a tag's letters along the split axis, screen px: its width across a row, its x-height
    // band up a stack (ascenders and descenders poke past it; the bridge meets the body)
    const auto extent = [&](int i, float halfW, float& lo, float& hi)
    {
        if (axisX) { lo = ctr[i].x - halfW; hi = ctr[i].x + halfW; }
        else       { const float top = ctr[i].y - fs * 0.5f; lo = top + xTop; hi = top + xBot; }
    };

    // squash & stretch (miwarp.h), from where the halves are THIS frame: each half of a dividing
    // pair leans toward the other along the axis about its own outer edge, thinning across.
    // wPiv / wSa along the axis, screen px; across it the scale is 1 / wSa about the centre.
    // A parent with daughters on both sides takes the last one's -- one pivot per tag
    float wPiv[kMaxTags], wSa[kMaxTags];
    for (int i = 0; i < n; ++i) { wPiv[i] = axisX ? ctr[i].x : ctr[i].y; wSa[i] = 1.f; }
    for (int i = 0; i < n; ++i)
    {
        const int p = tags[i].parent;
        if (p < 0) continue;
        const float uP = axisX ? ctr[p].x : ctr[p].y, uC = axisX ? ctr[i].x : ctr[i].y;
        const float m  = MorphAt(tags[i].raw);
        const float eC = axisX ? (tags[p].size.x + (tags[i].size.x - tags[p].size.x) * m) * 0.5f : fs * 0.5f;
        const float eP = axisX ? tags[p].size.x * 0.5f : fs * 0.5f;
        const miw::Warp wc = miw::SplitWarp(uC, eC, uP, tags[i].raw);
        const miw::Warp wp = miw::SplitWarp(uP, eP, uC, tags[i].raw);
        wPiv[i] = wc.piv; wSa[i] = wc.sa;
        wPiv[p] = wp.piv; wSa[p] = wp.sa;
    }
    const auto warpU = [&](int i, float u) { return wPiv[i] + (u - wPiv[i]) * wSa[i]; };

    for (int i = 0; i < n; ++i)
    {
        const Tag& t = tags[i];
        Comp& c = wk.comps[i];
        c = Comp{};
        c.parent = t.parent;
        for (int k = 0; k < 3; ++k) c.col[k] = t.col[k];
        if (!field(c.self, i, t.text, t.size.x)) return false;
        if (axisX) { c.px = (wPiv[i] - ox) * f; c.sx = wSa[i]; c.py = (ctr[i].y - oy) * f; c.sy = 1.f / wSa[i]; }
        else       { c.py = (wPiv[i] - oy) * f; c.sy = wSa[i]; c.px = (ctr[i].x - ox) * f; c.sx = 1.f / wSa[i]; }

        if (t.parent >= 0)
        {
            const Tag& p = tags[t.parent];
            if (!field(c.from, i, p.text, p.size.x)) return false;
            c.m = MorphAt(t.raw);
            // the daughter's colour turns with its letters: the parent's while it is a copy
            for (int k = 0; k < 3; ++k) c.col[k] = p.col[k] + (t.col[k] - p.col[k]) * c.m;
        }
        else if (t.parent == -1)
        {
            // pop in about its own centre
            c.px = (ctr[i].x - ox) * f; c.py = (ctr[i].y - oy) * f;
            c.sx = c.sy = PopAt(t.raw);
        }
    }

    // the strands, now that every half's field exists
    int nb = 0;
    for (int i = 0; i < n; ++i)
    {
        const Tag& t = tags[i];
        if (t.parent < 0) continue;
        const Tag&  p    = tags[t.parent];
        const float m    = wk.comps[i].m;
        const float uP   = axisX ? ctr[t.parent].x : ctr[t.parent].y;
        const float uC   = axisX ? ctr[i].x : ctr[i].y;
        const float side = uC >= uP ? 1.f : -1.f;
        float pLo, pHi, cLo, cHi;
        extent(t.parent, p.size.x * 0.5f, pLo, pHi);
        extent(i, (p.size.x + (t.size.x - p.size.x) * m) * 0.5f, cLo, cHi);
        // the facing edges of the (leaning) text boxes, screen px
        const float o   = axisX ? ox : oy;
        const float bP  = warpU(t.parent, side > 0.f ? pHi : pLo);
        const float bC  = warpU(i, side > 0.f ? cLo : cHi);
        const float cv  = axisX ? (ctr[t.parent].y - fs * 0.5f + (xTop + xBot) * 0.5f - oy) * f
                                : (ctr[t.parent].x - ox) * f;
        // where each half's INK meets the strand's line, box ss px: marched along the line from
        // its box edge into the tag (dir) to the first inked sample. The box is the advance width
        // -- side bearings, round letters (the 'a' of "air") and overhangs (the arm of a 't') sit
        // well inside or past it: a strand ended at the box hung short of the letter with its
        // notch open beside it, and gated on the boxes it stayed away while the ink on its line
        // was already pixels apart. Starts a little out in the gap, for ink poking past the box.
        // Nothing on the line within reach (a gap between letters up a stack): the box edge.
        // The hit is the zero crossing between the last two samples, not the sample: whole steps
        // walked the strand's ends (and its neck, measured on them) in 1 ss px jumps while the
        // daughter's letters morphed. Bisected before it is read off a straight line: the field
        // bends between samples (bilinear kinks, the morph's blend), and a line through the two
        // samples alone blipped the end ~0.1 px
        const auto ink = [&](int ci, float edge, float dir, float reach, bool& hit) -> float
        {
            const float from = (edge - o) * f;
            const float back = fminf(fmaxf((bC - bP) * side, 0.f) * 0.5f, fs * 0.1f) * f;
            const auto  at   = [&](float u) { return CompDist(wk.comps[ci], axisX ? u : cv, axisX ? cv : u); };
            float prev = kOut;
            for (float s = -back; s <= reach * f; s += 1.f)
            {
                const float u = from + dir * s;
                const float d = at(u);
                if (d < 0.f)
                {
                    hit = true;
                    if (prev >= kOut) return u;
                    float a = u - dir, da = prev, b = u, db = d;
                    for (int it = 0; it < 4; ++it)
                    {
                        const float mid = 0.5f * (a + b), dm = at(mid);
                        if (dm < 0.f) { b = mid; db = dm; } else { a = mid; da = dm; }
                    }
                    return a + (b - a) * da / (da - db);
                }
                prev = d;
            }
            hit = false;
            return from;
        };
        const float reachP = axisX ? p.size.x * 0.5f : xBot - xTop;
        const float reachC = axisX ? (p.size.x + (t.size.x - p.size.x) * m) * 0.5f : xBot - xTop;
        bool hitP, hitC;
        const float iP  = ink(t.parent, bP, -side, reachP, hitP);
        const float iC  = ink(i, bC, side, reachC, hitC);
        const float gap = (iC - iP) * side / f;   // screen px of bare line between the halves
        // the ends reach into the letters so the strand starts under the fillet: to the middle of
        // the stroke it met, or the old fixed tuck past a box edge
        const float tP  = hitP ? stem * 0.5f : fs * 0.08f, tC = hitC ? stem * 0.5f : fs * 0.08f;

        Bridge& br = wk.bridges[nb++];
        br = Bridge{};
        br.eP = iP - side * tP * f;
        br.eC = iC + side * tC * f;
        br.side = side;
        br.cv = cv;
        // grows to full weight over the first sliver of gap, so coming apart is not a pop
        br.hs     = gap > 0.f ? stem * 0.5f * Smooth(0.f, fs * 0.05f, gap) * f : 0.f;
        br.depth  = DepthAt(t.raw);
        br.cut    = CutAt(t.raw);
        br.r      = fs * 0.18f * f;   // the liquid flare where the strand meets the letters
        br.parent = t.parent; br.child = i;
    }

    Scene s;
    s.comps = wk.comps; s.nc = n;
    s.bridges = wk.bridges; s.nb = nb;
    s.axisX = axisX;
    s.w = w * ss; s.h = h * ss; s.ss = ss;
    wk.px.resize((size_t)w * h);
    Compose(s, wk.px.data());
    return true;
}
}
