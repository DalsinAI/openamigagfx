/* SPDX-License-Identifier: MIT */
/* OpenLayers' calls (layers_lib.h). Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Clean-room, from the autodocs, the NDK 3.2 headers' public structures and
 * OS 3.2's answers measured by tests/LayersABITest.c; never from Kickstart's
 * code. AROS was read for ideas only.
 *
 * How it works. Every call that changes the stack (create, delete, depth,
 * move, size, hide, show, bounds) does the same three things:
 *  1. begin: lock everything, note each layer's place, bounds and whether
 *     it is hidden, and put back each layer's full ClipRect list (a clip
 *     region's or an update's list is copied back into it first);
 *  2. the call changes the stack, bounds or hidden flag;
 *  3. finish: the core cuts every layer into new pieces; each gets a
 *     ClipRect, and each obscured piece of a smart refresh layer a backing
 *     bitmap. Then the pixels move, in an order where nothing is read
 *     after it was overwritten:
 *       a. everything that goes into a backing bitmap or a super bitmap
 *          (from the screen or an old bitmap);
 *       b. the moving layer's own pixels across the screen, ordered by
 *          the core so no copy writes over one still to come;
 *       c. everything coming back to the screen from a bitmap;
 *       d. what nobody has: the layer's backfill hook draws it, and it goes
 *          into the layer's DamageList (not for a layer just made); the
 *          screen no layer covers any more gets the Layer_Info's hook.
 *     The old lists go, the new ones go in, and each layer's clip region
 *     and update lists are made again. If memory runs out before any pixel
 *     has moved, the stack goes back as it was and the call fails.
 *
 * Where a piece's pixels are: on the screen at its bounds (visible), or in
 * its backing bitmap at (x - (MinX & ~15), y - MinY) as graphics.library
 * reads an obscured ClipRect; a super bitmap layer's obscured pieces have
 * backing bitmaps too (as OS 3.2's do), filled from the super bitmap (the
 * layer's own coordinates plus Scroll_X and Scroll_Y) where they are new
 * to the layer; SyncSBitMap brings them all back to it. A simple refresh
 * layer keeps nothing that is not on the screen.
 *
 * DamageList rectangles are relative to the layer's top left; a clip
 * region is in the RastPort's coordinates (the layer's own plus Scroll_X
 * and Scroll_Y), as graphics.library draws.
 *
 * Multicore: every call here is anchored (plan section 5): it runs where
 * the display's owner runs, holding the Layer_Info's and the layers'
 * semaphores as OS 3.2 does. */
#define NEWCLIPRECTS_1_1                /* struct ClipRect's Flags: CR_OPENLAYERS_COMPOSED */
#ifndef CR_OPENLAYERS_COMPOSED
#define CR_OPENLAYERS_COMPOSED  0x40000000L   /* as include/graphics/openlayers.h */
#endif
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <graphics/gfx.h>
#include <graphics/clip.h>
#include <graphics/layers.h>
#include <graphics/rastport.h>
#include <graphics/regions.h>
#include <utility/hooks.h>
#include "../../include/oa_protos_begin.h"   /* the protos' unused varargs wrappers dropped */
#include <proto/exec.h>
/* graphics.library's base is the library's (struct ol_base), never a
 * global: every function here that calls graphics has olb in scope */
#define GRAPHICS_BASE_NAME (olb->gfx)
#include <proto/graphics.h>
#include "../../include/oa_protos_end.h"

#include "layers_core.h"
#include "layers_lib.h"
#include "ol_sysbase.h"

/* NDK 3.2's ClipRect says obscured with a LONG, zero or not (it was lobs,
 * the layer in front, before V45). OS 3.2 puts 1 there (measured). */
#define OBSCURED 1

/* K_SURF: a composited layer's one piece, whose BitMap is the layer's
 * surface (layers_comp.c): placed as a K_BM piece is, never freed with the
 * list. */
enum { K_SCREEN, K_BM, K_SUPER, K_NONE, K_SURF };

/* Our ClipRects: graphics.library's, then what is ours. */
struct OClip {
    struct ClipRect cr;
    struct OClip *parent;       /* a clip region's or update's piece: the full list's piece it came from */
    UBYTE kind;
    UBYTE stolen;               /* its BitMap went to the new list's piece of the same bounds */
};

struct OLayer {
    struct Layer l;
    struct RastPort rp;
    struct OClip *full;         /* the whole layer's pieces; owns the backing bitmaps */
    struct OClip *region_list;  /* full, cut to the clip region */
    struct OClip *update_list;  /* the list in use, cut to the damage, during BeginUpdate */
    struct OClip *fresh;        /* finish's new full list */
    struct lc_rects movers;     /* finish's screen copies, where from, in order */
    struct lc_rects fills;      /* finish's pixels nobody has, on the screen */
    struct lc_rects fills_y;    /* the same, rows first, for a moved simple layer itself */
    struct Rectangle oldb;
    UBYTE hidden, oldhidden;
#ifdef OL_COMPOSITE
    ULONG magic;                /* OL_MAGIC: one of ours (the drawing marks check it) */
    struct BitMap *surf;        /* composited: the layer's surface; 0 for a classic layer */
    ULONG saddr, sbpr;          /* its pixel 0,0 and bytes a row (ORTG_BitMapInfo) */
    struct BitMap *nsurf;       /* finish's new surface, while it is being made */
    ULONG naddr, nbpr;
    UWORD sw, sh, nsw, nsh;     /* their sizes */
    UBYTE soff;                 /* the layer's pixels start at x soff in it: MinX & 15 when they were put there */
    UBYTE opacity;              /* 0-255, SetLayerOpacity's */
    UBYTE look;                 /* OLOOK_ bits (layers_comp.c) */
    UBYTE grown;
    struct lc_rect dirty;       /* drawn since the last compose, from the layer's top left; x0 > x1 for none */
#endif
};

/* What a Layer_Info has that its structure has no room for. */
struct OInfo {
    struct OInfo *next;
    struct Layer_Info *li;
    struct Hook *hook;          /* InstallLayerInfoHook's; LAYERS_BACKFILL clears */
    struct Rectangle bounds;    /* SetLayerInfoBounds' */
    UBYTE have_bounds;
    UWORD fattened;             /* FattenLayerInfo less ThinLayerInfo */
#ifdef OL_COMPOSITE
    struct OComp *comp;         /* composited: its state (layers_comp.c); 0 for classic layers */
    UBYTE why;                  /* classic: why (LAYERS_WHY_), for LAYERS_COMPOSE_WHY; 0 when never asked */
#endif
};

#ifdef OL_COMPOSITE
struct change;
struct OComp;
static int oc_finish(struct change *ch, struct Layer *made, struct Layer *gone);
static void oc_touch(struct ol_base *olb, struct Layer *l, const struct lc_rect *r);
static void oc_demoted(struct change *ch, int ok);
static void oc_gone(struct ol_base *olb, struct Layer *l);
static void oc_info_drop(struct ol_base *olb, struct OInfo *i);
static void oc_lock(struct ol_base *olb, int on);
#define OL_MAGIC 0x4F4C4159UL   /* 'OLAY' */
#endif

#define OL(l) ((struct OLayer *)(l))

/* OS 3.2 marks a hidden layer in its Flags with this bit */
#define OL_HIDDEN 0x1000

static void set_hidden(struct Layer *l, UBYTE h)
{
    OL(l)->hidden = h;
    if (h) l->Flags |= OL_HIDDEN;
    else l->Flags &= ~OL_HIDDEN;
}

/* ---- memory ------------------------------------------------------------ */

static void *m_alloc(void *ctx, uint32_t size) { (void)ctx; return AllocMem(size, MEMF_PUBLIC); }
static void m_free(void *ctx, void *p, uint32_t size) { (void)ctx; FreeMem(p, size); }
static const struct lc_mem mem = { m_alloc, m_free, 0 };

static void *zalloc(ULONG size) { return AllocMem(size, MEMF_PUBLIC | MEMF_CLEAR); }

void ol_init(struct ol_base *olb, struct GfxBase *gfx)
{
    olb->gfx = gfx;
    InitSemaphore(&olb->infolock);
    olb->infos = 0;
#ifdef OL_COMPOSITE
    olb->cb = 0;
#endif
}

static struct OInfo *info_of(struct ol_base *olb, struct Layer_Info *li, int make)
{
    struct OInfo *i;
    ObtainSemaphore(&olb->infolock);
    for (i = olb->infos; i && i->li != li; i = i->next)
        ;
    if (!i && make && (i = zalloc(sizeof *i))) {
        i->li = li;
        i->next = olb->infos;
        olb->infos = i;
    }
    ReleaseSemaphore(&olb->infolock);
    return i;
}

