/* SPDX-License-Identifier: MIT */
/* OpenLayers' composited mode. Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Included by layers_lib.c when OL_COMPOSITE is defined (the Amiga build;
 * the host and qemu tests build classic layers only). The design is
 * docs/design/compositing.md.
 *
 * On a composited Layer_Info (ComposeLayerInfo) every layer draws into a
 * surface of its own: a bitmap made with the screen's bitmap as its friend,
 * so OpenRTG makes it in its format, whose memory ORTG_BitMapInfo gives.
 * The layer has one ClipRect, obscured, whose BitMap is the surface, so
 * graphics.library and OpenRTG draw there as into a smart refresh layer's
 * backing bitmap: at x - (MinX & ~15). A layer's pixels therefore start at
 * x (MinX & 15) in its surface, and move in it when a move changes that.
 *
 * The screen's bitmap always holds what is shown. A change of the stack is
 * composed into it at once, under every lock the change holds; what is
 * drawn into a layer is marked (oc_mark, on the drawing calls' vectors)
 * and composed by the compositor task at the next vertical blank. A
 * compose is one OpenGPU batch with one fence: what no layer covers gets
 * the Layer_Info's backfill, then each shown layer, back to front, its
 * shadow under it, is copied (opaque) or composited (with its opacity, or
 * its own alpha for an ARGB32 surface). Layers that are opaque hide what
 * is behind them, so nothing hidden is drawn.
 *
 * Nothing here runs for a Layer_Info ComposeLayerInfo has not switched on:
 * those are classic layers, exactly as before. */
#include <exec/tasks.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <graphics/gfxbase.h>
#include <graphics/openlayers.h>
/* the public OpenGPU and OpenRTG headers and their inline calls (not
 * proto/opengpu.h: opengpu/module.h has a field named SysBase, which
 * ol_sysbase.h makes a macro) */
#include <opengpu/opengpu.h>
#include <openrtg/openrtg.h>
#define OPENGPU_BASE_NAME (cb->ogpu)
#include <inline/opengpu.h>
#define OPENRTG_BASE_NAME (cb->ortg)
#include <inline/openrtg.h>
#define DOS_BASE_NAME dos
#include <proto/dos.h>

#ifndef OGPU_FMT_XRGB32
#define OGPU_FMT_XRGB32 10      /* stream v1.3's; used only where OGPU_Query answers FULL for it */
#endif

enum { S_SCREEN, S_SURF, S_SHADOW, S_SUB };     /* the batch's slots */
/* the look's settings, as ENV:OpenPrefs/Windows' lines (oc_prefs' keys):
 * composite on|off; shadows on|off; the shadow's reach R in pixels; and,
 * as 0-255, its darkest and a dragged and an inactive window's opacity */
enum { SET_ON, SET_SHADOWS, SET_SIZE, SET_STRENGTH, SET_DRAG, SET_INACTIVE };

#define OC_STACK 16384
#define OC_SIG SIGBREAKF_CTRL_F
#define OC_NPATCH 37
#define OC_THUNK 25

/* One composited Layer_Info. */
struct OComp {
    struct OComp *next;
    struct OInfo *inf;
    struct Layer_Info *li;
    struct BitMap *screen;
    ULONG saddr, sbpr;          /* the screen's pixel 0,0 and bytes a row */
    UWORD w, h;
    UBYTE fmt, depth;           /* OGPU format of the screen and the surfaces; 16 or 32 */
    UBYTE set[6];               /* the prefs (oc_prefs): SET_ */
    UBYTE full;                 /* the compositor composes all of it next */
    ULONG *shadow;              /* ARGB32, (4R + 1) square: the shadow's corners and edges */
};

struct OThunk {
    UWORD w[OC_THUNK + 1];
};

/* The composited mode's state (olb->cb), made at the first composited
 * screen and kept. */
struct OCB {
    struct SignalSemaphore lock;        /* every composited stack change, and every compose */
    struct ol_base *olb;
    struct Library *ogpu, *ortg, *cgx;
    struct Task *task;
    struct OComp *comps;
    UBYTE pending;
    ULONG composes;
    struct OThunk *thunk[OC_NPATCH];    /* the newest thunk on each call */
};

struct oc_batch {
    ULONG *w;
    ULONG n, cap;
};

/* ---- marking what is drawn --------------------------------------------- */

/* What a drawing call draws, from its registers as the thunk saved them:
 * 0-7 d0-d7, 8 a0, 9 a1. */
enum { MK_WHOLE, MK_XYXY, MK_XYWH, MK_TEXT };
struct omark {
    UBYTE type, rp, a, b, c, d;
};
static const struct omark marks[] = {
    { MK_WHOLE, 9, 0, 0, 0, 0 },        /* 0: the RastPort in a1, all of its layer */
    { MK_XYXY, 9, 0, 1, 2, 3 },         /* 1: RectFill, BltPattern, EraseRect */
    { MK_XYXY, 9, 2, 3, 4, 5 },         /* 2: ScrollRaster, ScrollRasterBF */
    { MK_XYWH, 9, 2, 3, 4, 5 },         /* 3: BltTemplate, ClipBlit, BltBitMapRastPort, BltMaskBitMapRastPort, MovePixelArray */
    { MK_XYXY, 9, 0, 1, 0, 1 },         /* 4: WritePixel, WriteRGBPixel */
    { MK_XYXY, 8, 0, 1, 2, 3 },         /* 5: WritePixelArray8, WriteChunkyPixels */
    { MK_WHOLE, 8, 0, 0, 0, 0 },        /* 6: WritePixelLine8 */
    { MK_TEXT, 9, 0, 0, 0, 0 },         /* 7: Text: the rows of the font, all across */
    { MK_WHOLE, 9, 0, 0, 0, 0 },        /* 8: DrawEllipse */
    { MK_XYWH, 9, 3, 4, 5, 6 },         /* 9: WritePixelArray, WriteLUTPixelArray, WritePixelArrayAlpha */
    { MK_XYWH, 9, 0, 1, 2, 3 },         /* 10: InvertPixelArray, FillPixelArray, ORTG_FillPixels, ORTG_InvertPixels */
};

/* The calls marked: graphics.library's drawing calls (0), CyberGraphX's
 * (1) and OpenRTG's (2) that draw into a RastPort. */
