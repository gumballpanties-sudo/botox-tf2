#include "spectator_list.h"
#include "../../render/render.h"   // RenderFadedGradientLine
#include "watermark_styles.h"      // Watermark_GamesenseFrame
#include "../misc/unitE_logic.h"
#include "avatar_cache.h"
#include "avatar_pure.h"   // n_avatar::FriendIdToSteam64
#include "gif_tex.h"
#include "../screen/blur_rect.h"
#include "../../dependencies/data/kamibebra_gif_data.h"   // kamibebra spectator-list gif bleed
#include "../../dependencies/data/airflow_data.h"        // airflow spectator-list + watermark icons
#include "../../dependencies/imgui/imgui_internal.h"     // GImGui->HoveredWindow: menu window over the panel eats the click
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>
#include <cmath>

// ==== netvars (self-resolved, read-only, never retried once found or ruled out) ===========
static int  s_offObsMode   = 0;   // 0 unresolved, -1 not found, >0 offset
static int  s_offObsTarget = 0;

static void ResolveSpecNetvars()
{
    if (s_offObsMode != 0 && s_offObsTarget != 0) return;
    RecvTable* t = FindNetworkTable("DT_BasePlayer");
    if (!t) return;
    const int m = FindPropOffset(t, "m_iObserverMode");
    const int h = FindPropOffset(t, "m_hObserverTarget");
    s_offObsMode   = m > 0 ? m : -1;
    s_offObsTarget = h > 0 ? h : -1;
    DebugLog("spectator list: m_iObserverMode=0x%X m_hObserverTarget=0x%X", s_offObsMode, s_offObsTarget);
}

// IVEngineClient::GetPlayerInfo, slot 8 -- same layout scoreboard.cpp already confirmed and
// reads (friendsID); duplicated here rather than exported, both readers are one line each.
struct SpecPlayerInfo_t
{
    char     name[32];
    int      userID;
    char     guid[33];
    uint32_t friendsID;
    char     friendsName[32];
    bool     fakeplayer;
    bool     ishltv;
    uint32_t customFiles[4];
    uint8_t  filesDownloaded;
};

static std::string PlayerName(int idx)
{
    if (!g_engine) return {};
    SpecPlayerInfo_t pi = {};
    if (!VCall<bool>(g_engine, 8, idx, &pi)) return {};
    return pi.name[0] ? pi.name : "";
}

// SEH leaf, no destructible locals (MSVC C2712: __try cannot live in a function that also
// needs stack unwinding for a C++ object -- CollectSpectators below has std::string/vector,
// so the raw read is split out the same way render.cpp's PsEngineW2S() is).
static __declspec(noinline) bool ReadObsState(void* pEnt, int offMode, int offTarget,
                                              int& outMode, unsigned int& outHandle)
{
    __try {
        outMode   = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pEnt) + offMode);
        outHandle = *reinterpret_cast<unsigned int*>(reinterpret_cast<uintptr_t>(pEnt) + offTarget);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Players currently spectating the local player this frame (botox's "local only" list
// mode -- this DLL has no all-players observer scan, so that's the one mode ported).
// Render-thread only, scanned fresh every call.
static std::vector<std::string> CollectSpectators()
{
    std::vector<std::string> out;
    if (!g_specEnable || !IsInGame()) return out;
    ResolveSpecNetvars();
    if (s_offObsMode < 0 || s_offObsTarget < 0) return out;

    const int localIdx = GetLocalPlayerIndex();
    if (localIdx < 1) return out;

    for (int i = 1; i <= 64; ++i)
    {
        if (i == localIdx) continue;
        void* pEnt = GetClientEntity(i);
        if (!pEnt) continue;

        int mode = 0;
        unsigned int handle = 0xFFFFFFFFu;
        if (!ReadObsState(pEnt, s_offObsMode, s_offObsTarget, mode, handle)) continue;

        if (mode == 0 /* OBS_MODE_NONE */) continue;
        if (n_unitE::DecodeEHandleIndex(handle) != localIdx) continue;

        std::string name = PlayerName(i);
        if (!name.empty()) out.push_back(std::move(name));
        if (out.size() >= 16) break;   // a panel is not a scoreboard
    }
    return out;
}

// Richer row for the avatar-drawing styles below (chillware/lobotomy/kamibebra/eyes/
// clarity_v2/bhopcheat/airflow/evolve/skebob) -- same scan as CollectSpectators, plus the
// steamid64 Avatar_Get needs. steamid64 is 0 for a bot (friendsID 0) or a missing read, and
// Avatar_Get already no-ops on 0.
struct SpecRow { std::string name; uint64_t steamid64 = 0; };

static std::vector<SpecRow> CollectSpectatorRows()
{
    std::vector<SpecRow> out;
    if (!g_specEnable || !IsInGame()) return out;
    ResolveSpecNetvars();
    if (s_offObsMode < 0 || s_offObsTarget < 0) return out;

    const int localIdx = GetLocalPlayerIndex();
    if (localIdx < 1) return out;

    for (int i = 1; i <= 64; ++i)
    {
        if (i == localIdx) continue;
        void* pEnt = GetClientEntity(i);
        if (!pEnt) continue;

        int mode = 0;
        unsigned int handle = 0xFFFFFFFFu;
        if (!ReadObsState(pEnt, s_offObsMode, s_offObsTarget, mode, handle)) continue;

        if (mode == 0) continue;
        if (n_unitE::DecodeEHandleIndex(handle) != localIdx) continue;

        SpecPlayerInfo_t pi = {};
        if (!VCall<bool>(g_engine, 8, i, &pi) || !pi.name[0]) continue;

        SpecRow row;
        row.name = pi.name;
        if (!pi.fakeplayer && pi.friendsID) row.steamid64 = n_avatar::FriendIdToSteam64(pi.friendsID);
        out.push_back(std::move(row));
        if (out.size() >= 16) break;
    }
    return out;
}

// botox's spectator_row_text(): "{name} -> {target}" when show_target is on, bare
// name otherwise. Target is always the local player here (the one mode this DLL
// collects).
static std::string RowText(const std::string& name, const std::string& localName)
{
    if (!g_specShowTarget || localName.empty()) return name;
    return name + " -> " + localName;
}

static void AnchorPos(ImGuiIO& io, float w, float h, int pos, float margin, float& x, float& y)
{
    x = margin; y = margin;
    switch (pos) {
    case WM_TOP_LEFT:      x = margin;                        y = margin; break;
    case WM_TOP_CENTER:    x = (io.DisplaySize.x - w) * 0.5f; y = margin; break;
    case WM_TOP_RIGHT:     x = io.DisplaySize.x - w - margin; y = margin; break;
    case WM_BOTTOM_LEFT:   x = margin;                        y = io.DisplaySize.y - h - margin; break;
    case WM_BOTTOM_CENTER: x = (io.DisplaySize.x - w) * 0.5f; y = io.DisplaySize.y - h - margin; break;
    case WM_BOTTOM_RIGHT:  x = io.DisplaySize.x - w - margin; y = io.DisplaySize.y - h - margin; break;
    }
    // drag offset (SpecDrag below), kept on screen so a stale offset after a g_specPos change can't lose the panel
    x = fmaxf(0.f, fminf(x + g_specOffX, io.DisplaySize.x - w));
    y = fmaxf(0.f, fminf(y + g_specOffY, io.DisplaySize.y - h));
    x = floorf(x); y = floorf(y);
}

static ImU32 PackColor(const float c[4])
{
    return IM_COL32((int)(c[0] * 255.f), (int)(c[1] * 255.f), (int)(c[2] * 255.f), (int)(c[3] * 255.f));
}

// style 0: botox's real default -- n_misc::impl_t::draw_spectator_list(), geometry 1:1:
// window bg #0A0A0A rounded 5, 25px #191919 header, accent faded line under it, verdana bold
// title, (50,50,50) border inset 1px, rows = text line + ItemSpacing.y, block centred, slide
// in 10px + fade at 6/s, whole panel fades at 4/s. Kept: AnchorPos instead of botox's
// draggable ImGui window, and no avatar (no avatar toggle in this DLL).
static void DrawDefault(ImDrawList* dl, ImGuiIO& io, const std::vector<std::string>& names, const std::string& localName)
{
    struct Row { std::string text; float progress = 0.f; bool seen = false; };
    static std::vector<Row> rows;
    static float animW = 0.f, targetW = 0.f, panel = 0.f;
    static bool snap = true;

    if (rows.empty()) snap = true;
    for (auto& r : rows) r.seen = false;
    for (auto& n : names) {
        std::string text = RowText(n, localName);
        auto it = std::find_if(rows.begin(), rows.end(), [&](const Row& r) { return r.text == text; });
        if (it == rows.end()) rows.push_back({ text, 0.f, true });
        else { it->seen = true; }
    }
    const float dt = io.DeltaTime;
    for (auto& r : rows) r.progress = std::clamp(r.progress + dt * 6.f * (r.seen ? 1.f : -1.f), 0.f, 1.f);
    rows.erase(std::remove_if(rows.begin(), rows.end(), [](const Row& r) { return !r.seen && r.progress <= 0.f; }), rows.end());

    panel = std::clamp(panel + dt * 4.f * ((g_specAlwaysShow || !rows.empty()) ? 1.f : -1.f), 0.f, 1.f);
    if (panel < 0.02f) { snap = true; return; }

    ImFont* fT = g_menuBold ? g_menuBold : ImGui::GetFont();   // verdana bold 11
    ImFont* fR = g_menuReg ? g_menuReg : ImGui::GetFont();     // verdana 11 = botox default font
    const ImGuiStyle& st = ImGui::GetStyle();
    constexpr float headerH = 25.f, padX = 8.f, bottomPad = 2.f, slide = 10.f;
    const float textH = fR->FontSize, rowH = textH + st.ItemSpacing.y;

    const ImVec2 titleSz = fT->CalcTextSizeA(fT->FontSize, FLT_MAX, 0.f, "spectators");
    float widest = 0.f, listH = 0.f;
    for (auto& r : rows) {
        widest = (std::max)(widest, fR->CalcTextSizeA(fR->FontSize, FLT_MAX, 0.f, r.text.c_str()).x);
        listH += rowH * r.progress;
    }
    if (!rows.empty() || g_specAlwaysShow) {
        targetW = (std::max)(titleSz.x + 40.f, widest + padX * 2.f);
        animW = snap ? targetW : animW + (targetW - animW) * std::clamp(dt * 12.f, 0.f, 1.f);
        snap = false;
    }
    const float h = headerH + bottomPad + listH + bottomPad;

    float x, y; AnchorPos(io, animW, h, g_specPos, 2.f, x, y);
    const float rnd = st.WindowRounding;

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + animW, y + h), IM_COL32(10, 10, 10, (int)(255.f * panel)), rnd);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + animW, y + headerH), IM_COL32(25, 25, 25, (int)(255.f * panel)), rnd, ImDrawFlags_RoundCornersTop);
    const ImColor acc = ImGui::GetColorU32(ImGuiCol_Accent);
    RenderFadedGradientLine(dl, ImVec2(x, y + headerH - 1.f), ImVec2(animW, 1.f), ImColor(acc.Value.x, acc.Value.y, acc.Value.z, panel));
    dl->AddText(fT, fT->FontSize, ImVec2(x + (animW - titleSz.x) * 0.5f, y + (headerH - titleSz.y) * 0.5f),
                ImColor(1.f, 1.f, 1.f, panel), "spectators");
    dl->AddRect(ImVec2(x + 1.f, y + 1.f), ImVec2(x + animW - 1.f, y + h - 1.f), IM_COL32(50, 50, 50, (int)(255.f * panel)), rnd);

    const float blockX = (std::max)(padX, (animW - widest) * 0.5f);
    float rowY = headerH + bottomPad;
    for (auto& r : rows) {
        const float rh = rowH * r.progress;
        if (r.progress > 0.f) {
            const float alpha = panel * r.progress;
            const float rowX = x + blockX - (1.f - r.progress) * slide;
            dl->PushClipRect(ImVec2(x, y + rowY), ImVec2(x + animW, y + rowY + rh), true);
            dl->AddText(fR, fR->FontSize, ImVec2(rowX, y + rowY + (rh - textH) * 0.5f),
                        ImColor(g_specColorOne[0], g_specColorOne[1], g_specColorOne[2], g_specColorOne[3] * alpha), r.text.c_str());
            dl->PopClipRect();
        }
        rowY += rh;
    }
}

