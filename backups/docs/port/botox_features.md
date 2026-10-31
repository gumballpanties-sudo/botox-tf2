# botox (CS:GO) feature inventory for TF2 port

Source: `C:\Users\huber\Desktop\botox\csgo-sdk`. Menu tab order from `hacks/menu/menu.cpp:666`
(`tabs[]`): aimbot, visuals (esp/world/screen), movement (main/indicators/calculator/recorder),
misc (main/player list), inventory (weapons/player), fonts (indicators/esp/chud hud/default hud),
settings. Var names copied from `GET_VARIABLE( g_variables.XXX )` call sites in
`hacks/menu/menu.cpp` and `hacks/menu/menu_tab_*.cpp`; sub-option vars for deep features are
truncated to "+N more" — grep `globals/config/variables.h` for the full block (they're declared
contiguously after the parent var).

Hook abbreviations used below (all in `hooks/functions/*.cpp`): FSN=frame_stage_notify,
CMP=create_move_proxy, PM=process_movement, PT=paint_traverse, DME=draw_model_execute,
ES=end_scene, PRS=present, DSSE=do_post_screen_space_effects, OV=override_view, DVM=draw_view_models.

---

## TAB: aimbot (no subtabs) — `hacks/menu/menu.cpp:833 tab_aimbot()`

| feature | menu label | config vars | source file(s) + main func | hooks | CS:GO-only? | size |
|---|---|---|---|---|---|---|
| aimbot master | "aimbot master switch" | `m_aimbot_enable` | `hacks/aimbot/aimbot.cpp` `n_aimbot::run()` | CMP, FSN | no | M |
| aim on key | "aim on key" / "aim key" | `m_aimbot_on_key`, `m_aimbot_key` | aimbot.cpp | CMP | no | S |
| silent aim | "silent aim" / step | `m_aimbot_silent`, `m_aimbot_silent_step` | aimbot.cpp (angle rewrite, not sent to engine) | CMP, get_local_view_angles | no | M |
| target team | "target team" | `m_aimbot_target_team` | aimbot.cpp target filter | CMP | no — TF2 has teams too | S |
| scope fov scale | "scale fov with scope" / amount | `m_aimbot_scope_fov_scale`, `m_aimbot_scope_fov_scale_amount` | aimbot.cpp | CMP | yes — CS:GO sniper scope zoom model; TF2 zoom (sniper rifle) differs | S |
| per-weapon-class fov/smooth/rcs/hitboxes (general/pistol/heavy pistol/rifle/smg/sniper/scout/heavy) | "weapon settings" combo + per-class sliders | `m_aimbot_general_fov/smooth/rcs`, `m_aimbot_{pistol,heavy_pistol,rifle,smg,sniper,scout,heavy}_{fov,smooth,rcs,override}`, `m_aimbot_*_hitboxes` (vector\<bool\>, +~30 more vars) | aimbot.cpp weapon_configs table | CMP | yes — CS:GO weapon class taxonomy (pistol/smg/rifle/scout/awp/heavy) maps to nothing 1:1 in TF2; needs redesign per TF2 weapon class | L |
| rcs (recoil control) | "rcs start bullet/pitch/yaw", "rcs only with aimbot" | `m_aimbot_rcs_start`, `m_aimbot_rcs_pitch`, `m_aimbot_rcs_yaw`, `m_aimbot_rcs_only_with_aimbot` | aimbot.cpp | CMP | yes — CS:GO's fixed recoil pattern/spray; TF2 hitscan weapons have no comparable recoil pattern | M |
| autowall (wallbang damage aim) | "autowall" / "min damage" | `m_aimbot_autowall`, `m_aimbot_min_damage` | `hacks/auto_wall/auto_wall.cpp` | CMP | yes — CS:GO penetration/surface-prop model (`TraceLine` + material penetration tables); TF2 has no wallbang mechanic | L |
| backtrack | "backtrack" + "aim at backtrack"/"backtrack time"/"extended"/"extend amount" | `m_backtrack_enable`, `m_aimbot_aim_at_backtrack`, `m_backtrack_time_limit`, `m_backtrack_extend`, `m_backtrack_extend_amount` | `hacks/lagcomp/lagcomp.cpp` | FSN (record), CMP (apply) | no — lag comp exists in TF2 too, but offsets/slot differ | L |
| nospread | "nospread" + "sm_nospread" | `m_nospread_enable`, `m_nospread_sourcemod` | aimbot.cpp (weapon_accuracy_nospread convar poke + sourcemod remote path) | CMP | yes — CS:GO's `weapon_accuracy_nospread`/sourcemod plugin path; TF2 hitscan has different spread/crit RNG model | M |
| zeus bug | "zeus bug" / key / "swap back" | `m_zeusbug`, `m_zeusbug_key`, `m_zeusbug_swapback` | `hacks/indicators/indicators.cpp` (per grep) | CMP | yes — CS:GO Zeus x27 taser exploit; no TF2 equivalent | S |
| grenade aimbot | "grenade aimbot" + key, types, fov, arc, silent, auto throw, draw path+color | `m_grenade_aim`, `m_grenade_aim_key`, `m_grenade_aim_types` (vector\<bool\>×6), `m_grenade_aim_fov`, `m_grenade_aim_arc`, `m_grenade_aim_silent`, `m_grenade_aim_auto_throw`, `m_grenade_aim_draw`, `m_grenade_aim_color` | `hacks/aimbot/grenade_aim.cpp` | CMP | yes — CS:GO grenade set (flash/HE/smoke/molotov/decoy/TA grenade) and trajectory physics; TF2 projectiles (rocket/pipe/sticky/arrow) are a different port, not a grenade-aim reuse | L |

---

## TAB: visuals / SUBTAB: esp — `menu_tab_visuals.cpp:31 "players"` group onward

