#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Fetch JieLi's Linux toolchain (clang 4.0.1 for pi32v2, a plain tarball,
# no installer) and verify it against the pinned SHA-256 in
# tools/ci/jieli-linux.sha256. Prints FM1_PI32_TOOLCHAIN=<dir>.
#
# First run: the pin file is empty, so this script prints the tarball's
# hash and exits 3. Commit that hash to tools/ci/jieli-linux.sha256 after
# checking it against another trusted download (for example the hash the
# Felucca project's CI logs show for the same URL). From then on a changed
# tarball stops the build instead of compiling with an unreviewed compiler.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
dest="${1:-$HOME/.jieli}"
url="${JIELI_LINUX_TOOLCHAIN_URL:-https://pkgman.jieliapp.com/s/linux-toolchain}"
pin="$(tr -d ' \n\r' < "$here/jieli-linux.sha256" 2>/dev/null || true)"
mkdir -p "$dest"
tar="$dest/jieli-linux-toolchain.tar.xz"
if [ ! -f "$tar" ]; then
  echo "toolchain: downloading $url"
  curl -fL --retry 3 -o "$tar" "$url"
fi
sum="$(sha256sum "$tar" | cut -d' ' -f1)"
echo "toolchain: tarball sha256 $sum"
if [ -z "$pin" ]; then
  echo "toolchain: no pin recorded. Put this hash in tools/ci/jieli-linux.sha256 and re-run:"
  echo "$sum"
  exit 3
fi
if [ "$sum" != "$pin" ]; then
  echo "toolchain: SHA-256 mismatch (pinned $pin). Refusing to build with an unreviewed toolchain." >&2
  exit 4
fi
tar -xJf "$tar" -C "$dest"
tc="$(ls -d "$dest"/jieli-linux-toolchains-* | sort | tail -1)"
# The pi32v2 tools live in a subdirectory; find bin/clang under the extracted tree.
bin="$(find "$tc" -type f -name clang -path '*/bin/*' | head -1)"
[ -n "$bin" ] || { echo "toolchain: bin/clang not found under $tc" >&2; ls -R "$tc" | head -40; exit 5; }
root="$(dirname "$(dirname "$bin")")"
for f in clang pi32v2-ld LLVMgold.so llvm-nm llvm-objcopy; do
  [ -e "$root/bin/$f" ] || { echo "toolchain: missing bin/$f in $root" >&2; ls "$root/bin" | head -40; exit 6; }
done
"$root/bin/clang" --version | head -2
echo "FM1_PI32_TOOLCHAIN=$root" >> "${GITHUB_ENV:-/dev/stdout}"
