#include "sdk.h"
#include "../menu/cfg/vars_teamcounter.h"
#include "../hud/teamcounter_layout.h"
#include "../hud/teamcounter_assets.h"
#include "../../dependencies/fonts/stratum2_bold_otf.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push, 0)
#include "../../dependencies/nanosvg/nanosvg.h"       // implementation compiled in weaponsel.cpp
#include "../../dependencies/nanosvg/nanosvgrast.h"
#pragma warning(pop)

// ============================================================================================
// csgo team counter, 1:1 port of CS:GO's panorama CCSGO_HudTeamCounter (csgosrc2019
// cstrike15/panorama/hud/csgo_hudteamcounter.cpp + the shipped layout/hud/hudteamcounter.xml and
// styles/hud/hudteamcounter.css from code.pbin): top centre, round timer over the two team scores,
// a player avatar per team member either side. Layout math in hud/teamcounter_layout.h (tested).
//   BLU = CT, left. RED = T, right. Scores = CTeam m_iScore (what TF2's match hud shows).
//   Comp layout (SetModeNormalTen: 5 large avatars a side, ui-scale 88%) while neither team has
//   more than 5 players, casual (SetModeNormal: 2 rows of small avatars, 12 a side) past that.
//   Avatar: steam medium avatar, team border, white outline on the local player, grey 3px on the
//   spectated one, health bar (teammates only unless spectating / dead with mp_forcecamera 0),
//   skull when dead (red for teammates), domination / nemesis icons. Bots: CS:GO's default
//   alivebg CT/T picture. Assets straight out of CS:GO: tools/gen_teamcounter.py.
//   Timer: the round timer TF2's hud shows (objective resource m_iTimerToShowInHUD), in koth the
//   running koth clock; red under 11 s while the round runs.
//   Text: embedded Stratum2 Bold .otf (dependencies/fonts/stratum2_bold_otf.h), same face as the
//   weapon hud. CS:GO's monodigit cut only differs in tabular digits.
//
// Drawn in hooks.cpp IEngineVGui::Paint after the game's panels (surface pass: startmovie records
// it). TF2's HudMatchStatus (timer, scores, TeamStatus panels), the tournament timer and the koth
// timers are not painted meanwhile (world_fx3.cpp WorldFx3_SkipPanel). Menu: visuals > screen.
//
// Left out, with why:
//   bomb status       CS:GO c4
//   player colours /  CS:GO comp teammate colours, TF2 has none
//   letters
//   speaking icon     needs CVoiceStatus (no sig in this project)
//   alive counters    cl_hud_playercount_showcount, off by default in CS:GO
//   join panel        "now playing as CT" popup, not part of the bar
//   gun game modes    arsenal progress / leader
//   waiting for       CS:GO blanks the timer in warmup; TF2's own waiting clock is hidden here, so
//   players           the timer keeps showing it instead
// ============================================================================================

extern std::atomic<bool> g_eject;        // dllmain.cpp
void DebugLog(const char* fmt, ...);     // dllmain.cpp
bool Avatar_FetchRGBA(uint64_t steamid64, std::vector<uint8_t>& rgba, int& w, int& h);   // hud/avatar_cache.cpp

namespace
{
    constexpr int kTeamRed = 2, kTeamBlu = 3;
    constexpr int kHideAll = 1 << 2;            // HIDEHUD_ALL
    constexpr int kObsInEye = 4, kObsChase = 5;
    constexpr int kRoundRunning = 4;            // GR_STATE_RND_RUNNING
    constexpr uint64_t kSteam64Base = 76561197960265728ull;

    // css colours
    constexpr uint32_t kTimerBg   = 0x00000099u;   // #Timer, .GameScore__BGL/R background-color
    constexpr uint32_t kBgCT      = 0x7289A3FFu;   // .AvatarImageCT__Color
    constexpr uint32_t kBgT       = 0x916D41FFu;   // .AvatarImageT__Color
    constexpr uint32_t kImgL      = 0x7F7F7FFFu;   // .AvatarL__Internal background-color (avatar not in yet)
    constexpr uint32_t kImgS      = 0x000000FFu;   // .AvatarS__PlayerImage
    constexpr uint32_t kHpNormal  = 0xCACACAFFu;   // .Avatar__HealthBar--Normal
    constexpr uint32_t kHpRed     = 0xEA5151FFu;   // .Avatar__HealthBar--Red
    constexpr uint32_t kTimerRedC = 0xFF0000FFu;   // .teamcounter_red_timer color: red

