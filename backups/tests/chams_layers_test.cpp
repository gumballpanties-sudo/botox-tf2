// Gate test for src/hacks/visuals/chams_layers.h (botox chams layer list: parse/format/toggle).
// Build + run: cmake --build build --config Release --target chams_layers_test && build\Release\chams_layers_test.exe
#include "../src/hacks/visuals/chams_layers.h"
#include <cmath>
#include <cstdio>
#include <cstring>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

int main()
{
    using namespace ChamsLayers;
    List l;

    // empty / null config -> no layers
    Parse("", l);      CHECK(l.used == 0 && l.slots[0] == -1);
    Parse(nullptr, l); CHECK(l.used == 0);

    // round trip
    Parse("4:FF000099,1:00FF00FF", l);
    CHECK(l.used == 2 && l.slots[0] == Glow && l.slots[1] == Flat);
    CHECK(std::fabs(l.colors[0][0] - 1.f) < 1e-6f && std::fabs(l.colors[0][3] - 0x99 / 255.f) < 1e-6f);
    char buf[kCfgLen];
    Format(l, buf, sizeof(buf));
    CHECK(std::strcmp(buf, "4:FF000099,1:00FF00FF") == 0);

    // garbage entries are skipped, good ones kept in order
    Parse("99:FFFFFFFF,x,3:12,2:0000FFFF", l);
    CHECK(l.used == 1 && l.slots[0] == Shaded);

    // toggle: append white, remove shifts down
    Clear(l);
    CHECK(Toggle(l, Wet) && Toggle(l, Original) && Toggle(l, Gold));
    CHECK(l.used == 3 && l.slots[0] == Wet && l.slots[2] == Gold && l.colors[2][3] == 1.f);
    l.colors[1][0] = 0.25f;
    CHECK(Toggle(l, Wet));
    CHECK(l.used == 2 && l.slots[0] == Original && l.slots[1] == Gold && l.colors[0][0] == 0.25f && l.slots[2] == -1);
    CHECK(IndexOf(l, Gold) == 1 && IndexOf(l, Wet) == -1);

    // full list refuses a 9th, max-length format fits the config buffer
    Clear(l);
    for (int m = 0; m < kMaxLayers; ++m) Toggle(l, Count - 1 - m);
    CHECK(l.used == kMaxLayers && !Toggle(l, Original));
    Format(l, buf, sizeof(buf));
    List r; Parse(buf, r);
    CHECK(r.used == kMaxLayers && r.slots[0] == Count - 1 && r.slots[7] == Count - 8);

    // sound chams alpha (botox sound_esp alpha)
    CHECK(SoundAlpha(0.2f, 1.f, true) == 1.f);
    CHECK(std::fabs(SoundAlpha(0.75f, 1.f, true) - 0.5f) < 1e-5f);
    CHECK(SoundAlpha(0.75f, 1.f, false) == 1.f);
    CHECK(SoundAlpha(1.5f, 1.f, true) == 0.f && SoundAlpha(-0.1f, 1.f, true) == 0.f && SoundAlpha(0.1f, 0.f, true) == 0.f);

    std::printf("%s -- %d checks, %d failed\n", s_failed ? "FAILED" : "PASS", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
