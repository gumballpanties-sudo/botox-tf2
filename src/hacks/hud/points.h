#pragma once
#include "../../globals/globals.h"

// Score popup. TF2 scoring events differ from CS:GO round economy (confirmed:
// "player_death" for frags, "teamplay_point_captured"/"teamplay_flag_event" for objective
// scoring -- source-sdk-2013-master/game/server/tf/tf_gamestats.cpp, tf_player.cpp). Rides
// the same FireEvent piggyback as hit marker/death particles via unitE_events.cpp.
void Points_Push(const char* text, const float color[4]);
void RenderPoints();
