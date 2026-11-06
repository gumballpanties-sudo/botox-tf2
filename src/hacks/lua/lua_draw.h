#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

// lua render.* queue, esp_queue.h shape: the paint callback records on the game thread
// (h_PaintTraverse), render.cpp RenderLuaQueue replays it in BuildImGuiFrame (render thread,
// where the atlas lives). POD only: shared by the aim TU (lua_api.cpp) and render.cpp.

enum LuaDrawType : uint8_t
{
    LUAD_TEXT,            // x0,y0 top left, col, col2 shadow/outline, flags = text flags, font, size, text pool
    LUAD_LINE,            // x0,y0 -> x1,y1 col thick
    LUAD_RECT,            // min x0,y0 max x1,y1 col round thick, flags 1 = filled
    LUAD_GRAD,            // vertical gradient col top col2 bottom round
    LUAD_CIRCLE,          // centre x0,y0 radius x1 col segments(pn) thick
    LUAD_CIRCLE_FILLED,   // centre x0,y0 radius x1 col segments(pn)
    LUAD_POLY,            // convex fill col, outline col2 thick when flags 1, point pool
};

struct LuaDrawCmd
{
    uint8_t  type = 0, flags = 0;
    int      font = 0;
    float    x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    uint32_t col = 0, col2 = 0;
    float    thick = 1.f, round = 0.f, size = 0.f;
    int      p0 = 0, pn = 0;
};

struct LuaDrawFrame
{
    std::vector<LuaDrawCmd> cmds;
    std::vector<float>      pts;
    std::vector<char>       text;
    uint64_t                stampMs = 0;
    uint32_t                seq = 0;
    float                   srcW = 0, srcH = 0;
    void Clear() { cmds.clear(); pts.clear(); text.clear(); }
};

inline LuaDrawFrame g_luaDrawBuild;   // game thread only
inline constexpr int k_luaDrawRing = 4;
inline LuaDrawFrame g_luaDrawRing[k_luaDrawRing];   // under g_luaDrawLock
inline std::atomic<uint32_t> g_luaDrawSeq{ 0 };
inline std::atomic_flag g_luaDrawLock = ATOMIC_FLAG_INIT;

struct LuaDrawLockGuard
{
    LuaDrawLockGuard()  { while (g_luaDrawLock.test_and_set(std::memory_order_acquire)) {} }
    ~LuaDrawLockGuard() { g_luaDrawLock.clear(std::memory_order_release); }
};

namespace LuaDraw
{
    inline LuaDrawCmd& Push(uint8_t type)
    {
        g_luaDrawBuild.cmds.emplace_back();
        LuaDrawCmd& c = g_luaDrawBuild.cmds.back();
        c.type = type;
        return c;
    }

    inline void Text(const char* s, size_t n, LuaDrawCmd& c)
    {
        c.p0 = (int)g_luaDrawBuild.text.size();
        c.pn = (int)n;
        g_luaDrawBuild.text.insert(g_luaDrawBuild.text.end(), s, s + n);
    }

    inline void Points(const float* xy, int n, LuaDrawCmd& c)
    {
        c.p0 = (int)(g_luaDrawBuild.pts.size() / 2);
        c.pn = n;
        g_luaDrawBuild.pts.insert(g_luaDrawBuild.pts.end(), xy, xy + n * 2);
    }

    // game thread, once per paint (empty frames too: a script that stops drawing clears the screen)
    inline void Commit(float srcW, float srcH, uint64_t nowMs)
    {
        LuaDrawLockGuard lock;
        const uint32_t seq = g_luaDrawSeq.load(std::memory_order_relaxed) + 1;
        LuaDrawFrame& slot = g_luaDrawRing[seq % k_luaDrawRing];
        std::swap(slot.cmds, g_luaDrawBuild.cmds);
        std::swap(slot.pts, g_luaDrawBuild.pts);
        std::swap(slot.text, g_luaDrawBuild.text);
        slot.seq = seq;
        slot.stampMs = nowMs;
        slot.srcW = srcW;
        slot.srcH = srcH;
        g_luaDrawBuild.Clear();
        g_luaDrawSeq.store(seq, std::memory_order_release);
    }

    // render thread. seq 0 = newest
    inline void CopyFrame(uint32_t seq, LuaDrawFrame& out)
    {
        LuaDrawLockGuard lock;
        if (!seq) seq = g_luaDrawSeq.load(std::memory_order_relaxed);
        const LuaDrawFrame* src = &g_luaDrawRing[seq % k_luaDrawRing];
        if (src->seq != seq) src = &g_luaDrawRing[g_luaDrawSeq.load(std::memory_order_relaxed) % k_luaDrawRing];
        out.cmds = src->cmds;
        out.pts = src->pts;
        out.text = src->text;
        out.stampMs = src->stampMs;
        out.seq = src->seq;
        out.srcW = src->srcW;
        out.srcH = src->srcH;
    }
}

// fonts table ids, same numbers as botox csgo where the face exists
enum LuaFontId : int
{
    LUAF_VERDANA = 0, LUAF_VERDANA_BOLD = 1, LUAF_ICONS = 2, LUAF_INDICATOR = 3, LUAF_TAHOMA = 4, LUAF_TAHOMA_BOLD = 5,
    LUAF_CUSTOM_INDICATOR = 100, LUAF_CUSTOM_ESP = 101, LUAF_CUSTOM_NET_GRAPH = 102,
};

inline constexpr int k_luaFontSlots = 9;

inline int LuaFontSlot(int id)
{
    if (id >= 0 && id <= 5) return id;
    if (id >= 100 && id <= 102) return 6 + (id - 100);
    return 0;
}

// render.cpp publishes after every atlas build; the game thread measures text with it
// (render.text_size), it may not touch the atlas itself
struct LuaFontMetrics
{
    bool  ok = false;
    float size = 0.f;
    float fallback = 0.f;
    float adv[256] = {};
};

inline LuaFontMetrics g_luaFontMetrics[k_luaFontSlots];   // under g_luaDrawLock

inline void LuaMeasure(int id, const char* s, float size, float& w, float& h)
{
    LuaFontMetrics m;
    {
        LuaDrawLockGuard lock;
        m = g_luaFontMetrics[LuaFontSlot(id)];
    }
    w = h = 0.f;
    if (!m.ok || m.size <= 0.f) return;
    const float scale = size > 0.f ? size / m.size : 1.f;
    const float line = (size > 0.f ? size : m.size);
    float x = 0.f;
    h = line;
    for (const unsigned char* p = (const unsigned char*)s; *p;)
    {
        unsigned c = *p;
        int n = 1;
        if (c >= 0xF0) { n = 4; c = 0xFFFF; }
        else if (c >= 0xE0) { n = 3; c = 0xFFFF; }
        else if (c >= 0xC0) { n = 2; c = ((c & 0x1F) << 6) | (p[1] & 0x3F); }
        for (int i = 0; i < n && *p; i++) ++p;
        if (c == '\n') { if (x > w) w = x; x = 0.f; h += line; continue; }
        if (c == '\r') continue;
        x += (c < 256 ? m.adv[c] : m.fallback) * scale;
    }
    if (x > w) w = x;
}
