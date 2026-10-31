# HANDOFF — movement + aim optimization pass (dih)

Audit found 8 items. Fixes land one at a time, each built green before the next.
Backups: `cplus/backups/src_baks/*.bak_20260922_091023`.

## Status

| # | Item | File | State |
|---|---|---|---|
| A5+A6 | dead doubletap record sort + 6 KB record memset | `aim_hitscan.cpp` | **DONE, builds** |
| A3+A2 | studiohdr refetch per hitbox + per-enemy bone rebuild | `aim_global.cpp` | **DONE, builds + gate test** |
| A4 | heap alloc in projectile sim inner loop | `aim_projectile.cpp` | **DONE, builds** |
| M1 | `MV_DetectWallLines` has no broadphase | `movement.cpp` | **DONE, builds + gate test** |
| A1 | `BONE_USED_BY_ANYTHING` forces full bone rebuild | 5 aim files | **DONE, builds** |
| A7 | melee `UpdateInfo` by-value vector + map per tick | `aim_melee.cpp` | deferred on purpose |

**All five fixes are in. Full build green, all 20 gate tests pass** (`cmake --build build
--config Release`, then run `build\Release\*_test.exe`) — including the two new ones,
`aimfov_test` (98342 checks) and `mvscan_test` (637882 checks).

**Nothing is in-game tested.** Every item below is behavior-identical by construction
(dead code, allocation shape, cached lookups) except A2's FOV reject and A1's bone mask,
which are called out where they land. **A1 is the one to actually test in game** — read its
section before playing with it.

---

## Fix 1 — A5 + A6 — DONE

`src/hacks/aim/aim_hitscan.cpp`, `CAimbotHitscan::CanHit`.

Three changes, all in the target setup at the top of the function:

1. **Deleted the doubletap record sort** (was ~line 262-275). It could never have done
   anything: `vRecords` is one element in this build (no tick history, see `aim_record.h`),
   and the comparator read `a->m_aBones` for BOTH sides, so it was `f(a) < f(a)` — false for
   every pair. `iTargetBone` was a static that only that sort read; its write site inside the
   hitbox loop (was ~line 433) went with it.
2. **`std::vector<TickRecord*> vRecords` → `TickRecord* const vRecords[1]`.** One heap
   allocation per target per command, for a one-element list. The `for (auto pRecord : vRecords)`
   loop body is untouched.
3. **`g_tLiveRecord = { ... }` → four field assigns.** `TickRecord` carries
   `matrix3x4 m_aBones[MAXSTUDIOBONES]` = 6144 bytes, so the aggregate built a zeroed temp and
   copy-assigned 6.2 KB per target per command, immediately before `SetupBones` overwrote it.

Cost removed: 1 malloc + ~12 KB of memory traffic per target per command.

---

## Fix 2 — A3 + A2 — DONE

`src/hacks/aim/aim_global.cpp`, `CAimbotGlobal::PlayerBoneInFOV`. Runs once per enemy per
command, from all three target gathers (`aim_hitscan.cpp:43`, `aim_projectile.cpp:37`,
`aim_melee.cpp:234`).

1. **Hitbox set resolved once (A3).** `GetNumOfHitboxes()` was the loop CONDITION and
   `GetHitboxCenter()` ran in the body; each re-walks `GetModel()` -> `GetStudiomodel()` ->
   `pHitboxSet()` -> `pHitbox()` (`defs.cpp:7-70`), two engine virtuals plus an MDL cache hit.
   ~18 hitboxes x ~7 lookups x every enemy ~= 2900 lookups per command on a full server. Now
   `pSet` is resolved above the loop and the body does the `VectorTransform` inline — byte
   identical to what `GetHitboxCenter(aBones, n)` computed with its default `vOffset = {}`.
2. **`aBones` is `static` (A2).** `matrix3x4[MAXSTUDIOBONES]` is 6144 bytes of stack per call.
   Single-threaded (CreateMove), consumed before return.
3. **Bounding-sphere FOV reject before `SetupBones` (A2).** New pure header
   `src/hacks/aim/aim_fovreject.h`, `aimfov::SphereOutsideFov`. Bounds the player by a
   sphere and asks whether the sphere's NEAREST edge is still outside the FOV cone.

**A2's reject is the one behavior change in this pass.** Two things make it safe:
a sphere cannot be rotated out from under the test the way a bbox can, and the radius is
padded before it is allowed to say no — `k_fovRejectScale = 1.5f`, `k_fovRejectPad = 16.f` in
`aim_global.cpp`, because hitboxes reach a little past the collision bbox on a taunt or with
arms out. Degenerate inputs (eye inside the sphere, zero distance, negative radius) fail OPEN.