static void info_drop(struct ol_base *olb, struct Layer_Info *li)
{
    struct OInfo **pp, *i;
    ObtainSemaphore(&olb->infolock);
    for (pp = &olb->infos; (i = *pp) && i->li != li; pp = &i->next)
        ;
    if (i) *pp = i->next;
    ReleaseSemaphore(&olb->infolock);
    if (i) {
#ifdef OL_COMPOSITE
        if (i->comp) oc_info_drop(olb, i);
#endif
        FreeMem(i, sizeof *i);
    }
}

/* ---- rectangles -------------------------------------------------------- */

static struct lc_rect lr(const struct Rectangle *r)
{
    struct lc_rect o;
    o.x0 = r->MinX; o.y0 = r->MinY; o.x1 = r->MaxX; o.y1 = r->MaxY;
    return o;
}

static struct Rectangle gr(const struct lc_rect *r)
{
    struct Rectangle o;
    o.MinX = (WORD)r->x0; o.MinY = (WORD)r->y0; o.MaxX = (WORD)r->x1; o.MaxY = (WORD)r->y1;
    return o;
}

static struct lc_rect shift(struct lc_rect r, LONG dx, LONG dy)
{
    r.x0 += dx; r.x1 += dx; r.y0 += dy; r.y1 += dy;
    return r;
}

/* A region's rectangles into s, moved by (dx, dy). */
static int region_set(const struct Region *rg, LONG dx, LONG dy, struct lc_rects *s)
{
    const struct RegionRectangle *rr;
    lc_clear(s);
    if (!rg) return 0;
    for (rr = rg->RegionRectangle; rr; rr = rr->Next) {
        struct lc_rect r = lr(&rr->bounds);
        r = shift(r, rg->bounds.MinX + dx, rg->bounds.MinY + dy);
        if (lc_or_rect(s, &r)) return -1;
    }
    return 0;
}

/* ---- where pixels are -------------------------------------------------- */

struct place {
    struct BitMap *bm;
    LONG dx, dy;                /* bitmap coordinates = screen coordinates + (dx, dy) */
};

/* The place of a piece of l whose top left was at (ox, oy) on the screen. */
static struct place place_of(struct Layer *l, const struct OClip *c, LONG ox, LONG oy)
{
    struct place p;
    switch (c->kind) {
    case K_BM:
    case K_SURF:
        p.bm = c->cr.BitMap;
        p.dx = -(LONG)(c->cr.bounds.MinX & ~15);
        p.dy = -(LONG)c->cr.bounds.MinY;
        break;
    case K_SUPER:
        p.bm = l->SuperBitMap;
        p.dx = l->Scroll_X - ox;
        p.dy = l->Scroll_Y - oy;
        break;
    default:
        p.bm = l->rp->BitMap;
        p.dx = p.dy = 0;
    }
    return p;
}

/* Where a super bitmap layer's super bitmap reaches, in screen
 * coordinates. */
static struct lc_rect super_ext(struct ol_base *olb, struct Layer *l)
{
    struct lc_rect e;
    e.x0 = l->bounds.MinX - l->Scroll_X;
    e.y0 = l->bounds.MinY - l->Scroll_Y;
    e.x1 = e.x0 + (LONG)GetBitMapAttr(l->SuperBitMap, BMA_WIDTH) - 1;
    e.y1 = e.y0 + (LONG)GetBitMapAttr(l->SuperBitMap, BMA_HEIGHT) - 1;
    return e;
}

/* Copies r (screen coordinates where it lands) from s, where it was
 * (ddx, ddy) away, to d. */
static void copy(struct ol_base *olb, const struct place *s, const struct place *d, const struct lc_rect *r, LONG ddx,
                 LONG ddy)
{
    BltBitMap(s->bm, r->x0 - ddx + s->dx, r->y0 - ddy + s->dy, d->bm, r->x0 + d->dx, r->y0 + d->dy,
              r->x1 - r->x0 + 1, r->y1 - r->y0 + 1, 0xC0, 0xFF, 0);
}

/* Fills r of a place with a hook: l's (or the Layer_Info's, l 0). */
static void backfill_with(struct ol_base *olb, struct Layer *l, struct RastPort *from, struct Hook *hook,
                          const struct place *p, const struct lc_rect *r)
{
    struct ol_hookmsg msg;
    struct RastPort rp;

    if (hook == LAYERS_NOBACKFILL) return;
    if (hook == LAYERS_BACKFILL) {
        BltBitMap(p->bm, r->x0 + p->dx, r->y0 + p->dy, p->bm, r->x0 + p->dx, r->y0 + p->dy,
                  r->x1 - r->x0 + 1, r->y1 - r->y0 + 1, 0x00, 0xFF, 0);
        return;
    }
    /* a copy of from (or a bare RastPort) on the bitmap to fill */
    if (from) rp = *from;
    else InitRastPort(&rp);
    rp.BitMap = p->bm;
    msg.Layer = l;
    msg.Bounds.MinX = (WORD)(r->x0 + p->dx);
    msg.Bounds.MinY = (WORD)(r->y0 + p->dy);
    msg.Bounds.MaxX = (WORD)(r->x1 + p->dx);
    msg.Bounds.MaxY = (WORD)(r->y1 + p->dy);
    msg.OffsetX = l ? r->x0 - l->bounds.MinX + l->Scroll_X : r->x0;
    msg.OffsetY = l ? r->y0 - l->bounds.MinY + l->Scroll_Y : r->y0;
    ol_call_hook(hook, &rp, &msg);
}

/* As OS 3.2 calls a layer's hook: the layer's own RastPort, Layer and all
 * (a hook draws with Layer cleared, in its own copy); the Layer_Info's
 * hook, l 0, a bare one. */
static void backfill(struct ol_base *olb, struct Layer *l, struct Hook *hook, const struct place *p,
                     const struct lc_rect *r)
{
    backfill_with(olb, l, l ? l->rp : 0, hook, p, r);
}

/* ---- ClipRect lists ---------------------------------------------------- */

static struct BitMap *backing(struct ol_base *olb, struct Layer *l, const struct lc_rect *r)
{
    struct BitMap *scr = l->rp->BitMap;
    return AllocBitMap((ULONG)(r->x1 - (r->x0 & ~15) + 1), (ULONG)(r->y1 - r->y0 + 1),
                       GetBitMapAttr(scr, BMA_DEPTH), 0, scr);
}

static void free_list(struct ol_base *olb, struct OClip *c)
{
    while (c) {
        struct OClip *n = (struct OClip *)c->cr.Next;
        if (c->kind == K_BM && c->cr.BitMap && !c->stolen) FreeBitMap(c->cr.BitMap);
        FreeMem(c, sizeof *c);
        c = n;
    }
}

/* Appends a piece to a list being built. */
static struct OClip *add_clip(struct OClip **head, struct OClip **tail, const struct lc_rect *r, UBYTE kind)
{
    struct OClip *c = zalloc(sizeof *c);
    if (!c) return 0;
    c->cr.bounds = gr(r);
    c->kind = kind;
    if (*tail) (*tail)->cr.Next = &c->cr;
    else *head = c;
    *tail = c;
    return c;
}

/* The pieces of list cut to set (screen coordinates), each with its own
 * copy of a backing bitmap's pixels. */
static int make_sub(struct ol_base *olb, struct Layer *l, struct OClip *list, const struct lc_rects *set,
                    struct OClip **out)
{
    struct OClip *head = 0, *tail = 0, *c;
    uint32_t i;
    struct lc_rect m;

    for (c = list; c; c = (struct OClip *)c->cr.Next) {
        struct lc_rect b = lr(&c->cr.bounds);
        for (i = 0; i < set->n; i++) {
            struct OClip *s;
            if (!lc_meet(&b, &set->r[i], &m)) continue;
            /* a surface's piece is cut as a backing bitmap's: its own
             * bitmap, copied back to the surface by drop_sub */
            if (!(s = add_clip(&head, &tail, &m, c->kind == K_SURF ? K_BM : c->kind))) goto fail;
            s->parent = c;
            s->cr.obscured = c->cr.obscured;
            if (c->kind == K_SURF) s->cr.Flags |= CR_OPENLAYERS_COMPOSED;
            if (s->kind == K_BM) {
                struct place ps, pd;
                if (!(s->cr.BitMap = backing(olb, l, &m))) goto fail;
                ps = place_of(l, c, 0, 0);
                pd = place_of(l, s, 0, 0);
                copy(olb, &ps, &pd, &m, 0, 0);
            }
        }
    }
    *out = head;
    return 0;
fail:
    free_list(olb, head);
    *out = 0;
    return -1;
}

