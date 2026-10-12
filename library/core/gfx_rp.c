/* SPDX-License-Identifier: MIT */
/* OpenGfx's RastPort calls (gfx_rp.h). Copyright (c) 2026 Dalsin Limited. MIT. */
#include <graphics/gfx.h>
#include <graphics/clip.h>
#include <graphics/layers.h>
#include <graphics/text.h>
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

/* ---- ellipses ---------------------------------------------------------------------
 * Measured on OS 3.2 (GfxCoreABITest AREAPROBE2 and AREAPROBE3, 56
 * sizes): DrawEllipse draws a quarter, mirrored into the other three, as
 * two runs that may overlap. With F(x, y) = b²x² + a²y² - a²b², run A
 * starts at (0, b) and steps x by one, and y down by one too when that
 * leaves |F| smaller; it ends where y > 0 and b²(2x + 1) > a²(2y - 1),
 * drawing that point only on a wide ellipse (a >= b), or else where x
 * reaches a, drawing it. Run B starts at (a, 0) and steps y by one, and x
 * back by one when that leaves |F| smaller; it ends where x > 0 and
 * a²(2y - 1) > b²(2x + 1), not drawing that point, or else where y
 * reaches b, drawing it. The slope test comes first. (0, b) and (a, 0)
 * are always drawn. AreaEllipse fills each row out to the
 * outermost of those points. */
typedef long long ell_t;

struct ell {
    LONG a, b, x, y;
    ell_t a2, b2;
    int tall, phase, from;  /* phase: 0 (0, b), 1 run A, 2 (a, 0), 3 run B, 4 done */
};

static ell_t ell_f(const struct ell *e, LONG x, LONG y)
{
    ell_t f = e->b2 * x * x + e->a2 * y * y - e->a2 * e->b2;
    return f < 0 ? -f : f;
}

static void ell_start(struct ell *e, LONG a, LONG b)
{
    e->a = a; e->b = b;
    e->a2 = (ell_t)a * a; e->b2 = (ell_t)b * b;
    e->tall = b > a;
    e->x = 0; e->y = b;
    e->phase = 0;
}

/* The quarter's next point (x >= 0, y >= 0), and in e->from whether run A
 * (0) or run B (1) drew it; 0 at the end. A point can come more than once. */
static int ell_next(struct ell *e, LONG *px, LONG *py)
{
    for (;;) {
        LONG x = e->x, y = e->y;
        switch (e->phase) {
        case 0:
            e->phase = 1;
            e->from = 0;
            *px = 0; *py = e->b;
            return 1;
        case 1:
            if (y > 0 && e->b2 * (2 * x + 1) > e->a2 * (2 * y - 1)) {
                e->phase = 2;
                if (e->tall) continue;
            } else if (x >= e->a) {
                e->phase = 2;
            } else {
                e->x = x + 1;
                if (y > 0 && ell_f(e, x + 1, y - 1) < ell_f(e, x + 1, y)) e->y = y - 1;
            }
            *px = x; *py = y;
            return 1;
        case 2:
            e->phase = 3;
            e->from = 1;
            e->x = e->a; e->y = 0;
            *px = e->a; *py = 0;
            return 1;
        case 3:
            if (x > 0 && e->a2 * (2 * y - 1) > e->b2 * (2 * x + 1)) {
                e->phase = 4;
                continue;
            } else if (y >= e->b) {
                e->phase = 4;
            } else {
                e->y = y + 1;
                if (x > 0 && ell_f(e, x - 1, y + 1) < ell_f(e, x, y + 1)) e->x = x - 1;
            }
            *px = x; *py = y;
            return 1;
        default:
            return 0;
        }
    }
}

/* DrawEllipse's points in FgPen, solid, through Mask */
static void ell_draw(struct RastPort *rp, LONG xc, LONG yc, LONG a, LONG b)
{
    struct ell e;
    struct place pl;
    int have = 0, k;
    LONG x, y;

    ell_start(&e, a, b);
    while (ell_next(&e, &x, &y))
        for (k = 0; k < 4; k++) {
            LONG px = k & 1 ? xc - x : xc + x, py = k & 2 ? yc - y : yc + y;
            if (place_at(rp, &pl, &have, px, py))
                gfx_planar_put(&pl.bm, px + pl.dx, py + pl.dy, rp->FgPen, rp->Mask, GFX_SET);
        }
}

int ogfx_rp_DrawEllipse(struct RastPort *rp, LONG xc, LONG yc, LONG a, LONG b)
{
    if ((rp->DrawMode & (COMPLEMENT | INVERSVID)) || rp->LinePtrn != 0xFFFF || (WORD)a < 0 || (WORD)b < 0) return 0;
    ell_draw(rp, (WORD)xc, (WORD)yc, (WORD)a, (WORD)b);
    return 1;
}

/* ---- area fills -------------------------------------------------------------------
 * Measured on OS 3.2 (GfxCoreABITest AREAPROBE): an area is every pixel of
 * its edges, drawn as Draw draws them (from each vertex to the next, the
 * last back to the first), and between them every pixel with an odd
 * number of edges on its left, an edge counting in the rows from its upper
 * end down to, not including, its lower end (a horizontal edge in none).
 * An ellipse counts once on each side in each of its rows, and its pixels
 * are DrawEllipse's. Everything in one AreaEnd counts together, so where
 * two shapes overlap the inside is left out and the edges are not. The
 * area is then drawn like a RectFill through it: FgPen, the area pattern,
 * DrawMode and Mask. In JAM1 with AreaPtSz 2 (four pattern rows, or none
 * at all) the edges' pixels are taken out of the area instead (AREAPROBE4:
 * a two-point polygon draws nothing; with 1, 2, 8 or 16 rows, or any other
 * DrawMode, they are drawn). With AREAOUTLINE each polygon's edges are drawn over
 * it in AOlPen, solid, whatever DrawMode and LinePtrn say (LinePtrn's
 * count runs down as Draw's does), and each ellipse's DrawEllipse.
 *
 * The area is built as a one-plane mask in the RastPort's TmpRas, over the
 * part of the area's box the RastPort draws in: crossings first (a bit
 * toggled each row an edge counts in, those left of the box on its left
 * column), each row then filled from the left between them, the edges'
 * pixels set over that. */
