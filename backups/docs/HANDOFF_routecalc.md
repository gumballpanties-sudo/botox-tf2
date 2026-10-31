# Route calculator handoff (dihrc31, 2026-09-23)

## rc31 (built, 1026/1026 routesim + menu tests) -- SUGGESTIONS REMOVED

User asked for it. Gone: `g_rcSuggest` + cfg key `rcSuggest` (old cfg lines are ignored) + menu
"suggestions" checkbox, `RcDrawData::hasSuggest/suggest` + the dashed "suggested" ring in
render.cpp, `RcLedgesNear` + the whole suggestion branch of `RcSolveFinish` + `s_rcLocal` /
`s_rcRoute` / `s_rcParams` / `s_rcMoves` in routecalc.inl, `rs::Helper` / `HelperSolve` /
`HelperAt` / `FindHelpers` in routesim.h, `tests/helperpt_test.cpp` + its CMake target. Kept:
the dead-marker diagnosis lines ("no combination reaches marker N", "missed its window by X"),
`Result::closestDz` (routesim_test section 34 still pins it). The rc27-rc29 sections below
describe code that no longer exists. Backups: `backups/src_baks/**.bak_20260923_203930`.

## rc30 (built, gate tests 33/33 rcedge + 2071/2071 helperpt + 1026/1026 routesim, UNTESTED in game) -- an edgebug marker's z is its ledge's LIP

Report: "in route calc make edgebug points use the z of nearest edge instead of the z of its
placement position since its hard to place a point right at the edge of a slanted brush".

A ground marker is the height you LAND at, so where the crosshair hit is the answer for it. An
edgebug is the height you SLIDE OFF at, which is the lip -- and on a slanted brush the few units
between your click and the lip are the few units the bug is decided by. Clicking a lip through a
crosshair at range is not a thing anybody can do, so the marker stops asking.

* `src/hacks/movement/rc_edge.h` (new, pure, no engine): `rce::NearestEdge(x, y, zRef, floor, out)` --
  16 rays out from the marker, a floor sample every `k_step` 2u to `k_max` 64u. A ray ends on
  VOID or a drop taller than `k_drop` 18u (that is a lip), or on a rise taller than 18u / a
  BLOCKED sample (that is a wall, and a wall is never a lip). Nearest lip of all the rays wins,
  then 6 bisection steps put it within 0.03u -- 2u of a 45 deg brush is 2u of z, which is the
  whole number this exists to get right. `zRef` walks with the surface, so a long ramp is
  followed instead of being cut off one step down it.
* THE SAMPLER IS TRI-STATE on purpose (`kFloor` / `kVoid` / `kBlocked`). The engine-side ray
  starts one step OVER the last floor, so a wall standing on that floor swallows it and comes
  back `startsolid`. Read as void, the base of every wall in the map would be a lip you slide
  off. `tests/rcedge_test.cpp` section 5 pins it.
