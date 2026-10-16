/* host NDK overlay (tests only): graphics/text.h for building
 * library/core/gfx_rp.c next to OpenGfx's text calls in
 * tests/m68k/test_gfx_text.sh. Clean-room, from the published layouts
 * (offsets checked by text_types.h's static_asserts on the m68k).
 * MIT, AmigaChrome Team. */
#ifndef GRAPHICS_TEXT_H
#define GRAPHICS_TEXT_H
#include <exec/ports.h>
#include <graphics/gfx.h>
#define FS_NORMAL 0
#define FSF_UNDERLINED 0x01
#define FSF_BOLD 0x02
#define FSF_ITALIC 0x04
#define FSF_EXTENDED 0x08
#define FSF_COLORFONT 0x40
#define FSF_TAGGED 0x80
#define FPF_ROMFONT 0x01
#define FPF_DISKFONT 0x02
#define FPF_REVPATH 0x04
#define FPF_TALLDOT 0x08
#define FPF_WIDEDOT 0x10
#define FPF_PROPORTIONAL 0x20
#define FPF_DESIGNED 0x40
#define FPF_REMOVED 0x80
struct TextAttr { STRPTR ta_Name; UWORD ta_YSize; UBYTE ta_Style, ta_Flags; };
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
struct TagItem;
struct TTextAttr { STRPTR tta_Name; UWORD tta_YSize; UBYTE tta_Style, tta_Flags; struct TagItem *tta_Tags; };
#define TA_DeviceDPI (1 | 0x80000000UL)
#define MAXFONTMATCHWEIGHT 32767
#define TE0F_NOREMFONT 0x01
struct TextFontExtension {
    UWORD tfe_MatchWord;
    UBYTE tfe_Flags0, tfe_Flags1;
    struct TextFont *tfe_BackPtr;
    struct MsgPort *tfe_OrigReplyPort;
    struct TagItem *tfe_Tags;
    UWORD *tfe_OFontPatchS, *tfe_OFontPatchK;
};
#define CTF_MAPCOLOR 0x0001
struct ColorFontColors { UWORD cfc_Reserved, cfc_Count; UWORD *cfc_ColorTable; };
struct ColorTextFont {
    struct TextFont ctf_TF;
    UWORD ctf_Flags;
    UBYTE ctf_Depth, ctf_FgColor, ctf_Low, ctf_High, ctf_PlanePick, ctf_PlaneOnOff;
    struct ColorFontColors *ctf_ColorFontColors;
    APTR ctf_CharData[8];
};
#endif
