#pragma once
// CS:GO sniper scope, ported 1:1 from csgosrc2019, engine-free.
// SAME FILE in SimpleVisual and fpsoverlay (src/hacks/visuals/scope_csgo.h) -- edit both.
// Gate test: tests/scopecsgo_test.cpp.
//
// Sources (csgosrc2019/src/game):
//   client/cstrike15/cs_hud_scope.cpp   CHudScope::Paint           -> ScCs_Layout
//   shared/cstrike15/weapon_csbase.cpp  UpdateAccuracyPenalty,
//                                       GetInaccuracy, OnLand      -> ScCs_Inaccuracy
//   scripts/items/items_game.txt        sniper prefabs             -> kScCsWeapons
//
// TF adaptations:
//   - weapon GetMaxSpeed() -> player m_flMaxspeed (TF shrinks it while zoomed, like "max player speed alt")
//   - TICK_INTERVAL decay -> frame dt (display only, exp decay composes the same)
//   - m_bIsScoped / m_weaponMode -> TF_COND_ZOOMED
//   - fishtail -> 0 (csgo never sets m_fAccuracyFishtail non-zero)
//   - turning inaccuracy -> 0 (sv_turning_inaccuracy_enabled 0), no ladder/reload/fire terms
//   - moveSens / airSens scale the motion terms (1 = csgo, 0 = still scope)
#include <cmath>

struct ScCsModeStats { float stand, crouch, move, jump, land, spread; };
struct ScCsWeapon
{
    const char*   name;
    ScCsModeStats pri, alt;         // Primary_Mode (unscoped) / Secondary_Mode (scoped)
    float         jumpInitial;      // no alt variant
    float         recStand, recCrouch;
    float         zoomFov1, zoomFov2;
};

// items_game.txt prefab values * 0.001 (cs_weapon_parse.cpp GET_WEAPON_ATTR_FSCALE: inaccuracy + spread).
// recovery / zoom fov are unscaled. SCAR-20 and G3SG1 prefabs are identical.
inline constexpr ScCsWeapon kScCsWeapons[] = {
    { "AWP",
      { 0.0808f,  0.0606f,  0.17648f, 0.13383f, 0.000307f, 0.0002f  },
      { 0.002f,   0.0015f,  0.17648f, 0.13383f, 0.0001f,   0.0002f  },
      0.17286f, 0.34539f, 0.24671f, 40.f, 10.f },
    { "SSG 08",
      { 0.0317f,  0.02378f, 0.12345f, 0.00572f, 0.000215f, 0.00028f },
      { 0.003f,   0.0028f,  0.12345f, 0.00572f, 0.000215f, 0.00023f },
      0.20872f, 0.142096f, 0.055783f, 40.f, 15.f },
    { "SCAR-20 / G3SG1",
      { 0.0258f,  0.01935f, 0.15048f, 0.15377f, 0.000262f, 0.0003f  },
      { 0.002f,   0.0015f,  0.15048f, 0.15377f, 0.000262f, 0.0003f  },
      0.10769f, 0.544331f, 0.388808f, 40.f, 15.f },
};
inline constexpr int kScCsWeaponCount = 3;

inline constexpr float kScCsDuckSpeedMod = 0.34f;        // CS_PLAYER_SPEED_DUCK_MODIFIER
inline constexpr float kScCsMoveCurveExp = 0.25f;        // MOVEMENT_CURVE01_EXPONENT
inline constexpr float kScCsJumpImpulse  = 301.993377f;  // sv_jump_impulse
inline constexpr float kScCsMinFov       = 25.f;         // kScopeMinFOV

struct ScCsInput
{
    bool  zoomed   = false;
    bool  onGround = true;
    bool  ducking  = false;
    float speed2D  = 0.f;
    float velZ     = 0.f;
    float maxSpeed = 80.f;
};

struct ScCsState
{
    float penalty     = 0.f;    // m_fAccuracyPenalty
    bool  wasGround   = true;
    float lastVelZ    = 0.f;
    float animInset   = 1.f;    // CHudScope ctor
    float lineSpread  = 1.f;
};

inline float ScCs_RemapClamped(float v, float a, float b, float ra, float rb)
{
    if (a == b) return v >= b ? rb : ra;
    float t = (v - a) / (b - a);
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    return ra + (rb - ra) * t;
}

inline float ScCs_Approach(float target, float value, float speed)
{
    const float d = target - value;
    if (d > speed)  return value + speed;
    if (d < -speed) return value - speed;
    return target;
}

