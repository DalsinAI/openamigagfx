/* SPDX-License-Identifier: MIT */
/* OpenGfx's leaves (ogfx_leaves.h): the reference C, behind ac_magic.h's
 * tags. AC090 runs its host versions instead (amigachrome's jit_magic.c);
 * everything else runs this. Build it as ac_helpers.sh builds the helpers:
 * GCC, 32-bit ints, arguments on the stack (not -mregparm), with -fno-lto,
 * -fno-builtin and -fno-tree-loop-distribute-patterns, so no loop here
 * becomes a call to a C library this code may not have.
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include "ogfx_leaves.h"
#include "ac_magic.h"   /* a copy of amigachrome-guest common/amiga/ac_magic.h */

/* Bytes and longs a row at a time: a 68020 or 68040 takes four bytes in
 * one access once the pointer is even (the 68000 needs that too). The
 * longs alias the bytes, which C allows only through may_alias. */
typedef uint32_t __attribute__((may_alias)) ogfx_u32a;
static void ogfx_set(uint8_t *p, uint32_t n, uint8_t v)
{
    if (n >= 8) {
        uint32_t w = v * 0x01010101u;
        while ((uintptr_t)p & 3) { *p++ = v; n--; }
        for (; n >= 4; n -= 4, p += 4) *(ogfx_u32a *)p = w;
    }
    while (n--) *p++ = v;
}
static void ogfx_xor(uint8_t *p, uint32_t n, uint8_t v)
{
    if (n >= 8) {
        uint32_t w = v * 0x01010101u;
        while ((uintptr_t)p & 3) { *p++ ^= v; n--; }
        for (; n >= 4; n -= 4, p += 4) *(ogfx_u32a *)p ^= w;
    }
    while (n--) *p++ ^= v;
}
static void ogfx_move(uint8_t *d, const uint8_t *s, uint32_t n)
{
    if (d == s || !n) return;
    if (d < s) {
        if (n >= 8 && !(((uintptr_t)d ^ (uintptr_t)s) & 3)) {   /* both reach a long boundary together */
            while ((uintptr_t)d & 3) { *d++ = *s++; n--; }
            for (; n >= 4; n -= 4, d += 4, s += 4) *(ogfx_u32a *)d = *(const ogfx_u32a *)s;
        }
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        if (n >= 8 && !(((uintptr_t)d ^ (uintptr_t)s) & 3)) {
            while ((uintptr_t)d & 3) { *--d = *--s; n--; }
            for (; n >= 4; n -= 4) { d -= 4; s -= 4; *(ogfx_u32a *)d = *(const ogfx_u32a *)s; }
        }
        while (n--) *--d = *--s;
    }
}

AC_MAGIC(ogfx_planar_rect, AC_MAGIC_OGFX_PLANAR_RECT)
AC_MAGIC_KEEP uint32_t AC_MAGIC_BODY(ogfx_planar_rect)(const struct ogfx_planar_rect *r)
{
    uint32_t planes = r->planes, bpr = r->bytes_per_row, depth = r->depth, pen = r->pen, mask = r->mask, mode = r->mode;
    int32_t x0 = r->x0, y0 = r->y0, x1 = r->x1, y1 = r->y1;
    uint32_t left, right, inner, p, n = 0;
    uint8_t lmask, rmask;
    int32_t y;
    if (depth > 8 || x0 < 0 || y0 < 0 || x1 < x0 || y1 < y0) return 0;
    left = (uint32_t)x0 >> 3; right = (uint32_t)x1 >> 3;
    lmask = (uint8_t)(0xff >> (x0 & 7));
    rmask = (uint8_t)(0xff << (7 - (x1 & 7)));
    if (left == right) lmask &= rmask;
    inner = right > left ? right - left - 1 : 0;
    for (p = 0; p < depth; p++) {
        uint8_t *pl;
        if (!(mask >> p & 1)) continue;
        pl = ((uint8_t *const *)(uintptr_t)planes)[p];
        if (!pl || pl == (uint8_t *)(uintptr_t)-1) continue;
        n++;
        for (y = y0; y <= y1; y++) {
            uint8_t *row = pl + (uint32_t)y * bpr + left;
            if (mode & 1) {
                row[0] ^= lmask;
                if (right > left) { ogfx_xor(row + 1, inner, 0xff); row[right - left] ^= rmask; }
            } else if (pen >> p & 1) {
                row[0] |= lmask;
                if (right > left) { ogfx_set(row + 1, inner, 0xff); row[right - left] |= rmask; }
            } else {
                row[0] &= (uint8_t)~lmask;
                if (right > left) { ogfx_set(row + 1, inner, 0); row[right - left] &= (uint8_t)~rmask; }
            }
        }
    }
    return n;
}

AC_MAGIC(ogfx_chunky_rect, AC_MAGIC_OGFX_CHUNKY_RECT)
AC_MAGIC_KEEP uint32_t AC_MAGIC_BODY(ogfx_chunky_rect)(const struct ogfx_chunky_rect *r)
{
    uint32_t base = r->base, bpr = r->bytes_per_row, bpp = r->bytes_per_pixel, color = r->color, mode = r->mode;
    int32_t x0 = r->x0, y0 = r->y0, x1 = r->x1, y1 = r->y1;
    uint32_t w, k;
    uint8_t c[4];
    int32_t y;
    if (bpp < 1 || bpp > 4 || x0 < 0 || y0 < 0 || x1 < x0 || y1 < y0) return 0;
    w = (uint32_t)(x1 - x0) + 1;
    for (k = 0; k < bpp; k++) c[k] = (uint8_t)(color >> (8 * (bpp - 1 - k)));
    for (y = y0; y <= y1; y++) {
        uint8_t *q = (uint8_t *)(uintptr_t)(base + (uint32_t)y * bpr + (uint32_t)x0 * bpp);
        uint32_t x;
        if (bpp == 1) { if (mode & 1) ogfx_xor(q, w, c[0]); else ogfx_set(q, w, c[0]); continue; }
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        if (bpp == 4 && !((uintptr_t)q & 1)) {      /* a long a pixel, in the Amiga's own order */
            ogfx_u32a *l = (ogfx_u32a *)q;
            if (mode & 1) for (x = 0; x < w; x++) l[x] ^= color;
            else for (x = 0; x < w; x++) l[x] = color;
            continue;
        }
#endif
        for (x = 0; x < w; x++)
            for (k = 0; k < bpp; k++, q++) { if (mode & 1) *q ^= c[k]; else *q = c[k]; }
    }
    return w * ((uint32_t)(y1 - y0) + 1);
}

