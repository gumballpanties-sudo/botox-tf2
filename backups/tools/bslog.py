"""Auto backstab debug.log analyser (dih-bs15 / dih-bs16 BS: lines).

usage: python tools/bslog.py C:\\dih\\debug.log

Prints: build stamp, what each swing turned into (BACKSTAB / FRONT / WHIFF), calibration by the
promised p, the turn-bet split that motivated bs16's turn guard, and the refusals by gate.
Read with src/features/aim/bs_math.h (TurnGuarded) open.
"""
import re
import sys
from collections import Counter

KV = re.compile(r'(\w+)=(-?[\d.]+)')
FRAC = {0: 0.0, 1: 0.5, 2: 1.0}   # k_estFrac: hold, half rate, full rate


def parse(path):
    stamp, seq = None, []
    for n, line in enumerate(open(path, encoding='utf-8', errors='replace'), 1):
        if 'BACKSTAB BUILD' in line:
            stamp = line.strip()
        elif 'BS: STAB' in line:
            d = {k: float(v) for k, v in KV.findall(line)}
            seq.append(('F', n, d))
        elif 'BS: outcome' in line:
            seq.append(('O', n, {'bs': 'NOT A BACKSTAB' not in line}))
        elif 'BS: WHIFF' in line:
            seq.append(('W', n, {}))
        elif 'BS: miss' in line:
            parts = line.split('|')
            d = {k: float(v) for k, v in KV.findall(parts[0])}
            d['why'] = re.search(r'why=(\w+)', line).group(1)
            d['gates'] = {k: int(float(v)) for k, v in KV.findall(parts[2])} if len(parts) > 2 else {}
            seq.append(('M', n, d))
    return stamp, seq


def fires_with_results(seq):
    out = []
    for i, (t, n, d) in enumerate(seq):
        if t != 'F':
            continue
        nxt = next(((t2, d2) for t2, _, d2 in seq[i + 1:] if t2 in 'FOW'), (None, None))
        r = 'none'
        if nxt[0] == 'O':
            r = 'BS' if nxt[1]['bs'] else 'FRONT'
        elif nxt[0] == 'W':
            r = 'WHIFF'
        out.append((n, d, r))
    return out


def row(name, rs):
    c = Counter(rs)
    judged = c['BS'] + c['FRONT']
    rate = f'{c["BS"] / judged:.2f}' if judged else '  - '
    print(f'  {name:44s} n={len(rs):4d} BS={c["BS"]:4d} FRONT={c["FRONT"]:3d} WHIFF={c["WHIFF"]:2d} none={c["none"]:2d} rate={rate}')


def main():
    stamp, seq = parse(sys.argv[1])
    print(stamp or 'NO BUILD STAMP (older than bs15 -- tier= logs are not this format)')
    res = fires_with_results(seq)
    row('all swings', [r for _, _, r in res])

    print('\nby promised p:')
    for lo, hi in ((0.7, 0.8), (0.8, 0.9), (0.9, 0.97), (0.97, 1.01)):
        row(f'p [{lo:.2f}, {hi:.2f})', [r for _, d, r in res if lo <= d['p'] < hi])

    print('\nturn bets (the bet\'s extrapolated turn >= its window margin):')
    bet = lambda d: FRAC.get(int(d['k']), 0.5) * abs(d['rate']) * d['hor'] >= d['margin']
    row('turn bet', [r for _, d, r in res if bet(d)])
    row('not a turn bet', [r for _, d, r in res if not bet(d)])
    if any('pg' in d for _, d, _ in res):
        print('\nbs16 guard (pg = odds at seen + k_turnTrust of the turn):')
        row('guard was the binding number (pg < pb)', [r for _, d, r in res if d.get('pg', 1) < d.get('pb', 0)])
        row('bet was binding', [r for _, d, r in res if d.get('pg', 1) >= d.get('pb', 0)])

    print('\nfront stabs:')
    for n, d, r in res:
        if r == 'FRONT':
            extra = f" pb={d['pb']:.3f} pg={d['pg']:.3f} gm={d.get('gm', 0):.1f} sc={d.get('sc', 0):.1f}" if 'pg' in d else ''
            print(f"  L{n} p={d['p']:.3f}{extra} margin={d['margin']:.1f} sig={d['sig']:.1f} k={int(d['k'])} "
                  f"rate={d['rate']:.0f} hor={d['hor']:.3f} held={int(d['held'])}")
    print('whiffs:')
    for n, d, r in res:
        if r == 'WHIFF':
            print(f"  L{n} slack={d['slack']:.1f} pad={d['pad']:.2f} d={d['d']:.1f} spd={d['spd']:.0f} my={d['my']:.0f}")

    misses = [d for t, _, d in seq if t == 'M']
    gates = Counter()
    for m in misses:
        for k, v in m['gates'].items():
            if k not in ('fires', 'stabs', 'front', 'whiff'):
                gates[k] += v
    print('\nrefusals, commands per gate:', dict(gates))
    turns = [m for m in misses if m['why'] == 'turn']
    if turns:
        print(f'why=turn sample lines: {len(turns)} (bs15 would have swung here; gm < 0 = guard point outside his window)')
        for m in turns[:15]:
            print(f"  idx={int(m['idx'])} pb={m['pb']:.3f} pg={m['pg']:.3f} gm={m.get('gm', 0):.1f} sc={m.get('sc', 0):.1f} "
                  f"margin={m['margin']:.1f} rate={m['rate']:.0f} hor={m['hor']:.3f} k={int(m['k'])}")


if __name__ == '__main__':
    main()
