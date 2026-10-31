// Gate test for the mitosis split (src/hacks/hud/migoo.h). No device, no game: distance fields,
// the liquid bridge and the composed picture are all checkable offline -- plus mig::Render, the
// exact function movind.cpp draws with, on the real font (imgui's freetype atlas, tahoma bold),
// every frame written to build/testout to look at.
//
// WHAT WOULD BREAK WITHOUT IT
//   * a bridge that does not look like the letters (the x-height bar that read as an arrow in
//     game). Checked: the strand is exactly the stem weight it is given, nothing else in the gap,
//     it flares into the letters, thins, snaps, and pulls back; and no real-font frame swells
//     past its letters plus a stem-thick strand across the gap.
//   * squash / stretch instead of a division -- the whole-tag scale this replaced. Checked: the
//     letters are unscaled at both ends, the pair is JOINED by the bridge before kSnap and cut
//     through at it, and the stubs are gone by the end.
//   * the copy not being invisible at t = 0 (a new tag popping in on top of its parent).
//   * the letter morph being a cross-fade instead of a shape interpolation.
//   * any alpha on the letters, a popping tag showing a speck at t = 0, or a scaled tag going
//     blurry (the text-quad scale was a bilinear stretch).
//   * a wrong distance transform (a missed neighbour in either sweep) -- against brute force.
//
// Build + run: cmake --build build --config Release --target migoo_test && build\Release\migoo_test.exe
// (run from build\ so the frames land in build\testout\)

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <vector>
#include "../src/hacks/hud/migoo.h"
#include "freetype/imgui_freetype.h"
#include "imgui_internal.h"   // ImDrawListSharedData: a bare draw list for the text-pass check

static int g_fail = 0;

static void Check(bool ok, const char* what)
{
    if (!ok) { printf("FAIL  %s\n", what); ++g_fail; }
}

static void Near(float got, float want, float tol, const char* what)
{
    if (!(fabsf(got - want) <= tol))
    {
        printf("FAIL  %s: got %.4f want %.4f (tol %.4f)\n", what, got, want, tol);
        ++g_fail;
    }
}

static const int W = 64, H = 32;
typedef std::vector<unsigned char> Cov;

static void Rect(Cov& c, int x0, int y0, int x1, int y1, int w = W)
{
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            c[y * w + x] = 255;
}

// a whole-box signed field for a synthetic coverage shape
struct Sdf
{
    std::vector<float> d, s;
    mig::Field f;
    explicit Sdf(const Cov& c) : d(W * H), s(W * H)
    {
        std::vector<float> o(W * H), in(W * H), so(W * H), si(W * H);
        mig::SignedDist(c.data(), W, H, o.data(), in.data(), so.data(), si.data(), d.data(), s.data());
        f.x0 = 0; f.y0 = 0; f.w = W; f.h = H; f.d = d.data(); f.s = s.data();
    }
};

// one Compose at ss = 1 over the W x H box, split axis x
static std::vector<unsigned> Run(const mig::Comp* c, int nc, const mig::Bridge* br = nullptr, int nb = 0)
{
    std::vector<unsigned> out(W * H);
    mig::Scene s;
    s.comps = c; s.nc = nc; s.bridges = br; s.nb = nb;
    s.axisX = true; s.w = W; s.h = H; s.ss = 1;
    mig::Compose(s, out.data());
    return out;
}

static unsigned A(const std::vector<unsigned>& px, int x, int y, int w = W) { return px[y * w + x] >> 24; }

static int Area(const std::vector<unsigned>& px)
{
    int n = 0;
    for (unsigned v : px) if ((v >> 24) > 128) ++n;
    return n;
}

static float EaseOutBack(float x) { return mig::EaseOut(x); }   // movind.cpp's slot ease

// the in-game face: tahoma bold on imgui's freetype atlas, BuildFontAtlas's flags. null = skip
static ImFont* Tahoma(ImFontAtlas& atlas, float fs)
{
    ImFontConfig cfg;
    cfg.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;
    atlas.FontBuilderIO = ImGuiFreeType::GetBuilderForFreeType();
    ImFont* font = atlas.AddFontFromFileTTF("C:\\Windows\\Fonts\\tahomabd.ttf", fs, &cfg,
                                            atlas.GetGlyphRangesDefault());
    if (!font || !atlas.Build() || !atlas.TexPixelsAlpha8)
    {
        printf("SKIP  real font: no tahomabd.ttf / atlas build failed\n");
        return nullptr;
    }
    return font;
}

