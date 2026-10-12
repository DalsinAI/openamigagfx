/* SPDX-License-Identifier: MIT */
/* OpenGfx's RastPort calls (gfx_rp.h). Copyright (c) 2026 Dalsin Limited. MIT. */
#include <graphics/gfx.h>
#include <graphics/clip.h>
#include <graphics/layers.h>
#include "gfx_rp.h"
#include "gfx_core.h"

static void planar_from(struct BitMap *b, struct gfx_planar *bm)
{
    int p;

    for (p = 0; p < 8; p++)
        bm->planes[p] = p < b->Depth ? (uint8_t *)b->Planes[p] : 0;
    bm->bytes_per_row = b->BytesPerRow;
    bm->rows = b->Rows;
    bm->depth = b->Depth > 8 ? 8 : b->Depth;
}

static void planar_of(struct RastPort *rp, struct gfx_planar *bm)
{
    planar_from(rp->BitMap, bm);
}

/* ---- where a drawing lands --------------------------------------------------
 * A place is a bitmap, the offset from the RastPort's coordinates to the
 * bitmap's, and the rectangle of the bitmap the drawing may touch. An
 * unlayered RastPort has one: its bitmap, whole. A layered one has one a
 * ClipRect of its layer (the caller holds the layer locked): the screen's
 * bitmap inside the ClipRect where the layer is on top, and where it is
 * covered, a smart refresh layer's own bitmap for that ClipRect, which
 * keeps the screen's alignment within 16 pixels (its x 0 is the screen's
 * x at ClipRect's MinX & ~15). A simple refresh layer's covered parts are
 * not drawn. A super bitmap layer is graphics.library's (ogfx_rp_takes). */
struct place {
    struct gfx_planar bm;
    LONG dx, dy;                /* the bitmap's x = the RastPort's x + dx */
    LONG x0, y0, x1, y1;        /* the bitmap's rectangle, inclusive */
    int covered;                /* a smart refresh layer's own bitmap */
};
struct walk {
    struct RastPort *rp;
    struct ClipRect *cr;
    int done;
};

static void walk_start(struct walk *w, struct RastPort *rp)
{
    w->rp = rp;
    w->cr = rp->Layer ? rp->Layer->ClipRect : NULL;
    w->done = 0;
}

static int walk_next(struct walk *w, struct place *pl)
{
    struct Layer *l = w->rp->Layer;
    struct BitMap *b;

    if (!l) {
        if (w->done) return 0;
        w->done = 1;
        b = w->rp->BitMap;
        planar_from(b, &pl->bm);
        pl->dx = pl->dy = 0;
        pl->covered = 0;
        pl->x0 = pl->y0 = 0;
        pl->x1 = (LONG)b->BytesPerRow * 8 - 1;
        pl->y1 = (LONG)b->Rows - 1;
        return 1;
    }
    while (w->cr) {
        struct ClipRect *cr = w->cr;
        LONG ox = l->bounds.MinX - l->Scroll_X, oy = l->bounds.MinY - l->Scroll_Y, bx, by;
        w->cr = cr->Next;
        if (!cr->obscured) { b = w->rp->BitMap; bx = by = 0; }
        else if (cr->BitMap && !(l->Flags & LAYERSUPER)) { b = cr->BitMap; bx = cr->bounds.MinX & ~15L; by = cr->bounds.MinY; }
        else continue;
        planar_from(b, &pl->bm);
        pl->covered = b != w->rp->BitMap;
        pl->dx = ox - bx;
        pl->dy = oy - by;
        pl->x0 = cr->bounds.MinX - bx;
        pl->y0 = cr->bounds.MinY - by;
        pl->x1 = cr->bounds.MaxX - bx;
        pl->y1 = cr->bounds.MaxY - by;
        if (pl->x0 < 0) pl->x0 = 0;
        if (pl->y0 < 0) pl->y0 = 0;
        if (pl->x1 > (LONG)b->BytesPerRow * 8 - 1) pl->x1 = (LONG)b->BytesPerRow * 8 - 1;
        if (pl->y1 > (LONG)b->Rows - 1) pl->y1 = (LONG)b->Rows - 1;
        if (pl->x0 <= pl->x1 && pl->y0 <= pl->y1) return 1;
    }
    return 0;
}

