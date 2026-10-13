#!/bin/sh
# OpenGfx gels (library/gels/gels_core.cpp, lean C++) for the 68000, 68020
# and 68040 at -O0 and -O2, each run on that CPU under qemu-m68k on a
# simulated machine (tests/m68k/test_gfx_gels.c: Chip and Fast RAM, a
# blitter from the Hardware Reference Manual, its interrupt, the beam,
# tasks), against a model written a pixel at a time in python
# (tests/m68k/test_gfx_gels.py), which writes the script both run.
# Also checks the core: no .data or .bss, no C++ runtime, nothing called
# but libgcc and the env.
#   sh tests/m68k/test_gfx_gels.sh [SEED] [ROUNDS]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
gels=$here/../../library/gels
env=$here/../../library/graphics
[ -f "$env/gfx_env.h" ] || env=$gels/standin
cc=${M68K_CC:-m68k-linux-gnu-gcc}
cxx=${M68K_CXX:-m68k-linux-gnu-g++}
qemu=${QEMU_M68K:-qemu-m68k}
command -v "$cxx" >/dev/null || { echo "skipped: no $cxx"; exit 0; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
fail=0
seed=${1:-1}
rounds=${2:-280}

python3 "$here/test_gfx_gels.py" "$tmp" "$seed" "$rounds"
lean="-std=gnu++23 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-asynchronous-unwind-tables
  -fno-unwind-tables -nostdlib++ -fno-builtin -fno-tree-loop-distribute-patterns -Wall -Wextra -Werror"

for cpu in -m68000 -m68020 -m68040; do
  for opt in -O0 -O2; do
    case $cpu in
      -m68000) rt="-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -nostdlib -ffreestanding -fno-builtin $here/rt68000.c" ;;
      *) rt= ;;
    esac
    # shellcheck disable=SC2086
    "$cxx" $cpu $opt $lean -I"$env" -c -o "$tmp/core.o" "$gels/gels_core.cpp"
    # the core: no data, no bss, no C++ runtime, nothing undefined but libgcc and the env's hook
    bad=$(m68k-linux-gnu-nm "$tmp/core.o" | awk '$2 ~ /^[dDbBcCgGsS]$/ {print}
      $1 == "U" && $2 !~ /^(__mulsi3|__divsi3|__udivsi3|__modsi3|__umodsi3|gfx_hook_vsprites)$/ {print}
      /__cxa_|__gxx_personality|_ZTI|_ZTS|_GLOBAL__sub_I_|_ZGV|_Znw|_Zdl/ {print}')
    if [ -n "$bad" ]; then echo "FAIL $opt $cpu: the core has"; echo "$bad"; fail=1; fi
    # the register entries (gels_lvo.c), here with plain parameters: they
    # build, keep no data, and reach the state through the library base
    "$cc" $cpu $opt -Wall -Wextra -Werror -I"$env" -c -o "$tmp/lvo.o" "$gels/gels_lvo.c"
    bad=$(m68k-linux-gnu-nm "$tmp/lvo.o" | awk '$2 ~ /^[dDbBcCgGsS]$/ {print}')
    if [ -n "$bad" ]; then echo "FAIL $opt $cpu: gels_lvo.c has data"; echo "$bad"; fail=1; fi
    # shellcheck disable=SC2086
    "$cc" $cpu $opt -static -Wall -Wextra -Werror -I"$tmp" -I"$gels" -I"$env" -o "$tmp/t" \
      "$here/test_gfx_gels.c" "$tmp/core.o" "$tmp/lvo.o" $rt
    q=m${cpu#-m}
    if timeout 120 "$qemu" -cpu "$q" "$tmp/t" | head -c 8000000 > "$tmp/out.txt"; then :; fi
    last=$(tail -n 1 "$tmp/out.txt" | cut -c1-200)
    sed '$d' "$tmp/out.txt" > "$tmp/got.txt"
    if [ "${last#end }" != "$last" ] && [ "${last%FAILED}" = "$last" ] && cmp -s "$tmp/got.txt" "$tmp/expected.txt"; then
      echo "ok   $opt $cpu: $(wc -l < "$tmp/got.txt") lines as the model, $last"
    else
      echo "FAIL $opt $cpu: $last"
      diff "$tmp/expected.txt" "$tmp/got.txt" | head -20 || true
      grep '^FAIL' "$tmp/out.txt" | head -10 || true
      fail=1
    fi
  done
done
exit $fail
