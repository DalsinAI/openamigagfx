/* SPDX-License-Identifier: MIT */
/* OpenLayers: an open layers.library for AmigaOS 3.2, API and ABI
 * compatible. Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Clean-room: written from layers.library's autodocs, the NDK 3.2 headers'
 * published layouts and OS 3.2's answers measured by tests/LayersABITest,
 * never from Kickstart's code or bytes. The work is done in layers_lib.c
 * and layers_core.c, which are plain C tested under qemu-m68k
 * (tests/m68k/test_layers_core.sh, test_layers_lib.sh); this file is the
 * library around them: the RomTag, the vector table in layers.library's
 * LVO order (layers.lvo, which build-amiga.sh checks against the NDK's
 * layers_lib.fd), and each call taking its arguments in the documented
 * registers.
 *
 * LIBNAME is "layers.library" for the replacement and "openlayers.library"
 * for the side-by-side build that tests/LayersABITest opens next to OS
 * 3.2's own. layers.library is in ROM, so the replacement goes in at boot
 * as a resident module (OpenModule, plan section 3), never from LIBS:. */
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <graphics/gfxbase.h>
#include <graphics/clip.h>
#include <graphics/layers.h>
#include <graphics/regions.h>
#include <utility/hooks.h>
#include <proto/exec.h>

#include "layers_lib.h"
#include "ol_sysbase.h"

#define REG(r, decl) register decl __asm(#r)

#ifndef LIBNAME
#define LIBNAME "layers.library"
#endif
#define LIB_VERSION  47
#define LIB_REVISION 101                /* 47.100 and up: OpenLayers; 47.101: the composited mode */

/* Everything the library keeps is here (or on the stack): no writable
 * globals, so it can run from ROM. Each call reaches it through A6. */
struct OLBase {
    struct Library lib;
    BPTR seglist;
    struct ol_base ol;          /* graphics.library's base, the Layer_Infos' extra state */
};

/* Run as a program, the library does nothing. This must stay the first code
 * in the file (-fno-toplevel-reorder). */
int start(void) { return -1; }

static const char lib_name[] = LIBNAME;
static const char lib_id[] = "layers.library 47.101 (17.10.2026) OpenLayers, AmigaChrome Team\r\n";
static const char lib_ver[] __attribute__((used)) =
    "\0$VER: layers.library 47.101 (17.10.2026) OpenLayers, AmigaChrome Team";
static const char gfx_name[] = "graphics.library";

/* A hook as utility.library calls one: A0 the hook, A2 the object, A1 the
 * message, through h_Entry. */
ULONG ol_call_hook(struct Hook *hook, APTR obj, APTR msg)
{
    register struct Hook *a0 __asm("a0") = hook;
    register APTR a1 __asm("a1") = msg;
    register APTR a2 __asm("a2") = obj;
    register ULONG d0 __asm("d0");
    __asm volatile("move.l %%a6,-(%%sp)\n\t"
                   "move.l 8(%1),%%a6\n\t"
                   "jsr (%%a6)\n\t"
                   "move.l (%%sp)+,%%a6"
                   : "=r"(d0), "+r"(a0), "+r"(a1)
                   : "r"(a2)
                   : "d1", "cc", "memory");
    return d0;
}

/* ---- Library housekeeping -------------------------------------------- */

static struct Library *lib_init(REG(d0, struct OLBase *base), REG(a0, BPTR seglist),
                                REG(a6, struct ExecBase *sys))
{
    struct GfxBase *gfx;

    (void)sys;
    if (!(gfx = (struct GfxBase *)OpenLibrary((CONST_STRPTR)gfx_name, 39))) return 0;
    ol_init(&base->ol, gfx);
    base->seglist = seglist;
    base->lib.lib_Node.ln_Type = NT_LIBRARY;
    base->lib.lib_Node.ln_Name = (char *)lib_name;
    base->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->lib.lib_Version = LIB_VERSION;
    base->lib.lib_Revision = LIB_REVISION;
    base->lib.lib_IdString = (APTR)lib_id;
    return &base->lib;
}

static struct Library *lib_open(REG(a6, struct OLBase *base))
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return &base->lib;
}

static BPTR lib_expunge(REG(a6, struct OLBase *base));

