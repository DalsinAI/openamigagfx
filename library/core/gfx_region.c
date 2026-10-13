/* SPDX-License-Identifier: MIT */
/* OpenGfx's regions (gfx_region.h). Copyright (c) 2026 Dalsin Limited. MIT.
 *
 * Every call is one sweep: the edges of both operands cut the plane into
 * bands; in each band the two operands are runs of x, combined by the
 * operation; a band whose runs equal the band just above it grows that
 * band instead of starting a new one. Work is in half-open int32 spans so
 * that MaxX + 1 of 32767 needs no care. */
#include "gfx_region.h"

struct span { int32_t x0, x1; };
struct box { int32_t x0, y0, x1, y1; };

void gfx_rgn_init(struct gfx_region *r)
{
    r->bounds.min_x = r->bounds.min_y = r->bounds.max_x = r->bounds.max_y = 0;
    r->rects = 0;
}

static void free_list(struct gfx_rrect *p, const struct gfx_rgn_mem *m)
{
    while (p) {
        struct gfx_rrect *n = p->next;
        m->free(m->ctx, p, sizeof *p);
        p = n;
    }
}

void gfx_rgn_clear(struct gfx_region *r, const struct gfx_rgn_mem *m)
{
    free_list(r->rects, m);
    gfx_rgn_init(r);
}

static uint32_t count(const struct gfx_region *r)
{
    const struct gfx_rrect *p;
    uint32_t n = 0;
    for (p = r->rects; p; p = p->next) n++;
    return n;
}

static uint32_t boxes(const struct gfx_region *r, struct box *b)
{
    const struct gfx_rrect *p;
    uint32_t n = 0;
    for (p = r->rects; p; p = p->next) {
        if (p->bounds.max_x < p->bounds.min_x || p->bounds.max_y < p->bounds.min_y) continue;
        b[n].x0 = (int32_t)r->bounds.min_x + p->bounds.min_x;
        b[n].y0 = (int32_t)r->bounds.min_y + p->bounds.min_y;
        b[n].x1 = (int32_t)r->bounds.min_x + p->bounds.max_x + 1;
        b[n].y1 = (int32_t)r->bounds.min_y + p->bounds.max_y + 1;
        n++;
    }
    return n;
}

static void sort_ints(int32_t *a, uint32_t n)
{
    uint32_t gap, i, j;
    for (gap = n / 2; gap; gap /= 2)
        for (i = gap; i < n; i++) {
            int32_t v = a[i];
            for (j = i; j >= gap && a[j - gap] > v; j -= gap) a[j] = a[j - gap];
            a[j] = v;
        }
}

/* The runs of x that the boxes cover through the band y0..y1, sorted and
 * merged where they overlap or touch. */
static uint32_t runs(const struct box *b, uint32_t n, int32_t y0, int32_t y1, struct span *s)
{
    uint32_t i, j, k = 0;
    for (i = 0; i < n; i++)
        if (b[i].y0 <= y0 && b[i].y1 >= y1) {
            struct span v;
            v.x0 = b[i].x0;
            v.x1 = b[i].x1;
            for (j = k; j > 0 && s[j - 1].x0 > v.x0; j--) s[j] = s[j - 1];
            s[j] = v;
            k++;
        }
    if (!k) return 0;
    for (i = 1, j = 0; i < k; i++)
        if (s[i].x0 <= s[j].x1) {
            if (s[i].x1 > s[j].x1) s[j].x1 = s[i].x1;
        } else
            s[++j] = s[i];
    return j + 1;
}

static int in(int op, int a, int b)
{
    switch (op) {
    case GFX_RGN_OR: return a || b;
    case GFX_RGN_AND: return a && b;
    case GFX_RGN_XOR: return a != b;
    default: return a && !b;    /* CLEAR: dst less the rectangle */
    }
}

/* Two sorted lists of runs combined by op, into o; e is room for their
 * edges. */
static uint32_t combine(int op, const struct span *a, uint32_t na, const struct span *b, uint32_t nb,
                        int32_t *e, struct span *o)
{
    uint32_t ia = 0, ib = 0, ne = 0, k, n = 0;
    /* both lists' edges, in order, each once */
    while (ia < 2 * na || ib < 2 * nb) {
        int32_t va = ia < 2 * na ? (ia & 1 ? a[ia >> 1].x1 : a[ia >> 1].x0) : INT32_MAX;
        int32_t vb = ib < 2 * nb ? (ib & 1 ? b[ib >> 1].x1 : b[ib >> 1].x0) : INT32_MAX;
        int32_t v = va < vb ? va : vb;
        if (va == v) ia++;
        if (vb == v) ib++;
        if (!ne || e[ne - 1] != v) e[ne++] = v;
    }
    ia = ib = 0;
    for (k = 0; k + 1 < ne; k++) {
        int32_t x = e[k];
        while (ia < na && a[ia].x1 <= x) ia++;
        while (ib < nb && b[ib].x1 <= x) ib++;
        if (!in(op, ia < na && a[ia].x0 <= x, ib < nb && b[ib].x0 <= x)) continue;
        if (n && o[n - 1].x1 == x)
            o[n - 1].x1 = e[k + 1];
        else {
            o[n].x0 = x;
            o[n].x1 = e[k + 1];
            n++;
        }
    }
    return n;
}

