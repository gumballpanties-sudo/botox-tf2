# HANDOFF — Texturebug (antoha bug) session 2026-07-06 (+ perf rewrite 2026-07-07)

## dih155 (2026-09-18, UNTESTED) — real TB: pin window FLOOR + closing rungs (no fake/server code)

(A server-side "hold pin" was built and reverted same day: user wants real TB only, see memory no-fake-features.)
Physics (Review.txt 9.1 + 9.4.2): a swept trace collides only in case 2, s2 = gap - v_in*dt <= 0. So the pin window is
  gap/dt < v_in < room*|vz_mid|/(D+eps)   (floor = actually reach the wall, ceiling = top side wins).
Legacy solve had the ceiling only: band rungs = band_max*{.7..0.015}, never floored -> whenever gap > band_max*0.7*dt every rung stops short of the wall = the "mindz=12 zero contact" rows. Band rungs creep ~4e-4u/tick, entry rungs stop at >= 0.3 eps, so reaching gap ~1e-4 took ~40 ticks = dih91 "short contacts pin 0/25".
Also: every pin's 2nd TryPlayerMove bump hits the wall side -> gap = eps after each pin; next pin needs gap closed to ~1e-4 again first.
Change (texturebug.inl solve pass):
- pin rungs = max(band_max*frac, 1.15*gap/dt + 0.0005). Collapsed rungs dedup to one sim.
- closing rungs `k_tbCloseQ` {0.02,0.1,0.35}: v_in = (gap - q*min(gap,eps))/dt, s2 floor 4 ulp / 2e-5, only when gap < 2 eps. Never pin; creep min-support picks deepest non-colliding.
- ride rollback snapshot moved AFTER solve (only fan rolled back). Creep ride fwd<=1 cap waived when sim xy >= xy_base - 0.5.
- solve cap/reserve 22/26 -> 30/30.
Offline model (scratchpad rungs.py): gap 5e-5 vz -24 D 100 old 2/6 rungs pin, new 6/6; gap 0.001-0.0156 old 0/6 forever, new closing lands 2e-5..3e-4 -> feasible next tick.
Log: TB line adds `cls=` (closing rungs simmed) `plo=` (gap/dt floor).
Judge: same wall. Win = more B, B-C-B cycles longer (C with gap < 1e-4 then B), fewer C rows with gap stuck 0.001-0.03. Lose = cls>0 but next tick gap back at 0.03125 (closing overshoot collides) -> lower q / add brake check.
Backup: `backups/src_baks/features/*.bak_20260918_113052_dih155_pre`.

## dih153 (2026-09-17, UNTESTED) — TB_Simple: plan replay when wall scan drops, sticky crouch

dih152 verdict (user: "crouches when unoptimal, doesnt crouch when optimal"). 323 `TB S` rows C=224 L=90 B=9. B/L runs 2,1,7,11,6,10,7,11,9,1,14,8,1,11. maxUs 3894. Policy wins graze 79, graze-crouch 2, brake450 9, rest 0.
- Planner countdowns that stay on log land B on the exact tick (rows 36-42, 73-82, 105-111, 163-169, 230-237). Script says landed 48 / missed 42, but checked misses (58, 127) are a new jump next row (oz 395 -> 434, ride=0), not a wrong prediction.
- Crouch: both crouch wins (rows 143, 175) = landed tick `pol=1 t=2`, next logged row 78u+ away (ox 194 -> 116). Crouched hull left the reach-2 `MV_DetectWallLines` -> TB_Simple returned 0 -> buttons back to raw -> uncrouch -> fall. `duck=0` on all 323 rows: no crouched tick ever kept the wall.
- Base grid + creep built buttons from the raw cmd -> any planner crouch dropped the next tick, even on the pin tick.
- Cap 96 < 10 policies x 12 ticks: brake+into pair never simmed on no-catch ticks.
Change (texturebug.inl `TB_Simple`):
- REPLAY: no wall but a plan with t>=2 -> fly the stored policy open-loop (stored wall yaw + seed), t-1 each tick, 'B' on its pin tick. Log `rp=1`.
- STICKY CROUCH `s_duck_hold`: every cmd (grid, creep, planner, replay) builds from held buttons = raw ^ IN_DUCK while held. Policy `duck` = TOGGLE vs held state. Committing a toggle sets warm to its non-toggle twin (`TB_PolTwin`) so next tick keeps the state instead of flipping back. Released on H / no wall without plan / not riding.
- Planner tie rule unchanged (strict earlier t): crouch toggle only wins when it catches sooner.
- `k_tbPlanCap` 96 -> 160 (~+1.6ms worst at ~25us/sim).
Log: `TB S:` adds `hold= rp=`. `tb_log.ps1`: hold/duck/replay row counts, replay B count.
Judge: same wall. Win = `duck=1` rows exist with act B/L (crouched and still catching), `replay B` > 0, B/L runs longer than dih152 (max 14). Lose = hold=1 then H/C and fall = crouch still wrong -> check if pol toggles back and forth (pol alternates 1/0 on consecutive L rows).
Backup: `*.bak_20260917_072409_dih153_pre` (texturebug.inl, movement.cpp, tb_log.ps1).

## dih152 (2026-09-17, UNTESTED) — SIMPLE ENGINE default: 1-tick grid + K-tick catch planner. Menu cut to 4 knobs

dih151 verdict = LOSE (user: "still sucks"). 58 `TB REF` rows C=56 B=2, 2 `TB RESC` both `tried=56 pinned=0 mindz=12.000`.
- Both B at oz **416.75125 / 416.75122**, along-wall 21u apart (-2157.6 / -2136.8). Every B: pz == oz (z stops at trace start), xy keeps sliding 2.2-2.8u/tick.
- => catch = hull landing on a SHORT brush-top ledge inside the 0.03125 wall epsilon. Landed tick has already slid off it. 106 sims per landed tick (grid + duck flip + into-wall fwd 30 + 2-tick) zero contact. One-tick rescue is dead.
- Every grid so far caps fwd at 30 -> accelspeed 10*30*dt = 4.5 u/s/tick. fwd 450 = ~67 u/s/tick (engine clamps to class maxspeed). Never tried.
Change (drastic):
- `TB_RefLoop` + `TB_LandRescue` DELETED. New `TB_Simple()` (texturebug.inl above `TextureBug`) is the default engine.
  1. message.txt 1-tick grid, deduped by air-accel delta (ride ticks collapse 45 cells -> few sims). Pin -6/+6 -> 'B'.
  2. Planner (no 1-tick pin): 10 held policies x up to K ticks (`g_tbLookahead`, def 12): graze seed, into-wall 450, brake 60/200/450 (wish opposite velocity), brake+into 450; duck-flipped copies. Earliest -6 pin wins, later policies only roll to beat it (depth prune), warm = last winner first. Cap 96 sims + SimsLeft. Commit tick 1 -> 'L' (owns cmd). t=1 -> 'B'.
  3. else creep (support < 0.2) 'C', else hand back.
- Full TB kept as "legacy engine" (`g_tbLegacy`), dih151 rescue call removed from it.
- Menu TB group: lookahead ticks (1-24) / allow braking / allow crouch / legacy engine. All old sliders only show under legacy. cfg: `tbLegacy tbLookahead tbAllowBrake tbAllowCrouch` (tbRefLoop/tbLandRescue gone).
Log: `TB S: act(B/L/C/H) vz mindz bsup sims plan pol t K ride xy nx ny ox oy oz duck us cap w=<wins per policy>`. `tb_log.ps1` REF block reads `TB S:` too: B/L runs, "L predicted B landed/missed", policy wins.
Judge: same wall, defaults. Win = B/L runs > 6 with B repeating every few ticks, `L predicted B landed` >> missed. Missed high = rollout != real (prediction chain bug). Only brake policies win + speed dies = ledges too short to ride at speed, try allow braking off to compare. us < ~6000.
Backup: `*.bak_20260917_071244_dih152_pre` (texturebug.inl dllmain globals config menu movement tb_log.ps1).

## dih151 (2026-09-17, UNTESTED) — landed-tick rescue (P1 duck flip, P3 2-tick, P2 into-wall), both paths

