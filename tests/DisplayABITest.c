/* SPDX-License-Identifier: MIT */
/* DisplayABITest: OpenGfx graphics.library's display (library/display)
 * side by side with OS 3.2's graphics.library. Copyright (c) 2026 Dalsin
 * Limited. MIT.
 *
 * One script of calls is run through OS 3.2's graphics.library and then
 * through opengfx.library (OpenGfx's graphics.library under its own
 * name), each writing its answers as lines; then the lines are compared
 * and each one that differs is printed with both answers. The script is
 * the corners the autodocs leave open, each marked "ABI test" in
 * library/display: InitView's and InitVPort's bytes, GfxNew's node, the
 * display database (every ID NextDisplayInfo walks, with its
 * NotAvailable, properties, nominal size, depth and name), BestModeIDA,
 * CoerceMode, ModeNotAvailable of an unknown ID, VideoControl's unknown
 * tag and _GET tags, GetVPModeID of an old ViewPort, the monitors,
 * MakeVPort's answers, CalcIVG, UCopperListInit twice, sprites' control
 * words, AllocSpriteDataA's defaults, AllocDBufInfo. Lines starting COP
 * show the registers each library's copper list leaves for a ViewPort
 * (DIWSTRT, DDFSTRT, BPLCON1, the modulos, plane 1's offset in the
 * bitmap): the lists themselves are private and differ, what they show
 * must not. Nothing is loaded into the display: no LoadView; opened as
 * opengfx.library the library leaves the custom chips alone. Boot with
 * DEVS:Monitors empty: OS 3.2 adds each driver's modes to its database,
 * OpenGfx has the built-in PAL and NTSC ones only.
 * With PROBE it prints OS 3.2's lines only, to record them.
 *   DisplayABITest [PROBE] [LIB=dir]
 * Answers 0 when nothing differs, 5 when something does. */
#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/gfxnodes.h>
#include <graphics/view.h>
#include <graphics/copper.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <graphics/monitor.h>
#include <graphics/sprite.h>
#include <graphics/videocontrol.h>
#include <graphics/coerce.h>
#include <utility/tagitem.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>

struct GfxBase *GfxBase;
static int probe;

#define OUTSIZE (256 * 1024)
static char *out_buf;
static ULONG out_len;

static void out(const char *fmt, ...)
{
    va_list ap;
    if (out_len > OUTSIZE - 512) return;
    va_start(ap, fmt);
    out_len += vsprintf(out_buf + out_len, fmt, ap);
    va_end(ap);
}

static void bytes(const char *tag, const void *p, int n)
{
    int i;
    out("%s", tag);
    for (i = 0; i < n; i++) out(" %u", ((const UBYTE *)p)[i]);
    out("\n");
}

/* ---- the script ---------------------------------------------------------- */

static void s_init(void)
{
    struct View v;
    struct ViewPort vp;
    memset(&v, 0x5A, sizeof v);
    memset(&vp, 0x5A, sizeof vp);
    InitView(&v);
    InitVPort(&vp);
    bytes("INITVIEW", &v, sizeof v);
    bytes("INITVPORT", &vp, sizeof vp);
    out("SIZES View %lu ViewPort %lu ViewPortExtra %lu ViewExtra %lu CopList %lu MonitorSpec %lu DBufInfo %lu"
        " DisplayInfo %lu DimensionInfo %lu MonitorInfo %lu NameInfo %lu ExtSprite %lu\n",
        (ULONG)sizeof(struct View), (ULONG)sizeof(struct ViewPort), (ULONG)sizeof(struct ViewPortExtra),
        (ULONG)sizeof(struct ViewExtra), (ULONG)sizeof(struct CopList), (ULONG)sizeof(struct MonitorSpec),
        (ULONG)sizeof(struct DBufInfo), (ULONG)sizeof(struct DisplayInfo), (ULONG)sizeof(struct DimensionInfo),
        (ULONG)sizeof(struct MonitorInfo), (ULONG)sizeof(struct NameInfo), (ULONG)sizeof(struct ExtSprite));
}

