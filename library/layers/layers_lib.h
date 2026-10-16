/* SPDX-License-Identifier: MIT */
/* OpenLayers: layers.library's calls in plain C arguments. Copyright (c)
 * 2026 AmigaChrome Team. MIT.
 *
 * layers_lib.c does the work of each call, through the core
 * (layers_core.h) and exec's and graphics.library's own calls; the library
 * shell (openlayers_lib.c) takes each call's registers and comes here.
 * The tests build layers_lib.c on the host and under qemu-m68k against a
 * pixel model of graphics.library (tests/m68k/test_layers_lib.sh). */
#ifndef OPENLAYERS_LIB_H
#define OPENLAYERS_LIB_H

#include <exec/types.h>
#include <exec/semaphores.h>
#include <graphics/clip.h>
#include <graphics/layers.h>
#include <graphics/rastport.h>
#include <graphics/regions.h>
#include <utility/hooks.h>

/* What a layer's backfill hook and DoHookClipRects' hook get in A1; A2
 * holds a RastPort with no layer whose BitMap is where to draw. Bounds is
 * in that bitmap; OffsetX and OffsetY are the layer's own coordinates of
 * Bounds' top left (the screen's, for the Layer_Info's hook). */
struct ol_hookmsg {
    struct Layer *Layer;
    struct Rectangle Bounds;
    LONG OffsetX;
    LONG OffsetY;
};

/* The shell (or the test) calls a hook as utility.library does: A0 the
 * hook, A2 obj, A1 msg. */
ULONG ol_call_hook(struct Hook *hook, APTR obj, APTR msg);

/* What the calls keep between them: graphics.library's base and each
 * Layer_Info's extra state (layers_lib.c's struct OInfo). It lives in the
 * library base (openlayers_lib.c), or the test's own, and every call is
 * handed it: no writable globals, so the library can run from ROM. */
struct GfxBase;
struct OInfo;
struct ol_base {
    struct GfxBase *gfx;
    struct OInfo *infos;
    struct SignalSemaphore infolock;
};

/* Once, before any call. */
void ol_init(struct ol_base *olb, struct GfxBase *gfx);

void ol_InitLayers(struct ol_base *olb, struct Layer_Info *li);
struct Layer *ol_CreateUpfrontLayer(struct ol_base *olb, struct Layer_Info *li, struct BitMap *bm, LONG x0, LONG y0,
                                    LONG x1, LONG y1, LONG flags, struct BitMap *sbm);
struct Layer *ol_CreateBehindLayer(struct ol_base *olb, struct Layer_Info *li, struct BitMap *bm, LONG x0, LONG y0,
                                   LONG x1, LONG y1, LONG flags, struct BitMap *sbm);
LONG ol_UpfrontLayer(struct ol_base *olb, struct Layer *l);
LONG ol_BehindLayer(struct ol_base *olb, struct Layer *l);
LONG ol_MoveLayer(struct ol_base *olb, struct Layer *l, LONG dx, LONG dy);
LONG ol_SizeLayer(struct ol_base *olb, struct Layer *l, LONG dx, LONG dy);
void ol_ScrollLayer(struct ol_base *olb, struct Layer *l, LONG dx, LONG dy);
LONG ol_BeginUpdate(struct ol_base *olb, struct Layer *l);
void ol_EndUpdate(struct ol_base *olb, struct Layer *l, UWORD flag);
LONG ol_DeleteLayer(struct ol_base *olb, struct Layer *l);
void ol_LockLayer(struct ol_base *olb, struct Layer *l);
void ol_UnlockLayer(struct ol_base *olb, struct Layer *l);
void ol_LockLayers(struct ol_base *olb, struct Layer_Info *li);
void ol_UnlockLayers(struct ol_base *olb, struct Layer_Info *li);
void ol_LockLayerInfo(struct ol_base *olb, struct Layer_Info *li);
void ol_SwapBitsRastPortClipRect(struct ol_base *olb, struct RastPort *rp, struct ClipRect *cr);
struct Layer *ol_WhichLayer(struct ol_base *olb, struct Layer_Info *li, LONG x, LONG y);
void ol_UnlockLayerInfo(struct ol_base *olb, struct Layer_Info *li);
struct Layer_Info *ol_NewLayerInfo(struct ol_base *olb);
void ol_DisposeLayerInfo(struct ol_base *olb, struct Layer_Info *li);
LONG ol_FattenLayerInfo(struct ol_base *olb, struct Layer_Info *li);
void ol_ThinLayerInfo(struct ol_base *olb, struct Layer_Info *li);
LONG ol_MoveLayerInFrontOf(struct ol_base *olb, struct Layer *l, struct Layer *other);
struct Region *ol_InstallClipRegion(struct ol_base *olb, struct Layer *l, struct Region *region);
LONG ol_MoveSizeLayer(struct ol_base *olb, struct Layer *l, LONG dx, LONG dy, LONG dw, LONG dh);
struct Layer *ol_CreateUpfrontHookLayer(struct ol_base *olb, struct Layer_Info *li, struct BitMap *bm, LONG x0,
                                        LONG y0, LONG x1, LONG y1, LONG flags, struct Hook *hook, struct BitMap *sbm);
struct Layer *ol_CreateBehindHookLayer(struct ol_base *olb, struct Layer_Info *li, struct BitMap *bm, LONG x0, LONG y0,
                                       LONG x1, LONG y1, LONG flags, struct Hook *hook, struct BitMap *sbm);
struct Hook *ol_InstallLayerHook(struct ol_base *olb, struct Layer *l, struct Hook *hook);
struct Hook *ol_InstallLayerInfoHook(struct ol_base *olb, struct Layer_Info *li, struct Hook *hook);
void ol_SortLayerCR(struct ol_base *olb, struct Layer *l, LONG dx, LONG dy);
void ol_DoHookClipRects(struct ol_base *olb, struct Hook *hook, struct RastPort *rp, const struct Rectangle *rect);
LONG ol_LayerOccluded(struct ol_base *olb, struct Layer *l);
LONG ol_HideLayer(struct ol_base *olb, struct Layer *l);
LONG ol_ShowLayer(struct ol_base *olb, struct Layer *l, struct Layer *in_front_of);
LONG ol_SetLayerInfoBounds(struct ol_base *olb, struct Layer_Info *li, const struct Rectangle *bounds);

#endif
