# Emit the reskin info table (models / anim slots / style skins / turn-to-gold)
# straight out of items_game.txt, so no path is hand-typed.
import os
import re
import sys
import items_game as gen
import dump_mdl_seqs as vpk


# ---- kill icons -------------------------------------------------------------------------------
# The death notice icon is literally "d_" + the event's "weapon" string
# (game/client/tf/hud_basedeathnotice.cpp:515), looked up in the HUD texture table built from
# tf/scripts/mod_textures.txt -- which lives in tf2_misc_dir.vpk and keys its 417 entries WITHOUT
# the prefix (the "d_" comes from its own TextureFileRefs "dfile" block).
#
# Five reskins have an item_logname the file has no entry for (skullbat, prinny_machete,
# awper_hand, giger_counter, shooting_star), so writing that name into the event makes the HUD
# fail the lookup and draw the default skull. Those emit logname = nullptr, and the runtime
# leaves the event alone -- which keeps the BASE weapon's own icon, since the string already
# there is the one the game fired the kill with.
def icon_names():
    dirs = [os.path.join(vpk.TF, x) for x in os.listdir(vpk.TF) if x.endswith('_dir.vpk')]
    data = vpk.find([vpk.load_vpk(d) for d in dirs], 'scripts/mod_textures.txt')
    if not data:
        print('WARNING: scripts/mod_textures.txt not in the VPKs -- lognames emitted unchecked')
        return None
    txt = data.decode('utf-8', 'replace')
    return set(m.group(1).lower() for m in re.finditer(r'^\t\t"([A-Za-z0-9_]+)"', txt, re.M))


ICONS = icon_names()
dropped = []


def logname_of(defi):
    lg = gen.resolved(defi, 'item_logname')
    if lg and ICONS is not None and lg.lower() not in ICONS:
        dropped.append((defi, gen.name(defi), lg))
        return None
    return lg

# every defindex a ReskinVariant in skindata.h can select
RESKINS = [30667, 221, 264, 474, 880, 939, 1013, 1127, 423, 1071, 954, 30758,
           739, 609, 1100, 169, 727, 638, 482, 266, 457, 298, 851, 1092,
           741, 30474, 160, 30666, 161, 30668, 30665, 863, 433, 1121, 572,
           466, 1105, 297, 947, 933, 1102, 608, 574, 452]

# every weaponRedir that owns a reskin group -- the anim slot a reskin is remapped FROM
BASES = [190, 191, 192, 193, 194, 195, 196, 197, 198, 38, 132, 225, 325, 202,
         201, 56, 208, 209, 210, 140, 526, 42, 159, 222, 221, 153, 58, 212,
         736, 405]

SLOT = {None: 'AS_DEFAULT', 'melee': 'AS_DEFAULT',
        'melee_allclass': 'AS_MELEE_ALLCLASS', 'item1': 'AS_ITEM1',
        'item2': 'AS_ITEM2', 'building': 'AS_NONE', 'force_not_used': 'AS_NONE'}

# The activity family an anim_slot names. TF2 does this with s_viewmodelacttable
# (tf_weaponbase.cpp:4200+), one row per (base activity, family): ACT_VM_DRAW + TF_WPN_TYPE_ITEM1
# -> ACT_ITEM1_VM_DRAW. Every row of a family is the same string surgery, so the prefix is all
# that has to be carried: "ACT_VM_<X>" becomes "<prefix><X>".
#
# A slot with no family (the sapper's "building": the arms model names its sequences ACT_VM_DRAW /
# ACT_VM_IDLE outright) emits nullptr, and the runtime does no family swap for it. A wrong guess
# here cannot break anything either: every translated name is looked up in the model's own
# sequence table first and dropped if it is not there.
SLOT_PFX = {'melee': 'ACT_MELEE_VM_', 'melee_allclass': 'ACT_MELEE_ALLCLASS_VM_',
            'item1': 'ACT_ITEM1_VM_', 'item2': 'ACT_ITEM2_VM_', 'item3': 'ACT_ITEM3_VM_',
            'item4': 'ACT_ITEM4_VM_', 'primary': 'ACT_PRIMARY_VM_',
            'primary2': 'ACT_PRIMARY_VM_', 'secondary': 'ACT_SECONDARY_VM_',
            'secondary2': 'ACT_SECONDARY2_VM_', 'pda': 'ACT_PDA_VM_'}
# Fallback when the item names no anim_slot at all: the loadout slot it lives in.
ITEMSLOT_PFX = {'melee': 'ACT_MELEE_VM_', 'primary': 'ACT_PRIMARY_VM_',
                'secondary': 'ACT_SECONDARY_VM_'}