/* Puts a cut list's pixels back into the pieces it came from, and frees it. */
static void drop_sub(struct ol_base *olb, struct Layer *l, struct OClip *list)
{
    struct OClip *c;
    for (c = list; c; c = (struct OClip *)c->cr.Next)
        if (c->kind == K_BM && c->cr.BitMap && c->parent) {
            struct lc_rect b = lr(&c->cr.bounds);
            struct place ps = place_of(l, c, 0, 0), pd = place_of(l, c->parent, 0, 0);
            copy(olb, &ps, &pd, &b, 0, 0);
#ifdef OL_COMPOSITE
            if (c->parent->kind == K_SURF) oc_touch(olb, l, &b);
#endif
        }
    free_list(olb, list);
}

/* The layer's list in use goes back to its full list. */
static void detach(struct ol_base *olb, struct Layer *l)
{
    struct OLayer *o = OL(l);
    if (o->update_list) drop_sub(olb, l, o->update_list);
    if (o->region_list) drop_sub(olb, l, o->region_list);
    o->update_list = o->region_list = 0;
    l->ClipRect = o->full ? &o->full->cr : 0;
}

/* The clip region's list, then the update's, made again from the full one. */
static void attach(struct ol_base *olb, struct Layer *l)
{
    struct OLayer *o = OL(l);
    struct OClip *base = o->full;
    struct lc_rects s;

    lc_init(&s, &mem);
    l->Flags &= ~LAYER_CLIPRECTS_LOST;
    if (l->ClipRegion) {
        if (region_set(l->ClipRegion, l->bounds.MinX - l->Scroll_X, l->bounds.MinY - l->Scroll_Y, &s) ||
            make_sub(olb, l, base, &s, &o->region_list))
            l->Flags |= LAYER_CLIPRECTS_LOST;
        else
            base = o->region_list;
    }
    if (l->Flags & LAYERUPDATING) {
        if (region_set(l->DamageList, l->bounds.MinX, l->bounds.MinY, &s) ||
            make_sub(olb, l, base, &s, &o->update_list))
            l->Flags |= LAYER_CLIPRECTS_LOST;
        l->ClipRect = o->update_list ? &o->update_list->cr : 0;
    } else {
        l->ClipRect = base ? &base->cr : 0;
    }
    lc_free(&s);
}

/* ---- the stack --------------------------------------------------------- */

static int backdrop(const struct Layer *l) { return (l->Flags & LAYERBACKDROP) != 0; }

static void unlink_layer(struct Layer_Info *li, struct Layer *l)
{
    if (l->front) l->front->back = l->back;
    else li->top_layer = l->back;
    if (l->back) l->back->front = l->front;
    l->front = l->back = 0;
}

/* Puts l in front of at (0: at the back). */
static void link_before(struct Layer_Info *li, struct Layer *l, struct Layer *at)
{
    struct Layer *f;
    if (at) {
        f = at->front;
        at->front = l;
    } else {
        for (f = li->top_layer; f && f->back; f = f->back)
            ;
    }
    l->back = at;
    l->front = f;
    if (f) f->back = l;
    else li->top_layer = l;
}

static struct Layer *first_backdrop(struct Layer_Info *li, struct Layer *skip)
{
    struct Layer *l;
    for (l = li->top_layer; l && (l == skip || !backdrop(l)); l = l->back)
        ;
    return l;
}

/* Where l goes at the front, or the back, of its kind. */
static struct Layer *front_spot(struct Layer_Info *li, struct Layer *l)
{
    return backdrop(l) ? first_backdrop(li, l) : li->top_layer;
}
static struct Layer *back_spot(struct Layer_Info *li, struct Layer *l)
{
    return backdrop(l) ? 0 : first_backdrop(li, l);
}

static ULONG count_layers(struct Layer_Info *li)
{
    ULONG n = 0;
    struct Layer *l;
    for (l = li->top_layer; l; l = l->back) n++;
    return n;
}

/* ---- begin and finish -------------------------------------------------- */

struct change {
    struct ol_base *olb;
    struct Layer_Info *li;
    struct Layer **old;         /* the stack before, front to back */
    ULONG nold;
    struct lc_rect screen;
    int have_screen;
#ifdef OL_COMPOSITE
    struct OComp *comp;         /* the Layer_Info is composited */
    struct OComp *demoted;      /* it was, and goes back to classic layers in this call */
    struct Layer *subject;      /* the layer a depth, hide or show call is about */
    UBYTE locked, full;         /* the composited mode's lock is held; everything is to be composed */
#endif
};

static void lock_info_all(struct Layer_Info *li)
{
    struct Layer *l;
    ObtainSemaphore(&li->Lock);
    for (l = li->top_layer; l; l = l->back) ObtainSemaphore(&l->Lock);
}

/* Locks everything and notes the stack. Answers -1 when there is no memory
 * for the note (nothing is locked then). */
static int begin(struct ol_base *olb, struct Layer_Info *li, struct change *ch)
{
    struct Layer *l;
    struct OInfo *inf;
    ULONG i;

    lock_info_all(li);
    ch->olb = olb;
    ch->li = li;
    ch->nold = count_layers(li);
    ch->old = 0;
    if (ch->nold && !(ch->old = AllocMem(ch->nold * sizeof *ch->old, MEMF_PUBLIC))) {
        for (l = li->top_layer; l; l = l->back) ReleaseSemaphore(&l->Lock);
        ReleaseSemaphore(&li->Lock);
        return -1;
    }
    for (l = li->top_layer, i = 0; l; l = l->back, i++) {
        ch->old[i] = l;
        OL(l)->oldb = l->bounds;
        OL(l)->oldhidden = OL(l)->hidden;
        detach(olb, l);
    }
    inf = info_of(olb, li, 0);
#ifdef OL_COMPOSITE
    /* a composited Layer_Info's stack changes under the composited mode's
     * lock too, taken last, so its compositor sees it whole */
    ch->comp = inf ? inf->comp : 0;
    ch->demoted = 0;
    ch->subject = 0;
    ch->full = 0;
    if ((ch->locked = ch->comp != 0)) oc_lock(olb, 1);
#endif
    ch->have_screen = 0;
    if (inf && inf->have_bounds) {
        ch->screen = lr(&inf->bounds);
        ch->have_screen = 1;
    } else if (ch->nold) {
        struct BitMap *bm = ch->old[0]->rp->BitMap;
        ch->screen.x0 = ch->screen.y0 = 0;
        ch->screen.x1 = (LONG)GetBitMapAttr(bm, BMA_WIDTH) - 1;
        ch->screen.y1 = (LONG)GetBitMapAttr(bm, BMA_HEIGHT) - 1;
        ch->have_screen = 1;
    }
    return 0;
}

/* Unlocks what begin locked (and the layers made since, which were never
 * locked, stay so). */
static void end(struct change *ch)
{
    ULONG i;
#ifdef OL_COMPOSITE
    if (ch->locked) oc_lock(ch->olb, 0);
#endif
    for (i = 0; i < ch->nold; i++) ReleaseSemaphore(&ch->old[i]->Lock);
    ReleaseSemaphore(&ch->li->Lock);
    if (ch->old) FreeMem(ch->old, ch->nold * sizeof *ch->old);
}

/* The stack as the core sees it: now (old 0) or before (old 1). */
static struct lc_layer *stack_of(struct change *ch, int old, ULONG *n, struct Layer ***order)
{
    struct lc_layer *st;
    struct Layer **ord, *l;
    ULONG i, k = old ? ch->nold : count_layers(ch->li);

    *n = k;
    *order = 0;
    if (!k) return 0;
    st = AllocMem(k * sizeof *st, MEMF_PUBLIC);
    ord = AllocMem(k * sizeof *ord, MEMF_PUBLIC);
    if (!st || !ord) {
        if (st) FreeMem(st, k * sizeof *st);
        if (ord) FreeMem(ord, k * sizeof *ord);
        return 0;
    }
    for (i = 0, l = ch->li->top_layer; i < k; i++) {
        struct Layer *x = old ? ch->old[i] : l;
        ord[i] = x;
        st[i].bounds = lr(old ? &OL(x)->oldb : &x->bounds);
        st[i].flags = (old ? OL(x)->oldhidden : OL(x)->hidden) ? LC_HIDDEN : 0;
        if (!old) l = l->back;
    }
    *order = ord;
    return st;
}

static void free_stack(struct lc_layer *st, struct Layer **ord, ULONG n)
{
    if (st) FreeMem(st, n * sizeof *st);
    if (ord) FreeMem(ord, n * sizeof *ord);
}

