/* SPDX-License-Identifier: MIT */
/* OpenGfx: the face's LVOs (library/graphics/face_lvo.c) read only the low
 * word or byte of the arguments NDK 3.2's graphics_lib.sfd gives as WORD,
 * UWORD, BYTE, UBYTE or BOOL. A caller built with NDK 3.2's inlines sets only
 * d0.w for a WORD (measured: OpenIntuition's WhichLayer call left d0's upper
 * word as it was), so the upper part is whatever the register held. Each call
 * here is made twice, on two equal rasters: once with clean arguments, once
 * with garbage in the upper parts; the rasters and the answers must match.
 * The env, arena and hooks are test_gfx_face.c's. Copyright (c) 2026
 * AmigaChrome Team. MIT. */
#define main face_main
#include "test_gfx_face.c"
#undef main
#include <string.h>

int32_t gfx_lvo_BltBitMap(struct gfx_bitmap *, int32_t, int32_t, struct gfx_bitmap *, int32_t, int32_t, int32_t,
                          int32_t, uint32_t, uint32_t, uint8_t *, struct gfx_base *);
void gfx_lvo_DrawEllipse(struct gfx_rastport *, int32_t, int32_t, int32_t, int32_t, struct gfx_base *);
void gfx_lvo_SetRast(struct gfx_rastport *, uint32_t, struct gfx_base *);
void gfx_lvo_Move(struct gfx_rastport *, int32_t, int32_t, struct gfx_base *);
void gfx_lvo_Draw(struct gfx_rastport *, int32_t, int32_t, struct gfx_base *);
void gfx_lvo_RectFill(struct gfx_rastport *, int32_t, int32_t, int32_t, int32_t, struct gfx_base *);
int32_t gfx_lvo_ReadPixel(struct gfx_rastport *, int32_t, int32_t, struct gfx_base *);
int32_t gfx_lvo_WritePixel(struct gfx_rastport *, int32_t, int32_t, struct gfx_base *);
void gfx_lvo_PolyDraw(struct gfx_rastport *, int32_t, const int16_t *, struct gfx_base *);
void gfx_lvo_SetAPen(struct gfx_rastport *, uint32_t, struct gfx_base *);
void gfx_lvo_SetBPen(struct gfx_rastport *, uint32_t, struct gfx_base *);
void gfx_lvo_SetDrMd(struct gfx_rastport *, uint32_t, struct gfx_base *);
void gfx_lvo_InitBitMap(struct gfx_bitmap *, int32_t, int32_t, int32_t, struct gfx_base *);
void gfx_lvo_ScrollRaster(struct gfx_rastport *, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, struct gfx_base *);
uint8_t *gfx_lvo_AllocRaster(uint32_t, uint32_t, struct gfx_base *);
void gfx_lvo_FreeRaster(uint8_t *, uint32_t, uint32_t, struct gfx_base *);
void gfx_lvo_BltBitMapRastPort(struct gfx_bitmap *, int32_t, int32_t, struct gfx_rastport *, int32_t, int32_t, int32_t,
                               int32_t, uint32_t, struct gfx_base *);
uint32_t gfx_lvo_ScalerDiv(uint32_t, uint32_t, uint32_t, struct gfx_base *);
void gfx_lvo_EraseRect(struct gfx_rastport *, int32_t, int32_t, int32_t, int32_t, struct gfx_base *);
int32_t gfx_lvo_WritePixelArray8(struct gfx_rastport *, uint32_t, uint32_t, uint32_t, uint32_t, uint8_t *,
                                 struct gfx_rastport *, struct gfx_base *);

static uint8_t libbase[GFX_BASE_POSSIZE] __attribute__((aligned(4)));
#define GB ((struct gfx_base *)libbase)
#define W 96
#define H 40
#define D 3

static struct gfx_bitmap bma, bmb, bmsrc;
static struct gfx_rastport rpa, rpb;
static int checks;

/* the upper part a caller might leave: a fast-RAM address half, all ones, a pattern */
static uint32_t junk(void)
{
    static const uint32_t j[] = { 0x40210000u, 0xFFFF0000u, 0x5A5A0000u, 0x00010000u, 0x7FFF0000u };
    return j[rn(5)];
}
static int32_t gw(int32_t v) { return (int32_t)(((uint32_t)v & 0xFFFFu) | junk()); }   /* WORD */
static uint32_t gb8(uint32_t v) { return (v & 0xFFu) | junk() | (rnd() & 0xFF00u); }  /* BYTE */

