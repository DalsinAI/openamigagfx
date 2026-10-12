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

LONG ogfx_rp_WritePixel(struct RastPort *rp, LONG x, LONG y)
{
    struct gfx_planar bm;

    if (!inside(rp, x, y)) return -1;
    planar_of(rp, &bm);
    gfx_planar_put(&bm, x, y, rp->FgPen, rp->Mask, rp->DrawMode & COMPLEMENT ? GFX_INVERT : GFX_SET);
    return 0;
}

LONG ogfx_rp_ReadPixel(struct RastPort *rp, LONG x, LONG y)
{
    struct gfx_planar bm;

    if (!inside(rp, x, y)) return -1;
    planar_of(rp, &bm);
    return (LONG)gfx_planar_get(&bm, x, y);
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
