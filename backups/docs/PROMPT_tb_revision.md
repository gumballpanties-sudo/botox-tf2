# TB handoff — dih70: ramp hands-off (coldtires 119th ported). Next: full revision vs coldtires

## READ FIRST, IN THIS ORDER
1. `C:\dih\debug.log` — TB lines. Never guess ahead of the log.
2. `C:\Users\huber\Desktop\coldtires-main\csgo-sdk\hacks\movement\texturebug.cpp` (3559 lines) —
   THE reference. Grep it, never read whole.
3. `C:\Users\huber\Desktop\coldtires-main\csgo-sdk\HANDOFF_tb_76th.md` (128 lines) — the rules
   below come from it, read it once in full, it is short.
4. `C:\Users\huber\Desktop\Aletherium-main\Amalgam\src\Features\Misc\Misc.cpp` — second reference
   (TF2, our actual game; coldtires is CSGO). TB starts ~line 2246.

Our file: `src/hacks/movement/movement.cpp`, `static void TextureBug(void* pLocal, UserCmd_t* pCmd)`.

## THE ONE SENTENCE
coldtires is many revisions ahead of this port (it cites `dih55` inside itself at
texturebug.cpp:880). Every open item below is already written, tested and commented THERE.
The job is to port them one at a time, not to invent.

## LAWS (falsified the hard way — do not retry)
- **STALE BASE IS LOAD-BEARING.** `RestoreToFrame(nBase - 1)`. coldtires' 77th made the sim base
  live, the probe went green (340/376 healthy) and **no surf worked at all** — every pin became a
  wallbug. "Probe-green != surfs ride." Never touch the restore base.
- **STEEP ANGLES ARE THE RIDE KILLER.** 10-70 deg off the wall normal eat xy mid-ride. The sweep
  grid stays 86-90 grazing. (Exception: the PRESS ladder, item 2 — different quantity, see there.)
- **EVERY GRID WIDENING SO FAR DEGRADED RIDING** except the tiny forwards (item 1).
- **ONE CHANGE PER TEST.** User builds and plays. Two changes = an unreadable result.
- Backup before touching movement.cpp:
  `cp src/hacks/movement/movement.cpp src/hacks/movement/movement.cpp.bak_$(date +%Y%m%d_%H%M%S)_pre_<what>`

## WHAT THIS SESSION DID (dih67-70), newest last
Each was log-driven; the first three are real fixes that are now mostly moot ON RAMPS (dih70
returns before them) but still live on vertical seams.

- **dih67 support measurement.** `MV_SupportDistance` returned `fraction*32 - half`; the 32u probe
  missed constantly and TF2's hull half is 24, so it returned **8.000 exactly** — a sentinel, on
  every tick of every ride. 8.000 is above every support gate, so the creep could never fire.
  Now falls back to plane math (origin -> plane distance minus hull half). Diag `supf=`.
- **dih68 ride hold** (Aletherium `arm_ride_hold`, `TextureBugHoldTicks` default 6). Every hit
  stores the winning move; a scan miss replays it instead of `drop_ride()` (Misc.cpp:2486), and a
  mid-ride sweep miss coasts on it (Misc.cpp:3081). Diag `hold=`, `ridet=`, `pbr=7`/`pbr=8`.
- **dih69 sustain ranker** (Aletherium Misc.cpp:2955, `k_streak_depth`/`k_early_accept` = 12).
  A pinning variant is simmed forward 12 ticks; the streak it keeps re-clipping the seam is the
  rank. Ours used to commit the FIRST pin, so 1-tick pins won and the hull walked down the wall in
  a staircase. Diag `sus=` (committed), `susb=` (best seen).
- **dih70 RAMP = HANDS OFF** (coldtires 119th, texturebug.cpp:419). `|plane.normal.z| > tbRampNz`
  (def 0.05) -> `drop_ride()` and return before anything writes to the cmd. Knob: menu slider
  "ramp nz (hands off)", cfg `tbRampNz`. Diag `TB: ramp hands-off nz= band= ticks=` (1/sec).

**Why dih70 is the headline:** on a ramp, contact clips vz to the along-plane part, so vz crosses
-halfgrav constantly and the sweep reads a clean pin nearly every tick. TB commits it, takes the
cmd, and on a ramp the strafe IS the speed — an 85%-keep pin still bleeds 15% a tick with the
player locked out. Log proof: ride ticks at `pin=1 sus=12`, `vz` frozen `-18.0`, `xy` decaying
`267.70 -> 265.62`. coldtires' 119th comment quotes this user: "tb stops your horizontal velocity
if u do a surf".

## STATUS / WHAT TO CONFIRM FIRST
dih70 is built but **unverified in game**. Before any new work:
1. Surf a ramp. TB must not touch it — expect `TB: ramp hands-off` lines and the surf to feel
   exactly like TB off.
2. Then a vertical texturebug seam: does it still catch and ride? (dih67-69 live there.)
3. If it now MISSES catches on slightly tilted brush walls, that is the 120th's exact finding —
   raise the "ramp nz" slider toward 0.35 (the scan admits brush faces to 0.35) and re-test. Do
   not delete the gate.

## OPEN ITEMS — ranked, all already written in coldtires
### 1. Tiny forwards (cheapest, lowest risk)
`forward_values` in coldtires (texturebug.cpp:739) is 13 entries:
`{0.001, 0.01, 0.05, 0.25, 1, 0.875, 0.5, 1.3856, 2.125, 2.875, 5, 15, 30}`.
Ours is the last 9 — the tiny head is missing. Their 78th isolated exactly this: "the tiny fwds
did the catching", ride-safe because they barely push. Grid cost 13 x 5 = 65 sims, our cap is 192.

