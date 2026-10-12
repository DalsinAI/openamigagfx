/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenGfx inside opengpu.library: graphics.library's drawing and text.
 *
 * This is opengfx.library 1.1 (amigachrome-guest libraries/opengfx, commit
 * 90aa66f), brought into opengpu.library on 8 October 2026 so one library
 * does all the drawing (FORK.md records what changed on the way).
 *
 * OpenGfx owns every graphics.library drawing and text patch: Text,
 * TextLength, TextExtent, TextFit, RectFill, BltBitMap, BltTemplate and
 * ScrollRaster, and from 0.8 BltPattern, SetRast, Draw, PolyDraw,
 * WritePixel, ReadPixel, BltBitMapRastPort, BltMaskBitMapRastPort, ClipBlit,
 * WriteChunkyPixels, WritePixelArray8, WritePixelLine8, ReadPixelLine8 and
 * ReadPixelArray8 (22 in all). OpenFont patches nothing: it supplies glyphs, shaping and
 * metrics. OpenRTG and other RTG systems register a provider for these calls
 * instead of installing competing vectors. A call no provider and no leaf
 * takes goes to graphics.library's own code.
 *
 * A look (OpenLook, opengpu.library 0.7) is asked first on RectFill and
 * Text, so the system's look is drawn here instead of by a second patch.
 *
 * Programs reach the same code two ways: through graphics.library, once
 * OGFX_InstallPatches has pointed its eight vectors here, or through
 * opengpu.library's own LVOs (include/opengpu/gfx.h).
 *
 * No writable globals: the state is in opengpu.library's base (ogfx.h).
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <libraries/configvars.h>
#include <libraries/expansionbase.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/expansion.h>

#include "ogfx.h"
#include "ogfx_leaves.h"

#define REG OGFX_REG

/* Zorro manufacturer of AmigaChrome's own boards (amigachrome-guest
 * common/protocol/dalsin.h). The old id, 2011, is shared with UAE and
 * home-made boards, so it is not taken as proof. */
#define OGFX_DALSIN_MANUFACTURER 0xDA15

/* graphics.library's vectors, in OGFX_P_ order. */
static const WORD lvo_of[OGFX_P_COUNT] = {
    -60,    /* Text */
    -54,    /* TextLength */
    -690,   /* TextExtent */
    -696,   /* TextFit */
    -306,   /* RectFill */
    -30,    /* BltBitMap */
    -36,    /* BltTemplate */
    -396,   /* ScrollRaster */
    /* 0.8: every other drawing call (gfx.h) */
    -312,   /* BltPattern */
    -234,   /* SetRast */
    -246,   /* Draw */
    -336,   /* PolyDraw */
    -324,   /* WritePixel */
    -318,   /* ReadPixel */
    -606,   /* BltBitMapRastPort */
    -636,   /* BltMaskBitMapRastPort */
    -552,   /* ClipBlit */
    -1056,  /* WriteChunkyPixels */
    -786,   /* WritePixelArray8 */
    -774,   /* WritePixelLine8 */
    -768,   /* ReadPixelLine8 */
    -780,   /* ReadPixelArray8 */
};

static const char gfx_name[] = "graphics.library";
static const char expansion_name[] = "expansion.library";

typedef void (*rectfill_fn)(REG(a1, struct RastPort *),
                            REG(d0, LONG), REG(d1, LONG),
                            REG(d2, LONG), REG(d3, LONG),
                            REG(a6, struct GfxBase *));
typedef LONG (*text_fn)(REG(a1, struct RastPort *), REG(a0, STRPTR),
                        REG(d0, ULONG), REG(a6, struct GfxBase *));
typedef LONG (*bltbitmap_fn)(REG(a0, struct BitMap *),
                             REG(d0, LONG), REG(d1, LONG),
                             REG(a1, struct BitMap *),
                             REG(d2, LONG), REG(d3, LONG),
                             REG(d4, LONG), REG(d5, LONG),
                             REG(d6, ULONG), REG(d7, ULONG),
                             REG(a2, PLANEPTR),
                             REG(a6, struct GfxBase *));
typedef void (*scroll_fn)(REG(a1, struct RastPort *),
                          REG(d0, LONG), REG(d1, LONG),
                          REG(d2, LONG), REG(d3, LONG),
                          REG(d4, LONG), REG(d5, LONG),
                          REG(a6, struct GfxBase *));
typedef WORD (*textlength_fn)(REG(a1, struct RastPort *), REG(a0, STRPTR),
                              REG(d0, ULONG), REG(a6, struct GfxBase *));
typedef void (*textextent_fn)(REG(a1, struct RastPort *), REG(a0, STRPTR),
                              REG(d0, ULONG), REG(a2, struct TextExtent *),
                              REG(a6, struct GfxBase *));
typedef ULONG (*textfit_fn)(REG(a1, struct RastPort *), REG(a0, STRPTR),
                            REG(d0, ULONG), REG(a2, struct TextExtent *),
                            REG(a3, struct TextExtent *), REG(d1, LONG),
                            REG(d2, ULONG), REG(d3, ULONG),
                            REG(a6, struct GfxBase *));
typedef void (*blttemplate_fn)(REG(a0, PLANEPTR), REG(d0, LONG), REG(d1, LONG),
                               REG(a1, struct RastPort *), REG(d2, LONG), REG(d3, LONG),
                               REG(d4, LONG), REG(d5, LONG),
                               REG(a6, struct GfxBase *));

static inline struct ogfx_state *state_of(struct Library *base)
{
    return (struct ogfx_state *)((UBYTE *)base + ogpu_ogfx_at);
}

/* graphics.library's code for call p: the vector before the patches, or,
 * with no patches in, graphics.library's vector as it stands (so OpenGfx's
 * own LVOs never call themselves). */
static APTR original(struct ogfx_state *st, struct GfxBase *gfx, int p)
{
    APTR v = st->old[p];
    if (!v) {
        v = (APTR)((UBYTE *)gfx + lvo_of[p]);
        /* Keep the address a value: os32's GCC 6.5 folds a call through it
         * into JSR d16(A6) with the offset's sign lost (JSR 30(A6) for
         * BltBitMap's -30). tests/ogfx/run.sh looks for that. */
        __asm__ ("" : "+r" (v));
    }
    return v;
}

/* graphics.library for the LVO path, opened the first time it is needed (the
 * library may start from ROM before graphics.library is up). */
static struct GfxBase *gfx_of(struct ogfx_state *st)
{
    struct ExecBase *SysBase = st->sys;
    if (!st->gfx) {
        struct GfxBase *g = (struct GfxBase *)OpenLibrary((CONST_STRPTR)gfx_name, 39);
        Forbid();
        if (!st->gfx) { st->gfx = g; g = NULL; }
        Permit();
        if (g) CloseLibrary((struct Library *)g);
    }
    return st->gfx;
}

/* ---- the native planar paths ------------------------------------------------ */

/* Any plane of bm in Chip RAM (0 and -1 are no memory). */
static int chip_planes(struct ogfx_state *st, struct BitMap *bm)
{
    struct ExecBase *SysBase = st->sys;
    UWORD p;
    for (p = 0; p < bm->Depth && p < 8; p++) {
        PLANEPTR plane = bm->Planes[p];
        if (plane && plane != (PLANEPTR)-1 && (TypeOfMem(plane) & MEMF_CHIP))
            return 1;
    }
    return 0;
}

