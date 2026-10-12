/* GfxCoreABITest: OS 3.2's graphics.library and OpenGfx's core side by side.
 * Copyright (c) 2026 Dalsin Limited. MIT.
 *
 * Two identical Chip RAM bitmaps get the same random contents; one call
 * goes through graphics.library on the first and through OpenGfx's
 * RastPort calls (library/core/gfx_rp.c) on the second, with the same pens,
 * DrawMode and Mask on an unlayered RastPort. Then every plane byte and the
 * result are compared. Calls: WritePixel, ReadPixel, SetRast,
 * WritePixelArray8, ReadPixelArray8, WriteChunkyPixels, Draw (after a Move
 * or chained, with line patterns), PolyDraw, RectFill and BltPattern (with
 * area patterns and masks); after a line the RastPort's pen position,
 * pattern count and Flags are compared too. Prints the first DIFFs of each
 * call with what was asked and the first pixels that differ, and a count
 * per call. Exit 0 when
 * nothing differs, 5 when something does, 20 when memory runs out.
 *
 *   GfxCoreABITest [ROUNDS=n]   (default 400)
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/gfxmacros.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <string.h>
#include "../library/core/gfx_rp.h"

#define NCALLS 10
static const char *const names[NCALLS] = {
    "WritePixel", "ReadPixel", "SetRast", "WritePixelArray8", "ReadPixelArray8", "WriteChunkyPixels",
    "Draw", "PolyDraw", "RectFill", "BltPattern"
};
static ULONG runs[NCALLS], diffs[NCALLS], pad_runs;
static int nochip;
static ULONG seed = 0x0A6A;
static ULONG rnd(void) { seed = seed * 1103515245UL + 12345UL; return seed >> 8; }

static struct BitMap *bma, *bmb, *tmpbm;
static struct RastPort rpa, rpb, tmprp;
static UBYTE *arr_a, *arr_b;
#define ARR 16384

static void fill_random(void)
{
    ULONG p, i, n = (ULONG)bma->BytesPerRow * bma->Rows;
    for (p = 0; p < bma->Depth; p++)
        for (i = 0; i < n; i++)
            ((UBYTE *)bma->Planes[p])[i] = ((UBYTE *)bmb->Planes[p])[i] = (UBYTE)rnd();
}

/* 0 when both bitmaps hold the same bytes, else the first differing plane+1 */
static int same(ULONG *at)
{
    ULONG p, i, n = (ULONG)bma->BytesPerRow * bma->Rows;
    WaitBlit();
    for (p = 0; p < bma->Depth; p++)
        for (i = 0; i < n; i++)
            if (((UBYTE *)bma->Planes[p])[i] != ((UBYTE *)bmb->Planes[p])[i]) {
                *at = i;
                return (int)p + 1;
            }
    return 0;
}

/* What was asked, for a DIFF line: a format and its arguments */
static const char *what;
static LONG wa[10];

/* 1 when the DIFF was printed */
static int report(int call, LONG ra, LONG rb, int plane, ULONG at)
{
    runs[call]++;
    if (ra == rb && !plane) return 0;
    diffs[call]++;
    if (diffs[call] > 15) return 0;
    Printf("DIFF %s ", (LONG)names[call]);
    VPrintf((STRPTR)what, wa);
    Printf(": OS %ld, ours %ld; ", ra, rb);
    if (plane) Printf("plane %ld byte %lu differs", (LONG)(plane - 1), at);
    else PutStr("same bytes");
    Printf(" (depth %ld, %ld bytes a row)\n", (LONG)bma->Depth, (LONG)bma->BytesPerRow);
    return 1;
}

/* The first pixels of a rectangle that differ, OS's pen and ours */
static void show_pixels(LONG x0, LONG y0, LONG x1, LONG y1)
{
    LONG x, y, n = 0;
    WaitBlit();
    PutStr("  pixels:");
    for (y = y0; y <= y1 && n < 8; y++)
        for (x = x0; x <= x1 && n < 8; x++) {
            LONG a = (LONG)ReadPixel(&rpa, x, y), b = ogfx_rp_ReadPixel(&rpb, x, y);
            if (a != b) { Printf(" %ld,%ld OS %ld ours %ld;", x, y, a, b); n++; }
        }
    PutStr("\n");
}

