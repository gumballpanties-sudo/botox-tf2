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

// Called inside menu.cpp's "players" group (Page_VisualsEsp). botox menu_tab_visuals.cpp case 0
// "players" group, verbatim order / labels / ranges. TF2 rows that differ: flags list (TF2
// conditions), no weapon icon row (no TF2 icon font), no player stencil / internal glow rows
// (botox screen passes, not ported).
void UnitC_Menu_visuals_esp()
{
    const bool players = g_espEnable;

    ImGui::Checkbox("players", &g_espEnable);
    if (g_espEnable) {
        ImGui::OptionPopup("esp settings##player esp settings", [&]() {
            ImGui::Checkbox("include teammates", &g_espTeammates);
            ImGui::SliderFloat("max distance##player esp", &g_espMaxDistance, 0.f, 6000.f, "%.0f u", ImGuiSliderFlags_AlwaysClamp);
            if (g_espMaxDistance > 0.f)
                ImGui::Checkbox("fade with distance", &g_espDistanceFade);
            ImGui::Checkbox("only on sound", &g_espSoundOnly);
            if (g_espSoundOnly) {
                ImGui::SliderFloat("sound duration##player esp", &g_espSoundDuration, 0.2f, 5.f, "%.1fs");
                ImGui::Checkbox("sound fade##player esp", &g_espSoundFade);
                ImGui::SliderFloat("min volume##sound esp players", &g_chamsSoundMinVolume, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("sound max distance##sound esp players", &g_chamsSoundMaxDistance, 0.f, 6000.f, "%.0f u", ImGuiSliderFlags_AlwaysClamp);
            }
        }, ImVec2(210.f, -1));

        ImGui::Checkbox("bounding box##player", &g_espBox);
        if (g_espBox)
            ImGui::OptionPopup("bounding box settings##player bounding box settings", [&]() {
                ImGui::Checkbox("visibility colors##player box", &g_espBoxVisColors);
                if (g_espBoxVisColors) {
                    ImGui::ColorRow("visible##player box visible color", g_espBoxVisibleColor, kPickerAlpha);
                    ImGui::ColorRow("invisible##player box invisible color", g_espBoxInvisibleColor, kPickerAlpha);
                } else
                    ImGui::ColorRow("box color##player bounding box color", g_espBoxColor, kPickerAlpha);
                ImGui::Checkbox("3d bounding box##player", &g_espBox3d);
                if (!g_espBox3d)
                    ImGui::Checkbox("corner bounding box##player", &g_espBoxCorner);
                ImGui::SliderFloat("thickness##player box thickness", &g_espBoxThickness, 1.f, 5.f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("rounding##player box rounding", &g_espBoxRounding, 0.f, 20.f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
                ImGui::Checkbox("bounding box outline##player", &g_espBoxOutline);
                if (g_espBoxOutline) {
                    ImGui::ColorRow("outline color##player bounding box outline color", g_espBoxOutlineColor, kPickerAlpha);
                    ImGui::SliderFloat("outline thickness##player box outline thickness", &g_espBoxOutlineThickness, 1.f, 5.f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
                    if (!g_espBoxCorner && !g_espBox3d)
                        ImGui::Combo("outline side##player box outline side", &g_espBoxOutlineSide, "inside\0outside\0both\0");
                }
            }, ImVec2(200.f, -1));

        /* own row: fill without a frame is a valid look, box toggle must not gate it */
        ImGui::Checkbox("box fill##player", &g_espBoxFill);
        if (g_espBoxFill)
            ImGui::OptionPopup("box fill settings##player box fill settings", [&]() {
                ImGui::ColorRow(g_espBoxFillGradient ? "top color##player box fill color" : "fill color##player box fill color", g_espBoxFillColor, kPickerAlpha);
                ImGui::Checkbox("gradient##player box fill", &g_espBoxFillGradient);
                if (g_espBoxFillGradient)
                    ImGui::ColorRow("bottom color##player box fill bottom color", g_espBoxFillBottomColor, kPickerAlpha);
            }, ImVec2(200.f, -1));

        ImGui::Checkbox("name##player", &g_espName);
        if (g_espName)
            ImGui::OptionPopup("name settings##player name settings", [&]() {
                ImGui::Combo("position##player name", &g_espNamePosition, "above box\0below box\0inside top\0inside bottom\0");
                ImGui::Checkbox("visibility colors##player name", &g_espNameVisColors);
                if (g_espNameVisColors) {
                    ImGui::ColorRow("visible##player name visible color", g_espNameVisibleColor, kPickerAlpha);
                    ImGui::ColorRow("invisible##player name invisible color", g_espNameInvisibleColor, kPickerAlpha);
                } else
                    ImGui::ColorRow("name color##player name color", g_espNameColor, kPickerAlpha);
            }, ImVec2(200.f, -1));

        ImGui::Checkbox("health bar##player", &g_espHealthBar);
        if (g_espHealthBar)
            ImGui::OptionPopup("health bar settings##player health bar settings", [&]() {
                ImGui::Combo("bar side##player health bar", &g_espHealthBarSide, "left\0right\0top\0bottom\0corner\0");
                if (g_espHealthBarSide == 4)
                    ImGui::Combo("which corner##player health bar", &g_espHealthBarCornerPick, "bottom left\0bottom right\0top left\0top right\0");
                if (g_espHealthBarSide == 4 && !g_espBox) {
                    ImGui::SliderFloat("corner rounding##player health bar", &g_espHealthBarCornerRounding, 0.f, 20.f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::SliderFloat("corner size##player health bar", &g_espHealthBarCornerSize, 0.1f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                }
                ImGui::SliderFloat("bar thickness##player health bar", &g_espHealthBarThickness, 1.f, 10.f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
                ImGui::Checkbox("health bar outline", &g_espHealthBarOutline);
                ImGui::Checkbox("custom health bar color", &g_espHealthBarCustomColor);
                if (g_espHealthBarCustomColor) {
                    ImGui::ColorRow(g_espHealthBarGradient ? "top color##health bar color" : "bar color##health bar color", g_espHealthBarColor, kPickerAlpha);
                    ImGui::Checkbox("gradient##health bar", &g_espHealthBarGradient);
                    if (g_espHealthBarGradient)
                        ImGui::ColorRow("bottom color##health bar bottom color", g_espHealthBarBottomColor, kPickerAlpha);
                }
                ImGui::ColorRow("background color##health bar background color", g_espHealthBarBgColor, kPickerAlpha);
            }, ImVec2(200.f, -1));

        ImGui::Checkbox("health number##player", &g_espHealthText);
        if (g_espHealthText)
            ImGui::OptionPopup("health number settings##player health number settings", [&]() {
                ImGui::Checkbox("health number suffix", &g_espHealthSuffix);
                ImGui::Combo("health number style", &g_espHealthTextStyle, "standalone bottom\0follow health bar\0");
                ImGui::Checkbox("custom health number color", &g_espHealthTextCustomColor);
                if (g_espHealthTextCustomColor)
                    ImGui::ColorRow("number color##player health number color", g_espHealthTextColor, kPickerAlpha);
            }, ImVec2(200.f, -1));

        ImGui::Checkbox("weapon name##player", &g_espWeaponName);
        if (g_espWeaponName) {
            ImGui::SameLine();
            ImGui::ColorRow("##weapon name color", g_espWeaponNameColor, kPickerAlpha);
        }

        ImGui::Checkbox("weapon ammo bar##player", &g_espAmmoBar);
        if (g_espAmmoBar) {
            ImGui::SameLine();
            ImGui::ColorRow("##player ammo bar color", g_espAmmoBarColor, kPickerAlpha);
        }

        ImGui::Checkbox("distance##player", &g_espDistance);
        if (g_espDistance)
            ImGui::OptionPopup("distance settings##player distance settings", [&]() {
                ImGui::Combo("unit##player distance", &g_espDistanceUnit, "meters\0units\0feet\0");
                ImGui::ColorRow("distance color##player distance color", g_espDistanceColor, kPickerAlpha);
            }, ImVec2(200.f, -1));

        ImGui::Checkbox("flags##player", &g_espFlags);
        if (g_espFlags)
            ImGui::OptionPopup("flag settings##player flag settings", [&]() {
                ImGui::MultiCombo("flags##player flag list", &g_espFlagList, EspFlagNames(), EF_COUNT);
                ImGui::ColorRow("flag color##player flag color", g_espFlagsColor, kPickerAlpha);
                ImGui::ColorRow("alert color##player flag alert color", g_espFlagsAlertColor, kPickerAlpha);
            }, ImVec2(210.f, -1));
    }

    ImGui::BeginDisabled(!players);

    ImGui::Checkbox("player skeleton", &g_espSkeleton);
    if (g_espSkeleton)
        ImGui::OptionPopup("skeleton settings##player skeleton settings", [&]() {
            ImGui::Checkbox("visibility colors##player skeleton", &g_espSkeletonVisColors);
            if (g_espSkeletonVisColors) {
                ImGui::ColorRow("visible##player skeleton visible color", g_espSkeletonVisibleColor, kPickerAlpha);
                ImGui::ColorRow("invisible##player skeleton invisible color", g_espSkeletonInvisibleColor, kPickerAlpha);
            } else
                ImGui::ColorRow("skeleton color##player skeleton color", g_espSkeletonColor, kPickerAlpha);
            // cfg keeps 0 normal / 2 smooth (1 was lag compensated, gone with backtrack)
            int iSkel = g_espSkeletonType == 2 ? 1 : 0;
            if (ImGui::Combo("skeleton type##player skeleton", &iSkel, "normal\0smooth\0"))
                g_espSkeletonType = iSkel ? 2 : 0;
            ImGui::SliderFloat("thickness##player skeleton", &g_espSkeletonThickness, 0.5f, 5.f, "%.1f px", ImGuiSliderFlags_AlwaysClamp);
        }, ImVec2(200.f, -1));

    ImGui::Checkbox("show hitboxes", &g_espHitboxes);
    if (g_espHitboxes)
        ImGui::OptionPopup("hitboxes settings##player hitboxes settings", [&]() {
            ImGui::Combo("show##player hitboxes", &g_espHitboxesMode, "always\0on hit\0");
            ImGui::ColorRow("color##player hitboxes color", g_espHitboxColor, kPickerAlpha);
            ImGui::SliderFloat("thickness##player hitboxes", &g_espHitboxesThickness, 0.5f, 5.f, "%.1f px", ImGuiSliderFlags_AlwaysClamp);
            if (g_espHitboxesMode == 1)
                ImGui::SliderFloat("duration##player hitboxes", &g_espHitboxesDuration, 0.1f, 10.f, "%.1f s", ImGuiSliderFlags_AlwaysClamp);
        }, ImVec2(200.f, -1));

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

    ImGui::EndDisabled();

    ImGui::BeginDisabled(!players);

    ImGui::Checkbox("player avatar", &g_espAvatar);
    if (g_espAvatar)
        ImGui::OptionPopup("avatar settings##player avatar settings", [&]() {
            ImGui::Combo("position##player avatar", &g_espAvatarPosition, "next to name\0above box\0below box\0cover head\0");
            if (g_espAvatarPosition == 3)
                ImGui::SliderFloat("head scale##player avatar", &g_espAvatarHeadScale, 0.25f, 3.f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
            else
                ImGui::SliderFloat("avatar size##player avatar", &g_espAvatarSize, 6.f, 40.f, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
        }, ImVec2(200.f, -1));

    ImGui::Checkbox("damage numbers", &g_espDamageNumbers);
    if (g_espDamageNumbers)
        ImGui::OptionPopup("damage numbers settings", [&]() {
            ImGui::ColorRow("damage numbers color##damage numbers color", g_espDmgColor, kPickerAlpha);
            ImGui::SliderFloat("duration", &g_espDmgDuration, 0.2f, 5.f, "%.1f s");
            ImGui::SliderFloat("rise speed", &g_espDmgSpeed, 0.f, 100.f, "%.0f u/s");
            ImGui::Checkbox("stack fast hits", &g_espDmgStack);
        }, ImVec2(200.f, -1));

    ImGui::Checkbox("out of fov arrows", &g_espOofArrows);
    if (g_espOofArrows)
        ImGui::OptionPopup("fov arrows settings", [&]() {
            ImGui::Combo("arrows show##out of fov arrows type", &g_espOofType, "players\0enemy sounds\0players + sounds\0");
            if (g_espOofType != 1) {
                ImGui::ColorRow("visible color##out of fov arrows color", g_espOofColor, kPickerAlpha);
                ImGui::ColorRow("behind wall color##out of fov arrows wall color", g_espOofWallColor, kPickerAlpha);
            }
            if (g_espOofType != 0) {
                ImGui::ColorRow("sound color##out of fov arrows sound color", g_espOofSoundColor, kPickerAlpha);
                ImGui::SliderFloat("sound arrow duration", &g_espOofSoundDuration, 0.2f, 5.f, "%.1fs");
                ImGui::Checkbox("sound arrow fade", &g_espOofSoundFade);
                ImGui::SliderFloat("min volume##sound esp arrows", &g_chamsSoundMinVolume, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("max distance##sound esp arrows", &g_chamsSoundMaxDistance, 0.f, 6000.f, "%.0f u", ImGuiSliderFlags_AlwaysClamp);
            }
            ImGui::Combo("arrows style##out of fov arrows style", &g_espOofStyle, "default\0interium\0");
            ImGui::SliderFloat("arrows width", &g_espOofWidth, 1.f, 60.f, "%.1f px");
            ImGui::SliderFloat("arrows height", &g_espOofHeight, 1.f, 60.f, "%.1f px");
            ImGui::SliderInt("arrows distance", &g_espOofDistance, 10, 500, "%d px");
            if (g_espOofType != 1)
                ImGui::Checkbox("player avatars", &g_espOofAvatar);
            ImGui::Checkbox("follow aspect ratio", &g_espOofAspect);
        }, ImVec2(200.f, -1));

    // TF2 extra (not in botox): an expanding ring where a sound was made
    ImGui::Checkbox("sound pings", &g_espSoundPings);
    if (g_espSoundPings) {
        ImGui::SameLine();
        ImGui::ColorRow("##ping color esp", g_espSoundColor, kPickerAlpha);
    }

    ImGui::EndDisabled();
}

// One side's building rows: TF2's stand-in for botox's "edicts" group, same row shape (checkbox,
// swatch, cog with the parts). Box style / health bar look come from the players rows above.
static void BuildingRows(const char* label, const char* id, bool* on, float* col, bool* box, bool* name, bool* bar,
                         bool* text, bool* flags, bool* dist, bool* ownOnly)
{
    char buf[96];
    ImGui::Checkbox(label, on);
    if (!*on)
        return;
    snprintf(buf, sizeof(buf), "##%s color", id);
    ImGui::SameLine();
    ImGui::ColorRow(buf, col, kPickerAlpha);
    snprintf(buf, sizeof(buf), "%s settings##%s", label, id);
    ImGui::OptionPopup(buf, [&]() {
        char l[64];
        snprintf(l, sizeof(l), "bounding box##%s", id);  ImGui::Checkbox(l, box);
        snprintf(l, sizeof(l), "name##%s", id);          ImGui::Checkbox(l, name);
        snprintf(l, sizeof(l), "health bar##%s", id);    ImGui::Checkbox(l, bar);
        snprintf(l, sizeof(l), "health number##%s", id); ImGui::Checkbox(l, text);
        snprintf(l, sizeof(l), "flags##%s", id);         ImGui::Checkbox(l, flags);
        snprintf(l, sizeof(l), "distance##%s", id);      ImGui::Checkbox(l, dist);
        if (ownOnly) { snprintf(l, sizeof(l), "only my buildings##%s", id); ImGui::Checkbox(l, ownOnly); }
    }, ImVec2(200.f, -1));
}

// Called inside menu.cpp's "buildings" group (Page_VisualsEsp, under "players").
void UnitC_Menu_visuals_buildings()
{
    BuildingRows("enemy buildings", "bld enemy", &g_espBuildings, g_espBuildingColor, &g_espBldEnemyBox, &g_espBldEnemyName,
                 &g_espBldEnemyHealthBar, &g_espBldEnemyHealthText, &g_espBldEnemyFlags, &g_espBldEnemyDistance, nullptr);
    BuildingRows("friendly buildings", "bld team", &g_espBldTeam, g_espBldTeamColor, &g_espBldTeamBox, &g_espBldTeamName,
                 &g_espBldTeamHealthBar, &g_espBldTeamHealthText, &g_espBldTeamFlags, &g_espBldTeamDistance, &g_espBldTeamOwnOnly);
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

    ChamsDropdown("sound material", g_chamsSoundCfg);

    if (ChamsHasLayers(g_chamsSoundCfg)) {
        ImGui::SliderFloat("sound duration", &g_chamsSoundDuration, 0.2f, 5.f, "%.1fs");
        ImGui::Checkbox("sound fade", &g_chamsSoundFade);

        ImGui::SliderFloat("sound min volume##sound esp chams", &g_chamsSoundMinVolume, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("sound max distance##sound esp chams", &g_chamsSoundMaxDistance, 0.f, 6000.f, "%.0f u", ImGuiSliderFlags_AlwaysClamp);
    }

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
// keeps the ammo/projectile rows that exist here instead (buildings: esp page, own group).
void UnitC_Menu_visuals_world()
{
    ImGui::Checkbox("world entities", &g_espWorldEnable);
    if (g_espWorldEnable)
        ImGui::OptionPopup("world entities settings", [&]() {
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