// style 1: delusional -- draw_spectator_list_delusional(). Bare shadowed text rows,
// left or right of screen (g_specDelusionalLeft), stacked under the watermark when on
// the right and the watermark is showing (donor's "+6+19" header offset).
static void DrawDelusional(ImDrawList* dl, ImGuiIO& io, const std::vector<std::string>& names, const std::string& localName)
{
    if (names.empty()) return;
    ImFont* font = g_menuBold ? g_menuBold : ImGui::GetFont();   // botox verdana_bd_11
    const float fs = font->FontSize;
    const bool left = g_specDelusionalLeft;

    float y = 5.f;
    if (!left && g_wmEnable) y += 6.f + 19.f;

    const ImU32 local = PackColor(g_specColorOne);
    int row = 0;
    for (auto& n : names) {
        std::string text = RowText(n, localName);
        const float tw = font->CalcTextSizeA(fs, FLT_MAX, 0.f, text.c_str()).x;
        const float rx = left ? 6.f : io.DisplaySize.x - 6.f - tw;
        const float ry = y + 16.f * row++;
        dl->AddText(font, fs, ImVec2(rx + 1.f, ry + 1.f), IM_COL32(0, 0, 0, 255), text.c_str());
        dl->AddText(font, fs, ImVec2(rx, ry), local, text.c_str());
    }
}

// style 2: interwebz -- draw_spectator_list_interwebz(). Fixed-width retro beveled
// panel, "SPECTATOR LIST" title outlined, striped rows. Drag-to-reposition (donor's
// drag_placed_panel, a menu-only mouse interaction) skipped -- position follows
// g_specPos/g_specListWidth like every other style instead of a dragged x/y.
static void DrawInterwebz(ImDrawList* dl, ImGuiIO& io, const std::vector<std::string>& names, const std::string& localName)
{
    ImFont* title = g_fontTahoma12 ? g_fontTahoma12 : ImGui::GetFont();   // botox tahoma_12
    ImFont* row   = g_menuBold ? g_menuBold : ImGui::GetFont();           // botox verdana_bd_11
    const float w = (float)g_specListWidth;
    const int count = (int)names.size();
    const float panelH = 20.f + count * 20.f;

    float x, y; AnchorPos(io, w + 20.f, panelH, g_specPos, 4.f, x, y);

    for (int i = 1; i < 20; i++)
        dl->AddLine(ImVec2(x + i + 1.f, y + 20.f - i), ImVec2(x + i + 1.f + w - 1.f, y + 20.f - i), IM_COL32(59, 59, 59, 255));
    for (int i = 1; i <= count; i++)
        dl->AddRectFilled(ImVec2(x, y + i * 20.f), ImVec2(x + w, y + i * 20.f + 20.f), i % 2 == 0 ? IM_COL32(75, 75, 75, 255) : IM_COL32(45, 45, 45, 255));

    constexpr ImU32 frame = IM_COL32(85, 85, 85, 255);
    dl->AddLine(ImVec2(x + 20.f, y), ImVec2(x + 20.f + w + 1.f, y), frame);
    dl->AddLine(ImVec2(x, y + 20.f), ImVec2(x + 20.f, y), frame);
    dl->AddLine(ImVec2(x + w, y + 20.f), ImVec2(x + w + 20.f, y), frame);
    dl->AddLine(ImVec2(x, y + 20.f), ImVec2(x + w, y + 20.f), frame);
    dl->AddLine(ImVec2(x, y + 20.f), ImVec2(x, y + 20.f + count * 20.f), frame);
    dl->AddLine(ImVec2(x + w, y + 20.f), ImVec2(x + w, y + 20.f + count * 20.f), frame);
    dl->AddLine(ImVec2(x, y + 20.f + count * 20.f), ImVec2(x + w, y + 20.f + count * 20.f), frame);

    const ImU32 accent = PackColor(g_specColorOne);
    const float titleW = title->CalcTextSizeA(title->FontSize, FLT_MAX, 0.f, "SPECTATOR LIST").x;
    constexpr ImVec2 off[8] = { {-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1} };
    const ImVec2 tp(x + (w / 2.f) - titleW / 2.f, y + 4.f);
    for (auto& o : off) dl->AddText(title, title->FontSize, ImVec2(tp.x + o.x, tp.y + o.y), IM_COL32(0, 0, 0, 255), "SPECTATOR LIST");
    dl->AddText(title, title->FontSize, tp, accent, "SPECTATOR LIST");

    int r = 0;
    for (auto& n : names) {
        std::string text = RowText(n, localName);
        ++r;
        const ImVec2 rp(x + 4.f, y + 4.f + r * 20.f);
        for (auto& o : off) dl->AddText(row, row->FontSize, ImVec2(rp.x + o.x, rp.y + o.y), IM_COL32(0, 0, 0, 255), text.c_str());
        dl->AddText(row, row->FontSize, rp, IM_COL32(255, 255, 255, 255), text.c_str());
    }
}

// style 3: winxp -- draw_spectator_list_winxp(). A fake Notepad window: title bar,
// menu text, 3 bevelled buttons, rows grow the frame on overflow exactly like the
// donor (measured against last frame's size, settles in one frame).
static void DrawWinXP(ImDrawList* dl, ImGuiIO& io, const std::vector<std::string>& names, const std::string& localName)
{
    ImFont* font = g_fontTahomaBd12 ? g_fontTahomaBd12 : ImGui::GetFont();   // botox tahoma_bd_12
    const float fs = font->FontSize;
    static float sizeX = -200.f, sizeY = -200.f;

    float x, y; AnchorPos(io, 400.f + sizeX, 300.f + sizeY, g_specPos, 4.f, x, y);
    const ImVec2 o(x + 1.f, y + 1.f);

    const auto filled = [&](ImVec2 at, ImVec2 sz, ImU32 col) { dl->AddRectFilled(ImVec2(o.x + at.x, o.y + at.y), ImVec2(o.x + at.x + sz.x, o.y + at.y + sz.y), col); };
    const auto rect   = [&](ImVec2 at, ImVec2 sz, ImU32 col) { dl->AddRect(ImVec2(o.x + at.x, o.y + at.y), ImVec2(o.x + at.x + sz.x, o.y + at.y + sz.y), col); };
    const auto line    = [&](ImVec2 at, ImVec2 d, ImU32 col) { dl->AddLine(ImVec2(o.x + at.x, o.y + at.y), ImVec2(o.x + at.x + d.x, o.y + at.y + d.y), col); };
    const auto text    = [&](const char* s, ImVec2 at, ImU32 col) { dl->AddText(font, fs, ImVec2(o.x + at.x, o.y + at.y), col, s); };
    const auto bevel = [&](ImVec2 at) {
        filled(at, ImVec2(13.f, 11.f), IM_COL32(212, 208, 200, 255));
        ImVec2 b(at.x - 1.f, at.y - 1.f);
        line(b, ImVec2(15.f, 0.f), IM_COL32(255, 255, 255, 255));
        line(b, ImVec2(0.f, 13.f), IM_COL32(255, 255, 255, 255));
        line(ImVec2(b.x + 1.f, b.y + 12.f), ImVec2(14.f, 0.f), IM_COL32(128, 128, 128, 255));
        line(ImVec2(b.x + 14.f, b.y + 12.f), ImVec2(0.f, -11.f), IM_COL32(128, 128, 128, 255));
        line(ImVec2(b.x + 1.f, b.y + 13.f), ImVec2(15.f, 0.f), IM_COL32(0, 0, 0, 255));
        line(ImVec2(b.x + 15.f, b.y + 12.f), ImVec2(0.f, -12.f), IM_COL32(0, 0, 0, 255));
    };

    filled(ImVec2(1.f, 1.f), ImVec2(397.f + sizeX, 299.f + sizeY), IM_COL32(255, 255, 255, 255));
    rect(ImVec2(0.f, 0.f), ImVec2(399.f + sizeX, 300.f + sizeY), IM_COL32(223, 223, 223, 255));
    filled(ImVec2(2.f, 2.f), ImVec2(395.f + sizeX, 296.f + sizeY), IM_COL32(192, 192, 192, 255));
    line(ImVec2(4.f, 294.f + sizeY), ImVec2(391.f + sizeX, 0.f), IM_COL32(223, 223, 223, 255));
    line(ImVec2(394.f + sizeX, 294.f + sizeY), ImVec2(0.f, -14.f), IM_COL32(223, 223, 223, 255));
    line(ImVec2(4.f, 280.f + sizeY), ImVec2(0.f, 15.f), IM_COL32(128, 128, 128, 255));
    line(ImVec2(4.f, 280.f + sizeY), ImVec2(391.f + sizeX, 0.f), IM_COL32(128, 128, 128, 255));
    filled(ImVec2(4.f, 4.f), ImVec2(391.f + sizeX, 22.f), IM_COL32(0, 0, 128, 255));
    rect(ImVec2(4.f, 46.f), ImVec2(391.f + sizeX, 232.f + sizeY), IM_COL32(128, 128, 128, 255));
    rect(ImVec2(5.f, 47.f), ImVec2(389.f + sizeX, 230.f + sizeY), IM_COL32(0, 0, 0, 255));
    filled(ImVec2(6.f, 48.f), ImVec2(387.f + sizeX, 228.f + sizeY), IM_COL32(255, 255, 255, 255));
    text("Spectators", ImVec2(32.f, 6.f), IM_COL32(255, 255, 255, 255));
    text("File  Edit  Format  Help", ImVec2(7.f, 28.f), IM_COL32(0, 0, 0, 255));
    bevel(ImVec2(378.f + sizeX, 9.f)); text("X", ImVec2(381.f + sizeX, 7.f), IM_COL32(0, 0, 0, 255));
    bevel(ImVec2(344.f + sizeX, 9.f)); text("_", ImVec2(347.f + sizeX, 4.f), IM_COL32(0, 0, 0, 255));
    bevel(ImVec2(360.f + sizeX, 9.f)); rect(ImVec2(362.f + sizeX, 10.f), ImVec2(9.f, 9.f), IM_COL32(0, 0, 0, 255));

    sizeX = sizeY = -200.f;
    float overflowX = 0.f, overflowY = 0.f;
    int total = 0;
    for (auto& n : names) {
        std::string rowText = RowText(n, localName);
        ImVec2 at(35.f, 55.f + total++ * 15.f);
        text(rowText.c_str(), at, IM_COL32(0, 0, 0, 255));
        dl->AddCircleFilled(ImVec2(o.x + at.x - 10.f, o.y + at.y + 8.f), 4.f, IM_COL32(0, 0, 0, 255), 24);
        if (at.y > 48.f + 228.f + sizeY - 25.f) overflowY = at.y - (48.f + 228.f + sizeY - 25.f);
        const float rowW = at.x + font->CalcTextSizeA(fs, FLT_MAX, 0.f, rowText.c_str()).x;
        if (rowW > 387.f + sizeX) overflowX = rowW - (387.f + sizeX);
    }
    if (overflowY > 0.f) sizeY += overflowY;
    if (overflowX > 0.f) sizeX += overflowX;

    char counted[32]; snprintf(counted, sizeof(counted), "%d object(s)", total);
    text(counted, ImVec2(6.f, 279.f + sizeY), IM_COL32(0, 0, 0, 255));
}

