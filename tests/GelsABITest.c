/* SPDX-License-Identifier: MIT */
/* GelsABITest: OpenGfx graphics.library's gels and blitter calls
 * (library/gels) side by side with OS 3.2's graphics.library. Copyright
 * (c) 2026 AmigaChrome Team. MIT.
 *
 * One script of calls is run through OS 3.2's graphics.library and then
 * through opengfx.library (OpenGfx's graphics.library under its own name),
 * each writing its answers as lines; the lines are compared and each one
 * that differs is printed with both answers. The script is the corners
 * the autodocs leave open, each marked "ABI test" in library/gels:
 * InitGels' fields, AddVSprite's order and flags, a Bob's pixels (shift,
 * PlanePick/PlaneOnOff, OVERLAY, how much of SaveBuffer DrawGList
 * writes), Before/After, BOBSAWAY and double buffering, RemIBob over
 * other Bobs, DoCollision's calls and their arguments, Animate's
 * arithmetic and order, GetGBuffers' buffers, BltClear's odd counts and
 * fill, BltTemplate's DrawModes, BltMaskBitMapRastPort's minterms, the
 * QBlit routine's registers and calls, QBSBlit's beam, and (with NEST,
 * which may hang OS 3.2) OwnBlitter twice.
 * With PROBE it prints OS 3.2's lines only, to record them.
 *   GelsABITest [PROBE] [NEST] [LIB=dir]
 * Answers 0 when nothing differs, 5 when something does. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/gels.h>
#include <graphics/rastport.h>
#include <graphics/collide.h>
#include <hardware/blit.h>
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
static int probe, nest;

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

#define W 64
#define H 24
#define D 3
static struct BitMap *bm, *bm2;
static struct RastPort rp;

static void rows(const char *tag)
{
    int x, y;
    char row[W + 1];
    WaitBlit();
    out("%s\n", tag);
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) row[x] = "0123456789abcdef"[ReadPixel(&rp, x, y) & 15];
        row[x] = 0;
        out("  %s\n", row);
    }
}

static void pattern(void)
{
    int x, y;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            SetAPen(&rp, (x / 3 + y) & 7);
            WritePixel(&rp, x, y);
        }
}

static void hexdump(const char *tag, const void *p, int n)
{
    int i;
    out("%s", tag);
    for (i = 0; i < n; i++) out(" %02x", ((const UBYTE *)p)[i]);
    out("\n");
}

/* ---- the gels ------------------------------------------------------------ */
static struct GelsInfo gi;
static struct VSprite head, tail;
static struct collTable ct;
static WORD nextline[8];
static WORD *lastcolor[8];

static void gels_setup(void)
{
    memset(&gi, 0, sizeof gi);
    gi.nextLine = nextline;
    gi.lastColor = lastcolor;
    gi.collHandler = &ct;
    gi.leftmost = 0;
    gi.rightmost = W - 1;
    gi.topmost = 0;
    gi.bottommost = H - 1;
    InitGels(&head, &tail, &gi);
    rp.GelsInfo = &gi;
}

struct tbob {
    struct VSprite vs;
    struct Bob bob;
    struct DBufPacket db;
    WORD *img, *mask, *line, *save, *buf;
    ULONG savesize;
};
#define SENTINEL 32

