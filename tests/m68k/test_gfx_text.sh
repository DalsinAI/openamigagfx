#!/bin/sh
# OpenGfx text (library/text): the lean C++ core, built with an m68k cross
# GCC for the 68040 at -O0 and -O2, each run on that CPU
# under qemu-m68k against a python3 model made at test time
# (test_gfx_text_model.py: random fonts, colour, proportional, kerned and
# right-to-left, strings, styles and draw modes; Text's pixels, TextLength,
# TextExtent, TextFit, FontExtent, WeighTAMatch, OpenFont), with one-colour
# text also checked pixel for pixel against library/core's Text, and nm
# checks that the core has no .data or .bss, calls nothing but libgcc and
# has no C++ runtime. With library/graphics/gfx_env.h (gfx-face's; or
# GFX_ENV_DIR=dir) the register entries (text_lvo.c) are run too.
#   sh tests/m68k/test_gfx_text.sh [SEED]
set -eu
export PYTHONDONTWRITEBYTECODE=1
here=$(cd "$(dirname "$0")" && pwd)
root=$here/../..
text=$root/library/text
core=$root/library/core
cc=${M68K_CC:-m68k-linux-gnu-gcc}
cxx=${M68K_CXX:-m68k-linux-gnu-g++}
nm=${M68K_NM:-m68k-linux-gnu-nm}
size=${M68K_SIZE:-m68k-linux-gnu-size}
qemu=${QEMU_M68K:-qemu-m68k}
seed=${1:-${SEED:-7}}
command -v "$cxx" >/dev/null || { echo "skipped: no $cxx"; exit 0; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
fail=0

python3 "$here/test_gfx_text_model.py" "$seed" > "$tmp/cases.h"
# the font data must be what mkfont.py makes from topaz8.txt
python3 "$text/mkfont.py" > "$tmp/topaz_data.cpp"
cmp -s "$tmp/topaz_data.cpp" "$text/topaz_data.cpp" || { echo "FAIL topaz_data.cpp is not mkfont.py's"; fail=1; }

genv=${GFX_ENV_DIR:-$root/library/graphics}
lvo=
if [ -f "$genv/gfx_env.h" ] && [ -f "$genv/gfx_face.cpp" ]; then lvo="-DWITH_LVO -I$genv"; else echo "note: no library/graphics (gfx_env.h, gfx_face.cpp), register entries not run"; fi

warn="-Wall -Wextra -Werror"
inc="-I$here/text-ndk -I$here/../host-ndk -I$core -I$text -I$tmp -include $here/text-ndk/gfxdefs.h"
cxxflags="-std=gnu++23 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-asynchronous-unwind-tables
  -fno-unwind-tables -nostdlib++ -ffreestanding -fno-builtin $warn"
for cpu in -m68040; do
  for opt in -O0 -O2; do
    o=$tmp/o$cpu$opt; mkdir -p "$o"
    # shellcheck disable=SC2086
    "$cxx" $cpu $opt $cxxflags -c "$text/text_core.cpp" -o "$o/text_core.o"
    # shellcheck disable=SC2086
    "$cxx" $cpu $opt $cxxflags -c "$text/topaz_data.cpp" -o "$o/topaz_data.o"
    for f in text_core topaz_data; do
      bad=$("$size" -A "$o/$f.o" | awk '($1 ~ /^\.(data|bss)/ && $2 != 0) {print $1}')
      [ -z "$bad" ] || { echo "FAIL $opt $cpu: $f.o has $bad"; fail=1; }
      und=$("$nm" -u "$o/$f.o" | awk '{print $2}' |
            grep -vxE '__(mul|div|udiv|mod|umod)si3|gt_topaz(8|9|_name)' || true)
      [ -z "$und" ] || { echo "FAIL $opt $cpu: $f.o calls $und"; fail=1; }
      cxxrt=$("$nm" "$o/$f.o" | grep -E '__cxa_|__gxx_personality|_ZTI|_ZTS|_GLOBAL__sub_I_|_ZGV|_Znw|_Zna|_Zdl|_Zda' || true)
      [ -z "$cxxrt" ] || { echo "FAIL $opt $cpu: $f.o has C++ runtime: $cxxrt"; fail=1; }
    done
    src="$here/test_gfx_text.c $core/gfx_rp.c $core/gfx_core.c"
    if [ -n "$lvo" ]; then
      # the face's drawing, which text_lvo.c calls (gfx_face.h)
      # shellcheck disable=SC2086
      "$cxx" $cpu $opt $cxxflags -Wno-unused-parameter -I"$genv" -I"$core" -c "$genv/gfx_face.cpp" -o "$o/face.o"
      src="$src $text/text_lvo.c $core/gfx_region.c $o/face.o"
    fi
    # shellcheck disable=SC2086
    "$cc" $cpu $opt -static $warn -Wno-unused-parameter $lvo $inc -o "$o/t" $src "$o/text_core.o" "$o/topaz_data.o"
    q=m${cpu#-m}
    if out=$(timeout 120 "$qemu" -cpu $q "$o/t" 2>&1 | head -c 8000000); then
      case $out in *" 0 failed"*) echo "ok   $opt $cpu: $out" ;; *) echo "FAIL $opt $cpu:"; echo "$out"; fail=1 ;; esac
    else echo "FAIL $opt $cpu:"; echo "$out"; fail=1; fi
  done
done
exit $fail
