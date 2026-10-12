/* SPDX-License-Identifier: MIT */
/* OpenGfx's core (gfx_core.h). Copyright (c) 2026 Dalsin Limited. MIT. */
#include "gfx_core.h"

typedef uint32_t __attribute__((may_alias)) u32a;
#define PLANE_OK(p) ((p) && (p) != (uint8_t *)-1)

static uint32_t plane_bit(const uint8_t *pl, uint32_t off, uint8_t bit)
{
    if (!pl) return 0;
    if (pl == (uint8_t *)-1) return 1;
    return (pl[off] & bit) != 0;
}

void gfx_planar_put(const struct gfx_planar *bm, int32_t x, int32_t y, uint32_t pen, uint32_t mask, int mode)
{
    uint32_t off = (uint32_t)y * bm->bytes_per_row + ((uint32_t)x >> 3), p;
    uint8_t bit = (uint8_t)(0x80 >> (x & 7));

    for (p = 0; p < bm->depth; p++) {
        uint8_t *pl = bm->planes[p];
        if (!(mask >> p & 1) || !PLANE_OK(pl)) continue;
        if (mode == GFX_INVERT) pl[off] ^= bit;
        else if (pen >> p & 1) pl[off] |= bit;
        else pl[off] &= (uint8_t)~bit;
    }
}

uint32_t gfx_planar_get(const struct gfx_planar *bm, int32_t x, int32_t y)
{
    uint32_t off = (uint32_t)y * bm->bytes_per_row + ((uint32_t)x >> 3), p, pen = 0;
    uint8_t bit = (uint8_t)(0x80 >> (x & 7));

    for (p = 0; p < bm->depth; p++)
        pen |= plane_bit(bm->planes[p], off, bit) << p;
    return pen;
}

static void fill_bytes(uint8_t *d, uint32_t n, uint8_t v)
{
    while (n && ((uintptr_t)d & 3)) { *d++ = v; n--; }
    if (n >= 4) {
        uint32_t w = v * 0x01010101u;
        for (; n >= 4; n -= 4, d += 4) *(u32a *)d = w;
    }
    while (n--) *d++ = v;
}

void gfx_planar_span(const struct gfx_planar *bm, int32_t x0, int32_t x1, int32_t y, uint32_t pat,
                     uint32_t fg, uint32_t bg, uint32_t mask, int mode)
{
    uint32_t row = (uint32_t)y * bm->bytes_per_row, bx0 = (uint32_t)x0 >> 3, bx1 = (uint32_t)x1 >> 3, bx, p;
    uint8_t first = (uint8_t)(0xFF >> (x0 & 7)), last = (uint8_t)(0xFF << (7 - (x1 & 7)));

    for (p = 0; p < bm->depth; p++) {
        uint8_t *pl = bm->planes[p], f = fg >> p & 1 ? 0xFF : 0x00, b = bg >> p & 1 ? 0xFF : 0x00;
        if (!(mask >> p & 1) || !PLANE_OK(pl)) continue;
        pl += row;
        for (bx = bx0; bx <= bx1; bx++) {
            uint8_t edge = (uint8_t)((bx == bx0 ? first : 0xFF) & (bx == bx1 ? last : 0xFF));
            uint8_t on = (uint8_t)(bx & 1 ? pat : pat >> 8), d = pl[bx];
            if (mode == GFX_XOR) d ^= on & edge;
            else {
                d = (uint8_t)((d & ~(on & edge)) | (f & on & edge));
                if (mode == GFX_JAM2) d = (uint8_t)((d & ~(~on & edge)) | (b & ~on & edge));
            }
            pl[bx] = d;
        }
    }
}

void gfx_planar_fill_all(const struct gfx_planar *bm, uint32_t pen, uint32_t mask)
{
    uint32_t p, n = bm->bytes_per_row * bm->rows;

    for (p = 0; p < bm->depth; p++)
        if (mask >> p & 1 && PLANE_OK(bm->planes[p]))
            fill_bytes(bm->planes[p], n, pen >> p & 1 ? 0xFF : 0x00);
}

/* Eight pens to eight plane bytes: an 8x8 bit transpose. Pens s[0..7] go
 * in as rows (s[0] leftmost); out[p] is plane p's byte, bit 7 the leftmost
 * pixel. */
