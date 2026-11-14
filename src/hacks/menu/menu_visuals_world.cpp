#include "menu_visuals_world.h"
#include "menu.h"          // ImGui::OptionPopup
#include "../screen/screenfx_passes.h"
#include "../visuals/world_fx.h"
#include "../visuals/world_fx3_math.h"   // world/prop texture presets + categories

// Called inside menu.cpp's "screen" group (Page_VisualsScreen, col1): botox
// menu_tab_visuals.cpp (aspect ratio; third person moved to misc > game). Colour correction
// follows in menu.cpp, then Menu_Visuals_ScreenFx (sharpen .. show depth).
void Menu_Visuals_Camera()
{
    ImGui::Checkbox("aspect ratio", &g_aspectRatioEnable);
    if (g_aspectRatioEnable)
        // slider edits a pending value, apply commits it (botox: r_aspectratio re-lays the view)
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

            ImGui::Checkbox("stretch hud##aspect ratio", &g_sfxAspectHud);
            ImGui::Checkbox("stretch crosshair##aspect ratio", &g_sfxAspectCrosshair);
            ImGui::Checkbox("stretch cheat overlays##aspect ratio", &g_sfxAspectCheatOverlay);
        }, ImVec2(220.f, -1.f));
}

// Called inside menu.cpp's "world" group (Page_VisualsWorld, col1), after "flip world":
// botox menu_tab_visuals.cpp world group from "dlight" down, verbatim order / labels / ranges /
// popup widths. TF2 cuts: custom smoke / molotov colour (no such grenades), custom blood colour
// (no CParticleCollection layout for TF2 x64), old shaders (TF2 has no PBR shader to swap back),
// bullet impacts server colour (TF2 has no bullet_impact event). World/prop texture presets are
// TF2 textures under botox's labels (world_fx3_math.h). Our extra row after botox's: minecraft sky.
// botox menu_internal.h color_picker_alpha_flags / color_picker_no_alpha_flags
static constexpr ImGuiColorEditFlags kWdPickerAlpha = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
    ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip |
    ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoBorder;
static constexpr ImGuiColorEditFlags kWdPickerNoAlpha = ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_InputRGB |
    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar |
    ImGuiColorEditFlags_NoBorder;