/* Whether a leaf may draw on these bitmaps' planes now. Chip RAM: on a real
 * Amiga the blitter is the better answer there, so graphics.library keeps
 * it (OPENGFX_DESIGN.md section 5); on AmigaChrome the leaf runs as host
 * code and takes it. Either way the CPU must not touch planes a queued blit
 * may still be drawing, so it waits for the blitter first. */
static int cpu_may_draw(struct ogfx_state *st, struct GfxBase *GfxBase,
                        struct BitMap *a, struct BitMap *b)
{
    if (!chip_planes(st, a) && !(b && chip_planes(st, b)))
        return 1;
    if (!st->amigachrome)
        return 0;
    WaitBlit();
    return 1;
}

static LONG ranges_overlap(ULONG a, ULONG an, ULONG b, ULONG bn)
{
    uint64_t ae = (uint64_t)a + an;
    uint64_t be = (uint64_t)b + bn;
    return ae > b && be > a;
}

static LONG planar_bitmaps_alias(const struct BitMap *src,
                                 const struct BitMap *dst,
                                 ULONG depth)
{
    ULONG p, q;
    if (src == dst)
        return 0;

    for (p = 0; p < depth; ++p) {
        PLANEPTR dp = dst->Planes[p];
        ULONG dn;
        if (!dp || dp == (PLANEPTR)-1)
            continue;
        dn = (ULONG)dst->BytesPerRow * dst->Rows;
        for (q = 0; q < depth; ++q) {
            PLANEPTR sp = src->Planes[q];
            ULONG sn;
            if (!sp || sp == (PLANEPTR)-1)
                continue;
            sn = (ULONG)src->BytesPerRow * src->Rows;
            if (ranges_overlap((ULONG)sp, sn, (ULONG)dp, dn))
                return 1;
        }
    }
    return 0;
}

static LONG try_planar_bltbitmap(struct ogfx_state *st, struct GfxBase *GfxBase,
                                 struct BitMap *src,
                                 LONG sx, LONG sy,
                                 struct BitMap *dst,
                                 LONG dx, LONG dy,
                                 LONG width, LONG height,
                                 ULONG minterm, ULONG mask,
                                 LONG *result)
{
    struct ogfx_planar_blit r;
    ULONG sw, dw, sflags, dflags, depth, ddepth;

    if (!src || !dst || !result || dx < 0 || dy < 0)
        return 0;
    /* Chip RAM stays with the blitter, on AmigaChrome too: there the blitter
     * copies 640 x 480 x 8 planes in 0.2 ms and the planar-blit leaf (one bit
     * at a time, as host code) takes 12 ms; a 64 x 64 copy 0.05 against 0.9
     * (OpenGfxCheck, 8 October 2026). The leaf takes Fast RAM bitmaps, which
     * the blitter can't reach. This comes first: Picasso96's GetBitMapAttr
     * waits for a blit in flight, which cost 1.4 ms a full-screen copy. */
    if (chip_planes(st, src) || chip_planes(st, dst))
        return 0;

    sflags = GetBitMapAttr(src, BMA_FLAGS);
    dflags = GetBitMapAttr(dst, BMA_FLAGS);
    if (!(sflags & BMF_STANDARD) || !(dflags & BMF_STANDARD))
        return 0;

    depth = GetBitMapAttr(src, BMA_DEPTH);
    ddepth = GetBitMapAttr(dst, BMA_DEPTH);
    if (ddepth < depth) depth = ddepth;
    if (!depth || depth > 8)
        return 0;

    sw = GetBitMapAttr(src, BMA_WIDTH);
    dw = GetBitMapAttr(dst, BMA_WIDTH);
    if (!sw || !dw)
        return 0;

    if (sx < 0) {
        dx -= sx;
        width += sx;
        sx = 0;
    }
    if (sy < 0) {
        dy -= sy;
        height += sy;
        sy = 0;
    }

    if (sy + height > src->Rows) height = (LONG)src->Rows - sy;
    if (dy + height > dst->Rows) height = (LONG)dst->Rows - dy;
    if (sx + width > (LONG)sw) width = (LONG)sw - sx;
    if (dx + width > (LONG)dw) width = (LONG)dw - dx;

    /* Nothing to draw: graphics.library's own answer (cheap). */
    if (width <= 0 || height <= 0 || !(mask & ((1u << depth) - 1)))
        return 0;

    /* Two different BitMap structs can legally point into unusual shared
     * storage. Leave those to graphics.library; the leaf's overlap ordering
     * is defined only for one BitMap object. */
    if (planar_bitmaps_alias(src, dst, depth))
        return 0;

    r.src_planes = (ULONG)(APTR)&src->Planes[0];
    r.src_bytes_per_row = src->BytesPerRow;
    r.dst_planes = (ULONG)(APTR)&dst->Planes[0];
    r.dst_bytes_per_row = dst->BytesPerRow;
    r.depth = depth;
    r.sx = sx; r.sy = sy; r.dx = dx; r.dy = dy;
    r.width = width; r.height = height;
    r.minterm = minterm & 0xffu;
    r.mask = mask & 0xffu;
    r.same_bitmap = src == dst;

    /* The planes drawn, as graphics.library's own BltBitMap counts them
     * (OpenGfxCheck, 8 October 2026; Picasso96's BltBitMap answers with
     * every plane the two share instead). */
    *result = (LONG)ogfx_planar_blit(&r);
    return 1;
}

static void planar_fill_box(struct RastPort *rp,
                            LONG x0, LONG y0, LONG x1, LONG y1,
                            ULONG pen)
{
    struct ogfx_planar_rect r;
    struct BitMap *bm = rp->BitMap;

    if (x0 > x1 || y0 > y1)
        return;

    r.planes = (ULONG)(APTR)&bm->Planes[0];
    r.bytes_per_row = bm->BytesPerRow;
    r.depth = bm->Depth;
    r.x0 = x0; r.y0 = y0; r.x1 = x1; r.y1 = y1;
    r.pen = pen;
    r.mask = rp->Mask;
    r.mode = 0;
    (void)ogfx_planar_rect(&r);
}

