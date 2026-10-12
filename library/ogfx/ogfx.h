/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenGfx inside opengpu.library: what opengpu_lib.c sees of it.
 *
 * Everything OpenGfx keeps lives in struct ogfx_state, which sits in
 * opengpu.library's base: the library has no writable globals, so the same
 * build runs from LIBS: or from ROM (one-library layout, section 6). The
 * patches reach their state through a small entry each, also in the base
 * (struct ogfx_state's tramp[]): graphics.library calls a patch with its own
 * base in A6, so the entry puts the state in A4 and calls the C. */
#ifndef OPENGPU_OGFX_H
#define OPENGPU_OGFX_H

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <graphics/gfxbase.h>
#include "../../include/opengpu/gfx.h"

#define OGFX_REG(r, decl) register decl __asm(#r)

/* graphics.library's calls OpenGfx owns, in this order everywhere: 1.1's
 * eight, then the fourteen of 0.8 (gfx.h). */
enum {
    OGFX_P_TEXT, OGFX_P_TEXTLENGTH, OGFX_P_TEXTEXTENT, OGFX_P_TEXTFIT,
    OGFX_P_RECTFILL, OGFX_P_BLTBITMAP, OGFX_P_BLTTEMPLATE, OGFX_P_SCROLLRASTER,
    OGFX_P_BLTPATTERN, OGFX_P_SETRAST, OGFX_P_DRAW, OGFX_P_POLYDRAW,
    OGFX_P_WRITEPIXEL, OGFX_P_READPIXEL, OGFX_P_BLTBMRP, OGFX_P_BLTMASKBMRP,
    OGFX_P_CLIPBLIT, OGFX_P_WCP, OGFX_P_WPA8, OGFX_P_WPL8, OGFX_P_RPL8, OGFX_P_RPA8,
    OGFX_P_COUNT
};

#include "ogfx_tramp.h"

struct ogfx_state {
    struct ExecBase *sys;              /* exec, from address 4 at init */
    struct GfxBase *gfx;               /* graphics.library, opened on first need */
    UBYTE patched;
    UBYTE enabled;
    UBYTE have_provider;
    UBYTE amigachrome;                 /* Dalsin boards: the leaves run as host code */
    struct OGFXProviderAll provider;   /* a v1 provider's calls are provider.v1's */
    APTR old[OGFX_P_COUNT];            /* graphics.library's vectors before the patches */
    UWORD tramp[OGFX_P_COUNT][OGFX_TRAMP_WORDS]; /* each patch's entry, written by InstallPatches */
    /* 0.7: the look (gfx.h). Kept after the entries, so old[] stays just
     * before them as in 0.6 (OpenGadTools' GfxPatches test reads it there
     * to follow a vector's chain). */
    volatile UBYTE have_look;
    UBYTE pad_look;
    volatile UWORD look_busy;          /* calls inside the look now */
    struct OGFXLookV1 look;            /* kept when taken out, for calls still inside it */
};

/* Where the state sits in opengpu.library's base (opengpu_lib.c). */
extern const ULONG ogpu_ogfx_at;

void ogfx_init(struct ogfx_state *st, struct ExecBase *sys);
/* 0 while the patches are in: graphics.library points at this code, so the
 * library must stay. */
int ogfx_may_expunge(struct ogfx_state *st);
void ogfx_expunge(struct ogfx_state *st);

/* The LVOs (A6 is opengpu.library's base). */
ULONG OGFX_Version(OGFX_REG(a6, struct Library *base));
LONG OGFX_InstallPatches(OGFX_REG(a6, struct Library *base));
LONG OGFX_SetEnabled(OGFX_REG(d0, ULONG enabled), OGFX_REG(a6, struct Library *base));
ULONG OGFX_Status(OGFX_REG(a6, struct Library *base));
LONG OGFX_RegisterProvider(OGFX_REG(a0, struct OGFXProviderV1 *provider), OGFX_REG(a6, struct Library *base));
LONG OGFX_UnregisterProvider(OGFX_REG(a0, APTR owner), OGFX_REG(a6, struct Library *base));
LONG OGFX_RegisterLook(OGFX_REG(a0, struct OGFXLookV1 *look), OGFX_REG(a6, struct Library *base));
LONG OGFX_UnregisterLook(OGFX_REG(a0, APTR owner), OGFX_REG(a6, struct Library *base));
LONG OGFX_Text(OGFX_REG(a1, struct RastPort *rp), OGFX_REG(a0, STRPTR text), OGFX_REG(d0, ULONG length),
               OGFX_REG(a6, struct Library *base));
WORD OGFX_TextLength(OGFX_REG(a1, struct RastPort *rp), OGFX_REG(a0, STRPTR text), OGFX_REG(d0, ULONG length),
                     OGFX_REG(a6, struct Library *base));
void OGFX_TextExtent(OGFX_REG(a1, struct RastPort *rp), OGFX_REG(a0, STRPTR text), OGFX_REG(d0, ULONG length),
                     OGFX_REG(a2, struct TextExtent *extent), OGFX_REG(a6, struct Library *base));
ULONG OGFX_TextFit(OGFX_REG(a1, struct RastPort *rp), OGFX_REG(a0, STRPTR text), OGFX_REG(d0, ULONG length),
                   OGFX_REG(a2, struct TextExtent *extent), OGFX_REG(a3, struct TextExtent *constraining),
                   OGFX_REG(d1, LONG direction), OGFX_REG(d2, ULONG bit_width), OGFX_REG(d3, ULONG bit_height),
                   OGFX_REG(a6, struct Library *base));
void OGFX_RectFill(OGFX_REG(a1, struct RastPort *rp), OGFX_REG(d0, LONG x0), OGFX_REG(d1, LONG y0),
                   OGFX_REG(d2, LONG x1), OGFX_REG(d3, LONG y1), OGFX_REG(a6, struct Library *base));
LONG OGFX_BltBitMap(OGFX_REG(a0, struct BitMap *src), OGFX_REG(d0, LONG sx), OGFX_REG(d1, LONG sy),
                    OGFX_REG(a1, struct BitMap *dst), OGFX_REG(d2, LONG dx), OGFX_REG(d3, LONG dy),
                    OGFX_REG(d4, LONG width), OGFX_REG(d5, LONG height), OGFX_REG(d6, ULONG minterm),
                    OGFX_REG(d7, ULONG mask), OGFX_REG(a2, PLANEPTR temp), OGFX_REG(a6, struct Library *base));
void OGFX_BltTemplate(OGFX_REG(a0, PLANEPTR source), OGFX_REG(d0, LONG sx), OGFX_REG(d1, LONG modulo),
                      OGFX_REG(a1, struct RastPort *rp), OGFX_REG(d2, LONG dx), OGFX_REG(d3, LONG dy),
                      OGFX_REG(d4, LONG width), OGFX_REG(d5, LONG height), OGFX_REG(a6, struct Library *base));
void OGFX_ScrollRaster(OGFX_REG(a1, struct RastPort *rp), OGFX_REG(d0, LONG dx), OGFX_REG(d1, LONG dy),
                       OGFX_REG(d2, LONG x0), OGFX_REG(d3, LONG y0), OGFX_REG(d4, LONG x1), OGFX_REG(d5, LONG y1),
                       OGFX_REG(a6, struct Library *base));

#endif