// style 4: clarity -- draw_spectator_list_clarity(). Dark top->bottom gradient panel,
// centred bold title, rows coloured one/two.
static void DrawClarity(ImDrawList* dl, ImGuiIO& io, const std::vector<std::string>& names, const std::string& localName)
{
    if (names.empty() && !g_specAlwaysShow) return;

    ImFont* rowFont = g_fontTahoma12 ? g_fontTahoma12 : ImGui::GetFont();       // botox tahoma_12
    ImFont* titleFont = g_fontTahomaBd12 ? g_fontTahomaBd12 : ImGui::GetFont();  // botox tahoma_bd_12
    constexpr float padding = 10.f, topPad = 6.f, botPad = 6.f;
    const float lineH = rowFont->FontSize + 6.f;

    const ImVec2 titleSz = titleFont->CalcTextSizeA(titleFont->FontSize, FLT_MAX, 0.f, "spectators");
    float width = titleSz.x + padding * 2.f;
    for (auto& n : names) width = (std::max)(width, rowFont->CalcTextSizeA(rowFont->FontSize, FLT_MAX, 0.f, RowText(n, localName).c_str()).x + padding * 2.f);
    const float height = topPad + names.size() * lineH + rowFont->FontSize + botPad;

    float x, y; AnchorPos(io, width, height, g_specPos, 4.f, x, y);

    dl->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x + width, y + height), IM_COL32(32, 32, 32, 255), IM_COL32(32, 32, 32, 255), IM_COL32(15, 15, 15, 255), IM_COL32(15, 15, 15, 255));
    dl->AddText(titleFont, titleFont->FontSize, ImVec2(x + (width - titleSz.x) * 0.5f, y + topPad), IM_COL32(255, 255, 255, 255), "spectators");

    for (size_t i = 0; i < names.size(); ++i) {
        const float rowY = y + topPad + (i + 1) * lineH;
        dl->AddText(rowFont, rowFont->FontSize, ImVec2(x + padding, rowY), PackColor(g_specColorOne), RowText(names[i], localName).c_str());
    }
}

// ==== shared helpers for the botox styles ported below ======================================
static float TextW(ImFont* f, const std::string& s) { return f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.f, s.c_str()).x; }
static ImU32 AccentA(int a) { return IM_COL32((int)(g_specColorOne[0] * 255.f), (int)(g_specColorOne[1] * 255.f), (int)(g_specColorOne[2] * 255.f), a); }
static void ShadowText(ImDrawList* dl, ImFont* f, ImVec2 pos, ImU32 col, const char* text)
{
    dl->AddText(f, f->FontSize, ImVec2(floorf(pos.x) + 1.f, floorf(pos.y) + 1.f), col & IM_COL32_A_MASK, text);
    dl->AddText(f, f->FontSize, ImVec2(floorf(pos.x), floorf(pos.y)), col, text);
}

// style 9: cumhacck -- draw_spectator_list_cumhacck(). tahoma bold 12 rows stacked above/below
// screen vertical middle, left edge, verdana bold 35 "spectators:" label above them, shadow =
// black at the row colour's alpha.
static void DrawCumhacck(ImDrawList* dl, ImGuiIO& io, const std::vector<std::string>& names, const std::string& localName)
{
    if (names.empty()) return;
    ImFont* fT = g_fontCumhacck35 ? g_fontCumhacck35 : ImGui::GetFont();   // botox cumhacck_35
    ImFont* fR = g_fontTahomaBd12 ? g_fontTahomaBd12 : ImGui::GetFont();   // botox tahoma_bd_12
    const float mid = floorf(io.DisplaySize.y * 0.5f);
    const ImU32 col = PackColor(g_specColorOne);
    const ImU32 shadow = col & IM_COL32_A_MASK;
    const auto shadowed = [&](ImFont* f, float x, float y, const char* t) {
        dl->AddText(f, f->FontSize, ImVec2(x + 1.f, y + 1.f), shadow, t);
        dl->AddText(f, f->FontSize, ImVec2(x, y), col, t);
    };
    for (size_t i = 0; i < names.size(); ++i)
        shadowed(fR, 4.f, mid - 40.f + 15.f * i, RowText(names[i], localName).c_str());
    shadowed(fT, 4.f, mid - 75.f, "spectators:");
}

// style 11: airplane -- draw_spectator_list_airplane(). Diagonal gradient panel, bold
// outlined title, rows. Drag-to-reposition + title-case convar skipped (menu-only /
// no matching knob here) -- same AnchorPos + lowercase "spectators" every other style uses.
static void DrawAirplane(ImDrawList* dl, ImGuiIO& io, const std::vector<std::string>& names, const std::string& localName)
{
    if (names.empty() && !g_specAlwaysShow) return;
    ImFont* fT = g_fontAirplaneTitle ? g_fontAirplaneTitle : ImGui::GetFont();   // botox montserrat 10
    ImFont* fR = g_fontAirplaneRows ? g_fontAirplaneRows : ImGui::GetFont();     // botox tahoma bd 11 mono
    float w = 100.f;
    std::vector<std::string> rows;
    for (auto& n : names) {
        rows.push_back(RowText(n, localName));
        w = (std::max)(w, (float)(int)TextW(fR, rows.back()) + 5.f);
    }
    const float h = 15.f + 15.f * rows.size();
    float x, y; AnchorPos(io, w, h, g_specPos, 4.f, x, y);
    const ImU32 left = PackColor(g_specColorOne);
    dl->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x + w, y + h), left, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), left);
    // donor: 4-direction outline + the diagonal, shadow = black at the colour's alpha
    const auto shadow = [&](ImFont* f, float tx, float ty, const char* t) {
        const ImU32 sh = left & IM_COL32_A_MASK;
        dl->AddText(f, f->FontSize, ImVec2(tx + 1.f, ty), sh, t);
        dl->AddText(f, f->FontSize, ImVec2(tx - 1.f, ty), sh, t);
        dl->AddText(f, f->FontSize, ImVec2(tx, ty + 1.f), sh, t);
        dl->AddText(f, f->FontSize, ImVec2(tx, ty - 1.f), sh, t);
        dl->AddText(f, f->FontSize, ImVec2(tx + 1.f, ty + 1.f), sh, t);
        dl->AddText(f, f->FontSize, ImVec2(tx, ty), left, t);
    };
    const int tw = (int)TextW(fT, "spectators");
    shadow(fT, x + (float)((int)w / 2 - tw / 2), y + 3.f, "spectators");
    for (size_t i = 0; i < rows.size(); ++i)
        shadow(fR, x + 3.f, y + 15.f + 15.f * i, rows[i].c_str());
}

// style 13: millionware -- draw_spectator_list_millionware(). Plain right-aligned
// names stacked under the top-right corner, no decoration.
static void DrawMillionware(ImDrawList* dl, ImGuiIO& io, const std::vector<std::string>& names, const std::string&)
{
    if (names.empty()) return;
    ImFont* f = g_fontTahoma12 ? g_fontTahoma12 : ImGui::GetFont();   // botox tahoma_12
    for (size_t i = 0; i < names.size(); ++i) {
        const float tw = TextW(f, names[i]);
        dl->AddText(f, f->FontSize, ImVec2(io.DisplaySize.x - tw - 4.f, 4.f + 16.f * i), IM_COL32(255, 255, 255, 255), names[i].c_str());
    }
}

// style 14: aimware -- draw_spectator_list_aimware(). Red title banner + extending
// dark list body. Drag-to-reposition skipped, AnchorPos like every other style.
static void DrawAimware(ImDrawList* dl, ImGuiIO& io, const std::vector<std::string>& names, const std::string&)
{
    ImFont* f = g_fontSunflower30 ? g_fontSunflower30 : ImGui::GetFont();   // botox sunflower_30, drawn at 15 / 14
    float x, y; AnchorPos(io, 200.f, 24.f, g_specPos, 4.f, x, y);
    constexpr ImU32 red = IM_COL32(200, 40, 40, 255), white = IM_COL32(255, 255, 255, 255);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 200.f, y + 24.f), red, 3.f);
    dl->AddRectFilled(ImVec2(x, y + 23.f), ImVec2(x + 200.f, y + 24.f), red);
    constexpr auto title = "Spectators list";
    const float titleHalfH = f->CalcTextSizeA(15.f, FLT_MAX, 0.f, title).y * 0.5f;
    dl->AddText(f, 15.f, ImVec2(x + 8.f, y + 12.f - titleHalfH), white, title);
    float ext = 0.f;
    for (auto& n : names) ext += 8.f + f->CalcTextSizeA(14.f, FLT_MAX, 0.f, n.c_str()).y;
    ext += 8.f;
    dl->AddRectFilled(ImVec2(x, y + 24.f), ImVec2(x + 200.f, y + ext + 24.f), IM_COL32(0, 0, 0, 75));
    dl->AddRectFilled(ImVec2(x, y + ext + 24.f), ImVec2(x + 200.f, y + ext + 28.f), red);
    ext = 0.f;
    for (auto& n : names) {
        ext += 8.f;
        dl->AddText(f, 14.f, ImVec2(x + 6.f, y + ext + 24.f), white, n.c_str());
        ext += f->CalcTextSizeA(14.f, FLT_MAX, 0.f, n.c_str()).y;
    }
}

// style 15: havoc -- draw_spectator_list_havoc(). Beveled box, corner chevrons,
// "Spec"/"tators" split-colour title, rows with a right-aligned mode bracket.
static ImU32 GreyA(int shade, float alpha) { return IM_COL32(shade, shade, shade, (int)(255.f * alpha)); }
static void HavocLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col) { dl->AddLine(ImVec2(a.x + 0.5f, a.y + 0.5f), ImVec2(b.x + 0.5f, b.y + 0.5f), col); }
static void DrawHavoc(ImDrawList* dl, ImGuiIO& io, const std::vector<std::string>& names, const std::string& localName)
{
    static ImVec2 absSize{ 120.f, 20.f };
    ImFont* f = g_fontHavocTahoma13 ? g_fontHavocTahoma13 : ImGui::GetFont();   // botox havoc_tahoma_13
    const float dt = io.DeltaTime;
    // bhopcheat/havoc's obs_mode text needs the all-players scan this DLL doesn't have --
    // every row here IS watching local, so the bracket is always "[local]" / "[spectating]".
    const std::string mode = g_specShowTarget ? ("[" + localName + "]") : std::string("[spectating]");
    float targetW = 120.f, targetH = 20.f;
    for (auto& n : names) targetW = (std::max)(targetW, TextW(f, n) + TextW(f, mode) + 60.f);
    if (!names.empty()) targetH += 9.f + names.size() * 17.f;
    absSize.x += (targetW - absSize.x) * 10.f * dt;
    absSize.y += (targetH - absSize.y) * 10.f * dt;
    if (names.empty() && !g_specAlwaysShow && absSize.y < 21.f) return;
    const float w = (std::max)(absSize.x, 100.f), h = absSize.y;
    if (h < 1.f) return;
    float x, y; AnchorPos(io, w, h, g_specPos, 4.f, x, y);
    const auto shell = [&](float inset, int shade) { dl->AddRectFilled(ImVec2(x + inset, y + inset), ImVec2(x + w - inset, y + h - inset), GreyA(shade, 1.f), 5.f); };
    shell(-1.f, 10); shell(0.f, 55); shell(1.f, 10); shell(2.f, 20);
    const ImU32 accent = PackColor(g_specColorOne), accent2 = PackColor(g_specColorTwo);
    for (int i = 0; i < 3; ++i) {
        const float fi = (float)i;
        HavocLine(dl, ImVec2(x + w - 19.f - fi, y + 2.f), ImVec2(x + w - 4.f - fi, y + 17.f), accent);
        HavocLine(dl, ImVec2(x + w - 12.f - fi, y + 2.f), ImVec2(x + w - 2.f, y + 12.f + fi), accent2);
        HavocLine(dl, ImVec2(x + 19.f + fi, y + 2.f), ImVec2(x + 4.f + fi, y + 17.f), accent);
        HavocLine(dl, ImVec2(x + 11.f + fi, y + 2.f), ImVec2(x + 1.f, y + 12.f + fi), accent2);
    }
    if (h > 20.f) {
        HavocLine(dl, ImVec2(x + 1.f, y + 17.f), ImVec2(x + w - 2.f, y + 17.f), GreyA(10, 1.f));
        HavocLine(dl, ImVec2(x + 1.f, y + 18.f), ImVec2(x + w - 2.f, y + 18.f), GreyA(55, 1.f));
        HavocLine(dl, ImVec2(x + 1.f, y + 19.f), ImVec2(x + w - 2.f, y + 19.f), GreyA(10, 1.f));
    }
    const std::string t1 = "Spec", t2 = "tators";
    const float titleX = floorf(w * 0.5f - (TextW(f, t1) + TextW(f, t2)) * 0.5f);
    ShadowText(dl, f, ImVec2(x + titleX, y + 3.f), GreyA(255, 1.f), t1.c_str());
    ShadowText(dl, f, ImVec2(x + titleX + TextW(f, t1), y + 3.f), accent, t2.c_str());
    dl->PushClipRect(ImVec2(x + 2.f, y + 2.f), ImVec2(x + w - 1.f, y + h - 1.f), true);
    for (size_t i = 0; i < names.size(); ++i) {
        const float rowY = y + 25.f + 17.f * i;
        ShadowText(dl, f, ImVec2(x + 20.f, rowY + 1.f), GreyA(255, 1.f), names[i].c_str());
        ShadowText(dl, f, ImVec2(x + w - 20.f - TextW(f, mode), rowY), accent, mode.c_str());
    }
    dl->PopClipRect();
}

