#pragma once
#include "../../globals/globals.h"

// misc HUD's single piggyback point on the FireEvent hook (killfeed.cpp owns the install).
// Routes player_hurt/player_death/teamplay_point_captured/teamplay_flag_event to hit marker,
// hit sound, death particles and the score popup -- one dispatcher so killfeed.cpp gets one
// call line instead of four.
void GameEvents_Dispatch(void* pEvent);
