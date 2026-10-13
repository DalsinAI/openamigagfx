/* SPDX-License-Identifier: MIT */
/* LayersABITest on the host, over OpenLayers' own code: exec, dos and
 * graphics.library played closely enough that its PROBE lines can be set
 * beside the ones OS 3.2 gave on an Amiga. Copyright (c) 2026 Dalsin
 * Limited. MIT.
 *
 * Bitmaps are planar, laid out as AllocBitMap lays them out (rows of whole
 * words), so the screen's checksum is the Amiga's. RectFill and ReadPixel
 * go through a layer's ClipRects as graphics.library does: the screen where
 * a ClipRect is not obscured, else the super bitmap at the layer's own
 * coordinates, else the ClipRect's backing bitmap at x - (MinX & ~15),
 * y - MinY, else nowhere (ReadPixel gives -1). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shim.h"
#include "layers_lib.h"

/* ---- exec and dos -------------------------------------------------- */

APTR AllocMem(ULONG size, ULONG flags)
{
    UBYTE *p = malloc(size ? size : 1);
    if (p) memset(p, (flags & MEMF_CLEAR) ? 0 : 0xA5, size);
    return p;
}
void FreeMem(APTR p, ULONG size) { (void)size; free(p); }
void InitSemaphore(struct SignalSemaphore *s) { memset(s, 0, sizeof *s); }
void ObtainSemaphore(struct SignalSemaphore *s) { s->ss_NestCount++; }
void ReleaseSemaphore(struct SignalSemaphore *s)
{
    if (--s->ss_NestCount < 0) { fprintf(stderr, "semaphore released too often\n"); exit(20); }
}

static struct Library lib = { { 0 }, (APTR)"layers.library 47.100 (host) OpenLayers\n" };
struct Library *OpenLibrary(const char *name, ULONG version)
{
    static int done;
    (void)name; (void)version;
    if (!done) { ol_init(); done = 1; }
    return &lib;
}
void CloseLibrary(struct Library *l) { (void)l; }
BOOL AddPart(char *dir, const char *file, ULONG size) { (void)size; strcat(dir, "/"); strcat(dir, file); return TRUE; }

/* ---- bitmaps ------------------------------------------------------- */

void InitRastPort(struct RastPort *rp) { memset(rp, 0, sizeof *rp); rp->Mask = 0xFF; rp->FgPen = 1; }
void WaitBlit(void) {}

struct BitMap *AllocBitMap(ULONG w, ULONG h, ULONG depth, ULONG flags, struct BitMap *friend_bm)
{
    struct BitMap *bm = calloc(1, sizeof *bm);
    ULONG p, bpr = ((w + 15) >> 4) * 2;
    (void)friend_bm;
    if (!bm) return 0;
    bm->BytesPerRow = (UWORD)bpr;
    bm->Rows = (UWORD)h;
    bm->Depth = (UBYTE)depth;
    for (p = 0; p < depth; p++) {
        bm->Planes[p] = malloc(bpr * h);
        memset(bm->Planes[p], (flags & BMF_CLEAR) ? 0 : 0x5A, bpr * h);
    }
    return bm;
}

void FreeBitMap(struct BitMap *bm)
{
    int p;
    if (!bm) return;
    for (p = 0; p < bm->Depth; p++) free(bm->Planes[p]);
    free(bm);
}

ULONG GetBitMapAttr(struct BitMap *bm, ULONG attr)
{
    switch (attr) {
    case BMA_WIDTH: return (ULONG)bm->BytesPerRow * 8;
    case BMA_HEIGHT: return bm->Rows;
    case BMA_DEPTH: return bm->Depth;
    default: return 0;
    }
}

static int inside(struct BitMap *bm, LONG x, LONG y)
{
    return x >= 0 && y >= 0 && x < (LONG)bm->BytesPerRow * 8 && y < bm->Rows;
}

static ULONG get(struct BitMap *bm, LONG x, LONG y)
{
    ULONG v = 0;
    int p;
    for (p = 0; p < bm->Depth; p++)
        if (bm->Planes[p][y * bm->BytesPerRow + (x >> 3)] & (0x80 >> (x & 7))) v |= 1u << p;
    return v;
}

