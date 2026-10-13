/* Test-only: graphics/text.h as far as library/core/gfx_rp.c uses it, and
 * the RastPort constants and structures tests/host-ndk's rastport.h leaves out, for
 * tests/m68k/test_gfx_face.sh. Written from the published layouts; no NDK
 * text. MIT, Dalsin Limited. */
#ifndef GRAPHICS_TEXT_H
#define GRAPHICS_TEXT_H
#include <exec/nodes.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#ifndef JAM1
#define JAM1 0
#define JAM2 1
#define COMPLEMENT 2
#define INVERSVID 4
#define FRST_DOT 1
#define ONE_DOT 2
#define DBUFFER 4
#define AREAOUTLINE 8
#define NOCROSSFILL 0x20
#endif
#define FSF_UNDERLINED 1
#define FSF_BOLD 2
#define FSF_ITALIC 4
#define FSF_EXTENDED 8
#define FSF_COLORFONT 0x40
#define FPF_REVPATH 4
#define FPF_PROPORTIONAL 0x20
struct TmpRas { BYTE *RasPtr; LONG Size; };
struct AreaInfo {
    WORD *VctrTbl, *VctrPtr;
    BYTE *FlagTbl, *FlagPtr;
    WORD Count, MaxCount, FirstX, FirstY;
};
struct MsgPort;
struct Message { struct Node mn_Node; struct MsgPort *mn_ReplyPort; UWORD mn_Length; };
struct TextFont {
    struct Message tf_Message;
    UWORD tf_YSize;
    UBYTE tf_Style, tf_Flags;
    UWORD tf_XSize, tf_Baseline, tf_BoldSmear, tf_Accessors;
    UBYTE tf_LoChar, tf_HiChar;
    APTR tf_CharData;
    UWORD tf_Modulo;
    APTR tf_CharLoc, tf_CharSpace, tf_CharKern;
};
struct TextExtent { WORD te_Width, te_Height; struct Rectangle te_Extent; };
#endif
