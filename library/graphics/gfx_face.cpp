// SPDX-License-Identifier: MIT
// OpenGfx graphics.library, the face: bitmaps, RastPort state, colour maps
// and pens, regions, layer locks, super bitmaps, scaling, Flood and the
// drawing calls around the core. Copyright (c) 2026 Dalsin Limited. MIT.
//
// Clean-room: from graphics.library's autodocs, the NDK 3.2 layouts
// (gfx_types.h) and OS 3.2's answers where tests/GfxFaceABITest.c measures
// them; where neither settles a corner the comment says "ABI test" and the
// ABI test has a PROBE line for it. Lean C++ (no exceptions, RTTI, static
// constructors or libstdc++), no Amiga headers, no globals: memory, locks
// and the machine come through the env (gfx_env.h).
#include "gfx_face.h"

extern "C" {
#include "gfx_core.h"
#include "gfx_region.h"

// The core's RastPort calls (library/core/gfx_rp.h), declared here over
// gfx_types.h's layouts, which are the NDK's.
int ogfx_rp_takes(gfx_rastport *rp, int masked);
int32_t ogfx_rp_WritePixel(gfx_rastport *rp, int32_t x, int32_t y);
int32_t ogfx_rp_ReadPixel(gfx_rastport *rp, int32_t x, int32_t y);
void ogfx_rp_SetRast(gfx_rastport *rp, uint32_t pen);
int32_t ogfx_rp_WritePixelArray8(gfx_rastport *rp, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                                 uint8_t *array, gfx_rastport *temprp);
int32_t ogfx_rp_ReadPixelArray8(gfx_rastport *rp, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                                uint8_t *array, gfx_rastport *temprp);
void ogfx_rp_WriteChunkyPixels(gfx_rastport *rp, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                               uint8_t *array, int32_t bpr);
void ogfx_rp_Move(gfx_rastport *rp, int32_t x, int32_t y);
void ogfx_rp_Draw(gfx_rastport *rp, int32_t x, int32_t y);
void ogfx_rp_PolyDraw(gfx_rastport *rp, int32_t count, const int16_t *xy);
void ogfx_rp_RectFill(gfx_rastport *rp, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
void ogfx_rp_BltPattern(gfx_rastport *rp, const uint8_t *mask, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                        uint32_t bytecnt);
int32_t ogfx_rp_AreaMove(gfx_rastport *rp, int32_t x, int32_t y);
int32_t ogfx_rp_AreaDraw(gfx_rastport *rp, int32_t x, int32_t y);
int32_t ogfx_rp_AreaEllipse(gfx_rastport *rp, int32_t xc, int32_t yc, int32_t a, int32_t b);
int32_t ogfx_rp_AreaEnd(gfx_rastport *rp);
int ogfx_rp_DrawEllipse(gfx_rastport *rp, int32_t xc, int32_t yc, int32_t a, int32_t b);
int ogfx_rp_EraseRect(gfx_rastport *rp, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
int32_t ogfx_rp_BltBitMap(gfx_bitmap *src, int32_t sx, int32_t sy, gfx_bitmap *dst, int32_t dx, int32_t dy,
                          int32_t w, int32_t h, uint32_t minterm, uint32_t mask, uint8_t *temp);
void ogfx_rp_BltBitMapRastPort(gfx_bitmap *src, int32_t sx, int32_t sy, gfx_rastport *rp, int32_t dx, int32_t dy,
                               int32_t w, int32_t h, uint32_t minterm);
int ogfx_rp_ClipBlit(gfx_rastport *srp, int32_t sx, int32_t sy, gfx_rastport *drp, int32_t dx, int32_t dy,
                     int32_t w, int32_t h, uint32_t minterm);
int ogfx_rp_takes_copy(gfx_rastport *rp);
int ogfx_rp_ScrollRaster(gfx_rastport *rp, int32_t dx, int32_t dy, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
}

namespace {

// ---- small things ---------------------------------------------------------

inline void *mem_alloc(gfx_env *e, uint32_t n, uint32_t flags) { return n ? e->alloc(e->ctx, n, flags) : nullptr; }
inline void mem_free(gfx_env *e, void *p, uint32_t n)
{
    if (p) e->free(e->ctx, p, n);
}
inline void zero(void *p, uint32_t n)
{
    auto *b = static_cast<volatile uint8_t *>(p);   // volatile: no memset call from the optimiser
    while (n--) *b++ = 0;
}
// a struct copy with no memcpy call (the library links nothing but libgcc)
template <class T> void assign(T &d, const T &s)
{
    auto *o = reinterpret_cast<volatile uint8_t *>(&d);
    auto *i = reinterpret_cast<const uint8_t *>(&s);
    for (uint32_t n = 0; n < sizeof(T); n++) o[n] = i[n];
}
inline int32_t min32(int32_t a, int32_t b) { return a < b ? a : b; }
inline int32_t max32(int32_t a, int32_t b) { return a > b ? a : b; }
inline uint32_t row_bytes(uint32_t width) { return ((width + 15) >> 4) * 2; }

// utility.library's NextTagItem, here so the face needs nothing from it
const gfx_tagitem *next_tag(const gfx_tagitem **state)
{
    for (;;) {
        const gfx_tagitem *t = *state;
        if (!t) return nullptr;
        switch (t->tag) {
        case GFX_TAG_DONE: *state = nullptr; return nullptr;
        case GFX_TAG_IGNORE: *state = t + 1; break;
        case GFX_TAG_MORE: *state = reinterpret_cast<const gfx_tagitem *>(static_cast<uintptr_t>(t->data)); break;
        case GFX_TAG_SKIP: *state = t + 1 + t->data; break;
        default: *state = t + 1; return t;
        }
    }
}

const gfx_tagitem *find_tag(uint32_t tag, const gfx_tagitem *tags)
{
    const gfx_tagitem *t;
    while ((t = next_tag(&tags)))
        if (t->tag == tag) return t;
    return nullptr;
}

void planar_of(const gfx_bitmap *b, gfx_planar *p)
{
    for (int i = 0; i < 8; i++) p->planes[i] = i < b->depth ? b->planes[i] : nullptr;
    p->bytes_per_row = b->bytesperrow;
    p->rows = b->rows;
    p->depth = b->depth > 8 ? 8 : b->depth;
    p->interleaved = (b->flags & GFX_BMF_INTERLEAVED) != 0;
}

// A copy of w x h from (sx, sy) of s to (dx, dy) of d, cut to both.
void copy_cut(const gfx_bitmap *s, int32_t sx, int32_t sy, const gfx_bitmap *d, int32_t dx, int32_t dy, int32_t w,
              int32_t h)
{
    if (sx < 0) { dx -= sx; w += sx; sx = 0; }
    if (sy < 0) { dy -= sy; h += sy; sy = 0; }
    if (dx < 0) { sx -= dx; w += dx; dx = 0; }
    if (dy < 0) { sy -= dy; h += dy; dy = 0; }
    w = min32(w, min32(int32_t(s->bytesperrow) * 8 - sx, int32_t(d->bytesperrow) * 8 - dx));
    h = min32(h, min32(int32_t(s->rows) - sy, int32_t(d->rows) - dy));
    if (w <= 0 || h <= 0) return;
    gfx_planar ps, pd;
    planar_of(s, &ps);
    planar_of(d, &pd);
    gfx_planar_blit(&ps, sx, sy, &pd, dx, dy, w, h, 0xC0, 0xFF);
}

// ---- bitmaps ---------------------------------------------------------------

// What AllocBitMap keeps after the BitMap it hands out, for FreeBitMap.
struct bm_alloc {
    gfx_bitmap bm;
    uint32_t magic;
    uint32_t plane_size;    // each plane's bytes, or the whole block's when interleaved
};
constexpr uint32_t BM_MAGIC = 0x4F47424Du; // "OGBM"

// ---- colour maps -------------------------------------------------------------

gfx_colormap *vp_colormap(void *vp)
{
    return vp ? *reinterpret_cast<gfx_colormap **>(static_cast<char *>(vp) + 4) : nullptr;
}

uint32_t cm_rgb8(const gfx_colormap *cm, uint32_t i, int shift)
{
    uint32_t hi = cm->colortable[i] >> shift & 15, lo = cm->lowcolorbits ? cm->lowcolorbits[i] >> shift & 15 : hi;
    return hi << 4 | lo;
}

// pen states in PaletteExtra's AllocList
enum : uint8_t { PEN_FREE = 0, PEN_SHARED = 1, PEN_EXCL = 2 };

uint32_t color_dist(const gfx_colormap *cm, uint32_t i, uint32_t r, uint32_t g, uint32_t b)
{
    int32_t dr = int32_t(cm_rgb8(cm, i, 8)) - int32_t(r >> 24), dg = int32_t(cm_rgb8(cm, i, 4)) - int32_t(g >> 24),
            db = int32_t(cm_rgb8(cm, i, 0)) - int32_t(b >> 24);
    return uint32_t(dr * dr + dg * dg + db * db);
}

void pen_set_color(gfx_env *e, gfx_colormap *cm, uint32_t n, uint32_t r, uint32_t g, uint32_t b)
{
    gfx_SetRGB32CM(e, cm, n, r, g, b);
    if (cm->palextra && cm->palextra->viewport) gfx_hook_vp_colors(e, cm->palextra->viewport, n, 1);
}

// The next free pen, highest first (ABI test: which one OS 3.2 hands out
// first), or -1.
int32_t pen_take_free(gfx_colormap *cm)
{
    gfx_paletteextra *pe = cm->palextra;
    for (int32_t i = int32_t(pe->sharablecolors) - 1; i >= 0; i--)
        if (pe->alloclist[i] == PEN_FREE) return i;
    return -1;
}

void pen_mark(gfx_colormap *cm, uint32_t n, uint8_t state)
{
    gfx_paletteextra *pe = cm->palextra;
    if (pe->alloclist[n] == PEN_FREE) pe->nfree--;
    if (pe->alloclist[n] == PEN_SHARED) pe->nshared--;
    pe->alloclist[n] = state;
    pe->refcnt[n] = state == PEN_SHARED ? 1 : 0;
    if (state == PEN_FREE) pe->nfree++;
    if (state == PEN_SHARED) pe->nshared++;
}

void pen_share(gfx_paletteextra *pe, uint32_t n)
{
    if (pe->refcnt[n] < 255) pe->refcnt[n]++;
}

// ---- regions ---------------------------------------------------------------

void *rgn_alloc(void *ctx, uint32_t n)
{
    auto *e = static_cast<gfx_env *>(ctx);
    return mem_alloc(e, n, GFX_MEMF_PUBLIC);
}
void rgn_free(void *ctx, void *p, uint32_t n) { mem_free(static_cast<gfx_env *>(ctx), p, n); }

inline gfx_region *core_rgn(gfx_rgn *r) { return reinterpret_cast<gfx_region *>(r); }
inline const gfx_region *core_rgn(const gfx_rgn *r) { return reinterpret_cast<const gfx_region *>(r); }

// ---- layers and where drawing goes -----------------------------------------------

// Super bitmap pixels to and from the layer's pieces: every ClipRect of l,
// the screen where it is on top and its own bitmap where a smart piece is
// covered, cut to clip (super bitmap coordinates, which are the RastPort's)
// when there is one.
void super_xfer(gfx_layer *l, bool to_super, const gfx_rectangle *clip)
{
    if (!(l->flags & GFX_LAYERSUPER) || !l->superbitmap || !l->rp) return;
    int32_t ox = l->bounds.minx - l->scroll_x, oy = l->bounds.miny - l->scroll_y;
    for (gfx_cliprect *cr = l->cliprect; cr; cr = cr->next) {
        int32_t x0 = cr->bounds.minx, y0 = cr->bounds.miny, x1 = cr->bounds.maxx, y1 = cr->bounds.maxy;
        if (clip) {
            x0 = max32(x0, clip->minx + ox); y0 = max32(y0, clip->miny + oy);
            x1 = min32(x1, clip->maxx + ox); y1 = min32(y1, clip->maxy + oy);
        }
        if (x0 > x1 || y0 > y1) continue;
        const gfx_bitmap *b;
        int32_t bx = 0, by = 0;
        if (!cr->lobs) b = l->rp->bitmap;
        else if (cr->bitmap) { b = cr->bitmap; bx = cr->bounds.minx & ~15; by = cr->bounds.miny; }
        else continue;
        if (!b) continue;
        if (to_super) copy_cut(b, x0 - bx, y0 - by, l->superbitmap, x0 - ox, y0 - oy, x1 - x0 + 1, y1 - y0 + 1);
        else copy_cut(l->superbitmap, x0 - ox, y0 - oy, b, x0 - bx, y0 - by, x1 - x0 + 1, y1 - y0 + 1);
    }
}

// One drawing call's RastPort: the layer locked (the core draws with it
// held), the blitter idle, and a super bitmap layer's drawing done on its
// super bitmap, then copied to the layer's pieces (ours: OS 3.2 draws the
// pieces and keeps the super bitmap for SyncSBitMap; the pixels seen are
// the same).
struct target {
    gfx_env *e;
    gfx_rastport *rp;       // the caller's
    gfx_rastport *use;      // what to draw on
    gfx_rastport tmp;
    gfx_layer *layer;
    bool super;
};

void begin(target &t, gfx_env *e, gfx_rastport *rp)
{
    t.e = e;
    t.rp = rp;
    t.use = rp;
    t.layer = static_cast<gfx_layer *>(rp->layer);
    t.super = false;
    gfx_hook_wait_blit(e);
    if (!t.layer) return;
    e->sem_obtain(e->ctx, &t.layer->lock);
    if ((t.layer->flags & GFX_LAYERSUPER) && t.layer->superbitmap) {
        assign(t.tmp, *rp);
        t.tmp.layer = nullptr;
        t.tmp.bitmap = t.layer->superbitmap;
        t.use = &t.tmp;
        t.super = true;
    }
}

// done: the RastPort's box drawn in (super bitmap layers copy it), or
// nullptr for all of it
void end(target &t, const gfx_rectangle *done)
{
    if (t.super) {
        // what drawing changes in the RastPort itself
        t.rp->cp_x = t.tmp.cp_x;
        t.rp->cp_y = t.tmp.cp_y;
        t.rp->linpatcnt = t.tmp.linpatcnt;
        t.rp->flags = t.tmp.flags;
        super_xfer(t.layer, false, done);
    }
    if (t.layer) t.e->sem_release(t.e->ctx, &t.layer->lock);
}

// A rectangle call on a super bitmap: what a layer would clip, the
// super bitmap's edges clip (the core does not clip an unlayered RastPort);
// false when nothing is left.
bool clip_super(const target &t, int32_t &x0, int32_t &y0, int32_t &x1, int32_t &y1)
{
    if (!t.super) return true;
    x0 = max32(int16_t(x0), 0);
    y0 = max32(int16_t(y0), 0);
    x1 = min32(int16_t(x1), int32_t(t.use->bitmap->bytesperrow) * 8 - 1);
    y1 = min32(int16_t(y1), int32_t(t.use->bitmap->rows) - 1);
    return x0 <= x1 && y0 <= y1;
}

gfx_rectangle box(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    gfx_rectangle r;
    r.minx = int16_t(min32(max32(min32(x0, x1), -32768), 32767));
    r.miny = int16_t(min32(max32(min32(y0, y1), -32768), 32767));
    r.maxx = int16_t(min32(max32(max32(x0, x1), -32768), 32767));
    r.maxy = int16_t(min32(max32(max32(y0, y1), -32768), 32767));
    return r;
}

// A RastPort's window, in its own coordinates: the bitmap unlayered, the
// layer's bounds moved by its scroll in a layer.
gfx_rectangle window_of(const gfx_rastport *rp)
{
    auto *l = static_cast<const gfx_layer *>(rp->layer);
    if (!l) return box(0, 0, int32_t(rp->bitmap->bytesperrow) * 8 - 1, int32_t(rp->bitmap->rows) - 1);
    return box(l->scroll_x, l->scroll_y, l->scroll_x + l->bounds.maxx - l->bounds.minx,
               l->scroll_y + l->bounds.maxy - l->bounds.miny);
}

// A layer the core's copies take: a simple refresh layer's covered pieces
// have no bitmap and are skipped as the core skips them, and the ClipRects
// already hold the clip region.
struct copy_layer {
    gfx_layer l;
    gfx_rastport rp;
};
gfx_rastport *as_copyable(copy_layer &c, gfx_rastport *rp)
{
    auto *l = static_cast<gfx_layer *>(rp->layer);
    if (!l) return rp;
    assign(c.l, *l);
    c.l.flags = uint16_t((c.l.flags & ~(GFX_LAYERSIMPLE | GFX_LAYERSUPER)) | GFX_LAYERSMART);
    c.l.clipregion = nullptr;
    assign(c.rp, *rp);
    c.rp.layer = &c.l;
    return &c.rp;
}

// A simple refresh destination gets damage, and its refresh bit, where a
// copy could not read its source: the whole destination rectangle when
// either layer has a hidden piece or a rectangle is not wholly in its
// window (ours, coarse: OS 3.2 damages the hidden parts only; the ABI test
// PROBEs it).
bool hidden_in(const gfx_rastport *rp, const gfx_rectangle &r)
{
    auto *l = static_cast<const gfx_layer *>(rp->layer);
    if (!l) return false;
    for (const gfx_cliprect *cr = l->cliprect; cr; cr = cr->next)
        if (cr->lobs) return true;
    gfx_rectangle w = window_of(rp);
    return r.minx < w.minx || r.miny < w.miny || r.maxx > w.maxx || r.maxy > w.maxy;
}

void damage(gfx_env *e, gfx_rastport *src, const gfx_rectangle &sr, gfx_rastport *dst, const gfx_rectangle &dr)
{
    auto *l = static_cast<gfx_layer *>(dst->layer);
    if (!l || !(l->flags & GFX_LAYERSIMPLE) || !l->damagelist) return;
    if (!hidden_in(src, sr) && !hidden_in(dst, dr)) return;
    gfx_rgn_mem m = {rgn_alloc, rgn_free, e};
    gfx_rect c = {int16_t(dr.minx - l->scroll_x), int16_t(dr.miny - l->scroll_y), int16_t(dr.maxx - l->scroll_x),
                  int16_t(dr.maxy - l->scroll_y)};
    gfx_rgn_rect(core_rgn(l->damagelist), GFX_RGN_OR, &c, &m);
    l->flags |= GFX_LAYERREFRESH;
}

// Every run of set bits of a one-plane mask (w x h, bpr bytes a row, bit
// 7 leftmost), at (x0, y0) in the RastPort, through fill(rp, xa, xb, y).
template <class F> void runs(const uint8_t *m, uint32_t bpr, int32_t x0, int32_t y0, int32_t w, int32_t h, F fill)
{
    for (int32_t y = 0; y < h; y++) {
        const uint8_t *r = m + uint32_t(y) * bpr;
        for (int32_t x = 0; x < w; x++) {
            if (!(r[x >> 3] & (0x80 >> (x & 7)))) continue;
            int32_t run = x;
            while (run + 1 < w && (r[(run + 1) >> 3] & (0x80 >> ((run + 1) & 7)))) run++;
            fill(x0 + x, x0 + run, y0 + y);
            x = run;
        }
    }
}

// One row of FgPen through DrawMode and Mask with no area pattern: each
// pixel once, as a line draws it.
void solid_span(gfx_rastport *rp, int32_t xa, int32_t xb, int32_t y)
{
    if (!rp->layer) {
        // unlayered, the core does not clip: keep to the bitmap
        xa = max32(xa, 0);
        xb = min32(xb, int32_t(rp->bitmap->bytesperrow) * 8 - 1);
        if (xa > xb || y < 0 || y >= rp->bitmap->rows) return;
    }
    uint16_t *ptrn = rp->areaptrn;
    int8_t sz = rp->areaptsz;
    rp->areaptrn = nullptr;
    rp->areaptsz = 0;
    ogfx_rp_RectFill(rp, xa, y, xb, y);
    rp->areaptrn = ptrn;
    rp->areaptsz = sz;
}

// The default backfill and a layer's BackFill hook over a rectangle of a
// layered RastPort (its coordinates), piece by piece as layers.library
// calls it: A2 a RastPort with no layer on the piece's bitmap, A1 the
// layer, the bounds in that bitmap and the layer's own coordinates of
// their top left.
void backfill(gfx_env *e, gfx_rastport *rp, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    auto *l = static_cast<gfx_layer *>(rp->layer);
    void *hook = l->backfill;
    if (hook == reinterpret_cast<void *>(1)) return; // LAYERS_NOBACKFILL
    int32_t ox = l->bounds.minx - l->scroll_x, oy = l->bounds.miny - l->scroll_y;
    for (gfx_cliprect *cr = l->cliprect; cr; cr = cr->next) {
        int32_t a = max32(cr->bounds.minx, x0 + ox), b = max32(cr->bounds.miny, y0 + oy);
        int32_t c = min32(cr->bounds.maxx, x1 + ox), d = min32(cr->bounds.maxy, y1 + oy);
        if (a > c || b > d) continue;
        gfx_bitmap *bm;
        int32_t bx = 0, by = 0;
        if (!cr->lobs) bm = rp->bitmap;
        else if (cr->bitmap) { bm = cr->bitmap; bx = cr->bounds.minx & ~15; by = cr->bounds.miny; }
        else continue;
        if (!hook) {
            // ours, ABI test: pen 0 through the RastPort's Mask, as unlayered EraseRect
            gfx_planar p;
            planar_of(bm, &p);
            int32_t bw = int32_t(bm->bytesperrow) * 8, bh = bm->rows;
            int32_t xa = max32(a - bx, 0), xb = min32(c - bx, bw - 1);
            for (int32_t y = max32(b - by, 0); y <= min32(d - by, bh - 1); y++)
                if (xa <= xb) gfx_planar_span(&p, xa, xb, y, 0xFFFF, 0, 0, rp->mask, GFX_JAM1);
            continue;
        }
        gfx_rastport hrp;
        zero(&hrp, sizeof hrp);
        hrp.bitmap = bm;
        hrp.mask = 0xFF;
        hrp.fgpen = -1;
        hrp.aolpen = -1;
        hrp.drawmode = GFX_JAM2_MODE;
        hrp.lineptrn = 0xFFFF;
        hrp.linpatcnt = 15;
        hrp.flags = GFX_FRST_DOT;
        hrp.font = rp->font;
        gfx_backfillmsg msg;
        msg.layer = l;
        msg.bounds = box(a - bx, b - by, c - bx, d - by);
        msg.offsetx = a - l->bounds.minx + l->scroll_x;
        msg.offsety = b - l->bounds.miny + l->scroll_y;
        e->call_hook(e->ctx, hook, &hrp, &msg);
    }
}

void erase(gfx_env *e, gfx_rastport *rp, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (!ogfx_rp_EraseRect(rp, x0, y0, x1, y1)) backfill(e, rp, int16_t(x0), int16_t(y0), int16_t(x1), int16_t(y1));
}

// ScrollRaster and ScrollRasterBF: the part that stays moves, the rest is
// filled with solid BgPen (or, bf, the backfill).
void scroll(gfx_env *e, gfx_rastport *rp, int32_t dx, int32_t dy, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
            bool bf)
{
    dx = int16_t(dx); dy = int16_t(dy); x0 = int16_t(x0); y0 = int16_t(y0); x1 = int16_t(x1); y1 = int16_t(y1);
    if (x0 > x1 || y0 > y1) return;
    if (!bf && ogfx_rp_takes_copy(rp) && ogfx_rp_ScrollRaster(rp, dx, dy, x0, y0, x1, y1)) return;
    if (!dx && !dy) return;
    gfx_rectangle whole = box(x0, y0, x1, y1);
    if (rp->layer) {
        // in a layer only its window can change (ours: OS 3.2 here is the ABI test's)
        gfx_rectangle w = window_of(rp);
        x0 = max32(x0, w.minx); y0 = max32(y0, w.miny); x1 = min32(x1, w.maxx); y1 = min32(y1, w.maxy);
        if (x0 > x1 || y0 > y1) return;
    }
    int32_t w = x1 - x0 + 1, h = y1 - y0 + 1, adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    if (adx < w && ady < h) {
        copy_layer c;
        gfx_rastport *cp = as_copyable(c, rp);
        ogfx_rp_ClipBlit(cp, x0 + max32(dx, 0), y0 + max32(dy, 0), cp, x0 + max32(-dx, 0), y0 + max32(-dy, 0),
                         w - adx, h - ady, 0xC0);
    }
    damage(e, rp, whole, rp, whole);
    gfx_rectangle f[4];
    int n = 0;
    if (adx >= w || ady >= h) f[n++] = box(x0, y0, x1, y1);
    else {
        if (dx > 0) f[n++] = box(x1 - dx + 1, y0, x1, y1);
        if (dx < 0) f[n++] = box(x0, y0, x0 - dx - 1, y1);
        if (dy > 0) f[n++] = box(x0, y1 - dy + 1, x1, y1);
        if (dy < 0) f[n++] = box(x0, y0, x1, y0 - dy - 1);
    }
    for (int i = 0; i < n; i++) {
        if (bf) { erase(e, rp, f[i].minx, f[i].miny, f[i].maxx, f[i].maxy); continue; }
        int8_t pen = rp->fgpen, mode = rp->drawmode;
        uint16_t *ptrn = rp->areaptrn;
        rp->fgpen = rp->bgpen; rp->drawmode = GFX_JAM1_MODE; rp->areaptrn = nullptr;
        ogfx_rp_RectFill(rp, f[i].minx, f[i].miny, f[i].maxx, f[i].maxy);
        rp->fgpen = pen; rp->drawmode = mode; rp->areaptrn = ptrn;
    }
}

// Flood's mask: TmpRas bits over the RastPort's window, set for every
// pixel 4-connected to (x, y) that matches. Sweeps down and up, left and
// right, until nothing changes: no stack, no memory but the TmpRas.
struct flood_state {
    gfx_rastport *rp;
    uint8_t *m;
    uint32_t bpr;
    int32_t ox, oy, w, h;
    uint32_t mode, seed, depthmask;
};

bool fl_get(const flood_state &f, int32_t x, int32_t y)
{
    return f.m[uint32_t(y) * f.bpr + uint32_t(x >> 3)] & (0x80 >> (x & 7));
}
void fl_set(flood_state &f, int32_t x, int32_t y) { f.m[uint32_t(y) * f.bpr + uint32_t(x >> 3)] |= uint8_t(0x80 >> (x & 7)); }

bool fl_match(const flood_state &f, int32_t x, int32_t y)
{
    int32_t v = ogfx_rp_ReadPixel(f.rp, f.ox + x, f.oy + y);
    if (v < 0) return false;
    // mode 0: up to the outline pen; 1: the seed's colour (autodoc)
    return f.mode ? uint32_t(v) == f.seed : uint32_t(v) != (uint32_t(uint8_t(f.rp->aolpen)) & f.depthmask);
}

bool fl_try(flood_state &f, int32_t x, int32_t y)
{
    if (fl_get(f, x, y)) return false;
    bool near = (x > 0 && fl_get(f, x - 1, y)) || (x + 1 < f.w && fl_get(f, x + 1, y)) ||
                (y > 0 && fl_get(f, x, y - 1)) || (y + 1 < f.h && fl_get(f, x, y + 1));
    if (!near || !fl_match(f, x, y)) return false;
    fl_set(f, x, y);
    return true;
}

} // namespace

extern "C" {

// ---- bitmaps and rasters -----------------------------------------------------

void gfx_InitBitMap(gfx_env *, gfx_bitmap *bm, int32_t depth, int32_t width, int32_t height)
{
    // autodoc: the planes are the caller's to fill; they are left as they are
    bm->depth = uint8_t(depth);
    bm->rows = uint16_t(height);
    bm->bytesperrow = uint16_t(row_bytes(uint32_t(uint16_t(width))));
    bm->flags = 0;
    bm->pad = 0;
}

uint8_t *gfx_AllocRaster(gfx_env *e, uint32_t width, uint32_t height)
{
    // RASSIZE: rows of whole words
    uint32_t n = uint32_t(uint16_t(height)) * row_bytes(uint16_t(width));
    return static_cast<uint8_t *>(mem_alloc(e, n, GFX_MEMF_CHIP));
}

void gfx_FreeRaster(gfx_env *e, uint8_t *p, uint32_t width, uint32_t height)
{
    mem_free(e, p, uint32_t(uint16_t(height)) * row_bytes(uint16_t(width)));
}

gfx_bitmap *gfx_AllocBitMap(gfx_env *e, uint32_t width, uint32_t height, uint32_t depth, uint32_t flags,
                            const gfx_bitmap *)
{
    // planar only: more than 8 planes is a chunky/RTG bitmap (not yet)
    if (depth > 8 || !width || !height) return nullptr;
    uint32_t bpr = row_bytes(width);
    // a displayable bitmap on AGA keeps every row to 64 bits for the fetch
    // modes (ABI test: OS 3.2's rounding)
    if ((flags & GFX_BMF_DISPLAYABLE) && e->base && (e->base->chiprevbits0 & GFX_GFXF_AA_ALICE))
        bpr = (bpr + 7) & ~7u;
    auto *a = static_cast<bm_alloc *>(mem_alloc(e, sizeof(bm_alloc), GFX_MEMF_PUBLIC | GFX_MEMF_CLEAR));
    if (!a) return nullptr;
    uint32_t memf = GFX_MEMF_CHIP | ((flags & GFX_BMF_CLEAR) ? GFX_MEMF_CLEAR : 0);
    a->magic = BM_MAGIC;
    a->bm.depth = uint8_t(depth);
    a->bm.rows = uint16_t(height);
    if ((flags & GFX_BMF_INTERLEAVED) && depth > 1) {
        // one block, the planes' rows one after another
        uint8_t *p = static_cast<uint8_t *>(mem_alloc(e, bpr * depth * height, memf));
        if (p) {
            a->plane_size = bpr * depth * height;
            a->bm.bytesperrow = uint16_t(bpr * depth);
            a->bm.flags = uint8_t(GFX_BMF_INTERLEAVED | (flags & GFX_BMF_DISPLAYABLE));
            for (uint32_t i = 0; i < depth; i++) a->bm.planes[i] = p + i * bpr;
            return &a->bm;
        }
        // ours, ABI test: no block that big, so plane by plane
    }
    a->plane_size = bpr * height;
    a->bm.bytesperrow = uint16_t(bpr);
    a->bm.flags = uint8_t(flags & GFX_BMF_DISPLAYABLE);
    for (uint32_t i = 0; i < depth; i++) {
        a->bm.planes[i] = static_cast<uint8_t *>(mem_alloc(e, a->plane_size, memf));
        if (!a->bm.planes[i]) {
            gfx_FreeBitMap(e, &a->bm);
            return nullptr;
        }
    }
    return &a->bm;
}

void gfx_FreeBitMap(gfx_env *e, gfx_bitmap *bm)
{
    if (!bm) return;
    auto *a = reinterpret_cast<bm_alloc *>(bm);
    if (a->magic != BM_MAGIC) return;   // not AllocBitMap's: autodoc says never
    gfx_hook_wait_blit(e);
    if (bm->flags & GFX_BMF_INTERLEAVED) mem_free(e, bm->planes[0], a->plane_size);
    else
        for (uint32_t i = 0; i < bm->depth && i < 8; i++) mem_free(e, bm->planes[i], a->plane_size);
    a->magic = 0;
    mem_free(e, a, sizeof(bm_alloc));
}

uint32_t gfx_GetBitMapAttr(gfx_env *, const gfx_bitmap *bm, uint32_t attr)
{
    switch (attr) {
    case GFX_BMA_HEIGHT: return bm->rows;
    case GFX_BMA_DEPTH: return bm->depth;
    case GFX_BMA_WIDTH:
        if ((bm->flags & GFX_BMF_INTERLEAVED) && bm->depth) return uint32_t(bm->bytesperrow / bm->depth) * 8;
        return uint32_t(bm->bytesperrow) * 8;
    case GFX_BMA_FLAGS:
        return uint32_t(bm->flags & (GFX_BMF_DISPLAYABLE | GFX_BMF_INTERLEAVED)) | GFX_BMF_STANDARD;
    default: return 0; // ABI test: an attribute the autodoc does not list
    }
}

// ScalerDiv: factor * numerator / denominator, rounded to nearest (ABI test:
// the rounding, and a result of 0 for a factor that is not).
uint32_t gfx_ScalerDiv(gfx_env *, uint32_t factor, uint32_t numerator, uint32_t denominator)
{
    factor &= 0xFFFF; numerator &= 0xFFFF; denominator &= 0xFFFF;
    if (!denominator) return 0;
    return (factor * numerator + denominator / 2) / denominator;
}

// BitMapScale: the destination size from ScalerDiv, written back, and each
// destination pixel the source pixel its position scales from (nearest,
// ABI test: OS 3.2's DDA).
void gfx_BitMapScale(gfx_env *e, gfx_bitscaleargs *a)
{
    a->destwidth = uint16_t(gfx_ScalerDiv(e, a->srcwidth, a->xdestfactor, a->xsrcfactor));
    a->destheight = uint16_t(gfx_ScalerDiv(e, a->srcheight, a->ydestfactor, a->ysrcfactor));
    if (!a->srcbitmap || !a->destbitmap || !a->destwidth || !a->destheight || !a->xdestfactor || !a->ydestfactor)
        return;
    gfx_hook_wait_blit(e);
    gfx_planar s, d;
    planar_of(a->srcbitmap, &s);
    planar_of(a->destbitmap, &d);
    uint32_t sw = s.bytes_per_row * 8, dw = d.bytes_per_row * 8;
    for (uint32_t j = 0; j < a->destheight; j++) {
        uint32_t sy = a->srcy + min32(int32_t(j * a->ysrcfactor / a->ydestfactor), int32_t(a->srcheight) - 1);
        uint32_t dy = a->desty + j;
        if (sy >= s.rows || dy >= d.rows) continue;
        for (uint32_t i = 0; i < a->destwidth; i++) {
            uint32_t sx = a->srcx + min32(int32_t(i * a->xsrcfactor / a->xdestfactor), int32_t(a->srcwidth) - 1);
            uint32_t dx = a->destx + i;
            if (sx >= sw || dx >= dw) continue;
            gfx_planar_put(&d, int32_t(dx), int32_t(dy), gfx_planar_get(&s, int32_t(sx), int32_t(sy)), 0xFF, GFX_SET);
        }
    }
}

// ---- RastPort state --------------------------------------------------------------

void gfx_InitRastPort(gfx_env *e, gfx_rastport *rp)
{
    // the autodoc's defaults (ABI test: every byte, PROBE INITRP)
    zero(rp, sizeof *rp);
    rp->mask = 0xFF;
    rp->fgpen = -1;
    rp->bgpen = 0;
    rp->aolpen = -1;
    rp->drawmode = GFX_JAM2_MODE;
    rp->lineptrn = 0xFFFF;
    rp->linpatcnt = 15;
    rp->flags = GFX_FRST_DOT;
    rp->penwidth = 1;
    rp->penheight = 1;
    if (e->base && e->base->defaultfont) gfx_hook_set_font(e, rp, e->base->defaultfont);
}

gfx_tmpras *gfx_InitTmpRas(gfx_env *, gfx_tmpras *tr, uint8_t *buffer, int32_t size)
{
    tr->rasptr = buffer;
    tr->size = size;
    return tr;
}

void gfx_InitArea(gfx_env *, gfx_areainfo *ai, void *buffer, int32_t maxvectors)
{
    // a vector is two WORDs, and its flag a byte after all of them: the
    // autodoc's buffer of 5 bytes a vector
    ai->vctrtbl = ai->vctrptr = static_cast<int16_t *>(buffer);
    ai->flagtbl = ai->flagptr = static_cast<int8_t *>(buffer) + 4 * int16_t(maxvectors);
    ai->count = 0;
    ai->maxcount = int16_t(maxvectors);
    ai->firstx = ai->firsty = 0;
}

void gfx_SetAPen(gfx_env *, gfx_rastport *rp, uint32_t pen) { rp->fgpen = int8_t(pen); }
void gfx_SetBPen(gfx_env *, gfx_rastport *rp, uint32_t pen) { rp->bgpen = int8_t(pen); }
void gfx_SetDrMd(gfx_env *, gfx_rastport *rp, uint32_t mode) { rp->drawmode = int8_t(mode); }

void gfx_SetABPenDrMd(gfx_env *, gfx_rastport *rp, uint32_t apen, uint32_t bpen, uint32_t mode)
{
    rp->fgpen = int8_t(apen);
    rp->bgpen = int8_t(bpen);
    rp->drawmode = int8_t(mode);
}

uint32_t gfx_SetOutlinePen(gfx_env *, gfx_rastport *rp, uint32_t pen)
{
    uint32_t old = uint8_t(rp->aolpen);
    rp->aolpen = int8_t(pen);
    rp->flags |= GFX_AREAOUTLINE; // as the SetOPen macro always has
    return old;
}

uint32_t gfx_SetWriteMask(gfx_env *, gfx_rastport *rp, uint32_t mask)
{
    rp->mask = uint8_t(mask);
    return 1; // TRUE: a planar RastPort takes any mask
}

// A hint the planar drawing has no use for (ours, ABI test: what OS 3.2
// keeps of it).
void gfx_SetMaxPen(gfx_env *, gfx_rastport *, uint32_t) {}

uint32_t gfx_GetAPen(gfx_env *, const gfx_rastport *rp) { return uint8_t(rp->fgpen); }
uint32_t gfx_GetBPen(gfx_env *, const gfx_rastport *rp) { return uint8_t(rp->bgpen); }
uint32_t gfx_GetDrMd(gfx_env *, const gfx_rastport *rp) { return uint8_t(rp->drawmode); }
uint32_t gfx_GetOutlinePen(gfx_env *, const gfx_rastport *rp) { return uint8_t(rp->aolpen); }

void gfx_SetRPAttrsA(gfx_env *e, gfx_rastport *rp, const gfx_tagitem *tags)
{
    const gfx_tagitem *t;
    while ((t = next_tag(&tags))) {
        switch (t->tag) {
        case GFX_RPTAG_FONT: gfx_hook_set_font(e, rp, reinterpret_cast<void *>(uintptr_t(t->data))); break;
        case GFX_RPTAG_APEN: gfx_SetAPen(e, rp, t->data); break;
        case GFX_RPTAG_BPEN: gfx_SetBPen(e, rp, t->data); break;
        case GFX_RPTAG_DRMD: gfx_SetDrMd(e, rp, t->data); break;
        case GFX_RPTAG_OUTLINEPEN: gfx_SetOutlinePen(e, rp, t->data); break;
        case GFX_RPTAG_WRITEMASK: gfx_SetWriteMask(e, rp, t->data); break;
        case GFX_RPTAG_MAXPEN: gfx_SetMaxPen(e, rp, t->data); break;
        default: break; // DrawBounds is for GetRPAttrs; others are ignored
        }
    }
}

void gfx_GetRPAttrsA(gfx_env *e, gfx_rastport *rp, const gfx_tagitem *tags)
{
    const gfx_tagitem *t;
    while ((t = next_tag(&tags))) {
        auto *out = reinterpret_cast<uint32_t *>(uintptr_t(t->data));
        if (!out) continue;
        switch (t->tag) {
        case GFX_RPTAG_FONT: *out = uint32_t(uintptr_t(rp->font)); break;
        case GFX_RPTAG_APEN: *out = gfx_GetAPen(e, rp); break;
        case GFX_RPTAG_BPEN: *out = gfx_GetBPen(e, rp); break;
        case GFX_RPTAG_DRMD: *out = gfx_GetDrMd(e, rp); break;
        case GFX_RPTAG_OUTLINEPEN: *out = gfx_GetOutlinePen(e, rp); break;
        case GFX_RPTAG_WRITEMASK: *out = rp->mask; break;
        case GFX_RPTAG_MAXPEN:
            // ours, ABI test: the bitmap's highest pen
            *out = rp->bitmap ? (rp->bitmap->depth >= 8 ? 255u : (1u << rp->bitmap->depth) - 1) : 255u;
            break;
        case GFX_RPTAG_DRAWBOUNDS: {
            // ours, ABI test: the window the RastPort draws in, its own coordinates
            auto *r = reinterpret_cast<gfx_rectangle *>(out);
            if (rp->bitmap) *r = window_of(rp);
            else *r = box(0, 0, -1, -1);
            break;
        }
        default: break; // ABI test: an unknown tag's storage is left alone
        }
    }
}

// ---- colour maps and pens -----------------------------------------------------------

gfx_colormap *gfx_GetColorMap(gfx_env *e, int32_t entries)
{
    if (entries <= 0) return nullptr; // ABI test
    if (entries > 0xFFFF) return nullptr; // cm->count is a UWORD: FreeColorMap frees by it
    auto *cm = static_cast<gfx_colormap *>(mem_alloc(e, sizeof(gfx_colormap), GFX_MEMF_PUBLIC | GFX_MEMF_CLEAR));
    if (!cm) return nullptr;
    uint32_t n = uint32_t(entries) * 2;
    cm->colortable = static_cast<uint16_t *>(mem_alloc(e, n, GFX_MEMF_PUBLIC | GFX_MEMF_CLEAR));
    cm->lowcolorbits = static_cast<uint16_t *>(mem_alloc(e, n, GFX_MEMF_PUBLIC | GFX_MEMF_CLEAR));
    if (!cm->colortable || !cm->lowcolorbits) {
        mem_free(e, cm->colortable, n);
        mem_free(e, cm->lowcolorbits, n);
        mem_free(e, cm, sizeof(gfx_colormap));
        return nullptr;
    }
    cm->type = GFX_COLORMAP_TYPE_V39;
    cm->count = uint16_t(entries);
    // ABI test: OS 3.2's starting colours, sprite resolutions and bases
    cm->spriteresolution = 0xFF; // SPRITERESN_DEFAULT
    cm->spriteresdefault = 0xFF;
    return cm;
}

void gfx_FreeColorMap(gfx_env *e, gfx_colormap *cm)
{
    if (!cm) return;
    if (gfx_paletteextra *pe = cm->palextra) {
        mem_free(e, pe->refcnt, cm->count);
        mem_free(e, pe->alloclist, cm->count);
        mem_free(e, pe, sizeof(gfx_paletteextra));
    }
    mem_free(e, cm->colortable, uint32_t(cm->count) * 2);
    mem_free(e, cm->lowcolorbits, uint32_t(cm->count) * 2);
    mem_free(e, cm, sizeof(gfx_colormap));
}

void gfx_SetRGB4CM(gfx_env *, gfx_colormap *cm, int32_t index, uint32_t r, uint32_t g, uint32_t b)
{
    if (!cm || index < 0 || index >= cm->count) return;
    uint16_t v = uint16_t((r & 15) << 8 | (g & 15) << 4 | (b & 15));
    cm->colortable[index] = v;
    if (cm->lowcolorbits) cm->lowcolorbits[index] = v; // 4 bits stand for 8 by repeating
}

void gfx_SetRGB32CM(gfx_env *, gfx_colormap *cm, uint32_t n, uint32_t r, uint32_t g, uint32_t b)
{
    if (!cm || n >= cm->count) return;
    cm->colortable[n] = uint16_t((r >> 28) << 8 | (g >> 28) << 4 | (b >> 28));
    if (cm->lowcolorbits) cm->lowcolorbits[n] = uint16_t((r >> 24 & 15) << 8 | (g >> 24 & 15) << 4 | (b >> 24 & 15));
}

int32_t gfx_GetRGB4(gfx_env *, const gfx_colormap *cm, int32_t entry)
{
    if (!cm || entry < 0 || entry >= cm->count) return -1;
    return cm->colortable[entry] & 0xFFF;
}

void gfx_GetRGB32(gfx_env *, const gfx_colormap *cm, uint32_t first, uint32_t ncolors, uint32_t *table)
{
    if (!cm) return;
    // ours, ABI test: entries past the map's are not written
    for (uint32_t i = 0; i < ncolors && first + i < cm->count; i++)
        for (int c = 0; c < 3; c++) table[3 * i + c] = cm_rgb8(cm, first + i, 8 - 4 * c) * 0x01010101u;
}

void gfx_SetRGB4(gfx_env *e, void *vp, int32_t index, uint32_t r, uint32_t g, uint32_t b)
{
    gfx_colormap *cm = vp_colormap(vp);
    if (!cm || index < 0 || index >= cm->count) return;
    gfx_SetRGB4CM(e, cm, index, r, g, b);
    gfx_hook_vp_colors(e, vp, uint32_t(index), 1);
}

void gfx_SetRGB32(gfx_env *e, void *vp, uint32_t n, uint32_t r, uint32_t g, uint32_t b)
{
    gfx_colormap *cm = vp_colormap(vp);
    if (!cm || n >= cm->count) return;
    gfx_SetRGB32CM(e, cm, n, r, g, b);
    gfx_hook_vp_colors(e, vp, n, 1);
}

void gfx_LoadRGB4(gfx_env *e, void *vp, const uint16_t *colors, int32_t count)
{
    gfx_colormap *cm = vp_colormap(vp);
    if (!cm || count <= 0) return;
    int32_t n = min32(count, cm->count);
    for (int32_t i = 0; i < n; i++) gfx_SetRGB4CM(e, cm, i, colors[i] >> 8, colors[i] >> 4, colors[i]);
    gfx_hook_vp_colors(e, vp, 0, uint32_t(n));
}

void gfx_LoadRGB32(gfx_env *e, void *vp, const uint32_t *table)
{
    gfx_colormap *cm = vp_colormap(vp);
    if (!cm || !table) return;
    // runs of (count << 16 | first, then count R, G, B), ended by a count of 0
    for (;;) {
        uint32_t n = table[0] >> 16, first = table[0] & 0xFFFF;
        if (!n) break;
        table++;
        for (uint32_t i = 0; i < n; i++, table += 3) gfx_SetRGB32CM(e, cm, first + i, table[0], table[1], table[2]);
        if (first < cm->count) gfx_hook_vp_colors(e, vp, first, uint32_t(min32(int32_t(n), int32_t(cm->count - first))));
    }
}

// The pens a PalExtra shares: as many as its ViewPort shows ("the set of
// available colors will be determined by the mode and depth of the
// viewport", the autodoc): 1 << depth, 16 for HAM6 and 64 for HAM8, 32 for
// EHB, never more than the map holds; the whole map without a ViewPort,
// RasInfo or bitmap. A Workbench opened with SA_FullPalette has a map of
// 256 for its 16 colours, and pens past 15 drew nothing.
static uint32_t vp_pens(const void *vp, uint32_t count)
{
    if (!vp) return count;
    const char *v = static_cast<const char *>(vp);
    uint16_t modes = *reinterpret_cast<const uint16_t *>(v + 32);         // ViewPort.Modes
    const char *ri = *reinterpret_cast<char *const *>(v + 36);            // ViewPort.RasInfo
    if (!ri) return count;
    const gfx_bitmap *bm = *reinterpret_cast<gfx_bitmap *const *>(ri + 4); // RasInfo.BitMap
    if (!bm || !bm->depth) return count;
    uint32_t n;
    if (modes & 0x0800) n = bm->depth >= 8 ? 64 : 16;  // HAM
    else if (modes & 0x0080) n = 32;                    // EXTRA_HALFBRITE
    else n = bm->depth >= 16 ? 65536 : 1u << bm->depth;
    return n < count ? n : count;
}

int32_t gfx_AttachPalExtra(gfx_env *e, gfx_colormap *cm, void *vp)
{
    if (!cm) return 1;
    if (cm->palextra) return 0; // ABI test: a second call
    auto *pe = static_cast<gfx_paletteextra *>(mem_alloc(e, sizeof(gfx_paletteextra), GFX_MEMF_PUBLIC | GFX_MEMF_CLEAR));
    if (!pe) return 1;
    pe->refcnt = static_cast<uint8_t *>(mem_alloc(e, cm->count, GFX_MEMF_PUBLIC | GFX_MEMF_CLEAR));
    pe->alloclist = static_cast<uint8_t *>(mem_alloc(e, cm->count, GFX_MEMF_PUBLIC | GFX_MEMF_CLEAR));
    if (!pe->refcnt || !pe->alloclist) {
        mem_free(e, pe->refcnt, cm->count);
        mem_free(e, pe->alloclist, cm->count);
        mem_free(e, pe, sizeof(gfx_paletteextra));
        return 1;
    }
    e->sem_init(e->ctx, &pe->semaphore);
    // every pen starts free (ours: AllocList holds each pen's state, not
    // OS 3.2's own lists; FirstFree is the pen ObtainPen hands out next)
    uint32_t n = vp_pens(vp, cm->count);
    pe->nfree = uint16_t(n);
    pe->firstfree = uint16_t(n - 1);
    pe->viewport = vp;
    pe->sharablecolors = uint16_t(n);
    cm->palextra = pe;
    return 0;
}

int32_t gfx_ObtainPen(gfx_env *e, gfx_colormap *cm, uint32_t n, uint32_t r, uint32_t g, uint32_t b, uint32_t flags)
{
    gfx_paletteextra *pe = cm ? cm->palextra : nullptr;
    if (!pe) return -1;
    e->sem_obtain(e->ctx, &pe->semaphore);
    int32_t pen = -1;
    int32_t want = int32_t(n);
    if (want == -1) want = pen_take_free(cm);
    if (want >= 0 && want < pe->sharablecolors) {
        uint8_t st = pe->alloclist[want];
        if (st == PEN_FREE) {
            pen_mark(cm, uint32_t(want), (flags & GFX_PEN_EXCLUSIVE) ? PEN_EXCL : PEN_SHARED);
            if (!(flags & GFX_PEN_NO_SETCOLOR)) pen_set_color(e, cm, uint32_t(want), r, g, b);
            pen = want;
        } else if (st == PEN_SHARED && !(flags & GFX_PEN_EXCLUSIVE) &&
                   ((flags & GFX_PEN_NO_SETCOLOR) || !color_dist(cm, uint32_t(want), r, g, b))) {
            // ABI test: sharing a named pen asks for its colour
            pen_share(pe, uint32_t(want));
            pen = want;
        }
    }
    int32_t next = pen_take_free(cm);
    pe->firstfree = uint16_t(next < 0 ? 0 : next);
    e->sem_release(e->ctx, &pe->semaphore);
    return pen;
}

void gfx_ReleasePen(gfx_env *e, gfx_colormap *cm, uint32_t n)
{
    gfx_paletteextra *pe = cm ? cm->palextra : nullptr;
    if (!pe || n >= pe->sharablecolors) return;
    e->sem_obtain(e->ctx, &pe->semaphore);
    if (pe->alloclist[n] == PEN_SHARED) {
        if (pe->refcnt[n] > 1) pe->refcnt[n]--;
        else pen_mark(cm, n, PEN_FREE);
    } else if (pe->alloclist[n] == PEN_EXCL) pen_mark(cm, n, PEN_FREE);
    int32_t next = pen_take_free(cm);
    pe->firstfree = uint16_t(next < 0 ? 0 : next);
    e->sem_release(e->ctx, &pe->semaphore);
}

int32_t gfx_ObtainBestPenA(gfx_env *e, gfx_colormap *cm, uint32_t r, uint32_t g, uint32_t b,
                           const gfx_tagitem *tags)
{
    gfx_paletteextra *pe = cm ? cm->palextra : nullptr;
    if (!pe) return -1;
    const gfx_tagitem *t = find_tag(GFX_OBP_PRECISION, tags);
    int32_t prec = t ? int32_t(t->data) : GFX_PRECISION_IMAGE;
    t = find_tag(GFX_OBP_FAILIFBAD, tags);
    bool failbad = t && t->data;
    // ABI test: how near is near enough. Ours: each 8-bit gun within
    // 8 + 2 * precision, exact for PRECISION_EXACT.
    uint32_t tol = prec < 0 ? 0 : uint32_t(8 + 2 * prec);
    uint32_t limit = 3 * tol * tol;
    e->sem_obtain(e->ctx, &pe->semaphore);
    int32_t best = -1;
    uint32_t bestd = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < pe->sharablecolors; i++) {
        if (pe->alloclist[i] != PEN_SHARED) continue;
        uint32_t d = color_dist(cm, i, r, g, b);
        if (d < bestd) { bestd = d; best = int32_t(i); }
    }
    int32_t pen = -1;
    if (best >= 0 && bestd <= limit) {
        pen_share(pe, uint32_t(best));
        pen = best;
    } else if ((pen = pen_take_free(cm)) >= 0) {
        pen_mark(cm, uint32_t(pen), PEN_SHARED);
        pen_set_color(e, cm, uint32_t(pen), r, g, b);
    } else if (!failbad && best >= 0) {
        pen_share(pe, uint32_t(best));
        pen = best;
    }
    int32_t next = pen_take_free(cm);
    pe->firstfree = uint16_t(next < 0 ? 0 : next);
    e->sem_release(e->ctx, &pe->semaphore);
    return pen;
}

int32_t gfx_FindColor(gfx_env *, gfx_colormap *cm, uint32_t r, uint32_t g, uint32_t b, int32_t maxcolor)
{
    if (!cm || !cm->count) return 0;
    int32_t last = maxcolor < 0 || maxcolor >= cm->count ? int32_t(cm->count) - 1 : maxcolor;
    int32_t best = 0;
    uint32_t bestd = 0xFFFFFFFFu;
    // ABI test: OS 3.2's distance; ours sums the squares, the lowest pen wins a tie
    for (int32_t i = 0; i <= last; i++) {
        uint32_t d = color_dist(cm, uint32_t(i), r, g, b);
        if (d < bestd) { bestd = d; best = i; }
    }
    return best;
}

// ---- regions --------------------------------------------------------------------

gfx_rgn *gfx_NewRegion(gfx_env *e)
{
    auto *r = static_cast<gfx_rgn *>(mem_alloc(e, sizeof(gfx_rgn), GFX_MEMF_PUBLIC));
    if (r) gfx_rgn_init(core_rgn(r));
    return r;
}

void gfx_DisposeRegion(gfx_env *e, gfx_rgn *r)
{
    if (!r) return;
    gfx_ClearRegion(e, r);
    mem_free(e, r, sizeof(gfx_rgn));
}

void gfx_ClearRegion(gfx_env *e, gfx_rgn *r)
{
    gfx_rgn_mem m = {rgn_alloc, rgn_free, e};
    gfx_rgn_clear(core_rgn(r), &m);
}

static int32_t rect_op(gfx_env *e, gfx_rgn *r, int op, const gfx_rectangle *rect)
{
    gfx_rgn_mem m = {rgn_alloc, rgn_free, e};
    gfx_rect c = {rect->minx, rect->miny, rect->maxx, rect->maxy};
    return gfx_rgn_rect(core_rgn(r), op, &c, &m) ? 1 : 0;
}

void gfx_AndRectRegion(gfx_env *e, gfx_rgn *r, const gfx_rectangle *rect) { rect_op(e, r, GFX_RGN_AND, rect); }
int32_t gfx_OrRectRegion(gfx_env *e, gfx_rgn *r, const gfx_rectangle *rect) { return rect_op(e, r, GFX_RGN_OR, rect); }
int32_t gfx_XorRectRegion(gfx_env *e, gfx_rgn *r, const gfx_rectangle *rect) { return rect_op(e, r, GFX_RGN_XOR, rect); }
int32_t gfx_ClearRectRegion(gfx_env *e, gfx_rgn *r, const gfx_rectangle *rect)
{
    return rect_op(e, r, GFX_RGN_CLEAR, rect);
}

static int32_t rgn_op(gfx_env *e, const gfx_rgn *src, gfx_rgn *dst, int op)
{
    gfx_rgn_mem m = {rgn_alloc, rgn_free, e};
    return gfx_rgn_region(core_rgn(dst), op, core_rgn(src), &m) ? 1 : 0;
}

int32_t gfx_OrRegionRegion(gfx_env *e, const gfx_rgn *s, gfx_rgn *d) { return rgn_op(e, s, d, GFX_RGN_OR); }
int32_t gfx_XorRegionRegion(gfx_env *e, const gfx_rgn *s, gfx_rgn *d) { return rgn_op(e, s, d, GFX_RGN_XOR); }
int32_t gfx_AndRegionRegion(gfx_env *e, const gfx_rgn *s, gfx_rgn *d) { return rgn_op(e, s, d, GFX_RGN_AND); }

// ---- layers --------------------------------------------------------------------------

void gfx_LockLayerRom(gfx_env *e, gfx_layer *l)
{
    if (l) e->sem_obtain(e->ctx, &l->lock);
}

void gfx_UnlockLayerRom(gfx_env *e, gfx_layer *l)
{
    if (l) e->sem_release(e->ctx, &l->lock);
}

int32_t gfx_AttemptLockLayerRom(gfx_env *e, gfx_layer *l)
{
    return l && e->sem_attempt(e->ctx, &l->lock) ? 1 : 0;
}

void gfx_SyncSBitMap(gfx_env *e, gfx_layer *l)
{
    if (!l) return;
    gfx_hook_wait_blit(e);
    e->sem_obtain(e->ctx, &l->lock);
    super_xfer(l, true, nullptr);
    e->sem_release(e->ctx, &l->lock);
}

void gfx_CopySBitMap(gfx_env *e, gfx_layer *l)
{
    if (!l) return;
    gfx_hook_wait_blit(e);
    e->sem_obtain(e->ctx, &l->lock);
    super_xfer(l, false, nullptr);
    e->sem_release(e->ctx, &l->lock);
}

// ---- drawing ----------------------------------------------------------------------------

int32_t gfx_WritePixel(gfx_env *e, gfx_rastport *rp, int32_t x, int32_t y)
{
    target t;
    begin(t, e, rp);
    int32_t rc;
    if (t.super) {
        gfx_rectangle w = window_of(t.use);
        // ours, ABI test: off the super bitmap answers -1 as off a layer
        rc = int16_t(x) < w.minx || int16_t(y) < w.miny || int16_t(x) > w.maxx || int16_t(y) > w.maxy
                 ? -1 : ogfx_rp_WritePixel(t.use, x, y);
    } else rc = ogfx_rp_WritePixel(t.use, x, y);
    gfx_rectangle r = box(int16_t(x), int16_t(y), int16_t(x), int16_t(y));
    end(t, &r);
    return rc;
}

int32_t gfx_ReadPixel(gfx_env *e, gfx_rastport *rp, int32_t x, int32_t y)
{
    target t;
    begin(t, e, rp);
    int32_t rc;
    if (t.super) {
        gfx_rectangle w = window_of(t.use);
        rc = int16_t(x) < w.minx || int16_t(y) < w.miny || int16_t(x) > w.maxx || int16_t(y) > w.maxy
                 ? -1 : ogfx_rp_ReadPixel(t.use, x, y);
    } else rc = ogfx_rp_ReadPixel(t.use, x, y);
    if (t.layer) e->sem_release(e->ctx, &t.layer->lock);
    return rc;
}

void gfx_SetRast(gfx_env *e, gfx_rastport *rp, uint32_t pen)
{
    target t;
    begin(t, e, rp);
    ogfx_rp_SetRast(t.use, pen);
    end(t, nullptr);
}

void gfx_Move(gfx_env *, gfx_rastport *rp, int32_t x, int32_t y) { ogfx_rp_Move(rp, x, y); }

void gfx_Draw(gfx_env *e, gfx_rastport *rp, int32_t x, int32_t y)
{
    gfx_rectangle r = box(rp->cp_x, rp->cp_y, int16_t(x), int16_t(y));
    target t;
    begin(t, e, rp);
    ogfx_rp_Draw(t.use, x, y);
    end(t, &r);
}

void gfx_PolyDraw(gfx_env *e, gfx_rastport *rp, int32_t count, const int16_t *xy)
{
    target t;
    begin(t, e, rp);
    ogfx_rp_PolyDraw(t.use, int16_t(count), xy);
    end(t, nullptr);
}

void gfx_RectFill(gfx_env *e, gfx_rastport *rp, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    gfx_rectangle r = box(int16_t(x0), int16_t(y0), int16_t(x1), int16_t(y1));
    target t;
    begin(t, e, rp);
    if (clip_super(t, x0, y0, x1, y1)) ogfx_rp_RectFill(t.use, x0, y0, x1, y1);
    end(t, &r);
}

void gfx_BltPattern(gfx_env *e, gfx_rastport *rp, const uint8_t *mask, int32_t x0, int32_t y0, int32_t x1,
                    int32_t y1, uint32_t maskbpr)
{
    gfx_rectangle r = box(int16_t(x0), int16_t(y0), int16_t(x1), int16_t(y1));
    target t;
    begin(t, e, rp);
    if (!mask && !clip_super(t, x0, y0, x1, y1)) {
    } else if (ogfx_rp_takes(t.use, mask != nullptr)) ogfx_rp_BltPattern(t.use, mask, x0, y0, x1, y1, maskbpr);
    else {
        // through a mask in a layer: each run of the mask's set bits, filled
        // as RectFill fills (ours, ABI test: OS 3.2 draws the blitter's way)
        x0 = int16_t(x0); y0 = int16_t(y0); x1 = int16_t(x1); y1 = int16_t(y1);
        if (x0 <= x1 && y0 <= y1)
            runs(mask, uint16_t(maskbpr), x0, y0, x1 - x0 + 1, y1 - y0 + 1,
                 [&](int32_t a, int32_t b, int32_t y) { ogfx_rp_RectFill(t.use, a, y, b, y); });
    }
    end(t, &r);
}

void gfx_EraseRect(gfx_env *e, gfx_rastport *rp, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    gfx_rectangle r = box(int16_t(x0), int16_t(y0), int16_t(x1), int16_t(y1));
    target t;
    begin(t, e, rp);
    if (clip_super(t, x0, y0, x1, y1)) erase(e, t.use, x0, y0, x1, y1);
    end(t, &r);
}

void gfx_DrawEllipse(gfx_env *e, gfx_rastport *rp, int32_t xc, int32_t yc, int32_t a, int32_t b)
{
    xc = int16_t(xc); yc = int16_t(yc); a = int16_t(a); b = int16_t(b);
    gfx_rectangle r = box(xc - a, yc - b, xc + a, yc + b);
    target t;
    begin(t, e, rp);
    if (!ogfx_rp_DrawEllipse(t.use, xc, yc, a, b) && a >= 0 && b >= 0) {
        // COMPLEMENT, INVERSVID or a line pattern: the core's ellipse into
        // a one-plane mask, then each pixel once through the DrawMode (ours,
        // ABI test: OS 3.2 runs the line pattern round it)
        uint32_t w = uint32_t(2 * a + 1), h = uint32_t(2 * b + 1), bpr = row_bytes(w);
        uint8_t *m = static_cast<uint8_t *>(mem_alloc(e, bpr * h, GFX_MEMF_PUBLIC | GFX_MEMF_CLEAR));
        if (m) {
            gfx_bitmap mb;
            zero(&mb, sizeof mb);
            mb.bytesperrow = uint16_t(bpr);
            mb.rows = uint16_t(h);
            mb.depth = 1;
            mb.planes[0] = m;
            gfx_rastport mr;
            zero(&mr, sizeof mr);
            mr.bitmap = &mb;
            mr.mask = 1;
            mr.fgpen = 1;
            mr.drawmode = GFX_JAM1_MODE;
            mr.lineptrn = 0xFFFF;
            ogfx_rp_DrawEllipse(&mr, a, b, a, b);
            runs(m, bpr, xc - a, yc - b, int32_t(w), int32_t(h),
                 [&](int32_t xa, int32_t xb, int32_t y) { solid_span(t.use, xa, xb, y); });
            mem_free(e, m, bpr * h);
        }
    }
    end(t, &r);
}

int32_t gfx_AreaMove(gfx_env *, gfx_rastport *rp, int32_t x, int32_t y)
{
    return rp->areainfo ? ogfx_rp_AreaMove(rp, x, y) : -1;
}

int32_t gfx_AreaDraw(gfx_env *, gfx_rastport *rp, int32_t x, int32_t y)
{
    return rp->areainfo ? ogfx_rp_AreaDraw(rp, x, y) : -1;
}

int32_t gfx_AreaEllipse(gfx_env *, gfx_rastport *rp, int32_t xc, int32_t yc, int32_t a, int32_t b)
{
    return rp->areainfo ? ogfx_rp_AreaEllipse(rp, xc, yc, a, b) : -1;
}

int32_t gfx_AreaEnd(gfx_env *e, gfx_rastport *rp)
{
    if (!rp->areainfo) return -1;
    target t;
    begin(t, e, rp);
    // what the core has not measured (NOCROSSFILL, patterns of more than 16
    // rows or of several colours, an outline past a layer's edge) it draws
    // its own way all the same (ours; the README lists them)
    int32_t rc = ogfx_rp_AreaEnd(t.use);
    end(t, nullptr);
    return rc;
}

int32_t gfx_Flood(gfx_env *e, gfx_rastport *rp, uint32_t mode, int32_t x, int32_t y)
{
    target t;
    begin(t, e, rp);
    flood_state f;
    gfx_rectangle w = window_of(t.use);
    f.rp = t.use;
    f.ox = w.minx;
    f.oy = w.miny;
    f.w = w.maxx - w.minx + 1;
    f.h = w.maxy - w.miny + 1;
    f.bpr = row_bytes(uint32_t(f.w));
    f.mode = mode;
    uint32_t depth = t.use->bitmap->depth;
    f.depthmask = depth >= 8 ? 0xFF : (1u << depth) - 1;
    x = int16_t(x) - f.ox;
    y = int16_t(y) - f.oy;
    gfx_tmpras *tr = t.use->tmpras;
    int32_t rc = 1;
    // the TmpRas holds the mask: a plane the window's size (autodoc: Flood
    // needs a TmpRas; too small is FALSE, ABI test)
    if (!tr || !tr->rasptr || tr->size < int32_t(f.bpr * uint32_t(f.h))) rc = 0;
    else if (x >= 0 && y >= 0 && x < f.w && y < f.h) {
        f.m = tr->rasptr;
        zero(f.m, f.bpr * uint32_t(f.h));
        int32_t s = ogfx_rp_ReadPixel(f.rp, f.ox + x, f.oy + y);
        f.seed = uint32_t(s);
        if (s >= 0 && fl_match(f, x, y)) {
            fl_set(f, x, y);
            for (bool changed = true; changed;) {
                changed = false;
                for (int32_t yy = 0; yy < f.h; yy++) {
                    for (int32_t xx = 0; xx < f.w; xx++) changed |= fl_try(f, xx, yy);
                    for (int32_t xx = f.w - 1; xx >= 0; xx--) changed |= fl_try(f, xx, yy);
                }
                for (int32_t yy = f.h - 1; yy >= 0; yy--) {
                    for (int32_t xx = 0; xx < f.w; xx++) changed |= fl_try(f, xx, yy);
                    for (int32_t xx = f.w - 1; xx >= 0; xx--) changed |= fl_try(f, xx, yy);
                }
            }
            // drawn as RectFill draws: FgPen, the area pattern, DrawMode, Mask
            runs(f.m, f.bpr, f.ox, f.oy, f.w, f.h,
                 [&](int32_t a, int32_t b, int32_t yy) { ogfx_rp_RectFill(t.use, a, yy, b, yy); });
        }
    }
    end(t, nullptr);
    return rc;
}

void gfx_ScrollRaster(gfx_env *e, gfx_rastport *rp, int32_t dx, int32_t dy, int32_t x0, int32_t y0, int32_t x1,
                      int32_t y1)
{
    gfx_rectangle r = box(int16_t(x0), int16_t(y0), int16_t(x1), int16_t(y1));
    target t;
    begin(t, e, rp);
    scroll(e, t.use, dx, dy, x0, y0, x1, y1, false);
    end(t, &r);
}

void gfx_ScrollRasterBF(gfx_env *e, gfx_rastport *rp, int32_t dx, int32_t dy, int32_t x0, int32_t y0, int32_t x1,
                        int32_t y1)
{
    gfx_rectangle r = box(int16_t(x0), int16_t(y0), int16_t(x1), int16_t(y1));
    target t;
    begin(t, e, rp);
    scroll(e, t.use, dx, dy, x0, y0, x1, y1, true);
    end(t, &r);
}

void gfx_ClipBlit(gfx_env *e, gfx_rastport *src, int32_t sx, int32_t sy, gfx_rastport *dst, int32_t dx, int32_t dy,
                  int32_t w, int32_t h, uint32_t minterm)
{
    sx = int16_t(sx); sy = int16_t(sy); dx = int16_t(dx); dy = int16_t(dy); w = int16_t(w); h = int16_t(h);
    if (w <= 0 || h <= 0) return;
    gfx_rectangle r = box(dx, dy, dx + w - 1, dy + h - 1);
    target td, ts;
    bool same = src->layer && src->layer == dst->layer;
    // two layers: locked in the order of their addresses, so two copies
    // the opposite ways round never each hold the lock the other waits on
    bool src_first = !same && src->layer && dst->layer &&
                     reinterpret_cast<uintptr_t>(src->layer) < reinterpret_cast<uintptr_t>(dst->layer);
    if (src_first) begin(ts, e, src);
    begin(td, e, dst);
    if (!same && !src_first) begin(ts, e, src);
    else if (same) {
        // one layer, locked once
        ts.e = e;
        ts.rp = ts.use = src;
        ts.layer = td.layer;
        ts.super = false;
        if (td.super) {
            assign(ts.tmp, *src);
            ts.tmp.layer = nullptr;
            ts.tmp.bitmap = td.layer->superbitmap;
            ts.use = &ts.tmp;
        }
    }
    gfx_rectangle sr = box(sx, sy, sx + w - 1, sy + h - 1);
    if (!ogfx_rp_takes_copy(ts.use) || !ogfx_rp_takes_copy(td.use) ||
        !ogfx_rp_ClipBlit(ts.use, sx, sy, td.use, dx, dy, w, h, minterm)) {
        // a simple refresh layer, a clip region, or a rectangle not wholly
        // in a layer: what can be read and written is copied, and a simple
        // refresh destination is damaged where its source was hidden
        gfx_rectangle sw = window_of(ts.use), dw = window_of(td.use);
        int32_t a = max32(sx, sw.minx), b = max32(sy, sw.miny);
        int32_t c = min32(sx + w - 1, sw.maxx), d = min32(sy + h - 1, sw.maxy);
        a += dx - sx; c += dx - sx; b += dy - sy; d += dy - sy;
        a = max32(a, dw.minx); b = max32(b, dw.miny); c = min32(c, dw.maxx); d = min32(d, dw.maxy);
        if (a <= c && b <= d) {
            copy_layer cs, cd;
            ogfx_rp_ClipBlit(as_copyable(cs, ts.use), a - dx + sx, b - dy + sy, as_copyable(cd, td.use), a, b,
                             c - a + 1, d - b + 1, minterm);
        }
        damage(e, ts.use, sr, td.use, r);
    }
    if (!same) end(ts, nullptr);
    end(td, &r);
}

int32_t gfx_BltBitMap(gfx_env *e, gfx_bitmap *src, int32_t sx, int32_t sy, gfx_bitmap *dst, int32_t dx, int32_t dy,
                      int32_t w, int32_t h, uint32_t minterm, uint32_t mask, uint8_t *temp)
{
    if (src->depth > 8 || dst->depth > 8 || (src->flags & GFX_BMF_SPECIALFMT) || (dst->flags & GFX_BMF_SPECIALFMT)) {
        int32_t rc = gfx_hook_blt_bitmap(e, src, sx, sy, dst, dx, dy, w, h, minterm, mask, temp);
        return rc < 0 ? 0 : rc;
    }
    gfx_hook_wait_blit(e);
    return ogfx_rp_BltBitMap(src, sx, sy, dst, dx, dy, w, h, minterm, mask, temp);
}

void gfx_BltBitMapRastPort(gfx_env *e, gfx_bitmap *src, int32_t sx, int32_t sy, gfx_rastport *rp, int32_t dx,
                           int32_t dy, int32_t w, int32_t h, uint32_t minterm)
{
    gfx_rectangle r = box(int16_t(dx), int16_t(dy), int16_t(dx) + int16_t(w) - 1, int16_t(dy) + int16_t(h) - 1);
    target t;
    begin(t, e, rp);
    ogfx_rp_BltBitMapRastPort(src, sx, sy, t.use, dx, dy, w, h, minterm);
    end(t, &r);
}

int32_t gfx_WritePixelArray8(gfx_env *e, gfx_rastport *rp, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                             uint8_t *array, gfx_rastport *temprp)
{
    gfx_rectangle r = box(int32_t(uint16_t(x0)), int32_t(uint16_t(y0)), int32_t(uint16_t(x1)), int32_t(uint16_t(y1)));
    target t;
    begin(t, e, rp);
    int32_t rc = ogfx_rp_WritePixelArray8(t.use, x0, y0, x1, y1, array, temprp);
    end(t, &r);
    return rc;
}

int32_t gfx_ReadPixelArray8(gfx_env *e, gfx_rastport *rp, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                            uint8_t *array, gfx_rastport *temprp)
{
    target t;
    begin(t, e, rp);
    int32_t rc = ogfx_rp_ReadPixelArray8(t.use, x0, y0, x1, y1, array, temprp);
    if (t.layer) e->sem_release(e->ctx, &t.layer->lock);
    return rc;
}

// One row of WritePixelArray8 and ReadPixelArray8 (autodoc).
int32_t gfx_WritePixelLine8(gfx_env *e, gfx_rastport *rp, uint32_t x, uint32_t y, uint32_t width, uint8_t *array,
                            gfx_rastport *temprp)
{
    width &= 0xFFFF;
    if (!width) return 0;
    return gfx_WritePixelArray8(e, rp, x, y, x + width - 1, y, array, temprp);
}

int32_t gfx_ReadPixelLine8(gfx_env *e, gfx_rastport *rp, uint32_t x, uint32_t y, uint32_t width, uint8_t *array,
                           gfx_rastport *temprp)
{
    width &= 0xFFFF;
    if (!width) return 0;
    return gfx_ReadPixelArray8(e, rp, x, y, x + width - 1, y, array, temprp);
}

void gfx_WriteChunkyPixels(gfx_env *e, gfx_rastport *rp, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                           uint8_t *array, int32_t bytesperrow)
{
    gfx_rectangle r = box(int32_t(uint16_t(x0)), int32_t(uint16_t(y0)), int32_t(uint16_t(x1)), int32_t(uint16_t(y1)));
    target t;
    begin(t, e, rp);
    ogfx_rp_WriteChunkyPixels(t.use, x0, y0, x1, y1, array, bytesperrow);
    end(t, &r);
}

// ---- init ------------------------------------------------------------------------------

int gfx_init_face(gfx_env *) { return 1; }
void gfx_exit_face(gfx_env *) {}

} // extern "C"