* `RcEdgeAt` (routecalc.inl) is the trace side: `TraceLine` from `zRef + 18` down 128u,
  `TraceFilterWorldProps`, `startsolid/allsolid` = kBlocked, no hit or `normal.z <= 0.7`
  (CategorizePosition's own cut) = kVoid.
* `RcEdgeSnapPass(pLocal)` runs from `RouteCalc_Tick` right after `RC_RefreshLive`: one scan
  (~600 traces, one spike) per marker whose row is switched to edgebug. `RcPoint::baseZ` holds
  the z you clicked and is put back the moment the row is switched off edgebug; `edgeTried`
  stops a failed scan from re-running every command; entity-pinned markers are skipped, because
  `RC_RefreshLive` rewrites their z to the hull top every frame anyway. Jobs are traced OUTSIDE
  `g_rcLock` and re-checked (index + position) before they are applied.
* xy stays exactly where you placed it -- only the z moves. The menu row prints " (edge)", the
  panel message says `marker N: edgebug z X - the edge Yu away (placed z Z)`, and debug.log's
  `RC: edgebug marker` line carries the lip's own xyz.
* Gate test: `cmake --build build --config Release --target rcedge_test && build\Release\rcedge_test.exe`
  -- flat top, 45 deg brush (the case this exists for), nearest-in-any-direction, 24u drop vs 12u
  step, wall both ways, marker over nothing, out of reach, long ramp, bisection accuracy.

First in-game check: place a ground marker on a slanted ledge, switch the row to edgebug, and
read `RC: edgebug marker` in `C:\dih\debug.log` -- the lip xyz it prints is where the z came
from, and the row's z should be the lip's, not the click's.

## rc28 (built, gate tests 1529/1529 + 1026/1026, UNTESTED in game) -- the suggestion is an EXTRA marker

Report: "route calc suggestions dont work.. theyre supposed to suggest where you could place an
additional point to hit a ps not an alternat ps".

rc27 answered the wrong question. A pixel surf marker sits where the finder pinned a pixel, so
"that seam 3u lower would have been caught" names a different seam, not the one you aimed at. The
fix for a marker nothing reaches is a marker **in front of** it: a ledge to land on, press off, and
fly the dead leg again from there.

- **`rs::Helper` + `rs::FindHelpers`** (routesim.h, end of file) -- given the dead marker, the
  moves mask, a ceiling and a band, it returns up to N launch heights that reach it, highest first,
  each one flown to the marker by the solver before it is handed back (no false positives).
  - **The ceiling** is the failing leg's own feet apex (`route[dp].z + highest[dp]`, minus a
    standing hull for the head types): nothing over it can be landed on on the way there.
  - **The band** is `k_rcSuggestNear` (64u) under that ceiling, walked top down -- a ledge near the
    apex is the short detour, and past a hull's height it is a different route anyway.
  - **Why it is a search and not a scan**: an arc off a floor is the same shape shifted with the
    floor, so a launch moved by d moves every tick by d. The heights that catch a 0.04u window are
    a comb, so `Result::closestDz` (rc27's signed near miss) names the next tooth in ONE solve.
  - **One press per walk.** `Result::closest` is the nearest miss of every arc a solve flew, so a
    solve holding three presses snaps toward whichever press landed nearest and never sees the
    other two's teeth -- and they are not in the same places. 12 solves each, 36 in all, ~0.3ms.
  - **Snap at the window's CENTRE, not its edge** (`-closestDz +- 0.02`). `d < kSurfHi` is open, so
    the edge is a miss and the next snap is 0: the walk stalls there and burns the whole budget.
  - **The step between teeth is one falling tick at the seam**, `sqrt(2 g highest) * dt` -- 0.2u
    where the seam is just under the apex, 7u where the arc is coming down at speed. A flat 7u step
    walked straight over the tooth 2u under the apex (the one worth printing).
  - **A snap over the ceiling** is the wrong tick, not a dead end: walk it down whole gaps. An arc
    that never got over the marker at all has no such tick -- there only PixelSurf's +20 air duck
    catches it, so the candidate is 20u (`DuckShift`) lower.
- **The panel** (routecalc.inl `RcSolveFinish`): `suggestion (ring): add a marker between marker 1
  and marker 2, on anything at z -190.317 - off it: longjump -> pixelsurf (stand)`, plus a second
  line with up to two more heights. The pixel calculator has no route to add a marker to, so it
  says `land on something at z X on the way in` (its line still goes to `C:\dih\debug.log`). The
  dashed `?` ring is drawn at the **middle of the dead leg** at that height -- the horizontal half
  is yours, any surface at that height between the two markers is the spot.
- `RC: helper search -- marker N, apex z X, band 64u, N solve(s), N spot(s)` in the debug log.
- Gate test **`tests/helperpt_test.cpp`** (new): the shift premise tick by tick, then 24 bands
  against a **0.01u brute force scan** with the assist off -- every height printed really catches,
  the search is never empty on a band the scan found one in (22/22), and the first height printed
  is within **0.03u** of the highest the scan could find. Then the same with PixelSurf on (answers
  re-flown through the solver), a ground marker, an out-of-reach marker (empty, as it must be), and
  the budget (36 solves, 0.3ms a search).
- rc27's "a seam at z X" / "marker N at z X or lower" line is **gone**. `routesim_test` section 34
  still checks `closestDz`/`highest` -- they are what the walk snaps by now.

## rc27 (built, gate test 1022/1022, UNTESTED in game) -- suggestions: where the marker had to be (REPLACED by rc28)

Report: "add a 'suggestions' option in route calc where it will tell you where to put a point if
your calculation didnt hit but is very close".

A failed solve always knew how close it came (`Result::highest` = how far under the marker the best
arc peaked, `Result::closest` = how far a surf marker's nearest falling tick missed its 0.04u
window). It could not say WHERE, because a distance has no side to it. Two changes:

- **`Result::closestDz`** (routesim.h) -- the same near miss as a SIGNED z shift: + = the seam
  wanted to be higher, - = lower. Same arc either way: a surf marker is not a floor, so moving its
  z does not move the flight that missed it.
- **The suggestion** (routecalc.inl `RcSolveFinish`, menu *route calculator -> suggestions*, cfg
  `rcSuggest`, default on). When nothing completes the route and the dead marker missed by **64u or
  less**, one line names the spot and a **dashed ring with a `?`** is drawn there -- the marker's
  own xy at a height the flown arcs really reach:
  - ground / edgebug: `marker 3 at z X or lower - 3.20u down, the highest the feet reach`
  - headbang / texturebug: same, measured off the head. The apex is the EDGE of the range, not a
    spot that bangs -- a ceiling exactly level with the top of the head is touched and never
    blocks the move (test: 4 of 4 heights under it bang, the apex itself does not), which is why
    the line reads "or lower" at both ends of the hull
  - pixelsurf: `a seam at z X - 0.420u lower, the nearest height this fall can catch` (printed to
    a thousandth: the window is 0.04u wide, and `%.2f` could name a spot outside it)
  The ring follows the markers of whichever feature solved it, so the pixel calculator gets it too
  (its panel is combos-only, so its line goes to `C:\dih\debug.log` and the ring to the world).
- Gate test `routesim_test` section 34: ten seam misses from -12u to +12u, each one's suggestion
  re-solved -- **all ten solve where the suggestion points**. Past a unit or two the nearest
  falling tick is the NEXT one down, not the one you were aiming at, so the answer is "the nearest
  height this fall can catch" and the test asserts that rather than "back where you meant".

## rc26 (built, gate test 949/949, UNTESTED in game) -- no 'place as', no 'hop speed', hops on your flat

## rc26 (built, gate test 949/949, UNTESTED in game) -- no 'place as', no 'hop speed', hops on your flat

Backups: `backups/src_baks/*.bak_20260921_*`.
Build: `cmake --build build --config Release` -> `build\Release\routesim_test.exe`
-> `OK: 949 of 949 checks passed` (was 943; section 33 is new).

Report: "remove these from pixel and route calc [place as + hop speed], make it so if a point is
placed on a ceiling its automatically a headbang, also make it so you can rearrange points in the
menu. make pixel calculator consider combos on the flat plane youre standing on - not just single
jumps. also make pixel calc points look like regular circles but with a p inside of them."

**'place as' is gone.** `g_rcPlaceType`, cfg key `rcPlaceType` and its clamp are deleted; an old
cfg's line is skipped on load like any unmatched key. `RcAddPoint` reads the face: `normal.z > 0.7`
= **ground**, `normal.z < -0.7` = **headbang**, anything between = **refused** ("that face is a
wall"). A player head and a building top are still grounds. An **edgebug** is a ground marker whose
row in the menu is switched to edgebug -- it is the same ledge face, read differently.

**'hop speed' is gone.** `g_rcHopSpeed` + cfg `rcHopSpeed` + both sliders deleted.
`params.cropFrac` stays 1, which is what the slider's 0 already meant: every bhop in an answer is
one under 1.2x class max speed, where PreventBunnyJumping leaves a standing hop alone.

**Markers re-order.** `RC_PointMove(i, dir)` (routecalc.inl, movement.h) swaps a marker with its
neighbour under `g_rcLock`, bumps `s_rcGen` and drops the answer (it was for the old order). The
open marker row in the menu grows "move up" / "move down" (disabled at the ends); the open row
follows the marker and the row loop stops for that frame.

**Pixel calculator: hops on the flat you stand on.** One press now runs `k_pcMaxHops + 1 = 4`
solves back to back: pass k puts k **ground markers at your own floor z** (`start.floorEff`) in
front of the pixel, so the search may land back on your flat and press again, or **jumpbug** off it
(leaving the floor 20-22u up instead of touching it). Passes are queued by `s_pcNextHops` and
started from `RouteCalc_Tick` (it has the live cmd); each pass's block is stacked under the ones
before it (`s_pcStack`), the engine check runs once, the drawn arc belongs to the first pass that
answered, and only the last pass explains a pixel nothing reached. Later passes print half the
rows. Airborne = one pass (no flat of yours to come back to).
Gate test section 33 is the reason it exists: over floor+1u..floor+90u, **30 pixel heights are
reachable only with a hop, 19 of them with a jumpbug chain** (first is +8.00u,
`longjump -> jump -> pixelsurf (ducked)`).

**Pixel marker** draws as the route markers do: a 10px ring with a lowercase `p` in it (was a
square with `P`).

In game check: aim at a ceiling, press place -> `headbang` marker. Aim at a wall -> refused. Place
3 markers, open #3, "move up" twice -> it is #1 and the path re-draws in the new order. Place a
pixel, stand on a flat, calculate pixel -> the panel fills with the single presses first, then
`+1 hop on your flat first`, `+2`, `+3` blocks; the console prints the lot once.

## rc25 (built, gate test 943/943, UNTESTED in game) -- the finder ring names the type, bind gone

Backups: `backups/src_baks/**/*.bak_20260920_*_pre_rc25`.
Build: `cmake --build build --config Release --target dih` and `--target routesim_test`
-> `OK: 943 of 943 checks passed` (routesim.h untouched, the picker moved).

Report: "dont make route calc have a marker type bind, make it recognize pixelsurfs and
texturebugs by seeing what type of pixel finder point im placing them on."

`rcTypeKey` is deleted (globals.h, dllmain.cpp, config.cpp cfg key, menu bind row, the cycle block
in `RouteCalc_Tick`). An old cfg's `rcTypeKey` line is skipped on load like any unmatched key.

- `RcPickTyped` -> **`RcPickFinder(vEye, vFwd, bPsOnly, pos, bTb, nSeen)`**: searches grey pins +
  assist targets AND red seams in one pass, and reports which set the winner came from. `bPsOnly`
  keeps `RcPlacePixel` on grey rings (its marker is a pixelsurf by definition).
- `RcAddPoint` runs the finder pick FIRST, whatever the combo says: a hit types the marker
  `PT_SURF` (grey) or `PT_TEXTUREBUG` (red seam) and places it. No hit -> the old surface path,
  typed by 'place as'. The seam-type refusal branch is gone: there is nothing to refuse, a miss is
  just a surface marker now.
- This is NOT rc21's "nearest point of any kind" bug coming back. rc21 ranked raw perpendicular
  distance in an unbounded pool, so a 0.4u-off red seam beat the grey ring you aimed at and any
  ledge lip near the ray ate a ground marker. The window is `rs::AimTanTo` (2.5 deg, floor 2u, cap
  64u) and ranking is by angle: the ring has to be UNDER the crosshair, not near the ray.
- 'place as' combo is **ground / edgebug / headbang** only (menu.cpp, a 3-entry combo mapped onto
  `rs::PointType` 0/2/3). `g_rcPlaceType` still stores the enum and the cfg clamp still allows 0-4;
  a seam value from an rc24 cfg reads as ground in `RcAddPoint`.
- The wall refusal adds "for a pixelsurf / texturebug marker, aim at a finder ring itself" when the
  finder has points on the map -- aiming just off a ring is the way to hit it.

In game check: run the finder on a wall. Aim at a grey ring -> `pixelsurf` marker on its z. Aim at
a red seam -> `texturebug`. Aim at bare wall -> refused, "that face is a wall" + the ring hint.
Ledge top with 'place as: ground' -> ground; with edgebug -> edgebug. Ceiling with headbang ->
headbang. No marker-type bind in the menu at all.

## rc24 (built, gate test 943/943, UNTESTED in game) -- markers stop guessing their own type

Backups: `backups/src_baks/*.bak_20260920_2213*_pre_rc24`.
Build: `cmake --build build --config Release --target dih` and `--target routesim_test`
-> `build\Release\routesim_test.exe` -> `OK: 943 of 943 checks passed` (was 920; section 32 is new).

Report, four parts: "click a ps point, it puts a tb point / want an eb point, get a ps point /
want a ground point on a thin ledge, get a ps point / put a ps point on a pixelsurf point from the
finder and it lands nowhere near it."

All four were one design: **the crosshair chose the type.** `RcAddPoint` snapped to the nearest
point of ANY kind (`RcPickFinder` searched finder pins, assist targets and red seams in one pool)
and then read the type off whatever it found, falling back to the surface normal. So a red seam
0.4u from the grey ring you aimed at won and typed the marker texturebug; every ledge lip within
the window dragged a ground marker onto itself and typed it pixelsurf; and an edgebug could not be
placed at all, since nothing in the function ever produced one.

### 'place as' -- `g_rcPlaceType` (cfg `rcPlaceType`), menu combo + `rcTypeKey` bind
You say what you are placing, and placement only has to deliver it:
- **pixelsurf** -- a pixel finder pin (`s_psFinderPts`) or an assist target saved off one, and
  nothing else. **texturebug** -- a red seam (`s_psTbPts`) and nothing else. The two sets are never
  searched together. Nothing of that kind under the crosshair = **no marker**, and the message says
  whether the finder found any at all ("run the pixel finder on that wall first" vs "aim at one of
  the 37 grey finder rings"). This is the "can't place a ps point unless the finder found that ps"
  half of the ask, and `RC_PointSet` enforces it too, so the menu's per-marker type combo cannot
  turn an unsnapped marker into a pixelsurf either.
- **ground / edgebug** -- the floor face under the crosshair, no snap at all. A wall face is
  REFUSED with its normal z in the message rather than stored at whatever height the crosshair was
  at (0.7 = `CategorizePosition`'s own standable cut). **headbang** -- the ceiling face, same rule
  inverted. A player head and a building top stay ground markers whatever the combo says, and the
  message says so.
- The bind cycles the combo in game (a route is placed standing in the spot, not with the menu
  open) and prints `place as: <type>` on the panel.

### Picking a drawn point is an ANGLE now (`rs::AimTanTo`, routesim.h, gate-tested)
`perp / along` = tan of the angle from the aim ray to the point = how far its ring sits from the
crosshair on screen. Window `tan(2.5 deg)`, floored 2u (a 1u-spaced finder column stays separable
point blank) and capped 64u. Smallest angle wins, so a centred far ring beats an off-centre near
one -- rc21 ranked raw perpendicular distance and called those a tie.
**Dropped: rc21's "the point may not sit more than 16u past the surface the ray hit."** That clause
is report 4: aim at a point under a lip, the ring of rays catches the lip 200u in front, the point
is "past the hit" and the snap is refused, so the marker lands on the lip. A ring is a thing you
aim AT; what the ray bumps into on the way does not get a vote. `RcDeletePoint` uses the same rule
and no longer traces at all (a marker legitimately hangs in the air).

### The pixel calculator obeys the same rule
`RcPlacePixel` was accepting any wall hit with a "snap it for an exact z" note. That z was wherever
the crosshair happened to be on the wall -- an exact answer to a question about a pixel that is not
there. It now places on a finder point or refuses, and the panel's "(not snapped)" case is gone.

### In game check
Run the finder on a wall. `place as: pixelsurf` + aim at a grey ring -> pixelsurf on that ring's z,
never a red one. `place as: texturebug` + aim at a red ring -> texturebug. Either one aimed at bare
wall -> no marker, message. `place as: ground` on a thin ledge top -> ground at the ledge top; aim
at the ledge's SIDE -> refused, "that face is a wall". `place as: edgebug` on the same top ->
edgebug. Cycle key changes the combo. Delete: aim at a marker ring, that one goes.

Files: `src/hacks/movement/routecalc.inl` (picker + placement), `src/hacks/movement/routesim.h` (`AimTanTo`),
`src/hacks/menu/menu.cpp` (~2092-2115, 2177), `src/globals.h`, `src/dllmain.cpp`,
`src/hacks/menu/config.cpp`, `tests/routesim_test.cpp` (section 32).

## rc21 (built, gate test 825/825, UNTESTED in game)

Backups: `backups/src_baks/**/*.bak_20260920_170555_pre_rc21`.
Build: `cmake --build build --config Release --target dih` and `--target routesim_test`
-> `build\Release\routesim_test.exe` -> `OK: 825 of 825 checks passed` (was 643; every rc20 check
still runs, sections 26-29 are new).

### Markers stop snapping to ledges -- the crosshair picks (`RcPickFinder`, routecalc.inl)
The snap was "nearest finder/assist/seam point within `g_psAssistSnap` (48u) of the world hit": a
ball around the hit, and since a pixel finder point IS a lip, every ledge in range pulled markers
onto itself. Now a point counts only when the aim RAY passes through it (`RcRayOff`): perpendicular
distance < `along * tan(1.5deg)`, floored 2u, capped 16u, and the point may not sit more than 16u
past the surface the ray hit. Closest to the ray centre wins; nothing in the window = no snap and
the marker stays exactly where the ray landed. `RcDeletePoint` uses the same rule (it deleted the
wrong marker whenever two sat on one ledge).

### Buildings get their real height
Any hit on a sentry / dispenser / teleporter now puts the marker on the hull TOP
(`origin.z + m_vecMaxs.z + 1/32`, the same rule `RcAimPlayerHead` uses for a head) and types it
ground + pinned to the entity. Before, the marker kept whatever face the ray hit, so a sentry's
side came out as a "pixelsurf" partway up a 66u box. SDK sizes: sentry 66, dispenser 55, mini 20,
teleporter 12. A building whose `m_CollisionGroup != 22` is not solid to player movement
(baseobject_shared.cpp:468) and the placement message says so -- you cannot stand on it.

### The line between markers is the answer's own arc (`rs::Solver::TraceBest`)
`Edge` now carries the plan that flew it (`sc` / `jb` / `ride`, 3 bytes). After a solve,
`RcSolveFinish` re-flies the FIRST printed combination edge by edge and keeps the port's feet z
after every cmd; `RcDrawData::path` is that arc, spread along the straight line between the markers
(the horizontal is still the player's). The replay must reach the same arrival key the edge was
built from, or the path is dropped and the old straight line is drawn. New checkbox
**show path** (`rcShowPath`, default on) turns the line off entirely, arc or not.

### Minijump you cannot press ("it says minijump, I just jump twice")
Root cause: every duck RELEASE sets `m_flDuckTimer = curtime + 0.3` (tf_gamemovement.cpp:3295) and
for those 20 ticks `DuckOverrides` (:3194) strips IN_DUCK off every grounded cmd. A minijump is
jump + duck on one grounded cmd, so inside the window dih's MiniJump refuses to send the duck
(movement.cpp:1320) and the press comes out a plain jump. You climb onto a building with a crouch
jump, so up there the timer is nearly always still running -- hence "off a building".
rc20 WIPED that timer for a `start here` solve (`start.s.releaseTick = INT_MIN/2`) along with the
crouch, so the panel offered both ducking presses inside the window. rc21 keeps the timer (the hull
is still read as standing -- a crouch is a key, the timer is not), and the panel prints
`duck timer: N tick(s) left ...`. dih's MiniJump also logs `MJ: no mini this takeoff -- ...` with
the ms left, so the next report has evidence in `C:\dih\debug.log`.
Gate test 26 walks all 20 ticks: no minijump/longjump offered, plain jump unaffected.

### In game check
Place a marker on a floor a few feet from a ledge that has finder points: it stays put; aim at the
ring and it snaps. Aim anywhere on a sentry/dispenser: marker sits on its top. Solve: the line
becomes an arc that rises and falls like the first combo reads. Crouch, then calculate immediately:
the duck timer line shows and minijump/longjump are gone until it runs out.

# Route calculator handoff (dihrc20, 2026-09-20)

## rc20 (built, gate test 643/643, UNTESTED in game)

Backups: `backups/src_baks/**/*.bak_20260920_120739_pre_rc20`.
Build: `cmake --build build --config Release --target dih` (clean) and `--target routesim_test`
then `build\Release\routesim_test.exe` -> `OK: 643 of 643 checks passed`, 1500/1500 random routes
identical to the independent brute force (which now models texturebugs too).

### TEXTUREBUG markers -- `rs::PT_TEXTUREBUG` (type 4, last in the enum so old files are untouched)
The wall seam (antoha bug). Two coplanar faces the collision solver clips separately: a swept hull
whose SIDE spans the line touches both at once, TryPlayerMove's two-plane branch projects velocity
onto the horizontal crease and zeroes vz, FinishGravity puts back one half step. The move never
moved the origin, so z is exactly where the tick started.
- Arrival (`routesim.h` Arrive, case PT_TEXTUREBUG): feet `pre.z`, `vz = HalfGrav`, `onSeam = true`
  -- vertically identical to a pixelsurf catch, so it RIDES like one (Expand enumerates 0..17).
- Window: `pre.z + 1/32 < T.z < pre.z + hull - 1/32`. Not invented: `texturebug.inl:638` picks its
  own seam tick with `seam_z >= z + mins.z && seam_z <= z + maxs.z` and gives up on
  `z + maxs.z < seam_z`. `vz == -halfgrav` is that file's own pin test (`TargetPredictZVel`), and
  its dih152 log rows read `pz == oz`.
- Every straddling tick is a separate arrival and the arc flies ON past it (a pixelsurf stops the
  leg): where in the band you push in is the player's, like the rest of the horizontal. Later
  catches are labelled `texturebug (stand) +N ticks`.
- Bug found and fixed on the way: `RideText` built `surfAt` from steps starting "pixelsurf" only,
  so a ride after a texturebug left `P == 0` and `sets[pi]` wrote off an empty vector. **Crash.**
  Caught by random route 42 in the gate test. Fixed + a bounds guard.
- Placement: a marker snapped to one of the pixel finder's TB SEAM points (the red rings,
  `psassist.inl` `PfScanSeams` -> `s_psTbPts`) is typed texturebug; the menu type combo can also
  set it by hand. Run the finder on that wall first or there are no seam points to snap to.

### Edgebug
Already modelled (PT_EDGEBUG, ducked/standing distinguished in the printed step). New: the arrival
stance is one dropdown (`stand only / ducked only / stand or ducked`) instead of two checkboxes
that could both come off, and it is labelled "edgebug stance" on an edgebug marker.

### Pixel calculator is its own feature
`g_pixelCalc` + `g_pcShowMarker` + `g_pcShowPanel` (cfg keys `pixelCalc`, `pcShowMarker`,
`pcShowPanel`). Own menu group after the route calculator, own keys, own marker + panel toggles.
`RouteCalc_Tick` runs for either feature (movement.cpp) and each feature's keys only answer while
it is on. `RcDrawData::pixelSolve` says which feature owns the answer on screen. Shared: the
moves mask, hop speed, budget, max shown -- one solver, one set (the moves + hop speed are
repeated in the pixel group so it is usable with the route calculator off). "clear markers" no
longer drops the pixel marker; it is the other feature's.

### Points follow their feature's switch
- route markers: `g_routeCalc && g_rcShowMarkers`; pixel marker: `g_pixelCalc && g_pcShowMarker`.
- panel: whichever feature produced the answer.
- pixel finder points + red TB seams: `g_psFinder` (were drawn whenever the assist's "show points"
  was ticked). assist points: `g_psAssist && g_psAssistRender` (was "show points" alone, which
  outlived the feature).

### Marker placement fixes
- **Own buildings.** `TraceFilterRcMarker` (routecalc.inl): asks `TraceFilterWorldProps` first, and
  overrules its team rule for anything whose NETWORK CLASS is a building. The movement sim still
  runs the engine's own rule (collision group 22 = solid to player movement); the marker ray does
  not need to, and everything the group check gets wrong (teleporters, mid-carry, an unresolved
  `m_CollisionGroup`) it got wrong silently. The marker log now prints team + group + contents +
  surface name, so one press settles it in `C:\dih\debug.log`.
- **Thin railings / props.** `RcAimWorld`: the centre ray plus a ring of 8 at 1.1 degrees, nearest
  hit wins, and a ring hit must beat the centre by 2u to take it (a flat wall answers every ray at
  the same distance, so the ring can only win when something genuinely nearer is inside it).
- **Clip brushes.** A crosshair inside a player clip volume came back `startsolid` -- fraction 0,
  endpos = YOUR EYE, zero normal -- which the old code read as a hit and dropped a pixelsurf
  marker on your own face. `RcRay` retries without `CONTENTS_PLAYERCLIP` so the ray reaches the
  geometry you can see. A clip the ray runs INTO is kept (it is the surface you stand on) and the
  message says so, because an invisible surface reads as a misplaced marker.

### Two more
- Crouching while you press calculate no longer changes the answer: a GROUNDED start is read as
  the settled standing hull (`RcSolveBegin`). Every press starts with a jump, RunLeg stands the
  hull up first anyway, and the 0.3s duck timer a release starts was eating longjump/minijump
  ducks. A MID-AIR start keeps its real duck state -- there the stance is the arc.
- The "engine and the calculator disagree ... (walls/ceiling near you?)" line is gone from the
  panel. It still runs and still logs (`RC: engine check`).

### In game check
Own sentry/dispenser takes a marker; a handrail takes one without pixel aim; standing in a clip
volume and aiming somewhere puts the marker there, not on your face; crouch vs stand on the same
spot gives the same answer; a marker on a red seam point reads "texturebug" and solves; route calc
off = no route markers, pixel calc off = no pixel marker, finder off = no finder/seam points.

## rc19 (built, UNTESTED in game) -- tasks 1 + 2 below, done from code (debug.log was empty)
- Jitter root: multicore -> overlay built on render thread (h_EndScene) with the engine W2S
  matrix the main thread had already rewritten for the NEXT frame. Fix (render.cpp): main thread
  at vgui FocusOverlayPanel snapshots the matrix into a fake CFunctor ring (OvFunctor x8), queued
  via `ScreenFx_QueueFunctor` (screenfx call queue); render thread `OvFn_Call` grades, builds,
  draws at the vgui point with that matrix. EndScene defers while it ran last frame (`s_ovDefer`).
  mat_queue_mode 0 path unchanged. Pixel snap kept (DIH-1). Also fixes ps finder/assist markers.
- Players: head test used m_vecNetworkOrigin (~100ms ahead of drawn body) -> now
  `LagRes_RenderOrigin` (own 16-try budget, never burns lagres's). Teammate in front: world marker
  placed + note; teammate only: message. Markers pin to entity by full HANDLE (`RcPoint::ent`),
  `RC_RefreshLive` (vgui paint + each tick) re-pins; pin ends on gone / reused slot / death /
  >250u jump. Buildings pin too. Log: `RC: marker aim -- trace frac .. ent .. kind .. | player ..`.
- Cold critic ran: fixed ColorCorr order, eject hookDepth order, handle reuse, oHit offset, budget.
  Skipped: with lag resolver ON the marker follows interpolated, not smoothed, origin.
- In game check: markers steady while strafing/turning (multicore on); enemy bot head marker
  follows it; teammate aim shows the message.

## State
Works in game per user. Build clean (`cmake --build build --config Release --target dih`), gate test
`routesim_test` 444/444 incl. 1500 random routes identical to an independent brute force.

Files: `src/hacks/movement/routesim.h` (port + solver), `src/hacks/movement/routecalc.inl` (glue, markers),
`src/hacks/menu/menu.cpp` (~2030-2140), `tests/routesim_test.cpp`. Backups: `backups/src_baks/*pre_rc18*`.

What rc18 did:
- Moves only jump / minijump / longjump / jumpbug; start-only walk off / crouch walk off.
  Config key `rcMoveSet` (6 bits). Marker types ground / pixelsurf / edgebug / headbang;
  old files (`rc17`: jumpbug / pixelbounce types, 13 bits) migrate via `rs::ParseMarkerLine`.
- Solver `Fly` = dih's per-cmd pipeline: keys -> AutoJump/AutoJumpbug -> prediction ->
  LongJump/MiniJump/PixelSurf OR IN_DUCK. PixelSurf hunt (standing pass, duck pass, pin-1 press,
  16-tick latch), duck-hold owners (`State::hold` bits 1 player / 2 PixelSurf / 4 MiniJump),
  ride length enumerated 0..17, printed as "ride N ticks" only when it matters (`RideText`).
- Bug fixed in dih itself: `AutoJumpbug` arm distance +20 when ducked (movement.cpp ~900) --
  longjump -> jumpbug was 0/80, now 25/80.
- PixelSurf statics `s_prevSurfVel`, `s_pixelSurfTicks` moved to file scope; `s_prevCmdDuck` added.

Open from critic round 4: cut off by rate limit, never reported. Run one cold critic pass first.

## Next task 1: markers on players / buildings
Code exists but user says it does not work. Leads, check in order:
1. `RcAimPlayerHead` (routecalc.inl ~650) now SKIPS TEAMMATES (teammates are not solid in TF2).
   Testing on a teammate or a same-team bot = no head marker. Confirm team of target first.
2. Buildings: marker comes from the world `TraceLine` + `RcFloorKindOf(trace.m_pEnt)`.
   `TraceFilterWorldProps` (movement.cpp ~80-130) has a team-brush skip: check it does not drop
   own-team buildings for a LINE trace (comment says obj collision group is solid to both teams,
   verify in game via log). Add `DebugLog("RC: marker hit ent %d kind %d", ...)` in `RcAddPoint`.
3. `RcFloorKind` reads class names `CObjectSentrygun/Dispenser/Teleporter` via
   `VCall(g_entityList,0,idx)` then slot 2. Unverified in game -- log the name it returns.
4. Player/building z is captured once at placement. A moving target makes the marker stale.
   Option: store the entity index in `RcPoint` and refresh z (hull top + 1/32) each solve/draw.
   File format would need a 12th field (entity index is per-session only -- don't persist it).

## Next task 2: markers glitchy / jittery
Render: `render.cpp` ~318-390 (`RC_GetDrawData` + `PsW2SMatrix` W2S, `floorf(..)+0.5f` snap).
Leads:
1. Matrix and frame mismatch: `PsW2SMatrix()` may be the matrix from a different frame than the
   one being drawn (render thread vs engine view). Compare with how other world overlays in
   render.cpp (pixel finder points / ps assist) get their matrix -- if they are steady, copy that.
2. Pixel snap `floorf(x)+0.5f` makes sub-pixel motion step: fine when still, steps while moving.
   Try without the snap.
3. Markers drawn from `s_rcPoints` under `g_rcLock` every frame -- cheap, not a cause, but confirm
   no per-frame re-snap of pos (RcAddPoint snaps to finder points once only).
4. Player-head markers will also "jitter" if a live-refresh (task 1.4) is added without
   interpolation -- use the entity's render origin, not the networked origin.

## Verify in game (debug log C:\dih\debug.log)
`RC: solve (dihrc18)`, `RC: engine check` (worst |dz| > 0.01 = port wrong), `RC: marker`.

---

## rc29 (2026-09-21) -- the ring goes on a floor that exists, and texturebugs get suggestions

Built, `helperpt_test` 2071/2071, `routesim_test` 1026/1026, **untested in game.**

Report: *"route calc keeps suggesting random points in the air.. make it suggest points that
actually exist on the map and are reachable and are near the existing route points"* + *"improve
how it handles texturebugs"*.

### 1. The suggestion is searched the other way round

rc28 was not wrong about the HEIGHT -- `rs::FindHelpers` flies every height it prints to the
marker with the solver itself. It was wrong about there being anything AT it. The ring was drawn
at the **midpoint of the dead leg at that height**, so unless the map happened to have a floor at
exactly z 512.347 on exactly that line, it hung in mid air. No amount of work on the height fixes
that, because the height was never the missing half.

So: **trace the map first, ask the solver second.**

* `RcLedgesNear(pLocal, from, to, zTop, span, out, nTraces)` (routecalc.inl). 9 steps along the
  leg x 5 lateral offsets (0, +/-40, +/-80 = `k_rcLedgeLat`), one drop each over the whole band
  under the failing leg's apex. Highest first, one answer per floor.
* The drop uses a **1u foot box**, not the 82u hull: a full hull dropped from the apex is
  `startsolid` whenever the apex is under a ceiling, and the column would be thrown away over a
  ceiling that has nothing to do with where the floor is. The **standing** hull is then tested AT
  the floor (`rs::HelperSolve` presses off a settled standing hull, so a ledge only a ducked hull
  fits on is not the spot the answer would be about).
* Each floor is refused if `MV_PointUnusable` (CONTENTS_SOLID / PointOutsideWorld) or
  `normal.z <= 0.7`.
* `rs::HelperAt(T, p, moves, z, &move)` (routesim.h) asks the solver about each real floor, up to
  `k_rcLedgeTries` = 16. First one that flies wins -- highest first, so it is the short detour.
* Found: `s_rcSuggest` = **the floor's own world position**, ring drawn, line reads *"add a marker
  between X and marker N, on the floor the ring is on (z ...) - off it: <combo>"*.
* Not found: `rs::FindHelpers` still runs as the FALLBACK and its height is still printed --
  *"no floor near this leg works, but ANY surface at z X would"* -- but **no ring is drawn**,
  because nothing proved there is a surface at it anywhere near the route. A ring in mid air is
  the bug being fixed; printing a height you can go look for yourself is not.
* Cost: ~90 traces + <= 16 solves (~0.15 ms; 36 solves is 0.3 ms per `helperpt_test` section 5),
  once, at the end of a solve that already spent ticks.
* `RC: helper ledges` / `RC: helper heights` in debug.log say which path answered.

**Not claimed: pathfinding.** "Reachable" here means a real standable floor, a standing hull
fits, not in solid, not off the map, within 80u of the straight leg. There is no route FROM you
TO the ledge -- the solver itself is z-only and never had one.

### 2. Texturebug markers got zero suggestions, always

`rs::FindHelpers` branched on `SeamLike(T.type)`, which is true for `PT_TEXTUREBUG`, and read
`Result::closest` to snap the launch. **A texturebug's window is the whole hull** (`rs::Arrive`
`PT_TEXTUREBUG` catches on any airborne tick with the seam strictly between the feet and the head
and no stance change), so no tick ever misses it by a measurable distance and `closest` is never
written for one. The read came back `kInf` and the walk broke out on its first failed solve.

Fixed: the branch is `T.type == PT_SURF`. A texturebug reads `Result::highest` like a headbang --
come in LOWER, so the hull is slow where the seam is and a tick straddles it instead of crossing
a hull's worth of it in one. `helperpt_test` section 6 pins the reason (a failed TB solve leaves
`closest == kInf` and `highest > -kInf`) so a change back to `SeamLike()` fails loudly, then
checks the search against the solver over a band where 5 of 10 seams are reachable: 5 found, 5
refused, every height re-flown.

The panel's diagnosis line for a texturebug was *"arcs get there but none arrive the way this
marker needs"*, which named nothing. Now: *"the head gets Xu over it, but no airborne tick had
the seam between your feet and your head (a duck or unduck on that tick cancels it too) - come
past it slower"*.

### 3. Texturebug markers can now be placed anywhere

`RcPickFinder` also reads `s_psMapTbPts` -- the seams the **whole-map scan** finds (see
`HANDOFF_psmap.md`, dih160). Before this, a red ring only existed on a wall column you aimed at
and scanned by hand.

### Files touched

`routesim.h` (`HelperSolve` / `HelperAt` extracted, `FindHelpers` TB branch), `routecalc.inl`
(`RcLedgesNear`, the suggestion block, TB diagnosis, `s_rcLocal`, `RcPickFinder`),
`movement.cpp` (`MV_PointContents` / `MV_PointOutsideWorld` / `MV_PointUnusable` /
`MV_ValidateOutsideWorld`), `tests/helperpt_test.cpp` (sections 6 and 7).
