#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Where the flash goes: the biggest symbols and the sections of the app ELF (objdump of JieLi's toolchain), as
# GitHub annotations so they can be read through the API. Usage: tools/ci_sizes.sh ELF TITLE [N]
set -euo pipefail
elf=$1; title=$2; n=${3:-80}
od="$JIELI_TOOLCHAIN/common/bin/objdump"
[ -f "$elf" ] || { echo "::notice title=$title sizes::no ELF (the link failed)"; exit 0; }
"$od" -t "$elf" > /tmp/syms.txt
"$od" -h "$elf" > /tmp/secs.txt
python3 - "$title" "$n" <<'PY'
import re, sys
title, n = sys.argv[1], int(sys.argv[2])
syms = []
for l in open("/tmp/syms.txt"):
    f = l.split()
    if len(f) >= 5 and re.fullmatch(r"[0-9a-f]+", f[0]) and re.fullmatch(r"[0-9a-f]+", f[-2]) and not f[-3].startswith("*") and "bss" not in f[-3]:
        try: syms.append((int(f[-2], 16), f[-3], f[-1]))
        except ValueError: pass
syms.sort(reverse=True)
print("::notice title=%s top symbols::" % title + "%0A".join("%d %s %s" % s for s in syms[:n]))
secs = []
for l in open("/tmp/secs.txt"):
    f = l.split()
    if len(f) >= 3 and f[0].isdigit() and f[1].startswith("."):
        try: secs.append((int(f[2], 16), f[1]))
        except ValueError: pass
secs.sort(reverse=True)
print("::notice title=%s sections::" % title + "%0A".join("%s %d" % (s[1], s[0]) for s in secs[:24]))
PY