static void tbob_make(struct tbob *t, int w, int h, int depth, int pick, int onoff, int flags, int seed)
{
    int i, n = w * h * depth;
    memset(t, 0, sizeof *t);
    t->img = AllocMem(n * 2, MEMF_CHIP);
    t->mask = AllocMem(w * h * 2, MEMF_CHIP | MEMF_CLEAR);
    t->line = AllocMem(w * 2, MEMF_CHIP | MEMF_CLEAR);
    t->savesize = w * h * D * 2;
    t->save = AllocMem(t->savesize + SENTINEL, MEMF_CHIP);
    t->buf = AllocMem(t->savesize + SENTINEL, MEMF_CHIP);
    for (i = 0; i < n; i++) t->img[i] = (WORD)(seed * 0x9E37 + i * 0x7F4A) ^ (WORD)(i * i * 0x1234);
    memset(t->save, 0xA5, t->savesize + SENTINEL);
    memset(t->buf, 0xA5, t->savesize + SENTINEL);
    t->vs.Width = w;
    t->vs.Height = h;
    t->vs.Depth = depth;
    t->vs.PlanePick = pick;
    t->vs.PlaneOnOff = onoff;
    t->vs.Flags = flags;
    t->vs.ImageData = t->img;
    t->vs.CollMask = t->mask;
    t->vs.BorderLine = t->line;
    t->vs.VSBob = &t->bob;
    t->bob.BobVSprite = &t->vs;
    t->bob.SaveBuffer = t->save;
    InitMasks(&t->vs);
    t->bob.ImageShadow = t->mask;
}

static void tbob_free(struct tbob *t)
{
    FreeMem(t->img, t->vs.Width * t->vs.Height * t->vs.Depth * 2);
    FreeMem(t->mask, t->vs.Width * t->vs.Height * 2);
    FreeMem(t->line, t->vs.Width * 2);
    FreeMem(t->save, t->savesize + SENTINEL);
    FreeMem(t->buf, t->savesize + SENTINEL);
}

static int overrun(const WORD *p, ULONG size)
{
    int i, n = 0;
    for (i = 0; i < SENTINEL; i++) n += ((const UBYTE *)p)[size + i] != 0xA5;
    return n;
}

static void list(const char *tag)
{
    struct VSprite *v;
    out("%s", tag);
    for (v = head.NextVSprite; v && v != &tail; v = v->NextVSprite)
        out(" (%d,%d f%04x b%04x)", v->X, v->Y, (UWORD)v->Flags, v->VSBob ? (UWORD)v->VSBob->Flags : 0);
    out("\n");
}

static void s_layout(void)
{
    out("LAYOUT VSprite %lu %lu %lu %lu Bob %lu %lu AnimComp %lu %lu AnimOb %lu %lu GelsInfo %lu %lu bltnode %lu %lu %lu\n",
        (ULONG)sizeof(struct VSprite), (ULONG)offsetof(struct VSprite, Flags), (ULONG)offsetof(struct VSprite, ImageData),
        (ULONG)offsetof(struct VSprite, PlanePick), (ULONG)sizeof(struct Bob), (ULONG)offsetof(struct Bob, DBuffer),
        (ULONG)sizeof(struct AnimComp), (ULONG)offsetof(struct AnimComp, HeadOb), (ULONG)sizeof(struct AnimOb),
        (ULONG)offsetof(struct AnimOb, HeadComp), (ULONG)sizeof(struct GelsInfo), (ULONG)offsetof(struct GelsInfo, collHandler),
        (ULONG)sizeof(struct bltnode), (ULONG)offsetof(struct bltnode, beamsync), (ULONG)offsetof(struct bltnode, cleanup));
}

static void s_initgels(void)
{
    memset(&head, 0x5A, sizeof head);
    memset(&tail, 0x5A, sizeof tail);
    memset(&gi, 0x5A, sizeof gi);
    InitGels(&head, &tail, &gi);
    hexdump("INITGELS head", &head, sizeof head);
    hexdump("INITGELS tail", &tail, sizeof tail);
    out("INITGELS gi head=%d tail=%d", gi.gelHead == &head, gi.gelTail == &tail);
    hexdump("", &gi, sizeof gi);
    out("INITGELS links %d %d %d %d\n", head.NextVSprite == &tail, head.PrevVSprite == 0, tail.PrevVSprite == &head,
        tail.NextVSprite == 0);
}