/* The RastPort's line state after a line: pen position, pattern count, Flags */
static int line_state(void)
{
    if (rpa.cp_x == rpb.cp_x && rpa.cp_y == rpb.cp_y && rpa.linpatcnt == rpb.linpatcnt && rpa.Flags == rpb.Flags)
        return 0;
    return 1;
}
static void show_state(void)
{
    Printf("  state: OS cp %ld,%ld cnt %ld flags %04lx; ours cp %ld,%ld cnt %ld flags %04lx\n",
           (LONG)rpa.cp_x, (LONG)rpa.cp_y, (LONG)rpa.linpatcnt, (LONG)rpa.Flags,
           (LONG)rpb.cp_x, (LONG)rpb.cp_y, (LONG)rpb.linpatcnt, (LONG)rpb.Flags);
}

/* in Chip RAM: graphics.library's blitter reads BltPattern's mask */
static UWORD *areapat;
static UBYTE *bltmask;
#define BLTMASK 2048
#define ASKED(f, a0, a1, a2, a3, a4, a5, a6) \
    (what = (f), wa[0] = (LONG)(a0), wa[1] = (LONG)(a1), wa[2] = (LONG)(a2), wa[3] = (LONG)(a3), \
     wa[4] = (LONG)(a4), wa[5] = (LONG)(a5), wa[6] = (LONG)(a6))

static void pens(void)
{
    UBYTE fg = (UBYTE)rnd(), bg = (UBYTE)rnd(), dm = (UBYTE)(rnd() & 7), mask = rnd() % 3 ? 0xFF : (UBYTE)rnd();
    SetAPen(&rpa, fg); SetBPen(&rpa, bg); SetDrMd(&rpa, dm); SetWriteMask(&rpa, mask);
    SetAPen(&rpb, fg); SetBPen(&rpb, bg); SetDrMd(&rpb, dm); SetWriteMask(&rpb, mask);
}

