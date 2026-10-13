/* SPDX-License-Identifier: MIT */
/* OpenGfx text (library/text) under qemu-m68k: every call against the
 * python model's answers (test_gfx_text_model.py, cases.h), the one-colour
 * text the core draws itself checked pixel for pixel against library/core's
 * Text on a planar bitmap, the ROM fonts' data against their drawing, the
 * font list, extensions and memory. With WITH_LVO, also the register
 * entries (text_lvo.c) over a GfxBase and gfx_env like the face's.
 * Copyright (c) 2026 Dalsin Limited. MIT. */
#include <exec/types.h>
#include <exec/lists.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <utility/tagitem.h>
#include <string.h>
#include <stdio.h>
#include "gfx_rp.h"
#include "text_core.h"

struct tfont {
    int lo, hi, ys, base, xs, smear, style, flags, mod, depth;
    const UWORD *loc;
    const WORD *sp, *kn;
    int color;
    const UBYTE *planes[8];
    int pick, onoff, fgcolor, ctflags;
};
struct tcase {
    int font, n;
    const UBYTE *s;
    int algo, spacing, mode, fg, bg, mask, cpx, cpy, len;
    WORD e[6];
    unsigned long hash;
    int cp;
};
struct tfit {
    int font, n;
    const UBYTE *s;
    int idx, algo, spacing, dir;
    WORD con[4];
    int hascon;
    unsigned long bw, bh;
    int cnt;
    WORD e[6];
};
#include "cases.h"

static unsigned long checks, fails;
#define CHECK(c, what, i)                                                                                       \
    do {                                                                                                        \
        checks++;                                                                                               \
        if (!(c) && ++fails < 30) printf("FAIL %s %ld (line %ld)\n", what, (long)(i), (long)__LINE__);        \
    } while (0)

/* ---- the simulated machine ------------------------------------------------ */

#define W 320
#define H 48
static UBYTE grid[H][W];
static UBYTE planes[8][W / 8 * H];
static struct BitMap bm;
static long forbids, allocated, nallocs;
static UBYTE pool[16384];
static unsigned long pool_at;

static void m_forbid(void *ctx) { (void)ctx; forbids++; }
static void m_permit(void *ctx) { (void)ctx; forbids--; }
static void *m_alloc(void *ctx, uint32_t size)
{
    UBYTE *p;
    (void)ctx;
    if (pool_at + size + 4 > sizeof pool) return 0;
    p = pool + pool_at;
    pool_at += (size + 3) & ~3UL;
    memset(p, 0, size);
    allocated += size;
    nallocs++;
    return p;
}
static void m_free(void *ctx, void *p, uint32_t size) { (void)ctx; (void)p; allocated -= size; nallocs--; }
static struct TagItem *m_clone(void *ctx, const struct TagItem *t)
{
    unsigned long n = 0, i;
    struct TagItem *c;
    while (t[n].ti_Tag != TAG_DONE) n++;
    c = m_alloc(ctx, (n + 1) * sizeof *c);
    if (c)
        for (i = 0; i <= n; i++) c[i] = t[i];
    return c;
}
static void m_free_tags(void *ctx, struct TagItem *t)
{
    unsigned long n = 0;
    while (t[n].ti_Tag != TAG_DONE) n++;
    m_free(ctx, t, (n + 1) * sizeof *t);
}
static void m_pixel(void *ctx, struct RastPort *rp, int32_t x, int32_t y, uint32_t pen, int op)
{
    UBYTE m = rp->Mask;
    (void)ctx;
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    if (op) grid[y][x] ^= (UBYTE)(pen & m);
    else grid[y][x] = (UBYTE)((grid[y][x] & ~m) | (pen & m));
}
static long fills;
static int32_t fill_r[4];
static uint32_t fill_pen;
static void m_fill(void *ctx, struct RastPort *rp, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t pen)
{
    (void)ctx; (void)rp;
    fills++;
    fill_r[0] = x0; fill_r[1] = y0; fill_r[2] = x1; fill_r[3] = y1;
    fill_pen = pen;
}
static void m_size(void *ctx, struct RastPort *rp, int32_t *w, int32_t *h) { (void)ctx; (void)rp; *w = W; *h = H; }
static int core_text(void *ctx, struct RastPort *rp, const uint8_t *s, uint32_t n)
{
    (void)ctx;
    return ogfx_rp_Text(rp, (CONST_STRPTR)s, n);
}

