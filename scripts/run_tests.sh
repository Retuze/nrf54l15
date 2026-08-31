#!/usr/bin/env bash
# host 侧协议单测：host clang + host 运行（不进交叉编译，不需要板卡）。
# 用法：./scripts/run_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

cmake --fresh -S "$ROOT/tests" -B "$ROOT/build/tests" -G Ninja
cmake --build "$ROOT/build/tests"

rc=0
for t in "$ROOT"/build/tests/test_*; do
  echo "==> run $(basename "$t")"
  "$t" || rc=1
done
exit $rc