struct opatch {
    UBYTE lib, mark;
    WORD lvo;
};
static const struct opatch patches[OC_NPATCH] = {
    { 0, 3, -36 },      /* BltTemplate */
    { 0, 0, -42 },      /* ClearEOL */
    { 0, 0, -48 },      /* ClearScreen */
    { 0, 7, -60 },      /* Text */
    { 0, 0, -114 },     /* DrawGList */
    { 0, 8, -180 },     /* DrawEllipse */
    { 0, 0, -234 },     /* SetRast */
    { 0, 0, -246 },     /* Draw */
    { 0, 0, -264 },     /* AreaEnd */
    { 0, 1, -306 },     /* RectFill */
    { 0, 1, -312 },     /* BltPattern */
    { 0, 4, -324 },     /* WritePixel */
    { 0, 0, -330 },     /* Flood */
    { 0, 0, -336 },     /* PolyDraw */
    { 0, 2, -396 },     /* ScrollRaster */
    { 0, 3, -552 },     /* ClipBlit */
    { 0, 3, -606 },     /* BltBitMapRastPort */
    { 0, 3, -636 },     /* BltMaskBitMapRastPort */
    { 0, 6, -774 },     /* WritePixelLine8 */
    { 0, 5, -786 },     /* WritePixelArray8 */
    { 0, 1, -810 },     /* EraseRect */
    { 0, 2, -1002 },    /* ScrollRasterBF */
    { 0, 5, -1056 },    /* WriteChunkyPixels */
    { 1, 0, -90 },      /* ScalePixelArray */
    { 1, 4, -114 },     /* WriteRGBPixel */
    { 1, 9, -126 },     /* WritePixelArray */
    { 1, 3, -132 },     /* MovePixelArray */
    { 1, 10, -144 },    /* InvertPixelArray */
    { 1, 10, -150 },    /* FillPixelArray */
    { 1, 0, -156 },     /* DoCDrawMethodTagList */
    { 1, 9, -198 },     /* WriteLUTPixelArray */
    { 1, 9, -216 },     /* WritePixelArrayAlpha */
    { 2, 0, -78 },      /* ORTG_WritePixels */
    { 2, 10, -90 },     /* ORTG_FillPixels */
    { 2, 10, -96 },     /* ORTG_InvertPixels */
    { 2, 0, -114 },     /* ORTG_WritePixelsAlpha */
    { 2, 0, -120 },     /* ORTG_PresentVirgl */
};

/* Drawn: a rectangle from the layer's top left joins its dirty box, and
 * the compositor is woken once. */
static __attribute__((noinline)) void oc_add(struct OCB *cb, struct OLayer *o, LONG x0, LONG y0, LONG x1, LONG y1)
{
    struct lc_rect *d = &o->dirty;
    Forbid();
    if (d->x0 > d->x1) {
        d->x0 = x0; d->y0 = y0; d->x1 = x1; d->y1 = y1;
    } else {
        if (x0 < d->x0) d->x0 = x0;
        if (y0 < d->y0) d->y0 = y0;
        if (x1 > d->x1) d->x1 = x1;
        if (y1 > d->y1) d->y1 = y1;
    }
    if (!cb->pending) {
        cb->pending = 1;
        Signal(cb->task, OC_SIG);
    }
    Permit();
}

/* r (the screen's coordinates; 0: all of it) of a composited layer was
 * drawn by layers.library itself */
static void oc_touch(struct ol_base *olb, struct Layer *l, const struct lc_rect *r)
{
    struct lc_rect a, m;
    if (!olb->cb || !OL(l)->surf) return;
    a.x0 = a.y0 = 0;
    a.x1 = l->bounds.MaxX - l->bounds.MinX;
    a.y1 = l->bounds.MaxY - l->bounds.MinY;
    if (r) {
        m = shift(*r, -l->bounds.MinX, -l->bounds.MinY);
        if (!lc_meet(&a, &m, &a)) return;
    }
    oc_add(olb->cb, OL(l), a.x0, a.y0, a.x1, a.y1);
}

/* Called by a thunk after the call it stands on: what the call drew, if it
 * drew into a composited layer. regs: d0-d7, a0, a1 as they were. */
static void oc_mark(struct OCB *cb, ULONG kind, const ULONG *regs)
{
    const struct omark *k = &marks[kind];
    struct RastPort *rp = (struct RastPort *)regs[k->rp];
    struct Layer *l;
    struct OLayer *o;
    LONG x0, y0, x1, y1, w, h, t;

    if (!rp || !(l = rp->Layer)) return;
    o = OL(l);
    if (o->magic != OL_MAGIC || !o->surf) return;
    w = l->bounds.MaxX - l->bounds.MinX + 1;
    h = l->bounds.MaxY - l->bounds.MinY + 1;
    x0 = y0 = 0;
    x1 = w - 1;
    y1 = h - 1;
    if (k->type == MK_TEXT) {
        if (rp->Font) {
            y0 = rp->cp_y - rp->Font->tf_Baseline - l->Scroll_Y;
            y1 = y0 + rp->Font->tf_YSize - 1;
        }
    } else if (k->type != MK_WHOLE) {
        /* WORD arguments: only the low words are the caller's */
        LONG a = (WORD)regs[k->a], b = (WORD)regs[k->b], c = (WORD)regs[k->c], d = (WORD)regs[k->d];
        x0 = a;
        y0 = b;
        x1 = k->type == MK_XYXY ? c : a + c - 1;
        y1 = k->type == MK_XYXY ? d : b + d - 1;
        if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
        if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
        x0 -= l->Scroll_X; x1 -= l->Scroll_X;
        y0 -= l->Scroll_Y; y1 -= l->Scroll_Y;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= w) x1 = w - 1;
    if (y1 >= h) y1 = h - 1;
    if (x0 > x1 || y0 > y1) return;
    oc_add(cb, o, x0, y0, x1, y1);
}

/* A thunk, in memory, for each call: the call's own code runs first, with
 * every register as its caller set it, then oc_mark(cb, kind, regs).
 *   movem.l d0-d7/a0-a1,-(sp); jsr old; movem.l d0-d1,-(sp); pea 8(sp);
 *   move.l #kind,-(sp); move.l #cb,-(sp); jsr oc_mark; lea 12(sp),sp;
 *   movem.l (sp)+,d0-d1; lea 40(sp),sp; rts */
static const UWORD thunk_code[OC_THUNK] = {
    0x48E7, 0xFFC0, 0x4EB9, 0, 0, 0x48E7, 0xC000, 0x486F, 0x0008, 0x2F3C, 0, 0, 0x2F3C,
    0, 0, 0x4EB9, 0, 0, 0x4FEF, 0x000C, 0x4CDF, 0x0003, 0x4FEF, 0x0028, 0x4E75,
};

static void put32(UWORD *w, ULONG v)
{
    w[0] = (UWORD)(v >> 16);
    w[1] = (UWORD)v;
}

