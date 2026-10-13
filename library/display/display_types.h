/* SPDX-License-Identifier: MIT */
/* OpenGfx display: the NDK layouts only the display area uses, in stdint
 * types with no Amiga header. Copyright (c) 2026 Dalsin Limited. MIT.
 *
 * Clean-room: the field order and sizes of the NDK 3.2 headers' published
 * structures (graphics/view.h, graphics/copper.h, graphics/monitor.h,
 * graphics/displayinfo.h, graphics/sprite.h, graphics/gfxnodes.h,
 * graphics/videocontrol.h, graphics/modeid.h), written out so the lean C++
 * core can use them. On the 68k every size and the offsets that matter are
 * checked below; tests/DisplayABITest.c prints the NDK's own sizeof for the
 * side-by-side check. The exec and shared graphics structures (Node, List,
 * BitMap, ColorMap, GfxBase, TagItem) are gfx-face's, in gfx_types.h. */
#ifndef OPENGFX_DISPLAY_TYPES_H
#define OPENGFX_DISPLAY_TYPES_H

#include "gfx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- exec bits only the display uses -------------------------------- */

struct gd_extnode {                     /* struct ExtendedNode, 24 bytes */
    struct gfx_node node;
    uint8_t subsystem;
    uint8_t subtype;
    int32_t library;
    void *init;
};

struct gd_message {                     /* struct Message, 20 bytes */
    struct gfx_node node;
    void *replyport;
    uint16_t length;
};

/* ---- graphics/copper.h ------------------------------------------------ */

struct gd_copins {                      /* struct CopIns, 6 bytes */
    int16_t opcode;                     /* COPPER_MOVE, COPPER_WAIT, CPRNXTBUF | CPR_NT_* */
    union {
        struct gd_coplist *nxtlist;
        struct {
            int16_t a;                  /* VWaitPos or DestAddr */
            int16_t b;                  /* HWaitPos or DestData */
        } w;
    } u;
};
enum {
    GD_COPPER_MOVE = 0, GD_COPPER_WAIT = 1, GD_CPRNXTBUF = 2,
    GD_CPR_NT_SYS = 0x2000, GD_CPR_NT_SHT = 0x4000, GD_CPR_NT_LOF = (int16_t)0x8000
};

struct gd_coplist {                     /* struct CopList, 54 bytes */
    struct gd_coplist *next;
    struct gd_coplist *coplist;         /* _CopList */
    struct gd_viewport *viewport;       /* _ViewPort */
    struct gd_copins *copins;
    struct gd_copins *copptr;
    uint16_t *coplstart;                /* where MrgCop put it in the long frame list */
    uint16_t *copsstart;                /* ... and in the short frame list */
    int16_t count;
    int16_t maxcount;
    int16_t dyoffset;
    uint16_t *cop2start;
    uint16_t *cop3start;
    uint16_t *cop4start;
    uint16_t *cop5start;
    uint16_t slrepeat;
    uint16_t flags;
};

struct gd_ucoplist {                    /* struct UCopList, 12 bytes */
    struct gd_ucoplist *next;
    struct gd_coplist *firstcoplist;
    struct gd_coplist *coplist;
};

struct gd_cprlist {                     /* struct cprlist, 10 bytes */
    struct gd_cprlist *next;
    uint16_t *start;
    int16_t maxcount;                   /* in copper instructions (two words each) */
};

struct gd_copinit {                     /* struct copinit, 192 bytes */
    uint16_t vsync_hblank[2];
    uint16_t diagstrt[12];
    uint16_t fm0[2];
    uint16_t diwstart[10];
    uint16_t bplcon2[2];
    uint16_t sprfix[2 * 8];
    uint16_t sprstrtup[2 * 8 * 2];
    uint16_t wait14[2];
    uint16_t norm_hblank[2];
    uint16_t jump[2];
    uint16_t wait_forever[6];
    uint16_t sprstop[8];
};

/* ---- graphics/view.h --------------------------------------------------- */

struct gd_rasinfo {                     /* struct RasInfo, 12 bytes */
    struct gd_rasinfo *next;
    struct gfx_bitmap *bitmap;
    int16_t rxoffset, ryoffset;
};

struct gd_viewport {                    /* struct ViewPort, 40 bytes */
    struct gd_viewport *next;
    struct gfx_colormap *colormap;
    struct gd_coplist *dspins;
    struct gd_coplist *sprins;
    struct gd_coplist *clrins;
    struct gd_ucoplist *ucopins;
    int16_t dwidth, dheight;
    int16_t dxoffset, dyoffset;
    uint16_t modes;
    uint8_t spritepriorities;
    uint8_t extendedmodes;
    struct gd_rasinfo *rasinfo;
};

