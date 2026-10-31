# Fix texturebug rides: TB catches but falls off (dih146)

Paste everything below into a fresh Claude Code session started in `C:\Users\huber\Desktop\SimpleVisual\cplus`.

---

## Task

TextureBug (`src/hacks/movement/texturebug.inl`, `TextureBug()` ~line 1050) catches walls but does not RIDE them. The user reports this used to work long ago, especially with the reference loop in `C:\Users\huber\Desktop\texturebugALLSOURCES\les\message.txt` (398 lines, read it whole). Make TB hold rides again. The user does not code; you write everything, build, and hand them a DLL to test. Every build is one change, gets a new `MOVEMENT BUILD = dihNNN ...` stamp (movement.cpp ~line 4967), and is judged from `C:\dih\debug.log`.

## Read first (in this order)

1. `CLAUDE.md` (project) for paths, build, rules. Backup before edits: `cp <file> <file>.bak_$(date +%Y%m%d_%H%M%S)`.
2. `C:\Users\huber\Desktop\texturebugALLSOURCES\les\message.txt` = the known-good base.
3. `HANDOFF_texturebug.md`, top to the dih36 entry. dih36 log was "CONFIRMED GOOD (3440 pins / 6859 TB ticks)". dih42 was "STAYING = message.txt verbatim".
4. Old code from the era that rode: `C:\Users\huber\Desktop\SimpleVisual\old_backups\movement.cpp.bak_20260713_*_dih37_instant`, `*_dih38_pre`, `*_dih39_pre` (TB lived in movement.cpp then). Grep, don't read whole.
5. `C:\Users\huber\Desktop\texturebugALLSOURCES\PAPER\tf2_movement_mechanics.md` lines 455-590 (collision, pixelsurf, texturebug physics).

## What the last 4 logs proved (dih142-dih146, same wall, 2026-09-16)

Log line format: `TB: act=X vz= ... pin= sims= warm= ride= rt= ... dface= duse= ...`. Acts: B = committed pin, P = analytic press on the tick after a pin, Q = analytic press on a plateau (v_in = 0), C = sim-picked creep, H = hand back.

Every ride looks like this, in all 4 builds:

```
Q-251 Q-263 B-275 B-287 P-6 Q-18 Q-30 ... Q-174 B-186 -> wall=0 (fell)
best case: B-54 B-66 B-6 B-18 P-6 B-18 P-6 Q-18 Q-30 ...
```

- Pins land (engine vz becomes -6), but the tick after a landed pin (P, base vz = -6) NEVER finds a pin: 110-118 sims, all pure gravity (`mindz=12`).
- The first B of a pair is usually a 2-tick pin via the retry arm (`sims=3` = natural + warm + retry); the warm replay lands it next tick.
- Between pins, P/Q ticks commit an UNSIMULATED analytic press. On those ticks `vin=0.0000` and `gap=0.00002` never change. The hull free-falls 0.2-4u per tick.
- Pins only happen while `dface` is within ~1.5-6u under `duse` (hull near the face edge). Once `dface > duse` the catch window is gone; HS then sees a brush bottom overhead (`HS: ceil=1 nz=-1`) and `TB: wall=0`.

## Already tried, do not repeat

- dih142 "ULP pass" (wish straight into wall, one float step): measured `din=0 uvin=0` on ~150/150 stand sims. Deleted in dih146. Do not re-add.
- dih143: removed `!caught_last_tick` from the retry arm (texturebug.inl ~line 1699). Gave a few vz=-6 re-pins, no real change. Kept, fine to revert if it gets in the way.
- A "float precision ceiling" theory (the pin window at vz -6 is ~1 ULP wide, `u=1.53e-05`) was proposed and is NOT proven. Rides worked before on ordinary walls, so treat it as wrong until an A/B says otherwise.

## Leading hypothesis to test first

message.txt holds a ride with one rule: on EVERY no-pin tick, commit the SIMULATED variant with the smallest predicted support distance if it is `< 0.2` (the creep), else hand back. Its sweep is 10 forward values x angles 85.999..90 step 1, wish against the slide (`sign = relative_yaw >= 0 ? -1 : 1`), pin = predicted vz within 0.1 of -halfgrav OR +halfgrav, first pin wins.

Current TB on those same ticks commits `act=P/Q`, an analytic press ladder (dih131/132) that was never simulated. It outranks the creep (texturebug.inl commit ladder ~line 2652 press, ~2668 creep). The logs show that press holding `vin` at exactly 0, which per the paper collides with nothing. So the press, not physics, may be what stops the re-pin.

## Plan (one build per step, user tests each)

1. **Diff, don't guess.** Map what dih36-dih42 did on a no-pin ride tick vs what dih146 does. List every branch that can take a no-pin ride tick now (press, park, peel, brake, smash, pred approach) and when each was added (comments carry dihNN tags).
2. **A/B build:** on no-pin ride ticks, skip the P/Q press and commit the sim-picked creep (`best_hold_*`) exactly like message.txt (`support < 0.2`). Keep everything else. Stamp and log it. Success = P/Q gone, B on consecutive ticks, rides longer than ~6 ticks.
3. If step 2 is not enough: add a toggle (config + menu, follow existing TB knobs in globals.h/config.cpp/menu.cpp) that runs a message.txt-verbatim loop on top of the current primitives (`Pred_Simulate`, `RestoreToFrame`, `CorrectMovement`, `MV_DetectWallLines`, `MV_SupportDistance`), keeping ramp/displacement hands-off (~line 1221) and steer-away. The user can then flip between old and new on the same wall.
4. Only after rides hold: re-apply later additions one at a time if the user wants them.

## How to verify each build

- Build: `cmake --build build --config Release` -> `build/Release/dih.dll`. Confirm the stamp line in the log matches.
- Condense the log, never read it raw (lines are ~2KB each). This PowerShell worked:
  ```
  $l=Get-Content C:\dih\debug.log; ($l | ?{ $_ -match '^TB: act=(\w) vz=(\S+)' } | %{ if($_ -match 'act=(\w) vz=(\S+)'){ "$($matches[1])$([int][double]$matches[2])" } }) -join ' '
  ```
- Good ride: long runs of `B-6` / alternating B at -6..-18, `dface` staying put instead of climbing past `duse`.
- Watch cost: `us=` near 6000 or `cap=1` means the tick time budget binds.

## Rules

- Talk caveman (user's global CLAUDE.md). Short replies, exact file:line.
- No speculative physics claims shipped as facts; every claim ties to a log number.
- Never replace dllmain.cpp or movement.cpp whole; merge edits.
- End each turn telling the user what to test and which log fields to look at.
