#include "menu_movement.h"
#include "menu.h"
#include "../movement/detections.h"   // g_jumpstatsShowFails

// Movement widgets: flat botox-style rows, continuing the "movement" group menu.cpp
// already opened (Page_MovementMain). Items with their own cog (edge bug style/mouse
// lock/sound, long jump adaptive cancel, auto strafe avoid walls,
// tung surf, auto bounce, bounce assist, edge skip, blockbot) were merged into their
// feature's cog in menu.cpp; pixel finder modes moved into the ps finder cog on the
// calculator page. Nothing dropped -- see report.
void Menu_Movement_Main()
{
    ImGui::Checkbox("nulls (opposing key cancel)", &g_nulls);

    ImGui::Checkbox("half sideways bind", &g_hsw);
    if (g_hsw)
        ImGui::OptionPopup("half sideways settings", [&]() {
            ImGui::Label("half sideways keybind");
            ImGui::Keybind("half sideways key", &g_hswKey, &g_hswMode);
            ImGui::Combo("side##hsw", &g_hswSide, "right\0left\0");
            ImGui::Checkbox("allow w/s##hsw", &g_hswAllowWS);
            ImGui::Checkbox("grounded half##hsw", &g_hswPreGround);
            if (g_hswPreGround)
                ImGui::Checkbox("allow a/d on ground##hsw", &g_hswAllowAD);
        }, ImVec2(220.f, -1));

    ImGui::Checkbox("delay hop", &g_delayHop);
    if (g_delayHop)
        ImGui::OptionPopup("delay hop settings", [&]() {
            ImGui::Label("delay hop keybind");
            ImGui::Keybind("delay hop key", &g_delayHopKey, &g_delayHopMode);
            ImGui::SliderInt("delay hop ticks", &g_delayHopTicks, 1, 64);
        }, ImVec2(220.f, -1));

    ImGui::Checkbox("auto goomba", &g_goomba);
    if (g_goomba)
        ImGui::OptionPopup("auto goomba settings", [&]() {
            ImGui::Label("auto goomba keybind");
            ImGui::Keybind("auto goomba key", &g_goombaKey, &g_goombaMode);
            ImGui::Combo("type##goomba", &g_goombaHow, "sticky jump\0rocket jump\0regular jump\0");
            // ping + interp come from their simtime and rocket flight from the ARJ sim; this is extra slack
            ImGui::SliderInt("lead ticks##goomba", &g_goombaLead, 0, 64);
        }, ImVec2(220.f, -1));

    ImGui::Checkbox("auto pogo", &g_autoPogo);
    if (g_autoPogo)
        ImGui::OptionPopup("auto pogo settings", [&]() {
            ImGui::Label("auto pogo keybind");
            ImGui::Keybind("auto pogo key", &g_autoPogoKey, &g_autoPogoMode);
        }, ImVec2(220.f, -1));

    ImGui::Checkbox("auto crouch", &g_autoCrouch);
    if (g_autoCrouch)
        ImGui::OptionPopup("auto crouch settings", [&]() {
            ImGui::Label("auto crouch keybind");
            ImGui::Keybind("auto crouch key", &g_autoCrouchKey, &g_autoCrouchMode);
            ImGui::SliderInt("auto crouch ticks", &g_autoCrouchTicks, 1, 32);
        }, ImVec2(220.f, -1));

    ImGui::Checkbox("double jump strafe", &g_djStrafe);
    if (g_djStrafe)
        ImGui::OptionPopup("double jump strafe settings", [&]() {
            ImGui::Label("double jump strafe keybind");
            ImGui::Keybind("double jump strafe key", &g_djStrafeKey, &g_djStrafeMode);
        }, ImVec2(220.f, -1));

    ImGui::Checkbox("smooth demo view", &g_silentView);

    ImGui::Checkbox("jumpstats", &g_jumpstats);
    if (g_jumpstats)
        ImGui::OptionPopup("jumpstats settings", [&]() {
            ImGui::Checkbox("show fails##jumpstats", &g_jumpstatsShowFails);
        }, ImVec2(220.f, -1));
}