// ------------------------------------------------------------------------------------------
// A tag gliding by fractions of a pixel, as every slot glides in movind.cpp (a.mp4, 1-3 s: the
// slowing tail of each split walked in steps with stalls, then the tags jumped a pixel when the
// picture handed off to text, and again ~0.24 s later when their slots landed).
//   * the picture's ink moves WITH the tag. Built on a thresholded shape it moved in 1/ss px
//     steps; the centroid offset then spans ~1/ss over a pixel of glide
//   * the picture looks like the text it hands off to (GPU text emulated: BlitLabel at 1x = the
//     atlas sampled bilinear at the same sub-pixel spot, which is what the dx9 backend does)
//   * a strand's ends ride the ink continuously (the ink march interpolates its crossing)
//   * the text pass: AddText floors the pen, mig::UnfloorText puts the fraction back
static void SubPixelGlide(float fs)
{
    ImFontAtlas atlas;
    ImFont* font = Tahoma(atlas, fs);
    if (!font) return;
    const char* kT = "lj";
    const ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, kT);
    const float  X = fs * 6.f, Y = fs * 4.f;
    printf("\n      sub-pixel glide, tahoma bold %gpx\n", fs);

    mig::Work wk;
    float lo = 1e9f, hi = -1e9f, worst = 0.f, meanSum = 0.f;
    const int steps = 20;
    for (int k = 0; k <= steps; ++k)
    {
        const float fx = (float)k / steps;
        const mig::Tag t = { kT, ImVec2(X + fx, Y), sz, { 1.f, 1.f, 1.f }, 1.f, -2 };
        int ox, oy, w, h;
        if (!mig::Render(&t, 1, font, fs, true, wk, ox, oy, w, h)) { Check(false, "Render draws one tag"); return; }
        double m = 0.0, mx = 0.0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const double a = (wk.px[y * w + x] >> 24) / 255.0;
                m += a; mx += a * (ox + x + 0.5);
            }
        const float off = (float)(mx / m) - (X + fx);
        lo = fminf(lo, off); hi = fmaxf(hi, off);

        std::vector<unsigned char> txt(w * h, 0);
        mig::BlitLabel(txt.data(), w, h, ImVec2((float)ox, (float)oy), font, fs, kT, t.pos);
        float sum = 0.f; int cnt = 0;
        for (int i = 0; i < w * h; ++i)
        {
            const int a = (int)(wk.px[i] >> 24), b = txt[i];
            if (!a && !b) continue;
            sum += (float)abs(a - b); ++cnt;
            worst = fmaxf(worst, (float)abs(a - b));
        }
        meanSum += sum / cnt;
    }
    const float mean = meanSum / (steps + 1);
    printf("      picture ink centroid - tag x: %.3f .. %.3f px (span %.3f)\n", lo, hi, hi - lo);
    printf("      picture vs the text it hands off to: mean |dA| %.1f, worst %.0f (of 255)\n", mean, worst);
    Check(hi - lo < 0.03f, "the picture's ink moves with a gliding tag (no 1/ss px steps)");
    Check(mean < 12.f, "the picture looks like the text it hands off to");

    // a strand, the pair glided by fractions of a pixel: both ends move with it. Walked in whole
    // samples, each end stepped a full ss px (1/ss of a pixel) at a time.
    // raw 0.6: the daughter is its own letters (morph done), the stubs pulling back, slow: tight.
    // raw 0.35: still morphing, the daughter's field a blend of two letter sets, and a soft (two
    // texel) atlas edge is only (0.5 - coverage) * ss deep where true distance is twice that --
    // read off that far field the end creeps at ~60-75% and catches up. Fast frames, inside the
    // ink: held to half a step
    for (const float raw : { 0.35f, 0.6f })
    {
        const char* kP = "tb"; const char* kC = "air";
        const ImVec2 pSz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, kP);
        const ImVec2 cSz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, kC);
        const float  anim = EaseOutBack(raw), sp = fs * 0.2f, ss = fs < 20.f ? 3.f : 2.f;
        float pLo = 1e9f, pHi = -1e9f, cLo = 1e9f, cHi = -1e9f;
        for (int k = 0; k <= steps; ++k)
        {
            const float fx = (float)k / steps, x = X + fx;
            mig::Tag tags[2] = {
                { kP, ImVec2(x, Y), pSz, { 1.f, 1.f, 1.f }, 1.f, -2 },
                { kC, ImVec2(x + pSz.x + sp + (cSz.x * anim - cSz.x) * 0.5f, Y), cSz, { 1.f, 1.f, 1.f }, raw, 0 } };
            int ox, oy, w, h;
            if (!mig::Render(tags, 2, font, fs, true, wk, ox, oy, w, h)) { Check(false, "Render draws a pair"); return; }
            const mig::Bridge& br = wk.bridges[0];
            const float eP = ox + br.eP / ss - fx, eC = ox + br.eC / ss - fx;
            pLo = fminf(pLo, eP); pHi = fmaxf(pHi, eP); cLo = fminf(cLo, eC); cHi = fmaxf(cHi, eC);
        }
        const float tol = raw < 0.5f ? 0.5f / ss : 0.03f;
        printf("      strand ends at t %.2f - pair x: parent span %.3f, daughter span %.3f px (tol %.3f)\n",
               raw, pHi - pLo, cHi - cLo, tol);
        Check(pHi - pLo < 0.03f && cHi - cLo < tol, "the strand's ends ride the ink (no whole ss px steps)");
    }

    // ...and through TIME: while the daughter morphs its ink moves inside its box, so an end found
    // on whole march samples jumped a full ss px each time the ink passed one (the samples ride
    // the box edge, so the sub-pixel glide above cannot see it). Swept finely through the morph
    // and pull, the end's second difference stays small; a step shows as a spike of a sample
    {
        const char* kP = "tb"; const char* kC = "air";
        const ImVec2 pSz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, kP);
        const ImVec2 cSz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, kC);
        const float  sp = fs * 0.2f, ss = fs < 20.f ? 3.f : 2.f;
        float e[3] = {}, worst = 0.f;
        int   seen = 0;
        for (float raw = 0.25f; raw <= 0.5f + 1e-4f; raw += 0.0025f)
        {
            const float anim = EaseOutBack(raw);
            mig::Tag tags[2] = {
                { kP, ImVec2(X, Y), pSz, { 1.f, 1.f, 1.f }, 1.f, -2 },
                { kC, ImVec2(X + pSz.x + sp + (cSz.x * anim - cSz.x) * 0.5f, Y), cSz, { 1.f, 1.f, 1.f }, raw, 0 } };
            int ox, oy, w, h;
            if (!mig::Render(tags, 2, font, fs, true, wk, ox, oy, w, h)) { Check(false, "Render draws a pair"); return; }
            e[0] = e[1]; e[1] = e[2]; e[2] = ox + wk.bridges[0].eC / ss;
            if (++seen >= 3) worst = fmaxf(worst, fabsf(e[2] - 2.f * e[1] + e[0]));
            printf("DBG2 %.4f %.3f %.3f\n", raw, e[2], ox + wk.bridges[0].eP / ss);
        }
        printf("      daughter's strand end through the morph: worst 2nd difference %.3f px (a step: %.3f)\n",
               worst, 1.f / ss);
        Check(worst < 0.25f / ss, "the strand's end moves smoothly through the morph (no ss px steps)");
    }

    // the text pass: the quads land on the tag's sub-pixel spot, not the floored pen, padded half
    // a texel (= half a px at the atlas's own size) with the uv moved to match
    {
        ImDrawListSharedData sd;
        ImDrawList dl(&sd);
        dl._ResetForNewFrame();
        dl.PushClipRect(ImVec2(0.f, 0.f), ImVec2(4096.f, 4096.f));
        const ImFontGlyph* g = font->FindGlyph((ImWchar)'l');
        const float hu = 0.5f / atlas.TexWidth, hv = 0.5f / atlas.TexHeight;
        bool ok = g != nullptr;
        for (float f : { 0.f, 0.3f, 0.75f })
        {
            const ImVec2 p(X + f, Y + f * 0.5f);
            const int v0 = dl.VtxBuffer.Size;
            dl.AddText(font, fs, p, IM_COL32_WHITE, kT);
            mig::SubPixelText(&dl, v0, p, font);
            ok = ok && dl.VtxBuffer.Size > v0;
            if (!ok) break;
            const ImDrawVert& v = dl.VtxBuffer[v0];   // the first glyph's top-left corner
            ok = fabsf(v.pos.x - (p.x + g->X0 - 0.5f)) < 1e-3f && fabsf(v.pos.y - (p.y + g->Y0 - 0.5f)) < 1e-3f
              && fabsf(v.uv.x - (g->U0 - hu)) < 1e-6f && fabsf(v.uv.y - (g->V0 - hv)) < 1e-6f;
        }
        Check(ok, "text: the quads sit at the tag's sub-pixel spot (not the floored pen), padded");
    }
}

