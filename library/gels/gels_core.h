/* SPDX-License-Identifier: MIT */
/* OpenGfx gels: graphics.library's GELs (VSprites, Bobs, AnimObs), their
 * collisions, and the blitter: its queue (QBlit, QBSBlit), its ownership
 * (OwnBlitter, DisownBlitter, WaitBlit) and the blits drawn through it
 * (BltClear, BltTemplate, BltMaskBitMapRastPort). Copyright (c) 2026
 * Dalsin Limited. MIT.
 *
 * Clean-room, from the NDK 3.2 headers' published layouts (graphics/gels.h,
 * graphics/rastport.h's GelsInfo, hardware/blit.h's bltnode), the autodocs,
 * the Hardware Reference Manual's blitter and measured OS 3.2 behaviour
 * (tests/GelsABITest.c). Never from Kickstart, Hyperion or AROS code.
 *
 * gels_core.cpp is lean C++ with no Amiga headers. It reaches exec and the
 * custom chips only through struct gfx_env (library/graphics/gfx_env.h)
 * and the few calls below that need registers (struct gels_state). It
 * keeps nothing in globals: the blitter queue's state is GfxBase's own
 * fields, and the rest is in struct gels_state, which gfx_init_gels
 * allocates into gfx_private_of(GfxBase)->area[GFX_AREA_GELS]. */
#ifndef OPENGFX_GELS_CORE_H
#define OPENGFX_GELS_CORE_H

#include "gfx_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- graphics/gels.h ------------------------------------------------- */

struct gl_bob;
struct gl_animcomp;
struct gl_animob;

struct gl_vsprite {                     /* struct VSprite, 60 bytes */
    struct gl_vsprite *next, *prev;     /* NextVSprite, PrevVSprite: the gel list, sorted by y, x */
    struct gl_vsprite *drawpath;        /* system: unused here */
    struct gl_vsprite *clearpath;       /* system: the next Bob to erase (see DrawGList) */
    int16_t oldy, oldx;                 /* where the background in SaveBuffer came from */
    int16_t flags;
    int16_t y, x;
    int16_t height;
    int16_t width;                      /* in words */
    int16_t depth;                      /* planes of ImageData */
    int16_t memask, hitmask;
    int16_t *imagedata;
    int16_t *borderline;                /* width words: every row of CollMask ORed */
    int16_t *collmask;                  /* width x height words: every plane ORed */
    int16_t *sprcolors;                 /* a VSprite's three colours, or 0 */
    struct gl_bob *vsbob;
    int8_t planepick, planeonoff;
    int16_t vuserext;
};

/* VSprite flags: the user's */
enum { GL_VSPRITE = 0x0001, GL_SAVEBACK = 0x0002, GL_OVERLAY = 0x0004, GL_MUSTDRAW = 0x0008 };
/* and the system's */
enum { GL_BACKSAVED = 0x0100, GL_BOBUPDATE = 0x0200, GL_GELGONE = 0x0400, GL_VSOVERFLOW = 0x0800 };

struct gl_dbufpacket {                  /* struct DBufPacket, 12 bytes */
    int16_t bufy, bufx;
    struct gl_vsprite *bufpath;
    int16_t *bufbuffer;
};

struct gl_bob {                         /* struct Bob, 32 bytes */
    int16_t flags;
    int16_t *savebuffer;
    int16_t *imageshadow;
    struct gl_bob *before, *after;
    struct gl_vsprite *bobvsprite;
    struct gl_animcomp *bobcomp;
    struct gl_dbufpacket *dbuffer;
    int16_t buserext;
};

/* Bob flags: the user's */
enum { GL_SAVEBOB = 0x0001, GL_BOBISCOMP = 0x0002 };
/* and the system's */
enum { GL_BWAITING = 0x0100, GL_BDRAWN = 0x0200, GL_BOBSAWAY = 0x0400, GL_BOBNIX = 0x0800,
       GL_SAVEPRESERVE = 0x1000, GL_OUTSTEP = 0x2000 };

struct gl_animcomp {                    /* struct AnimComp, 38 bytes */
    int16_t flags;
    int16_t timer;
    int16_t timeset;
    struct gl_animcomp *nextcomp, *prevcomp;
    struct gl_animcomp *nextseq, *prevseq;
    int16_t (*animcroutine)(struct gl_animcomp *);  /* C, argument on the stack */
    int16_t ytrans, xtrans;
    struct gl_animob *headob;
    struct gl_bob *animbob;
};
enum { GL_RINGTRIGGER = 0x0001, GL_ANFRACSIZE = 6, GL_ANIMHALF = 0x0020 };

struct gl_animob {                      /* struct AnimOb, 42 bytes */
    struct gl_animob *nextob, *prevob;
    int32_t clock;
    int16_t anoldy, anoldx;
    int16_t any, anx;
    int16_t yvel, xvel;
    int16_t yaccel, xaccel;
    int16_t ringytrans, ringxtrans;
    int16_t (*animoroutine)(struct gl_animob *);    /* C, argument on the stack */
    struct gl_animcomp *headcomp;
    int16_t auserext;
};

