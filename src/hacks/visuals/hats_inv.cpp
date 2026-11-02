#include "../../globals/common.h"
#include "../../globals/globals.h"
#include "hats.h"

// ==== hat changer: backpack ====================================================================
// Every hat in a slot also shows up in TF2's own backpack / item lists, client-side only.
// lmaobox does this (lbox dump FUN_3c95344a loads items_custom.txt, FUN_3c956af5 builds one):
//   Plat_Alloc(sizeof CEconItem) -> CEconItem() -> id = rand | 0xFADED << 32 -> account, backpack
//   position, def, level, quality, origin -> SO cache type cache 1 ->AddObject -> local
//   inventory ->SOCreated(owner, item, eSOCacheEvent_Incremental)
// which is the GC's own "you got an item" path (econ_item_inventory.cpp:1528 SOCreated ->
// AddEconItem), so the backpack, loadout lists and item panels all see a real item. The SO cache
// entry matters: CEconItemView::GetSOCData looks the item up in it by id.
//
// x64 layout, all read off the shipping client.dll (11-02) and cross-checked with the SDK:
//   CEconItem 0x80 bytes, default ctor client+0x35FE30 (new site: mov ecx,80h / call new / inline
//   copy of that ctor). +0x48 m_ulID, +0x50 m_unAccountID, +0x54 m_unInventory, +0x58 m_unDefIndex
//   (CTFPlayerInventory::SOCreated: cmp word [rbx+58h],115h), +0x5A level, +0x5B quality,
//   +0x5C flags, +0x5D origin, +0x5E style, +0x5F dirty bits (dtor tests bit 2 = attrib
//   singleton), +0x78 m_pCustomData. vtable slot 0 = scalar deleting dtor, 1 = GetTypeID (1).
//   CPlayerInventory: +0x08 m_OwnerID (CSteamID), +0x98 m_pSOCache (CPlayerInventory::SOCreated
//   checks both). ISharedObjectListener slots: 0 SOCreated, 4 SODestroyed.
//   CTFInventoryManager: vtable slot 23 GetLocalInventory = `lea rax,[rcx+198h]; ret`.
//   CSharedObjectTypeCache: objects at +0x08 (pointer array), count at +0x28, slot 1 AddObject
//   (dedups), slot 3 RemoveObject(const CSharedObject&) -> removed object.
//   Plat_Alloc (tier0 export) = g_pMemAlloc->Alloc, the same heap the game's operator new and
//   delete use, so the game may free our item (GC resubscribe) and we may free it with its dtor.
//
//   CEconItemCustomData 0x58 bytes (its deleting dtor client+0x3603C0, `mov edx,58h` sized delete):
//   +0x00 CUtlVector<attribute_t> (attribute_t 16 bytes: def u16 @0, value @8; the dtor walks it
//   with `shl rsi,4` and unloads [rsi+8]), +0x20 interior item, +0x28 original id,
//   +0x38 CUtlVector<EquippedInstance_t> (class u16, slot u16). CUtlVector x64 = memory @0,
//   allocation count @8, grow size @0xC, size @0x10, element pointer @0x18.
//   Loadout screen: every equip and unequip goes through the virtual
//   CInventoryManager::UpdateInventoryEquippedState (CTFInventoryManager vtable slot 32, a thunk
//   that widens the u16 slot argument), which only messages the GC (econ_item_inventory.cpp:776).
//   The GC refuses an id it never issued, so for ours the hook does the GC's half locally:
//   rewrites the item's equipped list and calls SOUpdated -> CTFPlayerInventory::ItemHasBeenUpdated
//   re-derives m_LoadoutItems from it (tf_item_inventory.cpp:1698), which is what the screen shows.
//
// Ownership: our item ids are (0xFADED << 32) | (0x1000 + slot), a range no GC item uses. The GC
// can wipe them (resubscribe -> RemoveAllObjects -> delete), so a stored pointer is never
// trusted: every touch finds the item again in the type cache by id.

bool g_hatsBackpack = true;

static constexpr uint32_t kIdHigh = 0xFADED;
static uint64_t FakeId(int slot) { return (uint64_t(kIdHigh) << 32) | uint64_t(0x1000 + slot); }

using CEconItemCtor_t  = void*(__fastcall*)(void*);
using FindTypeCache_t  = void*(__fastcall*)(void*, int);
using PlatAlloc_t      = void*(__cdecl*)(size_t);