    Color_t Rgba(uint32_t c, float a = 1.f)
    {
        return { (byte)(c >> 24), (byte)(c >> 16), (byte)(c >> 8), (byte)std::clamp((int)lroundf((c & 0xFF) * a), 0, 255) };
    }

    template <class T> T& At(void* p, int off) { return *reinterpret_cast<T*>(uintptr_t(p) + off); }

    // ---- netvars, resolved once ------------------------------------------------------------
    struct Offsets
    {
        int teamNum, score, connected, team, obsMode, obsTarget, hideHud, dominated, dominatingMe,
            timerToShow, timerPaused, timeRemaining, timerEnd, timerDisabled, kothRed, kothBlu;
        bool ok;
    } o{};

    bool InitOffsets()
    {
        static bool done = false;
        if (done) return o.ok;
        done = true;
        o.teamNum       = U::NetVars.GetNetVar("CTFTeam", "m_iTeamNum");
        o.score         = U::NetVars.GetNetVar("CTFTeam", "m_iScore");
        o.connected     = U::NetVars.GetNetVar("CTFPlayerResource", "m_bConnected");
        o.team          = U::NetVars.GetNetVar("CTFPlayerResource", "m_iTeam");
        o.obsMode       = U::NetVars.GetNetVar("CBasePlayer", "m_iObserverMode");
        o.obsTarget     = U::NetVars.GetNetVar("CBasePlayer", "m_hObserverTarget");
        o.hideHud       = U::NetVars.GetNetVar("CBasePlayer", "m_iHideHUD");
        o.dominated     = U::NetVars.GetNetVar("CTFPlayer", "m_bPlayerDominated");
        o.dominatingMe  = U::NetVars.GetNetVar("CTFPlayer", "m_bPlayerDominatingMe");
        o.timerToShow   = U::NetVars.GetNetVar("CTFObjectiveResource", "m_iTimerToShowInHUD");
        o.timerPaused   = U::NetVars.GetNetVar("CTeamRoundTimer", "m_bTimerPaused");
        o.timeRemaining = U::NetVars.GetNetVar("CTeamRoundTimer", "m_flTimeRemaining");
        o.timerEnd      = U::NetVars.GetNetVar("CTeamRoundTimer", "m_flTimerEndTime");
        o.timerDisabled = U::NetVars.GetNetVar("CTeamRoundTimer", "m_bIsDisabled");
        o.kothRed       = U::NetVars.GetNetVar("CTFGameRulesProxy", "m_hRedKothTimer");
        o.kothBlu       = U::NetVars.GetNetVar("CTFGameRulesProxy", "m_hBlueKothTimer");
        // m_iTeamNum is DT_Team's first prop: offset 0 is not a failure there
        o.ok = o.score && o.connected && o.team;
        DebugLog("teamcounter: netvars team %d score %d conn %d pteam %d obs %d/%d dom %d/%d timer %d/%d/%d/%d/%d koth %d/%d",
                 o.teamNum, o.score, o.connected, o.team, o.obsMode, o.obsTarget, o.dominated, o.dominatingMe,
                 o.timerToShow, o.timerPaused, o.timeRemaining, o.timerEnd, o.timerDisabled, o.kothRed, o.kothBlu);
        return o.ok;
    }

    // ---- the few non-player entities, found by class and kept by index ---------------------
    CBaseEntity* EntOf(int idx, ETFClassID id)
    {
        if (idx <= 0) return nullptr;
        auto p = I::ClientEntityList->GetClientEntity(idx);
        if (!p) return nullptr;
        auto e = p->As<CBaseEntity>();
        return e->GetClassID() == id ? e : nullptr;
    }

    struct Ents { int resource = 0, objective = 0, team[4] = {}; } s_ents;
    float s_nextScan = 0.f;

