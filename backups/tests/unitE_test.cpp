// Gate test for unit E's pure logic (src/hacks/misc/unitE_logic.h): the ehandle decode the
// spectator list reads players' m_hObserverTarget with, the discord IPC JSON escaper, and
// the fade-fraction shared by hit marker/death particles/score popup.
// Build + run: cmake --build build --config Release --target unitE_test && build\Release\unitE_test.exe
#undef NDEBUG   // Release defines it and would strip every assert below
#include "../src/hacks/misc/unitE_logic.h"
#include <cassert>
#include <cstdio>

int main()
{
    using namespace n_unitE;

    // DecodeEHandleIndex: invalid sentinel, zero (no target), a real player slot, and a
    // handle whose serial bits are set above the 16-bit index mask.
    assert(DecodeEHandleIndex(0xFFFFFFFFu) == -1);
    assert(DecodeEHandleIndex(0u) == -1);                 // idx 0 is never a player
    assert(DecodeEHandleIndex(7u) == 7);
    assert(DecodeEHandleIndex((0x1234u << 16) | 12u) == 12);   // serial bits above the mask ignored
    assert(DecodeEHandleIndex(70000u) == -1);              // out of the 1..2048 player/edict range

    // JsonEscape: quotes and backslashes doubled, everything else passes through untouched --
    // a user-typed Discord state/details/image-key string must never break the IPC frame.
    assert(JsonEscape("plain") == "plain");
    assert(JsonEscape("say \"hi\"") == "say \\\"hi\\\"");
    assert(JsonEscape("back\\slash") == "back\\\\slash");
    assert(JsonEscape("") == "");
    assert(JsonEscape("a\nb\tc") == "abc");               // control bytes dropped (botox)

    // LevelNameShort: GetLevelName's "maps/x.bsp" -> botox's short map name
    assert(LevelNameShort("maps/ctf_2fort.bsp") == "ctf_2fort");
    assert(LevelNameShort("maps\\workshop\\koth_x.ugc1.bsp") == "koth_x.ugc1");
    assert(LevelNameShort("pl_upward") == "pl_upward");
    assert(LevelNameShort("") == "");
    assert(LevelNameShort(nullptr) == "");

    // FadeFrac: 1 at spawn, 0 at/after life, linear in between, clamped both ends.
    assert(FadeFrac(0.6f, 0.6f) == 1.f);
    assert(FadeFrac(0.3f, 0.6f) == 0.5f);
    assert(FadeFrac(0.f, 0.6f) == 0.f);
    assert(FadeFrac(-0.1f, 0.6f) == 0.f);
    assert(FadeFrac(1.0f, 0.6f) == 1.f);   // over life -- clamped, never >1
    assert(FadeFrac(0.3f, 0.f) == 0.f);    // zero life -- no divide-by-zero NaN/inf

    // McXpValue/McXpIcon: botox's ExperienceOrb split(1..40) -> icon tier bucketing
    // (kill_effects.cpp mc_xp_value/mc_xp_icon), used by the minecraft death-particle xp orbs.
    assert(McXpValue(1) == 1);
    assert(McXpValue(2) == 1);
    assert(McXpValue(3) == 3);
    assert(McXpValue(40) == 37);
    assert(McXpValue(3000) == 2477);
    assert(McXpIcon(1) == 0);
    assert(McXpIcon(3) == 1);
    assert(McXpIcon(2477) == 10);
    assert(McXpIcon(5000) == 10);   // above the table -- clamps to the top tier, never overflows
    // ScaleVirtual640x480: interwebz watermark's fixed (250,36) point in botox's 640x480
    // canvas, scaled to an arbitrary screen -- same relative spot, not the raw donor pixel.
    {
        float x, y;
        ScaleVirtual640x480(1920.f, 1080.f, 250.f, 36.f, x, y);
        assert(x > 749.9f && x < 750.1f);   // 250/640 * 1920
        assert(y > 80.9f && y < 81.1f);     // 36/480 * 1080
        ScaleVirtual640x480(640.f, 480.f, 250.f, 36.f, x, y);
        assert(x == 250.f && y == 36.f);    // identity at the donor's own resolution
    }

    printf("unitE_test: all assertions passed\n");
    return 0;
}