/* dst's answer for op with the boxes b of the other operand. */
static int sweep(struct gfx_region *dst, int op, const struct box *b, uint32_t nb, const struct gfx_rgn_mem *m)
{
    uint32_t na = count(dst), i, ny = 0;
    uint32_t wsize = (na + nb) * (uint32_t)(sizeof(struct box) + 4 * sizeof(int32_t) + 3 * sizeof(struct span))
                     + 8;
    struct box *a;
    int32_t *ys, *edges;
    struct span *sa, *sb, *so;
    struct gfx_rrect *head = 0, *tail = 0, *band = 0;
    int32_t band_y1 = 0, x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
    void *w = m->alloc(m->ctx, wsize);
    if (!w) return 0;
    a = (struct box *)w;
    ys = (int32_t *)(a + na + nb);
    sa = (struct span *)(ys + 2 * (na + nb));
    sb = sa + na + nb;
    so = sb + na + nb;
    edges = (int32_t *)(so + na + nb);
    na = boxes(dst, a);
    for (i = 0; i < na; i++) { ys[ny++] = a[i].y0; ys[ny++] = a[i].y1; }
    for (i = 0; i < nb; i++) { ys[ny++] = b[i].y0; ys[ny++] = b[i].y1; }
    sort_ints(ys, ny);
    for (i = 0; i + 1 < ny; i++) {
        uint32_t ka, kb, ko, j;
        struct gfx_rrect *p;
        int same;
        if (ys[i] == ys[i + 1]) continue;
        ka = runs(a, na, ys[i], ys[i + 1], sa);
        kb = runs(b, nb, ys[i], ys[i + 1], sb);
        ko = combine(op, sa, ka, sb, kb, edges, so);
        if (!ko) continue;
        /* the same runs as the band just above, touching it: grow that band */
        same = band && band_y1 == ys[i];
        for (p = band, j = 0; same && j < ko; j++) {
            same = p && p->bounds.min_x == so[j].x0 && p->bounds.max_x == so[j].x1 - 1;
            if (same) p = p->next;
        }
        if (same && !p) {
            for (p = band; p; p = p->next) p->bounds.max_y = (int16_t)(ys[i + 1] - 1);
        } else {
            band = 0;
            for (j = 0; j < ko; j++) {
                p = (struct gfx_rrect *)m->alloc(m->ctx, sizeof *p);
                if (!p) {
                    free_list(head, m);
                    m->free(m->ctx, w, wsize);
                    return 0;
                }
                p->next = 0;
                p->prev = tail;
                if (tail) tail->next = p; else head = p;
                tail = p;
                if (!band) band = p;
                p->bounds.min_x = (int16_t)so[j].x0;
                p->bounds.max_x = (int16_t)(so[j].x1 - 1);
                p->bounds.min_y = (int16_t)ys[i];
                p->bounds.max_y = (int16_t)(ys[i + 1] - 1);
            }
            if (so[0].x0 < x0) x0 = so[0].x0;
            if (so[ko - 1].x1 > x1) x1 = so[ko - 1].x1;
            if (ys[i] < y0) y0 = ys[i];
        }
        band_y1 = ys[i + 1];
        if (band_y1 > y1) y1 = band_y1;
    }
    m->free(m->ctx, w, wsize);
    free_list(dst->rects, m);
    gfx_rgn_init(dst);
    if (!head) return 1;
    dst->bounds.min_x = (int16_t)x0;
    dst->bounds.min_y = (int16_t)y0;
    dst->bounds.max_x = (int16_t)(x1 - 1);
    dst->bounds.max_y = (int16_t)(y1 - 1);
    dst->rects = head;
    for (tail = head; tail; tail = tail->next) {
        tail->bounds.min_x = (int16_t)(tail->bounds.min_x - x0);
        tail->bounds.max_x = (int16_t)(tail->bounds.max_x - x0);
        tail->bounds.min_y = (int16_t)(tail->bounds.min_y - y0);
        tail->bounds.max_y = (int16_t)(tail->bounds.max_y - y0);
    }
    return 1;
}

int gfx_rgn_rect(struct gfx_region *dst, int op, const struct gfx_rect *rect, const struct gfx_rgn_mem *m)
{
    struct box b;
    uint32_t n = 0;
    if (rect->max_x >= rect->min_x && rect->max_y >= rect->min_y) {
        b.x0 = rect->min_x;
        b.y0 = rect->min_y;
        b.x1 = (int32_t)rect->max_x + 1;
        b.y1 = (int32_t)rect->max_y + 1;
        n = 1;
    }
    return sweep(dst, op, &b, n, m);
}

int gfx_rgn_region(struct gfx_region *dst, int op, const struct gfx_region *src, const struct gfx_rgn_mem *m)
{
    uint32_t n = count(src);
    uint32_t size = n * (uint32_t)sizeof(struct box) + 4;
    struct box *b = (struct box *)m->alloc(m->ctx, size);
    int ok;
    if (!b) return 0;
    n = boxes(src, b);
    ok = sweep(dst, op, b, n, m);
    m->free(m->ctx, b, size);
    return ok;
}
