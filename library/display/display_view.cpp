/* SPDX-License-Identifier: MIT */
/* OpenGfx display: Views, ViewPorts and the copper. Copyright (c) 2026
 * Dalsin Limited. MIT.
 *
 * Clean-room: from graphics.library's autodocs (InitView, InitVPort,
 * MakeVPort, MrgCop, LoadView, FreeVPortCopLists, FreeCopList,
 * FreeCprList, ScrollVPort, UCopperListInit, CWait, CMove, CBump,
 * ChangeVPBitMap, AllocDBufInfo, FreeDBufInfo, CalcIVG), the NDK 3.2
 * layouts and the Hardware Reference Manual's display rules; never from
 * Kickstart's code. Lean C++: no Amiga headers, no globals.
 *
 * How a ViewPort becomes copper (ours; the lists' contents are private,
 * only what they show is graphics.library's contract):
 *   MakeVPort writes vp->DspIns as three blocks, each a WAIT and MOVEs:
 *     1. the registers and colours, some lines before the ViewPort (the
 *        intermediate vertical gap, CalcIVG), with bitplanes still off:
 *        BPLCON0..4, DIWSTRT/STOP, DDFSTRT/STOP, the modulos, the plane
 *        pointers (a long and a short frame copy for an interlaced
 *        ViewPort), the palette (bank by bank on AGA, high then low);
 *     2. at the ViewPort's first line, BPLCON0 with its planes;
 *     3. at the line after its last, BPLCON0 with none.
 *   Blocks 2 and 3 switch the bitplane DMA, so a ViewPort may start below
 *   line 255 on OCS (where DIWSTRT cannot) and stop above line 128 (where
 *   DIWSTOP cannot). DspIns' WAITs are frame lines; UCopIns', SprIns' and
 *   ClrIns' are the ViewPort's own lines (graphics.library's convention).
 *   MrgCop sorts every block of every ViewPort by its WAIT, adds the WAIT
 *   for line 255 that a list going past line 255 needs, and writes the
 *   long frame list (and the short frame list for an interlaced View).
 *   The copper starts every frame at copinit (GfxBase->copinit, ours),
 *   which loads the sprite pointers and jumps to COP2LC: the View's list
 *   for this field, set by LoadView and by the vertical blank. */
#include "display_int.h"

namespace gd {

void zero(void *p, uint32_t n)
{
    volatile uint8_t *b = static_cast<uint8_t *>(p);
    while (n--) *b++ = 0;
}

void list_init(gfx_list *l)
{
    l->head = reinterpret_cast<gfx_node *>(&l->tail);
    l->tail = nullptr;
    l->tailpred = reinterpret_cast<gfx_node *>(&l->head);
}

void add_tail(gfx_list *l, gfx_node *n)
{
    n->succ = reinterpret_cast<gfx_node *>(&l->tail);
    n->pred = l->tailpred;
    l->tailpred->succ = n;
    l->tailpred = n;
}

void remove(gfx_node *n)
{
    n->pred->succ = n->succ;
    n->succ->pred = n->pred;
}

gfx_tagitem *next_tag(const gfx_tagitem **state)
{
    const gfx_tagitem *t = *state;
    if (!t) return nullptr;
    for (;;) {
        switch (t->tag) {
        case GFX_TAG_DONE: *state = nullptr; return nullptr;
        case GFX_TAG_IGNORE: t++; continue;
        case GFX_TAG_MORE:
            t = reinterpret_cast<const gfx_tagitem *>(static_cast<uintptr_t>(t->data));
            if (!t) { *state = nullptr; return nullptr; }
            continue;
        case GFX_TAG_SKIP: t += t->data + 1; continue;
        default: *state = t + 1; return const_cast<gfx_tagitem *>(t);
        }
    }
}

uint32_t tag_data(const gfx_tagitem *tags, uint32_t tag, uint32_t def, bool *found)
{
    const gfx_tagitem *s = tags;
    if (found) *found = false;
    while (gfx_tagitem *t = next_tag(&s))
        if (t->tag == tag) {
            if (found) *found = true;
            return t->data;
        }
    return def;
}

gd_monitorspec *monitor_of_view(gfx_env *env, gd_view *view)
{
    if (view && (view->modes & GD_EXTEND_VSTRUCT)) {
        gd_viewextra *ve = reinterpret_cast<gd_viewextra *>(gd_GfxLookUp(env, view));
        if (ve && ve->n.subtype == GD_VIEW_EXTRA_TYPE && ve->monitor) return ve->monitor;
    }
    return static_cast<gd_monitorspec *>(base(env)->default_monitor);
}

geom view_geom(gfx_env *env, gd_view *view)
{
    geom g;
    gd_monitorspec *ms = monitor_of_view(env, view);
    g.pal = ms ? ms->total_rows > 280 : (base(env)->displayflags & GFX_PAL) != 0;
    g.total_rows = ms ? ms->total_rows : (g.pal ? 312 : 262);
    g.min_row = ms ? ms->min_row : (g.pal ? 29 : 21);
    return g;
}

} // namespace gd

using namespace gd;