/* Layer ord[idx]'s new full list, into its fresh. */
static int build(struct change *ch, struct lc_layer *st, struct Layer **ord, ULONG n, ULONG idx)
{
    struct ol_base *olb = ch->olb;
    struct Layer *l = ord[idx];
    struct OLayer *o = OL(l);
    struct lc_pieces p;
    struct OClip *head = 0, *tail = 0, *c, *old;
    LONG ddx = l->bounds.MinX - o->oldb.MinX, ddy = l->bounds.MinY - o->oldb.MinY;
    uint32_t i;

    o->fresh = 0;
    lc_pieces_init(&p, &mem);
    if (lc_layer_pieces(st, n, idx, ch->have_screen ? &ch->screen : 0, &p)) goto fail;
    for (i = 0; i < p.n; i++) {
        int32_t ob = p.p[i].obscurer;
        UBYTE kind;
        if (ob == LC_VISIBLE && !o->hidden) kind = K_SCREEN;
        else if (l->Flags & (LAYERSMART | LAYERSUPER)) kind = K_BM;
        else continue;  /* OS 3.2 gives a simple refresh layer no ClipRect where it is not on show */
        if (!(c = add_clip(&head, &tail, &p.p[i].r, kind))) goto fail;
        if (kind != K_SCREEN) c->cr.obscured = OBSCURED;
        if (kind != K_BM) continue;
        /* an old backing bitmap of the same bounds, where the layer has not
         * moved, is taken over as it is */
        if (!ddx && !ddy)
            for (old = o->full; old; old = (struct OClip *)old->cr.Next)
                if (old->kind == K_BM && !old->stolen && old->cr.bounds.MinX == c->cr.bounds.MinX &&
                    old->cr.bounds.MinY == c->cr.bounds.MinY && old->cr.bounds.MaxX == c->cr.bounds.MaxX &&
                    old->cr.bounds.MaxY == c->cr.bounds.MaxY) {
                    old->stolen = 1;
                    c->cr.BitMap = old->cr.BitMap;
                    c->parent = old;    /* finish copies nothing into it */
                    break;
                }
        if (!c->cr.BitMap && !(c->cr.BitMap = backing(olb, l, &p.p[i].r))) goto fail;
    }
    lc_pieces_free(&p);
    o->fresh = head;
    return 0;
fail:
    lc_pieces_free(&p);
    for (c = head; c; c = (struct OClip *)c->cr.Next)
        if (c->parent) { c->parent->stolen = 0; c->stolen = 1; }
    free_list(olb, head);
    return -1;
}

/* Whether a layer's bounds changed since begin. */
static int moved(struct Layer *l)
{
    struct Rectangle *a = &l->bounds, *b = &OL(l)->oldb;
    return a->MinX != b->MinX || a->MinY != b->MinY || a->MaxX != b->MaxX || a->MaxY != b->MaxY;
}

/* What finish will do for layer l that needs memory, worked out before any
 * pixel moves: the screen copies of phase b (where each comes from, in an
 * order where none writes over one still to come) and the pixels of phase
 * d that none of its old pieces had. */
static int plan(struct Layer *l, int made)
{
    struct OLayer *o = OL(l);
    struct OClip *n, *c;
    LONG ddx = l->bounds.MinX - o->oldb.MinX, ddy = l->bounds.MinY - o->oldb.MinY;
    struct lc_rect m;
    struct lc_rects one;

    lc_init(&o->movers, &mem);
    lc_init(&o->fills, &mem);
    lc_init(&o->fills_y, &mem);
    lc_init(&one, &mem);
    for (n = o->fresh; n; n = (struct OClip *)n->cr.Next) {
        struct lc_rect nb = lr(&n->cr.bounds);
        int pass;
        if (n->kind == K_SCREEN && (ddx || ddy))
            for (c = o->full; c; c = (struct OClip *)c->cr.Next) {
                struct lc_rect ob = shift(lr(&c->cr.bounds), ddx, ddy);
                if (c->kind != K_SCREEN || !lc_meet(&nb, &ob, &m)) continue;
                m = shift(m, -ddx, -ddy);
                if (lc_or_rect(&o->movers, &m)) goto fail;
            }
        if (n->kind == K_NONE || n->kind == K_SUPER || n->parent) continue;
        /* a moved or sized layer's new part goes to the hook columns first,
         * the rest rows first, as OS 3.2 does; a moved simple refresh
         * layer's goes a second time, to itself, rows first (fills_y) */
        for (pass = 0; pass < 2; pass++) {
            int cols = moved(l) && !pass;
            if (pass && (made || !moved(l) || (l->Flags & (LAYERSMART | LAYERSUPER)))) break;
            if (lc_set(&one, &nb)) goto fail;
            if (!made)
                for (c = o->full; c && one.n; c = (struct OClip *)c->cr.Next) {
                    struct lc_rect ob;
                    if (c->kind == K_NONE) continue;
                    ob = shift(lr(&c->cr.bounds), ddx, ddy);
                    if ((cols ? lc_sub_rect_x : lc_sub_rect)(&one, &ob)) goto fail;
                }
            if (lc_or(pass ? &o->fills_y : &o->fills, &one)) goto fail;
        }
    }
    lc_sort_for_move(o->movers.r, o->movers.n, ddx, ddy);
    lc_free(&one);
    return 0;
fail:
    lc_free(&one);
    lc_free(&o->movers);
    lc_free(&o->fills);
    lc_free(&o->fills_y);
    return -1;
}

/* Phase a, b or c (above) for layer l. */
static void transfer(struct ol_base *olb, struct Layer *l, int phase)
{
    struct OLayer *o = OL(l);
    struct OClip *n, *c;
    LONG ddx = l->bounds.MinX - o->oldb.MinX, ddy = l->bounds.MinY - o->oldb.MinY;
    struct lc_rect m;

    if (phase == 'b') {
        uint32_t i;
        struct place s;
        s.bm = l->rp->BitMap;
        s.dx = s.dy = 0;
        for (i = 0; i < o->movers.n; i++) {
            m = shift(o->movers.r[i], ddx, ddy);
            copy(olb, &s, &s, &m, ddx, ddy);
        }
        return;
    }
    for (n = o->fresh; n; n = (struct OClip *)n->cr.Next) {
        struct lc_rect nb = lr(&n->cr.bounds);
        struct place pd;
        if (phase == 'a' ? n->kind != K_BM && n->kind != K_SUPER : n->kind != K_SCREEN) continue;
        if (n->parent) continue;        /* took its old bitmap over */
        pd = place_of(l, n, l->bounds.MinX, l->bounds.MinY);
        for (c = o->full; c; c = (struct OClip *)c->cr.Next) {
            struct lc_rect ob = shift(lr(&c->cr.bounds), ddx, ddy);
            struct place ps;
            if (c->kind == K_NONE || !lc_meet(&nb, &ob, &m)) continue;
            if (phase == 'a' && c->kind == K_SUPER && n->kind == K_SUPER) continue;
            if (phase == 'c' && c->kind == K_SCREEN) continue;
            ps = place_of(l, c, o->oldb.MinX, o->oldb.MinY);
            copy(olb, &ps, &pd, &m, ddx, ddy);
        }
    }
}

/* Phase d for layer l: what none of its old pieces had (plan's fills). */

