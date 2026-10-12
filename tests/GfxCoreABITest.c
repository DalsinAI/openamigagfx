/* GfxCoreABITest: OS 3.2's graphics.library and OpenGfx's core side by side.
 * Copyright (c) 2026 Dalsin Limited. MIT.
 *
 * Two identical Chip RAM bitmaps get the same random contents; one call
 * goes through graphics.library on the first and through OpenGfx's
 * RastPort calls (library/core/gfx_rp.c) on the second, with the same pens,
 * DrawMode and Mask on an unlayered RastPort. Then every plane byte and the
 * result are compared. Calls: WritePixel, ReadPixel, SetRast,
 * WritePixelArray8, ReadPixelArray8, WriteChunkyPixels. Prints the first
 * DIFFs of each call with what was asked, and a count per call. Exit 0 when
 * nothing differs, 5 when something does, 20 when memory runs out.
 *
 *   GfxCoreABITest [ROUNDS=n]   (default 400)
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <string.h>
#include "../library/core/gfx_rp.h"

#define NCALLS 6
static const char *const names[NCALLS] = {
    "WritePixel", "ReadPixel", "SetRast", "WritePixelArray8", "ReadPixelArray8", "WriteChunkyPixels"
};
static ULONG runs[NCALLS], diffs[NCALLS];
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
static LONG wa[8];

static void report(int call, LONG ra, LONG rb, int plane, ULONG at)
{
    runs[call]++;
    if (ra == rb && !plane) return;
    diffs[call]++;
    if (diffs[call] > 15) return;
    Printf("DIFF %s ", (LONG)names[call]);
    VPrintf((STRPTR)what, wa);
    Printf(": OS %ld, ours %ld; ", ra, rb);
    if (plane) Printf("plane %ld byte %lu differs", (LONG)(plane - 1), at);
    else PutStr("same bytes");
    Printf(" (depth %ld, %ld bytes a row)\n", (LONG)bma->Depth, (LONG)bma->BytesPerRow);
}
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
    if (!arr_a || !arr_b) { PutStr("no memory\n"); return 20; }
    Printf("OpenGfx core against %s", (LONG)((struct Library *)GfxBase)->lib_IdString);

    for (r = 0; r < rounds && !(SetSignal(0, 0) & SIGBREAKF_CTRL_C); r++) {
        ULONG w = 16 + rnd() % 320, h = 1 + rnd() % 64, d = 1 + rnd() % 8;
        bma = AllocBitMap(w, h, d, BMF_CLEAR, NULL);
        bmb = AllocBitMap(w, h, d, BMF_CLEAR, NULL);
        tmpbm = AllocBitMap(((w + 15) & ~15) + 16, 1, d, BMF_CLEAR, NULL);
        if (!bma || !bmb || !tmpbm) { PutStr("no Chip RAM\n"); return 20; }
        InitRastPort(&rpa); rpa.BitMap = bma;
        InitRastPort(&rpb); rpb.BitMap = bmb;
        InitRastPort(&tmprp); tmprp.BitMap = tmpbm;
        tmprp.Layer = NULL;
        fill_random();
        for (k = 0; k < 10; k++) {
            ULONG W = (ULONG)bma->BytesPerRow * 8, H = bma->Rows, at = 0, i;
            LONG x = (LONG)(rnd() % (W + 8)) - 4, y = (LONG)(rnd() % (H + 4)) - 2, ra, rb;
            ULONG x0 = rnd() % W, y0 = rnd() % H, x1 = x0 + rnd() % (W - x0), y1 = y0 + rnd() % (H - y0);
            int plane;
            pens();
            switch (rnd() % 6) {
            case 0:
                ra = WritePixel(&rpa, x, y); rb = ogfx_rp_WritePixel(&rpb, x, y);
                plane = same(&at);
                ASKED("%ld,%ld pen %ld mode %ld mask %02lx", x, y, (LONG)rpa.FgPen, (LONG)rpa.DrawMode, (LONG)rpa.Mask, 0, 0);
                report(0, ra, rb, plane, at);
                break;
            case 1:
                ra = ReadPixel(&rpa, x, y); rb = ogfx_rp_ReadPixel(&rpb, x, y);
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
                for (i = 0; i < ARR && arr_a[i] == arr_b[i]; i++) ;
                ASKED("%ld,%ld-%ld,%ld array byte %ld: OS %ld, ours %ld", (LONG)x0, (LONG)y0, (LONG)x1, (LONG)y1, (LONG)i,
                      (LONG)(i < ARR ? arr_a[i] : 0), (LONG)(i < ARR ? arr_b[i] : 0));
                report(4, ra, rb + (i < ARR), 0, 0);
                break;
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
    FreeVec(arr_a); FreeVec(arr_b);
    if (rd) FreeArgs(rd);
    return bad ? 5 : 0;
}