dih150 verdict (70 `TB REF` rows, 0 full-TB rows in ref section, parsed by `cplus\tb_log.ps1` REF block): C=67 B=3. duck=0 hz=82 on all 70 (duck never changed).
- B z band: oz 415.934 / 414.134 / 414.556 (1.8u spread) across along-wall oy -2103.9 / -2118.1 / -2173.9 (70u). Gaps 14.2u/5 ticks and 55.8u/38 ticks = irregular = not seams. = **same-z edge**.
- Approach 2 (rows 31-40) fell 416.736 -> 412.431 in one tick (vz -281) = jumped the band, no B. Approaches 1/3 landed an origin inside it -> B.
- Every B: ox=-244.000 vx=0 (hull flush on plane pd=220, gap 0). Every landed C: drop oz-pz=0.180 (pure gravity), wz-oz 0.0002-0.0005, mindz 12.000.
- Graze grid on ride ticks: xy 181-190 with fwd<=30 -> addspeed<=0 on all 50 sims = 50 copies of one free fall. dxy 0.15/tick (fwd 1 bleed), 4.49/tick on fwd 30 approach ticks.
- Order rule fired: same-z edge -> P1 first. Drop>0.1 also true (0.18) but it is gravity on every vz -6 tick = no info -> P3 second. Seams rule not fired -> P2 last.
Change: `TB_LandRescue()` (texturebug.inl, above `TB_RefLoop`). Gate: pinned last tick (`caught_last_tick`) && base grid no pin && `g_tbLandRescue`. First pin (dz<0.1) wins, committed.
- P1: IN_DUCK toggled vs original buttons, same 9 fwd x 86-90 graze grid, TbDedup (graze collapses -> few sims).
- P3: 2 candidates (best-dz P1 variant ducked, seed standing), sim x2, pin on tick 2 -> commit tick-1 cmd.
- P2: off 0, +-15..+-75 x fwd 30/15/5/1, standing, dedup.
- Cap 60 sims + SimsLeft() every sim. Ref loop: -6/+6, any xy, seed = base best. Full TB: runs after fan pass, -6 only (mid-ride pin_dz rule), xy>=1, seed = warm (last pin), win -> best_pin + duck_needed latch = pin's duck bit.
- Menu "landed rescue" under "ref loop", cfg `tbLandRescue`, `g_tbLandRescue` def ON. Ramp/disp hands-off + steer-away untouched.
Log: `TB RESC: pass(1/3/2, 0=miss) tried pinned fwd ang duck vz mindz us cap p1 p3 p2` (p* = cumulative pins per pass). `tb_log.ps1` prints RESC count + pins by pass.
Judge: same wall, ref loop ON then OFF. Win = landed C rows (vz -6) followed by `TB RESC pinned=1` and next row B/vz -6 again, ride > 6 ticks. Which pass pins = what the edge wants (p1 = hull height, p3 = timing, p2 = inward push). All `pinned=0 mindz=12.000 tried<=60` = no single-tick input reaches this edge from a flush landed tick -> geometry: spot is a z band you only land in by falling into it, not a ride. Watch us < ~6000 and cap=0.
Backup: `src/**/*.bak_20260917_070015_dih151_pre`, `tb_log.ps1.bak_20260917_065536_dih151_pre`.

## dih150 (2026-09-17, UNTESTED) — diag only: origin/contact fields on `TB REF:` line

dih149 verdict = REF LOOP SAME AS FULL TB (user: "still behaves the same"). 103 `TB REF` lines, 0 full-TB lines (toggle on). Same shape: B (sims 1-4) -> C vz -6 mindz=12.000 (50 sims, zero contact) -> gravity. Best stretch B-54 C-6 B-18 C-6 then fall; B-66 C C B-30 C B-18 C then fall. us ~1.8-2.8ms, cap=0 everywhere.
=> the loop is NOT the bug. message.txt verbatim on our primitives cannot ride this wall either. Left: (a) this wall has no ride (catch points are separate seams, not one edge), (b) our sim base / hook timing differs from message.txt's engine_pred. Pins land when predicted (next vz -6), so (b) only matters if message.txt's own cheat rides THIS spot.
Change: none to behavior. `TB REF` line adds `ox oy oz` live origin, `pz` predicted z of chosen variant, `wz` wall hit z, `pd` plane dist, `wfr` hit fraction, `vx vy`, `duck`, `hz` hull height.
Judge: along-wall coord (ox or oy per nx/ny) at each B. Same z band + regular spacing along wall = seams at brush boundaries (geometry). oz steps vs pz on landed tick = how far hull is from the catch.
Backup: `src/features/*.bak_20260917_064933_dih150_pre`.

## dih149 (2026-09-17, TESTED SAME — see dih150) — menu "ref loop (message.txt)" = message.txt loop verbatim

dih148 verdict = LOSE (94 TB lines, `cplus\tb_log.ps1`): user "behaves same". C=78 B=15 H=1, max B run 4, rides 2,2,4,2,2. All 5 landed C rows (13,40,49,75,79: vz -6 ride=1) read **rty=32 rtp=0** mindz=12.000 = retry budget spent, zero contact. Every ride = B(retry pin, rty>=1 rtp=1) B(warm) land C fall. Retry dead end.
Change: `g_tbRefLoop` (dllmain/globals.h/config `tbRefLoop`/menu checkbox in TB group, def off). On = `TB_RefLoop()` (texturebug.inl above `TextureBug`): message.txt:109-398 line for line on MV_DetectWallLines(0.35, reach 2, no lookahead) / RestoreToFrame+Pred_Simulate / CorrectMovement / MV_SupportDistance. fwd {1,.875,.5,1,1.3856,2.125,2.875,5,15,30} x angle 85.999..89.999, pin = |vz-+6|<0.1 either sign every tick, else creep if best support < 0.2, else hand back. Kept: ramp/disp hands-off (g_tbRampNz), steer-away hand back while riding. Full TB state dropped each tick (drop_ride) so toggling back is clean. dih148 retry cap still in full TB.
Log: `TB REF: act=B/C/H vz mindz minsup bsup fwd ang sims ride xy nx ny us cap`.
Judge: same wall, toggle ON. Ref rides (B C B C / B B B, > 6 ticks) = our machinery is the bug, bisect back from ref. Ref also `B B C fell` with mindz=12 on landed tick = the wall/approach, not the loop.
Backup: `src/**/*.bak_20260917_064353_dih149_pre`.

## dih148 (2026-09-17, TESTED LOSE — see dih149) — ride retry cap 8 -> 32, + `rty=`/`rtp=` log

dih147 verdict = PARTIAL (155 TB lines, script `cplus\tb_log.ps1`): P/Q gone (C=129 B=25 H=1, no D). B->B 12, max B run 4 (was 2), ride runs 2,2,2,4,2,2,4,2, max us 6146, cap=1 on 9 approach rows only.
Landed tick (C vz=-6 ride=1) after a B: next tick pins **1 of 11**. mindz=12.000 = |vz|+6 = no sim touched wall (not a rejected +6 pin).
Creep caps listed for PARTIAL do NOT bind: every landed C reads cfwd=0.250 (< `k_tbCreepFwdMax` 1) and hsup=0.000 (< 0.2 hug). Veto moot, creep valid.
What worked from vz -6: rows 50/110 act=B sims=4/8 = warm + 2-tick retry pin -> next tick B again, ride 4 ticks. Retry cap `retries_left = 8` burns on warm + tiny fwds (0.001/0.01) before 0.25+ grid ever gets a retry.
Change: `k_tbRideRetries = 32` (texturebug.inl near `k_tbPressHold`), used when `caught_last_tick`. Fresh catch stays 8. Diag only: `rty` retries used, `rtp` retries that pinned (end of TB line).
Judge: landed C rows (vz -6 ride=1) should turn B with rtp>=1; rides > 4; watch us/cap on ride ticks (budget ~+1.2ms worst). rty=32 rtp=0 on landed ticks = retry dead end -> step 3 (message.txt-verbatim toggle).
Backup: `src/features/*.bak_20260917_063703_dih148_pre`. movement.cpp also carries unrelated velind s.tick/s.curtime lines (2026-09-16 21:38).

## dih147 (2026-09-16, TESTED PARTIAL — see dih148) — A/B: P/Q press OFF, creep gets no-pin ride ticks

