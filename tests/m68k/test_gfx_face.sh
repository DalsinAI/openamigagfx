#!/bin/sh
# OpenGfx graphics.library's face (library/graphics) under qemu-m68k: the
# lean C++ face with the core (library/core), built with an m68k cross
# GCC for the 68000, 68020 and 68040 at -O0 and -O2, each run on that CPU
# by tests/m68k/test_gfx_face.c, whose every line a python3 model written
# here, apart from the library, checks: bitmaps and rasters, RastPort
# state and tags, colour maps, pen sharing, regions, Flood, BitMapScale
# and ScalerDiv, DrawEllipse in COMPLEMENT and INVERSVID, ScrollRaster and
# EraseRect in a simple refresh layer (damage, backfill hooks), super
# bitmap layers. Then the library checks: graphics.lvo against the face's
# register entries (check_regs.py), gen_stubs.py, and nm: the face calls
# nothing but the core, libgcc and the hooks, with no .data, .bss or C++
# runtime.
#   sh tests/m68k/test_gfx_face.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
top=$here/../..
g=$top/library/graphics
core=$top/library/core
cc=${M68K_CC:-m68k-linux-gnu-gcc}
cxx=${M68K_CXX:-m68k-linux-gnu-g++}
nm=${M68K_NM:-m68k-linux-gnu-nm}
qemu=${QEMU_M68K:-qemu-m68k}
rounds=${ROUNDS:-24}
command -v "$cc" >/dev/null || { echo "skipped: no $cc"; exit 0; }
command -v python3 >/dev/null || { echo "skipped: no python3"; exit 0; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
fail=0

# ---- the library's tables ---------------------------------------------------
python3 "$g/check_regs.py" "$g/graphics.lvo" "$g/OWNERS.md" "$g/face_lvo.c" > "$tmp/regs" || { cat "$tmp/regs"; fail=1; }
head -c 120 "$tmp/regs"; echo
# check_regs.py must catch a wrong register and a slot in the wrong area
mkdir -p "$tmp/display"
printf 'void gfx_lvo_SetAPen(GFX_REG(a0, void *rp), GFX_REG(d0, unsigned pen)) {}\n' > "$tmp/display/bad.c"
if python3 "$g/check_regs.py" "$g/graphics.lvo" "$g/OWNERS.md" "$tmp/display/bad.c" > "$tmp/bad" 2>&1; then
  echo "FAIL check_regs.py passed a wrong entry"; fail=1
elif ! grep -q "takes a0,d0" "$tmp/bad" || ! grep -q "gfx-face's" "$tmp/bad"; then
  echo "FAIL check_regs.py:"; cat "$tmp/bad"; fail=1
fi
n=$(grep -c '^-[0-9]' "$g/graphics.lvo")
[ "$n" = 172 ] || { echo "FAIL graphics.lvo has $n slots, not 172"; fail=1; }
python3 - "$g/graphics.lvo" "$g/OWNERS.md" <<'PY' || fail=1
import re, sys
lvo = [l.split() for l in open(sys.argv[1]) if l.strip() and not l.startswith("#")]
offs = [int(f[0]) for f in lvo]
assert offs == list(range(-30, -30 - 6 * len(offs), -6)), "graphics.lvo's offsets are not -30 down by 6"
rows = re.findall(r"^\| (-\d+) \| (\w+) \|", open(sys.argv[2]).read(), re.M)
assert [(int(o), n) for o, n in rows] == [(int(f[0]), f[1]) for f in lvo], "OWNERS.md's table is not graphics.lvo's"
print("graphics.lvo: %d slots in order; OWNERS.md lists each" % len(offs))
PY

# ---- the face and the core on each CPU -------------------------------------------
cxxflags="-std=gnu++23 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-asynchronous-unwind-tables
  -fno-unwind-tables -nostdlib++ -ffreestanding -fno-builtin -Wall -Wextra -Werror"
warn="-Wall -Wextra -Werror -Wno-unused-parameter"
inc="-I$g -I$core -I$here/gfx_face_ndk -I$here/../host-ndk"

cat > "$tmp/model.py" <<'PY'
# The model: what each line of test_gfx_face.c's output must say, from
# graphics.library's autodocs and OpenGfx's documented choices (the README's
# edge cases), computed here pixel by pixel and entry by entry.
import sys

lines = open(sys.argv[1]).read().split("\n")
pos = 0
bad = 0
checks = 0

def err(msg):
    global bad
    bad += 1
    if bad <= 20: print("MODEL line %d: %s" % (pos, msg))

def nxt():
    global pos
    l = lines[pos]; pos += 1
    return l

def grid():
    tag, w, h = nxt().split()
    rows = [list(map(int, nxt()[2:])) for _ in range(int(h))]
    return tag, rows

pending_vpc = []
def take_vpc():
    global pending_vpc
    v = pending_vpc; pending_vpc = []
    return v

def rb(w): return ((w + 15) >> 4) * 2

# rastport
rp = {}
# colour map
cm = []; pens = []; refs = []
def rgb(i): return cm[i]
def dist(i, r, g, b):
    R, G, B = cm[i]
    return (R - r) ** 2 + (G - g) ** 2 + (B - b) ** 2

def take_free():
    for i in range(len(cm) - 1, -1, -1):
        if pens[i] == 0: return i
    return -1

while pos < len(lines):
    l = nxt()
    if not l: continue
    f = l.split()
    t = f[0]
    checks += 1
    if t == "ERR": err(l)
    elif t == "END":
        if f[1] != "0": err("harness errors " + f[1])
    elif t == "VPC": pending_vpc.append((int(f[1]), int(f[2])))
    elif t == "BM":
        w, h, d, fl, aa, inj = map(int, f[1:7])
        if f[7] == "none":
            if not inj and not (d > 8 or w == 0 or h == 0): err("AllocBitMap failed: " + l)
            continue
        bpr, rows, depth, flags, contig, chip, clear, W, H, D, F = map(int, f[7:])
        if d > 8: err("AllocBitMap took depth %d" % d); continue
        b = rb(w)
        if fl & 2 and aa: b = (b + 7) & ~7
        il = bool(fl & 4) and d > 1
        if inj and il and not flags & 4: il = False     # the interleaved block failed: plane by plane
        want = (b * d if il else b, h, d, (4 if il else 0) | (fl & 2), 1, 1, 1, b * 8, h, d, ((4 if il else 0) | (fl & 2)) | 8)
        if (bpr, rows, depth, flags, contig, chip, clear, W, H, D, F) != want: err("%s: want %s" % (l, want))
    elif t == "RAS":
        w, h, size, chip = map(int, f[1:])
        want = h * (((w + 15) >> 3) & ~1)
        if size != want or chip != (1 if want else 0): err("%s: want %d" % (l, want))
    elif t == "IBM":
        w, h, d, bpr, rows, flags, kept = map(int, f[1:])
        if bpr != rb(w & 0xFFFF) or rows != h or flags or not kept: err(l)
    elif t == "IRP":
        b = list(map(int, f[1:]))
        want = [0] * 100
        want[24] = 255; want[25] = 255; want[27] = 255; want[28] = 1; want[30] = 15; want[33] = 1
        want[34] = want[35] = 255; want[49] = 1; want[51] = 1; want[54] = 0x40
        if b != want: err("InitRastPort: %s" % [(i, b[i], want[i]) for i in range(100) if b[i] != want[i]])
        rp = dict(a=255, b=0, d=1, o=255, m=255, fl=1)
    elif t == "SETA": rp["a"] = int(f[1]) & 255
    elif t == "SETB": rp["b"] = int(f[1]) & 255
    elif t == "SETD": rp["d"] = int(f[1]) & 255
    elif t == "SETO":
        if int(f[2]) != rp["o"]: err("SetOutlinePen old %s want %d" % (f[2], rp["o"]))
        rp["o"] = int(f[1]) & 255; rp["fl"] |= 8
    elif t == "SETM":
        if f[2] != "1": err(l)
        rp["m"] = int(f[1]) & 255
    elif t == "SETABD": rp["a"], rp["b"], rp["d"] = int(f[1]) & 255, int(f[2]) & 255, int(f[3]) & 255
    elif t == "GET":
        want = [rp["a"], rp["b"], rp["d"], rp["o"], rp["m"], rp["fl"]]
        if list(map(int, f[1:])) != want: err("%s: want %s" % (l, want))
    elif t in ("SRPA", "GRPA"):
        items = f[1:]
        more = None
        if "M" in items:
            k = items.index("M"); more = int(items[k + 1]); items = items[:k]
        tags = [tuple(map(int, x.split(":"))) for x in items]
        vals = {0x80000002: "a", 0x80000003: "b", 0x80000004: "d", 0x80000005: "o", 0x80000006: "m"}
        got = None
        if t == "GRPA": got = nxt().split()[1:]
        i = 0; k = 0
        while i < len(tags):
            tag, data = tags[i]
            if tag == 1: pass
            elif tag == 3:
                for j in range(1, data + 1):
                    if got and got[i + j] not in ("57005", "-"): err("GetRPAttrs wrote a skipped tag: " + l)
                i += 1 + data; continue
            elif t == "SRPA":
                if tag in vals: rp[vals[tag]] = data & 255
                if tag == 0x80000005: rp["fl"] |= 8
            else:
                want = rp[vals[tag]] if tag in vals else 7    # MaxPen: the depth's highest
                if int(got[i]) != want: err("GetRPAttrs tag %x got %s want %d" % (tag, got[i], want))
            i += 1
        if more is not None:
            if t == "SRPA": rp["a"] = more & 255
            elif int(got[len(tags)]) != rp["a"]: err("GetRPAttrs through TAG_MORE: " + str(got))
    elif t == "TMPRAS":
        if f[1:] != ["1", "1"]: err(l)
    elif t == "AREA":
        if f[1:] != ["1", "1", "80", "1", "0", "20", "0"]: err(l)
    elif t == "CM":
        n = int(f[1])
        if f[2:] != [str(n), "2", "1"]: err(l)
        cm = [(0, 0, 0)] * n; pens = None
    elif t in ("S4", "V4", "S32", "V32"):
        i, r, g, b = map(int, f[1:])
        if t in ("S4", "V4"): r, g, b = r * 17, g * 17, b * 17
        vpc = take_vpc()
        if i < len(cm):
            cm[i] = (r, g, b)
            if t[0] == "V" and vpc != [(i, 1)]: err("%s: hook %s" % (l, vpc))
        elif vpc: err("%s: hook for an entry past the map" % l)
    elif t == "G4":
        i, v = int(f[1]), int(f[2])
        want = -1 if i >= len(cm) else (cm[i][0] >> 4) << 8 | (cm[i][1] >> 4) << 4 | (cm[i][2] >> 4)
        if v != want: err("%s: want %d" % (l, want))
    elif t == "G32":
        i, m = int(f[1]), int(f[2]); v = list(map(int, f[3:]))
        for j in range(m):
            for c in range(3):
                want = cm[i + j][c] * 0x01010101 if i + j < len(cm) else 7
                if v[3 * j + c] != want: err("%s: entry %d gun %d want %d" % (l, j, c, want))
    elif t == "L4":
        m = int(f[1]); cols = list(map(int, f[2:]))
        n = min(m, len(cm))
        for j in range(n): cm[j] = ((cols[j] >> 8 & 15) * 17, (cols[j] >> 4 & 15) * 17, (cols[j] & 15) * 17)
        vpc = take_vpc()
        if vpc != ([(0, n)] if n else []): err("%s: hook %s" % (l, vpc))
    elif t == "L32":
        v = list(map(int, f[1:])); k = 0; want = []
        while k < len(v):
            cnt, first = v[k], v[k + 1]; k += 2
            for j in range(cnt):
                if first + j < len(cm): cm[first + j] = tuple(v[k + 3 * j: k + 3 * j + 3])
            k += 3 * cnt
            if first < len(cm): want.append((first, min(cnt, len(cm) - first)))
        vpc = take_vpc()
        if vpc != want: err("%s: hook %s want %s" % (l, vpc, want))
    elif t in ("APE", "APE2"):
        if f[1] != "0": err(l)
        if t == "APE": pens = [0] * len(cm); refs = [0] * len(cm)
    elif t == "OP":
        want_pen, r, g, b, fl, res = map(int, f[1:])
        vpc = take_vpc(); pen = -1; setc = False
        w = take_free() if want_pen == -1 else want_pen
        if 0 <= w < len(cm):
            if pens[w] == 0:
                pens[w] = 2 if fl & 1 else 1; refs[w] = 0 if fl & 1 else 1; pen = w
                if not fl & 2: cm[w] = (r, g, b); setc = True
            elif pens[w] == 1 and not fl & 1 and (fl & 2 or cm[w] == (r, g, b)):
                refs[w] += 1; pen = w
        if res != pen: err("%s: want %d" % (l, pen))
        if vpc != ([(pen, 1)] if setc else []): err("%s: hook %s" % (l, vpc))
    elif t == "RP":
        i = int(f[1])
        if i < len(cm):
            if pens[i] == 1:
                refs[i] -= 1
                if refs[i] == 0: pens[i] = 0
            elif pens[i] == 2: pens[i] = 0; refs[i] = 0
    elif t == "BP":
        r, g, b, pr, fb, res = map(int, f[1:])
        vpc = take_vpc()
        tol = 0 if pr < 0 else 8 + 2 * pr
        best, bd = -1, None
        for i in range(len(cm)):
            if pens[i] == 1:
                d = dist(i, r, g, b)
                if bd is None or d < bd: best, bd = i, d
        pen = -1; setc = False
        if best >= 0 and bd <= 3 * tol * tol: pen = best; refs[best] += 1
        elif take_free() >= 0:
            pen = take_free(); pens[pen] = 1; refs[pen] = 1; cm[pen] = (r, g, b); setc = True
        elif not fb and best >= 0: pen = best; refs[best] += 1
        if res != pen: err("%s: want %d" % (l, pen))
        if vpc != ([(pen, 1)] if setc else []): err("%s: hook %s" % (l, vpc))
    elif t == "FC":
        r, g, b, mx, res = map(int, f[1:])
        last = len(cm) - 1 if mx < 0 or mx >= len(cm) else mx
        best = min(range(last + 1), key=lambda i: (dist(i, r, g, b), i))
        if res != best: err("%s: want %d" % (l, best))
    elif t == "CMNULL":
        if f[1:] != ["0", "-1"]: err(l)
    elif t == "FLBEFORE":
        pos -= 1
        _, before = grid()
        fl = nxt().split()
        mode, x, y, fg, aol, dm, d, badtr, rc = map(int, fl[1:])
        _, after = grid()
        H, W = len(before), len(before[0])
        dmask = (1 << d) - 1
        want = [row[:] for row in before]
        wrc = 0 if badtr else 1
        if not badtr and 0 <= x < W and 0 <= y < H:
            s = before[y][x]
            match = (lambda v: v == s) if mode else (lambda v: v != (aol & dmask))
            if match(s):
                seen = {(x, y)}; todo = [(x, y)]
                while todo:
                    px, py = todo.pop()
                    for qx, qy in ((px + 1, py), (px - 1, py), (px, py + 1), (px, py - 1)):
                        if 0 <= qx < W and 0 <= qy < H and (qx, qy) not in seen and match(before[qy][qx]):
                            seen.add((qx, qy)); todo.append((qx, qy))
                for px, py in seen:
                    want[py][px] = before[py][px] ^ dmask if dm & 2 else fg & dmask
        if rc != wrc: err("Flood answered %d want %d: %s" % (rc, wrc, " ".join(fl)))
        if after != want:
            diff = [(xx, yy) for yy in range(H) for xx in range(W) if after[yy][xx] != want[yy][xx]]
            err("Flood %s: %d pixels differ, first %s" % (" ".join(fl), len(diff), diff[:3]))
    elif t == "SDIV0":
        if f[1] != "0": err(l)
    elif t == "SDIV":
        fa, nu, de, r = map(int, f[1:])
        if r != (fa * nu + de // 2) // de: err(l)
    elif t == "SCSRC":
        pos -= 1
        _, src = grid()
        a = list(map(int, nxt().split()[1:]))
        _, dst = grid()
        sx, sy, sw, sh, xs, ys, xd, yd, dx, dy, dw, dh = a
        wdw, wdh = (sw * xd + xs // 2) // xs, (sh * yd + ys // 2) // ys
        if (dw, dh) != (wdw, wdh): err("BitMapScale size %s want %d %d" % (a, wdw, wdh))
        want = [[0] * 64 for _ in range(40)]
        for j in range(wdh):
            yy = sy + min(j * ys // yd, sh - 1)
            for i in range(wdw):
                xx = sx + min(i * xs // xd, sw - 1)
                if yy < 20 and dy + j < 40 and dx + i < 64:
                    want[dy + j][dx + i] = src[yy][xx] if xx < 40 else 0
        if dst != want: err("BitMapScale pixels %s" % a)
    elif t == "ELREF":
        pos -= 1
        _, ref = grid(); _, before = grid()
        fg, bg, mask, dm = map(int, nxt().split()[1:])
        _, after = grid()
        m = mask & 7
        want = [row[:] for row in before]
        for y in range(50):
            for x in range(80):
                if not ref[y][x]: continue
                if dm & 2: want[y][x] ^= m
                elif dm & 4:
                    if dm & 1: want[y][x] = (want[y][x] & ~m) | (bg & m)
                else: want[y][x] = (want[y][x] & ~m) | (fg & m)
        if after != want:
            diff = [(x, y) for y in range(50) for x in range(80) if after[y][x] != want[y][x]]
            err("DrawEllipse mode %d: %d pixels differ, first %s" % (dm, len(diff), diff[:3]))
    elif t == "LYBEFORE":
        pos -= 1
        _, before = grid()
        hooks = []
        while lines[pos].startswith("BFHOOK"): hooks.append(list(map(int, nxt().split()[1:])))
        lf = list(map(int, nxt().split()[1:]))
        dmg = list(map(int, nxt().split()[1:]))
        _, after = grid()
        kind, ox, oy, lw, lh, dx, dy, x0, y0, x1, y1, bg, mask, bfk, refresh = lf
        m = mask & 7
        want = [row[:] for row in before]
        wdmg, wref, whooks = [], 0, []
        if kind == 0 and (dx or dy):
            a, b, c, d = max(x0, 0), max(y0, 0), min(x1, lw - 1), min(y1, lh - 1)
            if a <= c and b <= d:
                w, h = c - a + 1, d - b + 1
                if abs(dx) < w and abs(dy) < h:
                    sxx, syy, dxx, dyy = a + max(dx, 0), b + max(dy, 0), a + max(-dx, 0), b + max(-dy, 0)
                    snap = [row[:] for row in want]
                    for j in range(h - abs(dy)):
                        for i in range(w - abs(dx)):
                            o = snap[oy + dyy + j][ox + dxx + i]
                            want[oy + dyy + j][ox + dxx + i] = (o & ~m) | (snap[oy + syy + j][ox + sxx + i] & m)
                    fills = []
                    if dx > 0: fills.append((c - dx + 1, b, c, d))
                    if dx < 0: fills.append((a, b, a - dx - 1, d))
                    if dy > 0: fills.append((a, d - dy + 1, c, d))
                    if dy < 0: fills.append((a, b, c, b - dy - 1))
                else: fills = [(a, b, c, d)]
                for (p, q, r, s) in fills:
                    for yy in range(q, s + 1):
                        for xx in range(p, r + 1): want[oy + yy][ox + xx] = (want[oy + yy][ox + xx] & ~m) | (bg & m)
                if x0 < 0 or y0 < 0 or x1 > lw - 1 or y1 > lh - 1: wdmg, wref = [x0, y0, x1, y1], 1
        if kind == 1:
            a, b, c, d = max(x0 + ox, ox), max(y0 + oy, oy), min(x1 + ox, ox + lw - 1), min(y1 + oy, oy + lh - 1)
            if a <= c and b <= d:
                for yy in range(b, d + 1):
                    for xx in range(a, c + 1):
                        if bfk == 0: want[yy][xx] &= ~m
                        if bfk == 2: want[yy][xx] = 5
                if bfk == 2: whooks = [[a, b, c, d, a - ox, b - oy, 1]]
        if after != want:
            diff = [(x, y) for y in range(48) for x in range(96) if after[y][x] != want[y][x]]
            err("layer %s: %d pixels differ, first %s" % (lf, len(diff), diff[:3]))
        if dmg != wdmg or refresh != wref: err("layer %s: damage %s %d want %s %d" % (lf, dmg, refresh, wdmg, wref))
        if hooks != whooks: err("layer %s: hooks %s want %s" % (lf, hooks, whooks))
    elif t == "SUPER":
        minx, miny, maxx, maxy, sx, sy = map(int, f[1:])
        _, sb = grid()
        ops = []
        while lines[pos].startswith(("SWP", "SRF")): ops.append(nxt().split())
        srd = list(map(int, nxt().split()[1:]))
        _, sb_after = grid(); _, scr = grid()
        ssync = int(nxt().split()[1])
        for o in ops:
            if o[0] == "SWP":
                a, b, pen, rc = map(int, o[1:])
                inside = 0 <= a < 64 and 0 <= b < 48
                if rc != (0 if inside else -1): err("super WritePixel %s" % o)
                if inside: sb[b][a] = pen & 3
            else:
                a, b, c, d, pen = map(int, o[1:])
                for yy in range(max(b, 0), min(d, 47) + 1):
                    for xx in range(max(a, 0), min(c, 63) + 1): sb[yy][xx] = pen & 3
        if sb_after != sb: err("super bitmap after drawing (%d %d)" % (sx, sy))
        for yy in range(40):
            for xx in range(80):
                inw = minx <= xx <= maxx and miny <= yy <= maxy
                want = sb[sy + yy - miny][sx + xx - minx] if inw else 0
                if scr[yy][xx] != want:
                    err("super screen (%d,%d) %d want %d" % (xx, yy, scr[yy][xx], want)); break
        if srd != [sb[sy][sx], -1]: err("super ReadPixel %s" % srd)
        if ssync != 3: err("SyncSBitMap %d" % ssync)
    elif t == "RGNEW": RA, RB = set(), set()
    elif t == "RG":
        op, a0, b0, a1, b1, rc = map(int, f[1:7])
        rest = f[7:]
        k = rest.index("|")
        def cells(v):
            s = set(); rs = []
            for i in range(0, len(v), 4):
                x0, y0, x1, y1 = map(int, v[i:i + 4])
                rs.append((x0, y0, x1, y1))
                for y in range(y0, y1 + 1):
                    for x in range(x0, x1 + 1):
                        if (x, y) in s: err("region rectangles overlap at %d,%d" % (x, y))
                        s.add((x, y))
            return s
        ga, gb = cells(rest[:k]), cells(rest[k + 1:])
        rect = {(x, y) for y in range(b0, b1 + 1) for x in range(a0, a1 + 1)}
        if op == 0: RA = RA & rect
        elif op == 1: RA = RA | rect
        elif op == 2: RA = RA ^ rect
        elif op == 3: RA = RA - rect
        elif op == 4: RB = RB | rect
        elif op == 5: RA = RA | RB
        elif op == 6: RA = RA & RB
        else: RA = RA ^ RB
        if rc != 1 or ga != RA or gb != RB: err("region op %d: %s" % (op, l[:80]))
    else:
        err("unknown line " + l[:40])
if pending_vpc: err("hook calls left over")
print("%d lines, %d model errors" % (checks, bad))
sys.exit(1 if bad else 0)
PY

for cpu in -m68000 -m68020 -m68040; do
  for opt in -O0 -O2; do
    case $cpu in
      -m68000) rt="-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -nostdlib -ffreestanding -fno-builtin $here/rt68000.c" ;;
      *) rt= ;;
    esac
    # shellcheck disable=SC2086
    "$cxx" $cpu $opt $cxxflags -I"$g" -I"$core" -c -o "$tmp/face.o" "$g/gfx_face.cpp"
    # the face itself: no data, no bss (read-only constants are code), no C++ runtime; it calls the core,
    # libgcc and the hooks only
    data=$("$nm" "$tmp/face.o" | awk '$2 ~ /^[dDbBcCgGsS]$/' || true)
    [ -z "$data" ] || { echo "FAIL $opt $cpu: data in the face: $data"; fail=1; }
    und=$("$nm" -u "$tmp/face.o" | awk '{print $2}' | grep -Ev '^(ogfx_rp_|gfx_planar_|gfx_rgn_|gfx_hook_|__(mul|div|udiv|mod|umod)si3$)' || true)
    [ -n "$und" ] && { echo "FAIL $opt $cpu: the face calls $und"; fail=1; }
    cxxrt=$("$nm" "$tmp/face.o" | grep -E '__cxa_|__gxx_personality|_ZTI|_ZTS|_GLOBAL__sub_I_|_ZGV|_Znw|_Zdl' || true)
    [ -z "$cxxrt" ] || { echo "FAIL $opt $cpu: C++ runtime: $cxxrt"; fail=1; }
    # shellcheck disable=SC2086
    "$cc" $cpu $opt -static $warn -DROUNDS=$rounds $inc -o "$tmp/t" "$here/test_gfx_face.c" \
      "$core/gfx_core.c" "$core/gfx_region.c" "$core/gfx_rp.c" "$tmp/face.o" $rt
    q=m${cpu#-m}
    if timeout 120 "$qemu" -cpu $q "$tmp/t" | head -c 8000000 > "$tmp/out"; then
      if res=$(python3 "$tmp/model.py" "$tmp/out"); then echo "ok   $opt $cpu: $res"
      else echo "FAIL $opt $cpu:"; echo "$res"; fail=1; fi
    else echo "FAIL $opt $cpu: the test stopped"; grep ERR "$tmp/out" | head; tail -3 "$tmp/out"; fail=1; fi
  done
done

# the face's register entries compile as C (GFX_REG as plain parameters here)
"$cc" -std=gnu11 -O2 $warn -I"$g" -c -o "$tmp/lvo.o" "$g/face_lvo.c" || fail=1
"$nm" "$tmp/lvo.o" > "$tmp/defined"
"$nm" "$tmp/face.o" >> "$tmp/defined"
python3 "$g/gen_stubs.py" "$g/graphics.lvo" "$tmp/defined" "$tmp" | cut -c1-100
"$cc" -O2 $warn -I"$g" -c -o "$tmp/stubs.o" "$tmp/gfx_stubs.c" || fail=1
grep -c 'gfx_lvo_' "$tmp/gfx_vectors.h" | grep -qx 344 || { echo "FAIL gfx_vectors.h is not 172 slots"; fail=1; }
[ $fail = 0 ] && echo "test_gfx_face: all passed"
exit $fail
