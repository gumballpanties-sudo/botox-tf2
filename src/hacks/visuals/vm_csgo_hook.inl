// CS:GO viewmodel glue: hook + eye adjust around vm_csgo.h. Included by visuals.cpp.
// SAME FILE in SimpleVisual and fpsoverlay (src/hacks/visuals/vm_csgo_hook.inl) -- edit both.
// Before including, define VMCS_REMOVE_BOB as the project's "remove bob" bool.
// Needs: g_eject, g_offVelocity/g_offFlags/g_offMaxspeed, PatternScan, DebugLog, MinHook,
// and the g_vmCs* globals.
#include "vm_csgo.h"

// ---- CalcViewModelBobHelper(CBasePlayer*, BobState_t*) -- tf_weaponbase.cpp ----
// Free function, called by CTFWeaponBase::AddViewmodelBob (vm 0) and CTFViewModel::AddViewModelBob
// (vm 1), right before AddViewModelBobHelper applies m_flVerticalBob / m_flLateralBob -- which is
// byte-for-byte CS:GO's helper, so replacing the calc is the whole CS:GO bob.
// client.dll x64 RVA 0x70d110 (2026-09-17), unique. Its first reads: player+0x178/0x17c
// (m_vecVelocity), state +0/+4/+8/+C/+10, gpGlobals +0xC curtime / +0x10 frametime.
static const char* const kVmCsBobSigs[] = {
    "40 53 48 81 EC ? ? ? ? 0F 29 B4 24 ? ? ? ? 48 8B DA 44 0F 29 4C 24 ? 45 0F 57 C9 44 0F 29 54 24 ? 48 85 D2 0F 84 ? ? ? ? 48 8B 05 ? ? ? ? F3 0F 10 05",
    nullptr
};

struct VmCsGlobals { float realtime; int framecount; float absoluteframetime; float curtime; float frametime; };

using VmCsBobHelper_t = float(__fastcall*)(void* player, void* state);
static VmCsBobHelper_t o_VmCsBobHelper = nullptr;
static VmCsGlobals**   s_vmCsGlobalsPP = nullptr;   // client.dll's gpGlobals variable