static struct List fonts_list;
static struct gt_env env = {0, &fonts_list, m_forbid, m_permit, m_alloc, m_free, m_clone, m_free_tags,
                            0, m_pixel, m_fill, m_size};

static void new_list(struct List *l)
{
    l->lh_Head = (struct Node *)&l->lh_Tail;
    l->lh_Tail = 0;
    l->lh_TailPred = (struct Node *)&l->lh_Head;
}
static long list_len(void)
{
    long n = 0;
    struct Node *p;
    for (p = fonts_list.lh_Head; p->ln_Succ; p = p->ln_Succ) n++;
    return n;
}

static unsigned long fnv(void)
{
    unsigned long h = 0x811C9DC5UL;
    int x, y;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) h = ((h ^ grid[y][x]) * 0x01000193UL) & 0xFFFFFFFFUL;
    return h;
}
static void grid_init(void)
{
    int x, y;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) grid[y][x] = (UBYTE)(x * 7 + y * 13);
}
static void planar_init(void)
{
    int x, y, p;
    memset(planes, 0, sizeof planes);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            for (p = 0; p < 8; p++)
                if ((x * 7 + y * 13) >> p & 1) planes[p][y * (W / 8) + x / 8] |= (UBYTE)(0x80 >> (x & 7));
}
static int planar_same(void)
{
    int x, y, p;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            int v = 0;
            for (p = 0; p < 8; p++) v |= (planes[p][y * (W / 8) + x / 8] >> (7 - (x & 7)) & 1) << p;
            if (v != grid[y][x]) return 0;
        }
    return 1;
}

/* ---- fonts ------------------------------------------------------------------ */

static struct ColorTextFont made[NFONTS];
static struct TextFont *topaz8, *topaz9;

static struct TextFont *font_of(int i)
{
    return i == -1 ? topaz8 : i == -2 ? topaz9 : &made[i].ctf_TF;
}

static void make_fonts(void)
{
    int i, p;
    for (i = 0; i < NFONTS; i++) {
        const struct tfont *f = fonts[i];
        struct ColorTextFont *c = &made[i];
        struct TextFont *tf = &c->ctf_TF;
        memset(c, 0, sizeof *c);
        tf->tf_YSize = (UWORD)f->ys;
        tf->tf_Style = (UBYTE)f->style;
        tf->tf_Flags = (UBYTE)f->flags;
        tf->tf_XSize = (UWORD)f->xs;
        tf->tf_Baseline = (UWORD)f->base;
        tf->tf_BoldSmear = (UWORD)f->smear;
        tf->tf_LoChar = (UBYTE)f->lo;
        tf->tf_HiChar = (UBYTE)f->hi;
        tf->tf_CharData = (APTR)f->planes[0];
        tf->tf_Modulo = (UWORD)f->mod;
        tf->tf_CharLoc = (APTR)f->loc;
        tf->tf_CharSpace = (APTR)f->sp;
        tf->tf_CharKern = (APTR)f->kn;
        if (f->color) {
            c->ctf_Flags = (UWORD)f->ctflags;
            c->ctf_Depth = (UBYTE)f->depth;
            c->ctf_FgColor = (UBYTE)f->fgcolor;
            c->ctf_PlanePick = (UBYTE)f->pick;
            c->ctf_PlaneOnOff = (UBYTE)f->onoff;
            for (p = 0; p < f->depth; p++) c->ctf_CharData[p] = (APTR)f->planes[p];
        }
    }
}

