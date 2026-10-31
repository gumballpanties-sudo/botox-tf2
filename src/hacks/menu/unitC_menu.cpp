#include "unitC_menu.h"
#include "menu.h"       // ImGui::OptionPopup / ColorRow
#include "../../globals/globals.h"
#include "../visuals/chams_layers.h"   // botox chams layer lists (config strings)
#include <cstdio>

// botox menu_internal.h color_picker_alpha_flags / color_picker_no_alpha_flags
static constexpr ImGuiColorEditFlags kPickerAlpha = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
    ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip |
    ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoBorder;
static constexpr ImGuiColorEditFlags kPickerNoAlpha = ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_InputRGB |
    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar |
    ImGuiColorEditFlags_NoBorder;

// Called inside menu.cpp's "players" group (Page_VisualsEsp). Mirrors botox's own
// per-feature checkbox + cog pattern (menu_tab_visuals.cpp case 0).
void UnitC_Menu_visuals_esp()
{
    ImGui::Checkbox("players", &g_espEnable);
    if (g_espEnable)
        ImGui::OptionPopup("esp settings", [&]() {
            ImGui::ColorRow("enemy color##esp", g_espEnemyColor, kPickerAlpha);
            ImGui::Checkbox("include teammates", &g_espTeammates);
            if (g_espTeammates)
                ImGui::ColorRow("team color##esp", g_espTeamColor, kPickerAlpha);
            ImGui::Checkbox("distance fade", &g_espDistanceFade);
            if (g_espDistanceFade)
            {
                ImGui::SliderFloat("fade near", &g_espFadeNear, 0.f, 4000.f, "%.0f");
                ImGui::SliderFloat("fade far", &g_espFadeFar, 0.f, 6000.f, "%.0f");
            }
        }, ImVec2(210.f, -1));

    ImGui::BeginDisabled(!g_espEnable);
    const char* boxStyles[] = { "off", "2d corner", "2d full" };
    ImGui::Combo("box##esp", &g_espBoxStyle, boxStyles, 3);
    ImGui::Checkbox("name##esp", &g_espName);
    ImGui::Checkbox("health##esp", &g_espHealth);
    ImGui::Checkbox("distance##esp", &g_espDistance);
    ImGui::Checkbox("flags##esp", &g_espFlags);
    if (g_espFlags) ImGui::ColorRow("flags color##esp", g_espFlagsColor, kPickerAlpha);
    ImGui::Checkbox("skeleton##esp", &g_espSkeleton);
    if (g_espSkeleton) ImGui::ColorRow("skeleton color##esp", g_espSkeletonColor, kPickerAlpha);
    ImGui::Checkbox("show hitboxes##esp", &g_espHitboxes);
    if (g_espHitboxes) ImGui::ColorRow("hitbox color##esp", g_espHitboxColor, kPickerAlpha);

    // botox menu_tab_visuals.cpp "player glow" row + popup, verbatim order/labels/ranges.
    // What each option drives in TF2: aim/chams.cpp GlowPass banner.
    ImGui::Checkbox("player glow", &g_glowEnable);
    if (g_glowEnable)
        ImGui::OptionPopup("player glow settings", [&]() {
            const bool legacy   = g_glowLegacy;
            const bool gradient = !legacy && g_glowGradient;

            ImGui::ColorRow(gradient ? "visible inner##player vis glow color" : "player visible##player vis glow color", g_glowVisColor, kPickerAlpha);
            if (gradient)
                ImGui::ColorRow("visible outer##player vis glow outer color", g_glowVisOuterColor, kPickerAlpha);
            ImGui::ColorRow(gradient ? "invisible inner##player invis glow color" : "player invisible##player invis glow color", g_glowInvisColor, kPickerAlpha);
            if (gradient)
                ImGui::ColorRow("invisible outer##player invis glow outer color", g_glowInvisOuterColor, kPickerAlpha);

            ImGui::Checkbox("accent wave##player glow", &g_glowWave);
            if (g_glowWave) {
                ImGui::ColorRow("wave color##player glow wave", g_glowWaveColor, kPickerNoAlpha);
                ImGui::SliderFloat("wave speed##player glow wave", &g_glowWaveSpeed, 0.05f, 4.f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
                if (!legacy)
                    ImGui::SliderFloat("wave size##player glow wave", &g_glowWaveSize, 0.05f, 2.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("wave strength##player glow wave", &g_glowWaveStrength, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            }

            ImGui::Checkbox("edge pulse##player glow", &g_glowPulse);
            if (g_glowPulse) {
                ImGui::SliderFloat("pulse speed##player glow pulse", &g_glowPulseSpeed, 0.1f, 4.f, "%.1fx", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("pulse min##player glow pulse", &g_glowPulseMin, 0.f, 95.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
            }

            ImGui::Checkbox("legacy##player glow", &g_glowLegacy);
            if (legacy) {
                ImGui::Combo("style##player glow legacy", &g_glowLegacyStyle, "default\0rim\0outline\0pulse\0");
                return;
            }

            ImGui::Checkbox("normal blending##player glow", &g_glowNormalBlend);
            if (g_glowNormalBlend)
                ImGui::SliderFloat("thickness##player glow", &g_glowThickness, 2.f, 30.f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);

            ImGui::Checkbox("gradient##player glow", &g_glowGradient);
            if (gradient)
                ImGui::SliderFloat("inner size##player glow gradient", &g_glowGradientInner, 0.f, 100.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
        }, ImVec2(200.f, -1));

    ImGui::Checkbox("out of fov arrows", &g_espOofArrows);
    if (g_espOofArrows) ImGui::ColorRow("arrow color##esp", g_espOofColor, kPickerAlpha);
    ImGui::Checkbox("sound pings", &g_espSoundPings);
    if (g_espSoundPings) ImGui::ColorRow("ping color##esp", g_espSoundColor, kPickerAlpha);
    ImGui::Checkbox("damage numbers##esp", &g_espDamageNumbers);
    if (g_espDamageNumbers) ImGui::ColorRow("number color##esp", g_espDmgColor, kPickerAlpha);
    ImGui::Checkbox("player avatar", &g_espAvatar);   // unit E5: steam avatar at the esp box, see avatar_cache.h
    ImGui::EndDisabled();
}

// botox menu.cpp chams_dropdown: combo of every material; a picked one shows its order number
// and a colour swatch, clicking it again removes it. The list lives in a config string.
static void ChamsDropdown(const char* label, char* cfg)
{
    using namespace ChamsLayers;
    List l;
    Parse(cfg, l);

    // stack buffer: rebuilt every frame, never longer than all names once
    char preview[192] = "none";
    int written = 0;
    for (int i = 0; i < l.used; i++) {
        const int printed = std::snprintf(preview + written, sizeof(preview) - written, written ? ", %s" : "%s", kNames[l.slots[i]]);
        if (printed <= 0) break;
        written += printed;
        if (written >= (int)sizeof(preview) - 1) break;
    }

    if (!ImGui::BeginCombo(label, preview))
        return;

    bool changed = false;
    for (int material = 0; material < Count; material++) {
        const int slot = IndexOf(l, material);

        // same ##id either way: toggling must not change the item id
        char text[64] = {};
        if (slot >= 0) std::snprintf(text, sizeof(text), "%i. %s##chams%i", slot + 1, kNames[material], material);
        else           std::snprintf(text, sizeof(text), "%s##chams%i", kNames[material], material);

        const bool toggled = ImGui::Selectable(text, slot >= 0, ImGuiSelectableFlags_DontClosePopups | ImGuiSelectableFlags_AllowItemOverlap);
        ImGui::SetItemAllowOverlap();

        if (slot >= 0) {
            const ImVec2 next_position = ImGui::GetCursorPos();
            ImGui::SameLine();
            char color_id[96] = {};
            std::snprintf(color_id, sizeof(color_id), "##chams color%s%s", label, kNames[material]);
            if (ImGui::ColorRow(color_id, l.colors[slot], kPickerAlpha))
                changed = true;
            ImGui::SetCursorPos(next_position);
        }

        if (toggled) {
            changed |= Toggle(l, material);
            break;   // botox: EndCombo + return right after a toggle
        }
    }
    ImGui::EndCombo();

    if (changed)
        Format(l, cfg, kCfgLen);
}

static bool ChamsHasLayers(const char* cfg)
{
    ChamsLayers::List l;
    ChamsLayers::Parse(cfg, l);
    return l.used > 0;
}

// Called inside menu.cpp's "chams" group (Page_VisualsEsp, col2). botox menu_tab_visuals.cpp
// "chams" group, verbatim order/labels/ranges. TF2 mapping: gloves = c_ arms, sleeve = classic
// v_ viewmodels (TF2 has no sleeve model), teammates = the extra visible/occluded pair the old
// TF2 chams had (botox draws enemies only).
void UnitC_Menu_visuals_chams()
{
    ImGui::Checkbox("enable enemy chams", &g_chamsEnable);

    ImGui::BeginDisabled(!g_chamsEnable);

    ChamsDropdown("visible material", g_chamsVisibleCfg);
    ChamsDropdown("occluded material", g_chamsOccludedCfg);

    ImGui::Checkbox("ragdolls##chams ragdolls", &g_chamsRagdolls);

    ChamsDropdown("backtrack material", g_chamsBacktrackCfg);

    ChamsDropdown("sound material", g_chamsSoundCfg);

    if (ChamsHasLayers(g_chamsSoundCfg)) {
        ImGui::SliderFloat("sound duration", &g_chamsSoundDuration, 0.2f, 5.f, "%.1fs");
        ImGui::Checkbox("sound fade", &g_chamsSoundFade);

        ImGui::SliderFloat("sound min volume##sound esp chams", &g_chamsSoundMinVolume, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("sound max distance##sound esp chams", &g_chamsSoundMaxDistance, 0.f, 6000.f, "%.0f u", ImGuiSliderFlags_AlwaysClamp);
    }

    ImGui::Checkbox("backtrack xqz", &g_chamsBacktrackXqz);
    ImGui::Combo("backtrack type", &g_chamsBacktrackType, "oldest record\0all records\0aimbot target record\0");

    if (g_chamsBacktrackType == 1)
        ImGui::SliderInt("backtrack max ghosts", &g_chamsBacktrackMaxGhosts, 2, 64);

    ImGui::Checkbox("backtrack tick gradient", &g_chamsBacktrackGradient);

    ImGui::EndDisabled();

    ImGui::Checkbox("enable team chams", &g_chamsTeam);

    ImGui::BeginDisabled(!g_chamsTeam);

    ChamsDropdown("visible material##team chams", g_chamsTeamVisibleCfg);
    ChamsDropdown("occluded material##team chams", g_chamsTeamOccludedCfg);

    ImGui::EndDisabled();

    ImGui::Checkbox("enable local chams", &g_chamsLocalEnable);

    ImGui::BeginDisabled(!g_chamsLocalEnable);

    ChamsDropdown("weapon material", g_chamsWeaponCfg);
    ChamsDropdown("gloves material", g_chamsArmsCfg);
    ChamsDropdown("sleeve material", g_chamsSleeveCfg);

    ImGui::EndDisabled();
}

// Called inside menu.cpp's "edicts" group (Page_VisualsWorld, col1). World-entity esp:
// TF2 has no dropped-weapon/bomb/grenade-path analogues botox's edicts group has, so this
// keeps the buildings/ammo/projectile rows that exist here instead.
void UnitC_Menu_visuals_world()
{
    ImGui::Checkbox("world entities", &g_espWorldEnable);
    if (g_espWorldEnable)
        ImGui::OptionPopup("world entities settings", [&]() {
            ImGui::Checkbox("buildings", &g_espBuildings);
            if (g_espBuildings) ImGui::ColorRow("building color##edicts", g_espBuildingColor, kPickerAlpha);
            ImGui::Checkbox("ammo packs", &g_espAmmoPacks);
            ImGui::Checkbox("dropped weapons", &g_espDroppedWeapons);
            if (g_espAmmoPacks || g_espDroppedWeapons) ImGui::ColorRow("item color##edicts", g_espItemColor, kPickerAlpha);
            ImGui::Checkbox("projectiles", &g_espProjectiles);
            if (g_espProjectiles) ImGui::ColorRow("projectile color##edicts", g_espProjColor, kPickerAlpha);
        }, ImVec2(200.f, -1));

    ImGui::Checkbox("projectile path", &g_espProjPath);
    if (g_espProjPath)
        ImGui::ColorRow("path color##edicts", g_espProjPathColor);
}
