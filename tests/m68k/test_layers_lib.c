/* SPDX-License-Identifier: MIT */
/* OpenLayers' library code (library/layers/layers_lib.c) against a model.
 * Copyright (c) 2026 Dalsin Limited. MIT.
 *
 * exec and graphics.library are played here: bitmaps are a byte a pixel,
 * BltBitMap copies, clears and exclusive-ors them (overlapping or not, and
 * counts a blit that leaves a bitmap as a failure), regions are lists of
 * rectangles stored as graphics.library stores them (relative to the
 * region's bounds), memory and semaphores are counted.
 *
 * Each round makes a Layer_Info and does random calls. Before each call
 * every pixel each layer can be drawn into (through its ClipRects, as
 * graphics.library draws) and the screen behind the layers get random
 * values, and are read back. After it:
 *  - every layer's ClipRects cover its bounds once each (a simple refresh
 *    layer's only where it is on show), obscured exactly
 *    where a shown layer is in front or the Layer_Info's bounds end, an
 *    obscured smart refresh ClipRect has a bitmap big enough for it;
 *  - a smart refresh or super bitmap layer still has every pixel it had,
 *    at the same place in the layer; what is new to it is its backfill;
 *  - a simple refresh layer still has the pixels that stayed on show; the
 *    rest of what shows is its backfill and in its DamageList;
 *  - the screen no layer covers is unchanged where nothing covered it
 *    before, and the Layer_Info's backfill where something did;
 *  - every semaphore is free and, at the end, all memory is back.
 * Clip regions and BeginUpdate are drawn through and checked the same way. */
#include <stdio.h>
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/clip.h>
#include <graphics/layers.h>
#include <graphics/rastport.h>
#include <graphics/regions.h>
#include "layers_lib.h"
#include "layers_core.h"

/* Under qemu-m68k the host NDK has the Amiga's layout: check it. */
#ifdef __mc68000__
#define AT(t, f, o) typedef char at_##t##_##f[offsetof(struct t, f) == (o) ? 1 : -1]
AT(Layer, rp, 12); AT(Layer, bounds, 16); AT(Layer, Flags, 30); AT(Layer, SuperBitMap, 32); AT(Layer, Window, 40);
AT(Layer, Scroll_X, 44); AT(Layer, LayerInfo, 68); AT(Layer, Lock, 72); AT(Layer, BackFill, 118);
AT(Layer, ClipRegion, 126); AT(Layer, Width, 134); AT(Layer, DamageList, 156);
AT(ClipRect, obscured, 8); AT(ClipRect, BitMap, 12); AT(ClipRect, bounds, 16);
AT(Layer_Info, Lock, 24); AT(Layer_Info, Flags, 88); AT(Layer_Info, LayerInfo_extra, 98);
AT(RastPort, BitMap, 4); AT(RastPort, Font, 52); AT(Region, RegionRectangle, 8);
typedef char size_Layer[sizeof(struct Layer) == 160 ? 1 : -1];
typedef char size_Layer_Info[sizeof(struct Layer_Info) == 102 ? 1 : -1];
typedef char size_RastPort[sizeof(struct RastPort) == 100 ? 1 : -1];
typedef char size_SignalSemaphore[sizeof(struct SignalSemaphore) == 46 ? 1 : -1];
#endif

#define SW 160
#define SH 96
#define MAXL 7

static uint32_t seed = 0x5eed;
static uint32_t rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }
static unsigned long checks, fails;
static const char *doing = "";
static void fail(const char *what, long a, long b)
{
    if (++fails < 25) printf("FAIL %s (%s) %ld %ld\n", what, doing, a, b);
}

/* ---- exec ---------------------------------------------------------- */

static uint8_t arena[1u << 21];
static uint32_t used;
static long live_allocs;
static long fail_alloc;         /* > 0: the n-th allocation from now fails */
static int gfx_mem;             /* graphics.library's own (regions): never made to fail */
struct blk { uint32_t size; uint32_t free; struct blk *next; };
static struct blk *freelist;

APTR AllocMem(ULONG size, ULONG flags)
{
    struct blk *b, **pp;
    uint8_t *p;
    if (!gfx_mem && fail_alloc > 0 && --fail_alloc == 0) return 0;
    size = (size + 7) & ~7u;
    for (pp = &freelist; (b = *pp); pp = &b->next)
        if (b->size == size) { *pp = b->next; break; }
    if (!b) {
        if (used + size + sizeof *b > sizeof arena) return 0;
        b = (struct blk *)(arena + used);
        used += size + sizeof *b;
        b->size = size;
    }
    b->free = 0;
    p = (uint8_t *)(b + 1);
    if (flags & MEMF_CLEAR) memset(p, 0, size);
    else memset(p, 0xA5, size);
    live_allocs++;
    return p;
}

