# Route calc: TF2 (botox-tf2) vs CS:GO (botox) — gap analysis + port plan

Read-only research. No source edited. Compares `botox-tf2`'s route calculator
(`src/hacks/movement/routecalc.inl` + `src/hacks/movement/routesim.h`) against `botox`'s
(`hacks/movement/route_calc.cpp` + `route_engine.cpp`).

## 1. TF2 route calc today

**What it computes.** Given an ordered list of markers (ground / pixelsurf /
edgebug / headbang / texturebug) placed in the world, it enumerates every
combination of replicable button presses (jump, minijump, longjump, jumpbug,
plus start-only walk-off / crouch-walk-off) that gets a z-only simulated
player from marker to marker, and prints every combo that completes the
whole route, e.g. `walk off -> jumpbug -> pixelsurf (stand) -> longjump`.
It is a **vertical-only** tool: horizontal movement, aim, and whether you're
actually over the right floor when you land are the player's job.

**Physics model (`routesim.h`).** A z-only, tick-exact port of
`CTFGameMovement` (source-sdk-2013 `tf_gamemovement.cpp` / base
`gamemovement.cpp`): `StartGravity`/`FinishGravity` (double-precision
literals preserved), `CheckJumpButton` (289 impulse, `jumpMod` for
`mod_jump_height`), `Duck`/`DuckOverrides`/`OnDuck`/`OnUnDuck`/`FinishDuck`/
`FinishUnDuck` (msec timers, ±20u stance teleport in air, `TF_TIME_TO_DUCK`
0.3s duck-release lockout), `CategorizePosition` (2u ground catch, +18 snap
when already grounded, `StayOnGround`), `AirMove` (floor/ceiling clip).
TF2-specific constants it encodes: no stamina (TF2 has none — confirmed,
`grep -ri stamina` across source-sdk-2013 hits only CS:GO-derived SDK
scaffolding, never `tf_gamemovement.cpp`), hull 82/62 (not CS:GO's 72/54),
jump impulse 289 (not CS:GO's), `TF_AIRDUCKED_COUNT` = 2, duck timer msec
rules instead of CS:GO's `m_flDuckAmount`-approach-rate duck.

**Moves** are deliberately restricted to dih's own replicable binds:
`jump`, `minijump`, `longjump`, `jumpbug` (+ start-only `walk off` /
`crouch walk off`). No generic "press jump, duck, or nothing, every tick"
search — the state space is the bind set, not raw buttons.

**Inputs:** ordered markers (xy fixed at placement; z from a raycast, or
from `rce::NearestEdge` for edgebug lips, or from an entity hull top for
buildings/players). Moves-mask per marker (`kMovesMarker` = jump+mini+long+
jumpbug), start-only walk-off bits.

**Outputs:** `s_rcLines` (panel text, one line per combo), `s_rcPath` (the
first printed combo re-flown through `rs::Solver::TraceBest` and drawn as an
arc), `s_rcMessage` (why nothing solved), plus a dead-leg "helper" search
(`rs::FindHelpers` / `RcLedgesNear`) that finds a real floor near a failed
leg and suggests where an extra marker would close the route.

**UI:** markers placed by aiming (face normal decides ground/headbang,
pixel-finder pins/seams decide pixelsurf/texturebug), menu rows can reorder
markers, edgebug/headbang have stance dropdowns, a separate "pixel
calculator" feature reuses the same solver for a single off-floor pixel with
up to `k_pcMaxHops` (3) extra hops on the player's own flat.

**Threading / performance.** `RouteCalc_Tick` runs **on the game thread**,
every tick, synchronously. `RcEngineCheck` cross-validates the port against
`Pred_Simulate` every solve (jump/longjump/minijump/jump+jumpbug from the
current floor), logging the worst `|dz|`. There is **no async worker, no
time budget, no node/memory cap, no OOM guard, no beam/trim, no "approx /
counted" mode** — `routecalc.inl` has no `std::thread`, no progress percent,
no cancel. For a handful of markers with the current move set (4 bits) this
is fine; it has not yet been reported to freeze the game, but it also has
never been pushed to CS:GO's `route_calc`-scale marker counts (10+ markers,
exact-vs-approx listing, OOM).

**Known bugs / history (from `docs/HANDOFF_routecalc.md`):**
- rc19: overlay jitter from a stale W2S matrix (render-thread/main-thread
  frame mismatch) — fixed via a functor-queue snapshot.
- rc20/21: marker-snap false positives (nearest-point-of-any-kind instead
  of angle-to-ring), minijump refused inside the 0.3s duck-release lockout
  on buildings, clip-brush false "pixelsurf on my own face" markers.
- rc24/25: marker TYPE used to be guessed from the crosshair snap; now the
  crosshair/face decides type outright (ground/headbang/wall-refused;
  pixelsurf/texturebug only from finder pins/seams).
- rc27-rc29 (SUGGESTIONS, now removed per rc31 user request): the dead-leg
  "where would a marker have to go" feature went through three redesigns
  (signed near-miss -> launch-height search -> real-floor-first search) and
  was then deleted outright because the user asked for it to be removed.
  **This is the one capability the TF2 side had and then explicitly killed
  — it should not be re-added without the user asking again.**
- All UNTESTED-in-game markers in the handoff are honest: most recent passes
  (rc28-rc31) built and gate-tested but never confirmed live.

## 2. botox (CS:GO) route calc today

**Architecture.** `route_calc.cpp` (UI/menu/marker glue, async job plumbing)
+ `route_engine.cpp` (the solver: `solve_search`, `point_step`, `expand_one`,
`solve_from`, `list_final`) + `tick_scale.h` (64/128-tick scaling) +
`pixel_calc.cpp` (single-pixel variant) + `assist_predict.h`. Roughly 2x the
line count of TF2's version (route_engine.cpp alone is 2892 lines vs TF2's
combined 3205 across routecalc.inl+routesim.h), because it carries several
capabilities TF2 doesn't have yet (below) plus CS:GO-only physics
(stamina).

