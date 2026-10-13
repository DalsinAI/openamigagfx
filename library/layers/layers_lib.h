/* SPDX-License-Identifier: MIT */
/* OpenLayers: layers.library's calls in plain C arguments. Copyright (c)
 * 2026 Dalsin Limited. MIT.
 *
 * layers_lib.c does the work of each call, through the core
 * (layers_core.h) and exec's and graphics.library's own calls; the library
 * shell (openlayers_lib.c) takes each call's registers and comes here.
 * The tests build layers_lib.c on the host and under qemu-m68k against a
 * pixel model of graphics.library (tests/m68k/test_layers_lib.sh). */
#ifndef OPENLAYERS_LIB_H
#define OPENLAYERS_LIB_H

#include <exec/types.h>
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

/* Once, before any call. */
void ol_init(void);

void ol_InitLayers(struct Layer_Info *li);
struct Layer *ol_CreateUpfrontLayer(struct Layer_Info *li, struct BitMap *bm, LONG x0, LONG y0, LONG x1, LONG y1,
                                    LONG flags, struct BitMap *sbm);
struct Layer *ol_CreateBehindLayer(struct Layer_Info *li, struct BitMap *bm, LONG x0, LONG y0, LONG x1, LONG y1,
                                   LONG flags, struct BitMap *sbm);
LONG ol_UpfrontLayer(struct Layer *l);
LONG ol_BehindLayer(struct Layer *l);
LONG ol_MoveLayer(struct Layer *l, LONG dx, LONG dy);
LONG ol_SizeLayer(struct Layer *l, LONG dx, LONG dy);
void ol_ScrollLayer(struct Layer *l, LONG dx, LONG dy);
LONG ol_BeginUpdate(struct Layer *l);
void ol_EndUpdate(struct Layer *l, UWORD flag);
LONG ol_DeleteLayer(struct Layer *l);
void ol_LockLayer(struct Layer *l);
void ol_UnlockLayer(struct Layer *l);
void ol_LockLayers(struct Layer_Info *li);
void ol_UnlockLayers(struct Layer_Info *li);
void ol_LockLayerInfo(struct Layer_Info *li);
void ol_SwapBitsRastPortClipRect(struct RastPort *rp, struct ClipRect *cr);
struct Layer *ol_WhichLayer(struct Layer_Info *li, LONG x, LONG y);
void ol_UnlockLayerInfo(struct Layer_Info *li);
struct Layer_Info *ol_NewLayerInfo(void);
void ol_DisposeLayerInfo(struct Layer_Info *li);
LONG ol_FattenLayerInfo(struct Layer_Info *li);
void ol_ThinLayerInfo(struct Layer_Info *li);
LONG ol_MoveLayerInFrontOf(struct Layer *l, struct Layer *other);
struct Region *ol_InstallClipRegion(struct Layer *l, struct Region *region);
LONG ol_MoveSizeLayer(struct Layer *l, LONG dx, LONG dy, LONG dw, LONG dh);
struct Layer *ol_CreateUpfrontHookLayer(struct Layer_Info *li, struct BitMap *bm, LONG x0, LONG y0, LONG x1,
                                        LONG y1, LONG flags, struct Hook *hook, struct BitMap *sbm);
struct Layer *ol_CreateBehindHookLayer(struct Layer_Info *li, struct BitMap *bm, LONG x0, LONG y0, LONG x1,
                                       LONG y1, LONG flags, struct Hook *hook, struct BitMap *sbm);
struct Hook *ol_InstallLayerHook(struct Layer *l, struct Hook *hook);
struct Hook *ol_InstallLayerInfoHook(struct Layer_Info *li, struct Hook *hook);
void ol_SortLayerCR(struct Layer *l, LONG dx, LONG dy);
void ol_DoHookClipRects(struct Hook *hook, struct RastPort *rp, const struct Rectangle *rect);
LONG ol_LayerOccluded(struct Layer *l);
LONG ol_HideLayer(struct Layer *l);
LONG ol_ShowLayer(struct Layer *l, struct Layer *in_front_of);
LONG ol_SetLayerInfoBounds(struct Layer_Info *li, const struct Rectangle *bounds);

#endif
