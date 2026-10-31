#pragma once
#include "../../globals/globals.h"
#include "../../dependencies/data/particledata.h"

// Skinchanger. Everything here is client-side and self-affecting: the weapons in the local
// player's own m_hMyWeapons get a rewritten m_iItemDefinitionIndex and a set of runtime
// attributes, which is what the client's own renderer reads to pick model, skin, material
// override and particles. No other player sees any of it.
//
// The work happens in two places inside FrameStageNotify:
//   stage 2  -- BEFORE the client unpacks the snapshot. The def index has to be ours here,
//               because CEconEntity::OnDataChanged caches the material override, the style
//               skin and the viewmodel attachment off the item definition exactly once, at
//               that moment. This is what makes a Golden Wrench gold and a Gold Frying Pan
//               gold instead of a plain grey pan.
//   stage 3/5 -- after the update: attributes, model index and m_nSkin are (re)stamped.

void Skins_Install();          // hooks: FrameStageNotify + CBaseAnimating::SetSequence
// Reskin animation remap. Whichever half owns the SetSequence hook calls this -- the aim
// tree hooks the same function off the same signature, and MinHook allows one per target.
int  Skins_RemapSequence(void* pEntity, int nSequence);
// player_death: dispatches the Exorcism ghosts by hand (a server effect the client never
// draws on its own) and points the kill notice at the reskin's own icon. Called from the
// killfeed's FireEvent hook because dih already owns that hook.
void Skins_OnGameEvent(void* pEvent);
// C_TFRagdoll::CreateTFRagdoll, called from visuals.cpp's hook BEFORE the original: stamps
// m_bGoldRagdoll on the corpse of anyone we just killed with a set_turn_to_gold reskin. The
// attribute itself is server-side and never reaches us, but every effect it drives (the gold
// material on the body and its wearables, the fixed constraints, Saxxy.TurnGold) is read out
// of that flag inside this one function.
// Returns true when THIS corpse was gilded -- "remove ragdolls" lets that one through, because
// a gold statue is the reason the weapon is equipped and the corpse is the statue.
bool Skins_OnCreateRagdoll(void* pRagdoll);

void Skins_SaveCfg(FILE* f);   // "skin_<def>_<field>=..." lines
bool Skins_LoadCfg(const char* key, const char* val);  // true = key consumed
// The tab body lives in menu.cpp with every other tab -- it needs that file's widgets.