static int in_place(const struct place *pl, LONG x, LONG y)
{
    x += pl->dx;
    y += pl->dy;
    return x >= pl->x0 && x <= pl->x1 && y >= pl->y0 && y <= pl->y1;
}

/* The place pixel (x, y) lands in, trying the last one first; NULL when
 * it lands nowhere. */
static struct place *place_at(struct RastPort *rp, struct place *pl, int *have, LONG x, LONG y)
{
    struct walk w;

    if (*have && in_place(pl, x, y)) return pl;
    walk_start(&w, rp);
    while (walk_next(&w, pl))
        if (in_place(pl, x, y)) return *have = 1, pl;
    *have = 0;
    return NULL;
}

/* The rectangle (RastPort coordinates) cut to a place, in the bitmap's
 * coordinates; 0 when nothing is left. */
static int cut_to(const struct place *pl, LONG x0, LONG y0, LONG x1, LONG y1, LONG *r)
{
    r[0] = x0 + pl->dx; r[1] = y0 + pl->dy; r[2] = x1 + pl->dx; r[3] = y1 + pl->dy;
    if (r[0] < pl->x0) r[0] = pl->x0;
    if (r[1] < pl->y0) r[1] = pl->y0;
    if (r[2] > pl->x1) r[2] = pl->x1;
    if (r[3] > pl->y1) r[3] = pl->y1;
    return r[0] <= r[2] && r[1] <= r[3];
}

int ogfx_rp_takes(struct RastPort *rp, int masked)
{
    struct Layer *l = rp->Layer;
    return !l || (!masked && !(l->Flags & LAYERSUPER));
}

/* The rectangle, cut to the bitmap; 0 when nothing is left. */
static int cut(struct RastPort *rp, ULONG *x0, ULONG *y0, ULONG *x1, ULONG *y1)
{
    ULONG w = (ULONG)rp->BitMap->BytesPerRow * 8, h = rp->BitMap->Rows;
    if (*x0 > *x1 || *y0 > *y1 || *x0 >= w || *y0 >= h) return 0;
    if (*x1 >= w) *x1 = w - 1;
    if (*y1 >= h) *y1 = h - 1;
    return 1;
}

/* Measured on OS 3.2: an unlayered RastPort is not clipped. WritePixel
 * answers 0 wherever the pixel is (OS 3.2 then writes outside the bitmap;
 * we draw nothing there), and ReadPixel reads whatever memory the pixel's
 * address falls on, as OS 3.2 does. Under INVERSVID the pixel is the
 * background: JAM2 draws BgPen (or inverts, with COMPLEMENT), and without
 * JAM2 the pixel is left as it is. */
LONG ogfx_rp_WritePixel(struct RastPort *rp, LONG x, LONG y)
{
    struct place pl;
    int have = 0;
    UBYTE mode = rp->DrawMode;

    /* in a layer: -1 where nothing keeps the pixel (outside the layer, a
     * simple refresh layer's covered part, outside its clip region:
     * measured on OS 3.2); unlayered, 0 wherever it is */
    if (!place_at(rp, &pl, &have, x, y)) return rp->Layer ? -1 : 0;
    if ((mode & (INVERSVID | JAM2)) == INVERSVID) return 0;
    x += pl.dx;
    y += pl.dy;
    if (mode & COMPLEMENT)
        gfx_planar_put(&pl.bm, x, y, 0, rp->Mask, GFX_INVERT);
    else
        gfx_planar_put(&pl.bm, x, y, mode & INVERSVID ? rp->BgPen : rp->FgPen, rp->Mask, GFX_SET);
    return 0;
}