/* Our thunk on each call, on top of whatever is there. A patch put on top
 * of ours later (OpenGfx's, when OpenRTG hands its drawing to it after
 * boot) may draw without calling ours; then a new thunk goes on top of it.
 * Thunks are never taken out: others may have called on to them. */
static void oc_patch_all(struct OCB *cb)
{
    ULONG i, j;
    for (i = 0; i < OC_NPATCH; i++) {
        struct Library *lib = patches[i].lib == 0 ? (struct Library *)cb->olb->gfx
                            : patches[i].lib == 1 ? cb->cgx : cb->ortg;
        UBYTE *vec;
        APTR cur;
        struct OThunk *t;
        if (!lib) continue;
        vec = (UBYTE *)lib + patches[i].lvo;
        Forbid();
        cur = *(APTR *)(vec + 2);
        if (cur != (APTR)cb->thunk[i] && (t = AllocMem(sizeof *t, MEMF_PUBLIC))) {
            for (j = 0; j < OC_THUNK; j++) t->w[j] = thunk_code[j];
            put32(&t->w[3], (ULONG)cur);
            put32(&t->w[10], patches[i].mark);
            put32(&t->w[13], (ULONG)cb);
            put32(&t->w[16], (ULONG)oc_mark);
            CacheClearU();
            SetFunction(lib, patches[i].lvo, (APTR)t);
            cb->thunk[i] = t;
        }
        Permit();
    }
}

static void oc_lock(struct ol_base *olb, int on)
{
    if (on) ObtainSemaphore(&olb->cb->lock);
    else ReleaseSemaphore(&olb->cb->lock);
}

/* ---- the batch ---------------------------------------------------------- */

static __attribute__((noinline)) void put(struct oc_batch *b, ULONG v)
{
    if (b->n < b->cap) b->w[b->n] = v;
    b->n++;
}

static __attribute__((noinline)) void op_surface(struct oc_batch *b, ULONG slot, ULONG addr, ULONG bpr, ULONG w, ULONG h, ULONG fmt)
{
    put(b, OGPU_HDR(OGPU_OP_SURFACE, 6));
    put(b, slot);
    put(b, addr);
    put(b, bpr);
    put(b, OGPU_XY(w, h));
    put(b, fmt);
}

static __attribute__((noinline)) void op_target(struct oc_batch *b, ULONG slot)
{
    put(b, OGPU_HDR(OGPU_OP_TARGET, 2));
    put(b, slot);
}

static __attribute__((noinline)) void op_xywh(struct oc_batch *b, const struct lc_rect *r)
{
    put(b, OGPU_XY(r->x0, r->y0));
    put(b, OGPU_XY(r->x1 - r->x0 + 1, r->y1 - r->y0 + 1));
}

static __attribute__((noinline)) void op_copy(struct oc_batch *b, ULONG slot, LONG sx, LONG sy, const struct lc_rect *r)
{
    put(b, OGPU_HDR(OGPU_OP_COPY, 5));
    put(b, slot);
    put(b, OGPU_XY(sx, sy));
    op_xywh(b, r);
}

static __attribute__((noinline)) void op_comp(struct oc_batch *b, ULONG slot, LONG sx, LONG sy, LONG sw, LONG sh, const struct lc_rect *r,
                    ULONG alpha, ULONG flags)
{
    put(b, OGPU_HDR(OGPU_OP_COMPOSITE, 8));
    put(b, slot);
    put(b, OGPU_XY(sx, sy));
    put(b, OGPU_XY(sw, sh));
    op_xywh(b, r);
    put(b, alpha);
    put(b, flags);
}

/* ---- the look ------------------------------------------------------------ */

static UBYTE oc_alpha(const struct OComp *c, const struct OLayer *o)
{
    ULONG a = o->opacity;
    if (o->look & LAYERLOOK_DRAGGED) a = a * c->set[SET_DRAG] / 255;
    if (o->look & LAYERLOOK_INACTIVE) a = a * c->set[SET_INACTIVE] / 255;
    return (UBYTE)a;
}

static int oc_opaque(const struct OComp *c, const struct OLayer *o)
{
    return oc_alpha(c, o) == 255 && !(o->look & LAYERLOOK_ALPHA);
}

/* Where l's shadow falls, its bounds b: the box moved R/4 right and R/2
 * down, grown by R. Answers 0 when it has none (or is too small for one:
 * at least 2R + 1 a side). force: as if its shadow were on. */
static int oc_shadow_rect(const struct OComp *c, const struct Layer *l, const struct Rectangle *b, struct lc_rect *sr,
                          int force)
{
    LONG r = c->set[SET_SIZE];
    if (!c->set[SET_SHADOWS] || !c->shadow || (!force && !(OL(l)->look & LAYERLOOK_SHADOW)) || (OL(l)->look & LAYERLOOK_ALPHA))
        return 0;
    if (b->MaxX - b->MinX < 2 * r || b->MaxY - b->MinY < 2 * r) return 0;
    sr->x0 = b->MinX - r + r / 4;
    sr->y0 = b->MinY - r + r / 2;
    sr->x1 = b->MaxX + r + r / 4;
    sr->y1 = b->MaxY + r + r / 2;
    return 1;
}

/* What a change to l at b shows or hides: b and its shadow */
static int oc_ext(const struct OComp *c, const struct Layer *l, const struct Rectangle *b, struct lc_rects *a, int force)
{
    struct lc_rect r = lr(b);
    if (lc_or_rect(a, &r)) return -1;
    if (oc_shadow_rect(c, l, b, &r, force) && lc_or_rect(a, &r)) return -1;
    return 0;
}

/* The shadow's picture: black, its alpha a ramp over 2R + 1 pixels
 * at each side, multiplied across. Its middle row and column are opaque. */
static ULONG *oc_shadow_image(UBYTE r)
{
    ULONG n = 4 * (ULONG)r + 1, i, j, t;
    UBYTE ramp[129];
    ULONG *p = AllocMem(n * n * 4, MEMF_PUBLIC);
    if (!p) return 0;
    for (i = 0; i < n; i++) {
        t = i <= 2 * (ULONG)r ? i + 1 : n - i;
        ramp[i] = (UBYTE)(t * 255 / (2 * (ULONG)r + 1));
    }
    for (j = 0; j < n; j++)
        for (i = 0; i < n; i++) p[j * n + i] = (ULONG)(ramp[i] * ramp[j] / 255) << 24;
    return p;
}

/* l's shadow, at sr, where it meets r: four corners 1:1 from the picture,
 * four edges from its middle row or column, stretched. The middle is
 * under the layer. */