struct amask {
    UBYTE *m;
    LONG x0, y0, x1, y1, bpr;
};

/* Each pixel Draw draws from (x0, y0) to (x1, y1), as line() steps them */
#define LINE_STEP(x0, y0, x1, y1, BODY) do { \
    LONG dx_ = (x1) > (x0) ? (x1) - (x0) : (x0) - (x1), dy_ = (y1) > (y0) ? (y1) - (y0) : (y0) - (y1); \
    LONG sx_ = (x1) >= (x0) ? 1 : -1, sy_ = (y1) >= (y0) ? 1 : -1; \
    LONG len_ = dx_ > dy_ ? dx_ : dy_, mn_ = dx_ > dy_ ? dy_ : dx_, err_ = 2 * mn_ - len_, i_; \
    LONG px = (x0), py = (y0); \
    for (i_ = 0; i_ <= len_; i_++) { \
        BODY; \
        if (err_ >= 0) { if (dx_ > dy_) py += sy_; else px += sx_; err_ -= 2 * len_; } \
        err_ += 2 * mn_; \
        if (dx_ > dy_) px += sx_; else py += sy_; \
    } \
} while (0)

/* how: 0 set the bit, 1 toggle it (a toggle left of the box toggles the
 * box's left column), 3 clear it */
static void am_bit(struct amask *a, LONG x, LONG y, int how)
{
    UBYTE *b, bit;
    if (y < a->y0 || y > a->y1 || x > a->x1) return;
    if (x < a->x0) {
        if (how != 1) return;
        x = a->x0;
    }
    b = a->m + (y - a->y0) * a->bpr + ((x - a->x0) >> 3);
    bit = (UBYTE)(0x80 >> ((x - a->x0) & 7));
    if (how == 1) *b ^= bit;
    else if (how == 3) *b &= (UBYTE)~bit;
    else *b |= bit;
}

/* An edge's crossings: one a row from its upper end down to its lower
 * end, the lower end's row left out */
static void am_cross(struct amask *a, LONG x0, LONG y0, LONG x1, LONG y1)
{
    LONG bot = y0 > y1 ? y0 : y1, last = -0x10000;
    if (y0 == y1) return;
    LINE_STEP(x0, y0, x1, y1, if (py != bot && py != last) { am_bit(a, px, py, 1); last = py; });
}

static void am_edge(struct amask *a, LONG x0, LONG y0, LONG x1, LONG y1, int how)
{
    LINE_STEP(x0, y0, x1, y1, am_bit(a, px, py, how ? 3 : 0));
}

/* An ellipse's crossings (how 1) or pixels (how as am_bit's): a crossing
 * on each side of each row, at the first point that row gets (run A's
 * rows, then run B's below them) */
static void am_ellipse(struct amask *a, LONG xc, LONG yc, LONG ea, LONG eb, int how)
{
    struct ell e;
    LONG x, y, last_a = -1, last_b = -1, low_a = 0x10000;

    ell_start(&e, ea, eb);
    while (ell_next(&e, &x, &y)) {
        if (how == 1) {
            if (!e.from) {
                if (y == last_a) continue;
                last_a = low_a = y;
            } else {
                if (y >= low_a || y == last_b) continue;
                last_b = y;
            }
        }
        am_bit(a, xc + x, yc + y, how);
        am_bit(a, xc - x, yc + y, how);
        if (y) {
            am_bit(a, xc + x, yc - y, how);
            am_bit(a, xc - x, yc - y, how);
        }
    }
}

/* Each row filled from the left: a bit toggles between outside and
 * inside, and is inside itself either way */
static void am_fill(struct amask *a)
{
    LONG y, j, words = a->bpr >> 1;
    for (y = 0; y <= a->y1 - a->y0; y++) {
        UBYTE *r = a->m + y * a->bpr;
        UWORD in = 0;
        for (j = 0; j < words; j++) {
            UWORD w = (UWORD)(r[2 * j] << 8 | r[2 * j + 1]), p = w;
            p ^= p >> 1; p ^= p >> 2; p ^= p >> 4; p ^= p >> 8;
            if (in) p = (UWORD)~p;
            in = p & 1;
            p |= w;
            r[2 * j] = (UBYTE)(p >> 8);
            r[2 * j + 1] = (UBYTE)p;
        }
    }
}

/* Row y's pixels xa..xb through the area pattern wherever the RastPort
 * draws, cut to it */
static void area_span(struct RastPort *rp, LONG xa, LONG xb, LONG y)
{
    struct walk w;
    struct place pl;
    LONG r[4];

    walk_start(&w, rp);
    while (walk_next(&w, &pl)) {
        uint32_t pat, r16;
        int mode;
        if (!cut_to(&pl, xa, y, xb, y, r)) continue;
        pat = area_row(rp, y);
        r16 = (uint32_t)pl.dx & 15;
        if (r16) pat = (pat >> r16 | pat << (16 - r16)) & 0xFFFF;
        mode = pat_mode(rp, &pat);
        gfx_planar_span(&pl.bm, r[0], r[2], r[1], pat, rp->FgPen, rp->BgPen, rp->Mask, mode);
    }
}

static void am_draw(struct RastPort *rp, struct amask *a)
{
    LONG y, x, run, w = a->x1 - a->x0 + 1;
    for (y = a->y0; y <= a->y1; y++) {
        const UBYTE *r = a->m + (y - a->y0) * a->bpr;
        for (x = 0; x < w; x = run + 1) {
            run = x;
            if (!(r[x >> 3] & (0x80 >> (x & 7)))) continue;
            while (run + 1 < w && (r[(run + 1) >> 3] & (0x80 >> ((run + 1) & 7)))) run++;
            area_span(rp, a->x0 + x, a->x0 + run, y);
        }
    }
}

