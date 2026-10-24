/* test_gfx_display's harness: the display core on a simulated machine.
 * The machine: memory for exec's AllocMem, the custom chips' registers, a
 * copper that runs the lists (WAIT, MOVE, COPJMP, 4 colour clocks an
 * instruction, its WAIT comparing the beam's low 8 bits as the hardware
 * does) and an Agnus/Denise/Lisa that fetches bitplanes between DDFSTRT and
 * DDFSTOP inside the vertical window and shows them inside DIWSTRT/DIWSTOP
 * with BPLCON1's scroll, HAM6, HAM8, EHB, dual playfield, AGA's palette
 * banks, LOCT, PF2OF and BPLAM, in lores, hires and superhires, with AGA's
 * wider fetches (FMODE bits 0-1). A line fetched against the chips' rules
 * (more planes than its fetch shows, a DDFSTOP off the fetch's cycle, a
 * pointer or modulo off a wider fetch's width) is counted as a failure.
 * The registers a line uses are taken at colour clock 0x20, before any
 * fetch. The frame is kept in 35ns (superhires) pixels; what it shows is
 * hashed over each ViewPort's rectangle and compared with model.py's
 * answer. */
#include <stdio.h>
#include <string.h>
#include "display.h"
struct sbm { int depth, rowbytes, rows, inter, seed; };
struct svp {
    int modes, dx, dy, w, h, bm, bm2, bmB, count;
    int rx[2], ry[2], rx2[2], ry2[2];
    int col, full, b1, bplam, hasclip, pad0;
    int clip[4];
    int ucop, nucop, pad1;
};
struct sscene { int chip, pal, vex, vlace, vx, vy, nvp, vp0, r0, nr; };
#include "scenes.h"

static unsigned long checks, fails;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; if (fails < 40) printf("FAIL %s (line %d)\n", what, __LINE__); } } while (0)

/* ---- memory ------------------------------------------------------------ */
enum { ARENA = 1 << 21, MAXA = 4096 };
static uint8_t arena[ARENA] __attribute__((aligned(8)));
static uint32_t used;
static struct { void *p; uint32_t size; } live[MAXA];
static int nlive;
static long fail_after = -1;
static unsigned long nallocs;

static void *m_alloc(void *, uint32_t size, uint32_t flags)
{
    if (fail_after == 0) return 0;
    if (fail_after > 0) fail_after--;
    uint32_t s = (size + 7) & ~7u;
    if (!size || used + s > ARENA || nlive == MAXA) { printf("FAIL arena\n"); fails++; return 0; }
    void *p = arena + used;
    used += s;
    live[nlive].p = p;
    live[nlive++].size = size;
    nallocs++;
    if (flags & GFX_MEMF_CLEAR) memset(p, 0, size);
    else memset(p, 0xA5, size);
    return p;
}
static void m_free(void *, void *p, uint32_t size)
{
    for (int i = 0; i < nlive; i++)
        if (live[i].p == p) {
            if (live[i].size != size) { fails++; printf("FAIL free size %lu for %lu\n", (unsigned long)size, (unsigned long)live[i].size); }
            live[i] = live[--nlive];
            if (!nlive) used = 0;
            return;
        }
    fails++;
    printf("FAIL free of unknown memory\n");
}
static uint32_t m_avail(void *, uint32_t) { return ARENA - used; }

/* the test's own memory: bitmaps, colour tables */
static uint8_t tpool[1 << 21] __attribute__((aligned(8)));
static uint32_t tused;
static void *talloc(uint32_t n)
{
    uint32_t s = (n + 7) & ~7u;
    if (tused + s > sizeof tpool) { printf("FAIL tpool\n"); fails++; return 0; }
    void *p = tpool + tused;
    tused += s;
    memset(p, 0, n);
    return p;
}

/* ---- the machine ---------------------------------------------------------- */
enum { FBW = 1840, LINES = 314 };            /* 35ns pixels from lores 0 */
static uint32_t fb[LINES][FBW];
static uint32_t stamp[LINES], frameno;
struct machine {
    int chip;                           /* 0 OCS, 1 ECS, 2 AGA */
    bool palagnus;
    uint16_t reg[256];
    uint32_t cop1lc, cop2lc, bplpt[8], sprpt[8];
    uint32_t palette[256];
    bool lof;
    uint16_t beamcon0;
    bool beamset;
    int cpu_t;                          /* the beam the CPU sees, in colour clocks */
    int denise_toggle;
    int copper_errors;
};
static machine M;
static int frame_lines() { bool pal = M.beamset ? (M.beamcon0 & 0x20) != 0 : M.palagnus; return (pal ? 312 : 262) + (M.lof ? 1 : 0); }

static uint32_t expand(uint16_t c) { return ((c >> 8 & 15) * 17u) << 16 | ((c >> 4 & 15) * 17u) << 8 | (c & 15) * 17u; }

static void sim_write(uint32_t r, uint16_t v, bool copper, uint32_t *pc)
{
    r &= 0x1FE;
    M.reg[r >> 1] = v;
    switch (r) {
    case GD_COP1LCH: M.cop1lc = (M.cop1lc & 0xFFFF) | (uint32_t)v << 16; return;
    case GD_COP1LCL: M.cop1lc = (M.cop1lc & 0xFFFF0000u) | v; return;
    case GD_COP2LCH: M.cop2lc = (M.cop2lc & 0xFFFF) | (uint32_t)v << 16; return;
    case GD_COP2LCL: M.cop2lc = (M.cop2lc & 0xFFFF0000u) | v; return;
    case GD_COPJMP1: if (copper) *pc = M.cop1lc; return;
    case GD_COPJMP2: if (copper) *pc = M.cop2lc; return;
    case GD_BEAMCON0: M.beamcon0 = v; M.beamset = M.chip > 0; return;
    }
    if (r >= GD_BPL1PTH && r < GD_BPL1PTH + 32) {
        int k = (r - GD_BPL1PTH) >> 2;
        if (r & 2) M.bplpt[k] = (M.bplpt[k] & 0xFFFF0000u) | v;
        else M.bplpt[k] = (M.bplpt[k] & 0xFFFF) | (uint32_t)v << 16;
        return;
    }
    if (r >= GD_SPR0PTH && r < GD_SPR0PTH + 32) {
        int k = (r - GD_SPR0PTH) >> 2;
        if (r & 2) M.sprpt[k] = (M.sprpt[k] & 0xFFFF0000u) | v;
        else M.sprpt[k] = (M.sprpt[k] & 0xFFFF) | (uint32_t)v << 16;
        return;
    }
    if (r >= GD_COLOR00 && r < GD_COLOR00 + 64) {
        int i = (r - GD_COLOR00) >> 1;
        if (M.chip < 2) { M.palette[i] = expand(v); return; }
        uint16_t c3 = M.reg[GD_BPLCON3 >> 1];
        i += 32 * (c3 >> 13);
        if (!(c3 & 0x200)) M.palette[i] = expand(v);
        else {
            uint32_t o = M.palette[i];
            M.palette[i] = (o & 0xF0F0F0) | (uint32_t)(v >> 8 & 15) << 16 | (uint32_t)(v >> 4 & 15) << 8 | (v & 15);
        }
    }
}

/* the copper, run from colour clock t0 of the frame until t1 */
static uint32_t pc;
static int cop_t;
static bool cop_halt;
static void copper_run(int t1)
{
    const int lines = frame_lines();
    while (!cop_halt && cop_t < t1) {
        const uint16_t *w = (const uint16_t *)(uintptr_t)pc;
        uint16_t a = w[0], b = w[1];
        if (a & 1) {
            if (b & 1) { pc += 4; cop_t += 4; continue; }   /* SKIP: not used */
            int vp = a >> 8, hp = a & 0xFE;
            int t = -1;
            for (int v = cop_t / 227; v < lines && t < 0; v++) {
                int lo = v & 0xFF;
                if (lo > vp) t = v * 227;
                else if (lo == vp && hp <= 226) t = v * 227 + hp;
                if (t >= 0 && t < cop_t) t = cop_t;
            }
            if (t < 0) { cop_halt = true; return; }
            if (t > cop_t) { cop_t = t; continue; }
            pc += 4;
            cop_t += 4;
        } else {
            uint32_t r = a & 0x1FE;
            pc += 4;
            cop_t += 4;
            if (r < 0x80) { M.copper_errors++; cop_halt = true; return; }
            sim_write(r, b, true, &pc);
        }
    }
}

