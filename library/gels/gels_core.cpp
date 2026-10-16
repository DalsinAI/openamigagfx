// SPDX-License-Identifier: MIT
// OpenGfx gels (gels_core.h). Copyright (c) 2026 AmigaChrome Team. MIT.
//
// Lean C++: no exceptions, no RTTI, no static constructors, no libstdc++,
// nothing in globals. Everything reaches exec and the chips through
// struct gfx_env.
#include "gels_core.h"

namespace {

// ---- the custom chips (HRM) -------------------------------------------
enum : uint32_t {
    DMACONR = 0x002, VPOSR = 0x004, VHPOSR = 0x006, INTENA = 0x09A, INTREQ = 0x09C,
    BLTCON0 = 0x040, BLTCON1 = 0x042, BLTAFWM = 0x044, BLTALWM = 0x046,
    BLTCPTH = 0x048, BLTBPTH = 0x04C, BLTAPTH = 0x050, BLTDPTH = 0x054, BLTSIZE = 0x058,
    BLTCMOD = 0x060, BLTBMOD = 0x062, BLTAMOD = 0x064, BLTDMOD = 0x066,
    BLTCDAT = 0x070, BLTBDAT = 0x072, BLTADAT = 0x074
};
constexpr uint16_t DMAF_BLTDONE = 0x4000;   // DMACONR's BBUSY
constexpr uint16_t INTF_BLIT = 0x0040;
constexpr uint32_t SIGF_BLIT = 0x10;
constexpr int USEA = 0x800, USEB = 0x400, USEC = 0x200, USED = 0x100;

inline gfx_env *E(gels_state *st) { return st->env; }
inline gfx_base *B(gels_state *st) { return st->env->base; }

// ---- exec lists ---------------------------------------------------------
inline bool list_empty(gfx_list *l) { return l->tailpred == reinterpret_cast<gfx_node *>(l); }
void list_addtail(gfx_list *l, gfx_node *n)
{
    n->succ = reinterpret_cast<gfx_node *>(&l->tail);
    n->pred = l->tailpred;
    l->tailpred->succ = n;
    l->tailpred = n;
}
gfx_node *list_remhead(gfx_list *l)
{
    gfx_node *n = l->head;
    if (!n->succ) return nullptr;
    l->head = n->succ;
    n->succ->pred = reinterpret_cast<gfx_node *>(&l->head);
    return n;
}

// ---- the blitter's registers ---------------------------------------------
inline uint16_t rd(gels_state *st, uint32_t r) { return E(st)->chip_read(E(st)->ctx, r); }
inline void wr(gels_state *st, uint32_t r, uint16_t v) { E(st)->chip_write(E(st)->ctx, r, v); }
inline void wrptr(gels_state *st, uint32_t r, const void *p)
{
    uint32_t a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
    wr(st, r, static_cast<uint16_t>(a >> 16));
    wr(st, r + 2, static_cast<uint16_t>(a));
}
bool busy(gels_state *st)
{
    if (!st->has_blitter) return false;
    // the first read after a blit starts may not show BBUSY yet (HRM): read twice
    rd(st, DMACONR);
    return rd(st, DMACONR) & DMAF_BLTDONE;
}
void wait_blit(gels_state *st)
{
    if (st->sys_blitter) {
        st->sys_blitter(st, GELS_SYS_WAITBLIT, nullptr);
        return;
    }
    while (busy(st)) {
    }
}
int32_t beam(gels_state *st)
{
    uint16_t v = rd(st, VPOSR), h = rd(st, VHPOSR);
    return static_cast<int32_t>((v & 7u) << 8 | h >> 8);
}
bool chip(gels_state *st, const void *p)
{
    gfx_env *e = E(st);
    return st->has_blitter && e->type_of_mem && (e->type_of_mem(e->ctx, p) & GFX_MEMF_CHIP);
}

// ---- the queue ------------------------------------------------------------
// With interrupts disabled. GfxBase's BlitLock is 1 while the blitter is
// someone's: a task (BlitOwner) or the queue (BlitOwner 0). Runs queued
// routines and hands the blitter on until it has to wait for a blit to
// finish (the blitter's interrupt comes back here) or nothing is left.
gl_bltnode *q_cur(gels_state *st)
{
    return static_cast<gl_bltnode *>(st->q_bs ? B(st)->bsblthd : B(st)->blthd);
}
void q_step(gels_state *st)
{
    st->q_again = st->call_blit(st, q_cur(st)) != 0;
}
void service(gels_state *st)
{
    gfx_base *b = B(st);
    for (;;) {
        if (b->blitlock) {
            if (b->blitowner || busy(st)) return;
            if (st->q_again) {
                q_step(st);
                continue;
            }
            gl_bltnode *n = q_cur(st);
            if (st->q_bs) {
                b->bsblthd = n->n;
                if (!n->n) b->bsblttl = nullptr;
            } else {
                b->blthd = n->n;
                if (!n->n) b->blttl = nullptr;
            }
            b->blitlock = 0;
            if (n->stat & GL_CLEANUP) st->call_cleanup(st, n);
            continue;
        }
        gl_bltnode *bs = static_cast<gl_bltnode *>(b->bsblthd);
        if (b->blthd || (bs && beam(st) >= bs->beamsync)) {
            if (busy(st)) return;
            b->blitlock = 1;
            b->blitowner = nullptr;
            st->q_bs = b->blthd ? 0 : 1;
            q_step(st);
            continue;
        }
        if (!list_empty(&b->blitwaitq)) {
            struct waiter { gfx_node node; void *task; };
            waiter *w = reinterpret_cast<waiter *>(list_remhead(&b->blitwaitq));
            b->blitlock = 1;
            b->blitowner = w->task;
            b->blitnest = 0;
            E(st)->signal(E(st)->ctx, w->task, SIGF_BLIT);
        }
        return;
    }
}

void own(gels_state *st) { gels_OwnBlitter(st); }
void disown(gels_state *st) { gels_DisownBlitter(st); }

// ---- one plane: D = minterm(A, B, C) in a rectangle -------------------------
// A source is a bitmap row (row: the rectangle's top row, x: the pixel
// under the rectangle's left) or, konst, the word val everywhere.
struct src {
    const uint8_t *row;
    int32_t bpr;
    int32_t x;
    int konst;
    uint16_t val;
};
src konst(uint16_t v) { return src{nullptr, 0, 0, 1, v}; }
src mem(const void *row, int32_t bpr, int32_t x) { return src{static_cast<const uint8_t *>(row), bpr, x, 0, 0}; }

// 16 pixels from bit position sp of a row, those outside lo..hi as 0
inline uint32_t fetch16(const uint8_t *row, int32_t sp, int32_t lo, int32_t hi)
{
    int32_t b0 = sp >> 3, blo = lo >> 3, bhi = hi >> 3;
    uint32_t v = 0;
    for (int k = 0; k < 3; k++) {
        int32_t bi = b0 + k;
        v = v << 8 | (bi >= blo && bi <= bhi ? row[bi] : 0u);
    }
    return v >> (8 - (sp & 7)) & 0xFFFF;
}
inline uint32_t mix(uint32_t mt, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t r = 0;
    if (mt & 0x80) r |= a & b & c;
    if (mt & 0x40) r |= a & b & ~c;
    if (mt & 0x20) r |= a & ~b & c;
    if (mt & 0x10) r |= a & ~b & ~c;
    if (mt & 0x08) r |= ~a & b & c;
    if (mt & 0x04) r |= ~a & b & ~c;
    if (mt & 0x02) r |= ~a & ~b & c;
    if (mt & 0x01) r |= ~a & ~b & ~c;
    return r & 0xFFFF;
}

void op_cpu(uint8_t *drow, int32_t dbpr, int32_t dx, int32_t w, int32_t h, const src &a, const src &b, uint32_t mt)
{
    int32_t j0 = dx >> 4, j1 = (dx + w - 1) >> 4, last = dx + w - 1;
    for (int32_t r = 0; r < h; r++) {
        const uint8_t *ar = a.konst ? nullptr : a.row + r * a.bpr;
        const uint8_t *br = b.konst ? nullptr : b.row + r * b.bpr;
        uint8_t *dr = drow + r * dbpr;
        for (int32_t j = j0; j <= j1; j++) {
            int32_t p0 = j * 16;
            uint32_t m = 0xFFFF;
            if (p0 < dx) m &= 0xFFFFu >> (dx - p0);
            if (p0 + 15 > last) m &= 0xFFFFu << (p0 + 15 - last) & 0xFFFF;
            uint32_t av = ar ? fetch16(ar, a.x + p0 - dx, a.x, a.x + w - 1) : a.val;
            uint32_t bv = br ? fetch16(br, b.x + p0 - dx, b.x, b.x + w - 1) : b.val;
            uint8_t *d = dr + 2 * j;
            uint32_t c = static_cast<uint32_t>(d[0]) << 8 | d[1];
            uint32_t v = (c & ~m) | (mix(mt, av, bv, c) & m);
            d[0] = static_cast<uint8_t>(v >> 8);
            d[1] = static_cast<uint8_t>(v);
        }
    }
}

// The same with the blitter, when it can do it exactly: chip memory; A and
// B (where not constant) at one alignment that the destination's is the
// same or to the right of, shifted right in ascending mode; D = C where A
// is 0 (outside the rectangle, FWM and LWM make A 0). Answers false to
// leave it to the CPU. The caller owns the blitter.
bool op_hw(gels_state *st, uint8_t *drow, int32_t dbpr, int32_t dx, int32_t w, int32_t h, const src &a,
           const src &b, uint32_t mt)
{
    if (!st->has_blitter || (mt & 0x0F) != 0x0A || !chip(st, drow)) return false;
    if (!a.konst && !chip(st, a.row)) return false;
    if (!b.konst && !chip(st, b.row)) return false;
    if ((!a.konst && a.x < 0) || (!b.konst && b.x < 0)) return false;
    if (!a.konst && !b.konst && ((a.x ^ b.x) & 15)) return false;
    int32_t sx = !a.konst ? a.x : !b.konst ? b.x : dx;
    int32_t s15 = sx & 15, d15 = dx & 15, delta = d15 - s15;
    if (delta < 0) return false;
    int32_t ns = (s15 + w + 15) >> 4, nd = (d15 + w + 15) >> 4;
    if (nd != ns && ((s15 + w) & 15)) return false;
    if (nd > 64) return false;
    uint16_t fwm = static_cast<uint16_t>(0xFFFFu >> s15);
    uint16_t lwm = nd == ns ? static_cast<uint16_t>(0xFFFFu << (15 - ((s15 + w - 1) & 15))) : 0;
    uint16_t con0 = static_cast<uint16_t>(delta << 12 | (a.konst ? 0 : USEA) | (b.konst ? 0 : USEB) | USEC | USED | mt);
    for (int32_t r = 0; r < h; r += 1024) {
        int32_t n = h - r < 1024 ? h - r : 1024;
        wait_blit(st);
        wr(st, BLTCON0, con0);
        wr(st, BLTCON1, static_cast<uint16_t>(delta << 12));
        wr(st, BLTAFWM, fwm);
        wr(st, BLTALWM, lwm);
        if (a.konst) wr(st, BLTADAT, a.val);
        else {
            wrptr(st, BLTAPTH, a.row + r * a.bpr + (a.x >> 4) * 2);
            wr(st, BLTAMOD, static_cast<uint16_t>(a.bpr - 2 * nd));
        }
        if (b.konst) wr(st, BLTBDAT, b.val);
        else {
            wrptr(st, BLTBPTH, b.row + r * b.bpr + (b.x >> 4) * 2);
            wr(st, BLTBMOD, static_cast<uint16_t>(b.bpr - 2 * nd));
        }
        uint8_t *d = drow + r * dbpr + (dx >> 4) * 2;
        wrptr(st, BLTCPTH, d);
        wrptr(st, BLTDPTH, d);
        wr(st, BLTCMOD, static_cast<uint16_t>(dbpr - 2 * nd));
        wr(st, BLTDMOD, static_cast<uint16_t>(dbpr - 2 * nd));
        wr(st, BLTSIZE, static_cast<uint16_t>((n & 1023) << 6 | (nd & 63)));
    }
    return true;
}

// One plane's rectangle, by the blitter where it can, else the CPU (once
// the blitter has finished with the memory).
void op(gels_state *st, uint8_t *drow, int32_t dbpr, int32_t dx, int32_t w, int32_t h, const src &a, const src &b,
        uint32_t mt)
{
    if (w <= 0 || h <= 0) return;
    if (op_hw(st, drow, dbpr, dx, w, h, a, b, mt)) return;
    wait_blit(st);
    op_cpu(drow, dbpr, dx, w, h, a, b, mt);
}

// A minterm with D = C where A is 0, and D = f(B, C) where A is 1.
template <typename F> uint32_t mt_where_a(F f)
{
    uint32_t mt = 0x0A;
    for (int bb = 0; bb < 2; bb++)
        for (int cc = 0; cc < 2; cc++)
            if (f(bb, cc)) mt |= 1u << (4 + 2 * bb + cc);
    return mt;
}

// ---- where a RastPort's drawing lands --------------------------------------
inline bool plane_ok(const uint8_t *p) { return p && p != reinterpret_cast<const uint8_t *>(-1); }
// Each plane's part of a row: BytesPerRow, or an interleaved bitmap's
// share of it, where its first two real planes (a constant plane, 0 or
// -1, is not one) are that far apart for each plane between them, or
// where it has fewer than two real planes.
uint32_t plane_bytes(const gfx_bitmap *bm)
{
    uint32_t bpr = bm->bytesperrow;
    if (!(bm->flags & GFX_BMF_INTERLEAVED) || bm->depth < 2) return bpr;
    uint32_t part = bpr / bm->depth;
    int first = -1, n = bm->depth > 8 ? 8 : bm->depth;
    for (int p = 0; p < n; p++) {
        if (!plane_ok(bm->planes[p])) continue;
        if (first < 0) first = p;
        else return bm->planes[p] == bm->planes[first] + static_cast<uint32_t>(p - first) * part ? part : bpr;
    }
    return part;
}
int32_t bm_width(const gfx_bitmap *bm) { return static_cast<int32_t>(plane_bytes(bm) * 8); }
int bm_depth(const gfx_bitmap *bm) { return bm->depth > 8 ? 8 : bm->depth; }

// A place: a bitmap, the offset from the RastPort's coordinates to its
// own, and the rectangle of it a drawing may touch. An unlayered RastPort
// has its bitmap. A layer has its ClipRects: the screen where it is on
// top, a smart refresh or super bitmap layer's own bitmap where it is
// covered (x 0 there is the screen's at MinX & ~15), nothing for a simple
// refresh layer's covered parts; and a super bitmap layer also has its
// super bitmap where the layer's window does not reach.
struct place {
    gfx_bitmap *bm;
    int32_t ox, oy;
    int32_t x0, y0, x1, y1;
};
struct walk {
    gfx_rastport *rp;
    gfx_cliprect *cr;
    int stage;
};
void walk_start(walk &w, gfx_rastport *rp)
{
    w.rp = rp;
    w.cr = rp->layer ? static_cast<gfx_layer *>(rp->layer)->cliprect : nullptr;
    w.stage = 0;
}
bool clamp(place &p)
{
    int32_t bw = bm_width(p.bm) - 1, bh = static_cast<int32_t>(p.bm->rows) - 1;
    if (p.x0 < 0) p.x0 = 0;
    if (p.y0 < 0) p.y0 = 0;
    if (p.x1 > bw) p.x1 = bw;
    if (p.y1 > bh) p.y1 = bh;
    return p.x0 <= p.x1 && p.y0 <= p.y1;
}
bool walk_next(walk &w, place &p)
{
    gfx_layer *l = static_cast<gfx_layer *>(w.rp->layer);
    if (!l) {
        if (w.stage++) return false;
        p.bm = w.rp->bitmap;
        p.ox = p.oy = 0;
        p.x0 = p.y0 = 0;
        p.x1 = bm_width(p.bm) - 1;
        p.y1 = static_cast<int32_t>(p.bm->rows) - 1;
        return true;
    }
    int32_t ox = l->bounds.minx - l->scroll_x, oy = l->bounds.miny - l->scroll_y;
    while (w.cr) {
        gfx_cliprect *cr = w.cr;
        w.cr = cr->next;
        int32_t bx, by;
        if (!cr->lobs) {
            p.bm = w.rp->bitmap;
            bx = by = 0;
        } else if (cr->bitmap && (l->flags & (GFX_LAYERSMART | GFX_LAYERSUPER))) {
            p.bm = cr->bitmap;
            bx = cr->bounds.minx & ~15;
            by = cr->bounds.miny;
        } else
            continue;
        p.ox = ox - bx;
        p.oy = oy - by;
        p.x0 = cr->bounds.minx - bx;
        p.y0 = cr->bounds.miny - by;
        p.x1 = cr->bounds.maxx - bx;
        p.y1 = cr->bounds.maxy - by;
        if (clamp(p)) return true;
    }
    // a super bitmap layer: its whole super bitmap, in the RastPort's
    // coordinates, the window's part too, so it stays what the screen
    // shows (the face draws on it and copies it out the same way)
    if (!(l->flags & GFX_LAYERSUPER) || !l->superbitmap || w.stage++) return false;
    p.bm = l->superbitmap;
    p.ox = p.oy = 0;
    p.x0 = p.y0 = 0;
    p.x1 = p.y1 = 0x7FFF;
    return clamp(p);
}

void lock_rp(gels_state *st, gfx_rastport *rp)
{
    if (rp->layer) E(st)->sem_obtain(E(st)->ctx, &static_cast<gfx_layer *>(rp->layer)->lock);
}
void unlock_rp(gels_state *st, gfx_rastport *rp)
{
    if (rp->layer) E(st)->sem_release(E(st)->ctx, &static_cast<gfx_layer *>(rp->layer)->lock);
}

// The rectangle x, y, w x h of the RastPort, cut into places: fn(place,
// plane number, the plane's row at the piece's top, bytes a row, the
// piece's x in the bitmap, its size, and its offset in the rectangle).
template <typename F> void each_piece(gfx_rastport *rp, int32_t x, int32_t y, int32_t w, int32_t h, F fn)
{
    if (w <= 0 || h <= 0) return;
    walk wk;
    place p;
    walk_start(wk, rp);
    while (walk_next(wk, p)) {
        int32_t x0 = x + p.ox, y0 = y + p.oy, x1 = x0 + w - 1, y1 = y0 + h - 1;
        if (x0 < p.x0) x0 = p.x0;
        if (y0 < p.y0) y0 = p.y0;
        if (x1 > p.x1) x1 = p.x1;
        if (y1 > p.y1) y1 = p.y1;
        if (x0 > x1 || y0 > y1) continue;
        int depth = bm_depth(p.bm);
        for (int pl = 0; pl < depth; pl++)
            fn(p.bm, pl, x0, y0, x1 - x0 + 1, y1 - y0 + 1, x0 - (x + p.ox), y0 - (y + p.oy));
    }
}
inline uint8_t *row_of(gfx_bitmap *bm, int pl, int32_t y) { return bm->planes[pl] + y * bm->bytesperrow; }

// ---- the GELs ----------------------------------------------------------------
inline bool sorts_before(const gl_vsprite *a, const gl_vsprite *b)
{
    return a->y < b->y || (a->y == b->y && a->x < b->x);
}
void unlink(gl_vsprite *vs)
{
    if (vs->prev) vs->prev->next = vs->next;
    if (vs->next) vs->next->prev = vs->prev;
}
gl_gelsinfo *gels_of(gfx_rastport *rp) { return static_cast<gl_gelsinfo *>(rp->gelsinfo); }
// Into the list after every gel at its y, x or before it.
void insert_sorted(gl_gelsinfo *gi, gl_vsprite *vs)
{
    gl_vsprite *at = gi->gelhead->next;
    while (at != gi->geltail && !sorts_before(vs, at)) at = at->next;
    vs->next = at;
    vs->prev = at->prev;
    at->prev->next = vs;
    at->prev = vs;
}
inline bool is_bob(const gl_vsprite *vs) { return !(vs->flags & GL_VSPRITE) && vs->vsbob; }

// A Bob's background: its rectangle's pixels, every plane of the
// bitmap, in buf at the Bob's own alignment (width words a row, height
// rows a plane), as many planes as the bitmap has.
void save_back(gels_state *st, gfx_rastport *rp, gl_vsprite *vs, int16_t *buf, int32_t x, int32_t y)
{
    int32_t ww = vs->width, hh = vs->height, bpr = ww * 2;
    uint8_t *base = reinterpret_cast<uint8_t *>(buf);
    each_piece(rp, x, y, ww * 16, hh, [&](gfx_bitmap *bm, int pl, int32_t bx, int32_t by, int32_t pw, int32_t ph,
                                          int32_t rx, int32_t ry) {
        uint8_t *d = base + (pl * hh + ry) * bpr;
        uint8_t *s = bm->planes[pl];
        src bsrc = !s ? konst(0) : s == reinterpret_cast<uint8_t *>(-1) ? konst(0xFFFF)
                                                                         : mem(row_of(bm, pl, by), bm->bytesperrow, bx);
        op(st, d, bpr, rx, pw, ph, konst(0xFFFF), bsrc, 0xCA);
    });
}
void restore_back(gels_state *st, gfx_rastport *rp, gl_vsprite *vs, const int16_t *buf, int32_t x, int32_t y)
{
    int32_t ww = vs->width, hh = vs->height, bpr = ww * 2;
    const uint8_t *base = reinterpret_cast<const uint8_t *>(buf);
    each_piece(rp, x, y, ww * 16, hh, [&](gfx_bitmap *bm, int pl, int32_t bx, int32_t by, int32_t pw, int32_t ph,
                                          int32_t rx, int32_t ry) {
        if (!plane_ok(bm->planes[pl])) return;
        op(st, row_of(bm, pl, by), bm->bytesperrow, bx, pw, ph, konst(0xFFFF),
           mem(base + (pl * hh + ry) * bpr, bpr, rx), 0xCA);
    });
}

// A Bob's image: a plane in PlanePick takes the next plane of ImageData,
// the others PlaneOnOff's bit, through ImageShadow under OVERLAY and over
// the whole rectangle without.
void draw_image(gels_state *st, gfx_rastport *rp, gl_vsprite *vs, gl_bob *bob)
{
    int32_t ww = vs->width, hh = vs->height, bpr = ww * 2;
    const uint8_t *img = reinterpret_cast<const uint8_t *>(vs->imagedata);
    const uint8_t *shadow = (vs->flags & GL_OVERLAY) ? reinterpret_cast<const uint8_t *>(bob->imageshadow) : nullptr;
    uint32_t pick = static_cast<uint8_t>(vs->planepick), onoff = static_cast<uint8_t>(vs->planeonoff);
    each_piece(rp, vs->x, vs->y, ww * 16, hh, [&](gfx_bitmap *bm, int pl, int32_t bx, int32_t by, int32_t pw,
                                                 int32_t ph, int32_t rx, int32_t ry) {
        if (!plane_ok(bm->planes[pl])) return;
        src a = shadow ? mem(shadow + ry * bpr, bpr, rx) : konst(0xFFFF);
        uint8_t *d = row_of(bm, pl, by);
        if (pick >> pl & 1) {
            // which plane of the image: the picked planes below this one
            int k = 0;
            for (int q = 0; q < pl; q++) k += pick >> q & 1;
            if (k < vs->depth && img) {
                op(st, d, bm->bytesperrow, bx, pw, ph, a, mem(img + (k * hh + ry) * bpr, bpr, rx), 0xCA);
                return;
            }
        }
        op(st, d, bm->bytesperrow, bx, pw, ph, a, konst(0), (onoff >> pl & 1) ? 0xFA : 0x0A);
    });
}

// Draws a Bob (saving its background first, under SAVEBACK) and puts it
// at the front of the chain of Bobs to erase, which runs from the last
// drawn: GelsInfo's gelHead->ClearPath, then each VSprite's ClearPath.
void draw_bob(gels_state *st, gfx_rastport *rp, gl_gelsinfo *gi, gl_bob *bob)
{
    gl_vsprite *vs = bob->bobvsprite;
    if ((vs->flags & GL_SAVEBACK) && bob->savebuffer) {
        save_back(st, rp, vs, bob->savebuffer, vs->x, vs->y);
        vs->oldx = vs->x;
        vs->oldy = vs->y;
        vs->flags |= GL_BACKSAVED;
        vs->clearpath = gi->gelhead->clearpath;
        gi->gelhead->clearpath = vs;
    }
    draw_image(st, rp, vs, bob);
    bob->flags = static_cast<int16_t>((bob->flags & ~GL_BWAITING) | GL_BDRAWN);
}

// Draws bob after the Bobs its After chain says come first.
void draw_ordered(gels_state *st, gfx_rastport *rp, gl_gelsinfo *gi, gl_bob *bob)
{
    while (!(bob->flags & GL_BDRAWN)) {
        gl_bob *x = bob;
        x->flags |= GL_BWAITING;
        while (x->after && !(x->after->flags & (GL_BDRAWN | GL_BWAITING | GL_BOBSAWAY | GL_BOBNIX))) {
            x = x->after;
            x->flags |= GL_BWAITING;
        }
        draw_bob(st, rp, gi, x);
        for (gl_bob *y = bob; y && (y->flags & GL_BWAITING); y = y->after) y->flags &= ~GL_BWAITING;
    }
}

// Swaps a double buffered Bob's two saves: (SaveBuffer, OldX, OldY) and
// (DBuffer's BufBuffer, BufX, BufY).
void swap_saves(gl_vsprite *vs, gl_bob *bob)
{
    gl_dbufpacket *db = bob->dbuffer;
    int16_t *t = bob->savebuffer;
    bob->savebuffer = db->bufbuffer;
    db->bufbuffer = t;
    int16_t x = vs->oldx, y = vs->oldy;
    vs->oldx = db->bufx;
    vs->oldy = db->bufy;
    db->bufx = x;
    db->bufy = y;
}

// Takes vs out of the erase chains.
void unchain(gl_gelsinfo *gi, gl_vsprite *vs)
{
    if (vs->flags & GL_BACKSAVED) {
        for (gl_vsprite **pp = &gi->gelhead->clearpath; *pp; pp = &(*pp)->clearpath)
            if (*pp == vs) {
                *pp = vs->clearpath;
                break;
            }
        vs->flags &= ~GL_BACKSAVED;
        vs->clearpath = nullptr;
    }
    gl_bob *bob = vs->vsbob;
    if (bob && bob->dbuffer && bob->dbuffer->bufpath) {
        gl_vsprite *end = gi->geltail;
        for (gl_vsprite **pp = &gi->geltail->clearpath; *pp && *pp != end; pp = &(*pp)->vsbob->dbuffer->bufpath)
            if (*pp == vs) {
                *pp = bob->dbuffer->bufpath;
                break;
            }
        bob->dbuffer->bufpath = nullptr;
    }
}

// The head of the gel list vs is in (its PrevVSprite chain's start).
gl_vsprite *head_of(gl_vsprite *vs)
{
    while (vs->prev) vs = vs->prev;
    return vs;
}

// VSprites onto the hardware sprites, in the list's order: the lowest
// sprite not in sprRsrvd that is free by the VSprite's line (nextLine)
// and whose pair shows the same colours or none at that line.
void place_vsprites(gels_state *st, gl_gelsinfo *gi, void *vp)
{
    uint32_t n = 0;
    for (gl_vsprite *v = gi->gelhead->next; v && v != gi->geltail; v = v->next)
        if (v->flags & GL_VSPRITE) n++;
    if (!gi->nextline || !gi->lastcolor) return;
    gfx_env *e = E(st);
    if (!n) {
        // none left: an empty set, so display drops the sprites it showed
        gfx_hook_vsprites(e, vp, nullptr, 0);
        return;
    }
    uint32_t size = n * static_cast<uint32_t>(sizeof(gels_vsprite_slot));
    gels_vsprite_slot *slots = static_cast<gels_vsprite_slot *>(e->alloc(e->ctx, size, GFX_MEMF_ANY));
    for (int s = 0; s < 8; s++) {
        gi->nextline[s] = -32768;
        gi->lastcolor[s] = nullptr;
    }
    uint32_t k = 0;
    for (gl_vsprite *v = gi->gelhead->next; v && v != gi->geltail; v = v->next) {
        if (!(v->flags & GL_VSPRITE)) continue;
        v->flags &= ~(GL_VSOVERFLOW | GL_GELGONE);
        if (v->height <= 0) {
            v->flags |= GL_GELGONE;
            continue;
        }
        int found = -1;
        for (int s = 0; s < 8 && found < 0; s++) {
            if (gi->sprrsrvd >> s & 1) continue;
            if (gi->nextline[s] > v->y) continue;
            int mate = s ^ 1;
            if (v->sprcolors && gi->nextline[mate] > v->y && gi->lastcolor[mate] && gi->lastcolor[mate] != v->sprcolors)
                continue;
            found = s;
        }
        if (found < 0) {
            v->flags |= GL_VSOVERFLOW;
            continue;
        }
        uint16_t fl = 0;
        if (v->sprcolors && gi->lastcolor[found] != v->sprcolors) fl = GELS_SLOT_COLORS;
        gi->nextline[found] = static_cast<int16_t>(v->y + v->height + 1);
        if (v->sprcolors) gi->lastcolor[found] = gi->lastcolor[found ^ 1] = v->sprcolors;
        if (slots) slots[k++] = gels_vsprite_slot{v, static_cast<int16_t>(found), v->y, fl};
    }
    if (slots) {
        gfx_hook_vsprites(e, vp, slots, k);
        e->free(e->ctx, slots, size);
    }
}

// Whether two gels' pixels meet: their rectangles, then their CollMasks
// where both have one.
bool gels_meet(const gl_vsprite *a, const gl_vsprite *b)
{
    int32_t ax1 = a->x + a->width * 16 - 1, bx1 = b->x + b->width * 16 - 1;
    int32_t ay1 = a->y + a->height - 1, by1 = b->y + b->height - 1;
    int32_t x0 = a->x > b->x ? a->x : b->x, x1 = ax1 < bx1 ? ax1 : bx1;
    int32_t y0 = a->y > b->y ? a->y : b->y, y1 = ay1 < by1 ? ay1 : by1;
    if (x0 > x1 || y0 > y1) return false;
    if (!a->collmask || !b->collmask) return true;
    const uint8_t *am = reinterpret_cast<const uint8_t *>(a->collmask);
    const uint8_t *bm = reinterpret_cast<const uint8_t *>(b->collmask);
    for (int32_t y = y0; y <= y1; y++)
        for (int32_t x = x0; x <= x1; x++) {
            int32_t ap = x - a->x, bp = x - b->x;
            if ((am[(y - a->y) * a->width * 2 + (ap >> 3)] >> (7 - (ap & 7)) & 1) &&
                (bm[(y - b->y) * b->width * 2 + (bp >> 3)] >> (7 - (bp & 7)) & 1))
                return true;
        }
    return false;
}

// The leftmost and rightmost pixel BorderLine has (the rectangle's
// without one); false when it has none.
bool border_span(const gl_vsprite *v, int32_t &l, int32_t &r)
{
    int32_t n = v->width * 16;
    if (!v->borderline) {
        l = 0;
        r = n - 1;
        return n > 0;
    }
    const uint8_t *b = reinterpret_cast<const uint8_t *>(v->borderline);
    l = -1;
    for (int32_t p = 0; p < n; p++)
        if (b[p >> 3] >> (7 - (p & 7)) & 1) {
            if (l < 0) l = p;
            r = p;
        }
    return l >= 0;
}

int16_t ring_x(gl_animob *ob) { return static_cast<int16_t>(ob->anx >> GL_ANFRACSIZE); }
int16_t ring_y(gl_animob *ob) { return static_cast<int16_t>(ob->any >> GL_ANFRACSIZE); }

// Each component's sequence ring, once round: fn(component).
template <typename F> void each_seq(gl_animob *ob, F fn)
{
    for (gl_animcomp *c = ob->headcomp; c; c = c->nextcomp) {
        gl_animcomp *s = c;
        do {
            fn(s);
            s = s->nextseq;
        } while (s && s != c);
    }
}

} // namespace

