#ifndef GRAPHICS_RASTPORT_H
#define GRAPHICS_RASTPORT_H
#include <graphics/gfx.h>
struct Layer; struct TmpRas; struct AreaInfo; struct GelsInfo; struct TextFont;
struct RastPort {
    struct Layer *Layer;
    struct BitMap *BitMap;
    UWORD *AreaPtrn;
    struct TmpRas *TmpRas;
    struct AreaInfo *AreaInfo;
    struct GelsInfo *GelsInfo;
    UBYTE Mask;
    BYTE FgPen, BgPen, AOlPen, DrawMode, AreaPtSz, linpatcnt, dummy;
    UWORD Flags;
    UWORD LinePtrn;
    WORD cp_x, cp_y;
    UBYTE minterms[8];
    WORD PenWidth, PenHeight;
    struct TextFont *Font;
    UBYTE AlgoStyle, TxFlags;
    UWORD TxHeight, TxWidth, TxBaseline;
    WORD TxSpacing;
    APTR *RP_User;
    ULONG longreserved[2];
    UWORD wordreserved[7];
    UBYTE reserved[8];
};
#endif