| feature | menu label | config vars | source file(s) + main func | hooks | CS:GO-only? | size |
|---|---|---|---|---|---|---|
| player esp master | "players" | `m_players` | `hacks/visuals/players/players.cpp` `draw()` | PT | no | M |
| esp: teammates filter | "include teammates" | `m_players_teammates` | players.cpp | PT | no | S |
| esp: distance fade | "fade with distance" | `m_players_distance_fade` | players.cpp | PT | no | S |
| esp: sound-only / sound fade | "only on sound", "sound fade" | `m_players_sound_only`, `m_players_sound_fade` | `hacks/visuals/players/sound_esp/sound_esp.cpp` | emit_sound, PT | no | M |
| esp box (2d/3d/corner/outline/fill/gradient) | "bounding box", "3d bounding box", "corner bounding box", "bounding box outline", "box fill", "gradient" | `m_players_box`, `m_players_box_3d`, `m_players_box_corner`, `m_players_box_outline`, `m_players_box_fill`, `m_players_box_fill_gradient` (+colors) | players.cpp box renderer | PT | no | M |
| esp name | "name" | `m_players_name` (+ color/shadow vars) | players.cpp | PT | no | S |
| esp health bar (+outline/gradient) | "health bar", "health bar outline", "gradient" | `m_players_health_bar`, `m_players_health_bar_outline`, `m_players_health_bar_gradient` | players.cpp | PT | no | S |
| esp health number (+suffix) | "health number", "health number suffix" | `m_players_health_text`, `m_players_health_suffix` | players.cpp | PT | no | S |
| esp weapon name/icon/ammo bar | "weapon name", "weapon icon", "weapon ammo bar" | `m_weapon_name`, `m_weapon_icon`, `m_player_ammo_bar` | players.cpp | PT | partial — icon art is CS:GO weapon set, needs TF2 weapon icon set | M |
| esp distance | "distance" | `m_players_distance` | players.cpp | PT | no | S |
| esp flags | "flags" | `m_players_flags` | players.cpp | PT | yes-ish — CS:GO player flags (defuser/bomb/scoped/flashed) differ from TF2 (ubered/cloaked/burning/bleeding) — redesign the flag set | M |
| player skeleton esp | "player skeleton" | `m_players_skeleton` | players.cpp bone draw | PT | no | M |
| show hitboxes | "show hitboxes" | `m_players_hitboxes` | players.cpp | PT | no — hitbox layout differs but concept ports | M |
| player glow (+wave/pulse/legacy/normal-blend/gradient) | "player glow" ... | `m_glow_enable`, `m_glow_wave`, `m_glow_pulse`, `m_glow_legacy`, `m_glow_normal_blend`, `m_glow_gradient` | `hooks/functions/glow_effect_spectator.cpp`, engine glow system | glow_effect_spectator, DME | no — Source engine glow (`CGlowObjectManager`) exists in TF2 too, slot differs | L |
| player stencil | "player stencil" | `m_players_stencil` | `hacks/visuals/screen/player_stencil.cpp` | PRS/ES, draw_model_execute | no | M |
| internal player glow (+follow bones) | "internal player glow", "follow bones" | `m_inner_glow`, `m_inner_glow_bones` | screen (inner glow shader pass) | DSSE | no | M |
| lag-comp backtrack trail | "lag compensated trail" | `m_players_backtrack_trail` | lagcomp.cpp + players.cpp | FSN, PT | no | M |
| player avatars (esp) | "player avatar" | `m_players_avatar` | `hacks/avatar_cache/avatar_cache.cpp` | level_init_post_entity (fetch), PT (draw) | yes — pulls Steam avatar via CS:GO's avatar cache/webapi path; TF2 side needs own Steam avatar fetch, otherwise portable | M |
| damage numbers (+stack) | "damage numbers", "stack fast hits" | `m_damage_numbers`, `m_damage_numbers_stack` | misc/indicators damage number renderer | fire_event_intern (player_hurt), PT | no | M |
| out-of-fov arrows (sound fade/avatars/aspect) | "sound arrow fade", "player avatars", "follow aspect ratio" | `m_out_of_fov_arrows_sound_fade`, `m_out_of_fov_arrows_avatar`, `m_out_of_fov_arrows_aspect` | `hacks/indicators/indicators.cpp` (oof arrows) | PT | no | M |
| dropped weapons esp (box/name/icon) | "dropped weapons" ... | `m_dropped_weapons`, `m_dropped_weapons_box`, `m_dropped_weapons_name`, `m_dropped_weapons_icon` | `hacks/visuals/edicts/edicts.cpp` | on_add_entity, PT | yes — TF2 doesn't drop guns on death the way CS:GO does; no equivalent entity | M |
| planted bomb esp (icon/countdown/timer bar) | "planted bomb", "icon", "countdown", "bomb timer bar" | `m_bomb_esp`, `m_bomb_esp_icon`, `m_bomb_esp_timer`, `m_bomb_timer_bar` | edicts.cpp | on_add_entity, PT | yes — C4/bomb entity is CS:GO-only (de_ maps) | M |
| thrown projectiles esp (name/icon) | "thrown projectiles" ... | `m_thrown_objects`, `m_thrown_objects_name`, `m_thrown_objects_icon` | edicts.cpp | on_add_entity, PT | partial — CS:GO grenade projectile classes; TF2 projectiles (rocket/pipe/sticky/flare/arrow) need own entity list | M |
| grenade path (+radius) | "grenade path", "effect radius" | `m_grenade_path`, `m_grenade_path_radius` | `hacks/visuals/grenade/grenade_path.cpp` | FSN (sim), PT (draw) | yes — CS:GO grenade arc/bounce sim; TF2 projectiles use different physics (gravity/bounce per weapon) | L |
| enemy chams (+ragdoll/sound-fade/backtrack xqz/gradient) | "enable enemy chams" ... | `m_chams_enable`, `m_chams_ragdolls`, `m_chams_sound_fade`, `m_chams_backtrack_xqz`, `m_chams_backtrack_gradient` | `hacks/chams/chams.h`/.cpp, material override | DME | no | L |
| local player chams | "enable local chams" | `m_chams_local_enable` | chams.cpp | DME | no | M |

---

## TAB: visuals / SUBTAB: world — `menu_tab_visuals.cpp:702`

| feature | menu label | config vars | source file(s) | hooks | CS:GO-only? | size |
|---|---|---|---|---|---|---|
| world fov | "world fov" | `m_world_fov_enable` (+amount var) | `hacks/visuals/screen/screen.*` | OV | no | S |
| precipitation (rain) toggle | "precipitation" | `m_precipitation` (+type/amount) | world render override | draw_world_lists | no | S |
| sun angle | "sun angle" | `m_sun_angle` | world lighting override | DSSE/FSN | no | S |
| fog | "fog" | `m_fog` | world render override | PRS/ES | no | S |
| fullbright | "fullbright" | `m_fullbright` | world render override | DME/draw_world_lists | no | S |
| flip world (+mirror hand) | "flip world", "gun other side" | `m_flip_world`, `m_flip_world_mirror_hand` | `hacks/visuals/screen/flip_world.cpp` | push_2d_view, world_to_screen_matrix, OV | no | M |
| dlight (+local player) | "dlight", "local player" | `m_dlight`, `m_dlight_local` | dynamic light override (muzzle flash etc.) | draw_model_execute / particle | no | S |
| custom particle colors (smoke/molotov/blood/precipitation) | "custom smoke/molotov/blood/precipitation color" | `m_custom_smoke`, `m_custom_molotov`, `m_custom_blood`, `m_custom_precipitation` (+ colors) | `hooks/functions/particle_collection_simulate.cpp` | particle_collection_simulate | yes — CS:GO smoke/molotov particle systems don't exist in TF2 (TF2 has no molotov/smoke grenade); blood ok, precipitation ok | M |
| old shaders (weapons/arms/players/world/props) | "old shaders" + per-category checks | `m_old_shaders`, `m_old_shaders_weapons`, `m_old_shaders_arms`, `m_old_shaders_players`, `m_old_shaders_world`, `m_old_shaders_props` | DME material swap | draw_model_execute | yes — targets CS:GO's PBR shader set vs legacy VertexLitGeneric; TF2 never moved to PBR so N/A | M |
| world texture replace (+remove decals) | "world texture", "remove decals" | `m_world_texture`, `m_world_texture_remove_decals` | DME/material override | draw_model_execute | no | M |
| prop texture replace | "prop texture" | `m_prop_texture` | DME material override + `proj-prop-texture-decal-1008` ($decaltexture neutralize) | draw_model_execute | no | M |
| bullet tracers (local/enemy/team) | "bullet tracers" ... | `m_bullet_tracers`, `m_bullet_tracers_local`, `m_bullet_tracers_enemy`, `m_bullet_tracers_team` | `hacks/visuals/bullets/bullets.cpp` | fire_event_intern (weapon_fire), PT | no | M |
| movement trail (+rainbow/air only) | "movement trail" ... | `m_movement_trail`, `m_movement_trail_rainbow`, `m_movement_trail_air_only` | movement trail renderer (proj-movement-trail-1005) | FSN, PT | no | M |
| bullet impacts | "bullet impacts" | `m_bullet_impacts` | bullets.cpp | fire_event_intern | no | S |
| world hit marker | "world hit marker" | `m_world_hit_marker` | bullets.cpp / indicators | fire_event_intern | no | S |

---

## TAB: visuals / SUBTAB: screen — `menu_tab_visuals.cpp:1024` (removals) through `:1687` (hud)