struct gd_view {                        /* struct View, 18 bytes */
    struct gd_viewport *viewport;
    struct gd_cprlist *lofcprlist;
    struct gd_cprlist *shfcprlist;
    int16_t dyoffset, dxoffset;
    uint16_t modes;
};

struct gd_viewextra {                   /* struct ViewExtra, 34 bytes */
    struct gd_extnode n;
    struct gd_view *view;
    struct gd_monitorspec *monitor;
    uint16_t topline;
};

struct gd_point { int16_t x, y; };

struct gd_vpextra {                     /* struct ViewPortExtra, 66 bytes */
    struct gd_extnode n;
    struct gd_viewport *viewport;
    struct gfx_rectangle displayclip;
    void *vectable;
    void *driverdata[2];
    uint16_t flags;
    struct gd_point origin[2];
    uint32_t cop1ptr;
    uint32_t cop2ptr;
};

/* ViewPort and View Modes */
enum {
    GD_GENLOCK_VIDEO = 0x0002, GD_LACE = 0x0004, GD_DOUBLESCAN = 0x0008, GD_SUPERHIRES = 0x0020,
    GD_PFBA = 0x0040, GD_EXTRA_HALFBRITE = 0x0080, GD_GENLOCK_AUDIO = 0x0100, GD_DUALPF = 0x0400,
    GD_HAM = 0x0800, GD_EXTEND_VSTRUCT = 0x1000, GD_VP_HIDE = 0x2000, GD_SPRITES = 0x4000,
    GD_HIRES = 0x8000
};
/* MakeVPort's and MrgCop's answers */
enum { GD_MVP_OK = 0, GD_MVP_NO_MEM = 1, GD_MVP_NO_VPE = 2, GD_MVP_NO_DSPINS = 3, GD_MVP_NO_DISPLAY = 4,
       GD_MVP_OFF_BOTTOM = 5 };
enum { GD_MCOP_OK = 0, GD_MCOP_NO_MEM = 1, GD_MCOP_NOP = 2 };

/* ColorMap Flags and AuxFlags (gfx_colormap) */
enum {
    GD_COLORMAP_TRANSPARENCY = 1, GD_COLORPLANE_TRANSPARENCY = 2, GD_BORDER_BLANKING = 4,
    GD_BORDER_NOTRANSPARENCY = 8, GD_VIDEOCONTROL_BATCH = 0x10, GD_USER_COPPER_CLIP = 0x20,
    GD_BORDERSPRITES = 0x40
};
enum { GD_CMAF_FULLPALETTE = 1, GD_CMAF_NO_INTERMED_UPDATE = 2, GD_CMAF_NO_COLOR_LOAD = 4,
       GD_CMAF_DUALPF_DISABLE = 8 };

/* struct DBufInfo, 84 bytes */
struct gd_dbufinfo {
    void *link1;
    uint32_t count1;
    struct gd_message safemessage;
    void *userdata1;
    void *link2;
    uint32_t count2;
    struct gd_message dispmessage;
    void *userdata2;
    uint32_t matchlong;
    void *copptr1, *copptr2, *copptr3;
    uint16_t beampos1, beampos2;
};

/* ---- graphics/monitor.h ------------------------------------------------ */

struct gd_signalinterval { uint16_t start, stop; };   /* AnalogSignalInterval */

struct gd_specialmonitor {              /* struct SpecialMonitor, 58 bytes */
    struct gd_extnode n;
    uint16_t flags;
    void *do_monitor;
    void *reserved1, *reserved2, *reserved3;
    struct gd_signalinterval hblank, vblank, hsync, vsync;
};

struct gd_monitorspec {                 /* struct MonitorSpec, 160 bytes */
    struct gd_extnode n;
    uint16_t flags;
    int32_t ratioh, ratiov;
    uint16_t total_rows;
    uint16_t total_colorclocks;
    uint16_t denisemaxdisplaycolumn;
    uint16_t beamcon0;
    uint16_t min_row;
    struct gd_specialmonitor *special;
    uint16_t opencount;
    void *transform, *translate, *scale;
    uint16_t xoffset, yoffset;
    struct gfx_rectangle legalview;
    void *maxoscan, *videoscan;
    uint16_t deniseminDisplaycolumn;
    uint32_t displaycompatible;
    struct gfx_list displayinfodatabase;
    struct gfx_semaphore displayinfodatabasesemaphore;
    void *mrgcop, *loadview, *killview;
};