static void fill_fresh(struct ol_base *olb, struct Layer *l, int made)
{
    struct OLayer *o = OL(l);
    struct OClip *n, sup;
    struct place ps, pd;
    struct Layer tmp;
    struct RastPort trp;
    int simple = !(l->Flags & (LAYERSMART | LAYERSUPER));
    uint32_t i;

    sup.kind = K_SUPER;
    ps = place_of(l, &sup, l->bounds.MinX, l->bounds.MinY);
    for (i = 0; i < o->fills.n; i++) {
        struct lc_rect *r = &o->fills.r[i];
        for (n = o->fresh; n; n = (struct OClip *)n->cr.Next)
            if (r->x0 >= n->cr.bounds.MinX && r->x0 <= n->cr.bounds.MaxX && r->y0 >= n->cr.bounds.MinY &&
                r->y0 <= n->cr.bounds.MaxY)
                break;
        if (!n) continue;
        pd = place_of(l, n, l->bounds.MinX, l->bounds.MinY);
        if (!made) {
            struct Rectangle d;
            d.MinX = (WORD)(r->x0 - l->bounds.MinX); d.MinY = (WORD)(r->y0 - l->bounds.MinY);
            d.MaxX = (WORD)(r->x1 - l->bounds.MinX); d.MaxY = (WORD)(r->y1 - l->bounds.MinY);
            OrRectRegion(l->DamageList, &d);
            /* OS 3.2 sets Intuition's two bits with it; only they clear them */
            l->Flags |= LAYERREFRESH | LAYERIREFRESH | LAYERIREFRESH2;
        }
        if (l->Flags & LAYERSUPER) {
            /* a super bitmap layer's new part comes from its bitmap (and is
             * damage all the same); where the bitmap does not reach, the
             * backing bitmap is cleared */
            struct lc_rect e, m, out[4];
            uint32_t k, nk = 1;
            if (!l->SuperBitMap) continue;
            e = super_ext(olb, l);
            out[0] = *r;
            if (lc_meet(r, &e, &m)) {
                copy(olb, &ps, &pd, &m, 0, 0);
                nk = lc_cut(r, &m, out);
            }
            for (k = 0; k < nk; k++) backfill(olb, l, LAYERS_BACKFILL, &pd, &out[k]);
        } else if (made) {
            /* a layer just made gets its hook with its own RastPort; a
             * simple refresh one a second time with a bare one (so the
             * hook draws), its refresh bits up meanwhile, as on OS 3.2 */
            backfill(olb, l, l->BackFill, &pd, r);
            if (simple) {
                UWORD f = l->Flags;
                l->Flags |= LAYERREFRESH | LAYERIREFRESH | LAYERIREFRESH2;
                backfill_with(olb, l, 0, l->BackFill, &pd, r);
                l->Flags = f;
            }
        } else if (moved(l)) {
            /* moved or sized: OS 3.2 hands the hook its stand-in for the
             * layer (a copy with the new bounds, whose RastPort names the
             * copy); a simple refresh layer then gets it itself, with a
             * bare RastPort, cut rows first (below) */
            tmp = *l;
            trp = *l->rp;
            tmp.rp = &trp;
            trp.Layer = &tmp;
            backfill_with(olb, &tmp, &trp, l->BackFill, &pd, r);
        } else {
            backfill_with(olb, l, 0, l->BackFill, &pd, r);
        }
    }
    if (simple && o->hidden && !made && moved(l)) {
        /* a hidden simple refresh layer keeps nothing, but what it grew by
         * is damage, as on OS 3.2 */
        struct lc_rect nb = lr(&l->bounds), ob = shift(lr(&o->oldb), l->bounds.MinX - o->oldb.MinX,
                                                         l->bounds.MinY - o->oldb.MinY), m, out[4];
        uint32_t k, nk = 1;
        out[0] = nb;
        if (lc_meet(&nb, &ob, &m)) nk = lc_cut(&nb, &m, out);
        for (k = 0; k < nk; k++) {
            struct Rectangle d;
            d.MinX = (WORD)(out[k].x0 - l->bounds.MinX); d.MinY = (WORD)(out[k].y0 - l->bounds.MinY);
            d.MaxX = (WORD)(out[k].x1 - l->bounds.MinX); d.MaxY = (WORD)(out[k].y1 - l->bounds.MinY);
            OrRectRegion(l->DamageList, &d);
            l->Flags |= LAYERREFRESH | LAYERIREFRESH | LAYERIREFRESH2;
        }
    }
    for (i = 0; i < o->fills_y.n; i++) {
        struct lc_rect *r = &o->fills_y.r[i];
        for (n = o->fresh; n; n = (struct OClip *)n->cr.Next)
            if (r->x0 >= n->cr.bounds.MinX && r->x0 <= n->cr.bounds.MaxX && r->y0 >= n->cr.bounds.MinY &&
                r->y0 <= n->cr.bounds.MaxY)
                break;
        if (!n) continue;
        pd = place_of(l, n, l->bounds.MinX, l->bounds.MinY);
        backfill_with(olb, l, 0, l->BackFill, &pd, r);
    }
}


/* The screen newly uncovered, as OS 3.2 hands it to the Layer_Info's
 * hook: for each layer of the old stack (front to back), what it covered
 * that no layer in front of it did then and no layer covers now, with that
 * layer (index into the old stack) as owner. */
static int uncovered_by(const struct lc_layer *st, ULONG n, const struct lc_layer *ost, ULONG on,
                        const struct lc_rect *screen, struct lc_pieces *who)
{
    struct lc_rects one;
    ULONG k, j;
    uint32_t i;
    int e = 0;

    lc_init(&one, &mem);
    for (k = 0; k < on && !e; k++) {
        struct lc_rect b;
        if ((ost[k].flags & LC_HIDDEN) || !lc_meet(&ost[k].bounds, screen, &b)) continue;
        if ((e = lc_set(&one, &b))) break;
        for (j = 0; j < n && one.n && !e; j++)
            if (!(st[j].flags & LC_HIDDEN)) e = lc_sub_rect(&one, &st[j].bounds);
        for (j = 0; j < k && one.n && !e; j++)
            if (!(ost[j].flags & LC_HIDDEN)) e = lc_sub_rect(&one, &ost[j].bounds);
        for (i = 0; i < one.n && !e; i++) e = lc_pieces_add(who, &one.r[i], (int32_t)k);
    }
    lc_free(&one);
    return e ? -1 : 0;
}

/* Moves the pixels for the change since begin, puts in the new lists and
 * unlocks. made: a layer just made (it gets no damage); gone: a layer
 * just taken out of the stack, whose list is freed by the caller. Answers
 * 0, or -1 when memory ran out and the stack is back as it was. */