/* The AreaInfo table, as OS 3.2 keeps it (measured by AREAPROBE2): a
 * vector is two WORDs (x, y) in VctrTbl and a flag byte in FlagTbl. A
 * move is flagged 0x00, a draw 0x03, and an ellipse takes two vectors,
 * its box's corners (xc - a, yc - b) flagged 0x83 and (xc + a, yc + b)
 * 0x02. A level draw is flagged 0x02, not 0x03 (AREAPROBE4), and with
 * AREAOUTLINE set a draw or an ellipse has 0x04 too. FirstX, FirstY is the
 * open polygon's first vertex. A polygon is closed, with a draw back to
 * that vertex, by AreaMove, AreaEllipse and AreaEnd when its last vertex
 * is elsewhere. A move or an ellipse after a move takes its place. */
#define AV_MOVE 0x00
#define AV_DRAW 0x03
#define AV_ELL  0x83
#define AV_ELL2 0x02

/* What vector i is: a move, a draw or an ellipse's first vector */
#define AV_IS_ELL(ai, i)  (((UBYTE)(ai)->FlagTbl[i] & 0x80) != 0)
#define AV_IS_DRAW(ai, i) (!AV_IS_ELL(ai, i) && ((UBYTE)(ai)->FlagTbl[i] & 0x02) && \
                           !((i) > 0 && AV_IS_ELL(ai, (i) - 1)))
#define AV_IS_MOVE(ai, i) (!AV_IS_ELL(ai, i) && !AV_IS_DRAW(ai, i) && !((i) > 0 && AV_IS_ELL(ai, (i) - 1)))

static void av_put(struct AreaInfo *ai, LONG x, LONG y, int flag)
{
    ai->VctrTbl[2 * ai->Count] = (WORD)x;
    ai->VctrTbl[2 * ai->Count + 1] = (WORD)y;
    ai->FlagTbl[ai->Count] = (BYTE)flag;
    ai->Count++;
    ai->VctrPtr = ai->VctrTbl + 2 * ai->Count;
    ai->FlagPtr = ai->FlagTbl + ai->Count;
}

/* A draw to (x, y), flagged as OS 3.2 flags it */
static void av_draw(struct RastPort *rp, LONG x, LONG y)
{
    struct AreaInfo *ai = rp->AreaInfo;
    int f = 0x02;
    if (ai->Count == 0 || ai->VctrTbl[2 * ai->Count - 1] != (WORD)y) f |= 0x01;
    if (rp->Flags & AREAOUTLINE) f |= 0x04;
    av_put(ai, x, y, f);
}

/* Whether the open polygon needs a draw back to its first vertex */
static int av_open(struct AreaInfo *ai)
{
    LONG n = ai->Count - 1;
    return n >= 0 && AV_IS_DRAW(ai, n) &&
           (ai->VctrTbl[2 * n] != ai->FirstX || ai->VctrTbl[2 * n + 1] != ai->FirstY);
}

LONG ogfx_rp_AreaMove(struct RastPort *rp, LONG x, LONG y)
{
    struct AreaInfo *ai = rp->AreaInfo;

    if (ai->Count > 0 && AV_IS_MOVE(ai, ai->Count - 1)) {
        /* a move after a move takes its place */
        ai->Count--;
    } else {
        /* the open polygon is closed first, if there is room, even when
         * there is then none for the move (measured) */
        if (av_open(ai)) {
            if (ai->Count >= ai->MaxCount) return -1;
            av_draw(rp, ai->FirstX, ai->FirstY);
        }
        if (ai->Count >= ai->MaxCount) return -1;
    }
    av_put(ai, x, y, AV_MOVE);
    ai->FirstX = (WORD)x;
    ai->FirstY = (WORD)y;
    return 0;
}

LONG ogfx_rp_AreaDraw(struct RastPort *rp, LONG x, LONG y)
{
    struct AreaInfo *ai = rp->AreaInfo;

    if (ai->Count >= ai->MaxCount) return -1;
    av_draw(rp, x, y);
    return 0;
}

LONG ogfx_rp_AreaEllipse(struct RastPort *rp, LONG xc, LONG yc, LONG a, LONG b)
{
    struct AreaInfo *ai = rp->AreaInfo;

    /* taking a move's place, or closing the open polygon first, as
     * AreaMove does */
    if (ai->Count > 0 && AV_IS_MOVE(ai, ai->Count - 1)) ai->Count--;
    else if (av_open(ai)) {
        if (ai->Count >= ai->MaxCount) return -1;
        av_draw(rp, ai->FirstX, ai->FirstY);
    }
    if (ai->Count + 2 > ai->MaxCount) return -1;
    av_put(ai, (WORD)xc - (WORD)a, (WORD)yc - (WORD)b, AV_ELL | (rp->Flags & AREAOUTLINE ? 0x04 : 0));
    av_put(ai, (WORD)xc + (WORD)a, (WORD)yc + (WORD)b, AV_ELL2);
    return 0;
}

/* Every edge and ellipse of the table: crossings (pass 0), pixels (1) or
 * pixels taken out (2) */
static void am_shapes(struct amask *a, struct AreaInfo *ai, int pass)
{
    LONG i, px = 0, py = 0;
    for (i = 0; i < ai->Count; i++) {
        LONG x = ai->VctrTbl[2 * i], y = ai->VctrTbl[2 * i + 1];
        if (AV_IS_ELL(ai, i) && i + 1 < ai->Count) {
            LONG x1 = ai->VctrTbl[2 * i + 2], y1 = ai->VctrTbl[2 * i + 3];
            am_ellipse(a, (x + x1) >> 1, (y + y1) >> 1, (x1 - x) >> 1, (y1 - y) >> 1, pass == 0 ? 1 : pass == 1 ? 0 : 3);
            i++;
            x = x1; y = y1;
        } else if (AV_IS_DRAW(ai, i)) {
            if (pass) am_edge(a, px, py, x, y, pass - 1); else am_cross(a, px, py, x, y);
        }
        px = x; py = y;
    }
}