**Capabilities beyond TF2, from the memory hub (`hub-route-calc.md`) and its
linked notes:**

1. **Async solve on a worker thread** (`proj-rc-async-1006`). Was a 4.6-5.2s
   game-thread freeze at 10 markers / 85k arcs. Moved to a detached
   `std::thread`, polled each game tick (`rc_poll`): generation-mismatch
   cancel, progress 0-999 checked every 64 nodes, popup `calculating (N%)`.
   Engine-thread trace calls that can't run off-thread (`IsEdgeBugTick`'s
   live local trace) are stubbed to a safe default off-thread. Shutdown
   hook cancels + waits up to 3s before unload.

2. **Exact TryPlayerMove-style collision, not a 2-unit-catch shortcut**
   (`proj-rc-tpm-split-1010`). `trace_z` = `IntersectRayWithBoxBrush`, z
   only: hits only on the TRUE face (floor face at `floor_z - 1/32`, roof
   face at `roof + 1/32`), `frac < 0.0001` moves nothing, landing can end
   up *inside the skin* and stay there (matches recorded in-game z values
   like `-155.9808`). This replaced an earlier, slightly-wrong 2-unit-only
   ground catch. CS:GO-generic (box-brush collision is engine-generic, not
   stamina-flavored) — **this is the single most portable, highest-value
   idea**: TF2's `Categorize`/`AirMove` currently use a simplified "does the
   fall cross `floorEff`" test, not a real skin-exact 1D brush trace.