static CEconItemCtor_t s_ctor       = nullptr;
static FindTypeCache_t s_findTC     = nullptr;
static PlatAlloc_t     s_alloc      = nullptr;
static void*           s_invMgr     = nullptr;
static bool            s_ready      = false;
static volatile bool   s_dirty      = false;   // the loadout hook changed an equip: tick now
static bool            s_dead       = false;

static void Resolve()
{
    if (s_ready || s_dead) return;
    HMODULE client = GetModuleHandleA("client.dll");
    HMODULE tier0  = GetModuleHandleA("tier0.dll");
    if (!client || !tier0) return;
    s_dead = true;   // one attempt: a sig that is not there now will not appear later
    uint8_t* ctor = PatternScan(client, "48 89 5C 24 08 57 48 83 EC 20 48 8D 05 ? ? ? ? 48 8B F9 48 89 01 48 83 C1 08 E8 ? ? ? ? 48 8B 5C 24 30 48 8D 05");
    // Amalgam's CGCClientSharedObjectCache::FindTypeCache (CTFGCClientSystem.h).
    uint8_t* ftc  = PatternScan(client, "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 0F B7 59 ? BE");
    // CTFPlayerInventory::SOCreated: `mov rax,[g_TFInventoryManager]; lea rcx,[g_TFInventoryManager];
    // call [rax+0B8h]` (GetLocalInventory) -- the lea is the instance.
    uint8_t* mgr  = PatternScan(client, "48 8B 05 ? ? ? ? 48 8D 0D ? ? ? ? FF 90 B8 00 00 00 48 3B C7");
    s_alloc = reinterpret_cast<PlatAlloc_t>(GetProcAddress(tier0, "Plat_Alloc"));
    if (mgr) s_invMgr = mgr + 14 + *reinterpret_cast<int32_t*>(mgr + 10);
    s_ctor   = reinterpret_cast<CEconItemCtor_t>(ctor);
    s_findTC = reinterpret_cast<FindTypeCache_t>(ftc);
    DebugLog("hats inv: ctor %p findTC %p invMgr %p alloc %p", (void*)ctor, (void*)ftc, s_invMgr, (void*)s_alloc);
    if (!ctor || !ftc || !s_invMgr || !s_alloc) return;
    s_ready = true; s_dead = false;
}

// ---- SEH leaves -------------------------------------------------------------------------------
struct InvCtx { void* inv; void* owner; void* tc; void** objs; int count; uint32_t account; };

__declspec(noinline) static bool GetCtx(InvCtx& c)
{
    __try {
        c.inv = VCall<void*>(s_invMgr, 23);   // GetLocalInventory
        if (!c.inv || c.inv != static_cast<uint8_t*>(s_invMgr) + 0x198) return false;
        uint8_t* inv = static_cast<uint8_t*>(c.inv);
        c.owner   = inv + 0x08;
        c.account = *reinterpret_cast<uint32_t*>(inv + 0x08);   // CSteamID low dword = account id
        void* soc = *reinterpret_cast<void**>(inv + 0x98);
        if (!soc || !c.account) return false;                    // not subscribed to the GC yet
        c.tc = s_findTC(soc, 1);                                 // CEconItem::k_nTypeID
        if (!c.tc) return false;
        c.objs  = *reinterpret_cast<void***>(static_cast<uint8_t*>(c.tc) + 0x08);
        c.count = *reinterpret_cast<int*>(static_cast<uint8_t*>(c.tc) + 0x28);
        return c.count >= 0 && c.count < 100000 && (c.count == 0 || c.objs);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static uint64_t ItemId(void* it)      { return *reinterpret_cast<uint64_t*>(static_cast<uint8_t*>(it) + 0x48); }
static uint32_t ItemInvPos(void* it)  { return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(it) + 0x54); }
static uint16_t ItemDef(void* it)     { return *reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(it) + 0x58); }
static uint8_t  ItemQuality(void* it) { return *reinterpret_cast<uint8_t*>(static_cast<uint8_t*>(it) + 0x5B); }
static uint8_t  ItemStyle(void* it)   { return *reinterpret_cast<uint8_t*>(static_cast<uint8_t*>(it) + 0x5E); }