static int finish(struct change *ch, struct Layer *made, struct Layer *gone)
{
    struct ol_base *olb = ch->olb;
    struct lc_layer *st, *ost = 0;
    struct Layer **ord, **oord = 0, *l;
    ULONG n, on = 0, i;
    struct lc_pieces who;
    struct OInfo *inf;
    int phase;

#ifdef OL_COMPOSITE
    /* a composited Layer_Info's change is composed (layers_comp.c); when a
     * surface can't be had, its screen goes back to classic layers here */
    if (ch->comp && !oc_finish(ch, made, gone)) return 0;
#endif
    if (!ch->have_screen && made) {
        struct BitMap *bm = made->rp->BitMap;
        ch->screen.x0 = ch->screen.y0 = 0;
        ch->screen.x1 = (LONG)GetBitMapAttr(bm, BMA_WIDTH) - 1;
        ch->screen.y1 = (LONG)GetBitMapAttr(bm, BMA_HEIGHT) - 1;
        ch->have_screen = 1;
    }
    st = stack_of(ch, 0, &n, &ord);
    if (n && !st) goto undo_nothing;
    for (i = 0; i < n; i++)
        if (build(ch, st, ord, n, i)) {
            while (i--) {
                struct OClip *c;
                for (c = OL(ord[i])->fresh; c; c = (struct OClip *)c->cr.Next)
                    if (c->parent) { c->parent->stolen = 0; c->stolen = 1; }
                free_list(olb, OL(ord[i])->fresh);
                OL(ord[i])->fresh = 0;
            }
            free_stack(st, ord, n);
            goto undo_nothing;
        }

    /* what needs memory, before any pixel moves */
    lc_pieces_init(&who, &mem);
    for (i = 0; i < n; i++)
        if (plan(ord[i], ord[i] == made)) break;
    if (i == n && ch->have_screen) {
        /* the screen newly uncovered, each piece with the layer that left
         * it (in the stack as it was) */
        ost = stack_of(ch, 1, &on, &oord);
        if ((on && !ost) || uncovered_by(st, n, ost, on, &ch->screen, &who)) i = n + 1;
    }
    if (i != n) {
        ULONG k;
        for (k = 0; k < n && k < i; k++) {
            lc_free(&OL(ord[k])->movers);
            lc_free(&OL(ord[k])->fills);
            lc_free(&OL(ord[k])->fills_y);
        }
        lc_pieces_free(&who);
        free_stack(ost, oord, on);
        for (k = 0; k < n; k++) {
            struct OClip *c;
            for (c = OL(ord[k])->fresh; c; c = (struct OClip *)c->cr.Next)
                if (c->parent) { c->parent->stolen = 0; c->stolen = 1; }
            free_list(olb, OL(ord[k])->fresh);
            OL(ord[k])->fresh = 0;
        }
        free_stack(st, ord, n);
        goto undo_nothing;
    }

    /* the pixels */
    for (phase = 'a'; phase <= 'c'; phase++)
        for (i = 0; i < n; i++) transfer(olb, ord[i], phase);
    /* simple refresh layers' new parts, the screen uncovered, then the
     * others' new parts: the order OS 3.2 calls the hooks in */
    for (i = 0; i < n; i++)
        if (!(ord[i]->Flags & (LAYERSMART | LAYERSUPER))) fill_fresh(olb, ord[i], ord[i] == made);
    if (who.n) {
        /* OS 3.2 hands the Layer_Info's hook a stand-in for the layer that
         * left each piece: that layer as it was (its old bounds), simple
         * refresh, and offsets from its old top left */
        struct place p;
        struct Layer tmp;
        struct RastPort trp;
        inf = info_of(olb, ch->li, 0);
        p.bm = (n ? ord[0] : gone)->rp->BitMap;
        p.dx = p.dy = 0;
        for (i = 0; i < who.n; i++) {
            struct Layer *was = oord[who.p[i].obscurer];
            /* a layer gone or hidden where it was comes itself */
            if (moved(was)) {
                tmp = *was;
                trp = *was->rp;
                tmp.bounds = OL(was)->oldb;
                /* (a smart or super bitmap layer's without its refresh bits) */
                if (was->Flags & (LAYERSMART | LAYERSUPER))
                    tmp.Flags = (UWORD)((was->Flags & ~(LAYERSMART | LAYERSUPER | LAYERREFRESH | LAYERIREFRESH |
                                                        LAYERIREFRESH2)) | LAYERSIMPLE);
                tmp.Scroll_X = tmp.Scroll_Y = 0;
                tmp.rp = &trp;
                trp.Layer = &tmp;
            }
            /* the stand-in's RastPort names it; a layer itself comes with
             * a bare one */
            if (moved(was)) backfill_with(olb, &tmp, &trp, inf ? inf->hook : LAYERS_BACKFILL, &p, &who.p[i].r);
            else backfill_with(olb, was, was == gone ? was->rp : 0, inf ? inf->hook : LAYERS_BACKFILL, &p, &who.p[i].r);
        }
    }
    for (i = 0; i < n; i++) {
        if (ord[i]->Flags & (LAYERSMART | LAYERSUPER)) fill_fresh(olb, ord[i], ord[i] == made);
        lc_free(&OL(ord[i])->movers);
        lc_free(&OL(ord[i])->fills);
        lc_free(&OL(ord[i])->fills_y);
    }
    lc_pieces_free(&who);
    free_stack(ost, oord, on);

    /* the lists */
    for (i = 0; i < n; i++) {
        struct OLayer *o = OL(ord[i]);
        struct OClip *c;
        for (c = o->fresh; c; c = (struct OClip *)c->cr.Next) c->parent = 0;
        free_list(olb, o->full);
        o->full = o->fresh;
        o->fresh = 0;
        attach(olb, ord[i]);
    }
    free_stack(st, ord, n);
#ifdef OL_COMPOSITE
    if (ch->demoted) oc_demoted(ch, 1);
#endif
    end(ch);
    return 0;

undo_nothing:
    /* the stack as it was: order, bounds and hidden */
    for (l = ch->li->top_layer; l; ) {
        struct Layer *b = l->back;
        l->front = l->back = 0;
        l = b;
    }
    ch->li->top_layer = 0;
    for (i = 0; i < ch->nold; i++) {
        struct Layer *x = ch->old[i];
        x->front = i ? ch->old[i - 1] : 0;
        x->back = i + 1 < ch->nold ? ch->old[i + 1] : 0;
        x->bounds = OL(x)->oldb;
        x->Width = (WORD)(x->bounds.MaxX - x->bounds.MinX + 1);
        x->Height = (WORD)(x->bounds.MaxY - x->bounds.MinY + 1);
        set_hidden(x, OL(x)->oldhidden);
        attach(olb, x);
    }
    ch->li->top_layer = ch->nold ? ch->old[0] : 0;
#ifdef OL_COMPOSITE
    if (ch->demoted) oc_demoted(ch, 0);
#endif
    end(ch);
    return -1;
}

/* ---- Layer_Info -------------------------------------------------------- */

void ol_InitLayers(struct ol_base *olb, struct Layer_Info *li)
{
    UBYTE *p = (UBYTE *)li;
    ULONG i;
    for (i = 0; i < sizeof *li; i++) p[i] = 0;
    InitSemaphore(&li->Lock);
}

LONG ol_FattenLayerInfo(struct ol_base *olb, struct Layer_Info *li)
{
    struct OInfo *i = info_of(olb, li, 1);
    if (!i) return FALSE;
    i->fattened++;
    return TRUE;
}

void ol_ThinLayerInfo(struct ol_base *olb, struct Layer_Info *li)
{
    struct OInfo *i = info_of(olb, li, 0);
    if (i && i->fattened > 0 && --i->fattened == 0) info_drop(olb, li);
}

struct Layer_Info *ol_NewLayerInfo(struct ol_base *olb)
{
    struct Layer_Info *li = zalloc(sizeof *li);
    if (!li) return 0;
    ol_InitLayers(olb, li);
    if (!ol_FattenLayerInfo(olb, li)) {
        FreeMem(li, sizeof *li);
        return 0;
    }
    li->Flags |= NEWLAYERINFO_CALLED;
    return li;
}

void ol_DisposeLayerInfo(struct ol_base *olb, struct Layer_Info *li)
{
    info_drop(olb, li);
    if (li->Flags & NEWLAYERINFO_CALLED) FreeMem(li, sizeof *li);
}

void ol_LockLayerInfo(struct ol_base *olb, struct Layer_Info *li) { ObtainSemaphore(&li->Lock); }
void ol_UnlockLayerInfo(struct ol_base *olb, struct Layer_Info *li) { ReleaseSemaphore(&li->Lock); }
void ol_LockLayer(struct ol_base *olb, struct Layer *l) { ObtainSemaphore(&l->Lock); }
void ol_UnlockLayer(struct ol_base *olb, struct Layer *l) { ReleaseSemaphore(&l->Lock); }

void ol_LockLayers(struct ol_base *olb, struct Layer_Info *li)
{
    lock_info_all(li);
}

void ol_UnlockLayers(struct ol_base *olb, struct Layer_Info *li)
{
    struct Layer *l;
    for (l = li->top_layer; l; l = l->back) ReleaseSemaphore(&l->Lock);
    ReleaseSemaphore(&li->Lock);
}

struct Hook *ol_InstallLayerInfoHook(struct ol_base *olb, struct Layer_Info *li, struct Hook *hook)
{
    struct OInfo *i = info_of(olb, li, 1);
    struct Hook *old;
    if (!i) return LAYERS_BACKFILL;
    ObtainSemaphore(&li->Lock);
    old = i->hook;
    i->hook = hook;
    ReleaseSemaphore(&li->Lock);
    return old;
}

LONG ol_SetLayerInfoBounds(struct ol_base *olb, struct Layer_Info *li, const struct Rectangle *bounds)
{
    struct change ch;
    struct OInfo *i = info_of(olb, li, 1);
    struct Rectangle ob;
    UBYTE had;
    if (!i || begin(olb, li, &ch)) return FALSE;
    ob = i->bounds;
    had = i->have_bounds;
#ifdef OL_COMPOSITE
    ch.full = 1;
#endif
    if (bounds) { i->bounds = *bounds; i->have_bounds = 1; ch.screen = lr(bounds); ch.have_screen = 1; }
    else i->have_bounds = 0;
    if (finish(&ch, 0, 0)) {
        i->bounds = ob;
        i->have_bounds = had;
        return FALSE;
    }
    return TRUE;
}

/* ---- making and deleting layers ---------------------------------------- */

static struct Layer *create(struct ol_base *olb, struct Layer_Info *li, struct BitMap *bm, LONG x0, LONG y0, LONG x1,
                            LONG y1, LONG flags, struct Hook *hook, struct BitMap *sbm, int upfront)
{
    struct OLayer *o;
    struct Layer *l;
    struct change ch;

    if (x1 < x0 || y1 < y0 || !bm) return 0;
    if ((flags & LAYERSUPER) && !sbm) return 0;
    if (!(o = zalloc(sizeof *o))) return 0;
    l = &o->l;
    if (!(l->DamageList = NewRegion())) { FreeMem(o, sizeof *o); return 0; }
    InitRastPort(&o->rp);
    o->rp.Layer = l;
    o->rp.BitMap = bm;
    l->rp = &o->rp;
    l->bounds.MinX = (WORD)x0; l->bounds.MinY = (WORD)y0;
    l->bounds.MaxX = (WORD)x1; l->bounds.MaxY = (WORD)y1;
    l->Width = (WORD)(x1 - x0 + 1);
    l->Height = (WORD)(y1 - y0 + 1);
    l->Flags = (UWORD)flags;
    l->SuperBitMap = (flags & LAYERSUPER) ? sbm : 0;
    l->LayerInfo = li;
    l->BackFill = hook;
    InitSemaphore(&l->Lock);
    o->oldb = l->bounds;
#ifdef OL_COMPOSITE
    o->magic = OL_MAGIC;
    o->opacity = 255;
    o->dirty.x0 = 1;            /* nothing drawn */
    o->dirty.x1 = 0;
#endif

    if (begin(olb, li, &ch)) goto fail;
    link_before(li, l, upfront ? front_spot(li, l) : back_spot(li, l));
    if (finish(&ch, l, 0)) goto fail;   /* finish took l out again */
    return l;
fail:
    DisposeRegion(l->DamageList);
    FreeMem(o, sizeof *o);
    return 0;
}