/* GfxNew's types (graphics/gfxnodes.h) */
enum { GD_VIEW_EXTRA_TYPE = 1, GD_VIEWPORT_EXTRA_TYPE = 2, GD_SPECIAL_MONITOR_TYPE = 3,
       GD_MONITOR_SPEC_TYPE = 4 };
enum { GD_SS_GRAPHICS = 0x02, GD_NT_GRAPHICS = 15 };

/* ---- graphics/displayinfo.h ------------------------------------------- */

struct gd_queryheader {                 /* struct QueryHeader, 16 bytes */
    uint32_t structid, displayid, skipid, length;
};

struct gd_displayinfo {                 /* struct DisplayInfo, 56 bytes */
    struct gd_queryheader header;
    uint16_t notavailable;
    uint32_t propertyflags;
    struct gd_point resolution;
    uint16_t pixelspeed;
    uint16_t numstdsprites;
    uint16_t paletterange;
    struct gd_point spriteresolution;
    uint8_t pad[4];
    uint8_t redbits, greenbits, bluebits;
    uint8_t pad2[5];
    uint32_t reserved[2];
};

struct gd_dimensioninfo {               /* struct DimensionInfo, 88 bytes */
    struct gd_queryheader header;
    uint16_t maxdepth;
    uint16_t minrasterwidth, minrasterheight;
    uint16_t maxrasterwidth, maxrasterheight;
    struct gfx_rectangle nominal, maxoscan, videooscan, txtoscan, stdoscan;
    uint8_t pad[14];
    uint32_t reserved[2];
};

struct gd_monitorinfo {                 /* struct MonitorInfo, 96 bytes */
    struct gd_queryheader header;
    struct gd_monitorspec *mspc;
    struct gd_point viewposition;
    struct gd_point viewresolution;
    struct gfx_rectangle viewpositionrange;
    uint16_t totalrows;
    uint16_t totalcolorclocks;
    uint16_t minrow;
    int16_t compatibility;
    uint8_t pad[32];
    struct gd_point mouseticks;
    struct gd_point defaultviewposition;
    uint32_t preferredmodeid;
    uint32_t reserved[2];
};

struct gd_nameinfo {                    /* struct NameInfo, 56 bytes */
    struct gd_queryheader header;
    uint8_t name[32];
    uint32_t reserved[2];
};

