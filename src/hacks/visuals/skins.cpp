#include "../../globals/common.h"
#include "../../globals/globals.h"
#include "skins.h"
#include "skins_pure.h"
#include "hats.h"
#include "../lua/lua.h"   // lua tick / frame_stage / eject shutdown (h_FrameStageNotify)
#include "../web/websurface.h"   // Web_MainTick (FRAME_START)
#include "skinshare.h"   // peers' loadouts: applied to THEIR weapons below, same passes as ours
#include "../../hooks/crashlog.h"   // CrashSpan: stutter probe per FSN module
#include "visuals.h"
#include "flipworld.h"
#include "../hud/demostreak.h"
#include "../movement/recorder.h"   // Recorder_FrameStart
#include "../misc/game_extras.h"     // GameExtras_Frame -- game-thread tick
#include "../misc/killeffects.h"     // KillEffects_FrameStart -- hl2 dissolve
#include "../../dependencies/data/skindata.h"
#include <cstdlib>

// ==== state =====================================================================================
bool g_enableSkins     = false;
bool g_skinsBackpack   = true;    // backpack + loadout copy of each enabled weapon (hats_inv.cpp)
bool g_skinNeedsUpdate = false;
std::unordered_map<int, SkinCfg> g_skinCfgs;
// True once Skins_Install has pre-created every key and the FSN hook can read the map from the
// engine thread. No key may be ADDED after that -- an insert can rehash, and a rehash frees the
// bucket array under a concurrent find(). Values may still be mutated in place.
static bool s_cfgsSealed = false;

// Netvar offsets. 0 = not looked up yet, -1 = looked up and not found; both mean "do not
// touch this field". Resolved lazily off the engine thread inside the hook, not from
// netvars.cpp, so the feature is live the first frame the player is in a server.
static int s_offMyWeapons     = 0;   // DT_BaseCombatCharacter m_hMyWeapons
static int s_offActiveWeapon  = 0;   // DT_BaseCombatCharacter m_hActiveWeapon
static int s_offItemDefIndex  = 0;   // DT_EconEntity          m_iItemDefinitionIndex
static int s_offAttrList      = 0;   // DT_EconEntity          m_AttributeList
static int s_offViewModelIdx  = 0;   // DT_BaseCombatWeapon    m_iViewModelIndex (arms model)
static int s_offCustomVMModel = 0;   // DT_BaseCombatWeapon    m_nCustomViewmodelModelIndex
static int s_offHViewModel0   = 0;   // DT_BasePlayer          m_hViewModel[0]
static int s_offSkin          = 0;   // DT_BaseAnimating       m_nSkin
static int s_offSequence      = 0;   // DT_BaseAnimating       m_nSequence
static int s_offTeamNum       = 0;   // DT_BaseEntity          m_iTeamNum
static int s_offWeaponState   = 0;   // DT_BaseCombatWeapon    m_iState (2 = WEAPON_IS_ACTIVE)
// There was a fourth model field here, sig-derived out of DrawOverriddenViewmodel's first
// instruction. It resolved to 3664 and climbed by ~2 a second in the dih-sk3 log, so it was a
// frame counter and the sig was wrong. Deleted with its diag column: TF2 builds the attachment
// off the def index we pre-write and no model index is written by hand anywhere.
static int s_offGoldRagdoll   = 0;   // DT_TFRagdoll           m_bGoldRagdoll
static int s_offRagdollPlayer = 0;   // DT_TFRagdoll           m_hPlayer
static int s_offNStreaks      = 0;   // DT_TFPlayer m_Shared   m_nStreaks[kTFStreak_COUNT]
static int s_offIStreaks      = 0;   // DT_TFPlayerResource    m_iStreaks[MAX_PLAYERS*COUNT]

// Hook originals, declared here so the diagnostics below can report which ones went in.
// x64 TF2's engine-side FrameStageNotify takes the stage in rcx and has no this pointer.
using FrameStageNotify_t = void(__fastcall*)(int);
static FrameStageNotify_t   o_FrameStageNotify   = nullptr;
static SetSequence_t        o_SetSequence        = nullptr;
static TF_IsHolidayActive_t o_TF_IsHolidayActive = nullptr;
static uintptr_t s_preWriteCallSite = 0;   // resolved = FSN(2) really is CL_ProcessPacketEntities
static bool      s_setSeqViaAim     = false;   // aim tree's SetSequence hook calls Skins_RemapSequence
static DWORD     s_fullUpdateTick   = 0;       // last cl_fullupdate we sent -- see Skins_OnGameEvent

static void*    s_pLocalViewmodelEnt = nullptr;
static void*    s_lastLocalEnt       = nullptr;
static uint32_t s_entityChangeAge    = 0;
static bool     s_skinWasEnabled     = false;

// ==== killstreak state ==========================================================================
// The killstreak ATTRIBUTES are not enough on their own. The sheen proxy
// (CProxyAnimatedWeaponSheen::GetTimeBetweenAnims) spaces the sheen by the player's kill streak
// -- 5 s apart at streak 0, continuous at 5 -- and the eye glow is refused outright below
// tf_killstreakeyes_minkills, which is 5. That streak is a NETWORKED COUNT the server keeps and
// a server that never issued the killstreak item never counts one, so we keep our own and write
// it into both places the client reads:
//   CTFPlayerShared::m_nStreaks[type]                       on the local player
//   CTFPlayerResource::m_iStreaks[playerIdx * COUNT + type] on the resource entity
// Both are arrays of int, so the byte stride is sizeof(int) -- Unibox's SetStreak indexes them
// without it, which is why its index math is not copied here.
enum ETFStreak { kTFStreak_Kills = 0, kTFStreak_KillsAll, kTFStreak_Ducks,
                 kTFStreak_Duck_levelup, kTFStreak_COUNT };

static int  s_killStreak     = 0;   // our own count, reset on our own death and on spawn
static int  s_stampedStreak  = 0;   // last value ApplyKillStreak wrote (kick included)
static bool s_anyKillstreak  = false;   // recomputed per snapshot in RebuildReverseMap
static ds::Demo s_demo;                 // streak carry-over across demos -- see demostreak.h

// Victims we killed while holding a set_turn_to_gold reskin, waiting for their ragdoll.
// player_death is fired while the packet is parsed; C_TFRagdoll::CreateTFRagdoll runs from the
// OnDataChanged pass at the END of that update, so the event is always in hand first.
struct PendingGold { int victimIdx; DWORD tick; };
static PendingGold s_goldPending[8] = {};
static int         s_goldPendingPos = 0;

// Defined with the rest of the killstreak work below; the FrameStageNotify pass needs them.
static bool AnyKillstreakWanted();
__declspec(noinline) static void ApplyKillStreak(int localIdx, int count);
__declspec(noinline) static void RestampStreak(const char* where);
static void ResetStreak(const char* why, bool death = false);
static int  StreakTotal();
// Custom killstreak color, defined in its own section below.
static int  KsSheenIndexFor(const SkinCfg& c);
static void KsColorUpdate(const SkinCfg* held);
static void KsColorRestoreAll();
static int  KsKickedStreak(int count);
__declspec(noinline) static const SkinCfg* ActiveWeaponCfg(int* outDefIdx, int* outConfigKey = nullptr);

// ==== SEH leaves ================================================================================
// MSVC forbids __try in a function that also unwinds an object (C2712), so every raw read the
// hook needs is its own noinline leaf.

