#include "unitB_menu.h"
#include "menu.h"        // MenuBindKey
#include "../hud/velgraph.h"
#include "../hud/keystroke.h"
#include "../hud/keyind_fx.h"
#include "../hud/movind.h"      // MI_COUNT / kMiNames
#include "../movement/detections.h"
#include "../movement/ps_lines.h"
#include "../movement/recorder.h"

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

void UnitB_Menu_movement_recorder()
{
    ImGui::TextDisabled("movement recorder");
    ImGui::Checkbox("enabled##rec", &g_recEnable);
    if (!g_recEnable) return;

    ImGui::Text("record");
    MenuBindKey("record##reckey", &g_recRecordKey, &g_recRecordMode);
    ImGui::SameLine();
    if (ImGui::Button(Recorder_IsRecording() ? "stop##recbtn" : "record##recbtn"))
    {
        if (Recorder_IsRecording()) Recorder_StopRecord(); else Recorder_StartRecord();
    }
    ImGui::Text("frames: %d", Recorder_FrameCount());

    int trimStart, trimEnd, maxTicks;
    Recorder_GetTrim(trimStart, trimEnd, maxTicks);
    if (maxTicks > 0)
    {
        ImGui::TextDisabled("clipper");
        bool changed = false;
        changed |= ImGui::SliderInt("trim start##rec", &trimStart, 0, maxTicks);
        changed |= ImGui::SliderInt("trim end##rec", &trimEnd, 0, maxTicks);
        if (changed) Recorder_SetTrim(trimStart, trimEnd);

        static char nameBuf[64] = "run1";
        ImGui::InputText("name##rec", nameBuf, sizeof(nameBuf));
        ImGui::SameLine();
        if (ImGui::Button("save##recbtn")) { Recorder_Save(nameBuf); Recorder_Refresh(); }
    }

    ImGui::Separator();
    ImGui::Text("play");
    MenuBindKey("play##playkey", &g_recPlayKey, &g_recPlayMode);
    ImGui::SameLine();
    if (ImGui::Button(Recorder_IsPlaying() ? "stop##playbtn" : "play##playbtn"))
    {
        if (Recorder_IsPlaying()) Recorder_StopPlay(); else Recorder_StartPlay();
    }
    ImGui::Checkbox("original playback viewangles##rec", &g_recLockVa);
    ImGui::Checkbox("lock while aiming to start##rec", &g_recLockGoingToStart);
    ImGui::Checkbox("stop playback on movement##rec", &g_recStopOnMove);
    if (g_recLockGoingToStart) ImGui::SliderFloat("approach distance##rec", &g_recApproachDist, 4.f, 128.f);
    ImGui::Checkbox("force same weapons##rec", &g_recForceWeapon);

    ImGui::Separator();
    ImGui::TextDisabled("routes");
    if (ImGui::Button("refresh##recbtn")) Recorder_Refresh();
    const int count = Recorder_ClipCount();
    const int sel = Recorder_GetSelected();
    if (ImGui::BeginListBox("##recroutes", ImVec2(0, 120.f)))
    {
        for (int i = 0; i < count; ++i)
        {
            const bool isSel = (i == sel);
            if (ImGui::Selectable(Recorder_ClipName(i), isSel)) Recorder_SetSelected(i);
        }
        ImGui::EndListBox();
    }
    if (sel >= 0 && sel < count)
    {
        ImGui::SameLine();
        if (ImGui::Button("delete##recbtn")) Recorder_Delete(sel);
    }
}
