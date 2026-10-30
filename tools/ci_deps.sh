#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Fetch the two external build inputs the way the Felucca-family firmwares do:
#   1. JieLi's Linux toolchain (clang 4.0.1 for pi32v2): tarball pinned by SHA-256 (tools/jieli-linux.sha256);
#      SLOOP's own tools/get_toolchain.sh layout (~/.jieli/toolchain -> jieli-linux-toolchains-*).
#   2. Three files of JieLi's AC79 SDK (Apache-2.0), verified against the hashes in sloop/tools/build.py.
#      First from JieLi's pinned Gitee commit, then from the ChoralRoot GitHub mirror of the same files.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
jl="${JIELI_DIR:-$HOME/.jieli}"
sdk="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
pin="$(tr -d ' \n\r\t' < "$here/jieli-linux.sha256")"

mkdir -p "$jl"
tar="$jl/jieli-linux-toolchain.tar.xz"
if [ ! -s "$tar" ]; then
  echo "deps: downloading the JieLi Linux toolchain"
  curl -fL --retry 3 -o "$tar" https://pkgman.jieliapp.com/s/linux-toolchain
fi
sum="$(sha256sum "$tar" | cut -d' ' -f1)"
[ "$sum" = "$pin" ] || { echo "deps: toolchain sha256 $sum != pinned $pin" >&2; exit 4; }
if [ ! -e "$jl/toolchain/pi32v2/bin/clang" ]; then
  tar -xJf "$tar" -C "$jl"
  tc="$(ls -d "$jl"/jieli-linux-toolchains-* | sort | tail -1)"
  ln -sfn "$(basename "$tc")" "$jl/toolchain"
fi
ls -l "$jl/toolchain/pi32v2/bin/clang"
"$jl/toolchain/pi32v2/bin/clang" --version | head -1

mkdir -p "$sdk/cpu/wl82/tools/cfg"
declare -A want=( [uboot.boot]=4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4
                  [cfg_tool.bin]=276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b
                  [cfg/eq_cfg_hw.bin]=41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d )
gitee="https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK/raw/AC79NN_SDK_V1.2.1_2023-12-13/cpu/wl82/tools"
mirror="https://raw.githubusercontent.com/Quixotic7/ChoralRootFM1/d09c687d8ee1850fc1360c080aca41763be998ca/tools/sdk"
for f in "${!want[@]}"; do
  dst="$sdk/cpu/wl82/tools/$f"
  if [ -s "$dst" ] && [ "$(sha256sum "$dst" | cut -d' ' -f1)" = "${want[$f]}" ]; then continue; fi
  ok=0
  for src in "$gitee/$f" "$mirror/$(basename "$f")" "$mirror/$f"; do
    if curl -fsSL --retry 2 -o "$dst.tmp" "$src" && [ "$(sha256sum "$dst.tmp" | cut -d' ' -f1)" = "${want[$f]}" ]; then
      mv "$dst.tmp" "$dst"; ok=1; echo "deps: $f from $src"; break
    fi
    rm -f "$dst.tmp"
  done
  [ "$ok" = 1 ] || { echo "deps: could not fetch a matching $f" >&2; exit 5; }
done
echo "JIELI_TOOLCHAIN=$jl/toolchain" >> "${GITHUB_ENV:-/dev/stdout}"
echo "AC79_SDK=$sdk" >> "${GITHUB_ENV:-/dev/stdout}"