dih142-146 logs, same wall: every ride = `B P Q Q Q ... fell`. Tick after landed pin (P, vz -6) never pins, 110-118 sims all gravity. P/Q = unsimmed analytic press (dih131/132), outranks creep (`texturebug.inl` commit ladder), `vin=0 gap=0.00002` frozen. message.txt hold = simmed min-support creep only.
Change: `k_tbPressHold = false` (texturebug.inl near `k_tbHugRideScale`) gates press branch. Creep unchanged (still xy_brake veto, `k_tbCreepFwdMax` on ride, hug 0.2 x2 on ride). Built as `build/Release/dih147.dll` (dih.dll was locked by TF2).
Judge: P/Q gone, acts = B/C. Win = B on consecutive ticks, rides > ~6 ticks, `dface` not climbing past `duse`. Lose = C ticks also never re-pin → step 3 (message.txt-verbatim loop toggle).
Backup: `src/features/*.bak_*_dih147_pre`.

## dih75b (2026-08-25, UNTESTED) — dih75 tested "completely broken". Press gate reverted.

dih75 log, 185 TB lines: **`pbr=11` (press) on 160 of them, pins 302 -> 9**, zero rides. The lines
read `minsup=-0.002 pin=0 vin=0.0000 pfwd=2.125` with vz stepping -293/-305/-317/-329/-341/-353 =
pure gravity while xy held at 284: TB pressed the player flush against a wall through an entire
free fall with their input taken away, and never caught anything.

Cause = dih75 change #3 alone. Widening the press to `hold_grace` made it self-sustaining: press
owns the cmd -> owning re-arms `s_ride_grace` -> grace satisfies `hold_grace` -> press again,
forever. **This is coldtires' own 115th, which they measured and reverted** ("146 of 152 press
ticks were approach ticks, they produced ZERO pins... it only displaced the creep, which at least
closes a gap") — the dih74 comment quotes that note and dih75 overrode it anyway.

dih75b:
- press compute gate back to **`caught_last_tick`** (after a REAL pin only)
- ladder order back to dih74 exactly: `smash-esc 9 / pin 1 / freshgrab 2 / park 3 / coast 8 /
  peel 10 / press 11 / creep 4 / drive 5 / ride-drive 6 / handback 0`. dih75's press-above-coast
  swap was speculative and shipped in the same build as the gate, so it could not be read
  separately — one change per test.
- **KEPT: the mid-ride solve + fan unlock.** That is the only thing this build measures.

Next log: ride ticks (`ride=1`) must now read `solve>0` / `fan>0` instead of dih74's 419/419
`solve=0 fan=0`. If ride ticks show `us=` near 6000 or `cap=1`, the clock is binding — lower
`k_cmdTimeShare` (0.40) before touching any pass.

## dih75 (2026-08-25, TESTED BAD — see dih75b) — "still slips off surfs". THE RIDE HAD NO SEARCH AT ALL.

dih74 log, 820 TB lines, and one number settles it: **419 of 419 ride ticks read `solve=0 fan=0`**,
with `sims=66 dup=91 vin=0.0000` and `mindz = |vz| - 6 EXACTLY` — 66 prediction sims a tick that
collided with **nothing**, on every tick of every ride.

Why 66 sims can touch nothing: `addspeed = min(fwd,30) - vel·wishdir`. The base grid only sweeps
**86-91° off the wall normal = along travel**, so at 300 u/s `vel·wishdir ≈ 300`, AirAccelerate
returns early and the velocity is **never touched**. Every graze variant is a NULL MOVE. The only
wishes with addspeed left point INTO the wall (perpendicular to travel, `vel·wishdir ≈ 0`, full 30
lands) — and those live in exactly three passes, all three switched off on a ride:
- **solve** and **fan** — gated `!ride_had_clean_pin`, which is true for the whole ride
- **press** — gated `caught_last_tick`, false on any creep/coast tick, and the coast branch
  outranked it anyway

So a ride's no-pin tick = 66 useless sims + a replay of a move that had *already failed its own
re-test that tick* (`warm=1` then `pbr=8` coast, 128 of them in one log). Hull free-falls ~0.35u
per tick, pins re-land by luck every 2-3 ticks, ride dies in ~13. That is "slips off surfs".

dih75:
1. **solve gate: `!ride_had_clean_pin` DELETED.** `!found_bug` already says the ride is not pinning.
2. **fan gate: same.** Its steep angles are the only variants that can accelerate a 300 u/s hull
   toward the plane at all.
3. **press computed on `hold_grace`**, not `caught_last_tick` — one no-pin tick used to kill it
   for the rest of the ride, exactly when `vin` has been clipped to 0.0000.
4. **press moved ABOVE the pin coast** (still below park/peel — pressing into a plane the sweep
   just called a wallbug is glue). Coast is now the fallback when no press exists.

Next log check: ride ticks should read `solve>0` / `fan>0`, `vin` should stop reading 0.0000, and
`pbr=11` should replace the `pbr=8` runs. If `cap=1` appears on ride ticks, the clock is binding —
raise `k_cmdTimeShare` before touching any pass.

## dih74 (2026-08-25, UNTESTED) — full review + revision vs coldtires: faster, catch more, SHORT LEDGES

Read from the dih73 log (1059 TB lines): `sims=75 dup=91 mindz=|vz|-6 EXACTLY supf=75` on every
tick = a 75-sim sweep where NO variant collided at all, plus `tbScanStep=8` in the user cfg =
a z grid that cannot see a lip shorter than 8u. Ported the 8 pieces coldtires has and we did not.

