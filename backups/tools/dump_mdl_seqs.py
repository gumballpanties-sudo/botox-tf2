import os, struct, sys

TF = r"C:\Program Files (x86)\Steam\steamapps\common\Team Fortress 2\tf"

def read_cstr(b, o):
    e = b.index(b"\x00", o)
    return b[o:e].decode("ascii", "replace"), e + 1

def load_vpk(dirvpk):
    b = open(dirvpk, "rb").read()
    sig, ver, tree = struct.unpack_from("<III", b, 0)
    assert sig == 0x55AA1234, hex(sig)
    o = 12 if ver == 1 else 28
    tree_start = o
    files = {}
    while True:
        ext, o = read_cstr(b, o)
        if ext == "": break
        while True:
            path, o = read_cstr(b, o)
            if path == "": break
            while True:
                name, o = read_cstr(b, o)
                if name == "": break
                crc, pre, arch, off, ln, term = struct.unpack_from("<IHHIIH", b, o)
                o += 18
                preload = b[o:o+pre]; o += pre
                full = (path + "/" if path != " " else "") + name + "." + ext
                files[full.lower()] = (arch, off, ln, preload, dirvpk)
    return files

def extract(files, name):
    key = name.lower().replace("\\", "/")
    if key not in files: return None
    arch, off, ln, preload, dirvpk = files[key]
    if ln == 0: return preload
    if arch == 0x7FFF:
        b = open(dirvpk, "rb").read()
        sig, ver, tree = struct.unpack_from("<III", b, 0)
        base = (12 if ver == 1 else 28) + tree
        return preload + b[base+off:base+off+ln]
    apath = dirvpk.replace("_dir.vpk", "_%03d.vpk" % arch)
    with open(apath, "rb") as f:
        f.seek(off); return preload + f.read(ln)

SEQ_STRIDE = 212
def seqs(mdl):
    assert mdl[:4] == b"IDST", mdl[:4]
    ver = struct.unpack_from("<i", mdl, 4)[0]
    numseq, seqidx = struct.unpack_from("<ii", mdl, 0xBC)
    numinc, incidx = struct.unpack_from("<ii", mdl, 0x150)
    inc = []
    for i in range(numinc):
        base = incidx + i*8
        szlabel, szname = struct.unpack_from("<ii", mdl, base)
        s, _ = read_cstr(mdl, base + szname)
        inc.append(s)
    out = []
    for i in range(numseq):
        d = seqidx + i*SEQ_STRIDE
        szlabel, szact = struct.unpack_from("<ii", mdl, d+4)
        label, _ = read_cstr(mdl, d + szlabel) if szlabel else ("", 0)
        act, _ = read_cstr(mdl, d + szact) if szact else ("", 0)
        out.append((i, label, act))
    return ver, out, inc

def find(files_list, name):
    for f in files_list:
        r = extract(f, name)
        if r: return r
    return None

if __name__ == "__main__":
    dirs = [os.path.join(TF, x) for x in os.listdir(TF) if x.endswith("_dir.vpk")]
    tables = [load_vpk(d) for d in dirs]
    print("vpks:", [os.path.basename(d) for d in dirs])
    for target in sys.argv[1:]:
        data = find(tables, target)
        if not data:
            print("MISSING", target); continue
        ver, sq, inc = seqs(data)
        print("== %s  ver=%d  seqs=%d  includes=%s" % (target, ver, len(sq), inc))
        for i, label, act in sq:
            print("   %3d %-34s %s" % (i, label, act))
