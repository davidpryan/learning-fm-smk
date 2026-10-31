#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# The learn layer's host tests: every stage this tree has, on SLOOP's UI harness (gcc). Run from the repo root.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/host
CC=${CC:-cc}
test -f sloop/build/gen/felucca_font.h || (cd sloop && python3 -c "import sys; sys.path.insert(0,'tools'); import build; build.generate()")
for stage in 1 2 3 4; do
  $CC -O1 -w -DFELUCCA_LEARN=1 -DLEARN_STAGE=$stage -Isloop/build/gen -Isloop/firmware/src -Isloop/firmware/hal \
      -Isloop/tests -Ilearn -o build/host/learn_ui_test_s$stage learn/tests/learn_ui_test.c -lm
  build/host/learn_ui_test_s$stage build/host
done
for t in learn_s2_test learn_s3_test learn_s4_test; do
  $CC -O1 -w -DFELUCCA_LEARN=1 -DLEARN_STAGE=4 -Isloop/build/gen -Isloop/firmware/src -Isloop/firmware/hal \
      -Isloop/tests -Ilearn -o build/host/$t learn/tests/$t.c -lm
  build/host/$t build/host
done
# with -Wall -Wextra the learn sources themselves must be clean (SLOOP's own files are compiled with -w)
$CC -O1 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -DFELUCCA_LEARN=1 -DLEARN_STAGE=4 \
    -Isloop/build/gen -Isloop/firmware/src -Isloop/firmware/hal -Isloop/tests -Ilearn \
    -o build/host/learn_ui_test_w learn/tests/learn_ui_test.c -lm 2>&1 | grep -E "^learn/(tests/\.\./)?[a-z0-9_]+\.[ch]:" && { echo "warnings in learn/"; exit 1; } || true
# the whole layer under AddressSanitizer + UBSan (SLOOP's own engines shift negative values: that check is off, as in its suite)
for t in learn_ui_test learn_s2_test learn_s3_test learn_s4_test; do
  $CC -O1 -g -w -fsanitize=address,undefined -fno-sanitize=shift -fno-sanitize-recover=undefined -DFELUCCA_LEARN=1 -DLEARN_STAGE=4 \
      -Isloop/build/gen -Isloop/firmware/src -Isloop/firmware/hal -Isloop/tests -Ilearn -o build/host/san_$t learn/tests/$t.c -lm
  ASAN_OPTIONS=detect_leaks=0 build/host/san_$t build/host > build/host/san_$t.log 2>&1 || { tail -20 build/host/san_$t.log; echo "sanitizer: $t failed"; exit 1; }
done
# the review tour on both boards' screens: every text must fit the screen (draw.c counts what runs off the edge)
for b in 0 1; do
  $CC -O1 -w -DFELUCCA_LEARN=1 -DLEARN_UI_SMK=$b -Isloop/build/gen -Isloop/firmware/src -Isloop/firmware/hal \
      -Isloop/tests -Ilearn -o build/host/review_$b learn/tests/learn_review.c -lm
  mkdir -p build/host/review && build/host/review_$b build/host/review
done
echo "HOST TESTS PASSED"