struct Layer *ol_CreateUpfrontHookLayer(struct ol_base *olb, struct Layer_Info *li, struct BitMap *bm, LONG x0,
                                        LONG y0, LONG x1, LONG y1, LONG flags, struct Hook *hook, struct BitMap *sbm)
{
    return create(olb, li, bm, x0, y0, x1, y1, flags, hook, sbm, 1);
}

struct Layer *ol_CreateBehindHookLayer(struct ol_base *olb, struct Layer_Info *li, struct BitMap *bm, LONG x0, LONG y0,
                                       LONG x1, LONG y1, LONG flags, struct Hook *hook, struct BitMap *sbm)
{
    return create(olb, li, bm, x0, y0, x1, y1, flags, hook, sbm, 0);
}

struct Layer *ol_CreateUpfrontLayer(struct ol_base *olb, struct Layer_Info *li, struct BitMap *bm, LONG x0, LONG y0,
                                    LONG x1, LONG y1, LONG flags, struct BitMap *sbm)
{
    return create(olb, li, bm, x0, y0, x1, y1, flags, LAYERS_BACKFILL, sbm, 1);
}

struct Layer *ol_CreateBehindLayer(struct ol_base *olb, struct Layer_Info *li, struct BitMap *bm, LONG x0, LONG y0,
                                   LONG x1, LONG y1, LONG flags, struct BitMap *sbm)
{
    return create(olb, li, bm, x0, y0, x1, y1, flags, LAYERS_BACKFILL, sbm, 0);
}

LONG ol_DeleteLayer(struct ol_base *olb, struct Layer *l)
{
    struct Layer_Info *li = l->LayerInfo;
    struct OLayer *o = OL(l);
    struct change ch;

    if (begin(olb, li, &ch)) return FALSE;
    /* a smart layer's pixels are lost with it: nothing to copy back, so
     * its list goes before the others are cut */
    unlink_layer(li, l);
    o->hidden = 1;
    if (finish(&ch, 0, l)) return FALSE;
    free_list(olb, o->full);
#ifdef OL_COMPOSITE
    oc_gone(olb, l);
#endif
    if (l->DamageList) DisposeRegion(l->DamageList);
    FreeMem(o, sizeof *o);
    return TRUE;
}

/* ---- depth ------------------------------------------------------------- */

static LONG restack(struct ol_base *olb, struct Layer *l, struct Layer *at, int use_at, int front)
{
    struct Layer_Info *li = l->LayerInfo;
    struct change ch;
    if (OL(l)->hidden) return TRUE;     /* OS 3.2 leaves a hidden layer where it is */
    if (begin(olb, li, &ch)) return FALSE;
#ifdef OL_COMPOSITE
    ch.subject = l;
#endif
    unlink_layer(li, l);
    if (!use_at) at = front ? front_spot(li, l) : back_spot(li, l);
    link_before(li, l, at);
    return finish(&ch, 0, 0) ? FALSE : TRUE;
}

LONG ol_UpfrontLayer(struct ol_base *olb, struct Layer *l) { return restack(olb, l, 0, 0, 1); }
LONG ol_BehindLayer(struct ol_base *olb, struct Layer *l) { return restack(olb, l, 0, 0, 0); }

LONG ol_MoveLayerInFrontOf(struct ol_base *olb, struct Layer *l, struct Layer *other)
{
    struct Layer *at = other, *x;
    if (other == l) return TRUE;
    /* a backdrop layer stays behind every other kind */
    if (!backdrop(l)) {
        for (x = l->LayerInfo->top_layer; x && x != other; x = x->back)
            if (x != l && backdrop(x)) { at = x; break; }
    } else if (other && !backdrop(other)) {
        at = first_backdrop(l->LayerInfo, l);
    }
    return restack(olb, l, at, 1, 0);
}

LONG ol_HideLayer(struct ol_base *olb, struct Layer *l)
{
    struct change ch;
    if (OL(l)->hidden) return TRUE;
    if (begin(olb, l->LayerInfo, &ch)) return FALSE;
#ifdef OL_COMPOSITE
    ch.subject = l;
#endif
    set_hidden(l, 1);
    /* OS 3.2 moves a hidden layer to the very back */
    unlink_layer(l->LayerInfo, l);
    link_before(l->LayerInfo, l, 0);
    return finish(&ch, 0, 0) ? FALSE : TRUE;
}

LONG ol_ShowLayer(struct ol_base *olb, struct Layer *l, struct Layer *in_front_of)
{
    struct Layer_Info *li = l->LayerInfo;
    struct change ch;
    if (begin(olb, li, &ch)) return FALSE;
#ifdef OL_COMPOSITE
    ch.subject = l;
#endif
    set_hidden(l, 0);
    if (in_front_of != l) {
        unlink_layer(li, l);
        if (in_front_of == (struct Layer *)1) link_before(li, l, back_spot(li, l));
        else if (!in_front_of) link_before(li, l, front_spot(li, l));
        else link_before(li, l, in_front_of);
    }
    return finish(&ch, 0, 0) ? FALSE : TRUE;
}

LONG ol_LayerOccluded(struct ol_base *olb, struct Layer *l)
{
    struct Layer *f;
    struct lc_rect b = lr(&l->bounds);
    LONG r = OL(l)->hidden ? TRUE : FALSE;     /* hidden counts as occluded on OS 3.2 */
    ObtainSemaphore(&l->LayerInfo->Lock);
    for (f = l->front; f && !r; f = f->front) {
        struct lc_rect fb = lr(&f->bounds);
        if (!OL(f)->hidden && lc_meet(&b, &fb, 0)) { r = TRUE; break; }
    }
    ReleaseSemaphore(&l->LayerInfo->Lock);
    return r;
}

struct Layer *ol_WhichLayer(struct ol_base *olb, struct Layer_Info *li, LONG x, LONG y)
{
    struct Layer *l;
    ObtainSemaphore(&li->Lock);
    for (l = li->top_layer; l; l = l->back)
        if (!OL(l)->hidden && x >= l->bounds.MinX && x <= l->bounds.MaxX && y >= l->bounds.MinY &&
            y <= l->bounds.MaxY)
            break;
    ReleaseSemaphore(&li->Lock);
    return l;
}

/* ---- moving and sizing ------------------------------------------------- */

LONG ol_MoveSizeLayer(struct ol_base *olb, struct Layer *l, LONG dx, LONG dy, LONG dw, LONG dh)
{
    struct change ch;
    LONG w = l->bounds.MaxX - l->bounds.MinX + 1 + dw, h = l->bounds.MaxY - l->bounds.MinY + 1 + dh;
    if (w < 1 || h < 1) return FALSE;
    if ((l->Flags & LAYERSUPER) && l->SuperBitMap &&
        (w + l->Scroll_X > (LONG)GetBitMapAttr(l->SuperBitMap, BMA_WIDTH) ||
         h + l->Scroll_Y > (LONG)GetBitMapAttr(l->SuperBitMap, BMA_HEIGHT)))
        return FALSE;
    if (begin(olb, l->LayerInfo, &ch)) return FALSE;
    l->bounds.MinX += (WORD)dx;
    l->bounds.MinY += (WORD)dy;
    l->bounds.MaxX = (WORD)(l->bounds.MinX + w - 1);
    l->bounds.MaxY = (WORD)(l->bounds.MinY + h - 1);
    l->Width = (WORD)w;
    l->Height = (WORD)h;
    if (finish(&ch, 0, 0)) return FALSE;
    if (dw < 0 || dh < 0) {
        struct Rectangle keep;
        keep.MinX = 0; keep.MinY = 0; keep.MaxX = (WORD)(w - 1); keep.MaxY = (WORD)(h - 1);
        AndRectRegion(l->DamageList, &keep);
    }
    return TRUE;
}

LONG ol_MoveLayer(struct ol_base *olb, struct Layer *l, LONG dx, LONG dy)
{
    return ol_MoveSizeLayer(olb, l, dx, dy, 0, 0);
}
LONG ol_SizeLayer(struct ol_base *olb, struct Layer *l, LONG dx, LONG dy)
{
    return ol_MoveSizeLayer(olb, l, 0, 0, dx, dy);
}