static uint16_t fetched[8][256];
static unsigned long misfetch;          /* lines fetched against the chips' rules */
static void render_line(int v)
{
    const bool aga = M.chip == 2;
    uint16_t con0 = M.reg[GD_BPLCON0 >> 1], con1 = M.reg[GD_BPLCON1 >> 1], con2 = M.reg[GD_BPLCON2 >> 1];
    uint16_t con3 = M.reg[GD_BPLCON3 >> 1], con4 = M.reg[GD_BPLCON4 >> 1];
    int planes = (con0 >> 12) & 7;
    if (aga && (con0 & 0x10)) planes = 8;
    if (planes > 6 && !aga) planes = 6;
    uint16_t ds = M.reg[GD_DIWSTRT >> 1], de = M.reg[GD_DIWSTOP >> 1];
    int vstart = ds >> 8, vs = de >> 8, vstop = (vs & 0x80) ? vs : vs | 0x100;
    if (!(planes > 0 && v >= vstart && v < vstop)) return;
    /* the resolution and the fetch (AGA's FMODE bits 0-1: 1x, 2x, 2x, 4x).
     * The planes a fetch shows, and the colour clocks of DDFSTOP's cycle:
     * 8 at 1x, and at 2x and 4x for hires and superhires; lores at 2x and
     * 4x is 16 and 32 (the runtime's rule: the cycle DDFSTOP falls in and
     * one more). Data shows from 2 * DDFSTRT + 17 lores pixels in lores,
     * + 9 in hires and superhires at 1x, + 17 at 2x and 4x. */
    const bool shres = M.chip > 0 && (con0 & 0x40) != 0, hires = !shres && (con0 & 0x8000) != 0;
    const int res = shres ? 2 : hires ? 1 : 0;
    const int fm = aga ? M.reg[GD_FMODE >> 1] & 3 : 0;
    const int fi = fm == 3 ? 2 : fm ? 1 : 0, fw = 1 << fi;
    static const int maxp[3][3] = { { 6, 4, 2 }, { 8, 8, 4 }, { 8, 8, 8 } }, unit[3][3] = { { 8, 8, 8 }, { 16, 8, 8 }, { 32, 16, 8 } };
    if (planes > ((aga && !fi && !res) ? 8 : maxp[fi][res])) misfetch++;
    const int u = unit[fi][res];
    int dstart = M.reg[GD_DDFSTRT >> 1] & (res || fi ? 0xFC : 0xF8), dstop = M.reg[GD_DDFSTOP >> 1] & 0xFC;
    if (fi && (dstart & 7)) misfetch++;
    int cycles = 0, x = dstop - dstart;
    if (x >= 0) {
        if (u == 8) {
            if (x & 7) misfetch++;
            cycles = (x >> 3) + 1;
        } else
            cycles = (x + u - 1) / u + 1;
    }
    int words = cycles * u * (2 << res) / 16;
    if (words > 256) words = 256;
    int16_t mod1 = (int16_t)M.reg[GD_BPL1MOD >> 1], mod2 = (int16_t)M.reg[GD_BPL2MOD >> 1];
    const uint32_t amask = (uint32_t)(2 * fw - 1);
    for (int k = 0; k < planes; k++) {
        int16_t mod = (k & 1) ? mod2 : mod1;
        if ((M.bplpt[k] | (uint32_t)mod) & amask) misfetch++;
        const uint8_t *p = (const uint8_t *)(uintptr_t)(M.bplpt[k] & ~amask);
        for (int i = 0; i < words; i++) fetched[k][i] = (uint16_t)(p[2 * i] << 8 | p[2 * i + 1]);
        M.bplpt[k] += (uint32_t)(2 * words + mod);
    }
    int delay[2];
    for (int f = 0; f < 2; f++) {
        int sh = f ? 4 : 0;
        delay[f] = ((con1 >> sh) & 15) << 2;
        if (aga) delay[f] |= ((con1 >> (sh + 8)) & 3) | ((con1 >> (sh + 10)) & 3) << 6;
    }
    const int base = 4 * (2 * dstart + (res && !fi ? 9 : 17)), p = 4 >> res;
    const int hstart = ds & 0xFF, hstop = (de & 0xFF) | 0x100;
    const bool ham = (con0 & 0x800) != 0, dpf = (con0 & 0x400) != 0;
    uint32_t hold = M.palette[0];
    int off2 = 8;
    if (aga) { int code = (con3 >> 10) & 7; off2 = code ? 1 << code : 0; }
    for (int su = 4 * hstart; su < 4 * hstop && su < FBW; su++) {
        int val = 0;
        for (int k = 0; k < planes; k++) {
            int s = su - base - delay[k & 1];
            if (s < 0) continue;
            int idx = s / p, wd = idx >> 4;
            if (wd < words && (fetched[k][wd] >> (15 - (idx & 15)) & 1)) val |= 1 << k;
        }
        uint32_t col;
        if (dpf) {
            int v1 = 0, v2 = 0;
            for (int k = 0; k < planes; k++)
                if (val >> k & 1) { if (k & 1) v2 |= 1 << (k >> 1); else v1 |= 1 << (k >> 1); }
            uint32_t c1 = M.palette[v1 & 255], c2 = M.palette[(v2 + off2) & 255];
            if (con2 & 0x40) col = v2 ? c2 : v1 ? c1 : M.palette[0];
            else col = v1 ? c1 : v2 ? c2 : M.palette[0];
        } else if (ham && planes == 6 && !aga) {
            int ctl = val >> 4, d = val & 15;
            if (!ctl) col = M.palette[d];
            else {
                int r = hold >> 20 & 15, g = hold >> 12 & 15, b = hold >> 4 & 15;
                if (ctl == 1) b = d; else if (ctl == 2) r = d; else g = d;
                col = expand((uint16_t)(r << 8 | g << 4 | b));
            }
            hold = col;
        } else if (ham && planes == 8 && aga) {
            int ctl = val & 3, d = val >> 2;
            if (!ctl) col = M.palette[d];
            else {
                uint32_t r = hold >> 16 & 255, g = hold >> 8 & 255, b = hold & 255;
                if (ctl == 1) b = (uint32_t)d << 2 | (b & 3); else if (ctl == 2) r = (uint32_t)d << 2 | (r & 3); else g = (uint32_t)d << 2 | (g & 3);
                col = r << 16 | g << 8 | b;
            }
            hold = col;
        } else if (planes == 6 && !ham && !(aga && (con2 & 0x0200))) {   /* AGA's KILLEHB */
            col = M.palette[val & 31];
            if (val & 32) {
                if (aga) col = (col >> 1) & 0x7F7F7F;
                else col = expand((uint16_t)(((col >> 20 & 15) >> 1) << 8 | ((col >> 12 & 15) >> 1) << 4 | ((col >> 4 & 15) >> 1)));
            }
        } else {
            if (aga) val ^= con4 >> 8;
            col = M.palette[val & 255];
        }
        fb[v][su] = col;
    }
    stamp[v] = frameno;
}

static gfx_env E;
static void frame(bool lof)
{
    M.lof = lof;
    frameno++;
    gd_vblank(&E);
    pc = M.cop1lc;
    cop_t = 0;
    cop_halt = false;
    int lines = frame_lines();
    for (int v = 0; v < lines; v++) {
        copper_run(v * 227 + 0x20);
        if (v < LINES) render_line(v);
    }
    copper_run(lines * 227);
    M.cpu_t = 0;
}

