/* SPDX-License-Identifier: MIT */
/* OpenGfx graphics.library: the gels LVOs in their registers, gels' init,
 * and the calls into user code that need registers. Copyright (c) 2026
 * Dalsin Limited. MIT.
 *
 * Each gfx_lvo_<Call> takes the fd's arguments in the registers
 * graphics.lvo lists (library/graphics/check_regs.py compares them), with
 * A6 the library base, and calls gels_core.cpp with the state gfx_init_gels
 * keeps in gfx_private_of(GfxBase)->area[GFX_AREA_GELS]. Built by the Amiga
 * compiler, whose register parameters GFX_REG names; elsewhere GFX_REG is a
 * plain parameter and the register calls are stubs, for the tests' compile
 * check. No NDK header, no .data, no .bss. */
#include "gels_core.h"

#ifndef GFX_REG
#define GFX_REG(r, decl) decl
#endif

static struct gels_state *gels_of_base(struct gfx_base *gb)
{
    return (struct gels_state *)gfx_private_of(gb)->area[GFX_AREA_GELS];
}

/* ---- calls into user code ------------------------------------------------ */
#if defined(__amigaos__) || defined(AMIGA)
/* A QBlit routine (and its cleanup): A0 the custom chips, A1 the node; it
 * may change D0, D1, A0 and A1 (ABI test: what OS 3.2 gives the cleanup). */
static int32_t call_regs(void *fn, struct gl_bltnode *bn)
{
    register int32_t d0 __asm("d0");
    register void *a0 __asm("a0") = (void *)0xDFF000;
    register void *a1 __asm("a1") = bn;
    register void *a5 __asm("a5") = fn;
    __asm volatile("jsr (%3)" : "=r"(d0), "+r"(a0), "+r"(a1) : "r"(a5) : "d1", "cc", "memory");
    return d0;
}

/* exec's interrupt entries: A1 is_Data (the gels state). The blitter's is
 * a handler (SetIntVector, INTB_BLIT); the vertical blank's a server,
 * which answers with Z set so the chain goes on. Exec saves D0/D1/A0/A1/
 * A5/A6 around both; the C they call keeps the rest. */
void gels_blit_entry(void);
void gels_vertb_entry(void);
void gels_blit_c(struct gels_state *st) { gels_blit_int(st); }
void gels_vertb_c(struct gels_state *st) { gels_vertb(st); }
__asm__(".text\n"
        "\t.globl _gels_blit_entry\n"
        "_gels_blit_entry:\n"
        "\tmove.l %a1,-(%sp)\n"
        "\tjsr _gels_blit_c\n"
        "\taddq.l #4,%sp\n"
        "\trts\n"
        "\t.globl _gels_vertb_entry\n"
        "_gels_vertb_entry:\n"
        "\tmove.l %a1,-(%sp)\n"
        "\tjsr _gels_vertb_c\n"
        "\taddq.l #4,%sp\n"
        "\tmoveq #0,%d0\n"
        "\trts\n");
#define BLIT_ENTRY ((void (*)(void))gels_blit_entry)
#define VERTB_ENTRY ((void (*)(void))gels_vertb_entry)
#else
static int32_t call_regs(void *fn, struct gl_bltnode *bn) { (void)fn; (void)bn; return 0; }
#define BLIT_ENTRY ((void (*)(void))0)
#define VERTB_ENTRY ((void (*)(void))0)
#endif

static int32_t call_blit(struct gels_state *st, struct gl_bltnode *bn)
{
    (void)st;
    return call_regs(bn->function, bn);
}
static void call_cleanup(struct gels_state *st, struct gl_bltnode *bn)
{
    (void)st;
    if (bn->cleanup) call_regs(bn->cleanup, bn);
}

/* ---- init and exit ---------------------------------------------------------- */
static const char blit_name[] = "graphics blitter";
static const char vertb_name[] = "graphics QBSBlit";

