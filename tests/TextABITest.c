/* TextABITest: OpenGfx's text calls (library/text) side by side with OS
 * 3.2's graphics.library. Copyright (c) 2026 Dalsin Limited. MIT.
 *
 * Opens OS 3.2's graphics.library and OpenGfx's built under its own name
 * (opengfx.library, library/graphics/build-amiga.sh) and makes each call
 * through both, switching GfxBase. Fonts made here (one-colour, proportional,
 * kerned, colour and right to left) are handed to both, so the pixels must
 * match; each library's own topaz is compared by its measures only (ours
 * is redrawn). Compared: Text (every plane byte of two Chip RAM bitmaps and
 * the pen), TextLength, TextExtent, TextFit (forward and back, with and
 * without a constraining extent), FontExtent, AskSoftStyle,
 * SetSoftStyle, AskFont, SetFont's RastPort fields, OpenFont's choice,
 * WeighTAMatch, ClearEOL and ClearScreen. A DIFF line shows both answers.
 *
 *   TextABITest [ROUNDS=n] [LIB=name]   (default 300, opengfx.library)
 *   TextABITest PROBE                   (OS 3.2's answers for every corner
 *                                        README.md's Edge cases calls an
 *                                        ABI test, one PROBE line each)
 * Exit 0 when nothing differs, 5 when something does, 20 when it cannot run. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

struct GfxBase *GfxBase;
static struct GfxBase *OSBase, *OurBase;
#define OS(x) (GfxBase = OSBase, (x))
#define US(x) (GfxBase = OurBase, (x))

static ULONG seed = 0x7E47;
static ULONG rnd(void) { seed = seed * 1103515245UL + 12345UL; return seed >> 8; }
static ULONG calls, diffs;

#define BW 320
#define BH 48
static struct BitMap *bma, *bmb;
static struct RastPort rpa, rpb;

/* ---- fonts made here ---------------------------------------------------- */

#define NF 8
static struct ColorTextFont cf[NF];
static UBYTE fdata[NF][3][64 * 12];
static UWORD floc[NF][2 * 80];
static WORD fspace[NF][80], fkern[NF][80];
static const char *const fname = "abitest.font";

static void make_font(int i)
{
    struct TextFont *tf = &cf[i].ctf_TF;
    int lo = 32 + (int)(rnd() % 40), hi = lo + 2 + (int)(rnd() % 30), n = hi - lo + 2, g, p, r, off = 0;
    int ys = 1 + (int)(rnd() % 12), depth = 1;
    memset(&cf[i], 0, sizeof cf[i]);
    tf->tf_Message.mn_Node.ln_Name = (char *)fname;
    tf->tf_YSize = (UWORD)ys;
    tf->tf_Baseline = (UWORD)(rnd() % ys);
    tf->tf_XSize = (UWORD)(1 + rnd() % 12);
    tf->tf_BoldSmear = (UWORD)(rnd() % 3);
    tf->tf_LoChar = (UBYTE)lo;
    tf->tf_HiChar = (UBYTE)hi;
    tf->tf_Modulo = 64;
    if (i >= 5) {               /* colour */
        tf->tf_Style |= FSF_COLORFONT;
        depth = 1 + (int)(rnd() % 3);
        cf[i].ctf_Depth = (UBYTE)depth;
        cf[i].ctf_PlanePick = (UBYTE)(rnd() % 255 + 1);
        cf[i].ctf_PlaneOnOff = (UBYTE)rnd();
        cf[i].ctf_FgColor = (UBYTE)(rnd() % 8);
        cf[i].ctf_Flags = (UWORD)(rnd() & 1);
    }
    if (i == 4 || i == 7) tf->tf_Flags |= FPF_REVPATH;
    for (g = 0; g < n; g++) {
        int w = (int)(rnd() % 12);
        floc[i][2 * g] = (UWORD)off;
        floc[i][2 * g + 1] = (UWORD)w;
        off += w;
        fspace[i][g] = (WORD)(rnd() % 14) - 2;
        fkern[i][g] = (WORD)(rnd() % 7) - 3;
    }
    for (p = 0; p < depth; p++) {
        for (r = 0; r < ys * 64; r++) fdata[i][p][r] = (UBYTE)(rnd() & rnd());
        cf[i].ctf_CharData[p] = fdata[i][p];
    }
    tf->tf_CharData = fdata[i][0];
    tf->tf_CharLoc = floc[i];
    if (i & 1) { tf->tf_CharSpace = fspace[i]; tf->tf_Flags |= FPF_PROPORTIONAL; }
    if (i & 2) tf->tf_CharKern = fkern[i];
}

