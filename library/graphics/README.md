# OpenGfx graphics.library: the face

`graphics.library` 47.100 is clean-room, MIT, and API and ABI compatible with OS 3.2's. It is built from four areas, and `OWNERS.md` lists who owns each of its 172 LVOs. This directory is the face. It has the library itself (the RomTag, the base and the vector table), the env and layouts that every area shares, and the face's own calls: bitmaps and rasters, RastPort state, colour maps and pen sharing, regions, layer locks and super bitmaps, scaling, Flood, and every drawing call of the core (`library/core`).

| File | What |
| --- | --- |
| `graphics.lvo` | The jump table: LVO, call and registers, from the NDK 3.2 `graphics_lib.fd` order. `build-amiga.sh` checks it against the stove's fd with `check_fd.py`. |
| `OWNERS.md` | Every LVO's owner and the contract between the areas: `gfx_lvo_<Call>` register entries, `gfx_init_<area>`, the hooks, `graphics.sources`, and which area sets which GfxBase fields. |
| `gfx_types.h` | The NDK layouts in stdint types, with 68k static_asserts: the exec basics, Rectangle, BitMap, RastPort, TmpRas, AreaInfo, ColorMap, PaletteExtra, Region, ClipRect, Layer and GfxBase (V40's fields at their offsets, 544 bytes). |
| `gfx_env.h` | `struct gfx_env`, through which each area's core reaches exec, utility.library and the custom chips. It also says where the env lives: in the library base, after GfxBase (`GFX_PRIVATE_OFFSET`). The library has no .data or .bss. |
| `gfx_face.h`, `gfx_face.cpp` | The face's calls, as lean C++ with no Amiga headers, in plain C arguments (`gfx_<Call>(env, ...)`). The other areas may call them too. |
| `face_lvo.c` | The face's 79 LVOs and the 9 private slots, each in its fd registers with A6 the base. |
| `opengfx_lib.c` | The library: the RomTag (priority 65, cold start), `lib_init` (opens utility.library, NewLists the GfxBase lists, fills the env, calls each area's init in turn: face, display, text, gels), and the vector table from `gfx_vectors.h`. |
| `gen_stubs.py` | Generates `gfx_vectors.h` from graphics.lvo, and a stub for every `gfx_lvo_`, `gfx_init_`, `gfx_exit_` or `gfx_hook_` symbol that no area defines yet. The library therefore links with any subset of the areas. |
| `check_regs.py` | Checks every area's `gfx_lvo_` definitions against graphics.lvo (the registers, in order) and OWNERS.md (the owning directory). |
| `build-amiga.sh` | The os32-gcc16 stove (GCC 16.2.0-open2). Runs the fd and register checks, then builds `graphics.library` (the replacement), `opengfx.library` (the same code under its own name) and `GfxFaceABITest`, with nm checks: nothing undefined, no data or bss, no C++ runtime. |
| `tests/m68k/test_gfx_face.sh` | The face and the core under qemu-m68k on the 68000, 68020 and 68040 at -O0 and -O2. Every line is checked against an independent python3 model. It also runs check_regs and gen_stubs, and nm checks on the face. |
| `tests/GfxFaceABITest.c` | One script run through OS 3.2's graphics.library and then through opengfx.library, line by line. `PROBE` records OS 3.2's answers for the corners marked "ABI test" below. `SCREENS` also prints the PaletteExtra of a screen of each depth, and the Workbench's. `ONLY=SHARE` runs the pen-sharing lines alone. |

## How drawing goes

Each drawing call (WritePixel ... WriteChunkyPixels) waits for the blitter (`gfx_hook_wait_blit`, from gels) and locks the RastPort's layer. It then calls the core. The core draws on an unlayered RastPort, or into the layer's ClipRects. Some calls the core leaves to graphics.library, and the face does those itself:

- **Super bitmap layers.** The call draws on the super bitmap, whose coordinates are the RastPort's. The face then copies the call's box from the super bitmap to the layer's pieces: the screen where the layer is on top, and a smart piece's own bitmap where it is covered. The super bitmap edges clip a rectangle call.
- **DrawEllipse in COMPLEMENT or INVERSVID, or with a line pattern.** The core draws its ellipse into a one-plane mask, and each pixel of the mask is then drawn once through the DrawMode.
- **BltPattern through a mask in a layer.** Each run of the mask's set bits is filled as RectFill fills it.
- **EraseRect in a layer.** The layer's BackFill hook is called piece by piece, as layers.library calls it. LAYERS_NOBACKFILL does nothing, and the default clears to pen 0 through the Mask.
- **ClipBlit and ScrollRaster in a simple refresh layer or under a clip region, or with a rectangle not wholly in the layer, or a ScrollRaster shift of its size or more.** The part that can be read and written is copied through the core, and the rest is filled. A simple refresh destination gets damage and LAYERREFRESH. ScrollRasterBF fills through the backfill.
- **AreaEnd that the core has not measured** (NOCROSSFILL, more than 16 pattern rows, multicolour patterns, an outline past a layer edge) is drawn the core's way anyway.

BltBitMap for a bitmap that is not standard planar (more than 8 planes, or BMF_SPECIALFMT) goes to gels (`gfx_hook_blt_bitmap`). Colour changes go to display (`gfx_hook_vp_colors`). InitRastPort's font goes through text (`gfx_hook_set_font`).

## Edge cases

| Case | What we do | Source |
| --- | --- | --- |
| InitRastPort | Mask 0xFF, FgPen and AOlPen -1, BgPen 0, JAM2, LinePtrn 0xFFFF, linpatcnt 15, FRST_DOT, PenWidth and PenHeight 1, DefaultFont through SetFont, the rest 0 | autodoc defaults; every byte in ABI test (INITRP) |
| InitBitMap | Sets BytesPerRow (whole words), Rows, Depth, Flags 0 and pad 0; leaves the planes alone | autodoc; ABI test (INITBM) |
| AllocRaster size | Rows of whole words (RASSIZE), Chip RAM | documented |
| AllocBitMap of depth > 8, or width or height 0 | NULL (planar only; RTG is not done yet) | ours; ABI test |
| AllocBitMap of depth 0 | A BitMap with no planes | ours. OS 3.2.3 jumps to an odd address, so GfxFaceABITest leaves it out |
| AllocBitMap BMF_DISPLAYABLE on AGA | Rows rounded to 64 bits | ABI test |
| AllocBitMap BMF_INTERLEAVED | One Chip block, BytesPerRow = row × depth, Flags BMF_INTERLEAVED. Depth 1 is not interleaved. With no block that large, it allocates plane by plane | documented; the fallback is ABI test |
| A failure part way through AllocBitMap | Everything allocated so far is freed, and the call answers NULL | test |
| FreeBitMap(NULL), or a BitMap not from AllocBitMap | Nothing | documented / ours |
| GetBitMapAttr | WIDTH is BytesPerRow × 8 (divided by the depth when interleaved). FLAGS is DISPLAYABLE and INTERLEAVED as allocated, plus STANDARD. An unknown attribute answers 0 | autodoc; ABI test |
| SetOutlinePen | Answers the old pen and sets AREAOUTLINE | documented |
| SetWriteMask | Answers TRUE | documented |
| SetMaxPen | Kept nowhere | ABI test |
| GetRPAttrs MaxPen and DrawBounds | The bitmap's highest pen; the RastPort's window (a layer's bounds plus its scroll) | ABI test |
| Tags | TAG_IGNORE, TAG_SKIP and TAG_MORE are followed; unknown tags are left alone | documented; test |
| GetColorMap(0) or less | NULL | ABI test |
| GetColorMap above 65535 | NULL: cm_Count is a UWORD, and FreeColorMap frees by it | ours |
| A new ColorMap | Type V39, all colours 0, sprite resolutions SPRITERESN_DEFAULT, the other fields 0 | ABI test (GETCM, CMRGB) |
| SetRGB4/SetRGB4CM | 4 bits stand for 8 by repeating (0xA is 0xAA) | documented |
| An index past the ColorMap | Set calls do nothing; GetRGB4 answers -1; GetRGB32 stops at the end and writes nothing past it | autodoc for GetRGB4; ABI test for GetRGB32 |
| LoadRGB32 | Runs of (count << 16 \| first) until a count of 0. Entries past the map are skipped | documented |
| Which pen ObtainPen(-1) and ObtainBestPen hand out | The highest free pen | ABI test |
| ObtainPen of a shared pen | Shared again when PEN_EXCLUSIVE is not asked for and the colour is the same (or with PEN_NO_SETCOLOR); otherwise -1 | ABI test |
| ObtainBestPenA's precision | Each 8-bit gun within 8 + 2 × precision; exact for PRECISION_EXACT. Default PRECISION_IMAGE | default documented; tolerance is ABI test |
| ObtainBestPenA with no good match | A free pen with that colour; with none free, the nearest shared pen, or -1 with OBP_FailIfBad | documented |
| FindColor | Least sum of squared 8-bit differences; the lowest pen wins a tie; maxcolor -1 is every pen | ABI test |
| AttachPalExtra twice | 0, and the first PaletteExtra is kept | ABI test |
| The pens a PaletteExtra shares | As many as its ViewPort shows: 1 << depth (1 at depth 0), 32 for EHB, 16 for HAM6, 64 for HAM8. `pe_SharableColors` and `pe_FirstFree` hold the highest of them (255 for 256 pens) and `pe_NFree` the count. With no ViewPort, none: 0xFFFF and 0. Never more than the map holds, and the whole map without a RasInfo or bitmap | ABI test (SHARE, lab t3-sharable on OS 3.2.3, 14 Oct 2026); the map limit and no-RasInfo case are ours |
| PaletteExtra's lists | AllocList holds each pen's state; NFree, NShared and FirstFree are kept up to date | ours; ABI test |
| ScalerDiv | factor × numerator / denominator rounded to nearest; a denominator of 0 gives 0 | ABI test |
| BitMapScale | Destination size from ScalerDiv, written back; nearest source pixel | ABI test (BITMAPSCALE) |
| Flood | 4-connected. Mode 0 stops at AOlPen and mode 1 takes the seed's colour; drawn as RectFill (FgPen, area pattern, DrawMode, Mask). The TmpRas must hold a plane the size of the window, otherwise FALSE | autodoc; connectivity and TmpRas size are ABI test |
| Flood from a pixel outside the window | TRUE, nothing drawn | ours |
| DrawEllipse COMPLEMENT/INVERSVID | Each pixel once: COMPLEMENT inverts it; INVERSVID draws nothing in JAM1 and BgPen in JAM2. A line pattern is not applied | ABI test (ELLIPSE) |
| DrawEllipse with a negative radius | Nothing | ours |
| Super bitmap layer WritePixel/ReadPixel off the super bitmap | -1 | ours, as off a layer |
| Damage from a copy in a simple refresh layer | The whole destination rectangle, when either layer has a hidden piece or the rectangle leaves its window | coarse ours; OS 3.2 damages only the hidden parts |
| EraseRect default backfill in a layer | Pen 0 through the RastPort's Mask | ABI test |
| AreaMove/AreaDraw/AreaEllipse/AreaEnd with no AreaInfo | -1 (OS 3.2 crashes) | ours |
| LockLayerRom(NULL) and the other layer calls given NULL | Nothing | ours |
| Private LVOs (-642, -648, -738, -744, -750, -846, -906, -912, -1014) | Answer 0 | ours |

## Results

`sh tests/m68k/test_gfx_face.sh` passes on all six builds (68000, 68020 and 68040 at -O0 and -O2), each built by m68k-linux-gnu-gcc/g++ 13 and run by qemu-m68k. With the default 24 rounds that is 1683 checked lines per build. The face's object has no data or bss, no C++ runtime, and no calls outside the core, libgcc and the hooks. check_regs reports 88 of 172 LVOs defined in this directory, with registers matching. gfx-text's `text_lvo.c` (branch team3/gfx-text-20261013) also matches. The Amiga build (`build-amiga.sh`, os32-gcc16) and GfxFaceABITest have not been run here, because there is no stove in the cloud.

## Not yet

- RTG: chunky bitmaps (depth > 8, BMF_SPECIALFMT, a friend bitmap's format). AllocBitMap takes the friend bitmap and ignores it.
- NOCROSSFILL and multicolour area patterns are drawn the core's way (multicolour as solid).
- DrawEllipse in COMPLEMENT/INVERSVID ignores the line pattern.
- Damage is the whole rectangle rather than the hidden parts.
- On a super bitmap layer under a clip region, the super bitmap is drawn outside the region too (the pieces on screen keep to it).
- ClipBlit/ScrollRaster/EraseRect in layers are not in GfxFaceABITest (they need layers.library on both sides).
- GfxBase fields after V40's HWEmul that 3.2 may publish (ABI test: sizeof).