    void Scan(float real)
    {
        const bool have = EntOf(s_ents.resource, ETFClassID::CTFPlayerResource) &&
                          EntOf(s_ents.team[kTeamRed], ETFClassID::CTFTeam) && EntOf(s_ents.team[kTeamBlu], ETFClassID::CTFTeam);
        if ((have && EntOf(s_ents.objective, ETFClassID::CTFObjectiveResource)) || real < s_nextScan)
            return;
        s_nextScan = real + 1.f;   // a map without an objective resource rescans once a second, not every frame
        s_ents = {};
        const int last = I::ClientEntityList->GetHighestEntityIndex();
        for (int n = I::EngineClient->GetMaxClients() + 1; n <= last; n++)
        {
            auto p = I::ClientEntityList->GetClientEntity(n);
            if (!p) continue;
            auto e = p->As<CBaseEntity>();
            switch (e->GetClassID())
            {
            case ETFClassID::CTFPlayerResource:    s_ents.resource = n; break;
            case ETFClassID::CTFObjectiveResource: s_ents.objective = n; break;
            case ETFClassID::CTFTeam:
            {
                const int t = At<int>(e, o.teamNum);
                if (t >= 0 && t < 4) s_ents.team[t] = n;
                break;
            }
            default: break;
            }
        }
    }

    int HandleIndex(uint32_t h)
    {
        const CBaseHandle bh(h);
        return bh.IsValid() ? bh.GetEntryIndex() : 0;
    }

    // ---- timer (CTeamRoundTimer::GetTimeRemaining) -----------------------------------------
    bool TimerLeft(float now, float& left)
    {
        auto timer = [&](int idx) -> CBaseEntity* {
            auto e = EntOf(idx, ETFClassID::CTeamRoundTimer);
            return e && !e->IsDormant() && !(o.timerDisabled && At<bool>(e, o.timerDisabled)) ? e : nullptr;
        };
        auto paused = [&](CBaseEntity* e) { return o.timerPaused && At<bool>(e, o.timerPaused); };
        auto read = [&](CBaseEntity* e) {
            left = paused(e) ? At<float>(e, o.timeRemaining) : At<float>(e, o.timerEnd) - now;
            return true;
        };

        // koth: TF2 draws two koth clocks (CTFHudMatchStatus keeps its timer hidden there); one
        // clock here = the one running, else the hud timer (point unlock), else blue's
        CBaseEntity* kothR = nullptr, * kothB = nullptr;
        if (CTFGameRules* gr = I::TFGameRules(); gr && o.kothRed && o.kothBlu)
        {
            kothR = timer(HandleIndex(At<uint32_t>(gr, o.kothRed)));
            kothB = timer(HandleIndex(At<uint32_t>(gr, o.kothBlu)));
            if (kothR && !paused(kothR)) return read(kothR);
            if (kothB && !paused(kothB)) return read(kothB);
        }
        if (auto obj = EntOf(s_ents.objective, ETFClassID::CTFObjectiveResource); obj && o.timerToShow)
            if (auto e = timer(At<int>(obj, o.timerToShow)))
                return read(e);
        if (kothB) return read(kothB);
        if (kothR) return read(kothR);
        return false;
    }

    // ---- textures ---------------------------------------------------------------------------
    int RgbaTex(const uint8_t* px, int w, int h)
    {
        const int id = I::MatSystemSurface->CreateNewTextureID(true);
        I::MatSystemSurface->DrawSetTextureRGBAEx(id, px, w, h, IMAGE_FORMAT_RGBA8888);
        return id;
    }

    int s_texBgCT = -1, s_texBgT = -1;

    struct AvTex { int tex = -1; float nextTry = 0.f; };
    // ponytail: never shrinks (64x64 a player ever seen this session); drop entries on map change
    // if a long session of pubs ever shows in vram
    std::unordered_map<uint64_t, AvTex> s_avatars;

    int AvatarTex(uint64_t xuid, float real)
    {
        AvTex& a = s_avatars[xuid];
        if (a.tex != -1 || real < a.nextTry) return a.tex;
        a.nextTry = real + 1.f;   // not downloaded yet: Steam was asked, look again in a second
        std::vector<uint8_t> px;
        int w = 0, h = 0;
        if (!Avatar_FetchRGBA(xuid, px, w, h)) return -1;
        a.tex = RgbaTex(px.data(), w, h);
        return a.tex;
    }

    // svg icons: rasterised at the on-screen size (square, the 32x32 viewBox scaled to fit); a
    // vgui procedural texture keeps its first size, so a new size gets a new id
    struct Svg
    {
        const char* src;
        NSVGimage* img = nullptr;
        int tex = -1, side = 0;
    };
    Svg s_skull{ tc_assets::kSvgSkull }, s_dominated{ tc_assets::kSvgDominated }, s_nemesis{ tc_assets::kSvgNemesis };
    NSVGrasterizer* s_rast = nullptr;

