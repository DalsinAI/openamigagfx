/* SPDX-License-Identifier: MIT */
/* OpenGfx's RastPort calls: graphics.library's arguments and results, on a
 * RastPort with no layer, through the core (gfx_core.h). Copyright (c) 2026
 * Dalsin Limited. MIT.
 *
 * Each reads the RastPort's pens, DrawMode and Mask and answers as
 * graphics.library does (measured on OS 3.2 by tests/GfxCoreABITest.c).
 * OS 3.2 does not clip an unlayered RastPort; these never write outside
 * the bitmap. On a layered RastPort they draw into each of its layer's
 * ClipRects (the screen where the layer is on top, a smart refresh
 * layer's own bitmaps where it is covered); the caller holds the layer
 * locked (LockLayerRom) and asks ogfx_rp_takes first.
 * Planar bitmaps only for now; chunky (RTG) comes with the bitmap model. */
#ifndef OPENGFX_RP_H
#define OPENGFX_RP_H

#include <exec/types.h>
#include <graphics/rastport.h>

/* Whether these draw on rp: unlayered, or in a layer that is not a super
 * bitmap layer, and then not BltPattern through a mask (masked: 1). The
 * rest is graphics.library's. */
int ogfx_rp_takes(struct RastPort *rp, int masked);

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

/* Blits. Coordinates and sizes are graphics.library's WORDs; minterm's
 * high four bits act (the A channel is all ones). BltBitMap copies between
 * standard planar bitmaps and answers with the planes it drew;
 * BltBitMapRastPort draws a bitmap into a RastPort through its Mask, into
 * the layer's places when it has one. */
LONG ogfx_rp_BltBitMap(struct BitMap *src, LONG sx, LONG sy, struct BitMap *dst, LONG dx, LONG dy,
                       LONG w, LONG h, ULONG minterm, ULONG mask, UBYTE *temp);
void ogfx_rp_BltBitMapRastPort(struct BitMap *src, LONG sx, LONG sy, struct RastPort *rp, LONG dx, LONG dy,
                               LONG w, LONG h, ULONG minterm);

/* ClipBlit and ScrollRaster: the whole rectangle moves as one, overlapping
 * or not. ScrollRaster does nothing when the shift is the rectangle's size
 * or more, and fills the space it leaves with solid BgPen through the
 * write mask, whatever the draw mode and area pattern (as OS 3.2 does).
 * They draw where ogfx_rp_takes_copy says: unlayered, or a smart refresh
 * layer with no clip region. Each returns 0, drawing nothing, for a call
 * graphics.library must make: in a layer, a rectangle not wholly inside
 * it, or a ScrollRaster shift of the rectangle's size or more. */
int ogfx_rp_takes_copy(struct RastPort *rp);
int ogfx_rp_ClipBlit(struct RastPort *srp, LONG sx, LONG sy, struct RastPort *drp, LONG dx, LONG dy,
                     LONG w, LONG h, ULONG minterm);
int ogfx_rp_ScrollRaster(struct RastPort *rp, LONG dx, LONG dy, LONG x0, LONG y0, LONG x1, LONG y1);

#endif
