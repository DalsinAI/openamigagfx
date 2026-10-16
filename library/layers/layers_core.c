/* SPDX-License-Identifier: MIT */
/* OpenLayers' core (layers_core.h). Copyright (c) 2026 AmigaChrome Team. MIT. */
#include "layers_core.h"

int lc_meet(const struct lc_rect *a, const struct lc_rect *b, struct lc_rect *out)
{
    struct lc_rect m;
    m.x0 = a->x0 > b->x0 ? a->x0 : b->x0;
    m.y0 = a->y0 > b->y0 ? a->y0 : b->y0;
    m.x1 = a->x1 < b->x1 ? a->x1 : b->x1;
    m.y1 = a->y1 < b->y1 ? a->y1 : b->y1;
    if (m.x0 > m.x1 || m.y0 > m.y1) return 0;
    if (out) *out = m;
    return 1;
}

/* r less the part m inside it (m within r): up to four rectangles, the
 * band above m and the band below it across r's width, then left and
 * right of m. Answers how many. */
uint32_t lc_cut(const struct lc_rect *r, const struct lc_rect *m, struct lc_rect out[4])
{
    uint32_t k = 0;
    if (r->y0 < m->y0) { out[k] = *r; out[k].y1 = m->y0 - 1; k++; }
    if (m->y1 < r->y1) { out[k] = *r; out[k].y0 = m->y1 + 1; k++; }
    if (r->x0 < m->x0) { out[k].x0 = r->x0; out[k].x1 = m->x0 - 1; out[k].y0 = m->y0; out[k].y1 = m->y1; k++; }
    if (m->x1 < r->x1) { out[k].x0 = m->x1 + 1; out[k].x1 = r->x1; out[k].y0 = m->y0; out[k].y1 = m->y1; k++; }
    return k;
}

/* The same, columns first: left and right whole height, then above and
 * below. */
static uint32_t cut_x(const struct lc_rect *r, const struct lc_rect *m, struct lc_rect out[4])
{
    uint32_t k = 0;
    if (r->x0 < m->x0) { out[k] = *r; out[k].x1 = m->x0 - 1; k++; }
    if (m->x1 < r->x1) { out[k] = *r; out[k].x0 = m->x1 + 1; k++; }
    if (r->y0 < m->y0) { out[k].y0 = r->y0; out[k].y1 = m->y0 - 1; out[k].x0 = m->x0; out[k].x1 = m->x1; k++; }
    if (m->y1 < r->y1) { out[k].y0 = m->y1 + 1; out[k].y1 = r->y1; out[k].x0 = m->x0; out[k].x1 = m->x1; k++; }
    return k;
}

#define cut lc_cut

/* Grows an array of items of size sz to hold at least want. */
static int grow(const struct lc_mem *mem, void **arr, uint32_t *cap, uint32_t n, uint32_t want, uint32_t sz)
{
    uint32_t c = *cap ? *cap : 8, i;
    uint8_t *p;
    if (want <= *cap) return 0;
    while (c < want) c *= 2;
    p = mem->alloc(mem->ctx, c * sz);
    if (!p) return -1;
    for (i = 0; i < n * sz; i++) p[i] = ((uint8_t *)*arr)[i];
    if (*arr) mem->free(mem->ctx, *arr, *cap * sz);
    *arr = p;
    *cap = c;
    return 0;
}

static int reserve(struct lc_rects *s, uint32_t want)
{
    void *a = s->r;
    int e = grow(s->mem, &a, &s->cap, s->n, want, sizeof *s->r);
    s->r = a;
    return e;
}

void lc_init(struct lc_rects *s, const struct lc_mem *mem)
{
    s->r = 0;
    s->n = s->cap = 0;
    s->mem = mem;
}

void lc_free(struct lc_rects *s)
{
    if (s->r) s->mem->free(s->mem->ctx, s->r, s->cap * sizeof *s->r);
    s->r = 0;
    s->n = s->cap = 0;
}

void lc_clear(struct lc_rects *s) { s->n = 0; }

