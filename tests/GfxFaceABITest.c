/* SPDX-License-Identifier: MIT */
/* GfxFaceABITest: OpenGfx graphics.library's face (library/graphics) side
 * by side with OS 3.2's graphics.library. Copyright (c) 2026 Dalsin
 * Limited. MIT.
 *
 * One script of calls is run through OS 3.2's graphics.library and then
 * through opengfx.library (the same code as OpenGfx's graphics.library
 * under its own name), each writing its answers as lines; then the lines
 * are compared and each one that differs is printed with both answers.
 * The script is the corners the autodocs leave open, each marked "ABI
 * test" in library/graphics: InitRastPort's bytes, AllocBitMap's rows,
 * flags and odd arguments, GetBitMapAttr, a new ColorMap's fields and
 * colours, GetRGB4/GetRGB32 past the end, pen sharing (which pen
 * ObtainPen hands out first, what a precision lets ObtainBestPen share,
 * FindColor's distance), ScalerDiv's rounding and BitMapScale's pixels,
 * Flood (connectivity, the outline pen, a short TmpRas), DrawEllipse in
 * COMPLEMENT and INVERSVID, GetRPAttrs' MaxPen and DrawBounds, and how
 * many pens AttachPalExtra shares for a ViewPort of each mode and depth.
 * With PROBE it prints OS 3.2's lines only, to record them. With SCREENS
 * it also opens a screen of each depth through intuition (the system's
 * graphics.library, not opengfx's) and prints its PaletteExtra, and the
 * Workbench's.
 * ONLY=SHARE runs the pen-sharing lines alone.
 *   GfxFaceABITest [PROBE] [SCREENS] [ONLY=SHARE] [LIB=dir]
 * Answers 0 when nothing differs, 5 when something does. */
#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/rpattr.h>
#include <graphics/view.h>
#include <graphics/modeid.h>
#include <graphics/scale.h>
#include <graphics/gfxmacros.h>   /* SetDrPt and the other RastPort macros */
#include <utility/tagitem.h>
#include <intuition/screens.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>

struct GfxBase *GfxBase;
struct IntuitionBase *IntuitionBase;
static int probe;
static const char *only;   /* ONLY=SHARE: one part of the script */

#define OUTSIZE (256 * 1024)
static char *out_buf;
static ULONG out_len;

static void out(const char *fmt, ...)
{
    va_list ap;
    if (probe) {
        /* recording: each line goes out as it is made, so a crash still
         * shows how far the script got */
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
        fflush(stdout);
        return;
    }
    if (out_len > OUTSIZE - 512) return;
    va_start(ap, fmt);
    out_len += vsprintf(out_buf + out_len, fmt, ap);
    va_end(ap);
}

static void rows(const char *tag, struct RastPort *rp, int w, int h)
{
    int x, y;
    char row[200];
    out("%s %d %d\n", tag, w, h);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w && x < 199; x++) row[x] = "0123456789abcdef"[ReadPixel(rp, x, y) & 15];
        row[x] = 0;
        out("  %s\n", row);
    }
}

/* ---- the script ---------------------------------------------------------- */

static void s_initrp(void)
{
    struct RastPort rp;
    int i;
    memset(&rp, 0x5A, sizeof rp);
    InitRastPort(&rp);
    out("INITRP font=default:%d", rp.Font == GfxBase->DefaultFont);
    for (i = 0; i < (int)sizeof rp; i++)
        if (i < 52 || i >= 56) out(" %d", ((UBYTE *)&rp)[i]);
    out("\n");
}

