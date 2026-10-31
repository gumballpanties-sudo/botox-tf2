#include "unitB_menu.h"
#include "menu.h"        // MenuBindKey
#include "../hud/velgraph.h"
#include "../hud/keystroke.h"
#include "../hud/keyind_fx.h"
#include "../hud/movind.h"      // MI_COUNT / kMiNames
#include "../movement/detections.h"
#include "../movement/ps_lines.h"
#include "../movement/recorder.h"
#include "menu_chrome.h"   // menu_columns_* / menu_group_*
#include <cstdio>

void UnitB_Menu_movement_indicators()
{
    ImGui::Checkbox("velocity graph", &g_vgEnable);
    if (g_vgEnable)
        ImGui::OptionPopup("velocity graph settings", [&]() {
            ImGui::SliderInt("history##vg", &g_vgLength, 2, 512);
            ImGui::SliderInt("width##vg", &g_vgWidth, 10, 600);
            ImGui::SliderInt("height##vg", &g_vgHeight, 5, 300);
            ImGui::SliderInt("position##vg", &g_vgPosition, 0, 600);
            ImGui::SliderInt("x offset##vg", &g_vgOffsetX, -600, 600);
            ImGui::SliderFloat("max speed (0=auto)##vg", &g_vgMax, 0.f, 2000.f);
            ImGui::Checkbox("fill##vg", &g_vgFill);
            ImGui::Checkbox("fade sides##vg", &g_vgFadeSides);
            ImGui::Checkbox("gain/loss colors##vg", &g_vgGainColors);
            ImGui::Checkbox("jump speed labels##vg", &g_vgJumpSpeeds);
            ImGui::SliderFloat("thickness##vg", &g_vgThickness, 0.5f, 6.f);
            ImGui::ColorRow("line color##vg", g_vgColor);
            if (g_vgGainColors)
            {
                ImGui::ColorRow("gain color##vg", g_vgColorGain);
                ImGui::ColorRow("loss color##vg", g_vgColorLoss);
            }
            if (g_vgFill) ImGui::ColorRow("fill color##vg", g_vgFillColor);
        }, ImVec2(240.f, -1));

    ImGui::Checkbox("particles on hit##mi", &g_miParticles);
    ImGui::Checkbox("random color per tag##mi", &g_miRandomColor);
    ImGui::Checkbox("custom labels##mi", &g_miCustomLabels);
    if (g_miCustomLabels)
        ImGui::OptionPopup("custom bind labels", [&]() {
            for (int i = 0; i < MI_COUNT; ++i)
            {
                char id[32]; snprintf(id, sizeof(id), "%s##milabel", kMiNames[i]);
                ImGui::InputText(id, g_miLabels[i], sizeof(g_miLabels[i]));
            }
        }, ImVec2(220.f, -1));

    ImGui::Checkbox("keystroke indicator", &g_ksEnable);
    if (g_ksEnable)
        ImGui::OptionPopup("keystroke indicator settings", [&]() {
            ImGui::SliderInt("layout##ks", &g_ksLayout, 0, 1);
            ImGui::Checkbox("duck/jump row##ks", &g_ksDuckJump);
            ImGui::Checkbox("mouse arrows##ks", &g_ksMouse);
            ImGui::Combo("released style##ks", &g_ksReleased, "cross fade\0color lerp\0fade only\0");
            ImGui::SliderFloat("fade time##ks", &g_ksFade, 0.f, 0.5f);
            ImGui::SliderFloat("scale##ks", &g_ksScale, 0.5f, 3.f);
            ImGui::SliderInt("position##ks", &g_ksPosition, 0, 600);
            ImGui::SliderInt("x offset##ks", &g_ksOffsetX, -600, 600);
            ImGui::ColorRow("on color##ks", g_ksColor);
            ImGui::ColorRow("released color##ks", g_ksColorReleased);
        }, ImVec2(240.f, -1));
}

void UnitB_Menu_movement_calculator()
{
    // botox menu_tab_movement "detections": label list order must match DetType
    ImGui::Checkbox("detections", &g_detections);
    if (g_detections)
        ImGui::OptionPopup("detection settings", [&]() {
            static const char* const kTypes[DET_MAX] = { "edgebug", "texturebug", "airstuck", "pixelsurf", "wallclimb" };
            ImGui::MultiCombo("detections", &g_detectionTypes, kTypes, DET_MAX);
            ImGui::Checkbox("stack edgebugs##detstack", &g_detectionStackEb);
            ImGui::InputText("chat label##detlabel", g_detectionLabel, sizeof(g_detectionLabel));
        }, ImVec2(220.f, -1));

    ImGui::Checkbox("pixelsurf lines", &g_psLinesEnable);
    if (g_psLinesEnable)
        ImGui::OptionPopup("pixelsurf lines settings", [&]() {
            ImGui::Checkbox("full trajectory##psl", &g_psLinesFull);
            ImGui::ColorRow("line color##psl", g_psLinesColor);
            if (g_psLinesFull) ImGui::ColorRow("full trajectory color##psl", g_psLinesFullColor);
        }, ImVec2(220.f, -1));
}

