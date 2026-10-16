/* SPDX-License-Identifier: MIT */
/* OpenGfx graphics.library, the face's calls in plain C arguments.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * gfx_face.cpp (lean C++, no Amiga headers) does the work: bitmaps and
 * rasters, RastPort state, colour maps and pen sharing, regions, layer
 * locks and super bitmaps, scaling, Flood, and every drawing call the core
 * (library/core) does, with the layer locked and what the core leaves to
 * graphics.library done here. face_lvo.c takes each call's registers and
 * comes here; the other areas may call these directly. Every call takes
 * the env first (gfx_env.h) and then the fd's arguments in the fd's order. */
#ifndef OPENGFX_GFX_FACE_H
#define OPENGFX_GFX_FACE_H

#include "gfx_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- bitmaps and rasters -------------------------------------------- */
void gfx_InitBitMap(struct gfx_env *env, struct gfx_bitmap *bm, int32_t depth, int32_t width, int32_t height);
uint8_t *gfx_AllocRaster(struct gfx_env *env, uint32_t width, uint32_t height);
void gfx_FreeRaster(struct gfx_env *env, uint8_t *p, uint32_t width, uint32_t height);
struct gfx_bitmap *gfx_AllocBitMap(struct gfx_env *env, uint32_t width, uint32_t height, uint32_t depth,
                                   uint32_t flags, const struct gfx_bitmap *friend_bm);
void gfx_FreeBitMap(struct gfx_env *env, struct gfx_bitmap *bm);
uint32_t gfx_GetBitMapAttr(struct gfx_env *env, const struct gfx_bitmap *bm, uint32_t attr);

/* BitMapScale's arguments (graphics/scale.h), 48 bytes. */
struct gfx_bitscaleargs {
    uint16_t srcx, srcy, srcwidth, srcheight;
    uint16_t xsrcfactor, ysrcfactor;
    uint16_t destx, desty, destwidth, destheight;
    uint16_t xdestfactor, ydestfactor;
    struct gfx_bitmap *srcbitmap, *destbitmap;
    uint32_t flags;
    uint16_t xdda, ydda;
    int32_t reserved1, reserved2;
};
uint32_t gfx_ScalerDiv(struct gfx_env *env, uint32_t factor, uint32_t numerator, uint32_t denominator);
void gfx_BitMapScale(struct gfx_env *env, struct gfx_bitscaleargs *bsa);

/* ---- RastPort state ---------------------------------------------------- */
void gfx_InitRastPort(struct gfx_env *env, struct gfx_rastport *rp);
struct gfx_tmpras *gfx_InitTmpRas(struct gfx_env *env, struct gfx_tmpras *tr, uint8_t *buffer, int32_t size);
void gfx_InitArea(struct gfx_env *env, struct gfx_areainfo *ai, void *buffer, int32_t maxvectors);
void gfx_SetAPen(struct gfx_env *env, struct gfx_rastport *rp, uint32_t pen);
void gfx_SetBPen(struct gfx_env *env, struct gfx_rastport *rp, uint32_t pen);
void gfx_SetDrMd(struct gfx_env *env, struct gfx_rastport *rp, uint32_t mode);
void gfx_SetABPenDrMd(struct gfx_env *env, struct gfx_rastport *rp, uint32_t apen, uint32_t bpen, uint32_t mode);
uint32_t gfx_SetOutlinePen(struct gfx_env *env, struct gfx_rastport *rp, uint32_t pen);
uint32_t gfx_SetWriteMask(struct gfx_env *env, struct gfx_rastport *rp, uint32_t mask);
void gfx_SetMaxPen(struct gfx_env *env, struct gfx_rastport *rp, uint32_t maxpen);
uint32_t gfx_GetAPen(struct gfx_env *env, const struct gfx_rastport *rp);
uint32_t gfx_GetBPen(struct gfx_env *env, const struct gfx_rastport *rp);
uint32_t gfx_GetDrMd(struct gfx_env *env, const struct gfx_rastport *rp);
uint32_t gfx_GetOutlinePen(struct gfx_env *env, const struct gfx_rastport *rp);
void gfx_SetRPAttrsA(struct gfx_env *env, struct gfx_rastport *rp, const struct gfx_tagitem *tags);
void gfx_GetRPAttrsA(struct gfx_env *env, struct gfx_rastport *rp, const struct gfx_tagitem *tags);
/* SetRPAttrs/GetRPAttrs tags (graphics/rpattr.h) */
enum {
    GFX_RPTAG_FONT = 0x80000000u, GFX_RPTAG_APEN = 0x80000002u, GFX_RPTAG_BPEN = 0x80000003u,
    GFX_RPTAG_DRMD = 0x80000004u, GFX_RPTAG_OUTLINEPEN = 0x80000005u, GFX_RPTAG_WRITEMASK = 0x80000006u,
    GFX_RPTAG_MAXPEN = 0x80000007u, GFX_RPTAG_DRAWBOUNDS = 0x80000008u
};