LONG ogfx_rp_ReadPixel(struct RastPort *rp, LONG x, LONG y)
{
    struct BitMap *b = rp->BitMap;

    if (rp->Layer) {
        /* in a layer: the pixel where it is kept, or -1 where nothing keeps it */
        struct place pl;
        int have = 0;
        if (!place_at(rp, &pl, &have, x, y)) return -1;
        return (LONG)gfx_planar_get(&pl.bm, x + pl.dx, y + pl.dy);
    }
    LONG off = y * (LONG)b->BytesPerRow + (x >> 3);
    UBYTE bit = (UBYTE)(0x80 >> (x & 7));
    ULONG pen = 0;
    int p;

    for (p = 0; p < b->Depth && p < 8; p++) {
        UBYTE *pl = (UBYTE *)b->Planes[p];
        if (pl == (UBYTE *)-1) pen |= 1UL << p;
        else if (pl && (pl[off] & bit)) pen |= 1UL << p;
    }
    return (LONG)pen;
}

void ogfx_rp_SetRast(struct RastPort *rp, ULONG pen)
{
    struct gfx_planar bm;
    struct walk w;
    struct place pl;
    LONG y;

    if (!rp->Layer) {
        planar_of(rp, &bm);
        gfx_planar_fill_all(&bm, pen, rp->Mask);
        return;
    }
    /* in a layer: every place the layer keeps; a covered part's bitmap
     * whole, its padding to 16 pixels too (measured on OS 3.2) */
    walk_start(&w, rp);
    while (walk_next(&w, &pl))
        if (pl.covered) gfx_planar_fill_all(&pl.bm, pen, rp->Mask);
        else for (y = pl.y0; y <= pl.y1; y++)
            gfx_planar_span(&pl.bm, pl.x0, pl.x1, y, 0xFFFF, pen, 0, rp->Mask, GFX_JAM1);
}

/* WritePixelArray8's and ReadPixelArray8's array rows are the width
 * rounded up to 16 bytes. */
#define ROW16(x0, x1) ((((x1) - (x0) + 1) + 15) & ~15UL)

/* An array (RastPort coordinates x0, y0 at array[0]) into every place of
 * a layered RastPort, or out of it (write 0). */
static void layer_array(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, UBYTE *array, ULONG bpr, int write)
{
    struct walk w;
    struct place pl;
    LONG r[4];

    walk_start(&w, rp);
    while (walk_next(&w, &pl)) {
        UBYTE *a;
        if (!cut_to(&pl, x0, y0, x1, y1, r)) continue;
        a = array + (ULONG)(r[1] - pl.dy - y0) * bpr + (ULONG)(r[0] - pl.dx - x0);
        if (write) gfx_planar_write8(&pl.bm, r[0], r[1], r[2], r[3], a, bpr, rp->Mask);
        else gfx_planar_read8(&pl.bm, r[0], r[1], r[2], r[3], a, bpr);
    }
}

LONG ogfx_rp_WritePixelArray8(struct RastPort *rp, ULONG xstart, ULONG ystart, ULONG xstop, ULONG ystop,
                              UBYTE *array, struct RastPort *temprp)
{
    ULONG bpr = ROW16(xstart, xstop), x1 = xstop, y1 = ystop;
    struct gfx_planar bm;

    (void)temprp;
    if (rp->Layer) {
        if (xstart > xstop || ystart > ystop) return 0;
        layer_array(rp, (LONG)xstart, (LONG)ystart, (LONG)xstop, (LONG)ystop, array, bpr, 1);
        return (LONG)((xstop - xstart + 1) * (ystop - ystart + 1));
    }
    if (!cut(rp, &xstart, &ystart, &x1, &y1)) return 0;
    planar_of(rp, &bm);
    gfx_planar_write8(&bm, xstart, ystart, x1, y1, array, bpr, rp->Mask);
    return (LONG)((x1 - xstart + 1) * (y1 - ystart + 1));
}

