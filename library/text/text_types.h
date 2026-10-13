// SPDX-License-Identifier: MIT
// OpenGfx text: the NDK 3.2 structures the text calls touch, with their
// published names, laid out for the m68k (big-endian, 32-bit pointers,
// 2-byte alignment) and checked by static_asserts. Copyright (c) 2026
// Dalsin Limited. MIT.
//
// Clean-room, written from the layouts graphics/text.h, graphics/rastport.h,
// exec/nodes.h, exec/lists.h, exec/ports.h and utility/tagitem.h publish
// (no NDK text). Only the core includes this; the library shell includes
// the real NDK and hands the same memory over through text_core.h, where
// these names are only declared. Never include this next to the NDK.
#ifndef OPENGFX_TEXT_TYPES_H
#define OPENGFX_TEXT_TYPES_H

#include <stdint.h>

#ifndef __cplusplus
#error "text_types.h is the C++ core's; C sees text_core.h only"
#endif

typedef int32_t LONG;
typedef uint32_t ULONG;
typedef int16_t WORD;
typedef uint16_t UWORD;
typedef int8_t BYTE;
typedef uint8_t UBYTE;

// exec/nodes.h, exec/lists.h, exec/ports.h
struct Node {
    Node *ln_Succ;
    Node *ln_Pred;
    UBYTE ln_Type;
    BYTE ln_Pri;
    char *ln_Name;
};
struct List {
    Node *lh_Head;
    Node *lh_Tail;
    Node *lh_TailPred;
    UBYTE lh_Type;
    UBYTE l_pad;
};
struct MsgPort;
struct Message {
    Node mn_Node;
    MsgPort *mn_ReplyPort;
    UWORD mn_Length;
};
enum { NT_FONT = 12 };

// utility/tagitem.h
struct TagItem {
    ULONG ti_Tag;
    ULONG ti_Data;
};
enum : ULONG { TAG_DONE = 0, TAG_END = 0, TAG_IGNORE = 1, TAG_MORE = 2, TAG_SKIP = 3, TAG_USER = 0x80000000UL };

// graphics/gfx.h
struct Rectangle {
    WORD MinX, MinY, MaxX, MaxY;
};

// graphics/text.h
enum : UBYTE {
    FS_NORMAL = 0,
    FSF_UNDERLINED = 0x01,
    FSF_BOLD = 0x02,
    FSF_ITALIC = 0x04,
    FSF_EXTENDED = 0x08,
    FSF_COLORFONT = 0x40,
    FSF_TAGGED = 0x80,
};
enum : UBYTE {
    FPF_ROMFONT = 0x01,
    FPF_DISKFONT = 0x02,
    FPF_REVPATH = 0x04,
    FPF_TALLDOT = 0x08,
    FPF_WIDEDOT = 0x10,
    FPF_PROPORTIONAL = 0x20,
    FPF_DESIGNED = 0x40,
    FPF_REMOVED = 0x80,
};
constexpr ULONG TA_DeviceDPI = TAG_USER | 1;   // ti_Data: X DPI << 16 | Y DPI
constexpr LONG MAXFONTMATCHWEIGHT = 32767;

struct TextAttr {
    char *ta_Name;
    UWORD ta_YSize;
    UBYTE ta_Style;
    UBYTE ta_Flags;
};
struct TTextAttr {
    char *tta_Name;
    UWORD tta_YSize;
    UBYTE tta_Style;
    UBYTE tta_Flags;
    TagItem *tta_Tags;
};
struct TextFont {
    Message tf_Message;         // tf_Extension is tf_Message.mn_ReplyPort
    UWORD tf_YSize;
    UBYTE tf_Style;
    UBYTE tf_Flags;
    UWORD tf_XSize;
    UWORD tf_Baseline;
    UWORD tf_BoldSmear;
    UWORD tf_Accessors;
    UBYTE tf_LoChar;
    UBYTE tf_HiChar;
    void *tf_CharData;
    UWORD tf_Modulo;
    void *tf_CharLoc;           // per glyph: UWORD offset in bits, UWORD width
    void *tf_CharSpace;         // per glyph: WORD advance, or 0: tf_XSize
    void *tf_CharKern;          // per glyph: WORD kern, or 0: none
};
constexpr UWORD TE_MATCHWORD = 0xF00D;
enum : UBYTE { TE0F_NOREMFONT = 0x01 };
struct TextFontExtension {
    UWORD tfe_MatchWord;
    UBYTE tfe_Flags0;
    UBYTE tfe_Flags1;
    TextFont *tfe_BackPtr;
    MsgPort *tfe_OrigReplyPort;
    TagItem *tfe_Tags;
    UWORD *tfe_OFontPatchS;
    UWORD *tfe_OFontPatchK;
};
enum : UWORD { CT_COLORMASK = 0x000F, CT_COLORFONT = 0x0001, CT_GREYFONT = 0x0002, CT_ANTIALIAS = 0x0004,
               CTF_MAPCOLOR = 0x0001 };
