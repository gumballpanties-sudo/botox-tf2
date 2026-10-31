#pragma once
#include "../../globals/common.h"

// ==== equip region unlock ============================================================
// TF2 refuses two cosmetics whose item definitions share an equip_region ("hat" +
// "whole_head", two "glasses", ...). The check is client-side book-keeping in the
// loadout UI plus a client-side validation pass, so three client.dll hooks turn it off:
//   1. CTFPlayerInventory::VerifyChangedLoadoutsAreValid - skip the validation pass
//   2. GenerateEquipRegionConflictMask                   - report zero conflicts
//   3. CTFInventoryManager::GetItemInLoadoutForClass     - return null when the equip
//      slot panel asks, so an occupied slot still offers every item
// Ported from fpsoverlay (sheen.cpp), sigs re-verified 1-hit against the live client.dll.
void EquipRegion_Install();   // resolves sigs + installs (no-op once installed)
void EquipRegion_Tick();      // installs on first enable
