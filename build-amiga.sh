#!/bin/sh
# OpenGfx with the os32-gcc16 stove: builds GfxCoreABITest, OpenGfx's core
# and RastPort calls side by side with OS 3.2's graphics.library.
#   sh build-amiga.sh   (out: build-amiga/GfxCoreABITest)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STOVE=${STOVE:-"$HOME/AmigaChrome/stoves/os32-gcc16"}
CC=${CC:-"$STOVE/prefix/bin/m68k-amigaos-gcc"}
OUT=${OUT:-"$HERE/build-amiga"}
mkdir -p "$OUT"
# A 68000 build: the core must run on every Amiga.
"$CC" -m68000 -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -Wno-pointer-sign -noixemul \
  -I"$HERE/library/core" -o "$OUT/GfxCoreABITest" \
  "$HERE/tests/GfxCoreABITest.c" "$HERE/library/core/gfx_rp.c" "$HERE/library/core/gfx_core.c"
echo "$OUT/GfxCoreABITest ($(wc -c < "$OUT/GfxCoreABITest") bytes)"