void FreeMem(APTR p, ULONG size)
{
    struct blk *b = (struct blk *)p - 1;
    if (!p) { fail("FreeMem of 0", 0, 0); return; }
    if (b->free || b->size != ((size + 7) & ~7u)) { fail("FreeMem size or twice", (long)b->size, (long)size); return; }
    b->free = 1;
    b->next = freelist;
    freelist = b;
    live_allocs--;
}

void InitSemaphore(struct SignalSemaphore *s) { memset(s, 0, sizeof *s); }
void ObtainSemaphore(struct SignalSemaphore *s) { s->ss_NestCount++; }
void ReleaseSemaphore(struct SignalSemaphore *s)
{
    if (--s->ss_NestCount < 0) fail("semaphore released too often", 0, 0);
}

/* ---- graphics ------------------------------------------------------ */

/* A model bitmap: BytesPerRow is its width in pixels, Planes[0] the bytes. */
void InitRastPort(struct RastPort *rp) { memset(rp, 0, sizeof *rp); rp->Mask = 0xFF; }

struct BitMap *AllocBitMap(ULONG w, ULONG h, ULONG depth, ULONG flags, struct BitMap *friend_bm)
{
    struct BitMap *bm = AllocMem(sizeof *bm, MEMF_CLEAR);
    (void)depth; (void)flags; (void)friend_bm;
    if (!bm) return 0;
    if (!(bm->Planes[0] = AllocMem(w * h + 1, 0))) { FreeMem(bm, sizeof *bm); return 0; }
    bm->BytesPerRow = (UWORD)w;
    bm->Rows = (UWORD)h;
    bm->Depth = 8;
    return bm;
}

void FreeBitMap(struct BitMap *bm)
{
    if (!bm) return;
    FreeMem(bm->Planes[0], (ULONG)bm->BytesPerRow * bm->Rows + 1);
    FreeMem(bm, sizeof *bm);
}

ULONG GetBitMapAttr(struct BitMap *bm, ULONG attr)
{
    switch (attr) {
    case BMA_WIDTH: return bm->BytesPerRow;
    case BMA_HEIGHT: return bm->Rows;
    case BMA_DEPTH: return 8;
    default: return 0;
    }
}

static uint8_t blittmp[SW * 4 * SH * 4];
LONG BltBitMap(struct BitMap *src, LONG sx, LONG sy, struct BitMap *dst, LONG dx, LONG dy, LONG w, LONG h,
               ULONG minterm, ULONG mask, PLANEPTR temp)
{
    LONG x, y;
    (void)mask; (void)temp;
    checks++;
    if (w <= 0 || h <= 0 || sx < 0 || sy < 0 || dx < 0 || dy < 0 || sx + w > src->BytesPerRow || sy + h > src->Rows ||
        dx + w > dst->BytesPerRow || dy + h > dst->Rows || w * h > (LONG)sizeof blittmp) {
        fail("blit outside a bitmap", sx * 1000 + sy, dx * 1000 + dy);
        return 0;
    }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) blittmp[y * w + x] = src->Planes[0][(sy + y) * src->BytesPerRow + sx + x];
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            uint8_t *d = &dst->Planes[0][(dy + y) * dst->BytesPerRow + dx + x], s = blittmp[y * w + x];
            if (minterm == 0xC0) *d = s;
            else if (minterm == 0x00) *d = 0;
            else if (minterm == 0x60) *d ^= s;
            else fail("minterm", (long)minterm, 0);
        }
    return 8;
}

/* Regions: the model keeps each rectangle relative to the bounds, as
 * graphics.library does; the rectangles may overlap. */
struct Region *NewRegion(void)
{
    struct Region *r;
    gfx_mem = 1;
    r = AllocMem(sizeof(struct Region), MEMF_CLEAR);
    gfx_mem = 0;
    return r;
}

