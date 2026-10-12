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
 * and in a layer whether OS 3.2 adds damage, which the core never does),
 * AreaMove, AreaDraw, AreaEllipse and AreaEnd (polygons and ellipses, with
 * AREAOUTLINE and area patterns), DrawEllipse, EraseRect, Text (fonts
 * made here, topaz 8 and disk fonts, with algorithmic styles and
 * TxSpacing), TextLength and TextExtent; after a line, an area, an
 * ellipse or text the RastPort's pen position,
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
 *   GfxCoreABITest AREAPROBE    (what OS 3.2's area fills draw, for the
 *                                rules)
 *   GfxCoreABITest AREAPROBE2   (the AreaInfo table, DrawEllipse and
 *                                AreaEllipse sizes, outline modes, EraseRect)
 *   GfxCoreABITest AREAPROBE3   (flat and thin ellipses)
 *   GfxCoreABITest AREAPROBE4   (two-point polygons, pattern phase, outline pen position)
 *   GfxCoreABITest TEXTPROBE    (what OS 3.2's Text draws and measures, for
 *                                the rules)
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
#include <proto/diskfont.h>
#include <graphics/text.h>
#include <string.h>
#include "../library/core/gfx_rp.h"

#define NCALLS 19
static const char *const names[NCALLS] = {
    "WritePixel", "ReadPixel", "SetRast", "WritePixelArray8", "ReadPixelArray8", "WriteChunkyPixels",
    "Draw", "PolyDraw", "RectFill", "BltPattern", "BltBitMap", "BltBitMapRastPort", "ClipBlit", "ScrollRaster",
    "AreaEnd", "DrawEllipse", "EraseRect", "Text", "TextExtent"
};
static ULONG runs[NCALLS], diffs[NCALLS], pad_runs, refused, junk;
static int nochip;
/* Chip RAM a switch case kept (after both sides' calls), and rounds that
 * ended with less Chip RAM than they started with */
static LONG chip_kept[NCALLS], chip_round_lost;
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
/* area fills: a TmpRas the bitmap's size a side, and AreaInfo tables */
static PLANEPTR tras_a, tras_b;
static struct TmpRas tmpras_a, tmpras_b;
static struct AreaInfo ainfo_a, ainfo_b;
static WORD avec_a[5 * 24 / 2], avec_b[5 * 24 / 2];

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

/* B's ClipRect with A's ClipRect's bounds: graphics.library may reorder
 * one side's list (a ScrollRaster or ClipBlit does), so parts pair up by
 * their rectangles, not their places in the lists */
static struct ClipRect *twin(struct ClipRect *ca)
{
    struct ClipRect *cb;
    for (cb = back[1]->ClipRect; cb; cb = cb->Next)
        if (ca->bounds.MinX == cb->bounds.MinX && ca->bounds.MinY == cb->bounds.MinY && ca->bounds.MaxX == cb->bounds.MaxX &&
            ca->bounds.MaxY == cb->bounds.MaxY)
            return cb;
    return NULL;
}

/* 1 when the back layer's ClipRects differ between the sides in number,
 * bounds, being covered or having a bitmap */
