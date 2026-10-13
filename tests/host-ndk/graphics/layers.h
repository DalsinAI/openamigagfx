#ifndef GRAPHICS_LAYERS_H
#define GRAPHICS_LAYERS_H
#include <exec/lists.h>
#include <exec/semaphores.h>
#include <graphics/clip.h>
#define LAYERSIMPLE 1
#define LAYERSMART 2
#define LAYERSUPER 4
#define LAYERUPDATING 0x10
#define LAYERBACKDROP 0x40
#define LAYERREFRESH 0x80
#define LAYER_CLIPRECTS_LOST 0x100
#define LAYERIREFRESH 0x200
#define LAYERIREFRESH2 0x400
#define NEWLAYERINFO_CALLED 1
#define LAYERS_NOBACKFILL ((struct Hook *)1)
#define LAYERS_BACKFILL ((struct Hook *)0)
struct Layer_Info {
    struct Layer *top_layer;
    struct Layer *check_lp;
    struct ClipRect *obs;
    struct ClipRect *FreeClipRects;
    LONG PrivateReserve1, PrivateReserve2;
    struct SignalSemaphore Lock;
    struct MinList gs_Head;
    WORD PrivateReserve3;
    VOID *PrivateReserve4;
    UWORD Flags;
    BYTE fatten_count;
    BYTE LockLayersCount;
    WORD PrivateReserve5;
    VOID *BlankHook;
    VOID *LayerInfo_extra;
};
#endif
