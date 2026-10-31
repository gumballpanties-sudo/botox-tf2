#include "movind.h"
#include "../movement/movement.h"
#include "migoo.h"
#include "miwarp.h"
#include "../../hooks/crashlog.h"
#include "keyind_fx.inl"
#include <vector>

// menu rows / on-screen tags, indexed by MiSlot -- one table, so a mask bit, a dropdown row and
// a drawn tag can never drift apart
const char* const kMiNames[MI_COUNT] = { "texturebug", "airstuck", "wallclimb", "pixelsurf",
                                         "ps assist", "edgebug", "jumpbug",
                                         "autostrafe", "edgejump", "longjump", "minijump" };
const char* const kMiTags[MI_COUNT]  = { "tb", "air", "wc", "ps", "ast", "eb",
                                         "jb", "as", "ej", "lj", "mj" };

// Velocity indicator, coldtires `indicators.cpp:679 velocity` 1:1:
//   * round(2D speed), centred, top edge g_viOffsetY px up from the bottom
//   * colour: same / down / up vs the speed sampled 5+ ticks ago (color3 / 4 / 5), or with
//     custom colour a color1->color2 blend by speed/250
//   * fade alpha: speed/250. Their `>= 250 ? 250 : v/250` saturates to the same thing
//   * pre speed: " (takeoff)" after the number while airborne and 2s after landing, fading
//     9/s (ImAnimationHelper Update(3, +-3)), its slot width scaling so the pair stays centred
// Reads the movement snapshot (speed, ground, tick, curtime), same source the tags use.
void RenderVelocityIndicator()
{
    if (!g_viEnable) return;

    MvInd_t s{};
    MV_GetIndicators(s);
    if (s.ms == 0 || GetTickCount64() - s.ms > 250) return;

    static int   s_last = 0, s_prevTick = 0, s_takeOff = 0, s_wjSeq = 0;
    static float s_takeOffTime = 0.f, s_preAnim = 0.f;
    static bool  s_lastGround = false;

    // map change / reconnect rewinds tickcount + curtime: without this the 5-tick sample
    // freezes and a stale takeoff timer keeps the pre speed up until the clock catches up
    if (s.tick < s_prevTick) s_prevTick = 0;
    if (s_takeOffTime > s.curtime + 2.f) s_takeOffTime = 0.f;

    const int vel = (int)roundf(s.speed);
    if (s_lastGround && !s.onGround)
    {
        s_takeOff     = vel;
        s_takeOffTime = s.curtime + 2.f;
    }
    // fake wall jump = a jump too: its post-jump speed becomes the pre speed
    if (s.wjSeq != s_wjSeq)
    {
        s_wjSeq       = s.wjSeq;
        s_takeOff     = (int)roundf(s.wjSpeed);
        s_takeOffTime = s.curtime + 2.f;
    }

    const float alpha = s.speed >= 250.f ? 1.f : s.speed / 250.f;
    const float fadeA = g_viFadeAlpha ? alpha : 1.f;

    float col[4];
    if (g_viCustomColor)
    {
        for (int i = 0; i < 4; ++i) col[i] = g_viColor1[i] + alpha * (g_viColor2[i] - g_viColor1[i]);
        // coldtires: `w * fade ? alpha : 1.f` -- precedence makes it (w * fade) ? alpha : 1,
        // so the blended alpha itself is dropped. Kept as shipped
        col[3] = (col[3] * (g_viFadeAlpha ? 1.f : 0.f)) != 0.f ? alpha : 1.f;
    }
    else
    {
        const float* c = vel == s_last ? g_viColor3 : (vel < s_last ? g_viColor4 : g_viColor5);
        for (int i = 0; i < 4; ++i) col[i] = c[i];
        col[3] *= fadeA;
    }

    const bool drawTakeOff = (!s.onGround || s_takeOffTime > s.curtime) && g_viPreSpeed;
    const float dt = ImGui::GetIO().DeltaTime > 0.f ? ImGui::GetIO().DeltaTime : (1.f / 60.f);
    s_preAnim += 9.f * dt * (drawTakeOff ? 1.f : -1.f);
    s_preAnim  = s_preAnim < 0.f ? 0.f : (s_preAnim > 1.f ? 1.f : s_preAnim);

    ImFont* font = g_fontMi ? g_fontMi : (g_fontBold ? g_fontBold : ImGui::GetFont());
    const float fs = font->FontSize;

    char mainText[16], preText[24];
    snprintf(mainText, sizeof(mainText), "%d", vel);
    snprintf(preText,  sizeof(preText),  " (%d)", s_takeOff);
    const ImVec2 mainSz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, mainText);
    const ImVec2 preSz  = font->CalcTextSizeA(fs, FLT_MAX, 0.f, preText);

    const ImGuiIO& io = ImGui::GetIO();
    const float y      = io.DisplaySize.y - g_viOffsetY;
    const float totalX = mainSz.x + preSz.x * s_preAnim;
    const float startX = (io.DisplaySize.x - totalX) * 0.5f;

    const auto sat = [](float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); };
    const auto u32 = [&](float r, float g, float b, float a)
    { return IM_COL32((int)(sat(r) * 255.f), (int)(sat(g) * 255.f), (int)(sat(b) * 255.f), (int)(sat(a) * 255.f)); };

    // text_flag_dropshadow = black copy at +1,+1
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddText(font, fs, ImVec2(startX + 1.f, y + 1.f), u32(0, 0, 0, fadeA), mainText);
    dl->AddText(font, fs, ImVec2(startX, y), u32(col[0], col[1], col[2], col[3]), mainText);
    if (s_preAnim > 0.f)
    {
        const float px = startX + mainSz.x;
        dl->AddText(font, fs, ImVec2(px + 1.f, y + 1.f), u32(0, 0, 0, fadeA * s_preAnim), preText);
        dl->AddText(font, fs, ImVec2(px, y), u32(col[0], col[1], col[2], col[3] * s_preAnim), preText);
    }

    if (s_prevTick + 5 < s.tick)
    {
        s_last     = vel;
        s_prevTick = s.tick;
    }
    s_lastGround = s.onGround;
}