/* ---- the env ------------------------------------------------------------- */
static int dis, forb;
static uint32_t task_sigs;
static unsigned long replies;
static void e_forbid(void *) { forb++; }
static void e_permit(void *) { forb--; }
static void e_disable(void *) { dis++; }
static void e_enable(void *) { dis--; }
static void e_sem(void *, gfx_semaphore *) {}
/* the semaphores InitSemaphore'd, so a GfxBase pointer to one can be checked */
static gfx_semaphore *inited[16];
static int ninited;
static void e_sem_init(void *, gfx_semaphore *s) { if (ninited < 16) inited[ninited++] = s; }
static bool was_inited(gfx_semaphore *s)
{
    for (int i = 0; i < ninited; i++)
        if (inited[i] == s) return true;
    return false;
}
static int32_t e_sem_attempt(void *, gfx_semaphore *) { return 1; }
static int task;
static void *e_find_task(void *) { return &task; }
static uint32_t e_wait(void *, uint32_t sigs)
{
    int guard = 0;
    while (!(task_sigs & sigs) && guard++ < 4) frame(true);
    uint32_t got = task_sigs & sigs;
    task_sigs &= ~sigs;
    return got;
}
static void e_signal(void *, void *t, uint32_t sigs) { if (t == &task) task_sigs |= sigs; }
static void e_reply(void *, void *) { replies++; }
static uint16_t e_read(void *, uint32_t r)
{
    M.cpu_t += 37;
    int lines = frame_lines();
    int t = M.cpu_t % (lines * 227), v = t / 227, h = t % 227;
    switch (r) {
    case GD_VPOSR: {
        unsigned id = M.chip == 0 ? (M.palagnus ? 0x10 : 0x00) : M.chip == 1 ? (M.palagnus ? 0x30 : 0x20) : (M.palagnus ? 0x23 : 0x22);
        return (uint16_t)((M.lof ? 0x8000 : 0) | id << 8 | ((v >> 8) & 7));
    }
    case GD_VHPOSR: return (uint16_t)((v & 0xFF) << 8 | h);
    case GD_DENISEID: return M.chip == 0 ? (uint16_t)(0x1234 + 0x1111 * (M.denise_toggle++ & 3)) : M.chip == 1 ? 0xFFFC : 0xFFF8;
    }
    return M.reg[(r & 0x1FE) >> 1];
}
static unsigned long nwrites;
static void e_write(void *, uint32_t r, uint16_t v) { uint32_t dummy = 0; nwrites++; sim_write(r, v, false, &dummy); }

static uint8_t basemem[GFX_BASE_POSSIZE + 16] __attribute__((aligned(8)));
static gfx_base *GB;
static void nl(gfx_list *l) { l->head = (gfx_node *)&l->tail; l->tail = 0; l->tailpred = (gfx_node *)&l->head; }
static void boot(int chip, bool pal, int own_hw = 1)
{
    memset(&M, 0, sizeof M);
    M.chip = chip;
    M.palagnus = pal;
    memset(basemem, 0, sizeof basemem);
    GB = (gfx_base *)basemem;
    nl(&GB->textfonts); nl(&GB->blitwaitq); nl(&GB->tof_waitq); nl(&GB->monitorlist);
    memset(&E, 0, sizeof E);
    E.ctx = &M;
    E.base = GB;
    E.alloc = m_alloc; E.free = m_free; E.avail = m_avail;
    E.forbid = e_forbid; E.permit = e_permit; E.disable = e_disable; E.enable = e_enable;
    ninited = 0;
    E.sem_init = e_sem_init; E.sem_obtain = e_sem; E.sem_obtain_shared = e_sem; E.sem_attempt = e_sem_attempt; E.sem_release = e_sem;
    E.find_task = e_find_task; E.wait = e_wait; E.signal = e_signal; E.reply_msg = e_reply;
    E.chip_read = e_read; E.chip_write = e_write;
    gfx_private_of(GB)->env = E;
    CHECK(gd_init(&E, own_hw) == 1, "gd_init");
    /* a monitor driver takes MonitorListSemaphore, and other programs the
     * rest: each must be a semaphore of its own, initialised */
    gfx_semaphore *sems[4] = { GB->lastchancememory, GB->actiewcprsemaphore, GB->monitorlistsemaphore,
                               GB->hashtablesemaphore };
    for (int i = 0; i < 4; i++) {
        bool own = true;
        for (int j = 0; j < i; j++) own = own && sems[j] != sems[i];
        CHECK(sems[i] && was_inited(sems[i]) && own, "GfxBase's semaphores");
    }
    uint8_t want = chip == 0 ? 0 : chip == 1 ? (GFX_GFXF_HR_AGNUS | GFX_GFXF_HR_DENISE)
                                             : (GFX_GFXF_HR_AGNUS | GFX_GFXF_HR_DENISE | GFX_GFXF_AA_ALICE | GFX_GFXF_AA_LISA);
    CHECK(GB->chiprevbits0 == want, "SetChipRev found the chips");
    CHECK(((GB->displayflags & GFX_PAL) != 0) == pal, "DisplayFlags PAL");
}
static void shutdown()
{
    gd_exit(&E);
    CHECK(nlive == 0, "all memory given back");
    if (nlive) { for (int i = 0; i < nlive && i < 5; i++) printf("  kept %lu bytes\n", (unsigned long)live[i].size); nlive = 0; used = 0; }
    CHECK(dis == 0 && forb == 0, "Disable/Forbid balanced");
}

/* ---- scenes ----------------------------------------------------------------- */
static gfx_bitmap *make_bm(const sbm &d)
{
    gfx_bitmap *bm = (gfx_bitmap *)talloc(sizeof(gfx_bitmap));
    bm->depth = (uint8_t)d.depth;
    bm->rows = (uint16_t)d.rows;
    uint32_t plane = (uint32_t)d.rowbytes * d.rows;
    uint8_t *mem = (uint8_t *)talloc(plane * (d.depth ? d.depth : 1));
    bm->bytesperrow = (uint16_t)(d.inter ? d.rowbytes * d.depth : d.rowbytes);
    for (int k = 0; k < d.depth; k++) {
        bm->planes[k] = d.inter ? mem + k * d.rowbytes : mem + k * plane;
        uint32_t s = (uint32_t)d.seed + 7919u * (uint32_t)k;
        for (int r = 0; r < d.rows; r++)
            for (int b = 0; b < d.rowbytes; b++) {
                s = s * 1103515245u + 12345u;
                bm->planes[k][r * bm->bytesperrow + b] = (uint8_t)(s >> 16);
            }
    }
    return bm;
}

static uint32_t hash_rect(int l0, int l1, int x0, int x1)
{
    uint32_t h = 2166136261u;
    for (int l = l0; l < l1; l++)
        for (int x = x0; x < x1; x++) {
            uint32_t c = (l < LINES && x >= 0 && x < FBW && stamp[l] == frameno) ? fb[l][x] : 0x123457;
            h = (h ^ (c >> 16 & 255)) * 16777619u;
            h = (h ^ (c >> 8 & 255)) * 16777619u;
            h = (h ^ (c & 255)) * 16777619u;
        }
    return h;
}

