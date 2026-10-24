#!/bin/sh
# DisplayABITest with the os32-gcc16 stove (GCC 16.2.0-open2). The library
# itself (graphics.library and opengfx.library, display included through
# graphics.sources) is library/graphics/build-amiga.sh's.
#   sh library/display/build-amiga.sh
# then on OS 3.2 with DEVS:Monitors empty and opengfx.library in LIBS: (or
# LIB=dir):  DisplayABITest PROBE   records OS 3.2's answers;
#            DisplayABITest         compares opengfx.library's with them.
# It also builds tests/ScreenDepthProbe, which runs on any ROM (its header
# says what it prints).
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOP=$(CDPATH= cd -- "$HERE/../.." && pwd)
STOVE=${STOVE:-"$HOME/AmigaChrome/stoves/os32-gcc16"}
CC=${CC:-"$STOVE/prefix/bin/m68k-amigaos-gcc"}
OUT=${OUT:-"$HERE/build-amiga"}
mkdir -p "$OUT/tests"
"$CC" -m68040 -specs="$TOP/build/m68k-soft-m68040.specs" -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -Wno-pointer-sign -Wno-format -noixemul \
  -o "$OUT/tests/DisplayABITest" "$TOP/tests/DisplayABITest.c"
echo "$OUT/tests/DisplayABITest ($(wc -c < "$OUT/tests/DisplayABITest") bytes)"
# ScreenDepthProbe: the screens the running ROM opens by resolution and depth
"$CC" -m68040 -specs="$TOP/build/m68k-soft-m68040.specs" -O2 -Wall -Wextra -Werror -Wno-pointer-sign -Wno-format -noixemul \
  -o "$OUT/tests/ScreenDepthProbe" "$TOP/tests/ScreenDepthProbe.c"
echo "$OUT/tests/ScreenDepthProbe ($(wc -c < "$OUT/tests/ScreenDepthProbe") bytes)"