int ogfx_rp_takes_area(struct RastPort *rp)
{
    struct AreaInfo *ai = rp->AreaInfo;
    struct Layer *l = rp->Layer;
    LONG i;

    if (rp->Flags & 0x20) return 0;     /* NOCROSSFILL */
    /* area patterns of more than 16 rows and multicolour ones: not measured */
    if (rp->AreaPtrn && (rp->AreaPtSz < 0 || rp->AreaPtSz > 4)) return 0;
    if (!l || !(rp->Flags & AREAOUTLINE)) return 1;
    for (i = 0; i < ai->Count; i++) {
        LONG x = ai->VctrTbl[2 * i] - l->Scroll_X, y = ai->VctrTbl[2 * i + 1] - l->Scroll_Y;
        if (!AV_IS_ELL(ai, i) && !(i > 0 && AV_IS_ELL(ai, i - 1)) &&
            (x < 0 || y < 0 || x > l->bounds.MaxX - l->bounds.MinX || y > l->bounds.MaxY - l->bounds.MinY))
            return 0;
    }
    return 1;
}

LONG ogfx_rp_AreaEnd(struct RastPort *rp)
{
    struct AreaInfo *ai = rp->AreaInfo;
    struct TmpRas *tr = rp->TmpRas;
    struct amask a;
    LONG i, n, bx0 = 0x7FFF, by0 = 0x7FFF, bx1 = -0x8000, by1 = -0x8000, cx0, cy0, cx1, cy1;
    LONG rc = 0, shapes = 0;

    if (av_open(ai)) {
        if (ai->Count >= ai->MaxCount) { rc = -1; goto out; }
        av_draw(rp, ai->FirstX, ai->FirstY);
    }
    if (!tr || !tr->RasPtr) { rc = -1; goto out; }
    n = ai->Count;
    for (i = 0; i < n; i++) {
        LONG x = ai->VctrTbl[2 * i], y = ai->VctrTbl[2 * i + 1];
        if (AV_IS_MOVE(ai, i) && (i + 1 >= n || !AV_IS_DRAW(ai, i + 1))) continue;
        if (x < bx0) bx0 = x;
        if (x > bx1) bx1 = x;
        if (y < by0) by0 = y;
        if (y > by1) by1 = y;
    }
    if (bx0 > bx1) goto out;
    shapes = 1;
    /* the part of the box the RastPort draws in */
    if (rp->Layer) {
        struct Layer *l = rp->Layer;
        cx0 = l->Scroll_X; cy0 = l->Scroll_Y;
        cx1 = cx0 + l->bounds.MaxX - l->bounds.MinX;
        cy1 = cy0 + l->bounds.MaxY - l->bounds.MinY;
    } else {
        cx0 = cy0 = 0;
        cx1 = (LONG)rp->BitMap->BytesPerRow * 8 - 1;
        cy1 = (LONG)rp->BitMap->Rows - 1;
    }
    a.x0 = bx0 > cx0 ? bx0 : cx0;
    a.y0 = by0 > cy0 ? by0 : cy0;
    a.x1 = bx1 < cx1 ? bx1 : cx1;
    a.y1 = by1 < cy1 ? by1 : cy1;
    if (a.x0 > a.x1 || a.y0 > a.y1) goto out;
    a.bpr = ((a.x1 - a.x0 + 16) >> 4) * 2;
    if (a.bpr * (a.y1 - a.y0 + 1) > tr->Size) { rc = -1; goto out; }
    a.m = (UBYTE *)tr->RasPtr;
    for (i = 0; i < a.bpr * (a.y1 - a.y0 + 1); i++) a.m[i] = 0;
    am_shapes(&a, ai, 0);
    am_fill(&a);
    am_shapes(&a, ai, rp->DrawMode == JAM1 && rp->AreaPtSz == 2 ? 2 : 1);
    am_draw(rp, &a);
out:
    rp->Flags |= FRST_DOT;
    /* a table with nothing drawn in it gets no outline (measured) */
    if (!rc && shapes && (rp->Flags & AREAOUTLINE)) {
        /* the outlines: polygons drawn as Draw draws, in AOlPen, solid */
        UBYTE fg = rp->FgPen, dm = rp->DrawMode;
        UWORD lp = rp->LinePtrn;
        rp->FgPen = rp->AOlPen; rp->DrawMode = JAM1; rp->LinePtrn = 0xFFFF;
        for (i = 0; i < ai->Count; i++) {
            LONG x = ai->VctrTbl[2 * i], y = ai->VctrTbl[2 * i + 1];
            /* a lone move left last moves the pen too (measured) */
            if (AV_IS_MOVE(ai, i)) ogfx_rp_Move(rp, x, y);
            else if (AV_IS_DRAW(ai, i)) ogfx_rp_Draw(rp, x, y);
            else if (AV_IS_ELL(ai, i) && i + 1 < ai->Count) {
                LONG x1 = ai->VctrTbl[2 * i + 2], y1 = ai->VctrTbl[2 * i + 3];
                ell_draw(rp, (x + x1) >> 1, (y + y1) >> 1, (x1 - x) >> 1, (y1 - y) >> 1);
                i++;
            }
        }
        rp->FgPen = fg; rp->DrawMode = dm; rp->LinePtrn = lp;
        /* and SetDrPt's "new pattern" bit is cleared (measured) */
        rp->Flags &= ~0x10;
    }
    ai->Count = 0;
    ai->VctrPtr = ai->VctrTbl;
    ai->FlagPtr = ai->FlagTbl;
    return rc;
}

/* Measured on OS 3.2: unlayered, EraseRect clears the rectangle to pen 0
 * through Mask, whatever BgPen and DrawMode say. */