| feature | menu label | config vars | source file(s) | hooks | CS:GO-only? | size |
|---|---|---|---|---|---|---|
| remove panorama blur | "remove panorama blur" | `m_remove_panorama_blur` | panorama hook / `ref-csgo-hud-blur-target` | DSSE | yes — Panorama UI doesn't exist in TF2 (old GameUI engine) | S |
| remove 3d skybox | "remove 3d skybox" | `m_remove_3d_skybox` | draw_world_lists override | draw_world_lists | no | S |
| remove hud elements | "remove hud elements" | `m_remove_hud_elements` | scaleform/panorama hide | fire_event_intern / paint_traverse | partial — CS:GO HUD is Scaleform+Panorama; TF2 HUD is VGUI panels, different removal path | M |
| remove ragdolls/smoke/flash | "remove ragdolls", "remove smoke", "remove flash" | `m_remove_ragdolls`, `m_remove_smoke`, `m_remove_flash` | entity on-add filter | on_add_entity | partial — smoke/flash CS:GO-only; ragdoll removal ports | S |
| remove visual recoil | "remove visual recoil" | `m_remove_visual_recoil` | viewmodel punch override | CMP/get_local_view_angles | no | S |
| disable interp | "disable interp" | `m_disable_interp` | movement/convar poke | FSN | no | S |
| viewmodel offset / fov | "viewmodel offset", "viewmodel fov" | `m_viewmodel_offset_enable` (+xyz), `m_viewmodel_fov_enable` (+amount) | `hacks/visuals/viewmodel_bones.h` / draw_view_models | draw_view_models | no | M |
| weapon sway | "weapon sway" | `m_weapon_sway` | viewmodel bone override | draw_view_models | no | M |
| weapon sheen | "weapon sheen" | `m_weapon_sheen` | `hacks/visuals/weapon_sheen.cpp` | draw_indexed_primitive (DIP slot) | yes — reads CS:GO skin "sheen"/pattern shader params on weapon material; TF2 weapon materials have no sheen param set | L |
| viewmodel bob changer | "bob changer" | `m_viewmodel_bob` | viewmodel_bones.h | draw_view_models | no | M |
| third person (+wall collision) | "third person", "wall collision" | `m_third_person`, `m_third_person_collision` | camera override | OV | no | M |
| aspect ratio (+stretch hud/crosshair) | "aspect ratio", "stretch hud", "stretch crosshair" | `m_aspect_ratio_enable`, `m_aspect_ratio_hud`, `m_aspect_ratio_overlay` | `hacks/visuals/screen/resolution_spoof.cpp` | present, draw_filled_rect | no | L |
| color correction | "color correction" | `m_color_correction` (+many curve vars) | `hacks/visuals/screen/color_correction.cpp` | DSSE | no | M |
| sharpen | "sharpen" | `m_sharpen` | screen post fx | DSSE | no | S |
| film grain | "film grain" | `m_film_grain` | screen post fx | DSSE | no | S |
| vignette (+invert) | "vignette", "invert" | `m_vignette`, `m_vignette_invert` | `proj-vignette-1002` | DSSE | no | S |
| invert filter | "invert" | `m_invert_filter` | screen post fx | DSSE | no | S |
| posterize | "posterize" | `m_posterize` | screen post fx | DSSE | no | S |
| deband | "deband" | `m_deband` | screen post fx | DSSE | no | S |
| channel shift | "channel shift" | `m_channel_shift` | screen post fx | DSSE | no | S |
| reflections (+smooth normals) | "reflections", "smooth normals" | `m_reflections`, `m_reflections_smooth_normals` | `hacks/visuals/screen/reflections.cpp` | DSSE, set_depth_stencil_surface | no | L |
| emphasize (+spherical) | "emphasize", "spherical" | `m_emphasize`, `m_emphasize_spherical` | screen post fx | DSSE | no | M |
| show depth (debug view, +blend/live preview/upside down/reversed/log) | "show depth" ... | `m_show_depth`, `m_show_depth_blend`, `m_show_depth_live_preview`, `m_show_depth_upside_down`, `m_show_depth_reversed`, `m_show_depth_logarithmic` | `hacks/visuals/screen/depth_view.cpp`, `depth_source.cpp` | create_texture, set_depth_stencil_surface, DSSE | no | L |
| depth of field (+auto focus/smooth blur/near blur/debug/universal) | "depth of field" ... | `m_depth_of_field`, `m_depth_of_field_auto_focus`, `m_depth_of_field_smooth`, `m_depth_of_field_near_blur`, `m_depth_of_field_debug_depth`, `m_depth_force_live` | `hacks/visuals/screen/depth_of_field.cpp` | DSSE | no | L |
| ambient occlusion (+debug) | "ambient occlusion", "show occlusion" | `m_ambient_occlusion`, `m_ambient_occlusion_debug` | `hacks/visuals/screen/ambient_occlusion.cpp` | DSSE | no | L |
| motion blur (+forward) | "motion blur", "blur forward motion" | `m_motion_blur`, `m_motion_blur_forward` | screen post fx | DSSE | no | M |
| true motion blur (+players/viewmodel) | "true motion blur" ... | `m_true_motion_blur`, `m_true_motion_blur_players`, `m_true_motion_blur_viewmodel` | `proj-botox-true-motion-blur` v20 `span=` | DSSE, draw_model_execute | no — UNTESTED per memory | L |
| bloom | "bloom" | `m_bloom` | `hacks/visuals/screen/bloom.cpp` | DSSE | no | M |
| sniper crosshair (+dot/t style/outline) | "sniper crosshair" ... | `m_sniper_crosshair`, `m_sniper_crosshair_dot`, `m_sniper_crosshair_t`, `m_sniper_crosshair_outline` | crosshair draw (`ref-csgo-crosshair-two-paths`) | draw_filled_rect | yes — tied to CS:GO scoped-sniper crosshair state; TF2's sniper scope has no comparable crosshair widget (uses native scope overlay) | M |
| aimbot fov circle | "aimbot fov circle" | `m_aimbot_fov_circle` | aimbot.cpp render | PT | no | S |
| default/classic crosshair color override | "default crosshair color", "classic crosshair color", "classic crosshair outline color" | `m_panorama_crosshair_color`, `m_classic_crosshair_color`, `m_classic_crosshair_outline_color` | `ref-csgo-classic-static-crosshair-math` | draw_set_color/draw_filled_rect | yes — CS:GO has two parallel crosshair systems (panorama dynamic + classic static); TF2 crosshair is a single VGUI/HUD element | M |
| square radar | "square radar" | `m_square_radar` | panorama radar override | fire_event_intern | yes — CS:GO radar panel; TF2 has no minimap/radar | S |
| panorama hud color | "hud color" | `m_panorama_hud_color` | panorama color class override (`ref-panorama-color-classes`) | fire_event_intern | yes — Panorama-only, no TF2 equivalent UI system | S |
| keep killfeed (when scaleform hidden) | "keep killfeed" | `m_keep_killfeed` | scaleform.cpp killfeed passthrough | fire_event_intern | yes — CS:GO Scaleform killfeed; TF2 killfeed is VGUI | S |
| scaleform (stock hud passthrough toggle) | "scaleform" | `m_scaleform` | `hacks/misc/scaleform/scaleform.cpp` | fire_event_intern, load_file_into_buffer, set_image_data_r8g8b8a8 | yes — Scaleform/GFx is CS:GO's whole HUD renderer, does not exist in TF2 (VGUI only) | L |
| chud hud (custom hud, +blur/watermark bg/spectator list/dropshadow) | "chud hud" ... | `m_chud_hud`, `m_chud_hud_blur`, `m_chud_hud_watermark_bg`, `m_chud_hud_spectators`, `m_chud_hud_shadow` | `hacks/chud_hud/chud_hud.cpp` | end_scene, paint_traverse | no — custom overlay HUD, fully portable concept (own ImGui-drawn panel) | L |
| minecraft hud (+crosshair) | "minecraft hud", "minecraft crosshair" | `m_mc_hud`, `m_mc_hud_crosshair` | `hacks/mc_hud/mc_hud.cpp` | end_scene, draw_world_lists (clouds/sky) | no | L |
| web on surface (+volume distance fade) | "web on surface", "fade with distance" | `m_web`, `m_web_volume_distance` | `hacks/web/websurface.cpp`, `webview2host.cpp` | end_scene, draw_model_execute (surface placement) | no — WebView2 overlay concept ports, needs its own TF2 surface-pick logic (blockbot etc.) | L |

---

## TAB: movement / SUBTAB: main — `menu_tab_movement.cpp:205`

All of these are CS:GO movement-tech bugs (edgebug/bhop/pixelsurf family). Most are Source-engine
physics quirks that are either absent in TF2 (no sv_accelerate/sv_airaccelerate parity, no
duck-jump timing bug set) or need re-derivation against TF2's actual physics constants — flagged
per-row.

