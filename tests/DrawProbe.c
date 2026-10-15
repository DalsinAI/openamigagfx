/* DrawProbe: what graphics.library draws, one call at a time, as lines to
 * diff between two ROMs (OS 3.2.3's and the open one).
 * Copyright (c) 2026 Dalsin Limited. MIT.
 *
 * Every line is "case | what | value", with no addresses. For each target
 * (an 8-plane and a 2-plane bitmap of its own, an 8-plane AGA screen, an
 * 8-bit RTG screen when there is a mode for one, a screen like the
 * Workbench's with a palette of its own in which no two pens share a
 * colour, and windows on these screens and on the Workbench, one of them
 * covered by another for the layers' clipping), each call is drawn alone
 * on the same background, and the
 * target's pens are read back with ReadPixel and summed (CRC-32). In a
 * window the pens are read through the window (the covered part a smart
 * refresh layer keeps included) and through the screen (what shows).
 * Calls: RectFill (draw modes, pens above the depth, masks, area
 * patterns), Draw (slopes, line patterns), WritePixel, BltTemplate,
 * BltBitMapRastPort and BltMaskBitMapRastPort (minterms, a source of 2 and
 * of 8 planes, masks), WritePixelArray8, WriteChunkyPixels, DrawImage
 * (PlanePick, PlaneOnOff), Text, ClipBlit, ScrollRaster, SetRast,
 * EraseRect, DrawEllipse; and the colours: GetRGB32 of the Workbench
 * screen and of a new screen, SetRGB32CM/GetRGB32 and SetRGB4CM/GetRGB4
 * round trips, and ObtainBestPen's picks.
 *   DrawProbe [DUMP] [ONLY substring]
 * DUMP prints the pens of every case (or of the ONLY ones) as "#" lines. */
#include <stdio.h>
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <graphics/modeid.h>
#include <graphics/displayinfo.h>
#include <graphics/text.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layers.h>

struct GfxBase *GfxBase;
struct IntuitionBase *IntuitionBase;
struct Library *LayersBase;

#define W 96
#define H 40
static int dump;
static const char *only;

static ULONG crctab[256];
static ULONG crc(const UBYTE *p, ULONG n)
{
    ULONG c = 0xFFFFFFFFUL;
    while (n--)
        c = crctab[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return ~c;
}

struct target {
    const char *name;
    struct ColorMap *cm;    /* when set, each pen read is also summed as its colour */
    struct RastPort *rp;    /* drawn into */
    struct RastPort *show;  /* read back as shown (a window's screen), or NULL */
    LONG sx, sy;            /* where the window is on that screen */
    LONG depth;
};

static UBYTE px[W * H];
static void readback(struct RastPort *rp, LONG ox, LONG oy)
{
    LONG x, y;
    WaitBlit();
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            px[y * W + x] = (UBYTE)ReadPixel(rp, ox + x, oy + y);
}
static void show_px(const char *c)
{
    LONG x, y;
    printf("# PIX %s\n", c);
    for (y = 0; y < H; y++) {
        printf("# ");
        for (x = 0; x < W; x++)
            printf("%02x", px[y * W + x]);
        printf("\n");
    }
}

/* the same background before every call: pen 0, pen 0x5A top left, 0xC3
 * bottom right, a pen 7 bar across the middle */
static void background(struct RastPort *rp)
{
    SetWrMsk(rp, 0xFF);
    SetAfPt(rp, NULL, 0);
    SetDrPt(rp, 0xFFFF);
    SetDrMd(rp, JAM2);
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, W - 1, H - 1);
    SetAPen(rp, 0x5A);
    RectFill(rp, 0, 0, W / 2 - 1, H / 2 - 1);
    SetAPen(rp, 0xC3);
    RectFill(rp, W / 2, H / 2, W - 1, H - 1);
    SetAPen(rp, 7);
    RectFill(rp, 0, H / 2 - 2, W - 1, H / 2 + 1);
    SetAPen(rp, 1);
    SetBPen(rp, 0);
}

/* the pens just read, as the colours the map gives them */
static ULONG colour_crc(struct ColorMap *cm)
{
    static ULONG rgb[256][3];
    static UBYTE row[W * 3];
    ULONG c = 0xFFFFFFFFUL;
    LONG x, y;
    GetRGB32(cm, 0, cm->Count < 256 ? cm->Count : 256, rgb[0]);
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            UBYTE p = px[y * W + x];
            row[x * 3] = (UBYTE)(rgb[p][0] >> 24);
            row[x * 3 + 1] = (UBYTE)(rgb[p][1] >> 24);
            row[x * 3 + 2] = (UBYTE)(rgb[p][2] >> 24);
        }
        c = crc(row, sizeof row) ^ (c * 31 + (ULONG)y);
    }
    return c;
}