namespace {

int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
int floormod(int a, int b) { return a - floordiv(a, b) * b; }
int imin(int a, int b) { return a < b ? a : b; }
int imax(int a, int b) { return a > b ? a : b; }

/* ---- a ViewPort's layout on the display -------------------------------- */

struct playfield {
    const gfx_bitmap *bm;
    int ry;
    int delay;                          /* BPLCON1, in 35ns (superhires) pixels */
    int w0;                             /* first word fetched in each row */
    int depth;
    int mod;
};

struct layout {
    bool lace;                          /* the ViewPort: fields show alternate rows */
    bool visible[2];                    /* per field: long, short */
    int first[2], last[2];              /* frame lines [first, last) per field */
    int row[2];                         /* the bitmap row (before RyOffset) at first */
    int step;                           /* rows per line */
    int ddfstrt, ddfstop, words;
    uint16_t diwstrt, diwstop;
    playfield pf[2];
    int npf;
    int depth;
    uint16_t con0off, con0, con1, con2, con3, con4;
    int ncolors;
};

uint32_t vp_layout(gfx_env *env, gd_view *view, gd_viewport *vp, layout &L)
{
    gfx_base *gb = base(env);
    geom g = view_geom(env, view);
    gfx_colormap *cm = vp->colormap;
    gd_rasinfo *ri = vp->rasinfo;
    zero(&L, sizeof L);
    if (!ri || !ri->bitmap) return GD_MVP_NO_DISPLAY;

    const uint16_t modes = vp->modes;
    const bool shres = (modes & GD_SUPERHIRES) != 0;
    const bool hires = !shres && (modes & GD_HIRES);
    const int p = shres ? 1 : hires ? 2 : 4;        /* 35ns pixels per ViewPort pixel */
    const int res = 4 / p;
    const bool lace = (modes & GD_LACE) != 0;
    const bool dpf = (modes & GD_DUALPF) && ri->next && ri->next->bitmap
                     && !(cm && (cm->auxflags & GD_CMAF_DUALPF_DISABLE));
    const bool isaga = aga(env);
    L.lace = lace;
    L.step = lace ? 2 : 1;

    /* planes */
    if (dpf) {
        int d2 = imin(ri->next->bitmap->depth, ri->bitmap->depth);
        int d1 = imin(ri->bitmap->depth, d2 + 1);
        L.pf[0] = { ri->bitmap, ri->ryoffset, 0, 0, d1, 0 };
        L.pf[1] = { ri->next->bitmap, ri->next->ryoffset, 0, 0, d2, 0 };
        L.npf = 2;
        L.depth = d1 + d2;
    } else {
        L.pf[0] = { ri->bitmap, ri->ryoffset, 0, 0, ri->bitmap->depth, 0 };
        L.npf = 1;
        L.depth = ri->bitmap->depth;
    }
    /* The fetch mode stays 1x: lores to 6 planes (8 on AGA), hires to 4,
     * superhires to 2. Deeper needs FMODE's wider fetches: not yet. */
    const int maxdepth = shres ? 2 : hires ? 4 : isaga ? 8 : 6;
    if (L.depth > maxdepth) return GD_MVP_NO_DISPLAY;

    /* BPLCON0..4 */
    const bool vlace = view && (view->modes & GD_LACE);
    uint16_t sys = static_cast<uint16_t>(gb->system_bplcon0) & 0x0102;   /* ERSY, GAUD: genlock */
    uint16_t c0 = 0x0200 | sys | (vlace ? 0x0004 : 0) | (ecs_denise(env) ? 0x0001 : 0);
    if (hires) c0 |= 0x8000;
    if (shres) c0 |= 0x0040;
    if ((modes & GD_HAM) && !dpf) c0 |= 0x0800;
    if (dpf) c0 |= 0x0400;
    if (modes & GD_GENLOCK_VIDEO) c0 |= 0x0002;
    L.con0off = c0;
    L.con0 = c0 | static_cast<uint16_t>((L.depth & 7) << 12) | (L.depth == 8 ? 0x0010 : 0);
    uint16_t c2 = vp->spritepriorities ? vp->spritepriorities : 0x24;   /* ABI test: 0 */
    if (dpf && (modes & GD_PFBA)) c2 |= 0x0040;
    if (cm && (cm->flags & GD_COLORMAP_TRANSPARENCY)) c2 |= 0x0400;     /* ZDCTEN */
    if (cm && (cm->flags & GD_COLORPLANE_TRANSPARENCY))
        c2 |= 0x0800 | static_cast<uint16_t>((cm->transparencyplane & 7) << 12);   /* ZDBPEN, ZDBPSEL */
    L.con2 = c2;
    uint16_t c3 = 0;
    if (cm) {
        int b1 = cm->bp_1_base, code = 0;
        while (code < 7 && (1 << code) < b1) code++;
        if (b1 && isaga) c3 |= static_cast<uint16_t>(code << 10);      /* PF2OF */
        if (cm->type >= GFX_COLORMAP_TYPE_V36) {
            if (cm->spriteresolution != 0xFF) c3 |= static_cast<uint16_t>((cm->spriteresolution & 3) << 6);
            if (cm->flags & GD_BORDER_BLANKING) c3 |= 0x0020;
            if (cm->flags & GD_BORDER_NOTRANSPARENCY) c3 |= 0x0010;
            if (cm->flags & GD_BORDERSPRITES) c3 |= 0x0002;
        }
    } else if (isaga)
        c3 |= 3 << 10;
    L.con3 = c3;
    L.con4 = cm ? static_cast<uint16_t>((cm->bp_0_base & 0xFF) << 8 | ((cm->spritebase_even >> 4) & 15) << 4
                                        | ((cm->spritebase_odd >> 4) & 15))
                : 0x0011;

    /* colours */
    if (cm && !(cm->auxflags & GD_CMAF_NO_COLOR_LOAD)) {
        int n = cm->count;
        if (isaga) {
            int want = 1 << L.depth;
            if (dpf) want = imax(cm->bp_1_base + (1 << L.pf[1].depth), 1 << L.pf[0].depth);
            else if (!(modes & GD_HAM)) want = (((1 << L.depth) - 1) | (cm->bp_0_base & 0xFF)) + 1;  /* BPLAM's XOR */
            if (want < 32) want = 32;
            if (!(cm->auxflags & GD_CMAF_FULLPALETTE) && n > want) n = want;
            if (n > 256) n = 256;
        } else if (n > 32)
            n = 32;
        L.ncolors = n;
    }

    /* vertical: the ViewPort's rows, its DisplayClip, the monitor */
    const int h = vp->dheight, w = vp->dwidth;
    const int top = (view ? view->dyoffset : VIEW_Y) + (lace ? (vp->dyoffset >> 1) : vp->dyoffset);
    if (top >= g.total_rows) return GD_MVP_OFF_BOTTOM;
    int r0 = 0, r1 = h, x0 = 0, x1 = w;
    gd_vpextra *vpe = cm ? static_cast<gd_vpextra *>(cm->vpe) : nullptr;
    if (vpe && cm->type >= GFX_COLORMAP_TYPE_V36) {
        r0 = imax(r0, vpe->displayclip.miny - vp->dyoffset);
        r1 = imin(r1, vpe->displayclip.maxy + 1 - vp->dyoffset);
        x0 = imax(x0, vpe->displayclip.minx - vp->dxoffset);
        x1 = imin(x1, vpe->displayclip.maxx + 1 - vp->dxoffset);
    }
    for (int f = 0; f < 2; f++) {
        int rlo = r0, rstep = 1;
        if (lace) {
            rstep = 2;
            if ((rlo & 1) != f) rlo++;
        }
        if (rlo >= r1) continue;
        int n = (r1 - 1 - rlo) / rstep + 1;
        int lfirst = top + (lace ? rlo >> 1 : rlo), llast = lfirst + n;
        if (lfirst < g.min_row) {
            rlo += (g.min_row - lfirst) * rstep;
            lfirst = g.min_row;
        }
        if (llast > g.total_rows) llast = g.total_rows;
        if (lfirst >= llast) continue;
        L.visible[f] = true;
        L.first[f] = lfirst;
        L.last[f] = llast;
        L.row[f] = rlo;
    }

    /* horizontal, in 35ns pixels: where the ViewPort's column 0 is, the
     * window, the fetch, the scroll */
    const int vx0 = 4 * ((view ? view->dxoffset : VIEW_X) + floordiv(vp->dxoffset, res));
    int xlo = imax(vx0 + x0 * p, 4 * LEGAL_X0), xhi = imin(vx0 + x1 * p, 4 * LEGAL_X1);
    if (xlo >= xhi || w <= 0 || h <= 0) L.visible[0] = L.visible[1] = false;
    if (!L.visible[0] && !L.visible[1]) return GD_MVP_OK;
    const int hstart = xlo / 4, hstop = (xhi + 3) / 4;
    const int c = hires || shres ? 9 : 17;          /* data starts at 2 * DDFSTRT + c (HRM) */
    const int gran = shres ? 2 : hires ? 4 : 8;     /* DDFSTRT's step: one word of each plane */
    const int blk = 16 * p;                         /* one word of the ViewPort's pixels, in 35ns */
    /* each playfield's stream: where its words start on the line, the
     * last such place at or left of the window; the fetch starts at the
     * last DDFSTRT at or left of them all, the rest is BPLCON1's delay */
    int S[2];
    int smin = 1 << 30;
    for (int i = 0; i < L.npf; i++) {
        const gd_rasinfo *r = i ? ri->next : ri;
        int rx = r->rxoffset;
        if (!isaga) rx &= ~(res - 1);               /* OCS/ECS scroll in lores pixels */
        int col0 = vx0 - p * rx;                    /* where the bitmap's column 0 would be */
        S[i] = 4 * hstart - floormod(4 * hstart - col0, blk);
        L.pf[i].w0 = (S[i] - col0) / blk;
        smin = imin(smin, S[i]);
    }
    L.ddfstrt = floordiv(smin - 4 * c, 8 * gran) * gran;
    const int base0 = 4 * (2 * L.ddfstrt + c);
    for (int i = 0; i < L.npf; i++) {
        int d = S[i] - base0;
        while (d >= blk) {                          /* the other playfield started the fetch earlier */
            d -= blk;
            L.pf[i].w0--;
        }
        L.pf[i].delay = d;
    }
    int words = floordiv(4 * hstop - 1 - base0, blk) + 1;
    const int per8 = shres ? 4 : hires ? 2 : 1;     /* words per plane in an 8 cycle fetch */
    words = (words + per8 - 1) / per8 * per8;
    while (words > per8 && L.ddfstrt + 8 * (words / per8 - 1) > 0xD8) words -= per8;
    L.words = words;
    L.ddfstop = L.ddfstrt + 8 * (words / per8 - 1);
    for (int i = 0; i < L.npf; i++) L.pf[i].mod = L.pf[i].bm->bytesperrow * L.step - 2 * words;
    auto enc = [isaga](int d, int sh) -> uint16_t {
        if (!isaga) return static_cast<uint16_t>(((d >> 2) & 15) << sh);
        return static_cast<uint16_t>((((d >> 2) & 15) << sh) | ((d & 3) << (sh + 8)) | (((d >> 6) & 3) << (sh + 10)));
    };
    L.con1 = enc(L.pf[0].delay, 0) | enc(L.npf == 2 ? L.pf[1].delay : L.pf[0].delay, 4);

    int vstart = 1 << 30, vstop = 0;
    for (int f = 0; f < 2; f++)
        if (L.visible[f]) {
            vstart = imin(vstart, L.first[f]);
            vstop = imax(vstop, L.last[f]);
        }
    vstart = imin(vstart, 255);
    vstop = imax(vstop, 128);
    L.diwstrt = static_cast<uint16_t>((vstart & 0xFF) << 8 | (imin(hstart, 0xFF) & 0xFF));
    L.diwstop = static_cast<uint16_t>((vstop & 0xFF) << 8 | (imax(hstop, 0x100) & 0xFF));
    return GD_MVP_OK;
}

/* ---- writing CopIns ------------------------------------------------------ */

struct emitter {
    gd_copins *buf;
    int n;
    void put(int16_t op, int a, int b)
    {
        if (buf) {
            buf[n].opcode = op;
            buf[n].u.w.a = static_cast<int16_t>(a);
            buf[n].u.w.b = static_cast<int16_t>(b);
        }
        n++;
    }
    void wait(int v, int h, int16_t fl = 0) { put(static_cast<int16_t>(GD_COPPER_WAIT | fl), v, h); }
    void move(int reg, unsigned val, int16_t fl = 0) { put(static_cast<int16_t>(GD_COPPER_MOVE | fl), reg, static_cast<int>(val & 0xFFFF)); }
};

const int16_t NT_LOF = GD_CPR_NT_LOF, NT_SHT = GD_CPR_NT_SHT;

/* the instructions of the first block in one field */
int ivg_of(int ninstr) { return (ninstr * 4 + 226) / 227 + 1; }

void emit_vp(gfx_env *env, gd_viewport *vp, const layout &L, emitter &e, int gapline)
{
    gfx_colormap *cm = vp->colormap;
    const bool isaga = aga(env), ecs = ecs_denise(env);
    e.wait(gapline, 0);
    e.move(GD_BPLCON0, L.con0off);
    e.move(GD_BPLCON1, L.con1);
    e.move(GD_BPLCON2, L.con2);
    if (ecs) e.move(GD_BPLCON3, L.con3);
    if (isaga) e.move(GD_BPLCON4, L.con4);
    e.move(GD_DIWSTRT, L.diwstrt);
    e.move(GD_DIWSTOP, L.diwstop);
    e.move(GD_DDFSTRT, static_cast<unsigned>(L.ddfstrt));
    e.move(GD_DDFSTOP, static_cast<unsigned>(L.ddfstop));
    e.move(GD_BPL1MOD, static_cast<unsigned>(L.pf[0].mod));
    e.move(GD_BPL2MOD, static_cast<unsigned>(L.npf == 2 ? L.pf[1].mod : L.pf[0].mod));
    for (int f = 0; f < (L.lace ? 2 : 1); f++) {
        int16_t fl = L.lace ? (f ? NT_LOF : NT_SHT) : 0;
        int row = L.visible[f] ? L.row[f] : L.row[f ^ 1];
        for (int k = 0; k < L.depth; k++) {
            const playfield &pf = L.npf == 2 ? L.pf[k & 1] : L.pf[0];
            int plane = L.npf == 2 ? k >> 1 : k;
            uint32_t a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pf.bm->planes[plane]))
                         + static_cast<uint32_t>((pf.ry + row) * pf.bm->bytesperrow + 2 * pf.w0);
            e.move(GD_BPL1PTH + 4 * k, a >> 16, fl);
            e.move(GD_BPL1PTH + 4 * k + 2, a, fl);
        }
    }
    if (L.ncolors) {
        const uint16_t *hi = cm->colortable;
        const uint16_t *lo = cm->type >= GFX_COLORMAP_TYPE_V39 ? cm->lowcolorbits : nullptr;
        if (isaga) {
            for (int b = 0; b * 32 < L.ncolors; b++) {
                int m = imin(32, L.ncolors - b * 32);
                e.move(GD_BPLCON3, L.con3 | static_cast<unsigned>(b << 13));
                for (int i = 0; i < m; i++) e.move(GD_COLOR00 + 2 * i, hi ? hi[b * 32 + i] : 0);
                if (lo) {
                    e.move(GD_BPLCON3, L.con3 | static_cast<unsigned>(b << 13) | 0x200);
                    for (int i = 0; i < m; i++) e.move(GD_COLOR00 + 2 * i, lo[b * 32 + i]);
                }
            }
            e.move(GD_BPLCON3, L.con3);
        } else
            for (int i = 0; i < L.ncolors; i++) e.move(GD_COLOR00 + 2 * i, hi ? hi[i] : 0);
    }
    /* blocks 2 and 3: bitplanes on at the first line, off after the last */
    bool same = !L.lace || (L.visible[0] == L.visible[1] && L.first[0] == L.first[1] && L.last[0] == L.last[1]);
    for (int f = 0; f < (same ? 1 : 2); f++) {
        if (!L.visible[f]) continue;
        int16_t fl = same ? 0 : (f ? NT_LOF : NT_SHT);
        e.wait(L.first[f], 0, fl);
        e.move(GD_BPLCON0, L.con0);
        e.wait(L.last[f], 0, fl);
        e.move(GD_BPLCON0, L.con0off);
    }
}