### 2. PRESS ladder — the real ride hold (biggest win, do after 1)
coldtires texturebug.cpp:1248-1284, commit branch 1535 (`act='P'`).
The measured reason our hold does nothing: `addspeed = min(wishspeed,30) - vel.wishdir`. A ride's
velocity is TANGENT, so an 86-90 deg graze keeps addspeed positive only while `|v|*sin(off) < 30`
— at 216 u/s that is `off < 8 deg`. **Our creep and drive-in are no-ops at ride speed**, which is
why xy sits frozen to 2 decimals across whole rides in every log this session. Their press uses a
separate near-normal ladder
`{0.001, 2, 5, 8, 10.999, 20.999, ..., 89.999}` x `forward_values` x both signs, picks the move
whose `v_in` is closest to `press_target`, and commits it on flush-no-pin ticks.
- `press_target = min(max(v_target*5, 0.001), min(band_max, 0.05))` (texturebug.cpp:1103)
- `v_target = 0.03125 * |vz| / s_z` (s_z = brush top over origin), `band_max` = PAPER ceiling at
  texturebug.cpp:1093. Their 116th note: pins ride at `pinv = 0.0033` with `v_target = 0.0006`,
  so aim a few multiples of v_target, NOT band_max (1.0 made the press a drive that glued).
- Our pass 3 "exact solve" already computes this same v_in quantity — but it is gated
  `!ride_had_clean_pin`, so it never runs on a ride. Read it before writing anything new.
- NOTE: press is `!ramp_plane` in coldtires. With dih70 the ramp never reaches here anyway.

### 3. Escape / peel instead of handing back (texturebug.cpp:1286-1316, 1505-1534)
On a wallbug tick ours hands the raw cmd back. Their measurement (log 19:16): that is what EATS
THE SPEED — the origin sits inside the 0.03125 extension, the engine's stuck re-test zeroes
velocity, `147 -> 18 -> 4 -> 2` in four ticks. Peel OUTWARD with the speed on instead:
`esc_angles = {90.001, 92, 95, 100, 110, 125, 145, 170}`, pick smallest peel that clears, keep the
ride armed (lock + duck latch + clean-pin flag), cap `k_esc_run_max = 3`.

### 4. Park capped to a low-speed bridge (texturebug.cpp:1471)
Ours parks whenever the ride ever pinned cleanly. Theirs adds `xy_base <= k_park_xy_max (30)` and
`s_park_run < k_park_run_max (2)` and `!ramp_plane`. Their note: parking while the ride still
carries speed trades a 240 u/s surf for a 0 u/s hang, and then every tick after parks again.

### 5. Smaller items
- `k_ride_grace` — theirs 6, ours `k_tbRideGrace = 12`.
- SMASH guard (texturebug.cpp:1318-1348): chain a 2nd sim on the committed pin; if xy does not
  survive, leave on the escape move. Armed only for the first `k_ride_settled = 3` ticks.
- Merged scan pass (their 80th): `mv_detect_wall_lines` answers locked + fallback in ONE sweep and
  skips provably-impossible directions (`N.dir <= 0.04`). ~4x fewer traces. Ours still runs the
  locked pass then a separate unlocked retry. Pure perf, behaviour-identical — do it only if the
  user reports lag on seam-heavy geometry.

## CONFIG — ours vs the references (user's `C:\dih\default.cfg`)
| knob | ours | coldtires / Aletherium |
|---|---|---|
| `tbReach` | 1.0 | Aletherium `TextureBugReach` 12 |
| `tbScanStep` | 8.0 | Aletherium `TextureBugScanStep` 2 |
| `tbRampNz` | 0.05 (new) | coldtires `ramp_nz_band()` 0.05 |
| catch eps | hardcoded 0.1 | coldtires 0.1 (same), Aletherium 1.0 |
Reach 1 means the wall must be within 1 unit or the scan reports nothing — that IS `why=noscan`.
Scan step 8 samples the 82u hull ~10 times, so thin seam lips fall between rays. These are the
user's knobs: recommend, do not silently change.

## DEBUG LINE FIELDS (TB:)
`vz mindz alivedz minsup pin sims eps ride duck lock rej xy stuck clean warm dup solve mir pinxy
vin steer resc drv grace pbr hull plant canun neardz supf hold ridet sus susb`
- `pbr` = branch that held the PREVIOUS tick: 0 handback, 1 clean pin, 2 freshgrab handback,
  3 wallbug park, 4 creep, 5 hands-off drive, 6 ride drive-in, 7 noscan hold, 8 pin coast.
- `minsup` 8.000 exactly = the dead sentinel (fixed in dih67; if it comes back, the plane math
  fallback broke). `mindz` = `|vz| - 6` exactly = no variant collided at all, pure gravity.
- Drop lines: `TB DROP: why=noscan|steer|freshgrab`, `TB HANDBACK:`, `TB HOLD: why=noscan`.

## BACKUPS FROM THIS SESSION (src/features/)
`movement.cpp.bak_20260824_235326_pre_plantgate`,
`..._pre_ridehold`, `..._pre_sustain` (timestamps in the filenames).

## RULES FROM CLAUDE.md THAT BIT THIS SESSION
- Read `C:\dih\debug.log` FIRST when debugging. Every real finding here came from it; every wrong
  turn came from reasoning ahead of it.
- Never read big files whole — grep. Never load `sdk_facts.md` / `amalgam_facts.md`.
- `C:\Users\huber\Documents\source-engine-2018-hl2_src-master\` is FORBIDDEN (older than sdk-2013).
- Caveman speech in replies and code comments.
