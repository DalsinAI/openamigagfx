#!/bin/sh
# OpenLayers with the os32-gcc16 stove: checks layers.lvo against the
# NDK 3.2 layers_lib.fd, then builds
#   layers.library       the replacement (47.100), a resident module
#   openlayers.library   the same code under its own name, for the ABI test
#   LayersABITest        runs the same calls through both and lists every difference
# The amigachrome-guest checkout with libraries/common/check_fd.py:
#   GUEST=~/team2-labs/guest-main sh library/layers/build-amiga.sh
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STOVE=${STOVE:-"$HOME/AmigaChrome/stoves/os32-gcc16"}
P="$STOVE/prefix"
CC=${CC:-"$P/bin/m68k-amigaos-gcc"}
OUT=${OUT:-"$HERE/build-amiga"}
# the ROM module's optimisation: -Os (size) by default; OPT=-O2 for speed
OPT=${OPT:--Os}
GUEST=${GUEST:?set GUEST to an amigachrome-guest checkout}
mkdir -p "$OUT/libs" "$OUT/tests"

# NDK 3.2's fd, never fd13
FD=${FD:-"$P/m68k-amigaos/ndk/lib/fd/layers_lib.fd"}
[ -f "$FD" ] || { echo "no $FD: set FD=" >&2; exit 1; }
python3 "$GUEST/libraries/common/check_fd.py" "$FD" "$HERE/layers.lvo"

LIBFLAGS="-m68040 $OPT -fno-strict-aliasing -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-toplevel-reorder
  -fno-lto -fno-builtin -fno-tree-loop-distribute-patterns -Wall -Wextra -Werror
  -Wno-unused-parameter -nostartfiles -nostdlib"
for name in layers openlayers; do
  # shellcheck disable=SC2086
  "$CC" $LIBFLAGS -DLIBNAME="\"$name.library\"" -I"$HERE" -o "$OUT/libs/$name.library" \
    "$HERE/openlayers_lib.c" "$HERE/layers_lib.c" "$HERE/layers_core.c" -lgcc
  echo "$OUT/libs/$name.library ($(wc -c < "$OUT/libs/$name.library") bytes)"
  und=$("$P/bin/m68k-amigaos-nm" -u "$OUT/libs/$name.library" || true)
  [ -z "$und" ] || { echo "undefined in $name.library: $und" >&2; exit 1; }
done

if [ -f "$HERE/../../tests/LayersABITest.c" ]; then
  "$CC" -m68040 -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -Wno-pointer-sign -Wno-format -noixemul \
    -o "$OUT/tests/LayersABITest" "$HERE/../../tests/LayersABITest.c"
  echo "$OUT/tests/LayersABITest ($(wc -c < "$OUT/tests/LayersABITest") bytes)"
fi
sha256sum "$OUT"/libs/* "$OUT"/tests/* 2>/dev/null || true
