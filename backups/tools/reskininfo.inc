// ---- Reskin runtime info, generated from items_game.txt ------------------------------
// model  : "model_player" (walked through the item's prefab chain). fpsoverlay typed
//          these by hand and got 15 of them wrong -- a path the engine cannot load
//          silently leaves the stock model on screen.
// anim   : "anim_slot". A reskin whose slot differs from the weapon it replaces needs
//          its sequences remapped (ACT_MELEE_VM_* -> ACT_MELEE_ALLCLASS_VM_*, ...).
// skins  : "visuals/styles/<n>/skin_red|skin_blu" -- what makes a Gold Frying Pan gold.
// gold   : the item carries set_turn_to_gold (attr 150): Saxxy, Golden Wrench, GFP.
// logname: "item_logname", which is what the killfeed reads to pick its icon -- but only
//          when tf/scripts/mod_textures.txt has an entry for it. nullptr = it has none
//          (skullbat, prinny_machete, awper_hand, giger_counter, shooting_star), and
//          the event must be left alone: "d_<name>" with no entry draws the SKULL.
// pfx    : the activity family the item's anim_slot names, as a prefix.
// repl   : "visuals/animation_replacement" -- the item's OWN activity overrides, which
//          TF2 consults BEFORE the anim_slot family table (tf_weaponbase.cpp:4419), so
//          this, not anim_slot, is what actually decides which sequence plays. 27 of the
//          44 reskins carry one; the Snack Attack is nothing BUT one (its slot is
//          "building", same as the stock sapper -- ACT_VM_DRAW -> ACT_BREADSAPPER_VM_DRAW
//          is the entire difference).
enum AnimSlot : uint8_t { AS_DEFAULT = 0, AS_MELEE_ALLCLASS, AS_ITEM1, AS_ITEM2, AS_NONE };

// One entry of an animation_replacement block: the activity the weapon code asks for,
// and the activity this item plays instead.
struct ActPair { const char* from; const char* to; };

