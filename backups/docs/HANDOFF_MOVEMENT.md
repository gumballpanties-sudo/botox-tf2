# HANDOFF — movement

## 2026-09-21 — edge bug: the along-path knob, the re-asked plan; auto align off props

**All UNVERIFIED IN GAME.** Builds clean (no warnings), gate suites green:
`ebstance_test` 211/211, `routesim_test` 1022/1022.

### edge bug — catch more
- **Brake shapes (mode 5, `EbPhaseMove`)** — the search could move the fall ACROSS its path (the
  weave, the lane steer) and the hull UP/DOWN it (the ±20 duck press/release). Nothing moved it
  ALONG the path, and that is the half an edgebug is decided by: how far past the lip the hull is
  on the tick it crosses. The duck knob cannot buy it — FinishDuck teleports the feet +20 and the
  fall carries on from there, so every press at or before the crossing puts the *same* +20 on it
  (one z offset and ~1.5 ticks of delay, not a continuous timing). Brake = the only continuous
  knob: 15 u/s a tick loses ~2.8u of travel over five ticks = half a tick of phase at 300 u/s.
  Three fractions of a back-wish (0.25 / 0.5 / 1.0, gentlest first), ducked and
  standing = 6 attempts; pass 1 sweeps duck timing over them like any other shape, so the search
  now covers phase × height instead of height alone. Menu *edge bug → hold back to time it*
  (default on), cfg `edgeBugBrake`. Latches as exec mode 3 (wish re-expressed in the live view), so
  a brake replay never touches the player's aim.
- **The press window now includes the lip tick** (`MV_EbPressWindow` in `eb_stance.h`, truth table
  in `tests/ebstance_test.cpp`). It ended at centre-1, and the centre is only an estimate (marched
  ray, contact tick, or chord) — a lip one tick later than the estimate had every press land
  strictly before it, so the +20 never reached the crossing. One extra run per attempt, and the
  window can only grow.
- **Standable displacements are edgebuggable again** (`EdgeBugSurfaceUsable`). It refused every
  disp outright = every terrain ledge on every outdoor map. `CategorizePosition` only asks
  `normal.z >= 0.7`, so a flat-enough terrain face resets gravity like a brush floor; the steep
  disp (the slide that fakes the scrape band) is still refused, and now counts in `rsteep=`.
- **Search effort slider** (menu *search effort*, cfg `edgeBugEffort`, 10-60%, default 30 = the old
  constant). The clock, not the geometry, is what ends most searches — coverage is bought in clock.

### edge bug — prediction quality
- **The latched plan is re-asked, not assumed** (`EbReplayHolds`). The replay only checked that the
  live origin was within 4u of the planned one; inside that the arc can cross the lip a tick early
  and every remaining cmd is flown for a bug that will not happen. Now, when the drift is over
  1/16 u, the rest of the plan is re-flown from the live state with the recorded cmds and judged by
  `EdgeBugCheck` — no hit, drop the lock and search this same tick. Costs (lock − current) sims,
  only on a tick that really drifted, capped at 48 ticks of plan. Logs `EB: replan`.

### auto align
- **Props are refused** (`MV_TraceIsProp` in `AutoAlign`). Every other part of pixel surf already
  refuses them: a prop's collision is a .phy convex hull at whatever angle it was dropped, so the
  into-wall press meets a hull CORNER and wedges. `MV_IsHullFlatWall` could not catch it — it reads
  the returned plane, and a square-dropped crate answers a perfectly axis-aligned normal.
  AutoAlign is the one that keeps pressing (0-100 u/s down the normal, 10 u/s fallback), so it is
  the one that turned the wedge into a stick. New `prop=` counter in the `AA:` diag line.

## 2026-09-20 — jumpbug look-ahead, edge bug visualiser + cadence, ps assist on uneven ground

**All UNVERIFIED IN GAME.** Builds clean, 9 gate suites green (1525 checks).

