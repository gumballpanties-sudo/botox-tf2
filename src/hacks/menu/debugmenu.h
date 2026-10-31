#pragma once
#include "../../globals/globals.h"

// Trivial debug overlay: internal DLL state at a glance (fps/ping, eject flag, hook depth
// counters already exported for exactly this kind of read). No new hook, no probe.
void RenderDebugMenu();
