#!/bin/sh
# OpenGfx and OpenLayers: the 16- and 8-bit arguments.
#  1. narrow_args.txt lists every argument NDK 3.2's graphics_lib.sfd and
#     layers_lib.sfd give as WORD, UWORD, BYTE, UBYTE or BOOL, with its
#     register; check_narrow.py checks that each LVO entry takes it narrow or
#     casts it to that width.
#  2. test_gfx_narrow.c calls a sample of the face's LVOs twice, with clean
#     arguments and with garbage in the upper parts, under qemu-m68k on the
#     68000, 68020 and 68040: the rasters and answers must match.
#   sh tests/m68k/test_gfx_narrow.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
top=$here/../..
g=$top/library/graphics
core=$top/library/core
cc=${M68K_CC:-m68k-linux-gnu-gcc}
cxx=${M68K_CXX:-m68k-linux-gnu-g++}
qemu=${QEMU_M68K:-qemu-m68k}
rounds=${ROUNDS:-24}
fail=0
python3 "$here/check_narrow.py" "$here/narrow_args.txt" "$top/library" || fail=1
command -v "$cc" >/dev/null || { echo "skipped the m68k part: no $cc"; exit $fail; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
cxxflags="-std=gnu++23 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-asynchronous-unwind-tables
  -fno-unwind-tables -nostdlib++ -ffreestanding -fno-builtin -Wall -Wextra -Werror"
warn="-Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function"
inc="-I$g -I$core -I$here/gfx_face_ndk -I$here/../host-ndk"
for cpu in -m68000 -m68020 -m68040; do
  for opt in -O0 -O2; do
    case $cpu in
      -m68000) rt="-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -nostdlib -ffreestanding -fno-builtin $here/rt68000.c" ;;
      *) rt= ;;
    esac
    # shellcheck disable=SC2086
    "$cxx" $cpu $opt $cxxflags -I"$g" -I"$core" -c -o "$tmp/face.o" "$g/gfx_face.cpp"
    # shellcheck disable=SC2086
    "$cc" $cpu $opt -std=gnu11 $warn -I"$g" -c -o "$tmp/lvo.o" "$g/face_lvo.c"
    # shellcheck disable=SC2086
    "$cc" $cpu $opt -static $warn -DROUNDS=$rounds $inc -o "$tmp/t" "$here/test_gfx_narrow.c" \
      "$core/gfx_core.c" "$core/gfx_region.c" "$core/gfx_rp.c" "$tmp/face.o" "$tmp/lvo.o" $rt
    if timeout 120 "$qemu" -cpu m${cpu#-m} "$tmp/t" > "$tmp/out" && grep -q '^END 0$' "$tmp/out"; then
      echo "ok   $opt $cpu: $(grep '^narrow:' "$tmp/out")"
    else
      echo "FAIL $opt $cpu:"; grep ERR "$tmp/out" | head; tail -2 "$tmp/out"; fail=1
    fi
  done
done
[ $fail = 0 ] && echo "test_gfx_narrow: all passed"
exit $fail
