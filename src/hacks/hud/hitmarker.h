#pragma once
#include "../../globals/globals.h"

// Hit marker + hit sound: fires off the real "player_hurt" event (attacker == local player),
// confirmed fields in source-sdk-2013-master/game/server/tf/tf_player.cpp:10760 (userid,
// attacker, damageamount, crit, minicrit). No new hook -- called from killfeed.cpp's existing
// FireEvent piggyback via game_events.cpp.
void Hitmarker_OnHurt(int damage, bool crit, bool minicrit, bool killedTarget);
void RenderHitMarker();   // PaintTraverse frame build, screen-centre crosshair feedback
