/* SPDX-License-Identifier: MIT */
/* OpenGfx graphics.library: the env every area's core reaches exec, the
 * machine and the other areas through. Copyright (c) 2026 Dalsin Limited.
 * MIT.
 *
 * No Amiga headers: the lean C++ cores of library/graphics (face),
 * library/display, library/text and library/gels include this and
 * gfx_types.h only. On the Amiga the face's shell (opengfx_lib.c) fills the
 * env from exec, utility.library and the custom chips; in the tests each
 * area fills it with a simulated machine. The rules are in OWNERS.md.
 *
 * The library has no .data or .bss: the env, and every area's state, live
 * in the library base, after the public GfxBase (gfx_private_of). */
#ifndef OPENGFX_GFX_ENV_H
#define OPENGFX_GFX_ENV_H

#include "gfx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct gfx_env {
    void *ctx;                  /* SysBase on the Amiga; the test's machine elsewhere */
    struct gfx_base *base;      /* the library base: public GfxBase, then gfx_private */

    /* exec memory. flags are exec's MEMF_ (GFX_MEMF_*); free gets the
     * size alloc was given. avail is AvailMem. */
    void *(*alloc)(void *ctx, uint32_t size, uint32_t memflags);
    void (*free)(void *ctx, void *p, uint32_t size);
    uint32_t (*avail)(void *ctx, uint32_t memflags);

    /* exclusion */
    void (*forbid)(void *ctx);
    void (*permit)(void *ctx);
    void (*disable)(void *ctx);
    void (*enable)(void *ctx);
    void (*sem_init)(void *ctx, struct gfx_semaphore *sem);
    void (*sem_obtain)(void *ctx, struct gfx_semaphore *sem);
    void (*sem_obtain_shared)(void *ctx, struct gfx_semaphore *sem);
    int32_t (*sem_attempt)(void *ctx, struct gfx_semaphore *sem); /* 1 got it, 0 not */
    void (*sem_release)(void *ctx, struct gfx_semaphore *sem);

    /* tasks, signals, messages, interrupts */
    void *(*find_task)(void *ctx);                    /* FindTask(0) */
    uint32_t (*wait)(void *ctx, uint32_t sigs);
    void (*signal)(void *ctx, void *task, uint32_t sigs);
    int32_t (*alloc_signal)(void *ctx, int32_t num);  /* -1: any; answers -1 when none */
    void (*free_signal)(void *ctx, int32_t num);
    void (*reply_msg)(void *ctx, void *msg);
    void (*add_int_server)(void *ctx, int32_t intnum, struct gfx_interrupt *is);
    void (*rem_int_server)(void *ctx, int32_t intnum, struct gfx_interrupt *is);

    /* the custom chips: offset from 0xDFF000 (0x002 VPOSR, 0x096 DMACON,
     * 0x180 COLOR00, ...). The tests give a simulated Agnus/Denise/Lisa,
     * blitter and copper here. */
    uint16_t (*chip_read)(void *ctx, uint32_t offset);
    void (*chip_write)(void *ctx, uint32_t offset, uint16_t value);

    /* utility.library's tag calls (NextTagItem, FindTagItem,
     * GetTagData, CloneTagItems, FreeTagItems). */
    struct gfx_tagitem *(*next_tag)(void *ctx, const struct gfx_tagitem **state);
    struct gfx_tagitem *(*find_tag)(void *ctx, uint32_t tag, const struct gfx_tagitem *tags);
    uint32_t (*get_tag_data)(void *ctx, uint32_t tag, uint32_t def, const struct gfx_tagitem *tags);
    struct gfx_tagitem *(*clone_tags)(void *ctx, const struct gfx_tagitem *tags);
    void (*free_tags)(void *ctx, struct gfx_tagitem *tags);

    /* debugging output; may be 0 */
    void (*log)(void *ctx, const char *s);
};

/* Each area's private state, by index into gfx_private.area. */
enum { GFX_AREA_FACE = 0, GFX_AREA_DISPLAY = 1, GFX_AREA_TEXT = 2, GFX_AREA_GELS = 3, GFX_AREAS = 4 };

/* After the public GfxBase, at a fixed offset that leaves room for what
 * later NDKs publish (ABI test: sizeof(struct GfxBase) on OS 3.2). */
