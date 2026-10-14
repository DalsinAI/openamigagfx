/* SPDX-License-Identifier: MIT */
/* OpenGfx graphics.library's face (library/graphics/gfx_face.cpp) over a
 * test env: memory from an arena that checks every free's size, semaphores
 * that count, hooks called as C. Random calls; each prints a line of what
 * it was given and what it answered or drew, and test_gfx_face.sh's python
 * model, written apart from the library, checks every line. The memory and
 * the locks are checked here. Copyright (c) 2026 Dalsin Limited. MIT. */
#include <stdio.h>
#include "gfx_face.h"
#include "gfx_core.h"

#ifndef ROUNDS
#define ROUNDS 40
#endif

#if !defined(__mc68020__) && !defined(__mc68030__) && !defined(__mc68040__) && !defined(__mc68060__)
/* the 68000 build has rt68000.c, not libgcc: the core's ellipse needs a
 * 64-bit multiply */
long long __muldi3(long long a, long long b);
long long __muldi3(long long a, long long b)
{
    unsigned long long x = (unsigned long long)a, y = (unsigned long long)b, r = 0;
    while (y) { if (y & 1) r += x; x <<= 1; y >>= 1; }
    return (long long)r;
}
#endif

static uint32_t seed = 0x5eed1;
static uint32_t rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }
static uint32_t rn(uint32_t n) { return n ? rnd() % n : 0; }
static int errors;

static void fail(const char *what, long a)
{
    printf("ERR %s %ld\n", what, a);
    errors++;
}

/* ---- the test env ------------------------------------------------------ */

#define ARENA (3u << 20)
#define BLOCKS 4096
static uint8_t arena[ARENA] __attribute__((aligned(8)));
static uint32_t used;
static struct { uint8_t *p; uint32_t size, flags; int live; } blocks[BLOCKS];
static int nblocks, live_blocks, sem_held, hook_calls, colors_calls;
static uint32_t fail_after = 0xFFFFFFFFu;     /* allocations before one fails */

static void *t_alloc(void *ctx, uint32_t size, uint32_t flags)
{
    uint32_t i;
    (void)ctx;
    if (!fail_after) return 0;
    fail_after--;
    if (!size || used + size + 8 > ARENA || nblocks == BLOCKS) return 0;
    blocks[nblocks].p = arena + used;
    blocks[nblocks].size = size;
    blocks[nblocks].flags = flags;
    blocks[nblocks].live = 1;
    nblocks++;
    live_blocks++;
    for (i = 0; i < size; i++) arena[used + i] = (flags & GFX_MEMF_CLEAR) ? 0 : (uint8_t)rnd();
    used = (used + size + 7) & ~7u;
    return blocks[nblocks - 1].p;
}

static void t_free(void *ctx, void *p, uint32_t size)
{
    int i;
    (void)ctx;
    for (i = 0; i < nblocks; i++)
        if (blocks[i].p == p && blocks[i].live) {
            if (blocks[i].size != size) fail("free size", (long)size - (long)blocks[i].size);
            blocks[i].live = 0;
            live_blocks--;
            return;
        }
    fail("free unknown", (long)size);
}

static uint32_t t_flags_of(const void *p)
{
    int i;
    for (i = 0; i < nblocks; i++)
        if (blocks[i].p == p && blocks[i].live) return blocks[i].flags;
    return 0xFFFFFFFFu;
}

static void arena_reset(const char *where)
{
    if (live_blocks) fail(where, live_blocks);
    if (sem_held) fail("locks held", sem_held);
    used = 0;
    nblocks = live_blocks = 0;
    sem_held = 0;
}

static void t_nop(void *ctx) { (void)ctx; }
static void t_sem_init(void *ctx, struct gfx_semaphore *s) { (void)ctx; s->nestcount = 0; }
static void t_sem_obtain(void *ctx, struct gfx_semaphore *s) { (void)ctx; s->nestcount++; sem_held++; }
static int32_t t_sem_attempt(void *ctx, struct gfx_semaphore *s) { t_sem_obtain(ctx, s); return 1; }
static void t_sem_release(void *ctx, struct gfx_semaphore *s)
{
    (void)ctx;
    if (s->nestcount <= 0) fail("release unheld", 0);
    s->nestcount--;
    sem_held--;
}

struct t_hook {                         /* struct Hook: MinNode, h_Entry, h_SubEntry, h_Data */
    struct gfx_minnode node;
    uint32_t (*entry)(struct t_hook *, void *, void *);
    void *sub;
    void *data;
};
static uint32_t t_call_hook(void *ctx, void *hook, void *obj, void *msg)
{
    struct t_hook *h = hook;
    (void)ctx;
    return h->entry(h, obj, msg);
}

static struct gfx_base gbase;
static struct gfx_env env;

/* the display's hook, as display would get it */
void gfx_hook_vp_colors(struct gfx_env *e, void *vp, uint32_t first, uint32_t count)
{
    (void)e; (void)vp;
    colors_calls++;
    printf("VPC %lu %lu\n", (unsigned long)first, (unsigned long)count);
}
void gfx_hook_set_font(struct gfx_env *e, struct gfx_rastport *rp, void *font) { (void)e; rp->font = font; }
int32_t gfx_hook_blt_bitmap(struct gfx_env *e, struct gfx_bitmap *s, int32_t sx, int32_t sy, struct gfx_bitmap *d,
                            int32_t dx, int32_t dy, int32_t w, int32_t h, uint32_t mt, uint32_t m, uint8_t *t)
{
    (void)e; (void)s; (void)sx; (void)sy; (void)d; (void)dx; (void)dy; (void)w; (void)h; (void)mt; (void)m; (void)t;
    return -1;
}
void gfx_hook_wait_blit(struct gfx_env *e) { (void)e; }

