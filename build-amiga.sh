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
# OpenAmigaGCC 16.2.0-open3's late-combine pass can store a copy one place late (GCC PR 123853): GfxCoreABITest's
# open_fonts copied its test font's tables as "move.w (a0)+,(a0,d2.l)", each word one place late. Off until
# OpenAmigaGCC open4 (openamigagcc #29), on a compiler that knows the option. Not in library/graphics or
# library/layers: their builds are the ROM's (Team 4's), which W128 found clean.
LATE=""
if "$CC" -fno-late-combine-instructions -S -o /dev/null -x c /dev/null 2>/dev/null; then LATE="-fno-late-combine-instructions"; fi
# A 68040 build (all builds are 68040 only), soft float: the specs file
# links libnix's soft libm020, not the FPU libm881.
"$CC" $LATE -m68040 -specs="$HERE/build/m68k-soft-m68040.specs" -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -Wno-pointer-sign -noixemul \
  -I"$HERE/library/core" -o "$OUT/GfxCoreABITest" \
  "$HERE/tests/GfxCoreABITest.c" "$HERE/library/core/gfx_rp.c" "$HERE/library/core/gfx_core.c"
echo "$OUT/GfxCoreABITest ($(wc -c < "$OUT/GfxCoreABITest") bytes)"
"$CC" $LATE -m68040 -specs="$HERE/build/m68k-soft-m68040.specs" -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -Wno-pointer-sign -noixemul \
  -I"$HERE/library/core" -o "$OUT/RegionABITest" \
  "$HERE/tests/RegionABITest.c" "$HERE/library/core/gfx_region.c"
echo "$OUT/RegionABITest ($(wc -c < "$OUT/RegionABITest") bytes)"