LONG ogfx_rp_ReadPixelArray8(struct RastPort *rp, ULONG xstart, ULONG ystart, ULONG xstop, ULONG ystop,
                             UBYTE *array, struct RastPort *temprp)
{
    ULONG bpr = ROW16(xstart, xstop), x1 = xstop, y1 = ystop;
    struct gfx_planar bm;

    (void)temprp;
    if (rp->Layer) {
        if (xstart > xstop || ystart > ystop) return 0;
        x1 = xstop; y1 = ystop;
        /* a pixel nothing keeps reads as 0 (measured on OS 3.2) */
        {
            ULONG yy, xx;
            for (yy = 0; yy <= y1 - ystart; yy++)
                for (xx = 0; xx <= x1 - xstart; xx++) array[yy * bpr + xx] = 0;
        }
        layer_array(rp, (LONG)xstart, (LONG)ystart, (LONG)xstop, (LONG)ystop, array, bpr, 0);
    } else {
        if (!cut(rp, &xstart, &ystart, &x1, &y1)) return 0;
        planar_of(rp, &bm);
        gfx_planar_read8(&bm, xstart, ystart, x1, y1, array, bpr);
    }
    /* each row's padding to 16 is 0, as OS 3.2 leaves it with a clean
     * temporary RastPort (it copies whatever the temporary row holds) */
    {
        ULONG y, x, w = x1 - xstart + 1;
        for (y = 0; y <= y1 - ystart; y++)
            for (x = w; x < bpr; x++) array[y * bpr + x] = 0;
    }
    return (LONG)((x1 - xstart + 1) * (y1 - ystart + 1));
}

void ogfx_rp_WriteChunkyPixels(struct RastPort *rp, ULONG xstart, ULONG ystart, ULONG xstop, ULONG ystop,
                               UBYTE *array, LONG bytesperrow)
{
    ULONG x1 = xstop, y1 = ystop;
    struct gfx_planar bm;

    if (rp->Layer) {
        if (xstart <= xstop && ystart <= ystop)
            layer_array(rp, (LONG)xstart, (LONG)ystart, (LONG)xstop, (LONG)ystop, array, (ULONG)bytesperrow, 1);
        return;
    }
    if (!cut(rp, &xstart, &ystart, &x1, &y1)) return;
    planar_of(rp, &bm);
    gfx_planar_write8(&bm, xstart, ystart, x1, y1, array, (uint32_t)bytesperrow, rp->Mask);
}

/* What a pattern bit of 1 and of 0 do in the RastPort's DrawMode: INVERSVID
 * swaps them, JAM2 draws BgPen for a 0, COMPLEMENT inverts for a 1.
 * Measured on OS 3.2: COMPLEMENT with JAM2 inverts every pixel, 1 or 0,
 * for lines and fills alike. */
static int pat_mode(struct RastPort *rp, uint32_t *pat)
{
    UBYTE mode = rp->DrawMode;

    if (mode & INVERSVID) *pat = ~*pat & 0xFFFF;
    if (mode & COMPLEMENT) {
        if (mode & JAM2) *pat = 0xFFFF;
        return GFX_XOR;
    }
    return mode & JAM2 ? GFX_JAM2 : GFX_JAM1;
}

void ogfx_rp_Move(struct RastPort *rp, LONG x, LONG y)
{
    rp->cp_x = (WORD)x;
    rp->cp_y = (WORD)y;
    rp->Flags |= FRST_DOT;
    rp->linpatcnt = 15;
}

/* A line from the pen position to (x, y), Bresenham along the longer axis,
 * every pixel on it drawn through LinePtrn: pixel i takes bit
 * (linpatcnt - i) & 15, and linpatcnt goes down by the line's length (a
 * BYTE, not kept to 0-15: measured on OS 3.2). The first pixel is left out
 * when FRST_DOT is clear (it is the last one of the line before), so a
 * COMPLEMENT line drawn on from another does not invert their corner
 * twice. */
