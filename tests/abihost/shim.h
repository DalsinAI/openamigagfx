/* SPDX-License-Identifier: MIT */
/* LayersABITest on the host: what it calls, played by abihost.c over
 * OpenLayers' own code. Copyright (c) 2026 Dalsin Limited. MIT. */
#ifndef ABIHOST_SHIM_H
#define ABIHOST_SHIM_H
#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/clip.h>
#include <graphics/layers.h>
#include <graphics/regions.h>
#include <utility/hooks.h>
#include <proto/exec.h>
#include <proto/graphics.h>

#define REG(r, decl) decl

struct Library { char pad[24]; APTR lib_IdString; };
struct Library *OpenLibrary(const char *name, ULONG version);
void CloseLibrary(struct Library *l);
BOOL AddPart(char *dir, const char *file, ULONG size);

void SetAPen(struct RastPort *rp, ULONG pen);
void RectFill(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1);
LONG ReadPixel(struct RastPort *rp, LONG x, LONG y);
void WaitBlit(void);

struct Layer_Info *NewLayerInfo(void);
void DisposeLayerInfo(struct Layer_Info *li);
struct Hook *InstallLayerInfoHook(struct Layer_Info *li, struct Hook *h);
struct Layer *CreateUpfrontHookLayer(struct Layer_Info *li, struct BitMap *bm, LONG x0, LONG y0, LONG x1, LONG y1,
                                     LONG flags, struct Hook *h, struct BitMap *sbm);
struct Layer *CreateBehindHookLayer(struct Layer_Info *li, struct BitMap *bm, LONG x0, LONG y0, LONG x1, LONG y1,
                                    LONG flags, struct Hook *h, struct BitMap *sbm);
LONG DeleteLayer(LONG dummy, struct Layer *l);
LONG UpfrontLayer(LONG dummy, struct Layer *l);
LONG BehindLayer(LONG dummy, struct Layer *l);
LONG MoveLayer(LONG dummy, struct Layer *l, LONG dx, LONG dy);
LONG SizeLayer(LONG dummy, struct Layer *l, LONG dx, LONG dy);
LONG MoveSizeLayer(struct Layer *l, LONG dx, LONG dy, LONG dw, LONG dh);
LONG MoveLayerInFrontOf(struct Layer *l, struct Layer *o);
LONG ShowLayer(struct Layer *l, struct Layer *o);
LONG HideLayer(struct Layer *l);
struct Region *InstallClipRegion(struct Layer *l, struct Region *r);
LONG BeginUpdate(struct Layer *l);
void EndUpdate(struct Layer *l, UWORD flag);
void ScrollLayer(LONG dummy, struct Layer *l, LONG dx, LONG dy);
struct Layer *WhichLayer(struct Layer_Info *li, LONG x, LONG y);
LONG LayerOccluded(struct Layer *l);
void LockLayer(LONG dummy, struct Layer *l);
void UnlockLayer(struct Layer *l);
void LockLayers(struct Layer_Info *li);
void UnlockLayers(struct Layer_Info *li);
void DoHookClipRects(struct Hook *h, struct RastPort *rp, struct Rectangle *r);
void SortLayerCR(struct Layer *l, LONG dx, LONG dy);
void SwapBitsRastPortClipRect(struct RastPort *rp, struct ClipRect *cr);
#endif
