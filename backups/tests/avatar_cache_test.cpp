// Gate test for unit E5's avatar cache arithmetic (src/hacks/hud/avatar_pure.h): the
// friendsID -> steamid64 conversion GetPlayerInfo feeds Avatar_Get, and the RGBA->BGRA
// swizzle ISteamUtils::GetImageRGBA's bytes need before a D3DFMT_A8R8G8B8 upload. No Steam
// SDK or d3d9 pulled in -- this is the pure half of avatar_cache.cpp.
// Build + run: cmake --build build --config Release --target avatar_cache_test && build\Release\avatar_cache_test.exe
#include "../src/hacks/hud/avatar_pure.h"
#include <cstdio>
#include <vector>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

int main()
{
    using namespace n_avatar;

    // FriendIdToSteam64: the documented base (76561197960265728) plus the raw account id,
    // same expression scoreboard.cpp's AccountIDToSteam64 already uses and the task spec's
    // literal number -- pinned both ways so a typo in either constant fails loudly.
    CHECK(FriendIdToSteam64(0) == 76561197960265728ull);
    CHECK(FriendIdToSteam64(0) == 0x0110000100000000ull);
    CHECK(FriendIdToSteam64(1) == 76561197960265729ull);
    CHECK(FriendIdToSteam64(12345678u) == 76561197960265728ull + 12345678ull);
    CHECK(FriendIdToSteam64(0xFFFFFFFFu) == (0x0110000100000000ull | 0xFFFFFFFFull));

    // RgbaToBgraInPlace: R and B swap, G and A untouched -- one pixel.
    {
        uint8_t px[4] = { 10, 20, 30, 40 };   // R,G,B,A
        RgbaToBgraInPlace(px, 1);
        CHECK(px[0] == 30 && px[1] == 20 && px[2] == 10 && px[3] == 40);
    }

    // Several pixels: every pixel's own R/B swap, none bleed into a neighbour.
    {
        std::vector<uint8_t> px = {
            1,2,3,4,      // pixel 0
            5,6,7,8,      // pixel 1
            9,10,11,12,   // pixel 2
        };
        RgbaToBgraInPlace(px.data(), 3);
        CHECK(px[0]==3 && px[1]==2 && px[2]==1 && px[3]==4);
        CHECK(px[4]==7 && px[5]==6 && px[6]==5 && px[7]==8);
        CHECK(px[8]==11 && px[9]==10 && px[10]==9 && px[11]==12);
    }

    // Applying it twice is the identity -- the swap is its own inverse.
    {
        std::vector<uint8_t> px = { 200, 150, 50, 255, 1, 2, 3, 4 };
        std::vector<uint8_t> orig = px;
        RgbaToBgraInPlace(px.data(), 2);
        RgbaToBgraInPlace(px.data(), 2);
        CHECK(px == orig);
    }

    // Zero pixels: no-op, never an out-of-bounds write.
    {
        uint8_t px[1] = { 0 };
        RgbaToBgraInPlace(px, 0);
        CHECK(px[0] == 0);
    }

    std::printf("%s: %d checks, %d failed\n", s_failed ? "FAIL" : "PASS", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
