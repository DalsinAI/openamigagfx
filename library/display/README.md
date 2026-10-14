# OpenGfx graphics.library: the display

This directory is the display area of OpenGfx's `graphics.library` (see `library/graphics/README.md` for the face and `OWNERS.md` for who owns what). It is clean-room work from the NDK 3.2 headers, the autodocs, the Hardware Reference Manual and OS 3.2's measured behaviour. It covers 43 LVOs:

- Views and ViewPorts: InitView, InitVPort, MakeVPort, MrgCop, LoadView, FreeVPortCopLists, FreeCopList, FreeCprList, ScrollVPort, ChangeVPBitMap, AllocDBufInfo, FreeDBufInfo and CalcIVG.
- User copper lists: UCopperListInit, CWait, CMove and CBump. CINIT, CWAIT, CMOVE and CEND are the NDK macros over these.
- The display database: FindDisplayInfo, NextDisplayInfo, GetDisplayInfoData, BestModeIDA, ModeNotAvailable, GetVPModeID and CoerceMode.
- GfxNew, GfxFree, GfxAssociate, GfxLookUp, VideoControl, OpenMonitor, CloseMonitor and SetChipRev.
- The beam: VBeamPos, WaitTOF and WaitBOVP.
- Sprites: GetSprite, FreeSprite, ChangeSprite, MoveSprite, AllocSpriteDataA, FreeSpriteData, GetExtSpriteA and ChangeExtSpriteA.

| File | What |
| --- | --- |
| `display_types.h` | The NDK layouts in stdint types (`gd_` prefix), with 68k static_asserts: View, ViewPort, CopList, CopIns, UCopList, cprlist, copinit, ViewExtra, ViewPortExtra, MonitorSpec, SpecialMonitor, DBufInfo, the DisplayInfo records, SimpleSprite and ExtSprite. It also has the register offsets and the tags. |
| `display.h` | The calls in plain C arguments (`gd_<Call>(env, ...)`), plus `gd_init`, `gd_exit`, `gd_vblank` and `gfx_hook_vp_colors`. |
| `display_int.h` | The core's private state. It is kept in `gfx_private.area[GFX_AREA_DISPLAY]`, because the library has no .bss. |
| `display_view.cpp` | Lean C++. It turns a ViewPort into copper (DspIns), merges the lists (MrgCop), and handles LoadView, ScrollVPort, user copper lists, double buffering and the vertical blank. |
| `display_hw.cpp` | Lean C++: the beam, WaitTOF, the chip revision and the sprites. |
| `display_db.cpp` | Lean C++: the display database, monitors, extended nodes, VideoControl and `gd_init`. |
| `display_lvo.c` | The 43 `gfx_lvo_` register entries, in graphics.lvo order with A6 last. `gfx_init_display` installs GfxBase->vbsrv, the vertical blank server, through the env. |
| `graphics.sources` | The files that `library/graphics/build-amiga.sh` builds into the library. |
| `build-amiga.sh` | Builds `DisplayABITest` with the os32-gcc16 stove. |
| `tests/m68k/test_gfx_display.sh` | The qemu test (below). |
| `tests/DisplayABITest.c` | One script run through OS 3.2's graphics.library and then through opengfx.library, compared line by line. `PROBE` prints OS 3.2's answers only. |

## How a ViewPort becomes copper

MakeVPort writes the ViewPort's DspIns in three blocks:

1. **The gap.** IVG lines before the ViewPort's first line, it sets the window, the fetch, the modulos, BPLCON1–4 (with BPLCON0 having no planes yet), the bitplane pointers and the colours.
2. **The start.** A WAIT on the first line, then BPLCON0 with its planes.
3. **The end.** A WAIT on the line after the last, then BPLCON0 with no planes.

On AGA the colours are loaded bank by bank: BPLCON3 with the bank number, the high nibbles, then LOCT and the low nibbles. On AGA it loads at least 32 entries, or as many as the ViewPort can show: its depth, BPLAM's XOR and PF2's base. With FULLPALETTE it loads them all.

MrgCop gathers every visible ViewPort's DspIns, SprIns, ClrIns and UCopIns:

- DspIns WAITs are frame lines. The other lists' WAITs count from the ViewPort's top.
- It sorts them stably by beam position and adds the `0xFFDF` wait before line 256.
- It writes one hardware list per field. A laced View gets a long-frame list and a short-frame list.
- Lists that are big enough are reused while the View is not on display. A View on display always gets new lists, since the copper may be running the old ones; they are switched to at the next vertical blank, and the old ones are freed after it.

ScrollVPort, ChangeVPBitMap and colour changes (`gfx_hook_vp_colors`) rewrite the gap block in place in each hardware list. They do not need a new MrgCop.