#define GD_DTAG_DISP ((uint32_t)0x80000000u)
#define GD_DTAG_DIMS ((uint32_t)0x80001000u)
#define GD_DTAG_MNTR ((uint32_t)0x80002000u)
#define GD_DTAG_NAME ((uint32_t)0x80003000u)
#define GD_DTAG_VEC ((uint32_t)0x80004000u)
#define GD_DI_AVAIL_NOCHIPS ((uint32_t)1)
#define GD_DI_AVAIL_NOMONITOR ((uint32_t)2)
#define GD_DI_AVAIL_NOTWITHGENLOCK ((uint32_t)4)
#define GD_DIPF_IS_LACE ((uint32_t)0x1)
#define GD_DIPF_IS_DUALPF ((uint32_t)0x2)
#define GD_DIPF_IS_PF2PRI ((uint32_t)0x4)
#define GD_DIPF_IS_HAM ((uint32_t)0x8)
#define GD_DIPF_IS_ECS ((uint32_t)0x10)
#define GD_DIPF_IS_PAL ((uint32_t)0x20)
#define GD_DIPF_IS_SPRITES ((uint32_t)0x40)
#define GD_DIPF_IS_GENLOCK ((uint32_t)0x80)
#define GD_DIPF_IS_WB ((uint32_t)0x100)
#define GD_DIPF_IS_DRAGGABLE ((uint32_t)0x200)
#define GD_DIPF_IS_PANELLED ((uint32_t)0x400)
#define GD_DIPF_IS_BEAMSYNC ((uint32_t)0x800)
#define GD_DIPF_IS_EXTRAHALFBRITE ((uint32_t)0x1000)
#define GD_DIPF_IS_SPRITES_ATT ((uint32_t)0x2000)
#define GD_DIPF_IS_SPRITES_CHNG_RES ((uint32_t)0x4000)
#define GD_DIPF_IS_SPRITES_BORDER ((uint32_t)0x8000)
#define GD_DIPF_IS_AA ((uint32_t)0x10000)
#define GD_DIPF_IS_SCANDBL ((uint32_t)0x20000)
#define GD_DIPF_IS_SPRITES_CHNG_BASE ((uint32_t)0x40000)
#define GD_DIPF_IS_SPRITES_CHNG_PRI ((uint32_t)0x80000)
#define GD_DIPF_IS_DBUFFER ((uint32_t)0x100000)
#define GD_DIPF_IS_PROGBEAM ((uint32_t)0x200000)
#define GD_DIPF_IS_FOREIGN ((uint32_t)0x80000000u)
/* graphics/modeid.h */
#define GD_INVALID_ID ((uint32_t)0xFFFFFFFFu)
#define GD_MONITOR_ID_MASK ((uint32_t)0xFFFF1000u)
#define GD_DEFAULT_MONITOR_ID ((uint32_t)0)
#define GD_NTSC_MONITOR_ID ((uint32_t)0x00011000u)
#define GD_PAL_MONITOR_ID ((uint32_t)0x00021000u)
#define GD_LORES_KEY ((uint32_t)0)
#define GD_HIRES_KEY ((uint32_t)0x8000)
#define GD_SUPER_KEY ((uint32_t)0x8020)
#define GD_HAM_KEY ((uint32_t)0x800)
#define GD_EXTRAHALFBRITE_KEY ((uint32_t)0x80)
#define GD_LORESDPF_KEY ((uint32_t)0x400)
#define GD_LORESDPF2_KEY ((uint32_t)0x440)
/* BestModeIDA's tags */
#define GD_BIDTAG_DIPFMustHave ((uint32_t)0x80000001u)
#define GD_BIDTAG_DIPFMustNotHave ((uint32_t)(0x80000001u + 1))
#define GD_BIDTAG_ViewPort ((uint32_t)(0x80000001u + 2))
#define GD_BIDTAG_NominalWidth ((uint32_t)(0x80000001u + 3))
#define GD_BIDTAG_NominalHeight ((uint32_t)(0x80000001u + 4))
#define GD_BIDTAG_DesiredWidth ((uint32_t)(0x80000001u + 5))
#define GD_BIDTAG_DesiredHeight ((uint32_t)(0x80000001u + 6))
#define GD_BIDTAG_Depth ((uint32_t)(0x80000001u + 7))
#define GD_BIDTAG_MonitorID ((uint32_t)(0x80000001u + 8))
#define GD_BIDTAG_SourceID ((uint32_t)(0x80000001u + 9))
#define GD_BIDTAG_RedBits ((uint32_t)(0x80000001u + 10))
#define GD_BIDTAG_BlueBits ((uint32_t)(0x80000001u + 11))
#define GD_BIDTAG_GreenBits ((uint32_t)(0x80000001u + 12))
#define GD_BIDTAG_GfxPrivate ((uint32_t)(0x80000001u + 13))
/* CoerceMode's flags */
enum { GD_PRESERVE_COLORS = 1, GD_AVOID_FLICKER = 2, GD_IGNORE_MCOMPAT = 4 };

/* ---- graphics/sprite.h ------------------------------------------------- */

struct gd_simplesprite {                /* struct SimpleSprite, 12 bytes */
    uint16_t *posctldata;
    uint16_t height;
    uint16_t x, y;
    int16_t num;
};

struct gd_extsprite {                   /* struct ExtSprite, 16 bytes */
    struct gd_simplesprite es;
    uint16_t wordwidth;
    uint16_t flags;
};
enum { GD_SPRITE_ATTACHED = 0x80 };
#define GD_SPRITEA_Width ((uint32_t)0x81000000u)
#define GD_SPRITEA_XReplication ((uint32_t)0x81000002u)
#define GD_SPRITEA_YReplication ((uint32_t)0x81000004u)
#define GD_SPRITEA_OutputHeight ((uint32_t)0x81000006u)
#define GD_SPRITEA_Attached ((uint32_t)0x81000008u)
#define GD_SPRITEA_OldDataFormat ((uint32_t)0x8100000Au)
#define GD_GSTAG_SPRITE_NUM ((uint32_t)0x82000020u)
#define GD_GSTAG_ATTACHED ((uint32_t)0x82000022u)
#define GD_GSTAG_SOFTSPRITE ((uint32_t)0x82000024u)
#define GD_GSTAG_SCANDOUBLED ((uint32_t)0x83000000u)