static void run_scene(int si)
{
    const sscene &S = SC[si];
    tused = 0;
    misfetch = 0;
    boot(S.chip, S.pal);
    gd_view view;
    gd_InitView(&E, &view);
    CHECK(view.dxoffset == 0x81 && view.dyoffset == 0x2C && !view.viewport, "InitView");
    view.dxoffset = (int16_t)S.vx;
    view.dyoffset = (int16_t)S.vy;
    view.modes = S.vlace ? GD_LACE : 0;
    gd_viewextra *ve = 0;
    if (S.vex) {
        ve = (gd_viewextra *)gd_GfxNew(&E, GD_VIEW_EXTRA_TYPE);
        ve->monitor = gd_OpenMonitor(&E, 0, S.vex == 1 ? GD_NTSC_MONITOR_ID : GD_PAL_MONITOR_ID);
        CHECK(ve->monitor != 0, "OpenMonitor by ID");
        gd_GfxAssociate(&E, &view, &ve->n);
        view.modes |= GD_EXTEND_VSTRUCT;
    }
    gd_viewport vps[3];
    gd_rasinfo ri[3][2];
    gfx_colormap cms[3];
    gfx_bitmap *bmB[3];
    gd_vpextra *vpe[3] = { 0, 0, 0 };
    gd_dbufinfo *dbi[3] = { 0, 0, 0 };
    for (int i = 0; i < S.nvp; i++) {
        const svp &V = SVP[S.vp0 + i];
        gd_viewport *vp = &vps[i];
        gd_InitVPort(&E, vp);
        vp->modes = (uint16_t)V.modes;
        vp->dxoffset = (int16_t)V.dx;
        vp->dyoffset = (int16_t)V.dy;
        vp->dwidth = (int16_t)V.w;
        vp->dheight = (int16_t)V.h;
        memset(ri[i], 0, sizeof ri[i]);
        ri[i][0].bitmap = make_bm(SBM[V.bm]);
        ri[i][0].rxoffset = (int16_t)V.rx[0];
        ri[i][0].ryoffset = (int16_t)V.ry[0];
        if (V.bm2 >= 0) {
            ri[i][1].bitmap = make_bm(SBM[V.bm2]);
            ri[i][1].rxoffset = (int16_t)V.rx[1];
            ri[i][1].ryoffset = (int16_t)V.ry[1];
            ri[i][0].next = &ri[i][1];
        }
        bmB[i] = make_bm(SBM[V.bmB]);
        vp->rasinfo = &ri[i][0];
        gfx_colormap *cm = &cms[i];
        memset(cm, 0, sizeof *cm);
        cm->type = GFX_COLORMAP_TYPE_V39;
        cm->count = (uint16_t)V.count;
        cm->colortable = (uint16_t *)talloc(2 * V.count);
        cm->lowcolorbits = (uint16_t *)talloc(2 * V.count);
        for (int c = 0; c < V.count; c++) { cm->colortable[c] = COLHI[V.col + c]; cm->lowcolorbits[c] = COLLO[V.col + c]; }
        cm->vpmodeid = GD_INVALID_ID;
        cm->spritebase_even = cm->spritebase_odd = 16;
        cm->bp_1_base = (uint16_t)V.b1;
        cm->bp_0_base = (uint16_t)V.bplam;
        cm->spriteresolution = 0xFF;
        cm->vp = vp;
        if (V.full) cm->auxflags |= GD_CMAF_FULLPALETTE;
        if (V.hasclip) {
            vpe[i] = (gd_vpextra *)gd_GfxNew(&E, GD_VIEWPORT_EXTRA_TYPE);
            vpe[i]->displayclip.minx = (int16_t)V.clip[0];
            vpe[i]->displayclip.miny = (int16_t)V.clip[1];
            vpe[i]->displayclip.maxx = (int16_t)V.clip[2];
            vpe[i]->displayclip.maxy = (int16_t)V.clip[3];
            cm->vpe = vpe[i];
        }
        vp->colormap = cm;
        if (V.nucop) {
            gd_ucoplist *u = (gd_ucoplist *)m_alloc(0, sizeof(gd_ucoplist), GFX_MEMF_PUBLIC | GFX_MEMF_CLEAR);
            CHECK(gd_UCopperListInit(&E, u, 2) != 0, "UCopperListInit");
            for (int k = 0; k < V.nucop; k++) {
                const int16_t *c = UCOP[V.ucop + k];
                gd_CWait(&E, u, c[0], 0);
                gd_CBump(&E, u);
                gd_CMove(&E, u, (void *)(uintptr_t)(0xDFF000u + GD_COLOR00 + 2u * (unsigned)c[1]), c[2]);
                gd_CBump(&E, u);
            }
            gd_CWait(&E, u, 10000, 255);
            gd_CBump(&E, u);
            vp->ucopins = u;
        }
        if (i) vps[i - 1].next = vp;
    }
    view.viewport = S.nvp ? &vps[0] : 0;
    for (int i = 0; i < S.nvp; i++) {
        uint32_t r = gd_MakeVPort(&E, &view, &vps[i]);
        CHECK(r == GD_MVP_OK, "MakeVPort");
        if (r) printf("  scene %d vp %d: MakeVPort %lu\n", si, i, (unsigned long)r);
        CHECK(gd_CalcIVG(&E, &view, &vps[i]) <= 13, "CalcIVG fits the scene's gaps");
    }
    CHECK(gd_MrgCop(&E, &view) == GD_MCOP_OK, "MrgCop");
    gd_LoadView(&E, &view);
    CHECK(GB->actiview == &view, "LoadView sets ActiView");
    int nf = S.vlace ? 2 : 1;
    int ri0 = S.r0, rn = S.nr;
    for (int phase = 0; phase < 3; phase++) {
        if (phase == 1)
            for (int i = 0; i < S.nvp; i++) {
                const svp &V = SVP[S.vp0 + i];
                ri[i][0].rxoffset = (int16_t)V.rx2[0];
                ri[i][0].ryoffset = (int16_t)V.ry2[0];
                ri[i][1].rxoffset = (int16_t)V.rx2[1];
                ri[i][1].ryoffset = (int16_t)V.ry2[1];
                gd_ScrollVPort(&E, &vps[i]);
            }
        if (phase == 2) {
            replies = 0;
            for (int i = 0; i < S.nvp; i++) {
                dbi[i] = gd_AllocDBufInfo(&E, &vps[i]);
                dbi[i]->safemessage.replyport = (void *)1;
                dbi[i]->dispmessage.replyport = (void *)1;
                gd_ChangeVPBitMap(&E, &vps[i], bmB[i], dbi[i]);
            }
            CHECK(replies == 0, "ChangeVPBitMap replies at the vertical blank, not before");
        }
        for (int f = 0; f < nf; f++) {
            frame(f == 0);
            if (phase == 2 && f == 0)
                CHECK(replies == (unsigned)S.nvp, "SafeMessage at the vertical blank, DispMessage a field later");
            for (int k = ri0; k < ri0 + rn; k++) {
                const int16_t *R = RECT[k];
                if (R[0] != phase || R[1] != f) continue;
#ifdef DUMP
                if (si == DUMP0 && phase == DUMP1 && f == DUMP2 && R[2] == DUMP3) {
                    int md = SVP[S.vp0 + R[2]].modes, ppx = (md & 0x20) ? 1 : (md & 0x8000) ? 2 : 4;
                    for (int l = R[3]; l < R[4]; l++) {
                        printf("L %d", l);
                        for (int x = R[5]; x < R[6] && x < R[5] + 400 / 7 * ppx; x += ppx) printf(" %06x", (unsigned)(stamp[l] == frameno ? fb[l][x] : 0x123457));
                        printf("\n");
                    }
                }
#endif
                printf("scene %d phase %d field %d vp %d %lu\n", si, phase, f, R[2],
                       (unsigned long)hash_rect(R[3], R[4], R[5], R[6]));
            }
        }
        if (phase == 2) {
            if (nf == 1) frame(true);   /* the field shown: DispMessage at the next blank */
            CHECK(replies == 2u * (unsigned)S.nvp, "ChangeVPBitMap's two messages each");
            for (int i = 0; i < S.nvp; i++) gd_FreeDBufInfo(&E, dbi[i]);
        }
    }
    CHECK(M.copper_errors == 0, "no copper danger");
    CHECK(misfetch == 0, "every line fetched by the chips' rules");
    if (misfetch) printf("  scene %d: %lu lines fetched against the rules\n", si, misfetch);
    gd_LoadView(&E, 0);
    for (int i = 0; i < S.nvp; i++) {
        gd_FreeVPortCopLists(&E, &vps[i]);
        CHECK(!vps[i].dspins && !vps[i].ucopins, "FreeVPortCopLists clears");
        if (vpe[i]) gd_GfxFree(&E, &vpe[i]->n);
    }
    gd_FreeCprList(&E, view.lofcprlist);
    gd_FreeCprList(&E, view.shfcprlist);
    if (ve) {
        CHECK(gd_GfxLookUp(&E, &view) == &ve->n, "GfxLookUp");
        CHECK(gd_CloseMonitor(&E, ve->monitor) == 0, "CloseMonitor");
        gd_GfxFree(&E, &ve->n);
        CHECK(gd_GfxLookUp(&E, &view) == 0, "GfxFree ends the association");
    }
    shutdown();
}