/* instructions of the first block that one field gets */
int first_block_len(const gd_coplist *cl, int field)
{
    int n = 0;
    for (int i = 0; i < cl->count; i++) {
        int16_t op = cl->copins[i].opcode;
        if (i && (op & 3) == GD_COPPER_WAIT) break;
        if (op & (field ? NT_SHT : NT_LOF)) continue;
        n++;
    }
    return n;
}

gd_coplist *new_coplist(gfx_env *env, int n)
{
    gd_coplist *cl = static_cast<gd_coplist *>(alloc(env, sizeof(gd_coplist), GFX_MEMF_PUBLIC | GFX_MEMF_CLEAR));
    if (!cl) return nullptr;
    if (n > 0) {
        cl->copins = static_cast<gd_copins *>(alloc(env, static_cast<uint32_t>(n) * sizeof(gd_copins),
                                                    GFX_MEMF_PUBLIC | GFX_MEMF_CLEAR));
        if (!cl->copins) {
            release(env, cl, sizeof(gd_coplist));
            return nullptr;
        }
    }
    cl->maxcount = static_cast<int16_t>(n);
    cl->copptr = cl->copins;
    return cl;
}

/* the gap line of a laid-out ViewPort */
int gap_line(const layout &L, int nfirst)
{
    int f0 = 1 << 30;
    for (int f = 0; f < 2; f++)
        if (L.visible[f]) f0 = imin(f0, L.first[f]);
    int g = f0 - ivg_of(nfirst);
    return g < 0 ? 0 : g;
}

