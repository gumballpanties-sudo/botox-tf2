# dih (botox-tf2) — existing feature inventory

Reference doc for porting features from the CS:GO "botox" DLL into dih without duplicating
work already done here. Built by reading `src/hacks/menu/menu.cpp` (3353 lines), `menu.h`,
`menu_chrome.h`, `config.cpp` (841 lines), `globals.h`, `dllmain.cpp`, `thread.cpp` (528
lines, `PollThread`/`MainThread`), and every `*.cpp/*.h/*.inl`/`src/hacks/aim/*` top
comment. All line numbers below are current as of this read.

Tab order is the enum in `menu.cpp:3076`: `TAB_HUD, TAB_VISUALS, TAB_MOVEMENT, TAB_AIMBOT,
TAB_MISC, TAB_SKINS` → UI labels `{hud, visuals, movement, combat, misc, skins}`
(`menu.cpp:3077`). The movement tab has 3 inline sub-tabs (`movement`/`fake movement`/
`practice`, `menu.cpp:3265`). Dispatch is the switch in `RenderMenu()`, `menu.cpp:3270-3280`.

---

## 1. Feature table per menu tab

### TAB_HUD — `Tab_Hud()`, `menu.cpp:604-953`

| Feature | Menu label | Globals / config keys | File + main func |
|---|---|---|---|
| Watermark | `Section("watermark")` / `"enabled##wm"` | `g_wmEnable`, `g_wmShowLabel`("title"), `g_wmLabel`(text), `g_wmShowFPS`("fps"), `g_wmShowPing`("ping"), `g_wmShowLoss`("loss"), `g_wmShowTime`("time"), `g_wmPos`(pos combo), `g_wmTextColor` | `watermark.cpp` / `RenderWatermark()`; menu `menu.cpp:609-628` |
| Media player | `Section("media player")` / `"enabled##mp"` | `g_mpEnable`, `g_mpCoverArt`("cover art"), `g_mpArtSide`(art side combo), `g_mpPos`(pos combo) | `mediaplayer.cpp` / `RenderMediaPlayer()`, `MediaPlayer_Start()`; menu `menu.cpp:630-650` |
| Movement indicators | `Section("movement indicators")` / `"enabled##mi"` | `g_miEnable`, `g_miHorizontal`, `g_miMitosis`, show-mask combo (`movind.h` `MI_AST` slot bits, config key `miShowMask`, legacy migration from `miOnlyActive` at `config.cpp:617`) | `movind.cpp` / `RenderMoveIndicators()`; menu `menu.cpp:652-708` |
| Velocity indicator | `Section("velocity indicator")` / `"enabled##vi"` | `g_viEnable`, `g_viPreSpeed`("show pre speed"), `g_viFadeAlpha`, `g_viCustomColor` | `render.cpp` / `RenderVelocityIndicator()`; menu `menu.cpp:709-741` |
| Crosshair (classic) | `Section("crosshair")` / `"enabled##ch"` | `g_chEnable`, `g_chAntialias`, blend combo (`Crosshair_BlendCombo`), `g_chDot`, `g_chDotCircle`, `g_chOutline` | `crosshair.cpp` / `RenderCrosshair()`; menu `menu.cpp:742-781` |
| Crosshair (CS:GO) | `"csgo crosshair"` | `g_chCsgoEnable`, `g_chCsgoStyle`(combo), `g_chCsgoCrisp`, `g_chCsgoLinear`, `g_chCsgoAdditive`, `g_chCsgoArcs`("spread lines"), `g_chCsgoPips`("arms") | `crosshair.cpp`; menu `menu.cpp:783-834` |
| Crosshair (CS:GO X-style variant) | nested under csgo crosshair | `g_csXDot`, `g_csXOutline`, `g_csXT`("t style"), `g_csXUseAlpha` | `crosshair.cpp`; menu `menu.cpp:852-864` |
| CS:GO scope | `"csgo scope"` | `g_scCsgo`, `g_scCsgoWeapon`(awp/ssg08/scar-20-g3sg1 combo), `g_scCsgoZoom`(combo), `g_scCsgoNormalInacc`, `g_scCsgoLens`("dust lens"), `g_scCsgoNoDot`, `g_scCsgoScopeIn`("scope-in blur"), `g_scCsgoBob`("sway") | `scope_csgo.inl` / `ScopeCsgo_Install`, `h_ScCsPaint`; menu `menu.cpp:881-930` |
| Scope misc | plain checkboxes | `g_scChargeHide`("hide scope charge meter"), `g_staticAmbyCrosshair`("static amby crosshair") | `scope_csgo.inl`; menu `menu.cpp:932-933` |
| Killfeed | `Section("killfeed")` | `g_selfKillfeedOnly`("only your killfeed") | `killfeed.cpp` / `h_FireEvent`; menu `menu.cpp:935-936` |
| Scoreboard | `Section("scoreboard")` | `g_revealScoreboard` | `scoreboard.cpp` / `h_VGuiRunFrame`; menu `menu.cpp:938-939` |
| Misc (Steam) | plain checkbox at tab end | `g_steamHistory`("steam history menu item") | `menu.cpp:945` (no separate .cpp — inline Steam overlay history toggle) |

### TAB_VISUALS — `Tab_Visuals()`, `menu.cpp:1225-1757`