/* Each side's call is its own statement: GfxBase is switched in between. */
#define BOTH(what, call_a, call_b)                                                                                \
    do {                                                                                                          \
        LONG ra_ = OS(call_a);                                                                                    \
        LONG rb_ = US(call_b);                                                                                    \
        l_cmp(what, ra_, rb_);                                                                                    \
    } while (0)

/* ---- comparing ---------------------------------------------------------- */

static void te_show(const char *who, const struct TextExtent *te)
{
    Printf("  %s: width %ld height %ld extent %ld,%ld-%ld,%ld\n", (LONG)who, (LONG)te->te_Width, (LONG)te->te_Height,
           (LONG)te->te_Extent.MinX, (LONG)te->te_Extent.MinY, (LONG)te->te_Extent.MaxX, (LONG)te->te_Extent.MaxY);
}
static void te_cmp(const char *what, const struct TextExtent *a, const struct TextExtent *b)
{
    calls++;
    if (!memcmp(a, b, sizeof *a)) return;
    diffs++;
    Printf("DIFF %s\n", (LONG)what);
    te_show("OS  ", a);
    te_show("ours", b);
}
static void l_cmp(const char *what, LONG a, LONG b)
{
    calls++;
    if (a == b) return;
    diffs++;
    Printf("DIFF %s: OS %ld ours %ld\n", (LONG)what, a, b);
}
static void bm_cmp(const char *what)
{
    int p;
    calls++;
    for (p = 0; p < bma->Depth; p++)
        if (memcmp(bma->Planes[p], bmb->Planes[p], (ULONG)bma->BytesPerRow * bma->Rows)) {
            diffs++;
            Printf("DIFF %s: plane %ld\n", (LONG)what, (LONG)p);
            return;
        }
}
static void bm_fill(void)
{
    int p;
    ULONG i;
    for (p = 0; p < bma->Depth; p++)
        for (i = 0; i < (ULONG)bma->BytesPerRow * bma->Rows; i++) bma->Planes[p][i] = bmb->Planes[p][i] = (UBYTE)rnd();
}

static void rp_same(struct TextFont *tf)
{
    UBYTE algo = (UBYTE)(rnd() % 8), mode = (UBYTE)(rnd() % 8), fg = (UBYTE)rnd(), bg = (UBYTE)rnd();
    WORD sp = (rnd() % 4) ? 0 : (WORD)(rnd() % 7) - 3, x = (WORD)(rnd() % BW), y = (WORD)(rnd() % BH);
    UBYTE mask = (rnd() % 3) ? 0xFF : (UBYTE)rnd();
    OS(SetFont(&rpa, tf));
    US(SetFont(&rpb, tf));
    rpa.AlgoStyle = rpb.AlgoStyle = algo;
    rpa.DrawMode = rpb.DrawMode = mode;
    rpa.FgPen = rpb.FgPen = fg;
    rpa.BgPen = rpb.BgPen = bg;
    rpa.Mask = rpb.Mask = mask;
    rpa.TxSpacing = rpb.TxSpacing = sp;
    rpa.cp_x = rpb.cp_x = x;
    rpa.cp_y = rpb.cp_y = y;
}

