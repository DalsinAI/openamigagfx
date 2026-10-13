# OpenGfx text

graphics.library's fonts and text for OpenGfx's graphics.library 47.100: clean-room, MIT, API- and ABI-compatible with OS 3.2's. These are the 18 LVOs `library/graphics/OWNERS.md` gives gfx-text:

- ClearEOL, ClearScreen
- TextLength, Text, TextExtent, TextFit
- SetFont, AskFont, OpenFont, CloseFont, AddFont, RemFont
- AskSoftStyle, SetSoftStyle
- FontExtent, WeighTAMatch, ExtendFont, StripFont

The area also has the ROM fonts topaz 8 and topaz 9, redrawn.

| File | What |
| --- | --- |
| `text_core.cpp`, `text_core.h` | The calls, in lean C++ with no Amiga headers (`gt_<Call>(const struct gt_env *, the fd's arguments)`). The core keeps nothing in globals. It reaches the font list, Forbid/Permit, memory, tag cloning and drawing through `struct gt_env`. Text with a one-colour font drawn left to right goes to library/core's Text (`ogfx_rp_Text`). The core draws everything else itself, pixel by pixel: colour fonts, fonts drawn right to left, and RastPorts the core's Text does not take. It follows the same measured rules. |
| `text_types.h` | The NDK 3.2 structures text touches (TextFont, TextFontExtension, ColorTextFont, TextAttr, TTextAttr, TextExtent, RastPort, Node, List, Message, TagItem), with their published names, checked by static_asserts on the 68k. |
| `text_lvo.c` | The register entries `gfx_lvo_<Call>`, `gfx_init_text` and `gfx_exit_text`, and `gfx_hook_set_font` (InitRastPort's SetFont). Built as C by the Amiga compiler with the NDK and `-I library/graphics -I library/core`. It makes `gt_env` from the library's `gfx_env` and keeps it, with the ROM fonts, in `gfx_private_of(GfxBase)->area[GFX_AREA_TEXT]`. One-colour text drawn left to right goes to library/core's Text, with the layer locked as LockLayerRom does. The rest of the text, ClearEOL and ClearScreen go through the face's `gfx_WritePixel` and `gfx_RectFill` (`library/graphics/gfx_face.h`), which take every RastPort, super bitmap layers included. |
| `topaz8.txt`, `mkfont.py`, `topaz_data.cpp`, `topaz_data.h` | The ROM fonts. `topaz8.txt` is OpenGfx's own drawing of an 8 x 8 topaz (baseline row 6), done by hand and not taken from ROM bytes. `mkfont.py` composes the Latin-1 accented letters from a base letter and an accent. It makes topaz 9 (10 x 9, baseline 7) by drawing columns 3 and 4 and row 3 twice. It writes `topaz_data.cpp`, which is constant data only. Characters 127 to 159 share the default glyph (a box). |
| `graphics.sources` | The files the face's build-amiga.sh builds into graphics.library. |
| `tests/m68k/test_gfx_text.sh` | Runs the core under qemu-m68k on the 68000, 68020 and 68040 at -O0 and -O2. It checks against `test_gfx_text_model.py`, an independent python3 model made at test time (details below). |
| `tests/TextABITest.c` | Every call side by side with OS 3.2's graphics.library (on the Amiga, against `opengfx.library`), plus a `PROBE` mode that prints OS 3.2's answer for each "ABI test" corner below. |

The model generates random fonts: one-colour, colour, proportional, kerned, right to left, and with images that overhang their advance. It also generates random strings, AlgoStyles, TxSpacings, draw modes, masks and pens. The test checks:

- Text's pixels, TextLength, TextExtent, TextFit (forward and back, with and without a constraining extent), FontExtent, WeighTAMatch and OpenFont's choice, all against the model.
- One-colour text drawn by this core, pixel for pixel against library/core's own Text on a planar bitmap, and the core's TextLength and TextExtent against the model.
- topaz_data.cpp, which must be exactly what mkfont.py makes from topaz8.txt, and every glyph bit, which must match the drawing.
- The font list, extensions, Forbid/Permit pairing and the memory being returned.
- The register entries over a GfxBase and gfx_env like the face's, linked with the face's `gfx_face.cpp`. This includes text and ClearEOL in a super bitmap layer, which must match the same text on a bare bitmap pixel for pixel and fill to the super bitmap's edge.
- With nm, that `text_core.o` and `topaz_data.o` have no .data or .bss, call nothing but libgcc and have no C++ runtime.

Results: m68k-linux-gnu-g++ / gcc 13 with qemu-m68k, all six builds pass (8467 checks each, register entries and the face included). The Amiga build (GCC 16.2.0-open2, os32-gcc16 stove) and TextABITest have not been run here.

## Edge cases

The "Where" column says whether a case is documented in the autodocs, was measured on OS 3.2 (GfxCoreABITest TEXTPROBE, graphics 47.10), or is an ABI test: unsettled, with a TextABITest `PROBE` line that records OS 3.2's answer. "Ours" is what this code does.

| Case | Where | Ours |
| --- | --- | --- |
| Character outside tf_LoChar..tf_HiChar | measured | The glyph after tf_HiChar's (the default glyph) |
| Count of Text, TextLength, TextExtent and TextFit | measured for Text; ABI test for TextFit (`TextFit 0x10002`) | Taken as a UWORD |
| Bold | measured | The images are ORed again tf_BoldSmear pixels right, and the box gains tf_BoldSmear on the right |
| Italic | measured | Row r moves right by floor((tf_Baseline − r) / 2). The box grows by tf_Baseline / 2 on the right and by (tf_YSize − tf_Baseline + 1) / 2 on the left |
| Underline | measured | Row tf_Baseline + 1, when the font has it, across the box, leaving a pixel clear on each side of image pixels |
| JAM2, COMPLEMENT, INVERSVID with one-colour text | measured | JAM2 fills the box. COMPLEMENT\|JAM2 inverts the whole box. INVERSVID inverts the template |
| TextExtent of no characters | measured | Height 0, extent 0,0 to −1,−1 |
| Colour font (FSF_COLORFONT) | ABI test (colour font PROBE) | Font plane i goes to the bitmap plane of the i-th bit of ctf_PlanePick. The other planes take ctf_PlaneOnOff. A pen equal to ctf_FgColor becomes FgPen under CTF_MAPCOLOR. Later images win. Pixels with no plane bit set are background (left alone, or BgPen with JAM2). The underline is FgPen. COMPLEMENT XORs the pixel's pen |
| Colour font with INVERSVID | ABI test | INVERSVID is ignored |
| Font drawn right to left (FPF_REVPATH) | ABI test (REVPATH PROBE) | The string lands to the left of the pen, last character leftmost. cp_x moves back by the width. TextLength stays positive. TextExtent's box is shifted left by the width |
| Text, TextLength or TextExtent with no font | not documented (OS 3.2 reads through the pointer) | Nothing is drawn, the answer is 0, and the extent is empty |
| SetFont with a null font | autodoc: "discouraged" | Answers 0 and changes nothing |
| SetFont and AlgoStyle or TxSpacing | ABI test (`SetFont`) | Both are kept. TxHeight, TxWidth and TxBaseline come from the font. Answers 1 |
| AskSoftStyle | ABI test (`AskSoftStyle tf_Style n`) | ~tf_Style & (UNDERLINED \| BOLD \| ITALIC). FSF_EXTENDED is never soft. No font answers 0 |
| SetSoftStyle | documented; ABI test for the answer | AlgoStyle takes the bits of style that are in both enable and AskSoftStyle. Answers AlgoStyle \| tf_Style |
| AskFont | ABI test | The name and tf_Flags come from the font, YSize from the RastPort's TxHeight, and the style is tf_Style \| AlgoStyle without FSF_TAGGED |
| OpenFont for a size not in the list | documented ("best matches"); ABI test (`OpenFont topaz n`) | The font with the highest WeighTAMatch among those with that name. No font with the name answers 0 |
| OpenFont name compare | ABI test (`TOPAZ.FONT`) | Case-insensitive ASCII |
| OpenFont among equal weights | ABI test (`OpenFont of equals`) | The first in the list, which is the newest added |
| WeighTAMatch numbers | ABI test (`WeighTAMatch …`) | Ours: 32767 less 256 a point of size (counted to 100), 128 more when the target is larger, 64/32/16/8 for EXTENDED/ITALIC/BOLD/UNDERLINED, 4 for REVPATH, 2 for PROPORTIONAL, and 1 for TALLDOT/WIDEDOT or a TA_DeviceDPI that differs. Never 0 |
| AddFont | ABI test (`AddFont`) | At the head, ln_Type NT_FONT, FPF_REMOVED cleared. A font already in the list stays once (OS 3.2 would link it twice) |
| RemFont | ABI test (`RemFont …`) | Answers 1 and sets FPF_REMOVED. A font not in the list, or one whose extension has TE0F_NOREMFONT, answers 0 and stays |
| CloseFont below 0 accessors | ABI test (`CloseFont below 0`) | Stays at 0. A null font is allowed |
| ExtendFont | ABI test (`ExtendFont`) | Tags are copied (CloneTagItems). An existing extension is kept and the new tags are ignored. tf_Style is not touched. Out of memory answers 0 |
| A tf_Extension that is not an extension | not documented | It is an extension only when tfe_MatchWord is 0xF00D and tfe_BackPtr is the font (the match word is read through the reply port) |
| StripFont with no extension | documented | Nothing |
| TextFit | documented; ABI test for the corners | Characters are taken until the first that does not fit. The box must be no wider than bitwidth and no taller than bitheight, and inside the constraint when one is given. dir −1 counts back from the pointer. No fit answers 0 with an empty extent |
| FontExtent | ABI test | Covers every glyph (the default one too) and the pen. te_Width is the widest advance (CharSpace + kern). No algorithmic style |
| ClearEOL / ClearScreen pen | autodoc says "DrawMode 2"; ABI test (`ClearEOL mode n`) | BgPen with JAM2, else pen 0. Uses the RastPort's TxHeight and TxBaseline. The right and bottom edges are the super bitmap's in a super bitmap layer, the layer's bounds in another layer, else the bitmap's |
| ROM fonts | ABI test (`font topaz.font n`) | topaz 8 is 8×8, baseline 6. topaz 9 is 10×9, baseline 7. Both: BoldSmear 1, chars 32–255, FPF_ROMFONT\|FPF_DESIGNED, mn_Length 52, accessors 0, topaz 8 first in the list and DefaultFont |
| topaz glyphs 127–159 | ours | The default box glyph |

## Not yet

- One-colour text in a super bitmap layer goes through the pixel-by-pixel path (correct, but slower than the core's Text).
- Chunky (RTG) bitmaps: text draws there only as far as the face's WritePixel and RectFill and the core take them.
- Colour fonts' ctf_ColorFontColors (loading the font's colours) and CT_GREYFONT/CT_ANTIALIAS shading: pens are used as they are.
- Speed on the pixel-by-pixel path (colour and right-to-left text). It walks the whole string for each 128-column piece of each row, so it is quadratic for very long strings.
- A hand-tuned topaz 9: today it is a stretch of our topaz 8.
- TextABITest has not been run on OS 3.2 yet. Every "ABI test" row above waits for its PROBE line.
