/* SPDX-License-Identifier: MIT */
/* RegionABITest: OpenGfx's regions (library/core/gfx_region.c) side by
 * side with OS 3.2's graphics.library. Copyright (c) 2026 AmigaChrome Team.
 * MIT.
 *
 * Each call is made on an OS 3.2 region (NewRegion, OrRectRegion...) and
 * on ours with the same arguments; then the answers and the areas the two
 * regions cover are compared. A difference prints both lists. How the area
 * is cut into rectangles, their order and the bounds are not compared:
 * OS 3.2 never merges rectangles, puts the newest first, keeps an empty
 * rectangle given to OrRectRegion in its list and leaves bounds that are
 * not the area's, where ours keeps bands (measured at 6a4988c). They are
 * counted, and the fixed cases print both lists to show the shapes.
 * XorRegionRegion of a region with itself is not made: OS 3.2 fills the
 * region with garbage (measured), where ours empties it.
 *   RegionABITest [SEED=n] [ROUNDS=n]
 * Answers 0 when nothing differs, 5 when something does. */
#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/regions.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <stddef.h>
#include <string.h>
#include "../library/core/gfx_region.h"

/* our structures must be the NDK's */
typedef char check_region[sizeof(struct Region) == sizeof(struct gfx_region) ? 1 : -1];
typedef char check_rrect[sizeof(struct RegionRectangle) == sizeof(struct gfx_rrect) ? 1 : -1];
typedef char check_rlist[offsetof(struct Region, RegionRectangle) == offsetof(struct gfx_region, rects) ? 1 : -1];
typedef char check_rbounds[offsetof(struct RegionRectangle, bounds) == offsetof(struct gfx_rrect, bounds) ? 1 : -1];

static void *m_alloc(void *ctx, uint32_t size) { (void)ctx; return AllocMem(size, MEMF_ANY); }
static void m_free(void *ctx, void *p, uint32_t size) { (void)ctx; FreeMem(p, size); }
static const struct gfx_rgn_mem mem = { m_alloc, m_free, 0 };

#define NREG 3
static struct Region *os[NREG];
static struct gfx_region ours[NREG];
static ULONG calls, diffs;
static ULONG seed = 0x2E64;
static ULONG rnd(void) { seed = seed * 1103515245UL + 12345UL; return seed >> 8; }

#define GX0 (-40)
#define GY0 (-24)
#define GW 160
#define GH 96
static UBYTE grid[2][GH][GW];

static void paint(UBYTE g[GH][GW], const struct Rectangle *b, const struct RegionRectangle *p)
{
    LONG x, y;
    memset(g, 0, GH * GW);
    for (; p; p = p->Next)
        for (y = b->MinY + p->bounds.MinY; y <= b->MinY + p->bounds.MaxY; y++)
            for (x = b->MinX + p->bounds.MinX; x <= b->MinX + p->bounds.MaxX; x++)
                if (x >= GX0 && x < GX0 + GW && y >= GY0 && y < GY0 + GH) g[y - GY0][x - GX0] = 1;
}

static void show(const char *who, const struct Rectangle *b, const struct RegionRectangle *p)
{
    int n = 0;
    Printf("  %s: bounds %ld,%ld-%ld,%ld:", (LONG)who, (LONG)b->MinX, (LONG)b->MinY, (LONG)b->MaxX, (LONG)b->MaxY);
    for (; p && n < 24; p = p->Next, n++)
        Printf(" %ld,%ld-%ld,%ld", (LONG)p->bounds.MinX, (LONG)p->bounds.MinY, (LONG)p->bounds.MaxX,
               (LONG)p->bounds.MaxY);
    if (p) Printf(" ...");
    Printf("\n");
}

static ULONG cuts;