| Feature | Menu label | Globals / config keys | File + main func |
|---|---|---|---|
| Viewmodel sway | `Section("viewmodel")` / `"sway"` | `g_vmSwayEnable` | `visuals.cpp`; menu `menu.cpp:1230-1245` |
| Viewmodel offset | `"offset"` | `g_vmOffsetEnable` | `visuals.cpp` `h_CalcViewModelView`; menu `menu.cpp:1246-1254` |
| Remove bob | plain checkbox | `g_vmRemoveBob` | `visuals.cpp`; `menu.cpp:1255` |
| CS:GO bob | `"csgo bob"` | `g_vmCsBob` | `vm_csgo_hook.inl` / `h_VmCsBobHelper`; menu `menu.cpp:1260-1285` |
| CS:GO jump lower | `"csgo jump lower"` | `g_vmCsGunLower` | `vm_csgo.h`; menu `menu.cpp:1286-1297` |
| CS:GO landing dip | `"csgo landing dip"` | `g_vmCsLandDip` | `vm_csgo.h`; menu `menu.cpp:1298-1314` |
| Viewmodel transparency | `"transparent"` | `g_vmAlphaEnable`, `g_vmFlatAlpha` | `render.cpp` (SetBlend/GetBlend via `g_renderView`); menu `menu.cpp:1315-1333` |
| Weather | `Section("weather")` / `"enabled##weather"` | `g_weatherEnable`, `g_weatherMode`(off/rain/snow) | `h_FindMaterial` (thread.cpp:208); menu `menu.cpp:1335-1370` |
| Fog | `Section("fog")` | `g_fogEnable` | fog override (visuals/render); menu `menu.cpp:1371-1383` |
| Skybox | `Section("skybox")` | `g_skyEnable`, preset combo, `g_skyName` | `h_R_DrawSkyBox` (engine.dll pattern hook, thread.cpp:316-318); menu `menu.cpp:1384-1432` |
| Flip world | `Section("flip world")` | `g_flipWorld` | `flipworld.cpp` / `FlipWorld_Install`; menu `menu.cpp:1433-1444` |
| Ambient occlusion | `Section("ambient occlusion")` | `g_aoEnable`, `g_aoViewmodel`, `g_aoFogFade`, `g_aoDebug` | `screenfx.cpp` (SSAO path) + `ssao.cpp`; menu `menu.cpp:1445-1490` |
| Depth of field | `Section("depth of field")` | `g_dofEnable`, `g_dofAutoFocus`, `g_dofNearBlur`, `g_dofViewmodelSharp`, `g_dofSmooth` | `dof.cpp` + `screenfx.cpp` (shared depth); menu `menu.cpp:1491-1561` |
| Frame blending (motion blur) | `Section("frame blending")` | `g_fbEnable`, `g_fbPlayers`, `g_fbViewmodel` | `frameblend.cpp` + `fbmath.h` + `fbbones.h`; menu `menu.cpp:1562-1625` |
| Screen effects (perf/plumbing) | `Section("screen effects")` | `g_fxUseCallQueue`, `g_fxForceQueueMode`, `g_fxDepthProbe` | `screenfx.cpp` / `h_SetRenderState`; menu `menu.cpp:1626-1649` |
| Bloom | `Section("bloom")` | `g_bloomEnable` | `bloom.cpp`; menu `menu.cpp:1650-1681` |
| Color correction | `Section("color correction")` | `g_ccEnable` (+ curve, see `CcCurveEditor()` `menu.cpp:1092`) | `colorcorrect.cpp` / `cc_shader.hlsl`; menu `menu.cpp:1682-1727` |
| Removals | `Section("removals")` | `g_noHats`, `g_noUnusual`, `g_removeRagdolls`, `g_removePropFade`, `g_fpsBoost`, `g_removeScreenEffects` | `h_DrawModelExecute`/`h_CParticlePropCreate*`/`h_CreateTFRagdoll`/`h_SetupBones`/`h_PerformScreenOverlay` (thread.cpp:206-231); menu `menu.cpp:1728-1736` |
| Lag resolver | `Section("lag resolver")` | `g_lagResolver`, `g_lrYaw`("smooth turning") | `lagres.cpp`; menu `menu.cpp:1737-1757` |

### TAB_MOVEMENT — `Tab_MovementRoot()` (`menu.cpp:3082`), 3 inline sub-tabs

#### Sub-tab "movement" — `Tab_Movement()`, `menu.cpp:1821-2535`, section "main" (`:1831`)

| Feature | Menu label | Globals | File + main func |
|---|---|---|---|
| Bunnyhop | `"bunnyhop"` | `g_bhop` | `movement.cpp` / `h_CreateMove`+`Movement_Run` |
| Edge jump | `"edge jump"` + bind | `g_edgeJump`, `g_edgeJumpKey`, `g_edgeJumpMode` | `movement.cpp` |
| Long jump | `"long jump"` | `g_longJump`, `g_longJumpEdge`("edgejump on lj") | `movement.cpp` |
| Mini jump | `"mini jump"` | `g_miniJump`, `g_miniJumpHoldDuck`, `g_miniJumpEdge` | `movement.cpp` (release logic ~`movement.cpp:1238`) |
| Jumpbug | `"jumpbug"` | `g_jumpbug`, `g_jbPredict`, `g_jbYieldSurf` | `movement.cpp` / `rs::JumpbugCmd`, `rs::JumpbugWillHit` (`routesim.h`) |
| Edge bug | `"edge bug"` | `g_edgeBug`, `g_edgeBugAdvSearch`, `g_edgeBugAutoStrafe`, `g_edgeBugToEdge`, `g_edgeBugBrake`, `g_edgeBugStandableOnly`, `g_edgeBugViz`, `g_edgeBugStance`(combo), `g_edgeBugHuman` | `edgebug.inl` (textually included by `movement.cpp`) |
| Auto strafe | `"auto strafe"` + bind | `g_autoStrafe`, `g_autoStrafeKey`, `g_autoStrafeMode` | `movement.cpp` |
| Strafe optimizer | `"strafe optimizer"` | `g_strafeOpt` | `movement.cpp` |
| Auto align | `"auto align"` | `g_autoAlign` | `movement.cpp` |
| Pixel surf | `"pixel surf"` | `g_pixelSurf`, `g_psGuide`, `g_psSpeedFix`, `g_psStick`, `g_psParkAtEnd` | `psassist.inl` (textually included) |
| Air stuck | `"air stuck"` | `g_airStuck`, `g_asAutoCrouch`, `g_asFreeMove`, `g_asYieldSurf` | `movement.cpp` |
| Texture bug | `"texture bug"` | `g_textureBug`, `g_tbPredict`, `g_tbSolveForward`, `g_tbNoOffgridStick`, `g_tbParkAtEnd`, `g_tbParkBrake` | `texturebug.inl` (textually included) |
| Wall climb | `"wall climb"` + bind | `g_wallClimb`, `g_wallClimbKey`, `g_wallClimbMode` | `movement.cpp` |
| Fast stop | `"fast stop"` | `g_fastStop` | `movement.cpp` |
| Fast accelerate | `"fast accelerate"` | `g_fastAccel` | `movement.cpp` |
| No push | `"no push"` | `g_noPush` | `movement.cpp` / `h_AvoidPlayers` |

Section "misc" of the movement sub-tab (`menu.cpp:2184-2535`):

