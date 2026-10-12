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
 * area patterns and masks), BltBitMap and BltBitMapRastPort (minterms,
 * plane masks, overlapping copies), ClipBlit and ScrollRaster (overlapping,
 * and in a layer whether OS 3.2 adds damage, which the core never does); after a line the RastPort's pen position,
 * pattern count and Flags are compared too. Prints the first DIFFs of each
 * call with what was asked and the first pixels that differ, and a count
 * per call. Exit 0 when
 * nothing differs, 5 when something does, 20 when memory runs out.
 *
 * LAYERS draws in a layer instead: each side gets its own Layer_Info on
 * its bitmap, the same back layer (smart or simple refresh, sometimes
 * with a clip region) and one to three layers in front of it, and the
 * calls go to the back layer's RastPort; the covered parts a smart
 * refresh layer keeps are compared too. Nothing here touches a screen.
 *
 *   GfxCoreABITest [ROUNDS=n]   (default 400)
 *   GfxCoreABITest LAYERS       (the same in layers)
 *   GfxCoreABITest PROBE        (which pixels OS 3.2's BltPattern draws
 *                                through a mask, for the rule)
 *   GfxCoreABITest LPROBE       (where a smart refresh layer keeps its
 *                                covered pixels, for the rule)
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/gfxmacros.h>
#include <graphics/clip.h>
#include <graphics/layers.h>
#include <graphics/regions.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <string.h>
#include "../library/core/gfx_rp.h"

#define NCALLS 14
static const char *const names[NCALLS] = {
    "WritePixel", "ReadPixel", "SetRast", "WritePixelArray8", "ReadPixelArray8", "WriteChunkyPixels",
    "Draw", "PolyDraw", "RectFill", "BltPattern", "BltBitMap", "BltBitMapRastPort", "ClipBlit", "ScrollRaster"
};
static ULONG runs[NCALLS], diffs[NCALLS], pad_runs, refused;
static int nochip;
/* Chip RAM a switch case kept (after both sides' calls), and rounds that
 * ended with less Chip RAM than they started with */
static LONG chip_kept[14], chip_round_lost;
/* graphics.library cut the back layer's ClipRects differently on the two
 * sides (the parts no longer pair up): set by same(), counted per round */
static int recut, last_call;
static ULONG recuts;
static ULONG chip_rounds_lost;
static ULONG seed = 0x0A6A;
static ULONG rnd(void) { seed = seed * 1103515245UL + 12345UL; return seed >> 8; }

static struct BitMap *bma, *bmb, *tmpbm, *sbm;   /* sbm: a source for the blits, the same for both */
static struct RastPort rpa, rpb, tmprp;
static UBYTE *arr_a, *arr_b;
#define ARR 16384

/* LAYERS: a Layer_Info a side, the back layer the calls draw in (back[]),
 * up to three in front, and the back layer's clip region if it has one */
struct Library *LayersBase;
static int layered;
static struct Layer_Info *li[2];
static struct Layer *back[2], *front[2][3];
static struct Region *region[2];
static int nfront, shown_setup, locked;
static WORD geom[4][4];          /* back, then fronts: MinX, MinY, MaxX, MaxY */
static ULONG geom_flags[4];
static WORD reg_rect[3][4];
static int nreg;

static void fill_random(void)
{
    ULONG p, i, n = (ULONG)bma->BytesPerRow * bma->Rows;
    for (p = 0; p < bma->Depth; p++)
        for (i = 0; i < n; i++)
            ((UBYTE *)bma->Planes[p])[i] = ((UBYTE *)bmb->Planes[p])[i] = (UBYTE)rnd();
}

/* 0 when two bitmaps hold the same bytes, else the first differing plane+1 */
static int same_bm(struct BitMap *a, struct BitMap *b, ULONG *at)
{
    ULONG p, i, n = (ULONG)a->BytesPerRow * a->Rows;
    for (p = 0; p < a->Depth; p++)
        for (i = 0; i < n; i++)
            if (((UBYTE *)a->Planes[p])[i] != ((UBYTE *)b->Planes[p])[i]) {
                *at = i;
                return (int)p + 1;
            }
    return 0;
}

/* 0 when two covered parts hold the same pixels within the part's bounds:
 * its bitmap starts at the 16-pixel boundary left of MinX, and the bits
 * outside the bounds are never shown (OS 3.2's ClipBlit writes some) */
static int same_part(struct ClipRect *ca, struct ClipRect *cb, ULONG *at)
{
    struct BitMap *a = ca->BitMap, *b = cb->BitMap;
    LONG x0 = ca->bounds.MinX & 15, x1 = x0 + ca->bounds.MaxX - ca->bounds.MinX, h = ca->bounds.MaxY - ca->bounds.MinY + 1, x, y;
    ULONG p;
    if (h > (LONG)a->Rows) h = (LONG)a->Rows;
    if (x1 >= (LONG)a->BytesPerRow * 8) x1 = (LONG)a->BytesPerRow * 8 - 1;
    for (p = 0; p < a->Depth; p++)
        for (y = 0; y < h; y++)
            for (x = x0; x <= x1; x++) {
                ULONG i = (ULONG)y * a->BytesPerRow + (ULONG)(x >> 3);
                if ((((UBYTE *)a->Planes[p])[i] ^ ((UBYTE *)b->Planes[p])[i]) & (0x80 >> (x & 7))) {
                    *at = i;
                    return (int)p + 1;
                }
            }
    return 0;
}