// style 16: onetap -- draw_spectator_list_onetap(). Fixed position, no panel at all.
static void DrawOnetap(ImDrawList* dl, ImGuiIO&, const std::vector<std::string>& names, const std::string&)
{
    ImFont* f = g_fontTahoma12 ? g_fontTahoma12 : ImGui::GetFont();   // botox tahoma_12
    for (size_t i = 0; i < names.size(); ++i)
        dl->AddText(f, f->FontSize, ImVec2(20.f, 200.f + 12.f * i), IM_COL32(230, 230, 230, 255), names[i].c_str());
}

// style 19: legendware -- draw_spectator_list_legendware(). Shadowed rows, right
// edge, under the watermark band when it's on.
static void DrawLegendware(ImDrawList* dl, ImGuiIO& io, const std::vector<std::string>& names, const std::string&)
{
    if (names.empty()) return;
    ImFont* f = g_fontLegendwareLucida10 ? g_fontLegendwareLucida10 : ImGui::GetFont();   // botox legendware_lucida_10
    const float top = g_wmEnable ? 30.f : 6.f;
    for (size_t i = 0; i < names.size(); ++i)
        ShadowText(dl, f, ImVec2(io.DisplaySize.x - TextW(f, names[i]) - 6.f, top + 16.f * i), IM_COL32(255, 255, 255, 255), names[i].c_str());
}

// style 20: interium -- draw_spectator_list_interium(). Rounded panel, rainbow band
// under the title, newline-joined name list below.
static ImU32 InteriumHsv(float hue, int alpha)
{
    float r = 0.f, g = 0.f, b = 0.f;
    ImGui::ColorConvertHSVtoRGB(hue, 1.f, 0.7f, r, g, b);
    return IM_COL32((int)(r * 255.f), (int)(g * 255.f), (int)(b * 255.f), alpha);
}
static void InteriumBand(ImDrawList* dl, float x, float y, float w, float dt)
{
    static float hueL = 0.f, hueR = 0.25f;
    hueL = fmodf(hueL + dt * 0.1f, 1.f);
    hueR = fmodf(hueR + dt * 0.1f, 1.f);
    dl->AddRectFilledMultiColor(ImVec2(x, y + 11.f), ImVec2(x + w, y + 22.f), InteriumHsv(hueL, 0), InteriumHsv(hueR, 0), InteriumHsv(hueR, 70), InteriumHsv(hueL, 70));
    dl->AddRectFilledMultiColor(ImVec2(x, y + 22.f), ImVec2(x + w, y + 24.f), InteriumHsv(hueL, 255), InteriumHsv(hueR, 255), InteriumHsv(hueR, 255), InteriumHsv(hueL, 255));
    dl->AddRectFilledMultiColor(ImVec2(x + 2.f, y + 24.f), ImVec2(x + w - 2.f, y + 29.f), InteriumHsv(hueL, 70), InteriumHsv(hueR, 70), InteriumHsv(hueR, 0), InteriumHsv(hueL, 0));
}
static void DrawInterium(ImDrawList* dl, ImGuiIO& io, const std::vector<std::string>& names, const std::string&)
{
    ImFont* f = g_fontInteriumDroid24 ? g_fontInteriumDroid24 : ImGui::GetFont();   // botox interium_droid_24, drawn at 16
    std::string list;
    for (auto& n : names) list += n + "\n";
    constexpr float w = 150.f;
    const float h = list.empty() ? 33.f : f->CalcTextSizeA(16.f, FLT_MAX, 0.f, list.c_str()).y + 31.f;
    float x, y; AnchorPos(io, w, h, g_specPos, 4.f, x, y);
    constexpr ImU32 white = IM_COL32(255, 255, 255, 255);
    // botox has a dedicated interium-bg convar; this DLL reuses g_specColorOne for it.
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + 22.f), PackColor(g_specColorOne), 3.f, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilled(ImVec2(x + 2.f, y + 22.f), ImVec2(x + w - 2.f, y + h), AccentA(127), 6.f, ImDrawFlags_RoundCornersBottom);
    constexpr auto title = "Spectator List";
    const float tw = f->CalcTextSizeA(16.f, FLT_MAX, 0.f, title).x;
    dl->AddText(f, 16.f, ImVec2(x + w * 0.5f - tw * 0.5f, y + 2.f), white, title);
    InteriumBand(dl, x, y, w, io.DeltaTime);
    if (!list.empty()) dl->AddText(f, 16.f, ImVec2(x + 8.f, y + 26.f), white, list.c_str());
}

// style 23: cumidere -- draw_spectator_list_cumidere(). Simple bordered box, title
// row + divider, one row per spectator.
static void DrawCumidere(ImDrawList* dl, ImGuiIO& io, const std::vector<std::string>& names, const std::string& localName)
{
    if (names.empty() && !g_specAlwaysShow) return;
    ImFont* f = g_fontCumidereTahoma16 ? g_fontCumidereTahoma16 : ImGui::GetFont();   // botox cumidere_tahoma_16
    const float size = f->FontSize, rowH = size + 4.f, header = size + 8.f;
    std::vector<std::string> texts;
    float widest = 0.f;
    for (auto& n : names) { texts.push_back(RowText(n, localName)); widest = (std::max)(widest, TextW(f, texts.back())); }
    const float titleW = TextW(f, "spectators");
    const float w = (std::max)((std::max)(widest, titleW), titleW * 2.f) + 16.f;
    const float h = names.size() * rowH + header + 1.f;
    float x, y; AnchorPos(io, w, h, g_specPos, 4.f, x, y);
    const ImU32 accent = PackColor(g_specColorOne);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(23, 23, 23, 255));
    dl->AddLine(ImVec2(x, y), ImVec2(x + w, y), accent);
    dl->AddLine(ImVec2(x, y), ImVec2(x, y + h), accent);
    dl->AddLine(ImVec2(x + w, y), ImVec2(x + w, y + h), accent);
    dl->AddText(f, size, ImVec2(x + 8.f, y + 4.f), accent, "spectators");
    dl->AddLine(ImVec2(x, y + header), ImVec2(x + w, y + header), accent);
    for (size_t i = 0; i < names.size(); ++i)
        dl->AddText(f, size, ImVec2(x + 8.f, y + header + 1.f + i * rowH), accent, texts[i].c_str());
}

// style 24: illusory -- draw_spectator_list_illusory(). Shadowed rows, right edge,
// under the watermark band.
static void DrawIllusory(ImDrawList* dl, ImGuiIO& io, const std::vector<std::string>& names, const std::string& localName)
{
    if (names.empty()) return;
    ImFont* f = g_fontTahomaBd12 ? g_fontTahomaBd12 : ImGui::GetFont();   // botox tahoma_bd_12 @ 12
    const float top = g_wmEnable ? 30.f : 5.f;
    const int w = (int)io.DisplaySize.x;
    for (size_t i = 0; i < names.size(); ++i) {
        const std::string text = RowText(names[i], localName);
        const int tw = (int)f->CalcTextSizeA(12.f, FLT_MAX, 0.f, text.c_str()).x;
        const float y = top + 16.f * i;
        dl->AddText(f, 12.f, ImVec2((float)(w - tw - 5), y + 1.f), IM_COL32(0, 0, 0, 255), text.c_str());
        dl->AddText(f, 12.f, ImVec2((float)(w - 6 - tw), y), PackColor(g_specColorOne), text.c_str());
    }
}

// style 25: lumi -- draw_spectator_list_lumi_interwebz(), the no-asset sub-style
// (lumi_new needs an avatar + a blur pass, not ported). Beveled hexagonal panel,
// striped rows, centred title tab.
static void DrawLumiInterwebz(ImDrawList* dl, ImGuiIO& io, const std::vector<std::string>& names, const std::string& localName)
{
    if (names.empty() && !g_specAlwaysShow) return;
    ImFont* f = g_fontLumiRubik16 ? g_fontLumiRubik16 : ImGui::GetFont();   // botox lumi_rubik_16
    ImFont* fT = g_fontLumiProggy13 ? g_fontLumiProggy13 : f;              // botox lumi_proggy_13
    const float size = f->FontSize;
    std::vector<std::string> rows;
    float widest = TextW(fT, "spectators");
    for (auto& n : names) { rows.push_back(RowText(n, localName)); widest = (std::max)(widest, TextW(f, rows.back())); }
    const float w = (std::max)(200.f, widest + 96.f);
    const float rowsH = rows.size() * 22.f;
    float x, y; AnchorPos(io, w + 20.f, 23.f + rowsH, g_specPos, 4.f, x, y);
    const float bottom = y + 23.f + rowsH;
    const ImVec2 shape[6] = { ImVec2(x + 23.f, y), ImVec2(x + w + 20.f, y), ImVec2(x + w, y + 23.f),
                               ImVec2(x + w, bottom), ImVec2(x, bottom), ImVec2(x, y + 23.f) };
    dl->AddConvexPolyFilled(shape, 6, IM_COL32(52, 52, 52, 255));
    const ImU32 rim = IM_COL32(150, 150, 150, 50);
    dl->AddLine(ImVec2(x + 24.f, y + 1.f), ImVec2(x + w + 19.f, y + 1.f), rim);
    dl->AddLine(ImVec2(x + w + 19.f, y + 1.f), ImVec2(x + w - 1.f, y + 22.f), rim);
    dl->AddLine(ImVec2(x + 1.f, y + 22.f), ImVec2(x + 24.f, y + 1.f), rim);
    dl->AddLine(ImVec2(x + 1.f, y + 22.f), ImVec2(x + w - 1.f, y + 22.f), rim);
    if (!rows.empty()) {
        const ImVec2 frame[4] = { ImVec2(x, y + 23.f), ImVec2(x, bottom), ImVec2(x + w, bottom), ImVec2(x + w, y + 23.f) };
        dl->AddPolyline(frame, 4, IM_COL32(69, 69, 69, 255), ImDrawFlags_None, 2.f);
    }
    const ImU32 local = PackColor(g_specColorOne);
    for (size_t i = 0; i < rows.size(); ++i) {
        const float rowY = y + 23.f + i * 22.f;
        dl->AddRectFilled(ImVec2(x, rowY), ImVec2(x + w, rowY + 22.f), (i & 1) ? IM_COL32(69, 69, 69, 255) : IM_COL32(41, 41, 41, 255));
        const float tw = TextW(f, rows[i]);
        dl->AddText(f, size, ImVec2(x + (w - tw) * 0.5f, rowY + 11.f - size * 0.5f - 1.f), local, rows[i].c_str());
    }
    const float titleW = TextW(fT, "spectators");
    dl->AddText(fT, fT->FontSize, ImVec2(x + w * 0.5f - titleW * 0.5f, y + (23.f - fT->FontSize) * 0.5f), local, "spectators");
}

