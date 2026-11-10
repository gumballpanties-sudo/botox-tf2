#pragma once
// Port round 11-01: one var header per feature (inline vars, no dllmain defs).
// Each feature owns ONLY its own vars_<feature>.h / .inc. Primitive types only (aim TU safe).
#include "vars_esp.h"
#include "vars_world.h"
#include "vars_screen.h"
#include "vars_post.h"
#include "vars_hud.h"
#include "vars_wm.h"
#include "vars_spec.h"
#include "vars_misc.h"
#include "vars_game.h"
#include "vars_btchams.h"
#include "vars_points.h"
#include "vars_weaponsel.h"
#include "vars_teamcounter.h"
#include "vars_web.h"
