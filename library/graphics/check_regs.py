#!/usr/bin/env python3
"""OpenGfx graphics.library: every gfx_lvo_<Call> definition in the given C
files takes graphics.lvo's registers in its order (then A6), is defined
once, and in its owner's directory (OWNERS.md). Exit 1 on any difference.

  check_regs.py graphics.lvo OWNERS.md file.c ...      MIT, Dalsin Limited."""
import os, re, sys

def read_lvo(path):
    out = {}
    for line in open(path):
        f = line.split("#")[0].split()
        if not f: continue
        name = "private_%d" % -int(f[0]) if f[1] == "private" else f[1]
        out[name] = f[2].split(",") if len(f) > 2 else []
    return out

def read_owners(path):
    out = {}
    for line in open(path):
        m = re.match(r"\|\s*-?\d+\s*\|\s*(\w+)\s*\|[^|]*\|\s*(gfx-\w+)\s*\|\s*`gfx_lvo_(\w+)`", line)
        if m: out[m.group(3)] = m.group(2)
    return out

DIRS = {"graphics": "gfx-face", "display": "gfx-display", "text": "gfx-text", "gels": "gfx-gels"}

def defs(path):
    src = open(path, encoding="latin-1").read()
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    src = re.sub(r"//[^\n]*", " ", src)
    for m in re.finditer(r"\bgfx_lvo_(\w+)\s*\(", src):
        i, depth = m.end(), 1
        while depth and i < len(src):
            depth += {"(": 1, ")": -1}.get(src[i], 0)
            i += 1
        params = src[m.end():i - 1]
        rest = src[i:].lstrip()
        if not rest.startswith("{"): continue          # a declaration or a use
        regs = re.findall(r"\b(?:GFX_)?REG\(\s*(\w+)\s*,", params)
        yield m.group(1), regs

lvo, owners = read_lvo(sys.argv[1]), read_owners(sys.argv[2])
bad, seen = 0, {}
for path in sys.argv[3:]:
    area = DIRS.get(os.path.basename(os.path.dirname(os.path.abspath(path))))
    for name, regs in defs(path):
        if name not in lvo:
            print("%s: gfx_lvo_%s is no LVO of graphics.lvo" % (path, name)); bad = 1; continue
        if name in seen:
            print("%s: gfx_lvo_%s defined again (first in %s)" % (path, name, seen[name])); bad = 1
        seen[name] = path
        if regs and regs[-1] == "a6": regs = regs[:-1]
        if regs != lvo[name]:
            print("%s: gfx_lvo_%s takes %s, graphics.lvo says %s" % (path, name, ",".join(regs) or "nothing",
                                                                     ",".join(lvo[name]) or "nothing")); bad = 1
        if area and owners.get(name) and owners[name] != area:
            print("%s: gfx_lvo_%s is %s's (OWNERS.md), not %s's" % (path, name, owners[name], area)); bad = 1
missing = [n for n in lvo if n not in seen]
print("check_regs: %d of %d LVOs defined, registers %s; not yet: %s"
      % (len(seen), len(lvo), "differ" if bad else "match", " ".join(missing) or "none"))
sys.exit(bad)
