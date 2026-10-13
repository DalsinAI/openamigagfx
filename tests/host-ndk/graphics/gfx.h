#ifndef GRAPHICS_GFX_H
#define GRAPHICS_GFX_H
#include <exec/types.h>
struct Rectangle { WORD MinX, MinY, MaxX, MaxY; };
struct BitMap { UWORD BytesPerRow; UWORD Rows; UBYTE Flags; UBYTE Depth; UWORD pad; PLANEPTR Planes[8]; };
#define BMA_HEIGHT 0
#define BMA_DEPTH 4
#define BMA_WIDTH 8
#define BMA_FLAGS 12
#define BMF_CLEAR 1
#define BMF_DISPLAYABLE 2
#define BMF_INTERLEAVED 4
#endif