| Feature | Menu label | Globals | File + main func |
|---|---|---|---|
| Pixel surf finder | `"ps finder"` | `g_psFinder`, `g_psFinderStrict`, `g_psFinderTb`, `g_psFinderNoProps`, `g_psMapLines`("show map lines", nested) | `psmap.inl` / PixelFinder |
| PS assist | `"ps assist"` | `g_psAssist`, `g_psGroundHelp`, `g_psAssistRender`("show points"), `g_psAssistJumpBox`("jump popup") + per-move checkboxes via `RC_MoveName()` | `psassist.inl` |
| Pixel calculator | `"pixel calculator"` | `g_pixelCalc`, `g_pcShowMarker`, `g_pcShowPanel` | menu `menu.cpp:2339-2364` (calc body elsewhere in movement.cpp) |
| Route calculator | `"route calculator"` | `g_routeCalc`, `g_rcStartHere`, `g_rcShowMarkers`, `g_rcShowPath`, `g_rcShowPanel` + marker editor (type combo, enabled, stance combo) | `routecalc.inl` (textually included); see `rc_edge.h` |

#### Sub-tab "fake movement" — `Tab_FakeMovement()`, `menu.cpp:2554-2692`

| Feature | Menu label | Globals | File + main func |
|---|---|---|---|
| Fake movement master | `"enabled"` | `g_fmEnabled` | `fakemove.inl` (textually included by movement.cpp) |
| Surf on any wall | `"surf on any wall"` | `g_fmWallSurf`, `g_fmWallSurfSeam`("seams only") | `fakemove.inl` / `fm_seam.h` |
| Jump off walls | `"jump off walls"` | `g_fmWallJump`, `g_fmWallJumpHold`, `g_fmWallJumpSeam` | `fakemove.inl` |
| Higher jumps | `"higher jumps"` | `g_fmHighJump` | `fakemove.inl` |
| Air stuck (fake move) | `"air stuck##fm"` | `g_fmAirStuck`, `g_fmAirStuckKeepVel`, `g_fmAirStuckNearOnly` | `fakemove.inl` |
| No fall damage | `"no fall damage"` + bind | `g_fmNoFall`, `g_fmNoFallKey`, `g_fmNoFallMode` | `fakemove.inl` |
| Crouch jump | `"crouch jump"` + bind | `g_fmCrouchJump`, `g_fmCrouchJumpKey`, `g_fmCrouchJumpMode` | `fakemove.inl` |
| Ramp boost | `"ramp boost"` | `g_fmRampBoost` | `fakemove.inl` |
| Head surf | `"head surf"` | `g_fmHeadSurf`, `g_fmHeadSurfLedgeOnly`, `g_fmHeadSurfBottoms` | `fakemove.inl` / `fm_slide.h` |

`fakemove.inl` installs its own hooks: `IGameMovement::ProcessMovement` slot `kGM_ProcessMovement`=1 on both client's `g_gameMovement` and (if present) `server.dll` `GameMovement001` (`fakemove.inl:748-759`).

#### Sub-tab "practice" — `Tab_Practice()`, `menu.cpp:2693-2750`

| Feature | Menu label | Globals | File + main func |
|---|---|---|---|
| Infinite ammo | `"infinite ammo"` | `g_prInfAmmo` | `practice.inl` (textually included) |
| Infinite health | `"infinite health"` | `g_prInfHealth` | `practice.inl` |
| Infinite map time | `"infinite map time"` | `g_prInfTime` | `practice.inl` |
| Binds | `Section("binds")` | (keybind rows, no new globals beyond bind triples) | `practice.inl` |
| Place bot at crosshair | `Section("place at crosshair")`, class combo `g_prBotClass` | `g_prBotClass` | `practice.inl` / `PrCmd(...)`, spawns `ent_create` named `dihb%d` |

### TAB_AIMBOT ("combat") — `Aim_RenderMenuTab()`, `src/hacks/aim/aim.cpp:426-546`

Entirely separate var/config framework (Amalgam-style `ConfigVar<T>` + `Vars::` namespace),
not dih's `g_`/config.cpp table. Rendered only if `Aim_Active()` (i.e. `Aim_Init()` succeeded).

| Feature | Menu label | Vars (ConfigVar) | File + main func |
|---|---|---|---|
| Aimbot master | `Section("aimbot")` / `"enabled"` + bind | `Aimbot::General::Enabled/AimKey/AimKeyMode`, `AimType`(combo), `AimFOV`, `MaxTargets`, `AssistStrength`(smooth/assistive only), `AutoShoot`, `NoSpread`, `Target`/`Ignore`(multi, + `IgnoreInvisible`, `TickTolerance`), plus `CritHack::ForceCrits/AvoidRandomCrits/AlwaysMeleeCrit` | `aim/aimbot.cpp`, `aim/crithack.cpp`, `aim/nospread*.cpp` |
| Hitscan | `Section("hitscan")` | `Aimbot::Hitscan::Enabled/Hitboxes/MultipointHitboxes/MultipointScale/Modifiers`(+`TapfireDistance`) | `aim/aim_hitscan.cpp` |
| Projectile | `Section("projectile")` | `Aimbot::Projectile::Enabled/AimFOV/Hitboxes/MaxSimulationTime/StrafePrediction(+HitChance)/SplashPrediction/SplashRadius/Modifiers/AutoDetonate(+AutodetRadius)/AutoAirblast(+AutoRelease)` | `aim/aim_projectile.cpp`, `aim/autodetonate.cpp`, `aim/autoairblast.cpp`, `aim/projsim.cpp` |
| Melee | `Section("melee")` | `Aimbot::Melee::Enabled/SwingPrediction` | `aim/aim_melee.cpp` |
| Auto backstab | top-level row, `"auto backstab"` + own bind | `Aimbot::Melee::AutoBackstab/BackstabKey/BackstabKeyMode/BackstabAimType/BackstabFOV(Silent only)/BackstabIgnore(+IgnoreInvisible)` | `aim/aim_melee.cpp`, `aim/bs_math.h` |
| Clean POV demos | `Section("demos")` | `Misc::Game::CleanPOVDemos`, `AntiCheatCompatibility` | hooked via `CPrediction_GetLocalViewAngles` (`aim/hooks.cpp`) |

Persistence rides inside dih's own `C:\dih\default.cfg` via `Aim_SaveVars`/`Aim_LoadVar`
(`aim.cpp:209-265`), gated by `IsAimVar()` prefix whitelist (`Vars::Aimbot::`,
`Vars::CritHack::`, two `Vars::Misc::Game::` keys) — see architecture section 3.