static void env_init(void)
{
    env.ctx = &gbase;
    env.base = &gbase;
    env.alloc = t_alloc;
    env.free = t_free;
    env.forbid = env.permit = env.disable = env.enable = t_nop;
    env.sem_init = t_sem_init;
    env.sem_obtain = env.sem_obtain_shared = t_sem_obtain;
    env.sem_attempt = t_sem_attempt;
    env.sem_release = t_sem_release;
    env.call_hook = t_call_hook;
}

/* ---- printing ---------------------------------------------------------------- */

static void print_bm(const char *tag, struct gfx_bitmap *bm, int w, int h)
{
    struct gfx_planar p;
    char row[260];
    int x, y, i;
    for (i = 0; i < 8; i++) p.planes[i] = i < bm->depth ? bm->planes[i] : 0;
    p.bytes_per_row = bm->bytesperrow;
    p.rows = bm->rows;
    p.depth = bm->depth;
    p.interleaved = (bm->flags & GFX_BMF_INTERLEAVED) != 0;
    printf("%s %d %d\n", tag, w, h);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) row[x] = (char)('0' + gfx_planar_get(&p, x, y));
        row[w] = 0;
        printf("R %s\n", row);
    }
}

static void random_bm(struct gfx_bitmap *bm, int w, int h, int pens, int blobs)
{
    struct gfx_planar p;
    int i, x, y, b;
    for (i = 0; i < 8; i++) p.planes[i] = i < bm->depth ? bm->planes[i] : 0;
    p.bytes_per_row = bm->bytesperrow;
    p.rows = bm->rows;
    p.depth = bm->depth;
    p.interleaved = (bm->flags & GFX_BMF_INTERLEAVED) != 0;
    gfx_planar_fill_all(&p, 0, 0xFF);
    for (b = 0; b < blobs; b++) {
        int x0 = (int)rn((uint32_t)w), y0 = (int)rn((uint32_t)h), x1 = x0 + (int)rn(12), y1 = y0 + (int)rn(8);
        uint32_t pen = rn((uint32_t)pens);
        if (rn(3) == 0) { /* a line */
            for (x = x0; x <= x1 && x < w; x++) gfx_planar_put(&p, x, y0, pen, 0xFF, GFX_SET);
            continue;
        }
        for (y = y0; y <= y1 && y < h; y++)
            for (x = x0; x <= x1 && x < w; x++)
                if (rn(5)) gfx_planar_put(&p, x, y, pen, 0xFF, GFX_SET);
    }
}

/* ---- the sections --------------------------------------------------------------- */

static void t_bitmaps(void)
{
    int r;
    for (r = 0; r < ROUNDS * 4; r++) {
        uint32_t w = 1 + rn(700), h = 1 + rn(300), d = rn(10), f = rn(32);
        int aa = (int)rn(2), k, inj = rn(12) == 0;
        struct gfx_bitmap *bm;
        gbase.chiprevbits0 = aa ? GFX_GFXF_AA_ALICE | GFX_GFXF_AA_LISA : GFX_GFXF_HR_AGNUS;
        if (inj) fail_after = rn(4);
        bm = gfx_AllocBitMap(&env, w, h, d, f, 0);
        fail_after = 0xFFFFFFFFu;
        if (!bm) { printf("BM %lu %lu %lu %lu %d %d none\n", (unsigned long)w, (unsigned long)h, (unsigned long)d, (unsigned long)f, aa, inj); arena_reset("bm fail"); continue; }
        {
            int contiguous = 1, chip = 1, clear = 1;
            uint32_t pl = bm->bytesperrow / (bm->flags & GFX_BMF_INTERLEAVED ? bm->depth : 1);
            for (k = 0; k < bm->depth; k++) {
                if (k && bm->planes[k] != bm->planes[0] + pl * (uint32_t)k) contiguous = 0;
                if (!(bm->flags & GFX_BMF_INTERLEAVED) && !(t_flags_of(bm->planes[k]) & GFX_MEMF_CHIP)) chip = 0;
                if ((f & GFX_BMF_CLEAR) && bm->planes[k][0] | bm->planes[k][pl - 1]) clear = 0;
            }
            for (k = bm->depth; k < 8; k++) if (bm->planes[k]) contiguous = 0, chip = 0;
            printf("BM %lu %lu %lu %lu %d %d %u %u %u %u %d %d %d %lu %lu %lu %lu\n", (unsigned long)w, (unsigned long)h,
                   (unsigned long)d, (unsigned long)f, aa, inj, bm->bytesperrow, bm->rows, bm->depth, bm->flags,
                   (bm->flags & GFX_BMF_INTERLEAVED) ? contiguous : 1, chip, clear,
                   (unsigned long)gfx_GetBitMapAttr(&env, bm, GFX_BMA_WIDTH),
                   (unsigned long)gfx_GetBitMapAttr(&env, bm, GFX_BMA_HEIGHT),
                   (unsigned long)gfx_GetBitMapAttr(&env, bm, GFX_BMA_DEPTH),
                   (unsigned long)gfx_GetBitMapAttr(&env, bm, GFX_BMA_FLAGS));
        }
        gfx_FreeBitMap(&env, bm);
        arena_reset("bitmap leak");
    }
    for (r = 0; r < ROUNDS; r++) {
        uint32_t w = rn(1000), h = rn(400);
        uint8_t *p = gfx_AllocRaster(&env, w, h);
        struct gfx_bitmap bm;
        uint32_t sz = 0;
        int i;
        for (i = 0; i < nblocks; i++) if (blocks[i].p == p) sz = blocks[i].size;
        printf("RAS %lu %lu %lu %d\n", (unsigned long)w, (unsigned long)h, (unsigned long)(p ? sz : 0),
               p ? (t_flags_of(p) & GFX_MEMF_CHIP) != 0 : 0);
        gfx_FreeRaster(&env, p, w, h);
        arena_reset("raster leak");
        bm.planes[3] = (uint8_t *)0x1234;
        gfx_InitBitMap(&env, &bm, (int32_t)rn(9), (int32_t)w, (int32_t)h);
        printf("IBM %lu %lu %u %u %u %u %d\n", (unsigned long)w, (unsigned long)h, bm.depth, bm.bytesperrow, bm.rows,
               bm.flags, bm.planes[3] == (uint8_t *)0x1234);
    }
}

