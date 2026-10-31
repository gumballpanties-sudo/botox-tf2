// Gate test for the CS:GO viewmodel port (src/hacks/visuals/vm_csgo.h). Expected numbers are worked
// by hand from csgosrc2019 weapon_csbase.cpp / predicted_viewmodel.cpp / baseplayer_shared.cpp.
// Build + run:  cmake --build build --config Release --target vmcsgo_test && build\Release\vmcsgo_test.exe
#include "../src/hacks/visuals/vm_csgo.h"
#include <cstdio>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)
static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

int main()
{
    VmCsBobCfg cfg;   // CS:GO defaults

    // ---- frametime 0: state untouched ----
    {
        VmBobState s{ 1.f, 2.f, 3.f, 4.f, 5.f };
        VmCs_CalcBob(s, 250.f, true, 300.f, 10.f, 0.f, cfg);
        CHECK(s.bobTime == 1.f && s.lastBobTime == 2.f && s.lastSpeed == 3.f && s.vert == 4.f && s.lat == 5.f);
    }

    // ---- phase 0 (bobTime lands on 0): sin = 0, only the 0.3 part + lowering ----
    // speed 250 steady (lastSpeed 250), curtime == lastBobTime -> bobTime stays 0
    // vert = 250*0.00625*0.25*0.3 - 21*0.2*250*0.006 = 0.1171875 - 6.3 = -6.1828125
    // lat  = 250*0.00625*0.4*0.3                      = 0.1875
    {
        VmBobState s{ 0.f, 5.f, 250.f, 0.f, 0.f };
        VmCs_CalcBob(s, 250.f, true, 300.f, 5.f, 0.01f, cfg);
        CHECK(Near(s.vert, -6.1828125f));
        CHECK(Near(s.lat, 0.1875f));
        CHECK(Near(s.lastSpeed, 250.f));
    }

    // ---- lower amt scales the dip with speed ----
    {
        VmCsBobCfg c = cfg; c.lowerAmt = 5.f;   // 5*0.2*250*0.006 = 1.5
        VmBobState s{ 0.f, 5.f, 250.f, 0.f, 0.f };
        VmCs_CalcBob(s, 250.f, true, 300.f, 5.f, 0.01f, c);
        CHECK(Near(s.vert, 0.1171875f - 1.5f));
    }

    // ---- lower clamps at -7 when fast ----
    {
        VmCsBobCfg c = cfg; c.lowerAmt = 30.f;   // 30*0.2*320*0.006 = 11.52 -> clamp
        VmBobState s{ 0.f, 5.f, 320.f, 0.f, 0.f };
        VmCs_CalcBob(s, 320.f, true, 300.f, 5.f, 0.01f, c);
        CHECK(Near(s.vert, -7.f));
    }

    // ---- air multiplier 0.00125 ----
    {
        VmBobState s{ 0.f, 5.f, 250.f, 0.f, 0.f };
        VmCsBobCfg c = cfg; c.lowerAmt = 0.f;
        VmCs_CalcBob(s, 250.f, false, 300.f, 5.f, 0.01f, c);
        CHECK(Near(s.vert, 250.f * 0.00125f * 0.25f * 0.3f));
        CHECK(Near(s.lat, 250.f * 0.00125f * 0.4f * 0.3f));
    }

    // ---- speed change limited to 640 u/s of curtime ----
    {
        VmBobState s{ 0.f, 0.f, 0.f, 0.f, 0.f };
        VmCs_CalcBob(s, 300.f, true, 300.f, 0.1f, 0.1f, cfg);   // max +64
        CHECK(Near(s.lastSpeed, 64.f));
        VmCs_CalcBob(s, 1000.f, true, 300.f, 1.f, 0.1f, cfg);   // +576 allowed, then 320 cap
        CHECK(Near(s.lastSpeed, 320.f));
    }

    // ---- bob time advances by dt * speed/320 ----
    {
        VmBobState s{ 0.f, 0.f, 160.f, 0.f, 0.f };
        VmCs_CalcBob(s, 160.f, true, 300.f, 0.5f, 0.01f, cfg);
        CHECK(Near(s.bobTime, 0.25f));
        CHECK(Near(s.lastBobTime, 0.5f));
    }

    // ---- quarter phase peak: cycle = bobup/2 -> sin(pi/2) = 1 ----
    // maxSpeed 300: flBobCycle = (700/3.5)*0.001*0.98 = 0.196. bobTime 0.049 -> frac 0.25 -> pi/2
    // vert = 250*0.00625*0.25 = 0.390625 (0.3 + 0.7) - 6.3
    {
        VmBobState s{ 0.049f, 5.f, 250.f, 0.f, 0.f };
        VmCs_CalcBob(s, 250.f, true, 300.f, 5.f, 0.01f, cfg);
        CHECK(Near(s.vert, 0.390625f - 6.3f, 1e-3f));
    }

    // ---- degenerate cvars never divide by zero ----
    {
        VmCsBobCfg c = cfg; c.cycle = 0.f; c.up = 0.f;
        VmBobState s{ 3.f, 5.f, 250.f, 0.f, 0.f };
        VmCs_CalcBob(s, 250.f, true, 2000.f, 5.1f, 0.01f, c);
        CHECK(std::isfinite(s.vert) && std::isfinite(s.lat));
    }

    // ---- gun lower: 0.1 per call toward 2 while airborne, back to 0 on ground ----
    {
        VmCsLower st;
        float p = 0.f, z = 0.f;
        VmCs_GunLower(st, true, 2.f, 0.1f, p, z);
        CHECK(Near(st.offset, 0.1f) && Near(p, 0.02f) && Near(z, 0.04f));
        for (int i = 0; i < 50; ++i) VmCs_GunLower(st, true, 2.f, 0.1f, p, z);
        CHECK(Near(st.offset, 2.f) && Near(p, 0.4f) && Near(z, 0.8f));
        for (int i = 0; i < 50; ++i) VmCs_GunLower(st, false, 2.f, 0.1f, p, z);
        CHECK(Near(st.offset, 0.f) && Near(p, 0.f));
    }

    // ---- landing dip: falls at 500, lands at t=10 ----
    // peak at 0.25s: 4 + (1/500)*0.1 ; still down at 0.3s ; over after 0.375s
    {
        VmCsDip st;
        CHECK(Near(VmCs_LandDip(st, 500.f, 9.9f, 4), 0.f));   // airborne: records old fall vel
        CHECK(Near(VmCs_LandDip(st, 0.f, 10.f, 4), 0.f));     // landing frame, fraction 0
        CHECK(st.inLanding);
        CHECK(Near(VmCs_LandDip(st, 0.f, 10.25f, 4), 4.f + 0.1f / 500.f, 1e-3f));
        CHECK(VmCs_LandDip(st, 0.f, 10.3f, 4) > 3.f);
        CHECK(Near(VmCs_LandDip(st, 0.f, 10.4f, 4), 0.f));
        CHECK(!st.inLanding);
        CHECK(Near(VmCs_LandDip(st, 0.f, 10.5f, 4), 0.f));    // grounded, oldFall now 0: no re-trigger
        CHECK(!st.inLanding);
    }

    // ---- landing dip time 0.1: bottom at 0.1s, over by 0.15s ----
    {
        VmCsDip st;
        VmCs_LandDip(st, 500.f, 9.9f, 4, 0.1f);
        VmCs_LandDip(st, 0.f, 10.f, 4, 0.1f);
        CHECK(Near(VmCs_LandDip(st, 0.f, 10.1f, 4, 0.1f), 4.f + 0.1f / 500.f, 1e-3f));
        CHECK(Near(VmCs_LandDip(st, 0.f, 10.16f, 4, 0.1f), 0.f));
        CHECK(!st.inLanding);
    }

    // ---- landing dip: tiny hop (fall <= 10) and fatal fall (> 1024) do nothing ----
    {
        VmCsDip a;
        VmCs_LandDip(a, 8.f, 1.f, 4);
        CHECK(Near(VmCs_LandDip(a, 0.f, 1.1f, 4), 0.f) && !a.inLanding);
        VmCsDip b;
        VmCs_LandDip(b, 1500.f, 1.f, 4);
        CHECK(Near(VmCs_LandDip(b, 0.f, 1.25f, 4), 0.f) && !b.inLanding);
    }

    // ---- off-hand double-calc dedupe (spy watch / dead ringer, vm index 1) ----
    {
        VmBobState vm0{}, vm1{};
        VmCsDedupe d;
        // frame 7: weapon calc on vm0, then vm1's two calls (weapon + CTFViewModel)
        CHECK(!VmCs_IsRepeatCall(d, &vm0, 7));
        CHECK(!VmCs_IsRepeatCall(d, &vm1, 7));
        CHECK(VmCs_IsRepeatCall(d, &vm1, 7));     // the duplicate -> bob zeroed, applied once
        CHECK(VmCs_IsRepeatCall(d, &vm1, 7));     // stays flagged inside the frame
        CHECK(!VmCs_IsRepeatCall(d, &vm1, 8));    // next frame: real calc again
        CHECK(!VmCs_IsRepeatCall(d, &vm0, 8));    // vm0 never flagged: one calc per frame
        CHECK(!VmCs_IsRepeatCall(d, &vm1, 8));
        CHECK(VmCs_IsRepeatCall(d, &vm1, 8));
    }

    // ---- watch ends up with exactly the main viewmodel's bob ----
    // Replays TF's frame: vm0 gets one calc+apply, vm1 gets two (weapon + CTFViewModel).
    // Sum what AddViewModelBobHelper would apply; without the dedupe vm1's sum is 2x.
    {
        VmBobState vm0{}, vm1{};
        VmCsDedupe d;
        float applied0 = 0.f, applied1 = 0.f, undeduped1 = 0.f;
        VmBobState raw1{};
        for (int f = 1; f <= 20; ++f)
        {
            const float t = f * 0.015f;
            auto calc = [&](VmBobState& s) { VmCs_CalcBob(s, 300.f, true, 300.f, t, 0.015f, cfg); };

            if (!VmCs_IsRepeatCall(d, &vm0, f)) calc(vm0);
            applied0 += vm0.vert;                              // vm0: weapon call only

            if (!VmCs_IsRepeatCall(d, &vm1, f)) calc(vm1);     // vm1 call 1 (weapon)
            applied1 += vm1.vert;
            if (VmCs_IsRepeatCall(d, &vm1, f)) vm1.vert = 0.f; else calc(vm1);
            applied1 += vm1.vert;                              // vm1 call 2 (CTFViewModel)

            calc(raw1); undeduped1 += raw1.vert;               // what it looked like before: 2x
            calc(raw1); undeduped1 += raw1.vert;
        }
        CHECK(applied0 != 0.f);
        CHECK(Near(applied1, applied0, 1e-5f));                // watch == main viewmodel
        CHECK(!Near(undeduped1, applied0, 1e-5f));             // the bug the dedupe removes
    }

    std::printf("%d/%d checks passed\n", s_checks - s_failed, s_checks);
    return s_failed ? 1 : 0;
}