static void s_bitmaps(void)
{
    static const ULONG cases[][4] = {
        {320, 200, 3, 0}, {321, 17, 2, BMF_CLEAR}, {1, 1, 1, 0}, {640, 256, 4, BMF_DISPLAYABLE},
        {100, 10, 5, BMF_DISPLAYABLE | BMF_CLEAR}, {320, 200, 3, BMF_INTERLEAVED},
        {320, 200, 3, BMF_INTERLEAVED | BMF_DISPLAYABLE}, {33, 5, 1, BMF_INTERLEAVED},
        /* no depth 0: OS 3.2.3's AllocBitMap(16, 16, 0, 0, 0) jumps to an
         * odd address (lab t3-sharable, 14 Oct 2026) */
        {16, 16, 8, 0}, {16, 16, 9, 0}, {0, 16, 2, 0}, {16, 0, 2, 0}, {17, 3, 2, BMF_MINPLANES},
    };
    int i, p;
    for (i = 0; i < (int)(sizeof cases / sizeof cases[0]); i++) {
        struct BitMap *bm = AllocBitMap(cases[i][0], cases[i][1], cases[i][2], cases[i][3], 0);
        out("ALLOCBM %lu %lu %lu %lu:", cases[i][0], cases[i][1], cases[i][2], cases[i][3]);
        if (!bm) { out(" none\n"); continue; }
        out(" bpr=%u rows=%u flags=%u depth=%u", bm->BytesPerRow, bm->Rows, bm->Flags, bm->Depth);
        for (p = 0; p < 8; p++) {
            ULONG d = p && bm->Planes[p] && bm->Planes[0] ? (ULONG)(bm->Planes[p] - bm->Planes[0]) : 0;
            out(" p%d=%s%lu", p, bm->Planes[p] ? "+" : "0", d);
        }
        out(" W=%lu H=%lu D=%lu F=%lu X=%lu chip=%d\n", GetBitMapAttr(bm, BMA_WIDTH), GetBitMapAttr(bm, BMA_HEIGHT),
            GetBitMapAttr(bm, BMA_DEPTH), GetBitMapAttr(bm, BMA_FLAGS), GetBitMapAttr(bm, 16),
            bm->Planes[0] ? (TypeOfMem(bm->Planes[0]) & MEMF_CHIP) != 0 : -1);
        WaitBlit();
        FreeBitMap(bm);
    }
    {
        struct BitMap b;
        memset(&b, 0x5A, sizeof b);
        InitBitMap(&b, 3, 33, 7);
        out("INITBM bpr=%u rows=%u flags=%u depth=%u pad=%u p0kept=%d\n", b.BytesPerRow, b.Rows, b.Flags, b.Depth,
            b.pad, b.Planes[0] == (PLANEPTR)0x5A5A5A5A);
    }
    FreeBitMap(0);
    out("FREEBM0 ok\n");
}