| feature | menu label | config vars | source file(s) + main func | hooks | CS:GO-only? | size |
|---|---|---|---|---|---|---|
| bunny hop (+chance) | "bunny hop", "success rate" | `m_bunny_hop`, `m_bunny_hop_chance` | `hacks/movement/movement.cpp` | PM, CMP | no — TF2 bhop is a known, well documented mechanic, but constants differ | M |
| delay hop (+key/ticks) | "delay hop" ... | `m_delay_hop`, `m_delay_hop_key`, `m_delay_hop_ticks` | movement.cpp | PM | no | S |
| edge jump (+key/ladder) | "edge jump" ... | `m_edge_jump`, `m_edge_jump_key`, `m_edge_jump_ladder` | movement.cpp (`ref-post-createmove-premove-frame`) | PM, CMP | no | M |
| long jump (+adaptive cancel/edge jump) | "long jump" ... | `m_long_jump`, `m_long_jump_key`, `m_long_jump_adaptive`, `m_long_jump_adaptive_stop` (vector), `m_long_jump_adaptive_time`, `m_long_jump_edge_jump`, `m_long_jump_edge_jump_ladder` | movement.cpp | PM | yes — CS:GO longjump duck-release timing is CS:GO-specific; TF2 has no "long jump" bug analog (closest = scout double jump, not a bug) | L |
| mini jump (+edge jump/hold crouch) | "mini jump" ... | `m_mini_jump`, `m_mini_jump_key`, `m_mini_jump_edge_jump`, `m_mini_jump_hold_duck` | movement.cpp | PM | yes — same, CS:GO specific duck timing bug | M |
| jump bug (+crouch/kangaroo) | "jump bug" ... | `m_jump_bug`, `m_jump_bug_key`, `m_jump_bug_crouch`, `m_jump_bug_kangaroo` (`proj-jb-kangaroo` UNTESTED) | movement.cpp | PM | yes — CS:GO jumpbug exploit timing; needs re-derivation for TF2 | L |
| auto crouch (+ticks) | "auto crouch" ... | `m_auto_crouch`, `m_auto_crouch_key`, `m_auto_crouch_ticks` | movement.cpp | CMP | no | S |
| auto strafe (standard/WASD/circle mouse +avoid walls) | "auto strafe" ... | `m_auto_strafe`, `m_auto_strafe_key`, `m_auto_strafe_type`, `m_auto_strafe_last_keys`, `m_auto_strafe_last_mouse`, `m_auto_strafe_avoid_walls`, `m_auto_strafe_avoid_dist` | `proj-autostrafe-clarity-0929` | PM, CMP | no — air strafe exists in TF2 too (same QuakeWorld-derived accel model) | L |
| strafe optimizer (+gain/min speed) | "strafe optimizer" ... | `m_strafe_optimizer`, `m_strafe_optimizer_gain`, `m_strafe_optimizer_min_speed` | `proj-strafe-optimizer` | PM | no | M |
| ladder bug / ladder glide / ladder freelook climb / fast ladder | 4 checkboxes + keys | `m_ladder_bug`(+key), `m_ladder_glide`(+key), `m_ladder_freelook_climb`(+key), `m_fast_ladder`(+key) | movement.cpp | PM | partial — Source ladder physics shared, but exact exploit timing is CS:GO-tuned; needs TF2 ladder re-derivation | M each |
| edge bug (3 styles: botox/delusional/dna, +mouse fix/advanced detection/autostrafe/mouse lock/sound/detection mode) | "edge bug" + big OptionPopup | `edge_bug`, `edge_bug_key`, `m_edgebug_style`, `m_edgebug_del_*` (6), `m_edgebug_dna_*` (4), `m_edgebug_predict_time`, `m_edgebug_autostrafe_to_edge`, `m_edgebug_autostrafe_max_angle`, `m_edgebug_mouse_steer`, `m_edgebug_paths`, `m_edgebug_mouse_lock*` (3), `m_edge_bug_sound*` (3), `m_edgebug_detection_mode`, `m_edgebug_search_amount` (~25 vars total) | `hacks/movement/edgebug.cpp` | PM, override_mouse_input | yes — edgebug is a lip/edge geometry exploit tuned to CS:GO map geometry and physics constants (`proj-tb-rewrite-1001`, `proj-eb-restore-0928-1011`); needs full TF2 re-derivation, flagged UNTESTED even in botox itself | L |
| edge skip (+use-eb-settings/predict/bend/search/mouse lock/sound/visualize) | "edge skip" ... | `m_edgebug_edge_skip`, `m_edge_skip_key`, `m_edge_skip_use_eb`, `m_edge_skip_predict_time`, `m_edge_skip_max_angle`, `m_edge_skip_search_amount`, `m_edge_skip_mouse_lock*`, `m_edge_skip_sound*`, `m_edge_skip_visualize` (~12 vars) | `hacks/movement/edge_skip.cpp` | PM | yes — same geometry-exploit family as edgebug | L |
| pixel surf (+key/sound) | "pixel surf" ... | `m_pixel_surf`, `m_pixel_surf_key`, `m_pixel_surf_sound*` (3) | `hacks/movement/pixelsurf_assist.cpp` (partially), `route_engine.cpp` | PM | yes — pixelsurf is a 1-unit-precision Source surf-ramp exploit; needs TF2 ramp/ramp-accel constants re-derived (`ref-csgo-vertical-sim`) | L |
| tung surf (local y6 ast, +y6 assist/snap/press into wall) | "tung surf" ... | `m_tung_surf`, `m_tung_surf_key`, `m_tung_surf_y6`, `m_tung_surf_snap`, `m_tung_surf_press` | `proj-tung-surf` (client fake ps v16, UNTESTED, online=y6 only) | PM | yes — explicitly CS:GO-tuned fake-pixelsurf, memory flags UNTESTED even there | L |
| pixelsurf assist (+key/point key/ticks/type/stamina/ground help/show points/render) | "pixelsurf assist" ... | `m_pixel_surf_assist`, `m_pixel_surf_assist_key`, `m_pixel_surf_assist_point_key`, `m_pixel_surf_assist_ticks`, `m_pixel_surf_assist_type`, `m_pixel_surf_assist_brokehop`, `m_pixel_surf_assist_ground_help`, `m_pixel_surf_assist_ground_angle_limit`, `m_pixel_surf_assist_show_points`, `m_pixel_surf_assist_render` | `hacks/movement/pixelsurf_assist.cpp` | PM | yes — `proj-psa-lip-probe-1009` UNTESTED, pixelsurf-specific | L |
| bounce assist (+stamina/render) | "bounce assist" ... | `m_bouncee_assist`, `m_bouncee_assist_brokehop`, `m_bouncee_assist_render` | `hacks/movement/bounce_assist.cpp` | PM | yes — ties to CS:GO stamina (`m_flStamina`) bhop-chance model; TF2 has no stamina stat | M |
| auto bounce (+jump/crouch jump/auto align/visualize) | "auto bounce" ... | `m_auto_bounce`, `m_auto_bounce_jump`, `m_auto_bounce_crouch`, `m_auto_bounce_align`, `m_auto_bounce_visualize` | `hacks/movement/auto_bounce.cpp` | PM | yes — `proj-botox-auto-bounce` `snap=` UNTESTED, tied to CS:GO bounce surfaces | M |
| texture bug (+wallstrafe) | "texture bug", "wallstrafe" | `m_texture_bug`, `m_texture_bug_wallstrafe` | `hacks/movement/texturebug.cpp`, `texturebug_wallstrafe.h` | PM | yes — `proj-tb-wallstrafe-1005` push450 port, CS:GO ramp/texture-angle exploit | L |
| air stuck (+fix air stucks/screen message) | "air stuck" ... | `m_air_stuck`, `m_fix_air_stucks`, `m_air_stuck_display_screen` | `hacks/movement/air_stuck.cpp` | PM | yes — `proj-air-stuck-singlesim-catch` `rob=`; Source air-stuck state tied to CS:GO collision response | M |
| wall climb (+prevent slow/crouch catches/turn view off wall/show wall slope) | "wall climb" ... | `m_wall_climb`, `m_wall_climb_prevent_slow`, `m_wall_climb_duck`, `m_wall_climb_visual_angles`, `m_show_wall_slope` | `hacks/movement/wall_climb.cpp` | PM | yes — ported from "ebanat" CS:GO exploit, `proj-botox-wallclimb` `flush=` UNTESTED | L |
| fireman (+approach/drop in/lock/crouch jump/sides only/push off early) | "fire man" ... | `m_fire_man`, `m_fire_man_approach`, `m_fire_man_drop_in`, `m_fire_man_drop_in_lock`, `m_fire_man_crouch_jump`, `m_fire_man_side_only`, `m_fire_man_push_off` | `hacks/movement/fireman.cpp` | PM | yes — `proj-botox-fireman` passes 34-65 UNTESTED, CS:GO geometry-specific | L |
| air freeze | "air freeze" | `m_air_freeze` | `proj-air-freeze-1008` `m_air_freeze` | PM | no | S |
| 128 tick fix | "128 tick fix" | `m_tick_fix_128` | movement.cpp tickrate compensation | PM, FSN | yes — CS:GO server tickrate split (64/128); TF2 servers are fixed 66/100 tick, no 128 case | S |
| movement fix | "movement fix" | `m_movement_fix` | movement.cpp general fix | PM | no — generic, needs own audit per port | S |
| smooth demo view | "smooth demo view" | `m_silent_view` | `proj-silent-view-1005` `VIEW:` UNTESTED | get_local_view_angles, OV | no | M |
| pixel surf fix | "pixel surf fix" | `m_pixel_surf_fix` | movement.cpp / route_engine.cpp | PM | yes — pixelsurf-specific | S |
| auto align | "auto align" | `m_auto_align` | `hacks/movement/auto_align.cpp` | PM | no — generic eye/ground snap, portable | M |
| nulls (opposing key cancel) | "nulls" | `m_nulls` | movement.cpp input filter | CMP | no | S |
| half sideways bind (+air W/S, pre-onground, ground A/D) | "half sideways bind" ... | `m_hsw`, `m_hsw_allow_w_s`, `m_hsw_pre_onground`, `m_hsw_allow_a_d` | `proj-hsw-1004` mv_hsw 1:1, gate hsw_check.py | CMP | no | M |
| auto one hop | "auto one hop" | `m_auto_one_hop` | movement.cpp | PM | no | S |
| blockbot | "blockbot" | `m_blockbot` | `hacks/misc/misc.cpp` (PRE before pixel_calc) | PM, CMP | no — concept (block exploit surface placement helper) portable but logic needs TF2 surface rules | M |
| fast stop | "fast stop" | `m_fast_stop` | `ref-csgo-fast-stop` tiny opposite wish; accel floor max(250,wish) | CMP | no — accel floor constant (250) is CS:GO sv_accelerate-tuned, needs TF2 constant | S |
| pixel finder (+pixelsurfs/texturebugs/headbounces/pixeljumps) | "pixel finder" ... | `m_pixel_finder`, `m_pixel_finder_pixelsurfs`, `m_pixel_finder_texturebugs`, `m_pixel_finder_headbounces`, `m_pixel_finder_pixeljumps` | `hacks/movement/pixel_finder.cpp`, `pixel_finder_scan.h` | PM, draw_world_lists (geometry scan) | yes — `proj-pixel-finder-v6-1007` RULE hull_plane_dist; entire finder targets CS:GO movement-exploit geometry classes | L |
| pixel calculator (+show point/advanced readout) | "pixel calculator" ... | `m_pixel_calc`, `m_pixel_calc_show_point`, `m_pixel_calc_advanced_readout` | `hacks/movement/pixel_calc.cpp` | PM | yes — pixel-precision surf calc, CS:GO-ramp specific | M |
| distance calculator | "distance calculator" | `m_dist_calc` | `hacks/movement/dist_calc.cpp` | PM, world_to_screen_matrix | no — generic w2s distance tool, `proj-dist-calc-crash-1008` note on clip/cap | M |
| practice window (+show window) | "practice window" ... | `m_practice_window`, `m_practice_window_show` | misc overlay window | paint_traverse | no | S |
| fake pov in demos (+snap view/arrow) | "fake pov in demos" ... | `m_fake_pov`, `m_fake_pov_snap_view`, `m_fake_pov_arrow` | `hacks/misc/fake_pov.cpp` IPrediction slot 12 | OV, is_playing_demo | no — demo playback feature, portable to TF2 demo system | M |
| hide y6o ps | "hide y6o ps" | `m_hide_y6o_ps` | `proj-hide-y6o-ps` msg 38 | dispatch_user_message | yes — hides a specific CS:GO usermessage artifact from pixelsurf exploit; N/A without the exploit it's hiding | S |
| jumpstats (+show fails) | "jumpstats" ... | `m_jump_stats`, `m_jump_stats_show_fails` | `proj-bot-names-jumpstats-0929` | PM, fire_event_intern (jump event) | partial — jump distance/type stats concept portable, CS:GO jump-type taxonomy (longjump/bunnyhop/ladderjump) needs TF2 remap | M |

