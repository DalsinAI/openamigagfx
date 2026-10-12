/* SPDX-License-Identifier: MIT */
/* OpenGfx's RastPort calls: graphics.library's arguments and results, on a
 * RastPort with no layer, through the core (gfx_core.h). Copyright (c) 2026
 * Dalsin Limited. MIT.
 *
 * Each reads the RastPort's pens, DrawMode and Mask, checks the call against
 * its bitmap's bounds and answers as graphics.library does (measured on
 * OS 3.2 by tests/GfxCoreABITest.c). A layered RastPort is the library's
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

#endif