static void s_colormap(void)
{
    struct ColorMap *cm = GetColorMap(32), *z = GetColorMap(0);
    ULONG t[3 * 34];
    int i;
    out("GETCM0 %s\n", z ? "map" : "none");
    if (z) FreeColorMap(z);
    if (!cm) { out("GETCM none\n"); return; }
    out("GETCM flags=%u type=%u count=%u tp=%u sres=%u sresdef=%u aux=%u sbe=%u sbo=%u bp0=%u bp1=%u modeid=%lu"
        " vpe=%d vp=%d pe=%d low=%d\n", cm->Flags, cm->Type, cm->Count, cm->TransparencyPlane, cm->SpriteResolution,
        cm->SpriteResDefault, cm->AuxFlags, cm->SpriteBase_Even, cm->SpriteBase_Odd, cm->Bp_0_base, cm->Bp_1_base,
        cm->VPModeID, cm->cm_vpe != 0, cm->cm_vp != 0, cm->PalExtra != 0, cm->LowColorBits != 0);
    for (i = 0; i < 3 * 34; i++) t[i] = 0x77;
    GetRGB32(cm, 0, 34, t);
    out("CMRGB");
    for (i = 0; i < 3 * 34; i++) out(" %lx", t[i]);
    out("\n");
    out("GETRGB4 %ld %ld %ld %ld\n", GetRGB4(cm, 0), GetRGB4(cm, 31), GetRGB4(cm, 32), GetRGB4(cm, -1));
    SetRGB32CM(cm, 5, 0x12345678, 0x9ABCDEF0, 0xFFFFFFFF);
    SetRGB4CM(cm, 6, 0xA, 0x5, 0x1F);
    GetRGB32(cm, 5, 2, t);
    out("SETRGB %lx %lx %lx %lx %lx %lx %ld %ld\n", t[0], t[1], t[2], t[3], t[4], t[5], GetRGB4(cm, 5), GetRGB4(cm, 6));

    /* pens */
    {
        struct TagItem tg[3];
        LONG a, b, c, d;
        out("PALEXTRA %ld", (LONG)AttachPalExtra(cm, 0));
        out(" %ld", (LONG)AttachPalExtra(cm, 0));
        out(" nfree=%u nshared=%u first=%u sharable=%u\n", cm->PalExtra->pe_NFree, cm->PalExtra->pe_NShared,
            cm->PalExtra->pe_FirstFree, cm->PalExtra->pe_SharableColors);
        a = ObtainPen(cm, -1, 0, 0, 0, 0);
        b = ObtainPen(cm, -1, 0xFF000000, 0, 0, PEN_EXCLUSIVE);
        c = ObtainPen(cm, a, 0, 0, 0, 0);
        d = ObtainPen(cm, a, 0x10000000, 0, 0, 0);
        out("OBTAINPEN %ld %ld %ld %ld %ld %ld\n", a, b, c, d, ObtainPen(cm, b, 0xFF000000, 0, 0, 0),
            ObtainPen(cm, 40, 0, 0, 0, 0));
        tg[1].ti_Tag = OBP_FailIfBad; tg[1].ti_Data = TRUE; tg[2].ti_Tag = TAG_DONE;
        for (i = 0; i < 4; i++) {
            static const LONG precs[] = {PRECISION_EXACT, PRECISION_IMAGE, PRECISION_ICON, PRECISION_GUI};
            static const ULONG near[] = {0x00, 0x04, 0x0A, 0x14, 0x28, 0x40};
            int k;
            tg[0].ti_Tag = OBP_Precision; tg[0].ti_Data = (ULONG)precs[i];
            out("BESTPEN prec=%ld", precs[i]);
            for (k = 0; k < 6; k++) {
                LONG p = ObtainBestPenA(cm, near[k] << 24, near[k] << 24, near[k] << 24, tg);
                out(" %lx:%ld", near[k], p);
                if (p >= 0) ReleasePen(cm, p);
            }
            out("\n");
        }
        out("FINDCOLOR %ld %ld %ld %ld\n", FindColor(cm, 0xFF000000, 0, 0, -1), FindColor(cm, 0x80000000, 0x80000000, 0x80000000, -1),
            FindColor(cm, 0xFF000000, 0xFF000000, 0xFF000000, 3), FindColor(cm, 0x12000000, 0x34000000, 0x56000000, 100));
        ReleasePen(cm, a); ReleasePen(cm, a); ReleasePen(cm, b); ReleasePen(cm, 200);
        out("RELEASED nfree=%u nshared=%u first=%u\n", cm->PalExtra->pe_NFree, cm->PalExtra->pe_NShared,
            cm->PalExtra->pe_FirstFree);
    }
    FreeColorMap(cm);
}

/* How many pens a PalExtra shares (pe_SharableColors), for a ViewPort of
 * each mode and depth, on maps of each size. Each line also says whether
 * the pen numbered pe_SharableColors and the one past it can be had, the
 * pen ObtainPen(-1) hands out after that, and NFree then. */