static void s_gfxnew(void)
{
    ULONG t;
    for (t = VIEW_EXTRA_TYPE; t <= MONITOR_SPEC_TYPE; t++) {
        struct ExtendedNode *n = GfxNew(t);
        if (!n) { out("GFXNEW %lu none\n", t); continue; }
        out("GFXNEW %lu type %u sub %u subtype %u lib=base:%d init:%d\n", t, n->xln_Type, n->xln_Subsystem,
            n->xln_Subtype, n->xln_Library == (LONG)GfxBase, n->xln_Init != 0);
        if (t == MONITOR_SPEC_TYPE) {
            struct MonitorSpec *ms = (struct MonitorSpec *)n;
            out("GFXNEW MonitorSpec ratio %ld %ld list:%d\n", ms->ratioh, ms->ratiov,
                ms->DisplayInfoDataBase.lh_Head == (struct Node *)&ms->DisplayInfoDataBase.lh_Tail);
        }
        GfxFree(n);
    }
}

static void s_database(void)
{
    ULONG id = INVALID_ID, n = 0;
    struct DisplayInfo di;
    struct DimensionInfo dims;
    struct NameInfo ni;
    while ((id = NextDisplayInfo(id)) != INVALID_ID && n < 300) {
        DisplayInfoHandle h = FindDisplayInfo(id);
        ULONG a, b, c;
        memset(&di, 0, sizeof di);
        memset(&dims, 0, sizeof dims);
        memset(&ni, 0, sizeof ni);
        a = GetDisplayInfoData(h, (UBYTE *)&di, sizeof di, DTAG_DISP, 0);
        b = GetDisplayInfoData(h, (UBYTE *)&dims, sizeof dims, DTAG_DIMS, 0);
        c = GetDisplayInfoData(h, (UBYTE *)&ni, sizeof ni, DTAG_NAME, 0);
        out("MODE %08lx %lu %lu %lu na=%u flags=%08lx res=%d,%d speed=%u spr=%u range=%u bits=%u,%u,%u"
            " depth=%u nominal=%d,%d,%d,%d max=%d,%d,%d,%d std=%d,%d,%d,%d raster=%u,%u name=%s\n",
            id, a, b, c, di.NotAvailable, di.PropertyFlags, di.Resolution.x, di.Resolution.y, di.PixelSpeed,
            di.NumStdSprites, di.PaletteRange, di.RedBits, di.GreenBits, di.BlueBits, dims.MaxDepth,
            dims.Nominal.MinX, dims.Nominal.MinY, dims.Nominal.MaxX, dims.Nominal.MaxY, dims.MaxOScan.MinX,
            dims.MaxOScan.MinY, dims.MaxOScan.MaxX, dims.MaxOScan.MaxY, dims.StdOScan.MinX, dims.StdOScan.MinY,
            dims.StdOScan.MaxX, dims.StdOScan.MaxY, dims.MaxRasterWidth, dims.MaxRasterHeight,
            c ? (char *)ni.Name : "-");
        n++;
    }
    out("MODES %lu\n", n);
    {
        struct MonitorInfo mi;
        ULONG got;
        memset(&mi, 0, sizeof mi);
        got = GetDisplayInfoData(0, (UBYTE *)&mi, sizeof mi, DTAG_MNTR, PAL_MONITOR_ID | HIRES_KEY);
        out("MNTR %lu rows=%u cclk=%u minrow=%u compat=%d pos=%d,%d res=%d,%d range=%d,%d,%d,%d ticks=%d,%d"
            " default=%d,%d preferred=%08lx\n", got, mi.TotalRows, mi.TotalColorClocks, mi.MinRow,
            mi.Compatibility, mi.ViewPosition.x, mi.ViewPosition.y, mi.ViewResolution.x, mi.ViewResolution.y,
            mi.ViewPositionRange.MinX, mi.ViewPositionRange.MinY, mi.ViewPositionRange.MaxX,
            mi.ViewPositionRange.MaxY, mi.MouseTicks.x, mi.MouseTicks.y, mi.DefaultViewPosition.x,
            mi.DefaultViewPosition.y, mi.PreferredModeID);
    }
    {
        UBYTE buf[64];
        out("VEC %lu\n", GetDisplayInfoData(0, buf, sizeof buf, DTAG_VEC, HIRES_KEY));
        out("SHORT %lu\n", GetDisplayInfoData(0, buf, 20, DTAG_DISP, HIRES_KEY));
        out("UNKNOWN %lu\n", GetDisplayInfoData(0, buf, sizeof buf, DTAG_DISP, 0x8001));
    }
    out("NOTAVAIL %lx %lx %lx %lx\n", ModeNotAvailable(0x12345678), ModeNotAvailable(PAL_MONITOR_ID | 0x8001),
        ModeNotAvailable(SUPER_KEY), ModeNotAvailable(PAL_MONITOR_ID | 0x8800));
    out("NEXT unknown %08lx\n", NextDisplayInfo(0x00000001));
}