/* (re)write vp->DspIns; answers MVP_ */
uint32_t build_dspins(gfx_env *env, gd_view *view, gd_viewport *vp, bool keep_size)
{
    layout L;
    uint32_t r = vp_layout(env, view, vp, L);
    if (r != GD_MVP_OK) return r;
    emitter e{nullptr, 0};
    if (L.visible[0] || L.visible[1]) emit_vp(env, vp, L, e, 0);
    int n = e.n;
    gd_coplist *cl = vp->dspins;
    if (keep_size && (!cl || cl->count != n)) return GD_MVP_NO_DSPINS;
    if (!cl || cl->maxcount < n) {
        gd_coplist *nl = new_coplist(env, n);
        if (!nl) return GD_MVP_NO_MEM;
        gd_FreeCopList(env, cl);
        vp->dspins = cl = nl;
    }
    cl->viewport = vp;
    if (!keep_size) cl->coplstart = cl->copsstart = nullptr;
    if (n) {
        /* the gap: the first block's longer field */
        emitter c{cl->copins, 0};
        emit_vp(env, vp, L, c, 0);
        cl->count = static_cast<int16_t>(n);
        int len = imax(first_block_len(cl, 0), L.lace ? first_block_len(cl, 1) : 0);
        emitter w{cl->copins, 0};
        emit_vp(env, vp, L, w, gap_line(L, len));
    }
    cl->count = static_cast<int16_t>(n);
    cl->copptr = cl->copins + n;
    return GD_MVP_OK;
}

