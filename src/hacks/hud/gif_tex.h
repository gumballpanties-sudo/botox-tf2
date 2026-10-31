#pragma once
#include "../../globals/globals.h"
#include <cstdint>
#include <cstddef>

// Unit E5: cache of decoded embedded GIFs (spectator_list/airflow icons), one reused
// IDirect3DTexture9 per GIF re-uploaded on frame change -- same approach spectator_list.cpp
// already uses, not a texture per frame (a 1.4MB donor GIF can run 50+ frames; keeping all
// of them resident would be hundreds of MB for an animation nobody looks at closely).
// ponytail: single reused texture, not "true" per-frame textures; bump to a small ring of
// pre-decoded textures only if a profile shows the re-upload cost actually matters.
struct GifTex;   // opaque, render-thread owned

// id is the cache key -- pass a stable string per GIF (e.g. "kamibebra_original"). data/size
// only matter the first time an id is seen.
GifTex* GifTex_Get(const char* id, const uint8_t* data, size_t size);

// RENDER THREAD: current frame for this GIF at timeSec of playback (decodes + uploads
// lazily on first call / frame change). Returns nullptr-equivalent (0) on any failure.
ImTextureID GifTex_Frame(GifTex* gif, double timeSec);

void GifTex_ReleaseAll();   // device Reset: the DEFAULT-pool frame texture is gone, GDI+ bitmap survives