static void rp_for(struct RastPort *rp, const struct tcase *c)
{
    memset(rp, 0, sizeof *rp);
    gt_SetFont(&env, rp, font_of(c->font));
    rp->AlgoStyle = (UBYTE)c->algo;
    rp->TxSpacing = (WORD)c->spacing;
    rp->DrawMode = (BYTE)c->mode;
    rp->FgPen = (BYTE)c->fg;
    rp->BgPen = (BYTE)c->bg;
    rp->Mask = (UBYTE)c->mask;
    rp->cp_x = (WORD)c->cpx;
    rp->cp_y = (WORD)c->cpy;
}

static int same_te(const struct TextExtent *te, const WORD *e)
{
    return te->te_Width == e[0] && te->te_Height == e[1] && te->te_Extent.MinX == e[2] &&
           te->te_Extent.MinY == e[3] && te->te_Extent.MaxX == e[4] && te->te_Extent.MaxY == e[5];
}

static void test_text(void)
{
    int i, p;
    struct RastPort rp;
    struct TextExtent te;
    for (i = 0; i < NCASES; i++) {
        const struct tcase *c = &cases[i];
        struct TextFont *tf = font_of(c->font);
        rp_for(&rp, c);
        CHECK(gt_TextLength(&env, &rp, c->s, (uint32_t)c->n) == c->len, "TextLength", i);
        gt_TextExtent(&env, &rp, c->s, (uint32_t)c->n, &te);
        CHECK(same_te(&te, c->e), "TextExtent", i);
        grid_init();
        gt_Text(&env, &rp, c->s, (uint32_t)c->n);
        CHECK(fnv() == c->hash, "Text pixels", i);
        CHECK(rp.cp_x == c->cp, "Text cp_x", i);
        if (tf->tf_Style & FSF_COLORFONT || tf->tf_Flags & FPF_REVPATH) continue;
        /* the core's own Text, and its measures, on a planar bitmap */
        {
            struct gt_env core = env;
            struct TextExtent ce;
            core.mono_text = core_text;
            core.write_pixel = 0;       /* the core must take it all */
            rp_for(&rp, c);
            bm.BytesPerRow = W / 8;
            bm.Rows = H;
            bm.Depth = 8;
            for (p = 0; p < 8; p++) bm.Planes[p] = planes[p];
            rp.BitMap = &bm;
            planar_init();
            CHECK(ogfx_rp_TextLength(&rp, (CONST_STRPTR)c->s, (ULONG)c->n) == c->len, "core TextLength", i);
            ogfx_rp_TextExtent(&rp, (CONST_STRPTR)c->s, (ULONG)c->n, &ce);
            CHECK(same_te(&ce, c->e), "core TextExtent", i);
            gt_Text(&core, &rp, c->s, (uint32_t)c->n);
            CHECK(rp.cp_x == c->cp, "core Text cp_x", i);
            CHECK(planar_same(), "core Text pixels", i);
        }
    }
}

static void test_fit(void)
{
    int i;
    struct RastPort rp;
    struct TextExtent te, con;
    for (i = 0; i < NFITS; i++) {
        const struct tfit *t = &fits[i];
        uint32_t k;
        memset(&rp, 0, sizeof rp);
        gt_SetFont(&env, &rp, font_of(t->font));
        rp.AlgoStyle = (UBYTE)t->algo;
        rp.TxSpacing = (WORD)t->spacing;
        con.te_Width = con.te_Height = 0;
        con.te_Extent.MinX = t->con[0];
        con.te_Extent.MinY = t->con[1];
        con.te_Extent.MaxX = t->con[2];
        con.te_Extent.MaxY = t->con[3];
        k = gt_TextFit(&env, &rp, t->s + t->idx, (uint32_t)t->n, &te, t->hascon ? &con : 0, t->dir, t->bw, t->bh);
        CHECK(k == (uint32_t)t->cnt, "TextFit count", i);
        CHECK(same_te(&te, t->e), "TextFit extent", i);
    }
}