// ---------------------------------------------------------------------------------------------
// The mitosis morph. The two splitting tags are taken OUT of the text pass and drawn as one
// morphing shape (migoo.h): the child is born as a copy of the parent's letters on top of it,
// slides out and turns into its own letters. Nothing is drawn behind or between the labels.
//
// The coverage comes from imgui's own font atlas (the CPU copy it keeps after the upload), so
// the shapes are exactly the glyphs AddText would have drawn, same rasterizer, same hinting.
// The box is rasterized on the CPU (supersampled, a few tens of thousands of samples), uploaded
// to one small texture and drawn as a single quad on an integer pixel rect, landing 1:1.
static IDirect3DTexture9* s_gooTex = nullptr;
static IDirect3DDevice9*  s_gooDev = nullptr;
static int  s_gooW = 0, s_gooH = 0;
static bool s_gooDyn = false;

void MoveInd_ReleaseTextures()
{
    if (s_gooTex) { s_gooTex->Release(); s_gooTex = nullptr; }
    s_gooDev = nullptr;
    s_gooW = s_gooH = 0;
}

// MANAGED first so a device Reset cannot lose it, DEFAULT+DYNAMIC for wrappers that refuse
// MANAGED -- the same pair crosshair.cpp's sprite atlas uses, and h_Reset drops both.
static HRESULT s_gooHr = S_OK;   // the last CreateTexture result, for the fallback note
static bool GooEnsureTex(int w, int h)
{
    if (!g_d3dDev) return false;
    if (s_gooDev != g_d3dDev) MoveInd_ReleaseTextures();
    int dim = 32;
    while (dim < w || dim < h) dim <<= 1;
    if (dim > 1024) return false;
    if (s_gooTex && s_gooW >= dim) return true;
    MoveInd_ReleaseTextures();

    s_gooDyn = false;
    HRESULT hr = g_d3dDev->CreateTexture(dim, dim, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                         &s_gooTex, nullptr);
    if (FAILED(hr) || !s_gooTex)
    {
        s_gooTex = nullptr;
        s_gooDyn = true;
        hr = g_d3dDev->CreateTexture(dim, dim, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
                                     D3DPOOL_DEFAULT, &s_gooTex, nullptr);
    }
    s_gooHr = hr;
    if (FAILED(hr) || !s_gooTex) { s_gooTex = nullptr; return false; }
    s_gooDev = g_d3dDev;
    s_gooW = s_gooH = dim;
    return true;
}