/* ---- the rest: database, monitors, VideoControl, sprites, beam, memory ---- */
static bool same_str(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}
static gfx_tagitem T(uint32_t t, uint32_t d) { gfx_tagitem x; x.tag = t; x.data = d; return x; }

static void misc(int chip, bool pal)
{
    tused = 0;
    boot(chip, pal);
    /* the database */
    int n = 0;
    uint32_t prev = 0, id = GD_INVALID_ID;
    bool sorted = true, found = true;
    while ((id = gd_NextDisplayInfo(&E, id)) != GD_INVALID_ID && n < 500) {
        if (n && id <= prev) sorted = false;
        if (!gd_FindDisplayInfo(&E, id)) found = false;
        prev = id;
        n++;
    }
    CHECK(n == 90 && sorted && found, "NextDisplayInfo walks every mode, in order");
    gd_displayinfo di;
    void *h = gd_FindDisplayInfo(&E, GD_PAL_MONITOR_ID | GD_SUPER_KEY);
    CHECK(gd_GetDisplayInfoData(&E, h, &di, sizeof di, GD_DTAG_DISP, 0) == 56, "DTAG_DISP size");
    CHECK(di.header.structid == GD_DTAG_DISP && di.header.displayid == (GD_PAL_MONITOR_ID | GD_SUPER_KEY)
          && di.header.skipid == GFX_TAG_SKIP && di.header.length == 7, "QueryHeader");
    CHECK(di.notavailable == (chip ? 0 : GD_DI_AVAIL_NOCHIPS), "SuperHires needs ECS Denise");
    CHECK((di.propertyflags & GD_DIPF_IS_PAL) && (di.propertyflags & GD_DIPF_IS_ECS), "DIPF PAL, ECS");
    CHECK(gd_ModeNotAvailable(&E, GD_NTSC_MONITOR_ID | 0x8800) == (chip == 2 ? 0 : GD_DI_AVAIL_NOCHIPS), "HIRESHAM needs AGA");
    CHECK(gd_ModeNotAvailable(&E, 0x12345678) == GD_DI_AVAIL_NOMONITOR, "an unknown monitor");
    CHECK(gd_GetDisplayInfoData(&E, 0, &di, 20, GD_DTAG_DISP, GD_HIRES_KEY) == 20, "a short buffer");
    CHECK(gd_GetDisplayInfoData(&E, 0, &di, 56, GD_DTAG_DISP, 0x8001) == 0, "an unknown ID");
    gd_dimensioninfo dims;
    CHECK(gd_GetDisplayInfoData(&E, 0, &dims, sizeof dims, GD_DTAG_DIMS, GD_PAL_MONITOR_ID | GD_LORES_KEY) == 88, "DTAG_DIMS");
    CHECK(dims.nominal.maxx == 319 && dims.nominal.maxy == 255 && dims.maxdepth == (chip == 2 ? 8 : 5), "PAL lores nominal");
    gd_nameinfo ni;
    CHECK(gd_GetDisplayInfoData(&E, 0, &ni, sizeof ni, GD_DTAG_NAME, GD_PAL_MONITOR_ID | 0x8004) == 56
          && same_str((char *)ni.name, "PAL:High Res Laced"), "NameInfo");
    gd_monitorinfo mi;
    CHECK(gd_GetDisplayInfoData(&E, 0, &mi, sizeof mi, GD_DTAG_MNTR, GD_NTSC_MONITOR_ID) == 96
          && mi.totalrows == 262 && mi.minrow == 21 && mi.mspc == gd_OpenMonitor(&E, "ntsc.monitor", 0), "MonitorInfo");
    gd_CloseMonitor(&E, mi.mspc);
    {
        gfx_tagitem t[] = { T(GD_BIDTAG_Depth, 8), T(0, 0) };
        uint32_t b = gd_BestModeIDA(&E, t);
        CHECK(chip == 2 ? (b & ~GD_MONITOR_ID_MASK) == GD_HIRES_KEY : b == GD_INVALID_ID, "BestModeIDA depth 8: AGA's hires (2x fetch)");
        gfx_tagitem t2[] = { T(GD_BIDTAG_NominalWidth, 640), T(GD_BIDTAG_NominalHeight, pal ? 512 : 400), T(0, 0) };
        CHECK(gd_BestModeIDA(&E, t2) == 0x8004, "BestModeIDA 640 x 400/512: hires laced");
        gfx_tagitem t3[] = { T(GD_BIDTAG_DIPFMustHave, GD_DIPF_IS_HAM), T(GD_BIDTAG_NominalWidth, 320), T(GD_BIDTAG_NominalHeight, pal ? 256 : 200), T(0, 0) };
        CHECK(gd_BestModeIDA(&E, t3) == GD_HAM_KEY, "BestModeIDA HAM");
        gfx_tagitem t4[] = { T(GD_BIDTAG_MonitorID, GD_PAL_MONITOR_ID), T(GFX_TAG_MORE, (uint32_t)(uintptr_t)t2) };
        CHECK(gd_BestModeIDA(&E, t4) == (GD_PAL_MONITOR_ID | 0x8004), "BestModeIDA on a monitor, TAG_MORE");
    }
    /* monitors */
    gd_monitorspec *ms = gd_OpenMonitor(&E, "PAL.Monitor", 0);
    CHECK(ms && ms->total_rows == 312 && ms->opencount == 1, "OpenMonitor by name");
    CHECK(gd_OpenMonitor(&E, 0, 0) == GB->default_monitor, "OpenMonitor default");
    gd_CloseMonitor(&E, (gd_monitorspec *)GB->default_monitor);
    CHECK(gd_CloseMonitor(&E, ms) == 0 && ms->opencount == 0, "CloseMonitor");
    CHECK(gd_CloseMonitor(&E, ms) == 1, "CloseMonitor once too often");
    CHECK(gd_OpenMonitor(&E, "vga.monitor", 0) == 0, "no such monitor");
    /* extended nodes */
    CHECK(gd_GfxNew(&E, 9) == 0, "GfxNew of no type");
    gd_extnode *x = gd_GfxNew(&E, GD_VIEWPORT_EXTRA_TYPE);
    CHECK(x && x->subtype == GD_VIEWPORT_EXTRA_TYPE && x->node.type == GD_NT_GRAPHICS && x->subsystem == GD_SS_GRAPHICS, "GfxNew");
    int key1, key2;
    gd_GfxAssociate(&E, &key1, x);
    CHECK(gd_GfxLookUp(&E, &key1) == x && !gd_GfxLookUp(&E, &key2), "GfxAssociate");
    gd_GfxFree(&E, x);
    CHECK(!gd_GfxLookUp(&E, &key1), "GfxFree");
    /* VideoControl and GetVPModeID, CoerceMode */
    gfx_colormap cm;
    memset(&cm, 0, sizeof cm);
    cm.type = GFX_COLORMAP_TYPE_V39;
    cm.vpmodeid = GD_INVALID_ID;
    gd_viewport vp;
    gd_InitVPort(&E, &vp);
    vp.modes = GD_HIRES | GD_LACE;
    vp.colormap = &cm;
    cm.vp = &vp;
    CHECK(gd_GetVPModeID(&E, &vp) == 0x8004, "GetVPModeID from Modes");
    CHECK(gd_CoerceMode(&E, &vp, GD_PAL_MONITOR_ID, 0) == 0x29004, "CoerceMode to PAL");
    {
        gfx_tagitem t[] = { T(0x80000001u, 0), T(0x80000020u, GD_PAL_MONITOR_ID | GD_LORES_KEY), T(0x80000005u, 0), T(0, 0) };
        CHECK(gd_VideoControl(&E, &cm, t) == 0, "VideoControl sets");
        CHECK(gd_GetVPModeID(&E, &vp) == (GD_PAL_MONITOR_ID | GD_LORES_KEY), "VTAG_VPMODEID_SET");
        gfx_tagitem g[] = { T(0x80000015u, 0), T(0x80000017u, 0), T(0x8000001Fu, 0), T(0x80000018u, 0), T(0, 0) };
        CHECK(gd_VideoControl(&E, &cm, g) == 0, "VideoControl gets");
        CHECK(g[0].tag == 0x80000001u && g[1].tag == 0x80000005u && g[2].data == (GD_PAL_MONITOR_ID | GD_LORES_KEY)
              && g[3].tag == 0x80000006u, "the _GET tags answer");
        gfx_tagitem u[] = { T(0x80001234u, 0), T(0, 0) };
        CHECK(gd_VideoControl(&E, &cm, u) == 1, "an unknown tag");
        CHECK(gd_VideoControl(&E, 0, u) == 1, "no ColorMap");
    }
    /* sprites */
    static uint16_t sdata[2 * 20] __attribute__((aligned(8)));
    gd_simplesprite s0, s1;
    memset(&s0, 0, sizeof s0);
    memset(&s1, 0, sizeof s1);
    s0.height = 10;
    s0.posctldata = sdata;
    CHECK(gd_GetSprite(&E, &s0, -1) == 0 && s0.num == 0 && (GB->spritereserved & 1), "GetSprite any");
    CHECK(gd_GetSprite(&E, &s1, 0) == -1, "GetSprite of a taken one");
    CHECK(gd_GetSprite(&E, &s1, 3) == 3, "GetSprite 3");
    CHECK(gd_GetSprite(&E, &s1, 8) == -1, "GetSprite 8");
    gd_FreeSprite(&E, 3);
    CHECK(!(GB->spritereserved & 8), "FreeSprite");
    gd_ChangeSprite(&E, 0, &s0, sdata);
    gd_MoveSprite(&E, 0, &s0, 100, 250);
    {
        int hs = 0x81 - 1 + 100, vs = 0x2C + 250, ve = vs + 10;   /* HRM: SPRxPOS, SPRxCTL */
        uint16_t pos = (uint16_t)((vs & 255) << 8 | (hs >> 1 & 255));
        uint16_t ctl = (uint16_t)((ve & 255) << 8 | (vs >> 8 & 1) << 2 | (ve >> 8 & 1) << 1 | (hs & 1));
        if (chip == 2) ctl |= (uint16_t)((vs >> 9 & 1) << 6 | (ve >> 9 & 1) << 5);
        CHECK(sdata[0] == pos && sdata[1] == ctl, "MoveSprite's control words");
    }
    frame(true);
    CHECK(M.sprpt[0] == (uint32_t)(uintptr_t)sdata, "copinit loads the sprite's pointer");
    CHECK(M.sprpt[1] == (uint32_t)(uintptr_t)((gd_copinit *)GB->copinit)->sprstop, "a free sprite shows nothing");
    {
        /* AllocSpriteDataA: 2 planes, doubled both ways */
        gfx_bitmap bm;
        memset(&bm, 0, sizeof bm);
        static uint8_t p0[2 * 4], p1[2 * 4];
        for (int i = 0; i < 8; i++) { p0[i] = (uint8_t)(0x5A ^ i * 17); p1[i] = (uint8_t)(0xC3 + i * 29); }
        bm.bytesperrow = 2; bm.rows = 4; bm.depth = 2; bm.planes[0] = p0; bm.planes[1] = p1;
        gfx_tagitem t[] = { T(GD_SPRITEA_Width, 32), T(GD_SPRITEA_XReplication, 1), T(GD_SPRITEA_YReplication, 1), T(0, 0) };
        gd_extsprite *es = gd_AllocSpriteDataA(&E, &bm, t);
        CHECK(es && es->wordwidth == 2 && es->es.height == 8 && ((uintptr_t)es->es.posctldata & 7) == 0, "AllocSpriteDataA");
        bool same = true;
        for (int oy = 0; es && oy < 8; oy++)
            for (int ox = 0; ox < 32; ox++) {
                int sx = ox >> 1, sy = oy >> 1;
                int b0 = p0[sy * 2 + (sx >> 3)] >> (7 - (sx & 7)) & 1, b1 = p1[sy * 2 + (sx >> 3)] >> (7 - (sx & 7)) & 1;
                const uint16_t *l = es->es.posctldata + 4 * (oy + 1);
                if ((l[ox >> 4] >> (15 - (ox & 15)) & 1) != b0 || (l[2 + (ox >> 4)] >> (15 - (ox & 15)) & 1) != b1) same = false;
            }
        CHECK(same, "AllocSpriteDataA's pixels, replicated");
        gd_FreeSprite(&E, 0);
        gd_extsprite *es2 = gd_AllocSpriteDataA(&E, &bm, t + 3);
        gfx_tagitem g[] = { T(GD_GSTAG_ATTACHED, (uint32_t)(uintptr_t)es2), T(0, 0) };
        CHECK(gd_GetExtSpriteA(&E, es, g) == 0 && es2->es.num == 1 && (es2->flags & GD_SPRITE_ATTACHED), "GetExtSpriteA attached pair");
        gfx_tagitem g2[] = { T(GD_GSTAG_SPRITE_NUM, 3), T(GD_GSTAG_ATTACHED, (uint32_t)(uintptr_t)es2), T(0, 0) };
        CHECK(gd_GetExtSpriteA(&E, es2, g2) == -1, "an attached pair starts even");
        gd_extsprite *es3 = gd_AllocSpriteDataA(&E, &bm, t + 1);
        int32_t r = gd_ChangeExtSpriteA(&E, 0, es2, es, 0);
        CHECK(chip == 2 ? r == 1 : r == 0, "ChangeExtSpriteA to a 32-pixel sprite needs AGA");
        frame(true);
        if (chip == 2) CHECK(M.reg[GD_FMODE >> 1] == 0x0004 && M.sprpt[1] == (uint32_t)(uintptr_t)es->es.posctldata, "FMODE and the pointer");
        gd_FreeSprite(&E, 0);
        gd_FreeSprite(&E, 1);
        gd_FreeSpriteData(&E, es);
        gd_FreeSpriteData(&E, es2);
        gd_FreeSpriteData(&E, es3);
        gd_FreeSpriteData(&E, 0);
    }
    /* the beam */
    M.cpu_t = 0;
    int32_t b1 = gd_VBeamPos(&E), b2 = b1;
    for (int i = 0; i < 20; i++) b2 = gd_VBeamPos(&E);
    CHECK(b1 >= 0 && b2 > b1 && b2 < b1 + 20, "VBeamPos moves");
    {
        gd_view v;
        gd_InitView(&E, &v);
        gd_viewport wp;
        gd_InitVPort(&E, &wp);
        wp.dheight = 100;
        v.viewport = &wp;
        GB->actiview = &v;
        M.cpu_t = 0;
        gd_WaitBOVP(&E, &wp);
        int t = M.cpu_t % (frame_lines() * 227);
        CHECK(t / 227 >= 0x2C + 100 && t / 227 < 0x2C + 102, "WaitBOVP waits for the ViewPort's bottom");
        GB->actiview = 0;
    }
    uint32_t c0 = GB->vbcounter;
    gd_WaitTOF(&E);
    CHECK(GB->vbcounter == c0 + 1 && GB->tof_waitq.head->succ == 0, "WaitTOF: one vertical blank");
    /* user copper lists past their first CopList; MrgCop of the shown View growing */
    {
        gd_ucoplist *u = (gd_ucoplist *)m_alloc(0, sizeof *u, GFX_MEMF_CLEAR);
        gd_coplist *cl = gd_UCopperListInit(&E, u, 3);
        for (int i = 0; i < 7; i++) { gd_CWait(&E, u, i, 0); gd_CBump(&E, u); }
        CHECK(cl && cl->count == 3 && cl->next && cl->next->count == 3 && u->coplist->count == 1, "CBump goes on in a new CopList");
        gd_FreeCopList(&E, u->firstcoplist);
        m_free(0, u, sizeof *u);
        CHECK(gd_UCopperListInit(&E, 0, 4) == 0, "UCopperListInit with no UCopList");
    }
    {
        /* out of memory at every step of MakeVPort and MrgCop: nothing kept */
        static uint8_t plane[40 * 50 * 4];
        gfx_bitmap bm;
        memset(&bm, 0, sizeof bm);
        bm.bytesperrow = 40; bm.rows = 50; bm.depth = 4;
        for (int k = 0; k < 4; k++) bm.planes[k] = plane + k * 2000;
        gd_rasinfo r;
        memset(&r, 0, sizeof r);
        r.bitmap = &bm;
        gd_view v;
        gd_InitView(&E, &v);
        gd_viewport p1, p2;
        gd_InitVPort(&E, &p1);
        gd_InitVPort(&E, &p2);
        p1.rasinfo = p2.rasinfo = &r;
        p1.dwidth = p2.dwidth = 320;
        p1.dheight = p2.dheight = 40;
        p2.dyoffset = 80;
        p1.next = 0;
        v.viewport = &p1;
        int okcount = 0;
        long base = (long)nlive;
        for (int k = 0; k < 12; k++) {
            fail_after = k;
            uint32_t a = gd_MakeVPort(&E, &v, &p1);
            uint32_t m = a == GD_MVP_OK ? gd_MrgCop(&E, &v) : 99;
            fail_after = -1;
            CHECK(a == GD_MVP_OK || a == GD_MVP_NO_MEM, "MakeVPort out of memory");
            CHECK(m == GD_MCOP_OK || m == GD_MCOP_NO_MEM || m == 99, "MrgCop out of memory");
            if (m == GD_MCOP_OK) okcount++;
            gd_FreeVPortCopLists(&E, &p1);
            gd_FreeCprList(&E, v.lofcprlist);
            v.lofcprlist = v.shfcprlist = 0;
            CHECK(nlive == base, "nothing kept after running out");
        }
        CHECK(okcount > 0, "enough memory makes it");
        /* MrgCop of the shown View, growing: new lists, the old ones freed after a frame */
        CHECK(gd_MakeVPort(&E, &v, &p1) == 0 && gd_MrgCop(&E, &v) == 0, "a View");
        gd_LoadView(&E, &v);
        frame(true);
        p1.next = &p2;
        gd_cprlist *old = v.lofcprlist;
        CHECK(gd_MakeVPort(&E, &v, &p2) == 0 && gd_MrgCop(&E, &v) == 0 && v.lofcprlist != old, "MrgCop grows the shown View's list");
        CHECK(GB->loflist == v.lofcprlist->start, "the shown list follows");
        gd_LoadView(&E, 0);
        gd_FreeVPortCopLists(&E, &p1);
        gd_FreeVPortCopLists(&E, &p2);
        gd_FreeCprList(&E, v.lofcprlist);
        gd_FreeCopList(&E, 0);
        gd_FreeCprList(&E, 0);
        gd_FreeVPortCopLists(&E, 0);
        CHECK(gd_MrgCop(&E, 0) == GD_MCOP_NOP, "MrgCop of no View");
        /* hidden and impossible ViewPorts */
        p1.modes = GD_VP_HIDE;
        CHECK(gd_MakeVPort(&E, &v, &p1) == GD_MVP_OK && !p1.dspins, "VP_HIDE");
        p1.modes = GD_HIRES;
        bm.depth = 5;
        CHECK(gd_MakeVPort(&E, &v, &p1) == (chip == 2 ? GD_MVP_OK : GD_MVP_NO_DISPLAY), "hires with 5 planes: AGA's 2x fetch");
        p1.modes = 0;
        p1.dyoffset = 400;
        CHECK(gd_MakeVPort(&E, &v, &p1) == GD_MVP_OFF_BOTTOM, "a ViewPort below the display");
        p1.rasinfo = 0;
        CHECK(gd_MakeVPort(&E, &v, &p1) == GD_MVP_NO_DISPLAY, "no RasInfo");
        gd_FreeVPortCopLists(&E, &p1);
    }
    shutdown();
    printf("misc chip %d pal %d done\n", chip, pal ? 1 : 0);
}