static void test_measures(void)
{
    int i;
    struct TextExtent te;
    for (i = 0; i < NFONTS + 2; i++) {
        gt_FontExtent(&env, i < NFONTS ? font_of(i) : font_of(NFONTS - 1 - i), &te);
        CHECK(same_te(&te, fextents[i]), "FontExtent", i);
    }
    for (i = 0; i < NWEIGHS; i++) {
        const LONG *w = weighs[i];
        struct TagItem rt[3] = {{TAG_IGNORE, 0}, {TA_DeviceDPI, (ULONG)w[3]}, {TAG_DONE, 0}};
        struct TagItem tt[2] = {{TA_DeviceDPI, (ULONG)w[7]}, {TAG_DONE, 0}};
        struct TTextAttr req = {0, (UWORD)w[0], (UBYTE)w[1], (UBYTE)w[2], 0};
        struct TextAttr tgt = {0, (UWORD)w[4], (UBYTE)w[5], (UBYTE)w[6]};
        if (w[3] != -1) {
            req.tta_Style |= FSF_TAGGED;
            req.tta_Tags = rt;
        }
        CHECK(gt_WeighTAMatch(&env, (struct TextAttr *)&req, &tgt, w[7] != -1 ? tt : 0) == w[8], "WeighTAMatch", i);
    }
}

static void test_topaz(void)
{
    int s, g, r;
    for (s = 0; s < 2; s++) {
        struct TextFont *tf = s ? topaz9 : topaz8;
        const UWORD *art = s ? topaz9_art : topaz8_art;
        const UWORD *loc = tf->tf_CharLoc;
        CHECK(tf->tf_LoChar == 32 && tf->tf_HiChar == 255 && !tf->tf_CharSpace && !tf->tf_CharKern, "topaz", s);
        CHECK(tf->tf_Modulo % 2 == 0, "topaz modulo even", s);
        for (g = 0; g < 225; g++) {
            CHECK(loc[2 * g + 1] == tf->tf_XSize, "topaz width", g);
            CHECK((loc[2 * g] + loc[2 * g + 1] + 7) / 8 <= tf->tf_Modulo, "topaz in modulo", g);
            for (r = 0; r < tf->tf_YSize; r++) {
                const UBYTE *row = (const UBYTE *)tf->tf_CharData + r * tf->tf_Modulo;
                UWORD v = 0;
                int x;
                for (x = 0; x < loc[2 * g + 1]; x++) {
                    int b = loc[2 * g] + x;
                    if (row[b / 8] >> (7 - b % 8) & 1) v |= (UWORD)(0x8000 >> x);
                }
                CHECK(v == art[g * tf->tf_YSize + r], "topaz glyph", g * 100 + r);
            }
        }
    }
}

