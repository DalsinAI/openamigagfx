# OpenGfx in opengpu.library: the merge record

OpenGfx is part of opengpu.library from 0.6 (8 October 2026). This page records where it came from and every change made on the way, so the history of `opengfx.library` can be followed into this directory.

The plan was a fork first, merged once the OpenGfx work's testing was done. That testing finished with no changes to the library, so the fork was never needed: the code was brought across once, at the commit below, and merged at the same time.

## Where it came from

| | |
| --- | --- |
| Repository | `DalsinAI/amigachrome-guest` |
| Path | `libraries/opengfx` (and `tests/m68k` for the tests, `common/amiga/ac_magic.h` for the tag) |
| Commit | `90aa66f` (#35): opengfx.library 1.1, all eight graphics.library drawing and text patches; Chip RAM fills wait for the blitter |
| Changes there since | none (`git log 90aa66f..origin/main -- libraries/opengfx` is empty on 8 October 2026) |

The old library is kept in amigachrome-guest, archived in place (`libraries/opengfx/library/opengfx_lib.c`) and never deleted. In its place `opengfx.library` 1.2 is a stub that sends its six calls to opengpu.library (see "Compatibility" below).

## What came across, and what changed

| File here | From | Changes |
| --- | --- | --- |
| `ogfx_leaves.c`, `ogfx_leaves.h` | `libraries/opengfx/` | The include of `ac_magic.h` is local; two comments name the new places. The leaves themselves are byte for byte as they were. |
| `ogfx_composite.c`, `ogfx_composite.h` | `libraries/opengfx/` | None. Not linked into the library yet: nothing calls them. |
| `ac_magic.h` | `common/amiga/ac_magic.h` | A copy, with a note that the guest's is the source of truth for the tag and the ids. One comment names the leaves' new path. |
| `ogfx_lib.c` | `libraries/opengfx/library/opengfx_lib.c` | The glue, rebuilt for the one library: see below. |
| `ogfx.h`, `ogfx_tramp.h` | new | The state OpenGfx keeps in opengpu.library's base, and the patches' entries. |
| `../../include/opengpu/gfx.h` | `include/libraries/opengfx.h` | The public header: the same provider record, request structures and status bits (ABI v1, unchanged), plus the LVO list. |
| `../../tests/ogfx/` | `tests/m68k/` | The leaves' and the composite's tests, their paths changed; a host run (32-bit, UBSan), an os32 object check and a patch-entry check added (`run.sh`). |
| `../../tools/ogfx_check.c` | new | OpenGfxCheck, run on an Amiga: OpenGfx against graphics.library, call by call, and timings. |

### The glue (`ogfx_lib.c`)

- **The calls are LVOs of opengpu.library, from 66,** in opengfx.library's order: `OGFX_Version`, `OGFX_InstallPatches`, `OGFX_SetEnabled`, `OGFX_Status`, `OGFX_RegisterProvider`, `OGFX_UnregisterProvider`. Their arguments, registers and results are unchanged.
- **The eight graphics.library calls are LVOs too** (102 to 144): `OGFX_Text`, `OGFX_TextLength`, `OGFX_TextExtent`, `OGFX_TextFit`, `OGFX_RectFill`, `OGFX_BltBitMap`, `OGFX_BltTemplate`, `OGFX_ScrollRaster`, with graphics.library's arguments in graphics.library's registers. They run the same code as the patches.
- **The patches are thin entries.** `OGFX_InstallPatches` writes eight 18-byte entries into the base (`ogfx_tramp.h`: save A4, load the state, call the C, restore A4) and points graphics.library's vectors at them. The C behind each entry is the call's code, built in, so a patched call costs one extra `JSR`.
- **No writable globals.** opengfx.library kept `SysBase`, `GfxBase`, `ExpansionBase` and its base in globals. Everything is now in `struct ogfx_state` inside opengpu.library's base: exec comes from the base, graphics.library is opened on first need, expansion.library only while `OGFX_InstallPatches` looks for Dalsin boards. `tests/ogfx/run.sh` checks that the objects have no `.data` or `.bss`.
- **Nothing from disk at init.** Init only records exec and switches OpenGfx's own drawing on. Opening the library changes no vector, as before.
- **The library stays while patched.** opengpu.library refuses to expunge while graphics.library points into it.
- **`OGFX_Version`** answers 1.2: the OpenGfx interface 1.1, inside opengpu.library, with the eight drawing LVOs.
- **The order is 1.1's:** the provider first, then the native planar paths for `RectFill`, `BltBitMap` and unlayered `ScrollRaster`, then graphics.library's own code. Chip RAM goes to the leaves only on AmigaChrome, after `WaitBlit()`. Every patch reads its 16-bit arguments from the registers' low words.
- **Two changes to what 1.1 did,** found by OpenGfxCheck, which compares OpenGfx with graphics.library call by call on an OS 3.2.3 instance:
  - `BltBitMap` on Chip RAM stays with the blitter on AmigaChrome too. There the planar-blit leaf (a bit at a time, as host code) copied 640 x 480 x 8 planes in 11.9 ms and 64 x 64 in 0.90 ms, where graphics.library's blitter path took 0.20 and 0.05. The leaf takes Fast RAM bitmaps, which the blitter can't reach. The Chip RAM test comes before anything else, because Picasso96's `GetBitMapAttr` waits for a blit in flight (1.4 ms a full-screen copy when it came after).
  - `ScrollRaster` by as much as the area or more goes to graphics.library, which leaves the area as it is; 1.1's planar path cleared it with the background pen.
  
  A `BltBitMap` with no plane in its mask also goes to graphics.library now (1.1 answered 0 itself). Everything else matched graphics.library byte for byte: `RectFill`, `ScrollRaster`, `BltBitMap` in Chip and Fast RAM, and the text calls and `BltTemplate`, which pass through. `BltBitMap` answers with the planes drawn, as graphics.library's own does; Picasso96's `BltBitMap` counts every plane the two bitmaps share, so under Picasso96 the two answers can differ for a masked copy.

### OpenRTG

`library/screens.c` registers OpenRTG's provider with opengpu.library when it is 0.6 or later (it already had it open) and puts the patches in there. With an older opengpu.library it opens `opengfx.library` as before, and with neither it keeps its own patches. openrtg.library is 0.12.

## Since the merge

- **opengpu.library 0.7 (8 October 2026): the look hook.** `OGFX_RegisterLook` and `OGFX_UnregisterLook` (LVOs 150 and 156, `struct OGFXLookV1` in `include/opengpu/gfx.h`). A look is asked first on `RectFill` and `Text`, so OpenLook draws window frames here instead of patching the two calls on top of OpenGfx. The state's new fields come after the patch entries, so `old[]` stays where it was. `OGFX_Version` answers 1.3; `OGFX_Status` adds `OGFX_STATUS_LOOK` and `OGFX_STATUS_LOOK_BUSY`. OpenGfxCheck checks the hook when no other look is in.

- **opengpu.library 0.8 (8 October 2026): every drawing call.** OpenGfx 1.4 patches 22 `graphics.library` calls: the eight, then `BltPattern`, `SetRast`, `Draw`, `PolyDraw`, `WritePixel`, `ReadPixel`, `BltBitMapRastPort`, `BltMaskBitMapRastPort`, `ClipBlit`, `WriteChunkyPixels`, `WritePixelArray8`, `WritePixelLine8`, `ReadPixelLine8` and `ReadPixelArray8`. Each asks the provider (a `struct OGFXProviderAll`, which starts with the v1 record) and otherwise calls `graphics.library`'s code with the registers as they came; there is no native path for them yet, and no LVOs. `old[]` and the entries grow to 22, `old[]` still just before the entries. openrtg.library 0.13 provides the fourteen and stops patching them when `OGFX_Version()` is 1.4 or later.

## Compatibility

- **`opengfx.library` 1.2, the stub** (amigachrome-guest `libraries/opengfx/stub`, 1.7 KB): opening it opens opengpu.library; each of its six calls jumps to the same LVO there. With no opengpu.library, or one older than 0.6, opening it fails, as when it is not installed, so programs fall back as they always have.
- **Why a stub, not an assign or a second copy:** a program or provider built for opengfx.library keeps its base and its calls; there is one set of patches and one provider slot, in opengpu.library, however a program got there; and the stub has nothing to keep in step.
- **OpenUp and OpenPrefs** open neither opengfx.library nor OpenGfx today (`tools/build_pack.py` ships opengpu.library and openrtg.library in the OpenRTG part, and OpenPrefs has no OpenGfx setting), so they need no change. The OpenRTG part picks OpenGfx up with the next openrtg.library and opengpu.library.

## Checked

See the pull request for the results: `tests/run.sh` (with `tests/ogfx/run.sh`), `tests/run_3d.sh`, the library build with `tools/fpcr_check.py`, and on a scratch copy of an OS 3.2.3 instance OpenGfxCheck, OpenGfxLibTest, OpenGPUCheck, OpenRTGExact, Workbench and MultiView, with OpenGfx on and off.