static LONG try_planar_scroll(struct ogfx_state *st, struct GfxBase *GfxBase,
                              struct RastPort *rp,
                              LONG dx, LONG dy,
                              LONG x0, LONG y0, LONG x1, LONG y1)
{
    struct BitMap *bm;
    struct ogfx_planar_blit r;
    ULONG flags, bw;
    LONG w, h, mw, mh;

    if (!rp || !(bm = rp->BitMap) || rp->Layer)
        return 0;

    flags = GetBitMapAttr(bm, BMA_FLAGS);
    if (!(flags & BMF_STANDARD) || !bm->Depth || bm->Depth > 8)
        return 0;

    bw = GetBitMapAttr(bm, BMA_WIDTH);
    if (!bw || !bm->Rows)
        return 0;
    if (!cpu_may_draw(st, GfxBase, bm, NULL))
        return 0;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= (LONG)bw) x1 = (LONG)bw - 1;
    if (y1 >= (LONG)bm->Rows) y1 = (LONG)bm->Rows - 1;
    if (x0 > x1 || y0 > y1)
        return 1;

    w = x1 - x0 + 1;
    h = y1 - y0 + 1;
    mw = w - (dx < 0 ? -dx : dx);
    mh = h - (dy < 0 ? -dy : dy);
    /* A scroll as large as the area or larger: graphics.library leaves the
     * area as it is rather than clearing it (OpenGfxCheck, 8 October 2026),
     * so it takes these. */
    if (mw <= 0 || mh <= 0)
        return 0;

    if (mw > 0 && mh > 0 && rp->Mask) {
        r.src_planes = (ULONG)(APTR)&bm->Planes[0];
        r.src_bytes_per_row = bm->BytesPerRow;
        r.dst_planes = (ULONG)(APTR)&bm->Planes[0];
        r.dst_bytes_per_row = bm->BytesPerRow;
        r.depth = bm->Depth;
        r.sx = x0 + (dx > 0 ? dx : 0);
        r.sy = y0 + (dy > 0 ? dy : 0);
        r.dx = x0 + (dx < 0 ? -dx : 0);
        r.dy = y0 + (dy < 0 ? -dy : 0);
        r.width = mw;
        r.height = mh;
        r.minterm = 0xc0;
        r.mask = rp->Mask;
        r.same_bitmap = 1;
        (void)ogfx_planar_blit(&r);
    }

    /* ScrollRaster clears the newly exposed area with the background pen. */
    if (dx > 0)
        planar_fill_box(rp, x1 - dx + 1 > x0 ? x1 - dx + 1 : x0,
                        y0, x1, y1, rp->BgPen);
    else if (dx < 0)
        planar_fill_box(rp, x0, y0,
                        x0 - dx - 1 < x1 ? x0 - dx - 1 : x1,
                        y1, rp->BgPen);

    if (dy > 0)
        planar_fill_box(rp, x0,
                        y1 - dy + 1 > y0 ? y1 - dy + 1 : y0,
                        x1, y1, rp->BgPen);
    else if (dy < 0)
        planar_fill_box(rp, x0, y0, x1,
                        y0 - dy - 1 < y1 ? y0 - dy - 1 : y1,
                        rp->BgPen);

    return 1;
}

static LONG try_planar_rect(struct ogfx_state *st, struct GfxBase *GfxBase,
                            struct RastPort *rp,
                            LONG x0, LONG y0, LONG x1, LONG y1)
{
    struct BitMap *bm;
    struct ogfx_planar_rect request;
    ULONG flags, width, height;
    UBYTE drawmode;

    if (!rp || !(bm = rp->BitMap) || rp->Layer || rp->AreaPtrn)
        return 0;

    drawmode = rp->DrawMode;
    if (drawmode != JAM1 && drawmode != JAM2 && drawmode != COMPLEMENT)
        return 0;

    flags = GetBitMapAttr(bm, BMA_FLAGS);
    if (!(flags & BMF_STANDARD) || !bm->Depth || bm->Depth > 8)
        return 0;

    if (!cpu_may_draw(st, GfxBase, bm, NULL))
        return 0;

    width = GetBitMapAttr(bm, BMA_WIDTH);
    height = GetBitMapAttr(bm, BMA_HEIGHT);
    if (!width || !height || x1 < x0 || y1 < y0)
        return 0;

    if (x1 < 0 || y1 < 0 || x0 >= (LONG)width || y0 >= (LONG)height)
        return 1;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= (LONG)width) x1 = (LONG)width - 1;
    if (y1 >= (LONG)height) y1 = (LONG)height - 1;

    request.planes = (ULONG)(APTR)&bm->Planes[0];
    request.bytes_per_row = bm->BytesPerRow;
    request.depth = bm->Depth;
    request.x0 = x0;
    request.y0 = y0;
    request.x1 = x1;
    request.y1 = y1;
    request.pen = rp->FgPen;
    request.mask = rp->Mask;
    request.mode = drawmode == COMPLEMENT ? 1 : 0;

    (void)ogfx_planar_rect(&request);
    return 1;
}

/* ---- the look (gfx.h): asked first on RectFill and Text ---------------------
 * look_busy counts the calls inside the look, so its owner knows when its
 * code is free to go after OGFX_UnregisterLook. A one-instruction add and
 * subtract on memory, which a task switch can't split. The count goes up
 * before the look is read: a caller that saw the look in is counted. */
static inline void look_enter(struct ogfx_state *st)
{
    __asm__ volatile ("addq.w #1,%0" : "+m" (st->look_busy) : : "cc");
}

static inline void look_leave(struct ogfx_state *st)
{
    __asm__ volatile ("subq.w #1,%0" : "+m" (st->look_busy) : : "cc");
}

static LONG look_rectfill(struct ogfx_state *st, struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1)
{
    LONG done = 0;
    LONG (*fn)(APTR, struct OGFXRectFillRequest *);
    look_enter(st);
    if (st->have_look && (fn = st->look.rectfill) != NULL) {
        struct OGFXRectFillRequest r;
        r.rp = rp; r.x0 = x0; r.y0 = y0; r.x1 = x1; r.y1 = y1;
        done = fn(st->look.userdata, &r);
    }
    look_leave(st);
    return done;
}

static LONG look_text(struct ogfx_state *st, struct RastPort *rp, STRPTR text, ULONG length, LONG *result)
{
    LONG done = 0;
    LONG (*fn)(APTR, struct OGFXTextRequest *);
    look_enter(st);
    if (st->have_look && (fn = st->look.text) != NULL) {
        struct OGFXTextRequest r;
        r.rp = rp; r.text = text; r.length = length; r.result = 0;
        if ((done = fn(st->look.userdata, &r)) != 0)
            *result = r.result;
    }
    look_leave(st);
    return done;
}

/* ---- the eight calls: provider, then the native path, then graphics.library ----
 * Each reads its 16-bit arguments as graphics.library does, from the
 * registers' low words, before a provider or leaf sees them. Each is built
 * into its patch and its LVO both, so a patched call costs no extra call. */
#define OGFX_CALL static inline __attribute__((always_inline))

OGFX_CALL void do_rectfill(struct ogfx_state *st, struct GfxBase *gfx, struct RastPort *rp,
                        LONG x0, LONG y0, LONG x1, LONG y1)
{
    x0 = (WORD)x0; y0 = (WORD)y0; x1 = (WORD)x1; y1 = (WORD)y1;

    /* The look first: a window frame it draws is drawn whatever the bitmap. */
    if (st->have_look && look_rectfill(st, rp, x0, y0, x1, y1))
        return;

    /* Providers own foreign bitmap semantics (for example OpenRTG) and
     * remain active when native OpenGfx acceleration is disabled. */
    if (st->have_provider && st->provider.v1.rectfill) {
        struct OGFXRectFillRequest r;
        r.rp = rp; r.x0 = x0; r.y0 = y0; r.x1 = x1; r.y1 = y1;
        if (st->provider.v1.rectfill(st->provider.v1.userdata, &r))
            return;
    }

    if (st->enabled && try_planar_rect(st, gfx, rp, x0, y0, x1, y1))
        return;

    ((rectfill_fn)original(st, gfx, OGFX_P_RECTFILL))(rp, x0, y0, x1, y1, gfx);
}

OGFX_CALL LONG do_text(struct ogfx_state *st, struct GfxBase *gfx, struct RastPort *rp,
                    STRPTR text, ULONG length)
{
    length = (UWORD)length;

    if (st->have_look) {
        LONG result;
        if (look_text(st, rp, text, length, &result))
            return result;
    }

    if (st->have_provider && st->provider.v1.text) {
        struct OGFXTextRequest r;
        r.rp = rp; r.text = text; r.length = length; r.result = 0;
        if (st->provider.v1.text(st->provider.v1.userdata, &r))
            return r.result;
    }

    return ((text_fn)original(st, gfx, OGFX_P_TEXT))(rp, text, length, gfx);
}

