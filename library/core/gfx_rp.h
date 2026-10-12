/* SPDX-License-Identifier: MIT */
/* OpenGfx's RastPort calls: graphics.library's arguments and results, on a
 * RastPort with no layer, through the core (gfx_core.h). Copyright (c) 2026
 * Dalsin Limited. MIT.
 *
 * Each reads the RastPort's pens, DrawMode and Mask and answers as
 * graphics.library does (measured on OS 3.2 by tests/GfxCoreABITest.c).
 * OS 3.2 does not clip an unlayered RastPort; these never write outside
 * the bitmap. A layered RastPort is the library's
 * to cut into its layer's rectangles first; these take only unlayered ones.
 * Planar bitmaps only for now; chunky (RTG) comes with the bitmap model. */
#ifndef OPENGFX_RP_H
#define OPENGFX_RP_H

#include <exec/types.h>
#include <graphics/rastport.h>

LONG ogfx_rp_WritePixel(struct RastPort *rp, LONG x, LONG y);
LONG ogfx_rp_ReadPixel(struct RastPort *rp, LONG x, LONG y);
void ogfx_rp_SetRast(struct RastPort *rp, ULONG pen);
LONG ogfx_rp_WritePixelArray8(struct RastPort *rp, ULONG xstart, ULONG ystart, ULONG xstop, ULONG ystop,
                              UBYTE *array, struct RastPort *temprp);
LONG ogfx_rp_ReadPixelArray8(struct RastPort *rp, ULONG xstart, ULONG ystart, ULONG xstop, ULONG ystop,
                             UBYTE *array, struct RastPort *temprp);
void ogfx_rp_WriteChunkyPixels(struct RastPort *rp, ULONG xstart, ULONG ystart, ULONG xstop, ULONG ystop,
                               UBYTE *array, LONG bytesperrow);

/* Lines and fills. Coordinates are graphics.library's WORDs. */
void ogfx_rp_Move(struct RastPort *rp, LONG x, LONG y);
void ogfx_rp_Draw(struct RastPort *rp, LONG x, LONG y);
void ogfx_rp_PolyDraw(struct RastPort *rp, LONG count, const WORD *xy);
void ogfx_rp_RectFill(struct RastPort *rp, LONG xmin, LONG ymin, LONG xmax, LONG ymax);
void ogfx_rp_BltPattern(struct RastPort *rp, const UBYTE *mask, LONG xl, LONG yl, LONG maxx, LONG maxy,
                        ULONG bytecnt);

#endif