static void test_list(void)
{
    static struct TextFont lf[10];
    struct TextAttr ta;
    struct TextFont *got;
    int i;
    long n0;
    gt_RemFont(&env, topaz8);   /* the model's list has only these */
    gt_RemFont(&env, topaz9);
    n0 = list_len();
    CHECK(n0 == 0, "RemFont ROM fonts", 0);
    for (i = 0; i < 10; i++) {
        memset(&lf[i], 0, sizeof lf[i]);
        lf[i].tf_Message.mn_Node.ln_Name = (char *)names[ofonts[i][0]];
        lf[i].tf_YSize = ofonts[i][1];
        lf[i].tf_Style = ofonts[i][2];
        lf[i].tf_Flags = ofonts[i][3] | FPF_REMOVED;
        lf[i].tf_CharLoc = (APTR)fonts[0]->loc;
        gt_AddFont(&env, &lf[i]);
        CHECK(!(lf[i].tf_Flags & FPF_REMOVED) && lf[i].tf_Message.mn_Node.ln_Type == 12, "AddFont", i);
    }
    gt_AddFont(&env, &lf[3]);   /* twice: stays once */
    CHECK(list_len() == n0 + 10, "AddFont twice", 0);
    for (i = 0; i < NOREQS; i++) {
        ta.ta_Name = (STRPTR)names[oreqs[i][0]];
        ta.ta_YSize = (UWORD)oreqs[i][1];
        ta.ta_Style = (UBYTE)oreqs[i][2];
        ta.ta_Flags = (UBYTE)oreqs[i][3];
        got = gt_OpenFont(&env, &ta);
        if (oreqs[i][4] < 0)
            CHECK(!got, "OpenFont none", i);
        else
            CHECK(got == &lf[oreqs[i][4]], "OpenFont", i);
        if (got) {
            UWORD a = got->tf_Accessors;
            gt_CloseFont(&env, got);
            CHECK(got->tf_Accessors == a - 1, "CloseFont", i);
        }
    }
    gt_CloseFont(&env, &lf[0]);
    CHECK(lf[0].tf_Accessors == 0, "CloseFont floor", 0);
    gt_CloseFont(&env, 0);
    ta.ta_Name = 0;
    CHECK(!gt_OpenFont(&env, &ta), "OpenFont no name", 0);

    /* extensions */
    {
        struct TagItem tags[2] = {{TA_DeviceDPI, 0x00480048}, {TAG_DONE, 0}};
        struct TextFontExtension *e;
        static UWORD portmem[20];   /* a reply port's memory: not a match word */
        struct MsgPort *port = (struct MsgPort *)portmem;
        long a0 = allocated;
        lf[1].tf_Message.mn_ReplyPort = port;
        CHECK(gt_ExtendFont(&env, &lf[1], tags) == 1, "ExtendFont", 0);
        e = (struct TextFontExtension *)lf[1].tf_Message.mn_ReplyPort;
        CHECK(e->tfe_MatchWord == 0xF00D && e->tfe_BackPtr == &lf[1] && e->tfe_OrigReplyPort == port, "ExtendFont ext", 0);
        CHECK(e->tfe_Tags && e->tfe_Tags != tags && e->tfe_Tags[0].ti_Data == 0x00480048, "ExtendFont tags", 0);
        CHECK(gt_ExtendFont(&env, &lf[1], 0) == 1 && lf[1].tf_Message.mn_ReplyPort == (struct MsgPort *)e, "ExtendFont again", 0);
        e->tfe_Flags0 |= TE0F_NOREMFONT;
        CHECK(gt_RemFont(&env, &lf[1]) == 0 && list_len() == n0 + 10, "RemFont NOREMFONT", 0);
        e->tfe_Flags0 = 0;
        gt_StripFont(&env, &lf[1]);
        CHECK(lf[1].tf_Message.mn_ReplyPort == port && allocated == a0, "StripFont", 0);
        gt_StripFont(&env, &lf[1]);
        CHECK(lf[1].tf_Message.mn_ReplyPort == port, "StripFont twice", 0);
        lf[1].tf_Message.mn_ReplyPort = 0;
    }
    for (i = 0; i < 10; i++) {
        CHECK(gt_RemFont(&env, &lf[i]) == 1 && lf[i].tf_Flags & FPF_REMOVED, "RemFont", i);
        CHECK(gt_RemFont(&env, &lf[i]) == 0, "RemFont twice", i);
    }
    CHECK(list_len() == n0, "RemFont all", 0);
    gt_AddFont(&env, topaz9);
    gt_AddFont(&env, topaz8);
    CHECK(list_len() == 2 && !(topaz8->tf_Flags & FPF_REMOVED), "AddFont ROM fonts back", 0);
    CHECK(forbids == 0, "Forbid/Permit pairs", 0);
}

