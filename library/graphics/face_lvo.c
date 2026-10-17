/* SPDX-License-Identifier: MIT */
/* OpenGfx graphics.library: the face's LVOs in their registers.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Each gfx_lvo_<Call> takes the fd's arguments in the registers
 * graphics.lvo lists (check_regs.py compares them), with A6 the library
 * base, and calls gfx_face.cpp. Built by the Amiga compiler, whose
 * register parameters GFX_REG names; elsewhere GFX_REG is a plain
 * parameter, for the tests' compile check. OWNERS.md says which LVOs are
 * the face's; the private slots answer 0. */
#include "gfx_face.h"

/* Arguments NDK 3.2's protos give as WORD, UWORD, BYTE, UBYTE or BOOL are
 * cast to that width where they are handed on: a caller built with those
 * inlines sets only the low part of the register (measured: OpenIntuition's
 * WhichLayer call left d0's upper word as it was). */
#ifndef GFX_REG
#define GFX_REG(r, decl) decl
#endif

int32_t gfx_lvo_BltBitMap(GFX_REG(a0, struct gfx_bitmap *src),
    GFX_REG(d0, int32_t sx),
    GFX_REG(d1, int32_t sy),
    GFX_REG(a1, struct gfx_bitmap *dst),
    GFX_REG(d2, int32_t dx),
    GFX_REG(d3, int32_t dy),
    GFX_REG(d4, int32_t w),
    GFX_REG(d5, int32_t h),
    GFX_REG(d6, uint32_t minterm),
    GFX_REG(d7, uint32_t mask),
    GFX_REG(a2, uint8_t *temp),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_BltBitMap(gfx_env_of(gb), src, (int16_t)sx, (int16_t)sy, dst, (int16_t)dx, (int16_t)dy, (int16_t)w, (int16_t)h, (uint8_t)minterm, (uint8_t)mask, temp);
}

void gfx_lvo_DrawEllipse(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d0, int32_t xc),
    GFX_REG(d1, int32_t yc),
    GFX_REG(d2, int32_t a),
    GFX_REG(d3, int32_t b),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_DrawEllipse(gfx_env_of(gb), rp, (int16_t)xc, (int16_t)yc, (int16_t)a, (int16_t)b);
}

int32_t gfx_lvo_AreaEllipse(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d0, int32_t xc),
    GFX_REG(d1, int32_t yc),
    GFX_REG(d2, int32_t a),
    GFX_REG(d3, int32_t b),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_AreaEllipse(gfx_env_of(gb), rp, (int16_t)xc, (int16_t)yc, (int16_t)a, (int16_t)b);
}

void gfx_lvo_LoadRGB4(GFX_REG(a0, void *vp),
    GFX_REG(a1, const uint16_t *colors),
    GFX_REG(d0, int32_t count),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_LoadRGB4(gfx_env_of(gb), vp, colors, (int16_t)count);
}

void gfx_lvo_InitRastPort(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_InitRastPort(gfx_env_of(gb), rp);
}

void gfx_lvo_SetRast(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d0, uint32_t pen),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_SetRast(gfx_env_of(gb), rp, (uint8_t)pen);
}

void gfx_lvo_Move(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d0, int32_t x),
    GFX_REG(d1, int32_t y),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_Move(gfx_env_of(gb), rp, (int16_t)x, (int16_t)y);
}

void gfx_lvo_Draw(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d0, int32_t x),
    GFX_REG(d1, int32_t y),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_Draw(gfx_env_of(gb), rp, (int16_t)x, (int16_t)y);
}

int32_t gfx_lvo_AreaMove(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d0, int32_t x),
    GFX_REG(d1, int32_t y),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_AreaMove(gfx_env_of(gb), rp, (int16_t)x, (int16_t)y);
}

int32_t gfx_lvo_AreaDraw(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d0, int32_t x),
    GFX_REG(d1, int32_t y),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_AreaDraw(gfx_env_of(gb), rp, (int16_t)x, (int16_t)y);
}