int ogfx_rp_EraseRect(struct RastPort *rp, LONG xmin, LONG ymin, LONG xmax, LONG ymax)
{
    struct gfx_planar bm;
    ULONG x0 = (ULONG)(WORD)xmin, y0 = (ULONG)(WORD)ymin, x1 = (ULONG)(WORD)xmax, y1 = (ULONG)(WORD)ymax, y;

    if (rp->Layer) return 0;
    if ((WORD)xmin < 0 || (WORD)ymin < 0 || !cut(rp, &x0, &y0, &x1, &y1)) return 1;
    planar_of(rp, &bm);
    for (y = y0; y <= y1; y++) gfx_planar_span(&bm, x0, x1, y, 0xFFFF, 0, 0, rp->Mask, GFX_JAM1);
    return 1;
}

/* ---- blits ------------------------------------------------------------------- */

/* The rectangle cut to both bitmaps (source sw x sh, destination dw x dh):
 * 0 when nothing is left. */
static int cut_blit(LONG *sx, LONG *sy, LONG *dx, LONG *dy, LONG *w, LONG *h, LONG sw, LONG sh, LONG dw, LONG dh)
{
    if (*sx < 0) { *dx -= *sx; *w += *sx; *sx = 0; }
    if (*sy < 0) { *dy -= *sy; *h += *sy; *sy = 0; }
    if (*dx < 0) { *sx -= *dx; *w += *dx; *dx = 0; }
    if (*dy < 0) { *sy -= *dy; *h += *dy; *dy = 0; }
    if (*sx + *w > sw) *w = sw - *sx;
    if (*sy + *h > sh) *h = sh - *sy;
    if (*dx + *w > dw) *w = dw - *dx;
    if (*dy + *h > dh) *h = dh - *dy;
    return *w > 0 && *h > 0;
}

/* The planes a blit draws: those in mask that both have, the destination's
 * not 0 or -1 (graphics.library's count, measured on OS 3.2) */
static LONG planes_drawn(const struct gfx_planar *s, const struct gfx_planar *d, ULONG mask)
{
    uint32_t depth = s->depth < d->depth ? s->depth : d->depth, p;
    LONG n = 0;
    for (p = 0; p < depth; p++)
        if (mask >> p & 1 && d->planes[p] && d->planes[p] != (uint8_t *)-1) n++;
    return n;
}

LONG ogfx_rp_BltBitMap(struct BitMap *src, LONG sx, LONG sy, struct BitMap *dst, LONG dx, LONG dy,
                       LONG w, LONG h, ULONG minterm, ULONG mask, UBYTE *temp)
{
    struct gfx_planar s, d;

    (void)temp;
    sx = (WORD)sx; sy = (WORD)sy; dx = (WORD)dx; dy = (WORD)dy; w = (WORD)w; h = (WORD)h;
    planar_from(src, &s);
    planar_from(dst, &d);
    if (!cut_blit(&sx, &sy, &dx, &dy, &w, &h, (LONG)src->BytesPerRow * 8, src->Rows,
                  (LONG)dst->BytesPerRow * 8, dst->Rows))
        return 0;
    gfx_planar_blit(&s, sx, sy, &d, dx, dy, w, h, minterm & 0xF0, mask & 0xFF);
    return planes_drawn(&s, &d, mask & 0xFF);
}

void ogfx_rp_BltBitMapRastPort(struct BitMap *src, LONG sx, LONG sy, struct RastPort *rp, LONG dx, LONG dy,
                               LONG w, LONG h, ULONG minterm)
{
    struct gfx_planar s;
    struct walk wk;
    struct place pl;
    LONG r[4];

    sx = (WORD)sx; sy = (WORD)sy; dx = (WORD)dx; dy = (WORD)dy; w = (WORD)w; h = (WORD)h;
    if (w <= 0 || h <= 0) return;
    planar_from(src, &s);
    walk_start(&wk, rp);
    while (walk_next(&wk, &pl)) {
        LONG bsx, bsy, bw, bh, bdx, bdy;
        if (!cut_to(&pl, dx, dy, dx + w - 1, dy + h - 1, r)) continue;
        /* the part of the source under this place's piece */
        bdx = r[0]; bdy = r[1];
        bsx = sx + (r[0] - pl.dx - dx); bsy = sy + (r[1] - pl.dy - dy);
        bw = r[2] - r[0] + 1; bh = r[3] - r[1] + 1;
        if (!cut_blit(&bsx, &bsy, &bdx, &bdy, &bw, &bh, (LONG)src->BytesPerRow * 8, src->Rows,
                      pl.x1 + 1, pl.y1 + 1))
            continue;
        gfx_planar_blit(&s, bsx, bsy, &pl.bm, bdx, bdy, bw, bh, minterm & 0xF0, rp->Mask);
    }
}

/* ---- copies between RastPorts: ClipBlit and ScrollRaster --------------------
 * Row by row, CHUNK pixels at a time: the source's pixels are read from
 * every place of srp first, then the destination's from drp, mixed by the
 * minterm and written back through the mask. A destination pixel whose
 * source pixel lands in no place (outside the layer, or in a simple
 * refresh layer's covered part) is left as it is. When both RastPorts draw
 * on one bitmap, the rows and chunks go in the order that reads every
 * source pixel before the copy overwrites it, judged on the screen (a
 * layer's offset included), so the whole rectangle moves as one, as with
 * the blitter's own ordering. */
#define CHUNK 128

static LONG off_x(struct RastPort *rp) { return rp->Layer ? rp->Layer->bounds.MinX - rp->Layer->Scroll_X : 0; }
static LONG off_y(struct RastPort *rp) { return rp->Layer ? rp->Layer->bounds.MinY - rp->Layer->Scroll_Y : 0; }