static void oc_shadow(struct oc_batch *b, const struct OComp *c, const struct lc_rect *sr, const struct lc_rect *r)
{
    LONG r2 = 2 * c->set[SET_SIZE], i, j;
    LONG x0[3], x1[3], y0[3], y1[3];
    struct lc_rect d, m;
    x0[0] = sr->x0; x1[0] = sr->x0 + r2 - 1;
    x0[1] = sr->x0 + r2; x1[1] = sr->x1 - r2;
    x0[2] = sr->x1 - r2 + 1; x1[2] = sr->x1;
    y0[0] = sr->y0; y1[0] = sr->y0 + r2 - 1;
    y0[1] = sr->y0 + r2; y1[1] = sr->y1 - r2;
    y0[2] = sr->y1 - r2 + 1; y1[2] = sr->y1;
    for (j = 0; j < 3; j++)
        for (i = 0; i < 3; i++) {
            LONG sx, sy, sw, sh;
            if (i == 1 && j == 1) continue;
            d.x0 = x0[i]; d.x1 = x1[i]; d.y0 = y0[j]; d.y1 = y1[j];
            if (d.x0 > d.x1 || d.y0 > d.y1 || !lc_meet(&d, r, &m)) continue;
            sx = i == 1 ? r2 : (i >> 1) * (r2 + 1) + m.x0 - d.x0;
            sw = i == 1 ? 1 : m.x1 - m.x0 + 1;
            sy = j == 1 ? r2 : (j >> 1) * (r2 + 1) + m.y0 - d.y0;
            sh = j == 1 ? 1 : m.y1 - m.y0 + 1;
            op_comp(b, S_SHADOW, sx, sy, sw, sh, &m, c->set[SET_STRENGTH], OGPU_COMP_SRCALPHA);
        }
}

/* ---- composing ------------------------------------------------------------ */

/* A clip region's or an update's pieces of l hold what was drawn there
 * since they were cut: into its surface first. */
static void oc_sync(struct OCB *cb, struct oc_batch *b, const struct OComp *c, struct Layer *l)
{
    struct OLayer *o = OL(l);
    struct OClip *list, *p;
    int k;
    struct OpenRTGBitMapInfo bi;
    for (k = 0; k < 2; k++)
        for (list = k ? o->update_list : o->region_list, p = list; p; p = (struct OClip *)p->cr.Next) {
            struct lc_rect r = lr(&p->cr.bounds);
            if (p->kind != K_BM || !p->cr.BitMap || !ORTG_BitMapInfo(p->cr.BitMap, &bi)) continue;
            op_surface(b, S_SUB, (ULONG)bi.memory, bi.bytes_per_row, bi.width, bi.height, c->fmt);
            op_target(b, S_SURF);
            r = shift(r, (l->bounds.MinX & 15) - l->bounds.MinX, -l->bounds.MinY);
            op_copy(b, S_SUB, p->cr.bounds.MinX & 15, 0, &r);
            op_target(b, S_SCREEN);
        }
}

/* The batch for a compose: the layers front to back in ord, each one's
 * part to show in v and its shadow's in s; rem, what no opaque layer
 * covers. */
static void oc_build(struct OCB *cb, const struct OComp *c, struct oc_batch *b, struct Layer **ord, ULONG n,
                     struct lc_rects *v, struct lc_rects *s, const struct lc_rects *rem, struct Hook *hook,
                     struct Layer **held)
{
    ULONG i, k, sn = 4 * (ULONG)c->set[SET_SIZE] + 1;
    int any = 0;

    op_surface(b, S_SCREEN, c->saddr, c->sbpr, c->w, c->h, c->fmt);
    /* a layer moved by a number of pixels that changed MinX & 15: its
     * pixels move in its surface first (COPY is overlap safe) */
    for (i = 0; i < n; i++) {
        struct Layer *l = ord[i];
        struct OLayer *o = OL(l);
        struct lc_rect r;
        if (!o->surf || o->soff == (l->bounds.MinX & 15)) continue;
        op_surface(b, S_SURF, o->saddr, o->sbpr, o->sw, o->sh, c->fmt);
        op_target(b, S_SURF);
        r.x0 = l->bounds.MinX & 15;
        r.y0 = 0;
        r.x1 = r.x0 + l->bounds.MaxX - l->bounds.MinX;
        r.y1 = l->bounds.MaxY - l->bounds.MinY;
        op_copy(b, S_SURF, o->soff, 0, &r);
    }
    op_target(b, S_SCREEN);
    put(b, OGPU_HDR(OGPU_OP_CLIP, 3));
    put(b, 0);
    put(b, 0);
    if (hook == LAYERS_BACKFILL)
        for (i = 0; i < rem->n; i++) {
            put(b, OGPU_HDR(OGPU_OP_FILL, 4));
            op_xywh(b, &rem->r[i]);
            put(b, 0);
        }
    for (i = 0; i < n; i++) any |= s[i].n != 0;
    if (any) op_surface(b, S_SHADOW, (ULONG)c->shadow, sn * 4, sn, sn, OGPU_FMT_ARGB32);
    for (i = n; i-- > 0;) {
        struct Layer *l = ord[i];
        struct OLayer *o = OL(l);
        LONG ox = (l->bounds.MinX & 15) - l->bounds.MinX, oy = -l->bounds.MinY;
        UBYTE a = oc_alpha(c, o);
        ULONG argb = o->look & LAYERLOOK_ALPHA;
        if (s[i].n) {
            struct lc_rect sr;
            oc_shadow_rect(c, l, &l->bounds, &sr, 0);
            for (k = 0; k < s[i].n; k++) oc_shadow(b, c, &sr, &s[i].r[k]);
        }
        if (!v[i].n) continue;
        op_surface(b, S_SURF, o->saddr, o->sbpr, o->sw, o->sh, argb ? OGPU_FMT_ARGB32 : c->fmt);
        if (held[i]) oc_sync(cb, b, c, l);
        for (k = 0; k < v[i].n; k++) {
            const struct lc_rect *r = &v[i].r[k];
            if (a == 255 && !argb) op_copy(b, S_SURF, r->x0 + ox, r->y0 + oy, r);
            else op_comp(b, S_SURF, r->x0 + ox, r->y0 + oy, r->x1 - r->x0 + 1, r->y1 - r->y0 + 1, r, a,
                         argb ? OGPU_COMP_SRCALPHA : 0);
        }
    }
}

/* Composes area a (the screen's coordinates; it is cut to the screen) of
 * c's screen, as the stack is now. Holds cb->lock. Answers 0; 1 when a
 * layer's lock was busy, so its part must be composed again; -1 when there
 * was no memory (nothing drawn). */
