#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# The script tests/m68k/test_gfx_gels.c runs, and what it should print,
# from a model of graphics.library's GELs and blits written a pixel at a
# time (no blitter, no words): each call's documented effect on each
# pixel, the gel list as a python list, the erase chains as lists, the
# blitter queue as an event list.
#   python3 test_gfx_gels.py OUTDIR [SEED] [ROUNDS]
# writes OUTDIR/gels_script.h and OUTDIR/expected.txt.
# Copyright (c) 2026 Dalsin Limited. MIT.
import os
import random
import sys

M32 = 0xFFFFFFFF


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


VSPRITE, SAVEBACK, OVERLAY = 1, 2, 4
BACKSAVED, GELGONE, VSOVERFLOW = 0x100, 0x400, 0x800
BWAITING, BDRAWN, BOBSAWAY, BOBNIX, OUTSTEP = 0x100, 0x200, 0x400, 0x800, 0x2000
RINGTRIGGER = 1
JAM2, COMPLEMENT, INVERSVID = 1, 2, 4


class Bitmap:
    def __init__(self, bpr, rows, flags, depth, planes):
        self.bpr, self.rows, self.flags, self.depth, self.planes = bpr, rows, flags, depth, planes

    def width(self):
        # interleaved: the planes' share of a row, where the first two real
        # planes are that far apart for each plane between them, or where
        # there are fewer than two real planes
        bpr = self.bpr
        if not (self.flags & 4) or self.depth < 2:
            return bpr * 8
        part = bpr // self.depth
        real = [(k, p) for k, p in enumerate(self.planes[:self.ndepth()]) if isinstance(p, tuple)]
        if len(real) >= 2:
            (k0, p0), (k1, p1) = real[0], real[1]
            if p0[0] != p1[0] or p1[1] != p0[1] + (k1 - k0) * part:
                return bpr * 8
        return part * 8

    def ndepth(self):
        return min(self.depth, 8)