static void rp_copy(struct RastPort *srp, LONG sx, LONG sy, struct RastPort *drp, LONG dx, LONG dy,
                    LONG w, LONG h, ULONG minterm, ULONG mask)
{
    uint8_t sbuf[CHUNK], kept[CHUNK], dbuf[CHUNK];
    uint8_t m7 = minterm & 0x80 ? 0xFF : 0, m6 = minterm & 0x40 ? 0xFF : 0;
    uint8_t m5 = minterm & 0x20 ? 0xFF : 0, m4 = minterm & 0x10 ? 0xFF : 0;
    int same = srp->BitMap == drp->BitMap, p;
    int up, left;
    uint32_t depth = srp->BitMap->Depth < drp->BitMap->Depth ? srp->BitMap->Depth : drp->BitMap->Depth;
    LONG j, c, nchunks = (w + CHUNK - 1) / CHUNK;

    if (w <= 0 || h <= 0) return;
    /* two BitMaps on the same planes are one bitmap */
    for (p = 0; !same && p < (int)depth; p++)
        if (srp->BitMap->Planes[p] == drp->BitMap->Planes[p] && srp->BitMap->BytesPerRow == drp->BitMap->BytesPerRow) same = 1;
    up = same && dy + off_y(drp) > sy + off_y(srp);
    left = same && dx + off_x(drp) > sx + off_x(srp);
    if (depth < 8) mask &= (1u << depth) - 1;
    for (j = 0; j < h; j++) {
        LONG row = up ? h - 1 - j : j;
        for (c = 0; c < nchunks; c++) {
            LONG c0 = (left ? nchunks - 1 - c : c) * CHUNK, n = w - c0 < CHUNK ? w - c0 : CHUNK, i, k, r[4];
            struct walk wk;
            struct place pl;
            for (i = 0; i < n; i++) kept[i] = 0;
            walk_start(&wk, srp);
            while (walk_next(&wk, &pl)) {
                if (!cut_to(&pl, sx + c0, sy + row, sx + c0 + n - 1, sy + row, r)) continue;
                i = r[0] - pl.dx - (sx + c0);
                gfx_planar_read8(&pl.bm, r[0], r[1], r[2], r[3], sbuf + i, CHUNK);
                for (k = 0; k <= r[2] - r[0]; k++) kept[i + k] = 1;
            }
            walk_start(&wk, drp);
            while (walk_next(&wk, &pl)) {
                LONG o;
                if (!cut_to(&pl, dx + c0, dy + row, dx + c0 + n - 1, dy + row, r)) continue;
                o = r[0] - pl.dx - (dx + c0);
                gfx_planar_read8(&pl.bm, r[0], r[1], r[2], r[3], dbuf, CHUNK);
                for (k = 0; k <= r[2] - r[0]; k++) {
                    uint8_t b = sbuf[o + k], d = dbuf[k];
                    if (kept[o + k]) dbuf[k] = (uint8_t)((b & d & m7) | (b & ~d & m6) | (~b & d & m5) | (~b & ~d & m4));
                }
                gfx_planar_write8(&pl.bm, r[0], r[1], r[2], r[3], dbuf, CHUNK, mask);
            }
        }
    }
}

int ogfx_rp_takes_copy(struct RastPort *rp)
{
    struct Layer *l = rp->Layer;

    /* a simple refresh layer: OS 3.2 adds damage for what a copy leaves; a
     * clip region: what OS 3.2 does there is not measured yet */
    return !l || !(l->Flags & (LAYERSUPER | LAYERSIMPLE) || l->ClipRegion);
}

/* 1 when the rectangle lies inside the RastPort's layer (or there is none) */
static int inside(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1)
{
    struct Layer *l = rp->Layer;

    if (!l) return 1;
    return x0 >= l->Scroll_X && y0 >= l->Scroll_Y && x1 <= l->Scroll_X + l->bounds.MaxX - l->bounds.MinX &&
           y1 <= l->Scroll_Y + l->bounds.MaxY - l->bounds.MinY;
}

int ogfx_rp_ClipBlit(struct RastPort *srp, LONG sx, LONG sy, struct RastPort *drp, LONG dx, LONG dy,
                     LONG w, LONG h, ULONG minterm)
{
    sx = (WORD)sx; sy = (WORD)sy; dx = (WORD)dx; dy = (WORD)dy; w = (WORD)w; h = (WORD)h;
    if (w <= 0 || h <= 0) return 1;
    if (!inside(srp, sx, sy, sx + w - 1, sy + h - 1) || !inside(drp, dx, dy, dx + w - 1, dy + h - 1)) return 0;
    rp_copy(srp, sx, sy, drp, dx, dy, w, h, minterm & 0xF0, drp->Mask);
    return 1;
}

int ogfx_rp_ScrollRaster(struct RastPort *rp, LONG dx, LONG dy, LONG x0, LONG y0, LONG x1, LONG y1)
{
    LONG w, h, adx, ady;
    UBYTE pen = rp->FgPen, mode = rp->DrawMode;
    UWORD *ptrn = rp->AreaPtrn;

    dx = (WORD)dx; dy = (WORD)dy; x0 = (WORD)x0; y0 = (WORD)y0; x1 = (WORD)x1; y1 = (WORD)y1;
    if (x0 > x1 || y0 > y1) return 1;
    w = x1 - x0 + 1; h = y1 - y0 + 1;
    adx = dx < 0 ? -dx : dx; ady = dy < 0 ? -dy : dy;
    /* unlayered, OS 3.2 leaves the rectangle alone when the shift is its
     * size or more; in a layer it draws outside the rectangle then, and
     * clips one that is partly outside the layer: those stay with it */
    if (rp->Layer && (adx >= w || ady >= h || !inside(rp, x0, y0, x1, y1))) return 0;
    if (adx >= w || ady >= h) return 1;
    rp_copy(rp, x0 + (dx > 0 ? dx : 0), y0 + (dy > 0 ? dy : 0), rp, x0 + (dx < 0 ? -dx : 0), y0 + (dy < 0 ? -dy : 0),
            w - adx, h - ady, 0xC0, rp->Mask);
    /* the space it leaves: solid BgPen through the write mask, whatever the
     * draw mode and area pattern */
    rp->FgPen = rp->BgPen; rp->DrawMode = JAM1; rp->AreaPtrn = NULL;
    if (dx > 0) ogfx_rp_RectFill(rp, x1 - dx + 1, y0, x1, y1);
    if (dx < 0) ogfx_rp_RectFill(rp, x0, y0, x0 - dx - 1, y1);
    if (dy > 0) ogfx_rp_RectFill(rp, x0, y1 - dy + 1, x1, y1);
    if (dy < 0) ogfx_rp_RectFill(rp, x0, y0, x1, y0 - dy - 1);
    rp->FgPen = pen; rp->DrawMode = mode; rp->AreaPtrn = ptrn;
    return 1;
}