### jumpbug: predict the hit, and don't eat the bhop on a miss
- `rs::JumpbugWillHit` (routesim.h) replays the bind's own policy against the engine port. No band
  tick before the floor → `AutoJumpbug` returns without touching the cmd, so a missed jumpbug still
  bunnyhops. Menu: *jumpbug → predict the hit* (default on), cfg `jbPredict`.
- Measured over a 4..400u drop × 0..-900 u/s sweep (`tests/jbpredict_test.cpp`): **31546 of 39620
  falls never reach the band**. Those are the ones the old arm crouched through for nothing.
- `AutoJumpbug` no longer carries its own copy of the rule — it calls `rs::JumpbugCmd` and writes
  out `Cmd{duck, jump}`. Same function the route calculator solves with, so they cannot drift.
- One hull trace per armed tick instead of two: the arm trace's `endpos.z` **is** routesim's
  `floorEff`, and every distance the policy wants is arithmetic off it.
- Test: `cmake --build build --config Release --target jbpredict_test && build\Release\jbpredict_test.exe`.
  It checks the look-ahead against a hand-written replay of the bind (not against `JumpbugCmd`), so
  the two drifting apart is a test failure.

### edge bug
- **Visualiser** (menu: *edge bug → visualize*, cfg `edgeBugViz` / `edgeBugVizSize`): the latched
  plan's own sim path plus a flat pad on the bug tick. Latched from `s_ebCmds[].origin`, which the
  search already paid for — no sims, no traces. The line is drawn from `s_ebCurrentTick`, so the
  replay eats it and the leftover is the countdown. `EB_GetDrawData` → `RenderEdgeBugOverlay`.
- **Search cadence**: beyond 24 ticks from contact the full search runs 1 tick in 3, and gets the
  whole clock share instead of the old 0.75 discount. Inside 24 ticks nothing changed. Worst case a
  far plan lands 2 ticks later, against a contact still 20+ ticks out.

### ps assist on uneven ground
- **ground help** (default off): grounded with a target in range, your own move is rotated toward it
  via `CorrectMovement`, capped by *steer limit* (default 45°). No key held = no-op; the view never
  moves. Fixes the half of the problem that is "the sim flies a Z-ONLY arc, so you have to be
  standing under the seam" — on stairs and slopes you are not.
- **launch z slack** (default 4u = half `k_psCatchZ`): a queued launch waits for real ground contact,
  which on stairs can be a different floor from the one the sim proved. Drift over the slack drops
  the plan and re-plans. 0 = old behaviour. Log: `PS: pending dropped=`.

### performance / structure
- `edgebug.inl` split out of `movement.cpp` (5765 → 3995 lines). Pure text move into the same
  translation unit at the same position — no semantic change.
- The ~10 KB build-changelog `DebugLog` string deleted (it shipped in the DLL's .rdata and printed
  on the first cmd of every session). One line now.
- `CvarFloat` cache 8 → 16 entries. The movement half asks for **ten** distinct names, so two of
  them were permanently on the "cache full" path = an ICvar string lookup every call, every cmd.
- The four 16-ray probe rings in `PixelSurf` / `MV_PinOnProp` are integer-indexed now. A float
  accumulator decides its own ray count by rounding: 17 rays with direction 0 twice, or 15 with a
  blind sector. Neither was anyone's choice.
- `MV_ReadSimState` / `MV_SimParams` are one reader shared by AutoJumpbug and the route calculator
  (`RcReadState` is now a one-line forward).
- ~450 dated `dihNNN:` archaeology tags stripped from comments across the movement files.

### menus
Movement tab was one flat list of 18 features under a single header. Five sections now:
**jumping** / **strafing** / **bugs** / **pixel surf points** / **calculators**.