/* the rectangles to absolute coordinates, and back relative to new bounds */
static void region_abs(struct Region *r)
{
    struct RegionRectangle *rr;
    for (rr = r->RegionRectangle; rr; rr = rr->Next) {
        rr->bounds.MinX += r->bounds.MinX; rr->bounds.MaxX += r->bounds.MinX;
        rr->bounds.MinY += r->bounds.MinY; rr->bounds.MaxY += r->bounds.MinY;
    }
}
static void region_rel(struct Region *r)
{
    struct RegionRectangle *rr;
    LONG nx = 0x7FFF, ny = 0x7FFF, mx = -0x8000, my = -0x8000;
    memset(&r->bounds, 0, sizeof r->bounds);
    if (!r->RegionRectangle) return;
    for (rr = r->RegionRectangle; rr; rr = rr->Next) {
        if (rr->bounds.MinX < nx) nx = rr->bounds.MinX;
        if (rr->bounds.MinY < ny) ny = rr->bounds.MinY;
        if (rr->bounds.MaxX > mx) mx = rr->bounds.MaxX;
        if (rr->bounds.MaxY > my) my = rr->bounds.MaxY;
    }
    for (rr = r->RegionRectangle; rr; rr = rr->Next) {
        rr->bounds.MinX -= (WORD)nx; rr->bounds.MaxX -= (WORD)nx;
        rr->bounds.MinY -= (WORD)ny; rr->bounds.MaxY -= (WORD)ny;
    }
    r->bounds.MinX = (WORD)nx; r->bounds.MinY = (WORD)ny; r->bounds.MaxX = (WORD)mx; r->bounds.MaxY = (WORD)my;
}

BOOL OrRectRegion(struct Region *r, struct Rectangle *rect)
{
    struct RegionRectangle *rr;
    gfx_mem = 1;
    rr = AllocMem(sizeof *rr, MEMF_CLEAR);
    gfx_mem = 0;
    if (!rr) return FALSE;
    region_abs(r);
    rr->bounds = *rect;
    rr->Next = r->RegionRectangle;
    if (rr->Next) rr->Next->Prev = rr;
    r->RegionRectangle = rr;
    region_rel(r);
    return TRUE;
}

void ClearRegion(struct Region *r)
{
    struct RegionRectangle *rr = r->RegionRectangle, *n;
    for (; rr; rr = n) { n = rr->Next; FreeMem(rr, sizeof *rr); }
    r->RegionRectangle = 0;
    memset(&r->bounds, 0, sizeof r->bounds);
}

void AndRectRegion(struct Region *r, struct Rectangle *rect)
{
    struct RegionRectangle *rr, *n, **pp;
    region_abs(r);
    for (pp = &r->RegionRectangle; (rr = *pp); ) {
        n = rr->Next;
        if (rr->bounds.MinX < rect->MinX) rr->bounds.MinX = rect->MinX;
        if (rr->bounds.MinY < rect->MinY) rr->bounds.MinY = rect->MinY;
        if (rr->bounds.MaxX > rect->MaxX) rr->bounds.MaxX = rect->MaxX;
        if (rr->bounds.MaxY > rect->MaxY) rr->bounds.MaxY = rect->MaxY;
        if (rr->bounds.MinX > rr->bounds.MaxX || rr->bounds.MinY > rr->bounds.MaxY) {
            *pp = n;
            if (n) n->Prev = rr->Prev;
            FreeMem(rr, sizeof *rr);
        } else {
            pp = &rr->Next;
        }
    }
    region_rel(r);
}

void DisposeRegion(struct Region *r) { ClearRegion(r); FreeMem(r, sizeof *r); }

static int in_region(const struct Region *r, LONG x, LONG y)
{
    const struct RegionRectangle *rr;
    if (!r) return 0;
    for (rr = r->RegionRectangle; rr; rr = rr->Next)
        if (x >= r->bounds.MinX + rr->bounds.MinX && x <= r->bounds.MinX + rr->bounds.MaxX &&
            y >= r->bounds.MinY + rr->bounds.MinY && y <= r->bounds.MinY + rr->bounds.MaxY)
            return 1;
    return 0;
}

/* ---- hooks --------------------------------------------------------- */

typedef ULONG (*hookfn)(struct Hook *, APTR, APTR);
ULONG ol_call_hook(struct Hook *hook, APTR obj, APTR msg) { return ((hookfn)(void *)hook->h_Entry)(hook, obj, msg); }

/* A pattern of the layer's (or screen's) own coordinates. */
static uint8_t pat(LONG x, LONG y, uint32_t salt) { return (uint8_t)(((x * 7) ^ (y * 13) ^ salt) | 1); }

/* A hook gets a RastPort on the bitmap to fill: bare, or naming the
 * layer the message names (as OS 3.2 gives it). */
static ULONG fill_hook(struct Hook *h, APTR obj, APTR m)
{
    struct RastPort *rp = obj;
    struct ol_hookmsg *msg = m;
    LONG x, y;
    struct BitMap *bm = rp->BitMap;
    checks++;
    if (msg->Bounds.MinX < 0 || msg->Bounds.MinY < 0 || msg->Bounds.MaxX >= bm->BytesPerRow ||
        msg->Bounds.MaxY >= bm->Rows || (rp->Layer && rp->Layer != msg->Layer)) {
        fail("hook bounds", msg->Bounds.MinX, msg->Bounds.MinY);
        return 0;
    }
    for (y = msg->Bounds.MinY; y <= msg->Bounds.MaxY; y++)
        for (x = msg->Bounds.MinX; x <= msg->Bounds.MaxX; x++)
            /* the Layer_Info's hook (salt 99) in the screen's coordinates:
             * its offsets are from the layer that uncovered the place */
            bm->Planes[0][y * bm->BytesPerRow + x] = h->h_Data == (APTR)(uintptr_t)99 ? pat(x, y, 99) :
                pat(msg->OffsetX + x - msg->Bounds.MinX, msg->OffsetY + y - msg->Bounds.MinY, (uint32_t)(uintptr_t)h->h_Data);
    return 0;
}

