#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# The learn layer's host tests: every stage this tree has, on SLOOP's UI harness (gcc). Run from the repo root.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/host
CC=${CC:-cc}
test -f sloop/build/gen/felucca_font.h || (cd sloop && python3 -c "import sys; sys.path.insert(0,'tools'); import build; build.generate()")
for stage in 1 2; do
  $CC -O1 -w -DFELUCCA_LEARN=1 -DLEARN_STAGE=$stage -Isloop/build/gen -Isloop/firmware/src -Isloop/firmware/hal \
      -Isloop/tests -Ilearn -o build/host/learn_ui_test_s$stage learn/tests/learn_ui_test.c -lm
  build/host/learn_ui_test_s$stage build/host
done
for t in learn_s2_test; do
  $CC -O1 -w -DFELUCCA_LEARN=1 -DLEARN_STAGE=2 -Isloop/build/gen -Isloop/firmware/src -Isloop/firmware/hal \
      -Isloop/tests -Ilearn -o build/host/$t learn/tests/$t.c -lm
  build/host/$t build/host
done
# with -Wall -Wextra the learn sources themselves must be clean (SLOOP's own files are compiled with -w)
$CC -O1 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -DFELUCCA_LEARN=1 -DLEARN_STAGE=2 \
    -Isloop/build/gen -Isloop/firmware/src -Isloop/firmware/hal -Isloop/tests -Ilearn \
    -o build/host/learn_ui_test_w learn/tests/learn_ui_test.c -lm 2>&1 | grep -E "^learn/[a-z0-9_]+\.[ch]:" && { echo "warnings in learn/"; exit 1; } || true
echo "HOST TESTS PASSED"