AC_MAGIC(ogfx_chunky_copy, AC_MAGIC_OGFX_CHUNKY_COPY)
AC_MAGIC_KEEP uint32_t AC_MAGIC_BODY(ogfx_chunky_copy)(const struct ogfx_chunky_copy *r)
{
    uint32_t src = r->src, sbpr = r->src_bytes_per_row, dst = r->dst, dbpr = r->dst_bytes_per_row, bpp = r->bytes_per_pixel;
    int32_t sx = r->sx, sy = r->sy, dx = r->dx, dy = r->dy, w = r->width, h = r->height, y;
    uint32_t rowbytes, s0, d0;
    if (bpp < 1 || bpp > 4 || sx < 0 || sy < 0 || dx < 0 || dy < 0 || w <= 0 || h <= 0) return 0;
    rowbytes = (uint32_t)w * bpp;
    s0 = src + (uint32_t)sy * sbpr + (uint32_t)sx * bpp;
    d0 = dst + (uint32_t)dy * dbpr + (uint32_t)dx * bpp;
    if (d0 > s0)
        for (y = h - 1; y >= 0; y--)
            ogfx_move((uint8_t *)(uintptr_t)(d0 + (uint32_t)y * dbpr), (const uint8_t *)(uintptr_t)(s0 + (uint32_t)y * sbpr), rowbytes);
    else
        for (y = 0; y < h; y++)
            ogfx_move((uint8_t *)(uintptr_t)(d0 + (uint32_t)y * dbpr), (const uint8_t *)(uintptr_t)(s0 + (uint32_t)y * sbpr), rowbytes);
    return (uint32_t)w * (uint32_t)h;
}


static uint32_t ogfx_minterm_bit(uint32_t minterm, uint32_t s, uint32_t d)
{
    uint32_t bit = 4u + (s ? 2u : 0u) + (d ? 1u : 0u);
    return (minterm >> bit) & 1u;
}

AC_MAGIC(ogfx_planar_blit, AC_MAGIC_OGFX_PLANAR_BLIT)
AC_MAGIC_KEEP uint32_t AC_MAGIC_BODY(ogfx_planar_blit)(const struct ogfx_planar_blit *r)
{
    uint32_t depth = r->depth, mask = r->mask, minterm = r->minterm;
    uint32_t src_planes = r->src_planes, dst_planes = r->dst_planes;
    uint32_t sbpr = r->src_bytes_per_row, dbpr = r->dst_bytes_per_row;
    int32_t sx = r->sx, sy = r->sy, dx = r->dx, dy = r->dy;
    int32_t w = r->width, h = r->height;
    int32_t y0, y1, ystep, x0, x1, xstep;
    uint32_t p, planes = 0;

    if (!depth || depth > 8 || sx < 0 || sy < 0 || dx < 0 || dy < 0 ||
        w <= 0 || h <= 0 || !mask)
        return 0;

    y0 = 0; y1 = h; ystep = 1;
    x0 = 0; x1 = w; xstep = 1;
    if (r->same_bitmap && dy > sy) {
        y0 = h - 1; y1 = -1; ystep = -1;
    } else if (r->same_bitmap && dy == sy && dx > sx) {
        x0 = w - 1; x1 = -1; xstep = -1;
    }

    for (p = 0; p < depth; ++p) {
        const uint8_t *sp;
        uint8_t *dp;
        if (!(mask >> p & 1u))
            continue;

        sp = ((const uint8_t *const *)(uintptr_t)src_planes)[p];
        dp = ((uint8_t *const *)(uintptr_t)dst_planes)[p];

        /* graphics.library treats these destination planes as constants. */
        if (!dp || dp == (uint8_t *)(uintptr_t)-1)
            continue;

        planes++;
        for (int32_t yy = y0; yy != y1; yy += ystep) {
            const uint8_t *srow = (sp && sp != (const uint8_t *)(uintptr_t)-1)
                ? sp + (uint32_t)(sy + yy) * sbpr : 0;
            uint8_t *drow = dp + (uint32_t)(dy + yy) * dbpr;
            for (int32_t xx = x0; xx != x1; xx += xstep) {
                uint32_t sxp = (uint32_t)(sx + xx), dxp = (uint32_t)(dx + xx);
                uint8_t sm = (uint8_t)(0x80u >> (sxp & 7u));
                uint8_t dm = (uint8_t)(0x80u >> (dxp & 7u));
                uint32_t sbit, dbit, out;

                if (!sp) sbit = 0;
                else if (sp == (const uint8_t *)(uintptr_t)-1) sbit = 1;
                else sbit = (srow[sxp >> 3] & sm) != 0;

                dbit = (drow[dxp >> 3] & dm) != 0;
                out = ogfx_minterm_bit(minterm, sbit, dbit);
                if (out) drow[dxp >> 3] |= dm;
                else drow[dxp >> 3] &= (uint8_t)~dm;
            }
        }
    }

    return planes;
}
