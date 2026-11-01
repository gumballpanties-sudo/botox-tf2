#pragma once
// Port round 11-01: one var header per fan-out unit (inline vars, no dllmain defs).
// Each unit owns ONLY its own u_<unit>.h / u_<unit>.inc. Primitive types only (aim TU safe).
#include "u_esp.h"
#include "u_world.h"
#include "u_screen.h"
#include "u_post.h"
#include "u_hud.h"
#include "u_wm.h"
#include "u_spec.h"
#include "u_misc.h"
#include "u_game.h"