Gate test: `tests/aimfov_test.cpp` (`cmake --build build --config Release --target aimfov_test
&& build\Release\aimfov_test.exe`). It brute-forces the one direction the reject may not be
wrong in: for 200k random eye/centre/radius/FOV/axis cases, every `true` is checked against 512
Fibonacci-sphere surface points and must have none inside the cone. **Result: ok, 98342 checks,
0 failed, 98294 rejects proven.**

Still untested in game. What to watch for: an enemy at the very edge of the FOV circle that the
aimbot now ignores. If that ever shows up, raise `k_fovRejectPad` first — it is the slack, and
raising it only costs bone rebuilds.

---

## Fix 3 — A4 — DONE

`src/hacks/aim/aim_projectile.cpp` + `aim_projectile.h`. Both copies of the projectile sim
loop: the weapon one (`CAimbotProjectile::RunProjectile`-side, ~line 1200) and the
`pProjectile` overload that mirrors it (~line 1885).

The loop runs to `iMaxTime` ticks per target (`MaxSimulationTime`, well over a hundred on an
arc). Four allocations per iteration became zero after warmup:

1. `std::vector<Point_t> vSplashPoints` — declared in the loop, now a function-level `static`
   with `.clear()` per iteration.
2. `std::vector<std::tuple<Point_t, int, int>> vPoints` — same.
3. `std::vector<Vec3> vProjLines` — same, and this was the worst of them: `TestAngle` grows it
   to ~`iSimTime` entries, so it reallocated its way up from nothing **per candidate point,
   per tick**, and the result is discarded unless the shot lands.
4. `GetSplashPoints` / `GetSplashPointsSimple` both returned `std::vector<Point_t>` by value
   and both built a `vPointDistances` vector inside. Signatures are now
   `void ...(..., std::vector<Point_t>& vPoints)` — the same out-param shape
   `SetupSplashPoints` next to them already used — and `vPointDistances` is a `static` + clear.

Why the statics are safe here: single-threaded (CreateMove), nothing holds a reference across
an iteration, the two functions' statics are separate objects from the callers', and
`vProjLines` is only ever READ after a `TestAngle` that returned true — which is the same call
that writes it (`*pProjectilePath = tProjInfo.m_vPath`, assignment not append, so a stale value
can never be read). The `goto skipSplash` no longer jumps over a declaration, since the
declarations moved out of the loop.

No behavior change: allocation shape only.

---

## Fix 4 — M1 — DONE

`src/hacks/movement/movement.cpp`, `MV_DetectWallLines`, plus new pure header
`src/hacks/movement/mv_scanbound.h`.

One zero-length hull trace at the top of the scan. Miss it and the whole 16-direction loop
returns false without tracing anything.

What it replaces: every direction opened with a hull trace before any cheap reject, so open air
cost **16 hull traces** to answer "no wall". TB's ladder (`texturebug.inl:1160-1198`) runs the
scan twice on a full miss = 32 per tick, every tick the bind is held. AutoAlign
(`movement.cpp:2647`), WallClimb (`:2952`) and AirStuck (`:3315`) pay it per tick too.

The site that gains most is `texturebug.inl:590`, one scan per predicted path step against
`k_tbPredTraceCap = 400`: in open air the cap was spent after ~25 steps, and at 1 trace per
empty step instead of 16 the same budget reaches far further. **That is a range win for the
predictive sweep, not only a tick-cost win** — worth watching for in game.

The box is `mv::ScanReachXY(half_x, half_y, extra_reach, |vel.xy|, dt)` on x and y, and the
hull's own `[mins.z - fall_look, maxs.z]` on z (no direction widens z). The xy term is
`hypot(half_x, half_y) + extra_reach + |vel.xy| * dt + 1u slack`. The part that is easy to get
wrong is `support`: the loop's `min(hx/|cos|, hy/|sin|)` peaks at the hull DIAGONAL (33.94 for
a 24x24 player hull), not at `half_x`.

Gate test: `tests/mvscan_test.cpp` (`cmake --build build --config Release --target mvscan_test
&& build\Release\mvscan_test.exe`). It re-implements the loop's per-direction arithmetic
verbatim and asserts the box contains both trace families over 637875 hull/reach/speed/
velocity-angle/scan-start combinations. **Result: ok, 637882 checks, 0 failed, tightest margin
1.0000u** — exactly `k_scanBoundSlack`, so the bound is safe AND tight (a loose bound would hit
on everything and save nothing, which the test also asserts).

`TraceHull` increments `s_tracesThisCmd`, so the `tr=` figure in the TB diag line should DROP.
That is the in-game measurement for this fix.

---

## Fix 5 — A1 — DONE