static void setup(void)
{
    uint32_t k;
    arena_reset("narrow");
    gfx_InitBitMap(&env, &bma, D, W, H);
    gfx_InitBitMap(&env, &bmb, D, W, H);
    gfx_InitBitMap(&env, &bmsrc, D, W, H);
    for (k = 0; k < D; k++) {
        bma.planes[k] = gfx_AllocRaster(&env, W, H);
        bmb.planes[k] = gfx_AllocRaster(&env, W, H);
        bmsrc.planes[k] = gfx_AllocRaster(&env, W, H);
        for (uint32_t i = 0; i < (uint32_t)bma.bytesperrow * H; i++) {
            uint8_t v = (uint8_t)rnd();
            bma.planes[k][i] = bmb.planes[k][i] = v;
            bmsrc.planes[k][i] = (uint8_t)rnd();
        }
    }
    gfx_InitRastPort(&env, &rpa); rpa.bitmap = &bma;
    gfx_InitRastPort(&env, &rpb); rpb.bitmap = &bmb;
}
static void teardown(void)
{
    for (uint32_t k = 0; k < D; k++) {
        gfx_FreeRaster(&env, bma.planes[k], W, H);
        gfx_FreeRaster(&env, bmb.planes[k], W, H);
        gfx_FreeRaster(&env, bmsrc.planes[k], W, H);
    }
    arena_reset("narrow leak");
}
static void same(const char *what, int r)
{
    checks++;
    for (uint32_t k = 0; k < D; k++)
        if (memcmp(bma.planes[k], bmb.planes[k], (size_t)bma.bytesperrow * H)) { fail(what, r); return; }
    if (rpa.cp_x != rpb.cp_x || rpa.cp_y != rpb.cp_y || rpa.fgpen != rpb.fgpen || rpa.bgpen != rpb.bgpen
        || rpa.drawmode != rpb.drawmode)
        fail(what, r);
}
static void eq(const char *what, int r, long a, long b) { checks++; if (a != b) fail(what, r); }