int32_t gfx_lvo_AreaEnd(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_AreaEnd(gfx_env_of(gb), rp);
}

void gfx_lvo_InitArea(GFX_REG(a0, struct gfx_areainfo *ai),
    GFX_REG(a1, void *buffer),
    GFX_REG(d0, int32_t maxvectors),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_InitArea(gfx_env_of(gb), ai, buffer, (int16_t)maxvectors);
}

void gfx_lvo_SetRGB4(GFX_REG(a0, void *vp),
    GFX_REG(d0, int32_t index),
    GFX_REG(d1, uint32_t r),
    GFX_REG(d2, uint32_t g),
    GFX_REG(d3, uint32_t b),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_SetRGB4(gfx_env_of(gb), vp, (int16_t)index, (uint8_t)r, (uint8_t)g, (uint8_t)b);
}

void gfx_lvo_RectFill(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d0, int32_t x0),
    GFX_REG(d1, int32_t y0),
    GFX_REG(d2, int32_t x1),
    GFX_REG(d3, int32_t y1),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_RectFill(gfx_env_of(gb), rp, (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1);
}

void gfx_lvo_BltPattern(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(a0, const uint8_t *mask),
    GFX_REG(d0, int32_t x0),
    GFX_REG(d1, int32_t y0),
    GFX_REG(d2, int32_t x1),
    GFX_REG(d3, int32_t y1),
    GFX_REG(d4, uint32_t maskbpr),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_BltPattern(gfx_env_of(gb), rp, mask, (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, (uint16_t)maskbpr);
}

int32_t gfx_lvo_ReadPixel(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d0, int32_t x),
    GFX_REG(d1, int32_t y),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_ReadPixel(gfx_env_of(gb), rp, (int16_t)x, (int16_t)y);
}

int32_t gfx_lvo_WritePixel(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d0, int32_t x),
    GFX_REG(d1, int32_t y),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_WritePixel(gfx_env_of(gb), rp, (int16_t)x, (int16_t)y);
}

int32_t gfx_lvo_Flood(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d2, uint32_t mode),
    GFX_REG(d0, int32_t x),
    GFX_REG(d1, int32_t y),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_Flood(gfx_env_of(gb), rp, mode, (int16_t)x, (int16_t)y);
}

void gfx_lvo_PolyDraw(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d0, int32_t count),
    GFX_REG(a0, const int16_t *xy),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_PolyDraw(gfx_env_of(gb), rp, (int16_t)count, xy);
}

void gfx_lvo_SetAPen(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d0, uint32_t pen),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_SetAPen(gfx_env_of(gb), rp, (uint8_t)pen);
}

void gfx_lvo_SetBPen(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d0, uint32_t pen),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_SetBPen(gfx_env_of(gb), rp, (uint8_t)pen);
}

void gfx_lvo_SetDrMd(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d0, uint32_t mode),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_SetDrMd(gfx_env_of(gb), rp, (uint8_t)mode);
}

void gfx_lvo_InitBitMap(GFX_REG(a0, struct gfx_bitmap *bm),
    GFX_REG(d0, int32_t depth),
    GFX_REG(d1, int32_t width),
    GFX_REG(d2, int32_t height),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_InitBitMap(gfx_env_of(gb), bm, (int8_t)depth, (int16_t)width, (int16_t)height);
}

void gfx_lvo_ScrollRaster(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d0, int32_t dx),
    GFX_REG(d1, int32_t dy),
    GFX_REG(d2, int32_t x0),
    GFX_REG(d3, int32_t y0),
    GFX_REG(d4, int32_t x1),
    GFX_REG(d5, int32_t y1),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_ScrollRaster(gfx_env_of(gb), rp, (int16_t)dx, (int16_t)dy, (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1);
}