static ULONG best(ULONG tag1, ULONG data1, ULONG tag2, ULONG data2, ULONG tag3, ULONG data3)
{
    struct TagItem t[4];
    t[0].ti_Tag = tag1; t[0].ti_Data = data1;
    t[1].ti_Tag = tag2; t[1].ti_Data = data2;
    t[2].ti_Tag = tag3; t[2].ti_Data = data3;
    t[3].ti_Tag = TAG_DONE;
    return BestModeIDA(t);
}

static void s_best(void)
{
    out("BEST none %08lx\n", best(TAG_IGNORE, 0, TAG_IGNORE, 0, TAG_IGNORE, 0));
    out("BEST depth8 %08lx\n", best(BIDTAG_Depth, 8, TAG_IGNORE, 0, TAG_IGNORE, 0));
    out("BEST 640x400 %08lx\n", best(BIDTAG_NominalWidth, 640, BIDTAG_NominalHeight, 400, TAG_IGNORE, 0));
    out("BEST 640x512 %08lx\n", best(BIDTAG_NominalWidth, 640, BIDTAG_NominalHeight, 512, TAG_IGNORE, 0));
    out("BEST ham %08lx\n", best(BIDTAG_DIPFMustHave, DIPF_IS_HAM, BIDTAG_NominalWidth, 320, BIDTAG_NominalHeight, 200));
    out("BEST pal %08lx\n", best(BIDTAG_MonitorID, PAL_MONITOR_ID, BIDTAG_NominalWidth, 640, TAG_IGNORE, 0));
    out("BEST source %08lx\n", best(BIDTAG_SourceID, PAL_MONITOR_ID | HAM_KEY, TAG_IGNORE, 0, TAG_IGNORE, 0));
    out("BEST desired %08lx\n", best(BIDTAG_DesiredWidth, 700, BIDTAG_DesiredHeight, 300, TAG_IGNORE, 0));
    out("BEST 12bits %08lx\n", best(BIDTAG_RedBits, 8, TAG_IGNORE, 0, TAG_IGNORE, 0));
}

