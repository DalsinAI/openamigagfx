# OpenGfx

OpenGfx: accelerated graphics.library drawing for AmigaOS 3.x (AmigaChrome's AC090 leaves and OpenGPU), MIT, Copyright (c) 2026 Dalsin Limited.

This repository starts from `DalsinAI/openamigartg` `library/ogfx` at 0b5f753, the copy that is built into opengpu.library from 0.6, with the header it uses (`include/opengpu/gfx.h`). `library/ogfx/FORK.md` records where OpenGfx came from (amigachrome-guest `libraries/opengfx`, opengfx.library 1.1) and every change since. openamigartg keeps its own copy and still builds it.

It is the home for the open graphics.library work (amigachrome-guest `libraries/OPEN_LIBRARIES.md`, sections 2 and 4): clean-room, MIT, API and ABI compatible with OS 3.2's graphics.library, planar and chunky, running on a real 68000 with no OpenGPU and faster where AmigaChrome's leaves or OpenGPU are there.

## The core

| File | What |
| --- | --- |
| `library/core/gfx_core.c`, `.h` | Drawing on a bitmap's own memory, in plain C: pixels, SetRast, and pens to and from byte arrays (an 8x8 bit transpose for planar). Pure: any core may run it. |
| `library/core/gfx_rp.c`, `.h` | graphics.library's WritePixel, ReadPixel, SetRast, WritePixelArray8, ReadPixelArray8 and WriteChunkyPixels on an unlayered RastPort, through the core. |
| `tests/m68k/test_gfx_core.sh` | The core for the 68000, 68020 and 68040 at -O0 and -O2 under qemu-m68k, against a pixel-at-a-time model. |
| `tests/GfxCoreABITest.c`, `build-amiga.sh` | The RastPort calls side by side with OS 3.2's graphics.library on Chip RAM bitmaps, byte for byte. |

These are the calls OpenGfx 1.4 still hands to graphics.library's own code. WritePixelArray8 and WriteChunkyPixels are how a browser that renders into a chunky buffer gets its page onto an AGA screen.
