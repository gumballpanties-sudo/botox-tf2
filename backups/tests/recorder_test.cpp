// Gate test for the movement recorder's serialization (src/hacks/movement/recorder_ser.h): a clip
// round-trips byte for byte through Serialize -> Deserialize, and a corrupt/truncated buffer
// is refused rather than returning garbage frames.
// Build + run: cmake --build build --config Release --target recorder_test && build\Release\recorder_test.exe
#include "../src/hacks/movement/recorder_ser.h"
#include <cstdio>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

int main()
{
    // 1. empty clip: Serialize/Deserialize must handle it cleanly (empty list, not corrupt)
    {
        std::vector<RecFrame> frames;
        std::vector<uint8_t> bytes;
        CHECK(RecSerialize(frames, bytes));
        std::vector<RecFrame> out;
        CHECK(RecDeserialize(bytes, out));
        CHECK(out.empty());
    }

    // 2. round trip a handful of frames with distinct, non-zero fields in every slot
    {
        std::vector<RecFrame> frames;
        for (int i = 0; i < 50; ++i)
        {
            RecFrame f;
            f.pitch = (float)i * 0.5f - 10.f;
            f.yaw   = (float)i * 3.3f;
            f.forward = (float)(i % 7) * 50.f;
            f.side    = (float)(i % 5) * -30.f;
            f.up      = (i % 3) ? 0.f : 1.f;
            f.buttons = i * 17;
            f.mouseDx = (short)(i - 25);
            f.mouseDy = (short)(25 - i);
            f.weaponSlot = (i % 4) - 1;
            f.posX = (float)i * 11.1f; f.posY = -(float)i * 2.2f; f.posZ = (float)i;
            f.velX = (float)i * 3.f;   f.velY = (float)i * -4.f;  f.velZ = 0.f;
            frames.push_back(f);
        }

        std::vector<uint8_t> bytes;
        CHECK(RecSerialize(frames, bytes));
        CHECK(!bytes.empty());

        std::vector<RecFrame> out;
        CHECK(RecDeserialize(bytes, out));
        CHECK(out.size() == frames.size());
        if (out.size() == frames.size())
        {
            for (size_t i = 0; i < frames.size(); ++i)
            {
                const RecFrame& a = frames[i];
                const RecFrame& b = out[i];
                CHECK(a.pitch == b.pitch && a.yaw == b.yaw);
                CHECK(a.forward == b.forward && a.side == b.side && a.up == b.up);
                CHECK(a.buttons == b.buttons);
                CHECK(a.mouseDx == b.mouseDx && a.mouseDy == b.mouseDy);
                CHECK(a.weaponSlot == b.weaponSlot);
                CHECK(a.posX == b.posX && a.posY == b.posY && a.posZ == b.posZ);
                CHECK(a.velX == b.velX && a.velY == b.velY && a.velZ == b.velZ);
            }
        }
    }

    // 3. corrupt magic is refused
    {
        std::vector<RecFrame> frames(3);
        std::vector<uint8_t> bytes;
        CHECK(RecSerialize(frames, bytes));
        bytes[0] ^= 0xFF;
        std::vector<RecFrame> out;
        CHECK(!RecDeserialize(bytes, out));
        CHECK(out.empty());
    }

    // 4. truncated buffer (declared count exceeds what is actually there) is refused, not
    //    read out of bounds
    {
        std::vector<RecFrame> frames(5);
        std::vector<uint8_t> bytes;
        CHECK(RecSerialize(frames, bytes));
        bytes.resize(bytes.size() - 10);   // chop off the tail of the last frame
        std::vector<RecFrame> out;
        CHECK(!RecDeserialize(bytes, out));
    }

    std::printf("recorder_test: %d/%d checks passed\n", s_checks - s_failed, s_checks);
    return s_failed ? 1 : 0;
}