static void s_monitors(void)
{
    struct MonitorSpec *ms = OpenMonitor(0, 0), *pal = OpenMonitor("pal.monitor", 0), *bad = OpenMonitor("nonesuch.monitor", 0);
    if (ms)
        out("MON default %s rows=%u cclk=%u beamcon0=%04x minrow=%u ratio=%ld,%ld legal=%d,%d,%d,%d max=%u min=%u"
            " xoff=%u\n", ms->ms_Node.xln_Name ? ms->ms_Node.xln_Name : "-", ms->total_rows,
            ms->total_colorclocks, ms->BeamCon0, ms->min_row, ms->ratioh, ms->ratiov, ms->ms_LegalView.MinX,
            ms->ms_LegalView.MinY, ms->ms_LegalView.MaxX, ms->ms_LegalView.MaxY, ms->DeniseMaxDisplayColumn,
            ms->DeniseMinDisplayColumn, ms->ms_xoffset);
    out("MON pal %d rows=%u minrow=%u none=%d\n", pal != 0, pal ? pal->total_rows : 0, pal ? pal->min_row : 0, bad != 0);
    out("MON close %ld %ld\n", ms ? CloseMonitor(ms) : -1, pal ? CloseMonitor(pal) : -1);
    out("MON close null %ld\n", CloseMonitor(0));
    out("GFXBASE rows=%u cols=%u maxrow=%u maxcol=%u mincol=%u dpm=%u,%u micros=%u sprw=%u defsprw=%u\n",
        GfxBase->NormalDisplayRows, GfxBase->NormalDisplayColumns, GfxBase->MaxDisplayRow,
        GfxBase->MaxDisplayColumn, GfxBase->MinDisplayColumn, GfxBase->NormalDPMX, GfxBase->NormalDPMY,
        GfxBase->MicrosPerLine, GfxBase->SpriteWidth, GfxBase->DefaultSpriteWidth);
}

/* A ViewPort the V39 way (RKM): a ColorMap with a ViewPortExtra and its
 * DisplayInfo attached. */
struct vpset {
    struct View view;
    struct ViewPort vp;
    struct RasInfo ri;
    struct BitMap *bm;
    struct ViewPortExtra *vpe;
};

static int make(struct vpset *s, ULONG modeid, UWORD modes, int w, int h, int depth, int rx)
{
    struct TagItem vc[4];
    InitView(&s->view);
    InitVPort(&s->vp);
    s->view.ViewPort = &s->vp;
    s->view.Modes = modes & LACE;
    memset(&s->ri, 0, sizeof s->ri);
    s->bm = AllocBitMap(w + 64, h, depth, BMF_DISPLAYABLE | BMF_CLEAR, 0);
    s->vpe = (struct ViewPortExtra *)GfxNew(VIEWPORT_EXTRA_TYPE);
    if (!s->bm || !s->vpe) return 0;
    s->ri.BitMap = s->bm;
    s->ri.RxOffset = rx;
    s->vp.RasInfo = &s->ri;
    s->vp.DWidth = w;
    s->vp.DHeight = h;
    s->vp.Modes = modes;
    s->vp.ColorMap = GetColorMap(32);
    if (!s->vp.ColorMap) return 0;
    vc[0].ti_Tag = VTAG_ATTACH_CM_SET; vc[0].ti_Data = (ULONG)&s->vp;
    vc[1].ti_Tag = VTAG_VIEWPORTEXTRA_SET; vc[1].ti_Data = (ULONG)s->vpe;
    vc[2].ti_Tag = VTAG_NORMAL_DISP_SET; vc[2].ti_Data = (ULONG)FindDisplayInfo(modeid);
    vc[3].ti_Tag = TAG_DONE;
    VideoControl(s->vp.ColorMap, vc);
    s->vpe->DisplayClip.MinX = 0;
    s->vpe->DisplayClip.MinY = 0;
    s->vpe->DisplayClip.MaxX = w - 1;
    s->vpe->DisplayClip.MaxY = h - 1;
    return 1;
}

static void unmake(struct vpset *s)
{
    FreeVPortCopLists(&s->vp);
    if (s->view.LOFCprList) FreeCprList(s->view.LOFCprList);
    if (s->view.SHFCprList) FreeCprList(s->view.SHFCprList);
    if (s->vpe) GfxFree((struct ExtendedNode *)s->vpe);
    if (s->vp.ColorMap) FreeColorMap(s->vp.ColorMap);
    if (s->bm) FreeBitMap(s->bm);
}