/* ---- the fetch: DDFSTRT, DDFSTOP, the modulos, BPLCON1 and FMODE --------- */
struct regs { int n; uint16_t ddfstrt, ddfstop, mod1, mod2, con1, fmode; };
static regs dsp_regs(gd_viewport *vp)
{
    regs r;
    memset(&r, 0, sizeof r);
    r.fmode = 0xEEEE;                   /* none */
    gd_coplist *cl = vp->dspins;
    for (int i = 1; cl && i < cl->count; i++) {
        const gd_copins &ci = cl->copins[i];
        if ((ci.opcode & 3) == GD_COPPER_WAIT) break;
        uint16_t v = (uint16_t)ci.u.w.b;
        switch (ci.u.w.a & 0x1FE) {
        case GD_DDFSTRT: r.ddfstrt = v; break;
        case GD_DDFSTOP: r.ddfstop = v; break;
        case GD_BPL1MOD: r.mod1 = v; break;
        case GD_BPL2MOD: r.mod2 = v; break;
        case GD_BPLCON1: r.con1 = v; break;
        case GD_FMODE: r.fmode = v; break;
        }
        r.n++;
    }
    return r;
}

/* a bitmap of rowbytes, planes at 64 bits plus off bytes */
static gfx_bitmap *plain_bm(int depth, int rowbytes, int rows, int off)
{
    gfx_bitmap *bm = (gfx_bitmap *)talloc(sizeof(gfx_bitmap));
    bm->depth = (uint8_t)depth;
    bm->rows = (uint16_t)rows;
    bm->bytesperrow = (uint16_t)rowbytes;
    for (int k = 0; k < depth; k++) bm->planes[k] = (uint8_t *)talloc((uint32_t)(rowbytes * rows + 8)) + off;
    return bm;
}