int gfx_init_gels(struct gfx_env *env)
{
    struct gfx_base *gb = env->base;
    struct gels_state *st = (struct gels_state *)env->alloc(env->ctx, sizeof *st, GFX_MEMF_PUBLIC | GFX_MEMF_CLEAR);
    if (!st) return 0;
    st->env = env;
    st->call_blit = call_blit;
    st->call_cleanup = call_cleanup;
    st->has_blitter = 1;
    gels_init(st);
    gfx_private_of(gb)->area[GFX_AREA_GELS] = st;
    if (BLIT_ENTRY && env->set_int_vector) {
        gb->bltsrv.node.type = 2;           /* NT_INTERRUPT */
        gb->bltsrv.node.name = (char *)blit_name;
        gb->bltsrv.data = st;
        gb->bltsrv.code = BLIT_ENTRY;
        st->old_blit = env->set_int_vector(env->ctx, 6, &gb->bltsrv);  /* INTB_BLIT */
        env->chip_write(env->ctx, 0x09C, 0x0040);                      /* INTREQ: clear BLIT */
        env->chip_write(env->ctx, 0x09A, 0x8040);                      /* INTENA: SET | BLIT */
    }
    if (VERTB_ENTRY && env->add_int_server) {
        st->vertb.node.type = 2;
        st->vertb.node.pri = -10;
        st->vertb.node.name = (char *)vertb_name;
        st->vertb.data = st;
        st->vertb.code = VERTB_ENTRY;
        env->add_int_server(env->ctx, 5, &st->vertb);                  /* INTB_VERTB */
    }
    return 1;
}

void gfx_exit_gels(struct gfx_env *env)
{
    struct gels_state *st = gels_of_base(env->base);
    if (!st) return;
    gels_WaitBlit(st);
    if (st->vertb.code && env->rem_int_server) env->rem_int_server(env->ctx, 5, &st->vertb);
    if (env->base->bltsrv.code == BLIT_ENTRY && BLIT_ENTRY && env->set_int_vector) {
        env->chip_write(env->ctx, 0x09A, 0x0040);                      /* INTENA: clear BLIT */
        env->set_int_vector(env->ctx, 6, st->old_blit);
    }
    gfx_private_of(env->base)->area[GFX_AREA_GELS] = 0;
    env->free(env->ctx, st, sizeof *st);
}

/* ---- the LVOs ------------------------------------------------------------------ */

void gfx_lvo_BltTemplate(GFX_REG(a0, const void *source),
    GFX_REG(d0, int32_t srcx),
    GFX_REG(d1, int32_t srcmod),
    GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d2, int32_t dx),
    GFX_REG(d3, int32_t dy),
    GFX_REG(d4, int32_t w),
    GFX_REG(d5, int32_t h),
    GFX_REG(a6, struct gfx_base *gb))
{
    gels_BltTemplate(gels_of_base(gb), source, srcx, srcmod, rp, dx, dy, w, h);
}

void gfx_lvo_AddBob(GFX_REG(a0, struct gl_bob *bob),
    GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(a6, struct gfx_base *gb))
{
    gels_AddBob(gels_of_base(gb), bob, rp);
}

void gfx_lvo_AddVSprite(GFX_REG(a0, struct gl_vsprite *vs),
    GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(a6, struct gfx_base *gb))
{
    gels_AddVSprite(gels_of_base(gb), vs, rp);
}

void gfx_lvo_DoCollision(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(a6, struct gfx_base *gb))
{
    gels_DoCollision(gels_of_base(gb), rp);
}

void gfx_lvo_DrawGList(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(a0, void *vp),
    GFX_REG(a6, struct gfx_base *gb))
{
    gels_DrawGList(gels_of_base(gb), rp, vp);
}

void gfx_lvo_InitGels(GFX_REG(a0, struct gl_vsprite *head),
    GFX_REG(a1, struct gl_vsprite *tail),
    GFX_REG(a2, struct gl_gelsinfo *gi),
    GFX_REG(a6, struct gfx_base *gb))
{
    gels_InitGels(gels_of_base(gb), head, tail, gi);
}

void gfx_lvo_InitMasks(GFX_REG(a0, struct gl_vsprite *vs),
    GFX_REG(a6, struct gfx_base *gb))
{
    gels_InitMasks(gels_of_base(gb), vs);
}

void gfx_lvo_RemIBob(GFX_REG(a0, struct gl_bob *bob),
    GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(a2, void *vp),
    GFX_REG(a6, struct gfx_base *gb))
{
    gels_RemIBob(gels_of_base(gb), bob, rp, vp);
}