static void share_case(const char *tag, UWORD modes, int depth, int count, int novp)
{
    struct ViewPort vp;
    struct RasInfo ri;
    struct BitMap bm;
    struct ColorMap *cm = GetColorMap(count);
    struct PaletteExtra *pe = 0;
    LONG first, past, at;
    if (!cm) { out("SHARE %s count=%d nomap\n", tag, count); return; }
    InitVPort(&vp);
    memset(&ri, 0, sizeof ri);
    InitBitMap(&bm, depth, 320, 1);
    ri.BitMap = &bm;
    vp.DWidth = 320; vp.DHeight = 1;
    vp.Modes = modes;
    vp.RasInfo = &ri;
    vp.ColorMap = cm;
    if (AttachPalExtra(cm, novp ? 0 : &vp) || !(pe = cm->PalExtra)) {
        out("SHARE %s count=%d nopalextra\n", tag, count);
        FreeColorMap(cm);
        return;
    }
    out("SHARE %s count=%d sharable=%u nfree=%u nshared=%u first=%u", tag, count, pe->pe_SharableColors,
        pe->pe_NFree, pe->pe_NShared, pe->pe_FirstFree);
    /* only pens inside the map: past its end OS 3.2 may write anywhere */
    at = pe->pe_SharableColors < count ? ObtainPen(cm, pe->pe_SharableColors, 0, 0, 0, PEN_EXCLUSIVE | PEN_NO_SETCOLOR)
                                       : -2;
    past = pe->pe_SharableColors + 1 < count
               ? ObtainPen(cm, (ULONG)pe->pe_SharableColors + 1, 0, 0, 0, PEN_EXCLUSIVE | PEN_NO_SETCOLOR) : -2;
    first = ObtainPen(cm, -1, 0, 0, 0, 0);
    out(" at=%ld past=%ld then=%ld nfree=%u\n", at, past, first, pe->pe_NFree);
    FreeColorMap(cm);
}

static void s_sharable(void)
{
    char tag[24];
    int d;
    share_case("novp", 0, 4, 256, 1);
    share_case("depth0", 0, 0, 256, 0);
    /* a map the depth's size, a map of 32 (GetColorMap's usual) and a map
     * of 256 (SA_FullPalette). Never a map smaller than the depth shows:
     * OS 3.2.3 then writes past it (d6 on a map of 32 jumped to an odd
     * address, lab t3-sharable, 14 Oct 2026). */
    for (d = 1; d <= 8; d++) {
        sprintf(tag, "d%d", d);
        share_case(tag, 0, d, 1 << d, 0);
        if (d < 5) share_case(tag, 0, d, 32, 0);
        if (d != 8) share_case(tag, 0, d, 256, 0);
    }
    share_case("ehb6", EXTRA_HALFBRITE, 6, 64, 0);
    share_case("ehb6", EXTRA_HALFBRITE, 6, 256, 0);
    share_case("ham6", HAM, 6, 64, 0);
    share_case("ham6", HAM, 6, 256, 0);
    share_case("ham8", HAM, 8, 256, 0);
}

static void print_screen_pe(const char *tag, struct Screen *s)
{
    struct ColorMap *cm = s->ViewPort.ColorMap;
    struct PaletteExtra *pe = cm ? cm->PalExtra : 0;
    struct BitMap *bm = s->ViewPort.RasInfo ? s->ViewPort.RasInfo->BitMap : 0;
    printf("SCREEN %s depth=%d bmdepth=%d modes=%x modeid=%lx count=%d", tag, s->BitMap.Depth, bm ? bm->Depth : -1,
           s->ViewPort.Modes, GetVPModeID(&s->ViewPort), cm ? cm->Count : -1);
    if (pe) printf(" sharable=%u nfree=%u nshared=%u first=%u\n", pe->pe_SharableColors, pe->pe_NFree, pe->pe_NShared,
                   pe->pe_FirstFree);
    else printf(" nopalextra\n");
    fflush(stdout);
}

/* Screens of each depth through intuition and the system's graphics, and
 * the Workbench: the numbers an application sees. */
