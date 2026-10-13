// SPDX-License-Identifier: MIT
// OpenGfx text: the ROM fonts' data (topaz_data.cpp, made by mkfont.py).
// Copyright (c) 2026 Dalsin Limited. MIT.
#ifndef OPENGFX_TOPAZ_DATA_H
#define OPENGFX_TOPAZ_DATA_H

#include <stdint.h>

// What a ROM TextFont is made from: its sizes, characters lochar..hichar
// and the default glyph after them, tf_CharData (modulo bytes a row) and
// tf_CharLoc (offset, width per glyph). Fixed width, no CharSpace or
// CharKern; tf_BoldSmear 1.
struct gt_rom_font {
    uint16_t ysize, xsize, baseline;
    uint8_t lochar, hichar;
    uint16_t modulo;
    const uint8_t *data;
    const uint16_t *loc;
};

extern "C" const char gt_topaz_name[];
extern "C" const gt_rom_font gt_topaz8, gt_topaz9;

#endif
