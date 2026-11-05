#include "game_events.h"
#include "../hud/hitmarker.h"
#include "killeffects.h"
#include "../visuals/world_fx3.h"   // world hit marker on player_hurt
#include "misc_overlays.h"   // healthshot kill trigger + displayed logs

// IGameEvent::GetBool/GetString -- slots 5 and 8 (common.h's own IGameEventVtbl comment:
// "5 GetBool 6 GetInt 7 GetFloat 8 GetString"). common.h only exposes GetName/GetInt/SetInt/
// SetString; kept local here rather than widening that shared struct for two readers.
static bool GetEventBool(void* pEvent, const char* key, bool def = false)
{
    void** vtbl = *(void***)pEvent;
    if ((uintptr_t)vtbl < 0x10000 || (uintptr_t)vtbl[5] < 0x10000) return def;
    using Fn = bool(__fastcall*)(void*, const char*, bool);
    return ((Fn)vtbl[5])(pEvent, key, def);
}

void GameEvents_Dispatch(void* pEvent)
{
    if (!pEvent || !IsInGame()) return;

    __try
    {
        const char* name = IGameEventVtbl::GetName(pEvent);
        if (!name) return;

        const int localIdx = GetLocalPlayerIndex();
        if (localIdx < 1) return;

        MiscLogs_OnGameEvent(pEvent, name, localIdx);

        if (strcmp(name, "player_hurt") == 0)
        {
            const int attackerIdx = GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "attacker", 0));
            if (attackerIdx != localIdx) return;   // hit marker/sound are OUR feedback only
            const int dmg      = IGameEventVtbl::GetInt(pEvent, "damageamount", 0);
            const bool crit     = GetEventBool(pEvent, "crit");
            const bool minicrit = GetEventBool(pEvent, "minicrit");
            const bool killed   = IGameEventVtbl::GetInt(pEvent, "health", 1) <= 0;
            Hitmarker_OnHurt(dmg, crit, minicrit, killed);
            WorldFx3_OnPlayerHurt(GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "userid", 0)));   // world hit marker
        }
        else if (strcmp(name, "player_death") == 0)
        {
            const int victimIdx   = GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "userid", 0));
            const int attackerIdx = GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "attacker", 0));
            KillEffects_OnDeath(victimIdx, attackerIdx, victimIdx == localIdx);
            // frag / capture score popups gone: points is botox's trick counter now (detections.cpp)
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