int lc_set(struct lc_rects *s, const struct lc_rect *r)
{
    s->n = 0;
    if (r->x0 > r->x1 || r->y0 > r->y1) return 0;
    if (reserve(s, 1)) return -1;
    s->r[s->n++] = *r;
    return 0;
}

int lc_copy(struct lc_rects *dst, const struct lc_rects *src)
{
    uint32_t i;
    dst->n = 0;
    if (reserve(dst, src->n)) return -1;
    for (i = 0; i < src->n; i++) dst->r[i] = src->r[i];
    dst->n = src->n;
    return 0;
}

static int sub_rect(struct lc_rects *s, const struct lc_rect *r, uint32_t (*cutter)(const struct lc_rect *, const struct lc_rect *, struct lc_rect *))
{
    uint32_t i, j, k, hits = 0, n0 = s->n;
    struct lc_rect m, out[4];

    for (i = 0; i < n0; i++) hits += lc_meet(&s->r[i], r, 0);
    if (!hits) return 0;
    if (reserve(s, n0 + 3 * hits)) return -1;
    /* each hit rectangle is replaced by its first piece, or marked empty,
     * and the rest go on the end; then the empties are squeezed out */
    for (i = 0; i < n0; i++) {
        if (!lc_meet(&s->r[i], r, &m)) continue;
        k = cutter(&s->r[i], &m, out);
        if (!k) { s->r[i].x0 = 1; s->r[i].x1 = 0; continue; }
        s->r[i] = out[0];
        for (j = 1; j < k; j++) s->r[s->n++] = out[j];
    }
    for (i = j = 0; i < s->n; i++)
        if (s->r[i].x0 <= s->r[i].x1) s->r[j++] = s->r[i];
    s->n = j;
    return 0;
}

int lc_sub_rect(struct lc_rects *s, const struct lc_rect *r) { return sub_rect(s, r, cut); }
int lc_sub_rect_x(struct lc_rects *s, const struct lc_rect *r) { return sub_rect(s, r, cut_x); }

void lc_and_rect(struct lc_rects *s, const struct lc_rect *r)
{
    uint32_t i, j;
    for (i = j = 0; i < s->n; i++)
        if (lc_meet(&s->r[i], r, &s->r[j])) j++;
    s->n = j;
}

int lc_or_rect(struct lc_rects *s, const struct lc_rect *r)
{
    struct lc_rects add;
    uint32_t i;
    int e = 0;

    lc_init(&add, s->mem);
    if (lc_set(&add, r)) return -1;
    for (i = 0; i < s->n && add.n && !e; i++) e = lc_sub_rect(&add, &s->r[i]);
    if (!e) e = reserve(s, s->n + add.n);
    if (!e) for (i = 0; i < add.n; i++) s->r[s->n++] = add.r[i];
    lc_free(&add);
    return e;
}

int lc_sub(struct lc_rects *s, const struct lc_rects *t)
{
    uint32_t i;
    for (i = 0; i < t->n && s->n; i++)
        if (lc_sub_rect(s, &t->r[i])) return -1;
    return 0;
}

int lc_and(struct lc_rects *s, const struct lc_rects *t)
{
    /* both sets are disjoint, so the pairwise meets are too */
    struct lc_rects out;
    struct lc_rect m;
    uint32_t i, j;

    lc_init(&out, s->mem);
    for (i = 0; i < s->n; i++)
        for (j = 0; j < t->n; j++) {
            if (!lc_meet(&s->r[i], &t->r[j], &m)) continue;
            if (reserve(&out, out.n + 1)) { lc_free(&out); return -1; }
            out.r[out.n++] = m;
        }
    lc_free(s);
    *s = out;
    return 0;
}

int lc_or(struct lc_rects *s, const struct lc_rects *t)
{
    uint32_t i;
    for (i = 0; i < t->n; i++)
        if (lc_or_rect(s, &t->r[i])) return -1;
    return 0;
}

void lc_offset(struct lc_rects *s, int32_t dx, int32_t dy)
{
    uint32_t i;
    for (i = 0; i < s->n; i++) {
        s->r[i].x0 += dx; s->r[i].x1 += dx;
        s->r[i].y0 += dy; s->r[i].y1 += dy;
    }
}