static struct target *cur;
static void result(const char *what)
{
    char c[160];
    if (only && !strstr(what, only))
        return;
    snprintf(c, sizeof c, "%s | %s", cur->name, what);
    readback(cur->rp, 0, 0);
    printf("%s | %08lx\n", c, (unsigned long)crc(px, sizeof px));
    if (dump)
        show_px(c);
    if (cur->cm)
        printf("%s as colours | %08lx\n", c, (unsigned long)colour_crc(cur->cm));
    if (cur->show) {
        readback(cur->show, cur->sx, cur->sy);
        printf("%s shown | %08lx\n", c, (unsigned long)crc(px, sizeof px));
        if (cur->cm)
            printf("%s shown as colours | %08lx\n", c, (unsigned long)colour_crc(cur->cm));
        if (dump)
            show_px(c);
    }
    fflush(stdout);
}

static const UBYTE modes[] = { JAM1, JAM2, COMPLEMENT, JAM1 | INVERSVID, JAM2 | INVERSVID, COMPLEMENT | INVERSVID };
static const char *const mname[] = { "JAM1", "JAM2", "COMPLEMENT", "JAM1|INV", "JAM2|INV", "COMPL|INV" };
static void set(struct RastPort *rp, ULONG a, ULONG b, int m, ULONG mask)
{
    SetAPen(rp, a);
    SetBPen(rp, b);
    SetDrMd(rp, modes[m]);
    SetWrMsk(rp, mask);
}

/* in Chip RAM, where the blitter reads them */
static UWORD *tmpl;     /* 48 x 8 template, 6 bytes a row */
static UWORD *mask1;    /* BltMaskBitMapRastPort's mask, 48 x 16 */
static UWORD *img2;     /* 2 planes of 32 x 10 */
static struct BitMap *src8, *src2;

static void make_sources(void)
{
    int i, p, r;
    tmpl = AllocMem(8 * 3 * 2, MEMF_CHIP);
    mask1 = AllocMem(16 * 3 * 2, MEMF_CHIP);
    img2 = AllocMem(2 * 10 * 2 * 2, MEMF_CHIP);
    if (!tmpl || !mask1 || !img2)
        return;
    for (i = 0; i < 8 * 3; i++)
        tmpl[i] = (UWORD)(0xF0C3 ^ (i * 0x1357));
    for (i = 0; i < 16 * 3; i++)
        mask1[i] = (UWORD)((i & 1) ? 0x0FF0 : 0xFF0F) ^ (UWORD)(i * 0x0101);
    for (i = 0; i < 2 * 10 * 2; i++)
        img2[i] = (UWORD)(0xA5C3 + i * 0x2211);
    src8 = AllocBitMap(48, 16, 8, BMF_CLEAR, NULL);
    src2 = AllocBitMap(48, 16, 2, BMF_CLEAR, NULL);
    WaitBlit();
    for (p = 0; p < 8; p++)
        if (src8)
            for (r = 0; r < 16; r++)
                for (i = 0; i < src8->BytesPerRow; i++)
                    src8->Planes[p][r * src8->BytesPerRow + i] = (UBYTE)(r * 17 + i * 29 + p * 71) ^ (UBYTE)(0x55 << (p & 1));
    for (p = 0; p < 2; p++)
        if (src2)
            for (r = 0; r < 16; r++)
                for (i = 0; i < src2->BytesPerRow; i++)
                    src2->Planes[p][r * src2->BytesPerRow + i] = (UBYTE)(r * 13 + i * 37 + p * 91);
}