### NOT touched, deliberately
Pixel surf and texture bug physics. Both are heavily tuned against logs this session did not have
(`C:\dih\debug.log` was empty), and changing what they see or when they steer without that evidence
is more likely to break them than improve them. What would settle it: one session with
`debug log` ticked — `PS:` pick/none lines for the surf, `TB S:` / `TB REAL:` for the texturebug.

---

# HANDOFF — movement port (2026-07-04, wave 3)

Core Aletherium movement now in SimpleVisual. This file = map for porting MORE features next session.

## Done wave 3.5 (2026-07-04) — per-point ENTER window + Atomizer dash

- **Per-point ENTER window** (Aletherium DrawAssistPointMenu, trimmed): aim at saved assist point (crosshair-nearest ≤16px, `s_psPointHover` set in RenderPsOverlay), press ENTER → window opens, view freezes (h_SetCursorPos eats recenter, WndProc routes input to imgui like menu). 7 "allowed jumps" checkboxes + delete button. ENTER/ESC close, BACKSPACE deletes, 250ms open debounce, autorepeat filtered (lParam bit 30). Skipped vs Aletherium: per-point active toggle + radius slider.
- **Point struct**: `s_psAssistPts` now `PsAssistPt{pos, typeMask}`. Mask bits (PsTypeBit, Aletherium readout order): 0 mini, 1 regular, 2 mini+duck, 3 crouch, 4 long, 5 long+duck, 6 double jump. File format `x y z mask`; old 3-field lines load as 127. `s_psGen` bumped on load/add/erase → open window auto-closes on list change.
- **Render↔movement API** (movement.h, lock inside): `PS_PointsGen / PS_PointGet / PS_PointSetMask / PS_PointDelete`. Mask edits persist per toggle.
- **Picker gates**: grounded 6-sim loop skips disallowed types (default JumpCatchResult_t = launchDelay −1 → all tiers reject); airdash block gated on bit 6.
- **Atomizer triple jump**: new netvar `g_offItemDefIndex` (m_iItemDefinitionIndex via `FindNetworkTable("DT_EconEntity")`). Dash budget = active weapon (m_hActiveWeapon low 16 bits → GetClientEntity) def index 450 → `m_iAirDash < 2`, else `< 1`. Soda Popper hype (5) NOT mirrored; engine 0.7s-after-deploy block needs no mirror (refused dash never rises in sim).

Build wave 3.5: 0 errors 0 warnings — NOT yet tested in-game.

## Done wave 3 (2026-07-04)

| Feature | Source (Aletherium Misc.cpp) | Notes |
|---|---|---|
| ps finder | PixelFinder | bind (def mode 1 hold) + scan margin slider. Hold = stretch column on aimed wall, release = brute-force (600 heights × ±16u band × stand/duck, DNA sub-unit offsets +0.0287018/−0.972092, embed −0.022 brush / +0.001 disp). Pins base frame via PredCommandsPredicted (NOT bare restore like Aletherium — drift-safe) |
| ps assist | PixelSurfAssist + SimJumpCatch + GridCheck | bind + set point / delete point keys (BindKeyNoMode, edge press) + radius/snap sliders + steer lock / show points / jump popup bools. 6-candidate SimJumpCatch picker (grid → catch → graze → clears tiers), pending mid-air launch, post-launch duck choreography, world-space steer lock, scout double jump SIMPLIFIED (class 1 + m_iAirDash<1, no Atomizer/hype). Points persist `C:\dih\points\<map>.txt` "x y z" per line |
| draw overlay | DrawPixelSurf | render.cpp RenderPsOverlay: W2S = IVEngineClient slot **36** WorldToScreenMatrix (counted vs anchors Time=14/SetViewAngles=20), GetLevelName = slot **51**. ImGui background drawlist, distance fade radius+200/100, crosshair-nearest highlight, finder scan line, jump box popup at 62% H (GetTickCount64 timing, 3s + 0.4s fade). Data via PS_GetDrawData snapshot under std::mutex (movement thread ↔ render thread) |

