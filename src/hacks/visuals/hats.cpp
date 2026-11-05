#include "../../globals/common.h"
#include "../../globals/globals.h"
#include "hats.h"
#include "skins.h"
#include "skinshare.h"   // peers' hats go on THEIR players, same lifecycle, slots after ours
#include "../../dependencies/data/hatdata.h"

// ==== hat changer ===============================================================================
// Reference: lmaobox's Hats page, decoded from Desktop\hats\lmaobox_*.bin (strings are xor'd
// per string with the byte before them; FUN_3c97225b/3c9741a9). Its spawn, FUN_3c968966:
//   CreateEntityByName("tf_wearable") -> item id = 0xfaded marker -> m_hOwnerEntity = player
//   -> m_iItemDefinitionIndex = def -> OnDataChanged(CREATED) -> m_bInitialized -> sprintf the
//   per-class model -> modelprecache AddString -> SetModel -> Equip(player) -> m_hMyWearables.
// x64 has no sigs for half of that, so the entity here is made the way the weather one is
// (visuals.cpp Rain_Tick): the CTFWearable ClientClass's CreateFn at a fixed high index, and
// then driven through the engine's own snapshot order (c_baseentity.cpp):
//   PreDataUpdate(CREATED)  -> Spawn() -> CEconWearable::InternalSetPlayerDisplayModel picks
//                              the model off the item + owner class/team (econ_wearable.cpp:66)
//   [netvars as a packet would set them: move parent]
//   PostDataUpdate(CREATED) -> HierarchySetParent(moveparent), ValidateModelIndex
//   OnDataChanged(CREATED)  -> queued by PreDataUpdate, delivered by the original FSN(5)'s
//                              ProcessOnDataChangedEvents: InitializeAttributes, UpdateVisibility
// Drawing is then TF2's own: bonemerged to the player, hidden in first person
// (CEconWearable::ShouldDraw), valid because the owner is the local player and the owner is the
// move parent (CEconEntity::ValidateEntityAttachedToPlayer, econ_entity.cpp:587).
//
// Ownership: the engine destroys every entity in its list on a level change, a disconnect and a
// full update (cl_fullupdate), ours included, and a server entity may take our index. So a hat is
// only "ours" while the entity at its index is the pointer we made AND still carries our marker
// in m_iItemIDLow/High -- checked before every touch. Only ours is ever Released.

bool    g_hatsEnable = false;
HatSlot g_hatSlots[kHatSlots] = {};   // def 0 = empty; the menu's add sets unusual/paint -1

int             Hats_Count()      { return k_hatCount; }
const HatDef*   Hats_At(int i)    { return (i >= 0 && i < k_hatCount) ? &k_hats[i] : nullptr; }
int             Hats_PaintCount() { return k_hatPaintCount; }
const HatPaint* Hats_Paint(int i) { return (i >= 0 && i < k_hatPaintCount) ? &k_hatPaints[i] : nullptr; }

const HatDef* Hats_Find(int def)
{
    // k_hats is sorted by def (the generator walks the defs in order): binary search.
    int lo = 0, hi = k_hatCount - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (k_hats[mid].def == def) return &k_hats[mid];
        if (k_hats[mid].def < def) lo = mid + 1; else hi = mid - 1;
    }
    return nullptr;
}

// Entity indices 1950..2045: ours at 1950..2013 (kHatSlots), then 4 per shared peer (skinshare.cpp,
// 8 peers). The weather entity owns 2047 (visuals.cpp); servers fill from the bottom, and a TF2 map
// that reaches 1950 networked entities is already at the edict limit (StillOurs covers it anyway).
// ponytail: 4 hats per peer, 8 peers -- the index budget under 2047; raise kShareMaxPeers with it.
static constexpr int kFirstIndex   = 1950;
static constexpr int kPeerHatsEach = 4;
static constexpr int kTotalSlots   = kHatSlots + kShareMaxPeers * kPeerHatsEach;
static_assert(kFirstIndex + kTotalSlots < 2047, "hat slots run into the weather entity");
static constexpr int kMarker     = 0xFADED;   // m_iItemIDLow, lbox's own marker; High = our index

// Netvar offsets off DT_TFWearable (FindPropOffset walks the base tables, so the base entity
// ones land at the same C_BaseEntity offsets the player has). 0 = unresolved, -1 = missing.
static struct {
    int modelIndex, teamNum, effects, owner, moveParent, parentAttach, skin;
    int itemDef, quality, level, idHigh, idLow, initialized, attrList, validated;
} s_off;
static ClientClass* s_wearableClass = nullptr;
static bool         s_ready         = false;

