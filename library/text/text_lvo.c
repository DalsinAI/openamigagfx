/* SPDX-License-Identifier: MIT */
/* OpenGfx text: graphics.library's text LVOs, taking their registers
 * (graphics.lvo, library/graphics/OWNERS.md) and coming to the core
 * (text_core.h). Copyright (c) 2026 Dalsin Limited. MIT.
 *
 * Built by the Amiga compiler with the NDK 3.2 headers, -I library/graphics
 * (gfx_env.h) and -I library/core (gfx_rp.h). gfx_init_text makes the
 * text's env from the library's gfx_env and keeps it, with the ROM fonts,
 * in gfx_private_of(GfxBase)->area[GFX_AREA_TEXT] (the library has no
 * .data or .bss). The drawing is library/core's: Text for one-colour fonts
 * drawn left to right, WritePixel and RectFill for the rest and ClearEOL,
 * each with the layer locked as LockLayerRom does. Where the core does not
 * draw a RastPort (a super bitmap layer, a chunky bitmap: ogfx_rp_takes)
 * nothing is drawn yet (README, "Not yet"). */
#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/clip.h>
#include <graphics/layers.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <utility/tagitem.h>

#include "gfx_env.h"
#include "gfx_rp.h"
#include "text_core.h"

#ifndef GFX_REG
#define GFX_REG(r, decl) decl
#endif

struct GfxBase;

struct gt_state {
    struct gt_env env;
    struct gfx_env *genv;
    struct TextFont *topaz8, *topaz9;
};

static struct gt_state *state_of(struct gfx_env *e)
{
    return (struct gt_state *)gfx_private_of(e->base)->area[GFX_AREA_TEXT];
}

static const struct gt_env *env_of(struct GfxBase *gb)
{
    return &state_of(gfx_env_of(gb))->env;
}

/* ---- the env -------------------------------------------------------- */

static void t_forbid(void *ctx)
{
    struct gfx_env *e = ctx;
    e->forbid(e->ctx);
}
static void t_permit(void *ctx)
{
    struct gfx_env *e = ctx;
    e->permit(e->ctx);
}
static void *t_alloc(void *ctx, uint32_t size)
{
    struct gfx_env *e = ctx;
    return e->alloc(e->ctx, size, GFX_MEMF_PUBLIC | GFX_MEMF_CLEAR);
}
static void t_free(void *ctx, void *p, uint32_t size)
{
    struct gfx_env *e = ctx;
    e->free(e->ctx, p, size);
}
static struct TagItem *t_clone_tags(void *ctx, const struct TagItem *tags)
{
    struct gfx_env *e = ctx;
    return (struct TagItem *)e->clone_tags(e->ctx, (const struct gfx_tagitem *)tags);
}
static void t_free_tags(void *ctx, struct TagItem *tags)
{
    struct gfx_env *e = ctx;
    e->free_tags(e->ctx, (struct gfx_tagitem *)tags);
}

static int t_mono_text(void *ctx, struct RastPort *rp, const uint8_t *s, uint32_t n)
{
    (void)ctx;
    return ogfx_rp_Text(rp, (CONST_STRPTR)s, n);
}

static void t_write_pixel(void *ctx, struct RastPort *rp, int32_t x, int32_t y, uint32_t pen, int op)
{
    BYTE fg = rp->FgPen, mode = rp->DrawMode;
    UBYTE mask = rp->Mask;
    (void)ctx;
    if (!ogfx_rp_takes(rp, 0)) return;
    if (op) {
        rp->DrawMode = COMPLEMENT;
        rp->Mask = (UBYTE)(mask & pen);
    } else {
        rp->DrawMode = JAM1;
        rp->FgPen = (BYTE)pen;
    }
    ogfx_rp_WritePixel(rp, x, y);
    rp->FgPen = fg;
    rp->DrawMode = mode;
    rp->Mask = mask;
}

static void t_fill(void *ctx, struct RastPort *rp, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t pen)
{
    BYTE fg = rp->FgPen, mode = rp->DrawMode, sz = rp->AreaPtSz;
    UWORD *pat = rp->AreaPtrn;
    (void)ctx;
    if (!ogfx_rp_takes(rp, 0)) return;
    rp->FgPen = (BYTE)pen;
    rp->DrawMode = JAM1;
    rp->AreaPtrn = 0;
    rp->AreaPtSz = 0;
    ogfx_rp_RectFill(rp, x0, y0, x1, y1);
    rp->FgPen = fg;
    rp->DrawMode = mode;
    rp->AreaPtrn = pat;
    rp->AreaPtSz = sz;
}