// ------------------------------------------------------------------------------------------
// mig::Render on the real font, laid out the way movind.cpp lays it out.
//   horizontal: kC divides off kP to its LEFT (after = false) or RIGHT, row centred
//   vertical:   kC divides off kP ABOVE it (the default stack)
static void RealFontSplit(float fs, bool axisX, const char* kP, const char* kC, bool after = false)
{
    ImFontAtlas atlas;
    ImFont* font = Tahoma(atlas, fs);
    if (!font) return;

    const ImVec2 pSz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, kP);
    const ImVec2 cSz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, kC);
    const float  colP[3] = { 0.92f, 0.92f, 0.92f }, colC[3] = { 0.92f, 0.92f, 0.92f };
    const float  X = fs * 6.f, Y = fs * 4.f;   // the screen anchor the layout centres on

    printf("\n      real-font split, tahoma bold %gpx, %s: \"%s\" divides off \"%s\"\n",
           fs, axisX ? "row" : "stack", kC, kP);
    printf("       t    slide depth  cut  morph  bridge  ink/letters  neck\n");

    const float ts[] = { 0.f, 0.05f, 0.1f, 0.15f, 0.2f, 0.25f, 0.3f, 0.35f, 0.4f, 0.45f,
                         0.5f, mig::kSnap, 0.6f, 0.65f, 0.7f, 0.75f, 0.8f, 0.9f, 1.f };
    char tag[32];
    snprintf(tag, sizeof(tag), "%s%02d%s", axisX ? "h" : "v", (int)fs, kC);
    int frame = 0;
    mig::Work wk;
    for (float t : ts)
    {
        // movind's layout: each slot scaled by the eased fade
        const float anim = EaseOutBack(t);
        mig::Tag tags[2];
        if (axisX)
        {
            const float sp = fs * 0.2f;
            const float slotC = cSz.x * anim;
            const float total = slotC + sp * 1.f + pSz.x;   // [C][gap][P], or [P][gap][C] after
            float x = X - total * 0.5f;
            if (after)
            {
                tags[0] = { kP, ImVec2(x, Y), pSz, {}, 1.f, -2 };
                x += pSz.x + sp;
                tags[1] = { kC, ImVec2(x + (slotC - cSz.x) * 0.5f, Y), cSz, {}, t, 0 };
            }
            else
            {
                tags[1] = { kC, ImVec2(x + (slotC - cSz.x) * 0.5f, Y), cSz, {}, t, 0 };
                x += slotC + sp;
                tags[0] = { kP, ImVec2(x, Y), pSz, {}, 1.f, -2 };
            }
        }
        else
        {
            tags[0] = { kP, ImVec2(X - pSz.x * 0.5f, Y), pSz, {}, 1.f, -2 };
            tags[1] = { kC, ImVec2(X - cSz.x * 0.5f, Y - pSz.y * anim), cSz, {}, t, 0 };
        }
        for (int k = 0; k < 3; ++k) { tags[0].col[k] = colP[k]; tags[1].col[k] = colC[k]; }

        int ox, oy, w, h;
        if (!mig::Render(tags, 2, font, fs, axisX, wk, ox, oy, w, h))
        {
            Check(false, "mig::Render draws a two-tag split");
            return;
        }
        std::vector<unsigned> out = wk.px;

        // reference: each label alone, settled (a one-tag Render of it = the same raster path)
        const auto alone = [&](const mig::Tag& one)
        {
            mig::Tag t1 = one; t1.parent = -2; t1.raw = 1.f;
            mig::Work w1;
            int ax, ay, aw, ah;
            std::vector<unsigned char> m(w * h, 0);
            if (!mig::Render(&t1, 1, font, fs, axisX, w1, ax, ay, aw, ah)) return m;
            for (int y = 0; y < ah; ++y)
                for (int x = 0; x < aw; ++x)
                {
                    const int bx = ax - ox + x, by = ay - oy + y;
                    if (bx >= 0 && by >= 0 && bx < w && by < h)
                        m[by * w + bx] = (unsigned char)(w1.px[y * aw + x] >> 24);
                }
            return m;
        };
        const auto inkP = alone(tags[0]);
        const auto inkC = alone(tags[1]);   // the child's own letters in its own slot

        int lit = 0, inkPn = 0, inkCn = 0;
        for (int i = 0; i < w * h; ++i)
        {
            lit   += (out[i] >> 24) > 128;
            inkPn += inkP[i] > 128;
            inkCn += inkC[i] > 128;
        }

        // the bridge's neck: the brightest pixel across it at its thinnest, out of its middle column
        // (row, for the stack) and the one either side. The break sits at its true sub-pixel
        // place, ~1.3 px wide at kSnap on 14px: centred on a pixel it blanks that column, on a
        // pixel boundary it leaves two columns a third lit -- one fixed column read that phase
        const mig::Bridge& br = wk.bridges[0];
        const int   ss = fs < 20.f ? 3 : 2;
        const bool  apart = br.hs > 0.f;
        const int   mc = (int)floorf((br.eP + br.eC) * 0.5f / ss);
        const auto  across = [&](int c)
        {
            int m = 0;
            if (c >= 0 && c < (axisX ? w : h))
                for (int k = 0; k < (axisX ? h : w); ++k)
                {
                    const int a = axisX ? (int)(out[k * w + c] >> 24) : (int)(out[c * w + k] >> 24);
                    if (a > m) m = a;
                }
            return m;
        };
        int neck = 0;
        if (apart) neck = std::min(across(mc), std::min(across(mc - 1), across(mc + 1)));
        // the most ink the strand may add: the gap times its thickness plus both fillets
        const float gapPx = apart ? fabsf(br.eC - br.eP) / ss : 0.f;
        const float band  = gapPx * (2.f * (br.hs + br.r) / ss + 2.f);
        const int   letters = inkPn + (inkPn > inkCn ? inkPn : inkCn);

        printf("      %4.2f  %4.2f  %4.2f  %4.2f  %4.2f   %4.1f    %6.0f%%     %3d\n", t, mig::SlideAt(t),
               mig::DepthAt(t), mig::CutAt(t), mig::MorphAt(t), gapPx, 100.f * lit / letters, neck);

        // the strand REACHES the ink: on every row of its full-weight band, each pixel from a
        // quarter of the way along it to the first inked pixel of either half is lit. Ended at the
        // text box (the advance width) it hung short of inset letters, and the notch beside its end
        // stayed open -- a dark slit before the 'a' of "air" in game, on the rows NEXT to its centre
        // line (the centre line alone touched). Checked while it is at full weight, the row only:
        // up a stack its line is the parent's centre column, which can fall between two letters
        if (axisX && (fabsf(t - 0.3f) < 1e-4f || fabsf(t - 0.35f) < 1e-4f))
        {
            const int  dir   = br.side > 0.f ? 1 : -1;
            const auto inked = [&](int ci, int x, int y)   // tag ci's own letters (no strand)
            { return mig::CompDist(wk.comps[ci], (x + 0.5f) * ss, (y + 0.5f) * ss) < 0.f; };
            const int line = (int)floorf(br.cv / ss);
            int kp = mc, kc = mc;
            while (kp >= 0 && kp < w && !inked(0, kp, line)) kp -= dir;
            while (kc >= 0 && kc < w && !inked(1, kc, line)) kc += dir;
            bool found = !inked(0, mc, line) && !inked(1, mc, line) && kp >= 0 && kp < w && kc >= 0 && kc < w;
            // the strand's half weight a quarter of the way in, less half a pixel: rows inside it
            const float hq = br.hs * (1.f - 1.35f * br.depth * 0.25f) - 0.5f * ss;
            const int   q  = found ? (kc - kp) / 4 : 0;
            int slit = 0, rows = 0;
            for (int y = 0; found && y < h; ++y)
            {
                if (fabsf((y + 0.5f) * ss - br.cv) > hq) continue;
                ++rows;
                for (int half = 0; half < 2; ++half)
                {
                    const int step = half ? dir : -dir;
                    int x = half ? kc - q : kp + q;
                    while (x >= 0 && x < w && !inked(half, x, y)) { slit += (int)(out[y * w + x] >> 24) <= 128; x += step; }
                    if (x < 0 || x >= w) found = false;
                }
            }
            printf("            strand line %d, %d rows: parent ink %d, daughter ink %d, %d unlit px before them\n",
                   line, rows, kp, kc, slit);
            Check(apart && found && rows > 0 && slit == 0,
                  "the strand reaches both halves' ink on every row (no slit at the letters)");
        }

        // squash & stretch is IN the picture: at its peak Render hands both halves the miwarp.h
        // lean, stretched along the split and thinned across. Checked on the scales themselves:
        // the 4% it is now is under a pixel on these sizes, so an ink count cannot see it (the
        // pop test covers the sampling through a scale)
        if (t == 0.5f)
        {
            const float along = 1.f + miw::kStretch;   // sin(pi * 0.5) = 1
            bool ok = true;
            for (int k = 0; k < 2; ++k)
            {
                const mig::Comp& c = wk.comps[k];
                const float sa = axisX ? c.sx : c.sy, sp = axisX ? c.sy : c.sx;
                ok = ok && fabsf(sa - along) < 1e-4f && fabsf(sp - 1.f / along) < 1e-4f;
            }
            Check(ok, "squash & stretch: at its peak both halves lean in and thin across");
        }
        if (t == 0.f)
        {
            // the daughter is born as an exact copy ON the parent: the frame is the parent, alone
            int diff = 0;
            for (int i = 0; i < w * h; ++i)
                if (((out[i] >> 24) > 128) != (inkP[i] > 128)) ++diff;
            Check(diff <= 2, "t = 0: the new tag is invisible, sitting exactly on its parent");
        }
        if (fabsf(t - mig::kSnap) < 1e-4f)
            Check(apart && neck < 64, "at kSnap the strands have let go");
        if (fabsf(t - 0.3f) < 1e-4f)
            Check(apart && neck > 200, "while they pull apart the two halves are joined by the bridge");
        if (t == 1.f)
        {
            int diff = 0;
            for (int i = 0; i < w * h; ++i)
                if (((out[i] >> 24) > 128) != (inkP[i] > 128 || inkC[i] > 128)) ++diff;
            Check(diff <= 2, "t = 1: exactly the two labels, unscaled, nothing between them");
            // no alpha on the letters
            int soft = 0, solid = 0;
            for (int i = 0; i < w * h; ++i)
                if (inkP[i] == 255) { ++solid; if ((out[i] >> 24) < 250) ++soft; }
            Check(soft * 20 <= solid, "the letters stay solid (no alpha)");
        }
        // no blob: never more ink than the parent plus the bigger of the daughter's two letter
        // sets. 10% slack: halfway through the letter morph a distance lerp of "ps" and "tb" is a
        // few % fatter than either end (it measured 104%) -- a blob would be far past that
        // + kStretch: the squash & stretch keeps area on paper (sa * sp = 1), but the stretch on
        // 14px strokes rounds UP a few % (measured 113%)
        Check(lit <= letters * (1.1f + miw::kStretch) + band + 4,
              "no frame has more ink than its letters plus the gap they span (no blob)");

        char path[128];
        snprintf(path, sizeof(path), "testout/mitosis%s_%02d.ppm", tag, frame++);
        if (FILE* fo = fopen(path, "wb"))
        {
            fprintf(fo, "P6\n%d %d\n255\n", w, h);
            for (int i = 0; i < w * h; ++i)
            {
                const unsigned c = out[i];
                const float al = (c >> 24) / 255.f;
                const unsigned char px[3] = {
                    (unsigned char)(((c >> 16) & 0xFF) * al + 18 * (1.f - al)),
                    (unsigned char)(((c >>  8) & 0xFF) * al + 18 * (1.f - al)),
                    (unsigned char)(( c        & 0xFF) * al + 18 * (1.f - al)) };
                fwrite(px, 1, 3, fo);
            }
            fclose(fo);
        }
    }
}