#define JOINT 2  /* first: a PolyDraw corner in COMPLEMENT without JAM2 */
static void line(struct RastPort *rp, LONG x, LONG y, int first)
{
    struct place pl;
    int have = 0;
    WORD x0 = rp->cp_x, y0 = rp->cp_y, x1 = (WORD)x, y1 = (WORD)y;
    LONG dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    LONG sx = x1 >= x0 ? 1 : -1, sy = y1 >= y0 ? 1 : -1;
    LONG len = dx > dy ? dx : dy, mn = dx > dy ? dy : dx, err = 2 * mn - len, i;
    LONG px = x0, py = y0;
    uint32_t ptrn = rp->LinePtrn;
    int cnt = rp->linpatcnt, mode = pat_mode(rp, &ptrn);

    for (i = 0; i <= len; i++) {
        uint32_t on = ptrn >> ((cnt - i) & 15) & 1;
        if (!i && first == JOINT) on = !(rp->LinePtrn >> (cnt & 15) & 1);
        if ((i || first) && place_at(rp, &pl, &have, px, py)) {
            LONG bx = px + pl.dx, by = py + pl.dy;
            if (mode == GFX_XOR) { if (on) gfx_planar_put(&pl.bm, bx, by, 0, rp->Mask, GFX_INVERT); }
            else if (on) gfx_planar_put(&pl.bm, bx, by, rp->FgPen, rp->Mask, GFX_SET);
            else if (mode == GFX_JAM2) gfx_planar_put(&pl.bm, bx, by, rp->BgPen, rp->Mask, GFX_SET);
        }
        if (err >= 0) {
            if (dx > dy) py += sy; else px += sx;
            err -= 2 * len;
        }
        err += 2 * mn;
        if (dx > dy) px += sx; else py += sy;
    }
    rp->linpatcnt = (BYTE)(cnt - len);
    rp->Flags &= ~FRST_DOT;
    rp->cp_x = x1;
    rp->cp_y = y1;
}

void ogfx_rp_Draw(struct RastPort *rp, LONG x, LONG y)
{
    line(rp, x, y, (rp->Flags & FRST_DOT) != 0);
}

/* Measured on OS 3.2: PolyDraw's first line keeps to FRST_DOT as Draw
 * does, and every later line draws its first pixel too, so with
 * COMPLEMENT and JAM2 (every pixel inverted) a corner is inverted twice.
 * In COMPLEMENT without JAM2 a corner ends up inverted exactly once
 * whatever LinePtrn holds there (never, under INVERSVID): the later
 * line's first pixel inverts it again only where the pattern bit is 0. */
void ogfx_rp_PolyDraw(struct RastPort *rp, LONG count, const WORD *xy)
{
    int again = (rp->DrawMode & (COMPLEMENT | JAM2)) != COMPLEMENT ? 1 : JOINT;
    LONG i;

    for (i = 0; i < count; i++, xy += 2) line(rp, xy[0], xy[1], i ? again : (rp->Flags & FRST_DOT) != 0);
}

/* The area pattern's row for line y: AreaPtrn holds 1 << AreaPtSz rows,
 * repeating down the bitmap; none is solid. Multicolour patterns (a
 * negative AreaPtSz) come later and draw solid for now. */
static uint32_t area_row(struct RastPort *rp, LONG y)
{
    if (!rp->AreaPtrn || rp->AreaPtSz < 0) return 0xFFFF;
    return rp->AreaPtrn[y & ((1L << rp->AreaPtSz) - 1)];
}

void ogfx_rp_RectFill(struct RastPort *rp, LONG xmin, LONG ymin, LONG xmax, LONG ymax)
{
    ogfx_rp_BltPattern(rp, NULL, xmin, ymin, xmax, ymax, 0);
}

/* The rectangle through the area pattern and, when there is one, a
 * one-plane mask of bytecnt bytes a row whose bit for (x - xl, y - yl)
 * says whether pixel (x, y) is drawn at all.
 *
 * With a mask, OS 3.2 draws as its blitter does, and a caller whose mask
 * is not exactly as many words wide as the rectangle's words on screen
 * sees it (measured by GfxCoreABITest PROBE on graphics 47.10). Each row
 * covers the screen words from xl's to maxx's (dw of them), reading dw
 * mask words from the row's start, on past bytecnt if need be. The last
 * of them keeps only the rectangle's width when dw is bytecnt / 2 words,
 * and nothing otherwise. The words are shifted right by xl & 15, the
 * bits shifted out carrying into the next word, and from the end of one
 * row into the start of the next: so with a set bit past the width, the
 * row below can be drawn left of xl. */