void ol_ScrollLayer(struct ol_base *olb, struct Layer *l, LONG dx, LONG dy)
{
    struct OLayer *o = OL(l);
    struct OClip *c;

    ObtainSemaphore(&l->Lock);
    detach(olb, l);
    if ((l->Flags & LAYERSUPER) && l->SuperBitMap) {
        /* every piece into the super bitmap, then back from the new place
         * (where the super bitmap reaches): OS 3.2 does not clamp */
        struct OClip sup;
        struct place ps, pd;
        int way;
        sup.kind = K_SUPER;
        for (way = 0; way < 2; way++) {
            struct lc_rect ext;
            if (way) {
                l->Scroll_X += (WORD)dx;
                l->Scroll_Y += (WORD)dy;
            }
            ext = super_ext(olb, l);
            for (c = o->full; c; c = (struct OClip *)c->cr.Next) {
                struct lc_rect b = lr(&c->cr.bounds);
                if (c->kind == K_NONE || !lc_meet(&b, &ext, &b)) continue;
                ps = place_of(l, c, 0, 0);
                pd = place_of(l, &sup, l->bounds.MinX, l->bounds.MinY);
                if (way) copy(olb, &pd, &ps, &b, 0, 0);
                else copy(olb, &ps, &pd, &b, 0, 0);
            }
        }
    } else {
        l->Scroll_X += (WORD)dx;
        l->Scroll_Y += (WORD)dy;
    }
    attach(olb, l);
#ifdef OL_COMPOSITE
    if (OL(l)->surf && (l->Flags & LAYERSUPER)) oc_touch(olb, l, 0);
#endif
    ReleaseSemaphore(&l->Lock);
}

/* ---- clip regions, updates and hooks ----------------------------------- */

struct Region *ol_InstallClipRegion(struct ol_base *olb, struct Layer *l, struct Region *region)
{
    struct Region *old;
    ObtainSemaphore(&l->Lock);
    detach(olb, l);
    old = l->ClipRegion;
    l->ClipRegion = region;
    attach(olb, l);
    ReleaseSemaphore(&l->Lock);
    return old;
}

LONG ol_BeginUpdate(struct ol_base *olb, struct Layer *l)
{
    LONG ok;
    ObtainSemaphore(&l->Lock);
    detach(olb, l);
    l->Flags |= LAYERUPDATING;
    attach(olb, l);
    ok = !(l->Flags & LAYER_CLIPRECTS_LOST);
    ReleaseSemaphore(&l->Lock);
    return ok;
}

void ol_EndUpdate(struct ol_base *olb, struct Layer *l, UWORD flag)
{
    ObtainSemaphore(&l->Lock);
    detach(olb, l);
    l->Flags &= ~LAYERUPDATING;
    /* LAYERREFRESH stays: Intuition clears it, as with OS 3.2 */
    if (flag) ClearRegion(l->DamageList);
    attach(olb, l);
    ReleaseSemaphore(&l->Lock);
}

struct Hook *ol_InstallLayerHook(struct ol_base *olb, struct Layer *l, struct Hook *hook)
{
    struct Hook *old;
    ObtainSemaphore(&l->Lock);
    old = l->BackFill;
    l->BackFill = hook;
    ReleaseSemaphore(&l->Lock);
    return old;
}

void ol_DoHookClipRects(struct ol_base *olb, struct Hook *hook, struct RastPort *rp, const struct Rectangle *rect)
{
    struct Layer *l = rp->Layer;
    struct lc_rect want = lr(rect), m;
    struct ClipRect *cr;

    if (!l) {
        struct place p;
        struct lc_rect all;
        p.bm = rp->BitMap;
        p.dx = p.dy = 0;
        all.x0 = all.y0 = 0;
        all.x1 = (LONG)GetBitMapAttr(rp->BitMap, BMA_WIDTH) - 1;
        all.y1 = (LONG)GetBitMapAttr(rp->BitMap, BMA_HEIGHT) - 1;
        if (lc_meet(&want, &all, &m)) backfill(olb, 0, hook, &p, &m);
        return;
    }
    ObtainSemaphore(&l->Lock);
    want = shift(want, l->bounds.MinX - l->Scroll_X, l->bounds.MinY - l->Scroll_Y);
    for (cr = l->ClipRect; cr; cr = cr->Next) {
        struct lc_rect b = lr(&cr->bounds);
        struct place p;
        if (!lc_meet(&b, &want, &m)) continue;
        if (!cr->obscured) {
            p.bm = rp->BitMap;
            p.dx = p.dy = 0;
        } else if (cr->BitMap) {
            p.bm = cr->BitMap;
            p.dx = -(LONG)(cr->bounds.MinX & ~15);
            p.dy = -(LONG)cr->bounds.MinY;
        } else {
            continue;
        }
        backfill(olb, l, hook, &p, &m);
#ifdef OL_COMPOSITE
        if (OL(l)->surf) oc_touch(olb, l, &m);
#endif
    }
    /* OS 3.2 also hands a super bitmap layer's hook the rectangle in the
     * super bitmap: the part in the layer's window, then the rest */
    if ((l->Flags & LAYERSUPER) && l->SuperBitMap) {
        struct place p;
        struct lc_rect all, win, out[4];
        uint32_t k, n;
        p.bm = l->SuperBitMap;
        p.dx = l->Scroll_X - l->bounds.MinX;
        p.dy = l->Scroll_Y - l->bounds.MinY;
        all = super_ext(olb, l);
        if (lc_meet(&want, &all, &want)) {
            win = lr(&l->bounds);
            if (lc_meet(&want, &win, &m)) {
                backfill(olb, l, hook, &p, &m);
                n = lc_cut(&want, &m, out);
            } else {
                out[0] = want;
                n = 1;
            }
            for (k = 0; k < n; k++) backfill(olb, l, hook, &p, &out[k]);
        }
    }
    ReleaseSemaphore(&l->Lock);
}

void ol_SortLayerCR(struct ol_base *olb, struct Layer *l, LONG dx, LONG dy)
{
    struct ClipRect *cr, **nodes, *prev;
    struct lc_rect *r;
    ULONG n = 0, i, j;

    for (cr = l->ClipRect; cr; cr = cr->Next) n++;
    if (n < 2) return;
    nodes = AllocMem(n * sizeof *nodes, MEMF_PUBLIC);
    r = AllocMem(n * sizeof *r, MEMF_PUBLIC);
    if (nodes && r) {
        for (cr = l->ClipRect, i = 0; cr; cr = cr->Next, i++) { nodes[i] = cr; r[i] = lr(&cr->bounds); }
        /* graphics.library's ScrollRaster moves the pixels by (-dx, -dy) */
        lc_sort_for_move(r, n, -dx, -dy);
        prev = 0;
        for (i = 0; i < n; i++) {
            for (j = i; j < n; j++)
                if (nodes[j]->bounds.MinX == r[i].x0 && nodes[j]->bounds.MinY == r[i].y0 &&
                    nodes[j]->bounds.MaxX == r[i].x1 && nodes[j]->bounds.MaxY == r[i].y1)
                    break;
            if (j < n) { cr = nodes[j]; nodes[j] = nodes[i]; nodes[i] = cr; }
            if (prev) prev->Next = nodes[i];
            else l->ClipRect = nodes[i];
            prev = nodes[i];
        }
        prev->Next = 0;
    }
    if (nodes) FreeMem(nodes, n * sizeof *nodes);
    if (r) FreeMem(r, n * sizeof *r);
}

void ol_SwapBitsRastPortClipRect(struct ol_base *olb, struct RastPort *rp, struct ClipRect *cr)
{
    LONG x = cr->bounds.MinX, y = cr->bounds.MinY;
    LONG w = cr->bounds.MaxX - x + 1, h = cr->bounds.MaxY - y + 1, bx = x - (x & ~15);
    /* three exclusive ors: a ^= b, b ^= a, a ^= b */
    BltBitMap(cr->BitMap, bx, 0, rp->BitMap, x, y, w, h, 0x60, 0xFF, 0);
    BltBitMap(rp->BitMap, x, y, cr->BitMap, bx, 0, w, h, 0x60, 0xFF, 0);
    BltBitMap(cr->BitMap, bx, 0, rp->BitMap, x, y, w, h, 0x60, 0xFF, 0);
}

#ifdef OL_COMPOSITE
/* The composited mode: in the same unit, so it reaches the statics above */
#include "layers_comp.c"
#endif