static void put(struct BitMap *bm, LONG x, LONG y, ULONG v, ULONG mask)
{
    int p;
    for (p = 0; p < bm->Depth; p++) {
        UBYTE *b = &bm->Planes[p][y * bm->BytesPerRow + (x >> 3)], bit = (UBYTE)(0x80 >> (x & 7));
        if (!(mask & (1u << p))) continue;
        if (v & (1u << p)) *b |= bit; else *b &= (UBYTE)~bit;
    }
}

static void bad(const char *what, long a, long b)
{
    fprintf(stderr, "abihost: %s %ld %ld\n", what, a, b);
    exit(20);
}

LONG BltBitMap(struct BitMap *src, LONG sx, LONG sy, struct BitMap *dst, LONG dx, LONG dy, LONG w, LONG h,
               ULONG minterm, ULONG mask, PLANEPTR temp)
{
    ULONG *t;
    LONG x, y;
    int p, depth = src->Depth < dst->Depth ? src->Depth : dst->Depth;
    (void)temp;
    if (w <= 0 || h <= 0) return 0;
    if (!inside(src, sx, sy) || !inside(src, sx + w - 1, sy + h - 1) || !inside(dst, dx, dy) ||
        !inside(dst, dx + w - 1, dy + h - 1))
        bad("blit outside a bitmap", sx * 1000 + sy, dx * 1000 + dy);
    t = malloc(sizeof *t * (size_t)(w * h));
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) t[y * w + x] = get(src, sx + x, sy + y);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            ULONG b = t[y * w + x], c = get(dst, dx + x, dy + y), r = 0;
            for (p = 0; p < depth; p++) {
                int bi = (b >> p) & 1, ci = (c >> p) & 1;
                if ((minterm >> (4 + 2 * bi + ci)) & 1) r |= 1u << p;
            }
            put(dst, dx + x, dy + y, r, mask & ((1u << depth) - 1));
        }
    free(t);
    return depth;
}

/* ---- regions: rectangles relative to the bounds, maybe overlapping -- */

struct Region *NewRegion(void) { return calloc(1, sizeof(struct Region)); }

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
    struct RegionRectangle *rr = calloc(1, sizeof *rr);
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
    for (; rr; rr = n) { n = rr->Next; free(rr); }
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
            free(rr);
        } else {
            pp = &rr->Next;
        }
    }
    region_rel(r);
}

void DisposeRegion(struct Region *r) { if (r) { ClearRegion(r); free(r); } }

/* ---- drawing through a layer --------------------------------------- */

typedef ULONG (*hookfn)(struct Hook *, APTR, APTR);
ULONG ol_call_hook(struct Hook *hook, APTR obj, APTR msg) { return ((hookfn)(void *)hook->h_Entry)(hook, obj, msg); }

void SetAPen(struct RastPort *rp, ULONG pen) { rp->FgPen = (BYTE)pen; }

/* where the rastport's (x, y) is: a bitmap and a place in it, or none */
static struct BitMap *where(struct RastPort *rp, LONG x, LONG y, LONG *bx, LONG *by)
{
    struct Layer *l = rp->Layer;
    struct ClipRect *cr;
    LONG sx, sy;
    if (!l) {
        if (!inside(rp->BitMap, x, y)) return 0;
        *bx = x; *by = y;
        return rp->BitMap;
    }
    sx = x - l->Scroll_X + l->bounds.MinX;
    sy = y - l->Scroll_Y + l->bounds.MinY;
    for (cr = l->ClipRect; cr; cr = cr->Next) {
        if (sx < cr->bounds.MinX || sx > cr->bounds.MaxX || sy < cr->bounds.MinY || sy > cr->bounds.MaxY) continue;
        if (!cr->obscured) { *bx = sx; *by = sy; return inside(rp->BitMap, sx, sy) ? rp->BitMap : 0; }
        if (l->Flags & LAYERSUPER) {
            *bx = x; *by = y;
            return l->SuperBitMap && inside(l->SuperBitMap, x, y) ? l->SuperBitMap : 0;
        }
        if (cr->BitMap) {
            *bx = sx - (cr->bounds.MinX & ~15); *by = sy - cr->bounds.MinY;
            if (!inside(cr->BitMap, *bx, *by)) bad("backing bitmap too small", *bx, *by);
            return cr->BitMap;
        }
        return 0;
    }
    return 0;
}

