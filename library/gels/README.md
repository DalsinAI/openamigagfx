# OpenGfx graphics.library: gels and the blitter

This is the gels area of OpenGfx's `graphics.library` 47.100 (clean-room, MIT, API and ABI compatible with OS 3.2's). It covers the GELs: VSprites, Bobs, AnimObs and their collisions. It also covers the blitter: its ownership, its queue, and the blits graphics.library draws through it. It owns 23 LVOs (`library/graphics/OWNERS.md`): InitGels, AddBob, AddVSprite, RemIBob, RemVSprite, SortGList, DrawGList, DoCollision, SetCollision, InitMasks, InitGMasks, AddAnimOb, Animate, GetGBuffers, FreeGBuffers, OwnBlitter, DisownBlitter, WaitBlit, QBlit, QBSBlit, BltClear, BltTemplate and BltMaskBitMapRastPort. RemBob is a macro (it sets BOBSAWAY), not an LVO.

It is written from the NDK 3.2 headers' layouts (graphics/gels.h, the GelsInfo of graphics/rastport.h, hardware/blit.h), the autodocs, the Hardware Reference Manual's blitter and OS 3.2's measured answers (`tests/GelsABITest.c`). No Kickstart, Hyperion or AROS code was used.

| File | What |
| --- | --- |
| `gels_core.h`, `gels_core.cpp` | The work, in lean C++ with no Amiga headers: no exceptions, RTTI, static constructors or libstdc++, and no .data or .bss. Exec and the custom chips are reached only through `struct gfx_env` (`library/graphics/gfx_env.h`). The layouts of VSprite, Bob, AnimComp, AnimOb, DBufPacket, collTable, GelsInfo and bltnode are here, with 68k static_asserts. |
| `gels_lvo.c` | The 23 `gfx_lvo_<Call>` register entries (checked by `library/graphics/check_regs.py`), `gfx_init_gels` and `gfx_exit_gels`, and the calls into user code that need registers. A QBlit routine is called with A0 = the custom chips and A1 = the node. The blitter's interrupt is an exec handler (`SetIntVector`, INTB_BLIT, GfxBase's `bltsrv`), and the QBSBlit beam wait is a VERTB server of gels' own. As the side-by-side `opengfx.library` it leaves the blitter, its queue and INTB_BLIT to OS 3.2's graphics.library and blits with the CPU (`has_blitter` 0), as the display leaves the custom chips alone there. The blitter is still shared, so there OwnBlitter, DisownBlitter, WaitBlit, QBlit and QBSBlit go to OS 3.2's graphics.library (`sys_blitter`, which `gfx_init_gels` opens), its LVOs and the ones gels makes around its own CPU blits alike: a CPU blit runs inside the OS's OwnBlitter and after its WaitBlit, and a queued node gets the OS's completion interrupt. |
| `graphics.sources` | The files `library/graphics/build-amiga.sh` builds into the library. |
| `build-amiga.sh` | Builds the library (through `library/graphics/build-amiga.sh`) and `GelsABITest`, with the os32-gcc16 stove (GCC 16.2.0-open2). |
| `tests/m68k/test_gfx_gels.sh`, `.c`, `.py` | The core and the register entries for the 68040 at -O0 and -O2, under qemu-m68k, on a simulated machine. The simulation has Chip and Fast RAM, and a blitter written from the HRM that runs a blit only when it finishes, a few DMACONR reads after BLTSIZE, so a CPU write made before WaitBlit shows up. It also has the blitter's interrupt (held while Disable()d), the beam, and tasks that block in OwnBlitter. A python3 model, written a pixel at a time with no blitter and no words, writes the script and what each step must print, and the two are compared line by line. Also nm checks: no data, no bss, no C++ runtime, and nothing called but libgcc and the env. |
| `tests/GelsABITest.c` | One script run through OS 3.2's graphics.library and then through opengfx.library, line by line. `PROBE` records OS 3.2's answers. `NEST` adds nested OwnBlitter, which may hang OS 3.2. |

Results: `sh tests/m68k/test_gfx_gels.sh` (m68k-linux-gnu-gcc/g++ 13, qemu-m68k): all six builds agree with the model on 2111 lines over 280 rounds. There were about 1800 simulated blits, about 1100 of them drawing through the blitter (A as mask, B as data, C and D the destination) and about 800 of those shifted. Some BltMaskBitMapRastPort calls take the RastPort's own bitmap as the source, overlapping. The Amiga build and GelsABITest need the stove and an OS 3.2 machine, which were not available here, so nothing has been measured on OS 3.2 yet.

## How it works