static int oc_compose(struct OCB *cb, struct OComp *c, struct lc_rects *a)
{
    struct ol_base *olb = cb->olb;
    struct lc_rect scr;
    struct Layer *l, **ord = 0, **held = 0;
    struct lc_rects *v = 0, *s = 0, rem;
    struct oc_batch b;
    struct Hook *hook = c->inf->hook;
    ULONG n = 0, i, fence = 0, size = 0;
    int e, again = 0, pass;
    UBYTE *m = 0;

    scr.x0 = scr.y0 = 0;
    scr.x1 = c->w - 1;
    scr.y1 = c->h - 1;
    lc_and_rect(a, &scr);
    if (!a->n) return 0;
    for (l = c->li->top_layer; l; l = l->back) n++;
    if (n) {
        size = n * (2 * sizeof(struct Layer *) + 2 * sizeof(struct lc_rects));
        if (!(m = AllocMem(size, MEMF_PUBLIC | MEMF_CLEAR))) return -1;
        ord = (struct Layer **)m;
        held = ord + n;
        v = (struct lc_rects *)(held + n);
        s = v + n;
    }
    lc_init(&rem, &mem);
    e = lc_copy(&rem, a);
    /* front to back: each layer shows what no opaque layer in front hides */
    for (l = c->li->top_layer, i = 0; l; l = l->back, i++) {
        struct OLayer *o = OL(l);
        struct lc_rect bb = lr(&l->bounds), sr;
        ord[i] = l;
        lc_init(&v[i], &mem);
        lc_init(&s[i], &mem);
        if (e || o->hidden || !o->surf) continue;
        if (!(e = lc_copy(&v[i], &rem))) lc_and_rect(&v[i], &bb);
        if (!e && oc_shadow_rect(c, l, &l->bounds, &sr, 0)) {
            if (!(e = lc_copy(&s[i], &rem))) {
                lc_and_rect(&s[i], &sr);
                e = lc_sub_rect(&s[i], &bb);
            }
        }
        if (!e && oc_opaque(c, o)) e = lc_sub_rect(&rem, &bb);
    }
    if (e) goto out;
    /* what no layer covers: the Layer_Info's hook draws it now (OGPU's
     * FILL clears it in the batch for LAYERS_BACKFILL) */
    if (hook != LAYERS_BACKFILL && hook != LAYERS_NOBACKFILL) {
        struct place p;
        p.bm = c->screen;
        p.dx = p.dy = 0;
        for (i = 0; i < rem.n; i++) backfill(olb, 0, hook, &p, &rem.r[i]);
    }
    /* a layer with a clip region's or an update's pieces is read under its
     * lock, taken only when it is free; otherwise later */
    for (i = 0; i < n; i++) {
        struct OLayer *o = OL(ord[i]);
        if (!v[i].n || (!o->region_list && !o->update_list)) continue;
        if (AttemptSemaphore(&ord[i]->Lock)) held[i] = ord[i];
        else oc_touch(olb, ord[i], 0), again = 1;
    }
    b.w = 0;
    b.cap = 0;
    for (pass = 0; pass < 2; pass++) {
        b.n = 0;
        oc_build(cb, c, &b, ord, n, v, s, &rem, hook, held);
        if (pass) break;
        b.cap = b.n;
        if (!(b.w = AllocMem(b.cap * 4, MEMF_PUBLIC))) {
            e = -1;
            break;
        }
    }
    if (!e) {
        OGPU_Submit(b.w, b.n, &fence);
        OGPU_Wait(fence);
        cb->composes++;
        for (i = 0; i < n; i++) OL(ord[i])->soff = (UBYTE)(ord[i]->bounds.MinX & 15);
    }
    if (b.w) FreeMem(b.w, b.cap * 4);
    for (i = 0; i < n; i++)
        if (held[i]) ReleaseSemaphore(&held[i]->Lock);
out:
    lc_free(&rem);
    for (i = 0; i < n; i++) {
        lc_free(&v[i]);
        lc_free(&s[i]);
    }
    if (m) FreeMem(m, size);
    if (e) return -1;
    return again;
}

/* The compositor: what was drawn, at the next vertical blank. */
static void oc_run(struct OCB *cb)
{
    struct OComp *c;
    struct Layer *l;
    struct lc_rects a;
    int again = 0;

    lc_init(&a, &mem);
    ObtainSemaphore(&cb->lock);
    Forbid();
    cb->pending = 0;
    Permit();
    oc_patch_all(cb);
    for (c = cb->comps; c; c = c->next) {
        int r = 0;
        lc_clear(&a);
        if (c->full) {
            struct lc_rect all;
            all.x0 = all.y0 = 0;
            all.x1 = c->w - 1;
            all.y1 = c->h - 1;
            c->full = 0;
            r = lc_or_rect(&a, &all);
        }
        for (l = c->li->top_layer; l && !r; l = l->back) {
            struct OLayer *o = OL(l);
            struct lc_rect d;
            Forbid();
            d = o->dirty;
            o->dirty.x0 = 1;
            o->dirty.x1 = 0;
            Permit();
            if (d.x0 > d.x1 || o->hidden) continue;
            d = shift(d, l->bounds.MinX, l->bounds.MinY);
            r = lc_or_rect(&a, &d);
        }
        if (!r && a.n) r = oc_compose(cb, c, &a);
        if (r < 0) c->full = 1;
        if (r) again = 1;
    }
    ReleaseSemaphore(&cb->lock);
    lc_free(&a);
    if (again) {
        Forbid();
        cb->pending = 1;
        Signal(cb->task, OC_SIG);
        Permit();
    }
}

static void oc_task(void)
{
    struct OCB *cb = (struct OCB *)FindTask(0)->tc_UserData;
    struct ol_base *olb = cb->olb;
    for (;;) {
        Wait(OC_SIG);
        WaitTOF();
        oc_run(cb);
    }
}

/* ---- a composited Layer_Info's changes --------------------------------- */

/* A surface for a layer w x h: room for MinX & 15 more, and some to grow
 * into, so sizing a window does not make a new one at every step. */
static int oc_surface(struct OCB *cb, struct OComp *c, LONG w, LONG h, struct OLayer *o)
{
    struct ol_base *olb = cb->olb;
    struct OpenRTGBitMapInfo bi;
    ULONG sw = (ULONG)(w + 15 + 63) & ~63UL, sh = (ULONG)(h + 31) & ~31UL;
    struct BitMap *bm = AllocBitMap(sw, sh, c->depth, BMF_CLEAR, c->screen);
    if (!bm) return 0;
    if (!ORTG_BitMapInfo(bm, &bi) || bi.format != (c->depth == 16 ? 1 : 2) || bi.width < sw || bi.height < sh) {
        FreeBitMap(bm);
        return 0;
    }
    o->nsurf = bm;
    o->naddr = (ULONG)bi.memory;
    o->nbpr = bi.bytes_per_row;
    o->nsw = (UWORD)sw;
    o->nsh = (UWORD)sh;
    return 1;
}