Skipped vs Aletherium: SimJumpReach (debug-only), PixelSurfLine, per-point ENTER menu (all points active / global radius / all types), draw-side hover delete (delete key removes aimed point via movement-thread trace), jump box Y slider.

Wave 3 wiring:
- New netvars: g_offViewOffset (m_vecViewOffset[0], eye pos), g_offAirDash (m_iAirDash).
- s_psRidingNow = 2-tick sustained ride flag (m_bPixelSurfingNow equivalent), set in PixelSurf top, gates assist hop-finished/pending sanity.
- PS_AssistTick runs post-pred ALWAYS when probesOK (bookkeeping/set/delete/choreography un-predicted; pickers only when s_predicted). PixelFinder runs last in predicted block. needPred += psAssist On() OR finder held/just-released (s_psFinderHeld keeps release tick predicted).
- **Strafe optimizer + auto align = plain bools now, NO bind** (g_strafeOptKey/Mode + g_autoAlignKey/Mode deleted from globals/config/menu).
- **EdgeJumpOn()** = edge jump On() OR (g_miniJumpEdge && mini On()) OR (g_longJumpEdge && long On()) — "edgejump on mj/lj" sub-checkboxes, edge jump feature itself may stay off.
- Menu: movement = ENTIRE col3; col2 = viewmodel/weather/fog/skybox/removals/killfeed/bypass. Bind mode popup cutoff fixed via SetNextWindowSize(90,0) before BeginPopup. BindKeyNoMode = key-only widget (set/delete point).
- Watermark: background box removed (plain text, g_wmBgColor menu row dropped, global kept for cfg compat), margin 5→2px, no padding.

Build wave 3: 0 errors 0 warnings, dih.dll 1.21 MB — NOT yet tested in-game.

## Done wave 2 (2026-07-04)

| Feature | Source (Aletherium Misc.cpp) | Notes |
|---|---|---|
| edge bug | EdgeBugPre/Check/Post + AutoStrafeEdgeBug | bind + scan ticks/adv search/angle limit/scan radius/search autostrafe. Runs AFTER Pred_End w/ own TimeGuard (tickbase window). EB_SIM_CAP 512, open-air broad-phase gate trace, drift abort >16u. SKIPPED: sound/chat/visualize/mouse lock (ApplyMouse hook not ported) |
| pixel surf | PixelSurf | bind. flat-upright-wall + incline reject gates, stand/duck 8-tick probe, indefinite stick 10u hold press. Ride indicator state kept but unused (no HUD) |
| auto align | AutoAlign + TB_SteeringAwayFromWall | bind. tangent-blend hold search (2 pass × 10 mults), pinned frame restore, steer-away release |
| texture bug | TextureBug (ridobebra antoha_bug 1:1) | bind + auto crouch / catch eps / reach / scan step sliders. per-Z line-trace detect_wall, pidoras sweep 10 muls × 5 yaws × duck dual-pass, duck-extension state machine, fix_air_stuck_move. SKIPPED: choke ticks (needs sendpacket ptr — see below) |
| head surf | HeadSurf | bind. +halfgrav pin under ceiling, locked zero-sim replay, cached config fast path, cold-skip 4, HS_SIM_BUDGET 80 |
| wall climb | WallClimb | bind. TB wall cache shared scan, CorrectMovement silent into-wall, WC_SIM_BUDGET 24 |
| air stuck | AirStuck (AirStuckCore, bSlantedOnly=false) | bind + catch eps / reach sliders (flushTol 56 / driftGain 18 / simBudget 64 = consts). drive-in drift, near-tangent sweep 88..89.9 × 5 mags, 2-tick confirm |