/* DoCollision's routines are C, arguments on the stack: number 0 is the
 * border's, called (gel, which borders); 1 to 15 a pair's, called (the
 * gel first in the list, the other). */
typedef void (*gl_collfn)(struct gl_vsprite *, void *);
struct gl_colltable {                   /* struct collTable, 64 bytes */
    gl_collfn collptrs[16];
};
enum { GL_BORDERHIT = 0, GL_TOPHIT = 1, GL_BOTTOMHIT = 2, GL_LEFTHIT = 4, GL_RIGHTHIT = 8 };

struct gl_gelsinfo {                    /* struct GelsInfo (graphics/rastport.h), 38 bytes */
    int8_t sprrsrvd;                    /* bit n: hardware sprite n is not for VSprites */
    uint8_t flags;
    struct gl_vsprite *gelhead, *geltail;
    int16_t *nextline;                  /* 8 WORDs: the first line each sprite is free again */
    int16_t **lastcolor;                /* 8 pointers: the colours each sprite last showed */
    struct gl_colltable *collhandler;
    int16_t leftmost, rightmost, topmost, bottommost;
    void *firstblissobj, *lastblissobj;
};

/* ---- hardware/blit.h ------------------------------------------------- */

struct gl_bltnode {                     /* struct bltnode, 18 bytes */
    struct gl_bltnode *n;
    void *function;                     /* called with A0 = 0xDFF000, A1 = the node; D0 = 0 when done */
    int8_t stat;                        /* GL_CLEANUP: call cleanup once done */
    int16_t blitsize;
    int16_t beamsync;                   /* QBSBlit: the beam's line to wait for */
    void *cleanup;                      /* called with A1 = the node? (ABI test) */
};
enum { GL_CLEANUP = 0x40 };

/* What DrawGList hands gfx-display for the VSprites it placed
 * (gfx_hook_vsprites), sorted by y. */
struct gels_vsprite_slot {
    struct gl_vsprite *vs;
    int16_t sprite;                     /* 0 to 7 */
    int16_t y;
    uint16_t flags;                     /* GELS_SLOT_COLORS: load vs->sprcolors into the pair's colours here */
};
enum { GELS_SLOT_COLORS = 1 };

/* ---- the state the library keeps for gels ----------------------------- */

struct gels_state {
    struct gfx_env *env;
    /* the calls that need registers (gels_lvo.c makes them on the Amiga;
     * the tests give C functions) */
    int32_t (*call_blit)(struct gels_state *st, struct gl_bltnode *bn);  /* bn->function: A0 custom, A1 bn */
    void (*call_cleanup)(struct gels_state *st, struct gl_bltnode *bn); /* bn->cleanup: A0 custom, A1 bn */
    /* a hardware blitter at 0xDFF000 (0: none, every blit by the CPU) */
    uint16_t has_blitter;
    /* the QBlit queue's step in progress: the routine asked to be called
     * again (1), and which queue it is from (0 QBlit, 1 QBSBlit) */
    uint16_t q_again;
    uint16_t q_bs;
    uint16_t pad;
    /* the interrupts gels adds: the VERTB server for QBSBlit's beam */
    struct gfx_interrupt vertb;
    struct gfx_interrupt *old_blit;
};

/* gfx_init_gels's work, once st->env, call_blit and call_cleanup are set:
 * the blitter's fields of GfxBase. */
void gels_init(struct gels_state *st);

/* ---- the calls, in plain C arguments (gels_lvo.c takes the registers) */

/* Blitter ownership and the queue. */
void gels_OwnBlitter(struct gels_state *st);
void gels_DisownBlitter(struct gels_state *st);
void gels_WaitBlit(struct gels_state *st);
void gels_QBlit(struct gels_state *st, struct gl_bltnode *bn);
void gels_QBSBlit(struct gels_state *st, struct gl_bltnode *bn);
/* The blitter's interrupt (INTB_BLIT, a handler: it clears INTREQ) and the
 * vertical blank's server, for QBSBlit. */
void gels_blit_int(struct gels_state *st);
void gels_vertb(struct gels_state *st);

/* The blits. */
void gels_BltClear(struct gels_state *st, void *mem, uint32_t bytecount, uint32_t flags);
void gels_BltTemplate(struct gels_state *st, const void *src, int32_t srcx, int32_t srcmod, struct gfx_rastport *rp,
                      int32_t dx, int32_t dy, int32_t w, int32_t h);
void gels_BltMaskBitMapRastPort(struct gels_state *st, struct gfx_bitmap *src, int32_t sx, int32_t sy,
                                struct gfx_rastport *rp, int32_t dx, int32_t dy, int32_t w, int32_t h,
                                uint32_t minterm, const void *mask);

