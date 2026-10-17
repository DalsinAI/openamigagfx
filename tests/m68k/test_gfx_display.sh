#!/bin/sh
# OpenGfx graphics.library's display (library/display) under qemu-m68k:
# the lean C++ core built with an m68k cross GCC for the 68040
# at -O0 and -O2, each run on that CPU by test_gfx_display.cpp on a
# simulated machine (exec's memory, the custom chips' registers, a copper
# running the lists, an Agnus/Denise/Lisa showing the bitplanes).
# test_gfx_display_model.py, written apart from the library, makes random
# scenes (OCS, ECS, AGA; PAL, NTSC; Views laced or not, with a ViewExtra's
# monitor or not; one to three ViewPorts of lores, hires, interlace, EHB,
# HAM6, HAM8, dual playfield, scrolled, clipped by the monitor or a
# DisplayClip, with user copper lists, palettes of 32 to 256 colours, PF2OF
# and BPLAM) and says what each ViewPort must show from graphics.library's
# documented View and ViewPort rules, with no copper: every ViewPort's
# pixels after MakeVPort, MrgCop and LoadView, after ScrollVPort, and after
# ChangeVPBitMap, in each field. The harness adds the database, monitors,
# VideoControl, sprites, the beam, WaitTOF, running out of memory and
# memory given back. Then the library checks: the register entries
# (check_regs.py), display_lvo.c compiled, and nm: the core calls nothing
# but libgcc and its own files, with no .data, .bss or C++ runtime.
#   sh tests/m68k/test_gfx_display.sh           (SEED=n SCENES=n to vary)
set -eu
here=$(cd "$(dirname "$0")" && pwd)
top=$here/../..
d=$top/library/display
g=$top/library/graphics
cc=${M68K_CC:-m68k-linux-gnu-gcc}
cxx=${M68K_CXX:-m68k-linux-gnu-g++}
nm=${M68K_NM:-m68k-linux-gnu-nm}
qemu=${QEMU_M68K:-qemu-m68k}
seed=${SEED:-20261013}
scenes=${SCENES:-60}
command -v "$cxx" >/dev/null || { echo "skipped: no $cxx"; exit 0; }
command -v python3 >/dev/null || { echo "skipped: no python3"; exit 0; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
fail=0

python3 "$here/test_gfx_display_model.py" "$seed" "$scenes" "$tmp/scenes.h" "$tmp/expected"

LEAN="-std=gnu++23 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-asynchronous-unwind-tables
  -fno-unwind-tables -nostdlib++ -fno-builtin -fno-tree-loop-distribute-patterns -fno-strict-aliasing"
CORE="$d/display_view.cpp $d/display_hw.cpp $d/display_db.cpp"
for cpu in 68040; do
  for opt in -O0 -O2; do
    # shellcheck disable=SC2086
    "$cxx" -m$cpu $opt -static $LEAN -Wall -Wextra -Werror -I"$d" -I"$g" -I"$tmp" -o "$tmp/t" \
      "$here/test_gfx_display.cpp" $CORE
    timeout 120 "$qemu" -cpu m$cpu "$tmp/t" | head -c 8000000 > "$tmp/out" || true
    last=$(tail -n 1 "$tmp/out")
    grep -v '^FAIL\|^  \|checks, ' "$tmp/out" > "$tmp/got" || true
    if ! cmp -s "$tmp/got" "$tmp/expected"; then
      echo "FAIL $opt -m$cpu: the display differs from the model:"
      diff "$tmp/got" "$tmp/expected" | head -n 12
      fail=1
    elif grep -q '^FAIL' "$tmp/out" || ! echo "$last" | grep -q ', 0 failed$'; then
      echo "FAIL $opt -m$cpu:"; grep '^FAIL\|^  ' "$tmp/out" | head -n 20; echo "$last"
      fail=1
    else
      echo "ok   $opt -m$cpu: $(grep -c '^scene' "$tmp/got") ViewPort pictures as the model's, $last"
    fi
  done
done

# ---- the library: register entries, the shell's compile, nm ----------------
python3 "$g/check_regs.py" "$g/graphics.lvo" "$g/OWNERS.md" "$d/display_lvo.c" > "$tmp/regs" || { cat "$tmp/regs"; fail=1; }
grep -q 'registers match' "$tmp/regs" || { echo "FAIL check_regs"; cat "$tmp/regs"; fail=1; }
for name in $(grep -E '\| gfx-display \|' "$g/OWNERS.md" | sed -n 's/.*`gfx_lvo_\([A-Za-z0-9_]*\)`.*/\1/p'); do
  grep -q "gfx_lvo_$name(" "$d/display_lvo.c" || { echo "FAIL gfx_lvo_$name is the display's and missing"; fail=1; }
done
"$cc" -std=gnu99 -m68040 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -I"$d" -I"$g" -c -o "$tmp/lvo.o" "$d/display_lvo.c"
for f in $CORE; do
  # shellcheck disable=SC2086
  "$cxx" -m68040 -O2 $LEAN -ffreestanding -Wall -Wextra -Werror -I"$d" -I"$g" -c -o "$tmp/$(basename "$f").o" "$f"
done
m68k-linux-gnu-ld -r -o "$tmp/core.o" "$tmp"/display_*.cpp.o
und=$("$nm" -u "$tmp/core.o" | awk '{print $2}' | grep -v -E '^__(mul|div|udiv|mod|umod)si3$' || true)
[ -z "$und" ] && echo "ok   nm: the core calls only libgcc" || { echo "FAIL the core calls: $und"; fail=1; }
data=$("$nm" "$tmp/core.o" "$tmp/lvo.o" | awk '$2 ~ /^[dDbBcCgGsS]$/' || true)
[ -z "$data" ] && echo "ok   nm: no .data or .bss" || { echo "FAIL data or bss: $data"; fail=1; }
bad=$("$nm" "$tmp/core.o" | grep -E '__cxa_|__gxx_personality|_ZTI|_ZTS|_GLOBAL__sub_I_|_ZGV|_Znw|_Zna|_Zdl|_Zda' || true)
[ -z "$bad" ] && echo "ok   nm: no C++ runtime" || { echo "FAIL C++ runtime: $bad"; fail=1; }
sed "s/; not yet:.*//" "$tmp/regs"
exit $fail