static void t_rastport(void)
{
    struct gfx_rastport rp;
    struct gfx_tagitem tags[12], more[3];
    uint32_t got[8];
    int r, i;
    struct gfx_bitmap bm;

    gbase.defaultfont = (void *)0x4000;
    gfx_InitRastPort(&env, &rp);
    printf("IRP");
    for (i = 0; i < 100; i++) printf(" %u", ((uint8_t *)&rp)[i]);
    printf("\n");
    gfx_InitBitMap(&env, &bm, 3, 64, 8);
    rp.bitmap = &bm;
    for (r = 0; r < ROUNDS * 4; r++) {
        uint32_t op = rn(10), v = rnd(), w = rnd(), x = rn(8);
        switch (op) {
        case 0: gfx_SetAPen(&env, &rp, v); printf("SETA %lu\n", (unsigned long)v); break;
        case 1: gfx_SetBPen(&env, &rp, v); printf("SETB %lu\n", (unsigned long)v); break;
        case 2: gfx_SetDrMd(&env, &rp, x); printf("SETD %lu\n", (unsigned long)x); break;
        case 3: printf("SETO %lu %lu\n", (unsigned long)v, (unsigned long)gfx_SetOutlinePen(&env, &rp, v)); break;
        case 4: printf("SETM %lu %lu\n", (unsigned long)v, (unsigned long)gfx_SetWriteMask(&env, &rp, v)); break;
        case 5: gfx_SetABPenDrMd(&env, &rp, v, w, x); printf("SETABD %lu %lu %lu\n", (unsigned long)v, (unsigned long)w, (unsigned long)x); break;
        case 6:
            printf("GET %lu %lu %lu %lu %u %u\n", (unsigned long)gfx_GetAPen(&env, &rp), (unsigned long)gfx_GetBPen(&env, &rp),
                   (unsigned long)gfx_GetDrMd(&env, &rp), (unsigned long)gfx_GetOutlinePen(&env, &rp), rp.mask, rp.flags);
            break;
        default: {
            /* a tag list: some of every tag, with IGNORE, SKIP and MORE */
            static const uint32_t ids[] = {GFX_RPTAG_APEN, GFX_RPTAG_BPEN, GFX_RPTAG_DRMD, GFX_RPTAG_OUTLINEPEN,
                                           GFX_RPTAG_WRITEMASK, GFX_RPTAG_MAXPEN, GFX_TAG_IGNORE, GFX_TAG_SKIP};
            int n = (int)rn(8), set = op == 7, k;
            printf("%s", set ? "SRPA" : "GRPA");
            for (k = 0; k < n; k++) {
                tags[k].tag = ids[rn(8)];
                tags[k].data = tags[k].tag == GFX_TAG_SKIP ? rn(2) : set ? rn(256) : (uint32_t)(uintptr_t)&got[k];
                if (tags[k].tag == GFX_TAG_SKIP && k + (int)tags[k].data >= n) tags[k].data = 0;
                got[k] = 0xDEAD;
                printf(" %lu:%lu", (unsigned long)tags[k].tag, (unsigned long)(set || tags[k].tag == GFX_TAG_SKIP ? tags[k].data : 0));
            }
            /* half the time the list ends in a TAG_MORE to a second one */
            if (rn(2)) {
                more[0].tag = set ? GFX_RPTAG_APEN : GFX_RPTAG_APEN;
                more[0].data = set ? rn(256) : (uint32_t)(uintptr_t)&got[7];
                more[1].tag = GFX_TAG_DONE;
                tags[n].tag = GFX_TAG_MORE;
                tags[n].data = (uint32_t)(uintptr_t)more;
                printf(" M %lu", (unsigned long)(set ? more[0].data : 0));
                got[7] = 0xDEAD;
            } else tags[n].tag = GFX_TAG_DONE;
            printf("\n");
            if (set) gfx_SetRPAttrsA(&env, &rp, tags);
            else {
                gfx_GetRPAttrsA(&env, &rp, tags);
                printf("GOT");
                for (k = 0; k < n; k++)
                    if (tags[k].tag != GFX_TAG_IGNORE && tags[k].tag != GFX_TAG_SKIP) printf(" %lu", (unsigned long)got[k]);
                    else printf(" -");
                if (tags[n].tag == GFX_TAG_MORE) printf(" %lu", (unsigned long)got[7]);
                printf("\n");
            }
        }
        }
    }
    {
        struct gfx_tmpras tr;
        struct gfx_areainfo ai;
        uint8_t buf[100];
        struct gfx_tmpras *got = gfx_InitTmpRas(&env, &tr, buf, 77);
        printf("TMPRAS %d %d\n", got == &tr, tr.rasptr == buf && tr.size == 77);
        gfx_InitArea(&env, &ai, buf, 20);
        printf("AREA %d %d %d %d %d %d %d\n", ai.vctrtbl == (int16_t *)buf, ai.vctrptr == ai.vctrtbl,
               (int)((uint8_t *)ai.flagtbl - buf), ai.flagptr == ai.flagtbl, ai.count, ai.maxcount, ai.firstx | ai.firsty);
    }
    arena_reset("rastport");
}

