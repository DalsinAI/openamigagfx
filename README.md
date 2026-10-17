# OpenGfx

OpenGfx: accelerated graphics.library drawing for AmigaOS 3.x (AmigaChrome's AC090 leaves and OpenGPU), MIT, Copyright (c) 2026 AmigaChrome Team.

This repository starts from `DalsinAI/openamigartg` `library/ogfx` at 0b5f753, the copy that is built into opengpu.library from 0.6, with the header it uses (`include/opengpu/gfx.h`). `library/ogfx/FORK.md` records where OpenGfx came from (amigachrome-guest `libraries/opengfx`, opengfx.library 1.1) and every change since. openamigartg keeps its own copy and still builds it.

It is the home for the open graphics.library work (amigachrome-guest `libraries/OPEN_LIBRARIES.md`, sections 2 and 4): clean-room, MIT, API and ABI compatible with OS 3.2's graphics.library, planar and chunky, built for the 68040 (all builds are 68040 only), with no OpenGPU needed and faster where AmigaChrome's leaves or OpenGPU are there.

## The core

| File | What |
| --- | --- |
| `library/core/gfx_core.c`, `.h` | Drawing on a bitmap's own memory, in plain C: pixels, SetRast, pens to and from byte arrays (an 8x8 bit transpose for planar), and rows through a 16-pixel pattern a byte at a time. Pure: any core may run it. |
| `library/core/gfx_rp.c`, `.h` | graphics.library's WritePixel, ReadPixel, SetRast, WritePixelArray8, ReadPixelArray8, WriteChunkyPixels, Move, Draw, PolyDraw, RectFill, BltPattern, BltBitMap, BltBitMapRastPort, ClipBlit, ScrollRaster, AreaMove, AreaDraw, AreaEllipse, AreaEnd, DrawEllipse, EraseRect, Text, TextLength and TextExtent, through the core: on an unlayered RastPort, and in a layer through its ClipRects (the screen where the layer is on top, a smart refresh layer's own bitmaps where it is covered; a super bitmap layer and BltPattern through a mask stay with graphics.library, and so do ClipBlit and ScrollRaster in a simple refresh layer, where OS 3.2 adds damage, under a clip region, with a rectangle not wholly inside the layer, or a ScrollRaster shift of the rectangle's size or more in a layer). Text with a colour font or one drawn right to left stays with graphics.library too. Multicolour area patterns come later. |
| `tests/m68k/test_gfx_core.sh` | The core for the 68040 at -O0 and -O2 under qemu-m68k, against a pixel-at-a-time model. |
| `library/core/gfx_region.c`, `.h` | graphics.library's regions: NewRegion's structure, ClearRegion, OrRectRegion, AndRectRegion, XorRectRegion, ClearRectRegion, OrRegionRegion, AndRegionRegion and XorRegionRegion. A region is kept in bands, top to bottom, each a run of rectangles left to right; a call builds its answer whole first, so one that runs out of memory answers FALSE and leaves the region as it was. Pure. |
| `tests/m68k/test_gfx_region.sh` | The regions for the 68040 at -O0 and -O2 under qemu-m68k, against a grid of cells, with calls made to run out of memory part way. |
| `tests/RegionABITest.c` | Every region call side by side with OS 3.2's graphics.library, comparing the answers and the area covered. The cut is ours: OS 3.2 never merges rectangles, puts the newest first, keeps an empty rectangle given to OrRectRegion and leaves bounds that are not the area's; ours keeps bands with tight bounds (0 when empty). XorRegionRegion of a region with itself fills it with garbage on OS 3.2; ours empties it. |
| `tests/GfxCoreABITest.c`, `build-amiga.sh` | The RastPort calls side by side with OS 3.2's graphics.library on Chip RAM bitmaps, byte for byte; `LAYERS` does the same in layers made with OS 3.2's layers.library (smart and simple refresh, clip regions, layers in front), the covered parts included. |

These are the calls OpenGfx 1.4 still hands to graphics.library's own code. WritePixelArray8 and WriteChunkyPixels are how a browser that renders into a chunky buffer gets its page onto an AGA screen.

## OpenLayers

The open layers.library starts here too, next to the graphics.library it draws through: clean-room, MIT, API and ABI compatible with OS 3.2's, every call of its jump table.

| File | What |
| --- | --- |
| `library/layers/layers_core.c`, `.h` | The geometry in plain C: sets of disjoint rectangles (subtract, and, or, as a Region's), a layer's bounds cut into pieces each visible, behind the frontmost layer covering it (a ClipRect's `obscured`) or off the Layer_Info's bounds, WhichLayer, and an order for copying rectangles moved by one offset (SortLayerCR). Pure: memory comes from the caller, any core may run it. |
| `library/layers/layers_lib.c`, `.h` | The calls: Layer_Info, making, deleting, depth, moving, sizing, hiding and showing layers, ClipRect lists in graphics.library's form (an obscured smart refresh piece's pixels in its own bitmap at `x - (MinX & ~15)`), super bitmap layers and ScrollLayer, damage and BeginUpdate/EndUpdate, clip regions, backfill hooks for layers and the Layer_Info, DoHookClipRects, SortLayerCR, SwapBitsRastPortClipRect, locking. One way through for every change of the stack: cut every layer anew, then move the pixels in an order where nothing is read after it was overwritten; out of memory, the stack goes back as it was. |
| `library/layers/openlayers_lib.c`, `layers.lvo`, `build-amiga.sh` | The library around them (`layers.library` 47.100, and `openlayers.library` for the side-by-side test), the jump table checked against the NDK 3.2 `layers_lib.fd`. |
| `tests/m68k/test_layers_core.sh` | The core for the 68040 at -O0 and -O2 under qemu-m68k (and the host with `HOST_CC`, under ASan and UBSan), against a grid of pixels. |
| `tests/m68k/test_layers_lib.sh`, `tests/host-ndk/` | The calls the same way, over a pixel model of exec and graphics.library: random stacks and calls, and after each one every layer's ClipRects, every pixel each layer keeps, its damage and backfill, the bare screen, the locks and, at the end, the memory. |
| `tests/LayersABITest.c` | The same script of calls through OS 3.2's layers.library and ours on the same bitmap, every layer's ClipRects, damage and pixels (read with graphics.library's ReadPixel) and the whole bitmap compared after each call. |