class Model:
    def __init__(self, seed):
        self.rng = random.Random(seed)
        self.script = []
        self.out = []
        self.reset_round()

    # ---- the script ----
    def emit(self, *ints):
        for v in ints:
            v = int(v)
            assert -0x80000000 <= v <= 0xFFFFFFFF, v
            self.script.append(v if v < 0x80000000 else v - 0x100000000)

    def reset_round(self):
        self.bufs = {}
        self.chip = {}
        self.bms = {}
        self.layers = {}
        self.rps = {}
        self.nextbuf = 0
        self.saved = {}       # a save buffer's id -> {(plane, rx, ry): bit}

    def RESET(self, blitter=True):
        self.emit(9)
        # some rounds on a machine with no blitter (RTG only): the same pixels
        self.emit(70, 1 if blitter or self.rng.random() < 0.75 else 0)
        self.reset_round()

    def BUF(self, size, chip, seed, guard=False):
        i = self.nextbuf
        self.nextbuf += 1
        if guard:
            # Fast RAM ending at a page no one may touch: reading past it faults
            chip = 0
            self.emit(71, i, size, seed)
        else:
            self.emit(1, i, size, chip, seed)
        b = bytearray(size)
        x = seed
        if seed:
            for k in range(size):
                x = (x * 1103515245 + 12345) & M32
                b[k] = (x >> 16) & 0xFF
        self.bufs[i] = b
        self.chip[i] = chip
        return i

    def HASH(self, ids):
        self.emit(8, len(ids), *ids)
        h = 2166136261
        for i in ids:
            for byte in self.bufs[i]:
                h = ((h ^ byte) * 16777619) & M32
        self.out.append("hash %d" % h)

    def BITMAP(self, i, bpr, rows, flags, depth, planes):
        self.emit(2, i, bpr, rows, flags, depth)
        for k in range(8):
            p = planes[k] if k < len(planes) else None
            if p is None:
                self.emit(-1, 0)
            elif p == 'ones':
                self.emit(-2, 0)
            else:
                self.emit(p[0], p[1])
        self.bms[i] = Bitmap(bpr, rows, flags, depth, (list(planes) + [None] * 8)[:8])

    # ---- pixels ----
    def pget(self, bm, p, x, y):
        sp = bm.planes[p]
        if sp is None:
            return 0
        if sp == 'ones':
            return 1
        return self.bufs[sp[0]][sp[1] + y * bm.bpr + (x >> 3)] >> (7 - (x & 7)) & 1

    def pset(self, bm, p, x, y, v):
        sp = bm.planes[p]
        if not isinstance(sp, tuple):
            return
        a = sp[1] + y * bm.bpr + (x >> 3)
        m = 0x80 >> (x & 7)
        b = self.bufs[sp[0]]
        b[a] = (b[a] | m) if v else (b[a] & ~m & 0xFF)

    def bit(self, bufid, base, x):
        return self.bufs[bufid][base + (x >> 3)] >> (7 - (x & 7)) & 1

    def places(self, rp):
        bm, layer = rp['bm'], rp['layer']
        if layer is None:
            return [(self.bms[bm], 0, 0, 0, 0, self.bms[bm].width() - 1, self.bms[bm].rows - 1)]
        l = self.layers[layer]
        ox, oy = l['minx'] - l['sx'], l['miny'] - l['sy']
        res = []
        for (x0, y0, x1, y1, kind, cbm) in l['crs']:
            if kind == 0:
                b, bx, by = self.bms[bm], 0, 0
            elif kind == 1:
                b, bx, by = self.bms[cbm], x0 & ~15, y0
            else:
                continue
            px0, py0 = max(x0 - bx, 0), max(y0 - by, 0)
            px1, py1 = min(x1 - bx, b.width() - 1), min(y1 - by, b.rows - 1)
            if px0 <= px1 and py0 <= py1:
                res.append((b, ox - bx, oy - by, px0, py0, px1, py1))
        return res

    def pieces(self, rp, x, y, w, h):
        """every pixel of the rectangle that lands somewhere: (bitmap, bx, by, rx, ry)"""
        if w <= 0 or h <= 0:
            return
        for (b, ox, oy, px0, py0, px1, py1) in self.places(rp):
            for by in range(max(y + oy, py0), min(y + h - 1 + oy, py1) + 1):
                for bx in range(max(x + ox, px0), min(x + w - 1 + ox, px1) + 1):
                    yield b, bx, by, bx - (x + ox), by - (y + oy)

    # ---- the blits ----
    def TEMPLATE(self, tb, srcx, srcmod, rp, dx, dy, w, h):
        self.emit(5, tb, srcx, srcmod, rp, dx, dy, w, h)
        r = self.rps[rp]
        mode, fg, bg, mask = r['mode'], r['fg'] & 0xFF, r['bg'] & 0xFF, r['mask']
        for (b, bx, by, rx, ry) in list(self.pieces(r, dx, dy, w, h)):
            t = self.bit(tb, ry * srcmod, srcx + rx)
            one = (not t) if mode & INVERSVID else t
            for p in range(b.ndepth()):
                if not (mask >> p & 1) or not isinstance(b.planes[p], tuple):
                    continue
                c = self.pget(b, p, bx, by)
                if mode & COMPLEMENT:
                    v = (not c) if (one or (mode & JAM2)) else c
                elif one:
                    v = fg >> p & 1
                else:
                    v = (bg >> p & 1) if mode & JAM2 else c
                self.pset(b, p, bx, by, v)

    def MASKBLT(self, sbm, sx, sy, rp, dx, dy, w, h, minterm, mb):
        self.emit(6, sbm, sx, sy, rp, dx, dy, w, h, minterm, mb)
        s = self.bms[sbm]
        sw, sh = s.width(), s.rows
        if sx < 0:
            dx -= sx; w += sx; sx = 0
        if sy < 0:
            dy -= sy; h += sy; sy = 0
        if sx + w > sw:
            w = sw - sx
        if sy + h > sh:
            h = sh - sy
        if w <= 0 or h <= 0:
            return
        r = self.rps[rp]
        mbpr = sw // 8
        pcs = list(self.pieces(r, dx, dy, w, h))
        # the source as it was before any of it is written (it may be the destination)
        old = {(p, rx, ry): self.pget(s, p, sx + rx, sy + ry)
               for (b, bx, by, rx, ry) in pcs for p in range(s.ndepth())}
        for (b, bx, by, rx, ry) in pcs:
            a = self.bit(mb, (sy + ry) * mbpr, sx + rx)
            for p in range(min(b.ndepth(), s.ndepth())):
                if not (r['mask'] >> p & 1) or not isinstance(b.planes[p], tuple):
                    continue
                sb = old[(p, rx, ry)]
                c = self.pget(b, p, bx, by)
                x = 1 - (c ^ sb)
                f = minterm >> (4 + 2 * a + x) & 1
                self.pset(b, p, bx, by, 1 - (f ^ sb))

    def CLEAR(self, b, count, flags):
        self.emit(7, b, count, flags)
        words = (count >> 16) * ((count & 0xFFFF) >> 1) if flags & 2 else count >> 1
        v = 0xFF if flags & 4 else 0
        for k in range(words * 2):
            self.bufs[b][k] = v

    # ---- gels ----
    def gels_init(self, rsrvd, left, right, top, bottom):
        self.emit(20, rsrvd, left, right, top, bottom)
        self.gi = dict(rsrvd=rsrvd, left=left, right=right, top=top, bottom=bottom)
        self.glist = []
        self.c0 = []
        self.c1 = []
        self.vs = {}
        self.nextline = [0] * 8
        self.lastcolor = [None] * 8
        self.slots = []

    def BOB(self, i, w, h, depth, pick, onoff, flags, img, sb, db, bb, memask, hitmask, mk, ln):
        self.emit(21, i, w, h, depth, pick, onoff, flags, img, sb, db, bb, memask, hitmask, mk, ln)
        v = dict(id=i, w=w, h=h, depth=depth, pick=pick & 0xFF, onoff=onoff & 0xFF, flags=flags, img=img,
                 x=0, y=0, oldx=0, oldy=0, memask=memask & 0xFFFF, hitmask=hitmask & 0xFFFF,
                 bob=dict(flags=0, save=sb if sb >= 0 else None, db=bool(db), buf=bb if db else None, bufx=0,
                          bufy=0, inc1=False, after=None), coll=None, border=None, colors=None)
        # InitMasks: CollMask is every plane ORed, BorderLine every row of it
        cm = [[0] * (w * 16) for _ in range(h)]
        for r in range(h):
            for x in range(w * 16):
                for k in range(depth):
                    if self.bit(img, (k * h + r) * w * 2, x):
                        cm[r][x] = 1
        v['coll'] = cm if mk >= 0 else None
        v['border'] = [max(cm[r][x] for r in range(h)) for x in range(w * 16)] if ln >= 0 else None
        if mk >= 0:
            for r in range(h):
                for x in range(0, w * 16, 8):
                    byte = 0
                    for k in range(8):
                        byte = byte << 1 | cm[r][x + k]
                    self.bufs[mk][r * w * 2 + x // 8] = byte
        if ln >= 0:
            for x in range(0, w * 16, 8):
                byte = 0
                for k in range(8):
                    byte = byte << 1 | v['border'][x + k]
                self.bufs[ln][x // 8] = byte
        v['shadow'] = v['coll']
        self.vs[i] = v

    def is_bob(self, i):
        return 'bob' in self.vs[i] and not (self.vs[i]['flags'] & VSPRITE)

    def add_vs(self, i, keep=False):
        v = self.vs[i]
        if not keep:
            v['flags'] &= 0xFF
        k = 0
        while k < len(self.glist) and not self.sorts_before(v, self.vs[self.glist[k]]):
            k += 1
        self.glist.insert(k, i)

    @staticmethod
    def sorts_before(a, b):
        return a['y'] < b['y'] or (a['y'] == b['y'] and a['x'] < b['x'])

    def ADDBOB(self, i):
        self.emit(22, i)
        self.ADDBOB_quiet(i)

    def MOVE(self, i, x, y):
        self.emit(23, i, x, y)
        self.vs[i]['x'], self.vs[i]['y'] = x, y

    def AFTER(self, i, o):
        self.emit(24, i, o)
        self.vs[i]['bob']['after'] = None if o < 0 else o

    def SORT(self):
        self.emit(26)
        self.glist.sort(key=lambda i: (self.vs[i]['y'], self.vs[i]['x']))

    def AWAY(self, i):
        self.emit(27, i)
        self.vs[i]['bob']['flags'] |= BOBSAWAY

    def SWAPBM(self, b):
        self.emit(29, b)
        self.rps[0]['bm'] = b

    def LIST(self):
        self.emit(31)
        s = "list"
        for i in self.glist:
            v = self.vs[i]
            s += " %d:%d:%d" % (i, v['flags'] & 0xFFFF, v['bob']['flags'] if 'bob' in v else 0)
        self.out.append(s)

    def save_back(self, v, sbid, x, y):
        d = {}
        for (b, bx, by, rx, ry) in self.pieces(self.rps[0], x, y, v['w'] * 16, v['h']):
            for p in range(b.ndepth()):
                if isinstance(b.planes[p], tuple):
                    d[(p, rx, ry)] = self.pget(b, p, bx, by)
        self.saved[sbid] = d

    def restore_back(self, v, sbid, x, y):
        d = self.saved.get(sbid, {})
        for (b, bx, by, rx, ry) in list(self.pieces(self.rps[0], x, y, v['w'] * 16, v['h'])):
            for p in range(b.ndepth()):
                if isinstance(b.planes[p], tuple) and (p, rx, ry) in d:
                    self.pset(b, p, bx, by, d[(p, rx, ry)])

    def draw_image(self, v):
        overlay = v['flags'] & OVERLAY
        for (b, bx, by, rx, ry) in list(self.pieces(self.rps[0], v['x'], v['y'], v['w'] * 16, v['h'])):
            if overlay and v['shadow'] is not None and not v['shadow'][ry][rx]:
                continue
            for p in range(b.ndepth()):
                if not isinstance(b.planes[p], tuple):
                    continue
                val = v['onoff'] >> p & 1
                if v['pick'] >> p & 1:
                    k = bin(v['pick'] & ((1 << p) - 1)).count('1')
                    if k < v['depth']:
                        val = self.bit(v['img'], (k * v['h'] + ry) * v['w'] * 2, rx)
                self.pset(b, p, bx, by, val)

    def draw_bob(self, i):
        v = self.vs[i]
        bob = v['bob']
        if (v['flags'] & SAVEBACK) and bob['save'] is not None:
            self.save_back(v, bob['save'], v['x'], v['y'])
            v['oldx'], v['oldy'] = v['x'], v['y']
            v['flags'] |= BACKSAVED
            self.c0.insert(0, i)
        self.draw_image(v)
        bob['flags'] = (bob['flags'] & ~BWAITING) | BDRAWN

    def draw_ordered(self, i):
        bob = self.vs[i]['bob']
        while not bob['flags'] & BDRAWN:
            x = i
            self.vs[x]['bob']['flags'] |= BWAITING
            while True:
                a = self.vs[x]['bob']['after']
                if a is None or self.vs[a]['bob']['flags'] & (BDRAWN | BWAITING | BOBSAWAY | BOBNIX):
                    break
                x = a
                self.vs[x]['bob']['flags'] |= BWAITING
            self.draw_bob(x)
            y = i
            while y is not None and self.vs[y]['bob']['flags'] & BWAITING:
                self.vs[y]['bob']['flags'] &= ~BWAITING
                y = self.vs[y]['bob']['after']

    def DRAW(self):
        self.emit(25)
        # 1
        dbs = []
        for i in self.c0:
            v = self.vs[i]
            v['flags'] &= ~BACKSAVED
            if v['bob']['db']:
                dbs.append(i)
            elif v['bob']['save'] is not None:
                self.restore_back(v, v['bob']['save'], v['oldx'], v['oldy'])
        self.c0 = []
        for i in self.c1:
            v = self.vs[i]
            if v['bob']['buf'] is not None:
                self.restore_back(v, v['bob']['buf'], v['bob']['bufx'], v['bob']['bufy'])
            v['bob']['inc1'] = False
        self.c1 = []
        for i in dbs:
            v = self.vs[i]
            bob = v['bob']
            bob['save'], bob['buf'] = bob['buf'], bob['save']
            v['oldx'], v['oldy'], bob['bufx'], bob['bufy'] = bob['bufx'], bob['bufy'], v['oldx'], v['oldy']
            bob['inc1'] = True
            self.c1.append(i)
        # 2
        for i in list(self.glist):
            if self.is_bob(i):
                bob = self.vs[i]['bob']
                bob['flags'] &= ~(BDRAWN | BWAITING)
                if bob['flags'] & BOBSAWAY and not (bob['db'] and bob['inc1']):
                    self.glist.remove(i)
                    bob['flags'] |= BOBNIX
        # 3
        self.place_vsprites()
        # 4
        for i in list(self.glist):
            if self.is_bob(i) and not self.vs[i]['bob']['flags'] & (BDRAWN | BOBSAWAY):
                self.draw_ordered(i)

    def REMIBOB(self, i):
        self.emit(28, i)
        v = self.vs[i]
        redo = []
        if v['flags'] & BACKSAVED:
            while self.c0:
                w = self.c0.pop(0)
                W = self.vs[w]
                self.restore_back(W, W['bob']['save'], W['oldx'], W['oldy'])
                W['flags'] &= ~BACKSAVED
                if w == i:
                    break
                redo.insert(0, w)
        if v['flags'] & BACKSAVED:
            self.c0.remove(i)
            v['flags'] &= ~BACKSAVED
        if v['bob']['db'] and v['bob']['inc1']:
            self.c1.remove(i)
            v['bob']['inc1'] = False
        if i in self.glist:
            self.glist.remove(i)
        v['bob']['flags'] |= BOBNIX
        for w in redo:
            self.draw_bob(w)

    # ---- VSprites ----
    def VSPRITE(self, i, y, x, h, c):
        self.emit(32, i, y, x, h, c)
        self.vs[i] = dict(id=i, x=x, y=y, h=h, w=1, flags=VSPRITE, colors=c or None)
        self.add_vs(i)

    def REMVS(self, i):
        self.emit(34, i)
        self.glist.remove(i)

    def place_vsprites(self):
        sprites = [i for i in self.glist if self.vs[i]['flags'] & VSPRITE]
        if not sprites:
            return
        nl = [-32768] * 8
        lc = [None] * 8
        slots = []
        for i in sprites:
            v = self.vs[i]
            v['flags'] &= ~(VSOVERFLOW | GELGONE)
            if v['h'] <= 0:
                v['flags'] |= GELGONE
                continue
            found = -1
            for s in range(8):
                if self.gi['rsrvd'] >> s & 1 or nl[s] > v['y']:
                    continue
                m = s ^ 1
                if v['colors'] and nl[m] > v['y'] and lc[m] and lc[m] != v['colors']:
                    continue
                found = s
                break
            if found < 0:
                v['flags'] |= VSOVERFLOW
                continue
            fl = 1 if v['colors'] and lc[found] != v['colors'] else 0
            nl[found] = v['y'] + v['h'] + 1
            if v['colors']:
                lc[found] = lc[found ^ 1] = v['colors']
            slots.append((i, found, v['y'], fl))
        self.slots = slots

    def SLOTS(self):
        self.emit(33)
        self.out.append("slots" + "".join(" %d:%d:%d:%d" % s for s in self.slots))
        self.slots = []

    # ---- collisions ----
    def COLL(self, m):
        self.emit(30, m)
        log = ""
        gl = self.glist
        for k, a in enumerate(gl):
            A = self.vs[a]
            if A['hitmask'] & 1:
                hit = 0
                if A['border'] is not None:
                    xs = [x for x in range(A['w'] * 16) if A['border'][x]]
                else:
                    xs = list(range(A['w'] * 16))
                if xs:
                    if A['y'] < self.gi['top']:
                        hit |= 1
                    if A['y'] + A['h'] - 1 > self.gi['bottom']:
                        hit |= 2
                    if A['x'] + xs[0] < self.gi['left']:
                        hit |= 4
                    if A['x'] + xs[-1] > self.gi['right']:
                        hit |= 8
                if hit and m & 1:
                    log += " B%d:%d" % (a, hit)
            for b in gl[k + 1:]:
                B = self.vs[b]
                if not B['y'] <= A['y'] + A['h'] - 1:
                    break
                mm = A['hitmask'] & B['memask'] & 0xFFFE
                if not mm or not self.meet(A, B):
                    continue
                bit = 1
                while not mm >> bit & 1:
                    bit += 1
                if m >> bit & 1:
                    log += " P%d:%d,%d" % (bit, a, b)
        self.out.append("coll" + log)

    @staticmethod
    def meet(a, b):
        x0, x1 = max(a['x'], b['x']), min(a['x'] + a['w'] * 16 - 1, b['x'] + b['w'] * 16 - 1)
        y0, y1 = max(a['y'], b['y']), min(a['y'] + a['h'] - 1, b['y'] + b['h'] - 1)
        if x0 > x1 or y0 > y1:
            return False
        if a['coll'] is None or b['coll'] is None:
            return True
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                if a['coll'][y - a['y']][x - a['x']] and b['coll'][y - b['y']][x - b['x']]:
                    return True
        return False

    # ---- animation ----
    def ANIMOB(self, i, y, x, yv, xv, ya, xa, ry, rx, routine):
        self.emit(40, i, y, x, yv, xv, ya, xa, ry, rx, routine)
        self.obs[i] = dict(id=i, any=y, anx=x, yvel=yv, xvel=xv, yacc=ya, xacc=xa, ringy=ry, ringx=rx,
                           routine=routine, clock=0, oldx=0, oldy=0, head=None)

    def COMP(self, i, bob, timeset, yt, xt, flags, routine):
        self.emit(41, i, bob, timeset, yt, xt, flags, routine)
        self.comps[i] = dict(id=i, bob=bob, timeset=timeset, timer=0, yt=yt, xt=xt, flags=flags, routine=routine,
                             next=None, prev=None, nseq=None, pseq=None)

    def SEQ(self, a, b):
        self.emit(42, a, b)
        self.comps[a]['nseq'] = b
        self.comps[b]['pseq'] = a

    def CHAIN(self, a, b):
        self.emit(43, a, b)
        self.comps[a]['next'] = b
        self.comps[b]['prev'] = a

    def HEAD(self, o, c):
        self.emit(44, o, c)
        self.obs[o]['head'] = c

    def chain_of(self, o):
        c = self.obs[o]['head']
        while c is not None:
            yield c
            c = self.comps[c]['next']

    def ADDANIM(self, o):
        self.emit(45, o)
        self.animkey.insert(0, o)
        for c in list(self.chain_of(o)):
            C = self.comps[c]
            C['timer'] = C['timeset']
            if C['bob'] is not None:
                self.ADDBOB_quiet(C['bob'])

    def ADDBOB_quiet(self, i):
        bob = self.vs[i]['bob']
        listed = bob['flags'] & (BOBSAWAY | BOBNIX) == BOBSAWAY
        bob['flags'] &= ~(BWAITING | BDRAWN | BOBSAWAY | BOBNIX | OUTSTEP)
        if listed:
            self.glist.remove(i)
            self.add_vs(i, keep=True)
            return
        if bob['db']:
            bob['inc1'] = False
        self.add_vs(i)

    def ANIMATE(self):
        self.emit(46)
        log = ""
        for o in self.animkey:
            ob = self.obs[o]
            ob['clock'] += 1
            ob['oldx'], ob['oldy'] = ob['anx'], ob['any']
            ob['xvel'] = s16(ob['xvel'] + ob['xacc'])
            ob['yvel'] = s16(ob['yvel'] + ob['yacc'])
            ob['anx'] = s16(ob['anx'] + ob['xvel'])
            ob['any'] = s16(ob['any'] + ob['yvel'])
            if ob['routine']:
                log += " O%d" % o
            c = ob['head']
            while c is not None:
                C = self.comps[c]
                nxt = C['next']
                if C['timeset'] and C['timer'] > 0:
                    C['timer'] -= 1
                if C['timeset'] and C['timer'] == 0 and C['nseq'] is not None and C['nseq'] != c:
                    n = C['nseq']
                    N = self.comps[n]
                    N['prev'], N['next'] = C['prev'], C['next']
                    if C['prev'] is not None:
                        self.comps[C['prev']]['next'] = n
                    else:
                        ob['head'] = n
                    if C['next'] is not None:
                        self.comps[C['next']]['prev'] = n
                    C['prev'] = C['next'] = None
                    N['timer'] = N['timeset']
                    if N['flags'] & RINGTRIGGER:
                        ob['anx'] = s16(ob['anx'] + ob['ringx'])
                        ob['any'] = s16(ob['any'] + ob['ringy'])
                    if C['bob'] != N['bob']:
                        if C['bob'] is not None:
                            self.vs[C['bob']]['bob']['flags'] |= BOBSAWAY
                        if N['bob'] is not None:
                            self.ADDBOB_quiet(N['bob'])
                c = nxt
            for c in self.chain_of(o):
                C = self.comps[c]
                if C['bob'] is not None:
                    v = self.vs[C['bob']]
                    v['x'] = s16(s16(ob['anx'] >> 6) + C['xt'])
                    v['y'] = s16(s16(ob['any'] >> 6) + C['yt'])
            for c in self.chain_of(o):
                if self.comps[c]['routine']:
                    log += " R%d" % c
        self.out.append("animate" + log)

    def ANIMDUMP(self):
        self.emit(47)
        s = "anim"
        for o in self.animkey:
            ob = self.obs[o]
            s += " ob%d %d %d,%d %d,%d %d,%d [" % (o, ob['clock'], ob['anx'], ob['any'], ob['xvel'], ob['yvel'],
                                                   ob['oldx'], ob['oldy'])
            for c in self.chain_of(o):
                C = self.comps[c]
                v = self.vs[C['bob']] if C['bob'] is not None else None
                s += " %d:%d:%d,%d" % (c, C['timer'], v['x'] if v else 0, v['y'] if v else 0)
            s += " ]"
        self.out.append(s)

    def GETGB(self, o, db, failat):
        self.emit(48, o, db, failat)
        n = 0
        for c in self.chain_of(o):
            s = c
            while True:
                if self.comps[s]['bob'] is not None:
                    n += 1
                s = self.comps[s]['nseq']
                if s is None or s == c:
                    break
        total = n * (6 if db else 4)
        if failat < 0 or failat >= total:
            self.out.append("getgb 1 %d 0" % total)
        else:
            self.out.append("getgb 0 0")

    # ---- the blitter queue ----
    def q_init(self):
        self.lock = None
        self.nest = 0
        self.queue = []
        self.bsq = []
        self.waiters = []
        self.qlog = ""
        self.cur = None
        self.nodes = {}

    def QNODE(self, i, again, cleanup, b, value, beamsync):
        self.emit(60, i, again, cleanup, b, value, beamsync)
        self.nodes[i] = dict(again=again, cleanup=cleanup, buf=b, value=value, beamsync=beamsync, calls=0)

    def call(self, n):
        N = self.nodes[n]
        self.qlog += " C%d" % n
        v = (N['value'] ^ N['calls']) & 0xFFFF
        b = self.bufs[N['buf']]
        for k in range(0, len(b) & ~1, 2):
            b[k], b[k + 1] = v >> 8, v & 0xFF
        N['calls'] += 1

    def start(self, vertb=False):
        """the blitter is free: the queue's first call, now (the rest at sync)"""
        if self.queue:
            self.lock, self.cur = 'Q', (self.queue, self.queue[0])
            self.call(self.queue[0])
        elif self.bsq and (vertb or self.nodes[self.bsq[0]]['beamsync'] == 0):
            self.lock, self.cur = 'Q', (self.bsq, self.bsq[0])
            self.call(self.bsq[0])
        elif self.waiters:
            self.lock = self.waiters.pop(0)
            self.nest = 0

    def sync(self):
        while self.lock == 'Q':
            q, n = self.cur
            N = self.nodes[n]
            while N['calls'] <= N['again']:
                self.call(n)
            if N['cleanup']:
                self.qlog += " X%d" % n
            q.pop(0)
            self.lock = None
            self.start()

    def QBLIT(self, n):
        self.emit(61, n)
        self.qlog += " Q%d" % n
        self.queue.append(n)
        if self.lock is None:
            self.start()
        self.qlog += " q"

    def QBSBLIT(self, n):
        self.emit(62, n)
        self.qlog += " S%d" % n
        self.bsq.append(n)
        if self.lock is None:
            self.start()
        self.qlog += " s"

    def OWN(self, t):
        self.emit(63, t)
        if self.lock == t:
            self.nest += 1
        elif self.lock is None and not self.queue and not self.waiters:
            self.lock = t
        else:
            self.waiters.append(t)
            self.sync()
            assert self.lock == t
        self.qlog += " O%d" % t

    def DISOWN(self, t):
        self.emit(64, t)
        assert self.lock == t
        if self.nest:
            self.nest -= 1
        else:
            self.lock = None
            self.start()
        self.qlog += " D%d" % t

    def OWNBLOCK(self, t, other):
        self.emit(65, t, other)
        assert self.lock == other and self.nest == 0
        self.waiters.append(t)
        self.lock = None
        self.start()
        self.sync()
        assert self.lock == t
        self.qlog += " O%d" % t

    def VERTB(self):
        self.emit(66)
        self.sync()
        if self.lock is None and not self.queue:
            self.start(vertb=True)

    def QLOG(self):
        self.emit(68)
        self.sync()
        self.out.append("qlog" + self.qlog)
        self.qlog = ""


# ---- the rounds ---------------------------------------------------------------

def make_bitmap(m, idx, depth, bpr, rows, chip, interleaved=False, odd_planes=True):
    r = m.rng
    if interleaved and depth > 1:
        b = m.BUF(bpr * depth * rows, chip, r.randrange(1, 1 << 30))
        planes = [(b, p * bpr) for p in range(depth)]
        m.BITMAP(idx, bpr * depth, rows, 4, depth, planes)
        return [b]
    planes, ids = [], []
    for p in range(depth):
        k = r.randrange(12) if odd_planes else 5
        if k == 0:
            planes.append(None)
        elif k == 1:
            planes.append('ones')
        else:
            b = m.BUF(bpr * rows, chip, r.randrange(1, 1 << 30))
            planes.append((b, 0))
            ids.append(b)
    m.BITMAP(idx, bpr, rows, 0, depth, planes)
    return ids


def make_rp(m, chip, layered, mode_rnd=True, depth=None, interleaved=None):
    """screen bitmap 0, rp 0, and maybe layer 0 with its ClipRects"""
    r = m.rng
    depth = depth or r.randint(1, 5)
    bpr = r.choice([4, 6, 8, 10, 12])
    rows = r.randint(12, 28)
    if interleaved is None:
        interleaved = r.random() < 0.2
    ids = make_bitmap(m, 0, depth, bpr, rows, chip, interleaved)
    layer = -1
    if layered:
        w = bpr * 8
        minx, miny = r.randint(0, w // 3), r.randint(0, rows // 3)
        maxx, maxy = r.randint(minx + 4, w - 1), r.randint(miny + 3, rows - 1)
        cuts_x = sorted(set([minx, r.randint(minx + 1, maxx), maxx + 1]))
        cuts_y = sorted(set([miny, r.randint(miny + 1, maxy), maxy + 1]))
        crs = []
        nb = 1
        for j in range(len(cuts_y) - 1):
            for i in range(len(cuts_x) - 1):
                x0, x1, y0, y1 = cuts_x[i], cuts_x[i + 1] - 1, cuts_y[j], cuts_y[j + 1] - 1
                kind = r.choice([0, 0, 1, 1, 2])
                cbm = 0
                if kind == 1:
                    cbm = nb
                    nb += 1
                    cbpr = ((x1 - (x0 & ~15)) // 16 + 1) * 2
                    ids += make_bitmap(m, cbm, depth, cbpr, y1 - y0 + 1, r.randrange(2), False, False)
                crs.append((x0, y0, x1, y1, kind, cbm))
        sx, sy = r.randint(0, 3), r.randint(0, 2)
        m.emit(3, 0, 0, minx, miny, maxx, maxy, sx, sy, 2, len(crs))
        for k, c in enumerate(crs):
            m.emit(k, *c)
        m.layers[0] = dict(minx=minx, miny=miny, sx=sx, sy=sy, crs=crs)
        layer = 0
    mask = 0xFF if r.random() < 0.6 else r.randrange(256)
    fg, bg, mode = r.randrange(256), r.randrange(256), r.randrange(8)
    m.emit(4, 0, 0, layer, mask, fg, bg, mode)
    m.rps[0] = dict(bm=0, layer=None if layer < 0 else 0, mask=mask, fg=fg, bg=bg, mode=mode)
    return ids, bpr * 8, rows, depth


def round_blits(m):
    r = m.rng
    m.RESET(blitter=False)
    chip = r.randrange(2)
    ids, w, rows, depth = make_rp(m, chip, r.random() < 0.4)
    for _ in range(r.randint(3, 7)):
        k = r.randrange(3)
        if k == 0:
            srcx = r.randint(0, 20)
            ww, hh = r.randint(1, 60), r.randint(1, 16)
            srcmod = ((srcx + ww + 15) // 16) * 2 + 2 * r.randrange(2)
            tb = m.BUF(srcmod * hh + 2, r.randrange(2), r.randrange(1, 1 << 30))
            m.TEMPLATE(tb, srcx, srcmod, 0, r.randint(-10, w), r.randint(-4, rows), ww, hh)
        elif k == 1:
            sd = r.randint(1, 5)
            sbpr, srows = r.choice([4, 6, 8]), r.randint(4, 20)
            sbid = 9
            make_bitmap(m, sbid, sd, sbpr, srows, r.randrange(2), r.random() < 0.2)
            mb = m.BUF(sbpr * srows, r.randrange(2), r.randrange(1, 1 << 30))
            mt = r.choice([0xE0, 0xE0, 0x20, r.randrange(256)])
            m.MASKBLT(sbid, r.randint(-6, sbpr * 8), r.randint(-3, srows), 0, r.randint(-10, w),
                      r.randint(-4, rows), r.randint(1, 64), r.randint(1, 20), mt, mb)
            if m.rps[0]['layer'] is None and r.random() < 0.5:
                # the RastPort's own bitmap as the source, the rectangle moved
                # a little down and right: an overlap
                own = m.rps[0]['bm']
                ob = m.bms[own]
                omb = m.BUF((ob.width() // 8) * ob.rows, r.randrange(2), r.randrange(1, 1 << 30))
                ox, oy = r.randint(0, 20), r.randint(0, 6)
                m.MASKBLT(own, ox, oy, 0, ox + r.randint(0, 9), oy + r.randint(0, 3), r.randint(1, 48),
                          r.randint(1, 12), r.choice([0xE0, 0x20]), omb)
        else:
            size = 2 * r.randint(1, 300)
            b = m.BUF(size, r.randrange(2), r.randrange(1, 1 << 30))
            if r.random() < 0.5:
                bp = r.randint(1, 40)
                rws = r.randint(0, size // max(bp, 1))
                count = rws << 16 | bp
                flags = 2
            else:
                count = r.randint(0, size)
                flags = 0
            flags |= r.randrange(2) | 4 * r.randrange(2)
            m.CLEAR(b, count, flags)
        m.HASH(sorted(m.bufs))


def make_bob(m, i, sdepth, chip, db=False, colls=False):
    r = m.rng
    w, h, d = r.randint(1, 2), r.randint(1, 8), r.randint(1, 3)
    img = m.BUF(w * h * d * 2, r.randrange(2) if not chip else 1, r.randrange(1, 1 << 30))
    sb = m.BUF(w * h * sdepth * 2, chip, 0) if r.random() < 0.9 or db else -1
    bb = m.BUF(w * h * sdepth * 2, chip, 0) if db else 0
    mk = m.BUF(w * h * 2, chip, 0) if not colls or r.random() < 0.8 else -1
    ln = m.BUF(w * 2, chip, 0) if not colls or r.random() < 0.8 else -1
    flags = (SAVEBACK if r.random() < 0.85 or db else 0) | (OVERLAY if r.random() < 0.6 else 0)
    memask, hitmask = r.randrange(1 << 16), r.randrange(1 << 16)
    if not colls:
        memask = hitmask = 0
    m.BOB(i, w, h, d, r.randrange(256) if r.random() < 0.3 else (1 << sdepth) - 1 - r.randrange(1 << sdepth) % 3,
          r.randrange(256), flags, img, sb, 1 if db else 0, bb, memask, hitmask, mk, ln)


def round_gels(m):
    r = m.rng
    m.RESET(blitter=False)
    chip = r.randrange(2)
    ids, w, rows, depth = make_rp(m, chip, r.random() < 0.35)
    m.gels_init(0, 0, w - 1, 0, rows - 1)
    n = r.randint(2, 5)
    for i in range(n):
        make_bob(m, i, depth, chip)
        m.MOVE(i, r.randint(-12, w), r.randint(-5, rows))
    for i in range(n):
        if r.random() < 0.4 and i + 1 < n:
            m.AFTER(i, r.randint(i + 1, n - 1))
    for i in r.sample(range(n), n):
        m.ADDBOB(i)
    m.LIST()
    for _ in range(r.randint(3, 6)):
        for i in range(n):
            if r.random() < 0.6:
                m.MOVE(i, r.randint(-12, w), r.randint(-5, rows))
        k = r.random()
        live = [i for i in m.glist if m.is_bob(i)]
        gone = [i for i in range(n) if m.vs[i]['bob']['flags'] & BOBNIX]
        if k < 0.15 and live:
            m.AWAY(r.choice(live))
        elif k < 0.3 and live:
            m.REMIBOB(r.choice(live))
        elif k < 0.45 and gone:
            m.ADDBOB(r.choice(gone))
        if r.random() < 0.5:
            m.SORT()
        m.DRAW()
        m.HASH(ids)
        m.LIST()


def round_dbgels(m):
    r = m.rng
    m.RESET()
    chip = r.randrange(2)
    depth = r.randint(1, 4)
    bpr, rows = r.choice([4, 6, 8]), r.randint(10, 24)
    ids = make_bitmap(m, 0, depth, bpr, rows, chip, False, False)
    ids += make_bitmap(m, 1, depth, bpr, rows, chip, False, False)
    m.emit(4, 0, 0, -1, 0xFF, 0, 0, 0)
    m.rps[0] = dict(bm=0, layer=None, mask=0xFF, fg=0, bg=0, mode=0)
    w = bpr * 8
    m.gels_init(0, 0, w - 1, 0, rows - 1)
    n = r.randint(1, 4)
    for i in range(n):
        make_bob(m, i, depth, chip, db=True)
        m.MOVE(i, r.randint(-8, w), r.randint(-4, rows))
        m.ADDBOB(i)
    cur = 0
    for _ in range(r.randint(4, 7)):
        m.SWAPBM(cur)
        cur ^= 1
        for i in range(n):
            if r.random() < 0.7:
                m.MOVE(i, r.randint(-8, w), r.randint(-4, rows))
        live = [i for i in m.glist if m.is_bob(i)]
        if r.random() < 0.2 and live:
            m.AWAY(r.choice(live))
        m.SORT()
        m.DRAW()
        m.HASH(ids)
        m.LIST()


def round_coll(m):
    r = m.rng
    m.RESET()
    ids, w, rows, depth = make_rp(m, 0, False, depth=2, interleaved=False)
    m.gels_init(0, r.randint(-4, 8), r.randint(w - 12, w + 4), r.randint(-4, 4), r.randint(rows - 6, rows + 2))
    n = r.randint(2, 7)
    for i in range(n):
        make_bob(m, i, depth, 0, colls=True)
        m.MOVE(i, r.randint(-10, 40), r.randint(-6, 16))
        m.ADDBOB(i)
    for _ in range(3):
        m.SORT()
        m.COLL(r.randrange(1 << 16))
        for i in range(n):
            m.MOVE(i, r.randint(-10, 40), r.randint(-6, 16))


def round_vsprites(m):
    r = m.rng
    m.RESET()
    make_rp(m, 0, False, depth=1, interleaved=False)
    m.gels_init(r.choice([0, 0, 0x03, 0x30, r.randrange(256)]), 0, 0, 0, 0)
    n = r.randint(1, 16)
    for i in range(n):
        m.VSPRITE(i, r.randint(-2, 60), r.randint(0, 300), r.randint(0, 12), r.randrange(4))
    m.DRAW()
    m.SLOTS()
    m.LIST()
    for i in r.sample(range(n), r.randint(0, n)):
        m.REMVS(i)
    m.DRAW()
    m.SLOTS()
    m.LIST()


def round_anim(m):
    r = m.rng
    m.RESET()
    ids, w, rows, depth = make_rp(m, 0, False, depth=2, interleaved=False)
    m.gels_init(0, 0, w - 1, 0, rows - 1)
    m.emit(49)
    m.obs, m.comps, m.animkey = {}, {}, []
    nb = nc = 0
    for o in range(r.randint(1, 2)):
        m.ANIMOB(o, r.randint(-500, 500), r.randint(-500, 500), r.randint(-40, 40), r.randint(-40, 40),
                 r.randint(-5, 5), r.randint(-5, 5), r.randint(-30, 30), r.randint(-30, 30), r.randrange(2))
        heads = []
        for k in range(r.randint(1, 3)):
            ring = []
            for s in range(r.randint(1, 3)):
                make_bob(m, nb, depth, 0)
                m.COMP(nc, nb, r.randint(0, 3), r.randint(-4, 4), r.randint(-4, 4),
                       RINGTRIGGER if r.random() < 0.3 else 0, r.randrange(2))
                ring.append(nc)
                nb += 1
                nc += 1
            for a, b in zip(ring, ring[1:] + ring[:1]):
                m.SEQ(a, b)
            heads.append(ring[0])
        for a, b in zip(heads, heads[1:]):
            m.CHAIN(a, b)
        m.HEAD(o, heads[0])
        m.ADDANIM(o)
    m.LIST()
    for _ in range(r.randint(3, 8)):
        m.ANIMATE()
        m.ANIMDUMP()
        m.LIST()
    for o in range(len(m.obs)):
        db = r.randrange(2)
        m.GETGB(o, db, r.choice([-1, r.randint(0, 20)]))


def round_ilmask(m):
    """BltMaskBitMapRastPort from an interleaved bitmap to itself, reaching
    its last row, the bitmap at the end of its memory: each plane's part
    of a row is read, never what follows the block. Sometimes plane 0 is
    constant."""
    r = m.rng
    m.RESET(blitter=False)
    depth, bpr, rows = r.randint(2, 5), r.choice([4, 6, 8]), r.randint(6, 16)
    b = m.BUF(bpr * depth * rows, 0, r.randrange(1, 1 << 30), guard=True)
    planes = [(b, p * bpr) for p in range(depth)]
    if r.random() < 0.3:
        planes[0] = r.choice([None, 'ones'])
    m.BITMAP(0, bpr * depth, rows, 4, depth, planes)
    mask, fg, bg = 0xFF, r.randrange(256), r.randrange(256)
    m.emit(4, 0, 0, -1, mask, fg, bg, 0)
    m.rps[0] = dict(bm=0, layer=None, mask=mask, fg=fg, bg=bg, mode=0)
    for _ in range(r.randint(1, 3)):
        h = r.randint(1, rows)
        ox, oy = r.randint(0, bpr * 4), rows - h
        omb = m.BUF(bpr * rows, r.randrange(2), r.randrange(1, 1 << 30))
        m.MASKBLT(0, ox, oy, 0, ox + r.randint(-4, 9), oy - r.randint(0, 3), r.randint(1, bpr * 8), h,
                  r.choice([0xE0, 0x20]), omb)
    m.HASH([b])
    # in Chip RAM with the blitter, the destination a whole number of words
    # along: the blitter reads the copy (MEMF_ANY may be Chip RAM), and it
    # is not freed until that blit is done
    m.RESET(blitter=True)
    ids, w, rows, depth = make_rp(m, 1, False, mode_rnd=False, interleaved=False)
    for _ in range(2):
        ox, oy = r.randint(0, 15), r.randint(0, 6)
        omb = m.BUF((w // 8) * rows, 1, r.randrange(1, 1 << 30))
        m.MASKBLT(0, ox, oy, 0, ox + 16 * r.randint(0, 1), oy + r.randint(0, 3), r.randint(1, 16 - ox),
                  r.randint(1, 12), r.choice([0xE0, 0x20]), omb)
    m.HASH(ids)


def round_queue(m):
    r = m.rng
    m.RESET(blitter=True)
    m.q_init()
    bufs = [m.BUF(2 * r.randint(1, 60), 1, 0) for _ in range(6)]
    nn = 0

    def node(beamsync=0):
        nonlocal nn
        i = nn
        nn += 1
        m.QNODE(i, r.randint(0, 3), r.randrange(2), r.choice(bufs), r.randrange(1 << 16), beamsync)
        return i

    # owned first: QBlit and QBSBlit wait; another task waits too
    m.OWN(0)
    for _ in range(r.randint(0, 3)):
        m.QBLIT(node())
    for _ in range(r.randint(0, 2)):
        m.QBSBLIT(node())
    m.OWNBLOCK(1, 0)
    m.DISOWN(1)
    m.QLOG()
    # free: QBlit's first call is at once
    m.QBLIT(node())
    m.QLOG()
    # nested ownership
    m.OWN(2)
    m.OWN(2)
    m.DISOWN(2)
    m.QBLIT(node())
    m.DISOWN(2)
    m.QLOG()
    # an owner waits for the queue
    m.QBLIT(node())
    m.OWN(3)
    m.DISOWN(3)
    m.QLOG()
    # QBSBlit waits for its line, which VERTB waits for
    m.QBSBLIT(node(r.randint(150, 300)))
    m.QLOG()
    m.VERTB()
    m.QLOG()
    m.emit(69)
    m.HASH(bufs)


def main():
    outdir = sys.argv[1]
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    rounds = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    m = Model(seed)
    kinds = [round_blits, round_gels, round_dbgels, round_coll, round_vsprites, round_anim, round_queue, round_ilmask]
    for k in range(rounds):
        kinds[k % len(kinds)](m)
    m.emit(0)
    with open(os.path.join(outdir, "gels_script.h"), "w") as f:
        f.write("static const int32_t S[] = {\n")
        for k in range(0, len(m.script), 16):
            f.write(",".join(str(v) for v in m.script[k:k + 16]) + ",\n")
        f.write("};\n")
    with open(os.path.join(outdir, "expected.txt"), "w") as f:
        f.write("\n".join(m.out) + "\n")


if __name__ == "__main__":
    main()