/* a ViewPort as far as RasInfo (offset 36), and a RasInfo as far as BitMap */
struct t_rasinfo { void *next; struct gfx_bitmap *bm; int16_t rx, ry; };
struct t_vp { void *next; struct gfx_colormap *cm; void *ins[4]; int16_t dw, dh, dx, dy; uint16_t modes;
              uint8_t sprpri, extmodes; struct t_rasinfo *rasinfo; };

static void t_colors(void)
{
    int r, k;
    for (r = 0; r < ROUNDS / 2; r++) {
        int32_t n = 1 + (int32_t)rn(40);
        struct gfx_colormap *cm = gfx_GetColorMap(&env, n);
        struct t_vp vp = {0, cm};
        uint32_t tab[3 * 50 + 8];
        uint16_t c4[48];
        printf("CM %ld %u %u %u\n", (long)n, cm->count, cm->type, cm->colortable && cm->lowcolorbits);
        for (k = 0; k < ROUNDS; k++) {
            uint32_t op = rn(8), i = rn((uint32_t)n + 3), a = rnd(), b = rnd(), c = rnd();
            switch (op) {
            case 0: gfx_SetRGB4CM(&env, cm, (int32_t)i, a, b, c); printf("S4 %lu %lu %lu %lu\n", (unsigned long)i, (unsigned long)(a & 15), (unsigned long)(b & 15), (unsigned long)(c & 15)); break;
            case 1: gfx_SetRGB32CM(&env, cm, i, a, b, c); printf("S32 %lu %lu %lu %lu\n", (unsigned long)i, (unsigned long)(a >> 24), (unsigned long)(b >> 24), (unsigned long)(c >> 24)); break;
            case 2: gfx_SetRGB4(&env, &vp, (int32_t)i, a, b, c); printf("V4 %lu %lu %lu %lu\n", (unsigned long)i, (unsigned long)(a & 15), (unsigned long)(b & 15), (unsigned long)(c & 15)); break;
            case 3: gfx_SetRGB32(&env, &vp, i, a, b, c); printf("V32 %lu %lu %lu %lu\n", (unsigned long)i, (unsigned long)(a >> 24), (unsigned long)(b >> 24), (unsigned long)(c >> 24)); break;
            case 4: printf("G4 %lu %ld\n", (unsigned long)i, (long)gfx_GetRGB4(&env, cm, (int32_t)i)); break;
            case 5: {
                uint32_t m = rn(6), j;
                for (j = 0; j < 3 * 8; j++) tab[j] = 7;
                gfx_GetRGB32(&env, cm, i, m, tab);
                printf("G32 %lu %lu", (unsigned long)i, (unsigned long)m);
                for (j = 0; j < 3 * m; j++) printf(" %lu", (unsigned long)tab[j]);
                printf("\n");
                break;
            }
            case 6: {
                uint32_t m = rn((uint32_t)n + 4), j;
                for (j = 0; j < m; j++) c4[j] = (uint16_t)rnd();
                gfx_LoadRGB4(&env, &vp, c4, (int32_t)m);
                printf("L4 %lu", (unsigned long)m);
                for (j = 0; j < m; j++) printf(" %u", c4[j]);
                printf("\n");
                break;
            }
            default: {
                /* two runs, then the end */
                uint32_t p = 0, run, j;
                for (run = 0; run < 2; run++) {
                    uint32_t cnt = 1 + rn(4), first = rn((uint32_t)n + 2);
                    tab[p++] = cnt << 16 | first;
                    for (j = 0; j < 3 * cnt; j++) tab[p++] = rnd();
                }
                tab[p] = 0;
                gfx_LoadRGB32(&env, &vp, tab);
                printf("L32");
                for (p = 0; tab[p]; p += 1 + 3 * (tab[p] >> 16)) {
                    printf(" %lu %lu", (unsigned long)(tab[p] >> 16), (unsigned long)(tab[p] & 0xFFFF));
                    for (j = 0; j < 3 * (tab[p] >> 16); j++) printf(" %lu", (unsigned long)(tab[p + 1 + j] >> 24));
                }
                printf("\n");
            }
            }
        }
        /* pens */
        {   /* the ViewPort's depth, or no bitmap: the pens it shares */
            static struct gfx_bitmap pbm;
            static struct t_rasinfo ri;
            uint32_t pd = rn(3) ? 0 : 1 + rn(8);
            pbm.depth = (uint8_t)pd;
            ri.bm = &pbm;
            vp.rasinfo = pd ? &ri : 0;
            printf("APE %ld %lu", (long)gfx_AttachPalExtra(&env, cm, &vp), (unsigned long)pd);
            printf(" %u\n", cm->palextra ? (unsigned)cm->palextra->sharablecolors : 0u);
        }
        printf("APE2 %ld\n", (long)gfx_AttachPalExtra(&env, cm, &vp));
        for (k = 0; k < ROUNDS * 2; k++) {
            uint32_t op = rn(6), i = rn((uint32_t)n + 2), a = rnd() & 0xFF000000u, b = rnd() & 0xFF000000u,
                     c = rnd() & 0xFF000000u;
            if (rn(3) == 0) a = b = c = rn(2) ? 0xFF000000u : 0; /* black and white, shared often */
            switch (op) {
            case 0: case 1: {
                uint32_t fl = rn(4);
                int32_t want = rn(3) ? -1 : (int32_t)i;
                printf("OP %ld %lu %lu %lu %lu %ld\n", (long)want, (unsigned long)(a >> 24), (unsigned long)(b >> 24),
                       (unsigned long)(c >> 24), (unsigned long)fl, (long)gfx_ObtainPen(&env, cm, (uint32_t)want, a, b, c, fl));
                break;
            }
            case 2: gfx_ReleasePen(&env, cm, i); printf("RP %lu\n", (unsigned long)i); break;
            case 3: case 4: {
                struct gfx_tagitem t[3];
                static const int32_t precs[] = {-1, 0, 16, 32};
                int32_t pr = precs[rn(4)];
                uint32_t fb = rn(2);
                t[0].tag = GFX_OBP_PRECISION; t[0].data = (uint32_t)pr;
                t[1].tag = GFX_OBP_FAILIFBAD; t[1].data = fb;
                t[2].tag = GFX_TAG_DONE;
                printf("BP %lu %lu %lu %ld %lu %ld\n", (unsigned long)(a >> 24), (unsigned long)(b >> 24),
                       (unsigned long)(c >> 24), (long)pr, (unsigned long)fb, (long)gfx_ObtainBestPenA(&env, cm, a, b, c, t));
                break;
            }
            default: {
                int32_t mx = (int32_t)rn((uint32_t)n + 2) - 1;
                printf("FC %lu %lu %lu %ld %ld\n", (unsigned long)(a >> 24), (unsigned long)(b >> 24), (unsigned long)(c >> 24),
                       (long)mx, (long)gfx_FindColor(&env, cm, a, b, c, mx));
            }
            }
            if (sem_held) fail("pen lock", sem_held);
        }
        gfx_FreeColorMap(&env, cm);
        arena_reset("colormap leak");
    }
    printf("CMNULL %ld %ld\n", (long)gfx_GetColorMap(&env, 0) , (long)gfx_GetRGB4(&env, 0, 0));
}