/* ---- MrgCop ------------------------------------------------------------ */

struct cursor {                         /* a place in a chain of CopLists */
    gd_coplist *cl;
    int i;
    void fix()
    {
        while (cl) {
            if (i < cl->count) {
                if ((cl->copins[i].opcode & 3) == GD_CPRNXTBUF) {
                    cl = cl->copins[i].u.nxtlist;
                    i = 0;
                    continue;
                }
                return;
            }
            cl = cl->next;
            i = 0;
        }
    }
    gd_copins *get() { fix(); return cl ? &cl->copins[i] : nullptr; }
    void step() { i++; fix(); }
};

struct segment {
    int v, h;
    int16_t flags;                      /* the WAIT's CPR_NT_ */
    cursor at;                          /* the first MOVE */
    gd_coplist *dsp;                    /* a DspIns' first block: where MrgCop puts it is kept */
};

/* the segments of one list; abs: WAITs are frame lines, else the ViewPort's */
int scan(gd_coplist *cl, bool abs, int top, bool lace, segment *out, int n, gd_coplist *dsp)
{
    cursor c{cl, 0};
    c.fix();
    bool firstblock = true;
    while (gd_copins *ci = c.get()) {
        segment s{};
        if ((ci->opcode & 3) == GD_COPPER_WAIT) {
            int v = ci->u.w.a;
            s.v = abs ? v : top + (lace ? v >> 1 : v);
            s.h = ci->u.w.b;
            s.flags = static_cast<int16_t>(ci->opcode & (NT_LOF | NT_SHT));
            c.step();
        } else {
            s.v = top;                  /* MOVEs before any WAIT: at the ViewPort's top (ABI test) */
            s.h = 0;
        }
        s.at = c;
        s.dsp = firstblock ? dsp : nullptr;
        firstblock = false;
        if (out) out[n] = s;
        n++;
        while ((ci = c.get()) && (ci->opcode & 3) != GD_COPPER_WAIT) c.step();
    }
    return n;
}

int vp_top(gd_view *view, gd_viewport *vp)
{
    return view->dyoffset + ((vp->modes & GD_LACE) ? vp->dyoffset >> 1 : vp->dyoffset);
}

int gather(gfx_env *env, gd_view *view, segment *out)
{
    int n = 0;
    (void)env;
    for (gd_viewport *vp = view->viewport; vp; vp = vp->next) {
        if (vp->modes & GD_VP_HIDE) continue;
        int top = vp_top(view, vp);
        bool lace = (vp->modes & GD_LACE) != 0;
        if (vp->dspins) n = scan(vp->dspins, true, top, lace, out, n, vp->dspins);
        if (vp->sprins) n = scan(vp->sprins, false, top, lace, out, n, nullptr);
        if (vp->clrins) n = scan(vp->clrins, false, top, lace, out, n, nullptr);
        for (gd_ucoplist *u = vp->ucopins; u; u = u->next)
            if (u->firstcoplist) {
                int k = scan(u->firstcoplist, false, top, lace, out, n, nullptr);
                /* USER_COPPER_CLIP: only the ViewPort's own lines */
                gfx_colormap *cm = vp->colormap;
                if (out && cm && (cm->flags & GD_USER_COPPER_CLIP)) {
                    int h = (lace ? vp->dheight >> 1 : vp->dheight);
                    for (int i = n; i < k; i++)
                        if (out[i].v < top || out[i].v >= top + h) out[i].v = 1 << 20;
                }
                n = k;
            }
    }
    return n;
}

/* write (or count, with out 0) one field's hardware list */
int write_field(segment *seg, int nseg, int field, int rows, uint16_t *out)
{
    int n = 0;
    bool crossed = false;
    auto put = [&](unsigned a, unsigned b) {
        if (out) {
            out[2 * n] = static_cast<uint16_t>(a);
            out[2 * n + 1] = static_cast<uint16_t>(b);
        }
        n++;
    };
    const int16_t skip = field ? NT_SHT : NT_LOF;
    for (int i = 0; i < nseg; i++) {
        segment &s = seg[i];
        if (s.flags & skip) continue;
        int v = s.v < 0 ? 0 : s.v;
        if (v > rows) continue;         /* below the frame: CEND's 10000 and the like */
        if (v >= 256 && !crossed) {
            put(0xFFDF, 0xFFFE);        /* line 255 at its end, then into line 256 */
            put(GD_NOOP, 0);
            put(GD_NOOP, 0);
            crossed = true;
        }
        if (s.dsp && out) {
            if (field) s.dsp->copsstart = out + 2 * n;
            else s.dsp->coplstart = out + 2 * n;
        }
        put(static_cast<unsigned>((v & 0xFF) << 8 | (s.h & 0xFE) | 1), 0xFFFE);
        cursor c = s.at;
        while (gd_copins *ci = c.get()) {
            if ((ci->opcode & 3) == GD_COPPER_WAIT) break;
            if (!(ci->opcode & skip)) put(static_cast<unsigned>(ci->u.w.a) & 0x1FE, static_cast<uint16_t>(ci->u.w.b));
            c.step();
        }
    }
    put(0xFFFF, 0xFFFE);
    return n;
}