/* ---- text -------------------------------------------------------------------
 * Measured on OS 3.2 (GfxCoreABITest TEXTPROBE, graphics 47.10). A
 * font's character c is glyph c - tf_LoChar, or for a character outside
 * tf_LoChar..tf_HiChar the glyph after tf_HiChar's. A glyph's image is
 * CharLoc's width bits from bit offset CharLoc's offset of each of the
 * font's tf_YSize rows of tf_CharData (tf_Modulo bytes apart). The image
 * lands CharKern's pixels (0 without CharKern) right of the pen, and the
 * pen moves on by CharSpace's width (tf_XSize without CharSpace), the
 * kerning and the RastPort's TxSpacing, with or without
 * FPF_PROPORTIONAL. The rows are drawn from the baseline at cp_y up
 * tf_Baseline rows.
 *
 * The text's box runs from the pen, or the leftmost image pixel if that is
 * further left, to the end of its width, or the rightmost image pixel if
 * that is further right, over all its rows. AlgoStyle's bold ORs the
 * images again tf_BoldSmear pixels right (not widening a character),
 * and adds that to the box's right. Italic moves row r right by
 * floor((tf_Baseline - r) / 2) and widens the box by tf_Baseline / 2
 * on the right and (tf_YSize - tf_Baseline + 1) / 2 on the left.
 * Underline fills the row below the baseline (when the font has one)
 * across the box, leaving a pixel clear each side of the image's pixels
 * there. The whole is drawn as a template through the DrawMode, JAM2
 * filling the box. TextExtent gives the box; for no characters, height 0
 * and 0,0..-1,-1. */
struct tx {
    struct TextFont *tf;
    const UBYTE *s;
    LONG n, spacing, smear, style;
};

static void tx_start(struct tx *t, struct RastPort *rp, const UBYTE *s, LONG n)
{
    t->tf = rp->Font;
    t->s = s;
    t->n = n;
    t->spacing = rp->TxSpacing;
    t->style = rp->AlgoStyle & (FSF_UNDERLINED | FSF_BOLD | FSF_ITALIC);
    t->smear = t->style & FSF_BOLD ? (LONG)t->tf->tf_BoldSmear : 0;
}

static LONG tx_index(const struct TextFont *tf, UBYTE c)
{
    if (c < tf->tf_LoChar || c > tf->tf_HiChar) return (LONG)tf->tf_HiChar - tf->tf_LoChar + 1;
    return (LONG)c - tf->tf_LoChar;
}

static LONG tx_kern(const struct TextFont *tf, LONG g)
{
    return tf->tf_CharKern ? ((const WORD *)tf->tf_CharKern)[g] : 0;
}

/* how far glyph g moves the pen */
static LONG tx_adv(const struct tx *t, LONG g)
{
    const struct TextFont *tf = t->tf;
    LONG sp = tf->tf_CharSpace ? ((const WORD *)tf->tf_CharSpace)[g] : (LONG)tf->tf_XSize;
    return sp + tx_kern(tf, g) + t->spacing;
}

/* how far italic moves row r right: floor((tf_Baseline - r) / 2) */
static LONG tx_slant(const struct tx *t, LONG r)
{
    LONG d = (LONG)t->tf->tf_Baseline - r;
    if (!(t->style & FSF_ITALIC)) return 0;
    return d >= 0 ? d / 2 : -((1 - d) / 2);
}

/* The string's width (where the pen ends, from where it began) and its
 * box's columns from the pen, inclusive (0..-1 for no characters). */
static LONG tx_measure(const struct tx *t, LONG *x0, LONG *x1)
{
    const struct TextFont *tf = t->tf;
    LONG i, pos = 0, lo = 0, hi = -0x7FFFFFFF;

    for (i = 0; i < t->n; i++) {
        LONG g = tx_index(tf, t->s[i]), w = ((const UWORD *)tf->tf_CharLoc)[2 * g + 1], left = pos + tx_kern(tf, g);
        if (w) {
            if (left < lo) lo = left;
            if (left + w - 1 > hi) hi = left + w - 1;
        }
        pos += tx_adv(t, g);
    }
    if (!t->n) { *x0 = 0; *x1 = -1; return 0; }
    if (pos - 1 > hi) hi = pos - 1;
    hi += t->smear;
    if (t->style & FSF_ITALIC) {
        hi += tf->tf_Baseline / 2;
        lo -= ((LONG)tf->tf_YSize - tf->tf_Baseline + 1) / 2;
    }
    *x0 = lo;
    *x1 = hi;
    return pos;
}

/* n bits (1 to 16) of a glyph row from bit off, left aligned */
static UWORD tx_get(const UBYTE *row, ULONG mod, ULONG off, LONG n)
{
    ULONG b = off >> 3, v = 0;
    int k;
    for (k = 0; k < 3; k++) v = v << 8 | (b + k < mod ? row[b + k] : 0);
    return (UWORD)((v << (off & 7)) >> 8 & (0xFFFF0000UL >> n));
}

/* OR v (16 bits) into buf from bit d */
static void tx_or(UBYTE *buf, LONG d, UWORD v)
{
    ULONG w = (ULONG)v << 8 >> (d & 7);
    UBYTE *p = buf + (d >> 3);
    p[0] |= (UBYTE)(w >> 16);
    p[1] |= (UBYTE)(w >> 8);
    p[2] |= (UBYTE)w;
}

static int tx_bit(const UBYTE *buf, LONG x)
{
    return buf[x >> 3] >> (7 - (x & 7)) & 1;
}

