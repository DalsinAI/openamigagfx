# The independent model: what each ViewPort shows, from graphics.library's
# documented View/ViewPort semantics and the Hardware Reference Manual's
# pixel rules, with no copper. Writes scenes.h for the harness and the
# expected output lines.
import random, sys

seed = int(sys.argv[1]); nscenes = int(sys.argv[2]); hdr = sys.argv[3]; exp = sys.argv[4]
R = random.Random(seed)
LEGAL0, LEGAL1 = 0x51, 0x1C1
OCS, ECS, AGA = 0, 1, 2

def lcg_stream(s, n):
    out = bytearray(n)
    for i in range(n):
        s = (s * 1103515245 + 12345) & 0xffffffff
        out[i] = (s >> 16) & 0xff
    return out

def fnv(h, b):
    h ^= b
    return (h * 16777619) & 0xffffffff

def expand12(c):
    r, g, b = (c >> 8) & 15, (c >> 4) & 15, c & 15
    return (r * 17) << 16 | (g * 17) << 8 | b * 17

class BM:
    def __init__(s, depth, rowbytes, rows, inter, seed):
        s.depth, s.rowbytes, s.rows, s.inter, s.seed = depth, rowbytes, rows, inter, seed
        s.planes = [lcg_stream(seed + 7919 * k, rowbytes * rows) for k in range(depth)]
    def bit(s, k, r, c):
        if c < 0 or c >= s.rowbytes * 8 or r < 0 or r >= s.rows: return 0
        return s.planes[k][r * s.rowbytes + (c >> 3)] >> (7 - (c & 7)) & 1

# The first scenes sweep AGA's fetches: one ViewPort of hires and of
# superhires at every depth from 1 to 8, and lores at 8, plain and laced;
# the rest are random.
SWEEP = [(res, depth, lace) for lace in (0, 1) for res in (1, 2) for depth in range(1, 9)] + [(0, 8, 0), (0, 8, 1)]