### TAB_MISC — `Misc_Convenience()` (`menu.cpp:954-1224`) + `Misc_Rest()` (`menu.cpp:1758-1820`)

| Feature | Menu label | Globals | File + main func |
|---|---|---|---|
| Region selector | `Section("general")` / `"region selector"` | `g_regionEnable`, regions combo | `netregion.cpp` / `Region_Install`, `h_GetPingToDataCenter` |
| Net settings | `"net settings"` | `g_netSettingsEnable`, `g_netSettingsSel`(combo), `g_netSettingsRates` | `netregion.cpp` / `netcvar.h` |
| Net graph color | `"net graph color"` | `g_ngColorEnable` | `netregion.cpp` / `h_DrawColoredText` |
| Net graph position | `"net graph position"` | `g_ngPosEnable` | `netregion.cpp` |
| Loadout unlock | `Section("loadout")` | `g_equipRegionUnlock` | `equipregion.cpp` / `EquipRegion_Install` |
| Performance | `Section("performance")` | `g_hiTimerRes`("1ms timer") | `thread.cpp`/`cmdbudget.h` |
| Bypass | `Section("bypass")` | `g_pureBypass`, `g_cheatsBypass`, `g_f2pChatBypass` | `h_CL_CheckForPureServerWhitelist` (thread.cpp:27); sv_cheats/f2p bypasses elsewhere in movement/netvars |
| Debug | `Section("debug")` | `g_debugLogEnable` | `dllmain.cpp` `DebugLog` gate |
| Keys | `Section("keys")` | (keybind summary rows only) | `menu.cpp:1814-1820` |

### TAB_SKINS — `Tab_Skins()`, `menu.cpp:2751-3075`

| Feature | Menu label | Globals / per-weapon config (`SkinCfg`) | File + main func |
|---|---|---|---|
| Skinchanger master | `Section("skinchanger")` | `g_enableSkins`, weapon combo, `cfg.enabled` | `skins.cpp` / `Skins_Install`, `h_FrameStageNotify` |
| Warpaint | `Section("warpaint")` | warpaint combo + filter, `cfg.paintKitId` | `skins.cpp`; data table `skindata.h` |
| Killstreak | `Section("killstreak")` | `cfg.ksTier`(combo), `cfg.ksSheen`(combo), `cfg.ksCustomColor`+`cfg.ksColor`, `cfg.ksEffect`(combo) | `skins.cpp` |
| Particle (unusual) | `Section("particle")` | particle combo + filter, `cfg.unusual`(id input) | `skins.cpp` |
| Variant | `Section("variant")` | `cfg.australium`, `cfg.festivized`, `cfg.festive` | `skins.cpp` |
| Reskin | `Section("reskin")` | reskin combo | `skins.cpp` / `Skins_RemapSequence` (also called from `aim/hooks.cpp` `CBaseAnimating_SetSequence` when the aim half owns that hook) |
| Extras | `Section("extras")` | `cfg.statClock`+`cfg.statKills`, 3x spell checkboxes (exorcism / pumpkin bombs / halloween fire) | `skins.cpp` |

---

## 2. Hook table

All via the shared helpers in `globals.h:717-780` (`InstallVTableHook`, `InstallPatternHook`,
`InstallPatternHookAny`) unless noted "raw MH_CreateHook". Installed from `thread.cpp`
`MainThread()` (d3d9 core 4 hooks) and `PollThread()` (everything else, once, gated on
`WaitForClientInit()`), plus feature-local installers called from `PollThread`.