---

## TAB: movement / SUBTAB: indicators — `menu_tab_movement.cpp:841`

| feature | menu label | config vars | source file(s) | hooks | CS:GO-only? | size |
|---|---|---|---|---|---|---|
| velocity indicator (+fade/shadow/outline, many layout vars) | "velocity indicator" ... | `m_velocity_indicator`, `m_velocity_indicator_fade_alpha`, `m_velocity_indicator_shadow`, `m_velocity_indicator_outline` (+~10 layout/color vars) | `hacks/indicators/indicators.cpp` | CMP (read speed), paint_traverse | no | M |
| stamina indicator (+fade/shadow/outline) | "stamina indicator" ... | `m_stamina_indicator`, `m_stamina_indicator_fade_alpha`, `m_stamina_indicator_shadow`, `m_stamina_indicator_outline` | indicators.cpp | CMP, paint_traverse | yes — reads CS:GO `m_flStamina` netvar; TF2 has no stamina stat (closest: nothing) | M |
| velocity graph (+gain colors/fill/fade sides/jump speeds/text shadow) | "velocity graph" ... | `m_velocity_graph`, `m_velocity_graph_gain_colors`, `m_velocity_graph_fill`, `m_velocity_graph_fade_sides`, `m_velocity_graph_jump_speeds`, `m_velocity_graph_text_shadow` | `proj-velocity-graph-jump-units-1002` | CMP, paint_traverse | no | M |
| keybind indicators (+horizontal/shadow/detect-on-shadow/outline/particles/random colors/custom labels) | "keybind indicators" ... | `m_key_indicators_enable`, `m_key_indicators_horizontal`, `m_key_indicators_shadow`, `m_key_indicators_shadow_detect`, `m_key_indicators_outline`, `m_key_indicators_particles`, `m_key_indicators_particle_random`, `m_key_indicators_custom_labels` (+label text array) | `proj-key-detect-particles-1002`, `proj-indicator-styles` | wndproc (key state), paint_traverse | no | L |
| keystroke indicators (+duck/jump, mouse direction, shadow, outline) | "keystroke indicators" ... | `m_key_press_indicator`, `m_key_press_duck_jump`, `m_key_press_mouse`, `m_key_press_shadow`, `m_key_press_outline` | `proj-key-press-indicator` | wndproc, paint_traverse | no | M |

---

## TAB: movement / SUBTAB: calculator — `menu_tab_movement.cpp:59` (route calculator page) + `:1202` detections

| feature | menu label | config vars | source file(s) | hooks | CS:GO-only? | size |
|---|---|---|---|---|---|---|
| route calculator (+start-here/allow-invalid/snap/show keys/show bar/advanced readout/popup time/max results/delay hop ticks/jump types) | "route calculator" group | `m_route_calc`, `m_route_calc_start_here`, `m_route_calc_allow_invalid`, `m_route_calc_snap_edge`, `m_route_calc_show_keys`, `m_route_calc_show_bar`, `m_route_calc_advanced_readout`, `m_route_calc_popup_time`, `m_route_calc_max_results`, `m_route_calc_delay_ticks`, `m_route_calc_global_moves`, `m_route_calc_start_styles` | `hacks/movement/route_calc.cpp`, `route_engine.cpp` | PM (sim), paint_traverse (draw) | yes — simulates CS:GO movement physics ahead of time (jump/longjump/minijump/jumpbug/walk-off combos tied to pixelsurf/texturebug/edgebug/headbounce point types); whole engine needs re-derivation against TF2 physics constants, explicitly a multi-thousand-line port | L |
| route calc keybinds (add/solve/delete/clear) | "keys" group | `m_route_calc_add_key`, `m_route_calc_solve_key`, `m_route_calc_delete_key`, `m_route_calc_clear_key` | route_calc.cpp | wndproc | no | S |
| route calc points list/editor | "points" group (listbox, move up/down/delete, point type combo, per-point styles) | point data lives in `n_route::g_route` struct, not ADD_VARIABLE (runtime state) | route_calc.cpp | PM | yes — point types (ground/pixelsurf/edgebug/headbang/texturebug/pixeljump/headbounce) are the CS:GO exploit taxonomy | L |
| detections master | "detections" | `m_detections` | `hacks/movement/movement.cpp` detection dispatch | PM | no — concept (announce when exploit executes) portable, individual detections below are CS:GO-specific | S |
| detection: stack edgebugs | "stack edgebugs##detstack" | `m_detection_stack_eb` | movement.cpp | PM | yes — edgebug-specific | S |
| edgebug visualize (+project onto geometry) | "edgebug visualize", "project onto geometry" | `m_edgebug_visualize`, `m_edgebug_visualize_projected` | edgebug.cpp render | PT | yes | S |
| pixelsurf lines (+full trajectory) | "pixelsurf lines", "full trajectory" | `m_pixel_surf_line_render`, `m_pixel_surf_line_full_trajectory` | pixelsurf render | PT | yes | S |

