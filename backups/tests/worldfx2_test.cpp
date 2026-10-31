// Gate test for Unit D2's pure helpers (src/hacks/visuals/world_fx2.h): the texture-group
// classification (engine-assigned string, not a name guess), the weather-sprite name match,
// and the dlight colour byte clamp. No engine, no vtables.
// Build + run: cmake --build build --config Release --target worldfx2_test && build\Release\worldfx2_test.exe
#include "../src/hacks/visuals/world_fx2.h"
#include <cstdio>
#include <limits>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

int main()
{
    // texture group classification -- exact engine strings only, everything else is "neither"
    CHECK(WorldFx2_ClassifyTextureGroup("World textures") == 1);
    CHECK(WorldFx2_ClassifyTextureGroup("Model textures") == 2);
    CHECK(WorldFx2_ClassifyTextureGroup("VGUI textures") == 0);
    CHECK(WorldFx2_ClassifyTextureGroup("") == 0);
    CHECK(WorldFx2_ClassifyTextureGroup(nullptr) == 0);
    CHECK(WorldFx2_ClassifyTextureGroup("world textures") == 0);   // case-sensitive, not a guess

    // weather sprite name match
    CHECK(WorldFx2_IsWeatherSprite("particle/rain") == true);
    CHECK(WorldFx2_IsWeatherSprite("particle/snow") == true);
    CHECK(WorldFx2_IsWeatherSprite("particles/rain") == false);   // pre-redirect name, never seen here
    CHECK(WorldFx2_IsWeatherSprite("models/props/crate") == false);
    CHECK(WorldFx2_IsWeatherSprite(nullptr) == false);

    // colour byte clamp
    CHECK(WorldFx2_ColorByte(0.f) == 0);
    CHECK(WorldFx2_ColorByte(1.f) == 255);
    CHECK(WorldFx2_ColorByte(0.5f) == 127);
    CHECK(WorldFx2_ColorByte(-1.f) == 0);     // below range
    CHECK(WorldFx2_ColorByte(2.f) == 255);    // above range
    CHECK(WorldFx2_ColorByte(std::numeric_limits<float>::quiet_NaN()) == 0);   // NaN -> safe default

    std::printf("%s -- %d checks, %d failed\n", s_failed ? "FAILED" : "PASS", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