| # | Target | Slot / pattern | Detour | Orig | Defined in |
|---|---|---|---|---|---|
| 1 | `user32.dll!SetCursorPos` (export) | raw `MH_CreateHook` on `GetProcAddress` | `h_SetCursorPos` | `o_SetCursorPos` | `thread.cpp:372` |
| 2 | IDirect3DDevice9 vtable | slot **17** (`Present`) | `h_Present` | `o_Present` | `thread.cpp:376`, def `render.cpp:1281` |
| 3 | IDirect3DDevice9 vtable | slot **16** (`Reset`) | `h_Reset` | `o_Reset` | `thread.cpp:377` |
| 4 | IDirect3DDevice9 vtable | slot **42** (`EndScene`) | `h_EndScene` | `o_EndScene` | `thread.cpp:380`, def `render.cpp:1155` |
| 5 | IDirect3DDevice9 vtable | slot **57** (`SetRenderState`, via `ScreenFx_InstallRenderStateHook`, raw `MH_CreateHook`) | `h_SetRenderState` | `o_SetRenderState` | `thread.cpp:385`, install `screenfx.cpp:769-776` |
| 6 | `vgui::IPanel` | slot **41** (`PaintTraverse`) | `h_PaintTraverse` | `o_PaintTraverse` | `thread.cpp:184`, def `render.cpp:1092` |
| 7 | `IVModelRender` (`g_modelRender`) | slot **19** (`DrawModelExecute`) | `h_DrawModelExecute` | `o_DrawModelExecute` | `thread.cpp:206` (no-hats / frame-blend gun bones) |
| 8 | `IMaterialSystem` (`g_matSystem`) | slot **71** (`FindMaterial`) | `h_FindMaterial` | `o_FindMaterial` | `thread.cpp:208` (weather materials) |
| 9 | client.dll pattern | `CParticleProperty::Create` — `48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC ? 48 8B 59 ? 49 8B F1` | `h_CParticlePropCreate` | `o_CParticlePropCreate` | `thread.cpp:217-219` |
| 10 | client.dll pattern | `CParticleProperty::Create(pt)` — `44 89 4C 24 ? 44 89 44 24 ? 53` | `h_CParticlePropCreatePt` | `o_CParticlePropCreatePt` | `thread.cpp:220-222` |
| 11 | client.dll pattern | `CreateTFRagdoll` — `48 89 4C 24 ? 55 53 56 57 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 8B 91` | `h_CreateTFRagdoll` | `o_CreateTFRagdoll` | `thread.cpp:223-225` |
| 12 | client.dll pattern | `SetupBones` — `48 8B C4 44 89 40 ? 48 89 50 ? 55 53` | `h_SetupBones` | `o_SetupBones` | `thread.cpp:226-228` |
| 13 | client.dll pattern | `PerformScreenOverlay` — `4C 8B DC 49 89 5B ? 89 54 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 81 EC` | `h_PerformScreenOverlay` | `o_PerformScreenOverlay` | `thread.cpp:229-231` |
| 14 | client.dll pattern | `AvoidPlayers` — `48 89 54 24 ? 55 41 56` | `h_AvoidPlayers` | `o_AvoidPlayers` | `thread.cpp:232-234` |
| 15 | client.dll pattern | `GetWeaponCrosshairScale` — `48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B F2 48 8B F9 E8 ? ? ? ? 48 8B D8 48 85 C0 0F 84` | `h_GetWeaponCrosshairScale` | `o_GetWeaponCrosshairScale` | `thread.cpp:235-237` |
| 16 | `VClient01[678]` vtable | slot **21** (`CreateMove`) — **skipped if the aim half owns it** (`Aim_Active()`) | `h_CreateMove` | `o_CreateMove` | `thread.cpp:263-264`, def `movement.cpp` |
| 17 | `VClient01[678]` vtable | slot **36** (`DispatchUserMessage`) | `h_DispatchUserMessage` | `o_DispatchUserMessage` | `thread.cpp:266-267` |
| 18 | client.dll pattern (2 candidates, `InstallPatternHookAny`) | `CalcViewModelView` — `48 89 74 24 ? 55 41 56 41 57 48 8D AC 24` or `48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 49 8B F0 48 8B EA 48 8B F9 33 DB` | `h_CalcViewModelView` | `o_CalcViewModelView` | `thread.cpp:274-280` |
| 19 | client.dll (`VmCsgo_Install`) pattern list `kVmCsBobSigs` | CS:GO bob helper | `h_VmCsBobHelper` | `o_VmCsBobHelper` | `thread.cpp:281`, install `vm_csgo_hook.inl:106-125` |
| 20 | client.dll (`ScopeCsgo_Install`) patterns `kScCsChargeSig` / `kScCsDotPosSig` / `kScCsPaintSig` (+ optional `IPanel::PaintTraverse` slot 41 under `SCSGO_OWN_PAINTTRAVERSE`) | raw `MH_CreateHook` via `ScCs_HookAt` | `h_ScCsChargePaint`, `h_ScCsDotPos`, `h_ScCsPaint`, (`h_ScCsPaintTraverse`) | `o_ScCs*` | `thread.cpp:282`, install `scope_csgo.inl:420-445` |
| 21 | `ISteamNetworkingUtils` (`g_steamNetUtils`) vtable | slot **8** (`GetPingToDataCenter`) | `h_GetPingToDataCenter` | `o_GetPingToDataCenter` | `thread.cpp:284`, install `netregion.cpp:167` |
| 22 | client.dll pattern | `CTFPartyClient::RequestQueueForMatch` — `40 55 56 48 81 EC ? ? ? ? 48 63 F2` | `h_RequestQueueForMatch`(ping refresh) | — | `netregion.cpp:184` |
| 23 | CTFGCClientSystem sigs (`MMFix_Install`) | (multiple internal sigs, "stuck match found" unstick) | — | — | `mmfix.cpp` |
| 24 | `IMatSystemSurface` | slot **162** (`DrawColoredText`, `k_drawColoredTextSlot`), installed disarmed | `h_DrawColoredText` | `o_DrawColoredText` | `netregion.cpp:580,754` |
| 25 | `IClientMode` (`ScreenFx_FindClientMode()`) | slot **39** (`DoPostScreenSpaceEffects`, `k_slotDoPostSSE`) | `h_SetupScreenFx`/screenfx's SSE detour | `o_DoPostScreenSpaceEffects` | `screenfx.cpp:1337,1529` |
| 26 | `IVRenderView` (`g_renderView`) | slot **24** (`ViewDrawFade`, `k_slotViewDrawFade`) | screenfx's gun-pass detour | `o_ViewDrawFade` | `screenfx.cpp:1207,1520` |
| 27 | `IVEngineClient` (`g_engine`) | slot **36** (`WorldToScreenMatrix`, `engWorldToScreenMatrix`, `flipmath.h:23`) | `h_FlipW2S` | `o_FlipW2S` | `flipworld.cpp:660` |
| 28 | `IVRenderView` (`g_renderView`) | slot **39** (`Push2DView`, `rvPush2DView`, `flipmath.h:26`) | `h_FlipPush2D` | `o_FlipPush2D` | `flipworld.cpp:662` |
| 29 | `IClientMode` (`mode`) | slot **21** (`CreateMove`, `cmCreateMove`, `flipmath.h:27`) | `h_FlipCreateMove` | `o_FlipCreateMove` | `flipworld.cpp:666` |
| 30 | engine.dll pattern | `CCvarUtilities::WriteVariables` — `40 57 41 54 41 56 41 57 48 83 EC 48 48 8B 0D ? ? ? ? 45 33 E4 4C 89 A4 24 88 00 00 00 45 0F B6 F0 4C 8B FA` | `h_FlipWriteVars` | `o_FlipWriteVars` | `flipworld.cpp:670-672` |
| 31 | client.dll pattern | `CTFViewModel::CalcViewModelLag` — `k_flipSwaySig` | `h_FlipVmLag` | `o_FlipVmLag` | `flipworld.cpp:674-675` |
| 32 | `IViewRender` (`view`) | slot **1** (`LevelInit`, `vrLevelInit`, `flipmath.h:28`) | `h_FlipLevelInit` | `o_FlipLevelInit` | `flipworld.cpp:692` |
| 33 | `IViewRender` (`view`) | slot **2** (`LevelShutdown`, `vrLevelShutdown`, `flipmath.h:29`) | `h_FlipLevelShutdown` | `o_FlipLevelShutdown` | `flipworld.cpp:694` |
| 34 | client.dll pattern | skinchanger `FrameStageNotify` engine-side — `4C 8B DC 56 48 83 EC` (on `hEngine`) | `h_FrameStageNotify` | `o_FrameStageNotify` | `skins.cpp:2447` |
| 35 | client.dll pattern | `CBaseAnimating::SetSequence` — `40 53 48 83 EC ? 48 8B D9 39 91 ? ? ? ? 74 ? 89 91` (skipped if aim half owns `CBaseAnimating_SetSequence`) | `h_SetSequence` | `o_SetSequence` | `skins.cpp:2465` |
| 36 | client.dll pattern | `TF_IsHolidayActive` — `48 83 EC ? 48 8B 05 ? ? ? ? 44 8B C9` | `h_TF_IsHolidayActive` | — | `skins.cpp:2483` |
| 37 | client.dll pattern | `VerifyChangedLoadoutsAreValid` — `41 56 48 83 EC ? 48 8B 05 ? ? ? ? 48 8D 54 24` | `h_VerifyChangedLoadoutsAreValid` | `o_VerifyChangedLoadoutsAreValid` | `equipregion.cpp:72-75` |
| 38 | client.dll pattern | `GenerateEquipRegionConflictMask` — `48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC ? 33 FF 41 8B E8` | `h_GenerateEquipRegionConflictMask` | `o_GenerateEquipRegionConflictMask` | `equipregion.cpp:76-79` |
| 39 | client.dll pattern | `GetItemInLoadoutForClass` — `48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 57 48 83 EC ? 81 60` | `h_GetItemInLoadoutForClass` | `o_GetItemInLoadoutForClass` | `equipregion.cpp:80-83` |
| 40 | vgui dll pattern | `scoreboard: CVGui::RunFrame` — `48 8B C4 53 48 81 EC` | `h_VGuiRunFrame` | `o_VGuiRunFrame` | `scoreboard.cpp:467` |
| 41 | engine.dll pattern | `skybox: R_DrawSkyBox` — `48 8B C4 55 53 41 54 41 55` | `h_R_DrawSkyBox` | `o_R_DrawSkyBox` | `thread.cpp:316-318` |
| 42 | engine.dll pattern | `bypass: sv_pure` (`CL_CheckForPureServerWhitelist`) — `40 56 48 83 EC ? 83 3D ? ? ? ? ? 48 8B F1 0F 8E` | `h_CL_CheckForPureServerWhitelist` | `o_CL_CheckForPureServerWhitelist` | `thread.cpp:319-321`, def `thread.cpp:27` |
| 43 | engine.dll pattern | `removals: ComputePropOpacity` (CStaticPropMgr, Aletherium sig) — `48 89 5C 24 ? 48 89 7C 24 ? 41 54 41 56 41 57 48 83 EC ? 48 8B 05` | `h_ComputePropOpacity` | `o_ComputePropOpacity` | `thread.cpp:322-324` |
| 44 | `IGameEventManager2` (`g_gameEventManager`, resolved via raw `CreateInterface`, not module table) | slot **8** (`FireEvent`) | `h_FireEvent` | `o_FireEvent` | `thread.cpp:345-346` (killfeed filter) |
| 45 | client.dll pattern | `bhop: ValidateUserCmd` (Aletherium `CInput::ValidateUserCmd` sig) — `48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B F9 41 8B D8`, raw `MH_CreateHook` | `h_ValidateUserCmd` | `o_ValidateUserCmd` | `movement.cpp:4220-4226` |
| 46 | `g_input` vtable | slot **8** (`GetUserCmd`), raw `MH_CreateHook` | `h_GetUserCmd` | `o_GetUserCmd` | `movement.cpp:4232-4238` |
| 47 | `IClientMode` | slot **39** again, for ambient occlusion's screen-space pass — **shares the screenfx install above**, not a second hook | — | — | n/a (same `k_slotDoPostSSE`) |

