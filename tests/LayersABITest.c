/* LayersABITest: OS 3.2's layers.library and OpenLayers side by side.
 * Copyright (c) 2026 Dalsin Limited. MIT.
 *
 * Runs one script of calls through a layers.library base and writes what
 * comes of each as lines of text. With openlayers.library in the directory
 * given with LIB= it runs the script through OS 3.2's library and then
 * through ours, each on a fresh bitmap with the same pixels, and prints
 * each line that differs, both answers. With PROBE it prints OS 3.2's
 * lines only, with every hook call: what the autodocs leave open, measured.
 *
 * The script, the same every run (ROUNDS= of them, 3 by default): a
 * Layer_Info on a 320 x 200 bitmap of 3 planes and up to 7 layers, simple,
 * smart and super bitmap refresh, some backdrop, with no backfill, the
 * default one or a hook that fills 8 x 8 cells of the layer's own
 * coordinates; then 30 random calls each: create, delete, UpfrontLayer,
 * BehindLayer, MoveLayer, SizeLayer, MoveSizeLayer, MoveLayerInFrontOf,
 * HideLayer and ShowLayer, ScrollLayer, a clip region drawn through,
 * BeginUpdate drawn through, WhichLayer, LayerOccluded, DoHookClipRects,
 * SortLayerCR and SwapBitsRastPortClipRect. Layers stay inside the
 * bitmap. Before each call graphics.library draws boxes into every shown
 * layer. After it, for every layer: its place, flags and scroll, a check
 * of its ClipRects (each pixel: visible, or the layer named by obscured,
 * and whether it has a bitmap), of its DamageList, of every pixel
 * ReadPixel reads through it, and of the whole bitmap.
 *
 *   LayersABITest [PROBE] [ROUNDS=n] [LIB=dir]
 * Exit 0 when nothing differs, 5 when something does, 20 on an error. */
#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/clip.h>
#include <graphics/layers.h>
#include <graphics/regions.h>
#include <utility/hooks.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifndef REG
#define REG(r, decl) register decl __asm(#r)
#endif

struct Library *LayersBase;

#define BW 320
#define BH 200
#define DEPTH 3
#define MAXL 7
#define STEPS 30

/* ---- the answers, as lines of text --------------------------------- */

#define OUTSIZE (1024UL * 1024UL)
static char *out_buf;
static ULONG out_len;
static int probe;

static void out(const char *fmt, ...)
{
    va_list ap;
    if (out_len > OUTSIZE - 512) return;
    va_start(ap, fmt);
    out_len += vsprintf(out_buf + out_len, fmt, ap);
    va_end(ap);
}

static ULONG crc;
static void crc_start(void) { crc = 2166136261UL; }
static void crc_add(ULONG v)
{
    int i;
    for (i = 0; i < 4; i++) { crc ^= (v >> (8 * i)) & 0xFF; crc *= 16777619UL; }
}

/* a longword as the Amiga reads it, on any host */
static ULONG be32(const UBYTE *b) { return (ULONG)b[0] << 24 | (ULONG)b[1] << 16 | (ULONG)b[2] << 8 | b[3]; }

static ULONG seed;
static ULONG rnd(void) { seed = seed * 1103515245UL + 12345UL; return seed >> 8; }
static LONG rn(LONG n) { return n > 0 ? (LONG)(rnd() % (ULONG)n) : 0; }

/* ---- the world ------------------------------------------------------ */

static struct BitMap *screen;
static struct Layer_Info *li;
static struct Layer *lay[MAXL];
static struct BitMap *sup[MAXL];
static int hid[MAXL], nlay;
static UBYTE map[BH][BW];

static int id_of(const void *l)
{
    int i;
    for (i = 0; i < nlay; i++) if ((const void *)lay[i] == l) return i;
    return l ? 99 : -1;
}

/* the hook: fills 8 x 8 cells of the layer's own coordinates (or the
 * screen's), and notes each call */
struct hookmsg {
    struct Layer *Layer;
    struct Rectangle Bounds;
    LONG OffsetX, OffsetY;
};
#define MAXCALLS 64
static char calls[MAXCALLS][112];
static int ncalls, morecalls;

