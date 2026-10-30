#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Fetch JieLi's Linux toolchain (clang 4.0.1 for pi32v2, a plain tarball,
# no installer) the way the Felucca-family FM-1 firmwares do in their
# Ubuntu CI, verify it, and build a shim bin/ with the tool names the
# vendor Build.ps1 expects. Prints FM1_PI32_TOOLCHAIN=<shim dir>.
#
# Verification (both must hold):
#   1. tarball SHA-256 == tools/ci/jieli-linux.sha256 (our pin; JieLi's URL
#      is a moving one, so this is what stops a silently changed archive);
#   2. the package is jieli-linux-toolchains-* and `clang --version` says
#      4.0.1 (the identity check Felucca's CI applies).
# The compiler's own SHA-256 is printed for the provenance record.
#
# Tarball layout (jieli-linux-toolchains-20260730.1):
#   pi32v2/bin/clang  pi32v2/bin/ld  common/bin/objcopy  common/bin/objdump  common/bin/nm (if present)
# Shim: bin/clang -> pi32v2/bin/clang, bin/pi32v2-ld -> pi32v2/bin/ld,
#       bin/llvm-objcopy -> common/bin/objcopy, bin/llvm-nm -> common/bin/nm or pi32v2/bin/nm
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
dest="${1:-$HOME/.jieli}"
url="${JIELI_LINUX_TOOLCHAIN_URL:-https://pkgman.jieliapp.com/s/linux-toolchain}"
pin="$(tr -d ' \n\r\t' < "$here/jieli-linux.sha256" 2>/dev/null || true)"
mkdir -p "$dest"
tar="$dest/jieli-linux-toolchain.tar.xz"
if [ ! -s "$tar" ]; then
  echo "toolchain: downloading $url"
  curl -fL --retry 3 -o "$tar" "$url"
fi
sum="$(sha256sum "$tar" | cut -d' ' -f1)"
echo "toolchain: tarball sha256 $sum"
if [ -z "$pin" ]; then
  echo "toolchain: no pin in tools/ci/jieli-linux.sha256. Record this hash there and re-run:"
  echo "$sum"
  exit 3
fi
if [ "$sum" != "$pin" ]; then
  echo "toolchain: SHA-256 mismatch (pinned $pin). Refusing an unreviewed toolchain." >&2
  exit 4
fi
if [ ! -d "$dest/extracted" ]; then
  mkdir -p "$dest/extracted"
  tar -xJf "$tar" -C "$dest/extracted"
fi
tc="$(find "$dest/extracted" -maxdepth 2 -type d -name 'jieli-linux-toolchains-*' | sort | tail -1)"
[ -n "$tc" ] || { echo "toolchain: no jieli-linux-toolchains-* directory in the tarball:" >&2; find "$dest/extracted" -maxdepth 3 | head -40 >&2; exit 5; }
echo "toolchain: package $(basename "$tc")"
clang="$(find "$tc" -type f -path '*/bin/clang' | head -1)"
[ -n "$clang" ] || { echo "toolchain: bin/clang not found under $tc" >&2; find "$tc" -maxdepth 3 | head -60 >&2; exit 5; }
chmod +x "$clang" 2>/dev/null || true
ver="$("$clang" --version 2>&1 | head -1)"
echo "toolchain: $ver"
case "$ver" in *"clang version 4.0.1"*) ;; *) echo "toolchain: expected JieLi clang 4.0.1" >&2; exit 6;; esac
echo "toolchain: compiler sha256 $(sha256sum "$clang" | cut -d' ' -f1)"
cbin="$(dirname "$clang")"
pick() { # pick <name> <candidates...>: first existing file
  local name="$1"; shift
  for c in "$@"; do if [ -f "$c" ]; then chmod +x "$c" 2>/dev/null || true; echo "$c"; return; fi; done
  echo "toolchain: no tool for $name (tried: $*)" >&2; echo "toolchain: contents:" >&2; find "$tc" -type f -path '*/bin/*' | sed "s|$tc/||" | sort >&2; exit 7
}
ld="$(pick ld "$cbin/ld" "$cbin/pi32v2-ld" "$cbin/ld.lld" "$tc/common/bin/ld")"
objcopy="$(pick objcopy "$tc/common/bin/objcopy" "$cbin/objcopy" "$cbin/llvm-objcopy" "$tc/common/bin/llvm-objcopy")"
nm="$(pick nm "$tc/common/bin/nm" "$cbin/nm" "$cbin/llvm-nm" "$tc/common/bin/llvm-nm")"
shim="$dest/shim"
rm -rf "$shim"; mkdir -p "$shim/bin"
ln -s "$clang" "$shim/bin/clang"
ln -s "$ld" "$shim/bin/pi32v2-ld"
ln -s "$objcopy" "$shim/bin/llvm-objcopy"
# Build.ps1 calls `llvm-nm --print-size`; GNU/JieLi binutils nm spells that -S and prints the same lines.
cat > "$shim/bin/llvm-nm" <<EOF2
#!/usr/bin/env bash
args=(); for a in "\$@"; do [ "\$a" = --print-size ] && a=-S; args+=("\$a"); done
exec "$nm" "\${args[@]}"
EOF2
chmod +x "$shim/bin/llvm-nm"
# Build.ps1 also resolves the toolchain's lib/ and include/ next to bin/ through clang itself; nothing to shim.
ls -l "$shim/bin"
echo "FM1_PI32_TOOLCHAIN=$shim" >> "${GITHUB_ENV:-/dev/stdout}"