/* The GELs. */
void gels_InitGels(struct gels_state *st, struct gl_vsprite *head, struct gl_vsprite *tail, struct gl_gelsinfo *gi);
void gels_AddVSprite(struct gels_state *st, struct gl_vsprite *vs, struct gfx_rastport *rp);
void gels_AddBob(struct gels_state *st, struct gl_bob *bob, struct gfx_rastport *rp);
void gels_RemVSprite(struct gels_state *st, struct gl_vsprite *vs);
void gels_RemIBob(struct gels_state *st, struct gl_bob *bob, struct gfx_rastport *rp, void *vp);
void gels_SortGList(struct gels_state *st, struct gfx_rastport *rp);
void gels_DrawGList(struct gels_state *st, struct gfx_rastport *rp, void *vp);
void gels_DoCollision(struct gels_state *st, struct gfx_rastport *rp);
void gels_SetCollision(struct gels_state *st, uint32_t num, gl_collfn routine, struct gl_gelsinfo *gi);
void gels_InitMasks(struct gels_state *st, struct gl_vsprite *vs);

/* Animation. */
void gels_AddAnimOb(struct gels_state *st, struct gl_animob *ob, struct gl_animob **key, struct gfx_rastport *rp);
void gels_Animate(struct gels_state *st, struct gl_animob **key, struct gfx_rastport *rp);
int32_t gels_GetGBuffers(struct gels_state *st, struct gl_animob *ob, struct gfx_rastport *rp, int32_t db);
void gels_FreeGBuffers(struct gels_state *st, struct gl_animob *ob, struct gfx_rastport *rp, int32_t db);
void gels_InitGMasks(struct gels_state *st, struct gl_animob *ob);

/* ---- layouts, checked where the alignment is the Amiga's -------------- */
#if defined(__m68k__) || defined(__mc68000__) || defined(mc68000)
#define GL_OFF(s, f, o) GFX_STATIC_ASSERT(offsetof(struct s, f) == (o), #s "." #f " is at " #o)
#define GL_SIZE(s, n) GFX_STATIC_ASSERT(sizeof(struct s) == (n), #s " is " #n " bytes")
GL_SIZE(gl_vsprite, 60);
GL_OFF(gl_vsprite, oldy, 16);
GL_OFF(gl_vsprite, flags, 20);
GL_OFF(gl_vsprite, y, 22);
GL_OFF(gl_vsprite, width, 28);
GL_OFF(gl_vsprite, memask, 32);
GL_OFF(gl_vsprite, imagedata, 36);
GL_OFF(gl_vsprite, sprcolors, 48);
GL_OFF(gl_vsprite, vsbob, 52);
GL_OFF(gl_vsprite, planepick, 56);
GL_OFF(gl_vsprite, vuserext, 58);
GL_SIZE(gl_dbufpacket, 12);
GL_SIZE(gl_bob, 32);
GL_OFF(gl_bob, savebuffer, 2);
GL_OFF(gl_bob, before, 10);
GL_OFF(gl_bob, bobvsprite, 18);
GL_OFF(gl_bob, dbuffer, 26);
GL_SIZE(gl_animcomp, 38);
GL_OFF(gl_animcomp, nextcomp, 6);
GL_OFF(gl_animcomp, animcroutine, 22);
GL_OFF(gl_animcomp, ytrans, 26);
GL_OFF(gl_animcomp, headob, 30);
GL_SIZE(gl_animob, 42);
GL_OFF(gl_animob, clock, 8);
GL_OFF(gl_animob, any, 16);
GL_OFF(gl_animob, ringytrans, 28);
GL_OFF(gl_animob, animoroutine, 32);
GL_OFF(gl_animob, headcomp, 36);
GL_SIZE(gl_colltable, 64);
GL_SIZE(gl_gelsinfo, 38);
GL_OFF(gl_gelsinfo, nextline, 10);
GL_OFF(gl_gelsinfo, collhandler, 18);
GL_OFF(gl_gelsinfo, leftmost, 22);
GL_OFF(gl_gelsinfo, firstblissobj, 30);
GL_SIZE(gl_bltnode, 18);
GL_OFF(gl_bltnode, stat, 8);
GL_OFF(gl_bltnode, blitsize, 10);
GL_OFF(gl_bltnode, beamsync, 12);
GL_OFF(gl_bltnode, cleanup, 14);
GFX_STATIC_ASSERT(offsetof(struct gfx_base, blthd) == 58, "GfxBase.blthd is at 58");
GFX_STATIC_ASSERT(offsetof(struct gfx_base, bltsrv) == 118, "GfxBase.bltsrv is at 118");
GFX_STATIC_ASSERT(offsetof(struct gfx_base, blitlock) == 170, "GfxBase.BlitLock is at 170");
GFX_STATIC_ASSERT(offsetof(struct gfx_base, blitowner) == 188, "GfxBase.BlitOwner is at 188");
#endif

#ifdef __cplusplus
}
#endif
#endif