static void s_screens(void)
{
    static const struct { const char *tag; int depth; ULONG id; } sc[] = {
        {"d1", 1, HIRES_KEY}, {"d2", 2, HIRES_KEY}, {"d3", 3, HIRES_KEY}, {"d4", 4, HIRES_KEY},
        {"d5", 5, LORES_KEY}, {"d6", 6, LORES_KEY}, {"d7", 7, LORES_KEY}, {"d8", 8, LORES_KEY},
        {"ehb6", 6, EXTRAHALFBRITE_KEY}, {"ham6", 6, HAM_KEY}, {"ham8", 8, HAM_KEY | LORES_KEY},
    };
    struct Screen *wb;
    char tag[40];
    int i, full;
    if (!(IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39))) {
        printf("SCREEN nointuition\n");
        return;
    }
    if ((wb = LockPubScreen(0))) {
        print_screen_pe("workbench", wb);
        UnlockPubScreen(0, wb);
    }
    for (i = 0; i < (int)(sizeof sc / sizeof sc[0]); i++)
        for (full = 0; full < 2; full++) {
            struct Screen *s;
            sprintf(tag, "%s%s", sc[i].tag, full ? "-full" : "");
            s = OpenScreenTags(0, SA_Depth, sc[i].depth, SA_DisplayID, sc[i].id, SA_Quiet, TRUE, SA_Behind, TRUE,
                               SA_ShowTitle, FALSE, SA_FullPalette, full, TAG_DONE);
            if (!s) { printf("SCREEN %s noscreen\n", tag); continue; }
            print_screen_pe(tag, s);
            CloseScreen(s);
        }
    CloseLibrary((struct Library *)IntuitionBase);
}

static void s_scale(void)
{
    static const UWORD f[][3] = {{10, 3, 2}, {10, 2, 3}, {7, 1, 2}, {5, 1, 2}, {1, 1, 3}, {0, 5, 3}, {100, 7, 9}, {16383, 16383, 1}};
    struct BitMap *s = AllocBitMap(32, 8, 2, BMF_CLEAR, 0), *d = AllocBitMap(64, 24, 2, BMF_CLEAR, 0);
    struct RastPort rs, rd;
    struct BitScaleArgs a;
    int i;
    out("SCALERDIV");
    for (i = 0; i < 8; i++) out(" %u", ScalerDiv(f[i][0], f[i][1], f[i][2]));
    out("\n");
    if (!s || !d) { out("SCALE nomem\n"); goto done; }
    InitRastPort(&rs); rs.BitMap = s;
    InitRastPort(&rd); rd.BitMap = d;
    for (i = 0; i < 32; i++) { SetAPen(&rs, (i * 7 + i / 5) & 3); WritePixel(&rs, i, i & 7); WritePixel(&rs, i, (i * 3) & 7); }
    memset(&a, 0, sizeof a);
    a.bsa_SrcX = 1; a.bsa_SrcY = 1; a.bsa_SrcWidth = 21; a.bsa_SrcHeight = 6;
    a.bsa_XSrcFactor = 3; a.bsa_XDestFactor = 5; a.bsa_YSrcFactor = 2; a.bsa_YDestFactor = 3;
    a.bsa_DestX = 2; a.bsa_DestY = 1; a.bsa_SrcBitMap = s; a.bsa_DestBitMap = d;
    BitMapScale(&a);
    WaitBlit();
    out("BITMAPSCALE dw=%u dh=%u\n", a.bsa_DestWidth, a.bsa_DestHeight);
    rows("SCALED", &rd, 48, 14);
done:
    WaitBlit();
    if (s) FreeBitMap(s);
    if (d) FreeBitMap(d);
}