static void s_addvs(void)
{
    static struct VSprite v[4];
    int i;
    gels_setup();
    for (i = 0; i < 4; i++) {
        memset(&v[i], 0, sizeof v[i]);
        v[i].Flags = (WORD)(0xFF00 | VSPRITE | (i & 1 ? SAVEBACK : 0));
        v[i].Y = 5;
        v[i].X = i < 2 ? 7 : -3;
        v[i].Width = 1;
        v[i].Height = 2;
        AddVSprite(&v[i], &rp);
    }
    out("ADDVS");
    {
        struct VSprite *p;
        for (p = head.NextVSprite; p != &tail; p = p->NextVSprite) out(" %d:%04x", (int)(p - v), (UWORD)p->Flags);
    }
    out("\n");
    v[0].Y = -1;
    SortGList(&rp);
    list("SORTGLIST");
    RemVSprite(&v[1]);
    list("REMVSPRITE");
}

static void s_bobs(void)
{
    static struct tbob a, b, c;
    gels_setup();
    SetRast(&rp, 0);
    pattern();
    tbob_make(&a, 2, 7, 2, 0x05, 0x02, SAVEBACK | OVERLAY, 1);
    tbob_make(&b, 1, 9, 3, 0x07, 0x00, SAVEBACK, 2);
    tbob_make(&c, 2, 5, 1, 0x02, 0x05, OVERLAY, 3);
    a.vs.X = 5;  a.vs.Y = 3;
    b.vs.X = 13; b.vs.Y = 4;
    c.vs.X = 40; c.vs.Y = -2;
    AddBob(&a.bob, &rp);
    AddBob(&b.bob, &rp);
    AddBob(&c.bob, &rp);
    list("ADDBOB");
    DrawGList(&rp, 0);
    rows("DRAWGLIST 1");
    list("DRAWN");
    out("SAVEBUFFER past Width*Height*Depth words: %d %d (a at x 5, b at x 13)\n", overrun(a.save, a.savesize),
        overrun(b.save, b.savesize));
    /* Before/After: b after a */
    a.bob.After = 0; b.bob.After = &a.bob; a.bob.Before = &b.bob;
    a.vs.X = 9; b.vs.X = 7; b.vs.Y = 2;
    SortGList(&rp);
    DrawGList(&rp, 0);
    rows("DRAWGLIST after/before");
    /* RemIBob of the Bob under another */
    RemIBob(&a.bob, &rp, 0);
    rows("REMIBOB");
    list("AFTER REMIBOB");
    out("REMIBOB flags %04x\n", (UWORD)a.bob.Flags);
    /* BOBSAWAY */
    RemBob(&b.bob);
    DrawGList(&rp, 0);
    rows("BOBSAWAY");
    list("AFTER BOBSAWAY");
    out("BOBSAWAY flags %04x\n", (UWORD)b.bob.Flags);
    RemIBob(&c.bob, &rp, 0);
    WaitBlit();
    tbob_free(&a); tbob_free(&b); tbob_free(&c);
}

static void s_dbuf(void)
{
    static struct tbob a;
    struct BitMap *bms[2];
    int f;
    bms[0] = bm; bms[1] = bm2;
    gels_setup();
    SetRast(&rp, 1);
    rp.BitMap = bm2;
    SetRast(&rp, 2);
    tbob_make(&a, 1, 6, 2, 0x03, 0, SAVEBACK | OVERLAY, 4);
    a.bob.DBuffer = &a.db;
    a.db.BufBuffer = a.buf;
    AddBob(&a.bob, &rp);
    for (f = 0; f < 5; f++) {
        rp.BitMap = bms[f & 1];
        a.vs.X = 3 + 5 * f;
        a.vs.Y = 2 + f;
        if (f == 3) RemBob(&a.bob);
        DrawGList(&rp, 0);
        rows(f & 1 ? "DBUF B" : "DBUF A");
        out("DBUF flags %04x %04x save=%d buf=%d BufX %d BufY %d\n", (UWORD)a.vs.Flags, (UWORD)a.bob.Flags,
            a.bob.SaveBuffer == a.save, a.db.BufBuffer == a.buf, a.db.BufX, a.db.BufY);
    }
    rp.BitMap = bm;
    tbob_free(&a);
}

