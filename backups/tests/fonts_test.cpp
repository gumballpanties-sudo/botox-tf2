// Gate test for the fonts tab's pure helpers (src/hacks/menu/fonts.h). No engine, no ImGui.
// Build + run: cmake --build build --config Release --target fonts_test && build\Release\fonts_test.exe
#include "../src/hacks/menu/fonts.h"
#include <cmath>
#include <cstdio>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

int main()
{
    // signature: every field and the reload counter move it; 0 is reserved for "never applied"
    const uint32_t base = FontSig("tahomabd", 12, 8, 0);
    CHECK(base != 0);
    CHECK(base == FontSig("tahomabd", 12, 8, 0));
    CHECK(base != FontSig("tahoma", 12, 8, 0));
    CHECK(base != FontSig("tahomabd", 13, 8, 0));
    CHECK(base != FontSig("tahomabd", 12, 9, 0));
    CHECK(base != FontSig("tahomabd", 12, 8, 1));
    CHECK(FontSig(nullptr, 0, 0, 0) != 0);

    // settle gate: nothing while the applied sig is current; a change fires only after it
    // has held still 250ms; a drag (sig changing every frame) never fires mid-drag
    {
        FontSettle s;
        CHECK(!s.Due(base, base, 1000));              // built == current
        const uint32_t a = FontSig("x", 20, 0, 0), b = FontSig("x", 21, 0, 0);
        CHECK(!s.Due(a, base, 2000));                 // just changed
        CHECK(!s.Due(a, base, 2249));
        CHECK(s.Due(a, base, 2250));                  // held 250ms -> rebuild
        CHECK(!s.Due(a, a, 2300));                    // applied -> quiet
        uint64_t t = 3000;
        for (int i = 0; i < 20; ++i, t += 16)         // dragging
            CHECK(!s.Due(FontSig("x", 22 + i, 0, 0), a, t));
        CHECK(s.Due(FontSig("x", 41, 0, 0), a, t + 250));
        CHECK(!s.Due(b, b, t + 600));                 // changed AND already applied -> nothing
    }

    // freetype flags -> vgui face: defaults reproduce the old hardcoded esp face
    {
        const VguiFace def = FontFlagsToVgui(kFontEspDefFlags);
        CHECK(def.weight == 700 && def.flags == 0x010);           // bold, antialias
        const VguiFace plain = FontFlagsToVgui(0);
        CHECK(plain.weight == 400 && plain.flags == 0x010);
        const VguiFace it = FontFlagsToVgui((1 << FF_OBLIQUE) | (1 << FF_MONOCHROME));
        CHECK(it.weight == 400 && it.flags == 0x001);             // italic, no antialias
        CHECK(FontFlagsToVgui(1 << FF_LIGHTHINTING).flags == 0x010);   // hinting bits ignored
    }

    // indicator px: default size reproduces the old floor(12 * scale + 0.5), size 0 = default
    for (float s : { 0.5f, 1.f, 1.25f, 1.7f, 2.5f })
        CHECK(FontIndPx(kFontIndDefSize, s) == std::floor(12.f * s + 0.5f));
    CHECK(FontIndPx(0, 1.f) == 12.f);
    CHECK(FontIndPx(12, 0.f) == 12.f);            // bad scale -> 1
    CHECK(FontIndPx(1, 1.f) == 6.f);              // floor
    CHECK(FontIndPx(50, 2.5f) == 100.f);          // ceiling
    CHECK(FontIndPx(29, 1.f) == 29.f);

    // flag bits == imgui freetype builder flags (imgui_freetype.h: NoHinting 1<<0 .. Monochrome 1<<7)
    CHECK((1 << FF_LIGHTHINTING) == 8 && (1 << FF_BOLD) == 32 && (1 << FF_MONOCHROME) == 128 && FF_COUNT == 8);

    std::printf("%s: %d/%d checks passed\n", s_failed ? "FAILED" : "ok", s_checks - s_failed, s_checks);
    return s_failed ? 1 : 0;
}
