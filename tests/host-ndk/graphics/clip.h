#ifndef GRAPHICS_CLIP_H
#define GRAPHICS_CLIP_H
#include <exec/semaphores.h>
#include <graphics/gfx.h>
#include <graphics/regions.h>
#include <utility/hooks.h>
struct Layer_Info; struct RastPort;
struct Layer {
    struct Layer *front, *back;
    struct ClipRect *ClipRect;
    struct RastPort *rp;
    struct Rectangle bounds;
    struct Layer *nlink;
    UWORD priority;
    UWORD Flags;
    struct BitMap *SuperBitMap;
    struct ClipRect *SuperClipRect;
    APTR Window;
    WORD Scroll_X, Scroll_Y;
    struct ClipRect *cr, *cr2, *crnew;
    struct ClipRect *SuperSaveClipRects;
    struct ClipRect *_cliprects;
    struct Layer_Info *LayerInfo;
    struct SignalSemaphore Lock;
    struct Hook *BackFill;
    ULONG reserved1;
    struct Region *ClipRegion;
    struct Region *saveClipRects;
    WORD Width, Height;
    UBYTE reserved2[18];
    struct Region *DamageList;
};
struct ClipRect {
    struct ClipRect *Next;
    struct ClipRect *reservedlink;
    LONG obscured;
    struct BitMap *BitMap;
    struct Rectangle bounds;
    void *_p1, *_p2;
    LONG reserved;
    LONG Flags;
};
#endif