static void round_fonts(void)
{
    char what[80];
    UBYTE s[16];
    int i = (int)(rnd() % NF), n = (int)(rnd() % 14), k;
    struct TextFont *tf = &cf[i].ctf_TF;
    struct TextExtent ta, tb, con;
    LONG ra, rb, dir;
    ULONG bwid, bhei;
    for (k = 0; k < n; k++) s[k] = (UBYTE)(rnd() % 3 ? tf->tf_LoChar + rnd() % 40 : rnd());
    rp_same(tf);
    sprintf(what, "font %d (style %d flags %d) n %d algo %d mode %d", i, tf->tf_Style, tf->tf_Flags, n,
            rpa.AlgoStyle, rpa.DrawMode);
    BOTH("TextLength", TextLength(&rpa, (STRPTR)s, n), TextLength(&rpb, (STRPTR)s, n));
    OS(TextExtent(&rpa, (STRPTR)s, n, &ta));
    US(TextExtent(&rpb, (STRPTR)s, n, &tb));
    te_cmp(what, &ta, &tb);
    bm_fill();
    OS(Text(&rpa, (STRPTR)s, n));
    US(Text(&rpb, (STRPTR)s, n));
    bm_cmp(what);
    l_cmp("Text cp_x", rpa.cp_x, rpb.cp_x);

    dir = rnd() & 1 ? 1 : -1;
    con.te_Extent.MinX = -(WORD)(rnd() % 10);
    con.te_Extent.MinY = -(WORD)(rnd() % 14);
    con.te_Extent.MaxX = (WORD)(rnd() % 80);
    con.te_Extent.MaxY = (WORD)(rnd() % 10);
    bwid = rnd() & 1 ? 0xFFFF : rnd() % 100;
    bhei = rnd() & 1 ? 0xFFFF : rnd() % 14;
    {
        STRPTR p = (STRPTR)s + (dir < 0 && n ? n - 1 : 0);
        struct TextExtent *c = rnd() & 1 ? &con : 0;
        ra = OS(TextFit(&rpa, p, n, &ta, c, dir, bwid, bhei));
        rb = US(TextFit(&rpb, p, n, &tb, c, dir, bwid, bhei));
    }
    l_cmp("TextFit count", ra, rb);
    te_cmp("TextFit extent", &ta, &tb);
    OS(FontExtent(tf, &ta));
    US(FontExtent(tf, &tb));
    te_cmp("FontExtent", &ta, &tb);
}

static void round_style(void)
{
    struct TextFont *tf = &cf[rnd() % NF].ctf_TF;
    struct TextAttr aa, ab;
    ULONG st = rnd() & 0xFF, en = rnd() & 0xFF;
    UBYTE save = tf->tf_Style;
    tf->tf_Style = (UBYTE)((save & FSF_COLORFONT) | (rnd() & 0x0F));
    rp_same(tf);
    BOTH("AskSoftStyle", AskSoftStyle(&rpa), AskSoftStyle(&rpb));
    BOTH("SetSoftStyle", SetSoftStyle(&rpa, st, en), SetSoftStyle(&rpb, st, en));
    l_cmp("SetSoftStyle AlgoStyle", rpa.AlgoStyle, rpb.AlgoStyle);
    OS(AskFont(&rpa, &aa));
    US(AskFont(&rpb, &ab));
    l_cmp("AskFont name", (LONG)aa.ta_Name, (LONG)ab.ta_Name);
    l_cmp("AskFont size", aa.ta_YSize, ab.ta_YSize);
    l_cmp("AskFont style", aa.ta_Style, ab.ta_Style);
    l_cmp("AskFont flags", aa.ta_Flags, ab.ta_Flags);
    tf->tf_Style = save;
}

