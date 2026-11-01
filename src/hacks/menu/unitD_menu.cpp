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
            ImGui::Checkbox("depth based##chshift", &g_channelShiftDepth);
            if (g_channelShiftDepth) {
                ImGui::Combo("depth side##chshift", &g_channelShiftDepthMode, "background\0" "foreground\0" "both sides\0");
                ImGui::SliderFloat("distance##chshift", &g_channelShiftDepthDist, 0.f, 4000.f, "%.0f u");
                ImGui::SliderFloat("range##chshift", &g_channelShiftDepthRange, 0.f, 2000.f, "%.0f u");
                ImGui::SliderFloat("ramp##chshift", &g_channelShiftDepthRamp, 1.f, 4000.f, "%.0f u");
            }
        }, ImVec2(210.f, -1));

    ImGui::Checkbox("depth distort", &g_warpEnable);
    if (g_warpEnable)
        ImGui::OptionPopup("depth distort settings", [&]() {
            ImGui::Combo("effect##warp", &g_warpEffect,
                         "bulge / pinch\0" "swirl\0" "spliced radials\0" "zigzag\0" "ripple\0" "wave\0" "drunk\0"
                         "tiny planet\0" "slit scan\0" "radial slit scan\0");
            const int e = g_warpEffect;
            const bool radial = e <= 4;

            static const char* k_anim[] = { "no\0" "yes\0", "no\0" "amplitude\0" "phase\0", "no\0" "amplitude\0" "phase\0" "angle\0" };
            const int animKind = (e == 3 || e == 4) ? 1 : (e == 5 ? 2 : 0);
            if (e != 6 && e != 7) {
                g_warpAnimate = std::clamp(g_warpAnimate, 0, animKind + 1);
                ImGui::Combo("animate##warp", &g_warpAnimate, k_anim[animKind]);
                if (g_warpAnimate)
                    ImGui::SliderFloat("animation speed##warp", &g_warpAnimSpeed, 0.05f, 10.f, "%.2f");
            }

            ImGui::Label("properties");
            switch (e) {
            case 0:
                ImGui::SliderFloat("magnitude##warp", &g_warpMagnitude, -1.f, 1.f, "%.2f");
                break;
            case 1:
            case 2:
                ImGui::SliderFloat("angle##warp", &g_warpAngle, -1800.f, 1800.f, "%.0f deg");
                ImGui::SliderFloat("inner radius##warp", &g_warpInnerRadius, 0.f, 1.f, "%.3f");
                if (e == 1) ImGui::Checkbox("inverse angle##warp", &g_warpInverse);
                else        ImGui::SliderInt("splices##warp", &g_warpSplices, 1, 50);
                break;
            case 3:
            case 4:
                ImGui::SliderFloat("angle##warp", &g_warpAngle, -999.f, 999.f, "%.0f");
                ImGui::SliderFloat("period##warp", &g_warpPeriod, 0.1f, 10.f, "%.2f");
                ImGui::SliderFloat("amplitude##warp", &g_warpAmplitude, -10.f, 10.f, "%.2f");
                ImGui::SliderFloat("phase##warp", &g_warpPhase, -5.f, 5.f, "%.2f");
                break;
            case 5:
                ImGui::Combo("wave type##warp", &g_warpWaveType, "x/x\0" "x/y\0");
                ImGui::SliderFloat("angle##warp", &g_warpWaveAngle, -360.f, 360.f, "%.0f deg");
                ImGui::SliderFloat("period##warp", &g_warpWavePeriod, 0.1f, 10.f, "%.2f");
                ImGui::SliderFloat("amplitude##warp", &g_warpWaveAmplitude, -1.f, 1.f, "%.3f");
                ImGui::SliderFloat("phase##warp", &g_warpPhase, -5.f, 5.f, "%.2f");
                break;
            case 6:
                ImGui::SliderFloat("strength x##warp", &g_warpDrunkStrength[0], 0.f, 100.f, "%.1f");
                ImGui::SliderFloat("strength y##warp", &g_warpDrunkStrength[1], 0.f, 100.f, "%.1f");
                ImGui::SliderFloat("speed##warp", &g_warpDrunkSpeed[0], 0.f, 10.f, "%.1f");
                ImGui::SliderFloat("bending angle##warp", &g_warpDrunkAngle, -1800.f, 1800.f, "%.0f deg");
                ImGui::SliderFloat("bending speed##warp", &g_warpDrunkAngleSpeed, 0.f, 10.f, "%.1f");
                break;
            case 7:
                ImGui::SliderFloat("scale##warp", &g_warpPlanetScale, 0.f, 10.f, "%.2f");
                ImGui::SliderFloat("rotation x##warp", &g_warpPlanetRot[0], 0.f, 1.f, "%.3f");
                ImGui::SliderFloat("rotation y##warp", &g_warpPlanetRot[1], 0.f, 1.f, "%.3f");
                ImGui::SliderFloat("rotation z##warp", &g_warpPlanetRot[2], 0.f, 360.f, "%.1f deg");
                ImGui::SliderFloat("offset x##warp", &g_warpPlanetOffset[0], -0.5f, 0.5f, "%.3f");
                ImGui::SliderFloat("offset y##warp", &g_warpPlanetOffset[1], -0.5f, 0.5f, "%.3f");
                ImGui::SliderFloat("seam blending##warp", &g_warpPlanetSeam, 0.5f, 1.f, "%.3f");
                break;
            case 8:
                ImGui::Combo("direction##warp", &g_warpSlitDir, "left\0" "right\0" "up\0" "down\0");
                if (!g_warpAnimate) ImGui::SliderFloat("position##warp", &g_warpSlitPos, 0.f, 1.f, "%.3f");
                ImGui::SliderFloat("scan speed##warp", &g_warpSlitSpeed, 0.f, 3.f, "%.2f");
                break;
            case 9:
                if (!g_warpAnimate) ImGui::SliderFloat("position##warp", &g_warpSlitPos, 0.f, 1.f, "%.3f");
                ImGui::ColorRow("border color##warp", g_warpBorderColor);
                break;
            }

            if (radial || e == 9) {
                ImGui::SliderFloat("center x##warp", &g_warpCenter[0], 0.f, 1.f, "%.3f");
                ImGui::SliderFloat("center y##warp", &g_warpCenter[1], 0.f, 1.f, "%.3f");
            }
            if (radial) {
                ImGui::SliderFloat("radius##warp", &g_warpRadius, 0.f, 1.f, "%.3f");
                ImGui::SliderFloat("tension##warp", &g_warpTension, 0.f, 10.f, "%.3f");
                ImGui::SliderFloat("aspect ratio##warp", &g_warpAspect, -100.f, 100.f, "%.1f");
                ImGui::SliderFloat("aspect angle##warp", &g_warpAspectAngle, -180.f, 180.f, "%.1f deg");
                ImGui::Checkbox("use offset coordinates##warp", &g_warpUseOffset);
                if (g_warpUseOffset) {
                    ImGui::SliderFloat("offset x##warp", &g_warpOffset[0], 0.f, 1.f, "%.3f");
                    ImGui::SliderFloat("offset y##warp", &g_warpOffset[1], 0.f, 1.f, "%.3f");
                }
            }

            ImGui::Label("blending");
            ImGui::Combo("blending mode##warp", &g_warpBlend,
                         "normal\0" "darken\0" "  multiply\0" "  color burn\0" "  linear burn\0" "lighten\0" "  screen\0"
                         "  color dodge\0" "  linear dodge\0" "  addition\0" "  glow\0" "overlay\0" "  soft light\0"
                         "  hard light\0" "  vivid light\0" "  linear light\0" "  pin light\0" "  hard mix\0" "difference\0"
                         "  exclusion\0" "subtract\0" "  divide\0" "  divide (alternative)\0" "  divide (photoshop)\0"
                         "  reflect\0" "  grain extract\0" "  grain merge\0" "hue\0" "  saturation\0" "  color\0"
                         "  luminosity\0");
            ImGui::SliderFloat("opacity##warp", &g_warpOpacity, 0.f, 1.f, "%.2f");

            ImGui::Label("depth");
            ImGui::Combo("depth side##warp", &g_warpDepthMode, "background\0" "foreground\0" "both sides\0");
            ImGui::SliderFloat("distance##warp", &g_warpDepthDist, 0.f, 4000.f, "%.0f u");
            ImGui::SliderFloat("range##warp", &g_warpDepthRange, 0.f, 2000.f, "%.0f u");
            ImGui::SliderFloat("ramp##warp", &g_warpDepthRamp, 1.f, 4000.f, "%.0f u");
            if (e <= 7) {
                ImGui::Checkbox("fade transition##warp", &g_warpFade);
                ImGui::Checkbox("protect edges##warp", &g_warpDepthProtect);
            }
        }, ImVec2(230.f, -1));

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
