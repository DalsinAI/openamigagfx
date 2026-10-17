/* SPDX-License-Identifier: MIT */
/* OpenGfx graphics.library: the display's LVOs in their registers, its
 * init and exit, and the vertical blank server. Copyright (c) 2026 AmigaChrome
 * Team. MIT.
 *
 * Each gfx_lvo_<Call> takes the fd's arguments in the registers
 * graphics.lvo lists (check_regs.py compares them), with A6 the library
 * base, and calls the core (display_*.cpp). Built by the Amiga compiler,
 * whose register parameters GFX_REG names; elsewhere GFX_REG is a plain
 * parameter, for the tests' compile check. OWNERS.md says which LVOs are
 * the display's. */
#include "display.h"

/* Arguments NDK 3.2's protos give as WORD, UWORD, BYTE, UBYTE or BOOL are
 * cast to that width where they are handed on: a caller built with those
 * inlines sets only the low part of the register (measured: OpenIntuition's
 * WhichLayer call left d0's upper word as it was). */
#ifndef GFX_REG
#define GFX_REG(r, decl) decl
#endif

#define ENV gfx_env_of(gb)

/* ---- init, exit, the vertical blank --------------------------------------
 *
 * GfxBase->vbsrv on INTB_VERTB: exec calls a server with A1 = is_Data (the
 * env) and wants D0 = 0 with Z set to go on down the chain. */
#if defined(__amigaos__) || defined(AMIGA)
#define GD_STR2(x) #x
#define GD_STR(x) GD_STR2(x)
#define GD_SYM(s) GD_STR(__USER_LABEL_PREFIX__) #s
void gd_vb_entry(void);
__asm__(".text\n\t.even\n"
        GD_SYM(gd_vb_entry) ":\n\t"
        "move.l a1,-(sp)\n\t"
        "jsr " GD_SYM(gd_vblank) "\n\t"
        "addq.l #4,sp\n\t"
        "moveq #0,d0\n\t"
        "rts");
#define GD_VB_CODE gd_vb_entry
#else
static void gd_vb_entry(void) {}
#define GD_VB_CODE gd_vb_entry
#endif

static const char gd_vb_name[] = "graphics.library vblank";

/* The display is ours only as graphics.library; opened next to OS 3.2's
 * as opengfx.library it must leave the custom chips alone. */
static int gd_is_system(const struct gfx_base *gb)
{
    static const char want[] = "graphics.library";
    const char *n = gb->libnode.node.name;
    unsigned i;
    if (!n) return 1;
    for (i = 0; i < sizeof want; i++)
        if (n[i] != want[i]) return 0;
    return 1;
}

int gfx_init_display(struct gfx_env *env)
{
    struct gfx_base *gb = env->base;
    if (!gd_init(env, gd_is_system(gb))) return 0;
    gb->vbsrv.node.type = 2;            /* NT_INTERRUPT */
    gb->vbsrv.node.pri = 10;
    gb->vbsrv.node.name = (char *)gd_vb_name;
    gb->vbsrv.data = env;
    gb->vbsrv.code = GD_VB_CODE;
    env->add_int_server(env->ctx, 5, &gb->vbsrv);   /* INTB_VERTB */
    return 1;
}

void gfx_exit_display(struct gfx_env *env)
{
    env->rem_int_server(env->ctx, 5, &env->base->vbsrv);
    gd_exit(env);
}

/* ---- the calls ------------------------------------------------------------ */

void gfx_lvo_InitVPort(GFX_REG(a0, struct gd_viewport *vp), GFX_REG(a6, struct gfx_base *gb))
{
    gd_InitVPort(ENV, vp);
}

uint32_t gfx_lvo_MrgCop(GFX_REG(a1, struct gd_view *view), GFX_REG(a6, struct gfx_base *gb))
{
    return gd_MrgCop(ENV, view);
}

uint32_t gfx_lvo_MakeVPort(GFX_REG(a0, struct gd_view *view), GFX_REG(a1, struct gd_viewport *vp),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gd_MakeVPort(ENV, view, vp);
}

void gfx_lvo_LoadView(GFX_REG(a1, struct gd_view *view), GFX_REG(a6, struct gfx_base *gb))
{
    gd_LoadView(ENV, view);
}

void gfx_lvo_WaitTOF(GFX_REG(a6, struct gfx_base *gb))
{
    gd_WaitTOF(ENV);
}

void gfx_lvo_InitView(GFX_REG(a1, struct gd_view *view), GFX_REG(a6, struct gfx_base *gb))
{
    gd_InitView(ENV, view);
}

void gfx_lvo_CBump(GFX_REG(a1, struct gd_ucoplist *ucl), GFX_REG(a6, struct gfx_base *gb))
{
    gd_CBump(ENV, ucl);
}

