#pragma once
// csgo team counter (aim/teamcounter.cpp) layout math, pure: tests/teamcounter_test.cpp.
// Every number is CS:GO panorama: layout/hud/hudteamcounter.xml + styles/hud/hudteamcounter.css
// (code.pbin), hud.css #HudTeamCounter, csgosrc2019 csgo_hudteamcounter.cpp. Layout px at a 1080
// tall screen; x is relative to the screen centre, y to the screen top.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace tc
{
// hud.css #HudTeamCounter margin-top 2px + .TeamCounter__Align--Top #TeamCounterBG margin-top 2px
inline constexpr float kTop       = 4.f;
// #ScoreAndTimeAndBomb width ScoreAndTimeWidth; #GameTime height; #GameScore margin-top 1px, height
inline constexpr float kCenterW   = 84.f;
inline constexpr float kTimeH     = 27.f;
inline constexpr float kScoreTop  = 28.f;
inline constexpr float kScoreH    = 27.f;
inline constexpr float kScoreW    = 42.f;   // .GameScore__BGL/R width GameScoreWidth, BGL margin-right 1px
inline constexpr int   kTimerPx   = 24;     // .teamcounter_timer font-size
inline constexpr int   kScorePx   = 24;     // .TeamScore font-size
inline constexpr float kTimerSpacing = 0.5f;   // letter-spacing
// Stratum2 Bold digits span y 0..666 under a 900 / 300 win ascent / descent cell (the GDI tall):
// their middle is (900 - 333) / 1200 of the cell down. Centring that, not the cell, puts the digits
// in the middle of their box (panorama's margin-bottom 4px on the timer offsets its own line box)
inline constexpr float kDigitMid = (900.f - 333.f) / 1200.f;
inline float DigitTop(float boxTop, float boxBottom, float cellTall) { return (boxTop + boxBottom) * 0.5f - cellTall * kDigitMid; }

// large (SetModeNormalTen): AvatarLargeSnippet--ScaleSmaller ui-scale 88% on every avatar
inline constexpr float kLScale    = 0.88f;
inline constexpr float kLW        = 60.f;   // .AvatarL width AvatarLBGWidth, margin 1px each side
inline constexpr float kLImgH     = 62.f;   // #AvatarL__ImageBG height AvatarLImageHeight (overflow noclip)
inline constexpr float kLMargin   = 1.f;
inline constexpr float kTeamMargin = 1.f;   // .Team__Large margin 1px / .Team__SmallCT margin-right / SmallT margin-left
// small (SetModeNormal): two rows per team, .AvatarS 27x27, margin 1px each side, Row2 margin-top 1px
inline constexpr float kSW        = 27.f;
inline constexpr float kSMargin   = 1.f;
inline constexpr float kRowGap    = 1.f;

inline constexpr int kLargeSlots  = 5;      // MAX_AVATAR_PANELS / 2
inline constexpr int kSmallSlots  = 12;     // MAX_PAVATAR_PANELS / 2

// CS:GO picks by server max players (> 10 = casual rows). Here: by who is on the teams now,
// comp layout while neither team has more than 5
inline bool UseLarge(int nLeft, int nRight) { return nLeft <= kLargeSlots && nRight <= kLargeSlots; }
inline int  MaxSlots(bool large) { return large ? kLargeSlots : kSmallSlots; }

// #AvatarL__ImageBG / #AvatarS__ImageBG box of team slot `slot` (0 = next to the timer).
// u = the snippet's ui-scale: every px inside the avatar is multiplied by it.
struct Box { float x, y, w, h, u; };
inline Box AvatarBox(bool large, bool left, int slot)
{
    const float edge = kCenterW * 0.5f + kTeamMargin;   // team container edge, off the centre block
    if (large)
    {
        // CT: panel idx = 4 - slot in a right aligned flow-right row, so slot 0 sits next to the
        // centre; T: idx = 5 + slot left aligned. Pitch = 60 + 2 * 1 margin, all at 88%
        const float u = kLScale, pitch = (kLW + 2.f * kLMargin) * u;
        const float x = left ? -edge - (slot + 1) * pitch + kLMargin * u
                             :  edge + slot * pitch + kLMargin * u;
        return { x, kTop, kLW * u, kLImgH * u, u };
    }
    // CT idx = 11 - slot, (idx & 1) ? Row1 : Row2; T idx = slot, (idx & 1) ? Row2 : Row1. Either
    // way even slots go on the top row, odd on the bottom, slot >> 1 places out from the centre
    const int row = slot & 1, k = slot >> 1;
    const float pitch = kSW + 2.f * kSMargin;
    const float x = left ? -edge - (k + 1) * pitch + kSMargin
                         :  edge + k * pitch + kSMargin;
    return { x, kTop + row * (kSW + kRowGap), kSW, kSW, 1.f };
}

// UpdateMiniScoreboard: health shows for teammates, for anyone while spectating, and for everyone
// once the local player is dead and mp_forcecamera is 0 (OBS_ALLOW_ALL)
inline bool ShowHealth(bool localSpectator, bool sameTeam, bool localAlive, int forceCamera)
{
    return localSpectator || sameTeam || (!localAlive && forceCamera == 0);
}

// CSGOAvatarHealthBar width = value % of the image box (value 0..100 = CS:GO health). TF2: % of max
inline int HealthPct(int hp, int maxHp)
{
    if (hp <= 0 || maxHp <= 0) return 0;
    return std::clamp((int)((int64_t)hp * 100 / maxHp), 0, 100);
}
inline bool HealthRed(int pct) { return pct <= 20; }   // health > 20 ? --Normal : --Red

// UpdateTimer: ceil of the seconds left, "%d:%.2d"; red below kTimeRemainingToDisplayRed 11
inline int  TimerSeconds(float left) { return (std::max)(0, (int)std::ceil(left)); }
inline bool TimerRed(int secs) { return secs < 11; }
inline void TimerText(int secs, char* out, size_t n) { std::snprintf(out, n, "%d:%.2d", secs / 60, secs % 60); }

// border on the image box: UpdateAvatarSlot border type, css cascade per avatar size.
//   large: Avatar__PlayerBorder--CT/T 2.5px team colour, Outline--White 2.5px for the local
//          player alive, Outline--Spectate 3px #D8D8D8D8 for the spectated one (set last, wins);
//          an outline class zeroes .Avatar__CommonMargin (1px) round the avatar image
//   small: AvatarS__Default--CT/T 1.5px team colour comes after every border class in the css, so
//          it is the border whatever the type; the outline still zeroes the image margin
struct Border { float w; uint32_t rgba; float imgMargin; };
inline constexpr uint32_t kColorCT = 0xB5D4EEFFu;   // csgostyles.css color-CT
inline constexpr uint32_t kColorT  = 0xEAD18AFFu;   // color-T
inline Border BorderFor(bool large, bool left, bool local, bool spectated)
{
    const uint32_t team = left ? kColorCT : kColorT;
    const bool outline = local || spectated;
    if (!large) return { 1.5f, team, outline ? 0.f : 1.f };
    if (spectated) return { 3.f, 0xD8D8D8D8u, 0.f };
    if (local)     return { 2.5f, 0xFFFFFFFFu, 0.f };
    return { 2.5f, team, 1.f };
}

// Avatar__Skull--*: teammates of the local player red, the rest their team's dark wash. All .7
enum class Skull { Red, CT, T };
inline Skull SkullFor(bool left, int localSide /* -1 none, 0 left, 1 right */)
{
    if (localSide == (left ? 0 : 1)) return Skull::Red;
    return left ? Skull::CT : Skull::T;
}
inline uint32_t SkullRgba(Skull s)
{
    switch (s)
    {
    case Skull::Red: return 0xCC0033B3u;   // #cc0033, opacity .7
    case Skull::CT:  return 0x47545FB3u;   // #47545f
    default:         return 0x4E4227B3u;   // #4e4227
    }
}
} // namespace tc