#define TX_CHUNK 512            /* pixels of a row built at a time */
#define TX_BITS (TX_CHUNK + 16) /* with a byte each side */
#define TX_BUF (TX_BITS / 8 + 3)

/* Row r of the template for bitmap columns cx - 8 .. cx + TX_CHUNK + 7
 * into buf (TX_BUF bytes; bit 0 is column cx - 8), the pen at bitmap
 * column px and the box from bitmap column bx0 to bx1. */
static void tx_row(const struct tx *t, LONG r, LONG px, LONG bx0, LONG bx1, LONG cx, UBYTE *buf)
{
    const struct TextFont *tf = t->tf;
    const UBYTE *row = (const UBYTE *)tf->tf_CharData + (ULONG)r * tf->tf_Modulo;
    LONG i, k, x, pos = px + tx_slant(t, r) - (cx - 8);

    for (i = 0; i < TX_BUF; i++) buf[i] = 0;
    for (i = 0; i < t->n; i++) {
        LONG g = tx_index(tf, t->s[i]);
        LONG off = ((const UWORD *)tf->tf_CharLoc)[2 * g], w = ((const UWORD *)tf->tf_CharLoc)[2 * g + 1];
        LONG left = pos + tx_kern(tf, g);
        pos += tx_adv(t, g);
        for (k = 0; k <= t->smear; k += t->smear ? t->smear : 1) {
            LONG j;
            for (j = 0; j < w; j += 16) {
                LONG n = w - j > 16 ? 16 : w - j, d = left + k + j;
                UWORD v;
                if (d >= TX_BITS || d + n <= 0) continue;
                v = tx_get(row, tf->tf_Modulo, (ULONG)(off + j), n);
                if (d < 0) { v = (UWORD)(v << -d); n += d; d = 0; }
                if (d + n > TX_BITS) n = TX_BITS - d;
                tx_or(buf, d, (UWORD)(v & (0xFFFF0000UL >> n)));
            }
        }
    }
    if (t->style & FSF_UNDERLINED && r == (LONG)tf->tf_Baseline + 1) {
        LONG a = bx0 - (cx - 8), b = bx1 - (cx - 8);
        UBYTE u[TX_BUF];
        for (i = 0; i < TX_BUF; i++) u[i] = 0;
        if (a < 1) a = 1;
        if (b > TX_BITS - 2) b = TX_BITS - 2;
        for (x = a; x <= b; x++)
            if (!tx_bit(buf, x - 1) && !tx_bit(buf, x + 1)) u[x >> 3] |= (UBYTE)(0x80 >> (x & 7));
        for (i = 0; i < TX_BUF; i++) buf[i] |= u[i];
    }
}

WORD ogfx_rp_TextLength(struct RastPort *rp, CONST_STRPTR s, ULONG n)
{
    struct tx t;
    LONG x0, x1;
    tx_start(&t, rp, (const UBYTE *)s, (LONG)(UWORD)n);
    return (WORD)tx_measure(&t, &x0, &x1);
}

void ogfx_rp_TextExtent(struct RastPort *rp, CONST_STRPTR s, ULONG n, struct TextExtent *te)
{
    struct tx t;
    LONG x0, x1;
    tx_start(&t, rp, (const UBYTE *)s, (LONG)(UWORD)n);
    te->te_Width = (WORD)tx_measure(&t, &x0, &x1);
    te->te_Extent.MinX = (WORD)x0;
    te->te_Extent.MaxX = (WORD)x1;
    if (!t.n) {
        te->te_Height = 0;
        te->te_Extent.MinY = 0;
        te->te_Extent.MaxY = -1;
        return;
    }
    te->te_Height = t.tf->tf_YSize;
    te->te_Extent.MinY = (WORD)-t.tf->tf_Baseline;
    te->te_Extent.MaxY = (WORD)(t.tf->tf_YSize - 1 - t.tf->tf_Baseline);
}

int ogfx_rp_takes_text(struct RastPort *rp)
{
    struct TextFont *tf = rp->Font;
    return tf && !(tf->tf_Style & FSF_COLORFONT) && !(tf->tf_Flags & FPF_REVPATH) && ogfx_rp_takes(rp, 0);
}

int ogfx_rp_Text(struct RastPort *rp, CONST_STRPTR s, ULONG n)
{
    struct tx t;
    struct walk w;
    struct place pl;
    UBYTE buf[TX_BUF];
    LONG x0, x1, len, top, r[4], y, cx, j;

    if (!ogfx_rp_takes_text(rp)) return 0;
    tx_start(&t, rp, (const UBYTE *)s, (LONG)(UWORD)n);
    len = tx_measure(&t, &x0, &x1);
    if (x0 <= x1) {
        top = rp->cp_y - (LONG)t.tf->tf_Baseline;
        walk_start(&w, rp);
        while (walk_next(&w, &pl)) {
            LONG px = rp->cp_x + pl.dx;
            if (!cut_to(&pl, rp->cp_x + x0, top, rp->cp_x + x1, top + t.tf->tf_YSize - 1, r)) continue;
            for (y = r[1]; y <= r[3]; y++)
                for (cx = r[0] & ~15L; cx <= r[2]; cx += TX_CHUNK) {
                    tx_row(&t, y - pl.dy - top, px, px + x0, px + x1, cx, buf);
                    for (j = 0; j < TX_CHUNK / 16 && cx + 16 * j <= r[2]; j++) {
                        uint32_t pat = (uint32_t)buf[1 + 2 * j] << 8 | buf[2 + 2 * j];
                        LONG a = cx + 16 * j, b = a + 15;
                        int mode = pat_mode(rp, &pat);
                        if (a < r[0]) a = r[0];
                        if (b > r[2]) b = r[2];
                        if (a > b || (mode != GFX_JAM2 && !pat)) continue;
                        gfx_planar_span(&pl.bm, a, b, y, pat, rp->FgPen, rp->BgPen, rp->Mask, mode);
                    }
                }
        }
    }
    rp->cp_x = (WORD)(rp->cp_x + len);
    return 1;
}