    int SvgTex(Svg& s, int side)
    {
        if (side < 1) return -1;
        if (s.tex != -1 && s.side == side) return s.tex;
        if (!s.img)
        {
            std::string buf = s.src;   // nsvgParse writes into its input
            s.img = nsvgParse(buf.data(), "px", 96.f);
            if (!s.img || s.img->width <= 0.f) { DebugLog("teamcounter: svg parse failed"); return -1; }
        }
        if (!s_rast) s_rast = nsvgCreateRasterizer();
        if (!s_rast) return -1;
        std::vector<unsigned char> px((size_t)side * side * 4, 0);
        const float k = side / (std::max)(s.img->width, s.img->height);
        nsvgRasterize(s_rast, s.img, (side - s.img->width * k) * 0.5f, (side - s.img->height * k) * 0.5f, k,
                      px.data(), side, side, side * 4);
        if (s.tex != -1) I::MatSystemSurface->DeleteTextureByID(s.tex);
        s.tex = RgbaTex(px.data(), side, side);
        s.side = side;
        return s.tex;
    }

    // ---- drawing ----------------------------------------------------------------------------
    void Fill(float x0, float y0, float x1, float y1, Color_t c)
    {
        const int X0 = (int)lroundf(x0), Y0 = (int)lroundf(y0), X1 = (int)lroundf(x1), Y1 = (int)lroundf(y1);
        if (X1 <= X0 || Y1 <= Y0 || !c.a) return;
        I::MatSystemSurface->DrawSetColor(c.r, c.g, c.b, c.a);
        I::MatSystemSurface->DrawFilledRect(X0, Y0, X1, Y1);
    }

    // css border: drawn inside the box
    void Ring(float x0, float y0, float x1, float y1, float w, Color_t c)
    {
        const float b = (std::max)(1.f, std::round(w));
        Fill(x0, y0, x1, y0 + b, c);
        Fill(x0, y1 - b, x1, y1, c);
        Fill(x0, y0 + b, x0 + b, y1 - b, c);
        Fill(x1 - b, y0 + b, x1, y1 - b, c);
    }

    void Image(int tex, float x0, float y0, float x1, float y1, Color_t c = { 255, 255, 255, 255 })
    {
        if (tex == -1 || !c.a) return;
        I::MatSystemSurface->DrawSetColor(c.r, c.g, c.b, c.a);
        I::MatSystemSurface->DrawSetTexture(tex);
        I::MatSystemSurface->DrawTexturedRect((int)lroundf(x0), (int)lroundf(y0), (int)lroundf(x1), (int)lroundf(y1));
    }

    // an Image panel w x h at (x, y): the square svg fitted, centred (scaling stretch-to-fit-preserve-aspect)
    void Icon(Svg& s, float x, float y, float w, float h, Color_t c)
    {
        const float side = (std::min)(w, h);
        const float ix = x + (w - side) * 0.5f, iy = y + (h - side) * 0.5f;
        Image(SvgTex(s, (int)lroundf(side)), ix, iy, ix + side, iy + side, c);
    }

    // ---- font: embedded Stratum2 Bold .otf (CSGOHudTeamCounter Label is Stratum2 Bold Monodigit,
    // the decoded panorama vfonts drew off through GDI), css px -> vgui tall. One HFont per tall
    // (weaponsel.cpp EnsureFont: vgui glyph caches are keyed by handle)
    Font_t s_font = { kStratum2Family, 0, 0, kStratum2Weight, 0 };
    std::vector<std::pair<int, unsigned long>> s_fontByTall;