/* LockLayerRom, UnlockLayerRom and AttemptLockLayerRom take the layer in
 * A5, which m68k-amigaos GCC keeps for its frame pointer. With
 * -fomit-frame-pointer, GCC 16 reads an A5 register parameter as the
 * eliminated frame's address: these three got an address on their caller's
 * stack in place of the layer, and locked a "semaphore" there (OS 3.2's
 * workbench.library then waited for good). So on the Amiga each is a stub
 * in assembly that passes A5 and A6 on the stack to a C body. The register
 * definitions stay for the other builds, and for check_regs.py. */
#if defined(__amigaos__) || defined(AMIGA)
void gfx_a5_LockLayerRom(struct gfx_layer *layer, struct gfx_base *gb);
void gfx_a5_UnlockLayerRom(struct gfx_layer *layer, struct gfx_base *gb);
int32_t gfx_a5_AttemptLockLayerRom(struct gfx_layer *layer, struct gfx_base *gb);

void gfx_a5_LockLayerRom(struct gfx_layer *layer, struct gfx_base *gb)
{
    gfx_LockLayerRom(gfx_env_of(gb), layer);
}

void gfx_a5_UnlockLayerRom(struct gfx_layer *layer, struct gfx_base *gb)
{
    gfx_UnlockLayerRom(gfx_env_of(gb), layer);
}

int32_t gfx_a5_AttemptLockLayerRom(struct gfx_layer *layer, struct gfx_base *gb)
{
    return gfx_AttemptLockLayerRom(gfx_env_of(gb), layer);
}

#define GFX_A5_STUB(call) \
    "\t.globl\t_gfx_lvo_" call "\n" \
    "_gfx_lvo_" call ":\n" \
    "\tmove.l\ta6,-(sp)\n" \
    "\tmove.l\ta5,-(sp)\n" \
    "\tjsr\t_gfx_a5_" call "\n" \
    "\taddq.l\t#8,sp\n" \
    "\trts\n"
__asm__("\t.text\n\t.even\n"
        GFX_A5_STUB("LockLayerRom")
        GFX_A5_STUB("UnlockLayerRom")
        GFX_A5_STUB("AttemptLockLayerRom"));
#else
void gfx_lvo_LockLayerRom(GFX_REG(a5, struct gfx_layer *layer),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_LockLayerRom(gfx_env_of(gb), layer);
}

void gfx_lvo_UnlockLayerRom(GFX_REG(a5, struct gfx_layer *layer),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_UnlockLayerRom(gfx_env_of(gb), layer);
}

int32_t gfx_lvo_AttemptLockLayerRom(GFX_REG(a5, struct gfx_layer *layer),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_AttemptLockLayerRom(gfx_env_of(gb), layer);
}
#endif

void gfx_lvo_SyncSBitMap(GFX_REG(a0, struct gfx_layer *layer),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_SyncSBitMap(gfx_env_of(gb), layer);
}

void gfx_lvo_CopySBitMap(GFX_REG(a0, struct gfx_layer *layer),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_CopySBitMap(gfx_env_of(gb), layer);
}

struct gfx_tmpras *gfx_lvo_InitTmpRas(GFX_REG(a0, struct gfx_tmpras *tr),
    GFX_REG(a1, uint8_t *buffer),
    GFX_REG(d0, int32_t size),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_InitTmpRas(gfx_env_of(gb), tr, buffer, size);
}

uint8_t *gfx_lvo_AllocRaster(GFX_REG(d0, uint32_t width),
    GFX_REG(d1, uint32_t height),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_AllocRaster(gfx_env_of(gb), (uint16_t)width, (uint16_t)height);
}

void gfx_lvo_FreeRaster(GFX_REG(a0, uint8_t *p),
    GFX_REG(d0, uint32_t width),
    GFX_REG(d1, uint32_t height),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_FreeRaster(gfx_env_of(gb), p, (uint16_t)width, (uint16_t)height);
}

void gfx_lvo_AndRectRegion(GFX_REG(a0, struct gfx_rgn *region),
    GFX_REG(a1, const struct gfx_rectangle *rect),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_AndRectRegion(gfx_env_of(gb), region, rect);
}

