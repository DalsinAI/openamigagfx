/* SPDX-License-Identifier: MIT */
/* OpenGfx display: graphics.library's View, ViewPort, copper, display
 * database, monitor, beam and sprite calls in plain C arguments.
 * Copyright (c) 2026 Dalsin Limited. MIT.
 *
 * The lean C++ core (display_*.cpp) does the work; display_lvo.c takes
 * each call's registers (gfx_lvo_<Call>) and comes here with the env
 * (gfx_env.h). Every pointer is the NDK structure of the same name
 * (display_types.h gives the layouts). The tests call these directly on a
 * simulated Agnus, Denise/Lisa and copper (tests/m68k/test_gfx_display.sh). */
#ifndef OPENGFX_DISPLAY_H
#define OPENGFX_DISPLAY_H

#include "gfx_env.h"
#include "display_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Once from gfx_init_display: chip revision, monitors, copinit, the
 * sprite table; answers 1, or 0 out of memory. With own_hw 0 (the
 * side-by-side opengfx.library, next to OS 3.2's graphics.library) every
 * call works on memory as usual but no custom chip register is written:
 * the display stays the OS's. gd_exit gives it all back (side-by-side
 * build only). */
int gd_init(struct gfx_env *env, int own_hw);
void gd_exit(struct gfx_env *env);
/* The vertical blank's work, from the vbsrv interrupt server: the field's
 * copper list for an interlaced View, ChangeVPBitMap's messages, WaitTOF's
 * waiters, VBCounter. */
void gd_vblank(struct gfx_env *env);

/* Views, ViewPorts, copper */
void gd_InitView(struct gfx_env *env, struct gd_view *view);
void gd_InitVPort(struct gfx_env *env, struct gd_viewport *vp);
uint32_t gd_MakeVPort(struct gfx_env *env, struct gd_view *view, struct gd_viewport *vp);
uint32_t gd_MrgCop(struct gfx_env *env, struct gd_view *view);
void gd_LoadView(struct gfx_env *env, struct gd_view *view);
void gd_FreeVPortCopLists(struct gfx_env *env, struct gd_viewport *vp);
void gd_FreeCopList(struct gfx_env *env, struct gd_coplist *cl);
void gd_FreeCprList(struct gfx_env *env, struct gd_cprlist *cpr);
void gd_ScrollVPort(struct gfx_env *env, struct gd_viewport *vp);
struct gd_coplist *gd_UCopperListInit(struct gfx_env *env, struct gd_ucoplist *ucl, int32_t n);
void gd_CWait(struct gfx_env *env, struct gd_ucoplist *ucl, int32_t v, int32_t h);
void gd_CMove(struct gfx_env *env, struct gd_ucoplist *ucl, void *reg, int32_t value);
void gd_CBump(struct gfx_env *env, struct gd_ucoplist *ucl);
void gd_ChangeVPBitMap(struct gfx_env *env, struct gd_viewport *vp, struct gfx_bitmap *bm, struct gd_dbufinfo *dbi);
struct gd_dbufinfo *gd_AllocDBufInfo(struct gfx_env *env, struct gd_viewport *vp);
void gd_FreeDBufInfo(struct gfx_env *env, struct gd_dbufinfo *dbi);
uint16_t gd_CalcIVG(struct gfx_env *env, struct gd_view *view, struct gd_viewport *vp);
/* gfx-face's SetRGB4, SetRGB32, LoadRGB4 and LoadRGB32 after they wrote vp's ColorMap */
void gfx_hook_vp_colors(struct gfx_env *env, void *vp, uint32_t first, uint32_t count);

/* Extended nodes, monitors, the display database */
struct gd_extnode *gd_GfxNew(struct gfx_env *env, uint32_t type);
void gd_GfxFree(struct gfx_env *env, struct gd_extnode *node);
void gd_GfxAssociate(struct gfx_env *env, const void *key, struct gd_extnode *node);
struct gd_extnode *gd_GfxLookUp(struct gfx_env *env, const void *key);
uint32_t gd_VideoControl(struct gfx_env *env, struct gfx_colormap *cm, struct gfx_tagitem *tags);
struct gd_monitorspec *gd_OpenMonitor(struct gfx_env *env, const char *name, uint32_t displayid);
int32_t gd_CloseMonitor(struct gfx_env *env, struct gd_monitorspec *ms);
void *gd_FindDisplayInfo(struct gfx_env *env, uint32_t id);
uint32_t gd_NextDisplayInfo(struct gfx_env *env, uint32_t id);
uint32_t gd_GetDisplayInfoData(struct gfx_env *env, void *handle, void *buf, uint32_t size, uint32_t tagid,
                               uint32_t id);
uint32_t gd_GetVPModeID(struct gfx_env *env, const struct gd_viewport *vp);
uint32_t gd_ModeNotAvailable(struct gfx_env *env, uint32_t id);
uint32_t gd_BestModeIDA(struct gfx_env *env, const struct gfx_tagitem *tags);
uint32_t gd_CoerceMode(struct gfx_env *env, struct gd_viewport *vp, uint32_t monitorid, uint32_t flags);
uint32_t gd_SetChipRev(struct gfx_env *env, uint32_t want);

/* The beam */
int32_t gd_VBeamPos(struct gfx_env *env);
void gd_WaitTOF(struct gfx_env *env);
void gd_WaitBOVP(struct gfx_env *env, struct gd_viewport *vp);

/* Sprites */
int16_t gd_GetSprite(struct gfx_env *env, struct gd_simplesprite *s, int32_t num);
void gd_FreeSprite(struct gfx_env *env, int32_t num);
void gd_ChangeSprite(struct gfx_env *env, struct gd_viewport *vp, struct gd_simplesprite *s, uint16_t *data);
void gd_MoveSprite(struct gfx_env *env, struct gd_viewport *vp, struct gd_simplesprite *s, int32_t x, int32_t y);
struct gd_extsprite *gd_AllocSpriteDataA(struct gfx_env *env, const struct gfx_bitmap *bm,
                                         const struct gfx_tagitem *tags);
void gd_FreeSpriteData(struct gfx_env *env, struct gd_extsprite *es);
int32_t gd_GetExtSpriteA(struct gfx_env *env, struct gd_extsprite *es, const struct gfx_tagitem *tags);
int32_t gd_ChangeExtSpriteA(struct gfx_env *env, struct gd_viewport *vp, struct gd_extsprite *oldsp,
                            struct gd_extsprite *newsp, const struct gfx_tagitem *tags);

#ifdef __cplusplus
}
#endif
#endif