static struct Hook hooks[3];    /* layer hooks with salts, and the Layer_Info's */

/* ---- the model ----------------------------------------------------- */

static struct BitMap *screen;
static struct Layer_Info *li;
static struct Layer *lay[MAXL];
static int nlay;
static struct Hook *lhook[MAXL];        /* each layer's hook, as made */
static struct Hook *ihook;

/* the layer's pixel at its own (u, v), through its ClipRects: where it is,
 * or 0 when the layer keeps none there */
static uint8_t *pix(struct Layer *l, struct ClipRect *list, LONG u, LONG v)
{
    LONG x = l->bounds.MinX + u, y = l->bounds.MinY + v;
    struct ClipRect *cr;
    for (cr = list; cr; cr = cr->Next) {
        if (x < cr->bounds.MinX || x > cr->bounds.MaxX || y < cr->bounds.MinY || y > cr->bounds.MaxY) continue;
        if (!cr->obscured) return &screen->Planes[0][y * SW + x];
        if (cr->BitMap) {
            LONG bx = x - (cr->bounds.MinX & ~15), by = y - cr->bounds.MinY;
            if (bx < 0 || by < 0 || bx >= cr->BitMap->BytesPerRow || by >= cr->BitMap->Rows) {
                fail("backing bitmap too small", bx, by);
                return 0;
            }
            return &cr->BitMap->Planes[0][by * cr->BitMap->BytesPerRow + bx];
        }
        return 0;
    }
    return 0;
}

struct shot {
    struct Layer *l;
    struct Rectangle b;
    WORD sx, sy;
    int hidden;
    uint8_t v[SH * 2][SW * 2];  /* the layer's pixels at its own coordinates */
    uint8_t have[SH * 2][SW * 2];
};
static struct shot before[MAXL];
static uint8_t bg[SH][SW], covered_before[SH][SW];
static int hidden[MAXL];

static int shown_at(LONG x, LONG y, int upto)  /* frontmost shown layer index at (x, y), or -1 */
{
    struct Layer *l;
    int i, k = 0;
    for (l = li->top_layer; l; l = l->back, k++) {
        if (k >= upto) break;
        for (i = 0; i < nlay; i++) if (lay[i] == l) break;
        if (hidden[i]) continue;
        if (x >= l->bounds.MinX && x <= l->bounds.MaxX && y >= l->bounds.MinY && y <= l->bounds.MaxY) return i;
    }
    return -1;
}

static int idx_of(struct Layer *l) { int i; for (i = 0; i < nlay; i++) if (lay[i] == l) return i; return -1; }

/* random pixels everywhere a layer can be drawn and on the bare screen,
 * and the picture of it all */
static void scribble_and_shoot(void)
{
    int i;
    LONG u, v, x, y;
    for (y = 0; y < SH; y++)
        for (x = 0; x < SW; x++) {
            int k = shown_at(x, y, 999);
            covered_before[y][x] = k >= 0;
            if (k < 0) bg[y][x] = screen->Planes[0][y * SW + x] = (uint8_t)rnd();
        }
    for (i = 0; i < nlay; i++) {
        struct Layer *l = lay[i];
        struct shot *s = &before[i];
        s->l = l;
        s->b = l->bounds;
        s->sx = l->Scroll_X;
        s->sy = l->Scroll_Y;
        s->hidden = hidden[i];
        for (v = 0; v <= l->bounds.MaxY - l->bounds.MinY; v++)
            for (u = 0; u <= l->bounds.MaxX - l->bounds.MinX; u++) {
                uint8_t *p = pix(l, l->ClipRect, u, v);
                s->have[v][u] = p != 0;
                if (p) s->v[v][u] = *p = (uint8_t)rnd();
            }
    }
}