uint32_t lc_area(const struct lc_rects *s)
{
    uint32_t i, a = 0;
    for (i = 0; i < s->n; i++)
        a += (uint32_t)(s->r[i].x1 - s->r[i].x0 + 1) * (uint32_t)(s->r[i].y1 - s->r[i].y0 + 1);
    return a;
}

void lc_pieces_init(struct lc_pieces *s, const struct lc_mem *mem)
{
    s->p = 0;
    s->n = s->cap = 0;
    s->mem = mem;
}

void lc_pieces_free(struct lc_pieces *s)
{
    if (s->p) s->mem->free(s->mem->ctx, s->p, s->cap * sizeof *s->p);
    s->p = 0;
    s->n = s->cap = 0;
}

/* Cuts every visible piece meeting c: the part inside c becomes owner's,
 * the rest stays visible. As OS 3.2 keeps them: the visible pieces first,
 * each cut one replaced where it was by its visible rest, then the pieces
 * not on show in the order they came to be. */
static int cut_pieces(struct lc_pieces *s, const struct lc_rect *c, int32_t owner)
{
    uint32_t i, j, k, hits = 0, n0 = s->n, cap;
    struct lc_rect m, out[4];
    struct lc_piece *p;

    for (i = 0; i < n0; i++) hits += s->p[i].obscurer == LC_VISIBLE && lc_meet(&s->p[i].r, c, 0);
    if (!hits) return 0;
    cap = n0 + 4 * hits;
    if (!(p = s->mem->alloc(s->mem->ctx, cap * sizeof *p))) return -1;
    j = 0;
    for (i = 0; i < n0; i++) {
        uint32_t nk;
        if (s->p[i].obscurer != LC_VISIBLE) continue;
        if (!lc_meet(&s->p[i].r, c, &m)) { p[j++] = s->p[i]; continue; }
        nk = cut(&s->p[i].r, &m, out);
        for (k = 0; k < nk; k++) {
            p[j].r = out[k];
            p[j++].obscurer = LC_VISIBLE;
        }
    }
    for (i = 0; i < n0; i++)
        if (s->p[i].obscurer != LC_VISIBLE) p[j++] = s->p[i];
    for (i = 0; i < n0; i++)
        if (s->p[i].obscurer == LC_VISIBLE && lc_meet(&s->p[i].r, c, &m)) {
            p[j].r = m;
            p[j++].obscurer = owner;
        }
    s->mem->free(s->mem->ctx, s->p, s->cap * sizeof *s->p);
    s->p = p;
    s->n = j;
    s->cap = cap;
    return 0;
}

int lc_layer_pieces(const struct lc_layer *stack, uint32_t n, uint32_t idx, const struct lc_rect *screen,
                    struct lc_pieces *out)
{
    const struct lc_rect *b = &stack[idx].bounds;
    uint32_t k;
    void *a = out->p;

    out->n = 0;
    if (b->x0 > b->x1 || b->y0 > b->y1) return 0;
    if (grow(out->mem, &a, &out->cap, 0, 1, sizeof *out->p)) return -1;
    out->p = a;
    out->p[0].r = *b;
    if (stack[idx].flags & LC_HIDDEN) {
        /* a hidden layer is one piece, none of it on show */
        out->p[0].obscurer = LC_OFFSCREEN;
        out->n = 1;
        return 0;
    }
    if (screen) {
        /* everything off the screen first: the part on it stays visible */
        struct lc_rect m, rest[4];
        uint32_t j, cnt;
        if (!lc_meet(b, screen, &m)) {
            out->p[0].obscurer = LC_OFFSCREEN;
            out->n = 1;
            return 0;
        }
        cnt = cut(b, &m, rest);
        if (grow(out->mem, &a, &out->cap, 0, 1 + cnt, sizeof *out->p)) return -1;
        out->p = a;
        out->p[0].r = m;
        out->p[0].obscurer = LC_VISIBLE;
        for (j = 0; j < cnt; j++) {
            out->p[1 + j].r = rest[j];
            out->p[1 + j].obscurer = LC_OFFSCREEN;
        }
        out->n = 1 + cnt;
    } else {
        out->p[0].obscurer = LC_VISIBLE;
        out->n = 1;
    }
    /* front to back, so a piece goes to the frontmost layer covering it */
    for (k = 0; k < idx && k < n; k++) {
        if (stack[k].flags & LC_HIDDEN) continue;
        if (cut_pieces(out, &stack[k].bounds, (int32_t)k)) return -1;
    }
    return 0;
}