OGFX_CALL LONG do_bltbitmap(struct ogfx_state *st, struct GfxBase *gfx,
                         struct BitMap *src, LONG sx, LONG sy,
                         struct BitMap *dst, LONG dx, LONG dy,
                         LONG width, LONG height, ULONG minterm, ULONG mask,
                         PLANEPTR temp)
{
    sx = (WORD)sx; sy = (WORD)sy; dx = (WORD)dx; dy = (WORD)dy;
    width = (WORD)width; height = (WORD)height; minterm = (UBYTE)minterm; mask = (UBYTE)mask;

    if (st->have_provider && st->provider.v1.bltbitmap) {
        struct OGFXBltBitMapRequest r;
        r.src = src; r.sx = sx; r.sy = sy;
        r.dst = dst; r.dx = dx; r.dy = dy;
        r.width = width; r.height = height;
        r.minterm = minterm; r.mask = mask; r.temp = temp; r.result = 0;
        if (st->provider.v1.bltbitmap(st->provider.v1.userdata, &r))
            return r.result;
    }

    if (st->enabled) {
        LONG result;
        if (try_planar_bltbitmap(st, gfx, src, sx, sy, dst, dx, dy,
                                 width, height, minterm, mask, &result))
            return result;
    }

    return ((bltbitmap_fn)original(st, gfx, OGFX_P_BLTBITMAP))(src, sx, sy, dst, dx, dy, width, height,
                                                               minterm, mask, temp, gfx);
}

OGFX_CALL void do_scroll(struct ogfx_state *st, struct GfxBase *gfx, struct RastPort *rp,
                      LONG dx, LONG dy, LONG x0, LONG y0, LONG x1, LONG y1)
{
    dx = (WORD)dx; dy = (WORD)dy; x0 = (WORD)x0; y0 = (WORD)y0; x1 = (WORD)x1; y1 = (WORD)y1;

    if (st->have_provider && st->provider.v1.scrollraster) {
        struct OGFXScrollRasterRequest r;
        r.rp = rp; r.dx = dx; r.dy = dy;
        r.x0 = x0; r.y0 = y0; r.x1 = x1; r.y1 = y1;
        if (st->provider.v1.scrollraster(st->provider.v1.userdata, &r))
            return;
    }

    if (st->enabled && try_planar_scroll(st, gfx, rp, dx, dy, x0, y0, x1, y1))
        return;

    ((scroll_fn)original(st, gfx, OGFX_P_SCROLLRASTER))(rp, dx, dy, x0, y0, x1, y1, gfx);
}

/* The text measurement calls and BltTemplate: the provider (OpenRTG, and
 * through OpenGfx OpenFont's metrics when they come), else graphics.library's
 * own code, so a program measures text as it is drawn. */
OGFX_CALL WORD do_textlength(struct ogfx_state *st, struct GfxBase *gfx, struct RastPort *rp,
                          STRPTR text, ULONG length)
{
    length = (UWORD)length;

    if (st->have_provider && st->provider.v1.textlength) {
        struct OGFXTextLengthRequest r;
        r.rp = rp; r.text = text; r.length = length; r.result = 0;
        if (st->provider.v1.textlength(st->provider.v1.userdata, &r))
            return (WORD)r.result;
    }

    return ((textlength_fn)original(st, gfx, OGFX_P_TEXTLENGTH))(rp, text, length, gfx);
}

OGFX_CALL void do_textextent(struct ogfx_state *st, struct GfxBase *gfx, struct RastPort *rp,
                          STRPTR text, ULONG length, struct TextExtent *extent)
{
    length = (UWORD)length;

    if (st->have_provider && st->provider.v1.textextent) {
        struct OGFXTextExtentRequest r;
        r.rp = rp; r.text = text; r.length = length; r.extent = extent;
        if (st->provider.v1.textextent(st->provider.v1.userdata, &r))
            return;
    }

    ((textextent_fn)original(st, gfx, OGFX_P_TEXTEXTENT))(rp, text, length, extent, gfx);
}

OGFX_CALL ULONG do_textfit(struct ogfx_state *st, struct GfxBase *gfx, struct RastPort *rp,
                        STRPTR text, ULONG length, struct TextExtent *extent,
                        struct TextExtent *constraining, LONG direction,
                        ULONG bit_width, ULONG bit_height)
{
    length = (UWORD)length; direction = (WORD)direction;
    bit_width = (UWORD)bit_width; bit_height = (UWORD)bit_height;

    if (st->have_provider && st->provider.v1.textfit) {
        struct OGFXTextFitRequest r;
        r.rp = rp; r.text = text; r.length = length; r.extent = extent;
        r.constraining = constraining; r.direction = direction;
        r.bit_width = bit_width; r.bit_height = bit_height; r.result = 0;
        if (st->provider.v1.textfit(st->provider.v1.userdata, &r))
            return r.result;
    }

    return ((textfit_fn)original(st, gfx, OGFX_P_TEXTFIT))(rp, text, length, extent, constraining,
                                                           direction, bit_width, bit_height, gfx);
}

OGFX_CALL void do_blttemplate(struct ogfx_state *st, struct GfxBase *gfx, PLANEPTR source,
                           LONG sx, LONG modulo, struct RastPort *rp,
                           LONG dx, LONG dy, LONG width, LONG height)
{
    sx = (WORD)sx; modulo = (WORD)modulo; dx = (WORD)dx; dy = (WORD)dy;
    width = (WORD)width; height = (WORD)height;

    if (st->have_provider && st->provider.v1.blttemplate) {
        struct OGFXBltTemplateRequest r;
        r.source = source; r.sx = sx; r.source_modulo = modulo;
        r.rp = rp; r.dx = dx; r.dy = dy; r.width = width; r.height = height;
        if (st->provider.v1.blttemplate(st->provider.v1.userdata, &r))
            return;
    }

    ((blttemplate_fn)original(st, gfx, OGFX_P_BLTTEMPLATE))(source, sx, modulo, rp, dx, dy, width, height, gfx);
}

/* ---- 0.8: the fourteen other drawing calls -----------------------------------
 * The provider (OpenRTG) first, with the arguments as graphics.library reads
 * them, then graphics.library's own code with the registers as they came.
 * OpenGfx has no native path for these yet. */
typedef void (*bltpattern_fn)(REG(a1, struct RastPort *), REG(a0, PLANEPTR), REG(d0, LONG), REG(d1, LONG),
                              REG(d2, LONG), REG(d3, LONG), REG(d4, LONG), REG(a6, struct GfxBase *));
typedef void (*setrast_fn)(REG(a1, struct RastPort *), REG(d0, ULONG), REG(a6, struct GfxBase *));
typedef void (*draw_fn)(REG(a1, struct RastPort *), REG(d0, LONG), REG(d1, LONG), REG(a6, struct GfxBase *));
typedef void (*polydraw_fn)(REG(a1, struct RastPort *), REG(d0, LONG), REG(a0, WORD *), REG(a6, struct GfxBase *));
typedef LONG (*pixel_fn)(REG(a1, struct RastPort *), REG(d0, LONG), REG(d1, LONG), REG(a6, struct GfxBase *));
typedef void (*bltbmrp_fn)(REG(a0, struct BitMap *), REG(d0, LONG), REG(d1, LONG), REG(a1, struct RastPort *),
                           REG(d2, LONG), REG(d3, LONG), REG(d4, LONG), REG(d5, LONG), REG(d6, ULONG),
                           REG(a6, struct GfxBase *));