// style 26: billware -- draw_spectator_list_billware(). Double-ring rounded box,
// fades in/out, shadowed rows.
static void BillwareRounded(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 bg, ImU32 ul, ImU32 ur, ImU32 br, ImU32 bl, float rounding)
{
    const float r = (std::min)(rounding, (std::min)(fabsf(mx.x - mn.x) * 0.5f - 1.f, fabsf(mx.y - mn.y) * 0.5f - 1.f));
    if (r <= 0.f) return;
    dl->AddRectFilledMultiColor(mn, mx, ul, ur, br, bl);
    struct C { ImVec2 corner, center; float a0, a1; };
    const C corners[] = {
        { mn, ImVec2(mn.x + r, mn.y + r), 4.82f, 3.1f },
        { ImVec2(mx.x, mn.y), ImVec2(mx.x - r, mn.y + r), 6.34f, 4.62f },
        { mx, ImVec2(mx.x - r, mx.y - r), 7.96f, 6.24f },
        { ImVec2(mn.x, mx.y), ImVec2(mn.x + r, mx.y - r), 9.5f, 7.77f },
    };
    for (auto& c : corners) { dl->PathLineTo(c.corner); dl->PathArcTo(c.center, r, c.a0, c.a1, 0); dl->PathFillConvex(bg); }
}
static void BillwareBox(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float alpha)
{
    const auto grey = [&](float v, float a) { return IM_COL32((int)(v * 255.f), (int)(v * 255.f), (int)(v * 255.f), (int)(a * 255.f)); };
    const auto grown = [&](float g) { return std::pair{ ImVec2(mn.x - g, mn.y - g), ImVec2(mx.x + g, mx.y + g) }; };
    const ImU32 dark = grey(0.07f, alpha), light = grey(0.14f, alpha);
    auto [fmn, fmx] = grown(2.f);
    BillwareRounded(dl, fmn, fmx, dark, dark, dark, light, light, 5.5f);
    BillwareRounded(dl, fmn, fmx, dark, light, light, dark, dark, 5.5f);
    const ImU32 rings[3] = { grey(0.15f, alpha), grey(0.3f, alpha * 0.7f), grey(0.15f, alpha) };
    for (int i = 0; i < 3; ++i) { auto [rmn, rmx] = grown(2.f + (float)i); dl->AddRect(rmn, rmx, rings[i], 5.5f, 0, 1.f); }
}
static void DrawBillware(ImDrawList* dl, ImGuiIO& io, const std::vector<std::string>& names, const std::string& localName)
{
    ImFont* f = g_fontTahomaBd12 ? g_fontTahomaBd12 : ImGui::GetFont();   // botox tahoma_bd_12 @ 12
    static float fade = 0.f;
    const float step = io.DeltaTime * 25.5f;
    fade = (!names.empty() || g_specAlwaysShow) ? (std::min)(fade + step, 1.f) : (std::max)(fade - step, 0.f);
    if (fade <= 0.f) return;
    std::vector<std::string> texts;
    float widest = 0.f;
    for (auto& n : names) { texts.push_back(RowText(n, localName)); widest = (std::max)(widest, TextW(f, texts.back())); }
    const float w = widest + 14.f, h = names.size() * 16.f + 14.f;
    float x, y; AnchorPos(io, w, h, g_specPos, 4.f, x, y);
    BillwareBox(dl, ImVec2(x, y), ImVec2(x + w, y + h), fade);
    const ImU32 shadow = AccentA((int)(fade * 255.f));
    const int textA = (int)(fade * 255.f);
    const ImU32 textColor = IM_COL32((int)(g_specColorOne[0] * 255.f), (int)(g_specColorOne[1] * 255.f), (int)(g_specColorOne[2] * 255.f), textA);
    for (size_t i = 0; i < names.size(); ++i) {
        const ImVec2 pos(x + 7.f, y + 7.f + 16.f * i);
        dl->AddText(f, f->FontSize, ImVec2(pos.x + 1.f, pos.y + 1.f), shadow, texts[i].c_str());
        dl->AddText(f, f->FontSize, pos, textColor, texts[i].c_str());
    }
}

// style 27: inkabanium -- draw_spectator_list_inkabanium(). Dark box with title bar,
// accent-coloured text. Rainbow title mode skipped (no matching convar) -- accent only.
static void DrawInkabanium(ImDrawList* dl, ImGuiIO& io, const std::vector<std::string>& names, const std::string&)
{
    if (names.empty() && !g_specAlwaysShow) return;
    ImFont* f = g_fontInbaTahoma13 ? g_fontInbaTahoma13 : ImGui::GetFont();   // botox inba_tahoma_13
    const float line = f->FontSize;
    constexpr float w = 195.f, titleH = 19.f;
    const float h = (names.empty() ? line : line * (names.size() + 1)) + 30.f;
    float x, y; AnchorPos(io, w, h, g_specPos, 4.f, x, y);
    const ImU32 color = PackColor(g_specColorOne);
    dl->AddRectFilled(ImVec2(x, y + titleH), ImVec2(x + w, y + h), IM_COL32(13, 13, 13, 178));
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + titleH), IM_COL32(13, 13, 13, 255));
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), color);
    const float tw = f->CalcTextSizeA(line, FLT_MAX, 0.f, "SPECTATORS").x;
    dl->AddText(f, line, ImVec2(floorf(x + (w - tw) * 0.5f), y + 3.f), color, "SPECTATORS");
    for (size_t i = 0; i < names.size(); ++i)
        dl->AddText(f, line, ImVec2(x + 8.f, y + titleH + 8.f + line * i), color, names[i].c_str());
}

// style 28: dna -- draw_spectator_list_dna(). Rounded box, accent gradient top bar,
// fades in/out.
static ImU32 DnaGrey(float v, float a = 1.f) { return IM_COL32((int)(v * 255.f), (int)(v * 255.f), (int)(v * 255.f), (int)(a * 255.f)); }
static void DrawDna(ImDrawList* dl, ImGuiIO& io, const std::vector<std::string>& names, const std::string& localName)
{
    ImFont* f = g_fontDnaMontserrat15 ? g_fontDnaMontserrat15 : ImGui::GetFont();   // botox dna_montserrat_15
    constexpr float size = 15.f, pad = 7.f, spacing = 3.f;
    static float alpha = 0.f;
    const float step = io.DeltaTime * 25.5f;
    alpha = std::clamp(alpha + ((!names.empty() || g_specAlwaysShow) ? step : -step), 0.f, 1.f);
    if (alpha <= 0.f) return;
    std::vector<std::string> texts;
    float widest = TextW(f, "Spectators");
    for (auto& n : names) { texts.push_back(RowText(n, localName)); widest = (std::max)(widest, TextW(f, texts.back())); }
    const float w = widest + pad * 2.f, titleH = size + pad * 2.f;
    const float bodyH = names.empty() ? 0.f : names.size() * (size + spacing) - spacing + pad * 2.f;
    float x, y; AnchorPos(io, w, titleH + bodyH, g_specPos, 4.f, x, y);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + titleH), DnaGrey(0.07f), 4.5f, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilled(ImVec2(x, y + titleH), ImVec2(x + w, y + titleH + bodyH), DnaGrey(0.07f, 0.8f), 4.5f, ImDrawFlags_RoundCornersBottom);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + titleH + bodyH), DnaGrey(0.1f, 0.7f), 5.5f);
    const float center = x + w * 0.5f;
    for (int i = 0; i < 60; ++i) {
        const float x0 = x + w / 60.f * i, x1 = x0 + w / 60.f;
        const float a = 1.f - fabsf((x0 + x1) * 0.5f - center) / (w * 0.5f);
        dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + 3.f), AccentA((int)(0.75f * a * 255.f)));
    }
    dl->AddText(f, size, ImVec2(x + pad, y + pad), DnaGrey(0.8f), "Spectators");
    for (size_t i = 0; i < texts.size(); ++i)
        dl->AddText(f, size, ImVec2(x + pad, y + titleH + i * (size + spacing) + pad), DnaGrey(0.8f, alpha), texts[i].c_str());
}

// style 29: cucumber -- draw_spectator_list_cucumber(). Pill header tab + fading
// rounded body.
static ImU32 CucumberFadeColor(ImU32 color, float alpha)
{
    const ImU32 rgb = color & ~IM_COL32_A_MASK;
    const int a = (int)((float)((color >> IM_COL32_A_SHIFT) & 0xFF) * alpha + 0.5f);
    return rgb | ((ImU32)a << IM_COL32_A_SHIFT);
}
static void DrawCucumber(ImDrawList* dl, ImGuiIO& io, const std::vector<std::string>& names, const std::string& localName)
{
    ImFont* f = g_fontCucumberTahoma14 ? g_fontCucumberTahoma14 : ImGui::GetFont();   // botox cucumber_tahoma_14
    constexpr ImU32 bg = IM_COL32(18, 15, 18, 255);
    constexpr float w = 160.f, line = 14.f;
    static float alpha = 0.f;
    const float step = io.DeltaTime * 5.f;
    const bool on = !names.empty() || g_specAlwaysShow;
    alpha = std::clamp(on ? alpha + step : alpha - step, 0.f, 1.f);
    if (!on && alpha <= 0.f) return;
    const float textH = names.empty() ? line : line * names.size();
    float x, y; AnchorPos(io, w, names.empty() ? 22.f : textH + 27.f, g_specPos, 4.f, x, y);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + 22.f), bg, 15.f);
    const float tw = f->CalcTextSizeA(line, FLT_MAX, 0.f, "Spectator List").x;
    dl->AddText(f, line, ImVec2(x + (w - tw) * 0.5f, y), CucumberFadeColor(IM_COL32(255, 255, 255, 255), alpha), "Spectator List");
    dl->AddRectFilled(ImVec2(x + 15.f, y + 20.f), ImVec2(x + 145.f, y + 22.f), AccentA(255));
    if (!names.empty()) {
        dl->AddRectFilled(ImVec2(x, y + 25.f), ImVec2(x + w, y + 27.f + textH), bg, 15.f);
        for (size_t i = 0; i < names.size(); ++i) {
            const std::string text = RowText(names[i], localName);
            dl->AddText(f, line, ImVec2(x + 6.f, y + 26.f + line * i), CucumberFadeColor(PackColor(g_specColorOne), alpha), text.c_str());
        }
    }
}

