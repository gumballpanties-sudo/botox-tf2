#include "unitD_menu.h"
#include "menu.h"          // ImGui::OptionPopup
#include "../screen/screenfx_passes.h"
#include "../visuals/world_fx.h"

// Called inside menu.cpp's "screen" group (Page_VisualsScreen, col1). Labels match
// botox's own menu_tab_visuals.cpp case 2 where the feature exists there.
void UnitD_Menu_visuals_screen()
{
    ImGui::Checkbox("third person", &g_thirdPersonEnable);
    if (g_thirdPersonEnable)
        // no wall collision yet -- botox's "wall collision" sub-toggle is not ported.
        ImGui::OptionPopup("third person settings", [&]() {
            ImGui::SliderFloat("distance##thirdperson", &g_thirdPersonDistance, 0.f, 400.f, "%.0f");
        }, ImVec2(210.f, -1));

    ImGui::Checkbox("aspect ratio", &g_aspectRatioEnable);
    if (g_aspectRatioEnable)
        // botox menu_tab_visuals.cpp:1157 -- slider edits a pending value, apply commits it.
        // botox's stretch hud/crosshair/cheat overlay toggles not ported: TF2's vgui hud is
        // not touched by r_aspectratio, and the crosshair/overlay draws live in other files.
        ImGui::OptionPopup("aspect ratio settings", [&]() {
            static float pending_ratio = 0.f;
            static float applied_ratio = 0.f;
            if (pending_ratio <= 0.f || applied_ratio != g_aspectRatioValue) {
                pending_ratio = g_aspectRatioValue;
                applied_ratio = g_aspectRatioValue;
            }
            ImGui::SliderFloat("ratio##aspect ratio", &pending_ratio, 0.5f, 4.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::Button("apply##aspect ratio", ImVec2(-1.f, 15.f))) {
                g_aspectRatioValue = pending_ratio;
                applied_ratio      = pending_ratio;
            }
        }, ImVec2(210.f, -1));

    ImGui::Checkbox("sharpen", &g_sharpenEnable);
    if (g_sharpenEnable)
        ImGui::OptionPopup("sharpen settings", [&]() {
            ImGui::SliderFloat("amount##sharpen", &g_sharpenAmount, 0.f, 2.f, "%.2f");
        }, ImVec2(210.f, -1));

    ImGui::Checkbox("film grain", &g_filmGrainEnable);
    if (g_filmGrainEnable)
        ImGui::OptionPopup("film grain settings", [&]() {
            ImGui::SliderFloat("amount##grain", &g_filmGrainAmount, 0.f, 0.5f, "%.3f");
        }, ImVec2(210.f, -1));

    ImGui::Checkbox("vignette", &g_vignetteEnable);
    if (g_vignetteEnable)
        ImGui::OptionPopup("vignette settings", [&]() {
            ImGui::SliderFloat("amount##vignette", &g_vignetteAmount, 0.f, 1.f, "%.2f");
            ImGui::Checkbox("invert##vignette", &g_vignetteInvert);
        }, ImVec2(210.f, -1));

    ImGui::Checkbox("invert filter", &g_invertFilterEnable);

    ImGui::Checkbox("posterize", &g_posterizeEnable);
    if (g_posterizeEnable)
        ImGui::OptionPopup("posterize settings", [&]() {
            ImGui::SliderFloat("levels##posterize", &g_posterizeLevels, 2.f, 32.f, "%.0f");
        }, ImVec2(210.f, -1));

    ImGui::Checkbox("deband", &g_debandEnable);
    if (g_debandEnable)
        ImGui::OptionPopup("deband settings", [&]() {
            ImGui::SliderFloat("amount##deband", &g_debandAmount, 0.f, 32.f, "%.1f");
        }, ImVec2(210.f, -1));

    ImGui::Checkbox("channel shift", &g_channelShiftEnable);
    if (g_channelShiftEnable)
        ImGui::OptionPopup("channel shift settings", [&]() {
            ImGui::SliderFloat("amount##chshift", &g_channelShiftAmount, 0.f, 10.f, "%.1f");
        }, ImVec2(210.f, -1));

    // emphasize + show depth: botox menu_tab_visuals.cpp 1395-1460, 1:1 (depth_view.cpp knobs)
    constexpr ImGuiColorEditFlags k_noAlpha = ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_InputRGB |
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop |
        ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoBorder | ImGuiColorEditFlags_NoAlpha;

    ImGui::Checkbox("emphasize", &g_emphasizeEnable);
    if (g_emphasizeEnable)
        ImGui::OptionPopup("emphasize settings", [&]() {
            ImGui::SliderFloat("focus depth##emphasize", &g_emphasizeFocusDepth, 0.f, 1.f, "%.3f");
            ImGui::SliderFloat("focus range##emphasize", &g_emphasizeFocusRange, 0.f, 1.f, "%.3f");
            ImGui::SliderFloat("focus edge##emphasize", &g_emphasizeFocusEdge, 0.f, 1.f, "%.3f");

            ImGui::Checkbox("spherical##emphasize", &g_emphasizeSpherical);
            if (g_emphasizeSpherical) {
                ImGui::SliderInt("sphere fov##emphasize", &g_emphasizeSphereFov, 1, 180, "%d deg");
                ImGui::SliderFloat("focus x##emphasize", &g_emphasizeSphereX, 0.f, 1.f, "%.3f");
                ImGui::SliderFloat("focus y##emphasize", &g_emphasizeSphereY, 0.f, 1.f, "%.3f");
            }

            ImGui::ColorRow("blend color##emphasize", g_emphasizeBlendColor, k_noAlpha);
            ImGui::SliderFloat("blend factor##emphasize", &g_emphasizeBlendFactor, 0.f, 1.f, "%.3f");
            ImGui::SliderFloat("effect factor##emphasize", &g_emphasizeAmount, 0.f, 1.f, "%.3f");
        }, ImVec2(210.f, -1));

    ImGui::Checkbox("show depth", &g_showDepthEnable);
    if (g_showDepthEnable)
        ImGui::OptionPopup("show depth settings", [&]() {
            ImGui::Combo("present type##show depth", &g_showDepthType, "depth map\0" "normal map\0" "show both\0");
            ImGui::Checkbox("blend into image##show depth", &g_showDepthBlend);

            ImGui::Checkbox("live preview##show depth", &g_showDepthLive);
            if (g_showDepthLive) {
                ImGui::Checkbox("upside down##show depth", &g_showDepthUpsideDown);
                ImGui::Checkbox("reversed##show depth", &g_showDepthReversed);
                ImGui::Checkbox("logarithmic##show depth", &g_showDepthLog);
                ImGui::SliderFloat("scale x##show depth", &g_showDepthScaleX, 0.f, 2.f, "%.3f");
                ImGui::SliderFloat("scale y##show depth", &g_showDepthScaleY, 0.f, 2.f, "%.3f");

                const ImVec2 screen = ImGui::GetIO().DisplaySize;
                ImGui::SliderInt("offset x##show depth", &g_showDepthOffsetX, -(int)screen.x, (int)screen.x, "%d px");
                ImGui::SliderInt("offset y##show depth", &g_showDepthOffsetY, -(int)screen.y, (int)screen.y, "%d px");

                ImGui::SliderFloat("far plane##show depth", &g_showDepthFarPlane, 0.f, 1000.f, "%.1f");
                ImGui::SliderFloat("multiplier##show depth", &g_showDepthMultiplier, 0.f, 1000.f, "%.3f");
            }
        }, ImVec2(210.f, -1));
}