static char clog[512];
static int clen;
static void c_border(struct VSprite *v, WORD flags) { clen += sprintf(clog + clen, " B%d:%d", v->Y, flags); }
static void c_pair1(struct VSprite *a, struct VSprite *b) { clen += sprintf(clog + clen, " P1:%d,%d", a->Y, b->Y); }
static void c_pair2(struct VSprite *a, struct VSprite *b) { clen += sprintf(clog + clen, " P2:%d,%d", a->Y, b->Y); }

static void s_collision(void)
{
    static struct tbob t[4];
    int i;
    gels_setup();
    gi.leftmost = 4;
    gi.rightmost = 40;
    gi.topmost = 2;
    gi.bottommost = 20;
    for (i = 0; i < 4; i++) {
        tbob_make(&t[i], 1, 6, 1, 1, 0, SAVEBACK, 10 + i);
        t[i].vs.X = (WORD)(i * 9);
        t[i].vs.Y = (WORD)(i * 4);
        t[i].vs.MeMask = (WORD)(i & 1 ? 0x0006 : 0x0002);
        t[i].vs.HitMask = (WORD)(i & 1 ? 0x0003 : 0x0007);
        AddBob(&t[i].bob, &rp);
    }
    t[3].vs.Y = 19;
    SetCollision(0, (VOID (*)())c_border, &gi);
    SetCollision(1, (VOID (*)())c_pair1, &gi);
    SetCollision(2, (VOID (*)())c_pair2, &gi);
    SortGList(&rp);
    clen = 0; clog[0] = 0;
    DoCollision(&rp);
    out("DOCOLLISION%s\n", clog);
    /* a CollMask of 0 */
    t[1].vs.CollMask = 0;
    clen = 0; clog[0] = 0;
    DoCollision(&rp);
    out("DOCOLLISION nomask%s\n", clog);
    t[1].vs.CollMask = t[1].mask;
    for (i = 0; i < 4; i++) RemVSprite(&t[i].vs);
    for (i = 0; i < 4; i++) tbob_free(&t[i]);
}

static char alog[512];
static int alen;
static WORD __STDARGS__ a_comp(struct AnimComp *c) { alen += sprintf(alog + alen, " C%d", c->XTrans); return 0; }
static WORD __STDARGS__ a_ob(struct AnimOb *o) { alen += sprintf(alog + alen, " O%d", o->AnX); return 0; }

