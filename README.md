# OpenGfx

OpenGfx: accelerated graphics.library drawing for AmigaOS 3.x (AmigaChrome's AC090 leaves and OpenGPU), MIT, Copyright (c) 2026 Dalsin Limited.

This repository starts from `DalsinAI/openamigartg` `library/ogfx` at 0b5f753, the copy that is built into opengpu.library from 0.6, with the header it uses (`include/opengpu/gfx.h`). `library/ogfx/FORK.md` records where OpenGfx came from (amigachrome-guest `libraries/opengfx`, opengfx.library 1.1) and every change since. openamigartg keeps its own copy and still builds it.

It is the home for the open graphics.library work (amigachrome-guest `libraries/OPEN_LIBRARIES.md`, sections 2 and 4): clean-room, MIT, API and ABI compatible with OS 3.2's graphics.library, planar and chunky, running on a real 68000 with no OpenGPU and faster where AmigaChrome's leaves or OpenGPU are there.

## The core

| File | What |
| --- | --- |
| `library/core/gfx_core.c`, `.h` | Drawing on a bitmap's own memory, in plain C: pixels, SetRast, pens to and from byte arrays (an 8x8 bit transpose for planar), and rows through a 16-pixel pattern a byte at a time. Pure: any core may run it. |
| `library/core/gfx_rp.c`, `.h` | graphics.library's WritePixel, ReadPixel, SetRast, WritePixelArray8, ReadPixelArray8, WriteChunkyPixels, Move, Draw, PolyDraw, RectFill, BltPattern, BltBitMap, BltBitMapRastPort, ClipBlit, ScrollRaster, AreaMove, AreaDraw, AreaEllipse, AreaEnd, DrawEllipse, EraseRect, Text, TextLength and TextExtent, through the core: on an unlayered RastPort, and in a layer through its ClipRects (the screen where the layer is on top, a smart refresh layer's own bitmaps where it is covered; a super bitmap layer and BltPattern through a mask stay with graphics.library, and so do ClipBlit and ScrollRaster in a simple refresh layer, where OS 3.2 adds damage, under a clip region, with a rectangle not wholly inside the layer, or a ScrollRaster shift of the rectangle's size or more in a layer). Text with a colour font or one drawn right to left stays with graphics.library too. Multicolour area patterns come later. |
| `tests/m68k/test_gfx_core.sh` | The core for the 68000, 68020 and 68040 at -O0 and -O2, each on that CPU under qemu-m68k, against a pixel-at-a-time model. |
| `library/core/gfx_region.c`, `.h` | graphics.library's regions: NewRegion's structure, ClearRegion, OrRectRegion, AndRectRegion, XorRectRegion, ClearRectRegion, OrRegionRegion, AndRegionRegion and XorRegionRegion. A region is kept in bands, top to bottom, each a run of rectangles left to right; a call builds its answer whole first, so one that runs out of memory answers FALSE and leaves the region as it was. Pure. |
| `tests/m68k/test_gfx_region.sh` | The regions for the 68000, 68020 and 68040 at -O0 and -O2 under qemu-m68k, against a grid of cells, with calls made to run out of memory part way. |
| `tests/RegionABITest.c` | Every region call side by side with OS 3.2's graphics.library, comparing answers, bounds and the rectangle lists. |
| `tests/GfxCoreABITest.c`, `build-amiga.sh` | The RastPort calls side by side with OS 3.2's graphics.library on Chip RAM bitmaps, byte for byte; `LAYERS` does the same in layers made with OS 3.2's layers.library (smart and simple refresh, clip regions, layers in front), the covered parts included. |

These are the calls OpenGfx 1.4 still hands to graphics.library's own code. WritePixelArray8 and WriteChunkyPixels are how a browser that renders into a chunky buffer gets its page onto an AGA screen.