// Called inside menu.cpp's "world" group (Page_VisualsWorld, col1).
void UnitD_Menu_visuals_world()
{
    ImGui::Checkbox("world fov", &g_worldFovEnable);
    if (g_worldFovEnable)
        ImGui::OptionPopup("world fov settings", [&]() {
            ImGui::SliderFloat("fov##worldfov", &g_worldFovAmount, 60.f, 140.f, "%.0f");
        }, ImVec2(200.f, -1));

    ImGui::Checkbox("fullbright", &g_fullbrightEnable);

    ImGui::Checkbox("dlight", &g_dlightEnable);
    if (g_dlightEnable)
        ImGui::OptionPopup("dlight settings", [&]() {
            ImGui::ColorEdit4("colour##dlight", g_dlightColor);
            ImGui::SliderFloat("radius##dlight", &g_dlightRadius, 10.f, 500.f, "%.0f");
        }, ImVec2(220.f, -1));

    // moves env_sun's glow sprite only -- baked world lighting is unaffected.
    ImGui::Checkbox("sun angle", &g_sunAngleEnable);
    if (g_sunAngleEnable)
        ImGui::OptionPopup("sun angle settings", [&]() {
            ImGui::SliderFloat("pitch##sunangle", &g_sunAnglePitch, -90.f, 90.f, "%.0f");
            ImGui::SliderFloat("yaw##sunangle", &g_sunAngleYaw, 0.f, 360.f, "%.0f");
        }, ImVec2(220.f, -1));

    ImGui::Checkbox("world texture colour", &g_worldTintEnable);
    if (g_worldTintEnable)
        ImGui::ColorRow("colour##worldtint", g_worldTintColor);

    ImGui::Checkbox("prop texture colour", &g_propTintEnable);
    if (g_propTintEnable)
        ImGui::ColorRow("colour##proptint", g_propTintColor);

    // rain/snow sprite only. custom blood colour is not ported -- blood is a particle
    // system, not a single swappable material.
    ImGui::Checkbox("weather particle colour", &g_weatherTintEnable);
    if (g_weatherTintEnable)
        ImGui::ColorRow("colour##weathertint", g_weatherTintColor);

    ImGui::Checkbox("minecraft sky + clouds", &g_mcWorldEnable);
}
