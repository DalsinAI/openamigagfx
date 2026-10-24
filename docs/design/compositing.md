# Compositing in layers.library

OpenLayers (`library/layers`) has two modes. **Classic layers** are OS 3.2's, byte for byte, as `tests/LayersABITest.c` measures them. The **composited mode** gives each layer a surface of its own and composes the screen from the surfaces with OpenGPU, so windows can be translucent and have shadows, and a window dragged with its contents costs no refresh. Team 5, 17 October 2026, after Dale: "layers needs revised to support hardware acceleration and transparency" and "extend layers; it will need to squeeze into ROM; we enable the prefs setting for transparency and shadow by default".

The code is `library/layers/layers_comp.c`, built into the Amiga library only (`OL_COMPOSITE`, `build-amiga.sh`; `COMPOSITE=0` leaves it out). The host and qemu tests build classic layers, as before.

## Where it composites, and where it stays classic

A `Layer_Info` is composited when all of these hold. Otherwise it is classic and nothing below runs for it.

- **Intuition asks**, for a public screen only: the Workbench, or a screen opened with `SA_PubName`. A custom screen, a game's, is never composited. The call is `ComposeLayerInfo(li, bitmap, LAYERS_COMPOSE_ON)` before the screen's first layer (its title bar's).
- **The bitmap is OpenRTG's, 16 or 32 bits** (`ORTG_BitMapInfo` answers, format 1 or 2). AGA screens and 8-bit RTG screens stay classic. OpenRTG has no 15-bit mode; its 16-bit is RGB565.
- **OpenGPU does COMPOSITE fully on a back end that is not the CPU**: `OGPU_Query(OGPU_OP_COMPOSITE, format)` answers `OGPU_FULL` with back end 1 (ACRTG.gpu on the Cradle). The CPU back end also answers FULL, but compositing on the 68k is not hardware acceleration, so it stays classic. The format is `OGPU_FMT_RGB565` for a 16-bit screen. For a 32-bit one it is stream v1.3's `OGPU_FMT_XRGB32`, because OpenRTG's 32-bit pixels keep their top byte 0 and v1.2's ARGB32 target would write alpha there; a back end without v1.3 leaves a 32-bit screen classic.
- **The switch is on**: `composite on` in `ENV:OpenPrefs/Windows`, which is the default when the file or the line is missing. The prefs are read when the screen opens, from the opener's process; from a plain task the defaults hold.

A screen keeps its mode until it closes. A changed switch applies to the screens opened after it (the Workbench's next reset). If a layer's surface can't be had, the screen goes back to classic layers in that same call (below); a window's opening never fails because of compositing.

### Every depth and pixel layout (W65, 18 October 2026)

OpenRTG 0.18 drives ACRTG boards only and has three formats (`openrtg.h`): CLUT8, RGB565 and ARGB32 with its top byte 0. A screen's bitmap takes its mode's format whatever depth is asked, so the depth in ScreenMode prefs doesn't choose the layout: the ModeID does. Measured with W65's DepthProbe on a public screen of ACRTG's 1920x1080 modes (the open ROM of 18 October, ACRTG.gpu, runtime stream v1.4):

| Mode | Depth asked | Bitmap | Composited |
| --- | --- | --- | --- |
| `0x610A1000` (8-bit) | 8 | CLUT8 | no: 8-bit |
| `0x610A1100` (16-bit) | 15, 16 | RGB565 | yes |
| `0x610A1100` (16-bit) | 24, 32 | the screen does not open | |
| `0x610A1200` (32-bit) | 8, 15, 16, 24, 32 | ARGB32 (as XRGB32) | yes |
| a custom screen on any of them | | as above | no: not asked |
| AGA (`0x00029004`) | 4 | planar | no: not RTG |

So every RTG depth of 15 bits and up composites, in both layouts OpenRTG has. ACRTG's board reports the 32-bit format as its format number 6 in the runtime's log; that is the board's register value for ARGB32, not an OpenRTG format. ACRTG.gpu answers `OGPU_Query(COMPOSITE)` FULL for RGB565, RGB565PC, RGB555, RGB555PC, ARGB32, BGRA32, XRGB32 and A8, so a board with one of those layouts needs only OpenRTG to make such bitmaps and layers to map the format; packed 24-bit, ABGR and RGBA have no OpenGPU format at all.

### Why it is classic

`ComposeLayerInfo(li, NULL, LAYERS_COMPOSE_WHY)` (47.102) answers why a Layer_Info isn't composited, and changes nothing. The reason is kept when Intuition asks (or when a screen goes back to classic):

| Answer | When |
| --- | --- |
| `LAYERS_WHY_COMPOSITED` (0) | it is composited |
| `LAYERS_WHY_NOT_ASKED` (1) | never switched on: a custom screen |
| `LAYERS_WHY_SWITCHED_OFF` (2) | `composite off` when the screen opened, or `ComposeLayerInfo` without `LAYERS_COMPOSE_ON` |
| `LAYERS_WHY_NOT_RTG` (3) | not an OpenRTG bitmap (AGA) |
| `LAYERS_WHY_8BIT` (4) | an 8-bit (CLUT8) screen |
| `LAYERS_WHY_LAYOUT` (5) | a format OpenGPU's driver doesn't composite fully |
| `LAYERS_WHY_CPU_ONLY` (6) | OpenGPU has only its CPU back end |
| `LAYERS_WHY_NO_OPENGPU` (7) | opengpu.library or openrtg.library would not open |
| `LAYERS_WHY_NO_MEMORY` (8) | a surface could not be had and the screen went back to classic |

OpenPrefs' Windows page (openamigaprefs, Windows 0.6.8) shows it beside "Transparency and shadows", as "Off on this screen: 8-bit". 47.101 takes the flag as "back to classic", so a program asks only when `lib_Revision >= 102`.

## How a composited screen works

### Surfaces

- **One surface per layer.** `AllocBitMap(w, h, depth, BMF_CLEAR | BMF_DISPLAYABLE, screen's bitmap)`: OpenRTG makes a friend of an OpenRTG bitmap in its format. 47.103: asked for as displayable, it is in the screen's board's video RAM while displayable friends take at most half of it (OpenRTG 0.19), and in fast RAM after that. The C core reaches either by its Amiga address. The host GPU (OpenGPU's Vulkan back end) reaches only video RAM: a target in fast RAM sends the command to the core, and a source there is copied into the upload arena first. At boot on a 1920 x 1200 Workbench, 1,430 of 1,561 drawing commands ran on the core because their target was in fast RAM (24 October 2026, before 47.103). 47.104: asked for with the extended form and OpenRTG 0.20's `ORTG_BMA_CARD` as well, it goes in the board's card part where there is one (acrtg-v7: the PC graphics card's own memory, which the GPU draws and composes at full speed and the CPU reads at a crawl), then below it, then in fast RAM. Only drawing commands read a surface, so that is safe: layers never hands one to a program.
- **One ClipRect.** The layer's list is one piece covering the layer, `obscured`, its BitMap the surface (kind `K_SURF`). graphics.library and OpenRTG draw there exactly as into a smart refresh layer's backing bitmap, at `x - (MinX & ~15)`. So the layer's pixels start at `MinX & 15` in the surface.
- **Size.** Width `w + 15` (for `MinX & 15`) rounded up to 64, height rounded up to 32, so sizing a window by a few pixels at a time does not make a new surface each time.
- **Moving** never copies on the CPU. A move that changes `MinX & 15` moves the pixels inside the surface with an OpenGPU COPY (overlap safe) at the start of the compose's batch.
- **Growing** past the surface makes a new one; the old pixels go into it with one BltBitMap, and what is new is filled by the layer's backfill hook (or from its super bitmap) and becomes damage, as classic layers do. A window that grows is the only one that gets damage; covering and uncovering a layer never damages it, simple refresh or smart.
- **Clip regions and BeginUpdate** cut the piece as classic layers cut a backing bitmap: each piece gets a bitmap of its own, copied from the surface and back when the region or update goes (`make_sub` and `drop_sub`). While a region stays installed, the compositor copies those pieces into the surface before it composes the layer, under the layer's lock taken only if it is free.

### What is shown

- **The screen's bitmap always holds what is shown.** It is the composition's target; nothing else is.
- **A change of the stack** (create, delete, depth, move, size, hide, show, `SetLayerInfoBounds`) is composed at once, inside the call, under the locks the call holds: the old and new places of the layer it changed, with their shadows, or everything for a change of the bounds.
- **What is drawn** into a layer is marked and composed by the compositor task (`layers.library compositor`, priority 1) as soon as the drawing pauses. It wakes on the first mark, then waits on timer.device in steps of 1 ms until a step passes with no new mark (8 ms at most from the first mark), takes every layer's dirty box and composes their union. A burst of drawing is composed once; a program drawing all the time is composed every 8 ms. Waiting for `WaitTOF` instead (until W56) made drawn-to-shown 20 ms on average, and a click that brought two redraws waited for two frames. Without timer.device it falls back to `WaitTOF`.

### The marks

graphics.library's drawing calls are where the marks come from. The thunks go on the vectors when the first screen is composited, so whichever code draws (OpenGfx's face, OpenGPU's OGFX patches, OpenRTG's provider) the mark follows the call:

- **graphics.library (23 calls):** BltTemplate, ClearEOL, ClearScreen, Text, DrawGList, DrawEllipse, SetRast, Draw, AreaEnd, RectFill, BltPattern, WritePixel, Flood, PolyDraw, ScrollRaster, ClipBlit, BltBitMapRastPort, BltMaskBitMapRastPort, WritePixelLine8, WritePixelArray8, EraseRect, ScrollRasterBF, WriteChunkyPixels.
- **cybergraphics.library (9):** ScalePixelArray, WriteRGBPixel, WritePixelArray, MovePixelArray, InvertPixelArray, FillPixelArray, DoCDrawMethodTagList, WriteLUTPixelArray, WritePixelArrayAlpha.
- **openrtg.library (5):** ORTG_WritePixels, ORTG_FillPixels, ORTG_InvertPixels, ORTG_WritePixelsAlpha, ORTG_PresentVirgl (a GL window's frames).

How a thunk works:

- It is 50 bytes of code in memory. It saves the argument registers, calls the call it stands on with every register as its caller set it, then hands the saved registers to `oc_mark`.
- `oc_mark` returns at once unless the RastPort's layer is a composited one of ours (a magic word and a surface). Otherwise it turns the arguments into a box: the rectangle of RectFill, ScrollRaster and the blits, the font's rows for Text, the whole layer for the rest.
- WORD arguments are read as words, as graphics.library reads them.

Thunks are never taken out, since others may have called on to them. A patch put on top of ours later, such as OpenGfx's when OpenRTG hands its drawing over after boot, may draw without calling ours. So every compose and every composited stack change checks the vectors, and puts a new thunk on top where ours is no longer there.

### A compose

A compose is one OpenGPU batch (stream v1.2's SURFACE, TARGET, CLIP, FILL, COPY and COMPOSITE) and one fence: `OGPU_Submit`, then `OGPU_Wait`. The steps:

1. **The area** (a set of rectangles) is cut to the screen.
2. **Front to back,** each shown layer's part is what of the area is not behind an opaque layer in front. An opaque layer then takes its bounds out of what is left. A layer is opaque at opacity 255 without its own alpha.
3. **What no opaque layer covers** gets the Layer_Info's backfill. `LAYERS_BACKFILL` is a FILL of 0 in the batch, as classic layers clear to 0. A hook is called on the CPU first, with a RastPort that has no layer, as classic layers call it.
4. **Back to front,** each layer's shadow, then the layer:
   - an opaque layer is a COPY from its surface;
   - a translucent one is a COMPOSITE with its opacity;
   - an ARGB32 one is a COMPOSITE with `OGPU_COMP_SRCALPHA`.
5. **Shadows** are COMPOSITE with `OGPU_COMP_SRCALPHA` from one small ARGB32 picture, a (4R + 1) square of black whose alpha ramps linearly over 2R + 1 pixels at each side, multiplied across:
   - the four corners come from it 1:1;
   - the four edges come from its opaque middle row or column, stretched;
   - the middle is under the window and is not drawn.

   COMPOSITE's MASK reads its A8 mask 1:1 against the destination, so a masked shadow would need a mask as long as the screen. The stretched ARGB32 source needs 4.4 KB for R = 8.

A program that draws on the screen's own RastPort, outside every layer, draws into the screen's bitmap. That shows until the next compose of that place, which draws over it.

### Locks

- **Every composited stack change** takes the composited mode's semaphore last, after the Layer_Info's and every layer's lock, as classic layers take theirs. The compositor takes only that semaphore. It takes a layer's lock only with `AttemptSemaphore`, to read a clip region's pieces. So it never waits for a program, and Intuition's menus, which hold layer locks while they are up, are composed while they are shown.
- **A mark** takes no lock: Forbid around the dirty box, and one Signal.
- **OpenGPU's own semaphores** are taken last of all, as by every drawing call.

### Back to classic

A composited stack change first allocates every surface it needs. If one can't be had:

- the Layer_Info leaves the composited mode, and the change goes on as a classic one;
- each layer's one piece, with its BitMap the old surface, is the "old list" from which classic layers move the pixels to their new pieces (the screen, backing bitmaps);
- then the surfaces are freed.

`ComposeLayerInfo` without `LAYERS_COMPOSE_ON` does the same on purpose.

## For programs

Public header: `include/graphics/openlayers.h`. These calls are in layers.library 47.101 and up, after OS 3.2's last LVO.

| LVO | Call | What |
| --- | --- | --- |
| -246 | `ComposeLayerInfo(li, bm, flags)(a0,a1,d0)` | `LAYERS_COMPOSE_ON` switches a Layer_Info with no layers to the composited mode. Without it, back to classic. Answers TRUE while composited. (SetLayerInfoBounds on a composited Layer_Info composes all of it again.) 47.102: `LAYERS_COMPOSE_WHY` alone answers why it is classic (above) |
| -252 | `SetLayerOpacity(layer, opacity)(a0,d0)` | 0-255; answers the old one, or -1 for a classic layer |
| -258 | `SetLayerLook(layer, what, value)(a0,d0,d1)` | `LAYERLOOK_OPACITY`, `_SHADOW`, `_DRAGGED`, `_INACTIVE`, `_ALPHA` (32-bit screens), `_ALL` (all at once), `_QUERY`. The layer is composed again only when what is seen of it changes |

Intuition (amigachrome-guest `libraries/openintuition`):

- `WA_Opacity` (`TAG_USER + 0x41430001`) sets a window's opacity, 0-255, at OpenWindowTagList. Later, `IntuitionControlA(window, tags)` takes the same tag; OS 4's SetWindowAttr has its LVO in the OS 4 SDK, which we don't use.
- `WA_AlphaSurface` (`TAG_USER + 0x41430002`): the window's own alpha shapes it (32-bit screens; the program draws the alpha).
- Intuition gives a shadow to every window with a frame (not backdrop, not borderless) and to menus.
- It marks a window dragged (solid moves, the default feel) and inactive, with one SetLayerLook for each of a new window's layers.
- OpenWindows (openamigaprefs) marks the windows it drags itself (`drag.solid on`) the same way.

## The look's defaults

Each is a line of `ENV:OpenPrefs/Windows`; a missing line is the default.

| Line | Default | What |
| --- | --- | --- |
| `composite on\|off` | on | The switch: "Transparency and shadows" in OpenPrefs Windows |
| `shadows on\|off` | on | Shadows under windows and menus |
| `shadow.size N` | 8 | The shadow's reach in pixels (2-32); it falls R/4 right and R/2 down |
| `shadow.strength N` | 40 | Its darkest, in percent |
| `drag.opacity N` | 75 | A window being dragged, in percent |
| `inactive.opacity N` | 100 | Inactive windows (100: opaque) |

## The contract with Team 4

Team 4 owns OpenGPU (openamigartg) and the host back ends. Team 5 owns layers, graphics, intuition, the ROM build and the prefs.

**What layers uses, and only this.**

- **opengpu.library:** `OGPU_Query`, `OGPU_Submit` and `OGPU_Wait`, through the public stream v1.2 of `include/opengpu/stream.h` and `opengpu.h`: SURFACE, TARGET, CLIP, FILL, COPY and COMPOSITE (with `OGPU_COMP_SRCALPHA`), in RGB565 and ARGB32 (sources); for a 32-bit screen also v1.3's XRGB32, asked with `OGPU_Query` first.
- **openrtg.library:** `ORTG_BitMapInfo`, for a bitmap's memory, bytes a row and format.
- **The opening:** layers opens opengpu.library and openrtg.library when the first screen is composited (from ROM:).
- **What is patched:** the drawing calls listed above, by SetFunction, on top of whatever is there.

**What layers assumes; tell Team 5 before changing it.**

- OpenRTG draws an obscured ClipRect's pixels into its BitMap at `x - (MinX & ~15)` (`screens.c`'s `pieces_then`).
- A friend of an OpenRTG bitmap is an OpenRTG bitmap of the friend's format, and ORTG_BitMapInfo answers for it.
- COPY within one slot is overlap safe.
- A batch's commands run in order, and `OGPU_Wait` on its fence returns when every one is done.
- OpenGPU's back ends read surfaces in fast RAM by their Amiga addresses.

**What Team 5 would like from Team 4.** These are not needed for this change; they would make it smaller or faster.

1. A damage callback from OpenRTG's drawing, or from OGFX's patches, for a layer's surface. Then layers would not need its 37 thunks.
2. A bitmap in video RAM on request, for a friend bitmap or with a tag. Done: OpenRTG 0.19's BMF_DISPLAYABLE, which layers asks for from 47.103. OpenRTG 0.19.3 clears such a bitmap with OpenGPU's FILL, not a byte at a time. The card part: OpenRTG 0.20's `ORTG_BMA_CARD`, which layers asks for from 47.104.
3. `OGPU_Query` for COMPOSITE that says the back end composites on the host GPU, not only "a driver".
4. COMPOSITE's mask stretched like its source. Shadows could then use the documented masked COMPOSITE.

## Size

The composited mode is in the ROM's early boot stream with layers.library (and intuition's part with intuition). The budget is 6 KB packed in all. The measured figures are in the W50 report (team5-work/reports/W50-layers-composite-20261017.md).