New infra wave 2 (movement.cpp):
- **RestoreToFrame / RestoreEntityToPredicted** — engine CPrediction::RestoreEntityToPredictedFrame, sig `40 55 48 83 EC ? 8B EA` (client.dll, direct fn: this, frame). `m_nCommandsPredicted` = CPrediction+20. Bare restore drifts per Simulate → probe loops pin `nBase = PredCommandsPredicted()` and restore `nBase-1`.
- **SetAbsOrigin** — sig `48 89 5C 24 ? 57 48 83 EC ? 48 8B FA 48 8B D9 E8 ? ? ? ? F3 0F 10 83` (client.dll, direct fn: ent, const Vec3&). apply_predicted_state = SetAbsOrigin + write m_vecOrigin + m_vecVelocity.
- **g_frameReady** = g_predReady + both sigs; `probesOK` in RunImpl gates all wave-2 features.
- **TraceLine** (no extents) + **TraceFilterWorldBrush** (WorldProps + reject FSOLID_TRIGGER|NOT_SOLID via new netvar `g_offSolidFlags` = m_usSolidFlags; studio-prop class reject NOT ported — props also hit, minor diff vs CTraceFilterWorldAndBrushOnly).
- **TB helpers**: TB_DetectWall (16-dir hull sweep + cached-angle fast path), TB_DetectCeiling, TB_WallSupportDistance, TB_SteeringAwayFromWall, TB_HalfGravityPerTick, IsTargetVz (= vz within eps of −sv_gravity×interval×0.5 ≈ −6.0), per-tick wall cache (reset in RunImpl).
- SURF_TRIGGER=0x40 surface flag, DISPSURF_SURFACE=1 dispFlags, MOVETYPE_FLY=3, MASK_ALL.
- Vec3 got Dot/DistTo/Normalize (movement.h).
- s_prePredVel captured next to s_prePredFlags (pixel surf ride detect keys off pre-pred velocity).
- Hit flags: s_textureBugHit → HeadSurf yields; +s_headSurfHit → WallClimb/AirStuck yield.

Pipeline now: RunPre = EdgeBugPre → AutoJump → MiniJumpPre → EdgeJump(pre) → AutoJumpbug → AutoStrafe → StrafeOpt. Pred_Start (needPred = any of edge/long/mini OR probesOK+any wave-2). RunPost = EdgeJump(post) → FastMovement → LongJump → MiniJump → [PixelSurf → AutoAlign → TextureBug → HeadSurf → WallClimb → AirStuck] (gated s_predicted && probesOK). Pred_End. Then **EdgeBugPost** (after End, Aletherium-exact).

Still not ported (next session candidates):
1. **Pixel surf assist family** — PixelFinder / PixelSurfLine / SimJumpReach / SimJumpCatch / PixelSurfAssist / DrawPixelSurf (~1500+ lines). Blocker: needs world-draw infra (WorldToScreen + overlay draw list) + per-map points file I/O + set/delete-point binds. SimpleVisual has no W2S yet.
2. **Edge bug mouse lock** — needs CTFInput_ApplyMouse hook, sig `48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 0F 29 74 24 ? 49 8B E8` (client.dll), lock logic trivial (Misc.cpp 1481-1531).
3. **Texture bug choke ticks** — needs sendpacket bool: Aletherium reads `_AddressOfReturnAddress()+0x20` inside CHLClient_CreateMove hook, consumes AFTER PacketManip (overchoke guard <14). ConsumeTextureBugChoke = Misc.cpp 3266.

Build wave 2: 0 errors 0 warnings, dih.dll 1.18 MB.

## Done wave 1 (2026-07-03)