static void test_rastport(void)
{
    struct RastPort rp;
    struct TextAttr ta;
    memset(&rp, 0, sizeof rp);
    CHECK(gt_SetFont(&env, &rp, 0) == 0 && !rp.Font, "SetFont null", 0);
    CHECK(gt_AskSoftStyle(&env, &rp) == 0, "AskSoftStyle no font", 0);
    CHECK(gt_TextLength(&env, &rp, (const uint8_t *)"ab", 2) == 0, "TextLength no font", 0);
    rp.AlgoStyle = FSF_ITALIC;
    CHECK(gt_SetFont(&env, &rp, topaz9) == 1 && rp.Font == topaz9 && rp.TxHeight == 9 && rp.TxWidth == 10 &&
              rp.TxBaseline == 7 && rp.AlgoStyle == FSF_ITALIC, "SetFont", 0);
    CHECK(gt_AskSoftStyle(&env, &rp) == 7, "AskSoftStyle", 0);
    topaz9->tf_Style = FSF_BOLD;
    CHECK(gt_AskSoftStyle(&env, &rp) == 5, "AskSoftStyle bold font", 0);
    CHECK(gt_SetSoftStyle(&env, &rp, FSF_UNDERLINED | FSF_BOLD, 0xFF) == (FSF_UNDERLINED | FSF_BOLD) &&
              rp.AlgoStyle == FSF_UNDERLINED, "SetSoftStyle", 0);
    CHECK(gt_SetSoftStyle(&env, &rp, 0, FSF_ITALIC) == (FSF_UNDERLINED | FSF_BOLD) && rp.AlgoStyle == FSF_UNDERLINED,
          "SetSoftStyle enable", 0);
    topaz9->tf_Style = FSF_TAGGED;
    gt_AskFont(&env, &rp, &ta);
    CHECK((char *)ta.ta_Name == topaz9->tf_Message.mn_Node.ln_Name && ta.ta_YSize == 9 && ta.ta_Style == FSF_UNDERLINED &&
              ta.ta_Flags == (FPF_ROMFONT | FPF_DESIGNED), "AskFont", 0);
    topaz9->tf_Style = 0;

    /* ClearEOL and ClearScreen */
    rp.cp_x = 30;
    rp.cp_y = 20;
    rp.BgPen = 5;
    rp.DrawMode = JAM1;
    fills = 0;
    gt_ClearEOL(&env, &rp);
    CHECK(fills == 1 && fill_r[0] == 30 && fill_r[1] == 13 && fill_r[2] == W - 1 && fill_r[3] == 21 && fill_pen == 0,
          "ClearEOL", 0);
    rp.DrawMode = JAM2;
    fills = 0;
    gt_ClearScreen(&env, &rp);
    CHECK(fills == 2 && fill_r[0] == 0 && fill_r[1] == 22 && fill_r[2] == W - 1 && fill_r[3] == H - 1 && fill_pen == 5,
          "ClearScreen", 0);
    rp.cp_x = W;
    fills = 0;
    gt_ClearEOL(&env, &rp);
    CHECK(fills == 0, "ClearEOL off the right", 0);
    rp.cp_y = H + 20;
    rp.cp_x = 0;
    gt_ClearScreen(&env, &rp);
    CHECK(fills == 0, "ClearScreen below", 0);
}