/* ---- colour maps and pens ---------------------------------------------- */
struct gfx_colormap *gfx_GetColorMap(struct gfx_env *env, int32_t entries);
void gfx_FreeColorMap(struct gfx_env *env, struct gfx_colormap *cm);
void gfx_SetRGB4CM(struct gfx_env *env, struct gfx_colormap *cm, int32_t index, uint32_t r, uint32_t g, uint32_t b);
void gfx_SetRGB32CM(struct gfx_env *env, struct gfx_colormap *cm, uint32_t n, uint32_t r, uint32_t g, uint32_t b);
int32_t gfx_GetRGB4(struct gfx_env *env, const struct gfx_colormap *cm, int32_t entry);
void gfx_GetRGB32(struct gfx_env *env, const struct gfx_colormap *cm, uint32_t first, uint32_t ncolors,
                  uint32_t *table);
/* vp is a struct ViewPort *: its ColorMap is at offset 4 */
void gfx_SetRGB4(struct gfx_env *env, void *vp, int32_t index, uint32_t r, uint32_t g, uint32_t b);
void gfx_SetRGB32(struct gfx_env *env, void *vp, uint32_t n, uint32_t r, uint32_t g, uint32_t b);
void gfx_LoadRGB4(struct gfx_env *env, void *vp, const uint16_t *colors, int32_t count);
void gfx_LoadRGB32(struct gfx_env *env, void *vp, const uint32_t *table);
int32_t gfx_AttachPalExtra(struct gfx_env *env, struct gfx_colormap *cm, void *vp);
int32_t gfx_ObtainPen(struct gfx_env *env, struct gfx_colormap *cm, uint32_t n, uint32_t r, uint32_t g, uint32_t b,
                      uint32_t flags);
void gfx_ReleasePen(struct gfx_env *env, struct gfx_colormap *cm, uint32_t n);
int32_t gfx_ObtainBestPenA(struct gfx_env *env, struct gfx_colormap *cm, uint32_t r, uint32_t g, uint32_t b,
                           const struct gfx_tagitem *tags);
int32_t gfx_FindColor(struct gfx_env *env, struct gfx_colormap *cm, uint32_t r, uint32_t g, uint32_t b,
                      int32_t maxcolor);
enum { GFX_PEN_EXCLUSIVE = 1, GFX_PEN_NO_SETCOLOR = 2 };
enum { GFX_OBP_PRECISION = 0x84000000u, GFX_OBP_FAILIFBAD = 0x84000001u };
enum { GFX_PRECISION_EXACT = -1, GFX_PRECISION_IMAGE = 0, GFX_PRECISION_ICON = 16, GFX_PRECISION_GUI = 32 };

/* ---- regions ------------------------------------------------------------- */
struct gfx_rgn *gfx_NewRegion(struct gfx_env *env);
void gfx_DisposeRegion(struct gfx_env *env, struct gfx_rgn *r);
void gfx_ClearRegion(struct gfx_env *env, struct gfx_rgn *r);
void gfx_AndRectRegion(struct gfx_env *env, struct gfx_rgn *r, const struct gfx_rectangle *rect);
int32_t gfx_OrRectRegion(struct gfx_env *env, struct gfx_rgn *r, const struct gfx_rectangle *rect);
int32_t gfx_XorRectRegion(struct gfx_env *env, struct gfx_rgn *r, const struct gfx_rectangle *rect);
int32_t gfx_ClearRectRegion(struct gfx_env *env, struct gfx_rgn *r, const struct gfx_rectangle *rect);
int32_t gfx_OrRegionRegion(struct gfx_env *env, const struct gfx_rgn *src, struct gfx_rgn *dst);
int32_t gfx_XorRegionRegion(struct gfx_env *env, const struct gfx_rgn *src, struct gfx_rgn *dst);
int32_t gfx_AndRegionRegion(struct gfx_env *env, const struct gfx_rgn *src, struct gfx_rgn *dst);

/* ---- layers ---------------------------------------------------------------- */
void gfx_LockLayerRom(struct gfx_env *env, struct gfx_layer *l);
void gfx_UnlockLayerRom(struct gfx_env *env, struct gfx_layer *l);
int32_t gfx_AttemptLockLayerRom(struct gfx_env *env, struct gfx_layer *l);
void gfx_SyncSBitMap(struct gfx_env *env, struct gfx_layer *l);
void gfx_CopySBitMap(struct gfx_env *env, struct gfx_layer *l);