// botox menu_tab_movement case 3, 1:1. Emits its own columns + groups (Page_MovementRecorder
// only calls this).
void UnitB_Menu_movement_recorder()
{
    static bool s_firstList = true;
    if (s_firstList) { Recorder_Refresh(); s_firstList = false; }

    menu_columns_begin();

    if (menu_group_begin("recorder"))
    {
        ImGui::Checkbox("movement recorder", &g_recEnable);
        ImGui::Checkbox("show recording line", &g_recShowLine);
        ImGui::Checkbox("show recorder timer", &g_recRender);
        if (g_recRender)
            ImGui::OptionPopup("recorder timer settings", [&]() {
                ImGui::Combo("position##recorder timer", &g_recPosition, "top left\0bottom left\0bottom right\0");
            }, ImVec2(200.f, -1));
        ImGui::Checkbox("clipper", &g_recClipper);
        if (g_recClipper)
        {
            ImGui::Checkbox("show clipper box", &g_recClipperBox);
            ImGui::SliderFloat("clipper duration##recorder", &g_recClipSeconds, 1.f, 120.f, "%.0f s");
        }
        ImGui::Checkbox("original playback viewangles", &g_recLockVa);
        ImGui::Combo("playback yaw", &g_recYaw, "original\0left\0right\0backwards\0spinning\0");
        if (g_recYaw == 4)
            ImGui::SliderFloat("spin speed##recorder", &g_recSpinSpeed, -1800.f, 1800.f, "%.0f deg/s");
        ImGui::SliderFloat("smooth start##recorder", &g_recSmoothStart, 0.f, 3.f, g_recSmoothStart > 0.01f ? "%.2f s" : "off");
        ImGui::SliderFloat("smooth end##recorder", &g_recSmoothEnd, 0.f, 3.f, g_recSmoothEnd > 0.01f ? "%.2f s" : "off");
        ImGui::Checkbox("lock while aiming to position", &g_recLockGoingToStart);
        ImGui::Checkbox("stop playback on movement", &g_recStopOnMove);
        ImGui::Checkbox("force same weapons", &g_recForceWeapon);

        // press actions: a style (always on / toggle) would fire them on every menu close
        int unused = 1;
        ImGui::Label("start recording");
        ImGui::Keybind("recorder start rec key", &g_recKeyStartRec, &unused, false);
        ImGui::Label("stop recording");
        ImGui::Keybind("recorder stop rec key", &g_recKeyStopRec, &unused, false);
        ImGui::Label("save route");
        ImGui::Keybind("recorder save key", &g_recKeySave, &unused, false);
        ImGui::Label("start playback");
        ImGui::Keybind("recorder start play key", &g_recKeyStartPlay, &unused, false);
        ImGui::Label("stop playback");
        ImGui::Keybind("recorder stop play key", &g_recKeyStopPlay, &unused, false);
        ImGui::Label("clear route");
        ImGui::Keybind("recorder clear key", &g_recKeyClear, &unused, false);
        if (g_recClipper)
        {
            ImGui::Label("clip route");
            ImGui::Keybind("recorder clip key", &g_recKeyClip, &unused, false);
        }
    }
    menu_group_end();

    if (menu_group_begin("actions"))
    {
        static constexpr struct { const char* label; unsigned action; } kActions[] = {
            { "start recording", REC_START_REC }, { "stop recording", REC_STOP_REC }, { "save route", REC_SAVE },
            { "start playback", REC_START_PLAY }, { "stop playback", REC_STOP_PLAY }, { "clear route", REC_CLEAR },
            { "clip route", REC_CLIP },
        };
        ImGui::BeginDisabled(!g_recEnable);
        for (const auto& a : kActions)
        {
            if (a.action == REC_CLIP && !g_recClipper) continue;
            char id[64];
            snprintf(id, sizeof(id), "%s##recorder action", a.label);
            if (ImGui::Button(id, ImVec2(-1.f, 15.f))) Recorder_Request(a.action);
        }
        ImGui::EndDisabled();
    }
    menu_group_end();

    menu_columns_next();

    if (menu_group_begin("routes"))
        Recorder_MenuRoutes();
    menu_group_end();

    menu_columns_end();
}