**Short ledges (2 fixes, both in `MV_DetectWallLines`)**
1. **Column sweep** — one box trace with ZERO x/y extents over the hull's z span, per direction,
   at the same geometry as the lines. It crosses every face the lines cross *and every face
   between them*, so a lip under the scan step is finally visible. Also: miss = the whole column
   is empty (skip the 11-73 line traces), hit = it bounds them, so the line loop usually never
   runs at all. Its impact x/y is exact, which also fixes the `s_z` brush-top trace (short brush
   -> big `v_target` -> the solve's window is wide; a low lip is the easiest catch once aimed at).
2. **Fall lookahead** — the scan only looked ahead HORIZONTALLY. The sweeps sim a tick forward and
   the hull drops ~6u/tick at 400 u/s, so a lip below the feet was "no wall" on the one tick that
   could catch it. Scan span extends DOWN by one tick of fall, capped 32u, falling only.

**Faster** (each measurable in the new TB line: `fast= fok= tr= res= resk= us= cap=`)
3. **Analytic fast probe** (coldtires 103rd) — closed-form air tick + 2 hull traces + 1 support
   ray replaces a full `Pred_Simulate` for every variant that touches nothing. One calibration
   sim per tick licenses it (measures accel*dt*friction, proves the tick is plain air). OFF on
   rides (`hold_grace`) and on any duck — a pin is exactly the state where a trace says clear.
4. **Restore skipping** — `s_predDirty`: only a sim / a hand-written state dirties the frame.
5. **Scan fallback reuse** — a failed locked scan keeps its own unlocked answer, so the seam
   tick's second full 16-direction scan is free. Plus `k_tbLockDirBound` skips directions that
   provably cannot hold the ride plane.
6. **Time budget** — `k_cmdTimeShare` 0.40 of a tick. The 192 count is now the bad-QPC ceiling
   only; a bound COUNT was truncating the solve/fan tails, i.e. the passes that find the pin.

**Catch more / better**
7. **Bounce fan** — steep angles 83.999..10 x low fwd. Our grid is 86-91 = pure graze = the RIDE
   move; a fresh contact arrives near HEAD-ON and its pin lives at an angle we never tried.
   Additive: no-pin ticks only, never on a plane that already surfed, wallbug rolled back.
8. **Press hold + peel + smash guard** — flush-with-no-pin now presses into the PAPER band
   (analytic, 0 sims) instead of re-picking a degenerate support; a stuck pin the park refuses
   peels OUTWARD with the speed on instead of gluing; a pin that dies next tick (2-sim chain,
   first `k_tbRideSettled` ticks only) peels instead of smashing. New pbr codes 9/10/11.

Deliberately NOT taken from coldtires: displacement faces (needs its own commit contract),
ladder skip, the freshgrab peel (that tick belongs to AirStuck — dih55).
Backup: `src/hacks/movement/movement.cpp.bak_*_pre_tbrevision`. Build stamp dih74.

## dih42 (2026-07-13, UNTESTED) — "crouch bad timing, UNCROUCHES and loses surf, base it more off message.txt" + "message.txt for staying, old method for catching"

Uncrouch root: dih41 stand pass stripped IN_DUCK (`&= ~IN_DUCK`) → stand variant won mid-surf → commit uncrouched the player → hull 82 → surf lost. message.txt NEVER touches IN_DUCK — duck rides in original_buttons untouched.

dih42 split by user order:
- **STAYING = message.txt verbatim**: fresh full sweep (low fwd first) + creep every tick. Ride fast path DELETED (last_valid/last_fwd/last_angle/last_duck/reset_ride all gone), retry pool gated `!caught_last_tick` (catching only). Mid-ride −half-only pin KEPT (`mid_ride = caught_last_tick`) — seam-stop fix, deliberate deviation.
- **CATCHING = old boosters kept**: natural pass, retry pool 8, approach duck-hold, hands-off drive-in fwd 10.
- **Duck = originals only**: duck passes deleted (single sweep pass, `duck=false` everywhere), try_variant never strips IN_DUCK. g_tbAutoCrouch = approach duck-hold (IN_DUCK ORed into pCmd + original_buttons while falling at detected wall, after steer-away check) — crouches BEFORE the seam, never uncrouches mid-ride (hold re-fires every tick while falling at wall).

## dih41 (2026-07-13, TESTED: duck timing still bad + mid-surf uncrouch — see dih42) — "still doesnt crouch in time" + "surfs randomly stopped by seams in the way"

1. **Approach duck-hold**: dih40 duck-first sweep still ducked on the catch tick only — hull change too late. Now `g_tbAutoCrouch && falling && wall detected` → IN_DUCK ORed into pCmd AND original_buttons right after steer-away check = duck held through the whole approach (manual-crouch parity), flows into sweep/natural/creep/drive-in/handback. try_variant stand pass strips IN_DUCK (`else &= ~IN_DUCK`) so the fallback pass is a real stand test.
2. **Mid-ride seam stops** = +half "landed on top" pins committing during an established ride (crossing seam ahead sims as "landed" → commit → stand up → stop). dih31 lesson #3 applied properly: `pin_dz` = −half ONLY when `caught_last_tick && last_valid` (mid-ride, incl. fast path), both signs on fresh catches (desktop tb: rejecting +half globally dropped catches — dih37's mistake was global removal).

## dih40 (2026-07-13, TESTED: duck still late — see dih41) — "auto crouch on tb doesnt seem to work, way better when i crouch manually"

Root: sweep order. Stand pass always ran first + first-pin-wins → stand pins committed before ANY duck variant simmed; duck pass = leftover budget only. Manual crouch = every variant ducked (hull 62 vs 82 eases the catch) = why it felt way better.
Fix: `firstDuck` — mid-ride keeps `last_duck` (flips = fresh-catch only, no crouch flap); cold catch with g_tbAutoCrouch → DUCK pass first, stand = fallback pass; knob off → current live duck state, single pass (unchanged). AS duck passes left stand-first (no complaint).

## dih39 (2026-07-13, UNTESTED) — dih38 "works better but doesnt stick to surfs long" + "AS like a wallbug"

User orders: more message.txt inspo for TB hold; PAPER for AS (dih38 AS = "like a wallbug" = correct read).

1. **TB coast DELETED entirely (both sites: detect-miss + sweep-miss).** message.txt has NO coast — its hold IS the creep: every no-pin tick commits the freshest min-support micro-move (adaptive), keeping the ε band alive so the next seam tick re-pins. Our coast replayed the STALE losing move up to 5 ticks and BLOCKED creep = short rides (dih36–38 all had this). Slip tick now: full sweep → no pin → `best_support < 0.2` creep commit (base path).
2. **TB eps = base VERBATIM 0.1** (was 0.35). Logs: pins land at exactly 0.000.
3. Kept: natural pass, verified fast path, retry pool 8, hands-off drive-in, steer-away. Diag log lost `coast=` field.
4. **AS = hover, not wallbug** (PAPER 9.2): pressing into wall = the stay-stuck wallbug (dih38's anti-normal 450 press — deleted). Real airstuck = hover AT ε boundary, swept/unswept disagree, velocity zeroed with ~no inward speed. dih39 AS = ridobebra sweep verbatim (ang 88..89.999 step 0.111 × fwd {1, 1.3856, 5, 10, 22, 30, 60, 120, 450} SMALL FIRST) + SDK pin gate `vz −half && predicted xy < 1` (surf keeps xy → no diet-TB) + best-align miss fallback (instant approach, never idles) + hold replay while live-stuck.
5. AS eps = min(g_airStuckCatchEps, 1).

Backup: `backups/movement.cpp.bak_*_dih39_pre`. Build dih39.

## dih38 (2026-07-13, TESTED: TB/AS better, rides short, AS wallbug-ish — see dih39) — dih37 "shittier / doesnt hit / AS still diet TB"; source-mandated rebuild

User source map (their words): message.txt = base, HOLDS well catches meh; desktop texturebug.cpp (tb) + PAPER = catch machinery; ridobebra AS good but check TF2 SDK. Coder friend on AS: "its just forwardmove on the best angle".

dih37 log verdicts (fresh, 451 TB lines):
- **live-pin hold replay = PIN LOSER**: every `hold replay` tick (live vz=−6) → next tick vz=−18. Unverified replay ≠ dih36 fast path (which re-SIMS + falls to sweep same tick). DELETED.
- −half-only pin dropped +half catches ("doesnt hit stuff"; desktop tb comment literally warns "rejecting +half dropped real catches").
- AS phase-1 `live_align > 0.05` = DEADLOCK on slants: horizontal support trace never < 0.05 vs slanted face → sweep never ran → "doesnt stick instant on slanted wall".

dih38:
1. **TB pin = base ±half, eps HARDCODED 0.35** (base 0.1; logs show pins EXACT 0.000; knob 8 was the seam-freeze source — knob dead for pin).
2. **hold replay deleted**; fast path/retry-8/coast 2/creep<0.2/hands-off drive fwd 10 all stay.
3. **TB natural pass** (desktop tb): 1 sim of raw user cmd on first-catch ticks; pin → keep user cmd. Logs `TB: natural pin`.
4. **AS = SDK-grounded stuck** (gamemovement.cpp TryPlayerMove): stuck = velocity ZEROED (allFraction==0 anti-normal press / swept-pass-unswept-solid on slants+displacements / crease). Zero + FinishGravity → signature `predicted vz = −half && predicted xy < 1`. That xy gate = the TB/AS split (surfs keep xy). Sweep: anti-normal press fwd {450,120,30,5} × off {0,±20,±40} FIRST (slant stuck lives here, fwd 450 first), tangent {88,89,89.5,89.9} × fwd {1,1.3856,5,15} second (vertical ε-band wallbug). Hold: live vz −6 && xy<1 → replay winning press, 0 sims. Fallbacks: best-xy variant if it halves slide, else press anti-normal 450 — never idles, instant on touch incl. slants. eps = min(g_airStuckCatchEps, 1).

Backup: `backups/movement.cpp.bak_*_dih38_pre`. Build dih38.

## dih37 (2026-07-13, TESTED BAD — see dih38) — dih36 log CONFIRMED GOOD (3440 pins / 6859 TB ticks); fixes from its 2 failure shapes + AS ridobebra port

dih36 log ground truth (user eps knob 8):
- Catches WORK: pins everywhere, retry pool lands seam pins (`pin=1 sims=2`), fast path streaks (`sims=1`).
- **Failure 1 — dying rides**: live `vz=-6` (engine STILL pinned!) but full sweep `pin=0 sims=109`, coast 2→1→0, ride dead. Sim disagrees with engine mid-ride; coast replay never re-pinned.
- **Failure 2 — seam freezes**: pin used `min(|pz−T|,|pz+T|) < eps`; knob 8 → ANY predicted vz in (−14,+14) "pinned" → landed/stopped junk committed = random vertical-seam stops.

dih37 changes:
1. **TB live-pin hold** (THE ride fix, HS locked-replay pattern): after detect+steer-away, `caught_last_tick && last_valid && IsTargetVz(live vz, 1.0)` → replay last win verbatim, ZERO sims, `coast_left=4`, return. Engine is the ride oracle, not the sim. Logs `TB: hold replay`.
2. **TB pin = −half ONLY**, eps capped `min(g_tbCatchEps, 2)`. +half/“landed” states → AS's job. Fixes seam freezes.
3. **coast 2→4** on commit.
4. **Instant on touch**: TB miss + hands-off → silent drive fwd 10 at wall yaw (`s_tbOwnsCmd=true`); WC miss + hands-off → commit best-support variant; manual input still keeps user cmd (invariant #3). Flush by seam tick = catch on first touch.
5. **AS = ridobebra air_stuck port** (movement.cpp:3480 of ridobebra-main), replaces xy-kill-gate version:
   - phase 1: wall in reach, `MV_SupportDistance > 0.05` → silent drive fwd 5 into wall EVERY tick (instant flush)
   - phase 2: `hold_valid && IsTargetVz(live vz, 1.0)` → replay winning move verbatim, zero sims (also fires on 1-tick detect loss = seam coast)
   - phase 3: sweep ang 88..89.999 step 0.111 × fwd {1, 1.3856, 5, 10, 22, 30, 60, 120, 450}, duck stripped (g_asAutoCrouch = 2nd pass); pin = vz ±half ≤ g_airStuckCatchEps; **non-450 pins must survive a 2nd sim tick** (fluke filter, replaces xy gate); fwd 450 pin = hard wallbug, instant accept; slipped hold retried first
   - miss → commit best-align variant (keep pressing at wall), NEVER restores user cmd while bind held; sign = velocity when sliding else `command_number & 1` alternation
6. Diag log still in. Next log: `hold replay` streaks = rides sustained; if seam freezes persist → suspect creep commit (support<0.2) holding a dead-stop variant.

Backup: `backups/movement.cpp.bak_*_dih37_instant`. Build dih37.


## dih36 (2026-07-12 pt6, UNTESTED) — LOG-DRIVEN catch-more; dih35 log = ground truth

dih35 user test: "catches but rarely". Log (C:\dih\debug.log, 1346 TB lines, user eps knob = 8):
- `minsup=-0.031` EVERY tick → detect + approach flawless, always flush at wall.
- `mindz = |vz−6|` every tick = predicted vz is pure gravity (vz−12) — NO variant alters vz except ONE tick: `vz=-257 mindz=0.000 pin=1 sims=31`. **Sim pins vz EXACTLY (−6.000) → our Pred_Simulate is exact after all; eps was never the problem. Pin is only physically possible on the seam-cross tick.**
- Tick right after pin: `vz=-6.0 mindz=12.000 pin=0` → caught then dropped = **1-tick rides** (wall clamp parks player AT ε; band re-entry needs a tick; sweep meanwhile finds pure gravity).

dih36 additions (each ↔ log observation, NOT speculation):
1. retry pool 8: flush (`support<=0.1`) falling near-miss → 1 extra sim of same variant (seam crossed mid-tick pins 1 late).
2. last-win fast path (`caught_last_tick && last_valid` → try_variant(last_fwd, last_angle, last_duck) first).
3. 2-tick coast: mid-ride sweep miss AND detect miss both replay `last_world_yaw/last_fwd/last_duck` (z stays near seam ticks after clamp — re-entry re-pins).
Diag log still in (added `ride= coast=`). Next log check: coast ticks followed by `pin=1 ride=1` = sustain works; if rides still 1-tick → raise coast 2→4 first.
NOTE: this = controlled re-introduction of 3 pieces of deleted v4 machinery, now justified by data. Do NOT re-add the rest (dual-sign, fwd caps, duck-hold) without log evidence.

## dih35 (2026-07-12 pt5, UNTESTED) — dih34 "STILL THE SAME" → diag build

dih34 stamp confirmed in C:\dih\debug.log (new code DID run — not stale DLL; SimpleVisual logs to C:\dih\debug.log, NOT C:\infovisual\). Hypothesis: base eps 0.1 assumes creator's prediction lands vz EXACTLY on ±half; OUR Pred_Simulate has slop — old WORKING dih6 pinned at eps ≤ 1.0, knob floor 0.25. dih33 (xy gate) + dih34 (eps 0.1) both likely caught ~nothing = why "same".
1. eps = g_tbCatchEps again (def 1.0); fluke guard now `support < 0.5f` on the pin instead of vz tightness (mid-arc junk pins are FAR from wall; real seam pins are flush).
2. **TEMP diag log** per wall-tick: `TB: vz= mindz= minsup= pin= sims= eps=` → C:\dih\debug.log. Read after user test: mindz >> eps every tick = sim never reaches pin vz; minsup never < 0.2 = never flush (approach/detect issue). REMOVE once fixed.
Next session: get user log FIRST, then tune from data — no more blind sweeps.

## dih34 (2026-07-12 pt4, UNTESTED) — TB pin back to base VERBATIM ("doesnt stick" fix)

dih33 TB missed real surfs. Two self-inflicted deviations from message.txt:
1. **pt3 xy gate** `predicted_xy >= live_xy*0.5` — catch tick clips the into-wall velocity component, xy legitimately halves+ on fast approach → gate rejected true catches. DELETED.
2. **eps** — knob g_tbCatchEps default 1.0 vs base 0.1: 10× band → mid-arc fluke pins (vz steps 12/tick through ±6) committed junk. Now **hardcoded 0.1** verbatim; g_tbCatchEps NO LONGER drives TB pin (knob dead for TB — delete or rewire later).
3. +half accept restored, fwd list base verbatim `{1, 0.875, 0.5, 1, 1.3856, 2.125, 2.875, 5, 15, 30}` (dup incl).
TB/AS split survives: AS keeps stuck-only gate (xy ≤ max(live*0.25, 10)) + into-wall offsets — TB may still seam-freeze occasionally on +half commits (base behavior, user chose exactness). Duck pass + steer-away handback + SimsLeft pool kept.

## dih33 (2026-07-12 pt3, UNTESTED) — TB/AS split + free strafe-away

dih32 feedback: "TB and AS do the same thing / both do both / stuck at random vertical seams". Cause = SAME pin (vz ±half) in both — surf vs stuck differ by XY velocity, neither checked it:
1. **TB pin = SURF only**: `vz ≈ -half && predicted_xy >= live_xy * 0.5` — +half accept dropped (base has it, but it commits "landed/stuck" states → seam freezes + AS behavior).
2. **AS pin = STUCK only**: `(vz ≈ ±half) && predicted_xy <= max(live_xy*0.25, 10)` — slide must die. Tangent offsets on vertical walls REMOVED (that made AS a 2nd TB); into-wall {0,25,50,70,88} on every wall. fwd 30 back in list (speed loss irrelevant at full stop).
3. **Strafe-away free** (user order, auto align pattern): TB/WC/AS early-return via `TB_SteeringAwayFromWall(pCmd, primary.plane.normal, fwd, side)` right after wall detect — wish pointing off wall (dot > 0.3) hands cmd back same tick. AutoAlign already did this at movement.cpp:1969.
If AS won't stick on flat vertical brush: wallbug needs a seam/lip — sim only finds what exists; creep keeps flush meanwhile. If TB misses catches after xy gate: loosen 0.5 → 0.25 first.

## dih32 (2026-07-12 pt2, UNTESTED) — SECOND full remake, faithful base port. dih27–31 = HISTORY

User re-ordered "remake COMPLETELY, use provided code as reference" (same message.txt). ALL v2–v4 machinery DELETED from TB/WC/AS: ride fast path, coast, retry pool, dual-sign sweep, mid-ride fwd cap, +half fresh-catch gate, WC/AS caches, per-feature budget caps, TB early-yield to HS. Each feature = plain base loop now: detect → sweep fwd×offset first-pin-wins → commit / creep(support<0.2) / restore.

Kept off-base (only): silent move via CorrectMovement; SimsLeft() pool gate; fwd LOW first, 30 dropped (creator advice — direct quote in TB comment); duck 2nd pass on g_tbAutoCrouch/g_asAutoCrouch; eps knobs (g_tbCatchEps, g_airStuckCatchEps; base hardcodes 0.1); ownership flags. Base sign restored verbatim (`relative_yaw >= 0 ? -1 : 1`). +half pin accepted always at full eps (base behavior).

- **TB**: detect 0.35/g_tbReach; sweep 10 fwd × yaw 85.999–89.999 step 1 × duck passes; pin ±half ≤ g_tbCatchEps; creep < 0.2; worst ≈ 100 sims (duck on 200, pool-capped).
- **WC** (displacement climb): detect **0.65** (was 0.35 — missed bumpy sub-standable faces); win = FL_ONGROUND after 1 sim; offsets 0–40 step 5 into wall; NEW support creep < 0.2 hugs face; yields to TB creep (s_tbOwnsCmd) too. WC creep does NOT set any flag — AS may stomp it (both-on = user choice).
- **AS**: detect 0.65; slanted (nz>0.15) into-wall {0,25,50,70,88} else tangent {85.999–89.999}; pin ±half ≤ g_airStuckCatchEps; no budget floor anymore (runs last, takes pool rest).
- HS locked-replay overrides a TB commit by itself (checks live vz, resets s_textureBugHit) — dropped TB early-yield relies on this.
- If dih31 symptoms return (mid-surf stop on fast rides): dih31 physics section below still valid — against-slide fwd 15 pin brakes 2.25 u/tick. First fix candidate = mid-ride fwd cap, NOT more machinery.
- Backup pre-remake: `src/features/backups/movement.cpp.bak_*_remake2` (= dih31 code). Build stamp + CMake bumped dih32.

## FULL REWRITE 2026-07-12 (dih27, UNTESTED) — everything below = HISTORY, code gone

TB + WC + AS rebuilt from creator's texturebug base (message.txt). Old ~1800 lines (ride hold, coast, duck-ext, creep lock, natural pass, durability, look-ahead) DELETED. HeadSurf untouched.

- Creator: "you need lower forwardmove values so it doesnt have 1 tick speed loss" → fwd lists sweep LOW first, first pin wins: TB/AS `{0.125, 0.25, 0.5, 0.875, 1, 1.3856, 2.125, 2.875, 5, 15}`, WC `{0.25 .. 15}`. 30 dropped.
- New shared helpers: `MV_DetectWallLines` (per-Z line scan, 16 dirs, hull prefilter + short-wall rescue; z step = g_tbScanStep) + `MV_SupportDistance` (fraction*32 − half-extent projection, call on predicted state). Old `TB_DetectWall`/`TB_SweepDirs16`/wall cache (`TB_RefreshWallCache`) DELETED.
- **TB**: detect (maxNormalZ 0.35, extra reach = g_tbReach) → sweep fwd × yaw 85.999–90 step 1 off wall, sign from velocity, silent via CorrectMovement → pin = −half ±g_tbCatchEps OR +half ±min(eps,0.35). Miss: best support < 0.2 → creep (s_tbOwnsCmd). g_tbAutoCrouch = 2nd duck pass. Yields to established HS. ~50–100 sims worst.
- **WC**: same base, win = FL_ONGROUND after 1 sim; offsets 0–45 step 5 into wall; cached-config fast path; budget min(SimsLeft, 64).
- **AS**: maxNormalZ 0.65; slanted (nz>0.15) offsets into wall {0,25,50,70,88} else tangent {85.999–89.999}; pin ±g_airStuckCatchEps; support<0.2 creep; cached fast path; budget clamp(SimsLeft, 40, 160). Old drive-in drift/look-ahead gone (g_airStuckReach now unused).
- Backup: `src/features/backups/movement.cpp.bak_*_msgtxt_rewrite`. If sticks drop at brush seams → old ride-hold/coast lived in backup.

### dih28 follow-up (same day, UNTESTED)
- Dead knobs DELETED everywhere (menu/config/globals/dllmain): g_tbHoldTicks, g_tbEdgeStop, g_airStuckReach.
- **"TB stops even with edge stop off" fix**: creep fallback tracked best_support across ALL sweep variants — wish tangent points AGAINST slide (sign), so fwd 15 flush at surf end braked to full stop every no-pin tick. Creep candidates now `forward <= 1.f` only (micro moves). If band-entry catches regress on fast approach → that gate is first suspect.
- g_asAutoCrouch wired back into AS: 2nd duck pass in sweep (rewrite had dropped it, knob was dead); cached fast path replays duck state (`cached_duck`).

### dih29 (same day, UNTESTED) — "catches less" + "STILL stops mid surf"
- **Mid-surf stop root**: base-code `sign = (relative_yaw >= 0) ? -1 : 1` put wish tangent AGAINST slide — every pinned/creep tick braked tangent speed until stop. FLIPPED to `? 1 : -1` = ride WITH velocity. (dih28 micro-creep cap treated symptom only.)
- **More catches** (old-code machinery restored into try_variant lambda):
  1. gap-aware 2-tick retry — falling near-miss with `support <= 0.03125 + (v_in + 30)*dt` gets 1 extra sim; band crossed mid-tick pins one tick late (fast-fall catches live here)
  2. creep gate 0.2 → 1.5 (old invariant #1: near-wall creep = multi-tick band setup; best_support init 1.5 too)
  3. outward wiggle pass 90.5–93.5° × fwd {0.125, 0.5, 1} when best_support < 0.5 — band re-entry from clamped-at-eps; allow_creep=false (outward move must not commit as creep)
  4. angle step 1.0 → 0.5 (9 angles/fwd)
- Worst tick ≈ 90 stand + retries + 90 duck + 12 wiggle, k_cmdSimCap 192 still caps. If lag near walls: step back to 1.0 first.
- If mid-surf stop persists: next suspect = +half pin accept (±0.35) committing "landed on top" states → ground friction; try dropping +half accept during established rides.

### dih30 (same day, UNTESTED) — dih29 "completely broken, near wall lags + does nothing"
dih29 breakers: retry gate `support <= 0.03125 + (v_in+30)*dt` unbounded — flying at wall v_in 100s → gate huge → retry on ~every variant → 2× sims → 192 cap exhausted → truncated sweep, no commits, lag. Plus 0.5° step (90+90 sims) + wiggle + creep 1.5 eating input while nothing caught.

TB v3 = faithful message.txt sweep (step 1.0°, creep gate 0.2, no wiggle) + only:
1. fwd LOW first (creator advice, kept)
2. sign flipped +1 (ride WITH slide, kept — mid-surf stop fix)
3. **ride fast path** (NEW): `last_valid/last_fwd/last_angle/last_duck` replayed first when `caught_last_tick` → 1-2 sims/surf-tick instead of full sweep; the missing piece behind near-wall lag
4. retry pool: cap 8/tick, gate `support <= 0.1f` absolute, falling only
Worst tick ≈ 50 stand + 50 duck + 8 retry. If still weak catches: raise retry gate 0.1 → 0.25 first, NOT the sweep density.

### dih31 TB v4 (same day, UNTESTED) — "still breaks mid surf, REMAKE THIS ENTIRELY"
**Air-accel model finally understood** (`addspeed = wishspeed − vel·wishdir`, accel = 10·dt·wishspeed):
- with-slide wish (dih30 sign) on fast ride → addspeed ≤ 0 → NO-OP → ride can't steer → drifts out of band → breaks mid surf (but catches on approach, where vel·wishdir ≈ 0)
- against-slide wish (dih28 sign) steers always but bleeds tangent 10·dt·fwd — fwd 15 pin = 2.25 u/tick ≈ 150 u/s² → rides ground to stop

v4 fixes ALL prior symptoms from this model:
1. both signs swept, against-slide FIRST (`sign_order = {-side_with, side_with}`)
2. **mid-ride fwd capped at 1** (`fwd_ride[]` 0.125–1) — brake ≤ 0.15/tick, never stops; cold catch keeps full 10-list
3. +half pin fresh-catch only (`pin_eps_pos = -1` when caught_last_tick) — mid-ride "landed on top" commit stood player up = the other stop
4. 2-tick coast (`coast_left`, armed on every win): replays `last_world_yaw/last_fwd/last_duck` through BOTH wall-scan loss AND sweep miss — brush seams
5. duck held mid-ride (`use_duck = last_duck`), flips fresh-catch only
6. ride fast path + capped retry pool (8, support ≤ 0.1, falling) kept from v3
Sims: ride slip tick worst ≈ 1 + 5×2×5 = 51; cold ≈ 100 stand + 100 duck (SimsLeft cap). Suspects if regress: coast 2 too short (seams) → 4; ride fwd cap starves re-pin on bouncy walls → allow 1.3856.

## State: WORKING (user confirmed dih6). dih7/dih8 = LAGGIER (duck near-miss gate + retry fired on every creep variant ≈ 250 sims/tick). dih9 = desktop-model rewrite — UNTESTED.

## Shared-pool + HS takeover pass 2026-07-08 (UNTESTED) — stick more / instant headsurf / lag / small ledges

Root lag = STACKED features, not TB alone: worst tick TB ~90 + HeadSurf 160 (ran even falling away from ceiling = pin impossible) + WallClimb 64 + AirStuck 160 ≈ 470 Pred_Simulate.

1. **Shared per-cmd sim pool** — `s_simsThisCmd` counts in `Pred_Simulate`, reset per cmd in Movement_RunImpl; `SimsLeft()`, cap `k_cmdSimCap = 192`. TB sweep loops gate on `SimsLeft() > 0`; HS budget = min(160, SimsLeft()) (60 when TB-owned); WC = min(64, SimsLeft()); AS = min(160, SimsLeft()). Call order = priority.
2. **HS physics gate** — `!s_hsSurfingNow && flVzNow <= 2*flHalfGrav` (≤ −12) → return, no sweep (falling away can never pin; slip tick −6 still passes → re-catch intact). THE big HS lag cut.
3. **HS takeover of TB stuck** ("stuck → headsurf instantly") — HS no longer early-returns on `s_textureBugHit`; `bTbOwned` flag: ceiling in reach + window open → capped 60-sim attempt; Commit/locked-replay set `s_textureBugHit = false` (HS owns tick); full miss restores originals (= TB's committed move, wall surf untouched); natural pass skipped (pCmd holds TB move); ceiling miss / vz gate → return with TB move.
4. **TB yields to established HS** — early in TB: `s_headSurfHit` (still last tick's value, HS runs after TB) + live vz ≈ +halfgrav → reset_duck_extension + return. No steal-back of a ceiling ride into a wall stop.
5. **Stick** — coast gate ride_ticks 3→2, fast-path nudges 3→2; HS coast gates 3→2 (both sites).
6. **Small ledges** — look-ahead probe distance cycle {12, 26, 45}u (command_number % 3; 12u catches tiny ledges 20|45 steered past); pass 4.5 duck probe widened live_gap<0.75 pidoras<0.5; wiggle pass gets delayed-pin retry when falling; HS cold reach cap 20→24.

Backup: `backups/movement.cpp.bak_*_tbperf3`. If catches regress: suspect (a) k_cmdSimCap starving rising duck pass — raise 192→256; (b) HS vz gate eating deep-slip re-catches — widen 2*halfgrav→3*. If TB/HS fight: check takeover clear of s_textureBugHit + TB early-yield eps (g_tbCatchEps).

## Duck-flap + slanted/instant pass 2026-07-08 pt2 (UNTESTED, dih1) — "random crouch/uncrouch mid-surf", "AS/TB not instant on fast fall / slanted walls"

1. **TB duck lock** — `ride_duck` static, set in `arm_ride_hold` (every commit path passes through it). Mid-ride (`caught_last_tick`): live-stick tries hold current duck state (riding ducked ORs IN_DUCK into every try incl. user steer; riding stand never offers duck variant); pass 3 rising duck sweep gated `!caught_last_tick || ride_duck`; pass 4.5 falling duck probe gated `!caught_last_tick`; pass 5 duck-extension gated `ride_ticks == 0` (mid-ride plan_changed re-plans = the crouch spam). Duck flips = fresh catch only.
2. **HS duck order** — `firstDuck = (s_hsSurfTicks > 0 || s_hsLocked) ? s_hsLockedDuck : false`; sweep tries current duck state first (first pin wins) — fixed stand-then-duck order re-crouched on every re-lock.
3. **AS slanted walls** — `TB_DetectWall` new param `flMaxNormalZ` (default 0.10 = vertical, TB unchanged); AS passes **0.65** (slanted but not standable <0.7). Slanted stick = wallbug (press INTO wall → swept/unswept disagree → velocity zeroed → vz −half pin, sim detects it) so AS uses into-wall angle set `{0,25,50,70,88}` when `normal.z > 0.15`, tangent set `{88..89.9}` else.
4. **AS floor budget 40** — pool-starved tick can't blind AS on first wall contact (stick must land within 1-2 ticks of touch). Worst tick now ≤ 192+40.
5. **TB fast-fall** — retry pool 8→12 when vz < −400 (pin lands one tick late when band crossed mid-tick; retry pass IS the catch there).

Backup: `backups/movement.cpp.bak_*_duckflap`. If duck-extension rides regress (short brushes drop at ride end): loosen pass-5 gate `ride_ticks == 0` → `<= 2`. If slanted sticks too grabby: raise AS slanted gate 0.15 → 0.3 or drop 0.65 → 0.4.

## HS stick fix 2026-07-07 → dih13 (UNTESTED) — dih12 headsurfs stuck less than pre-session

Root causes (all dih12 additions):
1. **Natural pass preempted the lock** — user strafes during headsurfs, so on every slip tick natural won with raw user input, set `s_hsLocked=false` AND cleared the cache → tuned-config machinery bypassed. Fix: natural only when `s_hsSurfTicks == 0` (initial catch), never clears cache.
2. **Shallow fallback required budget headroom** — sustain probes (+2/coarse pin) exhausted the 80 budget → shallow commit skipped → handback → dropped surf (pre-session locked instantly there). Fix: silent move saved at record time (`sh_moveFwd/sh_moveSide`), committed budget-free.
3. `HS_SIM_BUDGET` 80 → 120.

## HS merge 2026-07-07 → dih12 (UNTESTED → HS stickiness regressed, see above)

HeadSurf built into TB: `hs_enabled = headsurf toggle || TB toggle` — TB bind alone now drives both wall surfs and headsurfs (standalone HS toggle still works). HS got the TB dih11 treatment:
1. **Durability** — coarse sweep hit must sustain 2 extra sim ticks (`SustainTicks`); shallow pins (`sh_*`) committed only when nothing durable. Refined hits skip the probe (adjacent to a durable coarse hit).
2. **Natural pass** — user's own cmd pins → keep user cmd, no lock/replay (manual play wins). Manual input only.
3. **Coast** — `s_hsSurfTicks >= 3 && s_hsCoastLeft > 0` (armed 3 on every hit): replays locked move through ceiling-detect loss AND full sweep miss — brush seams no longer drop established headsurfs.

## Durability fix 2026-07-07 → dih11 (UNTESTED) — dih10 regressed ("even less prone to surfs")

Root cause: raw first-pin-wins (dih9) committed **1-tick fluke pins**; dih10's coast + rescue then amplified it — coast replayed the dead fluke move for hold-ticks INSTEAD of creep (the band setup), natural pass committed hands-off fluke pins and poisoned fast path (stale fast_fwd). Fixes:
1. **Durability probe** — winner = pin + 2 sustain sims of same move (eval_variant). Shallow pins (sustain 0-1) remembered; adopted only if sweep finds no durable pin (before pass 5). +2 sims per pinned candidate only.
2. **ride_ticks gate** — static counter of consecutive hit ticks. Coast requires `ride_ticks >= 3`; fast-path yaw nudges (slots 2-5) too. Flukes fall back to sweep/creep immediately.
3. **Natural pass** gated to `user_has_manual_movement_input`, sets `fast_fwd = 0`.

## Stick + headsurf pass 2026-07-07 → dih10 (UNTESTED → REGRESSED, see above)

User: "stick to surfs more, do more headsurfs". Changes:
1. **TB ride rescue** — fast path now 6 slots: exact 1-tick, exact 2-tick (delayed pin), yaw ±0.5°, ±1°. Nudge win updates `fast_yaw`. Runs only on would-drop ride ticks.
2. **TB pin coast** — mid-ride sweep miss no longer drops to creep: `!winner && caught_last_tick && ride_hold_ticks_left > 0` → replay ride-hold move (+edge stop), pin re-lands in 1-2 ticks. Reuses `g_tbHoldTicks` pool (menu "ride hold ticks", raise for stickier).
3. **HeadSurf more catches** — cold ceiling reach vz-scaled `min(6 + max(vz,0)*dt*2, 14)` (fixed 6u saw ceiling too late on fast rise); `s_hsColdSkip` after full miss 4→1; cold skip zeroed on lock slip.

## Desktop-model rewrite 2026-07-07 → dih9 (reference: `C:\Users\huber\Desktop\texturebug.cpp`)

Sweep rebuilt on desktop TB philosophy (first-hit-wins, cheap variants):
1. **Natural-hit pass** — 1 sim of user's raw cmd before any sweep; pin → commit user cmd + ride hold, return. Gated `!caught_last_tick`.
2. **First pin wins** — sweep stops at first pinned variant. Streak ranking + 8-tick per-variant lookahead REMOVED (that loop was the lag core). Ride depth = fast path + ride hold.
3. **Pass 2 retry** — 2-tick delayed-pin retest only for the 8 closest misses (by post-gap), only falling.
4. **Duck out of the sweep** — pass 3: full duck sweep only when rising (headbounce). Pass 5: ONE duck probe of the stand winner → duck extension when it also pins within gap tolerance (desktop texturebug_v2 probe style). dih7 "falling duck-only accept" DROPPED — it caused the regression.
5. `simulate_antoha_variant(..., bool allow_retry)`, streak out is 0/1. `eval_variant(yaw, mul, force_duck, allow_retry) -> bool win`.

Sim budget: creep tick worst ≈ 93 (was ~250), typical ≈ 55, riding ≈ 2, rising adds ≤47. Backup: `backups/movement.cpp.bak_*_tbdesktop`. If catches regress: suspect first-win ordering (small muls first) or pass-2 pool size 8.

## Perf rewrite 2026-07-07 (lag + missed-easy-TB complaint)

Root cause of lag = sim count, not traces: failing near-wall tick ran ~240 Pred_Simulate (80 variants × 2 blind retry + 40 wiggle × 2). Changes:

1. **Variant dedup** — (mul × ang) grid collapsed by effective wish vs wall: inward = mul·cos(ang), along = mul·sin(ang); buckets 0.03 u/s inward / 0.25 u/s along, built once into static tables. Main 80→47, wiggle 40→28. Low-mul rows were physical duplicates (inward spread < 1% of ε band per tick).
2. **Gap-aware retry** — delayed-pin 2nd sim only when `out_pidoras <= 0.03125 + (v_in_post + 30)*dt` (band reachable next tick). Was unconditional on every falling variant.
3. **Duck falling catches** (catch-rate fix, not perf): duck pass now also on falling near-miss (`stand_evaluated && stand_pidoras < 0.125 && live_gap < 0.5`); selection accepts duck-only pin while falling (`vz < -1`), was headbounce-only. Duck hull 62 vs 82 eases eq(10) — the marginal easy TBs. `simulate_antoha_variant` now returns bool (false = speed<1 reject).
4. **Probes** — look-ahead: one probe per tick (5-tick even cmds / 10-tick odd, `command_number & 1`) in new `hull_only` detect_wall mode (skips per-Z lines, accepts hull normal). Was 2 full scans/tick.

Expected: ~3–5× fewer sims near wall; approach ticks (gap 0.5–1.9) drop from ~160 to ~47 sims. Backup: `backups/movement.cpp.bak_*_tbperf2`. If catches regress: first suspect = dedup buckets (widen 0.03/0.25 → smaller), second = retry gate.

Feature: `TextureBug()` in `src/hacks/movement/movement.cpp` (~line 2015). Origin: 1:1 port of ridobebra antoha_bug (`C:\Users\huber\Desktop\USELESSSRCCODE\ridobebra-main\hacks\movement\movement.cpp` ~2783) + local extras.

## Physics (source: `C:\Users\huber\Desktop\USELESSSRCCODE\PAPER\tf2_movement_mechanics.md` + Review.txt §9.4)

- TB = collide topside of box brush while passing its BOTTOM (box-brush side-pick oversight).
- Catch condition: `(H_brush + H_player)/ε < |vz|/|v⊥|`, ε=0.03125. Tall brush ~6720 ratio → v⊥ must be ~0.07-0.2 u/s at seam tick. Faster fall = EASIER.
- s⊥ (dist to wall) must be strictly < ε at tick start. Wall collision clamps to exactly ε = NOT catchable. Band entry = tiny inward step (trace END inside ε w/o crossing plane = no collision). Big step crosses plane → collision → clamped back to ε.
- Pixelsurf sustain: sz ∈ (ε(1−|Δz|/|Δ⊥|), ε); smaller inward speed = wider window.
- Air-accel wish caps at 30 u/s (0.45u/tick) — but EXISTING velocity into wall is uncapped.

## How the code works

1. `detect_wall` — 16-dir scan, per-Z line traces (step `g_tbScanStep`) + hull prefilter; hull-trace fallback when line grid misses (short walls between lines).
2. Catch = `Pred_Simulate` variant sweep; pin = predicted vz == −halfgrav (`IsTargetVz`, eps `g_tbCatchEps`). Variants: 10 fwdmove {0.125..30} × yaw 85.999–90° step 0.5 off wall normal × stand/duck. Outward wiggle fallback (90.5–93.5°) when no pin && live_gap < 0.5.
3. Order per tick: duck-ext replay → fast path (retest last winning move) → sweep → manual-duck / duck-ext plan → far-input handback → drive → final apply (creep) + ride-hold arm.
4. `get_pidoras()`/live_gap = distance hull→wall (fraction*32 − support projection).

## CRITICAL invariants (violating these broke it this session)

1. **Near-wall creep must run even when user gives WASD input.** No-catch final apply (best-variant tangent micro-move) IS the multi-tick catch setup: keeps player inside ε band with tiny v⊥ so the seam tick can pin. Session bug: input-restore gate returned user cmd on all no-catch ticks → zero catches ever (log showed flush gap −0.016, viable sweep, streak=0 forever). Fix shipped: handback only when `live_gap > 1.5f`.
2. **Sweep gap gate must be velocity-aware.** `sweep_viable = live_gap <= 1.0 + (v_toward_wall + 30) * 2 * TickInterval()`. Fixed 1.0 gate broke all fly-at-wall catches (strafe-in velocity uncapped, 100s u/s; only accel wish caps at 30).
3. Aggressive steering (look-ahead probe, drive-at-wall) gated by `user_has_manual_movement_input` — user complaint: "tries very hard to stick to wall, messes up movement". Do NOT re-enable while strafing.

## Session changes (all in TextureBug / detect_wall)

- detect_wall hull-fallback → catches very short walls line grid misses.
- Look-ahead probe (5/10 ticks along ballistic path, z −= 400t²) → steer 450 toward upcoming wall; hands-off only.
- Drive threshold 2.0 → 0.25 gap; hands-off only.
- Outward wiggle sweep (band re-entry from clamped-at-ε).
- Perf: velocity-aware sweep gate (above); duck pass only when `stand_hit_streak > 0 || vLiveVelocity.z > 0` (falling+stand-miss duck can't help — headbounce needs rising); streak ratchet `out_hit_streak > best_hit_streak + 1 → break`; early sweep abort at streak 9; streak lookahead 6→8.
- Input handback (>1.5u gap only).

## Known quirks / leftovers

- Streak ratchet caps duck streaks same as stand → `duck_hit_streak > stand_hit_streak` (duck-extension trigger) rarely true now; duck-extension near-dead. Nobody complained; revisit if duck rides regress.
- `viable=0 gap=8.000` ticks = support trace missed wall (fraction 1 → 32−24). Harmless, sweep useless there anyway.
- Build: `cmake --build build --config Release` from `cplus\`; bump `OUTPUT_NAME "dihN"` in CMakeLists.txt each reinject (old DLL locked by game). Currently dih6.
- Backups: `src/features/backups/movement.cpp.bak_*_tbsurf` (pre-session), `*_tbperf` (post-perf).
- Debug method that cracked it: TbLog per-tick line (gap, v_toward_wall, vz, viable, streak, input) → `C:\infovisual\tb_debug.log`. Removed; re-add same pattern if TB breaks again.

## Memory

`project_texturebug.md` in memory dir mirrors this. `MEMORY.md` indexed.