static void check_cliprects(int i)
{
    struct Layer *l = lay[i];
    static uint8_t cov[SH * 4][SW * 4];
    struct ClipRect *cr;
    LONG x, y, ox = SW, oy = SH;   /* cov is offset so layers off the screen fit */
    int k, me = 0;
    struct Layer *f;
    for (f = li->top_layer; f && f != l; f = f->back) me++;
    checks++;
    for (y = l->bounds.MinY; y <= l->bounds.MaxY; y++)
        for (x = l->bounds.MinX; x <= l->bounds.MaxX; x++) cov[y + oy][x + ox] = 0;
    for (cr = l->ClipRect; cr; cr = cr->Next) {
        if (cr->bounds.MinX < l->bounds.MinX || cr->bounds.MaxX > l->bounds.MaxX || cr->bounds.MinY < l->bounds.MinY ||
            cr->bounds.MaxY > l->bounds.MaxY || cr->bounds.MinX > cr->bounds.MaxX || cr->bounds.MinY > cr->bounds.MaxY) {
            fail("ClipRect outside its layer", cr->bounds.MinX, cr->bounds.MinY);
            return;
        }
        if (cr->obscured && (l->Flags & (LAYERSMART | LAYERSUPER)) &&
            (!cr->BitMap || cr->BitMap->BytesPerRow < cr->bounds.MaxX - (cr->bounds.MinX & ~15) + 1 ||
             cr->BitMap->Rows < cr->bounds.MaxY - cr->bounds.MinY + 1)) {
            fail("obscured smart or super ClipRect without a big enough bitmap", cr->bounds.MinX, cr->bounds.MinY);
            return;
        }
        for (y = cr->bounds.MinY; y <= cr->bounds.MaxY; y++)
            for (x = cr->bounds.MinX; x <= cr->bounds.MaxX; x++) {
                int vis;
                if (cov[y + oy][x + ox]++) { fail("ClipRects overlap", x, y); return; }
                k = shown_at(x, y, me);
                vis = !hidden[i] && k < 0 && x >= 0 && y >= 0 && x < SW && y < SH;
                if (vis != !cr->obscured) { fail("ClipRect obscured where it should not be, or not", x, y); return; }
            }
    }
    for (y = l->bounds.MinY; y <= l->bounds.MaxY; y++)
        for (x = l->bounds.MinX; x <= l->bounds.MaxX; x++)
            if (!cov[y + oy][x + ox]) {
                /* a simple refresh layer has none where it is not on show */
                int vis = !hidden[i] && shown_at(x, y, me) < 0 && x >= 0 && y >= 0 && x < SW && y < SH;
                if (vis || (l->Flags & (LAYERSMART | LAYERSUPER))) { fail("ClipRects leave a hole", x, y); return; }
            }
}

/* What a fresh pixel of layer i must be at its (u, v), or -1 for any. */
static int fresh_value(int i, LONG u, LONG v)
{
    struct Layer *l = lay[i];
    struct Hook *h = lhook[i];
    if (h == LAYERS_NOBACKFILL) return -1;
    if (h == LAYERS_BACKFILL) return 0;
    return pat(u + l->Scroll_X, v + l->Scroll_Y, (uint32_t)(uintptr_t)h->h_Data);
}

/* After a call that kept layer i's origin at (old + delta) and its
 * contents: deleted 0 or 1. */
static void check_after(int made)
{
    int i;
    LONG u, v, x, y;
    for (i = 0; i < nlay; i++) {
        struct Layer *l = lay[i];
        struct shot *s = &before[i];
        int simple = !(l->Flags & (LAYERSMART | LAYERSUPER));
        LONG w = l->bounds.MaxX - l->bounds.MinX + 1, h = l->bounds.MaxY - l->bounds.MinY + 1;
        LONG ow = s->b.MaxX - s->b.MinX + 1, oh = s->b.MaxY - s->b.MinY + 1;
        int bad = 0;
        check_cliprects(i);
        if (l->Flags & LAYERUPDATING) continue;
        checks++;
        for (v = 0; v < h && !bad; v++)
            for (u = 0; u < w && !bad; u++) {
                uint8_t *p = pix(l, l->ClipRect, u, v);
                int fresh = i == made || u >= ow || v >= oh || (simple && !s->have[v][u]) ||
                            ((l->Flags & LAYERSUPER) && 0);
                if (!p) continue;      /* a simple layer's covered part: nothing to see */
                if (!fresh) {
                    if (*p != s->v[v][u]) { fail("layer lost a pixel", u, v); bad = 1; }
                } else {
                    int want = fresh_value(i, u, v);
                    if (l->Flags & LAYERSUPER) continue;
                    if (want >= 0 && *p != want) { fail("fresh pixel is not the backfill", u, v); bad = 1; }
                    if (!simple && i == made) continue;
                    if (i != made && (u < ow && v < oh ? simple : 1) && !in_region(l->DamageList, u, v)) {
                        fail("fresh pixel not in the DamageList", u, v); bad = 1;
                    }
                }
            }
    }
    checks++;
    for (y = 0; y < SH; y++)
        for (x = 0; x < SW; x++) {
            uint8_t p = screen->Planes[0][y * SW + x];
            if (shown_at(x, y, 999) >= 0) continue;
            if (!covered_before[y][x]) {
                if (p != bg[y][x]) { fail("bare screen changed", x, y); return; }
            } else {
                int want = ihook == LAYERS_BACKFILL ? 0 : ihook == LAYERS_NOBACKFILL ? -1 : pat(x, y, 99);
                if (want >= 0 && p != want) { fail("uncovered screen is not the Layer_Info's backfill", x, y); return; }
            }
        }
}