    bool EnsureFont(int px, float s)
    {
        static bool s_logged = false;
        if (!s_logged)
        {
            s_logged = true;
            if (!Stratum2Otf_Register()) DebugLog("teamcounter: Stratum2 otf register failed, GDI falls back");
        }
        const int tall = (std::max)(1, (int)lroundf(px * 1.2f * s));   // ascent + descent = 1.2 em
        int w = 0, h = 0;
        if (s_font.m_dwFont) I::MatSystemSurface->GetTextSize(s_font.m_dwFont, L"0", w, h);   // video mode change drops glyphs
        if (s_font.m_dwFont && s_font.m_nTall == tall && h)
            return true;

        s_font.m_nTall = tall;
        s_font.m_nFlags = FONTFLAG_ANTIALIAS;
        s_font.m_dwFont = 0;
        for (const auto& [t, f] : s_fontByTall)
            if (t == tall) s_font.m_dwFont = f;
        const bool fresh = !s_font.m_dwFont;
        if (fresh)
        {
            s_font.m_dwFont = I::MatSystemSurface->CreateFont();
            if (!s_font.m_dwFont) return false;
            s_fontByTall.push_back({ tall, s_font.m_dwFont });
        }
        int w2 = 0, h2 = 0;
        if (!fresh) I::MatSystemSurface->GetTextSize(s_font.m_dwFont, L"0", w2, h2);
        if (fresh || !h2)
        {
            I::MatSystemSurface->SetFontGlyphSet(s_font.m_dwFont, s_font.m_szName, tall, s_font.m_nWeight, 0, 0, s_font.m_nFlags);
            DebugLog("teamcounter: font %lu tall %d", s_font.m_dwFont, tall);
        }
        return true;
    }

    // a Label centred in [x0, x1] x [y0, y1], letter-spacing in screen px
    void Text(const char* t, float x0, float y0, float x1, float y1, float spacing, Color_t c)
    {
        wchar_t ch[2] = {};
        float total = 0.f;
        int th = 0, n = 0;
        for (const char* p = t; *p; ++p, ++n)
        {
            ch[0] = (wchar_t)(unsigned char)*p;
            int w = 0;
            I::MatSystemSurface->GetTextSize(s_font.m_dwFont, ch, w, th);
            total += w;
        }
        if (!n) return;
        total += spacing * (n - 1);
        float x = (x0 + x1 - total) * 0.5f;
        const int y = (int)lroundf(tc::DigitTop(y0, y1, (float)th));
        for (const char* p = t; *p; ++p)
        {
            ch[0] = (wchar_t)(unsigned char)*p;
            int w = 0, h = 0;
            I::MatSystemSurface->GetTextSize(s_font.m_dwFont, ch, w, h);
            H::Draw.String(s_font, (int)lroundf(x), y, c, ALIGN_TOPLEFT, ch);
            x += w + spacing;
        }
    }

    // ---- players ------------------------------------------------------------------------------
    struct Slot
    {
        uint64_t xuid;
        int pct;
        bool alive, local, spectated, dominated, nemesis, showHealth;
    };

    void DrawAvatar(const Slot& p, bool large, bool left, int slot, float cx, float top, float s, int localSide, float real)
    {
        const tc::Box b = tc::AvatarBox(large, left, slot);
        const float u = b.u * s;   // screen px per css px inside the avatar (ui-scale x hud scale)
        const float x0 = cx + b.x * s, y0 = top + b.y * s, x1 = x0 + b.w * s, y1 = y0 + b.h * s;

        if (!p.alive)
        {
            // ImageBG hidden, #Skull shown: large 42x42 at x 7 + margin-left 2, y 6 + margin-top 2;
            // small 20x20 centred. wash-color-fast keeps the alpha, opacity .7
            const Color_t c = Rgba(tc::SkullRgba(tc::SkullFor(left, localSide)));
            if (large) Icon(s_skull, x0 + 9.f * u, y0 + 8.f * u, 42.f * u, 42.f * u, c);
            else       Icon(s_skull, x0 + (b.w - 20.f) * 0.5f * u, y0 + (b.h - 20.f) * 0.5f * u, 20.f * u, 20.f * u, c);
            return;
        }

        const tc::Border bd = tc::BorderFor(large, left, p.local, p.spectated);
        const float bw = (std::max)(1.f, std::round(bd.w * u)), m = std::round(bd.imgMargin * u);
        Fill(x0, y0, x1, y1, Rgba(left ? kBgCT : kBgT));

        if (p.xuid)
        {
            const float ix0 = x0 + bw + m, iy0 = y0 + bw + m, ix1 = x1 - bw - m, iy1 = y1 - bw - m;
            Fill(ix0, iy0, ix1, iy1, Rgba(large ? kImgL : kImgS));
            Image(AvatarTex(p.xuid, real), ix0, iy0, ix1, iy1);
        }
        else
        {
            // no steam id (bot): AvatarL/S__Default--CT/T background-image, 92% 92%, centred
            const float w = (x1 - x0) * 0.92f, h = (y1 - y0) * 0.92f, mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;
            Image(left ? s_texBgCT : s_texBgT, mx - w * 0.5f, my - h * 0.5f, mx + w * 0.5f, my + h * 0.5f);
        }

        // CSGOAvatarHealthBar: bottom of the image box, width = health %, over the avatar
        if (p.showHealth && p.pct > 0)
        {
            const float hh = (large ? 4.f : 3.f) * u, w = (x1 - x0 - 2.f * bw) * p.pct / 100.f;
            Fill(x0 + bw, y1 - bw - hh, x0 + bw + w, y1 - bw, Rgba(tc::HealthRed(p.pct) ? kHpRed : kHpNormal));
        }
        Ring(x0, y0, x1, y1, bw, Rgba(bd.rgba));

        // reparented into #Visible on top: dominated wins over nemesis
        if (p.dominated)
        {
            if (large) Icon(s_dominated, x0 + 3.f * u, y0 + 5.f * u, 54.f * u, 54.f * u, { 255, 255, 255, 255 });
            else       Icon(s_dominated, x0 + 1.f * u, y0 - 2.f * u, 30.f * u, 30.f * u, { 255, 255, 255, 255 });
        }
        else if (p.nemesis)
        {
            if (large) Icon(s_nemesis, x0 + 10.f * u, y0 + 8.f * u, 42.f * u, 42.f * u, { 255, 255, 255, 179 });
            else       Icon(s_nemesis, x0 + 13.f * u, y0 - 1.f * u, 16.f * u, 28.f * u, { 255, 255, 255, 255 });
        }
    }
}