/* 1 when the back layer's ClipRects differ between the sides in number,
 * bounds, being covered or having a bitmap */
static int cuts_differ(void)
{
    struct ClipRect *ca = back[0]->ClipRect, *cb = back[1]->ClipRect;
    for (; ca && cb; ca = ca->Next, cb = cb->Next)
        if (ca->bounds.MinX != cb->bounds.MinX || ca->bounds.MinY != cb->bounds.MinY || ca->bounds.MaxX != cb->bounds.MaxX ||
            ca->bounds.MaxY != cb->bounds.MaxY || !ca->obscured != !cb->obscured || !ca->BitMap != !cb->BitMap)
            return 1;
    return ca || cb;
}

static void show_cuts(int s)
{
    struct ClipRect *c;
    Printf("  %s:", (LONG)(s ? "ours" : "OS  "));
    for (c = back[s]->ClipRect; c; c = c->Next)
        Printf(" %ld,%ld-%ld,%ld%s", (LONG)c->bounds.MinX, (LONG)c->bounds.MinY, (LONG)c->bounds.MaxX, (LONG)c->bounds.MaxY,
               (LONG)(!c->obscured ? "" : c->BitMap ? "c" : "h"));
    PutStr("\n");
}

/* 0 when both sides hold the same bytes: the bitmaps and, in layers, the
 * back layer's covered parts (then 16 * the ClipRect's number + plane+1) */
static int same(ULONG *at)
{
    struct ClipRect *ca, *cb;
    int p, n;
    WaitBlit();
    if ((p = same_bm(bma, bmb, at))) return p;
    if (!layered) return 0;
    if (cuts_differ()) { recut = 1; return 0; }
    for (ca = back[0]->ClipRect, cb = back[1]->ClipRect, n = 1; ca && cb; ca = ca->Next, cb = cb->Next, n++)
        if (ca->BitMap && cb->BitMap && ca->obscured && (p = same_part(ca, cb, at))) return 16 * n + p;
    return 0;
}

/* B takes A's bytes, bitmaps and covered parts */
static void sync_b(void)
{
    struct ClipRect *ca, *cb;
    ULONG p, n = (ULONG)bma->BytesPerRow * bma->Rows;
    WaitBlit();
    for (p = 0; p < bma->Depth; p++) CopyMem(bma->Planes[p], bmb->Planes[p], n);
    if (!layered) return;
    for (ca = back[0]->ClipRect, cb = back[1]->ClipRect; ca && cb; ca = ca->Next, cb = cb->Next)
        if (ca->BitMap && cb->BitMap && ca->obscured)
            for (p = 0; p < ca->BitMap->Depth; p++)
                CopyMem(ca->BitMap->Planes[p], cb->BitMap->Planes[p], (ULONG)ca->BitMap->BytesPerRow * ca->BitMap->Rows);
}

static void show_setup(void)
{
    int i;
    if (!layered || shown_setup) return;
    shown_setup = 1;
    Printf("  layers: back %ld,%ld-%ld,%ld %s", (LONG)geom[0][0], (LONG)geom[0][1], (LONG)geom[0][2], (LONG)geom[0][3],
           (LONG)(geom_flags[0] & LAYERSMART ? "smart" : "simple"));
    for (i = 1; i <= nfront; i++)
        Printf("; front %ld,%ld-%ld,%ld", (LONG)geom[i][0], (LONG)geom[i][1], (LONG)geom[i][2], (LONG)geom[i][3]);
    for (i = 0; i < nreg; i++)
        Printf("; clip %ld,%ld-%ld,%ld", (LONG)reg_rect[i][0], (LONG)reg_rect[i][1], (LONG)reg_rect[i][2], (LONG)reg_rect[i][3]);
    PutStr("\n");
}

/* A covered part that differs: its rectangle and bitmap, where the byte
 * is, both sides' bytes, and whether another part shares the bitmap */
static void show_part(int n, int plane, ULONG at)
{
    struct ClipRect *ca = back[0]->ClipRect, *cb = back[1]->ClipRect, *c;
    struct BitMap *b;
    int i, m;
    for (i = 1; i < n && ca && cb; i++) { ca = ca->Next; cb = cb->Next; }
    if (!ca || !cb) return;
    b = ca->BitMap;
    Printf("  part %ld %ld,%ld-%ld,%ld, bitmap %ld bytes a row %ld rows; row %ld x %ld-%ld: OS %02lx ours %02lx", (LONG)n,
           (LONG)ca->bounds.MinX, (LONG)ca->bounds.MinY, (LONG)ca->bounds.MaxX, (LONG)ca->bounds.MaxY,
           (LONG)b->BytesPerRow, (LONG)b->Rows, (LONG)(at / b->BytesPerRow), (LONG)(at % b->BytesPerRow * 8),
           (LONG)(at % b->BytesPerRow * 8 + 7), (LONG)((UBYTE *)b->Planes[plane])[at],
           (LONG)((UBYTE *)cb->BitMap->Planes[plane])[at]);
    for (c = back[0]->ClipRect, m = 1; c; c = c->Next, m++)
        if (m != n && c->BitMap == b) Printf("; shares its bitmap with part %ld", (LONG)m);
    PutStr("\n");
}

