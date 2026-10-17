#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# OpenGfx text: builds topaz_data.cpp, the ROM fonts topaz 8 and topaz 9,
# from romfont8.txt and romfont9.txt. Copyright (c) 2026 AmigaChrome Team. MIT.
#
# The glyphs are OpenGfx's own drawing: romfont8.txt (8 x 8 cells, baseline
# row 6) and romfont9.txt (10 x 9 cells, baseline row 7), each drawn by hand
# for this ROM, not taken from any other font. The Latin-1 accented letters
# are composed here: a mark from the file's "accent.NAME" glyphs above a
# short capital ("cap.X", the capital drawn again two rows lower) or above
# the small letter's own rows (the dotless i for i).
#   python3 library/text/mkfont.py > library/text/topaz_data.cpp
#   python3 library/text/mkfont.py --sheet 8|9 > sheet.txt   (every glyph, as rows)
# tests/m68k/test_gfx_text.sh imports this to check the data.
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LO, HI = 32, 255

# (xsize, ysize, baseline) of each size, as the ROM's topaz 8 and 9 have them
SIZES = {8: (8, 8, 6), 9: (10, 9, 7)}

# Latin-1 code: (base letter, accent)
COMPOSED = {}
for base, codes in (('A', 'C0 grave C1 acute C2 circ C3 tilde C4 diaer'),
                    ('E', 'C8 grave C9 acute CA circ CB diaer'),
                    ('I', 'CC grave CD acute CE circ CF diaer'),
                    ('N', 'D1 tilde'),
                    ('O', 'D2 grave D3 acute D4 circ D5 tilde D6 diaer'),
                    ('U', 'D9 grave DA acute DB circ DC diaer'),
                    ('Y', 'DD acute'),
                    ('a', 'E0 grave E1 acute E2 circ E3 tilde E4 diaer'),
                    ('e', 'E8 grave E9 acute EA circ EB diaer'),
                    ('i', 'EC grave ED acute EE circ EF diaer'),
                    ('n', 'F1 tilde'),
                    ('o', 'F2 grave F3 acute F4 circ F5 tilde F6 diaer'),
                    ('u', 'F9 grave FA acute FB circ FC diaer'),
                    ('y', 'FD acute FF diaer')):
    w = codes.split()
    for i in range(0, len(w), 2):
        COMPOSED[int(w[i], 16)] = (base, w[i + 1])


def parse(size):
    """{key: [rows of '#'/'.']} as drawn in romfontSIZE.txt; key a code, 'default', 'accent.x', 'cap.X' or 'dotless.i'."""
    xs, ys, _ = SIZES[size]
    glyphs, cur = {}, None
    for line in open(os.path.join(HERE, 'romfont%d.txt' % size), encoding='ascii'):
        line = line.rstrip('\n')
        if line.startswith('#') and (len(line) < 2 or line[1] == ' '):
            continue
        if line.startswith('@ '):
            key = line[2:]
            if key.startswith('0x'):
                cur = int(key, 16)
            elif len(key) == 1:
                cur = ord(key)
            else:
                assert key == 'default' or key.split('.')[0] in ('accent', 'cap', 'dotless'), key
                cur = key
            assert cur not in glyphs, key
            glyphs[cur] = []
        elif line:
            assert cur is not None and len(line) == xs and set(line) <= set('#.'), (size, cur, line)
            glyphs[cur].append(line)
    caprows = ys - 3                       # a short capital: rows 2 .. baseline
    for k, v in glyphs.items():
        want = 2 if str(k).startswith('accent.') else caprows if str(k).startswith('cap.') else ys
        assert len(v) == want, (size, k, len(v))
    return glyphs


def glyphs(size):
    """Every glyph of topaz SIZE, 32..255 and 'default': ysize rows of xsize; None for the default glyph's codes."""
    xs, ys, base = SIZES[size]
    g = parse(size)
    out = {}
    for c in range(LO, HI + 1):
        if c in g:
            out[c] = g[c]
        elif c in COMPOSED:
            letter, acc = COMPOSED[c]
            if letter.isupper():
                body = g['cap.' + letter] + ['.' * xs] * (ys - 1 - base)
            else:
                body = (g['dotless.i'] if letter == 'i' else g[ord(letter)])[2:]
            out[c] = g['accent.' + acc] + body
        elif 127 <= c <= 159:
            out[c] = None                               # the default glyph
        else:
            raise SystemExit('topaz %d: no glyph for %d' % (size, c))
    out['default'] = g['default']
    return out


def font(size):
    """(xsize, ysize, baseline, glyphs) for topaz 8 or 9."""
    xs, ys, base = SIZES[size]
    return xs, ys, base, glyphs(size)


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
    w('// OpenGfx text: the ROM fonts topaz 8 and topaz 9, OpenGfx\'s own drawing\n')
    w('// (romfont8.txt, romfont9.txt). Copyright (c) 2026 AmigaChrome Team. MIT.\n')
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


def sheet(size, out):
    """Every glyph of the size as rows, 16 to a line, for reading the whole font at once."""
    xs, ys, base, g = font(size)
    codes = [c for c in range(LO, HI + 1) if g[c] is not None]
    for i in range(0, len(codes), 16):
        line = codes[i:i + 16]
        out.write('  '.join('%-*s' % (xs, '%02X' % c) for c in line) + '\n')
        for r in range(ys):
            out.write('  '.join(g[c][r] for c in line) + '\n')
        out.write('\n')


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--sheet':
        sheet(int(sys.argv[2]), sys.stdout)
    else:
        emit(sys.stdout)
