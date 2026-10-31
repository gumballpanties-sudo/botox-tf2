# botox-tf2 — project config

Debloated fpsoverlay fork, rebranded to match botox (CS:GO). TF2 visual DLL. CMake target `botox`, output `build/Release/botox.dll`.

## Paths

| What | Path |
|---|---|
| Source root | `C:\Users\huber\Desktop\botox-tf2\src` |
| Port docs | `docs/port/STATUS.md` (what shipped / simplified / blocked + in-game check order), `gap.md`, `botox_menu_spec.md`, `routecalc_plan.md` |
| botox ports | unit menus `src/hacks/menu/unit*_menu.cpp` wired at `// UNIT_MENU_SLOT <tab>_<page>` in menu.cpp; movement ports `movement_ex.inl`, `movement_edge.inl`, `movement_tricks2.inl`; esp `aim/esp.cpp`; aim extras `aim/backtrack.cpp`, `grenade_lob.cpp`; screen `world_fx*.cpp`, `screenfx_passes.cpp`, `reflections.cpp`, `mc_world.cpp`; hud `watermark_styles.cpp`, `spectator_list.cpp`, `killeffects*.cpp`, `hitmarker.cpp`; assets `gif_tex`, `avatar_cache` (steam_api64 flat exports), `blur_rect` |
| x64 sigs | `C:\Users\huber\Desktop\Aletherium-main\Amalgam\src` (Amalgam TF2 x64). TF2 has NO ladders |
| Globals + DebugLog | `src/dllmain.cpp` (defs), `src/globals/` = `globals.h` `types.h` `common.h` |
| Hook install | `src/hooks/thread.cpp` → `PollThread()`; `src/hooks/` also `netvars.cpp`, `crashlog.cpp` |
| Layout (botox style) | `src/hacks/<area>/` — `movement/` (pipeline + `.inl`s, routesim, prediction, recorder, detections), `visuals/` (visuals, skins, chams_*, flipworld, scope/vm csgo, world_fx*, mc_world, reflections, lagres), `screen/` (screenfx, ssao, dof, bloom, colorcorrect, frameblend, blur_rect; `screen/shaders/` = all `fx_*.hlsl/.h`, `cc_shader`, `fx_chblend_build.sh`), `hud/` (crosshair, killfeed, watermark*, spectator_list, scoreboard, keystroke, velgraph, movind + migoo/miwarp/metaball, points, hitmarker, mediaplayer, gif_tex, avatar_cache), `menu/` (menu, menu_chrome, menu_flow, unit*_menu, fonts, config, debugmenu), `network/` (netregion, equipregion, mmfix, netcvar), `misc/` (discord_ex, boot_screen, killeffects*, unitE_events), `aim/` + `esp/` (aim subsystem, own SDK layer). `src/render/` = render.cpp. `src/dependencies/` = imgui, minhook, fonts, data. Includes are relative (`../hud/x.h`), no include-path magic |
| Movement halves | `movement.cpp` is the pipeline + the small features; the big ones are `.inl` files textually included by it, in pipeline order: `edgebug.inl`, `texturebug.inl`, `psassist.inl`, `routecalc.inl`, `fakemove.inl`. Same translation unit — a move between them is a text move, never an API change |
| Menu | botox look (spec `docs/port/botox_menu_spec.md`). `menu.cpp` = tab bodies + `RenderMenu` + sidebar; `menu_chrome.h` = bands, accent rules, `menu_group_begin/end`, `Section("title")` wrapper; `menu_flow.h` = metrics + math. Widget chrome lives in the ImGui fork (`imgui_widgets.cpp` Checkbox / SliderScalar / BeginCombo, botox bodies) and the palette in `render.cpp SetupImGuiStyle` — **ImGuiCol_Accent is the accent** (`g_accentColor`, settings tab). Tests: `menuflow_test` (math), `menuchrome_test` (headless ImGui; `menuchrome_test dump` prints the layout) |
| Movement physics | `src/hacks/movement/routesim.h` — z-only CTFGameMovement port. `rs::JumpbugCmd` is AutoJumpbug's policy AND the route calculator's; `rs::JumpbugWillHit` is the look-ahead the bind gates on (`tests/jbpredict_test.cpp`) |
| Screen FX | `src/hacks/screen/screenfx.cpp` (shared depth source + probe), `ssao.cpp`, `dof.cpp`, `frameblend.cpp`; shaders are `fx_*.hlsl` → `fx_*.h` via fxc (command is in each .hlsl header) |
| Frame blending | motion blur. `frameblend.cpp` + `fbmath.h` (tested by `tests/fbmath_test.cpp`) + `fbbones.h` (gun bones, captured in visuals.cpp's `h_DrawModelExecute`). Runs TWICE a frame: world+players from the post slot, gun from `IVRenderView::ViewDrawFade` slot 24. All of it is byte-identical in dih and fpsoverlay — edit one, copy to the other |
| Helpers | `src/globals/common.h` — `PatternScan(HMODULE, "IDA style ? bytes")` |
| Debug log | `C:\botox-tf2\debug.log` ← **READ FIRST when debugging** |
| Config | `C:\botox-tf2\default.cfg` |

`C:\infovisual\` = fpsoverlay, dead. Do not use.

## Build

From `C:\Users\huber\Desktop\botox-tf2`:
```
cmake --build build --config Release
```
→ `build/Release/botox.dll`. New files added to CMakeLists → rerun `cmake -B build -A x64` first.

## Startup — do it, don't announce

Grep dllmain.cpp for globals/hooks — do NOT read whole:
```
grep -nE "^(bool|int|float|void\*|HWND|DWORD|uintptr_t|auto) (g_|o_)|MH_CreateHook|static constexpr" src/dllmain.cpp | head -80
```

## Facts — grep on demand, never read whole

`C:\Users\huber\.claude\projects\C--Users-huber\memory\session_facts.md` (~2.8k tokens) — confirmed vtable slots, netvars, CParticleProperty layout, pattern scanning, threading. Titled "fpsoverlay" but engine facts apply here. Grep for the class/slot needed.

## DO NOT

- Never replace dllmain.cpp whole — merge into existing code.
- "Disguise name uses local name" — dead. Caused wrong hook, heap corruption, re-disguise cache break. Never re-add.
