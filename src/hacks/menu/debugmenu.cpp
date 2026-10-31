#include "debugmenu.h"

void RenderDebugMenu()
{
    if (!g_debugMenuEnable) return;

    ImGui::SetNextWindowSize(ImVec2(240.f, 0.f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("botox debug", &g_debugMenuEnable, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("fps: %d", (int)(g_fps.load() + 0.5f));
    ImGui::Text("ping: %d ms", g_conn.load() ? g_ping.load() : 0);
    ImGui::Text("loss: %d%%", g_conn.load() ? g_loss.load() : 0);
    ImGui::Separator();
    ImGui::Text("in game: %s", IsInGame() ? "yes" : "no");
    ImGui::Text("local idx: %d", GetLocalPlayerIndex());
    ImGui::Text("hook depth: %d", g_hookDepth.load());
    ImGui::Text("misc hook depth: %d", g_miscHookDepth.load());
    ImGui::Text("eject: %s", g_eject.load() ? "true" : "false");

    ImGui::End();
}