static void round_weigh(void)
{
    struct TTextAttr a = {0, (UWORD)(1 + rnd() % 30), (UBYTE)(rnd() % 16), (UBYTE)rnd(), 0};
    struct TextAttr b = {0, (UWORD)(rnd() & 1 ? a.tta_YSize : 1 + rnd() % 40), (UBYTE)(rnd() % 16), (UBYTE)rnd()};
    struct TagItem ta[2] = {{TA_DeviceDPI, 0x00480048}, {TAG_DONE, 0}}, tb[2] = {{TA_DeviceDPI, 0x00480024}, {TAG_DONE, 0}};
    struct TagItem *tt = rnd() & 1 ? (rnd() & 1 ? ta : tb) : 0;
    if (rnd() & 1) { a.tta_Style |= FSF_TAGGED; a.tta_Tags = rnd() & 1 ? ta : tb; }
    BOTH("WeighTAMatch", WeighTAMatch((struct TextAttr *)&a, &b, tt), WeighTAMatch((struct TextAttr *)&a, &b, tt));
}

static void round_clear(void)
{
    rp_same(&cf[0].ctf_TF);
    bm_fill();
    if (rnd() & 1) { OS(ClearEOL(&rpa)); US(ClearEOL(&rpb)); bm_cmp("ClearEOL"); }
    else { OS(ClearScreen(&rpa)); US(ClearScreen(&rpb)); bm_cmp("ClearScreen"); }
}

/* each library's own topaz: the measures (the drawing is redrawn) */
static void topaz_cmp(void)
{
    static const UWORD sizes[] = {8, 9, 7, 10, 11, 6};
    int i;
    for (i = 0; i < 6; i++) {
        struct TextAttr ta = {(STRPTR) "topaz.font", sizes[i], 0, 0};
        struct TextFont *a = OS(OpenFont(&ta)), *b = US(OpenFont(&ta));
        struct TextExtent ea, eb;
        l_cmp("OpenFont topaz found", a != 0, b != 0);
        if (!a || !b) continue;
        l_cmp("OpenFont topaz YSize", a->tf_YSize, b->tf_YSize);
        l_cmp("topaz XSize", a->tf_XSize, b->tf_XSize);
        l_cmp("topaz Baseline", a->tf_Baseline, b->tf_Baseline);
        l_cmp("topaz BoldSmear", a->tf_BoldSmear, b->tf_BoldSmear);
        l_cmp("topaz Flags", a->tf_Flags, b->tf_Flags);
        l_cmp("topaz LoChar", a->tf_LoChar, b->tf_LoChar);
        l_cmp("topaz HiChar", a->tf_HiChar, b->tf_HiChar);
        OS(FontExtent(a, &ea));
        US(FontExtent(b, &eb));
        te_cmp("topaz FontExtent", &ea, &eb);
        OS(CloseFont(a));
        US(CloseFont(b));
    }
}

/* ---- PROBE ---------------------------------------------------------------- */

static void probe_te(const char *what, const struct TextExtent *te)
{
    Printf("PROBE %s: width %ld height %ld extent %ld,%ld-%ld,%ld\n", (LONG)what, (LONG)te->te_Width,
           (LONG)te->te_Height, (LONG)te->te_Extent.MinX, (LONG)te->te_Extent.MinY, (LONG)te->te_Extent.MaxX,
           (LONG)te->te_Extent.MaxY);
}