// botox menu.cpp modulation_row: checkbox, picker on the same line, brightness slider under it
static void WdModulationRow(const char* label, bool* enable, float color[4], float* brightness)
{
    ImGui::Checkbox(label, enable);
    if (!*enable)
        return;
    char picker[64] = {}, slider[64] = {};
    snprintf(picker, sizeof(picker), "##%s color picker", label);
    snprintf(slider, sizeof(slider), "brightness##%s", label);
    ImGui::SameLine();
    ImGui::ColorRow(picker, color, kWdPickerAlpha);
    ImGui::SliderFloat(slider, brightness, 0.f, 400.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
}

void Menu_Visuals_World()
{
    // botox: right after flip world
    ImGui::Checkbox("rear view mirror", &g_wdRearMirror);
    if (g_wdRearMirror) {
        ImGui::OptionPopup("rear view mirror settings", [&]() {
            ImGui::SliderInt("width##rear mirror", &g_wdRearMirrorWidth, 80, 1280, "%d px", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderInt("height##rear mirror", &g_wdRearMirrorHeight, 40, 720, "%d px", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("fov##rear mirror", &g_wdRearMirrorFov, 30.f, 150.f, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("rounding##rear mirror", &g_wdRearMirrorRounding, 0.f, 60.f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
            ImGui::Checkbox("mirror image##rear mirror", &g_wdRearMirrorFlip);
        }, ImVec2(220.f, -1.f));
    }

    ImGui::Checkbox("dlight", &g_dlightEnable);
    if (g_dlightEnable) {
        ImGui::OptionPopup("dlight settings", [&]() {
            ImGui::ColorRow("enemies##dlight", g_dlightColor, kWdPickerNoAlpha);
            ImGui::Checkbox("local player##dlight", &g_wdDlightLocal);
            if (g_wdDlightLocal)
                ImGui::ColorRow("local##dlight local color", g_wdDlightLocalColor, kWdPickerNoAlpha);
            ImGui::SliderFloat("radius##dlight", &g_dlightRadius, 25.f, 1000.f, "%.0f u", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderInt("brightness##dlight", &g_wdDlightBrightness, 0, 10, "%d", ImGuiSliderFlags_AlwaysClamp);
        }, ImVec2(220.f, -1.f));
    }

    ImGui::Combo("ragdoll gravity", &g_wdRagdollGravity, "off\0away\0fly up\0");
    if (g_wdRagdollGravity > 0)
        ImGui::SliderFloat("strength##ragdoll gravity", &g_wdRagdollGravityStrength, 0.1f, 5.f, "%.1fx", ImGuiSliderFlags_AlwaysClamp);

    // TF2's precipitation is the rain/snow tracer sprite (c_effects.cpp), tinted where it is looked up
    ImGui::Checkbox("custom precipitation color", &g_weatherTintEnable);
    if (g_weatherTintEnable) {
        ImGui::SameLine();
        ImGui::ColorRow("##custom precipitation color picker", g_weatherTintColor, kWdPickerAlpha);
    }

    WdModulationRow("world modulation", &g_worldTintEnable, g_worldTintColor, &g_wdWorldModBright);
    WdModulationRow("prop modulation", &g_propTintEnable, g_propTintColor, &g_wdPropModBright);
    WdModulationRow("skybox modulation", &g_wdSkyMod, g_wdSkyModColor, &g_wdSkyModBright);

    // lamp static props get coloured dlights (props + players only: TF2 dlights never light brushes),
    // light glow / bloom sprites get the same colour through the modulation walk
    ImGui::Checkbox("lighting modulation", &g_wdLightMod);
    if (g_wdLightMod) {
        ImGui::OptionPopup("lighting modulation settings", [&]() {
            ImGui::ColorRow("color##lighting modulation", g_wdLightModColor, kWdPickerNoAlpha);
            ImGui::Checkbox("lamp lights##lighting modulation", &g_wdLightModLamps);
            if (g_wdLightModLamps) {
                ImGui::SliderInt("intensity##lighting modulation", &g_wdLightModIntensity, 0, 10, "%d", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("radius##lighting modulation", &g_wdLightModRadius, 25.f, 1000.f, "%.0f u", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderInt("max lights##lighting modulation", &g_wdLightModMax, 1, 16, "%d", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("max distance##lighting modulation", &g_wdLightModDist, 100.f, 8000.f, "%.0f u", ImGuiSliderFlags_AlwaysClamp);
            }
            ImGui::Checkbox("light glows##lighting modulation", &g_wdLightModGlows);
            if (g_wdLightModGlows)
                ImGui::SliderFloat("glow brightness##lighting modulation", &g_wdLightModGlowBright, 0.f, 400.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
        }, ImVec2(220.f, -1.f));
    }

    // Aletherium COPRenderSprites hooks: sprite particle rgb (+ alpha when < 255)
    ImGui::Checkbox("particle modulation", &g_wdParticleMod);
    if (g_wdParticleMod) {
        ImGui::SameLine();
        ImGui::ColorRow("##particle modulation color picker", g_wdParticleModColor, kWdPickerAlpha);
    }

    // botox commits a custom path only when the box is left (IsItemDeactivatedAfterEdit), so a
    // half-typed name never reaches the material walk
    constexpr auto presetLabel = [](void*, int i, const char** out) { *out = wfx3::k_texPresets[i].label; return true; };
    auto customBox = [](const char* id, char* edit, size_t editSize, char* committed, size_t committedSize) {
        ImGui::InputTextWithHint(id, "materials/ path, no .vtf", edit, editSize);
        if (ImGui::IsItemDeactivatedAfterEdit())
            strncpy_s(committed, committedSize, edit, _TRUNCATE);
        else if (!ImGui::IsItemActive() && strcmp(edit, committed) != 0)
            strncpy_s(edit, editSize, committed, _TRUNCATE);
    };

    ImGui::Checkbox("world texture", &g_wdWorldTex);
    if (g_wdWorldTex) {
        ImGui::OptionPopup("world texture settings", [&]() {
            int all = g_wdWorldTexPreset[0];
            for (int c = 1; c < wfx3::k_texCategoryCount; ++c)
                all = g_wdWorldTexPreset[c] == all ? all : -1;
            if (ImGui::Combo("all##world texture", &all, presetLabel, nullptr, wfx3::k_texPresetCount))
                for (int c = 0; c < wfx3::k_texCategoryCount; ++c) g_wdWorldTexPreset[c] = all;

            ImGui::Checkbox("remove decals", &g_wdWorldTexDecals);

            static char s_edit[wfx3::k_texCategoryCount][128] = {};
            for (int c = 0; c < wfx3::k_texCategoryCount; ++c) {
                ImGui::PushID(c);
                ImGui::Combo(wfx3::k_texCategories[c], &g_wdWorldTexPreset[c], presetLabel, nullptr, wfx3::k_texPresetCount);
                if (g_wdWorldTexPreset[c] == wfx3::k_texPresetCustom)
                    customBox("##custom", s_edit[c], sizeof(s_edit[c]), g_wdWorldTexCustom[c], sizeof(g_wdWorldTexCustom[c]));
                ImGui::PopID();
            }
        }, ImVec2(260.f, -1.f));
    }

    ImGui::Checkbox("prop texture", &g_wdPropTex);
    if (g_wdPropTex) {
        ImGui::OptionPopup("prop texture settings", [&]() {
            ImGui::Combo("texture##prop texture", &g_wdPropTexPreset, presetLabel, nullptr, wfx3::k_texPresetCount);
            static char s_edit[128] = {};
            if (g_wdPropTexPreset == wfx3::k_texPresetCustom)
                customBox("##prop custom", s_edit, sizeof(s_edit), g_wdPropTexCustom, sizeof(g_wdPropTexCustom));
        }, ImVec2(220.f, -1.f));
    }

    // every world material's base texture -> one pixel of its own colour (wins over world texture)
    ImGui::Checkbox("flat textures", &g_wdFlatTex);

    ImGui::Checkbox("bullet tracers", &g_wdTracers);
    if (g_wdTracers) {
        ImGui::OptionPopup("bullet tracers settings", [&]() {
            ImGui::Checkbox("local##bullet tracers", &g_wdTracersLocal);
            ImGui::SameLine();
            ImGui::ColorRow("##bullet tracers local color", g_wdTracersLocalColor, kWdPickerAlpha);

            ImGui::Checkbox("enemies##bullet tracers", &g_wdTracersEnemy);
            ImGui::SameLine();
            ImGui::ColorRow("##bullet tracers enemy color", g_wdTracersEnemyColor, kWdPickerAlpha);

            ImGui::Checkbox("teammates##bullet tracers", &g_wdTracersTeam);
            ImGui::SameLine();
            ImGui::ColorRow("##bullet tracers team color", g_wdTracersTeamColor, kWdPickerAlpha);

            ImGui::Combo("sprite##bullet tracers", &g_wdTracersSprite, "purple laser\0phys beam\0laser beam\0white\0");
            ImGui::SliderFloat("life##bullet tracers", &g_wdTracersLife, 0.1f, 10.f, "%.1f s", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("width##bullet tracers", &g_wdTracersWidth, 0.5f, 10.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        }, ImVec2(220.f, -1.f));
    }

    ImGui::Checkbox("movement trail", &g_wdTrail);
    if (g_wdTrail) {
        ImGui::OptionPopup("movement trail settings", [&]() {
            ImGui::ColorRow("color##movement trail", g_wdTrailColor, kWdPickerAlpha);
            ImGui::ColorRow("crouch color##movement trail", g_wdTrailCrouchColor, kWdPickerAlpha);
            ImGui::Checkbox("rainbow##movement trail", &g_wdTrailRainbow);
            ImGui::Checkbox("air only##movement trail", &g_wdTrailAirOnly);
            ImGui::Combo("sprite##movement trail", &g_wdTrailSprite, "purple laser\0phys beam\0laser beam\0white\0");
            ImGui::SliderFloat("life##movement trail", &g_wdTrailLife, 0.1f, 10.f, "%.1f s", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("width##movement trail", &g_wdTrailWidth, 0.5f, 20.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        }, ImVec2(220.f, -1.f));
    }

    ImGui::Checkbox("bullet impacts", &g_wdImpacts);
    if (g_wdImpacts) {
        ImGui::OptionPopup("bullet impacts settings", [&]() {
            ImGui::ColorRow("client color##bullet impacts", g_wdImpactsClientColor, kWdPickerAlpha);
            ImGui::SliderFloat("size##bullet impacts", &g_wdImpactsSize, 0.5f, 10.f, "%.1f u", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("duration##bullet impacts", &g_wdImpactsDuration, 0.1f, 30.f, "%.1f s", ImGuiSliderFlags_AlwaysClamp);
        }, ImVec2(220.f, -1.f));
    }

    ImGui::Checkbox("world hit marker", &g_wdHitMarker);
    if (g_wdHitMarker) {
        ImGui::OptionPopup("world hit marker settings", [&]() {
            ImGui::ColorRow("color##world hit marker", g_wdHitMarkerColor, kWdPickerAlpha);
            ImGui::SliderFloat("size##world hit marker", &g_wdHitMarkerSize, 2.f, 20.f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("duration##world hit marker", &g_wdHitMarkerDuration, 0.1f, 5.f, "%.1f s", ImGuiSliderFlags_AlwaysClamp);
        }, ImVec2(220.f, -1.f));
    }

    ImGui::Checkbox("minecraft sky + clouds", &g_mcWorldEnable);
}