typedef void (*bltmaskbmrp_fn)(REG(a0, struct BitMap *), REG(d0, LONG), REG(d1, LONG), REG(a1, struct RastPort *),
                               REG(d2, LONG), REG(d3, LONG), REG(d4, LONG), REG(d5, LONG), REG(d6, ULONG),
                               REG(a2, PLANEPTR), REG(a6, struct GfxBase *));
typedef void (*clipblit_fn)(REG(a0, struct RastPort *), REG(d0, LONG), REG(d1, LONG), REG(a1, struct RastPort *),
                            REG(d2, LONG), REG(d3, LONG), REG(d4, LONG), REG(d5, LONG), REG(d6, ULONG),
                            REG(a6, struct GfxBase *));
typedef void (*wcp_fn)(REG(a0, struct RastPort *), REG(d0, ULONG), REG(d1, ULONG), REG(d2, ULONG), REG(d3, ULONG),
                       REG(a2, UBYTE *), REG(d4, LONG), REG(a6, struct GfxBase *));
typedef LONG (*array8_fn)(REG(a0, struct RastPort *), REG(d0, ULONG), REG(d1, ULONG), REG(d2, ULONG), REG(d3, ULONG),
                          REG(a2, UBYTE *), REG(a1, struct RastPort *), REG(a6, struct GfxBase *));
typedef LONG (*line8_fn)(REG(a0, struct RastPort *), REG(d0, ULONG), REG(d1, ULONG), REG(d2, ULONG),
                         REG(a2, UBYTE *), REG(a1, struct RastPort *), REG(a6, struct GfxBase *));

#define PROVIDES(call) (st->have_provider && st->provider.call)

OGFX_CALL void do_bltpattern(struct ogfx_state *st, struct GfxBase *gfx, struct RastPort *rp, PLANEPTR mask,
                             LONG x0, LONG y0, LONG x1, LONG y1, LONG bpr)
{
    if (PROVIDES(bltpattern)) {
        struct OGFXBltPatternRequest r;
        r.rp = rp; r.mask = mask; r.x0 = (WORD)x0; r.y0 = (WORD)y0; r.x1 = (WORD)x1; r.y1 = (WORD)y1;
        r.mask_bpr = (WORD)bpr;
        if (st->provider.bltpattern(st->provider.v1.userdata, &r)) return;
    }
    ((bltpattern_fn)original(st, gfx, OGFX_P_BLTPATTERN))(rp, mask, x0, y0, x1, y1, bpr, gfx);
}

OGFX_CALL void do_setrast(struct ogfx_state *st, struct GfxBase *gfx, struct RastPort *rp, ULONG pen)
{
    if (PROVIDES(setrast)) {
        struct OGFXSetRastRequest r;
        r.rp = rp; r.pen = (UBYTE)pen;
        if (st->provider.setrast(st->provider.v1.userdata, &r)) return;
    }
    ((setrast_fn)original(st, gfx, OGFX_P_SETRAST))(rp, pen, gfx);
}

OGFX_CALL void do_draw(struct ogfx_state *st, struct GfxBase *gfx, struct RastPort *rp, LONG x, LONG y)
{
    if (PROVIDES(draw)) {
        struct OGFXDrawRequest r;
        r.rp = rp; r.x = (WORD)x; r.y = (WORD)y;
        if (st->provider.draw(st->provider.v1.userdata, &r)) return;
    }
    ((draw_fn)original(st, gfx, OGFX_P_DRAW))(rp, x, y, gfx);
}

OGFX_CALL void do_polydraw(struct ogfx_state *st, struct GfxBase *gfx, struct RastPort *rp, LONG n, WORD *xy)
{
    if (PROVIDES(polydraw)) {
        struct OGFXPolyDrawRequest r;
        r.rp = rp; r.count = (WORD)n; r.array = xy;
        if (st->provider.polydraw(st->provider.v1.userdata, &r)) return;
    }
    ((polydraw_fn)original(st, gfx, OGFX_P_POLYDRAW))(rp, n, xy, gfx);
}

OGFX_CALL LONG do_pixel(struct ogfx_state *st, struct GfxBase *gfx, int p, struct RastPort *rp, LONG x, LONG y)
{
    LONG (*fn)(APTR, struct OGFXPixelRequest *) = p == OGFX_P_WRITEPIXEL ? st->provider.writepixel : st->provider.readpixel;
    if (st->have_provider && fn) {
        struct OGFXPixelRequest r;
        r.rp = rp; r.x = (WORD)x; r.y = (WORD)y; r.result = 0;
        if (fn(st->provider.v1.userdata, &r)) return r.result;
    }
    return ((pixel_fn)original(st, gfx, p))(rp, x, y, gfx);
}

OGFX_CALL void do_bltbmrp(struct ogfx_state *st, struct GfxBase *gfx, int p, struct BitMap *src, LONG sx, LONG sy,
                          struct RastPort *rp, LONG dx, LONG dy, LONG w, LONG h, ULONG minterm, PLANEPTR mask)
{
    LONG (*fn)(APTR, struct OGFXBltBitMapRastPortRequest *) =
        p == OGFX_P_BLTBMRP ? st->provider.bltbitmaprastport : st->provider.bltmaskbitmaprastport;
    if (st->have_provider && fn) {
        struct OGFXBltBitMapRastPortRequest r;
        r.src = src; r.sx = (WORD)sx; r.sy = (WORD)sy; r.rp = rp; r.dx = (WORD)dx; r.dy = (WORD)dy;
        r.width = (WORD)w; r.height = (WORD)h; r.minterm = (UBYTE)minterm; r.mask = mask;
        if (fn(st->provider.v1.userdata, &r)) return;
    }
    if (p == OGFX_P_BLTBMRP)
        ((bltbmrp_fn)original(st, gfx, p))(src, sx, sy, rp, dx, dy, w, h, minterm, gfx);
    else
        ((bltmaskbmrp_fn)original(st, gfx, p))(src, sx, sy, rp, dx, dy, w, h, minterm, mask, gfx);
}

OGFX_CALL void do_clipblit(struct ogfx_state *st, struct GfxBase *gfx, struct RastPort *srp, LONG sx, LONG sy,
                           struct RastPort *rp, LONG dx, LONG dy, LONG w, LONG h, ULONG minterm)
{
    if (PROVIDES(clipblit)) {
        struct OGFXClipBlitRequest r;
        r.src_rp = srp; r.sx = (WORD)sx; r.sy = (WORD)sy; r.rp = rp; r.dx = (WORD)dx; r.dy = (WORD)dy;
        r.width = (WORD)w; r.height = (WORD)h; r.minterm = (UBYTE)minterm;
        if (st->provider.clipblit(st->provider.v1.userdata, &r)) return;
    }
    ((clipblit_fn)original(st, gfx, OGFX_P_CLIPBLIT))(srp, sx, sy, rp, dx, dy, w, h, minterm, gfx);
}