static void t_flood(void)
{
    int r;
    for (r = 0; r < ROUNDS; r++) {
        int w = 8 + (int)rn(60), h = 4 + (int)rn(24), d = 1 + (int)rn(3);
        struct gfx_bitmap *bm = gfx_AllocBitMap(&env, (uint32_t)w, (uint32_t)h, (uint32_t)d, 0, 0);
        struct gfx_rastport rp;
        struct gfx_tmpras tr;
        uint32_t need = ((((uint32_t)bm->bytesperrow * 8) + 15) >> 4) * 2 * (uint32_t)h;
        uint8_t *tbuf = env.alloc(env.ctx, need, 0);
        int32_t x = (int32_t)rn((uint32_t)w + 2) - 1, y = (int32_t)rn((uint32_t)h + 2) - 1, rc;
        uint32_t mode = rn(2), small = rn(10) == 0;
        gfx_InitRastPort(&env, &rp);
        rp.bitmap = bm;
        random_bm(bm, w, h, 1 << d, 6 + (int)rn(10));
        gfx_InitTmpRas(&env, &tr, tbuf, (int32_t)(small ? need - 1 : need));
        rp.tmpras = rn(12) ? &tr : 0;
        rp.fgpen = (int8_t)rn(1u << d);
        rp.aolpen = (int8_t)rn(1u << d);
        rp.drawmode = (int8_t)(rn(3) ? GFX_JAM1_MODE : GFX_COMPLEMENT);
        print_bm("FLBEFORE", bm, bm->bytesperrow * 8, h);
        rc = gfx_Flood(&env, &rp, mode, x, y);
        printf("FLOOD %lu %ld %ld %d %d %d %d %d %ld\n", (unsigned long)mode, (long)x, (long)y, (uint8_t)rp.fgpen,
               (uint8_t)rp.aolpen, rp.drawmode, d, rp.tmpras == 0 || small, (long)rc);
        print_bm("FLAFTER", bm, bm->bytesperrow * 8, h);
        env.free(env.ctx, tbuf, need);
        gfx_FreeBitMap(&env, bm);
        arena_reset("flood leak");
    }
}

