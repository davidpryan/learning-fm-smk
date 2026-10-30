#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Compile the learn sources the way the DEVICE build sees them (the SDK
# include set, no FM1_HOST_TEST, 32-bit, clang 4-era C) as a syntax and
# frame-size check on any host. Not a substitute for the JieLi build, but
# it catches host-only assumptions before CI spends a Windows runner.
set -euo pipefail
tree="$1"
inc=(-I"$tree/sdk/include" -I"$tree/sdk/include/libc" -I"$tree/app/synth" -I"$tree/sdk/config" -I"$tree/app" -I"$tree/app/board" -I"$tree/app/learn")
board=$(sed -n 's/^board=//p' "$tree/LEARN-BUILD.txt")
defs=(-DFM1_DSP_ABI=3U)
[ "$board" = smk37 ] && defs+=(-DSMK37_MODEL_PRO=1)
fail=0
for f in "$tree"/app/learn/*.c; do
  if ! clang -std=c11 -m32 -fsyntax-only -Wall -Wextra -Werror -Wframe-larger-than=2560 -Wno-unused-parameter -Wno-unused-function \
       -fno-builtin -D__GCC_PI32V2__ "${defs[@]}" "${inc[@]}" "$f" 2>&1 | sed "s|^|$(basename "$f"): |"; then fail=1; fi
done
# Static RAM per learn object (approximate: host sizes, 32-bit).
for f in "$tree"/app/learn/*.c; do
  o=/tmp/learn_sz_$$.o
  clang -std=c11 -m32 -c -Os -fno-builtin -D__GCC_PI32V2__ "${defs[@]}" "${inc[@]}" "$f" -o "$o" 2>/dev/null && size "$o" | tail -1 | awk -v n="$(basename "$f")" '{printf "%-18s text %6d data %5d bss %6d\n", n, $1, $2, $3}'
  rm -f "$o"
done
exit $fail