void gfx_lvo_RemVSprite(GFX_REG(a0, struct gl_vsprite *vs),
    GFX_REG(a6, struct gfx_base *gb))
{
    gels_RemVSprite(gels_of_base(gb), vs);
}

void gfx_lvo_SetCollision(GFX_REG(d0, uint32_t num),
    GFX_REG(a0, gl_collfn routine),
    GFX_REG(a1, struct gl_gelsinfo *gi),
    GFX_REG(a6, struct gfx_base *gb))
{
    gels_SetCollision(gels_of_base(gb), num, routine, gi);
}

void gfx_lvo_SortGList(GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(a6, struct gfx_base *gb))
{
    gels_SortGList(gels_of_base(gb), rp);
}

void gfx_lvo_AddAnimOb(GFX_REG(a0, struct gl_animob *ob),
    GFX_REG(a1, struct gl_animob **key),
    GFX_REG(a2, struct gfx_rastport *rp),
    GFX_REG(a6, struct gfx_base *gb))
{
    gels_AddAnimOb(gels_of_base(gb), ob, key, rp);
}

void gfx_lvo_Animate(GFX_REG(a0, struct gl_animob **key),
    GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(a6, struct gfx_base *gb))
{
    gels_Animate(gels_of_base(gb), key, rp);
}

int32_t gfx_lvo_GetGBuffers(GFX_REG(a0, struct gl_animob *ob),
    GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d0, int32_t db),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gels_GetGBuffers(gels_of_base(gb), ob, rp, db);
}

void gfx_lvo_InitGMasks(GFX_REG(a0, struct gl_animob *ob),
    GFX_REG(a6, struct gfx_base *gb))
{
    gels_InitGMasks(gels_of_base(gb), ob);
}

void gfx_lvo_WaitBlit(GFX_REG(a6, struct gfx_base *gb))
{
    gels_WaitBlit(gels_of_base(gb));
}

void gfx_lvo_QBlit(GFX_REG(a1, struct gl_bltnode *bn),
    GFX_REG(a6, struct gfx_base *gb))
{
    gels_QBlit(gels_of_base(gb), bn);
}

void gfx_lvo_QBSBlit(GFX_REG(a1, struct gl_bltnode *bn),
    GFX_REG(a6, struct gfx_base *gb))
{
    gels_QBSBlit(gels_of_base(gb), bn);
}

void gfx_lvo_BltClear(GFX_REG(a1, void *mem),
    GFX_REG(d0, uint32_t count),
    GFX_REG(d1, uint32_t flags),
    GFX_REG(a6, struct gfx_base *gb))
{
    gels_BltClear(gels_of_base(gb), mem, count, flags);
}

void gfx_lvo_OwnBlitter(GFX_REG(a6, struct gfx_base *gb))
{
    gels_OwnBlitter(gels_of_base(gb));
}

void gfx_lvo_DisownBlitter(GFX_REG(a6, struct gfx_base *gb))
{
    gels_DisownBlitter(gels_of_base(gb));
}

void gfx_lvo_FreeGBuffers(GFX_REG(a0, struct gl_animob *ob),
    GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d0, int32_t db),
    GFX_REG(a6, struct gfx_base *gb))
{
    gels_FreeGBuffers(gels_of_base(gb), ob, rp, db);
}

void gfx_lvo_BltMaskBitMapRastPort(GFX_REG(a0, struct gfx_bitmap *src),
    GFX_REG(d0, int32_t sx),
    GFX_REG(d1, int32_t sy),
    GFX_REG(a1, struct gfx_rastport *rp),
    GFX_REG(d2, int32_t dx),
    GFX_REG(d3, int32_t dy),
    GFX_REG(d4, int32_t w),
    GFX_REG(d5, int32_t h),
    GFX_REG(d6, uint32_t minterm),
    GFX_REG(a2, const void *mask),
    GFX_REG(a6, struct gfx_base *gb))
{
    gels_BltMaskBitMapRastPort(gels_of_base(gb), src, sx, sy, rp, dx, dy, w, h, minterm, mask);
}