int32_t gfx_lvo_OrRectRegion(GFX_REG(a0, struct gfx_rgn *region),
    GFX_REG(a1, const struct gfx_rectangle *rect),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_OrRectRegion(gfx_env_of(gb), region, rect);
}

struct gfx_rgn *gfx_lvo_NewRegion(GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_NewRegion(gfx_env_of(gb));
}

int32_t gfx_lvo_ClearRectRegion(GFX_REG(a0, struct gfx_rgn *region),
    GFX_REG(a1, const struct gfx_rectangle *rect),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_ClearRectRegion(gfx_env_of(gb), region, rect);
}

void gfx_lvo_ClearRegion(GFX_REG(a0, struct gfx_rgn *region),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_ClearRegion(gfx_env_of(gb), region);
}

void gfx_lvo_DisposeRegion(GFX_REG(a0, struct gfx_rgn *region),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_DisposeRegion(gfx_env_of(gb), region);
}

void gfx_lvo_ClipBlit(GFX_REG(a0, struct gfx_rastport *src),
    GFX_REG(d0, int32_t sx),
    GFX_REG(d1, int32_t sy),
    GFX_REG(a1, struct gfx_rastport *dst),
    GFX_REG(d2, int32_t dx),
    GFX_REG(d3, int32_t dy),
    GFX_REG(d4, int32_t w),
    GFX_REG(d5, int32_t h),
    GFX_REG(d6, uint32_t minterm),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_ClipBlit(gfx_env_of(gb), src, (int16_t)sx, (int16_t)sy, dst, (int16_t)dx, (int16_t)dy, (int16_t)w, (int16_t)h, (uint8_t)minterm);
}

int32_t gfx_lvo_XorRectRegion(GFX_REG(a0, struct gfx_rgn *region),
    GFX_REG(a1, const struct gfx_rectangle *rect),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_XorRectRegion(gfx_env_of(gb), region, rect);
}

struct gfx_colormap *gfx_lvo_GetColorMap(GFX_REG(d0, int32_t entries),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_GetColorMap(gfx_env_of(gb), entries);
}

void gfx_lvo_FreeColorMap(GFX_REG(a0, struct gfx_colormap *cm),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_FreeColorMap(gfx_env_of(gb), cm);
}

int32_t gfx_lvo_GetRGB4(GFX_REG(a0, struct gfx_colormap *cm),
    GFX_REG(d0, int32_t entry),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_GetRGB4(gfx_env_of(gb), cm, entry);
}

void gfx_lvo_BltBitMapRastPort(GFX_REG(a0, struct gfx_bitmap *src),
    GFX_REG(d0, int32_t sx),
    GFX_REG(d1, int32_t sy),
    GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d2, int32_t dx),
    GFX_REG(d3, int32_t dy),
    GFX_REG(d4, int32_t w),
    GFX_REG(d5, int32_t h),
    GFX_REG(d6, uint32_t minterm),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_BltBitMapRastPort(gfx_env_of(gb), src, (int16_t)sx, (int16_t)sy, rp, (int16_t)dx, (int16_t)dy, (int16_t)w, (int16_t)h, (uint8_t)minterm);
}

int32_t gfx_lvo_OrRegionRegion(GFX_REG(a0, const struct gfx_rgn *src),
    GFX_REG(a1, struct gfx_rgn *dst),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_OrRegionRegion(gfx_env_of(gb), src, dst);
}

int32_t gfx_lvo_XorRegionRegion(GFX_REG(a0, const struct gfx_rgn *src),
    GFX_REG(a1, struct gfx_rgn *dst),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_XorRegionRegion(gfx_env_of(gb), src, dst);
}

int32_t gfx_lvo_AndRegionRegion(GFX_REG(a0, const struct gfx_rgn *src),
    GFX_REG(a1, struct gfx_rgn *dst),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_AndRegionRegion(gfx_env_of(gb), src, dst);
}