/* The layer's size, or the bitmap's (ABI test: a super bitmap layer's). */
static void t_rp_size(void *ctx, struct RastPort *rp, int32_t *w, int32_t *h)
{
    (void)ctx;
    if (rp->Layer) {
        *w = rp->Layer->bounds.MaxX - rp->Layer->bounds.MinX + 1;
        *h = rp->Layer->bounds.MaxY - rp->Layer->bounds.MinY + 1;
    } else if (rp->BitMap) {
        *w = (int32_t)rp->BitMap->BytesPerRow * 8;
        *h = rp->BitMap->Rows;
    } else
        *w = *h = 0;
}

/* LockLayerRom and UnlockLayerRom around a call that draws or reads a
 * layered RastPort. */
static void lock(struct GfxBase *gb, struct RastPort *rp)
{
    struct gfx_env *e = gfx_env_of(gb);
    if (rp->Layer) e->sem_obtain(e->ctx, (struct gfx_semaphore *)&rp->Layer->Lock);
}
static void unlock(struct GfxBase *gb, struct RastPort *rp)
{
    struct gfx_env *e = gfx_env_of(gb);
    if (rp->Layer) e->sem_release(e->ctx, (struct gfx_semaphore *)&rp->Layer->Lock);
}

/* ---- init ------------------------------------------------------------ */

int gfx_init_text(struct gfx_env *e)
{
    struct gt_state *st = e->alloc(e->ctx, sizeof *st, GFX_MEMF_PUBLIC | GFX_MEMF_CLEAR);
    struct gfx_base *base = e->base;
    if (!st) return 0;
    st->genv = e;
    st->env.ctx = e;
    st->env.fonts = (struct List *)&base->textfonts;
    st->env.forbid = t_forbid;
    st->env.permit = t_permit;
    st->env.alloc = t_alloc;
    st->env.free = t_free;
    st->env.clone_tags = t_clone_tags;
    st->env.free_tags = t_free_tags;
    st->env.mono_text = t_mono_text;
    st->env.write_pixel = t_write_pixel;
    st->env.fill = t_fill;
    st->env.rp_size = t_rp_size;
    st->topaz8 = gt_init_rom_fonts(&st->env, &st->topaz9);
    if (!st->topaz8) {
        e->free(e->ctx, st, sizeof *st);
        return 0;
    }
    gfx_private_of(base)->area[GFX_AREA_TEXT] = st;
    base->defaultfont = st->topaz8;
    return 1;
}

void gfx_exit_text(struct gfx_env *e)
{
    struct gt_state *st = state_of(e);
    if (!st) return;
    if (e->base->defaultfont == st->topaz8) e->base->defaultfont = 0;
    gt_exit_rom_fonts(&st->env, st->topaz8, st->topaz9);
    gfx_private_of(e->base)->area[GFX_AREA_TEXT] = 0;
    e->free(e->ctx, st, sizeof *st);
}

/* InitRastPort's SetFont; before gfx_init_text, the fields alone. */
void gfx_hook_set_font(struct gfx_env *e, struct gfx_rastport *rp, void *font)
{
    struct gt_state *st = state_of(e);
    struct TextFont *tf = font;
    if (st) {
        gt_SetFont(&st->env, (struct RastPort *)rp, tf);
        return;
    }
    if (!tf) return;
    rp->font = tf;
    rp->txheight = tf->tf_YSize;
    rp->txwidth = tf->tf_XSize;
    rp->txbaseline = tf->tf_Baseline;
}

/* ---- the LVOs, in graphics.lvo's order ----------------------------- */

void gfx_lvo_ClearEOL(GFX_REG(a1, struct RastPort *rp), GFX_REG(a6, struct GfxBase *gb))
{
    lock(gb, rp);
    gt_ClearEOL(env_of(gb), rp);
    unlock(gb, rp);
}

void gfx_lvo_ClearScreen(GFX_REG(a1, struct RastPort *rp), GFX_REG(a6, struct GfxBase *gb))
{
    lock(gb, rp);
    gt_ClearScreen(env_of(gb), rp);
    unlock(gb, rp);
}

WORD gfx_lvo_TextLength(GFX_REG(a1, struct RastPort *rp), GFX_REG(a0, CONST_STRPTR string),
                        GFX_REG(d0, ULONG count), GFX_REG(a6, struct GfxBase *gb))
{
    return gt_TextLength(env_of(gb), rp, (const uint8_t *)string, count);
}

LONG gfx_lvo_Text(GFX_REG(a1, struct RastPort *rp), GFX_REG(a0, CONST_STRPTR string), GFX_REG(d0, ULONG count),
                  GFX_REG(a6, struct GfxBase *gb))
{
    LONG r;
    lock(gb, rp);
    r = gt_Text(env_of(gb), rp, (const uint8_t *)string, count);
    unlock(gb, rp);
    return r;
}