int lc_pieces_add(struct lc_pieces *s, const struct lc_rect *r, int32_t owner)
{
    void *a = s->p;
    if (grow(s->mem, &a, &s->cap, s->n, s->n + 1, sizeof *s->p)) return -1;
    s->p = a;
    s->p[s->n].r = *r;
    s->p[s->n++].obscurer = owner;
    return 0;
}

int lc_layer_visible(const struct lc_layer *stack, uint32_t n, uint32_t idx, const struct lc_rect *screen,
                     struct lc_rects *out)
{
    struct lc_pieces p;
    uint32_t i;
    int e;

    lc_pieces_init(&p, out->mem);
    out->n = 0;
    e = lc_layer_pieces(stack, n, idx, screen, &p);
    for (i = 0; i < p.n && !e; i++) {
        if (p.p[i].obscurer != LC_VISIBLE) continue;
        e = reserve(out, out->n + 1);
        if (!e) out->r[out->n++] = p.p[i].r;
    }
    lc_pieces_free(&p);
    return e;
}

int lc_uncovered(const struct lc_layer *stack, uint32_t n, const struct lc_rect *screen, struct lc_rects *out)
{
    uint32_t k;
    if (lc_set(out, screen)) return -1;
    for (k = 0; k < n && out->n; k++) {
        if (stack[k].flags & LC_HIDDEN) continue;
        if (lc_sub_rect(out, &stack[k].bounds)) return -1;
    }
    return 0;
}

int32_t lc_which(const struct lc_layer *stack, uint32_t n, int32_t x, int32_t y)
{
    uint32_t k;
    for (k = 0; k < n; k++) {
        const struct lc_rect *b = &stack[k].bounds;
        if (stack[k].flags & LC_HIDDEN) continue;
        if (x >= b->x0 && x <= b->x1 && y >= b->y0 && y <= b->y1) return (int32_t)k;
    }
    return -1;
}

/* Whether a is further along the move (dx, dy) than b: rows first. */
static int ahead(const struct lc_rect *a, const struct lc_rect *b, int32_t dx, int32_t dy)
{
    if (dy && a->y0 != b->y0) return dy > 0 ? a->y0 > b->y0 : a->y0 < b->y0;
    if (dx && a->x0 != b->x0) return dx > 0 ? a->x0 > b->x0 : a->x0 < b->x0;
    return 0;
}

uint32_t lc_sort_for_move(struct lc_rect *r, uint32_t n, int32_t dx, int32_t dy)
{
    /* Kahn's order, chosen greedily: a rectangle may go next when where it
     * lands meets no other rectangle still to go. Disjoint rectangles moved
     * by one offset can always be ordered (Guibas and Yao, 1980). Among
     * those free, the one furthest along the move goes first, which keeps
     * banded lists in band order. */
    uint32_t pos, i, j, stuck = 0;
    for (pos = 0; pos < n; pos++) {
        int32_t best = -1;
        for (i = pos; i < n; i++) {
            struct lc_rect d = r[i];
            d.x0 += dx; d.x1 += dx; d.y0 += dy; d.y1 += dy;
            for (j = pos; j < n; j++)
                if (j != i && lc_meet(&d, &r[j], 0)) break;
            if (j < n) continue;
            if (best < 0 || ahead(&r[i], &r[best], dx, dy)) best = (int32_t)i;
        }
        if (best < 0) { stuck++; best = (int32_t)pos; }
        if ((uint32_t)best != pos) {
            struct lc_rect t = r[pos];
            r[pos] = r[best];
            r[best] = t;
        }
    }
    return stuck;
}
