#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# OpenGfx text's reference model (tests/m68k/test_gfx_text.sh): makes random
# fonts, strings and RastPorts, works out what graphics.library's text calls
# answer and draw, a pixel at a time from the rules (library/text/README.md),
# and writes them as a C header for test_gfx_text.c to check the core
# against. Independent of library/text: glyphs are kept here as rows of
# pixels, not as the packed bits the C reads. Copyright (c) 2026 Dalsin
# Limited. MIT.
#   python3 test_gfx_text_model.py SEED > cases.h
import os
import random
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'library', 'text'))
import mkfont  # noqa: E402  (the topaz glyph art, as rows)

W, H = 320, 48
UL, BOLD, ITAL, EXT = 1, 2, 4, 8
REVPATH, PROP, TALL, WIDE = 4, 0x20, 8, 0x10
JAM2, COMP, INV = 1, 2, 4


class Font:
    pass


def rnd_font(r, i):
    f = Font()
    f.i = i
    f.lo = r.randint(30, 70)
    f.hi = f.lo + r.randint(0, 40)
    f.ys = r.randint(1, 12)
    f.base = r.randint(0, f.ys - 1)
    f.xs = r.randint(1, 12)
    f.smear = r.randint(0, 2)
    f.style = r.choice([0, 0, 0, BOLD, ITAL, UL | ITAL])
    f.flags = r.choice([0, 0, 0, REVPATH]) | r.choice([0, PROP])
    f.color = r.random() < 0.3
    f.depth = r.randint(1, 3) if f.color else 1
    n = f.hi - f.lo + 2
    f.w = [r.choice([0] + list(range(1, 12)) * 3) for _ in range(n)]
    top = r.choice([3, 12])     # 3: images overhang their advance
    f.space = [r.randint(-2, top) for _ in range(n)] if r.random() < 0.6 else None
    f.kern = [r.randint(-3, 3) for _ in range(n)] if r.random() < 0.5 else None
    # glyph images: planes x rows x width, each a 0/1
    f.img = [[[[r.randint(0, 1) if r.random() < 0.7 else 0 for _ in range(f.w[g])] for _ in range(f.ys)]
              for g in range(n)] for _ in range(f.depth)]
    off, f.off = 0, []
    for g in range(n):
        off += r.choice([0, 0, 1, 5])
        f.off.append(off)
        off += f.w[g]
    f.mod = (off + 15) // 16 * 2 + r.choice([0, 2])
    if f.color:
        f.pick = r.choice([1, 3, 5, 6, 7, 0x0C, 0x13, r.randint(1, 255)])
        f.onoff = r.randint(0, 255)
        f.fgcolor = r.randint(0, 15)
        f.ctflags = r.choice([0, 1])
    return f


def topaz(size):
    xs, ys, base, g = mkfont.font(size)
    f = Font()
    f.lo, f.hi, f.ys, f.base, f.xs, f.smear, f.style, f.flags = 32, 255, ys, base, xs, 1, 0, 0x41
    f.color, f.depth, f.space, f.kern = False, 1, None, None
    order = list(range(32, 256)) + ['default']
    f.w = [xs] * len(order)
    f.img = [[[[1 if ch == '#' else 0 for ch in row] for row in (g[k] or g['default'])] for k in order]]
    return f


def glyph(f, c):
    return f.hi - f.lo + 1 if c < f.lo or c > f.hi else c - f.lo


def kern(f, g):
    return f.kern[g] if f.kern else 0


def adv(f, g, spacing):
    return (f.space[g] if f.space else f.xs) + kern(f, g) + spacing


def drawn(f, s):
    return list(reversed(s)) if f.flags & REVPATH else list(s)