/* A's back layer's damage, to see whether an OS call added some (the core
 * adds none, so the calls it takes must not need any): LAYERREFRESH, the
 * damage's bounds and how many rectangles it has */
static ULONG damage_sig(void)
{
    struct Layer *l = back[0];
    struct RegionRectangle *r;
    ULONG n = 0;
    if (!l) return 0;
    if (l->DamageList)
        for (r = l->DamageList->RegionRectangle; r; r = r->Next) n++;
    return (l->Flags & LAYERREFRESH ? 0x80000000UL : 0) ^ (n << 20) ^
           (l->DamageList ? (ULONG)(l->DamageList->bounds.MinX * 7 + l->DamageList->bounds.MinY * 131 +
                                    l->DamageList->bounds.MaxX * 1031 + l->DamageList->bounds.MaxY * 8191) : 0);
}

/* What was asked, for a DIFF line: a format and its arguments */
static const char *what;
static LONG wa[10];

/* 1 when the DIFF was printed */
static int report(int call, LONG ra, LONG rb, int plane, ULONG at)
{
    runs[call]++;
    last_call = call;
    if (ra == rb && !plane) return 0;
    diffs[call]++;
    if (diffs[call] > 15) return 0;
    Printf("DIFF %s ", (LONG)names[call]);
    VPrintf((STRPTR)what, wa);
    Printf(": OS %ld, ours %ld; ", ra, rb);
    if (plane > 16) Printf("covered part %ld plane %ld byte %lu differs", (LONG)(plane / 16), (LONG)(plane % 16 - 1), at);
    else if (plane) Printf("plane %ld byte %lu differs", (LONG)(plane - 1), at);
    else PutStr("same bytes");
    Printf(" (depth %ld, %ld bytes a row)\n", (LONG)bma->Depth, (LONG)bma->BytesPerRow);
    show_setup();
    if (plane > 16) show_part(plane / 16, plane % 16 - 1, at);
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

/* The pixels of row y set from x0 to x1, relative to xl, as ranges */
static void ranges(struct RastPort *rp, LONG y, LONG x0, LONG x1, LONG xl)
{
    LONG x, start = -1;
    for (x = x0; x <= x1 + 1; x++) {
        int on = x <= x1 && ReadPixel(rp, x, y);
        if (on && start < 0) start = x;
        if (!on && start >= 0) {
            if (start == x - 1) Printf(" %ld", start - xl);
            else Printf(" %ld-%ld", start - xl, x - 1 - xl);
            start = -1;
        }
    }
}

/* BltPattern through a mask on a clear 1-plane bitmap, JAM1 pen 1, solid:
 * A, every mask byte $FF, rows 0 and 1 printed; B, row 0's bytes 0 and
 * row 1's $FF (does row 0 read on into row 1?); C, only the mask bits
 * below the width set (row 0). Pixels relative to xl. */
static int probe(void)
{
    static const UBYTE shifts[] = { 0, 1, 5, 8, 15 };
    static const UBYTE widths[] = { 1, 9, 16, 17, 33 };
    struct BitMap *bm = AllocBitMap(320, 4, 1, BMF_CLEAR, NULL);
    UBYTE *m = AllocVec(256, MEMF_CHIP | MEMF_CLEAR);
    struct RastPort rp;
    ULONG a, b, c, t, i;

    if (!bm || !m) { PutStr("no Chip RAM\n"); if (bm) FreeBitMap(bm); if (m) FreeVec(m); return 20; }
    InitRastPort(&rp); rp.BitMap = bm;
    SetAPen(&rp, 1); SetDrMd(&rp, JAM1); SetWriteMask(&rp, 1);
    PutStr("BltPattern PROBE: shift width bytes-a-row: A row0 | A row1 | B row0 | C row0\n");
    for (a = 0; a < sizeof shifts; a++)
        for (b = 0; b < sizeof widths; b++)
            for (c = 0; c < 3; c++) {
                LONG xl = 32 + shifts[a], w = widths[b], maxx = xl + w - 1;
                ULONG bpr = 2 * ((w + 15) / 16) + 2 * c;
                Printf("s %ld w %ld bpr %ld:", (LONG)shifts[a], w, (LONG)bpr);
                for (t = 0; t < 3; t++) {
                    for (i = 0; i < 256; i++)
                        m[i] = t == 0 ? 0xFF : t == 1 ? (i < bpr ? 0 : 0xFF)
                             : (i < bpr && (LONG)(i * 8) < w ? (UBYTE)(0xFF << ((LONG)(i * 8 + 8) > w ? 8 - (w - (LONG)i * 8) : 0)) : 0);
                    SetRast(&rp, 0);
                    BltPattern(&rp, m, xl, 1, maxx, 2, bpr);
                    WaitBlit();
                    ranges(&rp, 1, 0, 319, xl);
                    PutStr(" |");
                    if (t == 0) { ranges(&rp, 2, 0, 319, xl); PutStr(" |"); }
                }
                PutStr("\n");
            }
    FreeVec(m);
    FreeBitMap(bm);
    return 0;
}

/* LAYERS: the same layers on both bitmaps (filled with the same random
 * bytes first, so the covered parts a smart layer keeps start alike). The
 * calls draw in back[] through rpa and rpb; B's back layer stays locked
 * for the round, as OpenGfx holds it while the core draws. */
static void rnd_rect(WORD *r, LONG w, LONG h, LONG minw, LONG minh)
{
    LONG x0 = (LONG)(rnd() % (ULONG)(w - minw)), y0 = (LONG)(rnd() % (ULONG)(h - minh));
    r[0] = (WORD)x0; r[1] = (WORD)y0;
    r[2] = (WORD)(x0 + minw - 1 + (LONG)(rnd() % (ULONG)(w - x0 - minw + 1)));
    r[3] = (WORD)(y0 + minh - 1 + (LONG)(rnd() % (ULONG)(h - y0 - minh + 1)));
}

static void free_layers(void)
{
    int s, i;
    for (s = 0; s < 2; s++) {
        if (s == 1 && locked) { UnlockLayerRom(back[1]); locked = 0; }
        for (i = nfront - 1; i >= 0; i--) if (front[s][i]) DeleteLayer(0, front[s][i]);
        if (back[s]) {
            if (region[s]) InstallClipRegion(back[s], NULL);
            DeleteLayer(0, back[s]);
        }
        if (region[s]) DisposeRegion(region[s]);
        if (li[s]) DisposeLayerInfo(li[s]);
        back[s] = NULL; region[s] = NULL; li[s] = NULL;
        for (i = 0; i < 3; i++) front[s][i] = NULL;
    }
}

static int make_layers(void)
{
    LONG w = (LONG)bma->BytesPerRow * 8, h = bma->Rows;
    int s, i;

    nfront = 1 + (int)(rnd() % 3);
    rnd_rect(geom[0], w, h, 16, 8);
    geom_flags[0] = rnd() % 2 ? LAYERSMART : LAYERSIMPLE;
    for (i = 1; i <= nfront; i++) { rnd_rect(geom[i], w, h, 8, 4); geom_flags[i] = rnd() % 2 ? LAYERSMART : LAYERSIMPLE; }
    nreg = rnd() % 3 ? 0 : 1 + (int)(rnd() % 3);
    for (i = 0; i < nreg; i++) rnd_rect(reg_rect[i], geom[0][2] - geom[0][0] + 9, geom[0][3] - geom[0][1] + 5, 1, 1);
    shown_setup = 0;
    for (s = 0; s < 2; s++) {
        struct BitMap *bm = s ? bmb : bma;
        if (!(li[s] = NewLayerInfo())) return 0;
        if (!(back[s] = CreateUpfrontLayer(li[s], bm, geom[0][0], geom[0][1], geom[0][2], geom[0][3], geom_flags[0], NULL)))
            return 0;
        for (i = 0; i < nfront; i++)
            if (!(front[s][i] = CreateUpfrontLayer(li[s], bm, geom[i + 1][0], geom[i + 1][1], geom[i + 1][2], geom[i + 1][3],
                                                   geom_flags[i + 1], NULL)))
                return 0;
        if (nreg) {
            struct Rectangle r;
            if (!(region[s] = NewRegion())) return 0;
            for (i = 0; i < nreg; i++) {
                r.MinX = reg_rect[i][0]; r.MinY = reg_rect[i][1]; r.MaxX = reg_rect[i][2]; r.MaxY = reg_rect[i][3];
                if (!OrRectRegion(region[s], &r)) return 0;
            }
            InstallClipRegion(back[s], region[s]);
        }
    }
    InitRastPort(&rpa); rpa.Layer = back[0]; rpa.BitMap = bma;
    InitRastPort(&rpb); rpb.Layer = back[1]; rpb.BitMap = bmb;
    LockLayerRom(back[1]);
    locked = 1;
    if (!ogfx_rp_takes(&rpb, 0)) { PutStr("ogfx_rp_takes refuses the layer\n"); return 0; }
    return 1;
}

/* The bits set in a covered part's bitmap, plane 0, as byte.bit (bit 7
 * the leftmost), at most 12 */
static void set_bits(struct BitMap *b)
{
    ULONG i, n = (ULONG)b->BytesPerRow * b->Rows, k = 0;
    int j;
    for (i = 0; i < n && k < 12; i++)
        for (j = 7; j >= 0 && k < 12; j--)
            if (((UBYTE *)b->Planes[0])[i] >> j & 1) { Printf(" r%ld x%ld", (LONG)(i / b->BytesPerRow), (LONG)(i % b->BytesPerRow * 8 + 7 - j)); k++; }
    if (!k) PutStr(" none");
}

/* A smart refresh back layer at an odd x with a layer in front: for each
 * covered part, its rectangle and bitmap, then which of its bits one
 * WritePixel at the part's corner (and 1 and 5 pixels right of it, and one
 * down) sets, then what SetRast sets in it. */
static int lprobe(void)
{
    static const WORD geo[][8] = {
        { 37, 10, 200, 60, 50, 20, 120, 50 },
        { 16, 8, 150, 40, 3, 4, 40, 30 },
        { 45, 5, 230, 50, 100, 30, 240, 70 },
    };
    struct BitMap *bm = AllocBitMap(320, 100, 2, BMF_CLEAR, NULL);
    struct Layer_Info *info;
    struct Layer *bk, *fr;
    struct ClipRect *cr;
    ULONG g;
    int n;

    if (!bm || !(LayersBase = OpenLibrary("layers.library", 39))) { PutStr("no Chip RAM or layers.library\n"); if (bm) FreeBitMap(bm); return 20; }
    Printf("LPROBE, %s", (LONG)LayersBase->lib_IdString);
    for (g = 0; g < sizeof geo / sizeof geo[0]; g++) {
        const WORD *q = geo[g];
        if (!(info = NewLayerInfo())) break;
        bk = CreateUpfrontLayer(info, bm, q[0], q[1], q[2], q[3], LAYERSMART, NULL);
        fr = bk ? CreateUpfrontLayer(info, bm, q[4], q[5], q[6], q[7], LAYERSMART, NULL) : NULL;
        if (!fr) { if (bk) DeleteLayer(0, bk); DisposeLayerInfo(info); PutStr("no layers\n"); break; }
        Printf("back %ld,%ld-%ld,%ld, front %ld,%ld-%ld,%ld\n", (LONG)q[0], (LONG)q[1], (LONG)q[2], (LONG)q[3],
               (LONG)q[4], (LONG)q[5], (LONG)q[6], (LONG)q[7]);
        SetAPen(bk->rp, 1); SetDrMd(bk->rp, JAM1); SetWriteMask(bk->rp, 1);
        for (cr = bk->ClipRect, n = 1; cr; cr = cr->Next, n++) {
            static const WORD at[][2] = { { 0, 0 }, { 1, 0 }, { 5, 0 }, { 0, 1 } };
            struct BitMap *b = cr->BitMap;
            ULONG p, k;
            Printf(" part %ld %ld,%ld-%ld,%ld %s", (LONG)n, (LONG)cr->bounds.MinX, (LONG)cr->bounds.MinY,
                   (LONG)cr->bounds.MaxX, (LONG)cr->bounds.MaxY, (LONG)(cr->obscured ? "covered" : "on top"));
            if (!cr->obscured || !b) { PutStr("\n"); continue; }
            Printf(", bitmap %ld bytes a row, %ld rows, depth %ld, flags %02lx\n", (LONG)b->BytesPerRow, (LONG)b->Rows,
                   (LONG)b->Depth, (LONG)b->Flags);
            for (k = 0; k < 4; k++) {
                LONG x = cr->bounds.MinX - bk->bounds.MinX + at[k][0], y = cr->bounds.MinY - bk->bounds.MinY + at[k][1];
                for (p = 0; p < b->Depth; p++) memset(b->Planes[p], 0, (ULONG)b->BytesPerRow * b->Rows);
                Printf("  WritePixel %ld,%ld (screen %ld,%ld) answers %ld, sets", x, y, x + bk->bounds.MinX, y + bk->bounds.MinY,
                       WritePixel(bk->rp, x, y));
                WaitBlit();
                set_bits(b);
                PutStr("\n");
            }
            for (p = 0; p < b->Depth; p++) memset(b->Planes[p], 0, (ULONG)b->BytesPerRow * b->Rows);
            SetRast(bk->rp, 1);
            WaitBlit();
            {
                ULONG i, nb = (ULONG)b->BytesPerRow * b->Rows, ones = 0;
                for (i = 0; i < nb; i++) { UBYTE v = ((UBYTE *)b->Planes[0])[i]; while (v) { ones += v & 1; v >>= 1; } }
                Printf("  SetRast 1 sets %ld of its %ld bits; row 0:", (LONG)ones, (LONG)(nb * 8));
                for (i = 0; i < b->BytesPerRow; i++) Printf(" %02lx", (LONG)((UBYTE *)b->Planes[0])[i]);
                PutStr("\n");
            }
            SetRast(bk->rp, 0);
        }
        DeleteLayer(0, fr);
        DeleteLayer(0, bk);
        DisposeLayerInfo(info);
    }
    CloseLibrary(LayersBase);
    LayersBase = NULL;
    FreeBitMap(bm);
    return 0;
}

int main(void)
{
    LONG args[4] = { 0, 0, 0, 0 };
    struct RDArgs *rd = ReadArgs("ROUNDS/N,PROBE/S,LAYERS/S,LPROBE/S", args, NULL);
    ULONG rounds = rd && args[0] ? *(ULONG *)args[0] : 400, r, k, total = 0, bad = 0;

    if (rd && args[1]) { int rc = probe(); FreeArgs(rd); return rc; }
    if (rd && args[3]) { int rc = lprobe(); FreeArgs(rd); return rc; }
    if (rd && args[2]) {
        layered = 1;
        if (!(LayersBase = OpenLibrary("layers.library", 39))) { PutStr("no layers.library\n"); FreeArgs(rd); return 20; }
    }

    arr_a = AllocVec(ARR, MEMF_ANY);
    arr_b = AllocVec(ARR, MEMF_ANY);
    areapat = AllocVec(16 * sizeof(UWORD), MEMF_CHIP);
    bltmask = AllocVec(BLTMASK, MEMF_CHIP);
    if (!arr_a || !arr_b || !areapat || !bltmask) { PutStr("no memory\n"); return 20; }
    Printf("OpenGfx core against %s", (LONG)((struct Library *)GfxBase)->lib_IdString);
    if (layered) Printf("in layers, against %s", (LONG)LayersBase->lib_IdString);

    for (r = 0; r < rounds && !(SetSignal(0, 0) & SIGBREAKF_CTRL_C); r++) {
        LONG round_chip = (LONG)AvailMem(MEMF_CHIP);
        ULONG w = 16 + rnd() % 320, h = 1 + rnd() % 64, d = 1 + rnd() % 8;
        int lay_ok = 1;
        if (layered) { w = 64 + rnd() % 256; h = 16 + rnd() % 100; d = 1 + rnd() % 4; }
        bma = AllocBitMap(w, h, d, BMF_CLEAR, NULL);
        bmb = AllocBitMap(w, h, d, BMF_CLEAR, NULL);
        tmpbm = AllocBitMap(((w + 15) & ~15) + 32, 1, d, BMF_CLEAR, NULL);
        sbm = AllocBitMap(16 + rnd() % 320, 1 + rnd() % 64, 1 + rnd() % 8, 0, NULL);
        if (sbm) {
            ULONG p, i, n = (ULONG)sbm->BytesPerRow * sbm->Rows;
            for (p = 0; p < sbm->Depth; p++) for (i = 0; i < n; i++) ((UBYTE *)sbm->Planes[p])[i] = (UBYTE)rnd();
        }
        if (bma && bmb && tmpbm && sbm && layered) { fill_random(); lay_ok = make_layers(); }
        if (!bma || !bmb || !tmpbm || !sbm || !lay_ok) {
            if (layered) free_layers();
            if (sbm) FreeBitMap(sbm);
            if (bma) FreeBitMap(bma);
            if (bmb) FreeBitMap(bmb);
            if (tmpbm) FreeBitMap(tmpbm);
            Printf("no Chip RAM for a %ld x %ld x %ld bitmap: stopping after %ld rounds\n", (LONG)w, (LONG)h, (LONG)d, (LONG)r);
            nochip = 1;
            break;
        }
        if (!layered) {
            InitRastPort(&rpa); rpa.BitMap = bma;
            InitRastPort(&rpb); rpb.BitMap = bmb;
            fill_random();
        }
        InitRastPort(&tmprp); tmprp.BitMap = tmpbm;
        tmprp.Layer = NULL;
        for (k = 0; k < 10; k++) {
            /* in layers, the RastPort's coordinates: the back layer and 8
             * pixels past its right and bottom edges */
            ULONG W = layered ? (ULONG)(geom[0][2] - geom[0][0] + 9) : (ULONG)bma->BytesPerRow * 8,
                  H = layered ? (ULONG)(geom[0][3] - geom[0][1] + 9) : bma->Rows, at = 0, i;
            /* inside the bitmap: OS 3.2 does not clip an unlayered RastPort, so a
             * pixel outside would be written to memory that is not the bitmap's */
            LONG x = (LONG)(rnd() % W), y = (LONG)(rnd() % H), ra, rb, old, pa, pb;
            ULONG x0 = rnd() % W, y0 = rnd() % H, x1 = x0 + rnd() % (W - x0), y1 = y0 + rnd() % (H - y0);
            int plane;
            if (layered && (plane = same(&at))) {
                Printf("PRE: before call %ld of this round the sides already differ (%ld)\n", (LONG)k, (LONG)plane);
                show_setup();
                if (plane > 16) show_part(plane / 16, plane % 16 - 1, at);
                sync_b();
            }
            LONG chip0 = (LONG)AvailMem(MEMF_CHIP);
            ULONG sel = rnd() % 14;
            pens();
            switch (sel) {
            case 12: case 13: {
                /* ClipBlit within the RastPort or from an unlayered one on the
                 * round's source; ScrollRaster. Unlayered, inside the bitmap
                 * (OS 3.2 clips neither); in a layer, anywhere near it. Where
                 * the core refuses the layer (ogfx_rp_takes_copy), B gets
                 * graphics.library's call too and the call is not counted. */
                int call = (int)(rnd() % 2) + 12, self = rnd() % 3 != 0, mine = ogfx_rp_takes_copy(&rpb);
                LONG sdx = (LONG)(rnd() % 21) - 10, sdy = (LONG)(rnd() % 11) - 5, bw = (LONG)(x1 - x0 + 1), bh = (LONG)(y1 - y0 + 1);
                LONG sx = (LONG)x0, sy = (LONG)y0, tx = (LONG)x0 + sdx, ty = (LONG)y0 + sdy;
                ULONG mt = call == 13 || rnd() % 3 ? 0xC0 : rnd() & 0xF0, dsig = layered ? damage_sig() : 0;
                struct RastPort srca, *sa = &rpa, *sb = &rpb;
                if (!self) {
                    InitRastPort(&srca); srca.BitMap = sbm; sa = sb = &srca;
                    sx = (LONG)(rnd() % (ULONG)(sbm->BytesPerRow * 8)); sy = (LONG)(rnd() % (ULONG)sbm->Rows);
                    if (sx + bw > (LONG)sbm->BytesPerRow * 8) bw = (LONG)sbm->BytesPerRow * 8 - sx;
                    if (sy + bh > (LONG)sbm->Rows) bh = (LONG)sbm->Rows - sy;
                }
                if (!layered) {
                    if (tx < 0) tx = 0;
                    if (ty < 0) ty = 0;
                    if (tx >= (LONG)W || ty >= (LONG)H) break;
                    if (tx + bw > (LONG)W) bw = (LONG)W - tx;
                    if (ty + bh > (LONG)H) bh = (LONG)H - ty;
                }
                if (call == 12) {
                    ClipBlit(sa, sx, sy, &rpa, tx, ty, bw, bh, mt);
                    if (!mine || !(mine = ogfx_rp_ClipBlit(sb, sx, sy, &rpb, tx, ty, bw, bh, mt))) ClipBlit(sb, sx, sy, &rpb, tx, ty, bw, bh, mt);
                    ASKED("%s %ld,%ld to %ld,%ld size %ld x %ld minterm %02lx", (LONG)(self ? "self" : "source"), sx, sy, tx, ty, bw, bh);
                    wa[7] = (LONG)mt;
                } else {
                    ScrollRaster(&rpa, sdx, sdy, (LONG)x0, (LONG)y0, (LONG)x1, (LONG)y1);
                    if (!mine || !(mine = ogfx_rp_ScrollRaster(&rpb, sdx, sdy, (LONG)x0, (LONG)y0, (LONG)x1, (LONG)y1))) ScrollRaster(&rpb, sdx, sdy, (LONG)x0, (LONG)y0, (LONG)x1, (LONG)y1);
                    ASKED("by %ld,%ld in %ld,%ld-%ld,%ld fg %ld bg %ld", sdx, sdy, (LONG)x0, (LONG)y0, (LONG)x1, (LONG)y1, (LONG)(UBYTE)rpa.FgPen);
                    wa[7] = (LONG)(UBYTE)rpa.BgPen;
                }
                if (!mine) { refused++; break; }
                plane = same(&at);
                if (!plane && layered && damage_sig() != dsig) {
                    plane = 0; ra = 1; rb = 0;   /* OS added damage, the core none */
                } else ra = rb = 0;
                if (report(call, ra, rb, plane, at)) {
                    Printf("  mode %ld writemask %02lx pattern %s%s\n", (LONG)rpa.DrawMode, (LONG)rpa.Mask,
                           (LONG)(rpa.AreaPtrn ? "set" : "none"), (LONG)(ra != rb ? "; OS added damage (answers: 1 = OS)" : ""));
                    if (!layered) show_pixels((LONG)x0, (LONG)y0, (LONG)x1, (LONG)y1);
                }
                break;
            }
            case 10: case 11: {
                /* BltBitMap, from the round's source or within the bitmap
                 * itself (overlapping), and BltBitMapRastPort, into the
                 * RastPort (its layer, in LAYERS), which may hang over the
                 * layer's edge */
                int self = rnd() % 2, call = (int)(rnd() % 2) + 10;
                struct BitMap *sa = self ? bma : sbm, *sb = self ? bmb : sbm;
                LONG sw = (LONG)sa->BytesPerRow * 8, sh = sa->Rows, bw = 1 + (LONG)(rnd() % 64), bh = 1 + (LONG)(rnd() % 32);
                LONG sx = (LONG)(rnd() % (ULONG)sw), sy = (LONG)(rnd() % (ULONG)sh), tx = (LONG)(rnd() % W), ty = (LONG)(rnd() % H);
                ULONG mt = rnd() % 3 ? 0xC0 : rnd() & 0xF0, bmask = rnd() % 3 ? 0xFF : rnd() & 0xFF;
                if (self && rnd() % 2) { tx = sx + (LONG)(rnd() % 9) - 4; ty = sy + (LONG)(rnd() % 3) - 1; if (tx < 0) tx = 0; if (ty < 0) ty = 0; }
                /* OS 3.2 clips neither the source nor a bare bitmap: past
                 * either edge it reads or writes memory that is not the
                 * bitmap's (that corrupted the memory list), so both stay
                 * inside; only a layer's RastPort may be overhung */
                if (sx + bw > sw) bw = sw - sx;
                if (sy + bh > sh) bh = sh - sy;
                if (call == 10 || !layered || rnd() % 4) {
                    LONG dw = call == 10 ? (LONG)bma->BytesPerRow * 8 : (LONG)W, dh = call == 10 ? (LONG)bma->Rows : (LONG)H;
                    if (tx >= dw || ty >= dh) break;
                    if (tx + bw > dw) bw = dw - tx;
                    if (ty + bh > dh) bh = dh - ty;
                }
                if (call == 10) {
                    ra = BltBitMap(sa, sx, sy, bma, tx, ty, bw, bh, mt, bmask, NULL);
                    rb = ogfx_rp_BltBitMap(sb, sx, sy, bmb, tx, ty, bw, bh, mt, bmask, NULL);
                } else {
                    ra = rb = 0;
                    BltBitMapRastPort(sa, sx, sy, &rpa, tx, ty, bw, bh, mt);
                    ogfx_rp_BltBitMapRastPort(sb, sx, sy, &rpb, tx, ty, bw, bh, mt);
                }
                plane = same(&at);
                ASKED("%s %ld,%ld to %ld,%ld size %ld x %ld minterm %02lx", (LONG)(self ? "self" : "source"), sx, sy, tx, ty, bw, bh);
                wa[7] = (LONG)mt;
                if (report(call, ra, rb, plane, at))
                    Printf("  mask %02lx, source %ld x %ld x %ld\n", (LONG)(call == 10 ? bmask : rpa.Mask), sw, sh, (LONG)sa->Depth);
                break;
            }
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
                int usemask = !layered && call == 9 && rnd() % 3 && mbpr * (y1 - y0 + 1) + 4 <= BLTMASK;  /* OS reads up to 2 bytes past a row */
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
                    show_pixels((LONG)x0, (LONG)y0, (LONG)(x1 | 15) < (LONG)W ? (LONG)(x1 | 15) : (LONG)W - 1, (LONG)y1);
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
            chip_kept[sel] += chip0 - (LONG)AvailMem(MEMF_CHIP);
            if (recut) {
                /* the rest of the round can't be judged part by part */
                runs[last_call]--;
                if (recuts++ < 5) {
                    Printf("CUT %s ", (LONG)names[last_call]);
                    VPrintf((STRPTR)what, wa);
                    PutStr(": the two sides' ClipRects no longer pair up (c covered, h hidden); round ended\n");
                    shown_setup = 0; show_setup(); show_cuts(0); show_cuts(1);
                }
                recut = 0;
                break;
            }
            /* keep the two in step after a DIFF, so later calls are judged alone */
            rpb.cp_x = rpa.cp_x; rpb.cp_y = rpa.cp_y; rpb.linpatcnt = rpa.linpatcnt; rpb.Flags = rpa.Flags;
            if (same(&at)) sync_b();
        }
        WaitBlit();
        if (layered) free_layers();
        FreeBitMap(bma); FreeBitMap(bmb); FreeBitMap(tmpbm); FreeBitMap(sbm);
        round_chip -= (LONG)AvailMem(MEMF_CHIP);
        if (round_chip > 0) {
            if (chip_rounds_lost < 5) Printf("round %ld kept %ld bytes of Chip RAM\n", (LONG)r, round_chip);
            chip_rounds_lost++; chip_round_lost += round_chip;
        }
    }
    for (k = 0; k < NCALLS; k++) {
        Printf("%-18s %6lu runs, %lu differ\n", (LONG)names[k], runs[k], diffs[k]);
        total += runs[k]; bad += diffs[k];
    }
    Printf("%lu checks, %lu differ\n", total, bad);
    if (refused) Printf("%lu ClipBlit and ScrollRaster calls left to graphics.library (a simple refresh layer, a clip region, a rectangle not inside the layer or a ScrollRaster shift of its size)\n", refused);
    if (recuts) Printf("%lu rounds ended early: graphics.library cut the two sides' ClipRects differently\n", recuts);
    if (chip_rounds_lost) Printf("%lu rounds kept %ld bytes of Chip RAM in all\n", chip_rounds_lost, chip_round_lost);
    for (k = 0; k < 14; k++)
        if (chip_kept[k]) Printf("call kind %ld kept %ld bytes of Chip RAM until its round ended\n", (LONG)k, chip_kept[k]);
    if (pad_runs) Printf("%lu ReadPixelArray8 calls wrote the row padding differently (not counted)\n", pad_runs);
    FreeVec(arr_a); FreeVec(arr_b); FreeVec(areapat); FreeVec(bltmask);
    if (LayersBase) CloseLibrary(LayersBase);
    if (rd) FreeArgs(rd);
    return bad ? 5 : nochip ? 10 : 0;
}