The copper starts at copinit, which is in Chip RAM: FMODE, a blank window, BPLCON2, the eight sprite pointers, then a jump to COP2LC, which holds the View's list for the coming field. LoadView sets COP1LC and COP2LC, and also BEAMCON0 on ECS and AGA. The vertical blank server picks a laced View's list from VPOSR's LOF bit.

The horizontal position follows the HRM:

- Lores data shows from 2·DDFSTRT+17, and hires from 2·DDFSTRT+9 (in lores pixels).
- Each playfield's scroll (BPLCON1) and the fetch start are worked out so the first word fetched is the one holding RxOffset at DIW start.
- DDFSTOP covers the window, in whole fetch units, and is clamped at 0xD8.
- OCS and ECS scroll in lores pixels, so a hires RxOffset is rounded down to an even number.

## Edge cases

| Case | What we do | Source |
| --- | --- | --- |
| InitView | Every byte 0, then DxOffset 0x81 and DyOffset 0x2C (STANDARD_VIEW_X/Y) | ABI test (INITVIEW) |
| InitVPort | Every byte 0 | ABI test (INITVPORT) |
| MakeVPort with no RasInfo or bitmap, or too many planes for the chips (fetch mode 1×: lores 6, or 8 on AGA; hires 4; super hires 2) | MVP_NO_DISPLAY | documented codes; the limits are ours |
| MakeVPort below the frame | MVP_OFF_BOTTOM, with the old DspIns left alone | ABI test (VIEW offbottom) |
| VP_HIDE | MakeVPort answers MVP_OK and MrgCop leaves the ViewPort out | documented; ABI test |
| A ViewPort wholly clipped (by the monitor or a DisplayClip) | MVP_OK with no DspIns | ours |
| DyOffset in a laced ViewPort | In laced rows: halved and rounded down, so an odd DyOffset starts on the same frame line as DyOffset − 1. Even rows are in the long frame, odd rows in the short | ours; test |
| A View with no ViewPorts | MrgCop gives lists that only end the frame | ABI test (VIEW empty) |
| MrgCop out of memory | MCOP_NO_MEM, with the View's old lists kept | documented |
| Six planes without EXTRA_HALFBRITE, HAM or dual playfield | On AGA, BPLCON2's KILLEHB, so 64 colours; OCS and ECS have no such bit and show them half-bright | HRM (KILLEHB is AGA's); ABI test |
| ChangeVPBitMap's messages | dbi_SafeMessage at the next vertical blank (the old bitmap is no longer shown), dbi_DispMessage at the blank after it (a field of the new one has been shown) | autodoc; ABI test for the blanks |
| CoerceMode with AVOID_FLICKER | The non-interlaced modes are tried first, then the interlaced | autodoc (the flag); ABI test for the order |
| GfxFree of a node that is not one of GfxNew's (the type, subsystem or library not GfxNew's) | Left alone | ABI test |
| ScrollVPort on a ViewPort that is not shown | MakeVPort makes it again when it is merged | ABI test |
| MOVEs in a user copper list before its first WAIT | At the ViewPort's top line | ABI test |
| CBump past a CopList's end | Another CopList is chained on. With no memory, the last instruction is written over | documented / ours |
| UCopperListInit on a UCopList that already has a list | That list is reused when it is big enough and not chained; otherwise a new one | ABI test (UCOP init) |
| FreeVPortCopLists | Frees DspIns, SprIns, ClrIns and the UCopList, including the UCopList structure itself | documented; ABI test (UCOP freed) |
| SpritePriorities 0 | BPLCON2 0x24, as OS 3.2 does | ABI test |
| MoveSprite | x in the ViewPort's pixels and y in its lines; HSTART is the View's left edge plus x − 1 | ABI test (SPRITE moved) |
| GetSprite of a sprite that is taken, or -1 with none free | -1 | documented |
| AllocSpriteDataA defaults | num -1, height from the bitmap, word width 1, posctldata 8-aligned in Chip RAM | ABI test (SPRDATA) |
| GetExtSpriteA with old-format data and no OutputHeight | NULL: old data does not say how tall it is | ABI test |
| GfxNew | xln_Library is GfxBase, xln_Init is 0 (OS 3.2 puts a routine there), a MonitorSpec's DisplayInfoDataBase is NewList'd | ABI test (GFXNEW) |
| VideoControl with an unknown tag | Answers 1 and goes on | ABI test (VC unknown) |
| NextDisplayInfo of an ID not in the database | The next ID above it | ABI test (NEXT unknown) |
| ModeNotAvailable of an unknown monitor | DI_AVAIL_NOMONITOR | ABI test (NOTAVAIL) |
| GetDisplayInfoData DTAG_NAME for the default monitor's modes | 0: only NTSC and PAL modes have names | ABI test (MODE) |
| GetDisplayInfoData DTAG_VEC, or an unknown ID | 0 | ABI test (VEC, UNKNOWN) |
| GetDisplayInfoData with a short buffer | Copies as many bytes as fit and answers that count | documented; ABI test (SHORT) |
| PaletteRange | 4096 on OCS and ECS; 0xFFFF on AGA | ABI test |
| MonitorInfo ViewResolution | 44, 44 | ABI test (MNTR) |
| BestModeIDA with no DIPFMustNotHave | The default (SPECIAL_FLAGS) minus whatever DIPFMustHave asks for | documented default; the subtraction is ABI test |
| BestModeIDA ties | Scored in order: fits the size, closest aspect, closest size, then the asked-for monitor | ours |
| GetVPModeID of a ViewPort with no ColorMap or VPModeID | Its Modes on the default monitor | ABI test (VPMODEID old) |
| CloseMonitor | 0 when closed; 1 when it was not open; 0 for NULL | autodoc; ABI test |
| Alice's chip ID 0x23 | PAL | ABI test |
| WaitBOVP while the beam wraps into the next frame | Returns | ours |
| GfxBase's NormalDisplayRows, MicrosPerLine and similar fields | OS 3.2's values | ABI test (GFXBASE) |
| GfxBase's LastChanceMemory, ActiViewCprSemaphore, MonitorListSemaphore and HashTableSemaphore | Each points at a SignalSemaphore of its own, initialised in the display's state. A monitor driver run by LoadMonDrvs takes MonitorListSemaphore; when it was NULL, ObtainSemaphore read low memory as a semaphore owned by another task and the driver waited for ever | NDK `graphics/gfxbase.h` (the fields); test |
| opengfx.library opened next to OS 3.2 | Every call works, but no custom chip register is written (`gd_init`'s `own_hw`) | ours; test |

## The tests

`tests/m68k/test_gfx_display.sh` builds the core with m68k-linux-gnu-g++ 13.3 for the 68000, 68020 and 68040 at -O0 and -O2. It runs each build under qemu-m68k 8.2 on a simulated machine: exec's memory, the custom registers, a copper running the lists four colour clocks per instruction, and an Agnus/Denise/Lisa that shows the bitplanes line by line. The display models OCS, ECS and AGA, HAM6, HAM8, EHB, dual playfield, PF2OF, BPLAM and LOCT.

`test_gfx_display_model.py` is written separately from the library and does not model the copper. It makes random scenes and says, from the documented View and ViewPort rules, what each ViewPort must show. The scenes cover:

- OCS, ECS and AGA; PAL and NTSC.
- Laced and plain Views, with or without a ViewExtra.
- One to three ViewPorts each of lores, hires, interlace, EHB, HAM, dual playfield, scrolled, or clipped.
- User copper lists, and 32 to 256 colours.

For each ViewPort, it checks the picture after MakeVPort, MrgCop and LoadView, after ScrollVPort, and after ChangeVPBitMap, in each field. Pictures are compared by a hash of each ViewPort's rectangle.

The harness also checks:

- The database, monitors, VideoControl, sprites, the beam and WaitTOF.
- Running out of memory, and all memory given back.
- The side-by-side mode.

After that, the script:

- Runs `check_regs.py` on `display_lvo.c`.
- Compiles `display_lvo.c` as C.
- Runs nm checks: the core calls nothing but libgcc, and there is no .data, .bss or C++ runtime.

Result (m68k-linux-gnu-g++ 13.3.0, qemu-m68k 8.2.2, seed 20261013, 60 scenes): every build shows the same 420 ViewPort pictures as the model, with 1648 checks and 0 failed.

`DisplayABITest` has not yet been run on OS 3.2. It needs the stove and a boot with DEVS:Monitors empty, because OpenGfx has only the built-in PAL and NTSC monitors.

## Not yet

- Fetch modes above 1× on AGA: FMODE stays 0 for bitplanes. That limits depth (no 8-plane hires) and wide sprites (SPRITEA_Width 32 and 64 are allocated, but shown at 16).
- Super hires is built and placed but not simulated pixel by pixel in the test.
- DIWHIGH (windows past line 511 or ECS's extended horizontal range) and the productivity, VGA and other monitors' beam programming. The database has only the default, NTSC and PAL monitors.
- Monitor drivers from DEVS:Monitors (the private AddDisplayInfo slots) and DTAG_VEC.
- `gfx_hook_vsprites`: SprIns from gels' VSprites is still the stub.
- copinit's wait14 is a NOOP, not a wait at line 10.
- `DisplayABITest` has not yet been run against OS 3.2.