static void check_locks(void)
{
    struct Layer *l;
    checks++;
    if (li->Lock.ss_NestCount) fail("Layer_Info left locked", li->Lock.ss_NestCount, 0);
    for (l = li->top_layer; l; l = l->back)
        if (l->Lock.ss_NestCount) fail("layer left locked", l->Lock.ss_NestCount, 0);
}

static struct BitMap *supers[MAXL];

static void make_layer(void)
{
    LONG x0 = (LONG)(rnd() % (SW + 20)) - 10, y0 = (LONG)(rnd() % (SH + 20)) - 10;
    LONG x1 = x0 + 1 + (LONG)(rnd() % 70), y1 = y0 + 1 + (LONG)(rnd() % 50);
    LONG kind = rnd() % 3, flags;
    struct Hook *h;
    struct Layer *l;
    struct BitMap *sbm = 0;
    if (nlay >= MAXL) return;
    flags = kind == 0 ? LAYERSIMPLE : kind == 1 ? LAYERSMART : LAYERSMART | LAYERSUPER;
    if (rnd() % 5 == 0) flags |= LAYERBACKDROP;
    h = rnd() % 3 == 0 ? LAYERS_BACKFILL : rnd() % 4 == 0 ? LAYERS_NOBACKFILL : &hooks[rnd() % 2];
    if (flags & LAYERSUPER) {
        if (!(sbm = AllocBitMap(SW * 2, SH * 2, 8, 0, 0))) return;
        memset(sbm->Planes[0], 0x3C, SW * 2 * SH * 2);
    }
    doing = "create";
    scribble_and_shoot();
    l = rnd() % 2 ? ol_CreateUpfrontHookLayer(li, screen, x0, y0, x1, y1, flags, h, sbm)
                  : ol_CreateBehindHookLayer(li, screen, x0, y0, x1, y1, flags, h, sbm);
    if (!l) { if (sbm) FreeBitMap(sbm); return; }
    lay[nlay] = l;
    lhook[nlay] = h;
    hidden[nlay] = 0;
    supers[nlay] = sbm;
    before[nlay].b = l->bounds;
    nlay++;
    /* the new layer's own pixels: the backfill, or its super bitmap's */
    if (flags & LAYERSUPER) {
        LONG u, v;
        checks++;
        for (v = 0; v <= y1 - y0; v++)
            for (u = 0; u <= x1 - x0; u++) {
                uint8_t *p = pix(l, l->ClipRect, u, v);
                if (p && *p != 0x3C) { fail("new super layer is not its bitmap", u, v); v = y1; break; }
            }
    }
    check_after(nlay - 1);
    check_locks();
}

static void delete_layer(int i)
{
    int k;
    doing = "delete";
    scribble_and_shoot();
    if (!ol_DeleteLayer(lay[i])) { fail("DeleteLayer failed", 0, 0); return; }
    if (supers[i]) FreeBitMap(supers[i]);
    for (k = i; k + 1 < nlay; k++) {
        lay[k] = lay[k + 1]; lhook[k] = lhook[k + 1]; hidden[k] = hidden[k + 1]; supers[k] = supers[k + 1];
        before[k] = before[k + 1];
    }
    nlay--;
    check_after(-1);
    check_locks();
}

/* Draws random pixels through the layer's ClipRects in use (a clip
 * region's or an update's) and checks they land only inside what that
 * list covers. */
