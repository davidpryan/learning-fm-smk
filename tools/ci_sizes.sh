#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Where the flash goes: the biggest symbols of the app ELF (objdump -t of JieLi's toolchain), as one GitHub
# annotation so it can be read through the API. Usage: tools/ci_sizes.sh ELF TITLE [N]
set -euo pipefail
elf=$1; title=$2; n=${3:-70}
od="$JIELI_TOOLCHAIN/common/bin/objdump"
[ -f "$elf" ] || { echo "::notice title=$title sizes::no ELF (the link failed)"; exit 0; }
# objdump -t: "addr flags section size name"; keep .text/.rodata/.data-like sections, sort by size
"$od" -t "$elf" | awk '$4 ~ /^[0-9a-f]+$/ && NF >= 5 && $3 !~ /bss|\*ABS\*|\*UND\*/ { printf "%d %s %s\n", strtonum("0x"$4), $3, $NF }' \
  | sort -rn | head -n "$n" > /tmp/sizes.txt
{ printf "::notice title=%s top symbols::" "$title"; awk '{ printf "%s %s %s%%0A", $1, $2, $3 }' /tmp/sizes.txt; echo; } 
# section totals
"$od" -h "$elf" | awk '$2 ~ /^\./ { printf "%s %d\n", $2, strtonum("0x"$3) }' | sort -k2 -rn | head -20 > /tmp/secs.txt
{ printf "::notice title=%s sections::" "$title"; awk '{ printf "%s %s%%0A", $1, $2 }' /tmp/secs.txt; echo; }
