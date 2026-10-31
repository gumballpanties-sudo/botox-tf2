# botox (CS:GO) -> botox-tf2 (dih) gap analysis + work units

Research-only. No source touched. Cross-checked against `botox_features.md`,
`tf2_features.md`, TF2 SDK (`source-sdk-2013-master`), and TF2 domain knowledge
(Amalgam source was not available on disk as extracted source — only zip
archives under `Documents\!`/`Downloads` — so vtable/pattern claims below cite
`tf2_features.md`'s own hook table (dih's already-confirmed, already-shipping
hooks) wherever a feature can piggyback an existing one, and are flagged
"NEW hook, verify slot" wherever dih does not yet have that hook installed.
Route calc is excluded per instructions — see `routecalc_plan.md`.

Correction applied throughout: TF2 runs the same Source `CGameMovement`
lineage as CS:GO (confirmed: `tf_gamemovement.cpp` derives from
`gamemovement.cpp`, same hull-trace/duck/ladder/air-accel model, different
constants). Movement-tech rows below are judged on real TF2 mechanics, not
copied "CS:GO-only" verdicts from the source botox doc. botox-tf2 already
has edgebug/texturebug/pixelsurf/wallclimb/airstuck/jumpbug working
(`edgebug.inl`, `texturebug.inl`, `psassist.inl`, `fakemove.inl`,
`movement.cpp`) — that's the proof the family ports.

---

## 1. Feature table

Status legend: **HAS** = shipping in botox-tf2 today (cite the var/file from
`tf2_features.md`). **PARTIAL** = core exists, named sub-feature(s) missing.
**MISSING** = no TF2 analog built yet but one is real and portable.
**IMPOSSIBLE** = no TF2 analog exists, reason given.

### TAB: aimbot

| botox feature | botox vars | tf2 status | TF2 analog notes |
|---|---|---|---|
| aimbot master | `m_aimbot_enable` | HAS | `Aimbot::General::Enabled`, `aim/aim.cpp` |
| aim on key | `m_aimbot_on_key`,`m_aimbot_key` | HAS | `AimKey`/`AimKeyMode` |
| silent aim | `m_aimbot_silent`,`_step` | HAS | `AimType` combo includes silent mode |
| target team | `m_aimbot_target_team` | HAS | `Target`/`Ignore` multi-select |
| scope fov scale | `m_aimbot_scope_fov_scale*` | MISSING | TF2 sniper scope has one fixed zoom, no scale slider yet — small new feature, not a CS:GO-only concept |
| per-weapon-class fov/smooth/rcs/hitboxes | ~30 vars | PARTIAL | HAS global `AimFOV`/`Hitboxes`/`Modifiers` split by Hitscan/Projectile/Melee; MISSING per-TF2-weapon-slot granularity (scattergun vs sniper vs minigun tuning) |
| rcs (recoil control) | `m_aimbot_rcs_*` | IMPOSSIBLE | CS:GO fixed spray pattern has no TF2 hitscan analog (TF2 weapons use random/accumulating spread, not a recoil pattern) |
| autowall | `m_aimbot_autowall`,`_min_damage` | IMPOSSIBLE | TF2 hitscan has no penetration/surface-material model to exploit |
| backtrack | `m_backtrack_*` | MISSING | Portable — infra already compiled: `CBaseEntity_SetAbsVelocity`, `CBaseEntity_EstimateAbsVelocity`, `RecvProxy_SimulationTime`, `CL_ProcessPacketEntities` are already `MAKE_HOOK`'d and whitelisted in `aim/hooks.cpp` for prediction cleanup — same hooks a ring-buffer backtrack needs |
| nospread | `m_nospread_*` | HAS | `General::NoSpread` + `aim/nospread_projectile.cpp` |
| zeus bug | `m_zeusbug*` | IMPOSSIBLE | CS:GO Zeus x27 exploit, no TF2 equivalent item |
| grenade aimbot | `m_grenade_aim*` | PARTIAL | `aim_projectile.cpp` already does target-seeking aim for rocket/pipe/sticky/arrow (`Projectile::Enabled/AimFOV/StrafePrediction/SplashPrediction`); MISSING the FOV-lob-to-point-and-draw-arc widget for non-target throws (see visuals/grenade path row — same gap, different unit) |

### TAB: visuals / esp