static ULONG hook_fn(REG(a0, struct Hook *h), REG(a2, struct RastPort *rp), REG(a1, struct hookmsg *m))
{
    LONG x, y, salt = (LONG)h->h_Data;
    LONG x0 = m->Bounds.MinX, y0 = m->Bounds.MinY, x1 = m->Bounds.MaxX, y1 = m->Bounds.MaxY;
    LONG ox = m->OffsetX - x0, oy = m->OffsetY - y0;   /* own coordinate = bitmap coordinate + o */
    const char *where = rp->BitMap == screen ? "screen" : "other";
    int k;
    for (k = 0; k < nlay; k++) if (sup[k] && rp->BitMap == sup[k]) where = "super";
    if (ncalls < MAXCALLS) {
        sprintf(calls[ncalls], "K %d %s %ld %ld %ld %ld %ld %ld", id_of(m->Layer), where, (long)x0, (long)y0,
                (long)x1, (long)y1, (long)m->OffsetX, (long)m->OffsetY);
        if (id_of(m->Layer) == 99)    /* not one of ours: what it is */
            sprintf(calls[ncalls] + strlen(calls[ncalls]), " %d,%d,%d,%d f=%x rp=%d", m->Layer->bounds.MinX,
                    m->Layer->bounds.MinY, m->Layer->bounds.MaxX, m->Layer->bounds.MaxY, m->Layer->Flags,
                    rp->Layer ? id_of(rp->Layer) : -1);
        ncalls++;
    }
    else
        morecalls++;
    if (x0 > x1 || y0 > y1 || rp->Layer) return 0;
    for (y = (y0 + oy) & ~7L; y <= y1 + oy; y += 8)
        for (x = (x0 + ox) & ~7L; x <= x1 + ox; x += 8) {
            LONG a = x - ox, b = y - oy, c = x - ox + 7, d = y - oy + 7;
            if (a < x0) a = x0;
            if (b < y0) b = y0;
            if (c > x1) c = x1;
            if (d > y1) d = y1;
            SetAPen(rp, (ULONG)(((x >> 3) + (y >> 3) + salt) & 7));
            RectFill(rp, a, b, c, d);
        }
    return 0;
}

static struct Hook hooks[3];

static int cmp_str(const void *a, const void *b) { return strcmp(a, b); }

static void flush_calls(void)
{
    int i;
    if (probe) {
        qsort(calls, ncalls, sizeof calls[0], cmp_str);
        for (i = 0; i < ncalls; i++) out("%s\n", calls[i]);
        if (morecalls) out("K and %d more\n", morecalls);
    }
    ncalls = morecalls = 0;
}

/* ---- what each call leaves ------------------------------------------ */

static void detail(struct Layer *l, struct ClipRect *cr);