/* The pixel-array calls: p says which (OGFX_P_WCP to OGFX_P_RPA8). */
OGFX_CALL LONG do_array(struct ogfx_state *st, struct GfxBase *gfx, int p, struct RastPort *rp,
                        ULONG x0, ULONG y0, ULONG x1, ULONG y1, UBYTE *array, LONG bpr, struct RastPort *tmp)
{
    LONG (*fn)(APTR, struct OGFXPixelArrayRequest *) =
        p == OGFX_P_WCP ? st->provider.writechunkypixels : p == OGFX_P_WPA8 ? st->provider.writepixelarray8 :
        p == OGFX_P_WPL8 ? st->provider.writepixelline8 : p == OGFX_P_RPL8 ? st->provider.readpixelline8 :
        st->provider.readpixelarray8;
    int line = p == OGFX_P_WPL8 || p == OGFX_P_RPL8;
    if (st->have_provider && fn) {
        struct OGFXPixelArrayRequest r;
        r.rp = rp; r.array = array; r.bytes_per_row = p == OGFX_P_WCP ? bpr : 0; r.temp_rp = tmp; r.result = 0;
        if (line) {                    /* x1 holds the width (D2) */
            r.width = (UWORD)x1;
            r.x0 = (UWORD)x0; r.y0 = (UWORD)y0; r.x1 = r.x0 + (LONG)r.width - 1; r.y1 = r.y0;
        } else {
            r.width = 0;
            r.x0 = (UWORD)x0; r.y0 = (UWORD)y0; r.x1 = (UWORD)x1; r.y1 = (UWORD)y1;
        }
        if (fn(st->provider.v1.userdata, &r)) return r.result;
    }
    if (p == OGFX_P_WCP) {
        ((wcp_fn)original(st, gfx, p))(rp, x0, y0, x1, y1, array, bpr, gfx);
        return 0;
    }
    if (line)
        return ((line8_fn)original(st, gfx, p))(rp, x0, y0, x1, array, tmp, gfx);
    return ((array8_fn)original(st, gfx, p))(rp, x0, y0, x1, y1, array, tmp, gfx);
}

/* ---- the patches: thin entries, called through tramp[] with the state in A4 ---- */

static void rectfill_patch(REG(a1, struct RastPort *rp), REG(d0, LONG x0), REG(d1, LONG y0),
                           REG(d2, LONG x1), REG(d3, LONG y1), REG(a6, struct GfxBase *gfx),
                           REG(a4, struct ogfx_state *st))
{
    do_rectfill(st, gfx, rp, x0, y0, x1, y1);
}

static LONG text_patch(REG(a1, struct RastPort *rp), REG(a0, STRPTR text), REG(d0, ULONG length),
                       REG(a6, struct GfxBase *gfx), REG(a4, struct ogfx_state *st))
{
    return do_text(st, gfx, rp, text, length);
}

static LONG bltbitmap_patch(REG(a0, struct BitMap *src), REG(d0, LONG sx), REG(d1, LONG sy),
                            REG(a1, struct BitMap *dst), REG(d2, LONG dx), REG(d3, LONG dy),
                            REG(d4, LONG width), REG(d5, LONG height),
                            REG(d6, ULONG minterm), REG(d7, ULONG mask), REG(a2, PLANEPTR temp),
                            REG(a6, struct GfxBase *gfx), REG(a4, struct ogfx_state *st))
{
    return do_bltbitmap(st, gfx, src, sx, sy, dst, dx, dy, width, height, minterm, mask, temp);
}

static void scroll_patch(REG(a1, struct RastPort *rp), REG(d0, LONG dx), REG(d1, LONG dy),
                         REG(d2, LONG x0), REG(d3, LONG y0), REG(d4, LONG x1), REG(d5, LONG y1),
                         REG(a6, struct GfxBase *gfx), REG(a4, struct ogfx_state *st))
{
    do_scroll(st, gfx, rp, dx, dy, x0, y0, x1, y1);
}

static WORD textlength_patch(REG(a1, struct RastPort *rp), REG(a0, STRPTR text), REG(d0, ULONG length),
                             REG(a6, struct GfxBase *gfx), REG(a4, struct ogfx_state *st))
{
    return do_textlength(st, gfx, rp, text, length);
}

static void textextent_patch(REG(a1, struct RastPort *rp), REG(a0, STRPTR text), REG(d0, ULONG length),
                             REG(a2, struct TextExtent *extent), REG(a6, struct GfxBase *gfx),
                             REG(a4, struct ogfx_state *st))
{
    do_textextent(st, gfx, rp, text, length, extent);
}

static ULONG textfit_patch(REG(a1, struct RastPort *rp), REG(a0, STRPTR text), REG(d0, ULONG length),
                           REG(a2, struct TextExtent *extent), REG(a3, struct TextExtent *constraining),
                           REG(d1, LONG direction), REG(d2, ULONG bit_width), REG(d3, ULONG bit_height),
                           REG(a6, struct GfxBase *gfx), REG(a4, struct ogfx_state *st))
{
    return do_textfit(st, gfx, rp, text, length, extent, constraining, direction, bit_width, bit_height);
}

static void blttemplate_patch(REG(a0, PLANEPTR source), REG(d0, LONG sx), REG(d1, LONG modulo),
                              REG(a1, struct RastPort *rp), REG(d2, LONG dx), REG(d3, LONG dy),
                              REG(d4, LONG width), REG(d5, LONG height),
                              REG(a6, struct GfxBase *gfx), REG(a4, struct ogfx_state *st))
{
    do_blttemplate(st, gfx, source, sx, modulo, rp, dx, dy, width, height);
}

static void bltpattern_patch(REG(a1, struct RastPort *rp), REG(a0, PLANEPTR mask), REG(d0, LONG x0), REG(d1, LONG y0),
                             REG(d2, LONG x1), REG(d3, LONG y1), REG(d4, LONG bpr),
                             REG(a6, struct GfxBase *gfx), REG(a4, struct ogfx_state *st))
{
    do_bltpattern(st, gfx, rp, mask, x0, y0, x1, y1, bpr);
}

static void setrast_patch(REG(a1, struct RastPort *rp), REG(d0, ULONG pen),
                          REG(a6, struct GfxBase *gfx), REG(a4, struct ogfx_state *st))
{
    do_setrast(st, gfx, rp, pen);
}

static void draw_patch(REG(a1, struct RastPort *rp), REG(d0, LONG x), REG(d1, LONG y),
                       REG(a6, struct GfxBase *gfx), REG(a4, struct ogfx_state *st))
{
    do_draw(st, gfx, rp, x, y);
}

static void polydraw_patch(REG(a1, struct RastPort *rp), REG(d0, LONG n), REG(a0, WORD *xy),
                           REG(a6, struct GfxBase *gfx), REG(a4, struct ogfx_state *st))
{
    do_polydraw(st, gfx, rp, n, xy);
}

static LONG writepixel_patch(REG(a1, struct RastPort *rp), REG(d0, LONG x), REG(d1, LONG y),
                             REG(a6, struct GfxBase *gfx), REG(a4, struct ogfx_state *st))
{
    return do_pixel(st, gfx, OGFX_P_WRITEPIXEL, rp, x, y);
}

static LONG readpixel_patch(REG(a1, struct RastPort *rp), REG(d0, LONG x), REG(d1, LONG y),
                            REG(a6, struct GfxBase *gfx), REG(a4, struct ogfx_state *st))
{
    return do_pixel(st, gfx, OGFX_P_READPIXEL, rp, x, y);
}

static void bltbmrp_patch(REG(a0, struct BitMap *src), REG(d0, LONG sx), REG(d1, LONG sy), REG(a1, struct RastPort *rp),
                          REG(d2, LONG dx), REG(d3, LONG dy), REG(d4, LONG w), REG(d5, LONG h), REG(d6, ULONG m),
                          REG(a6, struct GfxBase *gfx), REG(a4, struct ogfx_state *st))
{
    do_bltbmrp(st, gfx, OGFX_P_BLTBMRP, src, sx, sy, rp, dx, dy, w, h, m, NULL);
}