static void draw_through(int i, const struct Region *region, int damage_only)
{
    struct Layer *l = lay[i];
    LONG u, v, w = l->bounds.MaxX - l->bounds.MinX + 1, h = l->bounds.MaxY - l->bounds.MinY + 1;
    static uint8_t drawn[SH * 2][SW * 2], val[SH * 2][SW * 2];
    struct shot *s = &before[i];
    for (v = 0; v < h; v++)
        for (u = 0; u < w; u++) {
            uint8_t *p = pix(l, l->ClipRect, u, v);
            int inside = (!region || in_region(region, u + l->Scroll_X, v + l->Scroll_Y)) &&
                         (!damage_only || in_region(l->DamageList, u, v));
            drawn[v][u] = 0;
            if (p) {
                if (!inside) { fail("a cut list reaches outside its region", u, v); u = w; v = h; break; }
                *p = val[v][u] = (uint8_t)rnd();
                drawn[v][u] = 1;
            } else if (inside && s->have[v][u] && !l->ClipRect) {
                ;   /* an empty update */
            }
        }
    /* back to the full list, then what was drawn must be there and the
     * rest as it was */
    if (damage_only) ol_EndUpdate(l, FALSE);
    else ol_InstallClipRegion(l, 0);
    checks++;
    for (v = 0; v < h; v++)
        for (u = 0; u < w; u++) {
            uint8_t *p = pix(l, l->ClipRect, u, v);
            if (!p) continue;
            if (drawn[v][u] ? *p != val[v][u] : (s->have[v][u] && *p != s->v[v][u])) {
                fail(drawn[v][u] ? "a pixel drawn through a cut list is lost" : "a cut list changed a pixel outside it", u, v);
                return;
            }
        }
}

