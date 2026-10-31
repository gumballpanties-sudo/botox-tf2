# SimpleVisual — session handoff

Read this + `C:\Users\huber\.claude\projects\C--Users-huber\memory\project_simplevisual.md` before touching code.

## What this is
Debloated infovisual fork. TF2 x64 DLL, DX9 Present hook + ImGui menu. Personal frag-video visuals, self-affecting only.
Path: `C:\Users\huber\Desktop\SimpleVisual\cplus`. Separate from fpsoverlay/infovisual — do NOT edit fpsoverlay files for this project.

## Build
```
cmake -B build -A x64
cmake --build build --config Release
```
Output `build\Release\dih.dll` (~1.4 MB, renamed from simplevisual.dll 2026-07-03). If link fails LNK1104 = dll loaded in TF2 → rename old dll (user prefers rename over delete, e.g. dih1.dll).
Size opts on: /O1 /Os /GL + /OPT:REF /OPT:ICF /LTCG. Bulk of size = static freetype (vcpkg `C:/vcpkg/installed/x64-windows-static`, links freetype+brotli+png+zlib+bz2). Pre-freetype build was 725 KB.

## Features (all default OFF, all sliders default 0)
| Feature | Where | Mechanism |
|---|---|---|
| Watermark (simple) | watermark.cpp | ImGui bg drawlist; fixed font size, rounding 0 (scale+rounding options removed); parts all optional: title (g_wmShowLabel), fps, ping, loss% (g_wmShowLoss, INetChannelInfo slot 11 GetAvgLoss*100), time; skips render if text empty |
| Viewmodel sway/offset/nobob | visuals.cpp | cvars cl_wpn_sway_*, cl_bobcycle=999; offset via h_CalcViewModelView pattern hook |
| Fog | visuals.cpp | fog_override + fog_* cvar force; fog_color via m_pszString pointer redirect (restore on eject) |
| Skybox | visuals.cpp | h_R_DrawSkyBox (engine.dll `48 8B C4 55 53 41 54 41 55`) + sv_skyname string redirect |
| Weather rain/snow | visuals.cpp | CPrecipitation entity slot 2047, precipType 0/1, r_rain* cvars; snow sprite via h_FindMaterial (slot 71) redirect particle/rain→particle/snow; NO custom-particle mode |
| No hats | visuals.cpp | h_DrawModelExecute (IVModelRender slot 19): skip IsHatModel draws + bodygroup targets; particle kill via SetShouldSimulate/m_bRemove + CParticlePropCreate hooks block unusual_ |
| Self killfeed | killfeed.cpp | h_FireEvent (IGameEventManager2 slot 8), return false if local not involved |
| Crosshair (simple) | crosshair.cpp | lines/dot/AA/outline only |
| sv_pure bypass | thread.cpp | h_CL_CheckForPureServerWhitelist pattern hook, early-return |
| sv_cheats bypass | visuals.cpp UpdateVisualsCvars | direct value force, restores 0 on toggle-off |
| Remove ragdolls | visuals.cpp h_CreateTFRagdoll | Aletherium sig `48 89 4C 24 ? 55 53 56 57 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 8B 91`, skip original |
| Bunnyhop | bhop.cpp | h_CreateMove = CHLClient::CreateMove (IBaseClientDLL/VClient slot 21), install in thread.cpp. pCmd = CInput::m_pCommands[seq%90] (CInput resolved via Aletherium sig `48 8B 0D ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 85 C0 0F 84 ? ? ? ? F3 0F 10 05`, m_pCommands off 264). Ground/duck from m_fFlags (g_offFlags netvar, FL_ONGROUND=1/FL_DUCKING=2). Aletherium AutoJump re-press logic. **REQUIRED**: h_GetUserCmd (IInput slot 8, hooked off g_input vtable in ResolveInput) returns live m_pCommands slot — engine otherwise sends/predicts CRC-verified COPY snapshotted inside CInput::CreateMove, so post-hook button edits were no-ops (bhop dead until 2026-07-03 fix) |
| Flip world | flipworld.cpp (+ flipmath.h) | Port of fpsoverlay's, same shape. Decisions live in flipmath.h FlipCoreTick (owner, m_yaw, HUD bytes, hand, eject fallback) + FlipFrameTrack; tests/flipworld_test.cpp drives them against a fake engine; every engine number is a flipslot:: constant that fpsoverlay tools/check_vtables.py binds to the binary and to the source wiring. RT made in CViewRender::LevelInit (1) every map, on or off (~6 MB; mid-match allocation races the queued render thread), VMT written once per session. Mirror = CopyRenderTargetToTexture + DrawScreenSpaceRectangle over the CViewSetup rect with INCLUSIVE reversed texels, fired from the first ViewDrawFade after DoPostSSE (after the DOF viewmodel pass). 'Mirrored' is frame-tracked, not timed: FSN RENDER_START / DoPostSSE (world pass) / mirror / IVRenderView::Push2DView (slot 39, NOT 38: the Push3DView overloads sit at 37/38) -- a world frame without the mirror clears it, freezecam and hitches keep it. W2S (36) mirrored except on the game thread inside the 3D pass (beams, sprite glows). Angle-placed HUD = 7 client.dll byte patches toggled on the game thread: damage arcs + medic callers (one linker-folded function), flag callouts, medic + flag arrows, off-screen annotations, flag compass arrows (CTFArrowPanel), PASS Time ball arrow. Hand: IConVar SetValue(int), no flag games; config.cfg guarded by hooking engine CCvarUtilities::WriteVariables; demo = recorded m_bFlipViewModels, SourceTV = none; released in CViewRender::LevelShutdown (2) and FSN; eject waits <=1 s on FlipWorld_Holding, then the off-thread fallback restores m_yaw/HUD bytes only if the game thread stopped ticking, and for a hand it could not release writes only config.cfg's digit. Input: command mirrored (sidemove + IN_MOVELEFT/RIGHT) BEFORE ClientModeShared::CreateMove (taunt turning, karts); m_yaw sign owned; strafe optimizer uses |m_yaw|. One owner per process via mapping Local\TF2FlipWorld-<pid> {owner token, frameMirrored}; when fpsoverlay owns it, dih's DOF viewmodel pass and the EndScene fallback stand down (frame already mirrored). |
| Static amby crosshair | crosshair.cpp | h_GetWeaponCrosshairScale = CTFRevolver::GetWeaponCrosshairScale (fpsoverlay sig `48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B F2 48 8B F9 E8 ? ? ? ? 48 8B D8 48 85 C0 0F 84`), skip original = crosshair stays fixed size |

