import re, io, sys, json

IG = r'C:\Program Files (x86)\Steam\steamapps\common\Team Fortress 2\tf\scripts\items\items_game.txt'
lines = open(IG, encoding='utf-8', errors='replace').read().split('\n')
START, END = 263853, 267311

# walk: maintain a stack of keys; a bare "name" line pushes a pending key, "{" opens it.
stack = []
pending = None
section = None
cur_id = None
data = {}   # section -> { id: {field:val} }

for i in range(START, END + 1):
    s = lines[i].strip()
    if not s or s.startswith('//'):
        continue
    m = re.fullmatch(r'"([^"]+)"\s+"([^"]*)"', s)
    if m:
        # key/value pair inside the current block
        if len(stack) == 3:
            data[stack[1]][stack[2]][m.group(1)] = m.group(2)
        continue
    m = re.fullmatch(r'"([^"]+)"', s)
    if m:
        pending = m.group(1)
        continue
    if s.startswith('{'):
        stack.append(pending)
        pending = None
        if len(stack) == 2:
            data.setdefault(stack[1], {})
        elif len(stack) == 3:
            data[stack[1]].setdefault(stack[2], {})
        continue
    if s.startswith('}'):
        if stack:
            stack.pop()
        continue

# display names from tf_english.txt
LOC = r'C:\Program Files (x86)\Steam\steamapps\common\Team Fortress 2\tf\resource\tf_english.txt'
names = {}
try:
    raw = open(LOC, encoding='utf-16', errors='replace').read()
except Exception:
    raw = open(LOC, encoding='utf-8', errors='replace').read()
for m in re.finditer(r'"Attrib_Particle(\d+)"\s+"([^"]*)"', raw):
    names[int(m.group(1))] = m.group(2)

out = {}
for sect, ids in data.items():
    lst = []
    for k, v in ids.items():
        try:
            n = int(k)
        except ValueError:
            continue
        lst.append({'id': n, 'system': v.get('system', ''), 'name': names.get(n, '')})
    lst.sort(key=lambda e: e['id'])
    out[sect] = lst

json.dump(out, open(sys.argv[1], 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
for sect, lst in out.items():
    print(sect, len(lst), lst[0] if lst else None, lst[-1] if lst else None)