#ifdef WITH_LVO
#include "gfx_env.h"
#include <graphics/clip.h>
#include <graphics/layers.h>
struct GfxBase;
/* the other areas' hooks the face calls (gen_stubs.py's stubs) */
void gfx_hook_vp_colors(struct gfx_env *env, void *vp, uint32_t first, uint32_t count)
{
    (void)env; (void)vp; (void)first; (void)count;
}
int32_t gfx_hook_blt_bitmap(struct gfx_env *env, struct gfx_bitmap *src, int32_t sx, int32_t sy,
                            struct gfx_bitmap *dst, int32_t dx, int32_t dy, int32_t w, int32_t h, uint32_t minterm,
                            uint32_t mask, uint8_t *temp)
{
    (void)env; (void)src; (void)sx; (void)sy; (void)dst; (void)dx; (void)dy; (void)w; (void)h; (void)minterm;
    (void)mask; (void)temp;
    return -1;
}
void gfx_hook_wait_blit(struct gfx_env *env) { (void)env; }
void gfx_hook_vsprites(struct gfx_env *env, void *vp, const void *slots, uint32_t n)
{
    (void)env; (void)vp; (void)slots; (void)n;
}
static long sem_depth;
WORD gfx_lvo_TextLength(struct RastPort *, CONST_STRPTR, ULONG, struct GfxBase *);
LONG gfx_lvo_Text(struct RastPort *, CONST_STRPTR, ULONG, struct GfxBase *);
struct TextFont *gfx_lvo_OpenFont(struct TextAttr *, struct GfxBase *);
void gfx_lvo_CloseFont(struct TextFont *, struct GfxBase *);
void gfx_lvo_ClearEOL(struct RastPort *, struct GfxBase *);
static void *g_alloc(void *ctx, uint32_t size, uint32_t f) { (void)f; return m_alloc(ctx, size); }
static void g_obtain(void *ctx, struct gfx_semaphore *s) { (void)ctx; (void)s; sem_depth++; }
static void g_release(void *ctx, struct gfx_semaphore *s) { (void)ctx; (void)s; sem_depth--; }
static UBYTE splanes[8][W / 8 * H];
static struct BitMap sbm;
static int same_planes(UBYTE (*a)[W / 8 * H], UBYTE (*b)[W / 8 * H]) { return !memcmp(a, b, 8 * (W / 8 * H)); }
static UBYTE gbase[GFX_BASE_POSSIZE];
static void test_lvo(void)
{
    struct gfx_base *b = (struct gfx_base *)gbase;
    struct gfx_env *e = gfx_env_of(b);
    struct TextAttr ta = {(STRPTR) "topaz.font", 9, 0, 0};
    struct TextFont *t9;
    struct RastPort rp;
    long a0 = allocated;
    int p;
    e->ctx = 0;
    e->base = b;
    e->alloc = g_alloc;
    e->free = m_free;
    e->forbid = m_forbid;
    e->permit = m_permit;
    e->sem_obtain = g_obtain;
    e->sem_release = g_release;
    e->clone_tags = (struct gfx_tagitem * (*)(void *, const struct gfx_tagitem *)) m_clone;
    e->free_tags = (void (*)(void *, struct gfx_tagitem *))m_free_tags;
    new_list((struct List *)&b->textfonts);
    CHECK(gfx_init_text(e) == 1 && b->defaultfont, "gfx_init_text", 0);
    t9 = gfx_lvo_OpenFont(&ta, (struct GfxBase *)b);
    CHECK(t9 && t9->tf_YSize == 9 && t9->tf_Accessors == 1, "lvo OpenFont", 0);
    memset(&rp, 0, sizeof rp);
    gfx_hook_set_font(e, (struct gfx_rastport *)&rp, b->defaultfont);
    CHECK(rp.Font == b->defaultfont && rp.TxHeight == 8, "gfx_hook_set_font", 0);
    bm.BytesPerRow = W / 8;
    bm.Rows = H;
    bm.Depth = 8;
    for (p = 0; p < 8; p++) bm.Planes[p] = planes[p];
    rp.BitMap = &bm;
    rp.Mask = 0xFF;
    rp.FgPen = 3;
    rp.DrawMode = JAM2;
    rp.cp_x = 10;
    rp.cp_y = 10;
    planar_init();
    CHECK(gfx_lvo_TextLength(&rp, (CONST_STRPTR) "Hi", 2, (struct GfxBase *)b) == 16, "lvo TextLength", 0);
    gfx_lvo_Text(&rp, (CONST_STRPTR) "Hi", 2, (struct GfxBase *)b);
    CHECK(rp.cp_x == 26, "lvo Text", 0);
    gfx_lvo_ClearEOL(&rp, (struct GfxBase *)b);
    {
        int x, y, ok = 1;
        for (y = 4; y <= 11; y++)
            for (x = 26; x < W; x++) {
                int v = 0;
                for (p = 0; p < 8; p++) v |= (planes[p][y * (W / 8) + x / 8] >> (7 - (x & 7)) & 1) << p;
                if (v != 0) ok = 0;
            }
        CHECK(ok, "lvo ClearEOL", 0);
    }
    /* a super bitmap layer: the core's Text does not take it, so the text
     * and ClearEOL go through the face's WritePixel and RectFill onto the
     * super bitmap, the same pixels as on a bare bitmap */
    {
        static struct Layer sl;
        struct RastPort lrp;
        static const UBYTE fonts_try[3] = {0, 4, 7};
        int k;
        memset(&sl, 0, sizeof sl);
        sl.Flags = LAYERSUPER | LAYERSMART;
        sl.SuperBitMap = &sbm;
        sl.bounds.MaxX = 99;
        sl.bounds.MaxY = 29;
        sbm = bm;
        for (p = 0; p < 8; p++) sbm.Planes[p] = splanes[p];
        for (k = 0; k < 3; k++) {
            struct TextFont *f = k ? &made[fonts_try[k]].ctf_TF : b->defaultfont;
            UBYTE s3[3];
            s3[0] = f->tf_LoChar; s3[1] = (UBYTE)(f->tf_LoChar + 1); s3[2] = (UBYTE)(f->tf_HiChar + 1);
            for (p = 0; p < 8; p++) memset(splanes[p], 0x5A, sizeof splanes[p]);
            planar_init();
            for (p = 0; p < 8; p++) memset(planes[p], 0x5A, sizeof planes[p]);
            memset(&lrp, 0, sizeof lrp);
            gfx_hook_set_font(e, (struct gfx_rastport *)&lrp, f);
            lrp.Mask = 0xFF; lrp.FgPen = 3; lrp.BgPen = 9; lrp.DrawMode = JAM2; lrp.AlgoStyle = 7;
            lrp.cp_x = 120; lrp.cp_y = 20;
            rp = lrp;
            rp.BitMap = &bm;
            lrp.Layer = &sl;
            lrp.BitMap = &bm;
            gfx_lvo_Text(&lrp, (CONST_STRPTR)s3, 3, (struct GfxBase *)b);
            gfx_lvo_Text(&rp, (CONST_STRPTR)s3, 3, (struct GfxBase *)b);
            CHECK(lrp.cp_x == rp.cp_x, "super bitmap Text cp_x", k);
            CHECK(same_planes(splanes, planes), "super bitmap Text pixels", k);
            CHECK(sem_depth == 0, "super bitmap layer unlocked", k);
        }
        /* ClearEOL to the super bitmap's right edge */
        lrp.cp_x = 300; lrp.cp_y = 20; lrp.DrawMode = JAM2; lrp.BgPen = 0;
        for (p = 0; p < 8; p++) memset(splanes[p], 0xFF, sizeof splanes[p]);
        gfx_lvo_ClearEOL(&lrp, (struct GfxBase *)b);
        {
            int x, y, ok = 1;
            for (y = 0; y < H; y++)
                for (x = 0; x < W; x++) {
                    int v = 0, y0 = 20 - lrp.TxBaseline, in = x >= 300 && y >= y0 && y <= y0 + lrp.TxHeight - 1;
                    for (p = 0; p < 8; p++) v |= (splanes[p][y * (W / 8) + x / 8] >> (7 - (x & 7)) & 1) << p;
                    if (v != (in ? 0 : 0xFF)) ok = 0;
                }
            CHECK(ok, "super bitmap ClearEOL", 0);
        }
    }
    gfx_lvo_CloseFont(t9, (struct GfxBase *)b);
    gfx_exit_text(e);
    CHECK(allocated == a0 && !b->defaultfont, "gfx_exit_text frees", 0);
    CHECK(forbids == 0, "lvo Forbid/Permit pairs", 0);
}
#endif

int main(void)
{
    long a0;
    new_list(&fonts_list);
    a0 = allocated;
    topaz8 = gt_init_rom_fonts(&env, &topaz9);
    CHECK(topaz8 && topaz9 && list_len() == 2 && fonts_list.lh_Head == &topaz8->tf_Message.mn_Node, "ROM fonts", 0);
    CHECK(topaz8->tf_YSize == 8 && topaz8->tf_XSize == 8 && topaz8->tf_Baseline == 6, "topaz 8", 0);
    make_fonts();
    test_text();
    test_fit();
    test_measures();
    test_topaz();
    test_list();
    test_rastport();
    gt_exit_rom_fonts(&env, topaz8, topaz9);
    CHECK(allocated == a0 && nallocs == 0 && list_len() == 0, "memory back", 0);
#ifdef WITH_LVO
    test_lvo();
#endif
    printf("%lu checks, %lu failed\n", checks, fails);
    return fails != 0;
}
