#pragma once
// Plain-type bridge between dih's DrawModelExecute hook (visuals.cpp) and the chams/glow
// implementation in aim/visuals/chams.cpp (which needs the aim SDK: IMaterial, KeyValues, CTFPlayer).
// Same split as aim/visuals/esp.h: no aim or windows types cross this header.

using ChamsDrawFn = void(__fastcall*)(void*, const void*, const void*, const void*);

// Draws the model through every chams/glow pass it wants and returns true; returns false when
// nothing applies (caller draws it normally). `vmKind` is visuals.cpp's ViewmodelDrawKind
// (0 not viewmodel, 1 arms stack, 2 weapon in hands), `modelName` may be null.
bool Chams_DrawModel(ChamsDrawFn fnOriginal, void* rcx, const void* pState, const void* pInfo, const void* pBones,
                     int entIdx, const char* modelName, int vmKind);

// Fast gate for the hook's cheap path: any chams/glow option that can draw anything.
bool Chams_AnyActive();

// enemy chams on + a backtrack material picked: aim/aimbot/backtrack.cpp builds records (full bones) for it
bool Chams_BacktrackActive();

// CSoundEmitterSystem::EmitSound peek (visuals.cpp h_EmitSound): sound chams timestamps.
void Chams_OnSound(int entindex, float volume, const float* origin);

// screenfx.cpp DoPostScreenSpaceEffects, before Glow_Render: fully cloaked spies (the engine never
// draws them) through the "cloaked material" list.
void Chams_RenderCloaked();

// Material lifetime: released on level shutdown (DecRef + DeleteIfUnreferenced), rebuilt
// lazily on the first draw of the next map. Both are called from visuals.cpp Chams_LevelInit /
// Chams_LevelUnload (flipworld.cpp's CViewRender::LevelInit/LevelShutdown hook).
void Chams_ReleaseMaterials();

// ---- player glow (aim/visuals/chams.cpp, Amalgam Features/Visuals/Glow on fpsoverlay vmglow's slots) ----
void Glow_LevelInit();     // flipworld.cpp CViewRender::LevelInit hook: render targets + materials
void Glow_LevelUnload();   // no-op: RTs live across maps, remade only on a backbuffer size change
void Glow_Render();        // screenfx.cpp DoPostScreenSpaceEffects, before the original: stencil + halo pass
bool Glow_Rendering();     // true inside Glow_Render's model draws: h_DrawModelExecute passes straight through

// ---- esp preview (hacks/visuals/esp_preview.cpp) ----
// ITexture*, null if Glow_LevelInit's preview target creation failed. Plain void*: chams_api.h stays
// engine-type-free, same as the rest of this header.
void Glow_PreviewInit();   // esp preview: make its targets if no Glow_LevelInit did yet (game thread)
void* Glow_PreviewTarget();
// Glow_Render for one item -- the preview entity -- on the preview target: call right after the
// preview's chams draw, still inside Push3DView with the preview RT bound (aim/visuals/chams.cpp has the
// ordering proof). worldToProjection is EspPreview's own camera (float[4][4], row-major); pBones is
// a matrix3x4 array of nBones entries off the drawn model; pHdr is its studiohdr_t*.
void Glow_RenderPreview(void* pEnt, void* pGun /*held weapon ent, may be null*/, const float (*worldToProjection)[4], const void* pBones, int nBones, const void* pHdr);

// ---- model glow (aim/visuals/chams.cpp, botox model_glow.cpp): viewmodel glow / stencil + dropped weapon glow ----
// screenfx.cpp: open after the post slot (DrawViewModels next, viewrender.cpp:2160), shut at ViewDrawFade
void ModelGlow_SetViewmodelWindow(bool open);
bool ModelGlow_ViewmodelOpen();            // window open + a viewmodel glow / stencil part wanted (cheap gate)
int  ModelGlow_ViewmodelRef(int vmKind);   // visuals.cpp ViewmodelDrawKind -> stencil ref the live part stamps, 0 = none
void ModelGlow_Stamp(int ref);             // h_DrawModelExecute around the live gun draw: ref, then 0 = stencil off
void ModelGlow_ViewmodelPass();            // screenfx.cpp ViewDrawFade, after the gun passes: queue the glow / outline
void ModelGlow_RenderDropped();            // screenfx.cpp post slot, right before Glow_Render: stamp + queue (read + clear)

// IVModelRender::ForcedMaterialOverride (slot 1), installed by thread.cpp. The engine's own
// overrides (invuln, burning, ...) are dropped while Glow_Rendering so the glow material stays.
using ForcedMaterialOverride_t = void(__fastcall*)(void*, void*, int);
extern ForcedMaterialOverride_t o_ForcedMaterialOverride;
void __fastcall h_ForcedMaterialOverride(void* rcx, void* pMat, int iType);