static void ResolveOnce()
{
    static int s_tries = 0;
    if (s_ready || s_tries > 60) return;
    ++s_tries;
    if (!s_wearableClass)
        for (ClientClass* c = GetAllClasses(); c; c = c->m_pNext)
            if (c->m_pNetworkName && strcmp(c->m_pNetworkName, "CTFWearable") == 0) { s_wearableClass = c; break; }
    if (!s_wearableClass || !s_wearableClass->m_pCreateFn || !s_wearableClass->m_pRecvTable) return;
    RecvTable* t = s_wearableClass->m_pRecvTable;
    const struct { int* p; const char* n; } k[] = {
        { &s_off.modelIndex,   "m_nModelIndex" },         { &s_off.teamNum,     "m_iTeamNum" },
        { &s_off.effects,      "m_fEffects" },            { &s_off.owner,       "m_hOwnerEntity" },
        { &s_off.moveParent,   "moveparent" },            { &s_off.parentAttach,"m_iParentAttachment" },
        { &s_off.skin,         "m_nSkin" },               { &s_off.itemDef,     "m_iItemDefinitionIndex" },
        { &s_off.quality,      "m_iEntityQuality" },      { &s_off.level,       "m_iEntityLevel" },
        { &s_off.idHigh,       "m_iItemIDHigh" },         { &s_off.idLow,       "m_iItemIDLow" },
        { &s_off.initialized,  "m_bInitialized" },        { &s_off.attrList,    "m_AttributeList" },
        { &s_off.validated,    "m_bValidatedAttachedEntity" },
    };
    bool all = true;
    for (const auto& e : k) {
        const int o = FindPropOffset(t, e.n);
        *e.p = (o > 0) ? o : -1;
        if (*e.p <= 0) { all = false; DebugLog("hats: netvar %s NOT FOUND", e.n); }
    }
    s_ready = all;
    DebugLog("hats: %s (class %p, model 0x%X owner 0x%X parent 0x%X itemdef 0x%X idlow 0x%X attrs 0x%X)",
             all ? "ready" : "netvars missing, feature off", (void*)s_wearableClass, s_off.modelIndex,
             s_off.owner, s_off.moveParent, s_off.itemDef, s_off.idLow, s_off.attrList);
    if (!all) s_tries = 1000;   // a missing netvar is a game change, not a timing issue
}

// ---- raw access, SEH leaves (MSVC: no __try next to an unwinding object) ----------------------
__declspec(noinline) static int RdI(void* p, int off)
{
    __try { return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(p) + off); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
__declspec(noinline) static void WrI(void* p, int off, int v)
{
    __try { *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(p) + off) = v; }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}
// m_iItemDefinitionIndex is an item_definition_index_t (uint16): write only its two bytes.
__declspec(noinline) static void WrU16(void* p, int off, uint16_t v)
{
    __try { *reinterpret_cast<uint16_t*>(reinterpret_cast<uintptr_t>(p) + off) = v; }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}
__declspec(noinline) static void WrB(void* p, int off, bool v)
{
    __try { *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(p) + off) = v; }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}
