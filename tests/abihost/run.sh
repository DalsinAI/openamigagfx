#!/bin/sh
# LayersABITest's PROBE run on the host over OpenLayers (tests/abihost),
# set beside OS 3.2's PROBE lines from an Amiga when a file of them is given.
#   sh tests/abihost/run.sh [os-probe.txt] [ROUNDS=n]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
top=$here/../..
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
cc=${HOST_CC:-cc}
$cc -O1 -g -w -fsanitize=address,undefined -I"$here/inc" -I"$top/tests/host-ndk" -I"$top/library/layers" \
  -include "$here/shim.h" -o "$tmp/abi" "$top/tests/LayersABITest.c" "$here/abihost.c" \
  "$top/library/layers/layers_lib.c" "$top/library/layers/layers_core.c"
os=
case ${1:-} in ''|ROUNDS=*) ;; *) os=$1; shift ;; esac
"$tmp/abi" PROBE "$@" > "$tmp/ours.txt"
if [ -z "$os" ]; then cat "$tmp/ours.txt"; exit 0; fi
tail -n +2 "$os" > "$tmp/os.txt"
tail -n +2 "$tmp/ours.txt" > "$tmp/o.txt"
if diff "$tmp/os.txt" "$tmp/o.txt" > "$tmp/d"; then echo "same as OS 3.2"; else cat "$tmp/d"; exit 5; fi