/* the registers a hardware list leaves set, and where its first WAIT is */
static void coplines(const char *tag, struct vpset *s, struct cprlist *c)
{
    static const UWORD regs[] = { 0x08E, 0x090, 0x092, 0x094, 0x100, 0x102, 0x104, 0x106, 0x108, 0x10A, 0x10C, 0x180, 0x182 };
    ULONG val[13], pt = 0, i, k;
    int firstwait = -1;
    for (k = 0; k < 13; k++) val[k] = 0xFFFFFFFF;
    for (i = 0; c && i < (ULONG)c->MaxCount; i++) {
        UWORD a = c->start[2 * i], b = c->start[2 * i + 1];
        if (a == 0xFFFF && b == 0xFFFE) break;
        if (a & 1) {
            if (firstwait < 0) firstwait = a >> 8;
            continue;
        }
        if (a == 0x0E0) pt = (pt & 0xFFFF) | (ULONG)b << 16;
        if (a == 0x0E2) pt = (pt & 0xFFFF0000) | b;
        for (k = 0; k < 13; k++)
            if (regs[k] == (a & 0x1FE)) val[k] = b;
    }
    out("COP %s firstwait=%d", tag, firstwait);
    for (k = 0; k < 13; k++) out(" %03x=%04lx", regs[k], val[k]);
    out(" pt1=%ld\n", (LONG)(pt - (ULONG)s->bm->Planes[0]));
}

static void s_views(void)
{
    static const struct { ULONG id; UWORD modes; int w, h, depth, rx; } c[] = {
        { LORES_KEY, 0, 320, 200, 4, 0 },
        { LORES_KEY, 0, 320, 200, 4, 5 },
        { HIRES_KEY, HIRES, 640, 200, 4, 3 },
        { HIRESLACE_KEY, HIRES | LACE, 640, 400, 2, 0 },
        { LORES_KEY, 0, 384, 283, 5, 0 },
        { HAM_KEY, HAM, 320, 200, 6, 0 },
        { LORESDPF_KEY, DUALPF, 320, 200, 2, 0 },
    };
    int i;
    for (i = 0; i < (int)(sizeof c / sizeof c[0]); i++) {
        struct vpset s;
        char tag[32];
        memset(&s, 0, sizeof s);
        if (!make(&s, c[i].id, c[i].modes, c[i].w, c[i].h, c[i].depth, c[i].rx)) { out("VIEW %d no memory\n", i); unmake(&s); continue; }
        out("VIEW %d make=%lu", i, (ULONG)MakeVPort(&s.view, &s.vp));
        out(" mrg=%lu ivg=%u modeid=%08lx\n", (ULONG)MrgCop(&s.view), CalcIVG(&s.view, &s.vp), GetVPModeID(&s.vp));
        sprintf(tag, "%d lof", i);
        coplines(tag, &s, s.view.LOFCprList);
        if (s.view.SHFCprList) {
            sprintf(tag, "%d shf", i);
            coplines(tag, &s, s.view.SHFCprList);
        }
        if (i == 0) {
            s.ri.RxOffset = 17;
            s.ri.RyOffset = 3;
            ScrollVPort(&s.vp);
            coplines("0 scrolled", &s, s.view.LOFCprList);
            s.vp.DyOffset = 400;
            out("VIEW offbottom make=%lu\n", (ULONG)MakeVPort(&s.view, &s.vp));
            s.vp.DyOffset = 0;
            s.vp.Modes |= VP_HIDE;
            out("VIEW hidden make=%lu dspins=%d mrg=%lu\n", (ULONG)MakeVPort(&s.view, &s.vp), s.vp.DspIns != 0,
                (ULONG)MrgCop(&s.view));
            s.vp.Modes &= ~VP_HIDE;
        }
        unmake(&s);
    }
    {
        struct View v;
        InitView(&v);
        out("VIEW empty mrg=%lu lof=%d\n", (ULONG)MrgCop(&v), v.LOFCprList != 0);
        if (v.LOFCprList) FreeCprList(v.LOFCprList);
    }
}

