#include "detections.h"
#include "detections_core.h"
#include "../aim/aim.h"
#include "../hud/points.h"            // botox points_on_trick
#include "../misc/misc_overlays.h"    // botox on_healthshot
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
    // botox run_detections prev_ok / prev_vz, read before DetEbStep rolls them (points: jb / head)
    const bool prevOk = eb.prevAir && eb.prevTick == t.tick - 1;
    const float prevVz = eb.prevVz;
    if (DetEbStep(eb, st, g, t.tb, t.ps))
    {
        ++chain;
        if (!g_detectionStackEb) PrintEb(chain);
        Healthshot_Trigger(1);                 // unit misc: botox on_healthshot( 1 ) + points_on_trick
        Points_OnTrick(PTS_EDGE_BUG);
    }

    // unit misc: botox detect() -- fresh = the trick (points + print), still held = its ride bonus
    const auto det = [](bool now, int& off, int trick) {
        if (DetFresh(now, off)) { Points_OnTrick(trick); return true; }
        if (now) Points_OnTrick(trick + (PTS_PIXEL_SURF_RIDE - PTS_PIXEL_SURF));
        return false;
    };
    if (det(t.tb, tbOff, PTS_TEXTURE_BUG)) Print(DET_TB, "texturebugged");
    if (det(t.as, asOff, PTS_AIR_STUCK)) Print(DET_AS, "airstucked");
    if (det(t.ps && !t.as && !t.tb, psOff, PTS_PIXEL_SURF)) { Print(DET_PS, "pixelsurfed"); Healthshot_Trigger(2); }
    if (DetFresh(t.wc, wcOff)) { Print(DET_WC, "wallclimbed"); Points_OnTrick(PTS_WALL_CLIMB); }
    // botox jump bug / head ceiling points (fireman needs a ladder, TF2 has none)
    if (prevOk && prevVz < 0.f && st.air && t.vz > 250.f) Points_OnTrick(PTS_JUMP_BUG);
    if (prevOk && prevVz > g * 2.f + 1.f && st.air && fabsf(t.vz + g) < 1.f) Points_OnTrick(PTS_HEAD_CEILING);
}