static void s_flood(void)
{
    struct BitMap *bm = AllocBitMap(48, 20, 3, BMF_CLEAR, 0);
    struct RastPort rp;
    struct TmpRas tr;
    UBYTE *buf = AllocMem(48 / 8 * 20 * 2, MEMF_CHIP | MEMF_CLEAR);
    int k;
    if (!bm || !buf) { out("FLOOD nomem\n"); goto done; }
    for (k = 0; k < 6; k++) {
        LONG rc;
        InitRastPort(&rp);
        rp.BitMap = bm;
        SetRast(&rp, 0);
        /* a box in pen 2 with a gap, a diagonal wall in pen 3, a pen 4 blob */
        SetAPen(&rp, 2); Move(&rp, 4, 2); Draw(&rp, 30, 2); Draw(&rp, 30, 16); Draw(&rp, 4, 16); Draw(&rp, 4, 9);
        SetAPen(&rp, 3); Move(&rp, 10, 4); Draw(&rp, 20, 14);
        SetAPen(&rp, 4); RectFill(&rp, 34, 5, 40, 12);
        WritePixel(&rp, 36, 8);
        InitTmpRas(&tr, buf, k == 5 ? 10 : 48 / 8 * 20 * 2);
        rp.TmpRas = k == 4 ? 0 : &tr;
        SetAPen(&rp, 5 + (k & 1));
        SetOutlinePen(&rp, k < 2 ? 2 : 3);
        if (k == 3) SetDrMd(&rp, COMPLEMENT);
        rc = Flood(&rp, k & 1, k < 2 ? 12 : 22, k < 2 ? 8 : 5);
        WaitBlit();
        out("FLOOD %d rc=%ld\n", k, rc);
        rows("FLOODED", &rp, 48, 20);
    }
done:
    if (buf) FreeMem(buf, 48 / 8 * 20 * 2);
    WaitBlit();
    if (bm) FreeBitMap(bm);
}

static void s_ellipse(void)
{
    struct BitMap *bm = AllocBitMap(64, 32, 3, BMF_CLEAR, 0);
    struct RastPort rp;
    static const UBYTE modes[] = {COMPLEMENT, COMPLEMENT | JAM2, INVERSVID, INVERSVID | JAM2, JAM1};
    int k;
    if (!bm) { out("ELLIPSE nomem\n"); return; }
    for (k = 0; k < 5; k++) {
        InitRastPort(&rp);
        rp.BitMap = bm;
        SetRast(&rp, 1);
        SetAPen(&rp, 6); SetBPen(&rp, 3);
        SetDrMd(&rp, modes[k]);
        if (k == 4) SetDrPt(&rp, 0xF0F0);
        DrawEllipse(&rp, 30, 15, 20 - k, 9);
        DrawEllipse(&rp, 2, 3, 6, 5);
        WaitBlit();
        out("ELLIPSE %d\n", modes[k]);
        rows("ELLIPSED", &rp, 64, 32);
    }
    WaitBlit();
    FreeBitMap(bm);
}

static void s_rpattrs(void)
{
    struct RastPort rp;
    struct BitMap *bm = AllocBitMap(40, 10, 3, 0, 0);
    ULONG mp = 0x77, ap = 0x77, unk = 0x77;
    struct Rectangle r = {7, 7, 7, 7};
    struct TagItem t[5];
    InitRastPort(&rp);
    rp.BitMap = bm;
    SetMaxPen(&rp, 5);
    t[0].ti_Tag = RPTAG_MaxPen; t[0].ti_Data = (ULONG)&mp;
    t[1].ti_Tag = RPTAG_APen; t[1].ti_Data = (ULONG)&ap;
    t[2].ti_Tag = RPTAG_DrawBounds; t[2].ti_Data = (ULONG)&r;
    t[3].ti_Tag = TAG_USER + 99; t[3].ti_Data = (ULONG)&unk;
    t[4].ti_Tag = TAG_DONE;
    GetRPAttrsA(&rp, t);
    out("RPATTRS maxpen=%lx apen=%lx bounds=%d,%d,%d,%d unknown=%lx mask=%u\n", mp, ap, r.MinX, r.MinY, r.MaxX, r.MaxY,
        unk, rp.Mask);
    out("OUTLINE %lu flags=%u wm=%lu\n", (ULONG)SetOutlinePen(&rp, 9), rp.Flags, (ULONG)SetWriteMask(&rp, 0x0F));
    WaitBlit();
    if (bm) FreeBitMap(bm);
}

