#!/bin/sh
# OpenLayers' library code (library/layers/layers_lib.c) with the core,
# built against tests/host-ndk and a pixel model of exec and
# graphics.library (tests/m68k/test_layers_lib.c), for the 68000, 68020
# and 68040 at -O0 and -O2 under qemu-m68k, where the structures have the
# Amiga's layout. With HOST_CC=cc it runs on the host first, under ASan
# and UBSan.
#   sh tests/m68k/test_layers_lib.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
lib=$here/../../library/layers
ndk=$here/../host-ndk
cc=${M68K_CC:-m68k-linux-gnu-gcc}
qemu=${QEMU_M68K:-qemu-m68k}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
fail=0
src="$here/test_layers_lib.c $lib/layers_lib.c $lib/layers_core.c"
warn="-Wall -Wextra -Werror -Wno-unused-parameter"

if [ -n "${HOST_CC:-}" ]; then
  "$HOST_CC" -O1 -g $warn -fsanitize=address,undefined -fno-sanitize-recover=undefined -DROUNDS=60 \
    -I"$ndk" -I"$lib" -o "$tmp/h" $src
  if out=$("$tmp/h"); then echo "ok   host: $out"; else echo "FAIL host:"; echo "$out"; fail=1; fi
fi
command -v "$cc" >/dev/null || { echo "skipped: no $cc"; exit $fail; }
for cpu in -m68000 -m68020 -m68040; do
  for opt in -O0 -O2; do
    case $cpu in
      -m68000) rt="-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -nostdlib -ffreestanding -fno-builtin $here/rt68000.c" ;;
      *) rt= ;;
    esac
    "$cc" $cpu $opt -static $warn -DROUNDS=40 -I"$ndk" -I"$lib" -o "$tmp/t" $src $rt
    q=m${cpu#-m}
    if out=$("$qemu" -cpu $q "$tmp/t"); then echo "ok   $opt $cpu: $out"; else echo "FAIL $opt $cpu:"; echo "$out"; fail=1; fi
  done
done
exit $fail