static BPTR lib_close(REG(a6, struct OLBase *base))
{
    if (base->lib.lib_OpenCnt)
        base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge(base);
    return 0;
}

/* The replacement is the system's and stays; the side-by-side build goes
 * once nobody has it open. */
static BPTR lib_expunge(REG(a6, struct OLBase *base))
{
    BPTR seglist;

    if (base->lib.lib_OpenCnt || !base->seglist || lib_name[0] == 'l') {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    CloseLibrary((struct Library *)base->ol.gfx);
    FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seglist;
}

static ULONG lib_null(void)
{
    return 0;
}

/* ---- The calls, in their documented registers ------------------------ */

/* Each takes the library base in A6, as every library call does, and hands
 * the calls its state. */
#define OB REG(a6, struct OLBase *base)

static void L_InitLayers(REG(a0, struct Layer_Info *li), OB) { ol_InitLayers(&base->ol, li); }

static struct Layer *L_CreateUpfrontLayer(REG(a0, struct Layer_Info *li), REG(a1, struct BitMap *bm),
                                          REG(d0, LONG x0), REG(d1, LONG y0), REG(d2, LONG x1), REG(d3, LONG y1),
                                          REG(d4, LONG flags), REG(a2, struct BitMap *sbm), OB)
{
    return ol_CreateUpfrontLayer(&base->ol, li, bm, x0, y0, x1, y1, flags, sbm);
}

static struct Layer *L_CreateBehindLayer(REG(a0, struct Layer_Info *li), REG(a1, struct BitMap *bm),
                                         REG(d0, LONG x0), REG(d1, LONG y0), REG(d2, LONG x1), REG(d3, LONG y1),
                                         REG(d4, LONG flags), REG(a2, struct BitMap *sbm), OB)
{
    return ol_CreateBehindLayer(&base->ol, li, bm, x0, y0, x1, y1, flags, sbm);
}

static LONG L_UpfrontLayer(REG(a0, LONG dummy), REG(a1, struct Layer *l), OB) { return ol_UpfrontLayer(&base->ol, l); }
static LONG L_BehindLayer(REG(a0, LONG dummy), REG(a1, struct Layer *l), OB) { return ol_BehindLayer(&base->ol, l); }

static LONG L_MoveLayer(REG(a0, LONG dummy), REG(a1, struct Layer *l), REG(d0, LONG dx), REG(d1, LONG dy), OB)
{
    return ol_MoveLayer(&base->ol, l, dx, dy);
}

static LONG L_SizeLayer(REG(a0, LONG dummy), REG(a1, struct Layer *l), REG(d0, LONG dx), REG(d1, LONG dy), OB)
{
    return ol_SizeLayer(&base->ol, l, dx, dy);
}

static void L_ScrollLayer(REG(a0, LONG dummy), REG(a1, struct Layer *l), REG(d0, LONG dx), REG(d1, LONG dy), OB)
{
    ol_ScrollLayer(&base->ol, l, dx, dy);
}

static LONG L_BeginUpdate(REG(a0, struct Layer *l), OB) { return ol_BeginUpdate(&base->ol, l); }
static void L_EndUpdate(REG(a0, struct Layer *l), REG(d0, UWORD flag), OB) { ol_EndUpdate(&base->ol, l, flag); }
static LONG L_DeleteLayer(REG(a0, LONG dummy), REG(a1, struct Layer *l), OB) { return ol_DeleteLayer(&base->ol, l); }
static void L_LockLayer(REG(a0, LONG dummy), REG(a1, struct Layer *l), OB) { ol_LockLayer(&base->ol, l); }
static void L_UnlockLayer(REG(a0, struct Layer *l), OB) { ol_UnlockLayer(&base->ol, l); }
static void L_LockLayers(REG(a0, struct Layer_Info *li), OB) { ol_LockLayers(&base->ol, li); }
static void L_UnlockLayers(REG(a0, struct Layer_Info *li), OB) { ol_UnlockLayers(&base->ol, li); }
static void L_LockLayerInfo(REG(a0, struct Layer_Info *li), OB) { ol_LockLayerInfo(&base->ol, li); }

static void L_SwapBitsRastPortClipRect(REG(a0, struct RastPort *rp), REG(a1, struct ClipRect *cr), OB)
{
    ol_SwapBitsRastPortClipRect(&base->ol, rp, cr);
}

/* x and y are WORDs (NDK 3.2's layers protos): a caller built with them
 * sets only d0.w and d1.w, and OpenIntuition's click hit test was measured
 * passing garbage in the upper halves, so only the low words are read. */
static struct Layer *L_WhichLayer(REG(a0, struct Layer_Info *li), REG(d0, LONG x), REG(d1, LONG y), OB)
{
    return ol_WhichLayer(&base->ol, li, (WORD)x, (WORD)y);
}

static void L_UnlockLayerInfo(REG(a0, struct Layer_Info *li), OB) { ol_UnlockLayerInfo(&base->ol, li); }
static struct Layer_Info *L_NewLayerInfo(OB) { return ol_NewLayerInfo(&base->ol); }
static void L_DisposeLayerInfo(REG(a0, struct Layer_Info *li), OB) { ol_DisposeLayerInfo(&base->ol, li); }
static LONG L_FattenLayerInfo(REG(a0, struct Layer_Info *li), OB) { return ol_FattenLayerInfo(&base->ol, li); }
static void L_ThinLayerInfo(REG(a0, struct Layer_Info *li), OB) { ol_ThinLayerInfo(&base->ol, li); }

static LONG L_MoveLayerInFrontOf(REG(a0, struct Layer *l), REG(a1, struct Layer *other), OB)
{
    return ol_MoveLayerInFrontOf(&base->ol, l, other);
}

static struct Region *L_InstallClipRegion(REG(a0, struct Layer *l), REG(a1, struct Region *region), OB)
{
    return ol_InstallClipRegion(&base->ol, l, region);
}

static LONG L_MoveSizeLayer(REG(a0, struct Layer *l), REG(d0, LONG dx), REG(d1, LONG dy), REG(d2, LONG dw),
                            REG(d3, LONG dh), OB)
{
    return ol_MoveSizeLayer(&base->ol, l, dx, dy, dw, dh);
}

static struct Layer *L_CreateUpfrontHookLayer(REG(a0, struct Layer_Info *li), REG(a1, struct BitMap *bm),
                                              REG(d0, LONG x0), REG(d1, LONG y0), REG(d2, LONG x1),
                                              REG(d3, LONG y1), REG(d4, LONG flags), REG(a3, struct Hook *hook),
                                              REG(a2, struct BitMap *sbm), OB)
{
    return ol_CreateUpfrontHookLayer(&base->ol, li, bm, x0, y0, x1, y1, flags, hook, sbm);
}

static struct Layer *L_CreateBehindHookLayer(REG(a0, struct Layer_Info *li), REG(a1, struct BitMap *bm),
                                             REG(d0, LONG x0), REG(d1, LONG y0), REG(d2, LONG x1),
                                             REG(d3, LONG y1), REG(d4, LONG flags), REG(a3, struct Hook *hook),
                                             REG(a2, struct BitMap *sbm), OB)
{
    return ol_CreateBehindHookLayer(&base->ol, li, bm, x0, y0, x1, y1, flags, hook, sbm);
}

static struct Hook *L_InstallLayerHook(REG(a0, struct Layer *l), REG(a1, struct Hook *hook), OB)
{
    return ol_InstallLayerHook(&base->ol, l, hook);
}

static struct Hook *L_InstallLayerInfoHook(REG(a0, struct Layer_Info *li), REG(a1, struct Hook *hook), OB)
{
    return ol_InstallLayerInfoHook(&base->ol, li, hook);
}

/* dx and dy are WORDs (NDK 3.2): only the low words are read */
static void L_SortLayerCR(REG(a0, struct Layer *l), REG(d0, LONG dx), REG(d1, LONG dy), OB)
{
    ol_SortLayerCR(&base->ol, l, (WORD)dx, (WORD)dy);
}

static void L_DoHookClipRects(REG(a0, struct Hook *hook), REG(a1, struct RastPort *rp),
                              REG(a2, const struct Rectangle *rect), OB)
{
    ol_DoHookClipRects(&base->ol, hook, rp, rect);
}

static LONG L_LayerOccluded(REG(a0, struct Layer *l), OB) { return ol_LayerOccluded(&base->ol, l); }
static LONG L_HideLayer(REG(a0, struct Layer *l), OB) { return ol_HideLayer(&base->ol, l); }

static LONG L_ShowLayer(REG(a0, struct Layer *l), REG(a1, struct Layer *in_front_of), OB)
{
    return ol_ShowLayer(&base->ol, l, in_front_of);
}

static LONG L_SetLayerInfoBounds(REG(a0, struct Layer_Info *li), REG(a1, const struct Rectangle *bounds), OB)
{
    return ol_SetLayerInfoBounds(&base->ol, li, bounds);
}

#ifdef OL_COMPOSITE
/* Our own calls (include/graphics/openlayers.h): the composited mode */
static LONG L_ComposeLayerInfo(REG(a0, struct Layer_Info *li), REG(a1, struct BitMap *bm), REG(d0, ULONG flags), OB)
{
    return ol_ComposeLayerInfo(&base->ol, li, bm, flags);
}

static LONG L_SetLayerOpacity(REG(a0, struct Layer *l), REG(d0, ULONG opacity), OB)
{
    return ol_SetLayerOpacity(&base->ol, l, opacity);
}

static ULONG L_SetLayerLook(REG(a0, struct Layer *l), REG(d0, ULONG what), REG(d1, ULONG value), OB)
{
    return ol_SetLayerLook(&base->ol, l, what, value);
}
#endif

#undef OB

/* layers.library's LVO order; layers.lvo says the same, line for line. */
static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    (APTR)L_InitLayers,                 /* -30 */
    (APTR)L_CreateUpfrontLayer,         /* -36 */
    (APTR)L_CreateBehindLayer,          /* -42 */
    (APTR)L_UpfrontLayer,               /* -48 */
    (APTR)L_BehindLayer,                /* -54 */
    (APTR)L_MoveLayer,                  /* -60 */
    (APTR)L_SizeLayer,                  /* -66 */
    (APTR)L_ScrollLayer,                /* -72 */
    (APTR)L_BeginUpdate,                /* -78 */
    (APTR)L_EndUpdate,                  /* -84 */
    (APTR)L_DeleteLayer,                /* -90 */
    (APTR)L_LockLayer,                  /* -96 */
    (APTR)L_UnlockLayer,                /* -102 */
    (APTR)L_LockLayers,                 /* -108 */
    (APTR)L_UnlockLayers,               /* -114 */
    (APTR)L_LockLayerInfo,              /* -120 */
    (APTR)L_SwapBitsRastPortClipRect,   /* -126 */
    (APTR)L_WhichLayer,                 /* -132 */
    (APTR)L_UnlockLayerInfo,            /* -138 */
    (APTR)L_NewLayerInfo,               /* -144 */
    (APTR)L_DisposeLayerInfo,           /* -150 */
    (APTR)L_FattenLayerInfo,            /* -156 */
    (APTR)L_ThinLayerInfo,              /* -162 */
    (APTR)L_MoveLayerInFrontOf,         /* -168 */
    (APTR)L_InstallClipRegion,          /* -174 */
    (APTR)L_MoveSizeLayer,              /* -180 */
    (APTR)L_CreateUpfrontHookLayer,     /* -186 */
    (APTR)L_CreateBehindHookLayer,      /* -192 */
    (APTR)L_InstallLayerHook,           /* -198 */
    (APTR)L_InstallLayerInfoHook,       /* -204 */
    (APTR)L_SortLayerCR,                /* -210 */
    (APTR)L_DoHookClipRects,            /* -216 */
    (APTR)L_LayerOccluded,              /* -222 */
    (APTR)L_HideLayer,                  /* -228 */
    (APTR)L_ShowLayer,                  /* -234 */
    (APTR)L_SetLayerInfoBounds,         /* -240 */
#ifdef OL_COMPOSITE
    (APTR)L_ComposeLayerInfo,           /* -246, ours */
    (APTR)L_SetLayerOpacity,            /* -252, ours */
    (APTR)L_SetLayerLook,               /* -258, ours */
#endif
    (APTR)-1
};

static const struct {
    ULONG size;
    const APTR *vectors;
    APTR data;
    APTR init;
} lib_inittable = {
    sizeof(struct OLBase), lib_vectors, NULL, (APTR)lib_init
};

const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, LIB_VERSION, NT_LIBRARY, 0,
    (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable
};
