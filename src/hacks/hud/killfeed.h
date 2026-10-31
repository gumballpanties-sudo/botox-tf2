#pragma once
#include "../../globals/globals.h"

// Self-only killfeed filter -- IGameEventManager2::FireEvent hook (slot 8).
using FireEvent_t = bool(__fastcall*)(void*, void*, bool);
extern FireEvent_t o_FireEvent;
bool __fastcall h_FireEvent(void* thisptr, void* pEvent, bool bDontBroadcast);
