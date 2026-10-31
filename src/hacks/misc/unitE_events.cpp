#include "unitE_events.h"
#include "../hud/hitmarker.h"
#include "killeffects.h"
#include "../hud/points.h"

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
static const char* GetEventString(void* pEvent, const char* key, const char* def = "")
{
    void** vtbl = *(void***)pEvent;
    if ((uintptr_t)vtbl < 0x10000 || (uintptr_t)vtbl[8] < 0x10000) return def;
    using Fn = const char*(__fastcall*)(void*, const char*, const char*);
    return ((Fn)vtbl[8])(pEvent, key, def);
}

void UnitE_OnGameEvent(void* pEvent)
{
    if (!pEvent || !IsInGame()) return;

    __try
    {
        const char* name = IGameEventVtbl::GetName(pEvent);
        if (!name) return;

        const int localIdx = GetLocalPlayerIndex();
        if (localIdx < 1) return;

        if (strcmp(name, "player_hurt") == 0)
        {
            const int attackerIdx = GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "attacker", 0));
            if (attackerIdx != localIdx) return;   // hit marker/sound are OUR feedback only
            const int dmg      = IGameEventVtbl::GetInt(pEvent, "damageamount", 0);
            const bool crit     = GetEventBool(pEvent, "crit");
            const bool minicrit = GetEventBool(pEvent, "minicrit");
            const bool killed   = IGameEventVtbl::GetInt(pEvent, "health", 1) <= 0;
            Hitmarker_OnHurt(dmg, crit, minicrit, killed);
        }
        else if (strcmp(name, "player_death") == 0)
        {
            const int victimIdx   = GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "userid", 0));
            const int attackerIdx = GetPlayerForUserID(IGameEventVtbl::GetInt(pEvent, "attacker", 0));
            KillEffects_OnDeath(victimIdx, attackerIdx, victimIdx == localIdx);
            if (attackerIdx == localIdx && victimIdx > 0 && victimIdx != localIdx)
            {
                static const float kFragCol[4] = { 1.f, 0.85f, 0.2f, 1.f };
                Points_Push("+1 Frag", kFragCol);
            }
        }
        else if (strcmp(name, "teamplay_point_captured") == 0)
        {
            const char* cappers = GetEventString(pEvent, "cappers", "");
            for (size_t i = 0; cappers[i]; ++i)
            {
                if ((unsigned char)cappers[i] == (unsigned char)localIdx)
                {
                    static const float kCapCol[4] = { 0.3f, 0.9f, 1.f, 1.f };
                    Points_Push("Point Captured!", kCapCol);
                    break;
                }
            }
        }
        else if (strcmp(name, "teamplay_flag_event") == 0)
        {
            // "player" is entindex() directly here, not a userid (tf_player.cpp:8559
            // confirms SetInt("player", entindex())), unlike every other event this
            // dispatcher reads.
            const int playerEnt = IGameEventVtbl::GetInt(pEvent, "player", 0);
            const int eventType = IGameEventVtbl::GetInt(pEvent, "eventtype", 0);
            if (playerEnt == localIdx && eventType == 2 /* TF_FLAGEVENT_CAPTURE */)
            {
                static const float kFlagCol[4] = { 0.3f, 0.9f, 1.f, 1.f };
                Points_Push("Intel Captured!", kFlagCol);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
