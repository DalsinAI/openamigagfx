# graphics.library 47.100: who writes each LVO

OpenGfx's graphics.library is four areas built into one library: gfx-face (`library/graphics/`, this directory), gfx-display (`library/display/`), gfx-text (`library/text/`) and gfx-gels (`library/gels/`). `graphics.lvo` is the jump table; this file says who owns each slot. 172 slots: gfx-display 43, gfx-face 88, gfx-gels 23, gfx-text 18 (the nine private slots are the face's, as stubs answering 0).

## The contract

- **Shared headers** (gfx-face's, here): `gfx_types.h` gives the NDK layouts in stdint types with 68k static_asserts (exec Node/List/Library/Interrupt/SignalSemaphore/TagItem; Rectangle, BitMap, TmpRas, AreaInfo, RastPort, ColorMap, PaletteExtra, GfxBase). `gfx_env.h` gives `struct gfx_env` (exec memory, Forbid/Disable, semaphores, signals, ReplyMsg, interrupt servers, custom chip read/write by offset from 0xDFF000, utility's tag calls, log, call_hook); every hook takes `ctx` first, which is the GfxBase on the Amiga and where it lives: `gfx_env_of(GfxBase)`, inside the library base after the public GfxBase (`GFX_PRIVATE_OFFSET`). The library has no .data or .bss, so an area keeps its state in `gfx_private_of(GfxBase)->area[GFX_AREA_<AREA>]`, which it allocates in its init. Structures only one area uses (View, ViewPort, TextFont, GelsInfo, ...) stay in that area's headers. An area may keep its own env type, built from `gfx_env` in its init.
- **Register entries**: each owner defines `gfx_lvo_<Call>` for its LVOs in a C file of its own directory, built by the Amiga compiler with the NDK headers, taking the fd's registers in graphics.lvo's order and then `GFX_REG(a6, struct GfxBase *)`. `check_regs.py` reads every `library/*/*.c` and checks each `gfx_lvo_` definition's registers against graphics.lvo. The face's shell (`opengfx_lib.c`) puts the symbols in the vector table; `gen_stubs.py` makes a stub (answering 0) for every `gfx_lvo_` no area defines yet, so the library links with any subset of the areas. Each area lists the files that go into the library, one per line relative to its directory (`.c` built as C, `.cpp` as lean C++), in `library/<area>/graphics.sources`; `build-amiga.sh` builds them.
- **Init**: `int gfx_init_<area>(struct gfx_env *)` / `void gfx_exit_<area>(struct gfx_env *)`, face, display, text, gels in that order. Stubbed (1 / nothing) when missing.
- **Hooks between areas** (`gfx_env.h`, stubbed when missing): `gfx_hook_vp_colors` (display: a ViewPort's colours changed), `gfx_hook_set_font` (text: SetFont, for InitRastPort), `gfx_hook_blt_bitmap` (gels: BltBitMap the CPU copy does not take), `gfx_hook_wait_blit` (gels: WaitBlit before the CPU draws), `gfx_hook_vsprites` (display, called by gels' DrawGList with the VSprites it placed).
- **Calls the face gives the others**, plain C in `library/graphics/gfx_face.h`: `gfx_AllocBitMap`, `gfx_FreeBitMap`, `gfx_GetBitMapAttr`, `gfx_InitBitMap`, `gfx_AllocRaster`, `gfx_FreeRaster`, `gfx_InitRastPort`, `gfx_GetColorMap`, `gfx_FreeColorMap`, `gfx_SetRGB32CM`, `gfx_GetRGB32`, each `(struct gfx_env *, the fd's arguments)`. The drawing is the core's: call `library/core`'s `ogfx_rp_*` directly.

## GfxBase: who sets what

