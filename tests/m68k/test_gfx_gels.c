/* SPDX-License-Identifier: MIT */
/* OpenGfx gels (library/gels/gels_core.cpp) on a simulated Amiga: Chip and
 * Fast RAM, a blitter written from the Hardware Reference Manual (it runs
 * a blit only when it finishes, a few DMACONR reads after BLTSIZE, so a
 * CPU write before WaitBlit shows), its interrupt (held while Disable()d),
 * the beam, and tasks that block in OwnBlitter. It runs the script
 * tests/m68k/test_gfx_gels.py writes (gels_script.h) and prints what it
 * sees; the python model prints what it should see, and the two are
 * compared line by line. Copyright (c) 2026 Dalsin Limited. MIT. */
#include <stdio.h>
#include <stdint.h>
#include "gels_core.h"
#include "gels_script.h"

#define ARENA (1u << 17)
static uint8_t chipmem[ARENA] __attribute__((aligned(4)));
static uint8_t fastmem[ARENA] __attribute__((aligned(4)));
static uint32_t chiptop, fasttop;
static uint32_t rng = 0x5eed;
static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 16; }
static int failed;
static void fail(const char *what) { if (failed++ < 10) printf("FAIL %s\n", what); }

/* A page of Fast RAM with a page no one may touch after it (Linux's
 * old_mmap and mprotect, through trap #0 as the 68000 runtime has no
 * libc): a buffer that ends there faults when a read runs past its end. */
static long sys1(long n, long a, long b, long c)
{
    register long d0 __asm__("d0") = n, d1 __asm__("d1") = a, d2 __asm__("d2") = b, d3 __asm__("d3") = c;
    __asm__ volatile ("trap #0" : "+d"(d0) : "d"(d1), "d"(d2), "d"(d3) : "memory");
    return d0;
}
static uint8_t *guarded(uint32_t size)
{
    static uint8_t *page;
    if (!page) {
        long args[6] = { 0, 8192, 3, 0x22, -1, 0 };    /* PROT_READ|WRITE, MAP_PRIVATE|ANONYMOUS */
        long a = sys1(90, (long)args, 0, 0);
        if (a < 0 && a > -4096) { fail("mmap"); return 0; }
        page = (uint8_t *)a;
        if (sys1(125, (long)(page + 4096), 4096, 0)) fail("mprotect");
    }
    if (size > 4096) { fail("guarded buffer too big"); size = 4096; }
    return page + 4096 - size;
}

static void *bump(uint32_t size, int chip)
{
    uint32_t *top = chip ? &chiptop : &fasttop, at = (*top + 3) & ~3u;
    if (at + size + 8 > ARENA) { fail("arena full"); at = 0; }
    *top = at + size + 4;      /* a gap, and room for the blitter's read past a row */
    return (chip ? chipmem : fastmem) + at;
}

/* ---- the machine ------------------------------------------------------- */
static struct gfx_base_area { struct gfx_base pub; uint8_t pad[GFX_PRIVATE_OFFSET - sizeof(struct gfx_base)]; struct gfx_private priv; } gb;
static struct gels_state st;
static int disabled, in_int, pending_int;
static long allocs;
static int alloc_fail_at = -1, alloc_count;
static int sem_depth;
static void *cur_task;
static uint32_t task_sigs[4];
static int tasks_id[4] = { 0, 1, 2, 3 };
static void (*on_block)(void);

/* the blitter */
static uint16_t regs[0x100];
static int bbusy, countdown;
static uint32_t beamcount;
static unsigned long blits, cblits, shifted;