static void s_videocontrol(void)
{
    struct ColorMap *cm = GetColorMap(32);
    struct ViewPort vp;
    struct TagItem t[6];
    if (!cm) return;
    InitVPort(&vp);
    vp.Modes = HIRES | LACE;
    out("VPMODEID old %08lx\n", GetVPModeID(&vp));
    vp.ColorMap = cm;
    out("VPMODEID cm %08lx coerce %08lx %08lx\n", GetVPModeID(&vp), CoerceMode(&vp, PAL_MONITOR_ID, 0),
        CoerceMode(&vp, PAL_MONITOR_ID, AVOID_FLICKER));
    t[0].ti_Tag = 0x80001234; t[0].ti_Data = 0;
    t[1].ti_Tag = TAG_DONE;
    out("VC unknown %lu\n", VideoControl(cm, t));
    t[0].ti_Tag = VTAG_CHROMAKEY_GET; t[0].ti_Data = 0;
    t[1].ti_Tag = VTAG_BORDERBLANK_GET; t[1].ti_Data = 0;
    t[2].ti_Tag = VTAG_VPMODEID_GET; t[2].ti_Data = 0;
    t[3].ti_Tag = VTAG_SPRITERESN_GET; t[3].ti_Data = 0;
    t[4].ti_Tag = VTAG_PF2_BASE_GET; t[4].ti_Data = 0;
    t[5].ti_Tag = TAG_DONE;
    out("VC get %lu", VideoControl(cm, t));
    out(" %08lx %08lx %08lx/%08lx %08lx/%ld %08lx/%lu\n", t[0].ti_Tag, t[1].ti_Tag, t[2].ti_Tag, t[2].ti_Data,
        t[3].ti_Tag, (LONG)t[3].ti_Data, t[4].ti_Tag, t[4].ti_Data);
    out("CM flags=%u type=%u count=%u sprres=%u def=%u vpmodeid=%08lx bases=%u,%u,%u,%u\n", cm->Flags, cm->Type,
        cm->Count, cm->SpriteResolution, cm->SpriteResDefault, cm->VPModeID, cm->SpriteBase_Even,
        cm->SpriteBase_Odd, cm->Bp_0_base, cm->Bp_1_base);
    FreeColorMap(cm);
}

static void s_copper(void)
{
    struct UCopList *u = AllocMem(sizeof *u, MEMF_PUBLIC | MEMF_CLEAR);
    struct CopList *a, *b;
    int i;
    if (!u) return;
    a = UCopperListInit(u, 4);
    b = UCopperListInit(u, 4);
    out("UCOP init %d %d same=%d max=%d\n", a != 0, b != 0, a == b, a ? a->MaxCount : -1);
    for (i = 0; i < 6; i++) {
        CWait(u, i, 0);
        CBump(u);
    }
    out("UCOP bumped first=%d count=%d next=%d cur=%d\n", u->FirstCopList != 0,
        u->FirstCopList ? u->FirstCopList->Count : -1, u->FirstCopList && u->FirstCopList->Next != 0,
        u->CopList ? u->CopList->Count : -1);
    {
        struct ViewPort vp;
        InitVPort(&vp);
        vp.UCopIns = u;
        FreeVPortCopLists(&vp);
        out("UCOP freed %d\n", vp.UCopIns != 0);
    }
}