static void probe(void)
{
    struct Node *n;
    struct TextFont *tf, pf[2];
    struct TextAttr ta;
    struct TextExtent te;
    struct TagItem tags[2] = {{TA_DeviceDPI, 0x00480048}, {TAG_DONE, 0}};
    struct TextFontExtension *e;
    struct MsgPort *port;
    LONG r;
    int i;
    GfxBase = OSBase;
    Printf("PROBE %s", (LONG)((struct Library *)GfxBase)->lib_IdString);
    Forbid();
    for (n = GfxBase->TextFonts.lh_Head; n->ln_Succ; n = n->ln_Succ) {
        tf = (struct TextFont *)n;
        Printf("PROBE font %s %ld: X %ld base %ld smear %ld style %ld flags %ld acc %ld chars %ld-%ld mod %ld "
               "space %ld kern %ld len %ld type %ld pri %ld%s\n", (LONG)n->ln_Name, (LONG)tf->tf_YSize,
               (LONG)tf->tf_XSize, (LONG)tf->tf_Baseline, (LONG)tf->tf_BoldSmear, (LONG)tf->tf_Style,
               (LONG)tf->tf_Flags, (LONG)tf->tf_Accessors, (LONG)tf->tf_LoChar, (LONG)tf->tf_HiChar,
               (LONG)tf->tf_Modulo, (LONG)(tf->tf_CharSpace != 0), (LONG)(tf->tf_CharKern != 0),
               (LONG)tf->tf_Message.mn_Length, (LONG)n->ln_Type, (LONG)n->ln_Pri,
               (LONG)(tf == GfxBase->DefaultFont ? " DefaultFont" : ""));
    }
    Permit();
    tf = GfxBase->DefaultFont;

    /* SetFont keeps AlgoStyle and TxSpacing? */
    rpa.AlgoStyle = 7;
    rpa.TxSpacing = 3;
    r = SetFont(&rpa, tf);
    Printf("PROBE SetFont: answers %ld AlgoStyle %ld TxSpacing %ld TxHeight %ld TxWidth %ld TxBaseline %ld\n", r,
           (LONG)rpa.AlgoStyle, (LONG)rpa.TxSpacing, (LONG)rpa.TxHeight, (LONG)rpa.TxWidth, (LONG)rpa.TxBaseline);
    rpa.AlgoStyle = 0;
    rpa.TxSpacing = 0;

    /* AskSoftStyle: is FSF_EXTENDED among the soft styles? */
    for (i = 0; i < 16; i++) {
        UBYTE save = tf->tf_Style;
        Forbid();
        tf->tf_Style = (UBYTE)i;
        r = AskSoftStyle(&rpa);
        tf->tf_Style = save;
        Permit();
        Printf("PROBE AskSoftStyle tf_Style %ld: %ld\n", (LONG)i, r);
    }
    r = SetSoftStyle(&rpa, 0xFF, 0xFF);
    Printf("PROBE SetSoftStyle(0xFF, 0xFF): answers %ld AlgoStyle %ld\n", r, (LONG)rpa.AlgoStyle);
    r = SetSoftStyle(&rpa, 0, FSF_BOLD);
    Printf("PROBE SetSoftStyle(0, BOLD): answers %ld AlgoStyle %ld\n", r, (LONG)rpa.AlgoStyle);
    rpa.TxHeight = 5;
    AskFont(&rpa, &ta);
    Printf("PROBE AskFont (TxHeight 5): size %ld style %ld flags %ld name %s\n", (LONG)ta.ta_YSize,
           (LONG)ta.ta_Style, (LONG)ta.ta_Flags, (LONG)(ta.ta_Name ? ta.ta_Name : (STRPTR) "(null)"));
    SetFont(&rpa, tf);
    rpa.AlgoStyle = 0;

    /* OpenFont: sizes not there, names in other case */
    for (i = 5; i <= 12; i++) {
        struct TextFont *o;
        ta.ta_Name = (STRPTR) "topaz.font";
        ta.ta_YSize = (UWORD)i;
        ta.ta_Style = 0;
        ta.ta_Flags = 0;
        o = OpenFont(&ta);
        Printf("PROBE OpenFont topaz %ld: %ld\n", (LONG)i, o ? (LONG)o->tf_YSize : -1L);
        if (o) CloseFont(o);
    }
    ta.ta_Name = (STRPTR) "TOPAZ.FONT";
    ta.ta_YSize = 8;
    {
        struct TextFont *o = OpenFont(&ta);
        Printf("PROBE OpenFont TOPAZ.FONT: %s\n", (LONG)(o ? "found" : "none"));
        if (o) CloseFont(o);
    }

    /* AddFont: head or tail, ln_Type, FPF_REMOVED; RemFont's answer */
    for (i = 0; i < 2; i++) {
        pf[i] = *tf;
        pf[i].tf_Message.mn_Node.ln_Name = (char *)"abiprobe.font";
        pf[i].tf_Message.mn_Node.ln_Type = 0;
        pf[i].tf_Message.mn_ReplyPort = 0;
        pf[i].tf_Flags |= FPF_REMOVED;
        pf[i].tf_Accessors = 0;
        AddFont(&pf[i]);
    }
    Printf("PROBE AddFont: type %ld flags %ld head %ld\n", (LONG)pf[0].tf_Message.mn_Node.ln_Type,
           (LONG)pf[0].tf_Flags, (LONG)(GfxBase->TextFonts.lh_Head == &pf[1].tf_Message.mn_Node));
    ta.ta_Name = (STRPTR) "abiprobe.font";
    ta.ta_YSize = tf->tf_YSize;
    {
        struct TextFont *o = OpenFont(&ta);
        Printf("PROBE OpenFont of equals: %s acc %ld\n", (LONG)(o == &pf[1] ? "newest" : o == &pf[0] ? "oldest" : "?"),
               o ? (LONG)o->tf_Accessors : -1L);
        if (o) { CloseFont(o); CloseFont(o); }
        Printf("PROBE CloseFont below 0: acc %ld\n", o ? (LONG)o->tf_Accessors : -1L);
        if (o) o->tf_Accessors = 0;
    }

    /* ExtendFont and StripFont */
    port = CreateMsgPort();
    pf[0].tf_Message.mn_ReplyPort = port;
    r = ExtendFont(&pf[0], tags);
    e = (struct TextFontExtension *)pf[0].tf_Message.mn_ReplyPort;
    Printf("PROBE ExtendFont: answers %ld match %lx flags0 %ld flags1 %ld back %ld orig %ld tags %s style %ld\n", r,
           (LONG)e->tfe_MatchWord, (LONG)e->tfe_Flags0, (LONG)e->tfe_Flags1, (LONG)(e->tfe_BackPtr == &pf[0]),
           (LONG)(e->tfe_OrigReplyPort == port), (LONG)(e->tfe_Tags == tags ? "same" : e->tfe_Tags ? "copied" : "none"),
           (LONG)pf[0].tf_Style);
    r = ExtendFont(&pf[0], 0);
    Printf("PROBE ExtendFont again: answers %ld same extension %ld\n", r,
           (LONG)((struct TextFontExtension *)pf[0].tf_Message.mn_ReplyPort == e));
    e->tfe_Flags0 |= TE0F_NOREMFONT;
    r = RemFont(&pf[0]);
    Printf("PROBE RemFont NOREMFONT: answers %ld flags %ld\n", r, (LONG)pf[0].tf_Flags);
    e->tfe_Flags0 &= ~TE0F_NOREMFONT;
    StripFont(&pf[0]);
    Printf("PROBE StripFont: port back %ld style %ld\n", (LONG)(pf[0].tf_Message.mn_ReplyPort == port),
           (LONG)pf[0].tf_Style);
    for (i = 0; i < 2; i++) {
        r = RemFont(&pf[i]);
        Printf("PROBE RemFont %ld: answers %ld flags %ld\n", (LONG)i, r, (LONG)pf[i].tf_Flags);
    }
    DeleteMsgPort(port);

    /* FontExtent of topaz 8 and with style bits */
    FontExtent(tf, &te);
    probe_te("FontExtent DefaultFont", &te);

    /* TextFit corners */
    SetFont(&rpa, tf);
    r = TextFit(&rpa, (STRPTR) "ABCDEFGH" + 7, 8, &te, 0, -1, 30, 100);
    Printf("PROBE TextFit back 30 wide: %ld\n", r);
    probe_te("TextFit back extent", &te);
    r = TextFit(&rpa, (STRPTR) "ABCDEFGH", 8, &te, 0, 1, 3, 100);
    Printf("PROBE TextFit none fits: %ld\n", r);
    probe_te("TextFit none extent", &te);
    r = TextFit(&rpa, (STRPTR) "ABCDEFGH", 8, &te, 0, 1, 1000, tf->tf_YSize - 1);
    Printf("PROBE TextFit too low: %ld\n", r);
    r = TextFit(&rpa, (STRPTR) "ABCDEFGH", 0x10002, &te, 0, 1, 1000, 100);
    Printf("PROBE TextFit 0x10002 characters: %ld\n", r);

    /* WeighTAMatch numbers */
    {
        static const UWORD ws[][6] = {
            {8, 0, 0, 8, 0, 0}, {8, 0, 0, 9, 0, 0}, {8, 0, 0, 7, 0, 0}, {8, 0, 0, 12, 0, 0}, {8, 0, 0, 8, FSF_BOLD, 0},
            {8, 0, 0, 8, FSF_ITALIC, 0}, {8, 0, 0, 8, FSF_UNDERLINED, 0}, {8, 0, 0, 8, FSF_EXTENDED, 0},
            {8, 0, 0, 8, 0, FPF_REVPATH}, {8, 0, 0, 8, 0, FPF_PROPORTIONAL}, {8, 0, 0, 8, 0, FPF_TALLDOT},
            {8, 0, 0, 8, 0, FPF_ROMFONT}, {8, 0, 0, 8, 0, FPF_DESIGNED}, {8, 0, FPF_DESIGNED, 8, 0, 0},
            {8, 0, 0, 200, 0, 0}};
        for (i = 0; i < (int)(sizeof ws / sizeof ws[0]); i++) {
            struct TextAttr a = {0, ws[i][0], (UBYTE)ws[i][1], (UBYTE)ws[i][2]};
            struct TextAttr b = {0, ws[i][3], (UBYTE)ws[i][4], (UBYTE)ws[i][5]};
            Printf("PROBE WeighTAMatch %ld/%ld/%ld to %ld/%ld/%ld: %ld\n", (LONG)ws[i][0], (LONG)ws[i][1],
                   (LONG)ws[i][2], (LONG)ws[i][3], (LONG)ws[i][4], (LONG)ws[i][5], (LONG)WeighTAMatch(&a, &b, 0));
        }
        {
            struct TTextAttr a = {0, 8, FSF_TAGGED, 0, tags};
            struct TagItem t2[2] = {{TA_DeviceDPI, 0x00480024}, {TAG_DONE, 0}};
            struct TextAttr b = {0, 8, 0, 0};
            Printf("PROBE WeighTAMatch DPI differ: %ld\n", (LONG)WeighTAMatch((struct TextAttr *)&a, &b, t2));
            Printf("PROBE WeighTAMatch DPI one side: %ld\n", (LONG)WeighTAMatch((struct TextAttr *)&a, &b, 0));
        }
    }

    /* ClearEOL's pen: JAM1, JAM2, COMPLEMENT */
    for (i = 0; i < 3; i++) {
        static const UBYTE modes[3] = {JAM1, JAM2, COMPLEMENT};
        SetRast(&rpa, 3);
        rpa.DrawMode = modes[i];
        rpa.BgPen = 5;
        rpa.FgPen = 6;
        rpa.cp_x = 10;
        rpa.cp_y = 20;
        ClearEOL(&rpa);
        Printf("PROBE ClearEOL mode %ld: pen %ld at 10,%ld; pen %ld at 9; rows %ld..%ld\n", (LONG)modes[i],
               ReadPixel(&rpa, 10, 20), 20L, ReadPixel(&rpa, 9, 20), (LONG)(20 - rpa.TxBaseline),
               (LONG)(20 - rpa.TxBaseline + rpa.TxHeight - 1));
    }
    rpa.DrawMode = JAM2;

    /* a font drawn right to left, and a colour font */
    make_font(4);
    make_font(7);
    for (i = 0; i < 2; i++) {
        struct TextFont *f = &cf[i ? 7 : 4].ctf_TF;
        UBYTE s[3];
        int x, y;
        s[0] = f->tf_LoChar;
        s[1] = (UBYTE)(f->tf_LoChar + 1);
        s[2] = (UBYTE)(f->tf_LoChar + 2);
        SetFont(&rpa, f);
        SetRast(&rpa, 0);
        rpa.cp_x = 100;
        rpa.cp_y = 20;
        rpa.FgPen = 1;
        rpa.BgPen = 2;
        TextExtent(&rpa, (STRPTR)s, 3, &te);
        Printf("PROBE %s: TextLength %ld\n", (LONG)(i ? "colour font" : "REVPATH"), (LONG)TextLength(&rpa, (STRPTR)s, 3));
        probe_te(i ? "colour TextExtent" : "REVPATH TextExtent", &te);
        Text(&rpa, (STRPTR)s, 3);
        Printf("PROBE %s Text: cp_x %ld\n", (LONG)(i ? "colour" : "REVPATH"), (LONG)rpa.cp_x);
        for (y = 20 - f->tf_Baseline; y < 20 - f->tf_Baseline + f->tf_YSize; y++) {
            char line[100];
            for (x = 60; x < 140; x++) { LONG p = ReadPixel(&rpa, x, y); line[x - 60] = (char)(p < 10 ? '0' + p : 'a' + p - 10); }
            line[80] = 0;
            Printf("PROBE   %3ld %s\n", (LONG)y, (LONG)line);
        }
    }
}