__declspec(noinline) static unsigned int SEH_ReadHandle(void* pBase, int offset, int i)
{
    __try {
        return *reinterpret_cast<unsigned int*>(
            reinterpret_cast<uintptr_t>(pBase) + offset + i * (int)sizeof(unsigned int));
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

__declspec(noinline) static bool SEH_VtableSane(void* pEnt)
{
    __try {
        void** vtbl = *reinterpret_cast<void***>(pEnt);
        return reinterpret_cast<uintptr_t>(vtbl) >= 0x10000;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// A networked EHANDLE packs the entity index in its low 14 bits. Everything else in it is
// the serial number, and passing the raw handle to a vtable slot calls the wrong function.
__declspec(noinline) static void* SEH_EntFromHandle(unsigned int hEnt)
{
    void* p = nullptr;
    __try {
        const int idx = (int)(hEnt & 0x3FFF);
        using GetClientEntity_t = void*(__fastcall*)(void*, int);
        void** vtbl = *reinterpret_cast<void***>(g_entityList);
        if (reinterpret_cast<uintptr_t>(vtbl) >= 0x10000)
            p = ((GetClientEntity_t)vtbl[3])(g_entityList, idx);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return p;
}

__declspec(noinline) static int SEH_ReadInt(void* pEnt, int offset)
{
    __try { return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pEnt) + offset); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

__declspec(noinline) static void SEH_WriteInt(void* pEnt, int offset, int val)
{
    __try { *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pEnt) + offset) = val; }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

__declspec(noinline) static bool SEH_WriteBool(void* pEnt, int offset, bool val)
{
    __try {
        *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(pEnt) + offset) = val;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ==== engine attribute API ======================================================================
// CAttributeList::SetRuntimeAttributeValue (econ_item_view.cpp:1754) adds/sets the attribute
// with the engine's own allocator and then calls m_pManager->OnAttributeValuesChanged, which
// is what clears the attribute-hook cache the sheen / paintkit / particle code reads through.
//
// It needs no schema lookup: the ONLY thing it reads off the attribute definition is
// m_nDefIndex (econ_item_schema.h:780, a WORD at +8 behind the KeyValues*). Disasm, client
// 10-30 build, client+0x3A3260: `lea rdi,[rdx+8]` / `movzx r10d, word ptr [rdi]`, nothing else.
// So a two-field stand-in is a complete definition for this call. (The old path looked the
// def up through a CEconItemSchema VTABLE probe of slots 3..7 -- GetAttributeDefinition is
// not virtual, those slots are GetRarityName & co, so it returned null and fell back to a raw
// write with the wrong element layout, i.e. nothing applied.)
struct AttrDefStub { void* kv; uint16_t defIndex; };

// x64 CEconItemAttribute as the same function lays it out: stride 0x18, def index WORD at +8,
// value at +0xC, refundable currency at +0x10 (`lea rcx,[rax+rax*2]` / `cmp word [rdx+rcx*8+8]`
// / `movss xmm0,[rcx+0xC]`). types.h's CEconItemAttribute puts the value at +0x10 -- wrong.
struct EconAttrX64 { void* vtbl; uint16_t defIndex; uint16_t _pad; float value; int refund; int _pad2; };
static_assert(sizeof(EconAttrX64) == 0x18, "CEconItemAttribute x64 stride");

static SetRuntimeAttributeValue_t s_fnSetRuntimeAttributeValue = nullptr;
// Manual particle dispatch, for the Exorcism spell (see the player_death section below).
static CTE_DispatchEffect_t       s_fnDispatchEffect           = nullptr;
static Get_ParticleSystemIndex_t  s_fnGetParticleIndex         = nullptr;
static bool                       s_sigsResolved               = false;

// The list's own element, or null. CAttributeList = { vtbl, CUtlVector{mem,alloc,grow,size,elems},
// m_pManager } -- the function reads the size at +0x18 and the memory at +8.
__declspec(noinline) static const EconAttrX64* FindAttr(const CAttributeList* pList, uint16_t index)
{
    __try {
        const int n = pList->m_Attributes.m_Size;
        const auto* mem = reinterpret_cast<const EconAttrX64*>(pList->m_Attributes.m_Memory.m_pMemory);
        if (n <= 0 || n > 256 || reinterpret_cast<uintptr_t>(mem) < 0x10000) return nullptr;
        for (int i = 0; i < n; ++i)
            if (mem[i].defIndex == index) return &mem[i];
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}

__declspec(noinline) static bool EngineSetAttribute(CAttributeList* pList, uint32_t index, float value)
{
    if (!s_fnSetRuntimeAttributeValue) return false;
    // Same bits already there: skip, so the manager's cache is not thrown away every frame.
    if (const EconAttrX64* a = FindAttr(pList, (uint16_t)index))
        if (memcmp(&a->value, &value, sizeof(float)) == 0) return true;
    const AttrDefStub def{ nullptr, (uint16_t)index };
    __try {
        s_fnSetRuntimeAttributeValue(pList, (void*)&def, value);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        static bool s_logged = false;
        if (!s_logged) { s_logged = true; DebugLog("skins: SEH in SetRuntimeAttributeValue (attr %u)", index); }
    }
    return false;
}

// An int-valued attribute (paint kit id, pattern seed halves, kill count) is stored in the
// float slot as raw bits, not converted.
static inline float AsFloatBits(int v) { return *reinterpret_cast<float*>(&v); }

static void ResolveSkinSigs()
{
    if (s_sigsResolved) return;
    HMODULE hClient = GetModuleHandleA("client.dll");
    if (!hClient) return;
    s_sigsResolved = true;

    // Each pattern was matched against the shipping x64 client.dll (10-30 build) and is unique
    // there; the disasm that proves what the function is sits next to it. One log line per sig.
    auto scan = [&](const char* what, const char* pat) -> uint8_t* {
        uint8_t* a = PatternScan(hClient, pat);
        if (a) DebugLog("skins: sig %s OK at client+0x%llX", what,
                        (unsigned long long)(reinterpret_cast<uintptr_t>(a) - reinterpret_cast<uintptr_t>(hClient)));
        else   DebugLog("skins: sig %s NOT FOUND", what);
        return a;
    };

    // CAttributeList::SetRuntimeAttributeValue -- `mov r9d,[rcx+18h]` (m_Attributes.Count()),
    // `lea rdi,[rdx+8]` (&pAttrDef->m_nDefIndex). Without it NO attribute is applied: warpaint,
    // killstreak, unusual, australium, festivizer, stat clock and spells all go through here.
    if (uint8_t* a = scan("SetRuntimeAttributeValue",
            "48 89 5C 24 ? 55 56 57 48 8B EC 48 83 EC ? 44 8B 49 18 48 8D 7A 08"))
        s_fnSetRuntimeAttributeValue = reinterpret_cast<SetRuntimeAttributeValue_t>(a);

    // DispatchEffect(const char*, const CEffectData&) + GetParticleSystemIndex(const char*): the
    // Exorcism ghosts are a server effect, so the client has to be told to draw them by hand.
    // DispatchEffect builds a CPVSFilter and calls te->DispatchEffect (vtable +0x130);
    // GetParticleSystemIndex returns 0 after its 0xFFFF "not found" Warning.
    if (uint8_t* a = scan("DispatchEffect",
            "48 89 5C 24 ? 57 48 83 EC ? 48 8B D9 48 8B FA 48 8D 4C 24 ? E8 ? ? ? ? 48 8D 05"))
        s_fnDispatchEffect = reinterpret_cast<CTE_DispatchEffect_t>(a);
    if (uint8_t* a = scan("GetParticleSystemIndex",
            "40 53 48 83 EC ? 48 8B D9 48 85 C9 74 ? 48 8B 0D ? ? ? ? 48 8B D3 48 8B 01 FF 50 ? 3D FF FF 00 00"))
        s_fnGetParticleIndex = reinterpret_cast<Get_ParticleSystemIndex_t>(a);
}

// IVEngineClient::ClientCmd_Unrestricted -- slot 106. A full update is the only way to make
// the client rebuild an entity's material override / style skin off the def index we wrote.
static void TriggerForceFullUpdate(const char* reason)
{
    DebugLog("skins: cl_fullupdate (%s)", reason);
    s_fullUpdateTick = GetTickCount();
    if (!g_engine) return;
    __try {
        void** vtbl = *reinterpret_cast<void***>(g_engine);
        if (reinterpret_cast<uintptr_t>(vtbl[106]) > 0x10000) {
            using Fn = void(__fastcall*)(void*, const char*);
            ((Fn)vtbl[106])(g_engine, "cl_fullupdate");
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// ==== def index redirection =====================================================================
// A weapon's config key is its "upgradeable" base def index: every festive and reskin variant
// folds onto the same entry, so a skin picked on Bat still applies while a Batsaber is held.
void Skin_RedirectIndex(int& d)
{
    switch (d) {
        case 13: d=200; break; // Scattergun
        case 23: d=209; break; // Scout Pistol
        case  0: d=190; break; // Bat
        case 18: d=205; break; // Rocket Launcher
        case 10: d=199; break; // Soldier Shotgun
        case  6: d=196; break; // Shovel
        case 21: d=208; break; // Flamethrower
        case 12: d=199; break; // Pyro Shotgun
        case  2: d=192; break; // Fire Axe
        case 19: d=206; break; // Grenade Launcher
        case 20: d=207; break; // Stickybomb Launcher
        case  1: d=191; break; // Bottle
        case 15: d=202; break; // Minigun
        case 11: d=199; break; // Heavy Shotgun
        case  5: d=195; break; // Fists
        case  9: d=199; break; // Engi Shotgun
        case 22: d=209; break; // Engi Pistol
        case  7: d=197; break; // Wrench
        case 29: d=211; break; // Medi Gun
        case  8: d=198; break; // Bonesaw
        case 14: d=201; break; // Sniper Rifle
        case 16: d=203; break; // SMG
        case  3: d=193; break; // Kukri
        case 24: d=210; break; // Revolver
        case  4: d=194; break; // Knife
        // Festive variants -> the base weapon's config key
        case  669: d=200; break; // Festive Scattergun
        case  660: d=190; break; // Festive Bat
        case  999: d=221; break; // Festive Holy Mackerel
        case 1078: d= 45; break; // Festive Force-a-Nature
        case 1145: d= 46; break; // Festive Bonk! Atomic Punch
        case  658: d=205; break; // Festive Rocket Launcher
        case 1001: d=129; break; // Festive Buff Banner
        case 1085: d=228; break; // Festive Black Box
        case 1141: d=199; break; // Festive Shotgun (all classes)
        case  659: d=208; break; // Festive Flamethrower
        case 1000: d= 38; break; // Festive Axtinguisher
        case 1081: d= 39; break; // Festive Flare Gun
        case 1146: d= 40; break; // Festive Backburner
        case  661: d=207; break; // Festive Stickybomb Launcher
        case 1007: d=206; break; // Festive Grenade Launcher
        case 1082: d=132; break; // Festive Eyelander
        case 1144: d=131; break; // Festive Chargin' Targe
        case  654: d=202; break; // Festive Minigun
        case 1002: d= 42; break; // Festive Sandvich
        case 1084: d=239; break; // Festive GRU
        case  662: d=197; break; // Festive Wrench
        case 1004: d=141; break; // Festive Frontier Justice
        case 1086: d=140; break; // Festive Wrangler
        case  663: d=211; break; // Festive Medi Gun
        case 1003: d= 37; break; // Festive Ubersaw
        case 1079: d=305; break; // Festive Crusader's Crossbow
        case 1143: d=198; break; // Festive Bonesaw
        case  664: d=201; break; // Festive Sniper Rifle
        case 1005: d= 56; break; // Festive Huntsman
        case 1083: d= 58; break; // Festive Jarate
        case 1149: d=203; break; // Festive SMG
        case  665: d=194; break; // Festive Knife
        case 1006: d= 61; break; // Festive Ambassador
        case 1142: d=210; break; // Festive Revolver
        // Promo / re-release copies of a listed weapon (festivizable in the schema)
        case  832: d=811; break; // Promo Huo-Long Heater
        case  834: d=813; break; // Promo Neon Annihilator
        case 1206: d=298; break; // Iron Curtain 2026
        // Reskin variants -> the base weapon's config key
        case 30667: d=190; break; // Batsaber           -> Bat
        case   739: d=192; break; // Lollichop          -> Fire Axe
        case   727: d=194; break; // Black Rose         -> Knife
        case  1100: d=195; break; // Bread Bite         -> Fists
        case   169: d=197; break; // Golden Wrench      -> Wrench
        case   266: d=132; break; // Headtaker          -> Eyelander
        case   574: d=225; break; // Wanga Prick        -> Your Eternal Reward
        case   452: d=325; break; // Three-Rune Blade   -> Boston Basher
        case   741: d=208; break; // Rainblower         -> Flamethrower
        case 30474: d=208; break; // Nostromo Napalmer  -> Flamethrower
        case   160: d=209; break; // Lugermorph         -> Pistol
        case 30666: d=209; break; // C.A.P.P.E.R        -> Pistol
        case   161: d=210; break; // Big Kill           -> Revolver
        case 30668: d=140; break; // Giger Counter      -> Wrangler
        case   863: d= 42; break; // Robo-Sandvich      -> Sandvich
        case   433: d=159; break; // Fishcake           -> Dalokohs Bar
        case  1121: d=222; break; // Mutated Milk       -> Mad Milk
        case   572: d=221; break; // Unarmed Combat     -> Holy Mackerel
        case   466: d=153; break; // Maul               -> Homewrecker
        case  1105: d= 58; break; // Self-Aware Beauty Mark -> Jarate
        case 30665: d=526; break; // Shooting Star      -> Machina
        // Spy gadget stock -> upgradeable
        case    30: d=212; break; // Invis Watch
        case   735: d=736; break; // Sapper
        case  1080: d=736; break; // Festive Sapper
        case   297: d=212; break; // Enthusiast's Timepiece
        case   947: d=212; break; // Quackenbirdt
        case   933: d=736; break; // Ap-Sap
        case  1102: d=736; break; // Snack Attack
        case   608: d=405; break; // Bootlegger -> Ali Baba's Wee Booties
        default: break;
    }
}

int Skin_FindSpecificDef(int weaponRedir, int pkId)
{
    for (int i = 0; i < k_skinDefCount; i++)
        if (k_skinDefs[i].weaponRedir == weaponRedir && k_skinDefs[i].pkId == (int8_t)pkId)
            return k_skinDefs[i].skinDef;
    return 0;
}

int Skin_FindWeaponRedir(int skinDef)
{
    for (int i = 0; i < k_skinDefCount; i++)
        if (k_skinDefs[i].skinDef == skinDef) return k_skinDefs[i].weaponRedir;
    return 0;
}

// Any weapon-specific warpaint def for this weapon. Used by the stat clock: the clock's model
// path is a STATIC attribute on the paintkit_base prefab (items_game.txt ~21985,
// weapon_uses_stattrak_module = models/weapons/c_models/stattrack.mdl), so only a 15xxx def
// carries it and only a weapon wearing one can ever be handed a clock. Which skin it is does
// not matter -- the paint is suppressed in Skin_Apply.
static int Skin_FirstSkinDefFor(int weaponRedir)
{
    for (int i = 0; i < k_skinDefCount; i++)
        if (k_skinDefs[i].weaponRedir == weaponRedir) return k_skinDefs[i].skinDef;
    return 0;
}

static int Skin_GetFestiveIdx(int redirIdx)
{
    for (int i = 0; i < k_wpnCount; i++)
        if (k_wpnList[i].redir == redirIdx && k_wpnList[i].festiveIdx > 0)
            return k_wpnList[i].festiveIdx;
    return 0;
}

// ==== IVModelInfo ===============================================================================
// Slots counted off IVModelInfo in the SDK's own public/engine/ivmodelinfo.h, which declares the
// SAME interface version the game asks for ("VModelInfoClient006"): 1 GetModel, 2 GetModelIndex,
// 3 GetModelName, 21 FindModel(cache), 22 FindModel(hdr,cache,name), 23 GetVirtualModel,
// 28 GetStudiomodel, 46 GetCacheHandle, 52 RegisterDynamicModel, 53 IsDynamicModelLoading,
// 54 AddRefDynamicModel.
//
// Do NOT count these off src/hacks/aim/defs.h. That header's IVModelInfo drops six of the
// obsolete-but-still-present stubs (InitDynamicModels, ShutdownDynamicModels, AddDynamicModel,
// ReferenceModel, UnreferenceModel, CleanupDynamicModels), which the real header keeps -- with
// bodies, so they occupy slots. Everything past FindOrLoadModel is six slots short there, which
// is where "RegisterDynamicModel is 46" came from. 46 is GetCacheHandle(const model_t*).

__declspec(noinline) static const void* MI_GetModel(int idx)
{
    if (!g_modelInfo || idx == 0) return nullptr;
    const void* r = nullptr;
    __try {
        using Fn = const void*(__fastcall*)(void*, int);
        r = ((Fn)(*(void***)g_modelInfo)[1])(g_modelInfo, idx);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}

__declspec(noinline) static const char* MI_GetModelName(const void* pModel)
{
    if (!g_modelInfo || !pModel) return nullptr;
    const char* r = nullptr;
    __try {
        using Fn = const char*(__fastcall*)(void*, const void*);
        r = ((Fn)(*(void***)g_modelInfo)[3])(g_modelInfo, pModel);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}

__declspec(noinline) static const void* MI_GetStudiomodel(const void* pModel)
{
    if (!g_modelInfo || !pModel) return nullptr;
    const void* r = nullptr;
    __try {
        using Fn = const void*(__fastcall*)(void*, const void*);
        r = ((Fn)(*(void***)g_modelInfo)[28])(g_modelInfo, pModel);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}

// GetVirtualModel (slot 23) was used to walk virtualmodel_t by hand. The live log answered
// "NO layout matched" for every candidate offset -- TF2's x64 tier1 does not lay CUtlVector out
// the way the 2013 headers do -- and the merged sequence list is derivable from the mdl files
// alone, so that call is gone. See the sequence-table section.
//
// FindModel(hdr, cache, name) is slot 22 -- the three-argument one. NOT 21: MSVC puts overloaded
// virtuals in reverse declaration order, so 21 is FindModel(void*) (engine.dll disasm: a bare jmp
// to IMDLCache::GetStudioHdr). Called with a studiohdr, 21 took the pointer's low 16 bits as an
// MDL handle: some other model's header (the "wrong header" below), or an AV in datacache.dll
// when that handle slot was empty. 22 is `if (!*cache) *cache = GetModelForName(name);
// return GetExtraData(*cache)`. It is exactly what
// studiohdr_t::FindModel wraps (game/shared/studio_shared.cpp:16, one line:
// `return modelinfo->FindModel(this, cache, modelname)`) and what virtualmodel_t::AppendModels
// calls for every $includemodel (public/studio_virtualmodel.cpp:111). It hands back the
// include's studiohdr DIRECTLY: no model index, no dynamic registration, no async wait.
//
// dih-sk5 went the other way -- RegisterDynamicModel on the include path -- and printed
// "not loaded yet (idx=-17)" 48 times: a client-side dynamic model is loaded lazily and only
// stays loaded while something holds a reference, and nothing did, so the studiohdr never
// appeared. FindModel takes that reference itself (the SDK comment at AppendModels says so),
// so the caller passes a fresh null cache each time, as AppendModels does.
__declspec(noinline) static const void* MI_FindModel(const void* pHdr, void** cache,
                                                     const char* name)
{
    if (!g_modelInfo || !pHdr || !cache || !name) return nullptr;
    const void* r = nullptr;
    __try {
        using Fn = const void*(__fastcall*)(void*, const void*, void**, const char*);
        r = ((Fn)(*(void***)g_modelInfo)[22])(g_modelInfo, pHdr, cache, name);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}

// AddRefDynamicModel, slot 54 -- only meaningful for a dynamic index (< -1). The fallback path
// below needs it: without a reference the engine never finishes (or even starts) the load.
__declspec(noinline) static void MI_AddRefDynamic(int idx)
{
    if (!g_modelInfo || idx >= -1) return;
    __try {
        using Fn = void(__fastcall*)(void*, int);
        ((Fn)(*(void***)g_modelInfo)[54])(g_modelInfo, idx);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Anything handed back as a studiohdr is believed only if it starts with "IDST". A wrong slot
// returns a plausible pointer and reading sequences off it is a crash.
__declspec(noinline) static bool HDR_IsStudio(const void* pHdr)
{
    __try {
        return pHdr && *(int*)pHdr == 0x54534449;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// studiohdr_t::name, a fixed 64-byte string at +0x0C. The model it belongs to, in its own words.
__declspec(noinline) static const char* HDR_Name(const void* pHdr)
{
    __try {
        if (!pHdr || *(int*)pHdr != 0x54534449) return nullptr;
        const char* n = (const char*)pHdr + 0x0C;
        return n[0] ? n : nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static const char* PathBaseName(const char* p)
{
    const char* s = p;
    for (const char* c = p; *c; ++c)
        if (*c == '/' || *c == '\\') s = c + 1;
    return s;
}

// Does this header belong to the model we asked for? The name is stored as the studiomdl input
// path, so only the file name is comparable.
static bool HDR_NamesModel(const void* pHdr, const char* path)
{
    const char* n = HDR_Name(pHdr);
    return n && path && _stricmp(PathBaseName(n), PathBaseName(path)) == 0;
}

__declspec(noinline) static int MI_GetModelIndex(const char* path)
{
    if (!g_modelInfo || !path) return -1;
    int r = -1;
    __try {
        using Fn = int(__fastcall*)(void*, const char*);
        r = ((Fn)(*(void***)g_modelInfo)[2])(g_modelInfo, path);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}

// RegisterDynamicModel, slot 52. Verified on the shipping engine.dll (10-30): the
// VModelInfoClient006 factory returns the object whose vtable[52] opens with
// `test r8b,r8b / jne / mov eax,-1` -- RegisterDynamicModel(name, bClientSide) refusing a
// server-side request. [46] is GetCacheHandle (`cmp [rdx+20h],3 / movzx eax,word [rdx+48h]`),
// [54] AddRefDynamicModel (`cmp edx,-1`). The old 52 -> 46 fallback called GetCacheHandle with
// a string; gone. This call is the ONLY thing that makes a reskin nobody in the server owns
// drawable at all -- see GetReskinViewmodelIdx.
static constexpr int s_registerSlot = 52;

__declspec(noinline) static int MI_RegisterDynamicAt(int slot, const char* path)
{
    if (!g_modelInfo || !path) return -1;
    int r = -1;
    __try {
        using Fn = int(__fastcall*)(void*, const char*, bool);
        r = ((Fn)(*(void***)g_modelInfo)[slot])(g_modelInfo, path, true);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}

// True when idx names exactly the model we asked for. A dynamic index is < -1; a precached one
// is > 0. Anything else (0, -1, or an index whose name does not match) is not an answer.
static bool ModelIdxIs(int idx, const char* path)
{
    if (idx == 0 || idx == -1) return false;
    const void* m = MI_GetModel(idx);
    const char* n = m ? MI_GetModelName(m) : nullptr;
    return n && _stricmp(n, path) == 0;
}

__declspec(noinline) static int MI_RegisterDynamic(const char* path)
{
    int r = MI_RegisterDynamicAt(s_registerSlot, path);
    if (ModelIdxIs(r, path)) return r;
    // GetModelIndex answers for a model the register call did load even when the return value
    // was not the index (a wrong slot returns junk but does nothing), so ask by name too.
    const int byName = MI_GetModelIndex(path);
    if (ModelIdxIs(byName, path)) return byName;
    DebugLog("skins: RegisterDynamicModel gave nothing usable for '%s' (slot %d -> %d, "
             "by name -> %d)", path, s_registerSlot, r, byName);
    return -1;
}

// IsDynamicModelLoading is slot 53 (s_registerSlot + 1) if a "still loading?" answer is ever
// wanted again: a freshly registered model has an index immediately and no studiohdr until the
// load lands, which is why the anim remap retries instead of building once.

// A model index the engine hands back for a reskin path. -1/0 = "not precached", which is the
// normal answer for a weapon nobody in the server owns.
//
// This is not cosmetic bookkeeping: CEconEntity::UpdateAttachmentModels builds the weapon in
// the player's hands with InitializeAsClientEntity(pItem->GetPlayerDisplayModel(...)), and that
// function bails on `modelinfo->GetModelIndex(name) == -1` (c_baseentity.cpp:1136) -- returning
// false, so no attachment entity is created and the viewmodel draws the ARMS AND NOTHING ELSE.
// Registering the path as a client-side dynamic model is what makes GetModelIndex answer, and
// it has to happen before the def index we wrote is unpacked.
// Called once per reskin weapon per snapshot, so a miss may not retry (or log) forever: a path
// items_game names but the game files do not have would otherwise re-register and print a line
// every frame. A few attempts covers "asked before modelinfo was ready", nothing covers a bad
// path.
struct ReskinIdxEntry { int idx; int tries; };
static constexpr int k_maxRegisterTries = 8;

static int GetReskinViewmodelIdx(int defindex)
{
    static std::unordered_map<int, ReskinIdxEntry> s_cache;
    ReskinIdxEntry& e = s_cache[defindex];
    if (e.idx != 0 || e.tries >= k_maxRegisterTries) return e.idx;

    const ReskinInfo* ri = Skin_FindReskinInfo(defindex);
    if (!ri || !ri->model) { e.tries = k_maxRegisterTries; return 0; }
    // No modelinfo yet is not an answer, and must not burn the retry budget.
    if (!g_modelInfo) return 0;

    ++e.tries;
    int raw = MI_GetModelIndex(ri->model);
    if (!ModelIdxIs(raw, ri->model)) raw = MI_RegisterDynamic(ri->model);
    if (raw > 0 || raw < -1) e.idx = raw;

    if (e.idx != 0 || e.tries == k_maxRegisterTries)
        DebugLog("skins: reskin def=%d '%s' idx=%d%s", defindex, ri->model, e.idx,
                 (e.idx < -1) ? " (dynamic)" : (e.idx == 0 ? " -- NOT FOUND, gave up" : ""));
    return e.idx;
}

// ==== sequence remap ============================================================================
// A reskin that plays different sequences to the weapon it replaces looks wrong: the base weapon
// asks for ACT_MELEE_VM_IDLE and an all-class model only has ACT_MELEE_ALLCLASS_VM_IDLE. The
// table maps sequence index -> sequence index.
//
// WHAT DECIDES THE MAPPING. Two things, in TF2's own order
// (CTFWeaponBase::TranslateViewmodelHandActivityInternal, tf_weaponbase.cpp:4415):
//   1. the item's animation_replacement block -- GetActivityOverride is consulted FIRST and
//      returns immediately when it answers. It is an exact activity->activity list, and for 27
//      of the 44 reskins it is the whole story. The Snack Attack has the same anim_slot as the
//      stock sapper ("building") and differs ONLY here (ACT_VM_DRAW -> ACT_BREADSAPPER_VM_DRAW),
//      which is why a slot-only remap left it playing the stock sapper's animation on a bread
//      monster's skeleton.
//   2. failing that, s_viewmodelacttable, which is a per-family string surgery on the base
//      activity: ACT_VM_<X> + family ITEM1 -> ACT_ITEM1_VM_<X>. Carried here as the family's
//      prefix (ReskinInfo::animPfx / BaseAnimEntry::animPfx).
// Both halves are generated into skindata.h from items_game.txt, per reskin AND per base weapon,
// so nothing about a particular weapon is hand-written here.
//
// The sequence the SERVER picked is the base weapon's (it knows nothing about the reskin), so
// each pair is walked base-side first: the activity the base plays for that request is the index
// to catch, and the reskin's activity is the index to play instead.
//
// WHICH MODEL. Not the weapon's. CTFWeaponBase::GetViewModel returns the CLASS ARMS model for
// any item that ShouldAttachToHands (tf_weaponbase.cpp:651) -- the weapon c_model is a separate
// bonemerged attachment entity -- so the viewmodel entity's model is c_<class>_arms.mdl and
// every SetSequence index is an index into ITS sequence list. dih-sk3 compared c_knife.mdl
// against c_saxxy.mdl, which is a pair of models the engine never indexes here; that is the
// whole of "mapped=0".
//
// The arms model's own 59 sequences carry NO activity names at all; the names live in the model
// it $includemodel's (c_spy_animations.mdl, 97 sequences), reached through the virtualmodel.
// Both halves of every remap we want are in that one file, e.g.
//     knife_idle          ACT_MELEE_VM_IDLE            acr_idle     ACT_ITEM1_VM_IDLE
//     melee_allclass_idle ACT_MELEE_ALLCLASS_VM_IDLE   eternal_idle ACT_ITEM2_VM_IDLE
// so this is a WITHIN-ONE-MODEL family swap and no base c_model is read.
//
// studiohdr_t layout (studio.h, x64 -- every pointer field is an int there, so the on-disk
// offsets hold): numlocalseq +0xBC, localseqindex +0xC0. mstudioseqdesc_t stride 0xD4,
// szactivitynameindex +0x08.

static int      s_seqRemap[512];
static bool     s_seqRemapBuilt = false;
static AnimSlot s_activeFrom    = AS_DEFAULT;
static AnimSlot s_activeTo      = AS_DEFAULT;
static int      s_activeReskin  = 0;
static int      s_activeBaseKey = 0;   // config key; a change to it rebuilds the table
static int      s_remapTries    = 0;   // build attempts since the last clear; caps the retry

// The two schema rows the table is built from, and whether they differ at all. A reskin with no
// replacement list and the base's own family (a Batsaber on a bat) has nothing to remap.
static const ReskinInfo*    s_activeReskinInfo = nullptr;
static const BaseAnimEntry* s_activeBaseInfo   = nullptr;
static bool                 s_activeNeedsRemap = false;

// A reskin's model can still be loading (it is registered dynamically), and a studiohdr that is
// not there yet maps nothing. Rebuilding every SetSequence until it lands would walk both
// sequence tables hundreds of times a second, so retries are capped and spaced.
static constexpr int k_maxRemapTries = 240;

static void ClearSeqRemap()
{
    memset(s_seqRemap, 0xFF, sizeof(s_seqRemap));
    s_seqRemapBuilt = false;
    s_remapTries    = 0;
}

static const char* SlotPrefix(AnimSlot s)
{
    switch (s) {
    case AS_MELEE_ALLCLASS: return "ACT_MELEE_ALLCLASS_VM_";
    case AS_ITEM1:          return "ACT_ITEM1_VM_";
    case AS_ITEM2:          return "ACT_ITEM2_VM_";
    case AS_DEFAULT:        return "ACT_MELEE_VM_";
    default:                return nullptr;   // AS_NONE: building / watch, never remapped
    }
}

// Where a backstab lands in the target family. All-class models have no backstab at all, so
// the whole ACT_BACKSTAB_VM_* family collapses onto that family's idle.
static const char* BackstabTarget(AnimSlot to, bool& outIsPrefix)
{
    switch (to) {
    case AS_MELEE_ALLCLASS: outIsPrefix = false; return "ACT_MELEE_ALLCLASS_VM_IDLE";
    case AS_ITEM1:          outIsPrefix = true;  return "ACT_ITEM1_BACKSTAB_VM_";
    case AS_ITEM2:          outIsPrefix = true;  return "ACT_ITEM2_BACKSTAB_VM_";
    default:                outIsPrefix = false; return nullptr;
    }
}

__declspec(noinline) static int HDR_SeqCount(const void* pHdr)
{
    __try {
        if (!pHdr) return 0;
        if (*(int*)pHdr != 0x54534449) return 0;             // "IDST"
        const int n = *(int*)((char*)pHdr + 0xBC);
        return (n >= 0 && n <= 4096) ? n : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

__declspec(noinline) static const char* HDR_SeqActName(const void* pHdr, int i)
{
    __try {
        if (!pHdr || *(int*)pHdr != 0x54534449) return nullptr;
        const int numSeq = *(int*)((char*)pHdr + 0xBC);
        if (i < 0 || i >= numSeq) return nullptr;
        const int seqOff = *(int*)((char*)pHdr + 0xC0);
        if (seqOff <= 0) return nullptr;
        const char* pD = (char*)pHdr + seqOff + i * 0xD4;
        const int actOff = *(int*)(pD + 0x08);
        if (actOff <= 4 || actOff >= 800000) return nullptr;
        const char* name = pD + actOff;
        return (name[0] >= 'A' && name[0] <= 'Z') ? name : nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// ---- the model's own sequence NAME table -------------------------------------------------------
// The index SetSequence is handed is a VIRTUAL sequence index: for a model with $includemodel the
// engine merges the include's sequences into the parent's list. dih-sk4 tried to read that list
// out of virtualmodel_t by probing its CUtlVector offsets and the log answered
// "NO layout matched" -- TF2's x64 tier1 does not lay them out the way the 2013 headers do.
//
// It does not have to be read at all. AppendSequences (public/studio_virtualmodel.cpp:142) builds
// the merged list by LABEL: group 0 (the model itself) contributes its sequences in file order,
// then each include contributes only the labels that are not already there, in file order. So the
// INDEX SPACE is fully determined by the label lists in the mdl files, which are plain int offsets
// this code already reads. Rebuild the same order here and there is nothing left to guess:
//
//   c_spy_arms.mdl        59 sequences, labels only, NO activity names
//   + c_spy_animations.mdl 97 sequences, same labels in the same order + the names
//     (knife_idle ACT_MELEE_VM_IDLE, acr_idle ACT_ITEM1_VM_IDLE, eternal_idle ACT_ITEM2_VM_IDLE,
//      melee_allclass_idle ACT_MELEE_ALLCLASS_VM_IDLE ...)
//
// studiohdr_t: numincludemodels +0x150, includemodelindex +0x154; mstudiomodelgroup_t is
// { int szlabelindex; int sznameindex; } (stride 8) and the name is the full model path.
static constexpr int k_maxSeqs = 512;

static const char* s_seqName[k_maxSeqs];    // activity name, null when the sequence has none
static const char* s_seqLabel[k_maxSeqs];
static int         s_seqNameCount = 0;

__declspec(noinline) static const char* HDR_SeqLabel(const void* pHdr, int i)
{
    __try {
        if (!pHdr || *(int*)pHdr != 0x54534449) return nullptr;
        const int numSeq = *(int*)((char*)pHdr + 0xBC);
        if (i < 0 || i >= numSeq) return nullptr;
        const int seqOff = *(int*)((char*)pHdr + 0xC0);
        if (seqOff <= 0) return nullptr;
        const char* pD = (char*)pHdr + seqOff + i * 0xD4;
        const int lblOff = *(int*)(pD + 0x04);
        if (lblOff <= 4 || lblOff >= 800000) return nullptr;
        return pD + lblOff;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

__declspec(noinline) static int HDR_IncludeCount(const void* pHdr)
{
    __try {
        if (!pHdr || *(int*)pHdr != 0x54534449) return 0;
        const int n = *(int*)((char*)pHdr + 0x150);
        return (n > 0 && n <= 64) ? n : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

__declspec(noinline) static const char* HDR_IncludeName(const void* pHdr, int i)
{
    __try {
        if (!pHdr || *(int*)pHdr != 0x54534449) return nullptr;
        const int n   = *(int*)((char*)pHdr + 0x150);
        const int off = *(int*)((char*)pHdr + 0x154);
        if (i < 0 || i >= n || off <= 0) return nullptr;
        const char* pG = (char*)pHdr + off + i * 8;
        const int nameOff = *(int*)(pG + 4);
        if (nameOff <= 0 || nameOff >= 800000) return nullptr;
        return pG + nameOff;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static int SeqLabelIndex(const char* label)
{
    for (int i = 0; i < s_seqNameCount; ++i)
        if (s_seqLabel[i] && _stricmp(s_seqLabel[i], label) == 0) return i;
    return -1;
}

// Returns false only when an include is named but its studiohdr is not loaded yet -- that is a
// wait, and the caller must keep its retry budget for it.
static bool BuildSeqNames(const void* hdr)
{
    s_seqNameCount = 0;
    const int localCount = HDR_SeqCount(hdr);
    for (int i = 0; i < localCount && s_seqNameCount < k_maxSeqs; ++i) {
        s_seqLabel[s_seqNameCount] = HDR_SeqLabel(hdr, i);
        s_seqName [s_seqNameCount] = HDR_SeqActName(hdr, i);
        ++s_seqNameCount;
    }

    bool complete = true;
    const int incCount = HDR_IncludeCount(hdr);
    for (int k = 0; k < incCount; ++k) {
        const char* path = HDR_IncludeName(hdr, k);
        if (!path || !path[0]) continue;

        // The engine's own include lookup first (slot 22), fresh null cache per call as
        // AppendModels does (a kept one would hold a model_t* across map changes). It answers
        // immediately or not at all. The header still has to name the model asked for (a
        // missing include can come back as error.mdl); if not, this include alone takes the
        // dynamic path.
        const char* via   = "findmodel";
        void*       cache = nullptr;
        const void* ih    = MI_FindModel(hdr, &cache, path);
        if (ih && !HDR_NamesModel(ih, path)) {
            const char* got = HDR_Name(ih);
            DebugLog("skins: anim remap -- FindModel (slot 22) answered for '%s' with "
                     "'%s' (%d seqs) -- not that model, dynamic path for this include",
                     path, got ? got : "?", HDR_SeqCount(ih));
            ih  = nullptr;
            via = "findmodel-wrong";
        }

        // Fallback: the dih-sk5 dynamic path, plus the reference it was missing. Slower, and it
        // needs the caller's retries because the load lands asynchronously.
        int idx = 0;
        if (!ih) {
            idx = MI_GetModelIndex(path);
            if (!ModelIdxIs(idx, path)) idx = MI_RegisterDynamic(path);
            if (idx < -1) MI_AddRefDynamic(idx);
            const void* m = (idx > 0 || idx < -1) ? MI_GetModel(idx) : nullptr;
            ih  = m ? MI_GetStudiomodel(m) : nullptr;
            via = "dynamic";
        }
        if (!ih) {
            DebugLog("skins: anim remap -- include '%s' not loaded yet (idx=%d, %s)",
                     path, idx, via);
            complete = false;
            continue;
        }

        const int n = HDR_SeqCount(ih);
        int added = 0, named = 0;
        for (int j = 0; j < n; ++j) {
            const char* lbl = HDR_SeqLabel(ih, j);
            const char* act = HDR_SeqActName(ih, j);
            if (!lbl) continue;
            const int e = SeqLabelIndex(lbl);
            if (e >= 0) {
                // Same label = the same merged slot. The include's copy overrides the parent's
                // forward declaration, so its activity name is the one that counts.
                if (act) { s_seqName[e] = act; ++named; }
            } else if (s_seqNameCount < k_maxSeqs) {
                s_seqLabel[s_seqNameCount] = lbl;
                s_seqName [s_seqNameCount] = act;
                ++s_seqNameCount;
                ++added;
            }
        }
        DebugLog("skins: anim remap include '%s' via=%s seqs=%d named=%d appended=%d (incl. its "
                 "own includes: %d)", path, via, n, named, added, HDR_IncludeCount(ih));
    }
    return complete;
}

static int SeqNameIndex(const char* want)
{
    for (int i = 0; i < s_seqNameCount; ++i)
        if (s_seqName[i] && strcmp(s_seqName[i], want) == 0) return i;
    return -1;
}

static int SeqPrefixCount(const char* pfx)
{
    const size_t n = strlen(pfx);
    int c = 0;
    for (int i = 0; i < s_seqNameCount; ++i)
        if (s_seqName[i] && strncmp(s_seqName[i], pfx, n) == 0) ++c;
    return c;
}

// The n-th sequence carrying this activity name, or the last one when there are fewer than n.
// A family names several sequences the same way (knife_stab_a/b/c are all ACT_MELEE_VM_HITCENTER,
// and so are acr_stab_a/b/c) and the server picks among them at random, so the whole group has to
// be mapped, ordinal for ordinal, or two thirds of the swings fall through unmapped.
static int SeqNameNth(const char* want, int n)
{
    int last = -1, seen = 0;
    for (int i = 0; i < s_seqNameCount; ++i) {
        if (!s_seqName[i] || _stricmp(s_seqName[i], want) != 0) continue;
        last = i;
        if (seen++ == n) return i;
    }
    return last;
}

// One activity -> one activity, across every sequence that carries the name.
static int MapActName(const char* from, const char* to)
{
    if (!from || !to || _stricmp(from, to) == 0) return 0;
    int mapped = 0, ord = 0;
    for (int i = 0; i < s_seqNameCount; ++i) {
        if (!s_seqName[i] || _stricmp(s_seqName[i], from) != 0) continue;
        const int j = SeqNameNth(to, ord++);
        if (j < 0 || j == i || s_seqRemap[i] >= 0) continue;
        s_seqRemap[i] = j;
        ++mapped;
    }
    return mapped;
}

// What the BASE weapon plays when the code asks for activity `act` -- the index the server will
// have picked, and so the index to catch. TF2's order: the base's own animation_replacement, then
// its family's string surgery on ACT_VM_<X>, then the activity itself. Each candidate is only
// taken if the model actually has a sequence with that name, which is what keeps a wrong family
// guess (or an item whose slot names a family this class's arms do not carry) harmless.
static const char* BaseActName(const char* act, const BaseAnimEntry* base)
{
    if (base) {
        for (int i = 0; i < base->actReplCount; ++i)
            if (_stricmp(base->actRepl[i].from, act) == 0) {
                if (SeqNameNth(base->actRepl[i].to, 0) >= 0) return base->actRepl[i].to;
                break;
            }

        static const char k_vm[] = "ACT_VM_";
        if (base->animPfx && strncmp(act, k_vm, sizeof(k_vm) - 1) == 0) {
            static char fam[160];
            _snprintf_s(fam, sizeof(fam), _TRUNCATE, "%s%s", base->animPfx,
                        act + sizeof(k_vm) - 1);
            if (SeqNameNth(fam, 0) >= 0) return fam;
        }
    }
    return act;
}

// The reskin's animation_replacement list, which is the exact answer wherever it has one.
static int MapActPairs(const ReskinInfo* ri, const BaseAnimEntry* base)
{
    int mapped = 0;
    if (!ri || !ri->actRepl) return 0;
    for (int k = 0; k < ri->actReplCount; ++k)
        mapped += MapActName(BaseActName(ri->actRepl[k].from, base), ri->actRepl[k].to);
    return mapped;
}

// Every ACT_<from>_<suffix> -> ACT_<to>_<suffix> in one model. A target family that has no
// entry for the suffix keeps the base sequence (the arms play it, the reskin is bonemerged to
// them, so an unmapped index is the ORIGINAL animation and never a missing one) -- except
// SWINGHARD, which every family spells HITCENTER when it has no hard swing of its own.
static int MapFamily(const char* fromPfx, const char* toPfx)
{
    const size_t fromLen = strlen(fromPfx);
    char want[160];
    int mapped = 0;
    for (int i = 0; i < s_seqNameCount; ++i) {
        const char* a = s_seqName[i];
        if (!a || s_seqRemap[i] >= 0) continue;
        if (strncmp(a, fromPfx, fromLen) != 0) continue;
        const char* suffix = a + fromLen;

        _snprintf_s(want, sizeof(want), _TRUNCATE, "%s%s", toPfx, suffix);
        int j = SeqNameIndex(want);
        if (j < 0 && strcmp(suffix, "SWINGHARD") == 0) {
            _snprintf_s(want, sizeof(want), _TRUNCATE, "%sHITCENTER", toPfx);
            j = SeqNameIndex(want);
        }
        if (j < 0 || j == i) continue;
        s_seqRemap[i] = j;
        ++mapped;
    }
    return mapped;
}

// The whole ACT_BACKSTAB_VM_* family onto one sequence -- an all-class model has no backstab.
static int MapFamilyToFixed(const char* fromPfx, const char* fixedName)
{
    const int j = SeqNameIndex(fixedName);
    if (j < 0) return 0;
    const size_t fromLen = strlen(fromPfx);
    int mapped = 0;
    for (int i = 0; i < s_seqNameCount; ++i) {
        const char* a = s_seqName[i];
        if (!a || s_seqRemap[i] >= 0) continue;
        if (strncmp(a, fromPfx, fromLen) != 0 || i == j) continue;
        s_seqRemap[i] = j;
        ++mapped;
    }
    return mapped;
}

// The stun is the one activity that is named after the family it BELONGS to instead of being
// prefixed by it: knife_stun is ACT_MELEE_VM_STUN but acr_stun is ACT_MELEE_VM_ITEM1_STUN.
static const char* StunTarget(AnimSlot to)
{
    switch (to) {
    case AS_ITEM1: return "ACT_MELEE_VM_ITEM1_STUN";
    case AS_ITEM2: return "ACT_MELEE_VM_ITEM2_STUN";
    default:       return nullptr;
    }
}

static void BuildAnimRemap(AnimSlot from, AnimSlot to, int reskinDef)
{
    const ReskinInfo*    ri   = s_activeReskinInfo;
    const BaseAnimEntry* base = s_activeBaseInfo;
    if (!ri) { s_remapTries = k_maxRemapTries; return; }

    // The family swap is the fallback half and only runs when both sides name a family. A pair
    // list on its own is enough (the Snack Attack has no family at all).
    const char* origPfx = base ? base->animPfx : nullptr;
    const char* acPfx   = ri->animPfx;

    // The viewmodel entity's own model is the only sequence space in play -- see the block
    // comment at the top of this section.
    int vmModelIdx = 0;
    if (s_pLocalViewmodelEnt && g_offModelIndex > 0)
        vmModelIdx = SEH_ReadInt(s_pLocalViewmodelEnt, g_offModelIndex) & 0xFFFF;
    const void* model = (vmModelIdx > 0) ? MI_GetModel(vmModelIdx) : nullptr;
    const void* hdr   = model ? MI_GetStudiomodel(model) : nullptr;
    if (!hdr) {
        if (s_remapTries == 1)
            DebugLog("skins: anim remap -- no viewmodel studiohdr (ent=%p modelIdx=%d)",
                     s_pLocalViewmodelEnt, vmModelIdx);
        return;   // the viewmodel entity arrives a few frames after the weapon; keep retrying
    }
    const char* vmName = MI_GetModelName(model);

    if (HDR_SeqCount(hdr) <= 0) {
        DebugLog("skins: anim remap -- '%s' has no sequences (hdr=%p)", vmName ? vmName : "?", hdr);
        s_remapTries = k_maxRemapTries;
        return;
    }
    const bool complete = BuildSeqNames(hdr);
    if (!complete && s_remapTries < k_maxRemapTries - 1) return;   // an include is still loading

    // The item's own list first: it is exact, and TF2 reads it before anything else.
    const int byPair = MapActPairs(ri, base);

    // Then the family, for every activity the list does not name (a Saxxy's list is three
    // INSPECT lines; the swings and the draw are pure ACT_MELEE_VM_ -> ACT_MELEE_ALLCLASS_VM_).
    int mapped = byPair;
    if (origPfx && acPfx && _stricmp(origPfx, acPfx) != 0) {
        mapped += MapFamily(origPfx, acPfx);

        bool bsIsPrefix = false;
        if (const char* bsTarget = BackstabTarget(to, bsIsPrefix)) {
            if (bsIsPrefix) mapped += MapFamily("ACT_BACKSTAB_VM_", bsTarget);
            else            mapped += MapFamilyToFixed("ACT_BACKSTAB_VM_", bsTarget);
        }
        if (const char* stun = StunTarget(to)) {
            const int si = SeqNameIndex("ACT_MELEE_VM_STUN");
            const int sj = SeqNameIndex(stun);
            if (si >= 0 && sj >= 0 && si != sj && s_seqRemap[si] < 0) {
                s_seqRemap[si] = sj; ++mapped;
            }
        }
    }

    // The census is the measurement: "0 names" and "names but not this family" are different
    // failures and used to print the same line.
    DebugLog("skins: anim remap %s -> %s def=%d model='%s' merged=%d (local=%d complete=%d) "
             "melee=%d allclass=%d item1=%d item2=%d backstab=%d pairs=%d/%d mapped=%d try=%d",
             origPfx ? origPfx : "(none)", acPfx ? acPfx : "(none)", reskinDef,
             vmName ? vmName : "?", s_seqNameCount,
             HDR_SeqCount(hdr), (int)complete,
             SeqPrefixCount("ACT_MELEE_VM_"), SeqPrefixCount("ACT_MELEE_ALLCLASS_VM_"),
             SeqPrefixCount("ACT_ITEM1_VM_"), SeqPrefixCount("ACT_ITEM2_VM_"),
             SeqPrefixCount("ACT_BACKSTAB_VM_"), byPair, (int)ri->actReplCount,
             mapped, s_remapTries);

    // The table was read, so this answer will not improve. Stop retrying either way.
    s_remapTries = k_maxRemapTries;
    if (mapped > 0) s_seqRemapBuilt = true;
}

// ==== SetSequence ===============================================================================
// The remap itself, split out from the hook because the aim tree also hooks
// CBaseAnimating::SetSequence off the same signature and MinHook allows one hook per target.
// Whichever half owns the hook calls this; the other half's install fails and stays inert.
int Skins_RemapSequence(void* pEntity, int nSequence)
{
    if (g_eject || !g_enableSkins) return nSequence;
    if (!s_pLocalViewmodelEnt || pEntity != s_pLocalViewmodelEnt) return nSequence;
    if (!s_activeNeedsRemap) return nSequence;

    if (!s_seqRemapBuilt && s_remapTries < k_maxRemapTries) {
        ++s_remapTries;
        BuildAnimRemap(s_activeFrom, s_activeTo, s_activeReskin);
    }
    if (s_seqRemapBuilt && nSequence >= 0 && (unsigned)nSequence < 512) {
        const int r = s_seqRemap[nSequence];
        if (r >= 0) return r;
    }
    return nSequence;
}

static void __fastcall h_SetSequence(void* rcx, int nSequence)
{
    // Guard FIRST: eject drains this counter to zero before MH_Uninitialize frees the
    // trampoline, and o_SetSequence IS that trampoline.
    MiscHookGuard _g;
    o_SetSequence(rcx, g_eject ? nSequence : Skins_RemapSequence(rcx, nSequence));
}

// ==== attribute application =====================================================================
// Killstreak "professional" kill effects, in the schema's own id order.
static const int k_ksEffectIds[] = { 0, 2002, 2003, 2004, 2005, 2006, 2007, 2008 };

// A paint kit id that is non-zero (so every "does this item have a paintkit" gate passes) and
// that no CPaintKitDefinition answers for (the real ones stop around 400), so nothing paints.
static constexpr int k_noPaintKit = 30000;

// The attribute set one config stands for. The weapon in hand (Skin_ApplyCfg) and its backpack
// copy (hats_inv.cpp) both take it from here, so the item the loadout screen names and the
// weapon you hold can never disagree. A def written twice keeps its slot, last value wins.
// inv = for the backpack CEconItem: that copy keeps its own def, so the stat clock's carrier
// trick is skipped, and an australium is strange the way every real one is.
int Skins_BuildAttrs(const SkinCfg& c, SkinAttr* out, int cap, bool inv)
{
    int n = 0;
    auto put = [&](uint16_t def, float v) {
        for (int i = 0; i < n; ++i) if (out[i].def == def) { out[i].value = v; return; }
        if (n < cap) out[n++] = { def, v };
    };

    // -- warpaint ------------------------------------------------------------------------
    // 725 wear, 731 allow_inspect (the paintkit only renders on an inspectable item),
    // 2022 loot_rarity, 866/867 the two halves of the 64-bit pattern seed.
    if (c.weaponSkinDef > 0 || c.paintKitId > 0) {
        if (c.weaponSkinDef <= 0)
            put(834, AsFloatBits(c.paintKitId));
        put(725,  c.wear);
        put(731,  1.0f);
        put(2022, 6.0f);
        put(866, AsFloatBits((int)(uint32_t)(c.pattern & 0xFFFFFFFFull)));
        put(867, AsFloatBits((int)(uint32_t)(c.pattern >> 32)));
    }

    // -- killstreak ----------------------------------------------------------------------
    if (c.ksTier > 0) {
        put(2025, (float)c.ksTier);
        // KsSheenIndexFor, not c.ksSheen: a custom color has to live in a sheen row, so
        // "custom color with no sheen picked" still writes one.
        const int sheenIdx = KsSheenIndexFor(c);
        if (sheenIdx > 0)
            put(2014, (float)sheenIdx);
        if (c.ksTier == 3 && c.ksEffect > 0) {
            const int ne = (int)(sizeof(k_ksEffectIds) / sizeof(k_ksEffectIds[0]));
            const int effectId = (c.ksEffect < ne) ? k_ksEffectIds[c.ksEffect] : 0;
            if (effectId > 0) put(2013, (float)effectId);
        }
    }

    // -- unusual particle ----------------------------------------------------------------
    // 134 is the animated attach point, 370 the static one; TF2's own unusual weapons
    // carry both. -1 means the user picked nothing (0 is the real "Invalid Particle" id).
    if (c.unusual >= 0) {
        put(134, (float)c.unusual);
        put(370, (float)c.unusual);
    }

    // -- australium ----------------------------------------------------------------------
    if (c.australium) {
        put(2027, 1.0f);   // is_australium_item
        put(542,  1.0f);   // item_style_override -> the gold style
        put(2022, 6.0f);
    }

    if (c.festivized) put(2053, 1.0f);

    // -- stat clock ----------------------------------------------------------------------
    // Two gates decide whether the clock entity is ever built (CTFWeaponBase::AddStatTrakModel,
    // tf_weaponbase.cpp:6730): a kill-eater score attribute (214 below) and GetStattrak(),
    // which needs a paint kit id AND the weapon_uses_stattrak_module string. The string is a
    // static attr on the paintkit_base prefab, so the weapon has to be wearing a 15xxx
    // warpaint def -- ResolveWeaponTarget puts it in one when no skin was picked. Here the
    // paint that def would otherwise apply is cancelled: 834 is set to an id no paintkit
    // definition answers for, so the composite never builds and the weapon looks stock.
    // Backpack copy: 214 is also what puts the strange rank in the name
    // (CStrangeRankLocalizationGenerator, econ_item_description.cpp:494).
    if (c.statClock || (inv && c.australium)) {
        put(214, AsFloatBits(c.statKills));  // kill_eater
        put(292, 0.0f);                      // score type: kills
        if (!inv && c.statClock && c.weaponSkinDef <= 0 && c.paintKitId <= 0) {
            put(834, AsFloatBits(k_noPaintKit));
            put(725, 0.2f);   // wear; GetPaintKitWear must answer
        }
    }

    if (c.spellFlags & kCfgSpell_Exorcism) put(1009, 1.0f);
    if (c.spellFlags & kCfgSpell_Pumpkin)  put(1007, 1.0f);
    if (c.spellFlags & kCfgSpell_Fire)     put(1008, 1.0f);

    // -- reskin's own item properties ----------------------------------------------------
    // Taken from items_game.txt, not hand-listed: attr 150 set_turn_to_gold is what a
    // Saxxy / Golden Wrench / Gold Frying Pan carries, and attr 542 item_style_override
    // is how a multi-style reskin (Black Rose black variant) selects its skin pair.
    // Australium already owns 542, so it wins when both are set.
    if (c.reskinDef > 0 && !c.australium) {
        if (const ReskinInfo* ri = Skin_FindReskinInfo(c.reskinDef)) {
            if (ri->turnToGold)   put(150, 1.0f);  // set_turn_to_gold
            if (ri->specialTaunt) put(551, 1.0f);  // special_taunt
            if (ri->styleCount > 1 && c.reskinStyle > 0
                && c.reskinStyle < ri->styleCount)
                put(542, (float)c.reskinStyle);
        }
    }
    return n;
}

__declspec(noinline) static void Skin_ApplyCfg(void* pWeapon, const SkinCfg& c)
{
    CAttributeList* pList = reinterpret_cast<CAttributeList*>(
        reinterpret_cast<uintptr_t>(pWeapon) + s_offAttrList);
    SkinAttr a[kSkinAttrCap];
    const int n = Skins_BuildAttrs(c, a, kSkinAttrCap, false);
    __try {
        for (int i = 0; i < n; ++i) EngineSetAttribute(pList, a[i].def, a[i].value);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void Skin_Apply(void* pWeapon, int configKey)
{
    auto it = g_skinCfgs.find(configKey);
    if (it != g_skinCfgs.end() && it->second.enabled) Skin_ApplyCfg(pWeapon, it->second);
}

// The skin index a reskin wants for the team we are on. -1 = leave the engine's own value,
// which is already right for every item whose style block is the plain 0/1 team pair.
static int ReskinSkinFor(const SkinCfg& c, int teamNum)
{
    if (c.reskinDef <= 0) return -1;
    const ReskinInfo* ri = Skin_FindReskinInfo(c.reskinDef);
    if (!ri || ri->styleCount == 0) return -1;
    int st = c.reskinStyle;
    if (st < 0 || st >= ri->styleCount) st = 0;
    const int8_t s = (teamNum == 3 /* TF_TEAM_BLUE */) ? ri->skinBlu[st] : ri->skinRed[st];
    return (s < 0) ? -1 : (int)s;
}

// ==== netvar resolution =========================================================================
static int ResolveOffset(const char* table, const char* prop, const char* fallbackTable = nullptr)
{
    RecvTable* t = FindNetworkTable(table);
    if (!t && fallbackTable) t = FindNetworkTable(fallbackTable);
    if (!t) return -1;
    const int off = FindPropOffset(t, prop);
    return (off > 0) ? off : -1;
}

static void ResolveSkinOffsets()
{
    if (s_offMyWeapons     == 0) s_offMyWeapons     = ResolveOffset("DT_BaseCombatCharacter", "m_hMyWeapons");
    if (s_offActiveWeapon  == 0) s_offActiveWeapon  = ResolveOffset("DT_BaseCombatCharacter", "m_hActiveWeapon");
    if (s_offItemDefIndex  == 0) s_offItemDefIndex  = ResolveOffset("DT_EconEntity", "m_iItemDefinitionIndex", "DT_TFWeaponBase");
    if (s_offAttrList      == 0) s_offAttrList      = ResolveOffset("DT_EconEntity", "m_AttributeList", "DT_TFWeaponBase");
    if (s_offViewModelIdx  == 0) s_offViewModelIdx  = ResolveOffset("DT_BaseCombatWeapon", "m_iViewModelIndex");
    if (s_offCustomVMModel == 0) s_offCustomVMModel = ResolveOffset("DT_BaseCombatWeapon", "m_nCustomViewmodelModelIndex");
    if (s_offHViewModel0   == 0) s_offHViewModel0   = ResolveOffset("DT_BasePlayer", "m_hViewModel[0]");
    if (s_offSkin          == 0) s_offSkin          = ResolveOffset("DT_BaseAnimating", "m_nSkin");
    if (s_offSequence      == 0) s_offSequence      = ResolveOffset("DT_BaseAnimating", "m_nSequence");
    if (s_offTeamNum       == 0) s_offTeamNum       = ResolveOffset("DT_BaseEntity", "m_iTeamNum");
    if (s_offWeaponState   == 0) s_offWeaponState   = ResolveOffset("DT_BaseCombatWeapon", "m_iState");
    if (s_offGoldRagdoll   == 0) s_offGoldRagdoll   = ResolveOffset("DT_TFRagdoll", "m_bGoldRagdoll");
    if (s_offRagdollPlayer == 0) s_offRagdollPlayer = ResolveOffset("DT_TFRagdoll", "m_hPlayer");
    // m_nStreaks lives inside m_Shared; FindPropOffset walks the nested table and adds both
    // offsets, so the DT_TFPlayer lookup lands on CTFPlayerShared::m_nStreaks itself.
    if (s_offNStreaks      == 0) s_offNStreaks      = ResolveOffset("DT_TFPlayer", "m_nStreaks");
    if (s_offIStreaks      == 0) s_offIStreaks      = ResolveOffset("DT_TFPlayerResource", "m_iStreaks");
}

// ==== the write pass ============================================================================
// One weapon: pick the def index it should report, write it, and hand the model over. Shared by
// the stage-2 pre-write and the post-update pass so the two can never disagree.
struct WeaponTarget { int configKey; int targetDef; const SkinCfg* cfg; };

// Reverse map: every def index an enabled config can WRITE, and the config key that wrote it.
// Needed because our own pre-write is what the next pass reads back. Skin_RedirectIndex only
// knows the stock/festive/class-specific variants -- it cannot fold Saxxy back onto a weapon
// (every melee can wear one), and it has no case at all for Nessie's Nine Iron or the AWPer
// Hand. Without this the pass after a pre-write finds no config and the weapon loses its
// attributes on the very next frame.
struct RevEntry { int targetDef; int configKey; };
static RevEntry s_rev[64];
static int      s_revCount = 0;

using SkinMap = std::unordered_map<int, SkinCfg>;

// Fills `out` from any config map (ours, or a peer's). Returns the count; anyKs = some killstreak on.
static int BuildReverseMap(const SkinMap& m, RevEntry* out, int cap, bool* anyKs)
{
    int n = 0;
    if (anyKs) *anyKs = false;
    for (const auto& [key, c] : m) {
        if (!c.enabled) continue;
        if (c.ksTier > 0 && anyKs) *anyKs = true;
        int t = 0;
        if      (c.festive)            t = Skin_GetFestiveIdx(key);
        else if (c.reskinDef > 0)      t = c.reskinDef;
        else if (c.weaponSkinDef > 0)  t = c.weaponSkinDef;
        else if (c.statClock)          t = Skin_FirstSkinDefFor(key);   // the clock's carrier def
        // The "wear the upgradeable twin" case needs no entry: redirecting that def index
        // lands on itself, so the direct lookup already finds the config.
        if (t > 0 && t != key && n < cap)
            out[n++] = { t, key };
    }
    return n;
}

static void RebuildReverseMap() { s_revCount = BuildReverseMap(g_skinCfgs, s_rev, 64, &s_anyKillstreak); }

// Any def index -> the config key that weapon reads. 0 = none (Bat is 0 but redirects to 190).
int Skins_ConfigKey(int def)
{
    int key = def;
    Skin_RedirectIndex(key);
    if (key <= 0) return 0;
    // A 15xxx def is a weapon-specific warpaint item; map it back to the weapon it belongs to.
    if (key == def && def >= 15000 && def < 16000) {
        const int redir = Skin_FindWeaponRedir(def);
        if (redir > 0) key = redir;
    }
    return key;
}

// The def the backpack copy is: the one the weapon in hand turns into, except the stat clock's
// 15xxx carrier (an implementation detail of the clock, not what the item is).
int Skins_InvDef(int key, const SkinCfg& c)
{
    if (c.festive)
        if (const int f = Skin_GetFestiveIdx(key)) return f;
    if (c.reskinDef > 0)     return c.reskinDef;
    if (c.weaponSkinDef > 0) return c.weaponSkinDef;
    return key;
}

static WeaponTarget ResolveWeaponTargetIn(const SkinMap& m, const RevEntry* rev, int nRev, int origDefIdx)
{
    WeaponTarget wt{ 0, 0, nullptr };

    int configKey = Skins_ConfigKey(origDefIdx);
    if (configKey <= 0) return wt;

    auto it = m.find(configKey);
    if (it == m.end() || !it->second.enabled) {
        // Not a def index the schema redirects -- is it one WE wrote last pass?
        for (int i = 0; i < nRev; ++i) {
            if (rev[i].targetDef != origDefIdx) continue;
            it = m.find(rev[i].configKey);
            if (it != m.end() && it->second.enabled) { configKey = rev[i].configKey; break; }
        }
        if (it == m.end() || !it->second.enabled) return wt;
    }
    const SkinCfg& sc = it->second;

    int targetDef = 0;
    if (sc.festive) {
        const int festIdx = Skin_GetFestiveIdx(configKey);
        if (festIdx > 0) targetDef = festIdx;
    } else if (sc.reskinDef > 0) {
        targetDef = sc.reskinDef;
    } else if (sc.weaponSkinDef > 0) {
        targetDef = sc.weaponSkinDef;
    } else if (configKey != origDefIdx && (sc.paintKitId > 0 || sc.australium || sc.festivized)) {
        // The stock def has no paintkit support; its "upgradeable" twin does.
        targetDef = configKey;
    }

    // Stat clock with no skin picked: wear a warpaint def purely for the prefab's static
    // weapon_uses_stattrak_module string, which is the only thing that can put a clock on the
    // weapon. Skin_Apply then writes a paint kit id no definition answers for, so the composite
    // never builds and the weapon keeps its normal material (c_tf_player.cpp:3583 null-checks
    // the paintkit definition, and GetPaintKitMaterialOverride does too).
    if (targetDef <= 0 && sc.statClock)
        targetDef = Skin_FirstSkinDefFor(configKey);

    wt.configKey = configKey;
    wt.targetDef = targetDef;
    wt.cfg       = &sc;
    return wt;
}

static WeaponTarget ResolveWeaponTarget(void* pWeapon, int origDefIdx)
{
    (void)pWeapon;
    return ResolveWeaponTargetIn(g_skinCfgs, s_rev, s_revCount, origDefIdx);
}

// ==== peers' weapons ============================================================================
// A peer's loadout (skinshare.cpp) goes onto the weapons in THAT player's m_hMyWeapons -- the array
// is in DT_BaseCombatCharacter proper, not the local-exclusive block (basecombatcharacter.cpp:204),
// so every player in the PVS carries it. Same two passes as ours: def index before the unpack
// (stage 2, material override / style skin) and def + m_nSkin + attributes after it. The viewmodel
// work (sequence remap, attachment refresh, streak counters) is ours alone and is not repeated.
// Gated on "show shared" only: our own skinchanger can be off and peers still show.
static bool PeerSkinsOn() { return g_shareNet && g_shareRecv && !g_eject; }

// A peer's loadout arriving used to cl_fullupdate: every entity on the server re-created, a hitch
// on every peer join (debug.log 11-05). Now only THAT peer's weapons are rebuilt, each through its
// own CREATED pass, once per (weapon entity, loadout): a new loadout or a new weapon entity
// (resupply, respawn, a weapon pickup) is one rebuild of that one weapon.
// [peer][m_hMyWeapons slot] = handle already rebuilt off s_peerBuiltHash[peer]; 0 = not yet.
static unsigned int s_peerBuilt[kShareMaxPeers][48];
static uint32_t     s_peerBuiltHash[kShareMaxPeers];
static bool         s_peerTouched = false;   // a peer weapon carries our def / attributes: "show shared" off full-updates it back

// CEconEntity::OnDataChanged(CREATED) (econ_entity.cpp:1020) is the one pass that builds the
// material override (warpaint, gold), and C_BaseCombatWeapon's half sets a non-local weapon's
// world model off the item (CalcOverrideModelIndex, c_basecombatweapon.cpp:159). On a live weapon
// every CREATED step re-runs clean: InitializeAttributes only re-points the manager, the material
// references re-Init, ListenForGameEvent skips a listener already in, particles go through
// SetParticleSystemsVisible (state-compared, no duplicate). Pre first, same as the festivizer
// refresh. false = dormant (out of PVS): try again when it is back.
__declspec(noinline) static bool RebuildPeerWeapon(unsigned int hWeapon)
{
    __try {
        void* pNet = VCall<void*>(g_entityList, 0, (int)(hWeapon & 0x3FFF));   // GetClientNetworkable
        if (!pNet || !SEH_VtableSane(pNet)) return true;
        if (VCall<bool>(pNet, 8)) return false;   // IsDormant
        void** vt = *reinterpret_cast<void***>(pNet);
        using Fn = void(__fastcall*)(void*, int);
        ((Fn)vt[4])(pNet, 1);   // OnPreDataChanged(DATA_UPDATE_DATATABLE_CHANGED)
        ((Fn)vt[5])(pNet, 0);   // OnDataChanged(DATA_UPDATE_CREATED)
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DebugLog("skins: SEH in peer weapon rebuild");
    }
    return true;
}

// rebuild: stage 5 only, after the original's ProcessOnDataChangedEvents -- a weapon created this
// frame has had its own CREATED pass before ours.
// ponytail: a skin a peer REMOVES mid-match stays on that weapon until it is re-created (only
// "show shared" off undoes our def write); store the original def per weapon if that matters.
__declspec(noinline) static void ApplyPeerWeapons(bool preWrite, bool rebuild = false)
{
    if (!PeerSkinsOn()) return;
    if (s_offMyWeapons <= 0 || s_offItemDefIndex <= 0 || (!preWrite && s_offAttrList <= 0)) return;
    __try {
        for (int p = 0; p < kShareMaxPeers; ++p) {
            const SharePeer* peer = SkinShare_Peer(p);
            if (!peer || !peer->hasLoadout || peer->playerIdx <= 0 || peer->lo.skins.empty()) continue;
            void* pPlayer = GetClientEntity(peer->playerIdx);
            if (!pPlayer || !SEH_VtableSane(pPlayer)) continue;
            if (rebuild && s_peerBuiltHash[p] != peer->loHash) {
                memset(s_peerBuilt[p], 0, sizeof(s_peerBuilt[p]));
                s_peerBuiltHash[p] = peer->loHash;
            }
            RevEntry rev[64];
            const int nRev = BuildReverseMap(peer->lo.skins, rev, 64, nullptr);
            const int teamNum = (!preWrite && s_offTeamNum > 0) ? SEH_ReadInt(pPlayer, s_offTeamNum) : 0;

            for (int i = 0; i < 48; i++) {
                const unsigned int hWeapon = SEH_ReadHandle(pPlayer, s_offMyWeapons, i);
                if (hWeapon == 0xFFFFFFFF || hWeapon == 0) continue;
                void* pWeapon = SEH_EntFromHandle(hWeapon);
                if (!pWeapon || !SEH_VtableSane(pWeapon)) continue;

                const int origDefIdx = SEH_ReadInt(pWeapon, s_offItemDefIndex);
                const WeaponTarget wt = ResolveWeaponTargetIn(peer->lo.skins, rev, nRev, origDefIdx);
                if (!wt.cfg) continue;
                // Same as ours: a reskin model nobody on the server owns has no model index, and
                // the weapon built off that def draws nothing. Cached per def, cheap.
                if (preWrite && wt.cfg->reskinDef > 0) GetReskinViewmodelIdx(wt.cfg->reskinDef);
                s_peerTouched = true;
                if (wt.targetDef > 0 && wt.targetDef != origDefIdx)
                    SEH_WriteInt(pWeapon, s_offItemDefIndex, wt.targetDef);
                if (preWrite) continue;
                if (wt.cfg->reskinDef > 0) {
                    const int skin = ReskinSkinFor(*wt.cfg, teamNum);
                    if (skin >= 0 && s_offSkin > 0) SEH_WriteInt(pWeapon, s_offSkin, skin);
                }
                Skin_ApplyCfg(pWeapon, *wt.cfg);
                if (rebuild && s_peerBuilt[p][i] != hWeapon) {
                    // the loadout can land this frame, before any stage-2 pre-write registered it
                    if (wt.cfg->reskinDef > 0) GetReskinViewmodelIdx(wt.cfg->reskinDef);
                    if (RebuildPeerWeapon(hWeapon)) s_peerBuilt[p][i] = hWeapon;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DebugLog("skins: SEH in peer pass");
    }
}

// NOTHING WRITES A MODEL INDEX ONTO THE WEAPON. dih-sk1 wrote three fields and every reskin
// drew arms and no weapon; dih-sk2 put a menu mask on them to find the offender. The SDK
// answers it outright, so the mask is gone and so are the writes:
//
//   * the weapon in the viewmodel is not the viewmodel. CEconEntity::UpdateAttachmentModels
//     (econ_entity.cpp:1148) creates a SEPARATE client entity, C_ViewmodelAttachmentModel, and
//     models it with pItem->GetPlayerDisplayModel() -- off the item definition, which is the
//     thing our pre-write already changes. No field on the weapon feeds it.
//   * m_iViewModelIndex is the arms model's index. Writing the weapon's model there replaces
//     the hands.
//   * m_nCustomViewmodelModelIndex > 0 makes CBaseCombatWeapon::SetViewModel
//     (basecombatweapon_shared.cpp:1195) swap the WHOLE viewmodel for that model and return,
//     which for a c_model weapon means a floating weapon and no arms at all.
//
// What the reskin actually needs is for its path to answer GetModelIndex -- see
// GetReskinViewmodelIdx. That is a registration, not a write.

// ==== diagnostics ===============================================================================
// Everything resolved at install time is logged again here, because DebugLog is OFF by default
// and the install lines are written before the menu can turn it on -- an empty log is not
// evidence. Prints once after the log is enabled, then one line per second while a reskin
// is active.
static void SkinsDiag(void* pLocal)
{
    if (!g_debugLogEnable) return;

    static bool s_once = false;
    if (!s_once) {
        s_once = true;
        DebugLog("skins diag: sigs setattr=%p effect=%p partidx=%p "
                 "fsn=%p setseq=%p%s holiday=%p prewrite=%p",
                 (void*)s_fnSetRuntimeAttributeValue, (void*)s_fnDispatchEffect,
                 (void*)s_fnGetParticleIndex, (void*)o_FrameStageNotify,
                 (void*)o_SetSequence, s_setSeqViaAim ? "(aim hook)" : "",
                 (void*)o_TF_IsHolidayActive, (void*)s_preWriteCallSite);
        DebugLog("skins diag: off myWpns=%d activeWpn=%d defIdx=%d attrList=%d vmIdx=%d "
                 "customVM=%d hVM0=%d skin=%d team=%d | ragdoll gold=%d player=%d "
                 "| streaks local=%d res=%d | regSlot=%d",
                 s_offMyWeapons, s_offActiveWeapon, s_offItemDefIndex, s_offAttrList,
                 s_offViewModelIdx, s_offCustomVMModel, s_offHViewModel0, s_offSkin,
                 s_offTeamNum, s_offGoldRagdoll, s_offRagdollPlayer,
                 s_offNStreaks, s_offIStreaks, s_registerSlot);
    }

    static DWORD s_lastTick = 0;
    const DWORD now = GetTickCount();
    if (now - s_lastTick < 1000) return;
    s_lastTick = now;

    if (s_offActiveWeapon <= 0 || s_offItemDefIndex <= 0) return;
    const unsigned int hAW = SEH_ReadHandle(pLocal, s_offActiveWeapon, 0);
    void* pWpn = SEH_EntFromHandle(hAW);
    if (!pWpn || !SEH_VtableSane(pWpn)) return;

    const int def = SEH_ReadInt(pWpn, s_offItemDefIndex);
    const WeaponTarget wt = ResolveWeaponTarget(pWpn, def);
    if (!wt.cfg || wt.cfg->reskinDef <= 0) return;

    const int wantIdx = GetReskinViewmodelIdx(wt.cfg->reskinDef);
    const int vmi     = (s_offViewModelIdx  > 0) ? SEH_ReadInt(pWpn, s_offViewModelIdx) : -2;
    const int cvm     = (s_offCustomVMModel > 0) ? (SEH_ReadInt(pWpn, s_offCustomVMModel) & 0xFFFF) : -2;
    const int skn     = (s_offSkin          > 0) ? SEH_ReadInt(pWpn, s_offSkin) : -2;

    // What the viewmodel entity itself ended up holding -- the arms model, or the weapon.
    int vmModelIdx = -2;
    const char* vmName = "?";
    if (s_pLocalViewmodelEnt && g_offModelIndex > 0) {
        vmModelIdx = SEH_ReadInt(s_pLocalViewmodelEnt, g_offModelIndex) & 0xFFFF;
        if (const void* m = MI_GetModel(vmModelIdx))
            if (const char* n = MI_GetModelName(m)) vmName = n;
    }

    // vmi/cvm are READS ONLY -- nothing writes them any more. They are printed because
    // "TF2 resolved the model itself" and "our attachment never got built" look identical on
    // screen and different here.
    DebugLog("skins diag: def=%d key=%d target=%d reskin=%d wantIdx=%d | m_iViewModelIndex=%d "
             "customVM=%d m_nSkin=%d | vmEnt=%p vmModel=%d '%s' | "
             "remap %d->%d built=%d tries=%d | streak=%d",
             def, wt.configKey, wt.targetDef, wt.cfg->reskinDef, wantIdx,
             vmi, cvm, skn, s_pLocalViewmodelEnt, vmModelIdx, vmName,
             (int)s_activeFrom, (int)s_activeTo,
             (int)s_seqRemapBuilt, s_remapTries, s_killStreak);
}

// stage 2 -- run BEFORE the client unpacks the snapshot, so CEconEntity::OnDataChanged builds
// the material override, the style skin and the viewmodel attachment off OUR def index. Without
// this the Golden Wrench keeps the plain wrench material and the Gold Frying Pan stays grey.
__declspec(noinline) static void PreWriteDefIndexes()
{
    __try {
        ResolveSkinSigs();
        ResolveSkinOffsets();
        RebuildReverseMap();
        if (s_offMyWeapons <= 0 || s_offItemDefIndex <= 0) return;

        const int localIdx = GetLocalPlayerIndex();
        if (localIdx < 1) return;
        void* pLocal = GetClientEntity(localIdx);
        if (!pLocal) return;

        for (int i = 0; g_enableSkins && i < 48; i++) {   // ours; peers below run either way
            const unsigned int hWeapon = SEH_ReadHandle(pLocal, s_offMyWeapons, i);
            if (hWeapon == 0xFFFFFFFF || hWeapon == 0) continue;
            void* pWeapon = SEH_EntFromHandle(hWeapon);
            if (!pWeapon || !SEH_VtableSane(pWeapon)) continue;

            const int origDefIdx = SEH_ReadInt(pWeapon, s_offItemDefIndex);
            const WeaponTarget wt = ResolveWeaponTarget(pWeapon, origDefIdx);
            if (!wt.cfg || wt.targetDef <= 0) continue;

            // Register the reskin's model BEFORE the def index it belongs to is unpacked:
            // the attachment entity is built during that unpack and refuses to exist if
            // GetModelIndex cannot answer for the path.
            if (wt.cfg->reskinDef > 0) GetReskinViewmodelIdx(wt.cfg->reskinDef);

            if (wt.targetDef != origDefIdx)
                SEH_WriteInt(pWeapon, s_offItemDefIndex, wt.targetDef);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DebugLog("skins: SEH in pre-write");
    }
    ApplyPeerWeapons(true);
}

// ==== FrameStageNotify ==========================================================================

__declspec(noinline) static void UpdateViewmodelEnt(void* pLocal)
{
    if (s_offHViewModel0 <= 0) return;
    __try {
        const unsigned int hVM = *reinterpret_cast<unsigned int*>(
            reinterpret_cast<uintptr_t>(pLocal) + (unsigned)s_offHViewModel0);
        void* pVM = SEH_EntFromHandle(hVM);
        // A new viewmodel entity is a new sequence space (a class change swaps the arms model),
        // so the table built against the old one has to go with it.
        if (pVM != s_pLocalViewmodelEnt) { s_pLocalViewmodelEnt = pVM; ClearSeqRemap(); }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Which anim families the currently held weapon is being remapped between.
__declspec(noinline) static void UpdateActiveRemap(void* pLocal)
{
    AnimSlot from = AS_DEFAULT, to = AS_DEFAULT;
    int reskin = 0, baseKey = 0;
    const ReskinInfo*    riInfo   = nullptr;
    const BaseAnimEntry* baseInfo = nullptr;

    __try {
        if (s_offActiveWeapon > 0 && s_offItemDefIndex > 0) {
            const unsigned int hAW = *reinterpret_cast<const unsigned int*>(
                reinterpret_cast<uintptr_t>(pLocal) + (unsigned)s_offActiveWeapon);
            void* pAW = SEH_EntFromHandle(hAW);
            if (pAW && SEH_VtableSane(pAW)) {
                // Same resolution the write pass uses, so a def index we already rewrote
                // still names the weapon it was rewritten FROM -- that weapon is the one
                // whose activity family the sequences have to be remapped out of.
                const int defIdx = SEH_ReadInt(pAW, s_offItemDefIndex);
                const WeaponTarget wt = ResolveWeaponTarget(pAW, defIdx);
                if (wt.cfg && wt.cfg->reskinDef > 0) {
                    if (const ReskinInfo* ri = Skin_FindReskinInfo(wt.cfg->reskinDef)) {
                        reskin   = wt.cfg->reskinDef;
                        baseKey  = wt.configKey;
                        from     = Skin_BaseAnimSlot(wt.configKey);
                        to       = ri->anim;
                        riInfo   = ri;
                        baseInfo = Skin_BaseAnimEntry(wt.configKey);
                    }
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    if (reskin != s_activeReskin || from != s_activeFrom || to != s_activeTo
        || baseKey != s_activeBaseKey) {
        s_activeReskin     = reskin;
        s_activeBaseKey    = baseKey;
        s_activeFrom       = from;
        s_activeTo         = to;
        s_activeReskinInfo = riInfo;
        s_activeBaseInfo   = baseInfo;

        // Nothing to remap when the reskin names no replacements and sits in the same family as
        // the weapon it replaces -- the overwhelming majority (a Batsaber on a bat).
        const char* bp = baseInfo ? baseInfo->animPfx : nullptr;
        const char* rp = riInfo   ? riInfo->animPfx   : nullptr;
        s_activeNeedsRemap = riInfo && ((riInfo->actRepl && riInfo->actReplCount > 0)
                                        || (bp && rp && _stricmp(bp, rp) != 0));
        ClearSeqRemap();
    }
}

// The DRAW is the one animation the SetSequence hook cannot catch in time. It arrives in the same
// snapshot that switches the weapon (RecvProxy_SequenceNum, baseviewmodel_shared.cpp:622, calls
// SetSequence during unpack), and the unpack runs BEFORE this frame's UpdateActiveRemap has seen
// the new active weapon -- so the hook was still holding the previous weapon's mapping, refused,
// and the reskin drew with the stock weapon's draw. Every later animation is fine, which is
// exactly the shape of the report ("draw animations are still broken").
//
// So the sequence is also swept here, after the state is current and before anything renders.
// Re-mapping an already-mapped index is a no-op (the reskin's own activities are never on the
// FROM side), so the two paths cannot fight.
//
// Called TWICE per stage-5: once before the original FrameStageNotify (which is where the
// sequence's anim events -- the draw SOUND -- are fired) and once after, see h_FrameStageNotify.
static void SweepViewmodelSequence()
{
    if (!s_activeNeedsRemap || !s_pLocalViewmodelEnt || s_offSequence <= 0) return;
    __try {
        const int cur = SEH_ReadInt(s_pLocalViewmodelEnt, s_offSequence);
        if (cur < 0 || cur >= 512) return;
        const int r = Skins_RemapSequence(s_pLocalViewmodelEnt, cur);
        if (r == cur) return;
        SEH_WriteInt(s_pLocalViewmodelEnt, s_offSequence, r);
        static int s_lastFrom = -1;
        if (cur != s_lastFrom) {
            s_lastFrom = cur;
            DebugLog("skins: anim sweep %d -> %d (the hook was too early for this one)", cur, r);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// The style skin (a Gold Frying Pan's gold is style 0's skin_red 2 on the ORDINARY pan model) is
// written to the weapon entity, which is the WORLD model. The thing in the player's own hands is
// a separate client-only entity, C_ViewmodelAttachmentModel, and its skin is copied once from
// CTFViewModel::GetSkin when the attachment is built (econ_entity.cpp:1196) -- for the GFP the
// model never changes (it IS the pan's model), so nothing rebuilds the attachment and it keeps
// the stock pan's skin forever. Hence: find it and write the skin on it directly.
//
// It is client-only, so it lives above the networked range, and it is identified by the reskin's
// own model index. The search is capped and cached; a hit is re-verified every frame by the same
// model index, so a rebuilt attachment is picked up again without another scan.
static void* s_pAttachEnt      = nullptr;
static int   s_attachScanTimer = 0;
static constexpr int k_maxEdicts     = 2048;   // MAX_EDICTS: everything above is client-only
static constexpr int k_attachScanTop = 2048 + 1024;

static void SweepAttachmentSkin(int teamNum)
{
    if (s_offSkin <= 0 || g_offModelIndex <= 0) return;
    const ReskinInfo* ri = s_activeReskinInfo;
    if (!ri || ri->styleCount == 0) { s_pAttachEnt = nullptr; return; }

    int skin = -1;
    if (const SkinCfg* cfg = ActiveWeaponCfg(nullptr, nullptr)) skin = ReskinSkinFor(*cfg, teamNum);
    if (skin < 0) return;

    const int wantIdx = GetReskinViewmodelIdx(ri->defindex);
    if (wantIdx == 0) return;

    __try {
        if (s_pAttachEnt) {
            const int mi = SEH_ReadInt(s_pAttachEnt, g_offModelIndex) & 0xFFFF;
            if (mi != (wantIdx & 0xFFFF)) s_pAttachEnt = nullptr;
        }
        if (!s_pAttachEnt) {
            if (--s_attachScanTimer > 0) return;
            s_attachScanTimer = 30;                 // ~2 scans a second while it is missing
            for (int i = k_maxEdicts; i < k_attachScanTop; ++i) {
                void* p = GetClientEntity(i);
                if (!p || !SEH_VtableSane(p)) continue;
                if ((SEH_ReadInt(p, g_offModelIndex) & 0xFFFF) != (wantIdx & 0xFFFF)) continue;
                s_pAttachEnt = p;
                DebugLog("skins: viewmodel attachment for def=%d found at client entity %d "
                         "(model idx %d)", ri->defindex, i, wantIdx);
                break;
            }
            if (!s_pAttachEnt) return;
        }
        if (SEH_ReadInt(s_pAttachEnt, s_offSkin) != skin)
            SEH_WriteInt(s_pAttachEnt, s_offSkin, skin);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// ==== festivizer lights =========================================================================
// The lights are attachment models, and CEconEntity::UpdateAttachmentModels (econ_entity.cpp:1079)
// is the only thing that builds them: it runs from OnDataChanged and reads is_festivized (attr
// 2053) off the attribute list AT THAT MOMENT. Toggling the box on is not a full update (SC_Fest),
// so the weapon's CREATED rebuild is long gone and only a networked change rebuilds the list: a minigun,
// a medigun or a sniper rifle churns netvars every few frames and lit up, a melee, a sapper or an
// unfired shotgun sent nothing and stayed dark until the next weapon switch. That was "festivizer
// works on some weapons".
//
// So the held weapon goes through its own data-changed pair once, after the attributes are on it.
// Pre first so every "old" copy (m_iOldState, m_bOldResetParity, m_iOldTeam) equals the current
// value and nothing reads it as a state change -- the same no-op delta a quiet snapshot is.
// Stage 5 only: our half runs AFTER the original's ProcessOnDataChangedEvents, so a weapon created
// this frame has already had its CREATED pass (InitializeAttributes, the attribute manager's outer)
// before we touch it. Holstered weapons need nothing: AttachmentModelsShouldBeVisible is false for
// them, and the deploy snapshot (m_iState -> active) rebuilds them with 2053 already present.
static void* s_festRefreshedWpn = nullptr;   // the weapon instance already rebuilt, null = none

__declspec(noinline) static void RefreshAttachmentModels(unsigned int hWeapon)
{
    __try {
        // IClientEntityList::GetClientNetworkable (slot 0): the networkable sub-object itself, so
        // no assumption about where it sits inside C_BaseEntity.
        void* pNet = VCall<void*>(g_entityList, 0, (int)(hWeapon & 0x3FFF));
        if (!pNet || !SEH_VtableSane(pNet)) return;
        void** vt = *reinterpret_cast<void***>(pNet);
        using Fn = void(__fastcall*)(void*, int);
        ((Fn)vt[4])(pNet, 1);   // OnPreDataChanged(DATA_UPDATE_DATATABLE_CHANGED)
        ((Fn)vt[5])(pNet, 1);   // OnDataChanged -> UpdateAttachmentModels
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DebugLog("skins: SEH in festivizer refresh");
    }
}

// Every edit since the last call, from any widget or a config load, folded into SkinChange bits.
// One place decides what an edit costs, so no menu widget can forget to ask for its update (wear,
// pattern, unusual, stat clock and the spells never did) or ask for one it does not need. Engine
// thread only; the menu only ever mutates values of keys that already exist (Skins_Install).
static std::unordered_map<int, SkinCfg> s_seenCfgs;

static unsigned DiffSkinCfgs()
{
    const bool first = s_seenCfgs.empty();
    unsigned r = SC_None;
    for (const auto& [key, c] : g_skinCfgs) {
        SkinCfg& seen = s_seenCfgs[key];
        if (!first) r |= SkinPure_Classify(seen, c);
        seen = c;
    }
    return r;
}

static void __fastcall h_FrameStageNotify(int stage)
{
    MiscHookGuard _g;

    // THE DRAW SOUND IS AN ANIM EVENT, so the sweep has to beat the ORIGINAL of stage 5, not
    // just the frame's draw. CHLClient::FrameStageNotify(FRAME_RENDER_START) calls OnRenderStart,
    // which runs ProcessOnDataChangedEvents and then SimulateEntities -- and SimulateEntities is
    // C_BaseAnimating::DoAnimationEvents, the thing that fires the sequence's own events
    // (cdll_client_int.cpp:2194/2207, c_baseanimating.cpp:5164). In c_spy_animations.mdl the
    // knife_draw sequence carries AE_CL_PLAYSOUND Weapon_Knife.Draw and melee_allclass_draw
    // carries Weapon_Machete.Draw, so sweeping after the original was in time for the picture
    // and one call too late for the sound: the Saxxy drew right and still opened a knife.
    // Prediction is NOT inside OnRenderStart -- it has already run by this point -- so nothing
    // rewrites m_nSequence between here and the draw.
    // lua: eject closes every state here (game thread, eject waits on this hook); else stage 0 = the
    // once-a-frame tick (requests, buttons, saves), then frame_stage before the game runs the stage
    if (g_eject) g_lua.shutdown();
    else
    {
        CrashSpan _s("fsn/lua");
        if (stage == 0) g_lua.on_tick();
        g_lua.on_frame_stage(stage);
    }
    if (stage == 5 && !g_eject && g_enableSkins) { SweepViewmodelSequence(); RestampStreak("before think"); }
    // Hats are built here, before the original: its OnRenderStart runs ProcessOnDataChangedEvents,
    // which is what delivers the OnDataChanged(CREATED) a new hat's PreDataUpdate queued.
    { CrashSpan _s("fsn/hats"); Hats_FrameStage(stage); }
    if (stage == 5) { CrashSpan _s("fsn/skinsinv"); SkinsInv_Tick(!g_eject && g_enableSkins && g_skinsBackpack); }
    // Weather entity, same reason and same spot as the hats (visuals.cpp Rain_Tick).
    if (stage == 5) { CrashSpan _s("fsn/rain"); Rain_Tick(); }

    o_FrameStageNotify(stage);
    { CrashSpan _s("fsn/extras"); GameExtras_Frame(stage); }   // chat translator print, bot ping, performance, cvar list (restores itself on g_eject)
    // Skin sharing: steam p2p probes / receive, player list. Game thread, in or out of a match
    // (it closes its sessions itself on leave and on eject).
    if (stage == 5) { CrashSpan _s("fsn/share"); SkinShare_Tick(); }
    // The killstreak color table is a write into the game's own data, so it has to go back
    // before this hook stops running -- nothing else would ever undo it. Same for flip world's
    // cl_flipviewmodels: its SetValue sends userinfo, so it goes back from this thread (eject
    // waits for it, thread.cpp).
    if (g_eject) { KsColorRestoreAll(); FlipWorld_Release(); return; }

    // Flip world's hold/release also has to run out of a match, where CreateMove does not.
    if (stage == 5) { FlipWorld_Tick(); FlipWorld_RenderStart(); }   // RenderView follows: W2S stays true for the 3D pass

    // Web on surface (botox frame_stage_notify FRAME_START): main thread every frame, main menu
    // included -- the browser's engine side needs exactly that (the post slot needs a world).
    if (stage == 0) { CrashSpan _s("fsn/web"); Web_MainTick(); }

    if (!g_engine || !g_entityList) return;

    // Fake movement: demo playback gets its jump anim/sound off the recorded velocity.
    // Stage 3 = net update end, the demo tick's data is in.
    if (stage == 3) FakeMove_DemoTick();

    // Movement recorder camera (botox on_frame_stage FRAME_START): per frame, so the replayed
    // view eases at fps instead of stepping at tick rate.
    if (stage == 0) { Recorder_FrameStart(); EdgeBug_FrameStart(); }   // + hotwheels eb view

    // Glow RTs + materials once for a mid-map inject (map loads get flipworld.cpp LevelInit). Main
    // thread, before the frame renders: off-thread they raced stdshader draws (thread.cpp note).
    static bool s_glowInjectInit = false;
    if (stage == 0 && !s_glowInjectInit && IsInGame()) { s_glowInjectInit = true; Glow_LevelInit(); }

    // No hats reads this back in DrawModelExecute; entities live on this thread.
    if (stage == 5 && IsInGame()) NoHats_BodyTick();
    // hl2 dissolve death particles: DissolveEffect makes a client entity, game thread only.
    if (stage == 5 && IsInGame()) KillEffects_FrameStart();

    // stage 2: the snapshot has arrived and nothing has been unpacked yet.
    if (stage == 2) {
        CrashSpan _s("fsn/skins-pre");
        if ((g_enableSkins || PeerSkinsOn()) && s_preWriteCallSite != 0 && s_entityChangeAge >= 9)
            PreWriteDefIndexes();
        return;
    }
    if (stage != 3 && stage != 5) return;
    CrashSpan _spanSkins("fsn/skins");   // the rest of this function: local + peer weapon passes

    static bool s_wasInGame = false;
    const bool bInGame = IsInGame();

    if (s_wasInGame && !bInGame) {
        // Leaving a server invalidates every cached pointer and both model tables.
        s_pLocalViewmodelEnt  = nullptr;
        s_lastLocalEnt        = nullptr;
        s_entityChangeAge     = 0;
        s_activeReskin        = 0;
        s_activeBaseKey       = 0;
        s_activeFrom          = AS_DEFAULT;
        s_activeTo            = AS_DEFAULT;
        s_activeReskinInfo    = nullptr;
        s_activeBaseInfo      = nullptr;
        s_activeNeedsRemap    = false;
        // A demo's streak outlives the demo: the next one carries it on (demostreak.h).
        if (!s_demo.active) ResetStreak("left server");
        memset(s_goldPending, 0, sizeof(s_goldPending));
        s_festRefreshedWpn    = nullptr;
        s_fullUpdateTick      = 0;   // a join is not our full update -- keep its age gate
        ClearSeqRemap();
        // the next server's handles can repeat these; its weapons are new entities anyway
        memset(s_peerBuilt, 0, sizeof(s_peerBuilt));
        s_peerTouched = false;
    }
    s_wasInGame = bInGame;
    // Diffed out of a game too, so edits made in the main menu are not taken for fresh ones on
    // join -- the weapons are built with them then anyway.
    const unsigned chg = DiffSkinCfgs();
    if (!bInGame) return;

    const bool skinEnabling  = g_enableSkins && !s_skinWasEnabled;
    const bool skinDisabling = !g_enableSkins && s_skinWasEnabled;
    s_skinWasEnabled = g_enableSkins;

    // A click (combo pick, checkbox) fires its full update this frame. A typed or dragged field
    // waits until it has been still for 350 ms, so a slider drag or a pattern seed is one update,
    // not one per frame / keystroke. And never two within 250 ms: the first one's snapshot has not
    // even arrived yet (debug.log 10-30 had four back to back). Live edits (sheen, kill effect,
    // clock count, spells on, custom color) need none -- Skin_Apply below writes them this frame.
    static bool  s_updPending = false;
    static DWORD s_updDue     = 0;
    // A peer's loadout arriving / changing needs NO full update: ApplyPeerWeapons rebuilds that
    // peer's weapons in place. Only "show shared" going off does -- the def index and attributes
    // we wrote on their weapons have nothing else to undo them.
    static bool s_peerWasOn = false;
    const bool  peerOn      = PeerSkinsOn();
    const bool  peerChanged = !peerOn && s_peerWasOn && s_peerTouched;
    if (peerChanged) s_peerTouched = false;
    if (!peerOn) memset(s_peerBuilt, 0, sizeof(s_peerBuilt));   // back on = rebuild what is in
    s_peerWasOn = peerOn;
    const bool updReq = skinEnabling || skinDisabling || peerChanged
                     || (g_enableSkins && (g_skinNeedsUpdate || (chg & SC_Rebuild)));
    g_skinNeedsUpdate = false;
    const DWORD now = GetTickCount();
    if (updReq) {
        const DWORD due = now + ((chg & SC_Typed) ? 350 : 0);
        if (!s_updPending || (int)(due - s_updDue) > 0) s_updDue = due;
        s_updPending = true;
    }
    if (s_updPending && (int)(now - s_updDue) >= 0 && now - s_fullUpdateTick >= 250) {
        s_updPending = false;
        TriggerForceFullUpdate("skin changed");
    }
    // Festivizer switched on: Skin_Apply writes 2053 below, then the held weapon's lights get
    // rebuilt by the refresh at the bottom -- same frame, no full update.
    if (g_enableSkins && (chg & SC_Fest)) s_festRefreshedWpn = nullptr;

    if (!g_enableSkins && !skinDisabling && !peerOn) return;

    const int localIdx = GetLocalPlayerIndex();
    if (localIdx < 1) return;
    void* pLocal = GetClientEntity(localIdx);
    if (!pLocal) return;

    ResolveSkinSigs();
    ResolveSkinOffsets();
    RebuildReverseMap();

    if (skinDisabling) KsColorRestoreAll();

    if (updReq) {
        s_activeReskin        = 0;
        s_activeBaseKey       = 0;
        s_activeFrom          = AS_DEFAULT;
        s_activeTo            = AS_DEFAULT;
        s_activeReskinInfo    = nullptr;
        s_activeBaseInfo      = nullptr;
        s_activeNeedsRemap    = false;
        ClearSeqRemap();
        if (g_enableSkins)
            for (auto& [id, cfg] : g_skinCfgs)
                if (cfg.enabled && cfg.reskinDef > 0) GetReskinViewmodelIdx(cfg.reskinDef);
        // The full update can hand the re-created weapon the very same address back.
        s_festRefreshedWpn = nullptr;
    }

    if (pLocal != s_lastLocalEnt) {
        DebugLog("skins: local entity changed to %p", pLocal);
        s_lastLocalEnt        = pLocal;
        s_entityChangeAge     = 0;
        s_activeReskin        = 0;
        s_activeBaseKey       = 0;
        s_activeFrom          = AS_DEFAULT;
        s_activeTo            = AS_DEFAULT;
        s_activeReskinInfo    = nullptr;
        s_activeBaseInfo      = nullptr;
        s_activeNeedsRemap    = false;
        s_festRefreshedWpn    = nullptr;
        ClearSeqRemap();
        // Our own cl_fullupdate re-creates the local player too, and this is stage 3 of that
        // full snapshot: every weapon is unpacked and its OnDataChanged(CREATED) -- the one pass
        // that builds the paint material, the clock and the festivizer lights -- runs in THIS
        // frame's stage 5. Waiting out the gate let that pass run with no attributes on the list,
        // so a generic warpaint, a stat clock or the lights showed on some updates and not others.
        // Applying now is what a respawn already does (stage 3, before CREATED); a null attribute
        // manager is fine, SetRuntimeAttributeValue checks it (econ_item_view.cpp:1910).
        if (GetTickCount() - s_fullUpdateTick > 5000) return;   // joined a server: keep the gate
        s_entityChangeAge = 9;
    }

    UpdateViewmodelEnt(pLocal);

    // The first frames after a respawn hand out weapon entities that are still being filled in.
    if (s_entityChangeAge < 9) { ++s_entityChangeAge; return; }
    if (!g_enableSkins) { ApplyPeerWeapons(false, stage == 5); return; }   // own off, peers still shown

    UpdateActiveRemap(pLocal);
    // Second sweep of the same frame, and the only one on the frame a reskin is first switched
    // on: the pre-original sweep above runs off the state THIS call is about to refresh, so it
    // is one frame behind whenever that state has just changed. Re-mapping an already-mapped
    // index is a no-op, so the two cannot fight.
    if (stage == 5) SweepViewmodelSequence();
    SweepAttachmentSkin((s_offTeamNum > 0) ? SEH_ReadInt(pLocal, s_offTeamNum) : 0);
    // Re-stamp the streak every update: both arrays are networked, so the server's own count
    // (zero, on a server that never issued the item) lands on top of ours in every snapshot.
    // The value is the total over every killstreak weapon, held or not -- see StreakTotal.
    if (AnyKillstreakWanted()) {
        if (stage == 5) RestampStreak("inside render start");   // log only: stamped again below
        int heldDef = 0, heldKey = 0;
        const SkinCfg* held = ActiveWeaponCfg(&heldDef, &heldKey);
        s_killStreak = StreakTotal();
        KsColorUpdate(held);
        // KsKickedStreak, not the raw count: a bumped count is what forces the eye glow to
        // re-read a recolored row.
        ApplyKillStreak(localIdx, KsKickedStreak(s_killStreak));
    } else {
        KsColorRestoreAll();
    }
    SkinsDiag(pLocal);

    if (s_offMyWeapons <= 0 || s_offItemDefIndex <= 0 || s_offAttrList <= 0) return;

    ApplyPeerWeapons(false, stage == 5);

    const int teamNum =(s_offTeamNum > 0) ? SEH_ReadInt(pLocal, s_offTeamNum) : 0;
    const unsigned int hActive = (s_offActiveWeapon > 0) ? SEH_ReadHandle(pLocal, s_offActiveWeapon, 0) : 0;

    for (int i = 0; i < 48; i++) {
        const unsigned int hWeapon = SEH_ReadHandle(pLocal, s_offMyWeapons, i);
        if (hWeapon == 0xFFFFFFFF || hWeapon == 0) continue;
        void* pWeapon = SEH_EntFromHandle(hWeapon);
        if (!pWeapon || !SEH_VtableSane(pWeapon)) continue;

        const int origDefIdx = SEH_ReadInt(pWeapon, s_offItemDefIndex);
        const WeaponTarget wt = ResolveWeaponTarget(pWeapon, origDefIdx);
        if (!wt.cfg) continue;

        if (wt.targetDef > 0 && wt.targetDef != origDefIdx)
            SEH_WriteInt(pWeapon, s_offItemDefIndex, wt.targetDef);
        if (wt.cfg->reskinDef > 0) {
            // The world model's skin is set from the item view in OnDataChanged, which only
            // runs on a data change -- stamp it every frame so a style pair like the Gold
            // Frying Pan's 2/3 survives a team switch or a late full update.
            const int skin = ReskinSkinFor(*wt.cfg, teamNum);
            if (skin >= 0 && s_offSkin > 0) SEH_WriteInt(pWeapon, s_offSkin, skin);
        }

        Skin_Apply(pWeapon, wt.configKey);

        // Festivizer lights: rebuild the held weapon's attachment list once, now that 2053 is
        // on it. See RefreshAttachmentModels for why nothing else would.
        if (stage == 5 && wt.cfg->festivized && hWeapon == hActive
            && pWeapon != s_festRefreshedWpn && s_offWeaponState > 0
            && SEH_ReadInt(pWeapon, s_offWeaponState) == 2 /* WEAPON_IS_ACTIVE */) {
            s_festRefreshedWpn = pWeapon;
            RefreshAttachmentModels(hWeapon);
            DebugLog("skins: festivizer refresh def=%d key=%d", origDefIdx, wt.configKey);
        }
    }
}

// ==== halloween spells ==========================================================================
// Pumpkin bombs and Halloween fire are read out of the attribute list only after the client
// asks TF_IsHolidayActive(kHoliday_HalloweenOrFullMoon). Off-season that returns false and the
// attribute is never looked at, so the two spell checkboxes would do nothing on a normal map.
// Spoofing it is what makes the spells visible year round, and it is spoofed ONLY while a
// spell is actually selected on some weapon.
static constexpr int k_holidayHalloween = 9;

static bool __fastcall h_TF_IsHolidayActive(int eHoliday)
{
    MiscHookGuard _g;
    if (!g_eject && eHoliday == k_holidayHalloween && g_enableSkins)
        for (const auto& [id, c] : g_skinCfgs)
            if (c.enabled && c.spellFlags) return true;
    return o_TF_IsHolidayActive(eHoliday);
}

// ==== player_death: exorcism ghosts + killfeed icon =============================================
// The config that applies to the weapon in the local player's hands right now, or null.
__declspec(noinline) static const SkinCfg* ActiveWeaponCfg(int* outDefIdx, int* outConfigKey)
{
    const SkinCfg* out = nullptr;
    __try {
        if (s_offActiveWeapon <= 0 || s_offItemDefIndex <= 0) return nullptr;
        const int localIdx = GetLocalPlayerIndex();
        if (localIdx < 1) return nullptr;
        void* pLocal = GetClientEntity(localIdx);
        if (!pLocal) return nullptr;

        const unsigned int hActive = *reinterpret_cast<unsigned int*>(
            reinterpret_cast<uintptr_t>(pLocal) + (unsigned)s_offActiveWeapon);
        void* pWpn = SEH_EntFromHandle(hActive);
        if (!pWpn || !SEH_VtableSane(pWpn)) return nullptr;

        const int def = SEH_ReadInt(pWpn, s_offItemDefIndex);
        if (outDefIdx) *outDefIdx = def;
        const WeaponTarget wt = ResolveWeaponTarget(pWpn, def);
        if (outConfigKey) *outConfigKey = wt.configKey;
        out = wt.cfg;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return out;
}

__declspec(noinline) static void DispatchExorcismGhosts(int victimIdx)
{
    __try {
        if (!s_fnDispatchEffect || !s_fnGetParticleIndex) return;
        if (g_offOrigin <= 0 || s_offTeamNum <= 0) return;
        void* pVictim = GetClientEntity(victimIdx);
        if (!pVictim) return;

        const float* pos = reinterpret_cast<const float*>(
            reinterpret_cast<uintptr_t>(pVictim) + g_offOrigin);
        const int teamNum = *reinterpret_cast<const int*>(
            reinterpret_cast<uintptr_t>(pVictim) + s_offTeamNum);

        const int pidx = s_fnGetParticleIndex(
            (teamNum == 3) ? "halloween_player_death_blue" : "halloween_player_death");
        if (pidx <= 0) return;

        CEffectData_Weather data = {};
        data.m_vOrigin[0] = pos[0];
        data.m_vOrigin[1] = pos[1];
        data.m_vOrigin[2] = pos[2] + 32.f;   // roughly chest height on the corpse
        data.m_nHitBox    = pidx;
        data.m_flScale    = 1.f;
        s_fnDispatchEffect("ParticleEffect", data);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Does this weapon carry the vanilla halloween_death_ghosts attribute already? A real spelled
// weapon still shows nothing off a Halloween server, so it gets the same manual dispatch.
__declspec(noinline) static bool WeaponHasExorcismAttr()
{
    bool found = false;
    __try {
        if (s_offActiveWeapon <= 0 || s_offAttrList <= 0) return false;
        const int localIdx = GetLocalPlayerIndex();
        if (localIdx < 1) return false;
        void* pLocal = GetClientEntity(localIdx);
        if (!pLocal) return false;
        const unsigned int hActive = *reinterpret_cast<unsigned int*>(
            reinterpret_cast<uintptr_t>(pLocal) + (unsigned)s_offActiveWeapon);
        void* pWpn = SEH_EntFromHandle(hActive);
        if (!pWpn || !SEH_VtableSane(pWpn)) return false;

        const CAttributeList* pList = reinterpret_cast<const CAttributeList*>(
            reinterpret_cast<uintptr_t>(pWpn) + s_offAttrList);
        found = FindAttr(pList, 1009) != nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return found;
}

// ==== killstreak count ==========================================================================
// CTFPlayerResource, found by class name off the entity list. IClientEntityList slot 0 is
// GetClientNetworkable and IClientNetworkable slot 2 is GetClientClass, both counted off the
// interface declarations in src/hacks/aim/defs.h (that file's early slots match the ones
// GetClientEntity=3 already uses here). Cached, and re-found whenever the pointer goes stale.
__declspec(noinline) static void* FindPlayerResource()
{
    static void* s_cached  = nullptr;
    static int   s_lastIdx = 0;

    __try {
        if (s_cached && s_lastIdx > 0 && GetClientEntity(s_lastIdx) == s_cached) return s_cached;
        s_cached = nullptr; s_lastIdx = 0;
        if (!g_entityList) return nullptr;

        void** listVtbl = *reinterpret_cast<void***>(g_entityList);
        if (reinterpret_cast<uintptr_t>(listVtbl) < 0x10000) return nullptr;
        using GetNetworkable_t = void*(__fastcall*)(void*, int);
        using GetHighest_t     = int(__fastcall*)(void*);
        const int highest = ((GetHighest_t)listVtbl[6])(g_entityList);
        const int cap     = (highest > 0 && highest < 2048) ? highest : 2048;

        // One walk, then cached. There is exactly one of these per server.
        for (int i = 1; i <= cap; ++i) {
            void* pNet = ((GetNetworkable_t)listVtbl[0])(g_entityList, i);
            if (!pNet || !SEH_VtableSane(pNet)) continue;
            void** vtbl = *reinterpret_cast<void***>(pNet);
            using GetClass_t = ClientClass*(__fastcall*)(void*);
            ClientClass* cc = ((GetClass_t)vtbl[2])(pNet);
            if (!cc || !cc->m_pNetworkName) continue;
            if (strcmp(cc->m_pNetworkName, "CTFPlayerResource") != 0) continue;
            s_cached  = GetClientEntity(i);
            s_lastIdx = i;
            return s_cached;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { s_cached = nullptr; s_lastIdx = 0; }
    return nullptr;
}

// Write the streak into both tables. `type` indexes m_nStreaks directly; the resource array is
// one row of kTFStreak_COUNT ints per player.
__declspec(noinline) static void ApplyKillStreak(int localIdx, int count)
{
    __try {
        if (localIdx < 1) return;
        s_stampedStreak = count;
        if (s_offNStreaks > 0) {
            if (void* pLocal = GetClientEntity(localIdx)) {
                SEH_WriteInt(pLocal, s_offNStreaks + kTFStreak_Kills    * (int)sizeof(int), count);
                SEH_WriteInt(pLocal, s_offNStreaks + kTFStreak_KillsAll * (int)sizeof(int), count);
            }
        }
        if (s_offIStreaks > 0) {
            if (void* pRes = FindPlayerResource()) {
                const int row = localIdx * kTFStreak_COUNT;
                SEH_WriteInt(pRes, s_offIStreaks + (row + kTFStreak_Kills)    * (int)sizeof(int), count);
                SEH_WriteInt(pRes, s_offIStreaks + (row + kTFStreak_KillsAll) * (int)sizeof(int), count);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// The HUD killstreak meter reads m_nStreaks in C_TFPlayer::ClientThink, which runs INSIDE the
// stage-5 original (OnRenderStart -> SimulateEntities, cdll_client_int.cpp:2005). The regular
// stamps run after the stage-3 and stage-5 originals, so a 0 landing between them reached the
// HUD for a frame -- the flicker on weapon switch. Put the last value back before the think, and
// log which side of the original it was caught on so a remaining flicker can be traced.
__declspec(noinline) static void RestampStreak(const char* where)
{
    __try {
        if (!AnyKillstreakWanted() || s_offNStreaks <= 0 || s_entityChangeAge < 9) return;
        if (!g_engine || !g_entityList || !IsInGame()) return;
        const int localIdx = GetLocalPlayerIndex();
        void* pLocal = (localIdx >= 1) ? GetClientEntity(localIdx) : nullptr;
        if (!pLocal || pLocal != s_lastLocalEnt) return;
        const int cur = SEH_ReadInt(pLocal, s_offNStreaks + kTFStreak_Kills * (int)sizeof(int));
        if (cur == s_stampedStreak) return;
        static DWORD s_lastLog = 0;
        const DWORD now = GetTickCount();
        if (now - s_lastLog > 1000) {
            s_lastLog = now;
            DebugLog("skins: streak overwritten %d -> %d (%s), restamped", s_stampedStreak, cur, where);
        }
        ApplyKillStreak(localIdx, s_stampedStreak);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Is any enabled config asking for a killstreak at all? The counter is a lie about a networked
// value, so it is only told while something on screen depends on it. Recomputed once per
// snapshot in RebuildReverseMap, which already walks the whole map -- this is read twice a
// frame and the map has one entry per weapon.
static bool AnyKillstreakWanted() { return s_anyKillstreak; }

// THE STREAK IS KEPT PER WEAPON, keyed by config key -- the same key the menu edits (0 = a real
// killstreak weapon with no config). A kill with no tier, ours or the server's, raises nothing.
// The kill notice shows a weapon's own entry; everything else shows the SUM. The count written into the
// networked arrays is the SUM over every weapon, whatever is held -- the same thing the server
// keeps in kTFStreak_Kills (tf_gamerules.cpp:13159), so the HUD count adds up across weapons and
// a streak never drops to 0 on weapon switch. Its own function because the map's destructor
// would stop Skins_OnGameEvent from using __try at all (C2712).
static std::unordered_map<int, int>& StreakByKey()
{
    static std::unordered_map<int, int> s_byKey;
    return s_byKey;
}

static int StreakTotal()
{
    int total = 0;
    for (const auto& [key, n] : StreakByKey()) total += n;
    return total;
}

static int BumpStreak(int configKey) { return ++StreakByKey()[configKey]; }

// IVEngineClient 51 GetLevelName, 76 IsPlayingDemo, 80 GetDemoPlaybackTick, 83
// GetDemoPlaybackTotalTicks (cdll_int.h:414-421; 76 is flipmath.h engIsPlayingDemo, 51 is
// psassist.inl EngineLevelName). Split from DemoStreakSync so the map copy never sits in a
// function with __try (C2712).
struct DemoNow { bool playing; const char* level; int length; int tick; };
__declspec(noinline) static DemoNow ReadDemoNow()
{
    DemoNow n{ false, nullptr, 0, 0 };
    __try {
        if (g_engine && VCall<bool>(g_engine, 76)) {
            n.level   = VCall<const char*>(g_engine, 51);
            n.length  = VCall<int>(g_engine, 83);
            n.tick    = VCall<int>(g_engine, 80);
            n.playing = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { n.playing = false; }
    return n;
}

// Before every change to the streak. Carries it into a new demo and puts it back on a rewind.
static ds::Load DemoStreakSync()
{
    const DemoNow n = ReadDemoNow();
    const ds::Load l = ds::Sync(s_demo, n.playing, n.level, n.length, n.tick, StreakByKey());
    if (l == ds::Load::NewDemo)
        DebugLog("skins: streak %d carried into demo %s (%d ticks)", StreakTotal(), s_demo.level, n.length);
    else if (l == ds::Load::Rewind)
        DebugLog("skins: demo rewound to tick %d, streak back to %d", n.tick, StreakTotal());
    return l;
}

// One place a streak ends, so the log can say WHICH end it was. Before this the map was never
// cleared at all (the kill notice's "N with this weapon" climbed across lives and rounds) and
// only two events reset the count: our own spawn and our own death.
// In a demo only the recorded player's death ends it; a demo load fires the rest.
static void ResetStreak(const char* why, bool death)
{
    if (!ds::ResetEnds(DemoStreakSync(), death)) {
        if (StreakTotal() > 0 || g_debugLogEnable)
            DebugLog("skins: streak %d kept (%s, demo)", StreakTotal(), why);
        return;
    }
    const bool had = (s_killStreak != 0) || !StreakByKey().empty();
    s_killStreak = 0;
    StreakByKey().clear();
    const int localIdx = GetLocalPlayerIndex();
    if (localIdx >= 1) ApplyKillStreak(localIdx, 0);
    if (had || g_debugLogEnable) DebugLog("skins: streak reset (%s)", why);
}

// ==== killstreak custom color ===================================================================
// Both halves of a killstreak's color come out of ONE static table in client.dll,
// g_KillStreakEffectsBase (c_tf_player.cpp:336):
//   sheen -- CProxyAnimatedWeaponSheen::OnBind copies that row's m_sheen_rgba straight into
//            $sheenmaptint on every material bind (c_tf_player.cpp:2883).
//   eyes  -- C_TFPlayer::UpdateKillStreakEffects reads m_color1/m_color2 out of the SAME row
//            through GetVectorColorForParticleSystem into m_vEyeGlowColor1/2
//            (c_tf_player.cpp:10440), and CTFPlayerShared::ClientKillStreakBuffThink pushes
//            those into control point 9 (CUSTOM_COLOR_CP1) of the two eyeglow particles every
//            think (tf_player_shared.cpp:6536). The particle art is white; CP1 IS the color.
// So one rewritten row recolors both, and neither the sheen proxy nor the particle system has
// to be hooked. The array is dynamically initialised (its ctor divides each byte by 255), so it
// lives in .data and is already writable.
//
// Row = killstreak_params_t, 64 bytes on x64:
//   +0  int m_iShaderIndex   +4 bool m_bHasTeamColor   +8 const char* m_pTexture
//   +16 float sheen rgba     +32 float color1 rgba     +48 float color2 rgba
// g_KillStreakEffectsBlue is the 2-row array directly behind it (verified in the shipped
// client.dll: rows 0..7 base, 8 empty, 9 blue). Row 1 (team shine) is the one row with
// m_bHasTeamColor set, and a BLU player reads the blue copy of it instead.
static constexpr int k_ksRowSize  = 64;
static constexpr int k_ksRows     = 10;   // the 8 base rows and then the 2 blue ones
static constexpr int k_ksBaseRows = 8;
static constexpr int k_ksBlueRow1 = 9;    // g_KillStreakEffectsBlue[1], the team shine copy

// Rows 1 and 2 as the SDK writes them -- the signature the table is found by. Compared with a
// tolerance because each value is a runtime division whose last bit depends on the compiler.
static const float k_ksRow1[12] = {
    200.f/255.f,  20.f/255.f,  15.f/255.f, 1.f,
    255.f/255.f, 118.f/255.f, 118.f/255.f, 1.f,
    255.f/255.f,  35.f/255.f,  28.f/255.f, 1.f };
static const float k_ksRow2[12] = {
    242.f/255.f, 172.f/255.f,  10.f/255.f, 1.f,
    255.f/255.f, 237.f/255.f, 138.f/255.f, 1.f,
    255.f/255.f, 213.f/255.f,  65.f/255.f, 1.f };
static const float k_ksRowBlue[12] = {
     40.f/255.f,  98.f/255.f, 200.f/255.f, 1.f,
      0.f/255.f,  92.f/255.f, 255.f/255.f, 1.f,
    134.f/255.f, 203.f/255.f, 243.f/255.f, 1.f };

static uint8_t* s_ksTable    = nullptr;        // &g_KillStreakEffectsBase[0]
static bool     s_ksSearched = false;
static bool     s_ksBlueOk   = false;          // the blue array really is behind the base one
static float    s_ksOrig[k_ksRows][12] = {};   // shipped colors, kept for the restore
static int      s_ksPatched  = -1;             // row currently holding a custom color
static float    s_ksPatchedColor[3] = {};
static int      s_ksGlowKick = 0;              // frames of a bumped streak -- see KsColorUpdate

static bool KsRowMatch(const uint8_t* p, const float* want)
{
    const float* f = reinterpret_cast<const float*>(p);
    for (int i = 0; i < 12; ++i) {
        const float d = f[i] - want[i];
        if (!(d > -0.002f && d < 0.002f)) return false;   // false on NaN too
    }
    return true;
}

// Found by its own contents rather than by a code signature: the table is data, so no pattern
// scan over .text can reach it and nothing in it moves between builds except its address.
__declspec(noinline) static void KsFindTable()
{
    if (s_ksSearched) return;
    HMODULE hClient = GetModuleHandleA("client.dll");
    if (!hClient) return;
    s_ksSearched = true;

    __try {
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(hClient);
        auto* nt  = reinterpret_cast<IMAGE_NT_HEADERS*>(
                        reinterpret_cast<uint8_t*>(hClient) + dos->e_lfanew);
        IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < (int)nt->FileHeader.NumberOfSections; ++i) {
            if (!(sec[i].Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
            uint8_t* start = reinterpret_cast<uint8_t*>(hClient) + sec[i].VirtualAddress;
            const size_t size = (size_t)sec[i].Misc.VirtualSize;
            if (size < (size_t)k_ksRowSize * k_ksRows) continue;

            // p walks candidate row 1s; row 0 (the empty one) sits one row in front of it and
            // the blue pair k_ksBlueRow1 rows behind, so both ends have to stay in section.
            uint8_t* last = start + size - (size_t)k_ksRowSize * (k_ksRows - 1);
            for (uint8_t* p = start + k_ksRowSize; p <= last; p += 8) {
                if (!KsRowMatch(p + 16, k_ksRow1)) continue;
                if (!KsRowMatch(p + k_ksRowSize + 16, k_ksRow2)) continue;

                s_ksTable = p - k_ksRowSize;
                for (int r = 0; r < k_ksRows; ++r)
                    memcpy(s_ksOrig[r], s_ksTable + r * k_ksRowSize + 16, sizeof(s_ksOrig[r]));
                s_ksBlueOk = KsRowMatch(s_ksTable + k_ksBlueRow1 * k_ksRowSize + 16, k_ksRowBlue);
                DebugLog("skins: killstreak color table at %p (client+%IX) blue=%d",
                         (void*)s_ksTable,
                         (uintptr_t)(s_ksTable - reinterpret_cast<uint8_t*>(hClient)),
                         s_ksBlueOk ? 1 : 0);
                return;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    DebugLog("skins: killstreak color table NOT FOUND -- custom killstreak color does nothing "
             "this session, the stock sheen and eye colors stay");
}

// Only the three color channels are written; the alpha of each group is left as shipped (the
// proxy overwrites the sheen alpha in first person anyway, tf_sheen_alpha_firstperson).
__declspec(noinline) static void KsWriteRow(int row, const float rgb[3])
{
    if (!s_ksTable || row < 0 || row >= k_ksRows) return;
    __try {
        float* f = reinterpret_cast<float*>(s_ksTable + row * k_ksRowSize + 16);
        for (int grp = 0; grp < 3; ++grp) {          // sheen, color1, color2
            f[grp * 4 + 0] = rgb[0];
            f[grp * 4 + 1] = rgb[1];
            f[grp * 4 + 2] = rgb[2];
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

__declspec(noinline) static void KsRestoreRow(int row)
{
    if (!s_ksTable || row < 0 || row >= k_ksRows) return;
    __try {
        memcpy(s_ksTable + row * k_ksRowSize + 16, s_ksOrig[row], sizeof(s_ksOrig[row]));
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void KsColorRestoreAll()
{
    if (s_ksTable && s_ksPatched >= 0) {
        KsRestoreRow(s_ksPatched);
        if (s_ksPatched == 1 && s_ksBlueOk) KsRestoreRow(k_ksBlueRow1);
        s_ksGlowKick = 4;
    }
    s_ksPatched = -1;
}

// The count the pass writes: the real streak, or one above it while a kick is pending.
static int KsKickedStreak(int count)
{
    if (s_ksGlowKick <= 0) return count;
    --s_ksGlowKick;
    return count + 1;
}

// A custom color needs a row to live in. With no sheen picked, attribute 2014 is never written
// and nothing ever reads the table, so "custom color" on its own quietly means team shine.
static int KsSheenIndexFor(const SkinCfg& c) { return SkinPure_SheenIdx(c); }

// Once a frame, off the held weapon's config: paint that weapon's sheen row and put every other
// row back. Only one row can be custom at a time because the table is process-wide -- it is the
// held weapon's, which is also the only weapon whose sheen and eyes are on screen.
static void KsColorUpdate(const SkinCfg* held)
{
    KsFindTable();
    if (!s_ksTable) return;

    const bool want = held && held->enabled && held->ksCustomColor && held->ksTier >= 2;
    const int  row  = want ? KsSheenIndexFor(*held) : -1;
    if (row <= 0 || row >= k_ksBaseRows) { KsColorRestoreAll(); return; }

    const float* rgb = held->ksColor;
    if (row == s_ksPatched && rgb[0] == s_ksPatchedColor[0] &&
        rgb[1] == s_ksPatchedColor[1] && rgb[2] == s_ksPatchedColor[2])
        return;                                   // already painted

    if (s_ksPatched >= 0 && s_ksPatched != row) KsColorRestoreAll();

    KsWriteRow(row, rgb);
    if (row == 1 && s_ksBlueOk) KsWriteRow(k_ksBlueRow1, rgb);   // team shine's BLU copy
    s_ksPatched = row;
    memcpy(s_ksPatchedColor, rgb, sizeof(s_ksPatchedColor));

    // The sheen picks a table write up on its next bind, but the eye glow does NOT: its color
    // is only re-read inside UpdateKillStreakEffects, which the client's think calls when the
    // streak COUNT changes -- otherwise the think just re-pushes the color the glow was built
    // with. Bumping the written count for a few frames and dropping it back makes the think
    // notice twice. +1 rather than -1 so the count never falls under tf_killstreakeyes_minkills
    // and blinks the glow off.
    s_ksGlowKick = 4;

    static DWORD s_lastLog = 0;
    const DWORD now = GetTickCount();
    if (now - s_lastLog > 1000) {                 // a dragged color picker changes every frame
        s_lastLog = now;
        DebugLog("skins: killstreak color row %d = %.2f %.2f %.2f", row, rgb[0], rgb[1], rgb[2]);
    }
}

// ==== gold ragdolls =============================================================================
// set_turn_to_gold (attr 150) is read SERVER side only -- tf_player.cpp:12720 hooks it off the
// killer's weapon and passes the result into CreateRagdollEntity, which sets m_bGoldRagdoll on
// the ragdoll it spawns. A client-side attribute write can never reach that. What IS client side
// is everything the flag then drives: C_TFRagdoll::CreateTFRagdoll reads m_bGoldRagdoll and from
// it alone applies models/player/shared/gold_player.vmt to the corpse AND every wearable
// following it, fixes the constraints and plays Saxxy.TurnGold. So the flag is set on the
// ragdoll entity in dih's existing CreateTFRagdoll hook, before the original runs.
static void QueueGoldVictim(int victimIdx)
{
    s_goldPending[s_goldPendingPos] = { victimIdx, GetTickCount() };
    s_goldPendingPos = (s_goldPendingPos + 1) % (int)(sizeof(s_goldPending)/sizeof(s_goldPending[0]));
    DebugLog("skins: gold kill on player %d -- waiting for its ragdoll", victimIdx);
}

// Called from h_CreateTFRagdoll with the ragdoll entity, before the original. True = gilded.
bool Skins_OnCreateRagdoll(void* pRagdoll)
{
    if (!pRagdoll || g_eject || !g_enableSkins) return false;
    if (s_offGoldRagdoll <= 0 || s_offRagdollPlayer <= 0) return false;

    bool gilded = false;
    __try {
        if (SEH_VtableSane(pRagdoll)) {
            const unsigned int hPlayer = SEH_ReadHandle(pRagdoll, s_offRagdollPlayer, 0);
            const int victimIdx = (hPlayer == 0 || hPlayer == 0xFFFFFFFF)
                                  ? 0 : (int)(hPlayer & 0x3FFF);
            const DWORD now = GetTickCount();
            int live = 0;
            for (auto& p : s_goldPending) {
                if (p.victimIdx == 0) continue;
                if (now - p.tick > 2000) { p.victimIdx = 0; continue; }   // stale, not ours
                if (p.victimIdx == victimIdx) {
                    p.victimIdx = 0;
                    gilded = SEH_WriteBool(pRagdoll, s_offGoldRagdoll, true);
                    DebugLog("skins: gold ragdoll for player %d (write %s)",
                             victimIdx, gilded ? "ok" : "FAILED");
                    break;
                }
                ++live;
            }
            // A corpse arriving while a gold kill is still waiting, naming a DIFFERENT player,
            // is the one way this dies silently (the event and the entity disagreeing on who
            // was killed, or the ragdoll arriving a snapshot before the event). Say so.
            if (!gilded && live > 0)
                DebugLog("skins: ragdoll for player %d matches none of %d pending gold kill(s)",
                         victimIdx, live);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return gilded;
}

// Called from the killfeed's FireEvent hook, unconditionally -- both of these are wanted
// whether or not the killfeed filter is on.
void Skins_OnGameEvent(void* pEvent)
{
    if (!pEvent || g_eject || !g_enableSkins) return;

    __try {
        const char* name = IGameEventVtbl::GetName(pEvent);
        if (!name) return;

        const int localIdx = GetLocalPlayerIndex();
        if (localIdx < 1) return;

        // Every way a streak can end. The three "userid" ones are ours only when the userid is
        // ours; the round events end everybody's. player_changeclass is the pick, and
        // localplayer_changeclass/changeteam are the ones the client fires for itself, so a
        // class change that never round-trips the server still resets.
        if (strcmp(name, "player_spawn")       == 0 ||
            strcmp(name, "player_changeclass") == 0) {
            const int uid = IGameEventVtbl::GetInt(pEvent, "userid", 0);
            if (uid > 0 && GetPlayerForUserID(uid) == localIdx) ResetStreak(name);
            return;
        }
        if (strcmp(name, "teamplay_round_start")    == 0 ||
            strcmp(name, "teamplay_round_active")   == 0 ||
            strcmp(name, "teamplay_win_panel")      == 0 ||
            strcmp(name, "localplayer_changeclass") == 0 ||
            strcmp(name, "localplayer_changeteam")  == 0) {
            // Our own cl_fullupdate rebuilds the local player, and the rebuild fires both
            // localplayer_* events (debug.log 10-30: every "cl_fullupdate (skin changed)" was
            // followed by a changeteam + changeclass reset). Same team, same class: keep it.
            if (name[0] == 'l' && GetTickCount() - s_fullUpdateTick < 3000) {
                DebugLog("skins: %s right after our cl_fullupdate, streak %d kept", name, StreakTotal());
                return;
            }
            ResetStreak(name);
            return;
        }
        if (strcmp(name, "player_death") != 0) return;

        const int attackerUid = IGameEventVtbl::GetInt(pEvent, "attacker", 0);
        const int victimUid   = IGameEventVtbl::GetInt(pEvent, "userid",   0);
        const int attackerIdx = (attackerUid > 0) ? GetPlayerForUserID(attackerUid) : 0;
        const int victimIdx   = (victimUid   > 0) ? GetPlayerForUserID(victimUid)   : 0;

        // Our own death ends the streak whoever did it -- not a Dead Ringer feign: same event,
        // death_flags carries TF_DEATH_FEIGN_DEATH (tf_shareddefs.h:1811), and we are alive.
        if (victimIdx == localIdx) {
            if (IGameEventVtbl::GetInt(pEvent, "death_flags", 0) & 0x0020)
                DebugLog("skins: feign death, streak %d kept", StreakTotal());
            else
                ResetStreak("death", true);
            return;
        }
        if (attackerIdx != localIdx || victimIdx <= 0) return;

        int heldDef = 0, heldKey = 0;
        const SkinCfg* cfg = ActiveWeaponCfg(&heldDef, &heldKey);

        // -- killstreak -------------------------------------------------------------------
        // The TOTAL spans every killstreak weapon, ours or real: a kill counts when our config
        // gives the held weapon a tier OR the server counted it (kill_streak_wep > 0 only for a
        // weapon that really has killstreak_tier, tf_gamerules.cpp:13179), so 9 on a real kunai
        // plus one with a faked ambassador is a total of 10. The number beside the killer's name
        // is kill_streak_wep (tf_hud_deathnotice.cpp:1195) and stays PER WEAPON, as the game
        // keeps it: ours for a weapon we gave a tier, the server's own for a real one. With no
        // config asking for a streak the event is left alone -- stock behavior.
        const bool ourKs    = cfg && cfg->ksTier > 0;
        const bool serverKs = IGameEventVtbl::GetInt(pEvent, "kill_streak_wep", 0) > 0;
        if (ourKs || serverKs) {
            DemoStreakSync();   // a rewind's replayed kills count once, not twice
            const int wep = BumpStreak(heldKey);   // key 0 = real ks weapon with no config
            s_killStreak  = StreakTotal();
            if (AnyKillstreakWanted()) {
                ApplyKillStreak(localIdx, s_killStreak);
                IGameEventVtbl::SetInt(pEvent, "kill_streak_total", s_killStreak);
                if (ourKs) IGameEventVtbl::SetInt(pEvent, "kill_streak_wep", wep);
            }
            DebugLog("skins: streak -> %d total, %d on weapon key %d (def %d, %s)",
                     s_killStreak, wep, heldKey, heldDef, ourKs ? "ours" : "server");
        }

        // -- gold statue ------------------------------------------------------------------
        if (cfg && cfg->reskinDef > 0)
            if (const ReskinInfo* ri = Skin_FindReskinInfo(cfg->reskinDef))
                if (ri->turnToGold) QueueGoldVictim(victimIdx);

        // -- exorcism ---------------------------------------------------------------------
        if ((cfg && (cfg->spellFlags & kCfgSpell_Exorcism)) || WeaponHasExorcismAttr())
            DispatchExorcismGhosts(victimIdx);

        // -- killfeed icon ----------------------------------------------------------------
        // The kill notice names the weapon by its item_logname; with a reskin equipped the
        // held def index is the reskin's, so the icon follows straight from the table.
        //
        // logname == nullptr means mod_textures.txt has no entry for it (five reskins, the
        // AWPer Hand among them). The HUD builds its icon as "d_" + this string and falls back
        // to the SKULL when the lookup fails, so a name it cannot resolve is strictly worse
        // than silence: leaving the event alone keeps the base weapon's own icon.
        if (const ReskinInfo* ri = Skin_FindReskinInfo(heldDef)) {
            if (ri->logname) {
                IGameEventVtbl::SetString(pEvent, "weapon", ri->logname);
                IGameEventVtbl::SetString(pEvent, "weapon_logclassname", ri->logname);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// ==== install ===================================================================================
void Skins_Install()
{
    memset(s_seqRemap, 0xFF, sizeof(s_seqRemap));

    // Give every weapon its entry up front, BEFORE the hook goes in. The menu writes this map
    // on the render thread while the FSN hook reads it on the engine thread; a rehash under a
    // concurrent find() is a crash, and pre-creating every key means the menu can only ever
    // mutate a value that is already there.
    g_skinCfgs.reserve((size_t)k_wpnCount * 2 + 64);
    for (int i = 0; i < k_wpnCount; ++i) g_skinCfgs[k_wpnList[i].redir];
    s_cfgsSealed = true;   // from here on NOTHING may add a key -- see Skins_LoadCfg

    HMODULE hEngine = GetModuleHandleA("engine.dll");
    HMODULE hClient = GetModuleHandleA("client.dll");
    if (!hEngine || !hClient) return;

    ResolveSkinSigs();

    InstallPatternHook(hEngine, "skins: FrameStageNotify", "4C 8B DC 56 48 83 EC",
                       (void*)h_FrameStageNotify, (void**)&o_FrameStageNotify);

    // The FSN(3) call inside CL_ProcessPacketEntities. Resolving it proves the stage-2 window
    // is the packet-entities one; the pre-write stays disarmed when the sig misses rather than
    // writing def indexes at a stage where the client has already unpacked them.
    if (uint8_t* call = PatternScan(hEngine, "E8 ? ? ? ? 48 8B 0D ? ? ? ? 48 8B 01 FF 50 ? 44 8B F0")) {
        s_preWriteCallSite = reinterpret_cast<uintptr_t>(call) + 5;
        DebugLog("skins: pre-write window resolved at %p", (void*)s_preWriteCallSite);
    } else {
        DebugLog("skins: CL_ProcessPacketEntities sig NOT FOUND -- pre-write disabled, "
                 "reskin materials/skins will not refresh");
    }

    // The aim tree hooks CBaseAnimating::SetSequence off this same signature (its whitelist
    // entry "CBaseAnimating_SetSequence"). When it is live this create fails, o_SetSequence
    // stays null and h_SetSequence never runs -- the aim hook calls Skins_RemapSequence
    // instead, so the reskin animation remap works either way.
    // Aim installs first and MinHook's jump overwrites the six prologue bytes the pattern opens
    // with, so the plain scan misses whenever aim owns it. Scan unhooked first, then with those
    // six wildcarded -- one line either way, never a false "NOT FOUND". (client+0x1D4BE0 on 10-30,
    // `cmp [rcx+m_nSequence],edx / je / mov [rcx+m_nSequence],edx` = SetSequence.)
    if (uint8_t* a = PatternScan(hClient, "40 53 48 83 EC ? 48 8B D9 39 91 ? ? ? ? 74 ? 89 91")) {
        InstallHookAt(a, "skins: CBaseAnimating::SetSequence", (void*)h_SetSequence, (void**)&o_SetSequence);
    } else if (uint8_t* h = PatternScan(hClient, "? ? ? ? ? ? 48 8B D9 39 91 ? ? ? ? 74 ? 89 91")) {
        s_setSeqViaAim = true;
        DebugLog("skins: CBaseAnimating::SetSequence at %p owned by the aim hook -- remap runs "
                 "from there (aim/hooks.cpp calls Skins_RemapSequence)", (void*)h);
    } else {
        DebugLog("skins: CBaseAnimating::SetSequence NOT FOUND (hooked or not) -- reskin anim "
                 "remap is off");
    }

    // Spoofed only while a Halloween spell is actually selected -- see h_TF_IsHolidayActive.
    InstallPatternHook(hClient, "skins: TF_IsHolidayActive",
                       "48 83 EC ? 48 8B 05 ? ? ? ? 44 8B C9",
                       (void*)h_TF_IsHolidayActive, (void**)&o_TF_IsHolidayActive);

    HatsInv_Install();   // hat changer: loadout screen equip for the backpack copies
    Hats_HudInstall();   // hat changer: hud class model panel wears ours
}

// ==== config ====================================================================================
// One block per weapon that has an entry. Keys are "skin_<configKey>_<field>", which cannot
// collide with dih's own flat keys or the aimbot's "Vars::" ones.
void Skins_SaveCfg(FILE* f)
{
    fprintf(f, "enableSkins=%d\n", g_enableSkins ? 1 : 0);
    fprintf(f, "skinsBackpack=%d\n", g_skinsBackpack ? 1 : 0);
    char buf[1024];   // one weapon's block is ~500 bytes; same text the wire carries (skinshare_pure.h)
    for (const auto& [id, c] : g_skinCfgs)
        if (SkinPure_Format(buf, sizeof(buf), id, c)) fputs(buf, f);
}

bool Skins_LoadCfg(const char* key, const char* val)
{
    if (strcmp(key, "enableSkins") == 0) { g_enableSkins = (atoi(val) != 0); return true; }
    if (strcmp(key, "skinsBackpack") == 0) { g_skinsBackpack = (atoi(val) != 0); return true; }
    if (strncmp(key, "skin_", 5) != 0) return false;

    const char* p = key + 5;
    char* end = nullptr;
    const long defIdx = strtol(p, &end, 10);
    if (!end || *end != '_' || defIdx <= 0) return true;   // malformed, but ours -- swallow it
    const char* field = end + 1;

    // AFTER Skins_Install the map is SEALED: operator[] would insert, an insert can rehash,
    // and a rehash frees the bucket array under the engine thread sitting in a find() or an
    // iteration (h_FrameStageNotify -> RebuildReverseMap / ResolveWeaponTarget). The menu is
    // safe because every key it can select was pre-created; the "load config" BUTTON was not,
    // because its def index comes out of the file. Startup load runs before the seal and still
    // creates whatever the cfg holds.
    if (s_cfgsSealed && g_skinCfgs.find((int)defIdx) == g_skinCfgs.end()) {
        DebugLog("skins: cfg key skin_%ld_%s ignored -- no such weapon (map sealed at install)",
                 defIdx, field);
        return true;
    }

    SkinPure_SetField(g_skinCfgs[(int)defIdx], field, val);
    return true;
}

// ==== exports for the hat changer (hats.cpp) ====================================================
// Same helpers the reskins use, so the hats get the same verified modelinfo slots and the same
// SetRuntimeAttributeValue sig instead of a second copy of each.

// A drawable index for path: precached (> 0) or registered client-side dynamic (< -1). 0 = none.
int Skins_ModelIndex(const char* path)
{
    if (!path || !path[0]) return 0;
    int idx = MI_GetModelIndex(path);
    if (!ModelIdxIs(idx, path)) idx = MI_RegisterDynamic(path);
    return (idx > 0 || idx < -1) ? idx : 0;
}

bool Skins_SetAttribute(void* pAttrList, unsigned int index, float value)
{
    ResolveSkinSigs();
    return pAttrList && EngineSetAttribute(reinterpret_cast<CAttributeList*>(pAttrList), index, value);
}

// model_t* -> its studiohdr_t, or null unless it reads "IDST".
const void* Skins_StudioHdr(const void* pModel)
{
    const void* h = MI_GetStudiomodel(pModel);
    return HDR_IsStudio(h) ? h : nullptr;
}

const void* Skins_ModelPtr(int modelIndex) { return MI_GetModel(modelIndex); }