## imgui — CUSTOM, careful
- Core = **nixware modded 1.89.6** copied from `Desktop\nixware-main\showcase\imgui` (NOT stock):
  - extra enums ImGuiCol_TextHovered / ImGuiCol_TextActive
  - Checkbox: 10px flat animated square
  - SliderScalar: label top-left, value top-right, thin bar below; width = GetColumnWidth() full column width, same for BeginCombo (-10 removed 2026-07-03 — was short of column edge, asymmetric vs SectionHeader line). Ignores PushItemWidth. **I patched fill clamp** (nub spilled left of bar at value 0) — search "Clamp fill to bar bounds" in imgui_widgets.cpp
  - **ColorEdit4 TRAP**: moves cursor UP one row + right-aligns 20x10 swatch. Do NOT use for inline swatches — menu.cpp RowColor uses ColorButton + ColorPicker4 popup instead
  - BeginChildEx: named child = label + thin line (my patch), forces NoBackground/NoScrollbar; menu columns use `BeginChild(ImGui::GetID("colN"))` (NULL name = no decor)
  - EndChild RenderNavHighlight drew white ring around columns ("bars at sides" bug) → fixed via ImGuiCol_NavHighlight alpha 0 in SetupImGuiStyle
- Backends imgui_impl_dx9/win32 are from 1.90.4 — win32 patched: F13-F24/AppBack/AppForward key cases removed (enums absent in 1.89.6)
- Freetype: `src/dependencies/imgui/freetype/` from nixware; IMGUI_ENABLE_FREETYPE defined in CMake

## Fonts (render.cpp, h_Present init)
- Menu font: Tahoma.ttf 12px, flags `MonoHinting | Monochrome` = crisp NO antialias (delusional-fix reference — user hates AA text)
- Bold: tahomabd.ttf 12px, same MonoHinting|Monochrome flags = exact delusional-fix menu_font_bold (user asked back after earlier rejecting; synth Bold flag dropped 2026-07-03), global `g_fontBold`, used by SectionHeader + title

## Menu (menu.cpp) — user is picky, current approved state
- Single window 760x640, NoTitleBar/NoScrollbar, monochrome black/white/dark-gray ONLY (no accent colors)
- Window bg FLAT = ImGuiCol_WindowBg in render.cpp SetupImGuiStyle (**user hand-tunes there**). Old menu.cpp gradient drawlist rect REMOVED — window drawlist clips 5px inset (WindowPadding/2) → gray bars at sides bug
- 3 columns = plain children; sections inline: bold white label + 1px line to column edge (SectionHeader)
- NO boxed section backgrounds (user hated them)
- Options hidden unless feature enabled (if(enable) blocks)
- RowColor = 10x10 swatch (checkbox square size, row top ypos), right-aligned to column edge, popup ColorPicker4 with RGB drag values + AlphaBar
- Sliders: NoInput flag removed everywhere — ctrl+click OR right-click types value. imgui_widgets.cpp SliderScalar+DragScalar patched: temp-input branch forces g.NavActivateId=id + PreferInput so InputTextEx grabs ActiveId (without it box lived 1 frame + died); right-click entry added
- eject button right margin mirrors title left padding (WindowPadding.x)
- Skybox: preset combo + "custom" entry; InputText only when custom selected (static s_skySel, -2=unsynced sentinel)
- INSERT = menu toggle, F11 = soft eject

