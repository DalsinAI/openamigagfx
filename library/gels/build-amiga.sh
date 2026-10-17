#!/bin/sh
# OpenGfx gels with the os32-gcc16 stove (GCC 16.2.0-open2). The gels go
# into graphics.library through library/gels/graphics.sources, built by
# library/graphics/build-amiga.sh; this builds that, then
#   GelsABITest   the gels and blitter calls side by side with OS 3.2's
#   GUEST=~/team2-labs/guest-main sh library/gels/build-amiga.sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOP=$(CDPATH= cd -- "$HERE/../.." && pwd)
STOVE=${STOVE:-"$HOME/AmigaChrome/stoves/os32-gcc16"}
P="$STOVE/prefix"
CC=${CC:-"$P/bin/m68k-amigaos-gcc"}
OUT=${OUT:-"$TOP/library/graphics/build-amiga"}
sh "$TOP/library/graphics/build-amiga.sh"
mkdir -p "$OUT/tests"
"$CC" -m68040 -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -Wno-pointer-sign -Wno-format -noixemul \
  -o "$OUT/tests/GelsABITest" "$TOP/tests/GelsABITest.c"
echo "$OUT/tests/GelsABITest ($(wc -c < "$OUT/tests/GelsABITest") bytes)"
