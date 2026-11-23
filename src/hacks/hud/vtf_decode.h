#pragma once
// Top mip of a plain 2d VTF 7.x -> BGRA8 (D3DFMT_A8R8G8B8 byte order). Pure, no engine: tested by
// tests/vtfdecode_test.cpp. Header layout = sdk public/vtf/vtf.h VTFFileHeader_t (7.3+ resource
// dir, 7.0-7.2 low res thumb before the image); formats = sdk public/bitmap/imageformat.h ids.
// Used by class_icons.cpp for the scoreboard icons (stock: DXT5 / BGRA8888, huds may ship others).
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace vtf
{
    enum : int { RGBA8888 = 0, RGB888 = 2, BGR888 = 3, BGRA8888 = 12, DXT1 = 13, DXT3 = 14, DXT5 = 15, BGRX8888 = 16, DXT1A = 20 };

    inline size_t ImageBytes(int fmt, int w, int h)
    {
        const size_t bw = (size_t)std::max((w + 3) / 4, 1), bh = (size_t)std::max((h + 3) / 4, 1);
        switch (fmt)
        {
        case DXT1: case DXT1A:                   return bw * bh * 8;
        case DXT3: case DXT5:                    return bw * bh * 16;
        case RGBA8888: case BGRA8888: case BGRX8888: return (size_t)w * h * 4;
        case RGB888: case BGR888:                return (size_t)w * h * 3;
        default:                                 return 0;
        }
    }

    // one 4x4 colour block (8 bytes) -> 16 BGRA px; fourColour = DXT3/5 (always the 4 colour mode)
    inline void ColourBlock(const uint8_t* s, uint8_t out[16][4], bool fourColour)
    {
        const unsigned c0 = s[0] | (s[1] << 8), c1 = s[2] | (s[3] << 8);
        uint8_t pal[4][4];   // b g r a
        const auto expand = [](unsigned c, uint8_t* p) {
            const unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
            p[0] = (uint8_t)((b << 3) | (b >> 2)); p[1] = (uint8_t)((g << 2) | (g >> 4)); p[2] = (uint8_t)((r << 3) | (r >> 2)); p[3] = 255;
        };
        expand(c0, pal[0]); expand(c1, pal[1]);
        for (int k = 0; k < 3; ++k)
        {
            if (fourColour || c0 > c1)
            {
                pal[2][k] = (uint8_t)((2 * pal[0][k] + pal[1][k]) / 3);
                pal[3][k] = (uint8_t)((pal[0][k] + 2 * pal[1][k]) / 3);
            }
            else
            {
                pal[2][k] = (uint8_t)((pal[0][k] + pal[1][k]) / 2);
                pal[3][k] = 0;
            }
        }
        pal[2][3] = 255;
        pal[3][3] = (fourColour || c0 > c1) ? 255 : 0;   // 3 colour mode: index 3 = transparent black
        const uint32_t idx = s[4] | (s[5] << 8) | (s[6] << 16) | ((uint32_t)s[7] << 24);
        for (int i = 0; i < 16; ++i)
            memcpy(out[i], pal[(idx >> (i * 2)) & 3], 4);
    }

    // DXT5 alpha block (8 bytes) -> 16 alphas
    inline void Dxt5Alpha(const uint8_t* s, uint8_t a[16])
    {
        uint8_t pal[8] = { s[0], s[1] };
        if (s[0] > s[1])
            for (int i = 1; i < 7; ++i) pal[i + 1] = (uint8_t)(((7 - i) * s[0] + i * s[1]) / 7);
        else
        {
            for (int i = 1; i < 5; ++i) pal[i + 1] = (uint8_t)(((5 - i) * s[0] + i * s[1]) / 5);
            pal[6] = 0; pal[7] = 255;
        }
        uint64_t bits = 0;
        for (int i = 0; i < 6; ++i) bits |= (uint64_t)s[2 + i] << (8 * i);
        for (int i = 0; i < 16; ++i) a[i] = pal[(bits >> (3 * i)) & 7];
    }

    // decode one w x h image of format fmt at src (ImageBytes(fmt, w, h) readable) into bgra (w*h*4)
    inline bool DecodeImage(int fmt, const uint8_t* src, int w, int h, uint8_t* bgra)
    {
        const size_t n = (size_t)w * h;
        switch (fmt)
        {
        case BGRA8888: memcpy(bgra, src, n * 4); return true;
        case BGRX8888: for (size_t i = 0; i < n; ++i) { memcpy(bgra + i * 4, src + i * 4, 3); bgra[i * 4 + 3] = 255; } return true;
        case RGBA8888: for (size_t i = 0; i < n; ++i) { const uint8_t* p = src + i * 4; uint8_t* o = bgra + i * 4; o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; o[3] = p[3]; } return true;
        case BGR888:   for (size_t i = 0; i < n; ++i) { memcpy(bgra + i * 4, src + i * 3, 3); bgra[i * 4 + 3] = 255; } return true;
        case RGB888:   for (size_t i = 0; i < n; ++i) { const uint8_t* p = src + i * 3; uint8_t* o = bgra + i * 4; o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; o[3] = 255; } return true;
        case DXT1: case DXT1A: case DXT3: case DXT5: break;
        default: return false;
        }
        const size_t blockBytes = (fmt == DXT1 || fmt == DXT1A) ? 8 : 16;
        const int bw = std::max((w + 3) / 4, 1), bh = std::max((h + 3) / 4, 1);
        for (int by = 0; by < bh; ++by)
            for (int bx = 0; bx < bw; ++bx)
            {
                const uint8_t* s = src + ((size_t)by * bw + bx) * blockBytes;
                uint8_t px[16][4];
                ColourBlock(blockBytes == 8 ? s : s + 8, px, blockBytes == 16);
                if (fmt == DXT5)
                {
                    uint8_t a[16]; Dxt5Alpha(s, a);
                    for (int i = 0; i < 16; ++i) px[i][3] = a[i];
                }
                else if (fmt == DXT3)
                    for (int i = 0; i < 16; ++i) px[i][3] = (uint8_t)(((s[i / 2] >> ((i & 1) * 4)) & 15) * 17);
                for (int i = 0; i < 16; ++i)
                {
                    const int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                    if (x < w && y < h) memcpy(bgra + ((size_t)y * w + x) * 4, px[i], 4);
                }
            }
        return true;
    }

    // whole .vtf file -> frame 0 of the top mip as BGRA. false = not a plain 2d texture we can read
    // (cubemap / volume / unknown format / truncated). maxSide caps the allocation.
    inline bool DecodeTop(const uint8_t* f, size_t size, int& w, int& h, std::vector<uint8_t>& bgra, int maxSide = 2048)
    {
        const auto u16 = [&](size_t at) { return (unsigned)(f[at] | (f[at + 1] << 8)); };
        const auto u32 = [&](size_t at) { return (unsigned)(u16(at) | (u16(at + 2) << 16)); };
        if (size < 64 || memcmp(f, "VTF", 4) != 0 || u32(4) != 7) return false;
        const unsigned minor = u32(8), header = u32(12);
        w = (int)u16(16); h = (int)u16(18);
        const unsigned flags = u32(20);
        const int frames = std::max((int)u16(24), 1), fmt = (int)u32(52), mips = std::max((int)f[56], 1);
        if (w <= 0 || h <= 0 || w > maxSide || h > maxSide || mips > 16) return false;
        if (flags & 0x4000) return false;                                    // TEXTUREFLAGS_ENVMAP: 6 faces
        if (minor >= 2 && size >= 65 && u16(63) > 1) return false;           // volume texture
        if (!ImageBytes(fmt, w, h)) return false;

        size_t image = 0;
        if (minor >= 3)
        {
            const unsigned count = std::min(u32(68), 32u);
            for (unsigned i = 0; i < count && 88 + (size_t)i * 8 <= size; ++i)
                if ((u32(80 + i * 8) & 0x00ffffff) == 0x30) image = u32(84 + i * 8);   // VTF_LEGACY_RSRC_IMAGE
        }
        else
        {
            const int lowFmt = (int)u32(57);
            image = header + (lowFmt >= 0 && f[61] && f[62] ? ImageBytes(lowFmt, f[61], f[62]) : 0);   // -1 / 0x0 = no thumb
        }
        if (!image || image >= size) return false;

        // mips run smallest first, each = frames x the image: the top mip's frame 0 sits after the rest
        size_t at = image;
        for (int m = mips - 1; m >= 1; --m)
            at += ImageBytes(fmt, std::max(w >> m, 1), std::max(h >> m, 1)) * frames;
        const size_t top = ImageBytes(fmt, w, h);
        if (at > size || top > size - at) return false;
        bgra.resize((size_t)w * h * 4);
        return DecodeImage(fmt, f + at, w, h, bgra.data());
    }
}
