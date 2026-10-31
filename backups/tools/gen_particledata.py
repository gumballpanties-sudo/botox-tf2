# Emit src/data/particledata.h for dih from TF2's own items_game.txt + tf_english.txt.
import re, json, sys

DATA = json.load(open('particles.json', encoding='utf-8'))

# fill the 6 unnamed team-blue twins from the red entry one id below
byid = {}
for sect, lst in DATA.items():
    for e in lst:
        byid[e['id']] = e
for sect, lst in DATA.items():
    for e in lst:
        if e['name']:
            continue
        twin = byid.get(e['id'] - 1)
        if twin and twin['name'] and 'teamcolor_blue' in e['system']:
            e['name'] = twin['name'] + ' (Blue)'
        elif e['system']:
            e['name'] = e['system']


def esc(s):
    return s.replace('\\', '\\\\').replace('"', '\\"')


def emit(varname, entries, extra=None):
    out = ['static const ParticleEntry %s[] = {' % varname]
    if extra:
        for nm, i in extra:
            out.append('    { "%s", %d },' % (esc(nm), i))
    for e in entries:
        out.append('    { "%s", %d },' % (esc(e['name']), e['id']))
    out.append('};')
    return '\n'.join(out)


hdr = []
hdr.append('#pragma once')
hdr.append('// Unusual / attached particle effects, generated from TF2\'s own')
hdr.append('// tf/scripts/items/items_game.txt "attribute_controlled_attached_particles" block,')
hdr.append('// names from tf/resource/tf_english.txt "Attrib_Particle<n>".')
hdr.append('// Written to attribute 134 (set_attached_particle) + 370 (set_attached_particle_static).')
hdr.append('// Do not hand-edit -- regenerate against items_game.txt when TF2 adds effects.')
hdr.append('')
hdr.append('struct ParticleEntry { const char* name; int id; };')
hdr.append('struct ParticleGroup { const char* name; const ParticleEntry* items; int count; };')
hdr.append('')

hdr.append('// id 0 is the schema\'s "no effect" slot -- TF2 shows it as Invalid Particle.')
hdr.append(emit('k_partOther', DATA['other_particles'],
                extra=[('None', -1), ('Invalid Particle', 0)]))
hdr.append('')
hdr.append(emit('k_partCosmetic', DATA['cosmetic_unusual_effects']))
hdr.append('')
hdr.append(emit('k_partWeapon', DATA['weapon_unusual_effects']))
hdr.append('')
hdr.append(emit('k_partTaunt', DATA['taunt_unusual_effects']))
hdr.append('')
hdr.append('#define PG(nm, arr) { nm, arr, (int)(sizeof(arr)/sizeof(arr[0])) }')
hdr.append('static const ParticleGroup k_particleGroups[] = {')
hdr.append('    PG("Weapon unusuals", k_partWeapon),')
hdr.append('    PG("Basic / community", k_partOther),')
hdr.append('    PG("Cosmetic unusuals", k_partCosmetic),')
hdr.append('    PG("Taunt unusuals",  k_partTaunt),')
hdr.append('};')
hdr.append('#undef PG')
hdr.append('static constexpr int k_particleGroupCount = (int)(sizeof(k_particleGroups)/sizeof(k_particleGroups[0]));')
hdr.append('')
hdr.append('// Display name for a stored SkinCfg::unusual value. 0 is a real id here, so')
hdr.append('// "not set" is -1 in the UI and 0 in the cfg only when the user picked Invalid Particle.')
hdr.append('inline const char* Particle_Name(int id)')
hdr.append('{')
hdr.append('    for (int g = 0; g < k_particleGroupCount; ++g)')
hdr.append('        for (int i = 0; i < k_particleGroups[g].count; ++i)')
hdr.append('            if (k_particleGroups[g].items[i].id == id) return k_particleGroups[g].items[i].name;')
hdr.append('    return "(custom)";')
hdr.append('}')
hdr.append('')

open(sys.argv[1], 'w', encoding='utf-8').write('\n'.join(hdr) + '\n')
n = sum(len(v) for v in DATA.values())
print('wrote', sys.argv[1], n, 'effects')
