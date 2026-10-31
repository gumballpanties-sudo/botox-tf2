#pragma once
#include "../../globals/common.h"

// ==== matchmaker unstick ============================================================
// Clears the client-side CTFGCClientSystem state that deadlocks the "match found" /
// "your party is in a match" popup. See mmfix.cpp for the disassembly evidence.

void MMFix_Install();   // resolve sigs + field offsets (poll thread)
// The two buttons only QUEUE. Both end in CTFGCClientSystem calls that walk the GC's SO cache
// and republish to the vgui panels, and the menu runs on the RENDER thread -- doing that work
// there races the main thread that owns both. MMFix_Pump() is what actually runs them, from
// CVGui::RunFrame (scoreboard.cpp), which is the main thread and ticks in the menu as well as
// in game. If that hook never installed, Pump is never called and the request is run inline on
// the next button press instead, with the status line saying so -- a button that silently does
// nothing is worse than the race it avoids.
void MMFix_Run();       // the fix button: clear the stuck local flags
void MMFix_Abandon();   // tell the GC to drop the match (heavier, can penalize)
void MMFix_Pump();      // main thread: run whatever the buttons queued + f2p chat bypass tick

// true while this client is on (or loading into) a Valve matchmade server. Read off
// CTFGCClientSystem::m_eConnectState, the offset MMFix_Install already resolves.
// Fail-safe: says true when it cannot tell.
bool MMFix_OnMatchmadeServer();

extern bool g_mmFixReady;        // sigs resolved
extern char g_mmFixStatus[192];  // last diag line, shown in menu