static void fetch_modes(int chip)
{
    tused = 0;
    boot(chip, true);
    static const char *const rn[3] = { "lores", "hires", "superhires" };
    const int maxd[3] = { chip == 2 ? 8 : 6, chip == 2 ? 8 : 4, chip == 2 ? 8 : 2 };
    /* the database's MaxDepth per chipset */
    static const uint32_t keys[] = { 0x0000, 0x8000, 0x8020, 0x8004, 0x8024, 0x0080, 0x0800, 0x0400, 0x8400, 0x8080, 0x8800 };
    const int ocs[] = { 5, 4, 2, 4, 2, 6, 6, 6, 4, 4, 4 }, agad[] = { 8, 8, 8, 8, 8, 6, 8, 8, 8, 6, 8 };
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        gd_dimensioninfo dims;
        CHECK(gd_GetDisplayInfoData(&E, 0, &dims, sizeof dims, GD_DTAG_DIMS, GD_PAL_MONITOR_ID | keys[i]) == 88, "DTAG_DIMS");
        int want = chip == 2 ? agad[i] : ocs[i];
        CHECK(dims.maxdepth == want, "MaxDepth per chipset");
        if (dims.maxdepth != want) printf("  chip %d mode %04lx: MaxDepth %d, want %d\n", chip, (unsigned long)keys[i], dims.maxdepth, want);
    }
    /* full width ViewPorts of every resolution and depth, plain and laced:
     * what MakeVPort answers, and the fetch it sets */
    gd_view v;
    gd_InitView(&E, &v);
    for (int res = 0; res < 3; res++)
        for (int lace = 0; lace < 2; lace++)
            for (int depth = 1; depth <= 8; depth++) {
                if (res == 2 && chip == 0) continue;
                int w = 320 << res, rb = w / 8;
                gfx_bitmap *bm = plain_bm(depth, rb, 32, 0);
                gd_rasinfo ri;
                memset(&ri, 0, sizeof ri);
                ri.bitmap = bm;
                gd_viewport vp;
                gd_InitVPort(&E, &vp);
                vp.rasinfo = &ri;
                vp.dwidth = (int16_t)w;
                vp.dheight = 16;
                vp.modes = (uint16_t)((res == 1 ? GD_HIRES : res == 2 ? GD_HIRES | GD_SUPERHIRES : 0) | (lace ? GD_LACE : 0)
                                      | (depth == 6 && res == 0 && chip < 2 ? GD_EXTRA_HALFBRITE : 0));
                uint32_t r = gd_MakeVPort(&E, &v, &vp);
                bool ok = depth <= maxd[res];
                CHECK(r == (ok ? GD_MVP_OK : GD_MVP_NO_DISPLAY), "MakeVPort: the depths each chipset shows");
                if (r != (ok ? GD_MVP_OK : GD_MVP_NO_DISPLAY))
                    printf("  chip %d %s%s depth %d: MakeVPort %lu\n", chip, rn[res], lace ? " laced" : "", depth, (unsigned long)r);
                if (r == GD_MVP_OK) {
                    /* at 1x: lores 0x38-0xD0, hires and superhires 0x3C-0xD4
                     * (HRM); hires at 2x and superhires at 4x: 0x38-0xD0
                     * (their data shows from 2 * DDFSTRT + 17); the modulo the
                     * rows the fetch skips: 0, or a row when laced */
                    int wide = (res == 1 && depth > 4) || (res == 2 && depth > 2);
                    regs g = dsp_regs(&vp);
                    uint16_t ws = (res && !wide) ? 0x3C : 0x38, we = (uint16_t)(ws + 0x98);
                    uint16_t wm = (uint16_t)(lace ? rb : 0), wf = (uint16_t)(chip < 2 ? 0xEEEE : !wide ? 0 : res == 1 ? 1 : 3);
                    bool same = g.ddfstrt == ws && g.ddfstop == we && g.mod1 == wm && g.mod2 == wm && g.con1 == 0 && g.fmode == wf;
                    CHECK(same, "the fetch per resolution and depth");
                    if (!same)
                        printf("  chip %d %s%s depth %d: DDF %02x-%02x mod %04x/%04x BPLCON1 %04x FMODE %04x; want %02x-%02x mod %04x FMODE %04x\n",
                               chip, rn[res], lace ? " laced" : "", depth, g.ddfstrt, g.ddfstop, g.mod1, g.mod2, g.con1, g.fmode, ws, we, wm, wf);
                }
                gd_FreeVPortCopLists(&E, &vp);
            }
    if (chip == 2) {
        gd_rasinfo ri;
        memset(&ri, 0, sizeof ri);
        gd_viewport vp;
        gd_InitVPort(&E, &vp);
        vp.rasinfo = &ri;
        /* SysInfo 4.4's screen: 640 superhires pixels, 3 planes: 4x, 40 words */
        ri.bitmap = plain_bm(3, 80, 32, 0);
        vp.dwidth = 640;
        vp.dheight = 16;
        vp.modes = GD_HIRES | GD_SUPERHIRES;
        CHECK(gd_MakeVPort(&E, &v, &vp) == GD_MVP_OK, "SysInfo's screen");
        regs g = dsp_regs(&vp);
        CHECK(g.ddfstrt == 0x38 && g.ddfstop == 0x80 && g.mod1 == 0 && g.mod2 == 0 && g.fmode == 3, "SysInfo's screen: 4x, 0x38-0x80");
        /* hires, 8 planes, scrolled 5 pixels: the fetch a cycle earlier,
         * 54 superhires pixels of delay (PF1H 13 and PF1H0-1 2), 42 words */
        ri.bitmap = plain_bm(8, 80, 32, 0);
        ri.rxoffset = 5;
        vp.modes = GD_HIRES;
        CHECK(gd_MakeVPort(&E, &v, &vp) == GD_MVP_OK, "hires 8 planes scrolled");
        g = dsp_regs(&vp);
        CHECK(g.ddfstrt == 0x30 && g.ddfstop == 0xD0 && g.mod1 == 0xFFFC && g.con1 == 0x22DD && g.fmode == 1, "hires 8 planes scrolled: 0x30-0xD0, BPLCON1 0x22DD");
        ri.rxoffset = 0;
        /* a wider fetch needs its planes and rows on its width */
        ri.bitmap = plain_bm(8, 82, 32, 0);
        CHECK(gd_MakeVPort(&E, &v, &vp) == GD_MVP_NO_DISPLAY, "hires 8 planes, rows of 82 bytes: no 2x");
        ri.bitmap = plain_bm(8, 84, 32, 0);
        CHECK(gd_MakeVPort(&E, &v, &vp) == GD_MVP_OK && dsp_regs(&vp).mod1 == 4, "hires 8 planes, rows of 84 bytes: 2x");
        ri.bitmap = plain_bm(8, 80, 32, 2);
        CHECK(gd_MakeVPort(&E, &v, &vp) == GD_MVP_NO_DISPLAY, "hires 8 planes off 32 bits: no 2x");
        ri.bitmap = plain_bm(3, 84, 32, 0);
        vp.modes = GD_HIRES | GD_SUPERHIRES;
        CHECK(gd_MakeVPort(&E, &v, &vp) == GD_MVP_NO_DISPLAY, "superhires 3 planes, rows of 84 bytes: no 4x");
        ri.bitmap = plain_bm(3, 88, 32, 4);
        CHECK(gd_MakeVPort(&E, &v, &vp) == GD_MVP_NO_DISPLAY, "superhires 3 planes off 64 bits: no 4x");
        ri.bitmap = plain_bm(2, 82, 32, 2);
        CHECK(gd_MakeVPort(&E, &v, &vp) == GD_MVP_OK, "superhires 2 planes at 1x: any word");
        /* the sprite width joins each shown ViewPort's FMODE */
        ri.bitmap = plain_bm(8, 80, 32, 0);
        vp.modes = GD_HIRES;
        v.viewport = &vp;
        CHECK(gd_MakeVPort(&E, &v, &vp) == GD_MVP_OK && gd_MrgCop(&E, &v) == GD_MCOP_OK, "a hires 8 plane View");
        gd_LoadView(&E, &v);
        frame(true);
        CHECK(M.reg[GD_FMODE >> 1] == 0x0001, "the View's FMODE: 2x, 16 pixel sprites");
        gfx_bitmap sb;
        memset(&sb, 0, sizeof sb);
        static uint8_t sp0[8], sp1[8];
        sb.bytesperrow = 4; sb.rows = 2; sb.depth = 2; sb.planes[0] = sp0; sb.planes[1] = sp1;
        gfx_tagitem t16[] = { T(0, 0) }, t32[] = { T(GD_SPRITEA_Width, 32), T(0, 0) }, g0[] = { T(GD_GSTAG_SPRITE_NUM, 0), T(0, 0) };
        gd_extsprite *e1 = gd_AllocSpriteDataA(&E, &sb, t16), *e2 = gd_AllocSpriteDataA(&E, &sb, t32);
        CHECK(e1 && e2 && gd_GetExtSpriteA(&E, e1, g0) == 0, "GetExtSpriteA 0");
        CHECK(gd_ChangeExtSpriteA(&E, &vp, e1, e2, 0) == 1, "a 32 pixel sprite");
        frame(true);
        CHECK(M.reg[GD_FMODE >> 1] == 0x0005, "the View's FMODE: 2x, 32 pixel sprites");
        gd_LoadView(&E, 0);
        gd_FreeSprite(&E, 0);
        gd_FreeSpriteData(&E, e1);
        gd_FreeSpriteData(&E, e2);
        gd_FreeVPortCopLists(&E, &vp);
        gd_FreeCprList(&E, v.lofcprlist);
        v.lofcprlist = v.shfcprlist = 0;
    }
    shutdown();
    printf("fetch chip %d done\n", chip);
}

/* opengfx.library next to OS 3.2's graphics.library: the same calls, no
 * custom chip register written */
static void side_by_side()
{
    tused = 0;
    nwrites = 0;
    boot(2, true, 0);
    gd_view v;
    gd_InitView(&E, &v);
    gd_MrgCop(&E, &v);
    gd_LoadView(&E, &v);
    gd_vblank(&E);
    CHECK(GB->actiview == &v && nwrites == 0, "side by side: LoadView and the vblank leave the chips alone");
    gd_LoadView(&E, nullptr);
    if (v.lofcprlist) gd_FreeCprList(&E, v.lofcprlist);
    if (v.shfcprlist) gd_FreeCprList(&E, v.shfcprlist);
    shutdown();
}

int main(void)
{
    for (int i = 0; i < NSCENES; i++) run_scene(i);
    for (int c = 0; c < 3; c++)
        for (int p = 0; p < 2; p++) misc(c, p != 0);
    for (int c = 0; c < 3; c++) fetch_modes(c);
    side_by_side();
    printf("%lu checks, %lu failed\n", checks, fails);
    return fails != 0;
}
