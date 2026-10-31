# Whole-map pixel surf scan ("search whole map" button)

Built 2026-09-21, gate tests green, **NOT tested in game yet.**

Menu: `movement -> ps finder -> search whole map` (the button is inside the ps finder group, so
the finder checkbox has to be on). Press again while it runs = cancel. Row under it prints
`seam N / M   K surfs`.

## What it does

Checks every brush in the map for pixel surfs and draws a white line along the whole length of
each one it confirms. Sequential: one budgeted slice per command (same `scan budget` slider the
crosshair finder uses), lines appear seam by seam while it runs.

## How, in three steps

1. **Candidates, not a grid.** `IEngineTrace::GetBrushesInAABB` over the whole world +
   `GetBrushInfo` per brush (`FM_Brushes`, fakemove.inl -- it already did this for the wall-jump
   seam gate). A pixel surf is the TOP EDGE OF A BRUSH'S WALL FACE, so `ps_map.h` turns each
   brush into its wall faces' top edges: a horizontal segment with real ends. That segment IS the
   length the overlay draws. Rows of brushes forming one wall merge back into one seam.
2. **Cheap screen.** A brush top with open air above it is an ordinary ledge (floors, stairs,
   rooftops -- most edges in a map). One ray 6u above the edge, into the wall: no wall there, no
   sims spent.
3. **The sim decides**, and it is the SAME code the crosshair finder uses -- `PfScanChunk`, one
   cell, pointed at the seam's exact xy instead of a crosshair column (`s_pfBandHalf = 0`).
   Strict mode, the prop gates and the fall gate all apply unchanged. A seam is sampled every 32u
   along its length; runs of confirmed samples become the drawn line, gaps split it.

## Files

| What | Where |
|---|---|
| Candidate math (pure, gate-tested) | `src/hacks/movement/ps_map.h` |
| Gate test, 87 checks | `tests/psmap_test.cpp` |
| Driver / state machine | `src/hacks/movement/psmap.inl` (included by movement.cpp after fakemove.inl) |
| Shared scanner hooks | `psassist.inl`: `s_pfBandHalf/Step`, `s_pfQuiet`, `PmAbort()` |
| Lines -> render | `PsDrawData::mapLines` (movement.h), drawn in render.cpp in 64u pieces |
| Button | `menu.cpp`, ps finder group. `PS_MapScanToggle()` / `PS_MapScanGet()` |

One scanner, two drivers: a crosshair press or turning the finder off cancels a map scan, and a
map scan cancels a running column scan. They can never run at once.

## Reading the result -- `C:\dih\debug.log`

```
PMAP: <map> -- 5123 brush(es) -> 1840 seam(s) in 62.3 ms, stride 32
PMAP: done -- 1840 seam(s), 2210 cell(s) simmed, 6104 skipped (no wall above),
      88 hit -> 31 line(s) | 1820 ms cpu over 12.4 s, budget 7.5 ms/tick
```

* `brush(es)` 0 = `GetBrushesInAABB` gave nothing: the slot (13/15) or the interface is wrong,
  nothing else in this feature can work.
* `seam(s)` 0 with brushes > 0 = every brush was refused by `PsMap_BrushTop` (no flat top) or by
  `k_psMapMaxNz` -- suspect the plane layout, not the scan.
* `skipped` >> `cell(s)` is normal and is the point: that is the ledge screen doing its job.
* `hit` 0 with `cell(s)` in the thousands = the sim never pinned. Compare against the crosshair
  finder on a wall you KNOW has a surf: if that finds it and this does not, the bug is in the
  cell setup (`PmProbe`), not in the probes.

## Known limits (by construction, not bugs)

* Brushes only. Displacement walls and prop surfaces are not brushes and are never found -- same
  as `ignore props` in the crosshair finder.
* Wall faces more than ~11 degrees off vertical are skipped (`k_psMapMaxNz`).
* Seams shorter than 12u are skipped (`k_psMapMinLen`) -- no hull rides them.
* A seam is sampled every 32u; a surf that only works on a 5u slice between two samples is
  missed. Lower `s_pmStride` (psmap.inl) if that shows up, it costs sims linearly.
* The candidate build is one hitch on the button press (logged in ms). The sims after it are what
  is spread over ticks.
* While a scan runs it spends the finder's budget every command, so other sim features
  (route calc, air stuck) go short for the duration. Same as a long crosshair scan.

---

# dih160 (2026-09-21) -- what it misses, what it invents, and texturebugs

Report: *"make ps finder search whole map better, cause rn it doesnt find all surfs + finds a lot
of surfs that are inside walls or out of bounds."* Built, gate tests green (`psmap_test` 645,
`helperpt_test` 2071, `routesim_test` 1026), **NOT tested in game.**

## Finds MORE

