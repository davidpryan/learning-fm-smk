#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Native host build of one assembled tree + the test harness, then run it.
#   tools/host_build.sh build/trees/smk37-s1
set -euo pipefail
tree="$1"
root="$(cd "$(dirname "$0")/.." && pwd)"
[ -f "$tree/app/screen.c" ] || { echo "no tree at $tree (run tools/overlay.py)"; exit 2; }
board=$(sed -n 's/^board=//p' "$tree/LEARN-BUILD.txt")
stage=$(sed -n 's/^stage=//p' "$tree/LEARN-BUILD.txt")
out="$tree/host"; mkdir -p "$out"
CC="${CC:-clang}"
srcs=()
while IFS= read -r f; do srcs+=("$f"); done < <(cd "$tree" && find web/engine web/synth third_party/synth app -name '*.c' | grep -v '^app/board/' | grep -v '^web/engine/Simulator.c$' | sort)
flags=(-std=c11 -O1 -g -DFM1_HOST_TEST -DFM1_OPEN_WEB -Wall -Wextra -Werror
       -Wno-unused-parameter -Wno-unused-function
       -I"$tree/web/engine" -I"$tree/web/synth" -I"$tree/third_party/synth" -I"$tree/sdk/include" -I"$tree/app/synth" -I"$tree/app" -I"$tree/app/learn")
if [ "$board" = smk37 ]; then flags+=(-DLEARN_BOARD_SMK37); else flags+=(-DLEARN_BOARD_FM1); fi
# Learn sources get strict warnings; vendor sources compile as they ship.
objs=()
for f in "${srcs[@]}"; do
  o="$out/$(echo "$f" | tr '/' '_').o"
  case "$f" in
    app/learn/*) "$CC" "${flags[@]}" -c "$tree/$f" -o "$o" ;;
    *) "$CC" "${flags[@]}" -Wno-error -w -c "$tree/$f" -o "$o" ;;
  esac
  objs+=("$o")
done
"$CC" "${flags[@]}" -Wno-error -w -c "$root/tools/host/sim_shim.c" -o "$out/sim_shim.o"
"$CC" "${flags[@]}" -c "$root/tools/host/harness.c" -o "$out/harness.o"
"$CC" -o "$out/harness" "${objs[@]}" "$out/sim_shim.o" "$out/harness.o" -lm
echo "host: built $board stage $stage -> $out/harness"
"$out/harness"