// Draws the splitting tags as one mitosis picture (migoo.h Render: slide, squash & stretch,
// bridge, snap, morph). false if anything is missing -- the caller then draws them as warped
// text, so a failure costs the morph, never the indicator. Every distinct failure, and the first
// success, gets ONE line in C:otox-tf2\crash.log (always on), so a session says which one it was.
static bool DrawMitosis(ImDrawList* dl, ImFont* font, float fs, const mig::Tag* tags, int n, bool axisX)
{
    static mig::Work s_wk;
    static unsigned  s_noted = 0;   // bit per line already written
    const auto first = [&](unsigned bit) { const bool f = !(s_noted & bit); s_noted |= bit; return f; };

    ImFontAtlas* atlas = font ? font->ContainerAtlas : nullptr;
    if (!atlas || !atlas->TexPixelsAlpha8)
    {
        if (first(1)) CrashLog_Note("mitosis: text fallback -- no atlas CPU pixels (atlas %p)", (void*)atlas);
        return false;
    }
    int ox = 0, oy = 0, w = 0, h = 0;
    if (!mig::Render(tags, n, font, fs, axisX, s_wk, ox, oy, w, h))
    {
        if (first(2)) CrashLog_Note("mitosis: text fallback -- Render refused (%d tags, box %dx%d, fs %.1f)", n, w, h, fs);
        return false;
    }
    if (!GooEnsureTex(w, h))
    {
        if (first(4)) CrashLog_Note("mitosis: text fallback -- texture %dx%d (dev %p, hr 0x%08lX)", w, h,
                                    (void*)g_d3dDev, (unsigned long)s_gooHr);
        return false;
    }
    // WHOLE level, never a sub-rect: TF2's device refuses MANAGED, so this is the DYNAMIC
    // texture, and DISCARD with a rect is D3DERR_INVALIDCALL (0x8876086C) -- which is what every
    // in-game split hit before this, silently falling back to text. Same call crosshair.cpp makes.
    D3DLOCKED_RECT lr{};
    const HRESULT  hr = s_gooTex->LockRect(0, &lr, nullptr, s_gooDyn ? D3DLOCK_DISCARD : 0);
    if (FAILED(hr))
    {
        if (first(8)) CrashLog_Note("mitosis: text fallback -- LockRect hr 0x%08lX (dynamic %d)",
                                    (unsigned long)hr, (int)s_gooDyn);
        return false;
    }
    // DISCARD leaves the rest undefined: clear the one texel column and row just past the box, the
    // only ones its edge can ever reach
    for (int y = 0; y < h; ++y)
    {
        unsigned int* row = (unsigned int*)((unsigned char*)lr.pBits + (size_t)lr.Pitch * y);
        memcpy(row, s_wk.px.data() + (size_t)w * y, (size_t)w * 4);
        if (w < s_gooW) row[w] = 0;
    }
    if (h < s_gooH) memset((unsigned char*)lr.pBits + (size_t)lr.Pitch * h, 0, (size_t)(w < s_gooW ? w + 1 : w) * 4);
    s_gooTex->UnlockRect(0);
    if (first(16)) CrashLog_Note("mitosis: drawing (%d tags, box %dx%d at %d,%d, tex %dx%d %s)", n, w, h, ox, oy,
                                 s_gooW, s_gooH, s_gooDyn ? "dynamic" : "managed");

    const ImTextureID id = (ImTextureID)s_gooTex;
    const ImVec2 p0((float)ox, (float)oy), p1((float)(ox + w), (float)(oy + h));
    const ImVec2 uv1((float)w / s_gooW, (float)h / s_gooH);
    // shadow first, the same 1px offset copy the text pass draws, tinted black through the shape
    dl->AddImage(id, ImVec2(p0.x + 1.f, p0.y + 1.f), ImVec2(p1.x + 1.f, p1.y + 1.f),
                 ImVec2(0.f, 0.f), uv1, IM_COL32(0, 0, 0, 255));
    dl->AddImage(id, p0, p1, ImVec2(0.f, 0.f), uv1, IM_COL32_WHITE);
    return true;
}

// Per-axis scale of the glyph quads AddText just appended, about `piv`. imgui text has one
// uniform size argument and no transform, so the warp is done on the vertices after the fact.
// Positions only: atlas UVs and colours stay exactly what AddText wrote.
static void ScaleVerts(ImDrawList* dl, int vtx0, ImVec2 piv, float sx, float sy)
{
    ImDrawVert* v   = dl->VtxBuffer.Data + vtx0;   // read Data AFTER AddText: it may have grown
    ImDrawVert* end = dl->VtxBuffer.Data + dl->VtxBuffer.Size;
    for (; v < end; ++v)
    {
        v->pos.x = piv.x + (v->pos.x - piv.x) * sx;
        v->pos.y = piv.y + (v->pos.y - piv.y) * sy;
    }
}