gd_cprlist *new_cprlist(gfx_env *env, int n)
{
    gd_cprlist *c = static_cast<gd_cprlist *>(alloc(env, sizeof(gd_cprlist), GFX_MEMF_PUBLIC | GFX_MEMF_CLEAR));
    if (!c) return nullptr;
    c->start = static_cast<uint16_t *>(alloc(env, static_cast<uint32_t>(n) * 4, GFX_MEMF_CHIP));
    if (!c->start) {
        release(env, c, sizeof(gd_cprlist));
        return nullptr;
    }
    c->maxcount = static_cast<int16_t>(n);
    return c;
}

void set_cop2(gfx_env *env, const uint16_t *list)
{
    uint32_t a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(list));
    put(env, GD_COP2LCH, static_cast<uint16_t>(a >> 16));
    put(env, GD_COP2LCL, static_cast<uint16_t>(a));
}

/* the list the copper should run for the coming field */
const uint16_t *field_list(gfx_env *env, bool lof)
{
    gfx_base *gb = base(env);
    gd_view *v = static_cast<gd_view *>(gb->actiview);
    state *s = st(env);
    if (!v || !gb->loflist) return s->copinit->wait_forever;
    if (!lof && (v->modes & GD_LACE) && gb->shflist) return gb->shflist;
    return gb->loflist;
}

bool in_list(const gd_cprlist *c, const uint16_t *p)
{
    return c && p && p >= c->start && p < c->start + 2 * c->maxcount;
}

} // namespace

namespace gd {

void patch_vp(gfx_env *env, gd_viewport *vp, bool colours_only)
{
    gfx_base *gb = base(env);
    gd_view *view = static_cast<gd_view *>(gb->actiview);
    if (!vp || !vp->dspins || !vp->dspins->count) return;
    gd_coplist *cl = vp->dspins;
    /* the View it was made for: the shown one if vp is in it */
    gd_viewport *q = view ? view->viewport : nullptr;
    while (q && q != vp) q = q->next;
    if (colours_only) {
        /* the palette's MOVEs in DspIns, from the ColorMap as it is now:
         * needs no View */
        gfx_colormap *cm = vp->colormap;
        if (!cm || !cm->colortable) return;
        const uint16_t *lo = cm->type >= GFX_COLORMAP_TYPE_V39 ? cm->lowcolorbits : nullptr;
        int bank = 0;
        bool loct = false;
        for (int i = 1; i < cl->count; i++) {
            gd_copins &ci = cl->copins[i];
            if ((ci.opcode & 3) == GD_COPPER_WAIT) break;
            int reg = ci.u.w.a & 0x1FE;
            if (reg == GD_BPLCON3) {
                bank = (ci.u.w.b >> 13) & 7;
                loct = (ci.u.w.b & 0x200) != 0;
            } else if (reg >= GD_COLOR00 && reg < GD_COLOR00 + 64) {
                int idx = bank * 32 + (reg - GD_COLOR00) / 2;
                if (idx < cm->count) ci.u.w.b = static_cast<int16_t>(loct ? (lo ? lo[idx] : 0) : cm->colortable[idx]);
            }
        }
    } else if (!q || build_dspins(env, view, vp, true) != GD_MVP_OK)
        return;                         /* not shown: MakeVPort makes it anew (ABI test) */
    if (!q) return;
    for (int f = 0; f < 2; f++) {
        uint16_t *at = f ? cl->copsstart : cl->coplstart;
        if (!in_list(f ? view->shfcprlist : view->lofcprlist, at)) continue;
        /* the block's WAIT and MOVEs, in place (the count MrgCop wrote) */
        int16_t skip = f ? NT_SHT : NT_LOF;
        int v = cl->copins[0].u.w.a;
        int n = 0;
        env->disable(env->ctx);
        at[0] = static_cast<uint16_t>(((v < 0 ? 0 : v) & 0xFF) << 8 | (cl->copins[0].u.w.b & 0xFE) | 1);
        at[1] = 0xFFFE;
        for (int i = 1; i < cl->count; i++) {
            gd_copins &ci = cl->copins[i];
            if ((ci.opcode & 3) == GD_COPPER_WAIT) break;
            if (ci.opcode & skip) continue;
            n++;
            at[2 * n] = static_cast<uint16_t>(ci.u.w.a & 0x1FE);
            at[2 * n + 1] = static_cast<uint16_t>(ci.u.w.b);
        }
        env->enable(env->ctx);
    }
}

} // namespace gd

/* ---- the calls --------------------------------------------------------- */