| Feature | Source (Aletherium Misc.cpp) | Notes |
|---|---|---|
| bunnyhop | AutoJump | plain bool, NO bind (user pick) |
| jumpbug | AutoJumpbug | TraceHull 20-22u band, arm trace, auto-crouch |
| edge jump | EdgeJump pre/post | needs engine prediction — auto-skips if sigs fail |
| auto strafe | AutoStrafe | steer-to-look, CorrectMovement silent yaw |
| strafe optimizer | StrafeOptimizer | + gain/min speed sliders, visible view snap |
| fast stop / fast accelerate | FastMovement | psilent yaw+roll270 via cmd-only edit; duck-speed branch removed (user). NOW plain bools, NO bind (user pick) |
| long jump | LongJump | bind. post-pred: grounded pre + airborne post -> inject jump + 2-tick duck window. assist gate (JT_NONE) dropped (assist not ported) |
| mini jump | MiniJump + MiniJumpPre | bind + sub-bools HoldDuck / Queue. same pre/post-ground takeoff detection as edge jump. queue = park/replay presses (MiniJumpPre in RunPre). m_bDucking term DROPPED (not networked) -> uses FL_DUCKING + m_flDuckJumpTime |

Binds = delusional style: `BindKey()` widget in menu.cpp — left click = capture key (ESC clears), right click = popup: always on(0) / on hotkey(1) / toggle(2). Runtime = `MovementKeyOK(key, mode)` (GetAsyncKeyState hold, GetKeyState&1 toggle, foreground-window gate). Per feature globals: `g_X`, `g_XKey`, `g_XMode` — config-persisted.

## Files

| File | What |
|---|---|
| `src/hacks/movement/movement.h` | Vec3 + math (AngleVectors/VectorAngles/NormalizeAngle), UserCmd_t (72B, viewangles Vec3 @16), decls |
| `src/hacks/movement/movement.cpp` | EVERYTHING now: trace structs, filter, EnginePrediction port, MD5, features, Movement_Run pipeline, + CreateMove(slot21)/GetUserCmd(slot8)/ValidateUserCmd hooks + CInput sig (bhop.cpp FOLDED IN, user pick — file deleted, removed from CMakeLists) |
| ~~`src/features/bhop.cpp`~~ | GONE — merged into movement.cpp. Hook decls now in movement.h. thread.cpp includes movement.h |
| `src/hooks/netvars.cpp` | `Movement_ResolveExtraNetvars()` — movement offsets |
| `src/hacks/menu/menu.cpp` | `BindKey()` widget + movement section (col2) |

## Infra now available (reuse for next ports!)

- **Engine prediction** (Aletherium CEnginePrediction port, movement.cpp):
  `Pred_Start(pLocal, pCmd)` → backup via CPredictionCopy + Simulate (ProcessMovement) → entity = PREDICTED state → read → `Pred_End()` restores. `Pred_Simulate()` callable repeatedly for probe loops (EdgeBug-style search). AdjustPlayers/batching NOT ported (skipped — only matters standing on players).
- **TraceHull(start, end, mins, maxs, mask, &filter, &tr)** + `TraceFilterWorldProps` (hand-rolled 2-slot vtable; hits world/static props/misc ents, rejects players via entity-list compare).
- **MD5_PseudoRandom** (single-block inline) for cmd seed.
- **CvarFloat(name, def)** — cached ConVar float read (+0x38 parent, +0x54 value).
- **EngineSetViewAngles(Vec3&)** — slot 20.
- **FixMovement / CorrectMovement** — silent-angle movement re-express.
- Entity reads: `EntF/EntI/EntB/EntV3(pLocal, g_offX)`, `EntGround/EntDucking/EntMoveType/EntSwimming/EntCondBlocked/EntAlive`.

## Slots / sigs / offsets (all Aletherium/Amalgam-verified x64)