### Aim subsystem's own hooks (`src/hacks/aim/hooks.cpp`, Amalgam-style `MAKE_HOOK`/`MAKE_SIGNATURE`)

25 `MAKE_HOOK` targets are **compiled** (registered with `Hooks::` at static-init); whether
each is actually installed is gated by the whitelist `s_vHookWhitelist` (`aim.cpp:36-97`) —
installation happens generically through `CHook::Create` → raw `MH_CreateHook`
(`aim/utils.h:82`), driven by `U::Hooks.Initialize(whitelist)` (`aim.cpp:118`). Every one of
the 25 compiled hooks is currently on the whitelist (`aim.cpp:38-96`), i.e. all 25 install:

`CHLClient_CreateMove`, `CBasePlayer_ItemPostFrame`, `CHLClient_EncodeUserCmdToBuffer`,
`CClientState_ProcessFixAngle`, `CPrediction_GetLocalViewAngles`, `CHLClient_FrameStageNotify`,
`RecvProxy_SimulationTime`, `CL_ProcessPacketEntities`, `CBaseAnimating_UpdateClientSideAnimation`,
`CBaseAnimating_MaintainSequenceTransitions`, `CBaseAnimating_Interpolate`,
`CBaseAnimating_SetSequence`, `CBaseEntity_BaseInterpolatePart1`, `CBaseEntity_ResetLatched`,
`CTFPlayer_BuildTransformations`, `CBaseEntity_SetAbsVelocity`, `CBaseEntity_EstimateAbsVelocity`,
`CPrediction_RunSimulation`, `CTFPlayerShared_InCond`, `CTFWeaponBase_CalcIsAttackCritical`,
`CTFWeaponBase_CanFireRandomCriticalShot`, `CTFRocketLauncher_CheckReloadMisfire`,
`IEngineVGui_Paint`, `DataTable_Warning`, `CHLClient_DispatchUserMessage`.

The whitelist mechanism still matters for a port: it exists to let `hooks.cpp` carry MORE
`MAKE_HOOK` definitions than are live (e.g. the historical `CM_BoxTrace`/`IEngineTrace_TraceRay`
hooks mentioned in `aim.cpp:51-54` were removed from the file entirely rather than left
unwhitelisted — so today "compiled" and "whitelisted" happen to be the same 25, but a ported
hook that isn't needed yet can be added to `hooks.cpp` and left OFF `s_vHookWhitelist` to stage
it without installing it).

`CHLClient_CreateMove` is the important collision point: MinHook allows one hook per target, so
dih's own `h_CreateMove` (hook #16 above) is **skipped** whenever `Aim_Active()` — the aim
half's hook calls dih's `Movement_Run()` directly instead (`thread.cpp:260-264`,
`aim/hooks.cpp:668`).

---

## 3. Menu architecture

**Tab dispatch**: `enum MenuTab{...}` + `kTabNames[]` (`menu.cpp:3076-3077`), switch in
`RenderMenu()` (`menu.cpp:3270-3280`). Movement tab has 3 inline sub-tabs drawn via
`InlineTabs(kSub, 3, s_moveSub)` (`menu.cpp:3263-3267`).