static void bltmaskbmrp_patch(REG(a0, struct BitMap *src), REG(d0, LONG sx), REG(d1, LONG sy), REG(a1, struct RastPort *rp),
                              REG(d2, LONG dx), REG(d3, LONG dy), REG(d4, LONG w), REG(d5, LONG h), REG(d6, ULONG m),
                              REG(a2, PLANEPTR mask), REG(a6, struct GfxBase *gfx), REG(a4, struct ogfx_state *st))
{
    do_bltbmrp(st, gfx, OGFX_P_BLTMASKBMRP, src, sx, sy, rp, dx, dy, w, h, m, mask);
}

static void clipblit_patch(REG(a0, struct RastPort *srp), REG(d0, LONG sx), REG(d1, LONG sy), REG(a1, struct RastPort *rp),
                           REG(d2, LONG dx), REG(d3, LONG dy), REG(d4, LONG w), REG(d5, LONG h), REG(d6, ULONG m),
                           REG(a6, struct GfxBase *gfx), REG(a4, struct ogfx_state *st))
{
    do_clipblit(st, gfx, srp, sx, sy, rp, dx, dy, w, h, m);
}

static void wcp_patch(REG(a0, struct RastPort *rp), REG(d0, ULONG x0), REG(d1, ULONG y0), REG(d2, ULONG x1), REG(d3, ULONG y1),
                      REG(a2, UBYTE *a), REG(d4, LONG bpr), REG(a6, struct GfxBase *gfx), REG(a4, struct ogfx_state *st))
{
    (void)do_array(st, gfx, OGFX_P_WCP, rp, x0, y0, x1, y1, a, bpr, NULL);
}

static LONG wpa8_patch(REG(a0, struct RastPort *rp), REG(d0, ULONG x0), REG(d1, ULONG y0), REG(d2, ULONG x1), REG(d3, ULONG y1),
                       REG(a2, UBYTE *a), REG(a1, struct RastPort *tmp), REG(a6, struct GfxBase *gfx), REG(a4, struct ogfx_state *st))
{
    return do_array(st, gfx, OGFX_P_WPA8, rp, x0, y0, x1, y1, a, 0, tmp);
}

static LONG rpa8_patch(REG(a0, struct RastPort *rp), REG(d0, ULONG x0), REG(d1, ULONG y0), REG(d2, ULONG x1), REG(d3, ULONG y1),
                       REG(a2, UBYTE *a), REG(a1, struct RastPort *tmp), REG(a6, struct GfxBase *gfx), REG(a4, struct ogfx_state *st))
{
    return do_array(st, gfx, OGFX_P_RPA8, rp, x0, y0, x1, y1, a, 0, tmp);
}

static LONG wpl8_patch(REG(a0, struct RastPort *rp), REG(d0, ULONG x0), REG(d1, ULONG y0), REG(d2, ULONG w),
                       REG(a2, UBYTE *a), REG(a1, struct RastPort *tmp), REG(a6, struct GfxBase *gfx), REG(a4, struct ogfx_state *st))
{
    return do_array(st, gfx, OGFX_P_WPL8, rp, x0, y0, w, 0, a, 0, tmp);
}

static LONG rpl8_patch(REG(a0, struct RastPort *rp), REG(d0, ULONG x0), REG(d1, ULONG y0), REG(d2, ULONG w),
                       REG(a2, UBYTE *a), REG(a1, struct RastPort *tmp), REG(a6, struct GfxBase *gfx), REG(a4, struct ogfx_state *st))
{
    return do_array(st, gfx, OGFX_P_RPL8, rp, x0, y0, w, 0, a, 0, tmp);
}

/* In OGFX_P_ order. */
static const APTR patch_entry[OGFX_P_COUNT] = {
    (APTR)text_patch, (APTR)textlength_patch, (APTR)textextent_patch, (APTR)textfit_patch,
    (APTR)rectfill_patch, (APTR)bltbitmap_patch, (APTR)blttemplate_patch, (APTR)scroll_patch,
    (APTR)bltpattern_patch, (APTR)setrast_patch, (APTR)draw_patch, (APTR)polydraw_patch,
    (APTR)writepixel_patch, (APTR)readpixel_patch, (APTR)bltbmrp_patch, (APTR)bltmaskbmrp_patch,
    (APTR)clipblit_patch, (APTR)wcp_patch, (APTR)wpa8_patch, (APTR)wpl8_patch, (APTR)rpl8_patch, (APTR)rpa8_patch,
};

/* ---- set-up ------------------------------------------------------------------- */

void ogfx_init(struct ogfx_state *st, struct ExecBase *sys)
{
    st->sys = sys;
    st->enabled = 1;
}

int ogfx_may_expunge(struct ogfx_state *st)
{
    return !st->patched;
}

void ogfx_expunge(struct ogfx_state *st)
{
    struct ExecBase *SysBase = st->sys;
    if (st->gfx) {
        CloseLibrary((struct Library *)st->gfx);
        st->gfx = NULL;
    }
}

/* ---- the LVOs ------------------------------------------------------------------ */

ULONG OGFX_Version(REG(a6, struct Library *base))
{
    (void)base;
    return ((ULONG)OGFX_INTERFACE_VERSION << 16) | OGFX_INTERFACE_REVISION;
}

LONG OGFX_InstallPatches(REG(a6, struct Library *base))
{
    struct ogfx_state *st = state_of(base);
    struct ExecBase *SysBase = st->sys;
    struct Library *gfx;
    int p;

    if (st->patched)
        return 1;
    if (!(gfx = (struct Library *)gfx_of(st)))
        return 0;

    /* AmigaChrome: boards with Dalsin's own id (0xDA15) are there and nowhere
     * else. The old id, 2011, is shared with UAE and home-made boards, so it
     * is not taken as proof: a runtime from before 1 Oct 2026 just keeps
     * Chip RAM on the blitter. */
    {
        struct Library *ExpansionBase = OpenLibrary((CONST_STRPTR)expansion_name, 37);
        if (ExpansionBase) {
            st->amigachrome = FindConfigDev(NULL, OGFX_DALSIN_MANUFACTURER, -1) != NULL;
            CloseLibrary(ExpansionBase);
        }
    }

    Forbid();
    if (!st->patched) {
        for (p = 0; p < OGFX_P_COUNT; p++)
            tramp_write(st->tramp[p], (ULONG)st, (ULONG)patch_entry[p]);
        CacheClearU();                 /* the entries are code now */
        for (p = 0; p < OGFX_P_COUNT; p++)
            st->old[p] = SetFunction(gfx, lvo_of[p], (APTR)st->tramp[p]);
        CacheClearU();
        st->patched = 1;
    }
    Permit();
    return 1;
}

LONG OGFX_SetEnabled(REG(d0, ULONG enabled), REG(a6, struct Library *base))
{
    struct ogfx_state *st = state_of(base);
    LONG old = st->enabled ? 1 : 0;
    st->enabled = enabled ? 1 : 0;
    return old;
}

ULONG OGFX_Status(REG(a6, struct Library *base))
{
    struct ogfx_state *st = state_of(base);
    ULONG status = 0;
    if (st->patched) status |= OGFX_STATUS_PATCHED;
    if (st->enabled) status |= OGFX_STATUS_ENABLED;
    if (st->have_provider) status |= OGFX_STATUS_PROVIDER;
    if (st->amigachrome) status |= OGFX_STATUS_AMIGACHROME;
    if (st->have_look) status |= OGFX_STATUS_LOOK;
    if (st->look_busy) status |= OGFX_STATUS_LOOK_BUSY;
    return status;
}