static VmCsGlobals* VmCs_Globals()
{
    __try { return s_vmCsGlobalsPP ? *s_vmCsGlobalsPP : nullptr; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static VmCsDedupe s_vmCsDedupe;   // off-hand vm gets the calc twice a frame -- see vm_csgo.h

static bool VmCs_BobSafe(void* player, VmBobState* s)
{
    __try {
        if (VMCS_REMOVE_BOB) { s->vert = 0.f; s->lat = 0.f; return true; }
        const VmCsGlobals* gl = VmCs_Globals();
        if (!gl || g_offVelocity <= 0 || g_offFlags <= 0) return false;
        // second calc on one BobState in one frame = the watch/dead ringer double bob
        if (VmCs_IsRepeatCall(s_vmCsDedupe, s, gl->framecount)) { s->vert = 0.f; s->lat = 0.f; return true; }
        const uintptr_t base = reinterpret_cast<uintptr_t>(player);
        const float* v = reinterpret_cast<const float*>(base + g_offVelocity);
        const int flags = *reinterpret_cast<const int*>(base + g_offFlags);
        const float maxSpeed = g_offMaxspeed > 0 ? *reinterpret_cast<const float*>(base + g_offMaxspeed) : 0.f;
        VmCsBobCfg c;
        c.cycle = g_vmCsBobCycle; c.up = g_vmCsBobUp;
        c.amtVert = g_vmCsBobVert; c.amtLat = g_vmCsBobLat; c.lowerAmt = g_vmCsBobLower;
        VmCs_CalcBob(*s, std::sqrt(v[0] * v[0] + v[1] * v[1]), (flags & 1) != 0,   // FL_ONGROUND
                     maxSpeed, gl->curtime, gl->frametime, c);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static float __fastcall h_VmCsBobHelper(void* player, void* state)
{
    if (!g_eject && player && state && (g_vmCsBob || VMCS_REMOVE_BOB)
        && VmCs_BobSafe(player, static_cast<VmBobState*>(state)))
        return 0.f;   // TF ignores the return ("NOTENOTE: We don't use this return value")
    return o_VmCsBobHelper(player, state);
}

// ---- gun lower on jump + landing dip: CS:GO moves the eye BEFORE the base CalcViewModelView ----
// CalcViewModelView runs once per viewmodel (2) per frame: state steps once per framecount.
static int       s_vmCsFrame = -1;
static float     s_vmCsPitch = 0.f, s_vmCsZ = 0.f;
static VmCsLower s_vmCsLower;
static VmCsDip   s_vmCsDip;

static void VmCs_EyeAdjustSafe(void* owner, float eye[3], float ang[3])
{
    __try {
        const VmCsGlobals* gl = VmCs_Globals();
        if (!gl || !owner || g_offVelocity <= 0 || g_offFlags <= 0) return;
        int& s_frame = s_vmCsFrame;
        float& s_pitch = s_vmCsPitch;
        float& s_z = s_vmCsZ;
        VmCsLower& s_lower = s_vmCsLower;
        VmCsDip& s_dip = s_vmCsDip;
        if (gl->framecount != s_frame)
        {
            s_frame = gl->framecount;
            const uintptr_t base = reinterpret_cast<uintptr_t>(owner);
            const float vz = reinterpret_cast<const float*>(base + g_offVelocity)[2];
            const bool onGround = (*reinterpret_cast<const int*>(base + g_offFlags) & 1) != 0;

            float pitchSub = 0.f, zSub = 0.f;
            if (g_vmCsGunLower) VmCs_GunLower(s_lower, !onGround, g_vmCsGunLowerAngle, g_vmCsGunLowerSpeed, pitchSub, zSub);
            else                s_lower.offset = 0.f;

            float dip = 0.f;
            if (g_vmCsLandDip) dip = VmCs_LandDip(s_dip, onGround ? 0.f : -vz, gl->curtime, (int)g_vmCsLandDipAmt, g_vmCsLandDipTime);
            else               s_dip = VmCsDip{};

            s_pitch = pitchSub;
            s_z = zSub + dip;
        }
        ang[0] -= s_pitch;
        eye[2] -= s_z;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void VmCs_Install(HMODULE client)
{
    if (!client || o_VmCsBobHelper) return;
    uint8_t* addr = nullptr;
    for (int i = 0; kVmCsBobSigs[i] && !addr; ++i) addr = PatternScan(client, kVmCsBobSigs[i]);
    if (!addr) { DebugLog("vmcs: CalcViewModelBobHelper pattern NOT FOUND"); return; }

    // gpGlobals: "mov rax, [rip+X]; ucomiss xmm9, [rax+10h]" (the frametime == 0 test)
    for (int i = 0; i < 0x100; ++i)
    {
        const uint8_t* p = addr + i;
        if (p[0] == 0x48 && p[1] == 0x8B && p[2] == 0x05 && p[7] == 0x44 && p[8] == 0x0F && p[9] == 0x2E
            && p[10] == 0x48 && p[11] == 0x10)
        {
            const int32_t disp = *reinterpret_cast<const int32_t*>(p + 3);
            s_vmCsGlobalsPP = reinterpret_cast<VmCsGlobals**>(const_cast<uint8_t*>(p) + 7 + disp);
            break;
        }
    }
    DebugLog("vmcs: bob helper %p gpGlobals var %p, offVelocity=%d (engine reads 0x178) offFlags=%d offMaxspeed=%d",
             addr, s_vmCsGlobalsPP, g_offVelocity, g_offFlags, g_offMaxspeed);
    if (!s_vmCsGlobalsPP) { DebugLog("vmcs: gpGlobals ref NOT FOUND, CS:GO bob off"); return; }

    if (MH_CreateHook(addr, (LPVOID)h_VmCsBobHelper, (LPVOID*)&o_VmCsBobHelper) != MH_OK)
    {
        o_VmCsBobHelper = nullptr;
        DebugLog("vmcs: MH_CreateHook failed");
        return;
    }
    MH_EnableHook(addr);
    DebugLog("vmcs: hooked");
}
