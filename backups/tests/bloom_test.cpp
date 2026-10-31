// Gate test for bloom's numbers (src/hacks/screen/shaders/fx_bloom_bright.hlsl, fx_bloom_blur.hlsl and the
// fxc output beside them). No device, no game: what it can check offline is the arithmetic, and
// whether the compiled headers are still in step with the shaders they were built from.
//
// WHAT WOULD BREAK WITHOUT IT
//   * a mistyped gaussian weight -- the five have to sum to 1, or every blur pass makes the glow
//     brighter (or dimmer) than the last and the radius slider changes the exposure.
//   * an edited .hlsl and a forgotten fxc run -- the DLL would keep shipping the old bytecode,
//     silently, because nothing at build time compiles hlsl.
//   * the knee losing the colour of what glows, or leaking light from under the threshold.
//
// Build + run: cmake --build build --config Release --target bloom_test && build\Release\bloom_test.exe

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <string>
#include <vector>
#include <fstream>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

static bool Near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

static std::string Slurp(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::printf("FAIL: cannot open %s\n", path); ++s_failed; return {}; }
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// Every float literal in the text, in order. Integers written without a dot are skipped: the
// interesting numbers here are all fractions, and "float4" or "ps_2_0" are not constants.
static std::vector<double> Floats(const std::string& s)
{
    std::vector<double> out;
    for (size_t i = 0; i < s.size();) {
        if (!std::isdigit((unsigned char)s[i])) { ++i; continue; }
        // not part of an identifier (ps_2_0, float4, c0, r1)
        if (i > 0 && (std::isalpha((unsigned char)s[i-1]) || s[i-1] == '_')) {
            while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_')) ++i;
            continue;
        }
        size_t j = i;
        while (j < s.size() && std::isdigit((unsigned char)s[j])) ++j;
        bool dot = (j < s.size() && s[j] == '.');
        if (dot) { ++j; while (j < s.size() && std::isdigit((unsigned char)s[j])) ++j; }
        if (j < s.size() && (s[j] == 'e' || s[j] == 'E')) {   // 0.0001 is written 1e-4 in one place
            size_t k = j + 1;
            if (k < s.size() && (s[k] == '+' || s[k] == '-')) ++k;
            if (k < s.size() && std::isdigit((unsigned char)s[k])) { dot = true; j = k; while (j < s.size() && std::isdigit((unsigned char)s[j])) ++j; }
        }
        if (dot) out.push_back(std::atof(s.substr(i, j - i).c_str()));
        i = j;
    }
    return out;
}

static bool Has(const std::vector<double>& v, double x, double eps = 1e-5)
{
    for (double d : v) if (std::fabs(d - x) <= eps) return true;
    return false;
}

// The bright pass's knee, exactly as fx_bloom_bright.hlsl writes it.
static void Knee(const double c[3], double thr, double out[3])
{
    const double lum = c[0]*0.2126 + c[1]*0.7152 + c[2]*0.0722;
    const double k   = (lum - thr > 0.0 ? lum - thr : 0.0) / (lum > 1e-4 ? lum : 1e-4);
    for (int i = 0; i < 3; ++i) out[i] = c[i] * k;
}

// The pass count bloom.cpp works out of the radius slider.
static int Passes(double radius)
{
    const double r = radius < 0.0 ? 0.0 : (radius > 1.0 ? 1.0 : radius);
    return 1 + (int)(r * 5.0 + 0.5);
}