void gfx_lvo_SetRGB4CM(GFX_REG(a0, struct gfx_colormap *cm),
    GFX_REG(d0, int32_t index),
    GFX_REG(d1, uint32_t r),
    GFX_REG(d2, uint32_t g),
    GFX_REG(d3, uint32_t b),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_SetRGB4CM(gfx_env_of(gb), cm, (int16_t)index, (uint8_t)r, (uint8_t)g, (uint8_t)b);
}

/* private -642: a private slot of OS 3.2's; answers 0 */
uint32_t gfx_lvo_private_642(void) { return 0; }

/* private -648: a private slot of OS 3.2's; answers 0 */
uint32_t gfx_lvo_private_648(void) { return 0; }

void gfx_lvo_BitMapScale(GFX_REG(a0, struct gfx_bitscaleargs *bsa),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_BitMapScale(gfx_env_of(gb), bsa);
}

uint32_t gfx_lvo_ScalerDiv(GFX_REG(d0, uint32_t factor),
    GFX_REG(d1, uint32_t numerator),
    GFX_REG(d2, uint32_t denominator),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_ScalerDiv(gfx_env_of(gb), (uint16_t)factor, (uint16_t)numerator, (uint16_t)denominator);
}

/* private -738: a private slot of OS 3.2's; answers 0 */
uint32_t gfx_lvo_private_738(void) { return 0; }

/* private -744: a private slot of OS 3.2's; answers 0 */
uint32_t gfx_lvo_private_744(void) { return 0; }

/* private -750: a private slot of OS 3.2's; answers 0 */
uint32_t gfx_lvo_private_750(void) { return 0; }

int32_t gfx_lvo_ReadPixelLine8(GFX_REG(a0, struct gfx_rastport *rp),
    GFX_REG(d0, uint32_t x),
    GFX_REG(d1, uint32_t y),
    GFX_REG(d2, uint32_t width),
    GFX_REG(a2, uint8_t *array),
    GFX_REG(a1, struct gfx_rastport *temprp),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_ReadPixelLine8(gfx_env_of(gb), rp, (uint16_t)x, (uint16_t)y, (uint16_t)width, array, temprp);
}

int32_t gfx_lvo_WritePixelLine8(GFX_REG(a0, struct gfx_rastport *rp),
    GFX_REG(d0, uint32_t x),
    GFX_REG(d1, uint32_t y),
    GFX_REG(d2, uint32_t width),
    GFX_REG(a2, uint8_t *array),
    GFX_REG(a1, struct gfx_rastport *temprp),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_WritePixelLine8(gfx_env_of(gb), rp, (uint16_t)x, (uint16_t)y, (uint16_t)width, array, temprp);
}

int32_t gfx_lvo_ReadPixelArray8(GFX_REG(a0, struct gfx_rastport *rp),
    GFX_REG(d0, uint32_t x0),
    GFX_REG(d1, uint32_t y0),
    GFX_REG(d2, uint32_t x1),
    GFX_REG(d3, uint32_t y1),
    GFX_REG(a2, uint8_t *array),
    GFX_REG(a1, struct gfx_rastport *temprp),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_ReadPixelArray8(gfx_env_of(gb), rp, (uint16_t)x0, (uint16_t)y0, (uint16_t)x1, (uint16_t)y1, array, temprp);
}

int32_t gfx_lvo_WritePixelArray8(GFX_REG(a0, struct gfx_rastport *rp),
    GFX_REG(d0, uint32_t x0),
    GFX_REG(d1, uint32_t y0),
    GFX_REG(d2, uint32_t x1),
    GFX_REG(d3, uint32_t y1),
    GFX_REG(a2, uint8_t *array),
    GFX_REG(a1, struct gfx_rastport *temprp),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_WritePixelArray8(gfx_env_of(gb), rp, (uint16_t)x0, (uint16_t)y0, (uint16_t)x1, (uint16_t)y1, array, temprp);
}