// OnLand + UpdateAccuracyPenalty + GetInaccuracy, in the mode TF_COND_ZOOMED picks.
// Returns GetInaccuracy() (not + spread).
inline float ScCs_Inaccuracy(ScCsState& s, const ScCsInput& in, const ScCsWeapon& w,
                             float dt, float moveSens, float airSens)
{
    const ScCsModeStats& m = in.zoomed ? w.alt : w.pri;

    // OnLand(m_flFallVelocity): banked the frame we touch down
    if (in.onGround && !s.wasGround)
        s.penalty += m.land * std::fabs(s.lastVelZ) * airSens;
    s.wasGround = in.onGround;
    s.lastVelZ  = in.velZ;

    float target;
    if (!in.onGround)    target = m.stand + m.jump * airSens;     // weapon_air_spread_scale 1
    else if (in.ducking) target = m.crouch;
    else                 target = m.stand;

    if (target > s.penalty)
        s.penalty = target;
    else
    {
        const float rec = !in.onGround ? w.recCrouch * 4.f       // 400% air recovery penalty
                        : in.ducking   ? w.recCrouch : w.recStand;
        const float decay = std::log(10.f) / rec;
        s.penalty = target + (s.penalty - target) * std::exp(dt * -decay);   // Lerp(t, new, old)
    }

    float acc = s.penalty;
    const float maxSpeed = in.maxSpeed > 1.f ? in.maxSpeed : 80.f;
    const float mv = ScCs_RemapClamped(in.speed2D, maxSpeed * kScCsDuckSpeedMod, maxSpeed * 0.95f, 0.f, 1.f);
    if (mv > 0.f)
        acc += std::pow(mv, kScCsMoveCurveExp) * m.move * moveSens;

    if (!in.onGround)
    {
        const float sqMax = std::sqrt(kScCsJumpImpulse);
        const float sqV   = std::sqrt(std::fabs(in.velZ));
        // RemapVal (unclamped) then clamp [0, 2x initial]
        float air = (sqV - sqMax * 0.25f) / (sqMax - sqMax * 0.25f) * w.jumpInitial;
        if (air < 0.f) air = 0.f;
        else if (air > 2.f * w.jumpInitial) air = 2.f * w.jumpInitial;
        acc += air * airSens;
    }

    return acc > 1.f ? 1.f : acc;
}

// Everything CHudScope::Paint draws, in screen pixels. ints stay ints where csgo's did.
struct ScCsFrame
{
    int   x, y;                     // cross centre (offset by bob)
    int   x1, y1, x2, y2;           // arc box
    int   middleX, middleY;         // == x, y
    int   xMod, yMod;               // lens half extents
    float blurWidth;                // fBlurWidth
    float screenBlurWidth;          // fScreenBlurWidth
    int   blurAlpha;                // iBlurAlpha
    bool  blurred;                  // textured blur lines instead of hard lines
    int   thickness;                // cl_crosshair_sniper_width, >= 1
    int   offsetX, offsetY;
};

// Scope out: csgo's "GetFOV() == GetDefaultFOV() && !m_bIsScoped" branch -- next scope-in animates from here.
inline void ScCs_Unscoped(ScCsState& s)
{
    s.animInset  = 2.f;
    s.lineSpread = 20.f;
}