// hooks.cpp IEngineVGui::Paint, inside H::Draw.Start/End (surface pass, recorded)
void TeamCounter_Paint()
{
    if (g_eject || !g_tcEnable || !I::EngineClient->IsInGame() || I::EngineClient->IsLevelMainMenuBackground())
        return;
    if (!InitOffsets())
        return;

    static ConVar* cl_drawhud = H::ConVars.FindVar("cl_drawhud");
    static ConVar* hidehud = H::ConVars.FindVar("hidehud");
    static ConVar* mp_forcecamera = H::ConVars.FindVar("mp_forcecamera");
    if (cl_drawhud && !cl_drawhud->GetBool()) return;

    const int iLocal = I::EngineClient->GetLocalPlayer();
    auto pLocalEnt = I::ClientEntityList->GetClientEntity(iLocal);
    if (!pLocalEnt) return;
    auto pLocal = pLocalEnt->As<CTFPlayer>();
    int hide = o.hideHud ? At<int>(pLocal, o.hideHud) : 0;
    if (hidehud && hidehud->GetInt()) hide = hidehud->GetInt();
    if (hide & kHideAll) return;

    const float real = I::GlobalVars->realtime, now = I::GlobalVars->curtime;
    Scan(real);
    auto res = EntOf(s_ents.resource, ETFClassID::CTFPlayerResource);
    if (!res) return;
    auto pr = res->As<CTFPlayerResource>();

    // ---- UpdateMiniScoreboard: players by index, per team, in slot order ----
    const int localTeam = At<int>(res, o.team + iLocal * 4);
    const int localSide = localTeam == kTeamBlu ? 0 : localTeam == kTeamRed ? 1 : -1;
    const bool localSpectator = localSide < 0;
    const bool localAlive = pr->m_bAlive(iLocal);
    int spectated = 0;
    if (o.obsMode && o.obsTarget)
    {
        const int mode = At<int>(pLocal, o.obsMode);
        if (mode == kObsInEye || mode == kObsChase) spectated = HandleIndex(At<uint32_t>(pLocal, o.obsTarget));
    }
    const int forceCamera = mp_forcecamera ? mp_forcecamera->GetInt() : 1;

    Slot left[tc::kSmallSlots], right[tc::kSmallSlots];
    int nLeft = 0, nRight = 0;
    for (int i = 1, n = I::EngineClient->GetMaxClients(); i <= n; i++)
    {
        if (!At<bool>(res, o.connected + i)) continue;
        const int team = At<int>(res, o.team + i * 4);
        if (team != kTeamRed && team != kTeamBlu) continue;
        const bool isLeft = team == kTeamBlu;
        int& count = isLeft ? nLeft : nRight;
        if (count >= tc::kSmallSlots) continue;   // this team is full: no more panels
        player_info_t info{};
        if (!I::EngineClient->GetPlayerInfo(i, &info) || info.ishltv || info.isreplay) continue;

        Slot& sl = (isLeft ? left : right)[count++];
        sl.xuid = (info.fakeplayer || !info.friendsID) ? 0 : kSteam64Base + info.friendsID;
        sl.alive = pr->m_bAlive(i);
        sl.local = i == iLocal;
        sl.spectated = i == spectated;
        sl.showHealth = tc::ShowHealth(localSpectator, team == localTeam, localAlive, forceCamera);
        sl.pct = sl.alive ? tc::HealthPct(pr->m_iHealth(i), pr->m_iMaxHealth(i)) : 0;
        sl.dominated = o.dominated && !sl.local && At<bool>(pLocal, o.dominated + i);
        sl.nemesis = o.dominatingMe && !sl.local && At<bool>(pLocal, o.dominatingMe + i);
    }

    const int sw = H::Draw.m_nScreenW, sh = H::Draw.m_nScreenH;
    if (sw <= 0 || sh <= 0) return;
    // panorama lays out at 1080 tall; the menu scale grows it from the top centre, the offset moves it
    const float base = sh / 1080.f;
    const float s = base * std::clamp(g_tcScale, 0.25f, 4.f);
    const float cx = sw * 0.5f, top = g_tcOffY * base;
    if (!EnsureFont(tc::kTimerPx, s)) return;
    if (s_texBgCT == -1)
    {
        s_texBgCT = RgbaTex(tc_assets::kAliveBgCT, tc_assets::kAliveBgCTW, tc_assets::kAliveBgCTH);
        s_texBgT = RgbaTex(tc_assets::kAliveBgT, tc_assets::kAliveBgTW, tc_assets::kAliveBgTH);
    }

    // ---- #ScoreAndTime: timer over the two scores ----
    const float bx0 = cx - tc::kCenterW * 0.5f * s, bx1 = cx + tc::kCenterW * 0.5f * s;
    const float ty0 = top + tc::kTop * s;
    Fill(bx0, ty0, bx1, ty0 + tc::kTimeH * s, Rgba(kTimerBg));
    float left_s = 0.f;
    if (TimerLeft(now, left_s))
    {
        const int secs = tc::TimerSeconds(left_s);
        CTFGameRules* gr = I::TFGameRules();
        const bool red = gr && gr->m_iRoundState() == kRoundRunning && tc::TimerRed(secs);
        char t[16];
        tc::TimerText(secs, t, sizeof t);
        // .teamcounter_timer margin-left 2px; digits centred in the whole #Timer box (tc::DigitTop)
        Text(t, bx0 + 2.f * s, ty0, bx1, ty0 + tc::kTimeH * s, tc::kTimerSpacing * s,
             Rgba(red ? kTimerRedC : 0xFFFFFFFFu));
    }
    const float sy0 = ty0 + tc::kScoreTop * s, sy1 = sy0 + tc::kScoreH * s;
    const float lx1 = bx0 + tc::kScoreW * s, rx0 = lx1 + 1.f * s, rx1 = rx0 + tc::kScoreW * s;
    Fill(bx0, sy0, lx1, sy1, Rgba(kTimerBg));
    Fill(rx0, sy0, rx1, sy1, Rgba(kTimerBg));
    auto score = [&](int team) {
        auto e = EntOf(s_ents.team[team], ETFClassID::CTFTeam);
        return e ? At<int>(e, o.score) : 0;
    };
    char sc[16];
    std::snprintf(sc, sizeof sc, "%d", score(kTeamBlu));
    Text(sc, bx0, sy0, lx1, sy1, 0.f, Rgba(tc::kColorCT));
    std::snprintf(sc, sizeof sc, "%d", score(kTeamRed));
    Text(sc, rx0, sy0, rx1, sy1, 0.f, Rgba(tc::kColorT));

    // ---- avatars ----
    const bool large = tc::UseLarge(nLeft, nRight);
    const int cap = tc::MaxSlots(large);
    for (int k = 0; k < (std::min)(nLeft, cap); k++)  DrawAvatar(left[k], large, true, k, cx, top, s, localSide, real);
    for (int k = 0; k < (std::min)(nRight, cap); k++) DrawAvatar(right[k], large, false, k, cx, top, s, localSide, real);
}