/* What is new to l at r (the screen's coordinates): its super bitmap's
 * pixels or its backfill, into its surface; damage, unless it was just
 * made (as classic layers do). */
static void oc_fill(struct ol_base *olb, struct Layer *l, const struct lc_rect *r, int made)
{
    struct place p = place_of(l, OL(l)->full, 0, 0);
    if ((l->Flags & LAYERSUPER) && l->SuperBitMap) {
        struct OClip sup;
        struct place ps;
        struct lc_rect e, m, out[4];
        uint32_t k, nk = 1;
        sup.kind = K_SUPER;
        ps = place_of(l, &sup, l->bounds.MinX, l->bounds.MinY);
        e = super_ext(olb, l);
        out[0] = *r;
        if (lc_meet(r, &e, &m)) {
            copy(olb, &ps, &p, &m, 0, 0);
            nk = lc_cut(r, &m, out);
        }
        for (k = 0; k < nk; k++) backfill(olb, l, LAYERS_BACKFILL, &p, &out[k]);
    } else {
        backfill(olb, l, l->BackFill, &p, r);
    }
    if (!made) {
        struct Rectangle d;
        d.MinX = (WORD)(r->x0 - l->bounds.MinX); d.MinY = (WORD)(r->y0 - l->bounds.MinY);
        d.MaxX = (WORD)(r->x1 - l->bounds.MinX); d.MaxY = (WORD)(r->y1 - l->bounds.MinY);
        OrRectRegion(l->DamageList, &d);
        l->Flags |= LAYERREFRESH | LAYERIREFRESH | LAYERIREFRESH2;
    }
}

/* This Layer_Info goes back to classic layers in the call under way
 * (finish does the rest, from the surfaces). */
static void oc_demote(struct change *ch)
{
    struct OCB *cb = ch->olb->cb;
    struct OComp *c = ch->comp, **pp;
    c->inf->comp = 0;
    for (pp = &cb->comps; *pp && *pp != c; pp = &(*pp)->next)
        ;
    if (*pp) *pp = c->next;
    ch->demoted = c;
    ch->comp = 0;
}

static void oc_free_comp(struct OComp *c)
{
    if (c->shadow) FreeMem(c->shadow, (4 * (ULONG)c->set[SET_SIZE] + 1) * (4 * (ULONG)c->set[SET_SIZE] + 1) * 4);
    FreeMem(c, sizeof *c);
}

/* After finish: ok, the layers are classic and their surfaces go; not ok
 * (finish put the stack back), the Layer_Info is composited again. */
static void oc_demoted(struct change *ch, int ok)
{
    struct ol_base *olb = ch->olb;
    struct OComp *c = ch->demoted;
    struct Layer *l;
    if (ok) {
        for (l = ch->li->top_layer; l; l = l->back)
            if (OL(l)->surf) {
                FreeBitMap(OL(l)->surf);
                OL(l)->surf = 0;
            }
        oc_free_comp(c);
    } else {
        c->inf->comp = c;
        c->next = olb->cb->comps;
        olb->cb->comps = c;
        ch->comp = c;
    }
    ch->demoted = 0;
}

static void oc_gone(struct ol_base *olb, struct Layer *l)
{
    if (OL(l)->surf) FreeBitMap(OL(l)->surf);
    OL(l)->surf = 0;
}

static void oc_info_drop(struct ol_base *olb, struct OInfo *i)
{
    struct OCB *cb = olb->cb;
    struct OComp **pp, *c = i->comp;
    ObtainSemaphore(&cb->lock);
    for (pp = &cb->comps; *pp && *pp != c; pp = &(*pp)->next)
        ;
    if (*pp) *pp = c->next;
    ReleaseSemaphore(&cb->lock);
    oc_free_comp(c);
    i->comp = 0;
}

/* finish for a composited Layer_Info: every layer gets a surface big
 * enough (or, when one can't be had, the screen goes back to classic
 * layers: answers -2 and finish goes on the classic way), then what the
 * call changed is composed. Answers 0 when done (unlocked). */
