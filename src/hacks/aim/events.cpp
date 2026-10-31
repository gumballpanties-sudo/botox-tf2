#include "events.h"

#include "core.h"
#include "crithack.h"
#include "aim_melee.h"
#include "../esp/esp_fx.h"   // Unit C2: damage numbers -- see esp.h's banner for why this is a
                             // plain-primitive header instead of dih's globals.h

// Unit C's own g_esp* toggle, declared the same way esp.cpp already does (plain extern, no
// dih globals.h include -- see esp.h's banner comment).
extern bool g_espDamageNumbers;

bool CEventListener::Initialize()
{
	std::vector<const char*> vEvents = { 
		"client_beginconnect", "client_connected", "client_disconnect", "game_newmap", "teamplay_round_start", "scorestats_accumulated_update", "mvm_reset_stats", "player_connect_client", "player_spawn", "player_changeclass", "player_hurt", "vote_cast", "item_pickup", "revive_player_notify", "player_death"
	};

	for (auto szEvent : vEvents)
	{
		I::GameEventManager->AddListener(this, szEvent, false);

		if (!I::GameEventManager->FindListener(this, szEvent))
		{
			U::Core.AppendFailText(std::format("Failed to add listener: {}", szEvent).c_str());
			m_bFailed = true;
		}
	}

	return !m_bFailed;
}

void CEventListener::Unload()
{
	I::GameEventManager->RemoveListener(this);
}

void CEventListener::FireGameEvent(IGameEvent* pEvent)
{
	if (!pEvent)
		return;

	auto pLocal = H::Entities.GetLocal();
	auto uHash = FNV1A::Hash32(pEvent->GetName());

	if (I::EngineClient->IsPlayingDemo())
		return;

	F::CritHack.Event(pEvent, uHash, pLocal);
	F::AimbotMelee.Event(pEvent, uHash, pLocal);   // auto backstab promise-vs-result line
	switch (uHash)
	{
	case FNV1A::Hash32Const("revive_player_notify"):
	{
		if (!Vars::Misc::MannVsMachine::InstantRevive.Value || pEvent->GetInt("entindex") != I::EngineClient->GetLocalPlayer())
			break;

		KeyValues* kv = new KeyValues("MVM_Revive_Response");
		kv->SetBool("accepted", true);
		I::EngineClient->ServerCmdKeyValues(kv);
		break;
	}
	case FNV1A::Hash32Const("player_hurt"):
	{
		if (!g_espDamageNumbers)
			break;

		const int iDamage = pEvent->GetInt("damageamount");
		if (iDamage <= 0)
			break;

		// own damage only, every other player's hits would spam the screen
		if (I::EngineClient->GetPlayerForUserID(pEvent->GetInt("attacker")) != I::EngineClient->GetLocalPlayer())
			break;
		const int iVictim = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid"));
		if (iVictim <= 0 || iVictim == I::EngineClient->GetLocalPlayer())   // rocket jump self-damage
			break;

		auto pEntity = I::ClientEntityList->GetClientEntity(iVictim);
		auto pVictim = pEntity ? pEntity->As<CTFPlayer>() : nullptr;
		if (!pVictim)
			break;

		const bool bCrit = pEvent->GetBool("crit") || pEvent->GetBool("minicrit");
		const Vec3 vOrigin = pVictim->GetAbsOrigin();
		EspFx_PushDamageNum(vOrigin.x, vOrigin.y, vOrigin.z + 64.f, iDamage, bCrit);
		break;
	}
	}
}