3. **Split solving to avoid OOM, not a hard node cap**
   (`proj-rc-tpm-split-1010`, `proj-rc-nolimit-1009`). When a point's
   frontier would blow the memory budget, the search splits the frontier in
   half and solves each half to completion before merging — rather than
   truncating results. Memory budget is computed from live free virtual
   memory (`heap_ratio`, learned online), not a fixed constant. A **time
   budget** (`k_rc_time_limit_ms`, default 45000) forces splitting once past
   half the budget so whole combos still finish rather than getting cut
   mid-combo; past the full budget it stops and reports partial results
   with a "stopped at 45s: N combos found so far" note.

4. **"Counted" / approximate mode past a result cap**
   (`proj-rc-count-1010`). Past `exact_alts` (100000) results, the engine
   switches from exact enumeration+listing to *counting* combos per arc
   (same-lead-text merge = `max`, not full cross product) and reports
   "about N combos" instead of an exact count — because full enumeration at
   10M+ combos cost 45s just building display strings for 6 visible rows.
   Distinguishes `searched` (space explored) from `found` (solutions), and
   caps per-arc expansion with `k_split`-triggered trimming when more than 2
   points remain (drop the harder half, keep going).

5. **Landing as a state SET, not a single point** (`proj-rc-landing-set-1010`).
   Near an apex, multiple ticks can cross onto the same landing box (the
   rising tick AND the falling tick), and in-game timing picks which one —
   so the old model (first-rising-wins, one node) silently discarded a real
   combo. Now a node carries `others` (shadow states for every tick that
   could land there) and `odds` (fraction of states that actually land there
   given the player's real timing variance), surfaced in the UI as
   `chancy N%` / `(works N% of tries)`. Weighted by `clear_ticks` (ticks the
   feet were clear of the box since the last catch), not uniform.

6. **Per-marker "types to calculate" bit flags, not one global move set**
   (`proj-rc-point-moves-1011`): jump, jump (ducked), delay-hop, minijump,
   longjump, jumpbug — toggled per point, not just per feature. TF2 has a
   single `rs::kMovesMarker` bitmask applied uniformly.

7. **Jumpbug duck-air + last-marker forced-duck landing**
   (`proj-rc-jb-crouch-1007`): jumpbug's air phase can duck even when the
   style isn't `style_jumpbug` outright, and the LAST ground marker will fly
   a ducked arc if the standing arc misses (popup ends `ground (ducked)`) —
   because a shorter ducked hull can clear a box the standing hull can't,
   and a player reading the popup needs the duck visible.

8. **Accuracy/false-positive pass** (`proj-rc-acc-1003`): most false
   positives were not physics error but **press-band modeling** — holding W
   into a wall only catches pixels at `z >= plane + 0.0203` (stand) /
   `+0.0105` (duck), not the whole window; a `pixel_tier` classifies
   good/edge/poor bands and lists by tier. Also: engine-exact duck-spam
   stamina recovery during travel, slope-floor handling (`k_flat_nz` 0.999
   cut, advanced aimed-ground-marker trace that rests ON the aimed plane
   instead of using the raw ray z, which was 3-8u low on real slopes).

9. **64/128-tick scaling** (`tick_scale.h`) — **CS:GO-specific**, not
   portable: CS:GO servers can run 64 or 128 tick and botox has a
   "forced 128" checkbox that rescales timing constants; TF2 servers are
   fixed-tickrate (`interval_per_tick` read once, no in-game toggle) so
   there is nothing to scale.

10. **Stamina simulation** (`sv_staminamax`/`sv_staminajumpcost`/
    `sv_staminalandcost`/`sv_staminarecoveryrate`) — **CS:GO-specific**,
    confirmed absent from TF2's `tf_gamemovement.cpp` and the whole
    source-sdk-2013 tree outside unrelated SDK-base scaffolding
    (`sdk_player_shared.h`). Not portable, not needed.

**Engine-generic vs CS:GO-specific summary:**

| idea | generic or CS:GO-specific |
|---|---|
| async worker thread + poll/cancel | generic |
| exact box-brush 1D trace (TryPlayerMove) | generic |
| split search / memory-budget-driven splitting | generic |
| time budget + partial results | generic |
| counted/approx mode past a cap | generic |
| landing-as-state-set / chancy % | generic (any engine with a ground-catch window wider than 1 tick) |
| per-marker move-type bit flags | generic |
| jumpbug duck-air / forced last-marker duck | generic, but exact arm/release numbers are TF2's own (dih's `AutoJumpbug`/`MiniJump`) |
| W-held press-band tiers | generic concept; TF2's own band constants (`kSurfLo`/`kSurfHi`) differ and are already correct for pixelsurf per `routesim.h`'s own recorded-hop validation |
| stamina | CS:GO only — skip |
| 64/128 tick scaling | CS:GO only — skip (TF2 servers don't expose a tick toggle) |

## 3. Gap table

| botox capability | TF2 has? | portable? | what changes for TF2 physics | priority | effort |
|---|---|---|---|---|---|
| Async worker thread (solve off game thread) | No — `RouteCalc_Tick` runs sync on game thread | Yes | None — pure glue change. `RcPlaceAt`/`RcSimTick` touch live engine state (`EntSetV3`, `Pred_Simulate`) and currently run on the game thread; the actual z-only solve (`rs::Solver`/combo enumeration) is pure and CAN run off-thread once the starting `rs::State`/`rs::World` snapshot is captured on the game thread first | **High** — no current freeze reported, but move-set is about to grow (gap below) and marker counts will too | Medium |
| Exact 1D box-brush trace instead of simplified ground-catch | Partial — `Categorize`/`AirMove` in `routesim.h` already do a real floor/ceiling z-crop, but it's a simplified "crosses floorEff" test, not a genuine skin-exact brush trace with `frac<0.0001` dead zone | Yes, and worth doing | TF2 hull constants (82/62) and `kDistEps` (1/32) already match `routesim.h`'s own documented plane-skin convention; this is a tightening of `AirMove`'s floor/ceil clip to match `CategorizePosition`'s actual 2u-trace+StayOnGround rule bit for bit (it's *mostly* already there — verify against recorded hops) | Medium | Medium |
| Split search / memory-budget splitting | No — no node cap at all currently | Yes | None conceptually; TF2's move set is 4 bits vs CS:GO's larger bit space, so TF2 combos grow slower, but it WILL blow up once gap #4 below (per-marker move bits) lands | **High** (prerequisite for safely growing the move set) | Medium-High |
| Time budget + partial results | No | Yes | None | High | Low (once split search exists) |
| Counted/approx mode past a result cap | No | Yes | None | Medium | Medium (depends on split search) |
| Landing-as-state-set / `chancy N%` | No — TF2's `Categorize` picks one deterministic landing tick | Yes, and TF2-relevant: `kSurfLo`/`kSurfHi` pixelsurf window is already documented as narrow (-0.01..0.03), and marker suggestion history (rc27-29, since removed) proves TF2 cares about near-miss timing sensitivity | TF2's ground-catch window (`Categorize`'s 2u offset) is narrower than CS:GO's but not zero-width; needs its own `clear_ticks`-style weighting derived from TF2's own `StayOnGround` snap rule | **High** — this is the generalized, correctly-modeled version of the suggestion feature the user had removed; worth proposing to the user explicitly rather than silently reviving a close cousin | High |
| Per-marker move-type bit flags (jump/jump-ducked/delay-hop/mini/long/jumpbug per point) | Partial — `RcPoint::moves` exists as a per-marker bitmask already (`kMovesMarker`), but the TF2 UI doesn't expose per-marker toggling for the full set; "delay hop" doesn't exist in TF2 at all | Partially | TF2 has no "delay hop" bind; would need a new dih feature (hold off N ticks before pressing) before the solver can model it — **not a solver-only change** | Medium | Medium (UI) + High (if delay-hop bind is added) |
| Jumpbug duck-air widening / forced last-marker duck | Partial — TF2's `rs::JumpbugCmd`/`MiniHold` already model duck-air for jumpbug; "last marker tries ducked if standing misses" is NOT in TF2 | Yes | TF2's jumpbug arm/release numbers (movement.cpp `AutoJumpbug`) are dih's own, already correct; only the "try ducked landing on the final marker when standing misses" fallback is new | Medium | Low-Medium |
| W-held press-band tiers (good/edge/poor) for pixelsurf catch | No — TF2's pixelsurf window (`kSurfLo`/`kSurfHi`) is treated as one hard window, no tiering by how firmly W predicts the catch | Yes conceptually | TF2's recorded hightower-hop validation already proves the window bounds are physically correct; tiering would be a NEW axis (how reliably a given horizontal approach speed/angle lands in that window), which TF2's z-only solver cannot derive without horizontal modeling it currently refuses to do on principle | Low — conflicts with the documented "horizontal is the player's job" design stance; raise with user before building | Medium-High |
| Stamina | N/A — TF2 has none | No | N/A | Skip | N/A |
| 64/128 tick scaling | N/A — TF2 has no tick toggle | No | N/A | Skip | N/A |
| Dead-leg "suggestion" marker | Had it, user asked it removed (rc31) | N/A | N/A | **Do not re-add without asking** | N/A |

## 4. Ranked implementation plan

Ranked by (priority, effort) from the gap table, each with concrete
functions/files and test cases.

### P1 — Split search + time/memory budget (prerequisite for everything else)

**Why first:** every other high-value gap (async, chancy%, bigger move
sets) either needs this or becomes dangerous without it. Right now TF2's
solver has **zero** node/memory ceiling; it has gotten away with it only
because the move set is small (4 bits) and marker counts have stayed low.

- `src/hacks/movement/routesim.h`: add `rs::Solver` frontier split, mirroring
  `route_engine.cpp`'s `point_step`/`solve_from`/`k_split` pattern:
  - New `rs::SolveSpan` struct (lo/hi bounds, like CS:GO's `span_t`).
  - `rs::Solver::PointStep(frontier, pointIdx, span) -> {kNext, kDead, kSplit}`
    returns `kSplit` when `frontier.size() * sizeof(Node)` crosses a budget
    computed from live free memory (reuse dih's existing memory-query
    helper if one exists, else `GlobalMemoryStatusEx`).
  - `rs::Solver::SolveFrom(frontier, p0, span)` recursive halving, same
    shape as `route_engine.cpp`'s `solve_from`.
- `src/hacks/movement/routecalc.inl`: add `k_rc_time_limit_ms` (reuse CS:GO's
  45000 default), check elapsed time every N expansions in the solve loop,
  stop and report partial results with `s_rcMessage = "stopped at Ns: N
  combos found so far"`.
- **Tests** (`tests/routesim_test.cpp`, new section): construct a synthetic
  route with an inflated move mask (temporarily OR extra placeholder bits
  to force combo blowup) and assert (a) split path produces byte-identical
  results to the unsplit path on a route that doesn't need splitting, (b) a
  deliberately huge route (e.g. 15 markers, full `kMovesMarker` each)
  completes without OOM and reports a partial/approx result rather than
  crashing or hanging past the time budget, (c) time budget triggers at the
  configured threshold (inject a fake clock or a budget of 1ms in the test
  to force it deterministically).

### P2 — Async worker thread

- `src/hacks/movement/routecalc.inl`: split `RouteCalc_Tick`'s solve call into
  `RcJob` (mirrors CS:GO's `rc_job_t`): snapshot `rs::State`/`rs::World`/
  marker list/params on the game thread (this is the ONLY part that touches
  live engine memory — `RcPlaceAt`, `RcReadState`, `RcEngineCheck`'s
  `Pred_Simulate` calls must stay on the game thread, run once per marker
  before the worker starts, their results captured into plain data).
  `RcStart` launches a detached `std::thread` running `RcWork` (pure
  `rs::Solver` over the snapshot, no engine calls). `RcPoll` runs from
  `RouteCalc_Tick` each tick: generation mismatch -> cancel + restart,
  done -> `RcFinish` (existing panel/log fill-in code, unchanged).
  Progress counter checked every 64 nodes like CS:GO's `rc_work`.
- Add `RouteCalc_Shutdown()` called from the existing unload path: cancel,
  join with a timeout (3s), matching CS:GO's shutdown discipline.
- **Risk-specific test**: `tests/routesim_test.cpp` or a new
  `tests/rcasync_test.cpp` — run the SAME route through the solver inline
  and through a real `std::thread` wrapper (the engine-free parts only,
  no engine mocking needed since the worker never touches engine state)
  and assert byte-identical output lists and ordering.

### P3 — Landing-as-state-set / `chancy N%`

This is the technically-correct generalization of the suggestion feature
the user had removed (rc31) — **raise with the user before building**,
since it's adjacent to something they explicitly killed, even though it
answers a different question (a working route's reliability, not "add a
marker here").

- `src/hacks/movement/routesim.h`: `rs::Node` gains `others` (shadow states for
  every tick within the catch window that lands differently) and `odds`
  (fraction of plausible real-timing states that land on this node's lead
  state), mirroring `shadow_t`/`node_t.odds`/`node_t.others` in
  `route_engine.cpp`. Needs a `rs::ClearTicks` concept — ticks since the
  hull last cleared the landing plane — as the weighting basis (CS:GO's
  `clear_ticks`), computed from TF2's own `Categorize`/`StayOnGround` 2u
  snap, which is already tick-exact in `routesim.h`.
- `src/hacks/movement/routecalc.inl`: panel line suffix `(chancy N%)` /
  `(works N% of tries)` when `odds < 1.f`.
- **Tests**: `tests/routesim_test.cpp` new section — a marker placed exactly
  at a known multi-tick-catch apex (derive from the existing hightower hop
  ladder data already validated in the file: `-255.26`/`-254.56` jump,
  `-255.25`/`-254.53` longjump) should report `odds < 1.0`; a marker clearly
  inside a single tick's catch window only should report `odds == 1.0`
  (safe). Cross-check against a brute-force per-tick scan (same pattern as
  the existing `helperpt_test.cpp` 0.01u brute-force oracle).

### P4 — Jumpbug duck-air widening + forced last-marker ducked landing

- `src/hacks/movement/routesim.h`: in the last-marker arrival path of
  `rs::Solver` (wherever ground-marker arrival is finalized), add a
  fallback: if the standing arc misses the last ground marker, re-fly with
  `s.ducked = true` from the point the duck would have to start, matching
  CS:GO's `duck_air_here` last-marker rule. Mark the printed combo
  `ground (ducked)` only in this fallback case (never silently).
- **Tests**: `tests/routesim_test.cpp` — reuse/extend `jbpredict_test.cpp`'s
  existing jumpbug cases with one where the standing hull fails a box the
  ducked hull clears; assert the combo prints with the `(ducked)` suffix
  and that it's absent whenever the plain arc already succeeds (no silent
  crouch when not required, matching CS:GO's own stated reasoning in
  `proj-rc-jb-crouch-1007`: "crouch must be visible, never silent").

### P5 — Per-marker move-type bit flags in the UI

- `src/hacks/menu/menu.cpp`: expose `RcPoint::moves` per-marker (it already
  exists as a field) as individual checkboxes instead of only the global
  default, matching CS:GO's point-list UI (`k_pt_duck_jump` equivalent
  flags). This is UI-only; the solver already honors `RcPoint::moves`.
- Skip "delay hop" — TF2 has no such bind. If the user wants it, that's a
  new dih feature (`movement.cpp`) before the solver can model it; flag as
  a separate, larger task, not bundled into this port.

### Explicitly out of scope / do not port

- Stamina — TF2 has none.
- 64/128 tick scaling — TF2 servers don't expose a tick toggle to the
  client; nothing to scale.
- The dead-leg "suggestion" marker (rc27-29) — user asked for its removal
  in rc31. The chancy% feature (P3) answers a related but distinct
  question (how safe is a working route) and should be pitched separately,
  not folded in as a disguised revival.
- W-held press-band tiering (good/edge/poor) for pixelsurf — requires
  modeling horizontal approach, which conflicts with the TF2 solver's
  documented design stance ("horizontal is the player's job"). Raise with
  user first if wanted.

## 5. Risks

- **Threading vs game-thread-only calls.** `RcPlaceAt`, `RcReadState`
  (`MV_ReadSimState`), and `RcEngineCheck`'s `Pred_Simulate` calls all touch
  live engine entity memory and must stay on the game thread. The async
  port (P2) must snapshot everything the pure solver needs into plain data
  structures (`rs::State`, `rs::World`, `rs::Params`) BEFORE spawning the
  worker — the existing code already mostly does this (`rs::State`/
  `rs::World` are already plain structs), so the main risk is a future
  change accidentally reaching back into `pLocal`/`EntSetV3`/etc from
  inside what's supposed to be the pure solver path. Guard with a comment
  + a test that the pure `rs::Solver` namespace never includes engine
  headers (mirrors how `routesim.h`/`rc_edge.h` are already documented as
  "pure, no engine").
- **x64 differences vs CS:GO's x86 solver.** `proj-rc-nolimit-1009` notes
  CS:GO's memory layout work (deque not vector, "no 190MB contiguous block
  in x86") was x86-address-space-driven. TF2's botox-tf2 target — verify
  which arch it actually builds for (CLAUDE.md says "TF2 movement, x64" in
  the task header) — if x64, the contiguous-allocation pressure CS:GO
  fought is much less severe (far more virtual address space), so the
  split-search memory budget (P1) can likely use a simpler/larger threshold
  than CS:GO's; don't blindly copy CS:GO's `heap_ratio`/512MB-floor-clamp
  constants without re-deriving them for x64 address space.
- **Memory-budget heuristics are a moving target.** CS:GO's `heap_ratio` is
  described as "learned, x1.25 on new heap growth" and was tuned/re-tuned
  multiple times (10-09 fix for a false-OOM floor). Expect the same
  iteration cost porting to TF2 — ship a conservative fixed floor first,
  only add adaptive scaling if real in-game OOM reports show the fixed
  floor is wrong (per the user's "no speculative code" rule: don't invent
  the adaptive heuristic before a measured need exists).
- **Chancy%/landing-set correctness is easy to get subtly wrong.**
  CS:GO's own history shows two iterations (rise-tick-only missed real
  catches; uniform-tick-weight was "wrong," per `proj-rc-landing-set-1010`'s
  10-10-eve note) before the `clear_ticks` weighting matched in-game
  truth. Treat P3 as genuinely large effort (full fan-out protocol per
  CLAUDE.md task sizing, not a quick add) and validate against TF2's own
  recorded hop data (the hightower ladder already in `routesim_test.cpp`)
  before trusting any printed percentage.
- **Vtable/offset risk is low for this task.** Nothing here adds a new
  MinHook detour or reads a new netvar/vtable slot — all work is in the
  pure `rs::`/engine-free layer plus glue already wired (`RcSimTick`,
  `RcPlaceAt`). The one exception is P4's "forced last-marker duck
  fallback," which only re-flies the EXISTING `rs::Tick` state machine with
  a different `Cmd.duck` input — no new engine surface touched.

## Report path

`C:\Users\huber\Desktop\botox-tf2\docs\port\routecalc_plan.md`