struct ColorFontColors {
    UWORD cfc_Reserved;
    UWORD cfc_Count;
    UWORD *cfc_ColorTable;
};
struct ColorTextFont {
    TextFont ctf_TF;
    UWORD ctf_Flags;
    UBYTE ctf_Depth;
    UBYTE ctf_FgColor;
    UBYTE ctf_Low;
    UBYTE ctf_High;
    UBYTE ctf_PlanePick;
    UBYTE ctf_PlaneOnOff;
    ColorFontColors *ctf_ColorFontColors;
    void *ctf_CharData[8];
};
struct TextExtent {
    WORD te_Width;
    WORD te_Height;
    Rectangle te_Extent;
};

// graphics/rastport.h: the RastPort, the fields text reads by name.
enum : UBYTE { JAM1 = 0, JAM2 = 1, COMPLEMENT = 2, INVERSVID = 4 };
struct RastPort {
    void *Layer;
    void *BitMap;
    UWORD *AreaPtrn;
    void *TmpRas;
    void *AreaInfo;
    void *GelsInfo;
    UBYTE Mask;
    BYTE FgPen;
    BYTE BgPen;
    BYTE AOlPen;
    BYTE DrawMode;
    BYTE AreaPtSz;
    BYTE linpatcnt;
    BYTE dummy;
    UWORD Flags;
    UWORD LinePtrn;
    WORD cp_x, cp_y;
    UBYTE minterms[8];
    WORD PenWidth;
    WORD PenHeight;
    TextFont *Font;
    UBYTE AlgoStyle;
    UBYTE TxFlags;
    UWORD TxHeight;
    UWORD TxWidth;
    UWORD TxBaseline;
    WORD TxSpacing;
    void **RP_User;
    ULONG longreserved[2];
    UWORD wordreserved[7];
    UBYTE reserved[8];
};

#define GT_AT(type, field, off) static_assert(__builtin_offsetof(type, field) == (off), #type "." #field)
#if defined(__m68k__) || defined(__mc68000__)
GT_AT(Node, ln_Type, 8);
GT_AT(Node, ln_Name, 10);
static_assert(sizeof(Node) == 14);
static_assert(sizeof(List) == 14);
GT_AT(Message, mn_ReplyPort, 14);
static_assert(sizeof(Message) == 20);
GT_AT(TextAttr, ta_YSize, 4);
GT_AT(TextAttr, ta_Style, 6);
static_assert(sizeof(TextAttr) == 8);
GT_AT(TTextAttr, tta_Tags, 8);
static_assert(sizeof(TTextAttr) == 12);
GT_AT(TextFont, tf_YSize, 20);
GT_AT(TextFont, tf_Style, 22);
GT_AT(TextFont, tf_Flags, 23);
GT_AT(TextFont, tf_XSize, 24);
GT_AT(TextFont, tf_Baseline, 26);
GT_AT(TextFont, tf_BoldSmear, 28);
GT_AT(TextFont, tf_Accessors, 30);
GT_AT(TextFont, tf_LoChar, 32);
GT_AT(TextFont, tf_HiChar, 33);
GT_AT(TextFont, tf_CharData, 34);
GT_AT(TextFont, tf_Modulo, 38);
GT_AT(TextFont, tf_CharLoc, 40);
GT_AT(TextFont, tf_CharSpace, 44);
GT_AT(TextFont, tf_CharKern, 48);
static_assert(sizeof(TextFont) == 52);
GT_AT(TextFontExtension, tfe_BackPtr, 4);
GT_AT(TextFontExtension, tfe_OrigReplyPort, 8);
GT_AT(TextFontExtension, tfe_Tags, 12);
static_assert(sizeof(TextFontExtension) == 24);
static_assert(sizeof(ColorFontColors) == 8);
GT_AT(ColorTextFont, ctf_Flags, 52);
GT_AT(ColorTextFont, ctf_Depth, 54);
GT_AT(ColorTextFont, ctf_FgColor, 55);
GT_AT(ColorTextFont, ctf_Low, 56);
GT_AT(ColorTextFont, ctf_PlanePick, 58);
GT_AT(ColorTextFont, ctf_PlaneOnOff, 59);
GT_AT(ColorTextFont, ctf_ColorFontColors, 60);
GT_AT(ColorTextFont, ctf_CharData, 64);
static_assert(sizeof(ColorTextFont) == 96);
static_assert(sizeof(TextExtent) == 12);
GT_AT(RastPort, Mask, 24);
GT_AT(RastPort, FgPen, 25);
GT_AT(RastPort, DrawMode, 28);
GT_AT(RastPort, cp_x, 36);
GT_AT(RastPort, cp_y, 38);
GT_AT(RastPort, Font, 52);
GT_AT(RastPort, AlgoStyle, 56);
GT_AT(RastPort, TxHeight, 58);
GT_AT(RastPort, TxWidth, 60);
GT_AT(RastPort, TxBaseline, 62);
GT_AT(RastPort, TxSpacing, 64);
static_assert(sizeof(RastPort) == 100);
#endif
#undef GT_AT

#endif
