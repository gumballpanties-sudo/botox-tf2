# SimpleVisual

Debloated infovisual fork. TF2 visual overlay DLL, x64, DX9 Present hook + ImGui.

## Features
- Watermark (simple): label, fps/ping/time, position, scale, colors
- Viewmodel: sway, offset XYZ, remove bob
- Fog changer (color, start, end, density)
- Skybox changer (preset list + free text)
- Weather: rain + snow (CPrecipitation entity, no particle mode)
- No hats (cosmetic skip + bodygroup restore + unusual particle block)
- Only-your-killfeed filter (FireEvent hook)
- Crosshair (simple): lines, dot, antialias, outline
- sv_pure + sv_cheats bypass
- Remove ragdolls (CreateTFRagdoll skip, from Aletherium)
- Flip world: mirrored 3D frame incl. gun + particles, HUD markers follow, controls mirrored, projectiles leave from the drawn side (load a map after injecting)

## Tests
- `cmake --build build --config Release --target flipworld_test && build\Release\flipworld_test.exe`

## Keys
- INSERT = menu
- F11 = eject (soft, DLL stays loaded)

## Build
```
cmake -B build -A x64
cmake --build build --config Release
```
Output: `build/Release/simplevisual.dll`

## Paths
- Config: `C:\simplevisual\default.cfg`
- Log: `C:\simplevisual\debug.log`

## Layout
- `src/dllmain.cpp` — global defs, DebugLog
- `src/common.h` — engine helpers (ConVar, VCall, PatternScan)
- `src/globals.h` — externs + inline engine helpers
- `src/hooks/thread.cpp` — DllMain, hook install, eject
- `src/render/render.cpp` — Present/Reset/WndProc, ImGui style
- `src/hacks/visuals/visuals.cpp` — fog/skybox/viewmodel/weather/nohats/ragdolls
- `src/hacks/menu/menu.cpp` — catalyst-style flat menu, all visible up front
- `src/hacks/menu/config.cpp` — key=value config, table-driven