static void report_layer(int i)
{
    struct Layer *l = lay[i];
    struct ClipRect *cr;
    struct RegionRectangle *rr;
    LONG x, y, w = l->bounds.MaxX - l->bounds.MinX + 1, h = l->bounds.MaxY - l->bounds.MinY + 1;
    ULONG c_cr, c_dmg, c_px;
    int ncr = 0, bad = 0;

    for (y = 0; y < h; y++) for (x = 0; x < w; x++) map[y][x] = 0xFF;   /* 0xFF: no ClipRect */
    for (cr = l->ClipRect; cr; cr = cr->Next, ncr++) {
        APTR ob = (APTR)cr->obscured;
        UBYTE v = ob ? (UBYTE)(1 + (id_of(ob) & 0x3F)) : 0;
        if (cr->BitMap) v |= 0x40;
        for (y = cr->bounds.MinY; y <= cr->bounds.MaxY; y++)
            for (x = cr->bounds.MinX; x <= cr->bounds.MaxX; x++) {
                LONG u = x - l->bounds.MinX, t = y - l->bounds.MinY;
                if (u < 0 || t < 0 || u >= w || t >= h) { bad++; continue; }
                if (map[t][u] != 0xFF) bad++;
                map[t][u] = v;
            }
    }
    crc_start();
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) crc_add(map[y][x]);
    c_cr = crc;

    for (y = 0; y < h; y++) for (x = 0; x < w; x++) map[y][x] = 0;
    if (l->DamageList)
        for (rr = l->DamageList->RegionRectangle; rr; rr = rr->Next)
            for (y = l->DamageList->bounds.MinY + rr->bounds.MinY; y <= l->DamageList->bounds.MinY + rr->bounds.MaxY; y++)
                for (x = l->DamageList->bounds.MinX + rr->bounds.MinX; x <= l->DamageList->bounds.MinX + rr->bounds.MaxX; x++)
                    if (x >= 0 && y >= 0 && x < w && y < h) map[y][x] = 1;
    crc_start();
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) crc_add(map[y][x]);
    c_dmg = crc;

    crc_start();
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) crc_add((ULONG)ReadPixel(l->rp, x + l->Scroll_X, y + l->Scroll_Y));
    c_px = crc;

    out("L %d %d,%d,%d,%d f=%04x s=%d,%d n=%d bad=%d cr=%08lx dmg=%08lx px=%08lx\n", i, l->bounds.MinX, l->bounds.MinY,
        l->bounds.MaxX, l->bounds.MaxY, l->Flags, l->Scroll_X, l->Scroll_Y, probe ? ncr : 0, bad,
        (unsigned long)c_cr, (unsigned long)c_dmg, (unsigned long)c_px);
    if (probe)
        for (cr = l->ClipRect; cr; cr = cr->Next) {
            out("C %d %d,%d,%d,%d ob=%d bm=%d\n", i, cr->bounds.MinX, cr->bounds.MinY, cr->bounds.MaxX, cr->bounds.MaxY,
                id_of((APTR)cr->obscured), cr->BitMap ? (cr->BitMap == l->SuperBitMap ? 2 : 1) : 0);
            if (cr->obscured) detail(l, cr);
        }
}

/* a pixel of a bitmap, or -1 outside it */
static LONG bm_pixel(struct BitMap *bm, LONG x, LONG y)
{
    LONG v = 0;
    int p;
    if (x < 0 || y < 0 || x >= bm->BytesPerRow * 8 || y >= bm->Rows) return -1;
    for (p = 0; p < bm->Depth; p++)
        if (bm->Planes[p][y * bm->BytesPerRow + (x >> 3)] & (0x80 >> (x & 7))) v |= 1L << p;
    return v;
}

/* PROBE only, for an obscured ClipRect: what obscured holds (a small
 * number, the Layer_Info, the layer itself, one of the layers, the
 * ClipRect or another pointer), its bitmap's shape, where its planes are
 * in a super bitmap layer's own, and how many of its pixels, read as a
 * smart refresh ClipRect's are (at x - (MinX & ~15), y - MinY), are the
 * super bitmap's at the layer's own coordinates */
static void detail(struct Layer *l, struct ClipRect *cr)
{
    ULONG ob = (ULONG)cr->obscured;
    struct BitMap *bm = cr->BitMap, *sb = l->SuperBitMap;
    char what[16];
    LONG x, y, eq = 0, all = 0, sp = -1;
    if (ob < 0x1000) sprintf(what, "%lu", (unsigned long)ob);
    else if (ob == (ULONG)li) strcpy(what, "li");
    else if (ob == (ULONG)l) strcpy(what, "self");
    else if (ob == (ULONG)cr) strcpy(what, "cr");
    else if (id_of((APTR)ob) != 99) sprintf(what, "L%d", id_of((APTR)ob));
    else strcpy(what, "p");
    if (!bm) { out("D ob=%s\n", what); return; }
    if (sb && bm != sb && bm->Planes[0] >= sb->Planes[0] && bm->Planes[0] < sb->Planes[0] + sb->BytesPerRow * sb->Rows)
        sp = (LONG)(bm->Planes[0] - sb->Planes[0]);
    if (sb && bm != sb)
        for (y = cr->bounds.MinY; y <= cr->bounds.MaxY; y++)
            for (x = cr->bounds.MinX; x <= cr->bounds.MaxX; x++, all++)
                eq += bm_pixel(bm, x - (cr->bounds.MinX & ~15), y - cr->bounds.MinY) ==
                      bm_pixel(sb, x - l->bounds.MinX + l->Scroll_X, y - l->bounds.MinY + l->Scroll_Y);
    out("D ob=%s bm=%dx%dx%d f=%d sp=%ld eq=%ld/%ld\n", what, bm->BytesPerRow, bm->Rows, bm->Depth, bm->Flags, (long)sp,
        (long)eq, (long)all);
}

