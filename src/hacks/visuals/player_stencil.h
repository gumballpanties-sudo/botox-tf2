#pragma once
// botox hacks/visuals/screen/player_stencil.{h,cpp}: "player stencil" (crisp outline of a set px
// width, visible / occluded colours), "internal player glow" (blurred edge or bone capsules inside
// the silhouette) and botox's own full-res "player glow" (gradient / wave / normal blending),
// all off one stencil mask in d3d9.
//
// TF2 wiring: aim/visuals/chams.cpp Glow_Render (DoPostScreenSpaceEffects, main thread) marks the
// players into the engine stencil -- 2 where a pixel is visible, 1 where it is behind something
// (botox's CS:GO glow values: red = stencil 2..127, green = 1) -- fills PlayerStencilSettings and
// calls PlayerStencil_Submit, which queues the pass onto the render thread right behind those
// draws (material system call queue, screenfx.cpp ScreenFx_QueueFunctor). Plain POD: the aim TU
// includes this header.

struct PlayerStencilCapsule { float a[2], b[2], sigma[2]; };   // ndc endpoints, sigma in ndc y units

// one mask pass (botox fill_t): frame px whose stencil passes (func, ref, test mask) write 1 into channels
// (r g b = class 1 2 3); zero = clear them there instead. Raw d3d9 numbers: this header stays d3d-free
// (player_stencil.cpp static_asserts them). D3DCMP_LESS 2, EQUAL 3, LESSEQUAL 4; RED 1, GREEN 2, BLUE 4.
struct PlayerStencilFill { unsigned func = 3, ref = 0, channels = 1; bool zero = false; unsigned mask = 0xFFFFFFFFu; };

// botox clear_t: after the read, where (stencil & test) == ref the write bits go to 0 (colour untouched);
// other users' bits (web surface 0x80) stay. write 0 = no clear (players: the engine still reads them)
struct PlayerStencilClear { unsigned test = 0, ref = 0, write = 0; };

// the band aim/visuals/chams.cpp's model glow stamps in ((stencil & test) == ref = 0x50..0x57, low bits only).
// The player mask zeroes it too: a model pass that never reached Draw (target / depth-stencil mismatch) leaves
// its refs, and they must not read as visible players. Player refs are 1 / 2, the engine glow's up to 3: never in it
inline constexpr unsigned kModelBandTest = 0x78;
inline constexpr unsigned kModelBandRef  = 0x50;

struct PlayerStencilSettings
{
	static constexpr int kMaxCapsules = 768;   // 32 players x the TF2 hitbox set (17-18) + headroom
	static constexpr int kMaxFills = 4;
	bool  outline = false, glow = false, inner = false, innerBones = false;
	// default = the player program: red where 2 <= stencil, zeroed where 128 <= stencil (vgui popups, web
	// 0x80) and in the model glow band (refs a skipped model pass left), green where stencil == 1.
	// aim/visuals/chams.cpp model glow sets its own
	PlayerStencilFill fills[kMaxFills] = { { 2, 1, 1, false }, { 4, 128, 1, true }, { 3, kModelBandRef, 1, true, kModelBandTest }, { 3, 1, 2, false } };
	int   nFills = 4;                                          // 0 = shader warm-up only (PlayerStencil_Warm)
	PlayerStencilClear clear;                                  // players: none
	bool  model = false;                                       // model glow pass: reached Draw -> PlayerStencil_ModelLive
	bool  probe = false;                                       // PlayerStencil_Probe: the Draw checks alone, no pass
	float visible[4] = {}, invisible[4] = {};                  // c1 c2: stencil colours
	float third[4] = {};                                       // c80: class 3 stencil colour
	float thickness = 1.5f;                                    // c0.w, c0.z = ceil
	float glowVisible[4] = {}, glowInvisible[4] = {};          // c3 c4
	float glowVisibleOuter[4] = {}, glowInvisibleOuter[4] = {};// c5 c6
	float glowThird[4] = {}, glowThirdOuter[4] = {};           // c81 c82: class 3 glow
	float gradient[4] = {};                                    // c7: start, 1/(1-start), radius, additive
	float wave[4] = {};                                        // c8: on, phase, 1/size, strength
	float waveColor[4] = {};                                   // c9
	float innerVisible[4] = {}, innerInvisible[4] = {};        // c10 c11
	float outlineWave[4] = {}, outlineWaveColor[4] = {};       // c12 c13: stencil outline accent wave
	// wave anchored to each player's body: c15 = glow anchor, box count, outline anchor;
	// boxes = ndc bounds per player (x0 y0 x1 y1, main thread) -> c16.., phase offsets -> c48..
	static constexpr int kMaxBoxes = 32;
	float waveAnchor[4] = {};
	float waveAngle[4] = { 1.f, 0.f, 1.f, 0.f };               // c14: glow cos sin, outline cos sin (angle slider)
	float boxes[kMaxBoxes][4] = {}, boxPhase[kMaxBoxes][4] = {};
	float innerReach[4] = {};                                  // c7 for the inner passes: intensity, -, size
	int   nCapsules = 0;
	PlayerStencilCapsule capsules[kMaxCapsules];
};

// Main thread, after the stencil marking draws. Copies s (a ring of 16: players + viewmodel + dropped
// weapons a frame, a queued frame behind, plus the preview). bPreview = false routes to the live,
// screen-sized target set (unchanged default: every existing call site keeps drawing the live
// instance); true routes to the second, 520x800-ish esp-preview target set (aim/visuals/chams.cpp
// Glow_RenderPreview) so the two never thrash rebuilding each other's D3DPOOL_DEFAULT textures
// every frame over their differing sizes.
void PlayerStencil_Submit(const PlayerStencilSettings& s, bool bPreview = false);
// main thread: shaders built and never failed = a queued pass will read (and clear) the stamps. Model glow
// stamps nothing before this (botox impl_t::ready); PlayerStencil_Warm queues the shader build alone.
bool PlayerStencil_Ready();
void PlayerStencil_Warm();
// main thread: the last model glow pass reached Draw (so its clear ran) and queued functors still run. A pass
// that skipped Draw (target / depth-stencil mismatch, build fail) left its refs: stamp nothing until a queued
// PlayerStencil_Probe sees the Draw checks pass again (botox model_live / probe)
bool PlayerStencil_ModelLive();
void PlayerStencil_Probe();
// shaders refused / targets failed for good: outline + inner glow off, glow back to the halo.
// Device-level (shaders are shared by both instances), so this covers the preview path too.
bool PlayerStencil_Failed();
// render.cpp h_Reset: the DEFAULT-pool targets must be gone before the device resets (shaders
// survive). Releases both instances' targets.
void PlayerStencil_ReleaseTargets();