static void t_scale(void)
{
    int r;
    printf("SDIV0 %lu\n", (unsigned long)gfx_ScalerDiv(&env, 5, 3, 0));
    for (r = 0; r < ROUNDS; r++) {
        struct gfx_bitscaleargs a;
        struct gfx_bitmap *s = gfx_AllocBitMap(&env, 40, 20, 2, GFX_BMF_CLEAR, 0);
        struct gfx_bitmap *d = gfx_AllocBitMap(&env, 64, 40, 2, GFX_BMF_CLEAR, 0);
        uint32_t f = rnd() & 0x3FFF, nu = rnd() & 0x3FFF, de = 1 + (rnd() & 0x3FFF);
        printf("SDIV %lu %lu %lu %lu\n", (unsigned long)f, (unsigned long)nu, (unsigned long)de,
               (unsigned long)gfx_ScalerDiv(&env, f, nu, de));
        random_bm(s, 40, 20, 4, 12);
        a.srcx = (uint16_t)rn(10); a.srcy = (uint16_t)rn(5);
        a.srcwidth = (uint16_t)(1 + rn(28)); a.srcheight = (uint16_t)(1 + rn(14));
        a.xsrcfactor = (uint16_t)(1 + rn(8)); a.ysrcfactor = (uint16_t)(1 + rn(8));
        a.xdestfactor = (uint16_t)(1 + rn(8)); a.ydestfactor = (uint16_t)(1 + rn(8));
        a.destx = (uint16_t)rn(8); a.desty = (uint16_t)rn(4);
        a.destwidth = a.destheight = 0;
        a.srcbitmap = s; a.destbitmap = d;
        a.flags = 0;
        print_bm("SCSRC", s, 40, 20);
        gfx_BitMapScale(&env, &a);
        printf("SCALE %u %u %u %u %u %u %u %u %u %u %u %u\n", a.srcx, a.srcy, a.srcwidth, a.srcheight, a.xsrcfactor,
               a.ysrcfactor, a.xdestfactor, a.ydestfactor, a.destx, a.desty, a.destwidth, a.destheight);
        print_bm("SCDST", d, 64, 40);
        gfx_FreeBitMap(&env, s);
        gfx_FreeBitMap(&env, d);
        arena_reset("scale leak");
    }
}

/* DrawEllipse through COMPLEMENT and INVERSVID, which the core leaves to
 * the face: compared with the core's own JAM1 ellipse. */
static void t_ellipse(void)
{
    int r;
    for (r = 0; r < ROUNDS; r++) {
        struct gfx_bitmap *ref = gfx_AllocBitMap(&env, 80, 50, 1, GFX_BMF_CLEAR, 0);
        struct gfx_bitmap *bm = gfx_AllocBitMap(&env, 80, 50, 3, 0, 0);
        struct gfx_rastport rr, rp;
        int32_t xc = (int32_t)rn(90) - 5, yc = (int32_t)rn(60) - 5, a = (int32_t)rn(30), b = (int32_t)rn(20);
        int mode = (int)rn(4);
        static const int8_t modes[] = {GFX_COMPLEMENT, GFX_COMPLEMENT | GFX_JAM2, GFX_INVERSVID, GFX_INVERSVID | GFX_JAM2};
        gfx_InitRastPort(&env, &rr);
        rr.bitmap = ref;
        rr.drawmode = GFX_JAM1_MODE;
        rr.fgpen = 1;
        gfx_DrawEllipse(&env, &rr, xc, yc, a, b);
        gfx_InitRastPort(&env, &rp);
        rp.bitmap = bm;
        random_bm(bm, 80, 50, 8, 10);
        rp.fgpen = (int8_t)rn(8);
        rp.bgpen = (int8_t)rn(8);
        rp.mask = (uint8_t)rn(8);
        rp.drawmode = modes[mode];
        print_bm("ELREF", ref, 80, 50);
        print_bm("ELBEFORE", bm, 80, 50);
        gfx_DrawEllipse(&env, &rp, xc, yc, a, b);
        printf("ELL %d %d %d %d\n", (uint8_t)rp.fgpen, (uint8_t)rp.bgpen, rp.mask, rp.drawmode);
        print_bm("ELAFTER", bm, 80, 50);
        gfx_FreeBitMap(&env, ref);
        gfx_FreeBitMap(&env, bm);
        arena_reset("ellipse leak");
    }
}

/* A layer of one visible ClipRect at (ox, oy) on a screen bitmap; simple
 * refresh (ScrollRaster through the face's own copy) or super bitmap. */
static struct t_hook bfhook;
static uint32_t bf_entry(struct t_hook *h, void *obj, void *msg)
{
    struct gfx_rastport *rp = obj;
    struct gfx_backfillmsg *m = msg;
    struct gfx_planar p;
    int x, y, i;
    (void)h;
    hook_calls++;
    printf("BFHOOK %d %d %d %d %ld %ld %d\n", m->bounds.minx, m->bounds.miny, m->bounds.maxx, m->bounds.maxy,
           (long)m->offsetx, (long)m->offsety, rp->layer == 0);
    /* the hook draws pen 5 */
    for (i = 0; i < 8; i++) p.planes[i] = i < rp->bitmap->depth ? rp->bitmap->planes[i] : 0;
    p.bytes_per_row = rp->bitmap->bytesperrow; p.rows = rp->bitmap->rows; p.depth = rp->bitmap->depth;
    p.interleaved = (rp->bitmap->flags & GFX_BMF_INTERLEAVED) != 0;
    for (y = m->bounds.miny; y <= m->bounds.maxy; y++)
        for (x = m->bounds.minx; x <= m->bounds.maxx; x++) gfx_planar_put(&p, x, y, 5, 0xFF, GFX_SET);
    return 0;
}

