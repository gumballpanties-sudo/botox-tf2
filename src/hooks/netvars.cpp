#include "../globals/globals.h"

IClientEntityList* g_entityList = nullptr;

// ==== NetVar resolver ==========================================================
ClientClass* GetAllClasses()
{
    HMODULE client = GetModuleHandleA("client.dll");
    if (!client) return nullptr;
    static void* g_clientDLL = nullptr;
    if (!g_clientDLL)
    {
        using CreateInterface_t = void*(*)(const char*, int*);
        auto ci = (CreateInterface_t)GetProcAddress(client, "CreateInterface");
        if (!ci) return nullptr;
        const char* names[] = {"VClient018","VClient017","VClient016",nullptr};
        for (int i = 0; names[i] && !g_clientDLL; ++i)
            g_clientDLL = ci(names[i], nullptr);
    }
    if (!g_clientDLL) return nullptr;
    ClientClass* result = nullptr;
    __try {
        using GetAllClasses_t = ClientClass*(__thiscall*)(void*);
        void** vtbl = *(void***)g_clientDLL;
        if (reinterpret_cast<uintptr_t>(vtbl) >= 0x10000)
            result = ((GetAllClasses_t)vtbl[8])(g_clientDLL);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

int FindPropOffset(RecvTable* pTable, const char* name, int baseOffset, int depth)
{
    if (!pTable || depth > 6) return -1;
    for (int i = 0; i < pTable->m_nProps; ++i)
    {
        RecvProp& prop = pTable->m_pProps[i];
        if (!prop.m_pVarName) continue;
        if (strcmp(prop.m_pVarName, name) == 0)
            return baseOffset + prop.m_Offset;
        if (prop.m_pDataTable)
        {
            int result = FindPropOffset(prop.m_pDataTable, name, baseOffset + prop.m_Offset, depth + 1);
            if (result >= 0) return result;
        }
    }
    return -1;
}

RecvTable* FindNetworkTable(const char* netTableName)
{
    for (auto* cls = GetAllClasses(); cls; cls = cls->m_pNext)
        if (cls->m_pRecvTable && strcmp(cls->m_pRecvTable->m_pNetTableName, netTableName) == 0)
            return cls->m_pRecvTable;
    return nullptr;
}

int  g_offClass        = 0;
int  g_offLifeState    = 0;
int  g_offBody         = 0;
int  g_offModelIndex   = 0;
int  g_offMoveParent   = 0;  // m_hNetworkMoveParent ("moveparent")
int  g_offWeaponOwner  = 0;  // m_hOwner in C_BaseCombatWeapon
int  g_offActiveWeapon = 0;  // m_hActiveWeapon
int  g_offMyWeapons    = 0;  // m_hMyWeapons[0] (DT_BaseCombatCharacter via DT_TFPlayer) --
                              // CHandle<CTFWeaponBase>[MAX_WEAPONS=48] (confirmed, Amalgam
                              // SDK CBaseCombatCharacter.h); index IS the weapon slot --
                              // unit B's recorder "force same weapons" reads this
int  g_offFlags        = 0;  // m_fFlags (FL_ONGROUND/FL_DUCKING)
int  g_offGroundEntity = 0;  // m_hGroundEntity -- Aletherium ground check
int  g_offCachedBones  = 0;  // m_CachedBoneData = m_hLightingOrigin - 88 (netvar-relative)
bool g_netvarsReady    = false;

// 0 = unresolved (retry next call), -1 = looked for and not in this build, >0 = offset.
// Every resolver in this file shares that sentinel, so a "not found" is never retried and
// never mistaken for an offset of 0.
static int NetvarFind(RecvTable* t, const char* name)
{
    const int off = t ? FindPropOffset(t, name) : -1;
    return off > 0 ? off : -1;
}

void ResolveNetvars()
{
    if (g_netvarsReady) return;

    RecvTable* pTFPlayer = FindNetworkTable("DT_TFPlayer");
    if (!pTFPlayer) return;
    RecvTable* pBase = FindNetworkTable("DT_BaseEntity");
    RecvTable* pAnim = FindNetworkTable("DT_BaseAnimating");
    RecvTable* pWpn  = FindNetworkTable("DT_BaseCombatWeapon");

    const struct { int* p; RecvTable* table; const char* name; } kNetvars[] = {
        { &g_offClass,        pTFPlayer, "m_iClass"         },
        { &g_offLifeState,    pTFPlayer, "m_lifeState"      },
        { &g_offActiveWeapon, pTFPlayer, "m_hActiveWeapon"  },
        { &g_offMyWeapons,    pTFPlayer, "m_hMyWeapons[0]"  },
        { &g_offFlags,        pTFPlayer, "m_fFlags"         },
        { &g_offGroundEntity, pTFPlayer, "m_hGroundEntity"  },
        { &g_offBody,         pAnim,     "m_nBody"          },
        { &g_offModelIndex,   pBase,     "m_nModelIndex"    },
        { &g_offMoveParent,   pBase,     "moveparent"       },
        { &g_offWeaponOwner,  pWpn,      "m_hOwner"         },
    };
    for (const auto& n : kNetvars)
        if (*n.p == 0) *n.p = NetvarFind(n.table, n.name);

    // m_CachedBoneData is not networked -- it sits 88 bytes before m_hLightingOrigin.
    if (g_offCachedBones == 0) {
        const int off = NetvarFind(pAnim, "m_hLightingOrigin");
        g_offCachedBones = (off > 88) ? off - 88 : -1;
    }

    if (g_offFlags        < 0) DebugLog("netvars: m_fFlags NOT FOUND");
    if (g_offGroundEntity < 0) DebugLog("netvars: m_hGroundEntity NOT FOUND");
    if (g_offCachedBones  < 0) DebugLog("netvars: m_hLightingOrigin NOT FOUND (fps boost dead)");
    DebugLog("netvars: flags=0x%X ground=0x%X cachedbones=0x%X",
             g_offFlags, g_offGroundEntity, g_offCachedBones);

    if (g_offClass != 0 && g_offLifeState != 0)
        g_netvarsReady = true;
}

// ==== movement netvars (resolved once, on demand) =================================
int g_offOrigin        = 0;  // m_vecOrigin (DT_BaseEntity)
int g_offVelocity      = 0;  // m_vecVelocity[0] (DT_BasePlayer localdata)
int g_offMins          = 0;  // m_vecMins (DT_BaseEntity)
int g_offMaxs          = 0;  // m_vecMaxs (DT_BaseEntity)
int g_offMaxspeed      = 0;  // m_flMaxspeed (DT_BasePlayer)
int g_offTickBase      = 0;  // m_nTickBase (DT_BasePlayer localdata)
int g_offConstraintEnt = 0;  // m_hConstraintEntity; m_pCurrentCommand = this - 8 (confirmed x64)
int g_offWaterLevel    = 0;  // m_nWaterLevel; m_MoveType = this - 4 (confirmed x64)
int g_offModelScale    = 0;  // m_flModelScale (DT_BaseAnimating)
int g_offCond          = 0;  // m_nPlayerCond (DT_TFPlayer m_Shared)
int g_offDucking       = 0;  // m_Local.m_bDucking   (DT_Local) -- duck transition, the mini jump state
int g_offDucked        = 0;  // m_Local.m_bDucked    (DT_Local)
int g_offDuckTime      = 0;  // m_Local.m_flDucktime (DT_Local)
int g_offDuckTimer     = 0;  // m_Shared.m_flDuckTimer (DT_TFPlayerShared) -- grounded duck strip window
int g_offAirDucked     = 0;  // m_Shared.m_nAirDucked  (DT_TFPlayerShared) -- 2 per air event
int g_offSolidFlags    = 0;  // m_usSolidFlags (DT_BaseEntity m_Collision) -- brush filter trigger reject
int g_offViewOffset    = 0;  // m_vecViewOffset[0] (DT_BasePlayer localdata) -- eye pos for PS finder/assist aim
int g_offAirDash       = 0;  // m_iAirDash (DT_TFPlayer m_Shared) -- scout double jump budget
int g_offItemDefIndex  = 0;  // m_iItemDefinitionIndex (DT_EconEntity m_AttributeManager.m_Item) -- Atomizer detect
int g_offTeamNum       = 0;  // m_iTeamNum (DT_BaseEntity) -- team brush pass-through in the trace filter
int g_offCollGroup     = 0;  // m_CollisionGroup (DT_BaseEntity) -- buildings stay solid to both teams
// m_flSimulationTime / m_iObserverMode / m_hObserverTarget / m_flDuckJumpTime were resolved here
// and read by nothing but the DebugLog below. Three belonged to the deleted interp removal; the
// fourth is named for a mini jump gate that never used it. Four FindPropOffset table walks gone.

void Movement_ResolveExtraNetvars()
{
    static bool s_done = false;
    if (s_done) return;

    RecvTable* pTFPlayer = FindNetworkTable("DT_TFPlayer");
    if (!pTFPlayer) return;
    RecvTable* pBase = FindNetworkTable("DT_BaseEntity");
    RecvTable* pAnim = FindNetworkTable("DT_BaseAnimating");

    const auto& find = NetvarFind;

    g_offOrigin        = find(pBase, "m_vecOrigin");
    g_offMins          = find(pBase, "m_vecMins");
    g_offMaxs          = find(pBase, "m_vecMaxs");
    g_offVelocity      = find(pTFPlayer, "m_vecVelocity[0]");
    g_offMaxspeed      = find(pTFPlayer, "m_flMaxspeed");
    g_offTickBase      = find(pTFPlayer, "m_nTickBase");
    g_offConstraintEnt = find(pTFPlayer, "m_hConstraintEntity");
    g_offWaterLevel    = find(pTFPlayer, "m_nWaterLevel");
    g_offModelScale    = find(pAnim, "m_flModelScale");
    g_offCond          = find(pTFPlayer, "m_nPlayerCond");
    g_offDucking       = find(pTFPlayer, "m_bDucking");
    g_offDucked        = find(pTFPlayer, "m_bDucked");
    g_offDuckTime      = find(pTFPlayer, "m_flDucktime");
    g_offDuckTimer     = find(pTFPlayer, "m_flDuckTimer");
    g_offAirDucked     = find(pTFPlayer, "m_nAirDucked");
    g_offSolidFlags    = find(pBase, "m_usSolidFlags");
    g_offViewOffset    = find(pTFPlayer, "m_vecViewOffset[0]");
    g_offAirDash       = find(pTFPlayer, "m_iAirDash");
    g_offItemDefIndex  = find(FindNetworkTable("DT_EconEntity"), "m_iItemDefinitionIndex");
    g_offTeamNum       = find(pBase, "m_iTeamNum");
    g_offCollGroup     = find(pBase, "m_CollisionGroup");

    s_done = true;
    DebugLog("movement netvars: org=0x%X vel=0x%X mins=0x%X maxs=0x%X maxspd=0x%X tickbase=0x%X constraint=0x%X water=0x%X scale=0x%X cond=0x%X team=0x%X collgrp=0x%X",
             g_offOrigin, g_offVelocity, g_offMins, g_offMaxs, g_offMaxspeed,
             g_offTickBase, g_offConstraintEnt, g_offWaterLevel, g_offModelScale, g_offCond,
             g_offTeamNum, g_offCollGroup);
    DebugLog("duck netvars: ducking=0x%X ducked=0x%X ducktime=0x%X ducktimer=0x%X airducked=0x%X",
             g_offDucking, g_offDucked, g_offDuckTime, g_offDuckTimer, g_offAirDucked);
}

// The auto-backstab netvars (m_angEyeAngles[1], m_nPlayerCondEx, m_flNextAttack,
// m_flNextPrimaryAttack) were resolved here for backstab.cpp. That file left the build at
// bs11 and the aim tree resolves its own, so this walked four tables for nobody. Deleted.
// The one fact worth keeping from bs5: EyeAngles is the CONFIRMED field for the behind dot
// -- CTFKnife::IsBehindAndFacingTarget reads pTarget->EyeAngles() (pl.v_angle on the
// server), and lag comp rewinds m_angRotation, which it never touches. Body yaw was logged
// against the server's own verdict and separated nothing.
