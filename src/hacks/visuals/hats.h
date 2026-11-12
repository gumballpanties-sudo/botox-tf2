#pragma once
#include <cstdio>
#include "hats_pure.h"

// Hat changer: client-side cosmetics on the local player. lmaobox's "Hats" page (lbox dump,
// FUN_3c968966) creates a tf_wearable, stamps item def + owner, sets the model and Equips it;
// here the same entity comes out of the CTFWearable ClientClass's CreateFn at a fixed high
// index (the weather entity's proven path, visuals.cpp Rain_Tick) and is fed through the
// engine's own network update order, so TF2's own Spawn picks the model, team and style.
// Nobody else sees any of it. See hats.cpp for the lifecycle.

// Menu = "add hat" list, packed from slot 0. ponytail: hard cap = entity index budget under
// the weather entity (hats.cpp kFirstIndex); raise both together.
constexpr int kHatSlots = 64;
// def 0 = empty, unusual/paint -1 = none.
// eqMask/eqPos: where TF2's loadout screen equipped the backpack copy (hats_inv.cpp hook). Bit 0 set
// = "loadout-managed": the hat is worn only on classes with their bit (1 scout .. 9 engineer) set,
// in loadout position eqPos[class]. Bit 0 clear = worn on every class that can (menu default).
// useHead/zOff: TF2's own loadout item options ("use head origin" + effect height slider, attrs
// 520/519, -8..8 u). Set only from TF2's loadout screen (hats_inv.cpp GC hook), saved in the cfg.
struct HatSlot { int def; int style; int unusual; int paint; uint16_t eqMask; uint8_t eqPos[10];
                 int useHead; float zOff; };

extern bool    g_hatsEnable;
extern bool    g_hatsBackpack;   // also put each worn hat in TF2's own backpack (hats_inv.cpp)
extern HatSlot g_hatSlots[kHatSlots];

int             Hats_Count();
const HatDef*   Hats_At(int i);
const HatDef*   Hats_Find(int def);
int             Hats_PaintCount();
const HatPaint* Hats_Paint(int i);
int             Hats_LocalClass();   // 0 when not in a game

// skins.cpp h_FrameStageNotify, BEFORE the original. Also the only place hats are Released:
// on g_eject it frees them on the game thread, and thread.cpp waits on Hats_AnyLive for that.
void Hats_FrameStage(int stage);
bool Hats_AnyLive();
// killfeed.cpp FireEvent hook. TF2 rule: a loadout change shows at the next spawn or resupply
// (post_inventory_application), never mid-life -- the worn set is a snapshot taken on those events.
void Hats_OnGameEvent(void* pEvent);
// hats_inv.cpp: fake CEconItems in the local SO cache, one per used slot. `want` false = remove ours.
void HatsInv_Tick(bool want);
bool HatsInv_AnyLive();
void HatsInv_Install();   // loadout equip hook + GC effect-offset hook; from Skins_Install
void Hats_HudInstall();   // hud class model panel (cl_hud_playerclass_use_playermodel) wears ours too
// DrawModelExecute (visuals.cpp): the local player's base model body with the worn hats'
// bodygroups hidden -- written into ModelRenderInfo_t::body per draw, never into m_nBody.
bool Hats_WantBody();
bool Hats_IsOurs(int entIdx);        // entity index is one of the hat changer's slots
void Hats_NoteDraw(int entIdx);      // DrawModelExecute: counts draws of our hats (diag line in hats.cpp)
// DrawModelExecute: true = skip this draw. The real hat the server put in a loadout position one of
// ours was equipped over through the loadout screen (otherwise both draw, clipping).
bool Hats_SkipDraw(int entIdx);
uint64_t HatsInv_RealEquippedAt(int cls, int pos);   // GC's real item at (class, loadout pos), 0 = none
int  Hats_Body(int entIdx, const void* pModel, int body);

void Hats_SaveCfg(FILE* f);
bool Hats_LoadCfg(const char* key, const char* val);   // true = key consumed