/* ---- the custom chips (offsets from 0xDFF000) -------------------------- */
enum {
    GD_VPOSR = 0x004, GD_VHPOSR = 0x006, GD_DENISEID = 0x07C, GD_COP1LCH = 0x080, GD_COP1LCL = 0x082,
    GD_COP2LCH = 0x084, GD_COP2LCL = 0x086, GD_COPJMP1 = 0x088, GD_COPJMP2 = 0x08A,
    GD_DIWSTRT = 0x08E, GD_DIWSTOP = 0x090, GD_DDFSTRT = 0x092, GD_DDFSTOP = 0x094,
    GD_BPL1PTH = 0x0E0, GD_BPLCON0 = 0x100, GD_BPLCON1 = 0x102, GD_BPLCON2 = 0x104, GD_BPLCON3 = 0x106,
    GD_BPL1MOD = 0x108, GD_BPL2MOD = 0x10A, GD_BPLCON4 = 0x10C, GD_SPR0PTH = 0x120, GD_SPR0POS = 0x140,
    GD_COLOR00 = 0x180, GD_BEAMCON0 = 0x1DC, GD_DIWHIGH = 0x1E4, GD_FMODE = 0x1FC, GD_NOOP = 0x1FE
};

#if defined(__m68k__) || defined(__mc68000__) || defined(mc68000)
#define GD_OFF(s, f, o) GFX_STATIC_ASSERT(offsetof(struct s, f) == (o), #s "." #f " is at " #o)
#define GD_SIZE(s, n) GFX_STATIC_ASSERT(sizeof(struct s) == (n), #s " is " #n " bytes")
GD_SIZE(gd_extnode, 24);
GD_SIZE(gd_message, 20);
GD_SIZE(gd_copins, 6);
GD_SIZE(gd_coplist, 54);
GD_OFF(gd_coplist, count, 28);
GD_OFF(gd_coplist, cop2start, 34);
GD_OFF(gd_coplist, flags, 52);
GD_SIZE(gd_ucoplist, 12);
GD_SIZE(gd_cprlist, 10);
GD_SIZE(gd_copinit, 192);
GD_OFF(gd_copinit, sprstrtup, 88);
GD_OFF(gd_copinit, jump, 160);
GD_OFF(gd_copinit, sprstop, 176);
GD_SIZE(gd_rasinfo, 12);
GD_SIZE(gd_viewport, 40);
GD_OFF(gd_viewport, dwidth, 24);
GD_OFF(gd_viewport, modes, 32);
GD_SIZE(gd_view, 18);
GD_OFF(gd_view, modes, 16);
GD_SIZE(gd_viewextra, 34);
GD_SIZE(gd_vpextra, 66);
GD_OFF(gd_vpextra, displayclip, 28);
GD_OFF(gd_vpextra, flags, 48);
GD_SIZE(gd_dbufinfo, 84);
GD_OFF(gd_dbufinfo, dispmessage, 40);
GD_OFF(gd_dbufinfo, matchlong, 64);
GD_SIZE(gd_specialmonitor, 58);
GD_SIZE(gd_monitorspec, 160);
GD_OFF(gd_monitorspec, total_rows, 34);
GD_OFF(gd_monitorspec, min_row, 42);
GD_OFF(gd_monitorspec, opencount, 48);
GD_OFF(gd_monitorspec, legalview, 66);
GD_OFF(gd_monitorspec, displaycompatible, 84);
GD_OFF(gd_monitorspec, displayinfodatabase, 88);
GD_OFF(gd_monitorspec, mrgcop, 148);
GD_SIZE(gd_queryheader, 16);
GD_SIZE(gd_displayinfo, 56);
GD_OFF(gd_displayinfo, propertyflags, 18);
GD_OFF(gd_displayinfo, redbits, 40);
GD_SIZE(gd_dimensioninfo, 88);
GD_OFF(gd_dimensioninfo, nominal, 26);
GD_OFF(gd_dimensioninfo, stdoscan, 58);
GD_SIZE(gd_monitorinfo, 96);
GD_OFF(gd_monitorinfo, totalrows, 36);
GD_OFF(gd_monitorinfo, mouseticks, 76);
GD_OFF(gd_monitorinfo, preferredmodeid, 84);
GD_SIZE(gd_nameinfo, 56);
GD_SIZE(gd_simplesprite, 12);
GD_SIZE(gd_extsprite, 16);
#endif

#ifdef __cplusplus
}
#endif
#endif