def gen_scene(n):
    sc = {}
    force = SWEEP[n] if n < len(SWEEP) else None
    chip = R.choice([OCS, ECS, AGA]); sc['chip'] = chip
    if force: chip = sc['chip'] = AGA
    pal = R.random() < 0.5; sc['pal'] = pal
    sc['vex'] = 0
    if chip != OCS and R.random() < 0.3:
        sc['vex'] = R.choice([1, 2])        # a ViewExtra on ntsc (1) or pal (2)
    monpal = pal if sc['vex'] == 0 else sc['vex'] == 2
    sc['rows'] = 312 if monpal else 262
    sc['minrow'] = 29 if monpal else 21
    sc['vlace'] = R.random() < 0.4
    if force: sc['vlace'] = force[2] == 1
    sc['vx'] = R.randint(0x70, 0x90); sc['vy'] = R.randint(0x1c, 0x30)
    nvp = 1 if force else R.randint(1, 3)
    vps = []
    line = sc['vy'] + R.randint(-14, 20)    # frame line of the first ViewPort's top
    for i in range(nvp):
        v = {}
        # 0 lores, 1 hires, 2 superhires (ECS Denise and up)
        res = 1 if R.random() < 0.35 else 0
        if chip != OCS and R.random() < 0.25: res = 2
        if force: res = force[0]
        hires = res != 0
        lace = sc['vlace'] and R.random() < 0.7 or (not sc['vlace'] and R.random() < 0.08)
        kind = 'n'
        r = R.random()
        # AGA: 8 planes in every resolution (hires at 2x, superhires at 4x),
        # so EHB and HAM8 in hires and superhires too
        if (res == 0 or chip == AGA) and r < 0.12: kind = 'ehb'
        elif res == 0 and r < 0.24 and chip != AGA: kind = 'ham6'
        elif r < 0.24 and chip == AGA: kind = 'ham8'
        elif r < 0.36: kind = 'dpf'
        maxd = 8 if chip == AGA else (6, 4, 2)[res]
        if force:
            lace = force[2] == 1
            kind = 'ehb' if force[1] == 6 and R.random() < 0.5 else 'n'
        if kind == 'ehb': depth = 6
        elif kind == 'ham6': depth = 6
        elif kind == 'ham8': depth = 8
        elif kind == 'dpf': depth = R.randint(2, maxd)
        else: depth = R.randint(1, maxd)
        if force: depth = force[1]
        if kind == 'n' and depth == 6 and res == 0 and chip != AGA: depth = 5   # 6 planes alone is EHB (AGA: KILLEHB)
        v.update(hires=hires, res=res, lace=lace, kind=kind, depth=depth)
        w = R.randint(8, 360) << res
        h = R.randint(3, 60)
        v['w'], v['h'] = w, h
        nocliplft = kind.startswith('ham')
        dxl = R.randint(-50, 60)            # in lores pixels
        if nocliplft:
            dxl = max(dxl, LEGAL0 - sc['vx'] + 1)
        v['dx'] = (dxl << res) + R.randint(0, (1 << res) - 1)
        # top line from line; dy in the ViewPort's lines
        if lace:
            dyl = (line - sc['vy']) * 2 + R.randint(0, 1)
        else:
            dyl = line - sc['vy']
        v['dy'] = dyl
        nl = (h + 1) // 2 if lace else h
        line = line + nl + R.randint(14, 30)
        # playfields
        # a wider fetch (AGA: hires over 4 planes at 2x, superhires over 2 at
        # 4x) wants rows and planes on 32 or 64 bits, as AllocBitMap's
        # displayable bitmaps are on AGA; the others may be any words
        wide = (res == 1 and depth > 4) or (res == 2 and depth > 2)
        align = 8 if (res == 2 and wide) or (chip == AGA and R.random() < 0.5) else 4 if wide else 2
        def mkbm(d, extra):
            rowbytes = ((R.randint(0, 40) + w + 64) // (8 * align) + 1) * align
            rows = h + R.randint(0, 12) + extra
            return dict(depth=d, rowbytes=rowbytes, rows=rows, inter=R.random() < 0.3, seed=R.randint(1, 1 << 30))
        if kind == 'dpf':
            d2 = depth // 2; d1 = depth - d2
            v['bm'] = [mkbm(d1, 12), mkbm(d2, 12)]
            v['bmB'] = mkbm(d1, 12)
        else:
            v['bm'] = [mkbm(depth, 12)]
            v['bmB'] = mkbm(depth, 12)
        v['bmB']['rowbytes'] = v['bm'][0]['rowbytes']
        npf = len(v['bm'])
        v['rx'] = [R.randint(0, 40) for _ in range(npf)]
        v['ry'] = [R.randint(0, 10) for _ in range(npf)]
        v['rx2'] = [R.randint(0, 40) for _ in range(npf)]
        v['ry2'] = [R.randint(0, 10) for _ in range(npf)]
        # colours
        if chip == AGA:
            count = R.choice([32, 64, 256]) if depth <= 5 else 256
            if kind == 'dpf': count = max(count, 64)
        else:
            count = 32
        v['count'] = count
        v['hi'] = [R.randint(0, 0xfff) for _ in range(count)]
        v['lo'] = [R.randint(0, 0xfff) for _ in range(count)]
        v['full'] = chip == AGA and R.random() < 0.3
        v['pfba'] = kind == 'dpf' and R.random() < 0.5
        v['b1'] = R.choice([8, 16]) if (chip == AGA and kind == 'dpf') else 8
        v['bplam'] = R.choice([0, 0x10, 0x40]) if (chip == AGA and kind == 'n' and R.random() < 0.3) else 0
        if v['bplam']: v['count'] = count = 256; v['hi'] += [R.randint(0, 0xfff) for _ in range(256 - len(v['hi']))]; v['lo'] += [R.randint(0, 0xfff) for _ in range(256 - len(v['lo']))]
        # DisplayClip (ViewPort pixels and lines, from the View's origin)
        v['clip'] = None
        if R.random() < 0.25 and not nocliplft:
            x0 = v['dx'] + R.randint(-10, w // 2); y0 = v['dy'] + R.randint(-4, h // 2)
            v['clip'] = (x0, y0, x0 + R.randint(4, w), y0 + R.randint(2, h))
        # user copper: colour changes at the ViewPort's own lines
        v['ucop'] = []
        top_here = sc['vy'] + (dyl >> 1 if lace else dyl)
        if not lace and R.random() < 0.35 and v['clip'] is None and top_here >= sc['minrow']:
            for _ in range(R.randint(1, 3)):
                v['ucop'].append((R.randint(1, max(1, h - 1)), R.randint(0, 31), R.randint(0, 0xfff)))
            v['ucop'].sort(key=lambda e: e[0])
        vps.append(v)
    sc['vps'] = vps
    return sc

def show(sc, phase, field):
    """[(rect, fnv)] for each ViewPort in one field of one phase"""
    out = []
    chip = sc['chip']
    for vi, v in enumerate(sc['vps']):
        lace = v['lace']; res = v['res']; ppx = 4 >> res      # 35ns pixels a ViewPort pixel
        if phase == 2: bms = [v['bmB']] + v['bm'][1:]
        else: bms = v['bm']
        B = [BM(b['depth'], b['rowbytes'], b['rows'], b['inter'], b['seed']) for b in bms]
        rx = v['rx2'] if phase else v['rx']; ry = v['ry2'] if phase else v['ry']
        rxe = [x if chip == AGA else x & ~((1 << res) - 1) for x in rx]   # OCS/ECS scroll in lores pixels
        top = sc['vy'] + (v['dy'] >> 1 if lace else v['dy'])
        X0 = 4 * sc['vx'] + 4 * (v['dx'] >> res)          # 35ns pixels
        cols = range(v['w'])
        rows = range(v['h'])
        if v['clip']:
            cx0, cy0, cx1, cy1 = v['clip']
            cols = range(max(0, cx0 - v['dx']), min(v['w'], cx1 + 1 - v['dx']))
            rows = range(max(0, cy0 - v['dy']), min(v['h'], cy1 + 1 - v['dy']))
        cols = [c for c in cols if X0 + c * ppx >= 4 * LEGAL0 and X0 + c * ppx + ppx <= 4 * LEGAL1]
        sel = []
        for r in rows:
            if lace and (r & 1) != field: continue
            if not sc['vlace'] and field: continue
            line = top + (r >> 1 if lace else r)
            if line < sc['minrow'] or line >= sc['rows']: continue
            sel.append((r, line))
        if not sel or not cols: continue
        # palette as 24-bit
        pal24 = []
        for i in range(256):
            if i < v['count']:
                hi, lo = v['hi'][i], v['lo'][i]
                if chip == AGA:
                    pal24.append(((hi >> 8 & 15) << 4 | (lo >> 8 & 15)) << 16 | ((hi >> 4 & 15) << 4 | (lo >> 4 & 15)) << 8 | ((hi & 15) << 4 | (lo & 15)))
                else:
                    pal24.append(expand12(hi))
            else:
                pal24.append(None)
        h = 2166136261
        for (r, line) in sel:
            p = list(pal24)
            for (k, idx, val) in v['ucop']:
                if r >= k: p[idx] = expand12(val)
            hold = p[0]
            def bits(pf, c):
                b = B[pf]; val = 0
                for k in range(b.depth):
                    val |= b.bit(k, ry[pf] + r, rxe[pf] + c) << k
                return val
            row = []
            for c in cols if not v['kind'].startswith('ham') else range(cols[-1] + 1):
                kind = v['kind']
                if kind == 'dpf':
                    a = bits(0, c); b = bits(1, c)
                    ca = p[a] if a else None; cb = p[b + v['b1']] if b else None
                    if v['pfba']: col = cb if b else (ca if a else p[0])
                    else: col = ca if a else (cb if b else p[0])
                else:
                    val = bits(0, c)
                    if kind == 'ehb':
                        col = p[val & 31]
                        if val & 32:
                            if chip == AGA: col = (col >> 1) & 0x7f7f7f
                            else:
                                rr, gg, bb = (col >> 20) & 15, (col >> 12) & 15, (col >> 4) & 15
                                col = expand12((rr >> 1) << 8 | (gg >> 1) << 4 | (bb >> 1))
                    elif kind == 'ham6':
                        ctl, d = val >> 4, val & 15
                        if ctl == 0: col = p[d]
                        else:
                            rr, gg, bb = (hold >> 20) & 15, (hold >> 12) & 15, (hold >> 4) & 15
                            if ctl == 1: bb = d
                            elif ctl == 2: rr = d
                            else: gg = d
                            col = expand12(rr << 8 | gg << 4 | bb)
                        hold = col
                    elif kind == 'ham8':
                        ctl, d = val & 3, val >> 2
                        if ctl == 0: col = p[d]
                        else:
                            rr, gg, bb = (hold >> 16) & 255, (hold >> 8) & 255, hold & 255
                            if ctl == 1: bb = d << 2 | (bb & 3)
                            elif ctl == 2: rr = d << 2 | (rr & 3)
                            else: gg = d << 2 | (gg & 3)
                            col = rr << 16 | gg << 8 | bb
                        hold = col
                    else:
                        col = p[val ^ v['bplam']]
                row.append((c, col))
            if DUMP == (CURSC, phase, field, vi):
                print('L', line, ' '.join('%06x' % col for (c, col) in row if c >= cols[0])[:400])
            for (c, col) in row:
                if c < cols[0]: continue
                for _ in range(ppx):
                    h = fnv(h, col >> 16 & 255); h = fnv(h, col >> 8 & 255); h = fnv(h, col & 255)
        rect = (vi, sel[0][1], sel[-1][1] + 1, X0 + cols[0] * ppx, X0 + cols[-1] * ppx + ppx)
        out.append((rect, h))
    return out

import os
DUMP = tuple(int(x) for x in os.environ['DUMP'].split(',')) if os.environ.get('DUMP') else None
CURSC = -1
scenes = [gen_scene(i) for i in range(nscenes)]
H = open(hdr, 'w'); E = open(exp, 'w')
def arr(a): return '{' + ','.join(str(x) for x in a) + '}'
H.write('/* generated by model.py: scenes for test_gfx_display */\n')
H.write('#define NSCENES %d\n' % nscenes)
H.write('static const struct sbm SBM[] = {\n'); bmlist = []
def bmidx(b):
    bmlist.append(b); return len(bmlist) - 1
vprecs = []; ucops = []; rects = []
for si, sc in enumerate(scenes):
    for v in sc['vps']:
        v['_bm'] = [bmidx(b) for b in v['bm']]; v['_bmB'] = bmidx(v['bmB'])
for b in bmlist:
    H.write('  {%d,%d,%d,%d,%d},\n' % (b['depth'], b['rowbytes'], b['rows'], 1 if b['inter'] else 0, b['seed']))
H.write('};\n')
H.write('static const uint16_t COLHI[] = {\n'); off = 0
for sc in scenes:
    for v in sc['vps']:
        v['_hi'] = off; H.write(','.join(str(x) for x in v['hi']) + ',\n'); off += len(v['hi'])
H.write('0};\nstatic const uint16_t COLLO[] = {\n')
for sc in scenes:
    for v in sc['vps']:
        H.write(','.join(str(x) for x in v['lo']) + ',\n')
H.write('0};\n')
H.write('static const struct svp SVP[] = {\n'); vpi = 0; ui = 0
for si, sc in enumerate(scenes):
    sc['_vp'] = vpi
    for v in sc['vps']:
        modes = (0x8000 if v['res'] else 0) | (0x20 if v['res'] == 2 else 0) | (4 if v['lace'] else 0)
        if v['kind'] == 'ehb': modes |= 0x80
        if v['kind'].startswith('ham'): modes |= 0x800
        if v['kind'] == 'dpf': modes |= 0x400 | (0x40 if v['pfba'] else 0)
        clip = v['clip'] or (0, 0, 0, 0)
        b1 = v['_bm'][1] if len(v['_bm']) > 1 else -1
        rx = v['rx'] + [0]; ry = v['ry'] + [0]; rx2 = v['rx2'] + [0]; ry2 = v['ry2'] + [0]
        H.write('  {%d,%d,%d,%d,%d,%d,%d,%d,%d,{%d,%d},{%d,%d},{%d,%d},{%d,%d},%d,%d,%d,%d,%d,%d,{%d,%d,%d,%d},%d,%d,%d},\n' % (
            modes, v['dx'], v['dy'], v['w'], v['h'], v['_bm'][0], b1, v['_bmB'], v['count'],
            rx[0], rx[1], ry[0], ry[1], rx2[0], rx2[1], ry2[0], ry2[1],
            v['_hi'], 1 if v['full'] else 0, v['b1'], v['bplam'], 1 if v['clip'] else 0, 0,
            clip[0], clip[1], clip[2], clip[3], ui, len(v['ucop']), 0))
        for u in v['ucop']: ucops.append(u)
        ui += len(v['ucop']); vpi += 1
H.write('};\n')
H.write('static const int16_t UCOP[][3] = {\n' + ''.join('  {%d,%d,%d},\n' % u for u in ucops) + '  {0,0,0}};\n')
H.write('static const struct sscene SC[] = {\n')
nr = 0
for si, sc in enumerate(scenes):
    CURSC = si
    sc['_r'] = nr
    for phase in range(3):
        for field in range(2 if sc['vlace'] else 1):
            for (rect, hv) in show(sc, phase, field):
                rects.append((phase, field) + rect)
                E.write('scene %d phase %d field %d vp %d %d\n' % (si, phase, field, rect[0], hv))
                nr += 1
    H.write('  {%d,%d,%d,%d,%d,%d,%d,%d,%d,%d},\n' % (sc['chip'], 1 if sc['pal'] else 0, sc['vex'], 1 if sc['vlace'] else 0,
            sc['vx'], sc['vy'], len(sc['vps']), sc['_vp'], sc['_r'], nr - sc['_r']))
H.write('};\n')
H.write('static const int16_t RECT[][7] = {\n' + ''.join('  {%d,%d,%d,%d,%d,%d,%d},\n' % r for r in rects) + '  {0}};\n')
H.close(); E.close()
with open(exp, 'a') as E2:
    for c in range(3):
        for p in range(2):
            E2.write('misc chip %d pal %d done\n' % (c, p))
    for c in range(3):
        E2.write('fetch chip %d done\n' % c)