extern "C" {

void gd_InitView(gfx_env *env, gd_view *view)
{
    (void)env;
    zero(view, sizeof *view);
    view->dxoffset = VIEW_X;            /* ABI test: STANDARD_VIEW_X/Y */
    view->dyoffset = VIEW_Y;
}

void gd_InitVPort(gfx_env *env, gd_viewport *vp)
{
    (void)env;
    zero(vp, sizeof *vp);               /* ABI test: every field 0 */
}

uint32_t gd_MakeVPort(gfx_env *env, gd_view *view, gd_viewport *vp)
{
    if (!vp) return GD_MVP_NO_DISPLAY;
    if (vp->modes & GD_VP_HIDE) return GD_MVP_OK;
    /* VideoControl's batch, kept for this moment */
    gfx_colormap *cm = vp->colormap;
    if (cm && (cm->flags & GD_VIDEOCONTROL_BATCH) && cm->batch_items) {
        cm->flags &= static_cast<uint8_t>(~GD_VIDEOCONTROL_BATCH);
        gd_VideoControl(env, cm, cm->batch_items);
        cm->flags |= GD_VIDEOCONTROL_BATCH;
    }
    return build_dspins(env, view, vp, false);
}

uint32_t gd_MrgCop(gfx_env *env, gd_view *view)
{
    gfx_base *gb = base(env);
    if (!view) return GD_MCOP_NOP;
    int nseg = gather(env, view, nullptr);
    segment *seg = nullptr;
    uint32_t segsize = static_cast<uint32_t>(nseg) * sizeof(segment);
    if (nseg) {
        seg = static_cast<segment *>(alloc(env, segsize, GFX_MEMF_PUBLIC));
        if (!seg) return GD_MCOP_NO_MEM;
        gather(env, view, seg);
        /* stable insertion sort by beam position */
        for (int i = 1; i < nseg; i++) {
            segment s = seg[i];
            int j = i;
            while (j > 0 && (seg[j - 1].v > s.v || (seg[j - 1].v == s.v && seg[j - 1].h > s.h))) {
                seg[j] = seg[j - 1];
                j--;
            }
            seg[j] = s;
        }
    }
    const bool lace = (view->modes & GD_LACE) != 0;
    const int rows = view_geom(env, view).total_rows;
    gd_cprlist *old[2] = { view->lofcprlist, view->shfcprlist };
    gd_cprlist *nw[2] = { nullptr, nullptr };
    bool ok = true;
    for (int f = 0; f < (lace ? 2 : 1) && ok; f++) {
        int n = write_field(seg, nseg, f, rows, nullptr);
        if (old[f] && old[f]->maxcount >= n) nw[f] = old[f];
        else if (!(nw[f] = new_cprlist(env, n))) ok = false;
    }
    if (!ok) {
        for (int f = 0; f < 2; f++)
            if (nw[f] && nw[f] != old[f]) gd_FreeCprList(env, nw[f]);
        release(env, seg, segsize);
        return GD_MCOP_NO_MEM;
    }
    const bool shown = gb->actiview == view;
    for (int f = 0; f < (lace ? 2 : 1); f++) {
        if (nw[f] == old[f]) env->disable(env->ctx);
        write_field(seg, nseg, f, rows, nw[f]->start);
        if (nw[f] == old[f]) env->enable(env->ctx);
    }
    release(env, seg, segsize);
    view->lofcprlist = nw[0];
    view->shfcprlist = nw[1];
    if (shown) {
        env->disable(env->ctx);
        gb->loflist = nw[0]->start;
        gb->shflist = nw[1] ? nw[1]->start : nw[0]->start;
        env->enable(env->ctx);
    }
    bool freed = false;
    for (int f = 0; f < 2; f++)
        if (old[f] && old[f] != nw[f]) {
            if (shown && !freed) {
                /* the copper may still be in the old list until the next frame */
                set_cop2(env, field_list(env, (env->chip_read(env->ctx, GD_VPOSR) & 0x8000) != 0));
                gd_WaitTOF(env);
                freed = true;
            }
            gd_FreeCprList(env, old[f]);
        }
    return GD_MCOP_OK;
}

void gd_LoadView(gfx_env *env, gd_view *view)
{
    gfx_base *gb = base(env);
    state *s = st(env);
    env->disable(env->ctx);
    gb->actiview = view;
    if (view && view->lofcprlist) {
        gb->loflist = view->lofcprlist->start;
        gb->shflist = view->shfcprlist ? view->shfcprlist->start : view->lofcprlist->start;
    } else {
        gb->loflist = gb->shflist = nullptr;
    }
    /* the View's monitor (ECS Agnus and up: PAL or NTSC beam) */
    gd_monitorspec *ms = monitor_of_view(env, view);
    if (ms && (gb->chiprevbits0 & GFX_GFXF_HR_AGNUS)) {
        put(env, GD_BEAMCON0, ms->beamcon0);
        gb->current_monitor = ms;
        gb->current_tot_rows = ms->total_rows;
        gb->current_tot_cclks = ms->total_colorclocks;
    }
    if (view) gb->modes = view->modes;
    uint32_t a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(s->copinit));
    put(env, GD_COP1LCH, static_cast<uint16_t>(a >> 16));
    put(env, GD_COP1LCL, static_cast<uint16_t>(a));
    set_cop2(env, field_list(env, (env->chip_read(env->ctx, GD_VPOSR) & 0x8000) != 0));
    env->enable(env->ctx);
}

void gd_FreeCopList(gfx_env *env, gd_coplist *cl)
{
    while (cl) {
        gd_coplist *next = cl->next;
        release(env, cl->copins, static_cast<uint32_t>(cl->maxcount) * sizeof(gd_copins));
        release(env, cl, sizeof(gd_coplist));
        cl = next;
    }
}

void gd_FreeCprList(gfx_env *env, gd_cprlist *c)
{
    while (c) {
        gd_cprlist *next = c->next;
        release(env, c->start, static_cast<uint32_t>(c->maxcount) * 4);
        release(env, c, sizeof(gd_cprlist));
        c = next;
    }
}

void gd_FreeVPortCopLists(gfx_env *env, gd_viewport *vp)
{
    if (!vp) return;
    gd_FreeCopList(env, vp->dspins);
    gd_FreeCopList(env, vp->sprins);
    gd_FreeCopList(env, vp->clrins);
    vp->dspins = vp->sprins = vp->clrins = nullptr;
    for (gd_ucoplist *u = vp->ucopins; u;) {
        gd_ucoplist *next = u->next;
        gd_FreeCopList(env, u->firstcoplist);
        release(env, u, sizeof(gd_ucoplist));
        u = next;
    }
    vp->ucopins = nullptr;
}

void gd_ScrollVPort(gfx_env *env, gd_viewport *vp)
{
    if (!vp || !vp->dspins) return;
    patch_vp(env, vp, false);
}

