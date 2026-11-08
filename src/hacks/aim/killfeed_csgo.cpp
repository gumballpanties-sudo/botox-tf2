#include "sdk.h"
#include "../../dependencies/fonts/stratum2_bold.h"
#include "../menu/fonts.h"   // g_fontKf*: fonts tab > killfeed
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <vector>

// ============================================================================================
// csgo killfeed, 1:1 port of CS:GO's panorama CCSGO_HudDeathNotice
// (csgosrc2019 cstrike15/panorama/hud/csgo_huddeathnotice.cpp + the shipped
// panorama/layout/hud/huddeathnotice.xml / styles/hud/huddeathnotice.css from code.pbin),
// drawing TF2's own kill icons (CHudBaseDeathNotice::GetIcon, the same d_ textures the stock
// feed uses). TF2's HudDeathNotice is left running (domination / revenge / penetration sounds
// live in its event handler) and only its paint is skipped (world_fx3.cpp WorldFx3_SkipPanel).
//
// Event half: killfeed.cpp h_FireEvent (FireEventClientSide, sees demo playback too, after the
// skinchanger rewrote "weapon"). Draw half: hooks.cpp IEngineVGui::Paint, the game's surface
// pass, so it is in startmovie recordings like the stock feed.
//
// Left out, with why:
//   revenge icon     TF2 has no revenge CHudTexture in mod_textures.txt
//   domination       event flag only: CS:GO also checks IsPlayerDominated(victim), TF2 sends
//                    m_bPlayerDominated to the local player only
//   headshot /       no separate icons: TF2's icon already is the headshot / penetration icon
//   penetrate        (d_headshot, d_headshot_player_penetration), picked exactly as TF2 does
//   noscope/smoke/   CS:GO hides them in its own css (visibility: collapse, no class sets them)
//   blind/flash
//   letter-spacing   0.45px, vgui text has none
// ============================================================================================

extern bool g_killfeedCsgo;              // dllmain.cpp
extern bool g_selfKillfeedOnly;          // dllmain.cpp
extern std::atomic<bool> g_eject;        // dllmain.cpp
void DebugLog(const char* fmt, ...);     // dllmain.cpp

namespace
{
    // csgo_huddeathnotice.cpp
    constexpr int   kNameTruncate          = 22;
    constexpr int   kAssistNameTruncate    = 18;
    constexpr int   kAssistShortTruncate   = 12;
    constexpr int   kMaxPending            = 15;
    // huddeathnotice.css @defines
    constexpr float kLifetime              = 5.0f;
    constexpr float kLocalPlayerLifetimeMod = 1.5f;
    constexpr float kFadeOutTime           = 1.0f;   // also the fade in: .Available's .2s is the FROM style
    constexpr float kMoveTime              = 0.2f;   // .DeathNotice transition: position .2s ease-out

    // TF2 shareddefs
    constexpr int kDeathDomination = 0x0001, kDeathFeign = 0x0020, kDeathAustralium = 0x0400;
    constexpr int kDmgCritical     = 1 << 20;   // DMG_CRITICAL = DMG_ACID
    constexpr int kCustomHeadshot = 1, kCustomBackstab = 2, kCustomBurningArrow = 17, kCustomFlyingBurn = 18,
                  kCustomPumpkinBomb = 19, kCustomHeadshotDecap = 51, kCustomKart = 75, kCustomGiantHammer = 76;

    struct Notice
    {
        wchar_t attacker[72], assister[72], victim[72];   // name + "..."
        int  attackerTeam, assisterTeam, victimTeam;
        bool assist, suicide, dominated, isVictim, isKiller;
        CHudTexture* weapon;     // nullptr on suicide (css: .DeathNoticeSuicide #Weapon collapse)
        CHudTexture* crit;       // TF2's crit / australium glow, drawn behind the weapon icon
        CHudTexture* skull;      // suicide icon
        CHudTexture* domination;
        float spawn, lifeMod;
        float fadeStart;         // < 0 = not fading
        float yFrom, yTo, yT0;   // position transition, panorama px; yTo < 0 = not placed yet
    };