int main(void)
{
    int r;
    env_init();
    memcpy(gfx_env_of(libbase), &env, sizeof env);
    for (r = 0; r < ROUNDS * 8; r++) {
        int32_t x0 = (int32_t)rn(W), y0 = (int32_t)rn(H), x1 = x0 + (int32_t)rn(W - x0), y1 = y0 + (int32_t)rn(H - y0);
        uint32_t pen = rn(8), mode = rn(4);
        setup();
        gfx_lvo_SetAPen(&rpa, pen, GB); gfx_lvo_SetAPen(&rpb, gb8(pen), GB); same("SetAPen", r);
        gfx_lvo_SetBPen(&rpa, pen ^ 5, GB); gfx_lvo_SetBPen(&rpb, gb8(pen ^ 5), GB); same("SetBPen", r);
        gfx_lvo_SetDrMd(&rpa, mode, GB); gfx_lvo_SetDrMd(&rpb, gb8(mode), GB); same("SetDrMd", r);
        switch (rn(12)) {
        case 0: gfx_lvo_RectFill(&rpa, x0, y0, x1, y1, GB); gfx_lvo_RectFill(&rpb, gw(x0), gw(y0), gw(x1), gw(y1), GB);
                same("RectFill", r); break;
        case 1: gfx_lvo_Move(&rpa, x0, y0, GB); gfx_lvo_Move(&rpb, gw(x0), gw(y0), GB); same("Move", r);
                gfx_lvo_Draw(&rpa, x1, y1, GB); gfx_lvo_Draw(&rpb, gw(x1), gw(y1), GB); same("Draw", r); break;
        case 2: eq("WritePixel", r, gfx_lvo_WritePixel(&rpa, x0, y0, GB), gfx_lvo_WritePixel(&rpb, gw(x0), gw(y0), GB));
                same("WritePixel", r);
                eq("ReadPixel", r, gfx_lvo_ReadPixel(&rpa, x1, y1, GB), gfx_lvo_ReadPixel(&rpb, gw(x1), gw(y1), GB)); break;
        case 3: gfx_lvo_SetRast(&rpa, pen, GB); gfx_lvo_SetRast(&rpb, gb8(pen), GB); same("SetRast", r); break;
        case 4: { int32_t dx = (int32_t)rn(9) - 4, dy = (int32_t)rn(9) - 4;
                gfx_lvo_ScrollRaster(&rpa, dx, dy, x0, y0, x1, y1, GB);
                gfx_lvo_ScrollRaster(&rpb, gw(dx), gw(dy), gw(x0), gw(y0), gw(x1), gw(y1), GB); same("ScrollRaster", r); break; }
        case 5: gfx_lvo_EraseRect(&rpa, x0, y0, x1, y1, GB); gfx_lvo_EraseRect(&rpb, gw(x0), gw(y0), gw(x1), gw(y1), GB);
                same("EraseRect", r); break;
        case 6: { int32_t w = x1 - x0 + 1, h = y1 - y0 + 1; uint32_t mt = 0xC0 | rn(64);
                eq("BltBitMap", r, gfx_lvo_BltBitMap(&bmsrc, x0, y0, &bma, 0, 0, w, h, mt, 0xFF, 0, GB),
                   gfx_lvo_BltBitMap(&bmsrc, gw(x0), gw(y0), &bmb, gw(0), gw(0), gw(w), gw(h), gb8(mt), gb8(0xFF), 0, GB));
                same("BltBitMap", r); break; }
        case 7: { int32_t w = x1 - x0 + 1, h = y1 - y0 + 1;
                gfx_lvo_BltBitMapRastPort(&bmsrc, 0, 0, &rpa, x0, y0, w, h, 0xC0, GB);
                gfx_lvo_BltBitMapRastPort(&bmsrc, gw(0), gw(0), &rpb, gw(x0), gw(y0), gw(w), gw(h), gb8(0xC0), GB);
                same("BltBitMapRastPort", r); break; }
        case 8: { int16_t xy[6] = { (int16_t)x1, (int16_t)y0, (int16_t)x0, (int16_t)y1, (int16_t)x1, (int16_t)y1 };
                int32_t n = 1 + (int32_t)rn(3);
                gfx_lvo_PolyDraw(&rpa, n, xy, GB); gfx_lvo_PolyDraw(&rpb, gw(n), xy, GB); same("PolyDraw", r); break; }
        case 9: { int32_t a = 1 + (int32_t)rn(20), b = 1 + (int32_t)rn(10);
                gfx_lvo_DrawEllipse(&rpa, x0, y0, a, b, GB); gfx_lvo_DrawEllipse(&rpb, gw(x0), gw(y0), gw(a), gw(b), GB);
                same("DrawEllipse", r); break; }
        case 10: { struct gfx_bitmap ba, bb; int32_t d = 1 + (int32_t)rn(8), w = 1 + (int32_t)rn(600), h = 1 + (int32_t)rn(300);
                memset(&ba, 0, sizeof ba); memset(&bb, 0, sizeof bb);
                gfx_lvo_InitBitMap(&ba, d, w, h, GB); gfx_lvo_InitBitMap(&bb, (int32_t)gb8((uint32_t)d), gw(w), gw(h), GB);
                eq("InitBitMap", r, memcmp(&ba, &bb, sizeof ba), 0);
                uint8_t *p = gfx_lvo_AllocRaster((uint32_t)w, (uint32_t)h, GB), *q;
                uint32_t na = nblocks ? blocks[nblocks - 1].size : 0;
                q = gfx_lvo_AllocRaster((uint32_t)gw(w), (uint32_t)gw(h), GB);
                eq("AllocRaster", r, (long)na, (long)(nblocks ? blocks[nblocks - 1].size : 0));
                eq("AllocRaster", r, p != 0, q != 0);
                gfx_lvo_FreeRaster(q, (uint32_t)gw(w), (uint32_t)gw(h), GB);
                gfx_lvo_FreeRaster(p, (uint32_t)w, (uint32_t)h, GB); break; }
        default: { uint32_t f = rnd() & 0xFFFFu, n = 1 + rn(0xFFFF), dd = 1 + rn(0xFFFF);
                eq("ScalerDiv", r, (long)gfx_lvo_ScalerDiv(f, n, dd, GB),
                   (long)gfx_lvo_ScalerDiv((uint32_t)gw((int32_t)f), (uint32_t)gw((int32_t)n), (uint32_t)gw((int32_t)dd), GB));
                break; }
        }
        teardown();
    }
    printf("narrow: %d checks\nEND %d\n", checks, errors);
    return errors != 0;
}
