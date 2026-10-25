/* SPDX-License-Identifier: MIT */
/* OpenLayers' own calls: the composited mode. Copyright (c) 2026 AmigaChrome
 * Team. MIT.
 *
 * layers.library 47.101 and up (OpenLayers; OS 3.2's layers.library has
 * none of these: check lib_Version 47 and lib_Revision >= 101 first). The
 * calls follow OS 3.2's last LVO (SetLayerInfoBounds, -240). The design is
 * docs/design/compositing.md.
 *
 *  -246 LONG  ComposeLayerInfo(struct Layer_Info *li, struct BitMap *bm, ULONG flags)(a0,a1,d0)
 *  -252 LONG  SetLayerOpacity(struct Layer *layer, ULONG opacity)(a0,d0)
 *  -258 ULONG SetLayerLook(struct Layer *layer, ULONG what, ULONG value)(a0,d0,d1)
 *
 * ComposeLayerInfo switches a Layer_Info with no layers yet to the
 * composited mode (LAYERS_COMPOSE_ON), for layers on the bitmap bm. It
 * answers TRUE when it is composited: bm is an OpenRTG bitmap of 16 or 32
 * bits, opengpu.library's back end (not the CPU) does COMPOSITE fully for
 * its format (RGB565, or stream v1.3's XRGB32), and ENV:OpenPrefs/Windows
 * does not say "composite off". Intuition asks it for public screens only.
 * Without LAYERS_COMPOSE_ON it puts a composited Layer_Info back to
 * classic layers. SetLayerInfoBounds on a composited Layer_Info composes
 * all of it again.
 *
 * 47.102: ComposeLayerInfo(li, NULL, LAYERS_COMPOSE_WHY) changes nothing and
 * answers why li is not composited: LAYERS_WHY_COMPOSITED (0) while it is,
 * otherwise one of the LAYERS_WHY_ reasons below. A program asks it of a
 * screen's &Screen->LayerInfo (OpenPrefs' Windows page does, for the
 * Workbench). 47.101 would take the call as "back to classic layers", so
 * ask only when lib_Revision >= 102.
 *
 * SetLayerOpacity sets a composited layer's opacity, 0 (not seen) to 255
 * (opaque, the default), and answers the old one; -1 for a classic layer,
 * where it does nothing. SetLayerLook sets the rest of a layer's look:
 * LAYERLOOK_OPACITY is SetLayerOpacity; LAYERLOOK_SHADOW, _DRAGGED,
 * _INACTIVE and _ALPHA are switched on by a value of 1 and off by 0, and
 * the answer is the old state (0 or 1). LAYERLOOK_ALL sets them all at
 * once (value: opacity | the LAYERLOOK_ bits << 8), as one compose. ~0 for
 * a classic layer. A layer is composed again only when what is seen of it
 * changes; 47.107: then its own box, and its shadow's only when it had or
 * has one.
 *
 * 47.107: LAYERLOOK_KEYED (32-bit screens) makes a layer's opacity its
 * background's. A keyed layer less than opaque is composed at its opacity
 * as any translucent layer, then its own alpha over that at full: a pixel
 * with alpha 255 stays opaque, one with 0 shows at the layer's opacity, and
 * one between mixes the two. Opaque, it is copied as before, its alpha not
 * read. Intuition keys the screen's bar this way (amigachrome-guest
 * libraries/openintuition: IntuitionControlA's ICTRL_BarOpacity), with the
 * title and the depth gadget at 255 and the background at 0. Pens leave a
 * 32-bit pixel's alpha 0, so what is drawn into a keyed layer with pens
 * fades with it until it is keyed again. With LAYERLOOK_ALPHA as well,
 * ALPHA wins. Ask for it only when lib_Revision >= 107: earlier revisions
 * keep the bit and compose the layer as before. */
#ifndef GRAPHICS_OPENLAYERS_H
#define GRAPHICS_OPENLAYERS_H

#define OPENLAYERS_COMPOSITE_REVISION 101
#define OPENLAYERS_KEYED_REVISION     107      /* LAYERLOOK_KEYED */

#define LAYERS_COMPOSE_ON       1UL
#define LAYERS_COMPOSE_WHY      2UL     /* 47.102: answers a LAYERS_WHY_, changes nothing */

/* Why a Layer_Info is classic (LAYERS_COMPOSE_WHY's answers). */
#define LAYERS_WHY_COMPOSITED   0       /* it is composited */
#define LAYERS_WHY_NOT_ASKED    1       /* never switched on: a custom screen (Intuition asks for public ones only) */
#define LAYERS_WHY_SWITCHED_OFF 2       /* "composite off" in ENV:OpenPrefs/Windows when it was asked, or put back */
#define LAYERS_WHY_NOT_RTG      3       /* not an OpenRTG bitmap: the native chipset (AGA), or a board OpenRTG
                                         * doesn't drive */
#define LAYERS_WHY_8BIT         4       /* an 8-bit (CLUT) screen */
#define LAYERS_WHY_LAYOUT       5       /* a pixel layout OpenGPU's driver doesn't composite fully */
#define LAYERS_WHY_CPU_ONLY     6       /* OpenGPU has no driver, only its CPU back end */
#define LAYERS_WHY_NO_OPENGPU   7       /* opengpu.library or openrtg.library would not open */
#define LAYERS_WHY_NO_MEMORY    8       /* a layer's surface could not be had: it went back to classic layers */

#define LAYERLOOK_OPACITY       0UL     /* 0-255 */
#define LAYERLOOK_SHADOW        1UL     /* a shadow under it (windows and menus: intuition sets it) */
#define LAYERLOOK_DRAGGED       2UL     /* being dragged: the look's drag opacity times its own */
#define LAYERLOOK_INACTIVE      4UL     /* an inactive window: the look's inactive opacity times its own */
#define LAYERLOOK_ALPHA         8UL     /* its surface is ARGB32 and its own alpha counts (32-bit screens) */
#define LAYERLOOK_KEYED         0x10UL  /* 47.107: its opacity fades only what its own alpha leaves clear (32-bit screens) */
#define LAYERLOOK_QUERY         0x100UL /* answers opacity | look << 8 | 1 << 16, changes nothing */
#define LAYERLOOK_ALL           0x200UL /* value: opacity | look << 8, all at once; answers as QUERY */

/* struct ClipRect's Flags (graphics/clip.h with NEWCLIPRECTS_1_1, layers' own field): a composited layer's
 * piece. It is obscured and its BitMap holds the layer's own pixels, which the compositor shows: nothing covers
 * it, so graphics.library's default backfill (EraseRect) clears it as a piece that shows. */
#define CR_OPENLAYERS_COMPOSED  0x40000000L

#define LVO_ComposeLayerInfo    (-246)
#define LVO_SetLayerOpacity     (-252)
#define LVO_SetLayerLook        (-258)

#endif