void gfx_lvo_CMove(GFX_REG(a1, struct gd_ucoplist *ucl), GFX_REG(d0, void *reg), GFX_REG(d1, int32_t value),
    GFX_REG(a6, struct gfx_base *gb))
{
    gd_CMove(ENV, ucl, reg, (int16_t)value);
}

void gfx_lvo_CWait(GFX_REG(a1, struct gd_ucoplist *ucl), GFX_REG(d0, int32_t v), GFX_REG(d1, int32_t h),
    GFX_REG(a6, struct gfx_base *gb))
{
    gd_CWait(ENV, ucl, (int16_t)v, (int16_t)h);
}

int32_t gfx_lvo_VBeamPos(GFX_REG(a6, struct gfx_base *gb))
{
    return gd_VBeamPos(ENV);
}

void gfx_lvo_WaitBOVP(GFX_REG(a0, struct gd_viewport *vp), GFX_REG(a6, struct gfx_base *gb))
{
    gd_WaitBOVP(ENV, vp);
}

int32_t gfx_lvo_GetSprite(GFX_REG(a0, struct gd_simplesprite *s), GFX_REG(d0, int32_t num),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gd_GetSprite(ENV, s, (int16_t)num);
}

void gfx_lvo_FreeSprite(GFX_REG(d0, int32_t num), GFX_REG(a6, struct gfx_base *gb))
{
    gd_FreeSprite(ENV, (int16_t)num);
}

void gfx_lvo_ChangeSprite(GFX_REG(a0, struct gd_viewport *vp), GFX_REG(a1, struct gd_simplesprite *s),
    GFX_REG(a2, uint16_t *data), GFX_REG(a6, struct gfx_base *gb))
{
    gd_ChangeSprite(ENV, vp, s, data);
}

void gfx_lvo_MoveSprite(GFX_REG(a0, struct gd_viewport *vp), GFX_REG(a1, struct gd_simplesprite *s),
    GFX_REG(d0, int32_t x), GFX_REG(d1, int32_t y), GFX_REG(a6, struct gfx_base *gb))
{
    gd_MoveSprite(ENV, vp, s, (int16_t)x, (int16_t)y);
}

void gfx_lvo_FreeVPortCopLists(GFX_REG(a0, struct gd_viewport *vp), GFX_REG(a6, struct gfx_base *gb))
{
    gd_FreeVPortCopLists(ENV, vp);
}

void gfx_lvo_FreeCopList(GFX_REG(a0, struct gd_coplist *cl), GFX_REG(a6, struct gfx_base *gb))
{
    gd_FreeCopList(ENV, cl);
}

void gfx_lvo_FreeCprList(GFX_REG(a0, struct gd_cprlist *cpr), GFX_REG(a6, struct gfx_base *gb))
{
    gd_FreeCprList(ENV, cpr);
}

void gfx_lvo_ScrollVPort(GFX_REG(a0, struct gd_viewport *vp), GFX_REG(a6, struct gfx_base *gb))
{
    gd_ScrollVPort(ENV, vp);
}

struct gd_coplist *gfx_lvo_UCopperListInit(GFX_REG(a0, struct gd_ucoplist *ucl), GFX_REG(d0, int32_t n),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gd_UCopperListInit(ENV, ucl, (int16_t)n);
}

struct gd_extnode *gfx_lvo_GfxNew(GFX_REG(d0, uint32_t type), GFX_REG(a6, struct gfx_base *gb))
{
    return gd_GfxNew(ENV, type);
}

void gfx_lvo_GfxFree(GFX_REG(a0, struct gd_extnode *node), GFX_REG(a6, struct gfx_base *gb))
{
    gd_GfxFree(ENV, node);
}

void gfx_lvo_GfxAssociate(GFX_REG(a0, const void *key), GFX_REG(a1, struct gd_extnode *node),
    GFX_REG(a6, struct gfx_base *gb))
{
    gd_GfxAssociate(ENV, key, node);
}

struct gd_extnode *gfx_lvo_GfxLookUp(GFX_REG(a0, const void *key), GFX_REG(a6, struct gfx_base *gb))
{
    return gd_GfxLookUp(ENV, key);
}

uint32_t gfx_lvo_VideoControl(GFX_REG(a0, struct gfx_colormap *cm), GFX_REG(a1, struct gfx_tagitem *tags),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gd_VideoControl(ENV, cm, tags);
}

struct gd_monitorspec *gfx_lvo_OpenMonitor(GFX_REG(a1, const char *name), GFX_REG(d0, uint32_t id),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gd_OpenMonitor(ENV, name, id);
}