// Our item for this slot as the cache holds it now, or null.
__declspec(noinline) static void* FindOurs(const InvCtx& c, int slot)
{
    const uint64_t id = FakeId(slot);
    __try {
        for (int i = 0; i < c.count; ++i)
            if (c.objs[i] && ItemId(c.objs[i]) == id) return c.objs[i];
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}

// Lowest backpack position (1-based) no item is in. TF position format: bit 31 = "has a backpack
// position", bit 30 = unacknowledged, low 16 bits = position (econ_item_constants.h).
__declspec(noinline) static uint32_t FreePosition(const InvCtx& c)
{
    static uint8_t used[4096];
    memset(used, 0, sizeof(used));
    __try {
        for (int i = 0; i < c.count; ++i) {
            if (!c.objs[i]) continue;
            const uint32_t p = ItemInvPos(c.objs[i]);
            if ((p & 0xC0000000u) == 0x80000000u && (p & 0xFFFF) < sizeof(used)) used[p & 0xFFFF] = 1;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
    for (uint32_t p = 1; p < sizeof(used); ++p) if (!used[p]) return p;
    return 0;
}

// What the item's equipped list should hold: (class, loadout position) for every class the
// loadout screen equipped it on.
static int WantEquips(const HatSlot& s, uint16_t* out /* [9][2] */)
{
    int n = 0;
    if (!(s.eqMask & 1)) return 0;
    for (int c = 1; c <= 9; ++c)
        if (s.eqMask >> c & 1) { out[n * 2] = (uint16_t)c; out[n * 2 + 1] = s.eqPos[c]; ++n; }
    return n;
}

static constexpr int kEqCap = 10, kAttrCap = 4;

// Custom data with room for every class and our few attributes, so nothing ever has to grow it.
// All three blocks come from Plat_Alloc = g_pMemAlloc, which is what the game frees them with.
__declspec(noinline) static void* MakeCustomData(const HatSlot& s, const HatDef* d)
{
    uint8_t* cd = static_cast<uint8_t*>(s_alloc(0x58));
    void* eq    = s_alloc(kEqCap * 4);
    void* at    = s_alloc(kAttrCap * 16);
    if (!cd || !eq || !at) return nullptr;   // ponytail: leaks a block on a failed alloc
    memset(cd, 0, 0x58); memset(eq, 0, kEqCap * 4); memset(at, 0, kAttrCap * 16);
    *reinterpret_cast<void**>(cd + 0x38) = eq;  *reinterpret_cast<int*>(cd + 0x40) = kEqCap;
    *reinterpret_cast<void**>(cd + 0x50) = eq;
    *reinterpret_cast<void**>(cd + 0x00) = at;  *reinterpret_cast<int*>(cd + 0x08) = kAttrCap;
    *reinterpret_cast<void**>(cd + 0x18) = at;
    uint8_t* a = static_cast<uint8_t*>(at);
    int na = 0;
    auto addAttr = [&](uint16_t def, float v) {
        *reinterpret_cast<uint16_t*>(a + na * 16) = def;
        *reinterpret_cast<float*>(a + na * 16 + 8) = v;
        ++na;
    };
    if (s.unusual >= 0) addAttr(134, (float)s.unusual);                 // attach particle effect
    if (d->paintable)
        if (const HatPaint* p = Hats_Paint(s.paint)) { addAttr(142, (float)p->rgb); addAttr(261, (float)p->rgb2); }
    *reinterpret_cast<int*>(cd + 0x10) = na;
    uint16_t want[18]; const int ne = WantEquips(s, want);
    memcpy(eq, want, ne * 4);
    *reinterpret_cast<int*>(cd + 0x48) = ne;
    return cd;
}

// Make the item's equipped list match the slot. True = it changed (caller sends SOUpdated).
__declspec(noinline) static bool SyncEquips(void* it, const HatSlot& s)
{
    __try {
        uint8_t* cd = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(it) + 0x78);
        if (!cd) return false;
        uint16_t want[18]; const int n = WantEquips(s, want);
        uint16_t* eq = *reinterpret_cast<uint16_t**>(cd + 0x38);
        int* cnt = reinterpret_cast<int*>(cd + 0x48);
        if (!eq || *reinterpret_cast<int*>(cd + 0x40) < n) return false;
        if (*cnt == n && memcmp(eq, want, n * 4) == 0) return false;
        memcpy(eq, want, n * 4);
        *cnt = n;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

__declspec(noinline) static bool Add(const InvCtx& c, int slot, const HatSlot& s, const HatDef* d)
{
    const uint32_t pos = FreePosition(c);
    if (!pos) return false;
    void* it = s_alloc(0x80);
    if (!it) return false;
    __try {
        memset(it, 0, 0x80);
        s_ctor(it);
        uint8_t* b = static_cast<uint8_t*>(it);
        *reinterpret_cast<uint64_t*>(b + 0x48) = FakeId(slot);
        *reinterpret_cast<uint32_t*>(b + 0x50) = c.account;
        *reinterpret_cast<uint32_t*>(b + 0x54) = 0x80000000u | pos;
        *reinterpret_cast<uint16_t*>(b + 0x58) = (uint16_t)d->def;
        b[0x5A] = 1;                                   // level
        b[0x5B] = s.unusual >= 0 ? 5 : 6;              // AE_UNUSUAL : AE_UNIQUE
        b[0x5D] = 0;                                   // kEconItemOrigin_Drop
        b[0x5E] = (uint8_t)((s.style >= 0 && s.style < d->nStyles) ? s.style : 0);
        *reinterpret_cast<void**>(b + 0x78) = MakeCustomData(s, d);   // equipped list + unusual/paint
        // AddObject (slot 1), then tell the inventory like the GC would. A refused add means the
        // cache did not take it: nothing else holds the item, so it is ours to free.
        if (!VCall<bool>(c.tc, 1, it)) { VCall<void>(it, 0, 1); return false; }
        VCall<void>(c.inv, 0, c.owner, it, 4);        // SOCreated(owner, item, eSOCacheEvent_Incremental)
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DebugLog("hats inv: SEH adding slot %d def %d", slot, d->def);
        return false;
    }
}

// Out of the cache first, then SODestroyed ("already removed from the cache, but still valid",
// gcclient_sharedobjectcache.h), then the game's own deleting dtor.
__declspec(noinline) static void Remove(const InvCtx& c, void* it)
{
    __try {
        void* removed = VCall<void*>(c.tc, 3, it);    // RemoveObject(const CSharedObject&)
        if (removed != it) return;                     // not ours to free if the cache did not give it back
        VCall<void>(c.inv, 4, c.owner, it, 4);        // SODestroyed
        VCall<void>(it, 0, 1);                         // scalar deleting dtor -> g_pMemAlloc->Free
    } __except(EXCEPTION_EXECUTE_HANDLER) { DebugLog("hats inv: SEH removing item"); }
}

static int     s_count = 0;            // our items in the cache at the last tick
static HatSlot s_built[kHatSlots];      // what each of ours was built from

bool HatsInv_AnyLive() { return s_count > 0; }

// The REAL item the GC has equipped at (class, loadout position), 0 = none. Our own are skipped:
// this is "what is under ours", whose world wearable the server still puts on the player.
// Equip state as CEconItem keeps it: singleton at +0x60/+0x62 when dirty bit 1 (+0x5F) is set,
// else the custom data's equipped vector (+0x78 -> +0x38 memory, +0x48 count).
__declspec(noinline) static uint64_t RealAtSEH(const InvCtx& c, int cls, int pos)
{
    __try {
        for (int i = 0; i < c.count; ++i) {
            uint8_t* it = static_cast<uint8_t*>(c.objs[i]);
            if (!it) continue;
            const uint64_t id = *reinterpret_cast<uint64_t*>(it + 0x48);
            if ((id >> 32) == kIdHigh) continue;
            if (it[0x5F] & 2) {
                if (*reinterpret_cast<uint16_t*>(it + 0x60) == cls && *reinterpret_cast<uint16_t*>(it + 0x62) == pos) return id;
                continue;
            }
            const uint8_t* cd = *reinterpret_cast<uint8_t**>(it + 0x78);
            if (!cd) continue;
            const uint16_t* eq = *reinterpret_cast<uint16_t* const*>(cd + 0x38);
            const int n = *reinterpret_cast<const int*>(cd + 0x48);
            if (!eq || n <= 0 || n > 64) continue;
            for (int k = 0; k < n; ++k)
                if (eq[k * 2] == cls && eq[k * 2 + 1] == pos) return id;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

uint64_t HatsInv_RealEquippedAt(int cls, int pos)
{
    Resolve();
    if (!s_ready) return 0;
    InvCtx c{};
    return GetCtx(c) ? RealAtSEH(c, cls, pos) : 0;
}

// Game thread (FSN stage 5), menu or in game. `want` false = take every one of ours out.
void HatsInv_Tick(bool want)
{
    static uint32_t s_frame = 0;
    if (want && !s_dirty && (++s_frame % 30) != 0) return;   // twice a second, or now after an equip
    s_dirty = false;
    if (!want && s_count == 0) return;
    Resolve();
    if (!s_ready) return;
    InvCtx c{};
    if (!GetCtx(c)) return;

    int n = 0;
    for (int i = 0; i < kHatSlots; ++i) {
        const HatSlot s = g_hatSlots[i];
        const HatDef* d = (want && s.def > 0) ? Hats_Find(s.def) : nullptr;
        void* it = FindOurs(c, i);
        if (it) {
            const HatSlot& b = s_built[i];
            if (d && ItemDef(it) == d->def && b.def == s.def && b.style == s.style && b.unusual == s.unusual
                  && b.paint == s.paint) {
                if (SyncEquips(it, s)) {
                    VCall<void>(c.inv, 2, c.owner, it, 4);   // SOUpdated -> loadout re-derived
                    DebugLog("hats inv: slot %d equips -> mask 0x%X", i, s.eqMask);
                }
                ++n; continue;
            }
            Remove(c, it);
            DebugLog("hats inv: removed slot %d", i);
            if (!GetCtx(c)) return;                    // the vector may have moved
            continue;                                  // re-added next tick
        }
        if (d && Add(c, i, s, d)) {
            ++n;
            s_built[i] = s;
            DebugLog("hats inv: slot %d def %d '%s' in backpack (id %llx)", i, d->def, d->name,
                     (unsigned long long)FakeId(i));
            if (!GetCtx(c)) return;
        }
    }
    s_count = n;
}

// ---- loadout screen ---------------------------------------------------------------------------
using UpdateEquipped_t = void(__fastcall*)(void*, void*, uint64_t, uint16_t, uint16_t);
static UpdateEquipped_t o_UpdateEquipped = nullptr;

static void __fastcall h_UpdateEquipped(void* mgr, void* inv, uint64_t id, uint16_t cls, uint16_t pos)
{
    MiscHookGuard _g;
    const bool local = !g_eject && g_hatsEnable && g_hatsBackpack && s_invMgr
                    && inv == static_cast<uint8_t*>(s_invMgr) + 0x198 && cls >= 1 && cls <= 9;
    if (!local) { o_UpdateEquipped(mgr, inv, id, cls, pos); return; }

    const bool ours = (id >> 32) == kIdHigh;
    const int  mine = ours ? (int)(id & 0xFFFF) - 0x1000 : -1;
    // Whatever goes into (class, position) now, none of ours stays there.
    bool hadOurs = false;
    for (int i = 0; i < kHatSlots; ++i) {
        HatSlot& s = g_hatSlots[i];
        if ((s.eqMask & 1) && (s.eqMask >> cls & 1) && s.eqPos[cls] == pos) {
            s.eqMask &= (uint16_t)~(1u << cls);
            hadOurs = true;
        }
    }
    if (ours) {
        if (mine >= 0 && mine < kHatSlots && g_hatSlots[mine].def > 0) {
            HatSlot& s = g_hatSlots[mine];
            s.eqMask |= (uint16_t)(1u | (1u << cls));
            s.eqPos[cls] = (uint8_t)pos;
            DebugLog("hats inv: loadout equip slot %d -> class %d pos %d", mine, cls, pos);
        }
        s_dirty = true;
        return;   // the GC never issued this id: nothing to tell it
    }
    if (hadOurs) s_dirty = true;
    // Clearing a position ours was shown in: the screen meant our item, not whatever real item
    // the GC still has there underneath, so the real loadout is left alone.
    if (hadOurs && id == ~0ull) return;
    o_UpdateEquipped(mgr, inv, id, cls, pos);
}

void HatsInv_Install()
{
    Resolve();
    if (!s_ready) return;
    void** vt = nullptr;
    __try { vt = *reinterpret_cast<void***>(s_invMgr); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if (!vt) { DebugLog("hats inv: no CTFInventoryManager vtable -- loadout equip off"); return; }
    // Slot 32, checked on the 11-02 client: movzx eax,word [rsp+28h] / mov [rsp+28h],ax / jmp base.
    const uint8_t* fn = static_cast<const uint8_t*>(vt[32]);
    if (!fn || fn[0] != 0x0F || fn[1] != 0xB7 || fn[2] != 0x44 || fn[3] != 0x24 || fn[4] != 0x28) {
        DebugLog("hats inv: vtable slot 32 is not UpdateInventoryEquippedState -- loadout equip off");
        return;
    }
    InstallHookAt((void*)fn, "hats: UpdateInventoryEquippedState", (void*)h_UpdateEquipped, (void**)&o_UpdateEquipped);
}