---

## TAB: movement / SUBTAB: recorder — `menu_tab_movement.cpp:1289`

Per memory: `proj-movement-recorder-1009` is `REC:` and marked UNTESTED in botox itself.

| feature | menu label | config vars | source file(s) + main func | hooks | CS:GO-only? | size |
|---|---|---|---|---|---|---|
| movement recorder (+show line/show timer) | "movement recorder" ... | `m_movement_rec`, `m_movement_rec_show_line`, `m_movement_rec_render` | `hacks/movement/movement_recorder.cpp` | PM, CMP (record/playback inject), wndproc, lock_cursor | no — generic input-record/playback, fully portable | L |
| clipper (+show clipper box) | "clipper", "show clipper box" | `m_movement_rec_clipper`, `m_movement_rec_clipper_box` | movement_recorder.cpp | PM | no | M |
| original playback viewangles | "original playback viewangles" | `m_movement_rec_lockva` | movement_recorder.cpp | CMP | no | S |
| lock while aiming to position | "lock while aiming to position" | `m_movement_rec_lockgoingtostart` | movement_recorder.cpp | CMP | no | S |
| stop playback on movement | "stop playback on movement" | `m_movement_rec_stop_on_move` | movement_recorder.cpp | CMP | no | S |
| force same weapons | "force same weapons" | `m_movement_rec_force_weapon` | movement_recorder.cpp | run_command | yes-ish — CS:GO weapon-equip path; TF2 weapon slots differ, needs remap | S |
| recorder actions (save/load/record/play buttons) | "actions" group buttons | runtime-only (no ADD_VARIABLE, file-backed) | movement_recorder.cpp (load/save) | — | no | M |
| recorder routes list | "routes" group (listbox of saved routes) | file-backed list, not config vars | movement_recorder.cpp | — | no | S |

---

## TAB: misc / SUBTAB: main — `menu_tab_settings.cpp:989 tab_misc()` (groups: overlays, game, convenience)