## User style rules (hard)
- CAVEMAN mode replies (see global CLAUDE.md)
- Everything default disabled, sliders 0
- No embedded fonts/icons/assets in binary
- Menu: monochrome, no rounded stuff, flat

## Dead-code cleanup 2026-07-03
Removed: g_accentColor, g_run, g_d3dDev, g_hSelf, g_snowLength (r_rainlength hardcoded 0 for snow), g_offTeam/g_offVelocity/g_offOrigin netvars, ConVar::ForceValue, ResetCVarInt (→ CVar_ForceInt), per-frame SetupImGuiStyle in h_Present (style = init-time only now), <fstream> (file-exists via GetFileAttributesA), IMGUI_ENABLE_FREETYPE CMake define (imconfig.h has it — was C4005 spam). netvarsReady gate = m_iClass + m_lifeState. Old cfg keys accentColor/snowLength ignored on load. DLL 1.44 MB, 0 warnings.

## 2026-07-03 dih rename + bhop fix pass
- DLL renamed: OUTPUT_NAME "dih" (CMakeLists). Menu title, watermark fallback, console prints = "dih". Config/log paths unchanged (C:\simplevisual).
- Bhop ground check now Aletherium-exact: new g_offGroundEntity netvar (m_hGroundEntity, grounded = handle != -1), m_fFlags FL_ONGROUND = fallback only, FL_DUCKING still from m_fFlags. Netvar-fail DebugLogs added + one-shot "bhop: active" log in h_CreateMove — check C:\simplevisual\debug.log if bhop still dead.
- Menu labels ALL lowercase now (user rule — keep new labels lowercase).
- "Outline thick" → "outline thickness", range 0-3 → 0-10.
- Watermark draws with g_fontBold (tahomabd) — same font as bold menu labels.
- Removed dead g_inGame atomic (write-only).
- Bhop root cause FOUND via diag log (data all correct: gnd=1/1, btn had IN_JUMP, "first repress" fired, GetUserCmd live path hit — yet no jumps): **TF2 x64 CInput::ValidateUserCmd re-checks each cmd vs CRC-verified copy before send and reverts tampered cmds**. Aletherium kills it with empty hook (CInput_ValidateUserCmd.cpp, original never called). Ported: h_ValidateUserCmd no-op in bhop.cpp, sig `48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B F9 41 8B D8` client.dll, installed in ResolveInput. g_eject → calls original. ALL THREE hooks now needed for usercmd edits: CreateMove(21) + GetUserCmd(8) + ValidateUserCmd(sig).
- SectionHeader pre-Dummy(0,4) removed — gap above headers was ~10px fatter than below ("bottom of section too low" complaint), now ~symmetric.
- FULL rename simplevisual → dih: CMake project/target = dih, CONFIG_DIR/PATH = C:\dih (+old default.cfg copied over), debug log = C:\dih\debug.log, load string "dih loaded", wm default label "dih". Folder Desktop\SimpleVisual NOT renamed (IDE open). Watermark separator " | " → " / " = USER hand edit, keep.

## Known untested
- In-game verify pending for: nav-ring fix, RowColor swatch layout, bold headers, slider clamp, full-width sliders/combos, bhop GetUserCmd fix. All compile clean.
- Config round-trip after defaults change (old default.cfg at C:\simplevisual\default.cfg may hold pre-zero values — LoadConfig overwrites zeros with saved; expected behavior).
- Eject path (soft, DLL stays loaded) unverified in this fork.

## Key infra facts (inherited from infovisual, confirmed slots)
- IVModelRender DrawModelExecute=19; IMaterialSystem FindMaterial=71; IGameEventManager2 FireEvent=8 (TF2 has extra AddListenerGlobal at 6)
- ConVar x64: parent+0x38, string+0x48, len+0x50, float+0x54, int+0x58
- PatternScan walks committed pages only (SEH); never __try + std::vector same func (C2712) → noinline helpers
- Engine calls on render thread only (h_Present); netvar reads guarded by g_netvarsReady
- minhook quirk: hook.c includes ../include/MinHook.h → src/include/MinHook.h must exist