static void s_sprites(void)
{
    static UWORD data[2 + 2 * 8 + 2];
    struct SimpleSprite s;
    struct ExtSprite *es;
    struct TagItem t[2];
    WORD n;
    struct BitMap *bm = AllocBitMap(16, 8, 2, BMF_CLEAR, 0);
    memset(&s, 0, sizeof s);
    s.height = 8;
    s.posctldata = data;
    /* sprite 2: Intuition holds 0 in the OS's GfxBase, not in ours. The
     * View under a NULL ViewPort is each library's ActiView, so the line
     * shows where (101,37) lands against (0,0), not the raw words. */
    n = GetSprite(&s, 2);
    out("SPRITE get %d num=%d reserved2=%d\n", n, s.num, (GfxBase->SpriteReserved & 4) != 0);
    if (n >= 0) {
        int h0, v0, e0, h1, v1, e1;
        MoveSprite(0, &s, 0, 0);
        h0 = (data[0] & 0xFF) << 1 | (data[1] & 1);
        v0 = data[0] >> 8 | (data[1] & 4) << 6;
        e0 = data[1] >> 8 | (data[1] & 2) << 7;
        MoveSprite(0, &s, 101, 37);
        h1 = (data[0] & 0xFF) << 1 | (data[1] & 1);
        v1 = data[0] >> 8 | (data[1] & 4) << 6;
        e1 = data[1] >> 8 | (data[1] & 2) << 7;
        out("SPRITE moved dh=%d dv=%d height=%d,%d x=%u y=%u attach=%u\n", h1 - h0, v1 - v0, e0 - v0, e1 - v1,
            s.x, s.y, data[1] & 0x80);
        FreeSprite(n);
        out("SPRITE freed reserved2=%d\n", (GfxBase->SpriteReserved & 4) != 0);
    }
    if (bm) {
        t[0].ti_Tag = TAG_DONE;
        es = AllocSpriteDataA(bm, t);
        if (es) {
            out("SPRDATA num=%d height=%u x=%u y=%u ww=%u flags=%u posctl=%04x %04x aligned=%d\n",
                es->es_SimpleSprite.num, es->es_SimpleSprite.height, es->es_SimpleSprite.x,
                es->es_SimpleSprite.y, es->es_wordwidth, es->es_flags, es->es_SimpleSprite.posctldata[0],
                es->es_SimpleSprite.posctldata[1], ((ULONG)es->es_SimpleSprite.posctldata & 7) == 0);
            FreeSpriteData(es);
        }
        t[0].ti_Tag = SPRITEA_Width; t[0].ti_Data = 32;
        t[1].ti_Tag = TAG_DONE;
        es = AllocSpriteDataA(bm, t);
        out("SPRDATA 32 %d ww=%u\n", es != 0, es ? es->es_wordwidth : 0);
        if (es) FreeSpriteData(es);
        FreeBitMap(bm);
    }
}

static void s_misc(void)
{
    struct ViewPort vp;
    struct DBufInfo *d;
    LONG b = VBeamPos();
    InitVPort(&vp);
    d = AllocDBufInfo(&vp);
    out("BEAM in range %d\n", b >= 0 && b < 1024);
    if (d) {
        out("DBUF match=vp:%d userdata=%lu,%lu links=%d,%d\n", d->dbi_MatchLong == (ULONG)&vp,
            (ULONG)d->dbi_UserData1, (ULONG)d->dbi_UserData2, d->dbi_Link1 != 0, d->dbi_Link2 != 0);
        FreeDBufInfo(d);
    }
    FreeDBufInfo(0);
}

static void run(void)
{
    out_len = 0;
    s_init();
    s_gfxnew();
    s_database();
    s_best();
    s_monitors();
    s_views();
    s_videocontrol();
    s_copper();
    s_sprites();
    s_misc();
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
    int i, rc = 0;
    ULONG os_len = 0, diffs = 0, lines = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "PROBE")) probe = 1;
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
    printf("ChipRevBits0 %u DisplayFlags %u copinit %lu\n", os_base->ChipRevBits0, os_base->DisplayFlags,
           (ULONG)sizeof(struct copinit));
    if (our_base) printf("ours: %s", (char *)our_base->LibNode.lib_IdString);

    GfxBase = os_base;
    run();
    if (!our_base) {
        fwrite(out_buf, 1, out_len, stdout);
        printf("OS 3.2's answers: %lu bytes\n", (unsigned long)out_len);
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
