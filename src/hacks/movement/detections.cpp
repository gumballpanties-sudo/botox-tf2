#include "detections.h"
#include "detections_core.h"
#include "../aim/aim.h"
#include <cstdio>

// botox push_notice detection_html: accent label, " | event" in #bebebe
static void Print(DetType type, const char* event)
{
    if (!g_detections || !(g_detectionTypes & (1 << type))) return;
    char label[64]; int n = 0;
    for (const char* p = g_detectionLabel; *p && n < 63; ++p) if (*p != '%') label[n++] = *p;   // ChatPrintf formats
    label[n] = 0;
    char line[160];
    snprintf(line, sizeof(line), "\x07%02X%02X%02X%s\x07" "BEBEBE | %s",
             (int)(g_accentColor[0] * 255), (int)(g_accentColor[1] * 255), (int)(g_accentColor[2] * 255),
             n ? label : "botox", event);
    Aim_ChatPrint(line);
    DebugLog("DET: %s", event);
}

static void PrintEb(int chain)
{
    char ev[32];
    if (chain > 1) snprintf(ev, sizeof(ev), "edgebugged x%d", chain);
    else           snprintf(ev, sizeof(ev), "edgebugged");
    Print(DET_EB, ev);
}

void Detections_Tick(const DetTick& t)
{
    static DetEb eb;
    static int chain = 0, groundTicks = 0;
    static int tbOff = 1 << 30, asOff = 1 << 30, psOff = 1 << 30, wcOff = 1 << 30;

    // chain ends on 2 ground ticks or a jump (botox). Stack mode: TF2 chat cannot rewrite a line
    // the way botox's panorama restack does, so the streak prints once, when it ends.
    // ponytail: stack = print-on-end; live in-place update needs a CHudChat history patch
    groundTicks = t.onGround ? groundTicks + 1 : 0;
    if (chain && (groundTicks >= 2 || (t.onGround && t.jumpPressed)))
    {
        if (g_detectionStackEb) PrintEb(chain);
        chain = 0;
    }

    DetStart st;
    st.air        = !t.onGround && t.walk;
    st.landedWalk = t.onGround && t.walk;
    st.vz = t.vz; st.speed = t.speed; st.tick = t.tick;
    const float g = t.gravity * 0.5f * t.interval;
    if (DetEbStep(eb, st, g, t.tb, t.ps))
    {
        ++chain;
        if (!g_detectionStackEb) PrintEb(chain);
    }

    if (DetFresh(t.tb, tbOff)) Print(DET_TB, "texturebugged");
    if (DetFresh(t.as, asOff)) Print(DET_AS, "airstucked");
    if (DetFresh(t.ps && !t.as && !t.tb, psOff)) Print(DET_PS, "pixelsurfed");
    if (DetFresh(t.wc, wcOff)) Print(DET_WC, "wallclimbed");
}