    std::mutex          s_mtx;
    std::vector<Notice> s_notices;   // oldest first, the VisibleNotices child order
    float               s_lastTime = 0.f;

    // css ease-out = cubic-bezier(0, 0, .58, 1)
    float EaseOut(float p)
    {
        if (p <= 0.f) return 0.f;
        if (p >= 1.f) return 1.f;
        float t = p;
        for (int i = 0; i < 8; i++)   // newton on x(t) = 3(1-t)t^2*.58 + 3(1-t)^2 t*0 + t^3
        {
            const float x  = 3.f * (1.f - t) * t * t * 0.58f + t * t * t;
            const float dx = 3.f * 0.58f * (2.f * t - 3.f * t * t) + 3.f * t * t;
            if (fabsf(dx) < 1e-5f) break;
            t -= (x - p) / dx;
            t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
        }
        return 3.f * (1.f - t) * (1.f - t) * t + 3.f * (1.f - t) * t * t + t * t * t;   // y: p1 = 0, p2 = 1
    }

    // c_cs_player.cpp TruncatePlayerName: keeps nTruncateAt + 1 characters, then "..."
    void Truncate(wchar_t* s, int n)
    {
        if ((int)wcslen(s) >= n + 1)
            wcscpy_s(s + n + 1, 72 - (n + 1), L"...");
    }

    void PlayerName(int idx, wchar_t (&out)[72])
    {
        out[0] = 0;
        player_info_t pi{};
        if (idx > 0 && I::EngineClient->GetPlayerInfo(idx, &pi))   // h_GetPlayerInfo: custom names applied
        {
            pi.name[sizeof(pi.name) - 1] = 0;
            MultiByteToWideChar(CP_UTF8, 0, pi.name, -1, out, 64);
            out[63] = 0;
        }
    }

    int PlayerTeam(int idx)
    {
        if (idx <= 0) return 0;
        auto pEnt = I::ClientEntityList->GetClientEntity(idx);
        return pEnt ? pEnt->As<CBaseEntity>()->m_iTeamNum() : 0;
    }