**Section/group-box flow**: `Section(title)` (`menu_chrome.h:179`) opens a group box in
whichever of the 2 columns is currently shorter, caching last frame's measured height per
`ImGui::GetID(title)`; must be called between `FlowBegin()`/`FlowEnd()`
(`menu.cpp:3269,3281`). Outside a tab body it falls back to a plain `SectionHeader()`.

**Feature-row pattern** — checkbox + fold chevron, chevron is the ONLY thing that
opens/closes a group (ticking the box does not auto-expand):

```cpp
// menu.cpp:609-614
Section("watermark");
FeatureCheckbox("enabled##wm", &g_wmEnable, oWm);
if (g_wmEnable && GroupToggle("##wmgrp", oWm))
{
    ImGui::Checkbox("title", &g_wmShowLabel);
    ...
```

`FeatureCheckbox` (`menu.cpp:555-559`) is a thin `ImGui::Checkbox` wrapper that keeps the
`open` flag in its signature purely so every call site pairs the box with its group bool.
`GroupToggle` (`menu.cpp:570-591`) hand-draws a 10×10 chevron (not `ArrowButtonEx`) at the
row's right edge; exported to the aim subsystem as `MenuGroupToggle()` (`menu.cpp:596`), which
is how `aim/aim.cpp`'s `VarGroup()` reuses the exact same chevron/row look
(`aim/aim.cpp:414-424`).

Bind rows use `BindKey(id, int* key, int* mode)` (`menu.cpp:426-469`), exported as
`MenuBindKey()` (`menu.cpp:470`) — 3-state capture (listen / always-on / hold / toggle),
reused verbatim by the aim tab for `AimKey`/`BackstabKey` (`aim/aim.cpp:443-445,519-521`).

**Config save/load** — one table drives both directions, `config.cpp:45-...` (`kEntries[]`),
typed `{key, type, ptr, size}`:

```cpp
// config.cpp:46-55 (excerpt)
static const CfgEntry kEntries[] = {
    { "menuKey",          CfgEntry::INT,    &g_menuKey, 0 },
    { "wmEnable",         CfgEntry::BOOL,   &g_wmEnable, 0 },
    { "wmShowLabel",      CfgEntry::BOOL,   &g_wmShowLabel, 0 },
    { "wmLabel",          CfgEntry::STR,    g_wmLabel, sizeof(g_wmLabel) },
    ...
```

Save walks the table (`config.cpp:510-526`, `fprintf(f, "%s=...\n", e.key, ...)` per type:
BOOL/INT/FLOAT/FLOAT4/STR/CURVE). Load does a linear `strcmp(key, e.key)` match per line
(`config.cpp:627`). Legacy-key migration is handled ad hoc before the table walk (e.g.
`miOnlyActive` → `miShowMask` bit, `config.cpp:617`). The **aim subsystem's own vars are
appended into this same file** via `Aim_SaveVars(f)`/`Aim_LoadVar(key,val)`
(`config.cpp:543`, def `aim/aim.cpp:209-265`) — they are NOT in `kEntries[]`; they walk
`G::Vars` (the aim tree's own registry) and self-filter by name prefix (`IsAimVar()`,
`aim.cpp:192-207`).