int main()
{
    // --- distance transform against brute force ------------------------------------------------
    {
        Cov c(W * H, 0);
        Rect(c, 10, 12, 14, 20);
        c[5 * W + 40] = 255;
        std::vector<float> d(W * H);
        mig::DistTransform(c.data(), W, H, d.data());
        float worst = 0.f;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
            {
                float best = mig::kFar;
                for (int sy = 0; sy < H; ++sy)
                    for (int sx = 0; sx < W; ++sx)
                        if (c[sy * W + sx] >= 128)
                        {
                            const float e = sqrtf((float)((sx - x) * (sx - x) + (sy - y) * (sy - y)));
                            if (e < best) best = e;
                        }
                const float got = d[y * W + x];
                Check(got >= best - 1e-3f, "chamfer never UNDER-estimates the true distance");
                if (best > 0.f && (got - best) / best > worst) worst = (got - best) / best;
            }
        Check(worst < 0.09f, "chamfer stays within 9% of the exact distance");
        printf("      chamfer worst-case error %.1f%%\n", worst * 100.f);
    }

    // --- signed distance: negative inside, zero line on the pixel boundary ----------------------
    {
        Cov c(W * H, 0);
        Rect(c, 10, 10, 20, 20);
        std::vector<float> o(W * H), in(W * H), so(W * H), si(W * H), s(W * H), sl(W * H);
        mig::SignedDist(c.data(), W, H, o.data(), in.data(), so.data(), si.data(), s.data(), sl.data());
        Check(s[15 * W + 15] < -4.f,              "deep inside is strongly negative");
        Near(s[15 * W + 10], -0.5f, 1e-4f,       "edge pixel inside: -0.5, the boundary is its outer side");
        Near(s[15 * W + 9],   0.5f, 1e-4f,       "first pixel outside: +0.5");
        Near(s[15 * W + 5],   4.5f, 1e-4f,       "5 px out: 4.5");
        Near(sl[15 * W + 5],  1.f, 1e-4f,        "a hard edge's slope: all of the coverage in one px");

        // a partly covered edge column keeps the edge's SUB-pixel place: coverage runs linearly
        // centre to centre (the atlas is bilinear-upsampled), so 1 at x 19.5 and 0.302 at 20.5
        // put 50% at 19.5 + 0.5 / 0.698. Thresholded to a hard shape, this edge sat on the pixel
        // boundary whatever the coverage, so a gliding tag's letters walked in whole (ss) px steps
        const float c20 = 77.f / 255.f;
        for (int y = 10; y < 20; ++y) c[y * W + 20] = 77;
        mig::SignedDist(c.data(), W, H, o.data(), in.data(), so.data(), si.data(), s.data(), sl.data());
        const float e = 19.5f + 0.5f / (1.f - c20);         // the edge, x
        Near(s[15 * W + 20], 20.5f - e, 1e-4f, "30% edge pixel: its centre just past the edge");
        Near(s[15 * W + 21], 21.5f - e, 1e-4f, "next pixel out: one more");
        Near(s[15 * W + 19], 19.5f - e, 1e-4f, "last solid pixel: inside by the rest");
        // coverage back from distance * slope: what Compose draws
        for (int x = 19; x <= 21; ++x)
            Near(mig::Sat(0.5f - s[15 * W + x] * sl[15 * W + x]), c[15 * W + x] / 255.f, 1e-3f,
                 "0.5 - distance * slope is the pixel's own coverage");
    }

    // --- field sampling --------------------------------------------------------------------------
    {
        Cov c(W * H, 0);
        Rect(c, 10, 10, 20, 20);
        Sdf s(c);
        Near(mig::Sample(s.f, 5.5f, 15.5f), s.d[15 * W + 5], 1e-5f, "a pixel centre samples exactly");
        Near(mig::Sample(s.f, 6.f, 15.5f), (s.d[15 * W + 5] + s.d[15 * W + 6]) * 0.5f, 1e-5f,
             "between two centres is their average");
        Near(mig::Sample(s.f, -3.f, 15.5f), mig::kOut, 0.f, "outside the rect is kOut");
    }

    // --- settled tags: Compose draws exactly their shapes --------------------------------------
    {
        Cov c(W * H, 0);
        Rect(c, 10, 10, 20, 20);
        Sdf s(c);
        mig::Comp k; k.self = s.f;
        const auto out = Run(&k, 1);
        int diff = 0;
        for (int i = 0; i < W * H; ++i) if (((out[i] >> 24) > 128) != (c[i] >= 128)) ++diff;
        Check(diff == 0, "a settled tag with no furrow is its own shape, pixel for pixel");
    }

    // --- the strand: stem weight, flares in, thins, snaps, pulls back ---------------------------
    {
        Cov c(W * H, 0);
        Rect(c, 8, 10, 20, 22);                    // two "letters", a 10 px gap (x 20..30), midline 16
        Rect(c, 30, 10, 42, 22);
        Sdf s(c);
        mig::Comp k; k.self = s.f;
        const int plain = Area(Run(&k, 1));
        mig::Bridge br;
        br.eP = 19.f; br.eC = 31.f; br.side = 1.f; br.cv = 16.5f; br.hs = 1.5f; br.r = 1.f;

        const auto rowsAt = [&](const std::vector<unsigned>& o, int x)
        { int n = 0; for (int y = 0; y < H; ++y) n += A(o, x, y) > 128; return n; };

        int prev = 1 << 30;
        for (int i = 0; i <= 4; ++i)
        {
            br.depth = i / 4.f;
            const auto out = Run(&k, 1, &br, 1);
            int stray = 0;
            for (int y = 0; y < H; ++y) for (int x = 21; x < 29; ++x)
                if (A(out, x, y) > 128 && fabsf(y + 0.5f - 16.5f) > br.hs + 0.5f) ++stray;
            Check(stray == 0, "nothing in the gap but the strand (no bar, no blob)");
            Check(Area(out) >= plain, "the strand never eats into the letters");
            Check(rowsAt(out, 25) <= prev, "the strand only thins as it pinches");
            prev = rowsAt(out, 25);
            if (i == 0) Check(rowsAt(out, 25) == 3, "unpinched: exactly the stem weight it was given");
            if (i == 4) Check(rowsAt(out, 25) == 0, "fully pinched: it has let go");
        }
        br.depth = 0.f; br.r = 5.f;   // Render's 0.18 fs is ~8 ss px at 24px; 5 is plenty to show
        {
            const auto out = Run(&k, 1, &br, 1);
            Check(rowsAt(out, 21) > rowsAt(out, 25), "the strand flares into the letters (liquid fillet)");
        }
        br.r = 1.f; br.depth = 1.f;
        int prevStub = 1 << 30;
        for (int i = 1; i <= 4; ++i)
        {
            br.cut = i / 4.f;
            const auto out = Run(&k, 1, &br, 1);
            int stub = 0;
            for (int y = 0; y < H; ++y) for (int x = 21; x < 29; ++x) stub += A(out, x, y) > 128;
            Check(rowsAt(out, 25) == 0, "after the snap it stays let go");
            Check(stub <= prevStub, "the halves only ever pull back");
            prevStub = stub;
            if (i == 4) Check(Area(out) == plain, "pulled back: just the letters");
        }
        mig::Bridge none = br; none.hs = 0.f; none.cut = 0.f;
        Check(Area(Run(&k, 1, &none, 1)) == plain, "still touching: no strand");
    }

    // --- the letter morph is a SHAPE interpolation, not a cross-fade ---------------------------
    {
        Cov g(W * H, 0), c(W * H, 0);
        Rect(g, 30, 14, 34, 18);                   // small square (what the daughter starts as)
        Rect(c, 26, 10, 38, 22);                   // big square (what it becomes), same centre
        Sdf sg(g), sc(c);
        int prev = -1;
        for (int i = 0; i <= 4; ++i)
        {
            mig::Comp k[2];
            k[1].parent = 0; k[1].self = sc.f; k[1].from = sg.f; k[1].m = i / 4.f;
            const auto out = Run(k, 2);   // k[0] = an empty parent, no field
            const int  a   = Area(out);
            Check(a >= prev, "the shape grows steadily from the small square to the big one");
            prev = a;
            Check(A(out, 32, 16) == 255, "the morphing shape is solid at its centre");
            if (i == 2)
            {
                Check(a > 16 + 10 && a < 144 - 10, "halfway is a middle-sized shape");
                Check(A(out, 27, 16) < 60, "halfway, the big square's edge is not there at half alpha");
            }
        }
    }

    // --- the pop: nothing at t = 0, then the letters scaled on their field -- SHARP at any size ----
    {
        Cov c(W * H, 0);
        Rect(c, 26, 10, 38, 22);                   // centre (32, 16)
        Sdf s(c);
        const auto pop = [&](float sc)
        {
            mig::Comp k; k.parent = -1; k.self = s.f;
            k.px = 32.f; k.py = 16.f; k.sx = k.sy = sc;
            return Run(&k, 1);
        };
        Check(Area(pop(mig::PopAt(0.f))) > 0 && Area(pop(mig::PopAt(0.f))) < Area(pop(1.f)),
              "a popping tag starts at half size, not from nothing (softer scale)");
        Check(Area(pop(0.5f)) < Area(pop(1.f)), "half way in it is smaller");
        int diff = 0;
        const auto end = pop(mig::PopAt(1.f));
        for (int i = 0; i < W * H; ++i) if (((end[i] >> 24) > 128) != (c[i] >= 128)) ++diff;
        Check(diff == 0, "t = 1: exactly its letters");
        // blown up 2.5x the edge is still a 1 px ramp: a bilinear stretch of the coverage (what the
        // old text-quad scale did) would smear it over 2-3 px
        const auto big = pop(2.5f);
        int soft = 0;
        for (int x = 0; x < W; ++x) { const unsigned a = A(big, x, 16); soft += a > 20 && a < 235; }
        Check(soft <= 2, "scaled up, the edges stay sharp (no blur)");
    }

    // --- layout slots glide: frame-rate independent, no overshoot, settled in ~0.25 s -------------
    {
        const auto run = [](float fps, float secs, float want)
        {
            float v = 0.f, peak = 0.f;
            for (int i = 0, n = (int)(fps * secs + 0.5f); i < n; ++i)
            { v = mig::Follow(v, want, 1.f / fps, mig::kSlotRate); peak = fmaxf(peak, v); }
            return std::make_pair(v, peak);
        };
        Near(run(60.f, 0.2f, 1.f).first, run(240.f, 0.2f, 1.f).first, 1e-3f,
             "same glide at 60 and 240 fps");
        Check(run(144.f, 3.f, 1.f).second <= 1.f, "never overshoots its slot");
        Check(run(144.f, 0.16f, 1.f).first > 0.94f, "settled (95%) within ~0.15 s");
        Check(run(144.f, 1.f / 144.f, 1.f).first < 0.15f,
              "one frame moves it under 15% of the way: a glide, not a jump");
        Check(run(144.f, 1.5f, 1.f).first == 1.f,
              "and it lands EXACTLY on the slot (a resting tag on whole pixels, crisp)");
        Near(mig::Follow(0.3f, 1.f, 0.f, mig::kSlotRate), 0.3f, 1e-6f, "dt 0 holds still");
        Near(mig::Follow(0.3f, 1.f, -1.f, mig::kSlotRate), 0.3f, 1e-6f, "a negative dt holds still");
    }

    // --- the bind fade: in linear, out lands soft (no snap shut at the end) ----------------------
    {
        const float sp = 4.f;   // movind.cpp k_showSpeed, mitosis
        // one fade-out at 144 fps: when (fraction of 1 / sp) the daughter's slide and the slot
        // (Follow on EaseOut, as movind drives it) move fastest, and how long it takes
        struct Out { float slidePk, slotPk, done; };
        const auto fadeOut = [&](bool linear)
        {
            const float dt = 1.f / 144.f;
            float s = 1.f, slot = 1.f, t = 0.f, vs = 0.f, vl = 0.f;
            Out o{ 0.f, 0.f, 0.f };
            while (s > 0.f)
            {
                const float s0 = s, l0 = slot;
                s    = linear ? mig::Sat(s - dt * sp) : mig::FadeStep(s, false, dt, sp);
                slot = mig::Follow(slot, mig::EaseOut(s), dt, mig::kSlotRate);
                t   += dt;
                const float a = fabsf(mig::SlideAt(s) - mig::SlideAt(s0)), b = fabsf(slot - l0);
                if (a > vs) { vs = a; o.slidePk = t * sp; }
                if (b > vl) { vl = b; o.slotPk  = t * sp; }
            }
            o.done = t * sp;
            return o;
        };
        const Out lin = fadeOut(true), soft = fadeOut(false);
        printf("      fade out: slide peaks at %.0f%%, slot at %.0f%%, done at %.0f%% (linear clock: %.0f%% / %.0f%%)\n",
               soft.slidePk * 100.f, soft.slotPk * 100.f, soft.done * 100.f, lin.slidePk * 100.f, lin.slotPk * 100.f);
        Check(lin.slidePk > 0.9f && lin.slotPk > 0.9f,
              "control: on a linear clock the merge DOES slam shut in its last 10% (the metric sees it)");
        Check(soft.slidePk < 0.8f, "fading out, the daughter is fastest mid-merge, not slamming in at the end");
        Check(soft.slotPk < 0.8f, "fading out, the row closes mid-fade, not in the last frames");
        Check(soft.done > 0.97f && soft.done < 1.03f, "fading out takes the same 1 / speed as fading in");

        float a = 0.f, b = 0.f, c = 0.f;
        for (int i = 0; i < 6;  ++i) a = mig::FadeStep(a, true, 1.f / 60.f, sp);
        for (int i = 0; i < 24; ++i) b = mig::FadeStep(b, true, 1.f / 240.f, sp);
        Near(a, b, 1e-5f, "fade in: same at 60 and 240 fps");
        Near(a, 0.4f, 1e-5f, "fade in is linear");
        a = b = 1.f;
        for (int i = 0; i < 6;  ++i) a = mig::FadeStep(a, false, 1.f / 60.f, sp);
        for (int i = 0; i < 24; ++i) b = mig::FadeStep(b, false, 1.f / 240.f, sp);
        Near(a, b, 1e-5f, "fade out: same at 60 and 240 fps (exact step)");
        Near(a, 0.36f, 1e-5f, "fade out runs on (1 - tau)^2");
        for (int i = 0; i < 200; ++i) c = mig::FadeStep(i ? c : 1.f, false, 1.f / 144.f, sp);
        Check(c == 0.f, "fade out lands on exactly 0");
        Near(mig::FadeStep(0.5f, false, 0.f, sp), 0.5f, 0.f, "dt 0 holds");
        Near(mig::FadeStep(0.5f, true, -1.f, sp), 0.5f, 0.f, "a negative dt holds");
        Check(fabsf(mig::FadeStep(0.5f, false, 1.f / 144.f, sp) - 0.5f) < 2.f * sp / 144.f,
              "turning round mid-fade does not jump");
    }

    // --- who divides off whom ------------------------------------------------------------------
    {
        const float lone[1] = { 0.3f };
        Check(mig::PickParent(lone, 1, 0) == -1, "a lone tag has nothing to divide off: it pops");
        const float r1[2] = { 1.f, 0.3f };
        Check(mig::PickParent(r1, 2, 1) == 0, "divides off the settled neighbour before it");
        const float r2[2] = { 0.3f, 1.f };
        Check(mig::PickParent(r2, 2, 0) == 1, "divides off the settled neighbour after it");
        const float r3[2] = { 0.3f, 0.3f };
        Check(mig::PickParent(r3, 2, 0) == -1 && mig::PickParent(r3, 2, 1) == -1,
              "two born side by side: both pop, neither divides off the other");
        const float r4[3] = { 0.6f, 0.3f, 1.f };
        Check(mig::PickParent(r4, 3, 1) == 2, "the OLDER neighbour is the parent");
        const float r5[3] = { 0.3f, 1.f, 0.3f };
        Check(mig::PickParent(r5, 3, 0) == 1 && mig::PickParent(r5, 3, 2) == 1,
              "two daughters at once, one either side, both divide off the middle tag");
    }

    // --- timeline ------------------------------------------------------------------------------
    {
        Near(mig::SlideAt(0.f), 0.f, 1e-6f, "slide 0 at the start: on the parent");
        Near(mig::SlideAt(mig::kSnap), mig::kPull, 1e-5f, "pulled furthest out at the snap");
        Near(mig::SlideAt(1.f), 1.f, 1e-5f, "slide 1 at the end: in its slot");
        Near(mig::DepthAt(0.f), 0.f, 1e-6f, "no pinch at the start");
        Near(mig::DepthAt(mig::kSnap), 1.f, 1e-6f, "the neck is through at kSnap");
        Near(mig::CutAt(mig::kSnap), 0.f, 1e-6f, "the stubs start pulling back only after the snap");
        Near(mig::CutAt(1.f), 1.f, 1e-6f, "stubs gone by the end");
        Near(mig::MorphAt(0.f), 0.f, 1e-6f, "frame one is still an exact copy of the parent");
        Check(mig::MorphAt(0.1f) > 0.05f, "the letters start turning right after it appears");
        Near(mig::MorphAt(mig::kSnap), 1.f, 1e-6f, "done morphing by the snap");
        Near(mig::MorphAt(1.f), 1.f, 1e-6f, "its own letters at the end");
        Near(mig::EaseOut(0.f), 0.f, 1e-6f, "the pop starts at nothing");
        Near(mig::EaseOut(1.f), 1.f, 1e-6f, "and ends at full size");
        float peak = 0.f;
        for (int i = 0; i <= 100; ++i) peak = fmaxf(peak, mig::EaseOut(i / 100.f));
        Check(peak > 1.01f && peak < 1.05f, "soft overshoot: a few %, not the textbook 10%");
        Near(mig::PopAt(0.f), 0.5f, 1e-6f, "the pop starts at half size");
        Near(mig::PopAt(1.f), 1.f, 1e-6f, "and lands at full size");
        float pp = 0.f, pmax = 0.f;
        for (int i = 0; i <= 100; ++i)
        {
            const float v = mig::PopAt(i / 100.f);
            Check(v >= pp - 1e-6f, "the pop only ever grows");
            pp = v; pmax = fmaxf(pmax, v);
        }
        Check(pmax <= 1.f, "the pop never overshoots");
        float pm = -1.f, pd = -1.f, pc = -1.f, prevS = -1.f;
        for (int i = 0; i <= 100; ++i)
        {
            const float t = i / 100.f;
            Check(mig::MorphAt(t) >= pm - 1e-6f, "the morph never runs backwards within a split");
            Check(mig::DepthAt(t) >= pd - 1e-6f, "the neck only pinches");
            Check(mig::CutAt(t) >= pc - 1e-6f, "the stubs only pull back");
            if (t <= mig::kSnap) Check(mig::SlideAt(t) >= prevS - 1e-6f, "pulls out steadily until the snap");
            else                 Check(mig::SlideAt(t) <= prevS + 1e-6f, "then springs back steadily");
            pm = mig::MorphAt(t); pd = mig::DepthAt(t); pc = mig::CutAt(t); prevS = mig::SlideAt(t);
        }
    }

    RealFontSplit(24.f, true,  "ps", "tb");         // g_miScale 2, the user's row
    RealFontSplit(14.f, true,  "ps", "tb");         // g_miScale ~1
    RealFontSplit(24.f, false, "as", "ej");         // the default vertical stack
    // the user's row "tb air ...": the 'a' sits well inside its advance box at the strand's line
    RealFontSplit(24.f, true,  "tb", "air", true);
    RealFontSplit(14.f, true,  "tb", "air", true);
    RealFontSplit(14.f, true,  "ast", "eb", true);  // round 'e' meets the 't' stem under its arm

    SubPixelGlide(14.f);                            // ss 3
    SubPixelGlide(24.f);                            // ss 2

    printf(g_fail ? "\n%d FAILED\n" : "\nall mitosis checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