static void t_layers(void)
{
    int r;
    bfhook.entry = bf_entry;
    for (r = 0; r < ROUNDS; r++) {
        struct gfx_bitmap *scr = gfx_AllocBitMap(&env, 96, 48, 3, 0, 0);
        struct gfx_layer l;
        struct gfx_cliprect cr;
        struct gfx_rastport rp;
        struct gfx_rgn *dmg = gfx_NewRegion(&env);
        int ox = (int)rn(30), oy = (int)rn(16), lw = 8 + (int)rn(50), lh = 4 + (int)rn(26), op = (int)rn(4);
        int32_t dx = (int32_t)rn(13) - 6, dy = (int32_t)rn(9) - 4, x0 = (int32_t)rn(lw + 6) - 3, y0 = (int32_t)rn(lh + 4) - 2;
        int32_t x1 = x0 + (int32_t)rn(30), y1 = y0 + (int32_t)rn(16);
        random_bm(scr, 96, 48, 8, 14);
        {
            uint8_t *z = (uint8_t *)&l; unsigned i;
            for (i = 0; i < sizeof l; i++) z[i] = 0;
            z = (uint8_t *)&cr;
            for (i = 0; i < sizeof cr; i++) z[i] = 0;
        }
        gfx_InitRastPort(&env, &rp);
        rp.bitmap = scr;
        rp.layer = &l;
        rp.bgpen = (int8_t)rn(8);
        rp.mask = (uint8_t)(rn(2) ? 0xFF : rn(8));
        l.rp = &rp;
        l.bounds.minx = (int16_t)ox; l.bounds.miny = (int16_t)oy;
        l.bounds.maxx = (int16_t)(ox + lw - 1); l.bounds.maxy = (int16_t)(oy + lh - 1);
        l.cliprect = &cr;
        cr.bounds = l.bounds;
        l.flags = GFX_LAYERSIMPLE;
        l.damagelist = dmg;
        l.backfill = op == 3 ? (void *)&bfhook : rn(4) ? 0 : (void *)1;
        t_sem_init(0, &l.lock);
        print_bm("LYBEFORE", scr, 96, 48);
        if (op <= 1) gfx_ScrollRaster(&env, &rp, dx, dy, x0, y0, x1, y1);
        else gfx_EraseRect(&env, &rp, x0, y0, x1, y1);
        printf("LAYER %d %d %d %d %d %ld %ld %ld %ld %ld %ld %d %d %ld %d\n", op <= 1 ? 0 : 1, ox, oy, lw, lh, (long)dx,
               (long)dy, (long)x0, (long)y0, (long)x1, (long)y1, (uint8_t)rp.bgpen, rp.mask,
               l.backfill == 0 ? 0L : l.backfill == (void *)1 ? 1L : 2L, (l.flags & GFX_LAYERREFRESH) != 0);
        {
            struct gfx_rgnrect *q;
            printf("DMG");
            for (q = dmg->rects; q; q = q->next)
                printf(" %d %d %d %d", dmg->bounds.minx + q->bounds.minx, dmg->bounds.miny + q->bounds.miny,
                       dmg->bounds.minx + q->bounds.maxx, dmg->bounds.miny + q->bounds.maxy);
            printf("\n");
        }
        print_bm("LYAFTER", scr, 96, 48);
        if (sem_held) fail("layer lock", sem_held);
        gfx_DisposeRegion(&env, dmg);
        gfx_FreeBitMap(&env, scr);
        arena_reset("layer leak");
    }
}

/* A super bitmap layer: drawing lands in the super bitmap and shows
 * through the layer's window; SyncSBitMap and CopySBitMap move it. */