def slot_of(defi):
    v = gen.resolved(defi, 'anim_slot')
    key = v.lower() if v else None
    return SLOT.get(key, 'AS_NONE')


def pfx_of(defi):
    v = gen.resolved(defi, 'anim_slot')
    if v:
        return SLOT_PFX.get(v.lower())
    s = gen.resolved(defi, 'item_slot')
    return ITEMSLOT_PFX.get(s.lower()) if s else None


def cstr(s):
    return ('"%s"' % s) if s else 'nullptr'


# One ActPair array per item that has an animation_replacement block, emitted ahead of the tables.
def emit_pairs(defi, tag):
    pairs = gen.anim_replacements(defi)
    if not pairs:
        return 'nullptr, 0,'
    nm = 'k_actRepl_%s%d' % (tag, defi)
    out.append('static const ActPair %s[] = {' % nm)
    for a, b in pairs:
        out.append('    { %-42s %s },' % ('"%s",' % a, '"%s"' % b))
    out.append('};')
    return '%s, %d,' % (nm, len(pairs))


out = []
out.append('// ---- Reskin runtime info, generated from items_game.txt ------------------------------')
out.append('// model  : "model_player" (walked through the item\'s prefab chain). fpsoverlay typed')
out.append('//          these by hand and got 15 of them wrong -- a path the engine cannot load')
out.append('//          silently leaves the stock model on screen.')
out.append('// anim   : "anim_slot". A reskin whose slot differs from the weapon it replaces needs')
out.append('//          its sequences remapped (ACT_MELEE_VM_* -> ACT_MELEE_ALLCLASS_VM_*, ...).')
out.append('// skins  : "visuals/styles/<n>/skin_red|skin_blu" -- what makes a Gold Frying Pan gold.')
out.append('// gold   : the item carries set_turn_to_gold (attr 150): Saxxy, Golden Wrench, GFP.')
out.append('// logname: "item_logname", which is what the killfeed reads to pick its icon -- but only')
out.append('//          when tf/scripts/mod_textures.txt has an entry for it. nullptr = it has none')
out.append('//          (skullbat, prinny_machete, awper_hand, giger_counter, shooting_star), and')
out.append('//          the event must be left alone: "d_<name>" with no entry draws the SKULL.')
out.append('// pfx    : the activity family the item\'s anim_slot names, as a prefix.')
out.append('// repl   : "visuals/animation_replacement" -- the item\'s OWN activity overrides, which')
out.append('//          TF2 consults BEFORE the anim_slot family table (tf_weaponbase.cpp:4419), so')
out.append('//          this, not anim_slot, is what actually decides which sequence plays. 27 of the')
out.append('//          44 reskins carry one; the Snack Attack is nothing BUT one (its slot is')
out.append('//          "building", same as the stock sapper -- ACT_VM_DRAW -> ACT_BREADSAPPER_VM_DRAW')
out.append('//          is the entire difference).')
out.append('enum AnimSlot : uint8_t { AS_DEFAULT = 0, AS_MELEE_ALLCLASS, AS_ITEM1, AS_ITEM2, AS_NONE };')
out.append('')
out.append('// One entry of an animation_replacement block: the activity the weapon code asks for,')
out.append('// and the activity this item plays instead.')
out.append('struct ActPair { const char* from; const char* to; };')
out.append('')

# ---- the per-item replacement arrays, emitted before the tables that point at them ----
reskin_pairs = {}
base_pairs = {}
for d in RESKINS:
    reskin_pairs[d] = emit_pairs(d, 'r')
for d in BASES:
    base_pairs[d] = emit_pairs(d, 'b')
out.append('')

out.append('static constexpr int k_maxReskinStyles = 4;')
out.append('struct ReskinInfo {')
out.append('    int      defindex;')
out.append('    const char* model;          // nullptr = the item has no c_model of its own')
out.append('    const char* logname;        // "item_logname" -- the killfeed icon name')
out.append('    AnimSlot anim;')
out.append('    uint8_t  styleCount;        // 0 = no styles block, skins come from the team default')
out.append('    int8_t   skinRed[k_maxReskinStyles];')
out.append('    int8_t   skinBlu[k_maxReskinStyles];')
out.append('    bool     turnToGold;        // attr 150')
out.append('    bool     specialTaunt;      // attr 551 special_taunt -- the Saxxy/Rainblower taunt kill')
out.append('    const char* animPfx;        // activity family prefix, null when the slot names none')
out.append('    const ActPair* actRepl;     // animation_replacement, null when the item has none')
out.append('    uint8_t  actReplCount;')
out.append('};')
out.append('')
out.append('static const ReskinInfo k_reskinInfo[] = {')
for d in RESKINS:
    nm = gen.name(d)
    mdl = gen.resolved(d, 'model_player')
    st = gen.styles(d)
    at = gen.attr_values(d)
    st = st[:4]
    sr = [str(s[1]) for s in st] + ['-1'] * (4 - len(st))
    sb = [str(s[2]) for s in st] + ['-1'] * (4 - len(st))
    lg = logname_of(d)
    out.append('    { %6d, %-78s %-24s %-18s %d, {%s}, {%s}, %-5s %-6s %-26s %s }, // %s' % (
        d,
        ('"%s",' % mdl) if mdl else 'nullptr,',
        ('"%s",' % lg) if lg else 'nullptr,',
        slot_of(d) + ',',
        len(st),
        ','.join(sr), ','.join(sb),
        ('true,' if at.get('set_turn_to_gold') else 'false,'),
        ('true,' if at.get('special_taunt') else 'false,'),
        cstr(pfx_of(d)) + ',',
        reskin_pairs[d],
        nm))