    CHudTexture* Icon(const char* name)
    {
        __try { return H::Draw.GetIcon(name, 0); }   // standard d_ art: csgo rows are always dark
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    // CTFHudDeathNotice::OnGameEvent's icon pick, minus the suicide-only branches (a suicide
    // shows the skull here, csgo style)
    void PickWeaponIcon(IGameEvent* e, const char* weapon, char (&icon)[96])
    {
        snprintf(icon, sizeof(icon), "d_%s", weapon);
        switch (e->GetInt("customkill"))
        {
        case kCustomBackstab:
            strcpy_s(icon, !strcmp(icon, "d_sharp_dresser") ? "d_sharp_dresser_backstab" : "d_backstab");
            break;
        case kCustomHeadshot:
        case kCustomHeadshotDecap:
            if (!strcmp(weapon, "ambassador"))    strcpy_s(icon, "d_ambassador_headshot");
            else if (!strcmp(weapon, "huntsman")) strcpy_s(icon, "d_huntsman_headshot");
            else if (!e->IsEmpty("playerpenetratecount") && e->GetInt("playerpenetratecount") > 0)
                strcpy_s(icon, "d_headshot_player_penetration");
            else strcpy_s(icon, "d_headshot");
            break;
        case kCustomBurningArrow: strcpy_s(icon, "d_huntsman_burning"); break;
        case kCustomFlyingBurn:   strcpy_s(icon, "d_huntsman_flyingburn"); break;
        case kCustomPumpkinBomb:  strcpy_s(icon, "d_pumpkindeath"); break;
        case kCustomKart:         strcpy_s(icon, "d_bumper_kart"); break;
        case kCustomGiantHammer:  strcpy_s(icon, "d_necro_smasher"); break;
        }
    }

    // OnPlayerDeath. POD out only: the caller holds the lock (no C++ unwinding under __try).
    bool BuildNotice(IGameEvent* e, Notice& n)
    {
        if (strcmp(e->GetName(), "player_death") != 0)
            return false;

        const int local    = I::EngineClient->GetLocalPlayer();
        const int attacker = I::EngineClient->GetPlayerForUserID(e->GetInt("attacker"));
        const int victim   = I::EngineClient->GetPlayerForUserID(e->GetInt("userid"));
        int assister       = I::EngineClient->GetPlayerForUserID(e->GetInt("assister"));   // mp_display_kill_assists 1

        const char* weapon = e->GetString("weapon");
        if (!weapon || !weapon[0])
            return false;
        if (local <= 0 || !I::ClientEntityList->GetClientEntity(local))   // csgo: no local player, no notice
            return false;

        // TF2's ShouldShowDeathNotice / feign handling: the feed must not out a dead ringer to
        // the spy's own team or to the spy, nor a silent kill to the victim's team
        const int flags      = e->GetInt("death_flags");
        const int victimTeam = PlayerTeam(victim);
        const int localTeam  = PlayerTeam(local);
        if (flags & kDeathFeign)
        {
            if (victim == local || victimTeam == localTeam)
                return false;
        }
        if (e->GetBool("silent_kill") && victimTeam == localTeam && victim != local)
            return false;

        memset(&n, 0, sizeof(n));
        n.suicide  = (attacker == 0 || attacker == victim);
        n.isVictim = local == victim;
        n.isKiller = !n.isVictim && (local == attacker || local == assister);   // victim AND killer in a suicide

        if (g_selfKillfeedOnly && !n.isVictim && !n.isKiller)
            return false;

        PlayerName(attacker, n.attacker);
        PlayerName(victim, n.victim);
        if (assister > 0)
        {
            if (assister == attacker) assister = 0;   // bot takeover case
            else PlayerName(assister, n.assister);
        }
        n.assist = assister > 0;

        n.attackerTeam = PlayerTeam(attacker);
        n.victimTeam   = victimTeam;
        n.assisterTeam = PlayerTeam(assister);
        n.dominated    = (flags & kDeathDomination) != 0;

        if (n.assist)
        {
            Truncate(n.attacker, kAssistNameTruncate);
            Truncate(n.assister, kAssistShortTruncate);
            Truncate(n.victim, kAssistNameTruncate);
        }
        else
        {
            Truncate(n.attacker, kNameTruncate);
            Truncate(n.victim, kNameTruncate);
        }

        // Attacker label = victim's name on a suicide (bVictimsAttackerIsSuicide); still colored
        // by nAtackerTeam, which is 0 (uncolored) for a world kill
        if (n.suicide)
            wcscpy_s(n.attacker, n.victim);

        n.skull = Icon("d_skull_tf");
        if (!n.suicide)
        {
            char icon[96];
            PickWeaponIcon(e, weapon, icon);
            n.weapon = Icon(icon);
            if (!n.weapon) n.weapon = n.skull;   // TF2's own fallback
            if (flags & kDeathAustralium)                   n.crit = Icon("d_australium");
            else if (e->GetInt("damagebits") & kDmgCritical) n.crit = Icon("d_crit");
        }
        if (n.dominated)
            n.domination = Icon("leaderboard_dominated");

        n.spawn     = I::GlobalVars->curtime;
        n.lifeMod   = (n.isVictim || n.isKiller) ? kLocalPlayerLifetimeMod : 1.f;
        n.fadeStart = -1.f;
        n.yTo       = -1.f;
        return true;
    }

    bool BuildNoticeSafe(void* pEvent, Notice& n)
    {
        __try { return BuildNotice(reinterpret_cast<IGameEvent*>(pEvent), n); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // ---- font: fonts tab > killfeed (menu/fonts.h). Default Stratum2 Bold 18px css, decoded
    // from csgo/panorama/fonts/stratum2bold.vfont ----
    char     s_fontName[64] = {};   // vgui copies it, this keeps m_szName valid
    Font_t   s_font = { s_fontName, 0, 0, 700, 0 };
    uint32_t s_fontSig = 0;         // Fonts_KfSig() of the applied glyph set

    // css px (0 = default) -> vgui tall at screen scale s. vgui tall is the GDI cell height;
    // font-size is the em, and Stratum2's win ascent + descent is 1.2 em
    int FontTall(float s)
    {
        const int px = g_fontKfSize > 0 ? g_fontKfSize : kFontKfDefSize;
        return (std::max)(1, (int)lroundf(px * 1.2f * s));
    }

    bool EnsureFont(int tall)
    {
        static bool s_registered = false;
        if (!s_registered)
        {
            DWORD n = 0;
            s_registered = true;   // once: AddFontMemResourceEx copies the data, process-private
            if (!AddFontMemResourceEx((void*)stratum2_bold, sizeof(stratum2_bold), nullptr, &n))
                DebugLog("killfeed csgo: Stratum2 register failed, GDI falls back");
        }
        // a video mode change drops glyph sets (CDraw::Start's same lost-font check)
        int w = 0, h = 0;
        if (s_font.m_dwFont)
            I::MatSystemSurface->GetTextSize(s_font.m_dwFont, L"A", w, h);
        const uint32_t sig = Fonts_KfSig();
        if (!s_font.m_dwFont || s_font.m_nTall != tall || !h || sig != s_fontSig)
        {
            if (!s_font.m_dwFont)
                s_font.m_dwFont = I::MatSystemSurface->CreateFont();
            strncpy_s(s_fontName, g_fontKfName[0] ? g_fontKfName : kFontKfDefName, _TRUNCATE);
            const int flagMask = g_fontKfSize > 0 ? FontFlagsNorm(g_fontKfFlags) : kFontKfDefFlags;   // size 0 = whole default face
            if (g_fontKfSize <= 0)
                strcpy_s(s_fontName, kFontKfDefName);
            const VguiFace face = FontFlagsToVgui(flagMask);
            s_font.m_nTall   = tall;
            s_font.m_nWeight = face.weight;
            s_font.m_nFlags  = face.flags | FONTFLAG_ADDITIVE;   // .DeathNoticeContent -s2-mix-blend-mode: additive
            I::MatSystemSurface->SetFontGlyphSet(s_font.m_dwFont, s_font.m_szName, tall, s_font.m_nWeight, 0, 0, s_font.m_nFlags);
            s_fontSig = sig;
            DebugLog("killfeed csgo: font '%s' tall %d weight %d flags 0x%x", s_fontName, tall, face.weight, s_font.m_nFlags);
        }
        return s_font.m_dwFont != 0;
    }
}

namespace
{
    Color_t Rgba(byte r, byte g, byte b, float a)
    {
        return { r, g, b, (byte)(a <= 0.f ? 0 : (a >= 1.f ? 255 : (int)(a * 255.f + 0.5f))) };
    }

    // .DeathNoticeCTColor / TColor. TF2's team numbers line up with CS:GO's: 2 = RED = T, 3 = BLU = CT
    Color_t NameColor(int team, float a)
    {
        if (team == 3) return Rgba(0x6f, 0x9c, 0xe6, a);
        if (team == 2) return Rgba(0xea, 0xbe, 0x54, a);
        return Rgba(255, 255, 255, a);
    }

    float TextW(const wchar_t* t)
    {
        int w = 0, h = 0;
        I::MatSystemSurface->GetTextSize(s_font.m_dwFont, t, w, h);
        return float(w);
    }

    float IconW(const CHudTexture* t, float h)
    {
        return (t && t->Height() > 0) ? t->Width() * h / t->Height() : 0.f;
    }

    // one row. s = panorama px -> screen px; a = BGBorder / Content opacity
    void DrawNotice(const Notice& n, float right, float y, float s, float innerH, float a)
    {
        const float m = 2.f * s;            // label margin 2px l/r, icon margin 2px l/r
        const float iconH = 24.f * s;
        static const wchar_t* kPlus = L"+";

        float w = 20.f * s;                 // padding 10px l/r
        if (n.domination) w += IconW(n.domination, iconH) + 2.f * m;
        w += TextW(n.attacker) + 2.f * m;
        if (n.assist)     w += TextW(kPlus) + TextW(n.assister) + 4.f * m;
        if (n.weapon)     w += IconW(n.weapon, iconH) + 2.f * m;
        if (n.suicide)    w += IconW(n.skull, iconH) + 2.f * m;
        w += TextW(n.victim) + 2.f * m;

        const float h  = (6.f + innerH + 3.f) * s;   // padding 6px top, 3px bottom
        const int   x0 = (int)lroundf(right - w), y0 = (int)lroundf(y);
        const int   wi = (int)lroundf(w), hi = (int)lroundf(h);
        const int   r  = (int)lroundf(3.f * s);      // border-radius 3px

        // background
        if (n.isKiller)
        {
            const int b = (std::max)(1, (int)lroundf(2.f * s));   // border: 2px solid #e10000
            for (int i = 0; i < b; i++)
                H::Draw.LineRoundRect(x0 + i, y0 + i, wi - 2 * i, hi - 2 * i, (std::max)(r - i, 0), Rgba(0xe1, 0, 0, a));
            H::Draw.FillRoundRect(x0 + b, y0 + b, wi - 2 * b, hi - 2 * b, (std::max)(r - b, 0), Rgba(0, 0, 0, 0.65f * a));
        }
        else if (n.isVictim)
        {
            H::Draw.FillRoundRect(x0, y0, wi, hi, r, Rgba(0xa8, 0x13, 0x13, 0.7f * a));
            // box-shadow: inset #e10000e6 0px 0px 1px -- a 1px blur, about half of it lands inside
            H::Draw.LineRoundRect(x0, y0, wi, hi, r, Rgba(0xe1, 0, 0, 0.9f * 0.5f * 0.7f * a));
        }
        else
            H::Draw.FillRoundRect(x0, y0, wi, hi, r, Rgba(0, 0, 0, 0.65f * a));

        // content, flow-children: right
        float x = x0 + 10.f * s;
        const float yText = y0 + 6.f * s;
        // vertical-align: middle with margin -2px 0 0: the 22px margin box centred, icon 2px above it
        const float yIcon = yText + (innerH * s - 22.f * s) * 0.5f - 2.f * s;
        const Color_t white = Rgba(255, 255, 255, a);

        auto label = [&](const wchar_t* t, Color_t c) {
            x += m;
            H::Draw.String(s_font, (int)lroundf(x), (int)lroundf(yText), c, ALIGN_TOPLEFT, t);
            x += TextW(t) + m;
        };
        auto icon = [&](CHudTexture* t, CHudTexture* under) {
            x += m;
            const float sc = (t && t->Height() > 0) ? iconH / t->Height() : 1.f;
            const float iw = IconW(t, iconH);
            // crit glow: TF2 draws it at the weapon icon's own rect (DrawSelf(x, y, iconActualWide, iconTall))
            if (under && !under->bRenderUsingFont && under->textureId != -1)
            {
                I::MatSystemSurface->DrawSetTexture(under->textureId);
                I::MatSystemSurface->DrawSetColor(white.r, white.g, white.b, white.a);
                I::MatSystemSurface->DrawTexturedSubRect((int)x, (int)yIcon, (int)(x + iw), (int)(yIcon + iconH),
                    under->texCoords[0], under->texCoords[1], under->texCoords[2], under->texCoords[3]);
            }
            H::Draw.DrawHudTexture(x, yIcon, sc, t, white);
            x += iw + m;
        };

        if (n.domination) icon(n.domination, nullptr);
        label(n.attacker, NameColor(n.attackerTeam, a));
        if (n.assist)
        {
            label(kPlus, white);
            label(n.assister, NameColor(n.assisterTeam, a));
        }
        if (n.weapon)  icon(n.weapon, n.crit);
        if (n.suicide) icon(n.skull, nullptr);
        label(n.victim, NameColor(n.victimTeam, a));
    }
}

// hooks.cpp IEngineVGui::Paint after the game's panels (above the csgo scope; under the pause
// menu while that is up), inside H::Draw.Start/End (surface pass, recorded)
void KillfeedCsgo_Paint()
{
    if (g_eject)
        return;
    std::lock_guard<std::mutex> lk(s_mtx);
    if (!g_killfeedCsgo)
    {
        s_notices.clear();
        return;
    }

    const float now = I::GlobalVars->curtime;
    if (now < s_lastTime)   // map change / demo seek: LevelInit's ClearNotices
        s_notices.clear();
    s_lastTime = now;

    // ProcessInput: start the fade after Lifetime * LifetimeMod, delete when it is done
    for (auto it = s_notices.begin(); it != s_notices.end();)
    {
        if (it->fadeStart < 0.f && now - it->spawn > kLifetime * it->lifeMod)
            it->fadeStart = now;
        if (it->fadeStart >= 0.f && now - it->fadeStart >= kFadeOutTime)
            it = s_notices.erase(it);
        else
            ++it;
    }
    if (s_notices.empty())
        return;

    // ShouldDraw: cl_drawhud (cl_drawhud_force_deathnotices is CS:GO only). ProcessInput above
    // keeps running while hidden, like SetActive(false)
    static ConVar* cl_drawhud = H::ConVars.FindVar("cl_drawhud");
    if (cl_drawhud && !cl_drawhud->GetBool())
        return;

    const int sw = H::Draw.m_nScreenW, sh = H::Draw.m_nScreenH;
    if (sw <= 0 || sh <= 0)
        return;
    const float s = sh / 1080.f;   // panorama lays out at 1080 tall
    if (!EnsureFont(FontTall(s)))
        return;

    int tw = 0, th = 0;
    I::MatSystemSurface->GetTextSize(s_font.m_dwFont, L"A", tw, th);
    // label box = line + margin-bottom 2px; icon margin box = 24 - 2. Content height is the max
    const float innerH = (std::max)(th / s + 2.f, 22.f);
    const float rowH   = 6.f + innerH + 3.f;
    const float right  = sw - 10.f * s;   // CSGOHudDeathNotice padding-right 10px, top-right

    float y = 72.f;                        // CSGOHudDeathNotice margin-top 72px
    for (Notice& n : s_notices)
    {
        y += 3.f;                          // .DeathNotice margin-top 3px
        if (n.yTo < 0.f) { n.yFrom = n.yTo = y; n.yT0 = now; }   // takes an Available slot in place
        else if (n.yTo != y)                                      // someone above was deleted
        {
            n.yFrom = n.yFrom + (n.yTo - n.yFrom) * EaseOut((now - n.yT0) / kMoveTime);
            n.yTo   = y;
            n.yT0   = now;
        }
        const float yDraw = n.yFrom + (n.yTo - n.yFrom) * EaseOut((now - n.yT0) / kMoveTime);
        y += rowH;

        float a = EaseOut((now - n.spawn) / kFadeOutTime);       // .Available opacity 0 -> 1
        if (n.fadeStart >= 0.f)
            a *= 1.f - EaseOut((now - n.fadeStart) / kFadeOutTime);   // .FadeOut opacity -> 0
        if (a <= 0.f)
            continue;
        DrawNotice(n, right, yDraw * s, s, innerH, a);
    }
}

void KillfeedCsgo_OnEvent(void* pEvent)
{
    if (!g_killfeedCsgo || g_eject || !pEvent)
        return;
    Notice n;
    if (!BuildNoticeSafe(pEvent, n))
        return;
    std::lock_guard<std::mutex> lk(s_mtx);
    if ((int)s_notices.size() >= kMaxPending)   // no Available panel left: delete the oldest outright
        s_notices.erase(s_notices.begin());
    s_notices.push_back(n);
}