static void report(int step, const char *what, LONG r)
{
    struct Layer *l;
    int i;
    ULONG p, n;
    out("S %d %s %ld\n", step, what, (long)r);
    flush_calls();
    out("O");
    for (l = li->top_layer; l; l = l->back) out(" %d", id_of(l));
    out("\n");
    for (i = 0; i < nlay; i++) report_layer(i);
    WaitBlit();
    crc_start();
    for (p = 0; p < DEPTH; p++)
        for (n = 0; n < (ULONG)screen->BytesPerRow * screen->Rows; n += 4) crc_add(be32(screen->Planes[p] + n));
    out("B %08lx\n", (unsigned long)crc);
}

/* boxes in every shown layer, in its own coordinates */
static void draw_all(void)
{
    int i, k;
    for (i = 0; i < nlay; i++) {
        struct Layer *l = lay[i];
        LONG w = l->bounds.MaxX - l->bounds.MinX + 1, h = l->bounds.MaxY - l->bounds.MinY + 1;
        if (hid[i]) continue;
        for (k = 0; k < 3; k++) {
            LONG a = rn(w), b = rn(h), c = a + rn(w - a), d = b + rn(h - b);
            SetAPen(l->rp, (ULONG)(1 + rn(7)));
            RectFill(l->rp, a + l->Scroll_X, b + l->Scroll_Y, c + l->Scroll_X, d + l->Scroll_Y);
        }
    }
}

static void fill_bitmap(struct BitMap *bm, ULONG salt)
{
    ULONG p, n;
    WaitBlit();
    for (p = 0; p < bm->Depth; p++)
        for (n = 0; n < (ULONG)bm->BytesPerRow * bm->Rows; n++) bm->Planes[p][n] = (UBYTE)((n * 7 + p * 31 + salt) ^ (n >> 5));
}

static void make(int step)
{
    LONG w = 2 + rn(120), h = 2 + rn(90), x0 = rn(BW - w), y0 = rn(BH - h), kind = rn(3), flags;
    struct Hook *hk;
    struct Layer *l;
    struct BitMap *sb = 0;
    if (nlay >= MAXL) return;
    flags = kind == 0 ? LAYERSIMPLE : kind == 1 ? LAYERSMART : LAYERSMART | LAYERSUPER;
    if (rn(5) == 0) flags |= LAYERBACKDROP;
    hk = rn(3) == 0 ? LAYERS_BACKFILL : rn(4) == 0 ? LAYERS_NOBACKFILL : &hooks[rn(2)];
    if (flags & LAYERSUPER) {
        if (!(sb = AllocBitMap(BW, BH, DEPTH, BMF_CLEAR, 0))) return;
        fill_bitmap(sb, (ULONG)step);
    }
    l = rn(2) ? CreateUpfrontHookLayer(li, screen, x0, y0, x0 + w - 1, y0 + h - 1, flags, hk, sb)
              : CreateBehindHookLayer(li, screen, x0, y0, x0 + w - 1, y0 + h - 1, flags, hk, sb);
    if (!l) { if (sb) FreeBitMap(sb); report(step, "create failed", 0); return; }
    lay[nlay] = l;
    sup[nlay] = sb;
    hid[nlay] = 0;
    nlay++;
    report(step, "create", flags);
}

static void delete_layer(int step, int i)
{
    int k;
    LONG r = DeleteLayer(0, lay[i]);
    if (sup[i]) { WaitBlit(); FreeBitMap(sup[i]); }
    for (k = i; k + 1 < nlay; k++) { lay[k] = lay[k + 1]; sup[k] = sup[k + 1]; hid[k] = hid[k + 1]; }
    nlay--;
    if (step >= 0) report(step, "delete", r);
}