void gfx_lvo_EraseRect(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d0, int32_t x0),
    GFX_REG(d1, int32_t y0),
    GFX_REG(d2, int32_t x1),
    GFX_REG(d3, int32_t y1),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_EraseRect(gfx_env_of(gb), rp, (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1);
}

int32_t gfx_lvo_AttachPalExtra(GFX_REG(a0, struct gfx_colormap *cm),
    GFX_REG(a1, void *vp),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_AttachPalExtra(gfx_env_of(gb), cm, vp);
}

int32_t gfx_lvo_ObtainBestPenA(GFX_REG(a0, struct gfx_colormap *cm),
    GFX_REG(d1, uint32_t r),
    GFX_REG(d2, uint32_t g),
    GFX_REG(d3, uint32_t b),
    GFX_REG(a1, const struct gfx_tagitem *tags),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_ObtainBestPenA(gfx_env_of(gb), cm, r, g, b, tags);
}

/* private -846: a private slot of OS 3.2's; answers 0 */
uint32_t gfx_lvo_private_846(void) { return 0; }

void gfx_lvo_SetRGB32(GFX_REG(a0, void *vp),
    GFX_REG(d0, uint32_t n),
    GFX_REG(d1, uint32_t r),
    GFX_REG(d2, uint32_t g),
    GFX_REG(d3, uint32_t b),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_SetRGB32(gfx_env_of(gb), vp, n, r, g, b);
}

uint32_t gfx_lvo_GetAPen(GFX_REG(a0, struct gfx_rastport *rp),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_GetAPen(gfx_env_of(gb), rp);
}

uint32_t gfx_lvo_GetBPen(GFX_REG(a0, struct gfx_rastport *rp),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_GetBPen(gfx_env_of(gb), rp);
}

uint32_t gfx_lvo_GetDrMd(GFX_REG(a0, struct gfx_rastport *rp),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_GetDrMd(gfx_env_of(gb), rp);
}

uint32_t gfx_lvo_GetOutlinePen(GFX_REG(a0, struct gfx_rastport *rp),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_GetOutlinePen(gfx_env_of(gb), rp);
}

void gfx_lvo_LoadRGB32(GFX_REG(a0, void *vp),
    GFX_REG(a1, const uint32_t *table),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_LoadRGB32(gfx_env_of(gb), vp, table);
}

void gfx_lvo_SetABPenDrMd(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d0, uint32_t apen),
    GFX_REG(d1, uint32_t bpen),
    GFX_REG(d2, uint32_t mode),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_SetABPenDrMd(gfx_env_of(gb), rp, apen, bpen, mode);
}

void gfx_lvo_GetRGB32(GFX_REG(a0, struct gfx_colormap *cm),
    GFX_REG(d0, uint32_t first),
    GFX_REG(d1, uint32_t ncolors),
    GFX_REG(a1, uint32_t *table),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_GetRGB32(gfx_env_of(gb), cm, first, ncolors, table);
}

/* private -906: a private slot of OS 3.2's; answers 0 */
uint32_t gfx_lvo_private_906(void) { return 0; }

/* private -912: a private slot of OS 3.2's; answers 0 */
uint32_t gfx_lvo_private_912(void) { return 0; }

struct gfx_bitmap *gfx_lvo_AllocBitMap(GFX_REG(d0, uint32_t width),
    GFX_REG(d1, uint32_t height),
    GFX_REG(d2, uint32_t depth),
    GFX_REG(d3, uint32_t flags),
    GFX_REG(a0, const struct gfx_bitmap *friend_bm),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_AllocBitMap(gfx_env_of(gb), width, height, depth, flags, friend_bm);
}

void gfx_lvo_FreeBitMap(GFX_REG(a0, struct gfx_bitmap *bm),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_FreeBitMap(gfx_env_of(gb), bm);
}

void gfx_lvo_ReleasePen(GFX_REG(a0, struct gfx_colormap *cm),
    GFX_REG(d0, uint32_t n),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_ReleasePen(gfx_env_of(gb), cm, n);
}