**Blits.** Each blit (BltTemplate, BltMaskBitMapRastPort, and a Bob's image, save and restore) is cut into its RastPort's places. An unlayered RastPort has one place, its bitmap. A layered one has its ClipRects: the screen where the layer is on top, and a smart refresh or super bitmap layer's own bitmap where it is covered, at `x - (MinX & ~15)`. Nothing is drawn for a simple refresh layer's covered parts. A super bitmap layer also gets its whole super bitmap, the window's part included, so it stays what the screen shows (the face keeps it so too). Each piece is then done one plane at a time as `D = minterm(A, B, C)`. The blitter does it when it can do it exactly: Chip RAM; A and B at one alignment that the destination's is the same as or to the right of (ascending, shifted right); and D = C wherever A is 0. FWM and LWM make A 0 outside the rectangle, and a blit is at most 64 words wide and 1024 rows a go. Otherwise the CPU does it word by word, after WaitBlit, so a pending blit is never overtaken. A machine with no blitter (`has_blitter` 0) does everything on the CPU, with the same pixels.

**The queue.** It lives in GfxBase's own fields: blthd/blttl, bsblthd/bsblttl, BlitLock, BlitNest, BlitWaitQ and BlitOwner. BlitLock is 1 while a task (BlitOwner) or the queue (BlitOwner 0) has the blitter. With interrupts disabled, `service` runs a queued routine whenever the blitter is free and idle, and calls it again after its blit finishes while it answers non-zero. Once it answers 0 and its blit is done, the node leaves the queue and its cleanup is called if `stat` has CLEANUP. Then the next node runs: QBlit's queue first, then QBSBlit's once the beam is at its `beamsync`, then the tasks waiting in OwnBlitter, which are woken with SIGF_BLIT. The blitter's interrupt carries the queue from one blit to the next.

**DrawGList.** There are four steps:
1. Every Bob that the last DrawGList saved gets its background back, newest first, along the chain from `gelHead->ClearPath`. A double buffered Bob's last save is on the other bitmap, so it keeps that one. Its save from the DrawGList before is put back instead (the chain from `gelTail->ClearPath`, through `DBufPacket.BufPath`), and the two saves change places.
2. A Bob with BOBSAWAY that has no saved background left leaves the list with BOBNIX. A double buffered one takes two DrawGLists.
3. VSprites get hardware sprites, and `gfx_hook_vsprites` hands them to display.
4. The Bobs are drawn in the list's order, each after the Bobs its After chain names. Each one saves its background first under SAVEBACK.

## Edge cases

| Case | What we do | Source |
| --- | --- | --- |
| InitGels | head X, Y = -32768, tail = 32767, linked; GelsInfo's gelHead, gelTail; both ClearPaths 0; nothing else touched | autodoc for the links; the rest ABI test (INITGELS) |
| AddVSprite of equal Y, X | after the ones already there | ABI test (ADDVS) |
| AddVSprite's flags | the system's byte (0xFF00) cleared | autodoc ("sets up the system flags"); ABI test |
| AddBob of a Bob still listed with BOBSAWAY (an Animate ring that comes back to it before a DrawGList) | it stays, keeping its saves, and is sorted in again | ours; ABI test |
| A Bob's SaveBuffer | Width × Height × the bitmap's depth words: the Bob's own pixels at its own alignment, so a shifted Bob needs no extra word | RKM's size; ABI test (SAVEBUFFER past ...) |
| PlanePick with more planes than Depth | the extra planes take PlaneOnOff | ABI test |
| OVERLAY with no ImageShadow | the whole rectangle | ours; ABI test |
| A Bob and the RastPort's Mask | Mask is not applied; only PlanePick and PlaneOnOff say which planes are drawn | ABI test |
| Bobs off the bitmap or a layer | clipped to the places; GelsInfo's leftmost to bottommost are for DoCollision only | ABI test |
| Before/After | a Bob is drawn after its After chain's undrawn Bobs; Before is not followed; a loop is broken | gels.h comments; ABI test (DRAWGLIST after/before) |
| RemIBob under other Bobs | those drawn after it are erased, newest first, then it; it leaves the list with BOBNIX; the others are drawn again in their order. A double buffered Bob's save on the other bitmap is dropped | ours; ABI test (REMIBOB) |
| Double buffering | the two saves (SaveBuffer, OldX/OldY) and (BufBuffer, BufX/BufY) swap at each DrawGList; BufPath links the older chain | ABI test (DBUF) |
| DoCollision | in list order: a gel with HitMask bit 0 against the borders (routine 0, called with the gel and TOPHIT, BOTTOMHIT, LEFTHIT, RIGHTHIT; horizontally by BorderLine), then each later gel whose rectangle starts by its last line, the earlier HitMask against the later MeMask, bit 0 left out; the lowest common bit picks the routine, called with (earlier, later); pixels meet by CollMask, or by rectangle where one has none | RKM; order and roles ABI test (DOCOLLISION) |
| SetCollision with num ≥ 16 | ignored | ours |
| InitMasks of a true VSprite | two planes a line at a time | gels.h's VSprite format |
| VSprite placement | lowest free sprite not in sprRsrvd, free from Y + Height + 1, whose pair shows the same colours or none at that line; else VSOVERFLOW; Height ≤ 0 GELGONE; nextLine, lastColor reset each DrawGList | ABI test |
| Animate | Clock++, AnOld = An, Vel += Accel, An += Vel, the AnimOb's routine; each component's Timer counts down when TimeSet is not 0, and at 0 the next of its sequence takes its place (BOBSAWAY on the old Bob, AddBob for the new, Timer = TimeSet, RINGTRIGGER adds RingX/YTrans); Bob X = (AnX >> 6) + XTrans; then each component's routine | RKM; order and rounding ABI test (ANIMATE) |
| AddAnimOb | at the head of the key's list; each first component gets HeadOb, Timer = TimeSet and AddBob | ABI test (ADDANIMOB) |
| GetGBuffers | ImageShadow and CollMask apart, BorderLine, SaveBuffer (the bitmap's depth), with db a cleared DBufPacket and BufBuffer, all Chip RAM; out of memory, every one it got is freed and the pointers are 0 | autodoc; ABI test (GETGBUFFERS) |
| FreeGBuffers | frees what is not 0 and sets the pointers 0 | ours |
| BltClear | words: bytecount / 2, or rows × (bytes per row / 2) with flags bit 1; bit 0 waits; bit 2 fills with $FF; by the CPU outside Chip RAM; any size, in blits of up to 64 × 1024 words | autodoc; odd counts ABI test (BLTCLEAR) |
| BltTemplate DrawModes | INVERSVID swaps 1 and 0; COMPLEMENT inverts the 1s (and every pixel with JAM2); else 1 is FgPen and 0 is BgPen under JAM2; through Mask | as `library/core` measures for Text; ABI test (BLTTEMPLATE) |
| BltMaskBitMapRastPort minterms | D = ~(F(mask, ~(dest ^ src)) ^ src), F the minterm's high four bits as BltBitMap's: (ABC\|ABNC\|ANBC) copies the source through the mask and ANBC inverts it, and outside the mask D = dest for both | autodoc for the two; the rest ABI test (BLTMASKBITMAPRASTPORT) |
| BltMaskBitMapRastPort, the source the destination's bitmap | Read from a copy made first, so an overlap gives the source as it was. Of an interleaved source only each plane's part of each row is copied (the next planes follow it in the row, and its last row ends the block), and the copy is freed only after the last blit reading it, as MEMF_ANY may be Chip RAM | ours; the qemu test puts the bitmap just before a page that faults and checks no blit is in progress on a freed block |
| An interleaved bitmap's plane width | BytesPerRow / Depth with BMF_INTERLEAVED, where its first two real planes (not 0 or -1) are that far apart for each plane between them, or it has fewer than two | ours |
| The side-by-side opengfx.library's blitter | OS 3.2's: OwnBlitter, DisownBlitter, WaitBlit, QBlit and QBSBlit are its, and gels' CPU blits run inside its OwnBlitter after its WaitBlit | ours; qemu test with a stand-in |
| BltMaskBitMapRastPort's mask rows | the source's width / 8 bytes | autodoc ("same size as a plane"); interleaved sources ABI test |
| OwnBlitter twice by one task | BlitNest counts, and DisownBlitter undoes one | ABI test (NEST, may hang OS 3.2) |
| OwnBlitter while the queue has nodes | waits until the queue is empty | autodoc |
| QBlit with the blitter free | the routine is called at once, before QBlit returns | ABI test (QBLIT) |
| A queue routine | A0 = $DFF000, A1 = the node; called again after its blit while it answers non-zero; cleanup the same way after its last blit, with CLEANUP | autodoc; cleanup's registers ABI test |
| QBSBlit | runs when the blitter is free and the beam is at `beamsync`: checked whenever the queue moves, and the VERTB server waits for the line (giving up at a new frame) | ABI test (QBSBLIT) |
| WaitBlit | DMACONR read twice (the first read after a start may not show BBUSY), then until BBUSY clears | HRM |
| The blitter's previous-word registers at a blit's start | taken as 0 (only matters for a first word that shows A's shifted-in bits, which every blit here masks) | HRM; the simulation assumes it |
| gfx_hook_blt_bitmap (BltBitMap the face's copy does not take) | -1: not taken | ours |

## Not yet

- **VSprites on the screen.** DrawGList places them and hands them to gfx-display (`gfx_hook_vsprites`), but the sprite copper list is display's. GelsABITest does not measure placement, because DrawGList with VSprites needs a real ViewPort.
- **Faster blits.** A Bob or blit that is clipped on its left, or whose source sits to the right of its destination within a word, goes to the CPU. So does anything wider than 64 words, or a minterm with D ≠ C where A is 0. The blitter could take more of these, with descending mode and a second pass.
- **ECS/AGA big blits** (BLTSIZV/BLTSIZH): blits are cut to OCS sizes.
- **QBSBlit by the copper.** The VERTB server waits for the line, which takes CPU time when `beamsync` is low on the screen. A copper interrupt at that line would need gfx-display.
- **A super bitmap layer's** parts beyond its window are drawn here into the super bitmap, but the qemu test has no super bitmap layer yet.
- **Nothing measured on OS 3.2 yet.** Every row above marked ABI test is ours until GelsABITest's PROBE has been run.
