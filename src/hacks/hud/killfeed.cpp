#include "killfeed.h"
#include "../visuals/skins.h"
#include "../visuals/hats.h"       // Hats_OnGameEvent: spawn / resupply snapshot
#include "../misc/unitE_events.h"   // hit marker/sound, death particles, score popup -- piggyback, no new hook

// Server events (player_death etc.) reach CHudBaseDeathNotice through slot 8, which in TF2 x64
// engine.dll is FireEventClientSide (CGameEventManager vtable: 6 CreateEvent, 7 FireEvent,
// 8 FireEventClientSide, 9 DuplicateEvent, 10 FreeEvent). Return false = swallow entry.
FireEvent_t o_FireEvent = nullptr;

// The manager owns a fired event and frees it on every path; swallowing one without calling the
// original leaked it. FreeEvent (slot 10) is what the engine would have done.
__declspec(noinline) static bool DropEvent(void* mgr, void* pEvent)
{
    __try {
        using FreeEvent_t = void(__fastcall*)(void*, void*);
        ((FreeEvent_t)(*(void***)mgr)[10])(mgr, pEvent);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

// body in _impl: uses __try, so the MiscHookGuard cannot live here (C2712)
static bool __fastcall h_FireEvent_impl(void* thisptr, void* pEvent, bool bDontBroadcast)
{
    if (pEvent && g_selfKillfeedOnly && !g_eject)
    {
        __try
        {
            const char* name = IGameEventVtbl::GetName(pEvent);
            if (name)
            {
                const int localIdx = GetLocalPlayerIndex();
                if (localIdx >= 1)
                {
                    auto uid2idx = [](int uid) -> int {
                        return (uid > 0) ? GetPlayerForUserID(uid) : 0;
                    };

                    // kill notices
                    if (strcmp(name, "player_death")     == 0 ||
                        strcmp(name, "fish_notice")      == 0 ||
                        strcmp(name, "fish_notice__arm") == 0 ||
                        strcmp(name, "slap_notice")      == 0)
                    {
                        int attackerIdx = uid2idx(IGameEventVtbl::GetInt(pEvent, "attacker", 0));
                        int victimIdx   = uid2idx(IGameEventVtbl::GetInt(pEvent, "userid",   0));
                        int assisterIdx = uid2idx(IGameEventVtbl::GetInt(pEvent, "assister", 0));
                        bool involved = (attackerIdx == localIdx) ||
                                        (victimIdx   == localIdx) ||
                                        (assisterIdx == localIdx && assisterIdx > 0);
                        if (!involved)
                            return DropEvent(thisptr, pEvent);
                    }
                    // building destroyed
                    else if (strcmp(name, "object_destroyed") == 0)
                    {
                        int attackerIdx = uid2idx(IGameEventVtbl::GetInt(pEvent, "attacker", 0));
                        int ownerIdx    = uid2idx(IGameEventVtbl::GetInt(pEvent, "userid",   0));
                        if (attackerIdx != localIdx && ownerIdx != localIdx)
                            return DropEvent(thisptr, pEvent);
                    }
                }
            }
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    return o_FireEvent(thisptr, pEvent, bDontBroadcast);
}

bool __fastcall h_FireEvent(void* thisptr, void* pEvent, bool bDontBroadcast)
{
    MiscHookGuard _g;
    // Skinchanger's player_death work: exorcism ghosts + the reskin's killfeed icon. Runs
    // before the filter below and regardless of it -- neither has anything to do with
    // whether the notice is shown.
    Skins_OnGameEvent(pEvent);
    Hats_OnGameEvent(pEvent);   // spawn / resupply: the hat changer takes its loadout snapshot
    UnitE_OnGameEvent(pEvent);
    return h_FireEvent_impl(thisptr, pEvent, bDontBroadcast);
}