int32_t gfx_lvo_ObtainPen(GFX_REG(a0, struct gfx_colormap *cm),
    GFX_REG(d0, uint32_t n),
    GFX_REG(d1, uint32_t r),
    GFX_REG(d2, uint32_t g),
    GFX_REG(d3, uint32_t b),
    GFX_REG(d4, uint32_t flags),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_ObtainPen(gfx_env_of(gb), cm, n, r, g, b, flags);
}

uint32_t gfx_lvo_GetBitMapAttr(GFX_REG(a0, const struct gfx_bitmap *bm),
    GFX_REG(d1, uint32_t attr),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_GetBitMapAttr(gfx_env_of(gb), bm, attr);
}

uint32_t gfx_lvo_SetOutlinePen(GFX_REG(a0, struct gfx_rastport *rp),
    GFX_REG(d0, uint32_t pen),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_SetOutlinePen(gfx_env_of(gb), rp, pen);
}

uint32_t gfx_lvo_SetWriteMask(GFX_REG(a0, struct gfx_rastport *rp),
    GFX_REG(d0, uint32_t mask),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_SetWriteMask(gfx_env_of(gb), rp, mask);
}

void gfx_lvo_SetMaxPen(GFX_REG(a0, struct gfx_rastport *rp),
    GFX_REG(d0, uint32_t maxpen),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_SetMaxPen(gfx_env_of(gb), rp, maxpen);
}

void gfx_lvo_SetRGB32CM(GFX_REG(a0, struct gfx_colormap *cm),
    GFX_REG(d0, uint32_t n),
    GFX_REG(d1, uint32_t r),
    GFX_REG(d2, uint32_t g),
    GFX_REG(d3, uint32_t b),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_SetRGB32CM(gfx_env_of(gb), cm, n, r, g, b);
}

void gfx_lvo_ScrollRasterBF(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d0, int32_t dx),
    GFX_REG(d1, int32_t dy),
    GFX_REG(d2, int32_t x0),
    GFX_REG(d3, int32_t y0),
    GFX_REG(d4, int32_t x1),
    GFX_REG(d5, int32_t y1),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_ScrollRasterBF(gfx_env_of(gb), rp, (int16_t)dx, (int16_t)dy, (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1);
}

int32_t gfx_lvo_FindColor(GFX_REG(a3, struct gfx_colormap *cm),
    GFX_REG(d1, uint32_t r),
    GFX_REG(d2, uint32_t g),
    GFX_REG(d3, uint32_t b),
    GFX_REG(d4, int32_t maxcolor),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gfx_FindColor(gfx_env_of(gb), cm, r, g, b, maxcolor);
}

/* private -1014: a private slot of OS 3.2's; answers 0 */
uint32_t gfx_lvo_private_1014(void) { return 0; }

void gfx_lvo_SetRPAttrsA(GFX_REG(a0, struct gfx_rastport *rp),
    GFX_REG(a1, const struct gfx_tagitem *tags),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_SetRPAttrsA(gfx_env_of(gb), rp, tags);
}

void gfx_lvo_GetRPAttrsA(GFX_REG(a0, struct gfx_rastport *rp),
    GFX_REG(a1, const struct gfx_tagitem *tags),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_GetRPAttrsA(gfx_env_of(gb), rp, tags);
}

void gfx_lvo_WriteChunkyPixels(GFX_REG(a0, struct gfx_rastport *rp),
    GFX_REG(d0, uint32_t x0),
    GFX_REG(d1, uint32_t y0),
    GFX_REG(d2, uint32_t x1),
    GFX_REG(d3, uint32_t y1),
    GFX_REG(a2, uint8_t *array),
    GFX_REG(d4, int32_t bytesperrow),
    GFX_REG(a6, struct gfx_base *gb))
{
    gfx_WriteChunkyPixels(gfx_env_of(gb), rp, (uint16_t)x0, (uint16_t)y0, (uint16_t)x1, (uint16_t)y1, array, bytesperrow);
}
