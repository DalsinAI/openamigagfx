/* host NDK (tests only): the graphics calls OpenLayers makes; the test supplies them. */
#ifndef PROTO_GRAPHICS_H
#define PROTO_GRAPHICS_H
#include <graphics/rastport.h>
#include <graphics/regions.h>
void InitRastPort(struct RastPort *rp);
struct BitMap *AllocBitMap(ULONG w, ULONG h, ULONG depth, ULONG flags, struct BitMap *friend_bm);
void FreeBitMap(struct BitMap *bm);
ULONG GetBitMapAttr(struct BitMap *bm, ULONG attr);
LONG BltBitMap(struct BitMap *src, LONG sx, LONG sy, struct BitMap *dst, LONG dx, LONG dy, LONG w, LONG h,
               ULONG minterm, ULONG mask, PLANEPTR temp);
struct Region *NewRegion(void);
void DisposeRegion(struct Region *r);
BOOL OrRectRegion(struct Region *r, struct Rectangle *rect);
void AndRectRegion(struct Region *r, struct Rectangle *rect);
void ClearRegion(struct Region *r);
#endif
