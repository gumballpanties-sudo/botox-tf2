#include "equipregion.h"
#include "../../globals/globals.h"
#include <intrin.h>   // _ReturnAddress -- hook 3 only fires for one call site

// ==== equip region unlock ============================================================
// See equipregion.h for what the three hooks are. All three are loadout-UI paths (a few
// calls per menu interaction, none per frame), so once created they stay armed and the
// g_equipRegionUnlock flag gates the behaviour inside.
//
// Sigs are fpsoverlay's, each re-scanned against tf/bin/x64/client.dll:
//   VerifyChangedLoadoutsAreValid    rva 0x3BCAA0   1 hit
//   GenerateEquipRegionConflictMask  rva 0x408BB0   1 hit
//   GetItemInLoadoutForClass         rva 0x3BB040   1 hit
//   the UpdateModelPanelsForSelection call site      rva 0x40ADB1   1 hit

using VerifyChangedLoadoutsAreValid_t   = void(__fastcall*)(void*);
using GenerateEquipRegionConflictMask_t = uint32_t(__fastcall*)(int, int, int);
using GetItemInLoadoutForClass_t        = void*(__fastcall*)(void*, int, int, void*);

static VerifyChangedLoadoutsAreValid_t   o_VerifyChangedLoadoutsAreValid   = nullptr;
static GenerateEquipRegionConflictMask_t o_GenerateEquipRegionConflictMask = nullptr;
static GetItemInLoadoutForClass_t        o_GetItemInLoadoutForClass        = nullptr;

// Return address inside CEquipSlotItemSelectionPanel::UpdateModelPanelsForSelection.
// Hook 3 must stay transparent everywhere else -- GetItemInLoadoutForClass is how the
// game reads its own loadout, and blanket-nulling it empties the whole thing.
static uintptr_t s_addrGetItemInLoadoutCall = 0;

static bool s_installed = false;

// Hook 1: skip inventory validation -- lets a conflicting loadout stand.
// MiscHookGuard on all three: eject drains that counter, and every OTHER hook in the dll
// carries one. These were the three that did not, so an eject with the loadout screen open
// could tear state down under a thread still inside them.
static void __fastcall h_VerifyChangedLoadoutsAreValid(void* rcx)
{
    MiscHookGuard _g;
    if (!g_equipRegionUnlock || g_eject)
        o_VerifyChangedLoadoutsAreValid(rcx);
}

// Hook 2: zero the conflict mask -- every slot reads as conflict-free, so the UI stops
// greying items out with #Econ_GreyOutReason_EquipRegionConflict.
static uint32_t __fastcall h_GenerateEquipRegionConflictMask(int iClass, int iUpToSlot, int iIgnoreSlot)
{
    MiscHookGuard _g;
    if (g_equipRegionUnlock && !g_eject)
        return 0;
    return o_GenerateEquipRegionConflictMask(iClass, iUpToSlot, iIgnoreSlot);
}

// Hook 3: only for the equip-slot panel's own query -- an already-equipped item there
// blocks picking a second one for the same region.
static void* __fastcall h_GetItemInLoadoutForClass(void* rcx, int iClass, int iSlot, void* pID)
{
    MiscHookGuard _g;
    if (g_equipRegionUnlock && !g_eject && s_addrGetItemInLoadoutCall &&
        reinterpret_cast<uintptr_t>(_ReturnAddress()) == s_addrGetItemInLoadoutCall)
        return nullptr;
    return o_GetItemInLoadoutForClass(rcx, iClass, iSlot, pID);
}

void EquipRegion_Install()
{
    if (s_installed) return;

    HMODULE client = GetModuleHandleA("client.dll");
    if (!client) { DebugLog("equipregion: client.dll not loaded"); return; }

    struct HookDef { const char* name; const char* sig; void* hook; void** orig; };
    const HookDef hooks[] = {
        { "equipregion: VerifyChangedLoadoutsAreValid",
          "41 56 48 83 EC ? 48 8B 05 ? ? ? ? 48 8D 54 24",
          (void*)h_VerifyChangedLoadoutsAreValid,
          (void**)&o_VerifyChangedLoadoutsAreValid },
        { "equipregion: GenerateEquipRegionConflictMask",
          "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC ? 33 FF 41 8B E8",
          (void*)h_GenerateEquipRegionConflictMask,
          (void**)&o_GenerateEquipRegionConflictMask },
        { "equipregion: GetItemInLoadoutForClass",
          "48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 57 48 83 EC ? 81 60",
          (void*)h_GetItemInLoadoutForClass,
          (void**)&o_GetItemInLoadoutForClass },
    };

    // Hook 3 is inert without its call-site sentinel, so resolve that first.
    {
        uint8_t* call = PatternScan(client,
            "48 85 C0 74 ? 48 8D 48 ? 48 8B 40 ? FF 50 ? 44 0B A0");
        if (call) s_addrGetItemInLoadoutCall = reinterpret_cast<uintptr_t>(call);
        else      DebugLog("equipregion: GetItemInLoadout call site NOT FOUND");
    }

    bool allOk = true;
    for (const auto& h : hooks)
        if (!InstallPatternHook(client, h.name, h.sig, h.hook, h.orig)) allOk = false;

    // Latch after ONE attempt whatever the result. client.dll does not change mid-session, so a
    // miss now is a miss forever -- the old "retry until all land" ran 4 full ~60MB scans of
    // client.dll on the Present thread every 2 s for the rest of the session (stale sigs after a
    // TF2 update = permanent stutter), and a partial hit re-created hooks that already existed.
    s_installed = true;
    if (!allOk || !s_addrGetItemInLoadoutCall)
        DebugLog("equipregion: incomplete install, sigs stale -- not retrying this session");
}

// Install lazily: nobody who leaves this off pays a scan of client.dll. The 2 s retry only
// covers client.dll not being loaded yet; Install latches after its first real attempt.
void EquipRegion_Tick()
{
    if (s_installed || !g_equipRegionUnlock || g_eject) return;

    static DWORD s_nextRetry = 0;
    DWORD now = GetTickCount();
    if (now < s_nextRetry) return;
    s_nextRetry = now + 2000;

    EquipRegion_Install();
}