static void s_anim(void)
{
    static struct tbob t[3];
    static struct AnimComp c[3];
    static struct AnimOb ob;
    struct AnimOb *key = 0;
    int i, f;
    gels_setup();
    memset(&ob, 0, sizeof ob);
    memset(c, 0, sizeof c);
    ob.AnX = 10 << 6 | 33;
    ob.AnY = -(3 << 6) - 5;
    ob.XVel = 70; ob.YVel = -9; ob.XAccel = -3; ob.YAccel = 4;
    ob.RingXTrans = 5; ob.RingYTrans = -2;
    ob.AnimORoutine = a_ob;   /* __STDARGS__ as gels.h's pointer (GCC 16: no cast between the two) */
    for (i = 0; i < 3; i++) {
        tbob_make(&t[i], 1, 3, 1, 1, 0, SAVEBACK, 20 + i);
        c[i].AnimBob = &t[i].bob;
        t[i].bob.BobComp = &c[i];
        t[i].bob.Flags = BOBISCOMP;
        c[i].TimeSet = (WORD)(i + 1);
        c[i].XTrans = (WORD)(i * 2 - 1);
        c[i].YTrans = (WORD)i;
        c[i].AnimCRoutine = a_comp;
    }
    c[0].NextSeq = &c[1]; c[1].NextSeq = &c[0]; c[1].PrevSeq = &c[0]; c[0].PrevSeq = &c[1];
    c[1].Flags = RINGTRIGGER;
    c[0].NextComp = &c[2]; c[2].PrevComp = &c[0];
    c[2].NextSeq = &c[2]; c[2].PrevSeq = &c[2];
    ob.HeadComp = &c[0];
    AddAnimOb(&ob, &key, &rp);
    out("ADDANIMOB key=%d timers %d %d %d headob %d %d\n", key == &ob, c[0].Timer, c[1].Timer, c[2].Timer,
        c[0].HeadOb == &ob, c[2].HeadOb == &ob);
    list("ANIM LIST");
    for (f = 0; f < 6; f++) {
        alen = 0; alog[0] = 0;
        Animate(&key, &rp);
        out("ANIMATE%s | clock %ld An %d,%d Vel %d,%d Old %d,%d head %d timers %d %d %d bobs", alog, ob.Clock,
            ob.AnX, ob.AnY, ob.XVel, ob.YVel, ob.AnOldX, ob.AnOldY, (int)(ob.HeadComp - c), c[0].Timer, c[1].Timer,
            c[2].Timer);
        for (i = 0; i < 3; i++) out(" %d,%d:%04x", t[i].vs.X, t[i].vs.Y, (UWORD)t[i].bob.Flags);
        out("\n");
    }
    list("ANIM LIST AFTER");
    /* GetGBuffers' buffers */
    {
        struct Bob *bb = &t[2].bob;
        WORD *s0 = bb->SaveBuffer;
        LONG r = GetGBuffers(&ob, &rp, TRUE);
        out("GETGBUFFERS %ld shadow=%d chip=%d coll=%d line=%d save=%d dbuf=%d buf=%d same=%d\n", r,
            bb->ImageShadow != 0, bb->ImageShadow ? (TypeOfMem(bb->ImageShadow) & MEMF_CHIP) != 0 : -1,
            t[2].vs.CollMask != 0, t[2].vs.BorderLine != 0, bb->SaveBuffer != 0 && bb->SaveBuffer != s0,
            bb->DBuffer != 0, bb->DBuffer && bb->DBuffer->BufBuffer, bb->ImageShadow == t[2].vs.CollMask);
        if (bb->DBuffer) hexdump("GETGBUFFERS dbufpacket", bb->DBuffer, 8);
        if (r) FreeGBuffers(&ob, &rp, TRUE);
        out("FREEGBUFFERS %d %d %d\n", bb->ImageShadow == 0, bb->SaveBuffer == 0, bb->DBuffer == 0);
        for (i = 0; i < 3; i++) {
            t[i].bob.SaveBuffer = t[i].save;
            t[i].bob.ImageShadow = t[i].vs.CollMask = t[i].mask;
            t[i].vs.BorderLine = t[i].line;
            t[i].bob.DBuffer = 0;
        }
    }
    for (i = 0; i < 3; i++) if (t[i].vs.PrevVSprite) RemVSprite(&t[i].vs);
    for (i = 0; i < 3; i++) tbob_free(&t[i]);
}

/* ---- blits ---------------------------------------------------------------- */

static void s_bltclear(void)
{
    static const ULONG cases[][2] = { {0, 0}, {7, 0}, {8, 1}, {10, 4}, {(3 << 16) | 5, 2}, {(2 << 16) | 6, 6},
                                      {200, 5}, {(0 << 16) | 4, 2} };
    UBYTE *chip = AllocMem(256, MEMF_CHIP), *fast = AllocMem(256, MEMF_FAST);
    int i, k;
    for (i = 0; i < (int)(sizeof cases / sizeof cases[0]); i++) {
        for (k = 0; k < 2; k++) {
            UBYTE *p = k ? fast : chip;
            int j, n = 0;
            if (!p) continue;
            memset(p, 0x5A, 256);
            BltClear(p, cases[i][0], cases[i][1]);
            WaitBlit();
            out("BLTCLEAR %lx %lu %s:", cases[i][0], cases[i][1], k ? "fast" : "chip");
            for (j = 0; j < 256; j++) if (p[j] != 0x5A) n = j + 1;
            out(" up to %d", n);
            for (j = 0; j < n && j < 24; j++) out(" %02x", p[j]);
            out("\n");
        }
    }
    if (chip) FreeMem(chip, 256);
    if (fast) FreeMem(fast, 256);
}

