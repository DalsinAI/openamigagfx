#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# OpenGfx text: builds topaz_data.cpp, the ROM fonts topaz 8 and topaz 9,
# from topaz8.txt. Copyright (c) 2026 AmigaChrome Team. MIT.
#
# The glyphs are OpenGfx's own drawing (topaz8.txt), not ROM bytes. Topaz 8
# is that drawing; the Latin-1 accented letters are composed here from a
# base letter and an accent; topaz 9 is topaz 8 widened to 10 columns and
# deepened to 9 rows (columns 3 and 4 and row 3 drawn twice).
#   python3 library/text/mkfont.py > library/text/topaz_data.cpp
# tests/m68k/test_gfx_text.sh imports this to check the data.
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LO, HI = 32, 255

ACCENTS = {
    'grave': ['.##.....', '..##....'],
    'acute': ['...##...', '..##....'],
    'circ': ['..##....', '.##.##..'],
    'tilde': ['.###.##.', '##.###..'],
    'diaer': ['.##.##..', '........'],
    'ring': ['..##....', '.#..#...'],
}
# Latin-1 code: (base letter, accent)
COMPOSED = {}
for base, codes in (('A', 'C0 grave C1 acute C2 circ C3 tilde C4 diaer C5 ring'),
                    ('E', 'C8 grave C9 acute CA circ CB diaer'),
                    ('I', 'CC grave CD acute CE circ CF diaer'),
                    ('N', 'D1 tilde'),
                    ('O', 'D2 grave D3 acute D4 circ D5 tilde D6 diaer'),
                    ('U', 'D9 grave DA acute DB circ DC diaer'),
                    ('Y', 'DD acute'),
                    ('a', 'E0 grave E1 acute E2 circ E3 tilde E4 diaer E5 ring'),
                    ('e', 'E8 grave E9 acute EA circ EB diaer'),
                    ('i', 'EC grave ED acute EE circ EF diaer'),
                    ('n', 'F1 tilde'),
                    ('o', 'F2 grave F3 acute F4 circ F5 tilde F6 diaer'),
                    ('u', 'F9 grave FA acute FB circ FC diaer'),
                    ('y', 'FD acute FF diaer')):
    w = codes.split()
    for i in range(0, len(w), 2):
        COMPOSED[int(w[i], 16)] = (base, w[i + 1])
CEDILLA = {0xC7: 'C', 0xE7: 'c'}


def parse(path=os.path.join(HERE, 'topaz8.txt')):
    """{code or 'default': [8 rows of '#'/'.']} as drawn."""
    glyphs, cur = {}, None
    for line in open(path, encoding='ascii'):
        line = line.rstrip('\n')
        if line.startswith('# ') or line == '#':
            continue
        if line.startswith('@ '):
            key = line[2:]
            if key == 'default':
                cur = 'default'
            elif key.startswith('0x'):
                cur = int(key, 16)
            else:
                assert len(key) == 1, key
                cur = ord(key)
            assert cur not in glyphs, key
            glyphs[cur] = []
        elif line:
            assert len(line) == 8 and set(line) <= set('#.'), line
            glyphs[cur].append(line)
    for k, v in glyphs.items():
        assert len(v) == 8, (k, v)
    return glyphs


def topaz8():
    """Every glyph of topaz 8, 32..255 and 'default': 8 rows of 8."""
    g = parse()
    out = {}
    for c in range(LO, HI + 1):
        if c in g:
            out[c] = g[c]
        elif c in COMPOSED:
            base, acc = COMPOSED[c]
            b = g[ord(base)]
            if base.isupper():
                rows = [b[0], b[2], b[3], b[4], b[6]]   # the capital in rows 2..6
            else:
                rows = b[2:7]
            out[c] = ACCENTS[acc] + rows + [b[7]]
        elif c in CEDILLA:
            out[c] = g[ord(CEDILLA[c])][:7] + ['..##....']
        elif 127 <= c <= 159:
            out[c] = None                               # the default glyph
        else:
            raise SystemExit('no glyph for %d' % c)
    out['default'] = g['default']
    return out


def widen(rows):
    """topaz 9 from topaz 8: columns 3 and 4 and row 3 twice."""
    cols = [0, 1, 2, 3, 3, 4, 4, 5, 6, 7]
    wide = [''.join(r[i] for i in cols) for r in rows]
    return wide[:4] + [wide[3]] + wide[4:]


def font(size):
    """(xsize, ysize, baseline, glyphs) for topaz 8 or 9."""
    g = topaz8()
    if size == 8:
        return 8, 8, 6, g
    return 10, 9, 7, {k: (widen(v) if v else None) for k, v in g.items()}


def build(size):
    """(modulo, data bytes, charloc pairs) as the TextFont has them."""
    xs, ys, base, g = font(size)
    order = list(range(LO, HI + 1)) + ['default']
    images = [k for k in order if g[k] is not None]
    nbits = len(images) * xs
    modulo = (nbits + 15) // 16 * 2
    at = {k: i * xs for i, k in enumerate(images)}
    data = bytearray(modulo * ys)
    for k in images:
        for r, row in enumerate(g[k]):
            for x, ch in enumerate(row):
                if ch == '#':
                    bit = at[k] + x
                    data[r * modulo + bit // 8] |= 0x80 >> (bit % 8)
    loc = [(at[k] if g[k] is not None else at['default'], xs) for k in order]
    return modulo, bytes(data), loc


def emit(out):
    w = out.write
    w('// SPDX-License-Identifier: MIT\n')
    w('// OpenGfx text: the ROM fonts topaz 8 and topaz 9, redrawn for OpenGfx\n')
    w('// (topaz8.txt). Copyright (c) 2026 AmigaChrome Team. MIT.\n')
    w('// Made by mkfont.py; do not edit.\n')
    w('#include "topaz_data.h"\n\n')
    w('extern "C" const char gt_topaz_name[] = "topaz.font";\n\n')
    for size in (8, 9):
        xs, ys, base, _ = font(size)
        modulo, data, loc = build(size)
        w('static const uint8_t topaz%d_data[%d] = {' % (size, len(data)))
        for i, b in enumerate(data):
            w(('\n    ' if i % 16 == 0 else ' ') + '0x%02x,' % b)
        w('\n};\n')
        w('static const uint16_t topaz%d_loc[%d] = {' % (size, 2 * len(loc)))
        for i, (o, n) in enumerate(loc):
            w(('\n    ' if i % 8 == 0 else ' ') + '%d, %d,' % (o, n))
        w('\n};\n')
        w('extern "C" const gt_rom_font gt_topaz%d = {%d, %d, %d, %d, %d, %d, topaz%d_data, topaz%d_loc};\n\n'
          % (size, ys, xs, base, LO, HI, modulo, size, size))


if __name__ == '__main__':
    emit(sys.stdout)
