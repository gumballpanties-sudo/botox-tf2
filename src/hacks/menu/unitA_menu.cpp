#include "unitA_menu.h"
#include "menu.h"

// Unit A widgets: flat botox-style rows, continuing the "movement" group menu.cpp
// already opened (Page_MovementMain). Items with their own cog (edge bug style/mouse
// lock/sound, long jump adaptive cancel, auto strafe avoid walls, jumpbug kangaroo,
// tung surf, auto bounce, bounce assist, edge skip, blockbot) were merged into their
// feature's cog in menu.cpp; pixel finder modes moved into the ps finder cog on the
// calculator page. Nothing dropped -- see report.
void UnitA_Menu_movement_main()
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

    ImGui::Checkbox("auto crouch", &g_autoCrouch);
    if (g_autoCrouch)
        ImGui::OptionPopup("auto crouch settings", [&]() {
            ImGui::Label("auto crouch keybind");
            ImGui::Keybind("auto crouch key", &g_autoCrouchKey, &g_autoCrouchMode);
            ImGui::SliderInt("auto crouch ticks", &g_autoCrouchTicks, 1, 32);
        }, ImVec2(220.f, -1));

    ImGui::Checkbox("smooth demo view", &g_silentView);

    ImGui::Checkbox("jumpstats", &g_jumpstats);
    if (g_jumpstats)
        ImGui::OptionPopup("jumpstats", [&]() {
            int bhop = 0, lj = 0, mj = 0, wc = 0;
            MV_GetJumpstats(bhop, lj, mj, wc);
            ImGui::Text("bhop %d  lj %d  mj %d  wallclimb %d", bhop, lj, mj, wc);
        }, ImVec2(220.f, -1));
}
