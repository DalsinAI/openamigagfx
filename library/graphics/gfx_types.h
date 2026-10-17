/* SPDX-License-Identifier: MIT */
/* OpenGfx graphics.library: the shared structure layouts, with no Amiga
 * headers. Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Clean-room: the field order and sizes of the NDK 3.2 headers'
 * published structures (exec/nodes.h, exec/lists.h, exec/libraries.h,
 * exec/interrupts.h, exec/semaphores.h, graphics/gfx.h, graphics/rastport.h,
 * graphics/view.h's ColorMap, graphics/view.h's PaletteExtra,
 * graphics/gfxbase.h, utility/tagitem.h), written out here in stdint types
 * so the lean C++ cores of every area (face, display, text, gels) can use
 * them with no Amiga header. On the 68k (2-byte alignment, big-endian,
 * 32-bit pointers) every offset is checked below; tests/GfxFaceABITest.c
 * prints the NDK's own offsetof for the side-by-side check (PROBE).
 *
 * Owned by gfx-face (library/graphics/OWNERS.md). Other areas define the
 * structures only they use (View, ViewPort, TextFont, GelsInfo, ...) in
 * their own headers. */
#ifndef OPENGFX_GFX_TYPES_H
#define OPENGFX_GFX_TYPES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
#define GFX_STATIC_ASSERT(c, m) static_assert(c, m)
extern "C" {
#else
#define GFX_STATIC_ASSERT(c, m) _Static_assert(c, m)
#endif

/* ---- exec ------------------------------------------------------------ */

struct gfx_node {                       /* struct Node, 14 bytes */
    struct gfx_node *succ, *pred;
    uint8_t type;
    int8_t pri;
    char *name;
};

struct gfx_minnode {                    /* struct MinNode, 8 bytes */
    struct gfx_minnode *succ, *pred;
};

struct gfx_list {                       /* struct List, 14 bytes */
    struct gfx_node *head, *tail, *tailpred;
    uint8_t type;
    uint8_t pad;
};

struct gfx_minlist {                    /* struct MinList, 12 bytes */
    struct gfx_minnode *head, *tail, *tailpred;
};

struct gfx_library {                    /* struct Library, 34 bytes */
    struct gfx_node node;
    uint8_t flags;
    uint8_t pad;
    uint16_t negsize, possize, version, revision;
    void *idstring;
    uint32_t sum;
    uint16_t opencnt;
};

struct gfx_interrupt {                  /* struct Interrupt, 22 bytes */
    struct gfx_node node;
    void *data;
    void (*code)(void);
};

struct gfx_semaphore {                  /* struct SignalSemaphore, 46 bytes */
    struct gfx_node link;
    int16_t nestcount;
    struct gfx_minlist waitqueue;
    struct gfx_minnode multiplelink;    /* SemaphoreRequest: MinNode, then the waiter */
    void *multiplewaiter;
    void *owner;
    int16_t queuecount;
};

struct gfx_tagitem {                    /* struct TagItem, 8 bytes */
    uint32_t tag;
    uint32_t data;
};
enum { GFX_TAG_DONE = 0, GFX_TAG_IGNORE = 1, GFX_TAG_MORE = 2, GFX_TAG_SKIP = 3, GFX_TAG_USER = 0x80000000u };

/* exec's MEMF_ flags, as gfx_env's alloc takes them. */
enum {
    GFX_MEMF_ANY = 0, GFX_MEMF_PUBLIC = 1, GFX_MEMF_CHIP = 2, GFX_MEMF_FAST = 4,
    GFX_MEMF_CLEAR = 0x10000, GFX_MEMF_LARGEST = 0x20000, GFX_MEMF_TOTAL = 0x80000
};

/* ---- graphics -------------------------------------------------------- */

struct gfx_rectangle {                  /* struct Rectangle, 8 bytes */
    int16_t minx, miny, maxx, maxy;
};

struct gfx_bitmap {                     /* struct BitMap, 40 bytes */
    uint16_t bytesperrow;
    uint16_t rows;
    uint8_t flags;
    uint8_t depth;
    uint16_t pad;
    uint8_t *planes[8];
};
/* AllocBitMap's flags and GetBitMapAttr's attributes (graphics/gfx.h). */
enum {
    GFX_BMF_CLEAR = 1, GFX_BMF_DISPLAYABLE = 2, GFX_BMF_INTERLEAVED = 4, GFX_BMF_STANDARD = 8,
    GFX_BMF_MINPLANES = 16, GFX_BMF_SPECIALFMT = 0x80
};
enum { GFX_BMA_HEIGHT = 0, GFX_BMA_DEPTH = 4, GFX_BMA_WIDTH = 8, GFX_BMA_FLAGS = 12 };

struct gfx_tmpras {                     /* struct TmpRas, 8 bytes */
    uint8_t *rasptr;
    int32_t size;
};

struct gfx_areainfo {                   /* struct AreaInfo, 24 bytes */
    int16_t *vctrtbl;
    int16_t *vctrptr;
    int8_t *flagtbl;
    int8_t *flagptr;
    int16_t count;
    int16_t maxcount;
    int16_t firstx, firsty;
};

struct gfx_rastport {                   /* struct RastPort, 100 bytes */
    void *layer;
    struct gfx_bitmap *bitmap;
    uint16_t *areaptrn;
    struct gfx_tmpras *tmpras;
    struct gfx_areainfo *areainfo;
    void *gelsinfo;
    uint8_t mask;
    int8_t fgpen;
    int8_t bgpen;
    int8_t aolpen;
    int8_t drawmode;
    int8_t areaptsz;
    int8_t linpatcnt;
    int8_t dummy;
    uint16_t flags;
    uint16_t lineptrn;
    int16_t cp_x, cp_y;
    uint8_t minterms[8];
    int16_t penwidth, penheight;
    void *font;
    uint8_t algostyle;
    uint8_t txflags;
    uint16_t txheight, txwidth, txbaseline;
    int16_t txspacing;
    void **rp_user;
    uint32_t longreserved[2];
    uint16_t wordreserved[7];
    uint8_t reserved[8];
};
/* DrawMode bits and RastPort Flags (graphics/rastport.h). */
enum { GFX_JAM1_MODE = 0, GFX_JAM2_MODE = 1, GFX_COMPLEMENT = 2, GFX_INVERSVID = 4 };
enum { GFX_FRST_DOT = 1, GFX_ONE_DOT = 2, GFX_DBUFFER = 4, GFX_AREAOUTLINE = 8, GFX_NOCROSSFILL = 0x20 };

struct gfx_paletteextra {               /* struct PaletteExtra, 68 bytes */
    struct gfx_semaphore semaphore;
    uint16_t firstfree;
    uint16_t nfree;
    uint16_t firstshared;
    uint16_t nshared;
    uint8_t *refcnt;
    uint8_t *alloclist;
    void *viewport;
    uint16_t sharablecolors;
};

struct gfx_colormap {                   /* struct ColorMap, 52 bytes */
    uint8_t flags;
    uint8_t type;
    uint16_t count;
    uint16_t *colortable;               /* 12-bit 0RGB, the high nibbles */
    void *vpe;                          /* struct ViewPortExtra * */
    uint16_t *lowcolorbits;             /* the low nibbles, V39 and up */
    uint8_t transparencyplane;
    uint8_t spriteresolution;
    uint8_t spriteresdefault;
    uint8_t auxflags;
    void *vp;                           /* struct ViewPort * */
    void *normaldisplayinfo;
    void *coercedisplayinfo;
    struct gfx_tagitem *batch_items;
    uint32_t vpmodeid;
    struct gfx_paletteextra *palextra;
    uint16_t spritebase_even;
    uint16_t spritebase_odd;
    uint16_t bp_0_base;
    uint16_t bp_1_base;
};
enum { GFX_COLORMAP_TYPE_V1_2 = 0, GFX_COLORMAP_TYPE_V36 = 1, GFX_COLORMAP_TYPE_V39 = 2 };

/* struct GfxBase (graphics/gfxbase.h), as far as V40 publishes it. */
struct gfx_base {
    struct gfx_library libnode;                 /*   0 */
    void *actiview;                             /*  34 struct View * */
    void *copinit;                              /*  38 */
    int32_t *cia;                               /*  42 */
    int32_t *blitter;                           /*  46 */
    uint16_t *loflist;                          /*  50 */
    uint16_t *shflist;                          /*  54 */
    void *blthd, *blttl;                        /*  58 struct bltnode * */
    void *bsblthd, *bsblttl;                    /*  66 */
    struct gfx_interrupt vbsrv, timsrv, bltsrv; /*  74 */
    struct gfx_list textfonts;                  /* 140 */
    void *defaultfont;                          /* 154 struct TextFont * */
    uint16_t modes;                             /* 158 */
    int8_t vblank;                              /* 160 */
    int8_t debug;                               /* 161 */
    int16_t beamsync;                           /* 162 */
    int16_t system_bplcon0;                     /* 164 */
    uint8_t spritereserved;                     /* 166 */
    uint8_t bytereserved;                       /* 167 */
    uint16_t flags;                             /* 168 */
    int16_t blitlock;                           /* 170 */
    int16_t blitnest;                           /* 172 */
    struct gfx_list blitwaitq;                  /* 174 */
    void *blitowner;                            /* 188 struct Task * */
    struct gfx_list tof_waitq;                  /* 192 */
    uint16_t displayflags;                      /* 206 */
    void **simplesprites;                       /* 208 */
    uint16_t maxdisplayrow;                     /* 212 */
    uint16_t maxdisplaycolumn;                  /* 214 */
    uint16_t normaldisplayrows;                 /* 216 */
    uint16_t normaldisplaycolumns;              /* 218 */
    uint16_t normaldpmx;                        /* 220 */
    uint16_t normaldpmy;                        /* 222 */
    struct gfx_semaphore *lastchancememory;     /* 224 */
    uint16_t *lcmptr;                           /* 228 */
    uint16_t microsperline;                     /* 232 */
    uint16_t mindisplaycolumn;                  /* 234 */
    uint8_t chiprevbits0;                       /* 236 */
    uint8_t memtype;                            /* 237 */
    uint8_t crb_reserved[4];                    /* 238 */
    uint16_t monitor_id;                        /* 242 */
    uint32_t hedley[8];                         /* 244 */
    uint32_t hedley_sprites[8];                 /* 276 */
    uint32_t hedley_sprites1[8];                /* 308 */
    int16_t hedley_count;                       /* 340 */
    uint16_t hedley_flags;                      /* 342 */
    int16_t hedley_tmp;                         /* 344 */
    int16_t *hash_table;                        /* 346 */
    uint16_t current_tot_rows;                  /* 350 */
    uint16_t current_tot_cclks;                 /* 352 */
    uint8_t hedley_hint;                        /* 354 */
    uint8_t hedley_hint2;                       /* 355 */
    uint32_t nreserved[4];                      /* 356 */
    int32_t *a2024_sync_raster;                 /* 372 */
    uint16_t control_delta_pal;                 /* 376 */
    uint16_t control_delta_ntsc;                /* 378 */
    void *current_monitor;                      /* 380 struct MonitorSpec * */
    struct gfx_list monitorlist;                /* 384 */
    void *default_monitor;                      /* 398 */
    struct gfx_semaphore *monitorlistsemaphore; /* 402 */
    void *displayinfodatabase;                  /* 406 */
    uint16_t topline;                           /* 410 */
    struct gfx_semaphore *actiewcprsemaphore;   /* 412 ActiViewCprSemaphore */
    uint32_t *utilbase;                         /* 416 */
    uint32_t *execbase;                         /* 420 */
    uint8_t *bwshifts;                          /* 424 */
    uint16_t *strtfetchmasks;                   /* 428 */
    uint16_t *stopfetchmasks;                   /* 432 */
    uint16_t *overrun;                          /* 436 */
    int16_t *realstops;                         /* 440 */
    uint16_t spritewidth;                       /* 444 */
    uint16_t spritefmode;                       /* 446 */
    int8_t softsprites;                         /* 448 */
    int8_t arraywidth;                          /* 449 */
    uint16_t defaultspritewidth;                /* 450 */
    int8_t sprmovedisable;                      /* 452 */
    uint8_t wantchips;                          /* 453 */
    uint8_t boardmemtype;                       /* 454 */
    uint8_t bugs;                               /* 455 */
    uint32_t *gb_layersbase;                    /* 456 */
    uint32_t colormask;                         /* 460 */
    void *ivector;                              /* 464 */
    void *idata;                                /* 468 */
    uint32_t specialcounter;                    /* 472 */
    void *dblist;                               /* 476 */
    uint16_t monitorflags;                      /* 480 */
    uint8_t scandoubledsprites;                 /* 482 */
    uint8_t bp3bits;                            /* 483 */
    uint16_t monitorvblank_start;               /* 484 struct AnalogSignalInterval */
    uint16_t monitorvblank_stop;                /* 486 */
    void *natural_monitor;                      /* 488 */
    void *progdata;                             /* 492 */
    uint8_t extsprites;                         /* 496 */
    uint8_t pad3;                               /* 497 */
    uint16_t gfxflags;                          /* 498 */
    uint32_t vbcounter;                         /* 500 */
    struct gfx_semaphore *hashtablesemaphore;   /* 504 */
    uint32_t *hwemul[9];                        /* 508 */
};                                              /* 544 */

struct gfx_rgnrect {                 /* struct RegionRectangle, 16 bytes */
    struct gfx_rgnrect *next, *prev;
    struct gfx_rectangle bounds;
};

struct gfx_rgn {                     /* struct Region, 12 bytes */
    struct gfx_rectangle bounds;
    struct gfx_rgnrect *rects;
};

struct gfx_cliprect {                   /* struct ClipRect, 40 bytes */
    struct gfx_cliprect *next;
    struct gfx_cliprect *prev;
    void *lobs;                         /* obscured: the layer in front, or 0 */
    struct gfx_bitmap *bitmap;          /* a smart refresh piece's own pixels */
    struct gfx_rectangle bounds;        /* on the screen */
    void *p1, *p2;
    int32_t reserved;
    int32_t flags;                      /* layers': GFX_CR_COMPOSED */
};
/* a composited layer's piece (include/graphics/openlayers.h CR_OPENLAYERS_COMPOSED) */
#define GFX_CR_COMPOSED 0x40000000L

struct gfx_layer {                      /* struct Layer, 160 bytes */
    struct gfx_layer *front, *back;
    struct gfx_cliprect *cliprect;
    struct gfx_rastport *rp;
    struct gfx_rectangle bounds;
    uint8_t reserved[4];
    uint16_t priority;
    uint16_t flags;
    struct gfx_bitmap *superbitmap;
    struct gfx_cliprect *superclip;
    void *window;
    int16_t scroll_x, scroll_y;
    struct gfx_cliprect *cr, *cr2, *crnew;
    struct gfx_cliprect *supersavecliprects;
    struct gfx_cliprect *cliprects_;
    void *layerinfo;
    struct gfx_semaphore lock;
    void *backfill;                     /* struct Hook *, or LAYERS_NOBACKFILL (1) */
    uint32_t reserved1;
    struct gfx_rgn *clipregion;
    struct gfx_rgn *savecliprects;
    int16_t width, height;
    uint8_t reserved2[18];
    struct gfx_rgn *damagelist;
};
enum { GFX_LAYERSIMPLE = 1, GFX_LAYERSMART = 2, GFX_LAYERSUPER = 4, GFX_LAYERREFRESH = 0x80 };

/* GfxBase's ChipRevBits0, DisplayFlags and MemType values. */
enum { GFX_GFXF_BIG_BLITS = 1, GFX_GFXF_HR_AGNUS = 1, GFX_GFXF_HR_DENISE = 2, GFX_GFXF_AA_ALICE = 4,
       GFX_GFXF_AA_LISA = 8, GFX_GFXF_AA_MLISA = 16 };
enum { GFX_NTSC = 1, GFX_GENLOC = 2, GFX_PAL = 4, GFX_TODA_SAFE = 8, GFX_REALLY_PAL = 16,
       GFX_LPEN_SWAP_FRAMES = 32 };

#if defined(__m68k__) || defined(__mc68000__) || defined(mc68000)
#define GFX_OFF(s, f, o) GFX_STATIC_ASSERT(offsetof(struct s, f) == (o), #s "." #f " is at " #o)
#define GFX_SIZE(s, n) GFX_STATIC_ASSERT(sizeof(struct s) == (n), #s " is " #n " bytes")
GFX_SIZE(gfx_node, 14);
GFX_SIZE(gfx_list, 14);
GFX_SIZE(gfx_minlist, 12);
GFX_SIZE(gfx_library, 34);
GFX_SIZE(gfx_interrupt, 22);
GFX_SIZE(gfx_semaphore, 46);
GFX_SIZE(gfx_tagitem, 8);
GFX_SIZE(gfx_rectangle, 8);
GFX_SIZE(gfx_bitmap, 40);
GFX_SIZE(gfx_tmpras, 8);
GFX_SIZE(gfx_areainfo, 24);
GFX_SIZE(gfx_rastport, 100);
GFX_OFF(gfx_rastport, mask, 24);
GFX_OFF(gfx_rastport, flags, 32);
GFX_OFF(gfx_rastport, cp_x, 36);
GFX_OFF(gfx_rastport, font, 52);
GFX_OFF(gfx_rastport, txspacing, 64);
GFX_OFF(gfx_rastport, longreserved, 70);
GFX_OFF(gfx_rastport, wordreserved, 78);
GFX_SIZE(gfx_rgn, 12);
GFX_SIZE(gfx_rgnrect, 16);
GFX_SIZE(gfx_cliprect, 40);
GFX_SIZE(gfx_layer, 160);
GFX_OFF(gfx_layer, flags, 30);
GFX_OFF(gfx_layer, scroll_x, 44);
GFX_OFF(gfx_layer, lock, 72);
GFX_OFF(gfx_layer, backfill, 118);
GFX_OFF(gfx_layer, clipregion, 126);
GFX_OFF(gfx_layer, damagelist, 156);
GFX_SIZE(gfx_paletteextra, 68);
GFX_OFF(gfx_paletteextra, refcnt, 54);
GFX_SIZE(gfx_colormap, 52);
GFX_OFF(gfx_colormap, vp, 20);
GFX_OFF(gfx_colormap, palextra, 40);
GFX_OFF(gfx_base, actiview, 34);
GFX_OFF(gfx_base, vbsrv, 74);
GFX_OFF(gfx_base, textfonts, 140);
GFX_OFF(gfx_base, defaultfont, 154);
GFX_OFF(gfx_base, blitwaitq, 174);
GFX_OFF(gfx_base, tof_waitq, 192);
GFX_OFF(gfx_base, displayflags, 206);
GFX_OFF(gfx_base, chiprevbits0, 236);
GFX_OFF(gfx_base, monitorlist, 384);
GFX_OFF(gfx_base, utilbase, 416);
GFX_OFF(gfx_base, execbase, 420);
GFX_OFF(gfx_base, gb_layersbase, 456);
GFX_OFF(gfx_base, extsprites, 496);
GFX_OFF(gfx_base, hwemul, 508);
GFX_SIZE(gfx_base, 544);
#endif

#ifdef __cplusplus
}
#endif
#endif
