# botox-tf2 port status (2026-10-30)

Base: copy of SimpleVisual/dih, rebranded to botox. Build: `cmake --build build --config Release` -> `build/Release/botox.dll`.
Files: `C:\botox-tf2\` (debug.log, default.cfg, crash.log). Debug log off by default (settings tab).

**Everything below is UNTESTED in game.** All of it builds and its gate tests pass; none of it has run against live TF2.

## Menu
botox chrome 1:1 (ImGui core widgets, verdana chrome fonts, 100x24 sidebar, group boxes, accent).
Tabs: aimbot | visuals (esp, world, screen) | movement (main, indicators, calculator, recorder) | misc (main, player list) | inventory | fonts | settings.
Settings: accent colour, dpi scale, safe mode (var only, gates nothing yet), config save/load, debug log, menu key.
Not done: whole-menu open/close fade, gear OptionPopup, fonts tab picker (no atlas-rebuild path), player list page (empty stub).

## Route calc
Split budget (`maxEdges`), 45s hard limit, async worker thread (snapshot in, gen counter out, bad_alloc caught), jumpbug ducked last-marker landing (`jump -> land (ducked)`).
Watch: `RC: worker result dropped`, `RC: ... TIMED OUT`.

## Features by tab (log tag in brackets where one exists)
| tab | shipped | simplified (not 1:1) | blocked |
|---|---|---|---|
| aimbot | backtrack (tick_count rewind, bones stored at capture), scope fov scale, per-weapon fov (sniper/minigun/scattergun), grenade lob to point | per-weapon tuning = fov only | |
| visuals/esp | player esp (box, name, hp, dist, flags, skeleton, hitboxes, fade), avatars, oof arrows (Amalgam OffscreenArrows math: offset 100, max dist 1000, screen-distance fade), sound pings, damage numbers, chams (players), player glow = Amalgam CGlow stencil + RT halo (aim/chams.cpp Glow_Render from DoPostScreenSpaceEffects; RTs made on LevelInit, so injected mid-map = glow after next map load; hats + held weapon glow with their owner) | glow popup: stencil 1 + blur 1 default, "normal blending" thickness = blur amount, legacy outline = stencil 2; gradient ignored; oof arrows skip on-screen enemies (Amalgam draws both) | weapon/viewmodel chams |
| visuals/world | buildings/ammo/dropped weapon/projectile esp, projectile path, dlight, sun angle, world/prop/weather tint | | blood colour (no .pcf colour hook) |
| visuals/screen | sharpen, grain, vignette, invert, posterize, deband, channel shift, emphasize + show depth (botox depth_view 1:1, post slot), fullbright, disable interp, aspect ratio, world fov, third person, no visual recoil, reflections, mc sky/clouds (botox sky colours/sun/clouds, drawn behind R_DrawSkyBox), low fps viewmodel (botox viewmodel_fps.h 1:1; camera from OverrideView, gun+arms via ViewmodelDrawKind) | third person has no collision; reflections = one pass (botox: 5 passes, cubemap fallback, blur/falloff/scale/quality/smooth sliders); mc sky evaluated per pixel instead of baked skybox VTFs | hide hud elements (no gHUD pattern), web on surface (no x64 WebView2Loader.dll) |
| movement/main | nulls, half sideways, delay hop, air freeze, auto crouch, jumpbug kangaroo, smooth demo view, jumpstats, auto strafe avoid walls, lj adaptive cancel, edge bug = botox edgebug.cpp 1:1 [EB:] (bands normal/enhanced/ebanat, 16 variants, duck press/release sweep, bend, multi-path mouse steer, mouse lock via OverrideMouseInput slot 20, delusional + dna donor styles), edge skip full search [ES:], blockbot [BB:], pixel finder headbounce/pixeljump [PF:], ps surf fix | tung surf [TUNG:] = widened ramp band, bounce/auto bounce [BNC:] = live wall-tangent redirect; eb TF2 rules: 20u air duck, no duck hold after the bug (TF2 can't jump ducked), a ducked bug that lands still ducked is only a fallback (`blk=`/`fb=` in the EB line), viz = path + pad only (no alt paths/shapes), sound = beep | fireman + all ladder tricks: TF2 has no ladders |
| movement/indicators | velocity graph, keystrokes + mouse, keybind particles/random colour/custom labels, detections + stack eb, ps lines | | |
| movement/recorder | record/play, clipper, lock va, walk to start, stop on move, force weapon, routes [REC:] | | |
| misc/main | hitmarker + sound, kill effects (blood, confetti, explosion, soul x4, smoke, balls, mc crit + xp orbs), score popup, spectator list (32 botox styles incl. gamesense), watermark (21 botox styles incl. gamesense; gifs, steam avatars, blur), media player = botox draw_media_player (look + fonts follow the watermark style, under-watermark anchor, simple / background / progress bar / precise time, prev-pause-next buttons while the menu is open), every style on its botox font: same face + px + FreeType flags (render.cpp BuildFontAtlas = botox build_fonts), discord rpc (needs your app id), debug menu, console colour | spectator kamidere (32) falls to default; media lyrics + waveform not ported; chillware link-line animation = straight row; eyes = drawn circle; chillware_v2 drag position not saved across restarts; skebob typing / lumi rain / dna glow-on-spot dropped | melt death, bot rename, chat translator, chat images |

Impossible in TF2 (skipped): CS:GO inventory/stickers/knife/agents/gloves (TF2 skins tab = inventory), panorama/scaleform/chud HUD, bomb, zeus, stamina, 128 tick, voice-packet network esp/skins, ladders.

## In-game check order (crash-prone first)
1. Inject, open menu, click every tab. Eject (F11) + re-inject.
2. Map change twice (chams/glow/avatars/gif textures re-init on LevelInit; textures released on device Reset: alt-tab fullscreen).
3. Route calc: place markers, solve, edit a marker mid-solve, change map mid-solve.
4. Backtrack: shoot a strafing player with backtrack on vs off.
5. Sound esp (new EmitSound hook), world fov/third person (new OverrideView slot 16 hook), no visual recoil (new CalcView pattern hook), edge bug mouse lock (new OverrideMouseInput slot 20 hook: `edgebug: OverrideMouseInput: hooked at ...`, then mouse must still turn the view with eb off).
6. Depth passes (10-31 depthfx): reflections, emphasize, show depth each ALONE -> `reflections: drew` / `screenfx_passes: depth view drew ... at the post slot`, and no repeating `rebuilding the depth source`. Minecraft sky -> `mc_world: sky drew WxH at the skybox draw`; 3D skybox props, smoke and rain must stay in front of it.
7. Precipitation: fresh load, tick it, rain must fall without touching the combo; rain -> snow -> rain; map change.
8. Everything else, one toggle at a time, debug log on.

Known test failure: `migoo_test` (3 checks), fails in original SimpleVisual too.

## Air stuck + pixel surf (10-31, branch airstuck) -- botox 1:1, UNTESTED IN GAME
Why: old AS lost the wall while already pinned (`AS SKIP why=nowall xy=0 vz=-6`), old PS held 3 of 74 ticks (`held=0`). The 10-30 TF2 update was a map + medals patch, not movement.
- `airstuck.inl` = botox `air_stuck.cpp` (clarity cardinal/tangent ladder + robust/catch_rob ulp scoring, park bisection, lumi 16-dir scan + 85.999..90 fan, hold replay, as_sync `cl_pred_optimize` <= 1 while held). `pixelsurf.inl` = botox `pixel_surf()` (hold re-proves your keys / 450 along-wall / 45-450 presses each tick, hunt 8 ticks with duck flip, `ps_cmd_start` duck-key bail). Pure math in `airstuck_pure.h`, gate `airstuck_test`.
- Order now botox's: ride detect -> last latch's duck -> AutoAlign -> TB/HS/HB -> AS -> WC -> AutoBounce -> **PixelSurf last**. `Movement_AirStuckLastWriter` (aim hooks.cpp after `EnginePrediction.End`) = `air_stuck_final_stamp` + `shot_proof`: re-proves the pin after silent aim rotates the move.
- Deviations: clarity `robust()` restores the frame per probe (no m_iEFlags offset to dirty abs velocity); world-only trace filter as botox; duck crop 0.3333 (TF2) not 0.34; `movement_add_window` toasts -> DebugLog; no auto-bounce/fireman owns-cmd gate (none here). Knobs: `asExtraReach` (def 4), `asBudget` (% tick, def 50); old asWallReach/asFreeMove/asYieldSurf/asAutoCrouch, psGuide*/psSpeedFix/psSpeedCap/psStick/psPark* deleted (cfg ignores unknown keys).
- In-game: hold AS key beside a wall mid-air -> `AS: clarity pin` / `AS: lumi ... pin=3` then `AS: hold`; fall onto a surf -> `PSU: latch` / `PSU: hold by=` and the ride keeps sliding; shoot during a stick -> `AS: shot keep=1`. Watch `AS: lost` / `PSU: release` counts and frame time (AS budget 50% tick).
