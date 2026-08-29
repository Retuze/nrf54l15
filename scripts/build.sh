#!/usr/bin/env bash
# 构建某个实验：./scripts/build.sh 01_conn
#
# 用 cmake --fresh 重建配置（等价"删 build/<name> 再配置"）：
# 改过 toolchain.cmake / targets/ / 任何 CMake 变量后无需手动删目录。
set -euo pipefail

NAME="${1:-01_conn}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/project/$NAME"

if [ ! -f "$SRC/CMakeLists.txt" ]; then
  echo "没找到实验项目：$SRC (用法: ./scripts/build.sh <project-name>)" >&2
  exit 1
fi

cmake --fresh -S "$SRC" -B "$ROOT/build/$NAME" -G Ninja
cmake --build "$ROOT/build/$NAME"

echo "--> 产物: $ROOT/build/$NAME/$NAME.elf / .hex / .bin"
