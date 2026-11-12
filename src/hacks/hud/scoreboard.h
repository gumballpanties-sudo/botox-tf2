#pragma once
#include "../../globals/globals.h"

// ==== reveal scoreboard (Aletherium Visuals::UI::RevealScoreboard) ==================
// Two independent reveals, two toggles: g_revealScoreboard = 1, g_revealTeamHud = 2:
//
//   1. scoreboard class icons. CTFClientScoreBoardDialog::UpdatePlayerList branches on
//      "is this player on my team": teammates get the class-icon block, enemies get the
//      nemesis / domination slot instead. Both enemy exits are redirected back into the
//      class-icon block, so enemies show their class.
//   2. match-hud player panels. CTFTeamStatusPlayerPanel::Update only fills a player's
//      health bar when CTFPlayerPanel::GetTeam() matches the local team, so GetTeam is
//      made to answer "local team" while the toggle is on.
//
// Read-only: nothing is sent to the server, no cvar is touched.
// Every sig below was verified unique against the live tf/bin/x64/client.dll and
// bin/x64/vgui2.dll before a line of this was written.

// ==== steam history (g_steamHistory) ==============================================
// Adds "Steam History" to the scoreboard's right-click menu; clicking it opens
// steamhistory.net/id/<steam64> for that player in the steam overlay. Also read-only:
// the only thing it touches is the menu it appends to.
//
void Scoreboard_Install();   // resolve sigs + install hooks (poll thread)
// Install runs from the poll thread long before anyone ticks "debug log", and DebugLog drops
// every line while that is off -- so the `steamhistory:` result was written to nowhere and the
// feature read as untested when it had simply never reported. The line is kept and replayed the
// first time this is called with logging on.
void Scoreboard_LogInstallOnce();