| Was | Now | Why it lost surfs |
|---|---|---|
| `k_pmMaxSamples = 64` | `512` | A 4000u wall got 64 samples **62u apart** -- wider than the 49u hull, so whole stretches were never asked. The cap was silently a cap on coverage. |
| `PsMap_SampleCount` floored | floor, **+1 if the spacing would overrun the stride** | 184u usable / 32 = 6 samples **36.8u apart**. A stride is a promise about the GAP; the count was rounded the wrong way. |
| `PmWallAbove` asked z+6 only | z+**2**, +6, +**16**, any one may answer | The ray was put low on purpose (the catch is at the feet) and still missed a 2u stub of wall over the edge -- a real surf, refused before any sim. |
| Merge quantised height to **0.5u** | **1/64u** (`ps_map.h` `key`) | Tops at 511.9 and 512.1 fell in one bucket and merged. The probe owns the 1u unit a height falls in (`TryPin` = floor(z)+0.0287 and sweeps that unit), so the whole merged run was probed at 511.x and every sample over the second brush asked a unit with no surf in it. Depth stays tolerant at 0.5u; height must not be. |

## Invents LESS

* **`PmSpotUsable`** -- `IEngineTrace::GetPointContents` (slot 0) and `PointOutsideWorld` (slot
  16) at the spot a hull would stand (26u out from the face, 36u up). `GetBrushesInAABB` hands
  back every brush in the BSP, including faces buried inside other brushes and the OUTSIDE faces
  of the shell that seals the map. A swept trace cannot see either: it reports where a sweep
  stopped, never whether it started somewhere real. Also in `PfScanChunk::ScanCell` (`void=` on
  the scan line) so the crosshair finder gets it too.
* **`PmWallAbove` no longer treats `startsolid` as "the wall is certainly there".** The ray
  starts 8u OUT from the face; solid out there is a second brush or a clip standing in front of
  the spot, not this wall carrying on above it.
* **The map scan is always strict** (`g_psFinderStrict || s_pfQuiet` in ScanCell). The checkbox
  is about a column you aimed at and can judge; thousands of lines on geometry you never looked
  at have nothing to be weighed against.

### Slot 16 is self-checked
`PointOutsideWorld` is one index past the last slot confirmed in the wild here (TraceRay 4,
GetBrushesInAABB 13, GetBrushInfo 15 all work) and TF2 ships EngineTraceClient004 vs the SDK
header's 003. A wrong slot would not crash -- it would answer nonsense and silently throw away
every spot found. `MV_ValidateOutsideWorld` asks it about the local player (who is, demonstrably,
inside the map) once per session and switches the gate off if it gets that wrong. Look for
`MV: PointOutsideWorld self-check` in `C:\dih\debug.log` on the first run.

## Texturebugs now come out of the same walk

`PmTbProbe`. A texturebug is two coplanar faces the solver clips separately, i.e. a horizontal
texture boundary on a wall -- which **is** a brush top edge with the wall carrying on flush above
it, exactly the candidate this file already had. Two line traces (`PfSeamProbe` at z-1 and z+1) +
`PfIsSeam` settle it, against the ~24 sims a pixel surf costs. The column pass has to hunt for
the boundary a unit at a time and then bisect for its height; here the plane math already knows
it to the float.

* Output: `s_psMapTbPts` -> `PsDrawData::mapTbPts`, red rings, drawn under **`g_psMapLines`**
  (the map scan's own switch), not the finder's -- same reasoning as the lines.
* `RcPickFinder` reads it, so **a route calculator TEXTUREBUG marker can now be placed anywhere
  on the map**, not only on a column you scanned by hand.
* Its own list, never `s_psTbPts`: a column scan overwrites that one on every release and a map
  scan is minutes of work.
* One ring per 64u of seam, 2048 per scan (`k_pmMaxTbPts`) -- a boundary runs a whole wall, and
  32u samples would ring a 4000u wall 125 times.
* Obeys the `texturebug seams` checkbox (`g_psFinderTb`).

## New debug.log fields

```
PMAP: done -- N seam(s), N cell(s) simmed, N skipped (no wall above),
      N skipped (in a brush / off the map), N hit -> N line(s) + N texturebug(s) | ...
PF:   ... prop=N void=N strict=N ...
```
`void` >> everything = the out-of-bounds gate is eating real spots; check the self-check line
first, then `PmSpotUsable`'s 26u/36u offsets against the wall you are testing.

## Still missed, by construction

Everything in the old list, plus: **a texturebug between two brushes with the SAME texture.**
`TB_FaceSame` uses the surface name because plane distance cannot separate two flush brushes --
that is what flush means. The brush math here does know both brushes exist, so it could in
principle decide it geometrically, but a single brush standing flush in FRONT of the candidate
would then read as a seam. Not worth a false positive; left as is.