#define GFX_PRIVATE_OFFSET 640u
struct gfx_private {
    struct gfx_env env;
    void *area[GFX_AREAS];      /* each area allocates and owns its own */
    uint32_t reserved[8];
};
#define GFX_BASE_POSSIZE (GFX_PRIVATE_OFFSET + (uint32_t)sizeof(struct gfx_private))

#if defined(__m68k__) || defined(__mc68000__) || defined(mc68000)
GFX_STATIC_ASSERT(sizeof(struct gfx_base) <= GFX_PRIVATE_OFFSET, "GfxBase fits before the private part");
#endif

static inline struct gfx_private *gfx_private_of(void *gfxbase)
{
    return (struct gfx_private *)((char *)gfxbase + GFX_PRIVATE_OFFSET);
}
static inline struct gfx_env *gfx_env_of(void *gfxbase)
{
    return &gfx_private_of(gfxbase)->env;
}

/* ---- What each area gives the library (OWNERS.md) --------------------
 *
 * Init and exit, called by the face's lib_init in the order face, display,
 * text, gels once the env and the public GfxBase lists are ready (exit in
 * the reverse order, the side-by-side build only). Answer 1, or 0 to fail
 * the library's init. */
int gfx_init_face(struct gfx_env *env);
int gfx_init_display(struct gfx_env *env);
int gfx_init_text(struct gfx_env *env);
int gfx_init_gels(struct gfx_env *env);
void gfx_exit_face(struct gfx_env *env);
void gfx_exit_display(struct gfx_env *env);
void gfx_exit_text(struct gfx_env *env);
void gfx_exit_gels(struct gfx_env *env);

/* Hooks between areas, plain C. A missing one gets gen_stubs.py's stub,
 * which does what the comment says. */

/* display: vp's ColorMap entries first .. first+count-1 changed (SetRGB4,
 * SetRGB32, LoadRGB4, LoadRGB32 after writing the ColorMap); load them into
 * the hardware or the copper lists if vp is shown. Stub: nothing. */
void gfx_hook_vp_colors(struct gfx_env *env, void *vp, uint32_t first, uint32_t count);

/* text: SetFont(rp, font) as text.library's SetFont does it (InitRastPort
 * calls it with GfxBase->DefaultFont). Stub: rp->Font, TxHeight, TxWidth,
 * TxBaseline from the TextFont's tf_YSize, tf_XSize, tf_Baseline. */
void gfx_hook_set_font(struct gfx_env *env, struct gfx_rastport *rp, void *font);

/* gels: BltBitMap for what the face's CPU copy does not take (a bitmap not
 * standard planar, or the blitter wanted). Answer the planes drawn, or -1
 * if it does not take it either. Stub: -1. */
int32_t gfx_hook_blt_bitmap(struct gfx_env *env, struct gfx_bitmap *src, int32_t sx, int32_t sy,
                            struct gfx_bitmap *dst, int32_t dx, int32_t dy, int32_t w, int32_t h,
                            uint32_t minterm, uint32_t mask, uint8_t *temp);

/* gels: wait for the blitter to finish (WaitBlit) before the CPU touches
 * chip memory the blitter may be writing. Stub: nothing. */
void gfx_hook_wait_blit(struct gfx_env *env);

/* ---- The register entries --------------------------------------------
 *
 * Every LVO of graphics.lvo is entered at gfx_lvo_<Call>, defined by its
 * owner in a C file built by the Amiga compiler, taking the fd's arguments
 * in the fd's registers (check_regs.py compares) and A6 = GfxBase last:
 *
 *     LONG gfx_lvo_ReadPixel(GFX_REG(a1, struct RastPort *rp), GFX_REG(d0, LONG x),
 *                            GFX_REG(d1, LONG y), GFX_REG(a6, struct GfxBase *gb))
 *     { return gt_or_whatever(gfx_env_of(gb), rp, x, y); }
 *
 * The private LVOs are gfx_lvo_private_<n> (n the negative offset, e.g.
 * gfx_lvo_private_642). */
#if defined(__amigaos__) || defined(AMIGA)
#define GFX_REG(r, decl) register decl __asm(#r)
#endif

#ifdef __cplusplus
}
#endif
#endif