def measure(f, chars, algo, spacing):
    """(width, x0, x1) of chars laid left to right from 0."""
    smear = f.smear if algo & BOLD else 0
    if not chars:
        return 0, 0, -1
    pos, lo, hi = 0, 0, None
    for c in chars:
        g = glyph(f, c)
        if f.w[g]:
            left = pos + kern(f, g)
            lo = min(lo, left)
            hi = left + f.w[g] - 1 if hi is None else max(hi, left + f.w[g] - 1)
        pos += adv(f, g, spacing)
    if hi is None or pos - 1 > hi:
        hi = pos - 1
    hi += smear
    if algo & ITAL:
        hi += f.base // 2
        lo -= (f.ys - f.base + 1) // 2
    return pos, lo, hi


def extent(f, s, algo, spacing):
    w, x0, x1 = measure(f, drawn(f, s), algo, spacing)
    if not s:
        return (0, 0, 0, 0, -1, -1)
    if f.flags & REVPATH:
        x0 -= w
        x1 -= w
    return (w, f.ys, x0, -f.base, x1, f.ys - 1 - f.base)


def sext(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def pen_of(f, g, r, x):
    """The glyph pixel's pen (0x100 for FgPen), or None."""
    if not f.color:
        return 0 if f.img[0][g][r][x] else None
    v, p, ink = 0, 0, False
    for q in range(8):
        if f.pick >> q & 1:
            if p < f.depth and f.img[p][g][r][x]:
                v |= 1 << q
                ink = True
            p += 1
        else:
            v |= f.onoff & (1 << q)
    if not ink:
        return None
    if f.ctflags & 1 and v == f.fgcolor:
        return 0x100
    return v


def draw(f, s, rp, grid):
    algo = rp['algo'] & 7
    smear = f.smear if algo & BOLD else 0
    chars = drawn(f, s)
    w, x0, x1 = measure(f, chars, algo, rp['spacing'])
    ox = rp['cpx'] - w if f.flags & REVPATH else rp['cpx']
    top = rp['cpy'] - f.base
    mode, fg, bg, mask = rp['mode'], rp['fg'], rp['bg'], rp['mask']

    def put(x, y, pen, op):
        if 0 <= x < W and 0 <= y < H:
            if op:
                grid[y][x] ^= pen & mask
            else:
                grid[y][x] = (grid[y][x] & ~mask | pen & mask) & 0xFF

    for r in range(f.ys):
        d = f.base - r
        sl = (d // 2 if d >= 0 else -((1 - d) // 2)) if algo & ITAL else 0
        ink = {}
        pos = 0
        for c in chars:
            g = glyph(f, c)
            left = pos + kern(f, g) + sl
            pos += adv(f, g, rp['spacing'])
            for k in ([0, smear] if smear else [0]):
                for j in range(f.w[g]):
                    v = pen_of(f, g, r, j)
                    if v is not None:
                        ink[left + k + j] = v
        for x in range(x0, x1 + 1):
            v = ink.get(x)
            u = algo & UL and r == f.base + 1 and v is None and x - 1 not in ink and x + 1 not in ink
            px, py = ox + x, top + r
            if not f.color:
                b = v is not None or u
                if mode & INV:
                    b = not b
                if mode & COMP:
                    if b or mode & JAM2:
                        put(px, py, 0xFF, 1)
                elif b:
                    put(px, py, fg, 0)
                elif mode & JAM2:
                    put(px, py, bg, 0)
                continue
            if u:
                v = 0x100
            if mode & COMP:
                if v is not None:
                    put(px, py, 0xFF if v == 0x100 else v, 1)
                elif mode & JAM2:
                    put(px, py, 0xFF, 1)
            elif v is not None:
                put(px, py, fg if v == 0x100 else v, 0)
            elif mode & JAM2:
                put(px, py, bg, 0)
    return sext(rp['cpx'] - w if f.flags & REVPATH else rp['cpx'] + w)


def fnv(grid):
    h = 0x811C9DC5
    for row in grid:
        for b in row:
            h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def textfit(f, s, idx, n, d, algo, spacing, con, bw, bh):
    """(count, extent) as TextFit answers, s[idx + i * d] the i-th character."""
    miny, maxy = -f.base, f.ys - 1 - f.base
    if f.ys > bh or (con and (miny < con[1] or maxy > con[3])):
        n = 0
    k = 0
    for i in range(1, n + 1):
        chars = [s[idx + j * d] for j in range(i)]
        mem = chars if d >= 0 else list(reversed(chars))  # in memory order
        e = extent(f, mem, algo, spacing)
        x0, x1 = e[2], e[4]
        if e[0] < 0:            # TxSpacing turns the path back: the fit fails
            k = 0
            break
        if x1 - x0 + 1 > bw:
            break
        if con and (x0 < con[0] or x1 > con[2]):
            break
        k = i
    chars = [s[idx + j * d] for j in range(k)]
    mem = chars if d >= 0 else list(reversed(chars))
    return k, extent(f, mem, algo, spacing)


def fontextent(f):
    lo, hi, wide = 0, -1, 0
    for g in range(f.hi - f.lo + 2):
        k, w = kern(f, g), f.w[g]
        a = (f.space[g] if f.space else f.xs) + k
        if w:
            lo, hi = min(lo, k), max(hi, k + w - 1)
        hi = max(hi, a - 1)
        wide = max(wide, a)
    return (wide, f.ys, lo, -f.base, hi, f.ys - 1 - f.base)


def weigh(req, tgt, rdpi, tdpi):
    dy = tgt[0] - req[0]
    pen = min(abs(dy), 100) * 256 + (128 if dy > 0 else 0)
    ds = (req[1] ^ tgt[1]) & 0x0F
    pen += (64 if ds & EXT else 0) + (32 if ds & ITAL else 0) + (16 if ds & BOLD else 0) + (8 if ds & UL else 0)
    df = req[2] ^ tgt[2]
    pen += (4 if df & REVPATH else 0) + (2 if df & PROP else 0)
    if df & (TALL | WIDE) or (rdpi is not None and tdpi is not None and rdpi != tdpi):
        pen += 1
    return 32767 - pen


def c_font(out, f, name):
    w = out.write
    n = f.hi - f.lo + 2
    for p in range(f.depth):
        data = bytearray(f.mod * f.ys)
        for g in range(n):
            for r in range(f.ys):
                for x in range(f.w[g]):
                    if f.img[p][g][r][x]:
                        b = f.off[g] + x
                        data[r * f.mod + b // 8] |= 0x80 >> (b % 8)
        w('static const UBYTE %s_d%d[] = {%s};\n' % (name, p, ','.join(str(b) for b in data) or '0'))
    w('static const UWORD %s_loc[] = {%s};\n' % (name, ','.join('%d,%d' % (f.off[g], f.w[g]) for g in range(n))))
    if f.space:
        w('static const WORD %s_sp[] = {%s};\n' % (name, ','.join(map(str, f.space))))
    if f.kern:
        w('static const WORD %s_kn[] = {%s};\n' % (name, ','.join(map(str, f.kern))))
    planes = ','.join('%s_d%d' % (name, p) for p in range(f.depth))
    w('static const struct tfont %s = {%d,%d,%d,%d,%d,%d,%d,%d,%d,%d, %s_loc, %s, %s, %d, {%s}, %d,%d,%d,%d};\n'
      % (name, f.lo, f.hi, f.ys, f.base, f.xs, f.smear, f.style | (0x40 if f.color else 0), f.flags, f.mod,
         f.depth, name, '%s_sp' % name if f.space else '0', '%s_kn' % name if f.kern else '0', int(f.color),
         planes, getattr(f, 'pick', 0), getattr(f, 'onoff', 0), getattr(f, 'fgcolor', 0), getattr(f, 'ctflags', 0)))


def main():
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    r = random.Random(seed)
    out = sys.stdout
    w = out.write
    w('/* made by test_gfx_text_model.py %d */\n' % seed)
    fonts = [rnd_font(r, i) for i in range(24)]
    for i, f in enumerate(fonts):
        c_font(out, f, 'f%d' % i)
    w('static const struct tfont *const fonts[] = {%s};\n' % ','.join('&f%d' % i for i in range(len(fonts))))
    w('#define NFONTS %d\n' % len(fonts))
    t8, t9 = topaz(8), topaz(9)

    # Text, TextLength, TextExtent: font -1 and -2 are topaz 8 and 9
    cases = []
    for k in range(400):
        fi = r.randrange(-2, len(fonts))
        f = t8 if fi == -1 else t9 if fi == -2 else fonts[fi]
        if fi < 0 and r.random() < 0.5:
            s = [r.randint(0, 255) for _ in range(r.randint(0, 20))]
        elif fi < 0:
            s = [ord(c) for c in r.choice(['Hello, World', 'topaz', 'AmigaOS 3.2', 'ÄÖÜ äöü ß', '0123456789'])]
        else:
            s = [r.choice([r.randint(0, 255), r.randint(f.lo - 2, f.hi + 2) & 0xFF]) for _ in range(r.randint(0, 12))]
        rp = dict(algo=r.choice([0, 0, UL, BOLD, ITAL, UL | BOLD | ITAL, r.randint(0, 255)]),
                  spacing=r.choice([0, 0, 0, r.randint(-3, 4)]),
                  mode=r.choice([0, 1, 2, 3, 4, 5, 6, 7, 0, 1]), fg=r.randint(0, 255), bg=r.randint(0, 255),
                  mask=r.choice([0xFF, 0xFF, r.randint(0, 255)]), cpx=r.randint(-20, 300), cpy=r.randint(-4, 52))
        grid = [[(x * 7 + y * 13) & 0xFF for x in range(W)] for y in range(H)]
        e = extent(f, s, rp['algo'] & 7, rp['spacing'])
        cp = draw(f, s, rp, grid)
        cases.append((fi, s, rp, sext(e[0]), e, fnv(grid), cp))
    w('static const struct tcase cases[] = {\n')
    for fi, s, rp, ln, e, h, cp in cases:
        w('  {%d, %d, (const UBYTE *)"%s", %d,%d,%d,%d,%d,%d,%d,%d, %d, {%s}, %luUL, %d},\n'
          % (fi, len(s), ''.join('\\%03o' % c for c in s), rp['algo'], rp['spacing'], rp['mode'], rp['fg'],
             rp['bg'], rp['mask'], rp['cpx'], rp['cpy'], ln, ','.join(map(str, e)), h, cp))
    w('};\n#define NCASES %d\n' % len(cases))

    # TextFit
    w('static const struct tfit fits[] = {\n')
    nfit = 0
    for k in range(400):
        fi = r.randrange(-1, len(fonts))
        f = t8 if fi == -1 else fonts[fi]
        s = [r.randint(0, 255) for _ in range(r.randint(0, 14))]
        algo, spacing = r.choice([0, 0, BOLD, ITAL, UL | BOLD | ITAL]), r.choice([0, 0, r.randint(-3, 4)])
        d = r.choice([1, 1, -1])
        n = len(s)
        idx = 0 if d > 0 else max(n - 1, 0)
        con = None
        if r.random() < 0.5:
            con = (r.randint(-10, 0), r.randint(-14, 0), r.randint(-1, 80), r.randint(-1, 10))
        bw, bh = r.choice([0xFFFF, r.randint(0, 100)]), r.choice([0xFFFF, r.randint(0, 14)])
        cnt, e = textfit(f, s, idx, n, d, algo, spacing, con, bw, bh) if n else (0, extent(f, [], algo, spacing))
        w('  {%d, %d, (const UBYTE *)"%s", %d, %d,%d, %d, {%s}, %d, %dUL,%dUL, %d, {%s}},\n'
          % (fi, n, ''.join('\\%03o' % c for c in s), idx, algo, spacing, d,
             ','.join(map(str, con)) if con else '0,0,0,0', int(con is not None), bw, bh, cnt,
             ','.join(map(str, e))))
        nfit += 1
    # TxSpacing that turns topaz 8's path back (-9 on 8 wide): no fit
    for s, d, con in (([97, 98, 99], 1, None), ([97, 98, 99], -1, (-100, -14, 100, 10))):
        n, idx = len(s), 0 if d > 0 else len(s) - 1
        cnt, e = textfit(t8, s, idx, n, d, 0, -9, con, 0xFFFF, 0xFFFF)
        w('  {%d, %d, (const UBYTE *)"%s", %d, %d,%d, %d, {%s}, %d, %dUL,%dUL, %d, {%s}},\n'
          % (-1, n, ''.join('\\%03o' % c for c in s), idx, 0, -9, d,
             ','.join(map(str, con)) if con else '0,0,0,0', int(con is not None), 0xFFFF, 0xFFFF, cnt,
             ','.join(map(str, e))))
        nfit += 1
    w('};\n#define NFITS %d\n' % nfit)

    # FontExtent
    w('static const WORD fextents[][6] = {%s};\n'
      % ','.join('{%s}' % ','.join(map(str, fontextent(f))) for f in fonts + [t8, t9]))

    # WeighTAMatch: (ysize, style, flags, dpi or -1) twice and the answer
    w('static const LONG weighs[][9] = {\n')
    for k in range(200):
        req = (r.randint(1, 30), r.randint(0, 15), r.randint(0, 255))
        tgt = (r.choice([req[0], r.randint(1, 300)]), r.choice([req[1], r.randint(0, 15)]),
               r.choice([req[2], r.randint(0, 255)]))
        rd = r.choice([None, 0x00480048, 0x00480024])
        td = r.choice([None, 0x00480048, 0x00480024])
        w('  {%d,%d,%d,%d, %d,%d,%d,%d, %d},\n' % (req + (-1 if rd is None else rd,) + tgt
                                                  + (-1 if td is None else td, weigh(req, tgt, rd, td))))
    w('};\n#define NWEIGHS 200\n')

    # OpenFont: fonts (name 0..2, size, style, flags) added in order, then
    # requests (name, size, style, flags) and which font answers (-1 none).
    names = ['topaz.font', 'Garnet.font', 'garnet.FONT', 'emerald.font']
    w('static const char *const names[] = {%s};\n' % ','.join('"%s"' % n for n in names))
    lst = [(r.randint(0, 2), r.randint(6, 12), r.choice([0, 0, BOLD, ITAL]), r.choice([0, PROP, REVPATH]))
           for _ in range(10)]
    w('static const UBYTE ofonts[][4] = {%s};\n' % ','.join('{%d,%d,%d,%d}' % t for t in lst))
    w('static const WORD oreqs[][5] = {\n')
    order = list(reversed(range(len(lst))))   # AddFont adds at the head
    for k in range(100):
        q = (r.randint(0, 3), r.randint(5, 13), r.choice([0, BOLD, ITAL, UL]), r.choice([0, PROP, REVPATH]))
        best, bw = -1, -1
        for i in order:
            if names[lst[i][0]].lower() != names[q[0]].lower():
                continue
            wt = weigh(q[1:], lst[i][1:], None, None)
            if wt > bw:
                best, bw = i, wt
        w('  {%d,%d,%d,%d, %d},\n' % (q + (best,)))
    w('};\n#define NOREQS 100\n')

    # The topaz glyphs as drawn, for the data check: [size][glyph][row] bits
    for size, f in ((8, t8), (9, t9)):
        rows = []
        for g in range(len(f.w)):
            for row in f.img[0][g]:
                rows.append(sum(b << (15 - i) for i, b in enumerate(row)))
        w('static const UWORD topaz%d_art[%d] = {%s};\n' % (size, len(rows), ','.join(map(str, rows))))


if __name__ == '__main__':
    main()