| feature | menu label | config vars | source file(s) | hooks | CS:GO-only? | size |
|---|---|---|---|---|---|---|
| watermark (+style, text, user/tickrate/fps/time/ping/velocity toggles, 11 style variants: skebob/dna/lumi/gif/kami/etc.) | "watermark" group, `m_watermark_style` combo | `m_watermark`, `m_watermark_style`, `m_watermark_text`, `m_watermark_user`, `m_watermark_tickrate`, `m_watermark_fps`, `m_watermark_time`, `m_watermark_ping`, `m_watermark_velocity`, plus ~40 style-specific vars (`m_watermark_skebob_*`, `m_watermark_dna_*`, `m_watermark_lumi_*`, `m_watermark_gif_*`, `m_watermark_kami_*`, `m_watermark_clarity_user`, `m_watermark_evolve_alpha`, `m_watermark_havoc_color`, `m_watermark_interium_bg`) | `hacks/chud_hud/chud_hud.cpp` watermark draw, `proj-lumi-wm-spec-1002` and sibling wm-spec docs | end_scene, paint_traverse | no — all overlay panels drawn in own ImGui/raw-panel code, fully portable; per-style ports happen one at a time per `feedback-new-wm-spec-same-knobs` | L |
| spectator list (+style combo with ~10 variants: inba/skebob/lumi/kamibebra/airplane/bhopcheat/clarity v2, rainbow/background/title/glow/blur/mascot/gif/bold/shadow/colors) | "spectator list" group | `m_spectators_list`, `m_spectators_list_style`, `m_spectators_list_type`, `m_spectators_list_text_color_one/two`, `m_spectators_inba_rainbow(+speed)`, `m_spectators_skebob_background/title`, `m_spectators_lumi_*` (type/hide_gotv/glow/blur/mascot+path+size), `m_spectators_kamibebra_*` (~15 vars incl. gif), `m_spectators_airplane_*` (5), `m_spectators_bhopcheat_mode`, `m_spectators_avatar`, `m_spectators_always_show`, `m_spectators_list_always_show`, `m_spectators_list_show_target`, `m_spectators_list_width`, `m_spectators_chillware_max_width`, `m_spectators_list_delusional_left`, `m_spectators_list_interwebz_background/label_x` | `hacks/misc/spectator_list/spectator_list.cpp`, `airflow_data.h`, `kamibebra_gif_data.h` | paint_traverse | yes — reads CS:GO spectator/HLTV observer list (`is_hltv`); TF2 spectator target list differs structurally but overlay rendering ports once the player list source is swapped | L |
| media player (+simple/cover art/background/progress bar, visualizer smooth+custom color, lyrics+rows+offset, precise time, in-game control+keys) | "media player" group | `m_media_player`, `m_media_player_position`, `m_media_player_simple`, `m_media_player_cover_art`, `m_media_player_background`, `m_media_player_progress_bar`, `m_media_player_visualizer`, `m_media_player_visualizer_height`, `m_media_player_visualizer_smooth`, `m_media_player_visualizer_bar_width`, `m_media_player_visualizer_custom_color(+color)`, `m_media_player_lyrics(+rows/offset)`, `m_media_player_time`, `m_media_player_ingame_control(+prev/toggle/next keys)` | `hacks/misc/discord_rpc.cpp`-adjacent media source readers, `proj-media-sources-1006`, `media-wave-source-1005`, `media-lyrics-1003`, `proj-media-controls-1010` | wndproc (media keys), paint_traverse | no — reads local OS media session (Windows SMTC), nothing CS:GO-specific; fully portable | L |
| points (score popup, +big font, appear speed/speed/offsets) | "points" group | `m_points`, `m_points_big_font`, `m_points_appear_speed`, `m_points_speed`, `m_points_x_offset`, `m_points_y_offset` | `hacks/misc/points.cpp`, `hacks/movement/points_ui.cpp` | fire_event_intern (kill/round events), paint_traverse | yes — CS:GO round economy/score popups tied to CS:GO scoring events (kill reward, round win); needs TF2 scoring-event remap (frags, caps, etc.) | M |
| low fps warning | "low fps warning" | `m_fps_warning` | misc perf monitor | FSN | no | S |
| hit marker (+outline, size/speed/thickness/color) | "hit marker" ... | `m_hit_marker`, `m_hit_marker_outline`, `m_hit_marker_size`, `m_hit_marker_speed`, `m_hit_marker_thickness`, `m_hit_marker_color`, `m_hit_marker_outline_color`, `m_hit_marker_outline_thickness` | `hacks/misc/hitmarker.cpp` | fire_event_intern (player_hurt) | no | M |
| hit sound (+type incl. custom, volume) | "hit sound" ... | `m_hit_sound`, `m_hit_sound_type`, `m_hit_sound_custom`, `m_hit_sound_volume` | hitmarker.cpp | fire_event_intern, emit_sound | no | M |
| healthshot effect (+triggers, duration) | "healthshot effect" ... | `m_healthshot_effect`, `m_healthshot_triggers`, `m_healthshot_duration` | `proj-kill-effects-0929` family | fire_event_intern | yes — CS:GO healthshot item is CS:GO-only (no medkit-use event in TF2); TF2 equivalent would be medic heal/overheal, different event | M |
| death particles (+type incl. melt/soul-style, multicolor, glow, melt goo+speed, amount, size, lifetime, mc mode+xp time) | "death particles" group | `m_death_particles`, `m_death_particles_type`, `m_death_particles_mc_mode(+mc_xp_time)`, `m_death_particles_soul_style`, `m_death_particles_multicolor(+color)`, `m_death_particles_glow`, `m_death_particles_melt_goo(+speed)`, `m_death_particles_amount`, `m_death_particles_size`, `m_death_particles_lifetime` | `hacks/misc/kill_effects.cpp`, `proj-melt-death-1005`, `proj-kfx-world-1009`, `mc_xp_orb.h` | fire_event_intern (player_death) | no — death-particle burst is generic, `player_death` event exists in TF2 too | L |
| console color (+color) | "console color" ... | `m_console_color_enable`, `m_console_color` | console color override | tier0_warning / console hooks | no | S |
| log types filter | (combo, no dedicated checkbox) | `m_log_types` | debug logging config | — | no | S |
| clantag (+animation style, text, frames/speed/hold) | "clantag" group | `m_clantag`, `m_clantag_animation`, `m_clantag_text`, `m_clantag_frames`, `m_clantag_speed`, `m_clantag_hold` | `proj-clantag-1004`, 10-10 anim "4 now playing" | run_command/send_net_msg (clan tag field) | yes — CS:GO clan tag is a server-visible player field (`m_szClan`); TF2 has no clan-tag field, would need a different presentation surface (name prefix) | M |
| chat translator | "chat translator" | `m_chat_translator`, `m_chat_translator_language` | `hacks/misc/chat_extras.cpp` | dispatch_user_message | no — chat exists in TF2 too, translator logic is generic | M |
| chat images | "chat images" | `m_chat_images` | chat_extras.cpp | dispatch_user_message, push_notice | partial — relies on CS:GO chat usermessage format; TF2 chat message hook differs but concept ports | M |
| bot names (+mode, same, length, no prefix, random ping, random rank+medal) | "bot names" group | `m_bot_names`, `m_bot_names_mode`, `m_bot_names_same`, `m_bot_names_length`, `m_bot_names_no_prefix`, `m_bot_names_ping`, `m_bot_names_profile` | `hacks/misc/bot_names.cpp` | pre_client_update (`bot_names_server_write`) | yes — CS:GO bot naming + CS:GO rank/medal cosmetic fields; TF2 bots have no comparable rank system | M |
| auto revive (+mode) | "auto revive" ... | `m_auto_revive`, `m_auto_revive_mode` | `proj-...` (not yet grepped individually) | run_command / fire_event_intern | yes — CS:GO revive (Danger Zone / Retakes plugin) mechanic; TF2 has no player-revive action for self | M |
| deagle spinner (+key) | "deagle spinner" ... | `m_deagle_spinner`, `m_deagle_spinner_key` | `proj-deagle-spin-server-1006` | draw_model_execute / CMP (viewmodel spin) | yes — specific to the CS:GO Desert Eagle model; TF2 has no deagle, would need a different weapon model target | S |
| pure bypass | (checkbox not captured by grep pattern, var present) | `m_pure_bypass` | `hooks/functions/sv_pure.cpp` loose_files_allowed | sv_pure | yes — targets CS:GO's `sv_pure`/pure-server file-integrity check; TF2 has its own sv_pure, logic concept ports but call site differs | S |
| performance (threaded bones, preload assets, skip unused records, multicore render, high priority, fast cores, no focus sleep) | "performance" group | `m_performance`, `m_performance_threaded_bones`, `m_performance_force_preload`, `m_performance_skip_unused_records`, `m_performance_multicore_render`, `m_performance_high_priority`, `m_performance_fast_cores`, `m_performance_no_focus_sleep` | `proj-perf-bench-1008` (measured 0% gain, bench removed), `proj-mitosis-perf-0929` | FSN, OS-level (priority/affinity) | no — engine-level perf toggles, concept portable (some no-ops per botox's own measurement) | M |
| discord rpc (+image) | "discord rpc" ... | `m_discord_rpc`, `m_discord_rpc_image` | `hacks/misc/discord_rpc.cpp` | FSN (status update) | no — Discord RPC is engine-agnostic | M |
| debugger menu | "debugger menu" | `m_debugger_visual` | debug overlay | paint_traverse | no | S |

---

## TAB: misc / SUBTAB: player list — `menu_tab_settings.cpp:888 player_list_page()`

| feature | menu label | config vars | source file(s) | hooks | CS:GO-only? | size |
|---|---|---|---|---|---|---|
| player list (per-SteamID revive/ignore/only flags, persisted to file) | "player list" page (listbox + per-row checkboxes) | not ADD_VARIABLE — file-backed (`player_list_entry_t` struct), path `C:\botox\player_list.txt` equivalent (`proj-player-list-1003`) | `hacks/misc/misc.cpp` `player_list_load/save/stamp` | on_add_entity (player join), pre_client_update | no — SteamID-keyed list is engine-agnostic, fully portable | M |

---

## TAB: inventory / SUBTAB: weapons — `menu_tab_settings.cpp:214 tab_inventory(), case 0`

Entire subtab is CS:GO's skin/sticker/paint-kit changer system. **100% CS:GO-only** — TF2 has
its own, structurally unrelated item/paintkit system (schema-driven, not CS:GO's VMT paint-kit
float-array model). Do not port 1:1; if a skinchanger is wanted for TF2 it is a new feature against
TF2's item schema, not a translation of this code.

| feature | menu label | config vars | source file(s) | hooks | CS:GO-only? | size |
|---|---|---|---|---|---|---|
| weapon skins (per-weapon paint kit, same-weapons-both-teams toggle, per-team override, wear/seed/pattern, sticker slots×4 with wear/scale/rotation) | "weapons" group | `m_skins_same_weapons`, `m_weapon_skins_selected` (array), `m_weapon_skins_sticker_slot` (+per-slot wear/scale/rotation, ~dozens of vars), plus `_tside` twin vars per `proj-skins-team-sets-1010` | `hacks/skins/skins.cpp`, `skins_changers.cpp`, `parser.cpp`, `skin_names.h`, `sticker_names.h` | find_mdl, random_int (seed), set_visuals_data | yes | L |
| same skins on both teams toggle | "same skins on both teams" | `m_skins_same_weapons` / `m_skins_same_player` | skins.cpp | — | yes | S |

## TAB: inventory / SUBTAB: player — `menu_tab_settings.cpp` case 1 (agents/knife/gloves)

| feature | menu label | config vars | source file(s) | hooks | CS:GO-only? | size |
|---|---|---|---|---|---|---|
| knife changer | "knife" group | knife-equivalent of `m_weapon_skins_selected` (per `menu_group_begin("knife")` at line 571) | skins.cpp (knife path) | find_mdl | yes — CS:GO knife model swap; TF2 melee weapons are schema items, no comparable swap path | L |
| agents (+custom models, per-team T/CT incl. custom path) | "enable agents", "custom models", per-team pickers | `m_agent_enable`, `m_agent_custom_models`, `m_agent_t(+custom)`, `m_agent_ct(+custom)` | `proj-agent-custom-body-1008` `AGT:` | on_add_entity (model swap), draw_model_execute | yes — CS:GO player "agent" models; TF2 has no agent system, player models are the class models (not swappable the same way without new cosmetic pipeline) | L |
| gloves changer | "gloves" group (line 708) | glove-equivalent paint kit var, `proj-botox-skinchanger` swap WORKS, paint regen `GLV: regen` UNTESTED | skins.cpp (gloves path) | find_mdl, draw_model_execute | yes — CS:GO glove viewmodel attachment; TF2 has no glove slot | M |

---

## TAB: fonts / SUBTABS: indicators, esp, chud hud, default hud — `menu_tab_settings.cpp:738 tab_fonts()`

| feature | menu label | config vars | source file(s) | hooks | CS:GO-only? | size |
|---|---|---|---|---|---|---|
| per-category font list picker (indicators/esp) | "font list" group ×2 | selection stored per-indicator-category (no single var dumped by grep; combo-driven) | `hacks/menu/menu_tab_settings.cpp` font list widget, `proj-embedded-fonts-tab-1008` | — (render-time font lookup) | no | M |
| chud hud font (+custom path, scale, nudge x/y) | "chud hud font" group | `m_chud_hud_font_custom`, `m_chud_hud_font_scale`, `m_chud_hud_font_nudge_x`, `m_chud_hud_font_nudge_y` | chud_hud.cpp font loader, `proj-font-reload-staged-1009` `FONT:` | end_scene (reload), paint_traverse | no | M |
| default hud font | "default hud font" group | `m_def_hud_font` | panorama/default hud font override | fire_event_intern | yes — overrides CS:GO Panorama's own font asset; TF2's default HUD font path is a ClientScheme.res / HudLayout.res swap, different mechanism | M |

---

## TAB: settings (no subtabs) — `menu_tab_settings.cpp:1988 tab_settings()`

| feature | menu label | config vars | source file(s) | hooks | CS:GO-only? | size |
|---|---|---|---|---|---|---|
| debug log | "debug log" | `m_debug_log` | `ref-botox-two-loggers`, `ref-botox-debug-log-path` (`C:\botox\botox_debug.log` equiv) | — (logger init at DLL load) | no | S |
| safe mode | "safe mode" | `m_safe_mode` | `proj-safe-mode-1009` `safe_checkbox()` | gates most hooks | no — generic "disable risky features" master switch, portable | S |
| share esp (network) | "share esp" | `m_botox_network_esp` | `hacks/network/botox_net.cpp` | send_net_msg, svc_msg_voice_data | yes — `proj-botox-network-1010` piggybacks ESP/skin data inside CS:GO voice packets; the voice-codec channel and packet framing are CS:GO engine specific (though the *technique* — steganographic data-over-voice — is portable if TF2's voice path supports it) | L |
| share skins (network) | "share skins" | `m_botox_network_skins` | botox_net.cpp | send_net_msg, svc_msg_voice_data | yes — same channel, and shared skin *data* is inherently CS:GO-only payload | L |
| accent color | (color picker, used across whole UI) | `m_accent` | menu-wide theme | — | no | S |
| dpi scale (+panels) | "dpi scale panels" (checkbox); main scale is a slider | `m_dpi_scale`, `m_dpi_scale_panels` | `proj-dpi-scale-1002`, `feedback-new-panel-dpi-scale` RULE | present (recompute on resize) | no | M |

---

## hooks/functions/*.cpp — one line each

| file | purpose |
|---|---|
| `alloc_key_values_memory.cpp` | hooks KeyValues allocator; used as an anchor/utility hook for KV-based config loads |
| `create_move_proxy.cpp` | per-tick client `CreateMove` entry point — dispatches to aimbot, grenade aim, lagcomp, misc, movement before the real move is built |
| `create_texture.cpp` | `IDirect3DDevice9::CreateTexture` — intercepts texture creation for depth-source capture |
| `dispatch_user_message.cpp` | engine usermessage dispatch — chat/extras text scanning (raw byte search for chat image/translator payloads) |
| `do_post_screen_space_effects.cpp` | post-processing entry point — chains every screen-space fx pass (bloom/AO/DoF/color correction/etc.) |
| `draw_filled_rect.cpp` | `IDirect3DDevice9`-level filled-rect draw — used to gate/rewrite crosshair rect draws |
| `draw_indexed_primitive.cpp` | DIP call — weapon sheen material pass hooks a specific DIP call slot |
| `draw_model_execute.cpp` | `IStudioRender`/model draw — chams, glow, skins, old-shaders, agent/knife/glove model swaps |
| `draw_set_color.cpp` | sets draw color before a rect — classic crosshair outline color override point |
| `draw_view_models.cpp` | viewmodel draw call — viewmodel offset/fov/sway/bob and screen effects tied to viewmodel visibility |
| `draw_world_lists.cpp` | world render list traversal — fullbright/precipitation/mc clouds and sky hook point |
| `emit_sound.cpp` | engine `EmitSound` — hit sound / detection sound playback, returns a sound guid callers later stop |
| `end_scene.cpp` | `IDirect3DDevice9::EndScene` — chud hud, entity cache tick, mc hud, misc per-frame work, image cache |
| `find_mdl.cpp` | model index lookup — skin/knife/agent/glove model path substitution |
| `fire_event_intern.cpp` | internal game-event firing — scaleform/chud/mc hud event taps (kill, hurt, round events) |
| `frame_stage_notify.cpp` | per-stage client frame hook — aimbot, animations, debug, entity cache and most per-frame logic dispatch |
| `get_local_view_angles.cpp` | reads local view angles — silent aim / visual recoil removal read point |
| `get_vcollide.cpp` | vcollide (collision model) fetch — edicts/world collision data hook |
| `glow_effect_spectator.cpp` | spectator glow effect manager — player glow / stencil / sound-esp glow draw |
| `is_hltv.cpp` | reports HLTV/spectator state — spectator list and related features gate off this |
| `is_paused.cpp` | reports demo-pause state |
| `is_playing_demo.cpp` | reports demo-playback state — fake POV / smooth demo view gate off this |
| `level_init_post_entity.cpp` | map load, post entity spawn — scaleform/chud hud/avatar cache/skins/aimbot re-init |
| `level_init_pre_entity.cpp` | map load, pre entity spawn — entity cache/avatar cache/scaleform/network re-init |
| `level_shutdown.cpp` | map unload — avatar cache/entity cache/edicts/misc cleanup |
| `list_leaves_in_box.cpp` | BSP leaf query — chams visibility/leaf-based render list filtering |
| `load_file_into_buffer.cpp` | filesystem file load — moi hud (panorama layout/style/image) stock-path substitution |
| `lock_cursor.cpp` | engine cursor lock — movement/movement recorder cursor-lock suppression while a route/recorder window is open |
| `modify_eye_position.cpp` | eye position calc — used by fake pov / third person / eye-correct-stale-bones handling |
| `net_earliertempents.cpp` | networking temp-ents timing — related to backtrack/lag comp timing |
| `on_add_entity.cpp` | entity spawn — entity cache/avatar cache/edicts registration, bot name/model swap trigger |
| `on_remove_entity.cpp` | entity removal — entity cache/avatar cache cleanup |
| `override_mouse_input.cpp` | raw mouse input override — edge skip/edgebug/movement mouse-lock and auto-strafe mouse steer |
| `override_view.cpp` | view setup override — mc clouds/screen fov, third person camera |
| `paint_traverse.cpp` | VGUI panel paint traversal — grenade aim path draw, debug overlay, indicators root draw call |
| `particle_collection_simulate.cpp` | particle system simulate — custom smoke/molotov/blood/precipitation color injection via particle attribute table |
| `pre_client_update.cpp` | listen-server `PreClientUpdate` — bot name server-side name write |
| `present.cpp` | `IDirect3DDevice9::Present` — resolution spoof / depth source frame-boundary work |
| `process_movement.cpp` | player movement processing — tung surf server-side apply, core movement tech dispatch |
| `push_2d_view.cpp` | 2D view push — flip world screen-space transform |
| `push_notice.cpp` | chat/notice push — chat extras / chud hud / scaleform notice text interception |
| `random_int.cpp` | engine RNG — skins (seed-based pattern) interception |
| `reset.cpp` | D3D device reset — avatar cache/indicators/mc hud/misc/screen-fx state reset on resolution/device change |
| `run_command.cpp` | usercmd/command build — weapon-equip related interception (e.g. recorder force-weapon) |
| `send_datagram.cpp` | net channel datagram send — networking timing hook |
| `send_net_msg.cpp` | net message send — botox network (esp/skin share) outbound hook |
| `set_depth_stencil_surface.cpp` | D3D depth-stencil surface set — depth source capture point |
| `set_image_data_r8g8b8a8.cpp` | raw image upload — moi hud / scaleform image substitution |
| `set_visuals_data.cpp` | sets a weapon's "visuals data" struct (paint kit index, VMT name) — skin changer write path |
| `sv_pure.cpp` | `loose_files_allowed` — sv_pure / pure-server bypass |
| `svc_msg_voice_data.cpp` | voice data net message — botox network esp/skin share payload channel |
| `tier0_warning.cpp` | engine warning sink — spam filter for specific known-noisy warnings |
| `view_draw_fade.cpp` | screen fade draw — flash/remove-flash related fade suppression |
| `wndproc.cpp` | window message proc — chud hud/mc hud/movement/movement recorder input handling, media keys |
| `world_to_screen_matrix.cpp` | view/projection matrix fetch — flip world transform, w2s for all ESP/indicator draws |

---

## Summary notes for the port

- Roughly 2 of the 7 top-level tabs (**inventory** entirely, most of **aimbot**'s weapon-class
  config, and large chunks of **movement** main) are CS:GO-specific game mechanics or exploit
  geometry with no direct TF2 analog — these need redesign, not translation.
- **visuals/screen** and **misc overlays** (watermark, spectator list, media player, hit
  marker/sound, death particles, chud hud, mc hud) are almost entirely engine-agnostic overlay
  code (own ImGui/raw-panel rendering) and port cleanly once the underlying game-state reads
  (player list, HLTV/spectator target, kill/hurt events) are remapped to TF2 equivalents.
- Panorama/Scaleform-dependent features (remove panorama blur, square radar, panorama hud/crosshair
  color, keep killfeed, scaleform passthrough, default hud font) have **no TF2 equivalent UI
  system** (TF2 uses VGUI, not Panorama+Scaleform) and cannot be ported as-is.
