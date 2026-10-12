/* SPDX-License-Identifier: MIT */
#ifndef OGFX_LEAVES_H
#define OGFX_LEAVES_H
/* OpenGfx's leaves (SPD-11, first slice): graphics.library's drawing work
 * on a bitmap's own memory, as magic functions (ac_magic.h).
 * On AmigaChrome's AC090 a call runs as host code; on a real Amiga, a
 * PiStorm or another emulator the C in ogfx_leaves.c runs. Each takes one
 * parameter block (32-bit fields, the Amiga's byte order) and returns how
 * much it drew, 0 when the block asks for nothing it can do.
 *
 * The leaves know nothing of RastPorts, layers or clipping: the 68k glue
 * (ogfx_lib.c, the patches of graphics.library) cuts a
 * call into rectangles the leaves can take, and calls graphics.library's
 * own code for anything else.
 *
 * Coordinates are inclusive (x0 <= x1, y0 <= y1) and never negative; a
 * block that breaks that draws nothing. Arithmetic is 32-bit, as the 68k's.
 * The C bodies here are the reference: AC090's host versions must give the
 * same memory and the same result for every block, and AC090_MAGIC=verify
 * checks it call by call. */
#include <stdint.h>

/* RectFill on a planar bitmap (AGA and older chip RAM displays). For each
 * plane p below depth whose mask bit is set, and whose pointer is neither
 * 0 nor -1 (graphics.library's all-zero and all-one planes): mode 0 sets
 * the rectangle's bits to pen's bit p (JAM1 and JAM2 as RectFill uses
 * them), mode 1 inverts them (COMPLEMENT). Bit 7 of a byte is the leftmost
 * pixel. Returns the planes drawn. */
struct ogfx_planar_rect {
    uint32_t planes;           /* address of depth plane pointers (a BitMap's Planes[]) */
    uint32_t bytes_per_row;
    uint32_t depth;            /* 1 to 8 */
    int32_t x0, y0, x1, y1;
    uint32_t pen;              /* bit p: plane p's value */
    uint32_t mask;             /* bit p: plane p is drawn (a RastPort's Mask) */
    uint32_t mode;             /* 0 set from pen, 1 invert */
};

/* RectFill on a chunky bitmap (RTG: Picasso96 and ACRTG), 1 to 4 bytes a
 * pixel. color is the pixel's value as its bytes lie in memory, the first
 * byte highest (so 0x00RRGGBB for 3 bytes, 0xAARRGGBB for 4). Mode 0
 * stores it, mode 1 XORs it in. Returns the pixels drawn. */
struct ogfx_chunky_rect {
    uint32_t base;             /* the bitmap's first byte */
    uint32_t bytes_per_row;
    uint32_t bytes_per_pixel;  /* 1 to 4 */
    int32_t x0, y0, x1, y1;
    uint32_t color;
    uint32_t mode;             /* 0 store, 1 XOR */
};

/* A rectangle copied between chunky bitmaps, or within one (BltBitMap's
 * plain copy, ScrollRaster). Each row moves as memmove does. When the
 * destination's first byte is above the source's, rows go from the last up,
 * so a rectangle moved down or right within one bitmap comes out whole.
 * Returns the pixels copied. */
struct ogfx_chunky_copy {
    uint32_t src, src_bytes_per_row;
    uint32_t dst, dst_bytes_per_row;
    uint32_t bytes_per_pixel;  /* 1 to 4 */
    int32_t sx, sy, dx, dy, width, height;
};

/* BltBitMap on standard planar bitmaps. Channel A is considered set inside
 * the rectangle, channel B is source and channel C is destination, matching
 * graphics.library's minterm convention. Only the high nibble of minterm is
 * therefore significant. Source planes may be 0 or -1 (constant zero/one);
 * destination 0/-1 planes are skipped. same_bitmap tells the reference which
 * direction is safe when source and destination rectangles overlap. Returns
 * the number of destination planes involved. */
struct ogfx_planar_blit {
    uint32_t src_planes;       /* address of source BitMap.Planes[] */
    uint32_t src_bytes_per_row;
    uint32_t dst_planes;       /* address of destination BitMap.Planes[] */
    uint32_t dst_bytes_per_row;
    uint32_t depth;            /* min(source depth, destination depth), 1..8 */
    int32_t sx, sy, dx, dy, width, height;
    uint32_t minterm;
    uint32_t mask;
    uint32_t same_bitmap;      /* non-zero only when src and dst are same BitMap */
};

uint32_t ogfx_planar_rect(const struct ogfx_planar_rect *r);
uint32_t ogfx_chunky_rect(const struct ogfx_chunky_rect *r);
uint32_t ogfx_chunky_copy(const struct ogfx_chunky_copy *r);
uint32_t ogfx_planar_blit(const struct ogfx_planar_blit *r);

#endif
