#include "misc_overlays.h"
#include "../movement/prediction.h"   // TickInterval
#include "../../render/dpi.h"          // botox logger: queued stretch block (dpi scale panels)
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>

// ==== healthshot effect =========================================================================
// botox on_healthshot: needs the trigger ticked, a live local player, duration clamped 0.1..5 s.
static std::atomic<unsigned long long> s_hsStart{ 0 }, s_hsEnd{ 0 };

void Healthshot_Trigger(int trigger)
{
    if (!g_hshotEnable || trigger < 0 || trigger > 2 || !(g_hshotTriggers & (1 << trigger))) return;
    if (!IsLocalPlayerAlive()) return;
    const unsigned long long now = GetTickCount64();
    const float duration = std::clamp(g_hshotDuration, 0.1f, 5.f);
    s_hsStart.store(now);
    s_hsEnd.store(now + (unsigned long long)(duration * 1000.f));
}

// CS:GO's effects/healthboost is a screen overlay over the edges for the boost time; TF2 has no
// such material, so: a blue-white edge glow that eases in over 0.15 s, breathes, and eases out
// over the last 40 %. Drawn on the background list = under every other overlay.
static void RenderHealthshot(ImDrawList* dl, const ImVec2& screen)
{
    const unsigned long long now = GetTickCount64(), start = s_hsStart.load(), end = s_hsEnd.load();
    if (!g_hshotEnable || now >= end || end <= start) return;
    if (!IsLocalPlayerAlive()) { s_hsEnd.store(0); return; }   // dead = the boost is gone

    const float total = (float)(end - start) / 1000.f, age = (float)(now - start) / 1000.f;
    const float t = age / total;
    float k = std::min(age / 0.15f, 1.f) * (t > 0.6f ? (1.f - t) / 0.4f : 1.f);
    k *= 0.85f + 0.15f * sinf(age * 6.2831853f * 1.5f);
    if (k <= 0.f) return;

    const float edge = std::min(screen.x, screen.y) * 0.22f;
    const ImU32 on  = IM_COL32(110, 190, 255, (int)(150.f * k));
    const ImU32 off = IM_COL32(110, 190, 255, 0);
    dl->AddRectFilledMultiColor(ImVec2(0.f, 0.f), ImVec2(screen.x, edge), on, on, off, off);
    dl->AddRectFilledMultiColor(ImVec2(0.f, screen.y - edge), screen, off, off, on, on);
    dl->AddRectFilledMultiColor(ImVec2(0.f, 0.f), ImVec2(edge, screen.y), on, off, off, on);
    dl->AddRectFilledMultiColor(ImVec2(screen.x - edge, 0.f), screen, off, on, on, off);
}

// ==== low fps warning: botox n_indicators::fps_warning ==========================================
static void RenderFpsWarning(ImDrawList* dl, const ImVec2& screen)
{
    static float anim = 0.f;
    const ImGuiIO& io = ImGui::GetIO();
    const int tickrate = (int)(1.f / TickInterval() + 0.5f);
    const bool on = g_fpsWarning && IsInGame() && GetLocalPlayerIndex() > 0;
    const bool active = on && (int)(io.Framerate + 0.5f) < tickrate;
    anim = std::clamp(anim + 2.f * io.DeltaTime * (active ? 2.f : -2.f), 0.f, 1.f);   // ImAnimationHelper Update(2, +-2)
    if (!on || anim <= 0.f) return;

    ImFont* bold = g_fontTahomaBd12 ? g_fontTahomaBd12 : ImGui::GetFont();
    ImFont* reg  = g_fontTahoma12 ? g_fontTahoma12 : ImGui::GetFont();
    const auto shadowed = [&](ImFont* f, float x, float y, ImU32 col, const char* s) {
        dl->AddText(f, f->FontSize, ImVec2(floorf(x) + 1.f, floorf(y) + 1.f), IM_COL32(0, 0, 0, (int)(255.f * anim)), s);
        dl->AddText(f, f->FontSize, ImVec2(floorf(x), floorf(y)), col, s);
    };

    const char* warn = "LOW FPS";
    const ImVec2 ws = bold->CalcTextSizeA(bold->FontSize, FLT_MAX, 0.f, warn);
    const float cx = floorf(screen.x * 0.5f);
    shadowed(bold, cx - ws.x * 0.5f, 200.f, IM_COL32(255, 255, 255, (int)(255.f * anim)), warn);
    shadowed(bold, cx - ws.x, 200.f, IM_COL32(255, 0, 0, 255), "!");
    shadowed(bold, cx + ws.x, 200.f, IM_COL32(255, 0, 0, 255), "!");

    char line[64];
    snprintf(line, sizeof(line), "current fps: %d needed fps: %d+", (int)(io.Framerate + 0.5f), tickrate);
    const ImVec2 ls = reg->CalcTextSizeA(reg->FontSize, FLT_MAX, 0.f, line);
    shadowed(reg, cx - ls.x * 0.5f, 215.f, IM_COL32(178, 178, 178, (int)(255.f * anim)), line);
}

