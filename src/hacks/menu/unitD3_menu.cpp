#include "unitD3_menu.h"
#include "menu.h"   // ImGui::OptionPopup

// Called inside menu.cpp's "screen" group (Page_VisualsScreen, col1). Minecraft sky
// moved to UnitD_Menu_visuals_world -- it's a skybox variant, botox groups it with world.
void UnitD3_Menu_visuals_screen()
{
    ImGui::Checkbox("reflections", &g_reflEnable);
    if (g_reflEnable)
        ImGui::OptionPopup("reflections settings", [&]() {
            ImGui::SliderFloat("intensity##refl", &g_reflIntensity, 0.f, 1.f, "%.2f");
            ImGui::SliderFloat("distance##refl", &g_reflDistance, 0.f, 1.f, "%.2f");
        }, ImVec2(210.f, -1));
}
