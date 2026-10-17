# archive

Kept, not built. All builds are 68040 only since 17 October 2026; these
files only made sense for the 68000 builds.

- `library/graphics/gfx_arith.c`, `library/layers/layers_arith.c`: 32-bit
  multiply and divide for the 68000 library builds (the 68000 libgcc.a had
  none under -nostdlib). A 68040 build does these in single instructions.
- `tests/m68k/rt68000.c`, `tests/m68k/text_rt68000.c`: the bare runtime the
  qemu-m68k tests linked for their 68000 builds.
- `tests/m68k/*_rt68000.c`: the same kind of runtime, written out at test
  time by test_gfx_core.sh, test_gfx_region.sh and test_layers_core.sh.