static void c2p8(const uint8_t *s, uint8_t out[8])
{
    uint32_t x = (uint32_t)s[0] << 24 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 8 | s[3];
    uint32_t y = (uint32_t)s[4] << 24 | (uint32_t)s[5] << 16 | (uint32_t)s[6] << 8 | s[7];
    uint32_t t;

    t = (x ^ (x >> 7)) & 0x00AA00AAu;  x ^= t ^ (t << 7);
    t = (y ^ (y >> 7)) & 0x00AA00AAu;  y ^= t ^ (t << 7);
    t = (x ^ (x >> 14)) & 0x0000CCCCu; x ^= t ^ (t << 14);
    t = (y ^ (y >> 14)) & 0x0000CCCCu; y ^= t ^ (t << 14);
    t = (x & 0xF0F0F0F0u) | ((y >> 4) & 0x0F0F0F0Fu);
    y = ((x << 4) & 0xF0F0F0F0u) | (y & 0x0F0F0F0Fu);
    x = t;
    /* row j of the transpose holds bit 7 - j of each pen */
    out[7] = (uint8_t)(x >> 24); out[6] = (uint8_t)(x >> 16); out[5] = (uint8_t)(x >> 8); out[4] = (uint8_t)x;
    out[3] = (uint8_t)(y >> 24); out[2] = (uint8_t)(y >> 16); out[1] = (uint8_t)(y >> 8); out[0] = (uint8_t)y;
}

void gfx_planar_write8(const struct gfx_planar *bm, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                       const uint8_t *src, uint32_t src_bpr, uint32_t mask)
{
    uint32_t bx0 = (uint32_t)x0 >> 3, bx1 = (uint32_t)x1 >> 3, bx, p, live = 0;
    int32_t y;

    for (p = 0; p < bm->depth; p++)
        if (mask >> p & 1 && PLANE_OK(bm->planes[p])) live |= 1u << p;
    if (!live) return;
    for (y = y0; y <= y1; y++, src += src_bpr) {
        uint32_t row = (uint32_t)y * bm->bytes_per_row;
        for (bx = bx0; bx <= bx1; bx++) {
            int32_t px = (int32_t)(bx << 3), i;
            uint8_t s[8], pb[8], keep = 0;
            for (i = 0; i < 8; i++) {
                if (px + i < x0 || px + i > x1) { s[i] = 0; keep |= (uint8_t)(0x80 >> i); }
                else s[i] = src[px + i - x0];
            }
            c2p8(s, pb);
            for (p = 0; p < bm->depth; p++) {
                uint8_t *d;
                if (!(live >> p & 1)) continue;
                d = bm->planes[p] + row + bx;
                *d = keep ? (uint8_t)((*d & keep) | (pb[p] & ~keep)) : pb[p];
            }
        }
    }
}

void gfx_planar_read8(const struct gfx_planar *bm, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                      uint8_t *dst, uint32_t dst_bpr)
{
    int32_t x, y;

    for (y = y0; y <= y1; y++, dst += dst_bpr)
        for (x = x0; x <= x1; x++)
            dst[x - x0] = (uint8_t)gfx_planar_get(bm, x, y);
}

/* Eight bits of a plane row from bit x on (bit 7 of the answer is x),
 * zeros past the row's end. */
static uint8_t bits8(const uint8_t *row, uint32_t nbytes, uint32_t x)
{
    uint32_t b = x >> 3, s = x & 7, v;

    v = (uint32_t)(b < nbytes ? row[b] : 0) << 8 | (b + 1 < nbytes ? row[b + 1] : 0);
    return (uint8_t)(v >> (8 - s));
}

