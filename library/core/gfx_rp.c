/* SPDX-License-Identifier: MIT */
/* OpenGfx's RastPort calls (gfx_rp.h). Copyright (c) 2026 Dalsin Limited. MIT. */
#include <graphics/gfx.h>
#include "gfx_rp.h"
#include "gfx_core.h"

static void planar_of(struct RastPort *rp, struct gfx_planar *bm)
{
    struct BitMap *b = rp->BitMap;
    int p;

    for (p = 0; p < 8; p++)
        bm->planes[p] = p < b->Depth ? (uint8_t *)b->Planes[p] : 0;
    bm->bytes_per_row = b->BytesPerRow;
    bm->rows = b->Rows;
    bm->depth = b->Depth > 8 ? 8 : b->Depth;
}

static int inside(struct RastPort *rp, LONG x, LONG y)
{
    return x >= 0 && y >= 0 && x < (LONG)rp->BitMap->BytesPerRow * 8 && y < (LONG)rp->BitMap->Rows;
}

/* The rectangle, cut to the bitmap; 0 when nothing is left. */
static int cut(struct RastPort *rp, ULONG *x0, ULONG *y0, ULONG *x1, ULONG *y1)
{
    ULONG w = (ULONG)rp->BitMap->BytesPerRow * 8, h = rp->BitMap->Rows;
    if (*x0 > *x1 || *y0 > *y1 || *x0 >= w || *y0 >= h) return 0;
    if (*x1 >= w) *x1 = w - 1;
    if (*y1 >= h) *y1 = h - 1;
    return 1;
}

/* Measured on OS 3.2: an unlayered RastPort is not clipped. WritePixel
 * answers 0 wherever the pixel is (OS 3.2 then writes outside the bitmap;
 * we draw nothing there), and ReadPixel reads whatever memory the pixel's
 * address falls on, as OS 3.2 does. Under INVERSVID the pixel is the
 * background: JAM2 draws BgPen (or inverts, with COMPLEMENT), and without
 * JAM2 the pixel is left as it is. */
LONG ogfx_rp_WritePixel(struct RastPort *rp, LONG x, LONG y)
{
    struct gfx_planar bm;
    UBYTE mode = rp->DrawMode;

    if (!inside(rp, x, y) || (mode & (INVERSVID | JAM2)) == INVERSVID) return 0;
    planar_of(rp, &bm);
    if (mode & COMPLEMENT)
        gfx_planar_put(&bm, x, y, 0, rp->Mask, GFX_INVERT);
    else
        gfx_planar_put(&bm, x, y, mode & INVERSVID ? rp->BgPen : rp->FgPen, rp->Mask, GFX_SET);
    return 0;
}

LONG ogfx_rp_ReadPixel(struct RastPort *rp, LONG x, LONG y)
{
    struct BitMap *b = rp->BitMap;
    LONG off = y * (LONG)b->BytesPerRow + (x >> 3);
    UBYTE bit = (UBYTE)(0x80 >> (x & 7));
    ULONG pen = 0;
    int p;

    for (p = 0; p < b->Depth && p < 8; p++) {
        UBYTE *pl = (UBYTE *)b->Planes[p];
        if (pl == (UBYTE *)-1) pen |= 1UL << p;
        else if (pl && (pl[off] & bit)) pen |= 1UL << p;
    }
    return (LONG)pen;
}

void ogfx_rp_SetRast(struct RastPort *rp, ULONG pen)
{
    struct gfx_planar bm;

    planar_of(rp, &bm);
    gfx_planar_fill_all(&bm, pen, rp->Mask);
}

/* WritePixelArray8's and ReadPixelArray8's array rows are the width
 * rounded up to 16 bytes. */
#define ROW16(x0, x1) ((((x1) - (x0) + 1) + 15) & ~15UL)

LONG ogfx_rp_WritePixelArray8(struct RastPort *rp, ULONG xstart, ULONG ystart, ULONG xstop, ULONG ystop,
                              UBYTE *array, struct RastPort *temprp)
{
    ULONG bpr = ROW16(xstart, xstop), x1 = xstop, y1 = ystop;
    struct gfx_planar bm;

    (void)temprp;
    if (!cut(rp, &xstart, &ystart, &x1, &y1)) return 0;
    planar_of(rp, &bm);
    gfx_planar_write8(&bm, xstart, ystart, x1, y1, array, bpr, rp->Mask);
    return (LONG)((x1 - xstart + 1) * (y1 - ystart + 1));
}