static const ActPair k_actRepl_r264[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_MELEE_ALLCLASS_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_MELEE_ALLCLASS_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_MELEE_ALLCLASS_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r474[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_MELEE_ALLCLASS_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_MELEE_ALLCLASS_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_MELEE_ALLCLASS_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r880[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_MELEE_ALLCLASS_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_MELEE_ALLCLASS_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_MELEE_ALLCLASS_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r939[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_MELEE_ALLCLASS_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_MELEE_ALLCLASS_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_MELEE_ALLCLASS_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r1013[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_MELEE_ALLCLASS_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_MELEE_ALLCLASS_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_MELEE_ALLCLASS_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r1127[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_MELEE_ALLCLASS_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_MELEE_ALLCLASS_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_MELEE_ALLCLASS_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r423[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_MELEE_ALLCLASS_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_MELEE_ALLCLASS_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_MELEE_ALLCLASS_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r1071[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_MELEE_ALLCLASS_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_MELEE_ALLCLASS_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_MELEE_ALLCLASS_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r954[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_MELEE_ALLCLASS_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_MELEE_ALLCLASS_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_MELEE_ALLCLASS_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r30758[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_MELEE_ALLCLASS_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_MELEE_ALLCLASS_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_MELEE_ALLCLASS_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r1100[] = {
    { "ACT_VM_HITLEFT",                          "ACT_BREADGLOVES_VM_HITLEFT" },
    { "ACT_VM_HITRIGHT",                         "ACT_BREADGLOVES_VM_HITRIGHT" },
    { "ACT_VM_SWINGHARD",                        "ACT_BREADGLOVES_VM_SWINGHARD" },
    { "ACT_VM_IDLE",                             "ACT_BREADGLOVES_VM_IDLE" },
    { "ACT_VM_DRAW",                             "ACT_BREADGLOVES_VM_DRAW" },
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_MELEE_ALT2_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_MELEE_ALT2_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_MELEE_ALT2_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r727[] = {
    { "ACT_VM_HITCENTER",                        "ACT_MELEE_VM_HITCENTER" },
    { "ACT_VM_SWINGHARD",                        "ACT_MELEE_VM_SWINGHARD" },
};
static const ActPair k_actRepl_r638[] = {
    { "ACT_VM_DRAW",                             "ACT_ITEM1_VM_DRAW" },
    { "ACT_VM_IDLE",                             "ACT_ITEM1_VM_IDLE" },
    { "ACT_VM_HITCENTER",                        "ACT_ITEM1_VM_HITCENTER" },
    { "ACT_VM_HITCENTER2",                       "ACT_ITEM1_VM_HITCENTER2" },
    { "ACT_VM_SWINGHARD",                        "ACT_ITEM1_VM_SWINGHARD" },
    { "ACT_BACKSTAB_VM_UP",                      "ACT_ITEM1_BACKSTAB_VM_UP" },
    { "ACT_BACKSTAB_VM_DOWN",                    "ACT_ITEM1_BACKSTAB_VM_DOWN" },
    { "ACT_BACKSTAB_VM_IDLE",                    "ACT_ITEM1_BACKSTAB_VM_IDLE" },
    { "ACT_MELEE_VM_STUN",                       "ACT_MELEE_VM_ITEM1_STUN" },
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_ITEM1_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_ITEM1_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_ITEM1_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r482[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_SPECIAL_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_SPECIAL_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_SPECIAL_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r266[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_SPECIAL_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_SPECIAL_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_SPECIAL_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r851[] = {
    { "ACT_PRIMARY_VM_INSPECT_START",            "ACT_PRIMARY_ALT1_VM_INSPECT_START" },
    { "ACT_PRIMARY_VM_INSPECT_IDLE",             "ACT_PRIMARY_ALT1_VM_INSPECT_IDLE" },
    { "ACT_PRIMARY_VM_INSPECT_END",              "ACT_PRIMARY_ALT1_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r1092[] = {
    { "ACT_PRIMARY_VM_INSPECT_START",            "ACT_ITEM2_VM_INSPECT_START" },
    { "ACT_PRIMARY_VM_INSPECT_IDLE",             "ACT_ITEM2_VM_INSPECT_IDLE" },
    { "ACT_PRIMARY_VM_INSPECT_END",              "ACT_ITEM2_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r741[] = {
    { "ACT_MP_ATTACK_CROUCH_SECONDARYFIRE",      "ACT_MP_ATTACK_CROUCH_PRIMARY_ALT" },
    { "ACT_MP_ATTACK_STAND_SECONDARYFIRE",       "ACT_MP_ATTACK_STAND_PRIMARY_ALT" },
    { "ACT_MP_ATTACK_SWIM_SECONDARYFIRE",        "ACT_MP_ATTACK_SWIM_PRIMARY_ALT" },
};
static const ActPair k_actRepl_r30474[] = {
    { "ACT_PRIMARY_VM_INSPECT_START",            "ACT_PRIMARY_ALT1_VM_INSPECT_START" },
    { "ACT_PRIMARY_VM_INSPECT_IDLE",             "ACT_PRIMARY_ALT1_VM_INSPECT_IDLE" },
    { "ACT_PRIMARY_VM_INSPECT_END",              "ACT_PRIMARY_ALT1_VM_INSPECT_END" },
    { "ACT_MP_ATTACK_CROUCH_SECONDARYFIRE",      "ACT_MP_ATTACK_CROUCH_PRIMARY_ALT" },
    { "ACT_MP_ATTACK_STAND_SECONDARYFIRE",       "ACT_MP_ATTACK_STAND_PRIMARY_ALT" },
    { "ACT_MP_ATTACK_SWIM_SECONDARYFIRE",        "ACT_MP_ATTACK_SWIM_PRIMARY_ALT" },
};
static const ActPair k_actRepl_r30668[] = {
    { "ACT_SECONDARY_VM_INSPECT_START",          "ACT_ITEM1_VM_INSPECT_START" },
    { "ACT_SECONDARY_VM_INSPECT_IDLE",           "ACT_ITEM1_VM_INSPECT_IDLE" },
    { "ACT_SECONDARY_VM_INSPECT_END",            "ACT_ITEM1_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r30665[] = {
    { "ACT_PRIMARY_VM_INSPECT_START",            "ACT_PRIMARY_ALT1_VM_INSPECT_START" },
    { "ACT_PRIMARY_VM_INSPECT_IDLE",             "ACT_PRIMARY_ALT1_VM_INSPECT_IDLE" },
    { "ACT_PRIMARY_VM_INSPECT_END",              "ACT_PRIMARY_ALT1_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r863[] = {
    { "ACT_SECONDARY_VM_INSPECT_START",          "ACT_ITEM1_VM_INSPECT_START" },
    { "ACT_SECONDARY_VM_INSPECT_IDLE",           "ACT_ITEM1_VM_INSPECT_IDLE" },
    { "ACT_SECONDARY_VM_INSPECT_END",            "ACT_ITEM1_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r433[] = {
    { "ACT_SECONDARY_VM_INSPECT_START",          "ACT_ITEM1_VM_INSPECT_START" },
    { "ACT_SECONDARY_VM_INSPECT_IDLE",           "ACT_ITEM1_VM_INSPECT_IDLE" },
    { "ACT_SECONDARY_VM_INSPECT_END",            "ACT_ITEM1_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r1121[] = {
    { "ACT_MP_ATTACK_STAND_MELEE_ALLCLASS",      "ACT_MP_THROW" },
    { "ACT_VM_DRAW",                             "ACT_BREADMONSTER_VM_DRAW" },
    { "ACT_VM_IDLE",                             "ACT_BREADMONSTER_VM_IDLE" },
    { "ACT_VM_PRIMARYATTACK",                    "ACT_BREADMONSTER_VM_PRIMARYATTACK" },
    { "ACT_SECONDARY_VM_INSPECT_START",          "ACT_BREADMONSTER_VM_INSPECT_START" },
    { "ACT_SECONDARY_VM_INSPECT_IDLE",           "ACT_BREADMONSTER_VM_INSPECT_IDLE" },
    { "ACT_SECONDARY_VM_INSPECT_END",            "ACT_BREADMONSTER_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r1105[] = {
    { "ACT_MP_ATTACK_STAND_MELEE_ALLCLASS",      "ACT_MP_THROW" },
    { "ACT_VM_DRAW",                             "ACT_BREADMONSTER_VM_DRAW" },
    { "ACT_VM_IDLE",                             "ACT_BREADMONSTER_VM_IDLE" },
    { "ACT_VM_PRIMARYATTACK",                    "ACT_BREADMONSTER_VM_PRIMARYATTACK" },
    { "ACT_SECONDARY_VM_INSPECT_START",          "ACT_BREADMONSTER_VM_INSPECT_START" },
    { "ACT_SECONDARY_VM_INSPECT_IDLE",           "ACT_BREADMONSTER_VM_INSPECT_IDLE" },
    { "ACT_SECONDARY_VM_INSPECT_END",            "ACT_BREADMONSTER_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r1102[] = {
    { "ACT_VM_DRAW",                             "ACT_BREADSAPPER_VM_DRAW" },
    { "ACT_VM_DRAW_DEPLOYED",                    "ACT_BREADSAPPER_VM_DRAW" },
    { "ACT_VM_IDLE",                             "ACT_BREADSAPPER_VM_IDLE" },
    { "ACT_BUILDING_VM_INSPECT_START",           "ACT_BREADMONSTER_VM_INSPECT_START" },
    { "ACT_BUILDING_VM_INSPECT_IDLE",            "ACT_BREADMONSTER_VM_INSPECT_IDLE" },
    { "ACT_BUILDING_VM_INSPECT_END",             "ACT_BREADMONSTER_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r574[] = {
    { "ACT_VM_DRAW",                             "ACT_ITEM2_VM_DRAW" },
    { "ACT_VM_IDLE",                             "ACT_ITEM2_VM_IDLE" },
    { "ACT_VM_HITCENTER",                        "ACT_ITEM2_VM_HITCENTER" },
    { "ACT_VM_SWINGHARD",                        "ACT_ITEM2_VM_SWINGHARD" },
    { "ACT_BACKSTAB_VM_UP",                      "ACT_ITEM2_BACKSTAB_VM_UP" },
    { "ACT_BACKSTAB_VM_DOWN",                    "ACT_ITEM2_BACKSTAB_VM_DOWN" },
    { "ACT_BACKSTAB_VM_IDLE",                    "ACT_ITEM2_BACKSTAB_VM_IDLE" },
    { "ACT_MELEE_VM_STUN",                       "ACT_MELEE_VM_ITEM2_STUN" },
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_ITEM2_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_ITEM2_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_ITEM2_VM_INSPECT_END" },
};
static const ActPair k_actRepl_b194[] = {
    { "ACT_VM_HITCENTER",                        "ACT_MELEE_VM_HITCENTER" },
    { "ACT_VM_SWINGHARD",                        "ACT_MELEE_VM_SWINGHARD" },
};
static const ActPair k_actRepl_b195[] = {
    { "ACT_VM_IDLE",                             "ACT_FISTS_VM_IDLE" },
    { "ACT_VM_HITLEFT",                          "ACT_FISTS_VM_HITLEFT" },
    { "ACT_VM_HITRIGHT",                         "ACT_FISTS_VM_HITRIGHT" },
    { "ACT_VM_SWINGHARD",                        "ACT_FISTS_VM_SWINGHARD" },
    { "ACT_VM_DRAW",                             "ACT_FISTS_VM_DRAW" },
};
static const ActPair k_actRepl_b132[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_SPECIAL_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_SPECIAL_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_SPECIAL_VM_INSPECT_END" },
};
static const ActPair k_actRepl_b225[] = {
    { "ACT_VM_DRAW",                             "ACT_ITEM2_VM_DRAW" },
    { "ACT_VM_IDLE",                             "ACT_ITEM2_VM_IDLE" },
    { "ACT_VM_HITCENTER",                        "ACT_ITEM2_VM_HITCENTER" },
    { "ACT_VM_SWINGHARD",                        "ACT_ITEM2_VM_SWINGHARD" },
    { "ACT_BACKSTAB_VM_UP",                      "ACT_ITEM2_BACKSTAB_VM_UP" },
    { "ACT_BACKSTAB_VM_DOWN",                    "ACT_ITEM2_BACKSTAB_VM_DOWN" },
    { "ACT_BACKSTAB_VM_IDLE",                    "ACT_ITEM2_BACKSTAB_VM_IDLE" },
    { "ACT_MELEE_VM_STUN",                       "ACT_MELEE_VM_ITEM2_STUN" },
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_ITEM2_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_ITEM2_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_ITEM2_VM_INSPECT_END" },
};
static const ActPair k_actRepl_b56[] = {
    { "ACT_PRIMARY_VM_INSPECT_START",            "ACT_ITEM2_VM_INSPECT_START" },
    { "ACT_PRIMARY_VM_INSPECT_IDLE",             "ACT_ITEM2_VM_INSPECT_IDLE" },
    { "ACT_PRIMARY_VM_INSPECT_END",              "ACT_ITEM2_VM_INSPECT_END" },
};
static const ActPair k_actRepl_b208[] = {
    { "ACT_MP_ATTACK_CROUCH_SECONDARYFIRE",      "ACT_MP_ATTACK_CROUCH_PRIMARY_ALT" },
    { "ACT_MP_ATTACK_STAND_SECONDARYFIRE",       "ACT_MP_ATTACK_STAND_PRIMARY_ALT" },
    { "ACT_MP_ATTACK_SWIM_SECONDARYFIRE",        "ACT_MP_ATTACK_SWIM_PRIMARY_ALT" },
};
static const ActPair k_actRepl_b140[] = {
    { "ACT_SECONDARY_VM_INSPECT_START",          "ACT_ITEM1_VM_INSPECT_START" },
    { "ACT_SECONDARY_VM_INSPECT_IDLE",           "ACT_ITEM1_VM_INSPECT_IDLE" },
    { "ACT_SECONDARY_VM_INSPECT_END",            "ACT_ITEM1_VM_INSPECT_END" },
};
static const ActPair k_actRepl_b526[] = {
    { "ACT_PRIMARY_VM_INSPECT_START",            "ACT_PRIMARY_ALT1_VM_INSPECT_START" },
    { "ACT_PRIMARY_VM_INSPECT_IDLE",             "ACT_PRIMARY_ALT1_VM_INSPECT_IDLE" },
    { "ACT_PRIMARY_VM_INSPECT_END",              "ACT_PRIMARY_ALT1_VM_INSPECT_END" },
};
static const ActPair k_actRepl_b42[] = {
    { "ACT_SECONDARY_VM_INSPECT_START",          "ACT_ITEM1_VM_INSPECT_START" },
    { "ACT_SECONDARY_VM_INSPECT_IDLE",           "ACT_ITEM1_VM_INSPECT_IDLE" },
    { "ACT_SECONDARY_VM_INSPECT_END",            "ACT_ITEM1_VM_INSPECT_END" },
};
static const ActPair k_actRepl_b159[] = {
    { "ACT_SECONDARY_VM_INSPECT_START",          "ACT_ITEM1_VM_INSPECT_START" },
    { "ACT_SECONDARY_VM_INSPECT_IDLE",           "ACT_ITEM1_VM_INSPECT_IDLE" },
    { "ACT_SECONDARY_VM_INSPECT_END",            "ACT_ITEM1_VM_INSPECT_END" },
};
static const ActPair k_actRepl_b222[] = {
    { "ACT_SECONDARY_VM_INSPECT_START",          "ACT_ITEM1_VM_INSPECT_START" },
    { "ACT_SECONDARY_VM_INSPECT_IDLE",           "ACT_ITEM1_VM_INSPECT_IDLE" },
    { "ACT_SECONDARY_VM_INSPECT_END",            "ACT_ITEM1_VM_INSPECT_END" },
};
static const ActPair k_actRepl_b58[] = {
    { "ACT_SECONDARY_VM_INSPECT_START",          "ACT_ITEM1_VM_INSPECT_START" },
    { "ACT_SECONDARY_VM_INSPECT_IDLE",           "ACT_ITEM1_VM_INSPECT_IDLE" },
    { "ACT_SECONDARY_VM_INSPECT_END",            "ACT_ITEM1_VM_INSPECT_END" },
};

static constexpr int k_maxReskinStyles = 4;
struct ReskinInfo {
    int      defindex;
    const char* model;          // nullptr = the item has no c_model of its own
    const char* logname;        // "item_logname" -- the killfeed icon name
    AnimSlot anim;
    uint8_t  styleCount;        // 0 = no styles block, skins come from the team default
    int8_t   skinRed[k_maxReskinStyles];
    int8_t   skinBlu[k_maxReskinStyles];
    bool     turnToGold;        // attr 150
    bool     specialTaunt;      // attr 551 special_taunt -- the Saxxy/Rainblower taunt kill
    const char* animPfx;        // activity family prefix, null when the slot names none
    const ActPair* actRepl;     // animation_replacement, null when the item has none
    uint8_t  actReplCount;
};

static const ReskinInfo k_reskinInfo[] = {
    {  30667, "models/workshop/weapons/c_models/c_invasion_bat/c_invasion_bat.mdl",          "batsaber",              AS_DEFAULT,        1, {0,-1,-1,-1}, {1,-1,-1,-1}, false, false, "ACT_MELEE_VM_",           nullptr, 0, }, // Batsaber
    {    221, "models/workshop/weapons/c_models/c_holymackerel/c_holymackerel.mdl",          "holymackerel",          AS_DEFAULT,        1, {0,-1,-1,-1}, {1,-1,-1,-1}, false, false, "ACT_MELEE_VM_",           nullptr, 0, }, // The Holy Mackerel
    {    264, "models/weapons/c_models/c_frying_pan/c_frying_pan.mdl",                       "fryingpan",             AS_MELEE_ALLCLASS, 0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r264, 3, }, // Frying Pan
    {    474, "models/workshop/weapons/c_models/c_picket/c_picket.mdl",                      "nonnonviolent_protest", AS_MELEE_ALLCLASS, 0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r474, 3, }, // The Conscientious Objector
    {    880, "models/workshop_partner/weapons/c_models/c_tw_eagle/c_tw_eagle.mdl",          "freedom_staff",         AS_MELEE_ALLCLASS, 0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r880, 3, }, // The Freedom Staff
    {    939, "models/workshop/weapons/c_models/c_skullbat/c_skullbat.mdl",                  nullptr,                 AS_MELEE_ALLCLASS, 4, {0,0,0,0}, {1,1,1,1}, false, false, "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r939, 3, }, // The Bat Outta Hell
    {   1013, "models/workshop/weapons/c_models/c_ham/c_ham.mdl",                            "ham_shank",             AS_MELEE_ALLCLASS, 0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r1013, 3, }, // The Ham Shank
    {   1127, "models/workshop/weapons/c_models/c_crossing_guard/c_crossing_guard.mdl",      "crossing_guard",        AS_MELEE_ALLCLASS, 0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r1127, 3, }, // The Crossing Guard
    {    423, "models/weapons/c_models/c_saxxy/c_saxxy.mdl",                                 "saxxy",                 AS_MELEE_ALLCLASS, 0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, true, true,  "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r423, 3, }, // Saxxy
    {   1071, "models/weapons/c_models/c_frying_pan/c_frying_pan.mdl",                       "golden_fryingpan",      AS_MELEE_ALLCLASS, 1, {2,-1,-1,-1}, {3,-1,-1,-1}, true, false, "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r1071, 3, }, // Gold Frying Pan
    {    954, "models/weapons/c_models/c_8mm_camera/c_8mm_camera.mdl",                       "memory_maker",          AS_MELEE_ALLCLASS, 0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r954, 3, }, // Memory Maker
    {  30758, "models/workshop_partner/weapons/c_models/c_prinny_knife/c_prinny_knife.mdl",  nullptr,                 AS_MELEE_ALLCLASS, 0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r30758, 3, }, // Prinny Machete
    {    739, "models/weapons/c_models/c_lollichop/c_lollichop.mdl",                         "lollichop",             AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_VM_",           nullptr, 0, }, // The Lollichop
    {    609, "models/workshop/weapons/c_models/c_scotland_shard/c_scotland_shard.mdl",      "scotland_shard",        AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_VM_",           nullptr, 0, }, // The Scottish Handshake
    {   1100, "models/weapons/c_models/c_breadmonster_gloves/c_breadmonster_gloves.mdl",     "bread_bite",            AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_VM_",           k_actRepl_r1100, 8, }, // The Bread Bite
    {    169, "models/weapons/c_models/c_wrench/c_wrench.mdl",                               "wrench_golden",         AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, true, false, "ACT_MELEE_VM_",           nullptr, 0, }, // Golden Wrench
    {    727, "models/workshop/weapons/c_models/c_ava_roseknife/c_ava_roseknife.mdl",        "black_rose",            AS_DEFAULT,        2, {0,2,-1,-1}, {1,3,-1,-1}, false, true,  "ACT_MELEE_VM_",           k_actRepl_r727, 2, }, // The Black Rose
    {    638, "models/workshop/weapons/c_models/c_acr_hookblade/c_acr_hookblade.mdl",        "sharp_dresser",         AS_ITEM1,          0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_ITEM1_VM_",           k_actRepl_r638, 12, }, // The Sharp Dresser
    {    482, "models/workshop/weapons/c_models/c_golfclub/c_golfclub.mdl",                  "nessieclub",            AS_ITEM1,          0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_ITEM1_VM_",           k_actRepl_r482, 3, }, // Nessie's Nine Iron
    {    266, "models/weapons/c_models/c_headtaker/c_headtaker.mdl",                         "headtaker",             AS_ITEM1,          0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_ITEM1_VM_",           k_actRepl_r266, 3, }, // The Horseless Headless Horseman's Headtaker
    {    457, "models/workshop/weapons/c_models/c_mailbox/c_mailbox.mdl",                    "mailbox",               AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_VM_",           nullptr, 0, }, // The Postal Pummeler
    {    298, "models/workshop/weapons/c_models/c_iron_curtain/c_iron_curtain.mdl",          "iron_curtain",          AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_PRIMARY_VM_",         nullptr, 0, }, // Iron Curtain
    {    851, "models/weapons/c_models/c_csgo_awp/c_csgo_awp.mdl",                           nullptr,                 AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_PRIMARY_VM_",         k_actRepl_r851, 3, }, // The AWPer Hand
    {   1092, "models/workshop_partner/weapons/c_models/c_bow_thief/c_bow_thief.mdl",        nullptr,                 AS_ITEM2,          0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_ITEM2_VM_",           k_actRepl_r1092, 3, }, // The Fortified Compound
    {    741, "models/weapons/c_models/c_rainblower/c_rainblower.mdl",                       "rainblower",            AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, true,  "ACT_PRIMARY_VM_",         k_actRepl_r741, 3, }, // The Rainblower
    {  30474, "models/workshop_partner/weapons/c_models/c_ai_flamethrower/c_ai_flamethrower.mdl", "ai_flamethrower",       AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_PRIMARY_VM_",         k_actRepl_r30474, 6, }, // The Nostromo Napalmer
    {    160, "models/workshop/weapons/c_models/c_ttg_max_gun/c_ttg_max_gun.mdl",            "maxgun",                AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_SECONDARY_VM_",       nullptr, 0, }, // TTG Max Pistol
    {  30666, "models/workshop/weapons/c_models/c_invasion_pistol/c_invasion_pistol.mdl",    "the_capper",            AS_DEFAULT,        1, {0,-1,-1,-1}, {1,-1,-1,-1}, false, false, "ACT_SECONDARY_VM_",       nullptr, 0, }, // The C.A.P.P.E.R
    {    161, "models/workshop/weapons/c_models/c_ttg_sam_gun/c_ttg_sam_gun.mdl",            "samrevolver",           AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_SECONDARY_VM_",       nullptr, 0, }, // TTG Sam Revolver
    {  30668, "models/workshop/weapons/c_models/c_invasion_wrangler/c_invasion_wrangler.mdl", nullptr,                 AS_ITEM1,          1, {0,-1,-1,-1}, {1,-1,-1,-1}, false, false, "ACT_ITEM1_VM_",           k_actRepl_r30668, 3, }, // The Giger Counter
    {  30665, "models/workshop/weapons/c_models/c_invasion_sniperrifle/c_invasion_sniperrifle.mdl", nullptr,                 AS_DEFAULT,        1, {0,-1,-1,-1}, {1,-1,-1,-1}, false, false, "ACT_PRIMARY_VM_",         k_actRepl_r30665, 3, }, // Shooting Star
    {    863, "models/weapons/c_models/c_sandwich/c_robo_sandwich.mdl",                      nullptr,                 AS_ITEM1,          0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, true,  "ACT_ITEM1_VM_",           k_actRepl_r863, 3, }, // The Robo-Sandvich
    {    433, "models/workshop/weapons/c_models/c_fishcake/c_fishcake.mdl",                  nullptr,                 AS_ITEM1,          0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, true,  "ACT_ITEM1_VM_",           k_actRepl_r433, 3, }, // Fishcake
    {   1121, "models/weapons/c_models/c_breadmonster/c_breadmonster_milk.mdl",              nullptr,                 AS_MELEE_ALLCLASS, 0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r1121, 7, }, // Mutated Milk
    {    572, "models/workshop/weapons/c_models/c_unarmed_combat/c_unarmed_combat.mdl",      "unarmed_combat",        AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_VM_",           nullptr, 0, }, // Unarmed Combat
    {    466, "models/workshop/weapons/c_models/c_rfa_hammer/c_rfa_hammer.mdl",              "the_maul",              AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_VM_",           nullptr, 0, }, // The Maul
    {   1105, "models/weapons/c_models/c_breadmonster/c_breadmonster.mdl",                   nullptr,                 AS_MELEE_ALLCLASS, 0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r1105, 7, }, // The Self-Aware Beauty Mark
    {    297, "models/weapons/v_models/v_ttg_watch_spy.mdl",                                 nullptr,                 AS_NONE,           0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, nullptr,                   nullptr, 0, }, // TTG Watch
    {    947, "models/workshop_partner/weapons/v_models/v_hm_watch/v_hm_watch.mdl",          nullptr,                 AS_NONE,           0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, nullptr,                   nullptr, 0, }, // The Quackenbirdt
    {    933, "models/weapons/c_models/c_p2rec/c_p2rec.mdl",                                 "psapper",               AS_NONE,           0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, nullptr,                   nullptr, 0, }, // The Ap-Sap
    {   1102, "models/weapons/c_models/c_breadmonster_sapper/c_breadmonster_sapper.mdl",     "snack_attack",          AS_NONE,           0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, nullptr,                   k_actRepl_r1102, 6, }, // The Snack Attack
    {    608, "models/workshop/player/items/demo/pegleg/pegleg.mdl",                         nullptr,                 AS_NONE,           0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, nullptr,                   nullptr, 0, }, // The Bootlegger
    {    574, "models/workshop/weapons/c_models/c_voodoo_pin/c_voodoo_pin.mdl",              "voodoo_pin",            AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_VM_",           k_actRepl_r574, 11, }, // The Wanga Prick
    {    452, "models/workshop/weapons/c_models/c_scout_sword/c_scout_sword.mdl",            "scout_sword",           AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_VM_",           nullptr, 0, }, // Three-Rune Blade
};
static constexpr int k_reskinInfoCount = (int)(sizeof(k_reskinInfo)/sizeof(k_reskinInfo[0]));

inline const ReskinInfo* Skin_FindReskinInfo(int defindex)
{
    for (int i = 0; i < k_reskinInfoCount; ++i)
        if (k_reskinInfo[i].defindex == defindex) return &k_reskinInfo[i];
    return nullptr;
}

// The weapon a reskin can be applied TO -- the "from" half of a remap.
// model is that weapon's own "model_player" c_model: the sequence table the game picked
// its index out of before we rewrote the def index. Generated rather than read back off
// the live entity, because by the second pre-write pass every model field on that
// weapon is already OURS and reading one back returns the reskin.
struct BaseAnimEntry {
    int weaponRedir; AnimSlot anim; const char* model;
    const char* animPfx;        // the family the BASE plays in -- the "from" side
    const ActPair* actRepl;     // the base's own overrides, applied before the family
    uint8_t actReplCount;
};
static const BaseAnimEntry k_baseAnimSlots[] = {
    {  190, AS_DEFAULT,        "models/weapons/c_models/c_bat.mdl",                                     "ACT_MELEE_VM_",           nullptr, 0, }, // Upgradeable TF_WEAPON_BAT
    {  191, AS_DEFAULT,        "models/weapons/c_models/c_bottle/c_bottle.mdl",                         "ACT_MELEE_VM_",           nullptr, 0, }, // Upgradeable TF_WEAPON_BOTTLE
    {  192, AS_DEFAULT,        "models/weapons/c_models/c_fireaxe_pyro/c_fireaxe_pyro.mdl",             "ACT_MELEE_VM_",           nullptr, 0, }, // Upgradeable TF_WEAPON_FIREAXE
    {  193, AS_DEFAULT,        "models/weapons/c_models/c_machete/c_machete.mdl",                       "ACT_MELEE_VM_",           nullptr, 0, }, // Upgradeable TF_WEAPON_CLUB
    {  194, AS_DEFAULT,        "models/weapons/c_models/c_knife/c_knife.mdl",                           "ACT_MELEE_VM_",           k_actRepl_b194, 2, }, // Upgradeable TF_WEAPON_KNIFE
    {  195, AS_DEFAULT,        nullptr,                                                                 "ACT_MELEE_VM_",           k_actRepl_b195, 5, }, // Upgradeable TF_WEAPON_FISTS
    {  196, AS_DEFAULT,        "models/weapons/c_models/c_shovel/c_shovel.mdl",                         "ACT_MELEE_VM_",           nullptr, 0, }, // Upgradeable TF_WEAPON_SHOVEL
    {  197, AS_DEFAULT,        "models/weapons/c_models/c_wrench/c_wrench.mdl",                         "ACT_MELEE_VM_",           nullptr, 0, }, // Upgradeable TF_WEAPON_WRENCH
    {  198, AS_DEFAULT,        "models/weapons/c_models/c_bonesaw/c_bonesaw.mdl",                       "ACT_MELEE_VM_",           nullptr, 0, }, // Upgradeable TF_WEAPON_BONESAW
    {   38, AS_DEFAULT,        "models/weapons/c_models/c_axtinguisher/c_axtinguisher_pyro.mdl",        "ACT_MELEE_VM_",           nullptr, 0, }, // The Axtinguisher
    {  132, AS_ITEM1,          "models/weapons/c_models/c_claymore/c_claymore.mdl",                     "ACT_ITEM1_VM_",           k_actRepl_b132, 3, }, // The Eyelander
    {  225, AS_DEFAULT,        "models/workshop/weapons/c_models/c_eternal_reward/c_eternal_reward.mdl", "ACT_MELEE_VM_",           k_actRepl_b225, 11, }, // Your Eternal Reward
    {  325, AS_DEFAULT,        "models/workshop/weapons/c_models/c_boston_basher/c_boston_basher.mdl",  "ACT_MELEE_VM_",           nullptr, 0, }, // The Boston Basher
    {  202, AS_DEFAULT,        "models/weapons/c_models/c_minigun/c_minigun.mdl",                       "ACT_PRIMARY_VM_",         nullptr, 0, }, // Upgradeable TF_WEAPON_MINIGUN
    {  201, AS_DEFAULT,        "models/weapons/c_models/c_sniperrifle/c_sniperrifle.mdl",               "ACT_PRIMARY_VM_",         nullptr, 0, }, // Upgradeable TF_WEAPON_SNIPERRIFLE
    {   56, AS_ITEM2,          "models/weapons/c_models/c_bow/c_bow.mdl",                               "ACT_ITEM2_VM_",           k_actRepl_b56, 3, }, // The Huntsman
    {  208, AS_DEFAULT,        "models/weapons/c_models/c_flamethrower/c_flamethrower.mdl",             "ACT_PRIMARY_VM_",         k_actRepl_b208, 3, }, // Upgradeable TF_WEAPON_FLAMETHROWER
    {  209, AS_DEFAULT,        "models/weapons/c_models/c_pistol/c_pistol.mdl",                         "ACT_SECONDARY_VM_",       nullptr, 0, }, // Upgradeable TF_WEAPON_PISTOL
    {  210, AS_DEFAULT,        "models/weapons/c_models/c_revolver/c_revolver.mdl",                     "ACT_SECONDARY_VM_",       nullptr, 0, }, // Upgradeable TF_WEAPON_REVOLVER
    {  140, AS_ITEM1,          "models/weapons/c_models/c_wrangler.mdl",                                "ACT_ITEM1_VM_",           k_actRepl_b140, 3, }, // The Wrangler
    {  526, AS_DEFAULT,        "models/workshop_partner/weapons/c_models/c_dex_sniperrifle/c_dex_sniperrifle.mdl", "ACT_PRIMARY_VM_",         k_actRepl_b526, 3, }, // The Machina
    {   42, AS_ITEM1,          "models/weapons/c_models/c_sandwich/c_sandwich.mdl",                     "ACT_ITEM1_VM_",           k_actRepl_b42, 3, }, // The Sandvich
    {  159, AS_ITEM1,          "models/workshop/weapons/c_models/c_chocolate/c_chocolate.mdl",          "ACT_ITEM1_VM_",           k_actRepl_b159, 3, }, // The Dalokohs Bar
    {  222, AS_ITEM1,          "models/workshop/weapons/c_models/c_madmilk/c_madmilk.mdl",              "ACT_ITEM1_VM_",           k_actRepl_b222, 3, }, // Mad Milk
    {  221, AS_DEFAULT,        "models/workshop/weapons/c_models/c_holymackerel/c_holymackerel.mdl",    "ACT_MELEE_VM_",           nullptr, 0, }, // The Holy Mackerel
    {  153, AS_DEFAULT,        "models/workshop/weapons/c_models/c_sledgehammer/c_sledgehammer.mdl",    "ACT_MELEE_VM_",           nullptr, 0, }, // The Homewrecker
    {   58, AS_ITEM1,          "models/weapons/c_models/urinejar.mdl",                                  "ACT_ITEM1_VM_",           k_actRepl_b58, 3, }, // Jarate
    {  212, AS_NONE,           "models/weapons/v_models/v_watch_spy.mdl",                               nullptr,                   nullptr, 0, }, // Upgradeable TF_WEAPON_INVIS
    {  736, AS_DEFAULT,        "models/weapons/c_models/c_sapper/c_sapper.mdl",                         nullptr,                   nullptr, 0, }, // Upgradeable TF_WEAPON_BUILDER_SPY
    {  405, AS_NONE,           "models/workshop/player/items/demo/demo_booties/demo_booties.mdl",       nullptr,                   nullptr, 0, }, // Ali Baba's Wee Booties
};
static constexpr int k_baseAnimCount = (int)(sizeof(k_baseAnimSlots)/sizeof(k_baseAnimSlots[0]));

inline const BaseAnimEntry* Skin_BaseAnimEntry(int weaponRedir)
{
    for (int i = 0; i < k_baseAnimCount; ++i)
        if (k_baseAnimSlots[i].weaponRedir == weaponRedir) return &k_baseAnimSlots[i];
    return nullptr;
}

inline AnimSlot Skin_BaseAnimSlot(int weaponRedir)
{
    for (int i = 0; i < k_baseAnimCount; ++i)
        if (k_baseAnimSlots[i].weaponRedir == weaponRedir) return k_baseAnimSlots[i].anim;
    return AS_DEFAULT;
}

// The c_model the base weapon plays its viewmodel sequences out of, or null if the
// weapon is not one a reskin group covers.
inline const char* Skin_BaseModel(int weaponRedir)
{
    for (int i = 0; i < k_baseAnimCount; ++i)
        if (k_baseAnimSlots[i].weaponRedir == weaponRedir) return k_baseAnimSlots[i].model;
    return nullptr;
}