void gfx_planar_blit(const struct gfx_planar *src, int32_t sx, int32_t sy, const struct gfx_planar *dst,
                     int32_t dx, int32_t dy, int32_t w, int32_t h, uint32_t minterm, uint32_t mask)
{
    uint8_t m7 = minterm & 0x80 ? 0xFF : 0, m6 = minterm & 0x40 ? 0xFF : 0;
    uint8_t m5 = minterm & 0x20 ? 0xFF : 0, m4 = minterm & 0x10 ? 0xFF : 0;
    uint32_t depth = src->depth < dst->depth ? src->depth : dst->depth, p;
    int32_t bx0 = dx >> 3, bx1 = (dx + w - 1) >> 3;
    uint8_t first = (uint8_t)(0xFF >> (dx & 7)), last = (uint8_t)(0xFF << (7 - ((dx + w - 1) & 7)));
    /* overlapping in one bitmap: rows from the bottom when moving down,
     * bytes from the right when moving right, so no source is overwritten
     * before it is read */
    int same = src == dst, up, left;

    if (!same && src->bytes_per_row == dst->bytes_per_row)
        for (same = 1, p = 0; p < 8; p++)
            if (src->planes[p] != dst->planes[p]) { same = 0; break; }
    up = same && dy > sy;
    left = same && dy == sy && dx > sx;

    if (w <= 0 || h <= 0) return;
    for (p = 0; p < depth; p++) {
        const uint8_t *sp = src->planes[p];
        uint8_t *dp = dst->planes[p];
        int32_t j;
        if (!(mask >> p & 1) || !PLANE_OK(dp)) continue;
        for (j = 0; j < h; j++) {
            int32_t r = up ? h - 1 - j : j, k;
            uint8_t *drow = dp + (uint32_t)(dy + r) * dst->bytes_per_row;
            const uint8_t *srow = PLANE_OK(sp) ? sp + (uint32_t)(sy + r) * src->bytes_per_row : 0;
            for (k = 0; k <= bx1 - bx0; k++) {
                int32_t bx = left ? bx1 - k : bx0 + k;
                /* the source bits under dest byte bx: x = 8 * bx - dx + sx on */
                int32_t xs = 8 * bx - dx + sx;
                uint8_t b, c = drow[bx], e = 0xFF, d;
                if (bx == bx0) e &= first;
                if (bx == bx1) e &= last;
                if (!srow) b = sp ? 0xFF : 0x00;
                else if (xs >= 0) b = bits8(srow, src->bytes_per_row, (uint32_t)xs);
                else b = (uint8_t)(bits8(srow, src->bytes_per_row, 0) >> -xs);  /* only bits right of the edge matter */
                d = (uint8_t)((b & c & m7) | (b & ~c & m6) | (~b & c & m5) | (~b & ~c & m4));
                drow[bx] = (uint8_t)((c & ~e) | (d & e));
            }
        }
    }
}

void gfx_chunky8_put(const struct gfx_chunky8 *bm, int32_t x, int32_t y, uint32_t pen, uint32_t mask, int mode)
{
    uint8_t *d = bm->base + (uint32_t)y * bm->bytes_per_row + (uint32_t)x, m = (uint8_t)mask;

    *d = mode == GFX_INVERT ? (uint8_t)(*d ^ m) : (uint8_t)((*d & ~m) | (pen & m));
}

uint32_t gfx_chunky8_get(const struct gfx_chunky8 *bm, int32_t x, int32_t y)
{
    return bm->base[(uint32_t)y * bm->bytes_per_row + (uint32_t)x];
}

void gfx_chunky8_fill_all(const struct gfx_chunky8 *bm, uint32_t pen, uint32_t mask)
{
    uint32_t y, x;
    uint8_t m = (uint8_t)mask, v = (uint8_t)(pen & mask);

    if (m == 0xFF) {
        for (y = 0; y < bm->rows; y++)
            fill_bytes(bm->base + y * bm->bytes_per_row, bm->bytes_per_row, v);
        return;
    }
    for (y = 0; y < bm->rows; y++)
        for (x = 0; x < bm->bytes_per_row; x++) {
            uint8_t *d = bm->base + y * bm->bytes_per_row + x;
            *d = (uint8_t)((*d & ~m) | v);
        }
}

void gfx_chunky8_span(const struct gfx_chunky8 *bm, int32_t x0, int32_t x1, int32_t y, uint32_t pat,
                      uint32_t fg, uint32_t bg, uint32_t mask, int mode)
{
    uint8_t *d = bm->base + (uint32_t)y * bm->bytes_per_row, m = (uint8_t)mask;
    int32_t x;

    for (x = x0; x <= x1; x++) {
        int on = pat >> (15 - (x & 15)) & 1;
        if (mode == GFX_XOR) { if (on) d[x] ^= m; }
        else if (on) d[x] = (uint8_t)((d[x] & ~m) | (fg & m));
        else if (mode == GFX_JAM2) d[x] = (uint8_t)((d[x] & ~m) | (bg & m));
    }
}

void gfx_chunky8_write8(const struct gfx_chunky8 *bm, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                        const uint8_t *src, uint32_t src_bpr, uint32_t mask)
{
    uint8_t m = (uint8_t)mask;
    int32_t x, y;

    for (y = y0; y <= y1; y++, src += src_bpr) {
        uint8_t *d = bm->base + (uint32_t)y * bm->bytes_per_row;
        if (m == 0xFF)
            for (x = x0; x <= x1; x++) d[x] = src[x - x0];
        else
            for (x = x0; x <= x1; x++) d[x] = (uint8_t)((d[x] & ~m) | (src[x - x0] & m));
    }
}

void gfx_chunky8_read8(const struct gfx_chunky8 *bm, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                       uint8_t *dst, uint32_t dst_bpr)
{
    int32_t x, y;

    for (y = y0; y <= y1; y++, dst += dst_bpr) {
        const uint8_t *s = bm->base + (uint32_t)y * bm->bytes_per_row;
        for (x = x0; x <= x1; x++) dst[x - x0] = s[x];
    }
}