- CPrediction (VClientPrediction001): SetLocalViewAngles=13, SetupMove=18, FinishMove=19; byte offs m_bInPrediction=12, m_bFirstTimePredicted=13, m_bEnginePaused=15. **WARNING: header decl order ≠ vtable order** (base IPrediction slots first) — see Aletherium Prediction.h comment.
- IGameMovement (GameMovement001): ProcessMovement=1
- IMoveHelper: sig `48 8B 0D ? ? ? ? 48 8B 01 FF 50 ? 0F B7 D7` (client.dll, RelToAbs+deref1), SetHost=12
- CGlobalVarsBase: sig `48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 8B CA` (engine.dll, RelToAbs, no deref); layout realtime/framecount/absframetime/curtime/frametime/maxClients/tickcount/interval_per_tick
- RandomSeed: sig `0F B6 1D ? ? ? ? 89 9D` (client.dll, RelToAbs) → int*
- CPredictionCopy::TransferData: sig `48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC ? 8B 3D` (client.dll, direct fn). Struct layout in movement.cpp must NOT change.
- IEngineTrace (EngineTraceClient003, engine.dll): TraceRay=4
- CBaseEntity::GetPredDescMap = virtual 15
- IVEngineClient::SetViewAngles = 20
- Netvars: m_pCurrentCommand = off(m_hConstraintEntity) − 8; m_MoveType = off(m_nWaterLevel) − 4; rest in netvars.cpp `Movement_ResolveExtraNetvars` (origin/velocity/mins/maxs/maxspeed/tickbase/waterlevel/modelscale/cond)
- EHANDLE: entry = low 16 bits, serial = >>16; static prop serial == (1<<15)
- MASK_PLAYERSOLID = 0x201400B

## Pipeline order (Movement_RunImpl — keep when inserting new features)

Aletherium order: RunPre = rawJumpHeld capture → [EdgeBugPre] → AutoJump → [MiniJumpPre] → EdgeJump(pre) → AutoJumpbug → AutoStrafe → StrafeOptimizer. Then prediction Start. RunPost = EdgeJump(post) → FastMovement → [LongJump → MiniJump → PixelSurf → AutoAlign → TextureBug → HeadSurf → WallClimb → AirStuck]. Then Pred_End. `[...]` = not ported yet. (AutoPeek/MovementLock/BreakJump sat in these spots in Aletherium — excluded, do not re-add.)

Gates (both pre+post): alive, MOVETYPE_WALK, !swimming (waterlevel≥2), !cond bits 7(taunt)/17(charge). Kart cond (82, Ex2) NOT checked — needs m_nPlayerCondEx2 if ever wanted.

## Next candidates (rough effort)

1. ~~Long jump / mini jump~~ — DONE wave 1.
2. ~~Edge bug~~ — DONE wave 2 (extras skipped: sound/chat/visualize/mouse lock).
3. ~~Texture bug / head surf / wall climb / air stuck / pixel surf / auto align~~ — DONE wave 2 (TB choke skipped).
4. **Pixel surf assist family** — see "Still not ported" above; blocker = W2S/draw infra.
5. EXCLUDED by user — do NOT port: jump stats, movement recorder, fake POV, duck speed, movement lock, break jump, autopeek, no push, auto ctap, auto rocket jump.

Note: `FixMovement()` kept in movement.cpp though currently unused (movement lock removed) — needed by future silent-angle ports.

## Gotchas learned

- `VCall<void>` does NOT compile (common.h template returns Ret{}) — use raw fn-ptr casts for void vcalls.
- __try functions must stay dtor-free (C2712) — SEH lives in Movement_Run/leaf helpers only; keep std:: containers out of those frames.
- Usercmd edits need all 3 hooks (CreateMove + GetUserCmd + ValidateUserCmd-noop) — already installed in bhop.cpp; new features just edit pCmd inside Movement_Run.
- Prediction only runs when edge jump active (`needPred` in Movement_RunImpl) — extend that condition when new features need predicted state.
- Menu: BindKey() must come right after the feature's Checkbox on the SAME row (does SameLine itself).
- Config: table-driven — add 3 entries (bool/key/mode) per new bound feature in config.cpp + globals.h + dllmain.cpp.

Build: `cmake --build build --config Release` from `Desktop\SimpleVisual\cplus` → build/Release/dih.dll. This pass: 0 errors 0 warnings, 1.14 MB.