static void run_target(struct target *t)
{
    struct RastPort *rp = t->rp;
    char w[96];
    int m, k, a;
    static const ULONG apens[] = { 1, 6, 0xA5 };
    static const ULONG wmasks[] = { 0xFF, 0x0F };
    static UWORD pat2[2] = { 0xAAAA, 0x5555 }, pat4[4] = { 0xF0F0, 0x0F0F, 0xFF00, 0x00FF };
    cur = t;
    printf("%s | depth | %ld\n", t->name, (long)t->depth);

    background(rp);
    result("background");

    for (m = 0; m < 6; m++)
        for (a = 0; a < 3; a++)
            for (k = 0; k < 2; k++) {
                background(rp);
                set(rp, apens[a], 2, m, wmasks[k]);
                RectFill(rp, 3, 2, 50, 30);
                snprintf(w, sizeof w, "RectFill %s pen %lx mask %lx", mname[m], (unsigned long)apens[a], (unsigned long)wmasks[k]);
                result(w);
            }
    for (m = 0; m < 6; m++)
        for (k = 0; k < 2; k++) {
            background(rp);
            set(rp, 3, 0x82, m, 0xFF);
            if (k)
                SetAfPt(rp, pat4, 2);
            else
                SetAfPt(rp, pat2, 1);
            RectFill(rp, 5, 3, 70, 33);
            SetAfPt(rp, NULL, 0);
            snprintf(w, sizeof w, "RectFill pattern %d %s", k ? 4 : 2, mname[m]);
            result(w);
        }
    background(rp);
    set(rp, 4, 0, 1, 0xFF);
    RectFill(rp, -5, -3, W + 7, 10);
    result("RectFill past the edges");

    for (m = 0; m < 6; m++)
        for (k = 0; k < 3; k++) {
            static const UWORD lp[] = { 0xFFFF, 0xF0F0, 0x8888 };
            background(rp);
            set(rp, 5, 0x21, m, 0xFF);
            SetDrPt(rp, lp[k]);
            Move(rp, 0, 0);
            Draw(rp, W - 1, H - 1);
            Draw(rp, W - 1, 0);
            Draw(rp, 0, H - 1);
            Draw(rp, 10, 3);
            Move(rp, 2, 35);
            Draw(rp, 90, 33);
            Move(rp, 40, 1);
            Draw(rp, 41, 39);
            Move(rp, 60, 5);
            Draw(rp, 60, 5);
            SetDrPt(rp, 0xFFFF);
            snprintf(w, sizeof w, "Draw %s pattern %04x", mname[m], lp[k]);
            result(w);
        }
    for (m = 0; m < 6; m++) {
        LONG x;
        background(rp);
        set(rp, 0x99, 0x0C, m, 0xFF);
        for (x = 0; x < W; x += 3)
            WritePixel(rp, x, (x * 7) % H);
        snprintf(w, sizeof w, "WritePixel %s", mname[m]);
        result(w);
    }
    for (m = 0; m < 6; m++)
        for (k = 0; k < 3; k++) {
            static const WORD sx[] = { 0, 5, 17 };
            background(rp);
            set(rp, 0x0B, 0x60, m, k == 2 ? 0x3C : 0xFF);
            BltTemplate((PLANEPTR)tmpl, sx[k], 6, rp, 7, 9, 48 - sx[k] - 3, 8);
            snprintf(w, sizeof w, "BltTemplate %s srcx %d mask %x", mname[m], sx[k], k == 2 ? 0x3C : 0xFF);
            result(w);
        }
    {
        static const UBYTE mt[] = { 0xC0, 0x30, 0x50, 0x60, 0x80, 0xE0, 0x20, 0x0A, 0xEE, 0x00, 0xFF };
        int s;
        for (s = 0; s < 2; s++)
            for (k = 0; k < (int)sizeof mt; k++)
                for (a = 0; a < 2; a++) {
                    background(rp);
                    SetWrMsk(rp, a ? 0x33 : 0xFF);
                    BltBitMapRastPort(s ? src2 : src8, a ? 3 : 0, 1, rp, 5, 7, 40, 14, mt[k]);
                    SetWrMsk(rp, 0xFF);
                    snprintf(w, sizeof w, "BltBitMapRastPort from %d planes minterm %02x %s", s ? 2 : 8, mt[k],
                             a ? "srcx 3 mask 33" : "srcx 0");
                    result(w);
                }
    }
    {
        static const UBYTE mt[] = { 0xE0, 0x60, 0xC0, 0x20, 0x80, 0x0A };
        int s;
        for (s = 0; s < 2; s++)
            for (k = 0; k < (int)sizeof mt; k++)
                for (a = 0; a < 2; a++) {
                    background(rp);
                    SetWrMsk(rp, a ? 0x0F : 0xFF);
                    BltMaskBitMapRastPort(s ? src2 : src8, a ? 5 : 0, 0, rp, 9, 6, 40, 16, mt[k], (PLANEPTR)mask1);
                    SetWrMsk(rp, 0xFF);
                    snprintf(w, sizeof w, "BltMaskBitMapRastPort from %d planes minterm %02x %s", s ? 2 : 8, mt[k],
                             a ? "srcx 5 mask 0f" : "srcx 0");
                    result(w);
                }
    }
    {
        static UBYTE arr[64 * 24];
        struct RastPort tmp;
        struct BitMap *tbm = AllocBitMap(64, 1, 8, BMF_CLEAR, NULL);
        int i;
        for (i = 0; i < (int)sizeof arr; i++)
            arr[i] = (UBYTE)(i * 7 + (i >> 6) * 3);
        for (k = 0; k < 2; k++) {
            background(rp);
            SetWrMsk(rp, k ? 0x1E : 0xFF);
            if (tbm) {
                InitRastPort(&tmp);
                tmp.Layer = NULL;
                tmp.BitMap = tbm;
                WritePixelArray8(rp, 5, 3, 5 + 55, 3 + 20, arr, &tmp);
            }
            SetWrMsk(rp, 0xFF);
            snprintf(w, sizeof w, "WritePixelArray8 mask %x", k ? 0x1E : 0xFF);
            result(w);
            background(rp);
            SetWrMsk(rp, k ? 0x1E : 0xFF);
            WriteChunkyPixels(rp, 7, 2, 7 + 55, 2 + 20, arr, 64);
            SetWrMsk(rp, 0xFF);
            snprintf(w, sizeof w, "WriteChunkyPixels mask %x", k ? 0x1E : 0xFF);
            result(w);
        }
        if (tbm) {
            WaitBlit();
            FreeBitMap(tbm);
        }
    }
    {
        static const UBYTE pick[] = { 0x03, 0x05, 0x00, 0x03, 0x0C, 0xC0 };
        static const UBYTE onoff[] = { 0x00, 0x02, 0x08, 0xF0, 0x01, 0x00 };
        for (k = 0; k < (int)sizeof pick; k++)
            for (m = 0; m < 2; m++) {
                struct Image im;
                memset(&im, 0, sizeof im);
                im.LeftEdge = 2;
                im.TopEdge = 1;
                im.Width = 30;
                im.Height = 10;
                im.Depth = 2;
                im.ImageData = img2;
                im.PlanePick = pick[k];
                im.PlaneOnOff = onoff[k];
                background(rp);
                set(rp, 1, 0, m, 0xFF);
                DrawImage(rp, &im, 4, 6);
                snprintf(w, sizeof w, "DrawImage pick %02x onoff %02x drawn in %s", pick[k], onoff[k], mname[m]);
                result(w);
            }
    }
    for (m = 0; m < 6; m++) {
        background(rp);
        set(rp, 1, 0x1C, m, 0xFF);
        SetFont(rp, GfxBase->DefaultFont);
        Move(rp, 3, 12);
        Text(rp, (CONST_STRPTR)"Label", 5);
        Move(rp, 50, 30);
        Text(rp, (CONST_STRPTR)"Iq", 2);
        snprintf(w, sizeof w, "Text %s", mname[m]);
        result(w);
    }
    {
        static const UBYTE mt[] = { 0xC0, 0x50, 0x30 };
        for (k = 0; k < 3; k++) {
            background(rp);
            ClipBlit(rp, 2, 2, rp, 9, 5, 60, 30, mt[k]);
            snprintf(w, sizeof w, "ClipBlit overlapping minterm %02x", mt[k]);
            result(w);
        }
    }
    background(rp);
    SetBPen(rp, 0x0D);
    ScrollRaster(rp, 3, 2, 4, 4, 80, 34);
    result("ScrollRaster 3,2");
    background(rp);
    SetBPen(rp, 0x0D);
    ScrollRaster(rp, -5, -3, 4, 4, 80, 34);
    result("ScrollRaster -5,-3");
    if (rp->Layer) {
        background(rp);
        EraseRect(rp, 10, 4, 70, 30);
        result("EraseRect");
    }
    for (m = 0; m < 3; m++) {
        background(rp);
        set(rp, 0x2E, 0, m, 0xFF);
        DrawEllipse(rp, 45, 20, 30, 15);
        DrawEllipse(rp, 20, 10, 3, 9);
        snprintf(w, sizeof w, "DrawEllipse %s", mname[m]);
        result(w);
    }
    background(rp);
    SetRast(rp, 0x81);
    result("SetRast 81");
    SetAPen(rp, 1);
    SetBPen(rp, 0);
    SetDrMd(rp, JAM2);
    SetWrMsk(rp, 0xFF);
}