/* ---- drawing ----------------------------------------------------------------- */
int32_t gfx_WritePixel(struct gfx_env *env, struct gfx_rastport *rp, int32_t x, int32_t y);
int32_t gfx_ReadPixel(struct gfx_env *env, struct gfx_rastport *rp, int32_t x, int32_t y);
void gfx_SetRast(struct gfx_env *env, struct gfx_rastport *rp, uint32_t pen);
void gfx_Move(struct gfx_env *env, struct gfx_rastport *rp, int32_t x, int32_t y);
void gfx_Draw(struct gfx_env *env, struct gfx_rastport *rp, int32_t x, int32_t y);
void gfx_PolyDraw(struct gfx_env *env, struct gfx_rastport *rp, int32_t count, const int16_t *xy);
void gfx_RectFill(struct gfx_env *env, struct gfx_rastport *rp, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
void gfx_BltPattern(struct gfx_env *env, struct gfx_rastport *rp, const uint8_t *mask, int32_t x0, int32_t y0,
                    int32_t x1, int32_t y1, uint32_t maskbpr);
void gfx_EraseRect(struct gfx_env *env, struct gfx_rastport *rp, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
void gfx_DrawEllipse(struct gfx_env *env, struct gfx_rastport *rp, int32_t xc, int32_t yc, int32_t a, int32_t b);
int32_t gfx_AreaMove(struct gfx_env *env, struct gfx_rastport *rp, int32_t x, int32_t y);
int32_t gfx_AreaDraw(struct gfx_env *env, struct gfx_rastport *rp, int32_t x, int32_t y);
int32_t gfx_AreaEllipse(struct gfx_env *env, struct gfx_rastport *rp, int32_t xc, int32_t yc, int32_t a, int32_t b);
int32_t gfx_AreaEnd(struct gfx_env *env, struct gfx_rastport *rp);
int32_t gfx_Flood(struct gfx_env *env, struct gfx_rastport *rp, uint32_t mode, int32_t x, int32_t y);
void gfx_ScrollRaster(struct gfx_env *env, struct gfx_rastport *rp, int32_t dx, int32_t dy, int32_t x0, int32_t y0,
                      int32_t x1, int32_t y1);
void gfx_ScrollRasterBF(struct gfx_env *env, struct gfx_rastport *rp, int32_t dx, int32_t dy, int32_t x0,
                        int32_t y0, int32_t x1, int32_t y1);
void gfx_ClipBlit(struct gfx_env *env, struct gfx_rastport *src, int32_t sx, int32_t sy, struct gfx_rastport *dst,
                  int32_t dx, int32_t dy, int32_t w, int32_t h, uint32_t minterm);
int32_t gfx_BltBitMap(struct gfx_env *env, struct gfx_bitmap *src, int32_t sx, int32_t sy, struct gfx_bitmap *dst,
                      int32_t dx, int32_t dy, int32_t w, int32_t h, uint32_t minterm, uint32_t mask, uint8_t *temp);
void gfx_BltBitMapRastPort(struct gfx_env *env, struct gfx_bitmap *src, int32_t sx, int32_t sy,
                           struct gfx_rastport *rp, int32_t dx, int32_t dy, int32_t w, int32_t h, uint32_t minterm);
int32_t gfx_WritePixelArray8(struct gfx_env *env, struct gfx_rastport *rp, uint32_t x0, uint32_t y0, uint32_t x1,
                             uint32_t y1, uint8_t *array, struct gfx_rastport *temprp);
int32_t gfx_ReadPixelArray8(struct gfx_env *env, struct gfx_rastport *rp, uint32_t x0, uint32_t y0, uint32_t x1,
                            uint32_t y1, uint8_t *array, struct gfx_rastport *temprp);
int32_t gfx_WritePixelLine8(struct gfx_env *env, struct gfx_rastport *rp, uint32_t x, uint32_t y, uint32_t width,
                            uint8_t *array, struct gfx_rastport *temprp);
int32_t gfx_ReadPixelLine8(struct gfx_env *env, struct gfx_rastport *rp, uint32_t x, uint32_t y, uint32_t width,
                           uint8_t *array, struct gfx_rastport *temprp);
void gfx_WriteChunkyPixels(struct gfx_env *env, struct gfx_rastport *rp, uint32_t x0, uint32_t y0, uint32_t x1,
                           uint32_t y1, uint8_t *array, int32_t bytesperrow);

/* What a hook in a layer's BackFill gets in A1 (layers.library's). */
struct gfx_backfillmsg {
    struct gfx_layer *layer;
    struct gfx_rectangle bounds;
    int32_t offsetx, offsety;
};

#ifdef __cplusplus
}
#endif
#endif
