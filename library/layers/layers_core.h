/* SPDX-License-Identifier: MIT */
/* OpenLayers' core: layers.library's geometry in plain C. Copyright (c)
 * 2026 Dalsin Limited. MIT.
 *
 * Clean-room, from the autodocs, the NDK 3.2 headers' public structures and
 * measurements of OS 3.2. The core knows nothing of Layer, ClipRect or
 * Layer_Info: the library glue hands it each layer's bounds, front to back,
 * and turns what comes back into ClipRect lists, damage regions and blits.
 *
 *  - rectangle sets (disjoint rectangles, as a Region's): subtract, and, or;
 *  - a layer's pieces: its bounds cut into rectangles each wholly visible,
 *    wholly behind one layer (the frontmost covering it: a ClipRect's lobs)
 *    or wholly off the Layer_Info's bounds;
 *  - WhichLayer;
 *  - an order for copying rectangles that move by one offset so that no
 *    copy writes over a rectangle not yet copied (SortLayerCR).
 *
 * Damage is set arithmetic on these: what a layer shows now less what it
 * still has (its old visible part for simple refresh, its whole old extent
 * for smart refresh, everything for a super bitmap). The area behind every
 * layer (the Layer_Info backfill hook's) is the bounds less every shown
 * layer.
 *
 * Every rectangle is inclusive (x0 <= x1, y0 <= y1), as graphics' Rectangle.
 * Every call is pure: it touches only what it is given, memory comes from
 * the caller's allocator, so any core may run it (plan section 5). Runs on
 * a real 68000 and up with no FPU. */
#ifndef OPENLAYERS_CORE_H
#define OPENLAYERS_CORE_H

#include <stdint.h>

struct lc_rect {
    int32_t x0, y0, x1, y1;
};

/* Where the core's memory comes from: AllocVec or a pool in the library,
 * a static arena in the tests. alloc answers 0 when there is none. */
struct lc_mem {
    void *(*alloc)(void *ctx, uint32_t size);
    void (*free)(void *ctx, void *p, uint32_t size);
    void *ctx;
};

/* A set of disjoint rectangles in no particular order. */
struct lc_rects {
    struct lc_rect *r;
    uint32_t n, cap;
    const struct lc_mem *mem;
};

/* Calls that may allocate answer 0, or -1 when memory ran out; the set is
 * then still a valid set of disjoint rectangles, though not the answer. */
void lc_init(struct lc_rects *s, const struct lc_mem *mem);
void lc_free(struct lc_rects *s);
void lc_clear(struct lc_rects *s);
int lc_set(struct lc_rects *s, const struct lc_rect *r);      /* s = r */
int lc_copy(struct lc_rects *dst, const struct lc_rects *src);
int lc_sub_rect(struct lc_rects *s, const struct lc_rect *r); /* s -= r */
int lc_sub_rect_x(struct lc_rects *s, const struct lc_rect *r); /* s -= r, its pieces cut columns first */
void lc_and_rect(struct lc_rects *s, const struct lc_rect *r);/* s &= r, never allocates */
int lc_or_rect(struct lc_rects *s, const struct lc_rect *r);  /* s |= r */
int lc_sub(struct lc_rects *s, const struct lc_rects *t);     /* s -= t */
int lc_and(struct lc_rects *s, const struct lc_rects *t);     /* s &= t */
int lc_or(struct lc_rects *s, const struct lc_rects *t);      /* s |= t */
void lc_offset(struct lc_rects *s, int32_t dx, int32_t dy);
uint32_t lc_area(const struct lc_rects *s);

/* Whether two rectangles share a pixel, and the shared part. */
int lc_meet(const struct lc_rect *a, const struct lc_rect *b, struct lc_rect *out);

/* A layer as the core sees it: its bounds on the Layer_Info's bitmap, and
 * whether it is hidden (HideLayer: it covers nothing). */
enum { LC_HIDDEN = 1 };
struct lc_layer {
    struct lc_rect bounds;
    uint32_t flags;
};

/* A piece of a layer: visible, behind layer stack[obscurer] (the frontmost
 * shown layer in front of it covering it), or outside the Layer_Info's
 * bounds (all of a hidden layer is one such piece). */
enum { LC_VISIBLE = -1, LC_OFFSCREEN = -2 };
struct lc_piece {
    struct lc_rect r;
    int32_t obscurer;
};
struct lc_pieces {
    struct lc_piece *p;
    uint32_t n, cap;
    const struct lc_mem *mem;
};
void lc_pieces_init(struct lc_pieces *s, const struct lc_mem *mem);
void lc_pieces_free(struct lc_pieces *s);

/* stack[0] is the frontmost layer. The pieces of stack[idx] cover its
 * bounds exactly, each pixel once. screen: the Layer_Info's bounds, or 0
 * for none (nothing is off). A hidden layer is one piece, LC_OFFSCREEN. */
int lc_layer_pieces(const struct lc_layer *stack, uint32_t n, uint32_t idx, const struct lc_rect *screen,
                    struct lc_pieces *out);

/* The visible part of stack[idx], as a set. */
int lc_layer_visible(const struct lc_layer *stack, uint32_t n, uint32_t idx, const struct lc_rect *screen,
                     struct lc_rects *out);

/* The part of screen no shown layer covers: where the backfill hook
 * draws. */
int lc_uncovered(const struct lc_layer *stack, uint32_t n, const struct lc_rect *screen, struct lc_rects *out);

/* WhichLayer: the index of the frontmost shown layer with (x, y) in its
 * bounds, or -1. */
int32_t lc_which(const struct lc_layer *stack, uint32_t n, int32_t x, int32_t y);

/* Reorders r[0..n-1] (disjoint) so that copying each in turn to itself
 * moved by (dx, dy) never writes over one still to be copied. Such an order
 * always exists for disjoint rectangles; answers how many could not be
 * placed (0). */
uint32_t lc_sort_for_move(struct lc_rect *r, uint32_t n, int32_t dx, int32_t dy);

#endif
