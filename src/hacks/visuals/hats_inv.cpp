#include "../../globals/common.h"
#include "../../globals/globals.h"
#include "hats.h"
#include "skins.h"
#include "skins_pure.h"

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

static constexpr uint32_t kIdHigh     = 0xFADED;   // hats: low = 0x1000 + slot
static constexpr uint32_t kSkinIdHigh = 0xFADEE;   // weapon skins: low = config key
static uint64_t FakeId(int slot) { return (uint64_t(kIdHigh) << 32) | uint64_t(0x1000 + slot); }
static uint64_t SkinId(int key)  { return (uint64_t(kSkinIdHigh) << 32) | uint64_t((uint32_t)key); }
static bool     IsFake(uint64_t id) { return (id >> 32) == kIdHigh || (id >> 32) == kSkinIdHigh; }

using CEconItemCtor_t  = void*(__fastcall*)(void*);
using FindTypeCache_t  = void*(__fastcall*)(void*, int);
using PlatAlloc_t      = void*(__cdecl*)(size_t);
using PlatFree_t       = void(__cdecl*)(void*);

static CEconItemCtor_t s_ctor       = nullptr;
static FindTypeCache_t s_findTC     = nullptr;
static PlatAlloc_t     s_alloc      = nullptr;
static PlatFree_t      s_free       = nullptr;   // optional: without it a replaced block leaks
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
    s_free  = reinterpret_cast<PlatFree_t>(GetProcAddress(tier0, "Plat_Free"));
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