static int cuts_differ(void)
{
    struct ClipRect *ca, *cb;
    int na = 0, nb = 0;
    for (ca = back[0]->ClipRect; ca; ca = ca->Next, na++)
        if (!(cb = twin(ca)) || !ca->obscured != !cb->obscured || !ca->BitMap != !cb->BitMap) return 1;
    for (cb = back[1]->ClipRect; cb; cb = cb->Next) nb++;
    return na != nb;
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
    /* the cuts first: with them differing, no comparison is fair */
    if (layered && cuts_differ()) { recut = 1; return 0; }
    if ((p = same_bm(bma, bmb, at))) return p;
    if (!layered) return 0;
    for (ca = back[0]->ClipRect, n = 1; ca; ca = ca->Next, n++)
        if ((cb = twin(ca)) && ca->BitMap && cb->BitMap && ca->obscured && (p = same_part(ca, cb, at))) return 16 * n + p;
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
    for (ca = back[0]->ClipRect; ca; ca = ca->Next)
        if ((cb = twin(ca)) && ca->BitMap && cb->BitMap && ca->obscured)
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
    for (i = 1; i < n && ca; i++) ca = ca->Next;
    if (!ca || !(cb = twin(ca))) return;
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
    for (y = y0; y <= y1 && n < 24; y++)
        for (x = x0; x <= x1 && n < 24; x++) {
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

/* AREAPROBE: what OS 3.2's AreaMove, AreaDraw, AreaEnd, AreaEllipse and
 * EraseRect draw, as pen digits a row, for the rules the core follows.
 * A 2-plane 128 x 48 bitmap; each shape is drawn on pen 0 (or pen 3 for
 * COMPLEMENT and EraseRect) and its box printed with 2 pixels round it.
 * One shape a line: name, the calls' results, cp and Flags after. */
struct ashape { const char *name; WORD n; WORD pts[20]; UBYTE fg, bg, dm, mask, outline, pat; UBYTE pre; };
static const struct ashape ashapes[] = {
    /* pts: n x,y pairs; x -1 starts a new polygon (AreaMove) at the next
     * pair; x -2 is an AreaEllipse of the next two pairs (xc,yc, a,b) */
    { "rect",            4, { 10,5, 30,5, 30,15, 10,15 },                   1, 0, 0, 0xFF, 0, 0, 0 },
    { "rect ccw",        4, { 10,5, 10,15, 30,15, 30,5 },                   1, 0, 0, 0xFF, 0, 0, 0 },
    { "triangle",        3, { 10,5, 40,20, 5,25 },                          1, 0, 0, 0xFF, 0, 0, 0 },
    { "thin diagonal",   3, { 10,5, 40,8, 11,6 },                           1, 0, 0, 0xFF, 0, 0, 0 },
    { "arrow concave",   7, { 5,10, 25,10, 25,4, 40,15, 25,26, 25,20, 5,20 }, 1, 0, 0, 0xFF, 0, 0, 0 },
    { "octagon",         8, { 15,3, 30,3, 40,10, 40,20, 30,27, 15,27, 5,20, 5,10 }, 1, 0, 0, 0xFF, 0, 0, 0 },
    { "two rects",       9, { 10,5, 30,5, 30,15, 10,15, -1,0, 20,10, 40,10, 40,20, 20,20 }, 1, 0, 0, 0xFF, 0, 0, 0 },
    { "bowtie",          4, { 10,5, 40,25, 40,5, 10,25 },                   1, 0, 0, 0xFF, 0, 0, 0 },
    { "horizontal",      2, { 10,10, 40,10 },                               1, 0, 0, 0xFF, 0, 0, 0 },
    { "vertical",        2, { 20,5, 20,25 },                                1, 0, 0, 0xFF, 0, 0, 0 },
    { "point",           1, { 20,10 },                                      1, 0, 0, 0xFF, 0, 0, 0 },
    { "quad steep",      4, { 5,5, 60,7, 58,30, 7,28 },                     1, 0, 0, 0xFF, 0, 0, 0 },
    { "1px wide tri",    3, { 20,5, 21,25, 20,25 },                         1, 0, 0, 0xFF, 0, 0, 0 },
    { "x odd start",     4, { 13,5, 47,9, 33,22, 3,17 },                    1, 0, 0, 0xFF, 0, 0, 0 },
    { "outline rect",    4, { 10,5, 30,5, 30,15, 10,15 },                   1, 0, 0, 0xFF, 2, 0, 0 },
    { "outline tri",     3, { 10,5, 40,20, 5,25 },                          1, 0, 0, 0xFF, 2, 0, 0 },
    { "outline concave", 7, { 5,10, 25,10, 25,4, 40,15, 25,26, 25,20, 5,20 }, 1, 0, 0, 0xFF, 2, 0, 0 },
    { "pattern jam2",    4, { 10,5, 40,5, 40,14, 10,14 },                   1, 2, 1, 0xFF, 0, 1, 0 },
    { "pattern jam2 y+1",4, { 10,6, 40,6, 40,15, 10,15 },                   1, 2, 1, 0xFF, 0, 1, 0 },
    { "pattern jam1 tri",3, { 10,5, 40,20, 5,25 },                          2, 1, 0, 0xFF, 0, 1, 0 },
    { "pattern inv",     3, { 10,5, 40,20, 5,25 },                          1, 2, 5, 0xFF, 0, 1, 0 },
    { "complement",      3, { 10,5, 40,20, 5,25 },                          1, 0, 2, 0xFF, 0, 0, 3 },
    { "jam2 solid",      3, { 10,5, 40,20, 5,25 },                          1, 2, 1, 0xFF, 0, 0, 3 },
    { "mask 2 pen 3",    3, { 10,5, 40,20, 5,25 },                          3, 0, 0, 0x02, 0, 0, 0 },
    { "outline mask 1",  3, { 10,5, 40,20, 5,25 },                          3, 0, 0, 0x01, 2, 0, 0 },
    { "ellipse",         3, { -2,0, 30,15, 20,8 },                          1, 0, 0, 0xFF, 0, 0, 0 },
    { "ellipse small",   3, { -2,0, 30,15, 3,2 },                           1, 0, 0, 0xFF, 0, 0, 0 },
    { "ellipse 1x1",     3, { -2,0, 30,15, 1,1 },                           1, 0, 0, 0xFF, 0, 0, 0 },
    { "ellipse outline", 3, { -2,0, 30,15, 12,6 },                          1, 0, 0, 0xFF, 2, 0, 0 },
    { "ellipse + tri",   7, { -2,0, 30,15, 12,6, -1,0, 10,5, 50,10, 20,25 }, 1, 0, 0, 0xFF, 0, 0, 0 },
};
static UWORD apat[2] = { 0xAAAA, 0xF0F0 };

static void grid(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1)
{
    LONG x, y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > 127) x1 = 127;
    if (y1 > 47) y1 = 47;
    Printf("   x %ld..%ld\n", x0, x1);
    for (y = y0; y <= y1; y++) {
        char line[140];
        int k = 0;
        for (x = x0; x <= x1; x++) line[k++] = (char)('0' + ReadPixel(rp, x, y));
        line[k] = 0;
        Printf("%3ld %s\n", y, (LONG)line);
    }
    Flush(Output());
}

static int areaprobe(void)
{
    struct BitMap *bm = AllocBitMap(128, 48, 2, BMF_CLEAR, NULL);
    PLANEPTR tr = AllocRaster(128, 48);
    WORD *vb = AllocVec(5 * 24, MEMF_CLEAR);
    struct RastPort rp;
    struct AreaInfo ai;
    struct TmpRas tmp;
    ULONG s;
    int i;

    if (!bm || !tr || !vb) {
        PutStr("no Chip RAM\n");
        if (bm) FreeBitMap(bm);
        if (tr) FreeRaster(tr, 128, 48);
        if (vb) FreeVec(vb);
        return 20;
    }
    Printf("AREAPROBE, %s", (LONG)((struct Library *)GfxBase)->lib_IdString);
    InitRastPort(&rp); rp.BitMap = bm;
    InitTmpRas(&tmp, tr, RASSIZE(128, 48));
    rp.TmpRas = &tmp;
    for (s = 0; s < sizeof ashapes / sizeof ashapes[0]; s++) {
        const struct ashape *a = &ashapes[s];
        LONG minx = 127, miny = 47, maxx = 0, maxy = 0, r, poly = 1;
        InitArea(&ai, vb, 24);
        rp.AreaInfo = &ai;
        SetWriteMask(&rp, 0xFF); SetDrMd(&rp, JAM1); SetAfPt(&rp, NULL, 0);
        rp.Flags &= ~AREAOUTLINE;
        SetRast(&rp, a->pre);
        SetAPen(&rp, a->fg); SetBPen(&rp, a->bg); SetDrMd(&rp, a->dm); SetWriteMask(&rp, a->mask);
        if (a->outline) SetOutlinePen(&rp, a->outline);
        if (a->pat) SetAfPt(&rp, apat, 1);
        Printf("== %s:", (LONG)a->name);
        for (i = 0; i < 2 * a->n; i += 2) {
            LONG x = a->pts[i], y = a->pts[i + 1];
            if (x == -1) { poly = 1; continue; }
            if (x == -2) {
                LONG xc = a->pts[i + 2], yc = a->pts[i + 3], ea = a->pts[i + 4], eb = a->pts[i + 5];
                r = AreaEllipse(&rp, xc, yc, ea, eb);
                Printf(" E%ld", r);
                if (xc - ea < minx) minx = xc - ea;
                if (yc - eb < miny) miny = yc - eb;
                if (xc + ea > maxx) maxx = xc + ea;
                if (yc + eb > maxy) maxy = yc + eb;
                i += 4;
                continue;
            }
            if (x < minx) minx = x;
            if (y < miny) miny = y;
            if (x > maxx) maxx = x;
            if (y > maxy) maxy = y;
            r = poly ? AreaMove(&rp, x, y) : AreaDraw(&rp, x, y);
            Printf(" %s%ld", (LONG)(poly ? "M" : "D"), r);
            poly = 0;
        }
        r = AreaEnd(&rp);
        WaitBlit();
        Printf(" End%ld cp %ld,%ld Flags $%lx Count %ld\n", r, (LONG)rp.cp_x, (LONG)rp.cp_y, (LONG)rp.Flags, (LONG)ai.Count);
        grid(&rp, minx - 2, miny - 2, maxx + 2, maxy + 2);
    }

    FreeVec(vb);
    FreeRaster(tr, 128, 48);
    FreeBitMap(bm);
    return 0;
}

/* AREAPROBE2: the rest, for the rules. AreaInfo: the vector table and flag
 * bytes AreaMove, AreaDraw and AreaEllipse leave, and what fits in how
 * AreaMove, AreaDraw and AreaEllipse leave; DrawEllipse and AreaEllipse
 * at many sizes (the flat ones last), each row's pixels relative to the
 * centre; the outline in JAM2 with a line pattern, in COMPLEMENT and with
 * an area pattern; EraseRect; last, what fits in how many vectors (a full
 * table may be what crashed OS 3.2 before). Each part is flushed as it
 * ends, so a crash keeps what came before it. */
static void ell_rows(struct RastPort *rp, LONG b)
{
    LONG y;
    for (y = 50 - b - 1; y <= 50 + b + 1; y++) {
        Printf(" %ld:", y - 50);
        ranges(rp, y, 0, 159, 80);
    }
    PutStr("\n");
    Flush(Output());
}

static void fill_count(struct RastPort *rp)
{
    LONG x, y, n = 0, x0 = 999, x1 = -1, y0 = 999, y1 = -1;
    for (y = 0; y < 100; y++)
        for (x = 0; x < 160; x++)
            if (ReadPixel(rp, x, y)) {
                n++;
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    Printf(" drew %ld", n);
    if (n) Printf(" in %ld,%ld-%ld,%ld", x0, y0, x1, y1);
    PutStr("\n");
    Flush(Output());
}

static int areaprobe2(void)
{
    static const WORD esz[][2] = {
        {1,1},{2,1},{1,2},{2,2},{3,1},{3,2},{2,3},{3,3},{4,1},{4,2},{4,3},{4,4},{5,2},{5,3},{5,5},
        {6,2},{6,4},{7,3},{8,2},{9,4},{12,6},{6,12},{20,8},{8,20},{17,11},{25,25},{30,10},{40,3},{3,40},{60,45},
        {1,0},{0,1},{0,0}
    };
    struct BitMap *bm = AllocBitMap(160, 100, 2, BMF_CLEAR, NULL);
    PLANEPTR tr = AllocRaster(160, 100);
    WORD *vb = AllocVec(5 * 32, MEMF_CLEAR);
    struct RastPort rp;
    struct AreaInfo ai;
    struct TmpRas tmp;
    ULONG s;
    int i, n;

    if (!bm || !tr || !vb) {
        PutStr("no Chip RAM\n");
        if (bm) FreeBitMap(bm);
        if (tr) FreeRaster(tr, 160, 100);
        if (vb) FreeVec(vb);
        return 20;
    }
    Printf("AREAPROBE2, %s", (LONG)((struct Library *)GfxBase)->lib_IdString);
    InitRastPort(&rp); rp.BitMap = bm;
    InitTmpRas(&tmp, tr, RASSIZE(160, 100));
    rp.TmpRas = &tmp;
    SetAPen(&rp, 1);

    /* The table: InitArea for 10 vectors on a buffer of $55, then each call */
    {
        static const char *const what[] = { "Move 1,2", "Draw 3,4", "Draw 5,6", "Move 7,8", "Move 9,10", "Draw 11,12", "Ellipse 13,14 15,16", "Draw 17,18" };
        memset(vb, 0x55, 5 * 32);
        InitArea(&ai, vb, 10);
        rp.AreaInfo = &ai;
        Printf("== table: VctrTbl +%ld FlagTbl +%ld MaxCount %ld Count %ld\n", (LONG)((UBYTE *)ai.VctrTbl - (UBYTE *)vb),
               (LONG)((UBYTE *)ai.FlagTbl - (UBYTE *)vb), (LONG)ai.MaxCount, (LONG)ai.Count);
        for (i = 0; i < 8; i++) {
            LONG r, k;
            switch (i) {
            case 0: r = AreaMove(&rp, 1, 2); break;
            case 1: r = AreaDraw(&rp, 3, 4); break;
            case 2: r = AreaDraw(&rp, 5, 6); break;
            case 3: r = AreaMove(&rp, 7, 8); break;
            case 4: r = AreaMove(&rp, 9, 10); break;
            case 5: r = AreaDraw(&rp, 11, 12); break;
            case 6: r = AreaEllipse(&rp, 13, 14, 15, 16); break;
            default: r = AreaDraw(&rp, 17, 18); break;
            }
            Printf("%s: %ld Count %ld First %ld,%ld VctrPtr +%ld FlagPtr +%ld\n  words", (LONG)what[i], r, (LONG)ai.Count,
                   (LONG)ai.FirstX, (LONG)ai.FirstY, (LONG)((UBYTE *)ai.VctrPtr - (UBYTE *)ai.VctrTbl),
                   (LONG)((UBYTE *)ai.FlagPtr - (UBYTE *)ai.FlagTbl));
            for (k = 0; k < 20; k++) Printf(" %ld", (LONG)ai.VctrTbl[k]);
            PutStr("\n  flags");
            for (k = 0; k < 10; k++) Printf(" $%02lx", (LONG)(UBYTE)ai.FlagTbl[k]);
            PutStr("\n");
            Flush(Output());
        }
        InitArea(&ai, vb, 10);
    }

    InitArea(&ai, vb, 32);
    rp.AreaInfo = &ai;

    /* DrawEllipse, then AreaEllipse, at (80,50) */
    for (s = 0; s < sizeof esz / sizeof esz[0]; s++) {
        LONG a = esz[s][0], b = esz[s][1];
        SetRast(&rp, 0);
        DrawEllipse(&rp, 80, 50, a, b);
        WaitBlit();
        Printf("== DrawEllipse %ld,%ld cp %ld,%ld Flags $%lx\n", a, b, (LONG)rp.cp_x, (LONG)rp.cp_y, (LONG)rp.Flags);
        ell_rows(&rp, b);
        SetRast(&rp, 0);
        Printf("== AreaEllipse %ld,%ld: %ld", a, b, AreaEllipse(&rp, 80, 50, a, b));
        Printf(" End %ld\n", AreaEnd(&rp));
        WaitBlit();
        ell_rows(&rp, b);
    }

    /* The outline's modes: a triangle with AOlPen 2 */
    for (s = 0; s < 5; s++) {
        static const char *const nm[] = { "JAM2 LinePtrn $F0F0", "COMPLEMENT on pen 3", "JAM1 LinePtrn $F0F0", "INVERSVID|JAM2 LinePtrn $FF00", "area pattern JAM2" };
        SetWriteMask(&rp, 0xFF);
        SetRast(&rp, s == 1 ? 3 : 0);
        SetAPen(&rp, 1); SetBPen(&rp, 3); SetDrMd(&rp, JAM1); SetAfPt(&rp, NULL, 0);
        rp.LinePtrn = 0xFFFF; rp.linpatcnt = 15;
        switch (s) {
        case 0: SetDrMd(&rp, JAM2); SetDrPt(&rp, 0xF0F0); break;
        case 1: SetDrMd(&rp, COMPLEMENT); break;
        case 2: SetDrPt(&rp, 0xF0F0); break;
        case 3: SetDrMd(&rp, INVERSVID | JAM2); SetDrPt(&rp, 0xFF00); break;
        default: SetDrMd(&rp, JAM2); SetAfPt(&rp, apat, 1); break;
        }
        SetOutlinePen(&rp, 2);
        AreaMove(&rp, 10, 5); AreaDraw(&rp, 40, 20); AreaDraw(&rp, 5, 25);
        Printf("== outline %s: End %ld", (LONG)nm[s], AreaEnd(&rp));
        WaitBlit();
        Printf(" cp %ld,%ld Flags $%lx linpatcnt %ld\n", (LONG)rp.cp_x, (LONG)rp.cp_y, (LONG)rp.Flags, (LONG)rp.linpatcnt);
        grid(&rp, 3, 3, 42, 27);
        rp.Flags &= ~AREAOUTLINE;
    }
    SetDrMd(&rp, JAM1); SetAfPt(&rp, NULL, 0); rp.LinePtrn = 0xFFFF;

    /* EraseRect unlayered: on pen 3, BgPen 1, JAM1 and Mask $FF, then
     * JAM2 and Mask $01 */
    for (i = 0; i < 2; i++) {
        SetWriteMask(&rp, 0xFF);
        SetRast(&rp, 3);
        SetAPen(&rp, 2); SetBPen(&rp, 1); SetDrMd(&rp, i ? JAM2 : JAM1); SetWriteMask(&rp, i ? 0x01 : 0xFF);
        EraseRect(&rp, 5, 4, 20, 9);
        WaitBlit();
        Printf("== EraseRect unlayered, %s mask $%lx\n", (LONG)(i ? "JAM2" : "JAM1"), (LONG)(i ? 0x01 : 0xFF));
        grid(&rp, 3, 2, 22, 11);
    }
    /* EraseRect in a simple and a smart layer, with the default backfill,
     * layer at 10,4: the rect is layer-relative, and partly outside */
    if ((LayersBase = OpenLibrary("layers.library", 39))) {
        ULONG fl[2] = { LAYERSIMPLE, LAYERSMART };
        for (i = 0; i < 2; i++) {
            struct Layer_Info *info = NewLayerInfo();
            struct Layer *l;
            SetWriteMask(&rp, 0xFF);
            SetRast(&rp, 3);
            if (!info) break;
            if ((l = CreateUpfrontLayer(info, bm, 10, 4, 49, 23, fl[i], NULL))) {
                SetAPen(l->rp, 2); SetBPen(l->rp, 1); SetWriteMask(l->rp, 0xFF);
                EraseRect(l->rp, 4, 2, 15, 7);
                EraseRect(l->rp, 30, 15, 60, 40);
                WaitBlit();
                Printf("== EraseRect in a %s layer 10,4-49,23: 4,2-15,7 and 30,15-60,40\n", (LONG)(i ? "smart" : "simple"));
                grid(&rp, 8, 2, 51, 25);
                DeleteLayer(0, l);
            }
            DisposeLayerInfo(info);
        }
        CloseLibrary(LayersBase);
        LayersBase = NULL;
    }
    /* What fits: a shape in n vectors, for n round its size */
    SetWriteMask(&rp, 0xFF);
    SetDrMd(&rp, JAM1);
    SetAPen(&rp, 1);
    for (s = 0; s < 6; s++) {
        static const char *const nm[] = { "rect 4 points", "rect closed 5 points", "two rects", "ellipse", "ellipse + rect", "move move rect" };
        for (n = 1; n <= 11; n++) {
            LONG r[12], k = 0, m;
            SetRast(&rp, 0);
            InitArea(&ai, vb, n);
            rp.AreaInfo = &ai;
            switch (s) {
            case 0: case 1: case 2: case 4: case 5:
                if (s == 4) r[k++] = AreaEllipse(&rp, 80, 50, 10, 5);
                if (s == 5) r[k++] = AreaMove(&rp, 1, 1);
                r[k++] = AreaMove(&rp, 10, 10); r[k++] = AreaDraw(&rp, 30, 10); r[k++] = AreaDraw(&rp, 30, 20); r[k++] = AreaDraw(&rp, 10, 20);
                if (s == 1) r[k++] = AreaDraw(&rp, 10, 10);
                if (s == 2) { r[k++] = AreaMove(&rp, 40, 30); r[k++] = AreaDraw(&rp, 60, 30); r[k++] = AreaDraw(&rp, 60, 40); r[k++] = AreaDraw(&rp, 40, 40); }
                break;
            default:
                r[k++] = AreaEllipse(&rp, 80, 50, 10, 5);
                break;
            }
            Printf("== fits %s in %ld:", (LONG)nm[s], (LONG)n);
            for (m = 0; m < k; m++) Printf(" %ld", r[m]);
            {
                LONG before = ai.Count, end = AreaEnd(&rp);
                Printf(" Count %ld, End %ld Count %ld", before, end, (LONG)ai.Count);
            }
            WaitBlit();
            fill_count(&rp);
        }
    }
    FreeVec(vb);
    FreeRaster(tr, 160, 100);
    FreeBitMap(bm);
    return 0;
}

/* AREAPROBE3: DrawEllipse and AreaEllipse on flat and thin ellipses (a or
 * b of 0 to 2) and every size up to 6 x 6, rows relative to the centre */
static int areaprobe3(void)
{
    static const WORD big[] = { 8, 12, 16, 26, 34 };
    struct BitMap *bm = AllocBitMap(160, 100, 1, BMF_CLEAR, NULL);
    PLANEPTR tr = AllocRaster(160, 100);
    WORD *vb = AllocVec(5 * 8, MEMF_CLEAR);
    struct RastPort rp;
    struct AreaInfo ai;
    struct TmpRas tmp;
    LONG a, b, i, k;

    if (!bm || !tr || !vb) {
        PutStr("no Chip RAM\n");
        if (bm) FreeBitMap(bm);
        if (tr) FreeRaster(tr, 160, 100);
        if (vb) FreeVec(vb);
        return 20;
    }
    Printf("AREAPROBE3, %s", (LONG)((struct Library *)GfxBase)->lib_IdString);
    InitRastPort(&rp); rp.BitMap = bm;
    InitTmpRas(&tmp, tr, RASSIZE(160, 100));
    rp.TmpRas = &tmp;
    InitArea(&ai, vb, 8);
    rp.AreaInfo = &ai;
    SetAPen(&rp, 1);
    for (k = 0; k < 49 + 4 * 5 * 3; k++) {
        if (k < 49) { a = k / 7; b = k % 7; }
        else {
            i = k - 49;
            a = big[i % 5]; b = i / 5 % 3;
            if (i >= 15) { a = i / 5 % 3; b = big[i % 5]; }
            if (i >= 30) { a = big[i % 5]; b = 1 + i / 5 % 3; if (i >= 45) { b = big[i % 5]; a = 1 + i / 5 % 3; } }
            if (b > 45 || a > 75) continue;
        }
        SetRast(&rp, 0);
        DrawEllipse(&rp, 80, 50, a, b);
        WaitBlit();
        Printf("== DrawEllipse %ld,%ld\n", a, b);
        ell_rows(&rp, b);
        SetRast(&rp, 0);
        Printf("== AreaEllipse %ld,%ld: %ld", a, b, AreaEllipse(&rp, 80, 50, a, b));
        Printf(" End %ld\n", AreaEnd(&rp));
        WaitBlit();
        ell_rows(&rp, b);
    }
    FreeVec(vb);
    FreeRaster(tr, 160, 100);
    FreeBitMap(bm);
    return 0;
}

/* AREAPROBE4: the cases the random test found: two-point polygons that are
 * not level or upright, the area pattern's phase with four rows, and the
 * pen position after an outline that ends on a lone AreaMove or has a move
 * before an ellipse. ops: 'M' x,y, 'D' x,y, 'E' xc,yc,a,b, 0 ends */
struct aprobe4 {
    const char *name;
    WORD sz, outline;
    WORD ops[40];
};
static const struct aprobe4 aprobe4s[] = {
    { "two points 14,38 18,28",           -1, 0, { 'M',14,38, 'D',18,28, 0 } },
    { "two points 10,5 30,12",            -1, 0, { 'M',10,5, 'D',30,12, 0 } },
    { "two points 10,5 12,20",            -1, 0, { 'M',10,5, 'D',12,20, 0 } },
    { "two points 30,12 10,5",            -1, 0, { 'M',30,12, 'D',10,5, 0 } },
    { "lone move then two points",        -1, 0, { 'M',60,24, 'M',14,38, 'D',18,28, 0 } },
    { "two points then a triangle",       -1, 0, { 'M',10,5, 'D',30,12, 'M',40,5, 'D',60,10, 'D',45,20, 0 } },
    { "three points on a line",           -1, 0, { 'M',10,5, 'D',20,10, 'D',30,15, 0 } },
    { "four points there and back",       -1, 0, { 'M',10,5, 'D',30,12, 'D',50,5, 'D',30,12, 0 } },
    { "pattern 4 rows rect y 1",           2, 0, { 'M',3,1, 'D',40,1, 'D',40,8, 'D',3,8, 0 } },
    { "pattern 4 rows rect y 2",           2, 0, { 'M',3,2, 'D',40,2, 'D',40,9, 'D',3,9, 0 } },
    { "pattern 4 rows rect y 3",           2, 0, { 'M',3,3, 'D',40,3, 'D',40,10, 'D',3,10, 0 } },
    { "pattern 4 rows triangle y 5",       2, 0, { 'M',10,5, 'D',40,15, 'D',5,18, 0 } },
    { "pattern 4 rows two points",         2, 0, { 'M',14,38, 'D',18,28, 0 } },
    { "pattern 2 rows two points",         1, 0, { 'M',14,38, 'D',18,28, 0 } },
    { "pattern 4 rows two polygons",       2, 0, { 'M',10,5, 'D',30,6, 'D',20,14, 'M',35,21, 'D',50,30, 'D',38,33, 0 } },
    { "pattern 8 rows two points",         3, 0, { 'M',14,38, 'D',18,28, 0 } },
    { "pattern 16 rows two points",        4, 0, { 'M',14,38, 'D',18,28, 0 } },
    { "pattern 8 rows rect y 1",           3, 0, { 'M',3,1, 'D',40,1, 'D',40,8, 'D',3,8, 0 } },
    { "no pattern, AreaPtSz 2, two points", -2, 0, { 'M',14,38, 'D',18,28, 0 } },
    { "pattern 1 row two points",          0, 0, { 'M',14,38, 'D',18,28, 0 } },
    { "pattern 4 rows ellipse",            2, 0, { 'E',30,20,12,6, 0 } },
    { "outline, lone move last",          -1, 1, { 'M',10,5, 'D',40,15, 'D',5,18, 'M',50,30, 0 } },
    { "outline, move then ellipse",       -1, 1, { 'M',10,5, 'D',40,15, 'D',5,18, 'M',50,30, 'E',30,30,8,4, 0 } },
    { "outline, ellipse then lone move",  -1, 1, { 'E',30,30,8,4, 'M',50,30, 0 } },
    { "outline, two points",              -1, 1, { 'M',14,38, 'D',18,28, 0 } },
    { "no outline, lone move last",       -1, 0, { 'M',10,5, 'D',40,15, 'D',5,18, 'M',50,30, 0 } },
    { "two points, move, ellipse",        -1, 0, { 'M',26,35, 'D',46,34, 'M',45,19, 'E',20,31,10,10, 0 } },
    { "two points, move, ellipse, JAM2|COMPLEMENT", -3, 0, { 'M',26,35, 'D',46,34, 'M',45,19, 'E',20,31,10,10, 0 } },
};
static UWORD apat4[16] = { 0xFF00, 0x0FF0, 0x00FF, 0xF00F, 0xFF00, 0x0FF0, 0x00FF, 0xF00F,
                           0xFF00, 0x0FF0, 0x00FF, 0xF00F, 0xFF00, 0x0FF0, 0x00FF, 0xF00F };

static int areaprobe4(void)
{
    struct BitMap *bm = AllocBitMap(128, 48, 2, BMF_CLEAR, NULL);
    PLANEPTR tr = AllocRaster(128, 48);
    WORD *vb = AllocVec(5 * 24, MEMF_CLEAR);
    struct RastPort rp;
    struct AreaInfo ai;
    struct TmpRas tmp;
    ULONG s;
    int i;

    if (!bm || !tr || !vb) {
        PutStr("no Chip RAM\n");
        if (bm) FreeBitMap(bm);
        if (tr) FreeRaster(tr, 128, 48);
        if (vb) FreeVec(vb);
        return 20;
    }
    Printf("AREAPROBE4, %s", (LONG)((struct Library *)GfxBase)->lib_IdString);
    InitRastPort(&rp); rp.BitMap = bm;
    InitTmpRas(&tmp, tr, RASSIZE(128, 48));
    rp.TmpRas = &tmp;
    for (s = 0; s < sizeof aprobe4s / sizeof aprobe4s[0]; s++) {
        const struct aprobe4 *a = &aprobe4s[s];
        LONG minx = 127, miny = 47, maxx = 0, maxy = 0, r;
        InitArea(&ai, vb, 24);
        rp.AreaInfo = &ai;
        SetWriteMask(&rp, 0xFF); SetDrMd(&rp, JAM1); SetAfPt(&rp, NULL, 0);
        rp.Flags &= ~AREAOUTLINE;
        SetRast(&rp, 0);
        Move(&rp, 0, 0);
        SetAPen(&rp, 1);
        if (a->outline) SetOutlinePen(&rp, 2);
        if (a->sz >= 0) SetAfPt(&rp, apat4, a->sz);
        if (a->sz == -2) rp.AreaPtSz = 2;
        if (a->sz == -3) SetDrMd(&rp, JAM2 | COMPLEMENT);
        Printf("== %s:", (LONG)a->name);
        for (i = 0; a->ops[i]; ) {
            LONG x = a->ops[i + 1], y = a->ops[i + 2], ea = 0, eb = 0;
            if (a->ops[i] == 'E') {
                ea = a->ops[i + 3]; eb = a->ops[i + 4];
                r = AreaEllipse(&rp, x, y, ea, eb);
                Printf(" E%ld", r);
                i += 5;
            } else {
                r = a->ops[i] == 'M' ? AreaMove(&rp, x, y) : AreaDraw(&rp, x, y);
                Printf(" %s%ld", (LONG)(a->ops[i] == 'M' ? "M" : "D"), r);
                i += 3;
            }
            if (x - ea < minx) minx = x - ea;
            if (y - eb < miny) miny = y - eb;
            if (x + ea > maxx) maxx = x + ea;
            if (y + eb > maxy) maxy = y + eb;
        }
        Printf(" table Count %ld flags", (LONG)ai.Count);
        for (i = 0; i < ai.Count; i++) Printf(" $%02lx", (LONG)(UBYTE)ai.FlagTbl[i]);
        r = AreaEnd(&rp);
        WaitBlit();
        Printf(" End%ld cp %ld,%ld cnt %ld Flags $%lx\n", r, (LONG)rp.cp_x, (LONG)rp.cp_y, (LONG)rp.linpatcnt, (LONG)rp.Flags);
        grid(&rp, minx - 2, miny - 2, maxx + 2, maxy + 2);
    }
    FreeVec(vb);
    FreeRaster(tr, 128, 48);
    FreeBitMap(bm);
    return 0;
}

/* ---- text ----------------------------------------------------------------
 * The fonts Text is tried with: four made here (Chip RAM), glyphs A to E
 * and the default glyph, whose shapes say which glyph and row drew a
 * pixel: F0 proportional with CharSpace and CharKern (some kerning
 * negative), F1 the same arrays without FPF_PROPORTIONAL, F2 neither
 * array, F3 BoldSmear 2, the baseline on the last row and FSF_BOLD in
 * tf_Style; then topaz 8 from ROM and whichever disk fonts open. */
#define NFONTS 10
struct tfont { struct TextFont *tf; const char *name; int disk, made; };
static struct tfont fonts[NFONTS];
static int nfonts;
struct Library *DiskfontBase;
static const UWORD mk_loc[12] = { 0, 5, 5, 3, 8, 7, 15, 1, 16, 8, 24, 4 };
static const WORD mk_space[6] = { 6, 4, 8, 2, 9, 5 }, mk_kern[6] = { 0, -1, 2, 1, -2, 0 };

static struct TextFont *make_font(int v)
{
    UBYTE *mem = AllocVec(sizeof(struct TextFont) + 7 * 6 + 24 + 12 + 12, MEMF_CHIP | MEMF_CLEAR);
    struct TextFont *tf = (struct TextFont *)mem;
    UBYTE *data;
    UWORD *loc;
    WORD *sp, *kn;
    int r, g, k;
    if (!mem) return NULL;
    data = mem + sizeof(struct TextFont);
    loc = (UWORD *)(data + 42);
    sp = (WORD *)(data + 66);
    kn = (WORD *)(data + 78);
    for (k = 0; k < 12; k++) loc[k] = mk_loc[k];
    for (k = 0; k < 6; k++) { sp[k] = mk_space[k]; kn[k] = mk_kern[k]; }
    for (r = 0; r < 7; r++)
        for (g = 0; g < 6; g++)
            for (k = 0; k < loc[2 * g + 1]; k++)
                if (k == 0 || (r + g) % 3)
                    data[r * 6 + (loc[2 * g] + k) / 8] |= (UBYTE)(0x80 >> ((loc[2 * g] + k) % 8));
    tf->tf_YSize = 7; tf->tf_XSize = 6; tf->tf_Baseline = 5; tf->tf_BoldSmear = 1;
    tf->tf_LoChar = 'A'; tf->tf_HiChar = 'E';
    tf->tf_CharData = data; tf->tf_Modulo = 6; tf->tf_CharLoc = loc;
    tf->tf_Flags = FPF_PROPORTIONAL | FPF_DESIGNED;
    tf->tf_CharSpace = sp; tf->tf_CharKern = kn;
    if (v == 1) tf->tf_Flags = FPF_DESIGNED;
    if (v == 2) { tf->tf_Flags = FPF_DESIGNED; tf->tf_CharSpace = tf->tf_CharKern = NULL; }
    if (v == 3) { tf->tf_BoldSmear = 2; tf->tf_Baseline = 6; tf->tf_Style = FSF_BOLD; }
    return tf;
}

static void open_fonts(void)
{
    static const char *const made[4] = { "F0 proportional", "F1 arrays, not proportional", "F2 no arrays", "F3 smear 2, baseline 6, bold" };
    static const char *const disk[5] = { "topaz.font", "helvetica.font", "times.font", "courier.font", "garnet.font" };
    static const UWORD dsize[5] = { 11, 13, 18, 13, 16 };
    struct TextAttr ta;
    int i;
    for (i = 0; i < 4; i++)
        if ((fonts[nfonts].tf = make_font(i))) { fonts[nfonts].name = made[i]; fonts[nfonts].made = 1; nfonts++; }
    ta.ta_Name = (STRPTR)"topaz.font"; ta.ta_YSize = 8; ta.ta_Style = 0; ta.ta_Flags = FPF_ROMFONT;
    if ((fonts[nfonts].tf = OpenFont(&ta))) fonts[nfonts++].name = "topaz 8";
    if (!(DiskfontBase = OpenLibrary("diskfont.library", 36))) return;
    for (i = 0; i < 5 && nfonts < NFONTS; i++) {
        ta.ta_Name = (STRPTR)disk[i]; ta.ta_YSize = dsize[i]; ta.ta_Style = 0; ta.ta_Flags = FPF_DISKFONT;
        if ((fonts[nfonts].tf = OpenDiskFont(&ta)) && fonts[nfonts].tf->tf_YSize == dsize[i]) {
            fonts[nfonts].name = disk[i]; fonts[nfonts].disk = 1; nfonts++;
        } else if (fonts[nfonts].tf) CloseFont(fonts[nfonts].tf);
    }
}

static void close_fonts(void)
{
    int i;
    for (i = 0; i < nfonts; i++) {
        if (fonts[i].made) FreeVec(fonts[i].tf);
        else CloseFont(fonts[i].tf);
    }
    nfonts = 0;
    if (DiskfontBase) CloseLibrary(DiskfontBase);
    DiskfontBase = NULL;
}

static void show_font(int i)
{
    struct TextFont *f = fonts[i].tf;
    Printf("font %ld %s: YSize %ld XSize %ld Baseline %ld BoldSmear %ld Style $%02lx Flags $%02lx chars %ld-%ld",
           (LONG)i, (LONG)fonts[i].name, (LONG)f->tf_YSize, (LONG)f->tf_XSize, (LONG)f->tf_Baseline,
           (LONG)f->tf_BoldSmear, (LONG)f->tf_Style, (LONG)f->tf_Flags, (LONG)f->tf_LoChar, (LONG)f->tf_HiChar);
    Printf("%s%s\n", (LONG)(f->tf_CharSpace ? " space" : ""), (LONG)(f->tf_CharKern ? " kern" : ""));
}

/* A made font's arrays and its message's reply port (where an extended
 * font keeps its TextFontExtension), to see what graphics.library adds */
static void dump_font(int i, const char *when)
{
    struct TextFont *f = fonts[i].tf;
    int k;
    Printf("font %ld %s: Style $%02lx Flags $%02lx XSize %ld ReplyPort $%08lx CharSpace $%08lx", (LONG)i, (LONG)when,
           (LONG)f->tf_Style, (LONG)f->tf_Flags, (LONG)f->tf_XSize, (LONG)f->tf_Message.mn_ReplyPort, (LONG)f->tf_CharSpace);
    if (f->tf_CharSpace) for (k = 0; k < 8; k++) Printf(" %ld", (LONG)((WORD *)f->tf_CharSpace)[k]);
    Printf(" CharKern $%08lx", (LONG)f->tf_CharKern);
    if (f->tf_CharKern) for (k = 0; k < 8; k++) Printf(" %ld", (LONG)((WORD *)f->tf_CharKern)[k]);
    PutStr("\n");
}

/* TEXTPROBE: each font, style and string drawn JAM2 pen 1 on pen 2 over
 * pen 0 = '.', so '#' is a glyph pixel, '.' the box JAM2 fills and ' '
 * untouched; with what TextLength, TextExtent and the pen say. The core
 * draws the same on a second bitmap; a grid of its own follows where it
 * differs. */
struct tprobe { int font; UBYTE style, direct, mode; BYTE spacing; const char *s; };
static const struct tprobe tprobes[] = {
    { 0, 0, 0, JAM2, 0, "ABCDE" }, { 0, FSF_BOLD, 0, JAM2, 0, "ABCDE" }, { 0, FSF_ITALIC, 0, JAM2, 0, "ABCDE" },
    { 0, FSF_UNDERLINED, 0, JAM2, 0, "ABCDE" }, { 0, 7, 0, JAM2, 0, "ABCDE" }, { 0, 0, 0, JAM2, 0, "EDBA" },
    { 0, 0, 0, JAM2, 0, "AZ@C" }, { 0, 0, 0, JAM2, 2, "ABCDE" }, { 0, FSF_BOLD, 0, JAM2, -1, "ABCDE" },
    { 0, 0, 0, JAM2, 0, "D" }, { 0, FSF_ITALIC, 0, JAM2, 0, "D" }, { 0, FSF_UNDERLINED, 0, JAM2, 0, "D" },
    { 0, 0, 0, JAM1, 0, "ABCDE" }, { 0, 0, 0, JAM1 | INVERSVID, 0, "ABCDE" }, { 0, 0, 0, JAM2 | INVERSVID, 0, "ABCDE" },
    { 0, 0, 0, COMPLEMENT, 0, "ABCDE" }, { 0, 0, 0, COMPLEMENT | JAM2, 0, "ABCDE" },
    { 1, 0, 0, JAM2, 0, "ABCDE" }, { 1, FSF_ITALIC, 0, JAM2, 0, "ABCDE" },
    { 2, 0, 0, JAM2, 0, "ABCDE" }, { 2, 7, 0, JAM2, 0, "ABCDE" }, { 2, 0, 0, JAM2, 0, "AZ@C" }, { 2, 0, 0, JAM2, 0, "ZZZ" },
    { 2, 0, 0, JAM2, -1, "ZAZE" },
    { 3, FSF_BOLD, 0, JAM2, 0, "ABCDE" }, { 3, FSF_BOLD, 1, JAM2, 0, "ABCDE" }, { 3, 5, 0, JAM2, 0, "ABCDE" }, { 3, 7, 1, JAM2, 0, "ABCDE" },
    { 4, 0, 0, JAM2, 0, "Wj_g" }, { 4, FSF_BOLD, 0, JAM2, 0, "Wj_g" }, { 4, FSF_ITALIC, 0, JAM2, 0, "Wj_g" },
    { 4, FSF_UNDERLINED, 0, JAM2, 0, "Wj_g" }, { 4, 7, 0, JAM2, 0, "Wj_g" }, { 4, 0, 0, JAM2, 0, "a\x7f\x80\xff" },
    { 5, 0, 0, JAM2, 0, "AVjy" }, { 5, 7, 0, JAM2, 0, "AVjy" }, { 6, 0, 0, JAM2, 0, "fjTy" }, { 6, 7, 0, JAM2, 0, "fjTy" },
};

static void tgrid(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1)
{
    LONG x, y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > 255) x1 = 255;
    if (y1 > 47) y1 = 47;
    Printf("   x %ld..%ld\n", x0, x1);
    for (y = y0; y <= y1; y++) {
        char line[260];
        int k = 0;
        for (x = x0; x <= x1; x++) { ULONG p = ReadPixel(rp, x, y); line[k++] = p == 1 ? '#' : p == 0 ? '.' : p == 2 ? ' ' : '?'; }
        line[k] = 0;
        Printf("%3ld %s\n", y, (LONG)line);
    }
    Flush(Output());
}

static int textprobe(void)
{
    struct BitMap *bm = AllocBitMap(256, 48, 2, BMF_CLEAR, NULL), *bm2 = AllocBitMap(256, 48, 2, BMF_CLEAR, NULL);
    struct RastPort rp, rp2;
    ULONG s;
    int i, last = -1;

    open_fonts();
    if (!bm || !bm2 || nfonts < 5) {
        PutStr("no Chip RAM or fonts\n");
        if (bm) FreeBitMap(bm);
        if (bm2) FreeBitMap(bm2);
        close_fonts();
        return 20;
    }
    Printf("TEXTPROBE, %s", (LONG)((struct Library *)GfxBase)->lib_IdString);
    if (DiskfontBase) Printf("with %s", (LONG)DiskfontBase->lib_IdString);
    for (i = 0; i < nfonts; i++) show_font(i);
    for (i = 0; i < 4; i++) dump_font(i, "as made");
    InitRastPort(&rp); rp.BitMap = bm;
    InitRastPort(&rp2); rp2.BitMap = bm2;
    for (s = 0; s < sizeof tprobes / sizeof tprobes[0]; s++) {
        const struct tprobe *t = &tprobes[s];
        struct TextFont *f;
        struct TextExtent te, te2;
        LONG n = (LONG)strlen(t->s), x = 40, y, len, len2, cp, cp2, same = 1, gx, gy;
        ULONG got = 0, ask;
        if (t->font >= nfonts) continue;
        f = fonts[t->font].tf;
        y = (LONG)f->tf_Baseline + 4;
        SetFont(&rp, f);
        if (t->font != last && t->font < 4) dump_font(t->font, "after SetFont");
        SetFont(&rp2, f);
        if (t->direct) { rp.AlgoStyle = rp2.AlgoStyle = t->style; }
        else { got = SetSoftStyle(&rp, t->style, 0xFF); SetSoftStyle(&rp2, t->style, 0xFF); }
        ask = AskSoftStyle(&rp);
        rp.TxSpacing = rp2.TxSpacing = t->spacing;
        SetWriteMask(&rp, 0xFF); SetWriteMask(&rp2, 0xFF);
        SetRast(&rp, 2); SetRast(&rp2, 2);
        SetAPen(&rp, 1); SetBPen(&rp, 0); SetDrMd(&rp, t->mode);
        SetAPen(&rp2, 1); SetBPen(&rp2, 0); SetDrMd(&rp2, t->mode);
        len = TextLength(&rp, (STRPTR)t->s, n);
        if (t->font != last && t->font < 4) dump_font(t->font, "after TextLength");
        TextExtent(&rp, (STRPTR)t->s, n, &te);
        len2 = ogfx_rp_TextLength(&rp2, t->s, n);
        ogfx_rp_TextExtent(&rp2, t->s, n, &te2);
        Move(&rp, x, y); Move(&rp2, x, y);
        Text(&rp, (STRPTR)t->s, n);
        if (!ogfx_rp_Text(&rp2, t->s, n)) PutStr("(core refused)\n");
        WaitBlit();
        cp = rp.cp_x; cp2 = rp2.cp_x;
        if (t->font != last) { PutStr("\n"); show_font(t->font); last = t->font; }
        Printf("== font %ld style $%lx%s mode %ld spacing %ld '", (LONG)t->font, (LONG)t->style,
               (LONG)(t->direct ? " (AlgoStyle set)" : ""), (LONG)t->mode, (LONG)t->spacing);
        for (i = 0; i < n; i++) Printf((UBYTE)t->s[i] >= 32 && (UBYTE)t->s[i] < 127 ? "%lc" : "\\x%02lx", (LONG)(UBYTE)t->s[i]);
        Printf("': SetSoftStyle %lx Ask %lx AlgoStyle %lx TextLength %ld TextExtent w %ld h %ld %ld,%ld..%ld,%ld cp %ld->%ld\n",
               got, ask, (LONG)rp.AlgoStyle, len, (LONG)te.te_Width, (LONG)te.te_Height, (LONG)te.te_Extent.MinX,
               (LONG)te.te_Extent.MinY, (LONG)te.te_Extent.MaxX, (LONG)te.te_Extent.MaxY, (LONG)x, cp);
        if (len2 != len || te2.te_Width != te.te_Width || te2.te_Height != te.te_Height || te2.te_Extent.MinX != te.te_Extent.MinX ||
            te2.te_Extent.MinY != te.te_Extent.MinY || te2.te_Extent.MaxX != te.te_Extent.MaxX || te2.te_Extent.MaxY != te.te_Extent.MaxY || cp2 != cp)
            Printf("   ours: TextLength %ld TextExtent w %ld h %ld %ld,%ld..%ld,%ld cp %ld\n", len2, (LONG)te2.te_Width, (LONG)te2.te_Height,
                   (LONG)te2.te_Extent.MinX, (LONG)te2.te_Extent.MinY, (LONG)te2.te_Extent.MaxX, (LONG)te2.te_Extent.MaxY, cp2);
        for (gy = 0; gy < 48; gy++)
            for (gx = 0; gx < 256; gx++)
                if (ReadPixel(&rp, gx, gy) != ReadPixel(&rp2, gx, gy)) same = 0;
        tgrid(&rp, x - 14, y - (LONG)f->tf_Baseline - 2, x + len + 14, y - (LONG)f->tf_Baseline + (LONG)f->tf_YSize + 1);
        if (!same) { PutStr("   ours:\n"); tgrid(&rp2, x - 14, y - (LONG)f->tf_Baseline - 2, x + len + 14, y - (LONG)f->tf_Baseline + (LONG)f->tf_YSize + 1); }
        else PutStr("   ours the same\n");
    }
    for (i = 0; i < 4; i++) dump_font(i, "at the end");
    FreeBitMap(bm); FreeBitMap(bm2);
    close_fonts();
    return 0;
}

int main(void)
{
    LONG args[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    struct RDArgs *rd = ReadArgs("ROUNDS/N,PROBE/S,LAYERS/S,LPROBE/S,AREAPROBE/S,AREAPROBE2/S,AREAPROBE3/S,AREAPROBE4/S,TEXTPROBE/S", args, NULL);
    ULONG rounds = rd && args[0] ? *(ULONG *)args[0] : 400, r, k, total = 0, bad = 0;

    if (rd && args[1]) { int rc = probe(); FreeArgs(rd); return rc; }
    if (rd && args[3]) { int rc = lprobe(); FreeArgs(rd); return rc; }
    if (rd && args[4]) { int rc = areaprobe(); FreeArgs(rd); return rc; }
    if (rd && args[5]) { int rc = areaprobe2(); FreeArgs(rd); return rc; }
    if (rd && args[6]) { int rc = areaprobe3(); FreeArgs(rd); return rc; }
    if (rd && args[7]) { int rc = areaprobe4(); FreeArgs(rd); return rc; }
    if (rd && args[8]) { int rc = textprobe(); FreeArgs(rd); return rc; }
    if (rd && args[2]) {
        layered = 1;
        if (!(LayersBase = OpenLibrary("layers.library", 39))) { PutStr("no layers.library\n"); FreeArgs(rd); return 20; }
    }

    arr_a = AllocVec(ARR, MEMF_ANY);
    arr_b = AllocVec(ARR, MEMF_ANY);
    areapat = AllocVec(16 * sizeof(UWORD), MEMF_CHIP);
    bltmask = AllocVec(BLTMASK, MEMF_CHIP);
    if (!arr_a || !arr_b || !areapat || !bltmask) { PutStr("no memory\n"); return 20; }
    open_fonts();
    if (!nfonts) { PutStr("no fonts\n"); return 20; }
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
        tras_a = bma ? AllocRaster(w, h) : NULL;
        tras_b = bmb ? AllocRaster(w, h) : NULL;
        if (bma && bmb && tmpbm && sbm && tras_a && tras_b && layered) { fill_random(); lay_ok = make_layers(); }
        if (!bma || !bmb || !tmpbm || !sbm || !tras_a || !tras_b || !lay_ok) {
            if (layered) free_layers();
            if (tras_a) FreeRaster(tras_a, w, h);
            if (tras_b) FreeRaster(tras_b, w, h);
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
            ULONG sel = rnd() % NCALLS;
            pens();
            switch (sel) {
            case 14: {
                /* AreaMove, AreaDraw, AreaEllipse and AreaEnd: one or two
                 * polygons and maybe an ellipse, sometimes with AREAOUTLINE
                 * and an area pattern. Unlayered, inside the bitmap (OS 3.2
                 * does not clip); in a layer, anywhere near it */
                LONG np = 1 + (LONG)(rnd() % 2), j, v, ell = rnd() % 3 == 0, rva = 0, rvb = 0, nv = 0, sz = (LONG)(rnd() % 3), outl = 0, starts = 0;
                LONG lo = layered ? -8 : 0;
                WORD pts[2 * 16];
                int st;
                InitTmpRas(&tmpras_a, tras_a, RASSIZE(w, h)); InitTmpRas(&tmpras_b, tras_b, RASSIZE(w, h));
                InitArea(&ainfo_a, avec_a, 24); InitArea(&ainfo_b, avec_b, 24);
                rpa.TmpRas = &tmpras_a; rpb.TmpRas = &tmpras_b;
                rpa.AreaInfo = &ainfo_a; rpb.AreaInfo = &ainfo_b;
                for (j = 0; j < 16; j++) areapat[j] = rnd() % 4 ? (UWORD)rnd() : 0xFFFF;
                if (rnd() % 3 == 0) { SetAfPt(&rpa, areapat, sz); SetAfPt(&rpb, areapat, sz); }
                else { SetAfPt(&rpa, NULL, 0); SetAfPt(&rpb, NULL, 0); }
                if (rnd() % 3 == 0) {
                    UBYTE ol = (UBYTE)rnd();
                    rpa.AOlPen = rpb.AOlPen = ol;
                    rpa.Flags |= AREAOUTLINE; rpb.Flags |= AREAOUTLINE;
                    outl = 1;
                }
                for (j = 0; j < np; j++) {
                    LONG n = 1 + (LONG)(rnd() % 6), cx = (LONG)(rnd() % W), cy = (LONG)(rnd() % H), rx = 1 + (LONG)(rnd() % 40), ry = 1 + (LONG)(rnd() % 30);
                    for (v = 0; v < n && nv < 16; v++, nv++) {
                        LONG px = cx + (LONG)(rnd() % (ULONG)(2 * rx + 1)) - rx, py = cy + (LONG)(rnd() % (ULONG)(2 * ry + 1)) - ry;
                        if (px < lo) px = lo;
                        if (py < lo) py = lo;
                        if (px >= (LONG)W) px = W - 1;
                        if (py >= (LONG)H) py = H - 1;
                        pts[2 * nv] = (WORD)px; pts[2 * nv + 1] = (WORD)py;
                        if (!v) starts |= 1L << nv;
                        rva |= v ? AreaDraw(&rpa, px, py) : AreaMove(&rpa, px, py);
                        rvb |= v ? ogfx_rp_AreaDraw(&rpb, px, py) : ogfx_rp_AreaMove(&rpb, px, py);
                    }
                }
                if (ell) {
                    LONG ea = (LONG)(rnd() % 30), eb = (LONG)(rnd() % 20), ex = (LONG)(rnd() % W), ey = (LONG)(rnd() % H);
                    if (!layered) {
                        if (ea > ex) ea = ex;
                        if (ea > (LONG)W - 1 - ex) ea = (LONG)W - 1 - ex;
                        if (eb > ey) eb = ey;
                        if (eb > (LONG)H - 1 - ey) eb = (LONG)H - 1 - ey;
                    }
                    rva |= AreaEllipse(&rpa, ex, ey, ea, eb);
                    rvb |= ogfx_rp_AreaEllipse(&rpb, ex, ey, ea, eb);
                    ASKED("%ld points, %ld polygons, ellipse %ld,%ld %ld x %ld", nv, np, ex, ey, ea, eb, 0);
                } else ASKED("%ld points, %ld polygons", nv, np, 0, 0, 0, 0, 0);
                if (!ogfx_rp_takes_area(&rpb)) {
                    AreaEnd(&rpa); AreaEnd(&rpb);
                    rpa.TmpRas = rpb.TmpRas = NULL;
                    rpa.AreaInfo = rpb.AreaInfo = NULL;
                    rpa.Flags &= ~AREAOUTLINE; rpb.Flags &= ~AREAOUTLINE;
                    refused++;
                    break;
                }
                ra = AreaEnd(&rpa);
                rb = ogfx_rp_AreaEnd(&rpb);
                /* a table with no draw: OS 3.2 answers whatever d0 held */
                if (ra != 0 && ra != -1) { ra = rb; junk++; }
                ra |= (rva != 0) << 8;
                rb |= (rvb != 0) << 8;
                rpa.TmpRas = rpb.TmpRas = NULL;
                rpa.AreaInfo = rpb.AreaInfo = NULL;
                rpa.Flags &= ~AREAOUTLINE; rpb.Flags &= ~AREAOUTLINE;
                plane = same(&at);
                st = line_state();
                if (report(14, ra, ra == rb && st ? rb + 1 : rb, plane, at)) {
                    Printf("  fg %ld bg %ld aol %ld mode %ld writemask %02lx outline %ld pattern", (LONG)(UBYTE)rpa.FgPen,
                           (LONG)(UBYTE)rpa.BgPen, (LONG)(UBYTE)rpa.AOlPen, (LONG)rpa.DrawMode, (LONG)rpa.Mask, (LONG)outl);
                    if (rpa.AreaPtrn) for (j = 0; j < 1L << rpa.AreaPtSz; j++) Printf(" $%04lx", (LONG)rpa.AreaPtrn[j]);
                    else PutStr(" none");
                    PutStr("\n");
                    show_state();
                    PutStr("  points (| a new polygon):");
                    for (j = 0; j < nv; j++) Printf("%s %ld,%ld", (LONG)(j && starts & 1L << j ? " |" : ""), (LONG)pts[2 * j], (LONG)pts[2 * j + 1]);
                    PutStr("\n");
                    if (!layered) show_pixels(0, 0, (LONG)W - 1, (LONG)H - 1);
                }
                break;
            }
            case 17: case 18: {
                /* Text, and TextLength with TextExtent, in one of the fonts
                 * with a random style, TxSpacing and string (sometimes
                 * characters outside the font). Unlayered, the text's box
                 * inside the bitmap (OS 3.2 does not clip); in a layer,
                 * anywhere near it. */
                struct tfont *tfo = &fonts[rnd() % nfonts];
                struct TextFont *f = tfo->tf;
                struct TextExtent tea, teb;
                UBYTE str[24];
                LONG n = (LONG)(rnd() % 21), j, st = (LONG)(rnd() % 8), spc = rnd() % 4 ? 0 : (LONG)(rnd() % 6) - 2, la, lb, top;
                int mine;
                for (j = 0; j < n; j++) str[j] = (UBYTE)(rnd() % 4 ? 32 + rnd() % 95 : rnd());
                SetFont(&rpa, f); SetFont(&rpb, f);
                SetSoftStyle(&rpa, (ULONG)st, 0xFF); SetSoftStyle(&rpb, (ULONG)st, 0xFF);
                rpa.TxSpacing = rpb.TxSpacing = (WORD)spc;
                if (sel == 18) {
                    la = TextLength(&rpa, (STRPTR)str, (ULONG)n);
                    lb = ogfx_rp_TextLength(&rpb, (CONST_STRPTR)str, (ULONG)n);
                    TextExtent(&rpa, (STRPTR)str, (ULONG)n, &tea);
                    ogfx_rp_TextExtent(&rpb, (CONST_STRPTR)str, (ULONG)n, &teb);
                    ASKED("font %ld style %ld spacing %ld n %ld: width %ld extent %ld..%ld", (LONG)(tfo - fonts), st, spc, n,
                          (LONG)tea.te_Width, (LONG)tea.te_Extent.MinX, (LONG)tea.te_Extent.MaxX);
                    if (la == lb && memcmp(&tea, &teb, sizeof tea)) lb = la + 0x10000;
                    if (report(18, la, lb, 0, 0))
                        Printf("  ours: width %ld height %ld extent %ld,%ld..%ld,%ld\n", (LONG)teb.te_Width, (LONG)teb.te_Height,
                               (LONG)teb.te_Extent.MinX, (LONG)teb.te_Extent.MinY, (LONG)teb.te_Extent.MaxX, (LONG)teb.te_Extent.MaxY);
                    break;
                }
                if (rnd() % 4 == 0) {
                    LONG sz = (LONG)(rnd() % 3);
                    for (j = 0; j < 16; j++) areapat[j] = (UWORD)rnd();
                    SetAfPt(&rpa, areapat, sz); SetAfPt(&rpb, areapat, sz);
                } else { SetAfPt(&rpa, NULL, 0); SetAfPt(&rpb, NULL, 0); }
                TextExtent(&rpa, (STRPTR)str, (ULONG)n, &tea);
                top = y - (LONG)f->tf_Baseline;
                if (!layered) {
                    /* 8 pixels to spare on each side */
                    if (x + tea.te_Extent.MinX - 8 < 0 || x + tea.te_Extent.MaxX + 8 >= (LONG)W ||
                        top < 0 || top + (LONG)f->tf_YSize + 1 >= (LONG)H) break;
                } else x -= 16;
                rpa.cp_x = rpb.cp_x = (WORD)x; rpa.cp_y = rpb.cp_y = (WORD)y;
                Text(&rpa, (STRPTR)str, (ULONG)n);
                if (!(mine = ogfx_rp_Text(&rpb, (CONST_STRPTR)str, (ULONG)n))) Text(&rpb, (STRPTR)str, (ULONG)n);
                SetAfPt(&rpa, NULL, 0); SetAfPt(&rpb, NULL, 0);
                ASKED("%ld,%ld font %ld style %ld spacing %ld n %ld mode %ld", x, y, (LONG)(tfo - fonts), st, spc, n, (LONG)rpa.DrawMode);
                if (!mine) { refused++; break; }
                plane = same(&at);
                if (report(17, 0, line_state(), plane, at)) {
                    show_state();
                    Printf("  fg %ld bg %ld writemask %02lx pattern rows %ld; string", (LONG)(UBYTE)rpa.FgPen, (LONG)(UBYTE)rpa.BgPen,
                           (LONG)rpa.Mask, (LONG)(rpa.AreaPtrn ? 1L << rpa.AreaPtSz : 0));
                    for (j = 0; j < n; j++) Printf(" %02lx", (LONG)str[j]);
                    PutStr("\n");
                    if (!layered) show_pixels(x + tea.te_Extent.MinX - 8, top, x + tea.te_Extent.MaxX + 8, top + (LONG)f->tf_YSize);
                }
                break;
            }
            case 15: case 16: {
                /* DrawEllipse (unlayered, inside the bitmap) and EraseRect;
                 * where the core refuses, B gets graphics.library's call and
                 * it is not counted */
                LONG ea = (LONG)(rnd() % 40), eb = (LONG)(rnd() % 30);
                int mine;
                if (sel == 15) {
                    if (rnd() % 2) { SetDrMd(&rpa, rpa.DrawMode & JAM2); SetDrMd(&rpb, rpb.DrawMode & JAM2); }
                    if (!layered) {
                        if (ea > x) ea = x;
                        if (ea > (LONG)W - 1 - x) ea = (LONG)W - 1 - x;
                        if (eb > y) eb = y;
                        if (eb > (LONG)H - 1 - y) eb = (LONG)H - 1 - y;
                    }
                    DrawEllipse(&rpa, x, y, ea, eb);
                    if (!(mine = ogfx_rp_DrawEllipse(&rpb, x, y, ea, eb))) DrawEllipse(&rpb, x, y, ea, eb);
                    ASKED("%ld,%ld %ld x %ld fg %ld mode %ld writemask %02lx", x, y, ea, eb, (LONG)(UBYTE)rpa.FgPen, (LONG)rpa.DrawMode, (LONG)rpa.Mask);
                } else {
                    EraseRect(&rpa, (LONG)x0, (LONG)y0, (LONG)x1, (LONG)y1);
                    if (!(mine = ogfx_rp_EraseRect(&rpb, (LONG)x0, (LONG)y0, (LONG)x1, (LONG)y1))) EraseRect(&rpb, (LONG)x0, (LONG)y0, (LONG)x1, (LONG)y1);
                    ASKED("%ld,%ld-%ld,%ld bg %ld mode %ld writemask %02lx", (LONG)x0, (LONG)y0, (LONG)x1, (LONG)y1, (LONG)(UBYTE)rpa.BgPen, (LONG)rpa.DrawMode, (LONG)rpa.Mask);
                }
                if (!mine) { refused++; break; }
                plane = same(&at);
                if (report((int)sel, 0, line_state(), plane, at)) {
                    show_state();
                    if (!layered) show_pixels(0, 0, (LONG)W - 1, (LONG)H - 1);
                }
                break;
            }
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
        FreeRaster(tras_a, w, h); FreeRaster(tras_b, w, h);
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
    if (refused) Printf("%lu calls left to graphics.library (ClipBlit and ScrollRaster: a simple refresh layer, a clip region, a rectangle not inside the layer or a ScrollRaster shift of its size; DrawEllipse in COMPLEMENT, INVERSVID or a line pattern; EraseRect in a layer; AreaEnd outlines in a layer reaching outside it)\n", refused);
    if (recuts) Printf("%lu rounds ended early: graphics.library cut the two sides' ClipRects into different rectangles\n", recuts);
    if (chip_rounds_lost) Printf("%lu rounds kept %ld bytes of Chip RAM in all\n", chip_rounds_lost, chip_round_lost);
    for (k = 0; k < NCALLS; k++)
        if (chip_kept[k]) Printf("call kind %ld kept %ld bytes of Chip RAM until its round ended\n", (LONG)k, chip_kept[k]);
    if (junk) Printf("%lu AreaEnd calls on a table with no draw: OS 3.2 answered junk (not compared)\n", junk);
    if (pad_runs) Printf("%lu ReadPixelArray8 calls wrote the row padding differently (not counted)\n", pad_runs);
    close_fonts();
    FreeVec(arr_a); FreeVec(arr_b); FreeVec(areapat); FreeVec(bltmask);
    if (LayersBase) CloseLibrary(LayersBase);
    if (rd) FreeArgs(rd);
    return bad ? 5 : nochip ? 10 : 0;
}