**Palette / style**: `SetupImGuiStyle()`, `render.cpp:1513-1557+`. Zero rounding everywhere,
1px borders, `kAccent = (160,170,220)` (zui's accent), greys written as `v/255`. Comment
confirms `ImGuiCol_CheckMark` is the accent color (per project CLAUDE.md); full palette block
continues past line 1557 (greys for `WindowBg`/`PopupBg`/`ChildBg`, text hierarchy at 200/215/
235/110 grey levels).

---

## 4. Branding strings

Grepped `-i` for `dih`, `SimpleVisual`, `fpsoverlay`, `infovisual`, `C:\dih`, `C:\infovisual`
across `src/` and `CMakeLists.txt`. Raw hit counts: **`dih` ≈288** (nearly all informal
project-nickname mentions inside comments — "dih's own X", "dih159", "dih-bs15" build
labels — not user-facing strings), **`fpsoverlay` ≈30** (all provenance comments: "ported
from fpsoverlay's X"), **`SimpleVisual` ≈7** (comments noting a file is byte-identical across
the two sibling trees), **`infovisual` = 0** hits in `src/` (only appears in the environment's
working-directory list, not in this repo's source).

Actual **branding artifacts** (what a port needs to rename/retarget), not prose mentions:

| File:line | What it is |
|---|---|
| `CMakeLists.txt:2` | `project(dih)` |
| `CMakeLists.txt:26` | `add_library(dih SHARED ...)` — target name |
| `CMakeLists.txt:134` | `OUTPUT_NAME "dih"` → `dih.dll` |
| `src/types.h:6` | `CONFIG_DIR = "C:\\dih"` |
| `src/types.h:7` | `CONFIG_PATH = "C:\\dih\\default.cfg"` |
| `src/dllmain.cpp:1` | file banner comment `// dih -- debloated TF2 visual overlay` |
| `src/dllmain.cpp:33` | `g_wmLabel[64] = "dih"` — default watermark text |
| `src/dllmain.cpp:572` | `_mkdir("C:\\dih")` |
| `src/dllmain.cpp:573` | `CreateFileA("C:\\dih\\debug.log", ...)` — debug log path |
| `src/hooks/crashlog.cpp:19` | `#define CRASHLOG_DIR "C:\\dih"` |
| `src/hooks/crashlog.cpp:22` | `#define CRASHLOG_FILE "C:\\dih\\crash.log"` |
| `src/hooks/crashlog.cpp:97` | crash-log session header text: `"dih session ... dih.dll %p..%p"` |
| `src/hacks/menu/menu.cpp:3127-3128` | `"dih"` wordmark text drawn next to the hand-drawn logo glyphs |
| `src/hacks/menu/menu.cpp:3201` | `ImGui::Begin("dih", ...)` — ImGui window title/id |
| `src/hacks/menu/menu.cpp:3328` | `"no cfgs in C:\\dih"` empty-state popup text |
| `src/hacks/visuals/flipworld.cpp:18` | `k_rtName = "DihFlipWorldTexture"` — D3D render-target name |
| `src/hacks/visuals/flipworld.cpp:19` | `k_matName = "dih_flipworld/flip"` — material name |
| `src/hacks/visuals/flipworld.cpp:42` | `k_flipToken = 0x31484944` ("DIH1") — cross-process flip-world ownership token (fpsoverlay uses `"FPS1"`, by design, so the two sibling DLLs never collide on one shared mapping) |
| `src/hacks/visuals/flipworld.cpp:151-152` | writes `tf\materials\dih_flipworld\flip.vmt` on disk |
| `src/hacks/movement/psassist.inl:26` | per-map points file `C:\dih\points\<map>.txt` |
| `src/hacks/movement/practice.inl:342,347-349,367` | spawned practice-bot entity names `dihb%d`, `dihbot%d` |
| `src/hacks/movement/routecalc.inl:887` | console-print tag `"dih"` (`RcCon(120,200,255,"dih")`) |

Everything else matching the grep is either (a) a comment explaining provenance ("ported from
fpsoverlay's render.cpp", "same file in SimpleVisual and fpsoverlay — edit both") or (b) an
informal self-reference inside a comment ("dih's own X does Y"). Neither needs renaming for a
port; they're documentation, not identifiers.

---

## 5. Draw paths

| Surface | Hook | Detail |
|---|---|---|
| ImGui UI (menu window, watermark, media player, movement/velocity indicators, classic crosshair, PS overlays, route overlay, edge-bug overlay) | **Primary**: `vgui::IPanel::PaintTraverse` slot 41 (`h_PaintTraverse`, `render.cpp:1092`, installed `thread.cpp:184`). **Fallback**: `IDirect3DDevice9::EndScene` slot 42 (`h_EndScene`, `render.cpp:1155`, installed `thread.cpp:380`) — used "when vgui never painted" (loading screens, minimized window), per `render.cpp:962`. | `BuildImGuiFrame()` (`render.cpp:965-1002`) is the single builder called by whichever of the two gets there first each frame; it calls `RenderPsOverlay/RenderRouteOverlay/RenderEdgeBugOverlay/RenderWatermark/RenderMediaPlayer/RenderVelocityIndicator/RenderMoveIndicators/RenderCrosshair/RenderPsPointWindow/RenderMenu`, then `ImGui::Render()` + `SplitDrawData()` (`render.cpp:858`), which splits the draw data into a "world" half (drawn immediately inside the vgui/EndScene pass) and a "menu" half (drawn later, flagged via `s_menuScene`, `render.cpp:856,1384-1387`, consumed back in `h_EndScene` at `render.cpp:1165`). |
| CS:GO scope / crosshair overlays (own paint path, optional) | `CHudScope::Paint`, `CHudScopeCharge::Paint`, `CSniperDot::GetRenderingPositions` pattern hooks (`scope_csgo.inl:420-445`); optionally its own `IPanel::PaintTraverse` slot 41 instance under `#ifdef SCSGO_OWN_PAINTTRAVERSE` (not dih's main one — a second install) | Separate from the main ImGui PaintTraverse hook; draws through the game's own VGUI surface interface (`s_scCsSurface`, resolved from `MatSystemSurface008`/`VGUI_Surface03x`). |
| World-space overlays (ambient occlusion, depth of field, frame blending/motion blur, screen effects/bloom/color-correction, "screenfx" shared depth) | `IClientMode::DoPostScreenSpaceEffects` slot 39 (`k_slotDoPostSSE`, `screenfx.cpp:1337,1529`) for the main screen-space pass; `IVRenderView::ViewDrawFade` slot 24 (`k_slotViewDrawFade`, `screenfx.cpp:1207,1520`) for the gun/viewmodel's own pass (frame-blend runs this pass **twice** per `CLAUDE.md`: world+players off the post slot, gun off `ViewDrawFade`); `IDirect3DDevice9::SetRenderState` slot 57 (`ScreenFx_InstallRenderStateHook`, `screenfx.cpp:769`) watches for the engine's own RESZ depth-resolve write to time the pre-translucent frame copy. | This is TF2's equivalent of a "paint/SetupSelection"-style surface hook family — there is no single `Paint()` vtable slot the way some SDKs expose; dih composes the effect pipeline out of `DoPostScreenSpaceEffects` + `ViewDrawFade` + the `SetRenderState` depth-timing hook instead. |
| Flip-world mirror | `IVEngineClient::WorldToScreenMatrix` slot 36, `IVRenderView::Push2DView` slot 39, `IClientMode::CreateMove` slot 21, `IViewRender::LevelInit`/`LevelShutdown` slots 1/2 (`flipworld.cpp:660-694`) | Own small hook set, independent of the above; rides the shared depth source `screenfx.cpp` already resolves. |
| **No separate "SetupSelection"/entity-paint hook exists.** TF2's dih has no analog to a per-entity ESP paint callback the way some CS:GO SDKs expose one — entity-adjacent effects (no-hats, ragdoll removal, prop-fade removal) are done by intercepting the *creation/setup* calls (`CParticleProperty::Create`, `CreateTFRagdoll`, `SetupBones`, `DrawModelExecute`) rather than a draw-time entity hook. State this explicitly to anyone porting an ESP-style ("paint every entity") feature: it needs a **new** hook (likely `DrawModelExecute` slot 19, already hooked for no-hats, or a new `IVModelRender`/`IVRenderView` slot), not a reuse of an existing paint path. | |

---

## Notes for the porter

- dih has **two parallel config/var systems** living in one `.cfg` file: dih's own flat
  `g_`-global + `kEntries[]` table (`config.cpp`), and the aim subsystem's `ConfigVar<T>` /
  `Vars::` tree with its own `IsAimVar()` prefix filter. A ported botox feature that is purely
  visual/movement belongs in the first; a ported aim/backtrack/resolver-style feature belongs
  in the second (`src/hacks/aim/vars.h` + `src/hacks/aim/hooks.cpp` + whitelist in
  `aim.cpp`).
- MinHook is one-hook-per-target: before adding a new hook, check both tables above — several
  targets (`CreateMove`, `SetSequence`) already have an ownership-handoff pattern (`Aim_Active()`
  gate) that any new hook on the same target must respect.
- The aim subsystem's whitelist (`aim.cpp:36-97`) is the install gate for anything added to
  `hooks.cpp` — a ported hook can be written there and left off `s_vHookWhitelist` to stage it
  compiled-but-inert, then flipped on later by adding its name to the list.