static uint32_t ptr_of(int r) { return (uint32_t)regs[r / 2] << 16 | regs[r / 2 + 1]; }
static uint16_t rdw(uint32_t a) { const uint8_t *p = (const uint8_t *)(uintptr_t)a; return (uint16_t)(p[0] << 8 | p[1]); }
static void wrw(uint32_t a, uint16_t v) { uint8_t *p = (uint8_t *)(uintptr_t)a; p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static int in_chip(uint32_t a) { return a >= (uint32_t)(uintptr_t)chipmem && a < (uint32_t)(uintptr_t)chipmem + ARENA; }

/* HRM: a blit of h rows of w words; each channel's pointer moves on a word
 * at a time (back, descending) and by its modulo after each row. A's word
 * is masked by FWM on a row's first word and LWM on its last, then A and B
 * are shifted by their shift counts with the previous word's bits coming
 * in (out of the other side, descending); a disabled A or B channel holds
 * its data register. D = the minterm of A, B, C. */
static void run_blit(void)
{
    uint16_t con0 = regs[0x40 / 2], con1 = regs[0x42 / 2], size = regs[0x58 / 2];
    int h = size >> 6 ? size >> 6 : 1024, w = size & 63 ? size & 63 : 64, desc = con1 & 2;
    int ash = con0 >> 12, bsh = con1 >> 12, mt = con0 & 0xFF;
    int ua = con0 & 0x800, ub = con0 & 0x400, uc = con0 & 0x200, ud = con0 & 0x100;
    uint32_t pa = ptr_of(0x50), pb = ptr_of(0x4C), pc = ptr_of(0x48), pd = ptr_of(0x54);
    int32_t ma = (int16_t)regs[0x64 / 2], mb = (int16_t)regs[0x62 / 2], mc = (int16_t)regs[0x60 / 2], md = (int16_t)regs[0x66 / 2];
    int step = desc ? -2 : 2, r, i, k;
    uint32_t aold = 0, bold = 0;
    if (con1 & 1) { fail("line mode"); return; }
    if (con1 & 0x18) { fail("fill mode"); return; }
    for (r = 0; r < h; r++) {
        for (i = 0; i < w; i++) {
            uint32_t a, b, c, d = 0, as, bs;
            if (ua) { if (!in_chip(pa)) fail("blitter A outside Chip RAM"); regs[0x74 / 2] = rdw(pa); pa += step; }
            if (ub) { if (!in_chip(pb)) fail("blitter B outside Chip RAM"); regs[0x72 / 2] = rdw(pb); pb += step; }
            a = regs[0x74 / 2]; b = regs[0x72 / 2];
            if (i == 0) a &= regs[0x44 / 2];
            if (i == w - 1) a &= regs[0x46 / 2];
            if (!desc) { as = ((aold << 16 | a) >> ash) & 0xFFFF; bs = ((bold << 16 | b) >> bsh) & 0xFFFF; }
            else { as = ((a << 16 | aold) << ash >> 16) & 0xFFFF; bs = ((b << 16 | bold) << bsh >> 16) & 0xFFFF; }
            aold = a; bold = b;
            c = uc ? rdw(pc) : regs[0x70 / 2];
            for (k = 0; k < 16; k++) {
                int idx = (int)((as >> k & 1) << 2 | (bs >> k & 1) << 1 | (c >> k & 1));
                d |= (uint32_t)(mt >> idx & 1) << k;
            }
            if (uc) pc += step;
            if (ud) { if (!in_chip(pd)) fail("blitter D outside Chip RAM"); wrw(pd, (uint16_t)d); pd += step; }
        }
        if (ua) pa += desc ? -ma : ma;
        if (ub) pb += desc ? -mb : mb;
        if (uc) pc += desc ? -mc : mc;
        if (ud) pd += desc ? -md : md;
    }
    blits++;
    if (uc) cblits++;
    if (uc && ash) shifted++;
}

static void deliver(void)
{
    while (pending_int && !disabled && !in_int) {
        pending_int = 0;
        in_int = 1;
        gels_blit_int(&st);
        in_int = 0;
    }
}
static void finish_blit(void)
{
    run_blit();
    bbusy = 0;
    pending_int = 1;
    deliver();
}
static uint16_t chip_read(void *ctx, uint32_t off)
{
    (void)ctx;
    if (off == 0x002) {
        uint16_t v = bbusy ? 0x4000 : 0;
        if (bbusy && --countdown <= 0) finish_blit();
        return v;
    }
    if (off == 0x004) { uint32_t line = ++beamcount % 312; return (uint16_t)(line >> 8 & 7); }
    if (off == 0x006) { uint32_t line = beamcount % 312; return (uint16_t)((line & 0xFF) << 8); }
    fail("chip read");
    return 0;
}
static void chip_write(void *ctx, uint32_t off, uint16_t v)
{
    (void)ctx;
    if (off >= 0x40 && off < 0x78) {
        if (bbusy) fail("blitter register written while busy");
        regs[off / 2] = v;
        if (off == 0x58) { bbusy = 1; countdown = 3 + (int)(rnd() % 4);   /* never within the two reads of the call that started it */ }
        return;
    }
    if (off == 0x09C || off == 0x09A || off == 0x096) return;
    fail("chip write");
}
static void tick(void) { if (bbusy && --countdown <= 0) finish_blit(); deliver(); }
static void settle(void) { int n = 0; while (bbusy && n++ < 100000) tick(); deliver(); }

/* MEMF_ANY gets Chip RAM every other time, as when Fast RAM runs short */
static int any_chip;
static void *e_alloc(void *ctx, uint32_t size, uint32_t flags)
{
    void *p;
    uint32_t i;
    (void)ctx;
    if (alloc_count++ == alloc_fail_at) return 0;
    if (!(flags & (GFX_MEMF_CHIP | GFX_MEMF_FAST))) any_chip ^= 1;
    p = bump(size, (flags & GFX_MEMF_CHIP) || (!(flags & GFX_MEMF_FAST) && any_chip));
    if (flags & GFX_MEMF_CLEAR) for (i = 0; i < size; i++) ((uint8_t *)p)[i] = 0;
    allocs++;
    return p;
}
/* nothing may be freed that a blit in progress reads or writes */
static void e_free(void *ctx, void *p, uint32_t size)
{
    static const uint16_t ptr[4] = { 0x50, 0x4C, 0x48, 0x54 };    /* A, B, C, D */
    uint32_t a = (uint32_t)(uintptr_t)p;
    int k;
    (void)ctx;
    for (k = 0; bbusy && k < 4; k++)
        if (regs[0x40 / 2] & (0x800 >> k) && ptr_of(ptr[k]) >= a && ptr_of(ptr[k]) < a + size)
            fail("freed while the blitter uses it");
    allocs--;
}
static void e_nop(void *ctx) { (void)ctx; }
static void e_disable(void *ctx) { (void)ctx; disabled++; }
static void e_enable(void *ctx) { (void)ctx; if (--disabled < 0) fail("Enable without Disable"); deliver(); }
static void e_sem_obtain(void *ctx, struct gfx_semaphore *s) { (void)ctx; s->nestcount++; sem_depth++; }
static void e_sem_release(void *ctx, struct gfx_semaphore *s) { (void)ctx; s->nestcount--; sem_depth--; }
static void *e_find_task(void *ctx) { (void)ctx; return cur_task; }
static void e_signal(void *ctx, void *task, uint32_t sigs) { (void)ctx; task_sigs[*(int *)task] |= sigs; }
static uint32_t e_wait(void *ctx, uint32_t sigs)
{
    int me = *(int *)cur_task;
    (void)ctx;
    if (disabled) fail("Wait while Disable()d");
    if (!(task_sigs[me] & sigs)) {
        /* another task runs while this one waits */
        void *was = cur_task;
        int n = 0;
        if (on_block) { void (*f)(void) = on_block; on_block = 0; f(); }
        cur_task = was;
        while (!(task_sigs[me] & sigs) && n++ < 100000) tick();
        if (!(task_sigs[me] & sigs)) { fail("OwnBlitter never got the blitter"); return 0; }
    }
    task_sigs[me] &= ~sigs;
    return sigs;
}
static uint32_t e_type_of_mem(void *ctx, const void *p)
{
    (void)ctx;
    return in_chip((uint32_t)(uintptr_t)p) ? GFX_MEMF_CHIP | GFX_MEMF_PUBLIC : GFX_MEMF_FAST | GFX_MEMF_PUBLIC;
}

/* the hook gfx-display gives */
static struct gels_vsprite_slot last_slots[32];
static uint32_t last_nslots;
void gfx_hook_vsprites(struct gfx_env *env, void *vp, const void *slots, uint32_t n)
{
    uint32_t i;
    (void)env; (void)vp;
    last_nslots = n > 32 ? 32 : n;
    for (i = 0; i < last_nslots; i++) last_slots[i] = ((const struct gels_vsprite_slot *)slots)[i];
}

/* ---- the script's objects ------------------------------------------------ */
#define NB 160
static struct { uint8_t *p; uint32_t size; } buf[NB];
static struct gfx_bitmap bms[12];
static struct gfx_layer layers[2];
static struct gfx_cliprect crs[12];
static struct gfx_rastport rps[2];
static struct gl_vsprite head, tail, vss[24];
static struct gl_bob bobs[24];
static struct gl_dbufpacket dbs[24];
static struct gl_gelsinfo gi;
static struct gl_colltable ct;
static int16_t nextline[8], *lastcolor[8];
static int16_t colors[3][3] = { { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 } };
static struct gl_animob obs[6];
static struct gl_animcomp comps[24];
static struct gl_animob *animkey;
struct tnode { struct gl_bltnode bn; int id, again, calls, buf, value; };
static struct tnode nodes[16];
static char logbuf[4096];
static int loglen;

static void logs(const char *s) { while (*s && loglen < (int)sizeof logbuf - 1) logbuf[loglen++] = *s++; logbuf[loglen] = 0; }
static void logn(long v)
{
    char t[12]; int i = 0; unsigned long u = v < 0 ? -(unsigned long)v : (unsigned long)v;
    if (v < 0) logs("-");
    do t[i++] = (char)('0' + u % 10); while (u /= 10);
    while (i) { char c[2] = { t[--i], 0 }; logs(c); }
}

static int vs_id(struct gl_vsprite *v) { return v == &head ? -1 : v == &tail ? -2 : (int)(v - vss); }

/* a QBlit routine: fills its buffer's words with value ^ the call's number,
 * through the blitter; asks to be called again `again` times */
static int32_t node_call(struct gels_state *s, struct gl_bltnode *bn)
{
    struct tnode *t = (struct tnode *)bn;
    uint32_t a = (uint32_t)(uintptr_t)buf[t->buf].p;
    (void)s;
    if (bbusy) fail("queue routine called while the blitter is busy");
    logs(" C"); logn(t->id);
    regs[0x40 / 2] = 0x01F0;   /* D = A, A from BLTADAT */
    regs[0x42 / 2] = 0;
    regs[0x44 / 2] = regs[0x46 / 2] = 0xFFFF;
    regs[0x74 / 2] = (uint16_t)(t->value ^ t->calls);
    regs[0x54 / 2] = (uint16_t)(a >> 16); regs[0x56 / 2] = (uint16_t)a;
    regs[0x66 / 2] = 0;
    chip_write(0, 0x58, (uint16_t)(1 << 6 | (buf[t->buf].size / 2 & 63)));
    return t->calls++ < t->again;
}
static void node_cleanup(struct gels_state *s, struct gl_bltnode *bn)
{
    (void)s;
    logs(" X"); logn(((struct tnode *)bn)->id);
}

/* collision routines: one for the border, one for each other bit */
static void coll_border(struct gl_vsprite *v, void *flags) { logs(" B"); logn(vs_id(v)); logs(":"); logn((long)(uintptr_t)flags); }
#define COLL(n) static void coll##n(struct gl_vsprite *a, void *b) { logs(" P" #n ":"); logn(vs_id(a)); logs(","); logn(vs_id((struct gl_vsprite *)b)); }
COLL(1) COLL(2) COLL(3) COLL(4) COLL(5) COLL(6) COLL(7) COLL(8) COLL(9) COLL(10) COLL(11) COLL(12) COLL(13) COLL(14) COLL(15)
static gl_collfn colls[16] = { coll_border, coll1, coll2, coll3, coll4, coll5, coll6, coll7, coll8, coll9, coll10,
                               coll11, coll12, coll13, coll14, coll15 };
static int16_t comp_routine(struct gl_animcomp *c) { logs(" R"); logn(c - comps); return 0; }
static int16_t ob_routine(struct gl_animob *o) { logs(" O"); logn(o - obs); return 0; }

static uint32_t fnv(uint32_t h, const uint8_t *p, uint32_t n) { while (n--) { h ^= *p++; h *= 16777619u; } return h; }
static void fill(int id, uint32_t seed)
{
    uint32_t x = seed, i;
    for (i = 0; i < buf[id].size; i++) {
        if (!seed) { buf[id].p[i] = 0; continue; }
        x = x * 1103515245u + 12345u;
        buf[id].p[i] = (uint8_t)(x >> 16);
    }
}

static int pc;
static int32_t next(void) { return S[pc++]; }
static uint8_t *plane_spec(void)
{
    int32_t id = next(), off = next();
    if (id == -1) return 0;
    if (id == -2) return (uint8_t *)-1;
    return buf[id].p + off;
}

void gfx_lvo_OwnBlitter(struct gfx_base *gb);
void gfx_lvo_DisownBlitter(struct gfx_base *gb);
void gfx_lvo_WaitBlit(struct gfx_base *gb);

/* OS 3.2's graphics.library, as the side-by-side build sees it: its
 * blitter calls, logged by letter */
static char syslog[32];
static int syslen;
static struct gl_bltnode *sys_bn;
static void fake_sys(struct gels_state *s, int32_t lvo, struct gl_bltnode *bn)
{
    char c = lvo == GELS_SYS_OWNBLITTER ? 'O' : lvo == GELS_SYS_DISOWNBLITTER ? 'D' : lvo == GELS_SYS_WAITBLIT ? 'W'
           : lvo == GELS_SYS_QBLIT ? 'Q' : lvo == GELS_SYS_QBSBLIT ? 'S' : '?';
    (void)s;
    if ((c == 'Q' || c == 'S') != (bn != 0) || (bn && bn != sys_bn)) fail("sys_blitter's node");
    if (syslen < (int)sizeof syslog - 1) syslog[syslen++] = c;
    syslog[syslen] = 0;
}
static int streq(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return *a == *b; }
static void side_by_side(void)
{
    static struct gfx_bitmap sb;
    static struct gfx_rastport srp;
    static const uint8_t tmpl[4] = { 0xFF, 0xF0, 0x0F, 0xFF };
    static struct gl_bltnode bn;
    uint8_t *pl = bump(8, 1);
    int k;
    for (k = 0; k < 8; k++) pl[k] = 0;
    sb.bytesperrow = 4; sb.rows = 2; sb.flags = 0; sb.depth = 1; sb.planes[0] = pl;
    srp.bitmap = &sb; srp.layer = 0; srp.mask = 0xFF; srp.fgpen = 1; srp.bgpen = 0; srp.drawmode = 0;
    st.has_blitter = 0;
    st.sys_blitter = fake_sys;
    syslen = 0;
    /* a CPU blit: inside the OS's OwnBlitter, after its WaitBlit */
    gels_BltTemplate(&st, tmpl, 0, 2, &srp, 0, 0, 16, 2);
    if (!streq(syslog, "OWD")) fail("side by side: BltTemplate not inside the OS's OwnBlitter and WaitBlit");
    if (pl[0] != 0xFF || pl[1] != 0xF0 || pl[4] != 0x0F || pl[5] != 0xFF) fail("side by side: BltTemplate's pixels");
    /* the calls themselves: the OS's, our queue and lock untouched */
    syslen = 0;
    sys_bn = &bn;
    gels_QBlit(&st, &bn);
    gels_QBSBlit(&st, &bn);
    gfx_lvo_OwnBlitter(&gb.pub);
    gfx_lvo_DisownBlitter(&gb.pub);
    gfx_lvo_WaitBlit(&gb.pub);
    gfx_hook_wait_blit(&gb.priv.env);
    if (!streq(syslog, "QSODWW")) fail("side by side: the blitter calls not the OS's");
    if (gb.pub.blthd || gb.pub.bsblthd || gb.pub.blitlock) fail("side by side: our own queue used");
    st.sys_blitter = 0;
    st.has_blitter = 1;
}

static void blocked_disown(void);
static int block_other;
static void blocked_disown(void) { cur_task = &tasks_id[block_other]; gels_DisownBlitter(&st); }

int main(void)
{
    int i;
    gb.priv.env.ctx = &gb;
    gb.priv.env.base = &gb.pub;
    gb.priv.env.alloc = e_alloc;
    gb.priv.env.free = e_free;
    gb.priv.env.forbid = e_nop;
    gb.priv.env.permit = e_nop;
    gb.priv.env.disable = e_disable;
    gb.priv.env.enable = e_enable;
    gb.priv.env.sem_obtain = e_sem_obtain;
    gb.priv.env.sem_release = e_sem_release;
    gb.priv.env.find_task = e_find_task;
    gb.priv.env.wait = e_wait;
    gb.priv.env.signal = e_signal;
    gb.priv.env.chip_read = chip_read;
    gb.priv.env.chip_write = chip_write;
    gb.priv.env.type_of_mem = e_type_of_mem;
    gb.priv.area[GFX_AREA_GELS] = &st;
    gb.pub.blitwaitq.head = (struct gfx_node *)&gb.pub.blitwaitq.tail;
    gb.pub.blitwaitq.tail = 0;
    gb.pub.blitwaitq.tailpred = (struct gfx_node *)&gb.pub.blitwaitq.head;
    st.env = &gb.priv.env;
    st.call_blit = node_call;
    st.call_cleanup = node_cleanup;
    st.has_blitter = 1;
    gels_init(&st);
    cur_task = &tasks_id[0];

    for (;;) {
        int32_t op = next();
        switch (op) {
        case 0:   /* end */
            settle();
            /* through the register entries, which find the state in the base */
            gfx_lvo_OwnBlitter(&gb.pub);
            if (gb.pub.blitowner != cur_task) fail("gfx_lvo_OwnBlitter");
            gfx_lvo_DisownBlitter(&gb.pub);
            gfx_lvo_WaitBlit(&gb.pub);
            side_by_side();
            if (disabled) fail("left Disable()d");
            if (sem_depth) fail("layer lock left held");
            if (gb.pub.blitlock) fail("blitter left owned");
            printf("end %lu blits (%lu drawing, %lu shifted)%s\n", blits, cblits, shifted, failed ? " FAILED" : "");
            return failed != 0;
        case 1: { /* BUF id size chip seed */
            int id = next(); uint32_t size = (uint32_t)next(); int chip = next(); uint32_t seed = (uint32_t)next();
            if (id >= NB) { fail("too many buffers"); return 1; }
            buf[id].p = bump(size, chip); buf[id].size = size; fill(id, seed);
            break;
        }
        case 71: { /* GBUF id size seed: in Fast RAM, ending at the guard page */
            int id = next(); uint32_t size = (uint32_t)next(); uint32_t seed = (uint32_t)next();
            if (id >= NB) { fail("too many buffers"); return 1; }
            buf[id].p = guarded(size); buf[id].size = size; fill(id, seed);
            break;
        }
        case 2: { /* BITMAP id bpr rows flags depth planes[8] */
            struct gfx_bitmap *b = &bms[next()];
            b->bytesperrow = (uint16_t)next(); b->rows = (uint16_t)next(); b->flags = (uint8_t)next(); b->depth = (uint8_t)next();
            for (i = 0; i < 8; i++) b->planes[i] = plane_spec();
            break;
        }
        case 3: { /* LAYER id screen-bm minx miny maxx maxy sx sy flags ncr (minx miny maxx maxy kind bm)... */
            struct gfx_layer *l = &layers[next()];
            int n, k;
            struct gfx_cliprect **pp;
            (void)next();
            l->bounds.minx = (int16_t)next(); l->bounds.miny = (int16_t)next(); l->bounds.maxx = (int16_t)next(); l->bounds.maxy = (int16_t)next();
            l->scroll_x = (int16_t)next(); l->scroll_y = (int16_t)next(); l->flags = (uint16_t)next();
            n = next(); pp = &l->cliprect;
            for (k = 0; k < n; k++) {
                struct gfx_cliprect *c = &crs[next()];
                int kind, bm;
                c->bounds.minx = (int16_t)next(); c->bounds.miny = (int16_t)next(); c->bounds.maxx = (int16_t)next(); c->bounds.maxy = (int16_t)next();
                kind = next(); bm = next();
                c->lobs = kind ? (void *)&layers[1] : 0;
                c->bitmap = kind == 1 ? &bms[bm] : 0;
                *pp = c; pp = &c->next;
            }
            *pp = 0;
            break;
        }
        case 4: { /* RP id bm layer mask fg bg mode */
            struct gfx_rastport *r = &rps[next()];
            int l;
            r->bitmap = &bms[next()]; l = next(); r->layer = l < 0 ? 0 : &layers[l];
            r->mask = (uint8_t)next(); r->fgpen = (int8_t)next(); r->bgpen = (int8_t)next(); r->drawmode = (int8_t)next();
            r->gelsinfo = &gi;
            break;
        }
        case 5: { /* TEMPLATE buf srcx srcmod rp dx dy w h */
            int b = next(), sx = next(), sm = next(), r = next(), dx = next(), dy = next(), w = next(), h = next();
            gels_BltTemplate(&st, buf[b].p, sx, sm, &rps[r], dx, dy, w, h);
            break;
        }
        case 6: { /* MASKBLT srcbm sx sy rp dx dy w h minterm maskbuf */
            int sb = next(), sx = next(), sy = next(), r = next(), dx = next(), dy = next(), w = next(), h = next();
            uint32_t mt = (uint32_t)next(); int mb = next();
            gels_BltMaskBitMapRastPort(&st, &bms[sb], sx, sy, &rps[r], dx, dy, w, h, mt, buf[mb].p);
            break;
        }
        case 7: { /* CLEAR buf count flags */
            int b = next(); uint32_t count = (uint32_t)next(), flags = (uint32_t)next();
            gels_BltClear(&st, buf[b].p, count, flags);
            break;
        }
        case 8: { /* HASH n ids: those buffers, once the blitter is done */
            uint32_t h = 2166136261u;
            int n = next();
            settle();
            while (n--) { int id = next(); h = fnv(h, buf[id].p, buf[id].size); }
            printf("hash %lu\n", (unsigned long)h);
            break;
        }
        case 9: /* RESET */
            settle();
            chiptop = fasttop = 0;
            beamcount = 0; last_nslots = 0; animkey = 0;
            for (i = 0; i < NB; i++) buf[i].p = 0;
            loglen = 0; logbuf[0] = 0;
            break;
        case 20: { /* GELS sprrsrvd left right top bottom */
            gi.sprrsrvd = (int8_t)next(); gi.leftmost = (int16_t)next(); gi.rightmost = (int16_t)next();
            gi.topmost = (int16_t)next(); gi.bottommost = (int16_t)next();
            gi.nextline = nextline; gi.lastcolor = lastcolor; gi.collhandler = &ct;
            for (i = 0; i < 16; i++) ct.collptrs[i] = 0;
            gels_InitGels(&st, &head, &tail, &gi);
            break;
        }
        case 21: { /* BOB id w h depth pick onoff vsflags imgbuf savebuf(-1) db bufbuf memask hitmask maskbuf linebuf */
            int id = next();
            struct gl_vsprite *v = &vss[id];
            struct gl_bob *b = &bobs[id];
            int sb, db, bb, mk, ln;
            v->width = (int16_t)next(); v->height = (int16_t)next(); v->depth = (int16_t)next();
            v->planepick = (int8_t)next(); v->planeonoff = (int8_t)next(); v->flags = (int16_t)next();
            v->imagedata = (int16_t *)buf[next()].p;
            sb = next(); db = next(); bb = next();
            v->memask = (int16_t)next(); v->hitmask = (int16_t)next();
            mk = next(); ln = next();
            v->collmask = mk < 0 ? 0 : (int16_t *)buf[mk].p;
            v->borderline = ln < 0 ? 0 : (int16_t *)buf[ln].p;
            v->sprcolors = 0;
            v->x = v->y = v->oldx = v->oldy = 0;
            v->clearpath = v->drawpath = 0;
            v->vsbob = b;
            b->flags = 0;
            b->bobvsprite = v;
            b->savebuffer = sb < 0 ? 0 : (int16_t *)buf[sb].p;
            b->before = b->after = 0;
            b->bobcomp = 0;
            b->dbuffer = db ? &dbs[id] : 0;
            if (db) { dbs[id].bufpath = 0; dbs[id].bufbuffer = (int16_t *)buf[bb].p; }
            gels_InitMasks(&st, v);
            b->imageshadow = v->collmask;
            break;
        }
        case 22: gels_AddBob(&st, &bobs[next()], &rps[0]); break;
        case 23: { int id = next(); vss[id].x = (int16_t)next(); vss[id].y = (int16_t)next(); break; }
        case 24: { int id = next(), o = next(); bobs[id].after = o < 0 ? 0 : &bobs[o]; break; }
        case 25: gels_DrawGList(&st, &rps[0], 0); break;
        case 26: gels_SortGList(&st, &rps[0]); break;
        case 27: bobs[next()].flags |= GL_BOBSAWAY; break;
        case 28: gels_RemIBob(&st, &bobs[next()], &rps[0], 0); break;
        case 29: rps[0].bitmap = &bms[next()]; break;
        case 30: { /* COLL: SetCollision for the bits in a mask, then DoCollision */
            int32_t m = next();
            for (i = 0; i < 16; i++) gels_SetCollision(&st, (uint32_t)i, (m >> i & 1) ? colls[i] : 0, &gi);
            loglen = 0; logbuf[0] = 0;
            gels_DoCollision(&st, &rps[0]);
            printf("coll%s\n", logbuf);
            break;
        }
        case 31: { /* LIST */
            struct gl_vsprite *v;
            printf("list");
            for (v = head.next; v && v != &tail; v = v->next)
                printf(" %d:%d:%d", vs_id(v), (int)(uint16_t)v->flags, v->vsbob ? (int)(uint16_t)v->vsbob->flags : 0);
            printf("\n");
            break;
        }
        case 32: { /* VSPRITE id y x height colors(0 none, 1-3) */
            int id = next(), c;
            struct gl_vsprite *v = &vss[id];
            v->y = (int16_t)next(); v->x = (int16_t)next(); v->height = (int16_t)next(); c = next();
            v->width = 1; v->depth = 2; v->flags = GL_VSPRITE; v->vsbob = 0;
            v->sprcolors = c ? colors[c - 1] : 0;
            gels_AddVSprite(&st, v, &rps[0]);
            break;
        }
        case 33: /* SLOTS */
            printf("slots");
            for (i = 0; i < (int)last_nslots; i++)
                printf(" %d:%d:%d:%d", vs_id(last_slots[i].vs), last_slots[i].sprite, last_slots[i].y, last_slots[i].flags);
            printf("\n");
            last_nslots = 0;
            break;
        case 34: /* REMVS */
            gels_RemVSprite(&st, &vss[next()]);
            break;
        case 40: { /* ANIMOB id y x yvel xvel yacc xacc ringy ringx routine */
            struct gl_animob *o = &obs[next()];
            o->any = (int16_t)next(); o->anx = (int16_t)next(); o->yvel = (int16_t)next(); o->xvel = (int16_t)next();
            o->yaccel = (int16_t)next(); o->xaccel = (int16_t)next(); o->ringytrans = (int16_t)next(); o->ringxtrans = (int16_t)next();
            o->animoroutine = next() ? ob_routine : 0;
            o->clock = 0; o->headcomp = 0;
            break;
        }
        case 41: { /* COMP id bob timeset ytrans xtrans flags routine */
            struct gl_animcomp *c = &comps[next()];
            int b = next();
            c->animbob = b < 0 ? 0 : &bobs[b];
            c->timeset = (int16_t)next(); c->ytrans = (int16_t)next(); c->xtrans = (int16_t)next(); c->flags = (int16_t)next();
            c->animcroutine = next() ? comp_routine : 0;
            c->nextcomp = c->prevcomp = c->nextseq = c->prevseq = 0; c->timer = 0; c->headob = 0;
            if (b >= 0) bobs[b].bobcomp = c;
            break;
        }
        case 42: { int a = next(), b = next(); comps[a].nextseq = &comps[b]; comps[b].prevseq = &comps[a]; break; }
        case 43: { int a = next(), b = next(); comps[a].nextcomp = &comps[b]; comps[b].prevcomp = &comps[a]; break; }
        case 44: { int o = next(), c = next(); obs[o].headcomp = &comps[c]; break; }
        case 45: gels_AddAnimOb(&st, &obs[next()], &animkey, &rps[0]); break;
        case 46: loglen = 0; logbuf[0] = 0; gels_Animate(&st, &animkey, &rps[0]); printf("animate%s\n", logbuf); break;
        case 47: { /* ANIMDUMP */
            struct gl_animob *o;
            printf("anim");
            for (o = animkey; o; o = o->nextob) {
                struct gl_animcomp *c;
                printf(" ob%d %ld %d,%d %d,%d %d,%d [", (int)(o - obs), (long)o->clock, o->anx, o->any, o->xvel, o->yvel, o->anoldx, o->anoldy);
                for (c = o->headcomp; c; c = c->nextcomp)
                    printf(" %d:%d:%d,%d", (int)(c - comps), c->timer, c->animbob ? c->animbob->bobvsprite->x : 0, c->animbob ? c->animbob->bobvsprite->y : 0);
                printf(" ]");
            }
            printf("\n");
            break;
        }
        case 48: { /* GETGB ob db failat: GetGBuffers, then FreeGBuffers when it got them */
            int o = next(), db = next(), r;
            long before = allocs;
            alloc_fail_at = next(); alloc_count = 0;
            r = gels_GetGBuffers(&st, &obs[o], &rps[0], db);
            printf("getgb %d %ld", (int)r, allocs - before);
            if (r) { gels_FreeGBuffers(&st, &obs[o], &rps[0], db); printf(" %ld", allocs - before); }
            printf("\n");
            alloc_fail_at = -1;
            break;
        }
        case 49: animkey = 0; break;
        case 60: { /* QNODE id again cleanup buf value beamsync */
            struct tnode *t = &nodes[next()];
            t->id = (int)(t - nodes); t->again = next(); t->bn.stat = next() ? GL_CLEANUP : 0;
            t->buf = next(); t->value = next(); t->bn.beamsync = (int16_t)next(); t->calls = 0;
            t->bn.function = (void *)node_call;
            break;
        }
        case 61: { int n = next(); logs(" Q"); logn(n); gels_QBlit(&st, &nodes[n].bn); logs(" q"); break; }
        case 62: { int n = next(); logs(" S"); logn(n); gels_QBSBlit(&st, &nodes[n].bn); logs(" s"); break; }
        case 63: { int t = next(); cur_task = &tasks_id[t]; gels_OwnBlitter(&st); logs(" O"); logn(t); break; }
        case 64: { int t = next(); cur_task = &tasks_id[t]; gels_DisownBlitter(&st); logs(" D"); logn(t); break; }
        case 65: { /* OWNBLOCK task other: task waits, other disowns meanwhile */
            int t = next();
            block_other = next(); on_block = blocked_disown;
            cur_task = &tasks_id[t]; gels_OwnBlitter(&st);
            if (on_block) fail("OwnBlitter did not wait");
            logs(" O"); logn(t);
            break;
        }
        case 66: settle(); gels_vertb(&st); break;
        case 67: { int n = next(); while (n--) tick(); break; }
        case 68: settle(); printf("qlog%s\n", logbuf); loglen = 0; logbuf[0] = 0; break;
        case 69: gels_WaitBlit(&st); if (bbusy) fail("WaitBlit returned busy"); break;
        case 70: /* NOBLITTER / BLITTER */ st.has_blitter = (uint16_t)next(); break;
        default:
            printf("bad op %ld at %d\n", (long)op, pc);
            return 1;
        }
    }
}