Five `SetupBones` call sites moved from `BONE_USED_BY_ANYTHING` to `BONE_USED_BY_HITBOX`:
`aim_global.cpp:74` (`PlayerBoneInFOV`), `aim_hitscan.cpp:252`, `aim_melee.cpp:745`,
`aim_projectile.cpp:324` and `:1101`. `BONE_USED_BY_HITBOX` (0x100) and
`BONE_USED_BY_ATTACHMENT` (0x200) added to `defs.h:8081` from `studio.h:415-416`; only HITBOX
is used, ATTACHMENT is there for the fallback below.

The highest-payoff item of the audit, and the reason is not the aim path. From the SDK
(`source-sdk-2013/src/game/client/c_baseanimating.cpp`):

- line ~2911: the bone cache hits only when `(GetReadableBones() & boneMask) == boneMask`.
  `BONE_USED_BY_ANYTHING` is `0x0007FF00`, all eleven "used by" bits, so it is **never** a hit —
  every call was a full rebuild (`StandardBlendingRules` + `CalculateIKLocks` +
  `BuildTransformations`).
- line ~2904: `m_iAccumulatedBoneMask |= boneMask`, and the next frame does
  `boneMask |= m_iPrevBoneMask`. So asking for ANYTHING once **upgraded the game's own
  render-path SetupBones for that player to full-bone setup, permanently.** That cost frames
  nowhere near the aim path.

`BONE_USED_BY_HITBOX` is a subset the renderer already builds for any player on screen, so it
should now be a cache hit.

Safety check done before editing: **every** read off these arrays in this codebase is
`aBones[pBox->bone]` — hitbox bones only. All sites checked, including `aim_hitscan.cpp:~430`
(`Math::RayToOBB(..., aBones[pBox->bone], ...)`) and the melee path, which turns out never to
read `m_aBones` at all (its `SetupBones` is purely the "are this model's bones available" gate;
it reads only `m_vOrigin` / `m_vMins` / `m_vMaxs`). No attachment bone, no raw bone index.
`aim_melee.cpp:732` also got the same 6 KB aggregate-assign fix as A6.

**This is the one to test in game.** The failure mode is NOT a crash: a bone that was not set
up reads as an identity or stale matrix, so a wrong mask shows up as the aimbot pointing at the
world origin or at where the player stood a second ago. Check a live player at close range
mid-animation (running, taunting, rocket-jumping), with hitscan and projectile both.

If it misbehaves: first try `BONE_USED_BY_HITBOX | BONE_USED_BY_ATTACHMENT` (0x300, both macros
are already in `defs.h`); failing that revert that one file — the five sites are independent.

---

## Left on the table, on purpose

**A7** — `aim_melee.cpp:336`, `CAimbotMelee::UpdateInfo` takes `std::vector<Target_t>` BY VALUE
(should be `const&`), and builds `std::unordered_map<int, MoveStorage> mStorage` per command,
then hash-looks-it-up per target per simulated tick (`:358`, `:390`). Real cost, but a bigger
refactor than its payoff justifies next to the five above. Pick it up if melee ever profiles hot.

## Next-session prompt

> dih (`C:\Users\huber\Desktop\SimpleVisual\cplus`). Read `HANDOFF_optimize.md`.
> All five fixes from the movement + aim optimization pass are in, full build green, all 20
> gate tests pass. **Nothing has been tested in game.** Two of the five changed behavior rather
> than just allocation shape, and they are what an in-game session should be looking at:
>
> 1. **A1, the bone mask** (Fix 5). Five `SetupBones` sites now ask for `BONE_USED_BY_HITBOX`
>    instead of `BONE_USED_BY_ANYTHING`. Failure mode is not a crash — a bone that was not set
>    up reads as an identity or stale matrix, so it looks like the aimbot pointing at the world
>    origin or at where the player stood a second ago. Test a live player at close range
>    mid-animation (running, taunting, rocket-jumping), hitscan and projectile both. Fallback
>    is `BONE_USED_BY_HITBOX | BONE_USED_BY_ATTACHMENT` (0x300, already in `defs.h`), then
>    revert the one file that misbehaves — the sites are independent.
> 2. **A2, the FOV reject** (Fix 2). `aim_global.cpp` now skips a target whose padded bounding
>    sphere is entirely outside `AimFOV`. Watch for an enemy at the very edge of the FOV circle
>    being ignored. If that shows up, raise `k_fovRejectPad` in `aim_global.cpp` — it is the
>    slack, and raising it only costs bone rebuilds.
>
> The one measurable number to read back: **M1 (Fix 4) should drop `tr=` in the TB diag line**
> in `C:\dih\debug.log`, since the broadphase replaces up to 16 hull traces with 1 whenever
> nothing is within reach. Also watch whether TB's predictive sweep now reaches further before
> it spends `k_tbPredTraceCap = 400` — that is the range half of the same fix.
>
> Read `C:\dih\crash.log` first, then `C:\dih\debug.log`. Backups of every file this pass
> touched are in `cplus/backups/src_baks/*.bak_20260922_091023`.
