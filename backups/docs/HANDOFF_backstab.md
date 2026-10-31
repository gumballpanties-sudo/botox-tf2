# Handoff: auto backstab (dih-bs16 = bs15 + turn guard)

## dih-bs16 (2026-09-24) -- read this first
First live bs15 log (`Desktop\DEBUY\debug (4).log`, Legit, 108 swings, horizon ~0.1 s):
91 backstabs, 12 front stabs, 4 whiffs. Analyse any log with `python tools/bslog.py <log>`.

| finding | numbers | action |
|---|---|---|
| **Turn bets** = the estimator's extrapolated turn >= the window margin (the log's margin is unsigned, so it cannot prove the seen yaw was outside; bs16 logs gm= so the next one can) | 13/22 landed, 9 of the 12 fronts. Full-rate estimator (k=2): 21/31 | **fixed**: turn guard |
| Every other swing | 78/81 landed, calibrated per p bucket | untouched |
| Whiffs | 4, all slack <= 5u with both players moving; but slack < 3u swings landed 33/39 | untouched (a motion pad would delay ~41 good swings to dodge 4) |

**Turn guard** (`bs_math.h TurnGuarded`, `k_turnTrust 0.4`): the swing is decided on
min(p on the bet, p at the guard point = seen yaw + 40% of the bet's turn, judged on `sigmaCore` =
sigma without the rate-spread term). Still targets unchanged; turn-ins wait until 40% of the turn
would already be a backstab.
- 0.4 picked by a closed-loop sweep (seen-only / 0.25 / 0.4 / 0.5 / 0.6 / stop-anywhere mixture,
  30/80/150 ms ping, human flick/spy-check/pan/still + spinner yaw models through the real Track).
  0.5 leaks fronts; seen-only (0) costs ~50 ms more for nothing.
- `backstab_test` out-of-sample result (fresh seeds, 300 episodes per style, one swing each):

  | ping | humans bs15 stabs / fronts | humans bs16 stabs / fronts | bs16 later on shared stabs |
  |---|---|---|---|
  | 30 ms | 845 / 32 | 862 / 9 | +14..27 ms |
  | 80 ms | 767 / 120 | 858 / 14 | +10..27 ms |
  | 150 ms | 734 / 141 | 763 / 102 | +32..58 ms |

  **Known ceiling:** spy checkers (180 away, short look, 180 back) at 150 ms: 221/71 -> 219/72, even.
  Their back shows for less than the horizon; no variant did better. Bounded in the test.
- `sigmaCore`, not `sigma`: with the full sigma a spinner at 150 ms ping was locked out ~0.8 s.
- Next-tick guard: seen lead = one tick, 0 while he chokes, capped at the horizon.
- `why=turn` = bs15 would have swung this tick and the guard stopped it (refusal OR a guard-made
  hold; hold count still follows the decision, so `k_patience` still ends it).