/* a delta that keeps a layer inside the bitmap */
static LONG keep(LONG lo, LONG hi, LONG d, LONG max)
{
    if (lo + d < 0) d = -lo;
    if (hi + d > max) d = max - hi;
    return d;
}

static void one_round(int round)
{
    int step;
    seed = 0x1A7E0000UL + (ULONG)round;
    fill_bitmap(screen, 0);
    nlay = 0;
    if (!(li = NewLayerInfo())) { out("no Layer_Info\n"); return; }
    out("R %d\n", round);
    if (rn(2)) InstallLayerInfoHook(li, &hooks[2]);
    for (step = 0; step < STEPS; step++) {
        int op = (int)rn(17), i;
        struct Layer *l;
        LONG r = 0, dx, dy, dw, dh;
        if (!nlay || (op == 0 && nlay < MAXL)) { draw_all(); make(step); continue; }
        i = (int)rn(nlay);
        l = lay[i];
        draw_all();
        switch (op) {
        case 1: report(step, "upfront", UpfrontLayer(0, l)); break;
        case 2: report(step, "behind", BehindLayer(0, l)); break;
        case 3: case 4:
            dx = keep(l->bounds.MinX, l->bounds.MaxX, rn(61) - 30, BW - 1);
            dy = keep(l->bounds.MinY, l->bounds.MaxY, rn(41) - 20, BH - 1);
            report(step, "move", MoveLayer(0, l, dx, dy));
            break;
        case 5:
            dx = keep(0, l->bounds.MaxX, rn(41) - 20, BW - 1);
            dy = keep(0, l->bounds.MaxY, rn(31) - 15, BH - 1);
            if (l->bounds.MaxX + dx <= l->bounds.MinX) dx = 0;
            if (l->bounds.MaxY + dy <= l->bounds.MinY) dy = 0;
            report(step, "size", SizeLayer(0, l, dx, dy));
            break;
        case 6:
            dx = keep(l->bounds.MinX, l->bounds.MaxX, rn(21) - 10, BW - 1);
            dy = keep(l->bounds.MinY, l->bounds.MaxY, rn(21) - 10, BH - 1);
            dw = keep(0, l->bounds.MaxX + dx, rn(21) - 10, BW - 1);
            dh = keep(0, l->bounds.MaxY + dy, rn(21) - 10, BH - 1);
            if (l->bounds.MaxX - l->bounds.MinX + dw < 1) dw = 0;
            if (l->bounds.MaxY - l->bounds.MinY + dh < 1) dh = 0;
            report(step, "movesize", MoveSizeLayer(l, dx, dy, dw, dh));
            break;
        case 7: report(step, "infrontof", MoveLayerInFrontOf(l, lay[rn(nlay)])); break;
        case 8:
            if (hid[i]) {
                int k = (int)rn(nlay);
                r = ShowLayer(l, k == i ? l : lay[k]);
                hid[i] = 0;
                report(step, "show", r);
            } else {
                r = HideLayer(l);
                hid[i] = 1;
                report(step, "hide", r);
            }
            break;
        case 9: delete_layer(step, i); break;
        case 10: {
            struct Region *rg = NewRegion(), *old;
            int k, n = 1 + (int)rn(3);
            LONG w = l->bounds.MaxX - l->bounds.MinX + 1, h = l->bounds.MaxY - l->bounds.MinY + 1;
            if (!rg) break;
            for (k = 0; k < n; k++) {
                struct Rectangle q;
                q.MinX = (WORD)(rn(w) + l->Scroll_X); q.MinY = (WORD)(rn(h) + l->Scroll_Y);
                q.MaxX = (WORD)(q.MinX + rn(40)); q.MaxY = (WORD)(q.MinY + rn(30));
                OrRectRegion(rg, &q);
            }
            old = InstallClipRegion(l, rg);
            SetAPen(l->rp, (ULONG)(1 + rn(7)));
            RectFill(l->rp, l->Scroll_X, l->Scroll_Y, l->Scroll_X + w - 1, l->Scroll_Y + h - 1);
            InstallClipRegion(l, old);
            DisposeRegion(rg);
            report(step, "clipregion", 0);
            break;
        }
        case 11: {
            LONG w = l->bounds.MaxX - l->bounds.MinX + 1, h = l->bounds.MaxY - l->bounds.MinY + 1;
            r = BeginUpdate(l);
            SetAPen(l->rp, (ULONG)(1 + rn(7)));
            RectFill(l->rp, l->Scroll_X, l->Scroll_Y, l->Scroll_X + w - 1, l->Scroll_Y + h - 1);
            EndUpdate(l, (UWORD)rn(2));
            report(step, "update", r);
            break;
        }
        case 12:
            dx = rn(21) - 10;
            dy = rn(21) - 10;
            ScrollLayer(0, l, dx, dy);
            report(step, "scroll", 0);
            break;
        case 13: {
            LONG x = rn(BW), y = rn(BH);
            out("W %ld %ld %d\n", (long)x, (long)y, id_of(WhichLayer(li, x, y)));
            out("V %d %ld\n", i, (long)LayerOccluded(l));
            break;
        }
        case 14: {
            struct Rectangle q;
            struct RastPort rp;
            q.MinX = (WORD)(rn(80) - 10); q.MinY = (WORD)(rn(60) - 10);
            q.MaxX = (WORD)(q.MinX + rn(100)); q.MaxY = (WORD)(q.MinY + rn(80));
            LockLayer(0, l);
            DoHookClipRects(&hooks[0], l->rp, &q);
            UnlockLayer(l);
            InitRastPort(&rp);
            rp.BitMap = screen;
            q.MinX = (WORD)rn(BW / 2); q.MinY = (WORD)rn(BH / 2);
            q.MaxX = (WORD)(q.MinX + rn(BW / 2)); q.MaxY = (WORD)(q.MinY + rn(BH / 2));
            LockLayers(li);
            DoHookClipRects(&hooks[1], &rp, &q);
            UnlockLayers(li);
            report(step, "dohook", 0);
            break;
        }
        case 15: {
            /* the order: copying each ClipRect moved by (-dx, -dy) never
             * lands on one still to come */
            struct ClipRect *a, *b;
            LONG sdx = rn(21) - 10, sdy = rn(21) - 10;
            int ok = 1;
            LockLayer(0, l);
            SortLayerCR(l, sdx, sdy);
            for (a = l->ClipRect; a; a = a->Next)
                for (b = a->Next; b; b = b->Next)
                    if (a->bounds.MinX - sdx <= b->bounds.MaxX && a->bounds.MaxX - sdx >= b->bounds.MinX &&
                        a->bounds.MinY - sdy <= b->bounds.MaxY && a->bounds.MaxY - sdy >= b->bounds.MinY)
                        ok = 0;
            UnlockLayer(l);
            out("Q %d %ld %ld %s\n", i, (long)sdx, (long)sdy, ok ? "ordered" : "overlaps");
            break;
        }
        default: {
            /* SwapBitsRastPortClipRect on the bare screen, as Intuition's
             * menus use it */
            struct ClipRect *cr = AllocMem(sizeof *cr, MEMF_PUBLIC | MEMF_CLEAR);
            struct RastPort rp;
            LONG w = 8 + rn(80), h = 8 + rn(60), x = rn(BW - w), y = rn(BH - h);
            if (!cr) break;
            if ((cr->BitMap = AllocBitMap((ULONG)(w + 16), (ULONG)h, DEPTH, BMF_CLEAR, screen))) {
                ULONG p, n;
                cr->bounds.MinX = (WORD)x; cr->bounds.MinY = (WORD)y;
                cr->bounds.MaxX = (WORD)(x + w - 1); cr->bounds.MaxY = (WORD)(y + h - 1);
                fill_bitmap(cr->BitMap, 5);
                InitRastPort(&rp);
                rp.BitMap = screen;
                LockLayers(li);
                SwapBitsRastPortClipRect(&rp, cr);
                UnlockLayers(li);
                WaitBlit();
                crc_start();
                for (p = 0; p < DEPTH; p++)
                    for (n = 0; n < (ULONG)cr->BitMap->BytesPerRow * h; n++) crc_add(cr->BitMap->Planes[p][n]);
                out("X %08lx\n", (unsigned long)crc);
                FreeBitMap(cr->BitMap);
            }
            FreeMem(cr, sizeof *cr);
            report(step, "swapbits", 0);
        }
        }
    }
    while (nlay) delete_layer(-1, 0);
    DisposeLayerInfo(li);
    flush_calls();
}