int main(void)
{
    LONG args[1] = { 0 };
    struct RDArgs *rd = ReadArgs("ROUNDS/N", args, NULL);
    ULONG rounds = rd && args[0] ? *(ULONG *)args[0] : 400, r, k, total = 0, bad = 0;

    arr_a = AllocVec(ARR, MEMF_ANY);
    arr_b = AllocVec(ARR, MEMF_ANY);
    areapat = AllocVec(16 * sizeof(UWORD), MEMF_CHIP);
    bltmask = AllocVec(BLTMASK, MEMF_CHIP);
    if (!arr_a || !arr_b || !areapat || !bltmask) { PutStr("no memory\n"); return 20; }
    Printf("OpenGfx core against %s", (LONG)((struct Library *)GfxBase)->lib_IdString);

    for (r = 0; r < rounds && !(SetSignal(0, 0) & SIGBREAKF_CTRL_C); r++) {
        ULONG w = 16 + rnd() % 320, h = 1 + rnd() % 64, d = 1 + rnd() % 8;
        bma = AllocBitMap(w, h, d, BMF_CLEAR, NULL);
        bmb = AllocBitMap(w, h, d, BMF_CLEAR, NULL);
        tmpbm = AllocBitMap(((w + 15) & ~15) + 16, 1, d, BMF_CLEAR, NULL);
        if (!bma || !bmb || !tmpbm) {
            if (bma) FreeBitMap(bma);
            if (bmb) FreeBitMap(bmb);
            if (tmpbm) FreeBitMap(tmpbm);
            Printf("no Chip RAM for a %ld x %ld x %ld bitmap: stopping after %ld rounds\n", (LONG)w, (LONG)h, (LONG)d, (LONG)r);
            nochip = 1;
            break;
        }
        InitRastPort(&rpa); rpa.BitMap = bma;
        InitRastPort(&rpb); rpb.BitMap = bmb;
        InitRastPort(&tmprp); tmprp.BitMap = tmpbm;
        tmprp.Layer = NULL;
        fill_random();
        for (k = 0; k < 10; k++) {
            ULONG W = (ULONG)bma->BytesPerRow * 8, H = bma->Rows, at = 0, i;
            /* inside the bitmap: OS 3.2 does not clip an unlayered RastPort, so a
             * pixel outside would be written to memory that is not the bitmap's */
            LONG x = (LONG)(rnd() % W), y = (LONG)(rnd() % H), ra, rb, old, pa, pb;
            ULONG x0 = rnd() % W, y0 = rnd() % H, x1 = x0 + rnd() % (W - x0), y1 = y0 + rnd() % (H - y0);
            int plane;
            pens();
            switch (rnd() % 10) {
            case 0:
                old = (LONG)ReadPixel(&rpa, x, y);
                ra = WritePixel(&rpa, x, y); rb = ogfx_rp_WritePixel(&rpb, x, y);
                plane = same(&at);
                pa = (LONG)ReadPixel(&rpa, x, y); pb = ogfx_rp_ReadPixel(&rpb, x, y);
                ASKED("%ld,%ld fg %ld bg %ld mode %ld mask %02lx: pixel %ld, OS made %ld, ours %ld", x, y,
                      (LONG)(UBYTE)rpa.FgPen, (LONG)(UBYTE)rpa.BgPen, (LONG)rpa.DrawMode, (LONG)rpa.Mask, old);
                wa[7] = pa; wa[8] = pb;
                report(0, ra, rb, plane, at);
                break;
            case 1:
                ra = (LONG)ReadPixel(&rpa, x, y); rb = ogfx_rp_ReadPixel(&rpb, x, y);
                ASKED("%ld,%ld", x, y, 0, 0, 0, 0, 0);
                report(1, ra, rb, 0, 0);
                break;
            case 2:
                if (rnd() % 4) break;
                i = rnd() & 0xFF;
                SetRast(&rpa, i); ogfx_rp_SetRast(&rpb, i);
                plane = same(&at);
                ASKED("pen %ld mask %02lx", (LONG)i, (LONG)rpa.Mask, 0, 0, 0, 0, 0);
                report(2, 0, 0, plane, at);
                break;
            case 3:
                if (((x1 - x0 + 16) & ~15) * (y1 - y0 + 1) > ARR) break;
                for (i = 0; i < ARR; i++) arr_a[i] = arr_b[i] = (UBYTE)rnd();
                ra = WritePixelArray8(&rpa, x0, y0, x1, y1, arr_a, &tmprp);
                rb = ogfx_rp_WritePixelArray8(&rpb, x0, y0, x1, y1, arr_b, &tmprp);
                plane = same(&at);
                ASKED("%ld,%ld-%ld,%ld mask %02lx", (LONG)x0, (LONG)y0, (LONG)x1, (LONG)y1, (LONG)rpa.Mask, 0, 0);
                report(3, ra, rb, plane, at);
                break;
            case 4:
                if (((x1 - x0 + 16) & ~15) * (y1 - y0 + 1) > ARR) break;
                memset(arr_a, 0xA5, ARR); memset(arr_b, 0xA5, ARR);
                ra = ReadPixelArray8(&rpa, x0, y0, x1, y1, arr_a, &tmprp);
                rb = ogfx_rp_ReadPixelArray8(&rpb, x0, y0, x1, y1, arr_b, &tmprp);
                {
                    /* the pixels asked for; the row's padding to 16 is counted apart */
                    ULONG bpr = (x1 - x0 + 16) & ~15UL, yy, xx, bad_at = ~0UL;
                    for (yy = 0; yy <= y1 - y0 && bad_at == ~0UL; yy++)
                        for (xx = 0; xx <= x1 - x0; xx++)
                            if (arr_a[yy * bpr + xx] != arr_b[yy * bpr + xx]) { bad_at = yy * bpr + xx; break; }
                    for (i = 0; i < ARR && arr_a[i] == arr_b[i]; i++) ;
                    if (bad_at == ~0UL && i < ARR && ++pad_runs <= 4) {
                        ULONG j, row = i / bpr * bpr;
                        Printf("PAD ReadPixelArray8 %ld,%ld-%ld,%ld: OS fills row bytes %ld-%ld with",
                               (LONG)x0, (LONG)y0, (LONG)x1, (LONG)y1, (LONG)(x1 - x0 + 1), (LONG)(bpr - 1));
                        for (j = row + x1 - x0 + 1; j < row + bpr; j++) Printf(" %ld", (LONG)arr_a[j]);
                        PutStr("; the bitmap there holds");
                        for (j = x1 + 1; j < x0 + bpr; j++)
                            Printf(" %ld", j < (ULONG)bma->BytesPerRow * 8 ? (LONG)ReadPixel(&rpa, j, y0 + i / bpr) : -1L);
                        PutStr("\n");
                    }
                    ASKED("%ld,%ld-%ld,%ld array byte %ld: OS %ld, ours %ld", (LONG)x0, (LONG)y0, (LONG)x1, (LONG)y1,
                          (LONG)bad_at, (LONG)(bad_at != ~0UL ? arr_a[bad_at] : 0), (LONG)(bad_at != ~0UL ? arr_b[bad_at] : 0));
                    report(4, ra, rb + (bad_at != ~0UL), 0, 0);
                }
                break;
            case 6: case 7: {
                /* a line or a polyline, inside the bitmap, after a Move or
                 * chained on from the last line (the pen position is kept
                 * inside the bitmap too) */
                UWORD ptrn = rnd() % 3 ? 0xFFFF : (UWORD)rnd();
                WORD poly[2 * 6];
                LONG n = k % 2 && rnd() % 2 ? 0 : 1, cnt = rnd() % 2 ? 1 : 1 + (LONG)(rnd() % 6), j;
                LONG sx = rpa.cp_x, sy = rpa.cp_y;
                int call = cnt > 1 ? 7 : 6, st;
                if (sx < 0 || sy < 0 || sx >= (LONG)W || sy >= (LONG)H) n = 1;
                if (n) {
                    SetDrPt(&rpa, ptrn); SetDrPt(&rpb, ptrn);
                    Move(&rpa, x, y); ogfx_rp_Move(&rpb, x, y);
                    sx = x; sy = y;
                }
                for (j = 0; j < cnt; j++) {
                    /* mostly short lines and every octant; sometimes flat or steep */
                    LONG tx = (LONG)(rnd() % W), ty = (LONG)(rnd() % H);
                    switch (rnd() % 4) {
                    case 0: ty = j ? poly[2 * j - 1] : sy; break;
                    case 1: tx = j ? poly[2 * j - 2] : sx; break;
                    case 2: tx = (j ? poly[2 * j - 2] : sx) + (LONG)(rnd() % 9) - 4;
                            ty = (j ? poly[2 * j - 1] : sy) + (LONG)(rnd() % 9) - 4;
                            if (tx < 0) tx = 0;
                            if (ty < 0) ty = 0;
                            if (tx >= (LONG)W) tx = W - 1;
                            if (ty >= (LONG)H) ty = H - 1;
                            break;
                    }
                    poly[2 * j] = (WORD)tx; poly[2 * j + 1] = (WORD)ty;
                }
                if (call == 6) { Draw(&rpa, poly[0], poly[1]); ogfx_rp_Draw(&rpb, poly[0], poly[1]); }
                else { PolyDraw(&rpa, cnt, poly); ogfx_rp_PolyDraw(&rpb, cnt, poly); }
                plane = same(&at);
                st = line_state();
                ASKED("%s%ld,%ld to %ld,%ld (%ld points) ptrn %04lx fg %ld bg %ld mode %ld",
                      (LONG)(n ? "Move " : "on from "), sx, sy, (LONG)poly[2 * cnt - 2], (LONG)poly[2 * cnt - 1],
                      cnt, (LONG)rpa.LinePtrn);
                wa[7] = (LONG)(UBYTE)rpa.FgPen; wa[8] = (LONG)(UBYTE)rpa.BgPen; wa[9] = (LONG)rpa.DrawMode;
                if (report(call, 0, st, plane, at)) {
                    show_pixels(0, 0, (LONG)W - 1, (LONG)H - 1);
                    show_state();
                    PutStr("  points:");
                    for (j = 0; j < cnt; j++) Printf(" %ld,%ld", (LONG)poly[2 * j], (LONG)poly[2 * j + 1]);
                    PutStr("\n");
                }
                break;
            }
            case 8: case 9: {
                /* RectFill, or BltPattern with or without a mask; solid, a
                 * one-row or a 2^n-row area pattern */
                LONG sz = rnd() % 3 ? 0 : (LONG)(rnd() % 5), j, call = rnd() % 2 ? 8 : 9;
                ULONG mbpr = ((x1 - x0 + 16) & ~15UL) / 8;
                int usemask = call == 9 && rnd() % 3 && mbpr * (y1 - y0 + 1) <= BLTMASK;
                for (j = 0; j < 16; j++) areapat[j] = rnd() % 4 ? (UWORD)rnd() : 0xFFFF;
                if (rnd() % 3) { SetAfPt(&rpa, areapat, sz); SetAfPt(&rpb, areapat, sz); }
                else { SetAfPt(&rpa, NULL, 0); SetAfPt(&rpb, NULL, 0); }
                for (j = 0; j < (LONG)BLTMASK; j++) bltmask[j] = rnd() % 4 ? (UBYTE)rnd() : 0xFF;
                if (call == 8) { RectFill(&rpa, x0, y0, x1, y1); ogfx_rp_RectFill(&rpb, x0, y0, x1, y1); }
                else {
                    BltPattern(&rpa, usemask ? bltmask : NULL, x0, y0, x1, y1, mbpr);
                    ogfx_rp_BltPattern(&rpb, usemask ? bltmask : NULL, x0, y0, x1, y1, mbpr);
                }
                plane = same(&at);
                ASKED("%ld,%ld-%ld,%ld pattern rows %ld mask bytes a row %ld fg %ld bg %ld mode %ld writemask %02lx",
                      (LONG)x0, (LONG)y0, (LONG)x1, (LONG)y1, (LONG)(rpa.AreaPtrn ? 1L << sz : 0),
                      (LONG)(usemask ? mbpr : 0), (LONG)(UBYTE)rpa.FgPen);
                wa[7] = (LONG)(UBYTE)rpa.BgPen; wa[8] = (LONG)rpa.DrawMode; wa[9] = (LONG)rpa.Mask;
                if (report(call, 0, 0, plane, at)) {
                    show_pixels((LONG)x0, (LONG)y0, (LONG)x1, (LONG)y1);
                    Printf("  pattern %04lx %04lx %04lx %04lx", (LONG)areapat[0], (LONG)areapat[1], (LONG)areapat[2], (LONG)areapat[3]);
                    if (usemask) Printf("; mask row 0 %02lx %02lx %02lx %02lx", (LONG)bltmask[0], (LONG)bltmask[1], (LONG)bltmask[2], (LONG)bltmask[3]);
                    PutStr("\n");
                }
                break;
            }
            default: {
                ULONG bpr = (x1 - x0 + 1) + rnd() % 9;
                if (bpr * (y1 - y0 + 1) > ARR) break;
                for (i = 0; i < ARR; i++) arr_a[i] = arr_b[i] = (UBYTE)rnd();
                WriteChunkyPixels(&rpa, x0, y0, x1, y1, arr_a, bpr);
                ogfx_rp_WriteChunkyPixels(&rpb, x0, y0, x1, y1, arr_b, bpr);
                plane = same(&at);
                ASKED("%ld,%ld-%ld,%ld bpr %ld mask %02lx", (LONG)x0, (LONG)y0, (LONG)x1, (LONG)y1, (LONG)bpr, (LONG)rpa.Mask, 0);
                report(5, 0, 0, plane, at);
            }
            }
            /* keep the two in step after a DIFF, so later calls are judged alone */
            rpb.cp_x = rpa.cp_x; rpb.cp_y = rpa.cp_y; rpb.linpatcnt = rpa.linpatcnt; rpb.Flags = rpa.Flags;
            if (same(&at)) {
                ULONG p, n = (ULONG)bma->BytesPerRow * bma->Rows;
                for (p = 0; p < bma->Depth; p++) CopyMem(bma->Planes[p], bmb->Planes[p], n);
            }
        }
        WaitBlit();
        FreeBitMap(bma); FreeBitMap(bmb); FreeBitMap(tmpbm);
    }
    for (k = 0; k < NCALLS; k++) {
        Printf("%-18s %6lu runs, %lu differ\n", (LONG)names[k], runs[k], diffs[k]);
        total += runs[k]; bad += diffs[k];
    }
    Printf("%lu checks, %lu differ\n", total, bad);
    if (pad_runs) Printf("%lu ReadPixelArray8 calls wrote the row padding differently (not counted)\n", pad_runs);
    FreeVec(arr_a); FreeVec(arr_b); FreeVec(areapat); FreeVec(bltmask);
    if (rd) FreeArgs(rd);
    return bad ? 5 : nochip ? 10 : 0;
}
