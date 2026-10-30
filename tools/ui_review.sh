#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# The UI review gallery: every page's main states on both boards, FM-1 left, SMK-37 right -> docs/ui-review/*.png
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/review docs/ui-review
for b in 0 1; do
  cc -O1 -w -DFELUCCA_LEARN=1 -DLEARN_UI_SMK=$b -Isloop/build/gen -Isloop/firmware/src -Isloop/firmware/hal \
     -Isloop/tests -Ilearn -o build/host/review_$b learn/tests/learn_review.c -lm
  build/host/review_$b build/review
done
python3 - <<'PY'
from PIL import Image
import glob, os
def key(n): return '-'.join(n.split('-')[:2])
fm = {key(os.path.basename(p)[4:-4]): os.path.basename(p)[4:-4] for p in glob.glob('build/review/fm1-*.ppm')}
sm = {key(os.path.basename(p)[4:-4]): os.path.basename(p)[4:-4] for p in glob.glob('build/review/smk-*.ppm')}
blank = Image.new('RGB', (240, 284), (24, 24, 24))
for k in sorted(set(fm) | set(sm)):
    a = Image.open(f'build/review/fm1-{fm[k]}.ppm') if k in fm else blank
    b = Image.open(f'build/review/smk-{sm[k]}.ppm') if k in sm else blank
    w = Image.new('RGB', (492, 284), (60, 60, 60)); w.paste(a, (0, 0)); w.paste(b, (252, 0))
    w.save(f'docs/ui-review/{sm.get(k, fm.get(k))}.png')
print("gallery: docs/ui-review/")
PY
