/* SPDX-License-Identifier: MIT */
/* OpenGfx text: graphics.library's font and text calls, as a lean C++
 * core with no Amiga headers. Copyright (c) 2026 Dalsin Limited. MIT.
 *
 * C and C++ both include this. The structures are the NDK's by their own
 * tags (struct RastPort, TextFont, TextAttr, TextExtent, TagItem): the
 * shell (text_lvo.c) sees them through the NDK 3.2 headers and the core
 * through text_types.h, which has the same layouts with static_asserts.
 *
 * The core keeps nothing in globals. Everything outside it comes through
 * struct gt_env: the font list (GfxBase->TextFonts), Forbid/Permit, memory,
 * utility's tag cloning and the drawing (text_lvo.c fills it from
 * gfx_env.h and library/core's ogfx_rp_* calls; the tests fill it with a
 * simulated machine). */
#ifndef OPENGFX_TEXT_CORE_H
#define OPENGFX_TEXT_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct RastPort;
struct TextFont;
struct TextAttr;
struct TextExtent;
struct TagItem;
struct List;

struct gt_env {
    void *ctx;
    struct List *fonts;                 /* &GfxBase->TextFonts, a List */
    void (*forbid)(void *ctx);
    void (*permit)(void *ctx);
    void *(*alloc)(void *ctx, uint32_t size); /* public, cleared; 0 when out of memory */
    void (*free)(void *ctx, void *p, uint32_t size);
    /* utility's CloneTagItems and FreeTagItems; clone_tags may answer 0 */
    struct TagItem *(*clone_tags)(void *ctx, const struct TagItem *tags);
    void (*free_tags)(void *ctx, struct TagItem *tags);

    /* Drawing, in the RastPort's own coordinates, through its Mask and
     * layer, for the calls graphics.library's own drawing does.
     * mono_text: library/core's Text for a one-colour font drawn left to
     * right (moving cp_x as Text does); answers 0, drawing nothing and
     * moving nothing, when it does not take the call, and the core then
     * draws the text itself through write_pixel. May be 0.
     * write_pixel: op 0 sets the pixel to pen; op 1 inverts the planes set
     * in pen (pen 0xFF: all of them, COMPLEMENT).
     * fill: a solid rectangle in pen, x0 <= x1 and y0 <= y1 (ClearEOL).
     * rp_size: the width and height the RastPort draws into (its layer's,
     * or its bitmap's). */
    int (*mono_text)(void *ctx, struct RastPort *rp, const uint8_t *s, uint32_t n);
    void (*write_pixel)(void *ctx, struct RastPort *rp, int32_t x, int32_t y, uint32_t pen, int op);
    void (*fill)(void *ctx, struct RastPort *rp, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t pen);
    void (*rp_size)(void *ctx, struct RastPort *rp, int32_t *w, int32_t *h);
};

/* The calls, graphics.library's arguments in the fd's order. */
void gt_ClearEOL(const struct gt_env *env, struct RastPort *rp);
void gt_ClearScreen(const struct gt_env *env, struct RastPort *rp);
int16_t gt_TextLength(const struct gt_env *env, struct RastPort *rp, const uint8_t *s, uint32_t n);
int32_t gt_Text(const struct gt_env *env, struct RastPort *rp, const uint8_t *s, uint32_t n);
int32_t gt_SetFont(const struct gt_env *env, struct RastPort *rp, struct TextFont *tf);
struct TextFont *gt_OpenFont(const struct gt_env *env, const struct TextAttr *ta);
void gt_CloseFont(const struct gt_env *env, struct TextFont *tf);
uint32_t gt_AskSoftStyle(const struct gt_env *env, struct RastPort *rp);
uint32_t gt_SetSoftStyle(const struct gt_env *env, struct RastPort *rp, uint32_t style, uint32_t enable);
void gt_AskFont(const struct gt_env *env, struct RastPort *rp, struct TextAttr *ta);
void gt_AddFont(const struct gt_env *env, struct TextFont *tf);
int32_t gt_RemFont(const struct gt_env *env, struct TextFont *tf);
void gt_TextExtent(const struct gt_env *env, struct RastPort *rp, const uint8_t *s, uint32_t n,
                   struct TextExtent *te);
uint32_t gt_TextFit(const struct gt_env *env, struct RastPort *rp, const uint8_t *s, uint32_t n,
                    struct TextExtent *te, const struct TextExtent *constraint, int32_t dir, uint32_t bitwidth,
                    uint32_t bitheight);
void gt_FontExtent(const struct gt_env *env, const struct TextFont *tf, struct TextExtent *te);
int32_t gt_WeighTAMatch(const struct gt_env *env, const struct TextAttr *req, const struct TextAttr *target,
                        const struct TagItem *targettags);
int32_t gt_ExtendFont(const struct gt_env *env, struct TextFont *tf, const struct TagItem *tags);
void gt_StripFont(const struct gt_env *env, struct TextFont *tf);

/* The ROM fonts, topaz 8 and topaz 9 (redrawn, topaz_data.cpp): each in
 * memory from env->alloc, its glyphs in the library's constant data, added
 * to the font list (topaz 8 first in it). Answers topaz 8, for
 * GfxBase->DefaultFont, or 0 when out of memory (nothing added). *t9 gets
 * topaz 9. */
struct TextFont *gt_init_rom_fonts(const struct gt_env *env, struct TextFont **t9);
/* Takes them out of the list and frees them (the side-by-side build's
 * expunge). */
void gt_exit_rom_fonts(const struct gt_env *env, struct TextFont *t8, struct TextFont *t9);

#ifdef __cplusplus
}
#endif
#endif