// style 31: gamesense -- draw_spectator_list_gamesense(). No list in the donor: same frame /
// bar / font as its gamesense watermark (sf pro bold 15 drawn at 13), title row = watermark
// row, "name -> target" rows under a (62) grey divider. Avatars off (donor default).
static void DrawGamesenseList(ImDrawList* dl, ImGuiIO& io, const std::vector<std::string>& names, const std::string& localName)
{
    if (names.empty() && !g_specAlwaysShow) return;
    ImFont* font = g_fontGamesenseSfBold15 ? g_fontGamesenseSfBold15 : ImGui::GetFont();
    constexpr float px = 13.f;
    constexpr const char* title = "spectators"; constexpr const char* arrow = " -> ";
    const auto gw = [&](const std::string& s) { return font->CalcTextSizeA(px, FLT_MAX, 0.f, s.c_str()).x; };

    constexpr bool bar = true;
    const float textH = font->CalcTextSizeA(px, FLT_MAX, 0.f, "A").y;
    const float header = textH + 10.f + (bar ? 3.f : 0.f), rowH = textH + 4.f;

    const float titleW = gw(title);
    float widest = titleW;
    for (auto& n : names) widest = (std::max)(widest, gw(g_specShowTarget ? n + arrow + localName : n));
    const float width = (std::max)(150.f, widest + 14.f);
    const float height = header + (names.empty() ? 0.f : names.size() * rowH + 4.f);

    // frame hangs 3 px outside the box
    float x, y; AnchorPos(io, width + 6.f, height + 6.f, g_specPos, 4.f, x, y);
    x += 3.f; y += 3.f;
    const ImVec2 mn(x, y), mx(x + width, y + height);

    Watermark_GamesenseFrame(dl, mn, mx, bar);

    constexpr ImU32 white = IM_COL32(255, 255, 255, 230), grey = IM_COL32(210, 210, 210, 255);
    dl->AddText(font, px, ImVec2(x + (width - titleW) * 0.5f, y + 3.f + (bar ? 2.f : 0.f)), white, title);
    if (!names.empty())
        dl->AddRectFilled(ImVec2(x + 2.f, y + header - 1.f), ImVec2(mx.x - 2.f, y + header), IM_COL32(62, 62, 62, 255));

    const ImU32 local = PackColor(g_specColorOne);
    dl->PushClipRect(ImVec2(x + 2.f, y), ImVec2(mx.x - 2.f, mx.y), true);
    for (size_t i = 0; i < names.size(); ++i) {
        const float rowY = y + header + 2.f + (float)i * rowH;
        float textX = x + 7.f;
        const auto put = [&](const std::string& s, ImU32 c) { dl->AddText(font, px, ImVec2(textX, rowY + 2.f), c, s.c_str()); textX += gw(s); };
        put(names[i], local);
        if (g_specShowTarget && !localName.empty()) { put(arrow, grey); put(localName, white); }
    }
    dl->PopClipRect();
}

// ==== remaining botox styles (asset-based: avatar/gif/blur infra) ===========================
// Donor extras not ported, same precedent as every style above: drag-to-reposition (menu-only
// mouse interaction, AnchorPos instead), per-row "all players" target column (this DLL only
// ever collects local-watchers, see CollectSpectatorRows), custom gif path / rainbow-title
// convars (menu-only extras).

// style 5: chillware -- draw_spectator_list_chillware(). Rounded card, title "spectator
// list", one row per spectator with a round avatar + "name -> you". Donor's bezier link
// lines (per-target grouping, converging curves) are a whole animation system for a cosmetic
// list -- ponytail: straight row instead, same information, no curve math. Width eases to
// its target the same way billware/lumi already do.
static void DrawChillware(ImDrawList* dl, ImGuiIO& io, const std::vector<SpecRow>& rows)
{
    if (rows.empty() && !g_specAlwaysShow) return;
    ImFont* font = g_fontChillware13 ? g_fontChillware13 : ImGui::GetFont();   // botox chillware_13
    const float fs = font->FontSize;

    std::vector<std::string> texts;
    float widest = TextW(font, "spectator list") + 20.f;
    for (auto& r : rows) { texts.push_back(r.name + " -> you"); widest = (std::max)(widest, 24.f + TextW(font, texts.back()) + 14.f); }

    static float width = 0.f;
    width += (widest - width) * std::clamp(12.f * io.DeltaTime, 0.f, 1.f);

    constexpr float header = 21.f, rowH = 21.f, pad = 7.f;
    const float height = header + pad * 2.f + rows.size() * rowH;
    float x, y; AnchorPos(io, width, height, g_specPos, 4.f, x, y);

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height), IM_COL32(8, 8, 10, 248), 12.f);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + header), IM_COL32(22, 22, 27, 248), 12.f, ImDrawFlags_RoundCornersTop);
    dl->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), IM_COL32(255, 255, 255, 16), 12.f);

    const float titleW = TextW(font, "spectator list");
    dl->AddText(font, fs, ImVec2(x + (width - titleW) * 0.5f, y + (header - fs) * 0.5f), IM_COL32(196, 196, 206, 255), "spectator list");

    const ImU32 col = PackColor(g_specColorOne);
    for (size_t i = 0; i < rows.size(); ++i) {
        const float rowY = y + header + pad + i * rowH;
        const ImVec2 av(x + pad, rowY + rowH * 0.5f - 8.f);
        const ImTextureID tex = rows[i].steamid64 ? Avatar_Get(rows[i].steamid64, false) : 0;
        if (tex) dl->AddImageRounded(tex, av, ImVec2(av.x + 16.f, av.y + 16.f), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 8.f);
        else dl->AddCircleFilled(ImVec2(av.x + 8.f, av.y + 8.f), 8.f, IM_COL32(38, 38, 46, 255));
        dl->AddText(font, fs, ImVec2(x + pad + 24.f, rowY + (rowH - fs) * 0.5f), col, texts[i].c_str());
    }
}

// style 6: lobotomy -- draw_spectator_list_lobotomy(). Dark rounded box, centred "Spectators"
// title, avatar + name rows with a gap between each (donor's item_spacing).
static void DrawLobotomy(ImDrawList* dl, ImGuiIO& io, const std::vector<SpecRow>& rows)
{
    if (rows.empty() && !g_specAlwaysShow) return;
    ImFont* font = g_fontLobotomy13 ? g_fontLobotomy13 : ImGui::GetFont();   // botox lobotomy_13
    const float fs = font->FontSize;
    constexpr float w = 200.f, avatar = 13.f, gap = 4.f, rowH = 17.f, rowSp = 10.f, top = 25.f, bottom = 10.f;
    const float h = top + rows.size() * rowH + (std::max)((float)rows.size() - 1.f, 0.f) * rowSp + bottom;
    float x, y; AnchorPos(io, w, h, g_specPos, 4.f, x, y);

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(13, 13, 13, 255), 5.f);
    const float titleW = TextW(font, "Spectators");
    dl->AddText(font, fs, ImVec2(x + (w - titleW) * 0.5f, y + 5.f), IM_COL32(255, 255, 255, 255), "Spectators");

    const ImU32 col = PackColor(g_specColorOne);
    for (size_t i = 0; i < rows.size(); ++i) {
        const float rowY = y + top + i * (rowH + rowSp);
        const ImTextureID tex = rows[i].steamid64 ? Avatar_Get(rows[i].steamid64, false) : 0;
        if (tex) dl->AddImage(tex, ImVec2(x + 10.f, rowY), ImVec2(x + 10.f + avatar, rowY + avatar));
        dl->AddText(font, fs, ImVec2(x + 10.f + avatar + gap, rowY), col, rows[i].name.c_str());
    }
}

// style 7: kamibebra -- draw_spectator_list_kamibebra(). Dark bordered box, bold "spectators"
// title, avatar + "name  >  you" rows, donor's kamibebra_gif_original bled in above the box
// at full size (donor's 3-way gif-type/custom-path picker is a menu-only extra, this DLL
// always uses the original gif).
static void DrawKamibebraList(ImDrawList* dl, ImGuiIO& io, const std::vector<SpecRow>& rows)
{
    if (rows.empty() && !g_specAlwaysShow) return;
    ImFont* fR = g_fontKamibebra13 ? g_fontKamibebra13 : ImGui::GetFont();   // botox kamibebra_13
    ImFont* fB = g_fontKamibebraBold13 ? g_fontKamibebraBold13 : fR;        // botox kamibebra_bold_13
    const float fs = fR->FontSize;
    constexpr float top = 29.f, rowH = 18.f, bottom = 5.f, pad = 10.f, avatar = 13.f, gap = 4.f;

    std::vector<std::string> texts;
    float widest = TextW(fB, "spectators");
    for (auto& r : rows) { texts.push_back(r.name + "  >  you"); widest = (std::max)(widest, avatar + gap + TextW(fR, texts.back())); }
    const float w = widest + pad * 2.f;
    const float h = top + rows.size() * rowH + bottom;
    float x, y; AnchorPos(io, w, h, g_specPos, 4.f, x, y);

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(18, 18, 18, 235), 6.f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(60, 60, 60, 200), 6.f);
    const float titleW = TextW(fB, "spectators");
    dl->AddText(fB, fB->FontSize, ImVec2(x + (w - titleW) * 0.5f, y + 6.f), PackColor(g_specColorOne), "spectators");

    for (size_t i = 0; i < rows.size(); ++i) {
        const float rowY = y + top + i * rowH;
        const ImTextureID tex = rows[i].steamid64 ? Avatar_Get(rows[i].steamid64, false) : 0;
        if (tex) dl->AddImage(tex, ImVec2(x + pad, rowY + 2.f), ImVec2(x + pad + avatar, rowY + 2.f + avatar));
        dl->AddText(fR, fs, ImVec2(x + pad + avatar + gap, rowY + 1.f), IM_COL32(230, 230, 230, 255), texts[i].c_str());
    }

    GifTex* gif = GifTex_Get("kamibebra_original", kamibebra_gif_original, sizeof(kamibebra_gif_original));
    const ImTextureID frame = GifTex_Frame(gif, ImGui::GetTime());
    if (frame) {
        constexpr float gifSize = 48.f;
        const ImVec2 gmin(x + (w - gifSize) * 0.5f, y - gifSize);
        dl->AddImage(frame, gmin, ImVec2(gmin.x + gifSize, gmin.y + gifSize));
    }
}

// style 8: eyes -- draw_spectator_list_eyes(). Avatar circle per row + a small drawn eye
// (white + pupil) instead of the donor's per-row procedural eye_row_texture -- every row
// here IS watching the local player (this DLL has no "all players" scan), so the pupil is
// always the local colour, no watching/not-watching branch needed.
static void DrawEyes(ImDrawList* dl, ImGuiIO&, const std::vector<SpecRow>& rows)
{
    if (rows.empty()) return;
    constexpr float face = 32.f, radius = face * 0.5f, pitch = face + 4.f, pad = 4.f;
    const ImU32 local = PackColor(g_specColorOne);
    for (size_t i = 0; i < rows.size(); ++i) {
        const ImVec2 center(pad + radius, pad + radius + i * pitch);
        const ImTextureID tex = rows[i].steamid64 ? Avatar_Get(rows[i].steamid64, false) : 0;
        if (tex) dl->AddImageRounded(tex, ImVec2(center.x - radius, center.y - radius), ImVec2(center.x + radius, center.y + radius),
            ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, radius);
        else dl->AddCircleFilled(center, radius, IM_COL32(45, 45, 45, 200));

        const ImVec2 eye(center.x + radius * 0.35f, center.y - radius * 0.1f);
        dl->AddCircleFilled(eye, radius * 0.22f, IM_COL32(255, 255, 255, 230));
        dl->AddCircleFilled(ImVec2(eye.x + radius * 0.08f, eye.y), radius * 0.09f, local);
    }
}

// style 10: clarity v2 -- draw_spectator_list_clarity_v2(). Dark rounded box, bold
// "spectators" title, rounded avatar + name rows, fades in/out on the row count (same
// fade speed as the already-ported watermark clarity v2's fps-slot ease).
static void DrawClarityV2List(ImDrawList* dl, ImGuiIO& io, const std::vector<SpecRow>& rows)
{
    ImFont* fT = g_fontInterBold15 ? g_fontInterBold15 : ImGui::GetFont();   // botox clarity_inter_bold_15
    ImFont* fR = g_fontInterMedium15 ? g_fontInterMedium15 : fT;            // botox clarity_inter_medium_15

    static float alpha = 0.f;
    const float target = (!rows.empty() || g_specAlwaysShow) ? 1.f : 0.f;
    alpha += (target - alpha) * std::clamp(15.f * io.DeltaTime, 0.f, 1.f);
    if (alpha < 0.02f) return;
    const int a255 = (int)(255.f * alpha);

    constexpr float w = 210.f, pad = 12.f, avatar = 20.f;
    const float rowH = (std::max)(fR->FontSize, avatar) + 3.f;
    const float titleH = fT->FontSize + 5.f;
    const float h = pad + titleH + rows.size() * rowH + pad;
    float x, y; AnchorPos(io, w, h, g_specPos, 4.f, x, y);

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(7, 7, 7, a255), 5.f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(50, 50, 50, a255), 5.f);

    const float titleW = TextW(fT, "spectators");
    dl->AddText(fT, fT->FontSize, ImVec2(x + w * 0.5f - titleW * 0.5f, y + pad), IM_COL32(255, 255, 255, a255), "spectators");

    ImU32 rowCol = PackColor(g_specColorOne);
    rowCol = (rowCol & ~IM_COL32_A_MASK) | ((ImU32)a255 << IM_COL32_A_SHIFT);
    float rowY = y + pad + titleH;
    for (auto& r : rows) {
        float rowX = x + pad;
        const ImTextureID tex = r.steamid64 ? Avatar_Get(r.steamid64, false) : 0;
        if (tex) {
            dl->AddImageRounded(tex, ImVec2(rowX, rowY), ImVec2(rowX + avatar, rowY + avatar), ImVec2(0, 0), ImVec2(1, 1),
                IM_COL32(255, 255, 255, a255), avatar * 0.5f);
            rowX += avatar + 5.f;
        }
        dl->AddText(fR, fR->FontSize, ImVec2(rowX, rowY + (rowH - fR->FontSize) * 0.5f), rowCol, r.name.c_str());
        rowY += rowH;
    }
}