static void colours(const char *c, struct ColorMap *cm, ULONG n)
{
    ULONG i, rgb[3];
    for (i = 0; i < n; i++) {
        GetRGB32(cm, i, 1, rgb);
        printf("%s | colour %lu | %08lx %08lx %08lx\n", c, (unsigned long)i, (unsigned long)rgb[0], (unsigned long)rgb[1], (unsigned long)rgb[2]);
    }
}

static void colour_calls(struct Screen *wb)
{
    struct ColorMap *cm = GetColorMap(32);
    static const ULONG guns[][3] = {
        { 0x00000000, 0x00000000, 0x00000000 }, { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF },
        { 0x12345678, 0x9ABCDEF0, 0x0F0F0F0F }, { 0x80000000, 0x7FFFFFFF, 0x01000000 },
        { 0xAA000000, 0x55000000, 0x00FF0000 }, { 0x00FFFFFF, 0xFF000000, 0x00000001 },
    };
    ULONG i, rgb[3];
    if (cm) {
        for (i = 0; i < 6; i++) {
            SetRGB32CM(cm, i, guns[i][0], guns[i][1], guns[i][2]);
            GetRGB32(cm, i, 1, rgb);
            printf("colourmap | SetRGB32CM %lu | %08lx %08lx %08lx GetRGB4 %04lx\n", (unsigned long)i, (unsigned long)rgb[0], (unsigned long)rgb[1], (unsigned long)rgb[2],
                   (unsigned long)(GetRGB4(cm, i) & 0xFFFF));
        }
        SetRGB4CM(cm, 7, 0xA, 0x5, 0xF);
        GetRGB32(cm, 7, 1, rgb);
        printf("colourmap | SetRGB4CM 7 a,5,f | %08lx %08lx %08lx GetRGB4 %04lx\n", (unsigned long)rgb[0], (unsigned long)rgb[1], (unsigned long)rgb[2],
               (unsigned long)(GetRGB4(cm, 7) & 0xFFFF));
        FreeColorMap(cm);
    }
    if (wb) {
        static const ULONG want[][3] = {
            { 0x00, 0x00, 0x00 }, { 0xFF, 0xFF, 0xFF }, { 0xAA, 0xAA, 0xAA }, { 0x66, 0x88, 0xBB },
            { 0xFF, 0x00, 0x00 }, { 0x12, 0x34, 0x56 }, { 0x95, 0x95, 0x95 }, { 0x3B, 0x67, 0xA2 },
        };
        LONG prec;
        colours("workbench screen", wb->ViewPort.ColorMap, 16);
        for (prec = 0; prec < 2; prec++)
            for (i = 0; i < 8; i++) {
                struct TagItem t[2] = { { OBP_Precision, prec ? PRECISION_EXACT : PRECISION_IMAGE }, { TAG_DONE, 0 } };
                LONG pen = ObtainBestPenA(wb->ViewPort.ColorMap, want[i][0] * 0x01010101UL, want[i][1] * 0x01010101UL,
                                          want[i][2] * 0x01010101UL, t);
                rgb[0] = rgb[1] = rgb[2] = 0;
                if (pen >= 0)
                    GetRGB32(wb->ViewPort.ColorMap, pen, 1, rgb);
                printf("workbench screen | ObtainBestPen %02lx%02lx%02lx %s | %s %08lx %08lx %08lx\n", (unsigned long)want[i][0],
                       (unsigned long)want[i][1], (unsigned long)want[i][2], prec ? "exact" : "image", pen < 0 ? "none" : "a pen",
                       (unsigned long)rgb[0], (unsigned long)rgb[1], (unsigned long)rgb[2]);
                if (pen >= 0)
                    ReleasePen(wb->ViewPort.ColorMap, pen);
            }
    }
}