// ==== displayed logs: botox g_logger + the [prefix] console line ================================
namespace
{
    struct LogLine { std::string text, prefix; float time; };
    std::mutex s_logMtx;               // FireEvent thread pushes, the frame build reads
    std::deque<LogLine> s_logs;

    // IVEngineClient::GetPlayerInfo slot 8, TF2 player_info_t (same layout spectator_list.cpp reads)
    struct LogPlayerInfo_t
    {
        char name[32]; int userID; char guid[33]; uint32_t friendsID; char friendsName[32];
        bool fakeplayer; bool ishltv; uint32_t customFiles[4]; uint8_t filesDownloaded;
    };
    std::string PlayerName(int idx)
    {
        LogPlayerInfo_t pi = {};
        if (!g_engine || idx < 1 || !VCall<bool>(g_engine, 8, idx, &pi)) return "unknown";
        pi.name[sizeof(pi.name) - 1] = 0;
        return std::string(pi.name).substr(0, 24);
    }

    int TeamOf(int idx)
    {
        void* e = GetClientEntity(idx);
        if (!e || g_offTeamNum <= 0) return -1;
        __try { return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(e) + g_offTeamNum); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }

    void Log(const char* prefix, const std::string& screen, const std::string& console)
    {
        {
            std::lock_guard<std::mutex> lock(s_logMtx);
            s_logs.push_back({ screen, prefix, 8.f });
        }
        // botox: accent "[prefix] " then the line in console text colour
        ConsoleColorPrint((uint8_t)(g_accentColor[0] * 255.f), (uint8_t)(g_accentColor[1] * 255.f),
                          (uint8_t)(g_accentColor[2] * 255.f), (std::string(prefix) + " ").c_str());
        ConsoleColorPrint(200, 200, 200, (console + "\n").c_str());
        DebugLog("LOG %s %s", prefix, screen.c_str());
    }
}

void MiscLogs_Push(const char* prefix, const char* text)
{
    Log(prefix, text, text);
}

void MiscLogs_OnGameEvent(void* pEvent, const char* name, int localIdx)
{
    // healthshot trigger 0 (botox fire_event_intern player_death: you killed someone else)
    if (strcmp(name, "player_death") == 0)
    {
        const int victim   = GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "userid", 0));
        const int attacker = GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "attacker", 0));
        if (attacker == localIdx && victim > 0 && victim != localIdx) Healthshot_Trigger(0);
        return;
    }
    if (!g_logTypes) return;

    // TF2 player_hurt: userid, health, attacker, damageamount (tf_player.cpp:10760). No hitgroup /
    // weapon name in it, so the line is botox's minus those.
    if (strcmp(name, "player_hurt") == 0)
    {
        if (!(g_logTypes & 1)) return;
        const int victim   = GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "userid", 0));
        const int attacker = GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "attacker", 0));
        if (attacker < 1 || victim < 1 || attacker == victim) return;
        if (TeamOf(attacker) == TeamOf(victim)) return;   // team hits not logged
        const int dmg = IGameEventVtbl::GetInt(pEvent, "damageamount", 0);
        const int hp  = IGameEventVtbl::GetInt(pEvent, "health", 0);
        char s[160], c[200];
        if (victim == localIdx)
        {
            const std::string who = PlayerName(attacker);
            snprintf(s, sizeof(s), "%s hit you for %d", who.c_str(), dmg);
            snprintf(c, sizeof(c), "%s hit you | dmg: %d hp | %d health remaining", who.c_str(), dmg, hp);
            Log("[hurt]", s, c);
        }
        else if (attacker == localIdx)
        {
            const std::string who = PlayerName(victim);
            snprintf(s, sizeof(s), "hit %s for %d", who.c_str(), dmg);
            snprintf(c, sizeof(c), "hit %s | dealt: %d hp | %d health remaining", who.c_str(), dmg, hp);
            Log("[damage]", s, c);
        }
        return;
    }

    // vote_cast: vote_option, team, entityid (c_vote_controller.cpp:173 / hud_vote.cpp)
    if ((g_logTypes & 2) && strcmp(name, "vote_cast") == 0)
    {
        const int who = IGameEventVtbl::GetInt(pEvent, "entityid", 0);
        const int opt = IGameEventVtbl::GetInt(pEvent, "vote_option", -1);
        const std::string n = PlayerName(who);
        char s[160];
        if (opt == 0 || opt == 1) snprintf(s, sizeof(s), "%s voted %s", n.c_str(), opt == 0 ? "yes" : "no");
        else                      snprintf(s, sizeof(s), "%s voted option %d", n.c_str(), opt + 1);
        Log("[vote]", s, s);
    }
}