// style 12: bhopcheat -- draw_spectator_list_bhopcheat(). Fixed top-left, no panel: shadowed
// name per row with a small avatar to its left.
static void DrawBhopcheat(ImDrawList* dl, ImGuiIO&, const std::vector<SpecRow>& rows)
{
    if (rows.empty()) return;
    ImFont* font = g_fontTahomaBd12 ? g_fontTahomaBd12 : ImGui::GetFont();   // botox tahoma_bd_12
    const float fs = font->FontSize;
    constexpr float x = 25.f;
    for (size_t i = 0; i < rows.size(); ++i) {
        const float y = 5.f + fs * i;
        const ImTextureID tex = rows[i].steamid64 ? Avatar_Get(rows[i].steamid64, false) : 0;
        if (tex) dl->AddImage(tex, ImVec2(9.f, y), ImVec2(21.f, y + 12.f));
        dl->AddText(font, fs, ImVec2(x + 1.f, y + 1.f), IM_COL32(0, 0, 0, 255), rows[i].name.c_str());
        dl->AddText(font, fs, ImVec2(x, y), IM_COL32(255, 255, 255, 255), rows[i].name.c_str());
    }
}

// style 17: airflow -- draw_spectator_list_airflow(). Two-part rounded panel: header pill
// (icon + "spectators"), divider line, body rows. Donor's live backbuffer blur under the
// panel -> BlurRect (same shader as bloom, straight replace instead of an additive combine).
static void DrawAirflowList(ImDrawList* dl, ImGuiIO& io, const std::vector<SpecRow>& rows)
{
    ImFont* font = g_fontAirflow14 ? g_fontAirflow14 : ImGui::GetFont();   // botox airflow_14
    static float alpha = 0.f;
    const float target = (!rows.empty() || g_specAlwaysShow) ? 1.f : 0.f;
    alpha += (target - alpha) * std::clamp(10.f * io.DeltaTime, 0.f, 1.f);
    if (alpha <= 0.01f) return;

    constexpr float w = 176.f, header = 32.f, rowH = 25.f;
    const float h = header + rows.size() * rowH + 8.f;
    float x, y; AnchorPos(io, w, h, g_specPos, 4.f, x, y);
    const int a = (int)(255.f * alpha);

    BlurRect(dl, ImVec2(x, y), ImVec2(x + w, y + h), 1.f, 4.f);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + header), IM_COL32(255, 255, 255, a / 3), 4.f, ImDrawFlags_RoundCornersTop);

    GifTex* gif = GifTex_Get("airflow_spectators_icon", airflow_spectators_icon, sizeof(airflow_spectators_icon));
    const ImTextureID icon = GifTex_Frame(gif, 0.0);
    if (icon) dl->AddImage(icon, ImVec2(x + 8.f, y + 8.f), ImVec2(x + 24.f, y + 24.f), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, a));
    dl->AddText(font, font->FontSize, ImVec2(x + 32.f, y + 9.f), IM_COL32(255, 255, 255, a), "spectators");
    dl->AddLine(ImVec2(x, y + header - 1.f), ImVec2(x + w, y + header - 1.f), IM_COL32(255, 255, 255, (int)(12.75f * alpha)));

    dl->AddRectFilled(ImVec2(x, y + header), ImVec2(x + w, y + h), IM_COL32(100, 100, 100, a / 3), 4.f, ImDrawFlags_RoundCornersBottom);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(100, 100, 100, (int)(100.f * alpha)), 4.f);

    constexpr const char* chase = "[ you ]";
    const float cw = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, chase).x;
    for (size_t i = 0; i < rows.size(); ++i) {
        const float rowY = y + header + 6.f + i * rowH;
        dl->AddText(font, font->FontSize, ImVec2(x + 12.f, rowY), IM_COL32(255, 255, 255, a), rows[i].name.c_str());
        dl->AddText(font, font->FontSize, ImVec2(x + w - 12.f - cw, rowY), IM_COL32(255, 255, 255, (int)(0.4f * 255.f * alpha)), chase);
    }
}

// style 18: ev0lve -- draw_spectator_list_evolve(). Rounded card that grows/shrinks with the
// row count, soft drop shadow, blurred body, bold "Spectators" header, avatar + name rows.
static void EvolveShadowList(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float radius, float alpha)
{
    const ImU32 dark = IM_COL32(0, 0, 0, (int)(255.f * alpha)), clear = IM_COL32(0, 0, 0, 0);
    dl->AddRectFilled(mn, mx, dark);
    dl->AddRectFilledMultiColor(ImVec2(mn.x - radius, mn.y), ImVec2(mn.x, mx.y), clear, dark, dark, clear);
    dl->AddRectFilledMultiColor(ImVec2(mx.x, mn.y), ImVec2(mx.x + radius, mx.y), dark, clear, clear, dark);
    dl->AddRectFilledMultiColor(ImVec2(mn.x, mn.y - radius), ImVec2(mx.x, mn.y), clear, clear, dark, dark);
    dl->AddRectFilledMultiColor(ImVec2(mn.x, mx.y), ImVec2(mx.x, mx.y + radius), dark, dark, clear, clear);
}
static void DrawEvolveList(ImDrawList* dl, ImGuiIO& io, const std::vector<SpecRow>& rows)
{
    ImFont* font = g_fontEvolve16 ? g_fontEvolve16 : ImGui::GetFont();   // botox evolve_16 / evolve_bold_16
    ImFont* bold = g_fontEvolveBold16 ? g_fontEvolveBold16 : font;

    static float alpha = 0.f, width = 160.f, height = 30.f;
    const bool show = !rows.empty() || g_specAlwaysShow;
    alpha += ((show ? 1.f : 0.f) - alpha) * std::clamp(10.f * io.DeltaTime, 0.f, 1.f);
    const float targetH = show ? (rows.size() + 1.f) * 24.f + 10.f : 30.f;
    const float targetW = show ? 220.f : 160.f;
    width += (targetW - width) * std::clamp(12.f * io.DeltaTime, 0.f, 1.f);
    height += (targetH - height) * std::clamp(12.f * io.DeltaTime, 0.f, 1.f);
    if (alpha <= 0.01f) return;

    float x, y; AnchorPos(io, width, height, g_specPos, 4.f, x, y);
    const ImVec2 mn(x, y), mx(x + width, y + height);
    const int a = (int)(255.f * alpha);

    EvolveShadowList(dl, mn, mx, 12.f, alpha * 0.25f);
    BlurRect(dl, mn, mx, 1.f, 4.f);
    dl->AddRectFilled(mn, mx, IM_COL32(18, 18, 18, a), 4.f);
    dl->AddRectFilled(mn, ImVec2(mx.x, mn.y + 24.f), IM_COL32(29, 29, 29, a), 4.f, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilled(ImVec2(mn.x, mn.y + 24.f), ImVec2(mx.x, mn.y + 25.f), PackColor(g_specColorOne));

    constexpr const char* title = "Spectators";
    const float tw = TextW(bold, title);
    dl->AddText(bold, bold->FontSize, ImVec2(x + (width - tw) * 0.5f, y + (24.f - bold->FontSize) * 0.5f), IM_COL32(220, 220, 220, a), title);

    dl->PushClipRect(mn, mx, true);
    float offset = 32.f;
    for (auto& r : rows) {
        const ImTextureID tex = r.steamid64 ? Avatar_Get(r.steamid64, false) : 0;
        if (tex) dl->AddImageRounded(tex, ImVec2(x + 10.f, y + offset), ImVec2(x + 30.f, y + offset + 20.f), ImVec2(0, 0), ImVec2(1, 1),
            IM_COL32(255, 255, 255, a), 10.f);
        dl->AddText(font, font->FontSize, ImVec2(x + 38.f, y + offset), IM_COL32(255, 255, 255, a), r.name.c_str());
        offset += 24.f;
    }
    dl->PopClipRect();
}

// style 21: chillware v2 -- draw_spectator_list_chillware_v2(). The one botox style that's
// an actual ImGui::Begin() window (auto-resize, draggable while the menu is open), not a
// background draw-list panel like every other style here -- ported the same way instead of
// flattened onto AnchorPos. Position is ImGuiCond_FirstUseEver, same as the donor; this DLL
// disables imgui.ini (render.cpp io.IniFilename = nullptr) so, unlike the donor, a drag does
// not survive a restart -- same scope as every other style's position knob (AnchorPos's
// g_specPos also doesn't remember a manual drag).
static void DrawChillwareV2(const std::vector<SpecRow>& rows)
{
    ImFont* titleFont = g_menuBold ? g_menuBold : ImGui::GetFont();   // botox verdana_bd_11
    ImFont* rowFont = g_menuReg ? g_menuReg : ImGui::GetFont();       // botox verdana_11

    int flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
    if (!g_showMenu) flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;

    const ImU32 accent = PackColor(g_specColorOne);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.f, 10.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(7, 7, 7, 255));
    ImGui::PushStyleColor(ImGuiCol_Border, (accent & ~IM_COL32_A_MASK) | (50u << IM_COL32_A_SHIFT));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));

    ImGui::SetNextWindowPos(ImVec2(15.f, ImGui::GetIO().DisplaySize.y * 0.3f), ImGuiCond_FirstUseEver);
    ImGui::PushFont(titleFont);
    ImGui::Begin("spectator list##botox-spectators-chillware-v2", nullptr, flags);
    ImGui::PopFont();
    {
        ImGui::PushFont(rowFont);
        for (auto& r : rows) {
            const std::string text = r.name + " -> you";
            ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(text.c_str()).x) * 0.5f);
            ImGui::TextColored(ImVec4(g_specColorOne[0], g_specColorOne[1], g_specColorOne[2], g_specColorOne[3]), "%s", text.c_str());
        }
        ImGui::PopFont();

        const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        const float left = pos.x, top = pos.y, right = pos.x + size.x, bottom = pos.y + size.y;
        const float midX = left + size.x * 0.5f, midY = top + size.y * 0.5f;
        const ImU32 clear = accent & ~IM_COL32_A_MASK;
        ImDrawList* wdl = ImGui::GetWindowDrawList();
        wdl->PushClipRectFullScreen();
        wdl->AddRectFilledMultiColor(ImVec2(left - 1.f, top - 1.f), ImVec2(midX + 1.f, top), accent, clear, clear, accent);
        wdl->AddRectFilledMultiColor(ImVec2(left - 1.f, top), ImVec2(left, midY + 1.f), accent, accent, clear, clear);
        wdl->AddRectFilledMultiColor(ImVec2(midX, bottom), ImVec2(right, bottom + 1.f), clear, accent, accent, clear);
        wdl->AddRectFilledMultiColor(ImVec2(right, midY), ImVec2(right + 1.f, bottom + 1.f), clear, clear, accent, accent);
        wdl->PopClipRect();
    }
    ImGui::End();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);
}