static void blt_masked(struct RastPort *rp, struct gfx_planar *bm, const UBYTE *mask, LONG xl, LONG yl,
                       LONG maxx, LONG maxy, ULONG bytecnt)
{
    LONG s = xl & 15, w = maxx - xl + 1, dw = (maxx >> 4) - (xl >> 4) + 1, y, j;
    LONG width = (LONG)rp->BitMap->BytesPerRow * 8, x0 = xl & ~15L;
    UWORD last = dw == (LONG)(bytecnt >> 1) ? (UWORD)(0xFFFF << ((-w) & 15)) : 0, prev = 0;

    for (y = yl; y <= maxy; y++) {
        const UBYTE *m = mask + (ULONG)(y - yl) * bytecnt;
        uint32_t pat = area_row(rp, y);
        int mode = pat_mode(rp, &pat);
        for (j = 0; j < dw; j++) {
            UWORD a = (UWORD)(m[2 * j] << 8 | m[2 * j + 1]), d;
            LONG x, run, xa, xb;
            if (j == dw - 1) a &= last;
            d = (UWORD)(s ? (prev << (16 - s)) | (a >> s) : a);
            prev = a;
            for (x = 0; x < 16; x = run + 1) {
                run = x;
                if (!(d & (0x8000 >> x))) continue;
                while (run < 15 && (d & (0x8000 >> (run + 1)))) run++;
                xa = x0 + 16 * j + x;
                xb = x0 + 16 * j + run;
                if (xb >= width) xb = width - 1;
                if (xa <= xb) gfx_planar_span(bm, xa, xb, y, pat, rp->FgPen, rp->BgPen, rp->Mask, mode);
            }
        }
    }
}

void ogfx_rp_BltPattern(struct RastPort *rp, const UBYTE *mask, LONG xl, LONG yl, LONG maxx, LONG maxy,
                        ULONG bytecnt)
{
    struct gfx_planar bm;
    ULONG x0 = (ULONG)(WORD)xl, y0 = (ULONG)(WORD)yl, x1 = (ULONG)(WORD)maxx, y1 = (ULONG)(WORD)maxy, y;

    if (rp->Layer) {
        /* in a layer (no mask: ogfx_rp_takes): the rectangle in each place;
         * the area pattern lines up with the RastPort's own x and y (measured
         * on OS 3.2), so each place's pattern is turned by its offset */
        struct walk w;
        struct place pl;
        LONG r[4], yy;
        if (mask || (WORD)xl > (WORD)maxx || (WORD)yl > (WORD)maxy) return;
        walk_start(&w, rp);
        while (walk_next(&w, &pl)) {
            if (!cut_to(&pl, (WORD)xl, (WORD)yl, (WORD)maxx, (WORD)maxy, r)) continue;
            for (yy = r[1]; yy <= r[3]; yy++) {
                uint32_t pat = area_row(rp, yy - pl.dy), r16 = (uint32_t)pl.dx & 15;
                if (r16) pat = (pat >> r16 | pat << (16 - r16)) & 0xFFFF;
                int mode = pat_mode(rp, &pat);
                gfx_planar_span(&pl.bm, r[0], r[2], yy, pat, rp->FgPen, rp->BgPen, rp->Mask, mode);
            }
        }
        return;
    }
    if ((WORD)xl < 0 || (WORD)yl < 0 || !cut(rp, &x0, &y0, &x1, &y1)) return;
    planar_of(rp, &bm);
    if (mask) {
        blt_masked(rp, &bm, mask, (WORD)xl, (WORD)yl, (WORD)maxx, (LONG)y1, bytecnt);
        return;
    }
    for (y = y0; y <= y1; y++) {
        uint32_t pat = area_row(rp, (LONG)y);
        int mode = pat_mode(rp, &pat);
        gfx_planar_span(&bm, x0, x1, y, pat, rp->FgPen, rp->BgPen, rp->Mask, mode);
    }
}