LONG ogfx_rp_ReadPixelArray8(struct RastPort *rp, ULONG xstart, ULONG ystart, ULONG xstop, ULONG ystop,
                             UBYTE *array, struct RastPort *temprp)
{
    ULONG bpr = ROW16(xstart, xstop), x1 = xstop, y1 = ystop;
    struct gfx_planar bm;

    (void)temprp;
    if (!cut(rp, &xstart, &ystart, &x1, &y1)) return 0;
    planar_of(rp, &bm);
    gfx_planar_read8(&bm, xstart, ystart, x1, y1, array, bpr);
    /* each row's padding to 16 is 0, as OS 3.2 leaves it with a clean
     * temporary RastPort (it copies whatever the temporary row holds) */
    {
        ULONG y, x, w = x1 - xstart + 1;
        for (y = 0; y <= y1 - ystart; y++)
            for (x = w; x < bpr; x++) array[y * bpr + x] = 0;
    }
    return (LONG)((x1 - xstart + 1) * (y1 - ystart + 1));
}

void ogfx_rp_WriteChunkyPixels(struct RastPort *rp, ULONG xstart, ULONG ystart, ULONG xstop, ULONG ystop,
                               UBYTE *array, LONG bytesperrow)
{
    ULONG x1 = xstop, y1 = ystop;
    struct gfx_planar bm;

    if (!cut(rp, &xstart, &ystart, &x1, &y1)) return;
    planar_of(rp, &bm);
    gfx_planar_write8(&bm, xstart, ystart, x1, y1, array, (uint32_t)bytesperrow, rp->Mask);
}

/* What a pattern bit of 1 and of 0 do in the RastPort's DrawMode: INVERSVID
 * swaps them, JAM2 draws BgPen for a 0, COMPLEMENT inverts for a 1.
 * Measured on OS 3.2: COMPLEMENT with JAM2 inverts every pixel, 1 or 0,
 * for lines and fills alike. */
static int pat_mode(struct RastPort *rp, uint32_t *pat)
{
    UBYTE mode = rp->DrawMode;

    if (mode & INVERSVID) *pat = ~*pat & 0xFFFF;
    if (mode & COMPLEMENT) {
        if (mode & JAM2) *pat = 0xFFFF;
        return GFX_XOR;
    }
    return mode & JAM2 ? GFX_JAM2 : GFX_JAM1;
}

void ogfx_rp_Move(struct RastPort *rp, LONG x, LONG y)
{
    rp->cp_x = (WORD)x;
    rp->cp_y = (WORD)y;
    rp->Flags |= FRST_DOT;
    rp->linpatcnt = 15;
}

/* A line from the pen position to (x, y), Bresenham along the longer axis,
 * every pixel on it drawn through LinePtrn: pixel i takes bit
 * (linpatcnt - i) & 15, and linpatcnt goes down by the line's length (a
 * BYTE, not kept to 0-15: measured on OS 3.2). The first pixel is left out
 * when FRST_DOT is clear (it is the last one of the line before), so a
 * COMPLEMENT line drawn on from another does not invert their corner
 * twice. */
static void line(struct RastPort *rp, LONG x, LONG y, int first)
{
    struct gfx_planar bm;
    WORD x0 = rp->cp_x, y0 = rp->cp_y, x1 = (WORD)x, y1 = (WORD)y;
    LONG dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    LONG sx = x1 >= x0 ? 1 : -1, sy = y1 >= y0 ? 1 : -1;
    LONG len = dx > dy ? dx : dy, mn = dx > dy ? dy : dx, err = 2 * mn - len, i;
    LONG px = x0, py = y0;
    uint32_t ptrn = rp->LinePtrn;
    int cnt = rp->linpatcnt, mode = pat_mode(rp, &ptrn);

    planar_of(rp, &bm);
    for (i = 0; i <= len; i++) {
        uint32_t on = ptrn >> ((cnt - i) & 15) & 1;
        if ((i || first) && inside(rp, px, py)) {
            if (mode == GFX_XOR) { if (on) gfx_planar_put(&bm, px, py, 0, rp->Mask, GFX_INVERT); }
            else if (on) gfx_planar_put(&bm, px, py, rp->FgPen, rp->Mask, GFX_SET);
            else if (mode == GFX_JAM2) gfx_planar_put(&bm, px, py, rp->BgPen, rp->Mask, GFX_SET);
        }
        if (err >= 0) {
            if (dx > dy) py += sy; else px += sx;
            err -= 2 * len;
        }
        err += 2 * mn;
        if (dx > dy) px += sx; else py += sy;
    }
    rp->linpatcnt = (BYTE)(cnt - len);
    rp->Flags &= ~FRST_DOT;
    rp->cp_x = x1;
    rp->cp_y = y1;
}