/* ObtainBestPenA and FindColor on a map of their own: 16 pens on a 4-plane
 * ViewPort that is never shown. "nearest": every pen shared with the
 * colours below, so ObtainBestPenA must answer one of them; "tolerance":
 * pens 0-7 shared, 8-15 free, and a target moved off pen 5's colour by d in
 * one, two or three guns, answered "s" (pen 5 shared) or "n" (a new pen) */
static ULONG prng = 0x2468ACE1UL;
static ULONG rnd8(void)
{
    prng = prng * 1103515245UL + 12345UL;
    return (prng >> 16) & 0xFF;
}
static void pen_calls(void)
{
    static const LONG precs[] = { PRECISION_EXACT, PRECISION_IMAGE, 1, 2, 3, 4, 6, 8, 12, PRECISION_ICON, 20, 24,
                                  PRECISION_GUI, 48, 64, 127, -2, -5 };
    static const UBYTE far[8][3] = { { 0, 0, 0 }, { 255, 255, 255 }, { 255, 0, 0 }, { 0, 255, 0 },
                                     { 0, 0, 255 }, { 0x80, 0x80, 0x80 }, { 255, 255, 0 }, { 0, 255, 255 } };
    ULONG pal[16][3];
    struct ViewPort vp;
    struct RasInfo ri;
    struct BitMap *bm = AllocBitMap(16, 1, 4, BMF_CLEAR, NULL);
    struct ColorMap *cm;
    int i, j, k, p, d;
    for (i = 0; i < 16; i++)
        for (k = 0; k < 3; k++)
            pal[i][k] = rnd8();
    pal[5][0] = pal[5][1] = pal[5][2] = 0x80;
    for (p = 0; p < 2; p++) {
        cm = GetColorMap(16);
        if (!cm || !bm)
            return;
        InitVPort(&vp);
        memset(&ri, 0, sizeof ri);
        ri.BitMap = bm;
        vp.RasInfo = &ri;
        vp.DWidth = 16;
        vp.DHeight = 1;
        vp.ColorMap = cm;
        if (AttachPalExtra(cm, &vp)) {
            FreeColorMap(cm);
            return;
        }
        for (i = 0; p && i < 8; i++)
            for (k = 0; k < 3; k++)
                pal[i][k] = far[i][k];
        for (i = 0; i < (p ? 8 : 16); i++)
            ObtainPen(cm, i, pal[i][0] * 0x01010101UL, pal[i][1] * 0x01010101UL, pal[i][2] * 0x01010101UL, 0);
        if (!p) {
            for (j = 0; j < 160; j++) {
                ULONG r = rnd8(), g = rnd8(), b = rnd8();
                struct TagItem t[2] = { { OBP_Precision, PRECISION_EXACT }, { TAG_DONE, 0 } };
                LONG pen = ObtainBestPenA(cm, r * 0x01010101UL, g * 0x01010101UL, b * 0x01010101UL, t);
                LONG fc = FindColor(cm, r * 0x01010101UL, g * 0x01010101UL, b * 0x01010101UL, -1);
                printf("pens | nearest %02lx%02lx%02lx | ObtainBestPen %ld FindColor %ld\n", (unsigned long)r,
                       (unsigned long)g, (unsigned long)b, (long)pen, (long)fc);
                if (pen >= 0)
                    ReleasePen(cm, pen);
            }
        } else {
            /* each direction: the guns moved (r, g, b), + or -; the
             * answer is the first d that does not share pen 5 */
            static const signed char dirs[][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { -1, 0, 0 }, { 0, -1, 0 },
                                                   { 0, 0, -1 }, { 1, 1, 0 }, { 1, 0, 1 }, { 0, 1, 1 }, { 1, 1, 1 },
                                                   { 1, -1, 0 }, { -1, -1, -1 }, { 2, 1, 0 }, { 1, 2, 0 }, { 0, 1, 2 } };
            for (i = 0; i < (int)(sizeof precs / sizeof precs[0]); i++)
                for (k = 0; k < (int)(sizeof dirs / sizeof dirs[0]); k++) {
                    int first = -1;
                    char what = '?';
                    for (d = 0; d < 120 && first < 0; d++) {
                        ULONG c[3];
                        int g;
                        struct TagItem t[2] = { { OBP_Precision, (ULONG)precs[i] }, { TAG_DONE, 0 } };
                        LONG pen;
                        for (g = 0; g < 3; g++)
                            c[g] = (ULONG)(0x80 + dirs[k][g] * d) * 0x01010101UL;
                        pen = ObtainBestPenA(cm, c[0], c[1], c[2], t);
                        if (pen != 5) {
                            first = d;
                            what = pen >= 8 ? 'n' : pen < 0 ? '-' : 'o';
                        }
                        if (pen >= 0)
                            ReleasePen(cm, pen);
                    }
                    printf("pens | tolerance precision %ld direction %d,%d,%d | %d %c\n", (long)precs[i], dirs[k][0],
                           dirs[k][1], dirs[k][2], first, what);
                }
            /* below the top byte of a gun */
            {
                struct TagItem t[2] = { { OBP_Precision, PRECISION_EXACT }, { TAG_DONE, 0 } };
                LONG pen = ObtainBestPenA(cm, 0x80FFFFFFUL, 0x80000000UL, 0x80123456UL, t);
                printf("pens | exact with low bits | %s\n", pen == 5 ? "shared" : pen >= 8 ? "new" : "other");
                if (pen >= 0)
                    ReleasePen(cm, pen);
            }
        }
        for (i = 0; i < (p ? 8 : 16); i++)
            ReleasePen(cm, i);
        FreeColorMap(cm);
    }
    WaitBlit();
    FreeBitMap(bm);
}