static void s_template(void)
{
    static const UWORD tpl[] = { 0xF0F0, 0x3C3C, 0x0FF0, 0x8001, 0xAAAA, 0x5555, 0xFFFF, 0x0000,
                                 0x1234, 0x5678, 0x9ABC, 0xDEF0, 0x0F0F, 0xC3C3, 0x7E7E, 0x1818 };
    UWORD *t = AllocMem(sizeof tpl, MEMF_CHIP);
    int mode;
    memcpy(t, tpl, sizeof tpl);
    for (mode = 0; mode < 8; mode++) {
        char tag[40];
        SetRast(&rp, 0);
        pattern();
        SetABPenDrMd(&rp, 5, 2, mode);
        BltTemplate((PLANEPTR)t, 3, 4, &rp, 7, 2, 25, 7);
        BltTemplate((PLANEPTR)t, 18, 4, &rp, 33, 12, 9, 5);
        sprintf(tag, "BLTTEMPLATE mode %d", mode);
        rows(tag);
    }
    SetDrMd(&rp, JAM1);
    SetWriteMask(&rp, 0x05);
    SetAPen(&rp, 7);
    BltTemplate((PLANEPTR)t, 0, 4, &rp, -5, -3, 20, 8);
    rows("BLTTEMPLATE mask 5, off the top left");
    SetWriteMask(&rp, 0xFF);
    FreeMem(t, sizeof tpl);
}

static void s_maskblt(void)
{
    static const UBYTE mts[] = { 0xE0, 0x20, 0xC0, 0x60, 0x80, 0x30, 0xCA };
    struct BitMap *src = AllocBitMap(48, 12, D, BMF_CLEAR, 0);
    UBYTE *mask = AllocMem(6 * 12, MEMF_CHIP);
    struct RastPort srp;
    int i, x, y;
    if (!src || !mask) goto done;
    InitRastPort(&srp);
    srp.BitMap = src;
    for (y = 0; y < 12; y++)
        for (x = 0; x < 48; x++) {
            SetAPen(&srp, (x * 3 + y * 5) % 8);
            WritePixel(&srp, x, y);
        }
    for (i = 0; i < 6 * 12; i++) mask[i] = (UBYTE)(0xC3 ^ (i * 37));
    for (i = 0; i < (int)sizeof mts; i++) {
        char tag[40];
        SetRast(&rp, 0);
        pattern();
        BltMaskBitMapRastPort(src, 5, 1, &rp, 11, 3, 30, 9, mts[i], mask);
        sprintf(tag, "BLTMASKBITMAPRASTPORT %02x", mts[i]);
        rows(tag);
    }
    SetRast(&rp, 0);
    pattern();
    BltMaskBitMapRastPort(src, -4, -2, &rp, 50, 20, 30, 9, 0xE0, mask);
    rows("BLTMASKBITMAPRASTPORT clipped");
done:
    WaitBlit();
    if (mask) FreeMem(mask, 6 * 12);
    if (src) FreeBitMap(src);
}

/* ---- the queue -------------------------------------------------------------- */
struct qnode {
    struct bltnode bn;
    int id, calls, again;
    ULONG a0, beam, cleanups;
    UWORD *dest;
};
static char qlog[512];
static int qlen;