static void run(int rounds)
{
    int r;
    out_len = 0;
    for (r = 0; r < rounds; r++) one_round(r);
}

static int word(const char *a, const char *w, int prefix)
{
    size_t n = strlen(w);
    return prefix ? !strncmp(a, w, n) : !strcmp(a, w);
}

/* the next line of a buffer: its length, and where the one after starts */
static ULONG line(const char *b, ULONG len, ULONG at, ULONG *next)
{
    ULONG e = at;
    while (e < len && b[e] != '\n') e++;
    *next = e < len ? e + 1 : e;
    return e - at;
}

int main(int argc, char **argv)
{
    static char path[256];
    char *dir = 0, *os_out = 0;
    struct Library *os_base = 0, *our_base = 0;
    int i, rounds = 3, rc = 0;
    ULONG os_len = 0, diffs = 0, lines = 0;

    for (i = 1; i < argc; i++) {
        if (word(argv[i], "PROBE", 0)) probe = 1;
        else if (word(argv[i], "ROUNDS=", 1)) rounds = (int)strtol(argv[i] + 7, 0, 10);
        else if (word(argv[i], "LIB=", 1)) dir = argv[i] + 4;
    }
    for (i = 0; i < 3; i++) {
        hooks[i].h_Entry = (ULONG (*)())(void (*)(void))hook_fn;
        hooks[i].h_Data = (APTR)(LONG)(i * 3);
    }
    out_buf = AllocMem(OUTSIZE, MEMF_PUBLIC);
    os_out = AllocMem(OUTSIZE, MEMF_PUBLIC);
    screen = AllocBitMap(BW, BH, DEPTH, BMF_CLEAR | BMF_DISPLAYABLE, 0);
    if (!out_buf || !os_out || !screen) { printf("no memory\n"); rc = 20; goto done; }
    if (!(os_base = OpenLibrary("layers.library", 39))) { printf("no layers.library\n"); rc = 20; goto done; }
    if (dir && !probe) {
        strcpy(path, dir);
        AddPart(path, "openlayers.library", sizeof path);
        if (!(our_base = OpenLibrary(path, 47))) { printf("no %s\n", path); rc = 20; goto done; }
    }
    printf("OS: %s", (char *)os_base->lib_IdString);
    if (our_base) printf("ours: %s", (char *)our_base->lib_IdString);

    LayersBase = os_base;
    run(rounds);
    if (!our_base) {
        fwrite(out_buf, 1, out_len, stdout);
        printf("OS 3.2's answers: %lu bytes\n", (unsigned long)out_len);
        goto done;
    }
    memcpy(os_out, out_buf, out_len);
    os_len = out_len;
    LayersBase = our_base;
    run(rounds);
    {
        ULONG a = 0, b = 0, na, nb;
        while (a < os_len || b < out_len) {
            ULONG la = line(os_out, os_len, a, &na), lb = line(out_buf, out_len, b, &nb);
            lines++;
            if (la != lb || memcmp(os_out + a, out_buf + b, la)) {
                if (diffs++ < 400) printf("OS:   %.*s\nours: %.*s\n", (int)la, os_out + a, (int)lb, out_buf + b);
            }
            a = na;
            b = nb;
        }
    }
    printf("%lu lines, %lu differ\n", (unsigned long)lines, (unsigned long)diffs);
    if (diffs) rc = 5;
done:
    LayersBase = 0;
    if (our_base) CloseLibrary(our_base);
    if (os_base) CloseLibrary(os_base);
    if (screen) { WaitBlit(); FreeBitMap(screen); }
    if (os_out) FreeMem(os_out, OUTSIZE);
    if (out_buf) FreeMem(out_buf, OUTSIZE);
    return rc;
}
