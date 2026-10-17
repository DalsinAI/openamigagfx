#!/usr/bin/env python3
"""OpenGfx and OpenLayers: every argument narrow_args.txt lists (NDK 3.2's
SFDs give it as WORD, UWORD, BYTE, UBYTE or BOOL) is taken narrow by its LVO
entry, or cast to that width in the entry's body: a caller built with NDK
3.2's inlines sets only the low part of the register.
  check_narrow.py narrow_args.txt LIBRARYDIR       check the sources
  check_narrow.py --sfd graphics_lib.sfd ...       write the table from SFDs"""
import os, re, sys

NARROW = {"WORD", "UWORD", "BYTE", "UBYTE", "BOOL"}

def from_sfd(paths):
    for path in paths:
        lib = os.path.basename(path).split("_")[0]
        sfd = open(path, encoding="latin-1").read()
        for m in re.finditer(r"^[A-Za-z][\w\s\*]*?\b(\w+)\s*\(([^()]*)\)\s*\(([^()]*)\)", sfd, re.M):
            name, args, regs = m.groups()
            al = [a.strip() for a in args.split(",") if a.strip()]
            rl = [r.strip().lower() for r in regs.replace("/", ",").split(",") if r.strip()]
            if not rl or len(al) != len(rl) or "..." in args:
                continue
            for a, r in zip(al, rl):
                t = a.replace("CONST", "").split()
                if "*" not in a and t and t[0] in NARROW:
                    print("%s %s %s %s %s" % (lib, name, r, t[0], t[-1]))

def body_of(src, end):
    depth, i = 1, end
    while depth:
        c = src[i]; depth += (c == "{") - (c == "}"); i += 1
    return src[end:i]

def check(table, libdir):
    want = {}
    for line in open(table):
        if line.strip() and not line.startswith("#"):
            lib, name, reg, t, arg = line.split()
            want.setdefault((lib, name), []).append((reg, t, arg))
    srcs = []
    for d, _, fs in os.walk(libdir):
        srcs += [open(os.path.join(d, f), encoding="latin-1").read() for f in fs if f.endswith(".c")]
    bad = n = 0
    for (lib, name), args in sorted(want.items()):
        pat = (r"\bgfx_lvo_%s\s*\(" if lib == "graphics" else r"\bL_%s\s*\(") % name
        hit = None
        for src in srcs:
            m = re.search(pat + r"((?:[^(){};]|\([^()]*\))*)\)\s*\{", src)
            if m:
                hit = (m.group(1), body_of(src, m.end())); break
        if not hit:
            print("none   %s %s (not written)" % (lib, name)); continue
        params = {r: d.strip() for r, d in re.findall(r"\b(?:GFX_)?REG\((\w+),\s*([^)]*)\)", hit[0])}
        for reg, t, arg in args:
            n += 1
            d = params.get(reg)
            v = re.findall(r"(\w+)\s*$", d)[0] if d else None
            if d is None:
                st = "takes no %s" % reg
            elif re.search(r"\b(u?int(16|8)_t|U?WORD|U?BYTE|BOOL)\b", d) and not re.search(r"\b(U?LONG|u?int32_t)\b", d):
                # a narrow register parameter: the os32-gcc16 stove's GCC reads only its low part
                # (measured at -Os and -O2: tst.w d0, ext.l d0, move.w d0,a0)
                continue
            else:
                cast = r"\((u?int(16|8)_t|U?WORD|U?BYTE|BOOL)\)\s*\(?\s*%s\b" % v
                rest = re.sub(cast, "", hit[1])
                if re.search(cast, hit[1]) and not re.search(r"(?<![\w.>])\b%s\b(?!\s*=[^=])" % v, rest):
                    continue
                st = "uses %s (%s) at 32 bits" % (reg, t)
            bad += 1
            print("FAIL   %s %s: %s" % (lib, name, st))
    print("check_narrow: %d narrow arguments, %d not narrowed" % (n, bad))
    return bad == 0

if __name__ == "__main__":
    if sys.argv[1] == "--sfd":
        from_sfd(sys.argv[2:])
    else:
        sys.exit(0 if check(sys.argv[1], sys.argv[2]) else 1)