// botox n_logger::on_paint_traverse: x 10, y 5 (m_last_spectators_y default), 15 px rows, 8 s,
// last 0.5 s fades and the top row slides up through its last 0.1 s, max 16 kept.
static void RenderLogs(ImDrawList* dl)
{
    std::lock_guard<std::mutex> lock(s_logMtx);
    const float dt = ImGui::GetIO().DeltaTime;
    for (size_t i = 0; i < s_logs.size();)
    {
        s_logs[i].time -= dt;
        if (s_logs[i].time <= 0.f || i > 15) { s_logs.erase(s_logs.begin() + i); continue; }
        ++i;
    }
    if (s_logs.empty()) return;
    DpiPanelScope block(dl);

    ImFont* bold = g_fontTahomaBd12 ? g_fontTahomaBd12 : ImGui::GetFont();
    ImFont* reg  = g_fontTahoma12 ? g_fontTahoma12 : ImGui::GetFont();
    const float x = 10.f, size = 15.f;
    float y = 5.f;
    for (size_t i = 0; i < s_logs.size(); ++i)
    {
        const LogLine& l = s_logs[i];
        float a = 1.f;
        if (l.time < 0.5f)
        {
            a = std::clamp(l.time, 0.f, 0.5f) / 0.5f;
            if (i == 0 && a < 0.2f) y -= size * (1.f - a / 0.2f);
        }
        const ImU32 shadow = IM_COL32(0, 0, 0, (int)(255.f * a));
        const ImU32 accent = ImColor(g_accentColor[0], g_accentColor[1], g_accentColor[2], a);
        dl->AddText(bold, bold->FontSize, ImVec2(x + 1.f, floorf(y) + 1.f), shadow, l.prefix.c_str());
        dl->AddText(bold, bold->FontSize, ImVec2(x, floorf(y)), accent, l.prefix.c_str());
        const float px = x + bold->CalcTextSizeA(bold->FontSize, FLT_MAX, 0.f, l.prefix.c_str()).x + 3.f;
        dl->AddText(reg, reg->FontSize, ImVec2(px + 1.f, floorf(y) + 1.f), shadow, l.text.c_str());
        dl->AddText(reg, reg->FontSize, ImVec2(px, floorf(y)), IM_COL32(255, 255, 255, (int)(255.f * a)), l.text.c_str());
        y += size;
    }
}

void RenderMiscOverlays()
{
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    if (screen.x <= 0.f || screen.y <= 0.f) return;
    RenderHealthshot(ImGui::GetBackgroundDrawList(), screen);
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    RenderFpsWarning(fg, screen);
    RenderLogs(fg);
}

// ==== console color: botox draw_set_color.cpp ===================================================
// ISurface::DrawSetColor(int, int, int, int) = slot 11: Amalgam ISurface.h, IAppSystem's 5 then
// Shutdown..PopMakeCurrent, the two DrawSetColor overloads at 10/11 in MSVC's reversed overload
// order (Color at 10). Cross-check: the same count puts SetCursor at 51 and LockCursor at 62,
// exactly the slots Amalgam hooks (Hooks/Direct3DDevice9.cpp).
namespace
{
    using DrawSetColor_t = void(__fastcall*)(void*, int, int, int, int);
    DrawSetColor_t o_DrawSetColor = nullptr;
    bool s_consoleDrawing = false;      // vgui paint thread only, set around the console's PaintTraverse
    uintptr_t s_consolePanel = 0, s_completionPanel = 0;

    void __fastcall h_DrawSetColor(void* surface, int r, int g, int b, int a)
    {
        if (s_consoleDrawing && g_consoleTintOn && !g_eject)
        {
            r = r * (int)(g_consoleTint[0] * 255.f) / 255;
            g = g * (int)(g_consoleTint[1] * 255.f) / 255;
            b = b * (int)(g_consoleTint[2] * 255.f) / 255;
            a = a * (int)(g_consoleTint[3] * 255.f) / 255;
        }
        o_DrawSetColor(surface, r, g, b, a);
    }
}

void ConsoleTint_Install()
{
    static const char* const kNames[] = { "VGUI_Surface031", "VGUI_Surface030", nullptr };
    void* surface = GetInterfaceAny("vguimatsurface.dll", kNames);
    if (!surface) { DebugLog("console color: VGUI_Surface not found"); return; }
    InstallVTableHook(surface, 11, "console color: DrawSetColor", (void*)&h_DrawSetColor, (void**)&o_DrawSetColor);
}

// botox paint_traverse: GameConsole + CompletionList (the console's autocomplete popup)
bool ConsoleTint_Begin(void* panels, uintptr_t vpanel)
{
    if (!g_consoleTintOn || !o_DrawSetColor || !panels) return false;
    if (vpanel != s_consolePanel && vpanel != s_completionPanel)
    {
        if (s_consolePanel && s_completionPanel) return false;
        // IPanel::GetName, slot 36 (render.cpp VGui_PanelName)
        using GetName_t = const char* (*)(void*, uintptr_t);
        const char* nm = ((GetName_t)(*(void***)panels)[36])(panels, vpanel);
        if (!nm) return false;
        if (!s_consolePanel && strcmp(nm, "GameConsole") == 0) { s_consolePanel = vpanel; DebugLog("console color: GameConsole panel found"); }
        else if (!s_completionPanel && strcmp(nm, "CompletionList") == 0) s_completionPanel = vpanel;
        else return false;
    }
    s_consoleDrawing = true;
    return true;
}

void ConsoleTint_End() { s_consoleDrawing = false; }