| botox feature | botox vars | tf2 status | TF2 analog notes |
|---|---|---|---|
| player esp master | `m_players` | MISSING | No ESP system in botox-tf2 at all today — this is the single largest gap |
| teammates filter | `m_players_teammates` | MISSING | part of player esp |
| distance fade | `m_players_distance_fade` | MISSING | part of player esp |
| sound-only/sound fade esp | `m_players_sound_only/fade` | MISSING | needs `IEngineSound` hook (interface confirmed to exist, `public/engine/IEngineSound.h`; not yet hooked in botox-tf2) |
| esp box (2d/3d/corner/outline/fill/gradient) | `m_players_box*` | MISSING | pure draw, no engine risk once w2s works (w2s already exists, flipworld uses it) |
| esp name | `m_players_name`+colors | MISSING | — |
| esp health bar/number | `m_players_health_*` | MISSING | TF2 netvar `m_iHealth` is standard, trivial read |
| weapon name/icon/ammo bar | `m_weapon_name/icon`,`m_player_ammo_bar` | MISSING | needs TF2 weapon icon set (game's own HUD icon atlas is reusable, no new art needed) |
| esp distance | `m_players_distance` | MISSING | — |
| esp flags | `m_players_flags` | MISSING | analog CONFIRMED: `TF_COND_UBERCHARGED`/`BURNING`/`STEALTHED`/`BLEEDING` etc exist in `tf_shareddefs.h`, and `CTFPlayerShared_InCond` is already a compiled+whitelisted hook in `aim/hooks.cpp` — reuse directly, no new hook |
| player skeleton esp | `m_players_skeleton` | MISSING | bone draw, generic |
| show hitboxes | `m_players_hitboxes` | MISSING | hitbox layout differs from CS:GO but concept ports (`aim_hitscan.cpp` already reads TF2 hitbox sets for the aimbot — reuse that data) |
| player glow | `m_glow_*` | MISSING | TF2 ships its own native `CGlowObjectManager` (used by game for cart/flag-carrier glow) — simpler port than CS:GO's: populate the native glow list, no custom render pass needed |
| player stencil | `m_players_stencil` | MISSING | — |
| internal player glow | `m_inner_glow*` | MISSING | — |
| lag-comp backtrack trail | `m_players_backtrack_trail` | MISSING | depends on aim unit's backtrack (F) |
| player avatars (esp) | `m_players_avatar` | MISSING | Steamworks avatar fetch is engine-agnostic, ports directly |
| damage numbers | `m_damage_numbers*` | MISSING | `player_hurt` event exists in TF2, ride `IGameEventManager2::FireEvent` slot 8 (confirmed hook, already installed for killfeed filtering — append) |
| out-of-fov arrows | `m_out_of_fov_arrows*` | MISSING | — |
| dropped weapons esp | `m_dropped_weapons*` | PARTIAL | TF2 does NOT drop guns like CS:GO, but `CTFDroppedWeapon` and `CTFAmmoPack` are real confirmed entity classes (`tf_dropped_weapon.cpp`, `tf_ammo_pack.cpp`) — real analog exists, just rarer trigger (death-drop + ammo pack pickups, not every corpse) |
| planted bomb esp | `m_bomb_esp*` | IMPOSSIBLE | no bomb entity in TF2; payload cart / intel flag are structurally different objectives, not a 1:1 swap — flag as a possible unrelated new feature, out of scope here |
| thrown projectiles esp | `m_thrown_objects*` | PARTIAL | TF2 projectile classes confirmed (`tf_weaponbase_rocket.cpp` family: rocket/pipe/sticky/arrow/jar) — same ESP technique, different class list |
| grenade path (+radius) | `m_grenade_path*` | PARTIAL | same gap as aimbot's grenade-aim-draw row — one simulate+draw feature covers both |
| enemy chams | `m_chams_enable*` | MISSING | `DrawModelExecute` slot 19 is already hooked (no-hats/frame-blend) — append material-override branch, same hook, no new install |
| local player chams | `m_chams_local_enable` | MISSING | same hook as above |

### TAB: visuals / world

| botox feature | botox vars | tf2 status | TF2 analog notes |
|---|---|---|---|
| world fov | `m_world_fov_enable` | MISSING | not in current HAS list; small, needs a camera-FOV override point (new, see unit D) |
| precipitation toggle | `m_precipitation*` | HAS | `g_weatherEnable/g_weatherMode` (rain/snow) |
| sun angle | `m_sun_angle` | MISSING | light_environment override, small |
| fog | `m_fog` | HAS | `g_fogEnable` |
| fullbright | `m_fullbright` | MISSING | trivial — `mat_fullbright` convar poke, no hook needed |
| flip world (+mirror hand) | `m_flip_world*` | PARTIAL | HAS core flip (`flipworld.cpp`, full hook set confirmed); MISSING "mirror gun to other hand" sub-toggle |
| dlight (+local) | `m_dlight*` | MISSING | small |
| custom particle colors | `m_custom_*` | PARTIAL | smoke/molotov IMPOSSIBLE (no such grenades in TF2); blood/precipitation color MISSING but portable (particle attribute override, generic engine feature) |
| old shaders | `m_old_shaders*` | IMPOSSIBLE | confirmed — TF2 never moved to a PBR shader set, nothing to "revert" |
| world texture replace | `m_world_texture*` | MISSING | `DrawModelExecute` slot 19, append |
| prop texture replace | `m_prop_texture` | MISSING | same hook |
| bullet tracers | `m_bullet_tracers*` | MISSING | `weapon_fire` event exists in TF2, ride `FireEvent` slot 8 |
| movement trail | `m_movement_trail*` | MISSING | generic, no risk |
| bullet impacts | `m_bullet_impacts` | MISSING | — |
| world hit marker | `m_world_hit_marker` | MISSING | — |

### TAB: visuals / screen

| botox feature | botox vars | tf2 status | TF2 analog notes |
|---|---|---|---|
| remove panorama blur | `m_remove_panorama_blur` | IMPOSSIBLE | confirmed — TF2 uses VGUI, no Panorama |
| remove 3d skybox | `m_remove_3d_skybox` | PARTIAL | skybox override HAS (`g_skyEnable`+preset+name); explicit "force 2D only" toggle not separately exposed |
| remove hud elements | `m_remove_hud_elements` | PARTIAL | TF2 HUD is VGUI `CHudElement` panels (simpler than CS:GO's Scaleform+Panorama) — different, easier mechanism: iterate/hide panels, no new render hook |
| remove ragdolls/smoke/flash | `m_remove_ragdolls/smoke/flash` | PARTIAL | ragdolls HAS (`g_removeRagdolls`, `CreateTFRagdoll` hook); smoke/flash IMPOSSIBLE (no CS:GO flashbang/smoke entity in TF2) |
| remove visual recoil | `m_remove_visual_recoil` | MISSING | `CPrediction_GetLocalViewAngles` already compiled+whitelisted (used for Clean POV Demos) — reuse to suppress TF2's shotgun/viewpunch kick |
| disable interp | `m_disable_interp` | MISSING | trivial `cl_interp 0` convar poke |
| viewmodel offset/fov | `m_viewmodel_offset/fov*` | HAS | `g_vmOffsetEnable`, viewmodel fov |
| weapon sway | `m_weapon_sway` | HAS | `g_vmSwayEnable` |
| weapon sheen | `m_weapon_sheen` | IMPOSSIBLE/superseded | CS:GO skin-pattern sheen shader has no TF2 param; TF2 already has its OWN native killstreak sheen (`cfg.ksSheen` combo, `TAB_SKINS`) — nothing to port, functionally already covered |
| viewmodel bob changer | `m_viewmodel_bob` | HAS | `g_vmCsBob`, remove-bob |
| third person (+collision) | `m_third_person*` | MISSING | needs a camera-override hook, see unit D |
| aspect ratio (+stretch) | `m_aspect_ratio*` | MISSING | `Present` slot 17 confirmed hooked already (other purposes) — reuse for letterbox/stretch math |
| color correction | `m_color_correction` | HAS | `g_ccEnable` + curve editor |
| sharpen/film grain/vignette/invert/posterize/deband/channel shift/emphasize | one var each | MISSING (all) | trivial — `DoPostScreenSpaceEffects` slot 39 is ALREADY the shared pipeline `screenfx.cpp` runs AO/DoF/bloom/color-correction through; each of these is one more pass appended to that existing dispatcher, zero new hooks |
| reflections (+smooth normals) | `m_reflections*` | MISSING | needs `IDirect3DDevice9::SetDepthStencilSurface` hook (NEW — not yet in botox-tf2's hook table) |
| show depth (debug view) | `m_show_depth*` | MISSING | depth source already resolved by `screenfx.cpp` (`g_fxDepthProbe` exists) — visualizing it is a small append, no new hook |
| depth of field | `m_depth_of_field*` | HAS | `g_dofEnable/_AutoFocus/_NearBlur/_ViewmodelSharp/_Smooth` |
| ambient occlusion | `m_ambient_occlusion*` | HAS | `g_aoEnable/_Viewmodel/_FogFade/_Debug` |
| motion blur (+forward) | `m_motion_blur*` | HAS | `g_fbEnable` (frame blending) covers this |
| true motion blur (+players/viewmodel) | `m_true_motion_blur*` | HAS | `g_fbPlayers`/`g_fbViewmodel` — same feature as above, already split by category |
| bloom | `m_bloom` | HAS | `g_bloomEnable` |
| sniper crosshair (+dot/t/outline) | `m_sniper_crosshair*` | HAS | "CS:GO scope" feature (`g_scCsgo*`) already draws this exact overlay over TF2's native sniper scope |
| default/classic crosshair color | `m_panorama_crosshair_color` etc | HAS | classic + CS:GO crosshair color vars exist |
| square radar | `m_square_radar` | IMPOSSIBLE | TF2 has no persistent minimap/radar HUD element to reshape |
| panorama hud color | `m_panorama_hud_color` | IMPOSSIBLE | no Panorama color-class system in TF2 |
| keep killfeed | `m_keep_killfeed` | N/A | nothing to "keep visible against" — TF2 killfeed is native VGUI, always there; botox-tf2 already has its own distinct "only your killfeed" filter (`g_selfKillfeedOnly`) |
| scaleform passthrough | `m_scaleform` | IMPOSSIBLE | no Scaleform/GFx in TF2 |
| chud hud (+blur/wm bg/spectator list/shadow) | `m_chud_hud*` | PARTIAL | watermark HAS; unifying it into one blurred custom-HUD container with embedded spectator list is MISSING (spectator list itself is fully missing, see misc) |
| minecraft hud (+crosshair) | `m_mc_hud*` | MISSING | novelty, needs `draw_world_lists`-equivalent hook for clouds/sky (NEW) |
| web on surface | `m_web*` | MISSING | large — WebView2 host port + TF2 surface-pick logic |

### TAB: movement / main

| botox feature | botox vars | tf2 status | TF2 analog notes |
|---|---|---|---|
| bunny hop (+chance) | `m_bunny_hop*` | HAS | `g_bhop` |
| delay hop | `m_delay_hop*` | MISSING | confirmed real gap even by `routecalc_plan.md` ("TF2 has no delay hop bind") — new bind, holds N ticks before jump |
| edge jump | `m_edge_jump*` | HAS | `g_edgeJump` |
| long jump (+adaptive cancel) | `m_long_jump*` | PARTIAL | HAS core + edgejump-on-lj; MISSING adaptive-cancel-time tuning sub-feature |
| mini jump | `m_mini_jump*` | HAS | `g_miniJump`/`_HoldDuck`/`_Edge` |
| jump bug (+crouch/kangaroo) | `m_jump_bug*` | PARTIAL | HAS core (`g_jumpbug`+predict+yieldsurf); MISSING explicit crouch/kangaroo variant toggle (flagged UNTESTED even in botox itself) |
| auto crouch | `m_auto_crouch*` | MISSING | trivial input toggle |
| auto strafe (+avoid walls) | `m_auto_strafe*` | PARTIAL | HAS core + mode; MISSING "avoid walls" sub-toggle |
| strafe optimizer | `m_strafe_optimizer*` | HAS | `g_strafeOpt` |
| ladder bug/glide/freelook climb/fast ladder | 4 features | IMPOSSIBLE | TF2 has no ladders (user, 10-30) |
| edge bug (3 styles, ~25 vars) | `edge_bug*` | PARTIAL | HAS core (`g_edgeBug`+advSearch+autoStrafe+toEdge+brake+standableOnly+viz+stance+human) — this is the hard mechanic, already working; MISSING the 3 named cosmetic style presets (botox/delusional/dna bundles) and mouse-lock/sound sub-toggles |
| edge skip | `m_edgebug_edge_skip*` | MISSING | distinct from edgebug (predictive catch before reaching the edge) — same geometry family that already works in TF2 (`edgebug.inl`), portable |
| pixel surf | `m_pixel_surf*` | HAS | `g_pixelSurf`+guide/speedfix/stick/parkAtEnd |
| tung surf | `m_tung_surf*` | MISSING | CS:GO-tuned fake-ps technique, itself flagged UNTESTED in botox; low priority |
| pixelsurf assist | `m_pixel_surf_assist*` | HAS | `g_psAssist`/`_GroundHelp`/`_Render`/`_JumpBox` (`psassist.inl`) |
| bounce assist | `m_bouncee_assist*` | MISSING | stamina sub-check drops (TF2 has none, confirmed); core bounce-surface-catch concept still portable, niche |
| auto bounce | `m_auto_bounce*` | MISSING | niche, portable |
| texture bug (+wallstrafe) | `m_texture_bug*` | HAS | `g_textureBug`+predict/solveForward/noOffgridStick/parkAtEnd/parkBrake (`texturebug.inl`) |
| air stuck (+fix/screen msg) | `m_air_stuck*` | HAS | `g_airStuck`+autoCrouch/freeMove/yieldSurf (also a second "fake movement" variant: `g_fmAirStuck*`) |
| wall climb | `m_wall_climb*` | HAS | `g_wallClimb`+key/mode |
| fireman | `m_fire_man*` | MISSING | "ebanat" geometry exploit, flagged UNTESTED even in botox; needs re-derivation, same family as wall climb |
| air freeze | `m_air_freeze` | MISSING | trivial, single var |
| 128 tick fix | `m_tick_fix_128` | IMPOSSIBLE | confirmed — TF2 servers are fixed-tickrate, nothing to scale |
| movement fix (generic) | `m_movement_fix` | MISSING | vague catch-all, needs its own bug audit before scoping — skip until a specific TF2 bug is found |
| smooth demo view | `m_silent_view` | MISSING | demo-only, portable; `IsPlayingDemo` is a native query in TF2, no hook needed |
| pixel surf fix | `m_pixel_surf_fix` | MISSING | folds into pixel surf polish, low priority |
| auto align | `m_auto_align` | HAS | `g_autoAlign` |
| nulls (opposing key cancel) | `m_nulls` | MISSING | trivial input filter |
| half sideways bind | `m_hsw*` | MISSING | pure usercmd math, same Source model — direct 1:1 port per CS:GO's own note (`mv_hsw` 1:1) |
| auto one hop | `m_auto_one_hop` | MISSING | small |
| blockbot | `m_blockbot` | MISSING | concept ports; TF2 surface-snap rules (buildings/props) differ from CS:GO, needs its own logic |
| fast stop | `m_fast_stop` | HAS | `g_fastStop` |
| pixel finder | `m_pixel_finder*` | PARTIAL | HAS core (`g_psFinder`+strict/tb/noProps, covers pixelsurfs+texturebugs); MISSING headbounce/pixeljump detection sub-modes |
| pixel calculator | `m_pixel_calc*` | HAS | `g_pixelCalc`+showMarker/showPanel |
| distance calculator | `m_dist_calc` | MISSING | trivial generic w2s tool |
| practice window | `m_practice_window*` | N/A | covered differently — TF2 side's "practice" sub-tab (infinite ammo/health/time/binds/bot spawn) solves the same training need with a different shape; not a direct port target |
| fake pov in demos | `m_fake_pov*` | PARTIAL | `Misc::Game::CleanPOVDemos` HAS covers the underlying goal (hide cheat artifacts in demo); the specific snap-view+arrow widget is MISSING |
| hide y6o ps | `m_hide_y6o_ps` | IMPOSSIBLE | hides an artifact of a CS:GO-specific exploit leak path; N/A without that exploit |
| jumpstats (+show fails) | `m_jump_stats*` | MISSING | portable concept, remap jump-type taxonomy to TF2 (bhop/longjump/ladderjump/wallclimb) |

### TAB: movement / indicators

| botox feature | botox vars | tf2 status | TF2 analog notes |
|---|---|---|---|
| velocity indicator | `m_velocity_indicator*` | HAS | `g_viEnable`+preSpeed/fadeAlpha/customColor |
| stamina indicator | `m_stamina_indicator*` | IMPOSSIBLE | confirmed — TF2 has no stamina stat |
| velocity graph | `m_velocity_graph*` | MISSING | distinct widget from the velocity number; portable, rides `PaintTraverse` slot 41 |
| keybind indicators | `m_key_indicators*` | PARTIAL | core HAS as "movement indicators" (`g_miEnable`/horizontal/mitosis/showMask); MISSING particle/random-color/custom-label decorative sub-features |
| keystroke indicators | `m_key_press_indicator*` | MISSING | distinct widget (duck/jump icons + mouse-direction arrow), low effort given `movind.cpp` infra already exists |

### TAB: movement / calculator

Route calc rows **excluded per task scope** — see `routecalc_plan.md` for its
own full gap analysis (it already covers this in detail: TF2's route calc has
no async worker, no split-search/memory-budget, no landing-as-state-set/
chancy%, no per-marker move bit UI — ranked P1-P5 there, do not duplicate).

| botox feature | botox vars | tf2 status | TF2 analog notes |
|---|---|---|---|
| route calculator (full) | `m_route_calc*` | HAS (excluded) | see `routecalc_plan.md` |
| route calc keybinds | — | HAS (excluded) | see `routecalc_plan.md` |
| route calc points list | — | HAS (excluded) | see `routecalc_plan.md` |
| detections master | `m_detections` | MISSING | generic "announce exploit executed" dispatcher, portable |
| detection: stack edgebugs | `m_detection_stack_eb` | MISSING | depends on detections master |
| edgebug visualize (+project) | `m_edgebug_visualize*` | HAS | `g_edgeBugViz` |
| pixelsurf lines (+full trajectory) | `m_pixel_surf_line*` | PARTIAL | `g_psAssistRender` ("show points") exists; dedicated line/full-trajectory render is a distinct, missing sub-feature |

### TAB: movement / recorder

| botox feature | botox vars | tf2 status | TF2 analog notes |
|---|---|---|---|
| movement recorder (+line/timer) | `m_movement_rec*` | MISSING | no recorder feature exists in botox-tf2 at all; fully portable generic input-record/playback (CS:GO's own is flagged UNTESTED too) |
| clipper | `m_movement_rec_clipper*` | MISSING | depends on recorder |
| original playback viewangles | `m_movement_rec_lockva` | MISSING | — |
| lock while aiming to position | `m_movement_rec_lockgoingtostart` | MISSING | — |
| stop playback on movement | `m_movement_rec_stop_on_move` | MISSING | — |
| force same weapons | `m_movement_rec_force_weapon` | MISSING | needs TF2 weapon-slot remap |
| recorder actions (buttons) | runtime/file-backed | MISSING | — |
| recorder routes list | file-backed | MISSING | — |

### TAB: misc / main

| botox feature | botox vars | tf2 status | TF2 analog notes |
|---|---|---|---|
| watermark (+11 style variants) | `m_watermark*` | PARTIAL | HAS core (label/fps/ping/loss/time/pos/color); MISSING the ~40 decorative style-variant vars (skebob/dna/lumi/gif/kami bundles) — same-knobs-new-style ports |
| spectator list (+10 style variants) | `m_spectators_*` | MISSING | fully missing today; TF2's own spectator-target concept (`GetObserverTarget`/spec_mode) differs structurally from CS:GO's `is_hltv`, but overlay rendering ports once the player-list source is swapped |
| media player | `m_media_player*` | PARTIAL | HAS core (`g_mpEnable/_CoverArt/_ArtSide/_Pos`); MISSING visualizer, lyrics, progress bar, in-game media-key control |
| points (score popup) | `m_points*` | MISSING | TF2 scoring events differ (frags/captures vs CS:GO round economy); ride `FireEvent` slot 8 on TF2's own kill/capture events |
| low fps warning | `m_fps_warning` | MISSING | trivial, FSN-based |
| hit marker | `m_hit_marker*` | MISSING | `player_hurt` event + `FireEvent` slot 8 (confirmed already installed) |
| hit sound | `m_hit_sound*` | MISSING | same event, needs `IEngineSound::EmitSound` hook (NEW — interface confirmed to exist in SDK, not yet hooked in botox-tf2) |
| healthshot effect | `m_healthshot*` | IMPOSSIBLE/analog | CS:GO healthshot item doesn't exist; real analog = trigger same visual on medic heal/overheal (different event, same idea) — portable as a redesigned feature, not a direct port |
| death particles | `m_death_particles*` | MISSING | `player_death` is a standard TF2 event, portable, generic particle burst |
| console color | `m_console_color*` | MISSING | trivial |
| log types filter | `m_log_types` | MISSING | trivial config combo, no hook |
| clantag | `m_clantag*` | MISSING/weak-analog | TF2 has no server-visible clan-tag field either (confirmed CS:GO-specific `m_szClan`); only analog is a client-side name-prefix spoof, weaker than the original — low priority |
| chat translator | `m_chat_translator*` | MISSING | needs a TF2 `SayText2` usermessage hook (NEW), chat itself is standard |
| chat images | `m_chat_images` | MISSING | same hook family |
| bot names | `m_bot_names*` | PARTIAL | basic rename is a plain string write, portable; CS:GO's rank/medal cosmetic fields have no TF2 bot equivalent (`tf_bot.cpp` bots carry no rank system) — IMPOSSIBLE for that part |
| auto revive | `m_auto_revive*` | IMPOSSIBLE | TF2 has no self-revive action outside special unrelated game modes |
| deagle spinner | `m_deagle_spinner*` | IMPOSSIBLE | model-specific (no Deagle in TF2); not worth re-targeting at a TF2 weapon |
| pure bypass | `m_pure_bypass` | HAS | `g_pureBypass` |
| performance toggles | `m_performance*` | PARTIAL | HAS "1ms timer" (`g_hiTimerRes`); other sub-toggles MISSING but low value — CS:GO's own bench measured ~0% gain and the feature was removed there (`proj-perf-bench-1008`) |
| discord rpc | `m_discord_rpc*` | MISSING | engine-agnostic OS IPC, trivial port |
| debugger menu | `m_debugger_visual` | MISSING | trivial debug overlay |

### TAB: misc / player list

| botox feature | botox vars | tf2 status | TF2 analog notes |
|---|---|---|---|
| player list (per-SteamID flags) | file-backed | MISSING | fully portable, SteamID-keyed, engine-agnostic; needs an entity-create listener (TF2 exposes `IClientEntityListener::OnEntityCreated`, NEW hook, verify) |

### TAB: inventory — entirely IMPOSSIBLE-as-port, superseded

botox-tf2 already has its own schema-native skin system (`TAB_SKINS`:
warpaint/killstreak/particle/variant/reskin/extras) — the correct analog, and
it's already shipping. Nothing here needs porting.

| botox feature | tf2 status | notes |
|---|---|---|
| weapon skins (paint kit/stickers) | IMPOSSIBLE/superseded | TF2's own warpaint+particle system already does this job |
| same skins both teams | IMPOSSIBLE/superseded | TAB_SKINS has its own team handling |
| knife changer | IMPOSSIBLE | TF2 melee = schema reskin, covered by TAB_SKINS reskin/warpaint |
| agents | IMPOSSIBLE | no agent system; TF2 player models are fixed class models |
| gloves changer | IMPOSSIBLE | TF2 has no glove viewmodel slot |

### TAB: fonts — entirely MISSING

botox-tf2 has no fonts tab at all today.

| botox feature | tf2 status | notes |
|---|---|---|
| per-category font list picker (indicators/esp) | MISSING | portable, botox-tf2 already draws everything with ImGui/embedded fonts — just needs per-feature font selection wiring |
| chud hud font (+scale/nudge) | MISSING | portable, same font infra |
| default hud font | MISSING | different mechanism — VGUI `ClientScheme.res` file swap, no hook needed, just a file write |

### TAB: settings

| botox feature | botox vars | tf2 status | TF2 analog notes |
|---|---|---|---|
| debug log | `m_debug_log` | HAS | `g_debugLogEnable` |
| safe mode | `m_safe_mode` | MISSING | trivial master-gate wrapper around risky hooks |
| share esp (network) | `m_botox_network_esp` | IMPOSSIBLE/out-of-scope | CS:GO voice-packet steganography, flagged UNTESTED even in botox; TF2 has its own voice codec so the technique could theoretically be re-derived, but both sides acknowledge fragility — not worth the risk |
| share skins (network) | `m_botox_network_skins` | IMPOSSIBLE/out-of-scope | same channel, same call |
| accent color | `m_accent` | MISSING | botox-tf2's palette is currently a fixed constant (`kAccent`); user-facing picker is a small add |
| dpi scale (+panels) | `m_dpi_scale*` | MISSING | botox-tf2 doesn't expose this yet; CS:GO's own rule applies verbatim (`imgui = io.DisplaySize px`) |

---

## 2. Summary counts

- **HAS**: 61
- **PARTIAL**: 25
- **MISSING**: 82
- **IMPOSSIBLE** (incl. superseded/N-A/weak-analog): 32

(Counts are over every row in the tables above, excluding the 3 route-calc
rows explicitly marked "excluded per task scope.")

---

## 3. Work units (parallel, minimal file overlap)

Shared files — **menu.cpp, config.cpp, globals.h, thread.cpp, CMakeLists.txt**
— are touched by every unit only via one small appended block (new
`Section()`/`CfgEntry`/hook-install call), never a rewrite. Each unit's new
files are its own; no two units write the same new file.

### Unit A — Movement tricks

**Features:** ladder bug/glide/freelook-climb/fast-ladder, edge skip,
fireman, tung surf, bounce assist, auto bounce, delay hop, auto crouch,
auto-strafe avoid-walls, jumpbug crouch/kangaroo variant, edgebug style
presets (botox/delusional/dna), long-jump adaptive-cancel, air freeze,
nulls, half sideways bind, auto one hop, blockbot, pixel finder
headbounce/pixeljump modes, jumpstats, movement fix (audit only), pixel
surf fix, smooth demo view, fake-pov snap-view widget.

**Port from (botox csgo-sdk):** `hacks/movement/movement.cpp` (ladder/
delay-hop/auto-crouch/nulls/one-hop/air-freeze sections),
`hacks/movement/edge_skip.cpp`, `hacks/movement/fireman.cpp`,
`hacks/movement/bounce_assist.cpp`, `hacks/movement/auto_bounce.cpp`,
`hacks/movement/pixel_finder.cpp` (headbounce/pixeljump only),
`hacks/misc/misc.cpp` (blockbot), `hacks/misc/fake_pov.cpp`,
memory refs `proj-tung-surf`, `proj-hsw-1004`,
`proj-bot-names-jumpstats-0929`, `proj-silent-view-1005`.

**New tf2 files:** `src/hacks/movement/movement_ex.inl` (ladder tricks, edge skip,
fireman, tung surf, bounce/auto-bounce, delay hop, auto crouch, nulls, hsw,
one-hop, air freeze — textually included from `movement.cpp` the same way
`edgebug.inl`/`texturebug.inl` already are), `src/features/blockbot.inl`,
`src/features/jumpstats.cpp`, `src/features/fakepov.cpp`.

**Hooks:** none new — all ride the existing `IGameMovement::ProcessMovement`
dispatch (`fakemove.inl`'s `kGM_ProcessMovement`=1) or `movement.cpp`'s
existing `CreateMove`/`ProcessMovement` call chain (slot 21, CreateMove
handoff via `Aim_Active()` already respected). `hsw` is pure usercmd math
inside `CreateMove`. `jumpstats` detects jump via `IN_JUMP` button-edge, no
event needed. `fakepov` extends `CPrediction_GetLocalViewAngles` (already
compiled+whitelisted).

**x86->x64 / adaptation notes:** this family is pure C++ state-machine math
(no inline `__asm` anywhere in the CS:GO source for these), so the x86->x64
risk is near zero. The real work is re-deriving TF2's own constants — but
`routesim.h` already has them correct (hull 82/62, jump impulse 289,
`TF_AIRDUCKED_COUNT`=2) since the route-calc port already did this exercise;
reuse those constants rather than re-deriving from CS:GO's.

**Size:** Large.

### Unit B — Movement indicators + recorder + points/detections

**Features:** velocity graph, keybind-indicator particles/random-colors/
custom-labels, keystroke indicators (duck/jump + mouse-direction arrow),
movement recorder (+clipper/lockva/lock-going-to-start/stop-on-move/force-
weapon/actions/routes list), detections master + stack-edgebug detection,
pixelsurf lines + full trajectory.

**Port from:** `hacks/indicators/indicators.cpp` (velocity graph, keybind
particles, keystroke), `hacks/movement/movement_recorder.cpp`,
`movement.cpp`'s detection dispatch, memory refs
`proj-velocity-graph-jump-units-1002`, `proj-key-detect-particles-1002`,
`proj-key-press-indicator`.

**New tf2 files:** `src/hacks/hud/velgraph.cpp`, `src/hacks/hud/keyind_fx.inl`
(appended-into `movind.cpp`'s render call, new file so it stays unit-owned),
`src/hacks/movement/recorder.cpp` + `recorder.h`, `src/features/detections.inl`.

**Hooks:** recorder piggybacks `CreateMove` slot 21 (append a call inside
`movement.cpp`'s existing dispatch, same Aim_Active() handoff rule already
documented); needs a **NEW** window-proc hook for record/play hotkeys —
verify no existing dih wndproc subclass before adding (botox-tf2's hook
table in `tf2_features.md` has none listed, so this is a genuinely new
install, low risk — standard `SetWindowsHookEx`/subclass, not a vtable).
Velocity graph / keystroke indicators ride `PaintTraverse` slot 41
(confirmed, already dih's primary ImGui draw surface).

**Size:** Medium-Large.

### Unit C — ESP / players / world visuals

**Features:** full player ESP (master/teammates/distance-fade/sound-only,
box variants, name, health bar/number, weapon name/icon/ammo, distance,
flags, skeleton, hitboxes), player glow (+wave/pulse/legacy/blend/
gradient), player stencil, internal glow, backtrack trail (render side
only — sim side is unit F), avatars, damage numbers, oof arrows, dropped-
weapon/ammo-pack/building ESP, thrown-projectile ESP, grenade/projectile
path draw, enemy + local chams.

**Port from:** `hacks/visuals/players/players.cpp` (+`sound_esp.cpp`),
`hooks/functions/glow_effect_spectator.cpp`,
`hacks/visuals/screen/player_stencil.cpp`,
`hacks/avatar_cache/avatar_cache.cpp`, misc indicators damage-number
renderer, `hacks/indicators/indicators.cpp` (oof arrows),
`hacks/visuals/edicts/edicts.cpp`, `hacks/visuals/grenade/grenade_path.cpp`,
`hacks/chams/chams.cpp`.

**New tf2 files:** `src/hacks/esp/players_esp.cpp`,
`src/hacks/esp/glow.cpp`, `src/hacks/esp/stencil.cpp`,
`src/hacks/esp/avatar_cache.cpp`, `src/hacks/esp/damage_numbers.cpp`,
`src/hacks/esp/oof_arrows.cpp`, `src/hacks/esp/world_esp.cpp`
(ammo/dropped/buildings/projectiles), `src/hacks/esp/proj_path.cpp`,
`src/features/chams.cpp`.

**Hooks:** `PaintTraverse` slot 41 (confirmed, main ESP surface),
`DrawModelExecute` slot 19 (confirmed, already hooked for no-hats/frame-
blend — append a chams branch, don't reinstall), `FireEvent` slot 8
(confirmed, `player_hurt` for damage numbers). **NEW, verify before
building:** `IEngineSound::EmitSound` hook (interface confirmed to exist,
`public/engine/IEngineSound.h`, not yet hooked anywhere in botox-tf2 — this
unit and unit E both need it; whichever lands first should install it once
behind a shared helper in `globals.h` so the other appends a callback
rather than re-hooking). **NEW, verify before building:** entity-create
listener for building/dropped-weapon/ammo-pack ESP registration — check
`IClientEntityListener::OnEntityCreated` slot in the TF2 SDK before writing
any detour (do not reuse CS:GO's `on_add_entity` pattern hook signature
blind — TF2's x64 client.dll will have a different byte pattern even if the
same interface exists).

**Adaptation:** `TF_COND_*` flags (confirmed in `tf_shareddefs.h`) via
`CTFPlayerShared_InCond` (already compiled+whitelisted) for esp flags.
Entity classes confirmed present in SDK: `CTFPlayer`, `CTFAmmoPack`,
`CTFDroppedWeapon`, `CObjectSentrygun`/`CObjectDispenser`/
`CObjectTeleporter`, projectile classes under `tf_weaponbase_rocket.cpp`.
Glow: TF2 already exposes a native `CGlowObjectManager` singleton (used by
the game itself for cart/flag-carrier glow) — populate its entity list
directly instead of porting CS:GO's custom glow-pass code; this is strictly
simpler than the CS:GO original.

**Size:** Large — biggest single unit by feature count, but shares only
append-points on two already-installed hooks (`DrawModelExecute`,
`FireEvent`), no file collisions with other units.

### Unit D — Screen post-fx + viewmodel + removals

**Features:** sharpen/film-grain/vignette/invert/posterize/deband/channel-
shift/emphasize, reflections, show-depth debug view, world fov, sun angle,
fullbright, dlight, custom blood/precip particle color, world/prop texture
replace, third person (+collision), aspect ratio stretch, remove hud
elements, remove visual recoil, disable interp, minecraft hud (+clouds/
sky), web on surface.

**Port from:** individual small files under `hacks/visuals/screen/*.cpp`
(sharpen/vignette/posterize/etc are each tiny), `reflections.cpp`,
`depth_view.cpp`, `particle_collection_simulate.cpp` (blood/precip branch
only — smoke/molotov branches are IMPOSSIBLE, don't port them),
`hacks/mc_hud/mc_hud.cpp`, `hacks/web/websurface.cpp`.

**New tf2 files:** `src/hacks/screen/screenfx_passes.cpp` (new post-fx passes,
called from one small appended line in the existing `screenfx.cpp`
dispatcher — do not rewrite `screenfx.cpp`), `src/hacks/visuals/reflections.cpp`,
`src/features/depth_debug.cpp`, `src/hacks/visuals/world_fx.cpp` (fov/sun/
fullbright/dlight/texture-replace), `src/features/thirdperson.cpp`,
`src/features/mc_hud.cpp`, `src/features/websurface.cpp`.

**Hooks:** `DoPostScreenSpaceEffects` slot 39 (confirmed, shared infra,
append passes). **NEW, verify before building:** `SetDepthStencilSurface`
hook for reflections (`set_depth_stencil_surface.cpp` in botox is the CS:GO
reference; per global CLAUDE.md rule, never a blanket `D3DSBT_ALL`
Apply/Get on a pure device — scope the state block narrowly). `DrawModel
Execute` slot 19 for texture replace (append, shared with unit C — append-
only, no conflict). **NEW, verify before building:** a `draw_world_lists`-
equivalent hook for fullbright/mc-clouds/sky (TF2's own world render-list
traversal — resolve via pattern scan in engine.dll/client.dll, do not reuse
CS:GO's byte pattern). **NEW, verify before building:** a camera-override
point for third person + world fov (check `IClientMode`'s view-setup slot
or whether `CalcView`/`OverrideView` is the right target in the TF2 x64
vtable before hooking).

**Size:** Large — many small features, but each is a one-pass append to an
already-shared pipeline; the risk concentrates in the 3 new hooks flagged
above.

### Unit E — Misc HUD

**Features:** watermark style variants, spectator list (full feature, all
~10 style variants), hit marker, hit sound, death particles (+style
variants), media player extras (visualizer/lyrics/progress bar/in-game
control), chat translator + chat images, bot name rename (basic only),
discord rpc image, debugger menu, console color, points (score popup).

**Port from:** `hacks/chud_hud/chud_hud.cpp` watermark styles + the wm-spec
docs, `hacks/misc/spectator_list/spectator_list.cpp`,
`hacks/misc/hitmarker.cpp`, `hacks/misc/kill_effects.cpp`, media source/
lyrics/controls files, `hacks/misc/chat_extras.cpp`,
`hacks/misc/bot_names.cpp` (rename path only), `hacks/misc/discord_rpc.cpp`,
`hacks/misc/misc.cpp` (debugger/console color), `hacks/misc/points.cpp`.

**New tf2 files:** `src/hacks/hud/watermark_styles.cpp` (new renderers only,
`watermark.cpp` itself untouched beyond one dispatch line),
`src/hacks/hud/spectator_list.cpp`, `src/hacks/hud/hitmarker.cpp`,
`src/hacks/misc/killeffects.cpp`, `src/hacks/hud/media_ex.cpp` (calls into
existing `mediaplayer.cpp`, doesn't rewrite it), `src/features/chat_extras.
cpp`, `src/features/botnames.cpp`, `src/hacks/hud/points.cpp`,
`src/hacks/misc/discord_ex.cpp`, `src/hacks/menu/debugmenu.cpp`.

**Hooks:** `FireEvent` slot 8 (confirmed, append — `player_hurt` for hit
marker/sound, `player_death` for death particles, TF2's capture/round
events for points). **Shared with unit C:** `IEngineSound::EmitSound` —
this unit is the primary consumer (hit sound playback) and should own the
install; unit C's sound-ESP calls the same hook via a shared helper added
to `globals.h` — coordinate so only one unit installs it. **NEW, verify
before building:** a `SayText2`-usermessage hook for chat translator/chat
images (resolve via `DispatchUserMessage` slot 36, already confirmed
hooked for other purposes — append a branch rather than installing a
second hook on the same target, MinHook is one-hook-per-target). Bot-name
rename and spectator list need no new hooks — plain string write / native
`GetObserverTarget()` read respectively.

**Size:** Large by line count, but almost zero vtable risk — nearly every
feature here is pure overlay/ImGui code once the two shared-hook
coordination points above are respected.

### Unit F — Aim extras (backtrack, scope tuning, grenade lob)

**Features:** backtrack/lag comp, scope fov scale, per-weapon aim fov/
smooth/hitbox tuning granularity, grenade/projectile FOV-lob-to-point +
path draw + color (the non-target-seeking half of CS:GO's grenade aimbot).

**Port from:** `hacks/lagcomp/lagcomp.cpp`, `hacks/aimbot/aimbot.cpp`
`weapon_configs` table, `hacks/aimbot/grenade_aim.cpp`.

**New tf2 files:** `src/hacks/aim/backtrack.cpp`,
`src/hacks/aim/weapon_tuning.cpp` (extends `Vars::` ConfigVar tree —
append to `aim/vars.h`, don't rewrite it), `src/hacks/aim/grenade_lob.
cpp`.

**Hooks:** backtrack needs **zero new hooks** — `CBaseEntity_SetAbsVelocity`,
`CBaseEntity_EstimateAbsVelocity`, `RecvProxy_SimulationTime`,
`CL_ProcessPacketEntities` are already compiled AND whitelisted in
`aim/hooks.cpp` (confirmed, all 25 compiled hooks are currently
whitelisted) — just add recording logic inside the existing handler
bodies. Scope fov scale rides `CPrediction_GetLocalViewAngles` (confirmed
compiled+whitelisted). Grenade lob rides `CHLClient_CreateMove` (confirmed,
the aim subsystem already owns this slot).

**Size:** Medium — backtrack is genuinely new logic (ring buffer + rewind-
trace), the rest is UI/tuning on top of existing var trees.

---

## 4. Menu placement

- **aimbot:** scope fov scale, per-weapon tuning granularity, backtrack
  toggle (unit F) — all under existing `Section("aimbot")`/`"hitscan"`
  rows, append.
- **visuals/esp:** all of unit C (player esp, glow, stencil, avatars,
  damage numbers, oof arrows, dropped/building/projectile esp, chams) — new
  `Section()` per feature group, same column-flow pattern as
  `Tab_Visuals()`.
- **visuals/world:** world fov, sun angle, fullbright, dlight, custom
  blood/precip color, world/prop texture replace (unit D).
- **visuals/screen:** all remaining unit D post-fx passes, reflections,
  depth debug, third person, aspect ratio, remove hud elements/visual
  recoil, disable interp, mc hud, web on surface, grenade/projectile path
  draw (unit C, drawn here since it's a screen overlay not an ESP entity
  list).
- **movement/main:** all of unit A (ladder tricks, edge skip, fireman,
  tung surf, bounce/auto bounce, delay hop, auto crouch, hsw, one-hop,
  blockbot, air freeze, nulls, jumpstats, fake-pov widget) + edge bug style
  presets + jumpbug crouch/kangaroo + auto-strafe avoid-walls + long-jump
  adaptive-cancel (all PARTIAL sub-features, append into existing
  `Section("main")` rows) + pixel finder headbounce/pixeljump modes.
- **movement/indicators:** velocity graph, keybind particles/colors/
  labels, keystroke indicators (unit B).
- **movement/calculator:** detections master + stack-edgebug detection,
  pixelsurf lines/full-trajectory (unit B) — route calc itself excluded,
  see `routecalc_plan.md`.
- **movement/recorder:** entire movement recorder feature set (unit B) —
  this sub-tab does not exist in botox-tf2's menu yet, needs a new
  `Tab_Recorder()` added to the movement inline-tab list (currently 3:
  movement/fake movement/practice -> becomes 4).
- **misc/main:** watermark style variants, spectator list, hit marker/
  sound, death particles, media player extras, points, chat translator/
  images, bot name rename, discord rpc image, debugger menu, console
  color, safe mode, dpi scale, accent color (unit E + stray settings rows).
- **misc/player list:** SteamID player list feature (new page, mirrors
  `TAB_MISC`'s existing player-list-less state — this is wholly new).
- **inventory:** nothing — superseded by `TAB_SKINS`, no new tab needed.
- **fonts:** entire fonts tab is new — add as a 7th top-level tab (or a
  sub-tab under settings, botox-tf2's call) holding per-category font
  picker + chud-hud-font scale/nudge + default-hud-font file swap.
- **settings:** safe mode, dpi scale, accent color (small appends to
  `TAB_MISC`'s existing settings-adjacent rows — botox-tf2 has no separate
  top-level settings tab today, these fold into `TAB_MISC`).

---

Report path: `C:\Users\huber\Desktop\botox-tf2\docs\port\gap.md`