LONG OGFX_RegisterProvider(REG(a0, struct OGFXProviderV1 *provider), REG(a6, struct Library *base))
{
    struct ogfx_state *st = state_of(base);
    struct ExecBase *SysBase = st->sys;
    ULONG size, i;
    if (!provider || provider->abi != OGFX_PROVIDER_ABI_V1 ||
        provider->size < OGFX_PROVIDER_V1_0_SIZE || !provider->owner)
        return 0;
    /* a v1 provider's record, or a 1.4 one's (struct OGFXProviderAll) */
    size = provider->size < sizeof(struct OGFXProviderAll) ? provider->size : sizeof(struct OGFXProviderAll);

    Forbid();
    {   /* a 1.0 or v1 provider's record is shorter: what it has no room for stays NULL */
        UBYTE *to = (UBYTE *)&st->provider;
        const UBYTE *from = (const UBYTE *)provider;
        for (i = 0; i < sizeof(st->provider); ++i)
            to[i] = i < size ? from[i] : 0;
    }
    st->have_provider = 1;
    Permit();
    return 1;
}

LONG OGFX_UnregisterProvider(REG(a0, APTR owner), REG(a6, struct Library *base))
{
    struct ogfx_state *st = state_of(base);
    struct ExecBase *SysBase = st->sys;
    LONG removed = 0;
    Forbid();
    if (st->have_provider && owner && st->provider.v1.owner == owner) {
        UBYTE *p = (UBYTE *)&st->provider;
        ULONG i;
        for (i = 0; i < sizeof(st->provider); ++i) p[i] = 0;
        st->have_provider = 0;
        removed = 1;
    }
    Permit();
    return removed;
}

/* The look (0.7, gfx.h). Taking it out leaves the record as it was, so a
 * call already inside it still has its function and userdata; a new look
 * waits until those calls are done. */
LONG OGFX_RegisterLook(REG(a0, struct OGFXLookV1 *look), REG(a6, struct Library *base))
{
    struct ogfx_state *st = state_of(base);
    struct ExecBase *SysBase = st->sys;
    LONG ok = 0;
    if (!look || look->abi != OGFX_LOOK_ABI_V1 || look->size < sizeof(struct OGFXLookV1) || !look->owner)
        return 0;
    Forbid();
    if (!st->have_look && !st->look_busy) {
        st->look.size = sizeof(struct OGFXLookV1);
        st->look.abi = OGFX_LOOK_ABI_V1;
        st->look.owner = look->owner;
        st->look.userdata = look->userdata;
        st->look.rectfill = look->rectfill;
        st->look.text = look->text;
        st->have_look = 1;
        ok = 1;
    }
    Permit();
    return ok;
}

LONG OGFX_UnregisterLook(REG(a0, APTR owner), REG(a6, struct Library *base))
{
    struct ogfx_state *st = state_of(base);
    struct ExecBase *SysBase = st->sys;
    LONG removed = 0;
    Forbid();
    if (st->have_look && owner && st->look.owner == owner) {
        st->have_look = 0;
        removed = 1;
    }
    Permit();
    return removed;
}

/* The drawing LVOs: the same code as the patches, for programs that call
 * opengpu.library directly. Without graphics.library they do nothing. */

LONG OGFX_Text(REG(a1, struct RastPort *rp), REG(a0, STRPTR text), REG(d0, ULONG length),
               REG(a6, struct Library *base))
{
    struct ogfx_state *st = state_of(base);
    struct GfxBase *gfx = gfx_of(st);
    return gfx ? do_text(st, gfx, rp, text, length) : 0;
}

WORD OGFX_TextLength(REG(a1, struct RastPort *rp), REG(a0, STRPTR text), REG(d0, ULONG length),
                     REG(a6, struct Library *base))
{
    struct ogfx_state *st = state_of(base);
    struct GfxBase *gfx = gfx_of(st);
    return gfx ? do_textlength(st, gfx, rp, text, length) : 0;
}

void OGFX_TextExtent(REG(a1, struct RastPort *rp), REG(a0, STRPTR text), REG(d0, ULONG length),
                     REG(a2, struct TextExtent *extent), REG(a6, struct Library *base))
{
    struct ogfx_state *st = state_of(base);
    struct GfxBase *gfx = gfx_of(st);
    if (gfx) do_textextent(st, gfx, rp, text, length, extent);
}

ULONG OGFX_TextFit(REG(a1, struct RastPort *rp), REG(a0, STRPTR text), REG(d0, ULONG length),
                   REG(a2, struct TextExtent *extent), REG(a3, struct TextExtent *constraining),
                   REG(d1, LONG direction), REG(d2, ULONG bit_width), REG(d3, ULONG bit_height),
                   REG(a6, struct Library *base))
{
    struct ogfx_state *st = state_of(base);
    struct GfxBase *gfx = gfx_of(st);
    return gfx ? do_textfit(st, gfx, rp, text, length, extent, constraining, direction, bit_width, bit_height) : 0;
}

void OGFX_RectFill(REG(a1, struct RastPort *rp), REG(d0, LONG x0), REG(d1, LONG y0),
                   REG(d2, LONG x1), REG(d3, LONG y1), REG(a6, struct Library *base))
{
    struct ogfx_state *st = state_of(base);
    struct GfxBase *gfx = gfx_of(st);
    if (gfx) do_rectfill(st, gfx, rp, x0, y0, x1, y1);
}

LONG OGFX_BltBitMap(REG(a0, struct BitMap *src), REG(d0, LONG sx), REG(d1, LONG sy),
                    REG(a1, struct BitMap *dst), REG(d2, LONG dx), REG(d3, LONG dy),
                    REG(d4, LONG width), REG(d5, LONG height), REG(d6, ULONG minterm),
                    REG(d7, ULONG mask), REG(a2, PLANEPTR temp), REG(a6, struct Library *base))
{
    struct ogfx_state *st = state_of(base);
    struct GfxBase *gfx = gfx_of(st);
    return gfx ? do_bltbitmap(st, gfx, src, sx, sy, dst, dx, dy, width, height, minterm, mask, temp) : 0;
}

void OGFX_BltTemplate(REG(a0, PLANEPTR source), REG(d0, LONG sx), REG(d1, LONG modulo),
                      REG(a1, struct RastPort *rp), REG(d2, LONG dx), REG(d3, LONG dy),
                      REG(d4, LONG width), REG(d5, LONG height), REG(a6, struct Library *base))
{
    struct ogfx_state *st = state_of(base);
    struct GfxBase *gfx = gfx_of(st);
    if (gfx) do_blttemplate(st, gfx, source, sx, modulo, rp, dx, dy, width, height);
}

void OGFX_ScrollRaster(REG(a1, struct RastPort *rp), REG(d0, LONG dx), REG(d1, LONG dy),
                       REG(d2, LONG x0), REG(d3, LONG y0), REG(d4, LONG x1), REG(d5, LONG y1),
                       REG(a6, struct Library *base))
{
    struct ogfx_state *st = state_of(base);
    struct GfxBase *gfx = gfx_of(st);
    if (gfx) do_scroll(st, gfx, rp, dx, dy, x0, y0, x1, y1);
}