static void run(void)
{
    out_len = 0;
    if (only) {
        if (!strcmp(only, "SHARE")) s_sharable();
        return;
    }
    s_initrp();
    s_bitmaps();
    s_colormap();
    s_sharable();
    s_scale();
    s_flood();
    s_ellipse();
    s_rpattrs();
}

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
    struct GfxBase *os_base = 0, *our_base = 0;
    int i, rc = 0, screens = 0;
    ULONG os_len = 0, diffs = 0, lines = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "PROBE")) probe = 1;
        else if (!strcmp(argv[i], "SCREENS")) screens = 1;
        else if (!strncmp(argv[i], "ONLY=", 5)) only = argv[i] + 5;
        else if (!strncmp(argv[i], "LIB=", 4)) dir = argv[i] + 4;
    }
    out_buf = AllocMem(OUTSIZE, MEMF_PUBLIC);
    os_out = AllocMem(OUTSIZE, MEMF_PUBLIC);
    if (!out_buf || !os_out) { printf("no memory\n"); rc = 20; goto done; }
    if (!(os_base = (struct GfxBase *)OpenLibrary("graphics.library", 39))) { printf("no graphics.library\n"); rc = 20; goto done; }
    if (!probe) {
        strcpy(path, dir ? dir : "LIBS:");
        AddPart(path, "opengfx.library", sizeof path);
        if (!(our_base = (struct GfxBase *)OpenLibrary(path, 47))) { printf("no %s\n", path); rc = 20; goto done; }
    }
    printf("OS: %s", (char *)os_base->LibNode.lib_IdString);
    /* the layout both share (PROBE: the NDK's own) */
    printf("GfxBase: %lu bytes; ActiView %lu TextFonts %lu DisplayFlags %lu ChipRevBits0 %lu MonitorList %lu"
           " UtilBase %lu ExecBase %lu gb_LayersBase %lu HWEmul %lu; PosSize %u\n",
           (ULONG)sizeof(struct GfxBase), (ULONG)offsetof(struct GfxBase, ActiView),
           (ULONG)offsetof(struct GfxBase, TextFonts), (ULONG)offsetof(struct GfxBase, DisplayFlags),
           (ULONG)offsetof(struct GfxBase, ChipRevBits0), (ULONG)offsetof(struct GfxBase, MonitorList),
           (ULONG)offsetof(struct GfxBase, UtilBase), (ULONG)offsetof(struct GfxBase, ExecBase),
           (ULONG)offsetof(struct GfxBase, gb_LayersBase), (ULONG)offsetof(struct GfxBase, HWEmul),
           os_base->LibNode.lib_PosSize);
    printf("ChipRevBits0 %u DisplayFlags %u MemType %u Modes %u\n", os_base->ChipRevBits0, os_base->DisplayFlags,
           os_base->MemType, os_base->Modes);
    if (our_base) printf("ours: %s", (char *)our_base->LibNode.lib_IdString);

    GfxBase = os_base;
    if (screens) s_screens();
    run();
    if (!our_base) {
        printf("OS 3.2's answers: done\n");
        goto done;
    }
    memcpy(os_out, out_buf, out_len);
    os_len = out_len;
    GfxBase = our_base;
    run();
    GfxBase = os_base;
    {
        ULONG a = 0, b = 0, na, nb;
        while (a < os_len || b < out_len) {
            ULONG la = line(os_out, os_len, a, &na), lb = line(out_buf, out_len, b, &nb);
            lines++;
            if (la != lb || memcmp(os_out + a, out_buf + b, la))
                if (diffs++ < 400) printf("OS:   %.*s\nours: %.*s\n", (int)la, os_out + a, (int)lb, out_buf + b);
            a = na;
            b = nb;
        }
    }
    printf("%lu lines, %lu differ\n", (unsigned long)lines, (unsigned long)diffs);
    if (diffs) rc = 5;
done:
    GfxBase = os_base;
    if (our_base) CloseLibrary((struct Library *)our_base);
    if (os_base) CloseLibrary((struct Library *)os_base);
    if (os_out) FreeMem(os_out, OUTSIZE);
    if (out_buf) FreeMem(out_buf, OUTSIZE);
    return rc;
}