| Fields | Set by |
| --- | --- |
| LibNode, ExecBase, UtilBase, gb_LayersBase (0), the lists TextFonts, BlitWaitQ, TOF_WaitQ, MonitorList (NewList'd), MemType, Flags | gfx-face, before any area's init |
| ActiView, copinit, LOFlist, SHFlist, vbsrv, timsrv, DisplayFlags, ChipRevBits0, the display rows and columns, MicrosPerLine, monitors, SpriteReserved, SimpleSprites, the sprite and fetch fields, MonitorList's entries, DisplayInfoDataBase, LastChanceMemory, ActiViewCprSemaphore, MonitorListSemaphore, HashTableSemaphore, VBCounter | gfx-display |
| TextFonts' entries, DefaultFont | gfx-text |
| blthd, blttl, bsblthd, bsblttl, bltsrv, BlitLock, BlitNest, BlitWaitQ's entries, BlitOwner | gfx-gels |

## Every LVO

| LVO | Call | Registers | Owner | Entry |
| --- | --- | --- | --- | --- |
| -30 | BltBitMap | a0 d0 d1 a1 d2 d3 d4 d5 d6 d7 a2 | gfx-face | `gfx_lvo_BltBitMap` |
| -36 | BltTemplate | a0 d0 d1 a1 d2 d3 d4 d5 | gfx-gels | `gfx_lvo_BltTemplate` |
| -42 | ClearEOL | a1 | gfx-text | `gfx_lvo_ClearEOL` |
| -48 | ClearScreen | a1 | gfx-text | `gfx_lvo_ClearScreen` |
| -54 | TextLength | a1 a0 d0 | gfx-text | `gfx_lvo_TextLength` |
| -60 | Text | a1 a0 d0 | gfx-text | `gfx_lvo_Text` |
| -66 | SetFont | a1 a0 | gfx-text | `gfx_lvo_SetFont` |
| -72 | OpenFont | a0 | gfx-text | `gfx_lvo_OpenFont` |
| -78 | CloseFont | a1 | gfx-text | `gfx_lvo_CloseFont` |
| -84 | AskSoftStyle | a1 | gfx-text | `gfx_lvo_AskSoftStyle` |
| -90 | SetSoftStyle | a1 d0 d1 | gfx-text | `gfx_lvo_SetSoftStyle` |
| -96 | AddBob | a0 a1 | gfx-gels | `gfx_lvo_AddBob` |
| -102 | AddVSprite | a0 a1 | gfx-gels | `gfx_lvo_AddVSprite` |
| -108 | DoCollision | a1 | gfx-gels | `gfx_lvo_DoCollision` |
| -114 | DrawGList | a1 a0 | gfx-gels | `gfx_lvo_DrawGList` |
| -120 | InitGels | a0 a1 a2 | gfx-gels | `gfx_lvo_InitGels` |
| -126 | InitMasks | a0 | gfx-gels | `gfx_lvo_InitMasks` |
| -132 | RemIBob | a0 a1 a2 | gfx-gels | `gfx_lvo_RemIBob` |
| -138 | RemVSprite | a0 | gfx-gels | `gfx_lvo_RemVSprite` |
| -144 | SetCollision | d0 a0 a1 | gfx-gels | `gfx_lvo_SetCollision` |
| -150 | SortGList | a1 | gfx-gels | `gfx_lvo_SortGList` |
| -156 | AddAnimOb | a0 a1 a2 | gfx-gels | `gfx_lvo_AddAnimOb` |
| -162 | Animate | a0 a1 | gfx-gels | `gfx_lvo_Animate` |
| -168 | GetGBuffers | a0 a1 d0 | gfx-gels | `gfx_lvo_GetGBuffers` |
| -174 | InitGMasks | a0 | gfx-gels | `gfx_lvo_InitGMasks` |
| -180 | DrawEllipse | a1 d0 d1 d2 d3 | gfx-face | `gfx_lvo_DrawEllipse` |
| -186 | AreaEllipse | a1 d0 d1 d2 d3 | gfx-face | `gfx_lvo_AreaEllipse` |
| -192 | LoadRGB4 | a0 a1 d0 | gfx-face | `gfx_lvo_LoadRGB4` |
| -198 | InitRastPort | a1 | gfx-face | `gfx_lvo_InitRastPort` |
| -204 | InitVPort | a0 | gfx-display | `gfx_lvo_InitVPort` |
| -210 | MrgCop | a1 | gfx-display | `gfx_lvo_MrgCop` |
| -216 | MakeVPort | a0 a1 | gfx-display | `gfx_lvo_MakeVPort` |
| -222 | LoadView | a1 | gfx-display | `gfx_lvo_LoadView` |
| -228 | WaitBlit | - | gfx-gels | `gfx_lvo_WaitBlit` |
| -234 | SetRast | a1 d0 | gfx-face | `gfx_lvo_SetRast` |
| -240 | Move | a1 d0 d1 | gfx-face | `gfx_lvo_Move` |
| -246 | Draw | a1 d0 d1 | gfx-face | `gfx_lvo_Draw` |
| -252 | AreaMove | a1 d0 d1 | gfx-face | `gfx_lvo_AreaMove` |
| -258 | AreaDraw | a1 d0 d1 | gfx-face | `gfx_lvo_AreaDraw` |
| -264 | AreaEnd | a1 | gfx-face | `gfx_lvo_AreaEnd` |
| -270 | WaitTOF | - | gfx-display | `gfx_lvo_WaitTOF` |
| -276 | QBlit | a1 | gfx-gels | `gfx_lvo_QBlit` |
| -282 | InitArea | a0 a1 d0 | gfx-face | `gfx_lvo_InitArea` |
| -288 | SetRGB4 | a0 d0 d1 d2 d3 | gfx-face | `gfx_lvo_SetRGB4` |
| -294 | QBSBlit | a1 | gfx-gels | `gfx_lvo_QBSBlit` |
| -300 | BltClear | a1 d0 d1 | gfx-gels | `gfx_lvo_BltClear` |
| -306 | RectFill | a1 d0 d1 d2 d3 | gfx-face | `gfx_lvo_RectFill` |
| -312 | BltPattern | a1 a0 d0 d1 d2 d3 d4 | gfx-face | `gfx_lvo_BltPattern` |
| -318 | ReadPixel | a1 d0 d1 | gfx-face | `gfx_lvo_ReadPixel` |
| -324 | WritePixel | a1 d0 d1 | gfx-face | `gfx_lvo_WritePixel` |
| -330 | Flood | a1 d2 d0 d1 | gfx-face | `gfx_lvo_Flood` |
| -336 | PolyDraw | a1 d0 a0 | gfx-face | `gfx_lvo_PolyDraw` |
| -342 | SetAPen | a1 d0 | gfx-face | `gfx_lvo_SetAPen` |
| -348 | SetBPen | a1 d0 | gfx-face | `gfx_lvo_SetBPen` |
| -354 | SetDrMd | a1 d0 | gfx-face | `gfx_lvo_SetDrMd` |
| -360 | InitView | a1 | gfx-display | `gfx_lvo_InitView` |
| -366 | CBump | a1 | gfx-display | `gfx_lvo_CBump` |
| -372 | CMove | a1 d0 d1 | gfx-display | `gfx_lvo_CMove` |
| -378 | CWait | a1 d0 d1 | gfx-display | `gfx_lvo_CWait` |
| -384 | VBeamPos | - | gfx-display | `gfx_lvo_VBeamPos` |
| -390 | InitBitMap | a0 d0 d1 d2 | gfx-face | `gfx_lvo_InitBitMap` |
| -396 | ScrollRaster | a1 d0 d1 d2 d3 d4 d5 | gfx-face | `gfx_lvo_ScrollRaster` |
| -402 | WaitBOVP | a0 | gfx-display | `gfx_lvo_WaitBOVP` |
| -408 | GetSprite | a0 d0 | gfx-display | `gfx_lvo_GetSprite` |
| -414 | FreeSprite | d0 | gfx-display | `gfx_lvo_FreeSprite` |
| -420 | ChangeSprite | a0 a1 a2 | gfx-display | `gfx_lvo_ChangeSprite` |
| -426 | MoveSprite | a0 a1 d0 d1 | gfx-display | `gfx_lvo_MoveSprite` |
| -432 | LockLayerRom | a5 | gfx-face | `gfx_lvo_LockLayerRom` |
| -438 | UnlockLayerRom | a5 | gfx-face | `gfx_lvo_UnlockLayerRom` |
| -444 | SyncSBitMap | a0 | gfx-face | `gfx_lvo_SyncSBitMap` |
| -450 | CopySBitMap | a0 | gfx-face | `gfx_lvo_CopySBitMap` |
| -456 | OwnBlitter | - | gfx-gels | `gfx_lvo_OwnBlitter` |
| -462 | DisownBlitter | - | gfx-gels | `gfx_lvo_DisownBlitter` |
| -468 | InitTmpRas | a0 a1 d0 | gfx-face | `gfx_lvo_InitTmpRas` |
| -474 | AskFont | a1 a0 | gfx-text | `gfx_lvo_AskFont` |
| -480 | AddFont | a1 | gfx-text | `gfx_lvo_AddFont` |
| -486 | RemFont | a1 | gfx-text | `gfx_lvo_RemFont` |
| -492 | AllocRaster | d0 d1 | gfx-face | `gfx_lvo_AllocRaster` |
| -498 | FreeRaster | a0 d0 d1 | gfx-face | `gfx_lvo_FreeRaster` |
| -504 | AndRectRegion | a0 a1 | gfx-face | `gfx_lvo_AndRectRegion` |
| -510 | OrRectRegion | a0 a1 | gfx-face | `gfx_lvo_OrRectRegion` |
| -516 | NewRegion | - | gfx-face | `gfx_lvo_NewRegion` |
| -522 | ClearRectRegion | a0 a1 | gfx-face | `gfx_lvo_ClearRectRegion` |
| -528 | ClearRegion | a0 | gfx-face | `gfx_lvo_ClearRegion` |
| -534 | DisposeRegion | a0 | gfx-face | `gfx_lvo_DisposeRegion` |
| -540 | FreeVPortCopLists | a0 | gfx-display | `gfx_lvo_FreeVPortCopLists` |
| -546 | FreeCopList | a0 | gfx-display | `gfx_lvo_FreeCopList` |
| -552 | ClipBlit | a0 d0 d1 a1 d2 d3 d4 d5 d6 | gfx-face | `gfx_lvo_ClipBlit` |
| -558 | XorRectRegion | a0 a1 | gfx-face | `gfx_lvo_XorRectRegion` |
| -564 | FreeCprList | a0 | gfx-display | `gfx_lvo_FreeCprList` |
| -570 | GetColorMap | d0 | gfx-face | `gfx_lvo_GetColorMap` |
| -576 | FreeColorMap | a0 | gfx-face | `gfx_lvo_FreeColorMap` |
| -582 | GetRGB4 | a0 d0 | gfx-face | `gfx_lvo_GetRGB4` |
| -588 | ScrollVPort | a0 | gfx-display | `gfx_lvo_ScrollVPort` |
| -594 | UCopperListInit | a0 d0 | gfx-display | `gfx_lvo_UCopperListInit` |
| -600 | FreeGBuffers | a0 a1 d0 | gfx-gels | `gfx_lvo_FreeGBuffers` |
| -606 | BltBitMapRastPort | a0 d0 d1 a1 d2 d3 d4 d5 d6 | gfx-face | `gfx_lvo_BltBitMapRastPort` |
| -612 | OrRegionRegion | a0 a1 | gfx-face | `gfx_lvo_OrRegionRegion` |
| -618 | XorRegionRegion | a0 a1 | gfx-face | `gfx_lvo_XorRegionRegion` |
| -624 | AndRegionRegion | a0 a1 | gfx-face | `gfx_lvo_AndRegionRegion` |
| -630 | SetRGB4CM | a0 d0 d1 d2 d3 | gfx-face | `gfx_lvo_SetRGB4CM` |
| -636 | BltMaskBitMapRastPort | a0 d0 d1 a1 d2 d3 d4 d5 d6 a2 | gfx-gels | `gfx_lvo_BltMaskBitMapRastPort` |
| -642 | private | - | gfx-face | `gfx_lvo_private_642` |
| -648 | private | - | gfx-face | `gfx_lvo_private_648` |
| -654 | AttemptLockLayerRom | a5 | gfx-face | `gfx_lvo_AttemptLockLayerRom` |
| -660 | GfxNew | d0 | gfx-display | `gfx_lvo_GfxNew` |
| -666 | GfxFree | a0 | gfx-display | `gfx_lvo_GfxFree` |
| -672 | GfxAssociate | a0 a1 | gfx-display | `gfx_lvo_GfxAssociate` |
| -678 | BitMapScale | a0 | gfx-face | `gfx_lvo_BitMapScale` |
| -684 | ScalerDiv | d0 d1 d2 | gfx-face | `gfx_lvo_ScalerDiv` |
| -690 | TextExtent | a1 a0 d0 a2 | gfx-text | `gfx_lvo_TextExtent` |
| -696 | TextFit | a1 a0 d0 a2 a3 d1 d2 d3 | gfx-text | `gfx_lvo_TextFit` |
| -702 | GfxLookUp | a0 | gfx-display | `gfx_lvo_GfxLookUp` |
| -708 | VideoControl | a0 a1 | gfx-display | `gfx_lvo_VideoControl` |
| -714 | OpenMonitor | a1 d0 | gfx-display | `gfx_lvo_OpenMonitor` |
| -720 | CloseMonitor | a0 | gfx-display | `gfx_lvo_CloseMonitor` |
| -726 | FindDisplayInfo | d0 | gfx-display | `gfx_lvo_FindDisplayInfo` |
| -732 | NextDisplayInfo | d0 | gfx-display | `gfx_lvo_NextDisplayInfo` |
| -738 | private | - | gfx-face | `gfx_lvo_private_738` |
| -744 | private | - | gfx-face | `gfx_lvo_private_744` |
| -750 | private | - | gfx-face | `gfx_lvo_private_750` |
| -756 | GetDisplayInfoData | a0 a1 d0 d1 d2 | gfx-display | `gfx_lvo_GetDisplayInfoData` |
| -762 | FontExtent | a0 a1 | gfx-text | `gfx_lvo_FontExtent` |
| -768 | ReadPixelLine8 | a0 d0 d1 d2 a2 a1 | gfx-face | `gfx_lvo_ReadPixelLine8` |
| -774 | WritePixelLine8 | a0 d0 d1 d2 a2 a1 | gfx-face | `gfx_lvo_WritePixelLine8` |
| -780 | ReadPixelArray8 | a0 d0 d1 d2 d3 a2 a1 | gfx-face | `gfx_lvo_ReadPixelArray8` |
| -786 | WritePixelArray8 | a0 d0 d1 d2 d3 a2 a1 | gfx-face | `gfx_lvo_WritePixelArray8` |
| -792 | GetVPModeID | a0 | gfx-display | `gfx_lvo_GetVPModeID` |
| -798 | ModeNotAvailable | d0 | gfx-display | `gfx_lvo_ModeNotAvailable` |
| -804 | WeighTAMatch | a0 a1 a2 | gfx-text | `gfx_lvo_WeighTAMatch` |
| -810 | EraseRect | a1 d0 d1 d2 d3 | gfx-face | `gfx_lvo_EraseRect` |
| -816 | ExtendFont | a0 a1 | gfx-text | `gfx_lvo_ExtendFont` |
| -822 | StripFont | a0 | gfx-text | `gfx_lvo_StripFont` |
| -828 | CalcIVG | a0 a1 | gfx-display | `gfx_lvo_CalcIVG` |
| -834 | AttachPalExtra | a0 a1 | gfx-face | `gfx_lvo_AttachPalExtra` |
| -840 | ObtainBestPenA | a0 d1 d2 d3 a1 | gfx-face | `gfx_lvo_ObtainBestPenA` |
| -846 | private | - | gfx-face | `gfx_lvo_private_846` |
| -852 | SetRGB32 | a0 d0 d1 d2 d3 | gfx-face | `gfx_lvo_SetRGB32` |
| -858 | GetAPen | a0 | gfx-face | `gfx_lvo_GetAPen` |
| -864 | GetBPen | a0 | gfx-face | `gfx_lvo_GetBPen` |
| -870 | GetDrMd | a0 | gfx-face | `gfx_lvo_GetDrMd` |
| -876 | GetOutlinePen | a0 | gfx-face | `gfx_lvo_GetOutlinePen` |
| -882 | LoadRGB32 | a0 a1 | gfx-face | `gfx_lvo_LoadRGB32` |
| -888 | SetChipRev | d0 | gfx-display | `gfx_lvo_SetChipRev` |
| -894 | SetABPenDrMd | a1 d0 d1 d2 | gfx-face | `gfx_lvo_SetABPenDrMd` |
| -900 | GetRGB32 | a0 d0 d1 a1 | gfx-face | `gfx_lvo_GetRGB32` |
| -906 | private | - | gfx-face | `gfx_lvo_private_906` |
| -912 | private | - | gfx-face | `gfx_lvo_private_912` |
| -918 | AllocBitMap | d0 d1 d2 d3 a0 | gfx-face | `gfx_lvo_AllocBitMap` |
| -924 | FreeBitMap | a0 | gfx-face | `gfx_lvo_FreeBitMap` |
| -930 | GetExtSpriteA | a2 a1 | gfx-display | `gfx_lvo_GetExtSpriteA` |
| -936 | CoerceMode | a0 d0 d1 | gfx-display | `gfx_lvo_CoerceMode` |
| -942 | ChangeVPBitMap | a0 a1 a2 | gfx-display | `gfx_lvo_ChangeVPBitMap` |
| -948 | ReleasePen | a0 d0 | gfx-face | `gfx_lvo_ReleasePen` |
| -954 | ObtainPen | a0 d0 d1 d2 d3 d4 | gfx-face | `gfx_lvo_ObtainPen` |
| -960 | GetBitMapAttr | a0 d1 | gfx-face | `gfx_lvo_GetBitMapAttr` |
| -966 | AllocDBufInfo | a0 | gfx-display | `gfx_lvo_AllocDBufInfo` |
| -972 | FreeDBufInfo | a1 | gfx-display | `gfx_lvo_FreeDBufInfo` |
| -978 | SetOutlinePen | a0 d0 | gfx-face | `gfx_lvo_SetOutlinePen` |
| -984 | SetWriteMask | a0 d0 | gfx-face | `gfx_lvo_SetWriteMask` |
| -990 | SetMaxPen | a0 d0 | gfx-face | `gfx_lvo_SetMaxPen` |
| -996 | SetRGB32CM | a0 d0 d1 d2 d3 | gfx-face | `gfx_lvo_SetRGB32CM` |
| -1002 | ScrollRasterBF | a1 d0 d1 d2 d3 d4 d5 | gfx-face | `gfx_lvo_ScrollRasterBF` |
| -1008 | FindColor | a3 d1 d2 d3 d4 | gfx-face | `gfx_lvo_FindColor` |
| -1014 | private | - | gfx-face | `gfx_lvo_private_1014` |
| -1020 | AllocSpriteDataA | a2 a1 | gfx-display | `gfx_lvo_AllocSpriteDataA` |
| -1026 | ChangeExtSpriteA | a0 a1 a2 a3 | gfx-display | `gfx_lvo_ChangeExtSpriteA` |
| -1032 | FreeSpriteData | a2 | gfx-display | `gfx_lvo_FreeSpriteData` |
| -1038 | SetRPAttrsA | a0 a1 | gfx-face | `gfx_lvo_SetRPAttrsA` |
| -1044 | GetRPAttrsA | a0 a1 | gfx-face | `gfx_lvo_GetRPAttrsA` |
| -1050 | BestModeIDA | a0 | gfx-display | `gfx_lvo_BestModeIDA` |
| -1056 | WriteChunkyPixels | a0 d0 d1 d2 d3 a2 d4 | gfx-face | `gfx_lvo_WriteChunkyPixels` |
