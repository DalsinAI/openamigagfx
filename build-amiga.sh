#!/bin/sh
# OpenGfx with the os32-gcc16 stove: builds GfxCoreABITest, OpenGfx's core
# and RastPort calls side by side with OS 3.2's graphics.library, and
# RegionABITest, its regions likewise.
#   sh build-amiga.sh   (out: build-amiga/GfxCoreABITest, RegionABITest)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STOVE=${STOVE:-"$HOME/AmigaChrome/stoves/os32-gcc16"}
CC=${CC:-"$STOVE/prefix/bin/m68k-amigaos-gcc"}
OUT=${OUT:-"$HERE/build-amiga"}
mkdir -p "$OUT"
# A 68040 build (all builds are 68040 only), soft float: the specs file
# links libnix's soft libm020, not the FPU libm881.
"$CC" -m68040 -specs="$HERE/build/m68k-soft-m68040.specs" -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -Wno-pointer-sign -noixemul \
  -I"$HERE/library/core" -o "$OUT/GfxCoreABITest" \
  "$HERE/tests/GfxCoreABITest.c" "$HERE/library/core/gfx_rp.c" "$HERE/library/core/gfx_core.c"
echo "$OUT/GfxCoreABITest ($(wc -c < "$OUT/GfxCoreABITest") bytes)"
"$CC" -m68040 -specs="$HERE/build/m68k-soft-m68040.specs" -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -Wno-pointer-sign -noixemul \
  -I"$HERE/library/core" -o "$OUT/RegionABITest" \
  "$HERE/tests/RegionABITest.c" "$HERE/library/core/gfx_region.c"
echo "$OUT/RegionABITest ($(wc -c < "$OUT/RegionABITest") bytes)"