int main(int argc, char **argv)
{
    struct BitMap *bm8 = NULL, *bm2 = NULL;
    struct RastPort rp8, rp2;
    struct Screen *wb = NULL, *aga = NULL, *rtg = NULL;
    struct Window *w1 = NULL, *w2 = NULL, *a1 = NULL, *a2 = NULL, *r1 = NULL;
    struct target t;
    ULONG aerr = 0, rerr = 0;
    struct Screen *tc = NULL;
    struct Window *t1 = NULL, *t2 = NULL;
    int i;
    ULONG c, n, b;
    for (n = 0; n < 256; n++) {
        c = n;
        for (b = 0; b < 8; b++)
            c = (c & 1) ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
        crctab[n] = c;
    }
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "DUMP"))
            dump = 1;
        else if (!strcmp(argv[i], "ONLY") && i + 1 < argc)
            only = argv[++i];
    }
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 40);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 40);
    LayersBase = OpenLibrary((CONST_STRPTR)"layers.library", 39);
    if (!GfxBase || !IntuitionBase || !LayersBase) {
        printf("# no libraries\n");
        return 20;
    }
    printf("# graphics.library %d.%d\n", GfxBase->LibNode.lib_Version, GfxBase->LibNode.lib_Revision);
    make_sources();
    if (!src8 || !src2 || !tmpl || !mask1 || !img2) {
        printf("# no source bitmaps\n");
        return 20;
    }
    memset(&t, 0, sizeof t);
    wb = LockPubScreen(NULL);
    colour_calls(wb);
    pen_calls();

    bm8 = AllocBitMap(W, H, 8, BMF_CLEAR, NULL);
    bm2 = AllocBitMap(W, H, 2, BMF_CLEAR, NULL);
    if (bm8) {
        InitRastPort(&rp8);
        rp8.BitMap = bm8;
        t.name = "bitmap 8";
        t.rp = &rp8;
        t.show = NULL;
        t.sx = t.sy = 0;
        t.depth = 8;
        run_target(&t);
    }
    if (bm2) {
        InitRastPort(&rp2);
        rp2.BitMap = bm2;
        t.name = "bitmap 2";
        t.rp = &rp2;
        t.show = NULL;
        t.depth = 2;
        run_target(&t);
    }

    aga = OpenScreenTags(NULL, SA_DisplayID, PAL_MONITOR_ID | HIRES_KEY, SA_Width, 640, SA_Height, 256,
                         SA_Depth, 8, SA_Quiet, TRUE, SA_ShowTitle, FALSE, SA_Behind, TRUE, SA_Exclusive, FALSE,
                         SA_Type, CUSTOMSCREEN, SA_ErrorCode, (ULONG)&aerr, TAG_DONE);
    printf("aga screen | opened | %s depth %ld error %lu\n", aga ? "yes" : "no",
           aga ? (long)GetBitMapAttr(aga->RastPort.BitMap, BMA_DEPTH) : 0L, (unsigned long)aerr);
    if (aga) {
        colours("aga screen", aga->ViewPort.ColorMap, 16);
        t.name = "aga screen";
        t.rp = &aga->RastPort;
        t.show = NULL;
        t.depth = 8;
        run_target(&t);
        a1 = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)aga, WA_Left, 100, WA_Top, 50, WA_Width, W, WA_Height, H,
                            WA_Borderless, TRUE, WA_SmartRefresh, TRUE, WA_Activate, FALSE, WA_RMBTrap, TRUE, TAG_DONE);
        a2 = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)aga, WA_Left, 100 + 60, WA_Top, 50 + 25, WA_Width, 50,
                            WA_Height, 30, WA_Borderless, TRUE, WA_SmartRefresh, TRUE, WA_Activate, FALSE, WA_RMBTrap,
                            TRUE, TAG_DONE);
        if (a1 && a2) {
            SetRast(a2->RPort, 3);
            t.name = "aga window";
            t.rp = a1->RPort;
            t.show = &aga->RastPort;
            t.sx = 100;
            t.sy = 50;
            run_target(&t);
        }
    }

    if (wb) {
        ULONG wbid = GetVPModeID(&wb->ViewPort);
        ULONG id = BestModeID(BIDTAG_MonitorID, wbid & MONITOR_ID_MASK, BIDTAG_NominalWidth, 640,
                              BIDTAG_NominalHeight, 480, BIDTAG_Depth, 8, TAG_DONE);
        printf("workbench screen | depth | %ld\n", (long)GetBitMapAttr(wb->RastPort.BitMap, BMA_DEPTH));
        if (id != (ULONG)INVALID_ID && (id & MONITOR_ID_MASK) == (wbid & MONITOR_ID_MASK))
            rtg = OpenScreenTags(NULL, SA_DisplayID, id, SA_Width, 640, SA_Height, 480, SA_Depth, 8, SA_Quiet, TRUE,
                                 SA_ShowTitle, FALSE, SA_Behind, TRUE, SA_Type, CUSTOMSCREEN, SA_ErrorCode,
                                 (ULONG)&rerr, TAG_DONE);
        printf("rtg screen | opened | %s depth %ld id %s error %lu\n", rtg ? "yes" : "no",
               rtg ? (long)GetBitMapAttr(rtg->RastPort.BitMap, BMA_DEPTH) : 0L,
               id == (ULONG)INVALID_ID ? "none" : "found", (unsigned long)rerr);
        if (rtg) {
            t.name = "rtg screen";
            t.rp = &rtg->RastPort;
            t.show = NULL;
            t.depth = 8;
            run_target(&t);
            r1 = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)rtg, WA_Left, 100, WA_Top, 50, WA_Width, W, WA_Height, H,
                                WA_Borderless, TRUE, WA_SmartRefresh, TRUE, WA_Activate, FALSE, WA_RMBTrap, TRUE,
                                TAG_DONE);
            if (r1) {
                t.name = "rtg window";
                t.rp = r1->RPort;
                t.show = &rtg->RastPort;
                t.sx = 100;
                t.sy = 50;
                run_target(&t);
            }
        }
        /* a screen like the Workbench's, with a palette of its own in which
         * every pen has a colour no other pen has */
        tc = OpenScreenTags(NULL, SA_LikeWorkbench, TRUE, SA_Quiet, TRUE, SA_ShowTitle, FALSE, SA_Behind, TRUE,
                            SA_Type, CUSTOMSCREEN, SA_Title, (ULONG)"DrawProbe", TAG_DONE);
        printf("truecolour screen | opened | %s depth %ld\n", tc ? "yes" : "no",
               tc ? (long)GetBitMapAttr(tc->RastPort.BitMap, BMA_DEPTH) : 0L);
        if (tc) {
            ULONG k2, cnt = tc->ViewPort.ColorMap->Count < 256 ? tc->ViewPort.ColorMap->Count : 256;
            for (k2 = 0; k2 < cnt; k2++)
                SetRGB32(&tc->ViewPort, k2, k2 * 0x01010101UL, (k2 ^ 0x5A) * 0x01010101UL,
                         ((k2 * 7 + 3) & 0xFF) * 0x01010101UL);
            printf("truecolour screen | pens | %lu\n", (unsigned long)cnt);
            t.name = "truecolour screen";
            t.rp = &tc->RastPort;
            t.cm = tc->ViewPort.ColorMap;
            t.show = NULL;
            t.depth = GetBitMapAttr(tc->RastPort.BitMap, BMA_DEPTH);
            run_target(&t);
            t1 = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)tc, WA_Left, 100, WA_Top, 50, WA_Width, W, WA_Height, H,
                                WA_Borderless, TRUE, WA_SmartRefresh, TRUE, WA_Activate, FALSE, WA_RMBTrap, TRUE,
                                TAG_DONE);
            t2 = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)tc, WA_Left, 100 + 60, WA_Top, 50 + 25, WA_Width, 50,
                                WA_Height, 30, WA_Borderless, TRUE, WA_SmartRefresh, TRUE, WA_Activate, FALSE,
                                WA_RMBTrap, TRUE, TAG_DONE);
            if (t1 && t2) {
                SetRast(t2->RPort, 3);
                t.name = "truecolour window";
                t.rp = t1->RPort;
                t.show = &tc->RastPort;
                t.sx = 100;
                t.sy = 50;
                run_target(&t);
            }
        }
        t.cm = NULL;
        w1 = OpenWindowTags(NULL, WA_PubScreen, (ULONG)wb, WA_Left, 300, WA_Top, 200, WA_Width, W, WA_Height, H,
                            WA_Borderless, TRUE, WA_SmartRefresh, TRUE, WA_Activate, FALSE, WA_RMBTrap, TRUE, TAG_DONE);
        w2 = OpenWindowTags(NULL, WA_PubScreen, (ULONG)wb, WA_Left, 300 + 60, WA_Top, 200 + 25, WA_Width, 50,
                            WA_Height, 30, WA_Borderless, TRUE, WA_SmartRefresh, TRUE, WA_Activate, FALSE, WA_RMBTrap,
                            TRUE, TAG_DONE);
        if (w1 && w2) {
            SetRast(w2->RPort, 3);
            t.name = "workbench window";
            t.cm = wb->ViewPort.ColorMap;
            t.rp = w1->RPort;
            t.show = &wb->RastPort;
            t.sx = 300;
            t.sy = 200;
            t.depth = GetBitMapAttr(wb->RastPort.BitMap, BMA_DEPTH);
            run_target(&t);
        }
    }

    if (t2) CloseWindow(t2);
    if (t1) CloseWindow(t1);
    if (tc) CloseScreen(tc);
    if (w2) CloseWindow(w2);
    if (w1) CloseWindow(w1);
    if (r1) CloseWindow(r1);
    if (rtg) CloseScreen(rtg);
    if (a2) CloseWindow(a2);
    if (a1) CloseWindow(a1);
    if (aga) CloseScreen(aga);
    if (wb) UnlockPubScreen(NULL, wb);
    WaitBlit();
    if (bm8) FreeBitMap(bm8);
    if (bm2) FreeBitMap(bm2);
    FreeBitMap(src8);
    FreeBitMap(src2);
    FreeMem(tmpl, 8 * 3 * 2);
    FreeMem(mask1, 16 * 3 * 2);
    FreeMem(img2, 2 * 10 * 2 * 2);
    printf("# done\n");
    CloseLibrary(LayersBase);
    CloseLibrary((struct Library *)IntuitionBase);
    CloseLibrary((struct Library *)GfxBase);
    return 0;
}