static void compare(const char *what, int i, LONG ra, LONG rb, int verbose)
{
    const struct Region *a = os[i];
    const struct RegionRectangle *p = a->RegionRectangle, *pq = (const struct RegionRectangle *)ours[i].rects;
    const struct gfx_rrect *q = ours[i].rects;
    int same = (ra != 0) == (rb != 0), cut;
    calls++;
    paint(grid[0], &a->bounds, a->RegionRectangle);
    paint(grid[1], (const struct Rectangle *)&ours[i].bounds, pq);
    if (memcmp(grid[0], grid[1], sizeof grid[0])) same = 0;
    cut = memcmp(&a->bounds, &ours[i].bounds, sizeof a->bounds) != 0;
    for (; !cut && p && q; p = p->Next, q = q->next)
        cut = memcmp(&p->bounds, &q->bounds, sizeof p->bounds) != 0;
    if (p || q) cut = 1;
    if (cut) cuts++;
    if (same && !verbose) return;
    if (!same) {
        diffs++;
        Printf("DIFF %s (region %ld): answers %ld %ld\n", (LONG)what, (LONG)i, ra, rb);
    } else
        Printf("CASE %s: same area%s\n", (LONG)what, (LONG)(cut ? ", cut differently" : ", same list"));
    show("OS  ", &a->bounds, a->RegionRectangle);
    if (!same || cut) show("ours", (const struct Rectangle *)&ours[i].bounds, pq);
}

static void rect(struct Rectangle *r, WORD x0, WORD y0, WORD x1, WORD y1)
{
    r->MinX = x0; r->MinY = y0; r->MaxX = x1; r->MaxY = y1;
}

/* op: 0 Or, 1 And, 2 Xor, 3 Clear */
static void rect_call(const char *what, int i, int op, const struct Rectangle *r, int verbose)
{
    LONG ra = 1, rb;
    switch (op) {
    case 0: ra = OrRectRegion(os[i], r); break;
    case 1: AndRectRegion(os[i], r); break;
    case 2: ra = XorRectRegion(os[i], r); break;
    default: ra = ClearRectRegion(os[i], r); break;
    }
    rb = gfx_rgn_rect(&ours[i], op, (const struct gfx_rect *)r, &mem);
    compare(what, i, ra, rb, verbose);
}

static void region_call(const char *what, int d, int op, int s, int verbose)
{
    LONG ra, rb;
    switch (op) {
    case 0: ra = OrRegionRegion(os[s], os[d]); break;
    case 1: ra = AndRegionRegion(os[s], os[d]); break;
    default: ra = XorRegionRegion(os[s], os[d]); break;
    }
    rb = gfx_rgn_region(&ours[d], op, &ours[s], &mem);
    compare(what, d, ra, rb, verbose);
}

static void clear(int i, const char *what, int verbose)
{
    ClearRegion(os[i]);
    gfx_rgn_clear(&ours[i], &mem);
    compare(what, i, 1, 1, verbose);
}