// The item with this id as the cache holds it now, or null.
__declspec(noinline) static void* FindId(const InvCtx& c, uint64_t id)
{
    __try {
        for (int i = 0; i < c.count; ++i)
            if (c.objs[i] && ItemId(c.objs[i]) == id) return c.objs[i];
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}
static void* FindOurs(const InvCtx& c, int slot) { return FindId(c, FakeId(slot)); }

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

static constexpr int kEqCap = 10;

// Custom data with room for every class and exactly these attributes, so nothing ever has to
// grow it. All three blocks come from Plat_Alloc = g_pMemAlloc, which is what the game frees
// them with. Every attribute we write is a float or int-bits type, whose unload (the dtor's
// walk) touches nothing.
__declspec(noinline) static void* MakeCustomData(const SkinAttr* attrs, int na, const uint16_t* want, int ne, int minCap = 0)
{
    const int cap = (std::max)((std::max)(na, minCap), 1);
    if (ne > kEqCap) ne = kEqCap;
    uint8_t* cd = static_cast<uint8_t*>(s_alloc(0x58));
    void* eq    = s_alloc(kEqCap * 4);
    void* at    = s_alloc(cap * 16);
    if (!cd || !eq || !at) return nullptr;   // ponytail: leaks a block on a failed alloc
    memset(cd, 0, 0x58); memset(eq, 0, kEqCap * 4); memset(at, 0, cap * 16);
    *reinterpret_cast<void**>(cd + 0x38) = eq;  *reinterpret_cast<int*>(cd + 0x40) = kEqCap;
    *reinterpret_cast<void**>(cd + 0x50) = eq;
    *reinterpret_cast<void**>(cd + 0x00) = at;  *reinterpret_cast<int*>(cd + 0x08) = cap;
    *reinterpret_cast<void**>(cd + 0x18) = at;
    uint8_t* a = static_cast<uint8_t*>(at);
    for (int i = 0; i < na; ++i) {
        *reinterpret_cast<uint16_t*>(a + i * 16) = attrs[i].def;
        *reinterpret_cast<float*>(a + i * 16 + 8) = attrs[i].value;
    }
    *reinterpret_cast<int*>(cd + 0x10) = na;
    *reinterpret_cast<uint64_t*>(cd + 0x28) = ~0ull;   // m_ulOriginalID = INVALID_ITEM_ID (ctor default)
    *reinterpret_cast<uint16_t*>(cd + 0x30) = 1;       // m_unQuantity
    memcpy(eq, want, ne * 4);
    *reinterpret_cast<int*>(cd + 0x48) = ne;
    return cd;
}

// Every attribute a hat item can carry. The row block is made this big up front, so a menu or
// loadout-screen change rewrites the rows in place (SetRows) instead of rebuilding the item.
static constexpr int kHatAttrCap = 5;

static float IntBits(uint32_t v) { float f; memcpy(&f, &v, 4); return f; }

static int HatAttrs(const HatSlot& s, const HatDef* d, SkinAttr* a /* [kHatAttrCap] */)
{
    int na = 0;
    if (s.unusual >= 0) {
        a[na++] = { 134, (float)s.unusual };   // attach particle effect
        // TF2's loadout "use head origin" + effect height (class_loadout_panel.cpp). 520 is
        // stored_as_integer: the row holds the int's bits. Absent, not 0, when off: a present 0
        // makes the preview skip the system's own attachment points (tf_playermodelpanel.cpp:1655).
        if (s.useHead) { a[na++] = { 520, IntBits(1) }; a[na++] = { 519, s.zOff }; }
    }
    if (d->paintable)
        if (const HatPaint* p = Hats_Paint(s.paint)) { a[na++] = { 142, (float)p->rgb }; a[na++] = { 261, (float)p->rgb2 }; }
    return na;
}

static void* MakeHatCustomData(const HatSlot& s, const HatDef* d)
{
    SkinAttr a[kHatAttrCap]; const int na = HatAttrs(s, d, a);
    uint16_t want[18]; const int ne = WantEquips(s, want);
    return MakeCustomData(a, na, want, ne, kHatAttrCap);
}

// Rewrite an item's attribute rows + header in place. false = no room (caller rebuilds the item).
// Old rows are float / int bits, whose unload is nothing: overwriting them leaks nothing.
__declspec(noinline) static bool SetRows(void* it, const SkinAttr* a, int na, uint8_t quality, uint8_t style)
{
    __try {
        uint8_t* b  = static_cast<uint8_t*>(it);
        uint8_t* cd = *reinterpret_cast<uint8_t**>(b + 0x78);
        if (!cd) return false;
        uint8_t* rows = *reinterpret_cast<uint8_t**>(cd + 0x00);
        if (!rows || *reinterpret_cast<int*>(cd + 0x08) < na) return false;
        int* count = reinterpret_cast<int*>(cd + 0x10);
        *count = 0;
        memset(rows, 0, (size_t)na * 16);
        for (int i = 0; i < na; ++i) {
            *reinterpret_cast<uint16_t*>(rows + i * 16) = a[i].def;
            memcpy(rows + i * 16 + 8, &a[i].value, 4);
        }
        *count = na;
        b[0x5B] = quality;
        b[0x5E] = style;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Make the item's equipped list `want` (n pairs). True = it changed (caller sends SOUpdated).
__declspec(noinline) static bool SetEquips(void* it, const uint16_t* want, int n)
{
    __try {
        uint8_t* cd = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(it) + 0x78);
        if (!cd) return false;
        uint16_t* eq = *reinterpret_cast<uint16_t**>(cd + 0x38);
        int* cnt = reinterpret_cast<int*>(cd + 0x48);
        if (!eq || *reinterpret_cast<int*>(cd + 0x40) < n) return false;
        if (*cnt == n && memcmp(eq, want, n * 4) == 0) return false;
        memcpy(eq, want, n * 4);
        *cnt = n;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool SyncEquips(void* it, const HatSlot& s)
{
    uint16_t want[18]; const int n = WantEquips(s, want);
    return SetEquips(it, want, n);
}

// cd = a MakeCustomData block; the item owns it from here (freed with it on a refused add).
__declspec(noinline) static bool AddItem(const InvCtx& c, uint64_t id, int def, uint8_t quality, uint8_t style, void* cd)
{
    const uint32_t pos = FreePosition(c);
    if (!pos) return false;
    void* it = s_alloc(0x80);
    if (!it) return false;
    __try {
        memset(it, 0, 0x80);
        s_ctor(it);
        uint8_t* b = static_cast<uint8_t*>(it);
        *reinterpret_cast<uint64_t*>(b + 0x48) = id;
        *reinterpret_cast<uint32_t*>(b + 0x50) = c.account;
        *reinterpret_cast<uint32_t*>(b + 0x54) = 0x80000000u | pos;
        *reinterpret_cast<uint16_t*>(b + 0x58) = (uint16_t)def;
        b[0x5A] = 1;                                   // level
        b[0x5B] = quality;
        b[0x5D] = 0;                                   // kEconItemOrigin_Drop
        b[0x5E] = style;
        *reinterpret_cast<void**>(b + 0x78) = cd;      // equipped list + attributes
        // AddObject (slot 1), then tell the inventory like the GC would. A refused add means the
        // cache did not take it: nothing else holds the item, so it is ours to free.
        if (!VCall<bool>(c.tc, 1, it)) { VCall<void>(it, 0, 1); return false; }
        VCall<void>(c.inv, 0, c.owner, it, 4);        // SOCreated(owner, item, eSOCacheEvent_Incremental)
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DebugLog("hats inv: SEH adding id %llx def %d", (unsigned long long)id, def);
        return false;
    }
}

static bool Add(const InvCtx& c, int slot, const HatSlot& s, const HatDef* d)
{
    return AddItem(c, FakeId(slot), d->def, s.unusual >= 0 ? 5 : 6,   // AE_UNUSUAL : AE_UNIQUE
                   (uint8_t)((s.style >= 0 && s.style < d->nStyles) ? s.style : 0), MakeHatCustomData(s, d));
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

// (class, position) pairs an item is equipped in, as CEconItem keeps them: singleton at
// +0x60/+0x62 when dirty bit 1 (+0x5F) is set, else the custom data's equipped vector
// (+0x78 -> +0x38 memory, +0x48 count). Raw reads: the caller holds the SEH frame.
static int EquipsOf(const uint8_t* it, uint16_t* out, int cap /* pairs */)
{
    if (it[0x5F] & 2) {
        out[0] = *reinterpret_cast<const uint16_t*>(it + 0x60);
        out[1] = *reinterpret_cast<const uint16_t*>(it + 0x62);
        return 1;
    }
    const uint8_t* cd = *reinterpret_cast<uint8_t* const*>(it + 0x78);
    if (!cd) return 0;
    const uint16_t* eq = *reinterpret_cast<uint16_t* const*>(cd + 0x38);
    int n = *reinterpret_cast<const int*>(cd + 0x48);
    if (!eq || n <= 0 || n > 64) return 0;
    if (n > cap) n = cap;
    memcpy(out, eq, n * 4);
    return n;
}

// The REAL item the GC has equipped at (class, loadout position), 0 = none. Our own are skipped:
// this is "what is under ours", whose world wearable the server still puts on the player.
__declspec(noinline) static uint64_t RealAtSEH(const InvCtx& c, int cls, int pos)
{
    __try {
        for (int i = 0; i < c.count; ++i) {
            uint8_t* it = static_cast<uint8_t*>(c.objs[i]);
            if (!it) continue;
            const uint64_t id = *reinterpret_cast<uint64_t*>(it + 0x48);
            if (IsFake(id)) continue;
            uint16_t eq[128]; const int n = EquipsOf(it, eq, 64);
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
            HatSlot& b = s_built[i];
            if (d && ItemDef(it) == d->def && b.def == s.def) {
                bool upd = SyncEquips(it, s);
                if (upd) DebugLog("hats inv: slot %d equips -> mask 0x%X", i, s.eqMask);
                if (b.style != s.style || b.unusual != s.unusual || b.paint != s.paint
                    || b.useHead != s.useHead || b.zOff != s.zOff) {
                    // Same item, new look: rows rewritten in place, so the loadout screen keeps
                    // the item (its options panel, equip state, preview) and just re-reads it.
                    SkinAttr a[kHatAttrCap]; const int na = HatAttrs(s, d, a);
                    const uint8_t st = (uint8_t)((s.style >= 0 && s.style < d->nStyles) ? s.style : 0);
                    if (!SetRows(it, a, na, s.unusual >= 0 ? 5 : 6, st)) goto rebuild;   // AE_UNUSUAL : AE_UNIQUE
                    DebugLog("hats inv: slot %d look -> style %d unu %d paint %d head %d z %.2f",
                             i, s.style, s.unusual, s.paint, s.useHead, s.zOff);
                    b = s;
                    upd = true;
                }
                if (upd) VCall<void>(c.inv, 2, c.owner, it, 4);   // SOUpdated -> loadout + preview re-read
                ++n; continue;
            }
        rebuild:
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

// ==== weapon skins: backpack + loadout ==========================================================
// One copy per enabled skinchanger weapon, built by the same rules (Skins_BuildAttrs) the weapon
// in hand gets, so TF2's own name generator (econ_item_description.cpp:610) reads it the way it
// reads a real one: quality 11 + attr 214 = "Strange <rank>", 2025/2014/2013 = "(Specialized /
// Professional) Killstreak", 2027 = "Australium", 2053 = "Festivized", 834 / a 15xxx def = the
// war paint, 5 + 134 = unusual.
//
// Loadout: the copy is equipped on exactly the (class, position) pairs where the GC's real loadout
// has the weapon it skins -- a real item with the same config key, or, in an empty position, the
// class's stock weapon there. The last SOUpdated wins m_LoadoutItems
// (CTFPlayerInventory::UpdateEquipStateForClass, tf_item_inventory.cpp:1741), so the copy is
// re-announced whenever the GC updates the real item under it (a strange kill, a rename), and the
// real item is re-announced when the copy leaves its position. The GC is never told: the server
// keeps handing out the real weapon and the skinchanger reskins it in hand.
//
// Stock weapon per (class, position), from items_game.txt item_slot + used_by_classes (checked
// 11-07), -1 = none. Classes 1 scout, 2 sniper, 3 soldier, 4 demoman, 5 medic, 6 heavy, 7 pyro,
// 8 spy, 9 engineer; positions 0 primary .. 6 pda2 (tf_item_constants.h:52).
static const int16_t kStock[10][7] = {
    { -1, -1, -1, -1,  -1, -1, -1 },
    { 13, 23,  0, -1,  -1, -1, -1 },   // scout
    { 14, 16,  3, -1,  -1, -1, -1 },   // sniper
    { 18, 10,  6, -1,  -1, -1, -1 },   // soldier
    { 19, 20,  1, -1,  -1, -1, -1 },   // demoman
    { 17, 29,  8, -1,  -1, -1, -1 },   // medic
    { 15, 11,  5, -1,  -1, -1, -1 },   // heavy
    { 21, 12,  2, -1,  -1, -1, -1 },   // pyro
    { -1, 24,  4, -1, 735, 27, 30 },   // spy
    {  9, 22,  7, -1,  28, 25, 26 },   // engineer
};

struct RealAt { uint64_t id; int key; uint32_t hash; };   // the GC's real item at a weapon position

static uint32_t Fnv(uint32_t h, const void* p, size_t n)
{
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 16777619u;
    return h;
}

// The real item at every (class, weapon position). Its hash covers the header (position, def,
// level, quality, flags) and the attribute array: everything a GC update can change.
__declspec(noinline) static bool ScanRealWeapons(const InvCtx& c, RealAt (&occ)[10][7])
{
    memset(occ, 0, sizeof(occ));
    __try {
        for (int i = 0; i < c.count; ++i) {
            const uint8_t* it = static_cast<const uint8_t*>(c.objs[i]);
            if (!it) continue;
            const uint64_t id = *reinterpret_cast<const uint64_t*>(it + 0x48);
            if (IsFake(id)) continue;
            uint16_t eq[128]; const int n = EquipsOf(it, eq, 64);
            int key = -1; uint32_t h = 0;
            for (int k = 0; k < n; ++k) {
                const int cls = eq[k * 2], pos = eq[k * 2 + 1];
                if (cls < 1 || cls > 9 || pos > 6) continue;
                if (key < 0) {
                    key = Skins_ConfigKey(*reinterpret_cast<const uint16_t*>(it + 0x58));
                    h = Fnv(2166136261u, it + 0x48, 0x18);
                    if (const uint8_t* cd = *reinterpret_cast<uint8_t* const*>(it + 0x78)) {
                        const uint8_t* at = *reinterpret_cast<uint8_t* const*>(cd);   // attribute_t, 16 bytes
                        const int na = *reinterpret_cast<const int*>(cd + 0x10);
                        if (at && na > 0 && na <= 256) h = Fnv(h, at, (size_t)na * 16);
                    }
                }
                occ[cls][pos] = { id, key, h };
            }
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Positions the copy of `key` belongs in: the ones holding that weapon's STOCK version, i.e. no
// owned item there to patch (an owned one is patched itself, see below). *under = hash of the
// real items it sits on (none today; kept so a GC change there re-asserts the copy).
static int SkinEquips(const RealAt (&occ)[10][7], int key, uint16_t* out, uint32_t* under)
{
    int n = 0;
    uint32_t h = 2166136261u;
    for (int cls = 1; cls <= 9; ++cls)
        for (int pos = 0; pos <= 6; ++pos) {
            const RealAt& r = occ[cls][pos];
            const int k = r.id ? -1 : (kStock[cls][pos] >= 0 ? Skins_ConfigKey(kStock[cls][pos]) : 0);
            if (k != key || n >= kEqCap) continue;
            out[n * 2] = (uint16_t)cls; out[n * 2 + 1] = (uint16_t)pos; ++n;
            h = Fnv(h, &r, sizeof(r));
        }
    *under = h;
    return n;
}

static void Reannounce(const InvCtx& c, uint64_t id)
{
    if (void* it = FindId(c, id)) VCall<void>(c.inv, 2, c.owner, it, 4);   // SOUpdated -> loadout re-derived
}

// Positions `was` held that `now` does not: the real item there is the loadout's again.
// (ItemIsBeingRemoved / UpdateEquipStateForClass left the stock item there, not it.)
static void GiveBack(const InvCtx& c, const RealAt (&occ)[10][7], const uint16_t* was, int nWas,
                     const uint16_t* now, int nNow)
{
    for (int i = 0; i < nWas; ++i) {
        const int cls = was[i * 2], pos = was[i * 2 + 1];
        bool kept = false;
        for (int j = 0; j < nNow && !kept; ++j) kept = (now[j * 2] == cls && now[j * 2 + 1] == pos);
        if (!kept && cls >= 1 && cls <= 9 && pos <= 6 && occ[cls][pos].id) Reannounce(c, occ[cls][pos].id);
    }
}

struct SkinCopy { SkinCfg built; int def; uint16_t eq[kEqCap * 2]; int neq; uint32_t under; };
static std::unordered_map<int, SkinCopy> s_skins;   // config key -> our copy as built (game thread)
static volatile bool s_skDirty = false;             // a loadout pick of ours: tick now

static bool AddSkin(const InvCtx& c, int key, const SkinCopy& s)
{
    SkinAttr a[kSkinAttrCap];
    const int na = Skins_BuildAttrs(s.built, a, kSkinAttrCap, true);
    void* cd = MakeCustomData(a, na, s.eq, s.neq);
    if (!cd) return false;
    const uint8_t style = (s.built.reskinDef > 0 && !s.built.australium) ? (uint8_t)s.built.reskinStyle : 0;
    return AddItem(c, SkinId(key), s.def, (uint8_t)SkinPure_InvQuality(s.built), style, cd);
}

// ---- an item you really own: patched in place -------------------------------------------------
// The owned item EQUIPPED in a weapon slot (a Strange Kunai with a name tag) gets the config of the
// weapon it is laid over, so it keeps its id, name, kill count, quality and loadout spot and gains
// ours on top. Unequipped copies of the same weapon are never touched; unequip it and it is undone.
// Only rows of our own defs are touched, all float / int typed (skins_pure.h SkinPure_PatchApply);
// the item's pointer-valued rows (its name) are moved at most, never copied, so every allocation
// keeps exactly one owner. A GC update (a kill on a strange) runs CEconItem::operator=, which keeps
// the custom data and overwrites per def: its values win, ours go back on top next tick. A
// resubscribe deletes the custom data: a new pointer, nothing of ours is in it, start over.
// Header too: def (only when the config swaps it -- reskin, festive, war paint def) and quality
// (SkinPure_MergeQuality). Undo puts back exactly what was there, unless the GC changed it since.
struct RealPatch {
    uint64_t   id = 0; void* item = nullptr; void* cd = nullptr;
    int        key = 0;   // config it was patched for (a patched reskin def can stop mapping back)
    PatchState st;
    bool       hdr = false;
    uint16_t   realDef = 0, wroteDef = 0;
    uint8_t    realQ = 0, wroteQ = 0;
};
static std::unordered_map<uint64_t, RealPatch> s_real;   // owned item id -> what we laid over it

// Custom data for an item that has none, the way CEconItem::EnsureCustomDataExists builds it
// (econ_item.cpp:1303): the equip and attribute singletons move in and their dirty bits clear,
// so every allocation still has one owner. Valid state for the game; never undone.
static uint8_t* EnsureCustomData(uint8_t* b)
{
    uint8_t* cd = static_cast<uint8_t*>(MakeCustomData(nullptr, 0, nullptr, 0));
    if (!cd) return nullptr;
    if (b[0x5F] & 2) {   // m_bHasEquipSingleton -> m_vecEquipped
        memcpy(*reinterpret_cast<uint8_t**>(cd + 0x38), b + 0x60, 4);
        *reinterpret_cast<int*>(cd + 0x48) = 1;
    }
    if (b[0x5F] & 4) {   // m_bHasAttribSingleton -> m_vecAttributes (cap 1, MakeCustomData)
        memcpy(*reinterpret_cast<uint8_t**>(cd + 0x00), b + 0x68, 16);
        *reinterpret_cast<int*>(cd + 0x10) = 1;
    }
    b[0x5F] &= (uint8_t)~6u;
    *reinterpret_cast<uint8_t**>(b + 0x78) = cd;
    return cd;
}

// Room for `need` more rows. The rows move, never copy: one owner per allocation.
static bool GrowRows(uint8_t* cd, int need)
{
    const int count = *reinterpret_cast<int*>(cd + 0x10);
    const int cap   = *reinterpret_cast<int*>(cd + 0x08);
    if (count + need <= cap) return true;
    if (*reinterpret_cast<int*>(cd + 0x0C) < 0) return false;   // external CUtlMemory: not ours to swap
    const int ncap = count + need + 4;
    uint8_t* mem = static_cast<uint8_t*>(s_alloc((size_t)ncap * 16));
    if (!mem) return false;
    uint8_t* old = *reinterpret_cast<uint8_t**>(cd + 0x00);
    memset(mem, 0, (size_t)ncap * 16);
    if (old && count > 0) memcpy(mem, old, (size_t)count * 16);
    *reinterpret_cast<uint8_t**>(cd + 0x00) = mem;
    *reinterpret_cast<int*>(cd + 0x08)      = ncap;
    *reinterpret_cast<uint8_t**>(cd + 0x18) = mem;
    if (old && s_free) s_free(old);   // same g_pMemAlloc heap; no Plat_Free = a small leak
    return true;
}

static AttrRows RowsOf(uint8_t* cd)
{
    return { *reinterpret_cast<uint8_t**>(cd + 0x00), reinterpret_cast<int*>(cd + 0x10),
             *reinterpret_cast<int*>(cd + 0x08) };
}

// true = the item changed (caller sends SOUpdated).
__declspec(noinline) static bool PatchRealSEH(uint8_t* b, RealPatch& p, int key, const SkinCfg& cfg)
{
    __try {
        const uint64_t id = *reinterpret_cast<uint64_t*>(b + 0x48);
        if (p.item != b || p.id != id) { p.item = b; p.id = id; p.cd = nullptr; p.st.n = 0; p.hdr = false; }
        uint8_t* cd = *reinterpret_cast<uint8_t**>(b + 0x78);
        if (!cd && !(cd = EnsureCustomData(b))) return false;
        if (cd != p.cd) { p.cd = cd; p.st.n = 0; }   // new custom data: nothing of ours in it

        SkinAttr a[kSkinAttrCap];
        const int na = Skins_BuildAttrs(cfg, a, kSkinAttrCap, true);
        // A real kill count stays: our stat clock only ADDS one to an item that has none.
        AttrRows rows = RowsOf(cd);
        bool ours214 = false;
        for (int j = 0; j < p.st.n; ++j) if (p.st.r[j].def == 214 && !p.st.r[j].had) ours214 = true;
        const bool keepReal214 = Rows_Find(rows, 214) >= 0 && !ours214;
        uint16_t defs[kSkinAttrCap]; uint32_t vals[kSkinAttrCap]; int n = 0;
        for (int i = 0; i < na; ++i) {
            if (keepReal214 && (a[i].def == 214 || a[i].def == 292)) continue;
            defs[n] = a[i].def; memcpy(&vals[n], &a[i].value, 4); ++n;
        }
        if (!GrowRows(cd, SkinPure_PatchNeed(rows, defs, n))) return false;
        rows = RowsOf(cd);
        bool changed = SkinPure_PatchApply(rows, p.st, defs, vals, n) > 0;

        // header: def @0x58, quality @0x5B. A value that is not what we wrote is the GC's.
        uint16_t& def = *reinterpret_cast<uint16_t*>(b + 0x58);
        uint8_t&  q   = b[0x5B];
        if (!p.hdr || def != p.wroteDef) p.realDef = def;
        if (!p.hdr || q   != p.wroteQ)   p.realQ   = q;
        p.hdr = true;
        const int invDef = Skins_InvDef(key, cfg);
        const uint16_t wantDef = (invDef != key) ? (uint16_t)invDef : p.realDef;
        const uint8_t  wantQ   = (uint8_t)SkinPure_MergeQuality(p.realQ, SkinPure_InvQuality(cfg));
        if (def != wantDef) { def = wantDef; changed = true; }
        if (q   != wantQ)   { q   = wantQ;   changed = true; }
        p.wroteDef = wantDef; p.wroteQ = wantQ;
        return changed;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DebugLog("skins inv: SEH patching real item for key %d", key);
        return false;
    }
}

// true = the item changed.
__declspec(noinline) static bool RestoreRealSEH(uint8_t* b, RealPatch& p)
{
    __try {
        if (p.item != b || *reinterpret_cast<uint64_t*>(b + 0x48) != p.id) return false;
        bool changed = false;
        uint8_t* cd = *reinterpret_cast<uint8_t**>(b + 0x78);
        if (cd && cd == p.cd) changed = SkinPure_PatchRestore(RowsOf(cd), p.st) > 0;
        if (p.hdr) {
            uint16_t& def = *reinterpret_cast<uint16_t*>(b + 0x58);
            if (def == p.wroteDef && def != p.realDef) { def = p.realDef; changed = true; }
            if (b[0x5B] == p.wroteQ && b[0x5B] != p.realQ) { b[0x5B] = p.realQ; changed = true; }
        }
        return changed;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void PatchReal(const InvCtx& c, uint64_t id, int key, const SkinCfg& cfg, void* it)
{
    RealPatch& p = s_real[id];
    const bool fresh = p.item != it;
    p.key = key;
    if (PatchRealSEH(static_cast<uint8_t*>(it), p, key, cfg)) {
        VCall<void>(c.inv, 2, c.owner, it, 4);   // SOUpdated: name, quality, icon re-read
        if (fresh) DebugLog("skins inv: key %d laid over equipped item %llx", key, (unsigned long long)id);
    }
}

static void UnpatchReal(const InvCtx& c, RealPatch& p)
{
    if (void* it = FindId(c, p.id))
        if (it == p.item && RestoreRealSEH(static_cast<uint8_t*>(it), p)) {
            VCall<void>(c.inv, 2, c.owner, it, 4);
            DebugLog("skins inv: key %d taken off item %llx", p.key, (unsigned long long)p.id);
        }
}

// Game thread (FSN stage 5), menu or in game. `want` false = take every copy out.
void SkinsInv_Tick(bool want)
{
    static uint32_t s_frame = 0;
    if (want && !s_skDirty && (++s_frame % 30) != 0) return;   // twice a second, or now after a pick
    s_skDirty = false;
    if (!want && s_skins.empty() && s_real.empty()) return;
    Resolve();
    if (!s_ready) return;
    InvCtx c{};
    if (!GetCtx(c)) return;
    static RealAt occ[10][7];
    if (!ScanRealWeapons(c, occ)) return;
    for (auto& row : occ)
        for (RealAt& r : row)
            if (r.id) { auto t = s_real.find(r.id); if (t != s_real.end()) r.key = t->second.key; }

    // 1. The owned item in each weapon slot gets its own weapon's config. Nothing else is patched.
    uint64_t done[64]; int nDone = 0;
    for (int cls = 1; want && cls <= 9; ++cls)
        for (int pos = 0; pos <= 6; ++pos) {
            const RealAt& r = occ[cls][pos];
            if (!r.id || r.key <= 0) continue;
            bool seen = false;
            for (int i = 0; i < nDone && !seen; ++i) seen = (done[i] == r.id);
            if (seen) continue;
            auto f = g_skinCfgs.find(r.key);   // keys sealed: find is safe against menu edits
            if (f == g_skinCfgs.end()) continue;
            const SkinCfg cfg = f->second;
            if (!cfg.enabled) continue;
            void* it = FindId(c, r.id);
            if (!it) continue;
            PatchReal(c, r.id, r.key, cfg, it);
            done[nDone++] = r.id;
        }
    // 2. Anything patched before that is no longer equipped (or wanted): put back.
    for (auto t = s_real.begin(); t != s_real.end(); ) {
        bool keep = false;
        for (int i = 0; i < nDone && !keep; ++i) keep = (done[i] == t->first);
        if (keep) { ++t; continue; }
        UnpatchReal(c, t->second);
        t = s_real.erase(t);
    }

    // 3. Copies: only for slots holding the stock weapon (no owned item there to patch).
    // Keys are sealed at Skins_Install, so iterating while the menu edits values is safe.
    for (const auto& kv : g_skinCfgs) {
        const int     key = kv.first;
        const SkinCfg cfg = kv.second;   // copy: the menu writes it from the render thread
        auto st = s_skins.find(key);
        SkinCopy now{ cfg, Skins_InvDef(key, cfg), {}, 0, 0 };
        bool on = want && cfg.enabled;
        if (on) { now.neq = SkinEquips(occ, key, now.eq, &now.under); on = now.neq > 0; }
        if (!on && st == s_skins.end()) continue;
        void* it = FindId(c, SkinId(key));
        if (!on) {
            if (it) {
                Remove(c, it);
                if (!GetCtx(c)) return;
                GiveBack(c, occ, st->second.eq, st->second.neq, nullptr, 0);
                DebugLog("skins inv: removed key %d", key);
            }
            s_skins.erase(st);
            continue;
        }

        if (it && st != s_skins.end() && st->second.def == now.def && SkinPure_Same(st->second.built, cfg)) {
            SkinCopy& b = st->second;
            if (SetEquips(it, now.eq, now.neq)) {
                VCall<void>(c.inv, 2, c.owner, it, 4);   // SOUpdated: ours takes its positions
                GiveBack(c, occ, b.eq, b.neq, now.eq, now.neq);
                DebugLog("skins inv: key %d now in %d loadout position(s)", key, now.neq);
            } else if (now.neq > 0 && now.under != b.under) {
                VCall<void>(c.inv, 2, c.owner, it, 4);   // the GC updated a real item under ours: take it back
            }
            b = now;
            continue;
        }

        // New, or built off an older config: (re)built here. The new copy takes the same
        // positions in its SOCreated, so a rebuild needs no GiveBack.
        if (it) {
            Remove(c, it);
            if (!GetCtx(c)) return;
        }
        if (AddSkin(c, key, now)) {
            s_skins[key] = now;
            DebugLog("skins inv: key %d def %d quality %d in backpack, %d loadout position(s) (id %llx)",
                     key, now.def, SkinPure_InvQuality(cfg), now.neq, (unsigned long long)SkinId(key));
        } else {
            if (it && st != s_skins.end()) GiveBack(c, occ, st->second.eq, st->second.neq, nullptr, 0);
            if (st != s_skins.end()) s_skins.erase(st);
            DebugLog("skins inv: key %d def %d NOT added", key, now.def);
        }
        if (!GetCtx(c)) return;   // the vector may have moved
    }
}

// ---- loadout screen ---------------------------------------------------------------------------
using UpdateEquipped_t = void(__fastcall*)(void*, void*, uint64_t, uint16_t, uint16_t);
static UpdateEquipped_t o_UpdateEquipped = nullptr;

// A real item with this config key: the one already at (cls, pos) (*already), else one equipped
// somewhere, else any. 0 = none.
__declspec(noinline) static uint64_t RealOfKey(const InvCtx& c, int key, int cls, int pos, bool* already)
{
    uint64_t best = 0; bool bestEquipped = false;
    *already = false;
    __try {
        for (int i = 0; i < c.count; ++i) {
            const uint8_t* it = static_cast<const uint8_t*>(c.objs[i]);
            if (!it) continue;
            const uint64_t id = *reinterpret_cast<const uint64_t*>(it + 0x48);
            if (IsFake(id) || Skins_ConfigKey(*reinterpret_cast<const uint16_t*>(it + 0x58)) != key) continue;
            uint16_t eq[128]; const int n = EquipsOf(it, eq, 64);
            for (int k = 0; k < n; ++k)
                if (eq[k * 2] == cls && eq[k * 2 + 1] == pos) { *already = true; return id; }
            if (!best || (!bestEquipped && n > 0)) { best = id; bestEquipped = n > 0; }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return best;
}

// Our weapon copy picked in the loadout screen: equip the weapon it skins, for real, so the server
// hands it out. A real item of it, else the class's stock weapon in that position. The copy then
// follows on the next tick, the way it follows any loadout change. The GC refuses a real item the
// class cannot use, same as it would for any pick.
static void EquipUnderlying(void* mgr, void* inv, int key, uint16_t cls, uint16_t pos)
{
    InvCtx c{};
    if (!GetCtx(c)) return;
    bool already = false;
    const uint64_t real = RealOfKey(c, key, cls, pos, &already);
    if (already) return;
    if (real) {
        o_UpdateEquipped(mgr, inv, real, cls, pos);
        DebugLog("skins inv: picked key %d -> equip real %llx at class %d pos %d", key, (unsigned long long)real, cls, pos);
        return;
    }
    const int stock = kStock[cls][pos];
    if (stock >= 0 && Skins_ConfigKey(stock) == key) {
        o_UpdateEquipped(mgr, inv, ~0ull, cls, pos);   // INVALID_ITEM_ID = the stock weapon
        DebugLog("skins inv: picked key %d -> stock at class %d pos %d", key, cls, pos);
    }
}

static void __fastcall h_UpdateEquipped(void* mgr, void* inv, uint64_t id, uint16_t cls, uint16_t pos)
{
    MiscHookGuard _g;
    if ((id >> 32) == kSkinIdHigh) {   // a weapon skin copy: never to the GC, it did not issue this id
        if (!g_eject && s_invMgr && inv == static_cast<uint8_t*>(s_invMgr) + 0x198 && cls >= 1 && cls <= 9 && pos <= 6)
            EquipUnderlying(mgr, inv, (int)(uint32_t)id, cls, pos);
        s_skDirty = true;
        return;
    }
    const bool local =!g_eject && g_hatsEnable && g_hatsBackpack && s_invMgr
                    && inv == static_cast<uint8_t*>(s_invMgr) + 0x198 && cls >= 1 && cls <= 9;
    if (!local) { o_UpdateEquipped(mgr, inv, id, cls, pos); return; }

    const bool ours = (id >> 32) == kIdHigh;
    const int  mine = ours ? (int)(id & 0xFFFF) - 0x1000 : -1;
    // The GC's half of an equip, done here and now, in the order the GC would: the screen rebuilds
    // its model preview and its equip-region conflict masks off m_LoadoutItems the moment this call
    // returns (class_loadout_panel.cpp), so the equipped lists + SOUpdated cannot wait for the tick.
    InvCtx c{};
    const bool ctx = GetCtx(c);
    auto syncNow = [&](int slot) {
        if (!ctx) { s_dirty = true; return; }
        if (void* it = FindOurs(c, slot))
            if (SyncEquips(it, g_hatSlots[slot])) VCall<void>(c.inv, 2, c.owner, it, 4);   // SOUpdated
    };
    // Whatever goes into (class, position) now, none of ours stays there.
    bool hadOurs = false;
    for (int i = 0; i < kHatSlots; ++i) {
        HatSlot& s = g_hatSlots[i];
        if ((s.eqMask & 1) && (s.eqMask >> cls & 1) && s.eqPos[cls] == pos) {
            s.eqMask &= (uint16_t)~(1u << cls);
            hadOurs = true;
            syncNow(i);
        }
    }
    if (ours) {
        if (mine >= 0 && mine < kHatSlots && g_hatSlots[mine].def > 0) {
            HatSlot& s = g_hatSlots[mine];
            // The real item in that position is displaced for real -- the GC's own "clear this
            // slot" message (INVALID_ITEM_ID, econ_item_inventory.cpp:776), the same thing
            // equipping any other hat over it does. The server stops dressing you in it at the
            // next spawn / resupply; until then Hats_SkipDraw hides its world model.
            const uint64_t real = ctx ? RealAtSEH(c, cls, pos) : 0;
            if (real) {
                o_UpdateEquipped(mgr, inv, ~0ull, cls, pos);
                DebugLog("hats inv: slot %d displaces real item %llx at class %d pos %d", mine, (unsigned long long)real, cls, pos);
            }
            s.eqMask |= (uint16_t)(1u | (1u << cls));
            s.eqPos[cls] = (uint8_t)pos;
            syncNow(mine);
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

// ---- loadout screen: unusual effect position ----------------------------------------------------
// TF2's item options panel (class_loadout_panel.cpp) has a "use head origin" box and an effect
// height slider for an item with an unusual. Both only message the GC (k_EMsgGCSetHatEffect-
// UseHeadOrigin 1088 / k_EMsgGCSetItemEffectVerticalOffset 1087) and wait for it to rewrite the
// item; the GC never issued our ids, so nothing came back and the controls did nothing. Every GC
// message leaves through ISteamGameCoordinator::SendMessage (slot 0): ours are taken off there and
// done locally -- the slot changes and the tick rewrites the item in place (SetRows + SOUpdated),
// which is the GC's reply as far as the screen can tell.
// Wire: [u32 emsg | 0x80000000][u32 header len][CMsgProtoBufHeader][body] (gcsdk/gcmsg.h
// ProtoBufMsgHeader_t), body = { uint64 item_id = 1; float offset = 2 | bool use_head = 2 }
// (base_gcmessages.proto:867/876).
using GcSend_t = int(__fastcall*)(void*, uint32_t, const void*, uint32_t);
static GcSend_t o_GcSend = nullptr;

__declspec(noinline) static bool ParseGcSEH(uint32_t type, const void* data, uint32_t size, HatGcMsg& m)
{
    __try { return Hat_ParseGcEffect(type, data, size, m); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// true = one of ours, handled: never sent.
static bool GcEffectMsg(uint32_t type, const void* data, uint32_t size)
{
    HatGcMsg m;
    if (!ParseGcSEH(type, data, size, m) || (m.id >> 32) != kIdHigh) return false;
    const int slot = (int)(m.id & 0xFFFF) - 0x1000;
    if (slot < 0 || slot >= kHatSlots || g_hatSlots[slot].def <= 0) return true;   // ours, gone: drop it
    HatSlot& s = g_hatSlots[slot];
    if (m.type == 1088) s.useHead = m.useHead ? 1 : 0;
    else s.zOff = (m.offset != m.offset) ? 0.f : (std::min)((std::max)(m.offset, -8.f), 8.f);   // TF2's slider range
    s_dirty = true;
    DebugLog("hats inv: loadout screen slot %d %s -> head %d z %.2f", slot,
             m.type == 1088 ? "use head" : "offset", s.useHead, s.zOff);
    return true;
}

static int __fastcall h_GcSend(void* gc, uint32_t type, const void* data, uint32_t size)
{
    MiscHookGuard _g;
    if (!g_eject && GcEffectMsg(type, data, size)) return 0;   // k_EGCResultOK
    return o_GcSend(gc, type, data, size);
}

// ISteamGameCoordinator the way TF2 gets it: SteamClient()->GetISteamGenericInterface(user, pipe,
// "SteamGameCoordinator001") (the string is in client.dll); flat exports, no vtable guess there.
static void InstallGcHook()
{
    HMODULE h = GetModuleHandleA("steam_api64.dll");
    if (!h) { DebugLog("hats inv: no steam_api64 -- loadout effect offset off"); return; }
    using Client_t  = void*(__cdecl*)();
    using Handle_t  = int(__cdecl*)();
    using Generic_t = void*(__cdecl*)(void*, int, int, const char*);
    auto client  = reinterpret_cast<Client_t>(GetProcAddress(h, "SteamClient"));
    auto user    = reinterpret_cast<Handle_t>(GetProcAddress(h, "SteamAPI_GetHSteamUser"));
    auto pipe    = reinterpret_cast<Handle_t>(GetProcAddress(h, "SteamAPI_GetHSteamPipe"));
    auto generic = reinterpret_cast<Generic_t>(GetProcAddress(h, "SteamAPI_ISteamClient_GetISteamGenericInterface"));
    void* gc = nullptr; void* fn = nullptr;
    __try {
        void* sc = (client && user && pipe && generic) ? client() : nullptr;
        gc = sc ? generic(sc, user(), pipe(), "SteamGameCoordinator001") : nullptr;
        if (gc) fn = (*reinterpret_cast<void***>(gc))[0];   // SendMessage
    } __except(EXCEPTION_EXECUTE_HANDLER) { gc = nullptr; fn = nullptr; }
    if (!fn) { DebugLog("hats inv: ISteamGameCoordinator not found -- loadout effect offset off"); return; }
    InstallHookAt(fn, "hats: ISteamGameCoordinator::SendMessage", (void*)h_GcSend, (void**)&o_GcSend);
}

void HatsInv_Install()
{
    Resolve();
    if (!s_ready) return;
    InstallGcHook();
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
