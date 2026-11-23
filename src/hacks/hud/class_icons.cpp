#include "class_icons.h"
#include "vtf_decode.h"
#include <atomic>
#include <cstdio>
#include <vector>

bool Game_ReadFile(const char* path, std::vector<unsigned char>& out);   // visuals/reflections.cpp

namespace
{
    // index = TF_CLASS_* (scout 1 .. engineer 9); file names are the scoreboard's ("demo", not demoman)
    const char* const kFile[10] = { nullptr, "scout", "sniper", "soldier", "demo", "medic", "heavy", "pyro", "spy", "engineer" };

    struct Icon { int w = 0, h = 0; std::vector<uint8_t> bgra; };
    Icon s_icon[10];                      // game thread writes once, read-only after s_ready
    std::atomic<bool> s_ready{ false };
    bool s_tried = false;                 // game thread only

    IDirect3DTexture9* s_tex[10] = {};    // render thread only
    bool s_texTried[10] = {};             // one upload try per device
}

// ponytail: decoded once per session; a live hud swap (multihud) with other class icons keeps the
// old ones until reinject. Reset s_tried + s_ready from the hud swap if that ever matters.
bool ClassIcons_Have(int cls)
{
    if (!s_tried)
    {
        s_tried = true;
        int ok = 0;
        for (int c = 1; c <= 9; ++c)
        {
            char path[96];
            snprintf(path, sizeof(path), "materials/hud/leaderboard_class_%s.vtf", kFile[c]);
            std::vector<unsigned char> file;
            Icon& i = s_icon[c];
            if (Game_ReadFile(path, file) && vtf::DecodeTop(file.data(), file.size(), i.w, i.h, i.bgra, 512))
                ++ok;
            else
            {
                i = {};
                DebugLog("class icons: %s missing or unsupported format, class label instead", path);
            }
        }
        DebugLog("class icons: %d/9 decoded", ok);
        s_ready.store(true, std::memory_order_release);
    }
    return cls >= 1 && cls <= 9 && !s_icon[cls].bgra.empty();
}

ImTextureID ClassIcon_Get(int cls)
{
    if (cls < 1 || cls > 9 || !s_ready.load(std::memory_order_acquire))
        return (ImTextureID)0;
    if (!s_tex[cls] && !s_texTried[cls] && g_d3dDev)
    {
        s_texTried[cls] = true;
        const Icon& i = s_icon[cls];
        if (i.bgra.empty())
            return (ImTextureID)0;
        IDirect3DTexture9* tex = nullptr;
        // DYNAMIC + DEFAULT only -- MANAGED does not exist on this engine's d3d9ex device (avatar_cache.cpp)
        HRESULT hr = g_d3dDev->CreateTexture((UINT)i.w, (UINT)i.h, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex, nullptr);
        if (FAILED(hr) || !tex)
        {
            DebugLog("class icons: CreateTexture %dx%d failed hr=0x%08X", i.w, i.h, (unsigned)hr);
            return (ImTextureID)0;
        }
        D3DLOCKED_RECT lr{};
        if (FAILED(tex->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD)))
        {
            tex->Release();
            return (ImTextureID)0;
        }
        for (int y = 0; y < i.h; ++y)
            memcpy((uint8_t*)lr.pBits + (size_t)y * lr.Pitch, i.bgra.data() + (size_t)y * i.w * 4, (size_t)i.w * 4);
        tex->UnlockRect(0);
        s_tex[cls] = tex;
    }
    return (ImTextureID)s_tex[cls];
}

void ClassIcons_ReleaseAll()
{
    for (int c = 0; c < 10; ++c)
    {
        if (s_tex[c]) s_tex[c]->Release();
        s_tex[c] = nullptr;
        s_texTried[c] = false;
    }
}