static int oc_finish(struct change *ch, struct Layer *made, struct Layer *gone)
{
    struct ol_base *olb = ch->olb;
    struct OCB *cb = olb->cb;
    struct OComp *c = ch->comp;
    struct Layer *l;
    struct lc_rects a;
    int fail = 0, e = 0;

    oc_patch_all(cb);
    /* 1. what needs memory, before anything changes */
    for (l = ch->li->top_layer; l && !fail; l = l->back) {
        struct OLayer *o = OL(l);
        LONG w = l->bounds.MaxX - l->bounds.MinX + 1, h = l->bounds.MaxY - l->bounds.MinY + 1;
        LONG ow = o->oldb.MaxX - o->oldb.MinX + 1, oh = o->oldb.MaxY - o->oldb.MinY + 1;
        int grows = l != made && (w > ow || h > oh), moves = o->surf && o->soff != (l->bounds.MinX & 15);
        /* a layer whose pixels move in its surface while it grows (its
         * backfill would draw where they are still to be read), or while it
         * has a clip region's or an update's pieces (attach cuts them from
         * the surface now), gets a new surface they are copied into at
         * once; otherwise they move in the compose's batch */
        int sub = l->ClipRegion || (l->Flags & LAYERUPDATING);
        o->nsurf = 0;
        o->fresh = 0;
        o->grown = (UBYTE)grows;
        if (!o->full && !(o->fresh = zalloc(sizeof(struct OClip)))) fail = 1;
        else if ((!o->surf || o->sw < w + 15 || o->sh < h || (moves && (grows || sub))) && !oc_surface(cb, c, w, h, o))
            fail = 1;
    }
    if (fail) {
        for (l = ch->li->top_layer; l; l = l->back) {
            if (OL(l)->nsurf) FreeBitMap(OL(l)->nsurf);
            if (OL(l)->fresh) FreeMem(OL(l)->fresh, sizeof(struct OClip));
            OL(l)->nsurf = 0;
            OL(l)->fresh = 0;
        }
        oc_demote(ch);
        return -2;
    }
    /* 2. each layer's one piece, its surface, and what is new to it */
    for (l = ch->li->top_layer; l; l = l->back) {
        struct OLayer *o = OL(l);
        struct OClip *p;
        LONG w = l->bounds.MaxX - l->bounds.MinX + 1, h = l->bounds.MaxY - l->bounds.MinY + 1;
        LONG ow = o->oldb.MaxX - o->oldb.MinX + 1, oh = o->oldb.MaxY - o->oldb.MinY + 1;
        if (o->nsurf) {
            UBYTE off = (UBYTE)(l->bounds.MinX & 15);
            if (o->surf) {
                BltBitMap(o->surf, o->soff, 0, o->nsurf, off, 0, ow < w ? ow : w, oh < h ? oh : h, 0xC0, 0xFF, 0);
                FreeBitMap(o->surf);
            }
            o->surf = o->nsurf;
            o->saddr = o->naddr;
            o->sbpr = o->nbpr;
            o->sw = o->nsw;
            o->sh = o->nsh;
            o->soff = off;
            o->nsurf = 0;
        }
        if (o->fresh) {
            o->full = o->fresh;
            o->fresh = 0;
        }
        p = o->full;
        p->cr.Next = 0;
        p->cr.bounds = l->bounds;
        p->cr.BitMap = o->surf;
        p->cr.obscured = OBSCURED;
        p->cr.Flags |= CR_OPENLAYERS_COMPOSED;  /* nothing covers it: EraseRect clears it (W49) */
        p->kind = K_SURF;
        if (l == made) {
            struct lc_rect r = lr(&l->bounds);
            oc_fill(olb, l, &r, 1);
        } else if (o->grown) {
            struct lc_rect r;
            if (w > ow) {
                r.x0 = l->bounds.MinX + ow; r.x1 = l->bounds.MaxX;
                r.y0 = l->bounds.MinY; r.y1 = l->bounds.MinY + (oh < h ? oh : h) - 1;
                oc_fill(olb, l, &r, 0);
            }
            if (h > oh) {
                r.x0 = l->bounds.MinX; r.x1 = l->bounds.MaxX;
                r.y0 = l->bounds.MinY + oh; r.y1 = l->bounds.MaxY;
                oc_fill(olb, l, &r, 0);
            }
        }
        attach(olb, l);
    }
    /* 3. what the call changed: old and new places, shadows and all */
    lc_init(&a, &mem);
    if (ch->full) {
        struct lc_rect all;
        all.x0 = all.y0 = 0;
        all.x1 = c->w - 1;
        all.y1 = c->h - 1;
        e = lc_or_rect(&a, &all);
    }
    for (l = ch->li->top_layer; l && !e && !ch->full; l = l->back) {
        struct OLayer *o = OL(l);
        int was = l != made && !o->oldhidden, is = !o->hidden;
        if (l != made && l != ch->subject && !moved(l) && was == is) continue;
        if (was) e = oc_ext(c, l, &o->oldb, &a, 0);
        if (is && !e) e = oc_ext(c, l, &l->bounds, &a, 0);
    }
    if (gone && !e && !OL(gone)->oldhidden) e = oc_ext(c, gone, &OL(gone)->oldb, &a, 0);
    if (e || oc_compose(cb, c, &a) < 0) {
        /* no memory: the compositor does all of it when it can */
        c->full = 1;
        Forbid();
        cb->pending = 1;
        Signal(cb->task, OC_SIG);
        Permit();
    }
    lc_free(&a);
    end(ch);
    return 0;
}

/* ---- switching it on, and the prefs --------------------------------------- */

/* oc_prefs' keys, in SET_ order, and how each value is read: 0 on or off,
 * 1 pixels (2-32), 2 percent (as 0-255) */
static const char *const keys[6] = { "composite", "shadows", "shadow.size", "shadow.strength", "drag.opacity",
                                     "inactive.opacity" };
static const UBYTE kinds[6] = { 0, 0, 1, 2, 2, 2 };

/* ENV:OpenPrefs/Windows' lines for the composited mode (the rest are
 * OpenWindows'). A missing file or line keeps the default. Answers 0 when
 * "composite off". */
static int oc_prefs(struct OComp *c)
{
    struct Process *me = (struct Process *)FindTask(0);
    struct Library *dos;
    BPTR fh;
    APTR win;
    LONG n = 0, v;
    char *buf, *p;
    const char *k, *q;
    int i;

    if (me->pr_Task.tc_Node.ln_Type != NT_PROCESS || !(dos = OpenLibrary((CONST_STRPTR)"dos.library", 36))) return 1;
    if ((buf = AllocMem(4096, MEMF_PUBLIC))) {
        win = me->pr_WindowPtr;
        me->pr_WindowPtr = (APTR)-1;            /* no requester when ENV: isn't there yet */
        if ((fh = Open((CONST_STRPTR)"ENV:OpenPrefs/Windows", MODE_OLDFILE))) {
            n = Read(fh, buf, 4095);
            Close(fh);
        }
        me->pr_WindowPtr = win;
        buf[n > 0 ? n : 0] = 0;
        for (p = buf; *p;) {
            for (i = 0; i < 6; i++) {
                for (k = keys[i], q = p; *k && *q == *k; k++, q++)
                    ;
                if (*k || *q != ' ') continue;
                while (*q == ' ') q++;
                for (v = 0; *q >= '0' && *q <= '9'; q++) v = v * 10 + *q - '0';
                if (kinds[i] == 0) v = q[0] == 'o' && q[1] == 'n';
                else if (kinds[i] == 1) v = v < 2 ? 2 : v > 32 ? 32 : v;
                else v = v >= 100 ? 255 : v * 653 >> 8;   /* percent to 0-255 */
                c->set[i] = (UBYTE)v;
            }
            while (*p && *p++ != '\n')
                ;
        }
        FreeMem(buf, 4096);
    }
    CloseLibrary(dos);
    return c->set[SET_ON];
}

/* The composited mode's state, made once: opengpu.library and
 * openrtg.library (from ROM:), and the compositor task. */