int32_t gfx_lvo_CloseMonitor(GFX_REG(a0, struct gd_monitorspec *ms), GFX_REG(a6, struct gfx_base *gb))
{
    return gd_CloseMonitor(ENV, ms);
}

void *gfx_lvo_FindDisplayInfo(GFX_REG(d0, uint32_t id), GFX_REG(a6, struct gfx_base *gb))
{
    return gd_FindDisplayInfo(ENV, id);
}

uint32_t gfx_lvo_NextDisplayInfo(GFX_REG(d0, uint32_t id), GFX_REG(a6, struct gfx_base *gb))
{
    return gd_NextDisplayInfo(ENV, id);
}

uint32_t gfx_lvo_GetDisplayInfoData(GFX_REG(a0, void *handle), GFX_REG(a1, void *buf), GFX_REG(d0, uint32_t size),
    GFX_REG(d1, uint32_t tagid), GFX_REG(d2, uint32_t id), GFX_REG(a6, struct gfx_base *gb))
{
    return gd_GetDisplayInfoData(ENV, handle, buf, size, tagid, id);
}

uint32_t gfx_lvo_GetVPModeID(GFX_REG(a0, const struct gd_viewport *vp), GFX_REG(a6, struct gfx_base *gb))
{
    return gd_GetVPModeID(ENV, vp);
}

uint32_t gfx_lvo_ModeNotAvailable(GFX_REG(d0, uint32_t id), GFX_REG(a6, struct gfx_base *gb))
{
    return gd_ModeNotAvailable(ENV, id);
}

uint32_t gfx_lvo_CalcIVG(GFX_REG(a0, struct gd_view *view), GFX_REG(a1, struct gd_viewport *vp),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gd_CalcIVG(ENV, view, vp);
}

uint32_t gfx_lvo_SetChipRev(GFX_REG(d0, uint32_t want), GFX_REG(a6, struct gfx_base *gb))
{
    return gd_SetChipRev(ENV, want);
}

int32_t gfx_lvo_GetExtSpriteA(GFX_REG(a2, struct gd_extsprite *es), GFX_REG(a1, const struct gfx_tagitem *tags),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gd_GetExtSpriteA(ENV, es, tags);
}

uint32_t gfx_lvo_CoerceMode(GFX_REG(a0, struct gd_viewport *vp), GFX_REG(d0, uint32_t monitorid),
    GFX_REG(d1, uint32_t flags), GFX_REG(a6, struct gfx_base *gb))
{
    return gd_CoerceMode(ENV, vp, monitorid, flags);
}

void gfx_lvo_ChangeVPBitMap(GFX_REG(a0, struct gd_viewport *vp), GFX_REG(a1, struct gfx_bitmap *bm),
    GFX_REG(a2, struct gd_dbufinfo *dbi), GFX_REG(a6, struct gfx_base *gb))
{
    gd_ChangeVPBitMap(ENV, vp, bm, dbi);
}

struct gd_dbufinfo *gfx_lvo_AllocDBufInfo(GFX_REG(a0, struct gd_viewport *vp), GFX_REG(a6, struct gfx_base *gb))
{
    return gd_AllocDBufInfo(ENV, vp);
}

void gfx_lvo_FreeDBufInfo(GFX_REG(a1, struct gd_dbufinfo *dbi), GFX_REG(a6, struct gfx_base *gb))
{
    gd_FreeDBufInfo(ENV, dbi);
}

struct gd_extsprite *gfx_lvo_AllocSpriteDataA(GFX_REG(a2, const struct gfx_bitmap *bm),
    GFX_REG(a1, const struct gfx_tagitem *tags), GFX_REG(a6, struct gfx_base *gb))
{
    return gd_AllocSpriteDataA(ENV, bm, tags);
}

int32_t gfx_lvo_ChangeExtSpriteA(GFX_REG(a0, struct gd_viewport *vp), GFX_REG(a1, struct gd_extsprite *oldsp),
    GFX_REG(a2, struct gd_extsprite *newsp), GFX_REG(a3, const struct gfx_tagitem *tags),
    GFX_REG(a6, struct gfx_base *gb))
{
    return gd_ChangeExtSpriteA(ENV, vp, oldsp, newsp, tags);
}

void gfx_lvo_FreeSpriteData(GFX_REG(a2, struct gd_extsprite *es), GFX_REG(a6, struct gfx_base *gb))
{
    gd_FreeSpriteData(ENV, es);
}

uint32_t gfx_lvo_BestModeIDA(GFX_REG(a0, const struct gfx_tagitem *tags), GFX_REG(a6, struct gfx_base *gb))
{
    return gd_BestModeIDA(ENV, tags);
}