out.append('};')
out.append('static constexpr int k_reskinInfoCount = (int)(sizeof(k_reskinInfo)/sizeof(k_reskinInfo[0]));')
out.append('')
out.append('inline const ReskinInfo* Skin_FindReskinInfo(int defindex)')
out.append('{')
out.append('    for (int i = 0; i < k_reskinInfoCount; ++i)')
out.append('        if (k_reskinInfo[i].defindex == defindex) return &k_reskinInfo[i];')
out.append('    return nullptr;')
out.append('}')
out.append('')
out.append('// The weapon a reskin can be applied TO -- the "from" half of a remap.')
out.append('// model is that weapon\'s own "model_player" c_model: the sequence table the game picked')
out.append('// its index out of before we rewrote the def index. Generated rather than read back off')
out.append('// the live entity, because by the second pre-write pass every model field on that')
out.append('// weapon is already OURS and reading one back returns the reskin.')
out.append('struct BaseAnimEntry {')
out.append('    int weaponRedir; AnimSlot anim; const char* model;')
out.append('    const char* animPfx;        // the family the BASE plays in -- the "from" side')
out.append('    const ActPair* actRepl;     // the base\'s own overrides, applied before the family')
out.append('    uint8_t actReplCount;')
out.append('};')
out.append('static const BaseAnimEntry k_baseAnimSlots[] = {')
for d in BASES:
    bm = gen.resolved(d, 'model_player')
    out.append('    { %4d, %-18s %-72s %-26s %s }, // %s' % (
        d, slot_of(d) + ',',
        (('"%s",' % bm) if bm else 'nullptr,'),
        cstr(pfx_of(d)) + ',',
        base_pairs[d],
        gen.name(d)))
out.append('};')
out.append('static constexpr int k_baseAnimCount = (int)(sizeof(k_baseAnimSlots)/sizeof(k_baseAnimSlots[0]));')
out.append('')
out.append('inline const BaseAnimEntry* Skin_BaseAnimEntry(int weaponRedir)')
out.append('{')
out.append('    for (int i = 0; i < k_baseAnimCount; ++i)')
out.append('        if (k_baseAnimSlots[i].weaponRedir == weaponRedir) return &k_baseAnimSlots[i];')
out.append('    return nullptr;')
out.append('}')
out.append('')
out.append('inline AnimSlot Skin_BaseAnimSlot(int weaponRedir)')
out.append('{')
out.append('    for (int i = 0; i < k_baseAnimCount; ++i)')
out.append('        if (k_baseAnimSlots[i].weaponRedir == weaponRedir) return k_baseAnimSlots[i].anim;')
out.append('    return AS_DEFAULT;')
out.append('}')
out.append('')
out.append('// The c_model the base weapon plays its viewmodel sequences out of, or null if the')
out.append('// weapon is not one a reskin group covers.')
out.append('inline const char* Skin_BaseModel(int weaponRedir)')
out.append('{')
out.append('    for (int i = 0; i < k_baseAnimCount; ++i)')
out.append('        if (k_baseAnimSlots[i].weaponRedir == weaponRedir) return k_baseAnimSlots[i].model;')
out.append('    return nullptr;')
out.append('}')
out.append('')

open(sys.argv[1], 'w', encoding='utf-8').write('\n'.join(out) + '\n')
print('wrote', sys.argv[1], len(RESKINS), 'reskins,', len(BASES), 'bases')
if ICONS is not None:
    print('mod_textures.txt icons:', len(ICONS), '| lognames with no icon ->'
          ' nullptr:', len(dropped))
    for d, nm, lg in dropped:
        print('   %6d %-32s %s' % (d, nm, lg))