static void t_super(void)
{
    int r;
    for (r = 0; r < ROUNDS; r++) {
        struct gfx_bitmap *scr = gfx_AllocBitMap(&env, 80, 40, 2, GFX_BMF_CLEAR, 0);
        struct gfx_bitmap *sb = gfx_AllocBitMap(&env, 64, 48, 2, GFX_BMF_CLEAR, 0);
        struct gfx_layer l;
        struct gfx_cliprect cr;
        struct gfx_rastport rp;
        int ox = (int)rn(30), oy = (int)rn(12), lw = 8 + (int)rn(40), lh = 4 + (int)rn(24);
        int sx = (int)rn(64 - lw + 1), sy = (int)rn(48 - lh + 1), k;
        uint8_t *z = (uint8_t *)&l;
        unsigned i;
        if (sx < 0) sx = 0;
        if (sy < 0) sy = 0;
        for (i = 0; i < sizeof l; i++) z[i] = 0;
        z = (uint8_t *)&cr;
        for (i = 0; i < sizeof cr; i++) z[i] = 0;
        gfx_InitRastPort(&env, &rp);
        rp.bitmap = scr;
        rp.layer = &l;
        l.rp = &rp;
        l.bounds.minx = (int16_t)ox; l.bounds.miny = (int16_t)oy;
        l.bounds.maxx = (int16_t)(ox + lw - 1); l.bounds.maxy = (int16_t)(oy + lh - 1);
        if (l.bounds.maxx > 79) l.bounds.maxx = 79;
        if (l.bounds.maxy > 39) l.bounds.maxy = 39;
        l.cliprect = &cr;
        cr.bounds = l.bounds;
        l.flags = GFX_LAYERSUPER | GFX_LAYERSMART;
        l.superbitmap = sb;
        l.scroll_x = (int16_t)sx; l.scroll_y = (int16_t)sy;
        t_sem_init(0, &l.lock);
        random_bm(sb, 64, 48, 4, 8);
        gfx_CopySBitMap(&env, &l);
        printf("SUPER %d %d %d %d %d %d\n", l.bounds.minx, l.bounds.miny, l.bounds.maxx, l.bounds.maxy, sx, sy);
        print_bm("SUSB0", sb, 64, 48);
        for (k = 0; k < 6; k++) {
            int32_t a = (int32_t)rn(70) - 3, b = (int32_t)rn(54) - 3, c = a + (int32_t)rn(12), d = b + (int32_t)rn(8);
            rp.fgpen = (int8_t)rn(4);
            rp.drawmode = GFX_JAM1_MODE;
            if (rn(2)) {
                printf("SWP %ld %ld %d %ld\n", (long)a, (long)b, (uint8_t)rp.fgpen, (long)gfx_WritePixel(&env, &rp, a, b));
            } else {
                gfx_RectFill(&env, &rp, a, b, c, d);
                printf("SRF %ld %ld %ld %ld %d\n", (long)a, (long)b, (long)c, (long)d, (uint8_t)rp.fgpen);
            }
        }
        printf("SRD %ld %ld\n", (long)gfx_ReadPixel(&env, &rp, sx, sy), (long)gfx_ReadPixel(&env, &rp, 64, 0));
        print_bm("SUSB", sb, 64, 48);
        print_bm("SUSCR", scr, 80, 40);
        /* the screen changes under the layer; SyncSBitMap takes it back */
        {
            struct gfx_planar p;
            for (i = 0; i < 8; i++) p.planes[i] = i < scr->depth ? scr->planes[i] : 0;
            p.bytes_per_row = scr->bytesperrow; p.rows = scr->rows; p.depth = scr->depth;
            p.interleaved = (scr->flags & GFX_BMF_INTERLEAVED) != 0;
            gfx_planar_put(&p, l.bounds.minx, l.bounds.miny, 3, 0xFF, GFX_SET);
            gfx_SyncSBitMap(&env, &l);
            printf("SSYNC %ld\n", (long)gfx_ReadPixel(&env, &rp, sx, sy));
        }
        if (sem_held) fail("super lock", sem_held);
        gfx_FreeBitMap(&env, scr);
        gfx_FreeBitMap(&env, sb);
        arena_reset("super leak");
    }
}

static void t_regions(void)
{
    int r, k;
    for (r = 0; r < ROUNDS / 2; r++) {
        struct gfx_rgn *a = gfx_NewRegion(&env), *b = gfx_NewRegion(&env);
        printf("RGNEW\n");
        for (k = 0; k < 12; k++) {
            struct gfx_rectangle q;
            int op = (int)rn(8);
            int32_t rc = 1;
            q.minx = (int16_t)(rn(40) - 4); q.miny = (int16_t)(rn(30) - 4);
            q.maxx = (int16_t)(q.minx + rn(16) - 1); q.maxy = (int16_t)(q.miny + rn(12) - 1);
            switch (op) {
            case 0: gfx_AndRectRegion(&env, a, &q); break;
            case 1: rc = gfx_OrRectRegion(&env, a, &q); break;
            case 2: rc = gfx_XorRectRegion(&env, a, &q); break;
            case 3: rc = gfx_ClearRectRegion(&env, a, &q); break;
            case 4: rc = gfx_OrRectRegion(&env, b, &q); break;
            case 5: rc = gfx_OrRegionRegion(&env, b, a); break;
            case 6: rc = gfx_AndRegionRegion(&env, b, a); break;
            default: rc = gfx_XorRegionRegion(&env, b, a); break;
            }
            printf("RG %d %d %d %d %d %ld", op, q.minx, q.miny, q.maxx, q.maxy, (long)rc);
            {
                struct gfx_rgnrect *p;
                for (p = a->rects; p; p = p->next)
                    printf(" %d %d %d %d", a->bounds.minx + p->bounds.minx, a->bounds.miny + p->bounds.miny,
                           a->bounds.minx + p->bounds.maxx, a->bounds.miny + p->bounds.maxy);
                printf(" |");
                for (p = b->rects; p; p = p->next)
                    printf(" %d %d %d %d", b->bounds.minx + p->bounds.minx, b->bounds.miny + p->bounds.miny,
                           b->bounds.minx + p->bounds.maxx, b->bounds.miny + p->bounds.maxy);
                printf("\n");
            }
        }
        gfx_DisposeRegion(&env, a);
        gfx_DisposeRegion(&env, b);
        gfx_DisposeRegion(&env, 0);
        arena_reset("region leak");
    }
}

int main(void)
{
    env_init();
    t_bitmaps();
    t_rastport();
    t_colors();
    t_flood();
    t_scale();
    t_ellipse();
    t_layers();
    t_super();
    t_regions();
    printf("END %d\n", errors);
    return errors != 0;
}