static LONG q_fn(register APTR custom __asm("a0"), register struct qnode *q __asm("a1"))
{
    volatile UWORD *c = (volatile UWORD *)custom;
    q->a0 = (ULONG)custom;
    q->beam = VBeamPos();
    qlen += sprintf(qlog + qlen, " C%d", q->id);
    c[0x40 / 2] = 0x01F0;
    c[0x42 / 2] = 0;
    c[0x44 / 2] = 0xFFFF;
    c[0x46 / 2] = 0xFFFF;
    c[0x74 / 2] = (UWORD)(0x1000 * q->id + q->calls);
    *(volatile ULONG *)(c + 0x54 / 2) = (ULONG)q->dest;
    c[0x66 / 2] = 0;
    c[0x58 / 2] = (UWORD)(1 << 6 | 8);
    return q->calls++ < q->again;
}
static void q_cleanup(register APTR custom __asm("a0"), register struct qnode *q __asm("a1"))
{
    (void)custom;
    qlen += sprintf(qlog + qlen, " X%d", q->id);
    q->cleanups++;
}

static void s_queue(void)
{
    static struct qnode q[3];
    UWORD *dest = AllocMem(64, MEMF_CHIP | MEMF_CLEAR);
    int i;
    if (!dest) return;
    for (i = 0; i < 3; i++) {
        memset(&q[i], 0, sizeof q[i]);
        q[i].id = i;
        q[i].again = i;
        q[i].dest = dest + 8 * i;
        /* the OS calls them with A0 and A1: through void (*)(void), GCC 16's way to cast between function types */
        q[i].bn.function = (int (*)())(void (*)(void))q_fn;
        q[i].bn.cleanup = (int (*)())(void (*)(void))q_cleanup;
        q[i].bn.stat = i == 1 ? 0 : CLEANUP;
    }
    qlen = 0; qlog[0] = 0;
    OwnBlitter();
    QBlit(&q[0].bn);
    QBlit(&q[1].bn);
    qlen += sprintf(qlog + qlen, " own");
    DisownBlitter();
    qlen += sprintf(qlog + qlen, " disowned");
    WaitTOF();
    WaitTOF();
    out("QBLIT%s\n", qlog);
    out("QBLIT a0=%lx calls %d %d cleanups %lu %lu\n", q[0].a0, q[0].calls, q[1].calls, q[0].cleanups, q[1].cleanups);
    hexdump("QBLIT dest", dest, 32);
    q[2].bn.beamsync = 150;
    qlen = 0; qlog[0] = 0;
    QBSBlit(&q[2].bn);
    for (i = 0; i < 3; i++) WaitTOF();
    out("QBSBLIT%s ran=%d beam>=150:%d\n", qlog, q[2].calls, q[2].calls ? (int)(q[2].beam >= 150) : -1);
    if (nest) {
        OwnBlitter();
        out("OWN lock %d nest %d owner %d\n", GfxBase->BlitLock, GfxBase->BlitNest, GfxBase->BlitOwner == FindTask(0));
        OwnBlitter();
        out("OWN2 lock %d nest %d owner %d\n", GfxBase->BlitLock, GfxBase->BlitNest, GfxBase->BlitOwner == FindTask(0));
        DisownBlitter();
        out("DISOWN lock %d nest %d owner %d\n", GfxBase->BlitLock, GfxBase->BlitNest, GfxBase->BlitOwner == FindTask(0));
        DisownBlitter();
        out("DISOWN2 lock %d nest %d\n", GfxBase->BlitLock, GfxBase->BlitNest);
    }
    WaitBlit();
    FreeMem(dest, 64);
}

static void run(void)
{
    out_len = 0;
    bm = AllocBitMap(W, H, D, BMF_CLEAR | BMF_DISPLAYABLE, 0);
    bm2 = AllocBitMap(W, H, D, BMF_CLEAR | BMF_DISPLAYABLE, 0);
    if (!bm || !bm2) { out("no bitmaps\n"); goto done; }
    InitRastPort(&rp);
    rp.BitMap = bm;
    s_layout();
    s_initgels();
    s_addvs();
    s_bobs();
    s_dbuf();
    s_collision();
    s_anim();
    s_bltclear();
    s_template();
    s_maskblt();
    s_queue();
done:
    WaitBlit();
    if (bm) FreeBitMap(bm);
    if (bm2) FreeBitMap(bm2);
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
        else if (!strcmp(argv[i], "NEST")) nest = 1;
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
