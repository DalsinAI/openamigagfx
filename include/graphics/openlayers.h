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
 * SetLayerOpacity sets a composited layer's opacity, 0 (not seen) to 255
 * (opaque, the default), and answers the old one; -1 for a classic layer,
 * where it does nothing. SetLayerLook sets the rest of a layer's look:
 * LAYERLOOK_OPACITY is SetLayerOpacity; LAYERLOOK_SHADOW, _DRAGGED,
 * _INACTIVE and _ALPHA are switched on by a value of 1 and off by 0, and
 * the answer is the old state (0 or 1). LAYERLOOK_ALL sets them all at
 * once (value: opacity | the LAYERLOOK_ bits << 8), as one compose. ~0 for
 * a classic layer. A layer is composed again only when what is seen of it
 * changes. */
#ifndef GRAPHICS_OPENLAYERS_H
#define GRAPHICS_OPENLAYERS_H

#define OPENLAYERS_COMPOSITE_REVISION 101

#define LAYERS_COMPOSE_ON       1UL

#define LAYERLOOK_OPACITY       0UL     /* 0-255 */
#define LAYERLOOK_SHADOW        1UL     /* a shadow under it (windows and menus: intuition sets it) */
#define LAYERLOOK_DRAGGED       2UL     /* being dragged: the look's drag opacity times its own */
#define LAYERLOOK_INACTIVE      4UL     /* an inactive window: the look's inactive opacity times its own */
#define LAYERLOOK_ALPHA         8UL     /* its surface is ARGB32 and its own alpha counts (32-bit screens) */
#define LAYERLOOK_QUERY         0x100UL /* answers opacity | look << 8 | 1 << 16, changes nothing */
#define LAYERLOOK_ALL           0x200UL /* value: opacity | look << 8, all at once; answers as QUERY */

#define LVO_ComposeLayerInfo    (-246)
#define LVO_SetLayerOpacity     (-252)
#define LVO_SetLayerLook        (-258)

#endif