// Keybind indicators, coldtires `indicators.cpp:447 keybind_indicators` shape:
//   * short lowercase tags (tb / air / wc / ps / eb ...), never a word
//   * bottom centre, stacked upward, so nothing sits over the crosshair
//   * one fade per bind (in while held, out when released) -- a released bind slides the
//     stack closed instead of popping a hole in it
//   * the tag stays the base colour and crossfades to the success colour on the tick the
//     trick actually LANDS, then back. Two colours total, like the reference
// A whole tick's state comes from one struct the movement thread publishes -- no locks, and a
// torn read is one stale digit in an overlay.
void RenderMoveIndicators()
{
    if (!g_miEnable) return;

    MvInd_t s{};
    MV_GetIndicators(s);
    // no live cmd in the last ~250 ms = dead / not in game / movement idle
    if (s.ms == 0 || GetTickCount64() - s.ms > 250) return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    // g_fontMi is the bold face rasterized AT 12 * g_miScale px (render.cpp BuildFontAtlas
    // rebuilds it the frame the slider moves), so drawing at its own FontSize is a 1:1 blit.
    // Scaling one 12px face up was the blur.
    ImFont* font = g_fontMi ? g_fontMi : (g_fontBold ? g_fontBold : ImGui::GetFont());
    const float fs = font->FontSize;
    const float dt = io.DeltaTime > 0.f ? io.DeltaTime : (1.f / 60.f);

    // PERFORMED = the success colour. The render thread samples at frame rate while a hit flag
    // lives for a single 15.6 ms cmd, so reading it live would drop most of them -- an edge bug
    // is ONE tick and would never be visible at all. Latch the tick each feature last fired,
    // then run the colour crossfade off the latch: a 1-tick event reads as a clean flash, a held
    // ride reads as solid. Slots are indexed by feature, so a row that fades out can never hand
    // its flash to the next one.
    static constexpr unsigned long long k_flashMs = 180;   // hold long enough that a 1-tick hit
                                                           // survives a few frames, no longer
    // 0.1 s end to end is right for a plain fade, but it is 6 frames for a whole cell division.
    // Mitosis slows the same fade to 0.25 s (0.55, 0.4 and 0.31 all read as slow in game): pull out, neck, snap, recoil and the letter morph
    // (migoo.h timeline) each still get a few frames
    const float k_showSpeed = g_miMitosis ? 4.f : 10.f;   // bind fade in/out
    static constexpr float k_hotSpeed  = 26.f;   // base->success crossfade, ~0.04 s
    static unsigned long long s_fire[MI_COUNT] = {};
    static float s_show[MI_COUNT] = {};   // 0..1 bind fade
    static float s_hot[MI_COUNT]  = {};   // 0..1 base->hit
    // 0..1 how open the tag's SLOT is in the row / stack. Follows the fade (mig::Follow) instead of
    // being it, so a bind coming or going makes the others glide over ~0.25 s at any fade speed --
    // tied to the fade (0.1 s plain) they jumped into place
    static float s_slot[MI_COUNT] = {};
    const unsigned long long now = GetTickCount64();

    const auto sat = [](float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); };

    // slots open with mig::EaseOut: a ~3% overshoot and settle, the row making room softly
    const auto ease = [](float x) { return mig::EaseOut(x); };

    // anim drives the slot (eased in mitosis); raw is the un-eased fade the mitosis timeline runs
    // on; rgb is col as 0..1 for the mitosis raster; pos is filled by the layout; piv/sx/sy are
    // the squash & stretch the text pass applies
    struct Ent { char text[56]; ImU32 col; ImU32 shadow; float anim; float raw; ImVec2 size;
                 ImVec2 pos; float rgb[3]; ImVec2 piv; float sx; float sy; bool justHit; };
    Ent ents[MI_COUNT]{};
    int n = 0;

    // idle -> active is a straight per-channel lerp on the hot fade, alpha carries the bind
    // fade on top. `dim` is the only extra: a feature that is armed but not driving the cmd
    // sits slightly back, which is what the old bright/dim pair said with two colours
    const auto emit = [&](int slot, bool on, bool hit, float dim, const char* text)
    {
        // dropped from the picker: kill the fades too, so re-ticking the row fades in fresh
        // instead of popping back at whatever alpha it was hidden at
        if (!(g_miShowMask & (1 << slot))) { s_show[slot] = s_hot[slot] = s_slot[slot] = 0.f; return; }

        // one burst per hit, not one per frame the hit flag stays up
        static bool s_prevHit[MI_COUNT] = {};
        const bool rise = hit && !s_prevHit[slot];
        s_prevHit[slot] = hit;

        if (hit) s_fire[slot] = now;
        const bool flash = s_fire[slot] != 0 && now - s_fire[slot] < k_flashMs;

        // mitosis fades OUT on a decelerating clock (mig::FadeStep): the split rewound on a
        // linear fade slammed shut in its last frames
        s_show[slot] = g_miMitosis ? mig::FadeStep(s_show[slot], on, dt, k_showSpeed)
                                   : sat(s_show[slot] + dt * k_showSpeed * (on ? 1.f : -1.f));
        s_hot[slot]  = sat(s_hot[slot]  + dt * k_hotSpeed  * (flash ? 1.f : -1.f));

        s_slot[slot] = mig::Follow(s_slot[slot], g_miMitosis ? ease(s_show[slot]) : s_show[slot],
                                   dt, mig::kSlotRate);
        // a released tag keeps its place until its slot has closed, so the others slide shut
        // instead of jumping the frame it is dropped
        if (s_show[slot] <= 0.001f && s_slot[slot] <= 0.002f) { s_slot[slot] = 0.f; return; }
        if (n >= MI_COUNT) return;

        // mitosis: NO alpha. The tag is solid from its first frame to its last -- it arrives as a
        // copy of its neighbour sitting exactly on it and leaves by merging back in, so a fade
        // would only show as a ghost of the morph
        // plain mode: never more opaque than the slot is open, or a new tag sits solid on top of
        // neighbours that are still gliding out of its way. Going out, the fade leads and the
        // slot closes after it
        const float fade = g_miMitosis ? 1.f : fminf(s_show[slot], s_slot[slot]);
        const float t = s_hot[slot];
        float r = g_miIdleColor[0] + (g_miActiveColor[0] - g_miIdleColor[0]) * t;
        float g = g_miIdleColor[1] + (g_miActiveColor[1] - g_miIdleColor[1]) * t;
        float b = g_miIdleColor[2] + (g_miActiveColor[2] - g_miIdleColor[2]) * t;
        const float a = (g_miIdleColor[3] + (g_miActiveColor[3] - g_miIdleColor[3]) * t)
                        * fade * dim;
        KeyIndFx_Color(slot, r, g, b);   // decorative random-colour override, unit B

        Ent& e = ents[n];
        snprintf(e.text, sizeof(e.text), "%s", KeyIndFx_Label(slot, text));
        e.col    = IM_COL32((int)(r * 255.f), (int)(g * 255.f), (int)(b * 255.f), (int)(a * 255.f));
        e.shadow = IM_COL32(0, 0, 0, (int)(a * 255.f));   // shadow keeps the tag's own alpha
        e.raw    = s_show[slot];
        e.anim   = s_slot[slot];
        e.size   = font->CalcTextSizeA(fs, FLT_MAX, 0.f, e.text);
        e.rgb[0] = r; e.rgb[1] = g; e.rgb[2] = b;
        e.sx = e.sy = 1.f;   // no warp unless the mitosis pass below asks for one
        e.justHit = rise;    // decorative particle burst, drawn after layout below
        ++n;
    };

    // texture bug first. tbHit = a pin landed this tick (the thing "performed"); tbOwn = TB is
    // merely driving the cmd, which is 94% of ticks on an approach and is not a texture bug, so
    // it never takes the success colour.
    // tb was the ONE tag drawn at 0.8 alpha while armed-but-not-driving, and armed-not-
    // driving is its normal state -- so it just read as "tb is permanently faded" next to every
    // other tag at full. The base/success crossfade already says what it is doing; drop the dim
    emit(MI_TB,  s.tbOn,     s.tbHit, 1.f, kMiTags[MI_TB]);

    emit(MI_AIR, s.asOn,     s.asHit, 1.f, kMiTags[MI_AIR]);   // coldtires calls air stuck "air"
    emit(MI_WC,  s.wcOn,     s.wcHit, 1.f, kMiTags[MI_WC]);
    // ps: hit = a surf is ACTUALLY under way (ps_surf.h IsSurf) -- a sustained -halfgrav pin
    // against a brush face whose top is at our feet. A fall beside a wall, an air stuck, a
    // texture bug and a hull wedged on a crate all pin vz the same way and all used to light it
    emit(MI_PS,  s.psOn,     s.psHit, 1.f, kMiTags[MI_PS]);
    // ps assist, delusional "ast": hit = the assist owns the arc (their HITGODA -- set while the
    // validated jump is pending or in flight, cleared when it ends), not a landing
    emit(MI_AST, s.astOn,    s.astHit, 1.f, kMiTags[MI_AST]);
    emit(MI_EB,  s.ebOn,     s.ebHit, 1.f, kMiTags[MI_EB]);
    emit(MI_JB,  s.jbOn,     s.jbHit, 1.f, kMiTags[MI_JB]);
    // always-on helpers: no landing state of their own, so base colour only
    emit(MI_ST,  s.strafeOn, false,   1.f, kMiTags[MI_ST]);    // auto strafe, coldtires "as"
    emit(MI_EJ,  s.ejOn,     false,   1.f, kMiTags[MI_EJ]);
    emit(MI_LJ,  s.ljOn,     false,   1.f, kMiTags[MI_LJ]);
    // mj is the exception: hit = the takeoff duck actually went out. A hop the engine refused
    // (FL_DUCKING) or whose duck the 0.3s duck timer stripped leaves the tag on its base colour,
    // so a chain that stops flashing IS the "that one was not a mini jump" readout
    emit(MI_MJ,  s.mjOn,     s.mjHit, 1.f, kMiTags[MI_MJ]);

    if (n <= 0) return;

    // shadow is a 1px offset copy, not an outline: same thing text_flag_dropshadow does. Both
    // copies go through one warp, so the shadow deforms with its glyph instead of tearing off it.
    // SUB-PIXEL: ImFont::RenderText floors the pen to whole pixels, so mig::SubPixelText puts the
    // fraction back on the quads (it was a scale of 1 about -frac here, i.e. nothing: tags froze
    // mid-glide, then jumped a pixel when their slot landed) and pads them so glyph edges glide
    // too. Resting tags sit on whole pixels (the layout below): the crisp 1:1 blit
    const auto draw = [&](const Ent& e)
    {
        const int v0 = dl->VtxBuffer.Size;
        dl->AddText(font, fs, ImVec2(e.pos.x + 1.f, e.pos.y + 1.f), e.shadow, e.text);
        dl->AddText(font, fs, e.pos, e.col, e.text);
        mig::SubPixelText(dl, v0, e.pos, font);
        if (e.sx != 1.f || e.sy != 1.f) ScaleVerts(dl, v0, e.piv, e.sx, e.sy);
    };

    if (g_miHorizontal)
    {
        // row, whole line centred. Each item's fade scales the SLOT it occupies, so a bind
        // coming or going slides its neighbours instead of overlapping them. Tight gap --
        // coldtires notes half a fontsize reads as "too far apart" in game
        // EVEN widths and spacing: at rest the row total is even, so the centred start and
        // every tag land on whole pixels (crisp), while anything moving stays continuous
        // (sub-pixel, anti-aliased by the draw above). Nothing here is floored mid-animation,
        // which would bring the pixel stepping back
        const auto even = [](float v) { return 2.f * ceilf(v * 0.5f); };
        const float spacing = even(fs * 0.2f);
        float total = 0.f;
        for (int i = 0; i < n; ++i)
        {
            if (i) total += spacing * ents[i].anim;
            total += even(ents[i].size.x) * ents[i].anim;
        }

        const float y = floorf(io.DisplaySize.y - g_miOffsetY);
        float x = floorf(io.DisplaySize.x * 0.5f) - total * 0.5f;
        for (int i = 0; i < n; ++i)
        {
            if (i) x += spacing * ents[i].anim;
            const float w    = even(ents[i].size.x);
            const float slot = w * ents[i].anim;
            // drawn centred in its own slot: a lone tag's slot centre IS the screen centre at
            // any slot width; a second tag widens the row and pushes both out. The inner offset
            // is a whole pixel, so the rest position stays on the grid
            ents[i].pos = ImVec2(x + (slot - w) * 0.5f + floorf((w - ents[i].size.x) * 0.5f), y);
            x += slot;
        }
    }
    else
    {
        // vertical (default): stack upward from the bottom, the fade scaling each slot's height
        float y = floorf(io.DisplaySize.y - g_miOffsetY);
        for (int i = 0; i < n; ++i)
        {
            ents[i].pos = ImVec2(floorf((io.DisplaySize.x - ents[i].size.x) * 0.5f), y);
            y -= ents[i].size.y * ents[i].anim;
        }
    }

    // mitosis: a tag never fades in on its own, it divides off the tag next to it.
    //   * every tag dividing off a neighbour, and that neighbour, leave the text pass and come
    //     back as one migoo.h picture: slide, squash & stretch, liquid bridge, snap, morph
    //   * a tag with nothing to divide off (lone, or born beside another new one) pops in from
    //     half size (mig::PopAt) -- in the picture, scaled on its distance field so it stays
    //     sharp (the text-quad scale below is only the fallback, and it blurs)
    //   * if the picture cannot draw, the dividing pairs get miwarp.h's lean on the text instead
    bool imaged[MI_COUNT] = {};
    if (g_miMitosis)
    {
        const auto centre = [&](int i)
        { return ImVec2(ents[i].pos.x + ents[i].size.x * 0.5f, ents[i].pos.y + fs * 0.5f); };

        float raw[MI_COUNT];
        int   par[MI_COUNT], map[MI_COUNT];
        bool  in[MI_COUNT] = {};
        for (int i = 0; i < n; ++i) raw[i] = ents[i].raw;
        for (int i = 0; i < n; ++i)
        {
            par[i] = -2;
            if (raw[i] >= 0.995f) continue;
            par[i] = mig::PickParent(raw, n, i);
            if (par[i] == -1)
            {
                ents[i].piv = centre(i);
                ents[i].sx  = ents[i].sy = mig::PopAt(raw[i]);
                in[i] = raw[i] > 0.001f;   // gone, only its slot still closing: draw nothing
                continue;
            }
            in[i] = in[par[i]] = true;
            // the text fallback's lean, from the layout (the picture works its own out from
            // where the halves really are this frame)
            for (int k : { i, par[i] })
            {
                const ImVec2 c = centre(k), o = centre(k == i ? par[i] : i);
                Ent& e = ents[k];
                if (g_miHorizontal)
                {
                    const miw::Warp w = miw::SplitWarp(c.x, e.size.x * 0.5f, o.x, raw[i]);
                    e.piv = ImVec2(w.piv, c.y); e.sx = w.sa; e.sy = w.sp;
                }
                else
                {
                    const miw::Warp w = miw::SplitWarp(c.y, fs * 0.5f, o.y, raw[i]);
                    e.piv = ImVec2(c.x, w.piv); e.sy = w.sa; e.sx = w.sp;
                }
            }
        }
        mig::Tag tags[MI_COUNT];
        int nt = 0;
        for (int i = 0; i < n; ++i) if (in[i]) map[i] = nt++;
        for (int i = 0; i < n; ++i)
        {
            if (!in[i]) continue;
            const Ent& e = ents[i];
            mig::Tag& t = tags[map[i]];
            t.text = e.text; t.pos = e.pos; t.size = e.size; t.raw = e.raw;
            t.col[0] = e.rgb[0]; t.col[1] = e.rgb[1]; t.col[2] = e.rgb[2];
            t.parent = par[i] >= 0 ? map[par[i]] : par[i];   // -1 pops, -2 settled
        }
        if (nt && DrawMitosis(dl, font, fs, tags, nt, g_miHorizontal))
            for (int i = 0; i < n; ++i) imaged[i] = in[i];
    }

    // a gone tag still holding its closing slot: faded out already, or in mitosis merged into its
    // parent -- the text fallback would draw it solid, so skip it
    for (int i = 0; i < n; ++i)
    {
        if (!imaged[i] && ents[i].raw > 0.001f) draw(ents[i]);
        // burst from the tag's centre (pos is its top-left corner)
        if (ents[i].justHit)
            KeyIndFx_OnHit(ImVec2(ents[i].pos.x + ents[i].size.x * 0.5f, ents[i].pos.y + ents[i].size.y * 0.5f),
                           ents[i].col);
    }
    KeyIndFx_DrawParticles(dl, dt);
}
