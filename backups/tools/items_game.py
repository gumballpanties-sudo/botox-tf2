# Generate dih data headers from TF2's own items_game.txt + tf_english.txt.
import re, sys, json, io

IG = r'C:\Program Files (x86)\Steam\steamapps\common\Team Fortress 2\tf\scripts\items\items_game.txt'
LOC = r'C:\Program Files (x86)\Steam\steamapps\common\Team Fortress 2\tf\resource\tf_english.txt'
raw = open(IG, encoding='utf-8', errors='replace').read()
lines = raw.split('\n')


def top_block(name):
    depth = 0
    pending = None
    for i, l in enumerate(lines):
        s = l.strip()
        if re.fullmatch(r'"[^"]+"', s):
            pending = s.strip('"'); continue
        if s.startswith('{'):
            depth += 1
            if depth == 2 and pending == name:
                return i
            continue
        if s.startswith('}'):
            depth -= 1
    return None


# ---------- item spans ----------
start = top_block('items')
d = 0; cur = None; s0 = None; spans = {}; pending = None
for i in range(start, len(lines)):
    s = lines[i].strip()
    if not s: continue
    if re.fullmatch(r'"[^"]+"', s):
        pending = s.strip('"'); continue
    if s.startswith('{'):
        d += 1
        if d == 2: cur = pending; s0 = i
        continue
    if s.startswith('}'):
        if d == 2 and cur is not None: spans[cur] = (s0, i)
        d -= 1
        if d == 0: break

# ---------- prefabs (for inherited anim_slot / model) ----------
pstart = top_block('prefabs')
d = 0; cur = None; s0 = None; pspans = {}; pending = None
for i in range(pstart, len(lines)):
    s = lines[i].strip()
    if not s: continue
    if re.fullmatch(r'"[^"]+"', s):
        pending = s.strip('"'); continue
    if s.startswith('{'):
        d += 1
        if d == 2: cur = pending; s0 = i
        continue
    if s.startswith('}'):
        if d == 2 and cur is not None: pspans[cur] = (s0, i)
        d -= 1
        if d == 0: break


def item_text(defi):
    sp = spans.get(str(defi))
    return '\n'.join(lines[sp[0]:sp[1] + 1]) if sp else None


def prefab_text(nm):
    sp = pspans.get(nm)
    return '\n'.join(lines[sp[0]:sp[1] + 1]) if sp else None


def field(txt, key):
    m = re.search(r'"%s"\s+"([^"]*)"' % key, txt)
    return m.group(1) if m else None


def resolved(defi, key, depth=0):
    """field on the item, else walked up its prefab chain."""
    txt = item_text(defi)
    if txt is None: return None
    v = field(txt, key)
    if v: return v
    pf = field(txt, 'prefab')
    seen = set()
    stack = list(pf.split()) if pf else []
    while stack:
        nm = stack.pop(0)
        if nm in seen: continue
        seen.add(nm)
        pt = prefab_text(nm)
        if not pt: continue
        v = field(pt, key)
        if v: return v
        pf2 = field(pt, 'prefab')
        if pf2: stack.extend(pf2.split())
    return None


def styles(defi):
    """[(skin_red, skin_blu)] per style index, or []"""
    txt = item_text(defi)
    if not txt: return []
    m = re.search(r'"styles"\s*\n\s*\{(.*?)\n\t{4}\}', txt, re.S)
    if not m:
        # looser: grab from "styles" to the matching close by brace counting
        i = txt.find('"styles"')
        if i < 0: return []
        d2 = 0; started = False; j = i
        while j < len(txt):
            if txt[j] == '{': d2 += 1; started = True
            elif txt[j] == '}':
                d2 -= 1
                if started and d2 == 0: break
            j += 1
        blk = txt[i:j + 1]
    else:
        blk = m.group(1)
    out = []
    # per-style sub-blocks in order
    for sm in re.finditer(r'"(\d+)"\s*\n\s*\{(.*?)\n\s*\}', blk, re.S):
        idx = int(sm.group(1)); body = sm.group(2)
        r = re.search(r'"skin_red"\s+"(-?\d+)"', body)
        b = re.search(r'"skin_blu"\s+"(-?\d+)"', body)
        out.append((idx, int(r.group(1)) if r else -1, int(b.group(1)) if b else -1))
    return out


def _block(txt, key):
    """the brace block that follows "key", by brace counting, or None"""
    i = txt.find('"%s"' % key)
    if i < 0: return None
    d = 0; started = False; j = i
    while j < len(txt):
        if txt[j] == '{': d += 1; started = True
        elif txt[j] == '}':
            d -= 1
            if started and d == 0: return txt[i:j + 1]
        j += 1
    return None


def anim_replacements(defi):
    """[(base_activity, replacement_activity)] from the item's "animation_replacement" block.

    This is what CEconItemDefinition::GetActivityOverride answers with, and TF2 consults it
    BEFORE the anim_slot table (tf_weaponbase.cpp:4419), so it -- not anim_slot -- is what
    actually decides which sequence a reskin plays. Walked up the prefab chain like any other
    field; a name is only listed once (first wins, as the schema parser does).
    """
    txts = []
    t = item_text(defi)
    if t: txts.append(t)
    pf = field(t, 'prefab') if t else None
    seen = set()
    stack = list(pf.split()) if pf else []
    while stack:
        nm = stack.pop(0)
        if nm in seen: continue
        seen.add(nm)
        pt = prefab_text(nm)
        if not pt: continue
        txts.append(pt)
        pf2 = field(pt, 'prefab')
        if pf2: stack.extend(pf2.split())

    out = []
    have = set()
    for txt in txts:
        blk = _block(txt, 'animation_replacement')
        if not blk: continue
        for a, b in re.findall(r'"(ACT_[A-Z0-9_]+)"\s+"(ACT_[A-Z0-9_]+)"', blk):
            if a in have: continue
            have.add(a)
            out.append((a, b))
    return out


def attr_values(defi):
    txt = item_text(defi)
    if not txt: return {}
    return dict(re.findall(r'"attribute_class"\s+"([^"]*)"\s*\n\s*"value"\s*"([^"]*)"', txt))


def material_override(defi):
    txt = item_text(defi)
    if not txt: return None
    return field(txt, 'material_override')


def name(defi):
    t = item_text(defi)
    return field(t, 'name') if t else None


if __name__ == '__main__':
    ids = [int(x) for x in sys.argv[1:]]
    for i in ids:
        print(i, '|', name(i), '|', resolved(i, 'model_player'), '| anim=', resolved(i, 'anim_slot'),
              '| styles=', styles(i), '| matover=', material_override(i), '| attrs=', attr_values(i))