static void cases(void)
{
    struct Rectangle r;
    clear(0, "empty", 1);
    rect(&r, 10, 10, 5, 20);    rect_call("Or of an empty rect", 0, 0, &r, 1);
    rect(&r, 10, 10, 19, 19);   rect_call("Or one rect", 0, 0, &r, 1);
    rect(&r, 30, 10, 39, 19);   rect_call("Or beside it", 0, 0, &r, 1);
    rect(&r, 20, 10, 29, 19);   rect_call("Or the gap between", 0, 0, &r, 1);
    rect(&r, 10, 20, 39, 29);   rect_call("Or the same width below", 0, 0, &r, 1);
    rect(&r, 15, 25, 50, 40);   rect_call("Or an overlapping one", 0, 0, &r, 1);
    rect(&r, 20, 15, 25, 30);   rect_call("Xor a hole", 0, 2, &r, 1);
    rect(&r, 0, 30, 60, 32);    rect_call("Clear a stripe", 0, 3, &r, 1);
    rect(&r, 12, 12, 45, 35);   rect_call("And a window", 0, 1, &r, 1);
    rect(&r, 100, 100, 110, 110); rect_call("And away to nothing", 0, 1, &r, 1);
    rect(&r, -20, -10, -5, 5);  rect_call("Or negative", 0, 0, &r, 1);
    rect(&r, -32768, 0, 32767, 0); rect_call("Or the widest row", 1, 0, &r, 1);
    clear(1, "ClearRegion", 1);
    rect(&r, 0, 0, 9, 9);       rect_call("Or a", 1, 0, &r, 1);
    rect(&r, 5, 5, 14, 14);     rect_call("Or b", 2, 0, &r, 1);
    region_call("OrRegionRegion", 2, 0, 1, 1);
    region_call("XorRegionRegion", 2, 2, 1, 1);
    rect(&r, 5, 5, 14, 14);     rect_call("Or b again", 2, 0, &r, 1);
    region_call("AndRegionRegion", 2, 1, 1, 1);
    region_call("OrRegionRegion with itself", 2, 0, 2, 1);
    clear(0, "ClearRegion 0", 0);
    region_call("AndRegionRegion with empty", 2, 1, 0, 1);
    rect(&r, 3, 3, 3, 3);       rect_call("ClearRect on empty", 0, 3, &r, 1);
    rect(&r, 3, 3, 3, 3);       rect_call("Xor one pixel", 0, 2, &r, 1);
    rect(&r, 3, 3, 3, 3);       rect_call("Xor it away", 0, 2, &r, 1);
}

static void random_rect(struct Rectangle *r)
{
    WORD x0 = (WORD)(GX0 + (LONG)(rnd() % GW)), y0 = (WORD)(GY0 + (LONG)(rnd() % GH));
    WORD x1 = (WORD)(x0 + (LONG)(rnd() % 48)), y1 = (WORD)(y0 + (LONG)(rnd() % 32));
    if (rnd() % 20 == 0) x1 = (WORD)(x0 - 1);
    if (x1 >= GX0 + GW) x1 = GX0 + GW - 1;
    if (y1 >= GY0 + GH) y1 = GY0 + GH - 1;
    rect(r, x0, y0, x1, y1);
}

int main(void)
{
    static const char *const names[] = { "OrRectRegion", "AndRectRegion", "XorRectRegion", "ClearRectRegion",
                                         "OrRegionRegion", "AndRegionRegion", "XorRegionRegion", "ClearRegion" };
    LONG args[2] = { 0, 0 }, rounds = 3000, k;
    struct RDArgs *rda = ReadArgs("SEED/K/N,ROUNDS/K/N", args, NULL);
    int i;
    if (rda) {
        if (args[0]) seed = *(ULONG *)args[0];
        if (args[1]) rounds = *(LONG *)args[1];
        FreeArgs(rda);
    }
    Printf("graphics.library %ld.%ld, seed %lu\n", (LONG)GfxBase->LibNode.lib_Version,
           (LONG)GfxBase->LibNode.lib_Revision, seed);
    for (i = 0; i < NREG; i++) {
        os[i] = NewRegion();
        gfx_rgn_init(&ours[i]);
        if (!os[i]) { Printf("no memory\n"); return 20; }
    }
    cases();
    for (k = 0; k < rounds && !(SetSignal(0, 0) & SIGBREAKF_CTRL_C); k++) {
        int op = (int)(rnd() % 8), d = (int)(rnd() % NREG), s = (int)(rnd() % NREG);
        struct Rectangle r;
        if (op < 4) {
            random_rect(&r);
            rect_call(names[op], d, op, &r, 0);
        } else if (op < 7) {
            if (op == 6 && s == d) s = (d + 1) % NREG;
            region_call(names[op], d, op - 4, s, 0);
        }
        else if (rnd() % 4 == 0)
            clear(d, names[op], 0);
    }
    for (i = 0; i < NREG; i++) {
        DisposeRegion(os[i]);
        gfx_rgn_clear(&ours[i], &mem);
    }
    Printf("%lu calls, %lu differ, %lu cut differently\n", calls, diffs, cuts);
    return diffs ? 5 : 0;
}
