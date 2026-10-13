/* SPDX-License-Identifier: MIT */
/* OpenGfx's regions: graphics.library's NewRegion, DisposeRegion,
 * ClearRegion and the And/Or/Xor/Clear Rect and Region calls, in plain C.
 * Copyright (c) 2026 Dalsin Limited. MIT.
 *
 * Clean-room, from the autodocs and measurements of OS 3.2
 * (tests/RegionABITest.c). The structures have graphics/regions.h's
 * layout on the 68k: a region is its bounds and a list of rectangles whose
 * coordinates are relative to bounds.MinX and bounds.MinY.
 *
 * A region is kept in bands: rows of equal height, top to bottom, each a
 * run of rectangles left to right that neither overlap nor touch, and two
 * bands that touch never hold the same runs. Every call builds its answer
 * whole before it changes the destination, so a call that runs out of
 * memory answers 0 and leaves it as it was. Pure: any core may run it
 * (plan section 5). Runs on a 68000. */
#ifndef OPENGFX_REGION_H
#define OPENGFX_REGION_H

#include <stdint.h>

struct gfx_rect {
    int16_t min_x, min_y, max_x, max_y;
};

struct gfx_rrect {
    struct gfx_rrect *next, *prev;
    struct gfx_rect bounds;
};

struct gfx_region {
    struct gfx_rect bounds;
    struct gfx_rrect *rects;
};

/* Memory for rectangles and work space; free gets the size alloc was
 * given. On the Amiga these are AllocMem and FreeMem. */
struct gfx_rgn_mem {
    void *(*alloc)(void *ctx, uint32_t size);
    void (*free)(void *ctx, void *p, uint32_t size);
    void *ctx;
};

enum { GFX_RGN_OR, GFX_RGN_AND, GFX_RGN_XOR, GFX_RGN_CLEAR };

/* An empty region: no rectangles and bounds of 0. */
void gfx_rgn_init(struct gfx_region *r);
/* Frees every rectangle; the region is then empty. */
void gfx_rgn_clear(struct gfx_region *r, const struct gfx_rgn_mem *m);

/* dst = dst op rect. A rectangle with MaxX < MinX or MaxY < MinY is empty.
 * Answers 1, or 0 with dst as it was when memory ran out. */
int gfx_rgn_rect(struct gfx_region *dst, int op, const struct gfx_rect *rect, const struct gfx_rgn_mem *m);
/* dst = src op dst (OR, AND or XOR); src is unchanged and may be dst. */
int gfx_rgn_region(struct gfx_region *dst, int op, const struct gfx_region *src, const struct gfx_rgn_mem *m);

#endif