int main(int argc, char **argv)
{
    ULONG rounds = 300, i;
    const char *lib = "opengfx.library";
    int doprobe = 0, a;
    for (a = 1; a < argc; a++) {
        if (!strncmp(argv[a], "ROUNDS=", 7)) rounds = (ULONG)atoi(argv[a] + 7);
        else if (!strncmp(argv[a], "LIB=", 4)) lib = argv[a] + 4;
        else if (!strcmp(argv[a], "PROBE")) doprobe = 1;
    }
    OSBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    OurBase = doprobe ? 0 : (struct GfxBase *)OpenLibrary(lib, 47);
    if (!OSBase || (!doprobe && !OurBase)) { Printf("cannot open %s\n", (LONG)lib); return 20; }
    GfxBase = OSBase;
    bma = AllocBitMap(BW, BH, 8, BMF_CLEAR, 0);
    bmb = AllocBitMap(BW, BH, 8, BMF_CLEAR, 0);
    if (!bma || !bmb) return 20;
    InitRastPort(&rpa);
    InitRastPort(&rpb);
    rpa.BitMap = bma;
    rpb.BitMap = bmb;
    if (doprobe) {
        probe();
    } else {
        for (i = 0; i < NF; i++) make_font((int)i);
        topaz_cmp();
        for (i = 0; i < rounds; i++) {
            switch (rnd() % 8) {
            case 0: round_style(); break;
            case 1: round_weigh(); break;
            case 2: round_clear(); break;
            default: round_fonts(); break;
            }
            if (i % 50 == 49) for (a = 0; a < NF; a++) make_font(a);
        }
        Printf("%ld calls, %ld differ\n", (LONG)calls, (LONG)diffs);
        CloseLibrary((struct Library *)OurBase);
    }
    GfxBase = OSBase;
    WaitBlit();
    FreeBitMap(bma);
    FreeBitMap(bmb);
    CloseLibrary((struct Library *)OSBase);
    return diffs ? 5 : 0;
}