// style 22: skebob -- draw_spectator_list_skebob(). Bordered box, optional title, rounded
// avatar + diagonally-shadowed name per row (donor's 4-direction shadow offset, same look
// at a fraction of the draw calls of a blur).
static void DrawSkebob(ImDrawList* dl, ImGuiIO& io, const std::vector<SpecRow>& rows)
{
    if (rows.empty() && !g_specAlwaysShow) return;
    // botox skebob_arial_12 rows + skebob_arial_13 title, both DRAWN at 12, pixel-rounded
    ImFont* font = g_fontSkebobArial12 ? g_fontSkebobArial12 : ImGui::GetFont();
    ImFont* titleFont = g_fontSkebobArial13 ? g_fontSkebobArial13 : font;
    constexpr float px = 12.f, avatar = 20.f, rowH = 25.f, textX = 35.f, titleH = 40.f;
    const auto roundPx = [](float v) { return floorf(v + 0.5f); };
    const auto w12 = [&](ImFont* f, const char* s) { return f->CalcTextSizeA(px, FLT_MAX, 0.f, s).x; };

    float widest = w12(titleFont, "spectators") + 20.f;
    for (auto& r : rows) widest = (std::max)(widest, textX + w12(font, r.name.c_str()) + 10.f);
    const float w = (std::max)(210.f, widest);
    const float h = 30.f + rows.size() * rowH;
    float x, y; AnchorPos(io, w, h, g_specPos, 4.f, x, y);

    dl->AddRectFilled(ImVec2(x + 1.f, y + 1.f), ImVec2(x + w - 1.f, y + h - 1.f), IM_COL32(19, 19, 19, 242), 5.f);
    dl->AddRect(ImVec2(x + 1.f, y + 1.f), ImVec2(x + w - 1.f, y + h - 1.f), PackColor(g_specColorOne), 5.f, 0, 1.f);

    const float titleW = w12(titleFont, "spectators");
    const ImVec2 tp(x + (w - titleW) * 0.5f, y + 10.f);
    dl->AddText(titleFont, px, ImVec2(roundPx(tp.x + 1.f), roundPx(tp.y + 1.f)), IM_COL32(0, 0, 0, 255), "spectators");
    dl->AddText(titleFont, px, ImVec2(roundPx(tp.x), roundPx(tp.y)), IM_COL32(255, 255, 255, 255), "spectators");

    constexpr ImVec2 diag[4] = { { 1.f, 1.f }, { -1.f, -1.f }, { 1.f, -1.f }, { -1.f, 1.f } };
    for (size_t i = 0; i < rows.size(); ++i) {
        const float rowY = y + titleH - 3.f + i * rowH;
        const ImTextureID tex = rows[i].steamid64 ? Avatar_Get(rows[i].steamid64, false) : 0;
        if (tex) dl->AddImageRounded(tex, ImVec2(x + 10.f, rowY - 10.f), ImVec2(x + 30.f, rowY + 10.f), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 10.f);

        const float tx = x + textX, ty = rowY - 6.f;
        for (auto& o : diag) dl->AddText(font, px, ImVec2(roundPx(tx + o.x), roundPx(ty + o.y)), IM_COL32(0, 0, 0, 255), rows[i].name.c_str());
        dl->AddText(font, px, ImVec2(roundPx(tx), roundPx(ty)), IM_COL32(255, 255, 255, 255), rows[i].name.c_str());
    }
}

// style 30: howeweware -- draw_spectator_list_howeweware(). Dark rounded box, header
// gradient hairline, centred "Spectators" title + centred rows (ProggyClean, botox's own
// donor face here -- ImGui's built-in default, not Tahoma).
static void DrawHoweweware(ImDrawList* dl, ImGuiIO& io, const std::vector<SpecRow>& rows)
{
    if (rows.empty() && !g_specAlwaysShow) return;
    ImFont* font = g_fontLumiProggy13 ? g_fontLumiProggy13 : ImGui::GetFont();   // botox lumi_proggy_13
    constexpr float size = 13.f, line = size + 4.f, pad = 8.f;

    float widest = TextW(font, "Spectators");
    for (auto& r : rows) widest = (std::max)(widest, TextW(font, r.name));
    const float w = (std::max)(32.f, widest + pad * 2.f);
    const float h = (std::max)(32.f, 2.f + line + 4.f + line * rows.size() - 4.f + pad);
    float x, y; AnchorPos(io, w, h, g_specPos, 4.f, x, y);

    constexpr ImU32 bg = IM_COL32(15, 15, 15, 240), bgTop = IM_COL32(31, 31, 31, 240);
    constexpr ImU32 border = IM_COL32(110, 110, 128, 128), borderClear = IM_COL32(110, 110, 128, 0);

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), bg, 7.f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), border, 7.f, 0, 1.f);

    const float header = y + size + 5.f;
    dl->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x + w, header), bgTop, bgTop, bg, bg);
    dl->AddRectFilledMultiColor(ImVec2(x, header + 1.f), ImVec2(x + w * 0.5f, header), borderClear, border, border, borderClear);
    dl->AddRectFilledMultiColor(ImVec2(x + w * 0.5f, header + 1.f), ImVec2(x + w, header), border, borderClear, borderClear, border);

    const float titleW = TextW(font, "Spectators");
    dl->AddText(font, size, ImVec2(x + w * 0.5f - titleW * 0.5f, y + 2.f), IM_COL32(255, 255, 255, 255), "Spectators");

    const ImU32 ours = PackColor(g_specColorOne);
    for (size_t i = 0; i < rows.size(); ++i) {
        const float rowW = TextW(font, rows[i].name);
        dl->AddText(font, size, ImVec2(x + w * 0.5f - rowW * 0.5f, y + 2.f + line + 4.f + line * i), ours, rows[i].name.c_str());
    }
}

// Styles that need a steamid64 per row (avatar) instead of just a name.
static bool StyleNeedsRows(int style)
{
    switch (style) {
    case 5: case 6: case 7: case 8: case 10: case 12: case 17: case 18: case 21: case 22: case 30:
        return true;
    default:
        return false;
    }
}

// Styles that hardcode their x/y instead of calling AnchorPos. None of them push a clip rect
// or a BlurRect, so SpecDrag can just shift the vertices they added.
static bool StyleFixedPos(int style)
{
    switch (style) {
    case 1: case 8: case 9: case 12: case 13: case 16: case 19: case 24:
        return true;
    default:
        return false;
    }
}

// Drag to reposition, menu open only: LMB drag moves, RMB resets to the bare g_specPos anchor.
// Panel rect = bounds of the vertices this frame's Draw* added (from v0), so every style's
// real footprint is the hit box with no per-style size bookkeeping. Delta lands next frame.
static void SpecDrag(ImDrawList* dl, ImGuiIO& io, int v0)
{
    static bool s_drag = false;
    ImDrawVert* v = dl->VtxBuffer.Data;
    const int v1 = dl->VtxBuffer.Size;
    if (v1 <= v0) { s_drag = false; return; }

    ImVec2 mn(FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX);
    for (int i = v0; i < v1; ++i) { mn = ImMin(mn, v[i].pos); mx = ImMax(mx, v[i].pos); }

    if (StyleFixedPos(g_specStyle)) {
        // same on-screen clamp AnchorPos does, offset itself left untouched
        const float dx = floorf(fmaxf(-mn.x, fminf(g_specOffX, io.DisplaySize.x - mx.x)));
        const float dy = floorf(fmaxf(-mn.y, fminf(g_specOffY, io.DisplaySize.y - mx.y)));
        for (int i = v0; i < v1; ++i) { v[i].pos.x += dx; v[i].pos.y += dy; }
        mn.x += dx; mn.y += dy; mx.x += dx; mx.y += dy;
    }

    if (!g_showMenu) { s_drag = false; return; }

    const ImVec2 m = io.MousePos;
    const bool hovered = !GImGui->HoveredWindow && m.x >= mn.x && m.x <= mx.x && m.y >= mn.y && m.y <= mx.y;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) s_drag = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) s_drag = false;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { g_specOffX = g_specOffY = 0.f; s_drag = false; }

    if (s_drag) {
        // delta clamped at the screen edge, so dragging past it and back doesn't feel sticky
        g_specOffX += fmaxf(-mn.x, fminf(io.MouseDelta.x, io.DisplaySize.x - mx.x));
        g_specOffY += fmaxf(-mn.y, fminf(io.MouseDelta.y, io.DisplaySize.y - mx.y));
    }
    if (hovered || s_drag) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        dl->AddRect(ImVec2(mn.x - 2.f, mn.y - 2.f), ImVec2(mx.x + 2.f, mx.y + 2.f), ImGui::GetColorU32(ImGuiCol_Accent));
    }
}

void RenderSpectatorList()
{
    if (!g_specEnable) return;

    // chillware v2 is an ImGui::Begin() window, not a background-draw-list panel, so it
    // can legitimately want to draw while the collected list is empty (menu open / always
    // show) even though every OTHER style bails out below on an empty + not-always-show list.
    // It is already draggable as a real window, so SpecDrag skips it.
    if (StyleNeedsRows(g_specStyle)) {
        std::vector<SpecRow> rows = CollectSpectatorRows();
        if (g_specStyle == 21) { DrawChillwareV2(rows); return; }
        if (rows.empty() && g_showMenu) rows.push_back({ "spectator", 0 });   // something to grab
        if (rows.empty() && !g_specAlwaysShow) return;

        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const int v0 = dl->VtxBuffer.Size;
        switch (g_specStyle) {
        case 5:  DrawChillware(dl, io, rows);      break;
        case 6:  DrawLobotomy(dl, io, rows);       break;
        case 7:  DrawKamibebraList(dl, io, rows);  break;
        case 8:  DrawEyes(dl, io, rows);           break;
        case 10: DrawClarityV2List(dl, io, rows);  break;
        case 12: DrawBhopcheat(dl, io, rows);      break;
        case 17: DrawAirflowList(dl, io, rows);    break;
        case 18: DrawEvolveList(dl, io, rows);     break;
        case 22: DrawSkebob(dl, io, rows);         break;
        case 30: DrawHoweweware(dl, io, rows);     break;
        }
        SpecDrag(dl, io, v0);
        return;
    }

    std::vector<std::string> specs = CollectSpectators();
    if (specs.empty() && g_showMenu) specs.push_back("spectator");   // something to grab
    if (specs.empty() && !g_specAlwaysShow) return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const std::string localName = PlayerName(GetLocalPlayerIndex());
    const int v0 = dl->VtxBuffer.Size;

    switch (g_specStyle)
    {
    case 1:  DrawDelusional(dl, io, specs, localName);    break;
    case 2:  DrawInterwebz(dl, io, specs, localName);     break;
    case 3:  DrawWinXP(dl, io, specs, localName);         break;
    case 4:  DrawClarity(dl, io, specs, localName);       break;
    case 9:  DrawCumhacck(dl, io, specs, localName);      break;
    case 11: DrawAirplane(dl, io, specs, localName);      break;
    case 13: DrawMillionware(dl, io, specs, localName);   break;
    case 14: DrawAimware(dl, io, specs, localName);       break;
    case 15: DrawHavoc(dl, io, specs, localName);         break;
    case 16: DrawOnetap(dl, io, specs, localName);        break;
    case 19: DrawLegendware(dl, io, specs, localName);    break;
    case 20: DrawInterium(dl, io, specs, localName);      break;
    case 23: DrawCumidere(dl, io, specs, localName);      break;
    case 24: DrawIllusory(dl, io, specs, localName);      break;
    case 25: DrawLumiInterwebz(dl, io, specs, localName); break;
    case 26: DrawBillware(dl, io, specs, localName);      break;
    case 27: DrawInkabanium(dl, io, specs, localName);    break;
    case 28: DrawDna(dl, io, specs, localName);           break;
    case 29: DrawCucumber(dl, io, specs, localName);      break;
    case 31: DrawGamesenseList(dl, io, specs, localName); break;
    default: DrawDefault(dl, io, specs, localName);       break;   // style 0 + every not-ported index (32 kamidere)
    }
    SpecDrag(dl, io, v0);
}