static struct OCB *oc_cb(struct ol_base *olb)
{
    struct OCB *cb;
    struct Task *t;
    UBYTE *stack;
    if (olb->cb) return olb->cb;
    if (!(cb = zalloc(sizeof *cb))) return 0;
    InitSemaphore(&cb->lock);
    cb->olb = olb;
    cb->ogpu = OpenLibrary((CONST_STRPTR)OPENGPU_NAME, OPENGPU_VERSION);
    cb->ortg = OpenLibrary((CONST_STRPTR)OPENRTG_NAME, OPENRTG_VERSION);
    cb->cgx = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 0);
    if (!cb->ogpu || !cb->ortg || !(t = zalloc(sizeof *t + OC_STACK))) {
        CloseLibrary(cb->ogpu);
        CloseLibrary(cb->ortg);
        CloseLibrary(cb->cgx);
        FreeMem(cb, sizeof *cb);
        return 0;
    }
    stack = (UBYTE *)(t + 1);
    t->tc_Node.ln_Type = NT_TASK;
    t->tc_Node.ln_Pri = 1;
    t->tc_Node.ln_Name = (char *)"layers.library compositor";
    t->tc_SPLower = stack;
    t->tc_SPUpper = stack + OC_STACK;
    t->tc_SPReg = stack + OC_STACK;
    t->tc_UserData = cb;
    cb->task = t;
    AddTask(t, (APTR)oc_task, 0);
    olb->cb = cb;
    return cb;
}

/* A new composited Layer_Info's state, when its bitmap is an OpenRTG one
 * of 16 or 32 bits, OpenGPU's back end (not the CPU) does COMPOSITE fully
 * for it, and the prefs don't say "composite off". */
static struct OComp *oc_new(struct OCB *cb, struct OInfo *inf, struct Layer_Info *li, struct BitMap *bm)
{
    struct OpenRTGBitMapInfo bi;
    struct OComp *c;
    ULONG q, fmt;
    if (!ORTG_BitMapInfo(bm, &bi) || (bi.format != 1 && bi.format != 2)) return 0;
    fmt = bi.format == 1 ? OGPU_FMT_RGB565 : OGPU_FMT_XRGB32;
    q = OGPU_Query(OGPU_OP_COMPOSITE, fmt);
    if (OGPU_ANSWER(q) != OGPU_FULL || OGPU_BACKEND(q) == OGPU_BACKEND_CPU) return 0;
    if (!(c = zalloc(sizeof *c))) return 0;
    /* the defaults: on, shadows on, 8 pixels, 40%; dragged 75%, inactive 100% */
    c->set[SET_ON] = 1;
    c->set[SET_SHADOWS] = 1;
    c->set[SET_SIZE] = 8;
    c->set[SET_STRENGTH] = 40 * 653 >> 8;
    c->set[SET_DRAG] = 75 * 653 >> 8;
    c->set[SET_INACTIVE] = 255;
    if (!oc_prefs(c)) {
        FreeMem(c, sizeof *c);
        return 0;
    }
    if (c->set[SET_SHADOWS]) c->shadow = oc_shadow_image(c->set[SET_SIZE]);
    c->inf = inf;
    c->li = li;
    c->screen = bm;
    c->saddr = (ULONG)bi.memory;
    c->sbpr = bi.bytes_per_row;
    c->w = bi.width;
    c->h = bi.height;
    c->fmt = (UBYTE)fmt;
    c->depth = bi.format == 1 ? 16 : 32;
    return c;
}

LONG ol_ComposeLayerInfo(struct ol_base *olb, struct Layer_Info *li, struct BitMap *bm, ULONG flags)
{
    struct OInfo *inf = info_of(olb, li, 1);
    struct OCB *cb;
    struct OComp *c;
    struct change ch;

    if (!inf) return FALSE;
    if (inf->comp) {
        cb = olb->cb;
        if (!(flags & LAYERS_COMPOSE_ON)) {
            /* back to classic layers, from the surfaces */
            if (begin(olb, li, &ch)) return TRUE;
            if (ch.comp) oc_demote(&ch);
            finish(&ch, 0, 0);
            return inf->comp != 0;
        }
        return TRUE;
    }
    if (!(flags & LAYERS_COMPOSE_ON) || !bm || li->top_layer) return FALSE;
    /* the first time only from a process: opengpu.library may come from
     * ROM:, through DOS, and OpenGPU loads its driver for a process */
    if (!olb->cb && FindTask(0)->tc_Node.ln_Type != NT_PROCESS) return FALSE;
    ObtainSemaphore(&olb->infolock);
    cb = oc_cb(olb);
    ReleaseSemaphore(&olb->infolock);
    if (!cb || !(c = oc_new(cb, inf, li, bm))) return FALSE;
    ObtainSemaphore(&cb->lock);
    c->next = cb->comps;
    cb->comps = c;
    inf->comp = c;
    oc_patch_all(cb);
    ReleaseSemaphore(&cb->lock);
    return TRUE;
}

/* What a layer looks like: its opacity as composed, and whether it has a
 * shadow and its own alpha. A change of anything else draws nothing. */
static ULONG oc_seen(const struct OComp *c, const struct OLayer *o)
{
    return oc_alpha(c, o) | (ULONG)(o->look & (LAYERLOOK_SHADOW | LAYERLOOK_ALPHA)) << 8;
}

ULONG ol_SetLayerLook(struct ol_base *olb, struct Layer *l, ULONG what, ULONG value)
{
    struct OCB *cb = olb->cb;
    struct OLayer *o = OL(l);
    struct OComp *c;
    struct lc_rects a;
    ULONG old, seen;

    if (!cb || !l) return ~0UL;
    ObtainSemaphore(&cb->lock);
    for (c = cb->comps; c && c->li != l->LayerInfo; c = c->next)
        ;
    if (!c || !o->surf) {
        ReleaseSemaphore(&cb->lock);
        return ~0UL;
    }
    seen = oc_seen(c, o);
    old = what == LAYERLOOK_OPACITY ? o->opacity
        : what >= LAYERLOOK_QUERY ? o->opacity | (ULONG)o->look << 8 | 1UL << 16 : (o->look & what) != 0;
    if (what == LAYERLOOK_OPACITY) {
        o->opacity = (UBYTE)(value > 255 ? 255 : value);
    } else if (what == LAYERLOOK_ALL) {
        o->opacity = (UBYTE)value;
        o->look = (UBYTE)(value >> 8);
    } else if (what < LAYERLOOK_QUERY) {
        if (value) o->look |= (UBYTE)what;
        else o->look &= (UBYTE)~what;
    }
    /* an ARGB32 surface only where the surfaces are 32-bit already */
    if (c->depth != 32) o->look &= (UBYTE)~LAYERLOOK_ALPHA;
    if (!o->hidden && oc_seen(c, o) != seen) {
        lc_init(&a, &mem);
        if (!oc_ext(c, l, &l->bounds, &a, 1)) oc_compose(cb, c, &a);
        lc_free(&a);
    }
    ReleaseSemaphore(&cb->lock);
    return old;
}

LONG ol_SetLayerOpacity(struct ol_base *olb, struct Layer *l, ULONG opacity)
{
    return (LONG)ol_SetLayerLook(olb, l, LAYERLOOK_OPACITY, opacity);
}