void ogfx_rp_Draw(struct RastPort *rp, LONG x, LONG y)
{
    line(rp, x, y, (rp->Flags & FRST_DOT) != 0);
}

/* Measured on OS 3.2: PolyDraw's first line keeps to FRST_DOT as Draw
 * does. Every later line draws its first pixel too, except in COMPLEMENT
 * without JAM2: so a COMPLEMENT polyline's corners are inverted once, and
 * with JAM2 (every pixel inverted) twice. */
void ogfx_rp_PolyDraw(struct RastPort *rp, LONG count, const WORD *xy)
{
    int again = (rp->DrawMode & (COMPLEMENT | JAM2)) != COMPLEMENT;
    LONG i;

    for (i = 0; i < count; i++, xy += 2) line(rp, xy[0], xy[1], i ? again : (rp->Flags & FRST_DOT) != 0);
}

/* The area pattern's row for line y: AreaPtrn holds 1 << AreaPtSz rows,
 * repeating down the bitmap; none is solid. Multicolour patterns (a
 * negative AreaPtSz) come later and draw solid for now. */
static uint32_t area_row(struct RastPort *rp, LONG y)
{
    if (!rp->AreaPtrn || rp->AreaPtSz < 0) return 0xFFFF;
    return rp->AreaPtrn[y & ((1L << rp->AreaPtSz) - 1)];
}

void ogfx_rp_RectFill(struct RastPort *rp, LONG xmin, LONG ymin, LONG xmax, LONG ymax)
{
    ogfx_rp_BltPattern(rp, NULL, xmin, ymin, xmax, ymax, 0);
}

/* The rectangle through the area pattern and, when there is one, a
 * one-plane mask of bytecnt bytes a row whose bit for (x - xl, y - yl)
 * says whether pixel (x, y) is drawn at all. Measured on OS 3.2: with a
 * mask, each row is drawn on to the end of the 16-pixel word that holds
 * maxx, wherever the mask's bits (read on past bytecnt if need be) are
 * set; a mask as wide as the rectangle with its padding clear draws only
 * the rectangle. */
void ogfx_rp_BltPattern(struct RastPort *rp, const UBYTE *mask, LONG xl, LONG yl, LONG maxx, LONG maxy,
                        ULONG bytecnt)
{
    struct gfx_planar bm;
    ULONG x0 = (ULONG)(WORD)xl, y0 = (ULONG)(WORD)yl, x1 = (ULONG)(WORD)maxx, y1 = (ULONG)(WORD)maxy, y;

    if ((WORD)xl < 0 || (WORD)yl < 0 || !cut(rp, &x0, &y0, &x1, &y1)) return;
    planar_of(rp, &bm);
    for (y = y0; y <= y1; y++) {
        uint32_t pat = area_row(rp, (LONG)y);
        int mode = pat_mode(rp, &pat);
        if (!mask) {
            gfx_planar_span(&bm, x0, x1, y, pat, rp->FgPen, rp->BgPen, rp->Mask, mode);
            continue;
        }
        {
            const UBYTE *m = mask + (y - y0) * bytecnt;
            ULONG x = x0, run, xe = x1 | 15, w = (ULONG)rp->BitMap->BytesPerRow * 8;
            if (xe >= w) xe = w - 1;
            while (x <= xe) {
                ULONG i = x - x0;
                if (!(m[i >> 3] & (0x80 >> (i & 7)))) { x++; continue; }
                for (run = x; run + 1 <= xe && (m[(run + 1 - x0) >> 3] & (0x80 >> ((run + 1 - x0) & 7))); run++) ;
                gfx_planar_span(&bm, x, run, y, pat, rp->FgPen, rp->BgPen, rp->Mask, mode);
                x = run + 1;
            }
        }
    }
}
