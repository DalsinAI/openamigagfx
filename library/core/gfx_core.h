/* SPDX-License-Identifier: MIT */
/* OpenGfx's core: graphics.library's drawing on a bitmap's own memory, in
 * plain C, planar (OCS/ECS/AGA) and chunky (RTG). Copyright (c) 2026
 * Dalsin Limited. MIT.
 *
 * Clean-room, from the autodocs and measurements of OS 3.2. The core knows
 * nothing of RastPorts, layers or clipping: the library glue reads a
 * RastPort's pens, mode and mask, cuts the call into rectangles inside the
 * bitmap and calls these. Every coordinate here is already inside the
 * bitmap; x1 >= x0 and y1 >= y0.
 *
 * Runs on a real 68000 and up with no FPU and no OpenGPU; on AmigaChrome
 * the same calls can become AC090 leaves (ogfx_leaves.h) or OpenGPU work,
 * giving the same bytes. Every call is pure: it touches only the bitmap
 * memory it is given, so any core may run it (plan section 5). */
#ifndef OPENGFX_CORE_H
#define OPENGFX_CORE_H

#include <stdint.h>

/* A planar bitmap: depth planes of rows rows, bytes_per_row bytes each,
 * bit 7 of a byte the leftmost pixel. A plane pointer of 0 reads as all
 * zeros and one of -1 as all ones; neither is written (graphics.library's
 * convention). */
struct gfx_planar {
    uint8_t *planes[8];
    uint32_t bytes_per_row;
    uint32_t rows;
    uint32_t depth;            /* 1 to 8 */
};

/* A chunky bitmap, one byte a pixel (CLUT): the RTG screens a pen means
 * something on. */
struct gfx_chunky8 {
    uint8_t *base;
    uint32_t bytes_per_row;
    uint32_t rows;
};

/* What a draw does to a pixel's planes: set them to pen's bits, or invert
 * them (COMPLEMENT). mask: bit p, plane p is drawn (a RastPort's Mask). */
enum { GFX_SET = 0, GFX_INVERT = 1 };

/* One pixel. */
void gfx_planar_put(const struct gfx_planar *bm, int32_t x, int32_t y, uint32_t pen, uint32_t mask, int mode);
uint32_t gfx_planar_get(const struct gfx_planar *bm, int32_t x, int32_t y);

/* Every pixel of the bitmap to pen (SetRast), planes in mask. */
void gfx_planar_fill_all(const struct gfx_planar *bm, uint32_t pen, uint32_t mask);

/* Pens from a byte array into a rectangle (WritePixelArray8,
 * WriteChunkyPixels): pixel (x, y) takes src[(y - y0) * src_bpr + x - x0],
 * planes in mask. Eight pixels at a time where a byte of every plane is
 * covered. */
void gfx_planar_write8(const struct gfx_planar *bm, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                       const uint8_t *src, uint32_t src_bpr, uint32_t mask);
/* The rectangle's pens into a byte array (ReadPixelArray8). */
void gfx_planar_read8(const struct gfx_planar *bm, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                      uint8_t *dst, uint32_t dst_bpr);

/* The same on a chunky bitmap: a pen is the byte; mask ANDs which bits are
 * written, as on a planar screen. */
void gfx_chunky8_put(const struct gfx_chunky8 *bm, int32_t x, int32_t y, uint32_t pen, uint32_t mask, int mode);
uint32_t gfx_chunky8_get(const struct gfx_chunky8 *bm, int32_t x, int32_t y);
void gfx_chunky8_fill_all(const struct gfx_chunky8 *bm, uint32_t pen, uint32_t mask);
void gfx_chunky8_write8(const struct gfx_chunky8 *bm, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                        const uint8_t *src, uint32_t src_bpr, uint32_t mask);
void gfx_chunky8_read8(const struct gfx_chunky8 *bm, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                       uint8_t *dst, uint32_t dst_bpr);

#endif