extern "C" {

// ---- init and the hooks the library calls -----------------------------------

void gels_init(gels_state *st)
{
    gfx_base *b = B(st);
    b->blthd = b->blttl = b->bsblthd = b->bsblttl = nullptr;
    b->blitlock = 0;
    b->blitnest = 0;
    b->blitowner = nullptr;
    st->q_again = st->q_bs = 0;
}

void gfx_hook_wait_blit(gfx_env *env)
{
    gels_state *st = static_cast<gels_state *>(gfx_private_of(env->base)->area[GFX_AREA_GELS]);
    if (st) wait_blit(st);
}

int32_t gfx_hook_blt_bitmap(gfx_env *, gfx_bitmap *, int32_t, int32_t, gfx_bitmap *, int32_t, int32_t, int32_t,
                            int32_t, uint32_t, uint32_t, uint8_t *)
{
    return -1;
}

// ---- the blitter -----------------------------------------------------------

void gels_OwnBlitter(gels_state *st)
{
    gfx_env *e = E(st);
    gfx_base *b = B(st);
    if (st->sys_blitter) {
        st->sys_blitter(st, GELS_SYS_OWNBLITTER, nullptr);
        return;
    }
    e->disable(e->ctx);
    void *me = e->find_task(e->ctx);
    if (b->blitlock && b->blitowner == me) {
        b->blitnest++;      // ABI test: OS 3.2 with OwnBlitter twice
    } else if (!b->blitlock && !b->blthd && list_empty(&b->blitwaitq)) {
        b->blitlock = 1;
        b->blitowner = me;
    } else {
        struct {
            gfx_node node;
            void *task;
        } w;
        w.node.type = 0;
        w.node.pri = 0;
        w.node.name = nullptr;
        w.task = me;
        list_addtail(&b->blitwaitq, &w.node);
        do {
            e->enable(e->ctx);
            e->wait(e->ctx, SIGF_BLIT);
            e->disable(e->ctx);
        } while (!b->blitlock || b->blitowner != me);
    }
    e->enable(e->ctx);
}

void gels_DisownBlitter(gels_state *st)
{
    gfx_env *e = E(st);
    gfx_base *b = B(st);
    if (st->sys_blitter) {
        st->sys_blitter(st, GELS_SYS_DISOWNBLITTER, nullptr);
        return;
    }
    e->disable(e->ctx);
    if (b->blitnest > 0) b->blitnest--;
    else {
        b->blitlock = 0;
        b->blitowner = nullptr;
        service(st);
    }
    e->enable(e->ctx);
}

void gels_WaitBlit(gels_state *st) { wait_blit(st); }

void gels_QBlit(gels_state *st, gl_bltnode *bn)
{
    gfx_env *e = E(st);
    gfx_base *b = B(st);
    if (st->sys_blitter) {
        st->sys_blitter(st, GELS_SYS_QBLIT, bn);
        return;
    }
    e->disable(e->ctx);
    bn->n = nullptr;
    if (b->blttl) static_cast<gl_bltnode *>(b->blttl)->n = bn;
    else b->blthd = bn;
    b->blttl = bn;
    service(st);
    e->enable(e->ctx);
}

void gels_QBSBlit(gels_state *st, gl_bltnode *bn)
{
    gfx_env *e = E(st);
    gfx_base *b = B(st);
    if (st->sys_blitter) {
        st->sys_blitter(st, GELS_SYS_QBSBLIT, bn);
        return;
    }
    e->disable(e->ctx);
    bn->n = nullptr;
    if (b->bsblttl) static_cast<gl_bltnode *>(b->bsblttl)->n = bn;
    else b->bsblthd = bn;
    b->bsblttl = bn;
    service(st);
    e->enable(e->ctx);
}

void gels_blit_int(gels_state *st)
{
    gfx_env *e = E(st);
    wr(st, INTREQ, INTF_BLIT);
    e->disable(e->ctx);
    service(st);
    e->enable(e->ctx);
}

// The vertical blank: a QBSBlit node waiting with the blitter free waits
// here for the beam to reach its line, then runs (ABI test: OS 3.2 may
// wait elsewhere). Gives up when the beam starts a new frame.
void gels_vertb(gels_state *st)
{
    gfx_env *e = E(st);
    gfx_base *b = B(st);
    e->disable(e->ctx);
    gl_bltnode *bs = static_cast<gl_bltnode *>(b->bsblthd);
    if (bs && !b->blitlock && !b->blthd) {
        int32_t last = -1, y;
        while ((y = beam(st)) < bs->beamsync && y >= last) last = y;
    }
    service(st);
    e->enable(e->ctx);
}

void gels_BltClear(gels_state *st, void *mem, uint32_t count, uint32_t flags)
{
    uint32_t words = (flags & 2) ? (count >> 16) * ((count & 0xFFFF) >> 1) : count >> 1;
    uint16_t fill = (flags & 4) ? 0xFFFF : 0;
    uint8_t *p = static_cast<uint8_t *>(mem);
    if (!words || !p) return;
    if (st->has_blitter && chip(st, p)) {
        own(st);
        while (words) {
            uint32_t rows, w;
            if (words >= 64) {
                rows = words / 64;
                if (rows > 1024) rows = 1024;
                w = 64;
            } else {
                rows = 1;
                w = words;
            }
            wait_blit(st);
            wr(st, BLTCON0, static_cast<uint16_t>(USED | (fill ? 0xFF : 0x00)));
            wr(st, BLTCON1, 0);
            wrptr(st, BLTDPTH, p);
            wr(st, BLTDMOD, 0);
            wr(st, BLTSIZE, static_cast<uint16_t>((rows & 1023) << 6 | (w & 63)));
            p += rows * w * 2;
            words -= rows * w;
        }
        if (flags & 1) wait_blit(st);
        disown(st);
        return;
    }
    wait_blit(st);
    for (uint32_t i = 0; i < words * 2; i++) p[i] = static_cast<uint8_t>(fill);
}

// What a template's 1 and 0 do in a DrawMode (as gfx_rp.c measures for
// OS 3.2): INVERSVID swaps them; COMPLEMENT inverts for a 1 (and for
// both, with JAM2); else a 1 draws FgPen and a 0 BgPen under JAM2.
void gels_BltTemplate(gels_state *st, const void *source, int32_t srcx, int32_t srcmod, gfx_rastport *rp, int32_t dx,
                      int32_t dy, int32_t w, int32_t h)
{
    dx = static_cast<int16_t>(dx);
    dy = static_cast<int16_t>(dy);
    w = static_cast<int16_t>(w);
    h = static_cast<int16_t>(h);
    srcx = static_cast<int16_t>(srcx);
    srcmod = static_cast<int16_t>(srcmod);
    if (w <= 0 || h <= 0 || !source) return;
    const uint8_t *tpl = static_cast<const uint8_t *>(source);
    int mode = rp->drawmode;
    uint32_t fg = static_cast<uint8_t>(rp->fgpen), bg = static_cast<uint8_t>(rp->bgpen), wmask = rp->mask;
    lock_rp(st, rp);
    own(st);
    each_piece(rp, dx, dy, w, h, [&](gfx_bitmap *bm, int pl, int32_t bx, int32_t by, int32_t pw, int32_t ph,
                                     int32_t rx, int32_t ry) {
        if (!(wmask >> pl & 1) || !plane_ok(bm->planes[pl])) return;
        int fb = fg >> pl & 1, gb = bg >> pl & 1;
        uint32_t mt = mt_where_a([&](int b, int c) {
            int one = (mode & GFX_INVERSVID) ? !b : b;
            if (mode & GFX_COMPLEMENT) return (one || (mode & GFX_JAM2_MODE)) ? !c : c;
            if (one) return fb;
            return (mode & GFX_JAM2_MODE) ? gb : c;
        });
        op(st, row_of(bm, pl, by), bm->bytesperrow, bx, pw, ph, konst(0xFFFF), mem(tpl + ry * srcmod, srcmod, srcx + rx),
           mt);
    });
    disown(st);
    unlock_rp(st, rp);
}

// Through the mask (A): the autodoc's two minterms, (ABC|ABNC|ANBC) to
// copy the source and ANBC to invert it, as BltBitMap's minterm put
// between two XNORs of the source: D = ~(F(mask, ~(dest ^ src)) ^ src),
// F the minterm's high four bits. Both give D = dest outside the mask.
// ABI test: OS 3.2 with other minterms.
void gels_BltMaskBitMapRastPort(gels_state *st, gfx_bitmap *srcbm, int32_t sx, int32_t sy, gfx_rastport *rp, int32_t dx,
                                int32_t dy, int32_t w, int32_t h, uint32_t minterm, const void *bltmask)
{
    sx = static_cast<int16_t>(sx);
    sy = static_cast<int16_t>(sy);
    dx = static_cast<int16_t>(dx);
    dy = static_cast<int16_t>(dy);
    w = static_cast<int16_t>(w);
    h = static_cast<int16_t>(h);
    // the rectangle cut to the source
    int32_t sw = bm_width(srcbm), sh = srcbm->rows;
    if (sx < 0) { dx -= sx; w += sx; sx = 0; }
    if (sy < 0) { dy -= sy; h += sy; sy = 0; }
    if (sx + w > sw) w = sw - sx;
    if (sy + h > sh) h = sh - sy;
    if (w <= 0 || h <= 0 || !bltmask) return;
    uint32_t mt = 0;
    for (int a = 0; a < 2; a++)
        for (int b = 0; b < 2; b++)
            for (int c = 0; c < 2; c++) {
                int x = !(c ^ b), f = minterm >> (4 + 2 * a + x) & 1;
                if (!(f ^ b)) mt |= 1u << (4 * a + 2 * b + c);
            }
    const uint8_t *mk = static_cast<const uint8_t *>(bltmask);
    int32_t mbpr = sw / 8, sdepth = bm_depth(srcbm), wmask = rp->mask;
    lock_rp(st, rp);
    own(st);
    // the source is (or may be) the destination: its rows read from a copy
    // made first, so rows and words written early are never read back as
    // source (an overlap down or to the right)
    gfx_layer *ly = static_cast<gfx_layer *>(rp->layer);
    uint8_t *copy = nullptr;
    uint32_t cbpr = srcbm->bytesperrow, csize = 0;
    if (srcbm == rp->bitmap || (ly && ly->superbitmap == srcbm)) {
        csize = static_cast<uint32_t>(sdepth) * static_cast<uint32_t>(h) * cbpr;
        wait_blit(st);
        copy = static_cast<uint8_t *>(E(st)->alloc(E(st)->ctx, csize, GFX_MEMF_ANY));
        // each row's own plane part only: an interleaved bitmap's next
        // planes follow it in the row, and its last row ends the block
        uint32_t part = plane_bytes(srcbm);
        for (int32_t pl = 0; copy && pl < sdepth; pl++)
            if (plane_ok(srcbm->planes[pl]))
                for (uint32_t r = 0; r < static_cast<uint32_t>(h); r++)
                    for (uint32_t i = 0; i < part; i++)
                        copy[(static_cast<uint32_t>(pl) * static_cast<uint32_t>(h) + r) * cbpr + i] =
                            srcbm->planes[pl][(static_cast<uint32_t>(sy) + r) * cbpr + i];
    }
    each_piece(rp, dx, dy, w, h, [&](gfx_bitmap *bm, int pl, int32_t bx, int32_t by, int32_t pw, int32_t ph,
                                     int32_t rx, int32_t ry) {
        if (pl >= sdepth || !(wmask >> pl & 1) || !plane_ok(bm->planes[pl])) return;
        uint8_t *s = srcbm->planes[pl];
        src bsrc = !s ? konst(0) : s == reinterpret_cast<uint8_t *>(-1) ? konst(0xFFFF)
                 : copy ? mem(copy + (static_cast<uint32_t>(pl) * static_cast<uint32_t>(h) + static_cast<uint32_t>(ry)) * cbpr,
                              cbpr, sx + rx)
                        : mem(s + (sy + ry) * srcbm->bytesperrow, srcbm->bytesperrow, sx + rx);
        op(st, row_of(bm, pl, by), bm->bytesperrow, bx, pw, ph, mem(mk + (sy + ry) * mbpr, mbpr, sx + rx), bsrc, mt);
    });
    if (copy) {
        wait_blit(st);      // the last blit may still be reading it (Chip RAM)
        E(st)->free(E(st)->ctx, copy, csize);
    }
    disown(st);
    unlock_rp(st, rp);
}

// ---- the GEL list -------------------------------------------------------------

void gels_InitGels(gels_state *, gl_vsprite *head, gl_vsprite *tail, gl_gelsinfo *gi)
{
    head->next = tail;
    head->prev = nullptr;
    tail->next = nullptr;
    tail->prev = head;
    head->y = head->x = -32768;
    tail->y = tail->x = 32767;
    head->clearpath = tail->clearpath = nullptr;
    gi->gelhead = head;
    gi->geltail = tail;
}

void gels_AddVSprite(gels_state *, gl_vsprite *vs, gfx_rastport *rp)
{
    vs->flags &= 0x00FF;
    vs->clearpath = nullptr;
    insert_sorted(gels_of(rp), vs);
}

// AddBob. A Bob still in the list waiting to go (BOBSAWAY, not yet BOBNIX:
// an Animate sequence that comes back to it before a DrawGList) stays,
// keeping its saved backgrounds, and is only sorted in again (ours: ABI
// test).
void gels_AddBob(gels_state *st, gl_bob *bob, gfx_rastport *rp)
{
    bool listed = (bob->flags & (GL_BOBSAWAY | GL_BOBNIX)) == GL_BOBSAWAY;
    bob->flags &= ~(GL_BWAITING | GL_BDRAWN | GL_BOBSAWAY | GL_BOBNIX | GL_OUTSTEP);
    bob->bobvsprite->vsbob = bob;
    if (listed) {
        unlink(bob->bobvsprite);
        insert_sorted(gels_of(rp), bob->bobvsprite);
        return;
    }
    if (bob->dbuffer) bob->dbuffer->bufpath = nullptr;
    gels_AddVSprite(st, bob->bobvsprite, rp);
}

void gels_RemVSprite(gels_state *, gl_vsprite *vs)
{
    gl_vsprite *head = head_of(vs);
    // the gel list's GelsInfo is not at hand: the chains hang off its head
    // and tail, so take vs out of them through those
    gl_gelsinfo gi;
    gi.gelhead = head;
    gl_vsprite *t = vs;
    while (t->next) t = t->next;
    gi.geltail = t;
    unchain(&gi, vs);
    unlink(vs);
}

void gels_SortGList(gels_state *, gfx_rastport *rp)
{
    gl_gelsinfo *gi = gels_of(rp);
    gl_vsprite *head = gi->gelhead, *tail = gi->geltail;
    gl_vsprite *v = head->next->next;
    if (head->next == tail) return;
    while (v && v != tail) {
        gl_vsprite *next = v->next, *at = v->prev;
        while (at != head && sorts_before(v, at)) at = at->prev;
        if (at != v->prev) {
            unlink(v);
            v->prev = at;
            v->next = at->next;
            at->next->prev = v;
            at->next = v;
        }
        v = next;
    }
}

// DrawGList: erase, retire, place the VSprites, draw.
//  1. Every Bob saved by the last DrawGList, newest first, gets its
//     background back (gelHead->ClearPath's chain). A double buffered
//     Bob's last save is on the other bitmap, so it keeps it: its save
//     from the DrawGList before (on this bitmap, gelTail->ClearPath's
//     chain through DBufPacket's BufPath) is put back instead, and the
//     two saves change places.
//  2. A Bob with BOBSAWAY that has no saved background left leaves the
//     list with BOBNIX (a double buffered one after two DrawGLists).
//  3. The VSprites get hardware sprites (gfx_hook_vsprites).
//  4. The Bobs are drawn in the list's order, each after its After
//     chain's, saving their backgrounds.
void gels_DrawGList(gels_state *st, gfx_rastport *rp, void *vp)
{
    gl_gelsinfo *gi = gels_of(rp);
    if (!gi || !gi->gelhead || !gi->geltail) return;
    gl_vsprite *head = gi->gelhead, *tail = gi->geltail;
    lock_rp(st, rp);
    own(st);
    // 1
    gl_vsprite *v = head->clearpath, *dbfirst = nullptr, *dblast = nullptr;
    head->clearpath = nullptr;
    while (v) {
        gl_vsprite *next = v->clearpath;
        gl_bob *bob = v->vsbob;
        v->flags &= ~GL_BACKSAVED;
        if (bob && bob->dbuffer) {
            v->clearpath = nullptr;
            if (dblast) dblast->drawpath = v;
            else dbfirst = v;
            dblast = v;
            v->drawpath = nullptr;
        } else {
            v->clearpath = nullptr;
            if (bob && bob->savebuffer) restore_back(st, rp, v, bob->savebuffer, v->oldx, v->oldy);
        }
        v = next;
    }
    for (v = tail->clearpath; v && v != tail;) {
        gl_dbufpacket *db = v->vsbob->dbuffer;
        gl_vsprite *next = db->bufpath;
        if (db->bufbuffer) restore_back(st, rp, v, db->bufbuffer, db->bufx, db->bufy);
        db->bufpath = nullptr;
        v = next;
    }
    tail->clearpath = nullptr;
    gl_vsprite *c1last = nullptr;
    for (v = dbfirst; v;) {
        gl_vsprite *next = v->drawpath;
        v->drawpath = nullptr;
        swap_saves(v, v->vsbob);
        v->vsbob->dbuffer->bufpath = tail;
        if (c1last) c1last->vsbob->dbuffer->bufpath = v;
        else tail->clearpath = v;
        c1last = v;
        v = next;
    }
    // 2
    for (v = head->next; v && v != tail;) {
        gl_vsprite *next = v->next;
        if (is_bob(v)) {
            gl_bob *bob = v->vsbob;
            bob->flags &= ~(GL_BDRAWN | GL_BWAITING);
            if ((bob->flags & GL_BOBSAWAY) && !(bob->dbuffer && bob->dbuffer->bufpath)) {
                unlink(v);
                v->next = v->prev = nullptr;
                bob->flags |= GL_BOBNIX;
            }
        }
        v = next;
    }
    // 3
    place_vsprites(st, gi, vp);
    // 4
    for (v = head->next; v && v != tail; v = v->next)
        if (is_bob(v) && !(v->vsbob->flags & (GL_BDRAWN | GL_BOBSAWAY))) draw_ordered(st, rp, gi, v->vsbob);
    disown(st);
    unlock_rp(st, rp);
}

// RemIBob: the Bobs drawn after bob are erased (newest first), then bob,
// which leaves the list with BOBNIX; the others are drawn again in their
// order. A double buffered Bob's save on the other bitmap is dropped.
// ABI test: OS 3.2's RemIBob and the Bobs over the one removed.
void gels_RemIBob(gels_state *st, gl_bob *bob, gfx_rastport *rp, void *)
{
    gl_gelsinfo *gi = gels_of(rp);
    gl_vsprite *vs = bob->bobvsprite;
    lock_rp(st, rp);
    own(st);
    gl_vsprite *redo = nullptr;
    if (vs->flags & GL_BACKSAVED) {
        gl_vsprite *v = gi->gelhead->clearpath;
        while (v) {
            gl_vsprite *next = v->clearpath;
            restore_back(st, rp, v, v->vsbob->savebuffer, v->oldx, v->oldy);
            v->flags &= ~GL_BACKSAVED;
            gi->gelhead->clearpath = next;
            if (v == vs) {
                v->clearpath = nullptr;
                break;
            }
            v->clearpath = redo;
            redo = v;
            v = next;
        }
    }
    unchain(gi, vs);
    unlink(vs);
    vs->next = vs->prev = nullptr;
    bob->flags |= GL_BOBNIX;
    while (redo) {
        gl_vsprite *next = redo->clearpath;
        redo->clearpath = nullptr;
        draw_bob(st, rp, gi, redo->vsbob);
        redo = next;
    }
    disown(st);
    unlock_rp(st, rp);
}

// DoCollision: in the list's order, a gel with HitMask bit 0 against the
// GelsInfo's borders (routine 0, with the borders it crosses), then each
// pair whose pixels meet, the earlier gel's HitMask against the later's
// MeMask: the lowest bit both have, 1 to 15, picks the routine, called
// (earlier, later). ABI test: the order and the masks' roles on OS 3.2.
void gels_DoCollision(gels_state *, gfx_rastport *rp)
{
    gl_gelsinfo *gi = gels_of(rp);
    if (!gi || !gi->gelhead) return;
    gl_colltable *ct = gi->collhandler;
    gl_vsprite *tail = gi->geltail;
    for (gl_vsprite *a = gi->gelhead->next; a && a != tail; a = a->next) {
        if (a->hitmask & 1) {
            int32_t l = 0, r = 0;
            uint32_t hit = 0;
            if (border_span(a, l, r)) {
                if (a->y < gi->topmost) hit |= GL_TOPHIT;
                if (a->y + a->height - 1 > gi->bottommost) hit |= GL_BOTTOMHIT;
                if (a->x + l < gi->leftmost) hit |= GL_LEFTHIT;
                if (a->x + r > gi->rightmost) hit |= GL_RIGHTHIT;
            }
            if (hit && ct && ct->collptrs[0])
                ct->collptrs[0](a, reinterpret_cast<void *>(static_cast<uintptr_t>(hit)));
        }
        for (gl_vsprite *b = a->next; b && b != tail && b->y <= a->y + a->height - 1; b = b->next) {
            uint32_t m = static_cast<uint16_t>(a->hitmask & b->memask) & 0xFFFE;
            if (!m || !gels_meet(a, b)) continue;
            int bit = 1;
            while (!(m >> bit & 1)) bit++;
            if (ct && ct->collptrs[bit]) ct->collptrs[bit](a, b);
        }
    }
}

void gels_SetCollision(gels_state *, uint32_t num, gl_collfn routine, gl_gelsinfo *gi)
{
    if (num < 16 && gi && gi->collhandler) gi->collhandler->collptrs[num] = routine;
}

// InitMasks: CollMask, every plane of the image ORed (a VSprite's two
// planes come a line at a time), and BorderLine, every row of CollMask
// ORed.
void gels_InitMasks(gels_state *, gl_vsprite *vs)
{
    int32_t ww = vs->width, hh = vs->height, depth = (vs->flags & GL_VSPRITE) ? 2 : vs->depth;
    const uint16_t *img = reinterpret_cast<const uint16_t *>(vs->imagedata);
    uint16_t *cm = reinterpret_cast<uint16_t *>(vs->collmask), *bl = reinterpret_cast<uint16_t *>(vs->borderline);
    if (bl)
        for (int32_t i = 0; i < ww; i++) bl[i] = 0;
    for (int32_t r = 0; r < hh; r++)
        for (int32_t i = 0; i < ww; i++) {
            uint16_t m = 0;
            for (int32_t k = 0; k < depth && img; k++)
                m |= (vs->flags & GL_VSPRITE) ? img[(r * depth + k) * ww + i] : img[(k * hh + r) * ww + i];
            if (cm) cm[r * ww + i] = m;
            if (bl) bl[i] |= m;
        }
}

// ---- animation ------------------------------------------------------------------

void gels_AddAnimOb(gels_state *st, gl_animob *ob, gl_animob **key, gfx_rastport *rp)
{
    ob->prevob = nullptr;
    ob->nextob = *key;
    if (*key) (*key)->prevob = ob;
    *key = ob;
    for (gl_animcomp *c = ob->headcomp; c; c = c->nextcomp) {
        c->headob = ob;
        c->timer = c->timeset;
        if (c->animbob) gels_AddBob(st, c->animbob, rp);
    }
}

// Animate, each AnimOb: the clock, acceleration into velocity and
// velocity into place (fixed point, ANFRACSIZE bits of fraction), its
// routine; each component's timer, and at 0 the next of its sequence in
// its place (the old Bob goes with BOBSAWAY, the new one is added, and
// RINGTRIGGER moves the AnimOb by RingXTrans, RingYTrans); every
// component's Bob to the AnimOb's place plus its own offset; then each
// component's routine. ABI test: the order and the rounding on OS 3.2.
void gels_Animate(gels_state *st, gl_animob **key, gfx_rastport *rp)
{
    for (gl_animob *ob = *key; ob; ob = ob->nextob) {
        ob->clock++;
        ob->anoldx = ob->anx;
        ob->anoldy = ob->any;
        ob->xvel = static_cast<int16_t>(ob->xvel + ob->xaccel);
        ob->yvel = static_cast<int16_t>(ob->yvel + ob->yaccel);
        ob->anx = static_cast<int16_t>(ob->anx + ob->xvel);
        ob->any = static_cast<int16_t>(ob->any + ob->yvel);
        if (ob->animoroutine) ob->animoroutine(ob);
        for (gl_animcomp *c = ob->headcomp; c;) {
            gl_animcomp *next = c->nextcomp;
            if (c->timeset && c->timer > 0) c->timer--;
            if (c->timeset && c->timer == 0 && c->nextseq && c->nextseq != c) {
                gl_animcomp *n = c->nextseq;
                n->prevcomp = c->prevcomp;
                n->nextcomp = c->nextcomp;
                if (c->prevcomp) c->prevcomp->nextcomp = n;
                else ob->headcomp = n;
                if (c->nextcomp) c->nextcomp->prevcomp = n;
                c->prevcomp = c->nextcomp = nullptr;
                n->headob = ob;
                n->timer = n->timeset;
                if (n->flags & GL_RINGTRIGGER) {
                    ob->anx = static_cast<int16_t>(ob->anx + ob->ringxtrans);
                    ob->any = static_cast<int16_t>(ob->any + ob->ringytrans);
                }
                if (c->animbob != n->animbob) {
                    if (c->animbob) c->animbob->flags |= GL_BOBSAWAY;
                    if (n->animbob) gels_AddBob(st, n->animbob, rp);
                }
            }
            c = next;
        }
        for (gl_animcomp *c = ob->headcomp; c; c = c->nextcomp)
            if (c->animbob && c->animbob->bobvsprite) {
                c->animbob->bobvsprite->x = static_cast<int16_t>(ring_x(ob) + c->xtrans);
                c->animbob->bobvsprite->y = static_cast<int16_t>(ring_y(ob) + c->ytrans);
            }
        for (gl_animcomp *c = ob->headcomp; c; c = c->nextcomp)
            if (c->animcroutine) c->animcroutine(c);
    }
}

// The sizes GetGBuffers allocates and FreeGBuffers frees.
struct gsizes {
    uint32_t mask, line, save;
};
static gsizes sizes_of(gl_vsprite *vs, gfx_rastport *rp)
{
    uint32_t w = static_cast<uint16_t>(vs->width), h = static_cast<uint16_t>(vs->height);
    uint32_t d = rp && rp->bitmap ? static_cast<uint32_t>(bm_depth(rp->bitmap)) : 0;
    return gsizes{w * h * 2, w * 2, w * h * d * 2};
}

void gels_FreeGBuffers(gels_state *st, gl_animob *ob, gfx_rastport *rp, int32_t db)
{
    gfx_env *e = E(st);
    each_seq(ob, [&](gl_animcomp *c) {
        gl_bob *bob = c->animbob;
        if (!bob || !bob->bobvsprite) return;
        gl_vsprite *vs = bob->bobvsprite;
        gsizes s = sizes_of(vs, rp);
        auto drop = [&](int16_t *&p, uint32_t n) {
            if (p && n) e->free(e->ctx, p, n);
            p = nullptr;
        };
        drop(bob->imageshadow, s.mask);
        drop(vs->collmask, s.mask);
        drop(vs->borderline, s.line);
        drop(bob->savebuffer, s.save);
        if (db && bob->dbuffer) {
            drop(bob->dbuffer->bufbuffer, s.save);
            e->free(e->ctx, bob->dbuffer, sizeof(gl_dbufpacket));
            bob->dbuffer = nullptr;
        }
    });
}

// GetGBuffers: every Bob of every sequence gets ImageShadow, CollMask,
// BorderLine and SaveBuffer (Chip RAM, the bitmap's depth), and with db a
// DBufPacket and its BufBuffer. Out of memory, what it got goes again and
// it answers 0.
int32_t gels_GetGBuffers(gels_state *st, gl_animob *ob, gfx_rastport *rp, int32_t db)
{
    gfx_env *e = E(st);
    bool ok = true;
    // nothing of the user's is freed if it fails: every pointer to fill is 0 first
    each_seq(ob, [&](gl_animcomp *c) {
        gl_bob *bob = c->animbob;
        if (!bob || !bob->bobvsprite) return;
        bob->imageshadow = bob->savebuffer = nullptr;
        bob->bobvsprite->collmask = bob->bobvsprite->borderline = nullptr;
        if (db) bob->dbuffer = nullptr;
    });
    each_seq(ob, [&](gl_animcomp *c) {
        gl_bob *bob = c->animbob;
        if (!ok || !bob || !bob->bobvsprite) return;
        gl_vsprite *vs = bob->bobvsprite;
        gsizes s = sizes_of(vs, rp);
        auto get = [&](int16_t *&p, uint32_t n) {
            p = nullptr;
            if (!ok || !n) return;
            p = static_cast<int16_t *>(e->alloc(e->ctx, n, GFX_MEMF_CHIP | GFX_MEMF_CLEAR));
            if (!p) ok = false;
        };
        get(bob->imageshadow, s.mask);
        get(vs->collmask, s.mask);
        get(vs->borderline, s.line);
        get(bob->savebuffer, s.save);
        if (db) {
            bob->dbuffer = ok ? static_cast<gl_dbufpacket *>(e->alloc(e->ctx, sizeof(gl_dbufpacket),
                                                                       GFX_MEMF_ANY | GFX_MEMF_CLEAR))
                              : nullptr;
            if (!bob->dbuffer) ok = false;
            else get(bob->dbuffer->bufbuffer, s.save);
        }
    });
    if (ok) return 1;
    gels_FreeGBuffers(st, ob, rp, db);
    return 0;
}

void gels_InitGMasks(gels_state *st, gl_animob *ob)
{
    each_seq(ob, [&](gl_animcomp *c) {
        if (c->animbob && c->animbob->bobvsprite) gels_InitMasks(st, c->animbob->bobvsprite);
    });
}

} // extern "C"