- Logs gain `pb=` (bet odds = bs15's p), `pg=` (guard odds), `gm=` (signed guard margin, deg),
  `sc=` (sigmaCore) on STAB / miss / outcome, and `turn=` in the gate counters.
- Cold critic round (bs16): no wiring/format bugs; findings fixed = pooled test hid the checker
  regression (now per style, per ping), log could not tune the knob (gm/sc added), choke lead,
  lead past horizon, why=turn undercount, weak/circular checks, tuning-seed reuse.

Backups of the bs15 state: `backups/src_baks/{bs_math.h,aim_melee.cpp,aim_melee.h,backstab_test.cpp}.bak_20260924_183118`.

**Next log:** check the stamp says dih-bs16, run `python tools/bslog.py`. `why=turn` followed by a STAB
on the same idx a few ticks later = the guard only delayed; no later STAB = a stab it cost. Fronts
where pg < pb should be rare. If many turn-ins are refused that bs15 would have landed, raise
k_turnTrust toward 0.5, never past it without a log.

---

# Handoff: auto backstab rewrite (dih-bs15)

Date: 2026-09-15. Status: **built and gate-tested, NOT tested in game, final critic round (9) not run.**

## Goal (user)
Make the knife's auto backstab (Legit + Silent) better:
- stop missing sidestabs
- stop swinging too early
- swing even when the back shows only briefly
- work on any net settings

## Where things are
| File | What |
|---|---|
| `src/hacks/aim/bs_math.h` | NEW. Engine-free math: server stab test as angle windows, odds (normal CDF), swing reach/order, server lerp, per-player snapshot Track, Rewind (lag comp copy), Decide. Header comment explains model and SDK citations. |
| `src/hacks/aim/aim_melee.cpp` | Knife path rewritten: world trace filter, `StoreBackstabSnapshots`, `UpdateBackstabTracks`, `JudgeKnife(int iBox, ...)`, `RunBackstab`, `Event`. Dead knife branches removed from `GetTargets`/`SortTargets`; `PredictSwingEye` deleted. Non-knife melee untouched. |
| `src/hacks/aim/aim_melee.h` | New gates enum, `BsJudge`, box arrays, realtime clocks for press/whiff/outcome. |
| `src/hacks/aim/hooks.cpp` | FSN `FRAME_NET_UPDATE_END` now calls `F::AimbotMelee.StoreBackstabSnapshots()`; includes `aim_melee.h`. |
| `tests/backstab_test.cpp` + `CMakeLists.txt` target `backstab_test` | Gate test, 330 checks, PASS. |
| Backups | `src/hacks/aim/{aim_melee.cpp,aim_melee.h,hooks.cpp,aimbot.cpp}.bak_20260915_010554`, `CMakeLists.txt.bak_20260915_010554` (bs14 state). |

Build and test:
```
cd C:\Users\huber\Desktop\SimpleVisual\cplus
cmake --build build --config Release --target backstab_test && build\Release\backstab_test.exe
cmake --build build --config Release --target dih
```
Last result: both clean, 330/330 pass, `build/Release/dih.dll` built.

## Root causes found in bs14 (all SDK-verified)
1. **Yaw source was the interpolated netvar.** The server reads the target's LIVE `pl.v_angle` (baseplayer_shared.cpp:310), and lag comp never restores it. Sampling the interpolated `m_angEyeAngles` added cl_interp (default 0.1 s) to the guess horizon, which is why it depended on net settings.
   - Fix: sample RAW values at `FRAME_NET_UPDATE_END`. PreDataUpdate has restored last-networked values there (c_baseentity.cpp:2166).
2. **Worst-case band.** It used the max of the last 16 prediction errors, so one flick refused every sidestab or short window.
   - Fix: measured odds. Take the RMS error of the best of 3 estimators (hold / half rate / full rate), then a CDF over the server's own yaw window.
3. **Rendered position sits 1 tick + 1 frame behind the lag-comp rewind tick.** Swings at reach edge whiffed.
   - Fix: `Rewind()` reproduces BacktrackPlayer on our raw records at `tick_count - TIME_TO_TICKS(server lerp)`.

## Key model facts (easy to get wrong again)
- **Two clocks.** `m_flSimulationTime` runs on the target's tickbase, 0-120 ms AHEAD of the absolute tick (player.cpp:3140-3172).
  - Rewind records are keyed on sim time; lag comp compares exactly those.
  - Yaw samples are keyed on `GetLastTimeStamp()` (packet time, absolute clock).
  - `tDue = tick_count*TICK_INTERVAL + AimLatency()`.
- **TF2 tick = 0.015 s**, not 1/66 (const.h:31).
- **Server lerp** = `max(cl_interp, clamp(ratio)/clamp(updaterate))`, 0 if cl_interpolate 0 (player.cpp:3484-3515). Uses Source clamp semantics.
- **Swing:** ray first; the ±18 hull only if the ray hit nothing (melee:509). Teammates block and are lag-compensated when meleeing (tf_player.cpp:20105). Buildings of both teams block. Only medigun shield and revive marker are team-skipped (IsCombatItem).
- **Yaw windows:** behind 90, facing 60, facestab 107.4576 degrees. Aiming within ±17.46° of the line to his centre means facestab cannot bind.
- **Sim-time rollover:** a sim tick older than the network base arrives as a multiple of 100 ticks (or +256) with identical origin/yaw. It is treated as a duplicate.
- **Lag comp 64u rule:** it applies per record STEP. The server leaves the target at his live origin if one step is over 64u (lagcomp.cpp:479-484).

## Decision
- **Thresholds:** `k_pSure 0.90` fires immediately. `k_pTake 0.70` fires if the next tick is not ≥ +0.03 better, reach closes next tick, or 4 ticks have been held. Below that: refuse.
- **Sigma** = max(fast EMA α.2 prior 15°, slow EMA α.05 bias-corrected) × horizon stretch, plus in quadrature:
  - floor 1°
  - onset 30°/s × horizon
  - |rate| × 1 tick timing
  - 0.25 × |rate| × horizon in-flight spread
- **Position error** = `0.175 + 0.5*v3D*extrap + 1500*extrap²`. Reach pad = 0.25 + that. Bearing sigma = atan(posErr/dist).
- **Rewind `extrap` semantics:**
  - 0 when inside records.
  - Priced gap past newest record: half-step for chokers, capped guess but 2×gap price past `k_maxExtrap` 0.1 s for non-chokers.
  - `k_unknown` (1000) when T is older than all records or there are fewer than 2 records. JudgeKnife refuses that as NOTRACK, about 120 ms after dormancy at lerp 0.1.
  - Choke-live check: predicted batch step > 64u and age ≥ half a batch → 2×age.

## Critic loop status (cold subagent, SDK as reference)
- **R1:** 15 defects. Fixed: clock bug, friendly buildings, target cull, trace cap, filter, graze, ghosts, etc.
- **R2:** overconfident sigma, Rewind vs choke/64u. Fixed.
- **R3:** choke pricing, rollover, sparse-record false break, packet clock poisoning. Fixed.
- **R4:** `k_tick` was 1/66 (real bug), choker one-sided error. Fixed.
- **R5:** >64u batches, 3D speed, uncapped extrap price. Fixed.
- **R6:** post-dormancy rewind unpriced (HIGH), varying batch size. Fixed.
- **R7:** "refuse" multiplied by zero speed (HIGH), live fallback, single dup refusing at high ping, no accel term. Fixed.
- **R8:** batch-based choke gate, accel coefficient 375→1500. Fixed, tests added.
- **R9:** NOT RUN, per user. The last two changes (per-batch choke gate `Rewind()` in bs_math.h, `PositionError` 1500 coefficient) have gate tests but no independent review yet.

Trend: severity fell each round, from core model bugs to choker/dormancy corner cases.

## Known RISKs (acknowledged, not fixed)
- `FRAME_NET_UPDATE_END` sees one snapshot per frame. At fps below updaterate, intermediate records are lost.
- `tDue` latency model (OUT+IN latency) can't be verified from the SDK.
- Onset 30°/s, thresholds, and calibration are unmeasured. Calibrate from logs.
- The lag comp 200 ms fallback (lagcomp.cpp:405-412) is not modelled (doubletap-shifted commands).
- A non-choker who starts a long choke inside the RTT window is unpriced.
- The world filter is a blacklist. Static-prop behaviour inside TRACE_EVERYTHING is engine code.
- The step-vs-linear audit truth for low updaterate is approximate.

## Next session: do this
1. **In-game test.** Enable `debug log` in the menu, play spy, and cover:
   - Legit and Silent
   - cl_interp 0.1 and 0.0152
   - bots and real players
2. **Read `C:\dih\debug.log`** lines prefixed `BS:`:
   - `STAB`: p, next, margin, sig, bear, k, rate, hor, lerp, ext, slack, pad.
   - `outcome` per swing: `BACKSTAB` / `NOT A BACKSTAB` with the promised p.
   - `WHIFF`: reach problem. `PRESS DID NOT SWING`: input or CanAttack, not odds.
   - `miss why=<gate>` every 0.5 s, plus gate counters: cd nt ntr rng fov blk miss ign rch fac odds hold.
3. **Calibrate.** Bucket fires by p and compare the real backstab rate per bucket.
   - Front stabs at p≥0.9: sigma too small. Check k=, hor=, onset.
   - Many `why=odds` on stabs a human would take: sigma too big, or lower `k_pTake`.
   - Many `why=rch`/WHIFF: check pad/ext numbers.
4. **Optional:** run critic round 9 on the per-batch choke gate and `PositionError` if more review is wanted.
5. **User settings seen in `C:\dih\default.cfg`:**
   - BackstabAimType=1 (Legit), BackstabFOV=46 (Silent only; refuses steep point-blank angles, logged `why=fov`), BackstabIgnore=131 (invuln + invisible + razorback).
   - AutoBackstab=0 in default.cfg (turn on).
   - AntiCheatCompatibility=1: checked, it doesn't alter the swing tick.
6. **No git repo** in cplus, so there are no commits. Backups listed above. The DLL needs re-injecting to take effect.