gd_coplist *gd_UCopperListInit(gfx_env *env, gd_ucoplist *ucl, int32_t n)
{
    if (!ucl || n <= 0 || n > 0x7FFF) return nullptr;
    gd_coplist *cl = ucl->firstcoplist;
    if (cl && cl->maxcount >= n && !cl->next) {
        cl->count = 0;                  /* ABI test: OS 3.2 may allocate anew */
        cl->copptr = cl->copins;
    } else {
        gd_coplist *nl = new_coplist(env, n);
        if (!nl) return nullptr;
        gd_FreeCopList(env, cl);
        cl = nl;
    }
    ucl->firstcoplist = ucl->coplist = cl;
    return cl;
}

void gd_CWait(gfx_env *env, gd_ucoplist *ucl, int32_t v, int32_t h)
{
    (void)env;
    gd_coplist *cl = ucl ? ucl->coplist : nullptr;
    if (!cl || cl->count >= cl->maxcount) return;
    cl->copptr->opcode = GD_COPPER_WAIT;
    cl->copptr->u.w.a = static_cast<int16_t>(v);
    cl->copptr->u.w.b = static_cast<int16_t>(h);
}

void gd_CMove(gfx_env *env, gd_ucoplist *ucl, void *reg, int32_t value)
{
    (void)env;
    gd_coplist *cl = ucl ? ucl->coplist : nullptr;
    if (!cl || cl->count >= cl->maxcount) return;
    cl->copptr->opcode = GD_COPPER_MOVE;
    cl->copptr->u.w.a = static_cast<int16_t>(reinterpret_cast<uintptr_t>(reg) & 0x1FE);
    cl->copptr->u.w.b = static_cast<int16_t>(value);
}

void gd_CBump(gfx_env *env, gd_ucoplist *ucl)
{
    gd_coplist *cl = ucl ? ucl->coplist : nullptr;
    if (!cl || cl->count >= cl->maxcount) return;
    cl->count++;
    cl->copptr++;
    if (cl->count == cl->maxcount) {
        /* full: the next instructions go to a new CopList of the same size */
        gd_coplist *nl = new_coplist(env, cl->maxcount);
        if (!nl) {
            cl->count--;                /* out of memory: the last one is written over (ours) */
            cl->copptr--;
            return;
        }
        cl->next = nl;
        ucl->coplist = nl;
    }
}

void gd_ChangeVPBitMap(gfx_env *env, gd_viewport *vp, gfx_bitmap *bm, gd_dbufinfo *dbi)
{
    gfx_base *gb = base(env);
    if (!vp || !vp->rasinfo || !bm) return;
    vp->rasinfo->bitmap = bm;
    patch_vp(env, vp, false);
    if (!dbi) return;
    /* the messages go back at the next vertical blank */
    env->disable(env->ctx);
    gd_dbufinfo *q = static_cast<gd_dbufinfo *>(gb->dblist);
    while (q && q != dbi) q = static_cast<gd_dbufinfo *>(q->link1);
    if (!q) {
        dbi->link1 = gb->dblist;
        gb->dblist = dbi;
    }
    env->enable(env->ctx);
}

gd_dbufinfo *gd_AllocDBufInfo(gfx_env *env, gd_viewport *vp)
{
    if (!vp) return nullptr;
    gd_dbufinfo *d = static_cast<gd_dbufinfo *>(alloc(env, sizeof(gd_dbufinfo), GFX_MEMF_PUBLIC | GFX_MEMF_CLEAR));
    if (d) d->matchlong = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(vp));
    return d;
}

void gd_FreeDBufInfo(gfx_env *env, gd_dbufinfo *dbi)
{
    gfx_base *gb = base(env);
    if (!dbi) return;
    env->disable(env->ctx);
    for (void **pp = &gb->dblist; *pp; pp = &static_cast<gd_dbufinfo *>(*pp)->link1)
        if (*pp == dbi) {
            *pp = dbi->link1;
            break;
        }
    env->enable(env->ctx);
    release(env, dbi, sizeof(gd_dbufinfo));
}

uint16_t gd_CalcIVG(gfx_env *env, gd_view *view, gd_viewport *vp)
{
    (void)env;
    (void)view;
    if (!vp || !vp->dspins || !vp->dspins->count) return 0;
    int n = imax(first_block_len(vp->dspins, 0), first_block_len(vp->dspins, 1));
    return static_cast<uint16_t>(ivg_of(n));
}

void gfx_hook_vp_colors(gfx_env *env, void *vp, uint32_t first, uint32_t count)
{
    (void)first;
    (void)count;
    gd_viewport *v = static_cast<gd_viewport *>(vp);
    if (v && v->dspins && !(v->colormap && (v->colormap->auxflags & GD_CMAF_NO_INTERMED_UPDATE)))
        patch_vp(env, v, true);
}

void gd_vblank(gfx_env *env)
{
    gfx_base *gb = base(env);
    gb->vbcounter++;
    /* the coming field's list (the LOF bit says which field this is) */
    if (gb->actiview && st(env)) set_cop2(env, field_list(env, (env->chip_read(env->ctx, GD_VPOSR) & 0x8000) != 0));
    /* ChangeVPBitMap: the new bitmap is shown from this frame on, the old one free */
    while (gd_dbufinfo *d = static_cast<gd_dbufinfo *>(gb->dblist)) {
        gb->dblist = d->link1;
        d->link1 = nullptr;
        if (d->safemessage.replyport) env->reply_msg(env->ctx, &d->safemessage);
        if (d->dispmessage.replyport) env->reply_msg(env->ctx, &d->dispmessage);
    }
    /* WaitTOF */
    gfx_list *q = &gb->tof_waitq;
    for (gfx_node *n = q->head; n->succ;) {
        gfx_node *next = n->succ;
        tof_waiter *w = reinterpret_cast<tof_waiter *>(n);
        void *task = w->task;
        remove(n);
        w->task = nullptr;
        env->signal(env->ctx, task, SIGF_SINGLE);
        n = next;
    }
}

} // extern "C"
