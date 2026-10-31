#include "gif_tex.h"
#include "gif_frame.h"
#include <gdiplus.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstring>

// GDI+ decode, same approach as botox's spectator_list.cpp (kamibebra_gif_data.h):
// Bitmap::FromStream on an in-memory copy, SelectActiveFrame to walk the GIF's own frame
// dimension, PropertyTagFrameDelay (centiseconds, *10 for ms) for timing.
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")

struct GifTex
{
    IStream*           stream  = nullptr;
    Gdiplus::Bitmap*   bitmap  = nullptr;
    std::vector<uint32_t> delaysMs;
    size_t             uploaded = (size_t)-1;   // last frame index uploaded to texture
    IDirect3DTexture9* texture  = nullptr;
    bool               failed   = false;
};

static std::unordered_map<std::string, GifTex> s_cache;
static ULONG_PTR s_gdiplusToken = 0;

static bool EnsureGdiplus()
{
    if (s_gdiplusToken) return true;
    Gdiplus::GdiplusStartupInput input{};
    return Gdiplus::GdiplusStartup(&s_gdiplusToken, &input, nullptr) == Gdiplus::Ok;
}

// Decodes header + delay table only -- no pixels yet, those come lazily per-frame in
// GifTex_Frame. Runs once per id, result cached whether it succeeded or not (a GIF that
// will not decode must not re-run GDI+ every call).
static bool DecodeHeader(GifTex& g, const uint8_t* data, size_t size)
{
    g.failed = true;   // one-shot unless every step below clears it

    if (!EnsureGdiplus() || !data || !size) return false;

    const HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!mem) return false;
    void* locked = GlobalLock(mem);
    if (!locked) { GlobalFree(mem); return false; }
    memcpy(locked, data, size);
    GlobalUnlock(mem);

    if (FAILED(CreateStreamOnHGlobal(mem, TRUE, &g.stream))) { GlobalFree(mem); g.stream = nullptr; return false; }

    g.bitmap = Gdiplus::Bitmap::FromStream(g.stream);
    const UINT frames = (g.bitmap && g.bitmap->GetLastStatus() == Gdiplus::Ok)
                             ? std::max(g.bitmap->GetFrameCount(&Gdiplus::FrameDimensionTime), 1u) : 0;
    if (!frames)
    {
        delete g.bitmap; g.bitmap = nullptr;
        g.stream->Release(); g.stream = nullptr;
        return false;
    }

    g.delaysMs.assign(frames, 100);
    if (const UINT propSize = g.bitmap->GetPropertyItemSize(PropertyTagFrameDelay); propSize)
    {
        std::vector<unsigned char> prop(propSize);
        auto* item = reinterpret_cast<Gdiplus::PropertyItem*>(prop.data());
        if (g.bitmap->GetPropertyItem(PropertyTagFrameDelay, propSize, item) == Gdiplus::Ok && item->value)
        {
            const UINT count = std::min(frames, (UINT)(item->length / sizeof(UINT)));
            for (UINT i = 0; i < count; ++i)
                g.delaysMs[i] = static_cast<const UINT*>(item->value)[i] * 10;
        }
    }

    g.failed = false;
    return true;
}

GifTex* GifTex_Get(const char* id, const uint8_t* data, size_t size)
{
    if (!id) return nullptr;
    auto it = s_cache.find(id);
    if (it != s_cache.end()) return &it->second;

    GifTex& g = s_cache[id];
    if (!DecodeHeader(g, data, size))
        DebugLog("gif_tex: '%s' failed to decode", id);
    return &g;
}

ImTextureID GifTex_Frame(GifTex* gif, double timeSec)
{
    if (!gif || gif->failed || !gif->bitmap || !g_d3dDev) return (ImTextureID)0;

    const size_t frame = n_gif::FrameIndexAt(gif->delaysMs, timeSec * 1000.0);

    if (frame != gif->uploaded || !gif->texture)
    {
        if (frame != gif->uploaded)
            gif->bitmap->SelectActiveFrame(&Gdiplus::FrameDimensionTime, (UINT)frame);

        const UINT w = gif->bitmap->GetWidth(), h = gif->bitmap->GetHeight();
        if (!w || !h) return (ImTextureID)0;

        if (!gif->texture)
        {
            // DYNAMIC + DEFAULT only -- MANAGED does not exist on this engine's d3d9ex device.
            if (FAILED(g_d3dDev->CreateTexture(w, h, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
                                               D3DPOOL_DEFAULT, &gif->texture, nullptr)) || !gif->texture)
            {
                DebugLog("gif_tex: CreateTexture %ux%u failed", w, h);
                gif->texture = nullptr;
                return (ImTextureID)0;
            }
        }

        Gdiplus::Rect rect(0, 0, (INT)w, (INT)h);
        Gdiplus::BitmapData bits{};
        if (gif->bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bits) == Gdiplus::Ok)
        {
            D3DLOCKED_RECT lr{};
            if (SUCCEEDED(gif->texture->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD)))
            {
                for (UINT y = 0; y < h; ++y)
                    memcpy((uint8_t*)lr.pBits + (size_t)y * lr.Pitch,
                           (const uint8_t*)bits.Scan0 + (size_t)y * bits.Stride, (size_t)w * 4);
                gif->texture->UnlockRect(0);
                gif->uploaded = frame;
            }
            gif->bitmap->UnlockBits(&bits);
        }
    }

    return (ImTextureID)gif->texture;
}

void GifTex_ReleaseAll()
{
    for (auto& kv : s_cache)
    {
        GifTex& g = kv.second;
        if (g.texture) { g.texture->Release(); g.texture = nullptr; }
        g.uploaded = (size_t)-1;   // forces a fresh LockBits+upload next GifTex_Frame
    }
}