void RectFill(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1)
{
    LONG x, y, bx, by;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            struct BitMap *bm = where(rp, x, y, &bx, &by);
            if (bm) put(bm, bx, by, (ULONG)(UBYTE)rp->FgPen, rp->Mask);
        }
}

LONG ReadPixel(struct RastPort *rp, LONG x, LONG y)
{
    LONG bx, by;
    struct BitMap *bm = where(rp, x, y, &bx, &by);
    return bm ? (LONG)get(bm, bx, by) : -1;
}

/* ---- layers.library, as LayersABITest calls it --------------------- */

struct Layer_Info *NewLayerInfo(void) { return ol_NewLayerInfo(); }
void DisposeLayerInfo(struct Layer_Info *li) { ol_DisposeLayerInfo(li); }
struct Hook *InstallLayerInfoHook(struct Layer_Info *li, struct Hook *h) { return ol_InstallLayerInfoHook(li, h); }
struct Layer *CreateUpfrontHookLayer(struct Layer_Info *li, struct BitMap *bm, LONG x0, LONG y0, LONG x1, LONG y1,
                                     LONG flags, struct Hook *h, struct BitMap *sbm)
{
    return ol_CreateUpfrontHookLayer(li, bm, x0, y0, x1, y1, flags, h, sbm);
}
struct Layer *CreateBehindHookLayer(struct Layer_Info *li, struct BitMap *bm, LONG x0, LONG y0, LONG x1, LONG y1,
                                    LONG flags, struct Hook *h, struct BitMap *sbm)
{
    return ol_CreateBehindHookLayer(li, bm, x0, y0, x1, y1, flags, h, sbm);
}
LONG DeleteLayer(LONG d, struct Layer *l) { (void)d; return ol_DeleteLayer(l); }
LONG UpfrontLayer(LONG d, struct Layer *l) { (void)d; return ol_UpfrontLayer(l); }
LONG BehindLayer(LONG d, struct Layer *l) { (void)d; return ol_BehindLayer(l); }
LONG MoveLayer(LONG d, struct Layer *l, LONG dx, LONG dy) { (void)d; return ol_MoveLayer(l, dx, dy); }
LONG SizeLayer(LONG d, struct Layer *l, LONG dx, LONG dy) { (void)d; return ol_SizeLayer(l, dx, dy); }
LONG MoveSizeLayer(struct Layer *l, LONG dx, LONG dy, LONG dw, LONG dh) { return ol_MoveSizeLayer(l, dx, dy, dw, dh); }
LONG MoveLayerInFrontOf(struct Layer *l, struct Layer *o) { return ol_MoveLayerInFrontOf(l, o); }
LONG ShowLayer(struct Layer *l, struct Layer *o) { return ol_ShowLayer(l, o); }
LONG HideLayer(struct Layer *l) { return ol_HideLayer(l); }
struct Region *InstallClipRegion(struct Layer *l, struct Region *r) { return ol_InstallClipRegion(l, r); }
LONG BeginUpdate(struct Layer *l) { return ol_BeginUpdate(l); }
void EndUpdate(struct Layer *l, UWORD flag) { ol_EndUpdate(l, flag); }
void ScrollLayer(LONG d, struct Layer *l, LONG dx, LONG dy) { (void)d; ol_ScrollLayer(l, dx, dy); }
struct Layer *WhichLayer(struct Layer_Info *li, LONG x, LONG y) { return ol_WhichLayer(li, x, y); }
LONG LayerOccluded(struct Layer *l) { return ol_LayerOccluded(l); }
void LockLayer(LONG d, struct Layer *l) { (void)d; ol_LockLayer(l); }
void UnlockLayer(struct Layer *l) { ol_UnlockLayer(l); }
void LockLayers(struct Layer_Info *li) { ol_LockLayers(li); }
void UnlockLayers(struct Layer_Info *li) { ol_UnlockLayers(li); }
void DoHookClipRects(struct Hook *h, struct RastPort *rp, struct Rectangle *r) { ol_DoHookClipRects(h, rp, r); }
void SortLayerCR(struct Layer *l, LONG dx, LONG dy) { ol_SortLayerCR(l, dx, dy); }
void SwapBitsRastPortClipRect(struct RastPort *rp, struct ClipRect *cr) { ol_SwapBitsRastPortClipRect(rp, cr); }
