cmake_minimum_required(VERSION 3.16)
project(botox)

# C++20: the aim tree (src/hacks/aim) uses std::format, designated initialisers,
# concepts and <ranges>. botox's own sources are C++17-clean and build fine at 20.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/src/dependencies/imgui)
set(MH_DIR    ${CMAKE_SOURCE_DIR}/src/dependencies/minhook)

# ==== aimbot subsystem =======================================================================
# src/hacks/aim is the aim half of the DLL: its own engine SDK layer, hook plumbing and the aimbot
# feature tree. It includes botox's single copy of ImGui and MinHook directly -- two ImGui
# builds in one DLL would be an ODR violation.
set(AIM_DIR ${CMAKE_SOURCE_DIR}/src/hacks/aim)

# The whole subsystem: the engine layer (defs/sdk/helpers/utils), the aim features, every
# engine hook (hooks.cpp) and the entry point. Each MAKE_HOOK in hooks.cpp registers itself at
# static-init time; CHooks::Initialize's whitelist (aim.cpp) decides which ones are installed,
# so none of them can collide with a hook botox already owns.
file(GLOB AIM_SRC CONFIGURE_DEPENDS ${AIM_DIR}/*.cpp)
# project-local core-only freetype (no png/brotli/bz2/zlib) -- global vcpkg untouched
set(FREETYPE_STATIC_DIR "${CMAKE_SOURCE_DIR}/vcpkg_local/x64-windows-static")

add_library(botox SHARED
    src/dllmain.cpp
    src/hooks/netvars.cpp
    src/hacks/visuals/visuals.cpp
    src/hacks/visuals/lagres.cpp
    src/render/render.cpp
    src/hacks/screen/colorcorrect.cpp
    src/hacks/screen/bloom.cpp
    src/hacks/screen/screenfx.cpp
    src/hacks/screen/ssao.cpp
    src/hacks/screen/dof.cpp
    src/hacks/screen/frameblend.cpp
    src/hacks/hud/killfeed.cpp
    src/hacks/hud/crosshair.cpp
    src/hacks/hud/watermark.cpp
    src/hacks/hud/watermark_styles.cpp
    src/hacks/misc/boot_screen.cpp
    src/hacks/hud/mediaplayer.cpp
    src/hacks/misc/unitE_events.cpp
    src/hacks/hud/hitmarker.cpp
    src/hacks/misc/killeffects.cpp
    src/hacks/misc/killeffects_world.cpp
    src/hacks/hud/points.cpp
    src/hacks/hud/spectator_list.cpp
    src/hacks/misc/discord_ex.cpp
    src/hacks/menu/debugmenu.cpp
    src/hacks/menu/unitE_menu.cpp
    src/hacks/hud/movind.cpp
    src/hacks/menu/menu.cpp
    src/hacks/menu/config.cpp
    src/hooks/thread.cpp
    src/hacks/movement/prediction.cpp
    src/hacks/movement/movement.cpp
    src/hacks/menu/unitA_menu.cpp
    src/hacks/network/netregion.cpp
    src/hacks/network/equipregion.cpp
    src/hacks/network/mmfix.cpp
    src/hacks/hud/scoreboard.cpp
    src/hooks/crashlog.cpp
    src/hacks/visuals/skins.cpp
    src/hacks/visuals/flipworld.cpp
    src/hacks/hud/velgraph.cpp
    src/hacks/hud/keystroke.cpp
    src/hacks/movement/detections.cpp
    src/hacks/movement/recorder.cpp
    src/hacks/menu/unitB_menu.cpp
    src/hacks/menu/unitC_menu.cpp
    src/hacks/menu/unitF_menu.cpp
    src/hacks/screen/screenfx_passes.cpp
    src/hacks/visuals/world_fx.cpp
    src/hacks/visuals/world_fx2.cpp
    src/hacks/menu/unitD_menu.cpp
    src/hacks/visuals/reflections.cpp
    src/hacks/visuals/mc_world.cpp
    src/hacks/menu/unitD3_menu.cpp
    src/hacks/hud/gif_tex.cpp
    src/hacks/hud/avatar_cache.cpp
    src/hacks/screen/blur_rect.cpp
    src/hacks/menu/fonts.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_dx9.cpp
    ${IMGUI_DIR}/imgui_impl_win32.cpp
    ${IMGUI_DIR}/freetype/imgui_freetype.cpp
    ${MH_DIR}/hde/hde64.c
    ${MH_DIR}/buffer.c
    ${MH_DIR}/hook.c
    ${MH_DIR}/trampoline.c
    ${AIM_SRC}
)

target_include_directories(botox PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${IMGUI_DIR}
    ${MH_DIR}
    "${FREETYPE_STATIC_DIR}/include"
)

target_link_directories(botox PRIVATE
    "${FREETYPE_STATIC_DIR}/lib"
    "$<$<CONFIG:Debug>:${FREETYPE_STATIC_DIR}/debug/lib>"
)

target_link_libraries(botox PRIVATE
    d3d9
    winmm
    user32
    gdi32
    dwmapi
    psapi
    optimized freetype debug freetyped
)

target_compile_definitions(botox PRIVATE
    _WIN32_WINNT=0x0A00
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    # IMGUI_ENABLE_FREETYPE lives in imconfig.h -- defining here too = C4005 spam
    # the aim tree expects these two from the build system.
    _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR
    __CONFIGURATION__="botox"
    __PLATFORM__="x64"
)

# The aimbot is the hot path (projectile sim, movement sim, bone setup). botox's global
# /O1 /Os is a size choice for the visual features; build the aim tree for speed.
set_source_files_properties(${AIM_SRC} PROPERTIES
    COMPILE_OPTIONS "/FI${AIM_DIR}/prefix.h;/bigobj;$<$<CONFIG:Release>:/O2>"
)

# Size opt: /O1 favor size, /GL+LTCG whole-program, strip unused w/ /OPT:REF+ICF
# /MP = parallel cl per source. Pure build-time, no codegen change; movement.cpp is a
# separate cl invocation anyway (its own /O2 below) so it overlaps the rest of the TUs.
target_compile_options(botox PRIVATE
    /MP
    $<$<CONFIG:Release>:/O1 /Os /GL /Gy /Gw>
)

# ...except the engine-prediction sim -- the one genuinely hot loop here, so hot it runs
# on a per-cmd clock + sim budget (k_cmdSimCap) with a reserve fenced off for AirStuck.
# Compiling THAT for size directly costs sims per tick, which is the resource the feature
# logic is rationing. /O2 (speed) on these files only. prediction.cpp is the dih107 split
# of the same sim core (Pred_Simulate, snapshots) and lost this override when it moved.
set_source_files_properties(src/hacks/movement/movement.cpp src/hacks/movement/prediction.cpp PROPERTIES
    COMPILE_OPTIONS "$<$<CONFIG:Release>:/O2>"
)
# /MAP: crash.log prints every frame as `module+0xRVA`, and the dll loads at a random base, so
# botox.dll's own offsets are only readable against the link map. Costs nothing at runtime and
# ships nothing extra -- build\Release\botox.map stays next to the dll.
target_link_options(botox PRIVATE
    $<$<CONFIG:Release>:/OPT:REF /OPT:ICF /LTCG /MAP>
)

set_target_properties(botox PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    OUTPUT_NAME "botox"
    PREFIX ""
    SUFFIX ".dll"
)

set(CMAKE_GENERATOR_PLATFORM x64 CACHE STRING "" FORCE)

# Gate test for flip world's engine-free math + cl_flipviewmodels hold (header-only, no game).
# Build + run: cmake --build build --config Release --target flipworld_test && build\Release\flipworld_test.exe
add_executable(flipworld_test tests/flipworld_test.cpp)

# Gate test for the auto backstab's engine-free math (src/hacks/aim/bs_math.h): the server's own
# knife test as ground truth, Monte Carlo calibration of the odds, swing order, lerp, snapshot track.
# Build + run: cmake --build build --config Release --target backstab_test && build\Release\backstab_test.exe
add_executable(backstab_test tests/backstab_test.cpp)

# Gate test for fake movement anti-snag (src/hacks/movement/fm_slide.h): box-world hull traces, lip step,
# seam slide, crease/tall wall/ceiling/startsolid must NOT move.
# Build + run: cmake --build build --config Release --target fmslide_test && build\Release\fmslide_test.exe
add_executable(fmslide_test tests/fmslide_test.cpp)

# Gate test for the route calculator core (src/hacks/movement/routesim.h): TF2 z physics vs the SDK and vs
# hops recorded in game, and the combo search vs a brute force.
# Build + run: cmake --build build --config Release --target routesim_test && build\Release\routesim_test.exe
add_executable(routesim_test tests/routesim_test.cpp)

# Gate test for fake movement seam gate (src/hacks/movement/fm_seam.h): box/wedge brushes, seam window.
# Build + run: cmake --build build --config Release --target fmseam_test && build\Release\fmseam_test.exe
add_executable(fmseam_test tests/fmseam_test.cpp)

# Gate test for the CS:GO viewmodel bob/lower/dip port (src/hacks/visuals/vm_csgo.h, shared w/ fpsoverlay).
# Build + run: cmake --build build --config Release --target vmcsgo_test && build\Release\vmcsgo_test.exe
add_executable(vmcsgo_test tests/vmcsgo_test.cpp)

# Gate test for the CS:GO sniper scope port (src/hacks/visuals/scope_csgo.h, shared w/ fpsoverlay).
# Build + run: cmake --build build --config Release --target scopecsgo_test && build\Release\scopecsgo_test.exe
add_executable(scopecsgo_test tests/scopecsgo_test.cpp)

# Gate test for the edge bug stance filter (src/hacks/movement/eb_stance.h): which of the search's run
# shapes "ducked only" / "stand only" may latch.
# Build + run: cmake --build build --config Release --target ebstance_test && build\Release\ebstance_test.exe
add_executable(ebstance_test tests/ebstance_test.cpp)

# Gate test for the jumpbug look-ahead (routesim.h rs::JumpbugWillHit): the gate AutoJumpbug asks
# before it may hold the crouch, checked against a hand-written replay of the same bind.
# Build + run: cmake --build build --config Release --target jbpredict_test && build\Release\jbpredict_test.exe
add_executable(jbpredict_test tests/jbpredict_test.cpp)

# Gate test for the media player's cover art geometry (src/hacks/hud/mp_art.h): the scale +
# centre crop handed to winrt's BitmapTransform, including the rect-out-of-bounds case that
# would throw inside the decode and silently kill the art.
# Build + run: cmake --build build --config Release --target mediaplayer_test && build\Release\mediaplayer_test.exe
add_executable(mediaplayer_test tests/mediaplayer_test.cpp)

# Gate test for unit E's pure logic (src/hacks/misc/unitE_logic.h): ehandle decode (spectator
# list), discord IPC JSON escaping, and the shared fade-fraction math.
# Build + run: cmake --build build --config Release --target unitE_test && build\Release\unitE_test.exe
add_executable(unitE_test tests/unitE_test.cpp)

# Gate test for unit E5's gif frame math (src/hacks/hud/gif_frame.h): frame-index-from-time
# with variable per-frame delays and infinite loop wrap.
# Build + run: cmake --build build --config Release --target giftex_test && build\Release\giftex_test.exe
add_executable(giftex_test tests/giftex_test.cpp)

# Gate test for unit E5's avatar cache arithmetic (src/hacks/hud/avatar_pure.h): friendsID ->
# steamid64, and the RGBA->BGRA swizzle before a d3d9 upload.
# Build + run: cmake --build build --config Release --target avatar_cache_test && build\Release\avatar_cache_test.exe
add_executable(avatar_cache_test tests/avatar_cache_test.cpp)

# Gate test for bloom's numbers (src/hacks/screen/shaders/fx_bloom_*.hlsl and the fxc output beside them):
# gaussian weights, the knee, the radius -> pass count map, and whether the compiled headers
# still match the shaders (nothing at build time compiles hlsl, so a stale .h ships silently).
# Build + run: cmake --build build --config Release --target bloom_test && build\Release\bloom_test.exe
add_executable(bloom_test tests/bloom_test.cpp)
target_compile_definitions(bloom_test PRIVATE BLOOM_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/hacks/screen/shaders")

# Gate test for frame blending's maths (src/hacks/screen/fbmath.h): the reprojection identities a
# wall and a running player have to satisfy, the bone conversion, and the shutter raised as a
# screw motion. Also catches an fx_fb_*.hlsl edited without rerunning fxc, and a shader rebuilt
# with different constant registers than frameblend.cpp uploads to.
# Build + run: cmake --build build --config Release --target fbmath_test && build\Release\fbmath_test.exe
add_executable(fbmath_test tests/fbmath_test.cpp)
target_compile_definitions(fbmath_test PRIVATE FB_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/hacks/screen/shaders")

# Gate test for the strafe air-accel math (src/hacks/movement/airlean.h): the closed-form best-gain and
# free-turn wish angles vs a brute-force sweep of the same AirAccelerate tick.
# Build + run: cmake --build build --config Release --target airlean_test && build\Release\airlean_test.exe
add_executable(airlean_test tests/airlean_test.cpp)

# Gate test for the low fps viewmodel (src/hacks/visuals/viewmodel_fps.h): camera-space hold
# round-trips exactly, a held pose rides the moved camera, the step cadence matches the rate.
# Build + run: cmake --build build --config Release --target vmfps_test && build\Release\vmfps_test.exe
add_executable(vmfps_test tests/vmfps_test.cpp)

# Gate test for the grenade lob pitch solver (src/hacks/aim/lob_solve.h): the secant
# root-finder against a closed-form parabola, independent of F::ProjSim.
# Build + run: cmake --build build --config Release --target lob_solve_test && build\Release\lob_solve_test.exe
add_executable(lob_solve_test tests/lob_solve_test.cpp)
# Gate test for Unit A's pure decisions (src/hacks/movement/movement_ex_pure.h): hsw sign,
# delay hop's tick gate, long jump's adaptive-cancel reasons.
# Build + run: cmake --build build --config Release --target movementex_test && build\Release\movementex_test.exe
add_executable(movementex_test tests/movementex_test.cpp)

# Gate test for Unit A3's pure decisions (src/hacks/movement/movement_tricks2_pure.h): bounce tangent
# flip, tung surf's widened ramp band, edge bug style preset bundles.
# Build + run: cmake --build build --config Release --target movementtricks2_test && build\Release\movementtricks2_test.exe
add_executable(movementtricks2_test tests/movementtricks2_test.cpp)
# Gate test for Unit A2's pure decisions (src/hacks/movement/movement_edge_pure.h): edge skip's
# duck gate, blockbot's head-mode wish + strafe sign.
# Build + run: cmake --build build --config Release --target movementedge_test && build\Release\movementedge_test.exe
add_executable(movementedge_test tests/movementedge_test.cpp)

# Gate test for the pixel surf indicator rule (src/hacks/movement/ps_surf.h): surf vs air stuck vs
# texture bug vs a hull wedged on a crate, all of which pin vz at -halfgrav identically.
# Build + run: cmake --build build --config Release --target pssurf_test && build\Release\pssurf_test.exe
add_executable(pssurf_test tests/pssurf_test.cpp)

# Gate test for the botox air stuck / pixel surf pure math (src/hacks/movement/airstuck_pure.h): the
# stamp rotation that carries a proven pin into the sent view, tick scaling, the clarity grid
# step, the along-wall strafe yaw and the surf-pin test.
# Build + run: cmake --build build --config Release --target airstuck_test && build\Release\airstuck_test.exe
add_executable(airstuck_test tests/airstuck_test.cpp)

# Gate test for the whole-map pixel surf candidates (src/hacks/movement/ps_map.h): a brush wall face's
# top edge and its real ends, wedges/ramps refused, and the merge that turns a row of brushes
# back into one surf.
# Build + run: cmake --build build --config Release --target psmap_test && build\Release\psmap_test.exe
add_executable(psmap_test tests/psmap_test.cpp)

# Gate test for the crash log (src/hooks/crashlog.cpp) and the unique pattern scan
# (src/common.h): it really faults -- AV / divide by zero under __except -- and reads the record
# back. Writes to its own scratch file, never C:\botox-tf2\crash.log.
# Build + run: cmake --build build --config Release --target crashlog_test && build\Release\crashlog_test.exe
add_executable(crashlog_test tests/crashlog_test.cpp src/hooks/crashlog.cpp)
target_include_directories(crashlog_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${IMGUI_DIR} ${MH_DIR})
target_link_libraries(crashlog_test PRIVATE psapi)
target_compile_definitions(crashlog_test PRIVATE
    NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
    CRASHLOG_DIR="${CMAKE_CURRENT_BINARY_DIR}/testout"
    CRASHLOG_FILE="${CMAKE_CURRENT_BINARY_DIR}/testout/crash_test.log"
    CRASHLOG_TEST_FILE="${CMAKE_CURRENT_BINARY_DIR}/testout/crash_test.log"
)
# /EHa: the test mixes C++ throw with __except on purpose (the throw must NOT be logged).
target_compile_options(crashlog_test PRIVATE /EHa)

# Gate test for the metaball envelope (src/hacks/hud/metaball.h): every slice sits on the iso
# surface, the neck pinches at d = 2*sqrt(2)*r and never bulges back, Pad covers the tips.
# NOTE the mitosis indicator no longer draws it -- the split pinches the glyphs themselves
# now (migoo.h). The header and this test stay because the shape is the only blob primitive here.
# Build + run: cmake --build build --config Release --target metaball_test && build\Release\metaball_test.exe
add_executable(metaball_test tests/metaball_test.cpp)

# Gate test for the mitosis warp (src/hacks/hud/miwarp.h): zero warp at both ends of the split,
# peak at the half-way pinch, area preserved, pivot on the outer edge so each label reaches
# across the join. The squash & stretch: text fallback AND inside the migoo.h picture.
# Build + run: cmake --build build --config Release --target miwarp_test && build\Release\miwarp_test.exe
add_executable(miwarp_test tests/miwarp_test.cpp)

# Gate test for the mitosis split (src/hacks/hud/migoo.h): the chamfer distance transform against
# brute force, the furrow only ever thinning letters (never adding ink), the cut, the timeline,
# and mig::Render end to end on imgui's freetype atlas (tahoma bold) for both layouts, every
# frame written to build/testout/mitosis_*.ppm.
# Build + run: cmake --build build --config Release --target migoo_test && build\Release\migoo_test.exe
add_executable(migoo_test tests/migoo_test.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/freetype/imgui_freetype.cpp
)
target_include_directories(migoo_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${IMGUI_DIR} "${FREETYPE_STATIC_DIR}/include")
target_link_directories(migoo_test PRIVATE
    "${FREETYPE_STATIC_DIR}/lib" "$<$<CONFIG:Debug>:${FREETYPE_STATIC_DIR}/debug/lib>")
target_link_libraries(migoo_test PRIVATE optimized freetype debug freetyped user32 gdi32)
target_compile_definitions(migoo_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
set_target_properties(migoo_test PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Gate test for the build placement port (src/hacks/movement/buildplace.h): the SDK's ground sweep, fit
# and VerifyCorner rules against a box world, plus the box/segment tests the entity scan runs.
# Build + run: cmake --build build --config Release --target buildplace_test && build\Release\buildplace_test.exe
add_executable(buildplace_test tests/buildplace_test.cpp)

# Gate test for the bounding-sphere FOV reject (src/hacks/aim/aim_fovreject.h), the cheap skip
# in front of PlayerBoneInFOV's bone rebuild. Brute-forces the one direction it may not be wrong
# in: a reject must mean no point of the sphere is inside the FOV cone.
# Build + run: cmake --build build --config Release --target aimfov_test && build\Release\aimfov_test.exe
add_executable(aimfov_test tests/aimfov_test.cpp)

# Gate test for the festivizer table (src/dependencies/data/skindata.h k_festiveDefs, generated by
# tools/gen_festivedefs.py): known yes/no defs, and k_wpnList's canFestivize flags vs the schema.
# Build + run: cmake --build build --config Release --target festive_test && build\Release\festive_test.exe
add_executable(festive_test tests/festive_test.cpp)

# Gate test for the menu's layout math (src/hacks/menu/menu_flow.h): the group box notch over its
# own border, the accent rule at a narrow width, the right-aligned header tab strip, and the
# section height round trip the two-column flow runs every frame.
# Build + run: cmake --build build --config Release --target menuflow_test && build\Release\menuflow_test.exe
add_executable(menuflow_test tests/menuflow_test.cpp)

# Gate test for the menu's group box flow (src/hacks/menu/menu_chrome.h) against a real ImGui
# context -- no window, no device: balanced push/pop, heights that converge, boxes that do not
# overlap or leave the panel, and a culled box that keeps its size. Links botox's own ImGui.
# Build + run: cmake --build build --config Release --target menuchrome_test && build\Release\menuchrome_test.exe
add_executable(menuchrome_test tests/menuchrome_test.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/freetype/imgui_freetype.cpp
)
target_include_directories(menuchrome_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${IMGUI_DIR} "${FREETYPE_STATIC_DIR}/include")
target_link_directories(menuchrome_test PRIVATE
    "${FREETYPE_STATIC_DIR}/lib" "$<$<CONFIG:Debug>:${FREETYPE_STATIC_DIR}/debug/lib>")
target_link_libraries(menuchrome_test PRIVATE optimized freetype debug freetyped user32 gdi32)
target_compile_definitions(menuchrome_test PRIVATE
    NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
# Asserts ON in release too: a push/pop leak in the flow is an IM_ASSERT, and the point of
# this target is to hit it.
target_compile_options(menuchrome_test PRIVATE $<$<CONFIG:Release>:/UNDEBUG>)
set_target_properties(menuchrome_test PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Gate test for the settings tab "visuals config" key list (src/hacks/menu/visual_keys.h):
# sorted + unique (binary search), visual keys in, settings/movement/aim/skin keys out.
# Regenerate the list: python tools/gen_visual_keys.py --write
# Build + run: cmake --build build --config Release --target visualkeys_test && build\Release\visualkeys_test.exe
add_executable(visualkeys_test tests/visualkeys_test.cpp)

# Gate test for the MV_DetectWallLines broadphase box (src/hacks/movement/mv_scanbound.h): the one
# trace that replaces sixteen. Re-runs the scan loop's own per-direction arithmetic and asserts
# the box contains every trace it would have run -- safe AND tight.
# Build + run: cmake --build build --config Release --target mvscan_test && build\Release\mvscan_test.exe
add_executable(mvscan_test tests/mvscan_test.cpp)

# Gate test for the edgebug marker's z (src/hacks/movement/rc_edge.h): the walk out to the nearest lip
# of the floor a marker sits on, so a slanted brush reports the height you slide off at instead
# of the height you clicked. Flat / slanted / stepped / walled floors, no engine.
# Build + run: cmake --build build --config Release --target rcedge_test && build\Release\rcedge_test.exe
add_executable(rcedge_test tests/rcedge_test.cpp)

# Gate test for the net settings decision rule (src/hacks/network/netcvar.h) and the movement
# pipeline's wall-clock budget (src/hacks/movement/cmdbudget.h). Covers the two defects behind
# "packet loss + net settings do nothing": the FCVAR_NOT_CONNECTED tier table (a missing
# cl_lagcompensation burned the pump's retries and abandoned the whole preset), TF2's own
# IsConnectedUserInfoChangeAllowed rule, the rate trio opt-in, and that a per-FRAME budget
# sized off a TICK cannot exceed its share of wall clock.
# Build + run: cmake --build build --config Release --target netcvar_test && build\Release\netcvar_test.exe
add_executable(netcvar_test tests/netcvar_test.cpp)

# Gate test for the killstreak carry-over across demos (src/hacks/hud/demostreak.h): demo load
# events keep the streak, a rewind does not count the replayed kills twice, a death still ends it,
# live play unchanged. Same file as fpsoverlay's.
# Build + run: cmake --build build --config Release --target demostreak_test && build\Release\demostreak_test.exe
add_executable(demostreak_test tests/demostreak_test.cpp)

# Gate test for the movement recorder's serialization (src/hacks/movement/recorder_ser.h): round
# trip + corrupt/truncated-buffer refusal.
# Build + run: cmake --build build --config Release --target recorder_test && build\Release\recorder_test.exe
add_executable(recorder_test tests/recorder_test.cpp)

# Gate test for the detection core (src/hacks/movement/detections_core.h): physical edgebug + fresh gate.
# Build + run: cmake --build build --config Release --target detections_test && build\Release\detections_test.exe
add_executable(detections_test tests/detections_test.cpp)
# Gate test for Unit C's ESP pure math (src/hacks/esp/esp_math.h): oof-arrow edge placement
# + distance fade. No SDK deps, so no AIM_DIR flags needed.
# Build + run: cmake --build build --config Release --target esp_math_test && build\Release\esp_math_test.exe
add_executable(esp_math_test tests/esp_math_test.cpp)
# Gate test for Unit D's aspect ratio letterbox/pillarbox math (src/hacks/visuals/world_fx.h).
# Build + run: cmake --build build --config Release && build\Release\worldfx_test.exe
add_executable(worldfx_test tests/worldfx_test.cpp)

# Gate test for Unit D3's sky/clouds pure math (src/hacks/visuals/mc_world_math.h): AngleVectors
# orthonormality, the corner-ray linearity the fixed-function quad draw depends on, and the
# cloud-offset wrap/drift rate.
# Build + run: cmake --build build --config Release --target mcworld_test && build\Release\mcworld_test.exe
add_executable(mcworld_test tests/mcworld_test.cpp)
# Gate test for Unit D2's pure helpers (src/hacks/visuals/world_fx2.h).
# Build + run: cmake --build build --config Release --target worldfx2_test && build\Release\worldfx2_test.exe
add_executable(worldfx2_test tests/worldfx2_test.cpp)
# Gate test for the fonts tab's pure helpers (src/hacks/menu/fonts.h): settings signature, the
# 250ms settle gate, freetype-flags -> vgui face mapping, indicator px.
# Build + run: cmake --build build --config Release --target fonts_test && build\Release\fonts_test.exe
add_executable(fonts_test tests/fonts_test.cpp)

# Gate test for the botox chams layer list (src/hacks/visuals/chams_layers.h): config parse/format
# round trip, garbage skip, dropdown toggle order.
# Build + run: cmake --build build --config Release --target chams_layers_test && build\Release\chams_layers_test.exe
add_executable(chams_layers_test tests/chams_layers_test.cpp)