LONG gfx_lvo_SetFont(GFX_REG(a1, struct RastPort *rp), GFX_REG(a0, struct TextFont *textFont),
                     GFX_REG(a6, struct GfxBase *gb))
{
    return gt_SetFont(env_of(gb), rp, textFont);
}

struct TextFont *gfx_lvo_OpenFont(GFX_REG(a0, struct TextAttr *textAttr), GFX_REG(a6, struct GfxBase *gb))
{
    return gt_OpenFont(env_of(gb), textAttr);
}

void gfx_lvo_CloseFont(GFX_REG(a1, struct TextFont *textFont), GFX_REG(a6, struct GfxBase *gb))
{
    gt_CloseFont(env_of(gb), textFont);
}

ULONG gfx_lvo_AskSoftStyle(GFX_REG(a1, struct RastPort *rp), GFX_REG(a6, struct GfxBase *gb))
{
    return gt_AskSoftStyle(env_of(gb), rp);
}

ULONG gfx_lvo_SetSoftStyle(GFX_REG(a1, struct RastPort *rp), GFX_REG(d0, ULONG style), GFX_REG(d1, ULONG enable),
                           GFX_REG(a6, struct GfxBase *gb))
{
    return gt_SetSoftStyle(env_of(gb), rp, style, enable);
}

void gfx_lvo_AskFont(GFX_REG(a1, struct RastPort *rp), GFX_REG(a0, struct TextAttr *textAttr),
                     GFX_REG(a6, struct GfxBase *gb))
{
    gt_AskFont(env_of(gb), rp, textAttr);
}

void gfx_lvo_AddFont(GFX_REG(a1, struct TextFont *textFont), GFX_REG(a6, struct GfxBase *gb))
{
    gt_AddFont(env_of(gb), textFont);
}

LONG gfx_lvo_RemFont(GFX_REG(a1, struct TextFont *textFont), GFX_REG(a6, struct GfxBase *gb))
{
    return gt_RemFont(env_of(gb), textFont);
}

void gfx_lvo_TextExtent(GFX_REG(a1, struct RastPort *rp), GFX_REG(a0, CONST_STRPTR string), GFX_REG(d0, ULONG count),
                        GFX_REG(a2, struct TextExtent *textExtent), GFX_REG(a6, struct GfxBase *gb))
{
    gt_TextExtent(env_of(gb), rp, (const uint8_t *)string, count, textExtent);
}

ULONG gfx_lvo_TextFit(GFX_REG(a1, struct RastPort *rp), GFX_REG(a0, CONST_STRPTR string), GFX_REG(d0, ULONG strLen),
                      GFX_REG(a2, struct TextExtent *textExtent),
                      GFX_REG(a3, const struct TextExtent *constrainingExtent), GFX_REG(d1, LONG strDirection),
                      GFX_REG(d2, ULONG constrainingBitWidth), GFX_REG(d3, ULONG constrainingBitHeight),
                      GFX_REG(a6, struct GfxBase *gb))
{
    return gt_TextFit(env_of(gb), rp, (const uint8_t *)string, strLen, textExtent, constrainingExtent, strDirection,
                      constrainingBitWidth, constrainingBitHeight);
}

void gfx_lvo_FontExtent(GFX_REG(a0, const struct TextFont *font), GFX_REG(a1, struct TextExtent *fontExtent),
                        GFX_REG(a6, struct GfxBase *gb))
{
    gt_FontExtent(env_of(gb), font, fontExtent);
}

WORD gfx_lvo_WeighTAMatch(GFX_REG(a0, const struct TextAttr *reqTextAttr),
                          GFX_REG(a1, const struct TextAttr *targetTextAttr),
                          GFX_REG(a2, const struct TagItem *targetTags), GFX_REG(a6, struct GfxBase *gb))
{
    return (WORD)gt_WeighTAMatch(env_of(gb), reqTextAttr, targetTextAttr, targetTags);
}

ULONG gfx_lvo_ExtendFont(GFX_REG(a0, struct TextFont *font), GFX_REG(a1, const struct TagItem *fontTags),
                         GFX_REG(a6, struct GfxBase *gb))
{
    return (ULONG)gt_ExtendFont(env_of(gb), font, fontTags);
}

void gfx_lvo_StripFont(GFX_REG(a0, struct TextFont *font), GFX_REG(a6, struct GfxBase *gb))
{
    gt_StripFont(env_of(gb), font);
}