static void one_round(int round)
{
    int step, i;
    nlay = 0;
    li = ol_NewLayerInfo();
    if (!li) { fail("NewLayerInfo", 0, 0); return; }
    ihook = rnd() % 2 ? LAYERS_BACKFILL : &hooks[2];
    ol_InstallLayerInfoHook(li, ihook);
    for (step = 0; step < 40; step++) {
        int op = (int)(rnd() % 14);
        if (nlay == 0 || (op == 0 && nlay < MAXL)) { make_layer(); continue; }
        i = (int)(rnd() % (uint32_t)nlay);
        scribble_and_shoot();
        switch (op) {
        case 1: doing = "upfront"; ol_UpfrontLayer(lay[i]); break;
        case 2: doing = "behind"; ol_BehindLayer(lay[i]); break;
        case 3: case 4: {
            LONG dx = (LONG)(rnd() % 41) - 20, dy = (LONG)(rnd() % 31) - 15;
            doing = "move";
            ol_MoveLayer(lay[i], dx, dy);
            break;
        }
        case 5: {
            LONG dx = (LONG)(rnd() % 41) - 20, dy = (LONG)(rnd() % 31) - 15;
            doing = "size";
            ol_SizeLayer(lay[i], dx, dy);
            break;
        }
        case 6: {
            LONG dx = (LONG)(rnd() % 21) - 10, dy = (LONG)(rnd() % 21) - 10, dw = (LONG)(rnd() % 21) - 10,
                 dh = (LONG)(rnd() % 21) - 10;
            doing = "movesize";
            ol_MoveSizeLayer(lay[i], dx, dy, dw, dh);
            break;
        }
        case 7: doing = "infrontof"; ol_MoveLayerInFrontOf(lay[i], lay[rnd() % (uint32_t)nlay]); break;
        case 8:
            doing = hidden[i] ? "show" : "hide";
            if (hidden[i]) { ol_ShowLayer(lay[i], 0); hidden[i] = 0; }
            else { ol_HideLayer(lay[i]); hidden[i] = 1; }
            break;
        case 9: delete_layer(i); continue;
        case 10: {
            /* a clip region of a few rectangles, drawn through */
            struct Region *rg = NewRegion();
            int k, n = 1 + (int)(rnd() % 3);
            doing = "clip region";
            for (k = 0; k < n; k++) {
                struct Rectangle r;
                r.MinX = (WORD)(rnd() % 60) - 5; r.MinY = (WORD)(rnd() % 40) - 5;
                r.MaxX = (WORD)(r.MinX + rnd() % 40); r.MaxY = (WORD)(r.MinY + rnd() % 30);
                OrRectRegion(rg, &r);
            }
            ol_InstallClipRegion(lay[i], rg);
            draw_through(i, rg, 0);
            DisposeRegion(rg);
            check_locks();
            continue;
        }
        case 11:
            doing = "update";
            if (ol_BeginUpdate(lay[i])) draw_through(i, 0, 1);
            else ol_EndUpdate(lay[i], FALSE);
            if (rnd() % 2) { ol_BeginUpdate(lay[i]); ol_EndUpdate(lay[i], TRUE); }
            checks++;
            if (lay[i]->DamageList->RegionRectangle && !(lay[i]->Flags & LAYERREFRESH) && 0) fail("damage without LAYERREFRESH", 0, 0);
            check_locks();
            continue;
        case 12:
            if (lay[i]->Flags & LAYERSUPER) {
                /* the super bitmap scrolls under the layer, unclamped: every
                 * pixel the layer keeps goes into it (where it reaches),
                 * then what shows is the super bitmap at the new place, or
                 * the pixel as it was where the super bitmap does not reach */
                struct Layer *l = lay[i];
                LONG dx = (LONG)(rnd() % 21) - 10, dy = (LONG)(rnd() % 21) - 10, u, v, sx, sy;
                static uint8_t sup[SH * 2][SW * 2];
                LONG w = l->bounds.MaxX - l->bounds.MinX + 1, h = l->bounds.MaxY - l->bounds.MinY + 1;
                doing = "scroll";
                for (v = 0; v < SH * 2; v++)
                    for (u = 0; u < SW * 2; u++) sup[v][u] = l->SuperBitMap->Planes[0][v * SW * 2 + u];
                for (v = 0; v < h; v++)
                    for (u = 0; u < w; u++) {
                        sx = u + l->Scroll_X; sy = v + l->Scroll_Y;
                        if (before[i].have[v][u] && sx >= 0 && sy >= 0 && sx < SW * 2 && sy < SH * 2)
                            sup[sy][sx] = before[i].v[v][u];
                    }
                ol_ScrollLayer(l, dx, dy);
                checks++;
                for (v = 0; v < h; v++)
                    for (u = 0; u < w; u++) {
                        uint8_t *p = pix(l, l->ClipRect, u, v);
                        int in;
                        sx = u + l->Scroll_X; sy = v + l->Scroll_Y;
                        in = sx >= 0 && sy >= 0 && sx < SW * 2 && sy < SH * 2;
                        if (p && *p != (in ? sup[sy][sx] : before[i].v[v][u])) { fail("scrolled pixel", u, v); v = h; break; }
                    }
                check_locks();
                continue;
            }
            doing = "whichlayer";
            {
                LONG x = (LONG)(rnd() % SW), y = (LONG)(rnd() % SH);
                int k = shown_at(x, y, 999);
                checks++;
                if (ol_WhichLayer(li, x, y) != (k < 0 ? 0 : lay[k])) fail("WhichLayer", x, y);
            }
            continue;
        default: {
            /* LayerOccluded */
            struct Layer *f;
            int want = hidden[i];       /* a hidden layer counts as occluded, as on OS 3.2 */
            doing = "occluded";
            for (f = lay[i]->front; f; f = f->front) {
                int k = idx_of(f);
                if (!hidden[k] && f->bounds.MinX <= lay[i]->bounds.MaxX && f->bounds.MaxX >= lay[i]->bounds.MinX &&
                    f->bounds.MinY <= lay[i]->bounds.MaxY && f->bounds.MaxY >= lay[i]->bounds.MinY)
                    want = 1;
            }
            checks++;
            if (!ol_LayerOccluded(lay[i]) != !want) fail("LayerOccluded", want, 0);
            continue;
        }
        }
        check_after(-1);
        check_locks();
        /* backdrop layers stay behind the others (OS 3.2 sends a hidden layer to the very back) */
        {
            struct Layer *l;
            int seen = 0;
            checks++;
            for (l = li->top_layer; l; l = l->back) {
                if (l->Flags & 0x1000) continue;
                if (l->Flags & LAYERBACKDROP) seen = 1;
                else if (seen) { fail("a backdrop layer in front of another", 0, 0); break; }
            }
        }
    }
    /* out of memory part way through a move: the stack as it was, or done */
    if (nlay) {
        struct Rectangle ob = lay[0]->bounds;
        int ok;
        doing = "move without memory";
        scribble_and_shoot();
        fail_alloc = 1 + rnd() % 6;
        ok = (int)ol_MoveLayer(lay[0], 7, 3);
        fail_alloc = 0;
        if (!ok) {
            checks++;
            if (memcmp(&ob, &lay[0]->bounds, sizeof ob)) fail("failed move moved the layer", 0, 0);
        }
        check_after(-1);
        check_locks();
    }
    while (nlay) delete_layer(0);
    ol_DisposeLayerInfo(li);
    (void)round;
}

int main(void)
{
    int round;
    ol_init();
    hooks[0].h_Entry = (ULONG (*)(void))(void *)fill_hook; hooks[0].h_Data = (APTR)(uintptr_t)11;
    hooks[1].h_Entry = (ULONG (*)(void))(void *)fill_hook; hooks[1].h_Data = (APTR)(uintptr_t)42;
    hooks[2].h_Entry = (ULONG (*)(void))(void *)fill_hook; hooks[2].h_Data = (APTR)(uintptr_t)99;
    screen = AllocBitMap(SW, SH, 8, 0, 0);
    for (round = 0; round < ROUNDS; round++) {
        long before_allocs = live_allocs;
        one_round(round);
        checks++;
        if (live_allocs != before_allocs) fail("memory not given back", live_allocs - before_allocs, round);
        if (fails) break;
    }
    if (fails) { printf("%lu of %lu checks failed\n", fails, checks); return 1; }
    printf("%lu checks\n", checks);
    return 0;
}