int main()
{
    const std::string dir = BLOOM_SRC_DIR;

    // ---- the gaussian ---------------------------------------------------------------
    {
        const double w[5] = { 0.2270270, 0.3162162, 0.3162162, 0.0702702, 0.0702702 };
        double sum = 0.0;
        for (double x : w) sum += x;
        // Off by a thousandth and six passes cost half a stop of exposure.
        CHECK(Near(sum, 1.0, 1e-6));

        const std::string blur = Slurp((dir + "/fx_bloom_blur.hlsl").c_str());
        const std::vector<double> f = Floats(blur);
        CHECK(Has(f, 0.2270270));
        CHECK(Has(f, 0.3162162));
        CHECK(Has(f, 0.0702702));
        // The two offsets are what buy nine taps' reach out of five fetches: they have to sit
        // between texels, so neither may be a whole number.
        CHECK(Has(f, 1.3846154));
        CHECK(Has(f, 3.2307692));
        CHECK(!Near(1.3846154, std::floor(1.3846154), 1e-3));
        CHECK(!Near(3.2307692, std::floor(3.2307692), 1e-3));
    }

    // ---- luma is a real luma in both shaders ----------------------------------------
    {
        CHECK(Near(0.2126 + 0.7152 + 0.0722, 1.0, 1e-9));
        for (const char* name : { "/fx_bloom_bright.hlsl", "/fx_bloom_blur.hlsl" }) {
            const std::vector<double> f = Floats(Slurp((dir + name).c_str()));
            CHECK(Has(f, 0.2126));
            CHECK(Has(f, 0.7152));
            CHECK(Has(f, 0.0722));
        }
    }

    // ---- the compiled headers are in step with the shaders --------------------------
    // fxc writes the disassembly into the #if 0 block at the top of the .h, constants and all,
    // so every number the shader was built with is readable there. A shader edited without a
    // rebuild fails right here instead of shipping stale bytecode nobody can see.
    {
        struct Pair { const char* hlsl; const char* header; } pairs[] = {
            { "/fx_bloom_bright.hlsl", "/fx_bloom_bright.h" },
            { "/fx_bloom_blur.hlsl",   "/fx_bloom_blur.h"   },
        };
        for (const Pair& p : pairs) {
            const std::string src = Slurp((dir + p.hlsl).c_str());
            const std::string hdr = Slurp((dir + p.header).c_str());
            const size_t dis = hdr.find("#endif");
            CHECK(dis != std::string::npos);                    // the disassembly block is there
            const std::vector<double> want = Floats(src.substr(src.find("float4 main")));
            const std::vector<double> got  = Floats(hdr.substr(0, dis == std::string::npos ? hdr.size() : dis));
            for (double x : want) {
                // 0 and 1 are skipped: fxc writes those into its constant blocks without a
                // decimal point ("def c4, 1, 0, 0, 0"), and a bare 1 is not a number this test
                // can tell from a register index.
                if (Near(x, 0.0, 1e-9) || Near(x, 1.0, 1e-9)) continue;
                // fxc prints its constants at ~9 significant digits, so the compare is loose in
                // the last place and nowhere else.
                if (!Has(got, x, 1e-5)) {
                    std::printf("FAIL: %s has %.7f, %s does not -- rerun fxc\n", p.hlsl, x, p.header);
                    ++s_failed;
                }
                ++s_checks;
            }
            CHECK(hdr.find("ps_2_0") != std::string::npos);     // ps_3_0 will not pair with the
                                                                // fixed-function quad bloom.cpp draws
        }
    }

    // ---- the knee ---------------------------------------------------------------------
    {
        double out[3];
        // under the threshold: nothing at all, or the whole frame hazes over
        const double dim[3] = { 0.30, 0.30, 0.30 };
        Knee(dim, 0.7, out);
        CHECK(Near(out[0], 0.0) && Near(out[1], 0.0) && Near(out[2], 0.0));

        // exactly at it: still nothing, and no divide blowing up
        const double at[3] = { 0.7, 0.7, 0.7 };
        Knee(at, 0.7, out);
        CHECK(Near(out[0], 0.0));

        // over it: the colour survives. A red lamp has to glow red, not white, which is the
        // whole reason the knee scales by luma instead of subtracting per channel.
        const double red[3] = { 1.0, 0.2, 0.2 };
        Knee(red, 0.3, out);
        CHECK(out[0] > 0.0);
        CHECK(Near(out[1] / out[0], red[1] / red[0], 1e-9));
        CHECK(Near(out[2] / out[0], red[2] / red[0], 1e-9));

        // and it never amplifies: k is a fraction of the pixel, so the glow starts no brighter
        // than what lit it (intensity is applied later, on purpose)
        const double white[3] = { 1.0, 1.0, 1.0 };
        Knee(white, 0.0, out);
        CHECK(out[0] <= 1.0 + 1e-9);
        Knee(white, 0.5, out);
        CHECK(out[0] < 0.51 && out[0] > 0.49);                 // half the light is over the line

        // pure black must not divide by zero
        const double black[3] = { 0.0, 0.0, 0.0 };
        Knee(black, 0.0, out);
        CHECK(std::isfinite(out[0]) && Near(out[0], 0.0));
    }

    // ---- the radius slider ------------------------------------------------------------
    {
        CHECK(Passes(0.0) == 1);
        CHECK(Passes(1.0) == 6);
        CHECK(Passes(-5.0) == 1);                              // a config file with junk in it
        CHECK(Passes(99.0) == 6);
        int prev = 0;
        for (int i = 0; i <= 100; ++i) {
            const int n = Passes(i / 100.0);
            CHECK(n >= 1 && n <= 6);                           // the loop is unbounded otherwise
            CHECK(n >= prev);                                  // more slider, never less blur
            prev = n;
        }
    }

    std::printf("%s -- %d checks, %d failed\n", s_failed ? "FAILED" : "PASSED", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