// IHandleEntity::GetRefEHandle, slot 2 (0 = scalar deleting dtor, 1 = SetRefEHandle).
__declspec(noinline) static unsigned RefHandle(void* pEnt)
{
    __try {
        using Fn = const unsigned*(__fastcall*)(void*);
        const unsigned* h = ((Fn)(*reinterpret_cast<void***>(pEnt))[2])(pEnt);
        return h ? *h : 0xFFFFFFFFu;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0xFFFFFFFFu; }
}
// IClientNetworkable slots, as the weather chain uses them: 1 Release, 6 PreDataUpdate, 7 PostDataUpdate.
__declspec(noinline) static bool NetCall(void* pNet, int slot, bool withArg)
{
    __try {
        void** vt = *reinterpret_cast<void***>(pNet);
        if (withArg) ((void(__fastcall*)(void*, int))vt[slot])(pNet, 0);   // DATA_UPDATE_CREATED
        else         ((void(__fastcall*)(void*))vt[slot])(pNet);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
__declspec(noinline) static void* CallCreate(void* fn, int idx)
{
    __try { return reinterpret_cast<void*(__cdecl*)(int, int)>(fn)(idx, 0); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
static void* Networkable(int idx) { return g_entityList ? VCall<void*>(g_entityList, 0, idx) : nullptr; }

// ---- live state ---------------------------------------------------------------------------------
struct LiveHat {
    void*    ent;       // C_TFWearable*, null = nothing of ours at this index
    HatSlot  cfg;       // what it was built from
    int      cls, team;
    unsigned owner;
    uint32_t bornFrame;
};
static LiveHat  s_live[kTotalSlots];
// Each hat's IClientNetworkable, for the dormancy hook below. Written before the hat is told
// it is transmitted, cleared before it is Released.
static void* volatile s_liveNet[kTotalSlots];

// ---- dormancy -----------------------------------------------------------------------------------
// The engine's CL_MarkEntitiesOutOfPVS walks EVERY entity in the client list after each packet and
// sends NotifyShouldTransmit(SHOULDTRANSMIT_END) to any the server's PVS bits do not name. Ours are
// never named, so every packet made them dormant again, and a dormant server entity is removed from
// the leaf system (UpdateVisibility) -- the "hats diag: dormant=1 leaf=0xFFFF draws=0" lines.
// C_BaseEntity::NotifyShouldTransmit (networkable slot 3) is hooked once, and END is dropped for
// our hats only; every other entity goes straight through.
using NotifyShouldTransmit_t = void(__fastcall*)(void*, int);
static NotifyShouldTransmit_t o_NotifyShouldTransmit = nullptr;
static bool s_nstHooked = false;

static void __fastcall h_NotifyShouldTransmit(void* net, int state)
{
    MiscHookGuard _g;
    if (state == 1 /* SHOULDTRANSMIT_END */ && !g_eject)
        for (int i = 0; i < kTotalSlots; ++i)
            if (s_liveNet[i] == net) return;
    o_NotifyShouldTransmit(net, state);
}

static void HookNotifyShouldTransmit(void* pNet)
{
    if (s_nstHooked) return;
    s_nstHooked = true;
    void* fn = nullptr;
    __try { fn = (*reinterpret_cast<void***>(pNet))[3]; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if (!fn || !InstallHookAt(fn, "hats: NotifyShouldTransmit", (void*)h_NotifyShouldTransmit, (void**)&o_NotifyShouldTransmit))
        DebugLog("hats: NotifyShouldTransmit hook failed -- hats will stay dormant (invisible)");
}
static uint32_t s_frame = 0;
static uint32_t s_freedFrame[kTotalSlots];   // a slot released this frame is not rebuilt until the next

static bool StillOurs(int i)
{
    void* e = s_live[i].ent;
    if (!e) return false;
    const int idx = kFirstIndex + i;
    return GetClientEntity(idx) == e && RdI(e, s_off.idLow) == kMarker && RdI(e, s_off.idHigh) == idx;
}

static void Release(int i, const char* why)
{
    s_liveNet[i] = nullptr;   // END goes through again before the entity dies
    if (StillOurs(i)) {
        if (void* pNet = Networkable(kFirstIndex + i)) NetCall(pNet, 1, false);
        DebugLog("hats: release slot %d def %d (%s)", i, s_live[i].cfg.def, why);
    }
    s_live[i] = {};
    s_freedFrame[i] = s_frame;
}

static bool SameCfg(const HatSlot& a, const HatSlot& b)
{
    return a.def == b.def && a.style == b.style && a.unusual == b.unusual && a.paint == b.paint;
}

static int ClampStyle(const HatDef* d, int s) { return (s >= 0 && s < d->nStyles) ? s : 0; }

static void Spawn(int i, const HatSlot& c, const HatDef* d, int cls, int team, unsigned owner, int mdlIdx)
{
    const int idx = kFirstIndex + i;
    if (GetClientEntity(idx)) return;   // the server got there first: this slot sits out
    void* pNet = CallCreate(s_wearableClass->m_pCreateFn, idx);
    void* e    = pNet ? GetClientEntity(idx) : nullptr;
    if (!e) { DebugLog("hats: CreateFn gave nothing at %d", idx); return; }
    pNet = Networkable(idx);
    if (!pNet) return;

    // What Spawn reads: the item, its owner and team. A packet would have carried these.
    WrI(e, s_off.owner, (int)owner);
    WrI(e, s_off.teamNum, team);
    WrU16(e, s_off.itemDef, (uint16_t)d->def);
    WrI(e, s_off.quality, c.unusual >= 0 ? 5 : 6);   // AE_UNUSUAL : AE_UNIQUE
    WrI(e, s_off.level, 1);
    WrI(e, s_off.idLow, kMarker);
    WrI(e, s_off.idHigh, idx);
    WrB(e, s_off.initialized, true);
    WrB(e, s_off.validated, true);
    WrI(e, s_off.modelIndex, mdlIdx);   // ours; Spawn replaces it with the schema's own pick

    // Runtime attributes. The list has no manager until InitializeAttributes (Spawn), and
    // SetRuntimeAttributeValue only notifies one if there is (econ_item_view.cpp:1910).
    void* attrs = reinterpret_cast<uint8_t*>(e) + s_off.attrList;
    const int style = ClampStyle(d, c.style);
    Skins_SetAttribute(attrs, 542, (float)style);                  // item style override
    if (c.unusual >= 0) Skins_SetAttribute(attrs, 134, (float)c.unusual);   // attach particle effect
    if (d->paintable)
        if (const HatPaint* p = Hats_Paint(c.paint)) {
            Skins_SetAttribute(attrs, 142, (float)p->rgb);              // set item tint RGB
            Skins_SetAttribute(attrs, 261, (float)p->rgb2);             // set item tint RGB 2 (BLU)
        }

    if (!NetCall(pNet, 6, true)) { DebugLog("hats: PreDataUpdate faulted at %d", idx); return; }

    // The packet's half: parent + attachment, bonemerge (Spawn adds it too), skin by team.
    WrI(e, s_off.moveParent, (int)owner);
    WrI(e, s_off.parentAttach, 0);
    WrI(e, s_off.effects, RdI(e, s_off.effects) | 0x001 | 0x080 | 0x200);   // BONEMERGE | _FASTCULL | PARENT_ANIMATES
    WrI(e, s_off.skin, team == 3 ? 1 : 0);
    const int spawnIdx = RdI(e, s_off.modelIndex);
    if (spawnIdx == 0 || spawnIdx == -1) WrI(e, s_off.modelIndex, mdlIdx);

    NetCall(pNet, 7, true);
    HookNotifyShouldTransmit(pNet);
    s_liveNet[i] = pNet;
    // Entering the PVS: IClientNetworkable slot 3 NotifyShouldTransmit(SHOULDTRANSMIT_START = 0) ->
    // SetDormant(false). The ctor starts every entity dormant (c_baseentity.cpp:996) and a dormant
    // server entity is never added to the leaf system, so without this the hat exists and never draws.
    NetCall(pNet, 3, true);

    s_live[i] = { e, c, cls, team, owner, s_frame };
    DebugLog("hats: spawn slot %d idx %d def %d '%s' style %d unu %d paint %d model %d (spawn picked %d) ent %p",
             i, idx, d->def, d->name, style, c.unusual, c.paint, RdI(e, s_off.modelIndex), spawnIdx, e);
}

// ---- bodygroups ---------------------------------------------------------------------------------
// The groups the worn hats hide (visuals.player_bodygroups + the style's
// additional_hidden_bodygroups) as (base, count) pairs on the local player's own model, for the
// DrawModelExecute hook. Rebuilt when the model or the set of worn hats changes.
struct BodyOp { int base, count; };
static BodyOp       s_bodyOps[16];
static volatile int s_bodyOpCount = 0;
static const void*  s_bodyModel   = nullptr;
static volatile int s_bodyEnt     = -1;

__declspec(noinline) static int BuildBodyOps(const void* hdr, const char* const* hides, int nHides, BodyOp* out, int cap)
{
    int n = 0;
    __try {
        const uint8_t* h = static_cast<const uint8_t*>(hdr);
        const int num = *reinterpret_cast<const int*>(h + 232);   // studiohdr_t::numbodyparts
        const int idx = *reinterpret_cast<const int*>(h + 236);   // studiohdr_t::bodypartindex
        if (num <= 0 || num > 64) return 0;
        for (int b = 0; b < num && n < cap; ++b) {
            const int* part = reinterpret_cast<const int*>(h + idx + b * 16);   // mstudiobodyparts_t
            const char* name = reinterpret_cast<const char*>(part) + part[0];
            for (int k = 0; k < nHides; ++k)
                if (Hat_ListHas(hides[k], name)) { out[n++] = { part[2], part[1] }; break; }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return n;
}

static void UpdateBody(int localIdx, void* pLocal)
{
    const char* hides[kHatSlots]; int nHides = 0;
    for (int i = 0; i < kHatSlots; ++i)
        if (s_live[i].ent)
            if (const HatDef* d = Hats_Find(s_live[i].cfg.def)) {
                const char* h = d->styles[ClampStyle(d, s_live[i].cfg.style)].hide;
                if (h && h[0]) hides[nHides++] = h;
            }
    const void* mdl = nHides ? Skins_ModelPtr(RdI(pLocal, s_off.modelIndex)) : nullptr;
    const void* hdr = mdl ? Skins_StudioHdr(mdl) : nullptr;
    BodyOp ops[16];
    const int n = hdr ? BuildBodyOps(hdr, hides, nHides, ops, 16) : 0;
    // The draw hook reads count first: shrink it before rewriting the pairs, grow it after.
    s_bodyOpCount = 0;
    memcpy(s_bodyOps, ops, sizeof(BodyOp) * n);
    s_bodyModel = mdl;
    s_bodyEnt   = localIdx;
    s_bodyOpCount = n;
}

bool Hats_WantBody() { return s_bodyOpCount > 0 && !g_eject; }
bool Hats_IsOurs(int entIdx) { return entIdx >= kFirstIndex && entIdx < kFirstIndex + kTotalSlots; }

int Hats_Body(int entIdx, const void* pModel, int body)
{
    const int n = s_bodyOpCount;
    if (n <= 0 || entIdx != s_bodyEnt || pModel != s_bodyModel) return body;
    for (int i = 0; i < n && i < 16; ++i)
        body = Hat_SetBodygroup(body, s_bodyOps[i].base, s_bodyOps[i].count, 1);
    return body;
}

// ---- diag ---------------------------------------------------------------------------------------
// "hat exists, nothing on screen": the state every draw gate reads, a few lines per spawn.
static volatile long s_drawCount[kTotalSlots];
void Hats_NoteDraw(int entIdx)
{
    if (entIdx >= kFirstIndex && entIdx < kFirstIndex + kTotalSlots) ++s_drawCount[entIdx - kFirstIndex];
}

__declspec(noinline) static void Diag(int i, void* pLocal)
{
    void* e = s_live[i].ent;
    int shouldDraw = -1, rh = -1, dormant = -1, ownerIdx = -1, parentIdx = -1;
    const void* mdl = nullptr;
    const char* mdlName = "?";
    __try {
        void* rend = VCall<void*>(e, 5);                       // IClientUnknown::GetClientRenderable
        if (rend) {
            shouldDraw = VCall<bool>(rend, 3) ? 1 : 0;           // IClientRenderable::ShouldDraw
            const uint16_t* h = VCall<const uint16_t*>(rend, 8); // RenderHandle(): 0xFFFF = not in the leaf system
            rh = h ? *h : -1;
            mdl = VCall<const void*>(rend, 9);                   // GetModel
        }
        if (void* net = Networkable(kFirstIndex + i)) dormant = VCall<bool>(net, 8) ? 1 : 0;   // IsDormant
        ownerIdx  = RdI(e, s_off.owner) & 0xFFF;
        parentIdx = RdI(e, s_off.moveParent) & 0xFFF;
        if (mdl) {
            using Fn = const char*(__fastcall*)(void*, const void*);
            if (g_modelInfo) mdlName = ((Fn)(*(void***)g_modelInfo)[3])(g_modelInfo, mdl);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    DebugLog("hats diag: slot %d idx %d draw=%d leaf=0x%X dormant=%d model=%p '%s' mi=%d fx=0x%X owner=%d parent=%d "
             "local=%d draws=%ld", i, kFirstIndex + i, shouldDraw, rh, dormant, mdl, mdlName ? mdlName : "?",
             RdI(e, s_off.modelIndex), RdI(e, s_off.effects), ownerIdx, parentIdx, GetLocalPlayerIndex(),
             s_drawCount[i]);
    (void)pLocal;
}

// ---- visibility ---------------------------------------------------------------------------------
// The leaf system only learns a renderable started (or stopped) wanting to draw from
// UpdateVisibility(). For a networked entity that call comes from its packets (PostDataUpdate:
// m_bOldShouldDraw != ShouldDraw) and, for wearables, from C_TFPlayer walking m_hMyWearables on a
// third person switch -- ours get neither. So a hat spawned while you were in first person stayed
// out of the leaf system forever ("hats diag: draw=1 leaf=0xFFFF"). Every frame: when ShouldDraw
// and leaf membership disagree, NotifyShouldTransmit(START) = SetDormant(false) = UpdateVisibility.
__declspec(noinline) static void SyncVisibility(int i)
{
    void* e = s_live[i].ent;
    void* net = s_liveNet[i];
    if (!e || !net) return;
    __try {
        void* rend = VCall<void*>(e, 5);                         // IClientUnknown::GetClientRenderable
        if (!rend) return;
        const bool sd = VCall<bool>(rend, 3);                    // ShouldDraw
        const uint16_t* h = VCall<const uint16_t*>(rend, 8);     // RenderHandle
        if (!h) return;
        if (sd != (*h != 0xFFFF)) NetCall(net, 3, true);         // NotifyShouldTransmit(SHOULDTRANSMIT_START)
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- real hat under ours ------------------------------------------------------------------------
// Equipping ours over a real hat in the loadout screen never reaches the GC, so the server keeps
// putting the real one on you and both draw. Its world wearable is found by item id (the GC's
// item at that class + loadout position, hats_inv.cpp) and its draws are skipped.
static volatile int s_hideIdx[kHatSlots];
static volatile int s_hideCount = 0;

bool Hats_SkipDraw(int entIdx)
{
    const int n = s_hideCount;
    for (int i = 0; i < n && i < kHatSlots; ++i) if (s_hideIdx[i] == entIdx) return true;
    return false;
}

__declspec(noinline) static uint64_t EntItemId(void* e)
{
    __try {
        return (uint64_t(uint32_t(RdI(e, s_off.idHigh))) << 32) | uint32_t(RdI(e, s_off.idLow));
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static void UpdateHide(int localIdx, int cls)
{
    uint64_t ids[kHatSlots]; int nid = 0;
    if (g_hatsBackpack && cls >= 1 && cls <= 9)
        for (int i = 0; i < kHatSlots; ++i) {
            const HatSlot& c = g_hatSlots[i];
            if (!s_live[i].ent || !(c.eqMask & 1) || !(c.eqMask >> cls & 1)) continue;
            if (const uint64_t id = HatsInv_RealEquippedAt(cls, c.eqPos[cls])) ids[nid++] = id;
        }
    int out[kHatSlots]; int n = 0;
    if (nid) {
        int highest = VCall<int>(g_entityList, 6);              // IClientEntityList::GetHighestEntityIndex
        if (highest > 2047) highest = 2047;
        for (int idx = 1; idx <= highest && n < kHatSlots; ++idx) {
            if (Hats_IsOurs(idx)) continue;
            void* net = Networkable(idx);
            if (!net || VCall<ClientClass*>(net, 2) != s_wearableClass) continue;   // GetClientClass
            void* e = GetClientEntity(idx);
            if (!e || (RdI(e, s_off.owner) & 0xFFF) != localIdx) continue;
            const uint64_t id = EntItemId(e);
            for (int k = 0; k < nid; ++k) if (id == ids[k]) { out[n++] = idx; break; }
        }
    }
    s_hideCount = 0;   // the draw hook reads the count first
    for (int k = 0; k < n; ++k) s_hideIdx[k] = out[k];
    s_hideCount = n;
}

// ---- per frame ----------------------------------------------------------------------------------
int Hats_LocalClass()
{
    const int idx = IsInGame() ? GetLocalPlayerIndex() : 0;
    return idx > 0 ? GetPlayerClass(idx) : 0;
}

bool Hats_AnyLive()
{
    for (const LiveHat& h : s_live) if (h.ent) return true;
    return HatsInv_AnyLive();
}

// One slot, one frame: keep / release / spawn against what it should be wearing now. `wear` false
// with a live hat releases it; `c` is read only when spawning.
static void TickSlot(int i, const HatSlot& c, const HatDef* d, bool wear, int cls, int team, unsigned owner)
{
    LiveHat& L = s_live[i];
    if (L.ent) {
        if (L.bornFrame == s_frame) return;   // its OnDataChanged is still queued
        if (!wear || !SameCfg(L.cfg, c) || L.cls != cls || L.owner != owner || L.team != team)
            Release(i, !wear ? "not wearable" : "changed");
        return;
    }
    if (!wear || s_freedFrame[i] == s_frame) return;

    char path[260];
    if (!Hat_ModelFor(d->styles[ClampStyle(d, c.style)].mdl, cls, path, sizeof(path))) return;
    // Registered before the entity exists, so Spawn's own SetModel(path) finds it too.
    static int s_badDef[kTotalSlots] = {};
    const int mdlIdx = Skins_ModelIndex(path);
    if (!mdlIdx) {
        if (s_badDef[i] != c.def) { s_badDef[i] = c.def; DebugLog("hats: no model index for '%s' (def %d)", path, c.def); }
        return;
    }
    s_badDef[i] = 0;
    Spawn(i, c, d, cls, team, owner, mdlIdx);
}

// Can `d` be worn by a player of this class on a playing team?
static bool Wearable(const HatDef* d, int cls, int team)
{
    return d && cls >= 1 && cls <= 9 && (d->classMask >> cls & 1) && (team == 2 || team == 3);
}

// m_lifeState == LIFE_ALIVE. A dead player is EF_NODRAW while its entity sits where it died, but
// CTFWearable::ShouldDrawWhenPlayerIsDead is true (tf_item_wearable.h:61), so a hat left on it
// hung in the air over the corpse. TF2's real hats go onto the ragdoll (server-side follow); ours
// come off at death and back on at spawn. ponytail: no ragdoll copy -- CEconEntity::
// ValidateEntityAttachedToPlayer wants the owner in the move-parent chain, which a ragdoll is not.
__declspec(noinline) static bool Alive(void* pEnt)
{
    if (!pEnt || g_offLifeState <= 0) return false;
    __try { return *reinterpret_cast<const uint8_t*>(reinterpret_cast<uintptr_t>(pEnt) + g_offLifeState) == 0; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---- the worn snapshot --------------------------------------------------------------------------
// TF2 applies a loadout at spawn and at the resupply locker (both fire post_inventory_application
// for the player), not the moment you click. So the world hats are built from s_applied, a copy of
// g_hatSlots taken on those events; the menu and the backpack copy (hats_inv.cpp) stay live, like
// TF2's own loadout screen does.
static HatSlot s_applied[kHatSlots];
static bool    s_appliedOnce = false;

static void ApplySnapshot(const char* why)
{
    bool changed = !s_appliedOnce;
    for (int i = 0; i < kHatSlots; ++i)
        if (memcmp(&s_applied[i], &g_hatSlots[i], sizeof(HatSlot)) != 0) { s_applied[i] = g_hatSlots[i]; changed = true; }
    s_appliedOnce = true;
    if (changed) DebugLog("hats: loadout applied (%s)", why);
}

void Hats_OnGameEvent(void* pEvent)
{
    if (!pEvent || g_eject) return;
    __try {
        const char* name = IGameEventVtbl::GetName(pEvent);
        if (!name) return;
        if (strcmp(name, "post_inventory_application") != 0 && strcmp(name, "player_spawn") != 0) return;
        const int localIdx = GetLocalPlayerIndex();
        const int uid = IGameEventVtbl::GetInt(pEvent, "userid", 0);
        if (localIdx < 1 || uid <= 0 || GetPlayerForUserID(uid) != localIdx) return;
        ApplySnapshot(name);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Shared peers (skinshare.cpp): the first kPeerHatsEach hats of each peer's loadout, on that
// player, slots kHatSlots + peer*kPeerHatsEach.. . A peer slot that is empty, gone, or out of the
// PVS releases what it wore. Bodygroup hiding (UpdateBody) is ours only: their real hats draw too.
static void TickPeers()
{
    const bool show = g_shareNet && g_shareRecv;
    for (int p = 0; p < kShareMaxPeers; ++p) {
        const SharePeer* peer = show ? SkinShare_Peer(p) : nullptr;
        void* pPlayer = (peer && peer->hasLoadout && peer->playerIdx > 0) ? GetClientEntity(peer->playerIdx) : nullptr;
        if (pPlayer && !Alive(pPlayer)) pPlayer = nullptr;
        const int cls  = pPlayer ? GetPlayerClass(peer->playerIdx) : 0;
        const int team = pPlayer ? RdI(pPlayer, s_off.teamNum) : 0;
        unsigned owner = pPlayer ? RefHandle(pPlayer) : 0;
        if (pPlayer && (owner & 0xFFF) != (unsigned)peer->playerIdx) pPlayer = nullptr;

        int k = 0;
        for (int h = 0; h < kHatSlots && k < kPeerHatsEach; ++h) {
            const HatSlot c = pPlayer ? peer->lo.hats[h] : HatSlot{ 0, 0, -1, -1 };
            if (c.def <= 0) continue;
            const HatDef* d = Hats_Find(c.def);
            if (!d) continue;
            TickSlot(kHatSlots + p * kPeerHatsEach + k++, c, d, Wearable(d, cls, team), cls, team, owner);
        }
        for (; k < kPeerHatsEach; ++k)   // the rest of this peer's slots wear nothing
            TickSlot(kHatSlots + p * kPeerHatsEach + k, HatSlot{ 0, 0, -1, -1 }, nullptr, false, 0, 0, 0);
    }
}

void Hats_FrameStage(int stage)
{
    if (stage != 5) return;
    // Backpack items live in the GC cache, not the world: kept in the main menu too.
    HatsInv_Tick(!g_eject && g_hatsEnable && g_hatsBackpack);
    if (!g_entityList) return;
    ++s_frame;

    // The engine may have destroyed ours (level change, disconnect, full update) or put its own
    // entity at the index. Forget it, never Release it.
    for (int i = 0; i < kTotalSlots; ++i)
        if (s_live[i].ent && !StillOurs(i)) {
            DebugLog("hats: slot %d def %d gone (engine destroyed or replaced it)", i, s_live[i].cfg.def);
            s_liveNet[i] = nullptr;   // that address may be some other entity's by now
            s_live[i] = {};
            s_freedFrame[i] = s_frame;
        }

    // Peers' hats need only "show shared": our own hat changer off still ticks them (local slots
    // then wear nothing, so body / hide below come out empty too).
    const bool peersOn = g_shareNet && g_shareRecv;
    if (!g_hatsEnable) s_appliedOnce = false;   // the next spawn applies whatever the menu holds by then
    if (g_eject || (!g_hatsEnable && !peersOn) || !IsInGame()) {
        for (int i = 0; i < kTotalSlots; ++i)
            if (s_live[i].ent) Release(i, g_eject ? "eject" : "off");
        if (s_bodyOpCount) s_bodyOpCount = 0;
        s_hideCount = 0;
        s_appliedOnce = false;
        return;
    }

    ResolveOnce();
    if (!s_ready) return;
    const int localIdx = GetLocalPlayerIndex();
    void* pLocal = localIdx > 0 ? GetClientEntity(localIdx) : nullptr;
    if (!pLocal) return;
    const int cls = GetPlayerClass(localIdx);
    const int team = RdI(pLocal, s_off.teamNum);
    const unsigned owner = RefHandle(pLocal);
    if ((owner & 0xFFF) != (unsigned)localIdx) return;   // not the handle we think it is
    const bool alive = Alive(pLocal);

    // The first snapshot: whatever the menu holds when the hats come on (a respawn applies the
    // rest). A dead local player keeps the old one until it spawns -- the TF2 order.
    if (g_hatsEnable && !s_appliedOnce) ApplySnapshot("first frame");

    for (int i = 0; i < kHatSlots; ++i) {
        const HatSlot c = s_applied[i];
        const HatDef* d = c.def > 0 ? Hats_Find(c.def) : nullptr;
        // TF2's rule when the hat is a backpack item: worn only on the classes it is equipped on
        // in the loadout screen (bit 0 = the screen touched it, bit cls = equipped there). With no
        // backpack copy there is nothing to equip, so the slot is worn on every class that can.
        const bool wear = g_hatsEnable && alive && Wearable(d, cls, team)
                       && (!g_hatsBackpack || ((c.eqMask & 1) && (c.eqMask >> cls & 1)));
        TickSlot(i, c, d, wear, cls, team, owner);
    }
    TickPeers();

    UpdateBody(localIdx, pLocal);
    for (int i = 0; i < kTotalSlots; ++i) SyncVisibility(i);
    if (s_frame % 30 == 0) UpdateHide(localIdx, cls);

    // diag: 1 s, 3 s, then every 10 s after a spawn, 8 lines max per spawn
    static uint32_t s_diagLines[kTotalSlots];
    for (int i = 0; i < kTotalSlots; ++i) {
        const LiveHat& L = s_live[i];
        if (!L.ent) { s_diagLines[i] = 0; continue; }
        const uint32_t age = s_frame - L.bornFrame;
        if (s_diagLines[i] < 8 && (age == 100 || age == 300 || (age > 300 && age % 1000 == 0))) {
            ++s_diagLines[i];
            Diag(i, pLocal);
        }
    }
}

// ---- config -------------------------------------------------------------------------------------
void Hats_SaveCfg(FILE* f)
{
    fprintf(f, "hatsEnable=%d\n", g_hatsEnable ? 1 : 0);
    fprintf(f, "hatsBackpack=%d\n", g_hatsBackpack ? 1 : 0);
    for (int i = 0; i < kHatSlots; ++i) {
        const HatSlot& c = g_hatSlots[i];
        if (c.def <= 0) continue;
        fprintf(f, "hat_%d=%d %d %d %d %u", i, c.def, c.style, c.unusual, c.paint, (unsigned)c.eqMask);
        for (int k = 1; k <= 9; ++k) fprintf(f, " %u", (unsigned)c.eqPos[k]);
        fprintf(f, "\n");
    }
}

bool Hats_LoadCfg(const char* key, const char* val)
{
    // hatsEnable is always written first, so a config that has fewer hats than the one before
    // it clears the rest instead of inheriting them.
    if (strcmp(key, "hatsEnable") == 0) {
        g_hatsEnable = atoi(val) != 0;
        for (HatSlot& s : g_hatSlots) s = { 0, 0, -1, -1 };
        return true;
    }
    if (strcmp(key, "hatsBackpack") == 0) { g_hatsBackpack = atoi(val) != 0; return true; }
    if (strncmp(key, "hat_", 4) != 0) return false;
    const int i = atoi(key + 4);
    if (i < 0 || i >= kHatSlots) return true;
    HatSlot c = { 0, 0, -1, -1 };
    unsigned m = 0, p[10] = {};
    if (sscanf(val, "%d %d %d %d %u %u %u %u %u %u %u %u %u %u", &c.def, &c.style, &c.unusual, &c.paint, &m,
               &p[1], &p[2], &p[3], &p[4], &p[5], &p[6], &p[7], &p[8], &p[9]) >= 1 && Hats_Find(c.def)) {
        c.eqMask = (uint16_t)(m & 0x3FF);
        for (int k = 1; k <= 9; ++k) c.eqPos[k] = (uint8_t)p[k];
        g_hatSlots[i] = c;
    }
    return true;
}