// Scoped frame. inaccuracy = GetInaccuracy(), zoomFov = weapon GetZoomFOV, rawLat/rawVert = BobState raw bob.
// scale sizes the arc box + lens about the screen centre (1 = csgo, exact ints); lines stay screen-long.
inline ScCsFrame ScCs_Layout(ScCsState& s, const ScCsWeapon& w, float inaccuracy, float zoomFov,
                             bool showNormalInaccuracy, int sniperWidth, float dt,
                             int screenWide, int screenTall, float rawLat, float rawVert, float scale = 1.f)
{
    if (!(scale > 0.01f)) scale = 0.01f;
    ScCsFrame f{};
    const float fov = zoomFov > kScCsMinFov ? zoomFov : kScCsMinFov;
    const float halfFov = fov * (3.14159265358979323846f / 180.f) * 0.5f;
    const float inaccIn640 = 320.f / std::tan(halfFov);

    // scoped -> Secondary_Mode spread
    float wi = inaccuracy + w.alt.spread;
    if (!showNormalInaccuracy)
        wi -= w.alt.stand + w.alt.spread;
    if (wi < 0.f) wi = 0.f;

    const float rawSpread = wi * inaccIn640;
    const float spread = rawSpread < 0.f ? 0.f : (rawSpread > 100.f ? 100.f : rawSpread);

    const float insetGoal = spread * (0.4f / 30.f);
    s.animInset = ScCs_Approach(insetGoal, s.animInset, std::fabs((insetGoal - s.animInset) * dt) * 19.f);
    s.lineSpread = ScCs_RemapClamped(dt * 140.f, 0.f, 1.f, s.lineSpread, rawSpread);

    const int tall14 = screenTall / 14;
    f.offsetX = (int)(rawLat * tall14 + 0.f);     // + flAccuracyFishtail (always 0)
    f.offsetY = (int)(rawVert * tall14);

    float inacBlur = s.animInset * 0.04f;
    if (inacBlur > 0.22f) inacBlur = 0.22f;

    const int inset = (int)(tall14 + inacBlur * (screenTall * 0.5f));
    // csgo: y1 = inset, x1 = (W-H)/2 + inset, y2 = H - y1, x2 = W - x1. Scaled about the centre;
    // at 1 the centre cancels exactly, so the ints are csgo's.
    const float cx = screenWide * 0.5f, cy = screenTall * 0.5f;
    const int y1 = (int)(cy - (cy - inset) * scale);
    const int x1 = (int)(cx - (cx - ((screenWide - screenTall) / 2 + inset)) * scale);
    f.y1 = y1 + f.offsetY;
    f.x1 = x1 + f.offsetX;
    f.y2 = screenTall - y1 + f.offsetY;
    f.x2 = screenWide - x1 + f.offsetX;

    f.x = screenWide / 2 + f.offsetX;
    f.y = screenTall / 2 + f.offsetY;
    f.xMod = (int)((screenWide / 2 + f.offsetX + inacBlur * screenTall) * scale);
    f.yMod = (int)((screenTall / 2 + f.offsetY + inacBlur * screenTall) * scale);
    f.middleX = f.x;
    f.middleY = f.y;

    f.blurWidth = std::pow(s.lineSpread, 0.75f);
    f.screenBlurWidth = f.blurWidth * screenTall / 640.f;
    f.thickness = sniperWidth < 1 ? 1 : sniperWidth;

    constexpr float kMaxVarianceWithFullAlpha = 1.8f;
    f.blurred = !(f.screenBlurWidth <= f.thickness + 0.5f);
    float a;
    if (!f.blurred) a = (f.blurWidth < 1.f) ? 1.f : 1.f / f.blurWidth;
    else            a = (f.blurWidth < kMaxVarianceWithFullAlpha) ? 1.f : kMaxVarianceWithFullAlpha / f.blurWidth;
    a = std::sqrt(a);
    int ia = (int)(a * 255.f);
    f.blurAlpha = ia < 140 ? 140 : (ia > 255 ? 255 : ia);
    return f;
}

// DXT5 block decode -> RGBA8 (D3D9 path is A8R8G8B8 so wrappers refusing DXT still get the art).
inline void ScCs_DecodeDxt5(const unsigned char* src, int w, int h, unsigned char* rgba)
{
    auto c565 = [](unsigned v, unsigned char* o) {   // bit replication, as GPUs expand 565
        const unsigned r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
        o[0] = (unsigned char)((r << 3) | (r >> 2));
        o[1] = (unsigned char)((g << 2) | (g >> 4));
        o[2] = (unsigned char)((b << 3) | (b >> 2));
    };
    for (int by = 0; by < (h + 3) / 4; ++by)
    for (int bx = 0; bx < (w + 3) / 4; ++bx)
    {
        const unsigned char* b = src + (by * ((w + 3) / 4) + bx) * 16;
        unsigned char a[8];
        a[0] = b[0]; a[1] = b[1];
        if (a[0] > a[1]) for (int i = 1; i < 7; ++i) a[i + 1] = (unsigned char)(((7 - i) * a[0] + i * a[1]) / 7);
        else { for (int i = 1; i < 5; ++i) a[i + 1] = (unsigned char)(((5 - i) * a[0] + i * a[1]) / 5); a[6] = 0; a[7] = 255; }
        unsigned long long ai = 0;
        for (int i = 0; i < 6; ++i) ai |= (unsigned long long)b[2 + i] << (8 * i);

        unsigned char c[4][3];
        const unsigned v0 = b[8] | (b[9] << 8), v1 = b[10] | (b[11] << 8);
        c565(v0, c[0]); c565(v1, c[1]);
        for (int k = 0; k < 3; ++k) {   // DXT5 color block is always 4-colour
            c[2][k] = (unsigned char)((2 * c[0][k] + c[1][k]) / 3);
            c[3][k] = (unsigned char)((c[0][k] + 2 * c[1][k]) / 3);
        }
        const unsigned ci = b[12] | (b[13] << 8) | (b[14] << 16) | ((unsigned)b[15] << 24);

        for (int py = 0; py < 4; ++py)
        for (int px = 0; px < 4; ++px)
        {
            const int X = bx * 4 + px, Y = by * 4 + py;
            if (X >= w || Y >= h) continue;
            const int i = py * 4 + px;
            unsigned char* o = rgba + (Y * w + X) * 4;
            const unsigned char* col = c[(ci >> (2 * i)) & 3];
            o[0] = col[0]; o[1] = col[1]; o[2] = col[2];
            o[3] = a[(ai >> (3 * i)) & 7];
        }
    }
}
